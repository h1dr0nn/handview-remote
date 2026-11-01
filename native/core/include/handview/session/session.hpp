#pragma once
#include "handview/net/net.hpp"
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

namespace handview::session {
template<class T> class Queue {
public:
    void push(T value) { std::lock_guard lock(mutex_); values_.push_back(std::move(value)); }
    std::optional<T> pop() { std::lock_guard lock(mutex_); if (values_.empty()) return {}; auto value = std::move(values_.front()); values_.pop_front(); return value; }
private:
    std::mutex mutex_;
    std::deque<T> values_;
};
class GameLink {
public:
    // The owner serializes connect/close. Sending and polling may run concurrently.
    explicit GameLink(net::LogSink log = {});
    ~GameLink();
    bool connect(std::uint16_t port, const std::array<std::uint8_t,32>& token);
    void close();
    bool connected() const;
    std::optional<protocol::Message> poll();
    bool send_video_frame(const protocol::VideoFrame& frame);
    bool send_stream_config_changed(const protocol::StreamConfigChanged& config);
    bool send_error(protocol::ErrorSource source, protocol::ErrorCode code);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct ServerConfig {
    protocol::Welcome host{ {}, protocol::Host::Godot, protocol::Codec::H264, protocol::Encoder::MfHardware, 1280, 720, protocol::Orientation::Landscape, 60, 8000, protocol::EditorStateValue::Stopped };
    std::chrono::milliseconds ping_interval{1000};
    std::chrono::milliseconds timeout{5000};
    net::LogSink log{};
};
struct SessionEvent {
    enum class Kind { connected, disconnected, game_disconnected, phone_error };
    Kind kind{};
    protocol::Hello hello{};
    protocol::ByeReason reason{protocol::ByeReason::Normal};
    std::uint32_t process_id{};
    bool timed_out{};
    protocol::ErrorSource source{};
    protocol::ErrorCode code{};
};
class EditorServer {
public:
    // The owner serializes start/stop. Sending and polling may run concurrently.
    explicit EditorServer(ServerConfig config = {});
    ~EditorServer();
    // No argument selects the first free port in 38500..38509. Zero selects an OS port.
    bool start();
    bool start(std::uint16_t port);
    void stop();
    std::uint16_t port() const;
    std::uint16_t link_port() const;
    void set_pending_token(std::array<std::uint8_t,32> token);
    void set_link_token(std::array<std::uint8_t,32> token);
    std::optional<protocol::Command> poll_command();
    std::optional<SessionEvent> poll_event();
    void command_result(protocol::Cmd cmd, protocol::Result result, protocol::EditorStateValue state);
    void editor_state(protocol::EditorStateValue state);
    // Welcome fields for the next session; also the fps and bitrate sent in LinkStart.
    void set_stream_info(protocol::Encoder encoder, std::uint16_t width, std::uint16_t height,
                         protocol::Orientation orientation, std::uint8_t fps, std::uint32_t bitrate_kbps);
    void stream_paused(protocol::PauseReason reason);
    void error(protocol::ErrorSource source, protocol::ErrorCode code);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
