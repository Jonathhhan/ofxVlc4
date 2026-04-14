# ofxVlc4 API Organization Guide

This guide explains the logical organization of the ofxVlc4 API and common usage patterns.

## Overview

The `ofxVlc4` class provides approximately 400 public methods organized into logical subsystems. While all methods are currently on the main class, they follow clear organizational patterns that group related functionality.

## API Subsystems

The API is logically organized into the following subsystems:

---

### 1. Lifecycle & Initialization

**Purpose**: Core initialization, configuration, and shutdown

**Key Methods**:
```cpp
// Initialization
void init(int argc = 0, const char * const * argv = nullptr);
void close();

// Pre-init configuration
void setAudioCaptureEnabled(bool enabled);
void setPreferredDecoderDevice(PreferredDecoderDevice device);
void setSubtitleTextRenderer(SubtitleTextRenderer renderer);
void setExtraInitArgs(const std::vector<std::string> & args);
void addExtraInitArg(const std::string & arg);

// Version info
static ofxVlc4AddonVersionInfo getAddonVersionInfo();
```

**Usage Pattern**:
```cpp
ofxVlc4 player;

// Configure before init
player.setAudioCaptureEnabled(true);
player.setPreferredDecoderDevice(PreferredDecoderDevice::D3D11);

// Initialize
player.init(0, nullptr);

// ... use player ...

// Shutdown
player.close();
```

---

### 2. Media Loading & Playlist Management

**Purpose**: Load media, manage playlists, handle external content

**Key Methods**:
```cpp
// Media loading
void load(const std::string & path);
void addToPlaylist(const std::string & path);
void addPathToPlaylist(const std::string & path);

// Playlist control
void clearPlaylist();
void removePlaylistIndex(int index);
void movePlaylistItem(int fromIndex, int toIndex);
void activatePlaylistIndex(int index, bool shouldPlay = true);
int getCurrentPlaylistIndex() const;

// Playlist state
PlaylistStateInfo getPlaylistStateInfo() const;
std::vector<PlaylistItemInfo> getPlaylistItems() const;
PlaylistItemInfo getCurrentPlaylistItemInfo() const;

// External content
void addMediaSlave(MediaSlaveType type, const std::string & uri, int priority);
void addSubtitleSlave(const std::string & uri, int priority = 0);
void addAudioSlave(const std::string & uri, int priority = 0);
```

**Usage Pattern**:
```cpp
// Build playlist
player.addPathToPlaylist("video1.mp4");
player.addPathToPlaylist("video2.mp4");
player.addPathToPlaylist("video3.mp4");

// Start playback
player.activatePlaylistIndex(0, true);

// Add external subtitle
player.addSubtitleSlave("subtitles.srt");

// Navigate
player.nextMediaListItem();
player.previousMediaListItem();
```

---

### 3. Playback Control & Transport

**Purpose**: Control playback state, seeking, speed, and transport

**Key Methods**:
```cpp
// Basic transport
void play();
void pause();
void stop();
void togglePlayPause();

// Seeking
void setTime(int64_t timeMs);
void setPosition(float position);
int64_t getTime() const;
float getPosition() const;

// Speed control
void setRate(float rate);
float getRate() const;

// Playback mode
void setPlaybackMode(PlaybackMode mode);
PlaybackMode getPlaybackMode() const;

// Navigation
void nextMediaListItem();
void previousMediaListItem();

// State queries
bool isPlaying() const;
bool isPaused() const;
PlaybackStateInfo getPlaybackStateInfo() const;
```

**Usage Pattern**:
```cpp
// Start playback
player.play();

// Seek to 30 seconds
player.setTime(30000);

// Change speed
player.setRate(1.5f);  // 1.5x speed

// Enable repeat
player.setPlaybackMode(PlaybackMode::RepeatOne);
```

---

### 4. Audio Control

**Purpose**: Volume, audio devices, audio effects, capture

**Key Methods**:
```cpp
// Volume
void setVolume(int volume);  // 0-100
int getVolume() const;
void toggleMute();
bool isMuted() const;

// Audio devices
std::vector<AudioOutputDeviceInfo> getAudioOutputDevices();
void setAudioOutputDevice(const std::string & deviceId);

// Equalizer
void setEqualizerEnabled(bool enabled);
void setEqualizerPreamp(float preampDb);
void setEqualizerBandAmplitude(unsigned band, float amplitudeDb);
std::vector<EqualizerPresetInfo> getEqualizerPresets() const;
void applyEqualizerPreset(int presetIndex);

// Audio capture
void setAudioCaptureEnabled(bool enabled);
void enableAudioCapture();
void disableAudioCapture();
AudioStateInfo getAudioStateInfo() const;
```

