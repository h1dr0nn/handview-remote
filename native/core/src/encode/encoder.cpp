#include "internal.hpp"
#include <cstdlib>
#include <exception>
#include <initializer_list>
namespace handview::encode {
namespace {
// Backends this platform has, in order of preference.
#ifdef _WIN32
constexpr std::initializer_list<Backend> kBackends = {Backend::Nvenc, Backend::MfHardware,
                                                      Backend::MfSoftware};
constexpr const char *kBackendNames = "nvenc, mf_hw or mf_sw";
#elif defined(__APPLE__)
constexpr std::initializer_list<Backend> kBackends = {Backend::VideoToolbox};
constexpr const char *kBackendNames = "videotoolbox";
#else
constexpr std::initializer_list<Backend> kBackends = {};
constexpr const char *kBackendNames = "no encoder on this platform";
#endif
std::unique_ptr<Encoder> make(Backend b, const EncoderConfig &c, std::string &reason) {
#ifdef _WIN32
    return b == Backend::Nvenc ? detail::make_nvenc(c, reason)
                               : detail::make_mf(c, b == Backend::MfHardware, reason);
#elif defined(__APPLE__)
    (void)b;
    return detail::make_videotoolbox(c, reason);
#else
    (void)b;
    (void)c;
    reason = "No H.264 encoder on this platform";
    return {};
#endif
}
bool probe(Backend b) {
#ifdef _WIN32
    return b == Backend::Nvenc ? detail::probe_nvenc() : detail::probe_mf(b == Backend::MfHardware);
#elif defined(__APPLE__)
    (void)b;
    return detail::probe_videotoolbox();
#else
    (void)b;
    return false;
#endif
}
std::optional<Backend> environment(bool &invalid) {
#ifdef _WIN32
    char value[32]{};
    auto n = GetEnvironmentVariableA("HANDVIEW_ENCODER", value, sizeof(value));
    invalid = false;
    if (!n)
        return {};
    if (n >= sizeof(value)) {
        invalid = true;
        return {};
    }
#else
    const char *value = std::getenv("HANDVIEW_ENCODER");
    invalid = false;
    if (!value || !*value)
        return {};
#endif
    if (std::string(value) == "nvenc")
        return Backend::Nvenc;
    if (std::string(value) == "mf_hw")
        return Backend::MfHardware;
    if (std::string(value) == "mf_sw")
        return Backend::MfSoftware;
    if (std::string(value) == "videotoolbox")
        return Backend::VideoToolbox;
    invalid = true;
    return {};
}
class GuardedEncoder final : public Encoder {
    std::unique_ptr<Encoder> encoder;
    bool failed = false;
    bool first = true;

  public:
    explicit GuardedEncoder(std::unique_ptr<Encoder> value) : encoder(std::move(value)) {}
    Backend backend() const override {
        return encoder->backend();
    }
    bool encode(const uint8_t *rgba, uint32_t stride, uint64_t pts, bool idr, EncodedFrame &out,
                std::string &error) override {
        try {
            out = {};
            error.clear();
            if (failed) {
                error = "Encoder requires reinitialization after failure";
                return false;
            }
            if (encoder->encode(rgba, stride, pts, idr || first, out, error)) {
                first = false;
                return true;
            }
            failed = !encoder->recoverable_failure();
            out = {};
            return false;
        } catch (const std::exception &ex) {
            failed = true;
            out = {};
            error = std::string("Encoder failure: ") + ex.what();
            return false;
        } catch (...) {
            failed = true;
            out = {};
            error = "Unexpected encoder failure";
            return false;
        }
    }
    bool set_rate(uint8_t fps, uint32_t kbps) override {
        if (failed)
            return false;
        try {
            return encoder->set_rate(fps, kbps);
        } catch (...) {
            failed = true;
            return false;
        }
    }
};
} // namespace
std::unique_ptr<Encoder> create_encoder(const EncoderConfig &c, std::optional<Backend> forced,
                                        std::string &error) try {
    error.clear();
    if (!c.in_w || !c.in_h || c.in_w > 16384 || c.in_h > 16384 || c.out_w < 2 || c.out_h < 2 ||
        c.out_w > 16384 || c.out_h > 16384 || c.out_w % 2 || c.out_h % 2 || !c.fps ||
        !c.bitrate_kbps || c.bitrate_kbps > UINT32_MAX / 1000 ||
        (c.profile != 66 && c.profile != 77)) {
        error = "Invalid encoder configuration";
        return {};
    }
    if (!forced) {
        bool invalid = false;
        forced = environment(invalid);
        if (invalid) {
            error = std::string("Invalid HANDVIEW_ENCODER (expected ") + kBackendNames + ")";
            return {};
        }
    }
    for (auto b : kBackends) {
        if (forced && *forced != b)
            continue;
        std::string reason;
        auto result = make(b, c, reason);
        if (result) {
            error.clear();
            return std::make_unique<GuardedEncoder>(std::move(result));
        }
        if (!error.empty())
            error += "; ";
        error += reason;
    }
    if (error.empty())
        error = "Unknown encoder backend";
    return {};
} catch (const std::exception &ex) {
    error = std::string("Encoder initialization failure: ") + ex.what();
    return {};
} catch (...) {
    error = "Unexpected encoder initialization failure";
    return {};
}
std::optional<Backend> probe_backend() try {
    bool invalid = false;
    auto forced = environment(invalid);
    if (invalid)
        return {};
    for (auto b : kBackends) {
        if (forced && *forced != b)
            continue;
        if (probe(b))
            return b;
    }
    return {};
} catch (...) {
    return {};
}
} // namespace handview::encode
