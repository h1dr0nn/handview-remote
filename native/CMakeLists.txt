cmake_minimum_required(VERSION 3.24)

# godot-cpp links the MSVC runtime statically (/MT). Every target in this tree
# must use the same runtime, so set it before any target is created.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "MSVC runtime library")

project(handview VERSION 0.1.0 LANGUAGES CXX)

option(HANDVIEW_BUILD_TESTS "Build native core unit tests" ON)
option(HANDVIEW_BUILD_GODOT "Build the GDExtension (needs third_party/godot-cpp)" ON)

if(HANDVIEW_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

if(HANDVIEW_BUILD_GODOT)
    add_subdirectory(godot-extension)
endif()
