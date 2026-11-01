#pragma once

#include "handview/protocol/wire.hpp"
#include <array>
#include <string>
#include <variant>
#include <vector>

namespace handview::protocol {
enum class Host : std::uint8_t { Unity = 0, Godot = 1 };
constexpr bool valid(Host value) { return value == Host::Unity || value == Host::Godot; }
enum class Codec : std::uint8_t { H264 = 1 };
constexpr bool valid(Codec value) { return value == Codec::H264; }
enum class Profile : std::uint8_t { Baseline = 66, Main = 77, High = 100 };
constexpr bool valid(Profile value) { return value == Profile::Baseline || value == Profile::Main || value == Profile::High; }
enum class Encoder : std::uint8_t { Nvenc = 0, MfHardware = 1, MfSoftware = 2 };
constexpr bool valid(Encoder value) { return value == Encoder::Nvenc || value == Encoder::MfHardware || value == Encoder::MfSoftware; }
enum class Orientation : std::uint8_t { Portrait = 0, Landscape = 1 };
constexpr bool valid(Orientation value) { return value == Orientation::Portrait || value == Orientation::Landscape; }
enum class EditorStateValue : std::uint8_t { Stopped = 0, Playing = 1, Paused = 2, Compiling = 3, Reloading = 4 };
constexpr bool valid(EditorStateValue value) { return value == EditorStateValue::Stopped || value == EditorStateValue::Playing || value == EditorStateValue::Paused || value == EditorStateValue::Compiling || value == EditorStateValue::Reloading; }
enum class Cmd : std::uint8_t { Play = 1, Pause = 2, Step = 3, Stop = 4 };
constexpr bool valid(Cmd value) { return value == Cmd::Play || value == Cmd::Pause || value == Cmd::Step || value == Cmd::Stop; }
enum class Result : std::uint8_t { Ok = 0, Ignored = 1, Busy = 2 };
constexpr bool valid(Result value) { return value == Result::Ok || value == Result::Ignored || value == Result::Busy; }
enum class Phase : std::uint8_t { Began = 0, Moved = 1, Stationary = 2, Ended = 3, Canceled = 4 };
constexpr bool valid(Phase value) { return value == Phase::Began || value == Phase::Moved || value == Phase::Stationary || value == Phase::Ended || value == Phase::Canceled; }
enum class SensorKind : std::uint8_t { Accelerometer = 1, Gyroscope = 2, Gravity = 3, Magnetometer = 4 };
constexpr bool valid(SensorKind value) { return value >= SensorKind::Accelerometer && value <= SensorKind::Magnetometer; }
enum class RejectReason : std::uint8_t { BadVersion = 0, BadToken = 1, Busy = 2 };
constexpr bool valid(RejectReason value) { return value == RejectReason::BadVersion || value == RejectReason::BadToken || value == RejectReason::Busy; }
enum class PauseReason : std::uint8_t { NotPlaying = 0, Paused = 1 };
constexpr bool valid(PauseReason value) { return value == PauseReason::NotPlaying || value == PauseReason::Paused; }
enum class ErrorSource : std::uint8_t { Encoder = 0, Decoder = 1 };
constexpr bool valid(ErrorSource value) { return value == ErrorSource::Encoder || value == ErrorSource::Decoder; }
enum class ErrorCode : std::uint16_t { InitFailed = 1, ProcessFailed = 2, DeviceLost = 3, Unsupported = 4 };
constexpr bool valid(ErrorCode value) { return value == ErrorCode::InitFailed || value == ErrorCode::ProcessFailed || value == ErrorCode::DeviceLost || value == ErrorCode::Unsupported; }
enum class ByeReason : std::uint8_t { Normal = 0, ProtocolError = 1, Compiling = 2, Replaced = 3, Reloading = 4 };
constexpr bool valid(ByeReason value) { return value == ByeReason::Normal || value == ByeReason::ProtocolError || value == ByeReason::Compiling || value == ByeReason::Replaced || value == ByeReason::Reloading; }
constexpr bool valid_level(std::uint8_t value) { return value >= 9 && value <= 62; }
enum class VideoFlags : std::uint8_t { None = 0, Keyframe = 1, CodecConfig = 2 };
constexpr VideoFlags operator|(VideoFlags a, VideoFlags b) {
    return static_cast<VideoFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr bool valid(VideoFlags value) { return (static_cast<std::uint8_t>(value) & 0xFCu) == 0; }

struct Decoder {
    Codec codec{};
    Profile profile{};
    std::uint8_t level{};
    std::uint16_t maxW{};
    std::uint16_t maxH{};
};
struct Pointer {
    std::uint8_t id{};
    Phase phase{};
    float x{};
    float y{};
    float pressure{};
};
struct Hello {
    std::uint16_t protoVersion{};
    std::array<std::uint8_t, 32> token{};
    std::uint16_t screenW{};
    std::uint16_t screenH{};
    std::uint16_t dpi{};
    std::uint8_t refreshHz{};
    std::vector<Decoder> decoders{};
    std::string model{};
};
struct Welcome {
    std::array<std::uint8_t, 16> sessionId{};
    Host host{};
    Codec codec{};
    Encoder encoder{};
    std::uint16_t width{};
    std::uint16_t height{};
    Orientation orientation{};
    std::uint8_t fps{};
    std::uint32_t bitrateKbps{};
    EditorStateValue editorState{};
};
struct Reject {
    RejectReason reason{};
};
struct VideoAttach {
    std::array<std::uint8_t, 16> sessionId{};
};
struct Command {
    Cmd cmd{};
};
struct CommandResult {
    Cmd cmd{};
    Result result{};
    EditorStateValue editorState{};
};
struct EditorState {
    EditorStateValue state{};
};
struct Touch {
    std::uint32_t touchSeq{};
    std::uint64_t eventTimeUs{};
    std::vector<Pointer> pointers{};
};
// A phone sensor reading, already in Godot's convention (SPEC 3).
struct Sensor {
    SensorKind kind{SensorKind::Accelerometer};
    float x{};
    float y{};
    float z{};
};
// A key from the phone's keyboard: Godot Key code, Unicode character, state.
struct Key {
    std::uint32_t keycode{};
    std::uint32_t unicode{};
    std::uint8_t pressed{};
};
struct RequestKeyframe {
};
struct DecoderStats {
    std::uint16_t pendingOutputs{};
    std::uint16_t droppedDisplays{};
};
struct StreamConfigChanged {
    std::uint16_t width{};
    std::uint16_t height{};
    Orientation orientation{};
    std::uint8_t fps{};
    std::uint32_t bitrateKbps{};
};
struct StreamPaused {
    PauseReason reason{};
};
struct Error {
    ErrorSource source{};
    ErrorCode code{};
};
struct FrameAck {
    std::uint32_t frameSeq{};
};
struct ViewSize {
    std::uint16_t width{};
    std::uint16_t height{};
    // Unsafe edges of the area (cutout, system bars), in its pixels.
    std::uint16_t safeLeft{};
    std::uint16_t safeTop{};
    std::uint16_t safeRight{};
    std::uint16_t safeBottom{};
};
// The user's video choices; 0 means automatic.
struct StreamPrefs {
    std::uint16_t maxLongSide{};
    std::uint16_t maxShortSide{};
    std::uint8_t maxFps{};
    std::uint32_t bitrateKbps{};
};
struct Ping {
    std::uint64_t timeUs{};
};
struct Pong {
    std::uint64_t timeUs{};
};
struct Bye {
    ByeReason reason{};
};
struct VideoFrame {
    std::uint32_t frameSeq{};
    std::uint32_t lastTouchSeq{};
    std::uint64_t ptsUs{};
    VideoFlags flags{};
    std::vector<std::uint8_t> data{};
};
struct LinkHello {
    std::array<std::uint8_t, 32> linkToken{};
    std::uint32_t processId{};
};
struct LinkStart {
    std::uint16_t maxW{};
    std::uint16_t maxH{};
    std::uint8_t maxFps{};
    std::uint32_t bitrateKbps{};
};
struct LinkStop {
};
struct Unknown { std::uint16_t type = 0; std::uint32_t length = 0; };
using Message = std::variant<Hello, Welcome, Reject, VideoAttach, Command, CommandResult, EditorState, Touch, Sensor, Key, RequestKeyframe, DecoderStats, StreamConfigChanged, StreamPaused, Error, FrameAck, ViewSize, StreamPrefs, Ping, Pong, Bye, VideoFrame, LinkHello, LinkStart, LinkStop, Unknown>;
enum class DecodeStatus { Ok, NeedMore, TooLarge, UnknownType, BadSize, BadEnum };
// Incomplete or oversized input consumes zero bytes; complete messages consume their full frame.
struct DecodeResult { DecodeStatus status; std::size_t consumed = 0; Message message{}; };
// Encode a valid, representable model. Unknown payload bytes are zero-filled.
std::vector<std::uint8_t> encode(const Message& message);
DecodeResult decode(const std::uint8_t* data, std::size_t size, Channel channel);
}  // namespace handview::protocol
