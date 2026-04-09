// Tests for MidiPlaybackSession from ofxVlc4MidiPlayback.h/.cpp.
// The class has no dependency on OF, GLFW, or VLC.

#include "ofxVlc4MidiPlayback.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors other test files)
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

static void beginSuite(const char * name) {
	std::printf("\n[%s]\n", name);
}

static void check(bool condition, const char * expr, const char * file, int line) {
	if (condition) {
		++g_passed;
		std::printf("  PASS  %s\n", expr);
	} else {
		++g_failed;
		std::printf("  FAIL  %s  (%s:%d)\n", expr, file, line);
	}
}

static bool nearlyEqual(double a, double b, double eps = 1e-9) {
	return std::fabs(a - b) <= eps;
}

#define CHECK(expr)      check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b)   check((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b) check(nearlyEqual((a), (b)), #a " ~= " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Helpers: build a minimal but valid MidiAnalysisReport and message list
// ---------------------------------------------------------------------------

static MidiAnalysisReport makeReport(double durationSeconds, int ticksPerQN = 480) {
	MidiAnalysisReport report;
	report.valid = true;
	report.durationSeconds = durationSeconds;
	report.ticksPerQuarterNote = ticksPerQN;
	report.usesSmpteTiming = false;
	// Add a default tempo of 120 BPM at time 0.
	MidiTempoChange tempo;
	tempo.tick = 0;
	tempo.seconds = 0.0;
	tempo.microsecondsPerQuarter = 500000; // 120 BPM
	tempo.bpm = 120.0;
	report.tempoChanges.push_back(tempo);
	return report;
}

// Build a sorted list of note-on messages at 0.5-second intervals.
static std::vector<MidiChannelMessage> makeMessages(int count, double interval = 0.5) {
	std::vector<MidiChannelMessage> messages;
	for (int i = 0; i < count; ++i) {
		MidiChannelMessage m;
		m.type = MidiMessageType::ChannelVoice;
		m.kind = "note_on";
		m.channel = 0;
		m.status = 0x90;
		m.data1 = 60 + i;
		m.data2 = 100;
		m.seconds = static_cast<double>(i) * interval;
		m.tick = static_cast<uint32_t>(i * 240);
		m.bytes = {
			static_cast<unsigned char>(0x90),
			static_cast<unsigned char>(60 + i),
			static_cast<unsigned char>(100)
		};
		messages.push_back(m);
	}
	return messages;
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

static void testInitialState() {
	beginSuite("initial state");

	MidiPlaybackSession session;

	CHECK(!session.isLoaded());
	CHECK(!session.isPlaying());
	CHECK(!session.isStopped() || !session.isLoaded()); // isStopped requires not loaded
	CHECK(!session.isFinished());
	CHECK(!session.hasMessageCallback());
	CHECK(!session.hasFinishedCallback());
	CHECK_NEAR(session.getDurationSeconds(), 0.0);
	CHECK_NEAR(session.getPositionSeconds(), 0.0);
	CHECK_NEAR(session.getPositionFraction(), 0.0);
	CHECK_NEAR(session.getTempoMultiplier(), 1.0);
	CHECK(session.getPath().empty());
	CHECK(session.getMessages().empty());
}

// ---------------------------------------------------------------------------
// load / clear
// ---------------------------------------------------------------------------

static void testLoadValid() {
	beginSuite("load: valid report");

	MidiPlaybackSession session;
	const auto report = makeReport(5.0);
	const auto messages = makeMessages(4);

	const bool loaded = session.load("/tmp/test.mid", report, messages);

	CHECK(loaded);
	CHECK(session.isLoaded());
	CHECK(!session.isPlaying());
	CHECK(!session.isFinished());
	CHECK_NEAR(session.getDurationSeconds(), 5.0);
	CHECK_EQ(session.getPath(), "/tmp/test.mid");
	CHECK_EQ(session.getMessages().size(), 4u);
}

static void testLoadInvalidReport() {
	beginSuite("load: invalid report → returns false");

	MidiPlaybackSession session;
	MidiAnalysisReport report;
	report.valid = false;

	const bool loaded = session.load("/tmp/test.mid", report, {});

	CHECK(!loaded);
	CHECK(!session.isLoaded());
}

static void testClear() {
	beginSuite("clear");

	MidiPlaybackSession session;
	const auto report = makeReport(5.0);
	const auto messages = makeMessages(4);
	session.load("/tmp/test.mid", report, messages);
	CHECK(session.isLoaded());

	session.clear();

	CHECK(!session.isLoaded());
	CHECK(!session.isPlaying());
	CHECK(!session.isFinished());
	CHECK_NEAR(session.getDurationSeconds(), 0.0);
	CHECK(session.getPath().empty());
	CHECK(session.getMessages().empty());
}

// ---------------------------------------------------------------------------
// play / pause / stop state transitions
// ---------------------------------------------------------------------------

static void testPlayTransition() {
	beginSuite("play: transitions to playing");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));

	session.play(0.0);

	CHECK(session.isPlaying());
	CHECK(!session.isPaused());
	CHECK(!session.isStopped());
	CHECK(!session.isFinished());
}

