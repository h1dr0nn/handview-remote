// Tests for handview::flow: in-flight limiter, keyframe limiter, decoder
// backpressure, resolution cap and default bitrate.

#include <cstdint>
#include <cstdio>

#include "handview/flow/flow.hpp"

namespace {

int g_failures = 0;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (false)

using namespace handview::flow;

// ---- InFlightLimiter ----

void test_inflight_basics() {
    InFlightLimiter limiter;
    CHECK(limiter.can_encode());
    CHECK(limiter.in_flight() == 0);

    limiter.on_frame_sent(5);
    CHECK(limiter.in_flight() == 1);
    CHECK(limiter.can_encode());

    limiter.on_frame_sent(6);
    CHECK(limiter.in_flight() == 2);
    CHECK(!limiter.can_encode());

    limiter.on_ack(5);
    CHECK(limiter.in_flight() == 1);
    CHECK(limiter.can_encode());

    limiter.on_ack(6);
    CHECK(limiter.in_flight() == 0);
    CHECK(limiter.can_encode());

    limiter.reset();
    CHECK(limiter.in_flight() == 0);
    CHECK(limiter.can_encode());
}

void test_inflight_first_seq_zero() {
    InFlightLimiter limiter;
    limiter.on_frame_sent(0);
    CHECK(limiter.in_flight() == 1);
    limiter.on_frame_sent(1);
    CHECK(limiter.in_flight() == 2);
    limiter.on_ack(0);
    CHECK(limiter.in_flight() == 1);
}

void test_inflight_ack_ordering() {
    InFlightLimiter limiter;

    // Ack before any frame was sent is ignored.
    limiter.on_ack(3);
    CHECK(limiter.in_flight() == 0);

    limiter.on_frame_sent(1);
    limiter.on_frame_sent(2);
    limiter.on_frame_sent(3);
    CHECK(limiter.in_flight() == 3);

    limiter.on_ack(3);
    CHECK(limiter.in_flight() == 0);

    // An older ack must not move the newest ack backwards.
    limiter.on_ack(1);
    CHECK(limiter.in_flight() == 0);

    // Duplicate send of an old seq does not change anything.
    limiter.on_frame_sent(2);
    CHECK(limiter.in_flight() == 0);

    // An ack for a frame that was never sent is ignored.
    limiter.on_frame_sent(4);
    limiter.on_ack(10);
    CHECK(limiter.in_flight() == 1);
    limiter.on_ack(4);
    CHECK(limiter.in_flight() == 0);
}

void test_inflight_skipped_acks() {
    InFlightLimiter limiter;
    limiter.on_frame_sent(1);
    limiter.on_frame_sent(2);
    limiter.on_frame_sent(3);
    // Acking 2 implicitly covers 1.
    limiter.on_ack(2);
    CHECK(limiter.in_flight() == 1);
}

void test_inflight_wraparound() {
    InFlightLimiter limiter;
    limiter.on_frame_sent(0xFFFFFFFEu);
    limiter.on_frame_sent(0xFFFFFFFFu);
    limiter.on_frame_sent(0u);
    limiter.on_frame_sent(1u);
    CHECK(limiter.in_flight() == 4);
    CHECK(!limiter.can_encode());

    limiter.on_ack(0xFFFFFFFFu);
    CHECK(limiter.in_flight() == 2);
    CHECK(!limiter.can_encode());

    limiter.on_ack(0u);
    CHECK(limiter.in_flight() == 1);
    CHECK(limiter.can_encode());

    // An ack from before the wrap is older than the newest ack.
    limiter.on_ack(0xFFFFFFFEu);
    CHECK(limiter.in_flight() == 1);

    limiter.on_ack(1u);
    CHECK(limiter.in_flight() == 0);
    CHECK(limiter.can_encode());

    limiter.on_frame_sent(2u);
    CHECK(limiter.in_flight() == 1);
    // Future ack across the wrap boundary is still rejected.
    limiter.on_ack(0x80000010u);
    CHECK(limiter.in_flight() == 1);
}

void test_inflight_first_frame_at_wrap() {
    InFlightLimiter limiter;
    limiter.on_frame_sent(0xFFFFFFFFu);
    CHECK(limiter.in_flight() == 1);
    limiter.on_frame_sent(0u);
    CHECK(limiter.in_flight() == 2);
    limiter.on_ack(0xFFFFFFFFu);
    CHECK(limiter.in_flight() == 1);
}

void test_inflight_custom_max() {
    InFlightLimiter strict(0);
    CHECK(strict.can_encode());
    strict.on_frame_sent(1);
    CHECK(!strict.can_encode());
    strict.on_ack(1);
    CHECK(strict.can_encode());

    InFlightLimiter loose(3);
    loose.on_frame_sent(1);
    loose.on_frame_sent(2);
    loose.on_frame_sent(3);
    CHECK(loose.can_encode());
    loose.on_frame_sent(4);
    CHECK(!loose.can_encode());
}

// ---- KeyframeLimiter ----

void test_keyframe_first_request_allowed() {
    KeyframeLimiter limiter;
    CHECK(!limiter.pending_due(0));
    CHECK(limiter.request(0));
}

void test_keyframe_denial_then_due() {
    KeyframeLimiter limiter;
    limiter.on_idr_sent(1000000);

    CHECK(!limiter.request(1100000));
    CHECK(!limiter.pending_due(1100000));
    CHECK(!limiter.pending_due(1499999));
    CHECK(limiter.pending_due(1500000));
    CHECK(limiter.pending_due(1600000));

    // Serving it clears the pending request.
    limiter.on_idr_sent(1600000);
    CHECK(!limiter.pending_due(5000000));
}

void test_keyframe_allowed_after_interval() {
    KeyframeLimiter limiter;
    limiter.on_idr_sent(0);
    CHECK(!limiter.request(499999));
    CHECK(limiter.request(500000));
}

void test_keyframe_no_pending_without_denial() {
    KeyframeLimiter limiter;
    limiter.on_idr_sent(0);
    CHECK(!limiter.pending_due(10000000));
}

void test_keyframe_custom_interval_and_reset() {
    KeyframeConfig config;
    config.min_interval_us = 100;
    KeyframeLimiter limiter(config);
    limiter.on_idr_sent(50);
    CHECK(!limiter.request(100));
    CHECK(limiter.pending_due(150));

    limiter.reset();
    CHECK(!limiter.pending_due(150));
    CHECK(limiter.request(51));
}

void test_keyframe_clock_backwards() {
    KeyframeLimiter limiter;
    limiter.on_idr_sent(1000000);
    CHECK(!limiter.request(500));
    CHECK(!limiter.pending_due(500));
}

// ---- DecoderBackpressure ----

constexpr std::uint64_t kSustainStep = 500001;
constexpr std::uint64_t kRecoverStep = 2000000;

// Advances time and feeds a backed-up decoder sample.
void bad_step(DecoderBackpressure& bp, std::uint64_t& now) {
    now += kSustainStep;
    bp.on_stats(2, now);
}

// Advances time and feeds a healthy decoder sample.
void good_step(DecoderBackpressure& bp, std::uint64_t& now) {
    now += kRecoverStep;
    bp.on_stats(0, now);
}

void test_backpressure_initial() {
    DecoderBackpressure bp(60, 10000);
    CHECK(bp.target_fps() == 60);
    CHECK(bp.target_bitrate_kbps() == 10000);
    CHECK(!bp.changed());
}

void test_backpressure_degrade_order() {
    DecoderBackpressure bp(60, 10000);
    std::uint64_t now = 0;
    bp.on_stats(2, now);
    CHECK(!bp.changed());

    // FPS goes down first: 60 -> 50 -> 40 -> 30 -> 20, bitrate untouched.
    const std::uint8_t expected_fps[] = {50, 40, 30, 20};
    for (std::uint8_t fps : expected_fps) {
        bad_step(bp, now);
        CHECK(bp.target_fps() == fps);
        CHECK(bp.target_bitrate_kbps() == 10000);
        CHECK(bp.changed());
        CHECK(!bp.changed());
    }

    // Then bitrate: 20 percent per step.
    bad_step(bp, now);
    CHECK(bp.target_fps() == 20);
    CHECK(bp.target_bitrate_kbps() == 8000);
    CHECK(bp.changed());

    bad_step(bp, now);
    CHECK(bp.target_fps() == 20);
    CHECK(bp.target_bitrate_kbps() == 6400);
    CHECK(bp.changed());
}

void test_backpressure_floors() {
    DecoderBackpressure bp(60, 10000);
    std::uint64_t now = 0;
    bp.on_stats(2, now);
    for (int i = 0; i < 40; ++i) {
        bad_step(bp, now);
    }
    CHECK(bp.target_fps() == 20);
    CHECK(bp.target_bitrate_kbps() == 2000);
    CHECK(bp.changed());

    // At the floors nothing changes any more.
    bad_step(bp, now);
    CHECK(bp.target_fps() == 20);
    CHECK(bp.target_bitrate_kbps() == 2000);
    CHECK(!bp.changed());
}

void test_backpressure_low_nominal() {
    // Nominal fps already below the floor: fps is never touched.
    DecoderBackpressure low_fps(15, 10000);
    std::uint64_t now = 0;
    low_fps.on_stats(2, now);
    bad_step(low_fps, now);
    CHECK(low_fps.target_fps() == 15);
    CHECK(low_fps.target_bitrate_kbps() == 8000);

    // Nominal bitrate at or below the floor: bitrate is never touched.
    DecoderBackpressure low_rate(30, 1500);
    now = 0;
    low_rate.on_stats(2, now);
    bad_step(low_rate, now);
    CHECK(low_rate.target_fps() == 20);
    bad_step(low_rate, now);
    CHECK(low_rate.target_fps() == 20);
    CHECK(low_rate.target_bitrate_kbps() == 1500);

    // A step larger than the remaining span clamps to the floor.
    DecoderBackpressure odd(25, 10000);
    now = 0;
    odd.on_stats(2, now);
    bad_step(odd, now);
    CHECK(odd.target_fps() == 20);
}

void test_backpressure_sustain_timing() {
    DecoderBackpressure bp(60, 10000);
    bp.on_stats(2, 0);
    // Exactly sustain_us is not "more than" sustain_us.
    bp.on_stats(2, 500000);
    CHECK(bp.target_fps() == 60);
    CHECK(!bp.changed());
    bp.on_stats(2, 500001);
    CHECK(bp.target_fps() == 50);
    CHECK(bp.changed());

    // The timer restarts after a step.
    bp.on_stats(2, 1000001);
    CHECK(bp.target_fps() == 50);
    bp.on_stats(2, 1000002);
    CHECK(bp.target_fps() == 40);
}

void test_backpressure_threshold_is_exclusive() {
    DecoderBackpressure bp(60, 10000);
    bp.on_stats(1, 0);
    bp.on_stats(1, 600000);
    bp.on_stats(1, 5000000);
    CHECK(bp.target_fps() == 60);
    CHECK(!bp.changed());
}

void test_backpressure_spikes_do_not_degrade() {
    DecoderBackpressure bp(60, 10000);
    bp.on_stats(2, 0);
    bp.on_stats(2, 400000);
    bp.on_stats(0, 450000);
    // A new bad run starts here and is only 400 ms long at its end.
    bp.on_stats(2, 500000);
    bp.on_stats(2, 900000);
    CHECK(bp.target_fps() == 60);
    CHECK(!bp.changed());

    // Repeated short spikes separated by healthy samples never add up.
    bp.on_stats(0, 950000);
    std::uint64_t now = 1000000;
    for (int i = 0; i < 20; ++i) {
        bp.on_stats(3, now);
        now += 300000;
        bp.on_stats(3, now);
        now += 100000;
        bp.on_stats(0, now);
        now += 100000;
    }
    CHECK(bp.target_fps() == 60);
    CHECK(bp.target_bitrate_kbps() == 10000);
    CHECK(!bp.changed());
}

void test_backpressure_recovery_order() {
    DecoderBackpressure bp(60, 10000);
    std::uint64_t now = 0;
    bp.on_stats(2, now);
    // Degrade to fps 20 and bitrate 6400 (6 steps).
    for (int i = 0; i < 6; ++i) {
        bad_step(bp, now);
    }
    CHECK(bp.target_fps() == 20);
    CHECK(bp.target_bitrate_kbps() == 6400);
    CHECK(bp.changed());

    // Healthy samples start the recovery timer.
    bp.on_stats(0, now);
    CHECK(!bp.changed());

    // Bitrate recovers first, exactly back through the same steps.
    good_step(bp, now);
    CHECK(bp.target_bitrate_kbps() == 8000);
    CHECK(bp.target_fps() == 20);
    CHECK(bp.changed());

    good_step(bp, now);
    CHECK(bp.target_bitrate_kbps() == 10000);
    CHECK(bp.target_fps() == 20);
    CHECK(bp.changed());

    // Then fps.
    const std::uint8_t expected_fps[] = {30, 40, 50, 60};
    for (std::uint8_t fps : expected_fps) {
        good_step(bp, now);
        CHECK(bp.target_fps() == fps);
        CHECK(bp.target_bitrate_kbps() == 10000);
        CHECK(bp.changed());
    }

    // Fully recovered: further healthy time changes nothing.
    good_step(bp, now);
    CHECK(bp.target_fps() == 60);
    CHECK(bp.target_bitrate_kbps() == 10000);
    CHECK(!bp.changed());
}

void test_backpressure_recovery_from_floor_is_exact() {
    DecoderBackpressure bp(60, 10000);
    std::uint64_t now = 0;
    bp.on_stats(2, now);
    for (int i = 0; i < 40; ++i) {
        bad_step(bp, now);
    }
    bp.on_stats(0, now);
    for (int i = 0; i < 40; ++i) {
        good_step(bp, now);
    }
    CHECK(bp.target_fps() == 60);
    CHECK(bp.target_bitrate_kbps() == 10000);
}

void test_backpressure_recovery_timing() {
    DecoderBackpressure bp(60, 10000);
    std::uint64_t now = 0;
    bp.on_stats(2, now);
    bad_step(bp, now);
    CHECK(bp.target_fps() == 50);
    CHECK(bp.changed());

    const std::uint64_t start = now + 1000;
    bp.on_stats(0, start);
    bp.on_stats(0, start + 1999999);
    CHECK(bp.target_fps() == 50);
    CHECK(!bp.changed());
    bp.on_stats(0, start + 2000000);
    CHECK(bp.target_fps() == 60);
    CHECK(bp.changed());
}

void test_backpressure_bad_sample_resets_recovery_timer() {
    DecoderBackpressure bp(60, 10000);
    std::uint64_t now = 0;
    bp.on_stats(2, now);
    bad_step(bp, now);
    CHECK(bp.changed());

    bp.on_stats(0, now + 1000000);
    // A single bad sample cancels the healthy run.
    bp.on_stats(2, now + 1500000);
    bp.on_stats(0, now + 1600000);
    bp.on_stats(0, now + 3500000);
    CHECK(bp.target_fps() == 50);
    CHECK(!bp.changed());
    bp.on_stats(0, now + 3600000);
    CHECK(bp.target_fps() == 60);
}

void test_backpressure_custom_config_and_reset() {
    BackpressureConfig config;
    config.pending_threshold = 3;
    config.sustain_us = 100;
    config.recover_after_us = 200;
    config.min_fps = 30;
    config.fps_step = 15;
    config.min_bitrate_kbps = 1000;
    config.bitrate_step_percent = 50;

    DecoderBackpressure bp(60, 4000, config);
    bp.on_stats(3, 0);
    bp.on_stats(3, 1000);
    CHECK(bp.target_fps() == 60);

    bp.on_stats(4, 2000);
    bp.on_stats(4, 2101);
    CHECK(bp.target_fps() == 45);
    bp.on_stats(4, 2202);
    CHECK(bp.target_fps() == 30);
    bp.on_stats(4, 2303);
    CHECK(bp.target_bitrate_kbps() == 2000);
    bp.on_stats(4, 2404);
    CHECK(bp.target_bitrate_kbps() == 1000);
    CHECK(bp.changed());

    bp.reset();
    CHECK(bp.target_fps() == 60);
    CHECK(bp.target_bitrate_kbps() == 4000);
    CHECK(!bp.changed());
}

// ---- Resolution Cap ----

void check_size(const StreamSize& size, unsigned w, unsigned h, unsigned orientation, int line) {
    if (static_cast<unsigned>(size.width) != w || static_cast<unsigned>(size.height) != h ||
        static_cast<unsigned>(size.orientation) != orientation) {
        std::printf("%s:%d: got %ux%u o=%u, expected %ux%u o=%u\n", __FILE__, line,
                    static_cast<unsigned>(size.width), static_cast<unsigned>(size.height),
                    static_cast<unsigned>(size.orientation), w, h, orientation);
        ++g_failures;
    }
}

#define CHECK_SIZE(size, w, h, o) check_size((size), (w), (h), (o), __LINE__)

void test_cap_landscape_4k_on_phone() {
    CHECK_SIZE(cap_resolution(3840, 2160, 1080, 2400, 0, 0), 1920, 1080, 1);
    // Same phone reported in landscape orientation.
    CHECK_SIZE(cap_resolution(3840, 2160, 2400, 1080, 0, 0), 1920, 1080, 1);
}

void test_cap_portrait_game() {
    CHECK_SIZE(cap_resolution(1080, 1920, 1080, 2400, 0, 0), 1080, 1920, 0);
    CHECK_SIZE(cap_resolution(2160, 3840, 1080, 2400, 0, 0), 1080, 1920, 0);
    CHECK_SIZE(cap_resolution(1080, 2340, 720, 1600, 0, 0), 720, 1560, 0);
}

void test_cap_decoder_limit() {
    CHECK_SIZE(cap_resolution(1920, 1080, 1080, 2400, 1280, 720), 1280, 720, 1);
    // Decoder limit given in the other orientation maps by long/short side.
    CHECK_SIZE(cap_resolution(1920, 1080, 1080, 2400, 720, 1280), 1280, 720, 1);
    // Portrait game under the same decoder limit.
    CHECK_SIZE(cap_resolution(1080, 1920, 1080, 2400, 1280, 720), 720, 1280, 0);
    // Screen is the tighter limit.
    CHECK_SIZE(cap_resolution(1920, 1080, 720, 1600, 4096, 2160), 1280, 720, 1);
}

void test_cap_odd_sizes() {
    CHECK_SIZE(cap_resolution(1001, 501, 4000, 4000, 0, 0), 1000, 500, 1);
    CHECK_SIZE(cap_resolution(601, 1001, 4000, 4000, 0, 0), 600, 1000, 0);
    // Scaling result 888.88 rounds down to an even value.
    CHECK_SIZE(cap_resolution(1920, 1080, 1000, 500, 0, 0), 888, 500, 1);
}

void test_cap_no_upscale() {
    CHECK_SIZE(cap_resolution(640, 360, 1080, 2400, 0, 0), 640, 360, 1);
    CHECK_SIZE(cap_resolution(640, 360, 1080, 2400, 4096, 4096), 640, 360, 1);
    CHECK_SIZE(cap_resolution(360, 640, 1080, 2400, 0, 0), 360, 640, 0);
}

void test_cap_decoder_unknown() {
    CHECK_SIZE(cap_resolution(1920, 1080, 1080, 2400, 0, 0), 1920, 1080, 1);
    // A partially unknown decoder limit is ignored as a whole.
    CHECK_SIZE(cap_resolution(1920, 1080, 1080, 2400, 0, 720), 1920, 1080, 1);
    CHECK_SIZE(cap_resolution(1920, 1080, 1080, 2400, 1280, 0), 1920, 1080, 1);
}

void test_cap_edge_cases() {
    // Square game: orientation is Portrait (width is not greater than height).
    CHECK_SIZE(cap_resolution(1000, 1000, 720, 1600, 0, 0), 720, 720, 0);
    // Minimum dimension is 2.
    CHECK_SIZE(cap_resolution(1, 1, 1080, 2400, 0, 0), 2, 2, 0);
    CHECK_SIZE(cap_resolution(1000, 1, 100, 100, 0, 0), 100, 2, 1);
    // Degenerate game size still yields a valid even size.
    CHECK_SIZE(cap_resolution(0, 0, 1080, 2400, 0, 0), 2, 2, 0);
}

// ---- default bitrate ----

void test_default_bitrate() {
    CHECK(default_bitrate_kbps(1920, 1080, 30) == 6220);
    CHECK(default_bitrate_kbps(1920, 1080, 60) == 12441);
    CHECK(default_bitrate_kbps(1280, 720, 30) == 2764);
    // Clamped to the minimum.
    CHECK(default_bitrate_kbps(640, 360, 20) == 2000);
    CHECK(default_bitrate_kbps(2, 2, 1) == 2000);
    // Clamped to the maximum.
    CHECK(default_bitrate_kbps(3840, 2160, 60) == 20000);
    CHECK(default_bitrate_kbps(65534, 65534, 255) == 20000);
    // Exactly on the minimum boundary.
    CHECK(default_bitrate_kbps(2000, 1000, 10) == 2000);
}

}  // namespace

