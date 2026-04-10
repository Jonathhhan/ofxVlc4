#include "ofApp.h"

#include <filesystem>

namespace {
const std::initializer_list<std::string> kSeedExtensions = {
	".mp4", ".mov", ".m4v", ".webm", ".avi", ".mkv",
	".jpg", ".jpeg", ".png", ".mp3", ".wav", ".aiff", ".mid", ".midi", ".h264",
	".flac", ".bmp"
};
const std::initializer_list<std::string> kSubtitleExtensions = {
	".srt", ".ass", ".ssa", ".vtt", ".sub", ".idx"
};
constexpr float kMaxVideoPreviewHeight = 4320.0f;
constexpr int kCustomSubtitleFontSize = 36;
constexpr float kCustomSubtitleBottomMargin = 54.0f;
constexpr float kCustomSubtitleShadowOffset = 2.0f;

struct CustomSubtitleFontCandidate {
	const char * label;
	const char * path;
};

const CustomSubtitleFontCandidate kCustomSubtitleFontCandidates[] = {
	{ "Arial", "C:\\Windows\\Fonts\\arial.ttf" },
	{ "Calibri", "C:\\Windows\\Fonts\\calibri.ttf" },
	{ "Georgia", "C:\\Windows\\Fonts\\georgia.ttf" },
	{ "Times New Roman", "C:\\Windows\\Fonts\\times.ttf" },
	{ "Trebuchet MS", "C:\\Windows\\Fonts\\trebuc.ttf" }
};

std::string normalizeInputPath(std::string path) {
	path = ofTrim(path);
	if (path.size() >= 2) {
		const char first = path.front();
		const char last = path.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			path = path.substr(1, path.size() - 2);
			path = ofTrim(path);
		}
	}
	return path;
}

bool looksLikeUri(const std::string & path) {
	return path.find("://") != std::string::npos;
}

std::vector<std::string> exampleShaderCandidatePaths(const std::string & fileName) {
	const std::string exeDir = ofFilePath::getCurrentExeDir();
	const std::string dataDir = ofToDataPath("", true);
	const std::string workingDir = ofFilePath::getCurrentWorkingDirectory();
	return {
		ofFilePath::getAbsolutePath(ofFilePath::join(dataDir, ofFilePath::join("shaders", fileName)), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(exeDir, ofFilePath::join("../../src/video/shaders", fileName)), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(exeDir, ofFilePath::join("../src/video/shaders", fileName)), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(workingDir, ofFilePath::join("src/video/shaders", fileName)), true),
	};
}

std::string resolveExampleShaderPath(const std::string & fileName) {
	const std::vector<std::string> candidatePaths = exampleShaderCandidatePaths(fileName);
	for (const std::string & candidatePath : candidatePaths) {
		if (ofFile::doesFileExist(candidatePath, true)) {
			return candidatePath;
		}
	}
	return {};
}

bool isSubtitlePath(const std::string & path) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(path));
	if (extension.empty()) {
		return false;
	}

	const std::string dottedExtension = "." + extension;
	return std::find(kSubtitleExtensions.begin(), kSubtitleExtensions.end(), dottedExtension) != kSubtitleExtensions.end();
}

bool isSrtPath(const std::string & path) {
	return ofToLower(ofFilePath::getFileExt(path)) == "srt";
}

std::string resolveInputPath(const std::string & rawPath);

std::string decodeUrlComponent(const std::string & value) {
	std::string decoded;
	decoded.reserve(value.size());
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '%' && i + 2 < value.size()) {
			const char hi = value[i + 1];
			const char lo = value[i + 2];
			const auto hexToInt = [](char c) -> int {
				if (c >= '0' && c <= '9') {
					return c - '0';
				}
				if (c >= 'a' && c <= 'f') {
					return 10 + (c - 'a');
				}
				if (c >= 'A' && c <= 'F') {
					return 10 + (c - 'A');
				}
				return -1;
			};
			const int hiValue = hexToInt(hi);
			const int loValue = hexToInt(lo);
			if (hiValue >= 0 && loValue >= 0) {
				decoded.push_back(static_cast<char>((hiValue << 4) | loValue));
				i += 2;
				continue;
			}
		}
		decoded.push_back(value[i]);
	}
	return decoded;
}

std::string findMetadataValue(
	const std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & label) {
	for (const auto & [entryLabel, entryValue] : metadata) {
		if (entryLabel == label) {
			return entryValue;
		}
	}
	return "";
}

bool mediaHasVideoTrack(const std::vector<std::pair<std::string, std::string>> & metadata) {
	return !findMetadataValue(metadata, "Video Codec").empty() ||
		!findMetadataValue(metadata, "Video Resolution").empty();
}

bool hasUsableTextureSize(const ofTexture & texture) {
	return texture.isAllocated() &&
		texture.getWidth() > 1.0f &&
		texture.getHeight() > 1.0f;
}

std::vector<std::string> defaultProjectMTextureSearchPaths() {
	std::vector<std::string> paths;
	const auto addIfDirectoryExists = [&paths](const std::string & rawPath) {
		if (!ofDirectory::doesDirectoryExist(rawPath, true)) {
			return;
		}
		const std::string absolutePath = ofFilePath::getAbsolutePath(rawPath, true);
		if (std::find(paths.begin(), paths.end(), absolutePath) == paths.end()) {
			paths.push_back(absolutePath);
		}
	};

	// Primary locations used by the download-projectm-assets.sh script.
	addIfDirectoryExists(ofToDataPath("textures/textures", true));
	addIfDirectoryExists(ofToDataPath("textures", true));
	// VLC's internal projectM module looks for textures inside the preset directory, so the
	// download script also mirrors the pack to bin/data/presets/textures/.  Include that path
	// here so the standalone ofxProjectM component benefits from the same texture set when
	// VLC's location is used as the single install target.
	addIfDirectoryExists(ofToDataPath("presets/textures", true));
	return paths;
}

