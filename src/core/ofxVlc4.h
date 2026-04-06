#pragma once

#include "midi/ofxVlc4MidiPlayback.h"
#include "recording/ofxVlc4Recorder.h"
#include "media/MediaLibraryState.h"
#include "support/ofxVlc4RingBuffer.h"
#include "VlcCoreSession.h"
#include "ofMain.h"
#include "GLFW/glfw3.h"
#include "vlc/vlc.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <initializer_list>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

typedef struct libvlc_renderer_discoverer_t libvlc_renderer_discoverer_t;
typedef struct libvlc_renderer_item_t libvlc_renderer_item_t;
typedef struct libvlc_dialog_id libvlc_dialog_id;
typedef struct libvlc_media_discoverer_t libvlc_media_discoverer_t;
typedef struct libvlc_media_list_t libvlc_media_list_t;
class VlcCoreSession;
class VlcEventRouter;
class PlaybackController;
class MediaLibrary;

struct ofxVlc4AddonVersionInfo {
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::string versionString;
};

struct ofxVlc4MuxOptions {
	std::string containerMux = "mp4";
	std::string audioCodec = "mp4a";
	int audioChannels = 2;
	int audioSampleRate = 44100;
	int audioBitrateKbps = 192;
	uint64_t inputReadyTimeoutMs = 2000;
	uint64_t outputReadyTimeoutMs = 1000;
	uint64_t muxTimeoutMs = 15000;
	uint64_t sourceDeleteTimeoutMs = 5000;
	bool deleteSourceFilesOnSuccess = false;
};

enum class ofxVlc4RecordingSessionState {
	Idle = 0,
	Capturing,
	Stopping,
	Finalizing,
	Muxing,
	Done,
	Failed
};

struct ofxVlc4RecordingStartOptions {
	bool includeAudioCapture = false;
	int outputWidth = 0;
	int outputHeight = 0;
};

enum class ofxVlc4RecordingSource {
	Texture = 0,
	Window
};

enum class ofxVlc4RecordingAudioSource {
	None = 0,
	VlcCaptured,
	ExternalSubmitted
};

enum class ofxVlc4RecordingVideoCodecPreset {
	H264 = 0,
	H265,
	Mp4v,
	Mjpg
};

enum class ofxVlc4RecordingMuxProfile {
	Mp4Aac = 0,
	MkvOpus,
	MkvFlac,
	MkvLpcm,
	OggVorbis
};

struct ofxVlc4RecordingPreset {
	ofxVlc4RecordingAudioSource audioSource = ofxVlc4RecordingAudioSource::ExternalSubmitted;
	ofxVlc4RecordingVideoCodecPreset videoCodecPreset = ofxVlc4RecordingVideoCodecPreset::H264;
	ofxVlc4RecordingMuxProfile muxProfile = ofxVlc4RecordingMuxProfile::Mp4Aac;
	int targetWidth = 0;
	int targetHeight = 0;
	int videoFrameRate = 30;
	int videoBitrateKbps = 8000;
	int audioBitrateKbps = 192;
	bool deleteMuxSourceFilesOnSuccess = true;
};

struct ofxVlc4RecordingSessionConfig {
	std::string outputBasePath;
	ofxVlc4RecordingSource source = ofxVlc4RecordingSource::Texture;
	const ofTexture * texture = nullptr;
	ofxVlc4RecordingAudioSource audioSource = ofxVlc4RecordingAudioSource::None;
	int targetWidth = 0;
	int targetHeight = 0;
	bool muxOnStop = false;
	std::string muxOutputPath;
	ofxVlc4MuxOptions muxOptions;
};

struct ofxVlc4RecorderSettingsInfo {
	ofxVlc4RecordingPreset preset;
	ofxVlc4RecordingAudioSource activeAudioSource = ofxVlc4RecordingAudioSource::None;
	ofxVlc4RecordingSessionState sessionState = ofxVlc4RecordingSessionState::Idle;
	ofxVlc4VideoReadbackPolicy readbackPolicy = ofxVlc4VideoReadbackPolicy::DropLateFrames;
	size_t readbackBufferCount = 3;
	bool muxPending = false;
	bool muxInProgress = false;
	std::string lastMuxedPath;
	std::string lastMuxError;
};

enum class ofxVlc4MidiSyncSource {
	Internal = 0,
	WatchTime
};

enum class ofxVlc4VlcHelpMode {
	Help = 0,
	FullHelp
};

class ofxVlc4 {
public:
	static ofxVlc4AddonVersionInfo getAddonVersionInfo();

	struct MediaTrackInfo {
		std::string id;
		std::string name;
		std::string language;
		std::string description;
		std::string codecName;
		std::string codecFourcc;
		std::string originalFourcc;
		std::string subtitleEncoding;
		unsigned bitrate = 0;
		int profile = 0;
		int level = 0;
		unsigned channels = 0;
		unsigned sampleRate = 0;
		unsigned width = 0;
		unsigned height = 0;
		unsigned sampleAspectNum = 0;
		unsigned sampleAspectDen = 0;
		unsigned frameRateNum = 0;
		unsigned frameRateDen = 0;
		bool idStable = false;
		bool selected = false;
	};

	struct AudioOutputDeviceInfo {
		std::string id;
		std::string description;
		bool current = false;
	};

	struct AudioOutputModuleInfo {
		std::string name;
		std::string description;
		bool current = false;
	};

	struct AudioFilterInfo {
		std::string name;
		std::string shortName;
		std::string description;
		std::string help;
	};

	struct VideoFilterInfo {
		std::string name;
		std::string shortName;
		std::string description;
		std::string help;
	};

	enum class MediaDiscovererCategory {
		Devices = 0,
		Lan,
		Podcasts,
		LocalDirs
	};

	struct MediaDiscovererInfo {
		std::string name;
		std::string longName;
		MediaDiscovererCategory category = MediaDiscovererCategory::Lan;
		bool current = false;
	};

	struct DiscoveredMediaItemInfo {
		std::string mrl;
		std::string name;
		bool isDirectory = false;
	};

	struct MediaDiscoveryStateInfo {
		std::string discovererName;
		std::string discovererLongName;
		MediaDiscovererCategory category = MediaDiscovererCategory::Lan;
		bool active = false;
		bool endReached = false;
		size_t itemCount = 0;
		size_t directoryCount = 0;
	};

	enum class DialogType {
		Login = 0,
		Question,
		Progress
	};

	enum class DialogQuestionSeverity {
		Normal = 0,
		Warning,
		Critical
	};

	struct DialogInfo {
		std::uintptr_t token = 0;
		DialogType type = DialogType::Login;
		DialogQuestionSeverity severity = DialogQuestionSeverity::Normal;
		std::string title;
		std::string text;
		std::string defaultUsername;
		bool askStore = false;
		bool progressIndeterminate = false;
		float progressPosition = 0.0f;
		bool cancellable = false;
		std::string cancelLabel;
		std::string action1Label;
		std::string action2Label;
	};

	struct DialogErrorInfo {
		bool available = false;
		std::string title;
		std::string text;
	};

	struct LibVlcLogEntry {
		int level = LIBVLC_NOTICE;
		std::string module;
		std::string file;
		unsigned line = 0;
		std::string objectName;
		std::string objectHeader;
		std::uintptr_t objectId = 0;
		std::string message;
	};

	enum class MediaPlayerRole {
		None = 0,
		Music,
		Video,
		Communication,
		Game,
		Notification,
		Animation,
		Production,
		Accessibility,
		Test
	};

	enum class WatchTimeEventType {
		Update = 0,
		Paused,
		Seek,
		SeekEnd
	};

	struct WatchTimeInfo {
		WatchTimeEventType eventType = WatchTimeEventType::Update;
		uint64_t sequence = 0;
		bool enabled = false;
		bool registered = false;
		bool available = false;
		bool paused = false;
		bool seeking = false;
		int64_t minPeriodUs = 0;
		double position = 0.0;
		double rate = 1.0;
		int64_t timeUs = -1;
		int64_t lengthUs = 0;
		int64_t systemDateUs = 0;
		int64_t interpolatedTimeUs = -1;
		double interpolatedPosition = 0.0;
	};

	using WatchTimeCallback = std::function<void(const WatchTimeInfo &)>;

	enum class MediaMetaField {
		Title = 0,
		Artist,
		Album,
		Genre,
		Description,
		Date,
		Language,
		Publisher,
		NowPlaying,
		Director,
		ShowName,
		ArtworkUrl,
		Url
	};

	struct TitleInfo {
		int index = -1;
		std::string name;
		int64_t durationMs = -1;
		bool current = false;
		bool isMenu = false;
		bool isInteractive = false;
	};

	struct ChapterInfo {
		int index = -1;
		std::string name;
		int64_t timeOffsetMs = 0;
		int64_t durationMs = -1;
		bool current = false;
	};

	struct ProgramInfo {
		int id = -1;
		std::string name;
		bool current = false;
		bool scrambled = false;
	};

	struct MediaStats {
		bool available = false;
		uint64_t readBytes = 0;
		float inputBitrate = 0.0f;
		uint64_t demuxReadBytes = 0;
		float demuxBitrate = 0.0f;
		uint64_t demuxCorrupted = 0;
		uint64_t demuxDiscontinuity = 0;
		uint64_t decodedVideo = 0;
		uint64_t decodedAudio = 0;
		uint64_t displayedPictures = 0;
		uint64_t latePictures = 0;
		uint64_t lostPictures = 0;
		uint64_t playedAudioBuffers = 0;
		uint64_t lostAudioBuffers = 0;
	};

