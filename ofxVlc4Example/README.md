# ofxVlc4Example

This is the full example app for:

- `ofxVlc4`
- `ofxProjectM`
- `ofxImGui`

It is meant as a real integration example, not just a minimal playback demo. The app combines playlist playback, diagnostics, alternate video backends, detached GUI panels, video preview windows, and `projectM`-based visualization.

For visualization, this example treats `ofxProjectM` as the primary integrated path. VLC's built-in visualization plugins can still exist in the bundled runtime, but they are better treated as optional libVLC extras than as the main example workflow.

## What it shows

- `libVLC` playback through the OF texture backend
- optional Windows-specific alternate video backends
- optional Windows decoder-hardware preference (`Auto`, `D3D11`, `DXVA2`, `NVDEC`, `None`)
- playlist management and transport
- compact top-level menus for playlist, `projectM`, equalizer, visualizer, and advanced controls
- detached draggable menu/submenu windows
- video preview and `projectM` preview/display windows
- `projectM` integration with texture input from image or video
- startup/readiness diagnostics driven by the newer state getters
- renderer discovery, snapshot/native record status, filter lists, and EQ preset tooling

## Main files

- `src/ofApp.*`
  - app lifecycle and integration wiring
- `src/ofVlcPlayer4Gui.*`
  - main GUI shell and layout
- `src/ofVlcPlayer4GuiAudio.*`
- `src/ofVlcPlayer4GuiMedia.*`
- `src/ofVlcPlayer4GuiVideo.*`
- `src/ofVlcPlayer4GuiVisualizer.*`
- `src/ofVlcPlayer4GuiEqualizer.*`
- `src/ofVlcPlayer4GuiWindows.*`
- `src/ofVlcPlayer4GuiControls.h`
  - compact menu/submenu + detached-window helpers
- `src/ofVlcPlayer4GuiStyle.h`
  - shared GUI palette and spacing tokens
- `src/SimpleSrtSubtitleParser.h`
  - lightweight `.srt` cue parsing for the optional OF-drawn subtitle overlay

## Setup

Install the addon dependencies first:

- see [../README.md](../README.md)
- see [../CHANGELOG.md](../CHANGELOG.md)
- see [../../ofxProjectM/README.md](../../ofxProjectM/README.md)

In practice this means:

- install `libVLC` into `ofxVlc4/libs/libvlc`
- build/install bundled `projectM` libs into `ofxProjectM/libs/projectM`
- install `ofxImGui` on its `develop` branch
- add preset and texture assets if you want the `projectM` parts to be useful

For the `projectM` assets, use:

```bash
bash ofxVlc4Example/scripts/download-projectm-assets.sh
```

This example does not ship a sample movie in `bin/data`. On startup it will first look for `finger.mp4` / `fingers.mp4` in its own `bin/data`, then fall back to the standard openFrameworks sample video in `examples/video/videoPlayerExample/bin/data/movies/fingers.mp4` when that file exists.

## GUI overview

The main GUI contains:

- `Media Playlist`
- `projectM`
- `Equalizer`
- `Visualizer`
- `Advanced`

Most sections use compact submenus. Menus and submenus can be dragged out into separate windows. When a detached window is closed, it returns collapsed to the main GUI.

## projectM assets

For the `ofxProjectM`-powered parts of this example, these packs are recommended:

