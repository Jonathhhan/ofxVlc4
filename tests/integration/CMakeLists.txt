cmake_minimum_required(VERSION 3.14)
project(ofxVlc4IntegrationTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# These tests require actual VLC runtime
# Run separately from unit tests: cmake /path/to/integration && make && ctest

include(CTest)

# Test media file path (can be overridden)
set(INTEGRATION_TEST_MEDIA "${CMAKE_CURRENT_SOURCE_DIR}/test_media.mp4"
    CACHE STRING "Path to test media file for integration tests")

# Check if test media exists
if(NOT EXISTS "${INTEGRATION_TEST_MEDIA}")
    message(WARNING
        "Test media not found at: ${INTEGRATION_TEST_MEDIA}\n"
        "Integration tests will be built but may fail.\n"
        "Download test media or set INTEGRATION_TEST_MEDIA path.")
endif()

# Find VLC (required for integration tests)
if(WIN32)
    # Windows: Use bundled VLC from install script
    set(VLC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/include")
    set(VLC_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/lib/vs/libvlc.lib")
elseif(APPLE)
    # macOS: Use bundled VLC from install script
    set(VLC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/include")
    set(VLC_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/lib/osx/lib/libvlc.dylib")
else()
    # Linux: Use pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VLC REQUIRED libvlc)
    set(VLC_INCLUDE_DIR ${VLC_INCLUDE_DIRS})
    set(VLC_LIBRARY ${VLC_LIBRARIES})
endif()

if(NOT EXISTS "${VLC_INCLUDE_DIR}/vlc/vlc.h")
    message(FATAL_ERROR
        "VLC headers not found. Run scripts/install-libvlc.sh first.\n"
        "Looking for: ${VLC_INCLUDE_DIR}/vlc/vlc.h")
endif()

# Source directories
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
set(STUBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../stubs")

# ---------------------------------------------------------------------------
# test_integration_playback
# Tests basic playback lifecycle with real VLC runtime
# ---------------------------------------------------------------------------

add_executable(test_integration_playback
    test_integration_playback.cpp
)

target_include_directories(test_integration_playback PRIVATE
    "${VLC_INCLUDE_DIR}"
    "${STUBS_DIR}"
    "${SRC_DIR}"
)

target_compile_definitions(test_integration_playback PRIVATE
    INTEGRATION_TEST_MEDIA="${INTEGRATION_TEST_MEDIA}"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(test_integration_playback PRIVATE ${VLC_LIBRARIES})
else()
    target_link_libraries(test_integration_playback PRIVATE "${VLC_LIBRARY}")
endif()

add_test(NAME integration_playback COMMAND test_integration_playback)

# Set test timeout (30 seconds max)
set_tests_properties(integration_playback PROPERTIES TIMEOUT 30)

# ---------------------------------------------------------------------------
# test_integration_recording
# Tests recording workflow with real VLC runtime
# ---------------------------------------------------------------------------

add_executable(test_integration_recording
    test_integration_recording.cpp
)

target_include_directories(test_integration_recording PRIVATE
    "${VLC_INCLUDE_DIR}"
    "${STUBS_DIR}"
    "${SRC_DIR}"
)

target_compile_definitions(test_integration_recording PRIVATE
    INTEGRATION_TEST_MEDIA="${INTEGRATION_TEST_MEDIA}"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(test_integration_recording PRIVATE ${VLC_LIBRARIES})
else()
    target_link_libraries(test_integration_recording PRIVATE "${VLC_LIBRARY}")
endif()

add_test(NAME integration_recording COMMAND test_integration_recording)

# Set test timeout (30 seconds max)
set_tests_properties(integration_recording PROPERTIES TIMEOUT 30)

# Print configuration
message(STATUS "Integration Tests Configuration:")
message(STATUS "  VLC include: ${VLC_INCLUDE_DIR}")
message(STATUS "  VLC library: ${VLC_LIBRARY}")
message(STATUS "  Test media:  ${INTEGRATION_TEST_MEDIA}")