void clampVideoPreviewDimensions(float & width, float & height) {
	if (width <= 0.0f || height <= 0.0f || height <= kMaxVideoPreviewHeight) {
		return;
	}

	const float scale = kMaxVideoPreviewHeight / height;
	width = std::max(1.0f, width * scale);
	height = kMaxVideoPreviewHeight;
}

void assignClampedPreviewDimensions(
	float sourceWidth,
	float sourceHeight,
	float & targetWidth,
	float & targetHeight) {
	targetWidth = sourceWidth;
	targetHeight = sourceHeight;
	clampVideoPreviewDimensions(targetWidth, targetHeight);
}

bool shouldUseAnaglyphPreview(
	bool previewHasContent,
	bool previewShowsVideo,
	bool anaglyphEnabled,
	ofxVlc4::VideoStereoMode stereoMode,
	bool shaderReady,
	const ofFbo & previewFbo) {
	return previewHasContent &&
		previewShowsVideo &&
		anaglyphEnabled &&
		stereoMode == ofxVlc4::VideoStereoMode::SideBySide &&
		shaderReady &&
		previewFbo.isAllocated();
}

std::string resolveArtworkPath(const std::string & rawArtworkUrl) {
	std::string artworkPath = ofTrim(rawArtworkUrl);
	if (artworkPath.empty()) {
		return "";
	}

	const std::string lowerPath = ofToLower(artworkPath);
	if (ofIsStringInString(lowerPath, "attachment://")) {
		return "";
	}

	if (lowerPath.rfind("file:///", 0) == 0) {
		artworkPath = decodeUrlComponent(artworkPath.substr(8));
	} else if (lowerPath.rfind("file://", 0) == 0) {
		artworkPath = decodeUrlComponent(artworkPath.substr(7));
	} else {
		artworkPath = decodeUrlComponent(artworkPath);
	}

	return resolveInputPath(artworkPath);
}

bool pathExists(const std::string & path) {
	return ofFile::doesFileExist(path, true) || ofDirectory::doesDirectoryExist(path, true);
}

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
		sharedMoviesDirectory / "fingers.mp4"
	};
}

bool addFirstExistingSeedMedia(ofxVlc4 & player, const std::vector<std::filesystem::path> & candidatePaths) {
	for (const std::filesystem::path & candidatePath : candidatePaths) {
		const std::string resolvedPath = candidatePath.lexically_normal().string();
		if (!ofFile::doesFileExist(resolvedPath, true)) {
			continue;
		}
		if (player.addPathToPlaylist(resolvedPath, kSeedExtensions) > 0) {
			return true;
		}
	}
	return false;
}

bool isSupportedCustomImagePath(const std::string & path) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(path));
	return extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "bmp";
}

bool isSupportedVideoPath(const std::string & path) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(path));
	return extension == "mp4" || extension == "mov" || extension == "m4v" || extension == "webm" ||
		extension == "avi" || extension == "mkv" || extension == "h264";
}

std::string resolveInputPath(const std::string & rawPath) {
	const std::string normalizedPath = normalizeInputPath(rawPath);
	if (normalizedPath.empty() || looksLikeUri(normalizedPath)) {
		return normalizedPath;
	}

	// Prefer explicit absolute/relative filesystem paths first, then fall back to OF's data folder lookup.
	if (pathExists(normalizedPath)) {
		return normalizedPath;
	}

	const std::string dataPath = ofToDataPath(normalizedPath, true);
	if (pathExists(dataPath)) {
		return dataPath;
	}

	return normalizedPath;
}

void clearAllocatedFbo(ofFbo & fbo) {
	if (!fbo.isAllocated()) {
		return;
	}

	fbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.end();
}

void resetPreviewFbo(ofFbo & fbo) {
	fbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(fbo);
}

void clearVideoPreviewState(
	ofFbo & fbo,
	ofImage & artworkImage,
	std::string & artworkPath,
	float & previewWidth,
	float & previewHeight) {
	resetPreviewFbo(fbo);
	artworkImage.clear();
	artworkPath.clear();
	previewWidth = 0.0f;
	previewHeight = 0.0f;
}

void drawProjectMSourceImageToFbo(ofFbo & targetFbo, const ofImage & image) {
	if (!targetFbo.isAllocated()) {
		return;
	}

	targetFbo.begin();
	ofClear(0, 0, 0, 0);
	if (image.isAllocated()) {
		image.draw(0.0f, 0.0f, targetFbo.getWidth(), targetFbo.getHeight());
	}
	targetFbo.end();
}

void ensureFboSize(ofFbo & fbo, float width, float height) {
	const int targetWidth = std::max(1, static_cast<int>(std::ceil(width)));
	const int targetHeight = std::max(1, static_cast<int>(std::ceil(height)));
	if (!fbo.isAllocated() ||
		static_cast<int>(fbo.getWidth()) != targetWidth ||
		static_cast<int>(fbo.getHeight()) != targetHeight) {
		fbo.allocate(targetWidth, targetHeight, GL_RGBA);
		clearAllocatedFbo(fbo);
	}
}

void restartCurrentProjectMPresetIfInitialized(ofxProjectM & projectM, bool projectMInitialized) {
	if (projectMInitialized) {
		projectM.restartPreset(true);
	}
}

bool ensureLoadedImage(ofImage & image, const std::string & path) {
	if (image.isAllocated()) {
		return true;
	}
	if (path.empty()) {
		return false;
	}

	image.clear();
	if (!looksLikeUri(path) && ofFile::doesFileExist(path, true)) {
		ofBuffer buffer = ofBufferFromFile(path, true);
		return !buffer.size() ? false : image.load(buffer);
	}

	return image.load(path);
}

