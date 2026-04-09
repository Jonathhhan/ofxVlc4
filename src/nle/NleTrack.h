#pragma once

// ---------------------------------------------------------------------------
// NleTrack — Track model with non-overlapping segments.
//
// A Track holds an ordered list of Segments.  Each Segment maps a region of
// source media onto the timeline.  The Track enforces that no two segments
// overlap on the timeline.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <string>
#include <vector>

#include "NleTimecode.h"

namespace nle {

// ---------------------------------------------------------------------------
// TrackType
// ---------------------------------------------------------------------------

enum class TrackType { Video, Audio };

// ---------------------------------------------------------------------------
// Segment — one contiguous piece of media on a track.
// ---------------------------------------------------------------------------

struct Segment {
	std::string masterClipId;  ///< Which source media
	Timecode sourceIn;         ///< Where in the source to start
	Timecode duration;         ///< How long
	Timecode timelineStart;    ///< Where on the timeline this sits
	float gain = 1.0f;        ///< Audio gain (audio tracks only)
	float pan = 0.0f;         ///< Audio pan (-1 left, 0 center, 1 right)

	/// End timecode on the timeline (exclusive).
	Timecode timelineEnd() const { return timelineStart + duration; }

	/// Source OUT timecode (exclusive).
	Timecode sourceOut() const { return sourceIn + duration; }
};

// ---------------------------------------------------------------------------
// Track
// ---------------------------------------------------------------------------

class Track {
public:
	inline Track(TrackType type, const std::string & name);

	inline TrackType type() const;
	inline const std::string & name() const;

	inline bool isLocked() const;
	inline void setLocked(bool locked);
	inline bool isMuted() const;
	inline void setMuted(bool muted);
	inline bool isSoloed() const;
	inline void setSoloed(bool soloed);

	/// All segments on this track (sorted by timelineStart).
	inline const std::vector<Segment> & segments() const;

	/// Add a segment.  Returns false if it would overlap an existing segment.
	inline bool addSegment(const Segment & seg);

	/// Remove segment at index.  Returns false if index is out of range.
	inline bool removeSegment(size_t index);

	/// Find segment at a given timeline timecode.  Returns index or -1.
	inline int segmentIndexAt(const Timecode & tc) const;

	/// Get the last timecode on this track (end of last segment).
	inline Timecode endTimecode() const;

	/// Shift all segments at or after @p from by @p offsetFrames frames.
	inline void rippleShift(const Timecode & from, int64_t offsetFrames);

	/// Replace a segment (for trim operations).  Returns false on overlap.
	inline bool replaceSegment(size_t index, const Segment & newSeg);

private:
	TrackType m_type;
	std::string m_name;
	bool m_locked = false;
	bool m_muted = false;
	bool m_soloed = false;
	std::vector<Segment> m_segments;

	/// Keep segments sorted by timelineStart.
	inline void sortSegments();

	/// Check if a segment would overlap any existing (excluding excludeIndex).
	inline bool wouldOverlap(const Segment & seg, int excludeIndex = -1) const;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

inline Track::Track(TrackType type, const std::string & name)
	: m_type(type)
	, m_name(name) {}

inline TrackType Track::type() const { return m_type; }
inline const std::string & Track::name() const { return m_name; }

inline bool Track::isLocked() const { return m_locked; }
inline void Track::setLocked(bool locked) { m_locked = locked; }
inline bool Track::isMuted() const { return m_muted; }
inline void Track::setMuted(bool muted) { m_muted = muted; }
inline bool Track::isSoloed() const { return m_soloed; }
inline void Track::setSoloed(bool soloed) { m_soloed = soloed; }

inline const std::vector<Segment> & Track::segments() const {
	return m_segments;
}

inline bool Track::addSegment(const Segment & seg) {
	if (seg.duration.totalFrames() <= 0) return false;
	if (wouldOverlap(seg)) return false;
	m_segments.push_back(seg);
	sortSegments();
	return true;
}

inline bool Track::removeSegment(size_t index) {
	if (index >= m_segments.size()) return false;
	m_segments.erase(m_segments.begin() + static_cast<ptrdiff_t>(index));
	return true;
}

inline int Track::segmentIndexAt(const Timecode & tc) const {
	for (size_t i = 0; i < m_segments.size(); ++i) {
		const auto & seg = m_segments[i];
		if (tc >= seg.timelineStart && tc < seg.timelineEnd()) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

inline Timecode Track::endTimecode() const {
	if (m_segments.empty()) return Timecode(0, FrameRate::Fps24);
	// Segments are sorted — last one has the greatest timelineStart.
	const auto & last = m_segments.back();
	return last.timelineEnd();
}

inline void Track::rippleShift(const Timecode & from, int64_t offsetFrames) {
	for (auto & seg : m_segments) {
		if (seg.timelineStart >= from) {
			int64_t newStart = seg.timelineStart.totalFrames() + offsetFrames;
			if (newStart < 0) newStart = 0;
			seg.timelineStart = Timecode(newStart, seg.timelineStart.rate());
		}
	}
	sortSegments();
}

inline bool Track::replaceSegment(size_t index, const Segment & newSeg) {
	if (index >= m_segments.size()) return false;
	if (newSeg.duration.totalFrames() <= 0) return false;
	if (wouldOverlap(newSeg, static_cast<int>(index))) return false;
	m_segments[index] = newSeg;
	sortSegments();
	return true;
}

inline void Track::sortSegments() {
	std::sort(m_segments.begin(), m_segments.end(),
		[](const Segment & a, const Segment & b) {
			return a.timelineStart < b.timelineStart;
		});
}

inline bool Track::wouldOverlap(const Segment & seg, int excludeIndex) const {
	for (size_t i = 0; i < m_segments.size(); ++i) {
		if (static_cast<int>(i) == excludeIndex) continue;
		const auto & existing = m_segments[i];
		// Two intervals [a, b) and [c, d) overlap iff a < d && c < b.
		if (seg.timelineStart < existing.timelineEnd() &&
			existing.timelineStart < seg.timelineEnd()) {
			return true;
		}
	}
	return false;
}

} // namespace nle
