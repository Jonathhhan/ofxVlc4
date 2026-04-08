#pragma once

#include "core/ofxVlc4.h"
#include "playback/PlaybackTransportState.h"

#include <string>
#include <vector>

class ofxVlc4;

class PlaybackController {
public:
	explicit PlaybackController(ofxVlc4 & owner);

	void playIndex(int index);
	void activatePlaylistIndex(int index, bool shouldPlay);
	void activatePlaylistIndexImmediate(int index, bool shouldPlay);
	bool activateDirectMediaImmediate(
		const std::string & source,
		bool isLocation,
		const std::vector<std::string> & options,
		bool shouldPlay,
		bool parseAsNetwork,
		const std::string & label);
	bool requestDirectMediaActivation(
		const std::string & source,
		bool isLocation,
		const std::vector<std::string> & options,
		bool shouldPlay,
		bool parseAsNetwork,
		const std::string & label);
	bool openDshowCapture(
		const std::string & videoDevice,
		const std::string & audioDevice,
		int width,
		int height,
		float fps);
	bool openScreenCapture(int width, int height, float fps, int left, int top);
	void setPosition(float pct);
	void play();
	void pause();
	void stop();
	void resetTransportState();
	void clearPendingActivationRequest();
	void handlePlaybackEnded();
	void processDeferredPlaybackActions();
	void handleMediaPlayerEvent(const libvlc_event_t * event);
	void onMediaPlayerPlaying();
	void onMediaPlayerStopping();
	void onMediaPlayerStopped();
	void prepareForClose();
	bool isPlaybackLocallyStopped() const;
	bool isPlaying() const;
	bool isManualStopPending() const;
	bool isStopped() const;
	bool isPlaybackTransitioning() const;
	bool isPlaybackRestartPending() const;
	bool isSeekable() const;
	bool canPause() const;
	float getPosition() const;
	int getTime() const;
	void setTime(int ms);
	float getLength() const;
	void setPlaybackMode(ofxVlc4::PlaybackMode mode);
	void setPlaybackMode(const std::string & mode);
	ofxVlc4::PlaybackMode getPlaybackMode() const;
	std::string getPlaybackModeString() const;
	void setShuffleEnabled(bool enabled);
	bool isShuffleEnabled() const;
	void invalidateShuffleQueue();
	bool isPlaybackWanted() const;
	bool isPauseRequested() const;
	bool hasPendingManualStopEvents() const;
	bool isAudioPauseSignaled() const;
	void setAudioPauseSignaled(bool paused);
	int getNextShuffleIndex() const;
	void nextMediaListItem();
	void previousMediaListItem();
	bool hasActiveDirectMedia() const;
	bool reloadActiveDirectMedia();
	float getBufferCache() const;
	bool isCorked() const;
	unsigned getCachedVideoOutputCount() const;
	bool isPausableLatched() const;

private:
	struct PlaylistSnapshot {
		size_t size = 0;
		int currentIndex = -1;

		bool contains(int index) const {
			return index >= 0 && index < static_cast<int>(size);
		}
	};

	PlaylistSnapshot getPlaylistSnapshot() const;
	void setCurrentPlaylistIndex(int index);
	void clearCurrentPlaylistIndex();
	ofxVlc4::AudioComponent & audio() const;
	ofxVlc4::VideoComponent & video() const;
	ofxVlc4::MediaComponent & media() const;
	void resetAudioBuffer();
	void applyPendingEqualizerOnPlay();
	void applyPendingVideoAdjustmentsOnPlay();
	void clearPendingEqualizerApplyOnPlay();
	void clearCurrentMedia(bool clearVideoResources = true);
	void clearWatchTimeState();
	void clearAudioPtsState();
	void setPlaybackModeValue(ofxVlc4::PlaybackMode mode);
	ofxVlc4::PlaybackMode getPlaybackModeValue() const;
	void setShuffleEnabledValue(bool enabled);
	bool isShuffleEnabledValue() const;
	void onMediaPlayerPaused();
	void onMediaPlayerVout(unsigned count);
	void onMediaPlayerBuffering(float cache);
	void onMediaPlayerSeekableChanged(bool seekable);
	void onMediaPlayerPausableChanged(bool pausable);
	void onMediaPlayerEncounteredError();
	void onMediaPlayerCorked();
	void onMediaPlayerUncorked();
	void buildShuffleQueue(int excludeIndex);
	int popNextFromShuffleQueue();
	int nextFromShuffleQueueLazy(int excludeIndex);

	ofxVlc4 & owner;
	PlaybackTransportState playbackTransport;
	std::vector<int> shuffleQueue;
	bool shuffleQueueBuilt = false;
};
