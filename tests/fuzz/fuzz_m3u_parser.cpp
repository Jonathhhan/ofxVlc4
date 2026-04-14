// LibFuzzer target for M3U playlist parser
// Compile: clang++ -g -O1 -fsanitize=fuzzer,address,undefined fuzz_m3u_parser.cpp -I../../src/support -std=c++17 -o fuzz_m3u_parser
// Run: ./fuzz_m3u_parser corpus/m3u -max_total_time=60

#include "ofxVlc4PlaylistHelpers.h"
#include <cstdint>
#include <cstddef>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	// Convert raw bytes to a string.
	const std::string content(reinterpret_cast<const char*>(data), size);

	// Parse the M3U content.
	const std::vector<std::string> items = ofxVlc4PlaylistHelpers::deserializeM3U(content);

	// Serialize back to M3U to exercise the serializer.
	if (!items.empty()) {
		ofxVlc4PlaylistHelpers::serializeM3U(items);
	}

	return 0;
}
