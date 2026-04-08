#include "test_runner.h"

// Pull in stubs before the real header so the OF/GLFW/libvlc types are
// already defined when ofxVlc4Utils.h is included.
#include "ofMain.h"
#include "GLFW/glfw3.h"
// Minimal libvlc_state_t definition (only values used by ofxVlc4Utils.h)
typedef enum libvlc_state_t {
	libvlc_NothingSpecial = 0,
	libvlc_Opening,
	libvlc_Buffering,
	libvlc_Playing,
	libvlc_Paused,
	libvlc_Stopped,
	libvlc_Stopping,
	libvlc_Error
} libvlc_state_t;

struct libvlc_media_player_t;

#include "support/ofxVlc4Utils.h"

#include <sstream>

using namespace ofxVlc4Utils;

// ---------------------------------------------------------------------------
// trimWhitespace
// ---------------------------------------------------------------------------

TEST(Utils_TrimWhitespace_Empty) {
	EXPECT_STREQ(trimWhitespace(""), "");
}

TEST(Utils_TrimWhitespace_AllSpaces) {
	EXPECT_STREQ(trimWhitespace("   "), "");
}

TEST(Utils_TrimWhitespace_AllTabs) {
	EXPECT_STREQ(trimWhitespace("\t\t\t"), "");
}

TEST(Utils_TrimWhitespace_MixedWhitespace) {
	EXPECT_STREQ(trimWhitespace(" \t \r\n "), "");
}

TEST(Utils_TrimWhitespace_NoWhitespace) {
	EXPECT_STREQ(trimWhitespace("hello"), "hello");
}

TEST(Utils_TrimWhitespace_LeadingOnly) {
	EXPECT_STREQ(trimWhitespace("   hello"), "hello");
}

TEST(Utils_TrimWhitespace_TrailingOnly) {
	EXPECT_STREQ(trimWhitespace("hello   "), "hello");
}

TEST(Utils_TrimWhitespace_BothSides) {
	EXPECT_STREQ(trimWhitespace("  hello world  "), "hello world");
}

TEST(Utils_TrimWhitespace_InternalSpacesPreserved) {
	EXPECT_STREQ(trimWhitespace(" a  b  c "), "a  b  c");
}

// ---------------------------------------------------------------------------
// fileNameFromUri
// ---------------------------------------------------------------------------

TEST(Utils_FileNameFromUri_SimpleFile) {
	EXPECT_STREQ(fileNameFromUri("http://example.com/path/to/file.mp4"), "file.mp4");
}

TEST(Utils_FileNameFromUri_NoSlash) {
	EXPECT_STREQ(fileNameFromUri("nopath"), "nopath");
}

TEST(Utils_FileNameFromUri_TrailingSlash) {
	// slashPos+1 >= size triggers the fallback: returns the full URL unchanged
	EXPECT_STREQ(fileNameFromUri("http://example.com/"), "http://example.com/");
}

TEST(Utils_FileNameFromUri_QueryStripped) {
	EXPECT_STREQ(fileNameFromUri("http://example.com/file.mp4?foo=bar"), "file.mp4");
}

TEST(Utils_FileNameFromUri_FragmentStripped) {
	EXPECT_STREQ(fileNameFromUri("http://example.com/file.mp4#anchor"), "file.mp4");
}

TEST(Utils_FileNameFromUri_QueryAndFragment) {
	EXPECT_STREQ(fileNameFromUri("http://example.com/video.mkv?t=10#s2"), "video.mkv");
}

TEST(Utils_FileNameFromUri_Empty) {
	EXPECT_STREQ(fileNameFromUri(""), "");
}

// ---------------------------------------------------------------------------
// isUri
// ---------------------------------------------------------------------------

TEST(Utils_IsUri_HttpUrl) {
	EXPECT_TRUE(isUri("http://example.com"));
}

TEST(Utils_IsUri_RtspUrl) {
	EXPECT_TRUE(isUri("rtsp://192.168.1.1/stream"));
}

TEST(Utils_IsUri_LocalPath_NotUri) {
	EXPECT_FALSE(isUri("/home/user/video.mp4"));
}

TEST(Utils_IsUri_RelativePath_NotUri) {
	EXPECT_FALSE(isUri("video.mp4"));
}

TEST(Utils_IsUri_Empty) {
	EXPECT_FALSE(isUri(""));
}

