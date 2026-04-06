#pragma once

#include "ofMain.h"
#include "ofxMidiOut.h"
#include "midi/ofxVlc4MidiAnalysis.h"
#include "midi/ofxVlc4MidiBridge.h"
#include "midi/ofxVlc4MidiPlayback.h"
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
		void shutdownPlayer();
		void replacePlaylistFromDroppedFiles(const std::vector<std::filesystem::path> & paths);
		void updateAudioOnlyMode();
		void updateMidiAnalysis();
		void clearMidiAnalysis();
		bool analyzeMidiPath(const std::string & path);
		bool exportMidiAnalysis(const std::string & path);
		bool loadMidiPath(const std::string & path, bool autoPlay = true);
		void clearMidiMode();
		bool isMidiModeActive() const;
		void refreshMidiOutputs();
		bool openMidiOutputPort(int index);
		void closeMidiOutput();
		void saveMidiOutputPreference() const;
		void dispatchMidiMessages(const std::vector<MidiChannelMessage> & messages, size_t beginIndex, size_t endIndex);
		void sendMidiPanic();
		bool isCurrentMediaAudioOnly() const;
		std::string currentMediaLabel();
		std::string playbackLabel();

		std::unique_ptr<ofxVlc4> player;
		bool shuttingDown = false;
		bool audioOnlyModeActive = false;
		MidiFileAnalyzer midiAnalyzer;
		MidiAnalysisReport midiReport;
		std::vector<MidiChannelMessage> midiChannelMessages;
		MidiPlaybackSession midiPlayback;
		ofxMidiOut midiOut;
		std::vector<std::string> midiOutputPorts;
		int midiOutputPortIndex = -1;
		std::string midiOutputStatus;
		int selectedMidiChannel = 0;
		int soloMidiChannel = -1;
		std::set<int> mutedMidiChannels;
		bool midiWasPlaying = false;
		std::string analyzedMidiPath;
		std::string midiExportPath;
		std::string midiAnalysisStatus;
		float previewMargin = 24.0f;
};
