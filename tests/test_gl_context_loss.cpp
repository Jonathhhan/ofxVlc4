#include <cstdio>
#include <string>

// Pull in the controllable GLFW stub (stubs_gl/GLFW/glfw3.h).
#include "GLFW/glfw3.h"

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

static void check(bool condition, const char * expr, const char * file, int line) {
	if (condition) {
		++g_passed;
		std::printf("  PASS  %s\n", expr);
	} else {
		++g_failed;
		std::printf("  FAIL  %s  (%s:%d)\n", expr, file, line);
	}
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Inline replica of hasCurrentGlContext() from ofxVlc4Utils.h
// (avoids pulling in the full utils header and its heavy deps)
// ---------------------------------------------------------------------------

static bool hasCurrentGlContext() {
	return glfwGetCurrentContext() != nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testNoContext() {
	std::printf("\n[hasCurrentGlContext returns false when null]\n");

	g_glfwCurrentContext = nullptr;
	CHECK(!hasCurrentGlContext());
}

static void testWithContext() {
	std::printf("\n[hasCurrentGlContext returns true when non-null]\n");

	GLFWwindow fakeWindow;
	g_glfwCurrentContext = &fakeWindow;
	CHECK(hasCurrentGlContext());
	g_glfwCurrentContext = nullptr; // reset
}

static void testDeferredCleanup() {
	std::printf("\n[deferred cleanup pattern]\n");

	int cleanupCallCount = 0;

	// Simulate a deferred cleanup function.
	auto tryCleanup = [&]() {
		if (!hasCurrentGlContext()) return;
		++cleanupCallCount;
	};

	// Context is null → cleanup should NOT run.
	g_glfwCurrentContext = nullptr;
	tryCleanup();
	CHECK(cleanupCallCount == 0);

	// Context restored → deferred cleanup runs.
	GLFWwindow fakeWindow;
	g_glfwCurrentContext = &fakeWindow;
	tryCleanup();
	CHECK(cleanupCallCount == 1);

	// Run again → still works with context present.
	tryCleanup();
	CHECK(cleanupCallCount == 2);

	// Context lost again → no cleanup.
	g_glfwCurrentContext = nullptr;
	tryCleanup();
	CHECK(cleanupCallCount == 2);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testNoContext();
	testWithContext();
	testDeferredCleanup();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
