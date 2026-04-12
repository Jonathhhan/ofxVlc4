#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "ofxVlc4Audio.h"
#include "media/ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "support/ofxVlc4Utils.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <sstream>

using ofxVlc4Utils::trimWhitespace;
using ofxVlc4Utils::parseFilterChainEntries;
using ofxVlc4Utils::joinFilterChainEntries;

namespace {
constexpr double kBufferedAudioSeconds = 0.75;
constexpr double kMinBufferedAudioSeconds = 0.05;
constexpr double kMaxBufferedAudioSeconds = 5.0;
constexpr float kEqualizerPresetMatchToleranceDb = 0.1f;
constexpr size_t kSpectrumFftSize = 4096;
constexpr float kSpectrumMinFrequencyHz = 20.0f;
constexpr float kSpectrumWindowOctaves = 0.10f;
constexpr float kSpectrumTopDb = 0.0f;
constexpr float kSpectrumBottomDb = -72.0f;
constexpr float kSpectrumAttack = 0.40f;
constexpr float kSpectrumRelease = 0.12f;
constexpr float kPi = 3.14159265358979323846f;
constexpr uint64_t kSpectrumUpdateIntervalMicros = 33333;

uint64_t steadyMicrosNow() {
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
}

void updateAtomicMax(std::atomic<uint64_t> & target, uint64_t value) {
	uint64_t current = target.load(std::memory_order_relaxed);
	while (current < value &&
		!target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
	}
}

const char * audioCaptureSampleFormatLabel(ofxVlc4::AudioCaptureSampleFormat format) {
	switch (format) {
	case ofxVlc4::AudioCaptureSampleFormat::Signed16:
		return "s16";
	case ofxVlc4::AudioCaptureSampleFormat::Signed32:
		return "s32";
	case ofxVlc4::AudioCaptureSampleFormat::Float32:
	default:
		return "float32";
	}
}

const char * audioCaptureSampleFormatCode(ofxVlc4::AudioCaptureSampleFormat format) {
	switch (format) {
	case ofxVlc4::AudioCaptureSampleFormat::Signed16:
		return "S16N";
	case ofxVlc4::AudioCaptureSampleFormat::Signed32:
		return "S32N";
	case ofxVlc4::AudioCaptureSampleFormat::Float32:
	default:
		return "FL32";
	}
}

int normalizeAudioCaptureSampleRate(int rate) {
	switch (rate) {
	case 22050:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 192000:
		return rate;
	default:
		return 44100;
	}
}

int normalizeAudioCaptureChannelCount(int channels) {
	return channels <= 1 ? 1 : 2;
}

double normalizeAudioCaptureBufferSeconds(double seconds) {
	if (!std::isfinite(seconds)) {
		return kBufferedAudioSeconds;
	}
	return std::clamp(seconds, kMinBufferedAudioSeconds, kMaxBufferedAudioSeconds);
}

libvlc_audio_output_mixmode_t toLibvlcAudioMixMode(ofxVlc4::AudioMixMode mode) {
	switch (mode) {
	case ofxVlc4::AudioMixMode::Stereo:
		return libvlc_AudioMixMode_Stereo;
	case ofxVlc4::AudioMixMode::Binaural:
		return libvlc_AudioMixMode_Binaural;
	case ofxVlc4::AudioMixMode::Surround4_0:
		return libvlc_AudioMixMode_4_0;
	case ofxVlc4::AudioMixMode::Surround5_1:
		return libvlc_AudioMixMode_5_1;
	case ofxVlc4::AudioMixMode::Surround7_1:
		return libvlc_AudioMixMode_7_1;
	case ofxVlc4::AudioMixMode::Auto:
	default:
		return libvlc_AudioMixMode_Unset;
	}
}

ofxVlc4::AudioMixMode fromLibvlcAudioMixMode(libvlc_audio_output_mixmode_t mode) {
	switch (mode) {
	case libvlc_AudioMixMode_Stereo:
		return ofxVlc4::AudioMixMode::Stereo;
	case libvlc_AudioMixMode_Binaural:
		return ofxVlc4::AudioMixMode::Binaural;
	case libvlc_AudioMixMode_4_0:
		return ofxVlc4::AudioMixMode::Surround4_0;
	case libvlc_AudioMixMode_5_1:
		return ofxVlc4::AudioMixMode::Surround5_1;
	case libvlc_AudioMixMode_7_1:
		return ofxVlc4::AudioMixMode::Surround7_1;
	case libvlc_AudioMixMode_Unset:
	default:
		return ofxVlc4::AudioMixMode::Auto;
	}
}

const char * audioMixModeLabel(ofxVlc4::AudioMixMode mode) {
	switch (mode) {
	case ofxVlc4::AudioMixMode::Stereo:
		return "stereo";
	case ofxVlc4::AudioMixMode::Binaural:
		return "binaural";
	case ofxVlc4::AudioMixMode::Surround4_0:
		return "4.0";
	case ofxVlc4::AudioMixMode::Surround5_1:
		return "5.1";
	case ofxVlc4::AudioMixMode::Surround7_1:
		return "7.1";
	case ofxVlc4::AudioMixMode::Auto:
	default:
		return "auto";
	}
}

libvlc_audio_output_stereomode_t toLibvlcAudioStereoMode(ofxVlc4::AudioStereoMode mode) {
	switch (mode) {
	case ofxVlc4::AudioStereoMode::Stereo:
		return libvlc_AudioStereoMode_Stereo;
	case ofxVlc4::AudioStereoMode::ReverseStereo:
		return libvlc_AudioStereoMode_RStereo;
	case ofxVlc4::AudioStereoMode::Left:
		return libvlc_AudioStereoMode_Left;
	case ofxVlc4::AudioStereoMode::Right:
		return libvlc_AudioStereoMode_Right;
	case ofxVlc4::AudioStereoMode::DolbySurround:
		return libvlc_AudioStereoMode_Dolbys;
	case ofxVlc4::AudioStereoMode::Mono:
		return libvlc_AudioStereoMode_Mono;
	case ofxVlc4::AudioStereoMode::Auto:
	default:
		return libvlc_AudioStereoMode_Unset;
	}
}

ofxVlc4::AudioStereoMode fromLibvlcAudioStereoMode(libvlc_audio_output_stereomode_t mode) {
	switch (mode) {
	case libvlc_AudioStereoMode_Stereo:
		return ofxVlc4::AudioStereoMode::Stereo;
	case libvlc_AudioStereoMode_RStereo:
		return ofxVlc4::AudioStereoMode::ReverseStereo;
	case libvlc_AudioStereoMode_Left:
		return ofxVlc4::AudioStereoMode::Left;
	case libvlc_AudioStereoMode_Right:
		return ofxVlc4::AudioStereoMode::Right;
	case libvlc_AudioStereoMode_Dolbys:
		return ofxVlc4::AudioStereoMode::DolbySurround;
	case libvlc_AudioStereoMode_Mono:
		return ofxVlc4::AudioStereoMode::Mono;
	case libvlc_AudioStereoMode_Unset:
	default:
		return ofxVlc4::AudioStereoMode::Auto;
	}
}

const char * audioStereoModeLabel(ofxVlc4::AudioStereoMode mode) {
	switch (mode) {
	case ofxVlc4::AudioStereoMode::Stereo:
		return "stereo";
	case ofxVlc4::AudioStereoMode::ReverseStereo:
		return "reverse stereo";
	case ofxVlc4::AudioStereoMode::Left:
		return "left";
	case ofxVlc4::AudioStereoMode::Right:
		return "right";
	case ofxVlc4::AudioStereoMode::DolbySurround:
		return "dolby surround";
	case ofxVlc4::AudioStereoMode::Mono:
		return "mono";
	case ofxVlc4::AudioStereoMode::Auto:
	default:
		return "auto";
	}
}

std::vector<std::string> splitSerializedPreset(const std::string & input) {
	std::vector<std::string> lines;
	std::string normalized = input;
	std::replace(normalized.begin(), normalized.end(), ';', '\n');
	std::stringstream stream(normalized);
	std::string line;
	while (std::getline(stream, line)) {
		line = trimWhitespace(line);
		if (!line.empty()) {
			lines.push_back(std::move(line));
		}
	}
	return lines;
}

bool equalizerBandsMatch(
	const std::vector<float> & lhs,
	const std::vector<float> & rhs,
	float toleranceDb) {
	if (lhs.size() != rhs.size()) {
		return false;
	}

	for (size_t i = 0; i < lhs.size(); ++i) {
		if (std::abs(lhs[i] - rhs[i]) > toleranceDb) {
			return false;
		}
	}

	return true;
}

size_t reverseBits(size_t value, unsigned int bitCount) {
	size_t reversed = 0;
	for (unsigned int i = 0; i < bitCount; ++i) {
		reversed = (reversed << 1) | (value & 1u);
		value >>= 1u;
	}
	return reversed;
}

void fftInPlace(std::vector<std::complex<float>> & values) {
	const size_t size = values.size();
	if (size <= 1) {
		return;
	}

	// fftInPlace requires a power-of-two input size
	if ((size & (size - 1)) != 0) {
		return;
	}

	unsigned int bitCount = 0;
	for (size_t value = size; value > 1; value >>= 1u) {
		++bitCount;
	}

	for (size_t i = 0; i < size; ++i) {
		const size_t j = reverseBits(i, bitCount);
		if (j > i) {
			std::swap(values[i], values[j]);
		}
	}

	for (size_t len = 2; len <= size; len <<= 1u) {
		const float angle = (-2.0f * kPi) / static_cast<float>(len);
		const std::complex<float> phaseStep(std::cos(angle), std::sin(angle));
		for (size_t start = 0; start < size; start += len) {
			std::complex<float> phase(1.0f, 0.0f);
			const size_t halfLen = len >> 1u;
			for (size_t i = 0; i < halfLen; ++i) {
				const std::complex<float> even = values[start + i];
				const std::complex<float> odd = phase * values[start + i + halfLen];
				values[start + i] = even + odd;
				values[start + i + halfLen] = even - odd;
				phase *= phaseStep;
			}
		}
	}
}
}

ofxVlc4::AudioComponent::AudioComponent(ofxVlc4 & owner)
	: owner(owner) {}

void ofxVlc4::AudioComponent::clearSpectrumAnalysisCache() const {
	spectrumCacheValid = false;
	spectrumCacheVersion = 0;
	spectrumCacheSampleRate = 0;
	spectrumCacheChannelCount = 0;
	spectrumCachePointCount = 0;
	spectrumLastUpdateMicros = 0;
	cachedSpectrumLevels.clear();
	owner.m_impl->analysisRuntime.smoothedSpectrumLevels.clear();
}

void ofxVlc4::AudioComponent::ensureSpectrumWindowPrepared() const {
	if (spectrumWindow.size() == kSpectrumFftSize) {
		return;
	}

	spectrumWindow.resize(kSpectrumFftSize, 0.0f);
	spectrumWindowSum = 0.0f;
	for (size_t frameIndex = 0; frameIndex < kSpectrumFftSize; ++frameIndex) {
		const float window = 0.5f - (0.5f * std::cos((2.0f * kPi * static_cast<float>(frameIndex)) / static_cast<float>(kSpectrumFftSize - 1)));
		spectrumWindow[frameIndex] = window;
		spectrumWindowSum += window;
	}
}

