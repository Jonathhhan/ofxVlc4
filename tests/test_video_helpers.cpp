// Tests for video helper functions extracted into ofxVlc4VideoHelpers.h.
// Covers deinterlace, aspect ratio, crop, adjustment engine labels,
// output backend/decoder device labels, RGB clamping, visible size,
// and playback mode string conversion — all pure logic with no runtime deps.

#include "ofxVlc4VideoHelpers.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace ofxVlc4VideoHelpers;

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
// videoDeinterlaceModeLabel
// ---------------------------------------------------------------------------

static void testVideoDeinterlaceModeLabel() {
	beginSuite("videoDeinterlaceModeLabel");

	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Auto)), std::string("auto"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Off)), std::string("off"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Blend)), std::string("blend"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Bob)), std::string("bob"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Linear)), std::string("linear"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::X)), std::string("x"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Yadif)), std::string("yadif"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Yadif2x)), std::string("yadif2x"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Phosphor)), std::string("phosphor"));
	CHECK_EQ(std::string(videoDeinterlaceModeLabel(VideoDeinterlaceMode::Ivtc)), std::string("ivtc"));
}

// ---------------------------------------------------------------------------
// videoDeinterlaceState
// ---------------------------------------------------------------------------

static void testVideoDeinterlaceState() {
	beginSuite("videoDeinterlaceState");

	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Auto), -1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Off), 0);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Blend), 1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Bob), 1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Linear), 1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::X), 1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Yadif), 1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Yadif2x), 1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Phosphor), 1);
	CHECK_EQ(videoDeinterlaceState(VideoDeinterlaceMode::Ivtc), 1);
}

// ---------------------------------------------------------------------------
// videoDeinterlaceFilterName
// ---------------------------------------------------------------------------

static void testVideoDeinterlaceFilterName() {
	beginSuite("videoDeinterlaceFilterName");

	// Auto and Off return nullptr.
	CHECK(videoDeinterlaceFilterName(VideoDeinterlaceMode::Auto) == nullptr);
	CHECK(videoDeinterlaceFilterName(VideoDeinterlaceMode::Off) == nullptr);

	// All others return valid strings.
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::Blend)), std::string("blend"));
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::Bob)), std::string("bob"));
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::Linear)), std::string("linear"));
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::X)), std::string("x"));
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::Yadif)), std::string("yadif"));
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::Yadif2x)), std::string("yadif2x"));
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::Phosphor)), std::string("phosphor"));
	CHECK_EQ(std::string(videoDeinterlaceFilterName(VideoDeinterlaceMode::Ivtc)), std::string("ivtc"));
}

// ---------------------------------------------------------------------------
// videoAspectRatioValue
// ---------------------------------------------------------------------------

static void testVideoAspectRatioValue() {
	beginSuite("videoAspectRatioValue");

	CHECK(videoAspectRatioValue(VideoAspectRatioMode::Default) == nullptr);
	CHECK_EQ(std::string(videoAspectRatioValue(VideoAspectRatioMode::Fill)), std::string("fill"));
	CHECK_EQ(std::string(videoAspectRatioValue(VideoAspectRatioMode::Ratio16_9)), std::string("16:9"));
	CHECK_EQ(std::string(videoAspectRatioValue(VideoAspectRatioMode::Ratio16_10)), std::string("16:10"));
	CHECK_EQ(std::string(videoAspectRatioValue(VideoAspectRatioMode::Ratio4_3)), std::string("4:3"));
	CHECK_EQ(std::string(videoAspectRatioValue(VideoAspectRatioMode::Ratio1_1)), std::string("1:1"));
	CHECK_EQ(std::string(videoAspectRatioValue(VideoAspectRatioMode::Ratio21_9)), std::string("21:9"));
	CHECK_EQ(std::string(videoAspectRatioValue(VideoAspectRatioMode::Ratio235_1)), std::string("235:100"));
}

// ---------------------------------------------------------------------------
// videoAspectRatioLabel
// ---------------------------------------------------------------------------

static void testVideoAspectRatioLabel() {
	beginSuite("videoAspectRatioLabel");

	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Default)), std::string("default"));
	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Fill)), std::string("fill"));
	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Ratio16_9)), std::string("16:9"));
	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Ratio16_10)), std::string("16:10"));
	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Ratio4_3)), std::string("4:3"));
	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Ratio1_1)), std::string("1:1"));
	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Ratio21_9)), std::string("21:9"));
	// Ratio235_1 renders as "2.35:1" not "235:100".
	CHECK_EQ(std::string(videoAspectRatioLabel(VideoAspectRatioMode::Ratio235_1)), std::string("2.35:1"));
}

// ---------------------------------------------------------------------------
// videoCropRatio
// ---------------------------------------------------------------------------

