#include "register_types.h"

#include "handview_editor_session.h"
#include "handview_native.h"
#include "handview_runtime.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

void initialize_handview_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(handview::HandviewNative);
    GDREGISTER_CLASS(handview::HandviewEditorSession);
    GDREGISTER_CLASS(handview::HandviewRuntime);
}

void uninitialize_handview_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
}

extern "C" {

GDExtensionBool GDE_EXPORT handview_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                 GDExtensionClassLibraryPtr p_library,
                                                 GDExtensionInitialization* r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_handview_module);
    init_obj.register_terminator(uninitialize_handview_module);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

}  // extern "C"
