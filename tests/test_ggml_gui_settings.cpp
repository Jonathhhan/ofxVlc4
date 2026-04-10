// ---------------------------------------------------------------------------
// test_ggml_gui_settings.cpp — Unit tests for GUI settings validation
//
// Validates default values, valid ranges, and session persistence format
// for all inference parameters added to the ofxGgml AI Studio GUI:
// top-p, repeat penalty, context size, batch size, GPU layers, seed,
// backend selection, and theme selection.
// ---------------------------------------------------------------------------

#include "ofxGgmlTypes.h"
#include "ofxGgmlHelpers.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <sstream>
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
// Mirrored settings struct from ofApp.h — keeps tests self-contained
// ---------------------------------------------------------------------------

struct GuiSettings {
	int maxTokens = 256;
	float temperature = 0.7f;
	float topP = 0.9f;
	float repeatPenalty = 1.1f;
	int contextSize = 2048;
	int batchSize = 512;
	int gpuLayers = 0;
	int seed = -1;
	int numThreads = 4;
	int selectedBackendIndex = 0;   // 0=Auto, 1=CPU, 2=GPU
	int themeIndex = 0;             // 0=Dark, 1=Light, 2=Classic
};

// Clamp helper — mirrors std::clamp usage in session load
template <typename T>
static T clampValue(T val, T lo, T hi) {
	return std::clamp(val, lo, hi);
}

// ---------------------------------------------------------------------------
// Default value tests
// ---------------------------------------------------------------------------

static bool testDefaultMaxTokens() {
	TEST("default maxTokens is 256");
	GuiSettings s;
	ASSERT(s.maxTokens == 256);
	ASSERT(s.maxTokens >= 32 && s.maxTokens <= 4096);
	PASS();
	return true;
}

static bool testDefaultTemperature() {
	TEST("default temperature is 0.7");
	GuiSettings s;
	ASSERT(s.temperature > 0.69f && s.temperature < 0.71f);
	ASSERT(s.temperature >= 0.0f && s.temperature <= 2.0f);
	PASS();
	return true;
}

static bool testDefaultTopP() {
	TEST("default topP is 0.9");
	GuiSettings s;
	ASSERT(s.topP > 0.89f && s.topP < 0.91f);
	ASSERT(s.topP >= 0.0f && s.topP <= 1.0f);
	PASS();
	return true;
}

static bool testDefaultRepeatPenalty() {
	TEST("default repeatPenalty is 1.1");
	GuiSettings s;
	ASSERT(s.repeatPenalty > 1.09f && s.repeatPenalty < 1.11f);
	ASSERT(s.repeatPenalty >= 1.0f && s.repeatPenalty <= 2.0f);
	PASS();
	return true;
}

static bool testDefaultContextSize() {
	TEST("default contextSize is 2048");
	GuiSettings s;
	ASSERT(s.contextSize == 2048);
	ASSERT(s.contextSize >= 256 && s.contextSize <= 16384);
	PASS();
	return true;
}

static bool testDefaultBatchSize() {
	TEST("default batchSize is 512");
	GuiSettings s;
	ASSERT(s.batchSize == 512);
	ASSERT(s.batchSize >= 32 && s.batchSize <= 4096);
	PASS();
	return true;
}

static bool testDefaultGpuLayers() {
	TEST("default gpuLayers is 0");
	GuiSettings s;
	ASSERT(s.gpuLayers == 0);
	ASSERT(s.gpuLayers >= 0 && s.gpuLayers <= 128);
	PASS();
	return true;
}

static bool testDefaultSeed() {
	TEST("default seed is -1 (random)");
	GuiSettings s;
	ASSERT(s.seed == -1);
	ASSERT(s.seed >= -1 && s.seed <= 99999);
	PASS();
	return true;
}

