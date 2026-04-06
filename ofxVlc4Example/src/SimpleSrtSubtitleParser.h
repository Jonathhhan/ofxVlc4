#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct SimpleSrtSubtitleCue {
	int startMs = 0;
	int endMs = 0;
	std::string text;
};

class SimpleSrtSubtitleParser {
public:
	static bool parseFile(const std::string & path, std::vector<SimpleSrtSubtitleCue> & cuesOut, std::string & errorOut) {
		cuesOut.clear();
		errorOut.clear();

		std::ifstream input(path, std::ios::binary);
		if (!input.is_open()) {
			errorOut = "Failed to open subtitle file.";
			return false;
		}

		std::ostringstream stream;
		stream << input.rdbuf();
		if (!input.good() && !input.eof()) {
			errorOut = "Failed to read subtitle file.";
			return false;
		}

		std::string content = stream.str();
		if (content.size() >= 3 &&
			static_cast<unsigned char>(content[0]) == 0xEF &&
			static_cast<unsigned char>(content[1]) == 0xBB &&
			static_cast<unsigned char>(content[2]) == 0xBF) {
			content.erase(0, 3);
		}

		normalizeLineEndings(content);

		size_t cursor = 0;
		while (cursor < content.size()) {
			const size_t blockEnd = content.find("\n\n", cursor);
			std::string block = content.substr(cursor, blockEnd == std::string::npos ? std::string::npos : blockEnd - cursor);
			cursor = (blockEnd == std::string::npos) ? content.size() : blockEnd + 2;

			trim(block);
			if (block.empty()) {
				continue;
			}

			std::istringstream blockStream(block);
			std::string line;
			std::vector<std::string> lines;
			while (std::getline(blockStream, line)) {
				trimRight(line);
				lines.push_back(line);
			}

			if (lines.empty()) {
				continue;
			}

			size_t timingIndex = 0;
			if (lines.size() > 1 && lines[0].find("-->") == std::string::npos) {
				timingIndex = 1;
			}
			if (timingIndex >= lines.size()) {
				continue;
			}

			const std::string & timingLine = lines[timingIndex];
			const size_t arrowPos = timingLine.find("-->");
			if (arrowPos == std::string::npos) {
				continue;
			}

			const std::string startText = trimCopy(timingLine.substr(0, arrowPos));
			const std::string endText = trimCopy(timingLine.substr(arrowPos + 3));

			SimpleSrtSubtitleCue cue;
			if (!parseTimecode(startText, cue.startMs) || !parseTimecode(endText, cue.endMs) || cue.endMs < cue.startMs) {
				continue;
			}

			std::ostringstream textStream;
			for (size_t i = timingIndex + 1; i < lines.size(); ++i) {
				if (lines[i].empty()) {
					continue;
				}
				if (textStream.tellp() > 0) {
					textStream << '\n';
				}
				textStream << lines[i];
			}

			cue.text = trimCopy(textStream.str());
			if (!cue.text.empty()) {
				cuesOut.push_back(std::move(cue));
			}
		}

		if (cuesOut.empty()) {
			errorOut = "No SRT subtitle cues were parsed.";
			return false;
		}

		return true;
	}

private:
	static void normalizeLineEndings(std::string & value) {
		value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
	}

	static void trimLeft(std::string & value) {
		value.erase(
			value.begin(),
			std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); }));
	}

	static void trimRight(std::string & value) {
		value.erase(
			std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base(),
			value.end());
	}

	static void trim(std::string & value) {
		trimLeft(value);
		trimRight(value);
	}

	static std::string trimCopy(std::string value) {
		trim(value);
		return value;
	}

	static bool parseTimecode(const std::string & rawText, int & timeMsOut) {
		std::string text = trimCopy(rawText);
		const size_t spacePos = text.find(' ');
		if (spacePos != std::string::npos) {
			text = text.substr(0, spacePos);
		}

		int hours = 0;
		int minutes = 0;
		int seconds = 0;
		int millis = 0;
		char colon1 = 0;
		char colon2 = 0;
		char comma = 0;
		std::istringstream parser(text);
		parser >> hours >> colon1 >> minutes >> colon2 >> seconds >> comma >> millis;
		if (!parser || colon1 != ':' || colon2 != ':' || (comma != ',' && comma != '.')) {
			return false;
		}

		timeMsOut = (((hours * 60) + minutes) * 60 + seconds) * 1000 + millis;
		return true;
	}
};