void drawPlaybackStateOverlay(const ofxVlc4::MediaReadinessInfo & readiness) {
	const float padding = 10.0f;
	const float lineHeight = 16.0f;
	const float boxWidth = 220.0f;
	const float boxHeight = padding * 2.0f + lineHeight * 6.0f;
	const float boxX = ofGetWidth() - boxWidth - 16.0f;
	const float boxY = 16.0f;
	const auto flagLabel = [](bool value) {
		return value ? "yes" : "no";
	};

	ofPushStyle();
	ofSetColor(0, 0, 0, 185);
	ofDrawRectangle(boxX, boxY, boxWidth, boxHeight);
	ofNoFill();
	ofSetColor(110, 110, 110, 220);
	ofDrawRectangle(boxX, boxY, boxWidth, boxHeight);
	ofFill();
	ofSetColor(235);
	ofDrawBitmapString("Playback State (F9)", boxX + padding, boxY + padding + 11.0f);
	ofDrawBitmapString("attached:  " + std::string(flagLabel(readiness.mediaAttached)), boxX + padding, boxY + padding + lineHeight * 2.0f);
	ofDrawBitmapString("prepared:  " + std::string(flagLabel(readiness.startupPrepared)), boxX + padding, boxY + padding + lineHeight * 3.0f);
	ofDrawBitmapString("geometry:  " + std::string(flagLabel(readiness.geometryKnown)), boxX + padding, boxY + padding + lineHeight * 4.0f);
	ofDrawBitmapString("frame:     " + std::string(flagLabel(readiness.hasReceivedVideoFrame)), boxX + padding, boxY + padding + lineHeight * 5.0f);
	ofDrawBitmapString("playing:   " + std::string(flagLabel(readiness.playbackActive)), boxX + padding, boxY + padding + lineHeight * 6.0f);
	ofPopStyle();
}

std::pair<glm::vec3, glm::vec3> getAnaglyphTints(AnaglyphColorMode mode) {
	switch (mode) {
	case AnaglyphColorMode::GreenMagenta:
		return {
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(1.0f, 0.0f, 1.0f)
		};
	case AnaglyphColorMode::AmberBlue:
		return {
			glm::vec3(1.0f, 0.75f, 0.0f),
			glm::vec3(0.0f, 0.5f, 1.0f)
		};
	case AnaglyphColorMode::RedCyan:
	default:
		return {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 1.0f)
		};
	}
}

bool loadShaderProgram(ofShader & shader, const std::string & fragmentBaseName) {
	const bool programmable = ofIsGLProgrammableRenderer();
	const std::string vertexFileName = programmable ? "passthrough_gl3.vert" : "passthrough_gl2.vert";
	const std::string fragmentFileName = programmable
		? (fragmentBaseName + "_gl3.frag")
		: (fragmentBaseName + "_gl2.frag");
	const std::string vertexPath = resolveExampleShaderPath(vertexFileName);
	const std::string fragmentPath = resolveExampleShaderPath(fragmentFileName);
	if (vertexPath.empty() || fragmentPath.empty()) {
		ofLogError("ofxVlc4") << "Missing example shader(s): "
			<< (vertexPath.empty() ? vertexFileName : "")
			<< ((vertexPath.empty() && fragmentPath.empty()) ? ", " : "")
			<< (fragmentPath.empty() ? fragmentFileName : "");
		return false;
	}

	bool ready =
		shader.setupShaderFromFile(GL_VERTEX_SHADER, vertexPath) &&
		shader.setupShaderFromFile(GL_FRAGMENT_SHADER, fragmentPath);
	if (ready && programmable) {
		shader.bindDefaults();
	}
	if (ready) {
		ready = shader.linkProgram();
	}
	return ready;
}
}

void ofApp::setupAnaglyphShader() {
	// The example keeps anaglyph rendering local: VLC still produces the stereo frame,
	// and this shader only remaps an SBS preview into a simple red/cyan display.
	anaglyphShaderReady = loadShaderProgram(anaglyphShader, "anaglyph");
}

void ofApp::updateAnaglyphPreview(const ofTexture & sourceTexture, float sourceWidth, float sourceHeight, const AnaglyphSettings & anaglyphSettings) {
	if (!anaglyphShaderReady || !sourceTexture.isAllocated() || sourceWidth <= 1.0f || sourceHeight <= 1.0f) {
		return;
	}

	clampVideoPreviewDimensions(sourceWidth, sourceHeight);
	// Anaglyph combines the left and right halves of the SBS preview into a single output,
	// so the preview width is halved while the original height stays untouched.
	const float targetWidth = std::max(1.0f, sourceWidth * 0.5f);
	ensureFboSize(anaglyphPreviewFbo, targetWidth, sourceHeight);
	const auto [leftTint, rightTint] = getAnaglyphTints(anaglyphSettings.colorMode);

	anaglyphPreviewFbo.begin();
	ofClear(0, 0, 0, 0);
	anaglyphShader.begin();
	anaglyphShader.setUniformTexture("tex0", sourceTexture, 0);
	anaglyphShader.setUniform3f("leftTint", leftTint.x, leftTint.y, leftTint.z);
	anaglyphShader.setUniform3f("rightTint", rightTint.x, rightTint.y, rightTint.z);
	anaglyphShader.setUniform1f("eyeSeparation", anaglyphSettings.eyeSeparation);
	anaglyphShader.setUniform1f("swapEyes", anaglyphSettings.swapEyes ? 1.0f : 0.0f);
	sourceTexture.draw(0.0f, 0.0f, targetWidth, sourceHeight);
	anaglyphShader.end();
	anaglyphPreviewFbo.end();
}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetWindowTitle("ofxVlc4");
	ofSetFrameRate(60);
	ofSetVerticalSync(true);
	ofDisableArbTex();
	ofSetLogLevel("ofxVlc4", OF_LOG_NOTICE);

	ofSoundStreamSettings settings;
	settings.setOutListener(this);

	settings.sampleRate = 44100;
	settings.numOutputChannels = outChannels;
	settings.numInputChannels = 0;
	settings.bufferSize = bufferSize;
	soundStream.setup(settings);
	soundStream.start();

	remoteGui.setup();
	setupCustomSubtitleFonts();
	setupAnaglyphShader();
	projectMSourceFbo.allocate(std::max(ofGetScreenWidth(), 1), std::max(ofGetScreenHeight(), 1), GL_RGBA);
	clearAllocatedFbo(projectMSourceFbo);
	if (projectM.getTextureSearchPaths().empty()) {
		projectM.setTextureSearchPaths(defaultProjectMTextureSearchPaths());
	}
	resetPreviewFbo(videoPreviewFbo);
	ofxVlc4::setLogLevel(OF_LOG_NOTICE);
	initializePlayer();
}

