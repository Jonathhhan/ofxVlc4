#pragma once

// ---------------------------------------------------------------------------
// Free-standing type definitions used by the ofxVlc4 addon.
//
// Extracted from the monolithic ofxVlc4.h header to keep the main class
// header focused on the class API.  All types defined here are self-contained
// and may be included independently where only the type definitions are needed.
// ---------------------------------------------------------------------------

#include "ofMain.h"

#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Video readback policy (originally in recording/ofxVlc4Recorder.h, moved
// here so that recording-related structs compile without pulling in the full
// recorder header).
// ---------------------------------------------------------------------------

enum class ofxVlc4VideoReadbackPolicy {
	DropLateFrames,
	BlockForFreshestFrame
};

// ---------------------------------------------------------------------------
// Addon version
// ---------------------------------------------------------------------------

struct ofxVlc4AddonVersionInfo {
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::string versionString;
};

// ---------------------------------------------------------------------------
// Mux / recording types
// ---------------------------------------------------------------------------

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
	Mjpg,
	Hap,
	HapAlpha,
	HapQ,
	VP8,
	VP9,
	Theora
};

enum class ofxVlc4RecordingMuxProfile {
	Mp4Aac = 0,
	MkvOpus,
	MkvFlac,
	MkvLpcm,
	OggVorbis,
	MovAac,
	WebmOpus,
	MkvAac
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
	size_t readbackBufferCount = 3;
	ofxVlc4VideoReadbackPolicy readbackPolicy = ofxVlc4VideoReadbackPolicy::DropLateFrames;
	uint64_t muxTimeoutMs = 15000;
	double audioRingBufferSeconds = 4.0;
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

// ---------------------------------------------------------------------------
// MIDI sync
// ---------------------------------------------------------------------------

enum class ofxVlc4MidiSyncSource {
	Internal = 0,
	WatchTime
};

// ---------------------------------------------------------------------------
// Miscellaneous configuration enums
// ---------------------------------------------------------------------------

enum class ofxVlc4VlcHelpMode {
	Help = 0,
	FullHelp
};

enum class ofxVlc4SubtitleTextRenderer {
	Auto = 0,
	Freetype,
	Sapi,
	Dummy,
	None
};

// ---------------------------------------------------------------------------
// Audio visualizer settings
// ---------------------------------------------------------------------------

enum class ofxVlc4AudioVisualizerModule {
	None = 0,
	Visual,
	Goom,
	Glspectrum,
	ProjectM
};

enum class ofxVlc4AudioVisualizerEffect {
	Spectrum = 0,
	Scope,
	Spectrometer,
	VuMeter
};

struct ofxVlc4AudioVisualizerSettings {
	ofxVlc4AudioVisualizerModule module = ofxVlc4AudioVisualizerModule::None;
	ofxVlc4AudioVisualizerEffect visualEffect = ofxVlc4AudioVisualizerEffect::Spectrum;
	int width = 1280;
	int height = 720;
	int goomSpeed = 6;
	std::string projectMPresetPath;
	// libprojectM rendering quality settings (passed to VLC's --projectm-* init args).
	// Set to 0 to omit the option and let VLC use its built-in default.
	int projectMTextureSize = 0; // --projectm-texture-size (power-of-two pixels, e.g. 512, 1024)
	int projectMMeshX = 0;       // --projectm-meshx (horizontal mesh density)
	int projectMMeshY = 0;       // --projectm-meshy (vertical mesh density)
};
