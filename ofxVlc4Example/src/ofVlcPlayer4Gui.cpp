#include "ofVlcPlayer4Gui.h"
#include "ofVlcPlayer4GuiControls.h"
#include "ofVlcPlayer4GuiStyle.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <cstring>
#include <vector>

namespace {
constexpr float kSectionSpacing = 14.0f;
constexpr float kButtonSpacing = 6.0f;
const float kRemoteWindowContentWidth = ofVlcPlayer4GuiStyle::kMainWindowLayoutMetrics.contentWidth;
using MenuContentPolicy = ofVlcPlayer4GuiControls::MenuContentPolicy;

constexpr float scaleFromContentWidth(float contentWidth, float baseWidthAt520) {
	return contentWidth * (baseWidthAt520 / 520.0f);
}

constexpr float kOuterWindowPadding = ofVlcPlayer4GuiStyle::kDisplayWindowPadding;
using ofVlcPlayer4GuiStyle::kCompactInnerPaddingY;
using ofVlcPlayer4GuiStyle::kLabelInnerSpacing;
using ofVlcPlayer4GuiStyle::kWideSliderWidth;
using ofVlcPlayer4GuiStyle::kWindowBorderSize;
struct PlaylistDragPayload {
	int index = -1;
};

constexpr const char * kVlcHelpModeLabels[] = {
	"help",
	"full-help"
};

ofxVlc4VlcHelpMode vlcHelpModeFromIndex(int index) {
	switch (index) {
	case 0:
		return ofxVlc4VlcHelpMode::Help;
	case 1:
	default:
		return ofxVlc4VlcHelpMode::FullHelp;
	}
}

std::string trimGuiString(const char * value) {
	if (value == nullptr) {
		return "";
	}

	std::string text(value);
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return "";
	}
	const auto last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

ofVlcPlayer4GuiLayoutMetrics makeGuiLayoutMetrics() {
	const float compactControlWidth = scaleFromContentWidth(kRemoteWindowContentWidth, 220.0f);
	const float actionButtonWidth = scaleFromContentWidth(kRemoteWindowContentWidth, 124.0f);
	const float projectMButtonRowWidth = (actionButtonWidth * 3.0f) + (kButtonSpacing * 2.0f);
	return {
		kRemoteWindowContentWidth,
		kRemoteWindowContentWidth + (kOuterWindowPadding * 2.0f),
		compactControlWidth,
		actionButtonWidth,
		(projectMButtonRowWidth - kButtonSpacing) * 0.5f,
		scaleFromContentWidth(kRemoteWindowContentWidth, 60.0f),
		scaleFromContentWidth(kRemoteWindowContentWidth, 140.0f),
		scaleFromContentWidth(kRemoteWindowContentWidth, 108.0f)
	};
}

float labeledInputWidth(const ofVlcPlayer4GuiLayoutMetrics & layout) {
	return std::max(1.0f, ImGui::GetContentRegionAvail().x - layout.inputLabelPadding);
}

float compactLabeledInputWidth(const ofVlcPlayer4GuiLayoutMetrics & layout) {
	return std::min(labeledInputWidth(layout), layout.compactControlWidth);
}

constexpr const char * kGuiSettingsTypeName = "ofVlcPlayer4Gui";
constexpr const char * kGuiThemeSettingsSectionName = "Theme";

void * openGuiThemeSettings(ImGuiContext *, ImGuiSettingsHandler * handler, const char * name) {
	if (std::strcmp(name, kGuiThemeSettingsSectionName) != 0) {
		return nullptr;
	}

	return handler->UserData;
}

void readGuiThemeSettingsLine(ImGuiContext *, ImGuiSettingsHandler *, void * entry, const char * line) {
	auto * selectedThemeIndex = static_cast<int *>(entry);
	if (selectedThemeIndex == nullptr) {
		return;
	}

	int themeIndex = 0;
#ifdef _MSC_VER
	if (::sscanf_s(line, "SelectedTheme=%d", &themeIndex) == 1) {
#else
	if (std::sscanf(line, "SelectedTheme=%d", &themeIndex) == 1) {
#endif
		*selectedThemeIndex = std::clamp(
			themeIndex,
			0,
			static_cast<int>(IM_ARRAYSIZE(ofVlcPlayer4GuiStyle::kThemeLabels)) - 1);
	}
}

void writeGuiThemeSettings(ImGuiContext *, ImGuiSettingsHandler * handler, ImGuiTextBuffer * outBuf) {
	const auto * selectedThemeIndex = static_cast<const int *>(handler->UserData);
	if (selectedThemeIndex == nullptr) {
		return;
	}

	outBuf->appendf("[%s][%s]\n", handler->TypeName, kGuiThemeSettingsSectionName);
	outBuf->appendf("SelectedTheme=%d\n", *selectedThemeIndex);
	outBuf->append("\n");
}

void registerGuiThemeSettingsHandler(int * selectedThemeIndex) {
	ImGuiContext * const context = ImGui::GetCurrentContext();
	if (context == nullptr) {
		return;
	}

	const ImGuiID typeHash = ImHashStr(kGuiSettingsTypeName);
	for (ImGuiSettingsHandler & existingHandler : context->SettingsHandlers) {
		if (existingHandler.TypeHash == typeHash) {
			existingHandler.UserData = selectedThemeIndex;
			return;
		}
	}

	ImGuiSettingsHandler handler {};
	handler.TypeName = kGuiSettingsTypeName;
	handler.TypeHash = typeHash;
	handler.UserData = selectedThemeIndex;
	handler.ReadOpenFn = openGuiThemeSettings;
	handler.ReadLineFn = readGuiThemeSettingsLine;
	handler.WriteAllFn = writeGuiThemeSettings;
	ImGui::AddSettingsHandler(&handler);
}

bool isValidPlaylistIndex(const std::vector<std::string> & playlist, int index);
bool isValidPlaylistIndex(size_t playlistSize, int index);

int currentPlaybackTimeMs(ofxVlc4 & player) {
	const auto playbackState = player.getPlaybackStateInfo();
	if (!playbackState.mediaAttached || playbackState.stopped) {
		return 0;
	}

	const auto watchTime = player.getWatchTimeInfo();
	if (watchTime.available && watchTime.interpolatedTimeUs >= 0) {
		return static_cast<int>(watchTime.interpolatedTimeUs / 1000);
	}

	return player.getTime();
}

float currentPlaybackPosition(ofxVlc4 & player) {
	const auto playbackState = player.getPlaybackStateInfo();
	if (!playbackState.mediaAttached || playbackState.stopped) {
		return 0.0f;
	}

	const auto watchTime = player.getWatchTimeInfo();
	if (watchTime.available) {
		return static_cast<float>(std::clamp(watchTime.interpolatedPosition, 0.0, 1.0));
	}

	return player.getPosition();
}

int currentPlaybackLengthMs(ofxVlc4 & player) {
	const auto playbackState = player.getPlaybackStateInfo();
	if (!playbackState.mediaAttached) {
		return 0;
	}

	const auto watchTime = player.getWatchTimeInfo();
	if (watchTime.available && watchTime.lengthUs > 0) {
		return static_cast<int>(watchTime.lengthUs / 1000);
	}

	return static_cast<int>(player.getLength());
}

std::string buildTimeText(ofxVlc4 & player, bool showRemainingTime) {
	const int currentSeconds = currentPlaybackTimeMs(player) / 1000;
	const int totalLengthSeconds = currentPlaybackLengthMs(player) / 1000;
	const int displaySeconds = showRemainingTime
		? std::max(0, totalLengthSeconds - currentSeconds)
		: currentSeconds;

	const int hours = displaySeconds / 3600;
	const int minutes = (displaySeconds % 3600) / 60;
	const int seconds = displaySeconds % 60;
	char timeLabel[16];
	std::snprintf(timeLabel, sizeof(timeLabel), "%02d:%02d:%02d", hours, minutes, seconds);

	return showRemainingTime
		? "Time:  -" + std::string(timeLabel)
		: "Time:  " + std::string(timeLabel);
}

std::string formatDurationSeconds(int totalSeconds) {
	const int hours = totalSeconds / 3600;
	const int minutes = (totalSeconds % 3600) / 60;
	const int seconds = totalSeconds % 60;
	char timeLabel[16];
	std::snprintf(timeLabel, sizeof(timeLabel), "%02d:%02d:%02d", hours, minutes, seconds);
	return timeLabel;
}

std::string joinStrings(const std::vector<std::string> & values, const char * separator = "; ") {
	std::ostringstream stream;
	for (size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			stream << separator;
		}
		stream << values[i];
	}
	return stream.str();
}

std::vector<std::string> parseSemicolonSeparatedItems(const std::string & value) {
	std::vector<std::string> items;
	std::stringstream stream(value);
	std::string item;
	while (std::getline(stream, item, ';')) {
		item = ofTrim(item);
		if (!item.empty()) {
			items.push_back(item);
		}
	}
	return items;
}

std::string formatFileSize(uintmax_t bytes) {
	static const char * units[] = { "B", "KB", "MB", "GB", "TB" };
	double value = static_cast<double>(bytes);
	size_t unitIndex = 0;
	while (value >= 1024.0 && unitIndex + 1 < std::size(units)) {
		value /= 1024.0;
		++unitIndex;
	}

	std::ostringstream stream;
	stream << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 1) << value << " " << units[unitIndex];
	return stream.str();
}

