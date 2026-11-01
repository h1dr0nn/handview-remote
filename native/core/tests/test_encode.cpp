#include "test_support.hpp"
#include "handview/encode/encoder.hpp"
#include "handview/encode/h264.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <wrl/client.h>
using namespace handview::encode;
using Microsoft::WRL::ComPtr;
static_assert(static_cast<uint8_t>(Backend::Nvenc) == 0);
static_assert(static_cast<uint8_t>(Backend::MfHardware) == 1);
static_assert(static_cast<uint8_t>(Backend::MfSoftware) == 2);
namespace {
struct BitWriter {
    std::vector<uint8_t> data;
    unsigned pos = 0;
    void bits(uint32_t n, unsigned count) {
        while (count--) {
            if (pos % 8 == 0)
                data.push_back(0);
            data.back() |= uint8_t(((n >> count) & 1) << (7 - pos % 8));
            ++pos;
        }
    }
    void ue(uint32_t n) {
        ++n;
        unsigned width = 0;
        for (auto x = n; x; x >>= 1)
            ++width;
        bits(0, width - 1);
        bits(n, width);
    }
};
std::vector<uint8_t> sps_fixture(bool interlaced = false) {
    BitWriter b;
    b.bits(77, 8);
    b.bits(0, 8);
    b.bits(31, 8);
    b.ue(0);
    b.ue(0);
    b.ue(0);
    b.ue(0);
    b.ue(1);
    b.bits(0, 1);
    b.ue(79);
    b.ue(interlaced ? 22 : 45);
    b.bits(interlaced ? 0 : 1, 1);
    if (interlaced)
        b.bits(0, 1);
    b.bits(1, 1);
    b.bits(1, 1);
    b.ue(0);
    b.ue(0);
    b.ue(0);
    b.ue(interlaced ? 4 : 8);
    b.bits(0, 1);
    b.bits(1, 1);
    std::vector<uint8_t> result{0x67};
    unsigned zeros = 0;
    for (auto byte : b.data) {
        if (zeros >= 2 && byte <= 3) {
            result.push_back(3);
            zeros = 0;
        }
        result.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return result;
}
void utilities() {
    const std::vector<uint8_t> annex{0,    0, 0, 1, 0x67, 0x12, 0,    0, 1, 0x68,
                                     0x34, 0, 0, 0, 1,    0x65, 0x56, 0, 0};
    auto nals = split_annex_b(annex);
    CHECK(nals.size() == 3);
    if (nals.size() == 3) {
        CHECK(nal_type(nals[0]) == 7);
        CHECK(nal_type(nals[1]) == 8);
        CHECK(nal_type(nals[2]) == 5);
        CHECK(nals[2].size() == 2);
    }
    CHECK(nal_type({}) == 0);
    CHECK(split_annex_b({}).empty());
    CHECK(split_annex_b(std::vector<uint8_t>{1, 2, 3}).empty());
    std::vector<uint8_t> converted;
    CHECK(to_annex_b(annex, converted));
    CHECK(converted == annex);
    const std::vector<uint8_t> avcc{0, 0,    0,    2, 0x67, 0x12, 0, 0,    0,
                                    2, 0x68, 0x34, 0, 0,    0,    2, 0x65, 0x56};
    CHECK(to_annex_b(avcc, converted));
    CHECK(split_annex_b(converted).size() == 3);
    CHECK(to_annex_b(std::vector<uint8_t>{2, 0x65, 0x11}, converted, 1));
    CHECK(to_annex_b(std::vector<uint8_t>{0, 2, 0x65, 0x11}, converted, 2));
    CHECK(to_annex_b(std::vector<uint8_t>{0, 0, 2, 0x65, 0x11}, converted, 3));
    std::vector<uint8_t> ambiguous(260, 0x55);
    ambiguous[0] = 0;
    ambiguous[1] = 0;
    ambiguous[2] = 1;
    ambiguous[3] = 0;
    ambiguous[4] = 0x65;
    CHECK(to_annex_b(ambiguous, converted));
    CHECK(converted.size() == 260 && converted[3] == 1 && converted[4] == 0x65);
    auto previous = converted;
    CHECK(!to_annex_b(std::vector<uint8_t>{0, 0, 0, 4, 0x65}, converted));
    CHECK(converted == previous);
    CHECK(!to_annex_b(std::vector<uint8_t>{0, 0, 0, 0}, converted));
    CHECK(!to_annex_b(std::vector<uint8_t>{0, 0, 0}, converted));
    CHECK(!to_annex_b({}, converted));
    CHECK(!to_annex_b(avcc, converted, 0));
    for (bool interlaced : {false, true}) {
        auto sps = sps_fixture(interlaced);
        auto info = parse_sps(sps);
        CHECK(info.has_value());
        if (info) {
            CHECK(info->profile_idc == 77);
            CHECK(info->level_idc == 31);
            CHECK(info->width == 1280);
            CHECK(info->height == 720);
        }
        for (size_t i = 0; i < sps.size() - 1; ++i)
            CHECK(!parse_sps(std::span(sps).first(i)));
    }
    CHECK(!parse_sps(std::vector<uint8_t>{0x68, 0, 0, 0}));
    CHECK(!parse_sps(std::vector<uint8_t>(100, 0)));
    EncoderConfig bad{1920, 1080, 1279, 720, 60, 8000};
    std::string error;
    CHECK(!create_encoder(bad, {}, error));
    CHECK(!error.empty());
}
// Decode every access unit to catch broken references after rate changes.
class Decoder {
    ComPtr<IMFTransform> transform;
    bool com = false, mf = false;

  public:
    unsigned frames = 0;
    ~Decoder() {
        transform.Reset();
        if (mf)
            MFShutdown();
        if (com)
            CoUninitialize();
    }
    bool init() {
        com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        if (!com)
            return false;
        mf = SUCCEEDED(MFStartup(MF_VERSION));
        if (!mf)
            return false;
        IMFActivate **activations = nullptr;
        UINT32 count = 0;
        MFT_REGISTER_TYPE_INFO input{MFMediaType_Video, MFVideoFormat_H264};
        auto h = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT, &input, nullptr,
                           &activations, &count);
        if (FAILED(h))
            return false;
        if (count)
            h = activations[0]->ActivateObject(IID_PPV_ARGS(&transform));
        for (UINT32 i = 0; i < count; ++i)
            activations[i]->Release();
        CoTaskMemFree(activations);
        if (!count || FAILED(h))
            return false;
        ComPtr<IMFMediaType> type;
        MFCreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, 1280, 720);
        MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 60, 1);
        type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(transform->SetInputType(0, type.Get(), 0)) || !set_output())
            return false;
        transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        return SUCCEEDED(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0));
    }
    bool set_output() {
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> t;
            if (FAILED(transform->GetOutputAvailableType(0, i, &t)))
                return false;
            GUID subtype{};
            t->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (subtype == MFVideoFormat_NV12)
                return SUCCEEDED(transform->SetOutputType(0, t.Get(), 0));
        }
    }
    bool pull() {
        for (unsigned i = 0; i < 256; ++i) {
            MFT_OUTPUT_STREAM_INFO info{};
            if (FAILED(transform->GetOutputStreamInfo(0, &info)))
                return false;
            ComPtr<IMFSample> supplied;
            if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                ComPtr<IMFMediaBuffer> buffer;
                MFCreateSample(&supplied);
                if (FAILED(MFCreateMemoryBuffer((std::max<DWORD>)(info.cbSize, 1280 * 720 * 3),
                                                &buffer)))
                    return false;
                supplied->AddBuffer(buffer.Get());
            }
            MFT_OUTPUT_DATA_BUFFER out{};
            out.pSample = supplied.Get();
            DWORD flags = 0;
            auto h = transform->ProcessOutput(0, 1, &out, &flags);
            if (out.pEvents)
                out.pEvents->Release();
            ComPtr<IMFSample> sample;
            if (out.pSample && out.pSample != supplied.Get())
                sample.Attach(out.pSample);
            else
                sample = supplied;
            if (h == MF_E_TRANSFORM_NEED_MORE_INPUT)
                return true;
            if (h == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (!set_output())
                    return false;
                continue;
            }
            if (FAILED(h) || !sample) {
                std::printf("Decoder output HRESULT %08lX, frames %u\n",
                            static_cast<unsigned long>(h), frames);
                return false;
            }
            ComPtr<IMFMediaType> type;
            transform->GetOutputCurrentType(0, &type);
            UINT32 w = 0, hgt = 0;
            MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &hgt);
            CHECK(w == 1280 && hgt == 720);
            ++frames;
        }
        return false;
    }
    bool feed(const EncodedFrame &frame, uint64_t pts) {
        ComPtr<IMFSample> sample;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(MFCreateSample(&sample)) ||
            FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(frame.data.size()), &buffer)))
            return false;
        BYTE *bytes = nullptr;
        if (FAILED(buffer->Lock(&bytes, nullptr, nullptr)))
            return false;
        std::copy(frame.data.begin(), frame.data.end(), bytes);
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(frame.data.size()));
        sample->AddBuffer(buffer.Get());
        sample->SetSampleTime(static_cast<LONGLONG>(pts * 10));
        sample->SetSampleDuration(10000000 / 60);
        if (FAILED(transform->ProcessInput(0, sample.Get(), 0)))
            return false;
        return pull();
    }
    bool finish() {
        transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        return pull();
    }
};
void integration(Backend backend, const char *name, uint8_t profile = 77) {
    EncoderConfig config{1920, 1080, 1280, 720, 60, 8000, profile};
    std::string error;
    auto encoder = create_encoder(config, backend, error);
    if (!encoder) {
        std::printf("SKIP %s: %s\n", name, error.c_str());
        return;
    }
    CHECK(error.empty());
    CHECK(encoder->backend() == backend);
    Decoder decoder;
    if (!decoder.init()) {
        CHECK(false);
        std::printf("H264 test decoder initialization failed\n");
        return;
    }
    constexpr uint32_t stride = 1920 * 4 + 32;
    std::vector<uint8_t> rgba(size_t(stride) * 1080, 0xcd);
    std::vector<double> times;
    uint64_t pts = 0;
    uint8_t fps = 60;
    for (unsigned frame = 0; frame < 120; ++frame) {
        for (unsigned y = 0; y < 1080; ++y)
            for (unsigned x = 0; x < 1920; ++x) {
                auto p = &rgba[size_t(y) * stride + x * 4];
                p[0] = uint8_t((x + frame * 4) / 8);
                p[1] = uint8_t((y + frame * 2) / 4);
                p[2] = uint8_t((x + y + frame * 8) / 16);
                p[3] = 255;
            }
        if (frame == 90) {
            if (encoder->set_rate(30, 6000)) {
                fps = 30;
                std::printf("%s: live rate changed to 30 fps, 6000 kbps\n", name);
            } else {
                CHECK(encoder->set_rate(60, 6000));
                std::printf("%s: live bitrate changed to 6000 kbps; FPS change unsupported\n",
                            name);
            }
        }
        EncodedFrame output;
        auto start = std::chrono::steady_clock::now();
        bool ok =
            encoder->encode(rgba.data(), stride, pts, frame == 0 || frame == 60, output, error);
        auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (!ok) {
            std::printf("FAIL %s frame %u: %s\n", name, frame, error.c_str());
            CHECK(ok);
            break;
        }
        times.push_back(elapsed);
        CHECK(!output.data.empty());
        bool sps = false, pps = false, idr = false;
        for (auto nal : split_annex_b(output.data)) {
            sps |= nal_type(nal) == 7;
            pps |= nal_type(nal) == 8;
            if (nal_type(nal) == 5) {
                CHECK(sps || (frame != 0 && frame != 60));
                CHECK(pps || (frame != 0 && frame != 60));
                idr = true;
            }
            if (nal_type(nal) == 7) {
                auto info = parse_sps(nal);
                CHECK(info.has_value());
                if (info) {
                    CHECK(info->width == 1280 && info->height == 720);
                    CHECK(info->profile_idc == profile);
                }
            }
        }
        CHECK(output.keyframe == idr);
        CHECK(output.has_codec_config == (sps && pps));
        CHECK(idr == (frame == 0 || frame == 60));
        if (frame == 0 || frame == 60)
            CHECK(sps && pps && idr);
        CHECK(decoder.feed(output, pts));
        pts += 1000000 / fps;
    }
    CHECK(decoder.finish());
    std::printf("Decoded %u frames\n", decoder.frames);
    CHECK(decoder.frames == 120);
    if (!times.empty()) {
        std::sort(times.begin(), times.end());
        std::printf("%s profile %u: %zu frames, encode p50 %.3f ms, p95 %.3f ms\n", name, profile,
                    times.size(), times[times.size() / 2], times[(times.size() - 1) * 95 / 100]);
    }
}
} // namespace
int main() {
    utilities();
    integration(Backend::Nvenc, "nvenc");
    integration(Backend::MfHardware, "mf_hw");
    integration(Backend::MfSoftware, "mf_sw");
    integration(Backend::Nvenc, "nvenc", 66);
    integration(Backend::MfHardware, "mf_hw", 66);
    integration(Backend::MfSoftware, "mf_sw", 66);
    return test_result("encode");
}
