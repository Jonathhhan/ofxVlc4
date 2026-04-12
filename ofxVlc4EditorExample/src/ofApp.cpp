#include "ofApp.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {
constexpr int kFrameRate = 60;
constexpr float kDefaultAspect = 16.0f / 9.0f;
constexpr float kPanelMargin = 8.0f;

std::string fileNameFromPath(const std::string & path) {
	return ofFilePath::getFileName(path);
}
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ofApp::setup() {
	ofSetWindowTitle("ofxVlc4 Editor Example");
	ofSetFrameRate(kFrameRate);
	ofSetBackgroundColor(ofColor(20, 20, 22));
	ofSetColor(255);

	exportOutputDir = ofToDataPath("exports", true);
	std::error_code error;
	std::filesystem::create_directories(exportOutputDir, error);

	player = std::make_unique<ofxVlc4>();
	player->setAudioCaptureEnabled(true);
	player->init(0, nullptr);
	player->setWatchTimeEnabled(true);
	player->setWatchTimeMinPeriodUs(30000);
	player->setVolume(70);
	player->setPlaybackMode(ofxVlc4::PlaybackMode::Default);

	gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
	ImGui::GetIO().IniFilename = "imgui_editor.ini";
	loadSeedMedia();
}

void ofApp::update() {
	if (!player || shuttingDown) {
		return;
	}
	player->update();

	if (exportState != ExportState::Idle && exportState != ExportState::Done && exportState != ExportState::Failed) {
		updateExportProgress();
	}
}

void ofApp::draw() {
	ofBackground(20, 20, 22);
	if (!player) {
		return;
	}

	// Layout: preview top-left, clip list right, timeline bar bottom.
	const float windowW = static_cast<float>(ofGetWidth());
	const float windowH = static_cast<float>(ofGetHeight());
	const float timelineHeight = 220.0f;
	const float previewH = windowH - timelineHeight;
	const float previewW = windowW;

	drawPreview(0.0f, 0.0f, previewW, previewH);

	gui.begin();
	drawTimelinePanel();
	gui.end();
}

void ofApp::exit() {
	shutdownPlayer();
	gui.exit();
}

void ofApp::audioOut(ofSoundBuffer & buffer) {
	buffer.set(0.0f);
	if (!player || shuttingDown) {
		return;
	}
	player->readAudioIntoBuffer(buffer, 1.0f);
}

void ofApp::keyPressed(int key) {
	if (!player || shuttingDown) {
		return;
	}
	switch (key) {
	case 'o':
	case 'O': {
		ofFileDialogResult result = ofSystemLoadDialog("Add media file to timeline");
		if (result.bSuccess) {
			addClipFromFile(result.getPath());
		}
		break;
	}
	case ' ':
		playPause();
		break;
	case 's':
	case 'S':
		stopPlayback();
		break;
	case '.':
		stepFrame(1);
		break;
	case ',':
		stepFrame(-1);
		break;
	case 'i':
	case 'I':
		if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size())) {
			clips[selectedClipIndex].inPointMs = player->getTime();
		}
		break;
	case 'k':
	case 'K':
		if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size())) {
			clips[selectedClipIndex].outPointMs = player->getTime();
		}
		break;
	case 'e':
	case 'E':
		if (exportState == ExportState::Idle) {
			startExport();
		}
		break;
	case OF_KEY_DEL:
	case OF_KEY_BACKSPACE:
		if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size())) {
			removeClip(selectedClipIndex);
		}
		break;
	case OF_KEY_UP:
		player->setVolume(std::min(100, player->getVolume() + 5));
		break;
	case OF_KEY_DOWN:
		player->setVolume(std::max(0, player->getVolume() - 5));
		break;
	default:
		break;
	}
}

void ofApp::dragEvent(ofDragInfo dragInfo) {
	for (const auto & file : dragInfo.files) {
		addClipFromFile(file.string());
	}
}

// ---------------------------------------------------------------------------
// Player helpers
// ---------------------------------------------------------------------------

