// Tests for the GL operation helpers in ofxVlc4GlOps.h.
//
// Verifies that each helper produces the correct GL call sequence by using
// the call-recording stubs in stubs_gl/ and checking the global call log.

#include "ofxVlc4GlOps.h"

#include <cassert>
#include <cstdio>
#include <cstring>

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
// Log query helpers
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

// ---------------------------------------------------------------------------
// Reset helper
// ---------------------------------------------------------------------------

static void resetAll() {
	resetGlStubs();
	g_ofClearCallCount = 0;
}

// ===================================================================
// Fence-sync tests
// ===================================================================

static void testDeleteFenceSync() {
	beginSuite("deleteFenceSync");

	// Null fence – no GL call, pointer stays null.
	{
		resetAll();
		GLsync fence = nullptr;
		ofxVlc4GlOps::deleteFenceSync(fence);
		CHECK_EQ(fence, nullptr);
		CHECK_EQ(logSize(), 0u);
	}

	// Valid fence – glDeleteSync called, pointer set to null.
	{
		resetAll();
		GLsync fence = reinterpret_cast<GLsync>(static_cast<uintptr_t>(42));
		ofxVlc4GlOps::deleteFenceSync(fence);
		CHECK_EQ(fence, nullptr);
		CHECK_EQ(logSize(), 1u);
		CHECK_EQ(logAt(0).name, std::string("glDeleteSync"));
	}
}

static void testInsertFenceSync() {
	beginSuite("insertFenceSync");

	resetAll();
	GLsync fence = ofxVlc4GlOps::insertFenceSync();
	CHECK(fence != nullptr);
	CHECK_EQ(logSize(), 1u);
	CHECK_EQ(logAt(0).name, std::string("glFenceSync"));
	CHECK_EQ(logAt(0).args[0], static_cast<uint64_t>(GL_SYNC_GPU_COMMANDS_COMPLETE));
	CHECK_EQ(logAt(0).args[1], 0u);
}

static void testClientWaitFenceSync() {
	beginSuite("clientWaitFenceSync");

	// Already signaled.
	{
		resetAll();
		g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;
		GLsync fence = reinterpret_cast<GLsync>(static_cast<uintptr_t>(7));
		GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000);
		CHECK_EQ(result, static_cast<GLenum>(GL_ALREADY_SIGNALED));
		CHECK_EQ(logSize(), 1u);
		CHECK_EQ(logAt(0).name, std::string("glClientWaitSync"));
	}

	// Condition satisfied.
	{
		resetAll();
		g_glClientWaitSyncResult = GL_CONDITION_SATISFIED;
		GLsync fence = reinterpret_cast<GLsync>(static_cast<uintptr_t>(7));
		GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fence, 0, 0);
		CHECK_EQ(result, static_cast<GLenum>(GL_CONDITION_SATISFIED));
	}

	// Timeout expired.
	{
		resetAll();
		g_glClientWaitSyncResult = GL_TIMEOUT_EXPIRED;
		GLsync fence = reinterpret_cast<GLsync>(static_cast<uintptr_t>(7));
		GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fence, 0, 500);
		CHECK_EQ(result, static_cast<GLenum>(GL_TIMEOUT_EXPIRED));
	}

	// Wait failed.
	{
		resetAll();
		g_glClientWaitSyncResult = GL_WAIT_FAILED;
		GLsync fence = reinterpret_cast<GLsync>(static_cast<uintptr_t>(7));
		GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fence, 0, 0);
		CHECK_EQ(result, static_cast<GLenum>(GL_WAIT_FAILED));
	}
}

static void testGpuWaitFenceSync() {
	beginSuite("gpuWaitFenceSync");

	resetAll();
	GLsync fence = reinterpret_cast<GLsync>(static_cast<uintptr_t>(99));
	ofxVlc4GlOps::gpuWaitFenceSync(fence);
	CHECK_EQ(logSize(), 1u);
	CHECK_EQ(logAt(0).name, std::string("glWaitSync"));
	CHECK_EQ(logAt(0).args[1], 0u); // flags = 0
	CHECK_EQ(logAt(0).args[2], static_cast<uint64_t>(GL_TIMEOUT_IGNORED));
}

// ===================================================================
// Framebuffer tests
// ===================================================================

