#pragma once

// Flow-control rules from protocol/SPEC.md sections 6.2 and 6.5. Pure logic:
// no clocks, no threads, no engine APIs. Every time value is passed in as
// microseconds by the caller.

#include <cstdint>

namespace handview::flow {

// Tracks last sent and newest acked frameSeq. All comparisons are serial
// number comparisons, so it stays correct across u32 wraparound.
class InFlightLimiter {
public:
    explicit InFlightLimiter(std::uint32_t max_in_flight = 1) : max_in_flight_(max_in_flight) {}

    // A seq that is not newer than the last sent seq is ignored.
    void on_frame_sent(std::uint32_t frame_seq);
    // Acks older than the newest ack, or newer than the last sent seq, are ignored.
    void on_ack(std::uint32_t frame_seq);

    // True when a new frame may be captured and encoded (in-flight <= max).
    bool can_encode() const;
    std::uint32_t in_flight() const;
    void reset();

private:
    std::uint32_t max_in_flight_;
    bool has_sent_ = false;
    std::uint32_t last_sent_ = 0;
    // Before the first ack this holds (first sent seq - 1) as a baseline.
    std::uint32_t last_acked_ = 0;
};

struct KeyframeConfig {
    std::uint64_t min_interval_us = 500000;
};

// Rate-limits IDR production. A denied request is remembered and becomes due
// once the minimum interval since the last IDR has passed.
class KeyframeLimiter {
public:
    explicit KeyframeLimiter(KeyframeConfig config = KeyframeConfig{}) : config_(config) {}

    // True when an IDR should be produced now. False when the last IDR went
    // out less than min_interval_us ago; the request is then kept pending.
    bool request(std::uint64_t now_us);
    // True when a previously denied request may now be served.
    bool pending_due(std::uint64_t now_us) const;
    // Call whenever an IDR is actually sent. Clears the pending request.
    void on_idr_sent(std::uint64_t now_us);
    void reset();

private:
    bool interval_passed(std::uint64_t now_us) const;

    KeyframeConfig config_;
    bool has_idr_ = false;
    std::uint64_t last_idr_us_ = 0;
    bool pending_ = false;
};

struct BackpressureConfig {
    std::uint16_t pending_threshold = 1;
    std::uint64_t sustain_us = 500000;
    std::uint64_t recover_after_us = 2000000;
    std::uint8_t min_fps = 20;
    std::uint8_t fps_step = 10;
    std::uint32_t min_bitrate_kbps = 2000;
    std::uint32_t bitrate_step_percent = 20;
};

// Lowers FPS first, then bitrate, while the decoder queue stays backed up, and
// recovers in reverse order (bitrate first, then FPS) once it is stable.
class DecoderBackpressure {
public:
    DecoderBackpressure(std::uint8_t nominal_fps, std::uint32_t nominal_bitrate_kbps,
                        BackpressureConfig config = BackpressureConfig{});

    void on_stats(std::uint16_t pending_outputs, std::uint64_t now_us);

    std::uint8_t target_fps() const { return target_fps_; }
    std::uint32_t target_bitrate_kbps() const { return target_bitrate_kbps_; }
    // True once after any target change, then false until the next change.
    bool changed();
    // Back to nominal targets with timers cleared.
    void reset();

private:
    std::uint8_t fps_at(std::uint32_t level) const;
    std::uint32_t bitrate_at(std::uint32_t level) const;
    void apply_levels();
    void degrade();
    void recover();

    BackpressureConfig config_;
    std::uint8_t nominal_fps_;
    std::uint32_t nominal_bitrate_kbps_;
    std::uint32_t max_fps_level_ = 0;
    std::uint32_t max_bitrate_level_ = 0;
    std::uint32_t fps_level_ = 0;
    std::uint32_t bitrate_level_ = 0;
    std::uint8_t target_fps_;
    std::uint32_t target_bitrate_kbps_;
    bool changed_ = false;
    bool bad_active_ = false;
    std::uint64_t bad_since_us_ = 0;
    bool good_active_ = false;
    std::uint64_t good_since_us_ = 0;
};

// Resolution Cap result. orientation: 0 Portrait, 1 Landscape.
struct StreamSize {
    std::uint16_t width;
    std::uint16_t height;
    std::uint8_t orientation;
};

inline constexpr std::uint8_t kOrientationPortrait = 0;
inline constexpr std::uint8_t kOrientationLandscape = 1;

// Stream size = min(game, phone screen, decoder limit) keeping the game's
// aspect ratio. Long and short sides are compared separately because the phone
// rotates to match the game. A zero decoder limit means unknown and is ignored.
// Never upscales; both dimensions are rounded down to even, minimum 2.
StreamSize cap_resolution(std::uint32_t game_w, std::uint32_t game_h, std::uint32_t screen_w,
                          std::uint32_t screen_h, std::uint32_t decoder_max_w,
                          std::uint32_t decoder_max_h);

// width * height * fps * 0.1 / 1000 (kbps, integer floor), clamped to [2000, 20000].
std::uint32_t default_bitrate_kbps(std::uint16_t width, std::uint16_t height, std::uint8_t fps);

}  // namespace handview::flow
