#pragma once

#include "ofxImGui.h"

#include <algorithm>
#include <cfloat>

namespace ofVlcPlayer4GuiStyle {
constexpr float kDisplayWindowPadding = 20.0f;
constexpr float kWindowBorderSize = 5.0f;
constexpr float kCompactInnerPaddingY = 5.0f;
const ImVec2 kLabelInnerSpacing(10.0f, 6.0f);
constexpr float kWideSliderWidth = 280.0f;
constexpr float kExtraWideSliderWidth = 320.0f;

struct MainWindowLayoutMetrics {
	float x = 20.0f;
	float y = 25.0f;
	float contentWidth = 500.0f;
	float maxHeight = FLT_MAX;
};

inline constexpr MainWindowLayoutMetrics kMainWindowLayoutMetrics {};

struct PreviewWindowLayoutMetrics {
	float width = 720.0f;
	float maxWidth = 680.0f;
	float x = 584.0f;
};

inline constexpr PreviewWindowLayoutMetrics kPreviewWindowLayoutMetrics {};

struct MainWindowStyleMetrics {
	float windowRounding = 0.0f;
	float childRounding = 0.0f;
	float frameRounding = 0.0f;
	float grabRounding = 0.0f;
	float indentSpacing = 0.0f;
	ImVec2 framePadding = ImVec2(7.0f, 6.0f);
	ImVec2 itemSpacing = ImVec2(6.0f, 7.0f);
	ImVec2 windowPadding = ImVec2(kDisplayWindowPadding, kDisplayWindowPadding);
	ImVec2 windowTitleAlign = ImVec2(0.5f, 0.5f);
};

inline const MainWindowStyleMetrics kMainWindowStyleMetrics {};

struct ChildWindowStyleMetrics {
	float childRounding = 0.0f;
	ImVec2 windowPadding = ImVec2(kDisplayWindowPadding, kCompactInnerPaddingY);
};

inline const ChildWindowStyleMetrics kPlaylistWindowStyleMetrics {};

struct FullscreenWindowStyleMetrics {
	ImVec2 windowPadding = ImVec2(0.0f, 0.0f);
	float windowBorderSize = 0.0f;
	float windowRounding = 0.0f;
};

inline const FullscreenWindowStyleMetrics kFullscreenWindowStyleMetrics {};

struct PreviewWindowStyleMetrics {
	ImVec2 windowPadding = ImVec2(kDisplayWindowPadding, kDisplayWindowPadding);
};

inline const PreviewWindowStyleMetrics kPreviewWindowStyleMetrics {};

inline constexpr const char * kThemeLabels[] = {
	"Graphit",
	"Dark",
	"Classic",
	"Light",
	"Slate",
	"Aurora",
	"Neon Noir",
	"Ember",
	"Deep Sea",
	"Rose Dust",
	"Brass",
	"Retro CRT",
	"Arctic Ice",
	"Sunset Motel"
};

inline void applyCustomThemePalette(
	ImGuiStyle & style,
	const ImVec4 & windowBg,
	const ImVec4 & titleBg,
	const ImVec4 & titleBgActive,
	const ImVec4 & childBg,
	const ImVec4 & border,
	const ImVec4 & frameBg,
	const ImVec4 & frameBgHovered,
	const ImVec4 & frameBgActive,
	const ImVec4 & button,
	const ImVec4 & buttonHovered,
	const ImVec4 & buttonActive,
	const ImVec4 & checkMark,
	const ImVec4 & sliderGrab,
	const ImVec4 & sliderGrabActive,
	const ImVec4 & scrollbarBg,
	const ImVec4 & scrollbarGrab,
	const ImVec4 & scrollbarGrabHovered,
	const ImVec4 & scrollbarGrabActive) {
	style.Colors[ImGuiCol_WindowBg] = windowBg;
	style.Colors[ImGuiCol_TitleBg] = titleBg;
	style.Colors[ImGuiCol_TitleBgActive] = titleBgActive;
	style.Colors[ImGuiCol_TitleBgCollapsed] = titleBg;
	style.Colors[ImGuiCol_ChildBg] = childBg;
	style.Colors[ImGuiCol_Border] = border;
	style.Colors[ImGuiCol_Separator] = border;
	style.Colors[ImGuiCol_SeparatorHovered] = border;
	style.Colors[ImGuiCol_SeparatorActive] = border;
	style.Colors[ImGuiCol_FrameBg] = frameBg;
	style.Colors[ImGuiCol_FrameBgHovered] = frameBgHovered;
	style.Colors[ImGuiCol_FrameBgActive] = frameBgActive;
	style.Colors[ImGuiCol_Button] = button;
	style.Colors[ImGuiCol_ButtonHovered] = buttonHovered;
	style.Colors[ImGuiCol_ButtonActive] = buttonActive;
	style.Colors[ImGuiCol_Header] = button;
	style.Colors[ImGuiCol_HeaderHovered] = buttonHovered;
	style.Colors[ImGuiCol_HeaderActive] = buttonActive;
	style.Colors[ImGuiCol_CheckMark] = checkMark;
	style.Colors[ImGuiCol_TextSelectedBg] = button;
	style.Colors[ImGuiCol_SliderGrab] = sliderGrab;
	style.Colors[ImGuiCol_SliderGrabActive] = sliderGrabActive;
	style.Colors[ImGuiCol_ScrollbarBg] = scrollbarBg;
	style.Colors[ImGuiCol_ScrollbarGrab] = scrollbarGrab;
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = scrollbarGrabHovered;
	style.Colors[ImGuiCol_ScrollbarGrabActive] = scrollbarGrabActive;
}

inline ImVec4 colorWithAlpha(const ImVec4 & color, float alpha) {
	return ImVec4(color.x, color.y, color.z, std::clamp(alpha, 0.0f, 1.0f));
}

inline int & currentThemeIndex() {
	static int themeIndex = 0;
	return themeIndex;
}

inline bool isGraphitTheme() {
	return currentThemeIndex() == 0;
}

inline ImVec4 graphitWindowBgColor() {
	return ImVec4(0.08f, 0.09f, 0.10f, 0.98f);
}

inline ImVec4 graphitChildBgColor() {
	return ImVec4(0.16f, 0.17f, 0.18f, 0.98f);
}

inline ImVec4 graphitFrameColor() {
	return ImVec4(0.21f, 0.22f, 0.24f, 1.0f);
}

inline ImVec4 graphitPanelColor() {
	return ImVec4(0.12f, 0.13f, 0.14f, 0.98f);
}

inline ImVec4 graphitFrameHoverColor() {
	return ImVec4(0.25f, 0.27f, 0.29f, 1.0f);
}

inline ImVec4 graphitFrameActiveColor() {
	return ImVec4(0.29f, 0.31f, 0.34f, 1.0f);
}

inline ImVec4 graphitBorderColor() {
	return ImVec4(0.26f, 0.27f, 0.30f, 1.0f);
}

inline ImVec4 graphitMenuHeaderColor() {
	return ImVec4(0.18f, 0.19f, 0.21f, 1.0f);
}

inline ImVec4 graphitMenuHeaderHoverColor() {
	return ImVec4(0.23f, 0.24f, 0.27f, 1.0f);
}

inline ImVec4 graphitMenuHeaderActiveColor() {
	return ImVec4(0.27f, 0.29f, 0.32f, 1.0f);
}

inline ImVec4 graphitAccentColor() {
	return ImVec4(0.31f, 0.33f, 0.35f, 0.94f);
}

inline ImVec4 graphitAccentHoverColor() {
	return ImVec4(0.37f, 0.39f, 0.41f, 1.0f);
}

inline ImVec4 graphitAccentActiveColor() {
	return ImVec4(0.27f, 0.29f, 0.31f, 1.0f);
}

inline ImVec4 graphitAccentBrightColor() {
	return ImVec4(0.47f, 0.49f, 0.52f, 1.0f);
}

inline ImVec4 graphitSliderGrabColor() {
	return ImVec4(0.66f, 0.68f, 0.71f, 1.0f);
}

inline ImVec4 graphitSliderGrabActiveColor() {
	return ImVec4(0.82f, 0.84f, 0.87f, 1.0f);
}

inline ImVec4 styleColor(ImGuiCol colorIndex) {
	return ImGui::GetStyleColorVec4(colorIndex);
}

inline ImVec4 styleColorWithAlpha(ImGuiCol colorIndex, float alpha) {
	return colorWithAlpha(styleColor(colorIndex), alpha);
}

inline ImVec4 themedChildBgColor() {
	return isGraphitTheme() ? graphitChildBgColor() : styleColor(ImGuiCol_ChildBg);
}

inline ImVec4 themedBorderColor() {
	return isGraphitTheme() ? graphitBorderColor() : styleColor(ImGuiCol_Border);
}

inline ImVec4 themedFrameColor() {
	return isGraphitTheme() ? graphitFrameColor() : styleColor(ImGuiCol_FrameBg);
}

inline ImVec4 themedMenuHeaderColor() {
	return isGraphitTheme() ? graphitMenuHeaderColor() : styleColorWithAlpha(ImGuiCol_Header, 0.96f);
}

inline ImVec4 themedMenuHeaderHoverColor() {
	return isGraphitTheme() ? graphitMenuHeaderHoverColor() : styleColor(ImGuiCol_HeaderHovered);
}

inline ImVec4 themedMenuHeaderActiveColor() {
	return isGraphitTheme() ? graphitMenuHeaderActiveColor() : styleColor(ImGuiCol_HeaderActive);
}

inline ImVec4 themedPanelBgColor() {
	return isGraphitTheme() ? graphitPanelColor() : styleColorWithAlpha(ImGuiCol_FrameBg, 0.92f);
}

inline ImVec4 themedScrollbarGutterBgColor() {
	return isGraphitTheme() ? graphitWindowBgColor() : styleColorWithAlpha(ImGuiCol_WindowBg, 0.96f);
}

inline ImVec4 themedGuideColor(float alpha = 0.75f) {
	return colorWithAlpha(themedFrameColor(), alpha);
}

inline ImVec4 themedAccentColor() {
	return isGraphitTheme() ? graphitAccentColor() : styleColor(ImGuiCol_SliderGrab);
}

inline ImVec4 themedAccentBrightColor() {
	return isGraphitTheme() ? graphitAccentBrightColor() : styleColor(ImGuiCol_SliderGrabActive);
}

inline ImVec4 themedAccentColorWithAlpha(float alpha) {
	return colorWithAlpha(themedAccentColor(), alpha);
}

inline ImVec4 themedAccentBrightColorWithAlpha(float alpha) {
	return colorWithAlpha(themedAccentBrightColor(), alpha);
}

inline ImVec4 themedCheckColor(float alpha = 1.0f) {
	const ImVec4 base = isGraphitTheme() ? graphitAccentBrightColor() : styleColor(ImGuiCol_CheckMark);
	return colorWithAlpha(base, alpha);
}

inline ImVec4 themedWindowBgColor() {
	return isGraphitTheme() ? graphitWindowBgColor() : styleColor(ImGuiCol_WindowBg);
}

inline void applyTheme(int themeIndex) {
	ImGuiStyle & style = ImGui::GetStyle();
	currentThemeIndex() = std::clamp(themeIndex, 0, static_cast<int>(IM_ARRAYSIZE(kThemeLabels)) - 1);
	switch (currentThemeIndex()) {
	case 0:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			graphitWindowBgColor(),
			ImVec4(0.15f, 0.16f, 0.17f, 1.0f),
			ImVec4(0.18f, 0.19f, 0.21f, 1.0f),
			graphitChildBgColor(),
			graphitBorderColor(),
			graphitFrameColor(),
			graphitFrameHoverColor(),
			graphitFrameActiveColor(),
			graphitAccentColor(),
			graphitAccentHoverColor(),
			graphitAccentActiveColor(),
			graphitAccentBrightColor(),
			graphitSliderGrabColor(),
			graphitSliderGrabActiveColor(),
			graphitChildBgColor(),
			graphitFrameActiveColor(),
			ImVec4(0.28f, 0.30f, 0.32f, 0.92f),
			graphitAccentActiveColor());
		break;
	case 1:
		ImGui::StyleColorsDark(&style);
		break;
	case 2:
		ImGui::StyleColorsClassic(&style);
		break;
	case 3:
		ImGui::StyleColorsLight(&style);
		break;
	case 4:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.09f, 0.11f, 0.14f, 0.98f),
			ImVec4(0.12f, 0.15f, 0.19f, 1.0f),
			ImVec4(0.15f, 0.19f, 0.24f, 1.0f),
			ImVec4(0.11f, 0.14f, 0.18f, 0.98f),
			ImVec4(0.22f, 0.27f, 0.32f, 1.0f),
			ImVec4(0.16f, 0.19f, 0.23f, 1.0f),
			ImVec4(0.20f, 0.24f, 0.29f, 1.0f),
			ImVec4(0.24f, 0.29f, 0.34f, 1.0f),
			ImVec4(0.29f, 0.36f, 0.43f, 0.94f),
			ImVec4(0.35f, 0.43f, 0.51f, 1.0f),
			ImVec4(0.25f, 0.31f, 0.38f, 1.0f),
			ImVec4(0.47f, 0.58f, 0.68f, 1.0f),
			ImVec4(0.64f, 0.72f, 0.80f, 1.0f),
			ImVec4(0.80f, 0.88f, 0.96f, 1.0f),
			ImVec4(0.11f, 0.14f, 0.18f, 0.98f),
			ImVec4(0.24f, 0.29f, 0.34f, 1.0f),
			ImVec4(0.27f, 0.34f, 0.41f, 0.92f),
			ImVec4(0.25f, 0.31f, 0.38f, 1.0f));
		break;
	case 5:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.08f, 0.09f, 0.14f, 0.98f),
			ImVec4(0.15f, 0.12f, 0.21f, 1.0f),
			ImVec4(0.18f, 0.20f, 0.29f, 1.0f),
			ImVec4(0.10f, 0.11f, 0.17f, 0.98f),
			ImVec4(0.31f, 0.35f, 0.47f, 1.0f),
			ImVec4(0.17f, 0.16f, 0.25f, 1.0f),
			ImVec4(0.24f, 0.24f, 0.34f, 1.0f),
			ImVec4(0.32f, 0.34f, 0.45f, 1.0f),
			ImVec4(0.34f, 0.46f, 0.48f, 0.94f),
			ImVec4(0.52f, 0.39f, 0.70f, 1.0f),
			ImVec4(0.27f, 0.34f, 0.43f, 1.0f),
			ImVec4(0.62f, 0.78f, 0.74f, 1.0f),
			ImVec4(0.78f, 0.90f, 0.86f, 1.0f),
			ImVec4(0.90f, 0.97f, 0.95f, 1.0f),
			ImVec4(0.10f, 0.11f, 0.17f, 0.98f),
			ImVec4(0.26f, 0.28f, 0.39f, 1.0f),
			ImVec4(0.34f, 0.36f, 0.50f, 0.92f),
			ImVec4(0.29f, 0.31f, 0.44f, 1.0f));
		break;
	case 6:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.04f, 0.04f, 0.08f, 0.98f),
			ImVec4(0.10f, 0.06f, 0.18f, 1.0f),
			ImVec4(0.12f, 0.10f, 0.24f, 1.0f),
			ImVec4(0.06f, 0.06f, 0.11f, 0.98f),
			ImVec4(0.24f, 0.18f, 0.38f, 1.0f),
			ImVec4(0.12f, 0.09f, 0.18f, 1.0f),
			ImVec4(0.18f, 0.12f, 0.28f, 1.0f),
			ImVec4(0.25f, 0.17f, 0.39f, 1.0f),
			ImVec4(0.08f, 0.58f, 0.59f, 0.94f),
			ImVec4(0.41f, 0.16f, 0.67f, 1.0f),
			ImVec4(0.09f, 0.40f, 0.41f, 1.0f),
			ImVec4(0.44f, 0.96f, 0.90f, 1.0f),
			ImVec4(0.85f, 0.30f, 0.98f, 1.0f),
			ImVec4(0.96f, 0.63f, 1.0f, 1.0f),
			ImVec4(0.06f, 0.06f, 0.11f, 0.98f),
			ImVec4(0.25f, 0.17f, 0.39f, 1.0f),
			ImVec4(0.14f, 0.47f, 0.50f, 0.92f),
			ImVec4(0.13f, 0.36f, 0.39f, 1.0f));
		break;
	case 7:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.13f, 0.07f, 0.05f, 0.98f),
			ImVec4(0.20f, 0.10f, 0.08f, 1.0f),
			ImVec4(0.26f, 0.14f, 0.09f, 1.0f),
			ImVec4(0.15f, 0.09f, 0.07f, 0.98f),
			ImVec4(0.39f, 0.22f, 0.16f, 1.0f),
			ImVec4(0.22f, 0.11f, 0.08f, 1.0f),
			ImVec4(0.29f, 0.15f, 0.10f, 1.0f),
			ImVec4(0.36f, 0.20f, 0.12f, 1.0f),
			ImVec4(0.55f, 0.26f, 0.10f, 0.94f),
			ImVec4(0.72f, 0.34f, 0.12f, 1.0f),
			ImVec4(0.44f, 0.21f, 0.09f, 1.0f),
			ImVec4(0.92f, 0.56f, 0.19f, 1.0f),
			ImVec4(0.98f, 0.73f, 0.34f, 1.0f),
			ImVec4(1.00f, 0.83f, 0.52f, 1.0f),
			ImVec4(0.15f, 0.09f, 0.07f, 0.98f),
			ImVec4(0.36f, 0.20f, 0.12f, 1.0f),
			ImVec4(0.47f, 0.25f, 0.13f, 0.92f),
			ImVec4(0.44f, 0.21f, 0.09f, 1.0f));
		break;
	case 8:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.03f, 0.08f, 0.10f, 0.98f),
			ImVec4(0.05f, 0.12f, 0.14f, 1.0f),
			ImVec4(0.07f, 0.16f, 0.19f, 1.0f),
			ImVec4(0.04f, 0.10f, 0.12f, 0.98f),
			ImVec4(0.11f, 0.28f, 0.30f, 1.0f),
			ImVec4(0.05f, 0.15f, 0.17f, 1.0f),
			ImVec4(0.08f, 0.21f, 0.24f, 1.0f),
			ImVec4(0.11f, 0.27f, 0.31f, 1.0f),
			ImVec4(0.13f, 0.42f, 0.45f, 0.94f),
			ImVec4(0.08f, 0.55f, 0.61f, 1.0f),
			ImVec4(0.08f, 0.33f, 0.37f, 1.0f),
			ImVec4(0.48f, 0.84f, 0.89f, 1.0f),
			ImVec4(0.72f, 0.93f, 0.97f, 1.0f),
			ImVec4(0.88f, 0.98f, 1.00f, 1.0f),
			ImVec4(0.04f, 0.10f, 0.12f, 0.98f),
			ImVec4(0.11f, 0.27f, 0.31f, 1.0f),
			ImVec4(0.10f, 0.35f, 0.39f, 0.92f),
			ImVec4(0.08f, 0.33f, 0.37f, 1.0f));
		break;
	case 9:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.16f, 0.09f, 0.12f, 0.98f),
			ImVec4(0.22f, 0.12f, 0.16f, 1.0f),
			ImVec4(0.28f, 0.16f, 0.20f, 1.0f),
			ImVec4(0.18f, 0.11f, 0.14f, 0.98f),
			ImVec4(0.38f, 0.24f, 0.30f, 1.0f),
			ImVec4(0.24f, 0.14f, 0.18f, 1.0f),
			ImVec4(0.30f, 0.18f, 0.24f, 1.0f),
			ImVec4(0.37f, 0.22f, 0.28f, 1.0f),
			ImVec4(0.55f, 0.30f, 0.40f, 0.94f),
			ImVec4(0.67f, 0.37f, 0.50f, 1.0f),
			ImVec4(0.44f, 0.25f, 0.33f, 1.0f),
			ImVec4(0.90f, 0.65f, 0.74f, 1.0f),
			ImVec4(0.97f, 0.79f, 0.85f, 1.0f),
			ImVec4(1.00f, 0.89f, 0.93f, 1.0f),
			ImVec4(0.18f, 0.11f, 0.14f, 0.98f),
			ImVec4(0.37f, 0.22f, 0.28f, 1.0f),
			ImVec4(0.45f, 0.26f, 0.33f, 0.92f),
			ImVec4(0.44f, 0.25f, 0.33f, 1.0f));
		break;
	case 10:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.14f, 0.11f, 0.06f, 0.98f),
			ImVec4(0.20f, 0.16f, 0.08f, 1.0f),
			ImVec4(0.27f, 0.20f, 0.10f, 1.0f),
			ImVec4(0.16f, 0.13f, 0.07f, 0.98f),
			ImVec4(0.40f, 0.32f, 0.14f, 1.0f),
			ImVec4(0.23f, 0.18f, 0.09f, 1.0f),
			ImVec4(0.31f, 0.24f, 0.12f, 1.0f),
			ImVec4(0.38f, 0.29f, 0.15f, 1.0f),
			ImVec4(0.58f, 0.44f, 0.16f, 0.94f),
			ImVec4(0.72f, 0.55f, 0.20f, 1.0f),
			ImVec4(0.46f, 0.35f, 0.13f, 1.0f),
			ImVec4(0.92f, 0.74f, 0.30f, 1.0f),
			ImVec4(0.99f, 0.86f, 0.49f, 1.0f),
			ImVec4(1.00f, 0.93f, 0.66f, 1.0f),
			ImVec4(0.16f, 0.13f, 0.07f, 0.98f),
			ImVec4(0.38f, 0.29f, 0.15f, 1.0f),
			ImVec4(0.47f, 0.36f, 0.16f, 0.92f),
			ImVec4(0.46f, 0.35f, 0.13f, 1.0f));
		break;
	case 11:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.02f, 0.05f, 0.03f, 0.98f),
			ImVec4(0.03f, 0.08f, 0.04f, 1.0f),
			ImVec4(0.04f, 0.11f, 0.05f, 1.0f),
			ImVec4(0.03f, 0.06f, 0.03f, 0.98f),
			ImVec4(0.10f, 0.28f, 0.12f, 1.0f),
			ImVec4(0.04f, 0.10f, 0.05f, 1.0f),
			ImVec4(0.06f, 0.15f, 0.07f, 1.0f),
			ImVec4(0.08f, 0.20f, 0.09f, 1.0f),
			ImVec4(0.10f, 0.33f, 0.12f, 0.94f),
			ImVec4(0.12f, 0.47f, 0.15f, 1.0f),
			ImVec4(0.08f, 0.25f, 0.10f, 1.0f),
			ImVec4(0.42f, 1.00f, 0.52f, 1.0f),
			ImVec4(0.63f, 1.00f, 0.68f, 1.0f),
			ImVec4(0.79f, 1.00f, 0.82f, 1.0f),
			ImVec4(0.03f, 0.06f, 0.03f, 0.98f),
			ImVec4(0.08f, 0.20f, 0.09f, 1.0f),
			ImVec4(0.09f, 0.27f, 0.10f, 0.92f),
			ImVec4(0.08f, 0.25f, 0.10f, 1.0f));
		break;
	case 12:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.11f, 0.15f, 0.18f, 0.98f),
			ImVec4(0.15f, 0.20f, 0.24f, 1.0f),
			ImVec4(0.19f, 0.26f, 0.31f, 1.0f),
			ImVec4(0.13f, 0.18f, 0.22f, 0.98f),
			ImVec4(0.34f, 0.46f, 0.52f, 1.0f),
			ImVec4(0.18f, 0.24f, 0.29f, 1.0f),
			ImVec4(0.24f, 0.31f, 0.37f, 1.0f),
			ImVec4(0.29f, 0.38f, 0.45f, 1.0f),
			ImVec4(0.44f, 0.61f, 0.68f, 0.94f),
			ImVec4(0.60f, 0.76f, 0.84f, 1.0f),
			ImVec4(0.35f, 0.49f, 0.56f, 1.0f),
			ImVec4(0.86f, 0.96f, 1.00f, 1.0f),
			ImVec4(0.94f, 0.99f, 1.00f, 1.0f),
			ImVec4(1.00f, 1.00f, 1.00f, 1.0f),
			ImVec4(0.13f, 0.18f, 0.22f, 0.98f),
			ImVec4(0.29f, 0.38f, 0.45f, 1.0f),
			ImVec4(0.37f, 0.48f, 0.56f, 0.92f),
			ImVec4(0.35f, 0.49f, 0.56f, 1.0f));
		break;
	case 13:
	default:
		ImGui::StyleColorsDark(&style);
		applyCustomThemePalette(
			style,
			ImVec4(0.13f, 0.07f, 0.12f, 0.98f),
			ImVec4(0.21f, 0.09f, 0.17f, 1.0f),
			ImVec4(0.28f, 0.12f, 0.18f, 1.0f),
			ImVec4(0.16f, 0.09f, 0.14f, 0.98f),
			ImVec4(0.41f, 0.25f, 0.26f, 1.0f),
			ImVec4(0.22f, 0.11f, 0.18f, 1.0f),
			ImVec4(0.30f, 0.15f, 0.22f, 1.0f),
			ImVec4(0.38f, 0.19f, 0.25f, 1.0f),
			ImVec4(0.79f, 0.39f, 0.23f, 0.94f),
			ImVec4(0.92f, 0.52f, 0.31f, 1.0f),
			ImVec4(0.58f, 0.28f, 0.18f, 1.0f),
			ImVec4(0.98f, 0.76f, 0.53f, 1.0f),
			ImVec4(1.00f, 0.85f, 0.66f, 1.0f),
			ImVec4(1.00f, 0.92f, 0.78f, 1.0f),
			ImVec4(0.16f, 0.09f, 0.14f, 0.98f),
			ImVec4(0.38f, 0.19f, 0.25f, 1.0f),
			ImVec4(0.53f, 0.27f, 0.20f, 0.92f),
			ImVec4(0.58f, 0.28f, 0.18f, 1.0f));
		break;
	}
}

const ImVec4 kUiBlack(0.0f, 0.0f, 0.0f, 1.0f);
}
