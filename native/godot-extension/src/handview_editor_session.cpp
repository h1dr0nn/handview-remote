#include "qrcodegen.hpp"
#include "handview_editor_session.h"

#include <handview/encode/encoder.hpp>
#include <handview/session/session.hpp>

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <array>
#include <string>

namespace handview {

namespace {

bool to_token(const PackedByteArray& bytes, std::array<std::uint8_t, 32>& out) {
    if (bytes.size() != 32) {
        UtilityFunctions::push_error("Handview: a token must be 32 bytes.");
        return false;
    }
    std::copy_n(bytes.ptr(), 32, out.begin());
    return true;
}

template <class Enum>
Enum to_enum(int64_t value) {
    return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(value));
}

}  // namespace

HandviewEditorSession::HandviewEditorSession() = default;

HandviewEditorSession::~HandviewEditorSession() { stop(); }

void HandviewEditorSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start", "port", "lan"), &HandviewEditorSession::start, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("set_pending_pin", "pin"), &HandviewEditorSession::set_pending_pin);
    ClassDB::bind_static_method("HandviewEditorSession", D_METHOD("make_qr", "text"), &HandviewEditorSession::make_qr);
    ClassDB::bind_method(D_METHOD("stop"), &HandviewEditorSession::stop);
    ClassDB::bind_method(D_METHOD("is_running"), &HandviewEditorSession::is_running);
    ClassDB::bind_method(D_METHOD("get_port"), &HandviewEditorSession::get_port);
    ClassDB::bind_method(D_METHOD("get_link_port"), &HandviewEditorSession::get_link_port);
    ClassDB::bind_method(D_METHOD("set_pending_token", "token"), &HandviewEditorSession::set_pending_token);
    ClassDB::bind_method(D_METHOD("set_link_token", "token"), &HandviewEditorSession::set_link_token);
    ClassDB::bind_method(D_METHOD("set_stream_info", "encoder", "width", "height", "fps", "bitrate_kbps"),
                         &HandviewEditorSession::set_stream_info);
    ClassDB::bind_method(D_METHOD("send_editor_state", "state"), &HandviewEditorSession::send_editor_state);
    ClassDB::bind_method(D_METHOD("send_command_result", "cmd", "result", "state"),
                         &HandviewEditorSession::send_command_result);
    ClassDB::bind_method(D_METHOD("send_stream_paused", "reason"), &HandviewEditorSession::send_stream_paused);
    ClassDB::bind_method(D_METHOD("poll"), &HandviewEditorSession::poll);
    ClassDB::bind_static_method("HandviewEditorSession", D_METHOD("probe_encoder"),
                                &HandviewEditorSession::probe_encoder);
}

bool HandviewEditorSession::start(int64_t port, bool lan) {
    stop();
    session::ServerConfig config;
    config.log = [](std::string_view line) {
        UtilityFunctions::print_verbose(String("Handview: ") + String::utf8(line.data(), static_cast<int>(line.size())));
    };
    server_ = std::make_unique<session::EditorServer>(std::move(config));
    if (!server_->start(static_cast<std::uint16_t>(port), lan)) {
        server_.reset();
        return false;
    }
    return true;
}

void HandviewEditorSession::stop() {
    if (server_) {
        server_->stop();
        server_.reset();
    }
}

bool HandviewEditorSession::is_running() const { return server_ != nullptr; }

int64_t HandviewEditorSession::get_port() const { return server_ ? server_->port() : 0; }

int64_t HandviewEditorSession::get_link_port() const { return server_ ? server_->link_port() : 0; }

void HandviewEditorSession::set_pending_token(const PackedByteArray& token) {
    std::array<std::uint8_t, 32> value{};
    if (server_ && to_token(token, value)) server_->set_pending_token(value);
}

void HandviewEditorSession::set_pending_pin(const PackedByteArray& pin) {
    if (!server_) return;
    // Empty turns PIN logins off; anything else must be a 32 byte token.
    if (pin.is_empty()) {
        server_->set_pending_pin(std::nullopt);
        return;
    }
    std::array<std::uint8_t, 32> value{};
    if (to_token(pin, value)) server_->set_pending_pin(value);
}

