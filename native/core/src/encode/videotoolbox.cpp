// H.264 through VideoToolbox, the hardware encoder of every Apple Silicon Mac.
// The game hands over RGBA in CPU memory (DECISIONS D12, D23); vImage scales
// it and converts it to NV12 (BT.709 limited range, D10), and VideoToolbox
// encodes one frame per call. The SPS gets VUI bitstream_restriction so
// decoders output every frame without delay.
#include "internal.hpp"
#include <Accelerate/Accelerate.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <cstring>
#include <vector>
namespace handview::encode::detail {
namespace {
template <class T> struct CfRef {
    T value = nullptr;
    CfRef() = default;
    explicit CfRef(T v) : value(v) {}
    ~CfRef() {
        if (value)
            CFRelease(value);
    }
    CfRef(const CfRef &) = delete;
    CfRef &operator=(const CfRef &) = delete;
    T *out() { return &value; }
    operator T() const { return value; }
};
bool ok(OSStatus status, const char *what, std::string &e) {
    if (status == noErr)
        return true;
    char text[128];
    std::snprintf(text, sizeof(text), "%s: OSStatus %d", what, static_cast<int>(status));
    e = text;
    return false;
}
CFNumberRef number(int32_t value) {
    return CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
}
bool set(VTCompressionSessionRef session, CFStringRef key, int32_t value) {
    CfRef<CFNumberRef> n(number(value));
    return VTSessionSetProperty(session, key, n) == noErr;
}
CFDictionaryRef hardware_specification() {
    const void *keys[] = {kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder};
    const void *values[] = {kCFBooleanTrue};
    return CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
}
CFDictionaryRef source_attributes(uint32_t w, uint32_t h) {
    CfRef<CFNumberRef> format(number(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange));
    CfRef<CFNumberRef> width(number(static_cast<int32_t>(w)));
    CfRef<CFNumberRef> height(number(static_cast<int32_t>(h)));
    CfRef<CFDictionaryRef> surface(CFDictionaryCreate(kCFAllocatorDefault, nullptr, nullptr, 0,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks));
    const void *keys[] = {kCVPixelBufferPixelFormatTypeKey, kCVPixelBufferWidthKey,
                          kCVPixelBufferHeightKey, kCVPixelBufferIOSurfacePropertiesKey};
    const void *values[] = {format, width, height, surface};
    return CFDictionaryCreate(kCFAllocatorDefault, keys, values, 4, &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
}

class VideoToolbox final : public Encoder {
    EncoderConfig config{};
    VTCompressionSessionRef session = nullptr;
    vImage_ARGBToYpCbCr conversion{};
    std::vector<uint8_t> scaled;
    // Filled by the output callback, which VTCompressionSessionCompleteFrames
    // runs before it returns.
    std::vector<uint8_t> *output = nullptr;
    OSStatus callback_status = noErr;
    bool produced = false;
    bool keyframe = false;
    std::string callback_error;

    static void on_output(void *self, void *, OSStatus status, VTEncodeInfoFlags,
                          CMSampleBufferRef sample) {
        static_cast<VideoToolbox *>(self)->collect(status, sample);
    }
    void collect(OSStatus status, CMSampleBufferRef sample) {
        callback_status = status;
        if (status != noErr || !sample || !output)
            return;
        bool sync = true;
        if (auto attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
            attachments && CFArrayGetCount(attachments) > 0) {
            auto first = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
            sync = !CFDictionaryContainsKey(first, kCMSampleAttachmentKey_NotSync);
        }
        if (sync && !append_parameter_sets(CMSampleBufferGetFormatDescription(sample)))
            return;
        CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
        size_t length = 0;
        char *bytes = nullptr;
        if (!block || CMBlockBufferGetDataPointer(block, 0, nullptr, &length, &bytes) != noErr) {
            callback_error = "VideoToolbox output without data";
            return;
        }
        // AVCC (4 byte big-endian lengths) to Annex-B.
        for (size_t i = 0; i + 4 <= length;) {
            const uint32_t n = (uint32_t(uint8_t(bytes[i])) << 24) |
                               (uint32_t(uint8_t(bytes[i + 1])) << 16) |
                               (uint32_t(uint8_t(bytes[i + 2])) << 8) | uint8_t(bytes[i + 3]);
            i += 4;
            if (n == 0 || n > length - i) {
                callback_error = "VideoToolbox output has a bad NAL length";
                return;
            }
            output->insert(output->end(), {0, 0, 0, 1});
            output->insert(output->end(), bytes + i, bytes + i + n);
            i += n;
        }
        keyframe = sync;
        produced = true;
    }
    bool append_parameter_sets(CMFormatDescriptionRef format) {
        size_t count = 0;
        if (!format || CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                           format, 0, nullptr, nullptr, &count, nullptr) != noErr) {
            callback_error = "VideoToolbox keyframe without parameter sets";
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            const uint8_t *set = nullptr;
            size_t size = 0;
            if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &set, &size, nullptr,
                                                                   nullptr) != noErr) {
                callback_error = "VideoToolbox parameter set unreadable";
                return false;
            }
            output->insert(output->end(), {0, 0, 0, 1});
            // VideoToolbox leaves out VUI bitstream_restriction, so decoders
            // such as Exynos hold several frames back for reordering that never
            // happens; the host then waits for acks that do not come.
            if (size && (set[0] & 31) == 7) {
                auto sps = sps_without_reordering({set, size});
                if (!sps) {
                    callback_error = "VideoToolbox SPS could not be rewritten";
                    return false;
                }
                output->insert(output->end(), sps->begin(), sps->end());
            } else {
                output->insert(output->end(), set, set + size);
            }
        }
        return true;
    }
    bool rate(uint8_t fps, uint32_t kbps) {
        const int64_t bits = int64_t(kbps) * 1000;
        // Cap any one second at 1.5x the average, so an IDR cannot flood the link.
        double limit_bytes = double(bits) / 8 * 1.5, seconds = 1.0;
        CfRef<CFNumberRef> b(CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &limit_bytes));
        CfRef<CFNumberRef> s(CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &seconds));
        const void *pair[] = {b, s};
        CfRef<CFArrayRef> limits(CFArrayCreate(kCFAllocatorDefault, pair, 2, &kCFTypeArrayCallBacks));
        if (!set(session, kVTCompressionPropertyKey_AverageBitRate, static_cast<int32_t>(bits)) ||
            !set(session, kVTCompressionPropertyKey_ExpectedFrameRate, fps))
            return false;
        VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, limits);
        return true;
    }
    bool convert(const uint8_t *rgba, uint32_t stride, CVPixelBufferRef target, std::string &e) {
        vImage_Buffer source{const_cast<uint8_t *>(rgba), config.in_h, config.in_w, stride};
        if (config.in_w != config.out_w || config.in_h != config.out_h) {
            // RGBA scales like ARGB: vImage only cares that there are four channels.
            vImage_Buffer to{scaled.data(), config.out_h, config.out_w, size_t(config.out_w) * 4};
            if (vImageScale_ARGB8888(&source, &to, nullptr, kvImageNoFlags) != kvImageNoError) {
                e = "vImage scale failed";
                return false;
            }
            source = to;
        }
        if (CVPixelBufferLockBaseAddress(target, 0) != kCVReturnSuccess) {
            e = "Cannot lock the encoder input buffer";
            return false;
        }
        vImage_Buffer y{CVPixelBufferGetBaseAddressOfPlane(target, 0), config.out_h, config.out_w,
                        CVPixelBufferGetBytesPerRowOfPlane(target, 0)};
        vImage_Buffer uv{CVPixelBufferGetBaseAddressOfPlane(target, 1), config.out_h / 2u,
                         config.out_w / 2u, CVPixelBufferGetBytesPerRowOfPlane(target, 1)};
        // Source bytes are R, G, B, A; the converter reads A, R, G, B.
        const uint8_t permute[4] = {3, 0, 1, 2};
        const auto status = vImageConvert_ARGB8888To420Yp8_CbCr8(&source, &y, &uv, &conversion,
                                                                 permute, kvImageNoFlags);
        CVPixelBufferUnlockBaseAddress(target, 0);
        if (status != kvImageNoError) {
            e = "vImage NV12 conversion failed";
            return false;
        }
        return true;
    }

  public:
    ~VideoToolbox() override {
        if (session) {
            VTCompressionSessionInvalidate(session);
            CFRelease(session);
        }
    }
    bool init(const EncoderConfig &c, std::string &e) {
        config = c;
        if (c.in_w != c.out_w || c.in_h != c.out_h)
            scaled.resize(size_t(c.out_w) * c.out_h * 4);
        // BT.709 limited range: Y 16..235, Cb and Cr 16..240.
        const vImage_YpCbCrPixelRange range{16, 128, 235, 240, 235, 16, 240, 16};
        if (vImageConvert_ARGBToYpCbCr_GenerateConversion(
                kvImage_ARGBToYpCbCrMatrix_ITU_R_709_2, &range, &conversion, kvImageARGB8888,
                kvImage420Yp8_CbCr8, kvImageNoFlags) != kvImageNoError) {
            e = "vImage BT.709 conversion setup failed";
            return false;
        }
        CfRef<CFDictionaryRef> specification(hardware_specification());
        CfRef<CFDictionaryRef> attributes(source_attributes(c.out_w, c.out_h));
        if (!ok(VTCompressionSessionCreate(kCFAllocatorDefault, c.out_w, c.out_h,
                                           kCMVideoCodecType_H264, specification, attributes,
                                           kCFAllocatorDefault, &VideoToolbox::on_output, this,
                                           &session),
                "VTCompressionSessionCreate (hardware H.264)", e))
            return false;
        const auto profile = c.profile == 66 ? kVTProfileLevel_H264_Baseline_AutoLevel
                                             : kVTProfileLevel_H264_Main_AutoLevel;
        if (!ok(VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, profile),
                "H.264 profile", e) ||
            !ok(VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue),
                "Real-time mode", e) ||
            !ok(VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering,
                                     kCFBooleanFalse),
                "No B-frames", e))
            return false;
        // One frame in, one frame out; IDRs only on request (infinite GOP, like MF and NVENC).
        set(session, kVTCompressionPropertyKey_MaxFrameDelayCount, 0);
        set(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, INT32_MAX);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration,
                             CfRef<CFNumberRef>(number(0)));
        VTSessionSetProperty(session, kVTCompressionPropertyKey_ColorPrimaries,
                             kCVImageBufferColorPrimaries_ITU_R_709_2);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_TransferFunction,
                             kCVImageBufferTransferFunction_ITU_R_709_2);
        VTSessionSetProperty(session, kVTCompressionPropertyKey_YCbCrMatrix,
                             kCVImageBufferYCbCrMatrix_ITU_R_709_2);
        if (!rate(c.fps, c.bitrate_kbps)) {
            e = "VideoToolbox rejected the bitrate or frame rate";
            return false;
        }
        return ok(VTCompressionSessionPrepareToEncodeFrames(session), "Prepare to encode", e);
    }
    Backend backend() const override {
        return Backend::VideoToolbox;
    }
    bool encode(const uint8_t *rgba, uint32_t stride, uint64_t pts, bool idr, EncodedFrame &out,
                std::string &e) override {
        if (!rgba || stride < uint64_t(config.in_w) * 4) {
            e = "Invalid RGBA pointer or stride";
            return false;
        }
        CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(session);
        CfRef<CVPixelBufferRef> buffer;
        if (!pool || CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, buffer.out()) !=
                         kCVReturnSuccess) {
            e = "No encoder input buffer";
            return false;
        }
        CVBufferSetAttachment(buffer, kCVImageBufferColorPrimariesKey,
                              kCVImageBufferColorPrimaries_ITU_R_709_2,
                              kCVAttachmentMode_ShouldPropagate);
        CVBufferSetAttachment(buffer, kCVImageBufferTransferFunctionKey,
                              kCVImageBufferTransferFunction_ITU_R_709_2,
                              kCVAttachmentMode_ShouldPropagate);
        CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey,
                              kCVImageBufferYCbCrMatrix_ITU_R_709_2,
                              kCVAttachmentMode_ShouldPropagate);
        if (!convert(rgba, stride, buffer, e))
            return false;
        CfRef<CFDictionaryRef> options;
        if (idr) {
            const void *keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
            const void *values[] = {kCFBooleanTrue};
            options.value = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
        }
        out.data.clear();
        output = &out.data;
        produced = false;
        callback_status = noErr;
        callback_error.clear();
        const CMTime time = CMTimeMake(static_cast<int64_t>(pts), 1000000);
        const CMTime duration = CMTimeMake(1, config.fps);
        OSStatus status = VTCompressionSessionEncodeFrame(session, buffer, time, duration, options,
                                                          nullptr, nullptr);
        if (status == noErr)
            status = VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
        output = nullptr;
        if (!ok(status, "VTCompressionSessionEncodeFrame", e) ||
            !ok(callback_status, "VideoToolbox encode", e))
            return false;
        if (!callback_error.empty()) {
            e = callback_error;
            return false;
        }
        if (!produced) {
            e = "VideoToolbox dropped the frame";
            return false;
        }
        classify(out);
        if (idr && !out.keyframe) {
            e = "VideoToolbox ignored the keyframe request";
            return false;
        }
        return true;
    }
    bool set_rate(uint8_t fps, uint32_t kbps) override {
        if (!rate(fps, kbps))
            return false;
        config.fps = fps;
        config.bitrate_kbps = kbps;
        return true;
    }
};
} // namespace
std::unique_ptr<Encoder> make_videotoolbox(const EncoderConfig &c, std::string &e) {
    auto encoder = std::make_unique<VideoToolbox>();
    if (!encoder->init(c, e))
        return {};
    return encoder;
}
bool probe_videotoolbox() {
    std::string ignored;
    VideoToolbox encoder;
    return encoder.init(EncoderConfig{640, 360, 640, 360, 30, 2000}, ignored);
}
} // namespace handview::encode::detail
