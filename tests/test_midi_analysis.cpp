#include "test_runner.h"

#include "midi/ofxVlc4MidiAnalysis.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helper: build and write a minimal MIDI file, return the temporary path.
// ---------------------------------------------------------------------------

namespace {

// Append a big-endian 32-bit integer to a byte vector.
void appendBe32(std::vector<uint8_t> & out, uint32_t v) {
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	out.push_back(static_cast<uint8_t>(v & 0xFF));
}

// Append a big-endian 16-bit integer to a byte vector.
void appendBe16(std::vector<uint8_t> & out, uint16_t v) {
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	out.push_back(static_cast<uint8_t>(v & 0xFF));
}

// Append a MIDI variable-length quantity.
void appendVlq(std::vector<uint8_t> & out, uint32_t v) {
	uint8_t buf[4];
	int len = 0;
	buf[len++] = static_cast<uint8_t>(v & 0x7F);
	v >>= 7;
	while (v > 0) {
		buf[len++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
		v >>= 7;
	}
	for (int i = len - 1; i >= 0; --i) {
		out.push_back(buf[i]);
	}
}

// Build a complete MIDI header chunk (MThd).
std::vector<uint8_t> buildMThd(uint16_t format, uint16_t trackCount, uint16_t division) {
	std::vector<uint8_t> out;
	out.insert(out.end(), { 'M', 'T', 'h', 'd' });
	appendBe32(out, 6);
	appendBe16(out, format);
	appendBe16(out, trackCount);
	appendBe16(out, division);
	return out;
}

// Wrap raw track event bytes in an MTrk chunk.
std::vector<uint8_t> buildMTrk(const std::vector<uint8_t> & events) {
	std::vector<uint8_t> out;
	out.insert(out.end(), { 'M', 'T', 'r', 'k' });
	appendBe32(out, static_cast<uint32_t>(events.size()));
	out.insert(out.end(), events.begin(), events.end());
	return out;
}

// End-of-track meta event.
std::vector<uint8_t> makeEndOfTrack() {
	return { 0x00, 0xFF, 0x2F, 0x00 };
}

// Set-tempo meta event (at delta=0).
std::vector<uint8_t> makeTempo(int microsecondsPerQuarter) {
	return {
		0x00, 0xFF, 0x51, 0x03,
		static_cast<uint8_t>((microsecondsPerQuarter >> 16) & 0xFF),
		static_cast<uint8_t>((microsecondsPerQuarter >> 8) & 0xFF),
		static_cast<uint8_t>(microsecondsPerQuarter & 0xFF)
	};
}

// Track-name meta event (type 0x03).
std::vector<uint8_t> makeTrackName(const std::string & name) {
	std::vector<uint8_t> out = { 0x00, 0xFF, 0x03 };
	appendVlq(out, static_cast<uint32_t>(name.size()));
	out.insert(out.end(), name.begin(), name.end());
	return out;
}

// Marker meta event (type 0x06).
std::vector<uint8_t> makeMarker(uint32_t delta, const std::string & text) {
	std::vector<uint8_t> out;
	appendVlq(out, delta);
	out.push_back(0xFF);
	out.push_back(0x06);
	appendVlq(out, static_cast<uint32_t>(text.size()));
	out.insert(out.end(), text.begin(), text.end());
	return out;
}

// Note-on event (delta, channel, note, velocity).
std::vector<uint8_t> makeNoteOn(uint32_t delta, uint8_t channel, uint8_t note, uint8_t velocity) {
	std::vector<uint8_t> out;
	appendVlq(out, delta);
	out.push_back(static_cast<uint8_t>(0x90 | (channel & 0x0F)));
	out.push_back(note);
	out.push_back(velocity);
	return out;
}

// Note-off event.
std::vector<uint8_t> makeNoteOff(uint32_t delta, uint8_t channel, uint8_t note) {
	std::vector<uint8_t> out;
	appendVlq(out, delta);
	out.push_back(static_cast<uint8_t>(0x80 | (channel & 0x0F)));
	out.push_back(note);
	out.push_back(0x00);
	return out;
}

// Program-change event.
std::vector<uint8_t> makeProgramChange(uint32_t delta, uint8_t channel, uint8_t program) {
	std::vector<uint8_t> out;
	appendVlq(out, delta);
	out.push_back(static_cast<uint8_t>(0xC0 | (channel & 0x0F)));
	out.push_back(program);
	return out;
}

// Concatenate multiple byte vectors.
std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
	std::vector<uint8_t> out;
	for (const auto & p : parts) {
		out.insert(out.end(), p.begin(), p.end());
	}
	return out;
}

// Write bytes to a temporary file and return the path.
std::string writeTempFile(const std::vector<uint8_t> & bytes, const std::string & suffix = ".mid") {
	static std::atomic<int> counter { 0 };
	const std::string path = std::filesystem::temp_directory_path().string() +
		"/ofxvlc4_test_" + std::to_string(++counter) + suffix;
	std::ofstream f(path, std::ios::binary);
	f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return path;
}

} // namespace

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_NonexistentFile) {
	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile("/no/such/file/at/all.mid");
	EXPECT_FALSE(report.valid);
	EXPECT_STRNE(report.errorMessage, "");
}