bool beginContentMenuSection(
	const char * label,
	ImGuiTreeNodeFlags flags = 0,
	MenuContentPolicy policy = MenuContentPolicy::Leaf) {
	if (!ofVlcPlayer4GuiControls::beginCompactMenu(label, flags)) {
		return false;
	}

	if (ofVlcPlayer4GuiControls::menuPolicyAddsTopPadding(policy)) {
		ofVlcPlayer4GuiControls::addMenuContentPadding();
	}
	return true;
}

void endContentMenuSection(MenuContentPolicy policy = MenuContentPolicy::Leaf) {
	if (ofVlcPlayer4GuiControls::menuPolicyAddsBottomSeparator(policy)) {
		ofVlcPlayer4GuiControls::addMenuContentPadding();
		ImGui::Separator();
	}
	ofVlcPlayer4GuiControls::endCompactMenu();
}

std::string formatLastWriteTime(const std::filesystem::path & path) {
	std::error_code ec;
	const auto lastWrite = std::filesystem::last_write_time(path, ec);
	if (ec) {
		return "";
	}

	const auto adjusted = lastWrite - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now();
	const auto systemTime = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(adjusted));
	std::tm localTime {};
#ifdef _WIN32
	localtime_s(&localTime, &systemTime);
#else
	localtime_r(&systemTime, &localTime);
#endif
	std::ostringstream stream;
	stream << std::put_time(&localTime, "%Y-%m-%d %H:%M");
	return stream.str();
}

std::string resolveDisplayFileName(const std::string & path, const std::string & fileName) {
	if (!fileName.empty()) {
		return fileName;
	}
	if (!path.empty() && path.find("://") == std::string::npos) {
		return ofFilePath::getFileName(path);
	}
	return "";
}

std::string findMetadataValue(
	const std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & label) {
	const auto entryIt = std::find_if(
		metadata.begin(),
		metadata.end(),
		[&label](const auto & entry) { return entry.first == label && !entry.second.empty(); });
	return entryIt != metadata.end() ? entryIt->second : "";
}

std::string resolveHeaderDisplayName(const MediaDisplayState & mediaDisplayState) {
	const std::string title = findMetadataValue(mediaDisplayState.metadata, "Title");
	const std::string artist = findMetadataValue(mediaDisplayState.metadata, "Artist");
	if (!artist.empty() && !title.empty()) {
		const std::string artistPrefix = artist + " - ";
		if (title.rfind(artistPrefix, 0) == 0) {
			return title;
		}
		return artistPrefix + title;
	}
	if (!title.empty()) {
		return title;
	}
	return mediaDisplayState.fileName;
}

bool headerDisplayNameUsesMetadata(const MediaDisplayState & mediaDisplayState) {
	return !findMetadataValue(mediaDisplayState.metadata, "Title").empty();
}

MediaDisplayState resolveMediaDisplayState(
	ofxVlc4 & player,
	int selectedIndex) {
	const auto playlistState = player.getPlaylistStateInfo();
	const int currentPlayingIndex = playlistState.currentIndex;
	const int mediaIndex = isValidPlaylistIndex(playlistState.items.size(), currentPlayingIndex)
		? currentPlayingIndex
		: (isValidPlaylistIndex(playlistState.items.size(), selectedIndex) ? selectedIndex : -1);
	const ofxVlc4::PlaylistItemInfo * playlistItem =
		mediaIndex >= 0 ? &playlistState.items[static_cast<size_t>(mediaIndex)] : nullptr;

	const std::string path = playlistItem ? playlistItem->path : player.getCurrentPath();
	const std::string fileName = resolveDisplayFileName(
		path,
		playlistItem ? playlistItem->label : player.getCurrentFileName());
	const auto metadata = mediaIndex >= 0
		? player.getMetadataAtIndex(mediaIndex)
		: player.getCurrentMetadata();

	return {
		path,
		fileName,
		metadata
	};
}


void drawCurrentMediaCopyTooltip() {
	if (ImGui::BeginTooltip()) {
		ImGui::TextUnformatted("Click to copy media path");
		ImGui::EndTooltip();
	}
}

void drawCurrentMediaInfoContent(
	const std::string & currentPath,
	const std::string & fileName,
	const std::vector<std::pair<std::string, std::string>> & metadata,
	const MediaFileInfoCache & fileInfoCache) {
	std::string title;
	for (const auto & [entryLabel, entryValue] : metadata) {
		if (entryLabel == "Title" && !entryValue.empty()) {
			title = entryValue;
			break;
		}
	}
	const std::string displayName = !title.empty() ? title : fileName;
	if (!displayName.empty()) {
		ImGui::TextUnformatted(displayName.c_str());
	}
	if (!fileName.empty() && !title.empty() && fileName != title) {
		ImGui::TextDisabled("%s", fileName.c_str());
	}
	ImGui::Separator();
	for (const auto & [entryLabel, entryValue] : metadata) {
		if (entryValue.empty()) {
			continue;
		}
		if (entryLabel == "Title" || entryLabel == "Artwork URL") {
			continue;
		}
		if (entryLabel == "Duration") {
			ImGui::TextWrapped("Duration: %s", formatDurationSeconds(ofToInt(entryValue)).c_str());
			continue;
		}
		if (entryLabel == "Date") {
			ImGui::TextWrapped("Year: %s", entryValue.c_str());
			continue;
		}
		ImGui::TextWrapped("%s: %s", entryLabel.c_str(), entryValue.c_str());
	}

	ImGui::Separator();
	ImGui::Text("Type: %s", fileInfoCache.isUri ? "URI" : "File");

	if (!fileInfoCache.extension.empty()) {
		ImGui::Text("Extension: %s", fileInfoCache.extension.c_str());
	}

	if (fileInfoCache.fileInfoAvailable) {
		if (!fileInfoCache.sizeText.empty()) {
			ImGui::Text("Size: %s", fileInfoCache.sizeText.c_str());
		}
		if (!fileInfoCache.modifiedText.empty()) {
			ImGui::Text("Modified: %s", fileInfoCache.modifiedText.c_str());
		}
	}
}

std::vector<int> sortedDescending(const std::set<int> & selection) {
	std::vector<int> out(selection.begin(), selection.end());
	std::sort(out.rbegin(), out.rend());
	return out;
}

