#pragma once

#include "ofMain.h"
#include "ofVlcPlayer4GuiStyle.h"
#include "ofxImGui.h"

#include <algorithm>
#include <cfloat>
#include <map>
#include <string>
#include <vector>

namespace ofVlcPlayer4GuiControls {
constexpr float kMenuContentInsetX = 10.0f;

enum class MenuContentPolicy {
	Leaf,
	NestedOnly,
	ContentThenNested
};

struct PanelRect {
	ImVec2 min;
	ImVec2 max;

	float width() const {
		return max.x - min.x;
	}

	float height() const {
		return max.y - min.y;
	}
};

struct PanelInsets {
	float left = 0.0f;
	float top = 0.0f;
	float right = 0.0f;
	float bottom = 0.0f;
};

struct DetachedSectionState {
	bool detached = false;
	bool collapseInlineOnReturn = false;
	bool applyInitialWindowPos = false;
	ImVec2 detachedWindowPos = ImVec2(0.0f, 0.0f);
	bool detachedWindowSizeInitialized = false;
	ImVec2 detachedWindowSize = ImVec2(0.0f, 0.0f);
	bool autoHeight = false;
	bool detachPending = false;
	ImVec2 detachDragOffset = ImVec2(28.0f, 16.0f);
	ImVec2 sourceWindowPos = ImVec2(0.0f, 0.0f);
	ImVec2 sourceWindowSize = ImVec2(0.0f, 0.0f);
};

struct DetachedSectionScope {
	bool detached = false;
	bool open = false;
	DetachedSectionState * state = nullptr;
	ImVec2 inlineSectionMin = ImVec2(0.0f, 0.0f);
	float inlineSectionMaxX = 0.0f;
};

inline PanelInsets makeUniformInsets(float inset) {
	return { inset, inset, inset, inset };
}

inline PanelRect insetRect(const PanelRect & rect, const PanelInsets & insets) {
	const float minX = rect.min.x + insets.left;
	const float minY = rect.min.y + insets.top;
	const float maxX = std::max(minX, rect.max.x - insets.right);
	const float maxY = std::max(minY, rect.max.y - insets.bottom);
	return { ImVec2(minX, minY), ImVec2(maxX, maxY) };
}

inline std::map<std::string, DetachedSectionState> & detachedSectionStates() {
	static std::map<std::string, DetachedSectionState> states;
	return states;
}

inline bool hasDetachedSections() {
	for (const auto & [_, state] : detachedSectionStates()) {
		if (state.detached || state.detachPending) {
			return true;
		}
	}
	return false;
}


inline void closeAllDetachedSections() {
	for (auto & [_, state] : detachedSectionStates()) {
		state.detached = false;
		state.detachPending = false;
		state.collapseInlineOnReturn = true;
		state.applyInitialWindowPos = false;
	}
}

inline std::vector<DetachedSectionScope> & detachedMenuScopes() {
	static std::vector<DetachedSectionScope> scopes;
	return scopes;
}

inline std::vector<DetachedSectionScope> & detachedSubMenuScopes() {
	static std::vector<DetachedSectionScope> scopes;
	return scopes;
}

inline bool menuPolicyAddsTopPadding(MenuContentPolicy policy) {
	return policy != MenuContentPolicy::NestedOnly;
}

inline bool menuPolicyAddsBottomSeparator(MenuContentPolicy policy) {
	return policy == MenuContentPolicy::Leaf;
}

inline float menuContentPaddingHeight() {
	return std::max(1.0f, ImGui::GetStyle().ItemSpacing.y * 0.35f);
}

inline void addMenuContentPadding() {
	ImGui::Dummy(ImVec2(0.0f, menuContentPaddingHeight()));
}

inline std::string makeDetachedSectionKey(const char * prefix, const char * label) {
	return std::string(prefix) + label;
}

inline bool isDetachedSubMenuOpen(const char * label) {
	const auto & states = detachedSectionStates();
	const auto it = states.find(makeDetachedSectionKey("submenu:", label));
	return it != states.end() && (it->second.detached || it->second.detachPending);
}

inline bool isAnyDetachedSubMenuOpen(std::initializer_list<const char *> labels) {
	for (const char * label : labels) {
		if (isDetachedSubMenuOpen(label)) {
			return true;
		}
	}
	return false;
}

inline std::string makeDetachedWindowTitle(const char * prefix, const char * label) {
	return std::string(label) + "###" + prefix + label;
}

inline void captureDetachedSectionWidth(DetachedSectionState & state) {
	const ImGuiStyle & style = ImGui::GetStyle();
	const float inlineHeaderWidth = ImGui::GetItemRectSize().x;
	const float detachedWindowWidth = inlineHeaderWidth + (style.WindowPadding.x * 2.0f);
	state.detachedWindowSize.x = std::max(state.detachedWindowSize.x, detachedWindowWidth);
}

inline void captureInlineSectionSize(DetachedSectionScope & scope) {
	if (scope.state == nullptr) {
		return;
	}

	const ImGuiStyle & style = ImGui::GetStyle();
	const ImVec2 sectionMax = ImGui::GetCursorScreenPos();
	const float sectionWidth = std::max(0.0f, scope.inlineSectionMaxX - scope.inlineSectionMin.x);
	const float sectionHeight = std::max(0.0f, sectionMax.y - scope.inlineSectionMin.y);
	scope.state->detachedWindowSize = ImVec2(
		sectionWidth + (style.WindowPadding.x * 2.0f),
		sectionHeight + (style.WindowPadding.y * 2.0f));
	scope.state->detachedWindowSizeInitialized = true;
}

inline bool finalizePendingDetach(DetachedSectionState & state) {
	if (!state.detachPending || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		return false;
	}

	const ImVec2 mousePos = ImGui::GetMousePos();
	const bool releasedOutsideSourceWindow =
		mousePos.x < state.sourceWindowPos.x ||
		mousePos.y < state.sourceWindowPos.y ||
		mousePos.x > (state.sourceWindowPos.x + state.sourceWindowSize.x) ||
		mousePos.y > (state.sourceWindowPos.y + state.sourceWindowSize.y);

	state.detachPending = false;
	if (!releasedOutsideSourceWindow) {
		return false;
	}

	state.detached = true;
	state.collapseInlineOnReturn = false;
	state.applyInitialWindowPos = true;
	state.detachedWindowPos = mousePos - state.detachDragOffset;
	return true;
}

inline bool beginDetachedSectionWindow(
	DetachedSectionState & state,
	const char * label,
	const char * titlePrefix,
	std::vector<DetachedSectionScope> & scopeStack) {
	finalizePendingDetach(state);
	if (!state.detached && !state.detachPending) {
		return false;
	}

	if (state.detachPending) {
		state.detachedWindowPos = ImGui::GetMousePos() - state.detachDragOffset;
	}

	if (state.applyInitialWindowPos || state.detachPending) {
		ImGui::SetNextWindowPos(state.detachedWindowPos, ImGuiCond_Always);
	}
	if (state.detachedWindowSizeInitialized && !state.autoHeight) {
		ImGui::SetNextWindowSize(state.detachedWindowSize, ImGuiCond_Always);
	} else if (state.detachedWindowSize.x > 0.0f) {
		ImGui::SetNextWindowSizeConstraints(
			ImVec2(state.detachedWindowSize.x, 0.0f),
			ImVec2(state.detachedWindowSize.x, FLT_MAX));
	}

	bool open = true;
	const std::string windowTitle = makeDetachedWindowTitle(titlePrefix, label);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	flags |= ImGuiWindowFlags_NoSavedSettings;
	if (!state.detachedWindowSizeInitialized || state.autoHeight) {
		flags |= ImGuiWindowFlags_AlwaysAutoResize;
	}
	const bool visible = ImGui::Begin(
		windowTitle.c_str(),
		&open,
		flags);

	state.applyInitialWindowPos = false;
	if (!open) {
		state.detached = false;
		state.collapseInlineOnReturn = true;
		if (visible) {
			ImGui::End();
		}
		return false;
	}

	scopeStack.push_back({ true, visible, &state });
	return visible;
}

inline bool maybeDetachCurrentHeader(DetachedSectionState & state) {
	if (state.detached) {
		return false;
	}

	const bool itemActive = ImGui::IsItemActive();
	const bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 8.0f);
	if (itemActive && dragging && !state.detachPending) {
		state.detachPending = true;
		state.collapseInlineOnReturn = false;
		state.detachDragOffset = ImGui::GetMousePos() - ImGui::GetItemRectMin();
		state.sourceWindowPos = ImGui::GetWindowPos();
		state.sourceWindowSize = ImGui::GetWindowSize();
		captureDetachedSectionWidth(state);
	}
	return false;
}

