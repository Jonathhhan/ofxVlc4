#include "ofApp.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>

namespace {
constexpr float kOverlayPadding = 12.0f;
constexpr float kLineHeight = 18.0f;
constexpr float kDefaultAspect = 16.0f / 9.0f;
constexpr int kVideoModeFrameRate = 60;
constexpr int kAudioOnlyModeFrameRate = 30;

const std::set<std::string> & audioOnlyExtensions() {
	static const std::set<std::string> extensions = {
		".wav", ".mp3", ".flac", ".ogg", ".mid", ".midi",
		".m4a", ".aac", ".aiff", ".wma"
	};
	return extensions;
}

bool isMidiPath(const std::string & path) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(path));
	return extension == "mid" || extension == "midi";
}

bool hasActiveVlcMedia(const ofxVlc4 & player) {
	return player.hasPlaylist() || !player.getCurrentPath().empty();
}

std::string formatSeconds(double seconds) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(2) << seconds << " s";
	return stream.str();
}

std::string trimWhitespace(const std::string & value) {
	const auto begin = value.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos) {
		return "";
	}
	const auto end = value.find_last_not_of(" \t\r\n");
	return value.substr(begin, end - begin + 1);
}
}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetWindowTitle("ofxVlc4 Simple Example");
	ofSetFrameRate(kVideoModeFrameRate);
	ofSetBackgroundColor(ofColor(12, 12, 12));
	ofSetColor(255);

	const std::string logDir = ofToDataPath("logs", true);
	std::error_code directoryError;
	std::filesystem::create_directories(logDir, directoryError);

	player = std::make_unique<ofxVlc4>();

	// Keep the minimal example on VLC's normal audio output path.
	player->setAudioCaptureEnabled(false);
	player->init(0, nullptr);
	player->setWatchTimeEnabled(true);
	player->setWatchTimeMinPeriodUs(50000);
	player->setVolume(70);
	refreshMidiOutputs();
	midiAnalysisStatus = "Drop media or press Space after loading a file.";
}

//--------------------------------------------------------------
void ofApp::update() {
	if (!player || shuttingDown) {
		return;
	}

	if (!isMidiModeActive()) {
		player->update();
	}
	midiPlayback.update(ofGetElapsedTimef());
	dispatchMidiMessages(
		midiPlayback.getMessages(),
		midiPlayback.getLastDispatchBegin(),
		midiPlayback.getLastDispatchEnd());
	if (midiWasPlaying && isMidiModeActive() && !midiPlayback.isPlaying()) {
		sendMidiPanic();
	}
	midiWasPlaying = isMidiModeActive() && midiPlayback.isPlaying();
	updateAudioOnlyMode();
	updateMidiAnalysis();
}

