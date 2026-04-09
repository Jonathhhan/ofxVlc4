#pragma once

// ---------------------------------------------------------------------------
// Pure recording path-building helpers extracted from ofxVlc4Recorder.cpp for
// testability.  These functions depend only on trimWhitespace + OF path/time
// stubs; no VLC runtime needed.
// ---------------------------------------------------------------------------

#include <string>

namespace ofxVlc4RecordingHelpers {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

struct RecordingOutputPaths {
	std::string audioPath;
	std::string videoPath;
};

// ---------------------------------------------------------------------------
// Text trimming (mirrors the anonymous-namespace variant in Recorder.cpp)
// ---------------------------------------------------------------------------

inline std::string trimRecorderText(const std::string & value) {
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}

	const size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, (last - first) + 1);
}

// ---------------------------------------------------------------------------
// Recording output stem — builds "<name>-<timestamp>" from user input.
//
// ofFilePath::getFileExt / removeExt and ofGetTimestampString are expected
// to be available (either from OF at runtime, or from test stubs).
// ---------------------------------------------------------------------------

inline std::string buildRecordingOutputStem(
	const std::string & name,
	std::string * extensionOut = nullptr) {

	std::string normalizedName = trimRecorderText(name);
	if (normalizedName.empty()) normalizedName = "recording";

	const std::string detectedExtension = ofFilePath::getFileExt(normalizedName);
	if (extensionOut) *extensionOut = detectedExtension;
	if (!detectedExtension.empty()) normalizedName = ofFilePath::removeExt(normalizedName);

	return normalizedName + ofGetTimestampString("-%Y-%m-%d-%H-%M-%S");
}

// ---------------------------------------------------------------------------
// Recording output path — appends detected or fallback extension.
// ---------------------------------------------------------------------------

inline std::string buildRecordingOutputPath(
	const std::string & name,
	const std::string & fallbackExtension) {

	std::string detectedExtension;
	const std::string outputStem = buildRecordingOutputStem(name, &detectedExtension);
	return !detectedExtension.empty()
		? outputStem + "." + detectedExtension
		: outputStem + fallbackExtension;
}

// ---------------------------------------------------------------------------
// Recording output paths — audio + video pair.
// ---------------------------------------------------------------------------

inline RecordingOutputPaths buildRecordingOutputPaths(
	const std::string & name,
	const std::string & videoFallbackExtension = ".ts") {

	const std::string outputStem = buildRecordingOutputStem(name);
	return { outputStem + ".wav", outputStem + videoFallbackExtension };
}

} // namespace ofxVlc4RecordingHelpers
