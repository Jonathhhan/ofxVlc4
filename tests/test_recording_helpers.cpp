// Tests for recording path-building helpers extracted into
// ofxVlc4RecordingHelpers.h.  Covers trimRecorderText, buildRecordingOutputStem,
// buildRecordingOutputPath, and buildRecordingOutputPaths.

#include <cassert>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Stubs for OF functions used by ofxVlc4RecordingHelpers.h
// ---------------------------------------------------------------------------

namespace ofFilePath {

inline std::string getFileExt(const std::string & path) {
	const auto dotPos = path.find_last_of('.');
	const auto slashPos = path.find_last_of("/\\");
	if (dotPos == std::string::npos) return "";
	if (slashPos != std::string::npos && dotPos < slashPos) return "";
	return path.substr(dotPos + 1);
}

inline std::string removeExt(const std::string & path) {
	const auto dotPos = path.find_last_of('.');
	const auto slashPos = path.find_last_of("/\\");
	if (dotPos == std::string::npos) return path;
	if (slashPos != std::string::npos && dotPos < slashPos) return path;
	return path.substr(0, dotPos);
}

} // namespace ofFilePath

// Controllable timestamp stub: returns a fixed suffix for deterministic tests.
static std::string g_stubTimestamp = "-2026-01-15-10-30-00";

inline std::string ofGetTimestampString(const std::string & /* fmt */) {
	return g_stubTimestamp;
}

// ---------------------------------------------------------------------------
// Now include the header under test.
// ---------------------------------------------------------------------------

#include "ofxVlc4RecordingHelpers.h"

using namespace ofxVlc4RecordingHelpers;

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
// trimRecorderText
// ---------------------------------------------------------------------------

static void testTrimRecorderText() {
	beginSuite("trimRecorderText");

	CHECK_EQ(trimRecorderText("hello"), std::string("hello"));
	CHECK_EQ(trimRecorderText("  hello  "), std::string("hello"));
	CHECK_EQ(trimRecorderText("\t\nhello\r\n"), std::string("hello"));
	CHECK_EQ(trimRecorderText("  "), std::string(""));
	CHECK_EQ(trimRecorderText(""), std::string(""));
	CHECK_EQ(trimRecorderText("a"), std::string("a"));
	CHECK_EQ(trimRecorderText("  hello world  "), std::string("hello world"));
	CHECK_EQ(trimRecorderText("\thello\tworld\n"), std::string("hello\tworld"));
}

// ---------------------------------------------------------------------------
// buildRecordingOutputStem
// ---------------------------------------------------------------------------

static void testBuildRecordingOutputStem() {
	beginSuite("buildRecordingOutputStem");

	// Normal name without extension.
	{
		std::string ext;
		const std::string stem = buildRecordingOutputStem("myrecording", &ext);
		CHECK_EQ(stem, std::string("myrecording" + g_stubTimestamp));
		CHECK_EQ(ext, std::string(""));
	}

	// Name with extension — extension is stripped from stem.
	{
		std::string ext;
		const std::string stem = buildRecordingOutputStem("myrecording.mp4", &ext);
		CHECK_EQ(stem, std::string("myrecording" + g_stubTimestamp));
		CHECK_EQ(ext, std::string("mp4"));
	}

	// Whitespace-only name — defaults to "recording".
	{
		std::string ext;
		const std::string stem = buildRecordingOutputStem("   ", &ext);
		CHECK_EQ(stem, std::string("recording" + g_stubTimestamp));
		CHECK_EQ(ext, std::string(""));
	}

	// Empty name — defaults to "recording".
	{
		std::string ext;
		const std::string stem = buildRecordingOutputStem("", &ext);
		CHECK_EQ(stem, std::string("recording" + g_stubTimestamp));
		CHECK_EQ(ext, std::string(""));
	}

	// Name with multiple dots — only last extension is extracted.
	{
		std::string ext;
		const std::string stem = buildRecordingOutputStem("my.file.mkv", &ext);
		CHECK_EQ(ext, std::string("mkv"));
		CHECK_EQ(stem, std::string("my.file" + g_stubTimestamp));
	}

	// Null extensionOut pointer — doesn't crash.
	{
		const std::string stem = buildRecordingOutputStem("test.wav", nullptr);
		CHECK_EQ(stem, std::string("test" + g_stubTimestamp));
	}

	// Name with leading/trailing whitespace and extension.
	{
		std::string ext;
		const std::string stem = buildRecordingOutputStem("  capture.ts  ", &ext);
		CHECK_EQ(ext, std::string("ts"));
		CHECK_EQ(stem, std::string("capture" + g_stubTimestamp));
	}
}

// ---------------------------------------------------------------------------
// buildRecordingOutputPath
// ---------------------------------------------------------------------------

static void testBuildRecordingOutputPath() {
	beginSuite("buildRecordingOutputPath");

	// Name with extension — uses detected extension.
	{
		const std::string path = buildRecordingOutputPath("video.mkv", ".ts");
		CHECK_EQ(path, std::string("video" + g_stubTimestamp + ".mkv"));
	}

	// Name without extension — uses fallback.
	{
		const std::string path = buildRecordingOutputPath("video", ".ts");
		CHECK_EQ(path, std::string("video" + g_stubTimestamp + ".ts"));
	}

	// Empty name — defaults to "recording" + fallback extension.
	{
		const std::string path = buildRecordingOutputPath("", ".mp4");
		CHECK_EQ(path, std::string("recording" + g_stubTimestamp + ".mp4"));
	}

	// Name with extension and different fallback — detected wins.
	{
		const std::string path = buildRecordingOutputPath("output.webm", ".ts");
		CHECK_EQ(path, std::string("output" + g_stubTimestamp + ".webm"));
	}
}

// ---------------------------------------------------------------------------
// buildRecordingOutputPaths
// ---------------------------------------------------------------------------

static void testBuildRecordingOutputPaths() {
	beginSuite("buildRecordingOutputPaths");

	// Default fallback extension (.ts).
	{
		const auto paths = buildRecordingOutputPaths("myrecording");
		CHECK_EQ(paths.audioPath, std::string("myrecording" + g_stubTimestamp + ".wav"));
		CHECK_EQ(paths.videoPath, std::string("myrecording" + g_stubTimestamp + ".ts"));
	}

	// Custom fallback extension.
	{
		const auto paths = buildRecordingOutputPaths("capture", ".mp4");
		CHECK_EQ(paths.audioPath, std::string("capture" + g_stubTimestamp + ".wav"));
		CHECK_EQ(paths.videoPath, std::string("capture" + g_stubTimestamp + ".mp4"));
	}

	// Empty name.
	{
		const auto paths = buildRecordingOutputPaths("");
		CHECK_EQ(paths.audioPath, std::string("recording" + g_stubTimestamp + ".wav"));
		CHECK_EQ(paths.videoPath, std::string("recording" + g_stubTimestamp + ".ts"));
	}

	// Audio always uses .wav regardless of input extension.
	{
		const auto paths = buildRecordingOutputPaths("output.mkv");
		CHECK(paths.audioPath.find(".wav") != std::string::npos);
	}
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testTrimRecorderText();
	testBuildRecordingOutputStem();
	testBuildRecordingOutputPath();
	testBuildRecordingOutputPaths();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
