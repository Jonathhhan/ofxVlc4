#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxVlc4.h"

#include <atomic>
#include <memory>

class RecorderApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();
	void audioOut(ofSoundBuffer & buffer);

	void keyPressed(int key);
	void dragEvent(ofDragInfo dragInfo);
	void windowResized(int w, int h);

private:
	void setupBenchmarkAudio();
	void closeBenchmarkAudio();
	void shutdownPlayer();
	void stopActiveRecorders();
	void loadSeedMedia();
	bool allocateBenchmarkTexture();
	void updateBenchmarkTexture();
	void drawControlPanel();
	bool loadMediaPath(const std::string & path, bool autoPlay = true);
	void replacePlaylistFromDroppedFiles(const std::vector<std::filesystem::path> & paths);
	void updateAudioOnlyMode();
	bool isCurrentMediaAudioOnly() const;
	void toggleBenchmarkMode();
	void toggleBenchmarkAudioSource();
	void cycleVideoRecordingCodec();
	void cycleBenchmarkMuxProfile();
	void toggleBenchmarkMuxSourceCleanup();
	void toggleBenchmarkVideoRecording();
	void toggleWindowRecording();
	bool isBenchmarkRecordingActive() const;
	bool isWindowCaptureRecordingActive() const;
	void requestBenchmarkRecordingStop();
	void requestWindowRecordingStop();
	void cycleReadbackPolicy();
	void adjustReadbackBufferCount(int delta);
	void toggleNativeRecording();
	void toggleAudioRecording();
	void takeSnapshot();
	std::string currentMediaLabel() const;
	std::string playbackLabel() const;
	std::string overlayStatusLine() const;
	std::string recordingSessionStateLabel() const;
	bool isRecordingBusy() const;

	std::unique_ptr<ofxVlc4> player;
	ofxImGui::Gui gui;
	bool shuttingDown = false;
	bool audioOnlyModeActive = false;
	bool benchmarkModeActive = false;
	bool benchmarkVideoRecordingActive = false;
	bool windowRecordingActive = false;
	std::string nativeRecordingDirectory;
	std::string audioRecordingBasePath;
	std::string benchmarkRecordingBasePath;
	std::string windowRecordingBasePath;
	std::string snapshotDirectory;
	ofFbo benchmarkFbo;
	float benchmarkPhase = 0.0f;
	int benchmarkWidth = 1920;
	int benchmarkHeight = 1080;
	float previewMargin = 24.0f;
	ofSoundStream benchmarkAudioStream;
	int benchmarkAudioSampleRate = 44100;
	int benchmarkAudioChannelCount = 2;
	double benchmarkAudioPhase = 0.0;
	double benchmarkAudioModPhase = 0.0;
};
