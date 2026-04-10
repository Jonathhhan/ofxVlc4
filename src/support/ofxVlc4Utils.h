#pragma once

#include "ofMain.h"
#include "GLFW/glfw3.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace ofxVlc4Utils {
inline std::string trimWhitespace(const std::string & value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return "";
	}

	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

inline bool isUri(const std::string & value) {
	// Find "://" — absent means it cannot be a URI.
	const size_t schemeEnd = value.find("://");
	if (schemeEnd == std::string::npos || schemeEnd == 0) {
		return false;
	}
	// RFC 3986 §3.1: a URI scheme is ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
	// and must start with a letter.  Any non-scheme character (e.g. a path
	// separator '/') before "://" means the string is a local path, not a URI.
	if (!std::isalpha(static_cast<unsigned char>(value[0]))) {
		return false;
	}
	for (size_t i = 1; i < schemeEnd; ++i) {
		const unsigned char c = static_cast<unsigned char>(value[i]);
		if (!std::isalpha(c) && !std::isdigit(c) && c != '+' && c != '-' && c != '.') {
			return false;
		}
	}
	return true;
}

inline std::string fileNameFromUri(const std::string & uri) {
	const size_t queryPos = uri.find_first_of("?#");
	const std::string withoutQuery = uri.substr(0, queryPos);
	const size_t slashPos = withoutQuery.find_last_of('/');
	if (slashPos == std::string::npos) {
		return withoutQuery;
	}
	if (slashPos + 1 >= withoutQuery.size()) {
		return {};
	}
	return withoutQuery.substr(slashPos + 1);
}

inline std::string mediaLabelForPath(const std::string & path) {
	if (path.empty()) {
		return "";
	}

	if (isUri(path)) {
		const std::string label = fileNameFromUri(path);
		return label.empty() ? path : label;
	}

	return ofFilePath::getFileName(path);
}

inline std::string sanitizeFileStem(std::string value) {
	if (value.empty()) {
		return "snapshot";
	}

	for (char & ch : value) {
		switch (ch) {
		case '<':
		case '>':
		case ':':
		case '"':
		case '/':
		case '\\':
		case '|':
		case '?':
		case '*':
			ch = '_';
			break;
		default:
			break;
		}
	}

	value = trimWhitespace(value);
	return value.empty() ? "snapshot" : value;
}

inline std::string fallbackIndexedLabel(const std::string & prefix, int index, const std::string & name) {
	const std::string trimmedName = trimWhitespace(name);
	if (!trimmedName.empty()) {
		return trimmedName;
	}

	return prefix + " " + ofToString(index + 1);
}

inline std::string formatProgramName(int programId, const std::string & name) {
	const std::string trimmedName = trimWhitespace(name);
	if (!trimmedName.empty()) {
		return trimmedName;
	}

	return "Program " + ofToString(programId);
}

inline std::string normalizeOptionalPath(const std::string & value) {
	const std::string trimmed = trimWhitespace(value);
	if (trimmed.empty() || isUri(trimmed)) {
		return trimmed;
	}

	return ofFilePath::getAbsolutePath(trimmed);
}

inline bool hasCurrentGlContext() {
	return glfwGetCurrentContext() != nullptr;
}

inline void clearAllocatedFbo(ofFbo & fbo) {
	if (!fbo.isAllocated() || !hasCurrentGlContext()) {
		return;
	}

	fbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.end();
}

inline bool isStoppedOrIdleState(libvlc_state_t state) {
	return state == libvlc_Stopped ||
		state == libvlc_NothingSpecial;
}

inline bool isTransientPlaybackState(libvlc_state_t state) {
	return state == libvlc_Opening || state == libvlc_Buffering || state == libvlc_Stopping;
}

inline bool setInputHandlingEnabled(
	libvlc_media_player_t * mediaPlayer,
	bool & currentValue,
	bool enabled,
	const char * enabledMessage,
	void (*apply)(libvlc_media_player_t *, unsigned),
	const std::function<void(const std::string &)> & logVerbose) {
	if (currentValue == enabled) {
		return false;
	}

	currentValue = enabled;
	if (mediaPlayer) {
		apply(mediaPlayer, enabled ? 1u : 0u);
	}
	logVerbose(std::string(enabledMessage) + (enabled ? "enabled." : "disabled."));
	return true;
}

inline bool nearlyEqual(float a, float b, float epsilon = 0.0001f) {
	return std::abs(a - b) <= epsilon;
}

inline std::string formatAdjustmentValue(float value, int precision = 1, const char * suffix = nullptr) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(precision) << value;
	if (suffix && *suffix) {
		stream << suffix;
	}
	return stream.str();
}

inline std::vector<std::string> parseFilterChainEntries(const std::string & filterChain) {
	std::vector<std::string> filters;
	std::stringstream stream(filterChain);
	std::string entry;
	while (std::getline(stream, entry, ':')) {
		entry = trimWhitespace(entry);
		if (entry.empty()) {
			continue;
		}
		if (std::find(filters.begin(), filters.end(), entry) == filters.end()) {
			filters.push_back(std::move(entry));
		}
	}
	return filters;
}

inline std::string joinFilterChainEntries(const std::vector<std::string> & filters) {
	std::ostringstream stream;
	bool first = true;
	for (const std::string & filter : filters) {
		const std::string trimmed = trimWhitespace(filter);
		if (trimmed.empty()) {
			continue;
		}
		if (!first) {
			stream << ":";
		}
		stream << trimmed;
		first = false;
	}
	return stream.str();
}

inline std::string readTextFileIfPresent(const std::string & path) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input.is_open()) {
		return "";
	}

	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}
} // namespace ofxVlc4Utils