void ofApp::shutdownPlayer() {
	if (!player || shuttingDown) {
		return;
	}
	shuttingDown = true;
	if (exportState == ExportState::RecordingClip) {
		player->stopRecordingSession();
	}
	player->close();
	player.reset();
}

void ofApp::loadSeedMedia() {
	if (!player || shuttingDown) {
		return;
	}
	const std::filesystem::path sharedMoviesDirectory =
		std::filesystem::path(ofFilePath::getCurrentExeDir()) /
		"../../../../examples/video/videoPlayerExample/bin/data/movies";
	const std::vector<std::filesystem::path> candidates = {
		ofToDataPath("finger.mp4", true),
		ofToDataPath("fingers.mp4", true),
		ofToDataPath("movie.mp4", true),
		ofToDataPath("sample.mp4", true),
		sharedMoviesDirectory / "finger.mp4",
		sharedMoviesDirectory / "fingers.mp4"
	};

	for (const auto & candidate : candidates) {
		std::error_code error;
		if (std::filesystem::exists(candidate, error) && !error) {
			addClipFromFile(candidate.string());
			return;
		}
	}
}

bool ofApp::loadMediaPath(const std::string & path, bool autoPlay) {
	if (!player || path.empty()) {
		return false;
	}
	player->stop();
	player->clearPlaylist();
	player->addPathToPlaylist(path);
	if (autoPlay) {
		player->playIndex(0);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Clip model
// ---------------------------------------------------------------------------

void ofApp::addClipFromFile(const std::string & path) {
	if (path.empty()) {
		return;
	}

	Clip clip;
	clip.sourcePath = path;
	clip.label = fileNameFromPath(path);
	clip.inPointMs = 0;
	clip.outPointMs = 0; // 0 means "use full duration" until we know the real length
	clips.push_back(clip);

	// Preview the newly added clip so the user can set in/out points.
	selectClip(static_cast<int>(clips.size()) - 1);
	previewClip(static_cast<int>(clips.size()) - 1);
}

void ofApp::removeClip(int index) {
	if (index < 0 || index >= static_cast<int>(clips.size())) {
		return;
	}
	clips.erase(clips.begin() + index);
	if (selectedClipIndex >= static_cast<int>(clips.size())) {
		selectedClipIndex = static_cast<int>(clips.size()) - 1;
	}
}

void ofApp::moveClipUp(int index) {
	if (index <= 0 || index >= static_cast<int>(clips.size())) {
		return;
	}
	std::swap(clips[index], clips[index - 1]);
	if (selectedClipIndex == index) {
		selectedClipIndex = index - 1;
	} else if (selectedClipIndex == index - 1) {
		selectedClipIndex = index;
	}
}

void ofApp::moveClipDown(int index) {
	if (index < 0 || index >= static_cast<int>(clips.size()) - 1) {
		return;
	}
	std::swap(clips[index], clips[index + 1]);
	if (selectedClipIndex == index) {
		selectedClipIndex = index + 1;
	} else if (selectedClipIndex == index + 1) {
		selectedClipIndex = index;
	}
}

void ofApp::selectClip(int index) {
	selectedClipIndex = (index >= 0 && index < static_cast<int>(clips.size())) ? index : -1;
}

void ofApp::previewClip(int index) {
	if (!player || index < 0 || index >= static_cast<int>(clips.size())) {
		return;
	}
	const Clip & clip = clips[index];
	loadMediaPath(clip.sourcePath, true);
	if (clip.inPointMs > 0) {
		player->setTime(clip.inPointMs);
	}
}

int ofApp::totalTimelineDurationMs() const {
	int total = 0;
	for (const auto & clip : clips) {
		total += clip.durationMs();
	}
	return total;
}

int ofApp::clipStartOnTimeline(int index) const {
	int start = 0;
	for (int i = 0; i < index && i < static_cast<int>(clips.size()); ++i) {
		start += clips[i].durationMs();
	}
	return start;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void ofApp::playPause() {
	if (!player) {
		return;
	}
	if (player->isPlaying()) {
		player->pause();
	} else {
		if (selectedClipIndex >= 0 && selectedClipIndex < static_cast<int>(clips.size())) {
			if (player->getCurrentPath().empty() ||
				player->getCurrentPath() != clips[selectedClipIndex].sourcePath) {
				previewClip(selectedClipIndex);
				return;
			}
		}
		player->play();
	}
}

void ofApp::stopPlayback() {
	if (!player) {
		return;
	}
	player->stop();
}

void ofApp::seekToTimelineMs(int timelineMs) {
	// Find which clip this falls into.
	int accumulated = 0;
	for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
		const int clipDur = clips[i].durationMs();
		if (timelineMs < accumulated + clipDur) {
			selectClip(i);
			previewClip(i);
			const int offset = timelineMs - accumulated;
			player->setTime(clips[i].inPointMs + offset);
			return;
		}
		accumulated += clipDur;
	}
}

void ofApp::stepFrame(int direction) {
	if (!player) {
		return;
	}
	if (direction > 0) {
		player->nextFrame();
	} else {
		player->jumpTime(-33);
	}
}

// ---------------------------------------------------------------------------
// Export pipeline
// ---------------------------------------------------------------------------

void ofApp::startExport() {
	if (!player || clips.empty()) {
		return;
	}

	exportState = ExportState::PreparingClip;
	exportClipIndex = 0;
	exportClipStarted = false;
	exportError.clear();
	exportLastFile.clear();

	std::error_code error;
	std::filesystem::create_directories(exportOutputDir, error);
}

void ofApp::updateExportProgress() {
	if (!player) {
		exportState = ExportState::Failed;
		exportError = "Player closed during export.";
		return;
	}

	switch (exportState) {
	case ExportState::PreparingClip: {
		if (exportClipIndex < 0 || exportClipIndex >= static_cast<int>(clips.size())) {
			exportState = ExportState::Done;
			return;
		}
		const Clip & clip = clips[exportClipIndex];
		loadMediaPath(clip.sourcePath, false);
		player->setTime(clip.inPointMs);
		exportClipStarted = false;
		exportState = ExportState::RecordingClip;

		const std::string baseName = "export-clip-" + ofToString(exportClipIndex);
		const std::string basePath = ofFilePath::join(exportOutputDir, baseName);

		player->setRecordingPreset(ofxVlc4RecordingPreset{});
		player->recordAudioVideo(basePath, player->getTexture());
		player->play();
		exportClipStarted = true;
		break;
	}
	case ExportState::RecordingClip: {
		if (exportClipIndex < 0 || exportClipIndex >= static_cast<int>(clips.size())) {
			exportState = ExportState::Failed;
			exportError = "Invalid clip index during recording.";
			return;
		}
		const Clip & clip = clips[exportClipIndex];
		const int currentTime = player->getTime();
		const int outPoint = clip.outPointMs;

		// If out-point is set and we've reached it, stop recording for this clip.
		if (outPoint > 0 && currentTime >= outPoint) {
			player->stop();
			exportClipStarted = false;

			// Move to next clip.
			++exportClipIndex;
			if (exportClipIndex >= static_cast<int>(clips.size())) {
				exportState = ExportState::Done;
				exportLastFile = ofFilePath::join(exportOutputDir, "export-clip-" + ofToString(exportClipIndex - 1));
			} else {
				exportState = ExportState::PreparingClip;
			}
		}
		break;
	}
	case ExportState::WaitingForMux: {
		const ofxVlc4RecordingSessionState muxState = player->getRecordingSessionState();
		if (muxState == ofxVlc4RecordingSessionState::Done ||
			muxState == ofxVlc4RecordingSessionState::Idle) {
			++exportClipIndex;
			if (exportClipIndex >= static_cast<int>(clips.size())) {
				exportState = ExportState::Done;
			} else {
				exportState = ExportState::PreparingClip;
			}
		} else if (muxState == ofxVlc4RecordingSessionState::Failed) {
			exportState = ExportState::Failed;
			exportError = "Mux failed for clip " + ofToString(exportClipIndex) + ".";
		}
		break;
	}
	default:
		break;
	}
}

void ofApp::cancelExport() {
	if (exportState == ExportState::RecordingClip && player) {
		player->stop();
	}
	exportState = ExportState::Idle;
	exportClipIndex = -1;
	exportClipStarted = false;
}

// ---------------------------------------------------------------------------
// UI Drawing
// ---------------------------------------------------------------------------

void ofApp::drawPreview(float x, float y, float w, float h) {
	if (!player) {
		return;
	}

	const ofxVlc4::MediaReadinessInfo readiness = player->getMediaReadinessInfo();
	const ofxVlc4::VideoStateInfo videoState = player->getVideoStateInfo();

	float sourceW = static_cast<float>(videoState.sourceWidth);
	float sourceH = static_cast<float>(videoState.sourceHeight);
	if (sourceW <= 1.0f || sourceH <= 1.0f) {
		sourceW = kDefaultAspect;
		sourceH = 1.0f;
	}

	const float sourceAspect = sourceW / sourceH;
	float drawW = w;
	float drawH = drawW / sourceAspect;
	if (drawH > h) {
		drawH = h;
		drawW = drawH * sourceAspect;
	}
	const float drawX = x + (w - drawW) * 0.5f;
	const float drawY = y + (h - drawH) * 0.5f;

	ofPushStyle();
	ofSetColor(0, 0, 0);
	ofDrawRectangle(drawX, drawY, drawW, drawH);
	if (readiness.hasReceivedVideoFrame) {
		ofSetColor(255);
		player->draw(drawX, drawY, drawW, drawH);
	} else {
		ofSetColor(120);
		ofDrawBitmapString(
			clips.empty()
				? "Drop media files or press O to add clips."
				: "Waiting for video frame...",
			drawX + 14.0f, drawY + 24.0f);
	}
	ofPopStyle();

	// Overlay timecode.
	const std::string timecode = player->formatCurrentPlaybackTimecode();
	if (!timecode.empty()) {
		ofPushStyle();
		ofSetColor(0, 0, 0, 160);
		ofDrawRectangle(drawX + drawW - 160.0f, drawY + drawH - 30.0f, 156.0f, 26.0f);
		ofSetColor(220, 220, 220);
		ofDrawBitmapString(timecode, drawX + drawW - 154.0f, drawY + drawH - 12.0f);
		ofPopStyle();
	}
}

void ofApp::drawTimelinePanel() {
	if (!player) {
		return;
	}

	const float windowW = static_cast<float>(ofGetWidth());
	const float windowH = static_cast<float>(ofGetHeight());
	const float panelHeight = 220.0f;

	ImGui::SetNextWindowPos(ImVec2(0.0f, windowH - panelHeight), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(windowW, panelHeight), ImGuiCond_Always);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

	if (ImGui::Begin("##TimelinePanel", nullptr, flags)) {
		drawTransportBar();
		ImGui::Separator();
		drawClipList();
		ImGui::Separator();
		drawExportPanel();
	}
	ImGui::End();
}

void ofApp::drawTransportBar() {
	if (ImGui::Button("Add Clip...")) {
		ofFileDialogResult result = ofSystemLoadDialog("Add media file");
		if (result.bSuccess) {
			addClipFromFile(result.getPath());
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(player->isPlaying() ? "Pause" : "Play")) {
		playPause();
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop")) {
		stopPlayback();
	}
	ImGui::SameLine();
	if (ImGui::Button("<")) {
		stepFrame(-1);
	}
	ImGui::SameLine();
	if (ImGui::Button(">")) {
		stepFrame(1);
	}
	ImGui::SameLine();
	int volume = player->getVolume();
	ImGui::SetNextItemWidth(100.0f);
	if (ImGui::SliderInt("Vol", &volume, 0, 100)) {
		player->setVolume(volume);
	}

	// Position scrubber for the currently loaded media.
	ImGui::SameLine();
	const float length = player->getLength();
	if (length > 0.0f) {
		float pos = player->getPosition();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::SliderFloat("##Pos", &pos, 0.0f, 1.0f, formatTimeMs(static_cast<int>(pos * length)).c_str())) {
			player->setPosition(pos);
		}
	} else {
		ImGui::Text("No media loaded");
	}
}

void ofApp::drawClipList() {
	ImGui::BeginChild("##ClipList", ImVec2(0, 90), true, ImGuiWindowFlags_HorizontalScrollbar);

	if (clips.empty()) {
		ImGui::TextDisabled("No clips on the timeline.  Drop files or press 'O' to add.");
	}

	for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
		Clip & clip = clips[i];
		ImGui::PushID(i);

		const bool isSelected = (i == selectedClipIndex);
		const std::string header = ofToString(i + 1) + ". " + clip.label;

		if (ImGui::Selectable(header.c_str(), isSelected, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
			selectClip(i);
			previewClip(i);
		}
		ImGui::SameLine();

		// Show in / out.
		ImGui::Text("[%s - %s]",
			formatTimeMs(clip.inPointMs).c_str(),
			clip.outPointMs > 0 ? formatTimeMs(clip.outPointMs).c_str() : "end");

		if (isSelected) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Set In (I)")) {
				clip.inPointMs = player->getTime();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Set Out (K)")) {
				clip.outPointMs = player->getTime();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Reset")) {
				clip.inPointMs = 0;
				clip.outPointMs = 0;
			}
			ImGui::SameLine();
			if (i > 0 && ImGui::SmallButton("Up")) {
				moveClipUp(i);
			}
			if (i > 0) {
				ImGui::SameLine();
			}
			if (i < static_cast<int>(clips.size()) - 1 && ImGui::SmallButton("Down")) {
				moveClipDown(i);
			}
			if (i < static_cast<int>(clips.size()) - 1) {
				ImGui::SameLine();
			}
			if (ImGui::SmallButton("Remove")) {
				removeClip(i);
				ImGui::PopID();
				break; // list invalidated
			}
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
}

void ofApp::drawExportPanel() {
	const bool isIdle = exportState == ExportState::Idle;
	const bool isDone = exportState == ExportState::Done;
	const bool isFailed = exportState == ExportState::Failed;

	if (isIdle) {
		ImGui::BeginDisabled(clips.empty());
		if (ImGui::Button("Export Timeline (E)")) {
			startExport();
		}
		ImGui::EndDisabled();
	} else if (isDone) {
		ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Export complete.");
		if (!exportLastFile.empty()) {
			ImGui::SameLine();
			ImGui::TextWrapped("Last: %s", exportLastFile.c_str());
		}
		if (ImGui::Button("OK")) {
			exportState = ExportState::Idle;
		}
	} else if (isFailed) {
		ImGui::TextColored(ImVec4(0.95f, 0.3f, 0.2f, 1.0f), "Export failed: %s", exportError.c_str());
		if (ImGui::Button("OK")) {
			exportState = ExportState::Idle;
		}
	} else {
		// In progress.
		const std::string progressLabel = "Exporting clip " + ofToString(exportClipIndex + 1) +
			" / " + ofToString(clips.size()) + "...";
		ImGui::Text("%s", progressLabel.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			cancelExport();
		}
	}

	ImGui::SameLine();
	ImGui::Text("Output: %s", exportOutputDir.c_str());
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::string ofApp::formatTimeMs(int ms) {
	if (ms < 0) {
		ms = 0;
	}
	const int totalSeconds = ms / 1000;
	const int hours = totalSeconds / 3600;
	const int minutes = (totalSeconds % 3600) / 60;
	const int seconds = totalSeconds % 60;
	const int millis = ms % 1000;

	char buffer[32];
	if (hours > 0) {
		std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d.%03d", hours, minutes, seconds, millis);
	} else {
		std::snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d", minutes, seconds, millis);
	}
	return std::string(buffer);
}
