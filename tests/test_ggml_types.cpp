// ---------------------------------------------------------------------------
// test_ggml_types.cpp — Unit tests for ofxGgmlTypes.h
//
// Validates POD default values, enum coverage, and struct layout for the
// ofxGgml type system.
// ---------------------------------------------------------------------------

#include "ofxGgmlTypes.h"

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
// Backend type enum coverage
// ---------------------------------------------------------------------------

static bool testBackendTypeEnum() {
	TEST("BackendType enum values");
	ASSERT(static_cast<int>(ofxGgmlBackendType::Cpu) == 0);
	ASSERT(static_cast<int>(ofxGgmlBackendType::Gpu) == 1);
	ASSERT(static_cast<int>(ofxGgmlBackendType::IntegratedGpu) == 2);
	ASSERT(static_cast<int>(ofxGgmlBackendType::Accelerator) == 3);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Tensor element type enum coverage
// ---------------------------------------------------------------------------

static bool testTypeEnum() {
	TEST("Type enum values");
	ASSERT(static_cast<int>(ofxGgmlType::F32) == 0);
	ASSERT(static_cast<int>(ofxGgmlType::F16) == 1);
	ASSERT(static_cast<int>(ofxGgmlType::Q4_0) == 2);
	ASSERT(static_cast<int>(ofxGgmlType::Q4_1) == 3);
	ASSERT(static_cast<int>(ofxGgmlType::Q5_0) == 6);
	ASSERT(static_cast<int>(ofxGgmlType::Q5_1) == 7);
	ASSERT(static_cast<int>(ofxGgmlType::Q8_0) == 8);
	ASSERT(static_cast<int>(ofxGgmlType::Q8_1) == 9);
	ASSERT(static_cast<int>(ofxGgmlType::I8) == 24);
	ASSERT(static_cast<int>(ofxGgmlType::I16) == 25);
	ASSERT(static_cast<int>(ofxGgmlType::I32) == 26);
	ASSERT(static_cast<int>(ofxGgmlType::I64) == 27);
	ASSERT(static_cast<int>(ofxGgmlType::F64) == 28);
	ASSERT(static_cast<int>(ofxGgmlType::BF16) == 30);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Unary op enum coverage
// ---------------------------------------------------------------------------

static bool testUnaryOpEnum() {
	TEST("UnaryOp enum values");
	// Verify all 14 operations are present and have unique values.
	int values[] = {
		static_cast<int>(ofxGgmlUnaryOp::Abs),
		static_cast<int>(ofxGgmlUnaryOp::Sgn),
		static_cast<int>(ofxGgmlUnaryOp::Neg),
		static_cast<int>(ofxGgmlUnaryOp::Step),
		static_cast<int>(ofxGgmlUnaryOp::Tanh),
		static_cast<int>(ofxGgmlUnaryOp::Elu),
		static_cast<int>(ofxGgmlUnaryOp::Relu),
		static_cast<int>(ofxGgmlUnaryOp::SiLU),
		static_cast<int>(ofxGgmlUnaryOp::Gelu),
		static_cast<int>(ofxGgmlUnaryOp::GeluQuick),
		static_cast<int>(ofxGgmlUnaryOp::Sigmoid),
		static_cast<int>(ofxGgmlUnaryOp::HardSwish),
		static_cast<int>(ofxGgmlUnaryOp::HardSigmoid),
		static_cast<int>(ofxGgmlUnaryOp::Exp)
	};
	// Check all values are distinct.
	for (int i = 0; i < 14; i++) {
		for (int j = i + 1; j < 14; j++) {
			ASSERT(values[i] != values[j]);
		}
	}
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// State enum coverage
// ---------------------------------------------------------------------------

static bool testStateEnum() {
	TEST("State enum values");
	ASSERT(static_cast<int>(ofxGgmlState::Uninitialized) != static_cast<int>(ofxGgmlState::Ready));
	ASSERT(static_cast<int>(ofxGgmlState::Ready) != static_cast<int>(ofxGgmlState::Computing));
	ASSERT(static_cast<int>(ofxGgmlState::Computing) != static_cast<int>(ofxGgmlState::Error));
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Settings defaults
// ---------------------------------------------------------------------------

static bool testSettingsDefaults() {
	TEST("Settings defaults");
	ofxGgmlSettings s;
	ASSERT(s.threads == 0);
	ASSERT(s.preferredBackend == ofxGgmlBackendType::Cpu);
	ASSERT(s.graphSize == 2048);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// DeviceInfo defaults
// ---------------------------------------------------------------------------

static bool testDeviceInfoDefaults() {
	TEST("DeviceInfo defaults");
	ofxGgmlDeviceInfo d;
	ASSERT(d.name.empty());
	ASSERT(d.description.empty());
	ASSERT(d.type == ofxGgmlBackendType::Cpu);
	ASSERT(d.memoryFree == 0);
	ASSERT(d.memoryTotal == 0);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// ComputeResult defaults
// ---------------------------------------------------------------------------

static bool testComputeResultDefaults() {
	TEST("ComputeResult defaults");
	ofxGgmlComputeResult r;
	ASSERT(r.success == false);
	ASSERT(r.elapsedMs == 0.0f);
	ASSERT(r.error.empty());
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// LogCallback type
// ---------------------------------------------------------------------------

static bool testLogCallbackType() {
	TEST("LogCallback is invocable");
	int called = 0;
	ofxGgmlLogCallback cb = [&called](int level, const std::string & msg) {
		called++;
		(void)level;
		(void)msg;
	};
	cb(0, "test");
	ASSERT(called == 1);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_types ===\n");

	bool ok = true;
	ok = testBackendTypeEnum() && ok;
	ok = testTypeEnum() && ok;
	ok = testUnaryOpEnum() && ok;
	ok = testStateEnum() && ok;
	ok = testSettingsDefaults() && ok;
	ok = testDeviceInfoDefaults() && ok;
	ok = testComputeResultDefaults() && ok;
	ok = testLogCallbackType() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
