#include "handview/protocol/messages.hpp"
#include <algorithm>
#include <bit>
#include <type_traits>
#include <utility>

namespace handview::protocol {
namespace {
struct Writer {
    std::vector<std::uint8_t> data;
    void u8(std::uint8_t v) { data.push_back(v); }
    void u16(std::uint16_t v) { const auto n = data.size(); data.resize(n + 2); write_u16(data.data() + n, v); }
    void u32(std::uint32_t v) { const auto n = data.size(); data.resize(n + 4); write_u32(data.data() + n, v); }
    void u64(std::uint64_t v) { const auto n = data.size(); data.resize(n + 8); write_u64(data.data() + n, v); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    template<class T> void bytes(const T& v) { data.insert(data.end(), v.begin(), v.end()); }
};
struct Reader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
    bool good = true;
    bool enums = true;
    bool take(std::size_t n) { if (n > size - pos) { good = false; return false; } return true; }
    std::uint8_t u8() { if (!take(1)) return 0; return data[pos++]; }
    std::uint16_t u16() { if (!take(2)) return 0; const auto v = read_u16(data + pos); pos += 2; return v; }
    std::uint32_t u32() { if (!take(4)) return 0; const auto v = read_u32(data + pos); pos += 4; return v; }
    std::uint64_t u64() { if (!take(8)) return 0; const auto v = read_u64(data + pos); pos += 8; return v; }
    float f32() { return std::bit_cast<float>(u32()); }
    template<class T> T enumeration(bool wide = false) {
        const auto v = static_cast<T>(wide ? u16() : u8());
        enums = valid(v) && enums;
        return v;
    }
    template<std::size_t N> void bytes(std::array<std::uint8_t, N>& v) { if (take(N)) { std::copy_n(data + pos, N, v.begin()); pos += N; } }
};
}  // namespace
std::vector<std::uint8_t> encode(const Message& message) {
    Writer w;
    std::uint16_t type = 0;
    std::visit([&](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, Hello>) {
            type = 1;
            w.u16(m.protoVersion);
            w.bytes(m.token);
            w.u16(m.screenW);
            w.u16(m.screenH);
            w.u16(m.dpi);
            w.u8(m.refreshHz);
            w.u8(static_cast<std::uint8_t>(m.decoders.size()));
            w.u16(static_cast<std::uint16_t>(m.model.size()));
            for (const auto& d : m.decoders) {
                w.u8(static_cast<std::uint8_t>(d.codec));
                w.u8(static_cast<std::uint8_t>(d.profile));
                w.u8(d.level);
                w.u16(d.maxW);
                w.u16(d.maxH);
            }
            w.bytes(m.model);
        }
        if constexpr (std::is_same_v<T, Welcome>) {
            type = 2;
            w.bytes(m.sessionId);
            w.u8(static_cast<std::uint8_t>(m.host));
            w.u8(static_cast<std::uint8_t>(m.codec));
            w.u8(static_cast<std::uint8_t>(m.encoder));
            w.u16(m.width);
            w.u16(m.height);
            w.u8(static_cast<std::uint8_t>(m.orientation));
            w.u8(m.fps);
            w.u32(m.bitrateKbps);
            w.u8(static_cast<std::uint8_t>(m.editorState));
        }
        if constexpr (std::is_same_v<T, Reject>) {
            type = 3;
            w.u8(static_cast<std::uint8_t>(m.reason));
        }
        if constexpr (std::is_same_v<T, VideoAttach>) {
            type = 4;
            w.bytes(m.sessionId);
        }
        if constexpr (std::is_same_v<T, Command>) {
            type = 16;
            w.u8(static_cast<std::uint8_t>(m.cmd));
        }
        if constexpr (std::is_same_v<T, CommandResult>) {
            type = 17;
            w.u8(static_cast<std::uint8_t>(m.cmd));
            w.u8(static_cast<std::uint8_t>(m.result));
            w.u8(static_cast<std::uint8_t>(m.editorState));
        }
        if constexpr (std::is_same_v<T, EditorState>) {
            type = 18;
            w.u8(static_cast<std::uint8_t>(m.state));
        }
        if constexpr (std::is_same_v<T, Touch>) {
            type = 32;
            w.u32(m.touchSeq); w.u64(m.eventTimeUs);
            w.u8(static_cast<std::uint8_t>(m.pointers.size()));
            for (const auto& d : m.pointers) {
                w.u8(d.id);
                w.u8(static_cast<std::uint8_t>(d.phase));
                w.f32(d.x);
                w.f32(d.y);
                w.f32(d.pressure);
            }
        }
        if constexpr (std::is_same_v<T, RequestKeyframe>) {
            type = 48;
        }
        if constexpr (std::is_same_v<T, DecoderStats>) {
            type = 49;
            w.u16(m.pendingOutputs);
            w.u16(m.droppedDisplays);
        }
        if constexpr (std::is_same_v<T, StreamConfigChanged>) {
            type = 50;
            w.u16(m.width);
            w.u16(m.height);
            w.u8(static_cast<std::uint8_t>(m.orientation));
            w.u8(m.fps);
            w.u32(m.bitrateKbps);
        }
        if constexpr (std::is_same_v<T, StreamPaused>) {
            type = 51;
            w.u8(static_cast<std::uint8_t>(m.reason));
        }
        if constexpr (std::is_same_v<T, Error>) {
            type = 52;
            w.u8(static_cast<std::uint8_t>(m.source));
            w.u16(static_cast<std::uint16_t>(m.code));
        }
        if constexpr (std::is_same_v<T, FrameAck>) {
            type = 53;
            w.u32(m.frameSeq);
        }
        if constexpr (std::is_same_v<T, ViewSize>) {
            type = 54;
            w.u16(m.width);
            w.u16(m.height);
            w.u16(m.safeLeft);
            w.u16(m.safeTop);
            w.u16(m.safeRight);
            w.u16(m.safeBottom);
        }
        if constexpr (std::is_same_v<T, Ping>) {
            type = 64;
            w.u64(m.timeUs);
        }
        if constexpr (std::is_same_v<T, Pong>) {
            type = 65;
            w.u64(m.timeUs);
        }
        if constexpr (std::is_same_v<T, Bye>) {
            type = 240;
            w.u8(static_cast<std::uint8_t>(m.reason));
        }
        if constexpr (std::is_same_v<T, VideoFrame>) {
            type = 256;
            w.u32(m.frameSeq);
            w.u32(m.lastTouchSeq);
            w.u64(m.ptsUs);
            w.u8(static_cast<std::uint8_t>(m.flags));
            w.bytes(m.data);
        }
        if constexpr (std::is_same_v<T, LinkHello>) {
            type = 512;
            w.bytes(m.linkToken);
            w.u32(m.processId);
        }
        if constexpr (std::is_same_v<T, LinkStart>) {
            type = 513;
            w.u16(m.maxW);
            w.u16(m.maxH);
            w.u8(m.maxFps);
            w.u32(m.bitrateKbps);
        }
        if constexpr (std::is_same_v<T, LinkStop>) {
            type = 514;
        }
        if constexpr (std::is_same_v<T, Unknown>) { type = m.type; w.data.resize(m.length); }
    }, message);
    std::vector<std::uint8_t> out(kHeaderSize + w.data.size());
    encode_header({type, static_cast<std::uint32_t>(w.data.size())}, out.data());
    std::copy(w.data.begin(), w.data.end(), out.begin() + kHeaderSize);
    return out;
}
DecodeResult decode(const std::uint8_t* data, std::size_t size, Channel channel) {
    Header header;
    const auto status = decode_header(data, size, channel, header);
    if (status == HeaderStatus::NeedMore) return {DecodeStatus::NeedMore};
    if (status == HeaderStatus::TooLarge) return {DecodeStatus::TooLarge};
    const auto total = kHeaderSize + static_cast<std::size_t>(header.length);
    if (size < total) return {DecodeStatus::NeedMore};
    Reader r{data + kHeaderSize, header.length};
    Message message;
    switch (header.type) {
    case 1: {
        Hello m;
        m.protoVersion = r.u16();
        r.bytes(m.token);
        m.screenW = r.u16();
        m.screenH = r.u16();
        m.dpi = r.u16();
        m.refreshHz = r.u8();
        const auto count = r.u8(); const auto modelLen = r.u16();
        if (r.size != 44u + 7u * count + modelLen) return {DecodeStatus::BadSize, total};
        for (unsigned i = 0; i < count; ++i) {
            Decoder d;
            d.codec = r.enumeration<Codec>();
            d.profile = r.enumeration<Profile>();
            d.level = r.u8();
            d.maxW = r.u16();
            d.maxH = r.u16();
            r.enums = valid_level(d.level) && r.enums;
            m.decoders.push_back(d);
        }
        if (r.take(modelLen)) { m.model.assign(reinterpret_cast<const char*>(r.data + r.pos), modelLen); r.pos += modelLen; }
        message = std::move(m); break;
    }
    case 2: {
        Welcome m;
        r.bytes(m.sessionId);
        m.host = r.enumeration<Host>();
        m.codec = r.enumeration<Codec>();
        m.encoder = r.enumeration<Encoder>();
        m.width = r.u16();
        m.height = r.u16();
        m.orientation = r.enumeration<Orientation>();
        m.fps = r.u8();
        m.bitrateKbps = r.u32();
        m.editorState = r.enumeration<EditorStateValue>();
        message = std::move(m); break;
    }
    case 3: {
        Reject m;
        m.reason = r.enumeration<RejectReason>();
        message = std::move(m); break;
    }
    case 4: {
        VideoAttach m;
        r.bytes(m.sessionId);
        message = std::move(m); break;
    }
    case 16: {
        Command m;
        m.cmd = r.enumeration<Cmd>();
        message = std::move(m); break;
    }
    case 17: {
        CommandResult m;
        m.cmd = r.enumeration<Cmd>();
        m.result = r.enumeration<Result>();
        m.editorState = r.enumeration<EditorStateValue>();
        message = std::move(m); break;
    }
    case 18: {
        EditorState m;
        m.state = r.enumeration<EditorStateValue>();
        message = std::move(m); break;
    }
    case 32: {
        Touch m;
        m.touchSeq = r.u32(); m.eventTimeUs = r.u64();
        const auto count = r.u8();
        if (r.size != 13u + 14u * count) return {DecodeStatus::BadSize, total};
        r.enums = count >= 1 && count <= 10;
        for (unsigned i = 0; i < count; ++i) {
            Pointer d;
            d.id = r.u8();
            d.phase = r.enumeration<Phase>();
            d.x = r.f32();
            d.y = r.f32();
            d.pressure = r.f32();
            m.pointers.push_back(d);
        }
        message = std::move(m); break;
    }
    case 48: {
        RequestKeyframe m;
        message = std::move(m); break;
    }
    case 49: {
        DecoderStats m;
        m.pendingOutputs = r.u16();
        m.droppedDisplays = r.u16();
        message = std::move(m); break;
    }
    case 50: {
        StreamConfigChanged m;
        m.width = r.u16();
        m.height = r.u16();
        m.orientation = r.enumeration<Orientation>();
        m.fps = r.u8();
        m.bitrateKbps = r.u32();
        message = std::move(m); break;
    }
    case 51: {
        StreamPaused m;
        m.reason = r.enumeration<PauseReason>();
        message = std::move(m); break;
    }
    case 52: {
        Error m;
        m.source = r.enumeration<ErrorSource>();
        m.code = r.enumeration<ErrorCode>(true);
        message = std::move(m); break;
    }
    case 53: {
        FrameAck m;
        m.frameSeq = r.u32();
        message = std::move(m); break;
    }
    case 54: {
        ViewSize m;
        m.width = r.u16();
        m.height = r.u16();
        m.safeLeft = r.u16();
        m.safeTop = r.u16();
        m.safeRight = r.u16();
        m.safeBottom = r.u16();
        message = std::move(m); break;
    }
    case 64: {
        Ping m;
        m.timeUs = r.u64();
        message = std::move(m); break;
    }
    case 65: {
        Pong m;
        m.timeUs = r.u64();
        message = std::move(m); break;
    }
    case 240: {
        Bye m;
        m.reason = r.enumeration<ByeReason>();
        message = std::move(m); break;
    }
    case 256: {
        VideoFrame m;
        m.frameSeq = r.u32();
        m.lastTouchSeq = r.u32();
        m.ptsUs = r.u64();
        m.flags = r.enumeration<VideoFlags>();
        m.data.assign(r.data + r.pos, r.data + r.size); r.pos = r.size;
        message = std::move(m); break;
    }
    case 512: {
        LinkHello m;
        r.bytes(m.linkToken);
        m.processId = r.u32();
        message = std::move(m); break;
    }
    case 513: {
        LinkStart m;
        m.maxW = r.u16();
        m.maxH = r.u16();
        m.maxFps = r.u8();
        m.bitrateKbps = r.u32();
        message = std::move(m); break;
    }
    case 514: {
        LinkStop m;
        message = std::move(m); break;
    }
    default: return {DecodeStatus::UnknownType, total, Unknown{header.type, header.length}};
    }
    if (!r.good || r.pos != r.size) return {DecodeStatus::BadSize, total};
    if (!r.enums) return {DecodeStatus::BadEnum, total};
    return {DecodeStatus::Ok, total, std::move(message)};
}
}  // namespace handview::protocol
