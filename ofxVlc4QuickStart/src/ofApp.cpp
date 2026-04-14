#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    ofSetWindowTitle("ofxVlc4 Quick Start");
    ofSetFrameRate(60);

    // Initialize player
    player.init(0, nullptr);

    // Optional: Enable watch time for UI display
    player.setWatchTimeEnabled(true);
    player.setWatchTimeMinPeriodUs(50000);

    ofLog() << "Quick Start Example Ready";
    ofLog() << "Drop a video file onto the window to play";
    ofLog() << "Press SPACE to play/pause";
    ofLog() << "Press 'f' for fullscreen";
}

//--------------------------------------------------------------
void ofApp::update() {
    player.update();
}

//--------------------------------------------------------------
void ofApp::draw() {
    ofBackground(0);

    // Draw video
    if (player.isMediaAttached()) {
        player.draw(0, 0, ofGetWidth(), ofGetHeight());

        // Draw simple playback info overlay
        ofPushStyle();
        ofSetColor(255, 255, 255, 200);

        std::string info;
        if (player.isPlaying()) {
            info = "Playing: ";
        } else if (player.isPaused()) {
            info = "Paused: ";
        } else {
            info = "Stopped: ";
        }

        auto watchTime = player.getWatchTimeInfo();
        if (watchTime.available) {
            int currentSec = static_cast<int>(watchTime.timeUs / 1000000);
            int totalSec = static_cast<int>(watchTime.lengthUs / 1000000);
            info += ofToString(currentSec / 60) + ":" + ofToString(currentSec % 60, 2, '0');
            info += " / ";
            info += ofToString(totalSec / 60) + ":" + ofToString(totalSec % 60, 2, '0');
        }

        ofDrawBitmapStringHighlight(info, 10, 20);
        ofPopStyle();
    } else {
        // Show instructions when no media loaded
        ofPushStyle();
        ofSetColor(255);
        std::string msg = "Drop a video file here to play\n\n";
        msg += "Keyboard Controls:\n";
        msg += "SPACE - Play/Pause\n";
        msg += "f - Toggle Fullscreen\n";
        msg += "UP/DOWN - Volume";

        ofDrawBitmapString(msg, (ofGetWidth() - msg.length() * 8) / 2, ofGetHeight() / 2);
        ofPopStyle();
    }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    if (key == ' ') {
        player.togglePlayPause();
    } else if (key == 'f' || key == 'F') {
        ofToggleFullscreen();
    } else if (key == OF_KEY_UP) {
        int vol = player.getVolume();
        player.setVolume(ofClamp(vol + 10, 0, 100));
    } else if (key == OF_KEY_DOWN) {
        int vol = player.getVolume();
        player.setVolume(ofClamp(vol - 10, 0, 100));
    }
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
    if (dragInfo.files.size() > 0) {
        std::string path = dragInfo.files[0];
        ofLog() << "Loading: " << path;

        player.clearPlaylist();
        player.addPathToPlaylist(path);
        player.playIndex(0);
    }
}
