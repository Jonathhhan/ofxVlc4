# Integration Tests

This directory contains integration tests that require a real VLC runtime.

Unlike the unit tests in `../tests/`, these tests:
- **Require libVLC 4** to be installed
- **Test end-to-end workflows** with actual media files
- **Run slower** due to real playback/recording
- **Are optional** - run separately from unit tests

## Running Integration Tests

### Prerequisites

1. Install libVLC:
   ```bash
   cd ../..
   bash scripts/install-libvlc.sh
   ```

2. Prepare test media:
   ```bash
   # Download a small test video (or use your own)
   cd integration
   wget https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/360/Big_Buck_Bunny_360_10s_1MB.mp4 \
        -O test_media.mp4
   ```

### Build and Run

```bash
mkdir -p /tmp/integration-build
cd /tmp/integration-build
cmake /path/to/ofxVlc4/tests/integration
make
ctest --output-on-failure
```

Or run individual tests:
```bash
./test_integration_playback
./test_integration_recording
```

## Test Coverage

### test_integration_playback
Tests basic playback lifecycle:
- Initialize player
- Load media file
- Play for a few seconds
- Seek to different positions
- Stop and cleanup

### test_integration_recording
Tests recording workflow:
- Initialize player
- Start recording session
- Record a few frames
- Stop and finalize
- Verify output file exists

## Test Media

Integration tests use small test videos to minimize:
- Download time
- Disk space
- Test execution time

Recommended test media:
- **Big Buck Bunny** (open source, various resolutions)
- **Tears of Steel** (open source, 4K available)
- Your own short clips (< 10 seconds recommended)

## CI Integration

These tests are **not** run in standard CI due to:
- VLC runtime requirement
- Media file dependencies
- Longer execution time

Consider running:
- Locally before major releases
- On dedicated test infrastructure
- As smoke tests for deployment

## Troubleshooting

### "libvlc not found"
- Run `scripts/install-libvlc.sh`
- Check `LD_LIBRARY_PATH` (Linux) or `PATH` (Windows)

### "Test media not found"
- Download test media to `tests/integration/test_media.mp4`
- Or update `INTEGRATION_TEST_MEDIA` path in CMakeLists.txt

### Tests hang
- Check VLC plugin path is correct
- Verify media file is valid
- Increase timeouts in test code

## Adding New Integration Tests

1. Create `test_integration_yourfeature.cpp`
2. Add to `CMakeLists.txt`
3. Use same pattern as existing tests
4. Keep tests < 30 seconds execution time
5. Clean up resources in destructors
6. Document any special requirements

## See Also

- Unit tests: `../tests/` (no VLC runtime needed)
- Fuzzing tests: `../tests/fuzz/`
- Examples: `../../ofxVlc4*Example/`
