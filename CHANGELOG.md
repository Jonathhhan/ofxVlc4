# Changelog

## Unreleased

- **GL workflow: fixed cross-context sync** — `glFlush()` is now called before releasing the VLC GL context in `makeCurrent(false)`, ensuring all render commands (including the frame fence) are submitted to the GPU before the main context tries to wait on them; `GL_SYNC_FLUSH_COMMANDS_BIT` only flushes the waiting context, not the producer context, so without this flush the main thread could stall unnecessarily
- **GL workflow: linear texture filtering** — the VLC render target is now allocated with `GL_LINEAR` min/mag filters so that video scaled up or down during rendering uses bilinear filtering instead of the driver default
- **GL workflow: 10-bit texture format** — `videoResize()` now selects `GL_RGB10_A2` for the render target and `render_cfg->opengl_format` when `cfg->bitdepth > 8`, matching the existing D3D11 path (`DXGI_FORMAT_R10G10B10A2_UNORM`); the allocated GL pixel format is tracked in `allocatedGlPixelFormat` so the exposed-texture FBO stays in sync and reallocates on format changes

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
