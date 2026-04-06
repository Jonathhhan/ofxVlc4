#include "ofxVlc4.h"
#include "audio/ofxVlc4Audio.h"
#include "media/MediaLibrary.h"
#include "media/ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "video/ofxVlc4Video.h"
#include "support/ofxVlc4Utils.h"
#include "VlcCoreSession.h"
#include "VlcEventRouter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using ofxVlc4Utils::clearAllocatedFbo;

namespace {

#ifdef TARGET_OSX
void configureMacLibVlcEnvironment() {
	const std::string bundledPluginPath = ofToDataPath("libvlc/macos/plugins", true);
	if (ofDirectory::doesDirectoryExist(bundledPluginPath, true)) {
		setenv("VLC_PLUGIN_PATH", bundledPluginPath.c_str(), 1);
	}
}
#endif
constexpr const char * kLogChannel = "ofxVlc4";
std::atomic<int> gLogLevel { static_cast<int>(OF_LOG_NOTICE) };
constexpr int kOfxVlc4AddonVersionMajor = 1;
constexpr int kOfxVlc4AddonVersionMinor = 0;
constexpr int kOfxVlc4AddonVersionPatch = 2;
constexpr const char * kOfxVlc4AddonVersionString = "1.0.2";

bool shouldLog(ofLogLevel level) {
	const ofLogLevel configuredLevel = static_cast<ofLogLevel>(gLogLevel.load());
	return configuredLevel != OF_LOG_SILENT && level >= configuredLevel;
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
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	} while (std::chrono::steady_clock::now() < deadline);

	std::error_code error;
	return !std::filesystem::exists(path, error);
}

bool tryRemoveRecordingFileOnce(const std::string & path) {
	if (path.empty()) {
		return true;
	}

	std::error_code error;
	if (!std::filesystem::exists(path, error)) {
		return !error;
	}
	if (std::filesystem::remove(path, error)) {
		return true;
	}
	return !error && !std::filesystem::exists(path, error);
}

std::string buildDefaultMuxOutputPath(const std::string & videoPath, const std::string & muxContainer) {
	std::filesystem::path outputPath(videoPath);
	outputPath.replace_filename(outputPath.stem().string() + "-muxed." + muxContainer);
	return outputPath.string();
}

std::string readTextFileIfPresent(const std::string & path) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input.is_open()) {
		return "";
	}

	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

std::vector<std::string> vlcHelpCandidatePaths(const std::string & fileName) {
	const std::string exeDir = ofFilePath::getCurrentExeDir();
	const std::string dataDir = ofToDataPath("", true);
	const std::string workingDir = ofFilePath::getCurrentWorkingDirectory();

	return {
		ofFilePath::getAbsolutePath(ofFilePath::join(exeDir, "../../" + fileName), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(exeDir, "../" + fileName), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(workingDir, fileName), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(dataDir, fileName), true),
	};
}

std::string loadBundledVlcHelpText(const std::string & fileName) {
	const std::vector<std::string> candidatePaths = vlcHelpCandidatePaths(fileName);
	for (const std::string & candidatePath : candidatePaths) {
		const std::string text = readTextFileIfPresent(candidatePath);
		if (!text.empty()) {
			return text;
		}
	}
	return "";
}

template <typename FilterInfo>
void appendFilterList(std::ostringstream & output, const std::string & title, const std::vector<FilterInfo> & filters) {
	output << title << "\n";
	if (filters.empty()) {
		output << "  (none discovered)\n";
		return;
	}

	for (const auto & filter : filters) {
		output << "  " << filter.name;
		if (!filter.shortName.empty() && filter.shortName != filter.name) {
			output << " (" << filter.shortName << ")";
		}
		if (!filter.description.empty()) {
			output << " - " << filter.description;
		}
		output << "\n";
	}
}

void logMultilineNotice(const std::string & text) {
	if (text.empty()) {
		return;
	}

	const std::vector<std::string> lines = ofSplitString(text, "\n", false, false);
	for (const std::string & line : lines) {
		ofxVlc4::logNotice(line);
	}
}

}

void ofxVlc4::syncCoreSessionStateFromLegacy() {
	if (!coreSession) {
		return;
	}

	coreSession->setInstance(libvlc);
	coreSession->setMedia(media);
	coreSession->setPlayer(mediaPlayer);
	coreSession->setPlayerEvents(mediaPlayerEventManager);
	coreSession->setMediaEvents(mediaEventManager);
	coreSession->setMediaDiscoverer(mediaDiscoverer);
	coreSession->setMediaDiscovererList(mediaDiscovererMediaList);
	coreSession->setMediaDiscovererListEvents(mediaDiscovererMediaListEventManager);
	coreSession->setRendererDiscoverer(rendererDiscoverer);
	coreSession->setRendererDiscovererEvents(rendererDiscovererEventManager);
	coreSession->setLoggingEnabled(libVlcLoggingEnabled);
	coreSession->setLogFileEnabled(libVlcLogFileEnabled);
	coreSession->setLogFilePath(libVlcLogFilePath);
	coreSession->setLogFileHandle(libVlcLogFileHandle);
}

void ofxVlc4::syncLegacyStateFromCoreSession() {
	if (!coreSession) {
		return;
	}

	libvlc = coreSession->instance();
	media = coreSession->media();
	mediaPlayer = coreSession->player();
	mediaPlayerEventManager = coreSession->playerEvents();
	mediaEventManager = coreSession->mediaEvents();
	mediaDiscoverer = coreSession->mediaDiscoverer();
	mediaDiscovererMediaList = coreSession->mediaDiscovererList();
	mediaDiscovererMediaListEventManager = coreSession->mediaDiscovererListEvents();
	rendererDiscoverer = coreSession->rendererDiscoverer();
	rendererDiscovererEventManager = coreSession->rendererDiscovererEvents();
	libVlcLoggingEnabled = coreSession->loggingEnabled();
	libVlcLogFileEnabled = coreSession->logFileEnabled();
	libVlcLogFilePath = coreSession->logFilePath();
	libVlcLogFileHandle = coreSession->logFileHandle();
}

libvlc_instance_t * ofxVlc4::sessionInstance() const {
	return coreSession ? coreSession->instance() : libvlc;
}

libvlc_media_t * ofxVlc4::sessionMedia() const {
	return coreSession ? coreSession->media() : media;
}

libvlc_media_player_t * ofxVlc4::sessionPlayer() const {
	return coreSession ? coreSession->player() : mediaPlayer;
}


