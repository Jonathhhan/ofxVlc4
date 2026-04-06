#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxVlc4.h"

#include <array>
#include <string>
#include <vector>

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed(int key);
	void dragEvent(ofDragInfo dragInfo);
	void windowResized(int w, int h);

private:
	void drawPreview();
	void drawControlPanel();
	void loadMediaPath(const std::string & path, bool autoPlay = true);
	void replacePlaylistFromPaths(const std::vector<std::string> & paths, bool autoPlay = true);
	bool canAddAnyPlaylistPath(const std::vector<std::string> & paths) const;
	void openMediaDialog();
	void resetViewpoint();
	void nudgeViewpoint(float deltaYaw, float deltaPitch, float deltaRoll, float deltaFov);
	std::string currentMediaLabel() const;
	std::string projectionLabel(ofxVlc4::VideoProjectionMode mode) const;
	std::string stereoLabel(ofxVlc4::VideoStereoMode mode) const;

	ofxVlc4 player;
	ofxImGui::Gui gui;

	bool shuttingDown = false;
	float previewMargin = 24.0f;
	std::string infoStatus;

	std::array<const char *, 4> projectionModeLabels = {
		"Auto",
		"Rectangular",
		"360 Equirectangular",
		"Cubemap"
	};
	std::array<ofxVlc4::VideoProjectionMode, 4> projectionModes = {
		ofxVlc4::VideoProjectionMode::Auto,
		ofxVlc4::VideoProjectionMode::Rectangular,
		ofxVlc4::VideoProjectionMode::Equirectangular,
		ofxVlc4::VideoProjectionMode::CubemapStandard
	};

	std::array<const char *, 5> stereoModeLabels = {
		"Auto",
		"Stereo",
		"Left Eye",
		"Right Eye",
		"Side By Side"
	};
	std::array<ofxVlc4::VideoStereoMode, 5> stereoModes = {
		ofxVlc4::VideoStereoMode::Auto,
		ofxVlc4::VideoStereoMode::Stereo,
		ofxVlc4::VideoStereoMode::LeftEye,
		ofxVlc4::VideoStereoMode::RightEye,
		ofxVlc4::VideoStereoMode::SideBySide
	};
};
