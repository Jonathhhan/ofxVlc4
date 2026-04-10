// Tests for the pure string / math helper functions in ofxVlc4Utils.h.
// These functions have no dependencies on OF, GLFW, or VLC at runtime;
// the stubs in tests/stubs/ satisfy the header-level includes.

#include "ofxVlc4Utils.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
	// Edge: two occurrences of "://" — first scheme (http) is still valid
	CHECK(isUri("http://host/a://b"));
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
	// URI ending in slash → no filename after the slash; return empty.
	CHECK_EQ(fileNameFromUri("http://example.com/"), "");
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
// normalizeOptionalPath
// ---------------------------------------------------------------------------

static void testNormalizeOptionalPath() {
	beginSuite("normalizeOptionalPath");

	using ofxVlc4Utils::normalizeOptionalPath;

	// Empty string → returns empty string unchanged.
	CHECK_EQ(normalizeOptionalPath(""), "");

	// Whitespace-only → trimmed to empty, returned as empty.
	CHECK_EQ(normalizeOptionalPath("   "), "");

	// URI → returned trimmed, not resolved as a path.
	CHECK_EQ(normalizeOptionalPath("http://example.com/video.mp4"), "http://example.com/video.mp4");
	CHECK_EQ(normalizeOptionalPath("  rtsp://camera/stream  "), "rtsp://camera/stream");

	// Local path → returned as an absolute path (non-empty, not a URI).
	{
		const std::string result = normalizeOptionalPath("video.mp4");
		CHECK(!result.empty());
		CHECK(!ofxVlc4Utils::isUri(result));
	}
	{
		const std::string result = normalizeOptionalPath("/tmp/video.mp4");
		CHECK_EQ(result, "/tmp/video.mp4");
	}
}

// ---------------------------------------------------------------------------
// mediaLabelForPath
// ---------------------------------------------------------------------------

static void testMediaLabelForPath() {
	beginSuite("mediaLabelForPath");

	using ofxVlc4Utils::mediaLabelForPath;

	// Empty path → empty label.
	CHECK_EQ(mediaLabelForPath(""), "");

	// URI → extracts filename from URI.
	CHECK_EQ(mediaLabelForPath("http://example.com/video.mp4"), "video.mp4");
	CHECK_EQ(mediaLabelForPath("rtsp://camera/stream.ts"), "stream.ts");

	// URI with query → strips query, extracts filename.
	CHECK_EQ(mediaLabelForPath("http://example.com/video.mp4?token=abc"), "video.mp4");

	// URI ending in slash → returns full URI as fallback.
	CHECK_EQ(mediaLabelForPath("http://example.com/"), "http://example.com/");

	// Local path → extracts filename.
	CHECK_EQ(mediaLabelForPath("/home/user/video.mp4"), "video.mp4");
	CHECK_EQ(mediaLabelForPath("video.mp4"), "video.mp4");
	CHECK_EQ(mediaLabelForPath("/path/to/my-song.wav"), "my-song.wav");
}

// ---------------------------------------------------------------------------
// isStoppedOrIdleState
// ---------------------------------------------------------------------------

static void testIsStoppedOrIdleState() {
	beginSuite("isStoppedOrIdleState");

	using ofxVlc4Utils::isStoppedOrIdleState;

	CHECK(isStoppedOrIdleState(libvlc_Stopped));
	CHECK(isStoppedOrIdleState(libvlc_NothingSpecial));
	CHECK(!isStoppedOrIdleState(libvlc_Playing));
	CHECK(!isStoppedOrIdleState(libvlc_Paused));
	CHECK(!isStoppedOrIdleState(libvlc_Opening));
	CHECK(!isStoppedOrIdleState(libvlc_Buffering));
	CHECK(!isStoppedOrIdleState(libvlc_Error));
	CHECK(!isStoppedOrIdleState(libvlc_Stopping));
}

// ---------------------------------------------------------------------------
// isTransientPlaybackState
// ---------------------------------------------------------------------------