static void testSetupFboWithTexture() {
	beginSuite("setupFboWithTexture");

	// texId 0 → early out, no GL calls.
	{
		resetAll();
		GLuint fboId = 0;
		bool ok = ofxVlc4GlOps::setupFboWithTexture(fboId, GL_TEXTURE_2D, 0);
		CHECK(!ok);
		CHECK_EQ(fboId, 0u);
		CHECK_EQ(logSize(), 0u);
	}

	// fboId 0 → generates new FBO, attaches texture, clears, unbinds.
	{
		resetAll();
		GLuint fboId = 0;
		bool ok = ofxVlc4GlOps::setupFboWithTexture(fboId, GL_TEXTURE_2D, 10);
		CHECK(ok);
		CHECK(fboId != 0u);
		CHECK(logContains("glGenFramebuffers"));
		CHECK(logContains("glBindFramebuffer"));
		CHECK(logContains("glFramebufferTexture2D"));
		CHECK(logContains("glDrawBuffer"));
		CHECK_EQ(g_ofClearCallCount, 1);
		// Last call should unbind FBO.
		CHECK_EQ(logAt(logSize() - 1).name, std::string("glBindFramebuffer"));
		CHECK_EQ(logAt(logSize() - 1).args[1], 0u);
	}

	// fboId already set → reuses existing FBO (no glGenFramebuffers).
	{
		resetAll();
		GLuint fboId = 42;
		bool ok = ofxVlc4GlOps::setupFboWithTexture(fboId, GL_TEXTURE_2D, 10);
		CHECK(ok);
		CHECK_EQ(fboId, 42u);
		CHECK(!logContains("glGenFramebuffers"));
		CHECK(logContains("glBindFramebuffer"));
		CHECK(logContains("glFramebufferTexture2D"));
	}

	// glGenFramebuffers fails (returns 0) → returns false.
	{
		resetAll();
		g_glGenFramebuffersFail = true;
		GLuint fboId = 0;
		bool ok = ofxVlc4GlOps::setupFboWithTexture(fboId, GL_TEXTURE_2D, 10);
		CHECK(!ok);
		CHECK_EQ(fboId, 0u);
	}
}

static void testBindFbo() {
	beginSuite("bindFbo");

	// Not dirty → only binds FBO, no reattach.
	{
		resetAll();
		bool dirty = false;
		ofxVlc4GlOps::bindFbo(5, dirty, GL_TEXTURE_2D, 10);
		CHECK(!dirty);
		CHECK_EQ(logSize(), 1u);
		CHECK_EQ(logAt(0).name, std::string("glBindFramebuffer"));
		CHECK_EQ(logAt(0).args[1], 5u);
	}

	// Dirty → binds FBO and reattaches texture, clears dirty.
	{
		resetAll();
		bool dirty = true;
		ofxVlc4GlOps::bindFbo(5, dirty, GL_TEXTURE_2D, 10);
		CHECK(!dirty);
		CHECK_EQ(logSize(), 2u);
		CHECK_EQ(logAt(0).name, std::string("glBindFramebuffer"));
		CHECK_EQ(logAt(1).name, std::string("glFramebufferTexture2D"));
		CHECK_EQ(logAt(1).args[3], 10u); // texture id
	}
}

static void testUnbindFbo() {
	beginSuite("unbindFbo");

	resetAll();
	ofxVlc4GlOps::unbindFbo();
	CHECK_EQ(logSize(), 1u);
	CHECK_EQ(logAt(0).name, std::string("glBindFramebuffer"));
	CHECK_EQ(logAt(0).args[0], static_cast<uint64_t>(GL_FRAMEBUFFER));
	CHECK_EQ(logAt(0).args[1], 0u);
}

static void testDeleteFbo() {
	beginSuite("deleteFbo");

	// fboId 0 → no GL call.
	{
		resetAll();
		GLuint fboId = 0;
		ofxVlc4GlOps::deleteFbo(fboId);
		CHECK_EQ(fboId, 0u);
		CHECK_EQ(logSize(), 0u);
	}

	// fboId non-zero → deletes and resets.
	{
		resetAll();
		GLuint fboId = 77;
		ofxVlc4GlOps::deleteFbo(fboId);
		CHECK_EQ(fboId, 0u);
		CHECK_EQ(logSize(), 1u);
		CHECK_EQ(logAt(0).name, std::string("glDeleteFramebuffers"));
	}
}

// ===================================================================
// Texture tests
// ===================================================================

