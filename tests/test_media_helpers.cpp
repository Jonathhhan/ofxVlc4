// Tests for media/recording helper functions extracted into
// ofxVlc4MediaHelpers.h.  Covers FOURCC conversion, bitrate/frame-rate/
// bookmark-time formatting, float formatting with trailing-zero stripping,
// and WAV header binary output — all pure logic with no runtime deps.

#include "ofxVlc4MediaHelpers.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace ofxVlc4MediaHelpers;

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

// Helper to read a little-endian uint16 from a buffer.
static uint16_t readU16LE(const char * buf) {
	return static_cast<uint16_t>(
		static_cast<unsigned char>(buf[0]) |
		(static_cast<unsigned char>(buf[1]) << 8));
}

// Helper to read a little-endian uint32 from a buffer.
static uint32_t readU32LE(const char * buf) {
	return static_cast<uint32_t>(
		static_cast<unsigned char>(buf[0]) |
		(static_cast<unsigned char>(buf[1]) << 8) |
		(static_cast<unsigned char>(buf[2]) << 16) |
		(static_cast<unsigned char>(buf[3]) << 24));
}

// ---------------------------------------------------------------------------
// codecFourccToString
// ---------------------------------------------------------------------------

static void testCodecFourccToString() {
	beginSuite("codecFourccToString");

	// Standard FOURCCs.
	// 'H264' stored as little-endian: 'H'=0x48, '2'=0x32, '6'=0x36, '4'=0x34
	{
		const uint32_t h264 = 'H' | ('2' << 8) | ('6' << 16) | ('4' << 24);
		CHECK_EQ(codecFourccToString(h264), std::string("H264"));
	}

	// 'avc1'
	{
		const uint32_t avc1 = 'a' | ('v' << 8) | ('c' << 16) | ('1' << 24);
		CHECK_EQ(codecFourccToString(avc1), std::string("avc1"));
	}

	// All zeros → non-printable → dots.
	{
		CHECK_EQ(codecFourccToString(0), std::string("...."));
	}

	// Space characters → replaced with dots.
	{
		const uint32_t withSpaces = 'A' | (' ' << 8) | ('B' << 16) | (' ' << 24);
		CHECK_EQ(codecFourccToString(withSpaces), std::string("A.B."));
	}

	// Tab characters → replaced with dots.
	{
		const uint32_t withTab = 'X' | ('\t' << 8) | ('Y' << 16) | ('Z' << 24);
		const std::string result = codecFourccToString(withTab);
		CHECK_EQ(result[1], '.');
		CHECK_EQ(result[0], 'X');
	}

	// Control characters → replaced with dots.
	{
		const uint32_t withCtrl = 0x01 | (0x7F << 8) | ('C' << 16) | ('D' << 24);
		const std::string result = codecFourccToString(withCtrl);
		CHECK_EQ(result[0], '.');
		CHECK_EQ(result[1], '.');
		CHECK_EQ(result[2], 'C');
		CHECK_EQ(result[3], 'D');
	}
}

// ---------------------------------------------------------------------------
// formatBitrate
// ---------------------------------------------------------------------------

static void testFormatBitrate() {
	beginSuite("formatBitrate");

	// Zero → empty string.
	CHECK_EQ(formatBitrate(0), std::string(""));

	// Below 1 Mbps → kbps.
	CHECK_EQ(formatBitrate(128000), std::string("128 kbps"));
	CHECK_EQ(formatBitrate(192000), std::string("192 kbps"));
	CHECK_EQ(formatBitrate(320000), std::string("320 kbps"));
	CHECK_EQ(formatBitrate(999999), std::string("1000 kbps"));

	// 1 Mbps and above → Mbps.
	CHECK_EQ(formatBitrate(1000000), std::string("1.0 Mbps"));
	CHECK_EQ(formatBitrate(1500000), std::string("1.5 Mbps"));
	CHECK_EQ(formatBitrate(5000000), std::string("5.0 Mbps"));
	CHECK_EQ(formatBitrate(10000000), std::string("10.0 Mbps"));

	// Small values.
	CHECK_EQ(formatBitrate(1000), std::string("1 kbps"));
	CHECK_EQ(formatBitrate(1), std::string("0 kbps"));
}

// ---------------------------------------------------------------------------
// formatFrameRate
// ---------------------------------------------------------------------------