bool isValidPlaylistIndex(const std::vector<std::string> & playlist, int index) {
	return index >= 0 && index < static_cast<int>(playlist.size());
}

bool isValidPlaylistIndex(size_t playlistSize, int index) {
	return index >= 0 && index < static_cast<int>(playlistSize);
}

int remapIndexAfterMove(int index, int fromIndex, int movedTo) {
	if (index < 0) {
		return index;
	}

	if (index == fromIndex) {
		return movedTo;
	}

	if (fromIndex < movedTo) {
		if (index > fromIndex && index <= movedTo) {
			return index - 1;
		}
		return index;
	}

	if (index >= movedTo && index < fromIndex) {
		return index + 1;
	}

	return index;
}

void movePlaylistItemAndSelection(
	ofxVlc4 & player,
	int fromIndex,
	int insertIndex,
	std::set<int> & selectedIndices,
	int & selectedIndex,
	int & lastClickedIndex) {
	if (fromIndex == insertIndex || fromIndex + 1 == insertIndex) {
		return;
	}

	int movedTo = insertIndex;
	if (fromIndex < insertIndex) {
		movedTo--;
	}

	player.movePlaylistItem(fromIndex, insertIndex);

	std::set<int> remappedSelection;
	for (int idx : selectedIndices) {
		remappedSelection.insert(remapIndexAfterMove(idx, fromIndex, movedTo));
	}
	selectedIndices = std::move(remappedSelection);
	selectedIndex = remapIndexAfterMove(selectedIndex, fromIndex, movedTo);
	lastClickedIndex = remapIndexAfterMove(lastClickedIndex, fromIndex, movedTo);
}

int computeInsertIndexForDrag(int hoveredRow, int grabbedIndex) {
	return (grabbedIndex < hoveredRow) ? (hoveredRow + 1) : hoveredRow;
}
}

void ofVlcPlayer4Gui::setup() {
	gui.setup(nullptr, false, ImGuiConfigFlags_ViewportsEnable, true);
	ImGui::GetIO().IniFilename = "imgui.ini";
	ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
	registerGuiThemeSettingsHandler(&selectedThemeIndex);
}

void ofVlcPlayer4Gui::refreshVlcHelpText(ofxVlc4 & player) {
	const ofxVlc4VlcHelpMode mode = vlcHelpModeFromIndex(selectedVlcHelpModeIndex);
	const std::string moduleName = trimGuiString(vlcHelpModuleName);
	if (!moduleName.empty()) {
		vlcHelpTextCache = player.getVlcModuleHelpText(moduleName);
	} else {
		vlcHelpTextCache = player.getVlcHelpText(mode);
	}
}

void ofVlcPlayer4Gui::draw(
	ofxVlc4 & player,
	ofxProjectM & projectM,
	bool projectMInitialized,
	const ofTexture & videoPreviewTexture,
	float videoPreviewWidth,
	float videoPreviewHeight,
	const std::function<int(const std::string &)> & addPathToPlaylist,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomSubtitle,
	const std::function<void()> & clearCustomSubtitle,
	const std::function<std::string()> & customSubtitleStatus,
	const std::function<std::vector<std::string>()> & customSubtitleFontLabels,
	const std::function<int()> & customSubtitleSelectedFontIndex,
	const std::function<void(int)> & setCustomSubtitleFontIndex) {
	gui.begin();
	drawImGui(
		player,
		projectM,
		projectMInitialized,
		videoPreviewTexture,
		videoPreviewWidth,
		videoPreviewHeight,
		addPathToPlaylist,
		randomProjectMPreset,
		reloadProjectMPresets,
		reloadProjectMTextures,
		loadPlayerProjectMTexture,
		loadCustomProjectMTexture,
		loadCustomSubtitle,
		clearCustomSubtitle,
		customSubtitleStatus,
		customSubtitleFontLabels,
		customSubtitleSelectedFontIndex,
		setCustomSubtitleFontIndex);
	gui.end();
	gui.draw();
}

void ofVlcPlayer4Gui::drawImGui(
	ofxVlc4 & player,
	ofxProjectM & projectM,
	bool projectMInitialized,
	const ofTexture & videoPreviewTexture,
	float videoPreviewWidth,
	float videoPreviewHeight,
	const std::function<int(const std::string &)> & addPathToPlaylist,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomSubtitle,
	const std::function<void()> & clearCustomSubtitle,
	const std::function<std::string()> & customSubtitleStatus,
	const std::function<std::vector<std::string>()> & customSubtitleFontLabels,
	const std::function<int()> & customSubtitleSelectedFontIndex,
	const std::function<void(int)> & setCustomSubtitleFontIndex) {
	windowsSection.handleFullscreenEscape();
	mediaSection.setCustomSubtitleCallbacks(
		loadCustomSubtitle,
		clearCustomSubtitle,
		customSubtitleStatus,
		customSubtitleFontLabels,
		customSubtitleSelectedFontIndex,
		setCustomSubtitleFontIndex);

	layoutMetrics = makeGuiLayoutMetrics();
	const auto & layout = layoutMetrics;
	const auto & windowStyle = ofVlcPlayer4GuiStyle::kMainWindowStyleMetrics;
	const auto & windowLayout = ofVlcPlayer4GuiStyle::kMainWindowLayoutMetrics;
	ofVlcPlayer4GuiStyle::applyTheme(selectedThemeIndex);
	ImGui::SetNextWindowPos(ImVec2(windowLayout.x, windowLayout.y), ImGuiCond_Once);
	ImGui::SetNextWindowSizeConstraints(
		ImVec2(layout.windowWidth, 0.0f),
		ImVec2(layout.windowWidth, std::numeric_limits<float>::max()));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, windowStyle.windowRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kWindowBorderSize);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, windowStyle.childRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, windowStyle.frameRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, windowStyle.grabRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, windowStyle.indentSpacing);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, windowStyle.framePadding);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, windowStyle.itemSpacing);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, windowStyle.windowTitleAlign);

	if (showMainWindow) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, windowStyle.windowPadding);
		bool windowOpen = showMainWindow;
		const bool windowVisible = ImGui::Begin(
			"ofxVlc4",
			&windowOpen,
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse |
			ImGuiWindowFlags_AlwaysAutoResize);
		if (windowVisible) {
			ImDrawList * windowDrawList = ImGui::GetWindowDrawList();
			ImGui::BeginGroup();
			if (ImGui::BeginTable(
				"main_content_layout",
				1,
				ImGuiTableFlags_SizingFixedFit |
				ImGuiTableFlags_NoPadInnerX |
				ImGuiTableFlags_NoPadOuterX)) {
				ImGui::TableSetupColumn("main_content", ImGuiTableColumnFlags_WidthFixed, layout.contentWidth);
				ImGui::TableNextColumn();

				handleImGuiShortcuts(player, projectMInitialized, randomProjectMPreset);
				const bool hasPlaylist = !player.getPlaylistStateInfo().empty;
				const MediaDisplayState displayState = resolveMediaDisplayState(player, selectedIndex);
				updateMediaFileInfoCache(displayState);
				drawHeaderSection(player, hasPlaylist, displayState);
				drawTransportSection(player, hasPlaylist);
				drawPositionSection(player, hasPlaylist);
				drawPlaybackOptionsSection(
					player,
					projectM,
					displayState,
					hasPlaylist,
					projectMInitialized,
					addPathToPlaylist,
					randomProjectMPreset,
					reloadProjectMPresets,
					reloadProjectMTextures,
					loadPlayerProjectMTexture,
					loadCustomProjectMTexture);
				ImGui::EndTable();
			}
			ImGui::EndGroup();
		}
		ImGui::End();
		ImGui::PopStyleVar();
		showMainWindow = windowOpen;
		if (!showMainWindow) {
			ofExit();
		}
	}

	if (windowsSection.hasAnyVisibleWindow()) {
		windowsSection.drawWindows(
			player,
			projectM,
			projectMInitialized,
			videoPreviewTexture,
			videoPreviewWidth,
			videoPreviewHeight);
	}

	ImGui::PopStyleVar(9);
}

