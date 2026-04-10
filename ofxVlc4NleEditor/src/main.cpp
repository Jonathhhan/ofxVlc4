#include "ofMain.h"
#include "ofApp.h"
#include "support/ofxVlc4CrashHandler.h"

int main() {
	ofxVlc4CrashHandler::install("ofxVlc4_crash.log");
	ofGLWindowSettings settings;
	settings.setSize(1920, 1080);
	settings.windowMode = OF_WINDOW;

	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();
}
