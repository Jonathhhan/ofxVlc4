#include "ofxVlc4.h"
#include "ofxVlc4Recorder.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <thread>

namespace {

struct RecordingOutputPaths {
	std::string audioPath;
	std::string videoPath;
};

std::string trimRecorderText(const std::string & value) {
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}

	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, (last - first) + 1);
}

std::string buildRecordingOutputStem(const std::string & name, std::string * extensionOut = nullptr) {
	std::string normalizedName = trimRecorderText(name);
	if (normalizedName.empty()) normalizedName = "recording";

	const std::string detectedExtension = ofFilePath::getFileExt(normalizedName);
	if (extensionOut) *extensionOut = detectedExtension;
	if (!detectedExtension.empty()) normalizedName = ofFilePath::removeExt(normalizedName);

	return normalizedName + ofGetTimestampString("-%Y-%m-%d-%H-%M-%S");
}

std::string buildRecordingOutputPath(const std::string & name, const std::string & fallbackExtension) {
	std::string detectedExtension;
	const std::string outputStem = buildRecordingOutputStem(name, &detectedExtension);
	return !detectedExtension.empty()
		? outputStem + "." + detectedExtension
		: outputStem + fallbackExtension;
}

RecordingOutputPaths buildRecordingOutputPaths(const std::string & name) {
	const std::string outputStem = buildRecordingOutputStem(name);
	return {outputStem + ".wav", outputStem + ".mp4"};
}

std::string normalizeSoutPath(const std::string & path) {
	std::string normalized = std::filesystem::path(path).lexically_normal().generic_string();
	size_t position = 0;
	while ((position = normalized.find('\'', position)) != std::string::npos) {
		normalized.insert(position, "\\");
		position += 2;
	}
	return normalized;
}

