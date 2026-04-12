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
# test_audio_pipeline
# Mock-based pipeline test that exercises the audio callback → ring buffer →
# read chain without a real VLC runtime.  Validates the core data path that
# all audio features depend on.
# ---------------------------------------------------------------------------

add_executable(test_audio_pipeline
    test_audio_pipeline.cpp
    "${SRC_DIR}/support/ofxVlc4RingBuffer.cpp"
)

target_include_directories(test_audio_pipeline PRIVATE
    "${STUBS_DIR}"
    "${SRC_DIR}/support"
)

target_compile_definitions(test_audio_pipeline PRIVATE
    OFXVLC4_RINGBUFFER_STUB
)

add_test(NAME audio_pipeline COMMAND test_audio_pipeline)

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
# test_gl_ops
# Tests the GL operation helpers in ofxVlc4GlOps.h covering fence-sync,
# FBO, texture, and PBO patterns used in playback and recording.
# Uses the same stubs_gl/ directory as test_gl.
# ---------------------------------------------------------------------------

add_executable(test_gl_ops
    test_gl_ops.cpp
)

target_include_directories(test_gl_ops PRIVATE
    "${STUBS_GL_DIR}"        # ofMain.h stub + GLFW/ sub-stub (controllable)
    "${SRC_DIR}/support"     # ofxVlc4GlOps.h
)

add_test(NAME gl_ops COMMAND test_gl_ops)

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

# ---------------------------------------------------------------------------
# test_audio_helpers
# Tests the pure audio helper functions (format labels, mode labels,
# normalisation, equalizer serialisation, bit reversal, FFT, atomic max)
# extracted into ofxVlc4AudioHelpers.h.  No runtime dependencies.
# ---------------------------------------------------------------------------

add_executable(test_audio_helpers
    test_audio_helpers.cpp
)

target_include_directories(test_audio_helpers PRIVATE
    "${SRC_DIR}/support"
)

add_test(NAME audio_helpers COMMAND test_audio_helpers)

# ---------------------------------------------------------------------------
# test_video_helpers
# Tests the pure video helper functions (deinterlace/aspect/crop labels,
# mode conversions, colour clamping, visible size, playback mode strings)
# extracted into ofxVlc4VideoHelpers.h.  No runtime dependencies.
# ---------------------------------------------------------------------------

add_executable(test_video_helpers
    test_video_helpers.cpp
)

target_include_directories(test_video_helpers PRIVATE
    "${SRC_DIR}/support"
)

add_test(NAME video_helpers COMMAND test_video_helpers)

# ---------------------------------------------------------------------------
# test_media_helpers
# Tests the pure media helper functions (FOURCC conversion, bitrate/frame-rate
# formatting, bookmark time labels, float formatting, WAV header binary
# output, PlaybackTransportState defaults) extracted into
# ofxVlc4MediaHelpers.h.  No runtime dependencies.
# ---------------------------------------------------------------------------

add_executable(test_media_helpers
    test_media_helpers.cpp
)

target_include_directories(test_media_helpers PRIVATE
    "${SRC_DIR}/support"
    "${SRC_DIR}"
)

add_test(NAME media_helpers COMMAND test_media_helpers)

# ---------------------------------------------------------------------------
# test_recording_helpers
# Tests the recording path-building helpers (trimRecorderText,
# buildRecordingOutputStem, buildRecordingOutputPath,
# buildRecordingOutputPaths) extracted into ofxVlc4RecordingHelpers.h.
# Uses inline OF function stubs (ofFilePath::getFileExt etc.).
# ---------------------------------------------------------------------------

add_executable(test_recording_helpers
    test_recording_helpers.cpp
)

target_include_directories(test_recording_helpers PRIVATE
    "${SRC_DIR}/support"
)

add_test(NAME recording_helpers COMMAND test_recording_helpers)

# ---------------------------------------------------------------------------
# test_media_library_helpers
# Tests the media-library pure helpers (normalizeExtensions,
# appendMetadataValue, hasDetailedTrackMetadata) extracted into
# ofxVlc4MediaLibraryHelpers.h.  No runtime dependencies.
# ---------------------------------------------------------------------------

add_executable(test_media_library_helpers
    test_media_library_helpers.cpp
)

target_include_directories(test_media_library_helpers PRIVATE
    "${SRC_DIR}/support"
)

