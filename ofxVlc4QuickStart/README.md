# ofxVlc4 Quick Start Example

The **simplest possible** ofxVlc4 video player in under 100 lines of code.

Perfect for:
- Learning ofxVlc4 basics
- Quick prototyping
- Copy-paste starting point

## Features

- Drag-and-drop video playback
- Basic transport controls (play/pause)
- Volume control
- Fullscreen toggle
- Time display overlay

## Controls

- **Drag & Drop** - Load a video file
- **SPACE** - Play/Pause
- **f** - Toggle fullscreen
- **UP/DOWN** - Volume control

## Building

### Using openFrameworks Project Generator

1. Open the Project Generator
2. Import this folder
3. Generate project
4. Build and run

### Manual Build

```bash
make Release
```

## Code Overview

This example demonstrates the **absolute minimum** needed for video playback:

```cpp
// Setup
player.init(0, nullptr);

// Load media
player.addPathToPlaylist("video.mp4");
player.playIndex(0);

// Update loop
player.update();

// Render
player.draw(0, 0, width, height);
```

That's it! The full example adds:
- Drag-and-drop file loading
- Keyboard controls
- Time display overlay

Total: **~90 lines** of actual code

## Next Steps

After trying this example, explore:

- **ofxVlc4MidiExample** - MIDI synchronization
- **ofxVlc4360Example** - 360° video playback
- **ofxVlc4RecorderExample** - Recording workflows
- **ofxVlc4Example** - Full-featured GUI player
- **ofxVlc4NleEditor** - Non-linear editing

## API Used

This minimal example uses only these core methods:

**Lifecycle**:
- `init(argc, argv)`
- `update()`

**Media Loading**:
- `addPathToPlaylist(path)`
- `playIndex(index)`
- `clearPlaylist()`

**Playback Control**:
- `togglePlayPause()`
- `isPlaying()`
- `isPaused()`

**Rendering**:
- `draw(x, y, width, height)`

**State**:
- `isMediaAttached()`
- `getWatchTimeInfo()`

**Audio**:
- `setVolume(level)`
- `getVolume()`

**Configuration** (optional):
- `setWatchTimeEnabled(enabled)`
- `setWatchTimeMinPeriodUs(microseconds)`

## See Also

- [Main README](../README.md) - Full addon documentation
- [API Guide](../docs/API_GUIDE.md) - Complete API reference
- [CONTRIBUTING](../CONTRIBUTING.md) - Development guidelines