void ofVlcPlayer4Gui::drawHeaderSection(
	ofxVlc4 & player,
	bool hasPlaylist,
	const MediaDisplayState & mediaDisplayState) {
	const auto & layout = layoutMetrics;
	const std::string displayName = resolveStableHeaderTitle(player, hasPlaylist, mediaDisplayState);
	const std::string currentTitle = displayName.empty()
		? "Nothing loaded"
		: "Media: " + displayName;

	const std::string timeText = buildTimeText(player, showRemainingTime);

	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + layout.contentWidth);
	ImGui::TextUnformatted(currentTitle.c_str());
	if (!mediaDisplayState.path.empty() && ImGui::IsItemClicked(0)) {
		ImGui::SetClipboardText(mediaDisplayState.path.c_str());
	}
	if (!mediaDisplayState.path.empty() && ImGui::IsItemHovered()) {
		drawCurrentMediaCopyTooltip();
	}
	ImGui::PopTextWrapPos();
	ImGui::TextUnformatted(timeText.c_str());
	if (hasPlaylist && ImGui::IsItemClicked(0)) {
		showRemainingTime = !showRemainingTime;
	}
	ImGui::Separator();
}

std::string ofVlcPlayer4Gui::resolveStableHeaderTitle(
	ofxVlc4 & player,
	bool hasPlaylist,
	const MediaDisplayState & mediaDisplayState) {
	const std::string resolvedDisplayName = resolveHeaderDisplayName(mediaDisplayState);
	const bool resolvedFromMetadata = headerDisplayNameUsesMetadata(mediaDisplayState);
	const auto readiness = player.getMediaReadinessInfo();
	const bool mediaActive = readiness.mediaAttached || hasPlaylist;

	if (!mediaDisplayState.path.empty()) {
		auto cachedTitleIt = mediaHeaderTitleCache.find(mediaDisplayState.path);
		if (cachedTitleIt == mediaHeaderTitleCache.end()) {
			if (!resolvedDisplayName.empty()) {
				mediaHeaderTitleCache.emplace(mediaDisplayState.path, resolvedDisplayName);
			}
		} else if (!resolvedDisplayName.empty()) {
			const bool cachedUsesFileNameFallback = cachedTitleIt->second == mediaDisplayState.fileName;
			if (cachedUsesFileNameFallback && resolvedFromMetadata) {
				cachedTitleIt->second = resolvedDisplayName;
			}
		}
	}

	if (!mediaActive && mediaDisplayState.path.empty() && resolvedDisplayName.empty()) {
		return "";
	}

	if (!mediaDisplayState.path.empty()) {
		const auto cachedTitleIt = mediaHeaderTitleCache.find(mediaDisplayState.path);
		if (cachedTitleIt != mediaHeaderTitleCache.end() && !cachedTitleIt->second.empty()) {
			return cachedTitleIt->second;
		}
	}

	if (!resolvedDisplayName.empty()) {
		return resolvedDisplayName;
	}

	return "";
}

void ofVlcPlayer4Gui::drawTransportSection(ofxVlc4 & player, bool hasPlaylist) {
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const float controlWidth = std::floor((availableWidth - (4.0f * kButtonSpacing)) / 5.0f);
	ImGui::BeginDisabled(!hasPlaylist);
	if (ImGui::Button("Play", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		if (selectedIndex >= 0 && selectedIndex != player.getCurrentIndex()) {
			player.playIndex(selectedIndex);
		} else {
			player.play();
		}
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Pause", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		player.pause();
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Stop", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		player.stop();
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Prev", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		player.previousMediaListItem();
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Next", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		player.nextMediaListItem();
	}
	ImGui::EndDisabled();
}

void ofVlcPlayer4Gui::drawPositionSection(ofxVlc4 & player, bool hasPlaylist) {
	const ofxVlc4::PlaybackStateInfo playbackState = player.getPlaybackStateInfo();
	const bool isStopped = playbackState.stopped || !playbackState.mediaAttached;
	const float actualPosition = (hasPlaylist && !isStopped) ? currentPlaybackPosition(player) : 0.0f;
	const bool canSeek = hasPlaylist && !isStopped && player.isSeekable();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Position");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

	float sliderPosition = positionSliderActive ? pendingSeekPosition : actualPosition;
	if (isStopped) {
		positionSliderActive = false;
		pendingSeekPosition = 0.0f;
		sliderPosition = 0.0f;
	} else if (!positionSliderActive) {
		pendingSeekPosition = actualPosition;
	}

	ImGui::BeginDisabled(!canSeek);
	ImGui::SliderFloat("##Position", &sliderPosition, 0.0f, 1.0f, "", ImGuiSliderFlags_NoInput);
	const bool sliderActive = canSeek && ImGui::IsItemActive();
	if (sliderActive) {
		pendingSeekPosition = std::clamp(sliderPosition, 0.0f, 1.0f);
	}
	if (canSeek && ImGui::IsItemDeactivatedAfterEdit()) {
		const int targetTimeMs = static_cast<int>(std::round(currentPlaybackLengthMs(player) * pendingSeekPosition));
		player.setTime(targetTimeMs);
	}
	positionSliderActive = sliderActive;
	ImGui::EndDisabled();

	ImGui::PopStyleVar();
}

void ofVlcPlayer4Gui::drawPlaybackOptionsSection(
	ofxVlc4 & player,
	ofxProjectM & projectM,
	const MediaDisplayState & mediaDisplayState,
	bool hasPlaylist,
	bool projectMInitialized,
	const std::function<int(const std::string &)> & addPathToPlaylist,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture) {
	const auto & layout = layoutMetrics;
	const ofxVlc4::AudioStateInfo audioState = player.getAudioStateInfo();
	volume = audioState.volumeKnown ? audioState.volume : player.getVolume();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(layout.headerSliderWidth);
	if (ImGui::SliderInt("Volume", &volume, 0, 100)) {
		player.setVolume(volume);
	}
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
		const auto & io = ImGui::GetIO();
		const float wheel = io.MouseWheel;
		if (wheel != 0.0f) {
			const int step = io.KeyCtrl ? 1 : 5;
			const int direction = (wheel > 0.0f) ? 1 : -1;
			volume = ofClamp(volume + direction * step, 0, 100);
			player.setVolume(volume);
		}
	}
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
	ImGui::SameLine(0.0f, kSectionSpacing);

	int playbackModeIndex = 0;
	auto mode = player.getPlaybackMode();
	if (mode == ofxVlc4::PlaybackMode::Repeat) {
		playbackModeIndex = 1;
	} else if (mode == ofxVlc4::PlaybackMode::Loop) {
		playbackModeIndex = 2;
	} else {
		playbackModeIndex = 0;
	}

	const char * playbackModes[] = { "Default", "Repeat", "Loop" };
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(layout.headerComboWidth);
	if (ofVlcPlayer4GuiControls::drawComboWithWheel("Mode", playbackModeIndex, playbackModes, IM_ARRAYSIZE(playbackModes))) {
		player.setPlaybackMode(static_cast<ofxVlc4::PlaybackMode>(playbackModeIndex));
	}
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();

	ImGui::SameLine(0.0f, kSectionSpacing);
	bool shuffle = player.isShuffleEnabled();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	if (ImGui::Checkbox("Shuffle", &shuffle)) {
		player.setShuffleEnabled(shuffle);
	}
	ImGui::PopStyleVar();
	ImGui::Separator();

	windowsSection.drawDisplayControls(kLabelInnerSpacing, kSectionSpacing);
	ImGui::Separator();

	if (beginContentMenuSection("Media Playlist", ImGuiTreeNodeFlags_DefaultOpen, MenuContentPolicy::Leaf)) {
		drawPlaylistSection(player);
		drawPathSection(player, hasPlaylist, addPathToPlaylist);
		endContentMenuSection(MenuContentPolicy::Leaf);
	}

	drawVisualizerSection(player);
	drawEqualizerSection(player);

	if (beginContentMenuSection("projectM", 0, MenuContentPolicy::ContentThenNested)) {
		drawProjectMSection(
			projectM,
			projectMInitialized,
			randomProjectMPreset,
			reloadProjectMPresets,
			reloadProjectMTextures,
			loadPlayerProjectMTexture,
			loadCustomProjectMTexture,
			false);
		endContentMenuSection(MenuContentPolicy::ContentThenNested);
	} else {
		drawProjectMSection(
			projectM,
			projectMInitialized,
			randomProjectMPreset,
			reloadProjectMPresets,
			reloadProjectMTextures,
			loadPlayerProjectMTexture,
			loadCustomProjectMTexture,
			true);
	}

	drawExtendedSections(player, mediaDisplayState, false);
	ImGui::Dummy(ImVec2(0.0f, kButtonSpacing));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0.0f, kButtonSpacing));

	const bool hasDetachedSections = ofVlcPlayer4GuiControls::hasDetachedSections();
	const float closeDetachedWidth = 160.0f;
	const float centeredButtonX = std::max(0.0f, (ImGui::GetContentRegionAvail().x - closeDetachedWidth) * 0.5f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centeredButtonX);
	ImGui::BeginDisabled(!hasDetachedSections);
	if (ImGui::Button("Close Detached Menus", ImVec2(closeDetachedWidth, 0.0f))) {
		ofVlcPlayer4GuiControls::closeAllDetachedSections();
	}
	ImGui::EndDisabled();
	ImGui::Separator();

}