ofxVlc4::ofxVlc4()
	: coreSession(subsystemRuntime.coreSession)
	, eventRouter(subsystemRuntime.eventRouter)
	, playbackController(subsystemRuntime.playbackController)
	, mediaLibraryController(subsystemRuntime.mediaLibraryController)
	, audioComponent(subsystemRuntime.audioComponent)
	, videoComponent(subsystemRuntime.videoComponent)
	, mediaComponent(subsystemRuntime.mediaComponent)
	, recorder(recordingObjectRuntime.recorder)
	, isVideoLoaded(videoFrameRuntime.isVideoLoaded)
	, startupPlaybackStatePrepared(videoFrameRuntime.startupPlaybackStatePrepared)
	, hasReceivedVideoFrame(videoFrameRuntime.hasReceivedVideoFrame)
	, exposedTextureDirty(videoFrameRuntime.exposedTextureDirty)
	, vlcFramebufferAttachmentDirty(videoFrameRuntime.vlcFramebufferAttachmentDirty)
	, ringBuffer(audioBufferRuntime.ringBuffer)
	, vlcFboBound(videoFrameRuntime.vlcFboBound)
	, libvlc(legacyCoreMirrorRuntime.libvlc)
	, media(legacyCoreMirrorRuntime.media)
	, mediaPlayer(legacyCoreMirrorRuntime.mediaPlayer)
	, mediaPlayerEventManager(legacyCoreMirrorRuntime.mediaPlayerEventManager)
	, mediaEventManager(legacyCoreMirrorRuntime.mediaEventManager)
	, mediaDiscoverer(legacyCoreMirrorRuntime.mediaDiscoverer)
	, mediaDiscovererMediaList(legacyCoreMirrorRuntime.mediaDiscovererMediaList)
	, mediaDiscovererMediaListEventManager(legacyCoreMirrorRuntime.mediaDiscovererMediaListEventManager)
	, rendererDiscoverer(legacyCoreMirrorRuntime.rendererDiscoverer)
	, rendererDiscovererEventManager(legacyCoreMirrorRuntime.rendererDiscovererEventManager)
	, videoMutex(synchronizationRuntime.videoMutex)
	, audioMutex(synchronizationRuntime.audioMutex)
	, mediaDiscovererMutex(synchronizationRuntime.mediaDiscovererMutex)
	, rendererMutex(synchronizationRuntime.rendererMutex)
	, dialogMutex(synchronizationRuntime.dialogMutex)
	, libVlcLogMutex(synchronizationRuntime.libVlcLogMutex)
	, watchTimeMutex(synchronizationRuntime.watchTimeMutex)
	, audioStateMutex(synchronizationRuntime.audioStateMutex)
	, subtitleStateMutex(synchronizationRuntime.subtitleStateMutex)
	, navigationStateMutex(synchronizationRuntime.navigationStateMutex)
	, playbackStateMutex(synchronizationRuntime.playbackStateMutex)
	, bookmarkMutex(bookmarkState.mutex)
	, playlistMutex(mediaLibrary.playlistMutex)
	, metadataCacheMutex(mediaLibrary.metadataCacheMutex)
	, thumbnailMutex(mediaRuntime.thumbnailMutex)
	, playlist(mediaLibrary.playlist)
	, bookmarksByPath(bookmarkState.entries)
	, metadataCache(mediaLibrary.metadataCache)
	, currentIndex(mediaLibrary.currentIndex)
	, currentMediaParseStatus(mediaRuntime.currentMediaParseStatus)
	, lastCompletedMediaParseStatus(mediaRuntime.lastCompletedMediaParseStatus)
	, mediaParseRequested(mediaRuntime.mediaParseRequested)
	, mediaParseActive(mediaRuntime.mediaParseActive)
	, mediaParseOptions(mediaRuntime.mediaParseOptions)
	, thumbnailRequest(mediaRuntime.thumbnailRequest)
	, lastGeneratedThumbnail(mediaRuntime.lastGeneratedThumbnail)
	, discoveredMediaItems(mediaDiscoveryRuntime.discoveredItems)
	, mediaDiscovererName(mediaDiscoveryRuntime.discovererName)
	, mediaDiscovererLongName(mediaDiscoveryRuntime.discovererLongName)
	, mediaDiscovererCategory(mediaDiscoveryRuntime.category)
	, mediaDiscovererEndReached(mediaDiscoveryRuntime.endReached)
	, rendererDiscovererName(rendererDiscoveryRuntime.discovererName)
	, selectedRendererId(rendererDiscoveryRuntime.selectedRendererId)
	, rendererStateInfo(rendererDiscoveryRuntime.stateInfo)
	, snapshotPending(mediaRuntime.snapshotPending)
	, snapshotAvailable(mediaRuntime.snapshotAvailable)
	, pendingSnapshotPath(mediaRuntime.pendingSnapshotPath)
	, lastSnapshotPath(mediaRuntime.lastSnapshotPath)
	, lastSnapshotBytes(mediaRuntime.lastSnapshotBytes)
	, lastSnapshotTimestamp(mediaRuntime.lastSnapshotTimestamp)
	, lastSnapshotEventMessage(mediaRuntime.lastSnapshotEventMessage)
	, lastSnapshotFailureReason(mediaRuntime.lastSnapshotFailureReason)
	, lastStatusMessage(diagnosticsRuntime.lastStatusMessage)
	, lastErrorMessage(diagnosticsRuntime.lastErrorMessage)
	, activeDialogs(diagnosticsRuntime.activeDialogs)
	, lastDialogError(diagnosticsRuntime.lastDialogError)
	, libVlcLoggingEnabled(diagnosticsRuntime.libVlcLoggingEnabled)
	, libVlcLogFileEnabled(diagnosticsRuntime.libVlcLogFileEnabled)
	, libVlcLogEntries(diagnosticsRuntime.libVlcLogEntries)
	, libVlcLogFilePath(diagnosticsRuntime.libVlcLogFilePath)
	, libVlcLogFileHandle(diagnosticsRuntime.libVlcLogFileHandle)
	, nativeRecordingEnabled(nativeRecordingRuntime.enabled)
	, nativeRecordingActive(nativeRecordingRuntime.active)
	, nativeRecordDirectory(nativeRecordingRuntime.directory)
	, lastNativeRecordedFilePath(nativeRecordingRuntime.lastOutputPath)
	, lastNativeRecordedFileBytes(nativeRecordingRuntime.lastOutputBytes)
	, lastNativeRecordedFileTimestamp(nativeRecordingRuntime.lastOutputTimestamp)
	, lastNativeRecordingEventMessage(nativeRecordingRuntime.lastEventMessage)
	, lastNativeRecordingFailureReason(nativeRecordingRuntime.lastFailureReason)
	, watchTimeEnabled(watchTimeRuntime.enabled)
	, watchTimeRegistered(watchTimeRuntime.registered)
	, watchTimePointAvailable(watchTimeRuntime.pointAvailable)
	, watchTimePaused(watchTimeRuntime.paused)
	, watchTimeSeeking(watchTimeRuntime.seeking)
	, watchTimeMinPeriodUs(watchTimeRuntime.minPeriodUs)
	, watchTimePauseSystemDateUs(watchTimeRuntime.pauseSystemDateUs)
	, watchTimeUpdateSequence(watchTimeRuntime.updateSequence)
	, cachedVideoTrackCount(stateCacheRuntime.cachedVideoTrackCount)
	, audioStateInfo(stateCacheRuntime.audio)
	, subtitleStateInfo(stateCacheRuntime.subtitle)
	, navigationStateInfo(stateCacheRuntime.navigation)
	, equalizerEnabled(effectsRuntime.equalizerEnabled)
	, equalizerPreamp(effectsRuntime.equalizerPreamp)
	, videoAdjustmentsEnabled(effectsRuntime.videoAdjustmentsEnabled)
	, videoAdjustmentEngine(effectsRuntime.videoAdjustmentEngine)
	, activeVideoAdjustmentEngine(effectsRuntime.activeVideoAdjustmentEngine)
	, videoAdjustContrast(effectsRuntime.videoAdjustContrast)
	, videoAdjustBrightness(effectsRuntime.videoAdjustBrightness)
	, videoAdjustHue(effectsRuntime.videoAdjustHue)
	, videoAdjustSaturation(effectsRuntime.videoAdjustSaturation)
	, videoAdjustGamma(effectsRuntime.videoAdjustGamma)
	, equalizerBandAmps(effectsRuntime.equalizerBandAmps)
	, currentEqualizerPresetIndex(effectsRuntime.currentEqualizerPresetIndex)
	, marqueeEnabled(overlayRuntime.marqueeEnabled)
	, marqueeText(overlayRuntime.marqueeText)
	, marqueePosition(overlayRuntime.marqueePosition)
	, marqueeOpacity(overlayRuntime.marqueeOpacity)
	, marqueeSize(overlayRuntime.marqueeSize)
	, marqueeColor(overlayRuntime.marqueeColor)
	, marqueeRefresh(overlayRuntime.marqueeRefresh)
	, marqueeTimeout(overlayRuntime.marqueeTimeout)
	, marqueeX(overlayRuntime.marqueeX)
	, marqueeY(overlayRuntime.marqueeY)
	, logoEnabled(overlayRuntime.logoEnabled)
	, logoPath(overlayRuntime.logoPath)
	, logoPosition(overlayRuntime.logoPosition)
	, logoOpacity(overlayRuntime.logoOpacity)
	, logoX(overlayRuntime.logoX)
	, logoY(overlayRuntime.logoY)
	, logoDelay(overlayRuntime.logoDelay)
	, logoRepeat(overlayRuntime.logoRepeat)
	, vlcFullscreenEnabled(videoPresentationRuntime.vlcFullscreenEnabled)
	, videoTitleDisplayEnabled(videoPresentationRuntime.videoTitleDisplayEnabled)
	, videoTitleDisplayPosition(videoPresentationRuntime.videoTitleDisplayPosition)
	, videoTitleDisplayTimeoutMs(videoPresentationRuntime.videoTitleDisplayTimeoutMs)
	, teletextPage(videoPresentationRuntime.teletextPage)
	, teletextTransparencyEnabled(videoPresentationRuntime.teletextTransparencyEnabled)
	, videoDeinterlaceMode(videoPresentationRuntime.videoDeinterlaceMode)
	, videoAspectRatioMode(videoPresentationRuntime.videoAspectRatioMode)
	, videoCropMode(videoPresentationRuntime.videoCropMode)
	, videoDisplayFitMode(videoPresentationRuntime.videoDisplayFitMode)
	, videoOutputBackend(videoPresentationRuntime.videoOutputBackend)
	, activeVideoOutputBackend(videoPresentationRuntime.activeVideoOutputBackend)
	, videoScale(videoPresentationRuntime.videoScale)
	, videoProjectionMode(videoPresentationRuntime.videoProjectionMode)
	, videoStereoMode(videoPresentationRuntime.videoStereoMode)
	, videoViewYaw(videoPresentationRuntime.videoViewYaw)
	, videoViewPitch(videoPresentationRuntime.videoViewPitch)
	, videoViewRoll(videoPresentationRuntime.videoViewRoll)
	, videoViewFov(videoPresentationRuntime.videoViewFov)
	, audioCaptureEnabled(playerConfigRuntime.audioCaptureEnabled)
	, audioMixMode(playerConfigRuntime.audioMixMode)
	, audioStereoMode(playerConfigRuntime.audioStereoMode)
	, audioCaptureSampleFormat(playerConfigRuntime.audioCaptureSampleFormat)
	, activeAudioCaptureSampleFormat(playerConfigRuntime.activeAudioCaptureSampleFormat)
	, audioCaptureSampleRate(playerConfigRuntime.audioCaptureSampleRate)
	, audioCaptureChannelCount(playerConfigRuntime.audioCaptureChannelCount)
	, audioCaptureBufferSeconds(playerConfigRuntime.audioCaptureBufferSeconds)
	, audioFilterChain(playerConfigRuntime.audioFilterChain)
	, videoFilterChain(playerConfigRuntime.videoFilterChain)
	, audioOutputModuleName(playerConfigRuntime.audioOutputModuleName)
	, audioOutputDeviceId(playerConfigRuntime.audioOutputDeviceId)
	, playbackRate(playerConfigRuntime.playbackRate)
	, audioDelayUs(playerConfigRuntime.audioDelayUs)
	, subtitleDelayUs(playerConfigRuntime.subtitleDelayUs)
	, subtitleTextScale(playerConfigRuntime.subtitleTextScale)
	, mediaPlayerRole(playerConfigRuntime.mediaPlayerRole)
	, keyInputEnabled(playerConfigRuntime.keyInputEnabled)
	, mouseInputEnabled(playerConfigRuntime.mouseInputEnabled)
	, channels(audioRuntime.channels)
	, sampleRate(audioRuntime.sampleRate)
	, isAudioReady(audioRuntime.ready)
	, currentVolume(audioRuntime.currentVolume)
	, audioOutputVolume(audioRuntime.outputVolume)
	, audioOutputMuted(audioRuntime.outputMuted)
	, audioCallbackCount(audioRuntime.callbackCount)
	, audioCallbackFrameCount(audioRuntime.callbackFrameCount)
	, audioCallbackSampleCount(audioRuntime.callbackSampleCount)
	, audioCallbackTotalMicros(audioRuntime.callbackTotalMicros)
	, audioCallbackMaxMicros(audioRuntime.callbackMaxMicros)
	, audioConversionTotalMicros(audioRuntime.conversionTotalMicros)
	, audioConversionMaxMicros(audioRuntime.conversionMaxMicros)
	, audioRingWriteTotalMicros(audioRuntime.ringWriteTotalMicros)
	, audioRingWriteMaxMicros(audioRuntime.ringWriteMaxMicros)
	, audioRecorderTotalMicros(audioRuntime.recorderTotalMicros)
	, audioRecorderMaxMicros(audioRuntime.recorderMaxMicros)
	, audioFirstCallbackSteadyMicros(audioRuntime.firstCallbackSteadyMicros)
	, audioLastCallbackSteadyMicros(audioRuntime.lastCallbackSteadyMicros)
	, renderWidth(videoGeometryRuntime.renderWidth)
	, renderHeight(videoGeometryRuntime.renderHeight)
	, videoWidth(videoGeometryRuntime.videoWidth)
	, videoHeight(videoGeometryRuntime.videoHeight)
	, pixelAspectNumerator(videoGeometryRuntime.pixelAspectNumerator)
	, pixelAspectDenominator(videoGeometryRuntime.pixelAspectDenominator)
	, displayAspectRatio(videoGeometryRuntime.displayAspectRatio)
	, allocatedVideoWidth(videoGeometryRuntime.allocatedVideoWidth)
	, allocatedVideoHeight(videoGeometryRuntime.allocatedVideoHeight)
	, lastBoundViewportWidth(videoGeometryRuntime.lastBoundViewportWidth)
	, lastBoundViewportHeight(videoGeometryRuntime.lastBoundViewportHeight)
	, pendingRenderWidth(videoGeometryRuntime.pendingRenderWidth)
	, pendingRenderHeight(videoGeometryRuntime.pendingRenderHeight)
	, pendingResize(videoGeometryRuntime.pendingResize)
	, mainWindow(videoResourceRuntime.mainWindow)
	, vlcWindow(videoResourceRuntime.vlcWindow)
	, playbackMode(playbackPolicyRuntime.playbackMode)
	, shuffleEnabled(playbackPolicyRuntime.shuffleEnabled)
	, pendingEqualizerApplyOnPlay(playbackPolicyRuntime.pendingEqualizerApplyOnPlay)
	, pendingVideoAdjustApplyOnPlay(playbackPolicyRuntime.pendingVideoAdjustApplyOnPlay)
	, pendingAbLoopStartTimeMs(playbackPolicyRuntime.pendingAbLoopStartTimeMs)
	, pendingAbLoopStartPosition(playbackPolicyRuntime.pendingAbLoopStartPosition)
	, videoTexture(videoResourceRuntime.videoTexture)
	, exposedTextureFbo(videoResourceRuntime.exposedTextureFbo)
	, videoAdjustShader(videoResourceRuntime.videoAdjustShader)
	, videoAdjustShaderReady(videoResourceRuntime.videoAdjustShaderReady)
	, vlcFramebufferId(videoResourceRuntime.vlcFramebufferId)
	, closeRequested(lifecycleRuntime.closeRequested)
	, shuttingDown(lifecycleRuntime.shuttingDown)
