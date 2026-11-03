# Shared Native Core: protocol codec, network, flow control, encoders.
# Must not depend on any engine API. Sources and tests are globbed so parallel
# work can add files without touching this list.

file(GLOB_RECURSE HANDVIEW_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
# Encoder backends are per platform: D3D11, NVENC and Media Foundation on
# Windows, VideoToolbox on macOS.
if(NOT WIN32)
    list(FILTER HANDVIEW_CORE_SOURCES EXCLUDE REGEX "/encode/(gpu|mf|nvenc)\\.cpp$")
endif()
if(NOT APPLE)
    list(FILTER HANDVIEW_CORE_SOURCES EXCLUDE REGEX "/encode/videotoolbox\\.cpp$")
endif()

add_library(handview_core STATIC ${HANDVIEW_CORE_SOURCES})
add_library(handview::core ALIAS handview_core)

target_include_directories(handview_core PUBLIC include)
target_compile_features(handview_core PUBLIC cxx_std_20)
set_target_properties(handview_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    target_compile_definitions(handview_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(handview_core PUBLIC ws2_32 d3d11 dxgi mfplat mfuuid mf strmiids ole32 oleaut32)
    target_include_directories(handview_core SYSTEM PRIVATE ../third_party/nv-codec-headers/include)
elseif(APPLE)
    target_link_libraries(handview_core PUBLIC
        "-framework VideoToolbox" "-framework CoreMedia" "-framework CoreVideo"
        "-framework CoreFoundation" "-framework Accelerate")
endif()

if(MSVC)
    target_compile_options(handview_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(handview_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(HANDVIEW_BUILD_TESTS)
    # One executable per tests/test_*.cpp. Tests find shared vectors through
    # HANDVIEW_VECTORS_DIR.
    file(GLOB HANDVIEW_CORE_TESTS CONFIGURE_DEPENDS tests/test_*.cpp)
    foreach(test_src IN LISTS HANDVIEW_CORE_TESTS)
        get_filename_component(test_name "${test_src}" NAME_WE)
        add_executable(${test_name} "${test_src}")
        target_link_libraries(${test_name} PRIVATE handview::core)
        target_include_directories(${test_name} PRIVATE tests)
        target_compile_definitions(${test_name} PRIVATE
            HANDVIEW_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/vectors")
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
