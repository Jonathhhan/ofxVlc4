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
	return *owner.subsystemRuntime.mediaComponent;
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
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
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

	libvlc_set_fullscreen(player, owner.videoPresentationRuntime.vlcFullscreenEnabled);
}

void ofxVlc4::VideoComponent::applyVideoInputHandling() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_key_input(player, owner.playerConfigRuntime.keyInputEnabled ? 1u : 0u);
	libvlc_video_set_mouse_input(player, owner.playerConfigRuntime.mouseInputEnabled ? 1u : 0u);
}

void ofxVlc4::VideoComponent::applyVideoTitleDisplay() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_set_video_title_display(
		player,
		owner.videoPresentationRuntime.videoTitleDisplayEnabled ? toLibvlcVideoTitlePosition(owner.videoPresentationRuntime.videoTitleDisplayPosition) : libvlc_position_disable,
		owner.videoPresentationRuntime.videoTitleDisplayTimeoutMs);
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::VideoComponent::resolveActiveVideoAdjustmentEngine() const {
	switch (owner.effectsRuntime.videoAdjustmentEngine) {
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
	return owner.effectsRuntime.videoAdjustmentsEnabled &&
		resolveActiveVideoAdjustmentEngine() == ofxVlc4::VideoAdjustmentEngine::Shader;
}

void ofxVlc4::VideoComponent::ensureVideoAdjustShaderLoaded() {
	if (owner.videoResourceRuntime.videoAdjustShaderReady || owner.lifecycleRuntime.shuttingDown.load()) {
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

	owner.videoResourceRuntime.videoAdjustShader.setupShaderFromSource(GL_VERTEX_SHADER, resolvedVertexShaderSource);
	owner.videoResourceRuntime.videoAdjustShader.setupShaderFromSource(GL_FRAGMENT_SHADER, resolvedFragmentShaderSource);
	owner.videoResourceRuntime.videoAdjustShader.bindDefaults();
	owner.videoResourceRuntime.videoAdjustShaderReady = owner.videoResourceRuntime.videoAdjustShader.linkProgram();
}

void ofxVlc4::VideoComponent::applyVideoAdjustments() {
	const ofxVlc4::VideoAdjustmentEngine activeEngine = resolveActiveVideoAdjustmentEngine();
	owner.effectsRuntime.activeVideoAdjustmentEngine = activeEngine;

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (activeEngine == ofxVlc4::VideoAdjustmentEngine::Shader) {
		if (player) {
			libvlc_video_set_adjust_int(player, libvlc_adjust_Enable, 0);
		}
		owner.videoFrameRuntime.exposedTextureDirty.store(true);
		return;
	}

	if (!player) {
		return;
	}

	if (!owner.effectsRuntime.videoAdjustmentsEnabled) {
		libvlc_video_set_adjust_int(player, libvlc_adjust_Enable, 0);
		return;
	}

	libvlc_video_set_adjust_float(player, libvlc_adjust_Contrast, owner.effectsRuntime.videoAdjustContrast);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Brightness, owner.effectsRuntime.videoAdjustBrightness);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Hue, owner.effectsRuntime.videoAdjustHue);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Saturation, owner.effectsRuntime.videoAdjustSaturation);
	libvlc_video_set_adjust_float(player, libvlc_adjust_Gamma, owner.effectsRuntime.videoAdjustGamma);
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
	owner.playbackPolicyRuntime.pendingVideoAdjustApplyOnPlay.store(pending);
}

void ofxVlc4::VideoComponent::clearPendingVideoAdjustApplyOnPlay() {
	setPendingVideoAdjustApplyOnPlay(false);
}

bool ofxVlc4::VideoComponent::hasPendingVideoAdjustApplyOnPlay() const {
	return owner.playbackPolicyRuntime.pendingVideoAdjustApplyOnPlay.load();
}

void ofxVlc4::VideoComponent::applyVideoDeinterlace() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (libvlc_video_set_deinterlace(
			player,
			videoDeinterlaceState(owner.videoPresentationRuntime.videoDeinterlaceMode),
			videoDeinterlaceFilterName(owner.videoPresentationRuntime.videoDeinterlaceMode)) != 0) {
		owner.logWarning("Video deinterlace could not be applied.");
	}
}

void ofxVlc4::VideoComponent::applyVideoAspectRatio() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_aspect_ratio(player, videoAspectRatioValue(owner.videoPresentationRuntime.videoAspectRatioMode));
}

void ofxVlc4::VideoComponent::applyVideoCrop() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	const auto [numerator, denominator] = videoCropRatio(owner.videoPresentationRuntime.videoCropMode);
	libvlc_video_set_crop_ratio(player, numerator, denominator);
}

void ofxVlc4::VideoComponent::applyVideoScaleAndFit() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_display_fit(player, toLibvlcVideoFitMode(owner.videoPresentationRuntime.videoDisplayFitMode));
	if (owner.videoPresentationRuntime.videoDisplayFitMode == VideoDisplayFitMode::Scale) {
		libvlc_video_set_scale(player, owner.videoPresentationRuntime.videoScale);
	} else {
		libvlc_video_set_scale(player, 0.0f);
	}
}

void ofxVlc4::VideoComponent::applyVideoProjectionMode() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (owner.videoPresentationRuntime.videoProjectionMode == VideoProjectionMode::Auto) {
		libvlc_video_unset_projection_mode(player);
		return;
	}

	libvlc_video_set_projection_mode(player, toLibvlcProjectionMode(owner.videoPresentationRuntime.videoProjectionMode));
}

void ofxVlc4::VideoComponent::applyVideoStereoMode() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_video_stereo_mode(player, toLibvlcStereoMode(owner.videoPresentationRuntime.videoStereoMode));
}

void ofxVlc4::VideoComponent::applyVideoViewpoint(bool absolute) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_viewpoint_t viewpoint {};
	viewpoint.f_yaw = owner.videoPresentationRuntime.videoViewYaw;
	viewpoint.f_pitch = owner.videoPresentationRuntime.videoViewPitch;
	viewpoint.f_roll = owner.videoPresentationRuntime.videoViewRoll;
	viewpoint.f_field_of_view = owner.videoPresentationRuntime.videoViewFov;
	libvlc_video_update_viewpoint(player, &viewpoint, absolute);
}

void ofxVlc4::VideoComponent::clearVideoHdrMetadata() {
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
	owner.analysisRuntime.videoHdrMetadata = {};
	owner.analysisRuntime.videoHdrMetadata.supported = owner.videoPresentationRuntime.videoOutputBackend == VideoOutputBackend::D3D11Metadata ||
		owner.videoPresentationRuntime.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata;
}

void ofxVlc4::VideoComponent::releaseD3D11Resources() {
#ifdef TARGET_WIN32
	if (owner.videoResourceRuntime.d3d11RenderTargetView) {
		owner.videoResourceRuntime.d3d11RenderTargetView->Release();
		owner.videoResourceRuntime.d3d11RenderTargetView = nullptr;
	}
	if (owner.videoResourceRuntime.d3d11RenderTexture) {
		owner.videoResourceRuntime.d3d11RenderTexture->Release();
		owner.videoResourceRuntime.d3d11RenderTexture = nullptr;
	}
	if (owner.videoResourceRuntime.d3d11Multithread) {
		owner.videoResourceRuntime.d3d11Multithread->Release();
		owner.videoResourceRuntime.d3d11Multithread = nullptr;
	}
	if (owner.videoResourceRuntime.d3d11DeviceContext) {
		owner.videoResourceRuntime.d3d11DeviceContext->Release();
		owner.videoResourceRuntime.d3d11DeviceContext = nullptr;
	}
	if (owner.videoResourceRuntime.d3d11Device) {
		owner.videoResourceRuntime.d3d11Device->Release();
		owner.videoResourceRuntime.d3d11Device = nullptr;
	}
	owner.videoResourceRuntime.d3d11RenderDxgiFormat = 0;
#endif
}

