#include "internal.hpp"
#include <cstring>
namespace handview::encode::detail {
bool Gpu::init(const EncoderConfig &c, bool nvidia, const LUID *luid, std::string &e) {
    config = c;
    ComPtr<IDXGIFactory1> factory;
    if (!hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1", e))
        return false;
    ComPtr<IDXGIAdapter1> adapter;
    if (nvidia || luid) {
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> candidate;
            auto status = factory->EnumAdapters1(i, &candidate);
            if (status == DXGI_ERROR_NOT_FOUND)
                break;
            if (!hr(status, "EnumAdapters1", e))
                return false;
            DXGI_ADAPTER_DESC1 d{};
            if (!hr(candidate->GetDesc1(&d), "Adapter description", e))
                return false;
            if ((nvidia && d.VendorId == 0x10de) ||
                (luid && d.AdapterLuid.HighPart == luid->HighPart &&
                 d.AdapterLuid.LowPart == luid->LowPart)) {
                adapter = candidate;
                break;
            }
        }
        if (!adapter) {
            e = "Requested GPU adapter unavailable";
            return false;
        }
    }
    if (!hr(D3D11CreateDevice(adapter.Get(),
                              adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                              D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context),
            "D3D11CreateDevice", e))
        return false;
    ComPtr<ID3D10Multithread> mt;
    if (!hr(context.As(&mt), "D3D multithread protection", e))
        return false;
    mt->SetMultithreadProtected(TRUE);
    if (!hr(device.As(&video), "D3D video device", e) ||
        !hr(context.As(&vc), "D3D video context", e))
        return false;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = c.in_w;
    content.InputHeight = c.in_h;
    content.OutputWidth = c.out_w;
    content.OutputHeight = c.out_h;
    content.InputFrameRate = {c.fps, 1};
    content.OutputFrameRate = {c.fps, 1};
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    if (!hr(video->CreateVideoProcessorEnumerator(&content, &enumerator),
            "Video processor enumeration", e) ||
        !hr(video->CreateVideoProcessor(enumerator.Get(), 0, &processor), "Video processor", e))
        return false;
    D3D11_TEXTURE2D_DESC t{};
    t.Width = c.in_w;
    t.Height = c.in_h;
    t.MipLevels = 1;
    t.ArraySize = 1;
    t.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_DEFAULT;
    t.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (!hr(device->CreateTexture2D(&t, nullptr, &rgba), "RGBA texture", e))
        return false;
    t.Width = c.out_w;
    t.Height = c.out_h;
    t.Format = DXGI_FORMAT_NV12;
    if (!hr(device->CreateTexture2D(&t, nullptr, &nv12), "NV12 texture", e))
        return false;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};
    iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{};
    ov.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (!hr(video->CreateVideoProcessorInputView(rgba.Get(), enumerator.Get(), &iv, &input),
            "Video input view", e) ||
        !hr(video->CreateVideoProcessorOutputView(nv12.Get(), enumerator.Get(), &ov, &output),
            "Video output view", e))
        return false;
    RECT src{0, 0, static_cast<LONG>(c.in_w), static_cast<LONG>(c.in_h)},
        dst{0, 0, c.out_w, c.out_h};
    vc->VideoProcessorSetStreamFrameFormat(processor.Get(), 0,
                                           D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    vc->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &src);
    vc->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &dst);
    vc->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &dst);
    vc->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{}, yuv{};
    rgb.RGB_Range = 0;
    rgb.YCbCr_Matrix = 1;
    rgb.Nominal_Range = 2;
    yuv.YCbCr_Matrix = 1;
    yuv.Nominal_Range = 1;
    vc->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &rgb);
    vc->VideoProcessorSetOutputColorSpace(processor.Get(), &yuv);
    ComPtr<ID3D11VideoContext1> vc1;
    if (SUCCEEDED(vc.As(&vc1))) {
        vc1->VideoProcessorSetStreamColorSpace1(processor.Get(), 0,
                                                DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        vc1->VideoProcessorSetOutputColorSpace1(processor.Get(),
                                                DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    }
    return true;
}
bool Gpu::convert(const uint8_t *p, uint32_t stride, std::string &e) {
    if (!p || stride < uint64_t(config.in_w) * 4) {
        e = "Invalid RGBA pointer or stride";
        return false;
    }
    context->UpdateSubresource(rgba.Get(), 0, nullptr, p, stride, 0);
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = input.Get();
    return hr(vc->VideoProcessorBlt(processor.Get(), output.Get(), 0, 1, &stream),
              "VideoProcessorBlt", e);
}
} // namespace handview::encode::detail
