# ofxVlc4360Example

Focused 360 / panoramic playback example for `ofxVlc4`.

This example keeps the UI intentionally small:

- one `ofxVlc4` player
- one preview surface
- one ImGui panel for projection, stereo mode, and viewpoint

What it shows:

- `VideoProjectionMode` controls:
  - `Auto`
  - `Rectangular`
  - `360 Equirectangular`
  - `Cubemap`
- `VideoStereoMode` controls:
  - `Auto`
  - `Stereo`
  - `Left Eye`
  - `Right Eye`
  - `Side By Side`
- live viewpoint control:
  - yaw
  - pitch
  - roll
  - FOV
- drag-and-drop loading
- ImGui control surface instead of keyboard-only navigation
- optional helper script for downloading a free 360 sample into the example data folder

Controls:

- `Open...`
  - choose media from a file dialog
- drag-and-drop
  - load one or more files, or a folder of supported media, into the playlist
- `Space`
  - play / pause
- `S`
  - stop
- `R`
  - reset viewpoint
- arrow keys
  - nudge yaw / pitch
- `[` / `]`
  - narrow / widen FOV

Useful API calls demonstrated:

- `setVideoProjectionMode(...)`
- `setVideoStereoMode(...)`
- `setVideoViewpoint(...)`
- `resetVideoViewpoint()`
- `getVideoStateInfo()`
- `formatCurrentPlaybackTimecode()`
- `draw(...)`

Helper script:

- [../scripts/download-360-example-media.sh](../scripts/download-360-example-media.sh)
- downloads a free 360 sample from Wikimedia Commons into `bin/data`
  - presets:
    - `oceanside-4k`
    - `dji-mini-2`
  - example:
    - `bash ../scripts/download-360-example-media.sh`

Notes:

- this example is meant for 360 and panoramic viewing workflows, not the full filter/effects surface
- `Texture` backend remains the safest default path here
- the addon now accepts a broader default set of media extensions for example loading, including common transport-stream, playlist, and panoramic image formats such as `ts`, `mts`, `m2ts`, `m3u8`, `webp`, and `tiff`
- dropping multiple files now builds a playlist instead of only taking the first path
- the helper script downloads free media, but licensing and attribution still follow the source file page noted by the script output
- for full diagnostics and every subsystem, use [../ofxVlc4Example/README.md](../ofxVlc4Example/README.md)