void ofVlcPlayer4Gui::drawAudioSection(ofxVlc4 & player, bool detachedOnly) {
	const auto & layout = layoutMetrics;
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Audio", MenuContentPolicy::NestedOnly)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Audio", MenuContentPolicy::NestedOnly, false);
	if (!open) {
		if (detachedOnly) {
			audioSection.drawContent(player, kLabelInnerSpacing, layout.compactControlWidth, kWideSliderWidth, true);
		}
		return;
	}

	audioSection.drawContent(player, kLabelInnerSpacing, layout.compactControlWidth, kWideSliderWidth);
	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::NestedOnly);
}

void ofVlcPlayer4Gui::drawMediaSection(ofxVlc4 & player, bool detachedOnly) {
	const auto & layout = layoutMetrics;
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Media", MenuContentPolicy::NestedOnly)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Media", MenuContentPolicy::NestedOnly, false);
	if (!open) {
		if (detachedOnly) {
			mediaSection.drawContent(
				player,
				kLabelInnerSpacing,
				layout.compactControlWidth,
				layout.inputLabelPadding,
				layout.dualActionButtonWidth,
				kButtonSpacing,
				true);
		}
		return;
	}

	mediaSection.drawContent(
		player,
		kLabelInnerSpacing,
		layout.compactControlWidth,
		layout.inputLabelPadding,
		layout.dualActionButtonWidth,
		kButtonSpacing);
	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::NestedOnly);
}

