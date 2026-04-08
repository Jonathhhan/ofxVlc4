#include "test_runner.h"

#include "support/ofxVlc4RingBuffer.h"

#include <cstring>
#include <numeric>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------------------
// Construction and allocation
// ---------------------------------------------------------------------------

TEST(RingBuffer_DefaultConstruct_ZeroSize) {
	// Default size arg is 0; allocate promotes to 2 (next power of two)
	ofxVlc4RingBuffer rb(0);
	EXPECT_GE(rb.size(), static_cast<size_t>(2));
}

TEST(RingBuffer_AllocateSizeIsPowerOfTwo) {
	for (size_t n : { 1u, 3u, 5u, 7u, 12u, 100u }) {
		ofxVlc4RingBuffer rb(n);
		const size_t cap = rb.size();
		EXPECT_GT(cap, static_cast<size_t>(0));
		// Power-of-two check: only one bit set
		EXPECT_EQ(cap & (cap - 1), static_cast<size_t>(0));
		EXPECT_GE(cap, n);
	}
}

TEST(RingBuffer_InitiallyEmpty) {
	ofxVlc4RingBuffer rb(16);
	EXPECT_EQ(rb.getNumReadableSamples(), static_cast<size_t>(0));
	EXPECT_EQ(rb.getNumWritableSamples(), rb.size());
}

// ---------------------------------------------------------------------------
// Basic write / read
// ---------------------------------------------------------------------------

TEST(RingBuffer_WriteAndReadBack) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 1.0f, 2.0f, 3.0f, 4.0f };
	const size_t written = rb.write(src.data(), src.size());
	EXPECT_EQ(written, src.size());
	EXPECT_EQ(rb.getNumReadableSamples(), src.size());

	std::vector<float> dst(src.size(), 0.0f);
	const size_t read = rb.read(dst.data(), dst.size());
	EXPECT_EQ(read, src.size());
	for (size_t i = 0; i < src.size(); ++i) {
		EXPECT_NEAR(dst[i], src[i], 1e-6f);
	}
}

TEST(RingBuffer_ReadEmptyReturnsZeroAndPadsWithSilence) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> dst(4, 99.0f);
	const size_t read = rb.read(dst.data(), dst.size());
	EXPECT_EQ(read, static_cast<size_t>(0));
	for (float v : dst) {
		EXPECT_NEAR(v, 0.0f, 1e-6f);
	}
}

TEST(RingBuffer_PartialReadPadsSilence) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 1.0f, 2.0f };
	rb.write(src.data(), src.size());

	std::vector<float> dst(6, 99.0f);
	const size_t read = rb.read(dst.data(), dst.size());
	EXPECT_EQ(read, static_cast<size_t>(2));
	EXPECT_NEAR(dst[0], 1.0f, 1e-6f);
	EXPECT_NEAR(dst[1], 2.0f, 1e-6f);
	// remaining positions should be zero
	for (size_t i = 2; i < dst.size(); ++i) {
		EXPECT_NEAR(dst[i], 0.0f, 1e-6f);
	}
}

TEST(RingBuffer_WriteNullReturnsZero) {
	ofxVlc4RingBuffer rb(16);
	EXPECT_EQ(rb.write(nullptr, 4), static_cast<size_t>(0));
}

TEST(RingBuffer_WriteZeroCountReturnsZero) {
	ofxVlc4RingBuffer rb(16);
	float v = 1.0f;
	EXPECT_EQ(rb.write(&v, 0), static_cast<size_t>(0));
}

TEST(RingBuffer_ReadNullReturnsZero) {
	ofxVlc4RingBuffer rb(16);
	float v = 1.0f;
	rb.write(&v, 1);
	EXPECT_EQ(rb.read(nullptr, 1), static_cast<size_t>(0));
}

// ---------------------------------------------------------------------------
// Capacity accounting
// ---------------------------------------------------------------------------

TEST(RingBuffer_WritableAndReadableConsistency) {
	ofxVlc4RingBuffer rb(16);
	const size_t cap = rb.size();
	std::vector<float> src(cap / 2);
	std::iota(src.begin(), src.end(), 0.0f);

	rb.write(src.data(), src.size());
	EXPECT_EQ(rb.getNumReadableSamples(), cap / 2);
	EXPECT_EQ(rb.getNumWritableSamples(), cap - cap / 2);
}

