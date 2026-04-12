#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxVlc4.h"

#include <array>
#include <vector>

class ofVlcPlayer4GuiVisualizer {
public:
	enum class DisplayStyle {
		Studio = 0,
		Mastering,
		RtaBars,
		Hybrid,
		Waveform,
		Vectorscope
	};

	enum class DbScale {
		Broadcast = 0,
		Studio,
		Wide
	};

	void drawContent(
		ofxVlc4 & player,
		const ImVec2 & labelInnerSpacing,
		float compactControlWidth);
	void drawVlcModuleControls(
		ofxVlc4 & player,
		const ImVec2 & labelInnerSpacing,
		float compactControlWidth);

private:
	DisplayStyle displayStyle = DisplayStyle::Studio;
	DbScale dbScale = DbScale::Broadcast;
	bool vlcVisualizerStateInitialized = false;
	ofxVlc4AudioVisualizerSettings pendingVlcVisualizerSettings;
	char projectMPresetPath[1024] = "";
	std::vector<float> peakHoldLevels;
	std::vector<float> peakHoldTimers;
	double lastUpdateTime = 0.0;
	std::array<float, 2> vuMeterDisplayedLevels = { 0.0f, 0.0f };
	std::array<float, 2> vuMeterDisplayedPeaks = { 0.0f, 0.0f };
	std::array<float, 2> vuMeterPeakTimers = { 0.0f, 0.0f };
};
