#pragma once

#include "ofxImGui.h"

class ofxVlc4;

enum class AnaglyphColorMode {
	RedCyan = 0,
	GreenMagenta,
	AmberBlue
};

struct AnaglyphSettings {
	bool enabled = false;
	AnaglyphColorMode colorMode = AnaglyphColorMode::RedCyan;
	bool swapEyes = false;
	float eyeSeparation = 0.0f;
};

class ofVlcPlayer4GuiVideo {
public:
	void drawViewContent(
		ofxVlc4 & player,
		const ImVec2 & labelInnerSpacing,
		float compactControlWidth,
		bool detachedOnly = false);
	void drawAdjustmentsContent(
		ofxVlc4 & player,
		const ImVec2 & labelInnerSpacing,
		float actionButtonWidth,
		float wideSliderWidth);
	void draw3DContent(
		ofxVlc4 & player,
		const ImVec2 & labelInnerSpacing,
		float compactControlWidth,
		float actionButtonWidth);

	AnaglyphSettings getAnaglyphSettings() const;

private:
	char videoFilterChain[256] = {};
	bool anaglyphEnabled = false;
	AnaglyphColorMode anaglyphColorMode = AnaglyphColorMode::RedCyan;
	bool anaglyphSwapEyes = false;
	float anaglyphEyeSeparation = 0.0f;
};
