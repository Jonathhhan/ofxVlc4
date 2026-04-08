// Tests for the GL-context helper functions in ofxVlc4Utils.h:
//   hasCurrentGlContext()
//   clearAllocatedFbo()
//
// Uses a dedicated stub directory (stubs_gl/) so that glfwGetCurrentContext()
// and ofFbo / ofClear can be observed and controlled during the tests.

#include "ofxVlc4Utils.h"

#include <cassert>
#include <cstdio>

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
// Helper: reset all observable state between sub-tests.
// ---------------------------------------------------------------------------

static void resetObservers() {
	g_glfwCurrentContext = nullptr;
	g_ofClearCallCount = 0;
}

// ---------------------------------------------------------------------------
// hasCurrentGlContext
// ---------------------------------------------------------------------------

static void testHasCurrentGlContext() {
	beginSuite("hasCurrentGlContext");

	// No context installed → must return false.
	resetObservers();
	CHECK(!ofxVlc4Utils::hasCurrentGlContext());

	// Set a non-null fake window → must return true.
	static GLFWwindow fakeWindow;
	g_glfwCurrentContext = &fakeWindow;
	CHECK(ofxVlc4Utils::hasCurrentGlContext());

	resetObservers();
}

// ---------------------------------------------------------------------------
// clearAllocatedFbo
// ---------------------------------------------------------------------------

static void testClearAllocatedFbo() {
	beginSuite("clearAllocatedFbo");

	// Case 1: not allocated, no GL context → early-out, nothing happens.
	{
		resetObservers();
		ofFbo fbo;
		fbo.setAllocated(false);
		ofxVlc4Utils::clearAllocatedFbo(fbo);
		CHECK_EQ(fbo.beginCount, 0);
		CHECK_EQ(fbo.endCount, 0);
		CHECK_EQ(g_ofClearCallCount, 0);
	}

	// Case 2: allocated, but no GL context → early-out, nothing happens.
	{
		resetObservers();
		ofFbo fbo;
		fbo.setAllocated(true);
		ofxVlc4Utils::clearAllocatedFbo(fbo);
		CHECK_EQ(fbo.beginCount, 0);
		CHECK_EQ(fbo.endCount, 0);
		CHECK_EQ(g_ofClearCallCount, 0);
	}

	// Case 3: not allocated, GL context present → early-out, nothing happens.
	{
		resetObservers();
		static GLFWwindow fakeWindow;
		g_glfwCurrentContext = &fakeWindow;
		ofFbo fbo;
		fbo.setAllocated(false);
		ofxVlc4Utils::clearAllocatedFbo(fbo);
		CHECK_EQ(fbo.beginCount, 0);
		CHECK_EQ(fbo.endCount, 0);
		CHECK_EQ(g_ofClearCallCount, 0);
	}

	// Case 4: allocated AND GL context present → clears the FBO.
	{
		resetObservers();
		static GLFWwindow fakeWindow;
		g_glfwCurrentContext = &fakeWindow;
		ofFbo fbo;
		fbo.setAllocated(true);
		ofxVlc4Utils::clearAllocatedFbo(fbo);
		CHECK_EQ(fbo.beginCount, 1);
		CHECK_EQ(fbo.endCount, 1);
		CHECK_EQ(g_ofClearCallCount, 1);
	}

	resetObservers();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testHasCurrentGlContext();
	testClearAllocatedFbo();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