static void testPlayWhenNotLoaded() {
	beginSuite("play: no-op when not loaded");

	MidiPlaybackSession session;
	session.play(0.0);

	CHECK(!session.isPlaying());
}

static void testPauseTransition() {
	beginSuite("pause: transitions from playing to paused");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));
	session.play(0.0);
	CHECK(session.isPlaying());

	session.pause(1.0);

	CHECK(!session.isPlaying());
	CHECK(session.isPaused());
	CHECK(!session.isFinished());
	// Playhead should have advanced to approximately t=1.0.
	CHECK(session.getPositionSeconds() > 0.0);
}

static void testPauseWhenNotPlaying() {
	beginSuite("pause: no-op when not playing");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));
	// Not yet playing.
	session.pause(1.0);

	// Should remain in not-playing state and playhead stays at 0.
	CHECK(!session.isPlaying());
	CHECK_NEAR(session.getPositionSeconds(), 0.0);
}

static void testStopTransition() {
	beginSuite("stop: resets playhead");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));
	session.play(0.0);
	session.pause(1.0); // advance playhead

	session.stop();

	CHECK(!session.isPlaying());
	CHECK(!session.isFinished());
	CHECK_NEAR(session.getPositionSeconds(), 0.0);
}

static void testStopWhenNotLoaded() {
	beginSuite("stop: safe when not loaded");

	MidiPlaybackSession session;
	session.stop(); // must not crash
	CHECK(!session.isPlaying());
}

// ---------------------------------------------------------------------------
// seek
// ---------------------------------------------------------------------------

static void testSeekClampsToRange() {
	beginSuite("seek: clamped to [0, duration]");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(10.0), makeMessages(10, 1.0));

	session.seek(5.0);
	CHECK_NEAR(session.getPositionSeconds(), 5.0);

	session.seek(-1.0);
	CHECK_NEAR(session.getPositionSeconds(), 0.0);

	session.seek(100.0);
	CHECK_NEAR(session.getPositionSeconds(), 10.0);
}

static void testSeekFraction() {
	beginSuite("seekFraction: maps fraction to position");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(10.0), makeMessages(4));

	session.seekFraction(0.5);
	CHECK_NEAR(session.getPositionSeconds(), 5.0);

	session.seekFraction(0.0);
	CHECK_NEAR(session.getPositionSeconds(), 0.0);

	session.seekFraction(1.0);
	CHECK_NEAR(session.getPositionSeconds(), 10.0);
}

static void testSeekWhenNotLoaded() {
	beginSuite("seek: no-op when not loaded");

	MidiPlaybackSession session;
	session.seek(5.0);
	CHECK_NEAR(session.getPositionSeconds(), 0.0);
}

