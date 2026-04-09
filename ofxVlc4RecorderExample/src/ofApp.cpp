#include "ofApp.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

namespace {
constexpr float kDefaultAspect = 16.0f / 9.0f;
constexpr int kVideoModeFrameRate = 60;
constexpr int kAudioOnlyModeFrameRate = 30;
constexpr double kTwoPi = 6.28318530717958647692;

const std::set<std::string> & audioOnlyExtensions() {
	static const std::set<std::string> extensions = {
		".wav", ".mp3", ".flac", ".ogg",
		".m4a", ".aac", ".aiff", ".wma"
	};
	return extensions;
}

bool hasActiveVlcMedia(const ofxVlc4 & player) {
	return player.hasPlaylist() || !player.getCurrentPath().empty();
}

std::string yesNo(bool value) {
	return value ? "yes" : "no";
}
}

void ofApp::setup() {
	ofSetWindowTitle("ofxVlc4 Recorder Example");
	ofSetFrameRate(kVideoModeFrameRate);
	ofSetBackgroundColor(ofColor(12, 12, 12));
	ofSetColor(255);

	nativeRecordingDirectory = ofToDataPath("recordings/native", true);
	audioRecordingBasePath = ofToDataPath("recordings/audio/audio-recording", true);
	benchmarkRecordingBasePath = ofToDataPath("recordings/benchmark/benchmark-recording", true);
	windowRecordingBasePath = ofToDataPath("recordings/window/window-recording", true);
	snapshotDirectory = ofToDataPath("snapshots", true);

	std::error_code error;
	std::filesystem::create_directories(nativeRecordingDirectory, error);
	std::filesystem::create_directories(std::filesystem::path(audioRecordingBasePath).parent_path(), error);
	std::filesystem::create_directories(std::filesystem::path(benchmarkRecordingBasePath).parent_path(), error);
	std::filesystem::create_directories(std::filesystem::path(windowRecordingBasePath).parent_path(), error);
	std::filesystem::create_directories(snapshotDirectory, error);

	player = std::make_unique<ofxVlc4>();
	player->setAudioCaptureEnabled(true);
	player->init(0, nullptr);
	player->setWatchTimeEnabled(true);
	player->setWatchTimeMinPeriodUs(50000);
	player->setVolume(70);
	player->setNativeRecordDirectory(nativeRecordingDirectory);
	player->setVideoReadbackBufferCount(3);
	player->setRecordingPreset(ofxVlc4RecordingPreset{});

	benchmarkAudioSampleRate = std::max(8000, player->getAudioCaptureSampleRate());
	benchmarkAudioChannelCount = std::max(1, player->getAudioCaptureChannelCount());
	setupBenchmarkAudio();
	gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
	ImGui::GetIO().IniFilename = "imgui_recorder.ini";
	loadSeedMedia();
}

void ofApp::update() {
	if (!player || shuttingDown) {
		return;
	}

	updateBenchmarkTexture();
	player->update();
	updateAudioOnlyMode();

	if (!player->isVideoRecording() && !player->isWindowRecording()) {
		benchmarkVideoRecordingActive = false;
		windowRecordingActive = false;
	}
}

void ofApp::draw() {
	ofBackground(12, 12, 12);

	if (!player) {
		ofSetColor(150);
		ofDrawBitmapString("Player closed.", previewMargin, previewMargin + 24.0f);
		return;
	}

	const bool vlcHasMedia = hasActiveVlcMedia(*player);
	const ofxVlc4::MediaReadinessInfo readiness = vlcHasMedia ? player->getMediaReadinessInfo() : ofxVlc4::MediaReadinessInfo{};
	const ofxVlc4::VideoStateInfo videoState = vlcHasMedia ? player->getVideoStateInfo() : ofxVlc4::VideoStateInfo{};
	const bool useBenchmarkPreview = benchmarkModeActive && benchmarkFbo.isAllocated();

	const float availableWidth = std::max(1.0f, ofGetWidth() - previewMargin * 2.0f);
	const float availableHeight = std::max(1.0f, ofGetHeight() - 230.0f);
	float sourceWidth = useBenchmarkPreview ? static_cast<float>(benchmarkFbo.getWidth()) : static_cast<float>(videoState.sourceWidth);
	float sourceHeight = useBenchmarkPreview ? static_cast<float>(benchmarkFbo.getHeight()) : static_cast<float>(videoState.sourceHeight);
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
	if (useBenchmarkPreview) {
		ofSetColor(255);
		benchmarkFbo.draw(drawX, drawY, drawWidth, drawHeight);
	} else if (readiness.hasReceivedVideoFrame) {
		ofSetColor(255);
		player->draw(drawX, drawY, drawWidth, drawHeight);
	} else {
		ofSetColor(150);
		const std::string placeholder = useBenchmarkPreview
			? "Benchmark preview active."
			: !vlcHasMedia
			? "Drop a media file or press O to load one."
			: (audioOnlyModeActive
				? "Audio-only media active. Recording controls still work."
				: "Waiting for first video frame.");
		ofDrawBitmapString(placeholder, drawX + 14.0f, drawY + 24.0f);
	}
	ofPopStyle();

	gui.begin();
	drawControlPanel();
	gui.end();
}

void ofApp::exit() {
	shutdownPlayer();
	gui.exit();
}

