// Tests for the pure filesystem/string helpers in ofxVlc4MuxHelpers.h.
// These functions have no dependency on OF, GLFW, or VLC.

#include "ofxVlc4MuxHelpers.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors test_utils.cpp)
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

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// normalizeSoutPath
// ---------------------------------------------------------------------------

static void testNormalizeSoutPath() {
	beginSuite("normalizeSoutPath");

	using ofxVlc4MuxHelpers::normalizeSoutPath;

	// No single-quotes → path is just normalised.
	{
		const std::string result = normalizeSoutPath("/tmp/video.ts");
		CHECK(result.find('\'') == std::string::npos);
	}

	// Single-quotes are escaped with a leading backslash.
	{
		const std::string result = normalizeSoutPath("/tmp/my'video.ts");
		CHECK(result.find("\\'") != std::string::npos);
		CHECK(result.find('\'') != std::string::npos);
	}

	// Multiple single-quotes are all escaped.
	{
		const std::string path = "/tmp/a'b'c.ts";
		const std::string result = normalizeSoutPath(path);
		size_t pos = 0;
		int escapedCount = 0;
		while ((pos = result.find("\\'", pos)) != std::string::npos) {
			++escapedCount;
			pos += 2;
		}
		CHECK(escapedCount == 2);
	}

	// Redundant path separators / dot-segments are normalised away.
	{
		const std::string result = normalizeSoutPath("/tmp/../tmp/./video.ts");
		CHECK(result == "/tmp/video.ts");
	}
}

// ---------------------------------------------------------------------------
// pathToFileUri
// ---------------------------------------------------------------------------

static void testPathToFileUri() {
	beginSuite("pathToFileUri");

	using ofxVlc4MuxHelpers::pathToFileUri;

	// Exact URI format: exactly three slashes, no double-slash before the path.
	{
		const std::string uri = pathToFileUri("/tmp/video.ts");
		CHECK_EQ(uri, "file:///tmp/video.ts");
	}

	// Result always starts with "file:///".
	{
		const std::string uri = pathToFileUri("/tmp/video.ts");
		CHECK(uri.substr(0, 8) == "file:///");
		// Must not have a fourth slash (was a previous bug on Unix).
		CHECK(uri.size() < 9 || uri[8] != '/');
	}

	// Plain ASCII path segments pass through unencoded.
	{
		const std::string uri = pathToFileUri("/tmp/video.ts");
		CHECK(uri.find("video.ts") != std::string::npos);
	}

	// Spaces are percent-encoded.
	{
		const std::string uri = pathToFileUri("/tmp/my video.ts");
		CHECK(uri.find("%20") != std::string::npos);
		CHECK(uri.find(' ') == std::string::npos);
	}

	// Single-quotes are percent-encoded.
	{
		const std::string uri = pathToFileUri("/tmp/my'video.ts");
		CHECK(uri.find("%27") != std::string::npos);
		CHECK(uri.find('\'') == std::string::npos);
	}
}

// ---------------------------------------------------------------------------
// waitForRecordingFile (no-cancel overload)
// ---------------------------------------------------------------------------

static void testWaitForRecordingFileNoCancel() {
	beginSuite("waitForRecordingFile (no cancel)");

	using ofxVlc4MuxHelpers::waitForRecordingFile;

	const std::string tmpPath = (std::filesystem::temp_directory_path() / "ofxvlc4_test_wait.bin").string();
	std::filesystem::remove(tmpPath);

	// File doesn't exist → should time out quickly and return false.
	{
		const bool result = waitForRecordingFile(tmpPath, 150);
		CHECK(!result);
	}

	// Write a stable file in a background thread, then wait for it.
	{
		std::filesystem::remove(tmpPath);
		std::thread writer([&] {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			std::ofstream f(tmpPath, std::ios::binary);
			f.write("data", 4);
			f.close();
		});

		const bool result = waitForRecordingFile(tmpPath, 2000);
		writer.join();
		CHECK(result);
		std::filesystem::remove(tmpPath);
	}
}

// ---------------------------------------------------------------------------
// waitForRecordingFile (cancel overload)
// ---------------------------------------------------------------------------

