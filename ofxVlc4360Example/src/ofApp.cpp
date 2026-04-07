#include "ofApp.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

namespace {
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
	ofDisableArbTex();
	ofSetWindowTitle("ofxVlc4360Example");
	ofSetFrameRate(60);
	ofBackground(10, 12, 16);

	sphere.setRadius(sphereRadius);
	sphere.setResolution(64);
	resetCameraView();

	ofxVlc4::setLogLevel(OF_LOG_NOTICE);
	player.setAudioCaptureEnabled(false);
	player.init(0, nullptr);
	player.setWatchTimeEnabled(true);
	player.setWatchTimeMinPeriodUs(50000);
	player.setVolume(100);

	gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
	ImGui::GetIO().IniFilename = "imgui_360.ini";

	infoStatus = "Open or drop a panoramic video, then drag the mouse to look around.";
	loadSeedMedia();
}

void ofApp::update() {
	if (shuttingDown) {
		return;
	}

	player.update();
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
		resetCameraView();
		break;
	case '[':
		cameraFov = std::max(30.0f, cameraFov - 3.0f);
		applyCameraFov();
		break;
	case ']':
		cameraFov = std::min(120.0f, cameraFov + 3.0f);
		applyCameraFov();
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
	const bool hasFrame = (readiness.hasReceivedVideoFrame || player.hasVideoOutput()) &&
		player.getTexture().isAllocated();

	if (hasFrame) {
		ofEnableDepthTest();
		camera.begin();
		ofPushMatrix();
		ofScale(-1.0f, 1.0f, 1.0f);
		ofSetColor(255);
		player.getTexture().bind();
		sphere.draw();
		player.getTexture().unbind();
		ofPopMatrix();
		camera.end();
		ofDisableDepthTest();
	} else {
		const float availableWidth = std::max(1.0f, static_cast<float>(ofGetWidth()) - (previewMargin * 2.0f));
		const float availableHeight = std::max(1.0f, static_cast<float>(ofGetHeight()) - (previewMargin * 2.0f));
		const float drawWidth = availableWidth;
		const float drawHeight = availableHeight;
		const float drawX = (static_cast<float>(ofGetWidth()) - drawWidth) * 0.5f;
		const float drawY = (static_cast<float>(ofGetHeight()) - drawHeight) * 0.5f;

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
			"Drop a panoramic video here\n"
			"or use Open in the ImGui panel.\n\n"
			"Play media, then drag the mouse to look around.\n"
			"Use [ and ] to change the camera FOV.";
		ofDrawBitmapStringHighlight(placeholder, drawX + 24.0f, drawY + 36.0f);
		ofSetColor(255, 255, 255, static_cast<int>(kPreviewOutlineAlpha));
		ofNoFill();
		ofDrawRectangle(drawX, drawY, drawWidth, drawHeight);
		ofFill();
		ofPopStyle();
	}

	ofSetColor(255);
	ofDrawBitmapStringHighlight("FPS: " + ofToString(ofGetFrameRate(), 1), 18.0f, 22.0f);
	ofDrawBitmapStringHighlight("Timecode: " + player.formatCurrentPlaybackTimecode(), 18.0f, 44.0f);
	ofDrawBitmapStringHighlight("Source: " + ofToString(videoState.sourceWidth) + "x" + ofToString(videoState.sourceHeight), 18.0f, 66.0f);
}

void ofApp::drawControlPanel() {
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	const ofxVlc4::MediaReadinessInfo readiness = player.getMediaReadinessInfo();
	const ofxVlc4::PlaylistStateInfo playlistState = player.getPlaylistStateInfo();

	ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("360 Controls", nullptr, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", currentMediaLabel().c_str());
	ImGui::Text("Playback: %s", player.isPlaying() ? "Playing" : (player.isStopped() ? "Stopped" : "Idle / Paused"));
	ImGui::Text("Timecode: %s", player.formatCurrentPlaybackTimecode().c_str());
	ImGui::Text("Source: %dx%d", videoState.sourceWidth, videoState.sourceHeight);
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
		resetCameraView();
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

	ImGui::SeparatorText("Camera");
	if (ImGui::SliderFloat("FOV", &cameraFov, 30.0f, 120.0f, "%.0f deg")) {
		applyCameraFov();
	}
	ImGui::TextDisabled("Drag with the mouse to look around inside the sphere.");
	ImGui::TextDisabled("Use R to reset view. [ and ] adjust FOV.");

	ImGui::SeparatorText("Diagnostics");
	ImGui::Text("Frame attached: %s", readiness.mediaAttached ? "yes" : "no");
	ImGui::Text("Prepared: %s", readiness.startupPrepared ? "yes" : "no");
	ImGui::Text("Video frame: %s", readiness.hasReceivedVideoFrame ? "yes" : "no");
	ImGui::Text("Video output: %s", player.hasVideoOutput() ? "yes" : "no");
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
		std::error_code error;
		if (std::filesystem::exists(candidate, error) && !error) {
			loadMediaPath(candidate.string(), false);
			return;
		}
	}
}

void ofApp::loadMediaPath(const std::string & path, bool autoPlay) {
	if (path.empty() || shuttingDown) {
		return;
	}

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

	resetCameraView();
	const std::string firstPath = supportedPaths.front();
	if (addedCount == 1) {
		infoStatus = "Loaded: " + ofFilePath::getFileName(firstPath);
	} else {
		infoStatus = "Loaded playlist items: " + ofToString(addedCount);
	}

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
	ofFileDialogResult result = ofSystemLoadDialog("Choose a panoramic media file");
	if (result.bSuccess) {
		loadMediaPath(result.getPath(), true);
	}
}

void ofApp::resetCameraView() {
	camera.reset();
	camera.setAutoDistance(false);
	camera.setPosition(0.0f, 0.0f, 0.0f);
	camera.lookAt(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.setNearClip(0.05f);
	camera.setFarClip(sphereRadius * 4.0f);
	applyCameraFov();
}

void ofApp::applyCameraFov() {
	camera.setFov(cameraFov);
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
