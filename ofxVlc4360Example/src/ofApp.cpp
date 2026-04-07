#include "ofApp.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <set>
#include <system_error>

namespace {
	constexpr float kPreviewOutlineAlpha = 34.0f;

	std::string formatSimpleWatchTimeLabel(const ofxVlc4::WatchTimeInfo & watchTime) {
		if (!watchTime.available) {
			return "--:--:--";
		}

		const int64_t timeUs = watchTime.interpolatedTimeUs >= 0 ? watchTime.interpolatedTimeUs : watchTime.timeUs;
		if (timeUs < 0) {
			return "--:--:--";
		}

		const int64_t totalSeconds = timeUs / 1000000;
		const int hours = static_cast<int>(totalSeconds / 3600);
		const int minutes = static_cast<int>((totalSeconds % 3600) / 60);
		const int seconds = static_cast<int>(totalSeconds % 60);
		std::ostringstream label;
		label << std::setfill('0')
			  << std::setw(2) << hours
			  << ":"
			  << std::setw(2) << minutes
			  << ":"
			  << std::setw(2) << seconds;
		return label.str();
	}

	std::vector<std::filesystem::path> defaultSeedMediaCandidates() {
		const std::filesystem::path dataDirectory = ofToDataPath("", true);
		const std::filesystem::path example360Directory = dataDirectory / "360";
		return {
			dataDirectory / "Crystal Shower.mp4",
			dataDirectory / "Crystal Shower.mov",
			dataDirectory / "Crystal Shower.webm",
			dataDirectory / "crystal-shower-falls-360.mp4",
			dataDirectory / "crystal-shower-falls-360.mov",
			dataDirectory / "crystal-shower-falls-360.webm",
			example360Directory / "Crystal Shower.mp4",
			example360Directory / "Crystal Shower.mov",
			example360Directory / "Crystal Shower.webm",
			example360Directory / "crystal-shower-falls-360.mp4",
			example360Directory / "crystal-shower-falls-360.mov",
			example360Directory / "crystal-shower-falls-360.webm",
			dataDirectory / "oceanside-beach-360-4k.webm",
			dataDirectory / "dji-mini-2-360-view.webm",
			example360Directory / "oceanside-beach-360-4k.webm",
			example360Directory / "dji-mini-2-360-view.webm"
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
	camera.setAutoDistance(false);
	resetCameraView();

	ofxVlc4::setLogLevel(OF_LOG_NOTICE);
	player.setAudioCaptureEnabled(false);
	player.init(0, nullptr);
	player.setWatchTimeEnabled(true);
	player.setWatchTimeMinPeriodUs(50000);
	player.setVolume(100);
	applyRenderModeBackend();

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

	if (startupSeedPending) {
		if (startupSeedDelayFrames > 0) {
			--startupSeedDelayFrames;
		} else {
			startupSeedPending = false;
			loadMediaPath(pendingSeedPath, false);
			pendingSeedPath.clear();
		}
	}

	if (renderMode == RenderMode::LibVlc360) {
		applyLibVlc360Viewpoint();
	} else if (libVlc360Applied) {
		releaseLibVlc360Viewpoint();
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
			startPlayback();
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
	case 'm':
	case 'M':
		renderMode = (renderMode == RenderMode::LibVlc360) ? RenderMode::Sphere : RenderMode::LibVlc360;
		applyRenderModeBackend();
		if (renderMode == RenderMode::Sphere) {
			releaseLibVlc360Viewpoint();
			infoStatus = "Renderer: sphere preview.";
		} else {
			libVlcViewDirty = true;
			infoStatus = "Renderer: libVLC 360.";
		}
		break;
	case '[':
		cameraFov = std::max(30.0f, cameraFov - 3.0f);
		applyCameraFov();
		libVlcViewDirty = true;
		break;
	case ']':
		cameraFov = std::min(120.0f, cameraFov + 3.0f);
		applyCameraFov();
		libVlcViewDirty = true;
		break;
	default:
		break;
	}
}

void ofApp::mousePressed(int x, int y, int button) {
	if (button != OF_MOUSE_BUTTON_LEFT) {
		return;
	}

	lastMouseX = x;
	lastMouseY = y;
	if (renderMode == RenderMode::LibVlc360) {
		libVlcMouseDragging = true;
	}
}

void ofApp::mouseDragged(int x, int y, int button) {
	if (button != OF_MOUSE_BUTTON_LEFT) {
		return;
	}

	const float deltaX = static_cast<float>(x - lastMouseX);
	const float deltaY = static_cast<float>(y - lastMouseY);
	lastMouseX = x;
	lastMouseY = y;

	if (renderMode == RenderMode::LibVlc360) {
		if (!libVlcMouseDragging) {
			return;
		}
		libVlcYaw = ofWrapDegrees(libVlcYaw + (deltaX * 0.12f));
		libVlcPitch = ofClamp(libVlcPitch + (deltaY * 0.12f), -90.0f, 90.0f);
		libVlcViewDirty = true;
	}
}

void ofApp::mouseReleased(int x, int y, int button) {
	(void)x;
	(void)y;
	if (button == OF_MOUSE_BUTTON_LEFT) {
		libVlcMouseDragging = false;
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
	const bool hasFrame = readiness.hasReceivedVideoFrame || player.hasVideoOutput();

	if (hasFrame) {
		if (renderMode == RenderMode::LibVlc360) {
			const float availableWidth = std::max(1.0f, static_cast<float>(ofGetWidth()) - (previewMargin * 2.0f));
			const float availableHeight = std::max(1.0f, static_cast<float>(ofGetHeight()) - (previewMargin * 2.0f));
			const float drawX = previewMargin;
			const float drawY = previewMargin;

			ofPushStyle();
			ofSetColor(20, 24, 32);
			ofDrawRectangle(drawX, drawY, availableWidth, availableHeight);
			ofSetColor(255, 255, 255, static_cast<int>(kPreviewOutlineAlpha));
			ofNoFill();
			ofDrawRectangle(drawX, drawY, availableWidth, availableHeight);
			ofFill();
			ofSetColor(220);
			const std::string nativeWindowMessage =
				"libVLC 360 is running in the separate native video window.\n\n"
				"Use this panel to control playback and drag in the native window\n"
				"to test libVLC's own panoramic viewpoint rendering.";
			ofDrawBitmapStringHighlight(nativeWindowMessage, drawX + 24.0f, drawY + 36.0f);
			ofPopStyle();
		} else {
			ofTexture & texture = player.getTexture();
			if (!texture.isAllocated()) {
				return;
			}

			if (texture.getWidth() != mappedTextureWidth || texture.getHeight() != mappedTextureHeight) {
				rebuildSphereTexCoords(texture);
				mappedTextureWidth = texture.getWidth();
				mappedTextureHeight = texture.getHeight();
			}

			camera.begin();
			ofPushMatrix();
			ofRotateZDeg(180.0f);
			ofRotateYDeg(180.0f);
			ofSetColor(255);
			texture.bind();
			sphere.draw();
			texture.unbind();
			ofPopMatrix();
			camera.end();
		}
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
	ofDrawBitmapStringHighlight("Time: " + formatSimpleWatchTimeLabel(player.getWatchTimeInfo()), 18.0f, 44.0f);
	ofDrawBitmapStringHighlight("Source: " + ofToString(videoState.sourceWidth) + "x" + ofToString(videoState.sourceHeight), 18.0f, 66.0f);
}

void ofApp::drawControlPanel() {
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	const ofxVlc4::MediaReadinessInfo readiness = player.getMediaReadinessInfo();
	const ofxVlc4::PlaylistStateInfo playlistState = player.getPlaylistStateInfo();

	const ImVec2 controlPanelSize(430.0f, 620.0f);
	ImGui::SetNextWindowSize(controlPanelSize, ImGuiCond_Always);
	if (!ImGui::Begin("360 Controls", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", currentMediaLabel().c_str());
	ImGui::Text("Playback: %s", player.isPlaying() ? "Playing" : (player.isStopped() ? "Stopped" : "Idle / Paused"));
	ImGui::Text("Time: %s", formatSimpleWatchTimeLabel(player.getWatchTimeInfo()).c_str());
	ImGui::Text("Source: %dx%d", videoState.sourceWidth, videoState.sourceHeight);
	ImGui::Separator();

	static const char * renderModeLabels[] = { "libVLC 360", "Sphere" };
	int renderModeIndex = renderMode == RenderMode::LibVlc360 ? 0 : 1;
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::Combo("Renderer", &renderModeIndex, renderModeLabels, IM_ARRAYSIZE(renderModeLabels))) {
		renderMode = renderModeIndex == 0 ? RenderMode::LibVlc360 : RenderMode::Sphere;
		applyRenderModeBackend();
		if (renderMode == RenderMode::Sphere) {
			releaseLibVlc360Viewpoint();
			infoStatus = "Renderer: sphere preview.";
		} else {
			libVlcViewDirty = true;
			infoStatus = "Renderer: libVLC 360.";
		}
	}

	if (renderMode == RenderMode::Sphere) {
		ImGui::TextDisabled("Sphere mode uses a flipped-Y mono mapping by default.");
	}

	if (ImGui::Button("Open...", ImVec2(92.0f, 0.0f))) {
		openMediaDialog();
	}
	ImGui::SameLine();
	if (ImGui::Button(player.isPlaying() ? "Pause" : "Play", ImVec2(92.0f, 0.0f))) {
		if (player.isPlaying()) {
			player.pause();
		} else {
			startPlayback();
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
		libVlcViewDirty = true;
	}
	if (renderMode == RenderMode::LibVlc360) {
		ImGui::TextDisabled("Drag with the mouse to update the libVLC 360 viewpoint.");
		ImGui::TextDisabled("Use R to reset view. [ and ] adjust viewpoint FOV.");
	} else {
		ImGui::TextDisabled("Drag with the mouse to look around inside the sphere.");
		ImGui::TextDisabled("Use R to reset view. [ and ] adjust FOV.");
		ImGui::TextDisabled("Sphere mode assumes standard mono 2:1 equirectangular video.");
		ImGui::TextDisabled("The default mapping uses a flipped Y orientation.");
	}

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
			pendingSeedPath = candidate.string();
			startupSeedPending = true;
			startupSeedDelayFrames = 2;
			infoStatus = "Startup seed queued: " + candidate.filename().string();
			return;
		}
	}

	infoStatus = "No bundled 360 sample found. Add Crystal Shower.mp4 to bin/data, use the downloader script, or open your own panoramic video.";
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
	libVlc360Applied = false;
	libVlcViewDirty = renderMode == RenderMode::LibVlc360;

	int addedCount = 0;
	for (const std::string & path : supportedPaths) {
		addedCount += player.addPathToPlaylist(path);
	}

	if (addedCount <= 0 || !player.hasPlaylist()) {
		infoStatus = "Failed to add media to playlist.";
		return;
	}

	resetCameraView();
	mappedTextureWidth = 0.0f;
	mappedTextureHeight = 0.0f;
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
	camera.setTarget(glm::vec3(1.0f, 0.0f, 0.0f));
	camera.setNearClip(1.0f);
	camera.setFarClip(sphereRadius * 4.0f);
	applyCameraFov();
	if (renderMode == RenderMode::Sphere) {
		camera.enableMouseInput();
	} else {
		camera.disableMouseInput();
	}
	libVlcYaw = 0.0f;
	libVlcPitch = 0.0f;
	libVlcRoll = 0.0f;
	libVlcViewDirty = renderMode == RenderMode::LibVlc360;
}

void ofApp::applyCameraFov() {
	camera.setFov(cameraFov);
}

void ofApp::applyRenderModeBackend() {
	if (renderMode == RenderMode::LibVlc360) {
		camera.disableMouseInput();
		player.setVideoOutputBackend(ofxVlc4::VideoOutputBackend::NativeWindow);
	} else {
		camera.enableMouseInput();
		player.setVideoOutputBackend(ofxVlc4::VideoOutputBackend::Texture);
	}
}

void ofApp::applyLibVlc360Viewpoint(bool force) {
	const ofxVlc4::MediaReadinessInfo readiness = player.getMediaReadinessInfo();
	const bool canApply = player.isPlaying() && (readiness.mediaAttached || readiness.hasReceivedVideoFrame || player.hasVideoOutput());
	if (!canApply) {
		return;
	}

	if (!libVlc360Applied || force) {
		player.setVideoProjectionMode(ofxVlc4::VideoProjectionMode::Equirectangular);
		player.setVideoStereoMode(ofxVlc4::VideoStereoMode::Auto);
		libVlc360Applied = true;
		libVlcViewDirty = true;
	}

	if (!libVlcViewDirty && !force) {
		return;
	}

	player.setVideoViewpoint(libVlcYaw, libVlcPitch, libVlcRoll, cameraFov, true);
	libVlcViewDirty = false;
}

void ofApp::releaseLibVlc360Viewpoint() {
	player.setVideoProjectionMode(ofxVlc4::VideoProjectionMode::Auto);
	player.setVideoStereoMode(ofxVlc4::VideoStereoMode::Auto);
	player.resetVideoViewpoint();
	libVlc360Applied = false;
	libVlcViewDirty = false;
}

void ofApp::rebuildSphereTexCoords(const ofTexture & texture) {
	ofMesh & mesh = sphere.getMesh();
	const auto & vertices = mesh.getVertices();
	std::vector<glm::vec2> texCoords;
	texCoords.reserve(vertices.size());

	const float twoPi = glm::two_pi<float>();

	for (const glm::vec3 & vertex : vertices) {
		const glm::vec3 direction = glm::normalize(vertex);
		float u = 0.5f + std::atan2(direction.z, direction.x) / twoPi;
		if (u < 0.0f) {
			u += 1.0f;
		}
		u = 1.0f - u;
		float v = 0.5f - (std::asin(ofClamp(direction.y, -1.0f, 1.0f)) / PI);
		v = 1.0f - v;
		texCoords.emplace_back(texture.getCoordFromPercent(ofClamp(u, 0.0f, 1.0f), ofClamp(v, 0.0f, 1.0f)));
	}

	mesh.clearTexCoords();
	mesh.addTexCoords(texCoords);
}

void ofApp::startPlayback() {
	const auto playlistState = player.getPlaylistStateInfo();
	const auto readiness = player.getMediaReadinessInfo();
	if (playlistState.size > 0 && !readiness.mediaAttached) {
		const int index = playlistState.currentIndex >= 0 ? playlistState.currentIndex : 0;
		player.playIndex(index);
		return;
	}

	player.play();
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
