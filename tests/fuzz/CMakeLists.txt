# CMakeLists.txt for fuzzing targets
# These targets use LibFuzzer and are built separately from the main test suite.
# Build with: CXX=clang++ cmake -DENABLE_FUZZING=ON .. && make
# Run: ./fuzz_midi_parser corpus/midi -max_total_time=60

cmake_minimum_required(VERSION 3.14)
project(ofxVlc4Fuzz CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ENABLE_FUZZING)
    message(STATUS "Fuzzing disabled. Enable with -DENABLE_FUZZING=ON")
    return()
endif()

# Require Clang for LibFuzzer support
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Fuzzing requires Clang compiler (use CXX=clang++)")
endif()

# Add fuzzer instrumentation and sanitizers
set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined -g -O1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FUZZ_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${FUZZ_FLAGS}")

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

# ---------------------------------------------------------------------------
# fuzz_midi_parser
# ---------------------------------------------------------------------------

add_executable(fuzz_midi_parser
    fuzz_midi_parser.cpp
    "${SRC_DIR}/midi/ofxVlc4MidiAnalysis.cpp"
)

target_include_directories(fuzz_midi_parser PRIVATE
    "${SRC_DIR}/midi"
)

# ---------------------------------------------------------------------------
# fuzz_m3u_parser
# ---------------------------------------------------------------------------

add_executable(fuzz_m3u_parser
    fuzz_m3u_parser.cpp
)

target_include_directories(fuzz_m3u_parser PRIVATE
    "${SRC_DIR}/support"
)

# ---------------------------------------------------------------------------
# fuzz_xspf_parser
# ---------------------------------------------------------------------------

add_executable(fuzz_xspf_parser
    fuzz_xspf_parser.cpp
)

target_include_directories(fuzz_xspf_parser PRIVATE
    "${SRC_DIR}/support"
)
