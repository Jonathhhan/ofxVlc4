// Tests for NleTrack.h and NleSequence.h — Track, Segment, and Sequence.
// Covers track creation, segment management, overlap rejection,
// segmentIndexAt, rippleShift, sequence duration, track removal, playhead.

#include "NleSequence.h"

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

// Helper to create a simple segment.
static Segment makeSeg(const std::string & clipId, int64_t srcIn,
                       int64_t dur, int64_t tlStart,
                       FrameRate rate = FrameRate::Fps24) {
	Segment seg;
	seg.masterClipId = clipId;
	seg.sourceIn = Timecode(srcIn, rate);
	seg.duration = Timecode(dur, rate);
	seg.timelineStart = Timecode(tlStart, rate);
	return seg;
}

// ---------------------------------------------------------------------------
// Track creation and properties
// ---------------------------------------------------------------------------

static void testTrackProperties() {
	beginSuite("Track creation and properties");

	Track vt(TrackType::Video, "V1");
	CHECK(vt.type() == TrackType::Video);
	CHECK_EQ(vt.name(), std::string("V1"));
	CHECK(!vt.isLocked());
	CHECK(!vt.isMuted());
	CHECK(!vt.isSoloed());

	vt.setLocked(true);
	CHECK(vt.isLocked());
	vt.setMuted(true);
	CHECK(vt.isMuted());
	vt.setSoloed(true);
	CHECK(vt.isSoloed());

	Track at(TrackType::Audio, "A1");
	CHECK(at.type() == TrackType::Audio);
}

// ---------------------------------------------------------------------------
// Adding non-overlapping segments
// ---------------------------------------------------------------------------

static void testAddNonOverlapping() {
	beginSuite("Adding non-overlapping segments");

	Track track(TrackType::Video, "V1");

	// Add segment [0, 30).
	CHECK(track.addSegment(makeSeg("clip1", 0, 30, 0)));
	CHECK_EQ(track.segments().size(), static_cast<size_t>(1));

	// Add segment [30, 60) — adjacent, no overlap.
	CHECK(track.addSegment(makeSeg("clip2", 0, 30, 30)));
	CHECK_EQ(track.segments().size(), static_cast<size_t>(2));

	// Add segment [100, 120) — gap, no overlap.
	CHECK(track.addSegment(makeSeg("clip3", 0, 20, 100)));
	CHECK_EQ(track.segments().size(), static_cast<size_t>(3));

	// Segments should be sorted by timelineStart.
	CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(0));
	CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(30));
	CHECK_EQ(track.segments()[2].timelineStart.totalFrames(), static_cast<int64_t>(100));
}

// ---------------------------------------------------------------------------
// Rejecting overlapping segments
// ---------------------------------------------------------------------------

static void testRejectOverlap() {
	beginSuite("Rejecting overlapping segments");

	Track track(TrackType::Video, "V1");
	track.addSegment(makeSeg("clip1", 0, 30, 0));   // [0, 30)
	track.addSegment(makeSeg("clip2", 0, 30, 30));  // [30, 60)

	// Fully inside existing.
	CHECK(!track.addSegment(makeSeg("x", 0, 10, 5)));

	// Partial overlap on left.
	CHECK(!track.addSegment(makeSeg("x", 0, 20, 20)));

	// Partial overlap on right.
	CHECK(!track.addSegment(makeSeg("x", 0, 20, 50)));

	// Straddling.
	CHECK(!track.addSegment(makeSeg("x", 0, 100, 0)));

	// Zero duration rejected.
	CHECK(!track.addSegment(makeSeg("x", 0, 0, 70)));

	// Exactly adjacent is OK.
	CHECK(track.addSegment(makeSeg("clip3", 0, 10, 60)));
}

// ---------------------------------------------------------------------------
// segmentIndexAt queries
// ---------------------------------------------------------------------------