bool ofxVlc4::shouldApplyEqualizerImmediately() const {
	return m_impl->subsystemRuntime.audioComponent->shouldApplyEqualizerImmediately();
}

void ofxVlc4::applyOrQueueEqualizerSettings() {
	m_impl->subsystemRuntime.audioComponent->applyOrQueueEqualizerSettings();
}

void ofxVlc4::applyPendingEqualizerOnPlay() {
	m_impl->subsystemRuntime.audioComponent->applyPendingEqualizerOnPlay();
}

void ofxVlc4::setAudioCaptureEnabled(bool enabled) {
	m_impl->subsystemRuntime.audioComponent->setAudioCaptureEnabled(enabled);
}

bool ofxVlc4::isAudioCaptureEnabled() const {
	return m_impl->subsystemRuntime.audioComponent->isAudioCaptureEnabled();
}

void ofxVlc4::submitRecordedAudioSamples(const float * samples, size_t sampleCount) {
	m_impl->subsystemRuntime.audioComponent->submitRecordedAudioSamples(samples, sampleCount);
}

void ofxVlc4::AudioComponent::prepareStartupAudioResources() {
	if (!owner.m_impl->playerConfigRuntime.audioCaptureEnabled) {
		return;
	}

	const AudioCaptureSampleFormat configuredFormat = owner.m_impl->playerConfigRuntime.audioCaptureSampleFormat;
	const int configuredRate = normalizeAudioCaptureSampleRate(owner.m_impl->playerConfigRuntime.audioCaptureSampleRate);
	const int configuredChannels = normalizeAudioCaptureChannelCount(owner.m_impl->playerConfigRuntime.audioCaptureChannelCount);

	owner.m_impl->playerConfigRuntime.activeAudioCaptureSampleFormat.store(static_cast<int>(configuredFormat), std::memory_order_relaxed);
	owner.m_impl->audioRuntime.sampleRate.store(configuredRate);
	owner.m_impl->audioRuntime.channels.store(configuredChannels);
	prepareRingBuffer();
	resetBuffer();
	prepareRecorderAudioBuffer(configuredRate, configuredChannels);
	playback().setAudioPauseSignaled(false);
}

const char * ofxVlc4::AudioComponent::getStartupAudioCaptureSampleFormatCode() const {
	return audioCaptureSampleFormatCode(owner.m_impl->playerConfigRuntime.audioCaptureSampleFormat);
}

unsigned int ofxVlc4::AudioComponent::getStartupAudioCaptureSampleRate() const {
	return static_cast<unsigned int>(normalizeAudioCaptureSampleRate(owner.m_impl->playerConfigRuntime.audioCaptureSampleRate));
}

unsigned int ofxVlc4::AudioComponent::getStartupAudioCaptureChannelCount() const {
	return static_cast<unsigned int>(normalizeAudioCaptureChannelCount(owner.m_impl->playerConfigRuntime.audioCaptureChannelCount));
}

void ofxVlc4::AudioComponent::prepareRingBuffer() {
	const int configuredRate = normalizeAudioCaptureSampleRate(owner.m_impl->playerConfigRuntime.audioCaptureSampleRate);
	const int configuredChannels = normalizeAudioCaptureChannelCount(owner.m_impl->playerConfigRuntime.audioCaptureChannelCount);
	const int rate = std::max(owner.m_impl->audioRuntime.sampleRate.load(), configuredRate);
	const int channelCount = std::max(owner.m_impl->audioRuntime.channels.load(), configuredChannels);
	owner.m_impl->audioRuntime.sampleRate.store(rate);
	owner.m_impl->audioRuntime.channels.store(channelCount);

	const size_t requestedSamples =
		static_cast<size_t>(rate) *
		static_cast<size_t>(channelCount) *
		normalizeAudioCaptureBufferSeconds(owner.m_impl->playerConfigRuntime.audioCaptureBufferSeconds);

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioMutex);
	owner.m_impl->audioBufferRuntime.ringBuffer.allocate(requestedSamples);
}

void ofxVlc4::AudioComponent::resetBuffer() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioMutex);
	owner.m_impl->audioBufferRuntime.ringBuffer.reset();
	clearSpectrumAnalysisCache();
}

void ofxVlc4::AudioComponent::prepareRecorderAudioBuffer(int configuredSampleRate, int configuredChannelCount) {
	recorder().prepareAudioCaptureBuffer(configuredSampleRate, configuredChannelCount);
}

void ofxVlc4::AudioComponent::captureRecorderAudioSamples(const float * samples, size_t sampleCount) {
	recorder().captureAudioSamples(samples, sampleCount);
}

void ofxVlc4::AudioComponent::resetRecorderCapturedAudio() {
	recorder().resetCapturedAudio();
}

void ofxVlc4::AudioComponent::applyCurrentPlayerSettings() {
	applyCurrentVolumeToPlayer();
	applyAudioOutputModule();
	applyAudioOutputDevice();
	applyAudioStereoMode();
	applyAudioMixMode();
	applyPlaybackRate();
	applyAudioDelay();
	applySubtitleDelay();
	applySubtitleTextScale();
}

void ofxVlc4::AudioComponent::applyCurrentVolumeToPlayer() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_audio_set_volume(player, ofClamp(owner.m_impl->audioRuntime.currentVolume.load(std::memory_order_acquire), 0, 100)) != 0) {
		owner.logWarning("libvlc_audio_set_volume failed.");
	}
}

void ofxVlc4::AudioComponent::applyAudioOutputModule() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || owner.m_impl->playerConfigRuntime.audioOutputModuleName.empty()) {
		return;
	}

	if (libvlc_audio_output_set(player, owner.m_impl->playerConfigRuntime.audioOutputModuleName.c_str()) != 0) {
		owner.logWarning("Audio output module could not be applied.");
	}
}

void ofxVlc4::AudioComponent::applyAudioOutputDevice() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	char * currentDevice = libvlc_audio_output_device_get(player);
	const bool hasCurrentDevice = currentDevice && currentDevice[0] != '\0';
	if (currentDevice) {
		libvlc_free(currentDevice);
	}

	if (owner.m_impl->playerConfigRuntime.audioOutputDeviceId.empty() && !hasCurrentDevice) {
		return;
	}

	if (libvlc_audio_output_device_set(
			player,
			owner.m_impl->playerConfigRuntime.audioOutputDeviceId.empty() ? nullptr : owner.m_impl->playerConfigRuntime.audioOutputDeviceId.c_str()) != 0) {
		owner.logWarning("Audio output device could not be applied.");
	}

	updateAudioStateFromDeviceEvent(owner.m_impl->playerConfigRuntime.audioOutputDeviceId);
}

void ofxVlc4::AudioComponent::applyAudioStereoMode() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_audio_set_stereomode(player, toLibvlcAudioStereoMode(owner.m_impl->playerConfigRuntime.audioStereoMode)) != 0) {
		owner.logWarning(std::string("Audio stereo mode could not be applied: ") + audioStereoModeLabel(owner.m_impl->playerConfigRuntime.audioStereoMode) + ".");
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.stereoMode = owner.m_impl->playerConfigRuntime.audioStereoMode;
}

void ofxVlc4::AudioComponent::applyAudioMixMode() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_audio_set_mixmode(player, toLibvlcAudioMixMode(owner.m_impl->playerConfigRuntime.audioMixMode)) != 0) {
		owner.logWarning(std::string("Audio mix mode could not be applied: ") + audioMixModeLabel(owner.m_impl->playerConfigRuntime.audioMixMode) + ".");
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.mixMode = owner.m_impl->playerConfigRuntime.audioMixMode;
}

void ofxVlc4::AudioComponent::applyEqualizerSettings() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (!owner.m_impl->effectsRuntime.equalizerEnabled) {
		owner.logVerbose("Equalizer disabled: clearing player equalizer.");
		libvlc_media_player_set_equalizer(player, nullptr);
		return;
	}

	libvlc_equalizer_t * equalizer = libvlc_audio_equalizer_new();
	if (!equalizer) {
		owner.logWarning("Equalizer could not be created.");
		return;
	}

	owner.logVerbose(
		"Applying equalizer: preamp=" + ofToString(owner.m_impl->effectsRuntime.equalizerPreamp, 2) +
		" bands=" + ofToString(owner.m_impl->effectsRuntime.equalizerBandAmps.size()) + ".");
	for (size_t index = 0; index < owner.m_impl->effectsRuntime.equalizerBandAmps.size(); ++index) {
		libvlc_audio_equalizer_set_amp_at_index(equalizer, owner.m_impl->effectsRuntime.equalizerBandAmps[index], static_cast<unsigned>(index));
	}
	libvlc_audio_equalizer_set_preamp(equalizer, owner.m_impl->effectsRuntime.equalizerPreamp);
	libvlc_media_player_set_equalizer(player, equalizer);
	libvlc_audio_equalizer_release(equalizer);
}

bool ofxVlc4::AudioComponent::shouldApplyEqualizerImmediately() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	return state == libvlc_Playing || state == libvlc_Paused;
}

void ofxVlc4::AudioComponent::applyOrQueueEqualizerSettings() {
	if (shouldApplyEqualizerImmediately()) {
		applyEqualizerSettings();
		clearPendingEqualizerApplyOnPlay();
	} else {
		setPendingEqualizerApplyOnPlay(true);
	}
}

void ofxVlc4::AudioComponent::applyPendingEqualizerOnPlay() {
	applyEqualizerSettings();
	clearPendingEqualizerApplyOnPlay();
}

void ofxVlc4::AudioComponent::setPendingEqualizerApplyOnPlay(bool pending) {
	owner.m_impl->playbackPolicyRuntime.pendingEqualizerApplyOnPlay.store(pending);
}

void ofxVlc4::AudioComponent::clearPendingEqualizerApplyOnPlay() {
	setPendingEqualizerApplyOnPlay(false);
}

bool ofxVlc4::AudioComponent::hasPendingEqualizerApplyOnPlay() const {
	return owner.m_impl->playbackPolicyRuntime.pendingEqualizerApplyOnPlay.load();
}

void ofxVlc4::AudioComponent::applyPlaybackRate() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_media_player_set_rate(player, owner.m_impl->playerConfigRuntime.playbackRate) != 0) {
		owner.logWarning("Playback rate could not be applied.");
	}
}

void ofxVlc4::AudioComponent::applyAudioDelay() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_audio_set_delay(player, owner.m_impl->playerConfigRuntime.audioDelayUs) != 0) {
		owner.logWarning("Audio delay could not be applied.");
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.audioDelayMs = static_cast<int>(owner.m_impl->playerConfigRuntime.audioDelayUs / 1000);
}