#ifdef TARGET_WIN32
	, d3d11Device(videoResourceRuntime.d3d11Device)
	, d3d11DeviceContext(videoResourceRuntime.d3d11DeviceContext)
	, d3d11Multithread(videoResourceRuntime.d3d11Multithread)
	, d3d11RenderTexture(videoResourceRuntime.d3d11RenderTexture)
	, d3d11RenderTargetView(videoResourceRuntime.d3d11RenderTargetView)
	, d3d11RenderDxgiFormat(videoResourceRuntime.d3d11RenderDxgiFormat)
#endif
	, discoveredRenderers(rendererDiscoveryRuntime.discoveredRenderers)
	, videoHdrMetadata(analysisRuntime.videoHdrMetadata)
	, watchTimeLastEventType(watchTimeRuntime.lastEventType)
	, watchTimeCallback(watchTimeRuntime.callback)
	, lastWatchTimePoint(watchTimeRuntime.lastPoint)
	, smoothedSpectrumLevels(analysisRuntime.smoothedSpectrumLevels)
	{
	ofGLFWWindowSettings settings;
	mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
	if (mainWindow) {
		settings = mainWindow->getSettings();
	}
	settings.setSize(1, 1);
	settings.setPosition(glm::vec2(-32000, -32000));
	settings.visible = false;
	settings.decorated = false;
	settings.resizable = false;
	settings.shareContextWith = mainWindow;
	vlcWindow = std::make_shared<ofAppGLFWWindow>();
	vlcWindow->setup(settings);
	vlcWindow->setVerticalSync(true);
	vlcWindow->setWindowTitle("ofxVlc4 Native Video");

	videoTexture.allocate(1, 1, GL_RGBA);
	videoTexture.getTextureData().bFlipTexture = true;
	exposedTextureFbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(exposedTextureFbo);
	allocatedVideoWidth = 1;
	allocatedVideoHeight = 1;
	audioComponent = std::make_unique<AudioComponent>(*this);
	videoComponent = std::make_unique<VideoComponent>(*this);
	mediaComponent = std::make_unique<MediaComponent>(*this);
	playbackController = std::make_unique<PlaybackController>(*this);
	mediaLibraryController = std::make_unique<MediaLibrary>(*this);
	coreSession = std::make_unique<VlcCoreSession>();
	eventRouter = std::make_unique<VlcEventRouter>(*this);
	syncLegacyStateFromCoreSession();
	equalizerBandAmps.assign(libvlc_audio_equalizer_get_band_count(), 0.0f);
}

ofxVlc4::~ofxVlc4() {
	close();
}

void ofxVlc4::setLogLevel(ofLogLevel level) {
	gLogLevel.store(static_cast<int>(level));
}

ofLogLevel ofxVlc4::getLogLevel() {
	return static_cast<ofLogLevel>(gLogLevel.load());
}

void ofxVlc4::logVerbose(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_VERBOSE)) {
		ofLogVerbose(kLogChannel) << message;
	}
}

void ofxVlc4::logError(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_ERROR)) {
		ofLogError(kLogChannel) << message;
	}
}

void ofxVlc4::logWarning(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_WARNING)) {
		ofLogWarning(kLogChannel) << message;
	}
}

void ofxVlc4::logNotice(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_NOTICE)) {
		ofLogNotice(kLogChannel) << message;
	}
}

const char * ofxVlc4::recordingAudioSourceLabel(ofxVlc4RecordingAudioSource source) {
	switch (source) {
	case ofxVlc4RecordingAudioSource::None:
		return "No audio";
	case ofxVlc4RecordingAudioSource::VlcCaptured:
		return "VLC audio";
	case ofxVlc4RecordingAudioSource::ExternalSubmitted:
		return "OF audio";
	}
	return "No audio";
}

