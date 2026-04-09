#include "PlaybackController.h"

#include "audio/ofxVlc4Audio.h"
#include "media/ofxVlc4Media.h"
#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "support/ofxVlc4Utils.h"
#include "video/ofxVlc4Video.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using ofxVlc4Utils::isStoppedOrIdleState;
using ofxVlc4Utils::isTransientPlaybackState;
using ofxVlc4Utils::trimWhitespace;

namespace {

std::string formatCaptureFloatValue(float value) {
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

}

PlaybackController::PlaybackController(ofxVlc4 & owner)
	: owner(owner) {
}

PlaybackController::PlaylistSnapshot PlaybackController::getPlaylistSnapshot() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	return PlaylistSnapshot { owner.m_impl->mediaLibrary.playlist.size(), owner.m_impl->mediaLibrary.currentIndex };
}

void PlaybackController::setCurrentPlaylistIndex(int index) {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	owner.m_impl->mediaLibrary.currentIndex = index;
}

void PlaybackController::clearCurrentPlaylistIndex() {
	setCurrentPlaylistIndex(-1);
}

ofxVlc4::AudioComponent & PlaybackController::audio() const {
	return *owner.m_impl->subsystemRuntime.audioComponent;
}

ofxVlc4::VideoComponent & PlaybackController::video() const {
	return *owner.m_impl->subsystemRuntime.videoComponent;
}

ofxVlc4::MediaComponent & PlaybackController::media() const {
	return *owner.m_impl->subsystemRuntime.mediaComponent;
}

void PlaybackController::resetAudioBuffer() {
	audio().resetBuffer();
}

void PlaybackController::applyPendingEqualizerOnPlay() {
	audio().applyPendingEqualizerOnPlay();
}

void PlaybackController::applyPendingVideoAdjustmentsOnPlay() {
	video().applyPendingVideoAdjustmentsOnPlay();
}

void PlaybackController::clearPendingEqualizerApplyOnPlay() {
	audio().clearPendingEqualizerApplyOnPlay();
}

void PlaybackController::clearCurrentMedia(bool clearVideoResources) {
	media().clearCurrentMedia(clearVideoResources);
}

void PlaybackController::clearWatchTimeState() {
	media().clearWatchTimeState();
}

void PlaybackController::clearAudioPtsState() {
	audio().clearAudioPtsState();
}

void PlaybackController::setPlaybackModeValue(ofxVlc4::PlaybackMode mode) {
	owner.m_impl->playbackPolicyRuntime.playbackMode = mode;
}

ofxVlc4::PlaybackMode PlaybackController::getPlaybackModeValue() const {
	return owner.m_impl->playbackPolicyRuntime.playbackMode;
}

void PlaybackController::setShuffleEnabledValue(bool enabled) {
	owner.m_impl->playbackPolicyRuntime.shuffleEnabled = enabled;
}

bool PlaybackController::isShuffleEnabledValue() const {
	return owner.m_impl->playbackPolicyRuntime.shuffleEnabled;
}

void PlaybackController::playIndex(int index) {
	if (!owner.sessionPlayer()) return;
	if (!getPlaylistSnapshot().contains(index)) return;

	activatePlaylistIndex(index, true);
}

void PlaybackController::activatePlaylistIndex(int index, bool shouldPlay) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) return;
	if (!getPlaylistSnapshot().contains(index)) return;

	if (playbackTransport.pendingManualStopEvents.load() > 0) {
		const libvlc_state_t state = libvlc_media_player_get_state(player);
		if (isStoppedOrIdleState(state)) {
			playbackTransport.pendingManualStopEvents.store(0);
			clearPendingActivationRequest();
		} else {
			playbackTransport.pendingActivateIndex.store(index);
			playbackTransport.pendingActivateShouldPlay.store(shouldPlay);
			playbackTransport.pendingActivateReady.store(false);
			playbackTransport.playNextRequested.store(false);
			resetAudioBuffer();
			return;
		}
	}

	libvlc_media_t * loadedMedia = libvlc_media_player_get_media(player);
	const bool hasLoadedMedia = (loadedMedia != nullptr);
	if (loadedMedia) {
		libvlc_media_release(loadedMedia);
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	const bool needsAsyncStop =
		hasLoadedMedia &&
		!isStoppedOrIdleState(state);

	if (needsAsyncStop) {
		playbackTransport.pendingActivateIndex.store(index);
		playbackTransport.pendingActivateShouldPlay.store(shouldPlay);
		playbackTransport.pendingActivateReady.store(false);
		playbackTransport.pendingManualStopEvents.store(1);
		playbackTransport.playNextRequested.store(false);
		resetAudioBuffer();
		libvlc_media_player_stop_async(player);
		return;
	}

	clearPendingActivationRequest();
	activatePlaylistIndexImmediate(index, shouldPlay);
}

void PlaybackController::activatePlaylistIndexImmediate(int index, bool shouldPlay) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) return;
	if (!getPlaylistSnapshot().contains(index)) return;

	invalidateShuffleQueue();
	setCurrentPlaylistIndex(index);
	playbackTransport.activeDirectMediaSource.clear();
	playbackTransport.activeDirectMediaOptions.clear();
	playbackTransport.activeDirectMediaIsLocation = true;
	playbackTransport.activeDirectMediaParseAsNetwork = false;
	playbackTransport.playNextRequested.store(false);
	resetAudioBuffer();
	playbackTransport.playbackWanted.store(shouldPlay);
	playbackTransport.manualStopInProgress.store(false);
	playbackTransport.pendingManualStopFinalize.store(false);
	playbackTransport.manualStopRetryIssued.store(false);
	playbackTransport.stoppedStateLatched.store(false);

	if (!media().loadMediaAtIndex(index)) {
		return;
	}

	if (shouldPlay) {
		applyPendingEqualizerOnPlay();
		applyPendingVideoAdjustmentsOnPlay();
		libvlc_media_player_play(player);
	}
}