std::vector<ofxVlc4::AudioOutputModuleInfo> ofxVlc4::AudioComponent::getAudioOutputModules() const {
	std::vector<AudioOutputModuleInfo> modules;
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return modules;
	}

	libvlc_audio_output_t * moduleList = libvlc_audio_output_list_get(instance);
	for (libvlc_audio_output_t * module = moduleList; module != nullptr; module = module->p_next) {
		AudioOutputModuleInfo info;
		info.name = trimWhitespace(module->psz_name ? module->psz_name : "");
		info.description = trimWhitespace(module->psz_description ? module->psz_description : "");
		info.current = !owner.m_impl->playerConfigRuntime.audioOutputModuleName.empty() && info.name == owner.m_impl->playerConfigRuntime.audioOutputModuleName;
		modules.push_back(std::move(info));
	}

	if (moduleList) {
		libvlc_audio_output_list_release(moduleList);
	}

	return modules;
}

std::vector<ofxVlc4::AudioFilterInfo> ofxVlc4::AudioComponent::getAudioFilters() const {
	std::vector<AudioFilterInfo> filters;
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return filters;
	}

	libvlc_module_description_t * filterList = libvlc_audio_filter_list_get(instance);
	for (libvlc_module_description_t * filter = filterList; filter != nullptr; filter = filter->p_next) {
		AudioFilterInfo info;
		info.name = trimWhitespace(filter->psz_name ? filter->psz_name : "");
		info.shortName = trimWhitespace(filter->psz_shortname ? filter->psz_shortname : "");
		info.description = trimWhitespace(filter->psz_longname ? filter->psz_longname : "");
		info.help = trimWhitespace(filter->psz_help ? filter->psz_help : "");
		filters.push_back(std::move(info));
	}

	if (filterList) {
		libvlc_module_description_list_release(filterList);
	}

	return filters;
}

const std::string & ofxVlc4::AudioComponent::getAudioFilterChain() const {
	return owner.m_impl->playerConfigRuntime.audioFilterChain;
}

ofxVlc4::MediaComponent & ofxVlc4::AudioComponent::media() const {
	return *owner.m_impl->subsystemRuntime.mediaComponent;
}

PlaybackController & ofxVlc4::AudioComponent::playback() const {
	return *owner.m_impl->subsystemRuntime.playbackController;
}

ofxVlc4Recorder & ofxVlc4::AudioComponent::recorder() const {
	return owner.m_impl->recordingObjectRuntime.recorder;
}

void ofxVlc4::AudioComponent::setAudioFilterChain(const std::string & filterChain) {
	owner.m_impl->playerConfigRuntime.audioFilterChain = trimWhitespace(filterChain);
	if (owner.m_impl->playerConfigRuntime.audioFilterChain.empty()) {
		if (media().reapplyCurrentMediaForFilterChainChange("Audio")) {
			owner.logNotice("Audio filter chain cleared.");
			return;
		}
		owner.setStatus("Audio filter chain cleared. Reload media to apply.");
		owner.logNotice("Audio filter chain cleared.");
		return;
	}

	if (media().reapplyCurrentMediaForFilterChainChange("Audio")) {
		owner.logNotice("Audio filter chain: " + owner.m_impl->playerConfigRuntime.audioFilterChain + ".");
		return;
	}

	owner.setStatus("Audio filter chain set. Reload media to apply.");
	owner.logNotice("Audio filter chain: " + owner.m_impl->playerConfigRuntime.audioFilterChain + ".");
}

const std::string & ofxVlc4::AudioComponent::getSelectedAudioOutputModuleName() const {
	return owner.m_impl->playerConfigRuntime.audioOutputModuleName;
}

bool ofxVlc4::AudioComponent::selectAudioOutputModule(const std::string & moduleName) {
	const std::string trimmedModuleName = trimWhitespace(moduleName);
	if (owner.m_impl->playerConfigRuntime.audioOutputModuleName == trimmedModuleName) {
		return true;
	}
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		owner.m_impl->playerConfigRuntime.audioOutputModuleName = trimmedModuleName;
		return true;
	}
	if (trimmedModuleName.empty()) {
		owner.m_impl->playerConfigRuntime.audioOutputModuleName.clear();
		owner.m_impl->playerConfigRuntime.audioOutputDeviceId.clear();
		owner.setStatus(player ? "Audio output module reset for the next init." : "Audio output module reset.");
		owner.logNotice(
			player
				? "Audio output module reset for the next init."
				: "Audio output module reset.");
		return true;
	}

	owner.m_impl->playerConfigRuntime.audioOutputModuleName = trimmedModuleName;
	if (libvlc_audio_output_set(player, trimmedModuleName.c_str()) != 0) {
		owner.m_impl->playerConfigRuntime.audioOutputModuleName.clear();
		owner.logWarning("Audio output module could not be applied.");
		return false;
	}

	applyAudioOutputDevice();
	owner.setStatus("Audio output module set.");
	owner.logNotice("Audio output module: " + owner.m_impl->playerConfigRuntime.audioOutputModuleName + ".");
	return true;
}

std::vector<ofxVlc4::AudioOutputDeviceInfo> ofxVlc4::AudioComponent::getAudioOutputDevices() const {
	std::vector<AudioOutputDeviceInfo> devices;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return devices;
	}

	std::string currentDeviceId = owner.m_impl->playerConfigRuntime.audioOutputDeviceId;
	char * currentDevice = libvlc_audio_output_device_get(player);
	if (currentDevice) {
		currentDeviceId = currentDevice;
		libvlc_free(currentDevice);
	}

	libvlc_audio_output_device_t * deviceList = libvlc_audio_output_device_enum(player);
	for (libvlc_audio_output_device_t * device = deviceList; device != nullptr; device = device->p_next) {
		AudioOutputDeviceInfo info;
		info.id = device->psz_device ? device->psz_device : "";
		info.description = trimWhitespace(device->psz_description ? device->psz_description : "");
		info.current = !currentDeviceId.empty() && info.id == currentDeviceId;
		devices.push_back(std::move(info));
	}

	if (deviceList) {
		libvlc_audio_output_device_list_release(deviceList);
	}

	return devices;
}

std::string ofxVlc4::AudioComponent::getSelectedAudioOutputDeviceId() const {
	const AudioStateInfo info = getAudioStateInfo();
	return info.deviceKnown ? info.deviceId : owner.m_impl->playerConfigRuntime.audioOutputDeviceId;
}

bool ofxVlc4::AudioComponent::selectAudioOutputDevice(const std::string & deviceId) {
	owner.m_impl->playerConfigRuntime.audioOutputDeviceId = trimWhitespace(deviceId);
	applyAudioOutputDevice();
	owner.setStatus(owner.m_impl->playerConfigRuntime.audioOutputDeviceId.empty() ? "Audio output device reset." : "Audio output device set.");
	owner.logNotice(
		owner.m_impl->playerConfigRuntime.audioOutputDeviceId.empty()
			? "Audio output device reset."
			: ("Audio output device: " + owner.m_impl->playerConfigRuntime.audioOutputDeviceId + "."));
	return true;
}

int ofxVlc4::AudioComponent::getVolume() const {
	const AudioStateInfo info = getAudioStateInfo();
	return info.volumeKnown ? info.volume : owner.m_impl->audioRuntime.currentVolume.load(std::memory_order_acquire);
}

void ofxVlc4::AudioComponent::setVolume(int volume) {
	const int clampedVolume = ofClamp(volume, 0, 100);
	updateAudioStateFromVolumeEvent(clampedVolume);
	updateAudioStateFromMutedEvent(clampedVolume <= 0);
	applyCurrentVolumeToPlayer();
}

bool ofxVlc4::AudioComponent::isMuted() const {
	const AudioStateInfo info = getAudioStateInfo();
	return info.mutedKnown ? info.muted : owner.m_impl->audioRuntime.outputMuted.load(std::memory_order_acquire);
}

void ofxVlc4::AudioComponent::toggleMute() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	const bool targetMuted = !isMuted();
	libvlc_audio_set_mute(player, targetMuted ? 1 : 0);
	updateAudioStateFromMutedEvent(targetMuted);
	owner.logNotice(std::string("Mute ") + (targetMuted ? "enabled." : "disabled."));
}

bool ofxVlc4::AudioComponent::isEqualizerEnabled() const {
	return owner.m_impl->effectsRuntime.equalizerEnabled;
}

void ofxVlc4::AudioComponent::setEqualizerEnabled(bool enabled) {
	owner.m_impl->effectsRuntime.equalizerEnabled = enabled;
	applyOrQueueEqualizerSettings();
	owner.setStatus(std::string("Equalizer ") + (enabled ? "enabled." : "disabled."));
	owner.logNotice(std::string("Equalizer ") + (enabled ? "enabled." : "disabled."));
}

float ofxVlc4::AudioComponent::getEqualizerPreamp() const {
	return owner.m_impl->effectsRuntime.equalizerPreamp;
}

void ofxVlc4::AudioComponent::setEqualizerPreamp(float preamp) {
	owner.m_impl->effectsRuntime.equalizerEnabled = true;
	owner.m_impl->effectsRuntime.equalizerPreamp = ofClamp(preamp, -20.0f, 20.0f);
	owner.m_impl->effectsRuntime.currentEqualizerPresetIndex = -1;
	applyOrQueueEqualizerSettings();
}

int ofxVlc4::AudioComponent::getEqualizerBandCount() const {
	return static_cast<int>(owner.m_impl->effectsRuntime.equalizerBandAmps.size());
}

float ofxVlc4::AudioComponent::getEqualizerBandFrequency(int index) const {
	if (index < 0 || index >= static_cast<int>(owner.m_impl->effectsRuntime.equalizerBandAmps.size())) {
		return -1.0f;
	}

	return libvlc_audio_equalizer_get_band_frequency(static_cast<unsigned>(index));
}

float ofxVlc4::AudioComponent::getEqualizerBandAmp(int index) const {
	if (index < 0 || index >= static_cast<int>(owner.m_impl->effectsRuntime.equalizerBandAmps.size())) {
		return 0.0f;
	}

	return owner.m_impl->effectsRuntime.equalizerBandAmps[static_cast<size_t>(index)];
}

int ofxVlc4::AudioComponent::getEqualizerPresetCount() const {
	return static_cast<int>(libvlc_audio_equalizer_get_preset_count());
}

std::vector<std::string> ofxVlc4::AudioComponent::getEqualizerPresetNames() const {
	std::vector<std::string> presetNames;
	const unsigned presetCount = libvlc_audio_equalizer_get_preset_count();
	presetNames.reserve(presetCount);
	for (unsigned presetIndex = 0; presetIndex < presetCount; ++presetIndex) {
		const char * presetName = libvlc_audio_equalizer_get_preset_name(presetIndex);
		presetNames.push_back(trimWhitespace(presetName ? presetName : ""));
	}
	return presetNames;
}

