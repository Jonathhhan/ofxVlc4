#pragma once

// ---------------------------------------------------------------------------
// NleSequence — Sequence / Timeline for the NLE.
//
// A Sequence owns ordered lists of video and audio Tracks and exposes
// timeline-level queries such as total duration, segment lookup by
// timecode, and playhead position.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <stdexcept>
#include <string>
#include <vector>

#include "NleTimecode.h"
#include "NleTrack.h"

namespace nle {

class Sequence {
public:
	inline Sequence(const std::string & name = "Untitled Sequence",
					FrameRate rate = FrameRate::Fps24);

	inline const std::string & name() const;
	inline void setName(const std::string & name);
	inline FrameRate rate() const;

	// -- Track management --

	inline Track & addVideoTrack(const std::string & name = "");
	inline Track & addAudioTrack(const std::string & name = "");
	inline size_t videoTrackCount() const;
	inline size_t audioTrackCount() const;
	inline Track & videoTrack(size_t index);
	inline Track & audioTrack(size_t index);
	inline const Track & videoTrack(size_t index) const;
	inline const Track & audioTrack(size_t index) const;
	inline bool removeVideoTrack(size_t index);
	inline bool removeAudioTrack(size_t index);

	// -- Timeline queries --

	/// Duration: end of last segment across all tracks.
	inline Timecode duration() const;

	/// Find segment index at a given timecode on a specific track.
	inline int segmentAtTimecode(size_t trackIndex, bool isVideo,
								 const Timecode & tc) const;

	// -- Playhead --

	inline Timecode playhead() const;
	inline void setPlayhead(const Timecode & tc);

private:
	std::string m_name;
	FrameRate m_rate;
	std::vector<Track> m_videoTracks;
	std::vector<Track> m_audioTracks;
	Timecode m_playhead;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

inline Sequence::Sequence(const std::string & name, FrameRate rate)
	: m_name(name)
	, m_rate(rate)
	, m_playhead(0, rate) {}

inline const std::string & Sequence::name() const { return m_name; }
inline void Sequence::setName(const std::string & name) { m_name = name; }
inline FrameRate Sequence::rate() const { return m_rate; }

inline Track & Sequence::addVideoTrack(const std::string & name) {
	std::string trackName = name.empty()
		? "V" + std::to_string(m_videoTracks.size() + 1)
		: name;
	m_videoTracks.emplace_back(TrackType::Video, trackName);
	return m_videoTracks.back();
}

inline Track & Sequence::addAudioTrack(const std::string & name) {
	std::string trackName = name.empty()
		? "A" + std::to_string(m_audioTracks.size() + 1)
		: name;
	m_audioTracks.emplace_back(TrackType::Audio, trackName);
	return m_audioTracks.back();
}

inline size_t Sequence::videoTrackCount() const { return m_videoTracks.size(); }
inline size_t Sequence::audioTrackCount() const { return m_audioTracks.size(); }

inline Track & Sequence::videoTrack(size_t index) {
	if (index >= m_videoTracks.size())
		throw std::out_of_range("Video track index out of range");
	return m_videoTracks[index];
}

inline Track & Sequence::audioTrack(size_t index) {
	if (index >= m_audioTracks.size())
		throw std::out_of_range("Audio track index out of range");
	return m_audioTracks[index];
}

inline const Track & Sequence::videoTrack(size_t index) const {
	if (index >= m_videoTracks.size())
		throw std::out_of_range("Video track index out of range");
	return m_videoTracks[index];
}

inline const Track & Sequence::audioTrack(size_t index) const {
	if (index >= m_audioTracks.size())
		throw std::out_of_range("Audio track index out of range");
	return m_audioTracks[index];
}

inline bool Sequence::removeVideoTrack(size_t index) {
	if (index >= m_videoTracks.size()) return false;
	m_videoTracks.erase(m_videoTracks.begin() + static_cast<ptrdiff_t>(index));
	return true;
}

inline bool Sequence::removeAudioTrack(size_t index) {
	if (index >= m_audioTracks.size()) return false;
	m_audioTracks.erase(m_audioTracks.begin() + static_cast<ptrdiff_t>(index));
	return true;
}

inline Timecode Sequence::duration() const {
	int64_t maxFrames = 0;
	for (const auto & t : m_videoTracks) {
		const int64_t f = t.endTimecode().totalFrames();
		if (f > maxFrames) maxFrames = f;
	}
	for (const auto & t : m_audioTracks) {
		const int64_t f = t.endTimecode().totalFrames();
		if (f > maxFrames) maxFrames = f;
	}
	return Timecode(maxFrames, m_rate);
}

inline int Sequence::segmentAtTimecode(size_t trackIndex, bool isVideo,
									   const Timecode & tc) const {
	if (isVideo) {
		if (trackIndex >= m_videoTracks.size()) return -1;
		return m_videoTracks[trackIndex].segmentIndexAt(tc);
	} else {
		if (trackIndex >= m_audioTracks.size()) return -1;
		return m_audioTracks[trackIndex].segmentIndexAt(tc);
	}
}

inline Timecode Sequence::playhead() const { return m_playhead; }
inline void Sequence::setPlayhead(const Timecode & tc) { m_playhead = tc; }

} // namespace nle