add_test(NAME media_library_helpers COMMAND test_media_library_helpers)

# ---------------------------------------------------------------------------
# test_playlist_helpers
# Tests the pure playlist serialisation helpers (M3U and XSPF format
# serialize/deserialize, XML escape/unescape, percent encode/decode)
# extracted into ofxVlc4PlaylistHelpers.h.  No runtime dependencies.
# ---------------------------------------------------------------------------

add_executable(test_playlist_helpers
    test_playlist_helpers.cpp
)

target_include_directories(test_playlist_helpers PRIVATE
    "${SRC_DIR}/support"
)

add_test(NAME playlist_helpers COMMAND test_playlist_helpers)

# ---------------------------------------------------------------------------
# test_video_pipeline
# Mock-based pipeline test that exercises the video callback → FBO →
# fence-sync → consumer read chain without a real VLC or GL runtime.
# Validates the core data path that all video features depend on.
# Uses the same stubs_gl/ directory as test_gl and test_gl_ops.
# ---------------------------------------------------------------------------

add_executable(test_video_pipeline
    test_video_pipeline.cpp
)

target_include_directories(test_video_pipeline PRIVATE
    "${STUBS_GL_DIR}"        # ofMain.h stub + GLFW/ sub-stub (controllable)
    "${SRC_DIR}/support"     # ofxVlc4GlOps.h, ofxVlc4VideoHelpers.h
)

add_test(NAME video_pipeline COMMAND test_video_pipeline)

# ---------------------------------------------------------------------------
# test_video_output_reinit
# Mock-based test that validates the video output reinit path:
# setup → frame delivery → teardown → re-setup → frame delivery.
# Ensures the shuttingDown flag guards callbacks during teardown and that
# resources can be safely torn down and re-created across reinit cycles.
# ---------------------------------------------------------------------------

add_executable(test_video_output_reinit
    test_video_output_reinit.cpp
)

target_include_directories(test_video_output_reinit PRIVATE
    "${STUBS_GL_DIR}"        # ofMain.h stub + GLFW/ sub-stub (controllable)
    "${SRC_DIR}/support"     # ofxVlc4GlOps.h, ofxVlc4VideoHelpers.h
)

add_test(NAME video_output_reinit COMMAND test_video_output_reinit)

# ---------------------------------------------------------------------------
# test_nle_timecode
# Tests the SMPTE timecode engine: construction, conversions, drop-frame
# math, arithmetic, comparison, clamping, and FrameRate helpers.
# ---------------------------------------------------------------------------

add_executable(test_nle_timecode
    test_nle_timecode.cpp
)

target_include_directories(test_nle_timecode PRIVATE
    "${SRC_DIR}/nle"
)

add_test(NAME nle_timecode COMMAND test_nle_timecode)

# ---------------------------------------------------------------------------
# test_nle_sequence
# Tests Track, Segment, and Sequence: creation, overlap rejection,
# segmentIndexAt, rippleShift, duration, playhead, track removal.
# ---------------------------------------------------------------------------

add_executable(test_nle_sequence
    test_nle_sequence.cpp
)

target_include_directories(test_nle_sequence PRIVATE
    "${SRC_DIR}/nle"
)

add_test(NAME nle_sequence COMMAND test_nle_sequence)

# ---------------------------------------------------------------------------
# test_nle_edit_ops
# Tests three-point editing, overwrite, splice-in, lift, and extract.
# ---------------------------------------------------------------------------

add_executable(test_nle_edit_ops
    test_nle_edit_ops.cpp
)

target_include_directories(test_nle_edit_ops PRIVATE
    "${SRC_DIR}/nle"
)

add_test(NAME nle_edit_ops COMMAND test_nle_edit_ops)

# ---------------------------------------------------------------------------
# test_nle_trim_ops
# Tests trimRipple, trimRoll, trimSlip, and trimSlide operations.
# ---------------------------------------------------------------------------

add_executable(test_nle_trim_ops
    test_nle_trim_ops.cpp
)

target_include_directories(test_nle_trim_ops PRIVATE
    "${SRC_DIR}/nle"
)

add_test(NAME nle_trim_ops COMMAND test_nle_trim_ops)

# ---------------------------------------------------------------------------
# test_nle_edl_export
# Tests CMX 3600 EDL export: single/multi segment, custom reel names,
# empty sequence, drop-frame header.
# ---------------------------------------------------------------------------

