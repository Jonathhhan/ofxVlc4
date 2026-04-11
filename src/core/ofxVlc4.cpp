#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "audio/ofxVlc4Audio.h"
#include "media/MediaLibrary.h"
#include "media/ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "video/ofxVlc4Video.h"
#include "support/ofxVlc4GlOps.h"
#include "support/ofxVlc4MuxHelpers.h"
#include "support/ofxVlc4Utils.h"
#include "VlcCoreSession.h"
#include "VlcEventRouter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef TARGET_WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using ofxVlc4Utils::clearAllocatedFbo;
using ofxVlc4Utils::readTextFileIfPresent;
using ofxVlc4MuxHelpers::removeRecordingFile;
using ofxVlc4MuxHelpers::tryRemoveRecordingFileOnce;
using ofxVlc4MuxHelpers::buildDefaultMuxOutputPath;
using RecordingMuxRuntimeState = ofxVlc4::Impl::RecordingMuxRuntimeState;

namespace {

#ifdef TARGET_OSX
void configureMacLibVlcEnvironment() {
	const std::string bundledPluginPath = ofToDataPath("libvlc/macos/plugins", true);
	if (ofDirectory::doesDirectoryExist(bundledPluginPath, true)) {
		setenv("VLC_PLUGIN_PATH", bundledPluginPath.c_str(), 1);
	}
	const std::string bundledDataPath = ofToDataPath("libvlc/macos", true);
	if (ofDirectory::doesDirectoryExist(bundledDataPath + "/lua", true)) {
		setenv("VLC_DATA_PATH", bundledDataPath.c_str(), 1);
	}
}
#endif

#ifdef TARGET_WIN32
void configureWindowsLibVlcEnvironment() {
	wchar_t exePathBuf[MAX_PATH];
	const DWORD len = GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
	if (len == 0 || len == MAX_PATH) return;

	const std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();
	if (std::error_code ec; std::filesystem::is_directory(exeDir / L"lua", ec)) {
		const std::string dirStr = exeDir.string();
		_putenv_s("VLC_DATA_PATH", dirStr.c_str());
	}
}
#endif
constexpr const char * kLogChannel = "ofxVlc4";
std::atomic<int> gLogLevel { static_cast<int>(OF_LOG_NOTICE) };
constexpr int kOfxVlc4AddonVersionMajor = 1;
constexpr int kOfxVlc4AddonVersionMinor = 0;
constexpr int kOfxVlc4AddonVersionPatch = 4;
constexpr const char * kOfxVlc4AddonVersionString = "1.0.4";

bool shouldLog(ofLogLevel level) {
	const ofLogLevel configuredLevel = static_cast<ofLogLevel>(gLogLevel.load());
	return configuredLevel != OF_LOG_SILENT && level >= configuredLevel;
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

const char * subtitleTextRendererOptionName(ofxVlc4SubtitleTextRenderer renderer) {
	switch (renderer) {
	case ofxVlc4SubtitleTextRenderer::Freetype:
		return "freetype";
	case ofxVlc4SubtitleTextRenderer::Sapi:
		return "sapi";
	case ofxVlc4SubtitleTextRenderer::Dummy:
		return "tdummy";
	case ofxVlc4SubtitleTextRenderer::None:
		return "none";
	case ofxVlc4SubtitleTextRenderer::Auto:
	default:
		return "";
	}
}

void appendPrefixedInitArg(std::vector<std::string> & initArgs, const char * prefix, const std::string & value) {
	if (!prefix || value.empty()) {
		return;
	}
	initArgs.emplace_back(std::string(prefix) + value);
}

const char * audioVisualizerModuleOptionName(ofxVlc4AudioVisualizerModule module) {
	switch (module) {
	case ofxVlc4AudioVisualizerModule::Visual:
		return "visual";
	case ofxVlc4AudioVisualizerModule::Goom:
		return "goom";
	case ofxVlc4AudioVisualizerModule::Glspectrum:
		return "glspectrum";
	case ofxVlc4AudioVisualizerModule::ProjectM:
		return "projectm";
	case ofxVlc4AudioVisualizerModule::None:
	default:
		return "";
	}
}

const char * audioVisualizerEffectOptionName(ofxVlc4AudioVisualizerEffect effect) {
	switch (effect) {
	case ofxVlc4AudioVisualizerEffect::Scope:
		return "scope";
	case ofxVlc4AudioVisualizerEffect::Spectrometer:
		return "spectrometer";
	case ofxVlc4AudioVisualizerEffect::VuMeter:
		return "vuMeter";
	case ofxVlc4AudioVisualizerEffect::Spectrum:
	default:
		return "spectrum";
	}
}

void appendAudioVisualizerInitArgs(
	std::vector<std::string> & initArgs,
	const ofxVlc4AudioVisualizerSettings & settings) {
	const char * moduleName = audioVisualizerModuleOptionName(settings.module);
	if (moduleName[0] == '\0') {
		return;
	}

	const int width = std::max(64, settings.width);
	const int height = std::max(64, settings.height);
	// VLC visualization modules are configured through the audio-visual hook.
	// Pushing them again through audio-filter can create an unreliable filter
	// chain on Windows, which showed up as gray/blank Goom and projectM output.
	initArgs.emplace_back(std::string("--audio-visual=") + moduleName);

	switch (settings.module) {
	case ofxVlc4AudioVisualizerModule::Visual:
		initArgs.emplace_back(std::string("--effect-list=") + audioVisualizerEffectOptionName(settings.visualEffect));
		initArgs.emplace_back(std::string("--effect-width=") + ofToString(width));
		initArgs.emplace_back(std::string("--effect-height=") + ofToString(height));
		break;
	case ofxVlc4AudioVisualizerModule::Goom:
		initArgs.emplace_back(std::string("--goom-width=") + ofToString(width));
		initArgs.emplace_back(std::string("--goom-height=") + ofToString(height));
		initArgs.emplace_back(std::string("--goom-speed=") + ofToString(ofClamp(settings.goomSpeed, 1, 10)));
		break;
	case ofxVlc4AudioVisualizerModule::Glspectrum:
		initArgs.emplace_back(std::string("--glspectrum-width=") + ofToString(width));
		initArgs.emplace_back(std::string("--glspectrum-height=") + ofToString(height));
		break;
	case ofxVlc4AudioVisualizerModule::ProjectM:
		initArgs.emplace_back(std::string("--projectm-width=") + ofToString(width));
		initArgs.emplace_back(std::string("--projectm-height=") + ofToString(height));
		appendPrefixedInitArg(initArgs, "--projectm-preset-path=", settings.projectMPresetPath);
		initArgs.emplace_back("--projectm-menu-font=Arial");
		initArgs.emplace_back("--projectm-title-font=Arial");
		if (settings.projectMTextureSize > 0) {
			initArgs.emplace_back(std::string("--projectm-texture-size=") + ofToString(settings.projectMTextureSize));
		}
		if (settings.projectMMeshX > 0) {
			initArgs.emplace_back(std::string("--projectm-meshx=") + ofToString(settings.projectMMeshX));
		}
		if (settings.projectMMeshY > 0) {
			initArgs.emplace_back(std::string("--projectm-meshy=") + ofToString(settings.projectMMeshY));
		}
		break;
	case ofxVlc4AudioVisualizerModule::None:
	default:
		break;
	}
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


libvlc_instance_t * ofxVlc4::sessionInstance() const {
	return m_impl->subsystemRuntime.coreSession ? m_impl->subsystemRuntime.coreSession->instance() : nullptr;
}

libvlc_media_t * ofxVlc4::sessionMedia() const {
	return m_impl->subsystemRuntime.coreSession ? m_impl->subsystemRuntime.coreSession->media() : nullptr;
}

libvlc_media_player_t * ofxVlc4::sessionPlayer() const {
	return m_impl->subsystemRuntime.coreSession ? m_impl->subsystemRuntime.coreSession->player() : nullptr;
}


ofxVlc4::ofxVlc4()
	: m_impl(std::make_unique<Impl>())
	{
	ofGLFWWindowSettings settings;
	m_impl->videoResourceRuntime.mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
	if (m_impl->videoResourceRuntime.mainWindow) {
		settings = m_impl->videoResourceRuntime.mainWindow->getSettings();
	}
	settings.setSize(1, 1);
	settings.setPosition(glm::vec2(-32000, -32000));
	settings.visible = false;
	settings.decorated = false;
	settings.resizable = false;
	settings.shareContextWith = m_impl->videoResourceRuntime.mainWindow;
	if (!m_impl->videoResourceRuntime.mainWindow) {
		ofLogWarning(kLogChannel) << "No main window available at construction; VLC render context will not share resources with the main context.";
	}
	m_impl->videoResourceRuntime.vlcWindow = std::make_shared<ofAppGLFWWindow>();
	m_impl->videoResourceRuntime.vlcWindow->setup(settings);
	// Vsync is intentionally disabled: VLC renders exclusively to an FBO and
	// never swaps the window's default framebuffer, so enabling vsync here
	// would only add unnecessary throttling on some drivers without providing
	// any benefit.
	m_impl->videoResourceRuntime.vlcWindow->setVerticalSync(false);
	m_impl->videoResourceRuntime.vlcWindow->setWindowTitle("ofxVlc4 Native Video");

	m_impl->videoResourceRuntime.videoTexture.allocate(1, 1, GL_RGBA);
	m_impl->videoResourceRuntime.videoTexture.getTextureData().bFlipTexture = true;
	m_impl->videoResourceRuntime.exposedTextureFbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(m_impl->videoResourceRuntime.exposedTextureFbo);
	m_impl->videoGeometryRuntime.allocatedVideoWidth = 1;
	m_impl->videoGeometryRuntime.allocatedVideoHeight = 1;
	m_impl->subsystemRuntime.audioComponent = std::make_unique<AudioComponent>(*this);
	m_impl->subsystemRuntime.videoComponent = std::make_unique<VideoComponent>(*this);
	m_impl->subsystemRuntime.mediaComponent = std::make_unique<MediaComponent>(*this);
	m_impl->subsystemRuntime.playbackController = std::make_unique<PlaybackController>(*this);
	m_impl->subsystemRuntime.mediaLibraryController = std::make_unique<MediaLibrary>(*this);
	m_impl->subsystemRuntime.coreSession = std::make_unique<VlcCoreSession>();
	m_impl->subsystemRuntime.eventRouter = std::make_unique<VlcEventRouter>(*this);
	m_impl->effectsRuntime.equalizerBandAmps.assign(libvlc_audio_equalizer_get_band_count(), 0.0f);
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

const char * ofxVlc4::getLibVlcVersion() {
	return libvlc_get_version();
}

const char * ofxVlc4::getLibVlcCompiler() {
	return libvlc_get_compiler();
}

const char * ofxVlc4::getLibVlcChangeset() {
	return libvlc_get_changeset();
}

int ofxVlc4::getLibVlcAbiVersion() {
	return libvlc_abi_version();
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
	case ofxVlc4RecordingVideoCodecPreset::H265:
		return "X265";
	case ofxVlc4RecordingVideoCodecPreset::Mp4v:
		return "MP4V";
	case ofxVlc4RecordingVideoCodecPreset::Mjpg:
		return "MJPG";
	case ofxVlc4RecordingVideoCodecPreset::Hap:
		return "Hap1";
	case ofxVlc4RecordingVideoCodecPreset::HapAlpha:
		return "Hap5";
	case ofxVlc4RecordingVideoCodecPreset::HapQ:
		return "HapY";
	case ofxVlc4RecordingVideoCodecPreset::VP8:
		return "VP80";
	case ofxVlc4RecordingVideoCodecPreset::VP9:
		return "VP90";
	case ofxVlc4RecordingVideoCodecPreset::Theora:
		return "theo";
	}
	return "H264";
}

ofxVlc4RecordingVideoCodecPreset ofxVlc4::recordingVideoCodecPresetForCodec(const std::string & codec) {
	const std::string normalizedCodec = ofToUpper(ofTrim(codec));
	if (normalizedCodec == "X265" || normalizedCodec == "H265" || normalizedCodec == "HEVC") {
		return ofxVlc4RecordingVideoCodecPreset::H265;
	}
	if (normalizedCodec == "MP4V") {
		return ofxVlc4RecordingVideoCodecPreset::Mp4v;
	}
	if (normalizedCodec == "MJPG") {
		return ofxVlc4RecordingVideoCodecPreset::Mjpg;
	}
	if (normalizedCodec == "HAP1") {
		return ofxVlc4RecordingVideoCodecPreset::Hap;
	}
	if (normalizedCodec == "HAP5") {
		return ofxVlc4RecordingVideoCodecPreset::HapAlpha;
	}
	if (normalizedCodec == "HAPY") {
		return ofxVlc4RecordingVideoCodecPreset::HapQ;
	}
	if (normalizedCodec == "VP80" || normalizedCodec == "VP8") {
		return ofxVlc4RecordingVideoCodecPreset::VP8;
	}
	if (normalizedCodec == "VP90" || normalizedCodec == "VP9") {
		return ofxVlc4RecordingVideoCodecPreset::VP9;
	}
	if (normalizedCodec == "THEO" || normalizedCodec == "THEORA") {
		return ofxVlc4RecordingVideoCodecPreset::Theora;
	}
	return ofxVlc4RecordingVideoCodecPreset::H264;
}

const char * ofxVlc4::recordingVideoCodecPresetLabel(ofxVlc4RecordingVideoCodecPreset preset) {
	switch (preset) {
	case ofxVlc4RecordingVideoCodecPreset::H264:
		return "H264";
	case ofxVlc4RecordingVideoCodecPreset::H265:
		return "H265 / HEVC";
	case ofxVlc4RecordingVideoCodecPreset::Mp4v:
		return "MP4V";
	case ofxVlc4RecordingVideoCodecPreset::Mjpg:
		return "MJPG";
	case ofxVlc4RecordingVideoCodecPreset::Hap:
		return "HAP";
	case ofxVlc4RecordingVideoCodecPreset::HapAlpha:
		return "HAP Alpha";
	case ofxVlc4RecordingVideoCodecPreset::HapQ:
		return "HAP Q";
	case ofxVlc4RecordingVideoCodecPreset::VP8:
		return "VP8";
	case ofxVlc4RecordingVideoCodecPreset::VP9:
		return "VP9";
	case ofxVlc4RecordingVideoCodecPreset::Theora:
		return "Theora";
	}
	return "H264";
}

bool ofxVlc4::recordingMuxProfileSupportsVideoCodec(
	ofxVlc4RecordingMuxProfile profile,
	ofxVlc4RecordingVideoCodecPreset preset) {
	const bool isHap =
		preset == ofxVlc4RecordingVideoCodecPreset::Hap ||
		preset == ofxVlc4RecordingVideoCodecPreset::HapAlpha ||
		preset == ofxVlc4RecordingVideoCodecPreset::HapQ;
	if (isHap) {
		return profile == ofxVlc4RecordingMuxProfile::MovAac;
	}
	const bool isOpenCodec =
		preset == ofxVlc4RecordingVideoCodecPreset::VP8 ||
		preset == ofxVlc4RecordingVideoCodecPreset::VP9 ||
		preset == ofxVlc4RecordingVideoCodecPreset::Theora;
	if (isOpenCodec) {
		return profile == ofxVlc4RecordingMuxProfile::MkvOpus ||
			profile == ofxVlc4RecordingMuxProfile::MkvFlac ||
			profile == ofxVlc4RecordingMuxProfile::MkvLpcm ||
			profile == ofxVlc4RecordingMuxProfile::MkvAac ||
			profile == ofxVlc4RecordingMuxProfile::OggVorbis ||
			profile == ofxVlc4RecordingMuxProfile::WebmOpus;
	}
	if (preset == ofxVlc4RecordingVideoCodecPreset::H265) {
		return profile == ofxVlc4RecordingMuxProfile::MkvOpus ||
			profile == ofxVlc4RecordingMuxProfile::MkvFlac ||
			profile == ofxVlc4RecordingMuxProfile::MkvLpcm ||
			profile == ofxVlc4RecordingMuxProfile::MkvAac;
	}
	return true;
}

std::string ofxVlc4::recordingMuxProfileCompatibilityMessage(
	ofxVlc4RecordingMuxProfile profile,
	ofxVlc4RecordingVideoCodecPreset preset) {
	if (recordingMuxProfileSupportsVideoCodec(profile, preset)) {
		return {};
	}

	const bool isHap =
		preset == ofxVlc4RecordingVideoCodecPreset::Hap ||
		preset == ofxVlc4RecordingVideoCodecPreset::HapAlpha ||
		preset == ofxVlc4RecordingVideoCodecPreset::HapQ;
	if (isHap) {
		return "HAP recording requires the MOV / AAC mux profile.";
	}
	const bool isOpenCodec =
		preset == ofxVlc4RecordingVideoCodecPreset::VP8 ||
		preset == ofxVlc4RecordingVideoCodecPreset::VP9 ||
		preset == ofxVlc4RecordingVideoCodecPreset::Theora;
	if (isOpenCodec) {
		return "VP8, VP9, and Theora recording require an MKV, OGG, or WebM mux profile.";
	}
	if (preset == ofxVlc4RecordingVideoCodecPreset::H265) {
		return "H265 / HEVC recording currently requires an MKV mux profile.";
	}
	return "Selected recording codec and mux profile are not compatible.";
}

std::string ofxVlc4::recordingMuxContainerForProfile(ofxVlc4RecordingMuxProfile profile) {
	switch (profile) {
	case ofxVlc4RecordingMuxProfile::Mp4Aac:
		return "mp4";
	case ofxVlc4RecordingMuxProfile::MkvOpus:
		return "mkv";
	case ofxVlc4RecordingMuxProfile::MkvFlac:
		return "mkv";
	case ofxVlc4RecordingMuxProfile::MkvLpcm:
		return "mkv";
	case ofxVlc4RecordingMuxProfile::OggVorbis:
		return "ogg";
	case ofxVlc4RecordingMuxProfile::MovAac:
		return "mov";
	case ofxVlc4RecordingMuxProfile::WebmOpus:
		return "webm";
	case ofxVlc4RecordingMuxProfile::MkvAac:
		return "mkv";
	}
	return "mp4";
}

std::string ofxVlc4::recordingMuxAudioCodecForProfile(ofxVlc4RecordingMuxProfile profile) {
	switch (profile) {
	case ofxVlc4RecordingMuxProfile::Mp4Aac:
		return "mp4a";
	case ofxVlc4RecordingMuxProfile::MkvOpus:
		return "opus";
	case ofxVlc4RecordingMuxProfile::MkvFlac:
		return "flac";
	case ofxVlc4RecordingMuxProfile::MkvLpcm:
		return "lpcm";
	case ofxVlc4RecordingMuxProfile::OggVorbis:
		return "vorb";
	case ofxVlc4RecordingMuxProfile::MovAac:
		return "mp4a";
	case ofxVlc4RecordingMuxProfile::WebmOpus:
		return "opus";
	case ofxVlc4RecordingMuxProfile::MkvAac:
		return "mp4a";
	}
	return "mp4a";
}

const char * ofxVlc4::recordingMuxProfileLabel(ofxVlc4RecordingMuxProfile profile) {
	switch (profile) {
	case ofxVlc4RecordingMuxProfile::Mp4Aac:
		return "MP4 / AAC";
	case ofxVlc4RecordingMuxProfile::MkvOpus:
		return "MKV / OPUS";
	case ofxVlc4RecordingMuxProfile::MkvFlac:
		return "MKV / FLAC";
	case ofxVlc4RecordingMuxProfile::MkvLpcm:
		return "MKV / LPCM";
	case ofxVlc4RecordingMuxProfile::OggVorbis:
		return "OGG / VORBIS";
	case ofxVlc4RecordingMuxProfile::MovAac:
		return "MOV / AAC";
	case ofxVlc4RecordingMuxProfile::WebmOpus:
		return "WEBM / OPUS";
	case ofxVlc4RecordingMuxProfile::MkvAac:
		return "MKV / AAC";
	}
	return "MP4 / AAC";
}

bool ofxVlc4::recordingVideoCodecUsesMovContainer(ofxVlc4RecordingVideoCodecPreset preset) {
	return preset == ofxVlc4RecordingVideoCodecPreset::Hap ||
		preset == ofxVlc4RecordingVideoCodecPreset::HapAlpha ||
		preset == ofxVlc4RecordingVideoCodecPreset::HapQ;
}

ofxVlc4MuxOptions ofxVlc4::recordingMuxOptionsForProfile(
	ofxVlc4RecordingMuxProfile profile,
	int sampleRate,
	int channelCount,
	bool deleteSourceFilesOnSuccess,
	uint64_t muxTimeoutMs,
	int audioBitrateKbps) {
	ofxVlc4MuxOptions options;
	options.containerMux = recordingMuxContainerForProfile(profile);
	options.audioCodec = recordingMuxAudioCodecForProfile(profile);
	options.audioSampleRate = std::max(8000, sampleRate);
	options.audioChannels = std::max(1, channelCount);
	options.deleteSourceFilesOnSuccess = deleteSourceFilesOnSuccess;
	options.muxTimeoutMs = muxTimeoutMs;
	const bool isLossless = options.audioCodec == "flac" || options.audioCodec == "lpcm";
	if (isLossless) {
		options.audioBitrateKbps = 0;
	} else if (audioBitrateKbps > 0) {
		options.audioBitrateKbps = audioBitrateKbps;
	} else {
		options.audioBitrateKbps =
			(options.audioCodec == "opus") ? 160 : 192;
	}
	return options;
}

ofxVlc4RecordingSessionConfig ofxVlc4::textureRecordingSessionConfig(
	std::string outputBasePath,
	const ofTexture & texture,
	const ofxVlc4RecordingPreset & preset,
	int sampleRate,
	int channelCount) {
	ofxVlc4RecordingSessionConfig config = textureRecordingSessionConfig(
		std::move(outputBasePath),
		texture,
		preset.audioSource,
		preset.muxProfile,
		sampleRate,
		channelCount,
		preset.deleteMuxSourceFilesOnSuccess,
		preset.muxTimeoutMs,
		preset.audioBitrateKbps);
	config.targetWidth = std::max(0, preset.targetWidth);
	config.targetHeight = std::max(0, preset.targetHeight);
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
	uint64_t muxTimeoutMs,
	int audioBitrateKbps) {
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
		muxTimeoutMs,
		audioBitrateKbps);
	return config;
}

ofxVlc4RecordingSessionConfig ofxVlc4::windowRecordingSessionConfig(
	std::string outputBasePath,
	const ofxVlc4RecordingPreset & preset,
	int sampleRate,
	int channelCount) {
	ofxVlc4RecordingSessionConfig config = windowRecordingSessionConfig(
		std::move(outputBasePath),
		preset.audioSource,
		preset.muxProfile,
		sampleRate,
		channelCount,
		preset.deleteMuxSourceFilesOnSuccess,
		preset.muxTimeoutMs,
		preset.audioBitrateKbps);
	config.targetWidth = std::max(0, preset.targetWidth);
	config.targetHeight = std::max(0, preset.targetHeight);
	return config;
}

ofxVlc4RecordingSessionConfig ofxVlc4::windowRecordingSessionConfig(
	std::string outputBasePath,
	ofxVlc4RecordingAudioSource audioSource,
	ofxVlc4RecordingMuxProfile muxProfile,
	int sampleRate,
	int channelCount,
	bool deleteSourceFilesOnSuccess,
	uint64_t muxTimeoutMs,
	int audioBitrateKbps) {
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
		muxTimeoutMs,
		audioBitrateKbps);
	return config;
}

void ofxVlc4::update() {
	finalizeRecordingMuxThread();
	processDeferredRecordingMuxCleanup();
	updateMidiTransport(ofGetElapsedTimef());
	if (m_impl->windowCaptureRuntime.active && !m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
		m_impl->windowCaptureRuntime.active = false;
		unregisterWindowCaptureListener();
	}
	if (m_impl->recordingObjectRuntime.recorder.needsCaptureUpdate()) {
		updateRecorder();
	}
	updatePendingRecordingMux();
	processDeferredPlaybackActions();
}

bool ofxVlc4::ensureWindowCaptureTarget(unsigned requiredWidth, unsigned requiredHeight) {
	if (requiredWidth == 0 || requiredHeight == 0) {
		return false;
	}

	if (m_impl->windowCaptureRuntime.captureFbo.isAllocated() &&
		m_impl->windowCaptureRuntime.captureWidth == requiredWidth &&
		m_impl->windowCaptureRuntime.captureHeight == requiredHeight) {
		return true;
	}

	if (glfwGetCurrentContext() == nullptr) {
		setError("Window recording requires a current OpenGL context.");
		return false;
	}

	clearAllocatedFbo(m_impl->windowCaptureRuntime.captureFbo);
	m_impl->windowCaptureRuntime.captureFbo.allocate(requiredWidth, requiredHeight, GL_RGB);
	if (!m_impl->windowCaptureRuntime.captureFbo.isAllocated()) {
		m_impl->windowCaptureRuntime.capturePixels.clear();
		m_impl->windowCaptureRuntime.sourceTexture.clear();
		setError("Failed to allocate window capture buffer.");
		return false;
	}

	m_impl->windowCaptureRuntime.captureWidth = requiredWidth;
	m_impl->windowCaptureRuntime.captureHeight = requiredHeight;
	m_impl->windowCaptureRuntime.captureFbo.getTexture().setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
	return true;
}

void ofxVlc4::registerWindowCaptureListener() {
	if (!m_impl->windowCaptureRuntime.listeners.empty()) {
		return;
	}

	m_impl->windowCaptureRuntime.listeners.push(
		ofEvents().draw.newListener(this, &ofxVlc4::onWindowCaptureDraw, OF_EVENT_ORDER_AFTER_APP + 1));
}

void ofxVlc4::unregisterWindowCaptureListener() {
	m_impl->windowCaptureRuntime.listeners.unsubscribeAll();
}

void ofxVlc4::captureCurrentWindowBackbuffer() {
	if (!m_impl->windowCaptureRuntime.active || !m_impl->windowCaptureRuntime.captureFbo.isAllocated()) {
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

	if (!m_impl->windowCaptureRuntime.capturePixels.isAllocated() ||
		m_impl->windowCaptureRuntime.sourceWidth != currentWidth ||
		m_impl->windowCaptureRuntime.sourceHeight != currentHeight) {
		m_impl->windowCaptureRuntime.capturePixels.allocate(
			currentWidth,
			currentHeight,
			OF_PIXELS_RGB);
		m_impl->windowCaptureRuntime.sourceTexture.clear();
		m_impl->windowCaptureRuntime.sourceTexture.allocate(currentWidth, currentHeight, GL_RGB);
		m_impl->windowCaptureRuntime.sourceTexture.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
		m_impl->windowCaptureRuntime.sourceWidth = currentWidth;
		m_impl->windowCaptureRuntime.sourceHeight = currentHeight;
	}
	if (!m_impl->windowCaptureRuntime.capturePixels.isAllocated()) {
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
		m_impl->windowCaptureRuntime.capturePixels.getData());
	m_impl->windowCaptureRuntime.capturePixels.mirror(true, false);
	m_impl->windowCaptureRuntime.sourceTexture.loadData(m_impl->windowCaptureRuntime.capturePixels);
	m_impl->windowCaptureRuntime.captureFbo.begin();
	ofClear(0, 0, 0, 255);
	ofSetColor(255);
	m_impl->windowCaptureRuntime.sourceTexture.draw(
		0.0f,
		0.0f,
		static_cast<float>(m_impl->windowCaptureRuntime.captureWidth),
		static_cast<float>(m_impl->windowCaptureRuntime.captureHeight));
	m_impl->windowCaptureRuntime.captureFbo.end();
}

void ofxVlc4::onWindowCaptureDraw(ofEventArgs &) {
	if (!m_impl->windowCaptureRuntime.active || m_impl->lifecycleRuntime.shuttingDown.load(std::memory_order_acquire)) {
		return;
	}
	if (!m_impl->recordingObjectRuntime.recorder.isVideoCaptureActive()) {
		m_impl->windowCaptureRuntime.active = false;
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

std::string ofxVlc4::getDiagnosticsReport() const {
	const auto boolStr = [](bool v) -> const char * { return v ? "yes" : "no"; };
	const auto naStr = [](const std::string & s) -> std::string {
		return s.empty() ? "(none)" : s;
	};

	std::ostringstream out;
	const ofxVlc4AddonVersionInfo version = getAddonVersionInfo();
	out << "=== ofxVlc4 Diagnostics Report ===\n";
	out << "Addon version: " << version.versionString << "\n";

	// --- Playback state ---
	const PlaybackStateInfo pb = getPlaybackStateInfo();
	out << "\n-- Playback --\n";
	out << "playing:     " << boolStr(pb.playing)
	    << "  stopped: " << boolStr(pb.stopped)
	    << "  transitioning: " << boolStr(pb.transitioning) << "\n";
	out << "seekable:    " << boolStr(pb.seekable)
	    << "  rate: " << pb.rate
	    << "  bufferCache: " << static_cast<int>(pb.bufferCache) << "%"
	    << "  corked: " << boolStr(pb.corked) << "\n";
	out << "position:    " << pb.position
	    << "  time: " << pb.timeMs << " ms"
	    << "  length: " << static_cast<int>(pb.lengthMs) << " ms\n";
	out << "media:       attached=" << boolStr(pb.mediaAttached)
	    << "  prepared=" << boolStr(pb.startupPrepared)
	    << "  geom=" << boolStr(pb.geometryKnown)
	    << "  frame=" << boolStr(pb.hasReceivedVideoFrame) << "\n";
	out << "path:        " << naStr(getCurrentPath()) << "\n";

	// --- Media readiness ---
	const MediaReadinessInfo rd = getMediaReadinessInfo();
	out << "\n-- Media Readiness --\n";
	out << "video tracks:    " << rd.videoTrackCount
	    << " (" << boolStr(rd.videoTracksReady) << ")"
	    << "  audio: " << rd.audioTrackCount
	    << " (" << boolStr(rd.audioTracksReady) << ")"
	    << "  subtitle: " << rd.subtitleTrackCount
	    << " (" << boolStr(rd.subtitleTracksReady) << ")\n";
	out << "navigation: " << boolStr(rd.navigationReady)
	    << "  titles: " << rd.titleCount
	    << "  chapters: " << rd.chapterCount
	    << "  programs: " << rd.programCount << "\n";
	out << "parse status: ";
	switch (rd.parseStatus) {
	case MediaParseStatus::None:      out << "none";      break;
	case MediaParseStatus::Pending:   out << "pending";   break;
	case MediaParseStatus::Skipped:   out << "skipped";   break;
	case MediaParseStatus::Failed:    out << "failed";    break;
	case MediaParseStatus::Timeout:   out << "timeout";   break;
	case MediaParseStatus::Cancelled: out << "cancelled"; break;
	case MediaParseStatus::Done:      out << "done";      break;
	}
	out << "  active: " << boolStr(rd.parseActive)
	    << "  requested: " << boolStr(rd.parseRequested) << "\n";

	// --- Video state ---
	const VideoStateInfo vs = getVideoStateInfo();
	out << "\n-- Video --\n";
	out << "source:   " << vs.sourceWidth << "x" << vs.sourceHeight
	    << "  render: " << vs.renderWidth << "x" << vs.renderHeight << "\n";
	out << "aspect:   " << vs.displayAspectRatio
	    << "  sar: " << vs.pixelAspectNumerator << "/" << vs.pixelAspectDenominator << "\n";
	out << "loaded:   " << boolStr(vs.loaded)
	    << "  hasVout: " << boolStr(vs.hasVideoOutput)
	    << " (" << vs.videoOutputCount << ")\n";
	out << "tracks:   " << vs.trackCount
	    << "  adjust: " << boolStr(vs.videoAdjustmentsEnabled)
	    << "  canPause: " << boolStr(canPause()) << "\n";

	// --- Audio state ---
	const AudioStateInfo as = getAudioStateInfo();
	out << "\n-- Audio --\n";
	out << "ready:    " << boolStr(as.ready)
	    << "  tracks: " << as.trackCount
	    << "  volume: " << as.volume
	    << "  muted: " << boolStr(as.muted) << "\n";
	out << "device:   " << naStr(as.deviceId) << "\n";
	out << "delay:    " << as.audioDelayMs << " ms\n";
	if (as.callbackPerformance.available) {
		const auto & cp = as.callbackPerformance;
		out << "callbacks: count=" << cp.callbackCount
		    << "  rate=" << cp.callbackRateHz << " Hz"
		    << "  avg=" << cp.averageCallbackMicros << " us"
		    << "  max=" << cp.maxCallbackMicros << " us\n";
		out << "           frames=" << cp.frameCount
		    << "  samples=" << cp.sampleCount << "\n";
	}

	// --- Media stats ---
	const MediaStats ms = getMediaStats();
	out << "\n-- Media Stats --\n";
	if (ms.available) {
		out << "input:   " << ms.readBytes << " bytes"
		    << "  bitrate: " << ms.inputBitrate << "\n";
		out << "demux:   " << ms.demuxReadBytes << " bytes"
		    << "  bitrate: " << ms.demuxBitrate
		    << "  corrupt: " << ms.demuxCorrupted
		    << "  disc: " << ms.demuxDiscontinuity << "\n";
		const uint64_t droppedFrames = ms.latePictures + ms.lostPictures;
		out << "video:   decoded=" << ms.decodedVideo
		    << "  displayed=" << ms.displayedPictures
		    << "  dropped=" << droppedFrames
		    << " (late=" << ms.latePictures
		    << " lost=" << ms.lostPictures << ")\n";
		out << "audio:   decoded=" << ms.decodedAudio
		    << "  played=" << ms.playedAudioBuffers
		    << "  lost=" << ms.lostAudioBuffers << "\n";
	} else {
		out << "(unavailable)\n";
	}

	// --- Status / error messages ---
	out << "\n-- Messages --\n";
	const std::string & status = getLastStatusMessage();
	const std::string & error  = getLastErrorMessage();
	out << "status: " << naStr(status) << "\n";
	out << "error:  " << naStr(error) << "\n";

	// --- libVLC log (last 10 entries) ---
	const std::vector<LibVlcLogEntry> logEntries = getLibVlcLogEntries();
	out << "\n-- libVLC Log (last " << std::min<size_t>(logEntries.size(), 10) << " of " << logEntries.size() << ") --\n";
	if (logEntries.empty()) {
		out << "(empty - enable libVLC logging to capture entries)\n";
	} else {
		const int firstIndex = std::max(0, static_cast<int>(logEntries.size()) - 10);
		for (int i = static_cast<int>(logEntries.size()) - 1; i >= firstIndex; --i) {
			const auto & entry = logEntries[static_cast<size_t>(i)];
			const char * levelStr = "notice";
			switch (entry.level) {
			case LIBVLC_DEBUG:   levelStr = "debug";   break;
			case LIBVLC_WARNING: levelStr = "warning"; break;
			case LIBVLC_ERROR:   levelStr = "error";   break;
			default:             levelStr = "notice";  break;
			}
			out << "[" << levelStr << "] " << entry.message;
			if (!entry.module.empty()) {
				out << "  (" << entry.module << ")";
			}
			out << "\n";
		}
	}

	out << "\n===================================\n";
	return out.str();
}

void ofxVlc4::init(int vlc_argc, char const * vlc_argv[]) {
	// Re-init starts from a clean VLC state so partial previous setup cannot leak across sessions.
	const bool hasExistingVlcSession =
		m_impl->subsystemRuntime.coreSession &&
		(m_impl->subsystemRuntime.coreSession->instance() != nullptr ||
			m_impl->subsystemRuntime.coreSession->player() != nullptr ||
			m_impl->subsystemRuntime.coreSession->media() != nullptr);
	if (hasExistingVlcSession) {
		// Do NOT set shuttingDown before releaseVlcResources().  VLC's OpenGL
		// display module calls our make_current(true) callback from inside
		// libvlc_media_player_release() to obtain a GL context for its own
		// resource cleanup (shader/VAO deletion).  If shuttingDown is true at
		// that point the callback returns false and VLC proceeds to delete GL
		// objects without a context, which crashes.  releaseVlcResources()
		// sets the flag internally after the player is fully released.
		logNotice("Reinit: tearing down previous VLC session.");
		releaseVlcResources();
	}
	m_impl->videoResourceRuntime.mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
	m_impl->lifecycleRuntime.closeRequested.store(false);
	m_impl->lifecycleRuntime.shuttingDown.store(false, std::memory_order_release);
	m_impl->subsystemRuntime.playbackController->resetTransportState();
	m_impl->subsystemRuntime.audioComponent->clearPendingEqualizerApplyOnPlay();
	m_impl->subsystemRuntime.videoComponent->clearPendingVideoAdjustApplyOnPlay();
	resetCurrentMediaParseState();
	clearWindowCaptureState(m_impl->videoResourceRuntime.mainWindow);
	if (m_impl->recordingObjectRuntime.recorder.hasCleanupState()) {
		clearRecorderCaptureState(m_impl->videoResourceRuntime.mainWindow);
	}
	clearLastMessages();
	clearLastDialogError();

#ifdef TARGET_OSX
	configureMacLibVlcEnvironment();
#endif

#ifdef TARGET_WIN32
	configureWindowsLibVlcEnvironment();
#endif

	std::vector<std::string> initArgs;
	initArgs.reserve(static_cast<size_t>(std::max(0, vlc_argc)) + m_impl->playerConfigRuntime.extraInitArgs.size() + 8);

#ifdef TARGET_WIN32
	switch (m_impl->videoPresentationRuntime.preferredDecoderDevice) {
	case PreferredDecoderDevice::D3D11:
		initArgs.emplace_back("--dec-dev=d3d11_filters");
		break;
	case PreferredDecoderDevice::DXVA2:
		initArgs.emplace_back("--dec-dev=d3d9_filters");
		if (m_impl->videoPresentationRuntime.videoOutputBackend == VideoOutputBackend::Texture) {
			initArgs.emplace_back("--glinterop=glinterop_dxva2");
		}
		break;
	case PreferredDecoderDevice::Nvdec:
		initArgs.emplace_back("--dec-dev=nvdec");
		if (m_impl->videoPresentationRuntime.videoOutputBackend == VideoOutputBackend::Texture) {
			initArgs.emplace_back("--glinterop=glinterop_nvdec");
		}
		break;
	case PreferredDecoderDevice::None:
		initArgs.emplace_back("--dec-dev=none");
		break;
	case PreferredDecoderDevice::Any:
	default:
		break;
	}
#endif

	const char * textRendererName = subtitleTextRendererOptionName(m_impl->playerConfigRuntime.subtitleTextRenderer);
	if (textRendererName[0] != '\0') {
		initArgs.emplace_back(std::string("--text-renderer=") + textRendererName);
	}
	appendPrefixedInitArg(initArgs, "--freetype-font=", m_impl->playerConfigRuntime.subtitleFontFamily);
	initArgs.emplace_back(std::string("--freetype-color=") + ofToString(ofClamp(m_impl->playerConfigRuntime.subtitleTextColor, 0, 16777215)));
	initArgs.emplace_back(std::string("--freetype-opacity=") + ofToString(ofClamp(m_impl->playerConfigRuntime.subtitleTextOpacity, 0, 255)));
	initArgs.emplace_back(m_impl->playerConfigRuntime.subtitleBold ? "--freetype-bold" : "--no-freetype-bold");
	appendAudioVisualizerInitArgs(initArgs, m_impl->playerConfigRuntime.audioVisualizerSettings);

	for (int i = 0; i < vlc_argc; ++i) {
		if (vlc_argv != nullptr && vlc_argv[i] != nullptr) {
			initArgs.emplace_back(vlc_argv[i]);
		}
	}

	for (const std::string & argument : m_impl->playerConfigRuntime.extraInitArgs) {
		if (!argument.empty()) {
			initArgs.push_back(argument);
		}
	}

	std::vector<const char *> initArgPointers;
	initArgPointers.reserve(initArgs.size());
	for (const std::string & argument : initArgs) {
		initArgPointers.push_back(argument.c_str());
	}
	const int runtimeAbi = libvlc_abi_version();
	if (runtimeAbi != LIBVLC_ABI_VERSION_INT) {
		logWarning("libVLC ABI mismatch: compiled against "
			+ ofToString(LIBVLC_ABI_VERSION_INT)
			+ " but runtime is "
			+ ofToString(runtimeAbi)
			+ ". Unexpected behavior may occur.");
	}

	m_impl->subsystemRuntime.coreSession->setInstance(libvlc_new(static_cast<int>(initArgPointers.size()), initArgPointers.data()));
	if (!m_impl->subsystemRuntime.coreSession->instance()) {
		const char * error = libvlc_errmsg();
		setError(error ? error : "libvlc_new failed");
		return;
	}

	libvlc_set_user_agent(m_impl->subsystemRuntime.coreSession->instance(), "ofxVlc4", "ofxVlc4/libVLC");
	libvlc_set_app_id(m_impl->subsystemRuntime.coreSession->instance(), "org.openframeworks.ofxVlc4", "", "");

	m_impl->subsystemRuntime.mediaComponent->applyLibVlcLogging();

	m_impl->subsystemRuntime.coreSession->setPlayer(libvlc_media_player_new(m_impl->subsystemRuntime.coreSession->instance()));
	if (!m_impl->subsystemRuntime.coreSession->player()) {
		const char * error = libvlc_errmsg();
		setError(error ? error : "libvlc_media_player_new failed");
		releaseVlcResources();
		return;
	}

	resetAudioStateInfo();
	resetRendererStateInfo();
	resetSubtitleStateInfo();
	resetNavigationStateInfo();

	updateNativeVideoWindowVisibility();
	if (!applyVideoOutputBackend()) {
		releaseVlcResources();
		return;
	}

	if (m_impl->playerConfigRuntime.audioCaptureEnabled) {
		libvlc_audio_set_callbacks(m_impl->subsystemRuntime.coreSession->player(), audioPlay, audioPause, audioResume, audioFlush, audioDrain, this);
		libvlc_audio_set_volume_callback(m_impl->subsystemRuntime.coreSession->player(), audioSetVolume);
		libvlc_audio_set_format(
			m_impl->subsystemRuntime.coreSession->player(),
			m_impl->subsystemRuntime.audioComponent->getStartupAudioCaptureSampleFormatCode(),
			m_impl->subsystemRuntime.audioComponent->getStartupAudioCaptureSampleRate(),
			m_impl->subsystemRuntime.audioComponent->getStartupAudioCaptureChannelCount());
	} else {
		m_impl->audioRuntime.ready.store(false);
		resetAudioBuffer();
	}

	m_impl->subsystemRuntime.coreSession->setPlayerEvents(libvlc_media_player_event_manager(m_impl->subsystemRuntime.coreSession->player()));
	if (m_impl->subsystemRuntime.coreSession->playerEvents() && m_impl->subsystemRuntime.eventRouter) {
		m_impl->subsystemRuntime.coreSession->attachPlayerEvents(m_impl->subsystemRuntime.eventRouter.get(), VlcEventRouter::vlcMediaPlayerEventStatic);
	}

	const libvlc_dialog_cbs dialogCallbacks = {
		VlcEventRouter::dialogDisplayLoginStatic,
		VlcEventRouter::dialogDisplayQuestionStatic,
		VlcEventRouter::dialogDisplayProgressStatic,
		VlcEventRouter::dialogCancelStatic,
		VlcEventRouter::dialogUpdateProgressStatic
	};
	libvlc_dialog_set_callbacks(m_impl->subsystemRuntime.coreSession->instance(), &dialogCallbacks, m_impl->subsystemRuntime.eventRouter.get());
	libvlc_dialog_set_error_callback(m_impl->subsystemRuntime.coreSession->instance(), VlcEventRouter::dialogErrorStatic, m_impl->subsystemRuntime.eventRouter.get());

	if (!m_impl->rendererDiscoveryRuntime.discovererName.empty()) {
		startRendererDiscovery(m_impl->rendererDiscoveryRuntime.discovererName);
	}

	applyWatchTimeObserver();
	applyCurrentMediaPlayerSettings();
	applyEqualizerSettings();
	logNotice("Player initialized.");
}

std::vector<std::string> ofxVlc4::getExtraInitArgs() const {
	return m_impl->playerConfigRuntime.extraInitArgs;
}

void ofxVlc4::setExtraInitArgs(const std::vector<std::string> & args) {
	m_impl->playerConfigRuntime.extraInitArgs.clear();
	for (const std::string & arg : args) {
		if (!arg.empty()) {
			m_impl->playerConfigRuntime.extraInitArgs.push_back(arg);
		}
	}
	setStatus("Extra init args updated for the next init.");
}

void ofxVlc4::addExtraInitArg(const std::string & arg) {
	if (arg.empty()) {
		return;
	}
	m_impl->playerConfigRuntime.extraInitArgs.push_back(arg);
	setStatus("Extra init args updated for the next init.");
}

void ofxVlc4::clearExtraInitArgs() {
	if (m_impl->playerConfigRuntime.extraInitArgs.empty()) {
		return;
	}
	m_impl->playerConfigRuntime.extraInitArgs.clear();
	setStatus("Extra init args cleared for the next init.");
}

ofxVlc4AudioVisualizerSettings ofxVlc4::getAudioVisualizerSettings() const {
	return m_impl->playerConfigRuntime.audioVisualizerSettings;
}

void ofxVlc4::setAudioVisualizerSettings(const ofxVlc4AudioVisualizerSettings & settings) {
	m_impl->playerConfigRuntime.audioVisualizerSettings.module = settings.module;
	m_impl->playerConfigRuntime.audioVisualizerSettings.visualEffect = settings.visualEffect;
	m_impl->playerConfigRuntime.audioVisualizerSettings.width = std::max(64, settings.width);
	m_impl->playerConfigRuntime.audioVisualizerSettings.height = std::max(64, settings.height);
	m_impl->playerConfigRuntime.audioVisualizerSettings.goomSpeed = ofClamp(settings.goomSpeed, 1, 10);
	m_impl->playerConfigRuntime.audioVisualizerSettings.projectMPresetPath = settings.projectMPresetPath;
	m_impl->playerConfigRuntime.audioVisualizerSettings.projectMTextureSize = std::max(0, settings.projectMTextureSize);
	m_impl->playerConfigRuntime.audioVisualizerSettings.projectMMeshX = std::max(0, settings.projectMMeshX);
	m_impl->playerConfigRuntime.audioVisualizerSettings.projectMMeshY = std::max(0, settings.projectMMeshY);
	if (sessionPlayer()) {
		if (reinitAndReapplyCurrentMedia("Audio visualizer")) {
			return;
		}
		logNotice("Audio visualizer settings updated. Reinit to apply.");
		setStatus("Audio visualizer settings updated. Reinit to apply.");
		return;
	}
	setStatus("Audio visualizer settings updated for the next init.");
}

ofxVlc4SubtitleTextRenderer ofxVlc4::getSubtitleTextRenderer() const {
	return m_impl->playerConfigRuntime.subtitleTextRenderer;
}

void ofxVlc4::setSubtitleTextRenderer(ofxVlc4SubtitleTextRenderer renderer) {
	if (m_impl->playerConfigRuntime.subtitleTextRenderer == renderer) {
		return;
	}
	m_impl->playerConfigRuntime.subtitleTextRenderer = renderer;
	setStatus("Subtitle text renderer updated for the next init.");
}

std::string ofxVlc4::getSubtitleFontFamily() const {
	return m_impl->playerConfigRuntime.subtitleFontFamily;
}

void ofxVlc4::setSubtitleFontFamily(const std::string & family) {
	if (m_impl->playerConfigRuntime.subtitleFontFamily == family) {
		return;
	}
	m_impl->playerConfigRuntime.subtitleFontFamily = family;
	setStatus("Subtitle font updated for the next init.");
}

int ofxVlc4::getSubtitleTextColor() const {
	return m_impl->playerConfigRuntime.subtitleTextColor;
}

void ofxVlc4::setSubtitleTextColor(int color) {
	const int clampedColor = ofClamp(color, 0, 16777215);
	if (m_impl->playerConfigRuntime.subtitleTextColor == clampedColor) {
		return;
	}
	m_impl->playerConfigRuntime.subtitleTextColor = clampedColor;
	setStatus("Subtitle text color updated for the next init.");
}

int ofxVlc4::getSubtitleTextOpacity() const {
	return m_impl->playerConfigRuntime.subtitleTextOpacity;
}

void ofxVlc4::setSubtitleTextOpacity(int opacity) {
	const int clampedOpacity = ofClamp(opacity, 0, 255);
	if (m_impl->playerConfigRuntime.subtitleTextOpacity == clampedOpacity) {
		return;
	}
	m_impl->playerConfigRuntime.subtitleTextOpacity = clampedOpacity;
	setStatus("Subtitle text opacity updated for the next init.");
}

bool ofxVlc4::isSubtitleBold() const {
	return m_impl->playerConfigRuntime.subtitleBold;
}

void ofxVlc4::setSubtitleBold(bool enabled) {
	if (m_impl->playerConfigRuntime.subtitleBold == enabled) {
		return;
	}
	m_impl->playerConfigRuntime.subtitleBold = enabled;
	setStatus("Subtitle bold styling updated for the next init.");
}

void ofxVlc4::setError(const std::string & message) {
	m_impl->diagnosticsRuntime.lastErrorMessage = message;
	m_impl->diagnosticsRuntime.lastStatusMessage.clear();
	logError(message);
}

void ofxVlc4::setStatus(const std::string & message) {
	m_impl->diagnosticsRuntime.lastStatusMessage = message;
	m_impl->diagnosticsRuntime.lastErrorMessage.clear();
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
	m_impl->recordingMuxRuntime.sessionState.store(static_cast<int>(state), std::memory_order_release);
}

ofxVlc4RecordingSessionState ofxVlc4::getRecordingSessionState() const {
	return static_cast<ofxVlc4RecordingSessionState>(
		m_impl->recordingMuxRuntime.sessionState.load(std::memory_order_acquire));
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
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	return m_impl->recordingSessionRuntime.hasConfig
		? m_impl->recordingSessionRuntime.config.audioSource
		: ofxVlc4RecordingAudioSource::None;
}

void ofxVlc4::setRecordingPresetInternal(const ofxVlc4RecordingPreset & preset) {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	m_impl->recordingSessionRuntime.preset = preset;
}

void ofxVlc4::setRecordingPreset(const ofxVlc4RecordingPreset & preset) {
	setRecordingPresetInternal(preset);
	setVideoRecordingFrameRate(preset.videoFrameRate);
	setVideoRecordingBitrateKbps(preset.videoBitrateKbps);
	setVideoRecordingCodecPreset(preset.videoCodecPreset);
	setVideoReadbackBufferCount(preset.readbackBufferCount);
	setVideoReadbackPolicy(preset.readbackPolicy);
	setRecordingAudioRingBufferSeconds(preset.audioRingBufferSeconds);
	if (const std::string compatibilityMessage =
			recordingMuxProfileCompatibilityMessage(preset.muxProfile, preset.videoCodecPreset);
		!compatibilityMessage.empty()) {
		setStatus(compatibilityMessage);
	}
}

ofxVlc4RecordingPreset ofxVlc4::getRecordingPreset() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	ofxVlc4RecordingPreset preset = m_impl->recordingSessionRuntime.preset;
	preset.videoFrameRate = getVideoRecordingFrameRate();
	preset.videoBitrateKbps = getVideoRecordingBitrateKbps();
	preset.videoCodecPreset = getVideoRecordingCodecPreset();
	preset.readbackBufferCount = getVideoReadbackBufferCount();
	preset.readbackPolicy = getVideoReadbackPolicy();
	preset.audioRingBufferSeconds = getRecordingAudioRingBufferSeconds();
	return preset;
}

void ofxVlc4::setRecordingAudioSourcePreset(ofxVlc4RecordingAudioSource source) {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	m_impl->recordingSessionRuntime.preset.audioSource = source;
}

ofxVlc4RecordingAudioSource ofxVlc4::getRecordingAudioSourcePreset() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	return m_impl->recordingSessionRuntime.preset.audioSource;
}

void ofxVlc4::setRecordingOutputSizePreset(int width, int height) {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	m_impl->recordingSessionRuntime.preset.targetWidth = std::max(0, width);
	m_impl->recordingSessionRuntime.preset.targetHeight = std::max(0, height);
}

std::pair<int, int> ofxVlc4::getRecordingOutputSizePreset() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	return {
		m_impl->recordingSessionRuntime.preset.targetWidth,
		m_impl->recordingSessionRuntime.preset.targetHeight
	};
}

void ofxVlc4::setRecordingMuxProfile(ofxVlc4RecordingMuxProfile profile) {
	ofxVlc4RecordingVideoCodecPreset codecPreset = ofxVlc4RecordingVideoCodecPreset::H264;
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
		m_impl->recordingSessionRuntime.preset.muxProfile = profile;
		codecPreset = m_impl->recordingSessionRuntime.preset.videoCodecPreset;
	}
	if (const std::string compatibilityMessage = recordingMuxProfileCompatibilityMessage(profile, codecPreset);
		!compatibilityMessage.empty()) {
		setStatus(compatibilityMessage);
	}
}

