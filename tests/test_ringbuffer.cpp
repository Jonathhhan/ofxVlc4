#include "ofxVlc4RingBuffer.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;
static std::string g_currentSuite;

static void beginSuite(const char * name) {
	g_currentSuite = name;
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

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

static bool nearlyEqual(float a, float b, float eps = 1e-5f) {
	return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testConstruction() {
	beginSuite("construction");

	ofxVlc4RingBuffer rb(0);
	CHECK_EQ(rb.size(), 2u); // minimum capacity is 2 (next power of two ≥ 2)

	ofxVlc4RingBuffer rb2(4);
	CHECK_EQ(rb2.size(), 4u);

	ofxVlc4RingBuffer rb3(5);
	CHECK_EQ(rb3.size(), 8u); // rounds up to next power of two

	ofxVlc4RingBuffer rb4(1024);
	CHECK_EQ(rb4.size(), 1024u);
}

static void testInitialState() {
	beginSuite("initial state");

	ofxVlc4RingBuffer rb(8);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
	CHECK_EQ(rb.getNumWritableSamples(), 8u);
	CHECK_EQ(rb.getOverrunCount(), 0u);
	CHECK_EQ(rb.getUnderrunCount(), 0u);
}

static void testSimpleWriteRead() {
	beginSuite("simple write / read");

	ofxVlc4RingBuffer rb(16);
	const float input[] = { 0.1f, 0.2f, 0.3f, 0.4f };
	const size_t written = rb.write(input, 4);
	CHECK_EQ(written, 4u);
	CHECK_EQ(rb.getNumReadableSamples(), 4u);

	float output[4] = {};
	const size_t read = rb.read(output, 4);
	CHECK_EQ(read, 4u);
	CHECK(nearlyEqual(output[0], 0.1f));
	CHECK(nearlyEqual(output[1], 0.2f));
	CHECK(nearlyEqual(output[2], 0.3f));
	CHECK(nearlyEqual(output[3], 0.4f));
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
}

static void testWrapAround() {
	beginSuite("wrap-around");

	ofxVlc4RingBuffer rb(8);
	// Fill with 6 samples then consume them so write pointer is near the end.
	float fill[6] = { 1, 2, 3, 4, 5, 6 };
	float discard[6] = {};
	rb.write(fill, 6);
	rb.read(discard, 6);

	// Now write across the wrap boundary.
	float input[5] = { 10, 20, 30, 40, 50 };
	const size_t written = rb.write(input, 5);
	CHECK_EQ(written, 5u);

	float output[5] = {};
	const size_t rd = rb.read(output, 5);
	CHECK_EQ(rd, 5u);
	CHECK(nearlyEqual(output[0], 10.0f));
	CHECK(nearlyEqual(output[1], 20.0f));
	CHECK(nearlyEqual(output[2], 30.0f));
	CHECK(nearlyEqual(output[3], 40.0f));
	CHECK(nearlyEqual(output[4], 50.0f));
}

static void testOverrun() {
	beginSuite("overrun");

	ofxVlc4RingBuffer rb(4);
	float data[4] = { 1, 2, 3, 4 };
	// Fill the buffer completely.
	rb.write(data, 4);
	CHECK_EQ(rb.getNumWritableSamples(), 0u);

	// Attempting to write when full → overrun.
	float extra[2] = { 5, 6 };
	const size_t w = rb.write(extra, 2);
	CHECK_EQ(w, 0u);
	CHECK(rb.getOverrunCount() >= 1u);
}

static void testUnderrun() {
	beginSuite("underrun");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 1, 2, 3, 4 };
	rb.write(data, 4);

	// Request more than available.
	float out[8] = {};
	const size_t rd = rb.read(out, 8);
	CHECK_EQ(rd, 4u); // only 4 samples returned
	// The remaining 4 should be zero-padded.
	CHECK(nearlyEqual(out[4], 0.0f));
	CHECK(nearlyEqual(out[7], 0.0f));
	CHECK(rb.getUnderrunCount() >= 1u);
}

static void testReadWithGain() {
	beginSuite("read with gain");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
	rb.write(data, 4);

	float out[4] = {};
	rb.read(out, 4, 2.0f);
	CHECK(nearlyEqual(out[0], 1.0f));
	CHECK(nearlyEqual(out[3], 1.0f));
}

static void testPeekLatest() {
	beginSuite("peekLatest");

	ofxVlc4RingBuffer rb(16);
	float data[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	rb.write(data, 8);

	// Peek at the last 4 samples without consuming any.
	float out[4] = {};
	const size_t peeked = rb.peekLatest(out, 4);
	CHECK_EQ(peeked, 4u);
	CHECK(nearlyEqual(out[0], 5.0f));
	CHECK(nearlyEqual(out[3], 8.0f));

	// Read pointer should not have advanced.
	CHECK_EQ(rb.getNumReadableSamples(), 8u);
}

static void testPeekLatestZeroPad() {
	beginSuite("peekLatest zero-padding");

	ofxVlc4RingBuffer rb(8);
	float data[2] = { 7.0f, 8.0f };
	rb.write(data, 2);

	// Request 4 but only 2 are available → leading 2 should be zero.
	float out[4] = { 9, 9, 9, 9 };
	const size_t peeked = rb.peekLatest(out, 4);
	CHECK_EQ(peeked, 2u);
	CHECK(nearlyEqual(out[0], 0.0f)); // zero-pad
	CHECK(nearlyEqual(out[1], 0.0f)); // zero-pad
	CHECK(nearlyEqual(out[2], 7.0f));
	CHECK(nearlyEqual(out[3], 8.0f));
}

static void testPeekLatestWithGain() {
	beginSuite("peekLatest with gain");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 0.25f, 0.25f, 0.25f, 0.25f };
	rb.write(data, 4);

	float out[4] = {};
	rb.peekLatest(out, 4, 4.0f);
	CHECK(nearlyEqual(out[0], 1.0f));
	CHECK(nearlyEqual(out[3], 1.0f));
}

static void testClear() {
	beginSuite("clear");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 1, 2, 3, 4 };
	rb.write(data, 4);
	CHECK_EQ(rb.getNumReadableSamples(), 4u);

	rb.clear();
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
	CHECK_EQ(rb.getOverrunCount(), 0u);
	CHECK_EQ(rb.getUnderrunCount(), 0u);

	// Should be re-usable after clear.
	rb.write(data, 4);
	CHECK_EQ(rb.getNumReadableSamples(), 4u);
}

