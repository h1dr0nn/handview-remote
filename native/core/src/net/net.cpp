#include "handview/net/net.hpp"
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif
#include <algorithm>
#include <climits>
#include <string>

namespace handview::net {
namespace {
#ifdef _WIN32
using AddressLength = int;
constexpr int kSendFlags = 0;
std::mutex startup_mutex;
unsigned references = 0;
// Positive when ready, zero on timeout, negative on error.
int ready(NativeSocket socket, Milliseconds timeout, bool writing) {
    if (socket == kInvalidSocket) return SOCKET_ERROR;
    fd_set set; FD_ZERO(&set); FD_SET(socket, &set);
    timeval time{static_cast<long>(timeout.count() / 1000), static_cast<long>((timeout.count() % 1000) * 1000)};
    return select(0, writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &time);
}
void close_native(NativeSocket s) { shutdown(s, SD_BOTH); closesocket(s); }
bool set_blocking(NativeSocket s, bool blocking) { u_long mode = blocking ? 0 : 1; return ioctlsocket(s, FIONBIO, &mode) == 0; }
bool connect_pending() { return WSAGetLastError() == WSAEWOULDBLOCK; }
// Windows lets another socket bind the same port unless this is set.
void exclusive_bind(NativeSocket s) { int yes = 1; setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes)); }
#else
using AddressLength = socklen_t;
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
int ready(NativeSocket socket, Milliseconds timeout, bool writing) {
    if (socket == kInvalidSocket) return -1;
    pollfd entry{socket, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    int result;
    do result = poll(&entry, 1, static_cast<int>(std::clamp<long long>(timeout.count(), 0, INT_MAX)));
    while (result < 0 && errno == EINTR);
    return result;
}
// shutdown() wakes a thread blocked in poll() on the same socket.
void close_native(NativeSocket s) { shutdown(s, SHUT_RDWR); ::close(s); }
bool set_blocking(NativeSocket s, bool blocking) {
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) == 0;
}
bool connect_pending() { return errno == EINPROGRESS; }
// Without SO_REUSEADDR a POSIX bind is already exclusive.
void exclusive_bind(NativeSocket) {}
#endif
// A peer that went away must fail the send, not raise SIGPIPE in the host process.
NativeSocket new_socket() {
    const NativeSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef SO_NOSIGPIPE
    if (s != kInvalidSocket) { int yes = 1; setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)); }
#endif
    return s;
}
sockaddr_in address(std::uint16_t port) {
    sockaddr_in result{}; result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(INADDR_LOOPBACK); result.sin_port = htons(port); return result;
}
}
#ifdef _WIN32
WinsockInit::WinsockInit() {
    std::lock_guard lock(startup_mutex);
    WSADATA data{};
    valid_ = references != 0 || WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (valid_) ++references;
}
WinsockInit::~WinsockInit() { std::lock_guard lock(startup_mutex); if (valid_ && --references == 0) WSACleanup(); }
#else
WinsockInit::WinsockInit() : valid_(true) {}
WinsockInit::~WinsockInit() = default;
#endif
Socket& Socket::operator=(Socket&& other) noexcept { if (this != &other) { close(); value_ = other.value_.exchange(kInvalidSocket); } return *this; }
void Socket::close() { const auto s = value_.exchange(kInvalidSocket); if (s != kInvalidSocket) close_native(s); }
std::uint16_t Socket::port() const { sockaddr_in a{}; AddressLength length = sizeof(a); return getsockname(native_handle(), reinterpret_cast<sockaddr*>(&a), &length) == 0 ? ntohs(a.sin_port) : 0; }
bool set_nodelay(const Socket& socket) { int yes = 1; return setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes)) == 0; }
static Socket listen_on(std::uint16_t port, bool any) {
    Socket socket{new_socket()};
    exclusive_bind(socket.native_handle());
    auto a = address(port);
    if (any) a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket.native_handle(), reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 || listen(socket.native_handle(), SOMAXCONN) != 0) socket.close();
    return socket;
}
Socket listen_loopback(std::uint16_t port) { return listen_on(port, false); }
Socket listen_any(std::uint16_t port) { return listen_on(port, true); }
Socket connect_loopback(std::uint16_t port, Milliseconds timeout) {
    Socket socket{new_socket()};
    if (!socket) return socket;
    set_blocking(socket.native_handle(), false);
    const auto a = address(port);
    const int result = ::connect(socket.native_handle(), reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    if (result != 0 && (!connect_pending() || ready(socket.native_handle(), timeout, true) <= 0)) { socket.close(); return socket; }
    int error = 0; AddressLength length = sizeof(error);
    if (getsockopt(socket.native_handle(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0) { socket.close(); return socket; }
    set_blocking(socket.native_handle(), true); set_nodelay(socket); return socket;
}
Socket accept(const Socket& listener, Milliseconds timeout) { if (ready(listener.native_handle(), timeout, false) <= 0) return Socket{}; Socket result{::accept(listener.native_handle(), nullptr, nullptr)};
#ifdef SO_NOSIGPIPE
    if (result) { int yes = 1; setsockopt(result.native_handle(), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes)); }
#endif
    set_nodelay(result); return result; }
bool send_all(const Socket& socket, std::span<const std::uint8_t> bytes) {
    while (!bytes.empty()) { const int n = ::send(socket.native_handle(), reinterpret_cast<const char*>(bytes.data()), static_cast<int>((std::min)(bytes.size(), static_cast<std::size_t>(INT_MAX))), kSendFlags); if (n <= 0) return false; bytes = bytes.subspan(static_cast<std::size_t>(n)); } return true;
}
int recv_some(const Socket& socket, std::span<std::uint8_t> bytes, Milliseconds timeout) { const int r = ready(socket.native_handle(), timeout, false); if (r == 0) return -1; if (r < 0) return 0; const int n = recv(socket.native_handle(), reinterpret_cast<char*>(bytes.data()), static_cast<int>((std::min)(bytes.size(), static_cast<std::size_t>(INT_MAX))), 0); return n > 0 ? n : 0; }
std::vector<Frame> FrameReader::feed(std::span<const std::uint8_t> bytes) {
    std::vector<Frame> frames;
    if (terminal()) return frames;
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    std::size_t offset = 0;
    while (offset < buffer_.size()) {
        auto result = protocol::decode(buffer_.data() + offset, buffer_.size() - offset, channel_);
        if (result.status == protocol::DecodeStatus::NeedMore) break;
        if (result.status == protocol::DecodeStatus::UnknownType) {
            const auto type = std::get<protocol::Unknown>(result.message).type;
            if (unknown_.insert(type).second && log_) log_("Unknown message type " + std::to_string(type));
        } else if (result.status == protocol::DecodeStatus::Ok) {
            frames.push_back({std::move(result.message), {buffer_.begin() + static_cast<std::ptrdiff_t>(offset), buffer_.begin() + static_cast<std::ptrdiff_t>(offset + result.consumed)}});
        } else { error_ = result.status; buffer_.clear(); return frames; }
        ++messages_seen_;
        offset += result.consumed;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset)); return frames;
}
bool FrameWriter::send(const protocol::Message& message) { const auto bytes = protocol::encode(message); return send_bytes(bytes); }
bool FrameWriter::send_bytes(std::span<const std::uint8_t> bytes) {
    std::lock_guard lock(mutex_);
    if (send_all(socket_, bytes)) return true;
    socket_.close();
    return false;
}
}