ofxVlc4RecordingMuxProfile ofxVlc4::getRecordingMuxProfile() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	return m_impl->recordingSessionRuntime.preset.muxProfile;
}

void ofxVlc4::setRecordingVideoFrameRatePreset(int fps) {
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
		m_impl->recordingSessionRuntime.preset.videoFrameRate = std::max(1, fps);
	}
	setVideoRecordingFrameRate(fps);
}

int ofxVlc4::getRecordingVideoFrameRatePreset() const {
	return getVideoRecordingFrameRate();
}

void ofxVlc4::setRecordingVideoBitratePreset(int bitrateKbps) {
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
		m_impl->recordingSessionRuntime.preset.videoBitrateKbps = std::max(0, bitrateKbps);
	}
	setVideoRecordingBitrateKbps(bitrateKbps);
}

int ofxVlc4::getRecordingVideoBitratePreset() const {
	return getVideoRecordingBitrateKbps();
}

void ofxVlc4::setRecordingAudioBitratePreset(int bitrateKbps) {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	m_impl->recordingSessionRuntime.preset.audioBitrateKbps = std::max(0, bitrateKbps);
}

int ofxVlc4::getRecordingAudioBitratePreset() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	return m_impl->recordingSessionRuntime.preset.audioBitrateKbps;
}

