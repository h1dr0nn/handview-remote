#pragma once

#include <godot_cpp/classes/ref_counted.hpp>

namespace handview {

using namespace godot;

// Entry point the runtime autoload talks to. M0 only exposes the protocol
// version so the addon can verify the extension loaded.
class HandviewNative : public RefCounted {
    GDCLASS(HandviewNative, RefCounted)

protected:
    static void _bind_methods();

public:
    static int64_t protocol_version();
};

}  // namespace handview