bool PlaybackController::activateDirectMediaImmediate(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool shouldPlay,
	bool parseAsNetwork,
	const std::string & label) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	clearCurrentPlaylistIndex();
	playbackTransport.playNextRequested.store(false);
	resetAudioBuffer();
	playbackTransport.playbackWanted.store(shouldPlay);
	playbackTransport.pauseRequested.store(false);
	playbackTransport.audioPausedSignal.store(false);
	playbackTransport.manualStopInProgress.store(false);
	playbackTransport.pendingManualStopFinalize.store(false);
	playbackTransport.stoppedStateLatched.store(false);

	if (!media().loadMediaSource(source, isLocation, options, parseAsNetwork)) {
		return false;
	}

	playbackTransport.activeDirectMediaSource = source;
	playbackTransport.activeDirectMediaOptions = options;
	playbackTransport.activeDirectMediaIsLocation = isLocation;
	playbackTransport.activeDirectMediaParseAsNetwork = parseAsNetwork;

	if (shouldPlay) {
		applyPendingEqualizerOnPlay();
		applyPendingVideoAdjustmentsOnPlay();
		libvlc_media_player_play(player);
		owner.logNotice("Playback started.");
	}

	if (!label.empty()) {
		owner.setStatus(label);
	}
	return true;
}

bool PlaybackController::requestDirectMediaActivation(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool shouldPlay,
	bool parseAsNetwork,
	const std::string & label) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		owner.setError("Initialize libvlc first.");
		return false;
	}
	if (trimWhitespace(source).empty()) {
		owner.setError("Capture source is empty.");
		return false;
	}

	if (playbackTransport.pendingManualStopEvents.load() > 0) {
		const libvlc_state_t state = libvlc_media_player_get_state(player);
		if (isStoppedOrIdleState(state)) {
			playbackTransport.pendingManualStopEvents.store(0);
			clearPendingActivationRequest();
		} else {
			playbackTransport.pendingDirectMediaSource = source;
			playbackTransport.pendingDirectMediaOptions = options;
			playbackTransport.pendingDirectMediaLabel = label;
			playbackTransport.pendingDirectMediaIsLocation = isLocation;
			playbackTransport.pendingDirectMediaParseAsNetwork = parseAsNetwork;
			playbackTransport.pendingActivateShouldPlay.store(shouldPlay);
			playbackTransport.pendingActivateReady.store(false);
			playbackTransport.playNextRequested.store(false);
			resetAudioBuffer();
			return true;
		}
	}

	libvlc_media_t * loadedMedia = libvlc_media_player_get_media(player);
	const bool hasLoadedMedia = (loadedMedia != nullptr);
	if (loadedMedia) {
		libvlc_media_release(loadedMedia);
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	const bool needsAsyncStop = hasLoadedMedia && !isStoppedOrIdleState(state);
	if (needsAsyncStop) {
		playbackTransport.pendingDirectMediaSource = source;
		playbackTransport.pendingDirectMediaOptions = options;
		playbackTransport.pendingDirectMediaLabel = label;
		playbackTransport.pendingDirectMediaIsLocation = isLocation;
		playbackTransport.pendingDirectMediaParseAsNetwork = parseAsNetwork;
		playbackTransport.pendingActivateShouldPlay.store(shouldPlay);
		playbackTransport.pendingActivateReady.store(false);
		playbackTransport.pendingManualStopEvents.store(1);
		playbackTransport.playNextRequested.store(false);
		resetAudioBuffer();
		libvlc_media_player_stop_async(player);
		return true;
	}

	clearPendingActivationRequest();
	return activateDirectMediaImmediate(source, isLocation, options, shouldPlay, parseAsNetwork, label);
}

bool PlaybackController::openDshowCapture(
	const std::string & videoDevice,
	const std::string & audioDevice,
	int width,
	int height,
	float fps) {
#ifdef _WIN32
	const std::string trimmedVideoDevice = trimWhitespace(videoDevice);
	if (trimmedVideoDevice.empty()) {
		owner.setError("Video device is empty.");
		return false;
	}

	std::vector<std::string> options;
	options.emplace_back(":dshow-vdev=" + trimmedVideoDevice);

	const std::string trimmedAudioDevice = trimWhitespace(audioDevice);
	if (!trimmedAudioDevice.empty()) {
		options.emplace_back(":dshow-adev=" + trimmedAudioDevice);
	}
	if (width > 0 && height > 0) {
		options.emplace_back(":dshow-size=" + ofToString(width) + "x" + ofToString(height));
	}
	if (fps > 0.0f) {
		options.emplace_back(":dshow-fps=" + formatCaptureFloatValue(fps));
	}

	return requestDirectMediaActivation("dshow://", true, options, true, false, "DirectShow capture opened.");
#else
	(void)videoDevice;
	(void)audioDevice;
	(void)width;
	(void)height;
	(void)fps;
	owner.setError("DirectShow capture is only available on Windows.");
	return false;
#endif
}

bool PlaybackController::openScreenCapture(int width, int height, float fps, int left, int top) {
	std::vector<std::string> options;
	if (width > 0) {
		options.emplace_back(":screen-width=" + ofToString(width));
	}
	if (height > 0) {
		options.emplace_back(":screen-height=" + ofToString(height));
	}
	if (fps > 0.0f) {
		options.emplace_back(":screen-fps=" + formatCaptureFloatValue(fps));
	}
	if (left != 0) {
		options.emplace_back(":screen-left=" + ofToString(left));
	}
	if (top != 0) {
		options.emplace_back(":screen-top=" + ofToString(top));
	}

	return requestDirectMediaActivation("screen://", true, options, true, false, "Screen capture opened.");
}

void PlaybackController::setPosition(float pct) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || !owner.sessionMedia() || isPlaybackLocallyStopped()) {
		return;
	}
	if (!libvlc_media_player_is_seekable(player)) {
		return;
	}

	resetAudioBuffer();
	libvlc_media_player_set_position(player, pct, true);
}

