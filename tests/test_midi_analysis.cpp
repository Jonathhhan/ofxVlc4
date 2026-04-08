// Tests for MidiFileAnalyzer, MidiJsonWriter, MidiTextWriter, and related
// helpers in ofxVlc4MidiAnalysis.  The parser is exercised by building
// minimal-but-valid MIDI byte streams in memory, writing them to /tmp, and
// calling the public analyzeFile() API.

#include "ofxVlc4MidiAnalysis.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
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

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

static bool nearlyEqual(double a, double b, double eps = 1e-4) {
	return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// MIDI builder helpers
// ---------------------------------------------------------------------------

// Append big-endian 16-bit word.
static void appendU16(std::vector<uint8_t> & v, uint16_t w) {
	v.push_back(static_cast<uint8_t>(w >> 8));
	v.push_back(static_cast<uint8_t>(w & 0xFF));
}

// Append big-endian 32-bit word.
static void appendU32(std::vector<uint8_t> & v, uint32_t w) {
	v.push_back(static_cast<uint8_t>((w >> 24) & 0xFF));
	v.push_back(static_cast<uint8_t>((w >> 16) & 0xFF));
	v.push_back(static_cast<uint8_t>((w >> 8) & 0xFF));
	v.push_back(static_cast<uint8_t>(w & 0xFF));
}

// Append a MIDI variable-length quantity (up to 4 bytes).
static void appendVlq(std::vector<uint8_t> & v, uint32_t value) {
	uint8_t buf[4];
	int len = 0;
	buf[len++] = value & 0x7F;
	value >>= 7;
	while (value > 0) {
		buf[len++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
		value >>= 7;
	}
	for (int i = len - 1; i >= 0; --i) {
		v.push_back(buf[i]);
	}
}

// Build a complete MIDI file from one or more pre-built track data blobs.
// format: 0 = single track, 1 = multi-track.
static std::vector<uint8_t> buildMidiFile(uint16_t format,
	uint16_t ppq,
	const std::vector<std::vector<uint8_t>> & tracks) {
	std::vector<uint8_t> file;

	// MThd header chunk
	file.insert(file.end(), { 'M', 'T', 'h', 'd' });
	appendU32(file, 6);
	appendU16(file, format);
	appendU16(file, static_cast<uint16_t>(tracks.size()));
	appendU16(file, ppq);

	// MTrk chunks
	for (const auto & track : tracks) {
		file.insert(file.end(), { 'M', 'T', 'r', 'k' });
		appendU32(file, static_cast<uint32_t>(track.size()));
		file.insert(file.end(), track.begin(), track.end());
	}

	return file;
}

// Build a track data blob from a series of events.
// Each event is: delta (vlq), then raw bytes.
// We accept raw bytes directly; callers build them manually.
static std::vector<uint8_t> endOfTrack(uint32_t delta = 0) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.insert(v.end(), { 0xFF, 0x2F, 0x00 });
	return v;
}

static std::vector<uint8_t> tempoEvent(uint32_t delta, uint32_t microsecondsPerQuarter) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(0xFF);
	v.push_back(0x51);
	v.push_back(0x03);
	v.push_back(static_cast<uint8_t>((microsecondsPerQuarter >> 16) & 0xFF));
	v.push_back(static_cast<uint8_t>((microsecondsPerQuarter >> 8) & 0xFF));
	v.push_back(static_cast<uint8_t>(microsecondsPerQuarter & 0xFF));
	return v;
}

static std::vector<uint8_t> noteOnEvent(uint32_t delta, int channel, int note, int velocity) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(static_cast<uint8_t>(0x90 | (channel & 0x0F)));
	v.push_back(static_cast<uint8_t>(note));
	v.push_back(static_cast<uint8_t>(velocity));
	return v;
}

static std::vector<uint8_t> noteOffEvent(uint32_t delta, int channel, int note, int velocity = 0) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(static_cast<uint8_t>(0x80 | (channel & 0x0F)));
	v.push_back(static_cast<uint8_t>(note));
	v.push_back(static_cast<uint8_t>(velocity));
	return v;
}

static std::vector<uint8_t> programChangeEvent(uint32_t delta, int channel, int program) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(static_cast<uint8_t>(0xC0 | (channel & 0x0F)));
	v.push_back(static_cast<uint8_t>(program));
	return v;
}

static std::vector<uint8_t> controlChangeEvent(uint32_t delta, int channel, int cc, int value) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(static_cast<uint8_t>(0xB0 | (channel & 0x0F)));
	v.push_back(static_cast<uint8_t>(cc));
	v.push_back(static_cast<uint8_t>(value));
	return v;
}

