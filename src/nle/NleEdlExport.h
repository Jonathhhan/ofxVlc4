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
	if (videoTrackIndex >= seq.videoTrackCount()) {
		// Header only.
		std::string edl;
		edl.reserve(64);
		edl += "TITLE: ";
		edl += seq.name();
		edl += "\nFCM: ";
		edl += isDropFrame(seq.rate()) ? "DROP FRAME" : "NON-DROP FRAME";
		edl += '\n';
		return edl;
	}

	const Track & trk = seq.videoTrack(videoTrackIndex);
	const auto & segs = trk.segments();

	// Pre-allocate: ~80 chars per header + ~80 chars per event line.
	std::string edl;
	edl.reserve(80 + segs.size() * 80);

	edl += "TITLE: ";
	edl += seq.name();
	edl += "\nFCM: ";
	edl += isDropFrame(seq.rate()) ? "DROP FRAME" : "NON-DROP FRAME";
	edl += '\n';

	// Reusable line buffer avoids per-event string concatenation overhead.
	char lineBuf[128];

	int eventNum = 1;
	for (const auto & seg : segs) {
		const auto it = clipIdToReelName.find(seg.masterClipId);
		const std::string & reelSrc = (it != clipIdToReelName.end())
			? it->second : seg.masterClipId;
		const std::string reel = detail::formatReelName(reelSrc);

		const std::string srcInStr  = seg.sourceIn.toSmpteString();
		const std::string srcOutStr = seg.sourceOut().toSmpteString();
		const std::string recInStr  = seg.timelineStart.toSmpteString();
		const std::string recOutStr = seg.timelineEnd().toSmpteString();

		const int n = std::snprintf(lineBuf, sizeof(lineBuf),
			"%03d  %s  V     C        %s %s %s %s\n",
			eventNum,
			reel.c_str(),
			srcInStr.c_str(),
			srcOutStr.c_str(),
			recInStr.c_str(),
			recOutStr.c_str());

		if (n > 0) edl.append(lineBuf, static_cast<size_t>(n));
		++eventNum;
	}

	return edl;
}

} // namespace nle
