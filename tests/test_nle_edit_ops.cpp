// Tests for NleEditOps.h — three-point editing, overwrite, splice-in,
// lift, and extract operations.

#include "NleEditOps.h"

#include <cstdio>
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

static Timecode tc(int64_t f) { return Timecode(f, R); }

static Segment makeSeg(const std::string & id, int64_t srcIn,
                       int64_t dur, int64_t tlStart) {
	Segment seg;
	seg.masterClipId = id;
	seg.sourceIn = tc(srcIn);
	seg.duration = tc(dur);
	seg.timelineStart = tc(tlStart);
	return seg;
}

// ---------------------------------------------------------------------------
// resolveThreePointEdit — all 4 valid combinations
// ---------------------------------------------------------------------------

static void testThreePointEdit() {
	beginSuite("resolveThreePointEdit: valid combinations");

	Timecode sIn  = tc(0);
	Timecode sOut = tc(100);
	Timecode rIn  = tc(200);
	Timecode rOut = tc(300);

	// 1. sourceIn + sourceOut + recordIn → recordOut
	{
		auto r = resolveThreePointEdit(&sIn, &sOut, &rIn, nullptr, R);
		CHECK(r.valid);
		CHECK_EQ(r.recordOut.totalFrames(), static_cast<int64_t>(300));
		CHECK_EQ(r.duration.totalFrames(), static_cast<int64_t>(100));
	}

	// 2. sourceIn + sourceOut + recordOut → recordIn
	{
		auto r = resolveThreePointEdit(&sIn, &sOut, nullptr, &rOut, R);
		CHECK(r.valid);
		CHECK_EQ(r.recordIn.totalFrames(), static_cast<int64_t>(200));
		CHECK_EQ(r.duration.totalFrames(), static_cast<int64_t>(100));
	}

	// 3. sourceIn + recordIn + recordOut → sourceOut
	{
		auto r = resolveThreePointEdit(&sIn, nullptr, &rIn, &rOut, R);
		CHECK(r.valid);
		CHECK_EQ(r.sourceOut.totalFrames(), static_cast<int64_t>(100));
	}

	// 4. sourceOut + recordIn + recordOut → sourceIn
	{
		auto r = resolveThreePointEdit(nullptr, &sOut, &rIn, &rOut, R);
		CHECK(r.valid);
		CHECK_EQ(r.sourceIn.totalFrames(), static_cast<int64_t>(0));
	}
}

// ---------------------------------------------------------------------------
// resolveThreePointEdit — invalid inputs
// ---------------------------------------------------------------------------

static void testThreePointEditInvalid() {
	beginSuite("resolveThreePointEdit: invalid inputs");

	Timecode sIn  = tc(0);
	Timecode sOut = tc(100);
	Timecode rIn  = tc(200);
	Timecode rOut = tc(300);

	// 0 points set.
	{
		auto r = resolveThreePointEdit(nullptr, nullptr, nullptr, nullptr, R);
		CHECK(!r.valid);
	}

	// 2 points set.
	{
		auto r = resolveThreePointEdit(&sIn, &sOut, nullptr, nullptr, R);
		CHECK(!r.valid);
	}

	// All 4 points set.
	{
		auto r = resolveThreePointEdit(&sIn, &sOut, &rIn, &rOut, R);
		CHECK(!r.valid);
	}

	// 1 point set.
	{
		auto r = resolveThreePointEdit(&sIn, nullptr, nullptr, nullptr, R);
		CHECK(!r.valid);
	}
}

// ---------------------------------------------------------------------------
// editOverwrite
// ---------------------------------------------------------------------------

