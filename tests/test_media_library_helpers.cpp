// Tests for media-library helpers extracted into ofxVlc4MediaLibraryHelpers.h.
// Covers normalizeExtensions, appendMetadataValue, and hasDetailedTrackMetadata.

#include "ofxVlc4MediaLibraryHelpers.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace ofxVlc4MediaLibraryHelpers;

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
// normalizeExtensions
// ---------------------------------------------------------------------------

static void testNormalizeExtensionsBasic() {
	beginSuite("normalizeExtensions: basic");

	// Standard extensions without dots.
	{
		const auto exts = normalizeExtensions({"mp4", "mkv", "avi"});
		CHECK_EQ(exts.size(), 3u);
		CHECK(exts.count(".mp4") == 1);
		CHECK(exts.count(".mkv") == 1);
		CHECK(exts.count(".avi") == 1);
	}

	// Extensions already with dots.
	{
		const auto exts = normalizeExtensions({".mp4", ".mkv"});
		CHECK_EQ(exts.size(), 2u);
		CHECK(exts.count(".mp4") == 1);
		CHECK(exts.count(".mkv") == 1);
	}

	// Mixed case → lowered.
	{
		const auto exts = normalizeExtensions({"MP4", "Mkv", "AVI"});
		CHECK_EQ(exts.size(), 3u);
		CHECK(exts.count(".mp4") == 1);
		CHECK(exts.count(".mkv") == 1);
		CHECK(exts.count(".avi") == 1);
	}

	// Duplicates collapsed by set.
	{
		const auto exts = normalizeExtensions({"mp4", "mp4", "MP4"});
		CHECK_EQ(exts.size(), 1u);
		CHECK(exts.count(".mp4") == 1);
	}
}

static void testNormalizeExtensionsEdgeCases() {
	beginSuite("normalizeExtensions: edge cases");

	// Empty list.
	{
		const auto exts = normalizeExtensions({});
		CHECK(exts.empty());
	}

	// Whitespace-only entries → skipped.
	{
		const auto exts = normalizeExtensions({"  ", "\t", ""});
		CHECK(exts.empty());
	}

	// Entries with surrounding whitespace.
	{
		const auto exts = normalizeExtensions({"  mp4  ", " .mkv "});
		CHECK_EQ(exts.size(), 2u);
		CHECK(exts.count(".mp4") == 1);
		CHECK(exts.count(".mkv") == 1);
	}

	// Dot-only entry → treated as valid (just a dot).
	{
		const auto exts = normalizeExtensions({"."});
		CHECK_EQ(exts.size(), 1u);
		CHECK(exts.count(".") == 1);
	}
}

// ---------------------------------------------------------------------------
// appendMetadataValue
// ---------------------------------------------------------------------------

static void testAppendMetadataValue() {
	beginSuite("appendMetadataValue");

	// Non-empty value → appended.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		appendMetadataValue(metadata, "Title", "My Video");
		CHECK_EQ(metadata.size(), 1u);
		CHECK_EQ(metadata[0].first, std::string("Title"));
		CHECK_EQ(metadata[0].second, std::string("My Video"));
	}

	// Empty value → not appended.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		appendMetadataValue(metadata, "Title", "");
		CHECK(metadata.empty());
	}

	// Whitespace-only value → not appended.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		appendMetadataValue(metadata, "Title", "   ");
		CHECK(metadata.empty());
	}

	// Value with surrounding whitespace → trimmed.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		appendMetadataValue(metadata, "Artist", "  John Doe  ");
		CHECK_EQ(metadata.size(), 1u);
		CHECK_EQ(metadata[0].second, std::string("John Doe"));
	}

	// Multiple appends.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		appendMetadataValue(metadata, "Title", "Video");
		appendMetadataValue(metadata, "Artist", "");
		appendMetadataValue(metadata, "Album", "Playlist");
		CHECK_EQ(metadata.size(), 2u);
		CHECK_EQ(metadata[0].first, std::string("Title"));
		CHECK_EQ(metadata[1].first, std::string("Album"));
	}
}

// ---------------------------------------------------------------------------
// hasDetailedTrackMetadata
// ---------------------------------------------------------------------------

static void testHasDetailedTrackMetadata() {
	beginSuite("hasDetailedTrackMetadata");

	// Empty metadata → false.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		CHECK(!hasDetailedTrackMetadata(metadata));
	}

	// Only non-track metadata → false.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Title", "My Video");
		metadata.emplace_back("Artist", "John Doe");
		CHECK(!hasDetailedTrackMetadata(metadata));
	}

	// Video Codec present → true.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Video Codec", "H264");
		CHECK(hasDetailedTrackMetadata(metadata));
	}

	// Audio Codec present → true.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Audio Codec", "AAC");
		CHECK(hasDetailedTrackMetadata(metadata));
	}

	// Subtitle Codec present → true.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Subtitle Codec", "SRT");
		CHECK(hasDetailedTrackMetadata(metadata));
	}

	// Video Resolution present → true.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Video Resolution", "1920x1080");
		CHECK(hasDetailedTrackMetadata(metadata));
	}

	// Frame Rate present → true.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Frame Rate", "29.97 fps");
		CHECK(hasDetailedTrackMetadata(metadata));
	}

	// Audio Channels present → true.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Audio Channels", "2");
		CHECK(hasDetailedTrackMetadata(metadata));
	}

	// Audio Rate present → true.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Audio Rate", "44100 Hz");
		CHECK(hasDetailedTrackMetadata(metadata));
	}

	// Track metadata with empty value → false (skipped).
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Video Codec", "");
		CHECK(!hasDetailedTrackMetadata(metadata));
	}

	// Mix of empty-value track fields and non-track fields → false.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Video Codec", "");
		metadata.emplace_back("Title", "Test");
		CHECK(!hasDetailedTrackMetadata(metadata));
	}

	// All seven recognized track labels present.
	{
		std::vector<std::pair<std::string, std::string>> metadata;
		metadata.emplace_back("Video Codec", "H264");
		metadata.emplace_back("Audio Codec", "AAC");
		metadata.emplace_back("Subtitle Codec", "SRT");
		metadata.emplace_back("Video Resolution", "1920x1080");
		metadata.emplace_back("Frame Rate", "30 fps");
		metadata.emplace_back("Audio Channels", "2");
		metadata.emplace_back("Audio Rate", "48000 Hz");
		CHECK(hasDetailedTrackMetadata(metadata));
	}
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testNormalizeExtensionsBasic();
	testNormalizeExtensionsEdgeCases();
	testAppendMetadataValue();
	testHasDetailedTrackMetadata();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