void ofApp::audioOut(ofSoundBuffer & buffer) {
	buffer.set(0.0f);
	if (!player || shuttingDown || player->getRecordingAudioSource() != ofxVlc4RecordingAudioSource::ExternalSubmitted) {
		return;
	}

	const size_t channelCount = std::max<size_t>(1, buffer.getNumChannels());
	const size_t frameCount = buffer.getNumFrames();
	auto & samples = buffer.getBuffer();
	const double sampleRate = static_cast<double>(std::max(1, benchmarkAudioSampleRate));

	for (size_t frame = 0; frame < frameCount; ++frame) {
		const float modulator = 0.5f + 0.5f * std::sin(benchmarkAudioModPhase);
		const double carrierFrequency = 180.0 + static_cast<double>(modulator) * 220.0;
		const float carrier = std::sin(benchmarkAudioPhase);
		const float shimmer = std::sin(benchmarkAudioPhase * 0.5 + benchmarkAudioModPhase * 0.35);
		const float sample = 0.16f * carrier + 0.07f * shimmer;

		for (size_t channel = 0; channel < channelCount; ++channel) {
			samples[frame * channelCount + channel] = sample;
		}

		benchmarkAudioPhase += kTwoPi * carrierFrequency / sampleRate;
		benchmarkAudioModPhase += kTwoPi * 0.35 / sampleRate;
		if (benchmarkAudioPhase >= kTwoPi) {
			benchmarkAudioPhase = std::fmod(benchmarkAudioPhase, kTwoPi);
		}
		if (benchmarkAudioModPhase >= kTwoPi) {
			benchmarkAudioModPhase = std::fmod(benchmarkAudioModPhase, kTwoPi);
		}
	}

	player->submitRecordedAudioSamples(samples.data(), samples.size());
}

void ofApp::keyPressed(int key) {
	if (!player || shuttingDown) {
		return;
	}

	switch (key) {
	case 'o':
	case 'O': {
		ofFileDialogResult result = ofSystemLoadDialog("Choose media file");
		if (result.bSuccess) {
			loadMediaPath(result.getPath(), true);
		}
		break;
	}
	case ' ':
		if (player->isPlaying()) {
			player->pause();
		} else {
			player->play();
		}
		break;
	case 's':
	case 'S':
		if (benchmarkVideoRecordingActive || isBenchmarkRecordingActive()) {
			requestBenchmarkRecordingStop();
			break;
		}
		if (windowRecordingActive || isWindowCaptureRecordingActive()) {
			requestWindowRecordingStop();
			break;
		}
		player->stop();
		break;
	case 'n':
	case 'N':
		player->nextMediaListItem();
		break;
	case 'p':
	case 'P':
		player->previousMediaListItem();
		break;
	case 'm':
	case 'M':
		player->toggleMute();
		break;
	case 'r':
	case 'R':
		toggleNativeRecording();
		break;
	case 'a':
	case 'A':
		toggleAudioRecording();
		break;
	case 'x':
	case 'X':
		takeSnapshot();
		break;
	case 'b':
	case 'B':
		toggleBenchmarkMode();
		break;
	case 'f':
	case 'F':
		toggleBenchmarkAudioSource();
		break;
	case 'g':
	case 'G':
		cycleVideoRecordingCodec();
		break;
	case 'h':
	case 'H':
		cycleBenchmarkMuxProfile();
		break;
	case 'j':
	case 'J':
		toggleBenchmarkMuxSourceCleanup();
		break;
	case 'v':
	case 'V':
		toggleBenchmarkVideoRecording();
		break;
	case 'w':
	case 'W':
		if (windowRecordingActive || isWindowCaptureRecordingActive()) {
			requestWindowRecordingStop();
			break;
		}
		toggleWindowRecording();
		break;
	case 'c':
	case 'C':
		player->clearLastMessages();
		break;
	case 'l':
	case 'L':
		cycleReadbackPolicy();
		break;
	case '[':
		adjustReadbackBufferCount(-1);
		break;
	case ']':
		adjustReadbackBufferCount(1);
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
	if (dragInfo.files.empty()) {
		return;
	}

	std::vector<std::filesystem::path> paths;
	paths.reserve(dragInfo.files.size());
	for (const auto & file : dragInfo.files) {
		paths.emplace_back(file);
	}
	replacePlaylistFromDroppedFiles(paths);
}

void ofApp::windowResized(int, int) {
}

void ofApp::shutdownPlayer() {
	if (!player || shuttingDown) {
		return;
	}

	shuttingDown = true;
	closeBenchmarkAudio();
	stopActiveRecorders();
	player->close();
	player.reset();
}

void ofApp::setupBenchmarkAudio() {
	ofSoundStreamSettings settings;
	settings.setOutListener(this);
	settings.sampleRate = benchmarkAudioSampleRate;
	settings.numOutputChannels = benchmarkAudioChannelCount;
	settings.numInputChannels = 0;
	settings.bufferSize = 512;
	settings.numBuffers = 4;

	const auto configureOutputApi = [&](ofSoundDevice::Api api) {
		settings.setApi(api);
		const std::vector<ofSoundDevice> devices = benchmarkAudioStream.getDeviceList(api);
		const auto defaultDevice = std::find_if(devices.begin(), devices.end(), [](const ofSoundDevice & device) {
			return device.outputChannels > 0 && device.isDefaultOutput;
		});
		if (defaultDevice != devices.end()) {
			settings.setOutDevice(*defaultDevice);
			return true;
		}
		const auto firstOutputDevice = std::find_if(devices.begin(), devices.end(), [](const ofSoundDevice & device) {
			return device.outputChannels > 0;
		});
		if (firstOutputDevice != devices.end()) {
			settings.setOutDevice(*firstOutputDevice);
			return true;
		}
		return false;
	};

	bool configuredApi = configureOutputApi(ofSoundDevice::Api::MS_WASAPI);
	if (!configuredApi) {
		configuredApi = configureOutputApi(ofSoundDevice::Api::MS_DS);
	}
	if (!configuredApi) {
		settings.setApi(ofSoundDevice::Api::DEFAULT);
	}

	benchmarkAudioStream.setup(settings);
}

void ofApp::closeBenchmarkAudio() {
	benchmarkAudioStream.stop();
	benchmarkAudioStream.close();
}

void ofApp::stopActiveRecorders() {
	if (!player) {
		return;
	}

	if (isWindowCaptureRecordingActive()) {
		windowRecordingActive = false;
		player->endWindowRecording();
	}
	if (isBenchmarkRecordingActive()) {
		player->stopRecordingSession();
		benchmarkVideoRecordingActive = false;
	}
	player->setNativeRecordingEnabled(false);
	if (player->isAudioRecording()) {
		player->recordAudio(audioRecordingBasePath);
	}
}

void ofApp::loadSeedMedia() {
	if (!player || shuttingDown) {
		return;
	}

	const std::filesystem::path sharedMoviesDirectory =
		std::filesystem::path(ofFilePath::getCurrentExeDir()) /
		"..\\..\\..\\..\\examples\\video\\videoPlayerExample\\bin\\data\\movies";
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
			loadMediaPath(candidate.string(), true);
			return;
		}
	}
}