void PlaybackController::play() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) return;

	libvlc_media_t * currentMedia = libvlc_media_player_get_media(player);
	const bool hasLoadedMedia = (currentMedia != nullptr);
	if (currentMedia) {
		libvlc_media_release(currentMedia);
	}
	int indexToLoad = -1;
	bool shouldReloadDirectMedia = false;
	PlaylistSnapshot playlist = getPlaylistSnapshot();
	if (playlist.size == 0 && !hasLoadedMedia) {
		shouldReloadDirectMedia = !trimWhitespace(playbackTransport.activeDirectMediaSource).empty();
		if (!shouldReloadDirectMedia) {
			return;
		}
	}
	if (!hasLoadedMedia && playlist.size > 0) {
		if (!playlist.contains(playlist.currentIndex)) {
			setCurrentPlaylistIndex(0);
			playlist.currentIndex = 0;
		}
		indexToLoad = playlist.currentIndex;
	}

	playbackTransport.playNextRequested.store(false);
	playbackTransport.playbackWanted.store(true);
	playbackTransport.pauseRequested.store(false);
	playbackTransport.audioPausedSignal.store(false);
	playbackTransport.manualStopInProgress.store(false);
	playbackTransport.pendingManualStopFinalize.store(false);
	playbackTransport.manualStopRequestTimeMs.store(0);
	playbackTransport.manualStopRetryIssued.store(false);
	playbackTransport.stoppedStateLatched.store(false);
	const libvlc_state_t state = libvlc_media_player_get_state(player);
	if (playbackTransport.pendingManualStopEvents.load() > 0) {
		if (isStoppedOrIdleState(state)) {
			playbackTransport.pendingManualStopEvents.store(0);
		} else {
			playbackTransport.pendingActivateShouldPlay.store(true);
			return;
		}
	}
	if (playbackTransport.pendingActivateIndex.load() >= 0) {
		playbackTransport.pendingActivateShouldPlay.store(true);
		return;
	}
	if (isTransientPlaybackState(state)) {
		return;
	}

	if (!hasLoadedMedia && shouldReloadDirectMedia) {
		if (!media().loadMediaSource(
				playbackTransport.activeDirectMediaSource,
				playbackTransport.activeDirectMediaIsLocation,
				playbackTransport.activeDirectMediaOptions,
				playbackTransport.activeDirectMediaParseAsNetwork)) {
			return;
		}
	} else if (!hasLoadedMedia) {
		if (!media().loadMediaAtIndex(indexToLoad)) {
			return;
		}
	}

	if (state == libvlc_Paused) {
		libvlc_media_player_set_pause(player, 0);
	} else {
		applyPendingEqualizerOnPlay();
		applyPendingVideoAdjustmentsOnPlay();
		libvlc_media_player_play(player);
	}
	owner.logNotice("Playback started.");
}

void PlaybackController::pause() {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		if (!owner.sessionMedia() || isPlaybackLocallyStopped()) {
			playbackTransport.pauseRequested.store(false);
			playbackTransport.playbackWanted.store(false);
			playbackTransport.audioPausedSignal.store(false);
			return;
		}

		const libvlc_state_t state = libvlc_media_player_get_state(player);
		const bool hasQueuedActivation = playbackTransport.pendingManualStopEvents.load() > 0 || playbackTransport.pendingActivateIndex.load() >= 0;
		const bool pauseSignaled = playbackTransport.audioPausedSignal.load();
		const bool shouldResume = playbackTransport.pauseRequested.load() || (pauseSignaled && (state == libvlc_Paused || hasQueuedActivation));
		if (shouldResume) {
			play();
			return;
		}
		if (isStoppedOrIdleState(state) && !hasQueuedActivation) {
			playbackTransport.pauseRequested.store(false);
			playbackTransport.playbackWanted.store(false);
			playbackTransport.audioPausedSignal.store(false);
			return;
		}

		playbackTransport.pauseRequested.store(true);
		playbackTransport.playbackWanted.store(false);
		if (hasQueuedActivation) {
			playbackTransport.pendingActivateShouldPlay.store(false);
			return;
		}
		if (isTransientPlaybackState(state)) {
			return;
		}
		if (state == libvlc_Playing) {
			libvlc_media_player_set_pause(player, 1);
			owner.logNotice("Playback paused.");
		}
	}
}

void PlaybackController::stop() {
	const bool shuttingDown = owner.m_impl->lifecycleRuntime.shuttingDown.load();
	clearPendingActivationRequest();
	playbackTransport.playNextRequested.store(false);
	playbackTransport.playbackWanted.store(false);
	playbackTransport.pauseRequested.store(false);
	playbackTransport.audioPausedSignal.store(false);
	resetAudioBuffer();
	playbackTransport.lastKnownPlaybackPosition.store(0.0f, std::memory_order_relaxed);
	playbackTransport.pendingManualStopFinalize.store(false);
	playbackTransport.stoppedStateLatched.store(true);
	clearWatchTimeState();

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		if (!shuttingDown) {
			owner.stopActiveRecorderSessions();
		}
		playbackTransport.manualStopInProgress.store(false);
		playbackTransport.pendingManualStopEvents.store(0);
		playbackTransport.manualStopRetryIssued.store(false);
		owner.logNotice("Playback stopped.");
		return;
	}

	libvlc_media_t * loadedMedia = libvlc_media_player_get_media(player);
	const bool hasLoadedMedia = (loadedMedia != nullptr);
	if (loadedMedia) {
		libvlc_media_release(loadedMedia);
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	const bool shouldStopActivePlayback = hasLoadedMedia && !isStoppedOrIdleState(state);
	if (!shouldStopActivePlayback) {
		if (!shuttingDown) {
			owner.stopActiveRecorderSessions();
		}
		playbackTransport.manualStopInProgress.store(false);
		clearCurrentMedia();
		playbackTransport.pendingManualStopEvents.store(0);
		playbackTransport.manualStopRequestTimeMs.store(0);
		playbackTransport.manualStopRetryIssued.store(false);
		owner.logNotice("Playback stopped.");
		return;
	}

	if (playbackTransport.pendingManualStopEvents.exchange(1) == 0) {
		playbackTransport.manualStopInProgress.store(true);
		playbackTransport.pendingManualStopFinalize.store(false);
		playbackTransport.manualStopRequestTimeMs.store(ofGetElapsedTimeMillis());
		playbackTransport.manualStopRetryIssued.store(false);
		libvlc_media_player_stop_async(player);
		clearCurrentMedia();
	}

	owner.logNotice("Playback stopped.");
}

