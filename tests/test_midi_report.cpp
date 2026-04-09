// Tests for MidiJsonWriter, MidiTextWriter, MidiReportExporter, and the
// toJson()/toTextReport() wrappers on MidiAnalysisReport.
// These classes depend only on the C++ standard library.

#include "ofxVlc4MidiAnalysis.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
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

#define CHECK(expr)    check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Helper: build a minimal valid MidiAnalysisReport.
// ---------------------------------------------------------------------------

static MidiAnalysisReport makeMinimalReport() {
	MidiAnalysisReport report;
	report.valid = true;
	report.sourcePath = "/home/user/test.mid";
	report.fileName = "test.mid";
	report.format = 1;
	report.trackCountDeclared = 2;
	report.trackCountParsed = 2;
	report.ticksPerQuarterNote = 480;
	report.totalTicks = 960;
	report.durationSeconds = 2.0;
	report.noteCount = 3;
	report.ccCount = 1;
	report.programChangeCount = 1;
	report.pitchBendCount = 0;
	report.sysexCount = 0;
	report.markerCount = 1;
	report.tempoChangeCount = 1;

	MidiTempoChange tempo;
	tempo.tick = 0;
	tempo.seconds = 0.0;
	tempo.microsecondsPerQuarter = 500000;
	tempo.bpm = 120.0;
	report.tempoChanges.push_back(tempo);

	MidiMarker marker;
	marker.tick = 0;
	marker.seconds = 0.0;
	marker.type = "marker";
	marker.text = "Intro";
	report.markers.push_back(marker);

	MidiInstrumentUse instrument;
	instrument.channel = 0;
	instrument.program = 0;
	instrument.programName = "Acoustic Grand Piano";
	instrument.firstTick = 0;
	instrument.firstSeconds = 0.0;
	instrument.useCount = 3;
	report.instruments.push_back(instrument);

	MidiTrackSummary track0;
	track0.index = 0;
	track0.name = "Tempo Track";
	track0.eventCount = 1;
	report.tracks.push_back(track0);

	MidiTrackSummary track1;
	track1.index = 1;
	track1.name = "Piano";
	track1.eventCount = 5;
	track1.noteCount = 3;
	track1.ccCount = 1;
	track1.programChangeCount = 1;
	report.tracks.push_back(track1);

	MidiNoteRangeSummary range;
	range.channel = 0;
	range.lowestNote = 48;
	range.highestNote = 72;
	range.noteOnCount = 3;
	report.noteRanges.push_back(range);

	MidiDrumHitSummary drum;
	drum.note = 36;
	drum.name = "Bass Drum";
	drum.hitCount = 2;
	report.drumHits.push_back(drum);

	MidiEventRecord noteOn;
	noteOn.trackIndex = 1;
	noteOn.tick = 0;
	noteOn.seconds = 0.0;
	noteOn.kind = "note_on";
	noteOn.channel = 0;
	noteOn.status = 0x90;
	noteOn.data1 = 60;
	noteOn.data2 = 100;
	report.events.push_back(noteOn);

	return report;
}

// ---------------------------------------------------------------------------
// MidiJsonWriter::write — empty report
// ---------------------------------------------------------------------------

