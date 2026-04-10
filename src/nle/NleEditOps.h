#pragma once

// ---------------------------------------------------------------------------
// NleEditOps — Three-point editing and segment operations.
//
// Free functions that operate on Sequence / Track to perform standard NLE
// edit modes: Overwrite, Splice-In (Insert), Lift, and Extract.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "NleTimecode.h"
#include "NleTrack.h"

namespace nle {

// ---------------------------------------------------------------------------
// ThreePointResult — resolved four-point edit.
// ---------------------------------------------------------------------------

struct ThreePointResult {
	bool valid = false;
	Timecode sourceIn;
	Timecode sourceOut;
	Timecode recordIn;
	Timecode recordOut;
	Timecode duration;
};

// ---------------------------------------------------------------------------
// resolveThreePointEdit — calculate the 4th point from 3 given points.
//
// Pass nullptr for whichever point is missing.  Exactly one must be nullptr.
// ---------------------------------------------------------------------------

inline ThreePointResult resolveThreePointEdit(
	const Timecode * sourceIn,
	const Timecode * sourceOut,
	const Timecode * recordIn,
	const Timecode * recordOut,
	FrameRate /*rate*/)
{
	ThreePointResult r;

	// Count how many points are set.
	int setCount = (sourceIn ? 1 : 0) + (sourceOut ? 1 : 0)
				 + (recordIn ? 1 : 0) + (recordOut ? 1 : 0);
	if (setCount != 3) return r; // exactly 3 required

	if (!sourceIn) {
		Timecode dur = *recordOut - *recordIn;
		r.sourceOut = *sourceOut;
		r.sourceIn  = *sourceOut - dur;
		r.recordIn  = *recordIn;
		r.recordOut = *recordOut;
		r.duration  = dur;
	} else if (!sourceOut) {
		Timecode dur = *recordOut - *recordIn;
		r.sourceIn  = *sourceIn;
		r.sourceOut = *sourceIn + dur;
		r.recordIn  = *recordIn;
		r.recordOut = *recordOut;
		r.duration  = dur;
	} else if (!recordIn) {
		Timecode dur = *sourceOut - *sourceIn;
		r.sourceIn  = *sourceIn;
		r.sourceOut = *sourceOut;
		r.recordOut = *recordOut;
		r.recordIn  = *recordOut - dur;
		r.duration  = dur;
	} else {
		Timecode dur = *sourceOut - *sourceIn;
		r.sourceIn  = *sourceIn;
		r.sourceOut = *sourceOut;
		r.recordIn  = *recordIn;
		r.recordOut = *recordIn + dur;
		r.duration  = dur;
	}

	r.valid = r.duration.totalFrames() > 0;
	return r;
}

// ---------------------------------------------------------------------------
// detail::carveOutRange — shared helper: collect segments surviving the
// removal of timeline range [from, to).  Segments wholly or partially
// inside the range are split/trimmed.  O(n) single pass.
// ---------------------------------------------------------------------------

namespace detail {

inline std::vector<Segment> carveOutRange(
	const std::vector<Segment> & segments,
	const Timecode & from, const Timecode & to)
{
	const FrameRate rate = from.rate();
	const int64_t fromFrames = from.totalFrames();
	const int64_t toFrames   = to.totalFrames();

	// Reserve space: most segments survive.
	std::vector<Segment> surviving;
	surviving.reserve(segments.size() + 1);

	for (const auto & seg : segments) {
		const int64_t segStart = seg.timelineStart.totalFrames();
		const int64_t segEnd   = seg.timelineEnd().totalFrames();

		if (segEnd <= fromFrames || segStart >= toFrames) {
			// Completely outside — keep as is.
			surviving.push_back(seg);
		} else if (segStart < fromFrames && segEnd > toFrames) {
			// Straddles — split into two.
			Segment left = seg;
			left.duration = from - seg.timelineStart;

			Segment right = seg;
			int64_t trimFront = toFrames - segStart;
			right.sourceIn = Timecode(seg.sourceIn.totalFrames() + trimFront, rate);
			right.timelineStart = to;
			right.duration = seg.timelineEnd() - to;

			surviving.push_back(left);
			surviving.push_back(right);
		} else if (segStart < fromFrames) {
			// Overlaps on the left — trim the tail.
			Segment trimmed = seg;
			trimmed.duration = from - seg.timelineStart;
			surviving.push_back(trimmed);
		} else if (segEnd > toFrames) {
			// Overlaps on the right — trim the head.
			Segment trimmed = seg;
			int64_t trimFront = toFrames - segStart;
			trimmed.sourceIn = Timecode(seg.sourceIn.totalFrames() + trimFront, rate);
			trimmed.timelineStart = to;
			trimmed.duration = seg.timelineEnd() - to;
			surviving.push_back(trimmed);
		}
		// Else fully inside — discarded.
	}

	return surviving;
}

} // namespace detail

// ---------------------------------------------------------------------------
// editOverwrite — replace material at recordIn..recordIn+duration on track.
// ---------------------------------------------------------------------------

inline bool editOverwrite(Track & track, const std::string & masterClipId,
						  const Timecode & sourceIn, const Timecode & duration,
						  const Timecode & recordIn)
{
	if (duration.totalFrames() <= 0) return false;

	const Timecode recordOut = recordIn + duration;

	auto surviving = detail::carveOutRange(track.segments(), recordIn, recordOut);

	// Build the new segment.
	Segment newSeg;
	newSeg.masterClipId = masterClipId;
	newSeg.sourceIn = sourceIn;
	newSeg.duration = duration;
	newSeg.timelineStart = recordIn;
	surviving.push_back(newSeg);

	// Bulk-replace (sorts + validates internally).
	return track.setSegments(std::move(surviving));
}

// ---------------------------------------------------------------------------
// editSpliceIn — push downstream material right, then insert.
// ---------------------------------------------------------------------------

inline bool editSpliceIn(Track & track, const std::string & masterClipId,
						 const Timecode & sourceIn, const Timecode & duration,
						 const Timecode & recordIn)
{
	if (duration.totalFrames() <= 0) return false;

	track.rippleShift(recordIn, duration.totalFrames());

	Segment newSeg;
	newSeg.masterClipId = masterClipId;
	newSeg.sourceIn = sourceIn;
	newSeg.duration = duration;
	newSeg.timelineStart = recordIn;
	return track.addSegment(newSeg);
}

// ---------------------------------------------------------------------------
// editLift — remove material in [from, to), leaving a gap (filler).
// ---------------------------------------------------------------------------

inline bool editLift(Track & track, const Timecode & from, const Timecode & to)
{
	if (to <= from) return false;

	auto surviving = detail::carveOutRange(track.segments(), from, to);
	return track.setSegments(std::move(surviving));
}

// ---------------------------------------------------------------------------
// editExtract — remove material and close gap (ripple delete).
// ---------------------------------------------------------------------------

inline bool editExtract(Track & track, const Timecode & from, const Timecode & to)
{
	if (to <= from) return false;

	if (!editLift(track, from, to)) return false;

	int64_t gapFrames = to.totalFrames() - from.totalFrames();
	track.rippleShift(from, -gapFrames);
	return true;
}

} // namespace nle
