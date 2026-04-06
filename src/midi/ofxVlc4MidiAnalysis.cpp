#include "ofxVlc4MidiAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

uint16_t readBe16(const std::vector<uint8_t> & bytes, size_t offset) {
	return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
		static_cast<uint16_t>(bytes[offset + 1]));
}

uint32_t readBe32(const std::vector<uint8_t> & bytes, size_t offset) {
	return (static_cast<uint32_t>(bytes[offset]) << 24) |
		(static_cast<uint32_t>(bytes[offset + 1]) << 16) |
		(static_cast<uint32_t>(bytes[offset + 2]) << 8) |
		static_cast<uint32_t>(bytes[offset + 3]);
}

uint32_t readVlq(const std::vector<uint8_t> & bytes, size_t & offset, size_t endOffset) {
	uint32_t value = 0;
	int count = 0;
	while (offset < endOffset) {
		const uint8_t byte = bytes[offset++];
		value = (value << 7) | static_cast<uint32_t>(byte & 0x7F);
		++count;
		if ((byte & 0x80) == 0) {
			return value;
		}
		if (count >= 4) {
			break;
		}
	}
	throw std::runtime_error("Invalid MIDI variable-length value.");
}

std::string readText(const std::vector<uint8_t> & bytes, size_t offset, size_t length) {
	if (offset >= bytes.size()) {
		return "";
	}
	const size_t safeLength = std::min(length, bytes.size() - offset);
	return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
		bytes.begin() + static_cast<std::ptrdiff_t>(offset + safeLength));
}

std::string escapeJson(const std::string & value) {
	std::ostringstream stream;
	for (const unsigned char ch : value) {
		switch (ch) {
		case '\"': stream << "\\\""; break;
		case '\\': stream << "\\\\"; break;
		case '\b': stream << "\\b"; break;
		case '\f': stream << "\\f"; break;
		case '\n': stream << "\\n"; break;
		case '\r': stream << "\\r"; break;
		case '\t': stream << "\\t"; break;
		default:
			if (ch < 0x20) {
				stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
					   << std::dec << std::setfill(' ');
			} else {
				stream << static_cast<char>(ch);
			}
			break;
		}
	}
	return stream.str();
}

double bpmFromMpq(int microsecondsPerQuarter) {
	if (microsecondsPerQuarter <= 0) {
		return 0.0;
	}
	return 60000000.0 / static_cast<double>(microsecondsPerQuarter);
}

double smpteFramesPerSecond(int formatCode) {
	switch (formatCode) {
	case 24: return 24.0;
	case 25: return 25.0;
	case 29: return 29.97;
	case 30: return 30.0;
	default: return 30.0;
	}
}

double ticksToSeconds(uint32_t ticks, int microsecondsPerQuarter, int ppq) {
	if (ppq <= 0) {
		return 0.0;
	}
	return (static_cast<double>(ticks) * static_cast<double>(microsecondsPerQuarter)) /
		(static_cast<double>(ppq) * 1000000.0);
}

double tickToSecondsWithTempoMap(uint32_t tick, const std::vector<MidiTempoChange> & tempoChanges, int ppq) {
	if (tempoChanges.empty()) {
		return ticksToSeconds(tick, 500000, ppq);
	}

	size_t selectedIndex = 0;
	for (size_t i = 1; i < tempoChanges.size(); ++i) {
		if (tempoChanges[i].tick > tick) {
			break;
		}
		selectedIndex = i;
	}

	const MidiTempoChange & tempo = tempoChanges[selectedIndex];
	return tempo.seconds + ticksToSeconds(tick - tempo.tick, tempo.microsecondsPerQuarter, ppq);
}

std::string gmProgramName(int program) {
	static const std::array<const char *, 128> kNames = {{
		"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
		"Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
		"Celesta", "Glockenspiel", "Music Box", "Vibraphone",
		"Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
		"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
		"Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
		"Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
		"Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
		"Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass",
		"Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
		"Violin", "Viola", "Cello", "Contrabass",
		"Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
		"String Ensemble 1", "String Ensemble 2", "SynthStrings 1", "SynthStrings 2",
		"Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
		"Trumpet", "Trombone", "Tuba", "Muted Trumpet",
		"French Horn", "Brass Section", "SynthBrass 1", "SynthBrass 2",
		"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
		"Oboe", "English Horn", "Bassoon", "Clarinet",
		"Piccolo", "Flute", "Recorder", "Pan Flute",
		"Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
		"Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)",
		"Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
		"Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
		"Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
		"FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
		"FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
		"Sitar", "Banjo", "Shamisen", "Koto",
		"Kalimba", "Bag pipe", "Fiddle", "Shanai",
		"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
		"Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
		"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
		"Telephone Ring", "Helicopter", "Applause", "Gunshot"
	}};

	if (program < 0 || program >= static_cast<int>(kNames.size())) {
		return "";
	}
	return kNames[static_cast<size_t>(program)];
}

