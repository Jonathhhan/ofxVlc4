#include "vlc/vlc.h"
#include "ofxVlc4Utils.h"

#include <cassert>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test harness (same style as test_ringbuffer.cpp)
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;
static std::string g_currentSuite;

static void beginSuite(const char * name) {
	g_currentSuite = name;
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

// ---------------------------------------------------------------------------
// isUri tests
// ---------------------------------------------------------------------------

static void testIsUri() {
	using ofxVlc4Utils::isUri;

	beginSuite("isUri — valid URIs");
	CHECK(isUri("http://example.com/video.mp4"));
	CHECK(isUri("https://example.com/stream"));
	CHECK(isUri("rtsp://192.168.1.1/stream"));
	CHECK(isUri("file:///home/user/video.mp4"));
	CHECK(isUri("rtp://239.0.0.1:5004"));

	beginSuite("isUri — local paths (must return false)");
	// Plain file paths should never be classified as URIs.
	CHECK(!isUri("/home/user/video.mp4"));
	CHECK(!isUri("relative/path/video.mp4"));
	CHECK(!isUri("C:/Users/video.mp4"));
	CHECK(!isUri(""));

	beginSuite("isUri — path with '://' inside a directory component (regression)");
	// A local path whose directory segment happens to contain "://"
	// must not be misclassified as a URI.
	CHECK(!isUri("/mnt/foo://bar/video.mp4"));
	CHECK(!isUri("some/path://like/this.wav"));

	beginSuite("isUri — scheme must start with a letter");
	CHECK(!isUri("1http://example.com"));
	CHECK(!isUri("://noscheme.com"));

	beginSuite("isUri — exactly one occurrence (no false negatives/positives)");
	// Double "://" should still work if the first one forms a valid scheme.
	CHECK(isUri("http://host/path?url=rtsp://inner"));
}

// ---------------------------------------------------------------------------
// trimWhitespace tests
// ---------------------------------------------------------------------------

static void testTrimWhitespace() {
	using ofxVlc4Utils::trimWhitespace;

	beginSuite("trimWhitespace");
	CHECK(trimWhitespace("  hello  ") == "hello");
	CHECK(trimWhitespace("\t\nhello\r\n") == "hello");
	CHECK(trimWhitespace("no-spaces") == "no-spaces");
	CHECK(trimWhitespace("   ") == "");
	CHECK(trimWhitespace("") == "");
}

// ---------------------------------------------------------------------------
// sanitizeFileStem tests
// ---------------------------------------------------------------------------

static void testSanitizeFileStem() {
	using ofxVlc4Utils::sanitizeFileStem;

	beginSuite("sanitizeFileStem");
	CHECK(sanitizeFileStem("hello") == "hello");
	CHECK(sanitizeFileStem("file<name>") == "file_name_");
	CHECK(sanitizeFileStem("a:b/c\\d") == "a_b_c_d");
	CHECK(sanitizeFileStem("") == "snapshot");
	CHECK(sanitizeFileStem("   ") == "snapshot");
}

// ---------------------------------------------------------------------------
// parseFilterChainEntries / joinFilterChainEntries tests
// ---------------------------------------------------------------------------

static void testFilterChain() {
	using ofxVlc4Utils::parseFilterChainEntries;
	using ofxVlc4Utils::joinFilterChainEntries;

	beginSuite("parseFilterChainEntries");
	{
		const auto entries = parseFilterChainEntries("equalizer:compressor");
		CHECK(entries.size() == 2);
		CHECK(!entries.empty() && entries[0] == "equalizer");
		CHECK(entries.size() >= 2 && entries[1] == "compressor");
	}
	{
		// Duplicates should be stripped.
		const auto entries = parseFilterChainEntries("eq:eq:comp");
		CHECK(entries.size() == 2);
	}
	{
		// Empty chain → empty vector.
		const auto entries = parseFilterChainEntries("");
		CHECK(entries.empty());
	}

	beginSuite("joinFilterChainEntries");
	{
		const std::string joined = joinFilterChainEntries({ "equalizer", "compressor" });
		CHECK(joined == "equalizer:compressor");
	}
	{
		const std::string joined = joinFilterChainEntries({});
		CHECK(joined.empty());
	}
}

// ---------------------------------------------------------------------------
// fileNameFromUri tests
// ---------------------------------------------------------------------------

static void testFileNameFromUri() {
	using ofxVlc4Utils::fileNameFromUri;

	beginSuite("fileNameFromUri");
	CHECK(fileNameFromUri("http://example.com/video.mp4") == "video.mp4");
	CHECK(fileNameFromUri("http://example.com/video.mp4?foo=bar") == "video.mp4");
	// When the URI ends with '/', find_last_of returns the trailing slash and
	// slashPos+1 == size(), so the whole string is returned (no bare filename).
	CHECK(fileNameFromUri("http://example.com/") == "http://example.com/");
	CHECK(fileNameFromUri("rtsp://host/path/stream") == "stream");
}

// ---------------------------------------------------------------------------
// nearlyEqual tests
// ---------------------------------------------------------------------------

static void testNearlyEqual() {
	using ofxVlc4Utils::nearlyEqual;

	beginSuite("nearlyEqual");
	CHECK(nearlyEqual(1.0f, 1.0f));
	CHECK(nearlyEqual(1.0f, 1.00005f));
	CHECK(!nearlyEqual(1.0f, 1.01f));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testIsUri();
	testTrimWhitespace();
	testSanitizeFileStem();
	testFilterChain();
	testFileNameFromUri();
	testNearlyEqual();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