std::string pathToFileUri(const std::string & path) {
	const std::string genericPath = std::filesystem::absolute(path).lexically_normal().generic_string();
	std::ostringstream uri;
	uri << "file:///";
	for (const unsigned char ch : genericPath) {
		if ((ch >= 'A' && ch <= 'Z') ||
			(ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9') ||
			ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
			ch == '/' || ch == ':') {
			uri << static_cast<char>(ch);
		} else {
			static constexpr char kHexDigits[] = "0123456789ABCDEF";
			uri << '%' << kHexDigits[(ch >> 4) & 0x0F] << kHexDigits[ch & 0x0F];
		}
	}
	return uri.str();
}

bool waitForRecordingFile(const std::string & path, uint64_t timeoutMs) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	uintmax_t previousSize = 0;
	while (std::chrono::steady_clock::now() < deadline) {
		std::error_code error;
		if (std::filesystem::exists(path, error)) {
			const uintmax_t currentSize = std::filesystem::file_size(path, error);
			if (!error && currentSize > 0) {
				if (currentSize == previousSize) {
					return true;
				}
				previousSize = currentSize;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return previousSize > 0;
}

bool waitForRecordingFile(
	const std::string & path,
	uint64_t timeoutMs,
	const std::atomic<bool> * cancelRequested) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	uintmax_t previousSize = 0;
	while (std::chrono::steady_clock::now() < deadline) {
		if (cancelRequested && cancelRequested->load(std::memory_order_acquire)) {
			return false;
		}

		std::error_code error;
		if (std::filesystem::exists(path, error)) {
			const uintmax_t currentSize = std::filesystem::file_size(path, error);
			if (!error && currentSize > 0) {
				if (currentSize == previousSize) {
					return true;
				}
				previousSize = currentSize;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return previousSize > 0;
}

bool removeRecordingFile(const std::string & path, uint64_t timeoutMs) {
	if (path.empty()) {
		return true;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	do {
		std::error_code error;
		if (!std::filesystem::exists(path, error)) {
			return !error;
		}
		if (std::filesystem::remove(path, error)) {
			return true;
		}
		if (!error && !std::filesystem::exists(path, error)) {
			return !error;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	} while (std::chrono::steady_clock::now() < deadline);

	std::error_code error;
	return !std::filesystem::exists(path, error);
}

}

bool ofxVlc4::muxRecordingFilesInternal(
	const std::string & videoPath,
	const std::string & audioPath,
	const std::string & outputPath,
	const ofxVlc4MuxOptions & options,
	const std::atomic<bool> * cancelRequested,
	std::string * errorOut) {
	const std::string normalizedMux = ofToLower(trimRecorderText(options.containerMux));
	const std::string normalizedAudioCodec = ofToLower(trimRecorderText(options.audioCodec));
	if (normalizedMux.empty()) {
		if (errorOut) {
			*errorOut = "Mux container is empty.";
		}
		return false;
	}
	if (normalizedAudioCodec.empty()) {
		if (errorOut) {
			*errorOut = "Mux audio codec is empty.";
		}
		return false;
	}

	const auto fail = [&](const std::string & message) {
		if (errorOut) {
			*errorOut = message;
		}
		return false;
	};

	if (videoPath.empty() || audioPath.empty() || outputPath.empty()) {
		return fail("Video, audio, and output paths are required for muxing.");
	}
	if (cancelRequested && cancelRequested->load(std::memory_order_acquire)) {
		return fail("Recording mux cancelled.");
	}
	if (!waitForRecordingFile(videoPath, options.inputReadyTimeoutMs, cancelRequested)) {
		return fail(cancelRequested && cancelRequested->load(std::memory_order_acquire)
			? "Recording mux cancelled."
			: "Timed out waiting for recorded video file.");
	}
	if (!waitForRecordingFile(audioPath, options.inputReadyTimeoutMs, cancelRequested)) {
		return fail(cancelRequested && cancelRequested->load(std::memory_order_acquire)
			? "Recording mux cancelled."
			: "Timed out waiting for recorded audio file.");
	}

	std::error_code error;
	if (const auto outputDirectory = std::filesystem::path(outputPath).parent_path(); !outputDirectory.empty()) {
		std::filesystem::create_directories(outputDirectory, error);
		if (error) {
			return fail("Failed to create mux output directory.");
		}
	}
	std::filesystem::remove(outputPath, error);

	const char * const args[] = {
		"--intf=dummy",
		"--vout=dummy",
		"--aout=dummy",
		"--quiet",
		"--verbose=-1",
		"--no-video-title-show",
		"--ignore-config"
	};
	libvlc_instance_t * muxInstance = libvlc_new(static_cast<int>(sizeof(args) / sizeof(args[0])), args);
	if (!muxInstance) {
		return fail("Failed to create libVLC mux instance.");
	}

	bool success = false;
	libvlc_media_t * media = libvlc_media_new_path(videoPath.c_str());
	if (!media) {
		libvlc_release(muxInstance);
		return fail("Failed to create mux media.");
	}

	const std::string audioUri = pathToFileUri(audioPath);
	if (libvlc_media_slaves_add(media, libvlc_media_slave_type_audio, 4u, audioUri.c_str()) != 0) {
		libvlc_media_release(media);
		libvlc_release(muxInstance);
		return fail("Failed to attach recorded audio as a libVLC slave.");
	}

	const int normalizedChannels = std::max(1, options.audioChannels);
	const int normalizedSampleRate = std::max(8000, options.audioSampleRate);
	std::string transcodeSpec =
		"acodec=" + normalizedAudioCodec +
		",channels=" + ofToString(normalizedChannels) +
		",samplerate=" + ofToString(normalizedSampleRate);
	if (options.audioBitrateKbps > 0 && normalizedAudioCodec != "flac") {
		transcodeSpec += ",ab=" + ofToString(options.audioBitrateKbps);
	}
	const std::string streamSpec =
		"sout=#transcode{" + transcodeSpec + "}:standard{access=file,mux=" + normalizedMux + ",dst='" +
		normalizeSoutPath(outputPath) + "'}";
	libvlc_media_add_option(media, streamSpec.c_str());

	libvlc_media_player_t * muxPlayer = libvlc_media_player_new_from_media(muxInstance, media);
	if (!muxPlayer) {
		libvlc_media_release(media);
		libvlc_release(muxInstance);
		return fail("Failed to create libVLC mux player.");
	}

	if (libvlc_media_player_play(muxPlayer) != 0) {
		libvlc_media_player_release(muxPlayer);
		libvlc_media_release(media);
		libvlc_release(muxInstance);
		return fail("Failed to start libVLC mux playback.");
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.muxTimeoutMs);
	while (std::chrono::steady_clock::now() < deadline) {
		if (cancelRequested && cancelRequested->load(std::memory_order_acquire)) {
			break;
		}

		const libvlc_state_t state = libvlc_media_player_get_state(muxPlayer);
		if (state == libvlc_Stopped) {
			success = waitForRecordingFile(outputPath, options.outputReadyTimeoutMs, cancelRequested);
			break;
		}
		if (state == libvlc_Error) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	libvlc_media_player_stop_async(muxPlayer);
	libvlc_media_player_release(muxPlayer);
	libvlc_media_release(media);
	libvlc_release(muxInstance);

	if (cancelRequested && cancelRequested->load(std::memory_order_acquire)) {
		std::filesystem::remove(outputPath, error);
		return fail("Recording mux cancelled.");
	}
	if (!success) {
		return fail("Timed out or failed while muxing benchmark recording.");
	}
	return true;
}

bool ofxVlc4::startNamedAudioCaptureSession(const std::string & audioPath) {
	if (std::string recorderError; !recorder.startAudioCapture(
		audioPath,
		sampleRate.load(),
		channels.load(),
		recorderError)) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		setError(recorderError);
		return false;
	}
	setRecordingSessionState(ofxVlc4RecordingSessionState::Capturing);
	return recorder.isAudioCaptureActive();
}

bool ofxVlc4::startRecordingSession(const ofxVlc4RecordingSessionConfig & config) {
	if (config.outputBasePath.empty()) {
		clearRecordingSessionConfig();
		setError("Recording output path is empty.");
		return false;
	}

	if (config.muxOnStop) {
		const ofxVlc4RecordingVideoCodecPreset videoCodecPreset = getVideoRecordingCodecPreset();
		const ofxVlc4RecordingMuxProfile muxProfile = getRecordingMuxProfile();
		if (const std::string compatibilityMessage =
				recordingMuxProfileCompatibilityMessage(muxProfile, videoCodecPreset);
			!compatibilityMessage.empty()) {
			clearRecordingSessionConfig();
			setError(compatibilityMessage);
			return false;
		}
	}

	ofxVlc4RecordingStartOptions options;
	options.includeAudioCapture = config.audioSource != ofxVlc4RecordingAudioSource::None;

	bool started = false;
	switch (config.source) {
	case ofxVlc4RecordingSource::Texture:
		if (!config.texture) {
			clearRecordingSessionConfig();
			setError("Texture recording requires a source texture.");
			return false;
		}
		{
			const unsigned sourceWidth = static_cast<unsigned>(std::max(0, static_cast<int>(config.texture->getWidth())));
			const unsigned sourceHeight = static_cast<unsigned>(std::max(0, static_cast<int>(config.texture->getHeight())));
			if (config.targetWidth > 0 && config.targetHeight > 0) {
				options.outputWidth = config.targetWidth;
				options.outputHeight = config.targetHeight;
			} else if (config.targetWidth > 0 && sourceWidth > 0 && sourceHeight > 0) {
				options.outputWidth = config.targetWidth;
				options.outputHeight = std::max(
					1,
					static_cast<int>(std::llround(
						static_cast<double>(config.targetWidth) * static_cast<double>(sourceHeight) /
						static_cast<double>(sourceWidth))));
			} else if (config.targetHeight > 0 && sourceWidth > 0 && sourceHeight > 0) {
				options.outputHeight = config.targetHeight;
				options.outputWidth = std::max(
					1,
					static_cast<int>(std::llround(
						static_cast<double>(config.targetHeight) * static_cast<double>(sourceWidth) /
						static_cast<double>(sourceHeight))));
			} else {
				options.outputWidth = static_cast<int>(sourceWidth);
				options.outputHeight = static_cast<int>(sourceHeight);
			}
			started = startNamedTextureCaptureSession(config.outputBasePath, *config.texture, options);
		}
		break;
	case ofxVlc4RecordingSource::Window:
		if (!mainWindow) {
			mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
		}

		const unsigned sourceWidth = static_cast<unsigned>(std::max(0, ofGetWidth()));
		const unsigned sourceHeight = static_cast<unsigned>(std::max(0, ofGetHeight()));
		const unsigned captureWidth = sourceWidth;
		const unsigned captureHeight = sourceHeight;
		if (captureWidth == 0 || captureHeight == 0) {
			clearRecordingSessionConfig();
			setError("Window recording requires a visible window size.");
			return false;
		}
		options.outputWidth = config.targetWidth > 0 ? config.targetWidth : static_cast<int>(captureWidth);
		options.outputHeight = config.targetHeight > 0 ? config.targetHeight : static_cast<int>(captureHeight);
		if (!ensureWindowCaptureTarget(captureWidth, captureHeight)) {
			clearRecordingSessionConfig();
			return false;
		}

		windowCaptureRuntime.includeAudioCapture = options.includeAudioCapture;
		windowCaptureRuntime.active = true;
		registerWindowCaptureListener();
		captureCurrentWindowBackbuffer();
		if (!startNamedTextureCaptureSession(config.outputBasePath, windowCaptureRuntime.captureFbo.getTexture(), options)) {
			windowCaptureRuntime.active = false;
			unregisterWindowCaptureListener();
			clearRecordingSessionConfig();
			return false;
		}
		if (!recorder.isVideoCaptureActive()) {
			windowCaptureRuntime.active = false;
			unregisterWindowCaptureListener();
			clearRecordingSessionConfig();
			return false;
		}

		setStatus(
			options.includeAudioCapture
				? "Window audio/video recording started."
				: "Window video recording started.");
		started = true;
		break;
	}

	if (started) {
		storeRecordingSessionConfig(config);
	} else {
		clearRecordingSessionConfig();
	}
	return started;
}

bool ofxVlc4::startNamedTextureCaptureSession(
	std::string name,
	const ofTexture & texture,
	const ofxVlc4RecordingStartOptions & options) {
	if (!ensureRecorderSessionCanStart(options.includeAudioCapture)) {
		return false;
	}
	if (!sessionInstance()) {
		setError("Initialize libvlc first.");
		return false;
	}
	libvlc_media_player_t * mediaPlayer = sessionPlayer();
	if (!mediaPlayer) {
		setError("Initialize the media player first.");
		return false;
	}
	if (!texture.isAllocated() || texture.getWidth() <= 0 || texture.getHeight() <= 0) {
		setError("Texture is not allocated.");
		return false;
	}
	if (recorder.getVideoCaptureFrameRate() <= 0) {
		setError("Video recording frame rate must be positive.");
		return false;
	}
	if (recorder.getVideoCaptureCodec().empty()) {
		setError("Video recording codec is empty.");
		return false;
	}

	const bool usesPlayerOutputTexture =
		(exposedTextureFbo.isAllocated() &&
			texture.getTextureData().textureID == exposedTextureFbo.getTexture().getTextureData().textureID) ||
		(videoTexture.isAllocated() &&
 		 texture.getTextureData().textureID == videoTexture.getTextureData().textureID);
	if (usesPlayerOutputTexture) {
		setError("Recording from the player's own output texture is not supported.");
		return false;
	}

	if (libvlc_media_t * currentMedia = libvlc_media_player_get_media(mediaPlayer)) {
		libvlc_media_release(currentMedia);
		if (!isStopped()) {
			setError("Stop current playback before recording.");
			return false;
		}
	}

	const auto failTextureRecordingStart = [&](const std::string & message) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		setError(message);
		if (options.includeAudioCapture) recorder.clearAudioRecording();
		return false;
	};
	const auto startTextureRecordingPlayback = [&](const std::string & videoPath, const std::string & startedStatus) {
		std::string recorderError;
		libvlc_media_t * recordingMedia = recorder.beginVideoCapture(
			texture,
			videoPath,
			options.outputWidth,
			options.outputHeight,
			recorderError);
		if (!recordingMedia) {
			return failTextureRecordingStart(recorderError);
		}

		clearCurrentMedia();
		media = recordingMedia;
		if (coreSession) coreSession->setMedia(media);
		libvlc_media_player_set_media(mediaPlayer, media);
		applyCurrentMediaPlayerSettings();
		applySelectedRenderer();
		const int playResult = libvlc_media_player_play(mediaPlayer);
		if (playResult != 0) {
			clearCurrentMedia(false);
			recorder.clearVideoRecording();
			return failTextureRecordingStart("Failed to start recording playback.");
		}

		setRecordingSessionState(ofxVlc4RecordingSessionState::Capturing);
		setStatus(startedStatus);
		return true;
	};
	if (!options.includeAudioCapture) {
		const std::string videoPath = buildRecordingOutputPath(name, ".mp4");
		return startTextureRecordingPlayback(videoPath, "Video recording started: " + videoPath);
	}

	const RecordingOutputPaths outputPaths = buildRecordingOutputPaths(name);
	if (!startTextureRecordingPlayback(outputPaths.videoPath, "Video recording started: " + outputPaths.videoPath)) {
		return false;
	}

	if (!startNamedAudioCaptureSession(outputPaths.audioPath)) {
		stop();
		return false;
	}

	setStatus("Audio/video recording started: " + outputPaths.videoPath + " and " + outputPaths.audioPath);
	return true;
}

bool ofxVlc4::ensureRecorderSessionCanStart(bool requireAudioCapture) {
	if (recorder.hasActiveCaptureSession()) {
		setError("Stop the current recording session first.");
		return false;
	}
	if (requireAudioCapture && !audioCaptureEnabled) {
		setError("Enable audio capture before recording audio.");
		return false;
	}
	return true;
}

void ofxVlc4::stopActiveRecorderSessions() {
	const bool hadActiveSession = recorder.hasActiveCaptureSession();
	recorder.clearCaptureState();
	clearRecordingSessionConfig();
	if (hadActiveSession && !recordingMuxRuntime.pending.load() && !recordingMuxRuntime.inProgress.load()) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Done);
	}
}

void ofxVlc4::recordAudio(std::string name) {
	if (recorder.isAudioCaptureActive() && !recorder.isVideoCaptureActive()) {
		setStatus("Audio recording saved to " + recorder.finishAudioCapture() + ".");
		clearRecordingSessionConfig();
		return;
	}
	if (!ensureRecorderSessionCanStart(true)) {
		return;
	}

	const std::string audioPath = buildRecordingOutputPath(name, ".wav");
	clearRecordingSessionConfig();
	if (!startNamedAudioCaptureSession(audioPath)) {
		return;
	}

	setStatus("Audio recording started: " + audioPath);
}

void ofxVlc4::recordVideo(std::string name, const ofTexture & texture) {
	startTextureRecordingSession(name, texture);
}

void ofxVlc4::recordAudioVideo(std::string name, const ofTexture & texture) {
	ofxVlc4RecordingStartOptions options;
	options.includeAudioCapture = true;
	startTextureRecordingSession(name, texture, options);
}

bool ofxVlc4::startTextureRecordingSession(
	std::string name,
	const ofTexture & texture,
	const ofxVlc4RecordingStartOptions & options) {
	ofxVlc4RecordingSessionConfig config;
	config.outputBasePath = std::move(name);
	config.source = ofxVlc4RecordingSource::Texture;
	config.texture = &texture;
	config.audioSource = options.includeAudioCapture
		? ofxVlc4RecordingAudioSource::VlcCaptured
		: ofxVlc4RecordingAudioSource::None;
	return startRecordingSession(config);
}

bool ofxVlc4::startWindowRecordingSession(
	std::string name,
	const ofxVlc4RecordingStartOptions & options) {
	ofxVlc4RecordingSessionConfig config;
	config.outputBasePath = std::move(name);
	config.source = ofxVlc4RecordingSource::Window;
	config.audioSource = options.includeAudioCapture
		? ofxVlc4RecordingAudioSource::VlcCaptured
		: ofxVlc4RecordingAudioSource::None;
	return startRecordingSession(config);
}

bool ofxVlc4::beginWindowRecording(std::string name, bool includeAudioCapture) {
	ofxVlc4RecordingStartOptions options;
	options.includeAudioCapture = includeAudioCapture;
	return startWindowRecordingSession(std::move(name), options);
}

void ofxVlc4::endWindowRecording() {
	windowCaptureRuntime.active = false;
	unregisterWindowCaptureListener();
	if (recorder.isVideoCaptureActive()) {
		stopRecordingSessionInternal(true);
	}
}

bool ofxVlc4::isWindowRecording() const {
	return windowCaptureRuntime.active && recorder.isVideoCaptureActive();
}

bool ofxVlc4::armPendingRecordingMux(const std::string & outputPath, const ofxVlc4MuxOptions & options) {
	finalizeRecordingMuxThread();
	if (recordingMuxRuntime.pending.load() || recordingMuxRuntime.inProgress.load()) {
		setStatus("Recording finalize/mux already in progress.");
		return false;
	}

	if (!recorder.isVideoCaptureActive()) {
		setError("Muxing requires an active video recording session.");
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		return false;
	}

	const std::string previousVideoPath = recorder.getLastFinishedVideoPath();
	const std::string previousAudioPath = recorder.getLastFinishedAudioPath();
	std::string expectedVideoPath;
	std::string expectedAudioPath;
	{
		std::lock_guard<std::mutex> recordingLock(recorder.recordingMutex);
		expectedVideoPath = recorder.videoOutputPath;
	}
	{
		std::lock_guard<std::mutex> audioLock(recorder.audioRecordingMutex);
		expectedAudioPath = recorder.outputPath;
	}
	const std::string resolvedExpectedVideoPath =
		!expectedVideoPath.empty() ? expectedVideoPath : recorder.getLastFinishedVideoPath();
	const std::string resolvedExpectedAudioPath =
		!expectedAudioPath.empty() ? expectedAudioPath : recorder.getLastFinishedAudioPath();
	if (resolvedExpectedVideoPath.empty()) {
		setError("Muxing requires a finalized video recording output.");
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		return false;
	}
	if (resolvedExpectedAudioPath.empty()) {
		setError("Muxing requires a finalized audio recording output.");
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
		recordingMuxRuntime.options = options;
		recordingMuxRuntime.previousVideoPath = previousVideoPath;
		recordingMuxRuntime.previousAudioPath = previousAudioPath;
		recordingMuxRuntime.expectedVideoPath = resolvedExpectedVideoPath;
		recordingMuxRuntime.expectedAudioPath = resolvedExpectedAudioPath;
		recordingMuxRuntime.requestedOutputPath = outputPath;
		recordingMuxRuntime.completedOutputPath.clear();
		recordingMuxRuntime.completedError.clear();
	}
	recordingMuxRuntime.pending.store(true);
	setRecordingSessionState(ofxVlc4RecordingSessionState::Finalizing);
	setStatus("Finalizing recording...");
	return true;
}

bool ofxVlc4::stopRecordingSessionInternal(bool allowConfiguredMux) {
	const bool hadVideoCapture = recorder.isVideoCaptureActive();
	const bool hadAudioCapture = recorder.isAudioCaptureActive();
	if (windowCaptureRuntime.active) {
		windowCaptureRuntime.active = false;
		unregisterWindowCaptureListener();
	}

	if (recorder.isVideoCaptureActive()) {
		if (allowConfiguredMux) {
			ofxVlc4RecordingSessionConfig configCopy;
			bool shouldMuxOnStop = false;
			{
				std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
				shouldMuxOnStop = recordingSessionRuntime.hasConfig && recordingSessionRuntime.config.muxOnStop;
				if (shouldMuxOnStop) {
					configCopy = recordingSessionRuntime.config;
				}
			}
			if (shouldMuxOnStop && !armPendingRecordingMux(configCopy.muxOutputPath, configCopy.muxOptions)) {
				return false;
			}
		}
		setRecordingSessionState(ofxVlc4RecordingSessionState::Stopping);
		stop();
		return true;
	}

	if (recorder.isAudioCaptureActive()) {
		const std::string finishedAudioPath = recorder.finishAudioCapture();
		clearRecordingSessionConfig();
		setRecordingSessionState(ofxVlc4RecordingSessionState::Done);
		setStatus("Audio recording saved to " + finishedAudioPath + ".");
		return true;
	}

	if (!hadVideoCapture && !hadAudioCapture) {
		clearRecordingSessionConfig();
		setRecordingSessionState(ofxVlc4RecordingSessionState::Idle);
	}

	return false;
}

bool ofxVlc4::stopRecordingSession() {
	return stopRecordingSessionInternal(true);
}

bool ofxVlc4::stopRecordingSessionAndMux(const ofxVlc4MuxOptions & options) {
	return stopRecordingSessionAndMux(std::string(), options);
}

bool ofxVlc4::stopRecordingSessionAndMux(const std::string & outputPath, const ofxVlc4MuxOptions & options) {
	if (!armPendingRecordingMux(outputPath, options)) {
		return false;
	}
	return stopRecordingSessionInternal(false);
}

bool ofxVlc4::isRecordingMuxPending() const {
	return recordingMuxRuntime.pending.load();
}

bool ofxVlc4::isRecordingMuxInProgress() const {
	return recordingMuxRuntime.inProgress.load();
}

std::string ofxVlc4::getLastMuxedRecordingPath() const {
	std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
	return recordingMuxRuntime.completedOutputPath;
}

std::string ofxVlc4::getLastMuxError() const {
	std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
	return recordingMuxRuntime.completedError;
}

bool ofxVlc4::startAudioRecordingForActiveVideo(std::string name) {
	if (!recorder.isVideoCaptureActive()) {
		setError("Start video recording before attaching audio.");
		return false;
	}
	if (recorder.isAudioCaptureActive()) {
		return true;
	}
	if (!audioCaptureEnabled) {
		setError("Enable audio capture before recording audio.");
		return false;
	}

	const std::string audioPath = buildRecordingOutputPath(name, ".wav");
	if (!startNamedAudioCaptureSession(audioPath)) {
		return false;
	}

	setStatus("Audio recording attached: " + audioPath);
	return true;
}

void ofxVlc4::setVideoRecordingFrameRate(int fps) {
	if (std::string recorderError; !recorder.setVideoCaptureFrameRate(fps, recorderError)) {
		setError(recorderError);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
		recordingSessionRuntime.preset.videoFrameRate = std::max(1, fps);
	}
}

void ofxVlc4::setVideoRecordingCodec(const std::string & codec) {
	if (std::string recorderError; !recorder.setVideoCaptureCodec(codec, recorderError)) {
		setError(recorderError);
		return;
	}

	ofxVlc4RecordingVideoCodecPreset codecPreset = ofxVlc4RecordingVideoCodecPreset::H264;
	ofxVlc4RecordingMuxProfile muxProfile = ofxVlc4RecordingMuxProfile::Mp4Aac;
	{
		std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
		recordingSessionRuntime.preset.videoCodecPreset = recordingVideoCodecPresetForCodec(codec);
		codecPreset = recordingSessionRuntime.preset.videoCodecPreset;
		muxProfile = recordingSessionRuntime.preset.muxProfile;
	}
	if (const std::string compatibilityMessage = recordingMuxProfileCompatibilityMessage(muxProfile, codecPreset);
		!compatibilityMessage.empty()) {
		setStatus(compatibilityMessage);
	}
}

void ofxVlc4::setVideoRecordingCodecPreset(ofxVlc4RecordingVideoCodecPreset preset) {
	setVideoRecordingCodec(recordingVideoCodecForPreset(preset));
}

void ofxVlc4::setVideoReadbackPolicy(ofxVlc4VideoReadbackPolicy policy) {
	recorder.setVideoReadbackPolicy(policy);
}

ofxVlc4VideoReadbackPolicy ofxVlc4::getVideoReadbackPolicy() const {
	return recorder.getVideoReadbackPolicy();
}

void ofxVlc4::setVideoReadbackBufferCount(size_t bufferCount) {
	recorder.setVideoReadbackBufferCount(bufferCount);
}

size_t ofxVlc4::getVideoReadbackBufferCount() const {
	return recorder.getVideoReadbackBufferCount();
}

void ofxVlc4::updateRecorder() {
	if (const std::string recorderError = recorder.updateCaptureState(); !recorderError.empty()) {
		setError(recorderError);
	}
}

bool ofxVlc4::isAudioRecording() const {
	return recorder.isAudioCaptureActive();
}

bool ofxVlc4::isVideoRecording() const {
	return recorder.isVideoCaptureActive();
}

int ofxVlc4::getVideoRecordingFrameRate() const {
	return recorder.getVideoCaptureFrameRate();
}

void ofxVlc4::setVideoRecordingBitrateKbps(int bitrateKbps) {
	if (std::string recorderError; !recorder.setVideoCaptureBitrateKbps(bitrateKbps, recorderError)) {
		setError(recorderError);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
		recordingSessionRuntime.preset.videoBitrateKbps = std::max(0, bitrateKbps);
	}
}

int ofxVlc4::getVideoRecordingBitrateKbps() const {
	return recorder.getVideoCaptureBitrateKbps();
}

const std::string & ofxVlc4::getVideoRecordingCodec() const {
	return recorder.getVideoCaptureCodec();
}

ofxVlc4RecordingVideoCodecPreset ofxVlc4::getVideoRecordingCodecPreset() const {
	return recordingVideoCodecPresetForCodec(getVideoRecordingCodec());
}

ofxVlc4RecorderPerformanceInfo ofxVlc4::getRecorderPerformanceInfo() const {
	return recorder.getPerformanceInfo();
}

void ofxVlc4Recorder::resetAudioCaptureState() {
	sampleRate = 0;
	channelCount = 0;
	dataBytes = 0;
	outputPath.clear();
	audioRecordingActive.store(false);
	clearBufferedAudioCapture();
}

void ofxVlc4Recorder::resetAudioCaptureBuffer(int sampleRate, int channelCount) {
	audioTransferScratch.clear();
	audioRingBuffer.allocate(
		static_cast<size_t>(sampleRate) *
		static_cast<size_t>(channelCount) *
		kBufferedAudioSeconds);
	audioRingBuffer.clear();
}

void ofxVlc4Recorder::prepareAudioCaptureBuffer(int configuredSampleRate, int configuredChannelCount) {
	if (!audioRecordingActive.load()) {
		return;
	}

	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	drainBufferedAudioLocked();
	if (!audioRecordingActive.load()) {
		return;
	}

	if (sampleRate > 0 && channelCount > 0 &&
		(configuredSampleRate != sampleRate || configuredChannelCount != channelCount)) {
		failAudioCapture("Audio recording format changed during capture.");
		return;
	}

	resetAudioCaptureBuffer(configuredSampleRate, configuredChannelCount);
}

void ofxVlc4Recorder::captureAudioSamples(const float * samples, size_t sampleCount) {
	if (!audioRecordingActive.load() || !samples || sampleCount == 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	audioRingBuffer.write(samples, sampleCount);
}

void ofxVlc4Recorder::resetCapturedAudio() {
	if (!audioRecordingActive.load()) {
		return;
	}

	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	clearBufferedAudioCapture();
}

bool ofxVlc4Recorder::isAudioCaptureActive() const {
	return audioRecordingActive.load();
}

bool ofxVlc4Recorder::isVideoCaptureActive() const {
	return videoRecordingActive.load();
}

bool ofxVlc4Recorder::hasActiveCaptureSession() const {
	return isVideoCaptureActive() || isAudioCaptureActive();
}

bool ofxVlc4Recorder::needsCaptureUpdate() const {
	return hasActiveCaptureSession() || errorPending.load(std::memory_order_acquire);
}

bool ofxVlc4Recorder::hasCleanupState() const {
	std::lock_guard<std::mutex> lock(recordingMutex);
	return hasActiveCaptureSession() ||
		recordingTexture.isAllocated() ||
		recordingPixels.size() > 0 ||
		recordingFrameSize > 0 ||
		recordingReadOffset > 0;
}

ofxVlc4RecorderPerformanceInfo ofxVlc4Recorder::getPerformanceInfo() const {
	std::lock_guard<std::mutex> lock(recordingMutex);

	ofxVlc4RecorderPerformanceInfo info;
	info.asyncVideoReadbackEnabled = recordingPboEnabled;
	info.asyncVideoReadbackPrimed = recordingPboPrimed;
	info.readbackPolicy = readbackPolicy;
	info.readbackBufferCount = recordingPixelPackBuffers.size();
	info.captureStartTimeUs = videoCaptureStartTimeUs;
	info.submittedFrameCount = videoFramesSubmitted;
	info.readyFrameCount = videoFramesReady;
	info.synchronousFrameCount = videoSynchronousFrames;
	info.fallbackFrameCount = videoFallbackFrames;
	info.droppedFrameCount = videoDroppedFrames;
	info.policyDroppedFrameCount = videoPolicyDroppedFrames;
	info.mapFailureCount = videoMapFailureCount;
	info.pendingFrameCount = recordingPboPendingIndices.size();
	info.maxPendingFrameCount = videoMaxPendingFrames;
	info.lastCaptureMicros = videoLastCaptureMicros;
	info.averageCaptureMicros = videoFramesSubmitted > 0
		? (videoTotalCaptureMicros / videoFramesSubmitted)
		: 0;
	info.maxCaptureMicros = videoMaxCaptureMicros;
	info.lastReadbackLatencyMicros = videoLastReadbackLatencyMicros;
	info.averageReadbackLatencyMicros = videoAsyncFramesReady > 0
		? (videoTotalReadbackLatencyMicros / videoAsyncFramesReady)
		: 0;
	info.maxReadbackLatencyMicros = videoMaxReadbackLatencyMicros;
	info.waitCount = videoWaitCount;
	info.averageWaitMicros = videoWaitCount > 0
		? (videoTotalWaitMicros / videoWaitCount)
		: 0;
	info.maxWaitMicros = videoMaxWaitMicros;

	if (videoCaptureStartTimeUs > 0) {
		const uint64_t elapsedUs = std::max<uint64_t>(1, ofGetElapsedTimeMicros() - videoCaptureStartTimeUs);
		const double elapsedSeconds = static_cast<double>(elapsedUs) / 1000000.0;
		info.submittedFramesPerSecond = static_cast<double>(videoFramesSubmitted) / elapsedSeconds;
		info.readyFramesPerSecond = static_cast<double>(videoFramesReady) / elapsedSeconds;
	}

	return info;
}

void ofxVlc4Recorder::clearCaptureState() {
	clearVideoRecording();
	clearAudioRecording();
}

bool ofxVlc4Recorder::startAudioCapture(
	const std::string & audioPath,
	int captureSampleRate,
	int captureChannelCount,
	std::string & errorOut) {
	errorOut.clear();
	const int recordingSampleRate = std::max(captureSampleRate, 44100);
	const int recordingChannelCount = std::max(captureChannelCount, 2);
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	closeAudioCapture();
	resetAudioCaptureBuffer(recordingSampleRate, recordingChannelCount);
	sampleRate = recordingSampleRate;
	channelCount = recordingChannelCount;
	dataBytes = 0;
	outputPath = audioPath;

	ofFilePath::createEnclosingDirectory(outputPath, false, true);
	stream.open(outputPath, std::ios::binary | std::ios::trunc);
	if (!stream.is_open()) {
		closeAudioCapture();
		errorOut = "Failed to open audio recording file.";
		return false;
	}

	writeWavHeader(stream, sampleRate, channelCount, 0);
	audioRecordingActive.store(true);
	return true;
}

std::string ofxVlc4Recorder::finishAudioCapture() {
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	const std::string finishedPath = outputPath;
	closeAudioCapture();
	return finishedPath;
}

const std::string & ofxVlc4Recorder::getLastFinishedAudioPath() const {
	return lastFinishedAudioPath;
}

const std::string & ofxVlc4Recorder::getLastFinishedVideoPath() const {
	return lastFinishedVideoPath;
}

bool ofxVlc4Recorder::setVideoCaptureFrameRate(int fps, std::string & errorOut) {
	errorOut.clear();
	if (fps <= 0) {
		errorOut = "Video recording frame rate must be positive.";
		return false;
	}

	videoFrameRate = fps;
	videoFrameIntervalUs = static_cast<uint64_t>(1000000.0 / static_cast<double>(videoFrameRate));
	return true;
}

int ofxVlc4Recorder::getVideoCaptureFrameRate() const {
	return videoFrameRate;
}

bool ofxVlc4Recorder::setVideoCaptureBitrateKbps(int bitrateKbps, std::string & errorOut) {
	errorOut.clear();
	if (bitrateKbps < 0) {
		errorOut = "Video recording bitrate cannot be negative.";
		return false;
	}

	videoBitrateKbps = bitrateKbps;
	return true;
}

int ofxVlc4Recorder::getVideoCaptureBitrateKbps() const {
	return videoBitrateKbps;
}

bool ofxVlc4Recorder::setVideoCaptureCodec(const std::string & codec, std::string & errorOut) {
	errorOut.clear();
	std::string normalizedCodec = trimRecorderText(codec);
	normalizedCodec = ofToUpper(normalizedCodec);
	if (normalizedCodec.empty()) {
		errorOut = "Video recording codec is empty.";
		return false;
	}

	videoCodec = normalizedCodec;
	return true;
}

const std::string & ofxVlc4Recorder::getVideoCaptureCodec() const {
	return videoCodec;
}

void ofxVlc4Recorder::setVideoReadbackPolicy(ofxVlc4VideoReadbackPolicy policy) {
	std::lock_guard<std::mutex> lock(recordingMutex);
	readbackPolicy = policy;
}

ofxVlc4VideoReadbackPolicy ofxVlc4Recorder::getVideoReadbackPolicy() const {
	std::lock_guard<std::mutex> lock(recordingMutex);
	return readbackPolicy;
}

void ofxVlc4Recorder::setVideoReadbackBufferCount(size_t bufferCount) {
	std::lock_guard<std::mutex> lock(recordingMutex);
	recordingPboBufferCount = std::clamp<size_t>(bufferCount, 2, 4);
}

size_t ofxVlc4Recorder::getVideoReadbackBufferCount() const {
	std::lock_guard<std::mutex> lock(recordingMutex);
	return recordingPboBufferCount;
}

libvlc_media_t * ofxVlc4Recorder::beginVideoCapture(
	const ofTexture & texture,
	const std::string & videoPath,
	int outputWidth,
	int outputHeight,
	std::string & errorOut) {
	errorOut.clear();
	clearVideoRecording();
	if (!texture.isAllocated() || texture.getWidth() <= 0 || texture.getHeight() <= 0) {
		errorOut = "Texture is not allocated.";
		return nullptr;
	}
	if (videoFrameRate <= 0) {
		errorOut = "Video recording frame rate must be positive.";
		return nullptr;
	}
	if (videoCodec.empty()) {
		errorOut = "Video recording codec is empty.";
		return nullptr;
	}

	const int textureWidth = static_cast<int>(texture.getWidth());
	const int textureHeight = static_cast<int>(texture.getHeight());
	const std::string normalizedVideoCodec = ofToUpper(videoCodec);
	const bool requiresHevcAlignment =
		normalizedVideoCodec == "X265" ||
		normalizedVideoCodec == "H265" ||
		normalizedVideoCodec == "HEVC";
	const int widthAlignment = requiresHevcAlignment ? 16 : 2;
	const int heightAlignment = requiresHevcAlignment ? 8 : 2;
	auto normalizeEncodedDimension = [](int requested, int source, int alignment) -> int {
		int resolved = requested > 0 ? requested : source;
		if (resolved <= 0) {
			return 0;
		}
		if (alignment > 1) {
			resolved -= resolved % alignment;
			resolved = std::max(alignment, resolved);
		}
		return resolved;
	};
	const int encodedWidth = normalizeEncodedDimension(outputWidth, textureWidth, widthAlignment);
	const int encodedHeight = normalizeEncodedDimension(outputHeight, textureHeight, heightAlignment);
	if (encodedWidth <= 0 || encodedHeight <= 0) {
		errorOut = "Video recording output dimensions are invalid.";
		return nullptr;
	}
	if ((outputWidth > 0 && encodedWidth != outputWidth) ||
		(outputHeight > 0 && encodedHeight != outputHeight)) {
		ofLogNotice("ofxVlc4")
			<< "Adjusted recording output size from "
			<< outputWidth << "x" << outputHeight
			<< " to " << encodedWidth << "x" << encodedHeight
			<< " for codec " << videoCodec << ".";
	}
	{
		std::lock_guard<std::mutex> lock(recordingMutex);
		videoCaptureStartTimeUs = ofGetElapsedTimeMicros();
		videoFramesSubmitted = 0;
		videoFramesReady = 0;
		videoAsyncFramesReady = 0;
		videoSynchronousFrames = 0;
		videoFallbackFrames = 0;
		videoDroppedFrames = 0;
		videoPolicyDroppedFrames = 0;
		videoMapFailureCount = 0;
		videoMaxPendingFrames = 0;
		videoLastCaptureMicros = 0;
		videoTotalCaptureMicros = 0;
		videoMaxCaptureMicros = 0;
		videoLastReadbackLatencyMicros = 0;
		videoTotalReadbackLatencyMicros = 0;
		videoMaxReadbackLatencyMicros = 0;
		videoWaitCount = 0;
		videoTotalWaitMicros = 0;
		videoMaxWaitMicros = 0;
		recordingSourceTexture.clear();
		recordingResizeFbo.clear();
		recordingTexture.clear();
		const bool requiresResizedCapture = encodedWidth != textureWidth || encodedHeight != textureHeight;
		if (requiresResizedCapture) {
			recordingSourceTexture.allocate(textureWidth, textureHeight, GL_RGB);
			recordingSourceTexture.setUseExternalTextureID(texture.getTextureData().textureID);
			recordingResizeFbo.allocate(encodedWidth, encodedHeight, GL_RGB);
		} else {
			recordingTexture.allocate(textureWidth, textureHeight, GL_RGB);
			recordingTexture.setUseExternalTextureID(texture.getTextureData().textureID);
		}
		recordingPixels.allocate(encodedWidth, encodedHeight, OF_PIXELS_RGB);
		recordingPixels.set(0);
		recordingFrameSize = recordingPixels.size();
		videoOutputPath = videoPath;
		recordingFrameSerial = 0;
		recordingReadFrameSerial = 0;
		lastVideoCaptureTimeUs = ofGetElapsedTimeMicros();
		if (glfwGetCurrentContext() != nullptr) {
			// For callback-fed rawvid recording, a CPU-ready frame buffer is more
			// important than hiding readback latency. Async PBO submission can lag
			// the callback stream and leave VLC with only the primed frame.
			destroyVideoReadbackBuffersLocked();
			const uint64_t captureStartUs = ofGetElapsedTimeMicros();
			if (!updateCaptureTextureLocked()) {
				clearVideoRecording();
				errorOut = "Failed to prepare capture texture.";
				return nullptr;
			}
			recordingTexture.readToPixels(recordingPixels);
			videoLastCaptureMicros = ofGetElapsedTimeMicros() - captureStartUs;
			videoTotalCaptureMicros += videoLastCaptureMicros;
			videoMaxCaptureMicros = std::max(videoMaxCaptureMicros, videoLastCaptureMicros);
			videoFramesSubmitted = 1;
			videoFramesReady = 1;
			videoSynchronousFrames = 1;
			publishCapturedFrameLocked();
			initializeVideoReadbackBuffersLocked(recordingFrameSize);
		}
		recordingReadOffset = 0;
	}

	libvlc_media_t * recordingMedia = libvlc_media_new_callbacks(
		ofxVlc4Recorder::textureOpen,
		ofxVlc4Recorder::textureRead,
		nullptr,
		ofxVlc4Recorder::textureClose,
		this);
	if (!recordingMedia) {
		clearVideoRecording();
		errorOut = "Failed to create recording media.";
		return nullptr;
	}

	const int captureWidth = static_cast<int>(recordingPixels.getWidth());
	const int captureHeight = static_cast<int>(recordingPixels.getHeight());
	const std::string width = "rawvid-width=" + ofToString(captureWidth);
	const std::string height = "rawvid-height=" + ofToString(captureHeight);
	const std::string bufferSize = "prefetch-buffer-size=" + ofToString(captureWidth * captureHeight * 3);
	const std::string rawFrameRate = "rawvid-fps=" + ofToString(videoFrameRate);
	std::string streamSpec = "sout=#transcode{vcodec=" + videoCodec;
	if (videoBitrateKbps > 0) {
		streamSpec += ",vb=" + ofToString(videoBitrateKbps);
	}
	if (encodedWidth > 0 && encodedHeight > 0 &&
		(encodedWidth != captureWidth || encodedHeight != captureHeight)) {
		streamSpec += ",width=" + ofToString(encodedWidth);
		streamSpec += ",height=" + ofToString(encodedHeight);
	}
	streamSpec += "}:standard{access=file,dst=" + videoPath + "}";

	libvlc_media_add_option(recordingMedia, "demux=rawvid");
	libvlc_media_add_option(recordingMedia, width.c_str());
	libvlc_media_add_option(recordingMedia, height.c_str());
	libvlc_media_add_option(recordingMedia, "rawvid-chroma=RV24");
	libvlc_media_add_option(recordingMedia, rawFrameRate.c_str());
	libvlc_media_add_option(recordingMedia, bufferSize.c_str());
	libvlc_media_add_option(recordingMedia, streamSpec.c_str());
	videoRecordingActive.store(true);
	return recordingMedia;
}

std::string ofxVlc4Recorder::updateCaptureState() {
	if (glfwGetCurrentContext() != nullptr && videoRecordingActive.load()) {
		const uint64_t nowUs = ofGetElapsedTimeMicros();

		std::lock_guard<std::mutex> lock(recordingMutex);
		if (recordingTexture.isAllocated() && recordingPixels.isAllocated()) {
			if (recordingPboEnabled && !drainAvailableReadbackBuffersLocked(false)) {
				++videoMapFailureCount;
				++videoDroppedFrames;
				destroyVideoReadbackBuffersLocked();
			}
			if (lastVideoCaptureTimeUs == 0 || nowUs >= lastVideoCaptureTimeUs + videoFrameIntervalUs) {
				captureVideoFrameLocked();
				lastVideoCaptureTimeUs = nowUs;
			}
		}
	}

	if (audioRecordingActive.load() && audioRingBuffer.getNumReadableSamples() > 0) {
		std::lock_guard<std::mutex> lock(audioRecordingMutex);
		if (audioRecordingActive.load()) {
			drainBufferedAudioLocked();
		}
	}

	return takePendingError();
}

void ofxVlc4Recorder::publishCapturedFrameLocked() {
	++recordingFrameSerial;
	recordingFrameReadyCondition.notify_all();
}

bool ofxVlc4Recorder::initializeVideoReadbackBuffersLocked(size_t frameBytes) {
#ifdef TARGET_OPENGLES
	(void)frameBytes;
	return false;
#else
	destroyVideoReadbackBuffersLocked();
	if (frameBytes == 0 || glfwGetCurrentContext() == nullptr) {
		return false;
	}

	recordingPixelPackBuffers.assign(recordingPboBufferCount, 0);
	recordingPixelPackFences.assign(recordingPboBufferCount, nullptr);
	recordingPboSubmitTimesUs.assign(recordingPboBufferCount, 0);
	recordingPboPendingIndices.clear();
	glGenBuffers(
		static_cast<GLsizei>(recordingPixelPackBuffers.size()),
		recordingPixelPackBuffers.data());
	const bool hasInvalidBuffer = std::any_of(
		recordingPixelPackBuffers.begin(),
		recordingPixelPackBuffers.end(),
		[](GLuint pbo) { return pbo == 0; });
	if (hasInvalidBuffer) {
		destroyVideoReadbackBuffersLocked();
		return false;
	}

	for (GLuint pbo : recordingPixelPackBuffers) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
		glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(frameBytes), nullptr, GL_STREAM_READ);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	recordingPboWriteIndex = 0;
	recordingPboPrimed = false;
	recordingPboEnabled = true;
	return true;
#endif
}

void ofxVlc4Recorder::destroyVideoReadbackBuffersLocked() {
	std::vector<GLuint> pixelPackBuffers = std::move(recordingPixelPackBuffers);
	std::vector<GLsync> pixelPackFences = std::move(recordingPixelPackFences);
	recordingPboSubmitTimesUs.clear();
	recordingPboPendingIndices.clear();
	recordingPboWriteIndex = 0;
	recordingPboPrimed = false;
	recordingPboEnabled = false;

#ifndef TARGET_OPENGLES
	if (glfwGetCurrentContext() != nullptr) {
		for (GLsync fence : pixelPackFences) {
			if (fence != nullptr) {
				glDeleteSync(fence);
			}
		}
	}
	if (!pixelPackBuffers.empty() && glfwGetCurrentContext() != nullptr) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glDeleteBuffers(static_cast<GLsizei>(pixelPackBuffers.size()), pixelPackBuffers.data());
	}
#endif
}

bool ofxVlc4Recorder::updateCaptureTextureLocked() {
	if (recordingResizeFbo.isAllocated()) {
		if (!recordingSourceTexture.isAllocated()) {
			return false;
		}
		recordingResizeFbo.begin();
		ofClear(0, 0, 0, 255);
		ofSetColor(255);
		recordingSourceTexture.draw(
			0.0f,
			0.0f,
			static_cast<float>(recordingResizeFbo.getWidth()),
			static_cast<float>(recordingResizeFbo.getHeight()));
		recordingResizeFbo.end();
		recordingTexture = recordingResizeFbo.getTexture();
		return recordingTexture.isAllocated();
	}
	return recordingTexture.isAllocated();
}

bool ofxVlc4Recorder::waitForSubmittedReadbackLocked(size_t bufferIndex, uint64_t & waitMicrosOut) {
	waitMicrosOut = 0;
#ifdef TARGET_OPENGLES
	(void)bufferIndex;
	return false;
#else
	if (bufferIndex >= recordingPixelPackFences.size()) {
		return false;
	}

	GLsync & fence = recordingPixelPackFences[bufferIndex];
	if (fence == nullptr) {
		return true;
	}

	const GLenum waitFlags = (readbackPolicy == ofxVlc4VideoReadbackPolicy::BlockForFreshestFrame)
		? GL_SYNC_FLUSH_COMMANDS_BIT
		: 0;
	const GLuint64 timeoutNs = (readbackPolicy == ofxVlc4VideoReadbackPolicy::BlockForFreshestFrame)
		? 1000000ull
		: 0ull;
	const uint64_t waitStartUs = ofGetElapsedTimeMicros();

	while (true) {
		const GLenum waitResult = glClientWaitSync(fence, waitFlags, timeoutNs);
		if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED) {
			waitMicrosOut = ofGetElapsedTimeMicros() - waitStartUs;
			return true;
		}
		if (waitResult == GL_WAIT_FAILED) {
			return false;
		}
		if (readbackPolicy != ofxVlc4VideoReadbackPolicy::BlockForFreshestFrame) {
			return false;
		}
	}
#endif
}

bool ofxVlc4Recorder::consumeReadbackBufferLocked(size_t bufferIndex) {
#ifdef TARGET_OPENGLES
	(void)bufferIndex;
	return false;
#else
	if (bufferIndex >= recordingPixelPackBuffers.size()) {
		return false;
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, recordingPixelPackBuffers[bufferIndex]);
	void * mapped = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
	if (!mapped) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		return false;
	}

	std::memcpy(recordingPixels.getData(), mapped, recordingFrameSize);
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	++videoFramesReady;
	++videoAsyncFramesReady;
	publishCapturedFrameLocked();
	if (recordingPboSubmitTimesUs[bufferIndex] > 0) {
		videoLastReadbackLatencyMicros = ofGetElapsedTimeMicros() - recordingPboSubmitTimesUs[bufferIndex];
		videoTotalReadbackLatencyMicros += videoLastReadbackLatencyMicros;
		videoMaxReadbackLatencyMicros = std::max(videoMaxReadbackLatencyMicros, videoLastReadbackLatencyMicros);
	}
	recordingPboSubmitTimesUs[bufferIndex] = 0;
	if (recordingPixelPackFences[bufferIndex] != nullptr) {
		glDeleteSync(recordingPixelPackFences[bufferIndex]);
		recordingPixelPackFences[bufferIndex] = nullptr;
	}
	return true;
#endif
}