std::string metaTypeLabel(uint8_t metaType) {
	switch (metaType) {
	case 0x01: return "text";
	case 0x02: return "copyright";
	case 0x03: return "track_name";
	case 0x04: return "instrument_name";
	case 0x05: return "lyrics";
	case 0x06: return "marker";
	case 0x07: return "cue_point";
	default: return "meta";
	}
}

std::string drumNoteName(int note) {
	switch (note) {
	case 35: return "Acoustic Bass Drum";
	case 36: return "Bass Drum 1";
	case 37: return "Side Stick";
	case 38: return "Acoustic Snare";
	case 39: return "Hand Clap";
	case 40: return "Electric Snare";
	case 41: return "Low Floor Tom";
	case 42: return "Closed Hi-Hat";
	case 43: return "High Floor Tom";
	case 44: return "Pedal Hi-Hat";
	case 45: return "Low Tom";
	case 46: return "Open Hi-Hat";
	case 47: return "Low-Mid Tom";
	case 48: return "Hi-Mid Tom";
	case 49: return "Crash Cymbal 1";
	case 50: return "High Tom";
	case 51: return "Ride Cymbal 1";
	case 52: return "Chinese Cymbal";
	case 53: return "Ride Bell";
	case 54: return "Tambourine";
	case 55: return "Splash Cymbal";
	case 56: return "Cowbell";
	case 57: return "Crash Cymbal 2";
	case 58: return "Vibraslap";
	case 59: return "Ride Cymbal 2";
	case 60: return "Hi Bongo";
	case 61: return "Low Bongo";
	case 62: return "Mute Hi Conga";
	case 63: return "Open Hi Conga";
	case 64: return "Low Conga";
	case 65: return "High Timbale";
	case 66: return "Low Timbale";
	case 67: return "High Agogo";
	case 68: return "Low Agogo";
	case 69: return "Cabasa";
	case 70: return "Maracas";
	case 71: return "Short Whistle";
	case 72: return "Long Whistle";
	case 73: return "Short Guiro";
	case 74: return "Long Guiro";
	case 75: return "Claves";
	case 76: return "Hi Wood Block";
	case 77: return "Low Wood Block";
	case 78: return "Mute Cuica";
	case 79: return "Open Cuica";
	case 80: return "Mute Triangle";
	case 81: return "Open Triangle";
	default: return "";
	}
}

std::string csvEscape(const std::string & value) {
	const bool needsQuotes = value.find_first_of(",\"\n\r") != std::string::npos;
	if (!needsQuotes) {
		return value;
	}

	std::string escaped = "\"";
	for (char ch : value) {
		if (ch == '"') {
			escaped += "\"\"";
		} else {
			escaped += ch;
		}
	}
	escaped += "\"";
	return escaped;
}

bool writeTextFile(const std::string & path, const std::string & contents, std::string & errorMessage) {
	std::ofstream output(path, std::ios::binary);
	if (!output) {
		errorMessage = "Could not open export file: " + path;
		return false;
	}
	output << contents;
	return true;
}

} // namespace

