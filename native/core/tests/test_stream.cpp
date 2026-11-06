// StreamController against a fake editor link (raw loopback socket + codec)
// and a real encoder. Skips when no encoder backend exists on the machine.

#include "handview/encode/encoder.hpp"
#include "handview/net/net.hpp"
#include "handview/stream/stream.hpp"
#include "test_support.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace handview;
using namespace handview::protocol;
using namespace std::chrono_literals;

struct FakeEditor {
    net::WinsockInit winsock;
    net::Socket listener = net::listen_loopback(0);
    net::Socket peer;
    std::optional<net::FrameWriter> writer;
    net::FrameReader reader{Channel::Link};
    std::vector<Message> inbox;

    bool accept() {
        peer = net::accept(listener, std::chrono::milliseconds(2000));
        if (!peer) return false;
        writer.emplace(peer);
        return true;
    }

    // Reads until `match` returns true for a received message or the timeout passes.
    std::optional<Message> wait_for(const std::function<bool(const Message&)>& match,
                                    std::chrono::milliseconds timeout = 2000ms) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            for (std::size_t i = 0; i < inbox.size(); ++i) {
                if (match(inbox[i])) {
                    Message found = inbox[i];
                    inbox.erase(inbox.begin() + static_cast<std::ptrdiff_t>(i));
                    return found;
                }
            }
            std::uint8_t buffer[65536];
            const int n = net::recv_some(peer, buffer, std::chrono::milliseconds(20));
            if (n < 0) return std::nullopt;
            if (n > 0) {
                for (auto& frame : reader.feed({buffer, static_cast<std::size_t>(n)})) inbox.push_back(frame.message);
            }
        }
        return std::nullopt;
    }

    std::optional<VideoFrame> next_video(std::chrono::milliseconds timeout = 2000ms) {
        auto m = wait_for([](const Message& x) { return std::holds_alternative<VideoFrame>(x); }, timeout);
        if (!m) return std::nullopt;
        return std::get<VideoFrame>(*m);
    }
};

bool has_flag(const VideoFrame& frame, VideoFlags flag) {
    return (static_cast<std::uint8_t>(frame.flags) & static_cast<std::uint8_t>(flag)) != 0;
}

// Runs capture attempts like an engine frame loop for `duration`.
void pump(stream::StreamController& controller, std::uint32_t touch_rounds, std::chrono::milliseconds duration,
          std::vector<stream::TouchEvent>* touches = nullptr) {
    const std::uint32_t w = 640, h = 360;
    static std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
    static std::uint8_t shade = 0;
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        for (std::uint32_t i = 0; i < touch_rounds; ++i) {
            auto events = controller.take_touches();
            if (touches) touches->insert(touches->end(), events.begin(), events.end());
        }
        if (controller.wants_frame(stream::now_us())) {
            ++shade;
            for (std::size_t i = 0; i < rgba.size(); i += 4) {
                rgba[i] = shade;
                rgba[i + 1] = static_cast<std::uint8_t>(i / 4 % w);
                rgba[i + 2] = 64;
                rgba[i + 3] = 255;
            }
            controller.submit_frame(rgba.data(), w, h, w * 4);
        }
        std::this_thread::sleep_for(4ms);
    }
}