static std::vector<uint8_t> pitchBendEvent(uint32_t delta, int channel, int lsb, int msb) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(static_cast<uint8_t>(0xE0 | (channel & 0x0F)));
	v.push_back(static_cast<uint8_t>(lsb));
	v.push_back(static_cast<uint8_t>(msb));
	return v;
}

static std::vector<uint8_t> sysexEvent(uint32_t delta, const std::vector<uint8_t> & payload) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(0xF0);
	appendVlq(v, static_cast<uint32_t>(payload.size()));
	v.insert(v.end(), payload.begin(), payload.end());
	return v;
}

static std::vector<uint8_t> trackNameEvent(uint32_t delta, const std::string & name) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(0xFF);
	v.push_back(0x03);
	appendVlq(v, static_cast<uint32_t>(name.size()));
	v.insert(v.end(), name.begin(), name.end());
	return v;
}

static std::vector<uint8_t> markerEvent(uint32_t delta, const std::string & text) {
	std::vector<uint8_t> v;
	appendVlq(v, delta);
	v.push_back(0xFF);
	v.push_back(0x06);
	appendVlq(v, static_cast<uint32_t>(text.size()));
	v.insert(v.end(), text.begin(), text.end());
	return v;
}

static void append(std::vector<uint8_t> & dst, const std::vector<uint8_t> & src) {
	dst.insert(dst.end(), src.begin(), src.end());
}

// Write bytes to a temporary file; return the path.
static std::string writeTmpFile(const std::string & name, const std::vector<uint8_t> & bytes) {
	const std::string path = "/tmp/" + name;
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(bytes.data()),
		static_cast<std::streamsize>(bytes.size()));
	return path;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void testFileNotFound() {
	beginSuite("file not found");

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile("/tmp/this_file_definitely_does_not_exist.mid");
	CHECK(!report.valid);
	CHECK(!report.errorMessage.empty());
}

static void testTooSmallFile() {
	beginSuite("file too small");

	const std::string path = writeTmpFile("too_small.mid", { 0x4D, 0x54, 0x68 }); // only 3 bytes
	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(!report.valid);
}

static void testInvalidMagic() {
	beginSuite("invalid MIDI magic");

	std::vector<uint8_t> bad(14, 0);
	const std::string path = writeTmpFile("bad_magic.mid", bad);
	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(!report.valid);
	CHECK(!report.errorMessage.empty());
}

