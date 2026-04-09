#pragma once

// ---------------------------------------------------------------------------
// Pure media-library helpers extracted from MediaLibrary.cpp for testability.
// These functions perform string matching, extension normalisation, and
// metadata filtering with no VLC/OF/GLFW runtime dependencies.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ofxVlc4MediaLibraryHelpers {

// ---------------------------------------------------------------------------
// Text trimming (local version to avoid dependency on ofxVlc4Utils)
// ---------------------------------------------------------------------------

inline std::string trimWhitespace(const std::string & value) {
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, (last - first) + 1);
}

// ---------------------------------------------------------------------------
// toLower (local version)
// ---------------------------------------------------------------------------

inline std::string toLower(const std::string & input) {
	std::string out = input;
	for (char & ch : out) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}
	return out;
}

// ---------------------------------------------------------------------------
// normalizeExtensions — canonicalise a list of file extensions into a set
// with leading dots and lowercase.
// ---------------------------------------------------------------------------

inline std::set<std::string> normalizeExtensions(
	const std::vector<std::string> & extensions) {

	std::set<std::string> out;
	for (const auto & extension : extensions) {
		std::string value = toLower(trimWhitespace(extension));
		if (value.empty()) {
			continue;
		}
		if (!value.empty() && value[0] != '.') {
			value = "." + value;
		}
		out.insert(value);
	}
	return out;
}

// ---------------------------------------------------------------------------
// appendMetadataValue — add a non-empty metadata pair to a vector.
// ---------------------------------------------------------------------------

inline void appendMetadataValue(
	std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & label,
	const std::string & value) {

	const std::string trimmedValue = trimWhitespace(value);
	if (!trimmedValue.empty()) {
		metadata.emplace_back(label, trimmedValue);
	}
}

// ---------------------------------------------------------------------------
// hasDetailedTrackMetadata — returns true if any known codec/resolution/
// rate metadata field has a non-empty value.
// ---------------------------------------------------------------------------

inline bool hasDetailedTrackMetadata(
	const std::vector<std::pair<std::string, std::string>> & metadata) {

	for (const auto & [label, value] : metadata) {
		if (value.empty()) {
			continue;
		}

		if (label == "Video Codec" ||
			label == "Audio Codec" ||
			label == "Subtitle Codec" ||
			label == "Video Resolution" ||
			label == "Frame Rate" ||
			label == "Audio Channels" ||
			label == "Audio Rate") {
			return true;
		}
	}

	return false;
}

} // namespace ofxVlc4MediaLibraryHelpers
