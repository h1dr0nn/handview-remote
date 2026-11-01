#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <memory>

namespace handview {

namespace stream {
class StreamController;
}

using namespace godot;

// Game side of Handview: wraps the native StreamController (link, encoder
// thread, flow control). The runtime autoload drives it every frame.
class HandviewRuntime : public RefCounted {
    GDCLASS(HandviewRuntime, RefCounted)

protected:
    static void _bind_methods();

public:
    HandviewRuntime();
    ~HandviewRuntime() override;

    bool start(int64_t link_port, const String& token_hex);
    void stop();

    // Array of Dictionary {id, phase, x, y, pressure}; x and y are 0..1.
    Array take_touches();
    bool wants_frame();
    void submit_frame(const PackedByteArray& rgba, int64_t width, int64_t height);
    int64_t get_target_fps() const;
    Dictionary get_status() const;

    // Gives the window WS_EX_TOOLWINDOW so it has no taskbar button.
    static bool set_tool_window(int64_t native_window_handle);

private:
    std::unique_ptr<stream::StreamController> controller_;
};

}  // namespace handview