static void testSetTextureLinearFiltering() {
	beginSuite("setTextureLinearFiltering");

	resetAll();
	ofxVlc4GlOps::setTextureLinearFiltering(GL_TEXTURE_2D, 20);
	CHECK_EQ(logSize(), 4u);
	// bind, set min, set mag, unbind
	CHECK_EQ(logAt(0).name, std::string("glBindTexture"));
	CHECK_EQ(logAt(0).args[1], 20u);
	CHECK_EQ(logAt(1).name, std::string("glTexParameteri"));
	CHECK_EQ(logAt(1).args[1], static_cast<uint64_t>(GL_TEXTURE_MIN_FILTER));
	CHECK_EQ(logAt(1).args[2], static_cast<uint64_t>(GL_LINEAR));
	CHECK_EQ(logAt(2).name, std::string("glTexParameteri"));
	CHECK_EQ(logAt(2).args[1], static_cast<uint64_t>(GL_TEXTURE_MAG_FILTER));
	CHECK_EQ(logAt(2).args[2], static_cast<uint64_t>(GL_LINEAR));
	CHECK_EQ(logAt(3).name, std::string("glBindTexture"));
	CHECK_EQ(logAt(3).args[1], 0u); // unbind
}

// ===================================================================
// Pixel-pack buffer (PBO) tests
// ===================================================================

static void testAllocatePixelPackBuffers() {
	beginSuite("allocatePixelPackBuffers");

	// count 0 → false.
	{
		resetAll();
		std::vector<GLuint> pbos;
		CHECK(!ofxVlc4GlOps::allocatePixelPackBuffers(pbos, 0, 1024));
	}

	// frameBytes 0 → false.
	{
		resetAll();
		std::vector<GLuint> pbos;
		CHECK(!ofxVlc4GlOps::allocatePixelPackBuffers(pbos, 3, 0));
	}

	// Success – 3 buffers.
	{
		resetAll();
		std::vector<GLuint> pbos;
		bool ok = ofxVlc4GlOps::allocatePixelPackBuffers(pbos, 3, 4096);
		CHECK(ok);
		CHECK_EQ(pbos.size(), 3u);
		for (GLuint pbo : pbos) {
			CHECK(pbo != 0u);
		}
		// glGenBuffers(3), 3 × (glBindBuffer + glBufferData), glBindBuffer(0)
		CHECK(logContains("glGenBuffers"));
		CHECK_EQ(logCount("glBindBuffer"), 4u); // 3 bind + 1 unbind
		CHECK_EQ(logCount("glBufferData"), 3u);
	}

	// Failure – glGenBuffers returns 0.
	{
		resetAll();
		g_glGenBuffersFail = true;
		std::vector<GLuint> pbos;
		bool ok = ofxVlc4GlOps::allocatePixelPackBuffers(pbos, 3, 4096);
		CHECK(!ok);
	}
}

static void testDestroyPixelPackBuffers() {
	beginSuite("destroyPixelPackBuffers");

	// With fences and buffers.
	{
		resetAll();
		std::vector<GLuint> pbos = {10, 20, 30};
		std::vector<GLsync> fences = {
			reinterpret_cast<GLsync>(static_cast<uintptr_t>(1)),
			nullptr,
			reinterpret_cast<GLsync>(static_cast<uintptr_t>(3))
		};
		ofxVlc4GlOps::destroyPixelPackBuffers(pbos, fences);
		CHECK(pbos.empty());
		CHECK(fences.empty());
		CHECK_EQ(logCount("glDeleteSync"), 2u);       // 2 non-null fences
		CHECK_EQ(logCount("glBindBuffer"), 1u);        // unbind
		CHECK_EQ(logCount("glDeleteBuffers"), 1u);
	}

	// Both empty – no GL calls.
	{
		resetAll();
		std::vector<GLuint> pbos;
		std::vector<GLsync> fences;
		ofxVlc4GlOps::destroyPixelPackBuffers(pbos, fences);
		CHECK_EQ(logSize(), 0u);
	}
}

static void testSubmitTextureReadback() {
	beginSuite("submitTextureReadback");

	resetAll();
	GLsync fence = ofxVlc4GlOps::submitTextureReadback(50, GL_TEXTURE_2D, 100);
	CHECK(fence != nullptr);
	// Expected sequence: bindBuffer, bindTexture, getTexImage, unbindTexture,
	//                    unbindBuffer, fenceSync.
	CHECK_EQ(logSize(), 6u);
	CHECK_EQ(logAt(0).name, std::string("glBindBuffer"));
	CHECK_EQ(logAt(0).args[0], static_cast<uint64_t>(GL_PIXEL_PACK_BUFFER));
	CHECK_EQ(logAt(0).args[1], 50u);
	CHECK_EQ(logAt(1).name, std::string("glBindTexture"));
	CHECK_EQ(logAt(1).args[1], 100u);
	CHECK_EQ(logAt(2).name, std::string("glGetTexImage"));
	CHECK_EQ(logAt(3).name, std::string("glBindTexture"));
	CHECK_EQ(logAt(3).args[1], 0u); // unbind
	CHECK_EQ(logAt(4).name, std::string("glBindBuffer"));
	CHECK_EQ(logAt(4).args[1], 0u); // unbind
	CHECK_EQ(logAt(5).name, std::string("glFenceSync"));
}

