#include "app/host/agent_channel.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace ps2r::app {

namespace {
constexpr size_t kMaxLineBytes = 64u << 20;   // a mem.write of a few MB base64 fits; anything larger is a fault
}

AgentChannel::~AgentChannel() { Close(); }

bool AgentChannel::Open(const std::string& path, int tcp_port, std::string* error) {
    if (open_.load()) { if (error) *error = "already open"; return false; }
    if (path.empty() && tcp_port <= 0) { if (error) *error = "no socket path and no TCP port"; return false; }
    if (!path.empty()) {
        sockaddr_un addr{};
        if (path.size() >= sizeof(addr.sun_path)) { if (error) *error = "socket path too long: " + path; return false; }
        unix_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (unix_fd_ < 0) { if (error) *error = std::string("socket(AF_UNIX): ") + std::strerror(errno); return false; }
        ::unlink(path.c_str());
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        // Owner-only from the first instant: the plane reads and writes game memory and has no authentication.
        const mode_t old_mask = ::umask(0177);
        const int rc = ::bind(unix_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::umask(old_mask);
        if (rc < 0) {
            if (error) *error = "bind(" + path + "): " + std::strerror(errno);
            ::close(unix_fd_);
            unix_fd_ = -1;
            return false;
        }
        ::chmod(path.c_str(), 0600);
        if (::listen(unix_fd_, 4) < 0) {
            if (error) *error = std::string("listen: ") + std::strerror(errno);
            ::close(unix_fd_);
            unix_fd_ = -1;
            ::unlink(path.c_str());
            return false;
        }
        path_ = path;
    }
    if (tcp_port > 0) {
        tcp_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_fd_ < 0) { if (error) *error = std::string("socket(AF_INET): ") + std::strerror(errno); Close(); return false; }
        const int one = 1;
        ::setsockopt(tcp_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(tcp_port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only, never a public interface
        if (::bind(tcp_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || ::listen(tcp_fd_, 4) < 0) {
            if (error) *error = "bind/listen 127.0.0.1:" + std::to_string(tcp_port) + ": " + std::strerror(errno);
            Close();
            return false;
        }
    }
    stopping_.store(false);
    open_.store(true);
    if (unix_fd_ >= 0) unix_acceptor_ = std::thread([this] { Accept(unix_fd_); });
    if (tcp_fd_ >= 0) tcp_acceptor_ = std::thread([this] { Accept(tcp_fd_); });
    return true;
}

void AgentChannel::Close() {
    if (!open_.load() && unix_fd_ < 0 && tcp_fd_ < 0) return;
    stopping_.store(true);
    open_.store(false);
    // Shutting down the listeners wakes the acceptors; shutting down the connections wakes the readers.
    if (unix_fd_ >= 0) { ::shutdown(unix_fd_, SHUT_RDWR); ::close(unix_fd_); }
    if (tcp_fd_ >= 0) { ::shutdown(tcp_fd_, SHUT_RDWR); ::close(tcp_fd_); }
    if (unix_acceptor_.joinable()) unix_acceptor_.join();
    if (tcp_acceptor_.joinable()) tcp_acceptor_.join();
    unix_fd_ = tcp_fd_ = -1;
    std::vector<std::shared_ptr<Connection>> all;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        all = connections_;
        all.insert(all.end(), closed_.begin(), closed_.end());
        connections_.clear();
        closed_.clear();
    }
    for (auto& c : all)
        if (c->fd >= 0) ::shutdown(c->fd, SHUT_RDWR);
    for (auto& c : all) {
        if (c->reader.joinable()) c->reader.join();
        if (c->fd >= 0) { ::close(c->fd); c->fd = -1; }
    }
    if (!path_.empty()) { ::unlink(path_.c_str()); path_.clear(); }
    queue_cv_.notify_all();
}

void AgentChannel::Accept(int listen_fd) {
    while (!stopping_.load()) {
        const int fd = ::accept(listen_fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;   // the listener was closed
        }
        if (stopping_.load()) { ::close(fd); break; }
#ifdef SO_NOSIGPIPE
        const int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        auto c = std::make_shared<Connection>();
        c->id = next_id_.fetch_add(1);
        c->fd = fd;
        accepted_.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.push_back(c);
        }
        c->reader = std::thread([this, c] { Read(c); });
    }
}

void AgentChannel::Read(std::shared_ptr<Connection> c) {
    std::string buf;
    char chunk[65536];
    for (;;) {
        const ssize_t n = ::recv(c->fd, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        buf.append(chunk, static_cast<size_t>(n));
        size_t start = 0;
        for (;;) {
            const size_t nl = buf.find('\n', start);
            if (nl == std::string::npos) break;
            std::string line = buf.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            start = nl + 1;
            if (!line.empty()) Enqueue(c->id, std::move(line));
        }
        buf.erase(0, start);
        if (buf.size() > kMaxLineBytes) break;
    }
    Drop(c->id);
}

void AgentChannel::Enqueue(uint64_t connection, std::string line) {
    lines_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(Request{connection, std::move(line)});
    }
    queue_cv_.notify_all();
}

void AgentChannel::Drop(uint64_t id) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (size_t i = 0; i < connections_.size(); ++i) {
        if (connections_[i]->id != id) continue;
        auto c = connections_[i];
        if (c->fd >= 0) { ::shutdown(c->fd, SHUT_RDWR); ::close(c->fd); c->fd = -1; }
        connections_.erase(connections_.begin() + static_cast<long>(i));
        closed_.push_back(c);   // its reader thread is this thread: joined on Close
        break;
    }
}

bool AgentChannel::Poll(Request* out) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (queue_.empty()) return false;
    *out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

bool AgentChannel::Wait(Request* out, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (queue_.empty()) queue_cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || stopping_.load(); });
    if (queue_.empty()) return false;
    *out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

bool AgentChannel::Send(uint64_t connection, const std::string& line) {
    std::shared_ptr<Connection> c;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& x : connections_)
            if (x->id == connection) { c = x; break; }
    }
    if (!c || c->fd < 0) return false;
    std::string out = line;
    out.push_back('\n');
    std::lock_guard<std::mutex> lock(c->write_mutex);
    size_t off = 0;
    while (off < out.size()) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send(c->fd, out.data() + off, out.size() - off, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(c->fd, out.data() + off, out.size() - off, 0);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

void AgentChannel::Broadcast(const std::string& line) {
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& x : connections_) ids.push_back(x->id);
    }
    for (uint64_t id : ids) Send(id, line);
}

size_t AgentChannel::Connections() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

}  // namespace ps2r::app