void ofxVlc4::setRecordingDeleteMuxSourceFilesOnSuccess(bool enabled) {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	m_impl->recordingSessionRuntime.preset.deleteMuxSourceFilesOnSuccess = enabled;
}

bool ofxVlc4::getRecordingDeleteMuxSourceFilesOnSuccess() const {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	return m_impl->recordingSessionRuntime.preset.deleteMuxSourceFilesOnSuccess;
}

void ofxVlc4::clearRecordingSessionConfig() {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	m_impl->recordingSessionRuntime.hasConfig = false;
	m_impl->recordingSessionRuntime.config = {};
}

void ofxVlc4::storeRecordingSessionConfig(const ofxVlc4RecordingSessionConfig & config) {
	std::lock_guard<std::mutex> lock(m_impl->recordingSessionRuntime.mutex);
	m_impl->recordingSessionRuntime.hasConfig = true;
	m_impl->recordingSessionRuntime.config = config;
}

void ofxVlc4::finalizeRecordingMuxThread() {
	const std::shared_ptr<RecordingMuxRuntimeState::TaskState> activeTask = m_impl->recordingMuxRuntime.activeTask;
	if (!m_impl->recordingMuxRuntime.worker.joinable() || !activeTask || activeTask->inProgress.load()) {
		return;
	}

	m_impl->recordingMuxRuntime.worker.join();
	m_impl->recordingMuxRuntime.activeTask.reset();
	m_impl->recordingMuxRuntime.inProgress.store(false);
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
		std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
		m_impl->recordingMuxRuntime.completedOutputPath = completedOutputPath;
		m_impl->recordingMuxRuntime.completedError = completedError;
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
				m_impl->recordingMuxRuntime.deferredSourceCleanup.begin(),
				m_impl->recordingMuxRuntime.deferredSourceCleanup.end(),
				[&](const RecordingMuxRuntimeState::DeferredSourceCleanup & item) {
					return item.path == path;
				});
			if (duplicate != m_impl->recordingMuxRuntime.deferredSourceCleanup.end()) {
				duplicate->label = label;
				duplicate->deadline = deadline;
				return;
			}
			m_impl->recordingMuxRuntime.deferredSourceCleanup.push_back({ path, label, deadline });
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
		std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
		if (m_impl->recordingMuxRuntime.deferredSourceCleanup.empty()) {
			return;
		}
		cleanupItems.swap(m_impl->recordingMuxRuntime.deferredSourceCleanup);
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

	std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
	for (auto & item : retryItems) {
		const auto duplicate = std::find_if(
			m_impl->recordingMuxRuntime.deferredSourceCleanup.begin(),
			m_impl->recordingMuxRuntime.deferredSourceCleanup.end(),
			[&](const RecordingMuxRuntimeState::DeferredSourceCleanup & existingItem) {
				return existingItem.path == item.path;
			});
		if (duplicate != m_impl->recordingMuxRuntime.deferredSourceCleanup.end()) {
			duplicate->label = item.label;
			duplicate->deadline = std::max(duplicate->deadline, item.deadline);
			continue;
		}
		m_impl->recordingMuxRuntime.deferredSourceCleanup.push_back(std::move(item));
	}
}