static void testGetPositionFraction() {
	beginSuite("getPositionFraction");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(10.0), makeMessages(4));

	session.seek(0.0);
	CHECK_NEAR(session.getPositionFraction(), 0.0);

	session.seek(5.0);
	CHECK_NEAR(session.getPositionFraction(), 0.5);

	session.seek(10.0);
	CHECK_NEAR(session.getPositionFraction(), 1.0);
}

// ---------------------------------------------------------------------------
// update and message dispatch
// ---------------------------------------------------------------------------

static void testUpdateDispatchesMessages() {
	beginSuite("update: dispatches messages in window");

	MidiPlaybackSession session;
	// 4 messages at t=0.0, 0.5, 1.0, 1.5; duration=2.0
	session.load("/tmp/test.mid", makeReport(2.0), makeMessages(4));

	std::vector<MidiChannelMessage> received;
	session.setMessageCallback([&](const MidiChannelMessage & m) {
		received.push_back(m);
	});

	// Start playback at wall t=0.
	session.play(0.0);

	// Simulate wall clock advancing to t=1.0 (playhead at 1.0).
	// Messages at t=0.0, 0.5, 1.0 should be dispatched.
	session.update(1.0);

	// 3 note-on messages should arrive (t<=1.0).
	CHECK(received.size() >= 3u);
}

static void testUpdateNoDispatchWhenPaused() {
	beginSuite("update: no dispatch when paused");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));

	std::vector<MidiChannelMessage> received;
	session.setMessageCallback([&](const MidiChannelMessage & m) {
		received.push_back(m);
	});

	// Do not call play(); session is paused/stopped.
	session.update(2.0);

	// No note-on messages should have been dispatched (but allNotesOff or
	// transport messages are possible — we simply require no NOTE_ON events).
	size_t noteOnCount = 0;
	for (const auto & m : received) {
		if (m.kind == "note_on") {
			++noteOnCount;
		}
	}
	CHECK_EQ(noteOnCount, 0u);
}

static void testUpdateFinishedCallback() {
	beginSuite("update: fires finishedCallback at end");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(1.0), makeMessages(2, 0.5));

	bool finishedFired = false;
	session.setFinishedCallback([&]() { finishedFired = true; });
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	// Advance wall clock past duration.
	session.update(2.0);

	CHECK(session.isFinished());
	CHECK(!session.isPlaying());
	CHECK(finishedFired);
}

static void testUpdateLooping() {
	beginSuite("update: loop restarts playback");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(1.0), makeMessages(2, 0.5));
	session.setLoopEnabled(true);
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	// Advance wall clock past duration → should loop.
	session.update(2.0);

	CHECK(!session.isFinished());
	CHECK(session.isPlaying());
	// After looping, playhead should be near the start.
	CHECK(session.getPositionSeconds() < session.getDurationSeconds());
}

// ---------------------------------------------------------------------------
// setTempoMultiplier
// ---------------------------------------------------------------------------

static void testSetTempoMultiplierClamped() {
	beginSuite("setTempoMultiplier: clamped to [0.1, 4.0]");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));

	session.setTempoMultiplier(2.0, 0.0);
	CHECK_NEAR(session.getTempoMultiplier(), 2.0);

	// Clamp below minimum.
	session.setTempoMultiplier(0.0, 0.0);
	CHECK_NEAR(session.getTempoMultiplier(), 0.1);

	// Clamp above maximum.
	session.setTempoMultiplier(10.0, 0.0);
	CHECK_NEAR(session.getTempoMultiplier(), 4.0);
}

static void testSetTempoMultiplierNoopOnSameValue() {
	beginSuite("setTempoMultiplier: no-op when value unchanged");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));

	// Set to 1.0 (default) — state should not change.
	session.setTempoMultiplier(1.0, 0.0);
	CHECK_NEAR(session.getTempoMultiplier(), 1.0);
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