	struct PlaylistItemInfo {
		int index = -1;
		std::string path;
		std::string label;
		bool current = false;
		bool uri = false;
		bool metadataCached = false;
	};

	struct PlaylistStateInfo {
		std::vector<PlaylistItemInfo> items;
		int currentIndex = -1;
		bool hasCurrent = false;
		size_t size = 0;
		bool empty = true;
	};

	struct BookmarkInfo {
		std::string id;
		std::string label;
		int timeMs = 0;
		bool current = false;
	};

	struct ThumbnailInfo {
		bool available = false;
		bool requestActive = false;
		std::string path;
		int64_t timeMs = -1;
		unsigned width = 0;
		unsigned height = 0;
	};

	struct AbLoopInfo {
		enum class State {
			None = 0,
			A,
			B
		};

		State state = State::None;
		int64_t aTimeMs = -1;
		float aPosition = -1.0f;
		int64_t bTimeMs = -1;
		float bPosition = -1.0f;
	};

	enum class MediaSlaveType {
		Subtitle = 0,
		Audio
	};

	enum class NavigationMode {
		Activate = 0,
		Up,
		Down,
		Left,
		Right,
		Popup
	};

	enum class MediaParseStatus {
		None = 0,
		Pending,
		Skipped,
		Failed,
		Timeout,
		Cancelled,
		Done
	};

	struct MediaParseInfo {
		MediaParseStatus status = MediaParseStatus::None;
		MediaParseStatus lastCompletedStatus = MediaParseStatus::None;
		bool requested = false;
		bool active = false;
	};

	struct MediaParseOptions {
		bool parseLocal = true;
		bool parseNetwork = true;
		bool forced = true;
		bool fetchLocal = false;
		bool fetchNetwork = false;
		bool doInteract = true;
		int timeoutMs = -1;
	};

	enum class TeletextKey {
		Red = 0,
		Green,
		Yellow,
		Blue,
		Index
	};

	enum class PlayerCommand {
		PlayPause = 0,
		Play,
		Pause,
		Stop,
		NextItem,
		PreviousItem,
		SeekForwardSmall,
		SeekBackwardSmall,
		SeekForwardLarge,
		SeekBackwardLarge,
		VolumeUp,
		VolumeDown,
		ToggleMute,
		NextFrame,
		PreviousChapter,
		NextChapter,
		MenuActivate,
		MenuUp,
		MenuDown,
		MenuLeft,
		MenuRight,
		MenuPopup,
		TeletextRed,
		TeletextGreen,
		TeletextYellow,
		TeletextBlue,
		TeletextIndex,
		ToggleTeletextTransparency
	};

	enum class ThumbnailImageType {
		Png = 0,
		Jpg,
		WebP
	};

	enum class ThumbnailSeekSpeed {
		Precise = 0,
		Fast
	};

	struct MediaSlaveInfo {
		std::string uri;
		MediaSlaveType type = MediaSlaveType::Subtitle;
		unsigned priority = 0;
	};

	struct RendererDiscovererInfo {
		std::string name;
		std::string longName;
	};

	struct RendererInfo {
		std::string id;
		std::string name;
		std::string type;
		std::string iconUri;
		bool canAudio = false;
		bool canVideo = false;
		bool selected = false;
	};

	// PlaybackMode mirrors the small policy surface the example exposes in its transport UI.
	enum class PlaybackMode {
		Default,
		Repeat,
		Loop
	};

	enum class AudioMixMode {
		Auto = 0,
		Stereo,
		Binaural,
		Surround4_0,
		Surround5_1,
		Surround7_1
	};

	enum class AudioStereoMode {
		Auto = 0,
		Stereo,
		ReverseStereo,
		Left,
		Right,
		DolbySurround,
		Mono
	};

	enum class AudioCaptureSampleFormat {
		Float32 = 0,
		Signed16,
		Signed32
	};

	struct AudioCallbackPerformanceInfo {
		bool available = false;
		uint64_t callbackCount = 0;
		uint64_t frameCount = 0;
		uint64_t sampleCount = 0;
		double callbackRateHz = 0.0;
		double averageFramesPerCallback = 0.0;
		double averageSamplesPerCallback = 0.0;
		double averageCallbackMicros = 0.0;
		uint64_t maxCallbackMicros = 0;
		double averageConversionMicros = 0.0;
		uint64_t maxConversionMicros = 0;
		double averageRingWriteMicros = 0.0;
		uint64_t maxRingWriteMicros = 0;
		double averageRecorderMicros = 0.0;
		uint64_t maxRecorderMicros = 0;
		double averageOtherMicros = 0.0;
	};

	struct AudioStateInfo {
		bool ready = false;
		bool paused = false;
		bool tracksAvailable = false;
		int trackCount = 0;
		bool volumeKnown = false;
		int volume = 50;
		bool mutedKnown = false;
		bool muted = false;
		bool deviceKnown = false;
		std::string deviceId;
		AudioMixMode mixMode = AudioMixMode::Auto;
		AudioStereoMode stereoMode = AudioStereoMode::Auto;
		int audioDelayMs = 0;
		bool audioPtsAvailable = false;
		int64_t audioPtsUs = -1;
		int64_t audioPtsSystemUs = 0;
		AudioCallbackPerformanceInfo callbackPerformance;
	};

	enum class VideoProjectionMode : int {
		Auto = -1,
		Rectangular = 0,
		Equirectangular = 1,
		CubemapStandard = 2
	};

	enum class VideoStereoMode {
		Auto = 0,
		Stereo,
		LeftEye,
		RightEye,
		SideBySide
	};

	enum class VideoDeinterlaceMode {
		Auto = 0,
		Off,
		Blend,
		Bob,
		Linear,
		X,
		Yadif,
		Yadif2x,
		Phosphor,
		Ivtc
	};

	enum class VideoAspectRatioMode {
		Default = 0,
		Fill,
		Ratio16_9,
		Ratio16_10,
		Ratio4_3,
		Ratio1_1,
		Ratio21_9,
		Ratio235_1
	};

	enum class VideoCropMode {
		None = 0,
		Ratio16_9,
		Ratio16_10,
		Ratio4_3,
		Ratio1_1,
		Ratio21_9,
		Ratio235_1
	};

	enum class VideoDisplayFitMode {
		Smaller = 0,
		Larger,
		Width,
		Height,
		Scale
	};

	enum class VideoOutputBackend {
		Texture = 0,
		NativeWindow,
		D3D11Metadata
	};

	enum class VideoAdjustmentEngine {
		Auto = 0,
		LibVlc,
		Shader
	};

	struct VideoHdrMetadataInfo {
		bool supported = false;
		bool available = false;
		unsigned width = 0;
		unsigned height = 0;
		unsigned bitDepth = 0;
		bool fullRange = false;
		libvlc_video_color_space_t colorspace = static_cast<libvlc_video_color_space_t>(0);
		libvlc_video_color_primaries_t primaries = static_cast<libvlc_video_color_primaries_t>(0);
		libvlc_video_transfer_func_t transfer = static_cast<libvlc_video_transfer_func_t>(0);
		uint16_t redPrimaryX = 0;
		uint16_t redPrimaryY = 0;
		uint16_t greenPrimaryX = 0;
		uint16_t greenPrimaryY = 0;
		uint16_t bluePrimaryX = 0;
		uint16_t bluePrimaryY = 0;
		uint16_t whitePointX = 0;
		uint16_t whitePointY = 0;
		unsigned maxMasteringLuminance = 0;
		unsigned minMasteringLuminance = 0;
		uint16_t maxContentLightLevel = 0;
		uint16_t maxFrameAverageLightLevel = 0;
	};

	struct VideoStateInfo {
		bool startupPrepared = false;
		bool geometryKnown = false;
		bool loaded = false;
		bool frameReceived = false;
		bool tracksAvailable = false;
		int trackCount = 0;
		unsigned sourceWidth = 0;
		unsigned sourceHeight = 0;
		unsigned renderWidth = 0;
		unsigned renderHeight = 0;
		unsigned pixelAspectNumerator = 1;
		unsigned pixelAspectDenominator = 1;
		float displayAspectRatio = 1.0f;
		unsigned videoOutputCount = 0;
		bool hasVideoOutput = false;
		bool videoAdjustmentsEnabled = false;
		VideoAdjustmentEngine videoAdjustmentEngine = VideoAdjustmentEngine::Auto;
		VideoAdjustmentEngine activeVideoAdjustmentEngine = VideoAdjustmentEngine::Auto;
		bool vlcFullscreenEnabled = false;
		bool teletextTransparencyEnabled = false;
		int teletextPage = 0;
		float scale = 0.0f;
		float yaw = 0.0f;
		float pitch = 0.0f;
		float roll = 0.0f;
		float fov = 80.0f;
		VideoDeinterlaceMode deinterlaceMode = VideoDeinterlaceMode::Auto;
		VideoAspectRatioMode aspectRatioMode = VideoAspectRatioMode::Default;
		VideoCropMode cropMode = VideoCropMode::None;
		VideoDisplayFitMode displayFitMode = VideoDisplayFitMode::Smaller;
		VideoOutputBackend outputBackend = VideoOutputBackend::Texture;
		VideoOutputBackend activeOutputBackend = VideoOutputBackend::Texture;
		VideoProjectionMode projectionMode = VideoProjectionMode::Auto;
		VideoStereoMode stereoMode = VideoStereoMode::Auto;
		VideoHdrMetadataInfo hdrMetadata;
	};

