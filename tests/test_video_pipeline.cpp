// Mock-based pipeline test that exercises the video callback → FBO → fence
// sync → consumer read chain without any real VLC or GL runtime.  This
// validates the core data path that all video features depend on.
//
// The video pipeline mirrors the sequence of operations that VLC performs when
// delivering decoded video frames:
//
//   1.  Setup: FBO created, texture attached.
//   2.  Resize: new dimensions queued, FBO reallocated.
//   3.  Per-frame (producer thread):
//       a. makeCurrent(true)  → bind FBO
//       b. VLC renders into FBO
//       c. videoSwap()        → insert fence sync, mark dirty
//       d. makeCurrent(false) → flush GPU commands
//   4.  Per-frame (consumer thread):
//       a. wait on fence
//       b. blit render texture to exposed texture
//   5.  Teardown: delete FBO, delete fence.

#include "ofxVlc4GlOps.h"
#include "ofxVlc4VideoHelpers.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors the other test files)
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

static void beginSuite(const char * name) {
	std::printf("\n[%s]\n", name);
}

static void check(bool condition, const char * expr, const char * file, int line) {
	if (condition) {
		++g_passed;
		std::printf("  PASS  %s\n", expr);
	} else {
		++g_failed;
		std::printf("  FAIL  %s  (%s:%d)\n", expr, file, line);
	}
}

#define CHECK(expr)      check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b)   check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Log query helpers (from the GL stubs in stubs_gl/ofMain.h)
// ---------------------------------------------------------------------------

static size_t logSize() {
	return glCallLog().size();
}

static bool logContains(const std::string & name) {
	for (const auto & entry : glCallLog()) {
		if (entry.name == name) return true;
	}
	return false;
}

static size_t logCount(const std::string & name) {
	size_t count = 0;
	for (const auto & entry : glCallLog()) {
		if (entry.name == name) ++count;
	}
	return count;
}

static const GlCallEntry & logAt(size_t index) {
	return glCallLog().at(index);
}

static void resetAll() {
	resetGlStubs();
	g_ofClearCallCount = 0;
}

// ---------------------------------------------------------------------------
// Simulated video pipeline state
//
// In the real addon this state lives in ofxVlc4Impl's various runtime
// structs.  Here we keep a minimal reproduction that mirrors the fields
// involved in the video callback chain.
// ---------------------------------------------------------------------------

struct MockVideoState {
	// FBO / texture resources.
	GLuint vlcFramebufferId = 0;
	GLuint textureId = 0;
	GLenum textureTarget = GL_TEXTURE_2D;
	bool vlcFramebufferAttachmentDirty = false;
	bool vlcFboBound = false;

	// Fence sync for frame publication.
	GLsync publishedVideoFrameFence = nullptr;

	// Frame flags.
	bool exposedTextureDirty = false;
	bool hasReceivedVideoFrame = false;
	bool isVideoLoaded = false;
	bool shuttingDown = false;

	// Geometry.
	unsigned renderWidth = 0;
	unsigned renderHeight = 0;
	unsigned sourceWidth = 0;
	unsigned sourceHeight = 0;
};

// ---------------------------------------------------------------------------
// Simulated pipeline operations
//
// These functions mirror the key operations performed by the VideoComponent
// methods but operate on MockVideoState instead of the PIMPL struct.
// ---------------------------------------------------------------------------

// Corresponds to ensureVideoRenderTargetCapacity → setupFboWithTexture.
static bool simulateSetup(MockVideoState & state, unsigned width, unsigned height) {
	state.renderWidth = width;
	state.renderHeight = height;
	state.sourceWidth = width;
	state.sourceHeight = height;

	// Simulate texture allocation (in real code, ofTexture::allocate).
	if (state.textureId == 0) {
		state.textureId = g_glNextObjectId++;
	}

	bool ok = ofxVlc4GlOps::setupFboWithTexture(state.vlcFramebufferId, state.textureTarget, state.textureId);
	if (ok) {
		ofxVlc4GlOps::setTextureLinearFiltering(state.textureTarget, state.textureId);
		state.vlcFramebufferAttachmentDirty = true;
		state.isVideoLoaded = true;
		state.exposedTextureDirty = true;
	}
	return ok;
}

// Corresponds to VideoComponent::videoResize (GL path).
static bool simulateResize(MockVideoState & state, unsigned newWidth, unsigned newHeight) {
	if (newWidth == 0 || newHeight == 0) {
		return false;
	}
	state.renderWidth = newWidth;
	state.renderHeight = newHeight;
	state.sourceWidth = newWidth;
	state.sourceHeight = newHeight;

	// Simulate texture reallocation.
	state.textureId = g_glNextObjectId++;
	bool ok = ofxVlc4GlOps::setupFboWithTexture(state.vlcFramebufferId, state.textureTarget, state.textureId);
	if (ok) {
		ofxVlc4GlOps::setTextureLinearFiltering(state.textureTarget, state.textureId);
		state.vlcFramebufferAttachmentDirty = true;
		state.exposedTextureDirty = true;
	}
	return ok;
}

