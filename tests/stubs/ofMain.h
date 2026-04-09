#pragma once

// ---------------------------------------------------------------------------
// Minimal stub of openFrameworks (ofMain.h) for the ofxVlc4 unit tests.
// Provides only the symbols that ofxVlc4Utils.h needs for compilation.
// ---------------------------------------------------------------------------

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

// ---------------------------------------------------------------------------
// VLC types needed by ofxVlc4Utils.h
// ---------------------------------------------------------------------------

typedef enum {
	libvlc_NothingSpecial = 0,
	libvlc_Opening,
	libvlc_Buffering,
	libvlc_Playing,
	libvlc_Paused,
	libvlc_Stopped,
	libvlc_Ended,
	libvlc_Error,
	libvlc_Stopping
} libvlc_state_t;

typedef struct libvlc_media_player_t libvlc_media_player_t;

// ---------------------------------------------------------------------------
// OF utility functions used by ofxVlc4Utils.h
// ---------------------------------------------------------------------------

inline int ofStringTimesInString(const std::string & big, const std::string & small) {
	if (small.empty()) return 0;
	int count = 0;
	size_t pos = 0;
	while ((pos = big.find(small, pos)) != std::string::npos) {
		++count;
		pos += small.size();
	}
	return count;
}

template<typename T>
inline std::string ofToString(T value) {
	std::ostringstream ss;
	ss << value;
	return ss.str();
}

// ---------------------------------------------------------------------------
// ofBuffer stub (used by readTextFileIfPresent via ofBufferFromFile)
// ---------------------------------------------------------------------------

class ofBuffer {
public:
	ofBuffer() = default;
	explicit ofBuffer(const std::string & text) : _data(text) {}

	std::string getText() const { return _data; }
	std::size_t size() const { return _data.size(); }
	const char * getData() const { return _data.data(); }

private:
	std::string _data;
};

inline ofBuffer ofBufferFromFile(const std::string & path, bool /*binary*/ = false) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input.is_open()) {
		return ofBuffer();
	}
	std::ostringstream contents;
	contents << input.rdbuf();
	return ofBuffer(contents.str());
}

// ---------------------------------------------------------------------------
// ofFilePath stub
// ---------------------------------------------------------------------------

namespace ofFilePath {
inline std::string getFileName(const std::string & path) {
	const auto pos = path.find_last_of("/\\");
	if (pos == std::string::npos) return path;
	return path.substr(pos + 1);
}

inline std::string getAbsolutePath(const std::string & path) {
	if (path.empty()) return path;
	std::error_code ec;
	const auto absolute = std::filesystem::absolute(path, ec);
	return ec ? path : absolute.string();
}
} // namespace ofFilePath

// ---------------------------------------------------------------------------
// ofFbo stub (used by clearAllocatedFbo)
// ---------------------------------------------------------------------------

class ofFbo {
public:
	bool isAllocated() const { return _allocated; }
	void setAllocated(bool v) { _allocated = v; }
	void begin() {}
	void end() {}

private:
	bool _allocated = false;
};

inline void ofClear(float, float, float, float) {}
