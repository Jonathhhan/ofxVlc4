#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxVlc4.h"

#include <string>

class ofVlcPlayer4GuiEqualizer {
public:
	void drawContent(
		ofxVlc4 & player,
		float actionButtonWidth);

private:
	int activeGuidePoint = -1;
	int editingPresetIndex = -1;
	std::string serializedPresetBuffer;
	std::string presetFilePathBuffer;
};
