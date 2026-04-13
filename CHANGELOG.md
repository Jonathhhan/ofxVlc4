# Changelog

## 1.0.4

- **phase 3 callback boundary cleanup** — removed callback-facing static event/dialog declarations from `src/core/ofxVlc4.h`; `VlcEventRouter` now owns callback dispatch and forwards to internal instance handlers while dropping late event/dialog callbacks during shutdown; added focused router regression coverage in `tests/test_event_router_lifecycle.cpp` for attach/detach wiring and shutdown drop behavior

- **code review: recording mux thread safety** — `finalizeRecordingMuxThread()` and `cancelPendingRecordingMux()` no longer `detach()` the worker on timeout; both now keep ownership-safe, deterministic shutdown by waiting for a safe `join()` (with timeout warnings logged before continuing to wait)

- **code review: log file close error reporting** — `VlcCoreSession::closeLogFile()` now logs a warning through `ofLogWarning` when `fflush()` or `fclose()` fails, replacing the previous placeholder comments

- **code review: WAV limit constant clarity** — replaced the raw hex literal `0xFFFFFFFFull` with `std::numeric_limits<uint32_t>::max()` for the WAV data-size ceiling

- **code review: test stub consistency** — unified the `getAbsolutePath()` implementation in `tests/stubs/ofMain.h` with the `tests/stubs_gl/ofMain.h` variant so both use the same ternary form

- **fix: version constants bumped to 1.0.4** — `kOfxVlc4AddonVersionPatch` and `kOfxVlc4AddonVersionString` in `ofxVlc4.cpp` and the README release line now match the changelog

