#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

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
// Mock lifecycle state (mirrors callbackDepth / drain fields in ofxVlc4Impl.h)
// ---------------------------------------------------------------------------

struct MockLifecycleState {
	std::atomic<uint32_t> callbackDepth { 0 };
	mutable std::mutex callbackDrainMutex;
	mutable std::condition_variable callbackDrainCv;
};

// Mirrors the underflow-safe decrement logic in ofxVlc4::leaveCallbackScope().
static void leaveCallbackScopeSafe(MockLifecycleState & s) {
	uint32_t prev = s.callbackDepth.load(std::memory_order_acquire);
	while (prev != 0 && !s.callbackDepth.compare_exchange_weak(
		prev,
		prev - 1,
		std::memory_order_acq_rel,
		std::memory_order_acquire)) {
	}
	if (prev == 1) {
		s.callbackDrainCv.notify_all();
	}
}

// Drain helper: wait until callbackDepth reaches 0 or timeout expires.
static bool drainCallbacks(MockLifecycleState & s, std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> lock(s.callbackDrainMutex);
	return s.callbackDrainCv.wait_for(lock, timeout, [&] {
		return s.callbackDepth.load(std::memory_order_acquire) == 0;
	});
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testDrainCompletesWhenDecremented() {
	std::printf("\n[drain completes when decremented]\n");

	MockLifecycleState state;
	state.callbackDepth.store(1, std::memory_order_release);

	std::thread worker([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		leaveCallbackScopeSafe(state);
	});

	auto t0 = std::chrono::steady_clock::now();
	bool drained = drainCallbacks(state, std::chrono::milliseconds(200));
	auto elapsed = std::chrono::steady_clock::now() - t0;

	CHECK(drained);
	CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 200);

	worker.join();
}

static void testDrainTimesOut() {
	std::printf("\n[drain times out when not decremented]\n");

	MockLifecycleState state;
	state.callbackDepth.store(1, std::memory_order_release);

	auto t0 = std::chrono::steady_clock::now();
	bool drained = drainCallbacks(state, std::chrono::milliseconds(50));
	auto elapsed = std::chrono::steady_clock::now() - t0;

	CHECK(!drained);
	CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= 40);
}

static void testDrainReturnsImmediatelyWhenZero() {
	std::printf("\n[drain returns immediately when depth is 0]\n");

	MockLifecycleState state; // callbackDepth defaults to 0

	auto t0 = std::chrono::steady_clock::now();
	bool drained = drainCallbacks(state, std::chrono::milliseconds(1000));
	auto elapsed = std::chrono::steady_clock::now() - t0;

	CHECK(drained);
	CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 50);
}

static void testLeaveDoesNotUnderflowWhenAlreadyZero() {
	std::printf("\n[leave callback scope at zero does not underflow]\n");

	MockLifecycleState state;
	leaveCallbackScopeSafe(state);
	CHECK(state.callbackDepth.load(std::memory_order_acquire) == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testDrainCompletesWhenDecremented();
	testDrainTimesOut();
	testDrainReturnsImmediatelyWhenZero();
	testLeaveDoesNotUnderflowWhenAlreadyZero();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
