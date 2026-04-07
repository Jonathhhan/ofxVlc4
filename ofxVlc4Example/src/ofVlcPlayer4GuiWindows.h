#pragma once

#include "ofMain.h"
#include "ofxImGui.h"

class ofTexture;
class ofxProjectM;
class ofxVlc4;

class ofVlcPlayer4GuiWindows {
public:
	void handleFullscreenEscape();
	void drawDisplayControls(const ImVec2 & labelInnerSpacing, float sectionSpacing);
	void drawVideoOutputControls(ofxVlc4 & player, const ImVec2 & labelInnerSpacing);
	void drawWindows(
		const ofxVlc4 & player,
		ofxProjectM & projectM,
		bool projectMInitialized,
		const ofTexture & videoPreviewTexture,
		float videoPreviewWidth,
		float videoPreviewHeight);
	bool shouldRenderProjectMPreview() const;
	bool hasAnyVisibleWindow() const;
	ofRectangle getVideoPreviewScreenRect() const;

private:
	ofRectangle videoPreviewScreenRect;
	bool hasVideoPreviewScreenRect = false;
};
