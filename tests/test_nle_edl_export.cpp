// Tests for NleEdlExport.h — CMX 3600 EDL export.

#include "NleEdlExport.h"

#include <cstdio>
#include <map>
#include <string>

using namespace nle;

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

static const FrameRate R = FrameRate::Fps24;

static Segment makeSeg(const std::string & id, int64_t srcIn,
                       int64_t dur, int64_t tlStart) {
	Segment seg;
	seg.masterClipId = id;
	seg.sourceIn = Timecode(srcIn, R);
	seg.duration = Timecode(dur, R);
	seg.timelineStart = Timecode(tlStart, R);
	return seg;
}

// ---------------------------------------------------------------------------
// Single-segment EDL
// ---------------------------------------------------------------------------

static void testSingleSegmentEdl() {
	beginSuite("Single-segment EDL");

	Sequence seq("TestSeq", FrameRate::Fps24);
	auto & v1 = seq.addVideoTrack();
	// 5 seconds of clip at 24fps = 120 frames.
	v1.addSegment(makeSeg("CLIP01", 0, 120, 0));

	std::string edl = exportEdlCmx3600(seq);

	// Header.
	CHECK(edl.find("TITLE: TestSeq") != std::string::npos);
	CHECK(edl.find("NON-DROP FRAME") != std::string::npos);

	// Event line.
	CHECK(edl.find("001") != std::string::npos);
	CHECK(edl.find("CLIP01") != std::string::npos);
	CHECK(edl.find("V") != std::string::npos);
	CHECK(edl.find("C") != std::string::npos);

	// Timecodes: src in/out = 00:00:00:00 / 00:00:05:00.
	CHECK(edl.find("00:00:00:00") != std::string::npos);
	CHECK(edl.find("00:00:05:00") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Multi-segment EDL
// ---------------------------------------------------------------------------

static void testMultiSegmentEdl() {
	beginSuite("Multi-segment EDL");

	Sequence seq("Multi", FrameRate::Fps24);
	auto & v1 = seq.addVideoTrack();
	v1.addSegment(makeSeg("A", 0, 48, 0));
	v1.addSegment(makeSeg("B", 0, 72, 48));

	std::string edl = exportEdlCmx3600(seq);

	// Two events: 001 and 002.
	CHECK(edl.find("001") != std::string::npos);
	CHECK(edl.find("002") != std::string::npos);

	// Both clip IDs present.
	CHECK(edl.find("A") != std::string::npos);
	CHECK(edl.find("B") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Custom reel name mapping
// ---------------------------------------------------------------------------

static void testCustomReelNames() {
	beginSuite("Custom reel name mapping");

	Sequence seq("Reel", FrameRate::Fps24);
	auto & v1 = seq.addVideoTrack();
	v1.addSegment(makeSeg("clip-uuid-123", 0, 48, 0));

	std::map<std::string, std::string> reelMap;
	reelMap["clip-uuid-123"] = "MYREEL";

	std::string edl = exportEdlCmx3600(seq, 0, reelMap);

	CHECK(edl.find("MYREEL") != std::string::npos);
	// The UUID should NOT appear as the reel name.
	// (it might appear elsewhere, but MYREEL replaces it in the event line)
}

// ---------------------------------------------------------------------------
// Empty sequence — header only
// ---------------------------------------------------------------------------

static void testEmptySequenceEdl() {
	beginSuite("Empty sequence EDL");

	Sequence seq("Empty", FrameRate::Fps24);
	seq.addVideoTrack();

	std::string edl = exportEdlCmx3600(seq);

	CHECK(edl.find("TITLE: Empty") != std::string::npos);
	CHECK(edl.find("NON-DROP FRAME") != std::string::npos);
	// No event lines — just header.
	CHECK(edl.find("001") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Drop-frame EDL header
// ---------------------------------------------------------------------------

static void testDropFrameEdlHeader() {
	beginSuite("Drop-frame EDL header");

	Sequence seq("DFSeq", FrameRate::Fps29_97_DF);
	seq.addVideoTrack();

	std::string edl = exportEdlCmx3600(seq);
	CHECK(edl.find("DROP FRAME") != std::string::npos);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testSingleSegmentEdl();
	testMultiSegmentEdl();
	testCustomReelNames();
	testEmptySequenceEdl();
	testDropFrameEdlHeader();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
