#include "handview/stream/stream.hpp"

#include "handview/encode/encoder.hpp"
#include "handview/session/session.hpp"

#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace handview::stream {

using namespace protocol;

std::uint64_t now_us() {
    static const std::uint64_t frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return static_cast<std::uint64_t>(value.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    const auto ticks = static_cast<std::uint64_t>(counter.QuadPart);
    // Split to avoid overflowing ticks * 1e6.
    return (ticks / frequency) * 1000000u + (ticks % frequency) * 1000000u / frequency;
}

namespace {

struct CapturedFrame {
    std::vector<std::uint8_t> rgba;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::uint32_t touch_seq = 0;
    std::uint64_t pts_us = 0;
};

struct QueuedTouch {
    std::uint32_t seq = 0;
    TouchEvent event;
};

const char* encoder_label(encode::Backend backend) {
    switch (backend) {
        case encode::Backend::Nvenc: return "NVENC";
        case encode::Backend::MfHardware: return "MF Hardware";
        case encode::Backend::MfSoftware: return "MF Software";
    }
    return "";
}

}  // namespace

struct StreamController::Impl {
    Impl(StreamOptions o, net::LogSink l)
        : options(o), log(std::move(l)), link(log), keyframes(o.keyframe) {}

    StreamOptions options;
    net::LogSink log;
    session::GameLink link;
    std::thread worker;
    std::atomic<bool> running{false};

    // Shared between the engine main thread and the worker, guarded by mutex.
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool streaming = false;
    LinkStart caps{};
    std::uint8_t fps = 60;
    flow::InFlightLimiter inflight;
    std::optional<CapturedFrame> pending;
    bool encoding = false;
    std::uint64_t last_capture_us = 0;
    std::vector<QueuedTouch> touches;
    std::map<std::uint8_t, TouchEvent> held;
    std::uint32_t last_touch_seq = 0;
    StreamStatus status;

    // Engine main thread only.
    std::uint32_t delivered_touch_seq = 0;

    // Worker thread only.
    std::unique_ptr<encode::Encoder> encoder;
    std::uint32_t encoder_in_w = 0;
    std::uint32_t encoder_in_h = 0;
    flow::StreamSize size{};
    std::uint32_t bitrate_kbps = 0;
    std::optional<flow::DecoderBackpressure> backpressure;
    flow::KeyframeLimiter keyframes;
    bool force_idr = true;
    bool reinit_tried = false;
    std::uint32_t frame_seq = 0;

    void say(const std::string& text) const {
        if (log) log(text);
    }

    void release_touches_locked() {
        for (auto& [id, event] : held) {
            TouchEvent canceled = event;
            canceled.phase = Phase::Canceled;
            touches.push_back({last_touch_seq, canceled});
        }
        held.clear();
    }

    void stop_stream(const std::string& error) {
        {
            std::lock_guard lock(mutex);
            streaming = false;
            pending.reset();
            release_touches_locked();
            status.streaming = false;
            if (!error.empty()) status.last_error = error;
        }
        encoder.reset();
    }

    void handle(const Message& message) {
        const auto now = now_us();
        if (const auto* start = std::get_if<LinkStart>(&message)) {
            {
                std::lock_guard lock(mutex);
                caps = *start;
                fps = start->maxFps != 0 ? start->maxFps : 60;
                streaming = true;
                pending.reset();
                inflight.reset();
                last_capture_us = 0;
                status.streaming = true;
                status.last_error.clear();
            }
            encoder.reset();
            size = {};
            keyframes.reset();
            force_idr = true;
            reinit_tried = false;
            frame_seq = 0;
        } else if (std::holds_alternative<LinkStop>(message)) {
            stop_stream({});
        } else if (const auto* touch = std::get_if<Touch>(&message)) {
            std::lock_guard lock(mutex);
            last_touch_seq = touch->touchSeq;
            for (const auto& pointer : touch->pointers) {
                const TouchEvent event{pointer.id, pointer.phase, pointer.x, pointer.y, pointer.pressure};
                touches.push_back({touch->touchSeq, event});
                if (pointer.phase == Phase::Ended || pointer.phase == Phase::Canceled) {
                    held.erase(pointer.id);
                } else {
                    held[pointer.id] = event;
                }
            }
        } else if (const auto* ack = std::get_if<FrameAck>(&message)) {
            std::lock_guard lock(mutex);
            inflight.on_ack(ack->frameSeq);
        } else if (const auto* stats = std::get_if<DecoderStats>(&message)) {
            if (!backpressure) return;
            backpressure->on_stats(stats->pendingOutputs, now);
            if (backpressure->changed()) {
                const auto target_fps = backpressure->target_fps();
                bitrate_kbps = backpressure->target_bitrate_kbps();
                if (encoder) encoder->set_rate(target_fps, bitrate_kbps);
                std::lock_guard lock(mutex);
                fps = target_fps;
                status.fps = target_fps;
                status.bitrate_kbps = bitrate_kbps;
            }
        } else if (const auto* view = std::get_if<ViewSize>(&message)) {
            std::lock_guard lock(mutex);
            status.view_width = view->width;
            status.view_height = view->height;
            status.safe_left = view->safeLeft;
            status.safe_top = view->safeTop;
            status.safe_right = view->safeRight;
            status.safe_bottom = view->safeBottom;
        } else if (std::holds_alternative<RequestKeyframe>(message)) {
            // The app is resyncing and dropped what it had without acking it;
            // without this reset the in-flight limit could hold the IDR back.
            {
                std::lock_guard lock(mutex);
                inflight.reset();
            }
            if (keyframes.request(now)) force_idr = true;
        }
    }

    bool open_encoder(const CapturedFrame& frame, std::uint8_t stream_fps) {
        encoder.reset();
        encode::EncoderConfig config{frame.width, frame.height, size.width, size.height, stream_fps, bitrate_kbps};
        std::string error;
        encoder = encode::create_encoder(config, std::nullopt, error);
        if (!encoder) {
            say("encoder init failed: " + error);
            return false;
        }
        encoder_in_w = frame.width;
        encoder_in_h = frame.height;
        force_idr = true;
        std::lock_guard lock(mutex);
        status.encoder = encoder_label(encoder->backend());
        return true;
    }

    void encode_and_send(CapturedFrame& frame) {
        LinkStart current_caps;
        std::uint8_t current_fps;
        {
            std::lock_guard lock(mutex);
            if (!streaming) return;
            current_caps = caps;
            current_fps = fps;
        }

        const auto capped = flow::cap_resolution(frame.width, frame.height, current_caps.maxW, current_caps.maxH, 0, 0);
        const bool size_changed = capped.width != size.width || capped.height != size.height;
        if (!encoder || size_changed || frame.width != encoder_in_w || frame.height != encoder_in_h) {
            if (size_changed) {
                size = capped;
                bitrate_kbps = current_caps.bitrateKbps != 0
                                   ? current_caps.bitrateKbps
                                   : flow::default_bitrate_kbps(size.width, size.height, current_fps);
                backpressure.emplace(current_fps, bitrate_kbps, options.backpressure);
                link.send_stream_config_changed({size.width, size.height, static_cast<Orientation>(size.orientation),
                                                 current_fps, bitrate_kbps});
                std::lock_guard lock(mutex);
                status.width = size.width;
                status.height = size.height;
                status.fps = current_fps;
                status.bitrate_kbps = bitrate_kbps;
            }
            if (!open_encoder(frame, current_fps)) {
                link.send_error(ErrorSource::Encoder, ErrorCode::InitFailed);
                stop_stream("Encoder could not start.");
                return;
            }
        }

        const auto now = now_us();
        if (keyframes.pending_due(now)) force_idr = true;

        encode::EncodedFrame out;
        std::string error;
        bool ok = encoder->encode(frame.rgba.data(), frame.stride, frame.pts_us, force_idr, out, error);
        if (!ok && !reinit_tried) {
            // One re-initialization, then an IDR (SPEC 6.6).
            reinit_tried = true;
            say("encode failed, re-initializing: " + error);
            ok = open_encoder(frame, current_fps) &&
                 encoder->encode(frame.rgba.data(), frame.stride, frame.pts_us, true, out, error);
        }
        if (!ok) {
            link.send_error(ErrorSource::Encoder, ErrorCode::ProcessFailed);
            stop_stream("Encoder failed: " + error);
            return;
        }
        reinit_tried = false;
        if (out.keyframe) {
            keyframes.on_idr_sent(now);
            force_idr = false;
        }

        auto flags = static_cast<std::uint8_t>(out.keyframe ? VideoFlags::Keyframe : VideoFlags::None);
        if (out.has_codec_config) flags |= static_cast<std::uint8_t>(VideoFlags::CodecConfig);
        VideoFrame video{++frame_seq, frame.touch_seq, frame.pts_us, static_cast<VideoFlags>(flags), std::move(out.data)};
        const auto seq = video.frameSeq;
        if (!link.send_video_frame(video)) return;
        std::lock_guard lock(mutex);
        inflight.on_frame_sent(seq);
        ++status.frames_sent;
    }

    void run() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (running.load()) {
            while (auto message = link.poll()) handle(*message);
            if (!link.connected()) {
                stop_stream("The editor link closed.");
                break;
            }
            std::optional<CapturedFrame> frame;
            {
                std::unique_lock lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(2), [&] { return pending.has_value() || !running.load(); });
                frame = std::move(pending);
                pending.reset();
                // A frame being encoded counts as in flight until it is sent.
                encoding = frame.has_value();
            }
            if (frame) {
                encode_and_send(*frame);
                std::lock_guard lock(mutex);
                encoding = false;
            }
        }
        encoder.reset();
        if (SUCCEEDED(com)) CoUninitialize();
    }
};

