#pragma once

#include "ofMain.h"
#include "ofxLineaDeTiempo.h"
#include "ofxGui.h"
#include "ofxVlc4.h"

#include "nle/NleClip.h"
#include "nle/NleEditOps.h"
#include "nle/NleEdlExport.h"
#include "nle/NleSequence.h"
#include "nle/NleTimecode.h"
#include "nle/NleTrimOps.h"
#include "nle/NleUndoStack.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace ofx::LineaDeTiempo;

// ---------------------------------------------------------------------------
// ofApp — Avid-oriented NLE editor using ofxVlc4 + ofxLineaDeTiempo.
//
// Features:
//   - Dual monitor: Source (left) and Record/Program (right)
//   - Track-based timeline via ofxLineaDeTiempo
//   - Three-point editing (I/O marks, Splice-In, Overwrite)
//   - Segment operations (Lift, Extract)
//   - Trim modes (Ripple, Roll, Slip, Slide)
//   - J-K-L shuttle control
//   - Bin system for media management
//   - Undo/Redo
//   - Export to file + EDL
//   - Keyboard-driven (Avid key layout)
// ---------------------------------------------------------------------------

class ofApp : public ofBaseApp {
public:
	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;

	void audioOut(ofSoundBuffer & buffer);

	void keyPressed(int key) override;
	void keyReleased(int key) override;
	void dragEvent(ofDragInfo dragInfo) override;
	void windowResized(int w, int h) override;

private:
	// -- Active monitor tracking --
	enum class ActiveMonitor { Source, Record };
	ActiveMonitor activeMonitor = ActiveMonitor::Source;

	// -- Player instances --
	std::unique_ptr<ofxVlc4> sourcePlayer;   ///< Source monitor player
	std::unique_ptr<ofxVlc4> recordPlayer;   ///< Record/Program monitor player
	ofSoundStream soundStream;
	bool shuttingDown = false;

	// -- NLE data model --
	std::map<std::string, nle::MasterClip> masterClips;
	std::vector<std::string> binOrder;  ///< Display order of clips in bin
	nle::Sequence sequence;
	nle::UndoStack undoStack;
	int nextClipId = 1;

	// -- Source monitor state --
	std::string sourceClipId;   ///< Currently loaded MasterClip in source
	bool sourceInSet = false;
	bool sourceOutSet = false;
	nle::Timecode sourceInMark;
	nle::Timecode sourceOutMark;

	// -- Record monitor state --
	bool recordInSet = false;
	bool recordOutSet = false;
	nle::Timecode recordInMark;
	nle::Timecode recordOutMark;

	// -- Timeline via ofxLineaDeTiempo --
	ofxLineaDeTiempo timeline = {"NLE Timeline"};
	ofParameterGroup timelineParams;
	ofParameter<float> playheadParam;
	ofxPanel gui;

	// -- J-K-L shuttle --
	int shuttleSpeed = 0;  ///< -3..+3 (negative=reverse, 0=pause)
	bool kKeyHeld = false;

	// -- Trim mode --
	enum class TrimMode { None, Ripple, Roll, Slip, Slide };
	TrimMode currentTrimMode = TrimMode::None;

	// -- Export state --
	//
	// The timeline is rendered as a single output file.  A recording session
	// is opened once at the start of export and kept running while each
	// segment is played sequentially through the source player.  When the
	// last segment finishes, the recording session is stopped, producing
	// one continuous file that contains the entire sequence.
	enum class ExportState {
		Idle,
		StartingSession,   ///< Open recording session, load first segment
		PreparingSegment,  ///< Load source media for the current segment
		RecordingSegment,  ///< Playing/recording the current segment
		FinishingSession,  ///< Stop recording, wait for mux
		Done,
		Failed
	};
	ExportState exportState = ExportState::Idle;
	int exportSegmentIndex = -1;
	bool exportRecordingActive = false;  ///< True while the single recording session is open
	std::string exportOutputDir;
	std::string exportError;
	std::string exportLastFile;

	// -- Layout constants --
	static constexpr float kMonitorY = 0.0f;
	static constexpr float kTimelineHeightFraction = 0.40f;
	static constexpr float kBinWidthFraction = 0.20f;

	// -- Cached layout (recomputed on window resize) --
	struct LayoutCache {
		float windowW = 1920.0f;
		float windowH = 1080.0f;
		float timelineH = 0.0f;
		float transportH = 36.0f;
		float infoBarH = 22.0f;
		float monitorH = 0.0f;
		float binW = 0.0f;
		float monitorW = 0.0f;

		void recompute(float w, float h, float tlFrac, float binFrac) {
			windowW = w;
			windowH = h;
			timelineH = h * tlFrac;
			monitorH = h - timelineH - transportH - infoBarH;
			binW = w * binFrac;
			monitorW = (w - binW) * 0.5f;
		}
	};
	LayoutCache layout;

	// -- Initialization --
	void initPlayers();
	void setupTimeline();

	// -- Media management --
	std::string addMasterClip(const std::string & filePath);
	void loadSourceClip(const std::string & clipId);

	// -- Monitor drawing --
	void drawSourceMonitor(float x, float y, float w, float h);
	void drawRecordMonitor(float x, float y, float w, float h);
	void drawMonitorOverlay(float x, float y, float w, float h,
							const std::string & label,
							const std::string & timecodeStr,
							bool hasIn, bool hasOut,
							const std::string & inStr,
							const std::string & outStr);

	// -- Bin panel --
	void drawBinPanel(float x, float y, float w, float h);

	// -- Transport bar --
	void drawTransportBar(float x, float y, float w, float h);

	// -- Info bar --
	void drawInfoBar(float x, float y, float w, float h);

	// -- Three-point editing --
	void markSourceIn();
	void markSourceOut();
	void markRecordIn();
	void markRecordOut();
	void clearMarks();
	void goToIn();
	void goToOut();

	// -- Edit operations --
	void performOverwrite();
	void performSpliceIn();
	void performLift();
	void performExtract();
	void performMatchFrame();

	// -- Transport --
	void playPause();
	void stopPlayback();
	void stepFrame(int direction);
	void updateShuttle();

	// -- Record monitor playback engine --
	void seekRecordToTimeline(const nle::Timecode & tc);
	nle::Timecode currentRecordTimecode() const;
	nle::Timecode currentSourceTimecode() const;

	// -- Export --
	void exportTimeline();
	void updateExportProgress();
	void cancelExport();
	void exportEdl();

	// -- Player shutdown --
	void shutdownPlayers();

	// -- Helpers --
	ofxVlc4 * activePlayer();
	static std::string formatTimecodeMs(int ms);

	// -- Three-point edit helper (shared by overwrite/splice-in) --
	nle::ThreePointResult resolveCurrentMarks();
};
