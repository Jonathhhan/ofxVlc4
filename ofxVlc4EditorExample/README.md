# ofxVlc4EditorExample

Simple cut-only video editor example for `ofxVlc4`.

This example demonstrates combining several addon features into a basic editing workflow:

- **clip model** — add, remove, and reorder clips from multiple source files
- **in / out points** — per-clip trim markers set from the current playback position
- **timeline clip list** — ImGui-based clip list with selection, reordering, and trim controls
- **preview** — scrub, play, pause, frame-step through the currently selected clip
- **timecode overlay** — displays the current playback position using the addon's timecode formatter
- **export pipeline** — sequentially plays each clip from in-point to out-point through the addon recording API, producing one exported file per clip
- **drag-and-drop** — drop media files onto the window to add them to the timeline

This is a **cut-only / trim editor**, not a multi-track NLE.  Each clip is played sequentially through VLC and captured through the addon's texture recording pipeline during export.

![Editor screenshot placeholder](../docs/ofxVlc4EditorExample.jpg)

See also:

- [../README.md](../README.md)
- [../CHANGELOG.md](../CHANGELOG.md)

## Controls

- `O`
  - open file dialog to add a clip
- `Space`
  - play / pause the selected clip
- `S`
  - stop playback
- `I`
  - set the in-point of the selected clip to the current playback position
- `K`
  - set the out-point of the selected clip to the current playback position
- `.` (period)
  - step one frame forward
- `,` (comma)
  - step one frame backward
- `E`
  - begin exporting the timeline
- `Delete / Backspace`
  - remove the selected clip
- `Up / Down`
  - adjust volume
- file drag-and-drop
  - add dropped files as clips on the timeline

## Export

Pressing `E` (or the **Export Timeline** button) iterates through each clip in order:

1. Loads the clip source
2. Seeks to the clip's in-point
3. Starts recording through the addon's `recordAudioVideo()` API
4. Plays until the out-point is reached
5. Stops recording
6. Moves to the next clip

Exported files appear in `bin/data/exports/`.

## Useful API calls demonstrated

- `addPathToPlaylist(...)` / `clearPlaylist()` / `playIndex(...)`
- `setTime(...)` / `getTime()` / `getLength()` / `getPosition()` / `setPosition(...)`
- `nextFrame()` / `jumpTime(...)`
- `play()` / `pause()` / `stop()`
- `recordAudioVideo(...)` / `stopRecordingSession()`
- `getRecordingSessionState()`
- `setRecordingPreset(...)`
- `formatCurrentPlaybackTimecode()`
- `setWatchTimeEnabled(...)` / `setWatchTimeMinPeriodUs(...)`
- `getPlaybackStateInfo()` / `getVideoStateInfo()` / `getMediaReadinessInfo()`
- `draw(...)` / `getTexture()`
- `readAudioIntoBuffer(...)`
- `close()`

## Building

This example follows the standard openFrameworks addon example layout.  It requires `ofxImGui` and `ofxVlc4` (declared in `addons.make`).  Build with the openFrameworks project generator or the Makefile on Linux:

```bash
cd ofxVlc4EditorExample
make Release
```
