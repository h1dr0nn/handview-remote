#pragma once
#include "handview/protocol/messages.hpp"
#include <winsock2.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace handview::net {
using LogSink = std::function<void(std::string_view)>;
using Milliseconds = std::chrono::milliseconds;
class WinsockInit {
public:
    WinsockInit();
    ~WinsockInit();
    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
    bool valid() const { return valid_; }
private:
    bool valid_{};
};
class Socket {
public:
    explicit Socket(SOCKET value = INVALID_SOCKET) : value_(value) {}
    ~Socket() { close(); }
    Socket(Socket&& other) noexcept : value_(other.value_.exchange(INVALID_SOCKET)) {}
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    SOCKET native_handle() const { return value_.load(); }
    explicit operator bool() const { return native_handle() != INVALID_SOCKET; }
    void close();
    std::uint16_t port() const;
private:
    std::atomic<SOCKET> value_;
};
Socket listen_loopback(std::uint16_t port);
Socket connect_loopback(std::uint16_t port, Milliseconds timeout = Milliseconds{2000});
Socket accept(const Socket& listener, Milliseconds timeout = Milliseconds{100});
bool set_nodelay(const Socket& socket);
bool send_all(const Socket& socket, std::span<const std::uint8_t> bytes);
// Positive byte count, zero for EOF/error, -1 for timeout.
int recv_some(const Socket& socket, std::span<std::uint8_t> bytes, Milliseconds timeout);
struct Frame { protocol::Message message; std::vector<std::uint8_t> bytes; };
class FrameReader {
public:
    explicit FrameReader(protocol::Channel channel, LogSink log = {}) : channel_(channel), log_(std::move(log)) {}
    std::vector<Frame> feed(std::span<const std::uint8_t> bytes);
    bool terminal() const { return error_.has_value(); }
    std::optional<protocol::DecodeStatus> error() const { return error_; }
    std::uint64_t messages_seen() const { return messages_seen_; }
private:
    protocol::Channel channel_;
    LogSink log_;
    std::vector<std::uint8_t> buffer_;
    std::unordered_set<std::uint16_t> unknown_;
    std::optional<protocol::DecodeStatus> error_;
    std::uint64_t messages_seen_{};
};
class FrameWriter {
public:
    explicit FrameWriter(Socket& socket) : socket_(socket) {}
    bool send(const protocol::Message& message);
    bool send_bytes(std::span<const std::uint8_t> bytes);
private:
    Socket& socket_;
    std::mutex mutex_;
};
}