std::string MidiJsonWriter::write(const MidiAnalysisReport & report) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(6);
	stream << "{";
	stream << "\"valid\":" << (report.valid ? "true" : "false") << ",";
	stream << "\"sourcePath\":\"" << escapeJson(report.sourcePath) << "\",";
	stream << "\"fileName\":\"" << escapeJson(report.fileName) << "\",";
	stream << "\"errorMessage\":\"" << escapeJson(report.errorMessage) << "\",";
	stream << "\"format\":" << report.format << ",";
	stream << "\"trackCountDeclared\":" << report.trackCountDeclared << ",";
	stream << "\"trackCountParsed\":" << report.trackCountParsed << ",";
	stream << "\"usesSmpteTiming\":" << (report.usesSmpteTiming ? "true" : "false") << ",";
	stream << "\"ticksPerQuarterNote\":" << report.ticksPerQuarterNote << ",";
	stream << "\"smpteFramesPerSecond\":" << report.smpteFramesPerSecond << ",";
	stream << "\"smpteTicksPerFrame\":" << report.smpteTicksPerFrame << ",";
	stream << "\"totalTicks\":" << report.totalTicks << ",";
	stream << "\"durationSeconds\":" << report.durationSeconds << ",";
	stream << "\"counts\":{"
		   << "\"notes\":" << report.noteCount << ","
		   << "\"controlChanges\":" << report.ccCount << ","
		   << "\"programChanges\":" << report.programChangeCount << ","
		   << "\"pitchBends\":" << report.pitchBendCount << ","
		   << "\"sysex\":" << report.sysexCount << ","
		   << "\"markers\":" << report.markerCount << ","
		   << "\"tempoChanges\":" << report.tempoChangeCount
		   << "},";

	stream << "\"tempoChanges\":[";
	for (size_t i = 0; i < report.tempoChanges.size(); ++i) {
		const MidiTempoChange & tempo = report.tempoChanges[i];
		if (i > 0) {
			stream << ",";
		}
		stream << "{"
			   << "\"tick\":" << tempo.tick << ","
			   << "\"seconds\":" << tempo.seconds << ","
			   << "\"microsecondsPerQuarter\":" << tempo.microsecondsPerQuarter << ","
			   << "\"bpm\":" << tempo.bpm
			   << "}";
	}
	stream << "],";

	stream << "\"markers\":[";
	for (size_t i = 0; i < report.markers.size(); ++i) {
		const MidiMarker & marker = report.markers[i];
		if (i > 0) {
			stream << ",";
		}
		stream << "{"
			   << "\"tick\":" << marker.tick << ","
			   << "\"seconds\":" << marker.seconds << ","
			   << "\"type\":\"" << escapeJson(marker.type) << "\","
			   << "\"text\":\"" << escapeJson(marker.text) << "\""
			   << "}";
	}
	stream << "],";

	stream << "\"instruments\":[";
	for (size_t i = 0; i < report.instruments.size(); ++i) {
		const MidiInstrumentUse & instrument = report.instruments[i];
		if (i > 0) {
			stream << ",";
		}
		stream << "{"
			   << "\"channel\":" << instrument.channel << ","
			   << "\"bankMsb\":" << instrument.bankMsb << ","
			   << "\"bankLsb\":" << instrument.bankLsb << ","
			   << "\"program\":" << instrument.program << ","
			   << "\"programName\":\"" << escapeJson(instrument.programName) << "\","
			   << "\"firstTick\":" << instrument.firstTick << ","
			   << "\"firstSeconds\":" << instrument.firstSeconds << ","
			   << "\"useCount\":" << instrument.useCount
			   << "}";
	}
	stream << "],";

	stream << "\"tracks\":[";
	for (size_t i = 0; i < report.tracks.size(); ++i) {
		const MidiTrackSummary & track = report.tracks[i];
		if (i > 0) {
			stream << ",";
		}
		stream << "{"
			   << "\"index\":" << track.index << ","
			   << "\"name\":\"" << escapeJson(track.name) << "\","
			   << "\"eventCount\":" << track.eventCount << ","
			   << "\"noteCount\":" << track.noteCount << ","
			   << "\"controlChangeCount\":" << track.ccCount << ","
			   << "\"programChangeCount\":" << track.programChangeCount << ","
			   << "\"pitchBendCount\":" << track.pitchBendCount << ","
			   << "\"sysexCount\":" << track.sysexCount << ","
			   << "\"markerCount\":" << track.markerCount
			   << "}";
	}
	stream << "],";

	stream << "\"noteRanges\":[";
	for (size_t i = 0; i < report.noteRanges.size(); ++i) {
		const MidiNoteRangeSummary & range = report.noteRanges[i];
		if (i > 0) {
			stream << ",";
		}
		stream << "{"
			   << "\"channel\":" << range.channel << ","
			   << "\"lowestNote\":" << range.lowestNote << ","
			   << "\"highestNote\":" << range.highestNote << ","
			   << "\"noteOnCount\":" << range.noteOnCount
			   << "}";
	}
	stream << "],";

	stream << "\"drumHits\":[";
	for (size_t i = 0; i < report.drumHits.size(); ++i) {
		const MidiDrumHitSummary & hit = report.drumHits[i];
		if (i > 0) {
			stream << ",";
		}
		stream << "{"
			   << "\"note\":" << hit.note << ","
			   << "\"name\":\"" << escapeJson(hit.name) << "\","
			   << "\"hitCount\":" << hit.hitCount
			   << "}";
	}
	stream << "],";

	stream << "\"events\":[";
	for (size_t i = 0; i < report.events.size(); ++i) {
		const MidiEventRecord & event = report.events[i];
		if (i > 0) {
			stream << ",";
		}
		stream << "{"
			   << "\"trackIndex\":" << event.trackIndex << ","
			   << "\"tick\":" << event.tick << ","
			   << "\"seconds\":" << event.seconds << ","
			   << "\"kind\":\"" << escapeJson(event.kind) << "\","
			   << "\"channel\":" << event.channel << ","
			   << "\"status\":" << event.status << ","
			   << "\"data1\":" << event.data1 << ","
			   << "\"data2\":" << event.data2 << ","
			   << "\"text\":\"" << escapeJson(event.text) << "\","
			   << "\"sizeBytes\":" << event.sizeBytes
			   << "}";
	}
	stream << "]";
	stream << "}";
	return stream.str();
}

