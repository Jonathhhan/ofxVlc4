// Tests for POD default values and enum coverage in ofxVlc4Types.h.
// These types have no dependencies on OF, GLFW, or VLC.

#include <cassert>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Forward declaration stubs for ofTexture (referenced by ofxVlc4Types.h).
// ---------------------------------------------------------------------------

class ofTexture;

#include "../src/core/ofxVlc4Types.h"

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors other test files)
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

static void beginSuite(const char * name) {
	std::printf("\n[%s]\n", name);
}

static void check(bool condition, const char * expr, const char * file, int line) {
	if (condition) {
		++g_passed;
		std::printf("  PASS  %s\n", expr);
	} else {
		++g_failed;
		std::printf("  FAIL  %s  (%s:%d)\n", expr, file, line);
	}
}

#define CHECK(expr)    check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// ofxVlc4AddonVersionInfo defaults
// ---------------------------------------------------------------------------

static void testAddonVersionInfoDefaults() {
	beginSuite("ofxVlc4AddonVersionInfo defaults");

	ofxVlc4AddonVersionInfo info;
	CHECK_EQ(info.major, 0);
	CHECK_EQ(info.minor, 0);
	CHECK_EQ(info.patch, 0);
	CHECK(info.versionString.empty());
}

// ---------------------------------------------------------------------------
// ofxVlc4MuxOptions defaults
// ---------------------------------------------------------------------------

static void testMuxOptionsDefaults() {
	beginSuite("ofxVlc4MuxOptions defaults");

	ofxVlc4MuxOptions opts;
	CHECK_EQ(opts.containerMux, "mp4");
	CHECK_EQ(opts.audioCodec, "mp4a");
	CHECK_EQ(opts.audioChannels, 2);
	CHECK_EQ(opts.audioSampleRate, 44100);
	CHECK_EQ(opts.audioBitrateKbps, 192);
	CHECK_EQ(opts.inputReadyTimeoutMs, 2000u);
	CHECK_EQ(opts.outputReadyTimeoutMs, 1000u);
	CHECK_EQ(opts.muxTimeoutMs, 15000u);
	CHECK_EQ(opts.sourceDeleteTimeoutMs, 5000u);
	CHECK(!opts.deleteSourceFilesOnSuccess);
}

// ---------------------------------------------------------------------------
// ofxVlc4RecordingPreset defaults
// ---------------------------------------------------------------------------

static void testRecordingPresetDefaults() {
	beginSuite("ofxVlc4RecordingPreset defaults");

	ofxVlc4RecordingPreset preset;
	CHECK_EQ(preset.audioSource, ofxVlc4RecordingAudioSource::ExternalSubmitted);
	CHECK_EQ(preset.videoCodecPreset, ofxVlc4RecordingVideoCodecPreset::H264);
	CHECK_EQ(preset.muxProfile, ofxVlc4RecordingMuxProfile::Mp4Aac);
	CHECK_EQ(preset.targetWidth, 0);
	CHECK_EQ(preset.targetHeight, 0);
	CHECK_EQ(preset.videoFrameRate, 30);
	CHECK_EQ(preset.videoBitrateKbps, 8000);
	CHECK_EQ(preset.audioBitrateKbps, 192);
	CHECK(preset.deleteMuxSourceFilesOnSuccess);
	CHECK_EQ(preset.readbackBufferCount, 3u);
	CHECK_EQ(preset.readbackPolicy, ofxVlc4VideoReadbackPolicy::DropLateFrames);
	CHECK_EQ(preset.muxTimeoutMs, 15000u);
	CHECK(preset.audioRingBufferSeconds > 0.0);
}

// ---------------------------------------------------------------------------
// ofxVlc4RecordingStartOptions defaults
// ---------------------------------------------------------------------------

