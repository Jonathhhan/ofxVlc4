#pragma once

// ---------------------------------------------------------------------------
// NleTrimOps — Trim operations for the NLE.
//
// Free functions that operate on a Track to perform standard NLE trim
// modes: Ripple, Roll, Slip, and Slide.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include "NleTimecode.h"
#include "NleTrack.h"

namespace nle {

// ---------------------------------------------------------------------------
// TrimSide — which side of a segment to trim.
// ---------------------------------------------------------------------------

enum class TrimSide { Left, Right };

// ---------------------------------------------------------------------------
// trimRipple — extend/shorten one side of a segment; timeline duration changes.
//
// Positive deltaFrames extends; negative shrinks.
// Left side: adjusts sourceIn and timelineStart.
// Right side: adjusts duration (and therefore sourceOut and timelineEnd).
// Downstream segments shift accordingly.
// ---------------------------------------------------------------------------

inline bool trimRipple(Track & track, size_t segmentIndex,
					   TrimSide side, int64_t deltaFrames)
{
	const auto & segs = track.segments();
	if (segmentIndex >= segs.size()) return false;
	if (deltaFrames == 0) return true;

	Segment seg = segs[segmentIndex];
	const FrameRate rate = seg.timelineStart.rate();

	if (side == TrimSide::Left) {
		// Moving the IN point: negative delta = extend left (more source),
		// positive delta = shrink left (less source).
		int64_t newSourceIn = seg.sourceIn.totalFrames() + deltaFrames;
		int64_t newTimelineStart = seg.timelineStart.totalFrames() + deltaFrames;
		int64_t newDuration = seg.duration.totalFrames() - deltaFrames;

		if (newSourceIn < 0 || newDuration <= 0 || newTimelineStart < 0)
			return false;

		seg.sourceIn = Timecode(newSourceIn, rate);
		seg.timelineStart = Timecode(newTimelineStart, rate);
		seg.duration = Timecode(newDuration, rate);

		if (!track.replaceSegment(segmentIndex, seg)) return false;

		// Ripple-shift downstream segments by -deltaFrames (opposite direction).
		Timecode shiftFrom = seg.timelineEnd();
		track.rippleShift(shiftFrom, -deltaFrames);
	} else {
		// Right side: extend or shrink the tail.
		int64_t newDuration = seg.duration.totalFrames() + deltaFrames;
		if (newDuration <= 0) return false;

		Timecode oldEnd = seg.timelineEnd();
		seg.duration = Timecode(newDuration, rate);

		if (!track.replaceSegment(segmentIndex, seg)) return false;

		// Shift downstream segments by deltaFrames.
		Timecode shiftFrom = oldEnd;
		track.rippleShift(shiftFrom, deltaFrames);
	}
	return true;
}

// ---------------------------------------------------------------------------
// trimRoll — move edit point between two adjacent segments.
//
// Total timeline duration is unchanged.  The right side of the left segment
// and the left side of the right segment move together.
// ---------------------------------------------------------------------------

inline bool trimRoll(Track & track, size_t leftSegIndex, int64_t deltaFrames)
{
	const auto & segs = track.segments();
	if (leftSegIndex + 1 >= segs.size()) return false;
	if (deltaFrames == 0) return true;

	Segment left = segs[leftSegIndex];
	Segment right = segs[leftSegIndex + 1];
	const FrameRate rate = left.timelineStart.rate();

	// Extend/shrink left segment's duration.
	int64_t newLeftDur = left.duration.totalFrames() + deltaFrames;
	if (newLeftDur <= 0) return false;

	// Adjust right segment: move its sourceIn and timelineStart, shrink duration.
	int64_t newRightSourceIn = right.sourceIn.totalFrames() + deltaFrames;
	int64_t newRightStart = right.timelineStart.totalFrames() + deltaFrames;
	int64_t newRightDur = right.duration.totalFrames() - deltaFrames;

	if (newRightSourceIn < 0 || newRightDur <= 0) return false;

	left.duration = Timecode(newLeftDur, rate);
	right.sourceIn = Timecode(newRightSourceIn, rate);
	right.timelineStart = Timecode(newRightStart, rate);
	right.duration = Timecode(newRightDur, rate);

	// Apply the shrinking replacement first to avoid intermediate overlap.
	if (deltaFrames > 0) {
		// Right segment shrinks — replace it first.
		if (!track.replaceSegment(leftSegIndex + 1, right)) return false;
		if (!track.replaceSegment(leftSegIndex, left)) return false;
	} else {
		// Left segment shrinks — replace it first.
		if (!track.replaceSegment(leftSegIndex, left)) return false;
		if (!track.replaceSegment(leftSegIndex + 1, right)) return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// trimSlip — change source IN/OUT, keeping timeline position and duration
//            fixed.  Only the visible portion of source media changes.
// ---------------------------------------------------------------------------

inline bool trimSlip(Track & track, size_t segmentIndex, int64_t deltaFrames)
{
	const auto & segs = track.segments();
	if (segmentIndex >= segs.size()) return false;
	if (deltaFrames == 0) return true;

	Segment seg = segs[segmentIndex];
	const FrameRate rate = seg.sourceIn.rate();

	int64_t newSourceIn = seg.sourceIn.totalFrames() + deltaFrames;
	if (newSourceIn < 0) return false;

	seg.sourceIn = Timecode(newSourceIn, rate);
	// Duration and timelineStart remain unchanged.
	return track.replaceSegment(segmentIndex, seg);
}

// ---------------------------------------------------------------------------
// trimSlide — move a segment on the timeline, adjusting the durations of
//             its neighbors to fill / consume the space.
//
// The segment keeps its own source range and duration.  The right side of
// the preceding segment and the left side of the following segment adjust.
// ---------------------------------------------------------------------------

inline bool trimSlide(Track & track, size_t segmentIndex, int64_t deltaFrames)
{
	const auto & segs = track.segments();
	if (segmentIndex >= segs.size()) return false;
	if (deltaFrames == 0) return true;
	// Need both a predecessor and a successor.
	if (segmentIndex == 0 || segmentIndex + 1 >= segs.size()) return false;

	Segment prev = segs[segmentIndex - 1];
	Segment curr = segs[segmentIndex];
	Segment next = segs[segmentIndex + 1];
	const FrameRate rate = curr.timelineStart.rate();

	// Adjust predecessor: extend/shrink its right side.
	int64_t newPrevDur = prev.duration.totalFrames() + deltaFrames;
	if (newPrevDur <= 0) return false;

	// Move the current segment's timeline position.
	int64_t newCurrStart = curr.timelineStart.totalFrames() + deltaFrames;
	if (newCurrStart < 0) return false;

	// Adjust successor: move its left side.
	int64_t newNextSourceIn = next.sourceIn.totalFrames() + deltaFrames;
	int64_t newNextStart = next.timelineStart.totalFrames() + deltaFrames;
	int64_t newNextDur = next.duration.totalFrames() - deltaFrames;
	if (newNextSourceIn < 0 || newNextDur <= 0) return false;

	prev.duration = Timecode(newPrevDur, rate);
	curr.timelineStart = Timecode(newCurrStart, rate);
	next.sourceIn = Timecode(newNextSourceIn, rate);
	next.timelineStart = Timecode(newNextStart, rate);
	next.duration = Timecode(newNextDur, rate);

	if (!track.replaceSegment(segmentIndex - 1, prev)) return false;
	if (!track.replaceSegment(segmentIndex, curr)) return false;
	if (!track.replaceSegment(segmentIndex + 1, next)) return false;
	return true;
}

} // namespace nle