static void testFormatFrameRate() {
	beginSuite("formatFrameRate");

	// Zero numerator or denominator → empty.
	CHECK_EQ(formatFrameRate(0, 1), std::string(""));
	CHECK_EQ(formatFrameRate(1, 0), std::string(""));
	CHECK_EQ(formatFrameRate(0, 0), std::string(""));

	// 24/1 → 24.00 fps.
	CHECK_EQ(formatFrameRate(24, 1), std::string("24.00 fps"));

	// 30000/1001 → 29.97 fps (NTSC).
	CHECK_EQ(formatFrameRate(30000, 1001), std::string("29.97 fps"));

	// 25/1 → 25.00 fps.
	CHECK_EQ(formatFrameRate(25, 1), std::string("25.00 fps"));

	// 60/1 → 60.00 fps.
	CHECK_EQ(formatFrameRate(60, 1), std::string("60.00 fps"));

	// 24000/1001 → 23.98 fps (film).
	CHECK_EQ(formatFrameRate(24000, 1001), std::string("23.98 fps"));
}

// ---------------------------------------------------------------------------
// defaultBookmarkLabel
// ---------------------------------------------------------------------------

static void testDefaultBookmarkLabel() {
	beginSuite("defaultBookmarkLabel");

	// 0ms → 0:00.
	CHECK_EQ(defaultBookmarkLabel(0), std::string("0:00"));

	// 5000ms → 0:05.
	CHECK_EQ(defaultBookmarkLabel(5000), std::string("0:05"));

	// 65000ms → 1:05.
	CHECK_EQ(defaultBookmarkLabel(65000), std::string("1:05"));

	// 3661000ms → 1:01:01 (1 hour, 1 minute, 1 second).
	CHECK_EQ(defaultBookmarkLabel(3661000), std::string("1:01:01"));

	// 3600000ms → 1:00:00.
	CHECK_EQ(defaultBookmarkLabel(3600000), std::string("1:00:00"));

	// 600000ms → 10:00.
	CHECK_EQ(defaultBookmarkLabel(600000), std::string("10:00"));

	// Negative values → clamped to 0:00.
	CHECK_EQ(defaultBookmarkLabel(-5000), std::string("0:00"));

	// Sub-second precision truncated (milliseconds dropped).
	CHECK_EQ(defaultBookmarkLabel(999), std::string("0:00"));
	CHECK_EQ(defaultBookmarkLabel(1999), std::string("0:01"));

	// Large values: 10 hours.
	CHECK_EQ(defaultBookmarkLabel(36000000), std::string("10:00:00"));

	// 59:59 (just under one hour).
	CHECK_EQ(defaultBookmarkLabel(3599000), std::string("59:59"));
}

// ---------------------------------------------------------------------------
// formatCaptureFloatValue
// ---------------------------------------------------------------------------

static void testFormatCaptureFloatValue() {
	beginSuite("formatCaptureFloatValue");

	// Integer-like values → no decimal.
	CHECK_EQ(formatCaptureFloatValue(1.0f), std::string("1"));
	CHECK_EQ(formatCaptureFloatValue(0.0f), std::string("0"));
	CHECK_EQ(formatCaptureFloatValue(42.0f), std::string("42"));
	CHECK_EQ(formatCaptureFloatValue(-5.0f), std::string("-5"));

	// Values with fractional part.
	CHECK_EQ(formatCaptureFloatValue(1.5f), std::string("1.5"));
	CHECK_EQ(formatCaptureFloatValue(3.14f), std::string("3.14"));
	CHECK_EQ(formatCaptureFloatValue(0.001f), std::string("0.001"));

	// Trailing zeros stripped.
	CHECK_EQ(formatCaptureFloatValue(1.100f), std::string("1.1"));
	CHECK_EQ(formatCaptureFloatValue(2.500f), std::string("2.5"));

	// Very small value (precision limited to 3 decimal places).
	CHECK_EQ(formatCaptureFloatValue(0.0001f), std::string("0"));

	// Negative fractional.
	CHECK_EQ(formatCaptureFloatValue(-0.5f), std::string("-0.5"));
	CHECK_EQ(formatCaptureFloatValue(-1.25f), std::string("-1.25"));
}