TEST(Utils_IsUri_DoubleScheme_NotUri) {
	// Two occurrences of "://" → not exactly 1
	EXPECT_FALSE(isUri("http://example.com://bad"));
}

// ---------------------------------------------------------------------------
// sanitizeFileStem
// ---------------------------------------------------------------------------

TEST(Utils_SanitizeFileStem_Empty) {
	EXPECT_STREQ(sanitizeFileStem(""), "snapshot");
}

TEST(Utils_SanitizeFileStem_AllWhitespace) {
	EXPECT_STREQ(sanitizeFileStem("   "), "snapshot");
}

TEST(Utils_SanitizeFileStem_NormalName) {
	EXPECT_STREQ(sanitizeFileStem("my_video"), "my_video");
}

TEST(Utils_SanitizeFileStem_ReplacesForwardSlash) {
	EXPECT_STREQ(sanitizeFileStem("a/b"), "a_b");
}

TEST(Utils_SanitizeFileStem_ReplacesBackslash) {
	EXPECT_STREQ(sanitizeFileStem("a\\b"), "a_b");
}

TEST(Utils_SanitizeFileStem_ReplacesColon) {
	EXPECT_STREQ(sanitizeFileStem("C:video"), "C_video");
}

TEST(Utils_SanitizeFileStem_ReplacesQuote) {
	EXPECT_STREQ(sanitizeFileStem("a\"b"), "a_b");
}

TEST(Utils_SanitizeFileStem_ReplacesPipe) {
	EXPECT_STREQ(sanitizeFileStem("a|b"), "a_b");
}

TEST(Utils_SanitizeFileStem_ReplacesQuestion) {
	EXPECT_STREQ(sanitizeFileStem("what?"), "what_");
}

TEST(Utils_SanitizeFileStem_ReplacesStar) {
	EXPECT_STREQ(sanitizeFileStem("a*b"), "a_b");
}

TEST(Utils_SanitizeFileStem_ReplacesAngleBrackets) {
	EXPECT_STREQ(sanitizeFileStem("<hello>"), "_hello_");
}

TEST(Utils_SanitizeFileStem_TrimsWhitespace) {
	EXPECT_STREQ(sanitizeFileStem("  hello  "), "hello");
}

TEST(Utils_SanitizeFileStem_OnlyInvalidChars) {
	// After replacing all invalid chars and trimming, result should be "snapshot"
	EXPECT_STREQ(sanitizeFileStem("   "), "snapshot");
}

// ---------------------------------------------------------------------------
// fallbackIndexedLabel
// ---------------------------------------------------------------------------

TEST(Utils_FallbackIndexedLabel_NonEmptyName) {
	EXPECT_STREQ(fallbackIndexedLabel("Track", 0, "Drums"), "Drums");
}

TEST(Utils_FallbackIndexedLabel_EmptyNameFallsBack) {
	EXPECT_STREQ(fallbackIndexedLabel("Track", 0, ""), "Track 1");
}

TEST(Utils_FallbackIndexedLabel_WhitespaceNameFallsBack) {
	EXPECT_STREQ(fallbackIndexedLabel("Item", 2, "   "), "Item 3");
}

TEST(Utils_FallbackIndexedLabel_IndexIsOneBased) {
	EXPECT_STREQ(fallbackIndexedLabel("Ch", 4, ""), "Ch 5");
}

// ---------------------------------------------------------------------------
// formatProgramName
// ---------------------------------------------------------------------------

TEST(Utils_FormatProgramName_WithName) {
	EXPECT_STREQ(formatProgramName(1, "Piano"), "Piano");
}

TEST(Utils_FormatProgramName_EmptyNameFallsBack) {
	EXPECT_STREQ(formatProgramName(5, ""), "Program 5");
}

TEST(Utils_FormatProgramName_WhitespaceNameFallsBack) {
	EXPECT_STREQ(formatProgramName(10, " "), "Program 10");
}

// ---------------------------------------------------------------------------
// nearlyEqual
// ---------------------------------------------------------------------------

TEST(Utils_NearlyEqual_ExactlyEqual) {
	EXPECT_TRUE(nearlyEqual(1.0f, 1.0f));
}

TEST(Utils_NearlyEqual_WithinDefaultEpsilon) {
	EXPECT_TRUE(nearlyEqual(1.0f, 1.00005f));
}

