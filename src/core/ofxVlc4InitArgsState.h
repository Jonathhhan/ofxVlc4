#pragma once

#include "ofxVlc4Types.h"

#include <string>
#include <vector>

struct ofxVlc4InitArgsState {
	std::vector<std::string> extraInitArgs;
	ofxVlc4AudioVisualizerSettings audioVisualizerSettings;
};