static void testRecordingStartOptionsDefaults() {
	beginSuite("ofxVlc4RecordingStartOptions defaults");

	ofxVlc4RecordingStartOptions opts;
	CHECK(!opts.includeAudioCapture);
	CHECK_EQ(opts.outputWidth, 0);
	CHECK_EQ(opts.outputHeight, 0);
}

// ---------------------------------------------------------------------------
// ofxVlc4RecordingSessionConfig defaults
// ---------------------------------------------------------------------------

static void testRecordingSessionConfigDefaults() {
	beginSuite("ofxVlc4RecordingSessionConfig defaults");

	ofxVlc4RecordingSessionConfig config;
	CHECK(config.outputBasePath.empty());
	CHECK_EQ(config.source, ofxVlc4RecordingSource::Texture);
	CHECK(config.texture == nullptr);
	CHECK_EQ(config.audioSource, ofxVlc4RecordingAudioSource::None);
	CHECK_EQ(config.targetWidth, 0);
	CHECK_EQ(config.targetHeight, 0);
	CHECK(!config.muxOnStop);
	CHECK(config.muxOutputPath.empty());
}

// ---------------------------------------------------------------------------
// ofxVlc4RecorderSettingsInfo defaults
// ---------------------------------------------------------------------------

static void testRecorderSettingsInfoDefaults() {
	beginSuite("ofxVlc4RecorderSettingsInfo defaults");

	ofxVlc4RecorderSettingsInfo info;
	CHECK_EQ(info.activeAudioSource, ofxVlc4RecordingAudioSource::None);
	CHECK_EQ(info.sessionState, ofxVlc4RecordingSessionState::Idle);
	CHECK_EQ(info.readbackPolicy, ofxVlc4VideoReadbackPolicy::DropLateFrames);
	CHECK_EQ(info.readbackBufferCount, 3u);
	CHECK(!info.muxPending);
	CHECK(!info.muxInProgress);
	CHECK(info.lastMuxedPath.empty());
	CHECK(info.lastMuxError.empty());
}

// ---------------------------------------------------------------------------
// ofxVlc4AudioVisualizerSettings defaults
// ---------------------------------------------------------------------------

static void testAudioVisualizerSettingsDefaults() {
	beginSuite("ofxVlc4AudioVisualizerSettings defaults");

	ofxVlc4AudioVisualizerSettings settings;
	CHECK_EQ(settings.module, ofxVlc4AudioVisualizerModule::None);
	CHECK_EQ(settings.visualEffect, ofxVlc4AudioVisualizerEffect::Spectrum);
	CHECK_EQ(settings.width, 1280);
	CHECK_EQ(settings.height, 720);
	CHECK_EQ(settings.goomSpeed, 6);
	CHECK(settings.projectMPresetPath.empty());
	CHECK(settings.projectMTexturePath.empty());
	CHECK_EQ(settings.projectMTextureSize, 0);
	CHECK_EQ(settings.projectMMeshX, 0);
	CHECK_EQ(settings.projectMMeshY, 0);
}

// ---------------------------------------------------------------------------
// Enum value checks — verify that enum members are distinct and castable.
// ---------------------------------------------------------------------------

static void testRecordingSessionStateEnum() {
	beginSuite("ofxVlc4RecordingSessionState enum values");

	CHECK(static_cast<int>(ofxVlc4RecordingSessionState::Idle) == 0);
	CHECK(static_cast<int>(ofxVlc4RecordingSessionState::Capturing) != static_cast<int>(ofxVlc4RecordingSessionState::Idle));
	CHECK(static_cast<int>(ofxVlc4RecordingSessionState::Stopping) != static_cast<int>(ofxVlc4RecordingSessionState::Capturing));
	CHECK(static_cast<int>(ofxVlc4RecordingSessionState::Finalizing) != static_cast<int>(ofxVlc4RecordingSessionState::Stopping));
	CHECK(static_cast<int>(ofxVlc4RecordingSessionState::Muxing) != static_cast<int>(ofxVlc4RecordingSessionState::Finalizing));
	CHECK(static_cast<int>(ofxVlc4RecordingSessionState::Done) != static_cast<int>(ofxVlc4RecordingSessionState::Muxing));
	CHECK(static_cast<int>(ofxVlc4RecordingSessionState::Failed) != static_cast<int>(ofxVlc4RecordingSessionState::Done));
}

