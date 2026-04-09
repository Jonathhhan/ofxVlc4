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

# ---------------------------------------------------------------------------
# test_mux_helpers
# Tests the pure filesystem/string helpers in ofxVlc4MuxHelpers.h.
# No dependency on OF, GLFW, or VLC.
# ---------------------------------------------------------------------------

add_executable(test_mux_helpers
    test_mux_helpers.cpp
)

target_include_directories(test_mux_helpers PRIVATE
    "${SRC_DIR}/support"
)

add_test(NAME mux_helpers COMMAND test_mux_helpers)

# ---------------------------------------------------------------------------
# test_playlist
# Tests the in-memory playlist manipulation logic (add, remove, move single,
# move multiple) mirroring the behaviour of MediaLibrary.  No dependency on
# OF, GLFW, or VLC.
# ---------------------------------------------------------------------------

add_executable(test_playlist
    test_playlist.cpp
)

add_test(NAME playlist COMMAND test_playlist)

# ---------------------------------------------------------------------------
# test_gl
# Tests the GL-context helpers hasCurrentGlContext() and clearAllocatedFbo()
# from ofxVlc4Utils.h.  Uses a dedicated stubs_gl/ directory that provides a
# controllable glfwGetCurrentContext() and an observable ofFbo / ofClear.
# ---------------------------------------------------------------------------

set(STUBS_GL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stubs_gl")

add_executable(test_gl
    test_gl.cpp
)

target_include_directories(test_gl PRIVATE
    "${STUBS_GL_DIR}"        # ofMain.h stub + GLFW/ sub-stub (controllable)
    "${SRC_DIR}/support"     # ofxVlc4Utils.h
)

add_test(NAME gl COMMAND test_gl)

# ---------------------------------------------------------------------------
# test_midi_bridge
# Tests MidiBridge (event-record ↔ channel-message conversion, filtering,
# describe) and MidiTimelineCursor.  Depends only on the MIDI source files;
# no OF, GLFW, or VLC stubs required.
# ---------------------------------------------------------------------------

add_executable(test_midi_bridge
    test_midi_bridge.cpp
    "${SRC_DIR}/midi/ofxVlc4MidiAnalysis.cpp"
    "${SRC_DIR}/midi/ofxVlc4MidiBridge.cpp"
)

target_include_directories(test_midi_bridge PRIVATE
    "${SRC_DIR}/midi"
)

add_test(NAME midi_bridge COMMAND test_midi_bridge)

# ---------------------------------------------------------------------------
# test_midi_playback
# Tests MidiPlaybackSession (load/clear, play/pause/stop, seek, update
# dispatch, tempo, sync, loop).  Depends on all three MIDI source files.
# ---------------------------------------------------------------------------

add_executable(test_midi_playback
    test_midi_playback.cpp
    "${SRC_DIR}/midi/ofxVlc4MidiAnalysis.cpp"
    "${SRC_DIR}/midi/ofxVlc4MidiBridge.cpp"
    "${SRC_DIR}/midi/ofxVlc4MidiPlayback.cpp"
)

target_include_directories(test_midi_playback PRIVATE
    "${SRC_DIR}/midi"
)

add_test(NAME midi_playback COMMAND test_midi_playback)

# ---------------------------------------------------------------------------
# test_types
# Tests the POD default values and enum coverage of ofxVlc4Types.h.
# No dependency on OF, GLFW, or VLC.
# ---------------------------------------------------------------------------

add_executable(test_types
    test_types.cpp
)

target_include_directories(test_types PRIVATE
    "${SRC_DIR}/core"
)

add_test(NAME types COMMAND test_types)

# ---------------------------------------------------------------------------
# test_midi_report
# Tests the MidiJsonWriter, MidiTextWriter, MidiReportExporter, and the
# toJson()/toTextReport() wrappers.  Depends on the MIDI analysis source file.
# ---------------------------------------------------------------------------

add_executable(test_midi_report
    test_midi_report.cpp
    "${SRC_DIR}/midi/ofxVlc4MidiAnalysis.cpp"
)

target_include_directories(test_midi_report PRIVATE
    "${SRC_DIR}/midi"
)

add_test(NAME midi_report COMMAND test_midi_report)
