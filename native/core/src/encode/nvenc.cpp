#include "internal.hpp"
#include <ffnvcodec/nvEncodeAPI.h>
#include <algorithm>
namespace handview::encode::detail {
namespace {
struct Library {
    HMODULE dll = nullptr;
    NV_ENCODE_API_FUNCTION_LIST api{};
    ~Library() {
        if (dll)
            FreeLibrary(dll);
    }
    bool init(std::string &e) {
        dll = LoadLibraryW(L"nvEncodeAPI64.dll");
        if (!dll) {
            e = "NVENC DLL unavailable";
            return false;
        }
        using Max = NVENCSTATUS(NVENCAPI *)(uint32_t *);
        using Create = NVENCSTATUS(NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST *);
        auto max = reinterpret_cast<Max>(GetProcAddress(dll, "NvEncodeAPIGetMaxSupportedVersion"));
        auto create = reinterpret_cast<Create>(GetProcAddress(dll, "NvEncodeAPICreateInstance"));
        uint32_t version = 0;
        if (!max || !create || max(&version) != NV_ENC_SUCCESS ||
            version < ((NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION)) {
            e = "NVENC driver does not support API 12.1";
            return false;
        }
        api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        if (create(&api) != NV_ENC_SUCCESS) {
            e = "NvEncodeAPICreateInstance failed";
            return false;
        }
        return true;
    }
};
class Nvenc final : public Encoder {
    Library lib;
    Gpu gpu;
    void *session = nullptr;
    NV_ENC_REGISTERED_PTR resource = nullptr;
    NV_ENC_OUTPUT_PTR bitstream = nullptr;
    NV_ENC_CONFIG config{};
    NV_ENC_INITIALIZE_PARAMS init_params{};
    bool check(NVENCSTATUS status, const char *operation, std::string &e) {
        if (status == NV_ENC_SUCCESS)
            return true;
        e = std::string(operation) + ": NVENC status " + std::to_string(status);
        if (session && lib.api.nvEncGetLastErrorString) {
            auto text = lib.api.nvEncGetLastErrorString(session);
            if (text)
                e += std::string(" ") + text;
        }
        return false;
    }