// Corresponds to VideoComponent::makeCurrent(true) + bindVlcRenderTarget.
static void simulateMakeCurrentTrue(MockVideoState & state) {
	if (state.vlcFramebufferId == 0) {
		return;
	}
	bool dirty = state.vlcFramebufferAttachmentDirty;
	ofxVlc4GlOps::bindFbo(state.vlcFramebufferId, dirty, state.textureTarget, state.textureId);
	state.vlcFramebufferAttachmentDirty = dirty;
	state.vlcFboBound = true;
}

// Corresponds to VideoComponent::unbindVlcRenderTarget.
static void simulateUnbindRenderTarget(MockVideoState & state) {
	if (!state.vlcFboBound) {
		return;
	}
	ofxVlc4GlOps::unbindFbo();
	state.vlcFboBound = false;
}

// Corresponds to VideoComponent::videoSwap.
static void simulateVideoSwap(MockVideoState & state) {
	if (state.shuttingDown) {
		return;
	}

	state.hasReceivedVideoFrame = true;

	const bool needsPublish = !state.exposedTextureDirty;
	state.exposedTextureDirty = true;

	if (!needsPublish) {
		return;
	}

	// Delete any previous fence before inserting a new one.
	ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
	state.publishedVideoFrameFence = ofxVlc4GlOps::insertFenceSync();
}

// Corresponds to VideoComponent::makeCurrent(false) — flush + unbind.
static void simulateMakeCurrentFalse(MockVideoState & state) {
	simulateUnbindRenderTarget(state);
	ofxVlc4GlOps::flushCommands();
}

// Corresponds to the consumer-side refreshExposedTextureLocked path.
static bool simulateConsumerRefresh(MockVideoState & state) {
	if (!state.exposedTextureDirty) {
		return false;
	}

	if (state.publishedVideoFrameFence) {
		GLenum result = ofxVlc4GlOps::clientWaitFenceSync(
			state.publishedVideoFrameFence,
			GL_SYNC_FLUSH_COMMANDS_BIT,
			1000000000ULL);

		if (result == GL_WAIT_FAILED) {
			// Error path — still clean up.
			ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
			return false;
		}

		if (result == GL_TIMEOUT_EXPIRED) {
			ofxVlc4GlOps::gpuWaitFenceSync(state.publishedVideoFrameFence);
		}

		ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
	}

	// In the real code this would blit the render texture to the exposed FBO.
	state.exposedTextureDirty = false;
	return true;
}

// Corresponds to pipeline teardown.
static void simulateTeardown(MockVideoState & state) {
	ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
	ofxVlc4GlOps::deleteFbo(state.vlcFramebufferId);
	state.textureId = 0;
	state.isVideoLoaded = false;
	state.hasReceivedVideoFrame = false;
	state.exposedTextureDirty = false;
}

// ---------------------------------------------------------------------------
// Helper: run one full producer frame cycle.
// ---------------------------------------------------------------------------

static void runProducerFrame(MockVideoState & state) {
	simulateMakeCurrentTrue(state);
	// (VLC renders into the FBO — simulated by the bind itself.)
	simulateVideoSwap(state);
	simulateMakeCurrentFalse(state);
}

// ===================================================================
// Tests
// ===================================================================

static void testSetupAndTeardown() {
	beginSuite("pipeline setup and teardown");

	resetAll();
	MockVideoState state;

	bool ok = simulateSetup(state, 1920, 1080);
	CHECK(ok);
	CHECK(state.vlcFramebufferId != 0u);
	CHECK(state.textureId != 0u);
	CHECK(state.isVideoLoaded);
	CHECK(state.exposedTextureDirty);
	CHECK_EQ(state.renderWidth, 1920u);
	CHECK_EQ(state.renderHeight, 1080u);
	CHECK(logContains("glGenFramebuffers"));
	CHECK(logContains("glFramebufferTexture2D"));
	CHECK(logContains("glTexParameteri"));

	simulateTeardown(state);
	CHECK_EQ(state.vlcFramebufferId, 0u);
	CHECK_EQ(state.textureId, 0u);
	CHECK(!state.isVideoLoaded);
	CHECK(logContains("glDeleteFramebuffers"));
}