void ofApp::initializePlayer(
	const std::vector<std::string> * playlistOverride,
	int restoreIndex,
	RestorePlaybackState restorePlaybackState,
	int restoreTimeMs,
	int restoreVolume,
	ofxVlc4::PlaybackMode restoreMode) {
	// Visualizer changes affect pre-init configuration, so reapply them from a
	// fully closed player state before creating the next VLC instance.
	player.close();

	const char * vlc_argv[] = {
		"--file-caching=0",
		"--network-caching=0",
		"--verbose=-1"
	};
	const int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);

	ofxVlc4AudioVisualizerSettings visualizerSettings = player.getAudioVisualizerSettings();
	if (visualizerSettings.module == ofxVlc4AudioVisualizerModule::ProjectM &&
		visualizerSettings.projectMPresetPath.empty()) {
		const std::string defaultPresetPath = ofToDataPath("presets", true);
		if (ofDirectory::doesDirectoryExist(defaultPresetPath, true)) {
			visualizerSettings.projectMPresetPath = defaultPresetPath;
			player.setAudioVisualizerSettings(visualizerSettings);
		}
	}

	player.setAudioCaptureEnabled(visualizerSettings.module == ofxVlc4AudioVisualizerModule::None);
	player.init(vlc_argc, vlc_argv);
	player.setWatchTimeEnabled(true);
	player.setWatchTimeMinPeriodUs(50000);
	player.setPlaybackMode(restoreMode);
	player.setVolume(restoreVolume);
	player.clearPlaylist();

	if (playlistOverride && !playlistOverride->empty()) {
		const bool shouldResumePlayback = restorePlaybackState == RestorePlaybackState::Playing;
		const bool hasActiveLibVlcVisualizer =
			visualizerSettings.module != ofxVlc4AudioVisualizerModule::None;
		for (const std::string & item : *playlistOverride) {
			if (!item.empty()) {
				player.addToPlaylist(item);
			}
		}
		if (restoreIndex >= 0 && restoreIndex < static_cast<int>(playlistOverride->size())) {
			player.activatePlaylistIndex(restoreIndex, shouldResumePlayback);
			// libVLC visualizers are playback-driven and can crash if we seek them
			// before the restarted instance has actually begun playback.
			if (restoreTimeMs > 0 && (shouldResumePlayback || !hasActiveLibVlcVisualizer)) {
				player.setTime(restoreTimeMs);
			}
		}
	} else {
		addFirstExistingSeedMedia(player, defaultSeedMediaCandidates());
	}
}

void ofApp::applyAudioVisualizerSettings() {
	const std::vector<std::string> playlist = player.getPlaylist();
	const int currentIndex = player.getCurrentIndex();
	RestorePlaybackState restorePlaybackState = RestorePlaybackState::Stopped;
	if (player.isPlaying()) {
		restorePlaybackState = RestorePlaybackState::Playing;
	} else if (!player.isStopped()) {
		restorePlaybackState = RestorePlaybackState::Paused;
	}
	const int restoreTimeMs = std::max(0, player.getTime());
	const int restoreVolume = player.getVolume();
	const ofxVlc4::PlaybackMode restoreMode = player.getPlaybackMode();
	initializePlayer(&playlist, currentIndex, restorePlaybackState, restoreTimeMs, restoreVolume, restoreMode);
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer & buffer) {
	buffer.set(0);

	const ofxVlc4::AudioStateInfo audioState = player.getAudioStateInfo();
	if (audioState.ready) {
		player.readAudioIntoBuffer(buffer, 1.0f);

		if (projectMInitialized && !buffer.getBuffer().empty()) {
			projectM.audio(
				buffer.getBuffer().data(),
				static_cast<int>(buffer.getNumFrames()),
				static_cast<int>(buffer.getNumChannels()));
		}
	}
}

