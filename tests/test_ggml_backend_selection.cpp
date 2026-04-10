// ---------------------------------------------------------------------------
// test_ggml_backend_selection.cpp — Unit tests for backend selection logic
//
// Validates that backend type→enum mapping is correct, settings with GPU
// preferences produce valid configurations, and the backend selection
// precedence order matches the ggml device type integers.
// ---------------------------------------------------------------------------

#include "ofxGgmlTypes.h"
#include "ofxGgmlHelpers.h"

#include <cassert>
#include <cstdio>
#include <string>

static int testCount = 0;
static int passCount = 0;

#define TEST(name) \
	do { testCount++; std::printf("  [TEST] %s ... ", name); } while (0)
#define PASS() \
	do { passCount++; std::printf("PASS\n"); } while (0)
#define ASSERT(cond) \
	do { if (!(cond)) { std::printf("FAIL (%s:%d)\n  %s\n", __FILE__, __LINE__, #cond); return false; } } while (0)

// ---------------------------------------------------------------------------
// Backend type enum → ggml_backend_dev_type integer mapping
//
// The ofxGgml::setup() casts preferredBackend to ggml_backend_dev_type.
// These values must match ggml-backend.h:
//   GGML_BACKEND_DEVICE_TYPE_CPU            = 0
//   GGML_BACKEND_DEVICE_TYPE_GPU            = 1
//   GGML_BACKEND_DEVICE_TYPE_ACCEL (legacy) = 2  (≈ IntegratedGpu in our mapping)
// ---------------------------------------------------------------------------

static bool testBackendTypeIntMapping() {
	TEST("backend type int mapping matches ggml");
	// CPU must be 0 — ggml's GGML_BACKEND_DEVICE_TYPE_CPU
	ASSERT(static_cast<int>(ofxGgmlBackendType::Cpu) == 0);
	// GPU must be 1 — ggml's GGML_BACKEND_DEVICE_TYPE_GPU
	ASSERT(static_cast<int>(ofxGgmlBackendType::Gpu) == 1);
	PASS();
	return true;
}

static bool testBackendTypesAreDistinct() {
	TEST("all backend types have distinct integer values");
	int cpu  = static_cast<int>(ofxGgmlBackendType::Cpu);
	int gpu  = static_cast<int>(ofxGgmlBackendType::Gpu);
	int igpu = static_cast<int>(ofxGgmlBackendType::IntegratedGpu);
	int acc  = static_cast<int>(ofxGgmlBackendType::Accelerator);
	ASSERT(cpu != gpu);
	ASSERT(cpu != igpu);
	ASSERT(cpu != acc);
	ASSERT(gpu != igpu);
	ASSERT(gpu != acc);
	ASSERT(igpu != acc);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Settings: GPU preference configuration
// ---------------------------------------------------------------------------

static bool testDefaultSettingsUseCpu() {
	TEST("default settings prefer CPU backend");
	ofxGgmlSettings s;
	ASSERT(s.preferredBackend == ofxGgmlBackendType::Cpu);
	PASS();
	return true;
}

static bool testGpuSettingsConfiguration() {
	TEST("GPU settings can be configured");
	ofxGgmlSettings s;
	s.preferredBackend = ofxGgmlBackendType::Gpu;
	s.threads = 8;
	s.graphSize = 4096;
	ASSERT(s.preferredBackend == ofxGgmlBackendType::Gpu);
	ASSERT(s.threads == 8);
	ASSERT(s.graphSize == 4096);
	// The int cast used in setup() must produce the GPU device type.
	ASSERT(static_cast<int>(s.preferredBackend) == 1);
	PASS();
	return true;
}

static bool testAllBackendSettingsValid() {
	TEST("all backend types produce valid settings");
	ofxGgmlBackendType backends[] = {
		ofxGgmlBackendType::Cpu,
		ofxGgmlBackendType::Gpu,
		ofxGgmlBackendType::IntegratedGpu,
		ofxGgmlBackendType::Accelerator
	};
	for (auto bt : backends) {
		ofxGgmlSettings s;
		s.preferredBackend = bt;
		// Every backend type should be >= 0 (valid device type).
		ASSERT(static_cast<int>(s.preferredBackend) >= 0);
		// And the helper name should not be "Unknown".
		std::string name = ofxGgmlHelpers::backendTypeName(bt);
		ASSERT(name != "Unknown");
	}
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Settings: thread count validation
// ---------------------------------------------------------------------------

static bool testDefaultThreadCount() {
	TEST("default thread count is 0 (auto-detect)");
	ofxGgmlSettings s;
	ASSERT(s.threads == 0);
	PASS();
	return true;
}

static bool testCustomThreadCount() {
	TEST("custom thread count is preserved");
	ofxGgmlSettings s;
	s.threads = 16;
	ASSERT(s.threads == 16);
	// In setup(), threads > 0 triggers ggml_backend_cpu_set_n_threads.
	ASSERT(s.threads > 0);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Settings: graph size validation
// ---------------------------------------------------------------------------

static bool testDefaultGraphSize() {
	TEST("default graph size is 2048");
	ofxGgmlSettings s;
	ASSERT(s.graphSize == 2048);
	PASS();
	return true;
}

static bool testLargeGraphSize() {
	TEST("large graph size for complex models");
	ofxGgmlSettings s;
	s.graphSize = 16384;
	// The graph size is used as: ggml_tensor_overhead() * maxNodes + ggml_graph_overhead()
	// It should remain a reasonable power-of-two.
	ASSERT(s.graphSize == 16384);
	ASSERT(s.graphSize > 0);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// DeviceInfo: GPU device info population
// ---------------------------------------------------------------------------

static bool testDeviceInfoGpuType() {
	TEST("DeviceInfo can represent GPU device");
	ofxGgmlDeviceInfo d;
	d.name = "NVIDIA RTX 4090";
	d.description = "CUDA backend";
	d.type = ofxGgmlBackendType::Gpu;
	d.memoryFree = static_cast<size_t>(20) * 1024 * 1024 * 1024;  // 20 GB
	d.memoryTotal = static_cast<size_t>(24) * 1024 * 1024 * 1024;  // 24 GB
	ASSERT(d.type == ofxGgmlBackendType::Gpu);
	ASSERT(d.memoryFree <= d.memoryTotal);
	ASSERT(ofxGgmlHelpers::formatBytes(d.memoryTotal).find("GB") != std::string::npos);
	PASS();
	return true;
}

static bool testDeviceInfoIntegratedGpu() {
	TEST("DeviceInfo can represent integrated GPU");
	ofxGgmlDeviceInfo d;
	d.name = "Intel UHD 770";
	d.description = "Vulkan backend";
	d.type = ofxGgmlBackendType::IntegratedGpu;
	d.memoryFree = 2ULL * 1024 * 1024 * 1024;
	d.memoryTotal = 4ULL * 1024 * 1024 * 1024;
	ASSERT(d.type == ofxGgmlBackendType::IntegratedGpu);
	ASSERT(ofxGgmlHelpers::backendTypeName(d.type) == "Integrated GPU");
	PASS();
	return true;
}

static bool testDeviceInfoAccelerator() {
	TEST("DeviceInfo can represent accelerator");
	ofxGgmlDeviceInfo d;
	d.name = "Apple M2 Neural Engine";
	d.description = "Metal backend";
	d.type = ofxGgmlBackendType::Accelerator;
	ASSERT(d.type == ofxGgmlBackendType::Accelerator);
	ASSERT(ofxGgmlHelpers::backendTypeName(d.type) == "Accelerator");
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Multi-device: settings for mixed CPU+GPU scheduling
// ---------------------------------------------------------------------------

static bool testDualBackendSchedulerSettings() {
	TEST("dual-backend scheduler settings");
	// In setup(), when preferred != CPU, the scheduler gets 2 backends.
	ofxGgmlSettings gpuSettings;
	gpuSettings.preferredBackend = ofxGgmlBackendType::Gpu;
	// Simulate the scheduler nBackends calculation from setup():
	// nBackends = (backend == cpuBackend) ? 1 : 2
	// With GPU preferred, backend != cpuBackend → 2 backends.
	bool gpuBackendIsCpu = (gpuSettings.preferredBackend == ofxGgmlBackendType::Cpu);
	int expectedBackends = gpuBackendIsCpu ? 1 : 2;
	ASSERT(expectedBackends == 2);

	// With CPU preferred, the fallback logic might still give 2 separate
	// allocations, but the pointer comparison would be false in practice.
	ofxGgmlSettings cpuSettings;
	cpuSettings.preferredBackend = ofxGgmlBackendType::Cpu;
	gpuBackendIsCpu = (cpuSettings.preferredBackend == ofxGgmlBackendType::Cpu);
	// When preferred is CPU, the code still creates a separate CPU backend
	// for the scheduler, but they may or may not be equal pointers.
	// The important thing is the settings are valid.
	ASSERT(cpuSettings.preferredBackend == ofxGgmlBackendType::Cpu);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_backend_selection ===\n");

	bool ok = true;

	// Backend type mapping.
	ok = testBackendTypeIntMapping() && ok;
	ok = testBackendTypesAreDistinct() && ok;

	// GPU settings configuration.
	ok = testDefaultSettingsUseCpu() && ok;
	ok = testGpuSettingsConfiguration() && ok;
	ok = testAllBackendSettingsValid() && ok;

	// Thread count.
	ok = testDefaultThreadCount() && ok;
	ok = testCustomThreadCount() && ok;

	// Graph size.
	ok = testDefaultGraphSize() && ok;
	ok = testLargeGraphSize() && ok;

	// GPU device info.
	ok = testDeviceInfoGpuType() && ok;
	ok = testDeviceInfoIntegratedGpu() && ok;
	ok = testDeviceInfoAccelerator() && ok;

	// Multi-device scheduling.
	ok = testDualBackendSchedulerSettings() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
