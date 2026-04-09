#pragma once

// ---------------------------------------------------------------------------
// NleEdlExport — CMX 3600 EDL export.
//
// Generates a CMX 3600 format Edit Decision List from a Sequence's video
// track.  Each segment becomes an edit event line of the form:
//
//   NNN  REEL     V  C        SRC_IN      SRC_OUT     REC_IN      REC_OUT
//   001  CLIP01   V  C        01:00:00:00 01:00:05:00 00:00:00:00 00:00:05:00
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <map>
#include <string>

#include "NleSequence.h"
#include "NleTimecode.h"
#include "NleTrack.h"

namespace nle {

// ---------------------------------------------------------------------------
// Helper: pad or truncate a reel name to exactly 8 characters.
// ---------------------------------------------------------------------------

namespace detail {

inline std::string formatReelName(const std::string & name) {
	std::string reel = name;
	if (reel.empty()) reel = "AX";
	if (reel.size() > 8) reel = reel.substr(0, 8);
	while (reel.size() < 8) reel += ' ';
	return reel;
}

} // namespace detail

// ---------------------------------------------------------------------------
// exportEdlCmx3600 — generate CMX 3600 EDL string.
// ---------------------------------------------------------------------------

inline std::string exportEdlCmx3600(
	const Sequence & seq,
	size_t videoTrackIndex = 0,
	const std::map<std::string, std::string> & clipIdToReelName = {})
{
	std::string edl;

	// Title header.
	edl += "TITLE: " + seq.name() + "\n";
	edl += "FCM: ";
	edl += isDropFrame(seq.rate()) ? "DROP FRAME" : "NON-DROP FRAME";
	edl += "\n";

	if (videoTrackIndex >= seq.videoTrackCount()) return edl;

	const Track & trk = seq.videoTrack(videoTrackIndex);
	const auto & segs = trk.segments();

	int eventNum = 1;
	for (const auto & seg : segs) {
		// Look up reel name (default to clip ID truncated).
		std::string reelSrc;
		auto it = clipIdToReelName.find(seg.masterClipId);
		if (it != clipIdToReelName.end()) {
			reelSrc = it->second;
		} else {
			reelSrc = seg.masterClipId;
		}
		std::string reel = detail::formatReelName(reelSrc);

		// Timecodes as SMPTE.
		Timecode srcIn  = seg.sourceIn;
		Timecode srcOut = seg.sourceOut();
		Timecode recIn  = seg.timelineStart;
		Timecode recOut = seg.timelineEnd();

		// Format event number as 3 digits.
		char numBuf[16];
		std::snprintf(numBuf, sizeof(numBuf), "%03d", eventNum);

		edl += std::string(numBuf) + "  "
			 + reel + "  "
			 + "V     C        "
			 + srcIn.toSmpteString() + " "
			 + srcOut.toSmpteString() + " "
			 + recIn.toSmpteString() + " "
			 + recOut.toSmpteString() + "\n";

		++eventNum;
	}

	return edl;
}

} // namespace nle