static void testMinimalEmptyTrack() {
	beginSuite("minimal file with empty track");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("minimal.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.format, 0u);
	CHECK_EQ(report.trackCountDeclared, 1u);
	CHECK_EQ(report.trackCountParsed, 1u);
	CHECK_EQ(static_cast<int>(report.ticksPerQuarterNote), 96);
	CHECK_EQ(report.noteCount, 0u);
	CHECK_EQ(report.totalTicks, 0u);
}

static void testSingleNoteDefaultTempo() {
	beginSuite("single note, default 120 BPM tempo");

	// Default tempo = 500 000 µs/quarter = 120 BPM.
	// PPQ = 96.  Note duration = 96 ticks = 1 quarter = 0.5 s.
	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, noteOnEvent(0, 0, 60, 100));
	append(track, noteOffEvent(96, 0, 60));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("single_note.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.noteCount, 1u);
	CHECK_EQ(report.totalTicks, 96u);
	CHECK(nearlyEqual(report.durationSeconds, 0.5));
}

static void testExplicitTempoEvent() {
	beginSuite("explicit tempo event (60 BPM)");

	// 60 BPM = 1 000 000 µs/quarter.  96 ticks = 1 s at 60 BPM.
	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, tempoEvent(0, 1000000));
	append(track, noteOnEvent(0, 0, 60, 80));
	append(track, noteOffEvent(96, 0, 60));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("tempo60.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.noteCount, 1u);
	CHECK(report.tempoChangeCount >= 1u);
	CHECK(nearlyEqual(report.durationSeconds, 1.0));
}

static void testTempoChangeCount() {
	beginSuite("tempo change count");

	const uint16_t ppq = 480;
	std::vector<uint8_t> track;
	append(track, tempoEvent(0, 500000));   // 120 BPM
	append(track, tempoEvent(480, 400000)); // 150 BPM
	append(track, tempoEvent(960, 300000)); // 200 BPM
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("tempo_changes.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	// Three explicit tempo events (plus any implicit default depending on ordering).
	CHECK(report.tempoChanges.size() >= 3u);
	// BPM values should be computed from the microsecondsPerQuarter values.
	bool found120 = false;
	bool found150 = false;
	bool found200 = false;
	for (const auto & tc : report.tempoChanges) {
		if (nearlyEqual(tc.bpm, 120.0, 0.1)) found120 = true;
		if (nearlyEqual(tc.bpm, 150.0, 0.1)) found150 = true;
		if (nearlyEqual(tc.bpm, 200.0, 0.1)) found200 = true;
	}
	CHECK(found120);
	CHECK(found150);
	CHECK(found200);
}

static void testTrackName() {
	beginSuite("track name meta event");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, trackNameEvent(0, "Piano"));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("track_name.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK(!report.tracks.empty());
	CHECK_EQ(report.tracks[0].name, "Piano");
}

static void testMarkerEvent() {
	beginSuite("marker meta event");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, markerEvent(0, "Intro"));
	append(track, markerEvent(96, "Verse"));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("markers.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.markerCount, 2u);
	CHECK_EQ(report.markers.size(), 2u);
	CHECK_EQ(report.markers[0].text, "Intro");
	CHECK_EQ(report.markers[1].text, "Verse");
}

static void testNoteOnVelocityZeroIsNoteOff() {
	beginSuite("note-on vel=0 treated as note-off");

	// A note-on with velocity 0 must NOT be counted as a note.
	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, noteOnEvent(0, 0, 60, 80));  // real note-on
	append(track, noteOnEvent(96, 0, 60, 0));  // note-on vel=0 ≡ note-off
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("noteon_vel0.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.noteCount, 1u); // only the first event is a real note-on
}

static void testNoteRangeSummary() {
	beginSuite("note range summary");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	// Three notes on channel 0: C3(48), C4(60), G4(67)
	append(track, noteOnEvent(0, 0, 48, 64));
	append(track, noteOnEvent(0, 0, 60, 64));
	append(track, noteOnEvent(0, 0, 67, 64));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("note_ranges.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.noteCount, 3u);
	CHECK(!report.noteRanges.empty());

	// Find the note range for channel 0.
	const MidiNoteRangeSummary * range0 = nullptr;
	for (const auto & r : report.noteRanges) {
		if (r.channel == 0) {
			range0 = &r;
			break;
		}
	}
	CHECK(range0 != nullptr);
	if (range0) {
		CHECK_EQ(range0->lowestNote, 48);
		CHECK_EQ(range0->highestNote, 67);
		CHECK_EQ(range0->noteOnCount, 3u);
	}
}

static void testDrumChannel() {
	beginSuite("drum channel (channel 9)");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	// Kick (note 36) × 2, snare (note 38) × 1 on channel 9.
	append(track, noteOnEvent(0, 9, 36, 100));
	append(track, noteOnEvent(0, 9, 36, 100));
	append(track, noteOnEvent(0, 9, 38, 90));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("drums.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	// Notes on ch9 contribute to drumHits, not noteRanges.
	CHECK_EQ(report.noteCount, 3u);
	CHECK(!report.drumHits.empty());

	const MidiDrumHitSummary * kick = nullptr;
	const MidiDrumHitSummary * snare = nullptr;
	for (const auto & h : report.drumHits) {
		if (h.note == 36) kick = &h;
		if (h.note == 38) snare = &h;
	}
	CHECK(kick != nullptr);
	CHECK(snare != nullptr);
	if (kick) CHECK_EQ(kick->hitCount, 2u);
	if (snare) CHECK_EQ(snare->hitCount, 1u);
	// Drum hits do not appear in noteRanges.
	for (const auto & r : report.noteRanges) {
		CHECK(r.channel != 9);
	}
}

static void testProgramChange() {
	beginSuite("program change → instrument tracking");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, programChangeEvent(0, 0, 0)); // Acoustic Grand Piano, ch0
	append(track, noteOnEvent(0, 0, 60, 80));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("program_change.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.programChangeCount, 1u);
	CHECK(!report.instruments.empty());
	CHECK_EQ(report.instruments[0].program, 0);
	CHECK_EQ(report.instruments[0].channel, 0);
	CHECK_EQ(report.instruments[0].programName, std::string("Acoustic Grand Piano"));
}

static void testControlChange() {
	beginSuite("control change event count");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, controlChangeEvent(0, 0, 7, 100)); // volume
	append(track, controlChangeEvent(0, 0, 10, 64)); // pan
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("cc.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.ccCount, 2u);
}

static void testPitchBend() {
	beginSuite("pitch bend event count");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, pitchBendEvent(0, 0, 0x00, 0x40)); // centre
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("pitch_bend.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.pitchBendCount, 1u);
}

static void testSysex() {
	beginSuite("sysex event count");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, sysexEvent(0, { 0x41, 0x10, 0xF7 })); // Roland GS reset (simplified)
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("sysex.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.sysexCount, 1u);
}

static void testRunningStatus() {
	beginSuite("MIDI running status");

	// After a note-on status byte (0x90) the next event can omit the status
	// and just provide two data bytes.  Build the track data manually.
	const uint16_t ppq = 96;
	std::vector<uint8_t> track;

	// delta=0, note-on ch0 (explicit status)
	appendVlq(track, 0);
	track.push_back(0x90); // note-on, ch0
	track.push_back(60);
	track.push_back(80);

	// delta=24, note-on ch0 using running status (no status byte)
	appendVlq(track, 24);
	track.push_back(62); // D4
	track.push_back(80);

	// delta=24, another note under running status
	appendVlq(track, 24);
	track.push_back(64); // E4
	track.push_back(80);

	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("running_status.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.noteCount, 3u);
}

static void testMultipleTempoChangesTimestamp() {
	beginSuite("multiple tempo changes → correct event timestamps");

	// Track: tempo=120 BPM at tick 0, then 60 BPM at tick 480 (ppq=480).
	// An event at tick 960 falls 480 ticks into the 60 BPM section.
	// At 120 BPM, 480 ticks = 0.5 s.  At 60 BPM, 480 ticks = 1.0 s.
	// So an event at tick 960 should be at 0.5 + 1.0 = 1.5 s.
	const uint16_t ppq = 480;
	std::vector<uint8_t> track;
	append(track, tempoEvent(0, 500000));   // 120 BPM at tick 0
	append(track, tempoEvent(480, 1000000)); // 60 BPM at tick 480
	append(track, noteOnEvent(480, 0, 60, 80)); // note at tick 960
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("multi_tempo.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);

	// Find the note-on event and check its timestamp.
	const MidiEventRecord * noteEvent = nullptr;
	for (const auto & ev : report.events) {
		if (ev.kind == "note_on" && ev.data1 == 60) {
			noteEvent = &ev;
			break;
		}
	}
	CHECK(noteEvent != nullptr);
	if (noteEvent) {
		CHECK(nearlyEqual(noteEvent->seconds, 1.5, 0.001));
	}
}

static void testFormat1MultiTrack() {
	beginSuite("format 1 multi-track file");

	const uint16_t ppq = 96;

	std::vector<uint8_t> track0;
	append(track0, trackNameEvent(0, "Tempo Map"));
	append(track0, tempoEvent(0, 500000));
	append(track0, endOfTrack());

	std::vector<uint8_t> track1;
	append(track1, trackNameEvent(0, "Piano"));
	append(track1, noteOnEvent(0, 0, 60, 80));
	append(track1, noteOffEvent(96, 0, 60));
	append(track1, endOfTrack());

	const auto file = buildMidiFile(1, ppq, { track0, track1 });
	const std::string path = writeTmpFile("format1.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.format, 1u);
	CHECK_EQ(report.trackCountParsed, 2u);
	CHECK_EQ(report.noteCount, 1u);

	// Verify track names were parsed.
	bool foundTempoMap = false;
	bool foundPiano = false;
	for (const auto & t : report.tracks) {
		if (t.name == "Piano") foundPiano = true;
		if (t.name == "Tempo Map") foundTempoMap = true;
	}
	CHECK(foundPiano);
	CHECK(foundTempoMap);
}

static void testBankSelectInstrument() {
	beginSuite("bank select + program change tracks bank MSB/LSB");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, controlChangeEvent(0, 0, 0, 8));   // bank MSB = 8
	append(track, controlChangeEvent(0, 0, 32, 0));  // bank LSB = 0
	append(track, programChangeEvent(0, 0, 42));     // program 42
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("bank_select.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK(!report.instruments.empty());
	CHECK_EQ(report.instruments[0].bankMsb, 8);
	CHECK_EQ(report.instruments[0].bankLsb, 0);
	CHECK_EQ(report.instruments[0].program, 42);
}

static void testEventsSortedByTick() {
	beginSuite("events sorted by tick");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, noteOnEvent(48, 0, 60, 80)); // tick 48
	append(track, noteOnEvent(0, 0, 62, 80));  // tick 48 (second, same tick)
	append(track, noteOnEvent(0, 0, 64, 80));  // tick 48 (third)
	append(track, noteOnEvent(48, 0, 67, 80)); // tick 96
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("events_sorted.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.noteCount, 4u);

	// Events must be monotonically non-decreasing by tick.
	for (size_t i = 1; i < report.events.size(); ++i) {
		CHECK(report.events[i].tick >= report.events[i - 1].tick);
	}
}

static void testToJsonNotEmpty() {
	beginSuite("toJson produces non-empty JSON");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, noteOnEvent(0, 0, 60, 80));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("json_test.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);

	const std::string json = report.toJson();
	CHECK(!json.empty());
	CHECK(json.front() == '{');
	CHECK(json.back() == '}');
	// Spot-check some required keys.
	CHECK(json.find("\"valid\":true") != std::string::npos);
	CHECK(json.find("\"format\"") != std::string::npos);
	CHECK(json.find("\"ticksPerQuarterNote\"") != std::string::npos);
	CHECK(json.find("\"events\"") != std::string::npos);
}

static void testToTextReport() {
	beginSuite("toTextReport produces expected header");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("text_test.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);

	const std::string text = report.toTextReport();
	CHECK(!text.empty());
	CHECK(text.find("MIDI Report") != std::string::npos);
	CHECK(text.find("Valid: yes") != std::string::npos);
}

static void testInvalidReportJson() {
	beginSuite("invalid report → toJson reflects valid=false");

	MidiAnalysisReport report;
	report.valid = false;
	report.errorMessage = "test error";

	const std::string json = report.toJson();
	CHECK(json.find("\"valid\":false") != std::string::npos);
	CHECK(json.find("test error") != std::string::npos);
}

static void testInvalidReportText() {
	beginSuite("invalid report → toTextReport reflects valid=no");

	MidiAnalysisReport report;
	report.valid = false;
	report.errorMessage = "test error";

	const std::string text = report.toTextReport();
	CHECK(text.find("Valid: no") != std::string::npos);
	CHECK(text.find("test error") != std::string::npos);
}

static void testJsonSpecialCharEscaping() {
	beginSuite("JSON special-character escaping in track name");

	// Track name with characters that require JSON escaping.
	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	const std::string name = "Track \"A\"\twith\nnewline";
	append(track, trackNameEvent(0, name));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("json_escape.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);

	const std::string json = report.toJson();
	// Raw tab and newline must not appear unescaped inside JSON strings.
	CHECK(json.find("\t") == std::string::npos);
	CHECK(json.find("\n") == std::string::npos);
	// The escaped sequences must be present.
	CHECK(json.find("\\\"") != std::string::npos);
	CHECK(json.find("\\t") != std::string::npos);
	CHECK(json.find("\\n") != std::string::npos);
}

static void testFileNameParsedFromPath() {
	beginSuite("fileName extracted from path");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("my_song.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK_EQ(report.fileName, "my_song.mid");
	CHECK_EQ(report.sourcePath, path);
}

static void testTrackSummaryCountsMatchReport() {
	beginSuite("track-level counts match report-level counts");

	const uint16_t ppq = 96;
	std::vector<uint8_t> track;
	append(track, noteOnEvent(0, 0, 60, 80));
	append(track, noteOnEvent(0, 0, 62, 80));
	append(track, controlChangeEvent(0, 0, 7, 100));
	append(track, programChangeEvent(0, 0, 25));
	append(track, pitchBendEvent(0, 0, 0x00, 0x40));
	append(track, endOfTrack());

	const auto file = buildMidiFile(0, ppq, { track });
	const std::string path = writeTmpFile("summary_counts.mid", file);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	CHECK(report.valid);
	CHECK(!report.tracks.empty());

	// Report-level counts and single-track summary counts must agree.
	CHECK_EQ(report.noteCount, report.tracks[0].noteCount);
	CHECK_EQ(report.ccCount, report.tracks[0].ccCount);
	CHECK_EQ(report.programChangeCount, report.tracks[0].programChangeCount);
	CHECK_EQ(report.pitchBendCount, report.tracks[0].pitchBendCount);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testFileNotFound();
	testTooSmallFile();
	testInvalidMagic();
	testMinimalEmptyTrack();
	testSingleNoteDefaultTempo();
	testExplicitTempoEvent();
	testTempoChangeCount();
	testTrackName();
	testMarkerEvent();
	testNoteOnVelocityZeroIsNoteOff();
	testNoteRangeSummary();
	testDrumChannel();
	testProgramChange();
	testControlChange();
	testPitchBend();
	testSysex();
	testRunningStatus();
	testMultipleTempoChangesTimestamp();
	testFormat1MultiTrack();
	testBankSelectInstrument();
	testEventsSortedByTick();
	testToJsonNotEmpty();
	testToTextReport();
	testInvalidReportJson();
	testInvalidReportText();
	testJsonSpecialCharEscaping();
	testFileNameParsedFromPath();
	testTrackSummaryCountsMatchReport();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
