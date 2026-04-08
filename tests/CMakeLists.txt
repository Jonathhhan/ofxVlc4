cmake_minimum_required(VERSION 3.14)
project(ofxVlc4Tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow tests to reach the repository source and stub headers.
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(STUBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stubs")

# ---------------------------------------------------------------------------
# test_ring_buffer
# Compiles the ring buffer implementation with a stub ofSoundBuffer.
# ---------------------------------------------------------------------------
add_executable(test_ring_buffer
	test_ring_buffer.cpp
	${REPO_ROOT}/src/support/ofxVlc4RingBuffer.cpp
)
target_include_directories(test_ring_buffer PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}   # test_runner.h
	${STUBS_DIR}                  # stubs/ofSoundBuffer.h (replaces OF's header)
	${REPO_ROOT}/src              # support/ofxVlc4RingBuffer.h
)

# ---------------------------------------------------------------------------
# test_utils
# Tests inline utility functions from ofxVlc4Utils.h.
# No source file to compile beyond the test itself (all functions are inline).
# ---------------------------------------------------------------------------
add_executable(test_utils
	test_utils.cpp
)
target_include_directories(test_utils PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${STUBS_DIR}                           # ofMain.h, GLFW/glfw3.h stubs
	${REPO_ROOT}/src                       # support/ofxVlc4Utils.h
	${REPO_ROOT}/libs/libvlc/include       # vlc/libvlc_media.h for libvlc_state_t
)

# ---------------------------------------------------------------------------
# test_midi_analysis
# The MIDI analyzer has no OF or VLC dependencies — compile it directly.
# ---------------------------------------------------------------------------
add_executable(test_midi_analysis
	test_midi_analysis.cpp
	${REPO_ROOT}/src/midi/ofxVlc4MidiAnalysis.cpp
)
target_include_directories(test_midi_analysis PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${REPO_ROOT}/src              # midi/ofxVlc4MidiAnalysis.h
)

# ---------------------------------------------------------------------------
# CTest integration
# ---------------------------------------------------------------------------
enable_testing()
add_test(NAME ring_buffer    COMMAND test_ring_buffer)
add_test(NAME utils          COMMAND test_utils)
add_test(NAME midi_analysis  COMMAND test_midi_analysis)
