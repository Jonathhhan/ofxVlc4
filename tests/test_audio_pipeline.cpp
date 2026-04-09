// Mock-based pipeline test that exercises the audio callback → ring buffer →
// read chain without any real VLC runtime.  This validates the core data path
// that all audio features depend on.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// Stubs — we only need the ring buffer and its ofSoundBuffer dependency.
// ---------------------------------------------------------------------------

#include "ofSoundBuffer.h"
#include "ofxVlc4RingBuffer.h"

// ---------------------------------------------------------------------------
// Minimal test harness
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

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Simulated audio callback — mirrors what VLC delivers in audioPlay().
//
// In the real addon, static audioPlay() receives raw PCM from VLC, the
// AudioComponent converts to float, and writes to the ring buffer.
// Here we skip the conversion (VLC delivers float when AudioCaptureSampleFormat
// is Float32) and directly write to the ring buffer.
// ---------------------------------------------------------------------------

static void simulateAudioCallback(
	ofxVlc4RingBuffer & ringBuffer,
	const float * samples,
	size_t frameCount,
	int channels) {

	const size_t sampleCount = frameCount * static_cast<size_t>(channels);
	ringBuffer.write(samples, sampleCount);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testBasicPipeline() {
	beginSuite("basic callback → buffer → read pipeline");

	const int channels = 2;
	const int sampleRate = 44100;
	const size_t bufferCapacity = static_cast<size_t>(sampleRate) * channels;

	ofxVlc4RingBuffer ring;
	ring.allocate(bufferCapacity);
	CHECK(ring.size() >= bufferCapacity);

	// Generate a test tone — 480 frames (stereo interleaved) of a 440Hz sine.
	const size_t frameCount = 480;
	std::vector<float> toneSamples(frameCount * channels);
	for (size_t i = 0; i < frameCount; ++i) {
		const float sample = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / static_cast<float>(sampleRate));
		toneSamples[i * channels + 0] = sample;
		toneSamples[i * channels + 1] = sample;
	}

	// Simulate the VLC audio callback writing into the ring buffer.
	simulateAudioCallback(ring, toneSamples.data(), frameCount, channels);

	// The ring buffer should now contain exactly frameCount * channels samples.
	CHECK(ring.getNumReadableSamples() == frameCount * channels);
	CHECK(ring.getOverrunCount() == 0);
	CHECK(ring.getUnderrunCount() == 0);

	// Read the data back out — mirrors readAudioIntoBuffer().
	std::vector<float> readBack(frameCount * channels, 0.0f);
	const size_t readCount = ring.read(readBack.data(), readBack.size());
	CHECK(readCount == frameCount * channels);

	// Verify the round-tripped data matches.
	bool match = true;
	for (size_t i = 0; i < toneSamples.size(); ++i) {
		if (std::fabs(readBack[i] - toneSamples[i]) > 1e-6f) {
			match = false;
			break;
		}
	}
	CHECK(match);
}

static void testMultipleCallbacks() {
	beginSuite("multiple callbacks accumulate in buffer");

	const int channels = 2;
	const int sampleRate = 44100;
	const size_t bufferCapacity = static_cast<size_t>(sampleRate) * channels;

	ofxVlc4RingBuffer ring;
	ring.allocate(bufferCapacity);

	// Simulate 10 small callbacks of 128 frames each.
	const size_t framesPerCallback = 128;
	const int numCallbacks = 10;
	std::vector<float> chunk(framesPerCallback * channels);

	for (int cb = 0; cb < numCallbacks; ++cb) {
		// Fill with a distinctive value for each callback.
		const float value = static_cast<float>(cb + 1) * 0.1f;
		for (auto & s : chunk) s = value;
		simulateAudioCallback(ring, chunk.data(), framesPerCallback, channels);
	}

	const size_t expectedTotal = framesPerCallback * channels * numCallbacks;
	CHECK(ring.getNumReadableSamples() == expectedTotal);

	// Read all accumulated data.
	std::vector<float> readBack(expectedTotal, 0.0f);
	const size_t readCount = ring.read(readBack.data(), readBack.size());
	CHECK(readCount == expectedTotal);

	// First chunk should be ~0.1, last chunk should be ~1.0.
	CHECK(std::fabs(readBack[0] - 0.1f) < 1e-6f);
	CHECK(std::fabs(readBack[expectedTotal - 1] - 1.0f) < 1e-6f);
}