static void testSegmentIndexAt() {
	beginSuite("segmentIndexAt queries");

	Track track(TrackType::Video, "V1");
	track.addSegment(makeSeg("A", 0, 30, 0));   // [0, 30)
	track.addSegment(makeSeg("B", 0, 30, 50));  // [50, 80)

	CHECK_EQ(track.segmentIndexAt(Timecode(0, FrameRate::Fps24)), 0);
	CHECK_EQ(track.segmentIndexAt(Timecode(15, FrameRate::Fps24)), 0);
	CHECK_EQ(track.segmentIndexAt(Timecode(29, FrameRate::Fps24)), 0);

	// At frame 30 — in the gap.
	CHECK_EQ(track.segmentIndexAt(Timecode(30, FrameRate::Fps24)), -1);

	// At frame 50 — segment B.
	CHECK_EQ(track.segmentIndexAt(Timecode(50, FrameRate::Fps24)), 1);
	CHECK_EQ(track.segmentIndexAt(Timecode(79, FrameRate::Fps24)), 1);

	// At frame 80 — past segment B (exclusive end).
	CHECK_EQ(track.segmentIndexAt(Timecode(80, FrameRate::Fps24)), -1);
}

// ---------------------------------------------------------------------------
// Segment removal
// ---------------------------------------------------------------------------

static void testSegmentRemoval() {
	beginSuite("Segment removal");

	Track track(TrackType::Video, "V1");
	track.addSegment(makeSeg("A", 0, 30, 0));
	track.addSegment(makeSeg("B", 0, 30, 30));

	CHECK(track.removeSegment(0));
	CHECK_EQ(track.segments().size(), static_cast<size_t>(1));
	CHECK_EQ(track.segments()[0].masterClipId, std::string("B"));

	// Out of range.
	CHECK(!track.removeSegment(5));
}

// ---------------------------------------------------------------------------
// rippleShift
// ---------------------------------------------------------------------------

static void testRippleShift() {
	beginSuite("rippleShift");

	Track track(TrackType::Video, "V1");
	track.addSegment(makeSeg("A", 0, 30, 0));   // [0, 30)
	track.addSegment(makeSeg("B", 0, 30, 30));  // [30, 60)
	track.addSegment(makeSeg("C", 0, 30, 60));  // [60, 90)

	// Shift everything at or after frame 30 by +10.
	track.rippleShift(Timecode(30, FrameRate::Fps24), 10);

	CHECK_EQ(track.segments()[0].timelineStart.totalFrames(), static_cast<int64_t>(0));
	CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(40));
	CHECK_EQ(track.segments()[2].timelineStart.totalFrames(), static_cast<int64_t>(70));

	// Shift back by -10.
	track.rippleShift(Timecode(40, FrameRate::Fps24), -10);
	CHECK_EQ(track.segments()[1].timelineStart.totalFrames(), static_cast<int64_t>(30));
	CHECK_EQ(track.segments()[2].timelineStart.totalFrames(), static_cast<int64_t>(60));
}

// ---------------------------------------------------------------------------
// Sequence creation with multiple tracks
// ---------------------------------------------------------------------------

static void testSequenceCreation() {
	beginSuite("Sequence creation");

	Sequence seq("My Seq", FrameRate::Fps24);
	CHECK_EQ(seq.name(), std::string("My Seq"));
	CHECK(seq.rate() == FrameRate::Fps24);
	CHECK_EQ(seq.videoTrackCount(), static_cast<size_t>(0));
	CHECK_EQ(seq.audioTrackCount(), static_cast<size_t>(0));

	auto & v1 = seq.addVideoTrack("V1");
	auto & v2 = seq.addVideoTrack();
	(void)v1; (void)v2; // references may be invalidated by vector growth
	auto & a1 = seq.addAudioTrack("A1");
	auto & a2 = seq.addAudioTrack();
	(void)a1; (void)a2;

	CHECK_EQ(seq.videoTrackCount(), static_cast<size_t>(2));
	CHECK_EQ(seq.audioTrackCount(), static_cast<size_t>(2));
	CHECK_EQ(seq.videoTrack(0).name(), std::string("V1"));
	CHECK_EQ(seq.videoTrack(1).name(), std::string("V2"));
	CHECK_EQ(seq.audioTrack(0).name(), std::string("A1"));
	CHECK_EQ(seq.audioTrack(1).name(), std::string("A2"));
}

// ---------------------------------------------------------------------------
// Sequence duration calculation
// ---------------------------------------------------------------------------