void ofxVlc4::VideoComponent::updateNativeVideoWindowVisibility() {
	if (!owner.videoResourceRuntime.vlcWindow) {
		return;
	}

	GLFWwindow * glfwWindow = owner.videoResourceRuntime.vlcWindow->getGLFWWindow();
	if (!glfwWindow) {
		return;
	}

	if (owner.videoPresentationRuntime.activeVideoOutputBackend == VideoOutputBackend::NativeWindow && owner.sessionPlayer()) {
		owner.videoResourceRuntime.vlcWindow->setWindowTitle("ofxVlc4 Native Video");
		owner.videoResourceRuntime.vlcWindow->setWindowShape(960, 540);
		owner.videoResourceRuntime.vlcWindow->setWindowPosition(560, 24);
		glfwShowWindow(glfwWindow);
	} else {
		glfwHideWindow(glfwWindow);
	}
}

bool ofxVlc4::VideoComponent::usesTextureVideoOutput() const {
	return owner.videoPresentationRuntime.activeVideoOutputBackend == VideoOutputBackend::Texture;
}

bool ofxVlc4::VideoComponent::applyVideoOutputBackend() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	clearVideoHdrMetadata();
	releaseD3D11Resources();
	updateNativeVideoWindowVisibility();

	if (owner.videoPresentationRuntime.videoOutputBackend == VideoOutputBackend::Texture) {
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
		owner.videoPresentationRuntime.activeVideoOutputBackend = VideoOutputBackend::Texture;
		updateNativeVideoWindowVisibility();
		return true;
	}

	if (owner.videoPresentationRuntime.videoOutputBackend == VideoOutputBackend::D3D11Metadata) {
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
		owner.videoPresentationRuntime.activeVideoOutputBackend = VideoOutputBackend::D3D11Metadata;
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
	if (!owner.videoResourceRuntime.vlcWindow || !owner.videoResourceRuntime.vlcWindow->getGLFWWindow()) {
		owner.setError("Native video window is unavailable.");
		return false;
	}

	void * hwnd = glfwGetWin32Window(owner.videoResourceRuntime.vlcWindow->getGLFWWindow());
	if (!hwnd) {
		owner.setError("Native window handle is unavailable.");
		return false;
	}

	libvlc_media_player_set_hwnd(player, hwnd);
	owner.videoPresentationRuntime.activeVideoOutputBackend = VideoOutputBackend::NativeWindow;
	updateNativeVideoWindowVisibility();
	return true;
#elif defined(TARGET_LINUX)
	if (!owner.videoResourceRuntime.vlcWindow || !owner.videoResourceRuntime.vlcWindow->getGLFWWindow()) {
		owner.setError("Native video window is unavailable.");
		return false;
	}

	Window xwindow = glfwGetX11Window(owner.videoResourceRuntime.vlcWindow->getGLFWWindow());
	if (xwindow == 0) {
		owner.setError("X11 native window handle is unavailable.");
		return false;
	}

	libvlc_media_player_set_xwindow(player, static_cast<uint32_t>(xwindow));
	owner.videoPresentationRuntime.activeVideoOutputBackend = VideoOutputBackend::NativeWindow;
	updateNativeVideoWindowVisibility();
	return true;
#elif defined(TARGET_OSX)
	if (!owner.videoResourceRuntime.vlcWindow || !owner.videoResourceRuntime.vlcWindow->getGLFWWindow()) {
		owner.setError("Native video window is unavailable.");
		return false;
	}

	id cocoaWindow = glfwGetCocoaWindow(owner.videoResourceRuntime.vlcWindow->getGLFWWindow());
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
	owner.videoPresentationRuntime.activeVideoOutputBackend = VideoOutputBackend::NativeWindow;
	updateNativeVideoWindowVisibility();
	return true;
#else
	owner.setError("Native window video output is only supported on Windows, Cocoa, and X11.");
	return false;
#endif
}

void ofxVlc4::VideoComponent::prepareStartupVideoResources() {
	if (owner.videoPresentationRuntime.activeVideoOutputBackend != VideoOutputBackend::Texture) {
		return;
	}

	unsigned width = 0;
	unsigned height = 0;
	unsigned sarNum = 1;
	unsigned sarDen = 1;
	if (!queryVideoTrackGeometry(width, height, sarNum, sarDen) || width == 0 || height == 0) {
		return;
	}

	const int glPixelFormat = owner.videoGeometryRuntime.pendingGlPixelFormat.load();

	owner.videoGeometryRuntime.pixelAspectNumerator.store(sarNum > 0 ? sarNum : 1u);
	owner.videoGeometryRuntime.pixelAspectDenominator.store(sarDen > 0 ? sarDen : 1u);
	owner.videoGeometryRuntime.renderWidth.store(width);
	owner.videoGeometryRuntime.renderHeight.store(height);
	owner.videoGeometryRuntime.videoWidth.store(width);
	owner.videoGeometryRuntime.videoHeight.store(height);
	refreshDisplayAspectRatio();
	ensureVideoRenderTargetCapacity(width, height, glPixelFormat);
	ensureExposedTextureFboCapacity(width, height, glPixelFormat);
	owner.videoFrameRuntime.exposedTextureDirty.store(true);
}

void ofxVlc4::VideoComponent::applyTeletextSettings() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_teletext_transparency(player, owner.videoPresentationRuntime.teletextTransparencyEnabled);
	libvlc_video_set_teletext(player, owner.videoPresentationRuntime.teletextPage);
}

void ofxVlc4::VideoComponent::applyVideoMarquee() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_marquee_int(player, libvlc_marquee_Enable, owner.overlayRuntime.marqueeEnabled ? 1 : 0);
	if (!owner.overlayRuntime.marqueeEnabled) {
		return;
	}

	libvlc_video_set_marquee_string(player, libvlc_marquee_Text, owner.overlayRuntime.marqueeText.c_str());
	libvlc_video_set_marquee_int(player, libvlc_marquee_Position, toLibvlcOverlayPosition(owner.overlayRuntime.marqueePosition));
	libvlc_video_set_marquee_int(player, libvlc_marquee_Opacity, owner.overlayRuntime.marqueeOpacity);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Size, owner.overlayRuntime.marqueeSize);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Color, owner.overlayRuntime.marqueeColor);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Refresh, owner.overlayRuntime.marqueeRefresh);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Timeout, owner.overlayRuntime.marqueeTimeout);
	libvlc_video_set_marquee_int(player, libvlc_marquee_X, owner.overlayRuntime.marqueeX);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Y, owner.overlayRuntime.marqueeY);
}

void ofxVlc4::VideoComponent::applyVideoLogo() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	const std::string resolvedLogoPath = normalizeOptionalPath(owner.overlayRuntime.logoPath);
	const bool canEnableLogo = owner.overlayRuntime.logoEnabled && !trimWhitespace(resolvedLogoPath).empty();
	libvlc_video_set_logo_int(player, libvlc_logo_enable, canEnableLogo ? 1 : 0);
	if (!canEnableLogo) {
		return;
	}

	libvlc_video_set_logo_string(player, libvlc_logo_file, resolvedLogoPath.c_str());
	libvlc_video_set_logo_int(player, libvlc_logo_position, toLibvlcOverlayPosition(owner.overlayRuntime.logoPosition));
	libvlc_video_set_logo_int(player, libvlc_logo_opacity, owner.overlayRuntime.logoOpacity);
	libvlc_video_set_logo_int(player, libvlc_logo_x, owner.overlayRuntime.logoX);
	libvlc_video_set_logo_int(player, libvlc_logo_y, owner.overlayRuntime.logoY);
	libvlc_video_set_logo_int(player, libvlc_logo_delay, owner.overlayRuntime.logoDelay);
	libvlc_video_set_logo_int(player, libvlc_logo_repeat, owner.overlayRuntime.logoRepeat);
}

