// LibFuzzer target for MIDI parser (ofxVlc4MidiAnalysis)
// Compile: clang++ -g -O1 -fsanitize=fuzzer,address,undefined fuzz_midi_parser.cpp ../../src/midi/ofxVlc4MidiAnalysis.cpp -I../../src/midi -o fuzz_midi_parser
// Run: ./fuzz_midi_parser corpus/midi -max_total_time=60

#include "ofxVlc4MidiAnalysis.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	// Write data to a temporary file for the MIDI parser to read.
	// The parser expects a file path, not raw bytes.
	const char* tempPath = "/tmp/fuzz_midi_input.mid";
	FILE* f = fopen(tempPath, "wb");
	if (!f) {
		return 0;
	}
	fwrite(data, 1, size, f);
	fclose(f);

	// Parse the MIDI file.
	MidiFileAnalyzer analyzer;
	MidiAnalysisReport report = analyzer.analyzeFile(tempPath);

	// Exercise the JSON and text report generators.
	if (report.valid) {
		report.toJson();
		report.toTextReport();
	}

	// Clean up temporary file.
	remove(tempPath);
	return 0;
}
