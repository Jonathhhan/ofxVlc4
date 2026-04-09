#include "ofxVlc4.h"
#include "playback/PlaybackController.h"
#include "support/ofxVlc4Utils.h"

using ofxVlc4Utils::trimWhitespace;




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




















void ofxVlc4::vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data) {
	((ofxVlc4 *)data)->vlcMediaPlayerEvent(event);
}

void ofxVlc4::vlcMediaPlayerEvent(const libvlc_event_t * event) {
	playbackController->handleMediaPlayerEvent(event);
}