bool ofxVlc4Recorder::tryConsumeSubmittedReadbackLocked(
	size_t bufferIndex,
	bool blockUntilReady,
	bool & consumedOut,
	uint64_t & waitMicrosOut) {
	consumedOut = false;
	waitMicrosOut = 0;
#ifdef TARGET_OPENGLES
	(void)bufferIndex;
	(void)blockUntilReady;
	return false;
#else
	if (bufferIndex >= recordingPixelPackFences.size()) {
		return false;
	}

	const auto originalPolicy = readbackPolicy;
	readbackPolicy = blockUntilReady
		? ofxVlc4VideoReadbackPolicy::BlockForFreshestFrame
		: ofxVlc4VideoReadbackPolicy::DropLateFrames;
	const bool ready = waitForSubmittedReadbackLocked(bufferIndex, waitMicrosOut);
	readbackPolicy = originalPolicy;
	if (!ready) {
		return !blockUntilReady;
	}

	consumedOut = consumeReadbackBufferLocked(bufferIndex);
	return consumedOut;
#endif
}

bool ofxVlc4Recorder::drainAvailableReadbackBuffersLocked(bool blockUntilReady) {
	while (!recordingPboPendingIndices.empty()) {
		bool consumed = false;
		uint64_t waitMicros = 0;
		const size_t readIndex = recordingPboPendingIndices.front();
		if (!tryConsumeSubmittedReadbackLocked(readIndex, blockUntilReady, consumed, waitMicros)) {
			return false;
		}
		if (!consumed) {
			break;
		}
		if (waitMicros > 0) {
			++videoWaitCount;
			videoTotalWaitMicros += waitMicros;
			videoMaxWaitMicros = std::max(videoMaxWaitMicros, waitMicros);
		}
		recordingPboPendingIndices.pop_front();
		recordingPboPrimed = !recordingPboPendingIndices.empty();
		blockUntilReady = false;
	}

	return true;
}

