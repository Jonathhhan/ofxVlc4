// Tests for NleTimecode.h — SMPTE timecode engine.
// Covers construction, conversions, drop-frame math, arithmetic,
// comparison, clamping, and FrameRate helpers.

#include "NleTimecode.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace nle;

// ---------------------------------------------------------------------------
// Minimal test harness (same pattern as test_media_helpers.cpp)
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
// Construction from frames
// ---------------------------------------------------------------------------

static void testConstructionFromFrames() {
	beginSuite("Construction from frames");

	Timecode tc(100, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(100));
	CHECK(tc.rate() == FrameRate::Fps24);

	// Default: frame 0 at 24 fps.
	Timecode def;
	CHECK_EQ(def.totalFrames(), static_cast<int64_t>(0));
	CHECK(def.rate() == FrameRate::Fps24);

	// Negative frames clamped to 0.
	Timecode neg(-10, FrameRate::Fps30);
	CHECK_EQ(neg.totalFrames(), static_cast<int64_t>(0));
}

// ---------------------------------------------------------------------------
// Construction from milliseconds
// ---------------------------------------------------------------------------

static void testFromMilliseconds() {
	beginSuite("Construction from milliseconds");

	// 24 fps: 1000 ms = 24 frames.
	auto tc = Timecode::fromMilliseconds(1000, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(24));

	// 25 fps: 1000 ms = 25 frames.
	tc = Timecode::fromMilliseconds(1000, FrameRate::Fps25);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(25));

	// 0 ms → 0 frames.
	tc = Timecode::fromMilliseconds(0, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(0));

	// Negative ms clamped to 0.
	tc = Timecode::fromMilliseconds(-500, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(0));
}

// ---------------------------------------------------------------------------
// Construction from SMPTE components
// ---------------------------------------------------------------------------

static void testFromSmpte() {
	beginSuite("Construction from SMPTE components");

	// 01:00:00:00 at 24 fps = 86400 frames.
	auto tc = Timecode::fromSmpte(1, 0, 0, 0, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(24 * 3600));

	// 00:01:00:00 at 24 fps = 1440 frames.
	tc = Timecode::fromSmpte(0, 1, 0, 0, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(24 * 60));

	// 00:00:01:00 at 24 fps = 24 frames.
	tc = Timecode::fromSmpte(0, 0, 1, 0, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(24));

	// 00:00:00:12 at 24 fps = 12 frames.
	tc = Timecode::fromSmpte(0, 0, 0, 12, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(12));
}

// ---------------------------------------------------------------------------
// Construction from SMPTE string
// ---------------------------------------------------------------------------

static void testFromSmpteString() {
	beginSuite("Construction from SMPTE string");

	auto tc = Timecode::fromSmpteString("01:00:00:00", FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(24 * 3600));

	tc = Timecode::fromSmpteString("00:00:01:12", FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(24 + 12));

	// DF separator ';'
	tc = Timecode::fromSmpteString("00:01:00;02", FrameRate::Fps29_97_DF);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(1800));

	// Bad string → frame 0.
	tc = Timecode::fromSmpteString("garbage", FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(0));
}

// ---------------------------------------------------------------------------
// Round-trip: frames → SMPTE → frames (NDF rates)
// ---------------------------------------------------------------------------

static void testRoundTripFramesSmpte() {
	beginSuite("Round-trip: frames -> SMPTE -> frames (NDF)");

	// 24 fps — first 100 frames.
	for (int64_t f = 0; f < 100; ++f) {
		Timecode tc(f, FrameRate::Fps24);
		int hh, mm, ss, ff;
		tc.toSmpte(hh, mm, ss, ff);
		auto tc2 = Timecode::fromSmpte(hh, mm, ss, ff, FrameRate::Fps24);
		if (tc2.totalFrames() != f) {
			CHECK(false); // mismatch
			return;
		}
	}
	CHECK(true); // all 100 matched

	// 25 fps — large value.
	{
		const int64_t f = 25 * 3600 + 25 * 60 + 13;
		Timecode tc(f, FrameRate::Fps25);
		int hh, mm, ss, ff;
		tc.toSmpte(hh, mm, ss, ff);
		auto tc2 = Timecode::fromSmpte(hh, mm, ss, ff, FrameRate::Fps25);
		CHECK_EQ(tc2.totalFrames(), f);
	}

	// 30 fps — another value.
	{
		const int64_t f = 30 * 60 * 5 + 15;
		Timecode tc(f, FrameRate::Fps30);
		int hh, mm, ss, ff;
		tc.toSmpte(hh, mm, ss, ff);
		auto tc2 = Timecode::fromSmpte(hh, mm, ss, ff, FrameRate::Fps30);
		CHECK_EQ(tc2.totalFrames(), f);
	}
}

