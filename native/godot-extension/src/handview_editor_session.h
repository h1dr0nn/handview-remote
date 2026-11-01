#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <memory>

namespace handview {

namespace session {
class EditorServer;
}

using namespace godot;

// Editor side of a phone session: wraps the native EditorServer, which runs
// the sockets and the game relay on its own threads. The editor plugin polls
// events from its main loop.
class HandviewEditorSession : public RefCounted {
    GDCLASS(HandviewEditorSession, RefCounted)

protected:
    static void _bind_methods();

public:
    HandviewEditorSession();
    ~HandviewEditorSession() override;

    bool start(int64_t port);
    void stop();
    bool is_running() const;
    int64_t get_port() const;
    int64_t get_link_port() const;

    void set_pending_token(const PackedByteArray& token);
    void set_link_token(const PackedByteArray& token);
    void set_stream_info(int64_t encoder, int64_t width, int64_t height, int64_t fps, int64_t bitrate_kbps);

    void send_editor_state(int64_t state);
    void send_command_result(int64_t cmd, int64_t result, int64_t state);
    void send_stream_paused(int64_t reason);

    // Array of Dictionary: {type: "command", cmd}, {type: "connected", model,
    // screen_w, screen_h, dpi, refresh_hz, decoders}, {type: "disconnected",
    // reason, timed_out}, {type: "game_disconnected"}.
    Array poll();

    // -1 when no encoder is available, else the wire encoder value.
    static int64_t probe_encoder();

private:
    std::unique_ptr<session::EditorServer> server_;
};

}  // namespace handview