- [presets-cream-of-the-crop](https://github.com/projectM-visualizer/presets-cream-of-the-crop)
- [presets-milkdrop-texture-pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack)

Install both with:

```bash
bash scripts/download-projectm-assets.sh
```

Useful variants:

```bash
bash scripts/download-projectm-assets.sh --presets
bash scripts/download-projectm-assets.sh --textures
```

The script installs into:

- `bin/data/presets`
- `bin/data/textures`

Manual layout if you prefer:

- presets from `presets-cream-of-the-crop` go in the `presets` folder
- textures from `presets-milkdrop-texture-pack` go in the `textures` folder

Inside `Advanced` the example now also exposes:

- audio routing, devices, callback capture settings, filters, sync, and diagnostics
- media info, tracks, subtitles, navigation, bookmarks, renderer/discovery, and capture/record status
- a dedicated `DVD / Disc` section for title, chapter, program, and menu navigation, while teletext lives in `Tracks & Subtitles`
- video output, filters, adjustments, 3D/stereo options, crop/aspect/fit, and backend state
- decoder-hardware selection next to `Video Output`, with NVDEC/DXVA2/D3D11 choices applied on the next init
- live audio callback timing counters so conversion/copy costs can be checked under load
- subtitles still render through libVLC's normal overlay path for built-in and attached subtitle tracks
- the same `Tracks & Subtitles` section now also exposes VLC subtitle-renderer controls for renderer choice, font family, color, opacity, and bold styling; these apply on the next init
- the example also has an optional custom `.srt` overlay path that parses cue text in C++ and draws it inside OF with selectable TTF fonts
- the `Tracks & Subtitles` section also includes a quick `Load Subtitle...` picker for attaching an external subtitle file through the existing media-slave path
- the same section now also includes `Load Custom SRT...`, `Disable Custom SRT`, and a font picker for the OF-drawn subtitle overlay
- `.srt`, `.ass`, `.ssa`, `.vtt`, `.sub`, and `.idx` files are treated as external subtitles through the picker, playlist/path entry flow, and drag/drop; other files still go through the normal playlist path

Outside `Advanced` there are dedicated top-level sections for:

- equalizer presets, editable band curve, import/export helpers, and preset matching
- `projectM` runtime, preset playlist, and texture/debug controls
- visualizer controls and output scaling
- playlist ordering, path entry, and drag/drop management

## Displays

There are separate display windows for:

- `Video Display`
- `projectM Display`

They use the same framed display style as the main GUI and are intended for inspection rather than as a polished end-user player UI.

Both display windows are fixed-size preview windows and are separate from the fullscreen/output backend paths.

## Diagnostics

This example is also the main diagnostics surface for the addon. It uses the newer snapshot getters instead of scattering many direct live queries through the GUI:

- `getMediaReadinessInfo()`
- `getPlaybackStateInfo()`
- `getAudioStateInfo()`
- `getVideoStateInfo()`
- `getRendererStateInfo()`
- `getSubtitleStateInfo()`
- `getNavigationStateInfo()`

A small hidden startup overlay can be toggled with `F9`. It shows the early startup phases:

- attached
- prepared
- geometry
- frame
- playing

That is useful when checking startup timing, preview readiness, and texture/display handoff.

## Workflow Notes

The example intentionally follows the safer addon workflow:

- keep heavy track/title/program/subtitle refreshes out of the libVLC callback thread
- let the addon own the core `update()`/state-sync path
- read readiness/state from the snapshot getters in GUI/app code
- use the texture path first, then alternate backends only when needed
- close playback through the normal `player.close()` path

This makes startup, backend switching, and shutdown much more predictable than trying to do more work directly inside libVLC callbacks.

## Notes

- The example is Windows-heavy because it demonstrates `HWND` and D3D11-specific paths in addition to the normal OF texture path.
- The default and most integrated path is still the texture-based OF backend.
- Effects note:
  - real-time video adjustments stay safest on the texture backend, where the addon uses its fallback path
  - libVLC video-filter chains such as `sepia`, `mirror`, `rotate`, `sharpen`, `hqdn3d`, and `invert` currently require the `NativeWindow` backend to be applied live
  - on the texture backend, the selected video-filter chain is still shown and stored, but it is not pushed into libVLC because that path has proven crash-prone
- Backend summary:
  - `Texture` is the default preview/integration path and the best fit for OF rendering plus safer live adjustments
  - `NativeWindow` is the path for libVLC-native video filters/effects
  - `D3D11` is an advanced Windows backend, not the normal day-to-day path
- The example now builds as a Windows subsystem app, so it should not open an extra console window.
- For the smallest MIDI-capable starting point, see [../ofxVlc4MidiExample/README.md](../ofxVlc4MidiExample/README.md).
