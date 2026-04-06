#include "ofVlcPlayer4GuiWindows.h"
#include "ofVlcPlayer4GuiControls.h"
#include "ofVlcPlayer4GuiStyle.h"

#include "ofxProjectM.h"
#include "ofxVlc4.h"
#include "ofxImGui.h"

#include <algorithm>
#include <cstdint>

namespace {
const float kPreviewWindowWidth = ofVlcPlayer4GuiStyle::kPreviewWindowLayoutMetrics.width;
const float kPreviewMaxWidth = ofVlcPlayer4GuiStyle::kPreviewWindowLayoutMetrics.maxWidth;
const float kPreviewWindowX = ofVlcPlayer4GuiStyle::kPreviewWindowLayoutMetrics.x;
constexpr float kDefaultPreviewAspect = 16.0f / 9.0f;
using ofVlcPlayer4GuiStyle::kDisplayWindowPadding;
using ofVlcPlayer4GuiStyle::kLabelInnerSpacing;
using ofVlcPlayer4GuiStyle::kUiBlack;
bool gShowVideoWindow = true;
bool gShowProjectMWindow = false;
bool gShowDisplayFullscreen = false;
int gFullscreenDisplaySource = 0;
glm::vec2 gLastVideoWindowPos = { kPreviewWindowX, 24.0f };
glm::vec2 gLastProjectMWindowPos = { kPreviewWindowX, 500.0f };
bool gRestoreVideoWindowPosition = false;
bool gRestoreProjectMWindowPosition = false;
bool gWasVideoFullscreen = false;
bool gWasProjectMFullscreen = false;
bool gFullscreenHotkeysArmed = false;

ImVec2 computePreviewSize(float sourceWidth, float sourceHeight, float maxPreviewWidth, float fallbackAspect = kDefaultPreviewAspect) {
	const float aspect = (sourceWidth > 1.0f && sourceHeight > 1.0f)
		? (sourceWidth / sourceHeight)
		: fallbackAspect;
	const float previewWidth = std::max(1.0f, maxPreviewWidth);
	return ImVec2(previewWidth, previewWidth / aspect);
}

ImVec2 computeFixedPreviewWindowSize(float windowWidth, float maxPreviewWidth, float fallbackAspect = kDefaultPreviewAspect) {
	const float framePadding = kDisplayWindowPadding * 2.0f;
	const ImGuiStyle & style = ImGui::GetStyle();
	const float titleBarHeight = ImGui::GetFontSize() + (style.FramePadding.y * 2.0f);
	const float previewWidth = std::max(1.0f, std::min(maxPreviewWidth, windowWidth - framePadding));
	const ImVec2 previewSize = computePreviewSize(0.0f, 0.0f, previewWidth, fallbackAspect);
	return ImVec2(windowWidth, previewSize.y + framePadding + titleBarHeight);
}

float displayInnerBorder(bool fullscreen) {
	return fullscreen ? 0.0f : 0.0f;
}

void drawTexturePreview(
	const ofTexture & texture,
	float displayWidth,
	float displayHeight,
	float maxWidth,
	bool fillWindow,
	bool flipVertical,
	float innerBorder,
	ImU32 textureBackgroundColor) {
	const ImVec2 contentStart = ImGui::GetCursorPos();
	const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
	const ImVec2 screenStart = ImGui::GetCursorScreenPos();
	const ofVlcPlayer4GuiControls::PanelRect outerRect {
		screenStart,
		ImVec2(screenStart.x + availableRegion.x, screenStart.y + availableRegion.y)
	};
	const auto textureRect = ofVlcPlayer4GuiControls::insetRect(
		outerRect,
		ofVlcPlayer4GuiControls::makeUniformInsets(innerBorder));
	const ImVec2 insetRegion(textureRect.width(), textureRect.height());
	const ImVec2 textureMin = textureRect.min;
	const ImVec2 textureMax = textureRect.max;
	float previewWidth = insetRegion.x;
	float previewHeight = insetRegion.y;
	if (!fillWindow) {
		const float sourceAspect = (displayWidth > 1.0f && displayHeight > 1.0f)
			? (displayWidth / displayHeight)
			: kDefaultPreviewAspect;
		previewWidth = std::min(maxWidth, insetRegion.x);
		previewHeight = previewWidth / sourceAspect;
		if (previewHeight > insetRegion.y) {
			previewHeight = insetRegion.y;
			previewWidth = previewHeight * sourceAspect;
		}
	}

	const ImVec2 previewOffset(
		std::max(0.0f, (insetRegion.x - previewWidth) * 0.5f),
		std::max(0.0f, (insetRegion.y - previewHeight) * 0.5f));
	const ImVec2 previewMin(textureMin.x + previewOffset.x, textureMin.y + previewOffset.y);
	const ImVec2 previewMax(previewMin.x + previewWidth, previewMin.y + previewHeight);

	ImDrawList * drawList = ImGui::GetWindowDrawList();
	if (textureBackgroundColor != 0) {
		drawList->AddRectFilled(textureMin, textureMax, textureBackgroundColor);
	}

	if (texture.isAllocated() && texture.getWidth() > 0.0f && texture.getHeight() > 0.0f) {
		const ImVec2 uvMin(0.0f, flipVertical ? 1.0f : 0.0f);
		const ImVec2 uvMax(1.0f, flipVertical ? 0.0f : 1.0f);
		drawList->AddImage(
			(ImTextureID)(uintptr_t)texture.getTextureData().textureID,
			previewMin,
			previewMax,
			uvMin,
			uvMax);
	}

	ImGui::Dummy(availableRegion);
	ImGui::SetCursorPos(contentStart);
	ImGui::Dummy(availableRegion);
}

void drawPreviewWindow(
	const char * title,
	bool & openFlag,
	const ofTexture & texture,
	float sourceWidth,
	float sourceHeight,
	bool flipVertical,
	bool fullscreen,
	const ImVec2 & fullscreenPos,
	const ImVec2 & fullscreenSize,
	glm::vec2 & lastWindowPos,
	bool & restoreWindowPosition,
	float fallbackAspect = kDefaultPreviewAspect) {
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	flags |= ImGuiWindowFlags_NoSavedSettings;
	if (fullscreen) {
		const auto & fullscreenStyle = ofVlcPlayer4GuiStyle::kFullscreenWindowStyleMetrics;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, fullscreenStyle.windowPadding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, fullscreenStyle.windowBorderSize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, fullscreenStyle.windowRounding);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, kUiBlack);
		ImGui::SetNextWindowPos(fullscreenPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(fullscreenSize, ImGuiCond_Always);
		flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
	} else {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ofVlcPlayer4GuiStyle::kPreviewWindowStyleMetrics.windowPadding);
		ImGui::SetNextWindowSize(
			computeFixedPreviewWindowSize(kPreviewWindowWidth, kPreviewMaxWidth, fallbackAspect),
			ImGuiCond_Always);
		ImGui::SetNextWindowPos(
			ImVec2(lastWindowPos.x, lastWindowPos.y),
			restoreWindowPosition ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
		flags |= ImGuiWindowFlags_NoResize;
	}