ofxVlc4::EqualizerPresetInfo ofxVlc4::AudioComponent::getEqualizerPresetInfo(int index) const {
	EqualizerPresetInfo info;
	const unsigned presetCount = libvlc_audio_equalizer_get_preset_count();
	if (index < 0 || static_cast<unsigned>(index) >= presetCount) {
		return info;
	}

	libvlc_equalizer_t * preset = libvlc_audio_equalizer_new_from_preset(static_cast<unsigned>(index));
	if (!preset) {
		return info;
	}

	info.index = index;
	const char * presetNamePtr = libvlc_audio_equalizer_get_preset_name(static_cast<unsigned>(index));
	info.name = trimWhitespace(presetNamePtr ? presetNamePtr : "");
	info.preamp = ofClamp(libvlc_audio_equalizer_get_preamp(preset), -20.0f, 20.0f);
	info.bandAmps.resize(owner.m_impl->effectsRuntime.equalizerBandAmps.size(), 0.0f);
	for (unsigned bandIndex = 0; bandIndex < info.bandAmps.size(); ++bandIndex) {
		info.bandAmps[bandIndex] = ofClamp(
			libvlc_audio_equalizer_get_amp_at_index(preset, bandIndex),
			-20.0f,
			20.0f);
	}
	libvlc_audio_equalizer_release(preset);

	info.matchesCurrent =
		std::abs(info.preamp - owner.m_impl->effectsRuntime.equalizerPreamp) <= kEqualizerPresetMatchToleranceDb &&
		equalizerBandsMatch(info.bandAmps, owner.m_impl->effectsRuntime.equalizerBandAmps, kEqualizerPresetMatchToleranceDb);
	return info;
}

std::vector<ofxVlc4::EqualizerPresetInfo> ofxVlc4::AudioComponent::getEqualizerPresetInfos() const {
	std::vector<EqualizerPresetInfo> presets;
	const int presetCount = getEqualizerPresetCount();
	presets.reserve(std::max(0, presetCount));
	for (int presetIndex = 0; presetIndex < presetCount; ++presetIndex) {
		presets.push_back(getEqualizerPresetInfo(presetIndex));
	}
	return presets;
}

int ofxVlc4::AudioComponent::getCurrentEqualizerPresetIndex() const {
	return owner.m_impl->effectsRuntime.currentEqualizerPresetIndex;
}

int ofxVlc4::AudioComponent::findMatchingEqualizerPresetIndex(float toleranceDb) const {
	const float resolvedTolerance = std::max(0.0f, toleranceDb);
	const int presetCount = getEqualizerPresetCount();
	for (int presetIndex = 0; presetIndex < presetCount; ++presetIndex) {
		const EqualizerPresetInfo preset = getEqualizerPresetInfo(presetIndex);
		if (preset.index < 0) {
			continue;
		}
		if (std::abs(preset.preamp - owner.m_impl->effectsRuntime.equalizerPreamp) <= resolvedTolerance &&
			equalizerBandsMatch(preset.bandAmps, owner.m_impl->effectsRuntime.equalizerBandAmps, resolvedTolerance)) {
			return preset.index;
		}
	}
	return -1;
}

std::string ofxVlc4::AudioComponent::exportCurrentEqualizerPreset() const {
	std::ostringstream stream;
	const char * exportPresetNamePtr =
		owner.m_impl->effectsRuntime.currentEqualizerPresetIndex >= 0
			? libvlc_audio_equalizer_get_preset_name(static_cast<unsigned>(owner.m_impl->effectsRuntime.currentEqualizerPresetIndex))
			: nullptr;
	stream << "name=" << trimWhitespace(exportPresetNamePtr ? exportPresetNamePtr : "") << "\n";
	stream << "preamp=" << ofToString(owner.m_impl->effectsRuntime.equalizerPreamp, 3) << "\n";
	stream << "bands=";
	for (size_t bandIndex = 0; bandIndex < owner.m_impl->effectsRuntime.equalizerBandAmps.size(); ++bandIndex) {
		if (bandIndex > 0) {
			stream << ",";
		}
		stream << ofToString(owner.m_impl->effectsRuntime.equalizerBandAmps[bandIndex], 3);
	}
	return stream.str();
}

bool ofxVlc4::AudioComponent::importEqualizerPreset(const std::string & serializedPreset) {
	const std::string trimmedPreset = trimWhitespace(serializedPreset);
	if (trimmedPreset.empty()) {
		owner.setError("Equalizer preset text is empty.");
		return false;
	}

	const std::vector<std::string> presetNames = getEqualizerPresetNames();
	for (int presetIndex = 0; presetIndex < static_cast<int>(presetNames.size()); ++presetIndex) {
		if (trimWhitespace(presetNames[static_cast<size_t>(presetIndex)]) == trimmedPreset) {
			return applyEqualizerPreset(presetIndex);
		}
	}

	float importedPreamp = owner.m_impl->effectsRuntime.equalizerPreamp;
	std::vector<float> importedBandAmps = owner.m_impl->effectsRuntime.equalizerBandAmps;
	bool hasPreamp = false;
	bool hasBands = false;

	for (const std::string & line : splitSerializedPreset(trimmedPreset)) {
		const size_t separator = line.find('=');
		if (separator == std::string::npos) {
			continue;
		}

		const std::string key = ofToLower(trimWhitespace(line.substr(0, separator)));
		const std::string value = trimWhitespace(line.substr(separator + 1));
		if (key == "preamp") {
			importedPreamp = ofClamp(ofToFloat(value), -20.0f, 20.0f);
			hasPreamp = true;
		} else if (key == "bands") {
			std::vector<float> parsedBandAmps;
			std::stringstream bandStream(value);
			std::string token;
			while (std::getline(bandStream, token, ',')) {
				token = trimWhitespace(token);
				if (!token.empty()) {
					parsedBandAmps.push_back(ofClamp(ofToFloat(token), -20.0f, 20.0f));
				}
			}
			if (parsedBandAmps.size() == owner.m_impl->effectsRuntime.equalizerBandAmps.size()) {
				importedBandAmps = std::move(parsedBandAmps);
				hasBands = true;
			}
		}
	}

	if (!hasPreamp && !hasBands) {
		owner.setError("Equalizer preset text did not contain usable preset data.");
		return false;
	}

	owner.m_impl->effectsRuntime.equalizerEnabled = true;
	owner.m_impl->effectsRuntime.equalizerPreamp = importedPreamp;
	owner.m_impl->effectsRuntime.equalizerBandAmps = std::move(importedBandAmps);
	owner.m_impl->effectsRuntime.currentEqualizerPresetIndex = findMatchingEqualizerPresetIndex(kEqualizerPresetMatchToleranceDb);
	applyOrQueueEqualizerSettings();
	owner.setStatus("Equalizer preset imported.");
	owner.logNotice("Equalizer preset imported.");
	return true;
}

bool ofxVlc4::AudioComponent::applyEqualizerPreset(int index) {
	const unsigned presetCount = libvlc_audio_equalizer_get_preset_count();
	if (index < 0 || static_cast<unsigned>(index) >= presetCount) {
		owner.setError("Equalizer preset index out of range.");
		return false;
	}

	libvlc_equalizer_t * preset = libvlc_audio_equalizer_new_from_preset(static_cast<unsigned>(index));
	if (!preset) {
		owner.setError("Equalizer preset could not be created.");
		return false;
	}

	owner.m_impl->effectsRuntime.equalizerEnabled = true;
	owner.m_impl->effectsRuntime.equalizerPreamp = ofClamp(libvlc_audio_equalizer_get_preamp(preset), -20.0f, 20.0f);
	for (unsigned bandIndex = 0; bandIndex < owner.m_impl->effectsRuntime.equalizerBandAmps.size(); ++bandIndex) {
		owner.m_impl->effectsRuntime.equalizerBandAmps[bandIndex] = ofClamp(
			libvlc_audio_equalizer_get_amp_at_index(preset, bandIndex),
			-20.0f,
			20.0f);
	}
	owner.m_impl->effectsRuntime.currentEqualizerPresetIndex = index;
	libvlc_audio_equalizer_release(preset);

	applyOrQueueEqualizerSettings();

	const char * presetName = libvlc_audio_equalizer_get_preset_name(static_cast<unsigned>(index));
	const std::string label = trimWhitespace(presetName ? presetName : "");
	owner.setStatus("Equalizer preset applied: " + (label.empty() ? ofToString(index) : label));
	owner.logNotice("Equalizer preset applied: " + (label.empty() ? ofToString(index) : label));
	return true;
}

void ofxVlc4::AudioComponent::setEqualizerBandAmp(int index, float amp) {
	if (index < 0 || index >= static_cast<int>(owner.m_impl->effectsRuntime.equalizerBandAmps.size())) {
		return;
	}

	owner.m_impl->effectsRuntime.equalizerEnabled = true;
	owner.m_impl->effectsRuntime.equalizerBandAmps[static_cast<size_t>(index)] = ofClamp(amp, -20.0f, 20.0f);
	owner.m_impl->effectsRuntime.currentEqualizerPresetIndex = -1;
	applyOrQueueEqualizerSettings();
}

void ofxVlc4::AudioComponent::resetEqualizer() {
	owner.m_impl->effectsRuntime.equalizerEnabled = true;
	owner.m_impl->effectsRuntime.equalizerPreamp = Impl::kDefaultEqualizerPreampDb;
	std::fill(owner.m_impl->effectsRuntime.equalizerBandAmps.begin(), owner.m_impl->effectsRuntime.equalizerBandAmps.end(), 0.0f);
	owner.m_impl->effectsRuntime.currentEqualizerPresetIndex = -1;
	applyOrQueueEqualizerSettings();
	owner.setStatus("Equalizer reset.");
	owner.logNotice("Equalizer reset.");
}