const char * ofxVlc4::recordingSessionStateLabel(ofxVlc4RecordingSessionState state) {
	switch (state) {
	case ofxVlc4RecordingSessionState::Idle:
		return "Idle";
	case ofxVlc4RecordingSessionState::Capturing:
		return "Capturing";
	case ofxVlc4RecordingSessionState::Stopping:
		return "Stopping";
	case ofxVlc4RecordingSessionState::Finalizing:
		return "Finalizing";
	case ofxVlc4RecordingSessionState::Muxing:
		return "Muxing";
	case ofxVlc4RecordingSessionState::Done:
		return "Done";
	case ofxVlc4RecordingSessionState::Failed:
		return "Failed";
	}
	return "Idle";
}

std::string ofxVlc4::recordingVideoCodecForPreset(ofxVlc4RecordingVideoCodecPreset preset) {
	switch (preset) {
	case ofxVlc4RecordingVideoCodecPreset::H264:
		return "H264";
	case ofxVlc4RecordingVideoCodecPreset::Mp4v:
		return "MP4V";
	case ofxVlc4RecordingVideoCodecPreset::Mjpg:
		return "MJPG";
	}
	return "H264";
}

ofxVlc4RecordingVideoCodecPreset ofxVlc4::recordingVideoCodecPresetForCodec(const std::string & codec) {
	const std::string normalizedCodec = ofToUpper(ofTrim(codec));
	if (normalizedCodec == "MP4V") {
		return ofxVlc4RecordingVideoCodecPreset::Mp4v;
	}
	if (normalizedCodec == "MJPG") {
		return ofxVlc4RecordingVideoCodecPreset::Mjpg;
	}
	return ofxVlc4RecordingVideoCodecPreset::H264;
}

const char * ofxVlc4::recordingVideoCodecPresetLabel(ofxVlc4RecordingVideoCodecPreset preset) {
	switch (preset) {
	case ofxVlc4RecordingVideoCodecPreset::H264:
		return "H264";
	case ofxVlc4RecordingVideoCodecPreset::Mp4v:
		return "MP4V";
	case ofxVlc4RecordingVideoCodecPreset::Mjpg:
		return "MJPG";
	}
	return "H264";
}

std::string ofxVlc4::recordingMuxContainerForProfile(ofxVlc4RecordingMuxProfile profile) {
	switch (profile) {
	case ofxVlc4RecordingMuxProfile::Mp4Aac:
		return "mp4";
	case ofxVlc4RecordingMuxProfile::MkvFlac:
		return "mkv";
	case ofxVlc4RecordingMuxProfile::OggVorbis:
		return "ogg";
	}
	return "mp4";
}

std::string ofxVlc4::recordingMuxAudioCodecForProfile(ofxVlc4RecordingMuxProfile profile) {
	switch (profile) {
	case ofxVlc4RecordingMuxProfile::Mp4Aac:
		return "mp4a";
	case ofxVlc4RecordingMuxProfile::MkvFlac:
		return "flac";
	case ofxVlc4RecordingMuxProfile::OggVorbis:
		return "vorb";
	}
	return "mp4a";
}

const char * ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile profile) {
	switch (profile) {
	case ofxVlc4RecordingMuxProfile::Mp4Aac:
		return "MP4 / AAC";
	case ofxVlc4RecordingMuxProfile::MkvFlac:
		return "MKV / FLAC";
	case ofxVlc4RecordingMuxProfile::OggVorbis:
		return "OGG / VORBIS";
	}
	return "MP4 / AAC";
}

ofxVlc4MuxOptions ofxVlc4::recordingMuxOptionsForProfile(
	ofxVlc4RecordingMuxProfile profile,
	int sampleRate,
	int channelCount,
	bool deleteSourceFilesOnSuccess,
	uint64_t muxTimeoutMs) {
	ofxVlc4MuxOptions options;
	options.containerMux = recordingMuxContainerForProfile(profile);
	options.audioCodec = recordingMuxAudioCodecForProfile(profile);
	options.audioSampleRate = std::max(8000, sampleRate);
	options.audioChannels = std::max(1, channelCount);
	options.deleteSourceFilesOnSuccess = deleteSourceFilesOnSuccess;
	options.muxTimeoutMs = muxTimeoutMs;
	options.audioBitrateKbps = profile == ofxVlc4RecordingMuxProfile::MkvFlac ? 0 : 192;
	return options;
}

ofxVlc4RecordingSessionConfig ofxVlc4::textureRecordingSessionConfig(
	std::string outputBasePath,
	const ofTexture & texture,
	const ofxVlc4RecordingPreset & preset,
	int sampleRate,
	int channelCount,
	uint64_t muxTimeoutMs) {
	ofxVlc4RecordingSessionConfig config = textureRecordingSessionConfig(
		std::move(outputBasePath),
		texture,
		preset.audioSource,
		preset.muxProfile,
		sampleRate,
		channelCount,
		preset.deleteMuxSourceFilesOnSuccess,
		muxTimeoutMs);
	config.targetWidth = std::max(0, preset.targetWidth);
	config.targetHeight = std::max(0, preset.targetHeight);
	if (preset.audioBitrateKbps > 0 && config.muxOptions.audioCodec != "flac") {
		config.muxOptions.audioBitrateKbps = preset.audioBitrateKbps;
	}
	return config;
}

ofxVlc4RecordingSessionConfig ofxVlc4::textureRecordingSessionConfig(
	std::string outputBasePath,
	const ofTexture & texture,
	ofxVlc4RecordingAudioSource audioSource,
	ofxVlc4RecordingMuxProfile muxProfile,
	int sampleRate,
	int channelCount,
	bool deleteSourceFilesOnSuccess,
	uint64_t muxTimeoutMs) {
	ofxVlc4RecordingSessionConfig config;
	config.outputBasePath = std::move(outputBasePath);
	config.source = ofxVlc4RecordingSource::Texture;
	config.texture = &texture;
	config.audioSource = audioSource;
	config.muxOnStop = true;
	config.muxOptions = recordingMuxOptionsForProfile(
		muxProfile,
		sampleRate,
		channelCount,
		deleteSourceFilesOnSuccess,
		muxTimeoutMs);
	return config;
}

ofxVlc4RecordingSessionConfig ofxVlc4::windowRecordingSessionConfig(
	std::string outputBasePath,
	const ofxVlc4RecordingPreset & preset,
	int sampleRate,
	int channelCount,
	uint64_t muxTimeoutMs) {
	ofxVlc4RecordingSessionConfig config = windowRecordingSessionConfig(
		std::move(outputBasePath),
		preset.audioSource,
		preset.muxProfile,
		sampleRate,
		channelCount,
		preset.deleteMuxSourceFilesOnSuccess,
		muxTimeoutMs);
	config.targetWidth = std::max(0, preset.targetWidth);
	config.targetHeight = std::max(0, preset.targetHeight);
	if (preset.audioBitrateKbps > 0 && config.muxOptions.audioCodec != "flac") {
		config.muxOptions.audioBitrateKbps = preset.audioBitrateKbps;
	}
	return config;
}

ofxVlc4RecordingSessionConfig ofxVlc4::windowRecordingSessionConfig(
	std::string outputBasePath,
	ofxVlc4RecordingAudioSource audioSource,
	ofxVlc4RecordingMuxProfile muxProfile,
	int sampleRate,
	int channelCount,
	bool deleteSourceFilesOnSuccess,
	uint64_t muxTimeoutMs) {
	ofxVlc4RecordingSessionConfig config;
	config.outputBasePath = std::move(outputBasePath);
	config.source = ofxVlc4RecordingSource::Window;
	config.audioSource = audioSource;
	config.muxOnStop = true;
	config.muxOptions = recordingMuxOptionsForProfile(
		muxProfile,
		sampleRate,
		channelCount,
		deleteSourceFilesOnSuccess,
		muxTimeoutMs);
	return config;
}

void ofxVlc4::update() {
	finalizeRecordingMuxThread();
	processDeferredRecordingMuxCleanup();
	updateMidiTransport(ofGetElapsedTimef());
	if (windowCaptureRuntime.active && !recorder.isVideoCaptureActive()) {
		windowCaptureRuntime.active = false;
		unregisterWindowCaptureListener();
	}
	if (recorder.needsCaptureUpdate()) {
		updateRecorder();
	}
	updatePendingRecordingMux();
	processDeferredPlaybackActions();
}

bool ofxVlc4::ensureWindowCaptureTarget(unsigned requiredWidth, unsigned requiredHeight) {
	if (requiredWidth == 0 || requiredHeight == 0) {
		return false;
	}

	if (windowCaptureRuntime.captureFbo.isAllocated() &&
		windowCaptureRuntime.captureWidth == requiredWidth &&
		windowCaptureRuntime.captureHeight == requiredHeight) {
		return true;
	}

	if (glfwGetCurrentContext() == nullptr) {
		setError("Window recording requires a current OpenGL context.");
		return false;
	}

	clearAllocatedFbo(windowCaptureRuntime.captureFbo);
	windowCaptureRuntime.captureFbo.allocate(requiredWidth, requiredHeight, GL_RGB);
	if (!windowCaptureRuntime.captureFbo.isAllocated()) {
		windowCaptureRuntime.capturePixels.clear();
		windowCaptureRuntime.sourceTexture.clear();
		setError("Failed to allocate window capture buffer.");
		return false;
	}

	windowCaptureRuntime.captureWidth = requiredWidth;
	windowCaptureRuntime.captureHeight = requiredHeight;
	windowCaptureRuntime.captureFbo.getTexture().setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
	return true;
}

