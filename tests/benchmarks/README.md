# Performance Benchmarks

This directory contains performance benchmarking tools for ofxVlc4.

## Purpose

Measure and track performance characteristics:
- **Playback performance** - Frame rates, seek times, startup latency
- **Recording throughput** - Frames per second, encoding overhead
- **Memory usage** - Baseline, per-stream, peak usage
- **Codec comparison** - H.264 vs H.265 vs MJPEG performance

## Running Benchmarks

### Prerequisites

1. Install libVLC:
   ```bash
   cd ../..
   bash scripts/install-libvlc.sh
   ```

2. Prepare test media (multiple resolutions):
   ```bash
   cd benchmarks
   # Download test videos or use your own
   # Recommended: 720p, 1080p, 4K samples
   ```

### Build and Run

```bash
mkdir -p /tmp/benchmark-build
cd /tmp/benchmark-build
cmake /path/to/ofxVlc4/tests/benchmarks
make
./benchmark_playback
./benchmark_recording
./benchmark_memory
```

## Available Benchmarks

### benchmark_playback
Measures playback performance:
- **Startup time** - Time to first frame
- **Seek performance** - Forward/backward seek latency
- **Frame rate stability** - Dropped frames, jitter
- **CPU usage** - During playback at various resolutions

Output: JSON report with timing data

### benchmark_recording
Measures recording performance:
- **Encoding throughput** - FPS at different resolutions
- **GL readback time** - GPU → CPU transfer cost
- **Codec comparison** - H.264 vs H.265 performance
- **Mux overhead** - Time to finalize files

Output: JSON report with throughput data

### benchmark_memory
Measures memory usage:
- **Baseline** - Empty player instance
- **Per-stream** - Memory per active player
- **Peak usage** - Maximum during operation
- **Leak detection** - Repeated init/cleanup cycles

Output: JSON report with memory metrics

## Benchmark Results

Results are written to:
- `benchmark_results_playback.json`
- `benchmark_results_recording.json`
- `benchmark_results_memory.json`

### Example Output

```json
{
  "benchmark": "playback",
  "timestamp": "2026-04-14T09:00:00Z",
  "system": {
    "os": "Linux",
    "cpu": "Intel i7-9700K",
    "gpu": "NVIDIA RTX 2060"
  },
  "results": {
    "startup_time_ms": 245,
    "seek_forward_ms": 18,
    "seek_backward_ms": 22,
    "dropped_frames": 3,
    "avg_fps": 59.8
  }
}
```

## Tracking Performance Over Time

To track performance regressions:

1. **Baseline** - Run benchmarks on stable release
2. **Save results** - Commit JSON files to git
3. **Compare** - Run again after changes
4. **Analyze** - Use diff tool to spot regressions

Example comparison:
```bash
# Before changes
./benchmark_playback > before.json

# After changes
./benchmark_playback > after.json

# Compare
diff before.json after.json
```

## CI Integration

Benchmarks can be run in CI to:
- Detect performance regressions
- Track trends over time
- Compare across platforms

**Note**: Absolute numbers vary by hardware. Focus on:
- Relative changes (% difference)
- Trends across commits
- Cross-platform consistency

## Interpreting Results

### Good Performance Indicators
- ✅ Startup < 300ms
- ✅ Seek latency < 50ms
- ✅ 60 FPS sustained at 1080p
- ✅ < 10% dropped frames
- ✅ Recording at 30+ FPS (720p)

### Performance Issues
- ⚠️ Startup > 1000ms - Check plugin loading
- ⚠️ Seek latency > 200ms - Check codec/file format
- ⚠️ Dropped frames > 20% - GPU/CPU bottleneck
- ⚠️ Recording < 15 FPS - Check encoding settings

## Adding New Benchmarks

1. Create `benchmark_yourfeature.cpp`
2. Add to `CMakeLists.txt`
3. Follow existing output format (JSON)
4. Keep execution time < 60 seconds
5. Output results to `benchmark_results_yourfeature.json`
6. Document expected ranges

## See Also

- Performance guide: `../../docs/RECORDING_PERFORMANCE.md`
- Unit tests: `../tests/`
- Integration tests: `../integration/`
