#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Pure mux helper functions — no dependency on OF, GLFW, or VLC.
// Used by ofxVlc4Recorder.cpp and testable in isolation.
// ---------------------------------------------------------------------------

namespace ofxVlc4MuxHelpers {

// Number of consecutive polls where the file size must be unchanged before
// the file is considered fully written and ready to read.
static constexpr int kFileStableCheckCount = 3;

// Polling interval used when waiting for a file to appear or be removed.
static constexpr int kFilePollIntervalMs = 50;

// Escapes single-quotes in a path and normalises it for use in a VLC sout
// stream specification.
inline std::string normalizeSoutPath(const std::string & path) {
	std::string normalized = std::filesystem::path(path).lexically_normal().generic_string();
	size_t position = 0;
	while ((position = normalized.find('\'', position)) != std::string::npos) {
		normalized.insert(position, "\\");
		position += 2;
	}
	return normalized;
}

// Converts an absolute or relative filesystem path to a percent-encoded
// file:/// URI suitable for passing to libVLC.
inline std::string pathToFileUri(const std::string & path) {
	const std::string genericPath = std::filesystem::absolute(path).lexically_normal().generic_string();
	std::ostringstream uri;
	// On Unix, generic_string() begins with '/' which already provides the
	// third slash of the authority separator, so emit only "file://".
	// On Windows the path starts with a drive letter (e.g. "C:/…"), so we
	// must add the extra '/' to form the correct "file:///C:/…" URI.
	uri << ((!genericPath.empty() && genericPath[0] == '/') ? "file://" : "file:///");
	for (const unsigned char ch : genericPath) {
		if ((ch >= 'A' && ch <= 'Z') ||
			(ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9') ||
			ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
			ch == '/' || ch == ':') {
			uri << static_cast<char>(ch);
		} else {
			static constexpr char kHexDigits[] = "0123456789ABCDEF";
			uri << '%' << kHexDigits[(ch >> 4) & 0x0F] << kHexDigits[ch & 0x0F];
		}
	}
	return uri.str();
}

// Polls the filesystem until the file at `path` exists, has a non-zero size,
// and its size has been stable for at least kMinStableCount consecutive checks.
// Returns true if the file appears ready before the deadline, false otherwise.
inline bool waitForRecordingFile(const std::string & path, uint64_t timeoutMs) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	uintmax_t previousSize = 0;
	int stableCount = 0;
	while (std::chrono::steady_clock::now() < deadline) {
		std::error_code error;
		if (std::filesystem::exists(path, error)) {
			const uintmax_t currentSize = std::filesystem::file_size(path, error);
			if (!error && currentSize > 0) {
				if (currentSize == previousSize) {
					if (++stableCount >= kFileStableCheckCount) {
						return true;
					}
				} else {
					stableCount = 0;
					previousSize = currentSize;
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kFilePollIntervalMs));
	}
	return false;
}

// Overload that additionally accepts an optional cancellation flag.  The wait
// exits early (returning false) as soon as `*cancelRequested` is true.
inline bool waitForRecordingFile(
	const std::string & path,
	uint64_t timeoutMs,
	const std::atomic<bool> * cancelRequested) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	uintmax_t previousSize = 0;
	int stableCount = 0;
	while (std::chrono::steady_clock::now() < deadline) {
		if (cancelRequested && cancelRequested->load(std::memory_order_acquire)) {
			return false;
		}

		std::error_code error;
		if (std::filesystem::exists(path, error)) {
			const uintmax_t currentSize = std::filesystem::file_size(path, error);
			if (!error && currentSize > 0) {
				if (currentSize == previousSize) {
					if (++stableCount >= kFileStableCheckCount) {
						return true;
					}
				} else {
					stableCount = 0;
					previousSize = currentSize;
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kFilePollIntervalMs));
	}
	return false;
}

// Retries std::filesystem::remove until it succeeds or the deadline is
// reached.  Returns true if the file no longer exists afterwards.
inline bool removeRecordingFile(const std::string & path, uint64_t timeoutMs) {
	if (path.empty()) {
		return true;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	do {
		std::error_code error;
		if (!std::filesystem::exists(path, error)) {
			return !error;
		}
		if (std::filesystem::remove(path, error)) {
			return true;
		}
		if (!error && !std::filesystem::exists(path, error)) {
			return !error;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kFilePollIntervalMs));
	} while (std::chrono::steady_clock::now() < deadline);

	std::error_code error;
	return !std::filesystem::exists(path, error);
}

} // namespace ofxVlc4MuxHelpers