std::string MidiAnalysisReport::toJson() const {
	return MidiJsonWriter::write(*this);
}

std::string MidiTextWriter::write(const MidiAnalysisReport & report) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(2);
	stream << "MIDI Report\n";
	stream << "File: " << report.fileName << "\n";
	stream << "Source: " << report.sourcePath << "\n";
	stream << "Valid: " << (report.valid ? "yes" : "no") << "\n";
	if (!report.valid) {
		stream << "Error: " << report.errorMessage << "\n";
		return stream.str();
	}

	stream << "Format: " << report.format << "\n";
	stream << "Tracks: " << report.trackCountParsed << " / " << report.trackCountDeclared << "\n";
	stream << "PPQ: " << report.ticksPerQuarterNote << "\n";
	stream << "Duration: " << report.durationSeconds << " s\n";
	stream << "Counts: notes=" << report.noteCount
		   << " cc=" << report.ccCount
		   << " program=" << report.programChangeCount
		   << " pitchBend=" << report.pitchBendCount
		   << " sysex=" << report.sysexCount
		   << " markers=" << report.markerCount
		   << " tempo=" << report.tempoChangeCount << "\n";

	stream << "\nTempo Changes\n";
	for (const MidiTempoChange & tempo : report.tempoChanges) {
		stream << "  tick " << tempo.tick << " | " << tempo.seconds << " s | "
			   << tempo.bpm << " BPM\n";
	}

	stream << "\nMarkers\n";
	for (const MidiMarker & marker : report.markers) {
		stream << "  tick " << marker.tick << " | " << marker.seconds << " s | "
			   << marker.type << " | " << marker.text << "\n";
	}

	stream << "\nInstruments\n";
	for (const MidiInstrumentUse & instrument : report.instruments) {
		stream << "  ch " << (instrument.channel + 1)
			   << " prog " << instrument.program;
		if (!instrument.programName.empty()) {
			stream << " " << instrument.programName;
		}
		stream << " | first " << instrument.firstSeconds << " s"
			   << " | uses " << instrument.useCount << "\n";
	}

	stream << "\nNote Ranges\n";
	for (const MidiNoteRangeSummary & range : report.noteRanges) {
		stream << "  ch " << (range.channel + 1)
			   << " | low " << range.lowestNote
			   << " | high " << range.highestNote
			   << " | note-ons " << range.noteOnCount << "\n";
	}

	stream << "\nDrum Hits\n";
	for (const MidiDrumHitSummary & hit : report.drumHits) {
		stream << "  note " << hit.note;
		if (!hit.name.empty()) {
			stream << " " << hit.name;
		}
		stream << " | hits " << hit.hitCount << "\n";
	}

	stream << "\nTracks\n";
	for (const MidiTrackSummary & track : report.tracks) {
		stream << "  #" << track.index << " " << track.name
			   << " | events " << track.eventCount
			   << " | notes " << track.noteCount
			   << " | cc " << track.ccCount
			   << " | program " << track.programChangeCount
			   << " | bend " << track.pitchBendCount
			   << " | sysex " << track.sysexCount
			   << " | markers " << track.markerCount << "\n";
	}

	return stream.str();
}

std::string MidiAnalysisReport::toTextReport() const {
	return MidiTextWriter::write(*this);
}