std::vector<float> ofxVlc4::AudioComponent::getEqualizerSpectrumLevels(size_t pointCount) const {
	std::vector<float> levels(pointCount, 0.0f);
	if (levels.empty() || owner.m_impl->effectsRuntime.equalizerBandAmps.empty() || !owner.m_impl->playerConfigRuntime.audioCaptureEnabled || !owner.m_impl->audioRuntime.ready.load()) {
		clearSpectrumAnalysisCache();
		return levels;
	}

	const int rate = owner.m_impl->audioRuntime.sampleRate.load();
	const int channelCount = std::max(owner.m_impl->audioRuntime.channels.load(), 1);
	if (rate <= 0 || channelCount <= 0) {
		clearSpectrumAnalysisCache();
		return levels;
	}

	const size_t interleavedSampleCount = kSpectrumFftSize * static_cast<size_t>(channelCount);
	size_t copiedSamples = 0;
	uint64_t audioVersion = 0;
	const uint64_t nowMicros = ofGetElapsedTimeMicros();
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioMutex);
		audioVersion = owner.m_impl->audioBufferRuntime.ringBuffer.getVersion();
		if (spectrumCacheValid &&
			spectrumCachePointCount == pointCount &&
			spectrumCacheSampleRate == rate &&
			spectrumCacheChannelCount == channelCount) {
			if (spectrumCacheVersion == audioVersion) {
				return cachedSpectrumLevels;
			}
			if ((nowMicros - spectrumLastUpdateMicros) < kSpectrumUpdateIntervalMicros) {
				return cachedSpectrumLevels;
			}
		}

		if (owner.m_impl->audioBufferRuntime.ringBuffer.getNumReadableSamples() == 0) {
			clearSpectrumAnalysisCache();
			return levels;
		}

		spectrumInterleavedSamples.resize(interleavedSampleCount);
		copiedSamples = owner.m_impl->audioBufferRuntime.ringBuffer.peekLatest(spectrumInterleavedSamples.data(), interleavedSampleCount);
	}
	if (copiedSamples == 0) {
		clearSpectrumAnalysisCache();
		return levels;
	}

	ensureSpectrumWindowPrepared();
	spectrumFftInput.resize(kSpectrumFftSize);
	for (size_t frameIndex = 0; frameIndex < kSpectrumFftSize; ++frameIndex) {
		float monoSample = 0.0f;
		const size_t frameOffset = frameIndex * static_cast<size_t>(channelCount);
		for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
			monoSample += spectrumInterleavedSamples[frameOffset + static_cast<size_t>(channelIndex)];
		}
		monoSample /= static_cast<float>(channelCount);

		spectrumFftInput[frameIndex] = std::complex<float>(monoSample * spectrumWindow[frameIndex], 0.0f);
	}

	fftInPlace(spectrumFftInput);

	spectrumMagnitudes.assign((kSpectrumFftSize / 2u) + 1u, 0.0f);
	for (size_t binIndex = 1; binIndex < spectrumMagnitudes.size(); ++binIndex) {
		spectrumMagnitudes[binIndex] = std::abs(spectrumFftInput[binIndex]);
	}

	const float nyquist = static_cast<float>(rate) * 0.5f;
	const float minEqFrequency = std::max(getEqualizerBandFrequency(0), kSpectrumMinFrequencyHz);
	const float maxEqFrequency = std::min(
		nyquist,
		std::max(getEqualizerBandFrequency(static_cast<int>(owner.m_impl->effectsRuntime.equalizerBandAmps.size()) - 1), minEqFrequency * 2.0f));
	if (maxEqFrequency <= minEqFrequency) {
		clearSpectrumAnalysisCache();
		return levels;
	}

	const float amplitudeScale = spectrumWindowSum > 0.0f ? (2.0f / spectrumWindowSum) : 0.0f;
	spectrumPointDecibels.assign(levels.size(), kSpectrumBottomDb);
	for (size_t pointIndex = 0; pointIndex < levels.size(); ++pointIndex) {
		const float t = levels.size() <= 1
			? 0.0f
			: static_cast<float>(pointIndex) / static_cast<float>(levels.size() - 1);
		const float targetFrequency = minEqFrequency * std::pow(maxEqFrequency / minEqFrequency, t);
		const float halfWindow = 0.5f * kSpectrumWindowOctaves;
		const float lowFrequency = std::max(
			kSpectrumMinFrequencyHz,
			targetFrequency / std::pow(2.0f, halfWindow));
		const float highFrequency = std::min(
			nyquist,
			targetFrequency * std::pow(2.0f, halfWindow));

		int lowBin = static_cast<int>(std::floor((lowFrequency / static_cast<float>(rate)) * static_cast<float>(kSpectrumFftSize)));
		int highBin = static_cast<int>(std::ceil((highFrequency / static_cast<float>(rate)) * static_cast<float>(kSpectrumFftSize)));
		lowBin = std::clamp(lowBin, 1, static_cast<int>(spectrumMagnitudes.size()) - 1);
		highBin = std::clamp(highBin, lowBin, static_cast<int>(spectrumMagnitudes.size()) - 1);

		float powerSum = 0.0f;
		int sampleCount = 0;
		for (int binIndex = lowBin; binIndex <= highBin; ++binIndex) {
			const float amplitude = spectrumMagnitudes[static_cast<size_t>(binIndex)] * amplitudeScale;
			powerSum += amplitude * amplitude;
			++sampleCount;
		}

		const float rmsAmplitude = sampleCount > 0
			? std::sqrt(powerSum / static_cast<float>(sampleCount))
			: 0.0f;
		spectrumPointDecibels[pointIndex] = 20.0f * std::log10(rmsAmplitude + 1.0e-9f);
	}

	if (spectrumPointDecibels.size() >= 7) {
		spectrumSmoothedDecibels.assign(spectrumPointDecibels.size(), 0.0f);
		for (size_t i = 0; i < spectrumPointDecibels.size(); ++i) {
			const size_t i0 = (i >= 3) ? (i - 3) : 0;
			const size_t i1 = (i >= 2) ? (i - 2) : 0;
			const size_t i2 = (i >= 1) ? (i - 1) : 0;
			const size_t i4 = std::min(i + 1, spectrumPointDecibels.size() - 1);
			const size_t i5 = std::min(i + 2, spectrumPointDecibels.size() - 1);
			const size_t i6 = std::min(i + 3, spectrumPointDecibels.size() - 1);
			spectrumSmoothedDecibels[i] =
				(spectrumPointDecibels[i0] * 0.06f) +
				(spectrumPointDecibels[i1] * 0.12f) +
				(spectrumPointDecibels[i2] * 0.18f) +
				(spectrumPointDecibels[i]  * 0.28f) +
				(spectrumPointDecibels[i4] * 0.18f) +
				(spectrumPointDecibels[i5] * 0.12f) +
				(spectrumPointDecibels[i6] * 0.06f);
		}
		spectrumPointDecibels.swap(spectrumSmoothedDecibels);
	}

	if (owner.m_impl->analysisRuntime.smoothedSpectrumLevels.size() != levels.size()) {
		owner.m_impl->analysisRuntime.smoothedSpectrumLevels.assign(levels.size(), 0.0f);
	}
	cachedSpectrumLevels.resize(levels.size(), 0.0f);

	for (size_t pointIndex = 0; pointIndex < levels.size(); ++pointIndex) {
		const float normalized = ofMap(spectrumPointDecibels[pointIndex], kSpectrumBottomDb, kSpectrumTopDb, 0.0f, 1.0f, true);
		const float previous = owner.m_impl->analysisRuntime.smoothedSpectrumLevels[pointIndex];
		const float smoothing = normalized > previous ? kSpectrumAttack : kSpectrumRelease;
		const float smoothed = previous + ((normalized - previous) * smoothing);
		owner.m_impl->analysisRuntime.smoothedSpectrumLevels[pointIndex] = smoothed;
		cachedSpectrumLevels[pointIndex] = smoothed;
	}

	spectrumCacheValid = true;
	spectrumCacheVersion = audioVersion;
	spectrumCacheSampleRate = rate;
	spectrumCacheChannelCount = channelCount;
	spectrumCachePointCount = pointCount;
	spectrumLastUpdateMicros = nowMicros;
	return cachedSpectrumLevels;
}

void ofxVlc4::applyEqualizerSettings() {
	m_impl->subsystemRuntime.audioComponent->applyEqualizerSettings();
}

bool ofxVlc4::isEqualizerEnabled() const {
	return m_impl->subsystemRuntime.audioComponent->isEqualizerEnabled();
}

void ofxVlc4::setEqualizerEnabled(bool enabled) {
	m_impl->subsystemRuntime.audioComponent->setEqualizerEnabled(enabled);
}

float ofxVlc4::getEqualizerPreamp() const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerPreamp();
}

void ofxVlc4::setEqualizerPreamp(float preamp) {
	m_impl->subsystemRuntime.audioComponent->setEqualizerPreamp(preamp);
}

int ofxVlc4::getEqualizerBandCount() const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerBandCount();
}

float ofxVlc4::getEqualizerBandFrequency(int index) const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerBandFrequency(index);
}

float ofxVlc4::getEqualizerBandAmp(int index) const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerBandAmp(index);
}

int ofxVlc4::getEqualizerPresetCount() const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerPresetCount();
}

std::vector<std::string> ofxVlc4::getEqualizerPresetNames() const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerPresetNames();
}

ofxVlc4::EqualizerPresetInfo ofxVlc4::getEqualizerPresetInfo(int index) const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerPresetInfo(index);
}

std::vector<ofxVlc4::EqualizerPresetInfo> ofxVlc4::getEqualizerPresetInfos() const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerPresetInfos();
}

int ofxVlc4::getCurrentEqualizerPresetIndex() const {
	return m_impl->subsystemRuntime.audioComponent->getCurrentEqualizerPresetIndex();
}

int ofxVlc4::findMatchingEqualizerPresetIndex(float toleranceDb) const {
	return m_impl->subsystemRuntime.audioComponent->findMatchingEqualizerPresetIndex(toleranceDb);
}

std::string ofxVlc4::exportCurrentEqualizerPreset() const {
	return m_impl->subsystemRuntime.audioComponent->exportCurrentEqualizerPreset();
}

bool ofxVlc4::importEqualizerPreset(const std::string & serializedPreset) {
	return m_impl->subsystemRuntime.audioComponent->importEqualizerPreset(serializedPreset);
}

bool ofxVlc4::applyEqualizerPreset(int index) {
	return m_impl->subsystemRuntime.audioComponent->applyEqualizerPreset(index);
}

void ofxVlc4::setEqualizerBandAmp(int index, float amp) {
	m_impl->subsystemRuntime.audioComponent->setEqualizerBandAmp(index, amp);
}

void ofxVlc4::resetEqualizer() {
	m_impl->subsystemRuntime.audioComponent->resetEqualizer();
}

std::vector<float> ofxVlc4::getEqualizerSpectrumLevels(size_t pointCount) const {
	return m_impl->subsystemRuntime.audioComponent->getEqualizerSpectrumLevels(pointCount);
}

ofxVlc4::AudioMixMode ofxVlc4::AudioComponent::getAudioMixMode() const {
	return getAudioStateInfo().mixMode;
}

void ofxVlc4::AudioComponent::setAudioMixMode(AudioMixMode mode) {
	owner.m_impl->playerConfigRuntime.audioMixMode = mode;
	applyAudioMixMode();

	owner.setStatus(std::string("Audio mix mode set to ") + audioMixModeLabel(mode) + ".");
	owner.logNotice(std::string("Audio mix mode: ") + audioMixModeLabel(mode));
}

ofxVlc4::AudioStereoMode ofxVlc4::AudioComponent::getAudioStereoMode() const {
	return getAudioStateInfo().stereoMode;
}

void ofxVlc4::AudioComponent::setAudioStereoMode(AudioStereoMode mode) {
	owner.m_impl->playerConfigRuntime.audioStereoMode = mode;
	applyAudioStereoMode();

	owner.setStatus(std::string("Audio stereo mode set to ") + audioStereoModeLabel(mode) + ".");
	owner.logNotice(std::string("Audio stereo mode: ") + audioStereoModeLabel(mode));
}

float ofxVlc4::AudioComponent::getPlaybackRate() const {
	return owner.m_impl->playerConfigRuntime.playbackRate;
}

void ofxVlc4::AudioComponent::setPlaybackRate(float rate) {
	const float clampedRate = ofClamp(rate, 0.25f, 4.0f);
	if (std::abs(owner.m_impl->playerConfigRuntime.playbackRate - clampedRate) < 0.0001f) {
		return;
	}

	owner.m_impl->playerConfigRuntime.playbackRate = clampedRate;
	applyPlaybackRate();
	owner.setStatus("Playback rate set.");
	owner.logVerbose("Playback rate: " + ofToString(owner.m_impl->playerConfigRuntime.playbackRate, 2) + "x.");
}

int ofxVlc4::AudioComponent::getAudioDelayMs() const {
	return getAudioStateInfo().audioDelayMs;
}

