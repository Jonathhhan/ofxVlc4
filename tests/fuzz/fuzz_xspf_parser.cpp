// LibFuzzer target for XSPF playlist parser
// Compile: clang++ -g -O1 -fsanitize=fuzzer,address,undefined fuzz_xspf_parser.cpp -I../../src/support -std=c++17 -o fuzz_xspf_parser
// Run: ./fuzz_xspf_parser corpus/xspf -max_total_time=60

#include "ofxVlc4PlaylistHelpers.h"
#include <cstdint>
#include <cstddef>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	// Convert raw bytes to a string.
	const std::string content(reinterpret_cast<const char*>(data), size);

	// Parse the XSPF content.
	const std::vector<std::string> items = ofxVlc4PlaylistHelpers::deserializeXSPF(content);

	// Serialize back to XSPF to exercise the serializer.
	if (!items.empty()) {
		ofxVlc4PlaylistHelpers::serializeXSPF(items, "Fuzz Test Playlist");
	}

	// Exercise XML escape/unescape helpers.
	for (const auto& item : items) {
		const std::string escaped = ofxVlc4PlaylistHelpers::xmlEscape(item);
		ofxVlc4PlaylistHelpers::xmlUnescape(escaped);
	}

	return 0;
}