static void testVideoCropRatio() {
	beginSuite("videoCropRatio");

	{
		auto [w, h] = videoCropRatio(VideoCropMode::None);
		CHECK_EQ(w, 0u);
		CHECK_EQ(h, 0u);
	}
	{
		auto [w, h] = videoCropRatio(VideoCropMode::Ratio16_9);
		CHECK_EQ(w, 16u);
		CHECK_EQ(h, 9u);
	}
	{
		auto [w, h] = videoCropRatio(VideoCropMode::Ratio16_10);
		CHECK_EQ(w, 16u);
		CHECK_EQ(h, 10u);
	}
	{
		auto [w, h] = videoCropRatio(VideoCropMode::Ratio4_3);
		CHECK_EQ(w, 4u);
		CHECK_EQ(h, 3u);
	}
	{
		auto [w, h] = videoCropRatio(VideoCropMode::Ratio1_1);
		CHECK_EQ(w, 1u);
		CHECK_EQ(h, 1u);
	}
	{
		auto [w, h] = videoCropRatio(VideoCropMode::Ratio21_9);
		CHECK_EQ(w, 21u);
		CHECK_EQ(h, 9u);
	}
	{
		auto [w, h] = videoCropRatio(VideoCropMode::Ratio235_1);
		CHECK_EQ(w, 235u);
		CHECK_EQ(h, 100u);
	}
}

// ---------------------------------------------------------------------------
// videoCropLabel
// ---------------------------------------------------------------------------

static void testVideoCropLabel() {
	beginSuite("videoCropLabel");

	CHECK_EQ(std::string(videoCropLabel(VideoCropMode::None)), std::string("none"));
	CHECK_EQ(std::string(videoCropLabel(VideoCropMode::Ratio16_9)), std::string("16:9"));
	CHECK_EQ(std::string(videoCropLabel(VideoCropMode::Ratio16_10)), std::string("16:10"));
	CHECK_EQ(std::string(videoCropLabel(VideoCropMode::Ratio4_3)), std::string("4:3"));
	CHECK_EQ(std::string(videoCropLabel(VideoCropMode::Ratio1_1)), std::string("1:1"));
	CHECK_EQ(std::string(videoCropLabel(VideoCropMode::Ratio21_9)), std::string("21:9"));
	CHECK_EQ(std::string(videoCropLabel(VideoCropMode::Ratio235_1)), std::string("2.35:1"));
}

// ---------------------------------------------------------------------------
// videoAdjustmentEngineLabel
// ---------------------------------------------------------------------------

static void testVideoAdjustmentEngineLabel() {
	beginSuite("videoAdjustmentEngineLabel");

	CHECK_EQ(std::string(videoAdjustmentEngineLabel(VideoAdjustmentEngine::Auto)), std::string("auto"));
	CHECK_EQ(std::string(videoAdjustmentEngineLabel(VideoAdjustmentEngine::LibVlc)), std::string("libVLC"));
	CHECK_EQ(std::string(videoAdjustmentEngineLabel(VideoAdjustmentEngine::Shader)), std::string("shader"));
}

// ---------------------------------------------------------------------------
// videoOutputBackendLabel
// ---------------------------------------------------------------------------

static void testVideoOutputBackendLabel() {
	beginSuite("videoOutputBackendLabel");

	CHECK_EQ(std::string(videoOutputBackendLabel(VideoOutputBackend::Texture)), std::string("Texture"));
	CHECK_EQ(std::string(videoOutputBackendLabel(VideoOutputBackend::NativeWindow)), std::string("Native Window"));
	CHECK_EQ(std::string(videoOutputBackendLabel(VideoOutputBackend::D3D11Metadata)), std::string("D3D11 HDR Metadata"));
}

// ---------------------------------------------------------------------------
// preferredDecoderDeviceLabel
// ---------------------------------------------------------------------------

static void testPreferredDecoderDeviceLabel() {
	beginSuite("preferredDecoderDeviceLabel");

	CHECK_EQ(std::string(preferredDecoderDeviceLabel(PreferredDecoderDevice::Any)), std::string("Auto"));
	CHECK_EQ(std::string(preferredDecoderDeviceLabel(PreferredDecoderDevice::D3D11)), std::string("D3D11"));
	CHECK_EQ(std::string(preferredDecoderDeviceLabel(PreferredDecoderDevice::DXVA2)), std::string("DXVA2"));
	CHECK_EQ(std::string(preferredDecoderDeviceLabel(PreferredDecoderDevice::Nvdec)), std::string("NVDEC"));
	CHECK_EQ(std::string(preferredDecoderDeviceLabel(PreferredDecoderDevice::None)), std::string("None"));
}

// ---------------------------------------------------------------------------
// clampPackedRgbColor
// ---------------------------------------------------------------------------

