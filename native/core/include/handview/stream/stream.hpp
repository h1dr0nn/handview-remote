#pragma once

// Game-side streaming pipeline, shared by every engine host: game link,
// capture gating, encoder thread, flow control and touch delivery.
// The engine calls take_touches() once per frame on its main thread, then
// wants_frame() / submit_frame() after the frame is drawn.

#include "handview/flow/flow.hpp"
#include "handview/net/net.hpp"
#include "handview/protocol/messages.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace handview::stream {

struct TouchEvent {
    std::uint8_t id = 0;
    protocol::Phase phase = protocol::Phase::Began;
    float x = 0.0f;  // normalized 0..1 over the game viewport
    float y = 0.0f;
    float pressure = 0.0f;
};

struct StreamOptions {
    flow::BackpressureConfig backpressure{};
    flow::KeyframeConfig keyframe{};
};

struct StreamStatus {
    bool link_connected = false;
    bool streaming = false;
    std::string encoder;  // "NVENC", "MF Hardware", "MF Software" or empty
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t fps = 0;
    std::uint32_t bitrate_kbps = 0;
    std::uint32_t frames_sent = 0;
    std::string last_error;
    // The phone's video area (ViewSize), 0 x 0 until the phone sent one.
    std::uint16_t view_width = 0;
    std::uint16_t view_height = 0;
    std::uint16_t safe_left = 0, safe_top = 0, safe_right = 0, safe_bottom = 0;
};

class StreamController {
public:
    explicit StreamController(StreamOptions options = {}, net::LogSink log = {});
    ~StreamController();
    StreamController(const StreamController&) = delete;
    StreamController& operator=(const StreamController&) = delete;

    // Connects the game link and starts the worker thread.
    bool connect(std::uint16_t link_port, const std::array<std::uint8_t, 32>& token);
    void close();

    // Main thread, once per frame before the game update. Touch events
    // returned here count as processed by the frame drawn next.
    std::vector<TouchEvent> take_touches();

    // Main thread, after the frame is drawn: true when this frame should be
    // read back and submitted (streaming, in-flight limit, frame pacing).
    bool wants_frame(std::uint64_t now_us);

    // Copies the RGBA8 frame and hands it to the encoder thread.
    void submit_frame(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height, std::uint32_t stride);

    // Target capture rate while streaming (lowered by decoder backpressure).
    std::uint8_t target_fps() const;

    StreamStatus status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Microseconds on the QueryPerformanceCounter clock, as used on the wire.
std::uint64_t now_us();

}  // namespace handview::stream
