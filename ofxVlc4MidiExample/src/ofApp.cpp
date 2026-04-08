#include "ofApp.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace {
constexpr float kDefaultAspect = 16.0f / 9.0f;
constexpr int kVideoModeFrameRate = 60;
constexpr int kAudioOnlyModeFrameRate = 30;
constexpr int kDefaultVolume = 70;

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
	ofSetWindowTitle("ofxVlc4 MIDI Example");
	ofSetFrameRate(kVideoModeFrameRate);
	ofSetBackgroundColor(ofColor(12, 12, 12));
	ofSetColor(255);

	const std::string logDir = ofToDataPath("logs", true);
	std::error_code directoryError;
	std::filesystem::create_directories(logDir, directoryError);

	player = std::make_unique<ofxVlc4>();
	player->setAudioCaptureEnabled(false);
	player->init(0, nullptr);
	player->setWatchTimeEnabled(true);
	player->setWatchTimeMinPeriodUs(50000);
	player->setVolume(kDefaultVolume);
	player->setMidiSyncToWatchTimeEnabled(false);

	installMidiDispatchCallback();
	refreshMidiOutputs();

	gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
	ImGui::GetIO().IniFilename = "imgui_midi.ini";

	midiAnalysisStatus = "Open or drop media to begin.";
}

//--------------------------------------------------------------
void ofApp::update() {
	if (!player || shuttingDown) {
		return;
	}

	player->update();

	const ofxVlc4::MidiTransportInfo midiInfo = player->getMidiTransportInfo();
	if (midiWasPlaying && midiInfo.loaded && !midiInfo.playing) {
		sendMidiPanic();
	}
	midiWasPlaying = midiInfo.playing;

	updateAudioOnlyMode();
}

//--------------------------------------------------------------
void ofApp::draw() {
	ofBackground(12, 12, 12);
	drawPreview();

	if (!player || shuttingDown) {
		return;
	}

	gui.begin();
	drawControlPanel();
	gui.end();
}