//--------------------------------------------------------------
void ofApp::draw() {
	ofBackground(12, 12, 12);

	if (!player) {
		ofSetColor(150);
		ofDrawBitmapString("Player closed.", previewMargin, previewMargin + 24.0f);
		return;
	}

	const bool midiMode = isMidiModeActive();
	const bool vlcHasMedia = !midiMode && hasActiveVlcMedia(*player);
	const ofxVlc4::MediaReadinessInfo readiness = midiMode
		? ofxVlc4::MediaReadinessInfo{}
		: (vlcHasMedia ? player->getMediaReadinessInfo() : ofxVlc4::MediaReadinessInfo{});
	const ofxVlc4::VideoStateInfo videoState = midiMode
		? ofxVlc4::VideoStateInfo{}
		: (vlcHasMedia ? player->getVideoStateInfo() : ofxVlc4::VideoStateInfo{});

	const float availableWidth = std::max(1.0f, ofGetWidth() - previewMargin * 2.0f);
	const float availableHeight = std::max(1.0f, ofGetHeight() - 170.0f);
	float sourceWidth = static_cast<float>(videoState.sourceWidth);
	float sourceHeight = static_cast<float>(videoState.sourceHeight);
	if (sourceWidth <= 1.0f || sourceHeight <= 1.0f) {
		sourceWidth = kDefaultAspect;
		sourceHeight = 1.0f;
	}

	const float sourceAspect = sourceWidth / sourceHeight;
	float drawWidth = availableWidth;
	float drawHeight = drawWidth / sourceAspect;
	if (drawHeight > availableHeight) {
		drawHeight = availableHeight;
		drawWidth = drawHeight * sourceAspect;
	}

	const float drawX = (ofGetWidth() - drawWidth) * 0.5f;
	const float drawY = previewMargin;

	ofPushStyle();
	ofSetColor(26, 26, 26);
	ofDrawRectangle(drawX - 1.0f, drawY - 1.0f, drawWidth + 2.0f, drawHeight + 2.0f);
	ofSetColor(0, 0, 0);
	ofDrawRectangle(drawX, drawY, drawWidth, drawHeight);
	if (!midiMode && readiness.hasReceivedVideoFrame) {
		ofSetColor(255);
		player->draw(drawX, drawY, drawWidth, drawHeight);
	} else {
		ofSetColor(150);
		const std::string placeholder = midiMode
			? "Local MIDI mode active. Playback is sent to the selected MIDI output port."
			: (!vlcHasMedia
				? "Drop a media file to begin."
			: (readiness.mediaAttached
			? (audioOnlyModeActive
				? "Audio-only media active. Video output is intentionally idle."
				: "No video frame yet. Audio-only or startup state.")
			: "Drop a media file to begin."));
		ofDrawBitmapString(placeholder, drawX + 14.0f, drawY + 24.0f);
	}
	ofPopStyle();

	const float boxX = previewMargin;
	const float boxY = drawY + drawHeight + 16.0f;
	const float boxW = static_cast<float>(ofGetWidth()) - previewMargin * 2.0f;
	const bool showMidiDetails = midiMode;
	const float boxH = showMidiDetails ? 224.0f : 130.0f;

	ofPushStyle();
	ofSetColor(18, 18, 18, 235);
	ofDrawRectangle(boxX, boxY, boxW, boxH);
	ofSetColor(220);
	ofDrawBitmapString("Media: " + currentMediaLabel(), boxX + kOverlayPadding, boxY + kOverlayPadding + kLineHeight * 0.0f);
	ofDrawBitmapString("State: " + playbackLabel(), boxX + kOverlayPadding, boxY + kOverlayPadding + kLineHeight * 1.0f);
	ofDrawBitmapString(
		midiMode
			? ("Readiness: local-midi=yes parsed=" + std::string(midiReport.valid ? "yes" : "no") +
				" messages=" + ofToString(midiChannelMessages.size()) +
				" dispatched=" + ofToString(midiPlayback.getDispatchedCount()) +
				" active=" + std::string(midiPlayback.isPlaying() ? "yes" : "no"))
			: ("Readiness: attached=" + std::string(readiness.mediaAttached ? "yes" : "no") +
				" prepared=" + std::string(readiness.startupPrepared ? "yes" : "no") +
				" geometry=" + std::string(readiness.geometryKnown ? "yes" : "no") +
				" frame=" + std::string(readiness.hasReceivedVideoFrame ? "yes" : "no") +
				" playing=" + std::string(readiness.playbackActive ? "yes" : "no")),
		boxX + kOverlayPadding,
		boxY + kOverlayPadding + kLineHeight * 2.0f);
	ofDrawBitmapString(
		midiMode
			? ("MIDI Transport: position " + formatSeconds(midiPlayback.getPositionSeconds()) +
				" / " + formatSeconds(midiPlayback.getDurationSeconds()))
			: ("Playlist: " + ofToString(player->getPlaylist().size()) +
				" items, current index " + ofToString(player->getCurrentIndex())),
		boxX + kOverlayPadding,
		boxY + kOverlayPadding + kLineHeight * 3.0f);
	ofDrawBitmapString(
		"Mode: " + std::string(audioOnlyModeActive ? "audio-only optimized" : "full video/default"),
		boxX + kOverlayPadding,
		boxY + kOverlayPadding + kLineHeight * 4.0f);
	ofDrawBitmapString(
		"MIDI: local timeline mode | VLC reserved for regular audio/video only",
		boxX + kOverlayPadding,
		boxY + kOverlayPadding + kLineHeight * 5.0f);
	ofDrawBitmapString(
		"Controls: Space play/pause, S stop, [/ ] port, R refresh, -/+ tempo, ,/. ch, K mute, L solo, 0 clear, I export",
		boxX + kOverlayPadding,
		boxY + kOverlayPadding + kLineHeight * 6.0f);

	if (showMidiDetails) {
		std::string midiLine1 = "MIDI: waiting for analysis...";
		std::string midiLine2 = "Instruments: waiting for analysis...";
		std::string midiLine3 = "MIDI export: not written";
		std::string midiLine4 = "OF MIDI bridge: not ready";
		if (midiReport.valid) {
			midiLine1 =
				"MIDI Info: fmt=" + ofToString(midiReport.format) +
				" tracks=" + ofToString(midiReport.trackCountParsed) +
				" ppq=" + ofToString(midiReport.ticksPerQuarterNote) +
				" duration=" + formatSeconds(midiReport.durationSeconds) +
				" tempos=" + ofToString(midiReport.tempoChangeCount) +
				" markers=" + ofToString(midiReport.markerCount);

			std::ostringstream instrumentLine;
			instrumentLine << "Instruments: ";
			if (midiReport.instruments.empty()) {
				instrumentLine << "none";
			} else {
				for (size_t i = 0; i < std::min<size_t>(midiReport.instruments.size(), 3); ++i) {
					if (i > 0) {
						instrumentLine << " | ";
					}
					const MidiInstrumentUse & instrument = midiReport.instruments[i];
					instrumentLine << "ch " << (instrument.channel + 1)
						<< " prog " << instrument.program;
					if (!instrument.programName.empty()) {
						instrumentLine << " " << instrument.programName;
					}
				}
				if (midiReport.instruments.size() > 3) {
					instrumentLine << " | +" << (midiReport.instruments.size() - 3) << " more";
				}
			}
			midiLine2 = instrumentLine.str();
			if (!midiExportPath.empty()) {
				midiLine3 = "MIDI export: " + ofFilePath::getFileName(midiExportPath);
			}
			std::ostringstream midiRoute;
			midiRoute
				<< "MIDI out: " << midiOutputStatus
				<< " | tempo x" << std::fixed << std::setprecision(2) << midiPlayback.getTempoMultiplier()
				<< " | ch " << (selectedMidiChannel + 1);
			if (soloMidiChannel >= 0) {
				midiRoute << " | solo " << (soloMidiChannel + 1);
			}
			if (!mutedMidiChannels.empty()) {
				midiRoute << " | muted " << mutedMidiChannels.size();
			}
			midiRoute << " | dispatched " << midiPlayback.getDispatchedCount()
				<< "/" << midiChannelMessages.size();
			midiLine4 = midiRoute.str();
		} else if (!midiAnalysisStatus.empty()) {
			midiLine1 = midiAnalysisStatus;
		}

		ofDrawBitmapString(midiLine1, boxX + kOverlayPadding, boxY + kOverlayPadding + kLineHeight * 7.0f);
		ofDrawBitmapString(midiLine2, boxX + kOverlayPadding, boxY + kOverlayPadding + kLineHeight * 8.0f);
		ofDrawBitmapString(midiLine3, boxX + kOverlayPadding, boxY + kOverlayPadding + kLineHeight * 9.0f);
		ofDrawBitmapString(midiLine4, boxX + kOverlayPadding, boxY + kOverlayPadding + kLineHeight * 10.0f);
	}

	std::string status;
	if (!midiMode) {
		status = player->getLastErrorMessage().empty()
			? player->getLastStatusMessage()
			: ("Error: " + player->getLastErrorMessage());
	}
	if (status.empty()) {
		status = midiAnalysisStatus;
	}
	if (!status.empty()) {
		ofDrawBitmapString(
			status,
			boxX + kOverlayPadding,
			boxY + kOverlayPadding + kLineHeight * (showMidiDetails ? 11.0f : 7.0f));
	}
	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::exit() {
	shutdownPlayer();
}

//--------------------------------------------------------------
void ofApp::shutdownPlayer() {
	if (shuttingDown) {
		return;
	}

	shuttingDown = true;
	sendMidiPanic();
	closeMidiOutput();
	if (player) {
		player.reset();
	}
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	if (!player || shuttingDown) {
		return;
	}

	const bool midiMode = isMidiModeActive();
	switch (key) {
	case ' ':
		if (midiMode) {
			if (midiPlayback.isPlaying()) {
				midiPlayback.pause(ofGetElapsedTimef());
				sendMidiPanic();
			} else {
				midiPlayback.play(ofGetElapsedTimef());
			}
		} else if (player->isPlaying()) {
			player->pause();
		} else {
			player->play();
		}
		break;
	case 's':
	case 'S':
		if (midiMode) {
			midiPlayback.stop();
			sendMidiPanic();
		} else {
			player->stop();
		}
		break;
	case 'n':
	case 'N':
		if (!midiMode) {
			player->nextMediaListItem();
		}
		break;
	case 'p':
	case 'P':
		if (!midiMode) {
			player->previousMediaListItem();
		}
		break;
	case 'm':
	case 'M':
		if (!midiMode) {
			player->toggleMute();
		}
		break;
	case 'i':
	case 'I':
		if (midiMode) {
			exportMidiAnalysis(midiPlayback.getPath());
		}
		break;
	case '[':
		if (!midiOutputPorts.empty()) {
			int nextIndex = midiOutputPortIndex <= 0 ? static_cast<int>(midiOutputPorts.size()) - 1 : midiOutputPortIndex - 1;
			openMidiOutputPort(nextIndex);
		}
		break;
	case ']':
		if (!midiOutputPorts.empty()) {
			int nextIndex = midiOutputPortIndex < 0 ? 0 : (midiOutputPortIndex + 1) % static_cast<int>(midiOutputPorts.size());
			openMidiOutputPort(nextIndex);
		}
		break;
	case 'r':
	case 'R':
		refreshMidiOutputs();
		break;
	case '-':
	case '_':
		if (midiMode) {
			midiPlayback.setTempoMultiplier(midiPlayback.getTempoMultiplier() - 0.1, ofGetElapsedTimef());
			midiAnalysisStatus = "MIDI tempo x" + ofToString(midiPlayback.getTempoMultiplier(), 2);
		}
		break;
	case '+':
	case '=':
		if (midiMode) {
			midiPlayback.setTempoMultiplier(midiPlayback.getTempoMultiplier() + 0.1, ofGetElapsedTimef());
			midiAnalysisStatus = "MIDI tempo x" + ofToString(midiPlayback.getTempoMultiplier(), 2);
		}
		break;
	case ',':
	case '<':
		if (midiMode) {
			selectedMidiChannel = selectedMidiChannel <= 0 ? 15 : selectedMidiChannel - 1;
			midiAnalysisStatus = "Selected MIDI channel " + ofToString(selectedMidiChannel + 1);
		}
		break;
	case '.':
	case '>':
		if (midiMode) {
			selectedMidiChannel = (selectedMidiChannel + 1) % 16;
			midiAnalysisStatus = "Selected MIDI channel " + ofToString(selectedMidiChannel + 1);
		}
		break;
	case 'k':
	case 'K':
		if (midiMode) {
			if (mutedMidiChannels.count(selectedMidiChannel) > 0) {
				mutedMidiChannels.erase(selectedMidiChannel);
				midiAnalysisStatus = "Unmuted MIDI channel " + ofToString(selectedMidiChannel + 1);
			} else {
				mutedMidiChannels.insert(selectedMidiChannel);
				midiAnalysisStatus = "Muted MIDI channel " + ofToString(selectedMidiChannel + 1);
			}
			sendMidiPanic();
		}
		break;
	case 'l':
	case 'L':
		if (midiMode) {
			soloMidiChannel = (soloMidiChannel == selectedMidiChannel) ? -1 : selectedMidiChannel;
			midiAnalysisStatus = soloMidiChannel >= 0
				? ("Solo MIDI channel " + ofToString(soloMidiChannel + 1))
				: "Cleared MIDI solo";
			sendMidiPanic();
		}
		break;
	case '0':
		if (midiMode) {
			soloMidiChannel = -1;
			midiAnalysisStatus = "Cleared MIDI solo";
			sendMidiPanic();
		}
		break;
	case OF_KEY_UP:
		if (!midiMode) {
			player->setVolume(std::min(100, player->getVolume() + 5));
		}
		break;
	case OF_KEY_DOWN:
		if (!midiMode) {
			player->setVolume(std::max(0, player->getVolume() - 5));
		}
		break;
	default:
		break;
	}
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
	(void)w;
	(void)h;
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
	replacePlaylistFromDroppedFiles(dragInfo.files);
}

void ofApp::replacePlaylistFromDroppedFiles(const std::vector<std::filesystem::path> & paths) {
	if (!player || shuttingDown) {
		return;
	}

	if (!paths.empty() && isMidiPath(paths.front().string())) {
		loadMidiPath(paths.front().string(), true);
		return;
	}

	sendMidiPanic();
	clearMidiMode();
	player->clearPlaylist();
	for (const auto & path : paths) {
		player->addPathToPlaylist(path.string());
	}

	if (player->hasPlaylist()) {
		midiAnalysisStatus = "Dropped media ready. Press Space to start.";
	}
}

//--------------------------------------------------------------
void ofApp::updateAudioOnlyMode() {
	if (!player || shuttingDown) {
		return;
	}

	const bool shouldUseAudioOnlyMode = isMidiModeActive() || isCurrentMediaAudioOnly();
	if (audioOnlyModeActive == shouldUseAudioOnlyMode) {
		return;
	}

	audioOnlyModeActive = shouldUseAudioOnlyMode;
	player->setWatchTimeEnabled(!audioOnlyModeActive);
	ofSetFrameRate(audioOnlyModeActive ? kAudioOnlyModeFrameRate : kVideoModeFrameRate);
}

//--------------------------------------------------------------
void ofApp::updateMidiAnalysis() {
	if (!player || shuttingDown || isMidiModeActive()) {
		return;
	}

	const std::string currentPath = player->getCurrentPath();
	if (!isMidiPath(currentPath)) {
		clearMidiAnalysis();
		return;
	}
	if (currentPath == analyzedMidiPath) {
		return;
	}

	if (analyzeMidiPath(currentPath)) {
		exportMidiAnalysis(currentPath);
	}
}

//--------------------------------------------------------------
void ofApp::clearMidiAnalysis() {
	analyzedMidiPath.clear();
	midiExportPath.clear();
	midiAnalysisStatus.clear();
	midiReport = {};
	midiChannelMessages.clear();
}

//--------------------------------------------------------------
bool ofApp::loadMidiPath(const std::string & path, bool autoPlay) {
	if (!player || shuttingDown) {
		return false;
	}

	player->stop();
	player->clearPlaylist();
	sendMidiPanic();
	clearMidiMode();
	if (!analyzeMidiPath(path)) {
		return false;
	}
	if (!midiPlayback.load(path, midiReport, midiChannelMessages)) {
		midiAnalysisStatus = "MIDI transport load failed.";
		return false;
	}

	midiAnalysisStatus = "MIDI transport ready.";
	if (autoPlay) {
		midiPlayback.play(ofGetElapsedTimef());
		midiAnalysisStatus = "MIDI transport playing.";
	} else {
		midiAnalysisStatus = "MIDI transport ready. Press Space to start.";
	}
	return true;
}

//--------------------------------------------------------------
void ofApp::clearMidiMode() {
	midiPlayback.clear();
	selectedMidiChannel = 0;
	soloMidiChannel = -1;
	mutedMidiChannels.clear();
	midiWasPlaying = false;
	clearMidiAnalysis();
}

//--------------------------------------------------------------
bool ofApp::isMidiModeActive() const {
	return midiPlayback.isLoaded();
}

//--------------------------------------------------------------
void ofApp::refreshMidiOutputs() {
	const std::string previouslyOpenName = midiOut.isOpen() ? midiOut.getName() : "";
	closeMidiOutput();
	midiOutputPorts = midiOut.getOutPortList();
	if (midiOutputPorts.empty()) {
		midiOutputPortIndex = -1;
		midiOutputStatus = "no output ports";
		ofLogNotice("ofxVlc4") << "No MIDI output ports available.";
		return;
	}

	std::string preferredName = previouslyOpenName;
	if (preferredName.empty()) {
		std::ifstream input(ofToDataPath("settings/midi-output.txt", true));
		if (input) {
			std::getline(input, preferredName);
			preferredName = trimWhitespace(preferredName);
		}
	}

	int preferredIndex = -1;
	if (!preferredName.empty()) {
		for (size_t i = 0; i < midiOutputPorts.size(); ++i) {
			if (midiOutputPorts[i] == preferredName) {
				preferredIndex = static_cast<int>(i);
				break;
			}
		}
	}

	if (!openMidiOutputPort(preferredIndex >= 0 ? preferredIndex : 0)) {
		midiOutputStatus = "failed to open MIDI output";
	}
}

//--------------------------------------------------------------
bool ofApp::openMidiOutputPort(int index) {
	if (index < 0 || index >= static_cast<int>(midiOutputPorts.size())) {
		return false;
	}

	sendMidiPanic();
	midiOut.closePort();
	if (!midiOut.openPort(static_cast<unsigned int>(index))) {
		midiOutputStatus = "failed: " + midiOutputPorts[index];
		midiOutputPortIndex = -1;
		return false;
	}

	midiOutputPortIndex = index;
	midiOutputStatus = midiOut.getName();
	saveMidiOutputPreference();
	ofLogNotice("ofxVlc4") << "Opened MIDI output: " << midiOutputStatus;
	return true;
}

//--------------------------------------------------------------
void ofApp::closeMidiOutput() {
	if (midiOut.isOpen()) {
		midiOut.closePort();
	}
	midiOutputPortIndex = -1;
	if (midiOutputPorts.empty()) {
		midiOutputStatus = "no output ports";
	} else {
		midiOutputStatus = "not connected";
	}
}

//--------------------------------------------------------------
void ofApp::saveMidiOutputPreference() const {
	if (midiOutputPortIndex < 0 || midiOutputPortIndex >= static_cast<int>(midiOutputPorts.size())) {
		return;
	}

	const std::string settingsDir = ofToDataPath("settings", true);
	std::error_code ec;
	std::filesystem::create_directories(settingsDir, ec);
	std::ofstream output(ofToDataPath("settings/midi-output.txt", true), std::ios::trunc);
	if (!output) {
		return;
	}
	output << midiOutputPorts[midiOutputPortIndex] << "\n";
}

//--------------------------------------------------------------
void ofApp::dispatchMidiMessages(const std::vector<MidiChannelMessage> & messages, size_t beginIndex, size_t endIndex) {
	if (!midiOut.isOpen() || messages.empty() || beginIndex >= endIndex || beginIndex >= messages.size()) {
		return;
	}

	const size_t clampedEnd = std::min(endIndex, messages.size());
	for (size_t i = beginIndex; i < clampedEnd; ++i) {
		const MidiChannelMessage & message = messages[i];
		if (message.bytes.empty()) {
			continue;
		}
		if (message.channel >= 0) {
			if (soloMidiChannel >= 0 && message.channel != soloMidiChannel) {
				continue;
			}
			if (mutedMidiChannels.count(message.channel) > 0) {
				continue;
			}
		}
		auto & bytes = const_cast<std::vector<unsigned char> &>(message.bytes);
		midiOut.sendMidiBytes(bytes);
	}
}

//--------------------------------------------------------------
void ofApp::sendMidiPanic() {
	if (!midiOut.isOpen()) {
		return;
	}

	for (int channel = 1; channel <= 16; ++channel) {
		midiOut.sendControlChange(channel, 120, 0);
		midiOut.sendControlChange(channel, 123, 0);
	}
}

//--------------------------------------------------------------
bool ofApp::analyzeMidiPath(const std::string & path) {
	clearMidiAnalysis();
	analyzedMidiPath = path;
	midiReport = midiAnalyzer.analyzeFile(path);
	if (!midiReport.valid) {
		midiAnalysisStatus = "MIDI analysis failed: " + midiReport.errorMessage;
		return false;
	}

	midiChannelMessages = MidiBridge::toChannelMessages(midiReport);
	midiAnalysisStatus = "MIDI analyzed in memory.";
	return true;
}

//--------------------------------------------------------------
bool ofApp::exportMidiAnalysis(const std::string & path) {
	if (midiReport.valid && analyzedMidiPath != path) {
		if (!analyzeMidiPath(path)) {
			return false;
		}
	} else if (!midiReport.valid) {
		if (!analyzeMidiPath(path)) {
			return false;
		}
	}

	const std::string logDir = ofToDataPath("logs", true);
	std::error_code ec;
	std::filesystem::create_directories(logDir, ec);

	const std::string baseName = ofFilePath::removeExt(ofFilePath::getFileName(path));
	const std::filesystem::path exportPrefix = std::filesystem::path(logDir) / (baseName + ".midi");
	std::string exportError;
	if (!MidiReportExporter::exportAll(midiReport, exportPrefix.string(), exportError)) {
		midiAnalysisStatus = "MIDI export failed: " + exportError;
		ofLogError("ofxVlc4") << midiAnalysisStatus;
		return false;
	}
	midiExportPath = exportPrefix.string() + ".txt";
	midiAnalysisStatus = "MIDI summary export written.";
	ofLogNotice("ofxVlc4") << "MIDI summary export written: " << midiExportPath;
	return true;
}

//--------------------------------------------------------------
bool ofApp::isCurrentMediaAudioOnly() const {
	if (!player || isMidiModeActive()) {
		return false;
	}

	if (!hasActiveVlcMedia(*player)) {
		return false;
	}

	const ofxVlc4::MediaReadinessInfo readiness = player->getMediaReadinessInfo();
	if (readiness.mediaAttached && readiness.audioTrackCount > 0 && readiness.videoTrackCount == 0) {
		return true;
	}

	const int currentIndex = player->getCurrentIndex();
	if (currentIndex < 0 || currentIndex >= static_cast<int>(player->getPlaylist().size())) {
		return false;
	}

	const std::string extension = ofToLower(ofFilePath::getFileExt(player->getFileNameAtIndex(currentIndex)));
	return !extension.empty() && audioOnlyExtensions().count("." + extension) > 0;
}

//--------------------------------------------------------------
std::string ofApp::currentMediaLabel() {
	if (!player) {
		return "Player closed";
	}

	if (isMidiModeActive()) {
		return ofFilePath::getFileName(midiPlayback.getPath());
	}

	if (!hasActiveVlcMedia(*player)) {
		return "No media loaded";
	}

	const int currentIndex = player->getCurrentIndex();
	if (currentIndex >= 0 && currentIndex < static_cast<int>(player->getPlaylist().size())) {
		return player->getFileNameAtIndex(currentIndex);
	}
	return "No media loaded";
}

//--------------------------------------------------------------
std::string ofApp::playbackLabel() {
	if (!player) {
		return "Closed";
	}

	if (isMidiModeActive()) {
		if (midiPlayback.isPlaying()) {
			return "Playing (MIDI out)";
		}
		if (midiPlayback.isPaused()) {
			return "Paused (MIDI out)";
		}
		if (midiPlayback.isFinished()) {
			return "Finished (MIDI out)";
		}
		return "Stopped (MIDI out)";
	}

	if (!hasActiveVlcMedia(*player)) {
		return "Idle";
	}

	if (player->isPlaying()) {
		return "Playing";
	}
	if (player->canPause() && !player->isStopped()) {
		return "Paused";
	}
	if (player->isStopped()) {
		return "Stopped";
	}
	return "Idle";
}
