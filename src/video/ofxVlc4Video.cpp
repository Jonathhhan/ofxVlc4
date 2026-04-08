#include "ofxVlc4.h"
#include "ofxVlc4Video.h"
#include "media/ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "support/ofxVlc4Utils.h"

#ifdef TARGET_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <d3d11.h>
#include "GLFW/glfw3native.h"
#pragma comment(lib, "d3d11.lib")
#elif defined(TARGET_OSX)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <objc/message.h>
#include <objc/runtime.h>
#include "GLFW/glfw3native.h"
#elif defined(TARGET_LINUX)
#define GLFW_EXPOSE_NATIVE_X11
#include "GLFW/glfw3native.h"
#endif

#include <algorithm>
#include <fstream>
#include <sstream>

using ofxVlc4Utils::clearAllocatedFbo;
using ofxVlc4Utils::joinFilterChainEntries;
using ofxVlc4Utils::normalizeOptionalPath;
using ofxVlc4Utils::parseFilterChainEntries;
using ofxVlc4Utils::setInputHandlingEnabled;
using ofxVlc4Utils::trimWhitespace;

namespace {
libvlc_video_projection_t toLibvlcProjectionMode(ofxVlc4::VideoProjectionMode mode) {
	switch (mode) {
	case ofxVlc4::VideoProjectionMode::Rectangular:
		return libvlc_video_projection_rectangular;
	case ofxVlc4::VideoProjectionMode::Equirectangular:
		return libvlc_video_projection_equirectangular;
	case ofxVlc4::VideoProjectionMode::CubemapStandard:
		return libvlc_video_projection_cubemap_layout_standard;
	case ofxVlc4::VideoProjectionMode::Auto:
	default:
		return libvlc_video_projection_rectangular;
	}
}

libvlc_video_stereo_mode_t toLibvlcStereoMode(ofxVlc4::VideoStereoMode mode) {
	switch (mode) {
	case ofxVlc4::VideoStereoMode::Stereo:
		return libvlc_VideoStereoStereo;
	case ofxVlc4::VideoStereoMode::LeftEye:
		return libvlc_VideoStereoLeftEye;
	case ofxVlc4::VideoStereoMode::RightEye:
		return libvlc_VideoStereoRightEye;
	case ofxVlc4::VideoStereoMode::SideBySide:
		return libvlc_VideoStereoSideBySide;
	case ofxVlc4::VideoStereoMode::Auto:
	default:
		return libvlc_VideoStereoAuto;
	}
}

libvlc_video_fit_mode_t toLibvlcVideoFitMode(ofxVlc4::VideoDisplayFitMode mode) {
	switch (mode) {
	case ofxVlc4::VideoDisplayFitMode::Larger:
		return libvlc_video_fit_larger;
	case ofxVlc4::VideoDisplayFitMode::Width:
		return libvlc_video_fit_width;
	case ofxVlc4::VideoDisplayFitMode::Height:
		return libvlc_video_fit_height;
	case ofxVlc4::VideoDisplayFitMode::Scale:
		return libvlc_video_fit_none;
	case ofxVlc4::VideoDisplayFitMode::Smaller:
	default:
		return libvlc_video_fit_smaller;
	}
}

libvlc_position_t toLibvlcOverlayPosition(ofxVlc4::OverlayPosition position) {
	switch (position) {
	case ofxVlc4::OverlayPosition::Left:
		return libvlc_position_left;
	case ofxVlc4::OverlayPosition::Right:
		return libvlc_position_right;
	case ofxVlc4::OverlayPosition::Top:
		return libvlc_position_top;
	case ofxVlc4::OverlayPosition::TopLeft:
		return libvlc_position_top_left;
	case ofxVlc4::OverlayPosition::TopRight:
		return libvlc_position_top_right;
	case ofxVlc4::OverlayPosition::Bottom:
		return libvlc_position_bottom;
	case ofxVlc4::OverlayPosition::BottomLeft:
		return libvlc_position_bottom_left;
	case ofxVlc4::OverlayPosition::BottomRight:
		return libvlc_position_bottom_right;
	case ofxVlc4::OverlayPosition::Center:
	default:
		return libvlc_position_center;
	}
}

libvlc_position_t toLibvlcVideoTitlePosition(ofxVlc4::OverlayPosition position) {
	return toLibvlcOverlayPosition(position);
}

libvlc_teletext_key_t toLibvlcTeletextKey(ofxVlc4::TeletextKey key) {
	switch (key) {
	case ofxVlc4::TeletextKey::Green:
		return libvlc_teletext_key_green;
	case ofxVlc4::TeletextKey::Yellow:
		return libvlc_teletext_key_yellow;
	case ofxVlc4::TeletextKey::Blue:
		return libvlc_teletext_key_blue;
	case ofxVlc4::TeletextKey::Index:
		return libvlc_teletext_key_index;
	case ofxVlc4::TeletextKey::Red:
	default:
		return libvlc_teletext_key_red;
	}
}

const char * videoDeinterlaceModeLabel(ofxVlc4::VideoDeinterlaceMode mode) {
	switch (mode) {
	case ofxVlc4::VideoDeinterlaceMode::Off:
		return "off";
	case ofxVlc4::VideoDeinterlaceMode::Blend:
		return "blend";
	case ofxVlc4::VideoDeinterlaceMode::Bob:
		return "bob";
	case ofxVlc4::VideoDeinterlaceMode::Linear:
		return "linear";
	case ofxVlc4::VideoDeinterlaceMode::X:
		return "x";
	case ofxVlc4::VideoDeinterlaceMode::Yadif:
		return "yadif";
	case ofxVlc4::VideoDeinterlaceMode::Yadif2x:
		return "yadif2x";
	case ofxVlc4::VideoDeinterlaceMode::Phosphor:
		return "phosphor";
	case ofxVlc4::VideoDeinterlaceMode::Ivtc:
		return "ivtc";
	case ofxVlc4::VideoDeinterlaceMode::Auto:
	default:
		return "auto";
	}
}

int videoDeinterlaceState(ofxVlc4::VideoDeinterlaceMode mode) {
	switch (mode) {
	case ofxVlc4::VideoDeinterlaceMode::Auto:
		return -1;
	case ofxVlc4::VideoDeinterlaceMode::Off:
		return 0;
	default:
		return 1;
	}
}

const char * videoAdjustmentEngineLabel(ofxVlc4::VideoAdjustmentEngine engine) {
	switch (engine) {
	case ofxVlc4::VideoAdjustmentEngine::LibVlc:
		return "libVLC";
	case ofxVlc4::VideoAdjustmentEngine::Shader:
		return "shader";
	case ofxVlc4::VideoAdjustmentEngine::Auto:
	default:
		return "auto";
	}
}

const std::string kVideoAdjustVertexShaderGl3 = R"(#version 150
uniform mat4 modelViewProjectionMatrix;
in vec4 position;
in vec2 texcoord;
out vec2 vTexCoord;

void main() {
	vTexCoord = texcoord;
	gl_Position = modelViewProjectionMatrix * position;
}
)";

const std::string kVideoAdjustFragmentShaderGl3 = R"(#version 150
uniform sampler2D tex0;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float gammaValue;
uniform float hueDegrees;
in vec2 vTexCoord;
out vec4 outputColor;

vec3 applyHueRotation(vec3 color, float hueDegreesValue) {
	float angle = radians(hueDegreesValue);
	float cosA = cos(angle);
	float sinA = sin(angle);

	mat3 rgbToYiq = mat3(
		0.299, 0.587, 0.114,
		0.596, -0.274, -0.322,
		0.211, -0.523, 0.312
	);
	mat3 yiqToRgb = mat3(
		1.0, 0.956, 0.621,
		1.0, -0.272, -0.647,
		1.0, -1.106, 1.703
	);

	vec3 yiq = rgbToYiq * color;
	vec2 iq = mat2(cosA, -sinA, sinA, cosA) * yiq.yz;
	return yiqToRgb * vec3(yiq.x, iq.x, iq.y);
}

void main() {
	vec3 color = texture(tex0, vTexCoord).rgb;
	color = applyHueRotation(color, hueDegrees);
	float luma = dot(color, vec3(0.299, 0.587, 0.114));
	color = mix(vec3(luma), color, saturation);
	color = ((color - 0.5) * contrast) + 0.5;
	color += vec3(brightness - 1.0);
	color = clamp(color, 0.0, 1.0);
	color = pow(color, vec3(1.0 / max(gammaValue, 0.001)));
outputColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";

std::string readTextFileIfPresent(const std::string & path) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input.is_open()) {
		return "";
	}

	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

std::vector<std::string> videoAdjustShaderCandidatePaths(const std::string & fileName) {
	const std::string exeDir = ofFilePath::getCurrentExeDir();
	const std::string dataDir = ofToDataPath("", true);
	const std::string workingDir = ofFilePath::getCurrentWorkingDirectory();

	return {
		ofFilePath::getAbsolutePath(ofFilePath::join(dataDir, ofFilePath::join("shaders/ofxVlc4", fileName)), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(exeDir, ofFilePath::join("../../src/video/shaders", fileName)), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(exeDir, ofFilePath::join("../src/video/shaders", fileName)), true),
		ofFilePath::getAbsolutePath(ofFilePath::join(workingDir, ofFilePath::join("src/video/shaders", fileName)), true),
	};
}

std::string loadVideoAdjustShaderSource(const std::string & fileName) {
	const std::vector<std::string> candidatePaths = videoAdjustShaderCandidatePaths(fileName);
	for (const std::string & candidatePath : candidatePaths) {
		const std::string source = readTextFileIfPresent(candidatePath);
		if (!source.empty()) {
			return source;
		}
	}
	return "";
}

const char * videoDeinterlaceFilterName(ofxVlc4::VideoDeinterlaceMode mode) {
	switch (mode) {
	case ofxVlc4::VideoDeinterlaceMode::Blend:
		return "blend";
	case ofxVlc4::VideoDeinterlaceMode::Bob:
		return "bob";
	case ofxVlc4::VideoDeinterlaceMode::Linear:
		return "linear";
	case ofxVlc4::VideoDeinterlaceMode::X:
		return "x";
	case ofxVlc4::VideoDeinterlaceMode::Yadif:
		return "yadif";
	case ofxVlc4::VideoDeinterlaceMode::Yadif2x:
		return "yadif2x";
	case ofxVlc4::VideoDeinterlaceMode::Phosphor:
		return "phosphor";
	case ofxVlc4::VideoDeinterlaceMode::Ivtc:
		return "ivtc";
	case ofxVlc4::VideoDeinterlaceMode::Auto:
	case ofxVlc4::VideoDeinterlaceMode::Off:
	default:
		return nullptr;
	}
}

const char * videoAspectRatioValue(ofxVlc4::VideoAspectRatioMode mode) {
	switch (mode) {
	case ofxVlc4::VideoAspectRatioMode::Fill:
		return "fill";
	case ofxVlc4::VideoAspectRatioMode::Ratio16_9:
		return "16:9";
	case ofxVlc4::VideoAspectRatioMode::Ratio16_10:
		return "16:10";
	case ofxVlc4::VideoAspectRatioMode::Ratio4_3:
		return "4:3";
	case ofxVlc4::VideoAspectRatioMode::Ratio1_1:
		return "1:1";
	case ofxVlc4::VideoAspectRatioMode::Ratio21_9:
		return "21:9";
	case ofxVlc4::VideoAspectRatioMode::Ratio235_1:
		return "235:100";
	case ofxVlc4::VideoAspectRatioMode::Default:
	default:
		return nullptr;
	}
}

const char * videoAspectRatioLabel(ofxVlc4::VideoAspectRatioMode mode) {
	switch (mode) {
	case ofxVlc4::VideoAspectRatioMode::Fill:
		return "fill";
	case ofxVlc4::VideoAspectRatioMode::Ratio16_9:
		return "16:9";
	case ofxVlc4::VideoAspectRatioMode::Ratio16_10:
		return "16:10";
	case ofxVlc4::VideoAspectRatioMode::Ratio4_3:
		return "4:3";
	case ofxVlc4::VideoAspectRatioMode::Ratio1_1:
		return "1:1";
	case ofxVlc4::VideoAspectRatioMode::Ratio21_9:
		return "21:9";
	case ofxVlc4::VideoAspectRatioMode::Ratio235_1:
		return "2.35:1";
	case ofxVlc4::VideoAspectRatioMode::Default:
	default:
		return "default";
	}
}

std::pair<unsigned, unsigned> videoCropRatio(ofxVlc4::VideoCropMode mode) {
	switch (mode) {
	case ofxVlc4::VideoCropMode::Ratio16_9:
		return { 16u, 9u };
	case ofxVlc4::VideoCropMode::Ratio16_10:
		return { 16u, 10u };
	case ofxVlc4::VideoCropMode::Ratio4_3:
		return { 4u, 3u };
	case ofxVlc4::VideoCropMode::Ratio1_1:
		return { 1u, 1u };
	case ofxVlc4::VideoCropMode::Ratio21_9:
		return { 21u, 9u };
	case ofxVlc4::VideoCropMode::Ratio235_1:
		return { 235u, 100u };
	case ofxVlc4::VideoCropMode::None:
	default:
		return { 0u, 0u };
	}
}

