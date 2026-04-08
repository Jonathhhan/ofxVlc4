#pragma once

// Minimal stub for openFrameworks ofMain.h.
// Provides only the symbols referenced by ofxVlc4Utils.h.

#include <algorithm>
#include <sstream>
#include <string>

// String utilities
inline int ofStringTimesInString(const std::string & str, const std::string & substr) {
	if (substr.empty()) {
		return 0;
	}
	int count = 0;
	size_t pos = 0;
	while ((pos = str.find(substr, pos)) != std::string::npos) {
		++count;
		pos += substr.size();
	}
	return count;
}

template <typename T>
inline std::string ofToString(const T & value) {
	std::ostringstream ss;
	ss << value;
	return ss.str();
}

// File path utilities
struct ofFilePath {
	static std::string getFileName(const std::string & path) {
		const auto pos = path.find_last_of("/\\");
		return pos == std::string::npos ? path : path.substr(pos + 1);
	}

	static std::string getAbsolutePath(const std::string & path) {
		// Simplified: return unchanged for test purposes
		return path;
	}
};

// ofFbo stub (clearAllocatedFbo uses isAllocated/begin/end)
struct ofFbo {
	bool isAllocated() const { return false; }
	void begin() const {}
	void end() const {}
};

// ofClear stub
inline void ofClear(float /*r*/, float /*g*/, float /*b*/, float /*a*/) {}