void ofxVlc4::registerWindowCaptureListener() {
	if (!windowCaptureRuntime.listeners.empty()) {
		return;
	}

	windowCaptureRuntime.listeners.push(
		ofEvents().draw.newListener(this, &ofxVlc4::onWindowCaptureDraw, OF_EVENT_ORDER_AFTER_APP + 1));
}

void ofxVlc4::unregisterWindowCaptureListener() {
	windowCaptureRuntime.listeners.unsubscribeAll();
}

void ofxVlc4::captureCurrentWindowBackbuffer() {
	if (!windowCaptureRuntime.active || !windowCaptureRuntime.captureFbo.isAllocated()) {
		return;
	}
	if (glfwGetCurrentContext() == nullptr) {
		return;
	}

	const unsigned currentWidth = static_cast<unsigned>(std::max(0, ofGetWidth()));
	const unsigned currentHeight = static_cast<unsigned>(std::max(0, ofGetHeight()));
	if (currentWidth == 0 || currentHeight == 0) {
		return;
	}

	if (!windowCaptureRuntime.capturePixels.isAllocated() ||
		windowCaptureRuntime.sourceWidth != currentWidth ||
		windowCaptureRuntime.sourceHeight != currentHeight) {
		windowCaptureRuntime.capturePixels.allocate(
			currentWidth,
			currentHeight,
			OF_PIXELS_RGB);
		windowCaptureRuntime.sourceTexture.clear();
		windowCaptureRuntime.sourceTexture.allocate(currentWidth, currentHeight, GL_RGB);
		windowCaptureRuntime.sourceTexture.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
		windowCaptureRuntime.sourceWidth = currentWidth;
		windowCaptureRuntime.sourceHeight = currentHeight;
	}
	if (!windowCaptureRuntime.capturePixels.isAllocated()) {
		return;
	}

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(
		0,
		0,
		static_cast<GLsizei>(currentWidth),
		static_cast<GLsizei>(currentHeight),
		GL_RGB,
		GL_UNSIGNED_BYTE,
		windowCaptureRuntime.capturePixels.getData());
	windowCaptureRuntime.capturePixels.mirror(true, false);
	windowCaptureRuntime.sourceTexture.loadData(windowCaptureRuntime.capturePixels);
	windowCaptureRuntime.captureFbo.begin();
	ofClear(0, 0, 0, 255);
	ofSetColor(255);
	windowCaptureRuntime.sourceTexture.draw(
		0.0f,
		0.0f,
		static_cast<float>(windowCaptureRuntime.captureWidth),
		static_cast<float>(windowCaptureRuntime.captureHeight));
	windowCaptureRuntime.captureFbo.end();
}

void ofxVlc4::onWindowCaptureDraw(ofEventArgs &) {
	if (!windowCaptureRuntime.active || shuttingDown.load()) {
		return;
	}
	if (!recorder.isVideoCaptureActive()) {
		windowCaptureRuntime.active = false;
		unregisterWindowCaptureListener();
		return;
	}

	captureCurrentWindowBackbuffer();
}

ofxVlc4AddonVersionInfo ofxVlc4::getAddonVersionInfo() {
	return {
		kOfxVlc4AddonVersionMajor,
		kOfxVlc4AddonVersionMinor,
		kOfxVlc4AddonVersionPatch,
		kOfxVlc4AddonVersionString
	};
}

void ofxVlc4::init(int vlc_argc, char const * vlc_argv[]) {
	// Re-init starts from a clean VLC state so partial previous setup cannot leak across sessions.
	syncCoreSessionStateFromLegacy();
	releaseVlcResources();
	mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
	closeRequested.store(false);
	shuttingDown.store(false);
	playbackController->resetTransportState();
	audioComponent->clearPendingEqualizerApplyOnPlay();
	videoComponent->clearPendingVideoAdjustApplyOnPlay();
	resetCurrentMediaParseState();
	clearWindowCaptureState(mainWindow);
	if (recorder.hasCleanupState()) {
		clearRecorderCaptureState(mainWindow);
	}
	clearLastMessages();
	clearLastDialogError();

#ifdef TARGET_OSX
	configureMacLibVlcEnvironment();
#endif

	libvlc = libvlc_new(vlc_argc, vlc_argv);
	if (!libvlc) {
		const char * error = libvlc_errmsg();
		setError(error ? error : "libvlc_new failed");
		return;
	}
	coreSession->setInstance(libvlc);
	syncLegacyStateFromCoreSession();

	mediaComponent->applyLibVlcLogging();

	mediaPlayer = libvlc_media_player_new(libvlc);
	if (!mediaPlayer) {
		const char * error = libvlc_errmsg();
		setError(error ? error : "libvlc_media_player_new failed");
		releaseVlcResources();
		return;
	}
	coreSession->setPlayer(mediaPlayer);
	syncLegacyStateFromCoreSession();

	resetAudioStateInfo();
	resetRendererStateInfo();
	resetSubtitleStateInfo();
	resetNavigationStateInfo();

	updateNativeVideoWindowVisibility();
	if (!applyVideoOutputBackend()) {
		releaseVlcResources();
		return;
	}

	if (audioCaptureEnabled) {
		libvlc_audio_set_callbacks(mediaPlayer, audioPlay, audioPause, audioResume, audioFlush, audioDrain, this);
		libvlc_audio_set_volume_callback(mediaPlayer, audioSetVolume);
		libvlc_audio_set_format(
			mediaPlayer,
			audioComponent->getStartupAudioCaptureSampleFormatCode(),
			audioComponent->getStartupAudioCaptureSampleRate(),
			audioComponent->getStartupAudioCaptureChannelCount());
	} else {
		isAudioReady.store(false);
		resetAudioBuffer();
	}

	mediaPlayerEventManager = libvlc_media_player_event_manager(mediaPlayer);
	coreSession->setPlayerEvents(mediaPlayerEventManager);
	syncLegacyStateFromCoreSession();
	if (mediaPlayerEventManager && coreSession && eventRouter) {
		coreSession->attachPlayerEvents(eventRouter.get(), VlcEventRouter::vlcMediaPlayerEventStatic);
	}

	const libvlc_dialog_cbs dialogCallbacks = {
		VlcEventRouter::dialogDisplayLoginStatic,
		VlcEventRouter::dialogDisplayQuestionStatic,
		VlcEventRouter::dialogDisplayProgressStatic,
		VlcEventRouter::dialogCancelStatic,
		VlcEventRouter::dialogUpdateProgressStatic
	};
	libvlc_dialog_set_callbacks(libvlc, &dialogCallbacks, eventRouter.get());
	libvlc_dialog_set_error_callback(libvlc, VlcEventRouter::dialogErrorStatic, eventRouter.get());

	if (!rendererDiscovererName.empty()) {
		startRendererDiscovery(rendererDiscovererName);
	}

	applyWatchTimeObserver();
	applyCurrentMediaPlayerSettings();
	applyEqualizerSettings();
	logNotice("Player initialized.");
}

void ofxVlc4::setError(const std::string & message) {
	lastErrorMessage = message;
	lastStatusMessage.clear();
	logError(message);
}

void ofxVlc4::setStatus(const std::string & message) {
	lastStatusMessage = message;
	lastErrorMessage.clear();
}

std::string ofxVlc4::vlcHelpModeToOptionString(ofxVlc4VlcHelpMode mode) {
	switch (mode) {
	case ofxVlc4VlcHelpMode::Help:
		return "--help";
	case ofxVlc4VlcHelpMode::FullHelp:
	default:
		return "--full-help";
	}
}

std::string ofxVlc4::getVlcHelpText(ofxVlc4VlcHelpMode mode) const {
	const std::string bundledHelp = loadBundledVlcHelpText("vlc-help.txt");
	const std::string bundledFullHelp = loadBundledVlcHelpText("vlc-full-help.txt");
	switch (mode) {
	case ofxVlc4VlcHelpMode::Help:
		if (!bundledHelp.empty()) {
			return bundledHelp;
		}
		return "ofxVlc4 VLC help reference is not available.";
	case ofxVlc4VlcHelpMode::FullHelp:
	default:
		if (!bundledFullHelp.empty()) {
			return bundledFullHelp;
		}
		return "ofxVlc4 VLC full help reference is not available.";
	}
}

void ofxVlc4::printVlcHelp(ofxVlc4VlcHelpMode mode) const {
	logMultilineNotice(getVlcHelpText(mode));
}