add_executable(test_nle_edl_export
    test_nle_edl_export.cpp
)

target_include_directories(test_nle_edl_export PRIVATE
    "${SRC_DIR}/nle"
)

add_test(NAME nle_edl_export COMMAND test_nle_edl_export)

# ---------------------------------------------------------------------------
# test_nle_undo
# Tests UndoStack: execute/undo/redo cycle, multiple commands, redo
# cleared on new command, canUndo/canRedo, clear, descriptions.
# ---------------------------------------------------------------------------

add_executable(test_nle_undo
    test_nle_undo.cpp
)

target_include_directories(test_nle_undo PRIVATE
    "${SRC_DIR}/nle"
)

add_test(NAME nle_undo COMMAND test_nle_undo)

# ---------------------------------------------------------------------------
# test_crash_handler
# Tests the crash handler utility: install idempotency, signal name/
# description helpers, unsigned-to-string conversion, and (on POSIX)
# verifies that a crashing child process writes a crash-log file.
# ---------------------------------------------------------------------------

add_executable(test_crash_handler
    test_crash_handler.cpp
)

target_include_directories(test_crash_handler PRIVATE
    "${SRC_DIR}/support"
)

add_test(NAME crash_handler COMMAND test_crash_handler)

# ---------------------------------------------------------------------------
# Threading support (needed by several tests below)
# ---------------------------------------------------------------------------

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# test_callback_drain
# Tests the condition-variable-based callback drain pattern used during
# lifecycle shutdown.  Pure C++17, no OF/VLC/GL dependencies.
# ---------------------------------------------------------------------------

add_executable(test_callback_drain
    test_callback_drain.cpp
)

target_link_libraries(test_callback_drain PRIVATE Threads::Threads)

add_test(NAME callback_drain COMMAND test_callback_drain)

# ---------------------------------------------------------------------------
# test_concurrent_shutdown
# Stress-tests the ControlBlock + shuttingDown pattern to ensure callbacks
# cannot race with owner destruction.  Pure C++17.
# ---------------------------------------------------------------------------

add_executable(test_concurrent_shutdown
    test_concurrent_shutdown.cpp
)

target_link_libraries(test_concurrent_shutdown PRIVATE Threads::Threads)

add_test(NAME concurrent_shutdown COMMAND test_concurrent_shutdown)

# ---------------------------------------------------------------------------
# test_gl_context_loss
# Tests hasCurrentGlContext() and the deferred-cleanup pattern using the
# controllable stubs_gl/ GLFW stub.
# ---------------------------------------------------------------------------

add_executable(test_gl_context_loss
    test_gl_context_loss.cpp
)

target_include_directories(test_gl_context_loss PRIVATE
    "${STUBS_GL_DIR}"
    "${SRC_DIR}/support"
)

add_test(NAME gl_context_loss COMMAND test_gl_context_loss)

# ---------------------------------------------------------------------------
# test_ringbuffer_overflow
# Tests ring buffer overflow policies (Drop / ExpandOnce), the one-shot
# warning flag, and a producer/consumer stress test.
# ---------------------------------------------------------------------------

add_executable(test_ringbuffer_overflow
    test_ringbuffer_overflow.cpp
    "${SRC_DIR}/support/ofxVlc4RingBuffer.cpp"
)

target_include_directories(test_ringbuffer_overflow PRIVATE
    "${STUBS_DIR}"
    "${SRC_DIR}/support"
)

target_compile_definitions(test_ringbuffer_overflow PRIVATE
    OFXVLC4_RINGBUFFER_STUB
)

target_link_libraries(test_ringbuffer_overflow PRIVATE Threads::Threads)

add_test(NAME ringbuffer_overflow COMMAND test_ringbuffer_overflow)

# ---------------------------------------------------------------------------
# test_mux_join_timeout
# Tests the future-based timeout pattern used for recording mux thread joins.
# Pure C++17, no OF/VLC/GL dependencies.
# ---------------------------------------------------------------------------

add_executable(test_mux_join_timeout
    test_mux_join_timeout.cpp
)

target_link_libraries(test_mux_join_timeout PRIVATE Threads::Threads)

add_test(NAME mux_join_timeout COMMAND test_mux_join_timeout)
