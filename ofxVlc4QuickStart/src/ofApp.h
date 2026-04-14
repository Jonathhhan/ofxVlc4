#pragma once

#include "ofMain.h"
#include "ofxVlc4.h"

class ofApp : public ofBaseApp {
public:
    void setup();
    void update();
    void draw();
    void keyPressed(int key);
    void dragEvent(ofDragInfo dragInfo);

private:
    ofxVlc4 player;
};
