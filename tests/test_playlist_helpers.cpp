// Tests for the pure playlist serialisation helpers (M3U and XSPF) in
// ofxVlc4PlaylistHelpers.h.  No OF, GLFW, or VLC dependencies.

#include "ofxVlc4PlaylistHelpers.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
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

using namespace ofxVlc4PlaylistHelpers;

// ---------------------------------------------------------------------------
// XML helpers
// ---------------------------------------------------------------------------

static void testXmlEscape() {
	beginSuite("xmlEscape");

	CHECK_EQ(xmlEscape("hello"), "hello");
	CHECK_EQ(xmlEscape("a&b"), "a&amp;b");
	CHECK_EQ(xmlEscape("<tag>"), "&lt;tag&gt;");
	CHECK_EQ(xmlEscape("it's \"quoted\""), "it&apos;s &quot;quoted&quot;");
	CHECK_EQ(xmlEscape(""), "");
}

static void testXmlUnescape() {
	beginSuite("xmlUnescape");

	CHECK_EQ(xmlUnescape("hello"), "hello");
	CHECK_EQ(xmlUnescape("a&amp;b"), "a&b");
	CHECK_EQ(xmlUnescape("&lt;tag&gt;"), "<tag>");
	CHECK_EQ(xmlUnescape("it&apos;s &quot;quoted&quot;"), "it's \"quoted\"");
	CHECK_EQ(xmlUnescape(""), "");
	// Lone ampersand passes through
	CHECK_EQ(xmlUnescape("a&b"), "a&b");
}

static void testXmlRoundTrip() {
	beginSuite("xmlEscape/xmlUnescape round-trip");

	const std::string original = "file:///path/to/video <'1'> & \"2\".mp4";
	CHECK_EQ(xmlUnescape(xmlEscape(original)), original);
}

static void testExtractTagContent() {
	beginSuite("extractTagContent");

	CHECK_EQ(extractTagContent("  <location>http://example.com</location>", "location"), "http://example.com");
	CHECK_EQ(extractTagContent("  <title>My Playlist</title>", "title"), "My Playlist");
	CHECK_EQ(extractTagContent("no tag here", "location"), "");
	CHECK_EQ(extractTagContent("<location>unclosed", "location"), "");
	CHECK_EQ(extractTagContent("", "location"), "");
}

// ---------------------------------------------------------------------------
// Percent encoding / decoding
// ---------------------------------------------------------------------------

static void testPercentEncodeDecode() {
	beginSuite("percentEncode / percentDecode");

	CHECK_EQ(percentEncodePath("/simple/path.mp4"), "/simple/path.mp4");
	CHECK_EQ(percentEncodePath("/path with spaces/file.mp4"), "/path%20with%20spaces/file.mp4");

	CHECK_EQ(percentDecode("/simple/path.mp4"), "/simple/path.mp4");
	CHECK_EQ(percentDecode("/path%20with%20spaces/file.mp4"), "/path with spaces/file.mp4");
	CHECK_EQ(percentDecode("%2Fhello%2F"), "/hello/");

	// Round-trip
	const std::string original = "/path/with spaces/and(parens)/file.mp4";
	CHECK_EQ(percentDecode(percentEncodePath(original)), original);

	// Invalid percent sequences pass through
	CHECK_EQ(percentDecode("%GG"), "%GG");
	CHECK_EQ(percentDecode("a%2"), "a%2");
}

// ---------------------------------------------------------------------------
// M3U serialisation / deserialisation
// ---------------------------------------------------------------------------

static void testSerializeM3U() {
	beginSuite("serializeM3U");

	{
		const std::vector<std::string> items = { "/path/to/song.mp3", "http://stream.example.com/live" };
		const std::string m3u = serializeM3U(items);
		CHECK(m3u.find("#EXTM3U") != std::string::npos);
		CHECK(m3u.find("/path/to/song.mp3") != std::string::npos);
		CHECK(m3u.find("http://stream.example.com/live") != std::string::npos);
		CHECK(m3u.find("#EXTINF:-1,") != std::string::npos);
	}

	{
		const std::vector<std::string> empty;
		const std::string m3u = serializeM3U(empty);
		CHECK_EQ(m3u, "#EXTM3U\n");
	}
}

static void testDeserializeM3U() {
	beginSuite("deserializeM3U");

	{
		const std::string m3u =
			"#EXTM3U\n"
			"#EXTINF:-1,Song One\n"
			"/path/to/song1.mp3\n"
			"#EXTINF:-1,Song Two\n"
			"/path/to/song2.mp3\n";
		const auto items = deserializeM3U(m3u);
		CHECK_EQ(items.size(), static_cast<size_t>(2));
		CHECK_EQ(items[0], "/path/to/song1.mp3");
		CHECK_EQ(items[1], "/path/to/song2.mp3");
	}

	// Windows line endings
	{
		const std::string m3u = "#EXTM3U\r\n/path/to/song.mp3\r\n";
		const auto items = deserializeM3U(m3u);
		CHECK_EQ(items.size(), static_cast<size_t>(1));
		CHECK_EQ(items[0], "/path/to/song.mp3");
	}

	// Empty / comment-only content
	{
		const auto items = deserializeM3U("#EXTM3U\n# comment\n\n");
		CHECK(items.empty());
	}

	// Bare M3U (no #EXTM3U header)
	{
		const std::string m3u = "/path/to/a.mp3\n/path/to/b.mp3\n";
		const auto items = deserializeM3U(m3u);
		CHECK_EQ(items.size(), static_cast<size_t>(2));
	}
}

