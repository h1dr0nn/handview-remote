#include "handview/flow/flow.hpp"

#include <algorithm>

#include "handview/protocol/wire.hpp"

namespace handview::flow {

namespace {

using handview::protocol::seq_newer_or_equal;

// Elapsed time that never underflows if the caller's clock steps backwards.
std::uint64_t elapsed_us(std::uint64_t now_us, std::uint64_t since_us) {
    return now_us >= since_us ? now_us - since_us : 0;
}

// One bitrate degrade step: lower by percent, never below the floor.
std::uint32_t next_lower_bitrate(std::uint32_t bitrate, const BackpressureConfig& config) {
    const std::uint64_t percent = std::min<std::uint64_t>(config.bitrate_step_percent, 100);
    const std::uint64_t cut = static_cast<std::uint64_t>(bitrate) * percent / 100;
    const std::uint64_t lowered = static_cast<std::uint64_t>(bitrate) - cut;
    const std::uint64_t floored = std::max<std::uint64_t>(lowered, config.min_bitrate_kbps);
    return static_cast<std::uint32_t>(floored);
}

}  // namespace

// ---- InFlightLimiter ----

void InFlightLimiter::on_frame_sent(std::uint32_t frame_seq) {
    if (!has_sent_) {
        has_sent_ = true;
        last_sent_ = frame_seq;
        last_acked_ = frame_seq - 1u;
        return;
    }
    if (frame_seq != last_sent_ && seq_newer_or_equal(frame_seq, last_sent_)) {
        last_sent_ = frame_seq;
    }
}

void InFlightLimiter::on_ack(std::uint32_t frame_seq) {
    if (!has_sent_) {
        return;
    }
    if (!seq_newer_or_equal(frame_seq, last_acked_)) {
        return;
    }
    if (!seq_newer_or_equal(last_sent_, frame_seq)) {
        return;
    }
    last_acked_ = frame_seq;
}

std::uint32_t InFlightLimiter::in_flight() const {
    return has_sent_ ? last_sent_ - last_acked_ : 0u;
}

bool InFlightLimiter::can_encode() const {
    return in_flight() <= max_in_flight_;
}

void InFlightLimiter::reset() {
    has_sent_ = false;
    last_sent_ = 0;
    last_acked_ = 0;
}

// ---- KeyframeLimiter ----

bool KeyframeLimiter::interval_passed(std::uint64_t now_us) const {
    return !has_idr_ || elapsed_us(now_us, last_idr_us_) >= config_.min_interval_us;
}

bool KeyframeLimiter::request(std::uint64_t now_us) {
    if (interval_passed(now_us)) {
        return true;
    }
    pending_ = true;
    return false;
}

bool KeyframeLimiter::pending_due(std::uint64_t now_us) const {
    return pending_ && interval_passed(now_us);
}

void KeyframeLimiter::on_idr_sent(std::uint64_t now_us) {
    has_idr_ = true;
    last_idr_us_ = now_us;
    pending_ = false;
}

void KeyframeLimiter::reset() {
    has_idr_ = false;
    last_idr_us_ = 0;
    pending_ = false;
}

// ---- DecoderBackpressure ----

DecoderBackpressure::DecoderBackpressure(std::uint8_t nominal_fps, std::uint32_t nominal_bitrate_kbps,
                                         BackpressureConfig config)
    : config_(config),
      nominal_fps_(nominal_fps),
      nominal_bitrate_kbps_(nominal_bitrate_kbps),
      target_fps_(nominal_fps),
      target_bitrate_kbps_(nominal_bitrate_kbps) {
    if (config_.fps_step > 0 && nominal_fps_ > config_.min_fps) {
        const std::uint32_t span = static_cast<std::uint32_t>(nominal_fps_ - config_.min_fps);
        max_fps_level_ = (span + config_.fps_step - 1u) / config_.fps_step;
    }
    std::uint32_t bitrate = nominal_bitrate_kbps_;
    while (bitrate > config_.min_bitrate_kbps) {
        const std::uint32_t lower = next_lower_bitrate(bitrate, config_);
        if (lower >= bitrate) {
            break;
        }
        bitrate = lower;
        ++max_bitrate_level_;
    }
}

std::uint8_t DecoderBackpressure::fps_at(std::uint32_t level) const {
    if (nominal_fps_ <= config_.min_fps) {
        return nominal_fps_;
    }
    const std::uint32_t span = static_cast<std::uint32_t>(nominal_fps_ - config_.min_fps);
    const std::uint32_t cut = std::min(level * config_.fps_step, span);
    return static_cast<std::uint8_t>(nominal_fps_ - cut);
}

std::uint32_t DecoderBackpressure::bitrate_at(std::uint32_t level) const {
    std::uint32_t bitrate = nominal_bitrate_kbps_;
    for (std::uint32_t i = 0; i < level; ++i) {
        bitrate = next_lower_bitrate(bitrate, config_);
    }
    return bitrate;
}

void DecoderBackpressure::apply_levels() {
    const std::uint8_t fps = fps_at(fps_level_);
    const std::uint32_t bitrate = bitrate_at(bitrate_level_);
    if (fps != target_fps_ || bitrate != target_bitrate_kbps_) {
        target_fps_ = fps;
        target_bitrate_kbps_ = bitrate;
        changed_ = true;
    }
}

void DecoderBackpressure::degrade() {
    if (fps_level_ < max_fps_level_) {
        ++fps_level_;
    } else if (bitrate_level_ < max_bitrate_level_) {
        ++bitrate_level_;
    }
    apply_levels();
}

void DecoderBackpressure::recover() {
    if (bitrate_level_ > 0) {
        --bitrate_level_;
    } else if (fps_level_ > 0) {
        --fps_level_;
    }
    apply_levels();
}

void DecoderBackpressure::on_stats(std::uint16_t pending_outputs, std::uint64_t now_us) {
    if (pending_outputs > config_.pending_threshold) {
        good_active_ = false;
        if (!bad_active_) {
            bad_active_ = true;
            bad_since_us_ = now_us;
        } else if (elapsed_us(now_us, bad_since_us_) > config_.sustain_us) {
            degrade();
            bad_since_us_ = now_us;
        }
        return;
    }

    bad_active_ = false;
    if (!good_active_) {
        good_active_ = true;
        good_since_us_ = now_us;
    } else if (elapsed_us(now_us, good_since_us_) >= config_.recover_after_us) {
        recover();
        good_since_us_ = now_us;
    }
}

bool DecoderBackpressure::changed() {
    const bool was_changed = changed_;
    changed_ = false;
    return was_changed;
}

void DecoderBackpressure::reset() {
    fps_level_ = 0;
    bitrate_level_ = 0;
    target_fps_ = nominal_fps_;
    target_bitrate_kbps_ = nominal_bitrate_kbps_;
    changed_ = false;
    bad_active_ = false;
    bad_since_us_ = 0;
    good_active_ = false;
    good_since_us_ = 0;
}

// ---- Resolution Cap ----

StreamSize cap_resolution(std::uint32_t game_w, std::uint32_t game_h, std::uint32_t screen_w,
                          std::uint32_t screen_h, std::uint32_t decoder_max_w,
                          std::uint32_t decoder_max_h) {
    // Largest even value a u16 dimension can hold.
    constexpr std::uint64_t kMaxDim = 65534;

    const std::uint64_t game_long = std::max(game_w, game_h);
    const std::uint64_t game_short = std::min(game_w, game_h);

    std::uint64_t limit_long = kMaxDim;
    std::uint64_t limit_short = kMaxDim;
    if (screen_w != 0 && screen_h != 0) {
        limit_long = std::min<std::uint64_t>(limit_long, std::max(screen_w, screen_h));
        limit_short = std::min<std::uint64_t>(limit_short, std::min(screen_w, screen_h));
    }
    if (decoder_max_w != 0 && decoder_max_h != 0) {
        limit_long = std::min<std::uint64_t>(limit_long, std::max(decoder_max_w, decoder_max_h));
        limit_short = std::min<std::uint64_t>(limit_short, std::min(decoder_max_w, decoder_max_h));
    }

    std::uint64_t out_long = game_long;
    std::uint64_t out_short = game_short;
    if (game_long != 0 && game_short != 0) {
        // Compare limit_long/game_long with limit_short/game_short by cross
        // multiplication to find the tighter ratio; scale only when it is < 1.
        if (limit_long * game_short <= limit_short * game_long) {
            if (limit_long < game_long) {
                out_long = limit_long;
                out_short = game_short * limit_long / game_long;
            }
        } else if (limit_short < game_short) {
            out_short = limit_short;
            out_long = game_long * limit_short / game_short;
        }
    }

    out_long = std::max<std::uint64_t>(out_long & ~std::uint64_t{1}, 2);
    out_short = std::max<std::uint64_t>(out_short & ~std::uint64_t{1}, 2);
    out_long = std::min(out_long, kMaxDim);
    out_short = std::min(out_short, kMaxDim);

    const bool game_landscape = game_w >= game_h;
    const std::uint64_t width = game_landscape ? out_long : out_short;
    const std::uint64_t height = game_landscape ? out_short : out_long;

    StreamSize size{};
    size.width = static_cast<std::uint16_t>(width);
    size.height = static_cast<std::uint16_t>(height);
    size.orientation = size.width > size.height ? kOrientationLandscape : kOrientationPortrait;
    return size;
}

std::uint32_t default_bitrate_kbps(std::uint16_t width, std::uint16_t height, std::uint8_t fps) {
    constexpr std::uint64_t kMinKbps = 2000;
    constexpr std::uint64_t kMaxKbps = 20000;
    // width * height * fps * 0.1 bits per pixel, in kbps.
    const std::uint64_t kbps =
        static_cast<std::uint64_t>(width) * height * fps / 10000u;
    return static_cast<std::uint32_t>(std::clamp(kbps, kMinKbps, kMaxKbps));
}

}  // namespace handview::flow