static void testVideoCodecPresetEnum() {
	beginSuite("ofxVlc4RecordingVideoCodecPreset enum values");

	CHECK(static_cast<int>(ofxVlc4RecordingVideoCodecPreset::H264) == 0);
	// Verify all presets are distinct (spot check).
	CHECK(static_cast<int>(ofxVlc4RecordingVideoCodecPreset::H265) != static_cast<int>(ofxVlc4RecordingVideoCodecPreset::H264));
	CHECK(static_cast<int>(ofxVlc4RecordingVideoCodecPreset::VP9) != static_cast<int>(ofxVlc4RecordingVideoCodecPreset::VP8));
	CHECK(static_cast<int>(ofxVlc4RecordingVideoCodecPreset::Theora) != static_cast<int>(ofxVlc4RecordingVideoCodecPreset::VP9));
}

static void testMuxProfileEnum() {
	beginSuite("ofxVlc4RecordingMuxProfile enum values");

	CHECK(static_cast<int>(ofxVlc4RecordingMuxProfile::Mp4Aac) == 0);
	CHECK(static_cast<int>(ofxVlc4RecordingMuxProfile::MkvOpus) != static_cast<int>(ofxVlc4RecordingMuxProfile::Mp4Aac));
	CHECK(static_cast<int>(ofxVlc4RecordingMuxProfile::WebmOpus) != static_cast<int>(ofxVlc4RecordingMuxProfile::MkvAac));
}

static void testVideoReadbackPolicyEnum() {
	beginSuite("ofxVlc4VideoReadbackPolicy enum values");

	CHECK(static_cast<int>(ofxVlc4VideoReadbackPolicy::DropLateFrames) != static_cast<int>(ofxVlc4VideoReadbackPolicy::BlockForFreshestFrame));
}

static void testAudioVisualizerModuleEnum() {
	beginSuite("ofxVlc4AudioVisualizerModule enum values");

	CHECK(static_cast<int>(ofxVlc4AudioVisualizerModule::None) == 0);
	CHECK(static_cast<int>(ofxVlc4AudioVisualizerModule::Visual) != static_cast<int>(ofxVlc4AudioVisualizerModule::None));
	CHECK(static_cast<int>(ofxVlc4AudioVisualizerModule::Goom) != static_cast<int>(ofxVlc4AudioVisualizerModule::Visual));
	CHECK(static_cast<int>(ofxVlc4AudioVisualizerModule::ProjectM) != static_cast<int>(ofxVlc4AudioVisualizerModule::Glspectrum));
}

static void testVlcHelpModeEnum() {
	beginSuite("ofxVlc4VlcHelpMode enum values");

	CHECK(static_cast<int>(ofxVlc4VlcHelpMode::Help) == 0);
	CHECK(static_cast<int>(ofxVlc4VlcHelpMode::FullHelp) != static_cast<int>(ofxVlc4VlcHelpMode::Help));
}

static void testSubtitleTextRendererEnum() {
	beginSuite("ofxVlc4SubtitleTextRenderer enum values");

	CHECK(static_cast<int>(ofxVlc4SubtitleTextRenderer::Auto) == 0);
	CHECK(static_cast<int>(ofxVlc4SubtitleTextRenderer::Freetype) != static_cast<int>(ofxVlc4SubtitleTextRenderer::Auto));
	CHECK(static_cast<int>(ofxVlc4SubtitleTextRenderer::None) != static_cast<int>(ofxVlc4SubtitleTextRenderer::Dummy));
}