bool MidiReportExporter::exportAll(const MidiAnalysisReport & report, const std::string & outputPrefix, std::string & errorMessage) {
	if (!report.valid) {
		errorMessage = "Cannot export an invalid MIDI report.";
		return false;
	}

	const std::filesystem::path prefix(outputPrefix);
	std::error_code ec;
	std::filesystem::create_directories(prefix.parent_path(), ec);

	if (!writeTextFile(outputPrefix + ".txt", report.toTextReport(), errorMessage)) {
		return false;
	}

	std::ostringstream tempoCsv;
	tempoCsv << "tick,seconds,microsecondsPerQuarter,bpm\n";
	for (const MidiTempoChange & tempo : report.tempoChanges) {
		tempoCsv << tempo.tick << "," << tempo.seconds << "," << tempo.microsecondsPerQuarter << "," << tempo.bpm << "\n";
	}
	if (!writeTextFile(outputPrefix + ".tempo.csv", tempoCsv.str(), errorMessage)) {
		return false;
	}

	std::ostringstream markerCsv;
	markerCsv << "tick,seconds,type,text\n";
	for (const MidiMarker & marker : report.markers) {
		markerCsv << marker.tick << "," << marker.seconds << ","
				  << csvEscape(marker.type) << "," << csvEscape(marker.text) << "\n";
	}
	if (!writeTextFile(outputPrefix + ".markers.csv", markerCsv.str(), errorMessage)) {
		return false;
	}

	std::ostringstream instrumentCsv;
	instrumentCsv << "channel,bankMsb,bankLsb,program,programName,firstTick,firstSeconds,useCount\n";
	for (const MidiInstrumentUse & instrument : report.instruments) {
		instrumentCsv << instrument.channel << "," << instrument.bankMsb << "," << instrument.bankLsb << ","
					  << instrument.program << "," << csvEscape(instrument.programName) << ","
					  << instrument.firstTick << "," << instrument.firstSeconds << "," << instrument.useCount << "\n";
	}
	if (!writeTextFile(outputPrefix + ".instruments.csv", instrumentCsv.str(), errorMessage)) {
		return false;
	}

	std::ostringstream trackCsv;
	trackCsv << "index,name,eventCount,noteCount,controlChangeCount,programChangeCount,pitchBendCount,sysexCount,markerCount\n";
	for (const MidiTrackSummary & track : report.tracks) {
		trackCsv << track.index << "," << csvEscape(track.name) << "," << track.eventCount << ","
				 << track.noteCount << "," << track.ccCount << "," << track.programChangeCount << ","
				 << track.pitchBendCount << "," << track.sysexCount << "," << track.markerCount << "\n";
	}
	if (!writeTextFile(outputPrefix + ".tracks.csv", trackCsv.str(), errorMessage)) {
		return false;
	}

	std::ostringstream rangeCsv;
	rangeCsv << "channel,lowestNote,highestNote,noteOnCount\n";
	for (const MidiNoteRangeSummary & range : report.noteRanges) {
		rangeCsv << range.channel << "," << range.lowestNote << "," << range.highestNote << "," << range.noteOnCount << "\n";
	}
	if (!writeTextFile(outputPrefix + ".note_ranges.csv", rangeCsv.str(), errorMessage)) {
		return false;
	}

	std::ostringstream drumCsv;
	drumCsv << "note,name,hitCount\n";
	for (const MidiDrumHitSummary & hit : report.drumHits) {
		drumCsv << hit.note << "," << csvEscape(hit.name) << "," << hit.hitCount << "\n";
	}
	if (!writeTextFile(outputPrefix + ".drums.csv", drumCsv.str(), errorMessage)) {
		return false;
	}

	return true;
}

