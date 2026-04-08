// Tests for the pure string / math helper functions in ofxVlc4Utils.h.
// These functions have no dependencies on OF, GLFW, or VLC at runtime;
// the stubs in tests/stubs/ satisfy the header-level includes.

#include "ofxVlc4Utils.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors test_ringbuffer.cpp)
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
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// trimWhitespace
// ---------------------------------------------------------------------------

static void testTrimWhitespace() {
	beginSuite("trimWhitespace");

	using ofxVlc4Utils::trimWhitespace;

	CHECK_EQ(trimWhitespace(""), "");
	CHECK_EQ(trimWhitespace("   "), "");
	CHECK_EQ(trimWhitespace("hello"), "hello");
	CHECK_EQ(trimWhitespace("  hello  "), "hello");
	CHECK_EQ(trimWhitespace("\thello\t"), "hello");
	CHECK_EQ(trimWhitespace("\r\nhello\r\n"), "hello");
	CHECK_EQ(trimWhitespace("  hello world  "), "hello world");
	CHECK_EQ(trimWhitespace("a"), "a");
	CHECK_EQ(trimWhitespace(" a "), "a");
}

// ---------------------------------------------------------------------------
// isUri
// ---------------------------------------------------------------------------

static void testIsUri() {
	beginSuite("isUri");

	using ofxVlc4Utils::isUri;

	CHECK(isUri("http://example.com"));
	CHECK(isUri("https://example.com/path?q=1"));
	CHECK(isUri("rtsp://camera/stream"));
	CHECK(isUri("file:///home/user/video.mp4"));
	CHECK(!isUri(""));
	CHECK(!isUri("/home/user/video.mp4"));
	CHECK(!isUri("relative/path.mp4"));
	CHECK(!isUri("justword"));
	// Edge: two occurrences of "://" → not considered a single URI
	CHECK(!isUri("http://host/a://b"));
}

// ---------------------------------------------------------------------------
// fileNameFromUri
// ---------------------------------------------------------------------------

static void testFileNameFromUri() {
	beginSuite("fileNameFromUri");

	using ofxVlc4Utils::fileNameFromUri;

	CHECK_EQ(fileNameFromUri("http://example.com/video.mp4"), "video.mp4");
	CHECK_EQ(fileNameFromUri("http://example.com/path/to/file.mkv"), "file.mkv");
	// Query string stripped
	CHECK_EQ(fileNameFromUri("http://example.com/video.mp4?token=abc"), "video.mp4");
	// Fragment stripped
	CHECK_EQ(fileNameFromUri("http://example.com/video.mp4#00:01:00"), "video.mp4");
	// URI ending in slash → nothing after the slash; function returns the
	// whole URI (without query) as a fallback.
	CHECK_EQ(fileNameFromUri("http://example.com/"), "http://example.com/");
	// No path component
	CHECK_EQ(fileNameFromUri("http://example.com"), "example.com");
}

// ---------------------------------------------------------------------------
// sanitizeFileStem
// ---------------------------------------------------------------------------

static void testSanitizeFileStem() {
	beginSuite("sanitizeFileStem");

	using ofxVlc4Utils::sanitizeFileStem;

	CHECK_EQ(sanitizeFileStem(""), "snapshot");
	CHECK_EQ(sanitizeFileStem("   "), "snapshot");
	CHECK_EQ(sanitizeFileStem("video"), "video");
	CHECK_EQ(sanitizeFileStem("my<video>"), "my_video_");
	CHECK_EQ(sanitizeFileStem("path/to:file"), "path_to_file");
	CHECK_EQ(sanitizeFileStem("file|name"), "file_name");
	CHECK_EQ(sanitizeFileStem("file?name"), "file_name");
	CHECK_EQ(sanitizeFileStem("file*name"), "file_name");
	CHECK_EQ(sanitizeFileStem("file\\name"), "file_name");
	CHECK_EQ(sanitizeFileStem("\"quoted\""), "_quoted_");
}

// ---------------------------------------------------------------------------
// parseFilterChainEntries / joinFilterChainEntries
// ---------------------------------------------------------------------------