void PlaybackController::resetTransportState() {
	playbackTransport.manualStopInProgress.store(false);
	playbackTransport.pendingManualStopFinalize.store(false);
	playbackTransport.pendingManualStopEvents.store(0);
	playbackTransport.manualStopRequestTimeMs.store(0);
	playbackTransport.manualStopRetryIssued.store(false);
	playbackTransport.stoppedStateLatched.store(false);
	playbackTransport.playNextRequested.store(false);
	playbackTransport.playbackWanted.store(false);
	playbackTransport.pauseRequested.store(false);
	playbackTransport.audioPausedSignal.store(false);
	playbackTransport.lastKnownPlaybackPosition.store(0.0f, std::memory_order_relaxed);
	playbackTransport.bufferCache.store(0.0f, std::memory_order_relaxed);
	playbackTransport.seekableLatched.store(false, std::memory_order_relaxed);
	playbackTransport.pausableLatched.store(false, std::memory_order_relaxed);
	playbackTransport.cachedVideoOutputCount.store(0, std::memory_order_relaxed);
	playbackTransport.corked.store(false, std::memory_order_relaxed);
	clearPendingActivationRequest();
	playbackTransport.activeDirectMediaSource.clear();
	playbackTransport.activeDirectMediaOptions.clear();
	playbackTransport.activeDirectMediaIsLocation = true;
	playbackTransport.activeDirectMediaParseAsNetwork = false;
	invalidateShuffleQueue();
}

void PlaybackController::clearPendingActivationRequest() {
	playbackTransport.pendingActivateIndex.store(-1);
	playbackTransport.pendingActivateShouldPlay.store(false);
	playbackTransport.pendingActivateReady.store(false);
	playbackTransport.pendingDirectMediaSource.clear();
	playbackTransport.pendingDirectMediaOptions.clear();
	playbackTransport.pendingDirectMediaLabel.clear();
	playbackTransport.pendingDirectMediaIsLocation = true;
	playbackTransport.pendingDirectMediaParseAsNetwork = false;
}

void PlaybackController::buildShuffleQueue(int excludeIndex) {
	const PlaylistSnapshot playlist = getPlaylistSnapshot();
	shuffleQueue.clear();
	for (int i = 0; i < static_cast<int>(playlist.size); ++i) {
		if (i != excludeIndex) {
			shuffleQueue.push_back(i);
		}
	}
	for (int i = static_cast<int>(shuffleQueue.size()) - 1; i > 0; --i) {
		const int j = static_cast<int>(ofRandom(static_cast<float>(i + 1)));
		std::swap(shuffleQueue[i], shuffleQueue[j]);
	}
	shuffleQueueBuilt = true;
}

void PlaybackController::invalidateShuffleQueue() {
	shuffleQueue.clear();
	shuffleQueueBuilt = false;
}

int PlaybackController::popNextFromShuffleQueue() {
	const PlaylistSnapshot playlist = getPlaylistSnapshot();
	while (!shuffleQueue.empty()) {
		const int next = shuffleQueue.back();
		shuffleQueue.pop_back();
		if (playlist.contains(next)) {
			return next;
		}
	}
	return -1;
}

int PlaybackController::nextFromShuffleQueueLazy(int excludeIndex) {
	if (!shuffleQueueBuilt) {
		buildShuffleQueue(excludeIndex);
	}
	int next = popNextFromShuffleQueue();
	if (next < 0 && getPlaybackMode() == ofxVlc4::PlaybackMode::Loop) {
		buildShuffleQueue(excludeIndex);
		next = popNextFromShuffleQueue();
	}
	return next;
}

void PlaybackController::handlePlaybackEnded() {
	const PlaylistSnapshot playlist = getPlaylistSnapshot();
	const int activeIndex = playlist.currentIndex;
	const size_t playlistSize = playlist.size;
	if (playlistSize == 0 || activeIndex < 0 || activeIndex >= static_cast<int>(playlistSize)) {
		playbackTransport.playNextRequested.store(false);
		playbackTransport.playbackWanted.store(false);
		playbackTransport.pauseRequested.store(false);
		playbackTransport.audioPausedSignal.store(false);
		resetAudioBuffer();
		playbackTransport.lastKnownPlaybackPosition.store(0.0f, std::memory_order_relaxed);
		playbackTransport.stoppedStateLatched.store(true);
		clearCurrentMedia();
		return;
	}

	if (getPlaybackMode() == ofxVlc4::PlaybackMode::Repeat) {
		playbackTransport.playNextRequested.store(false);
		resetAudioBuffer();

		if (libvlc_media_player_t * player = owner.sessionPlayer()) {
			libvlc_media_player_set_time(player, 0, true);
			libvlc_media_player_play(player);
			return;
		}

		playIndex(activeIndex);
		return;
	}

	if (isShuffleEnabled()) {
		int next = nextFromShuffleQueueLazy(activeIndex);
		if (next >= 0) {
			playIndex(next);
			return;
		}
		playbackTransport.playNextRequested.store(false);
		playbackTransport.playbackWanted.store(false);
		playbackTransport.pauseRequested.store(false);
		playbackTransport.audioPausedSignal.store(false);
		resetAudioBuffer();
		playbackTransport.lastKnownPlaybackPosition.store(0.0f, std::memory_order_relaxed);
		playbackTransport.stoppedStateLatched.store(true);
		clearCurrentMedia();
		return;
	}

	if (activeIndex + 1 < static_cast<int>(playlistSize)) {
		playIndex(activeIndex + 1);
		return;
	}

	if (getPlaybackMode() == ofxVlc4::PlaybackMode::Loop) {
		playIndex(0);
		return;
	}

	playbackTransport.playNextRequested.store(false);
	playbackTransport.playbackWanted.store(false);
	playbackTransport.pauseRequested.store(false);
	playbackTransport.audioPausedSignal.store(false);
	resetAudioBuffer();
	playbackTransport.lastKnownPlaybackPosition.store(0.0f, std::memory_order_relaxed);
	playbackTransport.stoppedStateLatched.store(true);
	clearCurrentMedia();
}

