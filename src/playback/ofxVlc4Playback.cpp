#include "ofxVlc4.h"
#include "playback/PlaybackController.h"
#include "support/ofxVlc4Utils.h"

using ofxVlc4Utils::trimWhitespace;

void ofxVlc4::playIndex(int index) {
	playbackController->playIndex(index);
}

void ofxVlc4::activatePlaylistIndex(int index, bool shouldPlay) {
	playbackController->activatePlaylistIndex(index, shouldPlay);
}

void ofxVlc4::activatePlaylistIndexImmediate(int index, bool shouldPlay) {
	playbackController->activatePlaylistIndexImmediate(index, shouldPlay);
}

bool ofxVlc4::activateDirectMediaImmediate(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool shouldPlay,
	bool parseAsNetwork,
	const std::string & label) {
	return playbackController->activateDirectMediaImmediate(
		source, isLocation, options, shouldPlay, parseAsNetwork, label);
}

bool ofxVlc4::requestDirectMediaActivation(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool shouldPlay,
	bool parseAsNetwork,
	const std::string & label) {
	return playbackController->requestDirectMediaActivation(
		source, isLocation, options, shouldPlay, parseAsNetwork, label);
}

bool ofxVlc4::openDshowCapture(
	const std::string & videoDevice,
	const std::string & audioDevice,
	int width,
	int height,
	float fps) {
	return playbackController->openDshowCapture(videoDevice, audioDevice, width, height, fps);
}

bool ofxVlc4::openScreenCapture(int width, int height, float fps, int left, int top) {
	return playbackController->openScreenCapture(width, height, fps, left, top);
}

void ofxVlc4::play() {
	playbackController->play();
}

void ofxVlc4::pause() {
	playbackController->pause();
}

void ofxVlc4::stop() {
	playbackController->stop();
}

int ofxVlc4::getNextShuffleIndex() const {
	return playbackController->getNextShuffleIndex();
}

void ofxVlc4::nextMediaListItem() {
	playbackController->nextMediaListItem();
}

void ofxVlc4::previousMediaListItem() {
	playbackController->previousMediaListItem();
}

void ofxVlc4::handlePlaybackEnded() {
	playbackController->handlePlaybackEnded();
}

void ofxVlc4::processDeferredPlaybackActions() {
	playbackController->processDeferredPlaybackActions();
}

void ofxVlc4::clearPendingActivationRequest() {
	playbackController->clearPendingActivationRequest();
}

ofxVlc4::PlaybackMode ofxVlc4::playbackModeFromString(const std::string & mode) {
	const std::string normalized = ofToLower(trimWhitespace(mode));
	if (normalized == "repeat") {
		return PlaybackMode::Repeat;
	}
	if (normalized == "loop") {
		return PlaybackMode::Loop;
	}
	return PlaybackMode::Default;
}

std::string ofxVlc4::playbackModeToString(PlaybackMode mode) {
	switch (mode) {
	case PlaybackMode::Repeat:
		return "repeat";
	case PlaybackMode::Loop:
		return "loop";
	case PlaybackMode::Default:
	default:
		return "default";
	}
}

void ofxVlc4::setPlaybackMode(PlaybackMode mode) {
	playbackController->setPlaybackMode(mode);
}

void ofxVlc4::setPlaybackMode(const std::string & mode) {
	playbackController->setPlaybackMode(mode);
}

std::string ofxVlc4::getPlaybackModeString() const {
	return playbackController->getPlaybackModeString();
}

ofxVlc4::PlaybackMode ofxVlc4::getPlaybackMode() const {
	return playbackController->getPlaybackMode();
}

void ofxVlc4::setShuffleEnabled(bool enabled) {
	playbackController->setShuffleEnabled(enabled);
}

bool ofxVlc4::isShuffleEnabled() const {
	return playbackController->isShuffleEnabled();
}

void ofxVlc4::setPosition(float pct) {
	playbackController->setPosition(pct);
}

bool ofxVlc4::isPlaying() {
	return playbackController->isPlaying();
}

bool ofxVlc4::isStopped() const {
	return playbackController->isStopped();
}

bool ofxVlc4::isPlaybackTransitioning() const {
	return playbackController->isPlaybackTransitioning();
}

bool ofxVlc4::isManualStopPending() const {
	return playbackController->isManualStopPending();
}

bool ofxVlc4::isPlaybackRestartPending() const {
	return playbackController->isPlaybackRestartPending();
}

bool ofxVlc4::isSeekable() const {
	return playbackController->isSeekable();
}

float ofxVlc4::getPosition() const {
	return playbackController->getPosition();
}

int ofxVlc4::getTime() const {
	return playbackController->getTime();
}

void ofxVlc4::setTime(int ms) {
	playbackController->setTime(ms);
}

float ofxVlc4::getLength() const {
	return playbackController->getLength();
}

void ofxVlc4::vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data) {
	((ofxVlc4 *)data)->vlcMediaPlayerEvent(event);
}

void ofxVlc4::vlcMediaPlayerEvent(const libvlc_event_t * event) {
	playbackController->handleMediaPlayerEvent(event);
}