std::string ofxVlc4::getVlcModuleHelpText(const std::string & moduleName) const {
	const std::string trimmedName = ofxVlc4Utils::trimWhitespace(moduleName);
	if (trimmedName.empty()) {
		return "Module name is empty.";
	}

	auto matchesFilter = [&](const auto & filter) {
		return ofIsStringInString(ofToLower(filter.name), ofToLower(trimmedName)) ||
			ofIsStringInString(ofToLower(filter.shortName), ofToLower(trimmedName));
	};

	for (const auto & filter : getVideoFilters()) {
		if (!matchesFilter(filter)) {
			continue;
		}

		std::ostringstream output;
		output << "Video module: " << filter.name << "\n";
		if (!filter.shortName.empty() && filter.shortName != filter.name) {
			output << "Short name: " << filter.shortName << "\n";
		}
		if (!filter.description.empty()) {
			output << "Description: " << filter.description << "\n";
		}
		if (!filter.help.empty()) {
			output << "Help: " << filter.help << "\n";
		}
		return output.str();
	}

	for (const auto & filter : getAudioFilters()) {
		if (!matchesFilter(filter)) {
			continue;
		}

		std::ostringstream output;
		output << "Audio module: " << filter.name << "\n";
		if (!filter.shortName.empty() && filter.shortName != filter.name) {
			output << "Short name: " << filter.shortName << "\n";
		}
		if (!filter.description.empty()) {
			output << "Description: " << filter.description << "\n";
		}
		if (!filter.help.empty()) {
			output << "Help: " << filter.help << "\n";
		}
		return output.str();
	}

	const std::string bundledHelp = loadBundledVlcHelpText("vlc-full-help.txt");
	if (!bundledHelp.empty() && ofIsStringInString(ofToLower(bundledHelp), ofToLower(trimmedName))) {
		std::ostringstream output;
		output << "No dedicated runtime module entry was found for '" << trimmedName << "'.\n";
		output << "The bundled VLC reference mentions it, so use printVlcHelp(--full-help equivalent) for the broader context.";
		return output.str();
	}

	return "No matching VLC audio/video module was found for '" + trimmedName + "'.";
}

void ofxVlc4::printVlcModuleHelp(const std::string & moduleName) const {
	logMultilineNotice(getVlcModuleHelpText(moduleName));
}

void ofxVlc4::setRecordingSessionState(ofxVlc4RecordingSessionState state) {
	recordingMuxRuntime.sessionState.store(static_cast<int>(state), std::memory_order_release);
}

ofxVlc4RecordingSessionState ofxVlc4::getRecordingSessionState() const {
	return static_cast<ofxVlc4RecordingSessionState>(
		recordingMuxRuntime.sessionState.load(std::memory_order_acquire));
}

ofxVlc4RecorderSettingsInfo ofxVlc4::getRecorderSettingsInfo() const {
	ofxVlc4RecorderSettingsInfo info;
	info.preset = getRecordingPreset();
	info.activeAudioSource = getRecordingAudioSource();
	info.sessionState = getRecordingSessionState();
	info.readbackPolicy = getVideoReadbackPolicy();
	info.readbackBufferCount = getVideoReadbackBufferCount();
	info.muxPending = isRecordingMuxPending();
	info.muxInProgress = isRecordingMuxInProgress();
	info.lastMuxedPath = getLastMuxedRecordingPath();
	info.lastMuxError = getLastMuxError();
	return info;
}

ofxVlc4RecordingAudioSource ofxVlc4::getRecordingAudioSource() const {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	return recordingSessionRuntime.hasConfig
		? recordingSessionRuntime.config.audioSource
		: ofxVlc4RecordingAudioSource::None;
}

void ofxVlc4::setRecordingPresetInternal(const ofxVlc4RecordingPreset & preset) {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.preset = preset;
}

void ofxVlc4::setRecordingPreset(const ofxVlc4RecordingPreset & preset) {
	setRecordingPresetInternal(preset);
	setVideoRecordingFrameRate(preset.videoFrameRate);
	setVideoRecordingBitrateKbps(preset.videoBitrateKbps);
	setVideoRecordingCodecPreset(preset.videoCodecPreset);
}

ofxVlc4RecordingPreset ofxVlc4::getRecordingPreset() const {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	ofxVlc4RecordingPreset preset = recordingSessionRuntime.preset;
	preset.videoFrameRate = getVideoRecordingFrameRate();
	preset.videoBitrateKbps = getVideoRecordingBitrateKbps();
	preset.videoCodecPreset = getVideoRecordingCodecPreset();
	return preset;
}

void ofxVlc4::setRecordingAudioSourcePreset(ofxVlc4RecordingAudioSource source) {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.preset.audioSource = source;
}

ofxVlc4RecordingAudioSource ofxVlc4::getRecordingAudioSourcePreset() const {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	return recordingSessionRuntime.preset.audioSource;
}

void ofxVlc4::setRecordingOutputSizePreset(int width, int height) {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.preset.targetWidth = std::max(0, width);
	recordingSessionRuntime.preset.targetHeight = std::max(0, height);
}

std::pair<int, int> ofxVlc4::getRecordingOutputSizePreset() const {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	return {
		recordingSessionRuntime.preset.targetWidth,
		recordingSessionRuntime.preset.targetHeight
	};
}

void ofxVlc4::setRecordingMuxProfile(ofxVlc4RecordingMuxProfile profile) {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.preset.muxProfile = profile;
}

ofxVlc4RecordingMuxProfile ofxVlc4::getRecordingMuxProfile() const {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	return recordingSessionRuntime.preset.muxProfile;
}

void ofxVlc4::setRecordingVideoFrameRatePreset(int fps) {
	{
		std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
		recordingSessionRuntime.preset.videoFrameRate = std::max(1, fps);
	}
	setVideoRecordingFrameRate(fps);
}

int ofxVlc4::getRecordingVideoFrameRatePreset() const {
	return getVideoRecordingFrameRate();
}

void ofxVlc4::setRecordingVideoBitratePreset(int bitrateKbps) {
	{
		std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
		recordingSessionRuntime.preset.videoBitrateKbps = std::max(0, bitrateKbps);
	}
	setVideoRecordingBitrateKbps(bitrateKbps);
}

int ofxVlc4::getRecordingVideoBitratePreset() const {
	return getVideoRecordingBitrateKbps();
}

void ofxVlc4::setRecordingAudioBitratePreset(int bitrateKbps) {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.preset.audioBitrateKbps = std::max(0, bitrateKbps);
}

int ofxVlc4::getRecordingAudioBitratePreset() const {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	return recordingSessionRuntime.preset.audioBitrateKbps;
}

void ofxVlc4::setRecordingDeleteMuxSourceFilesOnSuccess(bool enabled) {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.preset.deleteMuxSourceFilesOnSuccess = enabled;
}

bool ofxVlc4::getRecordingDeleteMuxSourceFilesOnSuccess() const {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	return recordingSessionRuntime.preset.deleteMuxSourceFilesOnSuccess;
}

void ofxVlc4::clearRecordingSessionConfig() {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.hasConfig = false;
	recordingSessionRuntime.config = {};
}

void ofxVlc4::storeRecordingSessionConfig(const ofxVlc4RecordingSessionConfig & config) {
	std::lock_guard<std::mutex> lock(recordingSessionRuntime.mutex);
	recordingSessionRuntime.hasConfig = true;
	recordingSessionRuntime.config = config;
}

void ofxVlc4::finalizeRecordingMuxThread() {
	const std::shared_ptr<RecordingMuxRuntimeState::TaskState> activeTask = recordingMuxRuntime.activeTask;
	if (!recordingMuxRuntime.worker.joinable() || !activeTask || activeTask->inProgress.load()) {
		return;
	}

	recordingMuxRuntime.worker.join();
	recordingMuxRuntime.activeTask.reset();
	recordingMuxRuntime.inProgress.store(false);
	if (!activeTask->completed.exchange(false)) {
		return;
	}

	ofxVlc4MuxOptions options;
	std::string sourceVideoPath;
	std::string sourceAudioPath;
	std::string completedOutputPath;
	std::string completedError;
	{
		std::lock_guard<std::mutex> lock(activeTask->mutex);
		options = activeTask->options;
		sourceVideoPath = activeTask->sourceVideoPath;
		sourceAudioPath = activeTask->sourceAudioPath;
		completedOutputPath = activeTask->completedOutputPath;
		completedError = activeTask->completedError;
	}
	{
		std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
		recordingMuxRuntime.completedOutputPath = completedOutputPath;
		recordingMuxRuntime.completedError = completedError;
	}

	if (!completedError.empty()) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Failed);
		setError("Recording mux failed: " + completedError);
		return;
	}
	if (options.deleteSourceFilesOnSuccess) {
		const auto queueDeferredCleanup = [&](const std::string & path, const std::string & label) {
			if (path.empty()) {
				return;
			}
			std::error_code error;
			if (!std::filesystem::exists(path, error)) {
				return;
			}

			const auto deadline = std::chrono::steady_clock::now()
				+ std::chrono::milliseconds(std::max<uint64_t>(options.sourceDeleteTimeoutMs * 24, 120000));
			const auto duplicate = std::find_if(
				recordingMuxRuntime.deferredSourceCleanup.begin(),
				recordingMuxRuntime.deferredSourceCleanup.end(),
				[&](const RecordingMuxRuntimeState::DeferredSourceCleanup & item) {
					return item.path == path;
				});
			if (duplicate != recordingMuxRuntime.deferredSourceCleanup.end()) {
				duplicate->label = label;
				duplicate->deadline = deadline;
				return;
			}
			recordingMuxRuntime.deferredSourceCleanup.push_back({ path, label, deadline });
		};

		if (!tryRemoveRecordingFileOnce(sourceVideoPath)) {
			queueDeferredCleanup(sourceVideoPath, "video");
		}
		if (!tryRemoveRecordingFileOnce(sourceAudioPath)) {
			queueDeferredCleanup(sourceAudioPath, "audio");
		}
	}
	if (!completedOutputPath.empty()) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Done);
		setStatus("Recording mux saved: " + completedOutputPath);
	} else {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Idle);
	}
}