void ofxVlc4::updatePendingRecordingMux() {
	if (!m_impl->recordingMuxRuntime.pending.load()) {
		return;
	}
	const std::shared_ptr<RecordingMuxRuntimeState::TaskState> activeTask = m_impl->recordingMuxRuntime.activeTask;
	if (activeTask && activeTask->inProgress.load()) {
		return;
	}
	if (m_impl->recordingObjectRuntime.recorder.hasActiveCaptureSession()) {
		setRecordingSessionState(ofxVlc4RecordingSessionState::Finalizing);
		return;
	}

	const std::string finishedVideoPath = m_impl->recordingObjectRuntime.recorder.getLastFinishedVideoPath();
	const std::string finishedAudioPath = m_impl->recordingObjectRuntime.recorder.getLastFinishedAudioPath();
	ofxVlc4MuxOptions options;
	std::string previousVideoPath;
	std::string previousAudioPath;
	std::string expectedVideoPath;
	std::string expectedAudioPath;
	std::string requestedOutputPath;
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
		options = m_impl->recordingMuxRuntime.options;
		previousVideoPath = m_impl->recordingMuxRuntime.previousVideoPath;
		previousAudioPath = m_impl->recordingMuxRuntime.previousAudioPath;
		expectedVideoPath = m_impl->recordingMuxRuntime.expectedVideoPath;
		expectedAudioPath = m_impl->recordingMuxRuntime.expectedAudioPath;
		requestedOutputPath = m_impl->recordingMuxRuntime.requestedOutputPath;
	}

	const std::string resolvedVideoPath = !finishedVideoPath.empty() ? finishedVideoPath : expectedVideoPath;
	const std::string resolvedAudioPath = !finishedAudioPath.empty() ? finishedAudioPath : expectedAudioPath;
	if (resolvedVideoPath.empty() || resolvedAudioPath.empty()) {
		return;
	}
	if (resolvedVideoPath == previousVideoPath || resolvedAudioPath == previousAudioPath) {
		return;
	}

	m_impl->recordingMuxRuntime.pending.store(false);
	m_impl->recordingMuxRuntime.inProgress.store(true);
	setRecordingSessionState(ofxVlc4RecordingSessionState::Muxing);
	const std::string outputPath =
		requestedOutputPath.empty()
			? buildDefaultMuxOutputPath(resolvedVideoPath, options.containerMux)
			: requestedOutputPath;
	const auto task = std::make_shared<RecordingMuxRuntimeState::TaskState>();
	{
		std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
		m_impl->recordingMuxRuntime.activeTask = task;
	}
	{
		std::lock_guard<std::mutex> lock(task->mutex);
		task->options = options;
		task->sourceVideoPath = resolvedVideoPath;
		task->sourceAudioPath = resolvedAudioPath;
		task->outputPath = outputPath;
	}
	setStatus("Muxing recording...");
	m_impl->recordingMuxRuntime.worker = std::thread([task, resolvedVideoPath, resolvedAudioPath, outputPath, options]() {
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
	m_impl->recordingMuxRuntime.pending.store(false);
	const std::shared_ptr<RecordingMuxRuntimeState::TaskState> activeTask = m_impl->recordingMuxRuntime.activeTask;
	if (activeTask) {
		activeTask->cancelRequested.store(true, std::memory_order_release);
	}
	if (m_impl->recordingMuxRuntime.worker.joinable()) {
		m_impl->recordingMuxRuntime.worker.join();
	}
	m_impl->recordingMuxRuntime.inProgress.store(false);
	m_impl->recordingMuxRuntime.activeTask.reset();
	setRecordingSessionState(ofxVlc4RecordingSessionState::Idle);
	std::lock_guard<std::mutex> lock(m_impl->recordingMuxRuntime.mutex);
	m_impl->recordingMuxRuntime.previousVideoPath.clear();
	m_impl->recordingMuxRuntime.previousAudioPath.clear();
	m_impl->recordingMuxRuntime.expectedVideoPath.clear();
	m_impl->recordingMuxRuntime.expectedAudioPath.clear();
	m_impl->recordingMuxRuntime.requestedOutputPath.clear();
	m_impl->recordingMuxRuntime.completedOutputPath.clear();
	m_impl->recordingMuxRuntime.completedError.clear();
	clearRecordingSessionConfig();
}

bool ofxVlc4::isInitialized() const {
	return sessionInstance() != nullptr && sessionPlayer() != nullptr;
}

void ofxVlc4::clearRecorderCaptureState(const std::shared_ptr<ofAppGLFWWindow> & cleanupWindow) {
	if (cleanupWindow) {
		cleanupWindow->makeCurrent();
	}
	m_impl->recordingObjectRuntime.recorder.clearCaptureState();
}

void ofxVlc4::clearWindowCaptureState(const std::shared_ptr<ofAppGLFWWindow> & cleanupWindow) {
	unregisterWindowCaptureListener();
	m_impl->windowCaptureRuntime.active = false;
	m_impl->windowCaptureRuntime.includeAudioCapture = false;
	m_impl->windowCaptureRuntime.sourceWidth = 0;
	m_impl->windowCaptureRuntime.sourceHeight = 0;
	m_impl->windowCaptureRuntime.captureWidth = 0;
	m_impl->windowCaptureRuntime.captureHeight = 0;
	m_impl->windowCaptureRuntime.capturePixels.clear();
	if (cleanupWindow) {
		cleanupWindow->makeCurrent();
	}
	m_impl->windowCaptureRuntime.sourceTexture.clear();
	clearAllocatedFbo(m_impl->windowCaptureRuntime.captureFbo);
}

void ofxVlc4::releaseVlcResources() {
	finalizeRecordingMuxThread();
	cancelPendingRecordingMux();
	detachEvents();
	dismissAllDialogs();
	stopMediaDiscoveryInternal();
	stopRendererDiscoveryInternal();
	std::shared_ptr<ofAppGLFWWindow> cleanupWindow = m_impl->videoResourceRuntime.vlcWindow ? m_impl->videoResourceRuntime.vlcWindow : m_impl->videoResourceRuntime.mainWindow;
	const bool recorderNeedsCleanup = m_impl->recordingObjectRuntime.recorder.hasCleanupState();
	const bool needsGlCleanup =
		m_impl->videoResourceRuntime.vlcFramebufferId != 0 ||
		m_impl->videoResourceRuntime.videoTexture.isAllocated() ||
		m_impl->videoResourceRuntime.exposedTextureFbo.isAllocated() ||
		m_impl->videoResourceRuntime.videoAdjustShaderReady ||
		m_impl->videoResourceRuntime.videoAdjustShader.isLoaded() ||
		m_impl->windowCaptureRuntime.captureFbo.isAllocated() ||
		recorderNeedsCleanup;

	if (m_impl->subsystemRuntime.coreSession->player()) {
		if (m_impl->watchTimeRuntime.registered) {
			libvlc_media_player_unwatch_time(m_impl->subsystemRuntime.coreSession->player());
			m_impl->watchTimeRuntime.registered = false;
		}
		libvlc_video_set_adjust_int(m_impl->subsystemRuntime.coreSession->player(), libvlc_adjust_Enable, 0);
		libvlc_media_player_release(m_impl->subsystemRuntime.coreSession->player());
		m_impl->subsystemRuntime.coreSession->setPlayer(nullptr);
		m_impl->subsystemRuntime.coreSession->setPlayerEvents(nullptr);
	}

	// The player is now fully released — VLC has joined all internal threads
	// (vout, audio, mainloop) and called our make_current / cleanup callbacks
	// as needed.  From this point no VLC thread will invoke our callbacks, so
	// flip the guard to prevent any stale or late invocations from touching
	// resources we are about to destroy.
	m_impl->lifecycleRuntime.shuttingDown.store(true, std::memory_order_release);

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
		m_impl->subsystemRuntime.videoComponent->clearPublishedFrameFence();
		if (m_impl->videoResourceRuntime.vlcWindow) {
			ofxVlc4GlOps::deleteFbo(m_impl->videoResourceRuntime.vlcFramebufferId);
		}
		if (m_impl->videoResourceRuntime.videoAdjustShader.isLoaded()) {
			m_impl->videoResourceRuntime.videoAdjustShader.unload();
		}
		m_impl->videoResourceRuntime.videoAdjustShaderReady = false;
		m_impl->videoResourceRuntime.videoTexture.clear();
		clearAllocatedFbo(m_impl->videoResourceRuntime.exposedTextureFbo);
	}
	if (cleanupWindow && needsGlCleanup) {
		// Restore the main window GL context so that callers invoked from
		// within draw() (e.g. an ImGui "Apply" button) can continue issuing
		// GL calls in the same frame.  Without this, glfwMakeContextCurrent(nullptr)
		// would leave the thread with no current context and any subsequent GL
		// operation would fail or crash.
		if (m_impl->videoResourceRuntime.mainWindow &&
			m_impl->videoResourceRuntime.mainWindow->getGLFWWindow()) {
			m_impl->videoResourceRuntime.mainWindow->makeCurrent();
		} else {
			glfwMakeContextCurrent(nullptr);
		}
	}
	releaseD3D11Resources();
	clearVideoHdrMetadata();
	m_impl->videoGeometryRuntime.allocatedVideoWidth = 1;
	m_impl->videoGeometryRuntime.allocatedVideoHeight = 1;

	if (m_impl->subsystemRuntime.coreSession->instance()) {
		libvlc_log_unset(m_impl->subsystemRuntime.coreSession->instance());
		m_impl->subsystemRuntime.mediaComponent->closeLibVlcLogFile();
		libvlc_dialog_set_error_callback(m_impl->subsystemRuntime.coreSession->instance(), nullptr, nullptr);
		libvlc_dialog_set_callbacks(m_impl->subsystemRuntime.coreSession->instance(), nullptr, nullptr);
		libvlc_release(m_impl->subsystemRuntime.coreSession->instance());
		m_impl->subsystemRuntime.coreSession->setInstance(nullptr);
	}
	processDeferredRecordingMuxCleanup(true);

	clearWatchTimeState();
	clearMidiTransport();

	resetAudioStateInfo();
	resetRendererStateInfo();
	resetSubtitleStateInfo();
	resetNavigationStateInfo();

	m_impl->videoPresentationRuntime.activeVideoOutputBackend = m_impl->videoPresentationRuntime.videoOutputBackend;
	m_impl->effectsRuntime.activeVideoAdjustmentEngine = m_impl->effectsRuntime.videoAdjustmentEngine;
}