static void testWaitForRecordingFileCancelFlag() {
	beginSuite("waitForRecordingFile (cancel flag)");

	using ofxVlc4MuxHelpers::waitForRecordingFile;

	const std::string tmpPath = (std::filesystem::temp_directory_path() / "ofxvlc4_test_cancel.bin").string();
	std::filesystem::remove(tmpPath);

	// File doesn't exist, cancel is already set → returns false immediately.
	{
		std::atomic<bool> cancel { true };
		const bool result = waitForRecordingFile(tmpPath, 2000, &cancel);
		CHECK(!result);
	}

	// File exists and is stable, cancel is not set → returns true.
	{
		{
			std::ofstream f(tmpPath, std::ios::binary);
			f.write("data", 4);
		}
		std::atomic<bool> cancel { false };
		const bool result = waitForRecordingFile(tmpPath, 2000, &cancel);
		CHECK(result);
		std::filesystem::remove(tmpPath);
	}

	// File doesn't exist, cancel fires mid-wait → returns false.
	{
		std::atomic<bool> cancel { false };
		std::thread canceller([&] {
			std::this_thread::sleep_for(std::chrono::milliseconds(80));
			cancel.store(true, std::memory_order_release);
		});
		const bool result = waitForRecordingFile(tmpPath, 2000, &cancel);
		canceller.join();
		CHECK(!result);
	}
}

// ---------------------------------------------------------------------------
// removeRecordingFile
// ---------------------------------------------------------------------------

static void testRemoveRecordingFile() {
	beginSuite("removeRecordingFile");

	using ofxVlc4MuxHelpers::removeRecordingFile;

	const std::string tmpPath = (std::filesystem::temp_directory_path() / "ofxvlc4_test_remove.bin").string();

	// Empty path → always returns true without touching the filesystem.
	CHECK(removeRecordingFile("", 500));

	// File doesn't exist → returns true (already gone).
	std::filesystem::remove(tmpPath);
	CHECK(removeRecordingFile(tmpPath, 500));

	// File exists → removes it and returns true.
	{
		std::ofstream f(tmpPath, std::ios::binary);
		f.write("x", 1);
	}
	CHECK(removeRecordingFile(tmpPath, 500));
	CHECK(!std::filesystem::exists(tmpPath));
}

// ---------------------------------------------------------------------------
// tryRemoveRecordingFileOnce
// ---------------------------------------------------------------------------

static void testTryRemoveRecordingFileOnce() {
	beginSuite("tryRemoveRecordingFileOnce");

	using ofxVlc4MuxHelpers::tryRemoveRecordingFileOnce;

	const std::string tmpPath = (std::filesystem::temp_directory_path() / "ofxvlc4_test_tryremove.bin").string();

	// Empty path → returns true.
	CHECK(tryRemoveRecordingFileOnce(""));

	// File doesn't exist → returns true.
	std::filesystem::remove(tmpPath);
	CHECK(tryRemoveRecordingFileOnce(tmpPath));

	// File exists → removes it and returns true.
	{
		std::ofstream f(tmpPath, std::ios::binary);
		f.write("test", 4);
	}
	CHECK(std::filesystem::exists(tmpPath));
	CHECK(tryRemoveRecordingFileOnce(tmpPath));
	CHECK(!std::filesystem::exists(tmpPath));

	// Already removed → still returns true.
	CHECK(tryRemoveRecordingFileOnce(tmpPath));
}

// ---------------------------------------------------------------------------
// buildDefaultMuxOutputPath
// ---------------------------------------------------------------------------

static void testBuildDefaultMuxOutputPath() {
	beginSuite("buildDefaultMuxOutputPath");

	using ofxVlc4MuxHelpers::buildDefaultMuxOutputPath;

	// Basic case: video.ts → video-muxed.mp4
	{
		const std::string result = buildDefaultMuxOutputPath("/tmp/video.ts", "mp4");
		CHECK(result.find("video-muxed.mp4") != std::string::npos);
	}

	// Different container: video.ts → video-muxed.mkv
	{
		const std::string result = buildDefaultMuxOutputPath("/tmp/video.ts", "mkv");
		CHECK(result.find("video-muxed.mkv") != std::string::npos);
	}

	// Nested path preservation.
	{
		const std::string result = buildDefaultMuxOutputPath("/home/user/recordings/clip.avi", "mp4");
		CHECK(result.find("clip-muxed.mp4") != std::string::npos);
		CHECK(result.find("/home/user/recordings/") != std::string::npos || result.find("home") != std::string::npos);
	}

	// Filename with dots: my.video.ts → my.video-muxed.webm
	{
		const std::string result = buildDefaultMuxOutputPath("/tmp/my.video.ts", "webm");
		CHECK(result.find("my.video-muxed.webm") != std::string::npos);
	}
}