void ofxVlc4::processDeferredRecordingMuxCleanup(bool finalPass) {
	std::vector<RecordingMuxRuntimeState::DeferredSourceCleanup> cleanupItems;
	{
		std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
		if (recordingMuxRuntime.deferredSourceCleanup.empty()) {
			return;
		}
		cleanupItems.swap(recordingMuxRuntime.deferredSourceCleanup);
	}

	const auto now = std::chrono::steady_clock::now();
	std::vector<RecordingMuxRuntimeState::DeferredSourceCleanup> retryItems;
	for (auto & item : cleanupItems) {
		const bool removed = finalPass
			? removeRecordingFile(item.path, 1500)
			: tryRemoveRecordingFileOnce(item.path);
		if (removed) {
			continue;
		}
		if (finalPass || now >= item.deadline) {
			std::error_code error;
			if (std::filesystem::exists(item.path, error)) {
				logWarning("Recording mux kept source " + item.label + " file: " + item.path);
			}
			continue;
		}
		retryItems.push_back(std::move(item));
	}

	if (retryItems.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
	for (auto & item : retryItems) {
		const auto duplicate = std::find_if(
			recordingMuxRuntime.deferredSourceCleanup.begin(),
			recordingMuxRuntime.deferredSourceCleanup.end(),
			[&](const RecordingMuxRuntimeState::DeferredSourceCleanup & existingItem) {
				return existingItem.path == item.path;
			});
		if (duplicate != recordingMuxRuntime.deferredSourceCleanup.end()) {
			duplicate->label = item.label;
			duplicate->deadline = std::max(duplicate->deadline, item.deadline);
			continue;
		}
		recordingMuxRuntime.deferredSourceCleanup.push_back(std::move(item));
	}
}

void ofxVlc4::updatePendingRecordingMux() {
	if (!recordingMuxRuntime.pending.load()) {
		return;
	}
	const std::shared_ptr<RecordingMuxRuntimeState::TaskState> activeTask = recordingMuxRuntime.activeTask;
	if (activeTask && activeTask->inProgress.load()) {
		return;
	}
	if (recorder.hasActiveCaptureSession()) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Finalizing);
		return;
	}

	const std::string finishedVideoPath = recorder.getLastFinishedVideoPath();
	const std::string finishedAudioPath = recorder.getLastFinishedAudioPath();
	ofxVlc4MuxOptions options;
	std::string previousVideoPath;
	std::string previousAudioPath;
	std::string expectedVideoPath;
	std::string expectedAudioPath;
	std::string requestedOutputPath;
	{
		std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
		options = recordingMuxRuntime.options;
		previousVideoPath = recordingMuxRuntime.previousVideoPath;
		previousAudioPath = recordingMuxRuntime.previousAudioPath;
		expectedVideoPath = recordingMuxRuntime.expectedVideoPath;
		expectedAudioPath = recordingMuxRuntime.expectedAudioPath;
		requestedOutputPath = recordingMuxRuntime.requestedOutputPath;
	}

	const std::string resolvedVideoPath = !finishedVideoPath.empty() ? finishedVideoPath : expectedVideoPath;
	const std::string resolvedAudioPath = !finishedAudioPath.empty() ? finishedAudioPath : expectedAudioPath;
	if (resolvedVideoPath.empty() || resolvedAudioPath.empty()) {
		return;
	}
	if (resolvedVideoPath == previousVideoPath || resolvedAudioPath == previousAudioPath) {
		return;
	}

	recordingMuxRuntime.pending.store(false);
	recordingMuxRuntime.inProgress.store(true);
	setRecordingSessionState(ofxVlc4RecordingSessionState::Muxing);
	const std::string outputPath =
		requestedOutputPath.empty()
			? buildDefaultMuxOutputPath(resolvedVideoPath, options.containerMux)
			: requestedOutputPath;
	const auto task = std::make_shared<RecordingMuxRuntimeState::TaskState>();
	{
		std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
		recordingMuxRuntime.activeTask = task;
	}
	{
		std::lock_guard<std::mutex> lock(task->mutex);
		task->options = options;
		task->sourceVideoPath = resolvedVideoPath;
		task->sourceAudioPath = resolvedAudioPath;
		task->outputPath = outputPath;
	}
	setStatus("Muxing recording...");
	recordingMuxRuntime.worker = std::thread([task, resolvedVideoPath, resolvedAudioPath, outputPath, options]() {
		std::string muxError;
		const bool muxed = ofxVlc4::muxRecordingFilesInternal(
			resolvedVideoPath,
			resolvedAudioPath,
			outputPath,
			options,
			&task->cancelRequested,
			&muxError);
		{
			std::lock_guard<std::mutex> lock(task->mutex);
			task->completedOutputPath = muxed ? outputPath : std::string();
			task->completedError = muxed ? std::string() : muxError;
		}
		task->inProgress.store(false);
		task->completed.store(true);
	});
}

void ofxVlc4::cancelPendingRecordingMux() {
	recordingMuxRuntime.pending.store(false);
	const std::shared_ptr<RecordingMuxRuntimeState::TaskState> activeTask = recordingMuxRuntime.activeTask;
	if (activeTask) {
		activeTask->cancelRequested.store(true, std::memory_order_release);
	}
	if (recordingMuxRuntime.worker.joinable()) {
		if (activeTask && activeTask->inProgress.load()) {
			recordingMuxRuntime.worker.detach();
		} else {
			recordingMuxRuntime.worker.join();
		}
	}
	recordingMuxRuntime.inProgress.store(false);
	recordingMuxRuntime.activeTask.reset();
	setRecordingSessionState(ofxVlc4RecordingSessionState::Idle);
	std::lock_guard<std::mutex> lock(recordingMuxRuntime.mutex);
	recordingMuxRuntime.previousVideoPath.clear();
	recordingMuxRuntime.previousAudioPath.clear();
	recordingMuxRuntime.expectedVideoPath.clear();
	recordingMuxRuntime.expectedAudioPath.clear();
	recordingMuxRuntime.requestedOutputPath.clear();
	recordingMuxRuntime.completedOutputPath.clear();
	recordingMuxRuntime.completedError.clear();
	clearRecordingSessionConfig();
}

bool ofxVlc4::isInitialized() const {
	return sessionInstance() != nullptr && sessionPlayer() != nullptr;
}

void ofxVlc4::clearRecorderCaptureState(const std::shared_ptr<ofAppGLFWWindow> & cleanupWindow) {
	if (cleanupWindow) {
		cleanupWindow->makeCurrent();
	}
	recorder.clearCaptureState();
}

void ofxVlc4::clearWindowCaptureState(const std::shared_ptr<ofAppGLFWWindow> & cleanupWindow) {
	unregisterWindowCaptureListener();
	windowCaptureRuntime.active = false;
	windowCaptureRuntime.includeAudioCapture = false;
	windowCaptureRuntime.sourceWidth = 0;
	windowCaptureRuntime.sourceHeight = 0;
	windowCaptureRuntime.captureWidth = 0;
	windowCaptureRuntime.captureHeight = 0;
	windowCaptureRuntime.capturePixels.clear();
	if (cleanupWindow) {
		cleanupWindow->makeCurrent();
	}
	windowCaptureRuntime.sourceTexture.clear();
	clearAllocatedFbo(windowCaptureRuntime.captureFbo);
}