void ofxVlc4::close() {
	bool expected = false;
	if (!m_impl->lifecycleRuntime.closeRequested.compare_exchange_strong(expected, true)) {
		return;
	}

	m_impl->subsystemRuntime.playbackController->prepareForClose();

	// Do NOT set shuttingDown before releaseVlcResources() — VLC's OpenGL
	// display module needs our make_current callback to work during the
	// player release so it can delete its own GL resources with a valid
	// context.  releaseVlcResources() sets the flag after the player is
	// fully released.
	releaseVlcResources();
	m_impl->audioRuntime.ready.store(false);
	m_impl->videoFrameRuntime.isVideoLoaded.store(false);
	m_impl->videoFrameRuntime.startupPlaybackStatePrepared.store(false);
	m_impl->videoFrameRuntime.hasReceivedVideoFrame.store(false);
	m_impl->videoFrameRuntime.vlcFboBound = false;
	setStatus("Player closed.");
}

void ofxVlc4::updateMidiTransport(double nowSeconds) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	if (!m_impl->midiRuntime.playback.isLoaded()) {
		return;
	}

	if (m_impl->midiRuntime.syncToWatchTime && m_impl->midiRuntime.syncSource == ofxVlc4MidiSyncSource::WatchTime) {
		const WatchTimeInfo watchTime = getWatchTimeInfo();
		const int64_t targetTimeUs = watchTime.interpolatedTimeUs >= 0 ? watchTime.interpolatedTimeUs : watchTime.timeUs;
		if (watchTime.available && targetTimeUs >= 0) {
			const double targetSeconds = static_cast<double>(targetTimeUs) / 1000000.0;
			const double currentSeconds = m_impl->midiRuntime.playback.getPositionSeconds();
			const bool discontinuity = m_impl->midiRuntime.lastWatchTimeUs < 0 ||
				std::llabs(targetTimeUs - m_impl->midiRuntime.lastWatchTimeUs) > 200000;
			const bool drifted = std::abs(currentSeconds - targetSeconds) > 0.05;

			if (watchTime.seeking || discontinuity || drifted) {
				m_impl->midiRuntime.playback.seek(targetSeconds, nowSeconds);
			}

			if (watchTime.paused) {
				if (m_impl->midiRuntime.playback.isPlaying()) {
					m_impl->midiRuntime.playback.pause(nowSeconds);
				}
			} else if (!m_impl->midiRuntime.playback.isPlaying()) {
				m_impl->midiRuntime.playback.play(nowSeconds);
			}

			m_impl->midiRuntime.lastWatchTimeUs = targetTimeUs;
		}
	}

	m_impl->midiRuntime.playback.update(nowSeconds);
}

