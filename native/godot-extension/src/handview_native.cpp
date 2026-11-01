#include "handview_native.h"

#include <handview/protocol/wire.hpp>

namespace handview {

void HandviewNative::_bind_methods() {
    ClassDB::bind_static_method("HandviewNative", D_METHOD("protocol_version"), &HandviewNative::protocol_version);
}

int64_t HandviewNative::protocol_version() {
    return protocol::kProtocolVersion;
}

}  // namespace handview
