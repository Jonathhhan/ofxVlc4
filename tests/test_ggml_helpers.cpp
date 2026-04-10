// ---------------------------------------------------------------------------
// test_ggml_helpers.cpp — Unit tests for ofxGgmlHelpers.h
//
// Validates all pure inline helper functions: typeName, backendTypeName,
// elementSize, stateName, formatBytes.
// ---------------------------------------------------------------------------

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
// typeName — every ofxGgmlType should produce a non-"unknown" label
// ---------------------------------------------------------------------------

static bool testTypeName() {
	TEST("typeName — all known types");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::F32) == "f32");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::F16) == "f16");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::Q4_0) == "q4_0");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::Q4_1) == "q4_1");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::Q5_0) == "q5_0");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::Q5_1) == "q5_1");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::Q8_0) == "q8_0");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::Q8_1) == "q8_1");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::I8) == "i8");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::I16) == "i16");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::I32) == "i32");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::I64) == "i64");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::F64) == "f64");
	ASSERT(ofxGgmlHelpers::typeName(ofxGgmlType::BF16) == "bf16");
	PASS();
	return true;
}

static bool testTypeNameUnknown() {
	TEST("typeName — unknown type");
	ASSERT(ofxGgmlHelpers::typeName(static_cast<ofxGgmlType>(999)) == "unknown");
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// backendTypeName
// ---------------------------------------------------------------------------

static bool testBackendTypeName() {
	TEST("backendTypeName — all known types");
	ASSERT(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Cpu) == "CPU");
	ASSERT(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Gpu) == "GPU");
	ASSERT(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::IntegratedGpu) == "Integrated GPU");
	ASSERT(ofxGgmlHelpers::backendTypeName(ofxGgmlBackendType::Accelerator) == "Accelerator");
	PASS();
	return true;
}

static bool testBackendTypeNameUnknown() {
	TEST("backendTypeName — unknown type");
	ASSERT(ofxGgmlHelpers::backendTypeName(static_cast<ofxGgmlBackendType>(99)) == "Unknown");
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// elementSize
// ---------------------------------------------------------------------------

static bool testElementSize() {
	TEST("elementSize — unquantized types");
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::F32) == 4);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::F16) == 2);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::BF16) == 2);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::F64) == 8);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::I8) == 1);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::I16) == 2);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::I32) == 4);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::I64) == 8);
	PASS();
	return true;
}

static bool testElementSizeQuantized() {
	TEST("elementSize — quantized types return 0");
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::Q4_0) == 0);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::Q4_1) == 0);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::Q5_0) == 0);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::Q5_1) == 0);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::Q8_0) == 0);
	ASSERT(ofxGgmlHelpers::elementSize(ofxGgmlType::Q8_1) == 0);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// stateName
// ---------------------------------------------------------------------------

static bool testStateName() {
	TEST("stateName — all states");
	ASSERT(ofxGgmlHelpers::stateName(ofxGgmlState::Uninitialized) == "Uninitialized");
	ASSERT(ofxGgmlHelpers::stateName(ofxGgmlState::Ready) == "Ready");
	ASSERT(ofxGgmlHelpers::stateName(ofxGgmlState::Computing) == "Computing");
	ASSERT(ofxGgmlHelpers::stateName(ofxGgmlState::Error) == "Error");
	PASS();
	return true;
}

static bool testStateNameUnknown() {
	TEST("stateName — unknown state");
	ASSERT(ofxGgmlHelpers::stateName(static_cast<ofxGgmlState>(42)) == "Unknown");
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// formatBytes
// ---------------------------------------------------------------------------

static bool testFormatBytesSmall() {
	TEST("formatBytes — small values");
	std::string result = ofxGgmlHelpers::formatBytes(0);
	ASSERT(result.find("0.00 B") != std::string::npos);
	result = ofxGgmlHelpers::formatBytes(512);
	ASSERT(result.find("512.00 B") != std::string::npos);
	PASS();
	return true;
}

static bool testFormatBytesKB() {
	TEST("formatBytes — kilobytes");
	std::string result = ofxGgmlHelpers::formatBytes(1024);
	ASSERT(result.find("KB") != std::string::npos);
	ASSERT(result.find("1.00") != std::string::npos);
	result = ofxGgmlHelpers::formatBytes(2560); // 2.5 KB
	ASSERT(result.find("KB") != std::string::npos);
	PASS();
	return true;
}

static bool testFormatBytesMB() {
	TEST("formatBytes — megabytes");
	std::string result = ofxGgmlHelpers::formatBytes(1024 * 1024);
	ASSERT(result.find("MB") != std::string::npos);
	ASSERT(result.find("1.00") != std::string::npos);
	PASS();
	return true;
}

static bool testFormatBytesGB() {
	TEST("formatBytes — gigabytes");
	size_t oneGB = static_cast<size_t>(1024) * 1024 * 1024;
	std::string result = ofxGgmlHelpers::formatBytes(oneGB);
	ASSERT(result.find("GB") != std::string::npos);
	ASSERT(result.find("1.00") != std::string::npos);
	PASS();
	return true;
}

static bool testFormatBytesTB() {
	TEST("formatBytes — terabytes");
	size_t oneTB = static_cast<size_t>(1024) * 1024 * 1024 * 1024;
	std::string result = ofxGgmlHelpers::formatBytes(oneTB);
	ASSERT(result.find("TB") != std::string::npos);
	ASSERT(result.find("1.00") != std::string::npos);
	PASS();
	return true;
}

static bool testFormatBytesLargeTB() {
	TEST("formatBytes — large TB doesn't overflow unit");
	size_t fiveTB = static_cast<size_t>(5) * 1024 * 1024 * 1024 * 1024;
	std::string result = ofxGgmlHelpers::formatBytes(fiveTB);
	ASSERT(result.find("TB") != std::string::npos);
	ASSERT(result.find("5.00") != std::string::npos);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_helpers ===\n");

	bool ok = true;
	ok = testTypeName() && ok;
	ok = testTypeNameUnknown() && ok;
	ok = testBackendTypeName() && ok;
	ok = testBackendTypeNameUnknown() && ok;
	ok = testElementSize() && ok;
	ok = testElementSizeQuantized() && ok;
	ok = testStateName() && ok;
	ok = testStateNameUnknown() && ok;
	ok = testFormatBytesSmall() && ok;
	ok = testFormatBytesKB() && ok;
	ok = testFormatBytesMB() && ok;
	ok = testFormatBytesGB() && ok;
	ok = testFormatBytesTB() && ok;
	ok = testFormatBytesLargeTB() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
