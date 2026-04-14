# ofxVlc4 API Facades

This directory contains simplified facade classes that wrap the full ofxVlc4 API for common use cases.

## Purpose

The full ofxVlc4 class provides ~400 methods organized into 12 subsystems. While powerful, this can be overwhelming for simple playback or recording needs.

The facade classes provide:
- **Simplified APIs** (~15 methods vs ~400)
- **Sensible defaults** for common workflows
- **Easier learning curve** for beginners
- **Access to full API** when needed via `getPlayer()`

## Available Facades

### ofxVlc4SimplePlayer

Simplified playback interface with essential methods:
- `load(path)` - Load and prepare media
- `play()`, `pause()`, `stop()` - Transport control
- `setPosition(pos)`, `getPosition()` - Seeking
- `setVolume(vol)`, `getVolume()` - Audio control
- `update()`, `draw(x,y,w,h)` - Rendering

**Example:**
```cpp
ofxVlc4SimplePlayer player;

void ofApp::setup() {
    player.load("movie.mp4");
    player.play();
}

void ofApp::update() {
    player.update();
}

void ofApp::draw() {
    player.draw(0, 0, ofGetWidth(), ofGetHeight());
}
```

### ofxVlc4SimpleRecorder

Simplified recording interface with quality presets:
- `startRecording(path, w, h, quality)` - Start texture recording
- `startWindowRecording(path, quality)` - Start window recording
- `recordFrame(texture)` - Record frame
- `stopRecording()` - Finalize file
- Quality presets: `Low`, `Medium`, `High`, `Ultra`

**Example:**
```cpp
ofxVlc4SimpleRecorder recorder;

void ofApp::keyPressed(int key) {
    if (key == 'r') {
        if (recorder.isRecording()) {
            recorder.stopRecording();
        } else {
            // Record at High quality (1080p 60fps, 10 Mbps)
            recorder.startRecording("output.mp4", 1920, 1080,
                                   ofxVlc4SimpleRecorder::Quality::High);
        }
    }
}

void ofApp::update() {
    if (recorder.isRecording()) {
        recorder.recordFrame(myTexture);
    }
}
```

## When to Use Facades vs Full API

### Use Facades When:
- ✅ Learning ofxVlc4 for the first time
- ✅ Building simple playback/recording applications
- ✅ Prototyping quickly
- ✅ You only need core functionality

### Use Full API When:
- 🎯 Advanced features needed (MIDI, NLE, 360°, subtitles)
- 🎯 Fine-grained control required
- 🎯 Custom codec/mux settings
- 🎯 Multiple simultaneous players
- 🎯 Professional applications

## Accessing Full API from Facades

All facades provide `getPlayer()` to access the underlying ofxVlc4 instance:

```cpp
ofxVlc4SimplePlayer player;

// Use simple API
player.load("movie.mp4");
player.play();

// Access full API when needed
player.getPlayer().addSubtitleSlave("subtitles.srt");
player.getPlayer().setPlaybackMode(PlaybackMode::RepeatAll);

auto watchTime = player.getPlayer().getWatchTimeInfo();
```

## Architecture

Facades use composition (not inheritance):
- Each facade owns an `ofxVlc4` instance
- All methods delegate to the wrapped player
- Lifecycle management handled automatically
- No virtual functions or polymorphism needed

## Documentation

- Full API docs: `../docs/API_GUIDE.md`
- Migration guide: `../docs/MIGRATION_FROM_OFVIDEOPLAYER.md`
- Examples: `../ofxVlc4QuickStart/`, `../ofxVlc4Example/`
