# ofxVlc4360Example

Focused 360 / panoramic playback example for `ofxVlc4`.

This example keeps the UI intentionally small:

- one `ofxVlc4` player
- one preview surface
- one ImGui panel for renderer and camera/viewpoint control

What it shows:

- two 360 render paths:
  - `Sphere`
  - `libVLC 360`
- live camera/viewpoint control:
  - drag to look around
  - FOV
- drag-and-drop loading
- ImGui control surface instead of keyboard-only navigation
- startup seed loading from `bin/data`
- preference for a known-good mono 360 sample named `Crystal Shower.mp4` when present
- optional helper script for downloading free 360 samples into the example data folder

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
- `[` / `]`
  - narrow / widen FOV
- `M`
  - toggle `Sphere` / `libVLC 360`

Useful API calls demonstrated:

- `setVideoOutputBackend(...)`
- `setVideoProjectionMode(...)`
- `setVideoStereoMode(...)`
- `setVideoViewpoint(...)`
- `resetVideoViewpoint()`
- `getVideoStateInfo()`
- `getWatchTimeInfo()`
- `getTexture()`

Helper script:

- [scripts/download-360-example-media.sh](scripts/download-360-example-media.sh)
- downloads a free 360 sample from Wikimedia Commons into `bin/data`
  - presets:
    - `oceanside-4k`
    - `dji-mini-2`
  - example:
    - `bash scripts/download-360-example-media.sh`

Recommended reference sample:

- `Crystal Shower Falls`
  - Vimeo reference: [Crystal Shower Falls](https://vimeo.com/215984159)
  - if you save it locally as `Crystal Shower.mp4` in `bin/data`, the example will prefer it as the startup seed
  - this is the preferred mono 360 reference for tuning `Sphere` mode

Notes:

- this example is meant for 360 and panoramic viewing workflows, not the full filter/effects surface
- `Sphere` is the default preview path because it stays inside the openFrameworks window
- `Sphere` mode uses a simple mono mapping with flipped-Y orientation by default
- `libVLC 360` is useful as a reference check, but it renders in the separate native video window
- the addon now accepts a broader default set of media extensions for example loading, including common transport-stream, playlist, and panoramic image formats such as `ts`, `mts`, `m2ts`, `m3u8`, `webp`, and `tiff`
- dropping multiple files now builds a playlist instead of only taking the first path
- some 360 files are stereo-packed or otherwise unusual; `libVLC 360` is the better reference path for validating those files
- the helper script downloads free media, but licensing and attribution still follow the source file page noted by the script output
- for full diagnostics and every subsystem, use [../ofxVlc4Example/README.md](../ofxVlc4Example/README.md)
