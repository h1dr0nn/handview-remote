# Shared Native Core: protocol codec, network server, encoders.
# Must not depend on any engine API.

add_library(handview_core STATIC
    src/protocol/wire.cpp
)
add_library(handview::core ALIAS handview_core)

target_include_directories(handview_core PUBLIC include)
target_compile_features(handview_core PUBLIC cxx_std_20)
set_target_properties(handview_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(handview_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(handview_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(HANDVIEW_BUILD_TESTS)
    add_executable(handview_core_tests tests/test_wire.cpp)
    target_link_libraries(handview_core_tests PRIVATE handview::core)
    add_test(NAME handview_core_tests COMMAND handview_core_tests)
endif()