TEST(MidiAnalysis_FileTooSmall) {
	const std::vector<uint8_t> tiny = { 0x4D, 0x54, 0x68, 0x64 }; // "MThd" but no data
	const std::string path = writeTempFile(tiny);
	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	EXPECT_FALSE(report.valid);
	EXPECT_STRNE(report.errorMessage, "");
	std::filesystem::remove(path);
}

TEST(MidiAnalysis_WrongMagicBytes) {
	std::vector<uint8_t> bad(14, 0x00);
	bad[0] = 'R'; bad[1] = 'I'; bad[2] = 'F'; bad[3] = 'F'; // wrong header
	const std::string path = writeTempFile(bad);
	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	EXPECT_FALSE(report.valid);
	std::filesystem::remove(path);
}

TEST(MidiAnalysis_InvalidHeaderLength) {
	// MThd with declared header length < 6
	auto header = buildMThd(0, 1, 96);
	header[7] = 0x04; // set length to 4 (invalid)
	const std::string path = writeTempFile(header);
	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);
	EXPECT_FALSE(report.valid);
	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Minimal valid MIDI (type 0)
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_MinimalType0_SingleNote) {
	// Build a type-0 file with a single note on channel 0 at C4 (note 60)
	auto trackEvents = concat({
		makeTempo(500000),    // 120 BPM
		makeNoteOn(0, 0, 60, 100),
		makeNoteOff(96, 0, 60),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.format, static_cast<uint16_t>(0));
	EXPECT_EQ(report.trackCountDeclared, static_cast<uint16_t>(1));
	EXPECT_EQ(report.trackCountParsed, static_cast<uint16_t>(1));
	EXPECT_EQ(report.ticksPerQuarterNote, 96);
	EXPECT_FALSE(report.usesSmpteTiming);
	EXPECT_EQ(report.noteCount, static_cast<size_t>(1));
	EXPECT_EQ(report.tempoChangeCount, static_cast<size_t>(1));
	EXPECT_NEAR(report.tempoChanges[0].bpm, 120.0, 0.001);
	EXPECT_GT(report.durationSeconds, 0.0);

	std::filesystem::remove(path);
}

TEST(MidiAnalysis_FileNameExtracted) {
	auto trackEvents = concat({ makeEndOfTrack() });
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes, ".mid");

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	// fileName should be just the basename
	EXPECT_FALSE(report.fileName.empty());
	EXPECT_FALSE(report.fileName.find('/') != std::string::npos);

	std::filesystem::remove(path);
}

