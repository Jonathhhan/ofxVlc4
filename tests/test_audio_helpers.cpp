// Tests for audio helper functions extracted into ofxVlc4AudioHelpers.h.
// These cover format labels, mode labels, normalization, EQ serialization,
// bit reversal, FFT, and atomic max — all pure logic with no runtime deps.

#include "ofxVlc4AudioHelpers.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace ofxVlc4AudioHelpers;

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

#define CHECK(expr)    check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

static bool nearlyEqual(float a, float b, float eps = 1e-4f) {
	return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// audioCaptureSampleFormatLabel
// ---------------------------------------------------------------------------

static void testAudioCaptureSampleFormatLabel() {
	beginSuite("audioCaptureSampleFormatLabel");

	CHECK_EQ(std::string(audioCaptureSampleFormatLabel(AudioCaptureSampleFormat::Float32)), std::string("float32"));
	CHECK_EQ(std::string(audioCaptureSampleFormatLabel(AudioCaptureSampleFormat::Signed16)), std::string("s16"));
	CHECK_EQ(std::string(audioCaptureSampleFormatLabel(AudioCaptureSampleFormat::Signed32)), std::string("s32"));
}

// ---------------------------------------------------------------------------
// audioCaptureSampleFormatCode
// ---------------------------------------------------------------------------

static void testAudioCaptureSampleFormatCode() {
	beginSuite("audioCaptureSampleFormatCode");

	CHECK_EQ(std::string(audioCaptureSampleFormatCode(AudioCaptureSampleFormat::Float32)), std::string("FL32"));
	CHECK_EQ(std::string(audioCaptureSampleFormatCode(AudioCaptureSampleFormat::Signed16)), std::string("S16N"));
	CHECK_EQ(std::string(audioCaptureSampleFormatCode(AudioCaptureSampleFormat::Signed32)), std::string("S32N"));
}

// ---------------------------------------------------------------------------
// normalizeAudioCaptureSampleRate
// ---------------------------------------------------------------------------

static void testNormalizeAudioCaptureSampleRate() {
	beginSuite("normalizeAudioCaptureSampleRate");

	// Valid rates pass through.
	CHECK_EQ(normalizeAudioCaptureSampleRate(22050), 22050);
	CHECK_EQ(normalizeAudioCaptureSampleRate(32000), 32000);
	CHECK_EQ(normalizeAudioCaptureSampleRate(44100), 44100);
	CHECK_EQ(normalizeAudioCaptureSampleRate(48000), 48000);
	CHECK_EQ(normalizeAudioCaptureSampleRate(88200), 88200);
	CHECK_EQ(normalizeAudioCaptureSampleRate(96000), 96000);
	CHECK_EQ(normalizeAudioCaptureSampleRate(192000), 192000);

	// Invalid rates default to 44100.
	CHECK_EQ(normalizeAudioCaptureSampleRate(0), 44100);
	CHECK_EQ(normalizeAudioCaptureSampleRate(11025), 44100);
	CHECK_EQ(normalizeAudioCaptureSampleRate(50000), 44100);
	CHECK_EQ(normalizeAudioCaptureSampleRate(-1), 44100);
}

// ---------------------------------------------------------------------------
// normalizeAudioCaptureChannelCount
// ---------------------------------------------------------------------------

static void testNormalizeAudioCaptureChannelCount() {
	beginSuite("normalizeAudioCaptureChannelCount");

	CHECK_EQ(normalizeAudioCaptureChannelCount(0), 1);
	CHECK_EQ(normalizeAudioCaptureChannelCount(1), 1);
	CHECK_EQ(normalizeAudioCaptureChannelCount(2), 2);
	CHECK_EQ(normalizeAudioCaptureChannelCount(5), 2);
	CHECK_EQ(normalizeAudioCaptureChannelCount(-1), 1);
}

// ---------------------------------------------------------------------------
// normalizeAudioCaptureBufferSeconds
// ---------------------------------------------------------------------------

static void testNormalizeAudioCaptureBufferSeconds() {
	beginSuite("normalizeAudioCaptureBufferSeconds");

	// In-range values pass through.
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(1.0)), 1.0f));
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(0.5)), 0.5f));

	// Below minimum clamps to 0.05.
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(0.01)), 0.05f));
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(0.0)), 0.05f));

	// Above maximum clamps to 5.0.
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(10.0)), 5.0f));

	// NaN and infinity default to 0.75.
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(std::nan(""))), 0.75f));
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(std::numeric_limits<double>::infinity())), 0.75f));
	CHECK(nearlyEqual(static_cast<float>(normalizeAudioCaptureBufferSeconds(-std::numeric_limits<double>::infinity())), 0.75f));
}

// ---------------------------------------------------------------------------
// audioMixModeLabel
// ---------------------------------------------------------------------------

