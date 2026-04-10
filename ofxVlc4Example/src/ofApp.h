#pragma once

#include "ofMain.h"
#include "ofVlcPlayer4Gui.h"
#include "SimpleSrtSubtitleParser.h"
#include "ofxProjectM.h"
#include "ofxVlc4.h"

#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
	enum class ProjectMTextureSourceMode {
		MainPlayerVideo,
		CustomImage,
		InternalTextures
	};

	enum class RestorePlaybackState {
		Stopped,
		Paused,
		Playing
	};

	void setup();
	void update();
	void draw();
	void keyPressed(int key);
	void exit();

	void dragEvent(ofDragInfo dragInfo);
	int addPathToPlaylist(const std::string & rawPath);
	void reloadProjectMTextures(bool useStandardTextures = false, bool restartPreset = true);
	bool loadCustomProjectMTexture(const std::string & rawPath);
	bool loadCustomSubtitleFile(const std::string & rawPath);
	void clearCustomSubtitleFile();
	std::string customSubtitleStatus() const;
	void setupCustomSubtitleFonts();
	bool reloadCustomSubtitleFont();
	std::vector<std::string> customSubtitleFontLabels() const;
	int customSubtitleFontSelection() const;
	void setCustomSubtitleFontSelection(int index);
	void loadPlayerProjectMTexture();
	void initializePlayer(const std::vector<std::string> * playlistOverride = nullptr, int restoreIndex = -1, RestorePlaybackState restorePlaybackState = RestorePlaybackState::Stopped, int restoreTimeMs = 0, int restoreVolume = 50, ofxVlc4::PlaybackMode restoreMode = ofxVlc4::PlaybackMode::Default);
	void applyAudioVisualizerSettings();
	void drawPlayerToFbo(ofxVlc4 & sourcePlayer, ofFbo & targetFbo, float width, float height, bool preserveAspect);
	void refreshProjectMSourceTexture();
	void applyProjectMTexture();
	bool hasProjectMSourceSize() const;
	void ensureProjectMInitialized();
	void setupAnaglyphShader();
	void updateAnaglyphPreview(const ofTexture & sourceTexture, float sourceWidth, float sourceHeight, const AnaglyphSettings & settings);
	const SimpleSrtSubtitleCue * findActiveCustomSubtitleCue() const;
	void drawCustomSubtitleOverlay() const;

	void audioOut(ofSoundBuffer & buffer);

	ofVlcPlayer4Gui remoteGui;

	ofxVlc4 player; // GUI-controlled

	ofSoundStream soundStream;
	ofxProjectM projectM;
	ofImage projectMCustomTextureImage;
	ofImage videoPreviewArtworkImage;
	ofFbo projectMSourceFbo;
	ofFbo videoPreviewFbo;
	ofFbo anaglyphPreviewFbo;
	ofShader anaglyphShader;
	std::string projectMCustomTexturePath;
	std::string videoPreviewArtworkPath;
	float videoPreviewWidth = 0.0f;
	float videoPreviewHeight = 0.0f;
	bool videoPreviewHasContent = false;
	bool videoPreviewShowsVideo = false;
	bool anaglyphShaderReady = false;
	bool showPlaybackStateOverlay = false;
	std::string customSubtitlePath;
	std::string customSubtitleLoadError;
	std::vector<SimpleSrtSubtitleCue> customSubtitleCues;
	std::vector<std::string> customSubtitleFontPaths;
	std::vector<std::string> customSubtitleFontNames;
	ofTrueTypeFont customSubtitleFont;
	int customSubtitleFontIndex = -1;
	bool customSubtitleFontLoaded = false;

	int bufferSize = 128;
	int outChannels = 2;
	bool projectMInitialized = false;
	ProjectMTextureSourceMode projectMTextureSourceMode = ProjectMTextureSourceMode::InternalTextures;
};
