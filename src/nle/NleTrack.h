#pragma once

// ---------------------------------------------------------------------------
// NleTrack — Track model with non-overlapping segments.
//
// A Track holds an ordered list of Segments.  Each Segment maps a region of
// source media onto the timeline.  The Track enforces that no two segments
// overlap on the timeline.
//
// Segments are kept sorted by timelineStart.  Lookup and overlap checks use
// binary search for O(log n) performance.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <string>
#include <utility>
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
	/// Uses binary search — O(log n).
	inline int segmentIndexAt(const Timecode & tc) const;

	/// Get the last timecode on this track (end of last segment).
	inline Timecode endTimecode() const;

	/// Shift all segments at or after @p from by @p offsetFrames frames.
	inline void rippleShift(const Timecode & from, int64_t offsetFrames);

	/// Replace a segment (for trim operations).  Returns false on overlap.
	inline bool replaceSegment(size_t index, const Segment & newSeg);

	/// Bulk-replace all segments (avoids repeated addSegment overhead).
	/// Segments are sorted and validated internally.  Returns false if
	/// any overlap or zero-duration segment is detected.
	inline bool setSegments(std::vector<Segment> segs);

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
	/// Uses binary search to find candidates — O(log n) average.
	inline bool wouldOverlap(const Segment & seg, int excludeIndex = -1) const;

	/// Find the first segment whose timelineEnd > tc via binary search.
	inline size_t lowerBoundByEnd(int64_t tcFrames) const;
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
	if (m_segments.empty()) return -1;

	const int64_t tcFrames = tc.totalFrames();

	// Binary search: find rightmost segment whose timelineStart <= tc.
	// Since segments are sorted by timelineStart, use upper_bound and step back.
	auto it = std::upper_bound(
		m_segments.begin(), m_segments.end(), tcFrames,
		[](int64_t frame, const Segment & seg) {
			return frame < seg.timelineStart.totalFrames();
		});

	if (it == m_segments.begin()) return -1;
	--it;

	// Check if tc falls within [timelineStart, timelineEnd).
	if (tcFrames < it->timelineEnd().totalFrames()) {
		return static_cast<int>(std::distance(m_segments.begin(), it));
	}
	return -1;
}

inline Timecode Track::endTimecode() const {
	if (m_segments.empty()) return Timecode(0, FrameRate::Fps24);
	const auto & last = m_segments.back();
	return last.timelineEnd();
}

inline void Track::rippleShift(const Timecode & from, int64_t offsetFrames) {
	// Binary search for the first segment at or after 'from'.
	auto it = std::lower_bound(
		m_segments.begin(), m_segments.end(), from.totalFrames(),
		[](const Segment & seg, int64_t frame) {
			return seg.timelineStart.totalFrames() < frame;
		});

	for (; it != m_segments.end(); ++it) {
		int64_t newStart = it->timelineStart.totalFrames() + offsetFrames;
		if (newStart < 0) newStart = 0;
		it->timelineStart = Timecode(newStart, it->timelineStart.rate());
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

inline bool Track::setSegments(std::vector<Segment> segs) {
	// Sort by timelineStart.
	std::sort(segs.begin(), segs.end(),
		[](const Segment & a, const Segment & b) {
			return a.timelineStart < b.timelineStart;
		});

	// Validate: no zero-duration and no overlaps.
	for (size_t i = 0; i < segs.size(); ++i) {
		if (segs[i].duration.totalFrames() <= 0) return false;
		if (i > 0 && segs[i].timelineStart < segs[i - 1].timelineEnd()) {
			return false;
		}
	}

	m_segments = std::move(segs);
	return true;
}

inline void Track::sortSegments() {
	std::sort(m_segments.begin(), m_segments.end(),
		[](const Segment & a, const Segment & b) {
			return a.timelineStart < b.timelineStart;
		});
}

inline size_t Track::lowerBoundByEnd(int64_t tcFrames) const {
	// Find first segment whose timelineEnd() > tcFrames.
	// Since segments are sorted by start and non-overlapping, we can
	// binary search: a segment whose start is beyond tcFrames certainly
	// has end > tcFrames.
	size_t lo = 0;
	size_t hi = m_segments.size();
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (m_segments[mid].timelineEnd().totalFrames() <= tcFrames) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo;
}

inline bool Track::wouldOverlap(const Segment & seg, int excludeIndex) const {
	if (m_segments.empty()) return false;

	const int64_t segStart = seg.timelineStart.totalFrames();
	const int64_t segEnd   = seg.timelineEnd().totalFrames();

	// Find candidates: any existing segment whose timelineEnd > segStart
	// AND whose timelineStart < segEnd.
	// Use binary search to find the first segment that could overlap.
	size_t start = lowerBoundByEnd(segStart);

	for (size_t i = start; i < m_segments.size(); ++i) {
		if (static_cast<int>(i) == excludeIndex) continue;
		const auto & existing = m_segments[i];
		if (existing.timelineStart.totalFrames() >= segEnd) break;
		// At this point: existing.timelineEnd > segStart && existing.start < segEnd
		if (segStart < existing.timelineEnd().totalFrames() &&
			existing.timelineStart.totalFrames() < segEnd) {
			return true;
		}
	}
	return false;
}

} // namespace nle