void ofxVlc4Recorder::captureVideoFrameLocked() {
	if (!recordingTexture.isAllocated() || !recordingPixels.isAllocated()) {
		return;
	}

	recordingFrameSize = recordingPixels.size();
	const uint64_t captureStartUs = ofGetElapsedTimeMicros();
	if (!updateCaptureTextureLocked()) {
		++videoDroppedFrames;
		return;
	}

#ifndef TARGET_OPENGLES
	if (recordingPboEnabled && glfwGetCurrentContext() != nullptr) {
		const auto failToSynchronousPath = [&]() {
			destroyVideoReadbackBuffersLocked();
		};
		if (!drainAvailableReadbackBuffersLocked(false)) {
			++videoMapFailureCount;
			++videoDroppedFrames;
			failToSynchronousPath();
		}

		if (!recordingPboEnabled) {
			// Fallback already selected above.
		} else if (recordingPboPendingIndices.size() >= recordingPixelPackBuffers.size()) {
			if (readbackPolicy == ofxVlc4VideoReadbackPolicy::DropLateFrames) {
				++videoDroppedFrames;
				++videoPolicyDroppedFrames;
				videoLastCaptureMicros = ofGetElapsedTimeMicros() - captureStartUs;
				videoTotalCaptureMicros += videoLastCaptureMicros;
				videoMaxCaptureMicros = std::max(videoMaxCaptureMicros, videoLastCaptureMicros);
				return;
			}

			if (!drainAvailableReadbackBuffersLocked(true)) {
				++videoMapFailureCount;
				++videoDroppedFrames;
				failToSynchronousPath();
			}
		}

		if (recordingPboEnabled) {
		const size_t writeIndex = recordingPboWriteIndex;
		const int pixelWidth = static_cast<int>(recordingPixels.getWidth());
		const int bytesPerChannel = static_cast<int>(recordingPixels.getBytesPerChannel());
		const int channelCount = static_cast<int>(recordingPixels.getNumChannels());

		ofSetPixelStoreiAlignment(
			GL_PACK_ALIGNMENT,
			pixelWidth,
			bytesPerChannel,
			channelCount);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, recordingPixelPackBuffers[writeIndex]);
		glBindTexture(recordingTexture.getTextureData().textureTarget, recordingTexture.getTextureData().textureID);
		glGetTexImage(recordingTexture.getTextureData().textureTarget, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
		glBindTexture(recordingTexture.getTextureData().textureTarget, 0);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		recordingPboSubmitTimesUs[writeIndex] = ofGetElapsedTimeMicros();
		if (recordingPixelPackFences[writeIndex] != nullptr) {
			glDeleteSync(recordingPixelPackFences[writeIndex]);
		}
		recordingPixelPackFences[writeIndex] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glFlush();
		recordingPboPendingIndices.push_back(writeIndex);
		videoMaxPendingFrames = std::max<uint64_t>(
			videoMaxPendingFrames,
			static_cast<uint64_t>(recordingPboPendingIndices.size()));
		recordingPboPrimed = !recordingPboPendingIndices.empty();
		++videoFramesSubmitted;
		recordingPboWriteIndex = (recordingPboWriteIndex + 1) % recordingPixelPackBuffers.size();
		videoLastCaptureMicros = ofGetElapsedTimeMicros() - captureStartUs;
		videoTotalCaptureMicros += videoLastCaptureMicros;
		videoMaxCaptureMicros = std::max(videoMaxCaptureMicros, videoLastCaptureMicros);
		return;
		}
	}
#endif

	recordingTexture.readToPixels(recordingPixels);
	recordingFrameSize = recordingPixels.size();
	++videoFramesSubmitted;
	++videoFramesReady;
	++videoSynchronousFrames;
	++videoFallbackFrames;
	publishCapturedFrameLocked();
	videoLastCaptureMicros = ofGetElapsedTimeMicros() - captureStartUs;
	videoTotalCaptureMicros += videoLastCaptureMicros;
	videoMaxCaptureMicros = std::max(videoMaxCaptureMicros, videoLastCaptureMicros);
}