void ofxVlc4::AudioComponent::setAudioDelayMs(int delayMs) {
	const int clampedDelayMs = ofClamp(delayMs, -5000, 5000);
	if (getAudioDelayMs() == clampedDelayMs) {
		return;
	}

	owner.m_impl->playerConfigRuntime.audioDelayUs = static_cast<int64_t>(clampedDelayMs) * 1000;
	applyAudioDelay();
	owner.setStatus("Audio delay set.");
	owner.logVerbose("Audio delay: " + ofToString(getAudioDelayMs()) + " ms.");
}

void ofxVlc4::AudioComponent::setAudioCaptureEnabled(bool enabled) {
	if (owner.m_impl->playerConfigRuntime.audioCaptureEnabled == enabled) {
		return;
	}

	if (owner.sessionInstance() || owner.sessionPlayer()) {
		owner.setError("setAudioCaptureEnabled() must be called before init(); reinitialize the player to change audio capture.");
		return;
	}

	owner.m_impl->playerConfigRuntime.audioCaptureEnabled = enabled;
	if (!enabled) {
		owner.m_impl->audioRuntime.ready.store(false);
		resetBuffer();
	}
}

bool ofxVlc4::AudioComponent::isAudioCaptureEnabled() const {
	return owner.m_impl->playerConfigRuntime.audioCaptureEnabled;
}

ofxVlc4::AudioCaptureSampleFormat ofxVlc4::AudioComponent::getAudioCaptureSampleFormat() const {
	return owner.m_impl->playerConfigRuntime.audioCaptureSampleFormat;
}

void ofxVlc4::AudioComponent::setAudioCaptureSampleFormat(AudioCaptureSampleFormat format) {
	owner.m_impl->playerConfigRuntime.audioCaptureSampleFormat = format;
	owner.setStatus(std::string("Audio callback format set to ") + audioCaptureSampleFormatLabel(format) + ".");
	owner.logNotice(std::string("Audio callback format: ") + audioCaptureSampleFormatLabel(format));
}

int ofxVlc4::AudioComponent::getAudioCaptureSampleRate() const {
	return owner.m_impl->playerConfigRuntime.audioCaptureSampleRate;
}

void ofxVlc4::AudioComponent::setAudioCaptureSampleRate(int rate) {
	owner.m_impl->playerConfigRuntime.audioCaptureSampleRate = normalizeAudioCaptureSampleRate(rate);
	owner.setStatus("Audio callback rate set to " + ofToString(owner.m_impl->playerConfigRuntime.audioCaptureSampleRate) + " Hz.");
	owner.logNotice("Audio callback rate: " + ofToString(owner.m_impl->playerConfigRuntime.audioCaptureSampleRate) + " Hz");
}

int ofxVlc4::AudioComponent::getAudioCaptureChannelCount() const {
	return owner.m_impl->playerConfigRuntime.audioCaptureChannelCount;
}

void ofxVlc4::AudioComponent::setAudioCaptureChannelCount(int channelCount) {
	owner.m_impl->playerConfigRuntime.audioCaptureChannelCount = normalizeAudioCaptureChannelCount(channelCount);
	owner.setStatus("Audio callback channels set to " + ofToString(owner.m_impl->playerConfigRuntime.audioCaptureChannelCount) + ".");
	owner.logNotice("Audio callback channels: " + ofToString(owner.m_impl->playerConfigRuntime.audioCaptureChannelCount));
}

double ofxVlc4::AudioComponent::getAudioCaptureBufferSeconds() const {
	return owner.m_impl->playerConfigRuntime.audioCaptureBufferSeconds;
}

void ofxVlc4::AudioComponent::setAudioCaptureBufferSeconds(double seconds) {
	owner.m_impl->playerConfigRuntime.audioCaptureBufferSeconds = normalizeAudioCaptureBufferSeconds(seconds);
	if (owner.m_impl->playerConfigRuntime.audioCaptureEnabled) {
		prepareRingBuffer();
		resetBuffer();
	}
	owner.setStatus("Audio callback buffer set to " + ofToString(owner.m_impl->playerConfigRuntime.audioCaptureBufferSeconds, 2) + " s.");
	owner.logNotice("Audio callback buffer: " + ofToString(owner.m_impl->playerConfigRuntime.audioCaptureBufferSeconds, 2) + " s");
}

void ofxVlc4::AudioComponent::resetAudioStateInfo() {
	owner.m_impl->audioRuntime.callbackCount.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.callbackFrameCount.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.callbackSampleCount.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.callbackTotalMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.callbackMaxMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.conversionTotalMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.conversionMaxMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.ringWriteTotalMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.ringWriteMaxMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.recorderTotalMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.recorderMaxMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.firstCallbackSteadyMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.lastCallbackSteadyMicros.store(0, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.pendingAudioCallbackWarning.store(false, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.pendingAudioCallbackWarningCode.store(0, std::memory_order_relaxed);

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio = {};
	owner.m_impl->stateCacheRuntime.audio.trackCount = 0;
	owner.m_impl->stateCacheRuntime.audio.tracksAvailable = false;
	owner.m_impl->stateCacheRuntime.audio.volume = owner.m_impl->audioRuntime.currentVolume.load(std::memory_order_acquire);
	owner.m_impl->stateCacheRuntime.audio.volumeKnown = false;
	owner.m_impl->stateCacheRuntime.audio.muted = owner.m_impl->audioRuntime.outputMuted.load(std::memory_order_acquire);
	owner.m_impl->stateCacheRuntime.audio.mutedKnown = false;
	owner.m_impl->stateCacheRuntime.audio.mixMode = owner.m_impl->playerConfigRuntime.audioMixMode;
	owner.m_impl->stateCacheRuntime.audio.stereoMode = owner.m_impl->playerConfigRuntime.audioStereoMode;
	owner.m_impl->stateCacheRuntime.audio.audioDelayMs = static_cast<int>(owner.m_impl->playerConfigRuntime.audioDelayUs / 1000);
}

void ofxVlc4::AudioComponent::updateAudioStateFromVolumeEvent(int volume) {
	const int clampedVolume = ofClamp(volume, 0, 100);
	owner.m_impl->audioRuntime.currentVolume.store(clampedVolume, std::memory_order_release);
	owner.m_impl->audioRuntime.outputVolume.store(static_cast<float>(clampedVolume) / 100.0f, std::memory_order_release);

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.volumeKnown = true;
	owner.m_impl->stateCacheRuntime.audio.volume = clampedVolume;
}

void ofxVlc4::AudioComponent::updateAudioStateFromMutedEvent(bool muted) {
	owner.m_impl->audioRuntime.outputMuted.store(muted, std::memory_order_release);

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.mutedKnown = true;
	owner.m_impl->stateCacheRuntime.audio.muted = muted;
}

void ofxVlc4::AudioComponent::updateAudioStateFromDeviceEvent(const std::string & deviceId) {
	owner.m_impl->playerConfigRuntime.audioOutputDeviceId = deviceId;

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.deviceKnown = true;
	owner.m_impl->stateCacheRuntime.audio.deviceId = deviceId;
}

void ofxVlc4::AudioComponent::updateAudioStateFromAudioPts(int64_t ptsUs, int64_t systemUs) {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.audioPtsAvailable = ptsUs >= 0;
	owner.m_impl->stateCacheRuntime.audio.audioPtsUs = ptsUs;
	owner.m_impl->stateCacheRuntime.audio.audioPtsSystemUs = systemUs;
}

void ofxVlc4::AudioComponent::clearAudioPtsState() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
	owner.m_impl->stateCacheRuntime.audio.audioPtsAvailable = false;
	owner.m_impl->stateCacheRuntime.audio.audioPtsUs = -1;
	owner.m_impl->stateCacheRuntime.audio.audioPtsSystemUs = 0;
}

ofxVlc4::AudioStateInfo ofxVlc4::AudioComponent::getAudioStateInfo() const {
	AudioStateInfo info;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
		info = owner.m_impl->stateCacheRuntime.audio;
	}

	info.ready = owner.m_impl->audioRuntime.ready.load();
	info.paused = playback().isAudioPauseSignaled();
	info.tracksAvailable = info.trackCount > 0;
	info.mixMode = owner.m_impl->playerConfigRuntime.audioMixMode;
	info.stereoMode = owner.m_impl->playerConfigRuntime.audioStereoMode;
	info.audioDelayMs = static_cast<int>(owner.m_impl->playerConfigRuntime.audioDelayUs / 1000);

	const uint64_t callbackCount = owner.m_impl->audioRuntime.callbackCount.load(std::memory_order_relaxed);
	if (callbackCount > 0) {
		AudioCallbackPerformanceInfo performance;
		performance.available = true;
		performance.callbackCount = callbackCount;
		performance.frameCount = owner.m_impl->audioRuntime.callbackFrameCount.load(std::memory_order_relaxed);
		performance.sampleCount = owner.m_impl->audioRuntime.callbackSampleCount.load(std::memory_order_relaxed);
		performance.maxCallbackMicros = owner.m_impl->audioRuntime.callbackMaxMicros.load(std::memory_order_relaxed);
		performance.maxConversionMicros = owner.m_impl->audioRuntime.conversionMaxMicros.load(std::memory_order_relaxed);
		performance.maxRingWriteMicros = owner.m_impl->audioRuntime.ringWriteMaxMicros.load(std::memory_order_relaxed);
		performance.maxRecorderMicros = owner.m_impl->audioRuntime.recorderMaxMicros.load(std::memory_order_relaxed);
		performance.averageFramesPerCallback =
			static_cast<double>(performance.frameCount) / static_cast<double>(callbackCount);
		performance.averageSamplesPerCallback =
			static_cast<double>(performance.sampleCount) / static_cast<double>(callbackCount);
		performance.averageCallbackMicros =
			static_cast<double>(owner.m_impl->audioRuntime.callbackTotalMicros.load(std::memory_order_relaxed)) / static_cast<double>(callbackCount);
		performance.averageConversionMicros =
			static_cast<double>(owner.m_impl->audioRuntime.conversionTotalMicros.load(std::memory_order_relaxed)) / static_cast<double>(callbackCount);
		performance.averageRingWriteMicros =
			static_cast<double>(owner.m_impl->audioRuntime.ringWriteTotalMicros.load(std::memory_order_relaxed)) / static_cast<double>(callbackCount);
		performance.averageRecorderMicros =
			static_cast<double>(owner.m_impl->audioRuntime.recorderTotalMicros.load(std::memory_order_relaxed)) / static_cast<double>(callbackCount);
		performance.averageOtherMicros = std::max(
			0.0,
			performance.averageCallbackMicros -
				(performance.averageConversionMicros + performance.averageRingWriteMicros + performance.averageRecorderMicros));

		const uint64_t firstMicros = owner.m_impl->audioRuntime.firstCallbackSteadyMicros.load(std::memory_order_relaxed);
		const uint64_t lastMicros = owner.m_impl->audioRuntime.lastCallbackSteadyMicros.load(std::memory_order_relaxed);
		if (lastMicros > firstMicros) {
			performance.callbackRateHz =
				(static_cast<double>(callbackCount) * 1000000.0) /
				static_cast<double>(lastMicros - firstMicros);
		}

		info.callbackPerformance = std::move(performance);
	}

	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		const int liveVolume = libvlc_audio_get_volume(player);
		if (liveVolume >= 0) {
			info.volumeKnown = true;
			info.volume = liveVolume;
		}

		const int liveMute = libvlc_audio_get_mute(player);
		if (liveMute >= 0) {
			info.mutedKnown = true;
			info.muted = liveMute != 0;
		}

		info.mixMode = fromLibvlcAudioMixMode(libvlc_audio_get_mixmode(player));
		info.stereoMode = fromLibvlcAudioStereoMode(libvlc_audio_get_stereomode(player));
		info.audioDelayMs = static_cast<int>(libvlc_audio_get_delay(player) / 1000);

		char * currentDevice = libvlc_audio_output_device_get(player);
		if (currentDevice) {
			info.deviceKnown = true;
			info.deviceId = currentDevice;
			libvlc_free(currentDevice);
		}
	}

	return info;
}

