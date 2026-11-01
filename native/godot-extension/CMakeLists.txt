# GDExtension: thin Godot wrapper around the Shared Native Core.
# Output goes straight into the addon so the editor picks it up.

set(GODOT_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/godot-cpp")
if(NOT EXISTS "${GODOT_CPP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "godot-cpp missing. Run: git submodule update --init --recursive")
endif()

# GODOTCPP_TARGET (template_debug | template_release) is a godot-cpp cache option.
# godot-cpp headers are not /W4 clean; treat them as system headers.
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
add_subdirectory("${GODOT_CPP_DIR}" "${CMAKE_BINARY_DIR}/godot-cpp" EXCLUDE_FROM_ALL)

add_library(handview_gdextension SHARED
    src/register_types.cpp
    src/handview_native.cpp
    src/handview_editor_session.cpp
    src/handview_runtime.cpp
)
# Nayuki QR Code generator (MIT), for the Wi-Fi pairing code in the dock.
add_library(handview_qrcodegen STATIC ../third_party/qrcodegen/qrcodegen.cpp)
target_include_directories(handview_qrcodegen SYSTEM PUBLIC ../third_party/qrcodegen)

target_link_libraries(handview_gdextension PRIVATE handview::core godot::cpp handview_qrcodegen)

if(MSVC)
    target_compile_options(handview_gdextension PRIVATE /W4 /permissive-)
endif()

set(HANDVIEW_ADDON_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../godot-addon/addons/handview/bin"
    CACHE PATH "Where the built GDExtension library is placed")

# MVP ships Windows x86_64 only. $<1:...> stops multi-config generators from
# appending a per-config subdirectory.
set_target_properties(handview_gdextension PROPERTIES
    OUTPUT_NAME "handview.windows.${GODOTCPP_TARGET}.x86_64"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "$<1:${HANDVIEW_ADDON_BIN_DIR}>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${HANDVIEW_ADDON_BIN_DIR}>"
)