bool ofApp::allocateBenchmarkTexture() {
	if (benchmarkFbo.isAllocated()) {
		return true;
	}

	benchmarkFbo.allocate(benchmarkWidth, benchmarkHeight, GL_RGB);
	if (!benchmarkFbo.isAllocated()) {
		ofLogError("ofApp") << "Failed to allocate benchmark FBO "
			<< benchmarkWidth << "x" << benchmarkHeight << ".";
		return false;
	}

	benchmarkFbo.begin();
	ofClear(0, 0, 0, 255);
	benchmarkFbo.end();
	benchmarkFbo.getTexture().setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
	return true;
}

void ofApp::updateBenchmarkTexture() {
	if (!benchmarkModeActive) {
		return;
	}

	if (!allocateBenchmarkTexture()) {
		return;
	}
	benchmarkPhase += 0.015f;

	benchmarkFbo.begin();
	ofPushStyle();
	ofClear(8, 8, 10, 255);
	ofBackgroundGradient(ofColor(18, 20, 28), ofColor(4, 6, 10));

	const float width = static_cast<float>(benchmarkFbo.getWidth());
	const float height = static_cast<float>(benchmarkFbo.getHeight());

	for (int i = 0; i < 48; ++i) {
		const float t = static_cast<float>(i) / 47.0f;
		const float x = t * width;
		const float barWidth = width / 64.0f;
		const float wave = 0.5f + 0.5f * std::sin(benchmarkPhase * 3.0f + t * 18.0f);
		const float barHeight = (0.15f + wave * 0.7f) * height;
		ofSetColor(
			static_cast<unsigned char>(40 + 160 * wave),
			static_cast<unsigned char>(120 + 90 * (1.0f - wave)),
			static_cast<unsigned char>(180 + 50 * wave),
			210);
		ofDrawRectangle(x, height - barHeight, barWidth, barHeight);
	}

	for (int ring = 0; ring < 9; ++ring) {
		const float radius = 70.0f + ring * 42.0f + 24.0f * std::sin(benchmarkPhase * 2.0f + ring);
		ofNoFill();
		ofSetLineWidth(2.0f);
		ofSetColor(60 + ring * 12, 130 + ring * 8, 220 - ring * 10, 180);
		ofDrawCircle(width * 0.5f, height * 0.42f, radius);
	}

	ofFill();
	for (int y = 0; y < 14; ++y) {
		for (int x = 0; x < 24; ++x) {
			const float tx = static_cast<float>(x) / 23.0f;
			const float ty = static_cast<float>(y) / 13.0f;
			const float pulse = 0.5f + 0.5f * std::sin(benchmarkPhase * 4.0f + tx * 11.0f + ty * 7.0f);
			ofSetColor(
				static_cast<unsigned char>(50 + 180 * pulse),
				static_cast<unsigned char>(40 + 90 * (1.0f - pulse)),
				static_cast<unsigned char>(90 + 120 * pulse),
				120);
			ofDrawRectangle(
				tx * width,
				ty * height * 0.6f,
				width / 30.0f,
				height / 24.0f);
		}
	}

	ofSetColor(255);
	ofDrawBitmapStringHighlight(
		"Benchmark texture " + ofToString(benchmarkWidth) + "x" + ofToString(benchmarkHeight) +
			" phase=" + ofToString(benchmarkPhase, 2),
		24.0f,
		32.0f);
	ofPopStyle();
	benchmarkFbo.end();
}