**Usage Pattern**:
```cpp
// Set volume
player.setVolume(75);

// Enable equalizer with preset
player.setEqualizerEnabled(true);
player.applyEqualizerPreset(2);  // "Rock" preset

// Adjust bass
player.setEqualizerBandAmplitude(0, 6.0f);  // +6dB

// Enable audio capture for visualization
player.setAudioCaptureEnabled(true);
```

---

### 5. Video Control & Rendering

**Purpose**: Video output, adjustments, effects, texture access

**Key Methods**:
```cpp
// Rendering
void draw(float x, float y, float w, float h);
void update();
ofTexture & getTexture();
ofTexture & getRenderTexture();

// Video backend
void setVideoOutputBackend(VideoOutputBackend backend);
VideoOutputBackend getVideoOutputBackend() const;

// Video adjustments
void setVideoAdjustmentsEnabled(bool enabled);
void setVideoAdjustContrast(float contrast);
void setVideoAdjustBrightness(float brightness);
void setVideoAdjustHue(float hue);
void setVideoAdjustSaturation(float saturation);
void setVideoAdjustGamma(float gamma);

// Aspect/fit/crop
void setVideoAspectRatioMode(VideoAspectRatioMode mode);
void setVideoCropMode(VideoCropMode mode);
void setVideoDisplayFitMode(VideoDisplayFitMode mode);

// State
VideoStateInfo getVideoStateInfo() const;
int getVideoWidth() const;
int getVideoHeight() const;
```

**Usage Pattern**:
```cpp
void ofApp::update() {
    player.update();
}

void ofApp::draw() {
    // Simple rendering
    player.draw(0, 0, ofGetWidth(), ofGetHeight());

    // Or use texture directly
    ofTexture & tex = player.getTexture();
    tex.draw(0, 0);

    // Adjust video
    player.setVideoAdjustBrightness(1.2f);
    player.setVideoAdjustContrast(1.1f);
}
```

---

### 6. Recording & Capture

**Purpose**: Record video, audio, and window content

**Key Methods**:
```cpp
// Session-based recording (recommended)
void startRecordingSession(const RecordingSessionConfig & config);
void startTextureRecordingSession(
    ofTexture & texture,
    const std::string & name,
    const ofxVlc4RecordingPreset & preset
);
void startWindowRecordingSession(
    const std::string & name,
    const ofxVlc4RecordingPreset & preset
);
void stopRecordingSession();

// Recording presets
void setRecordingPreset(const ofxVlc4RecordingPreset & preset);
ofxVlc4RecordingPreset getRecordingPreset() const;

// State and performance
RecordingSessionState getRecordingSessionState() const;
ofxVlc4RecorderPerformanceInfo getRecorderPerformanceInfo() const;

// Legacy methods (still available)
void recordVideo(const std::string & path, int width, int height, int fps);
void recordAudio(const std::string & path);
void stopRecording();
```

**Usage Pattern**:
```cpp
// Configure preset
ofxVlc4RecordingPreset preset = ofxVlc4RecordingPreset::H264_MP4();
preset.width = 1920;
preset.height = 1080;
preset.fps = 30;
preset.videoBitrateKbps = 8000;

// Start recording texture
player.startTextureRecordingSession(myTexture, "my-recording", preset);

// ... record frames ...

// Stop and mux
player.stopRecordingSession();

// Check performance
auto perf = player.getRecorderPerformanceInfo();
if (perf.droppedFrameCount > 0) {
    ofLogWarning() << "Dropped " << perf.droppedFrameCount << " frames";
}
```

See [RECORDING_PERFORMANCE.md](RECORDING_PERFORMANCE.md) for detailed performance optimization.

---

### 7. MIDI Support

**Purpose**: MIDI file playback, analysis, and synchronization

**Key Methods**:
```cpp
// MIDI loading
void loadMidiFile(const std::string & path);

// MIDI transport
void playMidi();
void pauseMidi();
void stopMidi();
void seekMidi(double positionSeconds);

// MIDI state
MidiTransportInfo getMidiTransportInfo() const;
MidiAnalysisReport getMidiAnalysisReport() const;

// MIDI output
void setMidiMessageCallback(MidiMessageCallback callback);
void setMidiSyncSettings(const MidiSyncSettings & settings);
void setMidiSyncSource(MidiSyncSource source);
void setMidiSyncToWatchTimeEnabled(bool enabled);
```