void ofApp::update() {
	player.update();
	ensureProjectMInitialized();
	const ofxVlc4::MediaReadinessInfo readiness = player.getMediaReadinessInfo();
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	const bool renderProjectMPreview = remoteGui.shouldRenderProjectMPreview();
	const ofTexture & playerTexture = player.getTexture();
	const float currentVideoWidth = static_cast<float>(videoState.sourceWidth);
	const float currentVideoHeight = static_cast<float>(videoState.sourceHeight);
	const bool playerHasReportedVideoSize = readiness.geometryKnown ||
		(currentVideoWidth > 1.0f && currentVideoHeight > 1.0f);
	const bool playerHasTextureSize = hasUsableTextureSize(playerTexture);
	const auto currentMetadata = player.getCurrentMetadata();
	const bool metadataReadyForPreviewDecision = !currentMetadata.empty();
	const bool playerHasVideoFrame =
		readiness.hasReceivedVideoFrame &&
		(!metadataReadyForPreviewDecision || mediaHasVideoTrack(currentMetadata));
	const bool playbackTransitioning = player.isPlaybackTransitioning();
	const bool playbackRestartPending = player.isPlaybackRestartPending();
	const bool holdPreviewState = playbackTransitioning || playbackRestartPending;
	const std::string artworkPath = resolveArtworkPath(findMetadataValue(currentMetadata, "Artwork URL"));
	const float previewSourceWidth = playerHasReportedVideoSize ? currentVideoWidth : (playerHasTextureSize ? playerTexture.getWidth() : 0.0f);
	const float previewSourceHeight = playerHasReportedVideoSize ? currentVideoHeight : (playerHasTextureSize ? playerTexture.getHeight() : 0.0f);
	const bool hadPreviewContent = videoPreviewHasContent;
	const bool hadVideoPreview = videoPreviewShowsVideo;
	videoPreviewHasContent = false;
	videoPreviewShowsVideo = false;
	// Prefer VLC's reported display geometry, but fall back to the exposed texture once it is larger than 1x1.
	if (playerHasReportedVideoSize) {
		assignClampedPreviewDimensions(currentVideoWidth, currentVideoHeight, videoPreviewWidth, videoPreviewHeight);
	} else if (playerHasTextureSize && videoPreviewWidth <= 1.0f && videoPreviewHeight <= 1.0f) {
		assignClampedPreviewDimensions(playerTexture.getWidth(), playerTexture.getHeight(), videoPreviewWidth, videoPreviewHeight);
	}
	if (playerHasVideoFrame && previewSourceWidth > 1.0f && previewSourceHeight > 1.0f) {
		assignClampedPreviewDimensions(previewSourceWidth, previewSourceHeight, videoPreviewWidth, videoPreviewHeight);
		ensureFboSize(videoPreviewFbo, videoPreviewWidth, videoPreviewHeight);
		videoPreviewFbo.begin();
		ofClear(0, 0, 0, 255);
		player.draw(0.0f, 0.0f, videoPreviewWidth, videoPreviewHeight);
		videoPreviewFbo.end();
		videoPreviewHasContent = true;
		videoPreviewShowsVideo = true;
	} else if (!player.isStopped() && !artworkPath.empty()) {
		if (videoPreviewArtworkPath != artworkPath) {
			videoPreviewArtworkPath = artworkPath;
			videoPreviewArtworkImage.clear();
		}
		if (ensureLoadedImage(videoPreviewArtworkImage, artworkPath) &&
			videoPreviewArtworkImage.isAllocated() &&
			videoPreviewArtworkImage.getWidth() > 1 &&
			videoPreviewArtworkImage.getHeight() > 1) {
			assignClampedPreviewDimensions(
				static_cast<float>(videoPreviewArtworkImage.getWidth()),
				static_cast<float>(videoPreviewArtworkImage.getHeight()),
				videoPreviewWidth,
				videoPreviewHeight);
			ensureFboSize(videoPreviewFbo, videoPreviewWidth, videoPreviewHeight);
			videoPreviewFbo.begin();
			ofClear(0, 0, 0, 255);
			videoPreviewArtworkImage.draw(0.0f, 0.0f, videoPreviewWidth, videoPreviewHeight);
			videoPreviewFbo.end();
			videoPreviewHasContent = true;
		}
		if (!videoPreviewHasContent && holdPreviewState && hadPreviewContent) {
			videoPreviewHasContent = true;
			videoPreviewShowsVideo = hadVideoPreview;
		}
	} else {
		if (holdPreviewState && hadPreviewContent) {
			videoPreviewHasContent = true;
			videoPreviewShowsVideo = hadVideoPreview;
		} else {
			clearVideoPreviewState(
				videoPreviewFbo,
				videoPreviewArtworkImage,
				videoPreviewArtworkPath,
				videoPreviewWidth,
				videoPreviewHeight);
		}
	}
	const ofFbo & previewEffectsSourceFbo = videoPreviewFbo;
	const ofTexture & previewEffectsSourceTexture = previewEffectsSourceFbo.getTexture();
	const auto & videoGui = remoteGui.getVideoSection();
	const AnaglyphSettings anaglyphSettings = videoGui.getAnaglyphSettings();

	// Only real video frames are pushed through the anaglyph pass.
	// Cover art and empty placeholders stay on the normal preview path.
	if (shouldUseAnaglyphPreview(
			videoPreviewHasContent,
			videoPreviewShowsVideo,
			anaglyphSettings.enabled,
			videoState.stereoMode,
			true,
			previewEffectsSourceFbo)) {
		updateAnaglyphPreview(previewEffectsSourceTexture, videoPreviewWidth, videoPreviewHeight, anaglyphSettings);
	}
	if (projectMInitialized &&
		renderProjectMPreview &&
		projectMTextureSourceMode == ProjectMTextureSourceMode::MainPlayerVideo) {
		// projectM consumes a texture, so the player frame is copied into its dedicated source FBO here.
		drawPlayerToFbo(
			player,
			projectMSourceFbo,
			currentVideoWidth,
			currentVideoHeight,
			false);
		}

	remoteGui.updateSelection(player);

	if (projectMInitialized && renderProjectMPreview) {
		projectM.update();
	}
}

