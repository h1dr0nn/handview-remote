#pragma once
#include "handview/encode/encoder.hpp"
#include "handview/encode/h264.hpp"
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <utility>
namespace handview::encode::detail {
using Microsoft::WRL::ComPtr;
template <class F> struct ScopeExit {
    F function;
    bool active = true;
    explicit ScopeExit(F f) : function(std::move(f)) {}
    ~ScopeExit() {
        if (active)
            function();
    }
    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
};
inline bool hr(HRESULT value, const char *what, std::string &error) {
    if (SUCCEEDED(value))
        return true;
    char text[128];
    std::snprintf(text, sizeof(text), "%s: HRESULT 0x%08lX", what,
                  static_cast<unsigned long>(value));
    error = text;
    return false;
}
struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VideoDevice> video;
    ComPtr<ID3D11VideoContext> vc;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    ComPtr<ID3D11Texture2D> rgba, nv12;
    ComPtr<ID3D11VideoProcessorInputView> input;
    ComPtr<ID3D11VideoProcessorOutputView> output;
    EncoderConfig config{};
    bool init(const EncoderConfig &, bool nvidia, const LUID *luid, std::string &);
    bool convert(const uint8_t *, uint32_t, std::string &);
};
inline void classify(EncodedFrame &out) {
    bool sps = false, pps = false;
    out.keyframe = false;
    for (auto n : split_annex_b(out.data)) {
        sps |= nal_type(n) == 7;
        pps |= nal_type(n) == 8;
        out.keyframe |= nal_type(n) == 5;
    }
    out.has_codec_config = sps && pps;
}
std::unique_ptr<Encoder> make_nvenc(const EncoderConfig &, std::string &);
std::unique_ptr<Encoder> make_mf(const EncoderConfig &, bool, std::string &);
bool probe_nvenc();
bool probe_mf(bool);
} // namespace handview::encode::detail
