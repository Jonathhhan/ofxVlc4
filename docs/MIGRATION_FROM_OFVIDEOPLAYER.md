# Migrating from ofVideoPlayer to ofxVlc4

This guide helps you transition from openFrameworks' built-in `ofVideoPlayer` to the more feature-rich `ofxVlc4` addon.

## Table of Contents

- [Why Migrate?](#why-migrate)
- [Quick Comparison](#quick-comparison)
- [Basic Migration](#basic-migration)
- [API Mapping](#api-mapping)
- [Common Patterns](#common-patterns)
- [New Capabilities](#new-capabilities)
- [Performance Considerations](#performance-considerations)
- [Troubleshooting](#troubleshooting)

## Why Migrate?

**ofxVlc4 advantages over ofVideoPlayer**:

✅ **More codecs and formats** - VLC supports virtually all media formats
✅ **Advanced features** - Recording, 360° video, MIDI sync, NLE editing
✅ **Better playback control** - Precise seeking, rate control, watch-time API
✅ **Hardware decoding** - GPU-accelerated decoding on all platforms
✅ **Network streaming** - HTTP, RTSP, and other network protocols
✅ **Subtitle support** - External subtitle files, track selection
✅ **Audio visualization** - ProjectM integration
✅ **Cross-platform consistency** - Same backend on all platforms

**When to stick with ofVideoPlayer**:

- Simple playback needs with common formats (MP4, MOV)
- Minimal dependencies preferred
- Lower learning curve needed
- Mobile platforms (iOS/Android) where VLC may be restricted

## Quick Comparison

| Feature | ofVideoPlayer | ofxVlc4 |
|---------|--------------|---------|
| **Formats** | Limited (platform-dependent) | Extensive (VLC codec library) |
| **Seeking** | Basic | Frame-accurate with watch-time API |
| **Playback Rate** | Limited | Variable speed (-8x to +8x) |
| **Recording** | No | Yes (with presets) |
| **Subtitles** | No | Yes (internal + external) |
| **360° Video** | No | Yes (multiple projection modes) |
| **MIDI Sync** | No | Yes |
| **Network Streams** | Limited | Full support |
| **API Size** | ~20 methods | ~400 methods (organized) |
| **Dependencies** | Built-in | Requires libVLC 4 |

## Basic Migration

### Installation

1. Clone ofxVlc4 to your `addons/` folder:
   ```bash
   cd addons
   git clone https://github.com/Jonathhhan/ofxVlc4.git
   ```

2. Install libVLC:
   ```bash
   cd ofxVlc4
   bash scripts/install-libvlc.sh
   ```

3. Add to your project:
   - **Project Generator**: Add `ofxVlc4` to addons
   - **Manual**: Update `addons.make` with `ofxVlc4`

### Minimal Migration Example

**Before (ofVideoPlayer)**:
```cpp
// ofApp.h
class ofApp : public ofBaseApp {
public:
    void setup();
    void update();
    void draw();

    ofVideoPlayer video;
};

// ofApp.cpp
void ofApp::setup() {
    video.load("movie.mp4");
    video.play();
}

void ofApp::update() {
    video.update();
}

void ofApp::draw() {
    video.draw(0, 0, ofGetWidth(), ofGetHeight());
}
```

**After (ofxVlc4)**:
```cpp
// ofApp.h
#include "ofxVlc4.h"  // Add header

class ofApp : public ofBaseApp {
public:
    void setup();
    void update();
    void draw();

    ofxVlc4 player;  // Changed type
};

// ofApp.cpp
void ofApp::setup() {
    player.init(0, nullptr);  // Initialize once
    player.addPathToPlaylist("movie.mp4");
    player.playIndex(0);
}

void ofApp::update() {
    player.update();  // Same
}

void ofApp::draw() {
    player.draw(0, 0, ofGetWidth(), ofGetHeight());  // Same
}
```

**Key differences**:
- Call `init()` once before using the player
- Use `addPathToPlaylist()` + `playIndex()` instead of `load()`
- Everything else is nearly identical for basic use

## API Mapping

### Loading & Playback

| ofVideoPlayer | ofxVlc4 | Notes |
|---------------|---------|-------|
| `load(path)` | `addPathToPlaylist(path)` + `playIndex(0)` | ofxVlc4 uses playlist model |
| `play()` | `play()` | ✅ Same |
| `stop()` | `stop()` | ✅ Same |
| `setPaused(bool)` | `pause()` or `play()` | Separate methods |
| `isPlaying()` | `isPlaying()` | ✅ Same |
| `isPaused()` | `isPaused()` | ✅ Same |
| `close()` | `close()` | ✅ Same |

### Seeking & Time

| ofVideoPlayer | ofxVlc4 | Notes |
|---------------|---------|-------|
| `setPosition(0.0-1.0)` | `setPosition(0.0-1.0)` | ✅ Same |
| `getPosition()` | `getPosition()` | ✅ Same |
| `getDuration()` | `getLength()` | Different name, same purpose |
| `getCurrentFrame()` | `getWatchTimeInfo().timeUs` | High-resolution timing |
| `getTotalNumFrames()` | Calculate from length + FPS | Not directly available |
| `setFrame(int)` | `setTime(ms)` | Time-based instead of frame-based |

### Rendering

| ofVideoPlayer | ofxVlc4 | Notes |
|---------------|---------|-------|
| `draw(x,y,w,h)` | `draw(x,y,w,h)` | ✅ Same |
| `getTexture()` | `getTexture()` | ✅ Same |
| `getPixels()` | `getTexture().readToPixels()` | Similar |
| `setAnchorPercent(x,y)` | Manual GL transform | Not built-in |
| `setAnchorPoint(x,y)` | Manual GL transform | Not built-in |

### Properties

| ofVideoPlayer | ofxVlc4 | Notes |
|---------------|---------|-------|
| `getWidth()` | `getVideoStateInfo().renderWidth` | Via state struct |
| `getHeight()` | `getVideoStateInfo().renderHeight` | Via state struct |
| `isLoaded()` | `isMediaAttached()` | Different name |
| `getSpeed()` | `getRate()` | ✅ Similar |
| `setSpeed(float)` | `setRate(float)` | ✅ Similar |
| `setVolume(0.0-1.0)` | `setVolume(0-100)` | Different scale (0-100) |

### Audio

| ofVideoPlayer | ofxVlc4 | Notes |
|---------------|---------|-------|
| `setVolume(float)` | `setVolume(int)` | 0-100 instead of 0.0-1.0 |
| N/A | `toggleMute()` | ✅ New feature |
| N/A | `setAudioOutputDevice()` | ✅ New feature |
| N/A | `readAudioIntoBuffer()` | ✅ New feature |
| N/A | Equalizer API | ✅ New feature |

### Looping

| ofVideoPlayer | ofxVlc4 | Notes |
|---------------|---------|-------|
| `setLoopState(OF_LOOP_NORMAL)` | `setPlaybackMode(PlaybackMode::RepeatAll)` | Different API |
| `setLoopState(OF_LOOP_PALINDROME)` | Not available | |
| `setLoopState(OF_LOOP_NONE)` | `setPlaybackMode(PlaybackMode::Default)` | |
| `getLoopState()` | `getPlaybackMode()` | |

## Common Patterns

### Pattern 1: Simple Loop Player

**ofVideoPlayer**:
```cpp
void ofApp::setup() {
    video.load("loop.mp4");
    video.setLoopState(OF_LOOP_NORMAL);
    video.play();
}
```

**ofxVlc4**:
```cpp
void ofApp::setup() {
    player.init(0, nullptr);
    player.addPathToPlaylist("loop.mp4");
    player.setPlaybackMode(PlaybackMode::RepeatAll);
    player.playIndex(0);
}
```

### Pattern 2: Volume Control

**ofVideoPlayer**:
```cpp
void ofApp::keyPressed(int key) {
    if (key == OF_KEY_UP) {
        float vol = video.getVolume();
        video.setVolume(ofClamp(vol + 0.1f, 0.0f, 1.0f));
    }
}
```

**ofxVlc4**:
```cpp
void ofApp::keyPressed(int key) {
    if (key == OF_KEY_UP) {
        int vol = player.getVolume();
        player.setVolume(ofClamp(vol + 10, 0, 100));
    }
}
```

### Pattern 3: Seek on Click

**ofVideoPlayer**:
```cpp
void ofApp::mousePressed(int x, int y, int button) {
    float pos = (float)x / ofGetWidth();
    video.setPosition(pos);
}
```

**ofxVlc4**:
```cpp
void ofApp::mousePressed(int x, int y, int button) {
    float pos = (float)x / ofGetWidth();
    player.setPosition(pos);  // Same!
}
```

### Pattern 4: Frame-by-Frame

**ofVideoPlayer**:
```cpp
void ofApp::keyPressed(int key) {
    if (key == OF_KEY_RIGHT) {
        video.nextFrame();
    }
    if (key == OF_KEY_LEFT) {
        video.previousFrame();
    }
}
```

**ofxVlc4**:
```cpp
void ofApp::keyPressed(int key) {
    if (key == OF_KEY_RIGHT) {
        player.nextFrame();  // Same!
    }
    if (key == OF_KEY_LEFT) {
        // Not available - use small time jumps instead
        player.jumpTime(-33);  // ~1 frame at 30fps
    }
}
```

## New Capabilities

Once migrated, you can leverage ofxVlc4's advanced features:

### 1. High-Resolution Timing

```cpp
// Enable watch-time API
player.setWatchTimeEnabled(true);
player.setWatchTimeMinPeriodUs(50000);  // 50ms updates

// Get precise timing
auto watchTime = player.getWatchTimeInfo();
if (watchTime.available) {
    int64_t currentUs = watchTime.timeUs;
    int64_t lengthUs = watchTime.lengthUs;
    double position = watchTime.position;
}
```

### 2. Subtitles

```cpp
// Load external subtitle file
player.addSubtitleSlave("movie.srt");

// Select subtitle track
auto tracks = player.getSubtitleTracks();
if (!tracks.empty()) {
    player.setSubtitleTrack(tracks[0].id);
}
```

### 3. Recording

```cpp
// Start recording current playback
player.startTextureRecordingSession(
    "output.mp4",
    ofxVlc4RecordingPreset::makeH264_1080p_30fps()
);

// Stop when done
player.stopRecordingSession();
```

### 4. Playlist Management

```cpp
// Build a playlist
player.addPathToPlaylist("video1.mp4");
player.addPathToPlaylist("video2.mp4");
player.addPathToPlaylist("video3.mp4");

// Auto-advance through playlist
player.setPlaybackMode(PlaybackMode::RepeatAll);
player.playIndex(0);

// Navigate
player.nextMediaListItem();
player.previousMediaListItem();
```

### 5. Network Streaming

```cpp
// Play HTTP stream
player.addPathToPlaylist("http://example.com/stream.m3u8");
player.playIndex(0);

// Play RTSP camera
player.addPathToPlaylist("rtsp://camera.local/stream");
player.playIndex(0);
```

## Performance Considerations

### Memory Usage

- **ofVideoPlayer**: Lower baseline memory
- **ofxVlc4**: Higher baseline (~50-100MB) due to VLC runtime

**Recommendation**: For mobile or memory-constrained devices, ofVideoPlayer may be more appropriate.

### Hardware Decoding

ofxVlc4 supports hardware decoding on all platforms:

```cpp
// Enable hardware decoding (Windows)
player.setPreferredDecoderDevice(PreferredDecoderDevice::D3D11);

// Or let VLC choose automatically (recommended)
player.setPreferredDecoderDevice(PreferredDecoderDevice::Auto);
```

### Startup Time

- **ofVideoPlayer**: Faster startup
- **ofxVlc4**: Slightly slower due to VLC initialization

**Recommendation**: Call `player.init()` early in `setup()` to minimize impact.

## Troubleshooting

### Common Migration Issues

#### Issue: "Video plays but no audio"

**Solution**: Enable audio capture before init:
```cpp
player.setAudioCaptureEnabled(true);  // Before init()
player.init(0, nullptr);
```

#### Issue: "First frame is black"

**Solution**: Wait for media readiness:
```cpp
if (player.getMediaReadinessInfo().frameReceived) {
    player.draw(0, 0, width, height);
}
```

#### Issue: "Seeking is imprecise"

**Solution**: Use watch-time API for precise seeking:
```cpp
player.setWatchTimeEnabled(true);
int64_t targetMs = 30000;  // 30 seconds
player.setTime(targetMs);
```

#### Issue: "Video file won't load"

**Solution**: Check file path is absolute:
```cpp
// Convert relative to absolute
std::string absPath = ofToDataPath("movie.mp4", true);
player.addPathToPlaylist(absPath);
```

#### Issue: "Crash on exit"

**Solution**: Call `close()` in ofApp destructor or exit():
```cpp
void ofApp::exit() {
    player.close();
}
```

### Getting Help

- **Documentation**: See [docs/API_GUIDE.md](API_GUIDE.md)
- **Examples**: Check `ofxVlc4QuickStart` for minimal setup
- **Issues**: Report bugs on [GitHub](https://github.com/Jonathhhan/ofxVlc4/issues)

## Summary Checklist

When migrating, remember to:

- [ ] Install libVLC with `scripts/install-libvlc.sh`
- [ ] Add `#include "ofxVlc4.h"` to your header
- [ ] Change `ofVideoPlayer` to `ofxVlc4`
- [ ] Call `player.init(0, nullptr)` in setup
- [ ] Replace `load()` with `addPathToPlaylist()` + `playIndex(0)`
- [ ] Update volume range from 0.0-1.0 to 0-100
- [ ] Enable audio capture if needed: `setAudioCaptureEnabled(true)`
- [ ] Call `player.close()` in exit or destructor
- [ ] Test thoroughly with your specific media files

## Next Steps

After successful migration:

1. Explore advanced features (recording, subtitles, MIDI sync)
2. Review [docs/API_GUIDE.md](API_GUIDE.md) for full API reference
3. Check performance guide: [docs/RECORDING_PERFORMANCE.md](RECORDING_PERFORMANCE.md)
4. Try advanced examples (360°, NLE editor, recorder)

---

**Need help?** See [CONTRIBUTING.md](../CONTRIBUTING.md) for development guidelines or open a GitHub issue.
