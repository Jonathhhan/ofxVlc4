# ofxVlc4RecorderExample

Focused recording example for `ofxVlc4`.

This example keeps the UI small and concentrates on the addon's recording-related features:

- normal audio/video playback through `libVLC`
- benchmark mode with an openFrameworks-generated texture
- whole-window recording through the addon capture helper
- switchable benchmark audio source: `OF synth` or `VLC playback`
- native VLC recording toggle
- WAV audio capture through the addon recorder
- session-based benchmark and window recording with post-stop muxing
- ImGui control panel for recorder state, codec, mux profile, size, FPS, and bitrate
  - includes `H265 / HEVC` plus `MKV / Opus` and `MKV / LPCM` recorder presets
- snapshot capture
- drag-and-drop media loading
- automatic startup seed from `bin/data/fingers.mp4` when present
- recorder readback metrics for async PBO latency, queue depth, drops, and map failures

See also:

- [../README.md](../README.md)
- [../CHANGELOG.md](../CHANGELOG.md)

Controls:

- `O`
  - open a media file
- `Space`
  - play / pause
- `S`
  - stop
- `N` / `P`
  - next / previous playlist item
- `Up / Down`
  - volume
- `M`
  - mute toggle
- `R`
  - toggle native VLC recording
- `A`
  - toggle WAV audio capture from the currently selected source
- `B`
  - toggle benchmark mode
- `F`
  - toggle benchmark audio source between `OF synth` and `VLC playback`
- `G`
  - cycle benchmark/native video recording codec between `H264`, `H265 / HEVC`, `MP4V`, and `MJPG`
- `H`
  - cycle benchmark mux profile between `MP4/AAC`, `MKV/Opus`, `MKV/FLAC`, `MKV/LPCM`, and `OGG/Vorbis`
  - when `H265 / HEVC` is selected, the cycle is restricted to the `MKV` profiles
- `J`
  - toggle whether successful muxes delete or keep the temporary sidecar `.mp4` and `.wav`
- `V`
  - start / stop benchmark video recording with the currently selected benchmark audio source
- `W`
  - start / stop whole-window recording with audio and post-stop muxing
- `L`
  - cycle recorder readback policy
- `[` / `]`
  - decrease / increase recorder readback buffer count
- `X`
  - take a snapshot
- `C`
  - clear last status / error messages
- file drag-and-drop
  - replace the playlist and start the first valid dropped file

Output folders:

- native recordings
  - `bin/data/recordings/native`
- WAV captures
  - `bin/data/recordings/audio`
- benchmark captures
  - `bin/data/recordings/benchmark`
  - successful benchmark muxes keep the final `*-muxed.mp4`, `*-muxed.mkv`, or `*-muxed.ogg`
- `J` controls whether the temporary sidecar `.mp4` and `.wav` are removed or preserved after a successful mux

The ImGui recorder panel is now always visible and is the primary control surface for the example.
- `H265 / HEVC` recording currently requires an `MKV` mux profile
- `H265 / HEVC` capture sizes are normalized before encoding (`width % 16 == 0`, `height % 8 == 0`)
- snapshots
  - `bin/data/snapshots`

Useful API calls demonstrated:

- `setAudioCaptureEnabled(...)`
- `init(...)`
- `startRecordingSession(...)`
- `startTextureRecordingSession(...)`
- `startWindowRecordingSession(...)`
- `stopRecordingSession()`
- `getRecordingSessionState()`
- `getRecordingAudioSource()`
- `setRecordingPreset(...)`
- `getRecordingPreset()`
- `setNativeRecordDirectory(...)`
- `setNativeRecordingEnabled(...)`
- `isNativeRecordingEnabled()`
- `recordAudio(...)`
- `submitRecordedAudioSamples(...)`
- `ofxVlc4MuxOptions`
- `isAudioRecording()`
- `isVideoRecording()`
- `isWindowRecording()`
- `takeSnapshot(...)`
- `getPlaybackStateInfo()`
- `getMediaReadinessInfo()`
- `getVideoStateInfo()`
- `draw(...)`
- `close()`

Recommended recorder flow:

- configure the addon once:
  - `setRecordingPreset(...)`
  - `setAudioCaptureEnabled(...)`
- start with one session entry point:
  - `startRecordingSession(...)`
  - or `startTextureRecordingSession(...)` / `startWindowRecordingSession(...)`
- stop with:
  - `stopRecordingSession()`
- observe lifecycle with:
  - `getRecordingSessionState()`
- let the addon own finalizing and optional muxing instead of stitching together stop and mux logic in app code

This example is a better starting point than the simple example when your main goal is testing recorder behavior, snapshot capture, and shutdown stability instead of MIDI or GUI tooling.