**Usage Pattern**:
```cpp
// Load MIDI file
player.loadMidiFile("song.mid");

// Set up output callback
player.setMidiMessageCallback([](const MidiMessage & msg) {
    // Send to MIDI device
    midiOut.sendMessage(msg.bytes);
});

// Sync MIDI to video playback
player.setMidiSyncToWatchTimeEnabled(true);

// Start MIDI playback
player.playMidi();
```

---

### 8. Track & Subtitle Management

**Purpose**: Select audio/video/subtitle tracks, manage subtitles

**Key Methods**:
```cpp
// Track selection
void setAudioTrack(int trackId);
void setVideoTrack(int trackId);
void setSubtitleTrack(int trackId);

// Track queries
std::vector<MediaTrackInfo> getAudioTracks();
std::vector<MediaTrackInfo> getVideoTracks();
std::vector<MediaTrackInfo> getSubtitleTracks();

// Subtitle control
void setSubtitleEnabled(bool enabled);
void setSubtitleDelay(int64_t delayMicros);
int64_t getSubtitleDelay() const;

// Track state
SubtitleStateInfo getSubtitleStateInfo() const;
```

**Usage Pattern**:
```cpp
// Get available audio tracks
auto audioTracks = player.getAudioTracks();
for (const auto & track : audioTracks) {
    ofLogNotice() << track.name << " (" << track.language << ")";
}

// Select Japanese audio
player.setAudioTrack(audioTracks[1].id);

// Enable subtitles
player.setSubtitleEnabled(true);
player.setSubtitleDelay(250000);  // +250ms
```

---

### 9. DVD/Disc Navigation

**Purpose**: Navigate DVD menus, titles, chapters

**Key Methods**:
```cpp
// Title navigation
void setTitle(int titleIndex);
int getTitle() const;
int getTitleCount() const;

// Chapter navigation
void setChapter(int chapterIndex);
int getChapter() const;
int getChapterCount() const;
void nextChapter();
void previousChapter();

// Menu navigation
void navigateMenu(NavigationMenuAction action);

// State
NavigationStateInfo getNavigationStateInfo() const;
```

**Usage Pattern**:
```cpp
// Load DVD
player.load("dvd:///D:");

// Navigate to title 2, chapter 5
player.setTitle(2);
player.setChapter(5);

// Menu navigation
player.navigateMenu(NavigationMenuAction::Activate);
```

---

### 10. Watch Time & Timing

**Purpose**: High-resolution playback timing for sync and metrics

**Key Methods**:
```cpp
// Watch time configuration
void setWatchTimeEnabled(bool enabled);
void setWatchTimeMinPeriodUs(int64_t periodMicros);
void setWatchTimeCallback(WatchTimeCallback callback);

// Watch time state
WatchTimeInfo getWatchTimeInfo() const;

// Timecode formatting
std::string formatCurrentPlaybackTimecode() const;
std::string formatPlaybackTimecode(int64_t timeMs) const;
double getPlaybackClockFramesPerSecond() const;
```

**Usage Pattern**:
```cpp
// Enable high-resolution timing
player.setWatchTimeEnabled(true);
player.setWatchTimeMinPeriodUs(16667);  // ~60Hz

// Set callback for timing updates
player.setWatchTimeCallback([](const WatchTimeInfo & info) {
    // High-precision timing for sync
    double currentTimeSeconds = info.position;
    // ... sync external systems ...
});

// Format as timecode
std::string tc = player.formatCurrentPlaybackTimecode();
// Output: "00:01:23:15" (HH:MM:SS:FF)
```

---

### 11. Diagnostics & State

**Purpose**: Query media info, statistics, logs, state snapshots

**Key Methods**:
```cpp
// Readiness state
MediaReadinessInfo getMediaReadinessInfo() const;

// Media statistics
MediaStats getMediaStats() const;

// Logging
void setLogLevel(LogLevel level);
std::vector<LibVlcLogEntry> getLibVlcLogEntries(int maxEntries = 100);

// Help text
std::string getVlcHelp() const;
std::string getVlcFullHelp() const;

// Filter lists
std::vector<AudioFilterInfo> getAudioFilters();
std::vector<VideoFilterInfo> getVideoFilters();
```