static void testClampPackedRgbColor() {
	beginSuite("clampPackedRgbColor");

	// In-range.
	CHECK_EQ(clampPackedRgbColor(0x000000), 0x000000);
	CHECK_EQ(clampPackedRgbColor(0xFF0000), 0xFF0000);
	CHECK_EQ(clampPackedRgbColor(0xFFFFFF), 0xFFFFFF);
	CHECK_EQ(clampPackedRgbColor(0x123456), 0x123456);

	// Below minimum.
	CHECK_EQ(clampPackedRgbColor(-1), 0x000000);
	CHECK_EQ(clampPackedRgbColor(-100), 0x000000);

	// Above maximum.
	CHECK_EQ(clampPackedRgbColor(0x1000000), 0xFFFFFF);
	CHECK_EQ(clampPackedRgbColor(0x7FFFFFFF), 0xFFFFFF);
}

// ---------------------------------------------------------------------------
// visibleVideoSourceSize
// ---------------------------------------------------------------------------

static void testVisibleVideoSourceSize() {
	beginSuite("visibleVideoSourceSize");

	// Source smaller than render → source dimensions.
	{
		SimpleVideoSizeInfo info { 640, 480, 1920, 1080 };
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 640u);
		CHECK_EQ(h, 480u);
	}

	// Source larger than render → render dimensions.
	{
		SimpleVideoSizeInfo info { 3840, 2160, 1920, 1080 };
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 1920u);
		CHECK_EQ(h, 1080u);
	}

	// Same dimensions.
	{
		SimpleVideoSizeInfo info { 1920, 1080, 1920, 1080 };
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 1920u);
		CHECK_EQ(h, 1080u);
	}

	// Zero dimensions.
	{
		SimpleVideoSizeInfo info { 0, 0, 0, 0 };
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 0u);
		CHECK_EQ(h, 0u);
	}

	// Mixed: source wider, render taller.
	{
		SimpleVideoSizeInfo info { 1920, 480, 800, 1080 };
		auto [w, h] = visibleVideoSourceSize(info);
		CHECK_EQ(w, 800u);
		CHECK_EQ(h, 480u);
	}
}

// ---------------------------------------------------------------------------
// playbackModeFromString / playbackModeToString
// ---------------------------------------------------------------------------

static void testPlaybackModeFromString() {
	beginSuite("playbackModeFromString");

	CHECK_EQ(playbackModeFromString("repeat"), PlaybackMode::Repeat);
	CHECK_EQ(playbackModeFromString("loop"), PlaybackMode::Loop);
	CHECK_EQ(playbackModeFromString("default"), PlaybackMode::Default);
	CHECK_EQ(playbackModeFromString(""), PlaybackMode::Default);
	CHECK_EQ(playbackModeFromString("unknown"), PlaybackMode::Default);

	// Case insensitive.
	CHECK_EQ(playbackModeFromString("REPEAT"), PlaybackMode::Repeat);
	CHECK_EQ(playbackModeFromString("Loop"), PlaybackMode::Loop);
	CHECK_EQ(playbackModeFromString("Repeat"), PlaybackMode::Repeat);

	// With whitespace.
	CHECK_EQ(playbackModeFromString("  repeat  "), PlaybackMode::Repeat);
	CHECK_EQ(playbackModeFromString("\tloop\n"), PlaybackMode::Loop);
}

static void testPlaybackModeToString() {
	beginSuite("playbackModeToString");

	CHECK_EQ(playbackModeToString(PlaybackMode::Default), std::string("default"));
	CHECK_EQ(playbackModeToString(PlaybackMode::Repeat), std::string("repeat"));
	CHECK_EQ(playbackModeToString(PlaybackMode::Loop), std::string("loop"));
}

static void testPlaybackModeRoundTrip() {
	beginSuite("playbackMode round-trip");

	// All modes survive a to-string → from-string round trip.
	CHECK_EQ(playbackModeFromString(playbackModeToString(PlaybackMode::Default)), PlaybackMode::Default);
	CHECK_EQ(playbackModeFromString(playbackModeToString(PlaybackMode::Repeat)), PlaybackMode::Repeat);
	CHECK_EQ(playbackModeFromString(playbackModeToString(PlaybackMode::Loop)), PlaybackMode::Loop);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testVideoDeinterlaceModeLabel();
	testVideoDeinterlaceState();
	testVideoDeinterlaceFilterName();
	testVideoAspectRatioValue();
	testVideoAspectRatioLabel();
	testVideoCropRatio();
	testVideoCropLabel();
	testVideoAdjustmentEngineLabel();
	testVideoOutputBackendLabel();
	testPreferredDecoderDeviceLabel();
	testClampPackedRgbColor();
	testVisibleVideoSourceSize();
	testPlaybackModeFromString();
	testPlaybackModeToString();
	testPlaybackModeRoundTrip();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