static void testParseJoinFilterChain() {
	beginSuite("parseFilterChainEntries / joinFilterChainEntries");

	using ofxVlc4Utils::parseFilterChainEntries;
	using ofxVlc4Utils::joinFilterChainEntries;

	// Empty string → empty vector
	{
		const auto v = parseFilterChainEntries("");
		CHECK(v.empty());
	}

	// Single entry
	{
		const auto v = parseFilterChainEntries("equalizer");
		CHECK(v.size() == 1u);
		CHECK_EQ(v[0], "equalizer");
	}

	// Two entries separated by colon
	{
		const auto v = parseFilterChainEntries("equalizer:compressor");
		CHECK(v.size() == 2u);
		CHECK_EQ(v[0], "equalizer");
		CHECK_EQ(v[1], "compressor");
	}

	// Duplicate entries are removed
	{
		const auto v = parseFilterChainEntries("equalizer:compressor:equalizer");
		CHECK(v.size() == 2u);
		CHECK_EQ(v[0], "equalizer");
		CHECK_EQ(v[1], "compressor");
	}

	// Whitespace-padded entries are trimmed
	{
		const auto v = parseFilterChainEntries("  equalizer  :  compressor  ");
		CHECK(v.size() == 2u);
		CHECK_EQ(v[0], "equalizer");
		CHECK_EQ(v[1], "compressor");
	}

	// Empty colon-delimited segments are skipped
	{
		const auto v = parseFilterChainEntries(":equalizer:");
		CHECK(v.size() == 1u);
		CHECK_EQ(v[0], "equalizer");
	}

	// Round-trip: parse then join
	{
		const std::string chain = "equalizer:compressor:reverb";
		const auto v = parseFilterChainEntries(chain);
		const std::string rejoined = joinFilterChainEntries(v);
		CHECK_EQ(rejoined, chain);
	}

	// joinFilterChainEntries handles empty entries gracefully
	{
		const std::vector<std::string> v = { "equalizer", "", "  ", "compressor" };
		const std::string joined = joinFilterChainEntries(v);
		CHECK_EQ(joined, "equalizer:compressor");
	}
}

// ---------------------------------------------------------------------------
// nearlyEqual
// ---------------------------------------------------------------------------

static void testNearlyEqual() {
	beginSuite("nearlyEqual");

	using ofxVlc4Utils::nearlyEqual;

	CHECK(nearlyEqual(0.0f, 0.0f));
	CHECK(nearlyEqual(1.0f, 1.0f));
	CHECK(nearlyEqual(1.0f, 1.00009f));
	CHECK(!nearlyEqual(1.0f, 1.001f));
	CHECK(nearlyEqual(-1.0f, -1.0f));
	CHECK(!nearlyEqual(0.0f, 0.001f));
}

// ---------------------------------------------------------------------------
// formatAdjustmentValue
// ---------------------------------------------------------------------------

static void testFormatAdjustmentValue() {
	beginSuite("formatAdjustmentValue");

	using ofxVlc4Utils::formatAdjustmentValue;

	CHECK_EQ(formatAdjustmentValue(1.0f, 1), "1.0");
	CHECK_EQ(formatAdjustmentValue(1.5f, 1), "1.5");
	CHECK_EQ(formatAdjustmentValue(100.0f, 0), "100");
	CHECK_EQ(formatAdjustmentValue(1.0f, 1, "%"), "1.0%");
	CHECK_EQ(formatAdjustmentValue(0.0f, 2, " dB"), "0.00 dB");
}

// ---------------------------------------------------------------------------
// fallbackIndexedLabel
// ---------------------------------------------------------------------------

static void testFallbackIndexedLabel() {
	beginSuite("fallbackIndexedLabel");

	using ofxVlc4Utils::fallbackIndexedLabel;

	// Non-empty name → return name as-is (trimmed)
	CHECK_EQ(fallbackIndexedLabel("Track", 0, "My Track"), "My Track");
	CHECK_EQ(fallbackIndexedLabel("Track", 0, "  My Track  "), "My Track");

	// Empty / whitespace name → synthesize from prefix + 1-based index
	CHECK_EQ(fallbackIndexedLabel("Track", 0, ""), "Track 1");
	CHECK_EQ(fallbackIndexedLabel("Track", 0, "  "), "Track 1");
	CHECK_EQ(fallbackIndexedLabel("Channel", 4, ""), "Channel 5");
}

// ---------------------------------------------------------------------------
// formatProgramName
// ---------------------------------------------------------------------------

static void testFormatProgramName() {
	beginSuite("formatProgramName");

	using ofxVlc4Utils::formatProgramName;

	CHECK_EQ(formatProgramName(1, "My Program"), "My Program");
	CHECK_EQ(formatProgramName(1, "  My Program  "), "My Program");
	CHECK_EQ(formatProgramName(1, ""), "Program 1");
	CHECK_EQ(formatProgramName(7, "  "), "Program 7");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testTrimWhitespace();
	testIsUri();
	testFileNameFromUri();
	testSanitizeFileStem();
	testParseJoinFilterChain();
	testNearlyEqual();
	testFormatAdjustmentValue();
	testFallbackIndexedLabel();
	testFormatProgramName();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
