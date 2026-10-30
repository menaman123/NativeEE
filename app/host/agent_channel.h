// AgentChannel: the transport of the agent control plane -- a UNIX domain socket (the file is created
// owner-only, 0600) and, only when asked, a TCP listener bound to 127.0.0.1. Newline-delimited JSON
// text both ways. Several clients may be connected at once; every request carries the connection it
// came from so its reply goes back there, events go to every connection.
//
// Threads: one acceptor per listener and one reader per connection, all owned here. They only parse
// bytes into lines and queue them; nothing of the game is touched off the game thread. The server
// (agent_server.h) pops requests on the game thread with Poll / Wait and writes replies with Send.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ps2r::app {

class AgentChannel {
public:
    struct Request {
        uint64_t connection = 0;   // the connection id; 0 = none (closed)
        std::string line;          // one JSON text, without the newline
    };

    AgentChannel() = default;
    ~AgentChannel();
    AgentChannel(const AgentChannel&) = delete;
    AgentChannel& operator=(const AgentChannel&) = delete;

    // Binds the UNIX socket at `path` (an existing file there is replaced) and, when tcp_port > 0, TCP
    // 127.0.0.1:tcp_port; starts the acceptors. False names the reason.
    bool Open(const std::string& path, int tcp_port, std::string* error);
    // Stops the acceptors and readers, closes every connection, unlinks the socket file.
    void Close();
    bool IsOpen() const { return open_.load(); }

    // Pops the next queued request (false when none). Wait blocks until one arrives or `timeout` passes.
    bool Poll(Request* out);
    bool Wait(Request* out, std::chrono::milliseconds timeout);
    // Appends a newline and writes to one connection (false: gone) / to every connection.
    bool Send(uint64_t connection, const std::string& line);
    void Broadcast(const std::string& line);
    size_t Connections() const;
    // Census.
    uint64_t Accepted() const { return accepted_.load(); }
    uint64_t Lines() const { return lines_.load(); }

private:
    struct Connection {
        uint64_t id = 0;
        int fd = -1;
        std::thread reader;
        std::mutex write_mutex;
    };
    void Accept(int listen_fd);
    void Read(std::shared_ptr<Connection> c);
    void Enqueue(uint64_t connection, std::string line);
    void Drop(uint64_t id);

    std::atomic<bool> open_{false};
    std::atomic<bool> stopping_{false};
    std::string path_;
    int unix_fd_ = -1, tcp_fd_ = -1;
    std::thread unix_acceptor_, tcp_acceptor_;
    mutable std::mutex connections_mutex_;
    std::vector<std::shared_ptr<Connection>> connections_;
    std::vector<std::shared_ptr<Connection>> closed_;   // readers that ended; joined on Close
    std::atomic<uint64_t> next_id_{1};
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<Request> queue_;
    std::atomic<uint64_t> accepted_{0}, lines_{0};
};

}  // namespace ps2r::app