static void testAudioMixModeLabel() {
	beginSuite("audioMixModeLabel");

	CHECK_EQ(std::string(audioMixModeLabel(AudioMixMode::Auto)), std::string("auto"));
	CHECK_EQ(std::string(audioMixModeLabel(AudioMixMode::Stereo)), std::string("stereo"));
	CHECK_EQ(std::string(audioMixModeLabel(AudioMixMode::Binaural)), std::string("binaural"));
	CHECK_EQ(std::string(audioMixModeLabel(AudioMixMode::Surround4_0)), std::string("4.0"));
	CHECK_EQ(std::string(audioMixModeLabel(AudioMixMode::Surround5_1)), std::string("5.1"));
	CHECK_EQ(std::string(audioMixModeLabel(AudioMixMode::Surround7_1)), std::string("7.1"));
}

// ---------------------------------------------------------------------------
// audioStereoModeLabel
// ---------------------------------------------------------------------------

static void testAudioStereoModeLabel() {
	beginSuite("audioStereoModeLabel");

	CHECK_EQ(std::string(audioStereoModeLabel(AudioStereoMode::Auto)), std::string("auto"));
	CHECK_EQ(std::string(audioStereoModeLabel(AudioStereoMode::Stereo)), std::string("stereo"));
	CHECK_EQ(std::string(audioStereoModeLabel(AudioStereoMode::ReverseStereo)), std::string("reverse stereo"));
	CHECK_EQ(std::string(audioStereoModeLabel(AudioStereoMode::Left)), std::string("left"));
	CHECK_EQ(std::string(audioStereoModeLabel(AudioStereoMode::Right)), std::string("right"));
	CHECK_EQ(std::string(audioStereoModeLabel(AudioStereoMode::DolbySurround)), std::string("dolby surround"));
	CHECK_EQ(std::string(audioStereoModeLabel(AudioStereoMode::Mono)), std::string("mono"));
}

// ---------------------------------------------------------------------------
// splitSerializedPreset
// ---------------------------------------------------------------------------

static void testSplitSerializedPreset() {
	beginSuite("splitSerializedPreset");

	// Semicolons as delimiters.
	{
		const auto lines = splitSerializedPreset("preamp=0.0;band0=1.5;band1=-3.0");
		CHECK_EQ(lines.size(), 3u);
		CHECK_EQ(lines[0], "preamp=0.0");
		CHECK_EQ(lines[1], "band0=1.5");
		CHECK_EQ(lines[2], "band1=-3.0");
	}

	// Newlines as delimiters.
	{
		const auto lines = splitSerializedPreset("preamp=0.0\nband0=1.5\nband1=-3.0");
		CHECK_EQ(lines.size(), 3u);
	}

	// Empty lines and whitespace are stripped.
	{
		const auto lines = splitSerializedPreset("  preamp=0.0 ;; ; band0=1.5 ");
		CHECK_EQ(lines.size(), 2u);
		CHECK_EQ(lines[0], "preamp=0.0");
		CHECK_EQ(lines[1], "band0=1.5");
	}

	// Empty string.
	{
		const auto lines = splitSerializedPreset("");
		CHECK(lines.empty());
	}

	// Only whitespace.
	{
		const auto lines = splitSerializedPreset("   ;  ; \n  ");
		CHECK(lines.empty());
	}
}

// ---------------------------------------------------------------------------
// equalizerBandsMatch
// ---------------------------------------------------------------------------

static void testEqualizerBandsMatch() {
	beginSuite("equalizerBandsMatch");

	// Identical vectors.
	{
		std::vector<float> a = { 0.0f, 1.0f, 2.0f };
		std::vector<float> b = { 0.0f, 1.0f, 2.0f };
		CHECK(equalizerBandsMatch(a, b, 0.1f));
	}

	// Within tolerance.
	{
		std::vector<float> a = { 0.0f, 1.0f, 2.0f };
		std::vector<float> b = { 0.05f, 1.05f, 1.95f };
		CHECK(equalizerBandsMatch(a, b, 0.1f));
	}

	// Outside tolerance.
	{
		std::vector<float> a = { 0.0f, 1.0f, 2.0f };
		std::vector<float> b = { 0.0f, 1.0f, 3.0f };
		CHECK(!equalizerBandsMatch(a, b, 0.1f));
	}

	// Different sizes.
	{
		std::vector<float> a = { 0.0f, 1.0f };
		std::vector<float> b = { 0.0f, 1.0f, 2.0f };
		CHECK(!equalizerBandsMatch(a, b, 0.1f));
	}

	// Both empty.
	{
		std::vector<float> a, b;
		CHECK(equalizerBandsMatch(a, b, 0.1f));
	}
}

// ---------------------------------------------------------------------------
// reverseBits
// ---------------------------------------------------------------------------