void ofxVlc4::VideoComponent::ensureVideoRenderTargetCapacity(unsigned requiredWidth, unsigned requiredHeight, int glPixelFormat) {
	if (requiredWidth == 0 || requiredHeight == 0 || owner.lifecycleRuntime.shuttingDown.load()) {
		return;
	}

	const bool formatChanged = glPixelFormat != owner.videoGeometryRuntime.allocatedGlPixelFormat;
	if (!owner.videoResourceRuntime.videoTexture.isAllocated() || requiredWidth > owner.videoGeometryRuntime.allocatedVideoWidth || requiredHeight > owner.videoGeometryRuntime.allocatedVideoHeight || formatChanged) {
		clearPublishedFrameFenceLocked();
		owner.videoGeometryRuntime.allocatedVideoWidth = std::max(owner.videoGeometryRuntime.allocatedVideoWidth, requiredWidth);
		owner.videoGeometryRuntime.allocatedVideoHeight = std::max(owner.videoGeometryRuntime.allocatedVideoHeight, requiredHeight);
		if (formatChanged) {
			owner.videoGeometryRuntime.allocatedGlPixelFormat = glPixelFormat;
		}
		owner.videoResourceRuntime.videoTexture.clear();
		owner.videoResourceRuntime.videoTexture.allocate(owner.videoGeometryRuntime.allocatedVideoWidth, owner.videoGeometryRuntime.allocatedVideoHeight, owner.videoGeometryRuntime.allocatedGlPixelFormat);
		owner.videoResourceRuntime.videoTexture.getTextureData().bFlipTexture = true;
		{
			const GLenum texTarget = owner.videoResourceRuntime.videoTexture.getTextureData().textureTarget;
			const GLuint texId = owner.videoResourceRuntime.videoTexture.getTextureData().textureID;
			glBindTexture(texTarget, texId);
			glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(texTarget, 0);
		}
		if (owner.videoResourceRuntime.vlcFramebufferId == 0) {
			glGenFramebuffers(1, &owner.videoResourceRuntime.vlcFramebufferId);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, owner.videoResourceRuntime.vlcFramebufferId);
		glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			owner.videoResourceRuntime.videoTexture.getTextureData().textureTarget,
			owner.videoResourceRuntime.videoTexture.getTextureData().textureID,
			0);
		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		ofClear(0, 0, 0, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		owner.videoFrameRuntime.vlcFramebufferAttachmentDirty.store(true);
	}
}

void ofxVlc4::VideoComponent::ensureExposedTextureFboCapacity(unsigned requiredWidth, unsigned requiredHeight, int glPixelFormat) {
	if (requiredWidth == 0 || requiredHeight == 0 || owner.lifecycleRuntime.shuttingDown.load()) {
		return;
	}

	const unsigned currentWidth =
		owner.videoResourceRuntime.exposedTextureFbo.isAllocated() ? static_cast<unsigned>(owner.videoResourceRuntime.exposedTextureFbo.getWidth()) : 0u;
	const unsigned currentHeight =
		owner.videoResourceRuntime.exposedTextureFbo.isAllocated() ? static_cast<unsigned>(owner.videoResourceRuntime.exposedTextureFbo.getHeight()) : 0u;
	const unsigned targetWidth = std::max(currentWidth, requiredWidth);
	const unsigned targetHeight = std::max(currentHeight, requiredHeight);

	const bool formatMismatch = owner.videoResourceRuntime.exposedTextureFbo.isAllocated() &&
		static_cast<int>(owner.videoResourceRuntime.exposedTextureFbo.getTexture().getTextureData().glInternalFormat) != glPixelFormat;

	if (!owner.videoResourceRuntime.exposedTextureFbo.isAllocated() ||
		targetWidth != currentWidth ||
		targetHeight != currentHeight ||
		formatMismatch) {
		owner.videoResourceRuntime.exposedTextureFbo.allocate(targetWidth, targetHeight, glPixelFormat);
		owner.videoResourceRuntime.exposedTextureFbo.getTexture().getTextureData().bFlipTexture = true;
		clearAllocatedFbo(owner.videoResourceRuntime.exposedTextureFbo);
	}
}

bool ofxVlc4::VideoComponent::applyPendingVideoResize() {
	if (!owner.videoGeometryRuntime.pendingResize.exchange(false)) {
		return false;
	}

	const unsigned newRenderWidth = owner.videoGeometryRuntime.pendingRenderWidth.load();
	const unsigned newRenderHeight = owner.videoGeometryRuntime.pendingRenderHeight.load();
	if (newRenderWidth == 0 || newRenderHeight == 0) {
		return false;
	}

	const int newGlPixelFormat = owner.videoGeometryRuntime.pendingGlPixelFormat.load();

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
	owner.videoGeometryRuntime.renderWidth.store(newRenderWidth);
	owner.videoGeometryRuntime.renderHeight.store(newRenderHeight);
	owner.videoGeometryRuntime.videoWidth.store(visibleWidth);
	owner.videoGeometryRuntime.videoHeight.store(visibleHeight);
	refreshDisplayAspectRatio();
	ensureVideoRenderTargetCapacity(newRenderWidth, newRenderHeight, newGlPixelFormat);
	owner.videoFrameRuntime.isVideoLoaded.store(true);
	owner.videoFrameRuntime.exposedTextureDirty.store(true);
	return true;
}

bool ofxVlc4::VideoComponent::videoResize(const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg) {
	if (!cfg || !render_cfg) {
		return false;
	}

	if (owner.videoPresentationRuntime.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata) {
#ifdef TARGET_WIN32
		const DXGI_FORMAT dxgiFormat = (cfg->bitdepth > 8) ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;

		{
			std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
			owner.analysisRuntime.videoHdrMetadata.supported = true;
			owner.analysisRuntime.videoHdrMetadata.width = cfg->width;
			owner.analysisRuntime.videoHdrMetadata.height = cfg->height;
			owner.analysisRuntime.videoHdrMetadata.bitDepth = cfg->bitdepth;
			owner.analysisRuntime.videoHdrMetadata.fullRange = cfg->full_range;
			owner.analysisRuntime.videoHdrMetadata.colorspace = cfg->colorspace;
			owner.analysisRuntime.videoHdrMetadata.primaries = cfg->primaries;
			owner.analysisRuntime.videoHdrMetadata.transfer = cfg->transfer;
			owner.analysisRuntime.videoHdrMetadata.available = false;

			if (!owner.videoResourceRuntime.d3d11Device) {
				return false;
			}

			if (!owner.videoResourceRuntime.d3d11RenderTexture ||
				!owner.videoResourceRuntime.d3d11RenderTargetView ||
				owner.videoGeometryRuntime.renderWidth.load() != cfg->width ||
				owner.videoGeometryRuntime.renderHeight.load() != cfg->height ||
				owner.videoResourceRuntime.d3d11RenderDxgiFormat != static_cast<int>(dxgiFormat)) {
				if (owner.videoResourceRuntime.d3d11RenderTargetView) {
					owner.videoResourceRuntime.d3d11RenderTargetView->Release();
					owner.videoResourceRuntime.d3d11RenderTargetView = nullptr;
				}
				if (owner.videoResourceRuntime.d3d11RenderTexture) {
					owner.videoResourceRuntime.d3d11RenderTexture->Release();
					owner.videoResourceRuntime.d3d11RenderTexture = nullptr;
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

				if (FAILED(owner.videoResourceRuntime.d3d11Device->CreateTexture2D(&textureDesc, nullptr, &owner.videoResourceRuntime.d3d11RenderTexture)) ||
					FAILED(owner.videoResourceRuntime.d3d11Device->CreateRenderTargetView(owner.videoResourceRuntime.d3d11RenderTexture, nullptr, &owner.videoResourceRuntime.d3d11RenderTargetView))) {
					releaseD3D11Resources();
					return false;
				}

				owner.videoResourceRuntime.d3d11RenderDxgiFormat = static_cast<int>(dxgiFormat);
			}
		}

		owner.videoGeometryRuntime.renderWidth.store(cfg->width);
		owner.videoGeometryRuntime.renderHeight.store(cfg->height);
		owner.videoGeometryRuntime.videoWidth.store(cfg->width);
		owner.videoGeometryRuntime.videoHeight.store(cfg->height);
		refreshDisplayAspectRatio();
		owner.videoFrameRuntime.isVideoLoaded.store(true);
		owner.videoFrameRuntime.exposedTextureDirty.store(true);

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

	if (cfg->width != owner.videoGeometryRuntime.renderWidth.load() || cfg->height != owner.videoGeometryRuntime.renderHeight.load() ||
		glPixelFormat != owner.videoGeometryRuntime.pendingGlPixelFormat.load()) {
		owner.videoGeometryRuntime.pendingGlPixelFormat.store(glPixelFormat);
		owner.videoGeometryRuntime.pendingRenderWidth.store(cfg->width);
		owner.videoGeometryRuntime.pendingRenderHeight.store(cfg->height);
		owner.videoGeometryRuntime.pendingResize.store(true);
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
			&owner.videoResourceRuntime.d3d11Device,
			&createdFeatureLevel,
			&owner.videoResourceRuntime.d3d11DeviceContext);
		if (hr == E_INVALIDARG) {
			hr = D3D11CreateDevice(
				nullptr,
				driverType,
				nullptr,
				creationFlags,
				featureLevels + 1,
				static_cast<UINT>((sizeof(featureLevels) / sizeof(featureLevels[0])) - 1),
				D3D11_SDK_VERSION,
				&owner.videoResourceRuntime.d3d11Device,
				&createdFeatureLevel,
				&owner.videoResourceRuntime.d3d11DeviceContext);
		}
		if (SUCCEEDED(hr)) {
			break;
		}
	}

	if (FAILED(hr) || !owner.videoResourceRuntime.d3d11Device || !owner.videoResourceRuntime.d3d11DeviceContext) {
		releaseD3D11Resources();
		owner.setError("D3D11 device creation failed.");
		return false;
	}

	(void)createdFeatureLevel;

	owner.videoResourceRuntime.d3d11DeviceContext->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void **>(&owner.videoResourceRuntime.d3d11Multithread));
	if (owner.videoResourceRuntime.d3d11Multithread) {
		owner.videoResourceRuntime.d3d11Multithread->SetMultithreadProtected(TRUE);
	}

	out->d3d11.device_context = owner.videoResourceRuntime.d3d11DeviceContext;
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
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
	owner.analysisRuntime.videoHdrMetadata.supported = true;
	owner.analysisRuntime.videoHdrMetadata.available = true;
	owner.analysisRuntime.videoHdrMetadata.redPrimaryX = hdr10->RedPrimary[0];
	owner.analysisRuntime.videoHdrMetadata.redPrimaryY = hdr10->RedPrimary[1];
	owner.analysisRuntime.videoHdrMetadata.greenPrimaryX = hdr10->GreenPrimary[0];
	owner.analysisRuntime.videoHdrMetadata.greenPrimaryY = hdr10->GreenPrimary[1];
	owner.analysisRuntime.videoHdrMetadata.bluePrimaryX = hdr10->BluePrimary[0];
	owner.analysisRuntime.videoHdrMetadata.bluePrimaryY = hdr10->BluePrimary[1];
	owner.analysisRuntime.videoHdrMetadata.whitePointX = hdr10->WhitePoint[0];
	owner.analysisRuntime.videoHdrMetadata.whitePointY = hdr10->WhitePoint[1];
	owner.analysisRuntime.videoHdrMetadata.maxMasteringLuminance = hdr10->MaxMasteringLuminance;
	owner.analysisRuntime.videoHdrMetadata.minMasteringLuminance = hdr10->MinMasteringLuminance;
	owner.analysisRuntime.videoHdrMetadata.maxContentLightLevel = hdr10->MaxContentLightLevel;
	owner.analysisRuntime.videoHdrMetadata.maxFrameAverageLightLevel = hdr10->MaxFrameAverageLightLevel;
}