bool ofApp::loadMediaPath(const std::string & path, bool autoPlay) {
	if (!player || path.empty()) {
		return false;
	}

	benchmarkModeActive = false;
	benchmarkVideoRecordingActive = false;
	stopActiveRecorders();
	player->stop();
	player->clearPlaylist();
	player->addPathToPlaylist(path);
	if (autoPlay) {
		player->playIndex(0);
	}
	return true;
}

void ofApp::replacePlaylistFromDroppedFiles(const std::vector<std::filesystem::path> & paths) {
	if (!player) {
		return;
	}

	std::vector<std::string> mediaPaths;
	mediaPaths.reserve(paths.size());
	for (const auto & path : paths) {
		if (std::filesystem::is_regular_file(path)) {
			mediaPaths.push_back(path.string());
		}
	}

	if (mediaPaths.empty()) {
		return;
	}

	benchmarkModeActive = false;
	benchmarkVideoRecordingActive = false;
	stopActiveRecorders();
	player->stop();
	player->clearPlaylist();

	for (const auto & mediaPath : mediaPaths) {
		player->addPathToPlaylist(mediaPath);
	}
	player->playIndex(0);
}

void ofApp::updateAudioOnlyMode() {
	const bool shouldUseAudioOnlyMode = isCurrentMediaAudioOnly();
	if (audioOnlyModeActive == shouldUseAudioOnlyMode) {
		return;
	}

	audioOnlyModeActive = shouldUseAudioOnlyMode;
	ofSetFrameRate(audioOnlyModeActive ? kAudioOnlyModeFrameRate : kVideoModeFrameRate);
}

bool ofApp::isCurrentMediaAudioOnly() const {
	if (benchmarkModeActive) {
		return false;
	}
	if (!player) {
		return false;
	}

	const std::string currentPath = player->getCurrentPath();
	if (currentPath.empty()) {
		return false;
	}

	const std::string extension = "." + ofToLower(ofFilePath::getFileExt(currentPath));
	return audioOnlyExtensions().count(extension) > 0;
}

void ofApp::toggleBenchmarkMode() {
	if (!player) {
		return;
	}

	if (benchmarkModeActive && isBenchmarkRecordingActive()) {
		player->stop();
		benchmarkVideoRecordingActive = false;
	}
	benchmarkModeActive = !benchmarkModeActive;
	if (benchmarkModeActive) {
		if (!allocateBenchmarkTexture()) {
			benchmarkModeActive = false;
			return;
		}
		ofSetFrameRate(kVideoModeFrameRate);
	}
}

void ofApp::toggleBenchmarkAudioSource() {
	if (isBenchmarkRecordingActive()) {
		return;
	}

	const ofxVlc4RecordingAudioSource audioSource = player->getRecordingAudioSourcePreset();
	player->setRecordingAudioSourcePreset(
		audioSource == ofxVlc4RecordingAudioSource::ExternalSubmitted
			? ofxVlc4RecordingAudioSource::VlcCaptured
			: ofxVlc4RecordingAudioSource::ExternalSubmitted);
}

void ofApp::cycleVideoRecordingCodec() {
	if (!player || isBenchmarkRecordingActive() || player->isNativeRecordingEnabled() || player->isVideoRecording()) {
		return;
	}

	const ofxVlc4RecordingVideoCodecPreset presets[] = {
		ofxVlc4RecordingVideoCodecPreset::H264,
		ofxVlc4RecordingVideoCodecPreset::H265,
		ofxVlc4RecordingVideoCodecPreset::Mp4v,
		ofxVlc4RecordingVideoCodecPreset::Mjpg,
		ofxVlc4RecordingVideoCodecPreset::Hap,
		ofxVlc4RecordingVideoCodecPreset::HapAlpha,
		ofxVlc4RecordingVideoCodecPreset::HapQ,
		ofxVlc4RecordingVideoCodecPreset::H264_NVENC,
		ofxVlc4RecordingVideoCodecPreset::H265_NVENC,
		ofxVlc4RecordingVideoCodecPreset::H264_QSV,
		ofxVlc4RecordingVideoCodecPreset::H265_QSV,
		ofxVlc4RecordingVideoCodecPreset::H264_VAAPI,
		ofxVlc4RecordingVideoCodecPreset::H265_VAAPI,
		ofxVlc4RecordingVideoCodecPreset::H264_AMF,
		ofxVlc4RecordingVideoCodecPreset::H265_AMF,
		ofxVlc4RecordingVideoCodecPreset::H264_MFT,
		ofxVlc4RecordingVideoCodecPreset::H265_MFT
	};
	const ofxVlc4RecordingVideoCodecPreset currentPreset = player->getVideoRecordingCodecPreset();
	for (size_t i = 0; i < std::size(presets); ++i) {
		if (presets[i] == currentPreset) {
			player->setVideoRecordingCodecPreset(presets[(i + 1) % std::size(presets)]);
			return;
		}
	}
	player->setVideoRecordingCodecPreset(presets[0]);
}

