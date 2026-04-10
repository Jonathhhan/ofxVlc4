// Tests for NleTrimOps.h — trimRipple, trimRoll, trimSlip, trimSlide.

#include "NleTrimOps.h"

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
// trimRipple — right side extend and shorten
// ---------------------------------------------------------------------------

static void testTrimRippleRight() {
	beginSuite("trimRipple: right side");

	// Single segment: extend right by 10.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 30, 0));

		CHECK(trimRipple(track, 0, TrimSide::Right, 10));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(40));
	}

	// Single segment: shorten right by 10.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 30, 0));

		CHECK(trimRipple(track, 0, TrimSide::Right, -10));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(20));
	}

	// Two adjacent segments: extend right on first, downstream shifts.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 30, 0));
		track.addSegment(makeSeg("B", 0, 30, 30));

		CHECK(trimRipple(track, 0, TrimSide::Right, 5));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(35));
		// B shifts from 30 to 35.
		CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(35));
	}

	// Two adjacent segments: shorten right on first, downstream shifts back.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 30, 0));
		track.addSegment(makeSeg("B", 0, 30, 30));

		CHECK(trimRipple(track, 0, TrimSide::Right, -10));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(20));
		CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(20));
	}
}

// ---------------------------------------------------------------------------
// trimRipple — left side extend and shorten
// ---------------------------------------------------------------------------

static void testTrimRippleLeft() {
	beginSuite("trimRipple: left side");

	// Extend left (delta < 0) on a segment with sourceIn > 0.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 10, 30, 10));

		CHECK(trimRipple(track, 0, TrimSide::Left, -5));
		// sourceIn moves from 10 to 5, timelineStart from 10 to 5, duration 30 to 35.
		CHECK_EQ(track.segments()[0].sourceIn.totalFrames(), static_cast<int64_t>(5));
		CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(5));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(35));
	}

	// Shorten left (delta > 0).
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 10, 30, 10));

		CHECK(trimRipple(track, 0, TrimSide::Left, 5));
		CHECK_EQ(track.segments()[0].sourceIn.totalFrames(), static_cast<int64_t>(15));
		CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(15));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(25));
	}
}

// ---------------------------------------------------------------------------
// trimRipple — edge cases
// ---------------------------------------------------------------------------

static void testTrimRippleEdgeCases() {
	beginSuite("trimRipple: edge cases");

	// Trim to zero duration (should fail).
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 10, 0));
		CHECK(!trimRipple(track, 0, TrimSide::Right, -10));
	}

	// Trim past zero duration (should fail).
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 10, 0));
		CHECK(!trimRipple(track, 0, TrimSide::Right, -20));
	}

	// Extend left past sourceIn=0 (should fail).
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 30, 0));
		CHECK(!trimRipple(track, 0, TrimSide::Left, -5));
	}

	// Zero delta is a no-op success.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 30, 0));
		CHECK(trimRipple(track, 0, TrimSide::Right, 0));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(30));
	}

	// Invalid segment index.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 0, 30, 0));
		CHECK(!trimRipple(track, 5, TrimSide::Right, 5));
	}
}

// ---------------------------------------------------------------------------
// trimRoll — move cut point between adjacent segments
// ---------------------------------------------------------------------------

static void testTrimRoll() {
	beginSuite("trimRoll");

	// Roll right by 5: left extends, right shrinks.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("L", 0, 30, 0));
		track.addSegment(makeSeg("R", 10, 30, 30));

		CHECK(trimRoll(track, 0, 5));

		// Left: duration 30 → 35.
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(35));
		// Right: sourceIn 10 → 15, timelineStart 30 → 35, duration 30 → 25.
		CHECK_EQ(track.segments()[1].sourceIn.totalFrames(), static_cast<int64_t>(15));
		CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(35));
		CHECK_EQ(track.segments()[1].duration.totalFrames(), static_cast<int64_t>(25));

		// Total timeline unchanged: 0 + 35 + 25 = 60.
		CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(60));
	}

	// Roll left by 5: left shrinks, right extends.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("L", 0, 30, 0));
		track.addSegment(makeSeg("R", 10, 30, 30));

		CHECK(trimRoll(track, 0, -5));
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(25));
		CHECK_EQ(track.segments()[1].sourceIn.totalFrames(), static_cast<int64_t>(5));
		CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(25));
		CHECK_EQ(track.segments()[1].duration.totalFrames(), static_cast<int64_t>(35));
		CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(60));
	}

	// Roll that would make right sourceIn negative → fail.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("L", 0, 30, 0));
		track.addSegment(makeSeg("R", 2, 30, 30));
		CHECK(!trimRoll(track, 0, -5));
	}

	// Zero delta is no-op success.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("L", 0, 30, 0));
		track.addSegment(makeSeg("R", 10, 30, 30));
		CHECK(trimRoll(track, 0, 0));
	}

	// Roll that would make right segment timelineStart negative → fail.
	// Left starts at 0 with duration 5; rolling left by 10 would push
	// right segment start to 5 + (-10) = -5.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("L", 0, 5, 0));
		track.addSegment(makeSeg("R", 100, 30, 5));
		CHECK(!trimRoll(track, 0, -10));
	}
}