const char * videoCropLabel(ofxVlc4::VideoCropMode mode) {
	switch (mode) {
	case ofxVlc4::VideoCropMode::Ratio16_9:
		return "16:9";
	case ofxVlc4::VideoCropMode::Ratio16_10:
		return "16:10";
	case ofxVlc4::VideoCropMode::Ratio4_3:
		return "4:3";
	case ofxVlc4::VideoCropMode::Ratio1_1:
		return "1:1";
	case ofxVlc4::VideoCropMode::Ratio21_9:
		return "21:9";
	case ofxVlc4::VideoCropMode::Ratio235_1:
		return "2.35:1";
	case ofxVlc4::VideoCropMode::None:
	default:
		return "none";
	}
}

int clampPackedRgbColor(int color) {
	return ofClamp(color, 0x000000, 0xFFFFFF);
}

const char * videoOutputBackendLabel(ofxVlc4::VideoOutputBackend backend) {
	switch (backend) {
	case ofxVlc4::VideoOutputBackend::NativeWindow:
		return "Native Window";
	case ofxVlc4::VideoOutputBackend::D3D11Metadata:
		return "D3D11 HDR Metadata";
	case ofxVlc4::VideoOutputBackend::Texture:
	default:
		return "Texture";
	}
}

const char * preferredDecoderDeviceLabel(ofxVlc4::PreferredDecoderDevice device) {
	switch (device) {
	case ofxVlc4::PreferredDecoderDevice::D3D11:
		return "D3D11";
	case ofxVlc4::PreferredDecoderDevice::DXVA2:
		return "DXVA2";
	case ofxVlc4::PreferredDecoderDevice::Nvdec:
		return "NVDEC";
	case ofxVlc4::PreferredDecoderDevice::None:
		return "None";
	case ofxVlc4::PreferredDecoderDevice::Any:
	default:
		return "Auto";
	}
}

std::pair<unsigned, unsigned> visibleVideoSourceSize(const ofxVlc4::VideoStateInfo & state) {
	return {
		std::min(state.sourceWidth, state.renderWidth),
		std::min(state.sourceHeight, state.renderHeight)
	};
}
}

ofxVlc4::MediaComponent & ofxVlc4::VideoComponent::media() const {
	return *owner.mediaComponent;
}

ofxVlc4::VideoComponent::VideoComponent(ofxVlc4 & owner)
	: owner(owner) {}

void ofxVlc4::VideoComponent::clearPublishedFrameFenceLocked() {
	if (owner.videoFrameRuntime.publishedVideoFrameFence) {
		glDeleteSync(owner.videoFrameRuntime.publishedVideoFrameFence);
		owner.videoFrameRuntime.publishedVideoFrameFence = nullptr;
	}
}

void ofxVlc4::VideoComponent::waitForPublishedFrameFenceLocked() {
	GLsync publishedFence = owner.videoFrameRuntime.publishedVideoFrameFence;
	if (!publishedFence) {
		return;
	}

	owner.videoFrameRuntime.publishedVideoFrameFence = nullptr;
	const GLenum waitResult = glClientWaitSync(
		publishedFence,
		GL_SYNC_FLUSH_COMMANDS_BIT,
		1000000000ULL);
	if (waitResult == GL_WAIT_FAILED) {
		owner.logWarning("GL sync wait failed; frame texture may be incomplete.");
	} else if (waitResult == GL_TIMEOUT_EXPIRED) {
		// CPU wait timed out; hand the wait off to the GPU pipeline so that
		// subsequent draw commands on this context are still ordered after the
		// fence, then continue.
		owner.logWarning("GL sync wait timed out; handing fence to GPU pipeline.");
		glWaitSync(publishedFence, 0, GL_TIMEOUT_IGNORED);
	}
	glDeleteSync(publishedFence);
}

void ofxVlc4::VideoComponent::clearPublishedFrameFence() {
	std::lock_guard<std::mutex> lock(owner.videoMutex);
	clearPublishedFrameFenceLocked();
}

void ofxVlc4::applyVlcFullscreen() {
	videoComponent->applyVlcFullscreen();
}

void ofxVlc4::VideoComponent::applyCurrentPlayerSettings() {
	applyVlcFullscreen();
	applyVideoInputHandling();
	applyVideoTitleDisplay();
	applyVideoAdjustments();
	applyVideoDeinterlace();
	applyVideoAspectRatio();
	applyVideoCrop();
	applyVideoScaleAndFit();
	applyTeletextSettings();
	applyVideoMarquee();
	applyVideoLogo();
	applyVideoProjectionMode();
	applyVideoStereoMode();
	applyVideoViewpoint();
}

void ofxVlc4::VideoComponent::applyVlcFullscreen() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_set_fullscreen(player, owner.vlcFullscreenEnabled);
}

void ofxVlc4::VideoComponent::applyVideoInputHandling() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_key_input(player, owner.keyInputEnabled ? 1u : 0u);
	libvlc_video_set_mouse_input(player, owner.mouseInputEnabled ? 1u : 0u);
}

void ofxVlc4::VideoComponent::applyVideoTitleDisplay() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_set_video_title_display(
		player,
		owner.videoTitleDisplayEnabled ? toLibvlcVideoTitlePosition(owner.videoTitleDisplayPosition) : libvlc_position_disable,
		owner.videoTitleDisplayTimeoutMs);
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::VideoComponent::resolveActiveVideoAdjustmentEngine() const {
	switch (owner.videoAdjustmentEngine) {
	case ofxVlc4::VideoAdjustmentEngine::Shader:
		return usesTextureVideoOutput() ? ofxVlc4::VideoAdjustmentEngine::Shader : ofxVlc4::VideoAdjustmentEngine::LibVlc;
	case ofxVlc4::VideoAdjustmentEngine::LibVlc:
		return ofxVlc4::VideoAdjustmentEngine::LibVlc;
	case ofxVlc4::VideoAdjustmentEngine::Auto:
	default:
		return usesTextureVideoOutput() ? ofxVlc4::VideoAdjustmentEngine::Shader : ofxVlc4::VideoAdjustmentEngine::LibVlc;
	}
}

bool ofxVlc4::VideoComponent::usesShaderVideoAdjustments() const {
	return owner.videoAdjustmentsEnabled &&
		resolveActiveVideoAdjustmentEngine() == ofxVlc4::VideoAdjustmentEngine::Shader;
}

void ofxVlc4::VideoComponent::ensureVideoAdjustShaderLoaded() {
	if (owner.videoAdjustShaderReady || owner.shuttingDown.load()) {
		return;
	}

	if (!ofIsGLProgrammableRenderer()) {
		return;
	}

	const std::string vertexShaderSource = loadVideoAdjustShaderSource("videoAdjust.vert");
	const std::string fragmentShaderSource = loadVideoAdjustShaderSource("videoAdjust.frag");
	const std::string & resolvedVertexShaderSource =
		vertexShaderSource.empty() ? kVideoAdjustVertexShaderGl3 : vertexShaderSource;
	const std::string & resolvedFragmentShaderSource =
		fragmentShaderSource.empty() ? kVideoAdjustFragmentShaderGl3 : fragmentShaderSource;

	owner.videoAdjustShader.setupShaderFromSource(GL_VERTEX_SHADER, resolvedVertexShaderSource);
	owner.videoAdjustShader.setupShaderFromSource(GL_FRAGMENT_SHADER, resolvedFragmentShaderSource);
	owner.videoAdjustShader.bindDefaults();
	owner.videoAdjustShaderReady = owner.videoAdjustShader.linkProgram();
}

void ofxVlc4::VideoComponent::applyVideoAdjustments() {
	const ofxVlc4::VideoAdjustmentEngine activeEngine = resolveActiveVideoAdjustmentEngine();
	owner.activeVideoAdjustmentEngine = activeEngine;

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (activeEngine == ofxVlc4::VideoAdjustmentEngine::Shader) {
		if (player) {
			libvlc_video_set_adjust_int(player, libvlc_adjust_Enable, 0);
		}
		owner.exposedTextureDirty.store(true);
		return;
	}

	if (!player) {
		return;
	}

	if (!owner.videoAdjustmentsEnabled) {
		libvlc_video_set_adjust_int(player, libvlc_adjust_Enable, 0);
		return;
	}

	libvlc_video_set_adjust_float(player, libvlc_adjust_Contrast, owner.videoAdjustContrast);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Brightness, owner.videoAdjustBrightness);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Hue, owner.videoAdjustHue);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Saturation, owner.videoAdjustSaturation);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Gamma, owner.videoAdjustGamma);
	libvlc_video_set_adjust_int(player, libvlc_adjust_Enable, 1);
}

bool ofxVlc4::VideoComponent::shouldApplyVideoAdjustmentsImmediately() const {
	if (usesShaderVideoAdjustments()) {
		return true;
	}
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	return state == libvlc_Playing || state == libvlc_Paused;
}

void ofxVlc4::VideoComponent::applyOrQueueVideoAdjustments() {
	if (shouldApplyVideoAdjustmentsImmediately()) {
		applyVideoAdjustments();
		clearPendingVideoAdjustApplyOnPlay();
	} else {
		setPendingVideoAdjustApplyOnPlay(true);
	}
}

void ofxVlc4::VideoComponent::applyPendingVideoAdjustmentsOnPlay() {
	applyVideoAdjustments();
	clearPendingVideoAdjustApplyOnPlay();
}

void ofxVlc4::VideoComponent::setPendingVideoAdjustApplyOnPlay(bool pending) {
	owner.pendingVideoAdjustApplyOnPlay.store(pending);
}

void ofxVlc4::VideoComponent::clearPendingVideoAdjustApplyOnPlay() {
	setPendingVideoAdjustApplyOnPlay(false);
}

bool ofxVlc4::VideoComponent::hasPendingVideoAdjustApplyOnPlay() const {
	return owner.pendingVideoAdjustApplyOnPlay.load();
}

void ofxVlc4::VideoComponent::applyVideoDeinterlace() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_video_set_deinterlace(
			player,
			videoDeinterlaceState(owner.videoDeinterlaceMode),
			videoDeinterlaceFilterName(owner.videoDeinterlaceMode)) != 0) {
		owner.logWarning("Video deinterlace could not be applied.");
	}
}

void ofxVlc4::VideoComponent::applyVideoAspectRatio() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_aspect_ratio(player, videoAspectRatioValue(owner.videoAspectRatioMode));
}

void ofxVlc4::VideoComponent::applyVideoCrop() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	const auto [numerator, denominator] = videoCropRatio(owner.videoCropMode);
	libvlc_video_set_crop_ratio(player, numerator, denominator);
}

void ofxVlc4::VideoComponent::applyVideoScaleAndFit() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_display_fit(player, toLibvlcVideoFitMode(owner.videoDisplayFitMode));
	if (owner.videoDisplayFitMode == VideoDisplayFitMode::Scale) {
		libvlc_video_set_scale(player, owner.videoScale);
	} else {
		libvlc_video_set_scale(player, 0.0f);
	}
}

void ofxVlc4::VideoComponent::applyVideoProjectionMode() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (owner.videoProjectionMode == VideoProjectionMode::Auto) {
		libvlc_video_unset_projection_mode(player);
		return;
	}

	libvlc_video_set_projection_mode(player, toLibvlcProjectionMode(owner.videoProjectionMode));
}

void ofxVlc4::VideoComponent::applyVideoStereoMode() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_video_stereo_mode(player, toLibvlcStereoMode(owner.videoStereoMode));
}

void ofxVlc4::VideoComponent::applyVideoViewpoint(bool absolute) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_viewpoint_t viewpoint {};
	viewpoint.f_yaw = owner.videoViewYaw;
	viewpoint.f_pitch = owner.videoViewPitch;
	viewpoint.f_roll = owner.videoViewRoll;
	viewpoint.f_field_of_view = owner.videoViewFov;
	libvlc_video_update_viewpoint(player, &viewpoint, absolute);
}

void ofxVlc4::VideoComponent::clearVideoHdrMetadata() {
	std::lock_guard<std::mutex> lock(owner.videoMutex);
	owner.videoHdrMetadata = {};
	owner.videoHdrMetadata.supported = owner.videoOutputBackend == VideoOutputBackend::D3D11Metadata ||
		owner.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata;
}

void ofxVlc4::VideoComponent::releaseD3D11Resources() {
#ifdef TARGET_WIN32
	if (owner.d3d11RenderTargetView) {
		owner.d3d11RenderTargetView->Release();
		owner.d3d11RenderTargetView = nullptr;
	}
	if (owner.d3d11RenderTexture) {
		owner.d3d11RenderTexture->Release();
		owner.d3d11RenderTexture = nullptr;
	}
	if (owner.d3d11Multithread) {
		owner.d3d11Multithread->Release();
		owner.d3d11Multithread = nullptr;
	}
	if (owner.d3d11DeviceContext) {
		owner.d3d11DeviceContext->Release();
		owner.d3d11DeviceContext = nullptr;
	}
	if (owner.d3d11Device) {
		owner.d3d11Device->Release();
		owner.d3d11Device = nullptr;
	}
	owner.d3d11RenderDxgiFormat = 0;
#endif
}

void ofxVlc4::VideoComponent::updateNativeVideoWindowVisibility() {
	if (!owner.vlcWindow) {
		return;
	}

	GLFWwindow * glfwWindow = owner.vlcWindow->getGLFWWindow();
	if (!glfwWindow) {
		return;
	}

	if (owner.activeVideoOutputBackend == VideoOutputBackend::NativeWindow && owner.sessionPlayer()) {
		owner.vlcWindow->setWindowTitle("ofxVlc4 Native Video");
		owner.vlcWindow->setWindowShape(960, 540);
		owner.vlcWindow->setWindowPosition(560, 24);
		glfwShowWindow(glfwWindow);
	} else {
		glfwHideWindow(glfwWindow);
	}
}