StreamController::StreamController(StreamOptions options, net::LogSink log)
    : impl_(std::make_unique<Impl>(options, std::move(log))) {}

StreamController::~StreamController() { close(); }

bool StreamController::connect(std::uint16_t link_port, const std::array<std::uint8_t, 32>& token) {
    if (impl_->running.load()) return false;
    if (!impl_->link.connect(link_port, token)) return false;
    impl_->running = true;
    impl_->worker = std::thread([this] { impl_->run(); });
    return true;
}

void StreamController::close() {
    impl_->running = false;
    impl_->wake.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->link.close();
}

std::vector<TouchEvent> StreamController::take_touches() {
    std::vector<QueuedTouch> queued;
    {
        std::lock_guard lock(impl_->mutex);
        queued.swap(impl_->touches);
    }
    std::vector<TouchEvent> events;
    events.reserve(queued.size());
    for (const auto& item : queued) {
        events.push_back(item.event);
        impl_->delivered_touch_seq = item.seq;
    }
    return events;
}

bool StreamController::wants_frame(std::uint64_t now) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->streaming || impl_->pending || impl_->encoding || !impl_->inflight.can_encode()) return false;
    const std::uint64_t interval = 1000000u / impl_->fps;
    // The engine already paces itself near the target rate; this only stops a
    // much faster game from capturing every frame, so frame jitter is tolerated.
    if (impl_->last_capture_us != 0 && now - impl_->last_capture_us < interval * 3 / 4) return false;
    impl_->last_capture_us = now;
    return true;
}

void StreamController::submit_frame(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height,
                                    std::uint32_t stride) {
    CapturedFrame frame;
    frame.rgba.assign(rgba, rgba + static_cast<std::size_t>(stride) * height);
    frame.width = width;
    frame.height = height;
    frame.stride = stride;
    frame.touch_seq = impl_->delivered_touch_seq;
    frame.pts_us = now_us();
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->streaming) return;
        impl_->pending = std::move(frame);
    }
    impl_->wake.notify_one();
}

std::uint8_t StreamController::target_fps() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->fps;
}

StreamStatus StreamController::status() const {
    StreamStatus copy;
    {
        std::lock_guard lock(impl_->mutex);
        copy = impl_->status;
    }
    copy.link_connected = impl_->link.connected();
    return copy;
}

}  // namespace handview::stream