// ---------------------------------------------------------------------------
// Wrap-around (circular behaviour)
// ---------------------------------------------------------------------------

TEST(RingBuffer_WrapAroundWrite) {
	// Use a small buffer so wrap-around is triggered quickly.
	ofxVlc4RingBuffer rb(4); // allocated to exactly 4
	const size_t cap = rb.size();
	EXPECT_EQ(cap, static_cast<size_t>(4));

	// Fill the buffer completely
	std::vector<float> fill = { 10.0f, 20.0f, 30.0f, 40.0f };
	EXPECT_EQ(rb.write(fill.data(), 4), static_cast<size_t>(4));

	// Consume half to free space
	std::vector<float> tmp(2);
	rb.read(tmp.data(), 2);

	// Write two more values – they wrap around the internal array
	std::vector<float> extra = { 50.0f, 60.0f };
	EXPECT_EQ(rb.write(extra.data(), 2), static_cast<size_t>(2));

	// Read all 4 remaining samples and verify order
	std::vector<float> out(4);
	rb.read(out.data(), 4);
	EXPECT_NEAR(out[0], 30.0f, 1e-6f);
	EXPECT_NEAR(out[1], 40.0f, 1e-6f);
	EXPECT_NEAR(out[2], 50.0f, 1e-6f);
	EXPECT_NEAR(out[3], 60.0f, 1e-6f);
}

TEST(RingBuffer_MultipleFullCycles) {
	ofxVlc4RingBuffer rb(8);
	const size_t cap = rb.size();

	for (int cycle = 0; cycle < 5; ++cycle) {
		std::vector<float> src(cap);
		std::iota(src.begin(), src.end(), static_cast<float>(cycle * 100));
		rb.write(src.data(), cap);

		std::vector<float> dst(cap);
		const size_t read = rb.read(dst.data(), cap);
		EXPECT_EQ(read, cap);
		for (size_t i = 0; i < cap; ++i) {
			EXPECT_NEAR(dst[i], src[i], 1e-6f);
		}
	}
}

// ---------------------------------------------------------------------------
// Overrun detection
// ---------------------------------------------------------------------------

TEST(RingBuffer_OverrunCountsWhenFull) {
	ofxVlc4RingBuffer rb(4);
	const size_t cap = rb.size();

	// Fill to capacity
	std::vector<float> src(cap, 1.0f);
	rb.write(src.data(), cap);

	// Writing to a full buffer should overrun
	float extra = 5.0f;
	const size_t written = rb.write(&extra, 1);
	EXPECT_EQ(written, static_cast<size_t>(0));
	EXPECT_GT(rb.getOverrunCount(), static_cast<uint64_t>(0));
}

TEST(RingBuffer_OverrunClearedAfterAllocate) {
	ofxVlc4RingBuffer rb(4);
	std::vector<float> src(rb.size(), 1.0f);
	rb.write(src.data(), src.size());
	float extra = 5.0f;
	rb.write(&extra, 1);
	EXPECT_GT(rb.getOverrunCount(), static_cast<uint64_t>(0));

	rb.allocate(4);
	EXPECT_EQ(rb.getOverrunCount(), static_cast<uint64_t>(0));
}

// ---------------------------------------------------------------------------
// Underrun detection
// ---------------------------------------------------------------------------

TEST(RingBuffer_UnderrunCountsWhenEmpty) {
	ofxVlc4RingBuffer rb(4);
	std::vector<float> dst(4);
	rb.read(dst.data(), 4);
	EXPECT_GT(rb.getUnderrunCount(), static_cast<uint64_t>(0));
}

TEST(RingBuffer_UnderrunClearedAfterAllocate) {
	ofxVlc4RingBuffer rb(4);
	std::vector<float> dst(4);
	rb.read(dst.data(), 4);
	EXPECT_GT(rb.getUnderrunCount(), static_cast<uint64_t>(0));

	rb.allocate(4);
	EXPECT_EQ(rb.getUnderrunCount(), static_cast<uint64_t>(0));
}

// ---------------------------------------------------------------------------
// Read with gain
// ---------------------------------------------------------------------------

TEST(RingBuffer_ReadWithGainScalesSamples) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 1.0f, 2.0f, 4.0f };
	rb.write(src.data(), src.size());

	std::vector<float> dst(src.size(), 0.0f);
	rb.read(dst.data(), dst.size(), 2.0f);

	EXPECT_NEAR(dst[0], 2.0f, 1e-6f);
	EXPECT_NEAR(dst[1], 4.0f, 1e-6f);
	EXPECT_NEAR(dst[2], 8.0f, 1e-6f);
}