	struct SnapshotStateInfo {
		bool pending = false;
		bool available = false;
		std::string lastRequestedPath;
		std::string lastSavedPath;
		bool lastSavedMetadataAvailable = false;
		uint64_t lastSavedBytes = 0;
		std::string lastSavedTimestamp;
		std::string lastEventMessage;
		std::string lastFailureReason;
	};

	struct NativeRecordingStateInfo {
		bool enabled = false;
		bool active = false;
		bool lastOutputPathAvailable = false;
		std::string directory;
		std::string lastOutputPath;
		bool lastOutputMetadataAvailable = false;
		uint64_t lastOutputBytes = 0;
		std::string lastOutputTimestamp;
		std::string lastEventMessage;
		std::string lastFailureReason;
	};

	struct MidiTransportInfo {
		bool loaded = false;
		bool playing = false;
		bool paused = false;
		bool finished = false;
		double durationSeconds = 0.0;
		double positionSeconds = 0.0;
		double tempoMultiplier = 1.0;
		size_t dispatchedCount = 0;
		size_t messageCount = 0;
		ofxVlc4MidiSyncSource syncSource = ofxVlc4MidiSyncSource::Internal;
		bool syncToWatchTime = false;
		bool hasCallback = false;
		MidiSyncSettings syncSettings;
	};

	struct EqualizerPresetInfo {
		int index = -1;
		std::string name;
		float preamp = 0.0f;
		std::vector<float> bandAmps;
		bool matchesCurrent = false;
	};

	struct PlaybackStateInfo {
		PlaybackMode mode = PlaybackMode::Default;
		bool shuffleEnabled = false;
		bool mediaAttached = false;
		bool startupPrepared = false;
		bool geometryKnown = false;
		bool hasReceivedVideoFrame = false;
		bool playbackWanted = false;
		bool pauseRequested = false;
		bool playing = false;
		bool stopped = true;
		bool transitioning = false;
		bool restartPending = false;
		bool seekable = false;
		float position = 0.0f;
		int timeMs = 0;
		float lengthMs = 0.0f;
		float rate = 1.0f;
		WatchTimeInfo watchTime;
		AudioStateInfo audio;
		VideoStateInfo video;
		SnapshotStateInfo snapshot;
		NativeRecordingStateInfo nativeRecording;
		MidiTransportInfo midi;
	};

	struct MediaReadinessInfo {
		bool mediaAttached = false;
		bool startupPrepared = false;
		bool geometryKnown = false;
		bool videoLoaded = false;
		bool hasReceivedVideoFrame = false;
		bool audioReady = false;
		bool playbackActive = false;
		MediaParseStatus parseStatus = MediaParseStatus::None;
		MediaParseStatus lastCompletedParseStatus = MediaParseStatus::None;
		bool parseRequested = false;
		bool parseActive = false;
		int videoTrackCount = 0;
		int audioTrackCount = 0;
		int subtitleTrackCount = 0;
		bool videoTracksReady = false;
		bool audioTracksReady = false;
		bool subtitleTracksReady = false;
		int titleCount = 0;
		int chapterCount = 0;
		int programCount = 0;
		bool navigationReady = false;
	};

	struct RendererStateInfo {
		bool discoveryActive = false;
		std::string discovererName;
		size_t discoveredRendererCount = 0;
		std::string requestedRendererId;
		bool selectedRendererKnown = false;
		RendererInfo selectedRenderer;
		bool selectedRendererAvailable = false;
		bool usingLocalFallback = true;
		bool reconnectPending = false;
	};

	struct SubtitleStateInfo {
		bool trackListAvailable = false;
		size_t trackCount = 0;
		bool trackSelected = false;
		std::string selectedTrackId;
		std::string selectedTrackLabel;
		int delayMs = 0;
		float textScale = 1.0f;
		int teletextPage = 0;
		bool teletextTransparencyEnabled = false;
	};

	struct NavigationStateInfo {
		bool available = false;
		int currentProgramId = -1;
		int programCount = 0;
		int currentTitleIndex = -1;
		int titleCount = 0;
		int currentChapterIndex = -1;
		int chapterCount = 0;
	};