// ---------------------------------------------------------------------------
// ofxVlc4RecorderPerformanceInfo defaults
// ---------------------------------------------------------------------------

static void testRecorderPerformanceInfoDefaults() {
	beginSuite("ofxVlc4RecorderPerformanceInfo defaults");

	ofxVlc4RecorderPerformanceInfo info;
	CHECK(!info.asyncVideoReadbackEnabled);
	CHECK(!info.asyncVideoReadbackPrimed);
	CHECK_EQ(info.readbackPolicy, ofxVlc4VideoReadbackPolicy::DropLateFrames);
	CHECK_EQ(info.readbackBufferCount, 0u);
	CHECK_EQ(info.captureStartTimeUs, 0u);
	CHECK_EQ(info.submittedFrameCount, 0u);
	CHECK_EQ(info.readyFrameCount, 0u);
	CHECK_EQ(info.synchronousFrameCount, 0u);
	CHECK_EQ(info.fallbackFrameCount, 0u);
	CHECK_EQ(info.droppedFrameCount, 0u);
	CHECK_EQ(info.policyDroppedFrameCount, 0u);
	CHECK_EQ(info.mapFailureCount, 0u);
	CHECK_EQ(info.pendingFrameCount, 0u);
	CHECK_EQ(info.maxPendingFrameCount, 0u);
	CHECK_EQ(info.lastCaptureMicros, 0u);
	CHECK_EQ(info.averageCaptureMicros, 0u);
	CHECK_EQ(info.maxCaptureMicros, 0u);
	CHECK_EQ(info.lastReadbackLatencyMicros, 0u);
	CHECK_EQ(info.averageReadbackLatencyMicros, 0u);
	CHECK_EQ(info.maxReadbackLatencyMicros, 0u);
	CHECK_EQ(info.waitCount, 0u);
	CHECK_EQ(info.averageWaitMicros, 0u);
	CHECK_EQ(info.maxWaitMicros, 0u);
	CHECK_EQ(info.submittedFramesPerSecond, 0.0);
	CHECK_EQ(info.readyFramesPerSecond, 0.0);
}

// ---------------------------------------------------------------------------
// Recording source enum
// ---------------------------------------------------------------------------

static void testRecordingSourceEnum() {
	beginSuite("ofxVlc4RecordingSource enum values");

	CHECK(static_cast<int>(ofxVlc4RecordingSource::Texture) == 0);
	CHECK(static_cast<int>(ofxVlc4RecordingSource::Window) != static_cast<int>(ofxVlc4RecordingSource::Texture));
}

// ---------------------------------------------------------------------------
// Recording audio source enum
// ---------------------------------------------------------------------------

static void testRecordingAudioSourceEnum() {
	beginSuite("ofxVlc4RecordingAudioSource enum values");

	CHECK(static_cast<int>(ofxVlc4RecordingAudioSource::None) == 0);
	CHECK(static_cast<int>(ofxVlc4RecordingAudioSource::VlcCaptured) != static_cast<int>(ofxVlc4RecordingAudioSource::None));
	CHECK(static_cast<int>(ofxVlc4RecordingAudioSource::ExternalSubmitted) != static_cast<int>(ofxVlc4RecordingAudioSource::VlcCaptured));
}

// ---------------------------------------------------------------------------
// MIDI sync source enum
// ---------------------------------------------------------------------------

static void testMidiSyncSourceEnum() {
	beginSuite("ofxVlc4MidiSyncSource enum values");

	CHECK(static_cast<int>(ofxVlc4MidiSyncSource::Internal) == 0);
	CHECK(static_cast<int>(ofxVlc4MidiSyncSource::WatchTime) != static_cast<int>(ofxVlc4MidiSyncSource::Internal));
}

// ---------------------------------------------------------------------------
// Audio visualizer effect enum
// ---------------------------------------------------------------------------