	ImGui::Begin(title, &openFlag, flags);
	drawTexturePreview(
		texture,
		sourceWidth,
		sourceHeight,
		fullscreen ? fullscreenSize.x : kPreviewMaxWidth,
		false,
		flipVertical,
		displayInnerBorder(fullscreen),
		ImGui::GetColorU32(kUiBlack));
	if (!fullscreen) {
		const ImVec2 currentPos = ImGui::GetWindowPos();
		lastWindowPos = glm::vec2(currentPos.x, currentPos.y);
		restoreWindowPosition = false;
	}
	ImGui::End();

	ImGui::PopStyleVar();
	if (fullscreen) {
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
	}
}
}

void ofVlcPlayer4GuiWindows::handleFullscreenEscape() {
	if (!gFullscreenHotkeysArmed) {
		gFullscreenHotkeysArmed = true;
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
		gShowDisplayFullscreen = !gShowDisplayFullscreen;
	}
	if (gShowDisplayFullscreen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		gShowDisplayFullscreen = false;
	}
}

void ofVlcPlayer4GuiWindows::drawVideoOutputControls(ofxVlc4 & player, const ImVec2 & labelInnerSpacing) {
	static const char * videoOutputBackends[] = {
		"Texture / OF",
		"Native Window / HWND",
		"D3D11 / HDR Metadata"
	};

	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	int videoOutputBackendIndex = static_cast<int>(videoState.outputBackend);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(170.0f);
	if (ofVlcPlayer4GuiControls::drawComboWithWheel(
			"Video Output",
			videoOutputBackendIndex,
			videoOutputBackends,
			IM_ARRAYSIZE(videoOutputBackends))) {
		player.setVideoOutputBackend(static_cast<ofxVlc4::VideoOutputBackend>(videoOutputBackendIndex));
	}
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();

	if (videoState.outputBackend == ofxVlc4::VideoOutputBackend::NativeWindow) {
		ImGui::TextDisabled("Native mode uses the separate VLC window.");
		if (player.isInitialized() &&
			videoState.outputBackend != videoState.activeOutputBackend) {
			ImGui::TextDisabled("Backend changes apply on the next init.");
		}
	} else if (videoState.outputBackend == ofxVlc4::VideoOutputBackend::D3D11Metadata) {
		ImGui::TextDisabled("D3D11 mode captures HDR metadata and disables OF texture preview.");
		if (player.isInitialized() &&
			videoState.outputBackend != videoState.activeOutputBackend) {
			ImGui::TextDisabled("Backend changes apply on the next init.");
		}
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	bool vlcFullscreenEnabled = videoState.vlcFullscreenEnabled;
	if (ImGui::Checkbox("libVLC Fullscreen", &vlcFullscreenEnabled)) {
		player.setVlcFullscreenEnabled(vlcFullscreenEnabled);
	}
	ImGui::PopStyleVar();
}

void ofVlcPlayer4GuiWindows::drawDisplayControls(const ImVec2 & labelInnerSpacing, float sectionSpacing) {
	static const char * fullscreenSources[] = { "Video", "projectM" };

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::Checkbox("Video Window", &gShowVideoWindow);
	ImGui::SameLine(0.0f, sectionSpacing);
	ImGui::Checkbox("projectM Window", &gShowProjectMWindow);
	ImGui::PopStyleVar();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::Checkbox("Display Fullscreen", &gShowDisplayFullscreen);
	ImGui::SameLine(0.0f, sectionSpacing);
	ImGui::PushItemWidth(126.0f);
	ofVlcPlayer4GuiControls::drawComboWithWheel("Source", gFullscreenDisplaySource, fullscreenSources, IM_ARRAYSIZE(fullscreenSources));
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4GuiWindows::drawWindows(
	const ofxVlc4 & player,
	ofxProjectM & projectM,
	bool projectMInitialized,
	const ofTexture & videoPreviewTexture,
	float videoPreviewWidth,
	float videoPreviewHeight) {
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	const bool usesTextureVideoOutput =
		videoState.activeOutputBackend == ofxVlc4::VideoOutputBackend::Texture;
	const ofTexture emptyTexture;
	const ofTexture & projectMTexture = projectMInitialized ? projectM.getTexture() : emptyTexture;
	const float projectMWidth = projectMInitialized ? static_cast<float>(projectMTexture.getWidth()) : 0.0f;
	const float projectMHeight = projectMInitialized ? static_cast<float>(projectMTexture.getHeight()) : 0.0f;
	const float videoWindowWidth = (videoPreviewWidth > 0.0f) ? videoPreviewWidth : projectMWidth;
	const float videoWindowHeight = (videoPreviewHeight > 0.0f) ? videoPreviewHeight : projectMHeight;

	const ImVec2 fullscreenPos(0.0f, 0.0f);
	const ImVec2 fullscreenSize(static_cast<float>(ofGetScreenWidth()), static_cast<float>(ofGetScreenHeight()));
	const bool fullscreenVideo = usesTextureVideoOutput && gShowDisplayFullscreen && gFullscreenDisplaySource == 0;
	const bool fullscreenProjectM = gShowDisplayFullscreen && gFullscreenDisplaySource == 1;
	const bool showNormalVideoWindow = usesTextureVideoOutput && gShowVideoWindow && !fullscreenVideo;
	const bool showNormalProjectMWindow = gShowProjectMWindow && !fullscreenProjectM;

	if (gWasVideoFullscreen && !fullscreenVideo) {
		gRestoreVideoWindowPosition = true;
	}
	if (gWasProjectMFullscreen && !fullscreenProjectM) {
		gRestoreProjectMWindowPosition = true;
	}
	gWasVideoFullscreen = fullscreenVideo;
	gWasProjectMFullscreen = fullscreenProjectM;

	if (showNormalVideoWindow || fullscreenVideo) {
		drawPreviewWindow(
			"Video Display",
			gShowVideoWindow,
			videoPreviewTexture,
			videoWindowWidth,
			videoWindowHeight,
			false,
			fullscreenVideo,
			fullscreenPos,
			fullscreenSize,
			gLastVideoWindowPos,
			gRestoreVideoWindowPosition,
			kDefaultPreviewAspect);
	}

	if (showNormalProjectMWindow || fullscreenProjectM) {
		drawPreviewWindow(
			"projectM Display",
			gShowProjectMWindow,
			projectMTexture,
			projectMWidth,
			projectMHeight,
			true,
			fullscreenProjectM,
			fullscreenPos,
			fullscreenSize,
			gLastProjectMWindowPos,
			gRestoreProjectMWindowPosition,
			kDefaultPreviewAspect);
	}
}

bool ofVlcPlayer4GuiWindows::shouldRenderProjectMPreview() const {
	return gShowProjectMWindow || (gShowDisplayFullscreen && gFullscreenDisplaySource == 1);
}

bool ofVlcPlayer4GuiWindows::hasAnyVisibleWindow() const {
	return gShowVideoWindow || gShowProjectMWindow || gShowDisplayFullscreen;
}
