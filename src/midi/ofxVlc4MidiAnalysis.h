#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct MidiTempoChange {
	uint32_t tick = 0;
	double seconds = 0.0;
	int microsecondsPerQuarter = 500000;
	double bpm = 120.0;
};

struct MidiMarker {
	uint32_t tick = 0;
	double seconds = 0.0;
	std::string type;
	std::string text;
};

struct MidiInstrumentUse {
	int channel = 0;
	int bankMsb = -1;
	int bankLsb = -1;
	int program = 0;
	std::string programName;
	uint32_t firstTick = 0;
	double firstSeconds = 0.0;
	uint32_t useCount = 0;
};

struct MidiEventRecord {
	uint32_t trackIndex = 0;
	uint32_t tick = 0;
	double seconds = 0.0;
	std::string kind;
	int channel = -1;
	int status = -1;
	int data1 = -1;
	int data2 = -1;
	std::string text;
	uint32_t sizeBytes = 0;
	std::vector<unsigned char> bytes;
};

struct MidiTrackSummary {
	uint32_t index = 0;
	std::string name;
	size_t eventCount = 0;
	size_t noteCount = 0;
	size_t ccCount = 0;
	size_t programChangeCount = 0;
	size_t pitchBendCount = 0;
	size_t sysexCount = 0;
	size_t markerCount = 0;
};

struct MidiNoteRangeSummary {
	int channel = 0;
	int lowestNote = 127;
	int highestNote = 0;
	size_t noteOnCount = 0;
};

struct MidiDrumHitSummary {
	int note = 0;
	std::string name;
	size_t hitCount = 0;
};

struct MidiAnalysisReport {
	bool valid = false;
	std::string sourcePath;
	std::string fileName;
	std::string errorMessage;
	uint16_t format = 0;
	uint16_t trackCountDeclared = 0;
	uint16_t trackCountParsed = 0;
	bool usesSmpteTiming = false;
	int ticksPerQuarterNote = 0;
	int smpteFramesPerSecond = 0;
	int smpteTicksPerFrame = 0;
	uint32_t totalTicks = 0;
	double durationSeconds = 0.0;
	size_t noteCount = 0;
	size_t ccCount = 0;
	size_t programChangeCount = 0;
	size_t pitchBendCount = 0;
	size_t sysexCount = 0;
	size_t markerCount = 0;
	size_t tempoChangeCount = 0;
	std::vector<MidiTempoChange> tempoChanges;
	std::vector<MidiMarker> markers;
	std::vector<MidiInstrumentUse> instruments;
	std::vector<MidiTrackSummary> tracks;
	std::vector<MidiNoteRangeSummary> noteRanges;
	std::vector<MidiDrumHitSummary> drumHits;
	std::vector<MidiEventRecord> events;

	std::string toJson() const;
	std::string toTextReport() const;
};

class MidiJsonWriter {
public:
	static std::string write(const MidiAnalysisReport & report);
};

class MidiTextWriter {
public:
	static std::string write(const MidiAnalysisReport & report);
};

class MidiReportExporter {
public:
	static bool exportAll(const MidiAnalysisReport & report, const std::string & outputPrefix, std::string & errorMessage);
};

class MidiFileAnalyzer {
public:
	MidiAnalysisReport analyzeFile(const std::string & path) const;
};