static void testMapPixelPackBuffer() {
	beginSuite("mapPixelPackBuffer");

	// Success.
	{
		resetAll();
		static char fakeData[16] = {};
		g_glMapBufferResult = fakeData;
		void * mapped = ofxVlc4GlOps::mapPixelPackBuffer(55);
		CHECK_EQ(mapped, static_cast<void *>(fakeData));
		// Only bind + map – no unbind because mapping succeeded.
		CHECK_EQ(logSize(), 2u);
		CHECK_EQ(logAt(0).name, std::string("glBindBuffer"));
		CHECK_EQ(logAt(1).name, std::string("glMapBuffer"));
	}

	// Failure (map returns null) → automatically unbinds.
	{
		resetAll();
		g_glMapBufferResult = nullptr;
		void * mapped = ofxVlc4GlOps::mapPixelPackBuffer(55);
		CHECK_EQ(mapped, nullptr);
		// bind + map + unbind
		CHECK_EQ(logSize(), 3u);
		CHECK_EQ(logAt(2).name, std::string("glBindBuffer"));
		CHECK_EQ(logAt(2).args[1], 0u); // unbind
	}
}

static void testUnmapPixelPackBuffer() {
	beginSuite("unmapPixelPackBuffer");

	resetAll();
	ofxVlc4GlOps::unmapPixelPackBuffer();
	CHECK_EQ(logSize(), 2u);
	CHECK_EQ(logAt(0).name, std::string("glUnmapBuffer"));
	CHECK_EQ(logAt(0).args[0], static_cast<uint64_t>(GL_PIXEL_PACK_BUFFER));
	CHECK_EQ(logAt(1).name, std::string("glBindBuffer"));
	CHECK_EQ(logAt(1).args[1], 0u);
}

// ===================================================================
// Miscellaneous tests
// ===================================================================

static void testFlushCommands() {
	beginSuite("flushCommands");

	resetAll();
	ofxVlc4GlOps::flushCommands();
	CHECK_EQ(logSize(), 1u);
	CHECK_EQ(logAt(0).name, std::string("glFlush"));
}

// ===================================================================
// Integration-style tests – verify composite patterns
// ===================================================================

// Playback pattern: create fence → wait (success) → delete.
static void testPlaybackFenceLifecycle() {
	beginSuite("playback fence lifecycle");

	resetAll();
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;

	// Insert fence (videoSwap).
	GLsync fence = ofxVlc4GlOps::insertFenceSync();
	CHECK(fence != nullptr);

	// Wait on the consumer side (drawCurrentFrame path).
	GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
	CHECK_EQ(result, static_cast<GLenum>(GL_ALREADY_SIGNALED));

	// Delete the fence.
	ofxVlc4GlOps::deleteFenceSync(fence);
	CHECK_EQ(fence, nullptr);

	// Verify call sequence.
	CHECK_EQ(logCount("glFenceSync"), 1u);
	CHECK_EQ(logCount("glClientWaitSync"), 1u);
	CHECK_EQ(logCount("glDeleteSync"), 1u);
}

// Playback pattern: fence wait times out → hand to GPU → delete.
static void testPlaybackFenceTimeout() {
	beginSuite("playback fence timeout");

	resetAll();
	g_glClientWaitSyncResult = GL_TIMEOUT_EXPIRED;

	GLsync fence = ofxVlc4GlOps::insertFenceSync();
	GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL);
	CHECK_EQ(result, static_cast<GLenum>(GL_TIMEOUT_EXPIRED));

	// Hand to GPU pipeline.
	ofxVlc4GlOps::gpuWaitFenceSync(fence);
	ofxVlc4GlOps::deleteFenceSync(fence);

	CHECK_EQ(logCount("glFenceSync"), 1u);
	CHECK_EQ(logCount("glClientWaitSync"), 1u);
	CHECK_EQ(logCount("glWaitSync"), 1u);
	CHECK_EQ(logCount("glDeleteSync"), 1u);
}