static void testMessageCallback() {
	beginSuite("message callback: set / clear / query");

	MidiPlaybackSession session;

	CHECK(!session.hasMessageCallback());

	session.setMessageCallback([](const MidiChannelMessage &) {});
	CHECK(session.hasMessageCallback());

	session.clearMessageCallback();
	CHECK(!session.hasMessageCallback());
}

static void testFinishedCallback() {
	beginSuite("finished callback: set / clear / query");

	MidiPlaybackSession session;

	CHECK(!session.hasFinishedCallback());

	session.setFinishedCallback([]() {});
	CHECK(session.hasFinishedCallback());

	session.clearFinishedCallback();
	CHECK(!session.hasFinishedCallback());
}

// ---------------------------------------------------------------------------
// getCurrentBpm
// ---------------------------------------------------------------------------

static void testGetCurrentBpm() {
	beginSuite("getCurrentBpm: returns tempo at playhead");

	MidiPlaybackSession session;
	// Default tempo is 120 BPM.
	const auto report = makeReport(5.0);
	session.load("/tmp/test.mid", report, makeMessages(4));

	CHECK_NEAR(session.getCurrentBpm(), 120.0);
}

static void testGetCurrentBpmWithTempoChange() {
	beginSuite("getCurrentBpm: returns updated tempo after tempo change");

	MidiAnalysisReport report = makeReport(10.0);
	// Add a second tempo event at t=3.0 → 180 BPM.
	MidiTempoChange faster;
	faster.tick = 1440;
	faster.seconds = 3.0;
	faster.microsecondsPerQuarter = 333333;
	faster.bpm = 180.0;
	report.tempoChanges.push_back(faster);

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", report, makeMessages(4));

	// Before the tempo change.
	session.seek(1.0);
	CHECK_NEAR(session.getCurrentBpm(), 120.0);

	// After the tempo change.
	session.seek(4.0);
	CHECK_NEAR(session.getCurrentBpm(), 180.0);
}

// ---------------------------------------------------------------------------
// SyncSettings
// ---------------------------------------------------------------------------

static void testSyncSettings() {
	beginSuite("setSyncSettings / getSyncSettings");

	MidiPlaybackSession session;

	MidiSyncSettings settings;
	settings.mode = MidiSyncMode::MidiClock;
	settings.timecodeFps = 25.0;
	settings.sendTransportMessages = false;
	settings.sendSongPositionOnSeek = false;

	session.setSyncSettings(settings);

	const auto retrieved = session.getSyncSettings();
	CHECK_EQ(retrieved.mode, MidiSyncMode::MidiClock);
	CHECK_NEAR(retrieved.timecodeFps, 25.0);
	CHECK_EQ(retrieved.sendTransportMessages, false);
	CHECK_EQ(retrieved.sendSongPositionOnSeek, false);
}

// ---------------------------------------------------------------------------
// Loop flag
// ---------------------------------------------------------------------------

static void testLoopEnabled() {
	beginSuite("setLoopEnabled / isLoopEnabled");

	MidiPlaybackSession session;

	CHECK(!session.isLoopEnabled());

	session.setLoopEnabled(true);
	CHECK(session.isLoopEnabled());

	session.setLoopEnabled(false);
	CHECK(!session.isLoopEnabled());
}

// ---------------------------------------------------------------------------
// Resume play after finish
// ---------------------------------------------------------------------------

static void testPlayAfterFinish() {
	beginSuite("play: resumes from start after finish");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(1.0), makeMessages(2, 0.5));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	session.update(2.0); // finish

	CHECK(session.isFinished());

	// play() should restart from the beginning.
	session.play(3.0);
	CHECK(session.isPlaying());
	CHECK(!session.isFinished());
	CHECK(session.getPositionSeconds() < 0.001);
}

// ---------------------------------------------------------------------------
// Dispatch index tracking
// ---------------------------------------------------------------------------

