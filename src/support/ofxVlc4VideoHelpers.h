#pragma once

// ---------------------------------------------------------------------------
// Pure video helper functions extracted from ofxVlc4Video.cpp for testability.
// These functions have no dependencies on OF, GLFW, or VLC at runtime.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <string>
#include <utility>

namespace ofxVlc4VideoHelpers {

// ---------------------------------------------------------------------------
// Deinterlace mode
// ---------------------------------------------------------------------------

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

inline const char * videoDeinterlaceModeLabel(VideoDeinterlaceMode mode) {
	switch (mode) {
	case VideoDeinterlaceMode::Off:
		return "off";
	case VideoDeinterlaceMode::Blend:
		return "blend";
	case VideoDeinterlaceMode::Bob:
		return "bob";
	case VideoDeinterlaceMode::Linear:
		return "linear";
	case VideoDeinterlaceMode::X:
		return "x";
	case VideoDeinterlaceMode::Yadif:
		return "yadif";
	case VideoDeinterlaceMode::Yadif2x:
		return "yadif2x";
	case VideoDeinterlaceMode::Phosphor:
		return "phosphor";
	case VideoDeinterlaceMode::Ivtc:
		return "ivtc";
	case VideoDeinterlaceMode::Auto:
	default:
		return "auto";
	}
}

inline int videoDeinterlaceState(VideoDeinterlaceMode mode) {
	switch (mode) {
	case VideoDeinterlaceMode::Auto:
		return -1;
	case VideoDeinterlaceMode::Off:
		return 0;
	default:
		return 1;
	}
}

inline const char * videoDeinterlaceFilterName(VideoDeinterlaceMode mode) {
	switch (mode) {
	case VideoDeinterlaceMode::Blend:
		return "blend";
	case VideoDeinterlaceMode::Bob:
		return "bob";
	case VideoDeinterlaceMode::Linear:
		return "linear";
	case VideoDeinterlaceMode::X:
		return "x";
	case VideoDeinterlaceMode::Yadif:
		return "yadif";
	case VideoDeinterlaceMode::Yadif2x:
		return "yadif2x";
	case VideoDeinterlaceMode::Phosphor:
		return "phosphor";
	case VideoDeinterlaceMode::Ivtc:
		return "ivtc";
	case VideoDeinterlaceMode::Auto:
	case VideoDeinterlaceMode::Off:
	default:
		return nullptr;
	}
}

// ---------------------------------------------------------------------------
// Aspect ratio
// ---------------------------------------------------------------------------

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

inline const char * videoAspectRatioValue(VideoAspectRatioMode mode) {
	switch (mode) {
	case VideoAspectRatioMode::Fill:
		return "fill";
	case VideoAspectRatioMode::Ratio16_9:
		return "16:9";
	case VideoAspectRatioMode::Ratio16_10:
		return "16:10";
	case VideoAspectRatioMode::Ratio4_3:
		return "4:3";
	case VideoAspectRatioMode::Ratio1_1:
		return "1:1";
	case VideoAspectRatioMode::Ratio21_9:
		return "21:9";
	case VideoAspectRatioMode::Ratio235_1:
		return "235:100";
	case VideoAspectRatioMode::Default:
	default:
		return nullptr;
	}
}

inline const char * videoAspectRatioLabel(VideoAspectRatioMode mode) {
	switch (mode) {
	case VideoAspectRatioMode::Fill:
		return "fill";
	case VideoAspectRatioMode::Ratio16_9:
		return "16:9";
	case VideoAspectRatioMode::Ratio16_10:
		return "16:10";
	case VideoAspectRatioMode::Ratio4_3:
		return "4:3";
	case VideoAspectRatioMode::Ratio1_1:
		return "1:1";
	case VideoAspectRatioMode::Ratio21_9:
		return "21:9";
	case VideoAspectRatioMode::Ratio235_1:
		return "2.35:1";
	case VideoAspectRatioMode::Default:
	default:
		return "default";
	}
}

// ---------------------------------------------------------------------------
// Crop mode
// ---------------------------------------------------------------------------

enum class VideoCropMode {
	None = 0,
	Ratio16_9,
	Ratio16_10,
	Ratio4_3,
	Ratio1_1,
	Ratio21_9,
	Ratio235_1
};

inline std::pair<unsigned, unsigned> videoCropRatio(VideoCropMode mode) {
	switch (mode) {
	case VideoCropMode::Ratio16_9:
		return { 16u, 9u };
	case VideoCropMode::Ratio16_10:
		return { 16u, 10u };
	case VideoCropMode::Ratio4_3:
		return { 4u, 3u };
	case VideoCropMode::Ratio1_1:
		return { 1u, 1u };
	case VideoCropMode::Ratio21_9:
		return { 21u, 9u };
	case VideoCropMode::Ratio235_1:
		return { 235u, 100u };
	case VideoCropMode::None:
	default:
		return { 0u, 0u };
	}
}

inline const char * videoCropLabel(VideoCropMode mode) {
	switch (mode) {
	case VideoCropMode::Ratio16_9:
		return "16:9";
	case VideoCropMode::Ratio16_10:
		return "16:10";
	case VideoCropMode::Ratio4_3:
		return "4:3";
	case VideoCropMode::Ratio1_1:
		return "1:1";
	case VideoCropMode::Ratio21_9:
		return "21:9";
	case VideoCropMode::Ratio235_1:
		return "2.35:1";
	case VideoCropMode::None:
	default:
		return "none";
	}
}

// ---------------------------------------------------------------------------
// Video adjustment engine
// ---------------------------------------------------------------------------

enum class VideoAdjustmentEngine {
	Auto = 0,
	LibVlc,
	Shader
};

inline const char * videoAdjustmentEngineLabel(VideoAdjustmentEngine engine) {
	switch (engine) {
	case VideoAdjustmentEngine::LibVlc:
		return "libVLC";
	case VideoAdjustmentEngine::Shader:
		return "shader";
	case VideoAdjustmentEngine::Auto:
	default:
		return "auto";
	}
}

// ---------------------------------------------------------------------------
// Video output backend
// ---------------------------------------------------------------------------

enum class VideoOutputBackend {
	Texture = 0,
	NativeWindow,
	D3D11Metadata
};

inline const char * videoOutputBackendLabel(VideoOutputBackend backend) {
	switch (backend) {
	case VideoOutputBackend::NativeWindow:
		return "Native Window";
	case VideoOutputBackend::D3D11Metadata:
		return "D3D11 HDR Metadata";
	case VideoOutputBackend::Texture:
	default:
		return "Texture";
	}
}

// ---------------------------------------------------------------------------
// Preferred decoder device
// ---------------------------------------------------------------------------

enum class PreferredDecoderDevice {
	Any = 0,
	D3D11,
	DXVA2,
	Nvdec,
	None
};

inline const char * preferredDecoderDeviceLabel(PreferredDecoderDevice device) {
	switch (device) {
	case PreferredDecoderDevice::D3D11:
		return "D3D11";
	case PreferredDecoderDevice::DXVA2:
		return "DXVA2";
	case PreferredDecoderDevice::Nvdec:
		return "NVDEC";
	case PreferredDecoderDevice::None:
		return "None";
	case PreferredDecoderDevice::Any:
	default:
		return "Auto";
	}
}

// ---------------------------------------------------------------------------
// Packed RGB color clamping
// ---------------------------------------------------------------------------

inline int clampPackedRgbColor(int color) {
	if (color < 0x000000) return 0x000000;
	if (color > 0xFFFFFF) return 0xFFFFFF;
	return color;
}

// ---------------------------------------------------------------------------
// Visible video source size calculation
// ---------------------------------------------------------------------------

struct SimpleVideoSizeInfo {
	unsigned sourceWidth = 0;
	unsigned sourceHeight = 0;
	unsigned renderWidth = 0;
	unsigned renderHeight = 0;
};

inline std::pair<unsigned, unsigned> visibleVideoSourceSize(const SimpleVideoSizeInfo & state) {
	return {
		std::min(state.sourceWidth, state.renderWidth),
		std::min(state.sourceHeight, state.renderHeight)
	};
}

// ---------------------------------------------------------------------------
// Playback mode string conversion
// ---------------------------------------------------------------------------

enum class PlaybackMode {
	Default,
	Repeat,
	Loop
};

inline PlaybackMode playbackModeFromString(const std::string & mode) {
	// Simple lowercase conversion.
	std::string normalized = mode;
	for (char & ch : normalized) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}
	// Trim whitespace.
	auto begin = normalized.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos) {
		return PlaybackMode::Default;
	}
	auto end = normalized.find_last_not_of(" \t\r\n");
	normalized = normalized.substr(begin, end - begin + 1);

	if (normalized == "repeat") {
		return PlaybackMode::Repeat;
	}
	if (normalized == "loop") {
		return PlaybackMode::Loop;
	}
	return PlaybackMode::Default;
}

inline std::string playbackModeToString(PlaybackMode mode) {
	switch (mode) {
	case PlaybackMode::Repeat:
		return "repeat";
	case PlaybackMode::Loop:
		return "loop";
	case PlaybackMode::Default:
	default:
		return "default";
	}
}

} // namespace ofxVlc4VideoHelpers