void ofxVlc4Recorder::clearBufferedAudioCapture() {
	audioTransferScratch.clear();
	audioRingBuffer.reset();
}

void ofxVlc4Recorder::drainBufferedAudioLocked() {
	if (!stream.is_open() || audioRingBuffer.getNumReadableSamples() == 0) {
		return;
	}

	const size_t readableSamples = audioRingBuffer.getNumReadableSamples();
	audioTransferScratch.resize(readableSamples);
	audioRingBuffer.read(audioTransferScratch.data(), readableSamples);
	writeInterleaved(audioTransferScratch.data(), readableSamples);
}

void ofxVlc4Recorder::finalizeAudioCaptureStream() {
	if (!stream.is_open()) {
		return;
	}

	writeWavHeader(
		stream,
		sampleRate,
		channelCount,
		static_cast<uint32_t>(std::min<uint64_t>(dataBytes, kMaxWavDataBytes)));
	stream.flush();
	stream.close();
}

void ofxVlc4Recorder::closeAudioCapture() {
	const std::string finishedPath = outputPath;
	if (!finishedPath.empty()) {
		lastFinishedAudioPath = finishedPath;
	}
	finalizeAudioCaptureStream();
	resetAudioCaptureState();
}

void ofxVlc4Recorder::failAudioCapture(const std::string & message) {
	{
		std::lock_guard<std::mutex> lock(errorMutex);
		lastError = message;
	}
	errorPending.store(true, std::memory_order_release);
	closeAudioCapture();
}