void ofxVlc4::AudioComponent::applySubtitleDelay() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_video_set_spu_delay(player, owner.m_impl->playerConfigRuntime.subtitleDelayUs) != 0) {
		owner.logWarning("Subtitle delay could not be applied.");
	}
}

void ofxVlc4::AudioComponent::applySubtitleTextScale() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_spu_text_scale(player, owner.m_impl->playerConfigRuntime.subtitleTextScale);
}

int ofxVlc4::AudioComponent::getSubtitleDelayMs() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return static_cast<int>(libvlc_video_get_spu_delay(player) / 1000);
	}
	return static_cast<int>(owner.m_impl->playerConfigRuntime.subtitleDelayUs / 1000);
}

void ofxVlc4::AudioComponent::setSubtitleDelayMs(int delayMs) {
	const int clampedDelayMs = ofClamp(delayMs, -5000, 5000);
	if (getSubtitleDelayMs() == clampedDelayMs) {
		return;
	}

	owner.m_impl->playerConfigRuntime.subtitleDelayUs = static_cast<int64_t>(clampedDelayMs) * 1000;
	applySubtitleDelay();
	media().refreshSubtitleStateInfo();
	owner.setStatus("Subtitle delay set.");
	owner.logVerbose("Subtitle delay: " + ofToString(getSubtitleDelayMs()) + " ms.");
}

float ofxVlc4::AudioComponent::getSubtitleTextScale() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return libvlc_video_get_spu_text_scale(player);
	}
	return owner.m_impl->playerConfigRuntime.subtitleTextScale;
}

void ofxVlc4::AudioComponent::setSubtitleTextScale(float scale) {
	const float clampedScale = ofClamp(scale, 0.1f, 5.0f);
	if (std::abs(owner.m_impl->playerConfigRuntime.subtitleTextScale - clampedScale) < 0.0001f) {
		return;
	}

	owner.m_impl->playerConfigRuntime.subtitleTextScale = clampedScale;
	applySubtitleTextScale();
	media().refreshSubtitleStateInfo();
	owner.setStatus("Subtitle text scale set.");
	owner.logVerbose("Subtitle text scale: " + ofToString(owner.m_impl->playerConfigRuntime.subtitleTextScale, 2) + "x.");
}

bool ofxVlc4::AudioComponent::audioIsReady() const {
	return owner.m_impl->audioRuntime.ready.load();
}

uint64_t ofxVlc4::AudioComponent::getAudioOverrunCount() const {
	return owner.m_impl->audioBufferRuntime.ringBuffer.getOverrunCount();
}

uint64_t ofxVlc4::AudioComponent::getAudioUnderrunCount() const {
	return owner.m_impl->audioBufferRuntime.ringBuffer.getUnderrunCount();
}

size_t ofxVlc4::AudioComponent::peekLatestAudioSamples(float * dst, size_t sampleCount) const {
	if (!dst || sampleCount == 0) {
		return 0;
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioMutex);
	return owner.m_impl->audioBufferRuntime.ringBuffer.peekLatest(dst, sampleCount);
}

void ofxVlc4::AudioComponent::readAudioIntoBuffer(ofSoundBuffer & buffer, float gain) {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioMutex);
	owner.m_impl->audioBufferRuntime.ringBuffer.readIntoBuffer(buffer, gain);
}

void ofxVlc4::AudioComponent::submitRecordedAudioSamples(const float * samples, size_t sampleCount) {
	if (!owner.m_impl->playerConfigRuntime.audioCaptureEnabled || !samples || sampleCount == 0) {
		return;
	}

	captureRecorderAudioSamples(samples, sampleCount);
}

void ofxVlc4::AudioComponent::audioPlay(const void * samples, unsigned int count, int64_t pts) {
	if (!owner.m_impl->playerConfigRuntime.audioCaptureEnabled || !samples || count == 0) {
		return;
	}

	const auto callbackStart = std::chrono::steady_clock::now();
	const uint64_t callbackStartMicros = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(callbackStart.time_since_epoch()).count());
	updateAudioStateFromAudioPts(pts, libvlc_clock());
	owner.m_impl->audioRuntime.ready.store(true);

	const size_t channelCount = static_cast<size_t>(std::max(1, owner.m_impl->audioRuntime.channels.load(std::memory_order_relaxed)));
	const size_t sampleCount = static_cast<size_t>(count) * channelCount;
	const float outputVolume = owner.m_impl->audioRuntime.outputMuted.load(std::memory_order_acquire)
		? 0.0f
		: owner.m_impl->audioRuntime.outputVolume.load(std::memory_order_acquire);
	uint64_t ringWriteMicros = 0;
	uint64_t recorderMicros = 0;
	const auto writeCapturedSamples = [&](const float * inputSamples, size_t inputSampleCount) {
		const auto ringWriteStart = std::chrono::steady_clock::now();
		owner.m_impl->audioBufferRuntime.ringBuffer.write(inputSamples, inputSampleCount);
		ringWriteMicros = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - ringWriteStart).count());

		if (owner.m_impl->audioBufferRuntime.ringBuffer.wasOverflowWarningIssued()) {
			owner.m_impl->audioRuntime.pendingAudioCallbackWarning.store(true, std::memory_order_relaxed);
			owner.m_impl->audioRuntime.pendingAudioCallbackWarningCode.store(1, std::memory_order_relaxed);
		}

		const auto recorderStart = std::chrono::steady_clock::now();
		captureRecorderAudioSamples(inputSamples, inputSampleCount);
		recorderMicros = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - recorderStart).count());
	};

	const AudioCaptureSampleFormat activeFormat =
		static_cast<AudioCaptureSampleFormat>(owner.m_impl->playerConfigRuntime.activeAudioCaptureSampleFormat.load(std::memory_order_relaxed));
	const bool applyVolume = std::abs(outputVolume - 1.0f) > 0.0001f;
	uint64_t conversionMicros = 0;
	if (activeFormat == ofxVlc4::AudioCaptureSampleFormat::Float32 && !applyVolume) {
		writeCapturedSamples(static_cast<const float *>(samples), sampleCount);
	} else {
		thread_local std::vector<float> convertedSamples;
		convertedSamples.resize(sampleCount);

		const auto conversionStart = std::chrono::steady_clock::now();
		switch (activeFormat) {
		case ofxVlc4::AudioCaptureSampleFormat::Signed16: {
			const int16_t * inputSamples = static_cast<const int16_t *>(samples);
			for (size_t i = 0; i < sampleCount; ++i) {
				convertedSamples[i] = static_cast<float>(inputSamples[i]) / 32768.0f;
			}
			break;
		}
		case ofxVlc4::AudioCaptureSampleFormat::Signed32: {
			const int32_t * inputSamples = static_cast<const int32_t *>(samples);
			for (size_t i = 0; i < sampleCount; ++i) {
				convertedSamples[i] = static_cast<float>(inputSamples[i] / 2147483648.0);
			}
			break;
		}
		case ofxVlc4::AudioCaptureSampleFormat::Float32:
		default: {
			const float * inputSamples = static_cast<const float *>(samples);
			std::copy(inputSamples, inputSamples + sampleCount, convertedSamples.begin());
			break;
		}
		}

		if (applyVolume) {
			for (size_t i = 0; i < sampleCount; ++i) {
				convertedSamples[i] *= outputVolume;
			}
		}
		conversionMicros = static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - conversionStart).count());

		writeCapturedSamples(convertedSamples.data(), sampleCount);
	}

	const uint64_t callbackMicros = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - callbackStart).count());
	owner.m_impl->audioRuntime.callbackCount.fetch_add(1, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.callbackFrameCount.fetch_add(static_cast<uint64_t>(count), std::memory_order_relaxed);
	owner.m_impl->audioRuntime.callbackSampleCount.fetch_add(static_cast<uint64_t>(sampleCount), std::memory_order_relaxed);
	owner.m_impl->audioRuntime.callbackTotalMicros.fetch_add(callbackMicros, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.conversionTotalMicros.fetch_add(conversionMicros, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.ringWriteTotalMicros.fetch_add(ringWriteMicros, std::memory_order_relaxed);
	owner.m_impl->audioRuntime.recorderTotalMicros.fetch_add(recorderMicros, std::memory_order_relaxed);
	updateAtomicMax(owner.m_impl->audioRuntime.callbackMaxMicros, callbackMicros);
	updateAtomicMax(owner.m_impl->audioRuntime.conversionMaxMicros, conversionMicros);
	updateAtomicMax(owner.m_impl->audioRuntime.ringWriteMaxMicros, ringWriteMicros);
	updateAtomicMax(owner.m_impl->audioRuntime.recorderMaxMicros, recorderMicros);

	uint64_t expectedFirstMicros = 0;
	owner.m_impl->audioRuntime.firstCallbackSteadyMicros.compare_exchange_strong(
		expectedFirstMicros,
		callbackStartMicros,
		std::memory_order_relaxed,
		std::memory_order_relaxed);
	owner.m_impl->audioRuntime.lastCallbackSteadyMicros.store(steadyMicrosNow(), std::memory_order_relaxed);
}

void ofxVlc4::AudioComponent::audioSetVolume(float volume, bool mute) {
	const float clampedVolume = std::max(0.0f, volume);
	owner.m_impl->audioRuntime.outputVolume.store(clampedVolume, std::memory_order_release);
	updateAudioStateFromVolumeEvent(static_cast<int>(std::round(clampedVolume * 100.0f)));
	updateAudioStateFromMutedEvent(mute);
}

void ofxVlc4::AudioComponent::audioPause(int64_t pts) {
	updateAudioStateFromAudioPts(pts, libvlc_clock());
	playback().setAudioPauseSignaled(true);
}

void ofxVlc4::AudioComponent::audioResume(int64_t pts) {
	updateAudioStateFromAudioPts(pts, libvlc_clock());
	playback().setAudioPauseSignaled(false);
}

void ofxVlc4::AudioComponent::audioFlush(int64_t pts) {
	updateAudioStateFromAudioPts(pts, libvlc_clock());
	playback().setAudioPauseSignaled(false);
	resetBuffer();
	clearAudioPtsState();
	resetRecorderCapturedAudio();
}

void ofxVlc4::AudioComponent::audioDrain() {
	playback().setAudioPauseSignaled(false);
	clearAudioPtsState();
}

void ofxVlc4::prepareAudioRingBuffer() {
	m_impl->subsystemRuntime.audioComponent->prepareRingBuffer();
}

void ofxVlc4::prepareStartupAudioResources() {
	m_impl->subsystemRuntime.audioComponent->prepareStartupAudioResources();
}

void ofxVlc4::resetAudioBuffer() {
	m_impl->subsystemRuntime.audioComponent->resetBuffer();
}

void ofxVlc4::resetAudioStateInfo() {
	m_impl->subsystemRuntime.audioComponent->resetAudioStateInfo();
}

void ofxVlc4::updateAudioStateFromVolumeEvent(int volume) {
	m_impl->subsystemRuntime.audioComponent->updateAudioStateFromVolumeEvent(volume);
}

void ofxVlc4::updateAudioStateFromMutedEvent(bool muted) {
	m_impl->subsystemRuntime.audioComponent->updateAudioStateFromMutedEvent(muted);
}

void ofxVlc4::updateAudioStateFromDeviceEvent(const std::string & deviceId) {
	m_impl->subsystemRuntime.audioComponent->updateAudioStateFromDeviceEvent(deviceId);
}

void ofxVlc4::updateAudioStateFromAudioPts(int64_t ptsUs, int64_t systemUs) {
	m_impl->subsystemRuntime.audioComponent->updateAudioStateFromAudioPts(ptsUs, systemUs);
}

void ofxVlc4::clearAudioPtsState() {
	m_impl->subsystemRuntime.audioComponent->clearAudioPtsState();
}

ofxVlc4::AudioStateInfo ofxVlc4::getAudioStateInfo() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioStateInfo();
}

void ofxVlc4::applyCurrentVolumeToPlayer() {
	m_impl->subsystemRuntime.audioComponent->applyCurrentVolumeToPlayer();
}

void ofxVlc4::applyAudioOutputModule() {
	m_impl->subsystemRuntime.audioComponent->applyAudioOutputModule();
}

void ofxVlc4::applyAudioOutputDevice() {
	m_impl->subsystemRuntime.audioComponent->applyAudioOutputDevice();
}

void ofxVlc4::applyAudioStereoMode() {
	m_impl->subsystemRuntime.audioComponent->applyAudioStereoMode();
}

void ofxVlc4::applyAudioMixMode() {
	m_impl->subsystemRuntime.audioComponent->applyAudioMixMode();
}

void ofxVlc4::applyPlaybackRate() {
	m_impl->subsystemRuntime.audioComponent->applyPlaybackRate();
}

void ofxVlc4::applyAudioDelay() {
	m_impl->subsystemRuntime.audioComponent->applyAudioDelay();
}

void ofxVlc4::applySubtitleDelay() {
	m_impl->subsystemRuntime.audioComponent->applySubtitleDelay();
}

void ofxVlc4::applySubtitleTextScale() {
	m_impl->subsystemRuntime.audioComponent->applySubtitleTextScale();
}

ofxVlc4::AudioMixMode ofxVlc4::getAudioMixMode() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioMixMode();
}