inline bool beginCompactMenu(const char * label, ImGuiTreeNodeFlags flags = 0) {
	auto & state = detachedSectionStates()[makeDetachedSectionKey("menu:", label)];
	state.autoHeight = true;
	if (beginDetachedSectionWindow(state, label, "detached_menu_", detachedMenuScopes())) {
		return true;
	}

	if (state.collapseInlineOnReturn) {
		ImGui::SetNextItemOpen(false, ImGuiCond_Always);
		state.collapseInlineOnReturn = false;
	}

	flags |= ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Header, ofVlcPlayer4GuiStyle::themedMenuHeaderColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ofVlcPlayer4GuiStyle::themedMenuHeaderHoverColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ofVlcPlayer4GuiStyle::themedMenuHeaderActiveColor());
	const std::string inlineLabel = makeDetachedWindowTitle("inline_menu_", label);
	const bool open = ImGui::CollapsingHeader(inlineLabel.c_str(), flags);
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(3);

	if (maybeDetachCurrentHeader(state)) {
		return false;
	}

	if (open) {
		detachedMenuScopes().push_back({ false, true, &state, ImGui::GetItemRectMin(), ImGui::GetItemRectMax().x });
		ImGui::Indent(kMenuContentInsetX);
	}

	return open;
}

inline void endCompactMenu() {
	auto & scopes = detachedMenuScopes();
	if (scopes.empty()) {
		return;
	}

	DetachedSectionScope scope = scopes.back();
	scopes.pop_back();
	if (!scope.open) {
		return;
	}

	if (scope.detached) {
		if (scope.state != nullptr) {
			scope.state->detachedWindowPos = ImGui::GetWindowPos();
			scope.state->detachedWindowSize = ImGui::GetWindowSize();
			scope.state->detachedWindowSizeInitialized = true;
		}
		ImGui::End();
		return;
	} else if (scope.state != nullptr && scope.state->detachPending) {
		captureInlineSectionSize(scope);
	}
	ImGui::Unindent(kMenuContentInsetX);
}