void ofApp::cycleBenchmarkMuxProfile() {
	if ((player && (player->isRecordingMuxPending() || player->isRecordingMuxInProgress())) ||
		isBenchmarkRecordingActive()) {
		return;
	}

	const ofxVlc4RecordingVideoCodecPreset codecPreset = player->getVideoRecordingCodecPreset();
	const ofxVlc4RecordingMuxProfile muxProfile = player->getRecordingMuxProfile();
	if (codecPreset == ofxVlc4RecordingVideoCodecPreset::H265 ||
		codecPreset == ofxVlc4RecordingVideoCodecPreset::H265_NVENC ||
		codecPreset == ofxVlc4RecordingVideoCodecPreset::H265_QSV ||
		codecPreset == ofxVlc4RecordingVideoCodecPreset::H265_VAAPI ||
		codecPreset == ofxVlc4RecordingVideoCodecPreset::H265_AMF ||
		codecPreset == ofxVlc4RecordingVideoCodecPreset::H265_MFT) {
		player->setRecordingMuxProfile(
			muxProfile == ofxVlc4RecordingMuxProfile::MkvOpus
				? ofxVlc4RecordingMuxProfile::MkvFlac
				: muxProfile == ofxVlc4RecordingMuxProfile::MkvFlac
					? ofxVlc4RecordingMuxProfile::MkvLpcm
					: ofxVlc4RecordingMuxProfile::MkvOpus);
		return;
	}

	const bool isHap = ofxVlc4::recordingVideoCodecUsesMovContainer(codecPreset);
	if (isHap) {
		player->setRecordingMuxProfile(ofxVlc4RecordingMuxProfile::MovAac);
		return;
	}

	player->setRecordingMuxProfile(
		muxProfile == ofxVlc4RecordingMuxProfile::Mp4Aac
			? ofxVlc4RecordingMuxProfile::MkvOpus
			: muxProfile == ofxVlc4RecordingMuxProfile::MkvOpus
				? ofxVlc4RecordingMuxProfile::MkvFlac
				: muxProfile == ofxVlc4RecordingMuxProfile::MkvFlac
					? ofxVlc4RecordingMuxProfile::MkvLpcm
					: muxProfile == ofxVlc4RecordingMuxProfile::MkvLpcm
						? ofxVlc4RecordingMuxProfile::OggVorbis
						: muxProfile == ofxVlc4RecordingMuxProfile::OggVorbis
							? ofxVlc4RecordingMuxProfile::MovAac
							: ofxVlc4RecordingMuxProfile::Mp4Aac);
}

void ofApp::toggleBenchmarkMuxSourceCleanup() {
	if ((player && (player->isRecordingMuxPending() || player->isRecordingMuxInProgress())) ||
		isBenchmarkRecordingActive()) {
		return;
	}

	player->setRecordingDeleteMuxSourceFilesOnSuccess(!player->getRecordingDeleteMuxSourceFilesOnSuccess());
}

void ofApp::toggleBenchmarkVideoRecording() {
	if (!player) {
		return;
	}
	if (isRecordingBusy() && !isBenchmarkRecordingActive()) {
		return;
	}
	if (benchmarkVideoRecordingActive || isBenchmarkRecordingActive()) {
		requestBenchmarkRecordingStop();
		return;
	}

	if (!benchmarkModeActive) {
		benchmarkModeActive = true;
		if (!allocateBenchmarkTexture()) {
			benchmarkModeActive = false;
			return;
		}
	}

	stopActiveRecorders();
	if (!benchmarkFbo.isAllocated()) {
		ofLogError("ofApp") << "Benchmark recording requested without an allocated FBO.";
		return;
	}
	if (!player->startRecordingSession(ofxVlc4::textureRecordingSessionConfig(
		benchmarkRecordingBasePath,
		benchmarkFbo.getTexture(),
		player->getRecordingPreset(),
		benchmarkAudioSampleRate,
		benchmarkAudioChannelCount))) {
		return;
	}
	benchmarkVideoRecordingActive = true;
}

void ofApp::toggleWindowRecording() {
	if (!player) {
		return;
	}
	if (windowRecordingActive || isWindowCaptureRecordingActive()) {
		requestWindowRecordingStop();
		return;
	}
	if (isRecordingBusy() && !isWindowCaptureRecordingActive()) {
		return;
	}

	stopActiveRecorders();
	if (!player->startRecordingSession(ofxVlc4::windowRecordingSessionConfig(
		windowRecordingBasePath,
		player->getRecordingPreset(),
		benchmarkAudioSampleRate,
		benchmarkAudioChannelCount))) {
		windowRecordingActive = false;
		return;
	}
	windowRecordingActive = true;
}

void ofApp::cycleReadbackPolicy() {
	if (!player) {
		return;
	}

	const ofxVlc4VideoReadbackPolicy currentPolicy = player->getVideoReadbackPolicy();
	player->setVideoReadbackPolicy(
		currentPolicy == ofxVlc4VideoReadbackPolicy::DropLateFrames
			? ofxVlc4VideoReadbackPolicy::BlockForFreshestFrame
			: ofxVlc4VideoReadbackPolicy::DropLateFrames);
}

void ofApp::adjustReadbackBufferCount(int delta) {
	if (!player) {
		return;
	}

	const size_t currentCount = player->getVideoReadbackBufferCount();
	const size_t nextCount = static_cast<size_t>(std::clamp<int>(static_cast<int>(currentCount) + delta, 2, 4));
	player->setVideoReadbackBufferCount(nextCount);
}

void ofApp::toggleNativeRecording() {
	if (!player) {
		return;
	}

	if (!player->isNativeRecordingEnabled() && player->isAudioRecording()) {
		player->recordAudio(audioRecordingBasePath);
	}
	player->setNativeRecordingEnabled(!player->isNativeRecordingEnabled());
}

