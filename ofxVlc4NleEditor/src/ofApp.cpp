#include "ofApp.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
constexpr int kFrameRate = 60;
constexpr float kDefaultAspect = 16.0f / 9.0f;
constexpr int kDefaultSeqDurationMs = 300000; // 5 minutes

std::string fileNameFromPath(const std::string & path) {
	return ofFilePath::getFileName(path);
}

// Map shuttle speed (-3..+3) to VLC playback rate.
float shuttleSpeedToRate(int speed) {
	switch (speed) {
	case -3: return -4.0f;
	case -2: return -2.0f;
	case -1: return -1.0f;
	case  0: return 0.0f;
	case  1: return 1.0f;
	case  2: return 2.0f;
	case  3: return 4.0f;
	default: return 0.0f;
	}
}

// Draw a video player's texture letterboxed within a given rectangle.
void drawPlayerLetterboxed(ofxVlc4 * player, float x, float y, float w, float h) {
	if (!player) return;

	const ofxVlc4::MediaReadinessInfo readiness = player->getMediaReadinessInfo();
	const ofxVlc4::VideoStateInfo videoState = player->getVideoStateInfo();

	float sourceW = static_cast<float>(videoState.sourceWidth);
	float sourceH = static_cast<float>(videoState.sourceHeight);
	if (sourceW <= 1.0f || sourceH <= 1.0f) {
		sourceW = kDefaultAspect;
		sourceH = 1.0f;
	}

	const float sourceAspect = sourceW / sourceH;
	float drawW = w;
	float drawH = drawW / sourceAspect;
	if (drawH > h) {
		drawH = h;
		drawW = drawH * sourceAspect;
	}
	const float drawX = x + (w - drawW) * 0.5f;
	const float drawY = y + (h - drawH) * 0.5f;

	ofPushStyle();
	ofSetColor(0, 0, 0);
	ofDrawRectangle(drawX, drawY, drawW, drawH);
	if (readiness.hasReceivedVideoFrame) {
		ofSetColor(255);
		player->draw(drawX, drawY, drawW, drawH);
	} else {
		ofSetColor(80);
		ofDrawBitmapString("No video", drawX + 14.0f, drawY + 24.0f);
	}
	ofPopStyle();
}
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ofApp::setup() {
	ofSetWindowTitle("ofxVlc4 NLE Editor");
	ofSetFrameRate(kFrameRate);
	ofSetBackgroundColor(ofColor(30, 30, 34));
	ofSetColor(255);

	exportOutputDir = ofToDataPath("exports", true);
	std::error_code error;
	std::filesystem::create_directories(exportOutputDir, error);

	initPlayers();
	setupTimeline();

	// Add default tracks to sequence.
	sequence.addVideoTrack("V1");
	sequence.addAudioTrack("A1");
	sequence.addAudioTrack("A2");

	// Initial layout computation.
	layout.recompute(static_cast<float>(ofGetWidth()),
					 static_cast<float>(ofGetHeight()),
					 kTimelineHeightFraction, kBinWidthFraction);
}

void ofApp::update() {
	if (shuttingDown) return;

	if (sourcePlayer) sourcePlayer->update();
	if (recordPlayer) recordPlayer->update();

	updateShuttle();

	// Drive segment-by-segment export state machine.
	if (exportState != ExportState::Idle &&
		exportState != ExportState::Done &&
		exportState != ExportState::Failed) {
		updateExportProgress();
	}
}

void ofApp::draw() {
	ofBackground(30, 30, 34);

	const auto & L = layout;

	// Draw monitors.
	drawSourceMonitor(0.0f, kMonitorY, L.monitorW, L.monitorH);
	drawRecordMonitor(L.monitorW, kMonitorY, L.monitorW, L.monitorH);

	// Draw bin panel.
	drawBinPanel(L.windowW - L.binW, kMonitorY, L.binW, L.monitorH);

	// Draw transport bar.
	drawTransportBar(0.0f, L.monitorH, L.windowW, L.transportH);

	// Draw info bar.
	drawInfoBar(0.0f, L.monitorH + L.transportH, L.windowW, L.infoBarH);

	// Draw timeline.
	timeline.setShape(ofRectangle(0.0f, L.monitorH + L.transportH + L.infoBarH,
								  L.windowW, L.timelineH));
	timeline.draw();
}

void ofApp::windowResized(int w, int h) {
	layout.recompute(static_cast<float>(w), static_cast<float>(h),
					 kTimelineHeightFraction, kBinWidthFraction);
}

void ofApp::exit() {
	shutdownPlayers();
}

