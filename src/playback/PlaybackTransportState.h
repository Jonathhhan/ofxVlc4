#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct PlaybackTransportState {
	std::atomic<bool> playbackWanted { false };
	std::atomic<bool> pauseRequested { false };
	std::atomic<bool> audioPausedSignal { false };
	std::string pendingDirectMediaSource;
	std::vector<std::string> pendingDirectMediaOptions;
	std::string pendingDirectMediaLabel;
	bool pendingDirectMediaIsLocation = true;
	bool pendingDirectMediaParseAsNetwork = false;
	// Atomic mirror of !pendingDirectMediaSource.empty() so VLC callback
	// threads can query it without a data race on the std::string.
	std::atomic<bool> hasPendingDirectMedia { false };
	std::string activeDirectMediaSource;
	std::vector<std::string> activeDirectMediaOptions;
	bool activeDirectMediaIsLocation = true;
	bool activeDirectMediaParseAsNetwork = false;
	std::atomic<bool> manualStopInProgress { false };
	std::atomic<bool> pendingManualStopFinalize { false };
	std::atomic<int> pendingManualStopEvents { 0 };
	std::atomic<uint64_t> manualStopRequestTimeMs { 0 };
	std::atomic<bool> manualStopRetryIssued { false };
	std::atomic<bool> stoppedStateLatched { false };
	std::atomic<bool> playNextRequested { false };
	std::atomic<int> pendingActivateIndex { -1 };
	std::atomic<bool> pendingActivateShouldPlay { false };
	std::atomic<bool> pendingActivateReady { false };
	mutable std::atomic<float> lastKnownPlaybackPosition { 0.0f };
	std::atomic<float> bufferCache { 0.0f };
	std::atomic<bool> seekableLatched { false };
	std::atomic<bool> pausableLatched { false };
	std::atomic<unsigned> cachedVideoOutputCount { 0 };
	std::atomic<bool> corked { false };
};