void ofApp::toggleAudioRecording() {
	if (!player) {
		return;
	}

	if (!player->isAudioRecording() && player->isNativeRecordingEnabled()) {
		player->setNativeRecordingEnabled(false);
	}
	player->recordAudio(audioRecordingBasePath);
}

void ofApp::takeSnapshot() {
	if (!player) {
		return;
	}

	player->takeSnapshot(snapshotDirectory);
}

std::string ofApp::currentMediaLabel() const {
	if (isWindowCaptureRecordingActive()) {
		return "app window";
	}
	if (benchmarkModeActive) {
		return "benchmark texture";
	}
	if (!player) {
		return "none";
	}

	const std::string currentPath = player->getCurrentPath();
	return currentPath.empty() ? "none" : ofFilePath::getFileName(currentPath);
}

std::string ofApp::playbackLabel() const {
	if (isWindowCaptureRecordingActive()) {
		return "Window recording";
	}
	if (benchmarkModeActive) {
		return isBenchmarkRecordingActive() ? "Benchmark recording" : "Benchmark preview";
	}
	if (!player) {
		return "closed";
	}

	const ofxVlc4::PlaybackStateInfo state = player->getPlaybackStateInfo();
	if (state.playing) {
		return "Playing";
	}
	if (state.transitioning) {
		return "Transitioning";
	}
	if (state.pauseRequested || player->canPause()) {
		return "Paused/ready";
	}
	if (state.stopped) {
		return "Stopped";
	}
	return "Idle";
}

bool ofApp::isBenchmarkRecordingActive() const {
	return benchmarkVideoRecordingActive ||
		(player &&
			benchmarkModeActive &&
			!player->isWindowRecording() &&
			(player->isVideoRecording() || player->isAudioRecording()));
}

bool ofApp::isWindowCaptureRecordingActive() const {
	return windowRecordingActive || (player && player->isWindowRecording());
}

void ofApp::requestBenchmarkRecordingStop() {
	if (!player) {
		return;
	}

	benchmarkVideoRecordingActive = false;
	player->stopRecordingSession();
}

void ofApp::requestWindowRecordingStop() {
	if (!player) {
		return;
	}

	windowRecordingActive = false;
	player->stopRecordingSession();
}

std::string ofApp::recordingSessionStateLabel() const {
	if (!player) {
		return "Closed";
	}
	return ofxVlc4::recordingSessionStateLabel(player->getRecordingSessionState());
}

bool ofApp::isRecordingBusy() const {
	if (!player) {
		return false;
	}

	const ofxVlc4RecordingSessionState sessionState = player->getRecordingSessionState();
	const bool sessionBusy =
		sessionState == ofxVlc4RecordingSessionState::Capturing ||
		sessionState == ofxVlc4RecordingSessionState::Stopping ||
		sessionState == ofxVlc4RecordingSessionState::Finalizing ||
		sessionState == ofxVlc4RecordingSessionState::Muxing;
	return sessionBusy ||
		player->isNativeRecordingEnabled() ||
		player->isVideoRecording() ||
		player->isAudioRecording() ||
		player->isWindowRecording();
}

