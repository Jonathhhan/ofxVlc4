#include "ofApp.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace {
	constexpr float kDefaultFov = 80.0f;
	constexpr float kPreviewOutlineAlpha = 34.0f;

	std::vector<std::filesystem::path> defaultSeedMediaCandidates() {
		const std::filesystem::path sharedMoviesDirectory =
			std::filesystem::path(ofFilePath::getCurrentExeDir()) /
			"..\\..\\..\\..\\examples\\video\\videoPlayerExample\\bin\\data\\movies";

		return {
			ofToDataPath("finger.mp4", true),
			ofToDataPath("fingers.mp4", true),
			ofToDataPath("movie.mp4", true),
			ofToDataPath("sample.mp4", true),
			sharedMoviesDirectory / "finger.mp4",
			sharedMoviesDirectory / "fingers.mp4",
			sharedMoviesDirectory / "movie.mp4"
		};
	}

	bool isLikelySupportedMediaPath(const std::filesystem::path & path) {
		static const std::set<std::string> extensions = {
			".wav", ".mp3", ".flac", ".ogg", ".opus",
			".m4a", ".aac", ".aiff", ".wma", ".mid", ".midi",
			".mp4", ".mov", ".mkv", ".avi", ".wmv", ".asf",
			".webm", ".m4v", ".mpg", ".mpeg", ".ts", ".mts",
			".m2ts", ".m2v", ".vob", ".ogv", ".3gp", ".m3u8",
			".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff"
		};
		const std::string extension = ofToLower(path.extension().string());
		return !extension.empty() && extensions.count(extension) > 0;
	}
}

void ofApp::setup() {
	ofSetWindowTitle("ofxVlc4360Example");
	ofSetFrameRate(60);
	ofBackground(10, 12, 16);

	ofxVlc4::setLogLevel(OF_LOG_NOTICE);
	player.setAudioCaptureEnabled(false);
	player.init(0, nullptr);
	player.setWatchTimeEnabled(true);
	player.setWatchTimeMinPeriodUs(50000);
	player.setVolume(100);

	gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
	ImGui::GetIO().IniFilename = "imgui_360.ini";

	infoStatus = "Open or drop a 360 / panoramic video to begin.";
	loadSeedMedia();
}

void ofApp::update() {
	if (shuttingDown) {
		return;
	}

	player.update();

	if (startupMediaPending && !pendingStartupMediaPath.empty()) {
		startupMediaPending = false;
		loadMediaPath(pendingStartupMediaPath, startupMediaAutoPlay);
		pendingStartupMediaPath.clear();
	}
}

void ofApp::draw() {
	drawPreview();
	gui.begin();
	drawControlPanel();
	gui.end();
}

void ofApp::exit() {
	if (shuttingDown) {
		return;
	}

	shuttingDown = true;
	player.close();
	gui.exit();
}

void ofApp::keyPressed(int key) {
	if (shuttingDown) {
		return;
	}

	switch (key) {
	case ' ':
		if (player.isPlaying()) {
			player.pause();
		} else {
			player.play();
		}
		break;
	case 's':
	case 'S':
		player.stop();
		break;
	case 'o':
	case 'O':
		openMediaDialog();
		break;
	case 'r':
	case 'R':
		resetViewpoint();
		break;
	case OF_KEY_LEFT:
		nudgeViewpoint(-5.0f, 0.0f, 0.0f, 0.0f);
		break;
	case OF_KEY_RIGHT:
		nudgeViewpoint(5.0f, 0.0f, 0.0f, 0.0f);
		break;
	case OF_KEY_UP:
		nudgeViewpoint(0.0f, 5.0f, 0.0f, 0.0f);
		break;
	case OF_KEY_DOWN:
		nudgeViewpoint(0.0f, -5.0f, 0.0f, 0.0f);
		break;
	case '[':
		nudgeViewpoint(0.0f, 0.0f, 0.0f, -3.0f);
		break;
	case ']':
		nudgeViewpoint(0.0f, 0.0f, 0.0f, 3.0f);
		break;
	default:
		break;
	}
}