//--------------------------------------------------------------
void ofApp::draw() {
	ofClear(0, 0, 0, 255);
	const ofTexture emptyTexture;
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	const auto & videoGui = remoteGui.getVideoSection();
	const bool showActiveVideoPreview = videoPreviewHasContent && (!player.isStopped() || player.isPlaybackRestartPending());
	// Display switches to the derived anaglyph texture only when the preview currently
	// represents SBS video. All other states keep the standard preview texture.
	const AnaglyphSettings anaglyphSettings = videoGui.getAnaglyphSettings();
	const bool useAnaglyphPreview = shouldUseAnaglyphPreview(
		showActiveVideoPreview,
		videoPreviewShowsVideo,
		anaglyphSettings.enabled,
		videoState.stereoMode,
		anaglyphShaderReady,
		anaglyphPreviewFbo);
	const ofTexture * videoPreviewTexturePtr = &emptyTexture;
	if (useAnaglyphPreview) {
		videoPreviewTexturePtr = &anaglyphPreviewFbo.getTexture();
	} else if (showActiveVideoPreview && videoPreviewFbo.isAllocated()) {
		videoPreviewTexturePtr = &videoPreviewFbo.getTexture();
	}
	const ofTexture & videoPreviewTexture = *videoPreviewTexturePtr;
	const float displayPreviewWidth = showActiveVideoPreview
		? (useAnaglyphPreview ? std::max(1.0f, videoPreviewWidth * 0.5f) : videoPreviewWidth)
		: 0.0f;
	const float displayPreviewHeight = showActiveVideoPreview ? videoPreviewHeight : 0.0f;

	remoteGui.draw(
		player,
		projectM,
		projectMInitialized,
		videoPreviewTexture,
		displayPreviewWidth,
		displayPreviewHeight,
		[this](const std::string & rawPath) {
			return addPathToPlaylist(rawPath);
		},
		[this]() {
			if (projectMInitialized) {
				projectM.randomPreset();
			}
		},
		[this]() {
			if (projectMInitialized) {
				projectM.reloadPresets();
			}
		},
		[this]() {
			reloadProjectMTextures(true);
		},
		[this]() {
			loadPlayerProjectMTexture();
		},
		[this](const std::string & rawPath) {
			return loadCustomProjectMTexture(rawPath);
		},
		[this]() {
			applyAudioVisualizerSettings();
		},
		[this](const std::string & rawPath) {
			return loadCustomSubtitleFile(rawPath);
		},
		[this]() {
			clearCustomSubtitleFile();
		},
		[this]() {
			return customSubtitleStatus();
		},
		[this]() {
			return customSubtitleFontLabels();
		},
		[this]() {
			return customSubtitleFontSelection();
		},
		[this](int index) {
			setCustomSubtitleFontSelection(index);
		});

	drawCustomSubtitleOverlay();

	if (showPlaybackStateOverlay) {
		drawPlaybackStateOverlay(player.getMediaReadinessInfo());
	}
}

void ofApp::keyPressed(int key) {
	if (key == OF_KEY_F9) {
		showPlaybackStateOverlay = !showPlaybackStateOverlay;
	}
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
	remoteGui.handleDragEvent(
		dragInfo,
		player,
		[this](const std::string & rawPath) {
			return addPathToPlaylist(rawPath);
		});
}

//--------------------------------------------------------------
void ofApp::exit() {
	player.close();
	soundStream.close();
}

int ofApp::addPathToPlaylist(const std::string & rawPath) {
	const std::string resolvedPath = resolveInputPath(rawPath);
	const bool isLocalPath = !looksLikeUri(resolvedPath);
	if (resolvedPath.empty()) {
		ofxVlc4::logWarning("Playlist path is empty.");
		return 0;
	}

	if (isLocalPath && !pathExists(resolvedPath)) {
		ofxVlc4::logWarning("Playlist path not found: " + normalizeInputPath(rawPath));
		return 0;
	}

	if (isSubtitlePath(resolvedPath)) {
		return player.addSubtitleSlave(resolvedPath) ? 1 : 0;
	}

	const int addedCount = player.addPathToPlaylist(resolvedPath, kSeedExtensions);
	return addedCount;
}

void ofApp::drawPlayerToFbo(ofxVlc4 & sourcePlayer, ofFbo & targetFbo, float width, float height, bool preserveAspect) {
	if (!targetFbo.isAllocated()) {
		return;
	}

	targetFbo.begin();
	ofClear(0, 0, 0, 255);
	if (width > 0.0f && height > 0.0f) {
		ofPushStyle();
		ofEnableBlendMode(OF_BLENDMODE_DISABLED);
		ofSetColor(255, 255, 255, 255);
		if (preserveAspect) {
			const float targetWidth = targetFbo.getWidth();
			const float targetHeight = targetFbo.getHeight();
			const float sourceAspect = width / height;
			const float targetAspect = targetWidth / targetHeight;

			float drawWidth = targetWidth;
			float drawHeight = targetHeight;
			float drawX = 0.0f;
			float drawY = 0.0f;

			if (sourceAspect > targetAspect) {
				drawHeight = drawWidth / sourceAspect;
				drawY = (targetHeight - drawHeight) * 0.5f;
			} else {
				drawWidth = drawHeight * sourceAspect;
				drawX = (targetWidth - drawWidth) * 0.5f;
			}

			sourcePlayer.draw(drawX, drawY, drawWidth, drawHeight);
		} else {
			sourcePlayer.draw(0.0f, 0.0f, targetFbo.getWidth(), targetFbo.getHeight());
		}
		ofPopStyle();
	}
	targetFbo.end();
}

void ofApp::refreshProjectMSourceTexture() {
	if (projectMTextureSourceMode != ProjectMTextureSourceMode::MainPlayerVideo) {
		return;
	}

	// Clear the source FBO when VLC has no valid frame so projectM shows a neutral texture instead of a stale frame.
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	if (!videoState.frameReceived) {
		clearAllocatedFbo(projectMSourceFbo);
		return;
	}
	const float sourceWidth = static_cast<float>(videoState.sourceWidth);
	const float sourceHeight = static_cast<float>(videoState.sourceHeight);

	drawPlayerToFbo(
		player,
		projectMSourceFbo,
		sourceWidth,
		sourceHeight,
		false);
}

void ofApp::applyProjectMTexture() {
	switch (projectMTextureSourceMode) {
	case ProjectMTextureSourceMode::InternalTextures:
		ofxProjectM::logNotice("Texture source: internal textures.");
		projectM.useInternalTextureOnly();
		projectM.resetTextures();
		break;
	case ProjectMTextureSourceMode::CustomImage:
		ofxProjectM::logNotice("Texture source: image texture.");
		if (projectMCustomTextureImage.isAllocated()) {
			drawProjectMSourceImageToFbo(projectMSourceFbo, projectMCustomTextureImage);
		}
		projectM.setTexture(projectMSourceFbo.getTexture());
		projectM.resetTextures();
		break;
	case ProjectMTextureSourceMode::MainPlayerVideo:
		ofxProjectM::logNotice("Texture source: main player video.");
		refreshProjectMSourceTexture();
		projectM.setTexture(projectMSourceFbo.getTexture());
		projectM.resetTextures();
		break;
	}
}

