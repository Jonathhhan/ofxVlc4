#pragma once

// Heavy includes only needed by internal implementation:
#include "ofxVlc4.h"
#include "GLFW/glfw3.h"
#include "midi/ofxVlc4MidiPlayback.h"
#include "recording/ofxVlc4Recorder.h"
#include "media/MediaLibraryState.h"
#include "support/ofxVlc4RingBuffer.h"
#include "VlcCoreSession.h"
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

struct ofxVlc4::Impl {
	static constexpr float kDefaultEqualizerPreampDb = 12.0f;

	struct RendererItemEntry {
		std::string id;
		std::string name;
		std::string type;
		std::string iconUri;
		bool canAudio = false;
		bool canVideo = false;
		libvlc_renderer_item_t * item = nullptr;
	};

	struct BookmarkState {
		mutable std::mutex mutex;
		std::unordered_map<std::string, std::vector<BookmarkInfo>> entries;
	};

	struct MediaRuntimeState {
		mutable std::mutex thumbnailMutex;
		std::atomic<int> currentMediaParseStatus { static_cast<int>(MediaParseStatus::None) };
		std::atomic<int> lastCompletedMediaParseStatus { static_cast<int>(MediaParseStatus::None) };
		std::atomic<bool> mediaParseRequested { false };
		std::atomic<bool> mediaParseActive { false };
		MediaParseOptions mediaParseOptions;
		libvlc_media_thumbnail_request_t * thumbnailRequest = nullptr;
		ThumbnailInfo lastGeneratedThumbnail;
		bool snapshotPending = false;
		bool snapshotAvailable = false;
		std::string pendingSnapshotPath;
		std::string lastSnapshotPath;
		uint64_t lastSnapshotBytes = 0;
		std::string lastSnapshotTimestamp;
		std::string lastSnapshotEventMessage;
		std::string lastSnapshotFailureReason;
	};

	struct MediaDiscoveryRuntimeState {
		std::vector<DiscoveredMediaItemInfo> discoveredItems;
		std::string discovererName;
		std::string discovererLongName;
		MediaDiscovererCategory category = MediaDiscovererCategory::Lan;
		bool endReached = false;
	};

	struct RendererDiscoveryRuntimeState {
		std::string discovererName;
		std::string selectedRendererId;
		RendererStateInfo stateInfo;
		std::vector<RendererItemEntry> discoveredRenderers;
	};

	struct DiagnosticsRuntimeState {
		std::string lastStatusMessage;
		std::string lastErrorMessage;
		std::vector<DialogInfo> activeDialogs;
		DialogErrorInfo lastDialogError;
		bool libVlcLoggingEnabled = false;
		bool libVlcLogFileEnabled = false;
		std::vector<LibVlcLogEntry> libVlcLogEntries;
		std::string libVlcLogFilePath;
		FILE * libVlcLogFileHandle = nullptr;
	};

	struct NativeRecordingRuntimeState {
		bool enabled = false;
		std::atomic<bool> active { false };
		std::string directory;
		std::string lastOutputPath;
		uint64_t lastOutputBytes = 0;
		std::string lastOutputTimestamp;
		std::string lastEventMessage;
		std::string lastFailureReason;
	};

	struct WatchTimeRuntimeState {
		bool enabled = false;
		bool registered = false;
		bool pointAvailable = false;
		bool paused = false;
		bool seeking = false;
		int64_t minPeriodUs = 100000;
		int64_t pauseSystemDateUs = 0;
		uint64_t updateSequence = 0;
		WatchTimeEventType lastEventType = WatchTimeEventType::Update;
		WatchTimeCallback callback;
		libvlc_media_player_time_point_t lastPoint {};
	};

	struct StateCacheRuntimeState {
		std::atomic<int> cachedVideoTrackCount { 0 };
		std::atomic<double> cachedVideoTrackFps { 0.0 };
		AudioStateInfo audio;
		SubtitleStateInfo subtitle;
		NavigationStateInfo navigation;
	};

	struct EffectsRuntimeState {
		bool equalizerEnabled = true;
		float equalizerPreamp = Impl::kDefaultEqualizerPreampDb;
		bool videoAdjustmentsEnabled = false;
		VideoAdjustmentEngine videoAdjustmentEngine = VideoAdjustmentEngine::Auto;
		VideoAdjustmentEngine activeVideoAdjustmentEngine = VideoAdjustmentEngine::Auto;
		float videoAdjustContrast = 1.0f;
		float videoAdjustBrightness = 1.0f;
		float videoAdjustHue = 0.0f;
		float videoAdjustSaturation = 1.0f;
		float videoAdjustGamma = 1.0f;
		std::vector<float> equalizerBandAmps;
		int currentEqualizerPresetIndex = -1;
	};