void test_stream_pipeline() {
    FakeEditor editor;
    CHECK(static_cast<bool>(editor.listener));
    stream::StreamOptions options;
    options.keyframe.min_interval_us = 0;
    stream::StreamController controller(options);
    std::array<std::uint8_t, 32> token{};
    token[0] = 0xAB;

    CHECK(controller.connect(editor.listener.port(), token));
    CHECK(editor.accept());
    auto hello = editor.wait_for([](const Message& m) { return std::holds_alternative<LinkHello>(m); });
    CHECK(hello && std::get<LinkHello>(*hello).linkToken == token);

    // Nothing is captured before LinkStart.
    CHECK(!controller.wants_frame(stream::now_us()));

    editor.writer->send(LinkStart{1280, 720, 60, 4000});
    for (int i = 0; i < 100 && !controller.status().streaming; ++i) std::this_thread::sleep_for(10ms);
    CHECK(controller.status().streaming);

    // Without acks only two frames go out (in-flight limit 1, then blocked).
    pump(controller, 1, 400ms);
    auto config = editor.wait_for([](const Message& m) { return std::holds_alternative<StreamConfigChanged>(m); });
    CHECK(config.has_value());
    if (config) {
        const auto& c = std::get<StreamConfigChanged>(*config);
        CHECK(c.width == 640 && c.height == 360);
        CHECK(c.orientation == Orientation::Landscape);
        CHECK(c.bitrateKbps == 4000);
    }
    auto first = editor.next_video();
    auto second = editor.next_video();
    CHECK(first && first->frameSeq == 1 && has_flag(*first, VideoFlags::Keyframe) && has_flag(*first, VideoFlags::CodecConfig));
    CHECK(second && second->frameSeq == 2 && !has_flag(*second, VideoFlags::Keyframe));
    CHECK(!editor.next_video(150ms).has_value());
    CHECK(controller.status().encoder.size() > 0);

    // Acks reopen the pipe; touches are delivered and stamped on later frames.
    // The touch is taken by the game before the pipe reopens, so frame 3 must carry it.
    editor.writer->send(Touch{7, 123, {Pointer{1, Phase::Began, 0.25f, 0.75f, 1.0f}}});
    std::vector<stream::TouchEvent> touches;
    for (int i = 0; i < 50 && touches.empty(); ++i) pump(controller, 1, 10ms, &touches);
    editor.writer->send(FrameAck{2});
    pump(controller, 1, 200ms, &touches);
    CHECK(touches.size() == 1);
    if (!touches.empty()) {
        CHECK(touches[0].id == 1 && touches[0].phase == Phase::Began);
        CHECK(touches[0].x == 0.25f && touches[0].y == 0.75f);
    }
    auto third = editor.next_video();
    CHECK(third && third->frameSeq == 3 && third->lastTouchSeq == 7);

    // A resyncing app drops frames without acking them, which closes the pipe.
    // RequestKeyframe alone reopens it and an IDR follows.
    pump(controller, 3, 50ms);
    while (editor.next_video(100ms)) {}
    pump(controller, 3, 50ms);
    CHECK(!editor.next_video(150ms).has_value());
    editor.writer->send(RequestKeyframe{});
    std::optional<VideoFrame> idr;
    for (int round = 0; round < 10 && !idr; ++round) {
        pump(controller, 1, 50ms);
        while (auto frame = editor.next_video(50ms)) {
            editor.writer->send(FrameAck{frame->frameSeq});
            if (has_flag(*frame, VideoFlags::Keyframe)) {
                idr = frame;
                break;
            }
        }
    }
    CHECK(idr.has_value());

    // LinkStop ends the stream and releases the held pointer.
    editor.writer->send(LinkStop{});
    for (int i = 0; i < 100 && controller.status().streaming; ++i) std::this_thread::sleep_for(10ms);
    CHECK(!controller.status().streaming);
    touches.clear();
    pump(controller, 1, 50ms, &touches);
    CHECK(touches.size() == 1 && touches[0].id == 1 && touches[0].phase == Phase::Canceled);
    CHECK(!controller.wants_frame(stream::now_us()));

    // Closing the editor side is noticed.
    editor.peer.close();
    for (int i = 0; i < 100 && controller.status().link_connected; ++i) std::this_thread::sleep_for(10ms);
    CHECK(!controller.status().link_connected);
    controller.close();
}

void test_now_us_monotonic() {
    const auto a = stream::now_us();
    std::this_thread::sleep_for(5ms);
    const auto b = stream::now_us();
    CHECK(b > a && b - a >= 4000 && b - a < 1000000);
}

// probe_backend() only lists backends; a CI runner can list Media Foundation and still fail to open
// or run it, so skip unless a real encoder opens and encodes a frame of the size pump() sends.
bool encoder_works(std::string& error) {
    encode::EncoderConfig config{640, 360, 640, 360, 60, 4000};
    auto encoder = encode::create_encoder(config, std::nullopt, error);
    if (!encoder) return false;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(640) * 360 * 4, 128);
    encode::EncodedFrame out;
    return encoder->encode(rgba.data(), 640 * 4, 0, true, out, error) && !out.data.empty();
}

}  // namespace

int main() {
    test_now_us_monotonic();
    std::string error;
    if (!encode::probe_backend() || !encoder_works(error)) {
        std::printf("SKIP stream pipeline: no working encoder backend (%s)\n", error.c_str());
        return test_result("stream");
    }
    test_stream_pipeline();
    return test_result("stream");
}