static void testDispatchIndexTracking() {
	beginSuite("dispatch index tracking");

	MidiPlaybackSession session;
	// 4 messages at 0.0, 0.5, 1.0, 1.5; duration 2.0
	session.load("/tmp/test.mid", makeReport(2.0), makeMessages(4));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);

	// Initial update at wall t=0.6 → messages at 0.0 and 0.5 should dispatch.
	session.update(0.6);
	CHECK(session.getLastDispatchEnd() > session.getLastDispatchBegin() ||
		  session.getLastDispatchEnd() == session.getLastDispatchBegin());
	CHECK(session.getDispatchedCount() >= 2u);

	// After full playback
	session.update(2.5);
	CHECK_EQ(session.getDispatchedCount(), 4u);
}

// ---------------------------------------------------------------------------
// Seek while playing
// ---------------------------------------------------------------------------

static void testSeekWhilePlaying() {
	beginSuite("seek: while playing adjusts playhead");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(10.0), makeMessages(10, 1.0));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	session.update(2.0); // play for 2 seconds

	// Seek to a different position while still playing.
	session.seek(7.0, 2.0);
	CHECK(session.isPlaying());
	CHECK_NEAR(session.getPositionSeconds(), 7.0);

	// After updating, playback should continue from the new position.
	session.update(3.0); // 1 more wall second at 1x → position 8.0
	CHECK(session.getPositionSeconds() > 7.0);
}

// ---------------------------------------------------------------------------
// SeekFraction while playing
// ---------------------------------------------------------------------------

static void testSeekFractionWhilePlaying() {
	beginSuite("seekFraction: while playing");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(10.0), makeMessages(4));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	session.update(1.0);

	session.seekFraction(0.8, 1.0);
	CHECK(session.isPlaying());
	CHECK_NEAR(session.getPositionSeconds(), 8.0);
}

// ---------------------------------------------------------------------------
// Double play (play when already playing)
// ---------------------------------------------------------------------------

static void testDoublePlaying() {
	beginSuite("play: called twice is safe");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	CHECK(session.isPlaying());

	// Second play call should be safe (no-op or restart).
	session.play(1.0);
	CHECK(session.isPlaying());
}

// ---------------------------------------------------------------------------
// Pause then stop sequence
// ---------------------------------------------------------------------------

static void testPauseStopSequence() {
	beginSuite("pause → stop: proper cleanup");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	session.update(1.0);

	session.pause(1.0);
	CHECK(session.isPaused());
	CHECK(!session.isPlaying());

	session.stop();
	CHECK(!session.isPlaying());
	// After stop(), isPaused() is true because the session is loaded, not
	// playing, and not finished.  isStopped() is the correct way to detect
	// the stopped state (loaded && !playing && playhead at 0).
	CHECK(session.isStopped());
	CHECK_NEAR(session.getPositionSeconds(), 0.0);
}

// ---------------------------------------------------------------------------
// Multiple seek calls
// ---------------------------------------------------------------------------

static void testMultipleSeeks() {
	beginSuite("seek: multiple consecutive seeks");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(10.0), makeMessages(10, 1.0));

	session.seek(3.0);
	CHECK_NEAR(session.getPositionSeconds(), 3.0);

	session.seek(7.0);
	CHECK_NEAR(session.getPositionSeconds(), 7.0);

	session.seek(0.0);
	CHECK_NEAR(session.getPositionSeconds(), 0.0);

	session.seek(10.0);
	CHECK_NEAR(session.getPositionSeconds(), 10.0);
}

// ---------------------------------------------------------------------------
// Clear while playing
// ---------------------------------------------------------------------------

static void testClearWhilePlaying() {
	beginSuite("clear: while playing stops and resets");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	session.update(1.0);
	CHECK(session.isPlaying());

	session.clear();
	CHECK(!session.isPlaying());
	CHECK(!session.isLoaded());
	CHECK_NEAR(session.getPositionSeconds(), 0.0);
	CHECK_NEAR(session.getDurationSeconds(), 0.0);
}

// ---------------------------------------------------------------------------
// Reload after clear
// ---------------------------------------------------------------------------