	struct OverlayRuntimeState {
		bool marqueeEnabled = false;
		std::string marqueeText;
		OverlayPosition marqueePosition = OverlayPosition::Bottom;
		int marqueeOpacity = 255;
		int marqueeSize = 24;
		int marqueeColor = 0xFFFFFF;
		int marqueeRefresh = 0;
		int marqueeTimeout = 0;
		int marqueeX = 0;
		int marqueeY = 0;
		bool logoEnabled = false;
		std::string logoPath;
		OverlayPosition logoPosition = OverlayPosition::TopRight;
		int logoOpacity = 255;
		int logoX = 0;
		int logoY = 0;
		int logoDelay = 0;
		int logoRepeat = 0;
	};

	struct VideoPresentationRuntimeState {
		bool vlcFullscreenEnabled = false;
		bool videoTitleDisplayEnabled = false;
		OverlayPosition videoTitleDisplayPosition = OverlayPosition::Bottom;
		unsigned videoTitleDisplayTimeoutMs = 3000;
		int teletextPage = 0;
		bool teletextTransparencyEnabled = false;
		VideoDeinterlaceMode videoDeinterlaceMode = VideoDeinterlaceMode::Auto;
		VideoAspectRatioMode videoAspectRatioMode = VideoAspectRatioMode::Default;
		VideoCropMode videoCropMode = VideoCropMode::None;
		VideoDisplayFitMode videoDisplayFitMode = VideoDisplayFitMode::Smaller;
		VideoOutputBackend videoOutputBackend = VideoOutputBackend::Texture;
		VideoOutputBackend activeVideoOutputBackend = VideoOutputBackend::Texture;
		PreferredDecoderDevice preferredDecoderDevice = PreferredDecoderDevice::Any;
		float videoScale = 0.0f;
		VideoProjectionMode videoProjectionMode = VideoProjectionMode::Auto;
		VideoStereoMode videoStereoMode = VideoStereoMode::Auto;
		float videoViewYaw = 0.0f;
		float videoViewPitch = 0.0f;
		float videoViewRoll = 0.0f;
		float videoViewFov = 80.0f;
	};

	struct PlayerConfigRuntimeState {
		bool audioCaptureEnabled = true;
		AudioMixMode audioMixMode = AudioMixMode::Auto;
		AudioStereoMode audioStereoMode = AudioStereoMode::Auto;
		AudioCaptureSampleFormat audioCaptureSampleFormat = AudioCaptureSampleFormat::Float32;
		std::atomic<int> activeAudioCaptureSampleFormat { static_cast<int>(AudioCaptureSampleFormat::Float32) };
		int audioCaptureSampleRate = 44100;
		int audioCaptureChannelCount = 2;
		double audioCaptureBufferSeconds = 0.75;
		std::string audioFilterChain;
		std::string videoFilterChain;
		std::string audioOutputModuleName;
		std::string audioOutputDeviceId;
		float playbackRate = 1.0f;
		int64_t audioDelayUs = 0;
		int64_t subtitleDelayUs = 0;
		float subtitleTextScale = 1.0f;
		ofxVlc4SubtitleTextRenderer subtitleTextRenderer = ofxVlc4SubtitleTextRenderer::Auto;
		std::string subtitleFontFamily;
		int subtitleTextColor = 16777215;
		int subtitleTextOpacity = 255;
		bool subtitleBold = false;
		MediaPlayerRole mediaPlayerRole = MediaPlayerRole::None;
		bool keyInputEnabled = true;
		bool mouseInputEnabled = true;
		std::vector<std::string> extraInitArgs;
		ofxVlc4AudioVisualizerSettings audioVisualizerSettings;
	};

	struct AudioRuntimeState {
		std::atomic<int> channels { 0 };
		std::atomic<int> sampleRate { 0 };
		std::atomic<bool> ready { false };
		std::atomic<int> currentVolume { 50 };
		std::atomic<float> outputVolume { 0.5f };
		std::atomic<bool> outputMuted { false };
		std::atomic<uint64_t> callbackCount { 0 };
		std::atomic<uint64_t> callbackFrameCount { 0 };
		std::atomic<uint64_t> callbackSampleCount { 0 };
		std::atomic<uint64_t> callbackTotalMicros { 0 };
		std::atomic<uint64_t> callbackMaxMicros { 0 };
		std::atomic<uint64_t> conversionTotalMicros { 0 };
		std::atomic<uint64_t> conversionMaxMicros { 0 };
		std::atomic<uint64_t> ringWriteTotalMicros { 0 };
		std::atomic<uint64_t> ringWriteMaxMicros { 0 };
		std::atomic<uint64_t> recorderTotalMicros { 0 };
		std::atomic<uint64_t> recorderMaxMicros { 0 };
		std::atomic<uint64_t> firstCallbackSteadyMicros { 0 };
		std::atomic<uint64_t> lastCallbackSteadyMicros { 0 };
	};