void ofApp::dragEvent(ofDragInfo dragInfo) {
	if (dragInfo.files.empty() || shuttingDown) {
		return;
	}

	std::vector<std::string> droppedPaths;
	droppedPaths.reserve(dragInfo.files.size());
	for (const auto & droppedPath : dragInfo.files) {
		droppedPaths.push_back(std::filesystem::path(droppedPath).string());
	}

	replacePlaylistFromPaths(droppedPaths, true);
}

void ofApp::windowResized(int w, int h) {
	(void)w;
	(void)h;
}

void ofApp::drawPreview() {
	ofBackgroundGradient(ofColor(15, 19, 28), ofColor(7, 8, 11), OF_GRADIENT_CIRCULAR);

	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	const ofxVlc4::MediaReadinessInfo readiness = player.getMediaReadinessInfo();

	const float availableWidth = std::max(1.0f, static_cast<float>(ofGetWidth()) - (previewMargin * 2.0f));
	const float availableHeight = std::max(1.0f, static_cast<float>(ofGetHeight()) - (previewMargin * 2.0f));
	float sourceWidth = static_cast<float>(videoState.sourceWidth);
	float sourceHeight = static_cast<float>(videoState.sourceHeight);
	if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
		sourceWidth = 16.0f;
		sourceHeight = 9.0f;
	}

	const float scale = std::min(availableWidth / sourceWidth, availableHeight / sourceHeight);
	const float drawWidth = std::max(1.0f, sourceWidth * scale);
	const float drawHeight = std::max(1.0f, sourceHeight * scale);
	const float drawX = (static_cast<float>(ofGetWidth()) - drawWidth) * 0.5f;
	const float drawY = (static_cast<float>(ofGetHeight()) - drawHeight) * 0.5f;

	const bool hasFrame = readiness.hasReceivedVideoFrame || player.hasVideoOutput();
	if (hasFrame) {
		ofSetColor(255);
		player.draw(drawX, drawY, drawWidth, drawHeight);
	} else {
		ofPushStyle();
		ofSetColor(24, 29, 40);
		ofDrawRectangle(drawX, drawY, drawWidth, drawHeight);
		ofSetColor(52, 68, 98);
		for (float x = drawX; x < drawX + drawWidth; x += std::max(32.0f, drawWidth / 18.0f)) {
			ofDrawLine(x, drawY, x, drawY + drawHeight);
		}
		for (float y = drawY; y < drawY + drawHeight; y += std::max(32.0f, drawHeight / 10.0f)) {
			ofDrawLine(drawX, y, drawX + drawWidth, y);
		}
		ofSetColor(210);
		const std::string placeholder =
			"Drop a 360 or panoramic video here\n"
			"or use the Open button in the panel.\n\n"
			"Projection, stereo mode, and viewpoint are controlled in the ImGui UI.";
		ofDrawBitmapStringHighlight(placeholder, drawX + 24.0f, drawY + 36.0f);
		ofPopStyle();
	}

	ofNoFill();
	ofSetColor(255, 255, 255, static_cast<int>(kPreviewOutlineAlpha));
	ofDrawRectangle(drawX, drawY, drawWidth, drawHeight);
	ofFill();
}