bool ofxVlc4::VideoComponent::usesTextureVideoOutput() const {
	return owner.activeVideoOutputBackend == VideoOutputBackend::Texture;
}

bool ofxVlc4::VideoComponent::applyVideoOutputBackend() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	clearVideoHdrMetadata();
	releaseD3D11Resources();
	updateNativeVideoWindowVisibility();

	if (owner.videoOutputBackend == VideoOutputBackend::Texture) {
		const bool configured = libvlc_video_set_output_callbacks(
			player,
			libvlc_video_engine_opengl,
			nullptr, nullptr, nullptr,
			&ofxVlc4::videoResize,
			&ofxVlc4::videoSwap,
			&ofxVlc4::make_current,
			&ofxVlc4::get_proc_address,
			nullptr, nullptr,
			&owner);
		if (!configured) {
			owner.setError("Texture video output callbacks could not be configured.");
			return false;
		}
		owner.activeVideoOutputBackend = VideoOutputBackend::Texture;
		updateNativeVideoWindowVisibility();
		return true;
	}

	if (owner.videoOutputBackend == VideoOutputBackend::D3D11Metadata) {
#ifdef TARGET_WIN32
		const bool configured = libvlc_video_set_output_callbacks(
			player,
			libvlc_video_engine_d3d11,
			&ofxVlc4::videoOutputSetup,
			&ofxVlc4::videoOutputCleanup,
			nullptr,
			&ofxVlc4::videoResize,
			&ofxVlc4::videoSwap,
			&ofxVlc4::make_current,
			nullptr,
			&ofxVlc4::videoFrameMetadata,
			nullptr,
			&owner);
		if (!configured) {
			owner.setError("D3D11 video output callbacks could not be configured.");
			return false;
		}
		owner.activeVideoOutputBackend = VideoOutputBackend::D3D11Metadata;
		updateNativeVideoWindowVisibility();
		owner.setStatus("D3D11 HDR metadata backend configured.");
		owner.logNotice("Video output backend: D3D11 HDR metadata.");
		return true;
#else
		owner.setError("D3D11 HDR metadata output is only supported on Windows.");
		return false;
#endif
	}

#ifdef TARGET_WIN32
	if (!owner.vlcWindow || !owner.vlcWindow->getGLFWWindow()) {
		owner.setError("Native video window is unavailable.");
		return false;
	}

	void * hwnd = glfwGetWin32Window(owner.vlcWindow->getGLFWWindow());
	if (!hwnd) {
		owner.setError("Native window handle is unavailable.");
		return false;
	}

	libvlc_media_player_set_hwnd(player, hwnd);
	owner.activeVideoOutputBackend = VideoOutputBackend::NativeWindow;
	updateNativeVideoWindowVisibility();
	return true;
#elif defined(TARGET_LINUX)
	if (!owner.vlcWindow || !owner.vlcWindow->getGLFWWindow()) {
		owner.setError("Native video window is unavailable.");
		return false;
	}

	Window xwindow = glfwGetX11Window(owner.vlcWindow->getGLFWWindow());
	if (xwindow == 0) {
		owner.setError("X11 native window handle is unavailable.");
		return false;
	}

	libvlc_media_player_set_xwindow(player, static_cast<uint32_t>(xwindow));
	owner.activeVideoOutputBackend = VideoOutputBackend::NativeWindow;
	updateNativeVideoWindowVisibility();
	return true;
#elif defined(TARGET_OSX)
	if (!owner.vlcWindow || !owner.vlcWindow->getGLFWWindow()) {
		owner.setError("Native video window is unavailable.");
		return false;
	}

	id cocoaWindow = glfwGetCocoaWindow(owner.vlcWindow->getGLFWWindow());
	if (!cocoaWindow) {
		owner.setError("Cocoa native window handle is unavailable.");
		return false;
	}

	id cocoaView = ((id(*)(id, SEL))objc_msgSend)(cocoaWindow, sel_registerName("contentView"));
	if (!cocoaView) {
		owner.setError("Cocoa content view is unavailable.");
		return false;
	}

	libvlc_media_player_set_nsobject(player, cocoaView);
	owner.activeVideoOutputBackend = VideoOutputBackend::NativeWindow;
	updateNativeVideoWindowVisibility();
	return true;
#else
	owner.setError("Native window video output is only supported on Windows, Cocoa, and X11.");
	return false;
#endif
}

void ofxVlc4::VideoComponent::prepareStartupVideoResources() {
	if (owner.activeVideoOutputBackend != VideoOutputBackend::Texture) {
		return;
	}

	unsigned width = 0;
	unsigned height = 0;
	unsigned sarNum = 1;
	unsigned sarDen = 1;
	if (!queryVideoTrackGeometry(width, height, sarNum, sarDen) || width == 0 || height == 0) {
		return;
	}

	const int glPixelFormat = owner.pendingGlPixelFormat.load();

	owner.pixelAspectNumerator.store(sarNum > 0 ? sarNum : 1u);
	owner.pixelAspectDenominator.store(sarDen > 0 ? sarDen : 1u);
	owner.renderWidth.store(width);
	owner.renderHeight.store(height);
	owner.videoWidth.store(width);
	owner.videoHeight.store(height);
	refreshDisplayAspectRatio();
	ensureVideoRenderTargetCapacity(width, height, glPixelFormat);
	ensureExposedTextureFboCapacity(width, height, glPixelFormat);
	owner.exposedTextureDirty.store(true);
}

void ofxVlc4::VideoComponent::applyTeletextSettings() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_teletext_transparency(player, owner.teletextTransparencyEnabled);
	libvlc_video_set_teletext(player, owner.teletextPage);
}

void ofxVlc4::VideoComponent::applyVideoMarquee() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_marquee_int(player, libvlc_marquee_Enable, owner.marqueeEnabled ? 1 : 0);
	if (!owner.marqueeEnabled) {
		return;
	}

	libvlc_video_set_marquee_string(player, libvlc_marquee_Text, owner.marqueeText.c_str());
	libvlc_video_set_marquee_int(player, libvlc_marquee_Position, toLibvlcOverlayPosition(owner.marqueePosition));
	libvlc_video_set_marquee_int(player, libvlc_marquee_Opacity, owner.marqueeOpacity);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Size, owner.marqueeSize);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Color, owner.marqueeColor);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Refresh, owner.marqueeRefresh);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Timeout, owner.marqueeTimeout);
	libvlc_video_set_marquee_int(player, libvlc_marquee_X, owner.marqueeX);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Y, owner.marqueeY);
}

void ofxVlc4::VideoComponent::applyVideoLogo() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	const std::string resolvedLogoPath = normalizeOptionalPath(owner.logoPath);
	const bool canEnableLogo = owner.logoEnabled && !trimWhitespace(resolvedLogoPath).empty();
	libvlc_video_set_logo_int(player, libvlc_logo_enable, canEnableLogo ? 1 : 0);
	if (!canEnableLogo) {
		return;
	}

	libvlc_video_set_logo_string(player, libvlc_logo_file, resolvedLogoPath.c_str());
	libvlc_video_set_logo_int(player, libvlc_logo_position, toLibvlcOverlayPosition(owner.logoPosition));
	libvlc_video_set_logo_int(player, libvlc_logo_opacity, owner.logoOpacity);
	libvlc_video_set_logo_int(player, libvlc_logo_x, owner.logoX);
	libvlc_video_set_logo_int(player, libvlc_logo_y, owner.logoY);
	libvlc_video_set_logo_int(player, libvlc_logo_delay, owner.logoDelay);
	libvlc_video_set_logo_int(player, libvlc_logo_repeat, owner.logoRepeat);
}

void ofxVlc4::VideoComponent::ensureVideoRenderTargetCapacity(unsigned requiredWidth, unsigned requiredHeight, int glPixelFormat) {
	if (requiredWidth == 0 || requiredHeight == 0 || owner.shuttingDown.load()) {
		return;
	}

	const bool formatChanged = glPixelFormat != owner.allocatedGlPixelFormat;
	if (!owner.videoTexture.isAllocated() || requiredWidth > owner.allocatedVideoWidth || requiredHeight > owner.allocatedVideoHeight || formatChanged) {
		clearPublishedFrameFenceLocked();
		owner.allocatedVideoWidth = std::max(owner.allocatedVideoWidth, requiredWidth);
		owner.allocatedVideoHeight = std::max(owner.allocatedVideoHeight, requiredHeight);
		if (formatChanged) {
			owner.allocatedGlPixelFormat = glPixelFormat;
		}
		owner.videoTexture.clear();
		owner.videoTexture.allocate(owner.allocatedVideoWidth, owner.allocatedVideoHeight, owner.allocatedGlPixelFormat);
		owner.videoTexture.getTextureData().bFlipTexture = true;
		{
			const GLenum texTarget = owner.videoTexture.getTextureData().textureTarget;
			const GLuint texId = owner.videoTexture.getTextureData().textureID;
			glBindTexture(texTarget, texId);
			glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(texTarget, 0);
		}
		if (owner.vlcFramebufferId == 0) {
			glGenFramebuffers(1, &owner.vlcFramebufferId);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, owner.vlcFramebufferId);
		glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			owner.videoTexture.getTextureData().textureTarget,
			owner.videoTexture.getTextureData().textureID,
			0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		ofClear(0, 0, 0, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		owner.vlcFramebufferAttachmentDirty.store(true);
	}
}

void ofxVlc4::VideoComponent::ensureExposedTextureFboCapacity(unsigned requiredWidth, unsigned requiredHeight, int glPixelFormat) {
	if (requiredWidth == 0 || requiredHeight == 0 || owner.shuttingDown.load()) {
		return;
	}

	const unsigned currentWidth =
		owner.exposedTextureFbo.isAllocated() ? static_cast<unsigned>(owner.exposedTextureFbo.getWidth()) : 0u;
	const unsigned currentHeight =
		owner.exposedTextureFbo.isAllocated() ? static_cast<unsigned>(owner.exposedTextureFbo.getHeight()) : 0u;
	const unsigned targetWidth = std::max(currentWidth, requiredWidth);
	const unsigned targetHeight = std::max(currentHeight, requiredHeight);

	const bool formatMismatch = owner.exposedTextureFbo.isAllocated() &&
		static_cast<int>(owner.exposedTextureFbo.getTexture().getTextureData().glInternalFormat) != glPixelFormat;

	if (!owner.exposedTextureFbo.isAllocated() ||
		targetWidth != currentWidth ||
		targetHeight != currentHeight ||
		formatMismatch) {
		owner.exposedTextureFbo.allocate(targetWidth, targetHeight, glPixelFormat);
		owner.exposedTextureFbo.getTexture().getTextureData().bFlipTexture = true;
		clearAllocatedFbo(owner.exposedTextureFbo);
	}
}

bool ofxVlc4::VideoComponent::applyPendingVideoResize() {
	if (!owner.pendingResize.exchange(false)) {
		return false;
	}

	const unsigned newRenderWidth = owner.pendingRenderWidth.load();
	const unsigned newRenderHeight = owner.pendingRenderHeight.load();
	if (newRenderWidth == 0 || newRenderHeight == 0) {
		return false;
	}

	const int newGlPixelFormat = owner.pendingGlPixelFormat.load();

	unsigned visibleWidth = newRenderWidth;
	unsigned visibleHeight = newRenderHeight;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (player) {
		unsigned queriedWidth = 0;
		unsigned queriedHeight = 0;
		if (libvlc_video_get_size(player, 0, &queriedWidth, &queriedHeight) == 0 &&
			queriedWidth > 0 &&
			queriedHeight > 0) {
			visibleWidth = queriedWidth;
			visibleHeight = queriedHeight;
		}
	}

	refreshPixelAspectRatio();
	owner.renderWidth.store(newRenderWidth);
	owner.renderHeight.store(newRenderHeight);
	owner.videoWidth.store(visibleWidth);
	owner.videoHeight.store(visibleHeight);
	refreshDisplayAspectRatio();
	ensureVideoRenderTargetCapacity(newRenderWidth, newRenderHeight, newGlPixelFormat);
	owner.isVideoLoaded.store(true);
	owner.exposedTextureDirty.store(true);
	return true;
}

bool ofxVlc4::VideoComponent::videoResize(const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg) {
	if (!cfg || !render_cfg) {
		return false;
	}

	if (owner.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata) {
#ifdef TARGET_WIN32
		const DXGI_FORMAT dxgiFormat = (cfg->bitdepth > 8) ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;

		{
			std::lock_guard<std::mutex> lock(owner.videoMutex);
			owner.videoHdrMetadata.supported = true;
			owner.videoHdrMetadata.width = cfg->width;
			owner.videoHdrMetadata.height = cfg->height;
			owner.videoHdrMetadata.bitDepth = cfg->bitdepth;
			owner.videoHdrMetadata.fullRange = cfg->full_range;
			owner.videoHdrMetadata.colorspace = cfg->colorspace;
			owner.videoHdrMetadata.primaries = cfg->primaries;
			owner.videoHdrMetadata.transfer = cfg->transfer;
			owner.videoHdrMetadata.available = false;

			if (!owner.d3d11Device) {
				return false;
			}

			if (!owner.d3d11RenderTexture ||
				!owner.d3d11RenderTargetView ||
				owner.renderWidth.load() != cfg->width ||
				owner.renderHeight.load() != cfg->height ||
				owner.d3d11RenderDxgiFormat != static_cast<int>(dxgiFormat)) {
				if (owner.d3d11RenderTargetView) {
					owner.d3d11RenderTargetView->Release();
					owner.d3d11RenderTargetView = nullptr;
				}
				if (owner.d3d11RenderTexture) {
					owner.d3d11RenderTexture->Release();
					owner.d3d11RenderTexture = nullptr;
				}

				D3D11_TEXTURE2D_DESC textureDesc {};
				textureDesc.Width = std::max(1u, cfg->width);
				textureDesc.Height = std::max(1u, cfg->height);
				textureDesc.MipLevels = 1;
				textureDesc.ArraySize = 1;
				textureDesc.Format = dxgiFormat;
				textureDesc.SampleDesc.Count = 1;
				textureDesc.Usage = D3D11_USAGE_DEFAULT;
				textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

				if (FAILED(owner.d3d11Device->CreateTexture2D(&textureDesc, nullptr, &owner.d3d11RenderTexture)) ||
					FAILED(owner.d3d11Device->CreateRenderTargetView(owner.d3d11RenderTexture, nullptr, &owner.d3d11RenderTargetView))) {
					releaseD3D11Resources();
					return false;
				}

				owner.d3d11RenderDxgiFormat = static_cast<int>(dxgiFormat);
			}
		}

		owner.renderWidth.store(cfg->width);
		owner.renderHeight.store(cfg->height);
		owner.videoWidth.store(cfg->width);
		owner.videoHeight.store(cfg->height);
		refreshDisplayAspectRatio();
		owner.isVideoLoaded.store(true);
		owner.exposedTextureDirty.store(true);

		render_cfg->dxgi_format = static_cast<int>(dxgiFormat);
		render_cfg->full_range = cfg->full_range;
		render_cfg->colorspace = cfg->colorspace;
		render_cfg->primaries = cfg->primaries;
		render_cfg->transfer = cfg->transfer;
		render_cfg->orientation = libvlc_video_orient_top_left;
		return true;
#else
		return false;
#endif
	}

	const int glPixelFormat = (cfg->bitdepth > 8) ? static_cast<int>(GL_RGB10_A2) : static_cast<int>(GL_RGBA);
	render_cfg->opengl_format = static_cast<unsigned>(glPixelFormat);
	render_cfg->full_range = true;
	render_cfg->colorspace = libvlc_video_colorspace_BT709;
	render_cfg->primaries = libvlc_video_primaries_BT709;
	render_cfg->transfer = libvlc_video_transfer_func_SRGB;
	render_cfg->orientation = libvlc_video_orient_top_left;

	if (cfg->width != owner.renderWidth.load() || cfg->height != owner.renderHeight.load() ||
		glPixelFormat != owner.pendingGlPixelFormat.load()) {
		owner.pendingGlPixelFormat.store(glPixelFormat);
		owner.pendingRenderWidth.store(cfg->width);
		owner.pendingRenderHeight.store(cfg->height);
		owner.pendingResize.store(true);
	}

	return true;
}

bool ofxVlc4::VideoComponent::videoOutputSetup(const libvlc_video_setup_device_cfg_t * cfg, libvlc_video_setup_device_info_t * out) {
#ifdef TARGET_WIN32
	if (!out) {
		return false;
	}

	releaseD3D11Resources();

	UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	if (cfg && cfg->hardware_decoding) {
		creationFlags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
	}

	static const D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};

	const D3D_DRIVER_TYPE driverTypes[] = {
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP
	};

	HRESULT hr = E_FAIL;
	D3D_FEATURE_LEVEL createdFeatureLevel = D3D_FEATURE_LEVEL_10_0;
	for (const D3D_DRIVER_TYPE driverType : driverTypes) {
		hr = D3D11CreateDevice(
			nullptr,
			driverType,
			nullptr,
			creationFlags,
			featureLevels,
			static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
			D3D11_SDK_VERSION,
			&owner.d3d11Device,
			&createdFeatureLevel,
			&owner.d3d11DeviceContext);
		if (hr == E_INVALIDARG) {
			hr = D3D11CreateDevice(
				nullptr,
				driverType,
				nullptr,
				creationFlags,
				featureLevels + 1,
				static_cast<UINT>((sizeof(featureLevels) / sizeof(featureLevels[0])) - 1),
				D3D11_SDK_VERSION,
				&owner.d3d11Device,
				&createdFeatureLevel,
				&owner.d3d11DeviceContext);
		}
		if (SUCCEEDED(hr)) {
			break;
		}
	}

	if (FAILED(hr) || !owner.d3d11Device || !owner.d3d11DeviceContext) {
		releaseD3D11Resources();
		owner.setError("D3D11 device creation failed.");
		return false;
	}

	(void)createdFeatureLevel;

	owner.d3d11DeviceContext->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void **>(&owner.d3d11Multithread));
	if (owner.d3d11Multithread) {
		owner.d3d11Multithread->SetMultithreadProtected(TRUE);
	}

	out->d3d11.device_context = owner.d3d11DeviceContext;
	out->d3d11.context_mutex = nullptr;
	return true;