std::string ofxVlc4Recorder::takePendingError() {
	if (!errorPending.exchange(false, std::memory_order_acq_rel)) {
		return {};
	}

	std::lock_guard<std::mutex> lock(errorMutex);
	std::string recorderError = std::move(lastError);
	lastError.clear();
	return recorderError;
}

void ofxVlc4Recorder::writeInterleaved(const float * samples, size_t sampleCount) {
	if (!stream.is_open() || !samples || sampleCount == 0) {
		return;
	}

	const uint64_t remainingBytes = (dataBytes < kMaxWavDataBytes) ? (kMaxWavDataBytes - dataBytes) : 0;
	const size_t writableSamples = static_cast<size_t>(std::min<uint64_t>(sampleCount, remainingBytes / sizeof(float)));
	if (writableSamples > 0) {
		const size_t byteCount = writableSamples * sizeof(float);
		stream.write(reinterpret_cast<const char *>(samples), static_cast<std::streamsize>(byteCount));
		if (!stream.good()) {
			failAudioCapture("Failed to write audio recording file.");
			return;
		}

		dataBytes += static_cast<uint64_t>(byteCount);
	}

	if (writableSamples < sampleCount) {
		failAudioCapture("Audio recording reached the WAV size limit.");
	}
}

void ofxVlc4Recorder::writeWavHeader(std::ofstream & stream, int sampleRate, int channels, uint32_t dataBytes) {
	if (!stream.is_open()) {
		return;
	}

	const uint16_t channelCount = static_cast<uint16_t>(channels);
	const uint16_t blockAlign = static_cast<uint16_t>(channels * sizeof(float));
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate * blockAlign);
	const uint32_t riffSize = 36u + dataBytes;
	const uint16_t audioFormat = 3; // IEEE float
	const uint16_t bitsPerSample = static_cast<uint16_t>(sizeof(float) * 8);
	const uint32_t fmtChunkSize = 16;

	stream.seekp(0, std::ios::beg);
	stream.write("RIFF", 4);
	stream.write(reinterpret_cast<const char *>(&riffSize), sizeof(riffSize));
	stream.write("WAVE", 4);
	stream.write("fmt ", 4);
	stream.write(reinterpret_cast<const char *>(&fmtChunkSize), sizeof(fmtChunkSize));
	stream.write(reinterpret_cast<const char *>(&audioFormat), sizeof(audioFormat));
	stream.write(reinterpret_cast<const char *>(&channelCount), sizeof(channelCount));
	stream.write(reinterpret_cast<const char *>(&sampleRate), sizeof(sampleRate));
	stream.write(reinterpret_cast<const char *>(&byteRate), sizeof(byteRate));
	stream.write(reinterpret_cast<const char *>(&blockAlign), sizeof(blockAlign));
	stream.write(reinterpret_cast<const char *>(&bitsPerSample), sizeof(bitsPerSample));
	stream.write("data", 4);
	stream.write(reinterpret_cast<const char *>(&dataBytes), sizeof(dataBytes));
}