	struct VideoGeometryRuntimeState {
		std::atomic<unsigned> renderWidth { 0 };
		std::atomic<unsigned> renderHeight { 0 };
		std::atomic<unsigned> videoWidth { 0 };
		std::atomic<unsigned> videoHeight { 0 };
		std::atomic<unsigned> pixelAspectNumerator { 1 };
		std::atomic<unsigned> pixelAspectDenominator { 1 };
		std::atomic<float> displayAspectRatio { 1.0f };
		unsigned allocatedVideoWidth = 1;
		unsigned allocatedVideoHeight = 1;
		int allocatedGlPixelFormat = static_cast<int>(GL_RGBA);
		unsigned lastBoundViewportWidth = 0;
		unsigned lastBoundViewportHeight = 0;
		std::atomic<unsigned> pendingRenderWidth { 0 };
		std::atomic<unsigned> pendingRenderHeight { 0 };
		std::atomic<bool> pendingResize { false };
		std::atomic<int> pendingGlPixelFormat { static_cast<int>(GL_RGBA) };
	};

	struct PlaybackPolicyRuntimeState {
		PlaybackMode playbackMode = PlaybackMode::Default;
		bool shuffleEnabled = false;
		std::atomic<bool> pendingEqualizerApplyOnPlay { false };
		std::atomic<bool> pendingVideoAdjustApplyOnPlay { false };
		int64_t pendingAbLoopStartTimeMs = -1;
		float pendingAbLoopStartPosition = -1.0f;
	};

	struct VideoResourceRuntimeState {
		std::shared_ptr<ofAppGLFWWindow> mainWindow;
		std::shared_ptr<ofAppGLFWWindow> vlcWindow;
		bool nativeWindowGeometryInitialized = false;
		ofTexture videoTexture;
		ofFbo exposedTextureFbo;
		ofShader videoAdjustShader;
		bool videoAdjustShaderReady = false;
		GLuint vlcFramebufferId = 0;
#ifdef TARGET_WIN32
		struct ID3D11Device * d3d11Device = nullptr;
		struct ID3D11DeviceContext * d3d11DeviceContext = nullptr;
		struct ID3D10Multithread * d3d11Multithread = nullptr;
		struct ID3D11Texture2D * d3d11RenderTexture = nullptr;
		struct ID3D11RenderTargetView * d3d11RenderTargetView = nullptr;
		int d3d11RenderDxgiFormat = 0;
#endif
	};

	struct WindowCaptureRuntimeState {
		bool active = false;
		bool includeAudioCapture = false;
		unsigned sourceWidth = 0;
		unsigned sourceHeight = 0;
		unsigned captureWidth = 0;
		unsigned captureHeight = 0;
		ofTexture sourceTexture;
		ofFbo captureFbo;
		ofPixels capturePixels;
		ofEventListeners listeners;
	};

	struct RecordingMuxRuntimeState {
		struct DeferredSourceCleanup {
			std::string path;
			std::string label;
			std::chrono::steady_clock::time_point deadline;
		};

		struct TaskState {
			mutable std::mutex mutex;
			ofxVlc4MuxOptions options;
			std::string sourceVideoPath;
			std::string sourceAudioPath;
			std::string outputPath;
			std::string completedOutputPath;
			std::string completedError;
			std::atomic<bool> cancelRequested { false };
			std::atomic<bool> inProgress { true };
			std::atomic<bool> completed { false };
		};

		mutable std::mutex mutex;
		ofxVlc4MuxOptions options;
		std::string previousVideoPath;
		std::string previousAudioPath;
		std::string expectedVideoPath;
		std::string expectedAudioPath;
		std::string requestedOutputPath;
		std::string completedOutputPath;
		std::string completedError;
		std::vector<DeferredSourceCleanup> deferredSourceCleanup;
		std::atomic<bool> pending { false };
		std::atomic<bool> inProgress { false };
		std::atomic<int> sessionState { static_cast<int>(ofxVlc4RecordingSessionState::Idle) };
		std::shared_ptr<TaskState> activeTask;
		std::thread worker;
	};

