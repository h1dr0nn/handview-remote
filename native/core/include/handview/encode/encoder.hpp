#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace handview::encode {
enum class Backend : uint8_t { Nvenc = 0, MfHardware = 1, MfSoftware = 2 };
struct EncoderConfig {
    uint32_t in_w, in_h;
    uint16_t out_w, out_h;
    uint8_t fps;
    uint32_t bitrate_kbps;
    uint8_t profile = 77;
};
struct EncodedFrame {
    std::vector<uint8_t> data;
    bool keyframe = false;
    bool has_codec_config = false;
};
// Create, use and destroy on one encoder thread.
class Encoder {
  public:
    virtual Backend backend() const = 0;
    virtual bool encode(const uint8_t *rgba, uint32_t stride, uint64_t pts_us, bool force_idr,
                        EncodedFrame &out, std::string &error) = 0;
    virtual bool set_rate(uint8_t fps, uint32_t bitrate_kbps) = 0;
    virtual ~Encoder() = default;
};
// An explicit backend takes precedence over HANDVIEW_ENCODER.
std::unique_ptr<Encoder> create_encoder(const EncoderConfig &, std::optional<Backend> forced,
                                        std::string &error);
// Advisory availability check without opening an encoding session. Safe on an STA editor thread.
std::optional<Backend> probe_backend();
} // namespace handview::encode
