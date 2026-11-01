#include "internal.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <strmif.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
namespace handview::encode::detail {
namespace {
std::mutex runtime_mutex;
unsigned runtime_refs = 0;
struct Runtime {
    bool com = false, mf = false;
    bool init(std::string &e, bool enumeration_only = false) {
        auto status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (status != RPC_E_CHANGED_MODE || !enumeration_only) {
            if (!hr(status, "COM MTA", e))
                return false;
            com = true;
        }
        std::lock_guard lock(runtime_mutex);
        if (!runtime_refs && !hr(MFStartup(MF_VERSION), "MFStartup", e))
            return false;
        ++runtime_refs;
        mf = true;
        return true;
    }
    ~Runtime() {
        if (mf) {
            std::lock_guard lock(runtime_mutex);
            if (!--runtime_refs)
                MFShutdown();
        }
        if (com)
            CoUninitialize();
    }
};
struct Activations {
    IMFActivate **items = nullptr;
    UINT32 count = 0;
    ~Activations() {
        for (UINT32 i = 0; i < count; ++i)
            items[i]->Release();
        CoTaskMemFree(items);
    }
    bool init(bool hw, std::string &e) {
        MFT_REGISTER_TYPE_INFO in{MFMediaType_Video, MFVideoFormat_NV12},
            out{MFMediaType_Video, MFVideoFormat_H264};
        return hr(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                            (hw ? MFT_ENUM_FLAG_HARDWARE : MFT_ENUM_FLAG_SYNCMFT) |
                                MFT_ENUM_FLAG_SORTANDFILTER,
                            &in, &out, &items, &count),
                  "MFTEnumEx", e);
    }
};
class Mf final : public Encoder {
    Runtime runtime;
    Gpu gpu;
    bool hardware;
    EncoderConfig config{};
    ComPtr<IMFActivate> activation;
    ComPtr<IMFTransform> transform;
    ComPtr<ICodecAPI> codec;
    ComPtr<IMFMediaEventGenerator> events;
    ComPtr<IMFDXGIDeviceManager> manager;
    std::array<ComPtr<ID3D11Texture2D>, 3> staging;
    unsigned slot = 0;
    DWORD input_id = 0, output_id = 0;
    bool streaming = false, need_input = false, have_output = false;
    std::vector<uint8_t> headers;
    uint8_t length_bytes = 4;
    bool property(const GUID &key, ULONG value, bool boolean = false) {
        VARIANT v{};
        v.vt = static_cast<VARTYPE>(boolean ? VT_BOOL : VT_UI4);
        if (boolean)
            v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
        else
            v.ulVal = value;
        return SUCCEEDED(codec->SetValue(&key, &v));
    }
    bool event(MediaEventType wanted, std::string &e) {
        if (wanted == METransformNeedInput && need_input) {
            need_input = false;
            return true;
        }
        if (wanted == METransformHaveOutput && have_output) {
            have_output = false;
            return true;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            ComPtr<IMFMediaEvent> ev;
            auto h = events->GetEvent(MF_EVENT_FLAG_NO_WAIT, &ev);
            if (h == MF_E_NO_EVENTS_AVAILABLE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (!hr(h, "MFT GetEvent", e))
                return false;
            HRESULT status = S_OK;
            ev->GetStatus(&status);
            if (!hr(status, "MFT event", e))
                return false;
            MediaEventType type = MEUnknown;
            ev->GetType(&type);
            if (type == wanted)
                return true;
            if (type == METransformNeedInput)
                need_input = true;
            if (type == METransformHaveOutput)
                have_output = true;
        }
        e = "MFT event timeout (one output per input required)";
        return false;
    }
    bool media_type(bool output, ComPtr<IMFMediaType> &t, std::string &e) {
        if (!hr(MFCreateMediaType(&t), "MFCreateMediaType", e))
            return false;
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, output ? MFVideoFormat_H264 : MFVideoFormat_NV12);
        MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, config.out_w, config.out_h);
        MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, config.fps, 1);
        MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        t->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
        t->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
        t->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
        t->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        if (output) {
            t->SetUINT32(MF_MT_AVG_BITRATE, config.bitrate_kbps * 1000);
            t->SetUINT32(MF_MT_MPEG2_PROFILE, config.profile);
        } else {
            t->SetUINT32(MF_MT_DEFAULT_STRIDE, config.out_w);
            t->SetUINT32(MF_MT_SAMPLE_SIZE, uint32_t(config.out_w) * config.out_h * 3 / 2);
        }
        return true;
    }
    void read_headers() {
        ComPtr<IMFMediaType> t;
        if (FAILED(transform->GetOutputCurrentType(output_id, &t)))
            return;
        UINT32 size = 0;
        if (FAILED(t->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) || !size)
            return;
        std::vector<uint8_t> blob(size);
        if (FAILED(t->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, blob.data(), size, nullptr)))
            return;
        std::vector<uint8_t> result;
        if (blob[0] != 1) {
            if (to_annex_b(blob, result))
                headers = std::move(result);
            return;
        }
        if (blob.size() < 7)
            return;
        length_bytes = (blob[4] & 3) + 1;
        size_t pos = 6;
        unsigned count = blob[5] & 31;
        for (unsigned group = 0; group < 2; ++group) {
            for (unsigned i = 0; i < count; ++i) {
                if (pos + 2 > blob.size())
                    return;
                size_t n = (size_t(blob[pos]) << 8) | blob[pos + 1];
                pos += 2;
                if (!n || n > blob.size() - pos)
                    return;
                result.insert(result.end(), {0, 0, 0, 1});
                result.insert(result.end(), blob.begin() + pos, blob.begin() + pos + n);
                pos += n;
            }
            if (group == 0) {
                if (pos >= blob.size())
                    return;
                count = blob[pos++];
            }
        }
        headers = std::move(result);
    }
    bool output(EncodedFrame &out, std::string &e) {
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            MFT_OUTPUT_STREAM_INFO info{};
            if (!hr(transform->GetOutputStreamInfo(output_id, &info), "Output stream info", e))
                return false;
            ComPtr<IMFSample> supplied;
            if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                ComPtr<IMFMediaBuffer> buffer;
                if (!hr(MFCreateSample(&supplied), "Output sample", e) ||
                    !hr(MFCreateAlignedMemoryBuffer(
                            (std::max<DWORD>)(info.cbSize, DWORD(config.out_w) * config.out_h * 3),
                            info.cbAlignment ? info.cbAlignment - 1 : 0, &buffer),
                        "Output buffer", e) ||
                    !hr(supplied->AddBuffer(buffer.Get()), "Output AddBuffer", e))
                    return false;
            }
            MFT_OUTPUT_DATA_BUFFER data{};
            data.dwStreamID = output_id;
            data.pSample = supplied.Get();
            DWORD status = 0;
            auto h = transform->ProcessOutput(0, 1, &data, &status);
            ComPtr<IMFCollection> output_events;
            output_events.Attach(data.pEvents);
            ComPtr<IMFSample> sample;
            if (data.pSample && data.pSample != supplied.Get())
                sample.Attach(data.pSample);
            else
                sample = supplied;
            if (h == MF_E_TRANSFORM_STREAM_CHANGE) {
                ComPtr<IMFMediaType> type;
                if (!hr(transform->GetOutputAvailableType(output_id, 0, &type),
                        "Changed output type", e) ||
                    !hr(transform->SetOutputType(output_id, type.Get(), 0),
                        "Set changed output type", e))
                    return false;
                read_headers();
                continue;
            }
            if (!hr(h, "MFT ProcessOutput", e))
                return false;
            if (!sample) {
                e = "MFT returned no output sample";
                return false;
            }
            ComPtr<IMFMediaBuffer> buffer;
            if (!hr(sample->ConvertToContiguousBuffer(&buffer), "Contiguous output", e))
                return false;
            BYTE *bytes = nullptr;
            DWORD size = 0;
            if (!hr(buffer->Lock(&bytes, nullptr, &size), "Lock output", e))
                return false;
            ScopeExit unlock([&] { buffer->Unlock(); });
            bool ok = to_annex_b({bytes, size}, out.data, length_bytes);
            if (!ok) {
                e = "Malformed H264 output";
                return false;
            }
            classify(out);
            bool sps_before = false, pps_before = false, seen_idr = false;
            std::vector<uint8_t> inline_headers;
            for (auto nal : split_annex_b(out.data)) {
                if (nal_type(nal) == 7 || nal_type(nal) == 8) {
                    inline_headers.insert(inline_headers.end(), {0, 0, 0, 1});
                    inline_headers.insert(inline_headers.end(), nal.begin(), nal.end());
                }
                seen_idr |= nal_type(nal) == 5;
                if (!seen_idr) {
                    sps_before |= nal_type(nal) == 7;
                    pps_before |= nal_type(nal) == 8;
                }
            }
            if (out.has_codec_config)
                headers = std::move(inline_headers);
            if (out.keyframe && !(sps_before && pps_before)) {
                if (headers.empty())
                    read_headers();
                EncodedFrame config_frame;
                config_frame.data = headers;
                classify(config_frame);
                if (!config_frame.has_codec_config) {
                    e = "IDR missing SPS/PPS";
                    return false;
                }
                out.data.insert(out.data.begin(), headers.begin(), headers.end());
                classify(out);
            }
            return true;
        }
        e = "Repeated MFT output format change";
        return false;
    }

  public:
    explicit Mf(bool hw) : hardware(hw) {}
    Backend backend() const override {
        return hardware ? Backend::MfHardware : Backend::MfSoftware;
    }
    ~Mf() override {
        if (transform && streaming) {
            transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
            if (events) {
                try {
                    std::string ignored;
                    event(METransformDrainComplete, ignored);
                } catch (...) {
                }
            }
            transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
            transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        }
        if (activation)
            activation->ShutdownObject();
    }
    bool init(const EncoderConfig &c, IMFActivate *selected, std::string &e) {
        config = c;
        if (!runtime.init(e))
            return false;
        activation = selected;
        if (!hr(activation->ActivateObject(IID_PPV_ARGS(&transform)), "Activate encoder", e) ||
            !hr(transform.As(&codec), "ICodecAPI", e))
            return false;
        ComPtr<IMFAttributes> attributes;
        if (!hr(transform->GetAttributes(&attributes), "MFT attributes", e))
            return false;
        UINT32 async = 0;
        attributes->GetUINT32(MF_TRANSFORM_ASYNC, &async);
        if (async) {
            if (!hr(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE), "Unlock async MFT",
                    e) ||
                !hr(transform.As(&events), "MFT event generator", e))
                return false;
        }
        auto ids = transform->GetStreamIDs(1, &input_id, 1, &output_id);
        if (FAILED(ids) && ids != E_NOTIMPL)
            return hr(ids, "MFT stream IDs", e);
        UINT64 packed = 0;
        LUID luid{};
        const LUID *chosen = nullptr;
        if (hardware && SUCCEEDED(activation->GetUINT64(MFT_ENUM_ADAPTER_LUID, &packed))) {
            std::memcpy(&luid, &packed, sizeof(luid));
            chosen = &luid;
        }
        if (!gpu.init(c, false, chosen, e))
            return false;
        if (hardware) {
            UINT token = 0;
            if (!hr(MFCreateDXGIDeviceManager(&token, &manager), "DXGI manager", e) ||
                !hr(manager->ResetDevice(gpu.device.Get(), token), "Reset DXGI manager", e) ||
                !hr(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                              reinterpret_cast<ULONG_PTR>(manager.Get())),
                    "Set D3D manager", e))
                return false;
        } else {
            D3D11_TEXTURE2D_DESC desc{};
            gpu.nv12->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (auto &texture : staging)
                if (!hr(gpu.device->CreateTexture2D(&desc, nullptr, &texture), "Staging NV12", e))
                    return false;
        }
        property(CODECAPI_AVLowLatencyMode, 1, true);
        property(CODECAPI_AVEncCommonRealTime, 1, true);
        property(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        property(CODECAPI_AVEncCommonMeanBitRate, c.bitrate_kbps * 1000);
        property(CODECAPI_AVEncMPVDefaultBPictureCount, 0);
        property(CODECAPI_AVEncMPVGOPSize, UINT32_MAX);
        ComPtr<IMFMediaType> out, in;
        if (!media_type(true, out, e) ||
            !hr(transform->SetOutputType(output_id, out.Get(), 0), "Set H264 output", e) ||
            !media_type(false, in, e) ||
            !hr(transform->SetInputType(input_id, in.Get(), 0), "Set NV12 input", e))
            return false;
        read_headers();
        if (!hr(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "Begin streaming",
                e))
            return false;
        streaming = true;
        return hr(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "Start stream",
                  e);
    }
    bool encode(const uint8_t *rgba, uint32_t stride, uint64_t pts, bool idr, EncodedFrame &out,
                std::string &e) override {
        out = {};
        e.clear();
        if (pts > uint64_t(INT64_MAX) / 10) {
            e = "PTS overflow";
            return false;
        }
        if (!gpu.convert(rgba, stride, e))
            return false;
        if (events && !event(METransformNeedInput, e))
            return false;
        if (idr && !property(CODECAPI_AVEncVideoForceKeyFrame, 1)) {
            e = "Force keyframe unsupported";
            return false;
        }
        ComPtr<IMFSample> sample;
        ComPtr<IMFMediaBuffer> buffer;
        if (!hr(MFCreateSample(&sample), "Input sample", e))
            return false;
        if (hardware) {
            if (!hr(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), gpu.nv12.Get(), 0, FALSE,
                                              &buffer),
                    "Surface buffer", e))
                return false;
        } else {
            auto texture = staging[slot].Get();
            slot = (slot + 1) % static_cast<unsigned>(staging.size());
            gpu.context->CopyResource(texture, gpu.nv12.Get());
            uint32_t size = uint32_t(config.out_w) * config.out_h * 3 / 2;
            if (!hr(MFCreateMemoryBuffer(size, &buffer), "NV12 memory buffer", e))
                return false;
            BYTE *dest = nullptr;
            if (!hr(buffer->Lock(&dest, nullptr, nullptr), "Lock NV12", e))
                return false;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            auto h = gpu.context->Map(texture, 0, D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(h)) {
                auto src = static_cast<const uint8_t *>(mapped.pData);
                for (uint32_t y = 0; y < uint32_t(config.out_h) * 3 / 2; ++y)
                    std::memcpy(dest + size_t(y) * config.out_w, src + size_t(y) * mapped.RowPitch,
                                config.out_w);
                gpu.context->Unmap(texture, 0);
            }
            buffer->Unlock();
            if (!hr(h, "Read staging NV12", e))
                return false;
            buffer->SetCurrentLength(size);
        }
        if (!hr(sample->AddBuffer(buffer.Get()), "Input AddBuffer", e) ||
            !hr(sample->SetSampleTime(static_cast<LONGLONG>(pts * 10)), "Sample time", e) ||
            !hr(sample->SetSampleDuration(10000000 / config.fps), "Sample duration", e) ||
            !hr(transform->ProcessInput(input_id, sample.Get(), 0), "MFT ProcessInput", e))
            return false;
        if (events && !event(METransformHaveOutput, e))
            return false;
        return output(out, e);
    }
    bool set_rate(uint8_t fps, uint32_t kbps) override {
        if (!fps || !kbps || kbps > UINT32_MAX / 1000)
            return false;
        if (!property(CODECAPI_AVEncCommonMeanBitRate, kbps * 1000))
            return false;
        VARIANT rate{};
        rate.vt = VT_UI8;
        rate.ullVal = (uint64_t(fps) << 32) | 1;
        if (fps != config.fps && FAILED(codec->SetValue(&CODECAPI_AVEncMaxFrameRate, &rate))) {
            property(CODECAPI_AVEncCommonMeanBitRate, config.bitrate_kbps * 1000);
            return false;
        }
        config.fps = fps;
        config.bitrate_kbps = kbps;
        return true;
    }
};
} // namespace
std::unique_ptr<Encoder> make_mf(const EncoderConfig &c, bool hw, std::string &e) {
    Runtime r;
    if (!r.init(e))
        return {};
    Activations a;
    if (!a.init(hw, e))
        return {};
    if (!a.count) {
        e = hw ? "No hardware H264 MFT" : "No software H264 MFT";
        return {};
    }
    std::string reasons;
    for (UINT32 i = 0; i < a.count; ++i) {
        auto encoder = std::make_unique<Mf>(hw);
        std::string reason;
        if (encoder->init(c, a.items[i], reason))
            return encoder;
        if (!reasons.empty())
            reasons += "; ";
        reasons += reason;
    }
    e = reasons;
    return {};
}
bool probe_mf(bool hw) {
    Runtime r;
    std::string e;
    if (!r.init(e, true))
        return false;
    Activations a;
    return a.init(hw, e) && a.count != 0;
}
} // namespace handview::encode::detail
