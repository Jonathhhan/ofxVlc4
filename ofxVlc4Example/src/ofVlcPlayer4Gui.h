#pragma once

#include "ofMain.h"
#ifndef OFXIMGUI_GLFW_FIX_MULTICONTEXT_SECONDARY_VP
#define OFXIMGUI_GLFW_FIX_MULTICONTEXT_SECONDARY_VP 1
#endif
#include "ofxImGui.h"
#include "ofxProjectM.h"
#include "ofxVlc4.h"
#include "ofVlcPlayer4GuiAudio.h"
#include "ofVlcPlayer4GuiEqualizer.h"
#include "ofVlcPlayer4GuiMedia.h"
#include "ofVlcPlayer4GuiVideo.h"
#include "ofVlcPlayer4GuiVisualizer.h"
#include "ofVlcPlayer4GuiWindows.h"

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct MediaDisplayState {
	std::string path;
	std::string fileName;
	std::vector<std::pair<std::string, std::string>> metadata;
};

struct MediaFileInfoCache {
	std::string path;
	std::string extension;
	std::string sizeText;
	std::string modifiedText;
	bool isUri = false;
	bool fileInfoAvailable = false;
};

struct ofVlcPlayer4GuiLayoutMetrics {
	float contentWidth = 0.0f;
	float windowWidth = 0.0f;
	float compactControlWidth = 0.0f;
	float actionButtonWidth = 0.0f;
	float dualActionButtonWidth = 0.0f;
	float inputLabelPadding = 0.0f;
	float headerSliderWidth = 0.0f;
	float headerComboWidth = 0.0f;
};

class ofVlcPlayer4Gui {
public:
	void setup();
	void draw(
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
		const std::function<void(int)> & setCustomSubtitleFontIndex);
	void updateSelection(ofxVlc4 & player);
	void handleDragEvent(
		const ofDragInfo & dragInfo,
		ofxVlc4 & player,
		const std::function<int(const std::string &)> & addPathToPlaylist);
	bool shouldRenderProjectMPreview() const;
	const ofVlcPlayer4GuiVideo & getVideoSection() const;

private:
	void drawImGui(
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
		const std::function<void(int)> & setCustomSubtitleFontIndex);
	void drawHeaderSection(
		ofxVlc4 & player,
		bool hasPlaylist,
		const MediaDisplayState & mediaDisplayState);
	std::string resolveStableHeaderTitle(
		ofxVlc4 & player,
		bool hasPlaylist,
		const MediaDisplayState & mediaDisplayState);
	void drawTransportSection(ofxVlc4 & player, bool hasPlaylist);
	void drawPositionSection(ofxVlc4 & player, bool hasPlaylist);
	void handleImGuiShortcuts(
		ofxVlc4 & player,
		bool projectMInitialized,
		const std::function<void()> & randomProjectMPreset);
	void drawAudioSection(ofxVlc4 & player, bool detachedOnly = false);
	void drawMediaSection(ofxVlc4 & player, bool detachedOnly = false);
	void drawVlcHelpSection(ofxVlc4 & player, bool detachedOnly = false);
	void drawEqualizerSection(ofxVlc4 & player);
	void drawVisualizerSection(ofxVlc4 & player);
	void drawVideoViewSection(ofxVlc4 & player, bool detachedOnly = false);
	void drawVideoAdjustmentsSection(ofxVlc4 & player, bool detachedOnly = false);
	void drawVideo3DSection(ofxVlc4 & player, bool detachedOnly = false);
	void drawProjectMSection(
		ofxProjectM & projectM,
		bool projectMInitialized,
		const std::function<void()> & randomProjectMPreset,
		const std::function<void()> & reloadProjectMPresets,
		const std::function<void()> & reloadProjectMTextures,
		const std::function<void()> & loadPlayerProjectMTexture,
		const std::function<bool(const std::string &)> & loadCustomProjectMTexture,
		bool detachedOnly = false);
	void drawExtendedSections(
		ofxVlc4 & player,
		const MediaDisplayState & mediaDisplayState,
		bool detachedOnly = false);
	void drawPlaybackOptionsSection(
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
		const std::function<bool(const std::string &)> & loadCustomProjectMTexture);
	void drawMediaInfoSection(const MediaDisplayState & mediaDisplayState, bool detachedOnly = false);
	void updateMediaFileInfoCache(const MediaDisplayState & mediaDisplayState);
	void drawPlaylistSection(ofxVlc4 & player);
	void drawPathSection(
		ofxVlc4 & player,
		bool hasPlaylist,
		const std::function<int(const std::string &)> & addPathToPlaylist);
	void deleteSelected(ofxVlc4 & player);
	void normalizeSelection(ofxVlc4 & player);
	void followCurrentTrack(ofxVlc4 & player);
	void syncProjectMTextInputs(const ofxProjectM & projectM);
	void refreshVlcHelpText(ofxVlc4 & player);

	ofxImGui::Gui gui;

	int selectedIndex = -1;
	int lastClickedIndex = -1;
	std::set<int> selectedIndices;

	int volume = 50;
	char addPath[1024] = "";
	char projectMTexturePath[1024] = "";
	char projectMPlaylistFilter[512] = "";
	char projectMTextureSearchPath[1024] = "";
	char projectMDebugImagePath[1024] = "";
	bool positionSliderActive = false;
	float pendingSeekPosition = 0.0f;
	int selectedThemeIndex = 0;
	bool showMainWindow = true;

	bool followPlaybackSelectionEnabled = true;
	bool showRemainingTime = false;
	int selectedVlcHelpModeIndex = 2;
	char vlcHelpModuleName[128] = "";
	std::string vlcHelpTextCache;
	ofVlcPlayer4GuiAudio audioSection;
	ofVlcPlayer4GuiEqualizer equalizerSection;
	ofVlcPlayer4GuiMedia mediaSection;
	ofVlcPlayer4GuiVideo videoSection;
	ofVlcPlayer4GuiVisualizer visualizerSection;
	ofVlcPlayer4GuiWindows windowsSection;
	MediaFileInfoCache mediaFileInfoCache;
	std::unordered_map<std::string, std::string> mediaHeaderTitleCache;
	ofVlcPlayer4GuiLayoutMetrics layoutMetrics;
};