void PlaybackController::processDeferredPlaybackActions() {
	constexpr uint64_t kManualStopRetryMs = 150;
	constexpr uint64_t kManualStopFallbackMs = 1500;
	libvlc_media_player_t * player = owner.sessionPlayer();
	libvlc_media_t * currentMedia = owner.sessionMedia();

	if (!playbackTransport.pendingManualStopFinalize.load() &&
		playbackTransport.manualStopInProgress.load() &&
		!playbackTransport.playbackWanted.load() &&
		!playbackTransport.pauseRequested.load()) {
		const libvlc_state_t state = player ? libvlc_media_player_get_state(player) : libvlc_Stopped;
		if (!player || isStoppedOrIdleState(state)) {
			playbackTransport.pendingManualStopEvents.store(0);
			playbackTransport.manualStopRequestTimeMs.store(0);
			playbackTransport.manualStopRetryIssued.store(false);
			playbackTransport.pendingManualStopFinalize.store(true);
		} else {
			const uint64_t stopRequestTimeMs = playbackTransport.manualStopRequestTimeMs.load();
			const uint64_t nowMs = ofGetElapsedTimeMillis();
			if (stopRequestTimeMs > 0) {
				if (!playbackTransport.manualStopRetryIssued.load() &&
					nowMs >= stopRequestTimeMs + kManualStopRetryMs) {
					libvlc_media_player_stop_async(player);
					playbackTransport.manualStopRetryIssued.store(true);
				}
				if (nowMs >= stopRequestTimeMs + kManualStopFallbackMs) {
					playbackTransport.pendingManualStopEvents.store(0);
					playbackTransport.manualStopRequestTimeMs.store(0);
					playbackTransport.manualStopRetryIssued.store(false);
					playbackTransport.pendingManualStopFinalize.store(true);
				}
			}
		}
	}

	if (player &&
		currentMedia &&
		!playbackTransport.manualStopInProgress.load() &&
		!playbackTransport.pendingManualStopFinalize.load() &&
		playbackTransport.pendingManualStopEvents.load() == 0 &&
		playbackTransport.playbackWanted.load() &&
		!playbackTransport.pauseRequested.load() &&
		playbackTransport.pendingActivateIndex.load() < 0 &&
		!playbackTransport.pendingActivateReady.load() &&
		playbackTransport.pendingDirectMediaSource.empty()) {
		const libvlc_state_t state = libvlc_media_player_get_state(player);
		if (isStoppedOrIdleState(state)) {
			playbackTransport.playNextRequested.store(true);
		}
	}

	if (playbackTransport.pendingManualStopFinalize.exchange(false)) {
		playbackTransport.manualStopRequestTimeMs.store(0);
		playbackTransport.manualStopRetryIssued.store(false);
		clearCurrentMedia();
		if (!owner.m_impl->lifecycleRuntime.shuttingDown.load()) {
			owner.stopActiveRecorderSessions();
		}
	}

	if (playbackTransport.pendingActivateReady.exchange(false)) {
		const int pendingIndex = playbackTransport.pendingActivateIndex.exchange(-1);
		const bool shouldPlay = playbackTransport.pendingActivateShouldPlay.exchange(false);
		if (pendingIndex >= 0) {
			activatePlaylistIndexImmediate(pendingIndex, shouldPlay);
		} else if (!playbackTransport.pendingDirectMediaSource.empty()) {
			const std::string source = playbackTransport.pendingDirectMediaSource;
			const std::vector<std::string> options = playbackTransport.pendingDirectMediaOptions;
			const std::string label = playbackTransport.pendingDirectMediaLabel;
			const bool isLocation = playbackTransport.pendingDirectMediaIsLocation;
			const bool parseAsNetwork = playbackTransport.pendingDirectMediaParseAsNetwork;
			clearPendingActivationRequest();
			activateDirectMediaImmediate(source, isLocation, options, shouldPlay, parseAsNetwork, label);
		}
	}

	if (playbackTransport.playNextRequested.exchange(false)) {
		handlePlaybackEnded();
	}
}

