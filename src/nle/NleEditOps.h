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
		// Missing source IN — derive from source OUT minus duration.
		Timecode dur = *recordOut - *recordIn;
		r.sourceOut = *sourceOut;
		r.sourceIn  = *sourceOut - dur;
		r.recordIn  = *recordIn;
		r.recordOut = *recordOut;
		r.duration  = dur;
	} else if (!sourceOut) {
		// Missing source OUT — derive from source IN plus duration.
		Timecode dur = *recordOut - *recordIn;
		r.sourceIn  = *sourceIn;
		r.sourceOut = *sourceIn + dur;
		r.recordIn  = *recordIn;
		r.recordOut = *recordOut;
		r.duration  = dur;
	} else if (!recordIn) {
		// Missing record IN — derive from record OUT minus duration.
		Timecode dur = *sourceOut - *sourceIn;
		r.sourceIn  = *sourceIn;
		r.sourceOut = *sourceOut;
		r.recordOut = *recordOut;
		r.recordIn  = *recordOut - dur;
		r.duration  = dur;
	} else {
		// Missing record OUT — derive from record IN plus duration.
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
// editOverwrite — replace material at recordIn..recordIn+duration on track.
//
// Any existing segments that fall within the overwrite range are removed or
// trimmed, and the new segment is inserted.
// ---------------------------------------------------------------------------

inline bool editOverwrite(Track & track, const std::string & masterClipId,
						  const Timecode & sourceIn, const Timecode & duration,
						  const Timecode & recordIn)
{
	if (duration.totalFrames() <= 0) return false;

	const FrameRate rate = recordIn.rate();
	const Timecode recordOut = recordIn + duration;

	// Collect surviving segments after carving out [recordIn, recordOut).
	std::vector<Segment> surviving;
	for (const auto & seg : track.segments()) {
		if (seg.timelineEnd() <= recordIn || seg.timelineStart >= recordOut) {
			// Completely outside — keep as is.
			surviving.push_back(seg);
		} else if (seg.timelineStart < recordIn && seg.timelineEnd() > recordOut) {
			// Segment straddles the entire overwrite region — split into two.
			Segment left = seg;
			left.duration = recordIn - seg.timelineStart;

			Segment right = seg;
			int64_t trimFront = recordOut.totalFrames() - seg.timelineStart.totalFrames();
			right.sourceIn = Timecode(seg.sourceIn.totalFrames() + trimFront, rate);
			right.timelineStart = recordOut;
			right.duration = seg.timelineEnd() - recordOut;

			surviving.push_back(left);
			surviving.push_back(right);
		} else if (seg.timelineStart < recordIn) {
			// Overlaps on the left — trim the tail.
			Segment trimmed = seg;
			trimmed.duration = recordIn - seg.timelineStart;
			surviving.push_back(trimmed);
		} else if (seg.timelineEnd() > recordOut) {
			// Overlaps on the right — trim the head.
			Segment trimmed = seg;
			int64_t trimFront = recordOut.totalFrames() - seg.timelineStart.totalFrames();
			trimmed.sourceIn = Timecode(seg.sourceIn.totalFrames() + trimFront, rate);
			trimmed.timelineStart = recordOut;
			trimmed.duration = seg.timelineEnd() - recordOut;
			surviving.push_back(trimmed);
		}
		// Else fully inside the overwrite region — discarded.
	}

	// Build the new segment.
	Segment newSeg;
	newSeg.masterClipId = masterClipId;
	newSeg.sourceIn = sourceIn;
	newSeg.duration = duration;
	newSeg.timelineStart = recordIn;
	surviving.push_back(newSeg);

	// Rebuild the track by removing all and re-adding.
	while (track.segments().size() > 0) {
		track.removeSegment(0);
	}
	// Sort by timelineStart before adding.
	std::sort(surviving.begin(), surviving.end(),
		[](const Segment & a, const Segment & b) {
			return a.timelineStart < b.timelineStart;
		});
	for (const auto & s : surviving) {
		track.addSegment(s);
	}
	return true;
}

// ---------------------------------------------------------------------------
// editSpliceIn — push downstream material right, then insert.
// ---------------------------------------------------------------------------

inline bool editSpliceIn(Track & track, const std::string & masterClipId,
						 const Timecode & sourceIn, const Timecode & duration,
						 const Timecode & recordIn)
{
	if (duration.totalFrames() <= 0) return false;

	// Ripple-shift everything at or after recordIn by duration.
	track.rippleShift(recordIn, duration.totalFrames());

	// Insert the new segment at the gap we just created.
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

	const FrameRate rate = from.rate();

	// Collect surviving segments after carving out [from, to).
	std::vector<Segment> surviving;
	for (const auto & seg : track.segments()) {
		if (seg.timelineEnd() <= from || seg.timelineStart >= to) {
			surviving.push_back(seg);
		} else if (seg.timelineStart < from && seg.timelineEnd() > to) {
			// Split.
			Segment left = seg;
			left.duration = from - seg.timelineStart;

			Segment right = seg;
			int64_t trimFront = to.totalFrames() - seg.timelineStart.totalFrames();
			right.sourceIn = Timecode(seg.sourceIn.totalFrames() + trimFront, rate);
			right.timelineStart = to;
			right.duration = seg.timelineEnd() - to;

			surviving.push_back(left);
			surviving.push_back(right);
		} else if (seg.timelineStart < from) {
			Segment trimmed = seg;
			trimmed.duration = from - seg.timelineStart;
			surviving.push_back(trimmed);
		} else if (seg.timelineEnd() > to) {
			Segment trimmed = seg;
			int64_t trimFront = to.totalFrames() - seg.timelineStart.totalFrames();
			trimmed.sourceIn = Timecode(seg.sourceIn.totalFrames() + trimFront, rate);
			trimmed.timelineStart = to;
			trimmed.duration = seg.timelineEnd() - to;
			surviving.push_back(trimmed);
		}
	}

	// Rebuild.
	while (track.segments().size() > 0) {
		track.removeSegment(0);
	}
	std::sort(surviving.begin(), surviving.end(),
		[](const Segment & a, const Segment & b) {
			return a.timelineStart < b.timelineStart;
		});
	for (const auto & s : surviving) {
		track.addSegment(s);
	}
	return true;
}

// ---------------------------------------------------------------------------
// editExtract — remove material and close gap (ripple delete).
// ---------------------------------------------------------------------------

inline bool editExtract(Track & track, const Timecode & from, const Timecode & to)
{
	if (to <= from) return false;

	// First lift, then ripple everything after 'to' left by the gap width.
	if (!editLift(track, from, to)) return false;

	int64_t gapFrames = to.totalFrames() - from.totalFrames();
	track.rippleShift(from, -gapFrames);
	return true;
}

} // namespace nle
