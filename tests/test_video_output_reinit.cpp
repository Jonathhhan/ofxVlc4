// Mock-based test that exercises the video pipeline reinit path:
// setup → frame delivery → teardown → re-setup → frame delivery.
//
// This validates that:
//  1. Resources can be torn down and re-created without leaking state.
//  2. The shuttingDown flag prevents producer callbacks from touching
//     resources that are being destroyed during reinit.
//  3. After reinit the pipeline resumes normal frame delivery.

#include "ofxVlc4GlOps.h"
#include "ofxVlc4VideoHelpers.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
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

static void resetAll() {
	resetGlStubs();
	g_ofClearCallCount = 0;
}

// ---------------------------------------------------------------------------
// Mock video pipeline state — same as test_video_pipeline.cpp.
// ---------------------------------------------------------------------------

struct MockVideoState {
	GLuint vlcFramebufferId = 0;
	GLuint textureId = 0;
	GLenum textureTarget = GL_TEXTURE_2D;
	bool vlcFramebufferAttachmentDirty = false;
	bool vlcFboBound = false;
	GLsync publishedVideoFrameFence = nullptr;
	bool exposedTextureDirty = false;
	bool hasReceivedVideoFrame = false;
	bool isVideoLoaded = false;
	std::atomic<bool> shuttingDown { false };
	unsigned renderWidth = 0;
	unsigned renderHeight = 0;
};

// ---------------------------------------------------------------------------
// Simulated pipeline operations
// ---------------------------------------------------------------------------

static bool simulateSetup(MockVideoState & state, unsigned width, unsigned height) {
	state.renderWidth = width;
	state.renderHeight = height;
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

static void simulateMakeCurrentTrue(MockVideoState & state) {
	if (state.shuttingDown.load(std::memory_order_acquire)) {
		return;
	}
	if (state.vlcFramebufferId == 0) {
		return;
	}
	bool dirty = state.vlcFramebufferAttachmentDirty;
	ofxVlc4GlOps::bindFbo(state.vlcFramebufferId, dirty, state.textureTarget, state.textureId);
	state.vlcFramebufferAttachmentDirty = dirty;
	state.vlcFboBound = true;
}

static void simulateUnbindRenderTarget(MockVideoState & state) {
	if (!state.vlcFboBound) {
		return;
	}
	ofxVlc4GlOps::unbindFbo();
	state.vlcFboBound = false;
}

static void simulateVideoSwap(MockVideoState & state) {
	if (state.shuttingDown.load(std::memory_order_acquire)) {
		return;
	}
	state.hasReceivedVideoFrame = true;
	const bool needsPublish = !state.exposedTextureDirty;
	state.exposedTextureDirty = true;
	if (!needsPublish) {
		return;
	}
	ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
	state.publishedVideoFrameFence = ofxVlc4GlOps::insertFenceSync();
}

static void simulateMakeCurrentFalse(MockVideoState & state) {
	if (state.shuttingDown.load(std::memory_order_acquire)) {
		return;
	}
	simulateUnbindRenderTarget(state);
	ofxVlc4GlOps::flushCommands();
}

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
			ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
			return false;
		}
		if (result == GL_TIMEOUT_EXPIRED) {
			ofxVlc4GlOps::gpuWaitFenceSync(state.publishedVideoFrameFence);
		}
		ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
	}
	state.exposedTextureDirty = false;
	return true;
}

static void runProducerFrame(MockVideoState & state) {
	simulateMakeCurrentTrue(state);
	simulateVideoSwap(state);
	simulateMakeCurrentFalse(state);
}

static void simulateTeardown(MockVideoState & state) {
	ofxVlc4GlOps::deleteFenceSync(state.publishedVideoFrameFence);
	ofxVlc4GlOps::deleteFbo(state.vlcFramebufferId);
	state.textureId = 0;
	state.isVideoLoaded = false;
	state.hasReceivedVideoFrame = false;
	state.exposedTextureDirty = false;
	state.vlcFboBound = false;
	state.renderWidth = 0;
	state.renderHeight = 0;
}

