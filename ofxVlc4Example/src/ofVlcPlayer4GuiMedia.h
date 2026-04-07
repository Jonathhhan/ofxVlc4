#pragma once

#include "ofxImGui.h"

#include <cstdint>
#include <functional>
#include <string>

class ofxVlc4;

class ofVlcPlayer4GuiMedia {
public:
	void drawContent(
		ofxVlc4 & player,
		const ImVec2 & labelInnerSpacing,
		float compactControlWidth,
		float inputLabelPadding,
		float dualActionButtonWidth,
		float buttonSpacing,
		bool detachedOnly = false);
	void setCustomSubtitleCallbacks(
		std::function<bool(const std::string &)> loadCallback,
		std::function<void()> clearCallback,
		std::function<std::string()> statusCallback,
		std::function<std::vector<std::string>()> fontLabelsCallback,
		std::function<int()> selectedFontIndexCallback,
		std::function<void(int)> setFontIndexCallback);
	bool hasDetachedDiagnosticsWindow() const;

private:
	void drawDiagnosticsSubMenu(
		ofxVlc4 & player,
		float inputLabelPadding,
		float singleActionButtonWidth,
		float dualActionButtonWidth,
		float buttonSpacing,
		bool detachedOnly = false);
	void drawMetadataSubMenu(
		ofxVlc4 & player,
		float inputLabelPadding,
		float singleActionButtonWidth,
		float dualActionButtonWidth,
		float buttonSpacing,
		bool detachedOnly = false);
	void drawDialogsSubMenu(
		ofxVlc4 & player,
		float inputLabelPadding,
		float singleActionButtonWidth,
		float dualActionButtonWidth,
		float buttonSpacing,
		bool detachedOnly = false);
	void resetMetadataEditor();
	void syncMetadataEditor(ofxVlc4 & player, const std::string & currentMediaId, bool hasCurrentMedia);
	void syncLibVlcLogFilePath(ofxVlc4 & player);

	std::string mediaSlavePath;
	int mediaSlaveTypeIndex = 0;
	int mediaDiscovererCategoryIndex = 1;
	int selectedDiscoveredMediaIndex = 0;
	std::string bookmarkLabel;
	int selectedBookmarkIndex = 0;
	std::uintptr_t activeLoginDialogToken = 0;
	std::string dialogUsername;
	std::string dialogPassword;
	bool dialogStore = false;
	std::string editMetaTitle;
	std::string editMetaArtist;
	std::string editMetaAlbum;
	std::string metaExtraName;
	std::string metaExtraValue;
	int selectedMetaExtraIndex = 0;
	std::string metadataPath;
	bool metadataLoadedFromPlayer = false;
	std::string libVlcLogFilePath;
	bool libVlcLogFilePathLoaded = false;
	std::function<bool(const std::string &)> loadCustomSubtitleCallback;
	std::function<void()> clearCustomSubtitleCallback;
	std::function<std::string()> customSubtitleStatusCallback;
	std::function<std::vector<std::string>()> customSubtitleFontLabelsCallback;
	std::function<int()> customSubtitleSelectedFontIndexCallback;
	std::function<void(int)> customSubtitleSetFontIndexCallback;
};
