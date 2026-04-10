#pragma once

// ---------------------------------------------------------------------------
// Pure media metadata helper functions extracted from MediaLibrary.cpp for
// testability.  These perform string formatting and byte manipulation with
// no dependencies on OF, GLFW, or VLC at runtime.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>

namespace ofxVlc4MediaHelpers {

// ---------------------------------------------------------------------------
// Codec FOURCC ↔ string
// ---------------------------------------------------------------------------

inline std::string codecFourccToString(uint32_t codec) {
	std::string out(4, ' ');
	out[0] = static_cast<char>(codec & 0xFF);
	out[1] = static_cast<char>((codec >> 8) & 0xFF);
	out[2] = static_cast<char>((codec >> 16) & 0xFF);
	out[3] = static_cast<char>((codec >> 24) & 0xFF);

	for (char & ch : out) {
		const unsigned char uchar = static_cast<unsigned char>(ch);
		if (!std::isprint(uchar) || std::isspace(uchar)) {
			ch = '.';
		}
	}

	return out;
}

// ---------------------------------------------------------------------------
// Bitrate formatting
// ---------------------------------------------------------------------------

inline std::string formatBitrate(unsigned int bitsPerSecond) {
	if (bitsPerSecond == 0) {
		return "";
	}

	std::ostringstream stream;
	if (bitsPerSecond >= 1000000) {
		stream << std::fixed << std::setprecision(1)
			   << (static_cast<double>(bitsPerSecond) / 1000000.0) << " Mbps";
	} else {
		stream << std::fixed << std::setprecision(0)
			   << (static_cast<double>(bitsPerSecond) / 1000.0) << " kbps";
	}
	return stream.str();
}

// ---------------------------------------------------------------------------
// Frame rate formatting
// ---------------------------------------------------------------------------

inline std::string formatFrameRate(unsigned numerator, unsigned denominator) {
	if (numerator == 0 || denominator == 0) {
		return "";
	}

	std::ostringstream stream;
	stream << std::fixed << std::setprecision(2)
		   << (static_cast<double>(numerator) / static_cast<double>(denominator)) << " fps";
	return stream.str();
}

// ---------------------------------------------------------------------------
// Bookmark time label
// ---------------------------------------------------------------------------

inline std::string defaultBookmarkLabel(int timeMs) {
	const int totalSeconds = std::max(0, timeMs / 1000);
	const int hours = totalSeconds / 3600;
	const int minutes = (totalSeconds / 60) % 60;
	const int seconds = totalSeconds % 60;
	std::ostringstream stream;
	if (hours > 0) {
		stream << hours << ":" << std::setw(2) << std::setfill('0') << minutes
			   << ":" << std::setw(2) << std::setfill('0') << seconds;
		return stream.str();
	}
	stream << minutes << ":" << std::setw(2) << std::setfill('0') << seconds;
	return stream.str();
}

// ---------------------------------------------------------------------------
// Float formatting with trailing-zero stripping
// (from PlaybackController.cpp)
// ---------------------------------------------------------------------------

inline std::string formatCaptureFloatValue(float value) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(3) << value;
	std::string text = stream.str();
	while (!text.empty() && text.back() == '0') {
		text.pop_back();
	}
	if (!text.empty() && text.back() == '.') {
		text.pop_back();
	}
	return text.empty() ? "0" : text;
}

// ---------------------------------------------------------------------------
// Playback/delete decision helpers
// ---------------------------------------------------------------------------

inline bool shouldQueuePlaybackAdvanceAfterStop(bool stillStopped, bool playbackWanted) {
	return stillStopped && playbackWanted;
}

inline bool shouldClearCurrentMediaAfterPlaylistMutation(
	bool manualStopPending,
	bool hasPendingManualStopEvents) {
	return !manualStopPending && !hasPendingManualStopEvents;
}

struct DeferredManualStopDecision {
	bool shouldResetManualStop = false;
	bool shouldFinalizeManualStop = false;
	bool shouldRetryStopAsync = false;
};