void ofxVlc4::VideoComponent::bindVlcRenderTarget() {
	if (!owner.videoResourceRuntime.videoTexture.isAllocated() || owner.videoResourceRuntime.vlcFramebufferId == 0) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, owner.videoResourceRuntime.vlcFramebufferId);
	if (owner.videoFrameRuntime.vlcFramebufferAttachmentDirty.load()) {
		glFramebufferTexture2D(
			GL_FRAMEBUFFER,
			GL_COLOR_ATTACHMENT0,
			owner.videoResourceRuntime.videoTexture.getTextureData().textureTarget,
			owner.videoResourceRuntime.videoTexture.getTextureData().textureID,
			0);
		owner.videoFrameRuntime.vlcFramebufferAttachmentDirty.store(false);
	}
	const unsigned currentRenderWidth = owner.videoGeometryRuntime.renderWidth.load();
	const unsigned currentRenderHeight = owner.videoGeometryRuntime.renderHeight.load();
	if (currentRenderWidth > 0 &&
		currentRenderHeight > 0 &&
		(currentRenderWidth != owner.videoGeometryRuntime.lastBoundViewportWidth || currentRenderHeight != owner.videoGeometryRuntime.lastBoundViewportHeight)) {
		ofViewport(0, 0, static_cast<float>(currentRenderWidth), static_cast<float>(currentRenderHeight), false);
		owner.videoGeometryRuntime.lastBoundViewportWidth = currentRenderWidth;
		owner.videoGeometryRuntime.lastBoundViewportHeight = currentRenderHeight;
	}
	owner.videoFrameRuntime.vlcFboBound = true;
}

void ofxVlc4::VideoComponent::unbindVlcRenderTarget() {
	if (!owner.videoFrameRuntime.vlcFboBound) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	owner.videoFrameRuntime.vlcFboBound = false;
}

void ofxVlc4::VideoComponent::videoSwap() {
	if (owner.lifecycleRuntime.shuttingDown.load()) {
		return;
	}

	owner.videoFrameRuntime.hasReceivedVideoFrame.store(true);

	if (owner.videoPresentationRuntime.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata) {
		return;
	}

	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
	const bool needsPublish = !owner.videoFrameRuntime.exposedTextureDirty.exchange(true);
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
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);

	if (owner.videoPresentationRuntime.activeVideoOutputBackend == VideoOutputBackend::D3D11Metadata) {
#ifdef TARGET_WIN32
		if (!owner.videoResourceRuntime.d3d11DeviceContext) {
			return false;
		}

		if (current) {
			if (!owner.videoResourceRuntime.d3d11RenderTargetView) {
				return false;
			}

			ID3D11RenderTargetView * renderTarget = owner.videoResourceRuntime.d3d11RenderTargetView;
			owner.videoResourceRuntime.d3d11DeviceContext->OMSetRenderTargets(1, &renderTarget, nullptr);

			D3D11_VIEWPORT viewport {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(std::max(1u, owner.videoGeometryRuntime.renderWidth.load()));
			viewport.Height = static_cast<float>(std::max(1u, owner.videoGeometryRuntime.renderHeight.load()));
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			owner.videoResourceRuntime.d3d11DeviceContext->RSSetViewports(1, &viewport);
		} else {
			ID3D11RenderTargetView * renderTarget = nullptr;
			owner.videoResourceRuntime.d3d11DeviceContext->OMSetRenderTargets(1, &renderTarget, nullptr);
		}
		return true;
#else
		return false;
#endif
	}

	if (!owner.videoResourceRuntime.vlcWindow || !owner.videoResourceRuntime.vlcWindow->getGLFWWindow()) {
		return false;
	}

	if (current) {
		owner.videoResourceRuntime.vlcWindow->makeCurrent();
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
	if (!state.frameReceived || !owner.videoResourceRuntime.videoTexture.isAllocated() || sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
		return false;
	}

	if (usesShaderVideoAdjustments()) {
		if (owner.videoFrameRuntime.exposedTextureDirty.exchange(false)) {
			refreshExposedTextureLocked(state);
		}
		owner.videoResourceRuntime.exposedTextureFbo.getTexture().drawSubsection(x, y, width, height, 0, 0, sourceWidth, sourceHeight);
		return true;
	}

	owner.videoResourceRuntime.videoTexture.drawSubsection(x, y, width, height, 0, 0, sourceWidth, sourceHeight);
	return true;
}

void ofxVlc4::VideoComponent::refreshExposedTextureLocked(const VideoStateInfo & state) {
	const auto [sourceWidth, sourceHeight] = visibleVideoSourceSize(state);
	const int glPixelFormat = owner.videoGeometryRuntime.allocatedGlPixelFormat;
	if (sourceWidth > 0 && sourceHeight > 0 && !state.frameReceived) {
		ensureExposedTextureFboCapacity(sourceWidth, sourceHeight, glPixelFormat);
		clearAllocatedFbo(owner.videoResourceRuntime.exposedTextureFbo);
		return;
	}

	if (!owner.videoResourceRuntime.videoTexture.isAllocated() || sourceWidth == 0 || sourceHeight == 0) {
		return;
	}

	waitForPublishedFrameFenceLocked();
	ensureExposedTextureFboCapacity(sourceWidth, sourceHeight, glPixelFormat);
	const bool fullFboOverwrite =
		owner.videoResourceRuntime.exposedTextureFbo.isAllocated() &&
		static_cast<unsigned>(owner.videoResourceRuntime.exposedTextureFbo.getWidth()) == sourceWidth &&
		static_cast<unsigned>(owner.videoResourceRuntime.exposedTextureFbo.getHeight()) == sourceHeight;
	owner.videoResourceRuntime.exposedTextureFbo.begin();
	if (!fullFboOverwrite) {
		ofClear(0, 0, 0, 255);
	}
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255, 255, 255, 255);
	if (usesShaderVideoAdjustments()) {
		ensureVideoAdjustShaderLoaded();
		if (owner.videoResourceRuntime.videoAdjustShaderReady) {
			owner.videoResourceRuntime.videoAdjustShader.begin();
			owner.videoResourceRuntime.videoAdjustShader.setUniformTexture("tex0", owner.videoResourceRuntime.videoTexture, 0);
			owner.videoResourceRuntime.videoAdjustShader.setUniform1f("brightness", owner.effectsRuntime.videoAdjustBrightness);
			owner.videoResourceRuntime.videoAdjustShader.setUniform1f("contrast", owner.effectsRuntime.videoAdjustContrast);
			owner.videoResourceRuntime.videoAdjustShader.setUniform1f("saturation", owner.effectsRuntime.videoAdjustSaturation);
			owner.videoResourceRuntime.videoAdjustShader.setUniform1f("gammaValue", owner.effectsRuntime.videoAdjustGamma);
			owner.videoResourceRuntime.videoAdjustShader.setUniform1f("hueDegrees", owner.effectsRuntime.videoAdjustHue);
		}
	}
	owner.videoResourceRuntime.videoTexture.drawSubsection(
		0.0f,
		0.0f,
		static_cast<float>(sourceWidth),
		static_cast<float>(sourceHeight),
		0.0f,
		0.0f,
		static_cast<float>(sourceWidth),
		static_cast<float>(sourceHeight));
	if (usesShaderVideoAdjustments() && owner.videoResourceRuntime.videoAdjustShaderReady) {
		owner.videoResourceRuntime.videoAdjustShader.end();
	}
	ofPopStyle();
	owner.videoResourceRuntime.exposedTextureFbo.end();
}

