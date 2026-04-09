#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main() {
	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 3);
	settings.setSize(1, 1);
	settings.setPosition(glm::vec2(-32000, -32000));
	settings.visible = false;
	settings.decorated = false;
	settings.resizable = false;
	settings.title = "ofxVlc4 Host";
	auto window = ofCreateWindow(settings);
	ofRunApp(window, std::make_shared<ofApp>());
	ofRunMainLoop();
}
