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
	void mousePressed(int x, int y, int button);
	void mouseDragged(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void dragEvent(ofDragInfo dragInfo);
	void windowResized(int w, int h);

private:
	enum class RenderMode {
		LibVlc360,
		Sphere
	};

	enum class SphereLayout {
		Mono,
		SideBySide,
		TopBottom
	};

	void drawPreview();
	void drawControlPanel();
	void loadSeedMedia();
	void loadMediaPath(const std::string & path, bool autoPlay = true);
	void replacePlaylistFromPaths(const std::vector<std::string> & paths, bool autoPlay = true);
	std::vector<std::string> collectSupportedPaths(const std::vector<std::string> & paths) const;
	void openMediaDialog();
	void resetCameraView();
	void applyCameraFov();
	void applyRenderModeBackend();
	void applyLibVlc360Viewpoint(bool force = false);
	void releaseLibVlc360Viewpoint();
	void rebuildSphereTexCoords(const ofTexture & texture);
	void startPlayback();
	std::string currentMediaLabel() const;

	ofxVlc4 player;
	ofxImGui::Gui gui;
	ofEasyCam camera;
	ofSpherePrimitive sphere;

	bool shuttingDown = false;
	float previewMargin = 24.0f;
	float sphereRadius = 900.0f;
	float cameraFov = 80.0f;
	float mappedTextureWidth = 0.0f;
	float mappedTextureHeight = 0.0f;
	float libVlcYaw = 0.0f;
	float libVlcPitch = 0.0f;
	float libVlcRoll = 0.0f;
	int lastMouseX = 0;
	int lastMouseY = 0;
	RenderMode renderMode = RenderMode::Sphere;
	bool libVlc360Applied = false;
	bool libVlcViewDirty = false;
	bool libVlcMouseDragging = false;
	bool startupSeedPending = false;
	int startupSeedDelayFrames = 0;
	std::string pendingSeedPath;
	std::string infoStatus;

	// libVLC 360 projection and stereo settings (indices into label arrays)
	int libVlcProjectionIndex = 0;
	int libVlcStereoIndex = 0;

	// Sphere-mode stereo layout
	SphereLayout sphereLayout = SphereLayout::Mono;
	int sphereEyeIndex = 0;
};