bool ofApp::hasProjectMSourceSize() const {
	switch (projectMTextureSourceMode) {
	case ProjectMTextureSourceMode::MainPlayerVideo:
	{
		const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
		return videoState.geometryKnown;
	}
	case ProjectMTextureSourceMode::CustomImage:
		return projectMCustomTextureImage.isAllocated() &&
			projectMCustomTextureImage.getWidth() > 0 &&
			projectMCustomTextureImage.getHeight() > 0;
	case ProjectMTextureSourceMode::InternalTextures:
		return ofGetScreenWidth() > 0 && ofGetScreenHeight() > 0;
	}

	return false;
}

void ofApp::ensureProjectMInitialized() {
	if (projectMInitialized || !hasProjectMSourceSize()) {
		return;
	}

	projectM.setWindowSize(ofGetScreenWidth(), ofGetScreenHeight());
	projectM.init();
	projectMInitialized = true;
	reloadProjectMTextures(projectMTextureSourceMode == ProjectMTextureSourceMode::InternalTextures, false);
}

void ofApp::reloadProjectMTextures(bool useStandardTextures, bool restartPreset) {
	if (useStandardTextures) {
		projectMTextureSourceMode = ProjectMTextureSourceMode::InternalTextures;
		applyProjectMTexture();
		if (restartPreset) {
			restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
		}
		return;
	}

	if (projectMTextureSourceMode == ProjectMTextureSourceMode::MainPlayerVideo) {
		applyProjectMTexture();
		if (restartPreset) {
			restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
		}
		return;
	}

	if (!ensureLoadedImage(projectMCustomTextureImage, projectMCustomTexturePath)) {
		projectMTextureSourceMode = ProjectMTextureSourceMode::InternalTextures;
	}

	applyProjectMTexture();
	if (restartPreset) {
		restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
	}
}

void ofApp::loadPlayerProjectMTexture() {
	projectMTextureSourceMode = ProjectMTextureSourceMode::MainPlayerVideo;
	applyProjectMTexture();
	restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
}

bool ofApp::loadCustomProjectMTexture(const std::string & rawPath) {
	const std::string normalizedPath = normalizeInputPath(rawPath);
	const std::string requestedPath = normalizedPath.empty() ? projectMCustomTexturePath : normalizedPath;
	const std::string resolvedPath = resolveInputPath(requestedPath);
	if (resolvedPath.empty()) {
		ofxProjectM::logWarning("projectM image texture path is empty.");
		return false;
	}

	if (!pathExists(resolvedPath)) {
		ofxProjectM::logWarning("projectM image texture path not found: " + requestedPath);
		return false;
	}

	if (isSupportedVideoPath(resolvedPath)) {
		ofxProjectM::logWarning("Only image textures allowed.");
		return false;
	}

	if (!isSupportedCustomImagePath(resolvedPath)) {
		return false;
	}

	ofImage image;
	if (!image.load(resolvedPath)) {
		ofxProjectM::logError("Failed to load custom projectM texture image: " + resolvedPath);
		return false;
	}

	projectMCustomTextureImage = std::move(image);
	projectMCustomTexturePath = resolvedPath;
	projectMTextureSourceMode = ProjectMTextureSourceMode::CustomImage;
	applyProjectMTexture();
	restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
	return true;
}

void ofApp::setupCustomSubtitleFonts() {
	customSubtitleFontPaths.clear();
	customSubtitleFontNames.clear();

	for (const auto & candidate : kCustomSubtitleFontCandidates) {
		if (!ofFile::doesFileExist(candidate.path, true)) {
			continue;
		}
		customSubtitleFontNames.push_back(candidate.label);
		customSubtitleFontPaths.push_back(candidate.path);
	}

	customSubtitleFontIndex = customSubtitleFontPaths.empty() ? -1 : 0;
	reloadCustomSubtitleFont();
}

bool ofApp::reloadCustomSubtitleFont() {
	customSubtitleFontLoaded = false;
	customSubtitleFont = ofTrueTypeFont();

	if (customSubtitleFontIndex < 0 ||
		customSubtitleFontIndex >= static_cast<int>(customSubtitleFontPaths.size())) {
		return false;
	}

	customSubtitleFontLoaded = customSubtitleFont.load(
		customSubtitleFontPaths[static_cast<size_t>(customSubtitleFontIndex)],
		kCustomSubtitleFontSize,
		true,
		true,
		false,
		0.3f,
		0);
	if (customSubtitleFontLoaded) {
		customSubtitleFont.setLineHeight(kCustomSubtitleFontSize * 1.25f);
	}
	return customSubtitleFontLoaded;
}

std::vector<std::string> ofApp::customSubtitleFontLabels() const {
	return customSubtitleFontNames;
}

int ofApp::customSubtitleFontSelection() const {
	return customSubtitleFontIndex;
}

void ofApp::setCustomSubtitleFontSelection(int index) {
	if (index < 0 || index >= static_cast<int>(customSubtitleFontPaths.size()) || index == customSubtitleFontIndex) {
		return;
	}

	customSubtitleFontIndex = index;
	reloadCustomSubtitleFont();
}

bool ofApp::loadCustomSubtitleFile(const std::string & rawPath) {
	const auto failCustomSubtitleLoad = [this](const std::string & errorMessage) {
		customSubtitlePath.clear();
		customSubtitleCues.clear();
		customSubtitleLoadError = errorMessage;
		return false;
	};

	const std::string normalizedPath = normalizeInputPath(rawPath);
	const std::string resolvedPath = resolveInputPath(normalizedPath);
	if (resolvedPath.empty()) {
		return failCustomSubtitleLoad("Custom subtitle path is empty.");
	}

	if (!pathExists(resolvedPath)) {
		return failCustomSubtitleLoad("Custom subtitle file not found.");
	}

	if (!isSrtPath(resolvedPath)) {
		return failCustomSubtitleLoad("Custom subtitle overlay currently supports .srt files only.");
	}

	std::vector<SimpleSrtSubtitleCue> parsedCues;
	std::string parseError;
	if (!SimpleSrtSubtitleParser::parseFile(resolvedPath, parsedCues, parseError)) {
		return failCustomSubtitleLoad(parseError.empty() ? "Failed to parse subtitle file." : parseError);
	}

	customSubtitlePath = resolvedPath;
	customSubtitleLoadError.clear();
	customSubtitleCues = std::move(parsedCues);
	return true;
}