static void testReset() {
	beginSuite("reset");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 1, 2, 3, 4 };
	rb.write(data, 4);

	rb.reset();
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
	CHECK_EQ(rb.getOverrunCount(), 0u);
}

static void testVersionBumps() {
	beginSuite("version bumps");

	ofxVlc4RingBuffer rb(8);
	uint64_t v0 = rb.getVersion();

	float data[2] = { 1, 2 };
	rb.write(data, 2);
	uint64_t v1 = rb.getVersion();
	CHECK(v1 > v0);

	rb.clear();
	uint64_t v2 = rb.getVersion();
	CHECK(v2 > v1);

	rb.reset();
	uint64_t v3 = rb.getVersion();
	CHECK(v3 > v2);

	rb.allocate(8);
	uint64_t v4 = rb.getVersion();
	CHECK(v4 > v3);
}

static void testReadIntoVector() {
	beginSuite("readIntoVector");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
	rb.write(data, 4);

	std::vector<float> out(4, 0.0f);
	rb.readIntoVector(out);
	CHECK(nearlyEqual(out[0], 0.1f));
	CHECK(nearlyEqual(out[3], 0.4f));
}

static void testReadIntoVectorWithGain() {
	beginSuite("readIntoVector with gain");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
	rb.write(data, 4);

	std::vector<float> out(4, 0.0f);
	rb.readIntoVector(out, 2.0f);
	CHECK(nearlyEqual(out[0], 1.0f));
	CHECK(nearlyEqual(out[3], 1.0f));
}

static void testReadIntoBuffer() {
	beginSuite("readIntoBuffer");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
	rb.write(data, 4);

	ofSoundBuffer sb(4);
	rb.readIntoBuffer(sb);
	CHECK(nearlyEqual(sb.getBuffer()[0], 0.1f));
	CHECK(nearlyEqual(sb.getBuffer()[3], 0.4f));
}

static void testWriteFromBuffer() {
	beginSuite("writeFromBuffer");

	ofxVlc4RingBuffer rb(8);
	ofSoundBuffer sb(4);
	sb.getBuffer()[0] = 0.1f;
	sb.getBuffer()[1] = 0.2f;
	sb.getBuffer()[2] = 0.3f;
	sb.getBuffer()[3] = 0.4f;
	rb.writeFromBuffer(sb);

	float out[4] = {};
	rb.read(out, 4);
	CHECK(nearlyEqual(out[0], 0.1f));
	CHECK(nearlyEqual(out[3], 0.4f));
}

