#include "ofVlcPlayer4GuiVideo.h"
#include "ofVlcPlayer4GuiControls.h"
#include "ofMain.h"
#include "ofxVlc4.h"

#include "imgui_stdlib.h"

#include <cstdio>
#include <functional>

namespace {
using MenuContentPolicy = ofVlcPlayer4GuiControls::MenuContentPolicy;

void packedRgbToFloat3(int color, float rgb[3]) {
	const ofColor converted = ofColor::fromHex(static_cast<unsigned int>(ofClamp(color, 0x000000, 0xFFFFFF)));
	rgb[0] = converted.r / 255.0f;
	rgb[1] = converted.g / 255.0f;
	rgb[2] = converted.b / 255.0f;
}

int float3ToPackedRgb(const float rgb[3]) {
	const ofColor converted(
		static_cast<unsigned char>(ofClamp(rgb[0], 0.0f, 1.0f) * 255.0f),
		static_cast<unsigned char>(ofClamp(rgb[1], 0.0f, 1.0f) * 255.0f),
		static_cast<unsigned char>(ofClamp(rgb[2], 0.0f, 1.0f) * 255.0f));
	return static_cast<int>(converted.getHex());
}
}

void ofVlcPlayer4GuiVideo::drawViewContent(
	ofxVlc4 & player,
	const ImVec2 & labelInnerSpacing,
	float compactControlWidth,
	const std::function<void()> & applyAudioVisualizerSettings,
	bool detachedOnly) {
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	static const char * deinterlaceModes[] = {
		"Auto", "Off", "Blend", "Bob", "Linear", "X", "Yadif", "Yadif2x", "Phosphor", "IVTC"
	};
	static const char * fitModes[] = {
		"Inside",
		"Outside",
		"Width",
		"Height",
		"Scale"
	};
	static const char * videoOutputBackends[] = {
		"Texture / OF",
		"Native Window / HWND",
		"D3D11 / HDR Metadata"
	};
	static const char * preferredDecoderDevices[] = {
		"Auto",
		"D3D11",
		"DXVA2",
		"NVDEC",
		"None"
	};
	static const char * aspectRatioModes[] = {
		"Default", "Fill", "16:9", "16:10", "4:3", "1:1", "21:9", "2.35:1"
	};
	static const char * cropModes[] = {
		"None", "16:9", "16:10", "4:3", "1:1", "21:9", "2.35:1"
	};
	static const char * overlayPositions[] = {
		"Center", "Left", "Right", "Top", "Top Left", "Top Right", "Bottom", "Bottom Left", "Bottom Right"
	};

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(compactControlWidth);
	const auto beginSubMenu = [detachedOnly](const char * label, MenuContentPolicy policy) {
		return detachedOnly
			? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu(label, policy)
			: ofVlcPlayer4GuiControls::beginSectionSubMenu(label, policy, false);
	};

	if (beginSubMenu("Geometry", MenuContentPolicy::Leaf)) {
		int videoOutputBackendIndex = static_cast<int>(videoState.outputBackend);
		int preferredDecoderDeviceIndex = static_cast<int>(videoState.preferredDecoderDevice);
		if (ofVlcPlayer4GuiControls::drawComboWithWheel(
				"Video Output",
				videoOutputBackendIndex,
				videoOutputBackends,
				IM_ARRAYSIZE(videoOutputBackends))) {
			player.setVideoOutputBackend(static_cast<ofxVlc4::VideoOutputBackend>(videoOutputBackendIndex));
		}
		if (ofVlcPlayer4GuiControls::drawComboWithWheel(
				"Decoder Hardware",
				preferredDecoderDeviceIndex,
				preferredDecoderDevices,
				IM_ARRAYSIZE(preferredDecoderDevices))) {
			player.setPreferredDecoderDevice(static_cast<ofxVlc4::PreferredDecoderDevice>(preferredDecoderDeviceIndex));
			if (player.isInitialized() && applyAudioVisualizerSettings) {
				applyAudioVisualizerSettings();
			}
		}
		if (player.isInitialized() && videoState.outputBackend != videoState.activeOutputBackend) {
			ImGui::TextDisabled("Backend changes apply on the next init.");
		}
		if (videoState.outputBackend == ofxVlc4::VideoOutputBackend::NativeWindow) {
			ImGui::TextDisabled("Native mode uses the separate VLC window.");
		} else if (videoState.outputBackend == ofxVlc4::VideoOutputBackend::D3D11Metadata) {
			ImGui::TextDisabled("D3D11 mode captures HDR metadata and disables OF texture preview.");
		}
		if (videoState.preferredDecoderDevice == ofxVlc4::PreferredDecoderDevice::Nvdec) {
			ImGui::TextDisabled("NVDEC requires a supported NVIDIA GPU, driver, and compatible media.");
		}
		ImGui::Separator();

		int deinterlaceModeIndex = static_cast<int>(videoState.deinterlaceMode);
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Deinterlace", deinterlaceModeIndex, deinterlaceModes, IM_ARRAYSIZE(deinterlaceModes))) {
			player.setVideoDeinterlaceMode(static_cast<ofxVlc4::VideoDeinterlaceMode>(deinterlaceModeIndex));
		}

		int aspectRatioModeIndex = static_cast<int>(videoState.aspectRatioMode);
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Aspect Ratio", aspectRatioModeIndex, aspectRatioModes, IM_ARRAYSIZE(aspectRatioModes))) {
			player.setVideoAspectRatioMode(static_cast<ofxVlc4::VideoAspectRatioMode>(aspectRatioModeIndex));
		}

		int cropModeIndex = static_cast<int>(videoState.cropMode);
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Crop", cropModeIndex, cropModes, IM_ARRAYSIZE(cropModes))) {
			player.setVideoCropMode(static_cast<ofxVlc4::VideoCropMode>(cropModeIndex));
		}

		int fitModeIndex = static_cast<int>(videoState.displayFitMode);
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Fit", fitModeIndex, fitModes, IM_ARRAYSIZE(fitModes))) {
			player.setVideoDisplayFitMode(static_cast<ofxVlc4::VideoDisplayFitMode>(fitModeIndex));
		}

		float videoScale = videoState.scale;
		ImGui::BeginDisabled(videoState.displayFitMode != ofxVlc4::VideoDisplayFitMode::Scale);
		if (ImGui::SliderFloat("Scale", &videoScale, 0.25f, 4.0f, "%.2fx")) {
			player.setVideoScale(videoScale);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(videoScale, 0.25f, 4.0f, 0.1f, 0.05f)) {
			player.setVideoScale(videoScale);
		}
		ImGui::EndDisabled();

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Native VLC Filters", MenuContentPolicy::Leaf)) {
		const auto videoFilters = player.getVideoFilters();
		if (videoFilters.empty()) {
			ImGui::TextUnformatted("No video filters reported.");
		} else {
			const bool canApplyVideoFilters = player.canApplyNativeVideoFilters();
			const std::string currentVideoFilterChain = player.getVideoFilterChain();
			if (std::string(videoFilterChain) != currentVideoFilterChain) {
				std::snprintf(videoFilterChain, sizeof(videoFilterChain), "%s", currentVideoFilterChain.c_str());
			}
			const std::vector<std::string> selectedVideoFilters = player.getVideoFilterChainEntries();
			ImGui::TextDisabled("%d selected", static_cast<int>(selectedVideoFilters.size()));
			ImGui::TextDisabled("These are VLC-native filters, not the safer live adjustment sliders.");
			if (!canApplyVideoFilters) {
				ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "Video filters currently require the NativeWindow backend.");
				ImGui::TextDisabled("The selected chain is stored, but it will only be applied after switching backends and reloading media.");
			}
			ImGui::BeginDisabled(!canApplyVideoFilters);
			for (size_t filterIndex = 0; filterIndex < videoFilters.size(); ++filterIndex) {
				std::string label = videoFilters[filterIndex].shortName.empty()
					? videoFilters[filterIndex].name
					: videoFilters[filterIndex].shortName;
				if (!videoFilters[filterIndex].name.empty() && videoFilters[filterIndex].name != label) {
					label += " (" + videoFilters[filterIndex].name + ")";
				}
				if (label.empty()) {
					label = "Filter";
				}

				bool enabled = player.hasVideoFilter(videoFilters[filterIndex].name);
				if (ImGui::Checkbox(label.c_str(), &enabled)) {
					player.toggleVideoFilter(videoFilters[filterIndex].name);
					const std::string updatedChain = player.getVideoFilterChain();
					std::snprintf(videoFilterChain, sizeof(videoFilterChain), "%s", updatedChain.c_str());
				}
				if (!videoFilters[filterIndex].description.empty()) {
					ImGui::TextWrapped("%s", videoFilters[filterIndex].description.c_str());
				}
			}

			ImGui::InputText("Filter Chain", videoFilterChain, sizeof(videoFilterChain));
			const float actionWidth = (compactControlWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			if (ImGui::Button("Apply", ImVec2(actionWidth, 0.0f))) {
				player.setVideoFilterChain(videoFilterChain);
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear", ImVec2(actionWidth, 0.0f))) {
				videoFilterChain[0] = '\0';
				player.setVideoFilterChain("");
			}
			ImGui::EndDisabled();
			if (canApplyVideoFilters) {
				ImGui::TextDisabled("Changing this chain asks libVLC to reapply native filters and may be less stable than live sliders.");
			}
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Title Overlay", MenuContentPolicy::Leaf)) {
		bool videoTitleEnabled = player.isVideoTitleDisplayEnabled();
		if (ImGui::Checkbox("Show", &videoTitleEnabled)) {
			player.setVideoTitleDisplayEnabled(videoTitleEnabled);
		}

		int videoTitlePositionIndex = static_cast<int>(player.getVideoTitleDisplayPosition());
		ImGui::BeginDisabled(!videoTitleEnabled);
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Position", videoTitlePositionIndex, overlayPositions, IM_ARRAYSIZE(overlayPositions))) {
			player.setVideoTitleDisplayPosition(static_cast<ofxVlc4::OverlayPosition>(videoTitlePositionIndex));
		}

		int videoTitleTimeoutMs = static_cast<int>(player.getVideoTitleDisplayTimeoutMs());
		if (ImGui::SliderInt("Timeout", &videoTitleTimeoutMs, 0, 10000, "%d ms")) {
			player.setVideoTitleDisplayTimeoutMs(static_cast<unsigned>(videoTitleTimeoutMs));
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(videoTitleTimeoutMs, 0, 10000, 250, 50)) {
			player.setVideoTitleDisplayTimeoutMs(static_cast<unsigned>(videoTitleTimeoutMs));
		}
		ImGui::EndDisabled();

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Marquee", MenuContentPolicy::Leaf)) {
		bool marqueeEnabled = player.isMarqueeEnabled();
		if (ImGui::Checkbox("Enable", &marqueeEnabled)) {
			player.setMarqueeEnabled(marqueeEnabled);
		}

		std::string marqueeText = player.getMarqueeText();
		if (ImGui::InputText("Text", &marqueeText)) {
			player.setMarqueeText(marqueeText);
		}

		int marqueePositionIndex = static_cast<int>(player.getMarqueePosition());
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Position", marqueePositionIndex, overlayPositions, IM_ARRAYSIZE(overlayPositions))) {
			player.setMarqueePosition(static_cast<ofxVlc4::OverlayPosition>(marqueePositionIndex));
		}

		int marqueeOpacity = player.getMarqueeOpacity();
		if (ImGui::SliderInt("Opacity", &marqueeOpacity, 0, 255)) {
			player.setMarqueeOpacity(marqueeOpacity);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(marqueeOpacity, 0, 255, 8, 1)) {
			player.setMarqueeOpacity(marqueeOpacity);
		}

		int marqueeSize = player.getMarqueeSize();
		if (ImGui::SliderInt("Size", &marqueeSize, 6, 96, "%d px")) {
			player.setMarqueeSize(marqueeSize);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(marqueeSize, 6, 96, 2, 1)) {
			player.setMarqueeSize(marqueeSize);
		}

		float marqueeColorRgb[3];
		packedRgbToFloat3(player.getMarqueeColor(), marqueeColorRgb);
		if (ImGui::ColorEdit3("Color", marqueeColorRgb, ImGuiColorEditFlags_NoAlpha)) {
			player.setMarqueeColor(float3ToPackedRgb(marqueeColorRgb));
		}

		int marqueeRefresh = player.getMarqueeRefresh();
		if (ImGui::SliderInt("Refresh", &marqueeRefresh, 0, 10000, "%d ms")) {
			player.setMarqueeRefresh(marqueeRefresh);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(marqueeRefresh, 0, 10000, 250, 50)) {
			player.setMarqueeRefresh(marqueeRefresh);
		}

		int marqueeTimeout = player.getMarqueeTimeout();
		if (ImGui::SliderInt("Timeout", &marqueeTimeout, 0, 10000, "%d ms")) {
			player.setMarqueeTimeout(marqueeTimeout);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(marqueeTimeout, 0, 10000, 250, 50)) {
			player.setMarqueeTimeout(marqueeTimeout);
		}

		int marqueeX = player.getMarqueeX();
		if (ImGui::SliderInt("X", &marqueeX, -4096, 4096)) {
			player.setMarqueeX(marqueeX);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(marqueeX, -4096, 4096, 32, 4)) {
			player.setMarqueeX(marqueeX);
		}

		int marqueeY = player.getMarqueeY();
		if (ImGui::SliderInt("Y", &marqueeY, -4096, 4096)) {
			player.setMarqueeY(marqueeY);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(marqueeY, -4096, 4096, 32, 4)) {
			player.setMarqueeY(marqueeY);
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Logo", MenuContentPolicy::Leaf)) {
		bool logoEnabled = player.isLogoEnabled();
		if (ImGui::Checkbox("Enable", &logoEnabled)) {
			player.setLogoEnabled(logoEnabled);
		}

		std::string logoPath = player.getLogoPath();
		if (ImGui::InputText("Path", &logoPath)) {
			player.setLogoPath(logoPath);
		}

		int logoPositionIndex = static_cast<int>(player.getLogoPosition());
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Position", logoPositionIndex, overlayPositions, IM_ARRAYSIZE(overlayPositions))) {
			player.setLogoPosition(static_cast<ofxVlc4::OverlayPosition>(logoPositionIndex));
		}

		int logoOpacity = player.getLogoOpacity();
		if (ImGui::SliderInt("Opacity", &logoOpacity, 0, 255)) {
			player.setLogoOpacity(logoOpacity);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(logoOpacity, 0, 255, 8, 1)) {
			player.setLogoOpacity(logoOpacity);
		}

		int logoX = player.getLogoX();
		if (ImGui::SliderInt("X", &logoX, -4096, 4096)) {
			player.setLogoX(logoX);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(logoX, -4096, 4096, 32, 4)) {
			player.setLogoX(logoX);
		}

		int logoY = player.getLogoY();
		if (ImGui::SliderInt("Y", &logoY, -4096, 4096)) {
			player.setLogoY(logoY);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(logoY, -4096, 4096, 32, 4)) {
			player.setLogoY(logoY);
		}

		int logoDelay = player.getLogoDelay();
		if (ImGui::SliderInt("Delay", &logoDelay, 0, 10000, "%d ms")) {
			player.setLogoDelay(logoDelay);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(logoDelay, 0, 10000, 250, 50)) {
			player.setLogoDelay(logoDelay);
		}

		int logoRepeat = player.getLogoRepeat();
		if (ImGui::SliderInt("Repeat", &logoRepeat, -1, 100)) {
			player.setLogoRepeat(logoRepeat);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(logoRepeat, -1, 100, 1, 1)) {
			player.setLogoRepeat(logoRepeat);
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Teletext", MenuContentPolicy::Leaf)) {
		int teletextPage = videoState.teletextPage;
		if (ImGui::SliderInt("Page", &teletextPage, 0, 999, teletextPage == 0 ? "Off" : "%03d")) {
			player.setTeletextPage(teletextPage);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(teletextPage, 0, 999, 1, 1)) {
			player.setTeletextPage(teletextPage);
		}

		bool teletextTransparency = videoState.teletextTransparencyEnabled;
		if (ImGui::Checkbox("Transparent", &teletextTransparency)) {
			player.setTeletextTransparencyEnabled(teletextTransparency);
		}

		const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
		const float teletextButtonWidth = (ImGui::GetContentRegionAvail().x - (buttonSpacing * 2.0f)) / 3.0f;
		if (ImGui::Button("Red", ImVec2(teletextButtonWidth, 0.0f))) {
			player.sendTeletextKey(ofxVlc4::TeletextKey::Red);
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		if (ImGui::Button("Green", ImVec2(teletextButtonWidth, 0.0f))) {
			player.sendTeletextKey(ofxVlc4::TeletextKey::Green);
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		if (ImGui::Button("Yellow", ImVec2(teletextButtonWidth, 0.0f))) {
			player.sendTeletextKey(ofxVlc4::TeletextKey::Yellow);
		}

		const float lowerTeletextButtonWidth = (ImGui::GetContentRegionAvail().x - buttonSpacing) / 2.0f;
		if (ImGui::Button("Blue", ImVec2(lowerTeletextButtonWidth, 0.0f))) {
			player.sendTeletextKey(ofxVlc4::TeletextKey::Blue);
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		if (ImGui::Button("Index", ImVec2(lowerTeletextButtonWidth, 0.0f))) {
			player.sendTeletextKey(ofxVlc4::TeletextKey::Index);
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4GuiVideo::drawAdjustmentsContent(
	ofxVlc4 & player,
	const ImVec2 & labelInnerSpacing,
	float actionButtonWidth,
	float wideSliderWidth) {
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(wideSliderWidth));

	const auto drawAdjustmentSlider = [&](const char * label,
		float & value,
		float minValue,
		float maxValue,
		const char * format,
		float wheelStep,
		const std::function<void(float)> & applyValue) {
		if (ImGui::SliderFloat(label, &value, minValue, maxValue, format)) {
			applyValue(value);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(value, minValue, maxValue, wheelStep)) {
			applyValue(value);
		}
	};

	bool videoAdjustmentsEnabled = videoState.videoAdjustmentsEnabled;
	if (ImGui::Checkbox("Enable", &videoAdjustmentsEnabled)) {
		player.setVideoAdjustmentsEnabled(videoAdjustmentsEnabled);
	}

	ImGui::TextDisabled("These adjustment sliders are the preferred live controls while video is playing.");

	ImGui::BeginDisabled(!videoAdjustmentsEnabled);

	float brightness = player.getVideoBrightness();
	drawAdjustmentSlider("Brightness", brightness, 0.0f, 2.0f, "%.2f", 0.1f, [&](float value) {
		player.setVideoBrightness(value);
	});

	float contrast = player.getVideoContrast();
	drawAdjustmentSlider("Contrast", contrast, 0.0f, 2.0f, "%.2f", 0.1f, [&](float value) {
		player.setVideoContrast(value);
	});

	float saturation = player.getVideoSaturation();
	drawAdjustmentSlider("Saturation", saturation, 0.0f, 3.0f, "%.2f", 0.1f, [&](float value) {
		player.setVideoSaturation(value);
	});

	float gamma = player.getVideoGamma();
	drawAdjustmentSlider("Gamma", gamma, 0.5f, 2.5f, "%.2f", 0.1f, [&](float value) {
		player.setVideoGamma(value);
	});

	float hue = player.getVideoHue();
	if (hue > 180.0f) {
		hue -= 360.0f;
	}
	drawAdjustmentSlider("Hue", hue, -180.0f, 180.0f, "%.0f deg", 5.0f, [&](float value) {
		player.setVideoHue(value);
	});

	if (ImGui::Button("Reset", ImVec2(actionButtonWidth, 0.0f))) {
		player.resetVideoAdjustments();
	}

	ImGui::EndDisabled();
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4GuiVideo::draw3DContent(
	ofxVlc4 & player,
	const ImVec2 & labelInnerSpacing,
	float compactControlWidth,
	float actionButtonWidth) {
	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	static const char * projectionModes[] = {
		"Auto",
		"Rectangular",
		"360 Equirectangular",
		"Cubemap"
	};
	static const char * stereoModes[] = {
		"Auto",
		"Stereo",
		"Left Eye",
		"Right Eye",
		"Side By Side"
	};
	static const char * anaglyphColorModes[] = {
		"Red / Cyan",
		"Green / Magenta",
		"Amber / Blue"
	};

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(compactControlWidth);

	ImGui::TextDisabled("Projection");
	int projectionIndex = static_cast<int>(videoState.projectionMode) + 1;
	if (ofVlcPlayer4GuiControls::drawComboWithWheel("Projection", projectionIndex, projectionModes, IM_ARRAYSIZE(projectionModes))) {
		player.setVideoProjectionMode(static_cast<ofxVlc4::VideoProjectionMode>(projectionIndex - 1));
	}

	int stereoIndex = static_cast<int>(videoState.stereoMode);
	if (ofVlcPlayer4GuiControls::drawComboWithWheel("Stereo", stereoIndex, stereoModes, IM_ARRAYSIZE(stereoModes))) {
		player.setVideoStereoMode(static_cast<ofxVlc4::VideoStereoMode>(stereoIndex));
	}

	ImGui::Separator();
	ImGui::TextDisabled("View");

	float yaw = videoState.yaw;
	float pitch = videoState.pitch;
	float roll = videoState.roll;
	float fov = videoState.fov;

	if (ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(yaw, -180.0f, 180.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}

	if (ImGui::SliderFloat("Pitch", &pitch, -90.0f, 90.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(pitch, -90.0f, 90.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}

	if (ImGui::SliderFloat("Roll", &roll, -180.0f, 180.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(roll, -180.0f, 180.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}

	if (ImGui::SliderFloat("FOV", &fov, 1.0f, 179.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(fov, 1.0f, 179.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}

	if (ImGui::Button("Reset", ImVec2(actionButtonWidth, 0.0f))) {
		player.resetVideoViewpoint();
	}

	ImGui::Separator();
	ImGui::TextDisabled("Anaglyph");
	ImGui::Checkbox("Anaglyph", &anaglyphEnabled);
	if (anaglyphEnabled) {
		int colorModeIndex = static_cast<int>(anaglyphColorMode);
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Colors", colorModeIndex, anaglyphColorModes, IM_ARRAYSIZE(anaglyphColorModes))) {
			anaglyphColorMode = static_cast<AnaglyphColorMode>(colorModeIndex);
		}

		ImGui::Checkbox("Swap Eyes", &anaglyphSwapEyes);
		if (ImGui::SliderFloat("Separation", &anaglyphEyeSeparation, -0.15f, 0.15f, "%.2f")) {
			anaglyphEyeSeparation = ofClamp(anaglyphEyeSeparation, -0.15f, 0.15f);
		}
		ofVlcPlayer4GuiControls::applyHoveredWheelStep(anaglyphEyeSeparation, -0.15f, 0.15f, 0.01f);
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

AnaglyphSettings ofVlcPlayer4GuiVideo::getAnaglyphSettings() const {
	return {
		anaglyphEnabled,
		anaglyphColorMode,
		anaglyphSwapEyes,
		anaglyphEyeSeparation
	};
}