static void testReverseBits() {
	beginSuite("reverseBits");

	// 0 bits → 0.
	CHECK_EQ(reverseBits(0, 0), 0u);

	// Single bit.
	CHECK_EQ(reverseBits(0, 1), 0u);
	CHECK_EQ(reverseBits(1, 1), 1u);

	// Two bits: 0b10 → 0b01.
	CHECK_EQ(reverseBits(0b10, 2), 0b01u);
	CHECK_EQ(reverseBits(0b11, 2), 0b11u);
	CHECK_EQ(reverseBits(0b01, 2), 0b10u);

	// Three bits: 0b100 → 0b001.
	CHECK_EQ(reverseBits(0b100, 3), 0b001u);
	CHECK_EQ(reverseBits(0b101, 3), 0b101u);
	CHECK_EQ(reverseBits(0b110, 3), 0b011u);

	// Eight bits.
	CHECK_EQ(reverseBits(0b10000000, 8), 0b00000001u);
	CHECK_EQ(reverseBits(0b11000000, 8), 0b00000011u);
}

// ---------------------------------------------------------------------------
// fftInPlace — basic sanity checks
// ---------------------------------------------------------------------------

static void testFftInPlace() {
	beginSuite("fftInPlace");

	// Size 1 → no change.
	{
		std::vector<std::complex<float>> v = { {1.0f, 0.0f} };
		fftInPlace(v);
		CHECK(nearlyEqual(v[0].real(), 1.0f));
	}

	// Non-power-of-two → no change (early return).
	{
		std::vector<std::complex<float>> v = { {1.0f, 0.0f}, {2.0f, 0.0f}, {3.0f, 0.0f} };
		std::vector<std::complex<float>> original = v;
		fftInPlace(v);
		for (size_t i = 0; i < v.size(); ++i) {
			CHECK(nearlyEqual(v[i].real(), original[i].real()));
			CHECK(nearlyEqual(v[i].imag(), original[i].imag()));
		}
	}

	// Size 4 with known DC signal: [1, 1, 1, 1] → FFT should give [4, 0, 0, 0].
	{
		std::vector<std::complex<float>> v = {
			{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}
		};
		fftInPlace(v);
		CHECK(nearlyEqual(v[0].real(), 4.0f));
		CHECK(nearlyEqual(v[0].imag(), 0.0f));
		CHECK(nearlyEqual(v[1].real(), 0.0f, 1e-3f));
		CHECK(nearlyEqual(v[2].real(), 0.0f, 1e-3f));
		CHECK(nearlyEqual(v[3].real(), 0.0f, 1e-3f));
	}

	// Size 4 with alternating signal: [1, -1, 1, -1] → FFT = [0, 0, 4, 0].
	{
		std::vector<std::complex<float>> v = {
			{1.0f, 0.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f}
		};
		fftInPlace(v);
		CHECK(nearlyEqual(v[0].real(), 0.0f, 1e-3f));
		CHECK(nearlyEqual(v[1].real(), 0.0f, 1e-3f));
		CHECK(nearlyEqual(v[2].real(), 4.0f, 1e-3f));
		CHECK(nearlyEqual(v[3].real(), 0.0f, 1e-3f));
	}

	// Size 2: [a, b] → FFT = [a+b, a-b].
	{
		std::vector<std::complex<float>> v = { {3.0f, 0.0f}, {1.0f, 0.0f} };
		fftInPlace(v);
		CHECK(nearlyEqual(v[0].real(), 4.0f));
		CHECK(nearlyEqual(v[1].real(), 2.0f));
	}

	// Parseval's theorem check: energy in time == energy in frequency.
	{
		std::vector<std::complex<float>> v = {
			{1.0f, 0.0f}, {2.0f, 0.0f}, {3.0f, 0.0f}, {4.0f, 0.0f}
		};
		float timeEnergy = 0.0f;
		for (const auto & c : v) {
			timeEnergy += std::norm(c); // |c|^2
		}

		fftInPlace(v);
		float freqEnergy = 0.0f;
		for (const auto & c : v) {
			freqEnergy += std::norm(c);
		}
		freqEnergy /= static_cast<float>(v.size());
		CHECK(nearlyEqual(timeEnergy, freqEnergy, 0.01f));
	}
}

// ---------------------------------------------------------------------------
// updateAtomicMax
// ---------------------------------------------------------------------------

static void testUpdateAtomicMax() {
	beginSuite("updateAtomicMax");

	std::atomic<uint64_t> target(0);

	// Update from 0 to 10.
	updateAtomicMax(target, 10);
	CHECK_EQ(target.load(), 10u);

	// Lower value does not update.
	updateAtomicMax(target, 5);
	CHECK_EQ(target.load(), 10u);

	// Equal value does not update.
	updateAtomicMax(target, 10);
	CHECK_EQ(target.load(), 10u);

	// Higher value updates.
	updateAtomicMax(target, 20);
	CHECK_EQ(target.load(), 20u);

	// Much higher value.
	updateAtomicMax(target, 1000000);
	CHECK_EQ(target.load(), 1000000u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testAudioCaptureSampleFormatLabel();
	testAudioCaptureSampleFormatCode();
	testNormalizeAudioCaptureSampleRate();
	testNormalizeAudioCaptureChannelCount();
	testNormalizeAudioCaptureBufferSeconds();
	testAudioMixModeLabel();
	testAudioStereoModeLabel();
	testSplitSerializedPreset();
	testEqualizerBandsMatch();
	testReverseBits();
	testFftInPlace();
	testUpdateAtomicMax();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