	enum class OverlayPosition {
		Center = 0,
		Left,
		Right,
		Top,
		TopLeft,
		TopRight,
		Bottom,
		BottomLeft,
		BottomRight
	};

public:
	class AudioComponent;
	class VideoComponent;
	class MediaComponent;

private:
	static constexpr float kDefaultEqualizerPreampDb = 12.0f;
	friend class VlcEventRouter;
	friend class PlaybackController;
	friend class MediaLibrary;

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
		AudioStateInfo audio;
		SubtitleStateInfo subtitle;
		NavigationStateInfo navigation;
	};

	struct EffectsRuntimeState {
		bool equalizerEnabled = true;
		float equalizerPreamp = kDefaultEqualizerPreampDb;
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
		MediaPlayerRole mediaPlayerRole = MediaPlayerRole::None;
		bool keyInputEnabled = true;
		bool mouseInputEnabled = true;
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
		unsigned lastBoundViewportWidth = 0;
		unsigned lastBoundViewportHeight = 0;
		std::atomic<unsigned> pendingRenderWidth { 0 };
		std::atomic<unsigned> pendingRenderHeight { 0 };
		std::atomic<bool> pendingResize { false };
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

	struct LegacyCoreMirrorState {
		libvlc_instance_t * libvlc = nullptr;
		libvlc_media_t * media = nullptr;
		libvlc_media_player_t * mediaPlayer = nullptr;
		libvlc_event_manager_t * mediaPlayerEventManager = nullptr;
		libvlc_event_manager_t * mediaEventManager = nullptr;
		libvlc_media_discoverer_t * mediaDiscoverer = nullptr;
		libvlc_media_list_t * mediaDiscovererMediaList = nullptr;
		libvlc_event_manager_t * mediaDiscovererMediaListEventManager = nullptr;
		libvlc_renderer_discoverer_t * rendererDiscoverer = nullptr;
		libvlc_event_manager_t * rendererDiscovererEventManager = nullptr;
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

	// Transitional phase-1 refactor state:
	// coreSession becomes the ownership boundary first, while legacy members are still mirrored
	// until playback/media/video code is moved over incrementally.
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
	LegacyCoreMirrorState legacyCoreMirrorRuntime;
	SubsystemRuntimeState subsystemRuntime;
	RecordingObjectRuntimeState recordingObjectRuntime;
	VideoFrameRuntimeState videoFrameRuntime;
	AudioBufferRuntimeState audioBufferRuntime;
	std::unique_ptr<VlcCoreSession> & coreSession;
	std::unique_ptr<VlcEventRouter> & eventRouter;
	std::unique_ptr<PlaybackController> & playbackController;
	std::unique_ptr<MediaLibrary> & mediaLibraryController;
	std::unique_ptr<AudioComponent> & audioComponent;
	std::unique_ptr<VideoComponent> & videoComponent;
	std::unique_ptr<MediaComponent> & mediaComponent;
	ofxVlc4Recorder & recorder;
	std::atomic<bool> & isVideoLoaded;
	std::atomic<bool> & startupPlaybackStatePrepared;
	std::atomic<bool> & hasReceivedVideoFrame;
	std::atomic<bool> & exposedTextureDirty;
	std::atomic<bool> & vlcFramebufferAttachmentDirty;
	ofxVlc4RingBuffer & ringBuffer;
	bool & vlcFboBound;
	libvlc_instance_t * & libvlc;
	libvlc_media_t * & media;
	libvlc_media_player_t * & mediaPlayer;
	libvlc_event_manager_t * & mediaPlayerEventManager;
	libvlc_event_manager_t * & mediaEventManager;
	libvlc_media_discoverer_t * & mediaDiscoverer;
	libvlc_media_list_t * & mediaDiscovererMediaList;
	libvlc_event_manager_t * & mediaDiscovererMediaListEventManager;
	libvlc_renderer_discoverer_t * & rendererDiscoverer;
	libvlc_event_manager_t * & rendererDiscovererEventManager;
	std::mutex & videoMutex;
	std::mutex & audioMutex;
	std::mutex & mediaDiscovererMutex;
	std::mutex & rendererMutex;
	std::mutex & dialogMutex;
	std::mutex & libVlcLogMutex;
	std::mutex & watchTimeMutex;
	std::mutex & audioStateMutex;
	std::mutex & subtitleStateMutex;
	std::mutex & navigationStateMutex;
	std::mutex & playbackStateMutex;
	std::mutex & bookmarkMutex;
	std::mutex & playlistMutex;
	std::mutex & metadataCacheMutex;
	std::mutex & thumbnailMutex;
	std::vector<std::string> & playlist;
	std::unordered_map<std::string, std::vector<BookmarkInfo>> & bookmarksByPath;
	std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> & metadataCache;
	int & currentIndex;
	std::atomic<int> & currentMediaParseStatus;
	std::atomic<int> & lastCompletedMediaParseStatus;
	std::atomic<bool> & mediaParseRequested;
	std::atomic<bool> & mediaParseActive;
	MediaParseOptions & mediaParseOptions;
	libvlc_media_thumbnail_request_t * & thumbnailRequest;
	ThumbnailInfo & lastGeneratedThumbnail;
	std::vector<DiscoveredMediaItemInfo> & discoveredMediaItems;
	std::string & mediaDiscovererName;
	std::string & mediaDiscovererLongName;
	MediaDiscovererCategory & mediaDiscovererCategory;
	bool & mediaDiscovererEndReached;
	std::string & rendererDiscovererName;
	std::string & selectedRendererId;
	RendererStateInfo & rendererStateInfo;
	bool & snapshotPending;
	bool & snapshotAvailable;
	std::string & pendingSnapshotPath;
	std::string & lastSnapshotPath;
	uint64_t & lastSnapshotBytes;
	std::string & lastSnapshotTimestamp;
	std::string & lastSnapshotEventMessage;
	std::string & lastSnapshotFailureReason;
	std::string & lastStatusMessage;
	std::string & lastErrorMessage;
	std::vector<DialogInfo> & activeDialogs;
	DialogErrorInfo & lastDialogError;
	bool & libVlcLoggingEnabled;
	bool & libVlcLogFileEnabled;
	std::vector<LibVlcLogEntry> & libVlcLogEntries;
	std::string & libVlcLogFilePath;
	FILE * & libVlcLogFileHandle;
	bool & nativeRecordingEnabled;
	std::atomic<bool> & nativeRecordingActive;
	std::string & nativeRecordDirectory;
	std::string & lastNativeRecordedFilePath;
	uint64_t & lastNativeRecordedFileBytes;
	std::string & lastNativeRecordedFileTimestamp;
	std::string & lastNativeRecordingEventMessage;
	std::string & lastNativeRecordingFailureReason;
	bool & watchTimeEnabled;
	bool & watchTimeRegistered;
	bool & watchTimePointAvailable;
	bool & watchTimePaused;
	bool & watchTimeSeeking;
	int64_t & watchTimeMinPeriodUs;
	int64_t & watchTimePauseSystemDateUs;
	uint64_t & watchTimeUpdateSequence;
	std::atomic<int> & cachedVideoTrackCount;
	AudioStateInfo & audioStateInfo;
	SubtitleStateInfo & subtitleStateInfo;
	NavigationStateInfo & navigationStateInfo;
	bool & equalizerEnabled;
	float & equalizerPreamp;
	bool & videoAdjustmentsEnabled;
	VideoAdjustmentEngine & videoAdjustmentEngine;
	VideoAdjustmentEngine & activeVideoAdjustmentEngine;
	float & videoAdjustContrast;
	float & videoAdjustBrightness;
	float & videoAdjustHue;
	float & videoAdjustSaturation;
	float & videoAdjustGamma;
	std::vector<float> & equalizerBandAmps;
	int & currentEqualizerPresetIndex;
	bool & marqueeEnabled;
	std::string & marqueeText;
	OverlayPosition & marqueePosition;
	int & marqueeOpacity;
	int & marqueeSize;
	int & marqueeColor;
	int & marqueeRefresh;
	int & marqueeTimeout;
	int & marqueeX;
	int & marqueeY;
	bool & logoEnabled;
	std::string & logoPath;
	OverlayPosition & logoPosition;
	int & logoOpacity;
	int & logoX;
	int & logoY;
	int & logoDelay;
	int & logoRepeat;
	bool & vlcFullscreenEnabled;
	bool & videoTitleDisplayEnabled;
	OverlayPosition & videoTitleDisplayPosition;
	unsigned & videoTitleDisplayTimeoutMs;
	int & teletextPage;
	bool & teletextTransparencyEnabled;
	VideoDeinterlaceMode & videoDeinterlaceMode;
	VideoAspectRatioMode & videoAspectRatioMode;
	VideoCropMode & videoCropMode;
	VideoDisplayFitMode & videoDisplayFitMode;
	VideoOutputBackend & videoOutputBackend;
	VideoOutputBackend & activeVideoOutputBackend;
	float & videoScale;
	VideoProjectionMode & videoProjectionMode;
	VideoStereoMode & videoStereoMode;
	float & videoViewYaw;
	float & videoViewPitch;
	float & videoViewRoll;
	float & videoViewFov;
	bool & audioCaptureEnabled;
	AudioMixMode & audioMixMode;
	AudioStereoMode & audioStereoMode;
	AudioCaptureSampleFormat & audioCaptureSampleFormat;
	std::atomic<int> & activeAudioCaptureSampleFormat;
	int & audioCaptureSampleRate;
	int & audioCaptureChannelCount;
	double & audioCaptureBufferSeconds;
	std::string & audioFilterChain;
	std::string & videoFilterChain;
	std::string & audioOutputModuleName;
	std::string & audioOutputDeviceId;
	float & playbackRate;
	int64_t & audioDelayUs;
	int64_t & subtitleDelayUs;
	float & subtitleTextScale;
	MediaPlayerRole & mediaPlayerRole;
	bool & keyInputEnabled;
	bool & mouseInputEnabled;
	std::atomic<int> & channels;
	std::atomic<int> & sampleRate;
	std::atomic<bool> & isAudioReady;
	std::atomic<int> & currentVolume;
	std::atomic<float> & audioOutputVolume;
	std::atomic<bool> & audioOutputMuted;
	std::atomic<uint64_t> & audioCallbackCount;
	std::atomic<uint64_t> & audioCallbackFrameCount;
	std::atomic<uint64_t> & audioCallbackSampleCount;
	std::atomic<uint64_t> & audioCallbackTotalMicros;
	std::atomic<uint64_t> & audioCallbackMaxMicros;
	std::atomic<uint64_t> & audioConversionTotalMicros;
	std::atomic<uint64_t> & audioConversionMaxMicros;
	std::atomic<uint64_t> & audioRingWriteTotalMicros;
	std::atomic<uint64_t> & audioRingWriteMaxMicros;
	std::atomic<uint64_t> & audioRecorderTotalMicros;
	std::atomic<uint64_t> & audioRecorderMaxMicros;
	std::atomic<uint64_t> & audioFirstCallbackSteadyMicros;
	std::atomic<uint64_t> & audioLastCallbackSteadyMicros;
	std::atomic<unsigned> & renderWidth;
	std::atomic<unsigned> & renderHeight;
	std::atomic<unsigned> & videoWidth;
	std::atomic<unsigned> & videoHeight;
	std::atomic<unsigned> & pixelAspectNumerator;
	std::atomic<unsigned> & pixelAspectDenominator;
	std::atomic<float> & displayAspectRatio;
	unsigned & allocatedVideoWidth;
	unsigned & allocatedVideoHeight;
	unsigned & lastBoundViewportWidth;
	unsigned & lastBoundViewportHeight;
	std::atomic<unsigned> & pendingRenderWidth;
	std::atomic<unsigned> & pendingRenderHeight;
	std::atomic<bool> & pendingResize;
	std::shared_ptr<ofAppGLFWWindow> & mainWindow;
	std::shared_ptr<ofAppGLFWWindow> & vlcWindow;
	PlaybackMode & playbackMode;
	bool & shuffleEnabled;
	std::atomic<bool> & pendingEqualizerApplyOnPlay;
	std::atomic<bool> & pendingVideoAdjustApplyOnPlay;
	int64_t & pendingAbLoopStartTimeMs;
	float & pendingAbLoopStartPosition;
	ofTexture & videoTexture;
	ofFbo & exposedTextureFbo;
	ofShader & videoAdjustShader;
	bool & videoAdjustShaderReady;
	GLuint & vlcFramebufferId;
	std::atomic<bool> & closeRequested;
	std::atomic<bool> & shuttingDown;
	std::vector<RendererItemEntry> & discoveredRenderers;
	VideoHdrMetadataInfo & videoHdrMetadata;
	WatchTimeEventType & watchTimeLastEventType;
	WatchTimeCallback & watchTimeCallback;
	libvlc_media_player_time_point_t & lastWatchTimePoint;
	std::vector<float> & smoothedSpectrumLevels;

#ifdef TARGET_WIN32
	struct ID3D11Device * & d3d11Device;
	struct ID3D11DeviceContext * & d3d11DeviceContext;
	struct ID3D10Multithread * & d3d11Multithread;
	struct ID3D11Texture2D * & d3d11RenderTexture;
	struct ID3D11RenderTargetView * & d3d11RenderTargetView;
	int & d3d11RenderDxgiFormat;
#endif

	// VLC Video callbacks
	static bool videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg);
	static void videoSwap(void * data);
	static bool make_current(void * data, bool current);
	static void * get_proc_address(void * data, const char * name);
	static bool videoOutputSetup(void ** data, const libvlc_video_setup_device_cfg_t * cfg, libvlc_video_setup_device_info_t * out);
	static void videoOutputCleanup(void * data);
	static void videoFrameMetadata(void * data, libvlc_video_metadata_type_t type, const void * metadata);

	static void audioPlay(void * data, const void * samples, unsigned int count, int64_t pts);
	static void audioSetVolume(void * data, float volume, bool mute);
	static void audioPause(void * data, int64_t pts);
	static void audioResume(void * data, int64_t pts);
	static void audioFlush(void * data, int64_t pts);
	static void audioDrain(void * data);

	static void vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data);
	void vlcMediaPlayerEvent(const libvlc_event_t * event);
	static void vlcMediaEventStatic(const libvlc_event_t * event, void * data);
	void vlcMediaEvent(const libvlc_event_t * event);
	static void mediaDiscovererMediaListEventStatic(const libvlc_event_t * event, void * data);
	void mediaDiscovererMediaListEvent(const libvlc_event_t * event);
	static void rendererDiscovererEventStatic(const libvlc_event_t * event, void * data);
	void rendererDiscovererEvent(const libvlc_event_t * event);
	static void dialogDisplayLoginStatic(
		void * data,
		libvlc_dialog_id * id,
		const char * title,
		const char * text,
		const char * defaultUsername,
		bool askStore);
	static void dialogDisplayQuestionStatic(
		void * data,
		libvlc_dialog_id * id,
		const char * title,
		const char * text,
		libvlc_dialog_question_type type,
		const char * cancel,
		const char * action1,
		const char * action2);
	static void dialogDisplayProgressStatic(
		void * data,
		libvlc_dialog_id * id,
		const char * title,
		const char * text,
		bool indeterminate,
		float position,
		const char * cancel);
	static void dialogCancelStatic(void * data, libvlc_dialog_id * id);
	static void dialogUpdateProgressStatic(void * data, libvlc_dialog_id * id, float position, const char * text);
	static void dialogErrorStatic(void * data, const char * title, const char * text);
	static void libVlcLogStatic(void * data, int level, const libvlc_log_t * ctx, const char * fmt, va_list args);
	static void watchTimeUpdateStatic(const libvlc_media_player_time_point_t * value, void * data);
	static void watchTimePausedStatic(int64_t system_date_us, void * data);
	static void watchTimeSeekStatic(const libvlc_media_player_time_point_t * value, void * data);

	void detachEvents();
	void stopMediaDiscoveryInternal();
	void stopRendererDiscoveryInternal();
	void clearRendererItems();
	void refreshDiscoveredMediaItems();
	void dismissAllDialogs();
	void upsertDialog(const DialogInfo & dialog);
	void removeDialog(std::uintptr_t token);
	void syncCoreSessionStateFromLegacy();
	void syncLegacyStateFromCoreSession();
	libvlc_instance_t * sessionInstance() const;
	libvlc_media_t * sessionMedia() const;
	libvlc_media_player_t * sessionPlayer() const;
	void releaseVlcResources();
	void stopActiveRecorderSessions();
	void clearRecordingSessionConfig();
	void storeRecordingSessionConfig(const ofxVlc4RecordingSessionConfig & config);
	void setRecordingPresetInternal(const ofxVlc4RecordingPreset & preset);
	void setError(const std::string & message);
	void setStatus(const std::string & message);
	static PlaybackMode playbackModeFromString(const std::string & mode);
	static std::string playbackModeToString(PlaybackMode mode);
	bool isSupportedMediaFile(const ofFile & file, const std::set<std::string> * extensions = nullptr) const;
	void clearPendingActivationRequest();
	bool loadMediaSource(const std::string & source, bool isLocation, const std::vector<std::string> & options, bool parseAsNetwork);
	bool activateDirectMediaImmediate(
		const std::string & source,
		bool isLocation,
		const std::vector<std::string> & options,
		bool shouldPlay,
		bool parseAsNetwork,
		const std::string & label);
	bool requestDirectMediaActivation(
		const std::string & source,
		bool isLocation,
		const std::vector<std::string> & options,
		bool shouldPlay,
		bool parseAsNetwork,
		const std::string & label);
	void prepareAudioRingBuffer();
	void resetAudioBuffer();
	void resetAudioStateInfo();
	void updateAudioStateFromVolumeEvent(int volume);
	void updateAudioStateFromMutedEvent(bool muted);
	void updateAudioStateFromDeviceEvent(const std::string & deviceId);
	void updateAudioStateFromAudioPts(int64_t ptsUs, int64_t systemUs);
	void clearAudioPtsState();
	void resetRendererStateInfo();
	void resetSubtitleStateInfo();
	void resetNavigationStateInfo();
	void refreshRendererStateInfo();
	void refreshPrimaryTrackStateInfo();
	void refreshSubtitleStateInfo();
	void refreshNavigationStateInfo();
	void updateSnapshotStateOnRequest(const std::string & requestedPath);
	void updateSnapshotStateFromEvent(const std::string & savedPath);
	void clearPendingSnapshotState();
	void updateSnapshotFailureState(const std::string & failureReason);
	void updateNativeRecordingStateFromEvent(bool active, const std::string & recordedFilePath);
	void updateNativeRecordingFailureState(const std::string & failureReason);
	void clearWatchTimeState();
	bool isPlaybackLocallyStopped() const;
	bool reapplyCurrentMediaForFilterChainChange(const std::string & label);
	void applySafeLoadedMediaPlayerSettings();
	void applyCurrentMediaPlayerSettings();
	void applyCurrentVolumeToPlayer();
	void applyAudioOutputModule();
	void applyAudioOutputDevice();
	bool applySelectedRenderer();
	void applyAudioStereoMode();
	void applyAudioMixMode();
	void applyPlaybackRate();
	void applyAudioDelay();
	void applySubtitleDelay();
	void applySubtitleTextScale();
	void applyMediaPlayerRole();
	void applyVideoInputHandling();
	void applyVideoTitleDisplay();
	void applyVlcFullscreen();
	void applyWatchTimeObserver();
	void clearVideoHdrMetadata();
	void releaseD3D11Resources();
	void prepareStartupPlaybackState();
	void prepareStartupVideoResources();
	void prepareStartupAudioResources();
	void prepareStartupMediaResources();
	bool shouldApplyEqualizerImmediately() const;
	void applyOrQueueEqualizerSettings();
	void applyPendingEqualizerOnPlay();
	bool shouldApplyVideoAdjustmentsImmediately() const;
	void applyOrQueueVideoAdjustments();
	void applyPendingVideoAdjustmentsOnPlay();
	void setPendingVideoAdjustApplyOnPlay(bool pending);
	void clearPendingVideoAdjustApplyOnPlay();
	bool hasPendingVideoAdjustApplyOnPlay() const;
	bool applyPendingVideoResize();
	void ensureVideoRenderTargetCapacity(unsigned requiredWidth, unsigned requiredHeight);
	void ensureExposedTextureFboCapacity(unsigned requiredWidth, unsigned requiredHeight);
	bool loadMediaAtIndex(int index);
	void activatePlaylistIndex(int index, bool shouldPlay);
	void activatePlaylistIndexImmediate(int index, bool shouldPlay);
	void addToPlaylistInternal(const std::string & path, bool preloadMetadata);
	void clearCurrentMedia(bool clearVideoResources = true);
	void handlePlaybackEnded();
	void applyEqualizerSettings();
	void applyVideoAdjustments();
	void applyVideoDeinterlace();
	void applyVideoAspectRatio();
	void applyVideoCrop();
	void applyVideoScaleAndFit();
	bool applyVideoOutputBackend();
	void updateNativeVideoWindowVisibility();
	bool usesTextureVideoOutput() const;
	void applyTeletextSettings();
	void applyVideoMarquee();
	void applyVideoLogo();
	void applyVideoProjectionMode();
	void applyVideoStereoMode();
	void applyVideoViewpoint(bool absolute = true);
	void applyNativeRecording();
	void refreshExposedTexture();
	void refreshDisplayAspectRatio();
	void refreshPixelAspectRatio();
	void resetCurrentMediaParseState();
	libvlc_media_t * retainCurrentOrLoadedMedia() const;
	void clearMetadataCache();
	void cacheArtworkPathForMediaPath(const std::string & mediaPath, const std::string & artworkPath);
	void clearGeneratedThumbnailInfo();
	void bindVlcRenderTarget();
	void unbindVlcRenderTarget();
	bool drawCurrentFrame(const VideoStateInfo & state, float x, float y, float width, float height);
	void processDeferredPlaybackActions();
	bool ensureWindowCaptureTarget(unsigned requiredWidth, unsigned requiredHeight);
	void registerWindowCaptureListener();
	void unregisterWindowCaptureListener();
	void captureCurrentWindowBackbuffer();
	void onWindowCaptureDraw(ofEventArgs &);
	void clearWindowCaptureState(const std::shared_ptr<ofAppGLFWWindow> & cleanupWindow);
	void updatePendingRecordingMux();
	void processDeferredRecordingMuxCleanup(bool finalPass = false);
	void finalizeRecordingMuxThread();
	void cancelPendingRecordingMux();
	void setRecordingSessionState(ofxVlc4RecordingSessionState state);
	bool armPendingRecordingMux(const std::string & outputPath, const ofxVlc4MuxOptions & options);
	void updateMidiTransport(double nowSeconds);
	std::vector<std::pair<std::string, std::string>> buildMetadataForMedia(libvlc_media_t * sourceMedia) const;
	bool queryVideoTrackGeometry(unsigned & width, unsigned & height, unsigned & sarNum, unsigned & sarDen) const;
	std::vector<MediaTrackInfo> getTrackInfos(libvlc_track_type_t type) const;
	bool selectTrackById(libvlc_track_type_t type, const std::string & trackId);
	int getNextShuffleIndex() const;
	bool isManualStopPending() const;
	void clearRecorderCaptureState(const std::shared_ptr<ofAppGLFWWindow> & cleanupWindow);
	bool ensureRecorderSessionCanStart(bool requireAudioCapture);
	bool stopRecordingSessionInternal(bool allowConfiguredMux);
	bool startNamedAudioCaptureSession(const std::string & audioPath);
	bool startNamedTextureCaptureSession(std::string name, const ofTexture & texture, const ofxVlc4RecordingStartOptions & options);
	static bool muxRecordingFilesInternal(
		const std::string & videoPath,
		const std::string & audioPath,
		const std::string & outputPath,
		const ofxVlc4MuxOptions & options,
		const std::atomic<bool> * cancelRequested,
		std::string * errorOut);