// ===================================================================
// Tests
// ===================================================================

static void testReinitCycle() {
	beginSuite("reinit cycle: setup → frames → teardown → re-setup → frames");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	// --- First session ---
	bool ok = simulateSetup(state, 640, 480);
	CHECK(ok);
	CHECK(state.vlcFramebufferId != 0u);
	CHECK_EQ(state.renderWidth, 640u);

	simulateConsumerRefresh(state);
	for (int i = 0; i < 3; ++i) {
		runProducerFrame(state);
		simulateConsumerRefresh(state);
	}
	CHECK(state.hasReceivedVideoFrame);

	// Teardown (simulates releaseVlcResources).
	simulateTeardown(state);
	CHECK_EQ(state.vlcFramebufferId, 0u);
	CHECK_EQ(state.textureId, 0u);
	CHECK(!state.isVideoLoaded);

	// --- Second session (reinit) ---
	// Do NOT call resetAll() here so that g_glNextObjectId keeps incrementing
	// and the new FBO gets a fresh ID, mirroring what happens in the real
	// addon where GL object IDs are never reused within a process.
	ok = simulateSetup(state, 1920, 1080);
	CHECK(ok);
	CHECK(state.vlcFramebufferId != 0u);
	CHECK_EQ(state.renderWidth, 1920u);
	CHECK_EQ(state.renderHeight, 1080u);

	simulateConsumerRefresh(state);
	for (int i = 0; i < 3; ++i) {
		runProducerFrame(state);
		simulateConsumerRefresh(state);
	}
	CHECK(state.hasReceivedVideoFrame);
	CHECK(!state.exposedTextureDirty);

	simulateTeardown(state);
}

static void testShutdownGuardDuringTeardown() {
	beginSuite("shuttingDown flag prevents callbacks during teardown");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	// Setup and deliver a frame.
	simulateSetup(state, 640, 480);
	simulateConsumerRefresh(state);
	runProducerFrame(state);
	simulateConsumerRefresh(state);

	// Simulate the reinit path: set shuttingDown before teardown.
	state.shuttingDown.store(true, std::memory_order_release);

	// Reset hasReceivedVideoFrame so we can verify the guarded frame does
	// not set it again.
	state.hasReceivedVideoFrame = false;

	// Attempt to deliver a frame while shutting down — all callbacks bail out.
	resetAll();
	runProducerFrame(state);

	// videoSwap bailed out — no frame received, no fence inserted.
	CHECK(!state.hasReceivedVideoFrame);
	CHECK(!logContains("glFenceSync"));
	// makeCurrent(true) also bailed — no FBO bind.
	CHECK(!logContains("glBindFramebuffer"));
	// makeCurrent(false) bailed — no flush.
	CHECK(!logContains("glFlush"));
	CHECK_EQ(logSize(), 0u);

	// Now teardown is safe since no callbacks are accessing resources.
	simulateTeardown(state);
	CHECK_EQ(state.vlcFramebufferId, 0u);

	// Clear shutdown flag (simulates init() resetting the flag).
	state.shuttingDown.store(false, std::memory_order_release);

	// Re-setup and verify normal operation resumes.
	resetAll();
	bool ok = simulateSetup(state, 1280, 720);
	CHECK(ok);
	CHECK(state.vlcFramebufferId != 0u);

	simulateConsumerRefresh(state);
	runProducerFrame(state);
	CHECK(state.hasReceivedVideoFrame);
	CHECK(state.exposedTextureDirty);
	CHECK(logContains("glFenceSync"));

	simulateConsumerRefresh(state);
	simulateTeardown(state);
}

