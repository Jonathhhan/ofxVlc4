# Changelog

## Unreleased

## 1.0.1

Highlights:

- renamed the main addon class and internal source layout from `ofxVlc4Player` to `ofxVlc4`
- kept `src/ofxVlc4Player.h` as a compatibility header with `using ofxVlc4Player = ofxVlc4`
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
