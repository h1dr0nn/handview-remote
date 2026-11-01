#include "handview_runtime.h"

#include <handview/stream/stream.hpp>

#include <godot_cpp/variant/utility_functions.hpp>

#include <windows.h>

#include <array>
#include <string>

namespace handview {

namespace {

bool parse_token(const String& hex, std::array<std::uint8_t, 32>& out) {
    const PackedByteArray bytes = hex.hex_decode();
    if (hex.length() != 64 || bytes.size() != 32) return false;
    for (int i = 0; i < 32; ++i) out[static_cast<std::size_t>(i)] = bytes[i];
    return true;
}

}  // namespace

HandviewRuntime::HandviewRuntime() = default;

HandviewRuntime::~HandviewRuntime() { stop(); }

void HandviewRuntime::_bind_methods() {
    ClassDB::bind_method(D_METHOD("start", "link_port", "token_hex"), &HandviewRuntime::start);
    ClassDB::bind_method(D_METHOD("stop"), &HandviewRuntime::stop);
    ClassDB::bind_method(D_METHOD("take_touches"), &HandviewRuntime::take_touches);
    ClassDB::bind_method(D_METHOD("wants_frame"), &HandviewRuntime::wants_frame);
    ClassDB::bind_method(D_METHOD("submit_frame", "rgba", "width", "height"), &HandviewRuntime::submit_frame);
    ClassDB::bind_method(D_METHOD("get_target_fps"), &HandviewRuntime::get_target_fps);
    ClassDB::bind_method(D_METHOD("get_status"), &HandviewRuntime::get_status);
    ClassDB::bind_static_method("HandviewRuntime", D_METHOD("set_tool_window", "native_window_handle"),
                                &HandviewRuntime::set_tool_window);
}

bool HandviewRuntime::start(int64_t link_port, const String& token_hex) {
    stop();
    std::array<std::uint8_t, 32> token{};
    if (link_port <= 0 || link_port > 65535 || !parse_token(token_hex, token)) {
        UtilityFunctions::push_error("Handview: invalid link port or token.");
        return false;
    }
    controller_ = std::make_unique<stream::StreamController>(stream::StreamOptions{}, [](std::string_view line) {
        UtilityFunctions::print_verbose(String("Handview: ") + String::utf8(line.data(), static_cast<int>(line.size())));
    });
    if (!controller_->connect(static_cast<std::uint16_t>(link_port), token)) {
        controller_.reset();
        return false;
    }
    return true;
}

void HandviewRuntime::stop() {
    if (controller_) {
        controller_->close();
        controller_.reset();
    }
}

Array HandviewRuntime::take_touches() {
    Array result;
    if (!controller_) return result;
    for (const auto& touch : controller_->take_touches()) {
        Dictionary event;
        event["id"] = touch.id;
        event["phase"] = static_cast<int64_t>(touch.phase);
        event["x"] = touch.x;
        event["y"] = touch.y;
        event["pressure"] = touch.pressure;
        result.push_back(event);
    }
    return result;
}

bool HandviewRuntime::wants_frame() { return controller_ && controller_->wants_frame(stream::now_us()); }

void HandviewRuntime::submit_frame(const PackedByteArray& rgba, int64_t width, int64_t height) {
    if (!controller_ || width <= 0 || height <= 0) return;
    const int64_t stride = width * 4;
    if (rgba.size() < stride * height) {
        UtilityFunctions::push_error("Handview: frame data is smaller than width x height x 4.");
        return;
    }
    controller_->submit_frame(rgba.ptr(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                              static_cast<std::uint32_t>(stride));
}

int64_t HandviewRuntime::get_target_fps() const { return controller_ ? controller_->target_fps() : 0; }

Dictionary HandviewRuntime::get_status() const {
    Dictionary result;
    const auto status = controller_ ? controller_->status() : stream::StreamStatus{};
    result["link_connected"] = status.link_connected;
    result["streaming"] = status.streaming;
    result["encoder"] = String::utf8(status.encoder.c_str());
    result["width"] = status.width;
    result["height"] = status.height;
    result["fps"] = status.fps;
    result["bitrate_kbps"] = status.bitrate_kbps;
    result["frames_sent"] = status.frames_sent;
    result["last_error"] = String::utf8(status.last_error.c_str());
    result["view_width"] = status.view_width;
    result["view_height"] = status.view_height;
    result["safe_left"] = status.safe_left;
    result["safe_top"] = status.safe_top;
    result["safe_right"] = status.safe_right;
    result["safe_bottom"] = status.safe_bottom;
    return result;
}

bool HandviewRuntime::set_tool_window(int64_t native_window_handle) {
    const auto hwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(native_window_handle));
    if (hwnd == nullptr || !IsWindow(hwnd)) return false;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    style = (style | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
    // The taskbar only re-reads the style when the window is shown again.
    ShowWindow(hwnd, SW_HIDE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return true;
}

}  // namespace handview