void PlaybackController::handleMediaPlayerEvent(const libvlc_event_t * event) {
	if (!event) {
		return;
	}

	switch (event->type) {
	case libvlc_MediaPlayerPlaying:
		onMediaPlayerPlaying();
		return;
	case libvlc_MediaPlayerOpening:
		return;
	case libvlc_MediaPlayerStopping:
		onMediaPlayerStopping();
		return;
	case libvlc_MediaPlayerMuted:
		audio().updateAudioStateFromMutedEvent(true);
		return;
	case libvlc_MediaPlayerUnmuted:
		audio().updateAudioStateFromMutedEvent(false);
		return;
	case libvlc_MediaPlayerAudioVolume: {
		libvlc_media_player_t * player = owner.sessionPlayer();
		int volume = player ? libvlc_audio_get_volume(player) : -1;
		if (volume < 0) {
			const float eventVolume = event->u.media_player_audio_volume.volume;
			volume = eventVolume <= 2.0f
				? static_cast<int>(std::round(eventVolume * 100.0f))
				: static_cast<int>(std::round(eventVolume));
		}
		audio().updateAudioStateFromVolumeEvent(ofClamp(volume, 0, 100));
		return;
	}
	case libvlc_MediaPlayerAudioDevice: {
		const char * device = event->u.media_player_audio_device.device;
		audio().updateAudioStateFromDeviceEvent(device ? device : "");
		return;
	}
	case libvlc_MediaPlayerSnapshotTaken: {
		const char * path = event->u.media_player_snapshot_taken.psz_filename;
		media().updateSnapshotStateFromEvent(path ? path : "");
		return;
	}
	case libvlc_MediaPlayerRecordChanged:
		media().updateNativeRecordingStateFromEvent(
			event->u.media_player_record_changed.recording,
			event->u.media_player_record_changed.recorded_file_path
				? event->u.media_player_record_changed.recorded_file_path
				: "");
		return;
	case libvlc_MediaPlayerESAdded:
	case libvlc_MediaPlayerESDeleted:
	case libvlc_MediaPlayerESUpdated:
		if (event->u.media_player_es_changed.i_type == libvlc_track_audio) {
			media().clearMetadataCache();
		}
		return;
	case libvlc_MediaPlayerESSelected:
		if (event->u.media_player_es_selection_changed.i_type == libvlc_track_audio) {
			media().clearMetadataCache();
		}
		return;
	case libvlc_MediaPlayerProgramAdded:
	case libvlc_MediaPlayerProgramDeleted:
	case libvlc_MediaPlayerProgramSelected:
	case libvlc_MediaPlayerProgramUpdated:
	case libvlc_MediaPlayerTitleListChanged:
	case libvlc_MediaPlayerTitleSelectionChanged:
	case libvlc_MediaPlayerChapterChanged:
		return;
	case libvlc_MediaPlayerStopped:
		onMediaPlayerStopped();
		return;
	case libvlc_MediaPlayerEncounteredError:
		onMediaPlayerEncounteredError();
		return;
	case libvlc_MediaPlayerPaused:
		onMediaPlayerPaused();
		return;
	case libvlc_MediaPlayerVout:
		onMediaPlayerVout(static_cast<unsigned>(event->u.media_player_vout.new_count));
		return;
	case libvlc_MediaPlayerBuffering:
		onMediaPlayerBuffering(event->u.media_player_buffering.new_cache);
		return;
	case libvlc_MediaPlayerSeekableChanged:
		onMediaPlayerSeekableChanged(event->u.media_player_seekable_changed.new_seekable != 0);
		return;
	case libvlc_MediaPlayerPausableChanged:
		onMediaPlayerPausableChanged(event->u.media_player_pausable_changed.new_pausable != 0);
		return;
	case libvlc_MediaPlayerCorked:
		onMediaPlayerCorked();
		return;
	case libvlc_MediaPlayerUncorked:
		onMediaPlayerUncorked();
		return;
	default:
		return;
	}
}

void PlaybackController::onMediaPlayerPlaying() {
	playbackTransport.audioPausedSignal.store(false);
	playbackTransport.pauseRequested.store(false);
	if (playbackTransport.playbackWanted.load()) {
		playbackTransport.manualStopInProgress.store(false);
		playbackTransport.manualStopRequestTimeMs.store(0);
		playbackTransport.manualStopRetryIssued.store(false);
	}
}

void PlaybackController::onMediaPlayerStopping() {
	playbackTransport.audioPausedSignal.store(false);
	if (!playbackTransport.playbackWanted.load()) {
		playbackTransport.manualStopInProgress.store(true);
	}
}

void PlaybackController::onMediaPlayerStopped() {
	playbackTransport.audioPausedSignal.store(false);
	clearAudioPtsState();
	owner.m_impl->nativeRecordingRuntime.active.store(false);
	playbackTransport.manualStopRequestTimeMs.store(0);
	playbackTransport.manualStopRetryIssued.store(false);
	playbackTransport.lastKnownPlaybackPosition.store(0.0f, std::memory_order_relaxed);
	playbackTransport.bufferCache.store(0.0f, std::memory_order_relaxed);
	playbackTransport.seekableLatched.store(false, std::memory_order_relaxed);
	playbackTransport.pausableLatched.store(false, std::memory_order_relaxed);
	playbackTransport.cachedVideoOutputCount.store(0, std::memory_order_relaxed);
	playbackTransport.corked.store(false, std::memory_order_relaxed);
	clearWatchTimeState();
	const int pendingManualStops = playbackTransport.pendingManualStopEvents.fetch_sub(1);
	if (pendingManualStops > 0) {
		playbackTransport.manualStopInProgress.store(true);
		if (playbackTransport.pendingActivateIndex.load() >= 0 || !playbackTransport.pendingDirectMediaSource.empty()) {
			playbackTransport.pendingActivateReady.store(true);
		} else if (!owner.sessionMedia()) {
			playbackTransport.pendingManualStopEvents.store(0);
			playbackTransport.pendingManualStopFinalize.store(true);
		} else {
			playbackTransport.pendingManualStopFinalize.store(true);
		}
	} else {
		playbackTransport.manualStopInProgress.store(false);
		playbackTransport.pendingManualStopEvents.store(0);
		libvlc_media_player_t * player = owner.sessionPlayer();
		const libvlc_state_t state = player ? libvlc_media_player_get_state(player) : libvlc_Stopped;
		const bool stillStopped = !player || isStoppedOrIdleState(state);
		if (stillStopped) {
			playbackTransport.playNextRequested.store(true);
		}
	}
}

void PlaybackController::onMediaPlayerPaused() {
	playbackTransport.audioPausedSignal.store(true);
}

