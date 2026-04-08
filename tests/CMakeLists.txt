cmake_minimum_required(VERSION 3.14)
project(ofxVlc4Tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# Source and stub root directories.
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(STUBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stubs")

# ---------------------------------------------------------------------------
# test_ringbuffer
# ---------------------------------------------------------------------------

add_executable(test_ringbuffer
    test_ringbuffer.cpp
    "${SRC_DIR}/support/ofxVlc4RingBuffer.cpp"
)

# Expose the stub headers in preference to any real OF installation.
target_include_directories(test_ringbuffer PRIVATE
    "${STUBS_DIR}"
    "${SRC_DIR}/support"
)

# The stub provides ofSoundBuffer.h directly; suppress the nested OF path.
target_compile_definitions(test_ringbuffer PRIVATE
    OFXVLC4_RINGBUFFER_STUB
)

add_test(NAME ringbuffer COMMAND test_ringbuffer)

# ---------------------------------------------------------------------------
# test_midi_analysis
# Depends only on the C++ standard library; no OF/GLFW stubs required.
# ---------------------------------------------------------------------------

add_executable(test_midi_analysis
    test_midi_analysis.cpp
    "${SRC_DIR}/midi/ofxVlc4MidiAnalysis.cpp"
)

target_include_directories(test_midi_analysis PRIVATE
    "${SRC_DIR}/midi"
)

add_test(NAME midi_analysis COMMAND test_midi_analysis)

# ---------------------------------------------------------------------------
# test_utils
# Tests the pure string/math helpers in ofxVlc4Utils.h.  Stubs supply the
# minimal OF, GLFW, and VLC type definitions needed for compilation.
# ---------------------------------------------------------------------------

add_executable(test_utils
    test_utils.cpp
)

target_include_directories(test_utils PRIVATE
    "${STUBS_DIR}"        # ofMain.h stub + GLFW/ and vlc/ sub-stubs
    "${SRC_DIR}/support"  # ofxVlc4Utils.h
)

add_test(NAME utils COMMAND test_utils)