void ofxVlc4::VideoComponent::refreshExposedTexture() {
	const VideoStateInfo state = getVideoStateInfo();
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
	refreshExposedTextureLocked(state);
}

void ofxVlc4::VideoComponent::draw(float x, float y, float width, float height) {
	if (!usesTextureVideoOutput()) {
		return;
	}
	const VideoStateInfo state = getVideoStateInfo();
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
	drawCurrentFrame(state, x, y, width, height);
}

void ofxVlc4::VideoComponent::draw(float x, float y) {
	if (!usesTextureVideoOutput()) {
		return;
	}
	const VideoStateInfo state = getVideoStateInfo();
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
	const float displayHeight = static_cast<float>(state.sourceHeight);
	const float displayWidth =
		(displayHeight > 0.0f) ? (displayHeight * std::max(state.displayAspectRatio, 0.0001f)) : static_cast<float>(state.sourceWidth);
	drawCurrentFrame(state, x, y, displayWidth, displayHeight);
}

ofxVlc4::VideoHdrMetadataInfo ofxVlc4::VideoComponent::getVideoHdrMetadata() const {
	std::lock_guard<std::mutex> lock(owner.synchronizationRuntime.videoMutex);
	return owner.analysisRuntime.videoHdrMetadata;
}

float ofxVlc4::VideoComponent::getVideoScale() const {
	return owner.videoPresentationRuntime.videoScale;
}

void ofxVlc4::VideoComponent::setVideoScale(float scale) {
	const float clampedScale = ofClamp(scale, 0.25f, 4.0f);
	if (std::abs(owner.videoPresentationRuntime.videoScale - clampedScale) < 0.0001f) {
		return;
	}

	owner.videoPresentationRuntime.videoScale = clampedScale;
	owner.videoPresentationRuntime.videoDisplayFitMode = VideoDisplayFitMode::Scale;
	applyVideoScaleAndFit();
	owner.setStatus("Video scale set.");
	owner.logNotice("Video scale: " + ofToString(owner.videoPresentationRuntime.videoScale, 2) + "x.");
}

ofxVlc4::VideoProjectionMode ofxVlc4::VideoComponent::getVideoProjectionMode() const {
	return owner.videoPresentationRuntime.videoProjectionMode;
}

void ofxVlc4::VideoComponent::setVideoProjectionMode(VideoProjectionMode mode) {
	if (owner.videoPresentationRuntime.videoProjectionMode == mode) {
		return;
	}

	owner.videoPresentationRuntime.videoProjectionMode = mode;
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
	return owner.videoPresentationRuntime.videoStereoMode;
}

void ofxVlc4::VideoComponent::setVideoStereoMode(VideoStereoMode mode) {
	if (owner.videoPresentationRuntime.videoStereoMode == mode) {
		return;
	}

	owner.videoPresentationRuntime.videoStereoMode = mode;
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
	return owner.videoPresentationRuntime.videoViewYaw;
}

float ofxVlc4::VideoComponent::getVideoPitch() const {
	return owner.videoPresentationRuntime.videoViewPitch;
}

float ofxVlc4::VideoComponent::getVideoRoll() const {
	return owner.videoPresentationRuntime.videoViewRoll;
}

float ofxVlc4::VideoComponent::getVideoFov() const {
	return owner.videoPresentationRuntime.videoViewFov;
}

void ofxVlc4::VideoComponent::setVideoViewpoint(float yaw, float pitch, float roll, float fov, bool absolute) {
	owner.videoPresentationRuntime.videoViewYaw = ofClamp(yaw, -180.0f, 180.0f);
	owner.videoPresentationRuntime.videoViewPitch = ofClamp(pitch, -90.0f, 90.0f);
	owner.videoPresentationRuntime.videoViewRoll = ofClamp(roll, -180.0f, 180.0f);
	owner.videoPresentationRuntime.videoViewFov = ofClamp(fov, 1.0f, 179.0f);
	applyVideoViewpoint(absolute);
}

void ofxVlc4::VideoComponent::resetVideoViewpoint() {
	owner.videoPresentationRuntime.videoViewYaw = 0.0f;
	owner.videoPresentationRuntime.videoViewPitch = 0.0f;
	owner.videoPresentationRuntime.videoViewRoll = 0.0f;
	owner.videoPresentationRuntime.videoViewFov = 80.0f;
	applyVideoViewpoint();
	owner.setStatus("3D view reset.");
	owner.logNotice("3D view reset.");
}

bool ofxVlc4::VideoComponent::isMarqueeEnabled() const {
	return owner.overlayRuntime.marqueeEnabled;
}

