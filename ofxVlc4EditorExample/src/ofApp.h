#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxVlc4.h"

#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Clip — one entry on the timeline.  Each clip references a source media file
// and carries its own in-point / out-point measured in milliseconds from the
// beginning of the source media.
// ---------------------------------------------------------------------------

struct Clip {
	std::string sourcePath;
	std::string label;
	int inPointMs = 0;
	int outPointMs = 0;

	int durationMs() const { return std::max(0, outPointMs - inPointMs); }
};

// ---------------------------------------------------------------------------
// ofApp — video-editor example.
// ---------------------------------------------------------------------------

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();
	void audioOut(ofSoundBuffer & buffer);

	void keyPressed(int key);
	void dragEvent(ofDragInfo dragInfo);

private:
	// -- player helpers --
	void shutdownPlayer();
	void loadSeedMedia();
	bool loadMediaPath(const std::string & path, bool autoPlay = true);

	// -- clip model --
	void addClipFromFile(const std::string & path);
	void removeClip(int index);
	void moveClipUp(int index);
	void moveClipDown(int index);
	void selectClip(int index);
	void previewClip(int index);
	int totalTimelineDurationMs() const;
	int clipStartOnTimeline(int index) const;

	// -- transport --
	void playPause();
	void stopPlayback();
	void seekToTimelineMs(int timelineMs);
	void stepFrame(int direction);

	// -- export --
	void startExport();
	void updateExportProgress();
	void cancelExport();

	// -- UI --
	void drawTimelinePanel();
	void drawClipList();
	void drawTransportBar();
	void drawPreview(float x, float y, float w, float h);
	void drawExportPanel();

	// -- formatting --
	static std::string formatTimeMs(int ms);

	// -- player --
	std::unique_ptr<ofxVlc4> player;
	ofxImGui::Gui gui;
	ofSoundStream soundStream;
	bool shuttingDown = false;

	// -- clip model --
	std::vector<Clip> clips;
	int selectedClipIndex = -1;

	// -- timeline scrubber --
	int scrubberTimelineMs = 0;

	// -- export state --
	enum class ExportState {
		Idle,
		PreparingClip,
		RecordingClip,
		WaitingForMux,
		Done,
		Failed
	};
	ExportState exportState = ExportState::Idle;
	int exportClipIndex = -1;
	std::string exportOutputDir;
	std::string exportLastFile;
	std::string exportError;
	bool exportClipStarted = false;

	// -- audio --
	int audioSampleRate = 44100;
	int audioChannelCount = 2;
};
