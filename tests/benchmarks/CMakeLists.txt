cmake_minimum_required(VERSION 3.14)
project(ofxVlc4Benchmarks CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Performance benchmarks - measure and track performance metrics

# Find VLC (required for benchmarks)
if(WIN32)
    set(VLC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/include")
    set(VLC_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/lib/vs/libvlc.lib")
elseif(APPLE)
    set(VLC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/include")
    set(VLC_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libvlc/lib/osx/lib/libvlc.dylib")
else()
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

# Test media path (can be overridden)
set(BENCHMARK_TEST_MEDIA "${CMAKE_CURRENT_SOURCE_DIR}/test_media.mp4"
    CACHE STRING "Path to test media file for benchmarks")

# ---------------------------------------------------------------------------
# benchmark_playback
# Measures playback performance metrics
# ---------------------------------------------------------------------------

add_executable(benchmark_playback
    benchmark_playback.cpp
)

target_include_directories(benchmark_playback PRIVATE
    "${VLC_INCLUDE_DIR}"
    "${STUBS_DIR}"
    "${SRC_DIR}"
)

target_compile_definitions(benchmark_playback PRIVATE
    BENCHMARK_TEST_MEDIA="${BENCHMARK_TEST_MEDIA}"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(benchmark_playback PRIVATE ${VLC_LIBRARIES})
else()
    target_link_libraries(benchmark_playback PRIVATE "${VLC_LIBRARY}")
endif()

# ---------------------------------------------------------------------------
# benchmark_recording
# Measures recording throughput and encoding performance
# ---------------------------------------------------------------------------

add_executable(benchmark_recording
    benchmark_recording.cpp
)

target_include_directories(benchmark_recording PRIVATE
    "${VLC_INCLUDE_DIR}"
    "${STUBS_DIR}"
    "${SRC_DIR}"
)

target_compile_definitions(benchmark_recording PRIVATE
    BENCHMARK_TEST_MEDIA="${BENCHMARK_TEST_MEDIA}"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(benchmark_recording PRIVATE ${VLC_LIBRARIES})
else()
    target_link_libraries(benchmark_recording PRIVATE "${VLC_LIBRARY}")
endif()

# ---------------------------------------------------------------------------
# benchmark_memory
# Measures memory usage patterns
# ---------------------------------------------------------------------------

add_executable(benchmark_memory
    benchmark_memory.cpp
)

target_include_directories(benchmark_memory PRIVATE
    "${VLC_INCLUDE_DIR}"
    "${STUBS_DIR}"
    "${SRC_DIR}"
)

target_compile_definitions(benchmark_memory PRIVATE
    BENCHMARK_TEST_MEDIA="${BENCHMARK_TEST_MEDIA}"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(benchmark_memory PRIVATE ${VLC_LIBRARIES})
else()
    target_link_libraries(benchmark_memory PRIVATE "${VLC_LIBRARY}")
endif()

# Print configuration
message(STATUS "Benchmarks Configuration:")
message(STATUS "  VLC include: ${VLC_INCLUDE_DIR}")
message(STATUS "  VLC library: ${VLC_LIBRARY}")
message(STATUS "  Test media:  ${BENCHMARK_TEST_MEDIA}")