static void testPlaybackDeleteDecisionHelpers() {
	beginSuite("playback/delete decision helpers");

	CHECK(shouldQueuePlaybackAdvanceAfterStop(true, true));
	CHECK(!shouldQueuePlaybackAdvanceAfterStop(true, false));
	CHECK(!shouldQueuePlaybackAdvanceAfterStop(false, true));
	CHECK(!shouldQueuePlaybackAdvanceAfterStop(false, false));

	CHECK(shouldClearCurrentMediaAfterPlaylistMutation(false, false));
	CHECK(!shouldClearCurrentMediaAfterPlaylistMutation(true, false));
	CHECK(!shouldClearCurrentMediaAfterPlaylistMutation(false, true));
	CHECK(!shouldClearCurrentMediaAfterPlaylistMutation(true, true));

	{
		const DeferredManualStopDecision decision = evaluateDeferredManualStop(
			false, true, false, false, false, true, false, 100, 260, false);
		CHECK(decision.shouldResetManualStop);
		CHECK(!decision.shouldFinalizeManualStop);
		CHECK(!decision.shouldRetryStopAsync);
	}

	{
		const DeferredManualStopDecision decision = evaluateDeferredManualStop(
			false, true, false, false, true, true, true, 100, 120, false);
		CHECK(!decision.shouldResetManualStop);
		CHECK(decision.shouldFinalizeManualStop);
		CHECK(!decision.shouldRetryStopAsync);
	}

	{
		const DeferredManualStopDecision decision = evaluateDeferredManualStop(
			false, true, false, false, true, true, false, 100, 300, false);
		CHECK(!decision.shouldResetManualStop);
		CHECK(!decision.shouldFinalizeManualStop);
		CHECK(decision.shouldRetryStopAsync);
	}

	{
		const DeferredManualStopDecision decision = evaluateDeferredManualStop(
			false, true, false, false, true, true, false, 100, 1700, false);
		CHECK(!decision.shouldResetManualStop);
		CHECK(decision.shouldFinalizeManualStop);
		CHECK(decision.shouldRetryStopAsync);
	}

	{
		const DeferredManualStopDecision decision = evaluateDeferredManualStop(
			false, true, true, false, true, true, true, 100, 1700, false);
		CHECK(!decision.shouldResetManualStop);
		CHECK(!decision.shouldFinalizeManualStop);
		CHECK(!decision.shouldRetryStopAsync);
	}

	{
		const StoppedPlaybackEventDecision decision = evaluateStoppedPlaybackEvent(
			1, false, false, true, false);
		CHECK(decision.keepManualStopInProgress);
		CHECK(decision.shouldFinalizeManualStop);
		CHECK(!decision.shouldActivatePendingRequest);
		CHECK(!decision.shouldQueuePlaybackAdvance);
	}

	{
		const StoppedPlaybackEventDecision decision = evaluateStoppedPlaybackEvent(
			1, true, false, true, false);
		CHECK(decision.keepManualStopInProgress);
		CHECK(!decision.shouldFinalizeManualStop);
		CHECK(decision.shouldActivatePendingRequest);
		CHECK(!decision.shouldQueuePlaybackAdvance);
	}

	{
		const StoppedPlaybackEventDecision decision = evaluateStoppedPlaybackEvent(
			0, false, false, true, true);
		CHECK(!decision.keepManualStopInProgress);
		CHECK(!decision.shouldFinalizeManualStop);
		CHECK(!decision.shouldActivatePendingRequest);
		CHECK(decision.shouldQueuePlaybackAdvance);
	}

	{
		const StoppedPlaybackEventDecision decision = evaluateStoppedPlaybackEvent(
			0, false, false, false, true);
		CHECK(!decision.keepManualStopInProgress);
		CHECK(!decision.shouldFinalizeManualStop);
		CHECK(!decision.shouldActivatePendingRequest);
		CHECK(!decision.shouldQueuePlaybackAdvance);
	}
}

// ---------------------------------------------------------------------------
// writeWavHeader: validates binary output structure
// ---------------------------------------------------------------------------

static void testWriteWavHeaderBasic() {
	beginSuite("writeWavHeader: basic 44100 stereo");

	std::ostringstream oss(std::ios::binary);
	writeWavHeader(oss, 44100, 2, 0);

	const std::string data = oss.str();
	CHECK_EQ(data.size(), 44u); // Standard WAV header is 44 bytes.

	const char * buf = data.c_str();

	// RIFF chunk ID.
	CHECK(std::memcmp(buf + 0, "RIFF", 4) == 0);
	// RIFF chunk size = 36 + dataBytes = 36.
	CHECK_EQ(readU32LE(buf + 4), 36u);
	// Format = WAVE.
	CHECK(std::memcmp(buf + 8, "WAVE", 4) == 0);
	// Subchunk1 ID = fmt.
	CHECK(std::memcmp(buf + 12, "fmt ", 4) == 0);
	// Subchunk1 size = 16.
	CHECK_EQ(readU32LE(buf + 16), 16u);
	// Audio format = 3 (IEEE float).
	CHECK_EQ(readU16LE(buf + 20), 3u);
	// Channels = 2.
	CHECK_EQ(readU16LE(buf + 22), 2u);
	// Sample rate = 44100.
	CHECK_EQ(readU32LE(buf + 24), 44100u);
	// Byte rate = 44100 * 2 * 4 = 352800.
	CHECK_EQ(readU32LE(buf + 28), 352800u);
	// Block align = 2 * 4 = 8.
	CHECK_EQ(readU16LE(buf + 32), 8u);
	// Bits per sample = 32.
	CHECK_EQ(readU16LE(buf + 34), 32u);
	// Subchunk2 ID = data.
	CHECK(std::memcmp(buf + 36, "data", 4) == 0);
	// Subchunk2 size = 0 (no audio data yet).
	CHECK_EQ(readU32LE(buf + 40), 0u);
}