static void testReloadAfterClear() {
	beginSuite("load: after clear works correctly");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(4));
	session.play(0.0);
	session.update(1.0);
	session.clear();

	// Reload with different data.
	const auto report2 = makeReport(8.0);
	const auto messages2 = makeMessages(6, 1.0);
	const bool loaded = session.load("/tmp/test2.mid", report2, messages2);

	CHECK(loaded);
	CHECK(session.isLoaded());
	CHECK_EQ(session.getMessages().size(), 6u);
	CHECK_NEAR(session.getDurationSeconds(), 8.0);
	CHECK_EQ(session.getPath(), "/tmp/test2.mid");
}

// ---------------------------------------------------------------------------
// Tempo multiplier during playback
// ---------------------------------------------------------------------------

static void testTempoMultiplierDuringPlayback() {
	beginSuite("setTempoMultiplier: during playback");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(10.0), makeMessages(10, 1.0));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	session.update(1.0); // play 1 second at 1x

	session.setTempoMultiplier(2.0, 1.0);
	CHECK_NEAR(session.getTempoMultiplier(), 2.0);

	// After 1 more wall second at 2x, playhead should advance by ~2 seconds.
	session.update(2.0);
	CHECK(session.getPositionSeconds() > 2.5); // Should be around 3.0
}

// ---------------------------------------------------------------------------
// Loop with seek
// ---------------------------------------------------------------------------

static void testLoopAfterSeekToEnd() {
	beginSuite("loop: seek to end then update triggers loop");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(2.0), makeMessages(4, 0.5));
	session.setLoopEnabled(true);
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.seek(1.9);
	session.play(0.0);
	session.update(0.5); // advance past end

	CHECK(!session.isFinished());
	CHECK(session.isPlaying());
}

// ---------------------------------------------------------------------------
// Dispatch index tracking after seek
// ---------------------------------------------------------------------------

static void testDispatchTrackingAfterSeek() {
	beginSuite("dispatch tracking: after seek");

	MidiPlaybackSession session;
	session.load("/tmp/test.mid", makeReport(5.0), makeMessages(10, 0.5));
	session.setMessageCallback([](const MidiChannelMessage &) {});

	session.play(0.0);
	session.update(1.0); // dispatch messages up to t=1.0
	const size_t countBefore = session.getDispatchedCount();
	CHECK(countBefore > 0u);

	// Seek back to start → dispatch count should reset or be tracked.
	session.seek(0.0, 1.0);

	// Continue playing from new position.
	session.update(1.5);
	// Should dispatch more messages from the new position.
	CHECK(session.getDispatchedCount() > 0u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testInitialState();
	testLoadValid();
	testLoadInvalidReport();
	testClear();
	testPlayTransition();
	testPlayWhenNotLoaded();
	testPauseTransition();
	testPauseWhenNotPlaying();
	testStopTransition();
	testStopWhenNotLoaded();
	testSeekClampsToRange();
	testSeekFraction();
	testSeekWhenNotLoaded();
	testGetPositionFraction();
	testUpdateDispatchesMessages();
	testUpdateNoDispatchWhenPaused();
	testUpdateFinishedCallback();
	testUpdateLooping();
	testSetTempoMultiplierClamped();
	testSetTempoMultiplierNoopOnSameValue();
	testMessageCallback();
	testFinishedCallback();
	testGetCurrentBpm();
	testGetCurrentBpmWithTempoChange();
	testSyncSettings();
	testLoopEnabled();
	testPlayAfterFinish();
	testDispatchIndexTracking();
	testSeekWhilePlaying();
	testSeekFractionWhilePlaying();
	testDoublePlaying();
	testPauseStopSequence();
	testMultipleSeeks();
	testClearWhilePlaying();
	testReloadAfterClear();
	testTempoMultiplierDuringPlayback();
	testLoopAfterSeekToEnd();
	testDispatchTrackingAfterSeek();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