  public:
    ~Nvenc() override {
        if (session) {
            if (bitstream)
                lib.api.nvEncDestroyBitstreamBuffer(session, bitstream);
            if (resource)
                lib.api.nvEncUnregisterResource(session, resource);
            lib.api.nvEncDestroyEncoder(session);
        }
    }
    Backend backend() const override {
        return Backend::Nvenc;
    }
    bool init(const EncoderConfig &c, std::string &e) {
        if (!lib.init(e) || !gpu.init(c, true, nullptr, e))
            return false;
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
        open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        open.device = gpu.device.Get();
        open.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
        open.apiVersion = NVENCAPI_VERSION;
        if (!check(lib.api.nvEncOpenEncodeSessionEx(&open, &session), "Open session", e))
            return false;
        NV_ENC_PRESET_CONFIG preset{};
        preset.version = NV_ENC_PRESET_CONFIG_VER;
        preset.presetCfg.version = NV_ENC_CONFIG_VER;
        if (!check(lib.api.nvEncGetEncodePresetConfigEx(
                       session, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P2_GUID,
                       NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset),
                   "Get preset", e))
            return false;
        config = preset.presetCfg;
        config.profileGUID =
            c.profile == 66 ? NV_ENC_H264_PROFILE_BASELINE_GUID : NV_ENC_H264_PROFILE_MAIN_GUID;
        config.gopLength = NVENC_INFINITE_GOPLENGTH;
        config.frameIntervalP = 1;
        auto &rc = config.rcParams;
        rc.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        rc.averageBitRate = c.bitrate_kbps * 1000;
        rc.maxBitRate = rc.averageBitRate;
        rc.vbvBufferSize = rc.averageBitRate / c.fps;
        rc.vbvInitialDelay = rc.vbvBufferSize;
        rc.enableLookahead = 0;
        rc.enableAQ = 0;
        auto &h = config.encodeCodecConfig.h264Config;
        h.idrPeriod = NVENC_INFINITE_GOPLENGTH;
        h.repeatSPSPPS = 1;
        h.outputAUD = 0;
        NV_ENC_CAPS_PARAM caps{};
        caps.version = NV_ENC_CAPS_PARAM_VER;
        caps.capsToQuery = NV_ENC_CAPS_SUPPORT_INTRA_REFRESH;
        int supported = 0;
        if (lib.api.nvEncGetEncodeCaps(session, NV_ENC_CODEC_H264_GUID, &caps, &supported) ==
                NV_ENC_SUCCESS &&
            supported) {
            h.enableIntraRefresh = 1;
            h.intraRefreshPeriod = uint32_t(c.fps) * 2;
            h.intraRefreshCnt = (std::max)(1u, uint32_t(c.fps) / 2);
        }
        auto &v = h.h264VUIParameters;
        v.videoSignalTypePresentFlag = 1;
        v.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
        v.videoFullRangeFlag = 0;
        v.colourDescriptionPresentFlag = 1;
        v.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
        v.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
        v.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;
        init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
        init_params.encodeGUID = NV_ENC_CODEC_H264_GUID;
        init_params.presetGUID = NV_ENC_PRESET_P2_GUID;
        init_params.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
        init_params.encodeWidth = c.out_w;
        init_params.encodeHeight = c.out_h;
        init_params.darWidth = c.out_w;
        init_params.darHeight = c.out_h;
        init_params.frameRateNum = c.fps;
        init_params.frameRateDen = 1;
        init_params.enablePTD = 1;
        init_params.encodeConfig = &config;
        init_params.maxEncodeWidth = c.out_w;
        init_params.maxEncodeHeight = c.out_h;
        if (!check(lib.api.nvEncInitializeEncoder(session, &init_params), "Initialize", e))
            return false;
        NV_ENC_REGISTER_RESOURCE reg{};
        reg.version = NV_ENC_REGISTER_RESOURCE_VER;
        reg.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        reg.resourceToRegister = gpu.nv12.Get();
        reg.width = c.out_w;
        reg.height = c.out_h;
        reg.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
        reg.bufferUsage = NV_ENC_INPUT_IMAGE;
        if (!check(lib.api.nvEncRegisterResource(session, &reg), "Register NV12", e))
            return false;
        resource = reg.registeredResource;
        NV_ENC_CREATE_BITSTREAM_BUFFER bits{};
        bits.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
        if (!check(lib.api.nvEncCreateBitstreamBuffer(session, &bits), "Create bitstream", e))
            return false;
        bitstream = bits.bitstreamBuffer;
        return true;
    }
    bool encode(const uint8_t *rgba, uint32_t stride, uint64_t pts, bool idr, EncodedFrame &out,
                std::string &e) override {
        out = {};
        e.clear();
        if (!gpu.convert(rgba, stride, e))
            return false;
        gpu.context->Flush();
        NV_ENC_MAP_INPUT_RESOURCE map{};
        map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map.registeredResource = resource;
        if (!check(lib.api.nvEncMapInputResource(session, &map), "Map input", e))
            return false;
        ScopeExit unmap([&] { lib.api.nvEncUnmapInputResource(session, map.mappedResource); });
        NV_ENC_PIC_PARAMS pic{};
        pic.version = NV_ENC_PIC_PARAMS_VER;
        pic.inputBuffer = map.mappedResource;
        pic.bufferFmt = map.mappedBufferFmt;
        pic.inputWidth = gpu.config.out_w;
        pic.inputHeight = gpu.config.out_h;
        pic.outputBitstream = bitstream;
        pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        pic.inputTimeStamp = pts;
        if (idr)
            pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
        bool ok = check(lib.api.nvEncEncodePicture(session, &pic), "Encode picture", e);
        if (ok) {
            NV_ENC_LOCK_BITSTREAM lock{};
            lock.version = NV_ENC_LOCK_BITSTREAM_VER;
            lock.outputBitstream = bitstream;
            ok = check(lib.api.nvEncLockBitstream(session, &lock), "Lock bitstream", e);
            if (ok) {
                ScopeExit unlock([&] { lib.api.nvEncUnlockBitstream(session, bitstream); });
                auto p = static_cast<const uint8_t *>(lock.bitstreamBufferPtr);
                out.data.assign(p, p + lock.bitstreamSizeInBytes);
                auto status = lib.api.nvEncUnlockBitstream(session, bitstream);
                unlock.active = false;
                ok = check(status, "Unlock bitstream", e);
            }
        }
        auto status = lib.api.nvEncUnmapInputResource(session, map.mappedResource);
        unmap.active = false;
        if (ok)
            ok = check(status, "Unmap input", e);
        if (ok) {
            classify(out);
            if (out.data.empty()) {
                e = "NVENC returned empty access unit";
                return false;
            }
        }
        return ok;
    }
    bool set_rate(uint8_t fps, uint32_t kbps) override {
        if (!fps || !kbps || kbps > UINT32_MAX / 1000)
            return false;
        auto next = config;
        next.rcParams.averageBitRate = kbps * 1000;
        next.rcParams.maxBitRate = kbps * 1000;
        next.rcParams.vbvBufferSize = kbps * 1000 / fps;
        next.rcParams.vbvInitialDelay = next.rcParams.vbvBufferSize;
        next.encodeCodecConfig.h264Config.intraRefreshPeriod = uint32_t(fps) * 2;
        next.encodeCodecConfig.h264Config.intraRefreshCnt = (std::max)(1u, uint32_t(fps) / 2);
        NV_ENC_RECONFIGURE_PARAMS re{};
        re.version = NV_ENC_RECONFIGURE_PARAMS_VER;
        re.reInitEncodeParams = init_params;
        re.reInitEncodeParams.frameRateNum = fps;
        re.reInitEncodeParams.encodeConfig = &next;
        if (lib.api.nvEncReconfigureEncoder(session, &re) != NV_ENC_SUCCESS)
            return false;
        config = next;
        init_params.frameRateNum = fps;
        gpu.config.fps = fps;
        return true;
    }
};
} // namespace
std::unique_ptr<Encoder> make_nvenc(const EncoderConfig &c, std::string &e) {
    auto p = std::make_unique<Nvenc>();
    if (!p->init(c, e))
        return {};
    return p;
}
bool probe_nvenc() {
    Library l;
    std::string e;
    if (!l.init(e))
        return false;
    ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))
        return false;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (FAILED(f->EnumAdapters1(i, &a)))
            return false;
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(a->GetDesc1(&d)))
            return false;
        if (d.VendorId == 0x10de)
            return true;
    }
}
} // namespace handview::encode::detail
