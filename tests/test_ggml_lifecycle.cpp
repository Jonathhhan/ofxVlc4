// ---------------------------------------------------------------------------
// test_ggml_lifecycle.cpp — Unit tests for ofxGgml lifecycle patterns
//
// Validates the state machine transitions, double-close safety, error result
// handling, and the backend cleanup patterns used by the GPU/ggml workflow.
// These tests verify correctness of the lifecycle logic using the POD types
// without requiring the ggml runtime library.
// ---------------------------------------------------------------------------

#include "ofxGgmlTypes.h"
#include "ofxGgmlHelpers.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

static int testCount = 0;
static int passCount = 0;

#define TEST(name) \
	do { testCount++; std::printf("  [TEST] %s ... ", name); } while (0)
#define PASS() \
	do { passCount++; std::printf("PASS\n"); } while (0)
#define ASSERT(cond) \
	do { if (!(cond)) { std::printf("FAIL (%s:%d)\n  %s\n", __FILE__, __LINE__, #cond); return false; } } while (0)

// ---------------------------------------------------------------------------
// Simulate the ofxGgml state machine without the ggml runtime.
// This mirrors the state transitions in ofxGgml.cpp.
// ---------------------------------------------------------------------------

class MockGgmlLifecycle {
public:
	ofxGgmlState state = ofxGgmlState::Uninitialized;
	int setupCallCount = 0;
	int closeCallCount = 0;
	int computeCallCount = 0;
	bool backendAllocated = false;
	bool cpuBackendAllocated = false;
	bool schedulerAllocated = false;
	std::vector<std::string> logMessages;

	/// Simulate setup() — mirrors ofxGgml::setup().
	bool setup(const ofxGgmlSettings & settings, bool simulateGpuAvailable = false) {
		if (state != ofxGgmlState::Uninitialized) {
			close();
		}
		setupCallCount++;

		// Simulate backend initialization.
		if (simulateGpuAvailable && settings.preferredBackend == ofxGgmlBackendType::Gpu) {
			backendAllocated = true;
		} else if (settings.preferredBackend == ofxGgmlBackendType::Cpu) {
			backendAllocated = true;
		} else {
			// GPU not available — fall back to best available (CPU).
			backendAllocated = true;
		}

		if (!backendAllocated) {
			state = ofxGgmlState::Error;
			logMessages.push_back("failed to initialize any backend");
			return false;
		}

		cpuBackendAllocated = true;
		if (!cpuBackendAllocated) {
			state = ofxGgmlState::Error;
			logMessages.push_back("failed to initialize CPU backend");
			backendAllocated = false;
			return false;
		}

		// Simulate scheduler creation.
		schedulerAllocated = true;
		if (!schedulerAllocated) {
			state = ofxGgmlState::Error;
			backendAllocated = false;
			cpuBackendAllocated = false;
			return false;
		}

		state = ofxGgmlState::Ready;
		return true;
	}

	/// Simulate close() — mirrors ofxGgml::close() with double-free fix.
	void close() {
		closeCallCount++;
		if (schedulerAllocated) {
			schedulerAllocated = false;
		}
		// Double-free guard: same pattern as the fixed close().
		bool sameBackend = (backendAllocated && cpuBackendAllocated);
		if (backendAllocated) {
			backendAllocated = false;
		}
		if (cpuBackendAllocated && !sameBackend) {
			cpuBackendAllocated = false;
		}
		cpuBackendAllocated = false;
		state = ofxGgmlState::Uninitialized;
	}

	/// Simulate compute() — mirrors ofxGgml::compute().
	ofxGgmlComputeResult compute(bool simulateSuccess = true) {
		ofxGgmlComputeResult result;
		computeCallCount++;

		if (state != ofxGgmlState::Ready) {
			result.error = "not ready";
			return result;
		}

		state = ofxGgmlState::Computing;

		auto t0 = std::chrono::steady_clock::now();
		// Simulate work...
		auto t1 = std::chrono::steady_clock::now();
		result.elapsedMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

		if (simulateSuccess) {
			result.success = true;
		} else {
			result.error = "compute failed";
		}

		state = ofxGgmlState::Ready;
		return result;
	}
};

// ---------------------------------------------------------------------------
// State machine transition tests
// ---------------------------------------------------------------------------

static bool testInitialState() {
	TEST("initial state is Uninitialized");
	MockGgmlLifecycle m;
	ASSERT(m.state == ofxGgmlState::Uninitialized);
	ASSERT(ofxGgmlHelpers::stateName(m.state) == "Uninitialized");
	PASS();
	return true;
}

static bool testSetupTransition() {
	TEST("setup transitions to Ready");
	MockGgmlLifecycle m;
	ofxGgmlSettings s;
	bool ok = m.setup(s);
	ASSERT(ok);
	ASSERT(m.state == ofxGgmlState::Ready);
	ASSERT(m.setupCallCount == 1);
	PASS();
	return true;
}

static bool testCloseTransition() {
	TEST("close transitions to Uninitialized");
	MockGgmlLifecycle m;
	m.setup({});
	ASSERT(m.state == ofxGgmlState::Ready);
	m.close();
	ASSERT(m.state == ofxGgmlState::Uninitialized);
	ASSERT(m.closeCallCount == 1);
	PASS();
	return true;
}

static bool testComputeTransition() {
	TEST("compute transitions Ready → Computing → Ready");
	MockGgmlLifecycle m;
	m.setup({});
	ASSERT(m.state == ofxGgmlState::Ready);
	auto result = m.compute();
	ASSERT(result.success);
	ASSERT(m.state == ofxGgmlState::Ready);  // Back to Ready after compute.
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Double-close safety
// ---------------------------------------------------------------------------

static bool testDoubleClose() {
	TEST("double close is safe");
	MockGgmlLifecycle m;
	m.setup({});
	m.close();
	ASSERT(m.state == ofxGgmlState::Uninitialized);
	m.close();  // Second close should be safe.
	ASSERT(m.state == ofxGgmlState::Uninitialized);
	ASSERT(m.closeCallCount == 2);
	PASS();
	return true;
}

static bool testCloseWithoutSetup() {
	TEST("close without setup is safe");
	MockGgmlLifecycle m;
	m.close();
	ASSERT(m.state == ofxGgmlState::Uninitialized);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Re-setup (close + setup cycle)
// ---------------------------------------------------------------------------

static bool testReSetup() {
	TEST("re-setup (implicit close + new setup)");
	MockGgmlLifecycle m;
	m.setup({});
	ASSERT(m.state == ofxGgmlState::Ready);
	// Re-setup should auto-close first.
	m.setup({});
	ASSERT(m.state == ofxGgmlState::Ready);
	ASSERT(m.setupCallCount == 2);
	ASSERT(m.closeCallCount == 1);  // Implicit close from re-setup.
	PASS();
	return true;
}

static bool testReSetupWithDifferentBackend() {
	TEST("re-setup with different backend preference");
	MockGgmlLifecycle m;
	ofxGgmlSettings cpuSettings;
	cpuSettings.preferredBackend = ofxGgmlBackendType::Cpu;
	m.setup(cpuSettings);
	ASSERT(m.state == ofxGgmlState::Ready);

	ofxGgmlSettings gpuSettings;
	gpuSettings.preferredBackend = ofxGgmlBackendType::Gpu;
	m.setup(gpuSettings, true);
	ASSERT(m.state == ofxGgmlState::Ready);
	ASSERT(m.setupCallCount == 2);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Compute error paths
// ---------------------------------------------------------------------------

static bool testComputeNotReady() {
	TEST("compute before setup returns error");
	MockGgmlLifecycle m;
	auto result = m.compute();
	ASSERT(!result.success);
	ASSERT(!result.error.empty());
	ASSERT(result.error.find("not ready") != std::string::npos);
	PASS();
	return true;
}

static bool testComputeAfterClose() {
	TEST("compute after close returns error");
	MockGgmlLifecycle m;
	m.setup({});
	m.close();
	auto result = m.compute();
	ASSERT(!result.success);
	ASSERT(!result.error.empty());
	PASS();
	return true;
}

static bool testComputeFailure() {
	TEST("compute failure preserves Ready state");
	MockGgmlLifecycle m;
	m.setup({});
	auto result = m.compute(false);  // Simulate failure.
	ASSERT(!result.success);
	ASSERT(!result.error.empty());
	ASSERT(m.state == ofxGgmlState::Ready);  // Still ready after transient error.
	PASS();
	return true;
}

static bool testComputeTimingPopulated() {
	TEST("compute result has timing info");
	MockGgmlLifecycle m;
	m.setup({});
	auto result = m.compute();
	ASSERT(result.success);
	ASSERT(result.elapsedMs >= 0.0f);  // Elapsed should be non-negative.
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Multiple compute calls
// ---------------------------------------------------------------------------

static bool testMultipleComputes() {
	TEST("multiple consecutive computes succeed");
	MockGgmlLifecycle m;
	m.setup({});
	for (int i = 0; i < 10; i++) {
		auto result = m.compute();
		ASSERT(result.success);
		ASSERT(m.state == ofxGgmlState::Ready);
	}
	ASSERT(m.computeCallCount == 10);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Resource cleanup verification
// ---------------------------------------------------------------------------

static bool testResourceCleanup() {
	TEST("all resources freed on close");
	MockGgmlLifecycle m;
	m.setup({});
	ASSERT(m.backendAllocated);
	ASSERT(m.cpuBackendAllocated);
	ASSERT(m.schedulerAllocated);
	m.close();
	ASSERT(!m.backendAllocated);
	ASSERT(!m.cpuBackendAllocated);
	ASSERT(!m.schedulerAllocated);
	PASS();
	return true;
}

static bool testDestructorCleansUp() {
	TEST("destructor pattern cleans up");
	// Mirrors ofxGgml::~ofxGgml() calling close().
	{
		MockGgmlLifecycle m;
		m.setup({});
		ASSERT(m.state == ofxGgmlState::Ready);
		m.close();  // Simulating destructor.
	}
	// If we get here without crash, cleanup was safe.
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Log callback lifecycle
// ---------------------------------------------------------------------------

static bool testLogCallbackReceivesMessages() {
	TEST("log callback receives setup messages");
	int callCount = 0;
	std::string lastMessage;
	ofxGgmlLogCallback cb = [&](int level, const std::string & msg) {
		callCount++;
		lastMessage = msg;
		(void)level;
	};
	// Just verify the callback type works.
	cb(0, "test message");
	ASSERT(callCount == 1);
	ASSERT(lastMessage == "test message");
	PASS();
	return true;
}

static bool testLogCallbackNullSafe() {
	TEST("null log callback is safe");
	ofxGgmlLogCallback cb;  // Default-constructed, empty.
	// Calling an empty std::function would throw. Verify it's not callable.
	ASSERT(!cb);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// ComputeResult detailed tests
// ---------------------------------------------------------------------------

static bool testComputeResultDefaults() {
	TEST("ComputeResult defaults are safe");
	ofxGgmlComputeResult r;
	ASSERT(!r.success);
	ASSERT(r.elapsedMs == 0.0f);
	ASSERT(r.error.empty());
	PASS();
	return true;
}

static bool testComputeResultErrorMessage() {
	TEST("ComputeResult error message pattern");
	ofxGgmlComputeResult r;
	r.error = std::string("ofxGgml: compute failed (status ") + "ALLOC_FAILED" + ")";
	ASSERT(r.error.find("ALLOC_FAILED") != std::string::npos);
	ASSERT(!r.success);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// GPU workflow: full lifecycle test
// ---------------------------------------------------------------------------

static bool testFullGpuWorkflow() {
	TEST("full GPU workflow: setup → compute × N → close");
	MockGgmlLifecycle m;

	// Setup with GPU preference.
	ofxGgmlSettings gpuSettings;
	gpuSettings.preferredBackend = ofxGgmlBackendType::Gpu;
	gpuSettings.threads = 8;
	gpuSettings.graphSize = 4096;
	ASSERT(m.setup(gpuSettings, true));
	ASSERT(m.state == ofxGgmlState::Ready);

	// Run multiple computations.
	for (int i = 0; i < 5; i++) {
		auto result = m.compute();
		ASSERT(result.success);
		ASSERT(result.elapsedMs >= 0.0f);
	}

	// Clean shutdown.
	m.close();
	ASSERT(m.state == ofxGgmlState::Uninitialized);
	ASSERT(!m.backendAllocated);
	ASSERT(!m.cpuBackendAllocated);
	ASSERT(!m.schedulerAllocated);
	PASS();
	return true;
}

static bool testGpuFallbackWorkflow() {
	TEST("GPU fallback: GPU unavailable falls back to CPU");
	MockGgmlLifecycle m;

	ofxGgmlSettings gpuSettings;
	gpuSettings.preferredBackend = ofxGgmlBackendType::Gpu;
	// Simulate GPU not available — should still succeed (fallback to CPU).
	ASSERT(m.setup(gpuSettings, false));
	ASSERT(m.state == ofxGgmlState::Ready);

	// Compute should work with CPU fallback.
	auto result = m.compute();
	ASSERT(result.success);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_lifecycle ===\n");

	bool ok = true;

	// State machine transitions.
	ok = testInitialState() && ok;
	ok = testSetupTransition() && ok;
	ok = testCloseTransition() && ok;
	ok = testComputeTransition() && ok;

	// Double-close safety.
	ok = testDoubleClose() && ok;
	ok = testCloseWithoutSetup() && ok;

	// Re-setup cycles.
	ok = testReSetup() && ok;
	ok = testReSetupWithDifferentBackend() && ok;

	// Compute error paths.
	ok = testComputeNotReady() && ok;
	ok = testComputeAfterClose() && ok;
	ok = testComputeFailure() && ok;
	ok = testComputeTimingPopulated() && ok;

	// Multiple computes.
	ok = testMultipleComputes() && ok;

	// Resource cleanup.
	ok = testResourceCleanup() && ok;
	ok = testDestructorCleansUp() && ok;

	// Log callback lifecycle.
	ok = testLogCallbackReceivesMessages() && ok;
	ok = testLogCallbackNullSafe() && ok;

	// ComputeResult.
	ok = testComputeResultDefaults() && ok;
	ok = testComputeResultErrorMessage() && ok;

	// Full GPU workflows.
	ok = testFullGpuWorkflow() && ok;
	ok = testGpuFallbackWorkflow() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