void ofxVlc4::VideoComponent::setMarqueeEnabled(bool enabled) {
	if (owner.overlayRuntime.marqueeEnabled == enabled) {
		return;
	}

	owner.overlayRuntime.marqueeEnabled = enabled;
	applyVideoMarquee();
	owner.setStatus(std::string("Marquee ") + (owner.overlayRuntime.marqueeEnabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::VideoComponent::getMarqueeText() const {
	return owner.overlayRuntime.marqueeText;
}

void ofxVlc4::VideoComponent::setMarqueeText(const std::string & text) {
	if (owner.overlayRuntime.marqueeText == text) {
		return;
	}

	owner.overlayRuntime.marqueeText = text;
	applyVideoMarquee();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getMarqueePosition() const {
	return owner.overlayRuntime.marqueePosition;
}

void ofxVlc4::VideoComponent::setMarqueePosition(OverlayPosition position) {
	if (owner.overlayRuntime.marqueePosition == position) {
		return;
	}

	owner.overlayRuntime.marqueePosition = position;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeOpacity() const {
	return owner.overlayRuntime.marqueeOpacity;
}

void ofxVlc4::VideoComponent::setMarqueeOpacity(int opacity) {
	const int clampedOpacity = ofClamp(opacity, 0, 255);
	if (owner.overlayRuntime.marqueeOpacity == clampedOpacity) {
		return;
	}

	owner.overlayRuntime.marqueeOpacity = clampedOpacity;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeSize() const {
	return owner.overlayRuntime.marqueeSize;
}

void ofxVlc4::VideoComponent::setMarqueeSize(int size) {
	const int clampedSize = ofClamp(size, 6, 96);
	if (owner.overlayRuntime.marqueeSize == clampedSize) {
		return;
	}

	owner.overlayRuntime.marqueeSize = clampedSize;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeColor() const {
	return owner.overlayRuntime.marqueeColor;
}

void ofxVlc4::VideoComponent::setMarqueeColor(int color) {
	const int clampedColor = clampPackedRgbColor(color);
	if (owner.overlayRuntime.marqueeColor == clampedColor) {
		return;
	}

	owner.overlayRuntime.marqueeColor = clampedColor;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeRefresh() const {
	return owner.overlayRuntime.marqueeRefresh;
}

void ofxVlc4::VideoComponent::setMarqueeRefresh(int refreshMs) {
	const int clampedRefreshMs = ofClamp(refreshMs, 0, 10000);
	if (owner.overlayRuntime.marqueeRefresh == clampedRefreshMs) {
		return;
	}

	owner.overlayRuntime.marqueeRefresh = clampedRefreshMs;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeTimeout() const {
	return owner.overlayRuntime.marqueeTimeout;
}

void ofxVlc4::VideoComponent::setMarqueeTimeout(int timeoutMs) {
	const int clampedTimeoutMs = ofClamp(timeoutMs, 0, 10000);
	if (owner.overlayRuntime.marqueeTimeout == clampedTimeoutMs) {
		return;
	}

	owner.overlayRuntime.marqueeTimeout = clampedTimeoutMs;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeX() const {
	return owner.overlayRuntime.marqueeX;
}

void ofxVlc4::VideoComponent::setMarqueeX(int x) {
	const int clampedX = ofClamp(x, -4096, 4096);
	if (owner.overlayRuntime.marqueeX == clampedX) {
		return;
	}

	owner.overlayRuntime.marqueeX = clampedX;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeY() const {
	return owner.overlayRuntime.marqueeY;
}

void ofxVlc4::VideoComponent::setMarqueeY(int y) {
	const int clampedY = ofClamp(y, -4096, 4096);
	if (owner.overlayRuntime.marqueeY == clampedY) {
		return;
	}

	owner.overlayRuntime.marqueeY = clampedY;
	applyVideoMarquee();
}

bool ofxVlc4::VideoComponent::isLogoEnabled() const {
	return owner.overlayRuntime.logoEnabled;
}

void ofxVlc4::VideoComponent::setLogoEnabled(bool enabled) {
	if (owner.overlayRuntime.logoEnabled == enabled) {
		return;
	}

	owner.overlayRuntime.logoEnabled = enabled;
	applyVideoLogo();
	owner.setStatus(std::string("Logo ") + (owner.overlayRuntime.logoEnabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::VideoComponent::getLogoPath() const {
	return owner.overlayRuntime.logoPath;
}

void ofxVlc4::VideoComponent::setLogoPath(const std::string & path) {
	if (owner.overlayRuntime.logoPath == path) {
		return;
	}

	owner.overlayRuntime.logoPath = path;
	applyVideoLogo();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getLogoPosition() const {
	return owner.overlayRuntime.logoPosition;
}

void ofxVlc4::VideoComponent::setLogoPosition(OverlayPosition position) {
	if (owner.overlayRuntime.logoPosition == position) {
		return;
	}

	owner.overlayRuntime.logoPosition = position;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoOpacity() const {
	return owner.overlayRuntime.logoOpacity;
}

void ofxVlc4::VideoComponent::setLogoOpacity(int opacity) {
	const int clampedOpacity = ofClamp(opacity, 0, 255);
	if (owner.overlayRuntime.logoOpacity == clampedOpacity) {
		return;
	}

	owner.overlayRuntime.logoOpacity = clampedOpacity;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoX() const {
	return owner.overlayRuntime.logoX;
}

void ofxVlc4::VideoComponent::setLogoX(int x) {
	const int clampedX = ofClamp(x, -4096, 4096);
	if (owner.overlayRuntime.logoX == clampedX) {
		return;
	}

	owner.overlayRuntime.logoX = clampedX;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoY() const {
	return owner.overlayRuntime.logoY;
}

void ofxVlc4::VideoComponent::setLogoY(int y) {
	const int clampedY = ofClamp(y, -4096, 4096);
	if (owner.overlayRuntime.logoY == clampedY) {
		return;
	}

	owner.overlayRuntime.logoY = clampedY;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoDelay() const {
	return owner.overlayRuntime.logoDelay;
}

void ofxVlc4::VideoComponent::setLogoDelay(int delayMs) {
	const int clampedDelayMs = ofClamp(delayMs, 0, 10000);
	if (owner.overlayRuntime.logoDelay == clampedDelayMs) {
		return;
	}

	owner.overlayRuntime.logoDelay = clampedDelayMs;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoRepeat() const {
	return owner.overlayRuntime.logoRepeat;
}

void ofxVlc4::VideoComponent::setLogoRepeat(int repeat) {
	const int clampedRepeat = ofClamp(repeat, -1, 100);
	if (owner.overlayRuntime.logoRepeat == clampedRepeat) {
		return;
	}

	owner.overlayRuntime.logoRepeat = clampedRepeat;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getTeletextPage() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return libvlc_video_get_teletext(player);
	}
	return owner.videoPresentationRuntime.teletextPage;
}

void ofxVlc4::VideoComponent::setTeletextPage(int page) {
	const int clampedPage = ofClamp(page, 0, 999);
	if (owner.videoPresentationRuntime.teletextPage == clampedPage) {
		return;
	}

	owner.videoPresentationRuntime.teletextPage = clampedPage;
	applyTeletextSettings();
	media().refreshSubtitleStateInfo();
}

bool ofxVlc4::VideoComponent::isTeletextTransparencyEnabled() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return libvlc_video_get_teletext_transparency(player);
	}
	return owner.videoPresentationRuntime.teletextTransparencyEnabled;
}

void ofxVlc4::VideoComponent::setTeletextTransparencyEnabled(bool enabled) {
	if (owner.videoPresentationRuntime.teletextTransparencyEnabled == enabled) {
		return;
	}

	owner.videoPresentationRuntime.teletextTransparencyEnabled = enabled;
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
	return owner.playerConfigRuntime.keyInputEnabled;
}

void ofxVlc4::VideoComponent::setKeyInputEnabled(bool enabled) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	setInputHandlingEnabled(
		player,
		owner.playerConfigRuntime.keyInputEnabled,
		enabled,
		"Video key input ",
		libvlc_video_set_key_input,
		[](const std::string & message) { ofxVlc4::logVerbose(message); });
}

bool ofxVlc4::VideoComponent::isMouseInputEnabled() const {
	return owner.playerConfigRuntime.mouseInputEnabled;
}

void ofxVlc4::VideoComponent::setMouseInputEnabled(bool enabled) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	setInputHandlingEnabled(
		player,
		owner.playerConfigRuntime.mouseInputEnabled,
		enabled,
		"Video mouse input ",
		libvlc_video_set_mouse_input,
		[](const std::string & message) { ofxVlc4::logVerbose(message); });
}

bool ofxVlc4::VideoComponent::isVlcFullscreenEnabled() const {
	return owner.videoPresentationRuntime.vlcFullscreenEnabled;
}

void ofxVlc4::VideoComponent::setVlcFullscreenEnabled(bool enabled) {
	if (owner.videoPresentationRuntime.vlcFullscreenEnabled == enabled) {
		return;
	}

	owner.videoPresentationRuntime.vlcFullscreenEnabled = enabled;
	applyVlcFullscreen();
	owner.logNotice(std::string("libVLC fullscreen ") + (owner.videoPresentationRuntime.vlcFullscreenEnabled ? "enabled." : "disabled."));
}

void ofxVlc4::VideoComponent::toggleVlcFullscreen() {
	setVlcFullscreenEnabled(!owner.videoPresentationRuntime.vlcFullscreenEnabled);
}

bool ofxVlc4::VideoComponent::isVideoTitleDisplayEnabled() const {
	return owner.videoPresentationRuntime.videoTitleDisplayEnabled;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayEnabled(bool enabled) {
	if (owner.videoPresentationRuntime.videoTitleDisplayEnabled == enabled) {
		return;
	}

	owner.videoPresentationRuntime.videoTitleDisplayEnabled = enabled;
	applyVideoTitleDisplay();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getVideoTitleDisplayPosition() const {
	return owner.videoPresentationRuntime.videoTitleDisplayPosition;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayPosition(OverlayPosition position) {
	if (owner.videoPresentationRuntime.videoTitleDisplayPosition == position) {
		return;
	}

	owner.videoPresentationRuntime.videoTitleDisplayPosition = position;
	applyVideoTitleDisplay();
}

unsigned ofxVlc4::VideoComponent::getVideoTitleDisplayTimeoutMs() const {
	return owner.videoPresentationRuntime.videoTitleDisplayTimeoutMs;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayTimeoutMs(unsigned timeoutMs) {
	const unsigned clampedTimeoutMs = std::min(timeoutMs, 60000u);
	if (owner.videoPresentationRuntime.videoTitleDisplayTimeoutMs == clampedTimeoutMs) {
		return;
	}

	owner.videoPresentationRuntime.videoTitleDisplayTimeoutMs = clampedTimeoutMs;
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
	return owner.effectsRuntime.videoAdjustmentsEnabled;
}

void ofxVlc4::VideoComponent::setVideoAdjustmentsEnabled(bool enabled) {
	if (owner.effectsRuntime.videoAdjustmentsEnabled == enabled) {
		return;
	}

	owner.effectsRuntime.videoAdjustmentsEnabled = enabled;
	applyOrQueueVideoAdjustments();
	owner.setStatus(std::string("Video adjustments ") + (enabled ? "enabled." : "disabled."));
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::VideoComponent::getVideoAdjustmentEngine() const {
	return owner.effectsRuntime.videoAdjustmentEngine;
}

ofxVlc4::VideoAdjustmentEngine ofxVlc4::VideoComponent::getActiveVideoAdjustmentEngine() const {
	return owner.effectsRuntime.activeVideoAdjustmentEngine;
}

void ofxVlc4::VideoComponent::setVideoAdjustmentEngine(ofxVlc4::VideoAdjustmentEngine engine) {
	const ofxVlc4::VideoAdjustmentEngine normalizedEngine = engine;
	if (owner.effectsRuntime.videoAdjustmentEngine == normalizedEngine) {
		return;
	}

	owner.effectsRuntime.videoAdjustmentEngine = normalizedEngine;
	applyOrQueueVideoAdjustments();
	owner.setStatus(std::string("Video adjustment engine: ") + videoAdjustmentEngineLabel(owner.effectsRuntime.activeVideoAdjustmentEngine) + ".");
}

float ofxVlc4::VideoComponent::getVideoContrast() const {
	return owner.effectsRuntime.videoAdjustContrast;
}

void ofxVlc4::VideoComponent::setVideoContrast(float contrast) {
	const float clampedContrast = ofClamp(contrast, 0.0f, 4.0f);
	if (std::abs(owner.effectsRuntime.videoAdjustContrast - clampedContrast) < 0.0001f) {
		return;
	}

	owner.effectsRuntime.videoAdjustContrast = clampedContrast;
	owner.effectsRuntime.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoBrightness() const {
	return owner.effectsRuntime.videoAdjustBrightness;
}

void ofxVlc4::VideoComponent::setVideoBrightness(float brightness) {
	const float clampedBrightness = ofClamp(brightness, 0.0f, 4.0f);
	if (std::abs(owner.effectsRuntime.videoAdjustBrightness - clampedBrightness) < 0.0001f) {
		return;
	}

	owner.effectsRuntime.videoAdjustBrightness = clampedBrightness;
	owner.effectsRuntime.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoHue() const {
	return owner.effectsRuntime.videoAdjustHue;
}

void ofxVlc4::VideoComponent::setVideoHue(float hue) {
	const float wrappedHue = std::fmod(hue, 360.0f);
	const float normalizedHue = wrappedHue < 0.0f ? (wrappedHue + 360.0f) : wrappedHue;
	if (std::abs(owner.effectsRuntime.videoAdjustHue - normalizedHue) < 0.0001f) {
		return;
	}

	owner.effectsRuntime.videoAdjustHue = normalizedHue;
	owner.effectsRuntime.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoSaturation() const {
	return owner.effectsRuntime.videoAdjustSaturation;
}

void ofxVlc4::VideoComponent::setVideoSaturation(float saturation) {
	const float clampedSaturation = ofClamp(saturation, 0.0f, 4.0f);
	if (std::abs(owner.effectsRuntime.videoAdjustSaturation - clampedSaturation) < 0.0001f) {
		return;
	}

	owner.effectsRuntime.videoAdjustSaturation = clampedSaturation;
	owner.effectsRuntime.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

float ofxVlc4::VideoComponent::getVideoGamma() const {
	return owner.effectsRuntime.videoAdjustGamma;
}

void ofxVlc4::VideoComponent::setVideoGamma(float gamma) {
	const float clampedGamma = ofClamp(gamma, 0.01f, 10.0f);
	if (std::abs(owner.effectsRuntime.videoAdjustGamma - clampedGamma) < 0.0001f) {
		return;
	}

	owner.effectsRuntime.videoAdjustGamma = clampedGamma;
	owner.effectsRuntime.videoAdjustmentsEnabled = true;
	applyOrQueueVideoAdjustments();
}

ofxVlc4::VideoDeinterlaceMode ofxVlc4::VideoComponent::getVideoDeinterlaceMode() const {
	return owner.videoPresentationRuntime.videoDeinterlaceMode;
}

void ofxVlc4::VideoComponent::setVideoDeinterlaceMode(VideoDeinterlaceMode mode) {
	if (owner.videoPresentationRuntime.videoDeinterlaceMode == mode) {
		return;
	}

	owner.videoPresentationRuntime.videoDeinterlaceMode = mode;
	applyVideoDeinterlace();
	owner.setStatus("Video deinterlace set.");
	owner.logNotice(std::string("Video deinterlace: ") + videoDeinterlaceModeLabel(mode) + ".");
}

ofxVlc4::VideoAspectRatioMode ofxVlc4::VideoComponent::getVideoAspectRatioMode() const {
	return owner.videoPresentationRuntime.videoAspectRatioMode;
}

void ofxVlc4::VideoComponent::setVideoAspectRatioMode(VideoAspectRatioMode mode) {
	if (owner.videoPresentationRuntime.videoAspectRatioMode == mode) {
		return;
	}

	owner.videoPresentationRuntime.videoAspectRatioMode = mode;
	applyVideoAspectRatio();
	owner.setStatus("Video aspect ratio set.");
	owner.logNotice(std::string("Video aspect ratio: ") + videoAspectRatioLabel(mode) + ".");
}

ofxVlc4::VideoCropMode ofxVlc4::VideoComponent::getVideoCropMode() const {
	return owner.videoPresentationRuntime.videoCropMode;
}

void ofxVlc4::VideoComponent::setVideoCropMode(VideoCropMode mode) {
	if (owner.videoPresentationRuntime.videoCropMode == mode) {
		return;
	}

	owner.videoPresentationRuntime.videoCropMode = mode;
	applyVideoCrop();
	owner.setStatus("Video crop set.");
	owner.logNotice(std::string("Video crop: ") + videoCropLabel(mode) + ".");
}

ofxVlc4::VideoDisplayFitMode ofxVlc4::VideoComponent::getVideoDisplayFitMode() const {
	return owner.videoPresentationRuntime.videoDisplayFitMode;
}

void ofxVlc4::VideoComponent::setVideoDisplayFitMode(VideoDisplayFitMode mode) {
	if (owner.videoPresentationRuntime.videoDisplayFitMode == mode) {
		return;
	}

	owner.videoPresentationRuntime.videoDisplayFitMode = mode;
	applyVideoScaleAndFit();
	owner.setStatus("Video fit set.");
	owner.logNotice("Video fit set.");
}

ofxVlc4::VideoOutputBackend ofxVlc4::VideoComponent::getVideoOutputBackend() const {
	return owner.videoPresentationRuntime.videoOutputBackend;
}

ofxVlc4::VideoOutputBackend ofxVlc4::VideoComponent::getActiveVideoOutputBackend() const {
	return owner.videoPresentationRuntime.activeVideoOutputBackend;
}

ofxVlc4::PreferredDecoderDevice ofxVlc4::VideoComponent::getPreferredDecoderDevice() const {
	return owner.videoPresentationRuntime.preferredDecoderDevice;
}

void ofxVlc4::VideoComponent::setVideoOutputBackend(VideoOutputBackend backend) {
	if (owner.videoPresentationRuntime.videoOutputBackend == backend) {
		return;
	}

	owner.videoPresentationRuntime.videoOutputBackend = backend;
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
	if (owner.videoPresentationRuntime.preferredDecoderDevice == device) {
		return;
	}

	owner.videoPresentationRuntime.preferredDecoderDevice = device;
	if (owner.sessionPlayer()) {
		owner.logWarning(std::string("Preferred decoder hardware changes apply on the next player initialization: ") +
			preferredDecoderDeviceLabel(device) + ".");
		owner.setStatus("Preferred decoder hardware updated for the next init.");
	}
}

void ofxVlc4::VideoComponent::resetVideoAdjustments() {
	owner.effectsRuntime.videoAdjustmentsEnabled = true;
	owner.effectsRuntime.videoAdjustContrast = 1.0f;
	owner.effectsRuntime.videoAdjustBrightness = 1.0f;
	owner.effectsRuntime.videoAdjustHue = 0.0f;
	owner.effectsRuntime.videoAdjustSaturation = 1.0f;
	owner.effectsRuntime.videoAdjustGamma = 1.0f;
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
	return owner.playerConfigRuntime.videoFilterChain;
}

void ofxVlc4::VideoComponent::setVideoFilterChain(const std::string & filterChain) {
	owner.playerConfigRuntime.videoFilterChain = trimWhitespace(filterChain);
	if (!owner.canApplyNativeVideoFilters()) {
		if (owner.playerConfigRuntime.videoFilterChain.empty()) {
			owner.setStatus("Video filter chain cleared. NativeWindow backend is required to apply video filters.");
			owner.logNotice("Video filter chain cleared.");
		} else {
			owner.setStatus("Video filter chain stored. Switch to NativeWindow backend and reload/play media to apply.");
			owner.logNotice("Video filter chain stored: " + owner.playerConfigRuntime.videoFilterChain + ".");
		}
		return;
	}

	if (owner.playerConfigRuntime.videoFilterChain.empty()) {
		if (media().reapplyCurrentMediaForFilterChainChange("Video")) {
			owner.logNotice("Video filter chain cleared.");
			return;
		}
		owner.setStatus("Video filter chain cleared. Reload media to apply.");
		owner.logNotice("Video filter chain cleared.");
		return;
	}

	if (media().reapplyCurrentMediaForFilterChainChange("Video")) {
		owner.logNotice("Video filter chain: " + owner.playerConfigRuntime.videoFilterChain + ".");
		return;
	}

	owner.setStatus("Video filter chain set. Reload media to apply.");
	owner.logNotice("Video filter chain: " + owner.playerConfigRuntime.videoFilterChain + ".");
}

unsigned ofxVlc4::VideoComponent::getVideoOutputCount() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return 0u;
	}
	return owner.subsystemRuntime.playbackController->getCachedVideoOutputCount();
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
		owner.videoGeometryRuntime.pixelAspectNumerator.store(sarNum);
		owner.videoGeometryRuntime.pixelAspectDenominator.store(sarDen);
		return;
	}

	owner.videoGeometryRuntime.pixelAspectNumerator.store(1);
	owner.videoGeometryRuntime.pixelAspectDenominator.store(1);
}

void ofxVlc4::VideoComponent::refreshDisplayAspectRatio() {
	const unsigned currentVideoWidth = owner.videoGeometryRuntime.videoWidth.load();
	const unsigned currentVideoHeight = owner.videoGeometryRuntime.videoHeight.load();
	if (currentVideoWidth == 0 || currentVideoHeight == 0) {
		owner.videoGeometryRuntime.displayAspectRatio.store(1.0f);
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

	const unsigned sarNum = owner.videoGeometryRuntime.pixelAspectNumerator.load();
	const unsigned sarDen = owner.videoGeometryRuntime.pixelAspectDenominator.load();
	if (sarNum > 0 && sarDen > 0) {
		aspect *= static_cast<float>(sarNum) / static_cast<float>(sarDen);
	}

	owner.videoGeometryRuntime.displayAspectRatio.store(std::max(aspect, 0.0001f));
}

ofTexture & ofxVlc4::VideoComponent::getTexture() {
	if (owner.videoFrameRuntime.exposedTextureDirty.exchange(false)) {
		refreshExposedTexture();
	}

	return owner.videoResourceRuntime.exposedTextureFbo.getTexture();
}

ofTexture & ofxVlc4::VideoComponent::getRenderTexture() {
	return owner.videoResourceRuntime.videoTexture;
}

ofxVlc4::VideoStateInfo ofxVlc4::VideoComponent::getVideoStateInfo() const {
	VideoStateInfo state;
	state.startupPrepared = owner.videoFrameRuntime.startupPlaybackStatePrepared.load();
	state.loaded = owner.videoFrameRuntime.isVideoLoaded.load();
	state.frameReceived = owner.videoFrameRuntime.hasReceivedVideoFrame.load();
	state.trackCount = std::max(0, owner.stateCacheRuntime.cachedVideoTrackCount.load());
	state.tracksAvailable = state.trackCount > 0;
	state.sourceWidth = owner.videoGeometryRuntime.videoWidth.load();
	state.sourceHeight = owner.videoGeometryRuntime.videoHeight.load();
	state.renderWidth = owner.videoGeometryRuntime.renderWidth.load();
	state.renderHeight = owner.videoGeometryRuntime.renderHeight.load();
	state.geometryKnown = state.sourceWidth > 0 &&
		state.sourceHeight > 0 &&
		state.renderWidth > 0 &&
		state.renderHeight > 0;
	state.pixelAspectNumerator = owner.videoGeometryRuntime.pixelAspectNumerator.load();
	state.pixelAspectDenominator = owner.videoGeometryRuntime.pixelAspectDenominator.load();
	state.displayAspectRatio = owner.videoGeometryRuntime.displayAspectRatio.load();
	state.videoOutputCount = getVideoOutputCount();
	state.hasVideoOutput = state.videoOutputCount > 0u;
	state.videoAdjustmentsEnabled = owner.effectsRuntime.videoAdjustmentsEnabled;
	state.videoAdjustmentEngine = owner.effectsRuntime.videoAdjustmentEngine;
	state.activeVideoAdjustmentEngine = owner.effectsRuntime.activeVideoAdjustmentEngine;
	state.vlcFullscreenEnabled = owner.videoPresentationRuntime.vlcFullscreenEnabled;
	state.teletextTransparencyEnabled = owner.videoPresentationRuntime.teletextTransparencyEnabled;
	state.teletextPage = owner.videoPresentationRuntime.teletextPage;
	state.scale = owner.videoPresentationRuntime.videoScale;
	state.yaw = owner.videoPresentationRuntime.videoViewYaw;
	state.pitch = owner.videoPresentationRuntime.videoViewPitch;
	state.roll = owner.videoPresentationRuntime.videoViewRoll;
	state.fov = owner.videoPresentationRuntime.videoViewFov;
	state.deinterlaceMode = owner.videoPresentationRuntime.videoDeinterlaceMode;
	state.aspectRatioMode = owner.videoPresentationRuntime.videoAspectRatioMode;
	state.cropMode = owner.videoPresentationRuntime.videoCropMode;
	state.displayFitMode = owner.videoPresentationRuntime.videoDisplayFitMode;
	state.outputBackend = owner.videoPresentationRuntime.videoOutputBackend;
	state.activeOutputBackend = owner.videoPresentationRuntime.activeVideoOutputBackend;
	state.preferredDecoderDevice = owner.videoPresentationRuntime.preferredDecoderDevice;
	state.projectionMode = owner.videoPresentationRuntime.videoProjectionMode;
	state.stereoMode = owner.videoPresentationRuntime.videoStereoMode;
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

void ofxVlc4::sendTeletextKeyRed() {
	sendTeletextKey(TeletextKey::Red);
}

void ofxVlc4::sendTeletextKeyGreen() {
	sendTeletextKey(TeletextKey::Green);
}

void ofxVlc4::sendTeletextKeyYellow() {
	sendTeletextKey(TeletextKey::Yellow);
}

void ofxVlc4::sendTeletextKeyBlue() {
	sendTeletextKey(TeletextKey::Blue);
}

void ofxVlc4::sendTeletextKeyIndex() {
	sendTeletextKey(TeletextKey::Index);
}

void ofxVlc4::toggleTeletextTransparency() {
	setTeletextTransparencyEnabled(!isTeletextTransparencyEnabled());
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