static void testIsTransientPlaybackState() {
	beginSuite("isTransientPlaybackState");

	using ofxVlc4Utils::isTransientPlaybackState;

	CHECK(isTransientPlaybackState(libvlc_Opening));
	CHECK(isTransientPlaybackState(libvlc_Buffering));
	CHECK(isTransientPlaybackState(libvlc_Stopping));
	CHECK(!isTransientPlaybackState(libvlc_Playing));
	CHECK(!isTransientPlaybackState(libvlc_Paused));
	CHECK(!isTransientPlaybackState(libvlc_Stopped));
	CHECK(!isTransientPlaybackState(libvlc_NothingSpecial));
	CHECK(!isTransientPlaybackState(libvlc_Error));
}

// ---------------------------------------------------------------------------
// setInputHandlingEnabled
// ---------------------------------------------------------------------------

static void testSetInputHandlingEnabled() {
	beginSuite("setInputHandlingEnabled");

	using ofxVlc4Utils::setInputHandlingEnabled;

	// Case 1: value changes from false to true → returns true.
	{
		bool currentValue = false;
		unsigned lastApplied = 99;
		std::string lastLog;

		auto apply = [](libvlc_media_player_t *, unsigned v) {
			// Would apply to real media player; no-op in test.
			(void)v;
		};

		const bool changed = setInputHandlingEnabled(
			nullptr, currentValue, true, "Keyboard handling ",
			apply,
			[&](const std::string & msg) { lastLog = msg; });

		CHECK(changed);
		CHECK(currentValue == true);
		CHECK(lastLog.find("enabled") != std::string::npos);
	}

	// Case 2: value is already the same → returns false (no-op).
	{
		bool currentValue = true;
		std::string lastLog;

		auto apply = [](libvlc_media_player_t *, unsigned) {};

		const bool changed = setInputHandlingEnabled(
			nullptr, currentValue, true, "Keyboard handling ",
			apply,
			[&](const std::string & msg) { lastLog = msg; });

		CHECK(!changed);
		CHECK(lastLog.empty()); // log not called
	}

	// Case 3: value changes from true to false → returns true, log says "disabled".
	{
		bool currentValue = true;
		std::string lastLog;

		auto apply = [](libvlc_media_player_t *, unsigned) {};

		const bool changed = setInputHandlingEnabled(
			nullptr, currentValue, false, "Mouse handling ",
			apply,
			[&](const std::string & msg) { lastLog = msg; });

		CHECK(changed);
		CHECK(currentValue == false);
		CHECK(lastLog.find("disabled") != std::string::npos);
	}
}

// ---------------------------------------------------------------------------
// readTextFileIfPresent
// ---------------------------------------------------------------------------

static void testReadTextFileIfPresent() {
	beginSuite("readTextFileIfPresent");

	using ofxVlc4Utils::readTextFileIfPresent;

	// Non-existent file → empty string.
	CHECK_EQ(readTextFileIfPresent((std::filesystem::temp_directory_path() / "ofxvlc4_nonexistent_file_12345.txt").string()), "");

	// Write a test file, read it back.
	{
		const std::string path = (std::filesystem::temp_directory_path() / "ofxvlc4_test_readtext.txt").string();
		{
			std::ofstream f(path);
			f << "Hello, World!";
		}
		const std::string content = readTextFileIfPresent(path);
		CHECK_EQ(content, "Hello, World!");
		std::remove(path.c_str());
	}

	// Empty file → empty string.
	{
		const std::string path = (std::filesystem::temp_directory_path() / "ofxvlc4_test_readtext_empty.txt").string();
		{
			std::ofstream f(path);
			// write nothing
		}
		const std::string content = readTextFileIfPresent(path);
		CHECK_EQ(content, "");
		std::remove(path.c_str());
	}

	// Multiline content.
	{
		const std::string path = (std::filesystem::temp_directory_path() / "ofxvlc4_test_readtext_multi.txt").string();
		{
			std::ofstream f(path);
			f << "Line1\nLine2\nLine3";
		}
		const std::string content = readTextFileIfPresent(path);
		CHECK_EQ(content, "Line1\nLine2\nLine3");
		std::remove(path.c_str());
	}
}

// ---------------------------------------------------------------------------
// isUri: additional edge cases
// ---------------------------------------------------------------------------

