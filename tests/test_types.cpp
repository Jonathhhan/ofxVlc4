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
	testMuxOptionsCopy();
	testRecordingPresetCopy();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