**Usage Pattern**:
```cpp
// Check media readiness
auto readiness = player.getMediaReadinessInfo();
if (readiness.frameReceived) {
    ofLogNotice() << "First frame arrived";
}

// Get playback statistics
auto stats = player.getMediaStats();
ofLogNotice() << "Decoded frames: " << stats.decodedVideo;
ofLogNotice() << "Dropped frames: " << stats.lostPictures;

// Enable verbose logging
player.setLogLevel(LogLevel::Debug);
```

---

### 12. Advanced Features

**Purpose**: Renderer discovery, media discovery, dialogs, bookmarks

**Key Methods**:
```cpp
// Renderer/Casting
void startRendererDiscovery();
void stopRendererDiscovery();
std::vector<RendererItemEntry> getDiscoveredRenderers() const;
void setRenderer(const std::string & rendererId);

// Media discovery
void startMediaDiscovery(const std::string & discovererName);
void stopMediaDiscovery();
std::vector<DiscoveredMediaItemInfo> getDiscoveredMediaItems() const;

// Bookmarks
void addBookmark(const std::string & mediaPath, const BookmarkInfo & bookmark);
std::vector<BookmarkInfo> getBookmarks(const std::string & mediaPath) const;
void clearBookmarks(const std::string & mediaPath);

// Dialogs
void setDialogCallbacksEnabled(bool enabled);
void registerDialogErrorCallback(DialogErrorCallback callback);
```

---

## Common Usage Patterns

### Pattern 1: Basic Video Player

```cpp
class ofApp : public ofBaseApp {
    ofxVlc4 player;

    void setup() {
        player.init(0, nullptr);
        player.addPathToPlaylist("video.mp4");
        player.playIndex(0);
    }

    void update() {
        player.update();
    }

    void draw() {
        player.draw(0, 0, ofGetWidth(), ofGetHeight());
    }

    void exit() {
        player.close();
    }
};
```

### Pattern 2: Video Player with Recording

```cpp
void ofApp::setup() {
    player.init(0, nullptr);
    player.setWatchTimeEnabled(true);
    player.addPathToPlaylist("input.mp4");
    player.playIndex(0);
}

void ofApp::keyPressed(int key) {
    if (key == 'r') {
        // Start recording
        auto preset = ofxVlc4RecordingPreset::H264_MP4();
        player.startWindowRecordingSession("capture", preset);
    }
    if (key == 's') {
        // Stop recording
        player.stopRecordingSession();
    }
}
```

### Pattern 3: MIDI-Synced Playback

```cpp
void ofApp::setup() {
    player.init(0, nullptr);
    player.setWatchTimeEnabled(true);

    // Load video and MIDI
    player.addPathToPlaylist("video.mp4");
    player.loadMidiFile("song.mid");

    // Sync MIDI to video
    player.setMidiSyncToWatchTimeEnabled(true);

    // Route MIDI to device
    player.setMidiMessageCallback([this](const MidiMessage & msg) {
        midiOut.sendMessage(msg.bytes);
    });

    // Start both
    player.playIndex(0);
    player.playMidi();
}
```

### Pattern 4: Multi-Track Media with Subtitles

```cpp
void ofApp::setup() {
    player.init(0, nullptr);
    player.load("movie.mkv");

    // Add external subtitle
    player.addSubtitleSlave("subtitles.srt");

    // Configure audio track
    auto audioTracks = player.getAudioTracks();
    player.setAudioTrack(audioTracks[1].id);  // Select track 2

    // Enable subtitles
    player.setSubtitleEnabled(true);

    player.play();
}
```

---

## Migration Path to Facade Pattern

For future major versions (v2.0), the API may be refactored into explicit subsystem facades:

```cpp
// Hypothetical v2.0 API structure
class ofxVlc4 {
public:
    // Subsystem accessors
    ofxVlc4Audio & audio();
    ofxVlc4Video & video();
    ofxVlc4Media & media();
    ofxVlc4Playback & playback();
    ofxVlc4Recorder & recorder();
    ofxVlc4Midi & midi();

    // Core methods remain on main class
    void init();
    void update();
    void close();
};

// Usage
player.audio().setVolume(75);
player.playback().play();
player.recorder().startSession(config);
```

This would maintain the current API through forwarding methods with deprecation warnings, allowing gradual migration.

---

## See Also

- [README.md](../README.md) - Main documentation and feature overview
- [ARCHITECTURE.md](ARCHITECTURE.md) - Internal architecture and component model
- [RECORDING_PERFORMANCE.md](RECORDING_PERFORMANCE.md) - Recording optimization guide
- Example applications in `ofxVlc4Example/`, `ofxVlc4RecorderExample/`, etc.
