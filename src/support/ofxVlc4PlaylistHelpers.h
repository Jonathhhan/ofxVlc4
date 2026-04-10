#pragma once

// ---------------------------------------------------------------------------
// Pure playlist serialisation helpers — M3U and XSPF formats.
// No dependency on OF, GLFW, or VLC at all; only the C++ standard library.
//
// M3U reference : https://en.wikipedia.org/wiki/M3U
// XSPF reference: https://www.xspf.org/spec (XML Shareable Playlist Format)
// ---------------------------------------------------------------------------

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace ofxVlc4PlaylistHelpers {

// ---------------------------------------------------------------------------
// XML helpers (XSPF)
// ---------------------------------------------------------------------------

inline std::string xmlEscape(const std::string & input) {
	std::string out;
	out.reserve(input.size());
	for (const char ch : input) {
		switch (ch) {
			case '&':  out += "&amp;";  break;
			case '<':  out += "&lt;";   break;
			case '>':  out += "&gt;";   break;
			case '"':  out += "&quot;"; break;
			case '\'': out += "&apos;"; break;
			default:   out += ch;       break;
		}
	}
	return out;
}

inline std::string xmlUnescape(const std::string & input) {
	std::string out;
	out.reserve(input.size());
	const size_t len = input.size();
	for (size_t i = 0; i < len; ++i) {
		if (input[i] == '&') {
			const size_t remaining = len - i;
			if (remaining >= 5 && input.compare(i, 5, "&amp;") == 0) {
				out += '&'; i += 4;
			} else if (remaining >= 4 && input.compare(i, 4, "&lt;") == 0) {
				out += '<'; i += 3;
			} else if (remaining >= 4 && input.compare(i, 4, "&gt;") == 0) {
				out += '>'; i += 3;
			} else if (remaining >= 6 && input.compare(i, 6, "&quot;") == 0) {
				out += '"'; i += 5;
			} else if (remaining >= 6 && input.compare(i, 6, "&apos;") == 0) {
				out += '\''; i += 5;
			} else {
				out += '&';
			}
		} else {
			out += input[i];
		}
	}
	return out;
}

// Extract the text content between <tag> and </tag> in `line`.
// Returns empty string if the tags are not found.
inline std::string extractTagContent(const std::string & line, const std::string & tag) {
	const std::string openTag = "<" + tag + ">";
	const std::string closeTag = "</" + tag + ">";
	const size_t start = line.find(openTag);
	if (start == std::string::npos) {
		return {};
	}
	const size_t contentStart = start + openTag.size();
	const size_t end = line.find(closeTag, contentStart);
	if (end == std::string::npos) {
		return {};
	}
	return line.substr(contentStart, end - contentStart);
}

// ---------------------------------------------------------------------------
// Whitespace trim (local copy — avoids dependency on ofxVlc4Utils)
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
// URI detection (local copy — avoids dependency on ofxVlc4Utils)
// ---------------------------------------------------------------------------

inline bool isUri(const std::string & value) {
	const size_t schemeEnd = value.find("://");
	if (schemeEnd == std::string::npos || schemeEnd == 0) {
		return false;
	}
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

// ---------------------------------------------------------------------------
// Percent-encoding for file:// URIs (RFC 3986)
// ---------------------------------------------------------------------------

inline std::string percentEncodePath(const std::string & path) {
	std::ostringstream out;
	for (const unsigned char ch : path) {
		if ((ch >= 'A' && ch <= 'Z') ||
			(ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9') ||
			ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
			ch == '/' || ch == ':') {
			out << static_cast<char>(ch);
		} else {
			static constexpr char kHexDigits[] = "0123456789ABCDEF";
			out << '%' << kHexDigits[(ch >> 4) & 0x0F] << kHexDigits[ch & 0x0F];
		}
	}
	return out.str();
}

// ---------------------------------------------------------------------------
// Percent-decoding
// ---------------------------------------------------------------------------

inline int hexDigitValue(char ch) {
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
	if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
	return -1;
}

inline std::string percentDecode(const std::string & input) {
	std::string out;
	out.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '%' && i + 2 < input.size()) {
			const int hi = hexDigitValue(input[i + 1]);
			const int lo = hexDigitValue(input[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out += static_cast<char>((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		out += input[i];
	}
	return out;
}

// ---------------------------------------------------------------------------
// M3U serialisation
// ---------------------------------------------------------------------------

inline std::string serializeM3U(const std::vector<std::string> & items) {
	std::ostringstream out;
	out << "#EXTM3U\n";
	for (const auto & item : items) {
		// #EXTINF with unknown duration (-1) and the raw entry as title
		out << "#EXTINF:-1," << item << "\n";
		out << item << "\n";
	}
	return out.str();
}

// ---------------------------------------------------------------------------
// M3U deserialisation
// ---------------------------------------------------------------------------

inline std::vector<std::string> deserializeM3U(const std::string & content) {
	std::vector<std::string> items;
	std::istringstream stream(content);
	std::string line;
	while (std::getline(stream, line)) {
		// Strip trailing \r (Windows line endings)
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const std::string trimmed = trimWhitespace(line);
		// Skip empty lines and comments/directives
		if (trimmed.empty() || trimmed[0] == '#') {
			continue;
		}
		items.push_back(trimmed);
	}
	return items;
}

// ---------------------------------------------------------------------------
// XSPF serialisation
// ---------------------------------------------------------------------------

inline std::string serializeXSPF(
	const std::vector<std::string> & items,
	const std::string & title = "Playlist") {

	std::ostringstream out;
	out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	out << "<playlist xmlns=\"http://xspf.org/ns/0/\" xmlns:vlc=\"http://www.videolan.org/vlc/playlist/ns/0/\" version=\"1\">\n";
	out << "  <title>" << xmlEscape(title) << "</title>\n";
	out << "  <trackList>\n";
	for (const auto & item : items) {
		out << "    <track>\n";
		out << "      <location>" << xmlEscape(item) << "</location>\n";
		out << "    </track>\n";
	}
	out << "  </trackList>\n";
	out << "</playlist>\n";
	return out.str();
}

// ---------------------------------------------------------------------------
// XSPF deserialisation
// A lightweight parser that extracts <location> elements from <track> blocks.
// This handles the XSPF files that VLC produces without requiring a full XML
// parser dependency.
// ---------------------------------------------------------------------------

inline std::vector<std::string> deserializeXSPF(const std::string & content) {
	std::vector<std::string> items;
	std::istringstream stream(content);
	std::string line;
	while (std::getline(stream, line)) {
		const std::string location = extractTagContent(line, "location");
		if (!location.empty()) {
			items.push_back(xmlUnescape(location));
		}
	}
	return items;
}

} // namespace ofxVlc4PlaylistHelpers