public:
	ofxVlc4();
	virtual ~ofxVlc4();

	static void setLogLevel(ofLogLevel level);
	static ofLogLevel getLogLevel();
	static void logVerbose(const std::string & message);
	static void logError(const std::string & message);
	static void logWarning(const std::string & message);
	static void logNotice(const std::string & message);
	static const char * recordingAudioSourceLabel(ofxVlc4RecordingAudioSource source);
	static const char * recordingSessionStateLabel(ofxVlc4RecordingSessionState state);
	static std::string recordingVideoCodecForPreset(ofxVlc4RecordingVideoCodecPreset preset);
	static ofxVlc4RecordingVideoCodecPreset recordingVideoCodecPresetForCodec(const std::string & codec);
	static const char * recordingVideoCodecPresetLabel(ofxVlc4RecordingVideoCodecPreset preset);
	static bool recordingMuxProfileSupportsVideoCodec(
		ofxVlc4RecordingMuxProfile profile,
		ofxVlc4RecordingVideoCodecPreset preset);
	static std::string recordingMuxProfileCompatibilityMessage(
		ofxVlc4RecordingMuxProfile profile,
		ofxVlc4RecordingVideoCodecPreset preset);
	static std::string recordingMuxContainerForProfile(ofxVlc4RecordingMuxProfile profile);
	static std::string recordingMuxAudioCodecForProfile(ofxVlc4RecordingMuxProfile profile);
	static const char * recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile profile);
	static ofxVlc4MuxOptions recordingMuxOptionsForProfile(
		ofxVlc4RecordingMuxProfile profile,
		int sampleRate,
		int channelCount,
		bool deleteSourceFilesOnSuccess = false,
		uint64_t muxTimeoutMs = 15000);
	static ofxVlc4RecordingSessionConfig textureRecordingSessionConfig(
		std::string outputBasePath,
		const ofTexture & texture,
		const ofxVlc4RecordingPreset & preset,
		int sampleRate,
		int channelCount,
		uint64_t muxTimeoutMs = 15000);
	static ofxVlc4RecordingSessionConfig textureRecordingSessionConfig(
		std::string outputBasePath,
		const ofTexture & texture,
		ofxVlc4RecordingAudioSource audioSource,
		ofxVlc4RecordingMuxProfile muxProfile,
		int sampleRate,
		int channelCount,
		bool deleteSourceFilesOnSuccess = false,
		uint64_t muxTimeoutMs = 15000);
	static ofxVlc4RecordingSessionConfig windowRecordingSessionConfig(
		std::string outputBasePath,
		const ofxVlc4RecordingPreset & preset,
		int sampleRate,
		int channelCount,
		uint64_t muxTimeoutMs = 15000);
	static ofxVlc4RecordingSessionConfig windowRecordingSessionConfig(
		std::string outputBasePath,
		ofxVlc4RecordingAudioSource audioSource,
		ofxVlc4RecordingMuxProfile muxProfile,
		int sampleRate,
		int channelCount,
		bool deleteSourceFilesOnSuccess = false,
		uint64_t muxTimeoutMs = 15000);

	// init() owns the VLC instance/player lifetime for this wrapper and can safely be called again.
	void update();
	void init(int vlc_argc, char const * vlc_argv[]);
	bool startRecordingSession(const ofxVlc4RecordingSessionConfig & config);
	bool startTextureRecordingSession(
		std::string name,
		const ofTexture & texture,
		const ofxVlc4RecordingStartOptions & options = {});
	bool startWindowRecordingSession(
		std::string name,
		const ofxVlc4RecordingStartOptions & options = {});
	void recordVideo(std::string name, const ofTexture & texture);
	void recordAudio(std::string name);
	void recordAudioVideo(std::string name, const ofTexture & texture);
	bool beginWindowRecording(std::string name, bool includeAudioCapture = false);
	void endWindowRecording();
	bool isWindowRecording() const;
	bool stopRecordingSession();
	bool stopRecordingSessionAndMux(const ofxVlc4MuxOptions & options);
	bool stopRecordingSessionAndMux(const std::string & outputPath, const ofxVlc4MuxOptions & options);
	ofxVlc4RecordingSessionState getRecordingSessionState() const;
	ofxVlc4RecorderSettingsInfo getRecorderSettingsInfo() const;
	void setRecordingPreset(const ofxVlc4RecordingPreset & preset);
	ofxVlc4RecordingPreset getRecordingPreset() const;
	void setRecordingAudioSourcePreset(ofxVlc4RecordingAudioSource source);
	ofxVlc4RecordingAudioSource getRecordingAudioSource() const;
	ofxVlc4RecordingAudioSource getRecordingAudioSourcePreset() const;
	void setRecordingOutputSizePreset(int width, int height);
	std::pair<int, int> getRecordingOutputSizePreset() const;
	void setRecordingMuxProfile(ofxVlc4RecordingMuxProfile profile);
	ofxVlc4RecordingMuxProfile getRecordingMuxProfile() const;
	void setRecordingVideoFrameRatePreset(int fps);
	int getRecordingVideoFrameRatePreset() const;
	void setRecordingVideoBitratePreset(int bitrateKbps);
	int getRecordingVideoBitratePreset() const;
	void setRecordingAudioBitratePreset(int bitrateKbps);
	int getRecordingAudioBitratePreset() const;
	void setRecordingDeleteMuxSourceFilesOnSuccess(bool enabled);
	bool getRecordingDeleteMuxSourceFilesOnSuccess() const;
	bool isRecordingMuxPending() const;
	bool isRecordingMuxInProgress() const;
	std::string getLastMuxedRecordingPath() const;
	std::string getLastMuxError() const;
	bool startAudioRecordingForActiveVideo(std::string name);
	std::string getLastAudioRecordingPath() const;
	std::string getLastVideoRecordingPath() const;
	static bool muxRecordingFiles(
		const std::string & videoPath,
		const std::string & audioPath,
		const std::string & outputPath,
		const ofxVlc4MuxOptions & options,
		std::string * errorOut = nullptr);
	static bool muxRecordingFiles(
		const std::string & videoPath,
		const std::string & audioPath,
		const std::string & outputPath,
		const std::string & containerMux,
		const std::string & audioCodec,
		uint64_t timeoutMs = 15000,
		std::string * errorOut = nullptr);
	static bool muxRecordingFilesToMp4(
		const std::string & videoPath,
		const std::string & audioPath,
		const std::string & outputPath,
		uint64_t timeoutMs = 15000,
		std::string * errorOut = nullptr);
	bool isAudioRecording() const;
	bool isVideoRecording() const;
	void setVideoRecordingFrameRate(int fps);
	int getVideoRecordingFrameRate() const;
	void setVideoRecordingBitrateKbps(int bitrateKbps);
	int getVideoRecordingBitrateKbps() const;
	void setVideoRecordingCodec(const std::string & codec);
	void setVideoRecordingCodecPreset(ofxVlc4RecordingVideoCodecPreset preset);
	const std::string & getVideoRecordingCodec() const;
	ofxVlc4RecordingVideoCodecPreset getVideoRecordingCodecPreset() const;
	void setVideoReadbackPolicy(ofxVlc4VideoReadbackPolicy policy);
	ofxVlc4VideoReadbackPolicy getVideoReadbackPolicy() const;
	void setVideoReadbackBufferCount(size_t bufferCount);
	size_t getVideoReadbackBufferCount() const;
	ofxVlc4RecorderPerformanceInfo getRecorderPerformanceInfo() const;

	void draw(float x, float y, float w, float h);
	void draw(float x, float y);
	void updateRecorder();
	void readAudioIntoBuffer(ofSoundBuffer & buffer, float gain);
	void submitRecordedAudioSamples(const float * samples, size_t sampleCount);

	ofTexture & getTexture();

	void play();
	void pause();
	void stop();

	void addToPlaylist(const std::string & path);
	bool openDshowCapture(
		const std::string & videoDevice,
		const std::string & audioDevice = "",
		int width = 0,
		int height = 0,
		float fps = 0.0f);
	bool openScreenCapture(
		int width = 0,
		int height = 0,
		float fps = 30.0f,
		int left = 0,
		int top = 0);
	// Folder imports can be filtered either by the player's default extensions or a per-call override.
	int addPathToPlaylist(const std::string & path);
	int addPathToPlaylist(const std::string & path, std::initializer_list<std::string> extensions);
	void clearPlaylist();
	void playIndex(int index);
	void nextMediaListItem();
	void previousMediaListItem();
	void removeFromPlaylist(int index);
	void movePlaylistItem(int fromIndex, int toIndex);
	void movePlaylistItems(const std::vector<int> & fromIndices, int toIndex);

	const std::vector<std::string> & getPlaylist() const { return playlist; }
	std::vector<PlaylistItemInfo> getPlaylistItems() const;
	PlaylistStateInfo getPlaylistStateInfo() const;
	PlaylistItemInfo getCurrentPlaylistItemInfo() const;
	std::string getPathAtIndex(int index) const;
	std::string getFileNameAtIndex(int index) const;
	std::vector<std::pair<std::string, std::string>> getMetadataAtIndex(int index) const;
	std::string getCurrentPath() const;
	std::string getCurrentFileName() const;
	std::vector<std::pair<std::string, std::string>> getCurrentMetadata() const;
	int getCurrentIndex() const;
	bool isInitialized() const;
	bool hasPlaylist() const;
	// Status/error messages let lightweight UIs surface addon feedback without duplicating validation logic.
	const std::string & getLastStatusMessage() const;
	const std::string & getLastErrorMessage() const;
	void clearLastMessages();
	static std::string vlcHelpModeToOptionString(ofxVlc4VlcHelpMode mode);
	std::string getVlcHelpText(ofxVlc4VlcHelpMode mode = ofxVlc4VlcHelpMode::FullHelp) const;
	void printVlcHelp(ofxVlc4VlcHelpMode mode = ofxVlc4VlcHelpMode::FullHelp) const;
	std::string getVlcModuleHelpText(const std::string & moduleName) const;
	void printVlcModuleHelp(const std::string & moduleName) const;

	// The enum overload is the canonical API; the string overload remains for compatibility.
	void setPlaybackMode(PlaybackMode mode);
	void setPlaybackMode(const std::string & mode);
	PlaybackMode getPlaybackMode() const;
	std::string getPlaybackModeString() const;

	void setShuffleEnabled(bool enabled);
	bool isShuffleEnabled() const;
	void setAudioCaptureEnabled(bool enabled);
	bool isAudioCaptureEnabled() const;
	AudioStateInfo getAudioStateInfo() const;
	VideoStateInfo getVideoStateInfo() const;
	PlaybackStateInfo getPlaybackStateInfo() const;
	MediaReadinessInfo getMediaReadinessInfo() const;
	RendererStateInfo getRendererStateInfo() const;
	SubtitleStateInfo getSubtitleStateInfo() const;
	NavigationStateInfo getNavigationStateInfo() const;
	AudioMixMode getAudioMixMode() const;
	void setAudioMixMode(AudioMixMode mode);
	AudioStereoMode getAudioStereoMode() const;
	void setAudioStereoMode(AudioStereoMode mode);
	AudioCaptureSampleFormat getAudioCaptureSampleFormat() const;
	void setAudioCaptureSampleFormat(AudioCaptureSampleFormat format);
	int getAudioCaptureSampleRate() const;
	void setAudioCaptureSampleRate(int rate);
	int getAudioCaptureChannelCount() const;
	void setAudioCaptureChannelCount(int channelCount);
	double getAudioCaptureBufferSeconds() const;
	void setAudioCaptureBufferSeconds(double seconds);
	std::vector<AudioOutputModuleInfo> getAudioOutputModules() const;
	std::vector<AudioFilterInfo> getAudioFilters() const;
	std::vector<VideoFilterInfo> getVideoFilters() const;
	std::string getAudioFilterChain() const;
	void setAudioFilterChain(const std::string & filterChain);
	std::vector<std::string> getAudioFilterChainEntries() const;
	void setAudioFilters(const std::vector<std::string> & filters);
	bool hasAudioFilter(const std::string & filterName) const;
	bool addAudioFilter(const std::string & filterName);
	bool removeAudioFilter(const std::string & filterName);
	bool toggleAudioFilter(const std::string & filterName);
	std::string getVideoFilterChain() const;
	void setVideoFilterChain(const std::string & filterChain);
	bool canApplyNativeVideoFilters() const;
	std::vector<std::string> getVideoFilterChainEntries() const;
	void setVideoFilters(const std::vector<std::string> & filters);
	bool hasVideoFilter(const std::string & filterName) const;
	bool addVideoFilter(const std::string & filterName);
	bool removeVideoFilter(const std::string & filterName);
	bool toggleVideoFilter(const std::string & filterName);
	std::string getSelectedAudioOutputModuleName() const;
	bool selectAudioOutputModule(const std::string & moduleName);
	std::vector<AudioOutputDeviceInfo> getAudioOutputDevices() const;
	std::string getSelectedAudioOutputDeviceId() const;
	bool selectAudioOutputDevice(const std::string & deviceId);
	std::vector<MediaDiscovererInfo> getMediaDiscoverers(MediaDiscovererCategory category) const;
	std::string getSelectedMediaDiscovererName() const;
	MediaDiscoveryStateInfo getMediaDiscoveryState() const;
	bool startMediaDiscovery(const std::string & discovererName);
	void stopMediaDiscovery();
	bool isMediaDiscoveryActive() const;
	std::vector<DiscoveredMediaItemInfo> getDiscoveredMediaItems() const;
	bool addDiscoveredMediaItemToPlaylist(int index);
	bool playDiscoveredMediaItem(int index);
	int addAllDiscoveredMediaItemsToPlaylist();
	std::vector<RendererDiscovererInfo> getRendererDiscoverers() const;
	std::string getSelectedRendererDiscovererName() const;
	bool startRendererDiscovery(const std::string & discovererName);
	void stopRendererDiscovery();
	bool isRendererDiscoveryActive() const;
	std::vector<RendererInfo> getDiscoveredRenderers() const;
	std::string getSelectedRendererId() const;
	bool selectRenderer(const std::string & rendererId);
	bool clearRenderer();
	float getPlaybackRate() const;
	void setPlaybackRate(float rate);
	int getAudioDelayMs() const;
	void setAudioDelayMs(int delayMs);
	int getSubtitleDelayMs() const;
	void setSubtitleDelayMs(int delayMs);
	float getSubtitleTextScale() const;
	void setSubtitleTextScale(float scale);
	bool isKeyInputEnabled() const;
	void setKeyInputEnabled(bool enabled);
	bool isMouseInputEnabled() const;
	void setMouseInputEnabled(bool enabled);
	bool isVlcFullscreenEnabled() const;
	void setVlcFullscreenEnabled(bool enabled);
	void toggleVlcFullscreen();
	bool isVideoTitleDisplayEnabled() const;
	void setVideoTitleDisplayEnabled(bool enabled);
	OverlayPosition getVideoTitleDisplayPosition() const;
	void setVideoTitleDisplayPosition(OverlayPosition position);
	unsigned getVideoTitleDisplayTimeoutMs() const;
	void setVideoTitleDisplayTimeoutMs(unsigned timeoutMs);
	bool getCursorPosition(int & x, int & y) const;
	std::vector<MediaSlaveInfo> getMediaSlaves() const;
	bool addMediaSlave(MediaSlaveType type, const std::string & uri, unsigned priority = 2);
	bool addSubtitleSlave(const std::string & uri, unsigned priority = 2);
	bool addAudioSlave(const std::string & uri, unsigned priority = 2);
	void clearMediaSlaves();
	std::string takeSnapshot(const std::string & directory = "");
	ThumbnailInfo getLastGeneratedThumbnail() const;
	bool requestThumbnailByTime(
		int timeMs,
		unsigned width = 0,
		unsigned height = 0,
		bool crop = false,
		ThumbnailImageType type = ThumbnailImageType::Png,
		ThumbnailSeekSpeed speed = ThumbnailSeekSpeed::Precise,
		int timeoutMs = 1500);
	bool requestThumbnailByPosition(
		float position,
		unsigned width = 0,
		unsigned height = 0,
		bool crop = false,
		ThumbnailImageType type = ThumbnailImageType::Png,
		ThumbnailSeekSpeed speed = ThumbnailSeekSpeed::Precise,
		int timeoutMs = 1500);
	void cancelThumbnailRequest();
	MediaParseInfo getCurrentMediaParseInfo() const;
	MediaParseStatus getCurrentMediaParseStatus() const;
	MediaParseOptions getMediaParseOptions() const;
	void setMediaParseOptions(const MediaParseOptions & options);
	bool requestCurrentMediaParse();
	void stopCurrentMediaParse();
	MediaStats getMediaStats() const;
	std::vector<ProgramInfo> getPrograms() const;
	int getSelectedProgramId() const;
	bool selectProgramId(int programId);
	bool canPause() const;
	unsigned getVideoOutputCount() const;
	bool hasVideoOutput() const;
	bool isScrambled() const;
	std::vector<DialogInfo> getActiveDialogs() const;
	DialogErrorInfo getLastDialogError() const;
	void clearLastDialogError();
	bool isLibVlcLoggingEnabled() const;
	void setLibVlcLoggingEnabled(bool enabled);
	bool isLibVlcLogFileEnabled() const;
	void setLibVlcLogFileEnabled(bool enabled);
	std::string getLibVlcLogFilePath() const;
	void setLibVlcLogFilePath(const std::string & path);
	std::vector<LibVlcLogEntry> getLibVlcLogEntries() const;
	void clearLibVlcLogEntries();
	MediaPlayerRole getMediaPlayerRole() const;
	void setMediaPlayerRole(MediaPlayerRole role);
	bool isWatchTimeEnabled() const;
	void setWatchTimeEnabled(bool enabled);
	int64_t getWatchTimeMinPeriodUs() const;
	void setWatchTimeMinPeriodUs(int64_t minPeriodUs);
	WatchTimeInfo getWatchTimeInfo() const;
	void setWatchTimeCallback(WatchTimeCallback callback);
	void clearWatchTimeCallback();
	bool hasWatchTimeCallback() const;
	double getPlaybackClockFramesPerSecond() const;
	std::string formatCurrentPlaybackTimecode(double fps = 0.0, bool interpolated = true) const;
	static std::string formatPlaybackTimecode(int64_t timeUs, double fps);
	bool loadMidiFile(const std::string & path, bool noteOffAsZeroVelocity = false);
	void clearMidiTransport();
	bool hasMidiLoaded() const;
	bool isMidiPlaying() const;
	bool isMidiPaused() const;
	bool isMidiFinished() const;
	double getMidiDurationSeconds() const;
	double getMidiPositionSeconds() const;
	double getMidiTempoMultiplier() const;
	void playMidi();
	void pauseMidi();
	void stopMidi();
	void seekMidi(double seconds);
	void setMidiTempoMultiplier(double multiplier);
	MidiAnalysisReport getMidiAnalysisReport() const;
	std::vector<MidiChannelMessage> getMidiMessages() const;
	MidiTransportInfo getMidiTransportInfo() const;
	void setMidiMessageCallback(MidiMessageCallback callback);
	void clearMidiMessageCallback();
	bool hasMidiMessageCallback() const;
	void setMidiSyncSettings(const MidiSyncSettings & settings);
	MidiSyncSettings getMidiSyncSettings() const;
	void setMidiSyncSource(ofxVlc4MidiSyncSource source);
	ofxVlc4MidiSyncSource getMidiSyncSource() const;
	void setMidiSyncToWatchTimeEnabled(bool enabled);
	bool isMidiSyncToWatchTimeEnabled() const;
	bool postDialogLogin(std::uintptr_t token, const std::string & username, const std::string & password, bool store);
	bool postDialogAction(std::uintptr_t token, int action);
	bool dismissDialog(std::uintptr_t token);
	std::string getCurrentMediaMeta(MediaMetaField field) const;
	bool setCurrentMediaMeta(MediaMetaField field, const std::string & value);
	bool saveCurrentMediaMeta();
	std::vector<std::string> getCurrentMediaMetaExtraNames() const;
	std::string getCurrentMediaMetaExtra(const std::string & name) const;
	bool setCurrentMediaMetaExtra(const std::string & name, const std::string & value);
	bool removeCurrentMediaMetaExtra(const std::string & name);
	std::vector<BookmarkInfo> getBookmarksForPath(const std::string & path) const;
	std::vector<BookmarkInfo> getCurrentBookmarks() const;
	bool addBookmarkAtTime(int timeMs, const std::string & label = "");
	bool addCurrentBookmark(const std::string & label = "");
	bool seekToBookmark(const std::string & bookmarkId);
	bool removeBookmark(const std::string & bookmarkId);
	void clearBookmarksForPath(const std::string & path);
	void clearCurrentBookmarks();
	AbLoopInfo getAbLoop() const;
	bool setAbLoopA();
	bool setAbLoopB();
	void clearAbLoop();
	std::vector<TitleInfo> getTitles() const;
	int getCurrentTitleIndex() const;
	bool selectTitleIndex(int index);
	std::vector<ChapterInfo> getChapters(int titleIndex = -1) const;
	int getCurrentChapterIndex() const;
	bool selectChapterIndex(int index);
	void previousChapter();
	void nextChapter();
	void nextFrame();
	void navigate(NavigationMode mode);
	bool executePlayerCommand(PlayerCommand command);
	int getTeletextPage() const;
	void setTeletextPage(int page);
	bool isTeletextTransparencyEnabled() const;
	void setTeletextTransparencyEnabled(bool enabled);
	void sendTeletextKey(TeletextKey key);
	std::vector<MediaTrackInfo> getVideoTracks() const;
	std::vector<MediaTrackInfo> getAudioTracks() const;
	std::vector<MediaTrackInfo> getSubtitleTracks() const;
	bool selectAudioTrackById(const std::string & trackId);
	bool selectSubtitleTrackById(const std::string & trackId);
	bool isEqualizerEnabled() const;
	void setEqualizerEnabled(bool enabled);
	float getEqualizerPreamp() const;
	void setEqualizerPreamp(float preamp);
	int getEqualizerBandCount() const;
	float getEqualizerBandFrequency(int index) const;
	float getEqualizerBandAmp(int index) const;
	void setEqualizerBandAmp(int index, float amp);
	std::vector<float> getEqualizerSpectrumLevels(size_t pointCount = 512) const;
	int getEqualizerPresetCount() const;
	std::vector<std::string> getEqualizerPresetNames() const;
	EqualizerPresetInfo getEqualizerPresetInfo(int index) const;
	std::vector<EqualizerPresetInfo> getEqualizerPresetInfos() const;
	int getCurrentEqualizerPresetIndex() const;
	int findMatchingEqualizerPresetIndex(float toleranceDb = 0.1f) const;
	std::string exportCurrentEqualizerPreset() const;
	bool importEqualizerPreset(const std::string & serializedPreset);
	bool applyEqualizerPreset(int index);
	void resetEqualizer();
	bool isVideoAdjustmentsEnabled() const;
	void setVideoAdjustmentsEnabled(bool enabled);
	VideoAdjustmentEngine getVideoAdjustmentEngine() const;
	VideoAdjustmentEngine getActiveVideoAdjustmentEngine() const;
	void setVideoAdjustmentEngine(VideoAdjustmentEngine engine);
	float getVideoContrast() const;
	void setVideoContrast(float contrast);
	float getVideoBrightness() const;
	void setVideoBrightness(float brightness);
	float getVideoHue() const;
	void setVideoHue(float hue);
	float getVideoSaturation() const;
	void setVideoSaturation(float saturation);
	float getVideoGamma() const;
	void setVideoGamma(float gamma);
	VideoDeinterlaceMode getVideoDeinterlaceMode() const;
	void setVideoDeinterlaceMode(VideoDeinterlaceMode mode);
	VideoAspectRatioMode getVideoAspectRatioMode() const;
	void setVideoAspectRatioMode(VideoAspectRatioMode mode);
	VideoCropMode getVideoCropMode() const;
	void setVideoCropMode(VideoCropMode mode);
	VideoDisplayFitMode getVideoDisplayFitMode() const;
	void setVideoDisplayFitMode(VideoDisplayFitMode mode);
	VideoOutputBackend getVideoOutputBackend() const;
	VideoOutputBackend getActiveVideoOutputBackend() const;
	void setVideoOutputBackend(VideoOutputBackend backend);
	VideoHdrMetadataInfo getVideoHdrMetadata() const;
	float getVideoScale() const;
	void setVideoScale(float scale);
	bool isMarqueeEnabled() const;
	void setMarqueeEnabled(bool enabled);
	std::string getMarqueeText() const;
	void setMarqueeText(const std::string & text);
	OverlayPosition getMarqueePosition() const;
	void setMarqueePosition(OverlayPosition position);
	int getMarqueeOpacity() const;
	void setMarqueeOpacity(int opacity);
	int getMarqueeSize() const;
	void setMarqueeSize(int size);
	int getMarqueeColor() const;
	void setMarqueeColor(int color);
	int getMarqueeRefresh() const;
	void setMarqueeRefresh(int refreshMs);
	int getMarqueeTimeout() const;
	void setMarqueeTimeout(int timeoutMs);
	int getMarqueeX() const;
	void setMarqueeX(int x);
	int getMarqueeY() const;
	void setMarqueeY(int y);
	bool isLogoEnabled() const;
	void setLogoEnabled(bool enabled);
	std::string getLogoPath() const;
	void setLogoPath(const std::string & path);
	OverlayPosition getLogoPosition() const;
	void setLogoPosition(OverlayPosition position);
	int getLogoOpacity() const;
	void setLogoOpacity(int opacity);
	int getLogoX() const;
	void setLogoX(int x);
	int getLogoY() const;
	void setLogoY(int y);
	int getLogoDelay() const;
	void setLogoDelay(int delayMs);
	int getLogoRepeat() const;
	void setLogoRepeat(int repeat);
	bool isNativeRecordingEnabled() const;
	void setNativeRecordingEnabled(bool enabled);
	std::string getNativeRecordDirectory() const;
	void setNativeRecordDirectory(const std::string & directory);
	void resetVideoAdjustments();
	VideoProjectionMode getVideoProjectionMode() const;
	void setVideoProjectionMode(VideoProjectionMode mode);
	VideoStereoMode getVideoStereoMode() const;
	void setVideoStereoMode(VideoStereoMode mode);
	float getVideoYaw() const;
	float getVideoPitch() const;
	float getVideoRoll() const;
	float getVideoFov() const;
	void setVideoViewpoint(float yaw, float pitch, float roll, float fov, bool absolute = true);
	void resetVideoViewpoint();

	void setPosition(float pct);
	float getHeight() const;
	float getWidth() const;
	bool isPlaying();
	bool isStopped() const;
	bool isPlaybackTransitioning() const;
	bool isPlaybackRestartPending() const;
	bool isSeekable() const;
	float getPosition() const;
	int getTime() const;
	void setTime(int ms);
	float getLength() const;
	int getVolume() const;
	void setVolume(int volume);
	bool isMuted() const;
	void toggleMute();
	void close();

	bool audioIsReady() const;
	int getChannelCount() const { return channels.load(std::memory_order_relaxed); }
	int getSampleRate() const { return sampleRate.load(std::memory_order_relaxed); }
	uint64_t getAudioOverrunCount() const;
	uint64_t getAudioUnderrunCount() const;
	size_t peekLatestAudioSamples(float * dst, size_t sampleCount) const;
	ofTexture & getRenderTexture();

};
