#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace TestRunner {

struct TestCase {
	std::string name;
	std::function<void()> fn;
};

inline std::vector<TestCase> & registry() {
	static std::vector<TestCase> cases;
	return cases;
}

inline int & failCount() {
	static int count = 0;
	return count;
}

inline int & passCount() {
	static int count = 0;
	return count;
}

inline std::string & currentTest() {
	static std::string name;
	return name;
}

inline void registerTest(const std::string & name, std::function<void()> fn) {
	registry().push_back({ name, fn });
}

inline int runAll() {
	int total = 0;
	int failed = 0;
	for (auto & tc : registry()) {
		currentTest() = tc.name;
		try {
			tc.fn();
		} catch (const std::exception & e) {
			std::cerr << "  [EXCEPTION] " << tc.name << ": " << e.what() << "\n";
			++failCount();
		} catch (...) {
			std::cerr << "  [EXCEPTION] " << tc.name << ": unknown\n";
			++failCount();
		}
		++total;
	}
	failed = failCount();
	std::cout << "\n=== Results: " << (total - failed) << "/" << total << " passed";
	if (failed > 0) {
		std::cout << " (" << failed << " FAILED)";
	}
	std::cout << " ===\n";
	return failed > 0 ? 1 : 0;
}

inline void recordFailure(const std::string & expr, const char * file, int line) {
	std::cerr << "  [FAIL] " << currentTest() << "\n"
			  << "         " << expr << "\n"
			  << "         at " << file << ":" << line << "\n";
	++failCount();
}

} // namespace TestRunner

#define TEST(name)                                                                    \
	static void _test_##name();                                                       \
	namespace {                                                                       \
	struct _reg_##name {                                                              \
		_reg_##name() { TestRunner::registerTest(#name, _test_##name); }              \
	} _reg_instance_##name;                                                           \
	}                                                                                 \
	static void _test_##name()

#define EXPECT_TRUE(expr)                                                             \
	do {                                                                              \
		if (!(expr)) {                                                                \
			TestRunner::recordFailure("Expected true: " #expr, __FILE__, __LINE__);   \
		}                                                                             \
	} while (0)

#define EXPECT_FALSE(expr)                                                            \
	do {                                                                              \
		if ((expr)) {                                                                 \
			TestRunner::recordFailure("Expected false: " #expr, __FILE__, __LINE__);  \
		}                                                                             \
	} while (0)

#define EXPECT_EQ(a, b)                                                               \
	do {                                                                              \
		if (!((a) == (b))) {                                                          \
			std::ostringstream _oss;                                                  \
			_oss << "Expected equal: " #a " == " #b " (got " << (a) << " vs " << (b) << ")"; \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)

#define EXPECT_NE(a, b)                                                               \
	do {                                                                              \
		if ((a) == (b)) {                                                             \
			TestRunner::recordFailure("Expected not equal: " #a " != " #b, __FILE__, __LINE__); \
		}                                                                             \
	} while (0)

#define EXPECT_NEAR(a, b, eps)                                                        \
	do {                                                                              \
		if (std::abs((a) - (b)) > (eps)) {                                            \
			std::ostringstream _oss;                                                  \
			_oss << "Expected near: |" #a " - " #b "| <= " #eps                      \
				 << " (got |" << (a) << " - " << (b) << "| = " << std::abs((a)-(b)) << ")"; \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)

#define EXPECT_STREQ(a, b)                                                            \
	do {                                                                              \
		if (std::string(a) != std::string(b)) {                                       \
			std::ostringstream _oss;                                                  \
			_oss << "Expected equal strings: " #a " == " #b                          \
				 << " (got \"" << (a) << "\" vs \"" << (b) << "\")";                  \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)

#define EXPECT_STRNE(a, b)                                                            \
	do {                                                                              \
		if (std::string(a) == std::string(b)) {                                       \
			std::ostringstream _oss;                                                  \
			_oss << "Expected different strings: " #a " != " #b;                     \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)

#define EXPECT_GT(a, b)                                                               \
	do {                                                                              \
		if (!((a) > (b))) {                                                           \
			std::ostringstream _oss;                                                  \
			_oss << "Expected " #a " > " #b " (got " << (a) << " vs " << (b) << ")"; \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)

#define EXPECT_GE(a, b)                                                               \
	do {                                                                              \
		if (!((a) >= (b))) {                                                          \
			std::ostringstream _oss;                                                  \
			_oss << "Expected " #a " >= " #b " (got " << (a) << " vs " << (b) << ")"; \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)

#define EXPECT_LE(a, b)                                                               \
	do {                                                                              \
		if (!((a) <= (b))) {                                                          \
			std::ostringstream _oss;                                                  \
			_oss << "Expected " #a " <= " #b " (got " << (a) << " vs " << (b) << ")"; \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)

#define EXPECT_LT(a, b)                                                               \
	do {                                                                              \
		if (!((a) < (b))) {                                                           \
			std::ostringstream _oss;                                                  \
			_oss << "Expected " #a " < " #b " (got " << (a) << " vs " << (b) << ")"; \
			TestRunner::recordFailure(_oss.str(), __FILE__, __LINE__);                \
		}                                                                             \
	} while (0)
