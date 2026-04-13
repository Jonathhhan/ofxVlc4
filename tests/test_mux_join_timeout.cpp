#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
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
// Tests
// ---------------------------------------------------------------------------

static void testWaitForTimeout() {
	std::printf("\n[wait_for returns timeout then ready]\n");

	std::promise<void> promise;
	auto future = promise.get_future();

	std::thread worker([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		promise.set_value();
	});

	// Should timeout after 100ms.
	auto status1 = future.wait_for(std::chrono::milliseconds(100));
	CHECK(status1 == std::future_status::timeout);

	// Should be ready within 1000ms.
	auto status2 = future.wait_for(std::chrono::milliseconds(1000));
	CHECK(status2 == std::future_status::ready);

	worker.join();
}

static void testFutureAlreadySet() {
	std::printf("\n[future already set returns immediately]\n");

	std::promise<void> promise;
	auto future = promise.get_future();

	promise.set_value();

	auto t0 = std::chrono::steady_clock::now();
	auto status = future.wait_for(std::chrono::milliseconds(1000));
	auto elapsed = std::chrono::steady_clock::now() - t0;

	CHECK(status == std::future_status::ready);
	CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 50);
}

static void testTimeoutThenSafeJoin() {
	std::printf("\n[timeout path still performs safe join]\n");

	std::promise<void> promise;
	auto future = promise.get_future();

	std::thread worker([p = std::move(promise)]() mutable {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		p.set_value();
	});

	auto statusTimeout = future.wait_for(std::chrono::milliseconds(25));
	CHECK(statusTimeout == std::future_status::timeout);

	// Even after timeout, policy is to wait for a safe join.
	worker.join();

	auto status = future.wait_for(std::chrono::milliseconds(0));
	CHECK(status == std::future_status::ready);
}

static void testCancelThenJoin() {
	std::printf("\n[cancel signal path joins deterministically]\n");

	std::mutex mutex;
	std::condition_variable cv;
	std::atomic<bool> cancelRequested { false };
	std::promise<void> promise;
	auto future = promise.get_future();

	std::thread worker([&] {
		std::unique_lock<std::mutex> lock(mutex);
		cv.wait(lock, [&] { return cancelRequested.load(std::memory_order_acquire); });
		promise.set_value();
	});

	{
		std::lock_guard<std::mutex> lock(mutex);
		cancelRequested.store(true, std::memory_order_release);
	}
	cv.notify_all();

	auto status = future.wait_for(std::chrono::milliseconds(500));
	CHECK(status == std::future_status::ready);
	worker.join();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testWaitForTimeout();
	testFutureAlreadySet();
	testTimeoutThenSafeJoin();
	testCancelThenJoin();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