#else
	(void)cfg;
	(void)out;
	return false;
#endif
}

void ofxVlc4::VideoComponent::videoOutputCleanup() {
	releaseD3D11Resources();
}

void ofxVlc4::VideoComponent::videoFrameMetadata(libvlc_video_metadata_type_t type, const void * metadata) {
	if (!metadata) {
		return;
	}

	if (type != libvlc_video_metadata_frame_hdr10) {
		return;
	}

	const auto * hdr10 = static_cast<const libvlc_video_frame_hdr10_metadata_t *>(metadata);
	std::lock_guard<std::mutex> lock(owner.videoMutex);
	owner.videoHdrMetadata.supported = true;
	owner.videoHdrMetadata.available = true;
	owner.videoHdrMetadata.redPrimaryX = hdr10->RedPrimary[0];
	owner.videoHdrMetadata.redPrimaryY = hdr10->RedPrimary[1];
	owner.videoHdrMetadata.greenPrimaryX = hdr10->GreenPrimary[0];
	owner.videoHdrMetadata.greenPrimaryY = hdr10->GreenPrimary[1];
	owner.videoHdrMetadata.bluePrimaryX = hdr10->BluePrimary[0];
	owner.videoHdrMetadata.bluePrimaryY = hdr10->BluePrimary[1];
	owner.videoHdrMetadata.whitePointX = hdr10->WhitePoint[0];
	owner.videoHdrMetadata.whitePointY = hdr10->WhitePoint[1];
	owner.videoHdrMetadata.maxMasteringLuminance = hdr10->MaxMasteringLuminance;
	owner.videoHdrMetadata.minMasteringLuminance = hdr10->MinMasteringLuminance;
	owner.videoHdrMetadata.maxContentLightLevel = hdr10->MaxContentLightLevel;
	owner.videoHdrMetadata.maxFrameAverageLightLevel = hdr10->MaxFrameAverageLightLevel;
}

void ofxVlc4::VideoComponent::bindVlcRenderTarget() {
	if (!owner.videoTexture.isAllocated() || owner.vlcFramebufferId == 0) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, owner.vlcFramebufferId);
	if (owner.vlcFramebufferAttachmentDirty.load()) {
		glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			owner.videoTexture.getTextureData().textureTarget,
			owner.videoTexture.getTextureData().textureID,
			0);
		owner.vlcFramebufferAttachmentDirty.store(false);
	}
	const unsigned currentRenderWidth = owner.renderWidth.load();
	const unsigned currentRenderHeight = owner.renderHeight.load();
	if (currentRenderWidth > 0 &&
		currentRenderHeight > 0 &&
		(currentRenderWidth != owner.lastBoundViewportWidth || currentRenderHeight != owner.lastBoundViewportHeight)) {
		ofViewport(0, 0, static_cast<float>(currentRenderWidth), static_cast<float>(currentRenderHeight), false);
		owner.lastBoundViewportWidth = currentRenderWidth;
		owner.lastBoundViewportHeight = currentRenderHeight;
	}
	owner.vlcFboBound = true;
}

void ofxVlc4::VideoComponent::unbindVlcRenderTarget() {
	if (!owner.vlcFboBound) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	owner.vlcFboBound = false;
}

void ofxVlc4::VideoComponent::videoSwap() {
	if (owner.shuttingDown.load()) {
		return;
	}

	owner.hasReceivedVideoFrame.store(true);

	if (owner.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata) {
		return;
	}

	std::lock_guard<std::mutex> lock(owner.videoMutex);
	const bool needsPublish = !owner.exposedTextureDirty.exchange(true);
	if (!needsPublish) {
		return;
	}

	clearPublishedFrameFenceLocked();
	owner.videoFrameRuntime.publishedVideoFrameFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	// The producer context is flushed in makeCurrent(false) after this call
	// returns, which ensures the fence is visible to other contexts before
	// the consumer attempts to wait on it.
}

bool ofxVlc4::VideoComponent::makeCurrent(bool current) {
	std::lock_guard<std::mutex> lock(owner.videoMutex);

	if (owner.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata) {
#ifdef TARGET_WIN32
		if (!owner.d3d11DeviceContext) {
			return false;
		}

		if (current) {
			if (!owner.d3d11RenderTargetView) {
				return false;
			}

			ID3D11RenderTargetView * renderTarget = owner.d3d11RenderTargetView;
			owner.d3d11DeviceContext->OMSetRenderTargets(1, &renderTarget, nullptr);

			D3D11_VIEWPORT viewport {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(std::max(1u, owner.renderWidth.load()));
			viewport.Height = static_cast<float>(std::max(1u, owner.renderHeight.load()));
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			owner.d3d11DeviceContext->RSSetViewports(1, &viewport);
		} else {
			ID3D11RenderTargetView * renderTarget = nullptr;
			owner.d3d11DeviceContext->OMSetRenderTargets(1, &renderTarget, nullptr);
		}
		return true;
#else
		return false;
#endif
	}

	if (!owner.vlcWindow || !owner.vlcWindow->getGLFWWindow()) {
		return false;
	}

	if (current) {
		owner.vlcWindow->makeCurrent();
		applyPendingVideoResize();
		bindVlcRenderTarget();
	} else {
		unbindVlcRenderTarget();
		// Flush pending GL commands before releasing the context so that the
		// fence inserted in videoSwap() and any preceding render commands are
		// submitted to the GPU.  Without this, glClientWaitSync on the main
		// context may stall because the producer-side commands haven't been
		// sent yet (GL_SYNC_FLUSH_COMMANDS_BIT only flushes the waiting
		// context, not the producer context).
		glFlush();
		glfwMakeContextCurrent(nullptr);
	}

	return true;
}

void * ofxVlc4::VideoComponent::getProcAddress(const char * name) const {
	return name ? reinterpret_cast<void *>(glfwGetProcAddress(name)) : nullptr;
}

bool ofxVlc4::VideoComponent::drawCurrentFrame(const VideoStateInfo & state, float x, float y, float width, float height) {
	const auto [visibleSourceWidth, visibleSourceHeight] = visibleVideoSourceSize(state);
	const float sourceWidth = static_cast<float>(visibleSourceWidth);
	const float sourceHeight = static_cast<float>(visibleSourceHeight);
	if (!state.frameReceived || !owner.videoTexture.isAllocated() || sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
		return false;
	}

	if (usesShaderVideoAdjustments()) {
		if (owner.exposedTextureDirty.exchange(false)) {
			refreshExposedTextureLocked(state);
		}
		owner.exposedTextureFbo.getTexture().drawSubsection(x, y, width, height, 0, 0, sourceWidth, sourceHeight);
		return true;
	}

	owner.videoTexture.drawSubsection(x, y, width, height, 0, 0, sourceWidth, sourceHeight);
	return true;
}

void ofxVlc4::VideoComponent::refreshExposedTextureLocked(const VideoStateInfo & state) {
	const auto [sourceWidth, sourceHeight] = visibleVideoSourceSize(state);
	const int glPixelFormat = owner.allocatedGlPixelFormat;
	if (sourceWidth > 0 && sourceHeight > 0 && !state.frameReceived) {
		ensureExposedTextureFboCapacity(sourceWidth, sourceHeight, glPixelFormat);
		clearAllocatedFbo(owner.exposedTextureFbo);
		return;
	}

	if (!owner.videoTexture.isAllocated() || sourceWidth == 0 || sourceHeight == 0) {
		return;
	}

	waitForPublishedFrameFenceLocked();
	ensureExposedTextureFboCapacity(sourceWidth, sourceHeight, glPixelFormat);
	const bool fullFboOverwrite =
		owner.exposedTextureFbo.isAllocated() &&
		static_cast<unsigned>(owner.exposedTextureFbo.getWidth()) == sourceWidth &&
		static_cast<unsigned>(owner.exposedTextureFbo.getHeight()) == sourceHeight;
	owner.exposedTextureFbo.begin();
	if (!fullFboOverwrite) {
		ofClear(0, 0, 0, 255);
	}
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255, 255, 255, 255);
	if (usesShaderVideoAdjustments()) {
		ensureVideoAdjustShaderLoaded();
		if (owner.videoAdjustShaderReady) {
			owner.videoAdjustShader.begin();
			owner.videoAdjustShader.setUniformTexture("tex0", owner.videoTexture, 0);
			owner.videoAdjustShader.setUniform1f("brightness", owner.videoAdjustBrightness);
			owner.videoAdjustShader.setUniform1f("contrast", owner.videoAdjustContrast);
			owner.videoAdjustShader.setUniform1f("saturation", owner.videoAdjustSaturation);
			owner.videoAdjustShader.setUniform1f("gammaValue", owner.videoAdjustGamma);
			owner.videoAdjustShader.setUniform1f("hueDegrees", owner.videoAdjustHue);
		}
	}
	owner.videoTexture.drawSubsection(
		0.0f,
		0.0f,
		static_cast<float>(sourceWidth),
		static_cast<float>(sourceHeight),
		0.0f,
		0.0f,
		static_cast<float>(sourceWidth),
		static_cast<float>(sourceHeight));
	if (usesShaderVideoAdjustments() && owner.videoAdjustShaderReady) {
		owner.videoAdjustShader.end();
	}
	ofPopStyle();
	owner.exposedTextureFbo.end();
}

