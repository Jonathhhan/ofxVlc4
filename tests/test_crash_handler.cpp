// ---------------------------------------------------------------------------
// test_crash_handler.cpp
// Tests for the ofxVlc4CrashHandler utility header.
// ---------------------------------------------------------------------------

#include "ofxVlc4CrashHandler.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int gTests = 0;
static int gPassed = 0;

static void check(bool condition, const char * label) {
	++gTests;
	if (condition) {
		++gPassed;
	} else {
		std::cerr << "FAIL: " << label << "\n";
	}
}

#define CHECK(cond) check((cond), #cond)

// Read the entire contents of a file into a string.
static std::string readFile(const char * path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

// ---------------------------------------------------------------------------
// Test: install returns true on first call, false on second
// ---------------------------------------------------------------------------
static void testInstallIdempotent() {
	// The handler was already installed in main(), so isInstalled() should
	// be true and a second install() should return false.
	CHECK(ofxVlc4CrashHandler::isInstalled());
	CHECK(!ofxVlc4CrashHandler::install("unused.log"));
}

// ---------------------------------------------------------------------------
// Test: signalName and signalDescription coverage
// ---------------------------------------------------------------------------
static void testSignalNames() {
	CHECK(std::strcmp(ofxVlc4CrashHandler::detail::signalName(SIGSEGV), "SIGSEGV") == 0);
	CHECK(std::strcmp(ofxVlc4CrashHandler::detail::signalName(SIGABRT), "SIGABRT") == 0);
	CHECK(std::strcmp(ofxVlc4CrashHandler::detail::signalName(SIGFPE), "SIGFPE") == 0);
	CHECK(std::strcmp(ofxVlc4CrashHandler::detail::signalName(SIGILL), "SIGILL") == 0);
#ifndef _WIN32
	CHECK(std::strcmp(ofxVlc4CrashHandler::detail::signalName(SIGBUS), "SIGBUS") == 0);
#endif
	CHECK(std::strcmp(ofxVlc4CrashHandler::detail::signalName(9999), "UNKNOWN") == 0);

	// Descriptions should be non-empty.
	CHECK(std::strlen(ofxVlc4CrashHandler::detail::signalDescription(SIGSEGV)) > 0);
	CHECK(std::strlen(ofxVlc4CrashHandler::detail::signalDescription(9999)) > 0);
}

#ifndef _WIN32
// ---------------------------------------------------------------------------
// Test: unsignedToStr converts correctly
// ---------------------------------------------------------------------------
static void testUnsignedToStr() {
	char buf[32];
	ofxVlc4CrashHandler::detail::unsignedToStr(0, buf, sizeof(buf));
	CHECK(std::strcmp(buf, "0") == 0);

	ofxVlc4CrashHandler::detail::unsignedToStr(12345, buf, sizeof(buf));
	CHECK(std::strcmp(buf, "12345") == 0);

	ofxVlc4CrashHandler::detail::unsignedToStr(999999999ULL, buf, sizeof(buf));
	CHECK(std::strcmp(buf, "999999999") == 0);
}

// ---------------------------------------------------------------------------
// Test: crashing child writes to the crash log file
// ---------------------------------------------------------------------------
static void testCrashWritesLogFile() {
	const char * logPath = "/tmp/ofxvlc4_test_crash.log";

	// Remove any stale log.
	std::remove(logPath);

	pid_t pid = fork();
	if (pid == 0) {
		// Child process — install handler and crash.
		// Reset the installed flag so we can install in the child.
		ofxVlc4CrashHandler::detail::gInstalled.store(false);
		ofxVlc4CrashHandler::detail::gCrashLogPath[0] = '\0';
		ofxVlc4CrashHandler::install(logPath);

		// Trigger SIGABRT.
		std::abort();
		_exit(1); // Should not reach here.
	}

	// Parent — wait for the child to terminate.
	int status = 0;
	waitpid(pid, &status, 0);

	// The child should have been killed by SIGABRT.
	CHECK(WIFSIGNALED(status));

	// The crash-log file should exist and contain the signal name.
	const std::string contents = readFile(logPath);
	CHECK(contents.find("SIGABRT") != std::string::npos);
	CHECK(contents.find("[ofxVlc4]") != std::string::npos);

	// Clean up.
	std::remove(logPath);
}
#endif

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
	// Install the handler once so isInstalled() tests work.
	ofxVlc4CrashHandler::install("/tmp/ofxvlc4_test_noop.log");

	testInstallIdempotent();
	testSignalNames();
#ifndef _WIN32
	testUnsignedToStr();
	testCrashWritesLogFile();
#endif

	std::remove("/tmp/ofxvlc4_test_noop.log");

	std::cout << gPassed << "/" << gTests << " tests passed.\n";
	return (gPassed == gTests) ? 0 : 1;
}