bool ofxVlc4::loadMidiFile(const std::string & path, bool noteOffAsZeroVelocity) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	MidiAnalysisReport report = m_impl->midiRuntime.analyzer.analyzeFile(path);
	if (!report.valid) {
		m_impl->midiRuntime.report = report;
		m_impl->midiRuntime.messages.clear();
		m_impl->midiRuntime.playback.clear();
		m_impl->midiRuntime.lastWatchTimeUs = -1;
		return false;
	}

	std::vector<MidiChannelMessage> messages = MidiBridge::toMessages(report, noteOffAsZeroVelocity);
	if (!m_impl->midiRuntime.playback.load(path, report, messages)) {
		m_impl->midiRuntime.report = report;
		m_impl->midiRuntime.messages.clear();
		m_impl->midiRuntime.lastWatchTimeUs = -1;
		return false;
	}

	m_impl->midiRuntime.noteOffAsZeroVelocity = noteOffAsZeroVelocity;
	m_impl->midiRuntime.report = std::move(report);
	m_impl->midiRuntime.messages = std::move(messages);
	m_impl->midiRuntime.lastWatchTimeUs = -1;
	return true;
}

void ofxVlc4::clearMidiTransport() {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.clear();
	m_impl->midiRuntime.report = {};
	m_impl->midiRuntime.messages.clear();
	m_impl->midiRuntime.lastWatchTimeUs = -1;
}

