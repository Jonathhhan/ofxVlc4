#include "ofxVlc4RingBuffer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
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
// Tests
// ---------------------------------------------------------------------------

static void testDropOverflow() {
	std::printf("\n[drop policy overflow]\n");

	ofxVlc4RingBuffer rb(64);
	rb.setOverflowPolicy(OverflowPolicy::Drop);

	// Write more samples than capacity without reading.
	std::vector<float> data(128, 1.0f);
	rb.write(data.data(), data.size());

	CHECK(rb.getOverrunCount() >= 1u);
}

static void testOverflowWarningFlag() {
	std::printf("\n[overflow warning flag]\n");

	ofxVlc4RingBuffer rb(64);
	rb.setOverflowPolicy(OverflowPolicy::Drop);

	// Fill and overflow.
	std::vector<float> data(128, 1.0f);
	rb.write(data.data(), data.size());

	// First call returns true.
	CHECK(rb.wasOverflowWarningIssued() == true);
	// Second call returns false (flag resets).
	CHECK(rb.wasOverflowWarningIssued() == false);
}

static void testExpandOncePolicy() {
	std::printf("\n[ExpandOnce policy]\n");

	ofxVlc4RingBuffer rb(64);
	rb.setOverflowPolicy(OverflowPolicy::ExpandOnce);

	size_t originalSize = rb.size();
	CHECK(originalSize == 64u);

	// Fill the buffer to capacity, then overflow.
	std::vector<float> fill(64, 1.0f);
	rb.write(fill.data(), fill.size());

	// This write should trigger expansion.
	std::vector<float> extra(8, 2.0f);
	rb.write(extra.data(), extra.size());

	CHECK(rb.size() == originalSize * 2);
}

static void testProducerConsumerStress() {
	std::printf("\n[producer/consumer stress]\n");

	ofxVlc4RingBuffer rb(1024);
	rb.setOverflowPolicy(OverflowPolicy::Drop);

	std::atomic<bool> running { true };
	std::atomic<uint64_t> totalWritten { 0 };
	std::atomic<uint64_t> totalRead { 0 };

	// Producer thread.
	std::thread producer([&] {
		std::vector<float> buf(32, 0.5f);
		while (running.load(std::memory_order_relaxed)) {
			size_t n = rb.write(buf.data(), buf.size());
			totalWritten.fetch_add(n, std::memory_order_relaxed);
		}
	});

	// Consumer thread.
	std::thread consumer([&] {
		std::vector<float> buf(32);
		while (running.load(std::memory_order_relaxed)) {
			size_t n = rb.read(buf.data(), buf.size());
			totalRead.fetch_add(n, std::memory_order_relaxed);
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	running.store(false, std::memory_order_relaxed);

	producer.join();
	consumer.join();

	// Verify the test ran and didn't crash.
	CHECK(totalWritten.load() > 0);
	CHECK(totalRead.load() > 0);

	std::printf("  written: %llu  read: %llu  overruns: %llu  underruns: %llu\n",
		(unsigned long long)totalWritten.load(),
		(unsigned long long)totalRead.load(),
		(unsigned long long)rb.getOverrunCount(),
		(unsigned long long)rb.getUnderrunCount());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testDropOverflow();
	testOverflowWarningFlag();
	testExpandOncePolicy();
	testProducerConsumerStress();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