static void testAudioVisualizerEffectEnum() {
	beginSuite("ofxVlc4AudioVisualizerEffect enum values");

	CHECK(static_cast<int>(ofxVlc4AudioVisualizerEffect::Spectrum) == 0);
	CHECK(static_cast<int>(ofxVlc4AudioVisualizerEffect::Scope) != static_cast<int>(ofxVlc4AudioVisualizerEffect::Spectrum));
	CHECK(static_cast<int>(ofxVlc4AudioVisualizerEffect::Spectrometer) != static_cast<int>(ofxVlc4AudioVisualizerEffect::Scope));
	CHECK(static_cast<int>(ofxVlc4AudioVisualizerEffect::VuMeter) != static_cast<int>(ofxVlc4AudioVisualizerEffect::Spectrometer));
}

// ---------------------------------------------------------------------------
// All video codec presets are distinct
// ---------------------------------------------------------------------------

static void testAllVideoCodecPresetsDistinct() {
	beginSuite("ofxVlc4RecordingVideoCodecPreset: all values distinct");

	const int h264 = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::H264);
	const int h265 = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::H265);
	const int mp4v = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::Mp4v);
	const int mjpg = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::Mjpg);
	const int hap = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::Hap);
	const int hapA = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::HapAlpha);
	const int hapQ = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::HapQ);
	const int vp8 = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::VP8);
	const int vp9 = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::VP9);
	const int theora = static_cast<int>(ofxVlc4RecordingVideoCodecPreset::Theora);

	// All 10 values must be distinct.
	int vals[] = { h264, h265, mp4v, mjpg, hap, hapA, hapQ, vp8, vp9, theora };
	for (int i = 0; i < 10; ++i) {
		for (int j = i + 1; j < 10; ++j) {
			CHECK(vals[i] != vals[j]);
		}
	}
}

// ---------------------------------------------------------------------------
// All mux profiles are distinct
// ---------------------------------------------------------------------------

static void testAllMuxProfilesDistinct() {
	beginSuite("ofxVlc4RecordingMuxProfile: all values distinct");

	int vals[] = {
		static_cast<int>(ofxVlc4RecordingMuxProfile::Mp4Aac),
		static_cast<int>(ofxVlc4RecordingMuxProfile::MkvOpus),
		static_cast<int>(ofxVlc4RecordingMuxProfile::MkvFlac),
		static_cast<int>(ofxVlc4RecordingMuxProfile::MkvLpcm),
		static_cast<int>(ofxVlc4RecordingMuxProfile::OggVorbis),
		static_cast<int>(ofxVlc4RecordingMuxProfile::MovAac),
		static_cast<int>(ofxVlc4RecordingMuxProfile::WebmOpus),
		static_cast<int>(ofxVlc4RecordingMuxProfile::MkvAac),
	};
	for (int i = 0; i < 8; ++i) {
		for (int j = i + 1; j < 8; ++j) {
			CHECK(vals[i] != vals[j]);
		}
	}
}

// ---------------------------------------------------------------------------
// All subtitle text renderers are distinct
// ---------------------------------------------------------------------------

static void testAllSubtitleRenderersDistinct() {
	beginSuite("ofxVlc4SubtitleTextRenderer: all values distinct");

	int vals[] = {
		static_cast<int>(ofxVlc4SubtitleTextRenderer::Auto),
		static_cast<int>(ofxVlc4SubtitleTextRenderer::Freetype),
		static_cast<int>(ofxVlc4SubtitleTextRenderer::Sapi),
		static_cast<int>(ofxVlc4SubtitleTextRenderer::Dummy),
		static_cast<int>(ofxVlc4SubtitleTextRenderer::None),
	};
	for (int i = 0; i < 5; ++i) {
		for (int j = i + 1; j < 5; ++j) {
			CHECK(vals[i] != vals[j]);
		}
	}
}

// ---------------------------------------------------------------------------
// RecordingSessionConfig copy semantics
// ---------------------------------------------------------------------------

