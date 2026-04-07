#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxVlc4.h"

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
	void loadSeedMedia();
	void loadMediaPath(const std::string & path, bool autoPlay = true);
	void replacePlaylistFromPaths(const std::vector<std::string> & paths, bool autoPlay = true);
	std::vector<std::string> collectSupportedPaths(const std::vector<std::string> & paths) const;
	void openMediaDialog();
	void resetCameraView();
	void applyCameraFov();
	std::string currentMediaLabel() const;

	ofxVlc4 player;
	ofxImGui::Gui gui;
	ofEasyCam camera;
	ofSpherePrimitive sphere;

	bool shuttingDown = false;
	float previewMargin = 24.0f;
	float sphereRadius = 900.0f;
	float cameraFov = 80.0f;
	std::string infoStatus;
};
