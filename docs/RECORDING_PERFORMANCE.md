# ofxVlc4 Recording Performance Guide

This document provides performance characteristics, optimization strategies, and configuration recommendations for the ofxVlc4 recording subsystem.

## Overview

The ofxVlc4 recorder captures video textures or windows and audio streams, encodes them through libVLC, and optionally muxes them into common container formats. The recording pipeline uses:

- **Asynchronous PBO readback** for GPU texture capture with minimal CPU blocking (enabled by default with 4+ buffer pipeline)
- **Lock-free audio ring buffer** for audio capture (default 6 seconds for better stability)
- **Post-stop muxing** for combining video and audio streams
- **Explicit stream finalization** to ensure VLC completes encoding before muxing

The async PBO path is now re-enabled with enhanced buffering to keep VLC rawvid callbacks fed while hiding GPU readback latency.

Performance characteristics depend on resolution, frame rate, codec choice, GPU capabilities, and system resources.

---

## Performance Profiles by Resolution

### 720p (1280x720)

**Real-time capable**: Yes, on most modern hardware

**Typical performance**:
- Frame rate: 30-60 fps achievable
- GPU readback latency: ~2-5ms per frame
- Memory per frame: ~3.7 MB (RGBA)
- Recommended buffer size: 4-8 frames

**Recommended settings**:
```cpp
ofxVlc4RecordingPreset preset;
preset.width = 1280;
preset.height = 720;
preset.fps = 30;
preset.videoBitrateKbps = 4000;  // 4 Mbps for good quality
preset.videoCodec = "h264";
preset.containerMux = "mp4";
preset.audioRingBufferSeconds = 6.0f;
```

**Use cases**: Real-time streaming, game capture, video tutorials

---

### 1080p (1920x1080)

**Real-time capable**: Yes, with modern GPU

**Typical performance**:
- Frame rate: 30-60 fps achievable (GPU-dependent)
- GPU readback latency: ~5-10ms per frame
- Memory per frame: ~8.3 MB (RGBA)
- Recommended buffer size: 6-10 frames

**Recommended settings**:
```cpp
ofxVlc4RecordingPreset preset;
preset.width = 1920;
preset.height = 1080;
preset.fps = 30;
preset.videoBitrateKbps = 8000;  // 8 Mbps for high quality
preset.videoCodec = "h264";
preset.containerMux = "mp4";
preset.audioRingBufferSeconds = 6.0f;
```

**Use cases**: High-quality recording, presentations, professional content

---

### 4K (3840x2160)

**Real-time capable**: Depends on GPU, codec, and frame rate

**Typical performance**:
- Frame rate: 24-30 fps achievable (high-end GPU required for 60 fps)
- GPU readback latency: ~15-30ms per frame
- Memory per frame: ~33 MB (RGBA)
- Recommended buffer size: 4-6 frames (memory constraints)

**Recommended settings**:
```cpp
ofxVlc4RecordingPreset preset;
preset.width = 3840;
preset.height = 2160;
preset.fps = 24;  // or 30
preset.videoBitrateKbps = 20000;  // 20 Mbps minimum
preset.videoCodec = "h265";  // HEVC for better compression
preset.containerMux = "mkv";  // MKV required for H265
preset.audioRingBufferSeconds = 6.0f;  // Larger buffer for heavier load
```

**Important notes for 4K**:
- H.265/HEVC recording requires MKV container
- H.265 encoder normalizes dimensions to multiples of 16×8
- Consider dropping to 24 fps if frame drops occur
- Monitor GPU memory usage (~200+ MB for buffers)

**Use cases**: Archival footage, cinema production, high-end displays

---

## Codec Performance Characteristics

### H.264 (AVC)

**Performance**: Fast encoding, good quality
- **CPU/GPU usage**: Moderate
- **Compression ratio**: Good (8-12 Mbps for 1080p)
- **Compatibility**: Excellent (universal playback)
- **Recommended for**: Real-time recording, streaming, general use

### H.265 (HEVC)

**Performance**: Slower encoding, excellent quality
- **CPU/GPU usage**: High
- **Compression ratio**: Excellent (4-8 Mbps for 1080p)
- **Compatibility**: Good (modern devices)
- **Restrictions**: MKV container required, dimensions normalized to 16×8
- **Recommended for**: 4K recording, archival, bandwidth-constrained scenarios

### MJPEG

**Performance**: Very fast encoding, large files
- **CPU/GPU usage**: Low
- **Compression ratio**: Poor (frame-by-frame JPEG)
- **Compatibility**: Excellent
- **Recommended for**: Frame-perfect capture, debugging, temporary recordings

---

## GPU Readback Performance

The recorder uses **asynchronous PBO readback** when a GL context is available, with an enhanced buffer pipeline (minimum 4 buffers) to ensure VLC rawvid callbacks always have frames available while hiding GPU latency.