bool ofxVlc4::hasMidiLoaded() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.isLoaded();
}

bool ofxVlc4::isMidiPlaying() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.isPlaying();
}

bool ofxVlc4::isMidiPaused() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.isPaused();
}

bool ofxVlc4::isMidiFinished() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.isFinished();
}

double ofxVlc4::getMidiDurationSeconds() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.getDurationSeconds();
}

double ofxVlc4::getMidiPositionSeconds() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.getPositionSeconds();
}

double ofxVlc4::getMidiTempoMultiplier() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.getTempoMultiplier();
}

void ofxVlc4::playMidi() {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.play(ofGetElapsedTimef());
}

void ofxVlc4::pauseMidi() {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.pause(ofGetElapsedTimef());
}

void ofxVlc4::stopMidi() {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.stop();
}

void ofxVlc4::seekMidi(double seconds) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.seek(seconds, ofGetElapsedTimef());
}

void ofxVlc4::setMidiTempoMultiplier(double multiplier) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.setTempoMultiplier(multiplier, ofGetElapsedTimef());
}

MidiAnalysisReport ofxVlc4::getMidiAnalysisReport() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.report;
}

std::vector<MidiChannelMessage> ofxVlc4::getMidiMessages() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.messages;
}

ofxVlc4::MidiTransportInfo ofxVlc4::getMidiTransportInfo() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	MidiTransportInfo info;
	info.loaded = m_impl->midiRuntime.playback.isLoaded();
	info.playing = m_impl->midiRuntime.playback.isPlaying();
	info.paused = m_impl->midiRuntime.playback.isPaused();
	info.finished = m_impl->midiRuntime.playback.isFinished();
	info.loopEnabled = m_impl->midiRuntime.playback.isLoopEnabled();
	info.durationSeconds = m_impl->midiRuntime.playback.getDurationSeconds();
	info.positionSeconds = m_impl->midiRuntime.playback.getPositionSeconds();
	info.positionFraction = m_impl->midiRuntime.playback.getPositionFraction();
	info.tempoMultiplier = m_impl->midiRuntime.playback.getTempoMultiplier();
	info.currentBpm = m_impl->midiRuntime.playback.getCurrentBpm();
	info.dispatchedCount = m_impl->midiRuntime.playback.getDispatchedCount();
	info.messageCount = m_impl->midiRuntime.messages.size();
	info.syncSource = m_impl->midiRuntime.syncSource;
	info.syncToWatchTime = m_impl->midiRuntime.syncToWatchTime;
	info.hasCallback = m_impl->midiRuntime.playback.hasMessageCallback();
	info.hasFinishedCallback = m_impl->midiRuntime.playback.hasFinishedCallback();
	info.syncSettings = m_impl->midiRuntime.playback.getSyncSettings();
	return info;
}