void ofxVlc4::VideoComponent::refreshExposedTexture() {
	const VideoStateInfo state = getVideoStateInfo();
	std::lock_guard<std::mutex> lock(owner.videoMutex);
	refreshExposedTextureLocked(state);
}

void ofxVlc4::VideoComponent::draw(float x, float y, float width, float height) {
	if (!usesTextureVideoOutput()) {
		return;
	}
	const VideoStateInfo state = getVideoStateInfo();
	std::lock_guard<std::mutex> lock(owner.videoMutex);
	drawCurrentFrame(state, x, y, width, height);
}

void ofxVlc4::VideoComponent::draw(float x, float y) {
	if (!usesTextureVideoOutput()) {
		return;
	}
	const VideoStateInfo state = getVideoStateInfo();
	std::lock_guard<std::mutex> lock(owner.videoMutex);
	const float displayHeight = static_cast<float>(state.sourceHeight);
	const float displayWidth =
		(displayHeight > 0.0f) ? (displayHeight * std::max(state.displayAspectRatio, 0.0001f)) : static_cast<float>(state.sourceWidth);
	drawCurrentFrame(state, x, y, displayWidth, displayHeight);
}

ofxVlc4::VideoHdrMetadataInfo ofxVlc4::VideoComponent::getVideoHdrMetadata() const {
	std::lock_guard<std::mutex> lock(owner.videoMutex);
	return owner.videoHdrMetadata;
}

float ofxVlc4::VideoComponent::getVideoScale() const {
	return owner.videoScale;
}

void ofxVlc4::VideoComponent::setVideoScale(float scale) {
	const float clampedScale = ofClamp(scale, 0.25f, 4.0f);
	if (std::abs(owner.videoScale - clampedScale) < 0.0001f) {
		return;
	}

	owner.videoScale = clampedScale;
	owner.videoDisplayFitMode = VideoDisplayFitMode::Scale;
	applyVideoScaleAndFit();
	owner.setStatus("Video scale set.");
	owner.logNotice("Video scale: " + ofToString(owner.videoScale, 2) + "x.");
}

ofxVlc4::VideoProjectionMode ofxVlc4::VideoComponent::getVideoProjectionMode() const {
	return owner.videoProjectionMode;
}

void ofxVlc4::VideoComponent::setVideoProjectionMode(VideoProjectionMode mode) {
	if (owner.videoProjectionMode == mode) {
		return;
	}

	owner.videoProjectionMode = mode;
	applyVideoProjectionMode();
	std::string modeLabel = "Auto";
	switch (mode) {
	case VideoProjectionMode::Rectangular:
		modeLabel = "Rectangular";
		break;
	case VideoProjectionMode::Equirectangular:
		modeLabel = "360 Equirectangular";
		break;
	case VideoProjectionMode::CubemapStandard:
		modeLabel = "Cubemap";
		break;
	case VideoProjectionMode::Auto:
	default:
		break;
	}
	owner.setStatus("3D projection set.");
	owner.logNotice("3D projection set: " + modeLabel + ".");
}

ofxVlc4::VideoStereoMode ofxVlc4::VideoComponent::getVideoStereoMode() const {
	return owner.videoStereoMode;
}

void ofxVlc4::VideoComponent::setVideoStereoMode(VideoStereoMode mode) {
	if (owner.videoStereoMode == mode) {
		return;
	}

	owner.videoStereoMode = mode;
	applyVideoStereoMode();
	std::string modeLabel = "Auto";
	switch (mode) {
	case VideoStereoMode::Stereo:
		modeLabel = "Stereo";
		break;
	case VideoStereoMode::LeftEye:
		modeLabel = "Left eye";
		break;
	case VideoStereoMode::RightEye:
		modeLabel = "Right eye";
		break;
	case VideoStereoMode::SideBySide:
		modeLabel = "Side by side";
		break;
	case VideoStereoMode::Auto:
	default:
		break;
	}
	owner.setStatus("3D stereo mode set.");
	owner.logNotice("3D stereo mode set: " + modeLabel + ".");
}

float ofxVlc4::VideoComponent::getVideoYaw() const {
	return owner.videoViewYaw;
}

float ofxVlc4::VideoComponent::getVideoPitch() const {
	return owner.videoViewPitch;
}

float ofxVlc4::VideoComponent::getVideoRoll() const {
	return owner.videoViewRoll;
}

float ofxVlc4::VideoComponent::getVideoFov() const {
	return owner.videoViewFov;
}

void ofxVlc4::VideoComponent::setVideoViewpoint(float yaw, float pitch, float roll, float fov, bool absolute) {
	owner.videoViewYaw = ofClamp(yaw, -180.0f, 180.0f);
	owner.videoViewPitch = ofClamp(pitch, -90.0f, 90.0f);
	owner.videoViewRoll = ofClamp(roll, -180.0f, 180.0f);
	owner.videoViewFov = ofClamp(fov, 1.0f, 179.0f);
	applyVideoViewpoint(absolute);
}

void ofxVlc4::VideoComponent::resetVideoViewpoint() {
	owner.videoViewYaw = 0.0f;
	owner.videoViewPitch = 0.0f;
	owner.videoViewRoll = 0.0f;
	owner.videoViewFov = 80.0f;
	applyVideoViewpoint();
	owner.setStatus("3D view reset.");
	owner.logNotice("3D view reset.");
}

bool ofxVlc4::VideoComponent::isMarqueeEnabled() const {
	return owner.marqueeEnabled;
}

