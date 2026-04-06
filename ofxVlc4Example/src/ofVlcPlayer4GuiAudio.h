#pragma once

#include "ofxImGui.h"

class ofxVlc4;

class ofVlcPlayer4GuiAudio {
public:
	void drawContent(
		ofxVlc4 & player,
		const ImVec2 & labelInnerSpacing,
		float compactControlWidth,
		float wideSliderWidth);

private:
	char audioFilterChain[512] = "";
	std::string bypassedAudioFilterChain;
	bool audioFiltersBypassed = false;
};
