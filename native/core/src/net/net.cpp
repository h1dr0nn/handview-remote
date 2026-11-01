#include "handview/net/net.hpp"
#include <ws2tcpip.h>
#include <algorithm>
#include <climits>
#include <string>

namespace handview::net {
namespace {
std::mutex startup_mutex;
unsigned references = 0;
int ready(SOCKET socket, Milliseconds timeout, bool writing) {
    if (socket == INVALID_SOCKET) return SOCKET_ERROR;
    fd_set set; FD_ZERO(&set); FD_SET(socket, &set);
    timeval time{static_cast<long>(timeout.count() / 1000), static_cast<long>((timeout.count() % 1000) * 1000)};
    return select(0, writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &time);
}
sockaddr_in address(std::uint16_t port) {
    sockaddr_in result{}; result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(INADDR_LOOPBACK); result.sin_port = htons(port); return result;
}
}
WinsockInit::WinsockInit() {
    std::lock_guard lock(startup_mutex);
    WSADATA data{};
    valid_ = references != 0 || WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (valid_) ++references;
}
WinsockInit::~WinsockInit() { std::lock_guard lock(startup_mutex); if (valid_ && --references == 0) WSACleanup(); }
Socket& Socket::operator=(Socket&& other) noexcept { if (this != &other) { close(); value_ = other.value_.exchange(INVALID_SOCKET); } return *this; }
void Socket::close() { const auto s = value_.exchange(INVALID_SOCKET); if (s != INVALID_SOCKET) { shutdown(s, SD_BOTH); closesocket(s); } }
std::uint16_t Socket::port() const { sockaddr_in a{}; int length = sizeof(a); return getsockname(native_handle(), reinterpret_cast<sockaddr*>(&a), &length) == 0 ? ntohs(a.sin_port) : 0; }
bool set_nodelay(const Socket& socket) { int yes = 1; return setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes)) == 0; }
static Socket listen_on(std::uint16_t port, bool any) {
    Socket socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    int yes = 1;
    setsockopt(socket.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&yes), sizeof(yes));
    auto a = address(port);
    if (any) a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socket.native_handle(), reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 || listen(socket.native_handle(), SOMAXCONN) != 0) socket.close();
    return socket;
}
Socket listen_loopback(std::uint16_t port) { return listen_on(port, false); }
Socket listen_any(std::uint16_t port) { return listen_on(port, true); }
Socket connect_loopback(std::uint16_t port, Milliseconds timeout) {
    Socket socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (!socket) return socket;
    u_long mode = 1; ioctlsocket(socket.native_handle(), FIONBIO, &mode);
    const auto a = address(port);
    const int result = ::connect(socket.native_handle(), reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    if (result != 0 && (WSAGetLastError() != WSAEWOULDBLOCK || ready(socket.native_handle(), timeout, true) <= 0)) { socket.close(); return socket; }
    int error = 0; int length = sizeof(error);
    if (getsockopt(socket.native_handle(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0) { socket.close(); return socket; }
    mode = 0; ioctlsocket(socket.native_handle(), FIONBIO, &mode); set_nodelay(socket); return socket;
}
Socket accept(const Socket& listener, Milliseconds timeout) { if (ready(listener.native_handle(), timeout, false) <= 0) return Socket{}; Socket result{::accept(listener.native_handle(), nullptr, nullptr)}; set_nodelay(result); return result; }
bool send_all(const Socket& socket, std::span<const std::uint8_t> bytes) {
    while (!bytes.empty()) { const int n = ::send(socket.native_handle(), reinterpret_cast<const char*>(bytes.data()), static_cast<int>((std::min)(bytes.size(), static_cast<std::size_t>(INT_MAX))), 0); if (n <= 0) return false; bytes = bytes.subspan(static_cast<std::size_t>(n)); } return true;
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