void ofxVlc4::VideoComponent::setMarqueeEnabled(bool enabled) {
	if (owner.marqueeEnabled == enabled) {
		return;
	}

	owner.marqueeEnabled = enabled;
	applyVideoMarquee();
	owner.setStatus(std::string("Marquee ") + (owner.marqueeEnabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::VideoComponent::getMarqueeText() const {
	return owner.marqueeText;
}

void ofxVlc4::VideoComponent::setMarqueeText(const std::string & text) {
	if (owner.marqueeText == text) {
		return;
	}

	owner.marqueeText = text;
	applyVideoMarquee();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getMarqueePosition() const {
	return owner.marqueePosition;
}

void ofxVlc4::VideoComponent::setMarqueePosition(OverlayPosition position) {
	if (owner.marqueePosition == position) {
		return;
	}

	owner.marqueePosition = position;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeOpacity() const {
	return owner.marqueeOpacity;
}

void ofxVlc4::VideoComponent::setMarqueeOpacity(int opacity) {
	const int clampedOpacity = ofClamp(opacity, 0, 255);
	if (owner.marqueeOpacity == clampedOpacity) {
		return;
	}

	owner.marqueeOpacity = clampedOpacity;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeSize() const {
	return owner.marqueeSize;
}

void ofxVlc4::VideoComponent::setMarqueeSize(int size) {
	const int clampedSize = ofClamp(size, 6, 96);
	if (owner.marqueeSize == clampedSize) {
		return;
	}

	owner.marqueeSize = clampedSize;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeColor() const {
	return owner.marqueeColor;
}

void ofxVlc4::VideoComponent::setMarqueeColor(int color) {
	const int clampedColor = clampPackedRgbColor(color);
	if (owner.marqueeColor == clampedColor) {
		return;
	}

	owner.marqueeColor = clampedColor;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeRefresh() const {
	return owner.marqueeRefresh;
}

void ofxVlc4::VideoComponent::setMarqueeRefresh(int refreshMs) {
	const int clampedRefreshMs = ofClamp(refreshMs, 0, 10000);
	if (owner.marqueeRefresh == clampedRefreshMs) {
		return;
	}

	owner.marqueeRefresh = clampedRefreshMs;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeTimeout() const {
	return owner.marqueeTimeout;
}

void ofxVlc4::VideoComponent::setMarqueeTimeout(int timeoutMs) {
	const int clampedTimeoutMs = ofClamp(timeoutMs, 0, 10000);
	if (owner.marqueeTimeout == clampedTimeoutMs) {
		return;
	}

	owner.marqueeTimeout = clampedTimeoutMs;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeX() const {
	return owner.marqueeX;
}

void ofxVlc4::VideoComponent::setMarqueeX(int x) {
	const int clampedX = ofClamp(x, -4096, 4096);
	if (owner.marqueeX == clampedX) {
		return;
	}

	owner.marqueeX = clampedX;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeY() const {
	return owner.marqueeY;
}

void ofxVlc4::VideoComponent::setMarqueeY(int y) {
	const int clampedY = ofClamp(y, -4096, 4096);
	if (owner.marqueeY == clampedY) {
		return;
	}

	owner.marqueeY = clampedY;
	applyVideoMarquee();
}

bool ofxVlc4::VideoComponent::isLogoEnabled() const {
	return owner.logoEnabled;
}

void ofxVlc4::VideoComponent::setLogoEnabled(bool enabled) {
	if (owner.logoEnabled == enabled) {
		return;
	}

	owner.logoEnabled = enabled;
	applyVideoLogo();
	owner.setStatus(std::string("Logo ") + (owner.logoEnabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::VideoComponent::getLogoPath() const {
	return owner.logoPath;
}

void ofxVlc4::VideoComponent::setLogoPath(const std::string & path) {
	if (owner.logoPath == path) {
		return;
	}

	owner.logoPath = path;
	applyVideoLogo();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getLogoPosition() const {
	return owner.logoPosition;
}

void ofxVlc4::VideoComponent::setLogoPosition(OverlayPosition position) {
	if (owner.logoPosition == position) {
		return;
	}

	owner.logoPosition = position;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoOpacity() const {
	return owner.logoOpacity;
}

void ofxVlc4::VideoComponent::setLogoOpacity(int opacity) {
	const int clampedOpacity = ofClamp(opacity, 0, 255);
	if (owner.logoOpacity == clampedOpacity) {
		return;
	}

	owner.logoOpacity = clampedOpacity;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoX() const {
	return owner.logoX;
}

void ofxVlc4::VideoComponent::setLogoX(int x) {
	const int clampedX = ofClamp(x, -4096, 4096);
	if (owner.logoX == clampedX) {
		return;
	}

	owner.logoX = clampedX;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoY() const {
	return owner.logoY;
}

void ofxVlc4::VideoComponent::setLogoY(int y) {
	const int clampedY = ofClamp(y, -4096, 4096);
	if (owner.logoY == clampedY) {
		return;
	}

	owner.logoY = clampedY;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoDelay() const {
	return owner.logoDelay;
}

void ofxVlc4::VideoComponent::setLogoDelay(int delayMs) {
	const int clampedDelayMs = ofClamp(delayMs, 0, 10000);
	if (owner.logoDelay == clampedDelayMs) {
		return;
	}

	owner.logoDelay = clampedDelayMs;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoRepeat() const {
	return owner.logoRepeat;
}

void ofxVlc4::VideoComponent::setLogoRepeat(int repeat) {
	const int clampedRepeat = ofClamp(repeat, -1, 100);
	if (owner.logoRepeat == clampedRepeat) {
		return;
	}

	owner.logoRepeat = clampedRepeat;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getTeletextPage() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return libvlc_video_get_teletext(player);
	}
	return owner.teletextPage;
}

void ofxVlc4::VideoComponent::setTeletextPage(int page) {
	const int clampedPage = ofClamp(page, 0, 999);
	if (owner.teletextPage == clampedPage) {
		return;
	}

	owner.teletextPage = clampedPage;
	applyTeletextSettings();
	media().refreshSubtitleStateInfo();
}

bool ofxVlc4::VideoComponent::isTeletextTransparencyEnabled() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return libvlc_video_get_teletext_transparency(player);
	}
	return owner.teletextTransparencyEnabled;
}

void ofxVlc4::VideoComponent::setTeletextTransparencyEnabled(bool enabled) {
	if (owner.teletextTransparencyEnabled == enabled) {
		return;
	}

	owner.teletextTransparencyEnabled = enabled;
	applyTeletextSettings();
	media().refreshSubtitleStateInfo();
}

void ofxVlc4::VideoComponent::sendTeletextKey(TeletextKey key) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_teletext(player, static_cast<int>(toLibvlcTeletextKey(key)));
}

bool ofxVlc4::VideoComponent::isKeyInputEnabled() const {
	return owner.keyInputEnabled;
}

void ofxVlc4::VideoComponent::setKeyInputEnabled(bool enabled) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	setInputHandlingEnabled(
		player,
		owner.keyInputEnabled,
		enabled,
		"Video key input ",
		libvlc_video_set_key_input,
		[](const std::string & message) { ofxVlc4::logVerbose(message); });
}

bool ofxVlc4::VideoComponent::isMouseInputEnabled() const {
	return owner.mouseInputEnabled;
}

void ofxVlc4::VideoComponent::setMouseInputEnabled(bool enabled) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	setInputHandlingEnabled(
		player,
		owner.mouseInputEnabled,
		enabled,
		"Video mouse input ",
		libvlc_video_set_mouse_input,
		[](const std::string & message) { ofxVlc4::logVerbose(message); });
}

bool ofxVlc4::VideoComponent::isVlcFullscreenEnabled() const {
	return owner.vlcFullscreenEnabled;
}

void ofxVlc4::VideoComponent::setVlcFullscreenEnabled(bool enabled) {
	if (owner.vlcFullscreenEnabled == enabled) {
		return;
	}

	owner.vlcFullscreenEnabled = enabled;
	applyVlcFullscreen();
	owner.logNotice(std::string("libVLC fullscreen ") + (owner.vlcFullscreenEnabled ? "enabled." : "disabled."));
}

void ofxVlc4::VideoComponent::toggleVlcFullscreen() {
	setVlcFullscreenEnabled(!owner.vlcFullscreenEnabled);
}

bool ofxVlc4::VideoComponent::isVideoTitleDisplayEnabled() const {
	return owner.videoTitleDisplayEnabled;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayEnabled(bool enabled) {
	if (owner.videoTitleDisplayEnabled == enabled) {
		return;
	}

	owner.videoTitleDisplayEnabled = enabled;
	applyVideoTitleDisplay();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getVideoTitleDisplayPosition() const {
	return owner.videoTitleDisplayPosition;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayPosition(OverlayPosition position) {
	if (owner.videoTitleDisplayPosition == position) {
		return;
	}

	owner.videoTitleDisplayPosition = position;
	applyVideoTitleDisplay();
}

unsigned ofxVlc4::VideoComponent::getVideoTitleDisplayTimeoutMs() const {
	return owner.videoTitleDisplayTimeoutMs;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayTimeoutMs(unsigned timeoutMs) {
	const unsigned clampedTimeoutMs = std::min(timeoutMs, 60000u);
	if (owner.videoTitleDisplayTimeoutMs == clampedTimeoutMs) {
		return;
	}

	owner.videoTitleDisplayTimeoutMs = clampedTimeoutMs;
	applyVideoTitleDisplay();
}

bool ofxVlc4::VideoComponent::getCursorPosition(int & x, int & y) const {
	x = 0;
	y = 0;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	libvlc_media_t * sourceMedia = libvlc_media_player_get_media(player);
	if (!sourceMedia) {
		return false;
	}
	libvlc_media_release(sourceMedia);

	return libvlc_video_get_cursor(player, 0u, &x, &y) == 0;
}

bool ofxVlc4::VideoComponent::isVideoAdjustmentsEnabled() const {
	return owner.videoAdjustmentsEnabled;
}

void ofxVlc4::VideoComponent::setVideoAdjustmentsEnabled(bool enabled) {
	if (owner.videoAdjustmentsEnabled == enabled) {
		return;
	}

	owner.videoAdjustmentsEnabled = enabled;
	applyOrQueueVideoAdjustments();
	owner.setStatus(std::string("Video adjustments ") + (enabled ? "enabled." : "disabled."));
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::VideoComponent::getVideoAdjustmentEngine() const {
	return owner.videoAdjustmentEngine;
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::VideoComponent::getActiveVideoAdjustmentEngine() const {
	return owner.activeVideoAdjustmentEngine;
}

void ofxVlc4::VideoComponent::setVideoAdjustmentEngine(ofxVlc4::VideoAdjustmentEngine engine) {
	const ofxVlc4::VideoAdjustmentEngine normalizedEngine = engine;
	if (owner.videoAdjustmentEngine == normalizedEngine) {
		return;
	}

	owner.videoAdjustmentEngine = normalizedEngine;
	applyOrQueueVideoAdjustments();
	owner.setStatus(std::string("Video adjustment engine: ") + videoAdjustmentEngineLabel(owner.activeVideoAdjustmentEngine) + ".");
}

float ofxVlc4::VideoComponent::getVideoContrast() const {
	return owner.videoAdjustContrast;
}

void ofxVlc4::VideoComponent::setVideoContrast(float contrast) {
	const float clampedContrast = ofClamp(contrast, 0.0f, 4.0f);
	if (std::abs(owner.videoAdjustContrast - clampedContrast) < 0.0001f) {
		return;
	}

	owner.videoAdjustContrast = clampedContrast;
	owner.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoBrightness() const {
	return owner.videoAdjustBrightness;
}

void ofxVlc4::VideoComponent::setVideoBrightness(float brightness) {
	const float clampedBrightness = ofClamp(brightness, 0.0f, 4.0f);
	if (std::abs(owner.videoAdjustBrightness - clampedBrightness) < 0.0001f) {
		return;
	}

	owner.videoAdjustBrightness = clampedBrightness;
	owner.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoHue() const {
	return owner.videoAdjustHue;
}

void ofxVlc4::VideoComponent::setVideoHue(float hue) {
	const float wrappedHue = std::fmod(hue, 360.0f);
	const float normalizedHue = wrappedHue < 0.0f ? (wrappedHue + 360.0f) : wrappedHue;
	if (std::abs(owner.videoAdjustHue - normalizedHue) < 0.0001f) {
		return;
	}

	owner.videoAdjustHue = normalizedHue;
	owner.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoSaturation() const {
	return owner.videoAdjustSaturation;
}

void ofxVlc4::VideoComponent::setVideoSaturation(float saturation) {
	const float clampedSaturation = ofClamp(saturation, 0.0f, 4.0f);
	if (std::abs(owner.videoAdjustSaturation - clampedSaturation) < 0.0001f) {
		return;
	}

	owner.videoAdjustSaturation = clampedSaturation;
	owner.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoGamma() const {
	return owner.videoAdjustGamma;
}

void ofxVlc4::VideoComponent::setVideoGamma(float gamma) {
	const float clampedGamma = ofClamp(gamma, 0.01f, 10.0f);
	if (std::abs(owner.videoAdjustGamma - clampedGamma) < 0.0001f) {
		return;
	}

	owner.videoAdjustGamma = clampedGamma;
	owner.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

ofxVlc4::VideoDeinterlaceMode ofxVlc4::VideoComponent::getVideoDeinterlaceMode() const {
	return owner.videoDeinterlaceMode;
}

void ofxVlc4::VideoComponent::setVideoDeinterlaceMode(VideoDeinterlaceMode mode) {
	if (owner.videoDeinterlaceMode == mode) {
		return;
	}

	owner.videoDeinterlaceMode = mode;
	applyVideoDeinterlace();
	owner.setStatus("Video deinterlace set.");
	owner.logNotice(std::string("Video deinterlace: ") + videoDeinterlaceModeLabel(mode) + ".");
}

ofxVlc4::VideoAspectRatioMode ofxVlc4::VideoComponent::getVideoAspectRatioMode() const {
	return owner.videoAspectRatioMode;
}

void ofxVlc4::VideoComponent::setVideoAspectRatioMode(VideoAspectRatioMode mode) {
	if (owner.videoAspectRatioMode == mode) {
		return;
	}

	owner.videoAspectRatioMode = mode;
	applyVideoAspectRatio();
	owner.setStatus("Video aspect ratio set.");
	owner.logNotice(std::string("Video aspect ratio: ") + videoAspectRatioLabel(mode) + ".");
}

ofxVlc4::VideoCropMode ofxVlc4::VideoComponent::getVideoCropMode() const {
	return owner.videoCropMode;
}

void ofxVlc4::VideoComponent::setVideoCropMode(VideoCropMode mode) {
	if (owner.videoCropMode == mode) {
		return;
	}

	owner.videoCropMode = mode;
	applyVideoCrop();
	owner.setStatus("Video crop set.");
	owner.logNotice(std::string("Video crop: ") + videoCropLabel(mode) + ".");
}

ofxVlc4::VideoDisplayFitMode ofxVlc4::VideoComponent::getVideoDisplayFitMode() const {
	return owner.videoDisplayFitMode;
}

void ofxVlc4::VideoComponent::setVideoDisplayFitMode(VideoDisplayFitMode mode) {
	if (owner.videoDisplayFitMode == mode) {
		return;
	}

	owner.videoDisplayFitMode = mode;
	applyVideoScaleAndFit();
	owner.setStatus("Video fit set.");
	owner.logNotice("Video fit set.");
}

ofxVlc4::VideoOutputBackend ofxVlc4::VideoComponent::getVideoOutputBackend() const {
	return owner.videoOutputBackend;
}

ofxVlc4::VideoOutputBackend ofxVlc4::VideoComponent::getActiveVideoOutputBackend() const {
	return owner.activeVideoOutputBackend;
}

ofxVlc4::PreferredDecoderDevice ofxVlc4::VideoComponent::getPreferredDecoderDevice() const {
	return owner.preferredDecoderDevice;
}

void ofxVlc4::VideoComponent::setVideoOutputBackend(VideoOutputBackend backend) {
	if (owner.videoOutputBackend == backend) {
		return;
	}

	owner.videoOutputBackend = backend;
	clearVideoHdrMetadata();

	if (owner.sessionPlayer()) {
		owner.logWarning(std::string("Video output backend changes apply on the next player initialization: ") +
			videoOutputBackendLabel(backend) + ".");
		owner.setStatus("Video output backend updated for the next init.");
		return;
	}

	updateNativeVideoWindowVisibility();
}

void ofxVlc4::VideoComponent::setPreferredDecoderDevice(PreferredDecoderDevice device) {
	if (owner.preferredDecoderDevice == device) {
		return;
	}

	owner.preferredDecoderDevice = device;
	if (owner.sessionPlayer()) {
		owner.logWarning(std::string("Preferred decoder hardware changes apply on the next player initialization: ") +
			preferredDecoderDeviceLabel(device) + ".");
		owner.setStatus("Preferred decoder hardware updated for the next init.");
	}
}

void ofxVlc4::VideoComponent::resetVideoAdjustments() {
	owner.videoAdjustmentsEnabled = true;
	owner.videoAdjustContrast = 1.0f;
	owner.videoAdjustBrightness = 1.0f;
	owner.videoAdjustHue = 0.0f;
	owner.videoAdjustSaturation = 1.0f;
	owner.videoAdjustGamma = 1.0f;
	applyOrQueueVideoAdjustments();
	owner.setStatus("Video adjustments reset.");
	owner.logNotice("Video adjustments reset.");
}

std::vector<ofxVlc4::VideoFilterInfo> ofxVlc4::VideoComponent::getVideoFilters() const {
	std::vector<VideoFilterInfo> filters;
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return filters;
	}

	libvlc_module_description_t * filterList = libvlc_video_filter_list_get(instance);
	for (libvlc_module_description_t * filter = filterList; filter != nullptr; filter = filter->p_next) {
		VideoFilterInfo info;
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

std::string ofxVlc4::VideoComponent::getVideoFilterChain() const {
	return owner.videoFilterChain;
}

void ofxVlc4::VideoComponent::setVideoFilterChain(const std::string & filterChain) {
	owner.videoFilterChain = trimWhitespace(filterChain);
	if (!owner.canApplyNativeVideoFilters()) {
		if (owner.videoFilterChain.empty()) {
			owner.setStatus("Video filter chain cleared. NativeWindow backend is required to apply video filters.");
			owner.logNotice("Video filter chain cleared.");
		} else {
			owner.setStatus("Video filter chain stored. Switch to NativeWindow backend and reload/play media to apply.");
			owner.logNotice("Video filter chain stored: " + owner.videoFilterChain + ".");
		}
		return;
	}

	if (owner.videoFilterChain.empty()) {
		if (media().reapplyCurrentMediaForFilterChainChange("Video")) {
			owner.logNotice("Video filter chain cleared.");
			return;
		}
		owner.setStatus("Video filter chain cleared. Reload media to apply.");
		owner.logNotice("Video filter chain cleared.");
		return;
	}

	if (media().reapplyCurrentMediaForFilterChainChange("Video")) {
		owner.logNotice("Video filter chain: " + owner.videoFilterChain + ".");
		return;
	}

	owner.setStatus("Video filter chain set. Reload media to apply.");
	owner.logNotice("Video filter chain: " + owner.videoFilterChain + ".");
}

unsigned ofxVlc4::VideoComponent::getVideoOutputCount() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return 0u;
	}
	return owner.playbackController->getCachedVideoOutputCount();
}

bool ofxVlc4::VideoComponent::hasVideoOutput() const {
	return getVideoOutputCount() > 0u;
}

bool ofxVlc4::VideoComponent::isScrambled() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	libvlc_media_t * sourceMedia = libvlc_media_player_get_media(player);
	if (!sourceMedia) {
		return false;
	}

	libvlc_media_release(sourceMedia);
	return libvlc_media_player_program_scrambled(player);
}

bool ofxVlc4::VideoComponent::queryVideoTrackGeometry(unsigned & width, unsigned & height, unsigned & sarNum, unsigned & sarDen) const {
	width = 0;
	height = 0;
	sarNum = 1;
	sarDen = 1;

	libvlc_media_player_t * player = owner.sessionPlayer();
	libvlc_media_t * currentMedia = owner.sessionMedia();
	if (!player) {
		return false;
	}

	auto applyVideoTrackGeometry = [&](const libvlc_media_track_t * track) {
		if (!track || track->i_type != libvlc_track_video || !track->video) {
			return false;
		}

		if (track->video->i_width == 0 || track->video->i_height == 0) {
			return false;
		}

		width = track->video->i_width;
		height = track->video->i_height;
		sarNum = track->video->i_sar_num > 0 ? track->video->i_sar_num : 1u;
		sarDen = track->video->i_sar_den > 0 ? track->video->i_sar_den : 1u;
		return true;
	};

	libvlc_media_track_t * selectedTrack =
		libvlc_media_player_get_selected_track(player, libvlc_track_video);
	if (selectedTrack) {
		const bool foundSelectedTrack = applyVideoTrackGeometry(selectedTrack);
		libvlc_media_track_release(selectedTrack);
		if (foundSelectedTrack) {
			return true;
		}
	}

	auto readTracklistGeometry = [&](bool selectedOnly) {
		libvlc_media_tracklist_t * tracklist =
			libvlc_media_player_get_tracklist(player, libvlc_track_video, selectedOnly);
		if (!tracklist) {
			return false;
		}

		const size_t trackCount = libvlc_media_tracklist_count(tracklist);
		for (size_t i = 0; i < trackCount; ++i) {
			const libvlc_media_track_t * track = libvlc_media_tracklist_at(tracklist, i);
			if (applyVideoTrackGeometry(track)) {
				libvlc_media_tracklist_delete(tracklist);
				return true;
			}
		}

		libvlc_media_tracklist_delete(tracklist);
		return false;
	};

	if (readTracklistGeometry(true) || readTracklistGeometry(false)) {
		return true;
	}

	if (currentMedia) {
		libvlc_media_tracklist_t * mediaTracklist = libvlc_media_get_tracklist(currentMedia, libvlc_track_video);
		if (mediaTracklist) {
			const size_t trackCount = libvlc_media_tracklist_count(mediaTracklist);
			for (size_t i = 0; i < trackCount; ++i) {
				const libvlc_media_track_t * track = libvlc_media_tracklist_at(mediaTracklist, i);
				if (applyVideoTrackGeometry(track)) {
					libvlc_media_tracklist_delete(mediaTracklist);
					return true;
				}
			}
			libvlc_media_tracklist_delete(mediaTracklist);
		}
	}

	return false;
}

void ofxVlc4::VideoComponent::refreshPixelAspectRatio() {
	unsigned trackWidth = 0;
	unsigned trackHeight = 0;
	unsigned sarNum = 1;
	unsigned sarDen = 1;
	if (queryVideoTrackGeometry(trackWidth, trackHeight, sarNum, sarDen)) {
		owner.pixelAspectNumerator.store(sarNum);
		owner.pixelAspectDenominator.store(sarDen);
		return;
	}

	owner.pixelAspectNumerator.store(1);
	owner.pixelAspectDenominator.store(1);
}

void ofxVlc4::VideoComponent::refreshDisplayAspectRatio() {
	const unsigned currentVideoWidth = owner.videoWidth.load();
	const unsigned currentVideoHeight = owner.videoHeight.load();
	if (currentVideoWidth == 0 || currentVideoHeight == 0) {
		owner.displayAspectRatio.store(1.0f);
		return;
	}

	refreshPixelAspectRatio();

	float aspect = static_cast<float>(currentVideoWidth) / static_cast<float>(currentVideoHeight);
	unsigned trackWidth = 0;
	unsigned trackHeight = 0;
	unsigned ignoredSarNum = 1;
	unsigned ignoredSarDen = 1;
	if (queryVideoTrackGeometry(trackWidth, trackHeight, ignoredSarNum, ignoredSarDen)) {
		if (trackWidth > 0 && trackHeight > 0) {
			aspect = static_cast<float>(trackWidth) / static_cast<float>(trackHeight);
		}
	}

	const unsigned sarNum = owner.pixelAspectNumerator.load();
	const unsigned sarDen = owner.pixelAspectDenominator.load();
	if (sarNum > 0 && sarDen > 0) {
		aspect *= static_cast<float>(sarNum) / static_cast<float>(sarDen);
	}

	owner.displayAspectRatio.store(std::max(aspect, 0.0001f));
}

ofTexture & ofxVlc4::VideoComponent::getTexture() {
	if (owner.exposedTextureDirty.exchange(false)) {
		refreshExposedTexture();
	}

	return owner.exposedTextureFbo.getTexture();
}

ofTexture & ofxVlc4::VideoComponent::getRenderTexture() {
	return owner.videoTexture;
}

ofxVlc4::VideoStateInfo ofxVlc4::VideoComponent::getVideoStateInfo() const {
	VideoStateInfo state;
	state.startupPrepared = owner.startupPlaybackStatePrepared.load();
	state.loaded = owner.isVideoLoaded.load();
	state.frameReceived = owner.hasReceivedVideoFrame.load();
	state.trackCount = std::max(0, owner.cachedVideoTrackCount.load());
	state.tracksAvailable = state.trackCount > 0;
	state.sourceWidth = owner.videoWidth.load();
	state.sourceHeight = owner.videoHeight.load();
	state.renderWidth = owner.renderWidth.load();
	state.renderHeight = owner.renderHeight.load();
	state.geometryKnown = state.sourceWidth > 0 &&
		state.sourceHeight > 0 &&
		state.renderWidth > 0 &&
		state.renderHeight > 0;
	state.pixelAspectNumerator = owner.pixelAspectNumerator.load();
	state.pixelAspectDenominator = owner.pixelAspectDenominator.load();
	state.displayAspectRatio = owner.displayAspectRatio.load();
	state.videoOutputCount = getVideoOutputCount();
	state.hasVideoOutput = state.videoOutputCount > 0u;
	state.videoAdjustmentsEnabled = owner.videoAdjustmentsEnabled;
	state.videoAdjustmentEngine = owner.videoAdjustmentEngine;
	state.activeVideoAdjustmentEngine = owner.activeVideoAdjustmentEngine;
	state.vlcFullscreenEnabled = owner.vlcFullscreenEnabled;
	state.teletextTransparencyEnabled = owner.teletextTransparencyEnabled;
	state.teletextPage = owner.teletextPage;
	state.scale = owner.videoScale;
	state.yaw = owner.videoViewYaw;
	state.pitch = owner.videoViewPitch;
	state.roll = owner.videoViewRoll;
	state.fov = owner.videoViewFov;
	state.deinterlaceMode = owner.videoDeinterlaceMode;
	state.aspectRatioMode = owner.videoAspectRatioMode;
	state.cropMode = owner.videoCropMode;
	state.displayFitMode = owner.videoDisplayFitMode;
	state.outputBackend = owner.videoOutputBackend;
	state.activeOutputBackend = owner.activeVideoOutputBackend;
	state.preferredDecoderDevice = owner.preferredDecoderDevice;
	state.projectionMode = owner.videoProjectionMode;
	state.stereoMode = owner.videoStereoMode;
	state.hdrMetadata = getVideoHdrMetadata();
	return state;
}

float ofxVlc4::VideoComponent::getHeight() const {
	return static_cast<float>(getVideoStateInfo().sourceHeight);
}

float ofxVlc4::VideoComponent::getWidth() const {
	const VideoStateInfo state = getVideoStateInfo();
	const float rawWidth = static_cast<float>(state.sourceWidth);
	const float rawHeight = static_cast<float>(state.sourceHeight);
	if (rawWidth <= 0.0f || rawHeight <= 0.0f) {
		return rawWidth;
	}

	const float aspect = std::max(state.displayAspectRatio, 0.0001f);
	return rawHeight * aspect;
}

void ofxVlc4::applyVideoInputHandling() {
	videoComponent->applyVideoInputHandling();
}

void ofxVlc4::applyVideoTitleDisplay() {
	videoComponent->applyVideoTitleDisplay();
}

void ofxVlc4::applyVideoAdjustments() {
	videoComponent->applyVideoAdjustments();
}

void ofxVlc4::applyVideoDeinterlace() {
	videoComponent->applyVideoDeinterlace();
}

void ofxVlc4::applyVideoAspectRatio() {
	videoComponent->applyVideoAspectRatio();
}

void ofxVlc4::applyVideoCrop() {
	videoComponent->applyVideoCrop();
}

void ofxVlc4::applyVideoScaleAndFit() {
	videoComponent->applyVideoScaleAndFit();
}

void ofxVlc4::clearVideoHdrMetadata() {
	videoComponent->clearVideoHdrMetadata();
}

void ofxVlc4::prepareStartupVideoResources() {
	videoComponent->prepareStartupVideoResources();
}

void ofxVlc4::releaseD3D11Resources() {
	videoComponent->releaseD3D11Resources();
}

bool ofxVlc4::usesTextureVideoOutput() const {
	return videoComponent->usesTextureVideoOutput();
}

void ofxVlc4::updateNativeVideoWindowVisibility() {
	videoComponent->updateNativeVideoWindowVisibility();
}

bool ofxVlc4::applyVideoOutputBackend() {
	return videoComponent->applyVideoOutputBackend();
}

void ofxVlc4::applyTeletextSettings() {
	videoComponent->applyTeletextSettings();
}

void ofxVlc4::applyVideoMarquee() {
	videoComponent->applyVideoMarquee();
}

void ofxVlc4::applyVideoLogo() {
	videoComponent->applyVideoLogo();
}

void ofxVlc4::applyVideoProjectionMode() {
	videoComponent->applyVideoProjectionMode();
}

void ofxVlc4::applyVideoStereoMode() {
	videoComponent->applyVideoStereoMode();
}

void ofxVlc4::applyVideoViewpoint(bool absolute) {
	videoComponent->applyVideoViewpoint(absolute);
}

void ofxVlc4::resetVideoAdjustments() {
	videoComponent->resetVideoAdjustments();
}

ofxVlc4::VideoDeinterlaceMode ofxVlc4::getVideoDeinterlaceMode() const {
	return videoComponent->getVideoDeinterlaceMode();
}

void ofxVlc4::setVideoDeinterlaceMode(VideoDeinterlaceMode mode) {
	videoComponent->setVideoDeinterlaceMode(mode);
}

ofxVlc4::VideoAspectRatioMode ofxVlc4::getVideoAspectRatioMode() const {
	return videoComponent->getVideoAspectRatioMode();
}

void ofxVlc4::setVideoAspectRatioMode(VideoAspectRatioMode mode) {
	videoComponent->setVideoAspectRatioMode(mode);
}

ofxVlc4::VideoCropMode ofxVlc4::getVideoCropMode() const {
	return videoComponent->getVideoCropMode();
}

void ofxVlc4::setVideoCropMode(VideoCropMode mode) {
	videoComponent->setVideoCropMode(mode);
}

ofxVlc4::VideoDisplayFitMode ofxVlc4::getVideoDisplayFitMode() const {
	return videoComponent->getVideoDisplayFitMode();
}

void ofxVlc4::setVideoDisplayFitMode(VideoDisplayFitMode mode) {
	videoComponent->setVideoDisplayFitMode(mode);
}

float ofxVlc4::getVideoScale() const {
	return videoComponent->getVideoScale();
}

void ofxVlc4::setVideoScale(float scale) {
	videoComponent->setVideoScale(scale);
}

int ofxVlc4::getTeletextPage() const {
	return videoComponent->getTeletextPage();
}

void ofxVlc4::setTeletextPage(int page) {
	videoComponent->setTeletextPage(page);
}

bool ofxVlc4::isTeletextTransparencyEnabled() const {
	return videoComponent->isTeletextTransparencyEnabled();
}

void ofxVlc4::setTeletextTransparencyEnabled(bool enabled) {
	videoComponent->setTeletextTransparencyEnabled(enabled);
}

void ofxVlc4::sendTeletextKey(TeletextKey key) {
	videoComponent->sendTeletextKey(key);
}

bool ofxVlc4::isKeyInputEnabled() const {
	return videoComponent->isKeyInputEnabled();
}

void ofxVlc4::setKeyInputEnabled(bool enabled) {
	videoComponent->setKeyInputEnabled(enabled);
}

bool ofxVlc4::isMouseInputEnabled() const {
	return videoComponent->isMouseInputEnabled();
}

void ofxVlc4::setMouseInputEnabled(bool enabled) {
	videoComponent->setMouseInputEnabled(enabled);
}

bool ofxVlc4::isVlcFullscreenEnabled() const {
	return videoComponent->isVlcFullscreenEnabled();
}

void ofxVlc4::setVlcFullscreenEnabled(bool enabled) {
	videoComponent->setVlcFullscreenEnabled(enabled);
}

void ofxVlc4::toggleVlcFullscreen() {
	videoComponent->toggleVlcFullscreen();
}

bool ofxVlc4::isVideoTitleDisplayEnabled() const {
	return videoComponent->isVideoTitleDisplayEnabled();
}

void ofxVlc4::setVideoTitleDisplayEnabled(bool enabled) {
	videoComponent->setVideoTitleDisplayEnabled(enabled);
}

ofxVlc4::OverlayPosition ofxVlc4::getVideoTitleDisplayPosition() const {
	return videoComponent->getVideoTitleDisplayPosition();
}

void ofxVlc4::setVideoTitleDisplayPosition(OverlayPosition position) {
	videoComponent->setVideoTitleDisplayPosition(position);
}

unsigned ofxVlc4::getVideoTitleDisplayTimeoutMs() const {
	return videoComponent->getVideoTitleDisplayTimeoutMs();
}

void ofxVlc4::setVideoTitleDisplayTimeoutMs(unsigned timeoutMs) {
	videoComponent->setVideoTitleDisplayTimeoutMs(timeoutMs);
}

bool ofxVlc4::getCursorPosition(int & x, int & y) const {
	return videoComponent->getCursorPosition(x, y);
}

bool ofxVlc4::isVideoAdjustmentsEnabled() const {
	return videoComponent->isVideoAdjustmentsEnabled();
}

void ofxVlc4::setVideoAdjustmentsEnabled(bool enabled) {
	videoComponent->setVideoAdjustmentsEnabled(enabled);
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::getVideoAdjustmentEngine() const {
	return videoComponent->getVideoAdjustmentEngine();
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::getActiveVideoAdjustmentEngine() const {
	return videoComponent->getActiveVideoAdjustmentEngine();
}

void ofxVlc4::setVideoAdjustmentEngine(ofxVlc4::VideoAdjustmentEngine engine) {
	videoComponent->setVideoAdjustmentEngine(engine);
}

float ofxVlc4::getVideoContrast() const {
	return videoComponent->getVideoContrast();
}

void ofxVlc4::setVideoContrast(float contrast) {
	videoComponent->setVideoContrast(contrast);
}

float ofxVlc4::getVideoBrightness() const {
	return videoComponent->getVideoBrightness();
}

void ofxVlc4::setVideoBrightness(float brightness) {
	videoComponent->setVideoBrightness(brightness);
}

float ofxVlc4::getVideoHue() const {
	return videoComponent->getVideoHue();
}

void ofxVlc4::setVideoHue(float hue) {
	videoComponent->setVideoHue(hue);
}

float ofxVlc4::getVideoSaturation() const {
	return videoComponent->getVideoSaturation();
}

void ofxVlc4::setVideoSaturation(float saturation) {
	videoComponent->setVideoSaturation(saturation);
}

float ofxVlc4::getVideoGamma() const {
	return videoComponent->getVideoGamma();
}

void ofxVlc4::setVideoGamma(float gamma) {
	videoComponent->setVideoGamma(gamma);
}

bool ofxVlc4::isMarqueeEnabled() const {
	return videoComponent->isMarqueeEnabled();
}

void ofxVlc4::setMarqueeEnabled(bool enabled) {
	videoComponent->setMarqueeEnabled(enabled);
}

std::string ofxVlc4::getMarqueeText() const {
	return videoComponent->getMarqueeText();
}

void ofxVlc4::setMarqueeText(const std::string & text) {
	videoComponent->setMarqueeText(text);
}

ofxVlc4::OverlayPosition ofxVlc4::getMarqueePosition() const {
	return videoComponent->getMarqueePosition();
}

void ofxVlc4::setMarqueePosition(OverlayPosition position) {
	videoComponent->setMarqueePosition(position);
}

int ofxVlc4::getMarqueeOpacity() const {
	return videoComponent->getMarqueeOpacity();
}

void ofxVlc4::setMarqueeOpacity(int opacity) {
	videoComponent->setMarqueeOpacity(opacity);
}

int ofxVlc4::getMarqueeSize() const {
	return videoComponent->getMarqueeSize();
}

void ofxVlc4::setMarqueeSize(int size) {
	videoComponent->setMarqueeSize(size);
}

int ofxVlc4::getMarqueeColor() const {
	return videoComponent->getMarqueeColor();
}

void ofxVlc4::setMarqueeColor(int color) {
	videoComponent->setMarqueeColor(color);
}

int ofxVlc4::getMarqueeRefresh() const {
	return videoComponent->getMarqueeRefresh();
}

void ofxVlc4::setMarqueeRefresh(int refreshMs) {
	videoComponent->setMarqueeRefresh(refreshMs);
}

int ofxVlc4::getMarqueeTimeout() const {
	return videoComponent->getMarqueeTimeout();
}

void ofxVlc4::setMarqueeTimeout(int timeoutMs) {
	videoComponent->setMarqueeTimeout(timeoutMs);
}

int ofxVlc4::getMarqueeX() const {
	return videoComponent->getMarqueeX();
}

void ofxVlc4::setMarqueeX(int x) {
	videoComponent->setMarqueeX(x);
}

int ofxVlc4::getMarqueeY() const {
	return videoComponent->getMarqueeY();
}

void ofxVlc4::setMarqueeY(int y) {
	videoComponent->setMarqueeY(y);
}

bool ofxVlc4::isLogoEnabled() const {
	return videoComponent->isLogoEnabled();
}

void ofxVlc4::setLogoEnabled(bool enabled) {
	videoComponent->setLogoEnabled(enabled);
}

std::string ofxVlc4::getLogoPath() const {
	return videoComponent->getLogoPath();
}

void ofxVlc4::setLogoPath(const std::string & path) {
	videoComponent->setLogoPath(path);
}

ofxVlc4::OverlayPosition ofxVlc4::getLogoPosition() const {
	return videoComponent->getLogoPosition();
}

void ofxVlc4::setLogoPosition(OverlayPosition position) {
	videoComponent->setLogoPosition(position);
}

int ofxVlc4::getLogoOpacity() const {
	return videoComponent->getLogoOpacity();
}

void ofxVlc4::setLogoOpacity(int opacity) {
	videoComponent->setLogoOpacity(opacity);
}

int ofxVlc4::getLogoX() const {
	return videoComponent->getLogoX();
}

void ofxVlc4::setLogoX(int x) {
	videoComponent->setLogoX(x);
}

int ofxVlc4::getLogoY() const {
	return videoComponent->getLogoY();
}

void ofxVlc4::setLogoY(int y) {
	videoComponent->setLogoY(y);
}

int ofxVlc4::getLogoDelay() const {
	return videoComponent->getLogoDelay();
}

void ofxVlc4::setLogoDelay(int delayMs) {
	videoComponent->setLogoDelay(delayMs);
}

int ofxVlc4::getLogoRepeat() const {
	return videoComponent->getLogoRepeat();
}

void ofxVlc4::setLogoRepeat(int repeat) {
	videoComponent->setLogoRepeat(repeat);
}

ofxVlc4::VideoProjectionMode ofxVlc4::getVideoProjectionMode() const {
	return videoComponent->getVideoProjectionMode();
}

void ofxVlc4::setVideoProjectionMode(VideoProjectionMode mode) {
	videoComponent->setVideoProjectionMode(mode);
}

ofxVlc4::VideoStereoMode ofxVlc4::getVideoStereoMode() const {
	return videoComponent->getVideoStereoMode();
}

void ofxVlc4::setVideoStereoMode(VideoStereoMode mode) {
	videoComponent->setVideoStereoMode(mode);
}

float ofxVlc4::getVideoYaw() const {
	return videoComponent->getVideoYaw();
}

float ofxVlc4::getVideoPitch() const {
	return videoComponent->getVideoPitch();
}

float ofxVlc4::getVideoRoll() const {
	return videoComponent->getVideoRoll();
}

float ofxVlc4::getVideoFov() const {
	return videoComponent->getVideoFov();
}

void ofxVlc4::setVideoViewpoint(float yaw, float pitch, float roll, float fov, bool absolute) {
	videoComponent->setVideoViewpoint(yaw, pitch, roll, fov, absolute);
}

void ofxVlc4::resetVideoViewpoint() {
	videoComponent->resetVideoViewpoint();
}

float ofxVlc4::getHeight() const {
	return videoComponent->getHeight();
}

float ofxVlc4::getWidth() const {
	return videoComponent->getWidth();
}

void ofxVlc4::ensureVideoRenderTargetCapacity(unsigned requiredWidth, unsigned requiredHeight) {
	videoComponent->ensureVideoRenderTargetCapacity(requiredWidth, requiredHeight);
}

void ofxVlc4::ensureExposedTextureFboCapacity(unsigned requiredWidth, unsigned requiredHeight) {
	videoComponent->ensureExposedTextureFboCapacity(requiredWidth, requiredHeight);
}

bool ofxVlc4::applyPendingVideoResize() {
	return videoComponent->applyPendingVideoResize();
}

bool ofxVlc4::videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg) {
	ofxVlc4 * that = static_cast<ofxVlc4 *>(data);
	if (!that) {
		return false;
	}
	return that->videoComponent->videoResize(cfg, render_cfg);
}

void ofxVlc4::videoSwap(void * data) {
	ofxVlc4 * that = static_cast<ofxVlc4 *>(data);
	if (!that) {
		return;
	}
	that->videoComponent->videoSwap();
}

bool ofxVlc4::make_current(void * data, bool current) {
	auto * that = static_cast<ofxVlc4 *>(data);
	if (!that) {
		return false;
	}
	return that->videoComponent->makeCurrent(current);
}

void * ofxVlc4::get_proc_address(void * data, const char * name) {
	auto * that = static_cast<ofxVlc4 *>(data);
	if (!that) {
		return nullptr;
	}

	return that->videoComponent->getProcAddress(name);
}

bool ofxVlc4::videoOutputSetup(
	void ** data,
	const libvlc_video_setup_device_cfg_t * cfg,
	libvlc_video_setup_device_info_t * out) {
#ifdef TARGET_WIN32
	auto * that = (data && *data) ? static_cast<ofxVlc4 *>(*data) : nullptr;
	if (!that) {
		return false;
	}
	return that->videoComponent->videoOutputSetup(cfg, out);
#else
	(void)data;
	(void)cfg;
	(void)out;
	return false;
#endif
}

void ofxVlc4::videoOutputCleanup(void * data) {
	auto * that = static_cast<ofxVlc4 *>(data);
	if (!that) {
		return;
	}
	that->videoComponent->videoOutputCleanup();
}

void ofxVlc4::videoFrameMetadata(void * data, libvlc_video_metadata_type_t type, const void * metadata) {
	auto * that = static_cast<ofxVlc4 *>(data);
	if (!that) {
		return;
	}
	that->videoComponent->videoFrameMetadata(type, metadata);
}

void ofxVlc4::bindVlcRenderTarget() {
	videoComponent->bindVlcRenderTarget();
}

void ofxVlc4::unbindVlcRenderTarget() {
	videoComponent->unbindVlcRenderTarget();
}

bool ofxVlc4::drawCurrentFrame(const VideoStateInfo & state, float x, float y, float width, float height) {
	return videoComponent->drawCurrentFrame(state, x, y, width, height);
}

void ofxVlc4::draw(float x, float y, float w, float h) {
	videoComponent->draw(x, y, w, h);
}

void ofxVlc4::draw(float x, float y) {
	videoComponent->draw(x, y);
}

void ofxVlc4::refreshExposedTexture() {
	videoComponent->refreshExposedTexture();
}

ofTexture & ofxVlc4::getTexture() {
	return videoComponent->getTexture();
}

ofTexture & ofxVlc4::getRenderTexture() {
	return videoComponent->getRenderTexture();
}

ofxVlc4::VideoOutputBackend ofxVlc4::getVideoOutputBackend() const {
	return videoComponent->getVideoOutputBackend();
}

ofxVlc4::VideoOutputBackend ofxVlc4::getActiveVideoOutputBackend() const {
	return videoComponent->getActiveVideoOutputBackend();
}

ofxVlc4::PreferredDecoderDevice ofxVlc4::getPreferredDecoderDevice() const {
	return videoComponent->getPreferredDecoderDevice();
}

ofxVlc4::VideoHdrMetadataInfo ofxVlc4::getVideoHdrMetadata() const {
	return videoComponent->getVideoHdrMetadata();
}

ofxVlc4::VideoStateInfo ofxVlc4::getVideoStateInfo() const {
	return videoComponent->getVideoStateInfo();
}

std::vector<ofxVlc4::VideoFilterInfo> ofxVlc4::getVideoFilters() const {
	return videoComponent->getVideoFilters();
}

std::string ofxVlc4::getVideoFilterChain() const {
	return videoComponent->getVideoFilterChain();
}

void ofxVlc4::setVideoFilterChain(const std::string & filterChain) {
	videoComponent->setVideoFilterChain(filterChain);
}

bool ofxVlc4::canApplyNativeVideoFilters() const {
	return videoOutputBackend == VideoOutputBackend::NativeWindow;
}

std::vector<std::string> ofxVlc4::getVideoFilterChainEntries() const {
	return parseFilterChainEntries(getVideoFilterChain());
}

void ofxVlc4::setVideoFilters(const std::vector<std::string> & filters) {
	setVideoFilterChain(joinFilterChainEntries(filters));
}

bool ofxVlc4::hasVideoFilter(const std::string & filterName) const {
	const std::string target = trimWhitespace(filterName);
	if (target.empty()) {
		return false;
	}
	const std::vector<std::string> filters = getVideoFilterChainEntries();
	return std::find(filters.begin(), filters.end(), target) != filters.end();
}

bool ofxVlc4::addVideoFilter(const std::string & filterName) {
	const std::string target = trimWhitespace(filterName);
	if (target.empty()) {
		return false;
	}
	std::vector<std::string> filters = getVideoFilterChainEntries();
	if (std::find(filters.begin(), filters.end(), target) != filters.end()) {
		return false;
	}
	filters.push_back(target);
	setVideoFilters(filters);
	return true;
}

bool ofxVlc4::removeVideoFilter(const std::string & filterName) {
	const std::string target = trimWhitespace(filterName);
	if (target.empty()) {
		return false;
	}
	std::vector<std::string> filters = getVideoFilterChainEntries();
	const auto it = std::remove(filters.begin(), filters.end(), target);
	if (it == filters.end()) {
		return false;
	}
	filters.erase(it, filters.end());
	setVideoFilters(filters);
	return true;
}

bool ofxVlc4::toggleVideoFilter(const std::string & filterName) {
	return hasVideoFilter(filterName) ? removeVideoFilter(filterName) : addVideoFilter(filterName);
}

void ofxVlc4::setVideoOutputBackend(VideoOutputBackend backend) {
	videoComponent->setVideoOutputBackend(backend);
}

void ofxVlc4::setPreferredDecoderDevice(PreferredDecoderDevice device) {
	videoComponent->setPreferredDecoderDevice(device);
}

bool ofxVlc4::queryVideoTrackGeometry(unsigned & width, unsigned & height, unsigned & sarNum, unsigned & sarDen) const {
	return videoComponent->queryVideoTrackGeometry(width, height, sarNum, sarDen);
}

void ofxVlc4::refreshPixelAspectRatio() {
	videoComponent->refreshPixelAspectRatio();
}

void ofxVlc4::refreshDisplayAspectRatio() {
	videoComponent->refreshDisplayAspectRatio();
}