static void testJsonWriterEmptyReport() {
	beginSuite("MidiJsonWriter: empty/default report");

	MidiAnalysisReport report;
	const std::string json = MidiJsonWriter::write(report);

	CHECK(!json.empty());
	CHECK(json.front() == '{');
	CHECK(json.back() == '}');
	CHECK(json.find("\"valid\":false") != std::string::npos);
	CHECK(json.find("\"sourcePath\":\"\"") != std::string::npos);
	CHECK(json.find("\"fileName\":\"\"") != std::string::npos);
	CHECK(json.find("\"events\":[]") != std::string::npos);
	CHECK(json.find("\"tempoChanges\":[]") != std::string::npos);
	CHECK(json.find("\"markers\":[]") != std::string::npos);
	CHECK(json.find("\"instruments\":[]") != std::string::npos);
	CHECK(json.find("\"tracks\":[]") != std::string::npos);
	CHECK(json.find("\"noteRanges\":[]") != std::string::npos);
	CHECK(json.find("\"drumHits\":[]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiJsonWriter::write — populated report
// ---------------------------------------------------------------------------

static void testJsonWriterPopulatedReport() {
	beginSuite("MidiJsonWriter: populated report");

	const MidiAnalysisReport report = makeMinimalReport();
	const std::string json = MidiJsonWriter::write(report);

	CHECK(!json.empty());
	CHECK(json.front() == '{');
	CHECK(json.back() == '}');

	// Check key fields are present.
	CHECK(json.find("\"valid\":true") != std::string::npos);
	CHECK(json.find("\"sourcePath\":\"/home/user/test.mid\"") != std::string::npos);
	CHECK(json.find("\"fileName\":\"test.mid\"") != std::string::npos);
	CHECK(json.find("\"format\":1") != std::string::npos);
	CHECK(json.find("\"trackCountDeclared\":2") != std::string::npos);
	CHECK(json.find("\"trackCountParsed\":2") != std::string::npos);
	CHECK(json.find("\"ticksPerQuarterNote\":480") != std::string::npos);

	// Counts sub-object.
	CHECK(json.find("\"notes\":3") != std::string::npos);
	CHECK(json.find("\"controlChanges\":1") != std::string::npos);

	// Tempo changes array.
	CHECK(json.find("\"microsecondsPerQuarter\":500000") != std::string::npos);

	// Markers array.
	CHECK(json.find("\"Intro\"") != std::string::npos);

	// Instruments array.
	CHECK(json.find("\"Acoustic Grand Piano\"") != std::string::npos);

	// Tracks array.
	CHECK(json.find("\"Tempo Track\"") != std::string::npos);
	CHECK(json.find("\"Piano\"") != std::string::npos);

	// Note ranges array.
	CHECK(json.find("\"lowestNote\":48") != std::string::npos);
	CHECK(json.find("\"highestNote\":72") != std::string::npos);

	// Drum hits array.
	CHECK(json.find("\"Bass Drum\"") != std::string::npos);

	// Events array.
	CHECK(json.find("\"note_on\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiJsonWriter::write — special characters in strings are escaped.
// ---------------------------------------------------------------------------

static void testJsonWriterEscaping() {
	beginSuite("MidiJsonWriter: JSON string escaping");

	MidiAnalysisReport report;
	report.sourcePath = "C:\\Users\\test\\file.mid";
	report.fileName = "file\"quotes\".mid";

	const std::string json = MidiJsonWriter::write(report);

	// Backslashes should be escaped.
	CHECK(json.find("\\\\") != std::string::npos);
	// Quotes in filename should be escaped.
	CHECK(json.find("\\\"quotes\\\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiAnalysisReport::toJson delegates to MidiJsonWriter.
// ---------------------------------------------------------------------------

static void testToJsonDelegation() {
	beginSuite("MidiAnalysisReport::toJson delegation");

	const MidiAnalysisReport report = makeMinimalReport();
	const std::string via_method = report.toJson();
	const std::string via_writer = MidiJsonWriter::write(report);

	CHECK_EQ(via_method, via_writer);
}

// ---------------------------------------------------------------------------
// MidiTextWriter::write — empty/invalid report
// ---------------------------------------------------------------------------

static void testTextWriterInvalidReport() {
	beginSuite("MidiTextWriter: invalid report");

	MidiAnalysisReport report;
	report.valid = false;
	report.fileName = "bad.mid";
	report.errorMessage = "Truncated file";

	const std::string text = MidiTextWriter::write(report);

	CHECK(!text.empty());
	CHECK(text.find("MIDI Report") != std::string::npos);
	CHECK(text.find("bad.mid") != std::string::npos);
	CHECK(text.find("Valid: no") != std::string::npos);
	CHECK(text.find("Truncated file") != std::string::npos);
	// An invalid report should NOT have sections like "Tempo Changes".
	CHECK(text.find("Tempo Changes") == std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiTextWriter::write — valid report
// ---------------------------------------------------------------------------

static void testTextWriterValidReport() {
	beginSuite("MidiTextWriter: valid report");

	const MidiAnalysisReport report = makeMinimalReport();
	const std::string text = MidiTextWriter::write(report);

	CHECK(!text.empty());
	CHECK(text.find("MIDI Report") != std::string::npos);
	CHECK(text.find("test.mid") != std::string::npos);
	CHECK(text.find("Valid: yes") != std::string::npos);
	CHECK(text.find("Format: 1") != std::string::npos);
	CHECK(text.find("PPQ: 480") != std::string::npos);

	// Section headers.
	CHECK(text.find("Tempo Changes") != std::string::npos);
	CHECK(text.find("Markers") != std::string::npos);
	CHECK(text.find("Instruments") != std::string::npos);
	CHECK(text.find("Note Ranges") != std::string::npos);
	CHECK(text.find("Drum Hits") != std::string::npos);
	CHECK(text.find("Tracks") != std::string::npos);

	// Data values.
	CHECK(text.find("120.00 BPM") != std::string::npos);
	CHECK(text.find("Intro") != std::string::npos);
	CHECK(text.find("Acoustic Grand Piano") != std::string::npos);
	CHECK(text.find("Bass Drum") != std::string::npos);
	CHECK(text.find("Piano") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiAnalysisReport::toTextReport delegates to MidiTextWriter.
// ---------------------------------------------------------------------------

static void testToTextReportDelegation() {
	beginSuite("MidiAnalysisReport::toTextReport delegation");

	const MidiAnalysisReport report = makeMinimalReport();
	const std::string via_method = report.toTextReport();
	const std::string via_writer = MidiTextWriter::write(report);

	CHECK_EQ(via_method, via_writer);
}

// ---------------------------------------------------------------------------
// MidiReportExporter::exportAll — invalid report
// ---------------------------------------------------------------------------

static void testExporterInvalidReport() {
	beginSuite("MidiReportExporter: invalid report");

	MidiAnalysisReport report;
	report.valid = false;

	std::string error;
	const bool result = MidiReportExporter::exportAll(report, "/tmp/ofxvlc4_test_export", error);

	CHECK(!result);
	CHECK(!error.empty());
}

// ---------------------------------------------------------------------------
// MidiReportExporter::exportAll — valid report writes files
// ---------------------------------------------------------------------------

static void testExporterValidReport() {
	beginSuite("MidiReportExporter: valid report writes files");

	const MidiAnalysisReport report = makeMinimalReport();
	const std::string prefix = (std::filesystem::temp_directory_path() / "ofxvlc4_test_export").string();

	// Clean up any previous test output.
	std::filesystem::remove(prefix + ".txt");
	std::filesystem::remove(prefix + ".tempo.csv");
	std::filesystem::remove(prefix + ".markers.csv");

	std::string error;
	const bool result = MidiReportExporter::exportAll(report, prefix, error);

	CHECK(result);
	CHECK(error.empty());

	// Verify text report file exists and has content.
	{
		std::ifstream f(prefix + ".txt");
		CHECK(f.is_open());
		std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		CHECK(!content.empty());
		CHECK(content.find("MIDI Report") != std::string::npos);
	}

	// Verify tempo CSV file exists and has a header and one row.
	{
		std::ifstream f(prefix + ".tempo.csv");
		CHECK(f.is_open());
		std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		CHECK(content.find("tick,seconds,microsecondsPerQuarter,bpm") != std::string::npos);
		CHECK(content.find("500000") != std::string::npos);
	}

	// Verify markers CSV file exists.
	{
		std::ifstream f(prefix + ".markers.csv");
		CHECK(f.is_open());
		std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		CHECK(content.find("tick,seconds,type,text") != std::string::npos);
		CHECK(content.find("Intro") != std::string::npos);
	}

	// Clean up.
	std::filesystem::remove(prefix + ".txt");
	std::filesystem::remove(prefix + ".tempo.csv");
	std::filesystem::remove(prefix + ".markers.csv");
}

// ---------------------------------------------------------------------------
// MidiAnalysisReport struct defaults
// ---------------------------------------------------------------------------

static void testMidiAnalysisReportDefaults() {
	beginSuite("MidiAnalysisReport: default values");

	MidiAnalysisReport report;
	CHECK(!report.valid);
	CHECK(report.sourcePath.empty());
	CHECK(report.fileName.empty());
	CHECK(report.errorMessage.empty());
	CHECK_EQ(report.format, static_cast<uint16_t>(0));
	CHECK_EQ(report.trackCountDeclared, static_cast<uint16_t>(0));
	CHECK_EQ(report.trackCountParsed, static_cast<uint16_t>(0));
	CHECK(!report.usesSmpteTiming);
	CHECK_EQ(report.ticksPerQuarterNote, 0);
	CHECK_EQ(report.totalTicks, 0u);
	CHECK_EQ(report.durationSeconds, 0.0);
	CHECK_EQ(report.noteCount, 0u);
	CHECK_EQ(report.ccCount, 0u);
	CHECK(report.tempoChanges.empty());
	CHECK(report.events.empty());
	CHECK(report.tracks.empty());
}

// ---------------------------------------------------------------------------
// MIDI struct defaults: MidiTempoChange, MidiMarker, MidiInstrumentUse, etc.
// ---------------------------------------------------------------------------

static void testMidiStructDefaults() {
	beginSuite("MIDI struct defaults");

	{
		MidiTempoChange tc;
		CHECK_EQ(tc.tick, 0u);
		CHECK_EQ(tc.seconds, 0.0);
		CHECK_EQ(tc.microsecondsPerQuarter, 500000);
		CHECK_EQ(tc.bpm, 120.0);
	}

	{
		MidiMarker marker;
		CHECK_EQ(marker.tick, 0u);
		CHECK_EQ(marker.seconds, 0.0);
		CHECK(marker.type.empty());
		CHECK(marker.text.empty());
	}

	{
		MidiInstrumentUse instr;
		CHECK_EQ(instr.channel, 0);
		CHECK_EQ(instr.bankMsb, -1);
		CHECK_EQ(instr.bankLsb, -1);
		CHECK_EQ(instr.program, 0);
		CHECK(instr.programName.empty());
		CHECK_EQ(instr.firstTick, 0u);
		CHECK_EQ(instr.useCount, 0u);
	}

	{
		MidiEventRecord event;
		CHECK_EQ(event.trackIndex, 0u);
		CHECK_EQ(event.tick, 0u);
		CHECK_EQ(event.channel, -1);
		CHECK_EQ(event.status, -1);
		CHECK_EQ(event.data1, -1);
		CHECK_EQ(event.data2, -1);
		CHECK(event.kind.empty());
		CHECK(event.bytes.empty());
	}

	{
		MidiTrackSummary track;
		CHECK_EQ(track.index, 0u);
		CHECK(track.name.empty());
		CHECK_EQ(track.eventCount, 0u);
		CHECK_EQ(track.noteCount, 0u);
		CHECK_EQ(track.ccCount, 0u);
	}

	{
		MidiNoteRangeSummary range;
		CHECK_EQ(range.channel, 0);
		CHECK_EQ(range.lowestNote, 127);
		CHECK_EQ(range.highestNote, 0);
		CHECK_EQ(range.noteOnCount, 0u);
	}

	{
		MidiDrumHitSummary drum;
		CHECK_EQ(drum.note, 0);
		CHECK(drum.name.empty());
		CHECK_EQ(drum.hitCount, 0u);
	}
}

// ---------------------------------------------------------------------------
// MidiJsonWriter with multiple tempo changes / events
// ---------------------------------------------------------------------------

static void testJsonWriterMultipleCollections() {
	beginSuite("MidiJsonWriter: multiple items in collections");

	MidiAnalysisReport report;
	report.valid = true;

	MidiTempoChange tc1;
	tc1.tick = 0;
	tc1.bpm = 120.0;
	report.tempoChanges.push_back(tc1);

	MidiTempoChange tc2;
	tc2.tick = 480;
	tc2.bpm = 140.0;
	report.tempoChanges.push_back(tc2);
	report.tempoChangeCount = 2;

	MidiEventRecord e1;
	e1.kind = "note_on";
	e1.channel = 0;
	e1.status = 0x90;
	e1.data1 = 60;
	e1.data2 = 100;
	report.events.push_back(e1);

	MidiEventRecord e2;
	e2.kind = "note_off";
	e2.channel = 0;
	e2.status = 0x80;
	e2.data1 = 60;
	e2.data2 = 0;
	report.events.push_back(e2);

	const std::string json = MidiJsonWriter::write(report);

	// Count commas between tempo entries (should have one comma separator).
	size_t tempoArrayPos = json.find("\"tempoChanges\":[{");
	CHECK(tempoArrayPos != std::string::npos);

	// Both events should appear.
	CHECK(json.find("\"note_on\"") != std::string::npos);
	CHECK(json.find("\"note_off\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testJsonWriterEmptyReport();
	testJsonWriterPopulatedReport();
	testJsonWriterEscaping();
	testToJsonDelegation();
	testTextWriterInvalidReport();
	testTextWriterValidReport();
	testToTextReportDelegation();
	testExporterInvalidReport();
	testExporterValidReport();
	testMidiAnalysisReportDefaults();
	testMidiStructDefaults();
	testJsonWriterMultipleCollections();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