inline DeferredManualStopDecision evaluateDeferredManualStop(
	bool pendingManualStopFinalize,
	bool manualStopInProgress,
	bool playbackWanted,
	bool pauseRequested,
	bool hasCurrentMedia,
	bool hasPlayer,
	bool playerStoppedOrIdle,
	uint64_t stopRequestTimeMs,
	uint64_t nowMs,
	bool manualStopRetryIssued,
	uint64_t retryDelayMs = 150,
	uint64_t fallbackDelayMs = 1500) {
	DeferredManualStopDecision decision;
	if (pendingManualStopFinalize || !manualStopInProgress || playbackWanted || pauseRequested) {
		return decision;
	}

	if (!hasCurrentMedia) {
		decision.shouldResetManualStop = true;
		return decision;
	}

	if (!hasPlayer || playerStoppedOrIdle) {
		decision.shouldFinalizeManualStop = true;
		return decision;
	}

	if (stopRequestTimeMs == 0 || nowMs < stopRequestTimeMs) {
		return decision;
	}

	if (!manualStopRetryIssued && nowMs >= stopRequestTimeMs + retryDelayMs) {
		decision.shouldRetryStopAsync = true;
	}
	if (nowMs >= stopRequestTimeMs + fallbackDelayMs) {
		decision.shouldFinalizeManualStop = true;
	}
	return decision;
}

struct StoppedPlaybackEventDecision {
	bool keepManualStopInProgress = false;
	bool shouldFinalizeManualStop = false;
	bool shouldActivatePendingRequest = false;
	bool shouldQueuePlaybackAdvance = false;
};

inline StoppedPlaybackEventDecision evaluateStoppedPlaybackEvent(
	int pendingManualStopsBeforeDecrement,
	bool hasPendingActivation,
	bool hasPendingDirectMedia,
	bool playerStoppedOrIdle,
	bool playbackWanted) {
	StoppedPlaybackEventDecision decision;
	if (pendingManualStopsBeforeDecrement > 0) {
		decision.keepManualStopInProgress = true;
		decision.shouldActivatePendingRequest = hasPendingActivation || hasPendingDirectMedia;
		decision.shouldFinalizeManualStop = !decision.shouldActivatePendingRequest;
		return decision;
	}

	decision.shouldQueuePlaybackAdvance =
		shouldQueuePlaybackAdvanceAfterStop(playerStoppedOrIdle, playbackWanted);
	return decision;
}

// ---------------------------------------------------------------------------
// WAV header writing
// ---------------------------------------------------------------------------

inline void writeWavHeader(std::ostream & stream, int sampleRate, int channels, uint32_t dataBytes) {
	if (!stream.good()) {
		return;
	}

	const uint16_t channelCount = static_cast<uint16_t>(channels);
	const uint16_t blockAlign = static_cast<uint16_t>(channels * sizeof(float));
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate * blockAlign);
	const uint32_t riffSize = 36u + dataBytes;
	const uint16_t audioFormat = 3; // IEEE float
	const uint16_t bitsPerSample = static_cast<uint16_t>(sizeof(float) * 8);
	const uint32_t fmtChunkSize = 16;

	stream.seekp(0, std::ios::beg);
	stream.write("RIFF", 4);
	stream.write(reinterpret_cast<const char *>(&riffSize), sizeof(riffSize));
	stream.write("WAVE", 4);
	stream.write("fmt ", 4);
	stream.write(reinterpret_cast<const char *>(&fmtChunkSize), sizeof(fmtChunkSize));
	stream.write(reinterpret_cast<const char *>(&audioFormat), sizeof(audioFormat));
	stream.write(reinterpret_cast<const char *>(&channelCount), sizeof(channelCount));
	stream.write(reinterpret_cast<const char *>(&sampleRate), sizeof(sampleRate));
	stream.write(reinterpret_cast<const char *>(&byteRate), sizeof(byteRate));
	stream.write(reinterpret_cast<const char *>(&blockAlign), sizeof(blockAlign));
	stream.write(reinterpret_cast<const char *>(&bitsPerSample), sizeof(bitsPerSample));
	stream.write("data", 4);
	stream.write(reinterpret_cast<const char *>(&dataBytes), sizeof(dataBytes));
}

} // namespace ofxVlc4MediaHelpers