static void testSingleFrameDelivery() {
	beginSuite("single frame delivery");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	simulateSetup(state, 640, 480);

	// Consumer clears the initial dirty flag (setup marks it dirty).
	CHECK(simulateConsumerRefresh(state));
	CHECK(!state.exposedTextureDirty);

	// Producer delivers one frame.
	resetAll();
	runProducerFrame(state);

	CHECK(state.hasReceivedVideoFrame);
	CHECK(state.exposedTextureDirty);
	CHECK(state.publishedVideoFrameFence != nullptr);

	// Verify the GL call sequence for a single frame.
	CHECK(logContains("glBindFramebuffer"));    // bind FBO
	CHECK(logContains("glFenceSync"));           // videoSwap fence
	CHECK(logContains("glFlush"));               // makeCurrent(false)

	// Consumer reads the frame.
	CHECK(simulateConsumerRefresh(state));
	CHECK(!state.exposedTextureDirty);
	CHECK_EQ(state.publishedVideoFrameFence, nullptr);

	simulateTeardown(state);
}

static void testMultipleFrames() {
	beginSuite("multiple sequential frames");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	simulateSetup(state, 1280, 720);

	// Clear setup dirty flag.
	simulateConsumerRefresh(state);

	const int kFrameCount = 10;
	for (int i = 0; i < kFrameCount; ++i) {
		runProducerFrame(state);
		CHECK(state.exposedTextureDirty);
		CHECK(state.publishedVideoFrameFence != nullptr);

		bool refreshed = simulateConsumerRefresh(state);
		CHECK(refreshed);
		CHECK(!state.exposedTextureDirty);
		CHECK_EQ(state.publishedVideoFrameFence, nullptr);
	}

	CHECK(state.hasReceivedVideoFrame);
	simulateTeardown(state);
}

static void testFrameSkipDetection() {
	beginSuite("frame skip (producer outpaces consumer)");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	simulateSetup(state, 640, 480);

	// Clear setup dirty flag.
	simulateConsumerRefresh(state);

	// Producer delivers first frame.
	runProducerFrame(state);
	CHECK(state.exposedTextureDirty);
	GLsync firstFence = state.publishedVideoFrameFence;
	CHECK(firstFence != nullptr);

	// Producer delivers second frame BEFORE consumer reads first.
	// The dirty flag is already set, so videoSwap skips the fence insert.
	resetAll();
	runProducerFrame(state);
	CHECK(state.exposedTextureDirty);

	// Since exposedTextureDirty was already true when videoSwap ran,
	// needsPublish was false and no new fence was inserted.  The original
	// fence from the first frame is still live.
	CHECK_EQ(state.publishedVideoFrameFence, firstFence);
	// No glFenceSync or glDeleteSync calls this frame.
	CHECK(!logContains("glFenceSync"));

	// Consumer catches up.
	bool refreshed = simulateConsumerRefresh(state);
	CHECK(refreshed);
	CHECK(!state.exposedTextureDirty);

	simulateTeardown(state);
}

static void testResizeDuringPlayback() {
	beginSuite("resize during playback");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	simulateSetup(state, 640, 480);
	simulateConsumerRefresh(state);

	// Deliver some frames at initial size.
	runProducerFrame(state);
	simulateConsumerRefresh(state);

	// Resize to 1920×1080.
	resetAll();
	bool ok = simulateResize(state, 1920, 1080);
	CHECK(ok);
	CHECK_EQ(state.renderWidth, 1920u);
	CHECK_EQ(state.renderHeight, 1080u);
	CHECK(state.vlcFramebufferAttachmentDirty);
	CHECK(state.exposedTextureDirty);
	CHECK(logContains("glBindFramebuffer"));
	CHECK(logContains("glTexParameteri"));

	// Deliver a frame at the new size — dirty FBO triggers reattach.
	resetAll();
	runProducerFrame(state);
	CHECK(!state.vlcFramebufferAttachmentDirty);
	CHECK(logContains("glFramebufferTexture2D")); // reattach on dirty bind

	simulateConsumerRefresh(state);
	simulateTeardown(state);
}

static void testFenceWaitTimeout() {
	beginSuite("consumer fence wait timeout → GPU hand-off");

	resetAll();
	MockVideoState state;

	simulateSetup(state, 640, 480);
	simulateConsumerRefresh(state);

	// Deliver a frame.
	runProducerFrame(state);

	// Consumer waits but the fence times out.
	resetAll();
	g_glClientWaitSyncResult = GL_TIMEOUT_EXPIRED;
	bool refreshed = simulateConsumerRefresh(state);
	CHECK(refreshed);
	CHECK(!state.exposedTextureDirty);

	// Verify that the GPU wait fallback path was taken.
	CHECK(logContains("glClientWaitSync"));
	CHECK(logContains("glWaitSync"));     // GPU hand-off
	CHECK(logContains("glDeleteSync"));

	simulateTeardown(state);
}