inline bool applyHoveredComboWheelStep(int & value, int minValue, int maxValue) {
	if (!ImGui::IsItemHovered()) {
		return false;
	}

	const float wheel = ImGui::GetIO().MouseWheel;
	if (wheel == 0.0f) {
		return false;
	}

	const int direction = (wheel < 0.0f) ? 1 : -1;
	const int newValue = ofClamp(value + direction, minValue, maxValue);
	if (newValue == value) {
		return false;
	}

	value = newValue;
	return true;
}

inline bool drawComboWithWheel(const char * label, int & value, const char * const items[], int itemCount) {
	if (itemCount <= 0) {
		return false;
	}

	bool changed = ImGui::Combo(label, &value, items, itemCount);
	if (applyHoveredComboWheelStep(value, 0, itemCount - 1)) {
		changed = true;
	}

	return changed;
}

inline bool drawComboWithWheel(const char * label, int & value, const std::vector<const char *> & items) {
	if (items.empty()) {
		return false;
	}

	bool changed = ImGui::Combo(label, &value, items.data(), static_cast<int>(items.size()));
	if (applyHoveredComboWheelStep(value, 0, static_cast<int>(items.size()) - 1)) {
		changed = true;
	}

	return changed;
}

inline bool applyHoveredWheelStep(int & value, int minValue, int maxValue, int step, int fineStep) {
	if (!ImGui::IsItemHovered()) {
		return false;
	}

	const ImGuiIO & io = ImGui::GetIO();
	const float wheel = io.MouseWheel;
	if (wheel == 0.0f || step <= 0 || fineStep <= 0) {
		return false;
	}

	const int direction = (wheel > 0.0f) ? 1 : -1;
	const int effectiveStep = io.KeyCtrl ? fineStep : step;
	const int newValue = ofClamp(value + (direction * effectiveStep), minValue, maxValue);
	if (newValue == value) {
		return false;
	}

	value = newValue;
	return true;
}

inline bool applyHoveredWheelStep(float & value, float minValue, float maxValue, float step, float fineStep = 0.1f) {
	if (!ImGui::IsItemHovered()) {
		return false;
	}

	const ImGuiIO & io = ImGui::GetIO();
	const float wheel = io.MouseWheel;
	if (wheel == 0.0f || step <= 0.0f || fineStep <= 0.0f) {
		return false;
	}

	const float effectiveStep = io.KeyCtrl ? fineStep : step;
	const float direction = (wheel > 0.0f) ? 1.0f : -1.0f;
	const float newValue = ofClamp(value + (direction * effectiveStep), minValue, maxValue);
	if (newValue == value) {
		return false;
	}

	value = newValue;
	return true;
}

inline float getCompactLabeledControlWidth(float preferredWidth, float labelReserve = 88.0f, float minWidth = 140.0f) {
	const float availableWidth = ImGui::GetContentRegionAvail().x - labelReserve;
	const float clampedWidth = std::min(preferredWidth, availableWidth);
	return std::max(minWidth, clampedWidth);
}