static void testSequenceDuration() {
	beginSuite("Sequence duration");

	Sequence seq("Test", FrameRate::Fps24);

	// Empty sequence has zero duration.
	CHECK_EQ(seq.duration().totalFrames(), static_cast<int64_t>(0));

	auto & v1 = seq.addVideoTrack();
	v1.addSegment(makeSeg("A", 0, 100, 0));  // ends at 100

	auto & a1 = seq.addAudioTrack();
	a1.addSegment(makeSeg("B", 0, 200, 50)); // ends at 250

	// Duration is the max end across all tracks.
	CHECK_EQ(seq.duration().totalFrames(), static_cast<int64_t>(250));
}

// ---------------------------------------------------------------------------
// Track removal
// ---------------------------------------------------------------------------

static void testTrackRemoval() {
	beginSuite("Track removal");

	Sequence seq("Test", FrameRate::Fps24);
	seq.addVideoTrack("V1");
	seq.addVideoTrack("V2");
	seq.addAudioTrack("A1");

	CHECK(seq.removeVideoTrack(0));
	CHECK_EQ(seq.videoTrackCount(), static_cast<size_t>(1));
	CHECK_EQ(seq.videoTrack(0).name(), std::string("V2"));

	CHECK(seq.removeAudioTrack(0));
	CHECK_EQ(seq.audioTrackCount(), static_cast<size_t>(0));

	// Out of range.
	CHECK(!seq.removeVideoTrack(5));
	CHECK(!seq.removeAudioTrack(0));
}

// ---------------------------------------------------------------------------
// Playhead get/set
// ---------------------------------------------------------------------------

static void testPlayhead() {
	beginSuite("Playhead get/set");

	Sequence seq("Test", FrameRate::Fps24);

	// Default playhead at 0.
	CHECK_EQ(seq.playhead().totalFrames(), static_cast<int64_t>(0));

	seq.setPlayhead(Timecode(100, FrameRate::Fps24));
	CHECK_EQ(seq.playhead().totalFrames(), static_cast<int64_t>(100));

	seq.setPlayhead(Timecode(0, FrameRate::Fps24));
	CHECK_EQ(seq.playhead().totalFrames(), static_cast<int64_t>(0));
}

// ---------------------------------------------------------------------------
// Sequence segmentAtTimecode
// ---------------------------------------------------------------------------

static void testSegmentAtTimecode() {
	beginSuite("Sequence segmentAtTimecode");

	Sequence seq("Test", FrameRate::Fps24);
	auto & v1 = seq.addVideoTrack();
	v1.addSegment(makeSeg("A", 0, 50, 0));

	auto & a1 = seq.addAudioTrack();
	a1.addSegment(makeSeg("B", 0, 100, 10));

	CHECK_EQ(seq.segmentAtTimecode(0, true, Timecode(25, FrameRate::Fps24)), 0);
	CHECK_EQ(seq.segmentAtTimecode(0, true, Timecode(60, FrameRate::Fps24)), -1);
	CHECK_EQ(seq.segmentAtTimecode(0, false, Timecode(50, FrameRate::Fps24)), 0);
	CHECK_EQ(seq.segmentAtTimecode(0, false, Timecode(5, FrameRate::Fps24)), -1);

	// Out of range track index.
	CHECK_EQ(seq.segmentAtTimecode(5, true, Timecode(0, FrameRate::Fps24)), -1);
}

// ---------------------------------------------------------------------------
// Track endTimecode
// ---------------------------------------------------------------------------

static void testEndTimecode() {
	beginSuite("Track endTimecode");

	Track track(TrackType::Video, "V1");

	// Empty track.
	CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(0));

	track.addSegment(makeSeg("A", 0, 30, 0));
	CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(30));

	track.addSegment(makeSeg("B", 0, 20, 50));
	CHECK_EQ(track.endTimecode().totalFrames(), static_cast<int64_t>(70));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testTrackProperties();
	testAddNonOverlapping();
	testRejectOverlap();
	testSegmentIndexAt();
	testSegmentRemoval();
	testRippleShift();
	testSequenceCreation();
	testSequenceDuration();
	testTrackRemoval();
	testPlayhead();
	testSegmentAtTimecode();
	testEndTimecode();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