// ---------------------------------------------------------------------------
// Round-trip: ms → Timecode → ms
// ---------------------------------------------------------------------------

static void testRoundTripMsTimecode() {
	beginSuite("Round-trip: ms -> Timecode -> ms");

	// 24 fps: 1000 ms round-trips.
	{
		auto tc = Timecode::fromMilliseconds(1000, FrameRate::Fps24);
		CHECK_EQ(tc.toMilliseconds(), 1000);
	}

	// 25 fps: 2000 ms.
	{
		auto tc = Timecode::fromMilliseconds(2000, FrameRate::Fps25);
		CHECK_EQ(tc.toMilliseconds(), 2000);
	}

	// 29.97 NDF: 1001 ms (exact at 30000/1001).
	{
		auto tc = Timecode::fromMilliseconds(1001, FrameRate::Fps29_97_NDF);
		CHECK_EQ(tc.toMilliseconds(), 1001);
	}

	// 23.976: 1001 ms.
	{
		auto tc = Timecode::fromMilliseconds(1001, FrameRate::Fps23_976);
		CHECK_EQ(tc.toMilliseconds(), 1001);
	}

	// 59.94 NDF: 1001 ms.
	{
		auto tc = Timecode::fromMilliseconds(1001, FrameRate::Fps59_94_NDF);
		CHECK_EQ(tc.toMilliseconds(), 1001);
	}
}

// ---------------------------------------------------------------------------
// Drop-frame math for 29.97 DF
// ---------------------------------------------------------------------------

