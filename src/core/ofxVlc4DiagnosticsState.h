#pragma once

#include "ofxVlc4.h"

#include <string>
#include <vector>

struct ofxVlc4DiagnosticsState {
	std::string lastStatusMessage;
	std::string lastErrorMessage;
	std::vector<ofxVlc4::DialogInfo> activeDialogs;
	ofxVlc4::DialogErrorInfo lastDialogError;
};