void ofVlcPlayer4Gui::drawVlcHelpSection(ofxVlc4 & player, bool detachedOnly) {
	const auto & layout = layoutMetrics;
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("VLC Help", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("VLC Help", MenuContentPolicy::Leaf, false);
	if (!open) {
		return;
	}

	if (vlcHelpTextCache.empty()) {
		refreshVlcHelpText(player);
	}

	int helpModeIndex = selectedVlcHelpModeIndex;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(layout.compactControlWidth);
	if (ofVlcPlayer4GuiControls::drawComboWithWheel(
		"Mode",
		helpModeIndex,
		kVlcHelpModeLabels,
		IM_ARRAYSIZE(kVlcHelpModeLabels))) {
		selectedVlcHelpModeIndex = helpModeIndex;
		refreshVlcHelpText(player);
	}
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(labeledInputWidth(layout));
	const bool moduleSubmitted = ImGui::InputText(
		"Module",
		vlcHelpModuleName,
		sizeof(vlcHelpModuleName),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();

	const float actionWidth = (layout.compactControlWidth - (kButtonSpacing * 2.0f)) / 3.0f;
	if (ImGui::Button("Refresh", ImVec2(actionWidth, 0.0f)) || moduleSubmitted) {
		refreshVlcHelpText(player);
	}
	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Clear Module", ImVec2(actionWidth, 0.0f))) {
		vlcHelpModuleName[0] = '\0';
		refreshVlcHelpText(player);
	}
	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Log", ImVec2(actionWidth, 0.0f))) {
		const std::string moduleName = trimGuiString(vlcHelpModuleName);
		if (!moduleName.empty()) {
			player.printVlcModuleHelp(moduleName);
		} else {
			player.printVlcHelp(vlcHelpModeFromIndex(selectedVlcHelpModeIndex));
		}
	}

	ImGui::TextDisabled("Examples: adjust, sharpen, deinterlace, sepia, mirror, equalizer, compressor");
	if (ImGui::BeginChild("vlc_help_text", ImVec2(0.0f, 280.0f), true, ImGuiWindowFlags_HorizontalScrollbar)) {
		ImGui::TextUnformatted(vlcHelpTextCache.empty() ? "No VLC help text available." : vlcHelpTextCache.c_str());
	}
	ImGui::EndChild();

	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4Gui::drawEqualizerSection(ofxVlc4 & player) {
	const auto & layout = layoutMetrics;
	if (!beginContentMenuSection("Equalizer", 0, MenuContentPolicy::Leaf)) {
		return;
	}

	equalizerSection.drawContent(player, layout.actionButtonWidth);
	endContentMenuSection(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4Gui::drawVisualizerSection(ofxVlc4 & player) {
	const auto & layout = layoutMetrics;
	if (!beginContentMenuSection("Visualizer", 0, MenuContentPolicy::Leaf)) {
		return;
	}

	visualizerSection.drawContent(player, kLabelInnerSpacing, layout.compactControlWidth);
	endContentMenuSection(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4Gui::drawVideoViewSection(ofxVlc4 & player, bool detachedOnly) {
	const auto & layout = layoutMetrics;
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Video", MenuContentPolicy::ContentThenNested)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Video", MenuContentPolicy::ContentThenNested, false);
	if (!open) {
		if (detachedOnly) {
			videoSection.drawViewContent(player, kLabelInnerSpacing, layout.compactControlWidth, true);
		}
		return;
	}

	windowsSection.drawVideoOutputControls(player, kLabelInnerSpacing);
	ImGui::Separator();
	videoSection.drawViewContent(player, kLabelInnerSpacing, layout.compactControlWidth);
	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::ContentThenNested);
}

void ofVlcPlayer4Gui::drawVideoAdjustmentsSection(ofxVlc4 & player, bool detachedOnly) {
	const auto & layout = layoutMetrics;
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Adjustments", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Adjustments", MenuContentPolicy::Leaf, false);
	if (!open) {
		return;
	}

	videoSection.drawAdjustmentsContent(player, kLabelInnerSpacing, layout.actionButtonWidth, kWideSliderWidth);
	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4Gui::drawVideo3DSection(ofxVlc4 & player, bool detachedOnly) {
	const auto & layout = layoutMetrics;
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("3D", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("3D", MenuContentPolicy::Leaf, false);
	if (!open) {
		return;
	}

	videoSection.draw3DContent(player, kLabelInnerSpacing, layout.compactControlWidth, layout.actionButtonWidth);
	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4Gui::drawExtendedSections(
	ofxVlc4 & player,
	const MediaDisplayState & mediaDisplayState,
	bool detachedOnly) {
	if (!detachedOnly && !beginContentMenuSection("Advanced", 0, MenuContentPolicy::NestedOnly)) {
		drawExtendedSections(player, mediaDisplayState, true);
		return;
	}

	const bool appearanceOpen = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Appearance", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Appearance", MenuContentPolicy::Leaf, false);
	if (appearanceOpen) {
		int themeIndex = selectedThemeIndex;
		if (ofVlcPlayer4GuiControls::drawComboWithWheel(
			"Theme",
			themeIndex,
			ofVlcPlayer4GuiStyle::kThemeLabels,
			IM_ARRAYSIZE(ofVlcPlayer4GuiStyle::kThemeLabels))) {
			selectedThemeIndex = themeIndex;
			ImGui::MarkIniSettingsDirty();
		}
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}
	drawAudioSection(player, detachedOnly);
	drawMediaSection(player, detachedOnly);
	drawVlcHelpSection(player, detachedOnly);
	drawVideoViewSection(player, detachedOnly);
	drawVideoAdjustmentsSection(player, detachedOnly);
	drawVideo3DSection(player, detachedOnly);
	drawMediaInfoSection(mediaDisplayState, detachedOnly);

	const bool shortcutsOpen = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Shortcuts", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Shortcuts", MenuContentPolicy::Leaf, false);
	if (shortcutsOpen) {
		ImGui::TextUnformatted("Space  Play / Pause");
		ImGui::TextUnformatted("S      Stop");
		ImGui::TextUnformatted("N      Next");
		ImGui::TextUnformatted("P      Previous");
		ImGui::TextUnformatted("Left   Previous");
		ImGui::TextUnformatted("Right  Next");
		ImGui::TextUnformatted("M      Mute");
		ImGui::TextUnformatted("F      Toggle Fullscreen");
		ImGui::TextUnformatted("+/-    Volume");
		ImGui::TextUnformatted("R      Random projectM preset");
		ImGui::TextUnformatted("Del    Delete selected playlist items");
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (!detachedOnly) {
		endContentMenuSection(MenuContentPolicy::NestedOnly);
	}
}

void ofVlcPlayer4Gui::syncProjectMTextInputs(const ofxProjectM & projectM) {
	if (projectMPlaylistFilter[0] == '\0') {
		const std::string filterText = joinStrings(projectM.getPlaylistFilter());
		if (!filterText.empty()) {
			std::snprintf(projectMPlaylistFilter, sizeof(projectMPlaylistFilter), "%s", filterText.c_str());
		}
	}

	if (projectMTextureSearchPath[0] == '\0') {
		const std::string textureSearchText = joinStrings(projectM.getTextureSearchPaths());
		if (!textureSearchText.empty()) {
			std::snprintf(projectMTextureSearchPath, sizeof(projectMTextureSearchPath), "%s", textureSearchText.c_str());
		}
	}
}

void ofVlcPlayer4Gui::drawProjectMSection(
	ofxProjectM & projectM,
	bool projectMInitialized,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture,
	bool detachedOnly) {
	const auto & layout = layoutMetrics;
	syncProjectMTextInputs(projectM);

	if (!detachedOnly) {
		ImGui::BeginDisabled(!projectMInitialized);
		if (ImGui::Button("Random Preset", ImVec2(layout.actionButtonWidth, 0.0f))) {
			randomProjectMPreset();
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Reload Presets", ImVec2(layout.actionButtonWidth, 0.0f))) {
			reloadProjectMPresets();
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Reload Textures", ImVec2(layout.actionButtonWidth, 0.0f))) {
			reloadProjectMTextures();
		}
		ImGui::EndDisabled();
		ImGui::Separator();
	}

	const bool runtimeOpen = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Runtime", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Runtime", MenuContentPolicy::Leaf, false);
	if (runtimeOpen) {
		ImGui::Text("Preset: %s", projectM.getPresetName().c_str());

		const auto versionInfo = ofxProjectM::getVersionInfo();
		ImGui::Text("Version: %s", versionInfo.versionString.empty() ? "unknown" : versionInfo.versionString.c_str());
		ImGui::Text("Last Frame Time: %.4f s", projectM.getLastFrameTime());

		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
		ImGui::PushItemWidth(layout.compactControlWidth);

		int presetDuration = static_cast<int>(std::round(projectM.getPresetDuration()));
		if (ImGui::SliderInt("Preset Duration", &presetDuration, 1, 180)) {
			projectM.setPresetDuration(static_cast<double>(presetDuration));
		}

		bool presetStartClean = projectM.isPresetStartClean();
		if (ImGui::Checkbox("Start Clean", &presetStartClean)) {
			projectM.setPresetStartClean(presetStartClean);
		}

		float easterEgg = projectM.getEasterEgg();
		if (ImGui::SliderFloat("Easter Egg", &easterEgg, 0.0f, 2.0f, "%.2f")) {
			projectM.setEasterEgg(easterEgg);
		}

		float texelOffsetX = 0.0f;
		float texelOffsetY = 0.0f;
		projectM.getTexelOffset(texelOffsetX, texelOffsetY);
		float texelOffset[2] = { texelOffsetX, texelOffsetY };
		if (ImGui::DragFloat2("Texel Offset", texelOffset, 0.001f, -1.0f, 1.0f, "%.3f")) {
			projectM.setTexelOffset(texelOffset[0], texelOffset[1]);
		}

		double frameTime = projectM.getFrameTime();
		float frameTimeValue = frameTime >= 0.0 ? static_cast<float>(frameTime) : -1.0f;
		if (ImGui::InputFloat("Frame Time", &frameTimeValue, 0.01f, 0.1f, "%.3f")) {
			if (frameTimeValue < 0.0f) {
				projectM.clearFrameTime();
			} else {
				projectM.setFrameTime(frameTimeValue);
			}
		}

		ImGui::PopItemWidth();
		ImGui::PopStyleVar();

		if (ImGui::Button("Clear Frame Time", ImVec2(layout.actionButtonWidth, 0.0f))) {
			projectM.clearFrameTime();
		}
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	const bool presetPlaylistOpen = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Preset Playlist", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Preset Playlist", MenuContentPolicy::Leaf, false);
	if (presetPlaylistOpen) {

		bool shuffleEnabled = projectM.isShuffleEnabled();
		if (ImGui::Checkbox("Shuffle", &shuffleEnabled)) {
			projectM.setShuffleEnabled(shuffleEnabled);
		}

		int retryCount = static_cast<int>(projectM.getRetryCount());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
		ImGui::PushItemWidth(compactLabeledInputWidth(layout));
		if (ImGui::InputInt("Retry Count", &retryCount)) {
			projectM.setRetryCount(static_cast<std::uint32_t>(std::max(0, retryCount)));
		}
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();

		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
		ImGui::PushItemWidth(labeledInputWidth(layout));
		ImGui::InputText("Filter", projectMPlaylistFilter, sizeof(projectMPlaylistFilter));
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();

		if (ImGui::Button("Apply Filter", ImVec2(layout.actionButtonWidth, 0.0f))) {
			projectM.setPlaylistFilter(parseSemicolonSeparatedItems(projectMPlaylistFilter));
			projectM.applyPlaylistFilter();
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Clear Filter", ImVec2(layout.actionButtonWidth, 0.0f))) {
			projectM.setPlaylistFilter({});
			projectM.applyPlaylistFilter();
			projectMPlaylistFilter[0] = '\0';
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Play Last", ImVec2(layout.actionButtonWidth, 0.0f))) {
			projectM.playLastPreset(true);
		}

		ImGui::Text("Preset Count: %d", projectM.getPresetCount());
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	const bool texturesAndDebugOpen = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Textures & Debug", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Textures & Debug", MenuContentPolicy::Leaf, false);
	if (texturesAndDebugOpen) {

		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
		ImGui::PushItemWidth(compactLabeledInputWidth(layout));
		const bool submittedTexturePath = ImGui::InputText(
			"Path##projectMTexture",
			projectMTexturePath,
			sizeof(projectMTexturePath),
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::InputText("Search Path", projectMTextureSearchPath, sizeof(projectMTextureSearchPath));
		ImGui::InputText("Debug Path", projectMDebugImagePath, sizeof(projectMDebugImagePath));
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();

		const bool loadTextureRequested =
			ImGui::Button("Load Image Texture", ImVec2(layout.dualActionButtonWidth, 0.0f)) || submittedTexturePath;
		if (loadTextureRequested && loadCustomProjectMTexture(projectMTexturePath)) {
			projectMTexturePath[0] = '\0';
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Load Video Texture", ImVec2(layout.dualActionButtonWidth, 0.0f))) {
			loadPlayerProjectMTexture();
		}

		if (ImGui::Button("Apply Search Path", ImVec2(layout.dualActionButtonWidth, 0.0f))) {
			projectM.setTextureSearchPaths(parseSemicolonSeparatedItems(projectMTextureSearchPath));
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Write Debug Image", ImVec2(layout.dualActionButtonWidth, 0.0f))) {
			projectM.writeDebugImageOnNextFrame(projectMDebugImagePath);
		}
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}
}

void ofVlcPlayer4Gui::drawMediaInfoSection(const MediaDisplayState & mediaDisplayState, bool detachedOnly) {
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Info", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Info", MenuContentPolicy::Leaf, false);
	if (!open) {
		return;
	}

	if (!mediaDisplayState.fileName.empty() || !mediaDisplayState.metadata.empty()) {
		drawCurrentMediaInfoContent(
			mediaDisplayState.path,
			mediaDisplayState.fileName,
			mediaDisplayState.metadata,
			mediaFileInfoCache);
	} else {
		ImGui::TextDisabled("No media selected");
	}
	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4Gui::updateMediaFileInfoCache(const MediaDisplayState & mediaDisplayState) {
	if (mediaFileInfoCache.path == mediaDisplayState.path) {
		return;
	}

	mediaFileInfoCache = {};
	mediaFileInfoCache.path = mediaDisplayState.path;
	if (mediaDisplayState.path.empty()) {
		return;
	}

	mediaFileInfoCache.isUri = mediaDisplayState.path.find("://") != std::string::npos;
	mediaFileInfoCache.extension = ofToLower(ofFilePath::getFileExt(mediaDisplayState.path));
	if (mediaFileInfoCache.isUri) {
		return;
	}

	const std::filesystem::path fsPath(mediaDisplayState.path);
	std::error_code ec;
	if (!std::filesystem::exists(fsPath, ec) || ec) {
		return;
	}

	mediaFileInfoCache.fileInfoAvailable = true;
	if (std::filesystem::is_regular_file(fsPath, ec) && !ec) {
		const auto bytes = std::filesystem::file_size(fsPath, ec);
		if (!ec) {
			mediaFileInfoCache.sizeText = formatFileSize(bytes);
		}
	}

	mediaFileInfoCache.modifiedText = formatLastWriteTime(fsPath);
}

void ofVlcPlayer4Gui::handleImGuiShortcuts(
	ofxVlc4 & player,
	bool projectMInitialized,
	const std::function<void()> & randomProjectMPreset) {
	if (ImGui::GetIO().WantTextInput) {
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
		if (player.isPlaying()) {
			player.pause();
		} else {
			followPlaybackSelectionEnabled = true;
			player.play();
		}
	}

	if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
		player.stop();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
		player.toggleMute();
		volume = player.getVolume();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
		volume = ofClamp(volume + 5, 0, 100);
		player.setVolume(volume);
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
		volume = ofClamp(volume - 5, 0, 100);
		player.setVolume(volume);
	}

	if (ImGui::IsKeyPressed(ImGuiKey_N, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
		followPlaybackSelectionEnabled = true;
		player.nextMediaListItem();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_P, false) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
		followPlaybackSelectionEnabled = true;
		player.previousMediaListItem();
	}

	if (projectMInitialized && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
		randomProjectMPreset();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
		deleteSelected(player);
	}
}

void ofVlcPlayer4Gui::drawPlaylistSection(ofxVlc4 & player) {
	const auto playlistState = player.getPlaylistStateInfo();
	const auto & playlistItems = playlistState.items;
	const int currentPlaying = playlistState.currentIndex;
	const ImGuiPayload * activePayload = ImGui::GetDragDropPayload();
	const bool draggingPlaylistItem = activePayload && activePayload->IsDataType("PLAYLIST_INDEX");
	const auto * dragPayload = draggingPlaylistItem ? static_cast<const PlaylistDragPayload *>(activePayload->Data) : nullptr;
	const ImGuiStyle & style = ImGui::GetStyle();
	const auto & playlistStyle = ofVlcPlayer4GuiStyle::kPlaylistWindowStyleMetrics;
	const float playlistInsetY = kCompactInnerPaddingY;
	const float textRowHeight = ImGui::GetTextLineHeight();
	const float additionalRowHeight = textRowHeight + style.ItemSpacing.y;
	const float childPaddingY = playlistStyle.windowPadding.y * 2.0f;
	const int visibleRowCount = std::max(1, static_cast<int>(playlistItems.size()));
	const float playlistRowsHeight =
		textRowHeight + (additionalRowHeight * (visibleRowCount - 1));
	const float desiredPlaylistHeight =
		childPaddingY + playlistInsetY + playlistRowsHeight + 10.0f;
	const float playlistHeight = std::min(desiredPlaylistHeight, 300.0f);

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, playlistStyle.childRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, playlistStyle.windowPadding);
	if (ImGui::BeginChild("playlist_child", ImVec2(0, playlistHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
		const float playlistInsetX = kLabelInnerSpacing.x;
		ImGui::Dummy(ImVec2(0.0f, playlistInsetY));
		if (playlistItems.empty()) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + playlistInsetX);
			ImGui::TextDisabled("No playlist items yet");
			ImGui::TextDisabled("Drop files or use the Path field below.");
		}
		static int lastScrolledToPlayingIndex = -1;
		static int liveDragIndex = -1;
		const int draggedIndex = dragPayload ? dragPayload->index : -1;
		if (!draggingPlaylistItem) {
			liveDragIndex = -1;
		} else if (liveDragIndex < 0) {
			liveDragIndex = draggedIndex;
		}

		std::vector<int> displayOrder(playlistItems.size());
		std::iota(displayOrder.begin(), displayOrder.end(), 0);

		for (int row = 0; row < static_cast<int>(displayOrder.size()); ++row) {
			const int i = displayOrder[row];
			const bool isSelected = selectedIndices.count(i) > 0;
			const bool isPlaying = (i == currentPlaying);
			const auto & item = playlistItems[i];
			std::string label = ofToString(row) + " - " + item.label;

			ImVec4 baseColor = ImVec4(0, 0, 0, 0);
			ImVec4 hoverColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
			ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
			if (isSelected) {
				baseColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
			}
			if (isPlaying) {
				baseColor = ofVlcPlayer4GuiStyle::themedWindowBgColor();
				hoverColor = ofVlcPlayer4GuiStyle::themedMenuHeaderHoverColor();
				activeColor = ofVlcPlayer4GuiStyle::themedMenuHeaderActiveColor();
			}

			ImGui::PushStyleColor(ImGuiCol_Header, baseColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, activeColor);

			const bool drawHighlighted = isSelected || isPlaying;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + playlistInsetX);
			const bool clicked = ImGui::Selectable(label.c_str(), drawHighlighted, 0, ImVec2(ImGui::GetContentRegionAvail().x - playlistInsetX, 0.0f));
			const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
				PlaylistDragPayload newDragPayload;
				newDragPayload.index = i;
				ImGui::SetDragDropPayload("PLAYLIST_INDEX", &newDragPayload, sizeof(newDragPayload));
				ImGui::TextUnformatted("1 Item Dragged");
				ImGui::EndDragDropSource();
			}

			if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) && draggingPlaylistItem) {
				const int insertIndex = computeInsertIndexForDrag(i, liveDragIndex);
				if (liveDragIndex != insertIndex && liveDragIndex + 1 != insertIndex) {
					const int newIndex = (liveDragIndex < insertIndex) ? (insertIndex - 1) : insertIndex;
					movePlaylistItemAndSelection(
						player,
						liveDragIndex,
						insertIndex,
						selectedIndices,
						selectedIndex,
						lastClickedIndex);
					liveDragIndex = newIndex;
				}
			}

			ImGui::PushStyleColor(ImGuiCol_DragDropTarget, ImVec4(0, 0, 0, 0));
			if (ImGui::BeginDragDropTarget()) {
				if (ImGui::AcceptDragDropPayload("PLAYLIST_INDEX")) {
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopStyleColor();

			if (isPlaying && currentPlaying >= 0 && currentPlaying != lastScrolledToPlayingIndex) {
				ImGui::SetScrollHereY(0.35f);
				lastScrolledToPlayingIndex = currentPlaying;
			}

			if (clicked && !doubleClicked) {
				const bool multi = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
				const bool range = ImGui::GetIO().KeyShift;

				if (range && !playlistItems.empty()) {
					selectedIndices.clear();
					if (lastClickedIndex >= 0) {
						const int a = std::min(lastClickedIndex, i);
						const int b = std::max(lastClickedIndex, i);
						for (int j = a; j <= b; ++j) {
							selectedIndices.insert(j);
						}
					} else {
						selectedIndices.insert(i);
					}
					selectedIndex = i;
					lastClickedIndex = i;
					followPlaybackSelectionEnabled = false;
				} else if (multi) {
					if (selectedIndices.count(i) > 0) {
						selectedIndices.erase(i);
					} else {
						selectedIndices.insert(i);
					}
					selectedIndex = i;
					lastClickedIndex = i;
					followPlaybackSelectionEnabled = false;
				} else {
					selectedIndices.clear();
					selectedIndices.insert(i);
					selectedIndex = i;
					lastClickedIndex = i;
					followPlaybackSelectionEnabled = false;
				}
			}

			ImGui::PopStyleColor(3);

			if (doubleClicked) {
				followPlaybackSelectionEnabled = false;
				player.playIndex(i);
			}
		}

		if (!playlistItems.empty()) {
			if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) && draggingPlaylistItem) {
				if (liveDragIndex >= 0 && liveDragIndex + 1 != static_cast<int>(playlistItems.size())) {
					movePlaylistItemAndSelection(
						player,
						liveDragIndex,
						static_cast<int>(playlistItems.size()),
						selectedIndices,
						selectedIndex,
						lastClickedIndex);
					liveDragIndex = static_cast<int>(playlistItems.size()) - 1;
				}
			}

			ImGui::PushStyleColor(ImGuiCol_DragDropTarget, ImVec4(0, 0, 0, 0));
			if (ImGui::BeginDragDropTarget()) {
				if (ImGui::AcceptDragDropPayload("PLAYLIST_INDEX")) {
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

void ofVlcPlayer4Gui::drawPathSection(
	ofxVlc4 & player,
	bool hasPlaylist,
	const std::function<int(const std::string &)> & addPathToPlaylist) {
	const auto & layout = layoutMetrics;
	// Keep playlist actions grouped here so the collapsible section owns all add/delete mutations.
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(labeledInputWidth(layout));
	bool submittedPath = ImGui::InputText("Path##playlistAdd", addPath, sizeof(addPath), ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
	submittedPath = ImGui::Button("Add", ImVec2(layout.actionButtonWidth, 0.0f)) || submittedPath;
	ImGui::SameLine(0.0f, kButtonSpacing);
	ImGui::BeginDisabled(selectedIndices.empty());
	if (ImGui::Button("Delete Selected", ImVec2(layout.actionButtonWidth, 0.0f)) && !selectedIndices.empty()) {
		deleteSelected(player);
	}
	ImGui::EndDisabled();
	ImGui::SameLine(0.0f, kButtonSpacing);
	ImGui::BeginDisabled(!hasPlaylist);
	if (ImGui::Button("Delete All", ImVec2(layout.actionButtonWidth, 0.0f)) && hasPlaylist) {
		player.clearPlaylist();
		selectedIndices.clear();
		selectedIndex = -1;
		lastClickedIndex = -1;
	}
	ImGui::EndDisabled();

	if (submittedPath) {
		if (addPathToPlaylist(addPath) > 0) {
			addPath[0] = '\0';
		}
	}
}

void ofVlcPlayer4Gui::deleteSelected(ofxVlc4 & player) {
	if (selectedIndices.empty()) {
		return;
	}

	int fallbackIndex = *selectedIndices.begin();
	const auto toDelete = sortedDescending(selectedIndices);
	for (int idx : toDelete) {
		player.removeFromPlaylist(idx);
	}

	selectedIndices.clear();
	selectedIndex = fallbackIndex;
	lastClickedIndex = fallbackIndex;
	normalizeSelection(player);
}

void ofVlcPlayer4Gui::normalizeSelection(ofxVlc4 & player) {
	const auto playlistState = player.getPlaylistStateInfo();
	if (playlistState.empty) {
		selectedIndices.clear();
		selectedIndex = -1;
		lastClickedIndex = -1;
		return;
	}

	std::set<int> normalized;
	for (int idx : selectedIndices) {
		if (isValidPlaylistIndex(playlistState.size, idx)) {
			normalized.insert(idx);
		}
	}
	selectedIndices = std::move(normalized);

	if (!isValidPlaylistIndex(playlistState.size, selectedIndex)) {
		selectedIndex = selectedIndices.empty() ? -1 : *selectedIndices.rbegin();
	}

	if (!isValidPlaylistIndex(playlistState.size, lastClickedIndex)) {
		lastClickedIndex = selectedIndex;
	}
}

void ofVlcPlayer4Gui::followCurrentTrack(ofxVlc4 & player) {
	if (!followPlaybackSelectionEnabled || selectedIndices.empty()) {
		return;
	}

	const int currentPlaying = player.getCurrentIndex();
	if (!isValidPlaylistIndex(player.getPlaylistStateInfo().size, currentPlaying)) {
		return;
	}

	if (selectedIndices.size() <= 1 && selectedIndex != currentPlaying) {
		selectedIndices.clear();
		selectedIndices.insert(currentPlaying);
		selectedIndex = currentPlaying;
		lastClickedIndex = currentPlaying;
	}
}

void ofVlcPlayer4Gui::updateSelection(ofxVlc4 & player) {
	normalizeSelection(player);
	followCurrentTrack(player);
}

void ofVlcPlayer4Gui::handleDragEvent(
	const ofDragInfo & dragInfo,
	ofxVlc4 & player,
	const std::function<int(const std::string &)> & addPathToPlaylist) {
	int addedCount = 0;
	for (const auto & file : dragInfo.files) {
		addedCount += addPathToPlaylist(file.string());
	}

	if (addedCount > 0) {
		normalizeSelection(player);
	}
}

bool ofVlcPlayer4Gui::shouldRenderProjectMPreview() const {
	return windowsSection.shouldRenderProjectMPreview();
}

ofRectangle ofVlcPlayer4Gui::getVideoPreviewScreenRect() const {
	return windowsSection.getVideoPreviewScreenRect();
}

const ofVlcPlayer4GuiVideo & ofVlcPlayer4Gui::getVideoSection() const {
	return videoSection;
}
