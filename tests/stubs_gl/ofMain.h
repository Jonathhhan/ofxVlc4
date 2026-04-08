#pragma once

// Minimal OF stub for test_gl.
// Provides the symbols that ofxVlc4Utils.h needs, with observable
// call counters on ofFbo and ofClear so the GL tests can verify behaviour.

#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// VLC types required by ofxVlc4Utils.h
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
// ofFilePath stub
// ---------------------------------------------------------------------------

namespace ofFilePath {
inline std::string getFileName(const std::string & path) {
	const auto pos = path.find_last_of("/\\");
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

inline std::string getAbsolutePath(const std::string & path) {
	if (path.empty()) return path;
	std::error_code ec;
	const auto absolute = std::filesystem::absolute(path, ec);
	return ec ? path : absolute.string();
}
} // namespace ofFilePath

// ---------------------------------------------------------------------------
// ofFbo stub with observable call counters
// ---------------------------------------------------------------------------

class ofFbo {
public:
	bool isAllocated() const { return _allocated; }
	void setAllocated(bool v) { _allocated = v; }
	void begin() { ++beginCount; }
	void end() { ++endCount; }

	bool _allocated = false;
	int beginCount = 0;
	int endCount = 0;
};

// ---------------------------------------------------------------------------
// ofClear stub with call counter
// ---------------------------------------------------------------------------

inline int g_ofClearCallCount = 0;

inline void ofClear(float, float, float, float) {
	++g_ofClearCallCount;
}