TEST(MidiAnalysis_SourcePathPreserved) {
	auto trackEvents = concat({ makeEndOfTrack() });
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_STREQ(report.sourcePath, path);
	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Tempo changes
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_TempoChange_BpmCalculation) {
	// 500000 μs/quarter = 120 BPM
	auto trackEvents = concat({
		makeTempo(500000),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.tempoChangeCount, static_cast<size_t>(1));
	EXPECT_EQ(report.tempoChanges[0].tick, static_cast<uint32_t>(0));
	EXPECT_EQ(report.tempoChanges[0].microsecondsPerQuarter, 500000);
	EXPECT_NEAR(report.tempoChanges[0].bpm, 120.0, 0.001);

	std::filesystem::remove(path);
}

TEST(MidiAnalysis_TempoChange_60Bpm) {
	// 1000000 μs/quarter = 60 BPM
	auto trackEvents = concat({
		makeTempo(1000000),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_NEAR(report.tempoChanges[0].bpm, 60.0, 0.001);

	std::filesystem::remove(path);
}

TEST(MidiAnalysis_MultipleTempoChanges) {
	// Encode delta=96 (one quarter at 96ppq) as VLQ = 0x60
	auto trackEvents = concat({
		makeTempo(500000),                      // tick 0 → 120 BPM
		{ 0x60, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 }, // tick 96 → 120 BPM (same, just two events)
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.tempoChangeCount, static_cast<size_t>(2));
	EXPECT_EQ(report.tempoChanges[0].tick, static_cast<uint32_t>(0));
	EXPECT_EQ(report.tempoChanges[1].tick, static_cast<uint32_t>(96));

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Markers
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_Marker_ParsedCorrectly) {
	auto trackEvents = concat({
		makeTempo(500000),
		makeMarker(0, "Section A"),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.markerCount, static_cast<size_t>(1));
	EXPECT_EQ(report.markers.size(), static_cast<size_t>(1));
	EXPECT_STREQ(report.markers[0].text, "Section A");
	EXPECT_STREQ(report.markers[0].type, "marker");

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Track name
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_TrackName_ParsedCorrectly) {
	auto trackEvents = concat({
		makeTrackName("MyTrack"),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.tracks.size(), static_cast<size_t>(1));
	EXPECT_STREQ(report.tracks[0].name, "MyTrack");

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_NoteCount_MultipleNotes) {
	auto trackEvents = concat({
		makeTempo(500000),
		makeNoteOn(0, 0, 60, 100),
		makeNoteOn(0, 0, 62, 90),
		makeNoteOn(0, 0, 64, 80),
		makeNoteOff(96, 0, 60),
		makeNoteOff(0, 0, 62),
		makeNoteOff(0, 0, 64),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.noteCount, static_cast<size_t>(3));

	std::filesystem::remove(path);
}

TEST(MidiAnalysis_NoteRange_Tracked) {
	// Notes on channel 0: 60 and 72
	auto trackEvents = concat({
		makeNoteOn(0, 0, 60, 100),
		makeNoteOn(0, 0, 72, 90),
		makeNoteOff(96, 0, 60),
		makeNoteOff(0, 0, 72),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	// Should have at least one note range entry for channel 0
	bool foundCh0 = false;
	for (const auto & r : report.noteRanges) {
		if (r.channel == 0) {
			foundCh0 = true;
			EXPECT_LE(r.lowestNote, 60);
			EXPECT_GE(r.highestNote, 72);
		}
	}
	EXPECT_TRUE(foundCh0);

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Program changes
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_ProgramChange_Counted) {
	auto trackEvents = concat({
		makeProgramChange(0, 0, 0), // Acoustic Grand Piano
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.programChangeCount, static_cast<size_t>(1));

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Multi-track (format 1)
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_Format1_TwoTracks) {
	auto tempoTrack = concat({ makeTempo(500000), makeEndOfTrack() });
	auto noteTrack = concat({
		makeNoteOn(0, 0, 60, 100),
		makeNoteOff(96, 0, 60),
		makeEndOfTrack()
	});

	auto bytes = concat({
		buildMThd(1, 2, 96),
		buildMTrk(tempoTrack),
		buildMTrk(noteTrack)
	});
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.format, static_cast<uint16_t>(1));
	EXPECT_EQ(report.trackCountDeclared, static_cast<uint16_t>(2));
	EXPECT_EQ(report.trackCountParsed, static_cast<uint16_t>(2));
	EXPECT_EQ(report.noteCount, static_cast<size_t>(1));
	EXPECT_EQ(report.tempoChangeCount, static_cast<size_t>(1));

	std::filesystem::remove(path);
}

TEST(MidiAnalysis_TotalTicksTracked) {
	// Note-off at delta=96 from tick 0, so totalTicks should be at least 96
	auto trackEvents = concat({
		makeNoteOn(0, 0, 60, 100),
		makeNoteOff(96, 0, 60),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_GE(report.totalTicks, static_cast<uint32_t>(96));

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Duration calculation
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_Duration_CorrectAt120Bpm_96Ppq) {
	// At 120 BPM, 96ppq: one quarter note = 0.5 s
	// Note held for 96 ticks (one quarter)
	auto trackEvents = concat({
		makeTempo(500000), // 120 BPM
		makeNoteOn(0, 0, 60, 100),
		makeNoteOff(96, 0, 60),
		makeEndOfTrack()
	});
	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(trackEvents) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	// Duration should be approximately 0.5 s (one quarter at 120 BPM)
	EXPECT_NEAR(report.durationSeconds, 0.5, 0.05);

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// MidiJsonWriter
// ---------------------------------------------------------------------------

TEST(MidiJsonWriter_EmptyReport_ProducesValidJson) {
	MidiAnalysisReport report;
	report.valid = false;
	report.sourcePath = "test.mid";
	report.fileName = "test.mid";
	report.errorMessage = "some error";

	const std::string json = MidiJsonWriter::write(report);
	EXPECT_FALSE(json.empty());
	EXPECT_TRUE(json.find("\"valid\":false") != std::string::npos);
	EXPECT_TRUE(json.find("\"sourcePath\":\"test.mid\"") != std::string::npos);
	EXPECT_TRUE(json.find("\"errorMessage\":\"some error\"") != std::string::npos);
}

TEST(MidiJsonWriter_ValidReport_ContainsTempoChanges) {
	MidiAnalysisReport report;
	report.valid = true;
	report.format = 0;
	report.ticksPerQuarterNote = 96;
	report.tempoChangeCount = 1;

	MidiTempoChange tc;
	tc.tick = 0;
	tc.seconds = 0.0;
	tc.microsecondsPerQuarter = 500000;
	tc.bpm = 120.0;
	report.tempoChanges.push_back(tc);

	const std::string json = MidiJsonWriter::write(report);
	EXPECT_TRUE(json.find("\"tempoChanges\":[") != std::string::npos);
	EXPECT_TRUE(json.find("\"bpm\":120") != std::string::npos);
	EXPECT_TRUE(json.find("\"microsecondsPerQuarter\":500000") != std::string::npos);
}

TEST(MidiJsonWriter_SpecialCharsEscaped) {
	MidiAnalysisReport report;
	report.valid = false;
	report.fileName = "file\"name";
	report.errorMessage = "line1\nline2";

	const std::string json = MidiJsonWriter::write(report);
	// The quote inside fileName should be escaped as \"
	EXPECT_TRUE(json.find("file\\\"name") != std::string::npos);
	// Newline in errorMessage should be escaped as \n
	EXPECT_TRUE(json.find("line1\\nline2") != std::string::npos);
}

TEST(MidiJsonWriter_ValidReport_ContainsMarkers) {
	MidiAnalysisReport report;
	report.valid = true;
	report.markerCount = 1;

	MidiMarker m;
	m.tick = 100;
	m.seconds = 0.5;
	m.type = "marker";
	m.text = "Section A";
	report.markers.push_back(m);

	const std::string json = MidiJsonWriter::write(report);
	EXPECT_TRUE(json.find("\"markers\":[") != std::string::npos);
	EXPECT_TRUE(json.find("\"text\":\"Section A\"") != std::string::npos);
}

TEST(MidiJsonWriter_ValidReport_ContainsInstruments) {
	MidiAnalysisReport report;
	report.valid = true;

	MidiInstrumentUse instr;
	instr.channel = 0;
	instr.bankMsb = -1;
	instr.bankLsb = -1;
	instr.program = 0;
	instr.programName = "Acoustic Grand Piano";
	instr.firstTick = 0;
	instr.firstSeconds = 0.0;
	instr.useCount = 5;
	report.instruments.push_back(instr);

	const std::string json = MidiJsonWriter::write(report);
	EXPECT_TRUE(json.find("\"instruments\":[") != std::string::npos);
	EXPECT_TRUE(json.find("\"programName\":\"Acoustic Grand Piano\"") != std::string::npos);
	EXPECT_TRUE(json.find("\"useCount\":5") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiTextWriter
// ---------------------------------------------------------------------------

TEST(MidiTextWriter_InvalidReport_ShowsError) {
	MidiAnalysisReport report;
	report.valid = false;
	report.fileName = "bad.mid";
	report.errorMessage = "corrupt file";

	const std::string text = MidiTextWriter::write(report);
	EXPECT_TRUE(text.find("bad.mid") != std::string::npos);
	EXPECT_TRUE(text.find("no") != std::string::npos); // "Valid: no"
	EXPECT_TRUE(text.find("corrupt file") != std::string::npos);
}

TEST(MidiTextWriter_ValidReport_ContainsKeyFields) {
	MidiAnalysisReport report;
	report.valid = true;
	report.fileName = "song.mid";
	report.format = 1;
	report.ticksPerQuarterNote = 480;
	report.trackCountParsed = 2;
	report.trackCountDeclared = 2;
	report.noteCount = 100;
	report.durationSeconds = 30.5;

	const std::string text = MidiTextWriter::write(report);
	EXPECT_TRUE(text.find("song.mid") != std::string::npos);
	EXPECT_TRUE(text.find("yes") != std::string::npos); // "Valid: yes"
	EXPECT_TRUE(text.find("480") != std::string::npos);
	EXPECT_TRUE(text.find("notes=100") != std::string::npos);
}

TEST(MidiTextWriter_ValidReport_ShowsTempoSection) {
	MidiAnalysisReport report;
	report.valid = true;

	MidiTempoChange tc;
	tc.tick = 0;
	tc.seconds = 0.0;
	tc.bpm = 120.0;
	tc.microsecondsPerQuarter = 500000;
	report.tempoChanges.push_back(tc);
	report.tempoChangeCount = 1;

	const std::string text = MidiTextWriter::write(report);
	EXPECT_TRUE(text.find("Tempo Changes") != std::string::npos);
	EXPECT_TRUE(text.find("120") != std::string::npos);
}

TEST(MidiTextWriter_ValidReport_ShowsTrackSection) {
	MidiAnalysisReport report;
	report.valid = true;

	MidiTrackSummary track;
	track.index = 0;
	track.name = "Bass";
	track.eventCount = 10;
	track.noteCount = 8;
	report.tracks.push_back(track);

	const std::string text = MidiTextWriter::write(report);
	EXPECT_TRUE(text.find("Tracks") != std::string::npos);
	EXPECT_TRUE(text.find("Bass") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiReportExporter
// ---------------------------------------------------------------------------

TEST(MidiReportExporter_InvalidReport_FailsWithError) {
	MidiAnalysisReport report;
	report.valid = false;

	std::string errorMessage;
	const bool ok = MidiReportExporter::exportAll(report, "/tmp/test_export_bad", errorMessage);
	EXPECT_FALSE(ok);
	EXPECT_FALSE(errorMessage.empty());
}

TEST(MidiReportExporter_ValidReport_CreatesFiles) {
	MidiAnalysisReport report;
	report.valid = true;
	report.fileName = "test.mid";
	report.sourcePath = "/tmp/test.mid";
	report.format = 0;
	report.ticksPerQuarterNote = 96;
	report.noteCount = 1;

	const std::string prefix = std::filesystem::temp_directory_path().string() + "/ofxvlc4_export_test";
	std::string errorMessage;
	const bool ok = MidiReportExporter::exportAll(report, prefix, errorMessage);
	EXPECT_TRUE(ok);
	EXPECT_TRUE(std::filesystem::exists(prefix + ".txt"));
	EXPECT_TRUE(std::filesystem::exists(prefix + ".tempo.csv"));
	EXPECT_TRUE(std::filesystem::exists(prefix + ".markers.csv"));
	EXPECT_TRUE(std::filesystem::exists(prefix + ".instruments.csv"));
	EXPECT_TRUE(std::filesystem::exists(prefix + ".tracks.csv"));
	EXPECT_TRUE(std::filesystem::exists(prefix + ".note_ranges.csv"));
	EXPECT_TRUE(std::filesystem::exists(prefix + ".drums.csv"));

	// Clean up
	std::filesystem::remove(prefix + ".txt");
	std::filesystem::remove(prefix + ".tempo.csv");
	std::filesystem::remove(prefix + ".markers.csv");
	std::filesystem::remove(prefix + ".instruments.csv");
	std::filesystem::remove(prefix + ".tracks.csv");
	std::filesystem::remove(prefix + ".note_ranges.csv");
	std::filesystem::remove(prefix + ".drums.csv");
}

TEST(MidiReportExporter_CsvContent_TempoCsv) {
	MidiAnalysisReport report;
	report.valid = true;

	MidiTempoChange tc;
	tc.tick = 0;
	tc.seconds = 0.0;
	tc.microsecondsPerQuarter = 500000;
	tc.bpm = 120.0;
	report.tempoChanges.push_back(tc);

	const std::string prefix = std::filesystem::temp_directory_path().string() + "/ofxvlc4_tempo_test";
	std::string errorMessage;
	MidiReportExporter::exportAll(report, prefix, errorMessage);

	// Read tempo CSV and verify header and data
	std::ifstream f(prefix + ".tempo.csv");
	std::string line;
	std::getline(f, line);
	EXPECT_TRUE(line.find("tick") != std::string::npos &&
		line.find("seconds") != std::string::npos &&
		line.find("microsecondsPerQuarter") != std::string::npos &&
		line.find("bpm") != std::string::npos);
	std::getline(f, line);
	EXPECT_TRUE(line.find("500000") != std::string::npos);

	// Clean up
	for (const auto & ext : { ".txt", ".tempo.csv", ".markers.csv", ".instruments.csv", ".tracks.csv", ".note_ranges.csv", ".drums.csv" }) {
		std::filesystem::remove(prefix + ext);
	}
}

// ---------------------------------------------------------------------------
// MidiAnalysisReport convenience methods
// ---------------------------------------------------------------------------

TEST(MidiAnalysisReport_ToJson_DelegatesToJsonWriter) {
	MidiAnalysisReport report;
	report.valid = false;
	report.fileName = "x.mid";

	const std::string a = report.toJson();
	const std::string b = MidiJsonWriter::write(report);
	EXPECT_STREQ(a, b);
}

TEST(MidiAnalysisReport_ToTextReport_DelegatesToTextWriter) {
	MidiAnalysisReport report;
	report.valid = false;
	report.fileName = "x.mid";

	const std::string a = report.toTextReport();
	const std::string b = MidiTextWriter::write(report);
	EXPECT_STREQ(a, b);
}

// ---------------------------------------------------------------------------
// Running status (MIDI optimization where status byte is omitted)
// ---------------------------------------------------------------------------

TEST(MidiAnalysis_RunningStatus_ParsedCorrectly) {
	// Build a track using running status: first event has status byte,
	// subsequent events reuse it (no status byte prefix).
	// delta=0, status=0x90 (note on, ch0), note=60, vel=100
	// delta=0, [running 0x90], note=62, vel=90   ← no status byte
	// delta=96, note off
	std::vector<uint8_t> track;
	appendVlq(track, 0);      // delta 0
	track.push_back(0x90);    // note-on, ch0
	track.push_back(60);      // note
	track.push_back(100);     // velocity

	appendVlq(track, 0);      // delta 0, running status (no 0x90 again)
	track.push_back(62);      // note
	track.push_back(90);      // velocity

	appendVlq(track, 96);     // delta 96
	track.push_back(0x80);    // note-off ch0
	track.push_back(60);
	track.push_back(0);

	appendVlq(track, 0);
	track.push_back(0x80);
	track.push_back(62);
	track.push_back(0);

	// End of track
	const auto eot = makeEndOfTrack();
	track.insert(track.end(), eot.begin(), eot.end());

	auto bytes = concat({ buildMThd(0, 1, 96), buildMTrk(track) });
	const std::string path = writeTempFile(bytes);

	MidiFileAnalyzer analyzer;
	const auto report = analyzer.analyzeFile(path);

	EXPECT_TRUE(report.valid);
	EXPECT_EQ(report.noteCount, static_cast<size_t>(2));

	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main() {
	return TestRunner::runAll();
}
