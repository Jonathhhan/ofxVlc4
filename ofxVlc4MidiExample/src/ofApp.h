#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxMidiOut.h"
#include "midi/ofxVlc4MidiAnalysis.h"
#include "ofxVlc4.h"

#include <memory>
#include <set>
#include <string>

class ofApp : public ofBaseApp{

	public:
		void setup();
		void update();
		void draw();
		void exit();

		void keyPressed(int key);
		void windowResized(int w, int h);
		void dragEvent(ofDragInfo dragInfo);

	private:
		void drawPreview();
		void drawControlPanel();
		void installMidiDispatchCallback();
		void shutdownPlayer();
		void replacePlaylistFromDroppedFiles(const std::vector<std::filesystem::path> & paths);
		void updateAudioOnlyMode();
		bool exportMidiAnalysis(const std::string & path);
		bool loadMidiPath(const std::string & path, bool autoPlay = true);
		void clearMidiMode();
		bool isMidiModeActive() const;
		void refreshMidiOutputs();
		bool openMidiOutputPort(int index);
		void closeMidiOutput();
		void saveMidiOutputPreference() const;
		void dispatchMidiMessage(const MidiChannelMessage & message);
		void sendMidiPanic();
		bool isCurrentMediaAudioOnly() const;
		std::string currentMediaLabel();
		std::string playbackLabel();
		std::string syncModeLabel(MidiSyncMode mode) const;
		std::string syncSourceLabel(ofxVlc4MidiSyncSource source) const;

		std::unique_ptr<ofxVlc4> player;
		ofxImGui::Gui gui;
		bool shuttingDown = false;
		bool audioOnlyModeActive = false;
		ofxMidiOut midiOut;
		std::vector<std::string> midiOutputPorts;
		int midiOutputPortIndex = -1;
		std::string midiOutputStatus;
		int selectedMidiChannel = 0;
		int soloMidiChannel = -1;
		std::set<int> mutedMidiChannels;
		bool midiWasPlaying = false;
		std::string activeMidiPath;
		std::string midiExportPath;
		std::string midiAnalysisStatus;
		float previewMargin = 24.0f;
};
