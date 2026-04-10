# ofxVlc4NleEditor

Avid-oriented Non-Linear Editor (NLE) built on `ofxVlc4`, `ofxLineaDeTiempo`, and openFrameworks.

This application provides professional NLE editing concepts on top of the `ofxVlc4` media engine, including dual source/record monitors, three-point editing, track-based timeline, segment mode editing, trim modes, J-K-L shuttle control, a bin system for media management, and CMX 3600 EDL export.

![NLE Editor](../docs/ofxVlc4NleEditor.jpg)

## Architecture

The NLE is built in layers:

| Layer | Component | Description |
|-------|-----------|-------------|
| **Data Model** | `src/nle/` headers | Pure C++ logic: Timecode, MasterClip, Track, Sequence, EditOps, TrimOps, EDL export, Undo stack |
| **Media Engine** | ofxVlc4 | Two player instances: source monitor + record monitor |
| **Timeline UI** | ofxLineaDeTiempo | Visual track-based timeline with keyframe support |
| **Application** | ofApp | Connects everything: Avid-style keyboard workflow, dual monitors, bin system |

## Layout

```
┌──────────────┬──────────────┬────────────┐
│    SOURCE    │    RECORD    │    BIN     │
│   Monitor    │   Monitor    │   List    │
│   (left)     │   (right)    │           │
├──────────────┴──────────────┴────────────┤
│              Transport Bar                │
├──────────────────────────────────────────┤
│              Info Bar                     │
├──────────────────────────────────────────┤
│         Timeline (ofxLineaDeTiempo)       │
└──────────────────────────────────────────┘
```

## Controls

### Monitor Selection
- `Esc` — Toggle active monitor (Source ↔ Record)

### File Operations
- `O` — Open file dialog to import media
- Drag-and-drop files onto the window to import

### Mark IN/OUT (Three-Point Editing)
- `I` — Mark IN point on active monitor
- `P` — Mark OUT point on active monitor
- `Q` — Go to IN point
- `W` — Go to OUT point
- `G` — Clear all marks

### Edit Operations
- `V` — **Splice-In** (Insert): push downstream material right, insert new clip (Avid yellow)
- `B` — **Overwrite**: replace material in-place (Avid red)
- `Z` — **Lift**: remove segment, leave gap (filler)
- `X` — **Extract**: remove segment and close gap (ripple delete)
- `F` — **Match Frame**: find source clip/timecode from record monitor position

### Transport (J-K-L Shuttle)
- `J` — Shuttle reverse (press repeatedly to increase speed: -1x, -2x, -4x)
- `K` — Pause / shuttle stop
- `L` — Shuttle forward (press repeatedly: +1x, +2x, +4x)
- `K+J` — Step one frame backward
- `K+L` — Step one frame forward
- `Space` — Play / Pause
- `Enter/Return` — Stop
- `.` — Step one frame forward
- `,` — Step one frame backward

### Trim Modes
- `1` — Trim mode OFF
- `2` — **Ripple** trim (extend/shorten, timeline duration changes)
- `3` — **Roll** trim (move edit point, duration unchanged)
- `4` — **Slip** (change source IN/OUT, keep position/duration)
- `5` — **Slide** (move segment, adjust neighbors)

### Volume
- `Up Arrow` — Volume up
- `Down Arrow` — Volume down

### Export
- `E` — Export timeline segments
- `D` — Export EDL (CMX 3600 format)

### Undo
- `Ctrl+Z` — Undo

### Timeline
- `T` — Show/Hide timeline view

## Three-Point Editing Workflow

1. Load a clip in the Source Monitor (drag-and-drop or press `O`)
2. Mark source IN (`I`) and OUT (`P`) on the Source Monitor
3. Park the Record Monitor playhead at the desired insert point, mark Record IN (`I`)
4. Press `V` (Splice-In) or `B` (Overwrite) — the system calculates the 4th point automatically

Any 3 of the 4 points (Source IN, Source OUT, Record IN, Record OUT) is sufficient. The system derives the missing 4th point.

## NLE Data Model

All editing logic lives in pure C++ headers under `src/nle/`:

- **`NleTimecode.h`** — SMPTE timecode with drop-frame support (29.97 DF, 59.94 DF)
- **`NleClip.h`** — MasterClip (source media reference + metadata), SubClip (named sub-range)
- **`NleTrack.h`** — Track with non-overlapping Segments, lock/mute/solo
- **`NleSequence.h`** — Sequence (timeline) managing video/audio tracks, playhead
- **`NleEditOps.h`** — Three-point edit resolution, Overwrite, Splice-In, Lift, Extract
- **`NleTrimOps.h`** — Ripple, Roll, Slip, Slide trim operations
- **`NleEdlExport.h`** — CMX 3600 EDL export
- **`NleUndoStack.h`** — Command-pattern undo/redo stack

These headers have no OF/VLC dependencies and are unit-tested in `tests/test_nle_*.cpp`.

## Export

### Timeline Export (`E`)
Iterates through each segment on V1, loading the source media via ofxVlc4 and recording through the addon's recording pipeline.

### EDL Export (`D`)
Generates a CMX 3600 format Edit Decision List and saves it to `bin/data/exports/<sequence-name>.edl`. This is the standard interchange format for NLE roundtripping with other professional editors (Avid Media Composer, DaVinci Resolve, Premiere Pro).

## Dependencies

- [openFrameworks](https://openframeworks.cc/) (0.12+)
- [ofxVlc4](https://github.com/Jonathhhan/ofxVlc4) — Media playback/recording engine
- [ofxLineaDeTiempo](https://github.com/roymacdonald/ofxLineaDeTiempo) — Timeline UI
- [ofxGui](https://openframeworks.cc/) — Included with openFrameworks

## Building

This example follows the standard openFrameworks addon example layout. Install all dependencies into your `openFrameworks/addons/` folder, then build:

```bash
cd ofxVlc4NleEditor
make Release
```

Or use the openFrameworks Project Generator.

## API Calls Demonstrated

### ofxVlc4
- `init()`, `close()`
- `addPathToPlaylist()`, `clearPlaylist()`, `playIndex()`
- `play()`, `pause()`, `stop()`
- `setTime()`, `getTime()`, `nextFrame()`, `jumpTime()`
- `setPlaybackRate()`
- `setVolume()`, `getVolume()`
- `draw()`, `getTexture()`
- `readAudioIntoBuffer()`
- `formatCurrentPlaybackTimecode()`
- `setWatchTimeEnabled()`, `setWatchTimeMinPeriodUs()`
- `getMediaReadinessInfo()`, `getVideoStateInfo()`
- `setAudioCaptureEnabled()`
- `recordAudioVideo()`, `stopRecordingSession()`

### ofxLineaDeTiempo
- `ofxLineaDeTiempo(name)` — constructor
- `add(ofxPanel)` — add parameter group to timeline
- `generateView()`, `destroyView()`, `hasView()`
- `setShape()` — position/size the timeline
- `draw()` — render the timeline
- `getTimeControl()` — access play/stop/in/out/total-time

See also:
- [../README.md](../README.md)
- [../CHANGELOG.md](../CHANGELOG.md)
- [../ofxVlc4EditorExample/](../ofxVlc4EditorExample/) — Simpler cut-only editor
