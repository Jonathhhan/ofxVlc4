#pragma once

#include "ofxImGui.h"

#include <cstdint>
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
		float buttonSpacing);

private:
	void drawDiagnosticsSubMenu(
		ofxVlc4 & player,
		float inputLabelPadding,
		float singleActionButtonWidth,
		float dualActionButtonWidth,
		float buttonSpacing);
	void drawMetadataSubMenu(
		ofxVlc4 & player,
		float inputLabelPadding,
		float singleActionButtonWidth,
		float dualActionButtonWidth,
		float buttonSpacing);
	void drawDialogsSubMenu(
		ofxVlc4 & player,
		float inputLabelPadding,
		float singleActionButtonWidth,
		float dualActionButtonWidth,
		float buttonSpacing);
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
};
