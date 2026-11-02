#include "app/host/agent_channel.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>   // AF_UNIX stream sockets: Windows 10 1803 and later
#include <cstdio>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>

namespace ps2r::app {

namespace {
constexpr size_t kMaxLineBytes = 64u << 20;   // a mem.write of a few MB base64 fits; anything larger is a fault

// The socket calls the channel makes, spelled once per platform (Winsock: SOCKET handles, closesocket,
// WSAGetLastError, char buffers and int lengths; the Unix-domain socket's file removed with std::remove).
#ifdef _WIN32
constexpr int kShutBoth = SD_BOTH;
SOCKET Sock(intptr_t fd) { return static_cast<SOCKET>(fd); }
intptr_t NewSocket(int family) {
    static std::once_flag started;
    std::call_once(started, [] { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); });
    const SOCKET s = ::socket(family, SOCK_STREAM, 0);
    return s == INVALID_SOCKET ? -1 : static_cast<intptr_t>(s);
}
void CloseSocket(intptr_t fd) { ::closesocket(Sock(fd)); }
void RemovePath(const std::string& path) { std::remove(path.c_str()); }
bool Interrupted() { return WSAGetLastError() == WSAEINTR; }
std::string SocketError() { return "Winsock error " + std::to_string(WSAGetLastError()); }
long long Recv(intptr_t fd, char* buf, size_t n) { return ::recv(Sock(fd), buf, static_cast<int>(n), 0); }
long long SendSome(intptr_t fd, const char* buf, size_t n) { return ::send(Sock(fd), buf, static_cast<int>(n), 0); }
#else
constexpr int kShutBoth = SHUT_RDWR;
int Sock(intptr_t fd) { return static_cast<int>(fd); }
intptr_t NewSocket(int family) { return ::socket(family, SOCK_STREAM, 0); }
void CloseSocket(intptr_t fd) { ::close(Sock(fd)); }
void RemovePath(const std::string& path) { ::unlink(path.c_str()); }
bool Interrupted() { return errno == EINTR; }
std::string SocketError() { return std::strerror(errno); }
long long Recv(intptr_t fd, char* buf, size_t n) { return ::recv(Sock(fd), buf, n, 0); }
long long SendSome(intptr_t fd, const char* buf, size_t n) {
#ifdef MSG_NOSIGNAL
    return ::send(Sock(fd), buf, n, MSG_NOSIGNAL);
#else
    return ::send(Sock(fd), buf, n, 0);
#endif
}
#endif
}  // namespace

AgentChannel::~AgentChannel() { Close(); }

bool AgentChannel::Open(const std::string& path, int tcp_port, std::string* error) {
    if (open_.load()) { if (error) *error = "already open"; return false; }
    if (path.empty() && tcp_port <= 0) { if (error) *error = "no socket path and no TCP port"; return false; }
    if (!path.empty()) {
        sockaddr_un addr{};
        if (path.size() >= sizeof(addr.sun_path)) { if (error) *error = "socket path too long: " + path; return false; }
        unix_fd_ = NewSocket(AF_UNIX);
        if (unix_fd_ < 0) { if (error) *error = "socket(AF_UNIX): " + SocketError(); return false; }
        RemovePath(path);
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        // Owner-only from the first instant: the plane reads and writes game memory and has no authentication.
        // (Windows: the socket file takes its folder's access list.)
#ifndef _WIN32
        const mode_t old_mask = ::umask(0177);
#endif
        const int rc = ::bind(Sock(unix_fd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#ifndef _WIN32
        ::umask(old_mask);
#endif
        if (rc < 0) {
            if (error) *error = "bind(" + path + "): " + SocketError();
            CloseSocket(unix_fd_);
            unix_fd_ = -1;
            return false;
        }
#ifndef _WIN32
        ::chmod(path.c_str(), 0600);
#endif
        if (::listen(Sock(unix_fd_), 4) < 0) {
            if (error) *error = "listen: " + SocketError();
            CloseSocket(unix_fd_);
            unix_fd_ = -1;
            RemovePath(path);
            return false;
        }
        path_ = path;
    }
    if (tcp_port > 0) {
        tcp_fd_ = NewSocket(AF_INET);
        if (tcp_fd_ < 0) { if (error) *error = "socket(AF_INET): " + SocketError(); Close(); return false; }
        const int one = 1;
        ::setsockopt(Sock(tcp_fd_), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(tcp_port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only, never a public interface
        if (::bind(Sock(tcp_fd_), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || ::listen(Sock(tcp_fd_), 4) < 0) {
            if (error) *error = "bind/listen 127.0.0.1:" + std::to_string(tcp_port) + ": " + SocketError();
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
    if (unix_fd_ >= 0) { ::shutdown(Sock(unix_fd_), kShutBoth); CloseSocket(unix_fd_); }
    if (tcp_fd_ >= 0) { ::shutdown(Sock(tcp_fd_), kShutBoth); CloseSocket(tcp_fd_); }
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
        if (c->fd >= 0) ::shutdown(Sock(c->fd), kShutBoth);
    for (auto& c : all) {
        if (c->reader.joinable()) c->reader.join();
        if (c->fd >= 0) { CloseSocket(c->fd); c->fd = -1; }
    }
    if (!path_.empty()) { RemovePath(path_); path_.clear(); }
    queue_cv_.notify_all();
}

void AgentChannel::Accept(intptr_t listen_fd) {
    while (!stopping_.load()) {
#ifdef _WIN32
        const SOCKET s = ::accept(Sock(listen_fd), nullptr, nullptr);
        const intptr_t fd = s == INVALID_SOCKET ? -1 : static_cast<intptr_t>(s);
#else
        const intptr_t fd = ::accept(Sock(listen_fd), nullptr, nullptr);
#endif
        if (fd < 0) {
            if (Interrupted()) continue;
            break;   // the listener was closed
        }
        if (stopping_.load()) { CloseSocket(fd); break; }
#ifdef SO_NOSIGPIPE
        const int one = 1;
        ::setsockopt(Sock(fd), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
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
        const long long n = Recv(c->fd, chunk, sizeof(chunk));
        if (n < 0 && Interrupted()) continue;
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
        if (c->fd >= 0) { ::shutdown(Sock(c->fd), kShutBoth); CloseSocket(c->fd); c->fd = -1; }
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
        const long long n = SendSome(c->fd, out.data() + off, out.size() - off);
        if (n < 0 && Interrupted()) continue;
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