void ofxVlc4Recorder::clearVideoRecording() {
	videoRecordingActive.store(false);

	std::lock_guard<std::mutex> lock(recordingMutex);
	if (!videoOutputPath.empty()) {
		lastFinishedVideoPath = videoOutputPath;
		videoOutputPath.clear();
	}
	recordingReadOffset = 0;
	recordingFrameSize = 0;
	recordingFrameSerial = 0;
	recordingReadFrameSerial = 0;
	lastVideoCaptureTimeUs = 0;
	recordingPixels.clear();
	destroyVideoReadbackBuffersLocked();
	recordingResizeFbo.clear();
	if (!(recordingSourceTexture.isAllocated() && glfwGetCurrentContext() == nullptr)) {
		recordingSourceTexture.clear();
	}
	if (!(recordingTexture.isAllocated() && glfwGetCurrentContext() == nullptr)) {
		recordingTexture.clear();
	}
	recordingFrameReadyCondition.notify_all();
}

void ofxVlc4Recorder::clearAudioRecording() {
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	drainBufferedAudioLocked();
	closeAudioCapture();
}

int ofxVlc4Recorder::textureOpen(void * data, void ** datap, uint64_t * sizep) {
	auto * recorder = static_cast<ofxVlc4Recorder *>(data);
	if (!recorder || !recorder->videoRecordingActive.load()) {
		return -1;
	}

	if (datap) {
		*datap = recorder;
	}
	if (sizep) {
		*sizep = std::numeric_limits<uint64_t>::max();
	}

	std::lock_guard<std::mutex> lock(recorder->recordingMutex);
	recorder->recordingReadOffset = 0;
	recorder->recordingReadFrameSerial = 0;
	return 0;
}