void PlaybackController::onMediaPlayerVout(unsigned count) {
	playbackTransport.cachedVideoOutputCount.store(count, std::memory_order_relaxed);
}

void PlaybackController::onMediaPlayerBuffering(float cache) {
	playbackTransport.bufferCache.store(cache, std::memory_order_relaxed);
}

void PlaybackController::onMediaPlayerSeekableChanged(bool seekable) {
	playbackTransport.seekableLatched.store(seekable, std::memory_order_relaxed);
}

void PlaybackController::onMediaPlayerPausableChanged(bool pausable) {
	playbackTransport.pausableLatched.store(pausable, std::memory_order_relaxed);
}

void PlaybackController::onMediaPlayerEncounteredError() {
	owner.setError("VLC encountered a playback error.");
}

void PlaybackController::onMediaPlayerCorked() {
	playbackTransport.corked.store(true, std::memory_order_relaxed);
}

void PlaybackController::onMediaPlayerUncorked() {
	playbackTransport.corked.store(false, std::memory_order_relaxed);
}

float PlaybackController::getBufferCache() const {
	return playbackTransport.bufferCache.load(std::memory_order_relaxed);
}

bool PlaybackController::isCorked() const {
	return playbackTransport.corked.load(std::memory_order_relaxed);
}

unsigned PlaybackController::getCachedVideoOutputCount() const {
	return playbackTransport.cachedVideoOutputCount.load(std::memory_order_relaxed);
}

bool PlaybackController::isPausableLatched() const {
	return playbackTransport.pausableLatched.load(std::memory_order_relaxed);
}

void PlaybackController::prepareForClose() {
	const ofxVlc4::PlaybackStateInfo playbackState = media().getPlaybackStateInfo();
	const bool hasActivePlaybackState =
		playbackState.playing ||
		playbackState.video.hasVideoOutput ||
		playbackState.video.frameReceived ||
		playbackState.audio.ready;
	libvlc_media_player_t * player = owner.sessionPlayer();
	const bool shouldStopActivePlayback = (player != nullptr) && hasActivePlaybackState;
	if (shouldStopActivePlayback) {
		constexpr int kCloseStopPollMs = 4;
		constexpr int kCloseStopMaxWaitMs = 40;

		libvlc_media_player_stop_async(player);

		for (int waitedMs = 0; waitedMs < kCloseStopMaxWaitMs; waitedMs += kCloseStopPollMs) {
			player = owner.sessionPlayer();
			if (!player) {
				break;
			}

			const libvlc_state_t state = libvlc_media_player_get_state(player);
			if (isStoppedOrIdleState(state) || state == libvlc_Stopping) {
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(kCloseStopPollMs));
		}
	}

	clearPendingActivationRequest();
	playbackTransport.pauseRequested.store(false);
	playbackTransport.audioPausedSignal.store(false);
}

bool PlaybackController::isPlaybackLocallyStopped() const {
	return playbackTransport.stoppedStateLatched.load() || isManualStopPending();
}

bool PlaybackController::isPlaying() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!owner.sessionMedia() || isPlaybackLocallyStopped()) {
		return false;
	}
	return player && libvlc_media_player_is_playing(player);
}

bool PlaybackController::isManualStopPending() const {
	return (playbackTransport.manualStopInProgress.load() || playbackTransport.pendingManualStopFinalize.load()) &&
		!playbackTransport.playbackWanted.load() &&
		!playbackTransport.pauseRequested.load();
}

bool PlaybackController::isStopped() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || !owner.sessionMedia() || isPlaybackLocallyStopped()) {
		return true;
	}
	return isStoppedOrIdleState(libvlc_media_player_get_state(player));
}

bool PlaybackController::isPlaybackTransitioning() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || !owner.sessionMedia() || isPlaybackLocallyStopped()) {
		return false;
	}
	return isTransientPlaybackState(libvlc_media_player_get_state(player));
}

bool PlaybackController::isPlaybackRestartPending() const {
	if (!playbackTransport.playbackWanted.load()) {
		return false;
	}

	if (playbackTransport.pendingManualStopEvents.load() > 0 ||
		playbackTransport.pendingActivateIndex.load() >= 0 ||
		playbackTransport.pendingActivateReady.load() ||
		playbackTransport.playNextRequested.load()) {
		return true;
	}

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	return isStoppedOrIdleState(state) || isTransientPlaybackState(state);
}

bool PlaybackController::isSeekable() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || !owner.sessionMedia() || isPlaybackLocallyStopped()) {
		return false;
	}
	return libvlc_media_player_is_seekable(player);
}

bool PlaybackController::canPause() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || isPlaybackLocallyStopped()) {
		return false;
	}

	libvlc_media_t * sourceMedia = libvlc_media_player_get_media(player);
	if (!sourceMedia) {
		return false;
	}
	libvlc_media_release(sourceMedia);
	return libvlc_media_player_can_pause(player);
}

float PlaybackController::getPosition() const {
	if (!owner.sessionMedia() || isPlaybackLocallyStopped()) {
		return playbackTransport.lastKnownPlaybackPosition.load(std::memory_order_relaxed);
	}

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return playbackTransport.lastKnownPlaybackPosition.load(std::memory_order_relaxed);
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	if (isStoppedOrIdleState(state)) {
		return playbackTransport.lastKnownPlaybackPosition.load(std::memory_order_relaxed);
	}

	const float livePosition = libvlc_media_player_get_position(player);
	if (std::isfinite(livePosition) && livePosition >= 0.0f && livePosition <= 1.0f) {
		playbackTransport.lastKnownPlaybackPosition.store(livePosition, std::memory_order_relaxed);
		return livePosition;
	}

	const ofxVlc4::WatchTimeInfo watchTime = media().getWatchTimeInfo();
	if (watchTime.available &&
		std::isfinite(watchTime.interpolatedPosition) &&
		watchTime.interpolatedPosition >= 0.0 &&
		watchTime.interpolatedPosition <= 1.0) {
		const float watchPosition = static_cast<float>(watchTime.interpolatedPosition);
		playbackTransport.lastKnownPlaybackPosition.store(watchPosition, std::memory_order_relaxed);
		return watchPosition;
	}

	return playbackTransport.lastKnownPlaybackPosition.load(std::memory_order_relaxed);
}