// ---------------------------------------------------------------------------
// normalizeSoutPath: edge cases
// ---------------------------------------------------------------------------

static void testNormalizeSoutPathEdgeCases() {
	beginSuite("normalizeSoutPath: edge cases");

	using ofxVlc4MuxHelpers::normalizeSoutPath;

	// Empty string.
	{
		const std::string result = normalizeSoutPath("");
		CHECK(result.empty());
	}

	// Path with no special characters.
	{
		const std::string result = normalizeSoutPath("/tmp/simple.ts");
		CHECK(result.find("simple.ts") != std::string::npos);
	}
}

// ---------------------------------------------------------------------------
// pathToFileUri: edge cases
// ---------------------------------------------------------------------------

static void testPathToFileUriEdgeCases() {
	beginSuite("pathToFileUri: edge cases");

	using ofxVlc4MuxHelpers::pathToFileUri;

	// Parentheses should be percent-encoded.
	{
		const std::string uri = pathToFileUri("/tmp/file(1).ts");
		CHECK(uri.find("%28") != std::string::npos); // (
		CHECK(uri.find("%29") != std::string::npos); // )
	}

	// Hash character should be percent-encoded.
	{
		const std::string uri = pathToFileUri("/tmp/file#1.ts");
		CHECK(uri.find("%23") != std::string::npos);
	}

	// Dashes, underscores, tildes should NOT be encoded.
	{
		const std::string uri = pathToFileUri("/tmp/a-b_c~d.ts");
		CHECK(uri.find("a-b_c~d.ts") != std::string::npos);
	}
}

// ---------------------------------------------------------------------------
// buildDefaultMuxOutputPath: edge cases
// ---------------------------------------------------------------------------

static void testBuildDefaultMuxOutputPathEdgeCases() {
	beginSuite("buildDefaultMuxOutputPath: edge cases");

	using ofxVlc4MuxHelpers::buildDefaultMuxOutputPath;

	// File with no extension → appends "-muxed.ext" to the stem.
	{
		const std::string result = buildDefaultMuxOutputPath("/tmp/videofile", "mp4");
		CHECK(result.find("-muxed.mp4") != std::string::npos);
	}

	// File at root directory.
	{
		const std::string result = buildDefaultMuxOutputPath("/video.avi", "mkv");
		CHECK(result.find("video-muxed.mkv") != std::string::npos);
	}
}

// ---------------------------------------------------------------------------
// waitForRecordingFile: file already exists and is stable
// ---------------------------------------------------------------------------

static void testWaitForRecordingFileAlreadyStable() {
	beginSuite("waitForRecordingFile: already stable");

	using ofxVlc4MuxHelpers::waitForRecordingFile;

	const std::string tmpPath = (std::filesystem::temp_directory_path() / "ofxvlc4_test_stable.bin").string();

	// Create a stable file first.
	{
		std::ofstream f(tmpPath, std::ios::binary);
		f.write("stable-data", 11);
	}

	// Wait should succeed quickly.
	const bool result = waitForRecordingFile(tmpPath, 2000);
	CHECK(result);

	std::filesystem::remove(tmpPath);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testNormalizeSoutPath();
	testPathToFileUri();
	testWaitForRecordingFileNoCancel();
	testWaitForRecordingFileCancelFlag();
	testRemoveRecordingFile();
	testTryRemoveRecordingFileOnce();
	testBuildDefaultMuxOutputPath();
	testNormalizeSoutPathEdgeCases();
	testPathToFileUriEdgeCases();
	testBuildDefaultMuxOutputPathEdgeCases();
	testWaitForRecordingFileAlreadyStable();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
