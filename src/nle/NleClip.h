#pragma once

// ---------------------------------------------------------------------------
// NleClip — MasterClip and SubClip data models for the NLE.
//
// A MasterClip represents a source media file and its cached metadata.
// A SubClip represents a named sub-range within a MasterClip, defined by
// source IN and OUT timecodes.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <string>

#include "NleTimecode.h"

namespace nle {

// ---------------------------------------------------------------------------
// MasterClip — a reference to source media with cached metadata.
// ---------------------------------------------------------------------------

struct MasterClip {
	std::string id;        ///< Unique ID (e.g. UUID or incrementing)
	std::string filePath;  ///< Absolute path to source file
	std::string name;      ///< Display name (usually filename)

	// Cached metadata (populated after media parse)
	std::string videoCodec;
	std::string audioCodec;
	int width = 0;
	int height = 0;
	double frameRateNum = 0;  ///< Numerator
	double frameRateDen = 1;  ///< Denominator
	int audioChannels = 0;
	int audioSampleRate = 0;
	int durationMs = 0;       ///< Total duration in milliseconds
	FrameRate timecodeRate = FrameRate::Fps24;
	Timecode startTimecode;   ///< Source start TC (usually 01:00:00:00)

	/// Computed duration as a Timecode.
	Timecode duration() const {
		return Timecode::fromMilliseconds(durationMs, timecodeRate);
	}
};

// ---------------------------------------------------------------------------
// SubClip — a named sub-range within a MasterClip.
// ---------------------------------------------------------------------------

struct SubClip {
	std::string masterClipId;  ///< References a MasterClip by id
	std::string name;
	Timecode inPoint;   ///< Source IN (relative to MasterClip start)
	Timecode outPoint;  ///< Source OUT

	/// Duration of this sub-clip.
	Timecode duration() const { return outPoint - inPoint; }

	/// IN point expressed in milliseconds.
	int inPointMs() const { return inPoint.toMilliseconds(); }

	/// OUT point expressed in milliseconds.
	int outPointMs() const { return outPoint.toMilliseconds(); }
};

} // namespace nle