static void testNullSrcDst() {
	beginSuite("null src/dst guards");

	ofxVlc4RingBuffer rb(8);
	CHECK_EQ(rb.write(nullptr, 4), 0u);
	CHECK_EQ(rb.read(nullptr, 4), 0u);
	CHECK_EQ(rb.peekLatest(nullptr, 4), 0u);
}

static void testZeroCapacity() {
	beginSuite("zero-capacity guards");

	ofxVlc4RingBuffer rb(0);
	// Minimum capacity is 2 after nextPowerOfTwo; write/read should still work.
	float data[1] = { 1.0f };
	CHECK(rb.size() >= 2u);

	ofxVlc4RingBuffer empty;
	// Default-constructed with size=0 also gets capacity 2.
	CHECK(empty.size() >= 2u);
}

static void testAllocateResetsState() {
	beginSuite("allocate resets state");

	ofxVlc4RingBuffer rb(8);
	float data[4] = { 1, 2, 3, 4 };
	rb.write(data, 4);
	CHECK(rb.getNumReadableSamples() > 0u);

	rb.allocate(16);
	CHECK_EQ(rb.size(), 16u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
}

// ---------------------------------------------------------------------------
// Full-buffer coincident-positions cases
//
// When writeStart = readStart + capacity the masked positions coincide
// (writePos == readPos mod capacity) even though the buffer is completely
// full.  readBegin / writeBegin must distinguish this from the empty case.
// ---------------------------------------------------------------------------

static void testFullBufferCoincidentAtZero() {
	beginSuite("full-buffer coincident at index 0");

	// After writing exactly capacity samples from a fresh buffer:
	// writeStart=8, readStart=0  →  writePos=0 & 7 = 0 == readPos=0 & 7 = 0
	ofxVlc4RingBuffer rb(8);

	float src[8];
	for (int i = 0; i < 8; ++i) src[i] = float(i + 1);

	CHECK_EQ(rb.write(src, 8), 8u);
	CHECK_EQ(rb.getNumReadableSamples(), 8u);
	CHECK_EQ(rb.getNumWritableSamples(), 0u);

	// Further writes must be rejected
	float extra[1] = { 99.f };
	CHECK_EQ(rb.write(extra, 1), 0u);
	CHECK(rb.getOverrunCount() >= 1u);

	// All samples must come back in order
	float dst[8] = {};
	CHECK_EQ(rb.read(dst, 8), 8u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
	for (int i = 0; i < 8; ++i)
		CHECK(nearlyEqual(dst[i], float(i + 1)));
}

static void testFullBufferCoincidentAtNonZero() {
	beginSuite("full-buffer coincident at index 5");

	ofxVlc4RingBuffer rb(8);

	// Advance both pointers to index 5
	float advance[5] = {}, discard[5] = {};
	rb.write(advance, 5);
	rb.read(discard, 5);

	CHECK_EQ(rb.getNumReadableSamples(), 0u);
	CHECK_EQ(rb.getNumWritableSamples(), 8u);

	// Fill completely: writeStart=13, readStart=5
	// writePos = 13 & 7 = 5 == readPos = 5 & 7 = 5  →  coincident, full
	float src[8];
	for (int i = 0; i < 8; ++i) src[i] = float(10 + i);

	CHECK_EQ(rb.write(src, 8), 8u);
	CHECK_EQ(rb.getNumReadableSamples(), 8u);
	CHECK_EQ(rb.getNumWritableSamples(), 0u);

	float extra[1] = { 99.f };
	CHECK_EQ(rb.write(extra, 1), 0u);
	CHECK(rb.getOverrunCount() >= 1u);

	// Reading wraps: index 5..7, then 0..4
	float dst[8] = {};
	CHECK_EQ(rb.read(dst, 8), 8u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
	for (int i = 0; i < 8; ++i)
		CHECK(nearlyEqual(dst[i], float(10 + i)));
}

static void testFullBufferCoincidentPartialRead() {
	beginSuite("full-buffer coincident — partial reads");

	ofxVlc4RingBuffer rb(8);

	// Advance to index 6 so the subsequent fill crosses the array end
	float junk[6] = {}, junk2[6] = {};
	rb.write(junk, 6);
	rb.read(junk2, 6);

	float src[8];
	for (int i = 0; i < 8; ++i) src[i] = float(20 + i);
	rb.write(src, 8);
	// writeStart=14, readStart=6  →  writePos=14&7=6 == readPos=6&7=6

	CHECK_EQ(rb.getNumReadableSamples(), 8u);

	float dst[8] = {};
	CHECK_EQ(rb.read(dst, 3), 3u);
	CHECK_EQ(rb.getNumReadableSamples(), 5u);
	CHECK_EQ(rb.read(dst + 3, 5), 5u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);

	for (int i = 0; i < 8; ++i)
		CHECK(nearlyEqual(dst[i], float(20 + i)));
}

static void testFullBufferCoincidentPeekLatest() {
	beginSuite("full-buffer coincident — peekLatest");

	ofxVlc4RingBuffer rb(8);

	float junk[3] = {}, junk2[3] = {};
	rb.write(junk, 3);
	rb.read(junk2, 3);
	// writeStart=3, readStart=3  →  both at index 3

	float src[8];
	for (int i = 0; i < 8; ++i) src[i] = float(30 + i);
	rb.write(src, 8);
	// coincident full buffer at index 3

	float out[4] = {};
	CHECK_EQ(rb.peekLatest(out, 4), 4u);
	CHECK_EQ(rb.getNumReadableSamples(), 8u); // read pointer unchanged
	for (int i = 0; i < 4; ++i)
		CHECK(nearlyEqual(out[i], float(34 + i)));
}

// ---------------------------------------------------------------------------
// Gain edge cases
// ---------------------------------------------------------------------------

static void testReadWithZeroGain() {
	beginSuite("read with zero gain");

	ofxVlc4RingBuffer rb(8);
	float src[] = { 1.0f, 2.0f, 3.0f, 4.0f };
	rb.write(src, 4);

	float dst[4] = { 99.0f, 99.0f, 99.0f, 99.0f };
	CHECK_EQ(rb.read(dst, 4, 0.0f), 4u);
	for (int i = 0; i < 4; ++i) {
		CHECK(nearlyEqual(dst[i], 0.0f));
	}
}

static void testReadWithNegativeGain() {
	beginSuite("read with negative gain");

	ofxVlc4RingBuffer rb(8);
	float src[] = { 1.0f, 2.0f, 3.0f };
	rb.write(src, 3);

	float dst[3] = {};
	CHECK_EQ(rb.read(dst, 3, -1.0f), 3u);
	CHECK(nearlyEqual(dst[0], -1.0f));
	CHECK(nearlyEqual(dst[1], -2.0f));
	CHECK(nearlyEqual(dst[2], -3.0f));
}

static void testReadWithLargeGain() {
	beginSuite("read with large gain");

	ofxVlc4RingBuffer rb(8);
	float src[] = { 0.5f, -0.5f };
	rb.write(src, 2);

	float dst[2] = {};
	CHECK_EQ(rb.read(dst, 2, 100.0f), 2u);
	CHECK(nearlyEqual(dst[0], 50.0f));
	CHECK(nearlyEqual(dst[1], -50.0f));
}

static void testPeekLatestWithZeroGain() {
	beginSuite("peekLatest with zero gain");

	ofxVlc4RingBuffer rb(8);
	float src[] = { 5.0f, 10.0f, 15.0f };
	rb.write(src, 3);

	float dst[3] = { 99.0f, 99.0f, 99.0f };
	CHECK_EQ(rb.peekLatest(dst, 3, 0.0f), 3u);
	for (int i = 0; i < 3; ++i) {
		CHECK(nearlyEqual(dst[i], 0.0f));
	}
}

static void testReadIntoVectorWithZeroGain() {
	beginSuite("readIntoVector with zero gain");

	ofxVlc4RingBuffer rb(8);
	float src[] = { 1.0f, 2.0f, 3.0f, 4.0f };
	rb.write(src, 4);

	std::vector<float> data(4);
	rb.readIntoVector(data, 0.0f);
	for (int i = 0; i < 4; ++i) {
		CHECK(nearlyEqual(data[i], 0.0f));
	}
}

// ---------------------------------------------------------------------------
// Multiple rapid re-allocations
// ---------------------------------------------------------------------------

static void testMultipleReallocations() {
	beginSuite("multiple rapid re-allocations");

	ofxVlc4RingBuffer rb(4);
	CHECK_EQ(rb.size(), 4u);

	float src[] = { 1.0f, 2.0f };
	rb.write(src, 2);
	CHECK_EQ(rb.getNumReadableSamples(), 2u);

	// Re-allocate to larger size — should clear state.
	rb.allocate(16);
	CHECK_EQ(rb.size(), 16u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);

	// Write into new buffer.
	float src2[] = { 10.0f, 20.0f, 30.0f };
	rb.write(src2, 3);
	CHECK_EQ(rb.getNumReadableSamples(), 3u);

	// Re-allocate to smaller size.
	rb.allocate(4);
	CHECK_EQ(rb.size(), 4u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);

	// Re-allocate again.
	rb.allocate(32);
	CHECK_EQ(rb.size(), 32u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);

	// Write and read should work after all reallocations.
	float final_src[] = { 100.0f };
	rb.write(final_src, 1);
	CHECK_EQ(rb.getNumReadableSamples(), 1u);
	float dst[1] = {};
	rb.read(dst, 1);
	CHECK(nearlyEqual(dst[0], 100.0f));
}

// ---------------------------------------------------------------------------
// Read exactly the buffer capacity
// ---------------------------------------------------------------------------

static void testReadExactCapacity() {
	beginSuite("read exact capacity");

	ofxVlc4RingBuffer rb(4);
	float src[] = { 1.0f, 2.0f, 3.0f, 4.0f };
	rb.write(src, 4);
	CHECK_EQ(rb.getNumReadableSamples(), 4u);
	CHECK_EQ(rb.getNumWritableSamples(), 0u);

	float dst[4] = {};
	CHECK_EQ(rb.read(dst, 4), 4u);
	CHECK_EQ(rb.getNumReadableSamples(), 0u);
	CHECK_EQ(rb.getNumWritableSamples(), 4u);
	for (int i = 0; i < 4; ++i) {
		CHECK(nearlyEqual(dst[i], float(i + 1)));
	}
}

// ---------------------------------------------------------------------------
// Overrun counter increments
// ---------------------------------------------------------------------------

static void testOverrunCounterIncrement() {
	beginSuite("overrun counter increment");

	ofxVlc4RingBuffer rb(4);
	float src[] = { 1.0f, 2.0f, 3.0f, 4.0f };
	rb.write(src, 4); // fill completely

	CHECK_EQ(rb.getOverrunCount(), 0u);

	float extra1[] = { 5.0f };
	rb.write(extra1, 1); // overrun 1
	CHECK_EQ(rb.getOverrunCount(), 1u);

	float extra2[] = { 6.0f };
	rb.write(extra2, 1); // overrun 2
	CHECK_EQ(rb.getOverrunCount(), 2u);
}

// ---------------------------------------------------------------------------
// Underrun counter increments
// ---------------------------------------------------------------------------

static void testUnderrunCounterIncrement() {
	beginSuite("underrun counter increment");

	ofxVlc4RingBuffer rb(4);
	CHECK_EQ(rb.getUnderrunCount(), 0u);

	float dst[2] = {};
	rb.read(dst, 2); // underrun 1
	CHECK_EQ(rb.getUnderrunCount(), 1u);

	rb.read(dst, 1); // underrun 2
	CHECK_EQ(rb.getUnderrunCount(), 2u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testConstruction();
	testInitialState();
	testSimpleWriteRead();
	testWrapAround();
	testOverrun();
	testUnderrun();
	testReadWithGain();
	testPeekLatest();
	testPeekLatestZeroPad();
	testPeekLatestWithGain();
	testClear();
	testReset();
	testVersionBumps();
	testReadIntoVector();
	testReadIntoVectorWithGain();
	testReadIntoBuffer();
	testWriteFromBuffer();
	testNullSrcDst();
	testZeroCapacity();
	testAllocateResetsState();
	testFullBufferCoincidentAtZero();
	testFullBufferCoincidentAtNonZero();
	testFullBufferCoincidentPartialRead();
	testFullBufferCoincidentPeekLatest();
	testReadWithZeroGain();
	testReadWithNegativeGain();
	testReadWithLargeGain();
	testPeekLatestWithZeroGain();
	testReadIntoVectorWithZeroGain();
	testMultipleReallocations();
	testReadExactCapacity();
	testOverrunCounterIncrement();
	testUnderrunCounterIncrement();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