### PBO Configuration

**Default behavior**:
- Multi-buffered (4+ PBOs by default)
- Async mapping with `GL_MAP_READ_BIT`
- Fence sync for completion detection
- Explicit stream finalization on stop

**Readback policies** (currently inactive):
1. **Drop late frames**: Skip frames that aren't ready (minimize latency)
2. **Wait for freshest**: Block until newest frame available (maximize quality)

### Performance Metrics API

Track readback performance in real-time:

```cpp
ofxVlc4RecorderPerformanceInfo perf = player.getRecorderPerformanceInfo();

std::cout << "Pending frames: " << perf.pendingFrameCount << "\n";
std::cout << "Avg latency: " << perf.averageLatencyMs << " ms\n";
std::cout << "Dropped frames: " << perf.droppedFrameCount << "\n";
std::cout << "Map failures: " << perf.mapFailureCount << "\n";
```

**Interpretation**:
- `pendingFrameCount` should stay < 3 for smooth recording
- `averageLatencyMs` should be < 20ms for real-time
- `droppedFrameCount` indicates readback can't keep up (reduce resolution/fps)
- `mapFailureCount` suggests GPU memory pressure

---

## Audio Ring Buffer Configuration

The audio ring buffer decouples audio capture from the VLC encode thread.

### Buffer Sizing

**Formula**: `bufferSize = sampleRate × channels × bufferSeconds`

**Default**: 4.0 seconds (192,000 samples @ 48kHz stereo)

**Recommendations**:
- **Real-time streaming**: 2.0-4.0 seconds (minimize latency)
- **Standard recording**: 4.0-6.0 seconds (balance latency/stability)
- **4K or heavy load**: 6.0-8.0 seconds (absorb encoding spikes)

### Buffer Monitoring

```cpp
// Check for audio buffer issues
if (player.getRecorderPerformanceInfo().audioOverruns > 0) {
    // Buffer too small or encoding too slow
    // Consider increasing audioRingBufferSeconds
}
```

---

## Mux Timeout Configuration

Post-stop muxing combines separate video and audio files into the final output.

### Timeout Recommendations

| Recording Duration | Recommended Timeout |
|--------------------|---------------------|
| < 1 minute        | 15 seconds (default) |
| 1-5 minutes       | 30 seconds |
| 5-15 minutes      | 60 seconds |
| > 15 minutes      | 120+ seconds |

**Configuration**:
```cpp
ofxVlc4RecordingPreset preset;
preset.muxTimeoutMs = 30000;  // 30 seconds
```

**File stability check**: The mux step waits for 3 consecutive stable file-size readings (~150ms) before processing, ensuring VLC has finalized the intermediate files.

**Keyframe policy**: All non-intraframe codecs (H264/H265/MP4V/VPx/Theora) are forced to emit a keyframe every frame to keep muxing stable across containers. Expect larger file sizes; MJPG and HAP codecs remain unchanged.

---

## Known Bottlenecks

### 1. GPU Readback Throughput

**Symptom**: Dropped frames, high `averageLatencyMs`

**Solutions**:
- Reduce resolution or frame rate
- Use H.264 instead of H.265
- Ensure GPU has sufficient VRAM
- Check for driver issues

### 2. Encoder Performance

**Symptom**: Real-time recording stutters, high CPU usage

**Solutions**:
- Lower bitrate (reduces encoder workload)
- Use faster codec preset (H.264 over H.265)
- Reduce fps (30 fps instead of 60 fps)
- Close other GPU-intensive applications

### 3. Audio Buffer Overruns

**Symptom**: Audio dropouts, `audioOverruns` > 0

**Solutions**:
- Increase `audioRingBufferSeconds` (6.0-8.0)
- Reduce video workload to free CPU for audio
- Check audio device latency settings

### 4. Mux Timeout Failures

**Symptom**: Separate video/audio files not combined

**Solutions**:
- Increase `muxTimeoutMs` for longer recordings
- Use MPEG-TS (`.ts`) for video intermediate (no finalization needed)
- Ensure sufficient disk I/O bandwidth

---

## Optimization Strategies

### Real-Time Streaming (Minimize Latency)

**Goal**: Lowest possible latency, can tolerate occasional frame drops

```cpp
ofxVlc4RecordingPreset preset;
preset.width = 1280;
preset.height = 720;
preset.fps = 30;
preset.videoBitrateKbps = 3000;
preset.videoCodec = "h264";
preset.containerMux = "mp4";
preset.audioRingBufferSeconds = 2.0f;  // Minimal buffer
preset.muxTimeoutMs = 15000;
// Use drop-late-frames readback policy
```

**Key settings**: Low buffer, fast codec, drop-frame policy

---

