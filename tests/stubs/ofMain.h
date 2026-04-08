#pragma once
// Minimal stub of openFrameworks headers used by ofxVlc4Utils.h.
#include <string>
#include <sstream>

template <typename T>
inline std::string ofToString(const T & value) {
	std::ostringstream ss;
	ss << value;
	return ss.str();
}

struct ofFilePath {
	static std::string getFileName(const std::string & path) { return path; }
	static std::string getAbsolutePath(const std::string & path) { return path; }
};

class ofFbo {
public:
	bool isAllocated() const { return false; }
	void begin() {}
	void end() {}
};

inline void ofClear(int, int, int, int) {}