// ---------------------------------------------------------------------------
// trimSlip — change source IN/OUT, keep position and duration
// ---------------------------------------------------------------------------

static void testTrimSlip() {
	beginSuite("trimSlip");

	// Slip forward by 5.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 10, 30, 0));

		CHECK(trimSlip(track, 0, 5));
		CHECK_EQ(track.segments()[0].sourceIn.totalFrames(), static_cast<int64_t>(15));
		// Duration and timelineStart unchanged.
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(30));
		CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(0));
	}

	// Slip backward by 5.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 10, 30, 0));

		CHECK(trimSlip(track, 0, -5));
		CHECK_EQ(track.segments()[0].sourceIn.totalFrames(), static_cast<int64_t>(5));
	}

	// Slip past sourceIn=0 → fail.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 3, 30, 0));
		CHECK(!trimSlip(track, 0, -5));
	}

	// Zero delta is no-op success.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("A", 10, 30, 0));
		CHECK(trimSlip(track, 0, 0));
	}
}

// ---------------------------------------------------------------------------
// trimSlide — move segment, adjusting neighbor durations
// ---------------------------------------------------------------------------

static void testTrimSlide() {
	beginSuite("trimSlide");

	// Slide right by 5.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("P", 0, 20, 0));
		track.addSegment(makeSeg("C", 10, 20, 20));
		track.addSegment(makeSeg("N", 10, 20, 40));

		CHECK(trimSlide(track, 1, 5));

		// Predecessor extended: duration 20 → 25.
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(25));
		// Current moved: timelineStart 20 → 25.
		CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(25));
		// Duration and sourceIn of current unchanged.
		CHECK_EQ(track.segments()[1].duration.totalFrames(), static_cast<int64_t>(20));
		CHECK_EQ(track.segments()[1].sourceIn.totalFrames(), static_cast<int64_t>(10));
		// Successor shrunk: sourceIn 10 → 15, start 40 → 45, duration 20 → 15.
		CHECK_EQ(track.segments()[2].sourceIn.totalFrames(), static_cast<int64_t>(15));
		CHECK_EQ(track.segments()[2].timelineStart.totalFrames(), static_cast<int64_t>(45));
		CHECK_EQ(track.segments()[2].duration.totalFrames(), static_cast<int64_t>(15));

		// Total timeline unchanged.
		CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(60));
	}

	// Slide left by 5.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("P", 0, 20, 0));
		track.addSegment(makeSeg("C", 10, 20, 20));
		track.addSegment(makeSeg("N", 10, 20, 40));

		CHECK(trimSlide(track, 1, -5));

		// Predecessor shrunk: duration 20 → 15.
		CHECK_EQ(track.segments()[0].duration.totalFrames(), static_cast<int64_t>(15));
		// Current moved left: timelineStart 20 → 15.
		CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(15));
		// Successor extended: sourceIn 10 → 5, start 40 → 35, duration 20 → 25.
		CHECK_EQ(track.segments()[2].sourceIn.totalFrames(), static_cast<int64_t>(5));
		CHECK_EQ(track.segments()[2].timelineStart.totalFrames(), static_cast<int64_t>(35));
		CHECK_EQ(track.segments()[2].duration.totalFrames(), static_cast<int64_t>(25));

		CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(60));
	}

	// No predecessor → fail.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("C", 0, 20, 0));
		track.addSegment(makeSeg("N", 0, 20, 20));
		CHECK(!trimSlide(track, 0, 5));
	}

	// No successor → fail.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("P", 0, 20, 0));
		track.addSegment(makeSeg("C", 0, 20, 20));
		CHECK(!trimSlide(track, 1, 5));
	}

	// Slide that would shrink predecessor to zero → fail.
	{
		Track track(TrackType::Video, "V1");
		track.addSegment(makeSeg("P", 0, 5, 0));
		track.addSegment(makeSeg("C", 0, 20, 5));
		track.addSegment(makeSeg("N", 10, 20, 25));
		CHECK(!trimSlide(track, 1, -10));
	}
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testTrimRippleRight();
	testTrimRippleLeft();
	testTrimRippleEdgeCases();
	testTrimRoll();
	testTrimSlip();
	testTrimSlide();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