	struct RecordingSessionRuntimeState {
		mutable std::mutex mutex;
		bool hasConfig = false;
		ofxVlc4RecordingSessionConfig config;
		ofxVlc4RecordingPreset preset;
	};

	struct MidiRuntimeState {
		mutable std::mutex mutex;
		MidiFileAnalyzer analyzer;
		MidiAnalysisReport report;
		std::vector<MidiChannelMessage> messages;
		MidiPlaybackSession playback;
		ofxVlc4MidiSyncSource syncSource = ofxVlc4MidiSyncSource::Internal;
		bool noteOffAsZeroVelocity = false;
		bool syncToWatchTime = false;
		int64_t lastWatchTimeUs = -1;
	};

	struct LifecycleRuntimeState {
		std::atomic<bool> closeRequested { false };
		std::atomic<bool> shuttingDown { false };
	};

	struct AnalysisRuntimeState {
		VideoHdrMetadataInfo videoHdrMetadata;
		mutable std::vector<float> smoothedSpectrumLevels;
	};

	struct SynchronizationRuntimeState {
		mutable std::mutex videoMutex;
		mutable std::mutex audioMutex;
		mutable std::mutex mediaDiscovererMutex;
		mutable std::mutex rendererMutex;
		mutable std::mutex dialogMutex;
		mutable std::mutex libVlcLogMutex;
		mutable std::mutex watchTimeMutex;
		mutable std::mutex audioStateMutex;
		mutable std::mutex subtitleStateMutex;
		mutable std::mutex navigationStateMutex;
		mutable std::mutex playbackStateMutex;
	};

	struct SubsystemRuntimeState {
		std::unique_ptr<VlcCoreSession> coreSession;
		std::unique_ptr<VlcEventRouter> eventRouter;
		std::unique_ptr<PlaybackController> playbackController;
		std::unique_ptr<MediaLibrary> mediaLibraryController;
		std::unique_ptr<AudioComponent> audioComponent;
		std::unique_ptr<VideoComponent> videoComponent;
		std::unique_ptr<MediaComponent> mediaComponent;
	};

	struct RecordingObjectRuntimeState {
		ofxVlc4Recorder recorder;
	};

	struct VideoFrameRuntimeState {
		std::atomic<bool> isVideoLoaded { false };
		std::atomic<bool> startupPlaybackStatePrepared { false };
		std::atomic<bool> hasReceivedVideoFrame { false };
		std::atomic<bool> exposedTextureDirty { true };
		std::atomic<bool> vlcFramebufferAttachmentDirty { true };
		GLsync publishedVideoFrameFence = nullptr;
		bool vlcFboBound = false;
	};

	struct AudioBufferRuntimeState {
		ofxVlc4RingBuffer ringBuffer;
	};

	// Runtime state:
	BookmarkState bookmarkState;
	MediaLibraryState mediaLibrary;
	MediaRuntimeState mediaRuntime;
	MediaDiscoveryRuntimeState mediaDiscoveryRuntime;
	RendererDiscoveryRuntimeState rendererDiscoveryRuntime;
	DiagnosticsRuntimeState diagnosticsRuntime;
	NativeRecordingRuntimeState nativeRecordingRuntime;
	WatchTimeRuntimeState watchTimeRuntime;
	StateCacheRuntimeState stateCacheRuntime;
	EffectsRuntimeState effectsRuntime;
	OverlayRuntimeState overlayRuntime;
	VideoPresentationRuntimeState videoPresentationRuntime;
	PlayerConfigRuntimeState playerConfigRuntime;
	AudioRuntimeState audioRuntime;
	VideoGeometryRuntimeState videoGeometryRuntime;
	PlaybackPolicyRuntimeState playbackPolicyRuntime;
	VideoResourceRuntimeState videoResourceRuntime;
	WindowCaptureRuntimeState windowCaptureRuntime;
	RecordingSessionRuntimeState recordingSessionRuntime;
	MidiRuntimeState midiRuntime;
	RecordingMuxRuntimeState recordingMuxRuntime;
	LifecycleRuntimeState lifecycleRuntime;
	AnalysisRuntimeState analysisRuntime;
	SynchronizationRuntimeState synchronizationRuntime;
	SubsystemRuntimeState subsystemRuntime;
	RecordingObjectRuntimeState recordingObjectRuntime;
	VideoFrameRuntimeState videoFrameRuntime;
	AudioBufferRuntimeState audioBufferRuntime;
};