static void testPeekLatest() {
	beginSuite("peekLatest reads without consuming");

	const int channels = 1;
	const size_t bufferCapacity = 1024;

	ofxVlc4RingBuffer ring;
	ring.allocate(bufferCapacity);

	// Write 100 mono samples.
	std::vector<float> samples(100);
	std::iota(samples.begin(), samples.end(), 1.0f);
	ring.write(samples.data(), samples.size());

	CHECK(ring.getNumReadableSamples() == 100);

	// Peek the latest 50 — should not consume.
	std::vector<float> peeked(50, 0.0f);
	const size_t peekedCount = ring.peekLatest(peeked.data(), 50);
	CHECK(peekedCount == 50);

	// After peek, the full 100 samples should still be readable.
	CHECK(ring.getNumReadableSamples() == 100);

	// The peeked data should be the *latest* 50 values (51..100).
	CHECK(std::fabs(peeked[0] - 51.0f) < 1e-6f);
	CHECK(std::fabs(peeked[49] - 100.0f) < 1e-6f);
}

static void testGainAppliedOnRead() {
	beginSuite("gain is applied during read");

	const int channels = 2;
	const size_t bufferCapacity = 4096;

	ofxVlc4RingBuffer ring;
	ring.allocate(bufferCapacity);

	// Write 64 stereo samples with value 1.0.
	const size_t frameCount = 64;
	std::vector<float> ones(frameCount * channels, 1.0f);
	ring.write(ones.data(), ones.size());

	// Read with gain = 0.5.
	std::vector<float> readBack(frameCount * channels, 0.0f);
	const size_t readCount = ring.read(readBack.data(), readBack.size(), 0.5f);
	CHECK(readCount == frameCount * channels);

	// Every sample should be 0.5.
	bool allHalf = true;
	for (float s : readBack) {
		if (std::fabs(s - 0.5f) > 1e-6f) {
			allHalf = false;
			break;
		}
	}
	CHECK(allHalf);
}

static void testOverrunDetection() {
	beginSuite("overrun detection on ring buffer overflow");

	const size_t bufferCapacity = 256;

	ofxVlc4RingBuffer ring;
	ring.allocate(bufferCapacity);

	// Write more data than the buffer can hold.
	std::vector<float> huge(bufferCapacity * 2, 0.5f);
	ring.write(huge.data(), huge.size());

	// Should have at least one overrun.
	CHECK(ring.getOverrunCount() > 0);
}

static void testOfSoundBufferReadPath() {
	beginSuite("readIntoBuffer (ofSoundBuffer path)");

	const int channels = 2;
	const size_t bufferCapacity = 8192;

	ofxVlc4RingBuffer ring;
	ring.allocate(bufferCapacity);

	// Write 256 stereo frames.
	const size_t frameCount = 256;
	std::vector<float> input(frameCount * channels);
	for (size_t i = 0; i < input.size(); ++i) {
		input[i] = static_cast<float>(i) * 0.001f;
	}
	ring.write(input.data(), input.size());

	// Read into an ofSoundBuffer with gain.
	ofSoundBuffer buf(frameCount * channels);
	ring.readIntoBuffer(buf, 2.0f);

	// Verify the first samples have the expected gain applied.
	const auto & data = buf.getBuffer();
	CHECK(std::fabs(data[0] - (0.0f * 2.0f)) < 1e-5f);
	CHECK(std::fabs(data[1] - (0.001f * 2.0f)) < 1e-5f);
	CHECK(std::fabs(data[2] - (0.002f * 2.0f)) < 1e-5f);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testBasicPipeline();
	testMultipleCallbacks();
	testPeekLatest();
	testGainAppliedOnRead();
	testOverrunDetection();
	testOfSoundBufferReadPath();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