long long ofxVlc4Recorder::textureRead(void * data, unsigned char * dst, size_t size) {
	auto * recorder = static_cast<ofxVlc4Recorder *>(data);
	if (!recorder || !dst || size == 0 || !recorder->videoRecordingActive.load()) {
		return 0;
	}

	std::unique_lock<std::mutex> lock(recorder->recordingMutex);
	if (recorder->recordingFrameSize == 0 || !recorder->recordingPixels.isAllocated() || !recorder->recordingPixels.getData()) {
		return 0;
	}

	if (recorder->recordingReadOffset == 0) {
		const auto nextFrameReady = [&]() {
			return !recorder->videoRecordingActive.load() ||
				recorder->recordingFrameSerial > recorder->recordingReadFrameSerial;
		};
		if (!nextFrameReady()) {
			const auto waitBudget = std::chrono::microseconds(
				std::max<uint64_t>(1000, recorder->videoFrameIntervalUs));
			recorder->recordingFrameReadyCondition.wait_for(lock, waitBudget, nextFrameReady);
		}
		if (recorder->recordingFrameSerial > recorder->recordingReadFrameSerial) {
			recorder->recordingReadFrameSerial = recorder->recordingFrameSerial;
		}
	}

	if (recorder->recordingFrameSize == 0 ||
		!recorder->recordingPixels.isAllocated() ||
		!recorder->recordingPixels.getData()) {
		return 0;
	}

	const unsigned char * src = recorder->recordingPixels.getData();
	const size_t offset = static_cast<size_t>(recorder->recordingReadOffset % recorder->recordingFrameSize);
	const size_t maxFrameChunk = recorder->recordingFrameSize - offset;
	const size_t chunkSize = std::min(size, maxFrameChunk);
	std::memcpy(dst, src + offset, chunkSize);
	recorder->recordingReadOffset = (recorder->recordingReadOffset + chunkSize) % recorder->recordingFrameSize;
	return static_cast<long long>(chunkSize);
}

int ofxVlc4Recorder::textureSeek(void * data, uint64_t offset) {
	auto * recorder = static_cast<ofxVlc4Recorder *>(data);
	if (!recorder) {
		return -1;
	}

	std::lock_guard<std::mutex> lock(recorder->recordingMutex);
	if (recorder->recordingFrameSize == 0) {
		return -1;
	}

	recorder->recordingReadOffset = offset % recorder->recordingFrameSize;
	return 0;
}

void ofxVlc4Recorder::textureClose(void * data) {
	auto * recorder = static_cast<ofxVlc4Recorder *>(data);
	if (!recorder) {
		return;
	}

	std::lock_guard<std::mutex> lock(recorder->recordingMutex);
	recorder->recordingReadOffset = 0;
}

std::string ofxVlc4::getLastAudioRecordingPath() const {
	return recorder.getLastFinishedAudioPath();
}

std::string ofxVlc4::getLastVideoRecordingPath() const {
	return recorder.getLastFinishedVideoPath();
}

bool ofxVlc4::muxRecordingFiles(
	const std::string & videoPath,
	const std::string & audioPath,
	const std::string & outputPath,
	const ofxVlc4MuxOptions & options,
	std::string * errorOut) {
	return muxRecordingFilesInternal(videoPath, audioPath, outputPath, options, nullptr, errorOut);
}

bool ofxVlc4::muxRecordingFiles(
	const std::string & videoPath,
	const std::string & audioPath,
	const std::string & outputPath,
	const std::string & containerMux,
	const std::string & audioCodec,
	uint64_t timeoutMs,
	std::string * errorOut) {
	ofxVlc4MuxOptions options;
	options.containerMux = containerMux;
	options.audioCodec = audioCodec;
	options.muxTimeoutMs = timeoutMs;
	return muxRecordingFiles(videoPath, audioPath, outputPath, options, errorOut);
}

bool ofxVlc4::muxRecordingFilesToMp4(
	const std::string & videoPath,
	const std::string & audioPath,
	const std::string & outputPath,
	uint64_t timeoutMs,
	std::string * errorOut) {
	ofxVlc4MuxOptions options;
	options.containerMux = "mp4";
	options.audioCodec = "mp4a";
	options.muxTimeoutMs = timeoutMs;
	return muxRecordingFiles(videoPath, audioPath, outputPath, options, errorOut);
}