- **pimpl migration: MIDI types, RecorderPerformanceInfo, getPlaylist()** — added `#include "midi/ofxVlc4MidiBridge.h"` to `ofxVlc4.h` for MIDI types used in the public API; moved `ofxVlc4RecorderPerformanceInfo` from `ofxVlc4Recorder.h` to `ofxVlc4Types.h`; replaced the broken inline `getPlaylist()` with a non-inline method delegating through `MediaComponent` → `MediaLibrary` (PR #46)

- **code review: ring buffer gain correctness** — `read(dst, wanted, gain)` and `peekLatest(dst, wanted, gain)` now apply gain only to the samples that were actually filled or copied, not to the zero-padded tail; the previous behavior multiplied zero-padded regions by gain which was harmless for audio output but wasteful and semantically incorrect

- **code review: core session null safety in `releaseVlcResources`** — `coreSession->setPlayer(nullptr)`, `coreSession->setPlayerEvents(nullptr)`, and `coreSession->setInstance(nullptr)` calls during shutdown are now guarded with `if (coreSession)` null checks; this prevents a potential null-pointer dereference if the session was not fully initialized before teardown

- **code review: MIDI parser bounds validation** — `readBe16()` and `readBe32()` now validate that the requested byte range is within the input buffer before accessing it, throwing a descriptive `std::runtime_error` on out-of-bounds access instead of reading past the end of the vector

- **code review: MIDI VLQ overflow protection** — `readVlq()` now checks for arithmetic overflow before each `value << 7` shift, throwing if the accumulated value would exceed `UINT32_MAX`

- **code review: MIDI track length overflow-safe comparison** — the track-length validation in `analyzeFile()` now uses an overflow-proof subtraction comparison (`trackLength > bytes.size() || offset > bytes.size() - trackLength`) instead of an addition that could wrap on 32-bit or large inputs

- **code review: equalizer loop index type** — the equalizer band-application loop in `applyEqualizerSettingsNow()` now uses `size_t` for the loop variable instead of `unsigned`, matching standard STL container indexing conventions

- **code review: `pathToFileUri` exception safety** — `std::filesystem::absolute()` and `lexically_normal()` in `pathToFileUri()` are now wrapped in a try-catch so that malformed or OS-rejected paths return an empty string instead of propagating an unhandled `filesystem_error` exception

- **docs: README source layout updated** — added `ofxVlc4MuxHelpers.h`, `ofxVlc4Types.h`, and `ofxVlc4Impl.h` to the documented source layout; corrected the test binary count from eight to nine and added `test_types` to the test list

## 1.0.3

- **tests: playlist manipulation unit tests** — `tests/test_playlist.cpp` adds a self-contained `Playlist` fixture that mirrors `MediaLibrary`'s locked add/remove/move operations; covers single-item add, duplicate path rejection, remove by index, move-single forward/backward, move-multiple, `currentIndex` tracking across all mutations, and edge cases (empty list, out-of-range indices, no-op moves); no OF, GLFW, or VLC dependencies required

- **tests: GL context unit tests** — `tests/test_gl.cpp` covers `hasCurrentGlContext()` and `clearAllocatedFbo()` from `src/support/ofxVlc4Utils.h`; uses a dedicated `tests/stubs_gl/` directory with a controllable `glfwGetCurrentContext()` stub and an observable `ofFbo` / `ofClear`, so context-present and context-absent branches can both be exercised without a real GL runtime

- **recording: useful recording settings in preset** — `ofxVlc4RecordingPreset` now exposes `muxTimeoutMs` (default 15 000 ms) and `audioRingBufferSeconds` (default 4.0 s) so all mux and audio-buffer tuning goes through the single preset struct; the preset-based `textureRecordingSessionConfig` / `windowRecordingSessionConfig` overloads no longer accept a separate `muxTimeoutMs` argument — they read the value from the preset instead; `setRecordingPreset` and `getRecordingPreset` apply and round-trip both new fields; the recorder example adds "Mux timeout (s)" and "Audio ring buffer (s)" sliders to the Recorder settings panel

- **recording: reliable muxing for longer files** — the intermediate video file written during audio+video recording sessions now uses MPEG-TS (`.ts`) instead of MP4; MPEG-TS is a streaming format that requires no end-of-file finalization, eliminating the race condition where VLC's MP4 moov-atom write (which happens after `libvlc_MediaPlayerStopped` fires) caused the mux step to open an incomplete file and produce empty H265 output or audio-only H264 output for longer recordings; the video-only recording path (no mux) is unchanged and continues to emit `.mp4`; `waitForRecordingFile` now also requires three consecutive stable size readings (~150 ms) instead of one (~50 ms) before declaring an input file ready, as an additional guard against brief size-stability windows during any remaining file finalization

- **GL workflow: fixed cross-context sync** — `glFlush()` is now called before releasing the VLC GL context in `makeCurrent(false)`, ensuring all render commands (including the frame fence) are submitted to the GPU before the main context tries to wait on them; `GL_SYNC_FLUSH_COMMANDS_BIT` only flushes the waiting context, not the producer context, so without this flush the main thread could stall unnecessarily
- **GL workflow: linear texture filtering** — the VLC render target is now allocated with `GL_LINEAR` min/mag filters so that video scaled up or down during rendering uses bilinear filtering instead of the driver default
- **GL workflow: 10-bit texture format** — `videoResize()` now selects `GL_RGB10_A2` for the render target and `render_cfg->opengl_format` when `cfg->bitdepth > 8`, matching the existing D3D11 path (`DXGI_FORMAT_R10G10B10A2_UNORM`); the allocated GL pixel format is tracked in `allocatedGlPixelFormat` so the exposed-texture FBO stays in sync and reallocates on format changes
- **GL workflow: disabled vsync on VLC render context** — `vlcWindow->setVerticalSync(false)` replaces the previous `true`; VLC renders exclusively to an FBO and never swaps the window's default framebuffer, so enabling vsync was unnecessary and could throttle rendering on some drivers
- **GL workflow: validated GLFW window handle in `makeCurrent`** — the check now also verifies `vlcWindow->getGLFWWindow() != nullptr` so that a failed GLFW window creation cannot silently pass a null handle to `glfwMakeContextCurrent`
- **GL workflow: diagnostic warnings for sync failures** — `waitForPublishedFrameFenceLocked` now emits a warning when `glClientWaitSync` returns `GL_WAIT_FAILED` or `GL_TIMEOUT_EXPIRED`, making GPU stall and sync-object error conditions visible in the log; the GPU-side `glWaitSync` fallback on timeout is preserved to keep draw-order guarantees intact
- **GL workflow: warn on unshared context** — a log warning is emitted at construction if `mainWindow` is not available, because without context sharing the VLC render texture is not visible to the main context

## 1.0.2

- fixed addon version reporting so the numeric version fields and version string stay in sync at `1.0.2`
- removed the deprecated compatibility header `src/ofxVlc4Player.h`
- projects should now include `src/ofxVlc4.h` and use `ofxVlc4` directly
- restored the bundled `vlc-help.txt` asset
- `Help` now derives from `vlc-help.txt`
- `FullHelp` derives from `vlc-full-help.txt`
- Windows installs now keep one shared `runtime/vs/x64` runtime and link examples to it locally
- added first-class `addSubtitleSlave(...)` and `addAudioSlave(...)` helpers on top of the generic media-slave path
- `ofxVlc4Example` now includes a subtitle picker plus consistent subtitle-file handling through path entry and drag/drop
- `ofxVlc4Example` now includes dedicated `DVD / Disc` controls for title/chapter/program navigation and menu actions, while teletext controls live under `Tracks & Subtitles`
- added a high-level `executePlayerCommand(...)` API for common transport, disc navigation, and teletext command dispatch
- added `setPreferredDecoderDevice(...)` / `getPreferredDecoderDevice()` for Windows hardware-decoder preference (`Auto`, `D3D11`, `DXVA2`, `NVDEC`, `None`)
- added pre-init VLC subtitle text-renderer settings for renderer choice, font family, color, opacity, and bold styling
- added advanced raw-init escape hatches through `setExtraInitArgs(...)`, `addExtraInitArg(...)`, and the existing `init(argc, argv)` path, with raw args applied after typed addon-generated init settings
- added first-class recording presets for `H265 / HEVC`, `MKV / Opus`, and `MKV / LPCM`
- `H265 / HEVC` recording now enforces `MKV` mux profiles and normalizes capture sizes to the bundled x265 encoder alignment (`width % 16 == 0`, `height % 8 == 0`)
- hardened playback-time diagnostics and track queries so `Diagnostics` and `Tracks & Subtitles` stay stable during active playback
- moved the example anaglyph shader files into addon-owned `src/video/shaders`
- simplified and stabilized `ofxVlc4360Example` around `Crystal Shower.mp4`, with `Sphere` as the default renderer and `libVLC 360` kept as the reference/native path
- marked bundled VLC visualizer-module support in `ofxVlc4Example` as experimental; `ofxProjectM` remains the primary integrated visualization path

## 1.0.1

Highlights:

- renamed the main addon class and internal source layout from `ofxVlc4Player` to `ofxVlc4`
- renamed the smaller example to `ofxVlc4MidiExample` and rebuilt the examples around the current addon scope:
  - `ofxVlc4Example`
  - `ofxVlc4360Example`
  - `ofxVlc4MidiExample`
  - `ofxVlc4RecorderExample`
- added `ofxVlc4360Example` as a focused ImGui-based 360 / panoramic playback surface for projection, stereo mode, and live viewpoint control
- added `ofxVlc4360Example/download-360-example-media.sh` to fetch free 360 sample media for `ofxVlc4360Example`
- formalized the addon-owned playlist API with structured helpers:
  - `getPlaylistStateInfo()`
  - `getPlaylistItems()`
  - `getCurrentPlaylistItemInfo()`
- expanded the recorder into an addon-owned session workflow with:
  - session start/stop/finalize/mux handling
  - texture and whole-window capture helpers
  - recorder presets for codec, mux profile, size, FPS, bitrate, and cleanup behavior
- added whole-window recording and post-stop muxing support to the recorder example
- added high-resolution playback clock support through `libvlc_media_player_watch_time(...)`, including:
  - watch-time state/callback wrappers
  - playback timecode formatting helpers
- expanded MIDI support with addon-owned local transport, sync, and output routing:
  - MIDI Clock
  - MTC quarter-frame
  - SysEx-aware message dispatch
- exposed addon-owned audio/video filter-chain helpers from the discovered libVLC filter lists
- tightened backend behavior around effects:
  - `Texture` remains the safer default path for integrated OF rendering and live adjustment fallback
  - `NativeWindow / HWND` is the path where libVLC-native video filters/effects are applied live
- refreshed the main and example documentation so playlist, recorder, MIDI, playback-clock, backend, and known-limit guidance are documented in English

## 1.0.0

Initial public release of the current `ofxVlc4` codebase.

Highlights:

- stable texture-based `libVLC 4` playback path for openFrameworks
- expanded audio, video, media, subtitle, renderer, navigation, snapshot, and recording API
- state snapshot getters for app/gui workflows:
  - `getMediaReadinessInfo()`
  - `getPlaybackStateInfo()`
  - `getAudioStateInfo()`
  - `getVideoStateInfo()`
  - `getRendererStateInfo()`
  - `getSubtitleStateInfo()`
  - `getNavigationStateInfo()`
- improved startup/readiness model with explicit phases for:
  - media attached
  - startup prepared
  - geometry known
  - first frame received
  - playback active
- safer startup, shutdown, and GL/video texture handling
- equalizer preset metadata and import/export helpers
- audio/video filter list support plus reload/live-reapply workflow helpers
- native snapshot and recording state/result reporting
- full ImGui example and smaller minimal example

Notes:

- the recommended default path is the openFrameworks texture backend
- heavy libVLC state refreshes should stay out of the media-player callback thread
- addon release version: `1.0.0`