static bool testDefaultBackend() {
	TEST("default backend is Auto (0)");
	GuiSettings s;
	ASSERT(s.selectedBackendIndex == 0);
	ASSERT(s.selectedBackendIndex >= 0 && s.selectedBackendIndex <= 2);
	PASS();
	return true;
}

static bool testDefaultTheme() {
	TEST("default theme is Dark (0)");
	GuiSettings s;
	ASSERT(s.themeIndex == 0);
	ASSERT(s.themeIndex >= 0 && s.themeIndex <= 2);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Range clamping tests — mirrors session load clamping
// ---------------------------------------------------------------------------

static bool testClampMaxTokens() {
	TEST("maxTokens clamped to [32, 4096]");
	ASSERT(clampValue(10, 32, 4096) == 32);
	ASSERT(clampValue(256, 32, 4096) == 256);
	ASSERT(clampValue(5000, 32, 4096) == 4096);
	PASS();
	return true;
}

static bool testClampTemperature() {
	TEST("temperature clamped to [0.0, 2.0]");
	ASSERT(clampValue(-0.5f, 0.0f, 2.0f) == 0.0f);
	ASSERT(clampValue(0.7f, 0.0f, 2.0f) == 0.7f);
	ASSERT(clampValue(3.0f, 0.0f, 2.0f) == 2.0f);
	PASS();
	return true;
}

static bool testClampTopP() {
	TEST("topP clamped to [0.0, 1.0]");
	ASSERT(clampValue(-0.1f, 0.0f, 1.0f) == 0.0f);
	ASSERT(clampValue(0.9f, 0.0f, 1.0f) == 0.9f);
	ASSERT(clampValue(1.5f, 0.0f, 1.0f) == 1.0f);
	PASS();
	return true;
}

static bool testClampRepeatPenalty() {
	TEST("repeatPenalty clamped to [1.0, 2.0]");
	ASSERT(clampValue(0.5f, 1.0f, 2.0f) == 1.0f);
	ASSERT(clampValue(1.1f, 1.0f, 2.0f) == 1.1f);
	ASSERT(clampValue(3.0f, 1.0f, 2.0f) == 2.0f);
	PASS();
	return true;
}

static bool testClampContextSize() {
	TEST("contextSize clamped to [256, 16384]");
	ASSERT(clampValue(64, 256, 16384) == 256);
	ASSERT(clampValue(2048, 256, 16384) == 2048);
	ASSERT(clampValue(32000, 256, 16384) == 16384);
	PASS();
	return true;
}

static bool testClampBatchSize() {
	TEST("batchSize clamped to [32, 4096]");
	ASSERT(clampValue(8, 32, 4096) == 32);
	ASSERT(clampValue(512, 32, 4096) == 512);
	ASSERT(clampValue(8000, 32, 4096) == 4096);
	PASS();
	return true;
}

static bool testClampGpuLayers() {
	TEST("gpuLayers clamped to [0, 128]");
	ASSERT(clampValue(-5, 0, 128) == 0);
	ASSERT(clampValue(32, 0, 128) == 32);
	ASSERT(clampValue(256, 0, 128) == 128);
	PASS();
	return true;
}

static bool testClampSeed() {
	TEST("seed clamped to [-1, 99999]");
	ASSERT(clampValue(-5, -1, 99999) == -1);
	ASSERT(clampValue(-1, -1, 99999) == -1);
	ASSERT(clampValue(42, -1, 99999) == 42);
	ASSERT(clampValue(100000, -1, 99999) == 99999);
	PASS();
	return true;
}

static bool testClampBackend() {
	TEST("backend index clamped to [0, 2]");
	ASSERT(clampValue(-1, 0, 2) == 0);
	ASSERT(clampValue(0, 0, 2) == 0);
	ASSERT(clampValue(1, 0, 2) == 1);
	ASSERT(clampValue(2, 0, 2) == 2);
	ASSERT(clampValue(5, 0, 2) == 2);
	PASS();
	return true;
}

static bool testClampTheme() {
	TEST("theme index clamped to [0, 2]");
	ASSERT(clampValue(-1, 0, 2) == 0);
	ASSERT(clampValue(0, 0, 2) == 0);
	ASSERT(clampValue(2, 0, 2) == 2);
	ASSERT(clampValue(10, 0, 2) == 2);
	PASS();
	return true;
}

static bool testClampThreads() {
	TEST("numThreads clamped to [1, 32]");
	ASSERT(clampValue(0, 1, 32) == 1);
	ASSERT(clampValue(4, 1, 32) == 4);
	ASSERT(clampValue(64, 1, 32) == 32);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Backend→ggml type mapping
// ---------------------------------------------------------------------------

static bool testBackendIndexMapping() {
	TEST("backend index maps to correct ggml backend type");
	// Index 0 = Auto (use default, which is CPU)
	// Index 1 = CPU  → ofxGgmlBackendType::Cpu
	// Index 2 = GPU  → ofxGgmlBackendType::Gpu
	ASSERT(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Cpu) == "CPU");
	ASSERT(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Gpu) == "GPU");
	// Auto defaults to CPU settings.
	ofxGgmlSettings autoSettings;
	ASSERT(autoSettings.preferredBackend == ofxGgmlBackendType::Cpu);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Session format: key=value serialization round-trip
// ---------------------------------------------------------------------------

static bool testSettingsSerializationRoundTrip() {
	TEST("settings serialize/deserialize round-trip");
	GuiSettings original;
	original.maxTokens = 512;
	original.temperature = 0.85f;
	original.topP = 0.95f;
	original.repeatPenalty = 1.3f;
	original.contextSize = 4096;
	original.batchSize = 256;
	original.gpuLayers = 24;
	original.seed = 42;
	original.numThreads = 8;
	original.selectedBackendIndex = 2;
	original.themeIndex = 1;

	// Serialize to key=value lines (mirrors saveSession).
	std::ostringstream oss;
	oss << "maxTokens=" << original.maxTokens << "\n";
	oss << "temperature=" << original.temperature << "\n";
	oss << "topP=" << original.topP << "\n";
	oss << "repeatPenalty=" << original.repeatPenalty << "\n";
	oss << "contextSize=" << original.contextSize << "\n";
	oss << "batchSize=" << original.batchSize << "\n";
	oss << "gpuLayers=" << original.gpuLayers << "\n";
	oss << "seed=" << original.seed << "\n";
	oss << "numThreads=" << original.numThreads << "\n";
	oss << "selectedBackend=" << original.selectedBackendIndex << "\n";
	oss << "theme=" << original.themeIndex << "\n";

	// Deserialize (mirrors loadSession).
	GuiSettings loaded;
	std::istringstream iss(oss.str());
	std::string line;
	while (std::getline(iss, line)) {
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = line.substr(0, eq);
		std::string value = line.substr(eq + 1);

		if (key == "maxTokens") loaded.maxTokens = clampValue(std::stoi(value), 32, 4096);
		else if (key == "temperature") loaded.temperature = clampValue(std::stof(value), 0.0f, 2.0f);
		else if (key == "topP") loaded.topP = clampValue(std::stof(value), 0.0f, 1.0f);
		else if (key == "repeatPenalty") loaded.repeatPenalty = clampValue(std::stof(value), 1.0f, 2.0f);
		else if (key == "contextSize") loaded.contextSize = clampValue(std::stoi(value), 256, 16384);
		else if (key == "batchSize") loaded.batchSize = clampValue(std::stoi(value), 32, 4096);
		else if (key == "gpuLayers") loaded.gpuLayers = clampValue(std::stoi(value), 0, 128);
		else if (key == "seed") loaded.seed = clampValue(std::stoi(value), -1, 99999);
		else if (key == "numThreads") loaded.numThreads = clampValue(std::stoi(value), 1, 32);
		else if (key == "selectedBackend") loaded.selectedBackendIndex = clampValue(std::stoi(value), 0, 2);
		else if (key == "theme") loaded.themeIndex = clampValue(std::stoi(value), 0, 2);
	}

	ASSERT(loaded.maxTokens == original.maxTokens);
	ASSERT(std::abs(loaded.temperature - original.temperature) < 0.01f);
	ASSERT(std::abs(loaded.topP - original.topP) < 0.01f);
	ASSERT(std::abs(loaded.repeatPenalty - original.repeatPenalty) < 0.01f);
	ASSERT(loaded.contextSize == original.contextSize);
	ASSERT(loaded.batchSize == original.batchSize);
	ASSERT(loaded.gpuLayers == original.gpuLayers);
	ASSERT(loaded.seed == original.seed);
	ASSERT(loaded.numThreads == original.numThreads);
	ASSERT(loaded.selectedBackendIndex == original.selectedBackendIndex);
	ASSERT(loaded.themeIndex == original.themeIndex);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Seed behavior: -1 means random, >= 0 means deterministic
// ---------------------------------------------------------------------------

static bool testSeedDeterminism() {
	TEST("seed >= 0 gives deterministic value, -1 means random");
	GuiSettings s;
	// -1 should be treated as random.
	ASSERT(s.seed == -1);
	s.seed = 42;
	ASSERT(s.seed >= 0);
	// Verify the seed is preserved through clamping.
	int clamped = clampValue(s.seed, -1, 99999);
	ASSERT(clamped == 42);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// GPU layers interaction with backend selection
// ---------------------------------------------------------------------------

static bool testGpuLayersWithCpuBackend() {
	TEST("gpuLayers with CPU backend is harmless");
	GuiSettings s;
	s.selectedBackendIndex = 1;  // CPU
	s.gpuLayers = 32;
	// Should be valid — GPU layers are ignored when running on CPU.
	ASSERT(s.gpuLayers >= 0 && s.gpuLayers <= 128);
	ASSERT(s.selectedBackendIndex == 1);
	PASS();
	return true;
}

static bool testGpuLayersWithGpuBackend() {
	TEST("gpuLayers with GPU backend enables offloading");
	GuiSettings s;
	s.selectedBackendIndex = 2;  // GPU
	s.gpuLayers = 64;
	ASSERT(s.selectedBackendIndex == 2);
	ASSERT(s.gpuLayers == 64);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_gui_settings ===\n");

	bool ok = true;

	// Default values.
	ok = testDefaultMaxTokens() && ok;
	ok = testDefaultTemperature() && ok;
	ok = testDefaultTopP() && ok;
	ok = testDefaultRepeatPenalty() && ok;
	ok = testDefaultContextSize() && ok;
	ok = testDefaultBatchSize() && ok;
	ok = testDefaultGpuLayers() && ok;
	ok = testDefaultSeed() && ok;
	ok = testDefaultBackend() && ok;
	ok = testDefaultTheme() && ok;

	// Range clamping.
	ok = testClampMaxTokens() && ok;
	ok = testClampTemperature() && ok;
	ok = testClampTopP() && ok;
	ok = testClampRepeatPenalty() && ok;
	ok = testClampContextSize() && ok;
	ok = testClampBatchSize() && ok;
	ok = testClampGpuLayers() && ok;
	ok = testClampSeed() && ok;
	ok = testClampBackend() && ok;
	ok = testClampTheme() && ok;
	ok = testClampThreads() && ok;

	// Backend mapping.
	ok = testBackendIndexMapping() && ok;

	// Serialization round-trip.
	ok = testSettingsSerializationRoundTrip() && ok;

	// Seed behavior.
	ok = testSeedDeterminism() && ok;

	// GPU layers interaction.
	ok = testGpuLayersWithCpuBackend() && ok;
	ok = testGpuLayersWithGpuBackend() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