void ofxVlc4::setMidiMessageCallback(MidiMessageCallback callback) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.setMessageCallback(std::move(callback));
}

void ofxVlc4::clearMidiMessageCallback() {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.clearMessageCallback();
}

bool ofxVlc4::hasMidiMessageCallback() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.hasMessageCallback();
}

void ofxVlc4::setMidiSyncSettings(const MidiSyncSettings & settings) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.setSyncSettings(settings);
}

MidiSyncSettings ofxVlc4::getMidiSyncSettings() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.getSyncSettings();
}

void ofxVlc4::setMidiSyncSource(ofxVlc4MidiSyncSource source) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.syncSource = source;
}

ofxVlc4MidiSyncSource ofxVlc4::getMidiSyncSource() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.syncSource;
}

void ofxVlc4::setMidiSyncToWatchTimeEnabled(bool enabled) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.syncToWatchTime = enabled;
	if (!enabled) {
		m_impl->midiRuntime.lastWatchTimeUs = -1;
	}
}

bool ofxVlc4::isMidiSyncToWatchTimeEnabled() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.syncToWatchTime;
}

void ofxVlc4::setMidiLoopEnabled(bool enabled) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.setLoopEnabled(enabled);
}

bool ofxVlc4::isMidiLoopEnabled() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.isLoopEnabled();
}

double ofxVlc4::getMidiCurrentBpm() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.getCurrentBpm();
}

double ofxVlc4::getMidiPositionFraction() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.getPositionFraction();
}

void ofxVlc4::seekMidiFraction(double fraction) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.seekFraction(fraction, ofGetElapsedTimef());
}

void ofxVlc4::setMidiFinishedCallback(MidiFinishedCallback callback) {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.setFinishedCallback(std::move(callback));
}

void ofxVlc4::clearMidiFinishedCallback() {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	m_impl->midiRuntime.playback.clearFinishedCallback();
}

bool ofxVlc4::hasMidiFinishedCallback() const {
	std::lock_guard<std::mutex> lock(m_impl->midiRuntime.mutex);
	return m_impl->midiRuntime.playback.hasFinishedCallback();
}

int ofxVlc4::getChannelCount() const {
	return m_impl->audioRuntime.channels.load(std::memory_order_relaxed);
}
int ofxVlc4::getSampleRate() const {
	return m_impl->audioRuntime.sampleRate.load(std::memory_order_relaxed);
}
