# ofxVlc4 Architecture

This document describes the internal architecture of the **ofxVlc4** addon
for [openFrameworks](https://openframeworks.cc/).  It is intended for
contributors and anyone who needs to understand how the pieces fit together.

---

## High-Level Overview

```
┌──────────────────────────────────────────────────────────┐
│                      ofxVlc4  (public API)               │
│  ofxVlc4.h  ─  single class with ~400 public methods     │
├──────────┬──────────┬───────────┬────────────────────────┤
│  Audio   │  Video   │  Media    │  Playback / Recording  │
│Component │Component │Component  │  Controller / Recorder │
├──────────┴──────────┴───────────┴────────────────────────┤
│               VlcCoreSession                             │
│   (single source of truth for all libVLC handles)        │
├──────────────────────────────────────────────────────────┤
│               libvlc  (VLC 4.x C API)                   │
└──────────────────────────────────────────────────────────┘
```

---

## Component Model

Every public `ofxVlc4::someMethod()` delegates to one of the internal
*component* classes.  Components never call each other directly; they always
route through the owning `ofxVlc4` instance.

| Component              | Header / Source                     | Responsibility |
|------------------------|-------------------------------------|----------------|
| **AudioComponent**     | `src/audio/ofxVlc4Audio.h/.cpp`     | Volume, EQ, audio capture, ring buffer, filters, output devices |
| **VideoComponent**     | `src/video/ofxVlc4Video.h/.cpp`     | Texture rendering, FBO management, deinterlace, aspect ratio, viewpoint, D3D11, shaders |
| **MediaComponent**     | `src/media/ofxVlc4Media.h/.cpp`     | Media loading, parsing, metadata, events, discovery, dialogs, watch time, logging |
| **PlaybackController** | `src/playback/PlaybackController.h` | Playlist advancement, playback mode (repeat/loop/shuffle), deferred actions |
| **ofxVlc4Recorder**    | `src/recording/ofxVlc4Recorder.h`   | Texture/window recording, audio WAV capture, muxing pipeline |
| **MediaLibrary**       | `src/media/MediaLibrary.cpp`        | Playlist state, metadata caching, file scanning |

### How a Public Method is Added

1. Declare the method in the `public:` section of `src/core/ofxVlc4.h`.
2. Add a matching method to the appropriate component header (e.g.
   `AudioComponent`).
3. Implement the component method in the component `.cpp` file.
4. Add a one-line forwarding wrapper in the `ofxVlc4` top-level `.cpp`
   (e.g. `ofxVlc4Audio.cpp` contains `void ofxVlc4::setVolume(int v) {
   audioComponent->setVolume(v); }`).

---

## PIMPL and Runtime State

`ofxVlc4` uses the PIMPL idiom.  The only member on the public class is
`std::unique_ptr<Impl> m_impl` declared in `src/core/ofxVlc4Impl.h`.

`Impl` is a plain aggregate of ~20 named *runtime-state* structs:

| Struct                           | Purpose |
|----------------------------------|---------|
| `SubsystemRuntimeState`         | Owns the component and session unique_ptrs |
| `SynchronizationRuntimeState`   | All mutexes (video, audio, dialog, …) |
| `LifecycleRuntimeState`         | `shuttingDown` flag checked by every VLC callback |
| `PlayerConfigRuntimeState`      | Sample rate, channel count, filter chains, subtitle settings |
| `AudioRuntimeState`             | Atomic counters, volume, ring buffer |
| `VideoGeometryRuntimeState`     | Render/source dimensions, aspect ratio |
| `VideoResourceRuntimeState`     | GL objects, FBO, textures, D3D11 handles |
| `VideoFrameRuntimeState`        | Frame-received flags, fence sync |
| `EffectsRuntimeState`           | EQ bands, video adjust values |
| `MediaRuntimeState`             | Parse status, thumbnail, snapshot |
| `WatchTimeRuntimeState`         | High-resolution playback clock |
| `RecordingMuxRuntimeState`      | Async mux worker thread |
| `BookmarkState`                 | Per-media bookmark map |
| `MediaLibraryState`             | Playlist entries and metadata cache |
| …                               | See `ofxVlc4Impl.h` for the full list |

### Accessing State from VLC Callbacks

All VLC callbacks are `static` functions receiving `void * data`.
Event/dialog callback registration is routed through `VlcEventRouter`, which
uses router userdata and forwards to `ofxVlc4` callback handlers through the
`ControlBlock` ownership guard. Callback implementations must access state
through `m_impl->` (e.g. `owner->m_impl->synchronizationRuntime.dialogMutex`).
Direct member access like `owner->dialogMutex` is incorrect because those
fields live inside the PIMPL struct.

Every callback **must** check `m_impl->lifecycleRuntime.shuttingDown` before
dereferencing component pointers, to prevent use-after-free during `close()`.

---

## VlcCoreSession

`VlcCoreSession` (`src/core/VlcCoreSession.h`) is the **single source of
truth** for all raw libVLC handles:

- `libvlc_instance_t *` — the VLC instance
- `libvlc_media_t *` — the currently attached media
- `libvlc_media_player_t *` — the media player
- Event managers for player, media, discoverer, renderer
- Discoverer and renderer handles
- Log configuration and log entry buffer

Components obtain handles through accessor methods on
`m_impl->subsystemRuntime.coreSession`.

---

## Pure-Logic Helper Headers

Testable, dependency-free helpers are extracted into `src/support/`:

| Header                        | Contains |
|-------------------------------|----------|
| `ofxVlc4Utils.h`             | String trimming, GL helpers, `readTextFileIfPresent` |
| `ofxVlc4MuxHelpers.h`        | File URI, sout-path normalisation, recording-file operations |
| `ofxVlc4AudioHelpers.h`      | Audio format labels, normalisers, FFT, EQ serialisation |
| `ofxVlc4VideoHelpers.h`      | Deinterlace/aspect/crop labels, RGB clamping |
| `ofxVlc4MediaHelpers.h`      | FOURCC, bitrate/frame-rate formatting, WAV header |
| `ofxVlc4RecordingHelpers.h`  | Recording path building, `isValidSoutModuleName` |
| `ofxVlc4MediaLibraryHelpers.h` | Extension normalisation, metadata helpers |
| `ofxVlc4GlOps.h`             | Fence sync, FBO, texture filtering, PBO readback |
| `ofxVlc4RingBuffer.h/.cpp`   | Lock-free audio ring buffer |

These headers are covered by dedicated test binaries in `tests/`.

---

## Threading Model

| Thread               | Created by          | Guarded by |
|----------------------|---------------------|------------|
| **Main/GL**          | openFrameworks      | — |
| **VLC decoder**      | libvlc              | `videoMutex`, `audioMutex` |
| **VLC audio**        | libvlc              | `audioMutex`, atomic ring buffer |
| **VLC events**       | libvlc              | Various mutexes per state group |
| **Mux worker**       | `ofxVlc4Recorder`   | `RecordingMuxRuntimeState::mutex` |
| **Recorder encode**  | `ofxVlc4Recorder`   | Recorder internal state |

Mutexes are per-concern (not a single global lock) and live in
`SynchronizationRuntimeState`.

---

## Recording Pipeline

```
ofTexture/Window  →  ofxVlc4Recorder (PBO readback + encode)
                        ↓ video.ts
audioPlay callback →  WAV writer
                        ↓ audio.wav
                     muxRecordingFilesInternal (sout transcode + standard mux)
                        ↓ output.mp4
```

The mux step runs on a dedicated worker thread.  Sout module names
(`containerMux`, `audioCodec`) are validated as alphanumeric-only via
`isValidSoutModuleName()` to prevent sout-string injection.

---

## D3D11 Video Output (Windows)

On Windows, an alternative `D3D11Metadata` video output backend creates a
D3D11 device and render target for HDR metadata passthrough.  The five raw
COM pointers (`ID3D11Device *`, `ID3D11DeviceContext *`,
`ID3D10Multithread *`, `ID3D11Texture2D *`, `ID3D11RenderTargetView *`)
are stored in `VideoResourceRuntimeState` and explicitly released in
`VideoComponent::releaseD3D11Resources()`.

---

## Test Infrastructure

Tests live in `tests/` and build with CMake (C++17):

```bash
mkdir -p /tmp/test-build && cd /tmp/test-build
cmake /path/to/ofxVlc4/tests && make && ctest
```

Stubs for OF, GLFW, and VLC types are in `tests/stubs/` and
`tests/stubs_gl/`.  Each test binary is self-contained with a minimal
test harness (`CHECK` / `CHECK_EQ` macros).

---

## Directory Layout

```
src/
  core/           ofxVlc4.h, ofxVlc4Impl.h, ofxVlc4Types.h, VlcCoreSession
  audio/          AudioComponent
  video/          VideoComponent
  media/          MediaComponent, MediaLibrary
  playback/       PlaybackController
  recording/      ofxVlc4Recorder
  midi/           MIDI analysis, bridge, playback
  support/        Pure-logic helpers and utilities
tests/            CMake-based test binaries + stubs
scripts/          install-libvlc.sh (macOS + Windows)
docs/             This file and screenshots
libs/libvlc/      Downloaded VLC headers and runtime (not committed)
```