static void testRecordingSessionConfigCopy() {
	beginSuite("ofxVlc4RecordingSessionConfig: copy semantics");

	ofxVlc4RecordingSessionConfig original;
	original.outputBasePath = "/tmp/output";
	original.source = ofxVlc4RecordingSource::Window;
	original.audioSource = ofxVlc4RecordingAudioSource::VlcCaptured;
	original.targetWidth = 1920;
	original.targetHeight = 1080;
	original.muxOnStop = true;
	original.muxOutputPath = "/tmp/final.mp4";

	ofxVlc4RecordingSessionConfig copy = original;
	CHECK_EQ(copy.outputBasePath, "/tmp/output");
	CHECK_EQ(copy.source, ofxVlc4RecordingSource::Window);
	CHECK_EQ(copy.audioSource, ofxVlc4RecordingAudioSource::VlcCaptured);
	CHECK_EQ(copy.targetWidth, 1920);
	CHECK_EQ(copy.targetHeight, 1080);
	CHECK(copy.muxOnStop);
	CHECK_EQ(copy.muxOutputPath, "/tmp/final.mp4");

	// Modify copy → original unchanged.
	copy.outputBasePath = "/tmp/other";
	CHECK_EQ(original.outputBasePath, "/tmp/output");
}

// ---------------------------------------------------------------------------
// RecorderSettingsInfo copy semantics
// ---------------------------------------------------------------------------

static void testRecorderSettingsInfoCopy() {
	beginSuite("ofxVlc4RecorderSettingsInfo: copy semantics");

	ofxVlc4RecorderSettingsInfo original;
	original.sessionState = ofxVlc4RecordingSessionState::Capturing;
	original.muxPending = true;
	original.lastMuxedPath = "/tmp/muxed.mp4";

	ofxVlc4RecorderSettingsInfo copy = original;
	CHECK_EQ(copy.sessionState, ofxVlc4RecordingSessionState::Capturing);
	CHECK(copy.muxPending);
	CHECK_EQ(copy.lastMuxedPath, "/tmp/muxed.mp4");

	copy.sessionState = ofxVlc4RecordingSessionState::Done;
	CHECK_EQ(original.sessionState, ofxVlc4RecordingSessionState::Capturing);
}

// ---------------------------------------------------------------------------
// AudioVisualizerSettings copy semantics
// ---------------------------------------------------------------------------

static void testAudioVisualizerSettingsCopy() {
	beginSuite("ofxVlc4AudioVisualizerSettings: copy semantics");

	ofxVlc4AudioVisualizerSettings original;
	original.module = ofxVlc4AudioVisualizerModule::ProjectM;
	original.visualEffect = ofxVlc4AudioVisualizerEffect::VuMeter;
	original.width = 800;
	original.height = 600;
	original.goomSpeed = 10;
	original.projectMPresetPath = "/presets";
	original.projectMTexturePath = "/textures";
	original.projectMTextureSize = 1024;

	ofxVlc4AudioVisualizerSettings copy = original;
	CHECK_EQ(copy.module, ofxVlc4AudioVisualizerModule::ProjectM);
	CHECK_EQ(copy.visualEffect, ofxVlc4AudioVisualizerEffect::VuMeter);
	CHECK_EQ(copy.width, 800);
	CHECK_EQ(copy.height, 600);
	CHECK_EQ(copy.goomSpeed, 10);
	CHECK_EQ(copy.projectMPresetPath, "/presets");
	CHECK_EQ(copy.projectMTexturePath, "/textures");
	CHECK_EQ(copy.projectMTextureSize, 1024);

	copy.module = ofxVlc4AudioVisualizerModule::Goom;
	CHECK_EQ(original.module, ofxVlc4AudioVisualizerModule::ProjectM);
}

// ---------------------------------------------------------------------------
// AudioVisualizerSettings equality (all fields are init-relevant)
// ---------------------------------------------------------------------------

