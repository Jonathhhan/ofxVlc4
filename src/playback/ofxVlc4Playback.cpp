#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "playback/PlaybackController.h"
#include "support/ofxVlc4Utils.h"

using ofxVlc4Utils::trimWhitespace;

void ofxVlc4::playIndex(int index) {
	m_impl->subsystemRuntime.playbackController->playIndex(index);
}

void ofxVlc4::activatePlaylistIndex(int index, bool shouldPlay) {
	m_impl->subsystemRuntime.playbackController->activatePlaylistIndex(index, shouldPlay);
}

void ofxVlc4::activatePlaylistIndexImmediate(int index, bool shouldPlay) {
	m_impl->subsystemRuntime.playbackController->activatePlaylistIndexImmediate(index, shouldPlay);
}

bool ofxVlc4::activateDirectMediaImmediate(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool shouldPlay,
	bool parseAsNetwork,
	const std::string & label) {
	return m_impl->subsystemRuntime.playbackController->activateDirectMediaImmediate(
		source, isLocation, options, shouldPlay, parseAsNetwork, label);
}

bool ofxVlc4::requestDirectMediaActivation(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool shouldPlay,
	bool parseAsNetwork,
	const std::string & label) {
	return m_impl->subsystemRuntime.playbackController->requestDirectMediaActivation(
		source, isLocation, options, shouldPlay, parseAsNetwork, label);
}

bool ofxVlc4::openDshowCapture(
	const std::string & videoDevice,
	const std::string & audioDevice,
	int width,
	int height,
	float fps) {
	return m_impl->subsystemRuntime.playbackController->openDshowCapture(videoDevice, audioDevice, width, height, fps);
}

bool ofxVlc4::openScreenCapture(int width, int height, float fps, int left, int top) {
	return m_impl->subsystemRuntime.playbackController->openScreenCapture(width, height, fps, left, top);
}

void ofxVlc4::play() {
	m_impl->subsystemRuntime.playbackController->play();
}

void ofxVlc4::pause() {
	m_impl->subsystemRuntime.playbackController->pause();
}

void ofxVlc4::stop() {
	m_impl->subsystemRuntime.playbackController->stop();
}

int ofxVlc4::getNextShuffleIndex() const {
	return m_impl->subsystemRuntime.playbackController->getNextShuffleIndex();
}

void ofxVlc4::nextMediaListItem() {
	m_impl->subsystemRuntime.playbackController->nextMediaListItem();
}

void ofxVlc4::previousMediaListItem() {
	m_impl->subsystemRuntime.playbackController->previousMediaListItem();
}

void ofxVlc4::handlePlaybackEnded() {
	m_impl->subsystemRuntime.playbackController->handlePlaybackEnded();
}

void ofxVlc4::processDeferredPlaybackActions() {
	m_impl->subsystemRuntime.playbackController->processDeferredPlaybackActions();
}

void ofxVlc4::clearPendingActivationRequest() {
	m_impl->subsystemRuntime.playbackController->clearPendingActivationRequest();
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
	m_impl->subsystemRuntime.playbackController->setPlaybackMode(mode);
}

void ofxVlc4::setPlaybackMode(const std::string & mode) {
	m_impl->subsystemRuntime.playbackController->setPlaybackMode(mode);
}

std::string ofxVlc4::getPlaybackModeString() const {
	return m_impl->subsystemRuntime.playbackController->getPlaybackModeString();
}

ofxVlc4::PlaybackMode ofxVlc4::getPlaybackMode() const {
	return m_impl->subsystemRuntime.playbackController->getPlaybackMode();
}

void ofxVlc4::setShuffleEnabled(bool enabled) {
	m_impl->subsystemRuntime.playbackController->setShuffleEnabled(enabled);
}

bool ofxVlc4::isShuffleEnabled() const {
	return m_impl->subsystemRuntime.playbackController->isShuffleEnabled();
}

void ofxVlc4::setPosition(float pct) {
	m_impl->subsystemRuntime.playbackController->setPosition(pct);
}

bool ofxVlc4::isPlaying() {
	return m_impl->subsystemRuntime.playbackController->isPlaying();
}

bool ofxVlc4::isStopped() const {
	return m_impl->subsystemRuntime.playbackController->isStopped();
}

bool ofxVlc4::isPlaybackTransitioning() const {
	return m_impl->subsystemRuntime.playbackController->isPlaybackTransitioning();
}

bool ofxVlc4::isManualStopPending() const {
	return m_impl->subsystemRuntime.playbackController->isManualStopPending();
}

bool ofxVlc4::isPlaybackRestartPending() const {
	return m_impl->subsystemRuntime.playbackController->isPlaybackRestartPending();
}

bool ofxVlc4::isSeekable() const {
	return m_impl->subsystemRuntime.playbackController->isSeekable();
}

float ofxVlc4::getBufferCache() const {
	return m_impl->subsystemRuntime.playbackController->getBufferCache();
}

bool ofxVlc4::isCorked() const {
	return m_impl->subsystemRuntime.playbackController->isCorked();
}

float ofxVlc4::getPosition() const {
	return m_impl->subsystemRuntime.playbackController->getPosition();
}

int ofxVlc4::getTime() const {
	return m_impl->subsystemRuntime.playbackController->getTime();
}

void ofxVlc4::setTime(int ms) {
	m_impl->subsystemRuntime.playbackController->setTime(ms);
}

void ofxVlc4::jumpTime(int ms) {
	m_impl->subsystemRuntime.playbackController->jumpTime(ms);
}

float ofxVlc4::getLength() const {
	return m_impl->subsystemRuntime.playbackController->getLength();
}

void ofxVlc4::vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) {
		return;
	}
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope || !event) {
		return;
	}
	scope.get()->vlcMediaPlayerEvent(event);
}

void ofxVlc4::vlcMediaPlayerEvent(const libvlc_event_t * event) {
	m_impl->subsystemRuntime.playbackController->handleMediaPlayerEvent(event);
}