MidiAnalysisReport MidiFileAnalyzer::analyzeFile(const std::string & path) const {
	MidiAnalysisReport report;
	report.sourcePath = path;

	try {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Could not open MIDI file.");
		}

		std::vector<uint8_t> bytes(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		if (bytes.size() < 14) {
			throw std::runtime_error("File is too small to be a valid MIDI file.");
		}

		const auto lastSlash = path.find_last_of("/\\");
		report.fileName = lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);

		if (readText(bytes, 0, 4) != "MThd") {
			throw std::runtime_error("Missing MIDI header chunk.");
		}

		const uint32_t headerLength = readBe32(bytes, 4);
		if (headerLength < 6 || bytes.size() < 8 + headerLength) {
			throw std::runtime_error("Invalid MIDI header length.");
		}

		report.format = readBe16(bytes, 8);
		report.trackCountDeclared = readBe16(bytes, 10);
		const uint16_t division = readBe16(bytes, 12);
		if ((division & 0x8000) == 0) {
			report.ticksPerQuarterNote = division;
		} else {
			report.usesSmpteTiming = true;
			const int8_t smpteByte = static_cast<int8_t>(static_cast<uint8_t>(division >> 8));
			report.smpteFramesPerSecond = std::abs(static_cast<int>(smpteByte));
			report.smpteTicksPerFrame = division & 0xFF;
		}

		size_t offset = 8 + headerLength;
		std::vector<MidiTempoChange> tempoChanges;
		std::vector<MidiMarker> markers;
		std::vector<MidiTrackSummary> tracks;
		std::vector<MidiEventRecord> events;
		std::unordered_map<std::string, MidiInstrumentUse> instruments;
		std::array<MidiNoteRangeSummary, 16> noteRanges;
		std::array<bool, 16> noteRangeUsed {};
		std::unordered_map<int, size_t> drumHitCounts;
		for (int channel = 0; channel < 16; ++channel) {
			noteRanges[static_cast<size_t>(channel)].channel = channel;
		}
		uint32_t totalTicks = 0;

		for (uint16_t trackIndex = 0; trackIndex < report.trackCountDeclared && offset + 8 <= bytes.size(); ++trackIndex) {
			if (readText(bytes, offset, 4) != "MTrk") {
				throw std::runtime_error("Missing MIDI track chunk.");
			}
			const uint32_t trackLength = readBe32(bytes, offset + 4);
			offset += 8;
			if (offset + trackLength > bytes.size()) {
				throw std::runtime_error("Invalid MIDI track length.");
			}

			const size_t trackEnd = offset + trackLength;
			size_t trackOffset = offset;
			offset = trackEnd;

			MidiTrackSummary trackSummary;
			trackSummary.index = trackIndex;
			std::array<int, 16> bankMsb;
			std::array<int, 16> bankLsb;
			bankMsb.fill(-1);
			bankLsb.fill(-1);
			uint8_t runningStatus = 0;
			uint32_t tick = 0;

			while (trackOffset < trackEnd) {
				const uint32_t delta = readVlq(bytes, trackOffset, trackEnd);
				tick += delta;
				totalTicks = std::max(totalTicks, tick);
				if (trackOffset >= trackEnd) {
					break;
				}

				uint8_t status = bytes[trackOffset];
				bool usedRunningStatus = false;
				if (status < 0x80) {
					if (runningStatus == 0) {
						throw std::runtime_error("Encountered MIDI running status without a previous status byte.");
					}
					status = runningStatus;
					usedRunningStatus = true;
				} else {
					++trackOffset;
					if (status < 0xF0) {
						runningStatus = status;
					} else {
						runningStatus = 0;
					}
				}

				if (status == 0xFF) {
					if (trackOffset >= trackEnd) {
						throw std::runtime_error("Unexpected end of track during meta event.");
					}
					const uint8_t metaType = bytes[trackOffset++];
					const uint32_t metaLength = readVlq(bytes, trackOffset, trackEnd);
					if (trackOffset + metaLength > trackEnd) {
						throw std::runtime_error("Invalid meta event length.");
					}

					const std::string text = readText(bytes, trackOffset, metaLength);
					if (metaType == 0x51 && metaLength == 3) {
						const int mpq = (static_cast<int>(bytes[trackOffset]) << 16) |
							(static_cast<int>(bytes[trackOffset + 1]) << 8) |
							static_cast<int>(bytes[trackOffset + 2]);
						MidiTempoChange tempo;
						tempo.tick = tick;
						tempo.microsecondsPerQuarter = mpq;
						tempo.bpm = bpmFromMpq(mpq);
						tempoChanges.push_back(tempo);

						MidiEventRecord tempoEvent;
						tempoEvent.trackIndex = trackIndex;
						tempoEvent.tick = tick;
						tempoEvent.kind = "tempo";
						tempoEvent.text = "Tempo";
						tempoEvent.data1 = mpq;
						tempoEvent.sizeBytes = metaLength;
						tempoEvent.bytes.reserve(2 + metaLength);
						tempoEvent.bytes.push_back(0xFF);
						tempoEvent.bytes.push_back(metaType);
						tempoEvent.bytes.insert(tempoEvent.bytes.end(), bytes.begin() + trackOffset, bytes.begin() + trackOffset + metaLength);
						events.push_back(tempoEvent);
						++trackSummary.eventCount;
					} else if (metaType == 0x03) {
						trackSummary.name = text;
					} else if (metaType == 0x06 || metaType == 0x07 || metaType == 0x05 || metaType == 0x01 || metaType == 0x04) {
						MidiMarker marker;
						marker.tick = tick;
						marker.type = metaTypeLabel(metaType);
						marker.text = text;
						markers.push_back(marker);
						++trackSummary.markerCount;

						MidiEventRecord event;
						event.trackIndex = trackIndex;
						event.tick = tick;
						event.kind = marker.type;
						event.text = text;
						event.sizeBytes = metaLength;
						event.bytes.reserve(2 + metaLength);
						event.bytes.push_back(0xFF);
						event.bytes.push_back(metaType);
						event.bytes.insert(event.bytes.end(), bytes.begin() + trackOffset, bytes.begin() + trackOffset + metaLength);
						events.push_back(event);
						++trackSummary.eventCount;
					}

					trackOffset += metaLength;
					if (metaType == 0x2F) {
						break;
					}
					continue;
				}

				if (status == 0xF0 || status == 0xF7) {
					const uint32_t sysexLength = readVlq(bytes, trackOffset, trackEnd);
					if (trackOffset + sysexLength > trackEnd) {
						throw std::runtime_error("Invalid sysex event length.");
					}

					MidiEventRecord event;
					event.trackIndex = trackIndex;
					event.tick = tick;
					event.kind = "sysex";
					event.status = status;
					event.sizeBytes = sysexLength;
					event.bytes.reserve(1 + sysexLength);
					event.bytes.push_back(status);
					event.bytes.insert(event.bytes.end(), bytes.begin() + trackOffset, bytes.begin() + trackOffset + sysexLength);
					events.push_back(event);
					++trackSummary.eventCount;
					++trackSummary.sysexCount;
					trackOffset += sysexLength;
					continue;
				}

				const uint8_t eventType = status & 0xF0;
				const int channel = status & 0x0F;
				const bool singleDataByte = (eventType == 0xC0 || eventType == 0xD0);
				int data1 = 0;
				int data2 = -1;
				if (usedRunningStatus) {
					data1 = bytes[trackOffset++];
				} else {
					if (trackOffset >= trackEnd) {
						throw std::runtime_error("Unexpected end of track during MIDI event.");
					}
					data1 = bytes[trackOffset++];
				}
				if (!singleDataByte) {
					if (trackOffset >= trackEnd) {
						throw std::runtime_error("Unexpected end of track during MIDI event data.");
					}
					data2 = bytes[trackOffset++];
				}

				MidiEventRecord event;
				event.trackIndex = trackIndex;
				event.tick = tick;
				event.channel = channel;
				event.status = status;
				event.data1 = data1;
				event.data2 = data2;
				event.sizeBytes = singleDataByte ? 1 : 2;
				event.bytes.push_back(status);
				event.bytes.push_back(data1);
				if (!singleDataByte) {
					event.bytes.push_back(data2);
				}

				switch (eventType) {
				case 0x80:
					event.kind = "note_off";
					break;
				case 0x90:
					event.kind = (data2 == 0) ? "note_off" : "note_on";
					if (data2 > 0) {
						++trackSummary.noteCount;
						++report.noteCount;
						if (channel == 9) {
							++drumHitCounts[data1];
						} else {
							MidiNoteRangeSummary & range = noteRanges[static_cast<size_t>(channel)];
							noteRangeUsed[static_cast<size_t>(channel)] = true;
							range.lowestNote = std::min(range.lowestNote, data1);
							range.highestNote = std::max(range.highestNote, data1);
							++range.noteOnCount;
						}
					}
					break;
				case 0xB0:
					event.kind = "control_change";
					++trackSummary.ccCount;
					++report.ccCount;
					if (data1 == 0) {
						bankMsb[static_cast<size_t>(channel)] = data2;
					} else if (data1 == 32) {
						bankLsb[static_cast<size_t>(channel)] = data2;
					}
					break;
				case 0xC0: {
					event.kind = "program_change";
					++trackSummary.programChangeCount;
					++report.programChangeCount;
					const std::string key = std::to_string(channel) + ":" +
						std::to_string(bankMsb[static_cast<size_t>(channel)]) + ":" +
						std::to_string(bankLsb[static_cast<size_t>(channel)]) + ":" +
						std::to_string(data1);
					MidiInstrumentUse & instrument = instruments[key];
					if (instrument.useCount == 0) {
						instrument.channel = channel;
						instrument.bankMsb = bankMsb[static_cast<size_t>(channel)];
						instrument.bankLsb = bankLsb[static_cast<size_t>(channel)];
						instrument.program = data1;
						instrument.programName = gmProgramName(data1);
						instrument.firstTick = tick;
					}
					++instrument.useCount;
					break;
				}
				case 0xE0:
					event.kind = "pitch_bend";
					++trackSummary.pitchBendCount;
					++report.pitchBendCount;
					break;
				case 0xA0:
					event.kind = "poly_aftertouch";
					break;
				case 0xD0:
					event.kind = "channel_aftertouch";
					break;
				default:
					event.kind = "midi";
					break;
				}

				events.push_back(event);
				++trackSummary.eventCount;
			}

			tracks.push_back(trackSummary);
		}

		if (!report.usesSmpteTiming) {
			if (tempoChanges.empty() || tempoChanges.front().tick != 0) {
				MidiTempoChange initialTempo;
				initialTempo.tick = 0;
				initialTempo.microsecondsPerQuarter = 500000;
				initialTempo.bpm = bpmFromMpq(initialTempo.microsecondsPerQuarter);
				tempoChanges.push_back(initialTempo);
			}

			std::sort(tempoChanges.begin(), tempoChanges.end(), [](const MidiTempoChange & a, const MidiTempoChange & b) {
				return a.tick < b.tick;
			});
			tempoChanges.erase(std::unique(tempoChanges.begin(), tempoChanges.end(),
				[](const MidiTempoChange & a, const MidiTempoChange & b) { return a.tick == b.tick; }),
				tempoChanges.end());

			tempoChanges.front().seconds = 0.0;
			for (size_t i = 1; i < tempoChanges.size(); ++i) {
				const MidiTempoChange & previousTempo = tempoChanges[i - 1];
				tempoChanges[i].seconds = previousTempo.seconds +
					ticksToSeconds(tempoChanges[i].tick - previousTempo.tick,
						previousTempo.microsecondsPerQuarter,
						report.ticksPerQuarterNote);
			}

			for (MidiEventRecord & event : events) {
				event.seconds = tickToSecondsWithTempoMap(event.tick, tempoChanges, report.ticksPerQuarterNote);
			}
			for (MidiMarker & marker : markers) {
				marker.seconds = tickToSecondsWithTempoMap(marker.tick, tempoChanges, report.ticksPerQuarterNote);
			}
			for (auto & pair : instruments) {
				pair.second.firstSeconds =
					tickToSecondsWithTempoMap(pair.second.firstTick, tempoChanges, report.ticksPerQuarterNote);
			}
			report.durationSeconds = tickToSecondsWithTempoMap(totalTicks, tempoChanges, report.ticksPerQuarterNote);
		} else {
			const double fps = smpteFramesPerSecond(report.smpteFramesPerSecond);
			const double ticksPerSecond = fps * static_cast<double>(std::max(1, report.smpteTicksPerFrame));
			for (MidiEventRecord & event : events) {
				event.seconds = ticksPerSecond > 0.0 ? static_cast<double>(event.tick) / ticksPerSecond : 0.0;
			}
			for (MidiMarker & marker : markers) {
				marker.seconds = ticksPerSecond > 0.0 ? static_cast<double>(marker.tick) / ticksPerSecond : 0.0;
			}
			for (auto & pair : instruments) {
				pair.second.firstSeconds =
					ticksPerSecond > 0.0 ? static_cast<double>(pair.second.firstTick) / ticksPerSecond : 0.0;
			}
			report.durationSeconds =
				ticksPerSecond > 0.0 ? static_cast<double>(totalTicks) / ticksPerSecond : 0.0;
		}

		std::sort(markers.begin(), markers.end(), [](const MidiMarker & a, const MidiMarker & b) {
			return a.tick < b.tick;
		});
		std::sort(events.begin(), events.end(), [](const MidiEventRecord & a, const MidiEventRecord & b) {
			if (a.tick != b.tick) {
				return a.tick < b.tick;
			}
			return a.trackIndex < b.trackIndex;
		});

		report.valid = true;
		report.trackCountParsed = static_cast<uint16_t>(tracks.size());
		report.totalTicks = totalTicks;
		report.tempoChangeCount = tempoChanges.size();
		report.markerCount = markers.size();
		report.sysexCount = std::count_if(events.begin(), events.end(), [](const MidiEventRecord & event) {
			return event.kind == "sysex";
		});
		report.tempoChanges = std::move(tempoChanges);
		report.markers = std::move(markers);
		report.tracks = std::move(tracks);
		report.events = std::move(events);

		std::vector<MidiInstrumentUse> instrumentList;
		instrumentList.reserve(instruments.size());
		for (const auto & pair : instruments) {
			instrumentList.push_back(pair.second);
		}
		std::sort(instrumentList.begin(), instrumentList.end(), [](const MidiInstrumentUse & a, const MidiInstrumentUse & b) {
			if (a.channel != b.channel) {
				return a.channel < b.channel;
			}
			if (a.program != b.program) {
				return a.program < b.program;
			}
			return a.firstTick < b.firstTick;
		});
		report.instruments = std::move(instrumentList);

		for (size_t channel = 0; channel < noteRanges.size(); ++channel) {
			if (noteRangeUsed[channel] && noteRanges[channel].noteOnCount > 0) {
				report.noteRanges.push_back(noteRanges[channel]);
			}
		}

		for (const auto & pair : drumHitCounts) {
			MidiDrumHitSummary hit;
			hit.note = pair.first;
			hit.name = drumNoteName(pair.first);
			hit.hitCount = pair.second;
			report.drumHits.push_back(hit);
		}
		std::sort(report.drumHits.begin(), report.drumHits.end(), [](const MidiDrumHitSummary & a, const MidiDrumHitSummary & b) {
			if (a.hitCount != b.hitCount) {
				return a.hitCount > b.hitCount;
			}
			return a.note < b.note;
		});
	} catch (const std::exception & error) {
		report.valid = false;
		report.errorMessage = error.what();
	}

	return report;
}