TEST(RingBuffer_ReadWithGainOneIsTransparent) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 0.5f, 1.0f };
	rb.write(src.data(), src.size());

	std::vector<float> dst(src.size(), 0.0f);
	rb.read(dst.data(), dst.size(), 1.0f);

	EXPECT_NEAR(dst[0], 0.5f, 1e-6f);
	EXPECT_NEAR(dst[1], 1.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// peekLatest
// ---------------------------------------------------------------------------

TEST(RingBuffer_PeekLatestReadsWithoutConsuming) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 1.0f, 2.0f, 3.0f };
	rb.write(src.data(), src.size());

	const size_t readable_before = rb.getNumReadableSamples();

	std::vector<float> peeked(src.size(), 0.0f);
	const size_t copied = rb.peekLatest(peeked.data(), peeked.size());
	EXPECT_EQ(copied, src.size());

	// Readable count unchanged after peek
	EXPECT_EQ(rb.getNumReadableSamples(), readable_before);

	for (size_t i = 0; i < src.size(); ++i) {
		EXPECT_NEAR(peeked[i], src[i], 1e-6f);
	}
}

TEST(RingBuffer_PeekLatestEmptyReturnsZeroAndPads) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> dst(4, 99.0f);
	const size_t copied = rb.peekLatest(dst.data(), dst.size());
	EXPECT_EQ(copied, static_cast<size_t>(0));
	for (float v : dst) {
		EXPECT_NEAR(v, 0.0f, 1e-6f);
	}
}

TEST(RingBuffer_PeekLatestWithGainScalesSamples) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 2.0f, 4.0f };
	rb.write(src.data(), src.size());

	std::vector<float> dst(src.size(), 0.0f);
	rb.peekLatest(dst.data(), dst.size(), 0.5f);

	EXPECT_NEAR(dst[0], 1.0f, 1e-6f);
	EXPECT_NEAR(dst[1], 2.0f, 1e-6f);
}

TEST(RingBuffer_PeekLatestReturnsNewestSamples) {
	ofxVlc4RingBuffer rb(8);
	// Write 6 samples so the buffer has 6 readable
	std::vector<float> src = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
	rb.write(src.data(), src.size());

	// Peek only the latest 3 — should be the last 3 written
	std::vector<float> dst(3, 0.0f);
	const size_t copied = rb.peekLatest(dst.data(), 3);
	EXPECT_EQ(copied, static_cast<size_t>(3));
	EXPECT_NEAR(dst[0], 4.0f, 1e-6f);
	EXPECT_NEAR(dst[1], 5.0f, 1e-6f);
	EXPECT_NEAR(dst[2], 6.0f, 1e-6f);
}