int main() {
    test_inflight_basics();
    test_inflight_first_seq_zero();
    test_inflight_ack_ordering();
    test_inflight_skipped_acks();
    test_inflight_wraparound();
    test_inflight_first_frame_at_wrap();
    test_inflight_custom_max();

    test_keyframe_first_request_allowed();
    test_keyframe_denial_then_due();
    test_keyframe_allowed_after_interval();
    test_keyframe_no_pending_without_denial();
    test_keyframe_custom_interval_and_reset();
    test_keyframe_clock_backwards();

    test_backpressure_initial();
    test_backpressure_degrade_order();
    test_backpressure_floors();
    test_backpressure_low_nominal();
    test_backpressure_sustain_timing();
    test_backpressure_threshold_is_exclusive();
    test_backpressure_spikes_do_not_degrade();
    test_backpressure_recovery_order();
    test_backpressure_recovery_from_floor_is_exact();
    test_backpressure_recovery_timing();
    test_backpressure_bad_sample_resets_recovery_timer();
    test_backpressure_custom_config_and_reset();

    test_cap_landscape_4k_on_phone();
    test_cap_portrait_game();
    test_cap_decoder_limit();
    test_cap_odd_sizes();
    test_cap_no_upscale();
    test_cap_decoder_unknown();
    test_cap_edge_cases();

    test_default_bitrate();

    if (g_failures != 0) {
        std::printf("test_flow: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_flow: all checks passed\n");
    return 0;
}