void ofxVlc4::releaseVlcResources() {
	syncCoreSessionStateFromLegacy();
	finalizeRecordingMuxThread();
	cancelPendingRecordingMux();
	detachEvents();
	dismissAllDialogs();
	stopMediaDiscoveryInternal();
	stopRendererDiscoveryInternal();
	std::shared_ptr<ofAppGLFWWindow> cleanupWindow = vlcWindow ? vlcWindow : mainWindow;
	const bool recorderNeedsCleanup = recorder.hasCleanupState();
	const bool needsGlCleanup =
		vlcFramebufferId != 0 ||
		videoTexture.isAllocated() ||
		exposedTextureFbo.isAllocated() ||
		windowCaptureRuntime.captureFbo.isAllocated() ||
		recorderNeedsCleanup;

	if (mediaPlayer) {
		if (watchTimeRegistered) {
			libvlc_media_player_unwatch_time(mediaPlayer);
			watchTimeRegistered = false;
		}
		libvlc_video_set_adjust_int(mediaPlayer, libvlc_adjust_Enable, 0);
		libvlc_media_player_release(mediaPlayer);
		mediaPlayer = nullptr;
		coreSession->setPlayer(nullptr);
		coreSession->setPlayerEvents(nullptr);
	}

	clearCurrentMedia(false);

	if (cleanupWindow && needsGlCleanup) {
		updateNativeVideoWindowVisibility();
		cleanupWindow->makeCurrent();
	}

	if (recorderNeedsCleanup) {
		clearRecorderCaptureState(nullptr);
	}
	clearWindowCaptureState(nullptr);

	if (needsGlCleanup) {
		videoComponent->clearPublishedFrameFence();
		if (vlcWindow && vlcFramebufferId != 0) {
			glDeleteFramebuffers(1, &vlcFramebufferId);
			vlcFramebufferId = 0;
		}
		videoTexture.clear();
		clearAllocatedFbo(exposedTextureFbo);
	}
	if (cleanupWindow && needsGlCleanup) {
		glfwMakeContextCurrent(nullptr);
	}
	releaseD3D11Resources();
	clearVideoHdrMetadata();
	allocatedVideoWidth = 1;
	allocatedVideoHeight = 1;

	if (libvlc) {
		libvlc_log_unset(libvlc);
		mediaComponent->closeLibVlcLogFile();
		libvlc_dialog_set_error_callback(libvlc, nullptr, nullptr);
		libvlc_dialog_set_callbacks(libvlc, nullptr, nullptr);
		libvlc_release(libvlc);
		libvlc = nullptr;
		coreSession->setInstance(nullptr);
	}
	processDeferredRecordingMuxCleanup(true);

	clearWatchTimeState();
	clearMidiTransport();

	resetAudioStateInfo();
	resetRendererStateInfo();
	resetSubtitleStateInfo();
	resetNavigationStateInfo();

	activeVideoOutputBackend = videoOutputBackend;
	activeVideoAdjustmentEngine = videoAdjustmentEngine;
	syncLegacyStateFromCoreSession();
}

void ofxVlc4::close() {
	bool expected = false;
	if (!closeRequested.compare_exchange_strong(expected, true)) {
		return;
	}

	playbackController->prepareForClose();

	// Only flip the hard shutdown guard once playback had a chance to unwind cleanly.
	shuttingDown.store(true);
	releaseVlcResources();
	isAudioReady.store(false);
	isVideoLoaded.store(false);
	startupPlaybackStatePrepared.store(false);
	hasReceivedVideoFrame.store(false);
	vlcFboBound = false;
	setStatus("Player closed.");
}

void ofxVlc4::updateMidiTransport(double nowSeconds) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	if (!midiRuntime.playback.isLoaded()) {
		return;
	}

	if (midiRuntime.syncToWatchTime && midiRuntime.syncSource == ofxVlc4MidiSyncSource::WatchTime) {
		const WatchTimeInfo watchTime = getWatchTimeInfo();
		const int64_t targetTimeUs = watchTime.interpolatedTimeUs >= 0 ? watchTime.interpolatedTimeUs : watchTime.timeUs;
		if (watchTime.available && targetTimeUs >= 0) {
			const double targetSeconds = static_cast<double>(targetTimeUs) / 1000000.0;
			const double currentSeconds = midiRuntime.playback.getPositionSeconds();
			const bool discontinuity = midiRuntime.lastWatchTimeUs < 0 ||
				std::llabs(targetTimeUs - midiRuntime.lastWatchTimeUs) > 200000;
			const bool drifted = std::abs(currentSeconds - targetSeconds) > 0.05;

			if (watchTime.seeking || discontinuity || drifted) {
				midiRuntime.playback.seek(targetSeconds);
			}

			if (watchTime.paused) {
				if (midiRuntime.playback.isPlaying()) {
					midiRuntime.playback.pause(nowSeconds);
				}
			} else if (!midiRuntime.playback.isPlaying()) {
				midiRuntime.playback.play(nowSeconds);
			}

			midiRuntime.lastWatchTimeUs = targetTimeUs;
		}
	}

	midiRuntime.playback.update(nowSeconds);
}

bool ofxVlc4::loadMidiFile(const std::string & path, bool noteOffAsZeroVelocity) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	MidiAnalysisReport report = midiRuntime.analyzer.analyzeFile(path);
	if (!report.valid) {
		midiRuntime.report = report;
		midiRuntime.messages.clear();
		midiRuntime.playback.clear();
		midiRuntime.lastWatchTimeUs = -1;
		return false;
	}

	std::vector<MidiChannelMessage> messages = MidiBridge::toMessages(report, noteOffAsZeroVelocity);
	if (!midiRuntime.playback.load(path, report, messages)) {
		midiRuntime.report = report;
		midiRuntime.messages.clear();
		midiRuntime.lastWatchTimeUs = -1;
		return false;
	}

	midiRuntime.noteOffAsZeroVelocity = noteOffAsZeroVelocity;
	midiRuntime.report = std::move(report);
	midiRuntime.messages = std::move(messages);
	midiRuntime.lastWatchTimeUs = -1;
	return true;
}

void ofxVlc4::clearMidiTransport() {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.clear();
	midiRuntime.report = {};
	midiRuntime.messages.clear();
	midiRuntime.lastWatchTimeUs = -1;
}

bool ofxVlc4::hasMidiLoaded() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.isLoaded();
}

bool ofxVlc4::isMidiPlaying() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.isPlaying();
}

bool ofxVlc4::isMidiPaused() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.isPaused();
}

bool ofxVlc4::isMidiFinished() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.isFinished();
}

double ofxVlc4::getMidiDurationSeconds() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.getDurationSeconds();
}

double ofxVlc4::getMidiPositionSeconds() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.getPositionSeconds();
}

double ofxVlc4::getMidiTempoMultiplier() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.getTempoMultiplier();
}

void ofxVlc4::playMidi() {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.play(ofGetElapsedTimef());
}

void ofxVlc4::pauseMidi() {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.pause(ofGetElapsedTimef());
}

void ofxVlc4::stopMidi() {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.stop();
}

void ofxVlc4::seekMidi(double seconds) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.seek(seconds);
}

void ofxVlc4::setMidiTempoMultiplier(double multiplier) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.setTempoMultiplier(multiplier, ofGetElapsedTimef());
}

MidiAnalysisReport ofxVlc4::getMidiAnalysisReport() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.report;
}

std::vector<MidiChannelMessage> ofxVlc4::getMidiMessages() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.messages;
}

ofxVlc4::MidiTransportInfo ofxVlc4::getMidiTransportInfo() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	MidiTransportInfo info;
	info.loaded = midiRuntime.playback.isLoaded();
	info.playing = midiRuntime.playback.isPlaying();
	info.paused = midiRuntime.playback.isPaused();
	info.finished = midiRuntime.playback.isFinished();
	info.durationSeconds = midiRuntime.playback.getDurationSeconds();
	info.positionSeconds = midiRuntime.playback.getPositionSeconds();
	info.tempoMultiplier = midiRuntime.playback.getTempoMultiplier();
	info.dispatchedCount = midiRuntime.playback.getDispatchedCount();
	info.messageCount = midiRuntime.messages.size();
	info.syncSource = midiRuntime.syncSource;
	info.syncToWatchTime = midiRuntime.syncToWatchTime;
	info.hasCallback = midiRuntime.playback.hasMessageCallback();
	info.syncSettings = midiRuntime.playback.getSyncSettings();
	return info;
}

void ofxVlc4::setMidiMessageCallback(MidiMessageCallback callback) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.setMessageCallback(std::move(callback));
}

void ofxVlc4::clearMidiMessageCallback() {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.clearMessageCallback();
}

bool ofxVlc4::hasMidiMessageCallback() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.hasMessageCallback();
}

void ofxVlc4::setMidiSyncSettings(const MidiSyncSettings & settings) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.playback.setSyncSettings(settings);
}

MidiSyncSettings ofxVlc4::getMidiSyncSettings() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.playback.getSyncSettings();
}

void ofxVlc4::setMidiSyncSource(ofxVlc4MidiSyncSource source) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.syncSource = source;
}

ofxVlc4MidiSyncSource ofxVlc4::getMidiSyncSource() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.syncSource;
}

void ofxVlc4::setMidiSyncToWatchTimeEnabled(bool enabled) {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	midiRuntime.syncToWatchTime = enabled;
	if (!enabled) {
		midiRuntime.lastWatchTimeUs = -1;
	}
}

bool ofxVlc4::isMidiSyncToWatchTimeEnabled() const {
	std::lock_guard<std::mutex> lock(midiRuntime.mutex);
	return midiRuntime.syncToWatchTime;
}