int PlaybackController::getTime() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!owner.sessionMedia() || !player || isPlaybackLocallyStopped()) {
		return 0;
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	if (isStoppedOrIdleState(state)) {
		return 0;
	}

	return static_cast<int>(libvlc_media_player_get_time(player));
}

void PlaybackController::setTime(int ms) {
	const int clampedMs = std::max(0, ms);

	const float lengthMs = getLength();
	if (lengthMs > 0.0f) {
		playbackTransport.lastKnownPlaybackPosition.store(
			ofClamp(static_cast<float>(clampedMs) / lengthMs, 0.0f, 1.0f),
			std::memory_order_relaxed);
	}

	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		resetAudioBuffer();
		libvlc_media_player_set_time(player, clampedMs, true);
	}
}

void PlaybackController::jumpTime(int deltaMs) {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		resetAudioBuffer();
		libvlc_media_player_jump_time(player, static_cast<libvlc_time_t>(deltaMs));
	}
}

float PlaybackController::getLength() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	return (player && owner.sessionMedia()) ? static_cast<float>(libvlc_media_player_get_length(player)) : 0.f;
}

void PlaybackController::setPlaybackMode(ofxVlc4::PlaybackMode mode) {
	setPlaybackModeValue(mode);
	owner.setStatus("Playback mode set to " + ofxVlc4::playbackModeToString(mode) + ".");
	owner.logNotice("Playback mode: " + ofxVlc4::playbackModeToString(mode));
}

void PlaybackController::setPlaybackMode(const std::string & mode) {
	setPlaybackMode(ofxVlc4::playbackModeFromString(mode));
}

ofxVlc4::PlaybackMode PlaybackController::getPlaybackMode() const {
	return getPlaybackModeValue();
}

std::string PlaybackController::getPlaybackModeString() const {
	return ofxVlc4::playbackModeToString(getPlaybackMode());
}

void PlaybackController::setShuffleEnabled(bool enabled) {
	setShuffleEnabledValue(enabled);
	if (!enabled) {
		invalidateShuffleQueue();
	}
	owner.setStatus(std::string("Shuffle ") + (enabled ? "enabled." : "disabled."));
	owner.logNotice(std::string("Shuffle ") + (enabled ? "enabled." : "disabled."));
}

bool PlaybackController::isShuffleEnabled() const {
	return isShuffleEnabledValue();
}

bool PlaybackController::isPlaybackWanted() const {
	return playbackTransport.playbackWanted.load();
}

bool PlaybackController::isPauseRequested() const {
	return playbackTransport.pauseRequested.load();
}

bool PlaybackController::hasPendingManualStopEvents() const {
	return playbackTransport.pendingManualStopEvents.load() > 0;
}

bool PlaybackController::isAudioPauseSignaled() const {
	return playbackTransport.audioPausedSignal.load();
}

void PlaybackController::setAudioPauseSignaled(bool paused) {
	playbackTransport.audioPausedSignal.store(paused);
}

int PlaybackController::getNextShuffleIndex() const {
	int next = -1;
	const PlaylistSnapshot playlist = getPlaylistSnapshot();
	const int current = playlist.currentIndex;
	const size_t playlistSize = playlist.size;
	if (playlistSize == 0) return -1;
	if (playlistSize == 1) return 0;

	next = current;
	while (next == current) {
		next = static_cast<int>(ofRandom(static_cast<float>(playlistSize)));
	}
	return next;
}

void PlaybackController::nextMediaListItem() {
	const PlaylistSnapshot playlist = getPlaylistSnapshot();
	if (playlist.size == 0) {
		return;
	}
	const bool shouldPlay = playbackTransport.playbackWanted.load();

	if (isShuffleEnabled()) {
		int next = nextFromShuffleQueueLazy(playlist.currentIndex);
		if (next >= 0) {
			activatePlaylistIndex(next, shouldPlay);
		}
		owner.logNotice("Next playlist item selected.");
		return;
	}

	const int nextIndex = (playlist.currentIndex + 1 < static_cast<int>(playlist.size)) ? (playlist.currentIndex + 1) : 0;
	activatePlaylistIndex(nextIndex, shouldPlay);
	owner.logNotice("Next playlist item selected.");
}

void PlaybackController::previousMediaListItem() {
	const PlaylistSnapshot playlist = getPlaylistSnapshot();
	if (playlist.size == 0) {
		return;
	}
	const bool shouldPlay = playbackTransport.playbackWanted.load();

	if (isShuffleEnabled()) {
		int previous = nextFromShuffleQueueLazy(playlist.currentIndex);
		if (previous >= 0) {
			activatePlaylistIndex(previous, shouldPlay);
		}
		owner.logNotice("Previous playlist item selected.");
		return;
	}

	const int previousIndex = (playlist.currentIndex > 0) ? (playlist.currentIndex - 1) : (static_cast<int>(playlist.size) - 1);
	activatePlaylistIndex(previousIndex, shouldPlay);
	owner.logNotice("Previous playlist item selected.");
}

bool PlaybackController::hasActiveDirectMedia() const {
	return !trimWhitespace(playbackTransport.activeDirectMediaSource).empty();
}

bool PlaybackController::reloadActiveDirectMedia() {
	if (!hasActiveDirectMedia()) {
		return false;
	}

	return media().loadMediaSource(
		playbackTransport.activeDirectMediaSource,
		playbackTransport.activeDirectMediaIsLocation,
		playbackTransport.activeDirectMediaOptions,
		playbackTransport.activeDirectMediaParseAsNetwork);
}