### High-Quality Archival (Maximize Quality)

**Goal**: Best possible quality, recording time not critical

```cpp
ofxVlc4RecordingPreset preset;
preset.width = 1920;
preset.height = 1080;  // or 3840x2160 for 4K
preset.fps = 30;
preset.videoBitrateKbps = 12000;  // High bitrate
preset.videoCodec = "h265";  // HEVC compression
preset.containerMux = "mkv";
preset.audioRingBufferSeconds = 6.0f;
preset.muxTimeoutMs = 60000;  // Long timeout for large files
// Use wait-for-freshest readback policy
```

**Key settings**: High bitrate, HEVC, larger buffers, wait policy

---

### Resource-Constrained Environments

**Goal**: Reliable recording with limited GPU/CPU resources

```cpp
ofxVlc4RecordingPreset preset;
preset.width = 1280;
preset.height = 720;  // Lower resolution
preset.fps = 24;  // Reduced frame rate
preset.videoBitrateKbps = 2500;
preset.videoCodec = "h264";  // Fast codec
preset.containerMux = "mp4";
preset.audioRingBufferSeconds = 6.0f;  // Stable buffer
preset.muxTimeoutMs = 30000;
```

**Key settings**: Conservative resolution/fps, simple codec, adequate buffers

---

## Common Recording Patterns

### Session-Based Texture Recording

```cpp
// Start recording from texture
ofxVlc4RecordingPreset preset = ofxVlc4RecordingPreset::H264_MP4();
player.startTextureRecordingSession(
    texture,
    "my-recording",
    preset
);

// ... record frames ...

// Stop and mux
player.stopRecordingSession();  // Blocks until mux completes
```

### Window Capture Recording

```cpp
// Start recording entire window
player.startWindowRecordingSession(
    "window-capture",
    preset
);

// ... application renders ...

player.stopRecordingSession();
```

### Manual Control (Advanced)

```cpp
// Fine-grained control over video/audio streams
player.recordVideo("output.mp4", 1920, 1080, 30);
player.recordAudio("output.wav");

// ... record ...

player.stopRecording();
// Manually mux if needed
```

---

## Performance Monitoring Example

```cpp
void ofApp::update() {
    if (player.getRecordingSessionState() == RecordingSessionState::Active) {
        auto perf = player.getRecorderPerformanceInfo();

        // Log performance metrics
        ofLogNotice() << "Recording performance:";
        ofLogNotice() << "  Pending frames: " << perf.pendingFrameCount;
        ofLogNotice() << "  Avg latency: " << perf.averageLatencyMs << "ms";
        ofLogNotice() << "  Dropped: " << perf.droppedFrameCount;
        ofLogNotice() << "  Overruns: " << perf.audioOverruns;

        // Alert on issues
        if (perf.droppedFrameCount > 10) {
            ofLogWarning() << "High frame drop rate - reduce resolution/fps";
        }
        if (perf.audioOverruns > 0) {
            ofLogWarning() << "Audio buffer overruns - increase ring buffer size";
        }
    }
}
```

---

## Platform-Specific Notes

### Windows

- Hardware encoding via NVDEC/DXVA2 available through decoder preference API
- D3D11 backend can be used alongside texture recording
- Check GPU driver version for optimal PBO performance

### macOS

- Metal may offer better performance than OpenGL for high-resolution capture
- Ensure VLC is built with GPU acceleration support
- Consider ProRes codec for professional workflows

### Linux

- VAAPI hardware acceleration available on supported GPUs
- Check libVLC build for codec support (`--list` to see available codecs)
- PBO performance varies significantly by GPU vendor/driver

---

## Troubleshooting

### Recording fails to start

**Check**:
- VLC instance initialized successfully
- Recording paths are writable
- Codec names are valid (`h264`, `h265`, not `H.264`)
- Container format matches codec (H.265 requires MKV)

### Choppy video playback

**Cause**: Frame drops during recording

**Solutions**:
- Monitor `droppedFrameCount` in performance metrics
- Reduce resolution, fps, or bitrate
- Switch from H.265 to H.264

### Audio/video sync issues

**Cause**: Buffer underrun or timing drift

**Solutions**:
- Increase `audioRingBufferSeconds`
- Ensure consistent frame timing in render loop
- Check for system clock drift (NTP sync)

### Large file sizes

**Cause**: High bitrate or lossless codec

**Solutions**:
- Reduce `videoBitrateKbps` (test quality/size tradeoff)
- Use H.265 for better compression
- Use variable bitrate (VBR) if supported

---

## See Also

- [ofxVlc4Recorder API Reference](../src/recording/ofxVlc4Recorder.h)
- [Recording Example](../ofxVlc4RecorderExample/README.md)
- [Main README - Recording Helpers](../README.md#recorder-helpers)