void ofApp::audioOut(ofSoundBuffer & buffer) {
	buffer.set(0.0f);
	if (shuttingDown) return;

	// Mix audio from the active monitor.
	if (activeMonitor == ActiveMonitor::Source && sourcePlayer) {
		sourcePlayer->readAudioIntoBuffer(buffer, 1.0f);
	} else if (activeMonitor == ActiveMonitor::Record && recordPlayer) {
		recordPlayer->readAudioIntoBuffer(buffer, 1.0f);
	}
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void ofApp::initPlayers() {
	sourcePlayer = std::make_unique<ofxVlc4>();
	sourcePlayer->setAudioCaptureEnabled(true);
	sourcePlayer->init(0, nullptr);
	sourcePlayer->setWatchTimeEnabled(true);
	sourcePlayer->setWatchTimeMinPeriodUs(30000);
	sourcePlayer->setVolume(70);
	sourcePlayer->setPlaybackMode(ofxVlc4::PlaybackMode::Default);

	recordPlayer = std::make_unique<ofxVlc4>();
	recordPlayer->setAudioCaptureEnabled(true);
	recordPlayer->init(0, nullptr);
	recordPlayer->setWatchTimeEnabled(true);
	recordPlayer->setWatchTimeMinPeriodUs(30000);
	recordPlayer->setVolume(70);
	recordPlayer->setPlaybackMode(ofxVlc4::PlaybackMode::Default);
}

void ofApp::setupTimeline() {
	timelineParams.setName("NLE");
	timelineParams.add(playheadParam.set("Playhead", 0.0f, 0.0f,
										 static_cast<float>(kDefaultSeqDurationMs)));
	gui.setup(timelineParams, "", 0, 0);

	timeline.add(gui);
	timeline.generateView();
	timeline.getTimeControl()->setTotalTime(kDefaultSeqDurationMs);
}

// ---------------------------------------------------------------------------
// Media management
// ---------------------------------------------------------------------------

std::string ofApp::addMasterClip(const std::string & filePath) {
	// Check if already imported.
	for (const auto & [id, mc] : masterClips) {
		if (mc.filePath == filePath) return id;
	}

	nle::MasterClip clip;
	clip.id = "MC" + ofToString(nextClipId++);
	clip.filePath = filePath;
	clip.name = fileNameFromPath(filePath);
	clip.timecodeRate = sequence.rate();
	clip.startTimecode = nle::Timecode::fromSmpte(1, 0, 0, 0, sequence.rate());
	// Duration will be populated once VLC parses the media.
	clip.durationMs = 0;

	masterClips[clip.id] = clip;
	binOrder.push_back(clip.id);

	return clip.id;
}

void ofApp::loadSourceClip(const std::string & clipId) {
	auto it = masterClips.find(clipId);
	if (it == masterClips.end() || !sourcePlayer) return;

	sourceClipId = clipId;
	sourceInSet = false;
	sourceOutSet = false;

	sourcePlayer->stop();
	sourcePlayer->clearPlaylist();
	sourcePlayer->addPathToPlaylist(it->second.filePath);
	sourcePlayer->playIndex(0);

	activeMonitor = ActiveMonitor::Source;
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

void ofApp::keyPressed(int key) {
	if (shuttingDown) return;

	switch (key) {
	// -- File operations --
	case 'o':
	case 'O': {
		ofFileDialogResult result = ofSystemLoadDialog("Import media file");
		if (result.bSuccess) {
			std::string id = addMasterClip(result.getPath());
			loadSourceClip(id);
		}
		break;
	}

	// -- Monitor selection --
	case OF_KEY_ESC:
		activeMonitor = (activeMonitor == ActiveMonitor::Source)
			? ActiveMonitor::Record : ActiveMonitor::Source;
		break;

	// -- Mark IN/OUT (Avid standard: I / O) --
	case 'i':
	case 'I':
		if (activeMonitor == ActiveMonitor::Source) {
			markSourceIn();
		} else {
			markRecordIn();
		}
		break;
	case 'o' + 256: // We use 'O' for open, so we treat lowercase only
		break;
	// Use 'p' for OUT mark to avoid conflict with 'O' for Open
	case 'p':
	case 'P':
		if (activeMonitor == ActiveMonitor::Source) {
			markSourceOut();
		} else {
			markRecordOut();
		}
		break;

	// -- Go to IN/OUT --
	case 'q':
	case 'Q':
		goToIn();
		break;
	case 'w':
	case 'W':
		goToOut();
		break;

	// -- Clear marks --
	case 'g':
	case 'G':
		clearMarks();
		break;

	// -- Edit operations (Avid-style) --
	case 'v':
	case 'V':
		performSpliceIn();  // Yellow splice-in (insert)
		break;
	case 'b':
	case 'B':
		performOverwrite();  // Red overwrite
		break;
	case 'z':
	case 'Z':
		performLift();
		break;
	case 'x':
	case 'X':
		performExtract();
		break;

	// -- Match Frame --
	case 'f':
	case 'F':
		performMatchFrame();
		break;

	// -- Transport: J-K-L shuttle --
	case 'j':
	case 'J':
		if (kKeyHeld) {
			// K+J = step back one frame
			stepFrame(-1);
		} else {
			shuttleSpeed = std::max(-3, shuttleSpeed - 1);
		}
		break;
	case 'k':
	case 'K':
		kKeyHeld = true;
		shuttleSpeed = 0;
		if (activeMonitor == ActiveMonitor::Source && sourcePlayer) {
			sourcePlayer->pause();
		} else if (recordPlayer) {
			recordPlayer->pause();
		}
		break;
	case 'l':
	case 'L':
		if (kKeyHeld) {
			// K+L = step forward one frame
			stepFrame(1);
		} else {
			shuttleSpeed = std::min(3, shuttleSpeed + 1);
		}
		break;

	// -- Space: play/pause --
	case ' ':
		playPause();
		break;

	// -- Frame step: . and , --
	case '.':
		stepFrame(1);
		break;
	case ',':
		stepFrame(-1);
		break;

	// -- Stop --
	case OF_KEY_RETURN:
		stopPlayback();
		break;

	// -- Undo/Redo --
	case 26: // Ctrl+Z (ASCII 26)
		if (undoStack.canUndo()) undoStack.undo();
		break;

	// -- Trim modes --
	case '1':
		currentTrimMode = TrimMode::None;
		break;
	case '2':
		currentTrimMode = TrimMode::Ripple;
		break;
	case '3':
		currentTrimMode = TrimMode::Roll;
		break;
	case '4':
		currentTrimMode = TrimMode::Slip;
		break;
	case '5':
		currentTrimMode = TrimMode::Slide;
		break;

	// -- Volume --
	case OF_KEY_UP: {
		ofxVlc4 * active = activePlayer();
		if (active) active->setVolume(std::min(100, active->getVolume() + 5));
		break;
	}
	case OF_KEY_DOWN: {
		ofxVlc4 * active = activePlayer();
		if (active) active->setVolume(std::max(0, active->getVolume() - 5));
		break;
	}

	// -- Export --
	case 'e':
	case 'E':
		if (exportState == ExportState::StartingSession ||
			exportState == ExportState::PreparingSegment ||
			exportState == ExportState::RecordingSegment ||
			exportState == ExportState::FinishingSession) {
			cancelExport();
		} else if (exportState == ExportState::Done ||
				   exportState == ExportState::Failed) {
			// Clear finished/failed state, returning to idle.
			exportState = ExportState::Idle;
		} else {
			exportTimeline();
		}
		break;

	// -- EDL export --
	case 'd':
	case 'D':
		exportEdl();
		break;

	// -- Timeline show/hide --
	case 't':
	case 'T':
		if (timeline.hasView()) {
			timeline.destroyView();
		} else {
			timeline.generateView();
		}
		break;

	default:
		break;
	}
}

void ofApp::keyReleased(int key) {
	if (key == 'k' || key == 'K') {
		kKeyHeld = false;
	}
}

void ofApp::dragEvent(ofDragInfo dragInfo) {
	for (const auto & file : dragInfo.files) {
		std::string id = addMasterClip(file);
		loadSourceClip(id);
	}
}

// ---------------------------------------------------------------------------
// Monitor drawing
// ---------------------------------------------------------------------------

void ofApp::drawSourceMonitor(float x, float y, float w, float h) {
	// Border to indicate active monitor.
	ofPushStyle();
	if (activeMonitor == ActiveMonitor::Source) {
		ofSetColor(80, 130, 200);
	} else {
		ofSetColor(50, 50, 55);
	}
	ofNoFill();
	ofSetLineWidth(2.0f);
	ofDrawRectangle(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f);
	ofPopStyle();

	// Draw the video.
	const float inset = 4.0f;
	drawPlayerLetterboxed(sourcePlayer.get(),
						  x + inset, y + inset + 18.0f,
						  w - 2.0f * inset, h - 2.0f * inset - 18.0f);

	// Overlay.
	std::string tcStr = sourcePlayer
		? sourcePlayer->formatCurrentPlaybackTimecode() : "--:--:--:--";
	std::string inStr = sourceInSet ? sourceInMark.toSmpteString() : "---";
	std::string outStr = sourceOutSet ? sourceOutMark.toSmpteString() : "---";

	drawMonitorOverlay(x, y, w, h, "SOURCE", tcStr,
					   sourceInSet, sourceOutSet, inStr, outStr);
}

void ofApp::drawRecordMonitor(float x, float y, float w, float h) {
	ofPushStyle();
	if (activeMonitor == ActiveMonitor::Record) {
		ofSetColor(200, 80, 80);
	} else {
		ofSetColor(50, 50, 55);
	}
	ofNoFill();
	ofSetLineWidth(2.0f);
	ofDrawRectangle(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f);
	ofPopStyle();

	const float inset = 4.0f;
	drawPlayerLetterboxed(recordPlayer.get(),
						  x + inset, y + inset + 18.0f,
						  w - 2.0f * inset, h - 2.0f * inset - 18.0f);

	nle::Timecode playhead = sequence.playhead();
	std::string tcStr = playhead.toSmpteString();
	std::string inStr = recordInSet ? recordInMark.toSmpteString() : "---";
	std::string outStr = recordOutSet ? recordOutMark.toSmpteString() : "---";

	drawMonitorOverlay(x, y, w, h, "RECORD", tcStr,
					   recordInSet, recordOutSet, inStr, outStr);
}

void ofApp::drawMonitorOverlay(float x, float y, float w, float /*h*/,
							   const std::string & label,
							   const std::string & timecodeStr,
							   bool hasIn, bool hasOut,
							   const std::string & inStr,
							   const std::string & outStr) {
	ofPushStyle();

	// Label at top-left.
	ofSetColor(200, 200, 200);
	ofDrawBitmapString(label, x + 8.0f, y + 14.0f);

	// Timecode at top-right.
	ofSetColor(0, 0, 0, 150);
	ofDrawRectangle(x + w - 172.0f, y + 2.0f, 168.0f, 16.0f);
	ofSetColor(220, 220, 0);
	ofDrawBitmapString(timecodeStr, x + w - 168.0f, y + 14.0f);

	// IN/OUT marks below label.
	ofSetColor(hasIn ? ofColor(100, 220, 100) : ofColor(80));
	ofDrawBitmapString("IN: " + inStr, x + 8.0f, y + 28.0f);

	ofSetColor(hasOut ? ofColor(220, 100, 100) : ofColor(80));
	ofDrawBitmapString("OUT: " + outStr, x + 120.0f, y + 28.0f);

	ofPopStyle();
}

// ---------------------------------------------------------------------------
// Bin panel
// ---------------------------------------------------------------------------

void ofApp::drawBinPanel(float x, float y, float w, float h) {
	ofPushStyle();
	ofSetColor(25, 25, 28);
	ofFill();
	ofDrawRectangle(x, y, w, h);
	ofSetColor(60, 60, 65);
	ofNoFill();
	ofDrawRectangle(x, y, w, h);
	ofPopStyle();

	ofPushStyle();
	ofSetColor(180, 180, 180);
	ofDrawBitmapString("BIN", x + 8.0f, y + 16.0f);
	ofSetColor(100, 100, 105);
	ofDrawLine(x, y + 20.0f, x + w, y + 20.0f);

	float entryY = y + 34.0f;
	const float lineHeight = 16.0f;

	for (const auto & clipId : binOrder) {
		if (entryY > y + h - 10.0f) break;

		auto it = masterClips.find(clipId);
		if (it == masterClips.end()) continue;

		const nle::MasterClip & mc = it->second;

		if (clipId == sourceClipId) {
			ofSetColor(60, 80, 120);
			ofFill();
			ofDrawRectangle(x + 2.0f, entryY - 12.0f, w - 4.0f, lineHeight);
			ofSetColor(220, 220, 220);
		} else {
			ofSetColor(160, 160, 165);
		}

		std::string display = mc.name;
		if (mc.durationMs > 0) {
			display += "  [" + formatTimecodeMs(mc.durationMs) + "]";
		}

		ofDrawBitmapString(display, x + 8.0f, entryY);
		entryY += lineHeight;
	}

	ofPopStyle();
}

// ---------------------------------------------------------------------------
// Transport bar
// ---------------------------------------------------------------------------

void ofApp::drawTransportBar(float x, float y, float w, float h) {
	ofPushStyle();
	ofSetColor(35, 35, 40);
	ofFill();
	ofDrawRectangle(x, y, w, h);
	ofPopStyle();

	const float btnW = 70.0f;
	const float btnH = h - 4.0f;
	float bx = x + 8.0f;
	const float by = y + 2.0f;

	ofPushStyle();

	// Transport buttons rendered as coloured rectangles with text.
	auto drawBtn = [&](const std::string & label, ofColor bg) {
		ofSetColor(bg);
		ofFill();
		ofDrawRectangle(bx, by, btnW, btnH);
		ofSetColor(220, 220, 220);
		ofDrawBitmapString(label, bx + 6.0f, by + btnH - 8.0f);
		bx += btnW + 4.0f;
	};

	// Splice-In (V)
	drawBtn("SplcIn V", ofColor(180, 160, 30));

	// Overwrite (B)
	drawBtn("Ovrwrt B", ofColor(160, 40, 40));

	// Lift (Z)
	drawBtn("Lift Z", ofColor(80, 80, 40));

	// Extract (X)
	drawBtn("Extrct X", ofColor(40, 80, 80));

	bx += 12.0f;

	// J-K-L.
	drawBtn("J <<", ofColor(60, 60, 70));
	drawBtn(shuttleSpeed == 0 ? "K ||" : "K", ofColor(60, 60, 70));
	drawBtn("L >>", ofColor(60, 60, 70));

	// Shuttle speed indicator.
	bx += 8.0f;
	ofSetColor(180, 180, 50);
	static const char * const kSpeedLabels[] = {
		"Speed: -3", "Speed: -2", "Speed: -1", "Speed: 0",
		"Speed: 1", "Speed: 2", "Speed: 3"
	};
	int speedIdx = shuttleSpeed + 3;
	if (speedIdx >= 0 && speedIdx < 7) {
		ofDrawBitmapString(kSpeedLabels[speedIdx], bx, by + btnH - 8.0f);
	}
	bx += 100.0f;

	// Trim mode.
	ofSetColor(140, 180, 140);
	static const char * const kTrimLabels[] = {
		"Trim: OFF", "Trim: RIPPLE", "Trim: ROLL", "Trim: SLIP", "Trim: SLIDE"
	};
	ofDrawBitmapString(kTrimLabels[static_cast<int>(currentTrimMode)],
					   bx, by + btnH - 8.0f);
	bx += 120.0f;

	// Undo/Redo state.
	ofSetColor(120, 120, 180);
	std::string undoLabel = undoStack.canUndo()
		? "Undo: " + undoStack.undoDescription()
		: "Undo: ---";
	ofDrawBitmapString(undoLabel, bx, by + btnH - 8.0f);

	ofPopStyle();
}

// ---------------------------------------------------------------------------
// Info bar
// ---------------------------------------------------------------------------

void ofApp::drawInfoBar(float x, float y, float w, float h) {
	ofPushStyle();
	ofSetColor(22, 22, 26);
	ofFill();
	ofDrawRectangle(x, y, w, h);

	ofSetColor(130, 130, 135);

	// Use snprintf to avoid per-frame string concatenation overhead.
	char infoBuf[256];
	const std::string durStr = sequence.duration().toSmpteString();
	const std::string rateStr = nle::frameRateLabel(sequence.rate());
	std::snprintf(infoBuf, sizeof(infoBuf),
		"Seq: %s  |  Rate: %s  |  Dur: %s  |  V:%zu  A:%zu  |  Clips: %zu",
		sequence.name().c_str(),
		rateStr.c_str(),
		durStr.c_str(),
		sequence.videoTrackCount(),
		sequence.audioTrackCount(),
		masterClips.size());

	std::string info(infoBuf);

	if (exportState == ExportState::Done) {
		info += "  |  EXPORT COMPLETE";
		if (!exportLastFile.empty()) {
			info += ": ";
			info += exportLastFile;
		}
	} else if (exportState == ExportState::Failed) {
		info += "  |  EXPORT FAILED: ";
		info += exportError;
	} else if (exportState == ExportState::StartingSession) {
		info += "  |  EXPORT STARTING...";
	} else if (exportState == ExportState::FinishingSession) {
		info += "  |  EXPORT FINISHING...";
	} else if (exportState == ExportState::PreparingSegment ||
			   exportState == ExportState::RecordingSegment) {
		const size_t totalSegs = (sequence.videoTrackCount() > 0)
			? sequence.videoTrack(0).segments().size() : 0;
		char exportBuf[64];
		std::snprintf(exportBuf, sizeof(exportBuf),
			"  |  EXPORTING seg %d/%zu...",
			exportSegmentIndex + 1,
			totalSegs);
		info += exportBuf;
	}

	ofDrawBitmapString(info, x + 8.0f, y + 14.0f);
	ofPopStyle();
}

// ---------------------------------------------------------------------------
// Three-point editing marks
// ---------------------------------------------------------------------------

void ofApp::markSourceIn() {
	if (!sourcePlayer) return;
	sourceInMark = nle::Timecode::fromMilliseconds(sourcePlayer->getTime(),
												   sequence.rate());
	sourceInSet = true;
}

void ofApp::markSourceOut() {
	if (!sourcePlayer) return;
	sourceOutMark = nle::Timecode::fromMilliseconds(sourcePlayer->getTime(),
													sequence.rate());
	sourceOutSet = true;
}

void ofApp::markRecordIn() {
	recordInMark = sequence.playhead();
	recordInSet = true;
}

void ofApp::markRecordOut() {
	recordOutMark = sequence.playhead();
	recordOutSet = true;
}

void ofApp::clearMarks() {
	sourceInSet = sourceOutSet = false;
	recordInSet = recordOutSet = false;
}

void ofApp::goToIn() {
	if (activeMonitor == ActiveMonitor::Source) {
		if (sourceInSet && sourcePlayer) {
			sourcePlayer->setTime(sourceInMark.toMilliseconds());
		}
	} else {
		if (recordInSet) {
			seekRecordToTimeline(recordInMark);
		}
	}
}

void ofApp::goToOut() {
	if (activeMonitor == ActiveMonitor::Source) {
		if (sourceOutSet && sourcePlayer) {
			sourcePlayer->setTime(sourceOutMark.toMilliseconds());
		}
	} else {
		if (recordOutSet) {
			seekRecordToTimeline(recordOutMark);
		}
	}
}

// ---------------------------------------------------------------------------
// Edit operations
// ---------------------------------------------------------------------------

void ofApp::performOverwrite() {
	if (sourceClipId.empty()) return;
	if (sequence.videoTrackCount() == 0) return;

	nle::ThreePointResult result = resolveCurrentMarks();
	if (!result.valid) return;

	nle::editOverwrite(sequence.videoTrack(0), sourceClipId,
					   result.sourceIn, result.duration, result.recordIn);
	clearMarks();
}

void ofApp::performSpliceIn() {
	if (sourceClipId.empty()) return;
	if (sequence.videoTrackCount() == 0) return;

	nle::ThreePointResult result = resolveCurrentMarks();
	if (!result.valid) return;

	nle::editSpliceIn(sequence.videoTrack(0), sourceClipId,
					  result.sourceIn, result.duration, result.recordIn);
	clearMarks();
}

void ofApp::performLift() {
	if (sequence.videoTrackCount() == 0) return;
	if (!recordInSet || !recordOutSet) return;

	nle::editLift(sequence.videoTrack(0), recordInMark, recordOutMark);
	clearMarks();
}

void ofApp::performExtract() {
	if (sequence.videoTrackCount() == 0) return;
	if (!recordInSet || !recordOutSet) return;

	nle::editExtract(sequence.videoTrack(0), recordInMark, recordOutMark);
	clearMarks();
}

void ofApp::performMatchFrame() {
	// From the record monitor, find which source clip and timecode.
	if (sequence.videoTrackCount() == 0) return;

	nle::Timecode playhead = sequence.playhead();
	int segIdx = sequence.videoTrack(0).segmentIndexAt(playhead);
	if (segIdx < 0) return;

	const nle::Segment & seg = sequence.videoTrack(0).segments()[segIdx];
	// Calculate source position.
	int64_t offsetFrames = playhead.totalFrames() - seg.timelineStart.totalFrames();
	nle::Timecode srcPos(seg.sourceIn.totalFrames() + offsetFrames,
						 sequence.rate());

	// Load source clip in source monitor at that frame.
	loadSourceClip(seg.masterClipId);
	if (sourcePlayer) {
		sourcePlayer->setTime(srcPos.toMilliseconds());
	}
	activeMonitor = ActiveMonitor::Source;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void ofApp::playPause() {
	ofxVlc4 * active = activePlayer();
	if (!active) return;

	if (active->isPlaying()) {
		active->pause();
		shuttleSpeed = 0;
	} else {
		active->play();
		shuttleSpeed = 1;
	}
}

void ofApp::stopPlayback() {
	if (sourcePlayer) sourcePlayer->stop();
	if (recordPlayer) recordPlayer->stop();
	shuttleSpeed = 0;
}

void ofApp::stepFrame(int direction) {
	ofxVlc4 * active = activePlayer();
	if (!active) return;

	if (direction > 0) {
		active->nextFrame();
	} else {
		active->jumpTime(-33);
	}
}

void ofApp::updateShuttle() {
	if (shuttleSpeed == 0) return;

	ofxVlc4 * active = activePlayer();
	if (!active) return;

	float rate = shuttleSpeedToRate(shuttleSpeed);
	if (rate < 0.0f) {
		// VLC doesn't support negative rates directly — simulate with jumpTime.
		active->pause();
		int jumpMs = static_cast<int>(rate * 33.0f); // ~1 frame at 30fps
		active->jumpTime(jumpMs);
	} else if (rate > 0.0f) {
		if (!active->isPlaying()) active->play();
		active->setPlaybackRate(rate);
	}

	// Update timeline playhead if we're in record monitor.
	if (activeMonitor == ActiveMonitor::Record && recordPlayer) {
		int ms = recordPlayer->getTime();
		sequence.setPlayhead(nle::Timecode::fromMilliseconds(ms, sequence.rate()));
		playheadParam.set(static_cast<float>(ms));
	}
}

// ---------------------------------------------------------------------------
// Record monitor playback engine
// ---------------------------------------------------------------------------

void ofApp::seekRecordToTimeline(const nle::Timecode & tc) {
	if (!recordPlayer || sequence.videoTrackCount() == 0) return;

	sequence.setPlayhead(tc);

	// Find which segment is at this timecode.
	int segIdx = sequence.videoTrack(0).segmentIndexAt(tc);
	if (segIdx < 0) return;

	const nle::Segment & seg = sequence.videoTrack(0).segments()[segIdx];
	auto it = masterClips.find(seg.masterClipId);
	if (it == masterClips.end()) return;

	// Calculate source position.
	int64_t offsetFrames = tc.totalFrames() - seg.timelineStart.totalFrames();
	nle::Timecode srcPos(seg.sourceIn.totalFrames() + offsetFrames,
						 sequence.rate());

	// Load and seek.
	recordPlayer->stop();
	recordPlayer->clearPlaylist();
	recordPlayer->addPathToPlaylist(it->second.filePath);
	recordPlayer->playIndex(0);
	recordPlayer->setTime(srcPos.toMilliseconds());
}

nle::Timecode ofApp::currentRecordTimecode() const {
	return sequence.playhead();
}

nle::Timecode ofApp::currentSourceTimecode() const {
	if (!sourcePlayer) return nle::Timecode(0, sequence.rate());
	return nle::Timecode::fromMilliseconds(sourcePlayer->getTime(), sequence.rate());
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

void ofApp::exportTimeline() {
	if (sequence.videoTrackCount() == 0 ||
		sequence.videoTrack(0).segments().empty()) {
		return;
	}

	// Don't start a new export while one is already running.
	if (exportState == ExportState::StartingSession ||
		exportState == ExportState::PreparingSegment ||
		exportState == ExportState::RecordingSegment ||
		exportState == ExportState::FinishingSession) {
		return;
	}

	exportState = ExportState::StartingSession;
	exportSegmentIndex = 0;
	exportRecordingActive = false;
	exportError.clear();
	exportLastFile.clear();

	std::error_code error;
	std::filesystem::create_directories(exportOutputDir, error);

	ofLogNotice("NLE") << "Starting unified timeline export: "
					   << sequence.videoTrack(0).segments().size()
					   << " segment(s) to " << exportOutputDir;
}

void ofApp::updateExportProgress() {
	if (!sourcePlayer) {
		exportState = ExportState::Failed;
		exportError = "Player closed during export.";
		return;
	}

	const auto & segs = sequence.videoTrack(0).segments();

	switch (exportState) {
	case ExportState::StartingSession: {
		// Open a single recording session that will capture all segments.
		const std::string baseName = sequence.name().empty()
			? "timeline-export" : sequence.name();
		const std::string basePath = ofFilePath::join(exportOutputDir, baseName);

		sourcePlayer->setRecordingPreset(ofxVlc4RecordingPreset{});
		sourcePlayer->recordAudioVideo(basePath, sourcePlayer->getTexture());
		exportRecordingActive = true;

		// Immediately fall through to prepare the first segment.
		exportState = ExportState::PreparingSegment;
		[[fallthrough]];
	}
	case ExportState::PreparingSegment: {
		if (exportSegmentIndex < 0 ||
			exportSegmentIndex >= static_cast<int>(segs.size())) {
			// All segments recorded — stop the session.
			exportState = ExportState::FinishingSession;
			sourcePlayer->stopRecordingSession();
			sourcePlayer->stop();
			exportRecordingActive = false;
			return;
		}

		const nle::Segment & seg = segs[exportSegmentIndex];
		auto it = masterClips.find(seg.masterClipId);
		if (it == masterClips.end()) {
			exportState = ExportState::Failed;
			exportError = "Missing master clip: " + seg.masterClipId;
			sourcePlayer->stopRecordingSession();
			sourcePlayer->stop();
			exportRecordingActive = false;
			return;
		}

		// Load the source file and seek to source IN — the recording session
		// stays open throughout, so the output is one continuous file.
		sourcePlayer->stop();
		sourcePlayer->clearPlaylist();
		sourcePlayer->addPathToPlaylist(it->second.filePath);
		sourcePlayer->playIndex(0);
		sourcePlayer->setTime(seg.sourceIn.toMilliseconds());

		exportState = ExportState::RecordingSegment;

		ofLogNotice("NLE") << "Exporting segment " << (exportSegmentIndex + 1)
						   << "/" << segs.size()
						   << " from " << it->second.name;
		break;
	}
	case ExportState::RecordingSegment: {
		if (exportSegmentIndex < 0 ||
			exportSegmentIndex >= static_cast<int>(segs.size())) {
			exportState = ExportState::Failed;
			exportError = "Invalid segment index during recording.";
			return;
		}

		const nle::Segment & seg = segs[exportSegmentIndex];
		const int currentTimeMs = sourcePlayer->getTime();
		const int sourceOutMs = seg.sourceOut().toMilliseconds();

		// Check if we've reached the source OUT point for this segment.
		if (currentTimeMs >= sourceOutMs) {
			// Move to the next segment (recording stays active).
			++exportSegmentIndex;
			if (exportSegmentIndex >= static_cast<int>(segs.size())) {
				// All segments done — finish the recording.
				exportState = ExportState::FinishingSession;
				sourcePlayer->stopRecordingSession();
				sourcePlayer->stop();
				exportRecordingActive = false;

				exportLastFile = ofFilePath::join(
					exportOutputDir,
					sequence.name().empty() ? "timeline-export" : sequence.name());
				ofLogNotice("NLE") << "Timeline export complete: " << exportLastFile;
			} else {
				// Prepare the next segment (keeps recording open).
				exportState = ExportState::PreparingSegment;
			}
		}
		break;
	}
	case ExportState::FinishingSession: {
		const ofxVlc4RecordingSessionState muxState =
			sourcePlayer->getRecordingSessionState();
		if (muxState == ofxVlc4RecordingSessionState::Done ||
			muxState == ofxVlc4RecordingSessionState::Idle) {
			exportState = ExportState::Done;
			ofLogNotice("NLE") << "Timeline export mux complete.";
		} else if (muxState == ofxVlc4RecordingSessionState::Failed) {
			exportState = ExportState::Failed;
			exportError = "Mux failed during export finalization.";
		}
		break;
	}
	default:
		break;
	}
}

void ofApp::cancelExport() {
	if (exportRecordingActive && sourcePlayer) {
		sourcePlayer->stopRecordingSession();
		sourcePlayer->stop();
		exportRecordingActive = false;
	}
	exportState = ExportState::Idle;
	exportSegmentIndex = -1;
	ofLogNotice("NLE") << "Export cancelled.";
}

void ofApp::exportEdl() {
	if (sequence.videoTrackCount() == 0) return;

	// Build reel name map from MasterClip names.
	std::map<std::string, std::string> reelMap;
	for (const auto & [id, mc] : masterClips) {
		// Truncate name to 8 characters for EDL reel name.
		std::string reel = mc.name;
		if (reel.size() > 8) reel = reel.substr(0, 8);
		// Replace spaces with underscores.
		for (char & c : reel) {
			if (c == ' ') c = '_';
		}
		reelMap[id] = reel;
	}

	std::string edl = nle::exportEdlCmx3600(sequence, 0, reelMap);

	// Write to file.
	std::string edlPath = ofFilePath::join(exportOutputDir,
										   sequence.name() + ".edl");
	std::ofstream file(edlPath);
	if (file.is_open()) {
		file << edl;
		file.close();
		ofLogNotice("NLE") << "EDL exported to: " << edlPath;
	}
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ofxVlc4 * ofApp::activePlayer() {
	return (activeMonitor == ActiveMonitor::Source)
		? sourcePlayer.get() : recordPlayer.get();
}

nle::ThreePointResult ofApp::resolveCurrentMarks() {
	nle::Timecode * srcIn  = sourceInSet  ? &sourceInMark  : nullptr;
	nle::Timecode * srcOut = sourceOutSet ? &sourceOutMark : nullptr;
	nle::Timecode * recIn  = recordInSet  ? &recordInMark  : nullptr;
	nle::Timecode * recOut = recordOutSet ? &recordOutMark : nullptr;
	return nle::resolveThreePointEdit(srcIn, srcOut, recIn, recOut, sequence.rate());
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void ofApp::shutdownPlayers() {
	if (shuttingDown) return;
	shuttingDown = true;

	// Cancel any active export before closing players.
	if (exportRecordingActive && sourcePlayer) {
		sourcePlayer->stopRecordingSession();
		exportRecordingActive = false;
	}
	exportState = ExportState::Idle;

	if (sourcePlayer) {
		sourcePlayer->close();
		sourcePlayer.reset();
	}
	if (recordPlayer) {
		recordPlayer->close();
		recordPlayer.reset();
	}
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

std::string ofApp::formatTimecodeMs(int ms) {
	if (ms < 0) ms = 0;
	const int totalSeconds = ms / 1000;
	const int hours = totalSeconds / 3600;
	const int minutes = (totalSeconds % 3600) / 60;
	const int seconds = totalSeconds % 60;
	const int millis = ms % 1000;

	char buffer[32];
	if (hours > 0) {
		std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d.%03d",
					  hours, minutes, seconds, millis);
	} else {
		std::snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d",
					  minutes, seconds, millis);
	}
	return std::string(buffer);
}