TEST(Utils_NearlyEqual_OutsideDefaultEpsilon) {
	EXPECT_FALSE(nearlyEqual(1.0f, 1.001f));
}

TEST(Utils_NearlyEqual_CustomEpsilon) {
	EXPECT_TRUE(nearlyEqual(1.0f, 1.05f, 0.1f));
	EXPECT_FALSE(nearlyEqual(1.0f, 1.2f, 0.1f));
}

TEST(Utils_NearlyEqual_NegativeValues) {
	EXPECT_TRUE(nearlyEqual(-1.0f, -1.00005f));
	EXPECT_FALSE(nearlyEqual(-1.0f, -1.002f));
}

// ---------------------------------------------------------------------------
// formatAdjustmentValue
// ---------------------------------------------------------------------------

TEST(Utils_FormatAdjustmentValue_DefaultPrecision) {
	EXPECT_STREQ(formatAdjustmentValue(1.5f), "1.5");
}

TEST(Utils_FormatAdjustmentValue_ZeroPrecision) {
	EXPECT_STREQ(formatAdjustmentValue(3.7f, 0), "4");
}

TEST(Utils_FormatAdjustmentValue_TwoPrecision) {
	EXPECT_STREQ(formatAdjustmentValue(3.14159f, 2), "3.14");
}

TEST(Utils_FormatAdjustmentValue_WithSuffix) {
	EXPECT_STREQ(formatAdjustmentValue(100.0f, 1, "%"), "100.0%");
}

TEST(Utils_FormatAdjustmentValue_NullSuffix) {
	EXPECT_STREQ(formatAdjustmentValue(2.5f, 1, nullptr), "2.5");
}

TEST(Utils_FormatAdjustmentValue_EmptySuffix) {
	EXPECT_STREQ(formatAdjustmentValue(2.5f, 1, ""), "2.5");
}

TEST(Utils_FormatAdjustmentValue_NegativeValue) {
	EXPECT_STREQ(formatAdjustmentValue(-3.5f, 1), "-3.5");
}

// ---------------------------------------------------------------------------
// normalizeOptionalPath
// ---------------------------------------------------------------------------

TEST(Utils_NormalizeOptionalPath_EmptyReturnsEmpty) {
	EXPECT_STREQ(normalizeOptionalPath(""), "");
}

TEST(Utils_NormalizeOptionalPath_WhitespaceOnlyReturnsEmpty) {
	EXPECT_STREQ(normalizeOptionalPath("   "), "");
}

TEST(Utils_NormalizeOptionalPath_UriPassedThrough) {
	// URIs are returned unchanged (no absolute path resolution)
	const std::string uri = "rtsp://192.168.1.1/stream";
	EXPECT_STREQ(normalizeOptionalPath(uri), uri);
}

// ---------------------------------------------------------------------------
// isStoppedOrIdleState
// ---------------------------------------------------------------------------

TEST(Utils_IsStoppedOrIdleState_Stopped) {
	EXPECT_TRUE(isStoppedOrIdleState(libvlc_Stopped));
}

TEST(Utils_IsStoppedOrIdleState_NothingSpecial) {
	EXPECT_TRUE(isStoppedOrIdleState(libvlc_NothingSpecial));
}

TEST(Utils_IsStoppedOrIdleState_Playing) {
	EXPECT_FALSE(isStoppedOrIdleState(libvlc_Playing));
}

// ---------------------------------------------------------------------------
// isTransientPlaybackState
// ---------------------------------------------------------------------------

TEST(Utils_IsTransientPlaybackState_Opening) {
	EXPECT_TRUE(isTransientPlaybackState(libvlc_Opening));
}

TEST(Utils_IsTransientPlaybackState_Buffering) {
	EXPECT_TRUE(isTransientPlaybackState(libvlc_Buffering));
}

TEST(Utils_IsTransientPlaybackState_Stopping) {
	EXPECT_TRUE(isTransientPlaybackState(libvlc_Stopping));
}

TEST(Utils_IsTransientPlaybackState_Playing) {
	EXPECT_FALSE(isTransientPlaybackState(libvlc_Playing));
}

TEST(Utils_IsTransientPlaybackState_Stopped) {
	EXPECT_FALSE(isTransientPlaybackState(libvlc_Stopped));
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main() {
	return TestRunner::runAll();
}