static void testWriteWavHeaderMono48k() {
	beginSuite("writeWavHeader: 48000 mono");

	std::ostringstream oss(std::ios::binary);
	writeWavHeader(oss, 48000, 1, 96000);

	const std::string data = oss.str();
	CHECK_EQ(data.size(), 44u);

	const char * buf = data.c_str();

	// RIFF chunk size = 36 + 96000 = 96036.
	CHECK_EQ(readU32LE(buf + 4), 96036u);
	// Channels = 1.
	CHECK_EQ(readU16LE(buf + 22), 1u);
	// Sample rate = 48000.
	CHECK_EQ(readU32LE(buf + 24), 48000u);
	// Byte rate = 48000 * 1 * 4 = 192000.
	CHECK_EQ(readU32LE(buf + 28), 192000u);
	// Block align = 1 * 4 = 4.
	CHECK_EQ(readU16LE(buf + 32), 4u);
	// Data size = 96000.
	CHECK_EQ(readU32LE(buf + 40), 96000u);
}

static void testWriteWavHeaderLargeDataSize() {
	beginSuite("writeWavHeader: large data size");

	const uint32_t dataBytes = 100000000u; // ~100 MB
	std::ostringstream oss(std::ios::binary);
	writeWavHeader(oss, 44100, 2, dataBytes);

	const std::string data = oss.str();
	const char * buf = data.c_str();

	CHECK_EQ(readU32LE(buf + 4), 36u + dataBytes);
	CHECK_EQ(readU32LE(buf + 40), dataBytes);
}

// ---------------------------------------------------------------------------
// PlaybackTransportState defaults
// ---------------------------------------------------------------------------

// Include the header directly since it has no external dependencies
// beyond <atomic>, <string>, <vector>, <cstdint>.
#include "../src/playback/PlaybackTransportState.h"

static void testPlaybackTransportStateDefaults() {
	beginSuite("PlaybackTransportState: default values");

	PlaybackTransportState state;

	CHECK(!state.playbackWanted.load());
	CHECK(!state.pauseRequested.load());
	CHECK(!state.audioPausedSignal.load());
	CHECK(state.pendingDirectMediaSource.empty());
	CHECK(state.pendingDirectMediaOptions.empty());
	CHECK(state.pendingDirectMediaLabel.empty());
	CHECK(state.pendingDirectMediaIsLocation);
	CHECK(!state.pendingDirectMediaParseAsNetwork);
	CHECK(!state.hasPendingDirectMedia.load());
	CHECK(state.activeDirectMediaSource.empty());
	CHECK(state.activeDirectMediaOptions.empty());
	CHECK(state.activeDirectMediaIsLocation);
	CHECK(!state.activeDirectMediaParseAsNetwork);
	CHECK(!state.manualStopInProgress.load());
	CHECK(!state.pendingManualStopFinalize.load());
	CHECK_EQ(state.pendingManualStopEvents.load(), 0);
	CHECK_EQ(state.manualStopRequestTimeMs.load(), 0u);
	CHECK(!state.manualStopRetryIssued.load());
	CHECK(!state.stoppedStateLatched.load());
	CHECK(!state.playNextRequested.load());
	CHECK_EQ(state.pendingActivateIndex.load(), -1);
	CHECK(!state.pendingActivateShouldPlay.load());
	CHECK(!state.pendingActivateReady.load());
	CHECK(state.lastKnownPlaybackPosition.load() == 0.0f);
	CHECK(state.bufferCache.load() == 0.0f);
	CHECK(!state.seekableLatched.load());
	CHECK(!state.pausableLatched.load());
	CHECK_EQ(state.cachedVideoOutputCount.load(), 0u);
	CHECK(!state.corked.load());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testCodecFourccToString();
	testFormatBitrate();
	testFormatFrameRate();
	testDefaultBookmarkLabel();
	testFormatCaptureFloatValue();
	testPlaybackDeleteDecisionHelpers();
	testWriteWavHeaderBasic();
	testWriteWavHeaderMono48k();
	testWriteWavHeaderLargeDataSize();
	testPlaybackTransportStateDefaults();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