static void testIsUriEdgeCases() {
	beginSuite("isUri: edge cases");

	using ofxVlc4Utils::isUri;

	// "://" at position 0 (scheme is empty) → not a URI
	CHECK(!isUri("://host/path"));

	// Scheme with digits (h264://...) → valid
	CHECK(isUri("h264://example.com"));

	// Scheme with plus/dash/dot (custom+scheme-1.0://...) → valid per RFC 3986
	CHECK(isUri("custom+scheme-1.0://example.com"));

	// Scheme starting with a digit → invalid
	CHECK(!isUri("3com://host"));

	// Path separator before "://" → not a URI (local path containing "://")
	CHECK(!isUri("/path/to/file://something"));

	// Scheme with underscore → invalid (underscore not allowed in schemes)
	CHECK(!isUri("my_scheme://host"));

	// Just "://" → not a URI (empty scheme)
	CHECK(!isUri("://"));

	// Single letter scheme → valid
	CHECK(isUri("x://host"));
}

// ---------------------------------------------------------------------------
// fileNameFromUri: additional edge cases
// ---------------------------------------------------------------------------

static void testFileNameFromUriEdgeCases() {
	beginSuite("fileNameFromUri: edge cases");

	using ofxVlc4Utils::fileNameFromUri;

	// Empty string → returns empty.
	CHECK_EQ(fileNameFromUri(""), "");

	// No slash at all → returns the whole string.
	CHECK_EQ(fileNameFromUri("file"), "file");

	// Fragment and query combined.
	CHECK_EQ(fileNameFromUri("http://example.com/video.mp4?a=1#frag"), "video.mp4");

	// Only fragment, no query.
	CHECK_EQ(fileNameFromUri("http://example.com/video.mp4#t=10"), "video.mp4");

	// Deeply nested path.
	CHECK_EQ(fileNameFromUri("http://a.com/1/2/3/4/deep.ts"), "deep.ts");

	// Trailing slash → no filename → returns empty.
	CHECK_EQ(fileNameFromUri("http://example.com/path/"), "");
	CHECK_EQ(fileNameFromUri("/"), "");
}

// ---------------------------------------------------------------------------
// sanitizeFileStem: additional edge cases
// ---------------------------------------------------------------------------

static void testSanitizeFileStemEdgeCases() {
	beginSuite("sanitizeFileStem: edge cases");

	using ofxVlc4Utils::sanitizeFileStem;

	// All forbidden characters → all replaced, result is non-empty.
	CHECK_EQ(sanitizeFileStem("<>:\"/\\|?*"), "_________");

	// Mixed valid and forbidden.
	CHECK_EQ(sanitizeFileStem("my:video<1>"), "my_video_1_");

	// Already clean → unchanged.
	CHECK_EQ(sanitizeFileStem("clean_name"), "clean_name");

	// All whitespace after sanitization → "snapshot".
	CHECK_EQ(sanitizeFileStem("\t\r\n"), "snapshot");
}

// ---------------------------------------------------------------------------
// nearlyEqual: custom epsilon
// ---------------------------------------------------------------------------

static void testNearlyEqualCustomEpsilon() {
	beginSuite("nearlyEqual: custom epsilon");

	using ofxVlc4Utils::nearlyEqual;

	// With a large epsilon, values further apart should match.
	CHECK(nearlyEqual(1.0f, 1.5f, 1.0f));
	CHECK(!nearlyEqual(1.0f, 3.0f, 1.0f));

	// Very small epsilon.
	CHECK(!nearlyEqual(1.0f, 1.00001f, 0.000001f));
	CHECK(nearlyEqual(1.0f, 1.0000001f, 0.000001f));

	// Negative values with custom epsilon.
	CHECK(nearlyEqual(-1.0f, -1.05f, 0.1f));
	CHECK(!nearlyEqual(-1.0f, -1.15f, 0.1f));
}

// ---------------------------------------------------------------------------
// formatAdjustmentValue: edge cases
// ---------------------------------------------------------------------------