static void testAudioVisualizerSettingsEquality() {
	beginSuite("ofxVlc4AudioVisualizerSettings: equality");

	ofxVlc4AudioVisualizerSettings a;
	a.module = ofxVlc4AudioVisualizerModule::Visual;

	// Changing module: should differ.
	ofxVlc4AudioVisualizerSettings c = a;
	c.module = ofxVlc4AudioVisualizerModule::Goom;
	CHECK(a != c);

	// Changing width: should differ.
	ofxVlc4AudioVisualizerSettings d = a;
	d.width = 640;
	CHECK(a != d);

	// Changing projectM texture path: should differ.
	ofxVlc4AudioVisualizerSettings d2 = a;
	d2.projectMTexturePath = "/tmp/projectm/textures";
	CHECK(a != d2);

	// Identical settings: should be equal.
	ofxVlc4AudioVisualizerSettings e = a;
	CHECK(a == e);
}

// ---------------------------------------------------------------------------
// Copy / assign semantics of POD types
// ---------------------------------------------------------------------------

static void testMuxOptionsCopy() {
	beginSuite("ofxVlc4MuxOptions: copy semantics");

	ofxVlc4MuxOptions original;
	original.containerMux = "mkv";
	original.audioCodec = "opus";
	original.audioChannels = 6;
	original.audioBitrateKbps = 320;
	original.deleteSourceFilesOnSuccess = true;

	ofxVlc4MuxOptions copy = original;
	CHECK_EQ(copy.containerMux, "mkv");
	CHECK_EQ(copy.audioCodec, "opus");
	CHECK_EQ(copy.audioChannels, 6);
	CHECK_EQ(copy.audioBitrateKbps, 320);
	CHECK(copy.deleteSourceFilesOnSuccess);

	// Modify copy → original unchanged.
	copy.containerMux = "mp4";
	CHECK_EQ(original.containerMux, "mkv");
}

static void testRecordingPresetCopy() {
	beginSuite("ofxVlc4RecordingPreset: copy semantics");

	ofxVlc4RecordingPreset original;
	original.videoCodecPreset = ofxVlc4RecordingVideoCodecPreset::VP9;
	original.muxProfile = ofxVlc4RecordingMuxProfile::WebmOpus;
	original.videoFrameRate = 60;

	ofxVlc4RecordingPreset copy = original;
	CHECK_EQ(copy.videoCodecPreset, ofxVlc4RecordingVideoCodecPreset::VP9);
	CHECK_EQ(copy.muxProfile, ofxVlc4RecordingMuxProfile::WebmOpus);
	CHECK_EQ(copy.videoFrameRate, 60);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testAddonVersionInfoDefaults();
	testMuxOptionsDefaults();
	testRecordingPresetDefaults();
	testRecordingStartOptionsDefaults();
	testRecordingSessionConfigDefaults();
	testRecorderSettingsInfoDefaults();
	testAudioVisualizerSettingsDefaults();
	testRecordingSessionStateEnum();
	testVideoCodecPresetEnum();
	testMuxProfileEnum();
	testVideoReadbackPolicyEnum();
	testAudioVisualizerModuleEnum();
	testVlcHelpModeEnum();
	testSubtitleTextRendererEnum();
	testRecorderPerformanceInfoDefaults();
	testRecordingSourceEnum();
	testRecordingAudioSourceEnum();
	testMidiSyncSourceEnum();
	testAudioVisualizerEffectEnum();
	testAllVideoCodecPresetsDistinct();
	testAllMuxProfilesDistinct();
	testAllSubtitleRenderersDistinct();
	testRecordingSessionConfigCopy();
	testRecorderSettingsInfoCopy();
	testAudioVisualizerSettingsCopy();
	testAudioVisualizerSettingsEquality();
	testMuxOptionsCopy();
	testRecordingPresetCopy();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