void ofApp::clearCustomSubtitleFile() {
	customSubtitlePath.clear();
	customSubtitleLoadError.clear();
	customSubtitleCues.clear();
}

std::string ofApp::customSubtitleStatus() const {
	if (!customSubtitleLoadError.empty()) {
		return "Custom SRT: " + customSubtitleLoadError;
	}
	if (customSubtitlePath.empty() || customSubtitleCues.empty()) {
		return "Custom SRT: Off";
	}

	std::string status = "Custom SRT: " + ofFilePath::getFileName(customSubtitlePath) +
		" (" + ofToString(customSubtitleCues.size()) + " cues)";
	if (customSubtitleFontIndex >= 0 &&
		customSubtitleFontIndex < static_cast<int>(customSubtitleFontNames.size())) {
		status += "   |   Font: " + customSubtitleFontNames[static_cast<size_t>(customSubtitleFontIndex)];
	}
	return status;
}

const SimpleSrtSubtitleCue * ofApp::findActiveCustomSubtitleCue() const {
	if (customSubtitleCues.empty()) {
		return nullptr;
	}

	const int currentTimeMs = player.getTime();
	if (currentTimeMs < 0) {
		return nullptr;
	}

	auto upper = std::upper_bound(
		customSubtitleCues.begin(),
		customSubtitleCues.end(),
		currentTimeMs,
		[](int timeMs, const SimpleSrtSubtitleCue & cue) {
			return timeMs < cue.startMs;
		});
	if (upper == customSubtitleCues.begin()) {
		return nullptr;
	}

	const auto & cue = *std::prev(upper);
	return (currentTimeMs >= cue.startMs && currentTimeMs <= cue.endMs) ? &cue : nullptr;
}

void ofApp::drawCustomSubtitleOverlay() const {
	const SimpleSrtSubtitleCue * cue = findActiveCustomSubtitleCue();
	if (cue == nullptr || cue->text.empty()) {
		return;
	}

	const ofRectangle previewRect = remoteGui.getVideoPreviewScreenRect();
	if (previewRect.getWidth() <= 1.0f || previewRect.getHeight() <= 1.0f) {
		return;
	}

	std::vector<std::string> lines = ofSplitString(cue->text, "\n", false, false);
	if (lines.empty()) {
		return;
	}

	ofPushStyle();
	ofEnableAlphaBlending();
	const GLboolean wasScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
	GLint previousScissorBox[4] = { 0, 0, 0, 0 };
	glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);

	const GLint scissorX = static_cast<GLint>(std::max(0.0f, std::floor(previewRect.getX())));
	const GLint scissorY = static_cast<GLint>(std::max(0.0f, std::floor(ofGetHeight() - previewRect.getBottom())));
	const GLint scissorWidth = static_cast<GLint>(std::max(1.0f, std::ceil(previewRect.getWidth())));
	const GLint scissorHeight = static_cast<GLint>(std::max(1.0f, std::ceil(previewRect.getHeight())));
	glEnable(GL_SCISSOR_TEST);
	glScissor(scissorX, scissorY, scissorWidth, scissorHeight);

	const float previewCenterX = previewRect.getX() + (previewRect.getWidth() * 0.5f);
	const float previewBottom = previewRect.getBottom();
	const float previewLeft = previewRect.getX();
	const float previewRight = previewRect.getRight();
	const float previewTop = previewRect.getY();

	if (!customSubtitleFontLoaded) {
		float y = previewBottom - kCustomSubtitleBottomMargin - static_cast<float>((lines.size() - 1) * 18);
		y = std::max(previewTop + 18.0f, y);
		for (const auto & line : lines) {
			const float lineWidth = static_cast<float>(line.size()) * 8.0f;
			const float x = std::max(previewLeft + 20.0f, previewCenterX - (lineWidth * 0.5f));
			ofDrawBitmapStringHighlight(line, x, y, ofColor(0, 0, 0, 180), ofColor::white);
			y += 18.0f;
		}
		if (!wasScissorEnabled) {
			glDisable(GL_SCISSOR_TEST);
		} else {
			glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
		}
		ofPopStyle();
		return;
	}

	const float lineHeight = std::max(customSubtitleFont.getLineHeight(), 1.0f);
	float baselineY = previewBottom - kCustomSubtitleBottomMargin - lineHeight * static_cast<float>(lines.size() - 1);
	baselineY = std::max(previewTop + lineHeight, baselineY);
	for (const auto & line : lines) {
		const ofRectangle bounds = customSubtitleFont.getStringBoundingBox(line, 0.0f, 0.0f);
		const float maxDrawX = previewRight - bounds.width - 12.0f;
		const float drawX = std::max(previewLeft + 12.0f, std::min(previewCenterX - (bounds.width * 0.5f), maxDrawX));
		const float drawY = baselineY;

		ofSetColor(0, 0, 0, 200);
		customSubtitleFont.drawString(line, drawX - kCustomSubtitleShadowOffset, drawY);
		customSubtitleFont.drawString(line, drawX + kCustomSubtitleShadowOffset, drawY);
		customSubtitleFont.drawString(line, drawX, drawY - kCustomSubtitleShadowOffset);
		customSubtitleFont.drawString(line, drawX, drawY + kCustomSubtitleShadowOffset);

		ofSetColor(255);
		customSubtitleFont.drawString(line, drawX, drawY);
		baselineY += lineHeight;
	}

	if (!wasScissorEnabled) {
		glDisable(GL_SCISSOR_TEST);
	} else {
		glScissor(previousScissorBox[0], previousScissorBox[1], previousScissorBox[2], previousScissorBox[3]);
	}

	ofPopStyle();
}