inline bool beginCompactSubMenu(const char * label, bool defaultOpen = true) {
	auto & state = detachedSectionStates()[makeDetachedSectionKey("submenu:", label)];
	state.autoHeight = true;
	if (beginDetachedSectionWindow(state, label, "detached_submenu_", detachedSubMenuScopes())) {
		return true;
	}

	if (state.collapseInlineOnReturn) {
		ImGui::SetNextItemOpen(false, ImGuiCond_Always);
		state.collapseInlineOnReturn = false;
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (defaultOpen) {
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Header, ofVlcPlayer4GuiStyle::themedMenuHeaderColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ofVlcPlayer4GuiStyle::themedMenuHeaderHoverColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ofVlcPlayer4GuiStyle::themedMenuHeaderActiveColor());
	const std::string inlineLabel = makeDetachedWindowTitle("inline_submenu_", label);
	const bool open = ImGui::TreeNodeEx(inlineLabel.c_str(), flags);
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(3);
	if (maybeDetachCurrentHeader(state)) {
		if (open) {
			ImGui::TreePop();
		}
		return false;
	}
	if (open) {
		detachedSubMenuScopes().push_back({ false, true, &state, ImGui::GetItemRectMin(), ImGui::GetItemRectMax().x });
		ImGui::Indent(kMenuContentInsetX);
	}
	return open;
}

inline bool beginInlineSubMenuNoDetach(const char * label, bool defaultOpen = true) {
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (defaultOpen) {
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	const ImVec2 itemSpacing = ImGui::GetStyle().ItemSpacing;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Header, ofVlcPlayer4GuiStyle::themedMenuHeaderColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ofVlcPlayer4GuiStyle::themedMenuHeaderHoverColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ofVlcPlayer4GuiStyle::themedMenuHeaderActiveColor());
	const std::string inlineLabel = makeDetachedWindowTitle("inline_nested_submenu_", label);
	const bool open = ImGui::TreeNodeEx(inlineLabel.c_str(), flags);
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(3);
	if (open) {
		detachedSubMenuScopes().push_back({ false, true, nullptr, ImGui::GetItemRectMin(), ImGui::GetItemRectMax().x });
		ImGui::Indent(kMenuContentInsetX);
	}
	return open;
}

inline bool beginSectionSubMenu(
	const char * label,
	MenuContentPolicy policy = MenuContentPolicy::Leaf,
	bool defaultOpen = false) {
	if (!beginCompactSubMenu(label, defaultOpen)) {
		return false;
	}
	if (menuPolicyAddsTopPadding(policy)) {
		addMenuContentPadding();
	}
	return true;
}

inline bool beginDetachedOnlySubMenu(
	const char * label,
	MenuContentPolicy policy = MenuContentPolicy::Leaf) {
	const bool insideDetachedParent =
		(!detachedMenuScopes().empty() && detachedMenuScopes().back().detached) ||
		(!detachedSubMenuScopes().empty() && detachedSubMenuScopes().back().detached);
	if (insideDetachedParent) {
		if (!beginInlineSubMenuNoDetach(label, false)) {
			return false;
		}
		if (menuPolicyAddsTopPadding(policy)) {
			addMenuContentPadding();
		}
		return true;
	}

	auto & state = detachedSectionStates()[makeDetachedSectionKey("submenu:", label)];
	state.autoHeight = true;
	if (!beginDetachedSectionWindow(state, label, "detached_submenu_", detachedSubMenuScopes())) {
		return false;
	}
	if (menuPolicyAddsTopPadding(policy)) {
		addMenuContentPadding();
	}
	return true;
}

inline void endCompactSubMenu() {
	auto & scopes = detachedSubMenuScopes();
	if (scopes.empty()) {
		return;
	}

	DetachedSectionScope scope = scopes.back();
	scopes.pop_back();
	if (!scope.open) {
		return;
	}

	if (scope.detached) {
		if (scope.state != nullptr) {
			scope.state->detachedWindowPos = ImGui::GetWindowPos();
			scope.state->detachedWindowSize = ImGui::GetWindowSize();
			scope.state->detachedWindowSizeInitialized = true;
		}
		ImGui::End();
		return;
	}

	if (scope.state != nullptr && scope.state->detachPending) {
		captureInlineSectionSize(scope);
	}
	ImGui::Unindent(kMenuContentInsetX);
	ImGui::TreePop();
}

inline void endSectionSubMenu(MenuContentPolicy policy = MenuContentPolicy::Leaf) {
	if (menuPolicyAddsBottomSeparator(policy)) {
		addMenuContentPadding();
		ImGui::Separator();
	}
	endCompactSubMenu();
}
}