Dictionary HandviewEditorSession::make_qr(const String& text) {
    Dictionary result;
    const auto utf8 = text.utf8();
    std::optional<qrcodegen::QrCode> encoded;
    try {
        encoded.emplace(qrcodegen::QrCode::encodeText(utf8.get_data(), qrcodegen::QrCode::Ecc::MEDIUM));
    } catch (const std::exception&) {
        return result;  // too long for a QR code
    }
    const auto& qr = *encoded;
    const int size = qr.getSize();
    PackedByteArray data;
    data.resize(static_cast<int64_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) data[static_cast<int64_t>(y) * size + x] = qr.getModule(x, y) ? 1 : 0;
    result["size"] = size;
    result["data"] = data;
    return result;
}

void HandviewEditorSession::set_link_token(const PackedByteArray& token) {
    std::array<std::uint8_t, 32> value{};
    if (server_ && to_token(token, value)) server_->set_link_token(value);
}

void HandviewEditorSession::set_stream_info(int64_t encoder, int64_t width, int64_t height, int64_t fps,
                                            int64_t bitrate_kbps) {
    if (!server_) return;
    const auto orientation = width > height ? protocol::Orientation::Landscape : protocol::Orientation::Portrait;
    server_->set_stream_info(to_enum<protocol::Encoder>(encoder), static_cast<std::uint16_t>(width),
                             static_cast<std::uint16_t>(height), orientation, static_cast<std::uint8_t>(fps),
                             static_cast<std::uint32_t>(bitrate_kbps));
}

void HandviewEditorSession::send_editor_state(int64_t state) {
    if (server_) server_->editor_state(to_enum<protocol::EditorStateValue>(state));
}

void HandviewEditorSession::send_command_result(int64_t cmd, int64_t result, int64_t state) {
    if (server_) {
        server_->command_result(to_enum<protocol::Cmd>(cmd), to_enum<protocol::Result>(result),
                                to_enum<protocol::EditorStateValue>(state));
    }
}

void HandviewEditorSession::send_stream_paused(int64_t reason) {
    if (server_) server_->stream_paused(to_enum<protocol::PauseReason>(reason));
}

Array HandviewEditorSession::poll() {
    Array events;
    if (!server_) return events;
    // Session events first: a Command can arrive right after Welcome, and the
    // plugin must see "connected" before acting on it.
    while (auto item = server_->poll_event()) {
        Dictionary event;
        switch (item->kind) {
            case session::SessionEvent::Kind::connected: {
                event["type"] = "connected";
                event["model"] = String::utf8(item->hello.model.c_str());
                event["screen_w"] = item->hello.screenW;
                event["screen_h"] = item->hello.screenH;
                event["dpi"] = item->hello.dpi;
                event["refresh_hz"] = item->hello.refreshHz;
                Array decoders;
                for (const auto& decoder : item->hello.decoders) {
                    Dictionary d;
                    d["profile"] = static_cast<int64_t>(decoder.profile);
                    d["level"] = decoder.level;
                    d["max_w"] = decoder.maxW;
                    d["max_h"] = decoder.maxH;
                    decoders.push_back(d);
                }
                event["decoders"] = decoders;
                break;
            }
            case session::SessionEvent::Kind::disconnected:
                event["type"] = "disconnected";
                event["reason"] = static_cast<int64_t>(item->reason);
                event["timed_out"] = item->timed_out;
                break;
            case session::SessionEvent::Kind::game_disconnected:
                event["type"] = "game_disconnected";
                break;
            case session::SessionEvent::Kind::pin_locked:
                event["type"] = "pin_locked";
                break;
            case session::SessionEvent::Kind::phone_error:
                event["type"] = "phone_error";
                event["source"] = static_cast<int64_t>(item->source);
                event["code"] = static_cast<int64_t>(item->code);
                break;
        }
        events.push_back(event);
    }
    while (auto command = server_->poll_command()) {
        Dictionary event;
        event["type"] = "command";
        event["cmd"] = static_cast<int64_t>(command->cmd);
        events.push_back(event);
    }
    return events;
}

int64_t HandviewEditorSession::probe_encoder() {
    const auto backend = encode::probe_backend();
    return backend ? static_cast<int64_t>(*backend) : -1;
}

}  // namespace handview
