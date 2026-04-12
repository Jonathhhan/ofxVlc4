#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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
// Mock control block (mirrors the ControlBlock pattern in ofxVlc4.h)
// ---------------------------------------------------------------------------

struct MockControlBlock {
	int * owner;
	std::atomic<bool> expired { false };
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testConcurrentShutdown() {
	std::printf("\n[concurrent shutdown stress]\n");

	constexpr int kNumThreads = 16;
	constexpr int kIterations = 100000;

	int ownerData = 42;
	auto controlBlock = std::make_shared<MockControlBlock>();
	controlBlock->owner = &ownerData;

	std::atomic<int> readCountBeforeExpiry { 0 };
	std::atomic<int> readCountAfterExpiry { 0 };
	std::atomic<bool> startFlag { false };

	std::vector<std::thread> threads;
	threads.reserve(kNumThreads);

	for (int t = 0; t < kNumThreads; ++t) {
		threads.emplace_back([&] {
			while (!startFlag.load(std::memory_order_acquire)) {
				// spin until all threads are ready
			}
			for (int i = 0; i < kIterations; ++i) {
				if (controlBlock->expired.load(std::memory_order_acquire)) {
					readCountAfterExpiry.fetch_add(1, std::memory_order_relaxed);
					continue;
				}
				// Simulate callback work: read from owner.
				volatile int value = *controlBlock->owner;
				(void)value;
				readCountBeforeExpiry.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	// Let all threads start, run briefly, then signal shutdown.
	startFlag.store(true, std::memory_order_release);
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	controlBlock->expired.store(true, std::memory_order_release);

	// Join all threads — they should all exit cleanly.
	for (auto & t : threads) {
		t.join();
	}

	int totalReads = readCountBeforeExpiry.load() + readCountAfterExpiry.load();
	CHECK(totalReads == kNumThreads * kIterations);
	CHECK(readCountAfterExpiry.load() > 0);

	std::printf("  reads before expiry: %d\n", readCountBeforeExpiry.load());
	std::printf("  reads after  expiry: %d\n", readCountAfterExpiry.load());
}

static void testNoAccessAfterExpiry() {
	std::printf("\n[no access after expiry is set first]\n");

	int ownerData = 99;
	auto controlBlock = std::make_shared<MockControlBlock>();
	controlBlock->owner = &ownerData;

	// Set expired before spawning threads.
	controlBlock->expired.store(true, std::memory_order_release);

	std::atomic<int> accessCount { 0 };

	constexpr int kNumThreads = 8;
	std::vector<std::thread> threads;
	threads.reserve(kNumThreads);

	for (int t = 0; t < kNumThreads; ++t) {
		threads.emplace_back([&] {
			for (int i = 0; i < 1000; ++i) {
				if (controlBlock->expired.load(std::memory_order_acquire)) {
					continue;
				}
				volatile int value = *controlBlock->owner;
				(void)value;
				accessCount.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	for (auto & t : threads) {
		t.join();
	}

	CHECK(accessCount.load() == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testConcurrentShutdown();
	testNoAccessAfterExpiry();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