static void testFormatAdjustmentValueEdgeCases() {
	beginSuite("formatAdjustmentValue: edge cases");

	using ofxVlc4Utils::formatAdjustmentValue;

	// Null suffix pointer → no suffix appended.
	CHECK_EQ(formatAdjustmentValue(1.0f, 1, nullptr), "1.0");

	// Empty string suffix → no suffix appended.
	CHECK_EQ(formatAdjustmentValue(1.0f, 1, ""), "1.0");

	// Negative values.
	CHECK_EQ(formatAdjustmentValue(-3.5f, 1), "-3.5");

	// High precision.
	CHECK_EQ(formatAdjustmentValue(1.123456f, 4), "1.1235");

	// Zero precision.
	CHECK_EQ(formatAdjustmentValue(42.7f, 0), "43");
}

// ---------------------------------------------------------------------------
// isUri: file:// scheme
// ---------------------------------------------------------------------------

static void testIsUriFileScheme() {
	beginSuite("isUri: file:// scheme");

	using ofxVlc4Utils::isUri;

	CHECK(isUri("file:///home/user/video.mp4"));
	CHECK(isUri("file:///tmp/test.ts"));
	CHECK(isUri("file:///C:/Users/video.mp4"));
}

// ---------------------------------------------------------------------------
// nearlyEqual: special float values
// ---------------------------------------------------------------------------

static void testNearlyEqualSpecialValues() {
	beginSuite("nearlyEqual: special float values");

	using ofxVlc4Utils::nearlyEqual;

	// Both zero.
	CHECK(nearlyEqual(0.0f, 0.0f));

	// Positive and negative zero.
	CHECK(nearlyEqual(0.0f, -0.0f));

	// Very small positive values.
	CHECK(nearlyEqual(0.00001f, 0.00002f, 0.0001f));

	// Very large values that differ by a small absolute amount.
	CHECK(nearlyEqual(1e6f, 1e6f + 0.00001f));
}

// ---------------------------------------------------------------------------
// formatAdjustmentValue: very large and very small values
// ---------------------------------------------------------------------------

static void testFormatAdjustmentValueLargeValues() {
	beginSuite("formatAdjustmentValue: large/small values");

	using ofxVlc4Utils::formatAdjustmentValue;

	// Very large value.
	const std::string large = formatAdjustmentValue(99999.0f, 0);
	CHECK_EQ(large, "99999");

	// Very small value.
	const std::string small = formatAdjustmentValue(0.001f, 3);
	CHECK_EQ(small, "0.001");
}

// ---------------------------------------------------------------------------
// readTextFileIfPresent: binary content
// ---------------------------------------------------------------------------

static void testReadTextFileIfPresentBinaryContent() {
	beginSuite("readTextFileIfPresent: binary content");

	using ofxVlc4Utils::readTextFileIfPresent;

	const std::string path = (std::filesystem::temp_directory_path() / "ofxvlc4_test_binary.bin").string();
	{
		std::ofstream f(path, std::ios::binary);
		char data[] = { 'A', 'B', '\0', 'C', 'D' };
		f.write(data, sizeof(data));
	}
	const std::string content = readTextFileIfPresent(path);
	// Binary read should at least return something non-empty.
	CHECK(!content.empty());
	// First two characters should be 'A' and 'B'.
	CHECK(content.size() >= 2);
	CHECK_EQ(content[0], 'A');
	CHECK_EQ(content[1], 'B');
	std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// joinFilterChainEntries: single entry
// ---------------------------------------------------------------------------

static void testJoinFilterChainEntriesSingle() {
	beginSuite("joinFilterChainEntries: single entry");

	using ofxVlc4Utils::joinFilterChainEntries;

	const std::vector<std::string> v = { "equalizer" };
	const std::string joined = joinFilterChainEntries(v);
	CHECK_EQ(joined, "equalizer");
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
	testNormalizeOptionalPath();
	testMediaLabelForPath();
	testIsStoppedOrIdleState();
	testIsTransientPlaybackState();
	testSetInputHandlingEnabled();
	testReadTextFileIfPresent();
	testIsUriEdgeCases();
	testFileNameFromUriEdgeCases();
	testSanitizeFileStemEdgeCases();
	testNearlyEqualCustomEpsilon();
	testFormatAdjustmentValueEdgeCases();

	// Additional edge case tests.
	testIsUriFileScheme();
	testNearlyEqualSpecialValues();
	testFormatAdjustmentValueLargeValues();
	testReadTextFileIfPresentBinaryContent();
	testJoinFilterChainEntriesSingle();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