static void testEditOverwrite() {
	beginSuite("editOverwrite");

	// Start with an empty track, place a segment.
	{
		Track track(TrackType::Video, "V1");
		CHECK(editOverwrite(track, "A", tc(0), tc(50), tc(0)));
		CHECK_EQ(track.segments().size(), static_cast<size_t>(1));
		CHECK_EQ(track.segments()[0].masterClipId, std::string("A"));
		CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(0));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(50));
	}

	// Overwrite replaces existing material.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("OLD", 0, 100, 0));

		// Overwrite [20, 40) with new clip.
		CHECK(editOverwrite(track, "NEW", tc(0), tc(20), tc(20)));

		// Should have 3 segments: OLD[0,20), NEW[20,40), OLD[40,100).
		CHECK_EQ(track.segments().size(), static_cast<size_t>(3));
		CHECK_EQ(track.segments()[0].masterClipId, std::string("OLD"));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(20));
		CHECK_EQ(track.segments()[1].masterClipId, std::string("NEW"));
		CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(20));
		CHECK_EQ(track.segments()[2].masterClipId, std::string("OLD"));
		CHECK_EQ(track.segments()[2].timelineStart.totalFrames(), static_cast<int64_t>(40));
	}

	// Zero duration rejected.
	{
		Track track(TrackType::Video, "V1");
		CHECK(!editOverwrite(track, "A", tc(0), tc(0), tc(0)));
	}
}

// ---------------------------------------------------------------------------
// editSpliceIn
// ---------------------------------------------------------------------------

static void testEditSpliceIn() {
	beginSuite("editSpliceIn");

	Track track(TrackType::Video, "V1");
	track.addSegment(makeSeg("A", 0, 30, 0));   // [0, 30)
	track.addSegment(makeSeg("B", 0, 30, 30));  // [30, 60)

	// Splice-in 10 frames of "INS" at frame 30.
	CHECK(editSpliceIn(track, "INS", tc(0), tc(10), tc(30)));

	// Track should have: A [0,30), INS [30,40), B [40,70).
	CHECK_EQ(track.segments().size(), static_cast<size_t>(3));
	CHECK_EQ(track.segments()[0].masterClipId, std::string("A"));
	CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(0));
	CHECK_EQ(track.segments()[1].masterClipId, std::string("INS"));
	CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(30));
	CHECK_EQ(track.segments()[2].masterClipId, std::string("B"));
	CHECK_EQ(track.segments()[2].timelineStart.totalFrames(), static_cast<int64_t>(40));

	// Total timeline extended by 10.
	CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(70));

	// Zero duration rejected.
	{
		Track t2(TrackType::Video, "V1");
		CHECK(!editSpliceIn(t2, "X", tc(0), tc(0), tc(0)));
	}
}

// ---------------------------------------------------------------------------
// editLift — remove leaving a gap
// ---------------------------------------------------------------------------

static void testEditLift() {
	beginSuite("editLift");

	Track track(TrackType::Video, "V1");
	track.addSegment(makeSeg("A", 0, 30, 0));   // [0, 30)
	track.addSegment(makeSeg("B", 0, 30, 30));  // [30, 60)
	track.addSegment(makeSeg("C", 0, 30, 60));  // [60, 90)

	// Lift [30, 60) — removes B, leaves a gap.
	CHECK(editLift(track, tc(30), tc(60)));

	CHECK_EQ(track.segments().size(), static_cast<size_t>(2));
	CHECK_EQ(track.segments()[0].masterClipId, std::string("A"));
	CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(0));
	CHECK_EQ(track.segments()[1].masterClipId, std::string("C"));
	// C stays at 60 — the gap is preserved.
	CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(60));

	// Invalid range.
	CHECK(!editLift(track, tc(50), tc(50)));
	CHECK(!editLift(track, tc(60), tc(50)));
}

// ---------------------------------------------------------------------------
// editExtract — remove and close gap (ripple delete)
// ---------------------------------------------------------------------------

static void testEditExtract() {
	beginSuite("editExtract");

	Track track(TrackType::Video, "V1");
	track.addSegment(makeSeg("A", 0, 30, 0));   // [0, 30)
	track.addSegment(makeSeg("B", 0, 30, 30));  // [30, 60)
	track.addSegment(makeSeg("C", 0, 30, 60));  // [60, 90)

	// Extract [30, 60) — removes B and closes the gap.
	CHECK(editExtract(track, tc(30), tc(60)));

	CHECK_EQ(track.segments().size(), static_cast<size_t>(2));
	CHECK_EQ(track.segments()[0].masterClipId, std::string("A"));
	CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(0));
	CHECK_EQ(track.segments()[1].masterClipId, std::string("C"));
	// C moved from 60 to 30 (gap closed).
	CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(30));

	// Total duration shrunk by 30.
	CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(60));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testThreePointEdit();
	testThreePointEditInvalid();
	testEditOverwrite();
	testEditSpliceIn();
	testEditLift();
	testEditExtract();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