// Playback pattern: FBO setup → bind (dirty) → bind (clean) → unbind → delete.
static void testPlaybackFboLifecycle() {
	beginSuite("playback FBO lifecycle");

	resetAll();

	GLuint fboId = 0;
	bool ok = ofxVlc4GlOps::setupFboWithTexture(fboId, GL_TEXTURE_2D, 100);
	CHECK(ok);
	CHECK(fboId != 0u);

	// First bind with dirty flag.
	bool dirty = true;
	ofxVlc4GlOps::bindFbo(fboId, dirty, GL_TEXTURE_2D, 100);
	CHECK(!dirty);

	// Second bind – not dirty.
	ofxVlc4GlOps::bindFbo(fboId, dirty, GL_TEXTURE_2D, 100);
	CHECK(!dirty);

	ofxVlc4GlOps::unbindFbo();
	ofxVlc4GlOps::deleteFbo(fboId);
	CHECK_EQ(fboId, 0u);

	// Verify key calls present.
	CHECK(logContains("glGenFramebuffers"));
	CHECK(logContains("glDeleteFramebuffers"));
	CHECK_EQ(logCount("glFramebufferTexture2D"), 2u); // setup + dirty bind
}

// Recording pattern: allocate PBOs → submit readback → map → unmap → destroy.
static void testRecordingPboLifecycle() {
	beginSuite("recording PBO lifecycle");

	resetAll();
	static char fakePixels[64] = {};
	g_glMapBufferResult = fakePixels;

	// Allocate.
	std::vector<GLuint> pbos;
	std::vector<GLsync> fences;
	bool ok = ofxVlc4GlOps::allocatePixelPackBuffers(pbos, 2, 1024);
	CHECK(ok);
	CHECK_EQ(pbos.size(), 2u);
	fences.assign(2, nullptr);

	// Submit readback into first PBO.
	fences[0] = ofxVlc4GlOps::submitTextureReadback(pbos[0], GL_TEXTURE_2D, 200);
	CHECK(fences[0] != nullptr);

	// Wait for fence.
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;
	GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fences[0], 0, 0);
	CHECK_EQ(result, static_cast<GLenum>(GL_ALREADY_SIGNALED));

	// Map and read.
	void * mapped = ofxVlc4GlOps::mapPixelPackBuffer(pbos[0]);
	CHECK(mapped != nullptr);
	ofxVlc4GlOps::unmapPixelPackBuffer();

	// Clean up fence.
	ofxVlc4GlOps::deleteFenceSync(fences[0]);

	// Destroy all.
	ofxVlc4GlOps::destroyPixelPackBuffers(pbos, fences);
	CHECK(pbos.empty());
	CHECK(fences.empty());

	// Verify key calls.
	CHECK(logContains("glGenBuffers"));
	CHECK(logContains("glGetTexImage"));
	CHECK(logContains("glMapBuffer"));
	CHECK(logContains("glUnmapBuffer"));
	CHECK(logContains("glDeleteBuffers"));
}

// Recording pattern: submit then fence wait fails.
static void testRecordingFenceWaitFailed() {
	beginSuite("recording fence wait failed");

	resetAll();
	g_glClientWaitSyncResult = GL_WAIT_FAILED;

	GLsync fence = ofxVlc4GlOps::insertFenceSync();
	GLenum result = ofxVlc4GlOps::clientWaitFenceSync(fence, 0, 0);
	CHECK_EQ(result, static_cast<GLenum>(GL_WAIT_FAILED));

	// Fence was not deleted – caller still owns it.
	CHECK(fence != nullptr);
	ofxVlc4GlOps::deleteFenceSync(fence);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	// Unit tests.
	testDeleteFenceSync();
	testInsertFenceSync();
	testClientWaitFenceSync();
	testGpuWaitFenceSync();
	testSetupFboWithTexture();
	testBindFbo();
	testUnbindFbo();
	testDeleteFbo();
	testSetTextureLinearFiltering();
	testAllocatePixelPackBuffers();
	testDestroyPixelPackBuffers();
	testSubmitTextureReadback();
	testMapPixelPackBuffer();
	testUnmapPixelPackBuffer();
	testFlushCommands();

	// Integration tests.
	testPlaybackFenceLifecycle();
	testPlaybackFenceTimeout();
	testPlaybackFboLifecycle();
	testRecordingPboLifecycle();
	testRecordingFenceWaitFailed();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
