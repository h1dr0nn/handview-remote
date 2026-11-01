#include "handview/protocol/messages.hpp"
#include "test_support.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>

using namespace handview::protocol;

int main() {
    std::ifstream file(HANDVIEW_VECTORS_DIR "/messages.tsv");
    CHECK(file.is_open());
    std::string line;
    std::size_t count = 0;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string name, channel, expected, hex;
        CHECK(static_cast<bool>(std::getline(row, name, '\t')));
        CHECK(static_cast<bool>(std::getline(row, channel, '\t')));
        CHECK(static_cast<bool>(std::getline(row, expected, '\t')));
        CHECK(static_cast<bool>(std::getline(row, hex)));
        CHECK(hex.size() % 2 == 0);
        std::vector<std::uint8_t> bytes;
        const auto digit = [](char c) -> unsigned { return c <= '9' ? static_cast<unsigned>(c - '0') : static_cast<unsigned>(c - 'a' + 10); };
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
            bytes.push_back(static_cast<std::uint8_t>((digit(hex[i]) << 4) | digit(hex[i + 1])));
        }
        const auto ch = channel == "control" ? Channel::Control : channel == "video" ? Channel::Video : Channel::Link;
        const auto result = decode(bytes.data(), bytes.size(), ch);
        const std::array<const char*, 6> names = {"ok", "need_more", "too_large", "unknown_type", "bad_size", "bad_enum"};
        if (expected != names[static_cast<std::size_t>(result.status)]) std::fprintf(stderr, "vector: %s\n", name.c_str());
        CHECK(expected == names[static_cast<std::size_t>(result.status)]);
        if (result.status == DecodeStatus::NeedMore || result.status == DecodeStatus::TooLarge) {
            CHECK(result.consumed == 0);
        } else {
            CHECK(result.consumed == kHeaderSize + read_u32(bytes.data() + 2));
        }
        if (result.status == DecodeStatus::Ok) CHECK(encode(result.message) == bytes);
        if (name == "hello" && result.status == DecodeStatus::Ok) {
            const auto& m = std::get<Hello>(result.message);
            CHECK(m.protoVersion == 1 && m.token[31] == 31);
            CHECK(m.screenW == 1080 && m.screenH == 2400 && m.dpi == 420 && m.refreshHz == 90);
            CHECK(m.model == "Pixel 9" && m.decoders.size() == 2);
            CHECK(m.decoders[0].profile == Profile::Baseline && m.decoders[0].level == 31);
            CHECK(m.decoders[1].profile == Profile::High && m.decoders[1].maxW == 3840 && m.decoders[1].maxH == 2160);
        }
        if (name == "welcome" && result.status == DecodeStatus::Ok) {
            const auto& m = std::get<Welcome>(result.message);
            CHECK(read_u32(bytes.data() + 2) == 30);
            CHECK(m.sessionId[0] == 16 && m.sessionId[15] == 31);
            CHECK(m.host == Host::Godot && m.codec == Codec::H264 && m.encoder == Encoder::MfSoftware);
            CHECK(m.width == 1920 && m.height == 1080 && m.orientation == Orientation::Landscape);
            CHECK(m.fps == 60 && m.bitrateKbps == 12500 && m.editorState == EditorStateValue::Playing);
        }
        if (name == "touch" && result.status == DecodeStatus::Ok) {
            const auto& m = std::get<Touch>(result.message);
            CHECK(m.touchSeq == 0xFEDCBA98u && m.eventTimeUs == 0x0102030405060708ull);
            CHECK(m.pointers.size() == 2);
            CHECK(m.pointers[0].id == 3 && m.pointers[0].phase == Phase::Began);
            CHECK(m.pointers[0].x == 0.125f && m.pointers[0].y == 0.75f && m.pointers[0].pressure == 0.5f);
            CHECK(m.pointers[1].id == 7 && m.pointers[1].phase == Phase::Moved);
            CHECK(m.pointers[1].x == 0.875f && m.pointers[1].pressure == 0.625f);
        }
        if (name == "video_frame" && result.status == DecodeStatus::Ok) {
            const auto& m = std::get<VideoFrame>(result.message);
            CHECK(m.frameSeq == 0xFFFFFFFFu && m.lastTouchSeq == 0xFEDCBA98u);
            CHECK(m.ptsUs == 123456789 && m.flags == (VideoFlags::Keyframe | VideoFlags::CodecConfig));
            CHECK(m.data == (std::vector<std::uint8_t>{0, 0, 0, 1, 0x67, 0x64, 0, 0x1F}));
        }
        if (name == "link_start" && result.status == DecodeStatus::Ok) {
            const auto& m = std::get<LinkStart>(result.message);
            CHECK(m.maxW == 1600 && m.maxH == 900 && m.maxFps == 50 && m.bitrateKbps == 16000);
        }
        if (name == "unknown") {
            const auto* m = std::get_if<Unknown>(&result.message);
            CHECK(m != nullptr);
            if (m) CHECK(m->type == 0x1234 && m->length == 4);
            auto combined = bytes;
            const auto next = encode(Message{RequestKeyframe{}});
            combined.insert(combined.end(), next.begin(), next.end());
            const auto first = decode(combined.data(), combined.size(), ch);
            const auto second = decode(combined.data() + first.consumed, combined.size() - first.consumed, ch);
            CHECK(second.status == DecodeStatus::Ok && std::holds_alternative<RequestKeyframe>(second.message));
        }
        ++count;
    }
    CHECK(count >= 21);
    return test_result("messages");
}