static void testConcurrentShutdownGuard() {
	beginSuite("concurrent producer respects shuttingDown during teardown");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	simulateSetup(state, 640, 480);
	simulateConsumerRefresh(state);

	// Simulate a producer thread that keeps trying to deliver frames
	// while the main thread sets shuttingDown and tears down resources.
	std::atomic<bool> producerDone { false };
	std::atomic<int> framesAttempted { 0 };
	std::atomic<int> framesDelivered { 0 };

	std::thread producerThread([&]() {
		while (!producerDone.load(std::memory_order_acquire)) {
			framesAttempted.fetch_add(1, std::memory_order_relaxed);
			// The shuttingDown guard in each callback prevents resource access.
			if (!state.shuttingDown.load(std::memory_order_acquire)) {
				simulateVideoSwap(state);
				framesDelivered.fetch_add(1, std::memory_order_relaxed);
			}
			std::this_thread::yield();
		}
	});

	// Let the producer run briefly.
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// Set shutdown flag — producer should stop delivering frames.
	state.shuttingDown.store(true, std::memory_order_release);

	// Small delay to ensure the producer sees the flag.
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	// Teardown while producer is alive (but guarded).
	simulateTeardown(state);

	producerDone.store(true, std::memory_order_release);
	producerThread.join();

	// Verify that the producer attempted frames and some were delivered
	// before shutdown, but the teardown completed without issues.
	CHECK(framesAttempted.load() > 0);
	CHECK_EQ(state.vlcFramebufferId, 0u);
	CHECK(!state.isVideoLoaded);

	// Reset and verify pipeline works again.
	state.shuttingDown.store(false, std::memory_order_release);
	resetAll();
	bool ok = simulateSetup(state, 320, 240);
	CHECK(ok);
	CHECK(state.vlcFramebufferId != 0u);

	simulateTeardown(state);
}

static void testMultipleReinitCycles() {
	beginSuite("multiple consecutive reinit cycles");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	const unsigned widths[] = { 640, 1280, 1920, 800, 3840 };
	const unsigned heights[] = { 480, 720, 1080, 600, 2160 };

	for (int cycle = 0; cycle < 5; ++cycle) {
		resetAll();
		bool ok = simulateSetup(state, widths[cycle], heights[cycle]);
		CHECK(ok);
		CHECK_EQ(state.renderWidth, widths[cycle]);
		CHECK_EQ(state.renderHeight, heights[cycle]);

		simulateConsumerRefresh(state);
		for (int f = 0; f < 3; ++f) {
			runProducerFrame(state);
			simulateConsumerRefresh(state);
		}
		CHECK(state.hasReceivedVideoFrame);

		// Simulate reinit: shutdown guard → teardown → clear flag.
		state.shuttingDown.store(true, std::memory_order_release);
		simulateTeardown(state);
		state.shuttingDown.store(false, std::memory_order_release);
		state.hasReceivedVideoFrame = false;

		CHECK_EQ(state.vlcFramebufferId, 0u);
	}
}

static void testTeardownWithPendingFence() {
	beginSuite("teardown with pending fence from last frame");

	resetAll();
	MockVideoState state;
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	simulateSetup(state, 640, 480);
	simulateConsumerRefresh(state);

	// Producer delivers a frame but consumer does NOT read it.
	runProducerFrame(state);
	CHECK(state.publishedVideoFrameFence != nullptr);
	CHECK(state.exposedTextureDirty);

	// Teardown with an outstanding fence — must not leak.
	state.shuttingDown.store(true, std::memory_order_release);
	resetAll();
	simulateTeardown(state);

	CHECK_EQ(state.publishedVideoFrameFence, nullptr);
	CHECK(logContains("glDeleteSync"));
	CHECK(logContains("glDeleteFramebuffers"));

	state.shuttingDown.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testReinitCycle();
	testShutdownGuardDuringTeardown();
	testConcurrentShutdownGuard();
	testMultipleReinitCycles();
	testTeardownWithPendingFence();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