void ofApp::drawControlPanel() {
	if (!player) {
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(std::max(24.0f, ofGetWidth() - 410.0f), 24.0f), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(386.0f, std::min(720.0f, ofGetHeight() - 48.0f)), ImGuiCond_Once);

	const bool muxBusy = player->isRecordingMuxPending() || player->isRecordingMuxInProgress();
	const bool benchmarkActive = benchmarkVideoRecordingActive || isBenchmarkRecordingActive();
	const bool windowActive = windowRecordingActive || isWindowCaptureRecordingActive();
	const bool nativeActive = player->isNativeRecordingEnabled();
	const bool audioActive = player->isAudioRecording();
	const std::string muxState = recordingSessionStateLabel();

	if (ImGui::Begin("Recorder Controls", nullptr, ImGuiWindowFlags_NoCollapse)) {
		ImGui::Text("Mode: %s", benchmarkModeActive ? "Benchmark" : "Playback");
		ImGui::TextWrapped("Media: %s", currentMediaLabel().c_str());
		ImGui::Text("Playback: %s", playbackLabel().c_str());
		ImGui::Text("Recording: %s", muxState.c_str());

		const std::string muxedPath = player->getLastMuxedRecordingPath();
		if (!muxedPath.empty()) {
			ImGui::TextWrapped("Last muxed: %s", ofFilePath::getFileName(muxedPath).c_str());
		}
		const std::string muxError = player->getLastMuxError();
		if (!muxError.empty()) {
			ImGui::TextWrapped("Mux note: %s", muxError.c_str());
		}

		ImGui::SeparatorText("Transport");
		if (ImGui::Button("Open...", ImVec2(88, 0))) {
			ofFileDialogResult result = ofSystemLoadDialog("Choose media file");
			if (result.bSuccess) {
				loadMediaPath(result.getPath(), true);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button(player->isPlaying() ? "Pause" : "Play", ImVec2(88, 0))) {
			if (player->isPlaying()) {
				player->pause();
			} else {
				player->play();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop", ImVec2(88, 0))) {
			if (benchmarkActive) {
				requestBenchmarkRecordingStop();
			} else if (windowActive) {
				requestWindowRecordingStop();
			} else {
				player->stop();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Snap", ImVec2(88, 0))) {
			takeSnapshot();
		}

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

		int volume = player->getVolume();
		if (ImGui::SliderInt("Volume", &volume, 0, 100)) {
			player->setVolume(volume);
		}

		ImGui::SeparatorText("Recorder");
		bool benchmarkMode = benchmarkModeActive;
		if (ImGui::Checkbox("Benchmark mode", &benchmarkMode)) {
			toggleBenchmarkMode();
		}

		ofxVlc4RecordingPreset recordingPreset = player->getRecordingPreset();
		bool presetChanged = false;
		const char * audioSourceItems[] = { "OpenFrameworks", "VLC playback" };
		int audioSourceIndex = recordingPreset.audioSource == ofxVlc4RecordingAudioSource::ExternalSubmitted ? 0 : 1;
		ImGui::BeginDisabled(benchmarkActive);
		if (ImGui::Combo("Benchmark audio", &audioSourceIndex, audioSourceItems, IM_ARRAYSIZE(audioSourceItems))) {
			recordingPreset.audioSource = audioSourceIndex == 0
				? ofxVlc4RecordingAudioSource::ExternalSubmitted
				: ofxVlc4RecordingAudioSource::VlcCaptured;
			presetChanged = true;
		}

		const ofxVlc4RecordingVideoCodecPreset codecPresets[] = {
			ofxVlc4RecordingVideoCodecPreset::H264,
			ofxVlc4RecordingVideoCodecPreset::H265,
			ofxVlc4RecordingVideoCodecPreset::Mp4v,
			ofxVlc4RecordingVideoCodecPreset::Mjpg,
			ofxVlc4RecordingVideoCodecPreset::Hap,
			ofxVlc4RecordingVideoCodecPreset::HapAlpha,
			ofxVlc4RecordingVideoCodecPreset::HapQ,
			ofxVlc4RecordingVideoCodecPreset::H264_NVENC,
			ofxVlc4RecordingVideoCodecPreset::H265_NVENC,
			ofxVlc4RecordingVideoCodecPreset::H264_QSV,
			ofxVlc4RecordingVideoCodecPreset::H265_QSV,
			ofxVlc4RecordingVideoCodecPreset::H264_VAAPI,
			ofxVlc4RecordingVideoCodecPreset::H265_VAAPI,
			ofxVlc4RecordingVideoCodecPreset::H264_AMF,
			ofxVlc4RecordingVideoCodecPreset::H265_AMF,
			ofxVlc4RecordingVideoCodecPreset::H264_MFT,
			ofxVlc4RecordingVideoCodecPreset::H265_MFT
		};
		int codecIndex = 0;
		for (size_t i = 0; i < std::size(codecPresets); ++i) {
			if (codecPresets[i] == recordingPreset.videoCodecPreset) {
				codecIndex = static_cast<int>(i);
				break;
			}
		}
		if (ImGui::BeginCombo("Video codec", ofxVlc4::recordingVideoCodecPresetLabel(codecPresets[codecIndex]))) {
			for (int i = 0; i < static_cast<int>(std::size(codecPresets)); ++i) {
				const bool selected = i == codecIndex;
				if (ImGui::Selectable(ofxVlc4::recordingVideoCodecPresetLabel(codecPresets[i]), selected)) {
					recordingPreset.videoCodecPreset = codecPresets[i];
					presetChanged = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		const char * muxProfileItems[] = {
			ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile::Mp4Aac),
			ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile::MkvOpus),
			ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile::MkvFlac),
			ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile::MkvLpcm),
			ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile::OggVorbis),
			ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile::MovAac)
		};
		int muxProfileIndex = recordingPreset.muxProfile == ofxVlc4RecordingMuxProfile::Mp4Aac
			? 0
			: recordingPreset.muxProfile == ofxVlc4RecordingMuxProfile::MkvOpus
				? 1
				: recordingPreset.muxProfile == ofxVlc4RecordingMuxProfile::MkvFlac
					? 2
					: recordingPreset.muxProfile == ofxVlc4RecordingMuxProfile::MkvLpcm
						? 3
						: recordingPreset.muxProfile == ofxVlc4RecordingMuxProfile::OggVorbis
							? 4
							: 5;
		if (ImGui::Combo("Mux profile", &muxProfileIndex, muxProfileItems, IM_ARRAYSIZE(muxProfileItems))) {
			recordingPreset.muxProfile = muxProfileIndex == 0
				? ofxVlc4RecordingMuxProfile::Mp4Aac
				: muxProfileIndex == 1
					? ofxVlc4RecordingMuxProfile::MkvOpus
					: muxProfileIndex == 2
						? ofxVlc4RecordingMuxProfile::MkvFlac
						: muxProfileIndex == 3
							? ofxVlc4RecordingMuxProfile::MkvLpcm
							: muxProfileIndex == 4
								? ofxVlc4RecordingMuxProfile::OggVorbis
								: ofxVlc4RecordingMuxProfile::MovAac;
			presetChanged = true;
		}
		if (const std::string compatibilityMessage =
				ofxVlc4::recordingMuxProfileCompatibilityMessage(
					recordingPreset.muxProfile,
					recordingPreset.videoCodecPreset);
			!compatibilityMessage.empty()) {
			ImGui::TextWrapped("%s", compatibilityMessage.c_str());
		}

		bool deleteTempSources = recordingPreset.deleteMuxSourceFilesOnSuccess;
		if (ImGui::Checkbox("Delete temp mux sources", &deleteTempSources)) {
			recordingPreset.deleteMuxSourceFilesOnSuccess = deleteTempSources;
			presetChanged = true;
		}

		int outputWidth = recordingPreset.targetWidth;
		int outputHeight = recordingPreset.targetHeight;
		if (ImGui::InputInt("Output width", &outputWidth, 0, 0)) {
			recordingPreset.targetWidth = std::max(0, outputWidth);
			presetChanged = true;
		}
		if (ImGui::InputInt("Output height", &outputHeight, 0, 0)) {
			recordingPreset.targetHeight = std::max(0, outputHeight);
			presetChanged = true;
		}
		ImGui::TextDisabled("0 x 0 keeps the source size.");

		int recordingFps = recordingPreset.videoFrameRate;
		if (ImGui::SliderInt("Recording FPS", &recordingFps, 1, 120)) {
			recordingPreset.videoFrameRate = recordingFps;
			presetChanged = true;
		}

		int videoBitrate = recordingPreset.videoBitrateKbps;
		if (ImGui::InputInt("Video bitrate (kbps)", &videoBitrate, 0, 0)) {
			recordingPreset.videoBitrateKbps = std::max(0, videoBitrate);
			presetChanged = true;
		}

		int audioBitrate = recordingPreset.audioBitrateKbps;
		if (ImGui::InputInt("Mux audio bitrate (kbps)", &audioBitrate, 0, 0)) {
			recordingPreset.audioBitrateKbps = std::max(0, audioBitrate);
			presetChanged = true;
		}

		int muxTimeoutSec = static_cast<int>(recordingPreset.muxTimeoutMs / 1000);
		if (ImGui::SliderInt("Mux timeout (s)", &muxTimeoutSec, 5, 120)) {
			recordingPreset.muxTimeoutMs = static_cast<uint64_t>(std::max(5, muxTimeoutSec)) * 1000;
			presetChanged = true;
		}

		float audioRingBuf = static_cast<float>(recordingPreset.audioRingBufferSeconds);
		if (ImGui::SliderFloat("Audio ring buffer (s)", &audioRingBuf, 1.0f, 30.0f, "%.1f")) {
			recordingPreset.audioRingBufferSeconds = static_cast<double>(audioRingBuf);
			presetChanged = true;
		}

		int readbackPolicyIndex = player->getVideoReadbackPolicy() == ofxVlc4VideoReadbackPolicy::DropLateFrames ? 0 : 1;
		const char * readbackPolicyItems[] = { "Drop late frames", "Block for freshest" };
		if (ImGui::Combo("Readback policy", &readbackPolicyIndex, readbackPolicyItems, IM_ARRAYSIZE(readbackPolicyItems))) {
			player->setVideoReadbackPolicy(
				readbackPolicyIndex == 0
					? ofxVlc4VideoReadbackPolicy::DropLateFrames
					: ofxVlc4VideoReadbackPolicy::BlockForFreshestFrame);
		}

		int readbackBuffers = static_cast<int>(player->getVideoReadbackBufferCount());
		if (ImGui::SliderInt("Readback buffers", &readbackBuffers, 2, 4)) {
			player->setVideoReadbackBufferCount(static_cast<size_t>(readbackBuffers));
		}
		if (presetChanged) {
			player->setRecordingPreset(recordingPreset);
		}
		ImGui::EndDisabled();

		if (ImGui::Button(benchmarkActive ? "Stop Benchmark AV" : "Start Benchmark AV", ImVec2(-1, 0))) {
			toggleBenchmarkVideoRecording();
		}
		if (ImGui::Button(windowActive ? "Stop Window AV" : "Start Window AV", ImVec2(-1, 0))) {
			if (windowActive) {
				requestWindowRecordingStop();
			} else {
				toggleWindowRecording();
			}
		}

		if (ImGui::Button(nativeActive ? "Stop Native VLC Record" : "Start Native VLC Record", ImVec2(-1, 0))) {
			toggleNativeRecording();
		}
		if (ImGui::Button(audioActive ? "Stop WAV Audio" : "Start WAV Audio", ImVec2(-1, 0))) {
			toggleAudioRecording();
		}

		ImGui::SeparatorText("Status");
		ImGui::TextWrapped("%s", overlayStatusLine().c_str());
		ImGui::TextWrapped("Keyboard fallback: V benchmark, W window, S stop.");
	}
	ImGui::End();
}

std::string ofApp::overlayStatusLine() const {
	if (!player) {
		return "Player closed";
	}

	const ofxVlc4::PlaybackStateInfo state = player->getPlaybackStateInfo();
	if (!player->getLastErrorMessage().empty()) {
		return "Error: " + player->getLastErrorMessage();
	}
	if (!player->getLastStatusMessage().empty()) {
		return player->getLastStatusMessage();
	}
	if (!state.nativeRecording.lastFailureReason.empty()) {
		return "Native record error: " + state.nativeRecording.lastFailureReason;
	}
	if (!state.snapshot.lastFailureReason.empty()) {
		return "Snapshot error: " + state.snapshot.lastFailureReason;
	}
	if (!state.nativeRecording.lastOutputPath.empty()) {
		return "Last native output: " + state.nativeRecording.lastOutputPath;
	}
	if (!state.snapshot.lastSavedPath.empty()) {
		return "Last snapshot: " + state.snapshot.lastSavedPath;
	}
	if (benchmarkModeActive) {
		return "Benchmark mode ready";
	}
	return "Ready";
}
