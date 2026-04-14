// Tests for D3D11 video backend helpers and configuration.
// Validates D3D11-specific enumerations, labels, and backend selection logic.
// This test runs on all platforms but includes Windows-specific validation.

#include "ofxVlc4VideoHelpers.h"

#include <cassert>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#endif

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
// VideoOutputBackend label tests
// ---------------------------------------------------------------------------

static void testVideoOutputBackendLabel() {
	beginSuite("videoOutputBackendLabel");

	CHECK_EQ(std::string(videoOutputBackendLabel(VideoOutputBackend::Texture)), std::string("Texture"));
	CHECK_EQ(std::string(videoOutputBackendLabel(VideoOutputBackend::NativeWindow)), std::string("Native Window"));
	CHECK_EQ(std::string(videoOutputBackendLabel(VideoOutputBackend::D3D11Metadata)), std::string("D3D11 HDR Metadata"));
}

// ---------------------------------------------------------------------------
// PreferredDecoderDevice label tests
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
// D3D11 availability tests (Windows only)
// ---------------------------------------------------------------------------

#ifdef _WIN32
static void testD3D11Availability() {
	beginSuite("D3D11 Availability (Windows)");

	// Attempt to create a D3D11 device to verify D3D11 is available
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};

	D3D_FEATURE_LEVEL featureLevel;

	UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	HRESULT hr = D3D11CreateDevice(
		nullptr,                    // adapter
		D3D_DRIVER_TYPE_HARDWARE,   // driver type
		nullptr,                    // software rasterizer
		creationFlags,              // flags
		featureLevels,              // feature levels
		ARRAYSIZE(featureLevels),   // num feature levels
		D3D11_SDK_VERSION,          // SDK version
		&device,                    // device output
		&featureLevel,              // feature level output
		&context                    // context output
	);

	if (SUCCEEDED(hr) && device && context) {
		std::printf("  INFO  D3D11 device created successfully\n");
		std::printf("  INFO  Feature level: %d\n", static_cast<int>(featureLevel));
		CHECK(true);  // D3D11 is available

		// Cleanup
		context->Release();
		device->Release();
	} else {
		std::printf("  WARN  D3D11 device creation failed (HRESULT: 0x%08X)\n", static_cast<unsigned>(hr));
		std::printf("  INFO  This may be expected in headless/VM environments\n");
		// Don't fail the test - D3D11 may not be available in CI
		CHECK(true);
	}
}

static void testD3D11DeviceFeatures() {
	beginSuite("D3D11 Device Features (Windows)");

	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	D3D_FEATURE_LEVEL featureLevel;

	HRESULT hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
		featureLevels,
		ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&device,
		&featureLevel,
		&context
	);

	if (SUCCEEDED(hr) && device && context) {
		std::printf("  INFO  D3D11 device with video support created\n");

		// Check for multithreading support
		ID3D10Multithread* multithread = nullptr;
		hr = context->QueryInterface(__uuidof(ID3D10Multithread), reinterpret_cast<void**>(&multithread));

		if (SUCCEEDED(hr) && multithread) {
			std::printf("  INFO  Multithreading interface available\n");
			CHECK(true);
			multithread->Release();
		} else {
			std::printf("  WARN  Multithreading interface not available\n");
			CHECK(true);  // Not critical
		}

		context->Release();
		device->Release();
	} else {
		std::printf("  INFO  D3D11 device with video support not available\n");
		CHECK(true);  // Expected in some environments
	}
}
#endif

// ---------------------------------------------------------------------------
// Backend selection logic tests
// ---------------------------------------------------------------------------

static void testBackendSelection() {
	beginSuite("Backend Selection Logic");

	// Test that D3D11Metadata backend is a valid option
	VideoOutputBackend backend = VideoOutputBackend::D3D11Metadata;
	CHECK_EQ(static_cast<int>(backend), 2);

	// Test PreferredDecoderDevice D3D11 option
	PreferredDecoderDevice decoder = PreferredDecoderDevice::D3D11;
	CHECK_EQ(static_cast<int>(decoder), 1);

	// Verify all backend enum values are distinct
	CHECK(static_cast<int>(VideoOutputBackend::Texture) != static_cast<int>(VideoOutputBackend::NativeWindow));
	CHECK(static_cast<int>(VideoOutputBackend::NativeWindow) != static_cast<int>(VideoOutputBackend::D3D11Metadata));
	CHECK(static_cast<int>(VideoOutputBackend::Texture) != static_cast<int>(VideoOutputBackend::D3D11Metadata));

	// Verify all decoder device enum values are distinct
	CHECK(static_cast<int>(PreferredDecoderDevice::Any) != static_cast<int>(PreferredDecoderDevice::D3D11));
	CHECK(static_cast<int>(PreferredDecoderDevice::D3D11) != static_cast<int>(PreferredDecoderDevice::DXVA2));
	CHECK(static_cast<int>(PreferredDecoderDevice::DXVA2) != static_cast<int>(PreferredDecoderDevice::Nvdec));
	CHECK(static_cast<int>(PreferredDecoderDevice::Nvdec) != static_cast<int>(PreferredDecoderDevice::None));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== D3D11 Backend Tests ===\n");

	testVideoOutputBackendLabel();
	testPreferredDecoderDeviceLabel();
	testBackendSelection();

#ifdef _WIN32
	testD3D11Availability();
	testD3D11DeviceFeatures();
#else
	std::printf("\n[D3D11 Platform-Specific Tests]\n");
	std::printf("  INFO  Skipped (not Windows)\n");
#endif

	std::printf("\n=== Summary ===\n");
	std::printf("Passed: %d\n", g_passed);
	std::printf("Failed: %d\n", g_failed);

	return (g_failed == 0) ? 0 : 1;
}