void ofApp::drawControlPanel() {
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	const ofxVlc4::MediaReadinessInfo readiness = player.getMediaReadinessInfo();
	const ofxVlc4::PlaylistStateInfo playlistState = player.getPlaylistStateInfo();

	ImGui::SetNextWindowSize(ImVec2(430.0f, 720.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("360 Controls", nullptr, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", currentMediaLabel().c_str());
	ImGui::Text("Timecode: %s", player.formatCurrentPlaybackTimecode().c_str());
	ImGui::Text("Projection: %s", projectionLabel(videoState.projectionMode).c_str());
	ImGui::Text("Stereo: %s", stereoLabel(videoState.stereoMode).c_str());
	ImGui::Text("Source: %dx%d", videoState.sourceWidth, videoState.sourceHeight);
	ImGui::Text("Playback: %s", player.isPlaying() ? "Playing" : (player.isStopped() ? "Stopped" : "Idle / Paused"));
	ImGui::Separator();

	if (ImGui::Button("Open...", ImVec2(92.0f, 0.0f))) {
		openMediaDialog();
	}
	ImGui::SameLine();
	if (ImGui::Button(player.isPlaying() ? "Pause" : "Play", ImVec2(92.0f, 0.0f))) {
		if (player.isPlaying()) {
			player.pause();
		} else {
			player.play();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop", ImVec2(92.0f, 0.0f))) {
		player.stop();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset View", ImVec2(120.0f, 0.0f))) {
		resetViewpoint();
	}

	if (playlistState.size > 1) {
		if (ImGui::Button("Prev", ImVec2(92.0f, 0.0f))) {
			player.previousMediaListItem();
		}
		ImGui::SameLine();
		if (ImGui::Button("Next", ImVec2(92.0f, 0.0f))) {
			player.nextMediaListItem();
		}
	}

	ImGui::SeparatorText("Projection");

	int projectionIndex = 0;
	for (size_t i = 0; i < projectionModes.size(); ++i) {
		if (projectionModes[i] == videoState.projectionMode) {
			projectionIndex = static_cast<int>(i);
			break;
		}
	}
	if (ImGui::Combo("Mode", &projectionIndex, projectionModeLabels.data(), static_cast<int>(projectionModeLabels.size()))) {
		player.setVideoProjectionMode(projectionModes[static_cast<size_t>(projectionIndex)]);
	}

	int stereoIndex = 0;
	for (size_t i = 0; i < stereoModes.size(); ++i) {
		if (stereoModes[i] == videoState.stereoMode) {
			stereoIndex = static_cast<int>(i);
			break;
		}
	}
	if (ImGui::Combo("Stereo", &stereoIndex, stereoModeLabels.data(), static_cast<int>(stereoModeLabels.size()))) {
		player.setVideoStereoMode(stereoModes[static_cast<size_t>(stereoIndex)]);
	}

	ImGui::TextDisabled("Use equirectangular or cubemap for real 360 sources.");

	ImGui::SeparatorText("Viewpoint");

	float yaw = videoState.yaw;
	float pitch = videoState.pitch;
	float roll = videoState.roll;
	float fov = videoState.fov;
	if (ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ImGui::SliderFloat("Pitch", &pitch, -90.0f, 90.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ImGui::SliderFloat("Roll", &roll, -180.0f, 180.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ImGui::SliderFloat("FOV", &fov, 1.0f, 179.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}

	if (ImGui::Button("Yaw -15", ImVec2(92.0f, 0.0f))) {
		nudgeViewpoint(-15.0f, 0.0f, 0.0f, 0.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Yaw +15", ImVec2(92.0f, 0.0f))) {
		nudgeViewpoint(15.0f, 0.0f, 0.0f, 0.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Pitch +10", ImVec2(92.0f, 0.0f))) {
		nudgeViewpoint(0.0f, 10.0f, 0.0f, 0.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Pitch -10", ImVec2(92.0f, 0.0f))) {
		nudgeViewpoint(0.0f, -10.0f, 0.0f, 0.0f);
	}

	ImGui::TextDisabled("Arrow keys nudge yaw/pitch. [ and ] change FOV.");

	ImGui::SeparatorText("Diagnostics");
	ImGui::Text("Frame attached: %s", readiness.mediaAttached ? "yes" : "no");
	ImGui::Text("Prepared: %s", readiness.startupPrepared ? "yes" : "no");
	ImGui::Text("Video frame: %s", readiness.hasReceivedVideoFrame ? "yes" : "no");
	ImGui::Text("Playlist items: %d", static_cast<int>(playlistState.size));
	if (!infoStatus.empty()) {
		ImGui::Separator();
		ImGui::TextWrapped("%s", infoStatus.c_str());
	}
	if (!player.getLastErrorMessage().empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%s", player.getLastErrorMessage().c_str());
	} else if (!player.getLastStatusMessage().empty()) {
		ImGui::TextWrapped("%s", player.getLastStatusMessage().c_str());
	}

	ImGui::End();
}

void ofApp::loadSeedMedia() {
	for (const auto & candidate : defaultSeedMediaCandidates()) {
		if (std::filesystem::exists(candidate)) {
			queueStartupMediaPath(candidate.string(), true);
			return;
		}
	}
}

void ofApp::queueStartupMediaPath(const std::string & path, bool autoPlay) {
	pendingStartupMediaPath = path;
	startupMediaAutoPlay = autoPlay;
	startupMediaPending = !pendingStartupMediaPath.empty();
}

void ofApp::loadMediaPath(const std::string & path, bool autoPlay) {
	if (path.empty() || shuttingDown) {
		return;
	}

	startupMediaPending = false;
	pendingStartupMediaPath.clear();

	replacePlaylistFromPaths({ path }, autoPlay);
}

void ofApp::replacePlaylistFromPaths(const std::vector<std::string> & paths, bool autoPlay) {
	if (paths.empty() || shuttingDown) {
		return;
	}

	const std::vector<std::string> supportedPaths = collectSupportedPaths(paths);
	if (supportedPaths.empty()) {
		infoStatus = "No supported media found in the selected paths.";
		return;
	}

	player.stop();
	player.clearPlaylist();

	int addedCount = 0;
	for (const std::string & path : supportedPaths) {
		addedCount += player.addPathToPlaylist(path);
	}

	if (addedCount <= 0 || !player.hasPlaylist()) {
		infoStatus = "Failed to add media to playlist.";
		return;
	}

	const std::string firstPath = supportedPaths.front();
	if (addedCount == 1) {
		infoStatus = "Loaded: " + ofFilePath::getFileName(firstPath);
	} else {
		infoStatus = "Loaded playlist items: " + ofToString(addedCount);
	}

	resetViewpoint();
	if (autoPlay) {
		player.playIndex(0);
	}
}

std::vector<std::string> ofApp::collectSupportedPaths(const std::vector<std::string> & paths) const {
	std::vector<std::string> supportedPaths;
	supportedPaths.reserve(paths.size());
	for (const auto & rawPath : paths) {
		const std::filesystem::path path(rawPath);
		if (std::filesystem::exists(path) && isLikelySupportedMediaPath(path)) {
			supportedPaths.push_back(path.string());
		}
	}
	return supportedPaths;
}

void ofApp::openMediaDialog() {
	ofFileDialogResult result = ofSystemLoadDialog("Choose a 360 or panoramic media file");
	if (result.bSuccess) {
		loadMediaPath(result.getPath(), true);
	}
}

void ofApp::resetViewpoint() {
	player.resetVideoViewpoint();
	player.setVideoViewpoint(0.0f, 0.0f, 0.0f, kDefaultFov);
}

void ofApp::nudgeViewpoint(float deltaYaw, float deltaPitch, float deltaRoll, float deltaFov) {
	const float yaw = player.getVideoYaw() + deltaYaw;
	const float pitch = player.getVideoPitch() + deltaPitch;
	const float roll = player.getVideoRoll() + deltaRoll;
	const float fov = player.getVideoFov() + deltaFov;
	player.setVideoViewpoint(yaw, pitch, roll, fov);
}

std::string ofApp::currentMediaLabel() const {
	const auto currentItem = player.getCurrentPlaylistItemInfo();
	if (currentItem.index >= 0 && !currentItem.label.empty()) {
		return currentItem.label;
	}
	if (!player.getCurrentPath().empty()) {
		return ofFilePath::getFileName(player.getCurrentPath());
	}
	return "No media loaded";
}

std::string ofApp::projectionLabel(ofxVlc4::VideoProjectionMode mode) const {
	switch (mode) {
	case ofxVlc4::VideoProjectionMode::Rectangular:
		return "Rectangular";
	case ofxVlc4::VideoProjectionMode::Equirectangular:
		return "360 Equirectangular";
	case ofxVlc4::VideoProjectionMode::CubemapStandard:
		return "Cubemap";
	case ofxVlc4::VideoProjectionMode::Auto:
	default:
		return "Auto";
	}
}

std::string ofApp::stereoLabel(ofxVlc4::VideoStereoMode mode) const {
	switch (mode) {
	case ofxVlc4::VideoStereoMode::Stereo:
		return "Stereo";
	case ofxVlc4::VideoStereoMode::LeftEye:
		return "Left Eye";
	case ofxVlc4::VideoStereoMode::RightEye:
		return "Right Eye";
	case ofxVlc4::VideoStereoMode::SideBySide:
		return "Side By Side";
	case ofxVlc4::VideoStereoMode::Auto:
	default:
		return "Auto";
	}
}