//--------------------------------------------------------------
void ofApp::drawPreview() {
	if (!player) {
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

	const float availableWidth = std::max(1.0f, static_cast<float>(ofGetWidth()) - previewMargin * 2.0f);
	const float availableHeight = std::max(1.0f, static_cast<float>(ofGetHeight()) - previewMargin * 2.0f);
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
	const float drawY = (ofGetHeight() - drawHeight) * 0.5f;

	ofPushStyle();
	ofSetColor(26, 26, 26);
	ofDrawRectangle(drawX - 1.0f, drawY - 1.0f, drawWidth + 2.0f, drawHeight + 2.0f);
	ofSetColor(0, 0, 0);
	ofDrawRectangle(drawX, drawY, drawWidth, drawHeight);
	if (!midiMode && readiness.hasReceivedVideoFrame) {
		ofSetColor(255);
		player->draw(drawX, drawY, drawWidth, drawHeight);
	} else {
		ofSetColor(160);
		const std::string placeholder = midiMode
			? "Local MIDI transport active.\nMessages are routed to the selected MIDI output."
			: (!vlcHasMedia
				? "Open or drop media to begin."
				: (audioOnlyModeActive
					? "Audio-only media active.\nThe video preview is intentionally idle."
					: "Waiting for the first video frame..."));
		ofDrawBitmapStringHighlight(placeholder, drawX + 18.0f, drawY + 28.0f, ofColor(20, 20, 20, 180), ofColor(220));
	}
	ofPopStyle();
}

//--------------------------------------------------------------
void ofApp::drawControlPanel() {
	if (!player) {
		return;
	}

	const bool midiMode = isMidiModeActive();
	const ofxVlc4::MidiTransportInfo midiInfo = player->getMidiTransportInfo();
	const MidiAnalysisReport midiReport = player->getMidiAnalysisReport();
	const bool vlcHasMedia = !midiMode && hasActiveVlcMedia(*player);
	const ofxVlc4::MediaReadinessInfo readiness = vlcHasMedia
		? player->getMediaReadinessInfo()
		: ofxVlc4::MediaReadinessInfo{};
	const int currentVolume = player->getVolume();

	ImGui::SetNextWindowSize(ImVec2(420.0f, 680.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("MIDI Controls", nullptr, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", currentMediaLabel().c_str());
	ImGui::Text("State: %s", playbackLabel().c_str());
	if (midiMode) {
		ImGui::Text("Position: %.2f / %.2f s", midiInfo.positionSeconds, midiInfo.durationSeconds);
	} else {
		ImGui::Text("Playback timecode: %s", player->formatCurrentPlaybackTimecode().c_str());
	}
	ImGui::Separator();

	if (ImGui::Button("Open...", ImVec2(88, 0))) {
		ofFileDialogResult result = ofSystemLoadDialog("Choose media or MIDI file");
		if (result.bSuccess) {
			replacePlaylistFromDroppedFiles({ std::filesystem::path(result.getPath()) });
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(midiMode ? (midiInfo.playing ? "Pause" : "Play") : (player->isPlaying() ? "Pause" : "Play"), ImVec2(88, 0))) {
		if (midiMode) {
			if (midiInfo.playing) {
				player->pauseMidi();
				sendMidiPanic();
			} else {
				player->playMidi();
			}
		} else if (player->isPlaying()) {
			player->pause();
		} else {
			player->play();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop", ImVec2(88, 0))) {
		if (midiMode) {
			player->stopMidi();
			sendMidiPanic();
		} else {
			player->stop();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh", ImVec2(88, 0))) {
		refreshMidiOutputs();
	}

	if (!midiMode) {
		if (ImGui::Button("Prev", ImVec2(88, 0))) {
			player->previousMediaListItem();
		}
		ImGui::SameLine();
		if (ImGui::Button("Next", ImVec2(88, 0))) {
			player->nextMediaListItem();
		}
		ImGui::SameLine();
		if (ImGui::Button(player->isMuted() ? "Unmute" : "Mute", ImVec2(88, 0))) {
			player->toggleMute();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Msgs", ImVec2(88, 0))) {
			player->clearLastMessages();
		}

		int volume = currentVolume;
		if (ImGui::SliderInt("Volume", &volume, 0, 100)) {
			player->setVolume(volume);
		}
	}

	ImGui::SeparatorText("MIDI Output");
	if (midiOutputPorts.empty()) {
		ImGui::TextDisabled("No MIDI output ports available.");
	} else {
		const char * preview = midiOutputPortIndex >= 0 && midiOutputPortIndex < static_cast<int>(midiOutputPorts.size())
			? midiOutputPorts[static_cast<size_t>(midiOutputPortIndex)].c_str()
			: "Select output";
		if (ImGui::BeginCombo("Output port", preview)) {
			for (int i = 0; i < static_cast<int>(midiOutputPorts.size()); ++i) {
				const bool selected = i == midiOutputPortIndex;
				if (ImGui::Selectable(midiOutputPorts[static_cast<size_t>(i)].c_str(), selected)) {
					openMidiOutputPort(i);
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}
	ImGui::TextWrapped("Status: %s", midiOutputStatus.c_str());

	ImGui::SeparatorText("Transport");
	if (midiMode) {
		float tempo = static_cast<float>(midiInfo.tempoMultiplier);
		if (ImGui::SliderFloat("Tempo", &tempo, 0.25f, 4.0f, "%.2fx")) {
			player->setMidiTempoMultiplier(static_cast<double>(tempo));
		}

		float position = static_cast<float>(midiInfo.positionSeconds);
		if (midiInfo.durationSeconds > 0.0) {
			if (ImGui::SliderFloat("Position", &position, 0.0f, static_cast<float>(midiInfo.durationSeconds), "%.2f s")) {
				player->seekMidi(static_cast<double>(position));
			}
		} else {
			ImGui::BeginDisabled();
			ImGui::SliderFloat("Position", &position, 0.0f, 1.0f, "%.2f s");
			ImGui::EndDisabled();
		}

		bool loopEnabled = midiInfo.loopEnabled;
		if (ImGui::Checkbox("Loop", &loopEnabled)) {
			player->setMidiLoopEnabled(loopEnabled);
		}
		ImGui::SameLine();
		ImGui::Text("BPM: %.1f", midiInfo.currentBpm);
	} else {
		const ofxVlc4::PlaylistStateInfo playlistState = player->getPlaylistStateInfo();
		ImGui::Text("Playlist items: %d", static_cast<int>(playlistState.size));
		ImGui::Text("Current index: %d", playlistState.currentIndex);
		ImGui::Text("Readiness: attached=%s prepared=%s frame=%s",
			readiness.mediaAttached ? "yes" : "no",
			readiness.startupPrepared ? "yes" : "no",
			readiness.hasReceivedVideoFrame ? "yes" : "no");
	}

	ImGui::SeparatorText("MIDI Routing");
	int channel = selectedMidiChannel + 1;
	if (ImGui::SliderInt("Channel", &channel, 1, 16)) {
		selectedMidiChannel = std::clamp(channel - 1, 0, 15);
	}
	if (ImGui::Button("Mute/Unmute Selected")) {
		if (mutedMidiChannels.count(selectedMidiChannel) > 0) {
			mutedMidiChannels.erase(selectedMidiChannel);
		} else {
			mutedMidiChannels.insert(selectedMidiChannel);
		}
		sendMidiPanic();
	}
	ImGui::SameLine();
	if (ImGui::Button("Solo Selected")) {
		soloMidiChannel = (soloMidiChannel == selectedMidiChannel) ? -1 : selectedMidiChannel;
		sendMidiPanic();
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear Solo")) {
		soloMidiChannel = -1;
		sendMidiPanic();
	}
	ImGui::Text("Solo: %s", soloMidiChannel >= 0 ? ofToString(soloMidiChannel + 1).c_str() : "none");
	ImGui::Text("Muted channels: %d", static_cast<int>(mutedMidiChannels.size()));
	if (ImGui::Button("Panic")) {
		sendMidiPanic();
	}

	ImGui::SeparatorText("MIDI Sync");
	MidiSyncSettings syncSettings = player->getMidiSyncSettings();
	int syncModeIndex = static_cast<int>(syncSettings.mode);
	static const char * syncModes[] = { "None", "MIDI Clock", "MTC Quarter Frame" };
	if (ImGui::Combo("Sync mode", &syncModeIndex, syncModes, IM_ARRAYSIZE(syncModes))) {
		syncSettings.mode = static_cast<MidiSyncMode>(syncModeIndex);
		player->setMidiSyncSettings(syncSettings);
	}
	float mtcFps = static_cast<float>(syncSettings.timecodeFps);
	if (ImGui::SliderFloat("Timecode FPS", &mtcFps, 24.0f, 60.0f, "%.2f")) {
		syncSettings.timecodeFps = static_cast<double>(mtcFps);
		player->setMidiSyncSettings(syncSettings);
	}
	bool sendTransport = syncSettings.sendTransportMessages;
	if (ImGui::Checkbox("Send transport", &sendTransport)) {
		syncSettings.sendTransportMessages = sendTransport;
		player->setMidiSyncSettings(syncSettings);
	}
	bool sendSongPosition = syncSettings.sendSongPositionOnSeek;
	if (ImGui::Checkbox("Song position on seek", &sendSongPosition)) {
		syncSettings.sendSongPositionOnSeek = sendSongPosition;
		player->setMidiSyncSettings(syncSettings);
	}

	int syncSourceIndex = static_cast<int>(midiInfo.syncSource);
	static const char * syncSourceItems[] = { "Internal", "Watch time" };
	if (ImGui::Combo("Sync source", &syncSourceIndex, syncSourceItems, IM_ARRAYSIZE(syncSourceItems))) {
		player->setMidiSyncSource(static_cast<ofxVlc4MidiSyncSource>(syncSourceIndex));
	}
	bool syncToWatchTime = midiInfo.syncToWatchTime;
	if (ImGui::Checkbox("Follow watch time", &syncToWatchTime)) {
		player->setMidiSyncToWatchTimeEnabled(syncToWatchTime);
	}

	ImGui::SeparatorText("MIDI Analysis");
	if (midiMode && midiReport.valid) {
		ImGui::Text("Format: %d", midiReport.format);
		ImGui::Text("Tracks: %d", midiReport.trackCountParsed);
		ImGui::Text("PPQ: %d", midiReport.ticksPerQuarterNote);
		ImGui::Text("Messages: %d", static_cast<int>(midiInfo.messageCount));
		ImGui::Text("Dispatched: %d", static_cast<int>(midiInfo.dispatchedCount));
		ImGui::Text("Tempo changes: %d", midiReport.tempoChangeCount);
		ImGui::Text("Markers: %d", midiReport.markerCount);
		ImGui::Text("Instruments: %d", static_cast<int>(midiReport.instruments.size()));
		ImGui::TextWrapped("Sync: %s | source: %s | callback: %s",
			syncModeLabel(syncSettings.mode).c_str(),
			syncSourceLabel(midiInfo.syncSource).c_str(),
			midiInfo.hasCallback ? "yes" : "no");
		if (ImGui::Button("Export analysis")) {
			exportMidiAnalysis(activeMidiPath);
		}
		if (!midiExportPath.empty()) {
			ImGui::TextWrapped("Export: %s", ofFilePath::getFileName(midiExportPath).c_str());
		}
	} else {
		ImGui::TextWrapped("Load a MIDI file to see timeline, sync, and analysis details.");
	}

	if (!midiAnalysisStatus.empty()) {
		ImGui::Separator();
		ImGui::TextWrapped("%s", midiAnalysisStatus.c_str());
	}
	if (!midiMode) {
		const std::string status = player->getLastErrorMessage().empty()
			? player->getLastStatusMessage()
			: ("Error: " + player->getLastErrorMessage());
		if (!status.empty()) {
			ImGui::Separator();
			ImGui::TextWrapped("%s", status.c_str());
		}
	}

	ImGui::End();
}

//--------------------------------------------------------------
void ofApp::exit() {
	shutdownPlayer();
	gui.exit();
}

//--------------------------------------------------------------
void ofApp::installMidiDispatchCallback() {
	if (!player) {
		return;
	}

	player->setMidiMessageCallback([this](const MidiChannelMessage & message) {
		dispatchMidiMessage(message);
	});
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
		player->clearMidiMessageCallback();
		player->close();
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
			const ofxVlc4::MidiTransportInfo midiInfo = player->getMidiTransportInfo();
			if (midiInfo.playing) {
				player->pauseMidi();
				sendMidiPanic();
			} else {
				player->playMidi();
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
			player->stopMidi();
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
			exportMidiAnalysis(activeMidiPath);
		}
		break;
	case '[':
		if (!midiOutputPorts.empty()) {
			const int nextIndex = midiOutputPortIndex <= 0 ? static_cast<int>(midiOutputPorts.size()) - 1 : midiOutputPortIndex - 1;
			openMidiOutputPort(nextIndex);
		}
		break;
	case ']':
		if (!midiOutputPorts.empty()) {
			const int nextIndex = midiOutputPortIndex < 0 ? 0 : (midiOutputPortIndex + 1) % static_cast<int>(midiOutputPorts.size());
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
			const auto midiInfo = player->getMidiTransportInfo();
			player->setMidiTempoMultiplier(midiInfo.tempoMultiplier - 0.1);
		}
		break;
	case '+':
	case '=':
		if (midiMode) {
			const auto midiInfo = player->getMidiTransportInfo();
			player->setMidiTempoMultiplier(midiInfo.tempoMultiplier + 0.1);
		}
		break;
	case ',':
	case '<':
		if (midiMode) {
			selectedMidiChannel = selectedMidiChannel <= 0 ? 15 : selectedMidiChannel - 1;
		}
		break;
	case '.':
	case '>':
		if (midiMode) {
			selectedMidiChannel = (selectedMidiChannel + 1) % 16;
		}
		break;
	case 'k':
	case 'K':
		if (midiMode) {
			if (mutedMidiChannels.count(selectedMidiChannel) > 0) {
				mutedMidiChannels.erase(selectedMidiChannel);
			} else {
				mutedMidiChannels.insert(selectedMidiChannel);
			}
			sendMidiPanic();
		}
		break;
	case 'l':
	case 'L':
		if (midiMode) {
			soloMidiChannel = (soloMidiChannel == selectedMidiChannel) ? -1 : selectedMidiChannel;
			sendMidiPanic();
		}
		break;
	case '0':
		if (midiMode) {
			soloMidiChannel = -1;
			sendMidiPanic();
		}
		break;
	case 'o':
	case 'O':
		if (midiMode) {
			player->setMidiLoopEnabled(!player->isMidiLoopEnabled());
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

//--------------------------------------------------------------
void ofApp::replacePlaylistFromDroppedFiles(const std::vector<std::filesystem::path> & paths) {
	if (!player || shuttingDown || paths.empty()) {
		return;
	}

	if (isMidiPath(paths.front().string())) {
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
		midiAnalysisStatus = "Dropped media ready. Press Play to start.";
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
bool ofApp::loadMidiPath(const std::string & path, bool autoPlay) {
	if (!player || shuttingDown) {
		return false;
	}

	player->stop();
	player->clearPlaylist();
	sendMidiPanic();
	clearMidiMode();
	if (!player->loadMidiFile(path)) {
		midiAnalysisStatus = "MIDI load failed: " + player->getLastErrorMessage();
		return false;
	}

	activeMidiPath = path;
	midiAnalysisStatus = "MIDI transport ready.";
	if (autoPlay) {
		player->playMidi();
		midiAnalysisStatus = "MIDI transport playing.";
	}
	return true;
}

//--------------------------------------------------------------
void ofApp::clearMidiMode() {
	if (!player) {
		return;
	}

	player->stopMidi();
	player->clearMidiTransport();
	installMidiDispatchCallback();
	activeMidiPath.clear();
	midiExportPath.clear();
	midiAnalysisStatus.clear();
	selectedMidiChannel = 0;
	soloMidiChannel = -1;
	mutedMidiChannels.clear();
	midiWasPlaying = false;
}

//--------------------------------------------------------------
bool ofApp::isMidiModeActive() const {
	return player && player->hasMidiLoaded();
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
		midiOutputStatus = "failed: " + midiOutputPorts[static_cast<size_t>(index)];
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
	output << midiOutputPorts[static_cast<size_t>(midiOutputPortIndex)] << "\n";
}

//--------------------------------------------------------------
void ofApp::dispatchMidiMessage(const MidiChannelMessage & message) {
	if (!midiOut.isOpen() || message.bytes.empty()) {
		return;
	}

	if (message.type == MidiMessageType::ChannelVoice && message.channel >= 0) {
		if (soloMidiChannel >= 0 && message.channel != soloMidiChannel) {
			return;
		}
		if (mutedMidiChannels.count(message.channel) > 0) {
			return;
		}
	}

	auto & bytes = const_cast<std::vector<unsigned char> &>(message.bytes);
	midiOut.sendMidiBytes(bytes);
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
bool ofApp::exportMidiAnalysis(const std::string & path) {
	if (!player || path.empty()) {
		return false;
	}

	const MidiAnalysisReport midiReport = player->getMidiAnalysisReport();
	if (!midiReport.valid) {
		midiAnalysisStatus = "MIDI export failed: no valid report.";
		return false;
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

	const ofxVlc4::PlaylistStateInfo playlistState = player->getPlaylistStateInfo();
	if (!playlistState.hasCurrent) {
		return false;
	}

	const auto currentItem = player->getCurrentPlaylistItemInfo();
	const std::string extension = ofToLower(ofFilePath::getFileExt(currentItem.label));
	return !extension.empty() && audioOnlyExtensions().count("." + extension) > 0;
}

//--------------------------------------------------------------
std::string ofApp::currentMediaLabel() {
	if (!player) {
		return "Player closed";
	}

	if (isMidiModeActive()) {
		return activeMidiPath.empty() ? "Local MIDI mode" : ofFilePath::getFileName(activeMidiPath);
	}

	if (!hasActiveVlcMedia(*player)) {
		return "No media loaded";
	}

	const auto currentItem = player->getCurrentPlaylistItemInfo();
	if (currentItem.index >= 0) {
		return currentItem.label;
	}
	return "No media loaded";
}

//--------------------------------------------------------------
std::string ofApp::playbackLabel() {
	if (!player) {
		return "Closed";
	}

	if (isMidiModeActive()) {
		const auto midiInfo = player->getMidiTransportInfo();
		if (midiInfo.playing) {
			return midiInfo.loopEnabled ? "Looping (MIDI out)" : "Playing (MIDI out)";
		}
		if (midiInfo.paused) {
			return "Paused (MIDI out)";
		}
		if (midiInfo.finished) {
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

//--------------------------------------------------------------
std::string ofApp::syncModeLabel(MidiSyncMode mode) const {
	switch (mode) {
	case MidiSyncMode::MidiClock:
		return "MIDI Clock";
	case MidiSyncMode::MidiTimecodeQuarterFrame:
		return "MTC Quarter Frame";
	case MidiSyncMode::None:
	default:
		return "None";
	}
}

//--------------------------------------------------------------
std::string ofApp::syncSourceLabel(ofxVlc4MidiSyncSource source) const {
	switch (source) {
	case ofxVlc4MidiSyncSource::WatchTime:
		return "Watch time";
	case ofxVlc4MidiSyncSource::Internal:
	default:
		return "Internal";
	}
}