static void testFenceWaitFailed() {
	beginSuite("consumer fence wait failure");

	resetAll();
	MockVideoState state;

	simulateSetup(state, 640, 480);
	simulateConsumerRefresh(state);

	runProducerFrame(state);

	// Consumer wait fails.
	resetAll();
	g_glClientWaitSyncResult = GL_WAIT_FAILED;
	bool refreshed = simulateConsumerRefresh(state);
	CHECK(!refreshed);   // Returns false on wait failure.

	// Fence should still have been cleaned up.
	CHECK_EQ(state.publishedVideoFrameFence, nullptr);
	CHECK(logContains("glDeleteSync"));

	simulateTeardown(state);
}

static void testShutdownDuringSwap() {
	beginSuite("shutdown flag prevents swap");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	simulateSetup(state, 640, 480);
	simulateConsumerRefresh(state);

	// Set shutdown flag before frame delivery.
	state.shuttingDown = true;
	resetAll();
	runProducerFrame(state);

	// videoSwap should have bailed early — no fence inserted.
	CHECK(!state.hasReceivedVideoFrame);
	CHECK(!logContains("glFenceSync"));

	simulateTeardown(state);
}

static void testConsumerRefreshWithoutFrame() {
	beginSuite("consumer refresh with no dirty frame");

	resetAll();
	MockVideoState state;

	simulateSetup(state, 640, 480);
	// Clear the initial dirty flag.
	simulateConsumerRefresh(state);

	// No producer frame — consumer refresh should be a no-op.
	resetAll();
	bool refreshed = simulateConsumerRefresh(state);
	CHECK(!refreshed);
	CHECK_EQ(logSize(), 0u);

	simulateTeardown(state);
}

static void testVisibleSourceSizeDuringResize() {
	beginSuite("visible source size calculation");

	using ofxVlc4VideoHelpers::SimpleVideoSizeInfo;
	using ofxVlc4VideoHelpers::visibleVideoSourceSize;

	// Source fits inside render area.
	{
		SimpleVideoSizeInfo info;
		info.sourceWidth = 640;
		info.sourceHeight = 480;
		info.renderWidth = 1920;
		info.renderHeight = 1080;
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 640u);
		CHECK_EQ(h, 480u);
	}

	// Render area smaller than source.
	{
		SimpleVideoSizeInfo info;
		info.sourceWidth = 1920;
		info.sourceHeight = 1080;
		info.renderWidth = 640;
		info.renderHeight = 480;
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 640u);
		CHECK_EQ(h, 480u);
	}

	// Exact match.
	{
		SimpleVideoSizeInfo info;
		info.sourceWidth = 1280;
		info.sourceHeight = 720;
		info.renderWidth = 1280;
		info.renderHeight = 720;
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 1280u);
		CHECK_EQ(h, 720u);
	}
}

static void testFullPipelineLifecycle() {
	beginSuite("full pipeline lifecycle (setup → frames → resize → frames → teardown)");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	// 1. Setup at 640×480.
	bool ok = simulateSetup(state, 640, 480);
	CHECK(ok);

	// 2. Consumer clears initial dirty flag.
	simulateConsumerRefresh(state);

	// 3. Deliver 5 frames.
	for (int i = 0; i < 5; ++i) {
		runProducerFrame(state);
		simulateConsumerRefresh(state);
	}
	CHECK(state.hasReceivedVideoFrame);
	CHECK(!state.exposedTextureDirty);

	// 4. Resize to 1920×1080.
	ok = simulateResize(state, 1920, 1080);
	CHECK(ok);
	CHECK_EQ(state.renderWidth, 1920u);

	// 5. Deliver 5 more frames at new size.
	for (int i = 0; i < 5; ++i) {
		runProducerFrame(state);
		simulateConsumerRefresh(state);
	}
	CHECK(!state.exposedTextureDirty);

	// 6. Teardown.
	simulateTeardown(state);
	CHECK_EQ(state.vlcFramebufferId, 0u);
	CHECK_EQ(state.textureId, 0u);
	CHECK(!state.isVideoLoaded);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testSetupAndTeardown();
	testSingleFrameDelivery();
	testMultipleFrames();
	testFrameSkipDetection();
	testResizeDuringPlayback();
	testFenceWaitTimeout();
	testFenceWaitFailed();
	testShutdownDuringSwap();
	testConsumerRefreshWithoutFrame();
	testVisibleSourceSizeDuringResize();
	testFullPipelineLifecycle();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
