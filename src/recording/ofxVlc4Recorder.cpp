#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "ofxVlc4Recorder.h"
#include "support/ofxVlc4GlOps.h"
#include "support/ofxVlc4MuxHelpers.h"
#include "support/ofxVlc4RecordingHelpers.h"
#include "support/ofxVlc4Utils.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <thread>

namespace {

using namespace ofxVlc4MuxHelpers;
using ofxVlc4Utils::hasCurrentGlContext;

}

bool ofxVlc4::muxRecordingFilesInternal(
	const std::string & videoPath,
	const std::string & audioPath,
	const std::string & outputPath,
	const ofxVlc4MuxOptions & options,
	const std::atomic<bool> * cancelRequested,
	std::string * errorOut,
	const ofxVlc4MuxHelpers::FileReadinessContext * fileReadiness) {
	const std::string normalizedMux = ofToLower(ofxVlc4RecordingHelpers::trimRecorderText(options.containerMux));
	const std::string normalizedAudioCodec = ofToLower(ofxVlc4RecordingHelpers::trimRecorderText(options.audioCodec));
	if (!ofxVlc4RecordingHelpers::isValidSoutModuleName(normalizedMux)) {
		if (errorOut) {
			*errorOut = normalizedMux.empty()
				? "Mux container is empty."
				: "Mux container name contains invalid characters.";
		}
		return false;
	}
	if (!ofxVlc4RecordingHelpers::isValidSoutModuleName(normalizedAudioCodec)) {
		if (errorOut) {
			*errorOut = normalizedAudioCodec.empty()
				? "Mux audio codec is empty."
				: "Audio codec name contains invalid characters.";
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

	const bool hasVideoSignal = fileReadiness && fileReadiness->videoFinalized && fileReadiness->mutex && fileReadiness->cv;
	const bool hasAudioSignal = fileReadiness && fileReadiness->audioFinalized && fileReadiness->mutex && fileReadiness->cv;

	const bool videoReady = hasVideoSignal
		? waitForRecordingFile(videoPath, options.inputReadyTimeoutMs, cancelRequested,
			*fileReadiness->videoFinalized, *fileReadiness->mutex, *fileReadiness->cv)
		: waitForRecordingFile(videoPath, options.inputReadyTimeoutMs, cancelRequested);
	if (!videoReady) {
		return fail(cancelRequested && cancelRequested->load(std::memory_order_acquire)
			? "Recording mux cancelled."
			: "Timed out waiting for recorded video file.");
	}

	const bool audioReady = hasAudioSignal
		? waitForRecordingFile(audioPath, options.inputReadyTimeoutMs, cancelRequested,
			*fileReadiness->audioFinalized, *fileReadiness->mutex, *fileReadiness->cv)
		: waitForRecordingFile(audioPath, options.inputReadyTimeoutMs, cancelRequested);
	if (!audioReady) {
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
		"--ignore-config",
		"--sout-keep",
		"--sout-all"
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
		"sout=#gather:transcode{" + transcodeSpec + "}:standard{access=file,mux=" + normalizedMux + ",dst='" +
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

	const auto stallTimeout = std::chrono::milliseconds(options.muxTimeoutMs);
	auto lastProgressTime = std::chrono::steady_clock::now();
	uintmax_t lastOutputSize = 0;
	while (true) {
		if (cancelRequested && cancelRequested->load(std::memory_order_acquire)) {
			break;
		}

		const libvlc_state_t state = libvlc_media_player_get_state(muxPlayer);
#if !defined(LIBVLC_VERSION_MAJOR) || LIBVLC_VERSION_MAJOR < 4
		const bool reachedEnd = state == libvlc_Stopped || state == libvlc_Ended;
#else
		const bool reachedEnd = state == libvlc_Stopped;
#endif
		if (reachedEnd) {
			success = waitForRecordingFile(outputPath, options.outputReadyTimeoutMs, cancelRequested);
			break;
		}
		if (state == libvlc_Error) {
			break;
		}

		std::error_code sizeError;
		if (std::filesystem::exists(outputPath, sizeError)) {
			const uintmax_t currentSize = std::filesystem::file_size(outputPath, sizeError);
			if (!sizeError && currentSize > lastOutputSize) {
				lastOutputSize = currentSize;
				lastProgressTime = std::chrono::steady_clock::now();
			}
		}
		if (std::chrono::steady_clock::now() - lastProgressTime > stallTimeout) {
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
		return fail("Timed out or failed while muxing recording.");
	}
	return true;
}

bool ofxVlc4::startNamedAudioCaptureSession(const std::string & audioPath) {
	if (std::string recorderError; !m_impl->recordingObjectRuntime.recorder.startAudioCapture(
		audioPath,
		m_impl->audioRuntime.sampleRate.load(),
		m_impl->audioRuntime.channels.load(),
		recorderError)) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		setError(recorderError);
		return false;
	}
	setRecordingSessionState(ofxVlc4RecordingSessionState::Capturing);
	return m_impl->recordingObjectRuntime.recorder.isAudioCaptureActive();
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
		if (!m_impl->videoResourceRuntime.mainWindow) {
			m_impl->videoResourceRuntime.mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
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

		m_impl->windowCaptureRuntime.includeAudioCapture = options.includeAudioCapture;
		m_impl->windowCaptureRuntime.active = true;
		registerWindowCaptureListener();
		captureCurrentWindowBackbuffer();
		if (!startNamedTextureCaptureSession(config.outputBasePath, m_impl->windowCaptureRuntime.captureFbo.getTexture(), options)) {
			m_impl->windowCaptureRuntime.active = false;
			unregisterWindowCaptureListener();
			clearRecordingSessionConfig();
			return false;
		}
		if (!m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
			m_impl->windowCaptureRuntime.active = false;
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
	if (m_impl->recordingObjectRuntime.recorder.getVideoCaptureFrameRate() <= 0) {
		setError("Video recording frame rate must be positive.");
		return false;
	}
	if (m_impl->recordingObjectRuntime.recorder.getVideoCaptureCodec().empty()) {
		setError("Video recording codec is empty.");
		return false;
	}

	const bool usesPlayerOutputTexture =
		(m_impl->videoResourceRuntime.exposedTextureFbo.isAllocated() &&
			texture.getTextureData().textureID == m_impl->videoResourceRuntime.exposedTextureFbo.getTexture().getTextureData().textureID) ||
		(m_impl->videoResourceRuntime.videoTexture.isAllocated() &&
 		 texture.getTextureData().textureID == m_impl->videoResourceRuntime.videoTexture.getTextureData().textureID);
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
		if (options.includeAudioCapture) m_impl->recordingObjectRuntime.recorder.clearAudioRecording();
		return false;
	};
	const auto startTextureRecordingPlayback = [&](const std::string & videoPath, const std::string & startedStatus) {
		std::string recorderError;
		libvlc_media_t * recordingMedia = m_impl->recordingObjectRuntime.recorder.beginVideoCapture(
			texture,
			videoPath,
			options.outputWidth,
			options.outputHeight,
			recorderError);
		if (!recordingMedia) {
			return failTextureRecordingStart(recorderError);
		}

		clearCurrentMedia();
		if (m_impl->subsystemRuntime.coreSession) {
			m_impl->subsystemRuntime.coreSession->setMedia(recordingMedia);
		}
		libvlc_media_player_set_media(mediaPlayer, recordingMedia);
		applyCurrentMediaPlayerSettings();
		applySelectedRenderer();
		const int playResult = libvlc_media_player_play(mediaPlayer);
		if (playResult != 0) {
			clearCurrentMedia(false);
			m_impl->recordingObjectRuntime.recorder.clearVideoRecording();
			return failTextureRecordingStart("Failed to start recording playback.");
		}

		setRecordingSessionState(ofxVlc4RecordingSessionState::Capturing);
		setStatus(startedStatus);
		return true;
	};
	if (!options.includeAudioCapture) {
		const std::string videoFallbackExt = recordingVideoCodecUsesMovContainer(getVideoRecordingCodecPreset()) ? ".mov" : ".mp4";
		const std::string videoPath = ofxVlc4RecordingHelpers::buildRecordingOutputPath(name, videoFallbackExt);
		return startTextureRecordingPlayback(videoPath, "Video recording started: " + videoPath);
	}

	const std::string videoFallbackExt = recordingVideoCodecUsesMovContainer(getVideoRecordingCodecPreset()) ? ".mov" : ".ts";
	const ofxVlc4RecordingHelpers::RecordingOutputPaths outputPaths =
		ofxVlc4RecordingHelpers::buildRecordingOutputPaths(name, videoFallbackExt);
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
	if (m_impl->recordingObjectRuntime.recorder.hasActiveCaptureSession()) {
		setError("Stop the current recording session first.");
		return false;
	}
	if (requireAudioCapture && !m_impl->playerConfigRuntime.audioCaptureEnabled) {
		setError("Enable audio capture before recording audio.");
		return false;
	}
	return true;
}

void ofxVlc4::stopActiveRecorderSessions() {
	const bool hadActiveSession = m_impl->recordingObjectRuntime.recorder.hasActiveCaptureSession();
	m_impl->recordingObjectRuntime.recorder.clearCaptureState();
	clearRecordingSessionConfig();
	if (hadActiveSession) {
		{
			std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.fileReadyMutex);
			m_impl->recordingMuxRuntime.videoFileFinalized.store(true, std::memory_order_release);
			m_impl->recordingMuxRuntime.audioFileFinalized.store(true, std::memory_order_release);
		}
		m_impl->recordingMuxRuntime.fileReadyCv.notify_all();
		if (!m_impl->recordingMuxRuntime.pending.load() && !m_impl->recordingMuxRuntime.inProgress.load()) {
			setRecordingSessionState(ofxVlc4RecordingSessionState::Done);
		}
	}
}

void ofxVlc4::recordAudio(std::string name) {
	if (m_impl->recordingObjectRuntime.recorder.isAudioCaptureActive() && !m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
		setStatus("Audio recording saved to " + m_impl->recordingObjectRuntime.recorder.finishAudioCapture() + ".");
		clearRecordingSessionConfig();
		return;
	}
	if (!ensureRecorderSessionCanStart(true)) {
		return;
	}

	const std::string audioPath = ofxVlc4RecordingHelpers::buildRecordingOutputPath(name, ".wav");
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
	m_impl->windowCaptureRuntime.active = false;
	unregisterWindowCaptureListener();
	if (m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
		stopRecordingSessionInternal(true);
	}
}

bool ofxVlc4::isWindowRecording() const {
	return m_impl->windowCaptureRuntime.active && m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive();
}

bool ofxVlc4::armPendingRecordingMux(const std::string & outputPath, const ofxVlc4MuxOptions & options) {
	finalizeRecordingMuxThread();
	if (m_impl->recordingMuxRuntime.pending.load() || m_impl->recordingMuxRuntime.inProgress.load()) {
		setStatus("Recording finalize/mux already in progress.");
		return false;
	}

	if (!m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
		setError("Muxing requires an active video recording session.");
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.fileReadyMutex);
		m_impl->recordingMuxRuntime.videoFileFinalized.store(false, std::memory_order_release);
		m_impl->recordingMuxRuntime.audioFileFinalized.store(false, std::memory_order_release);
	}

	const std::string previousVideoPath = m_impl->recordingObjectRuntime.recorder.getLastFinishedVideoPath();
	const std::string previousAudioPath = m_impl->recordingObjectRuntime.recorder.getLastFinishedAudioPath();
	std::string expectedVideoPath;
	std::string expectedAudioPath;
	{
		std::lock_guard<std::mutex> recordingLock(m_impl->recordingObjectRuntime.recorder.recordingMutex);
		expectedVideoPath = m_impl->recordingObjectRuntime.recorder.videoOutputPath;
	}
	{
		std::lock_guard<std::mutex> audioLock(m_impl->recordingObjectRuntime.recorder.audioRecordingMutex);
		expectedAudioPath = m_impl->recordingObjectRuntime.recorder.outputPath;
	}
	const std::string resolvedExpectedVideoPath =
		!expectedVideoPath.empty() ? expectedVideoPath : m_impl->recordingObjectRuntime.recorder.getLastFinishedVideoPath();
	const std::string resolvedExpectedAudioPath =
		!expectedAudioPath.empty() ? expectedAudioPath : m_impl->recordingObjectRuntime.recorder.getLastFinishedAudioPath();
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
		std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
		m_impl->recordingMuxRuntime.options = options;
		m_impl->recordingMuxRuntime.previousVideoPath = previousVideoPath;
		m_impl->recordingMuxRuntime.previousAudioPath = previousAudioPath;
		m_impl->recordingMuxRuntime.expectedVideoPath = resolvedExpectedVideoPath;
		m_impl->recordingMuxRuntime.expectedAudioPath = resolvedExpectedAudioPath;
		m_impl->recordingMuxRuntime.requestedOutputPath = outputPath;
		m_impl->recordingMuxRuntime.completedOutputPath.clear();
		m_impl->recordingMuxRuntime.completedError.clear();
	}
	m_impl->recordingMuxRuntime.pending.store(true);
	setRecordingSessionState(ofxVlc4RecordingSessionState::Finalizing);
	setStatus("Finalizing recording...");
	return true;
}

bool ofxVlc4::stopRecordingSessionInternal(bool allowConfiguredMux) {
	const bool hadVideoCapture = m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive();
	const bool hadAudioCapture = m_impl->recordingObjectRuntime.recorder.isAudioCaptureActive();
	if (m_impl->windowCaptureRuntime.active) {
		m_impl->windowCaptureRuntime.active = false;
		unregisterWindowCaptureListener();
	}

	if (m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
		if (allowConfiguredMux) {
			ofxVlc4RecordingSessionConfig configCopy;
			bool shouldMuxOnStop = false;
			{
				std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
				shouldMuxOnStop = m_impl->recordingSessionRuntime.hasConfig && m_impl->recordingSessionRuntime.config.muxOnStop;
				if (shouldMuxOnStop) {
					configCopy = m_impl->recordingSessionRuntime.config;
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

	if (m_impl->recordingObjectRuntime.recorder.isAudioCaptureActive()) {
		const std::string finishedAudioPath = m_impl->recordingObjectRuntime.recorder.finishAudioCapture();
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
	return m_impl->recordingMuxRuntime.pending.load();
}

bool ofxVlc4::isRecordingMuxInProgress() const {
	return m_impl->recordingMuxRuntime.inProgress.load();
}

std::string ofxVlc4::getLastMuxedRecordingPath() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
	return m_impl->recordingMuxRuntime.completedOutputPath;
}

std::string ofxVlc4::getLastMuxError() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
	return m_impl->recordingMuxRuntime.completedError;
}

bool ofxVlc4::startAudioRecordingForActiveVideo(std::string name) {
	if (!m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
		setError("Start video recording before attaching audio.");
		return false;
	}
	if (m_impl->recordingObjectRuntime.recorder.isAudioCaptureActive()) {
		return true;
	}
	if (!m_impl->playerConfigRuntime.audioCaptureEnabled) {
		setError("Enable audio capture before recording audio.");
		return false;
	}

	const std::string audioPath = ofxVlc4RecordingHelpers::buildRecordingOutputPath(name, ".wav");
	if (!startNamedAudioCaptureSession(audioPath)) {
		return false;
	}

	setStatus("Audio recording attached: " + audioPath);
	return true;
}

void ofxVlc4::setVideoRecordingFrameRate(int fps) {
	if (std::string recorderError; !m_impl->recordingObjectRuntime.recorder.setVideoCaptureFrameRate(fps, recorderError)) {
		setError(recorderError);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
		m_impl->recordingSessionRuntime.preset.videoFrameRate = std::max(1, fps);
	}
}

void ofxVlc4::setVideoRecordingCodec(const std::string & codec) {
	if (std::string recorderError; !m_impl->recordingObjectRuntime.recorder.setVideoCaptureCodec(codec, recorderError)) {
		setError(recorderError);
		return;
	}

	ofxVlc4RecordingVideoCodecPreset codecPreset = ofxVlc4RecordingVideoCodecPreset::H264;
	ofxVlc4RecordingMuxProfile muxProfile = ofxVlc4RecordingMuxProfile::Mp4Aac;
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
		m_impl->recordingSessionRuntime.preset.videoCodecPreset = recordingVideoCodecPresetForCodec(codec);
		codecPreset = m_impl->recordingSessionRuntime.preset.videoCodecPreset;
		muxProfile = m_impl->recordingSessionRuntime.preset.muxProfile;
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
	m_impl->recordingObjectRuntime.recorder.setVideoReadbackPolicy(policy);
}

ofxVlc4VideoReadbackPolicy ofxVlc4::getVideoReadbackPolicy() const {
	return m_impl->recordingObjectRuntime.recorder.getVideoReadbackPolicy();
}

void ofxVlc4::setVideoReadbackBufferCount(size_t bufferCount) {
	m_impl->recordingObjectRuntime.recorder.setVideoReadbackBufferCount(bufferCount);
}

size_t ofxVlc4::getVideoReadbackBufferCount() const {
	return m_impl->recordingObjectRuntime.recorder.getVideoReadbackBufferCount();
}

void ofxVlc4::updateRecorder() {
	if (const std::string recorderError = m_impl->recordingObjectRuntime.recorder.updateCaptureState(); !recorderError.empty()) {
		setError(recorderError);
	}
}

bool ofxVlc4::isAudioRecording() const {
	return m_impl->recordingObjectRuntime.recorder.isAudioCaptureActive();
}

bool ofxVlc4::isVideoRecording() const {
	return m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive();
}

int ofxVlc4::getVideoRecordingFrameRate() const {
	return m_impl->recordingObjectRuntime.recorder.getVideoCaptureFrameRate();
}

void ofxVlc4::setVideoRecordingBitrateKbps(int bitrateKbps) {
	if (std::string recorderError; !m_impl->recordingObjectRuntime.recorder.setVideoCaptureBitrateKbps(bitrateKbps, recorderError)) {
		setError(recorderError);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
		m_impl->recordingSessionRuntime.preset.videoBitrateKbps = std::max(0, bitrateKbps);
	}
}

int ofxVlc4::getVideoRecordingBitrateKbps() const {
	return m_impl->recordingObjectRuntime.recorder.getVideoCaptureBitrateKbps();
}

const std::string & ofxVlc4::getVideoRecordingCodec() const {
	return m_impl->recordingObjectRuntime.recorder.getVideoCaptureCodec();
}

ofxVlc4RecordingVideoCodecPreset ofxVlc4::getVideoRecordingCodecPreset() const {
	return recordingVideoCodecPresetForCodec(getVideoRecordingCodec());
}

ofxVlc4RecorderPerformanceInfo ofxVlc4::getRecorderPerformanceInfo() const {
	return m_impl->recordingObjectRuntime.recorder.getPerformanceInfo();
}

void ofxVlc4::setRecordingAudioRingBufferSeconds(double seconds) {
	m_impl->recordingObjectRuntime.recorder.setAudioRingBufferSeconds(seconds);
}

double ofxVlc4::getRecordingAudioRingBufferSeconds() const {
	return m_impl->recordingObjectRuntime.recorder.getAudioRingBufferSeconds();
}

void ofxVlc4Recorder::resetAudioCaptureState() {
	sampleRate = 0;
	channelCount = 0;
	dataBytes = 0;
	wavSizeLimitWarned = false;
	outputPath.clear();
	audioRecordingActive.store(false);
	clearBufferedAudioCapture();
}

void ofxVlc4Recorder::resetAudioCaptureBuffer(int sampleRate, int channelCount) {
	audioTransferScratch.clear();
	audioRingBuffer.allocate(
		static_cast<size_t>(sampleRate) *
		static_cast<size_t>(channelCount) *
		audioRingBufferSeconds);
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
	std::string normalizedCodec = ofxVlc4RecordingHelpers::trimRecorderText(codec);
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
	recordingPboBufferCount = std::clamp<size_t>(bufferCount, 2, 8);
}

size_t ofxVlc4Recorder::getVideoReadbackBufferCount() const {
	std::lock_guard<std::mutex> lock(recordingMutex);
	return recordingPboBufferCount;
}

void ofxVlc4Recorder::setAudioRingBufferSeconds(double seconds) {
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	audioRingBufferSeconds = std::max(kMinAudioRingBufferSeconds, seconds);
}

double ofxVlc4Recorder::getAudioRingBufferSeconds() const {
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	return audioRingBufferSeconds;
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
	const bool requiresHapAlignment =
		normalizedVideoCodec == "HAP1" ||
		normalizedVideoCodec == "HAP5" ||
		normalizedVideoCodec == "HAPY";
	const int widthAlignment = requiresHevcAlignment ? 16 : (requiresHapAlignment ? 4 : 2);
	const int heightAlignment = requiresHevcAlignment ? 8 : (requiresHapAlignment ? 4 : 2);
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
		if (hasCurrentGlContext()) {
			// Initialize async PBO readback with proper buffering to keep the
			// VLC rawvid callback fed while hiding GPU readback latency.
			// Use larger buffer count to ensure callback always has frames available.
			const size_t enhancedBufferCount = std::max<size_t>(4, recordingPboBufferCount);
			if (initializeVideoReadbackBuffersLocked(recordingFrameSize)) {
				// Prime the PBO pipeline with initial frames
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
			} else {
				// Fallback to synchronous readback if PBO init fails
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
			}
		}
		recordingReadOffset = 0;
	}

	libvlc_media_t * recordingMedia = libvlc_media_new_callbacks(
		ofxVlc4Recorder::textureOpen,
		ofxVlc4Recorder::textureRead,
		ofxVlc4Recorder::textureSeek,
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
	streamSpec += "}:standard{access=file,dst='" + normalizeSoutPath(videoPath) + "'}";

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
	if (hasCurrentGlContext() && videoRecordingActive.load()) {
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
				if (lastVideoCaptureTimeUs == 0) {
					lastVideoCaptureTimeUs = nowUs;
				} else {
					lastVideoCaptureTimeUs += videoFrameIntervalUs;
				}
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
	if (frameBytes == 0 || !hasCurrentGlContext()) {
		return false;
	}

	recordingPixelPackFences.assign(recordingPboBufferCount, nullptr);
	recordingPboSubmitTimesUs.assign(recordingPboBufferCount, 0);
	recordingPboPendingIndices.clear();
	if (!ofxVlc4GlOps::allocatePixelPackBuffers(recordingPixelPackBuffers, recordingPboBufferCount, frameBytes)) {
		destroyVideoReadbackBuffersLocked();
		return false;
	}

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
	if (hasCurrentGlContext()) {
		ofxVlc4GlOps::destroyPixelPackBuffers(pixelPackBuffers, pixelPackFences);
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
		const GLenum waitResult = ofxVlc4GlOps::clientWaitFenceSync(fence, waitFlags, timeoutNs);
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

	void * mapped = ofxVlc4GlOps::mapPixelPackBuffer(recordingPixelPackBuffers[bufferIndex]);
	if (!mapped) {
		return false;
	}

	std::memcpy(recordingPixels.getData(), mapped, recordingFrameSize);
	ofxVlc4GlOps::unmapPixelPackBuffer();
	++videoFramesReady;
	++videoAsyncFramesReady;
	publishCapturedFrameLocked();
	if (recordingPboSubmitTimesUs[bufferIndex] > 0) {
		videoLastReadbackLatencyMicros = ofGetElapsedTimeMicros() - recordingPboSubmitTimesUs[bufferIndex];
		videoTotalReadbackLatencyMicros += videoLastReadbackLatencyMicros;
		videoMaxReadbackLatencyMicros = std::max(videoMaxReadbackLatencyMicros, videoLastReadbackLatencyMicros);
	}
	recordingPboSubmitTimesUs[bufferIndex] = 0;
	ofxVlc4GlOps::deleteFenceSync(recordingPixelPackFences[bufferIndex]);
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
	if (recordingPboEnabled && hasCurrentGlContext()) {
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
		ofxVlc4GlOps::deleteFenceSync(recordingPixelPackFences[writeIndex]);
		recordingPixelPackFences[writeIndex] = ofxVlc4GlOps::submitTextureReadback(
			recordingPixelPackBuffers[writeIndex],
			recordingTexture.getTextureData().textureTarget,
			recordingTexture.getTextureData().textureID);
		recordingPboSubmitTimesUs[writeIndex] = ofGetElapsedTimeMicros();
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

		constexpr uint64_t kWavWarningThreshold = static_cast<uint64_t>(kMaxWavDataBytes * kWavWarningThresholdRatio);
		if (!wavSizeLimitWarned && dataBytes >= kWavWarningThreshold) {
			wavSizeLimitWarned = true;
			ofLogWarning("ofxVlc4") << "Audio recording is approaching the WAV size limit (~4 GB). Stop the recording soon to avoid data loss.";
		}
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
	// Signal VLC to finish encoding by setting the active flag to false first
	// This allows the textureRead callback to return EOF on next call
	videoRecordingActive.store(false);

	// Wait for VLC to actually stop reading frames before we clear the state
	// This prevents the "last 10 seconds freeze" issue where VLC's encoder
	// buffer still has frames to process but we've already cleared the frame data
	const uint64_t stopTimeMicros = ofGetElapsedTimeMicros();
	const uint64_t maxWaitMicros = 5000000; // 5 seconds max wait
	const uint64_t readIdleThresholdMicros = 200000; // 200ms idle = VLC finished reading

	while (ofGetElapsedTimeMicros() - stopTimeMicros < maxWaitMicros) {
		const uint64_t lastReadTime = lastVlcReadTimeMicros.load(std::memory_order_acquire);
		const uint64_t timeSinceLastRead = ofGetElapsedTimeMicros() - lastReadTime;

		// If VLC hasn't read for 200ms, it's done processing
		if (timeSinceLastRead > readIdleThresholdMicros) {
			break;
		}

		// Sleep briefly to avoid busy waiting
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

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
	const bool hasGlContext = hasCurrentGlContext();
	if (!(recordingResizeFbo.isAllocated() && !hasGlContext)) {
		recordingResizeFbo.clear();
	}
	if (!(recordingSourceTexture.isAllocated() && !hasGlContext)) {
		recordingSourceTexture.clear();
	}
	if (!(recordingTexture.isAllocated() && !hasGlContext)) {
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
	if (!recorder || !dst || size == 0) {
		return 0;
	}

	// Update last read time to track when VLC is still reading frames
	recorder->lastVlcReadTimeMicros.store(ofGetElapsedTimeMicros(), std::memory_order_release);

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

		// Check if we have a new frame to read
		const bool hasNewFrame = recorder->recordingFrameSerial > recorder->recordingReadFrameSerial;

		// If recording stopped and there are no new frames, signal EOF
		// This prevents VLC from endlessly re-reading the same last frame
		if (!recorder->videoRecordingActive.load() && !hasNewFrame) {
			return 0;
		}

		if (hasNewFrame) {
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
	return m_impl->recordingObjectRuntime.recorder.getLastFinishedAudioPath();
}

std::string ofxVlc4::getLastVideoRecordingPath() const {
	return m_impl->recordingObjectRuntime.recorder.getLastFinishedVideoPath();
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