void ofxVlc4::setAudioMixMode(AudioMixMode mode) {
	m_impl->subsystemRuntime.audioComponent->setAudioMixMode(mode);
}

ofxVlc4::AudioStereoMode ofxVlc4::getAudioStereoMode() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioStereoMode();
}

void ofxVlc4::setAudioStereoMode(AudioStereoMode mode) {
	m_impl->subsystemRuntime.audioComponent->setAudioStereoMode(mode);
}

ofxVlc4::AudioCaptureSampleFormat ofxVlc4::getAudioCaptureSampleFormat() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioCaptureSampleFormat();
}

void ofxVlc4::setAudioCaptureSampleFormat(AudioCaptureSampleFormat format) {
	m_impl->subsystemRuntime.audioComponent->setAudioCaptureSampleFormat(format);
}

int ofxVlc4::getAudioCaptureSampleRate() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioCaptureSampleRate();
}

void ofxVlc4::setAudioCaptureSampleRate(int rate) {
	m_impl->subsystemRuntime.audioComponent->setAudioCaptureSampleRate(rate);
}

int ofxVlc4::getAudioCaptureChannelCount() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioCaptureChannelCount();
}

void ofxVlc4::setAudioCaptureChannelCount(int channelCount) {
	m_impl->subsystemRuntime.audioComponent->setAudioCaptureChannelCount(channelCount);
}

double ofxVlc4::getAudioCaptureBufferSeconds() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioCaptureBufferSeconds();
}

void ofxVlc4::setAudioCaptureBufferSeconds(double seconds) {
	m_impl->subsystemRuntime.audioComponent->setAudioCaptureBufferSeconds(seconds);
}

std::vector<ofxVlc4::AudioOutputModuleInfo> ofxVlc4::getAudioOutputModules() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioOutputModules();
}

std::vector<ofxVlc4::AudioFilterInfo> ofxVlc4::getAudioFilters() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioFilters();
}

std::string ofxVlc4::getAudioFilterChain() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioFilterChain();
}

void ofxVlc4::setAudioFilterChain(const std::string & filterChain) {
	m_impl->subsystemRuntime.audioComponent->setAudioFilterChain(filterChain);
}

std::vector<std::string> ofxVlc4::getAudioFilterChainEntries() const {
	return parseFilterChainEntries(getAudioFilterChain());
}

void ofxVlc4::setAudioFilters(const std::vector<std::string> & filters) {
	setAudioFilterChain(joinFilterChainEntries(filters));
}

bool ofxVlc4::hasAudioFilter(const std::string & filterName) const {
	const std::string target = trimWhitespace(filterName);
	if (target.empty()) {
		return false;
	}
	const std::vector<std::string> filters = getAudioFilterChainEntries();
	return std::find(filters.begin(), filters.end(), target) != filters.end();
}

bool ofxVlc4::addAudioFilter(const std::string & filterName) {
	const std::string target = trimWhitespace(filterName);
	if (target.empty()) {
		return false;
	}
	std::vector<std::string> filters = getAudioFilterChainEntries();
	if (std::find(filters.begin(), filters.end(), target) != filters.end()) {
		return false;
	}
	filters.push_back(target);
	setAudioFilters(filters);
	return true;
}

bool ofxVlc4::removeAudioFilter(const std::string & filterName) {
	const std::string target = trimWhitespace(filterName);
	if (target.empty()) {
		return false;
	}
	std::vector<std::string> filters = getAudioFilterChainEntries();
	auto it = std::remove(filters.begin(), filters.end(), target);
	if (it == filters.end()) {
		return false;
	}
	filters.erase(it, filters.end());
	setAudioFilters(filters);
	return true;
}

bool ofxVlc4::toggleAudioFilter(const std::string & filterName) {
	return hasAudioFilter(filterName) ? removeAudioFilter(filterName) : addAudioFilter(filterName);
}

std::string ofxVlc4::getSelectedAudioOutputModuleName() const {
	return m_impl->subsystemRuntime.audioComponent->getSelectedAudioOutputModuleName();
}

bool ofxVlc4::selectAudioOutputModule(const std::string & moduleName) {
	return m_impl->subsystemRuntime.audioComponent->selectAudioOutputModule(moduleName);
}

float ofxVlc4::getPlaybackRate() const {
	return m_impl->subsystemRuntime.audioComponent->getPlaybackRate();
}

void ofxVlc4::setPlaybackRate(float rate) {
	m_impl->subsystemRuntime.audioComponent->setPlaybackRate(rate);
}

int ofxVlc4::getAudioDelayMs() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioDelayMs();
}

void ofxVlc4::setAudioDelayMs(int delayMs) {
	m_impl->subsystemRuntime.audioComponent->setAudioDelayMs(delayMs);
}

int ofxVlc4::getSubtitleDelayMs() const {
	return m_impl->subsystemRuntime.audioComponent->getSubtitleDelayMs();
}

void ofxVlc4::setSubtitleDelayMs(int delayMs) {
	m_impl->subsystemRuntime.audioComponent->setSubtitleDelayMs(delayMs);
}

float ofxVlc4::getSubtitleTextScale() const {
	return m_impl->subsystemRuntime.audioComponent->getSubtitleTextScale();
}

void ofxVlc4::setSubtitleTextScale(float scale) {
	m_impl->subsystemRuntime.audioComponent->setSubtitleTextScale(scale);
}

std::vector<ofxVlc4::AudioOutputDeviceInfo> ofxVlc4::getAudioOutputDevices() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioOutputDevices();
}

std::string ofxVlc4::getSelectedAudioOutputDeviceId() const {
	return m_impl->subsystemRuntime.audioComponent->getSelectedAudioOutputDeviceId();
}

bool ofxVlc4::selectAudioOutputDevice(const std::string & deviceId) {
	return m_impl->subsystemRuntime.audioComponent->selectAudioOutputDevice(deviceId);
}

int ofxVlc4::getVolume() const {
	return m_impl->subsystemRuntime.audioComponent->getVolume();
}

void ofxVlc4::setVolume(int volume) {
	m_impl->subsystemRuntime.audioComponent->setVolume(volume);
}

bool ofxVlc4::isMuted() const {
	return m_impl->subsystemRuntime.audioComponent->isMuted();
}

void ofxVlc4::toggleMute() {
	m_impl->subsystemRuntime.audioComponent->toggleMute();
}

void ofxVlc4::readAudioIntoBuffer(ofSoundBuffer & buffer, float gain) {
	m_impl->subsystemRuntime.audioComponent->readAudioIntoBuffer(buffer, gain);
}

void ofxVlc4::audioPlay(void * data, const void * samples, unsigned int count, int64_t pts) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) return;
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) return;
	scope.get()->m_impl->subsystemRuntime.audioComponent->audioPlay(samples, count, pts);
}

void ofxVlc4::audioSetVolume(void * data, float volume, bool mute) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) return;
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) return;
	scope.get()->m_impl->subsystemRuntime.audioComponent->audioSetVolume(volume, mute);
}

void ofxVlc4::audioPause(void * data, int64_t pts) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) return;
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) return;
	scope.get()->m_impl->subsystemRuntime.audioComponent->audioPause(pts);
}

void ofxVlc4::audioResume(void * data, int64_t pts) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) return;
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) return;
	scope.get()->m_impl->subsystemRuntime.audioComponent->audioResume(pts);
}

void ofxVlc4::audioFlush(void * data, int64_t pts) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) return;
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) return;
	scope.get()->m_impl->subsystemRuntime.audioComponent->audioFlush(pts);
}

void ofxVlc4::audioDrain(void * data) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) return;
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) return;
	scope.get()->m_impl->subsystemRuntime.audioComponent->audioDrain();
}

bool ofxVlc4::audioIsReady() const {
	return m_impl->subsystemRuntime.audioComponent->audioIsReady();
}

uint64_t ofxVlc4::getAudioOverrunCount() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioOverrunCount();
}

uint64_t ofxVlc4::getAudioUnderrunCount() const {
	return m_impl->subsystemRuntime.audioComponent->getAudioUnderrunCount();
}

size_t ofxVlc4::peekLatestAudioSamples(float * dst, size_t sampleCount) const {
	return m_impl->subsystemRuntime.audioComponent->peekLatestAudioSamples(dst, sampleCount);
}
