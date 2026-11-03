cmake_minimum_required(VERSION 3.24)

# godot-cpp links the MSVC runtime statically (/MT). Every target in this tree
# must use the same runtime, so set it before any target is created.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "MSVC runtime library")

# Must precede project(). Godot needs 10.13; arm64 Macs start at 11.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(handview VERSION 0.1.0 LANGUAGES CXX)


# The mock client decodes with Media Foundation and both tools spawn processes
# with Win32, so they build on Windows only.
if(WIN32)
    option(HANDVIEW_BUILD_TOOLS "Build internal test tools" ON)
else()
    option(HANDVIEW_BUILD_TOOLS "Build internal test tools" OFF)
endif()
option(HANDVIEW_BUILD_TESTS "Build native core unit tests" ON)
option(HANDVIEW_BUILD_GODOT "Build the GDExtension (needs third_party/godot-cpp)" ON)

if(HANDVIEW_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

if(HANDVIEW_BUILD_GODOT)
    add_subdirectory(godot-extension)
endif()

if(HANDVIEW_BUILD_TOOLS)
    add_subdirectory(../tools ${CMAKE_BINARY_DIR}/tools)
endif()