static void testM3URoundTrip() {
	beginSuite("M3U round-trip");

	const std::vector<std::string> original = {
		"/path/to/video.mp4",
		"http://stream.example.com/live",
		"/another/file.mkv"
	};
	const std::string serialized = serializeM3U(original);
	const auto deserialized = deserializeM3U(serialized);
	CHECK_EQ(deserialized.size(), original.size());
	for (size_t i = 0; i < original.size(); ++i) {
		CHECK_EQ(deserialized[i], original[i]);
	}
}

// ---------------------------------------------------------------------------
// XSPF serialisation / deserialisation
// ---------------------------------------------------------------------------

static void testSerializeXSPF() {
	beginSuite("serializeXSPF");

	{
		const std::vector<std::string> items = { "/path/to/song.mp3", "http://stream.example.com/live" };
		const std::string xspf = serializeXSPF(items, "Test Playlist");
		CHECK(xspf.find("<?xml version=\"1.0\"") != std::string::npos);
		CHECK(xspf.find("<title>Test Playlist</title>") != std::string::npos);
		CHECK(xspf.find("<trackList>") != std::string::npos);
		CHECK(xspf.find("<location>/path/to/song.mp3</location>") != std::string::npos);
		CHECK(xspf.find("<location>http://stream.example.com/live</location>") != std::string::npos);
	}

	{
		const std::vector<std::string> empty;
		const std::string xspf = serializeXSPF(empty);
		CHECK(xspf.find("<trackList>") != std::string::npos);
		CHECK(xspf.find("</trackList>") != std::string::npos);
	}
}

static void testDeserializeXSPF() {
	beginSuite("deserializeXSPF");

	{
		const std::string xspf =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<playlist xmlns=\"http://xspf.org/ns/0/\" version=\"1\">\n"
			"  <title>My List</title>\n"
			"  <trackList>\n"
			"    <track>\n"
			"      <location>/path/to/song1.mp3</location>\n"
			"    </track>\n"
			"    <track>\n"
			"      <location>http://example.com/stream</location>\n"
			"    </track>\n"
			"  </trackList>\n"
			"</playlist>\n";
		const auto items = deserializeXSPF(xspf);
		CHECK_EQ(items.size(), static_cast<size_t>(2));
		CHECK_EQ(items[0], "/path/to/song1.mp3");
		CHECK_EQ(items[1], "http://example.com/stream");
	}

	// Empty content
	{
		const auto items = deserializeXSPF("");
		CHECK(items.empty());
	}

	// Content with XML-escaped characters
	{
		const std::string xspf =
			"<playlist><trackList>\n"
			"<track><location>path/with &amp; and &lt;special&gt;</location></track>\n"
			"</trackList></playlist>\n";
		const auto items = deserializeXSPF(xspf);
		CHECK_EQ(items.size(), static_cast<size_t>(1));
		CHECK_EQ(items[0], "path/with & and <special>");
	}
}

static void testXSPFRoundTrip() {
	beginSuite("XSPF round-trip");

	const std::vector<std::string> original = {
		"/path/to/video.mp4",
		"http://stream.example.com/live",
		"/path/with spaces/file.mkv",
		"file with <special> & 'chars'.mp4"
	};
	const std::string serialized = serializeXSPF(original, "Round-trip Test");
	const auto deserialized = deserializeXSPF(serialized);
	CHECK_EQ(deserialized.size(), original.size());
	for (size_t i = 0; i < original.size(); ++i) {
		CHECK_EQ(deserialized[i], original[i]);
	}
}

// ---------------------------------------------------------------------------
// URI detection
// ---------------------------------------------------------------------------

static void testIsUri() {
	beginSuite("isUri");

	CHECK(isUri("http://example.com"));
	CHECK(isUri("https://example.com/path"));
	CHECK(isUri("rtsp://192.168.0.1:554/stream"));
	CHECK(isUri("file:///path/to/file.mp4"));
	CHECK(!isUri("/path/to/file.mp4"));
	CHECK(!isUri("relative/path.mp4"));
	CHECK(!isUri(""));
	CHECK(!isUri("://no-scheme"));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testXmlEscape();
	testXmlUnescape();
	testXmlRoundTrip();
	testExtractTagContent();
	testPercentEncodeDecode();
	testSerializeM3U();
	testDeserializeM3U();
	testM3URoundTrip();
	testSerializeXSPF();
	testDeserializeXSPF();
	testXSPFRoundTrip();
	testIsUri();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