TEST(RingBuffer_PeekLatestWrappedBuffer) {
	ofxVlc4RingBuffer rb(4);
	// Fill and drain to advance the write/read pointers past the wrap point
	std::vector<float> phase1 = { 10.0f, 20.0f, 30.0f, 40.0f };
	rb.write(phase1.data(), 4);
	std::vector<float> tmp(2);
	rb.read(tmp.data(), 2); // consume 2, freeing space

	// Now write 2 more so they wrap around
	std::vector<float> phase2 = { 50.0f, 60.0f };
	rb.write(phase2.data(), 2);

	// peekLatest(4) should give: 30, 40, 50, 60
	std::vector<float> dst(4, 0.0f);
	rb.peekLatest(dst.data(), 4);
	EXPECT_NEAR(dst[0], 30.0f, 1e-6f);
	EXPECT_NEAR(dst[1], 40.0f, 1e-6f);
	EXPECT_NEAR(dst[2], 50.0f, 1e-6f);
	EXPECT_NEAR(dst[3], 60.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// clear and reset
// ---------------------------------------------------------------------------

TEST(RingBuffer_ClearResetsPositionsAndZerosFill) {
	ofxVlc4RingBuffer rb(8);
	std::vector<float> src = { 1.0f, 2.0f, 3.0f };
	rb.write(src.data(), src.size());

	rb.clear();

	EXPECT_EQ(rb.getNumReadableSamples(), static_cast<size_t>(0));
	EXPECT_EQ(rb.getNumWritableSamples(), rb.size());
}

TEST(RingBuffer_ResetDoesNotClearFillButResetsPositions) {
	ofxVlc4RingBuffer rb(8);
	std::vector<float> src = { 1.0f, 2.0f };
	rb.write(src.data(), src.size());
	EXPECT_GT(rb.getNumReadableSamples(), static_cast<size_t>(0));

	rb.reset();

	// After reset, read and write positions are both 0
	EXPECT_EQ(rb.getNumReadableSamples(), static_cast<size_t>(0));
}

TEST(RingBuffer_VersionIncreasesOnWrite) {
	ofxVlc4RingBuffer rb(16);
	const uint64_t v0 = rb.getVersion();
	float v = 1.0f;
	rb.write(&v, 1);
	EXPECT_GT(rb.getVersion(), v0);
}

TEST(RingBuffer_VersionIncreasesOnAllocate) {
	ofxVlc4RingBuffer rb(16);
	const uint64_t v0 = rb.getVersion();
	rb.allocate(16);
	EXPECT_GT(rb.getVersion(), v0);
}

// ---------------------------------------------------------------------------
// ofSoundBuffer interface
// ---------------------------------------------------------------------------

TEST(RingBuffer_ReadIntoBuffer) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 3.0f, 6.0f, 9.0f };
	rb.write(src.data(), src.size());

	ofSoundBuffer sb(src.size());
	rb.readIntoBuffer(sb);

	const auto & data = sb.getBuffer();
	EXPECT_NEAR(data[0], 3.0f, 1e-6f);
	EXPECT_NEAR(data[1], 6.0f, 1e-6f);
	EXPECT_NEAR(data[2], 9.0f, 1e-6f);
}

TEST(RingBuffer_ReadIntoBufferWithGain) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 1.0f, 2.0f };
	rb.write(src.data(), src.size());

	ofSoundBuffer sb(src.size());
	rb.readIntoBuffer(sb, 3.0f);

	const auto & data = sb.getBuffer();
	EXPECT_NEAR(data[0], 3.0f, 1e-6f);
	EXPECT_NEAR(data[1], 6.0f, 1e-6f);
}

TEST(RingBuffer_WriteFromBuffer) {
	ofSoundBuffer sb(3);
	auto & data = sb.getBuffer();
	data[0] = 7.0f;
	data[1] = 8.0f;
	data[2] = 9.0f;

	ofxVlc4RingBuffer rb(16);
	rb.writeFromBuffer(sb);

	std::vector<float> dst(3, 0.0f);
	rb.read(dst.data(), dst.size());
	EXPECT_NEAR(dst[0], 7.0f, 1e-6f);
	EXPECT_NEAR(dst[1], 8.0f, 1e-6f);
	EXPECT_NEAR(dst[2], 9.0f, 1e-6f);
}

TEST(RingBuffer_ReadIntoVector) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 5.0f, 10.0f, 15.0f };
	rb.write(src.data(), src.size());

	std::vector<float> dst(src.size(), 0.0f);
	rb.readIntoVector(dst);

	for (size_t i = 0; i < src.size(); ++i) {
		EXPECT_NEAR(dst[i], src[i], 1e-6f);
	}
}

TEST(RingBuffer_ReadIntoVectorWithGain) {
	ofxVlc4RingBuffer rb(16);
	std::vector<float> src = { 1.0f, 2.0f };
	rb.write(src.data(), src.size());

	std::vector<float> dst(src.size(), 0.0f);
	rb.readIntoVector(dst, 4.0f);

	EXPECT_NEAR(dst[0], 4.0f, 1e-6f);
	EXPECT_NEAR(dst[1], 8.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// getReadPosition
// ---------------------------------------------------------------------------

TEST(RingBuffer_GetReadPositionInitiallyZero) {
	ofxVlc4RingBuffer rb(8);
	EXPECT_EQ(rb.getReadPosition(), static_cast<size_t>(0));
}

TEST(RingBuffer_GetReadPositionAdvancesOnRead) {
	ofxVlc4RingBuffer rb(8);
	std::vector<float> src = { 1.0f, 2.0f };
	rb.write(src.data(), src.size());

	std::vector<float> dst(2);
	rb.read(dst.data(), 2);
	// Position should have advanced by 2 (masked by capacity)
	EXPECT_EQ(rb.getReadPosition(), static_cast<size_t>(2));
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main() {
	return TestRunner::runAll();
}
