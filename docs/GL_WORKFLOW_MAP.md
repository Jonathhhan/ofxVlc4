# GL Workflow Map (Playback + Recording)

This note gives a quick end-to-end map of the current OpenGL frame paths for contributors.

## 1) Playback path (callback → fence → wait → draw)

### Producer side (VLC callback thread / VLC GL context)
1. VLC calls `make_current(true)` and `VideoComponent::bindVlcRenderTarget()` binds the shared FBO + texture target.
2. VLC renders into the texture-backed FBO.
3. VLC calls `videoSwap()` (`swap_cb`), which marks `exposedTextureDirty = true`.
4. VLC calls `make_current(false)`:
   - unbind FBO
   - insert fence (`insertFenceSync()`)
   - flush commands (`flushCommands()`)
   - release current context

### Consumer side (main thread / app GL context)
1. Before drawing or copying the exposed texture, code waits on the published fence (`waitForPublishedFrameFenceLocked()`).
2. On success (or timeout fallback path), draw from `videoTexture` (or refresh `exposedTextureFbo` first when shader adjustments are enabled).

### Why this order matters
- `swap_cb` can run without a current GL context, so fence creation is done in `make_current(false)` while context is valid.
- `glFlush()` after fence insertion ensures producer commands are submitted before consumer-side wait.

## 2) Recording async readback path (PBO submit → wait → map → consume)

> Note: The recorder currently forces synchronous GL readback to keep VLC rawvid
> callbacks fed with CPU-ready frames. The async PBO flow below is retained as a
> reference for when the path can be safely re-enabled.

### Submit stage
1. Recorder allocates pixel-pack buffers (`allocatePixelPackBuffers()`).
2. On capture, submit readback for write index:
   - set pack alignment
   - `submitTextureReadback()` (`glGetTexImage` into PBO + fence insertion)
   - store fence + submit timestamp
   - enqueue buffer index in pending queue

### Wait/consume stage
1. Recorder drains pending indices:
   - `waitForSubmittedReadbackLocked()` waits on fence via `clientWaitFenceSync()`
   - policy controls behavior:
     - `DropLateFrames`: non-blocking wait, drop when not ready
     - `BlockForFreshestFrame`: blocking retry path
2. When ready:
   - `mapPixelPackBuffer()`
   - memcpy into `recordingPixels`
   - `unmapPixelPackBuffer()`
   - delete fence, update stats, publish frame-ready signal

### Fallback
- If async path fails (allocation/map/wait failure), recorder falls back to synchronous `recordingTexture.readToPixels(recordingPixels)`.

## 3) Primary code locations

- Playback callbacks and fence handoff:
  - `src/video/ofxVlc4Video.cpp`
- GL helper primitives (fences/FBO/PBO):
  - `src/support/ofxVlc4GlOps.h`
- Recorder PBO pipeline:
  - `src/recording/ofxVlc4Recorder.cpp`
