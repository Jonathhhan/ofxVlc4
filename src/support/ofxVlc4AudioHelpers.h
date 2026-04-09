#pragma once

// ---------------------------------------------------------------------------
// Pure audio helper functions extracted from ofxVlc4Audio.cpp for testability.
// These functions have no dependencies on OF, GLFW, or VLC at runtime.
// The anonymous-namespace originals in ofxVlc4Audio.cpp call these directly.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace ofxVlc4AudioHelpers {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double kBufferedAudioSeconds = 0.75;
static constexpr double kMinBufferedAudioSeconds = 0.05;
static constexpr double kMaxBufferedAudioSeconds = 5.0;
static constexpr float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// Audio capture sample format label/code
// ---------------------------------------------------------------------------

enum class AudioCaptureSampleFormat {
	Float32 = 0,
	Signed16,
	Signed32
};

inline const char * audioCaptureSampleFormatLabel(AudioCaptureSampleFormat format) {
	switch (format) {
	case AudioCaptureSampleFormat::Signed16:
		return "s16";
	case AudioCaptureSampleFormat::Signed32:
		return "s32";
	case AudioCaptureSampleFormat::Float32:
	default:
		return "float32";
	}
}

inline const char * audioCaptureSampleFormatCode(AudioCaptureSampleFormat format) {
	switch (format) {
	case AudioCaptureSampleFormat::Signed16:
		return "S16N";
	case AudioCaptureSampleFormat::Signed32:
		return "S32N";
	case AudioCaptureSampleFormat::Float32:
	default:
		return "FL32";
	}
}

// ---------------------------------------------------------------------------
// Audio capture normalization
// ---------------------------------------------------------------------------

inline int normalizeAudioCaptureSampleRate(int rate) {
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

inline int normalizeAudioCaptureChannelCount(int channels) {
	return channels <= 1 ? 1 : 2;
}

inline double normalizeAudioCaptureBufferSeconds(double seconds) {
	if (!std::isfinite(seconds)) {
		return kBufferedAudioSeconds;
	}
	return std::clamp(seconds, kMinBufferedAudioSeconds, kMaxBufferedAudioSeconds);
}

// ---------------------------------------------------------------------------
// Audio mix mode labels
// ---------------------------------------------------------------------------

enum class AudioMixMode {
	Auto = 0,
	Stereo,
	Binaural,
	Surround4_0,
	Surround5_1,
	Surround7_1
};

inline const char * audioMixModeLabel(AudioMixMode mode) {
	switch (mode) {
	case AudioMixMode::Stereo:
		return "stereo";
	case AudioMixMode::Binaural:
		return "binaural";
	case AudioMixMode::Surround4_0:
		return "4.0";
	case AudioMixMode::Surround5_1:
		return "5.1";
	case AudioMixMode::Surround7_1:
		return "7.1";
	case AudioMixMode::Auto:
	default:
		return "auto";
	}
}

// ---------------------------------------------------------------------------
// Audio stereo mode labels
// ---------------------------------------------------------------------------

enum class AudioStereoMode {
	Auto = 0,
	Stereo,
	ReverseStereo,
	Left,
	Right,
	DolbySurround,
	Mono
};

inline const char * audioStereoModeLabel(AudioStereoMode mode) {
	switch (mode) {
	case AudioStereoMode::Stereo:
		return "stereo";
	case AudioStereoMode::ReverseStereo:
		return "reverse stereo";
	case AudioStereoMode::Left:
		return "left";
	case AudioStereoMode::Right:
		return "right";
	case AudioStereoMode::DolbySurround:
		return "dolby surround";
	case AudioStereoMode::Mono:
		return "mono";
	case AudioStereoMode::Auto:
	default:
		return "auto";
	}
}

// ---------------------------------------------------------------------------
// Equalizer preset serialization
// ---------------------------------------------------------------------------

inline std::vector<std::string> splitSerializedPreset(const std::string & input) {
	std::vector<std::string> lines;
	std::string normalized = input;
	std::replace(normalized.begin(), normalized.end(), ';', '\n');
	std::stringstream stream(normalized);
	std::string line;
	while (std::getline(stream, line)) {
		// Trim whitespace (local version to avoid dependency on ofxVlc4Utils).
		auto begin = line.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			continue;
		}
		auto end = line.find_last_not_of(" \t\r\n");
		lines.push_back(line.substr(begin, end - begin + 1));
	}
	return lines;
}

inline bool equalizerBandsMatch(
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

// ---------------------------------------------------------------------------
// Bit reversal and FFT
// ---------------------------------------------------------------------------

inline size_t reverseBits(size_t value, unsigned int bitCount) {
	size_t reversed = 0;
	for (unsigned int i = 0; i < bitCount; ++i) {
		reversed = (reversed << 1) | (value & 1u);
		value >>= 1u;
	}
	return reversed;
}

inline void fftInPlace(std::vector<std::complex<float>> & values) {
	const size_t size = values.size();
	if (size <= 1) {
		return;
	}
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

// ---------------------------------------------------------------------------
// Atomic max helper
// ---------------------------------------------------------------------------

inline void updateAtomicMax(std::atomic<uint64_t> & target, uint64_t value) {
	uint64_t current = target.load(std::memory_order_relaxed);
	while (current < value &&
		!target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
	}
}

} // namespace ofxVlc4AudioHelpers