static void testDropFrame2997() {
	beginSuite("Drop-frame 29.97 DF");

	const auto rate = FrameRate::Fps29_97_DF;

	// Frame 0 → 00:00:00;00.
	{
		Timecode tc(0, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:00:00;00"));
	}

	// Frame 1799 → 00:00:59;29  (last frame of minute 0).
	{
		Timecode tc(1799, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:00:59;29"));
	}

	// Frame 1800 → 00:01:00;02  (frames ;00 and ;01 are dropped).
	{
		Timecode tc(1800, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:01:00;02"));
	}

	// Reverse: 00:01:00;02 → frame 1800.
	{
		auto tc = Timecode::fromSmpte(0, 1, 0, 2, rate);
		CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(1800));
	}

	// 10th minute boundary: no drop at minute 10.
	// Frame 17982 = frames in 10 minutes = 30*600 - 2*9 = 17982.
	{
		Timecode tc(17982, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:10:00;00"));
	}

	// Round-trip across many frames at boundaries.
	{
		bool allOk = true;
		int64_t testFrames[] = {0, 1, 29, 30, 1799, 1800, 1801, 3597, 3598,
		                        17981, 17982, 17983, 35964};
		for (auto f : testFrames) {
			Timecode tc(f, rate);
			int hh, mm, ss, ff;
			tc.toSmpte(hh, mm, ss, ff);
			auto tc2 = Timecode::fromSmpte(hh, mm, ss, ff, rate);
			if (tc2.totalFrames() != f) { allOk = false; break; }
		}
		CHECK(allOk);
	}
}

// ---------------------------------------------------------------------------
// Drop-frame math for 59.94 DF
// ---------------------------------------------------------------------------

static void testDropFrame5994() {
	beginSuite("Drop-frame 59.94 DF");

	const auto rate = FrameRate::Fps59_94_DF;

	// Frame 3599 → 00:00:59;59.
	{
		Timecode tc(3599, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:00:59;59"));
	}

	// Frame 3600 → 00:01:00;04  (drops ;00, ;01, ;02, ;03).
	{
		Timecode tc(3600, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:01:00;04"));
	}

	// Reverse: 00:01:00;04 → frame 3600.
	{
		auto tc = Timecode::fromSmpte(0, 1, 0, 4, rate);
		CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(3600));
	}

	// 10-minute boundary: 60*600 - 4*9 = 35964 frames.
	{
		Timecode tc(35964, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:10:00;00"));
	}

	// Round-trip spot check.
	{
		bool allOk = true;
		int64_t testFrames[] = {0, 59, 3599, 3600, 3601, 7195, 7196, 35963, 35964};
		for (auto f : testFrames) {
			Timecode tc(f, rate);
			int hh, mm, ss, ff;
			tc.toSmpte(hh, mm, ss, ff);
			auto tc2 = Timecode::fromSmpte(hh, mm, ss, ff, rate);
			if (tc2.totalFrames() != f) { allOk = false; break; }
		}
		CHECK(allOk);
	}
}

// ---------------------------------------------------------------------------
// Non-drop-frame 29.97 NDF
// ---------------------------------------------------------------------------

static void testNonDropFrame2997() {
	beginSuite("Non-drop-frame 29.97 NDF");

	const auto rate = FrameRate::Fps29_97_NDF;

	// NDF uses ':' separator, no dropped frame numbers.
	{
		Timecode tc(1800, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:01:00:00"));
	}

	// Frame 30 → 00:00:01:00.
	{
		Timecode tc(30, rate);
		CHECK_EQ(tc.toSmpteString(), std::string("00:00:01:00"));
	}

	// Round-trip.
	{
		auto tc = Timecode::fromSmpte(0, 1, 0, 0, rate);
		CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(1800));
	}
}

// ---------------------------------------------------------------------------
// Arithmetic operators
// ---------------------------------------------------------------------------

static void testArithmetic() {
	beginSuite("Arithmetic operators");

	Timecode a(100, FrameRate::Fps24);
	Timecode b(30, FrameRate::Fps24);

	// operator+
	auto sum = a + b;
	CHECK_EQ(sum.totalFrames(), static_cast<int64_t>(130));

	// operator-
	auto diff = a - b;
	CHECK_EQ(diff.totalFrames(), static_cast<int64_t>(70));

	// Subtraction clamped to 0.
	auto under = b - a;
	CHECK_EQ(under.totalFrames(), static_cast<int64_t>(0));

	// operator+=
	Timecode c(50, FrameRate::Fps24);
	c += Timecode(10, FrameRate::Fps24);
	CHECK_EQ(c.totalFrames(), static_cast<int64_t>(60));

	// operator-=
	c -= Timecode(5, FrameRate::Fps24);
	CHECK_EQ(c.totalFrames(), static_cast<int64_t>(55));

	// operator-= clamped to 0.
	Timecode d(3, FrameRate::Fps24);
	d -= Timecode(10, FrameRate::Fps24);
	CHECK_EQ(d.totalFrames(), static_cast<int64_t>(0));

	// Increment / decrement.
	Timecode e(5, FrameRate::Fps24);
	++e;
	CHECK_EQ(e.totalFrames(), static_cast<int64_t>(6));
	--e;
	CHECK_EQ(e.totalFrames(), static_cast<int64_t>(5));
	// Decrement at 0 stays 0.
	Timecode z(0, FrameRate::Fps24);
	--z;
	CHECK_EQ(z.totalFrames(), static_cast<int64_t>(0));
}

// ---------------------------------------------------------------------------
// Comparison operators
// ---------------------------------------------------------------------------

static void testComparison() {
	beginSuite("Comparison operators");

	Timecode a(10, FrameRate::Fps24);
	Timecode b(20, FrameRate::Fps24);
	Timecode c(10, FrameRate::Fps24);

	CHECK(a == c);
	CHECK(a != b);
	CHECK(a < b);
	CHECK(a <= b);
	CHECK(a <= c);
	CHECK(b > a);
	CHECK(b >= a);
	CHECK(a >= c);
}

// ---------------------------------------------------------------------------
// Clamping negative frames to 0
// ---------------------------------------------------------------------------

static void testClampNegative() {
	beginSuite("Clamping negative frames to 0");

	Timecode tc(-100, FrameRate::Fps24);
	CHECK_EQ(tc.totalFrames(), static_cast<int64_t>(0));

	// fromMilliseconds with negative.
	auto tc2 = Timecode::fromMilliseconds(-500, FrameRate::Fps24);
	CHECK_EQ(tc2.totalFrames(), static_cast<int64_t>(0));
}

// ---------------------------------------------------------------------------
// FrameRate helpers
// ---------------------------------------------------------------------------

static void testFrameRateHelpers() {
	beginSuite("FrameRate helpers");

	// nominalFps
	CHECK_EQ(nominalFps(FrameRate::Fps23_976), 24);
	CHECK_EQ(nominalFps(FrameRate::Fps24), 24);
	CHECK_EQ(nominalFps(FrameRate::Fps25), 25);
	CHECK_EQ(nominalFps(FrameRate::Fps29_97_DF), 30);
	CHECK_EQ(nominalFps(FrameRate::Fps29_97_NDF), 30);
	CHECK_EQ(nominalFps(FrameRate::Fps30), 30);
	CHECK_EQ(nominalFps(FrameRate::Fps50), 50);
	CHECK_EQ(nominalFps(FrameRate::Fps59_94_DF), 60);
	CHECK_EQ(nominalFps(FrameRate::Fps60), 60);

	// exactFps — check a few.
	CHECK(std::abs(exactFps(FrameRate::Fps24) - 24.0) < 0.001);
	CHECK(std::abs(exactFps(FrameRate::Fps29_97_DF) - 29.97003) < 0.001);
	CHECK(std::abs(exactFps(FrameRate::Fps25) - 25.0) < 0.001);

	// isDropFrame
	CHECK(isDropFrame(FrameRate::Fps29_97_DF));
	CHECK(isDropFrame(FrameRate::Fps59_94_DF));
	CHECK(!isDropFrame(FrameRate::Fps29_97_NDF));
	CHECK(!isDropFrame(FrameRate::Fps24));
	CHECK(!isDropFrame(FrameRate::Fps25));

	// frameRateLabel
	CHECK_EQ(frameRateLabel(FrameRate::Fps24), std::string("24"));
	CHECK_EQ(frameRateLabel(FrameRate::Fps29_97_DF), std::string("29.97 DF"));
	CHECK_EQ(frameRateLabel(FrameRate::Fps29_97_NDF), std::string("29.97 NDF"));
	CHECK_EQ(frameRateLabel(FrameRate::Fps59_94_DF), std::string("59.94 DF"));
}

// ---------------------------------------------------------------------------
// SMPTE string format: ':' for NDF, ';' for DF
// ---------------------------------------------------------------------------

static void testSmpteStringFormat() {
	beginSuite("SMPTE string format separators");

	// NDF uses ':'.
	{
		Timecode tc(100, FrameRate::Fps24);
		std::string s = tc.toSmpteString();
		CHECK(s.find(';') == std::string::npos);
		// Should contain exactly 3 ':' separators.
		int colonCount = 0;
		for (char ch : s) if (ch == ':') ++colonCount;
		CHECK_EQ(colonCount, 3);
	}

	// DF uses ';' before the frame field.
	{
		Timecode tc(100, FrameRate::Fps29_97_DF);
		std::string s = tc.toSmpteString();
		CHECK(s.find(';') != std::string::npos);
		// ';' should be the last separator (position 8).
		CHECK_EQ(s[8], ';');
	}
}

// ---------------------------------------------------------------------------
// Drop-frame overflow — verify int64_t used for dropCorrection
// ---------------------------------------------------------------------------

static void testDropFrameOverflow() {
	beginSuite("Drop-frame overflow (large hours)");

	// A large hour value that would overflow a 32-bit int in the old
	// computation: totalMinutes * d could exceed INT_MAX.
	// 10 000 hours at 29.97 DF → totalMinutes = 600 000, dropCorrection ~= 1 080 000
	const auto tc = Timecode::fromSmpte(10000, 0, 0, 0, FrameRate::Fps29_97_DF);
	// Sanity: should be a very large positive frame count.
	CHECK(tc.totalFrames() > 0);
	// Round-trip back to SMPTE should yield the same components.
	int hh = 0, mm = 0, ss = 0, ff = 0;
	tc.toSmpte(hh, mm, ss, ff);
	CHECK_EQ(hh, 10000);
	CHECK_EQ(mm, 0);
	CHECK_EQ(ss, 0);
	CHECK_EQ(ff, 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testConstructionFromFrames();
	testFromMilliseconds();
	testFromSmpte();
	testFromSmpteString();
	testRoundTripFramesSmpte();
	testRoundTripMsTimecode();
	testDropFrame2997();
	testDropFrame5994();
	testNonDropFrame2997();
	testArithmetic();
	testComparison();
	testClampNegative();
	testFrameRateHelpers();
	testSmpteStringFormat();
	testDropFrameOverflow();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
