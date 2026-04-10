// ---------------------------------------------------------------------------
// test_ggml_session.cpp — Unit tests for ofxGgml GUI session persistence
//
// Validates the session escape/unescape round-trip and save/load format.
// The session format is a line-oriented key=value text file with escaped
// newlines, tabs, backslashes, and carriage returns.
// ---------------------------------------------------------------------------

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int testCount = 0;
static int passCount = 0;

#define TEST(name) \
	do { testCount++; std::printf("  [TEST] %s ... ", name); } while (0)
#define PASS() \
	do { passCount++; std::printf("PASS\n"); } while (0)
#define ASSERT(cond) \
	do { if (!(cond)) { std::printf("FAIL (%s:%d)\n  %s\n", __FILE__, __LINE__, #cond); return false; } } while (0)

// ---------------------------------------------------------------------------
// Extracted session escape/unescape — mirrors ofApp.cpp
// ---------------------------------------------------------------------------

static std::string escapeSessionText(const std::string & text) {
	std::string result;
	result.reserve(text.size() + text.size() / 8);
	for (char c : text) {
		switch (c) {
			case '\n': result += "\\n";  break;
			case '\r': result += "\\r";  break;
			case '\t': result += "\\t";  break;
			case '\\': result += "\\\\"; break;
			default:   result += c;      break;
		}
	}
	return result;
}

static std::string unescapeSessionText(const std::string & text) {
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\\' && i + 1 < text.size()) {
			switch (text[i + 1]) {
				case 'n':  result += '\n'; i++; break;
				case 'r':  result += '\r'; i++; break;
				case 't':  result += '\t'; i++; break;
				case '\\': result += '\\'; i++; break;
				default:   result += text[i];   break;
			}
		} else {
			result += text[i];
		}
	}
	return result;
}

// ---------------------------------------------------------------------------
// Escape tests
// ---------------------------------------------------------------------------

static bool testEscapeEmpty() {
	TEST("escape empty string");
	ASSERT(escapeSessionText("") == "");
	PASS();
	return true;
}

static bool testEscapePlain() {
	TEST("escape plain text");
	ASSERT(escapeSessionText("hello world") == "hello world");
	PASS();
	return true;
}

static bool testEscapeNewline() {
	TEST("escape newline");
	ASSERT(escapeSessionText("line1\nline2") == "line1\\nline2");
	PASS();
	return true;
}

static bool testEscapeCarriageReturn() {
	TEST("escape carriage return");
	ASSERT(escapeSessionText("line1\rline2") == "line1\\rline2");
	PASS();
	return true;
}

static bool testEscapeTab() {
	TEST("escape tab");
	ASSERT(escapeSessionText("col1\tcol2") == "col1\\tcol2");
	PASS();
	return true;
}

static bool testEscapeBackslash() {
	TEST("escape backslash");
	ASSERT(escapeSessionText("path\\to\\file") == "path\\\\to\\\\file");
	PASS();
	return true;
}

static bool testEscapeMixed() {
	TEST("escape mixed specials");
	std::string input = "line1\nline2\ttab\r\n\\end";
	std::string expected = "line1\\nline2\\ttab\\r\\n\\\\end";
	ASSERT(escapeSessionText(input) == expected);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Unescape tests
// ---------------------------------------------------------------------------

static bool testUnescapeEmpty() {
	TEST("unescape empty string");
	ASSERT(unescapeSessionText("") == "");
	PASS();
	return true;
}

static bool testUnescapePlain() {
	TEST("unescape plain text");
	ASSERT(unescapeSessionText("hello world") == "hello world");
	PASS();
	return true;
}

static bool testUnescapeNewline() {
	TEST("unescape \\n");
	ASSERT(unescapeSessionText("line1\\nline2") == "line1\nline2");
	PASS();
	return true;
}

static bool testUnescapeCarriageReturn() {
	TEST("unescape \\r");
	ASSERT(unescapeSessionText("line1\\rline2") == "line1\rline2");
	PASS();
	return true;
}

static bool testUnescapeTab() {
	TEST("unescape \\t");
	ASSERT(unescapeSessionText("col1\\tcol2") == "col1\tcol2");
	PASS();
	return true;
}

static bool testUnescapeBackslash() {
	TEST("unescape \\\\");
	ASSERT(unescapeSessionText("path\\\\to\\\\file") == "path\\to\\file");
	PASS();
	return true;
}

static bool testUnescapeUnknownEscape() {
	TEST("unescape unknown escape (\\x) preserves backslash");
	ASSERT(unescapeSessionText("hello\\xworld") == "hello\\xworld");
	PASS();
	return true;
}

static bool testUnescapeTrailingBackslash() {
	TEST("unescape trailing backslash");
	ASSERT(unescapeSessionText("trail\\") == "trail\\");
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Round-trip tests
// ---------------------------------------------------------------------------

static bool testRoundTrip(const std::string & input, const std::string & label) {
	TEST(label.c_str());
	std::string escaped = escapeSessionText(input);
	std::string roundTripped = unescapeSessionText(escaped);
	ASSERT(roundTripped == input);
	PASS();
	return true;
}

static bool testRoundTrips() {
	bool ok = true;
	ok = testRoundTrip("", "round-trip empty") && ok;
	ok = testRoundTrip("simple text", "round-trip simple") && ok;
	ok = testRoundTrip("line1\nline2\nline3", "round-trip newlines") && ok;
	ok = testRoundTrip("\t\t\ttabs", "round-trip tabs") && ok;
	ok = testRoundTrip("C:\\Users\\test\\path", "round-trip windows path") && ok;
	ok = testRoundTrip("mixed\n\r\t\\end", "round-trip all specials") && ok;
	ok = testRoundTrip(std::string(1000, 'a'), "round-trip long string") && ok;

	// Multi-line code block.
	std::string code = "void main() {\n\tprintf(\"hello\\n\");\n\treturn;\n}";
	ok = testRoundTrip(code, "round-trip code block") && ok;

	return ok;
}

// ---------------------------------------------------------------------------
// Session file format tests — write & read back
// ---------------------------------------------------------------------------

static bool testSessionFileRoundTrip() {
	TEST("session file write + read");

	std::string tempDir = "/tmp/test_ggml_session_" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::error_code ec;
	std::filesystem::create_directories(tempDir, ec);
	ASSERT(!ec);

	std::string path = tempDir + "/test.session";

	// Write a session file.
	{
		std::ofstream out(path);
		ASSERT(out.is_open());
		out << "[session_v1]\n";
		out << "mode=1\n";
		out << "model=2\n";
		out << "language=3\n";
		out << "maxTokens=512\n";
		out << "temperature=0.8500\n";
		out << "numThreads=8\n";
		out << "chatInput=" << escapeSessionText("hello\nworld") << "\n";
		out << "scriptOutput=" << escapeSessionText("void main() {\n\treturn;\n}") << "\n";
		out << "chatMessageCount=2\n";
		out << "msg=user|1.50|" << escapeSessionText("test message\nwith newline") << "\n";
		out << "msg=assistant|2.50|" << escapeSessionText("response\ttab") << "\n";
		out << "[/session_v1]\n";
		out.close();
	}

	// Read it back and verify.
	{
		std::ifstream in(path);
		ASSERT(in.is_open());

		std::string line;
		ASSERT(std::getline(in, line) && line == "[session_v1]");

		int mode = -1, model = -1, language = -1, maxTokens = -1, numThreads = -1;
		float temperature = -1.0f;
		std::string chatInput, scriptOutput;
		std::vector<std::string> msgRoles;
		std::vector<std::string> msgTexts;

		while (std::getline(in, line)) {
			if (line == "[/session_v1]") break;
			size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			std::string key = line.substr(0, eq);
			std::string value = line.substr(eq + 1);

			if (key == "mode") mode = std::stoi(value);
			else if (key == "model") model = std::stoi(value);
			else if (key == "language") language = std::stoi(value);
			else if (key == "maxTokens") maxTokens = std::stoi(value);
			else if (key == "temperature") temperature = std::stof(value);
			else if (key == "numThreads") numThreads = std::stoi(value);
			else if (key == "chatInput") chatInput = unescapeSessionText(value);
			else if (key == "scriptOutput") scriptOutput = unescapeSessionText(value);
			else if (key == "msg") {
				size_t sep1 = value.find('|');
				if (sep1 != std::string::npos) {
					size_t sep2 = value.find('|', sep1 + 1);
					if (sep2 != std::string::npos) {
						msgRoles.push_back(unescapeSessionText(value.substr(0, sep1)));
						msgTexts.push_back(unescapeSessionText(value.substr(sep2 + 1)));
					}
				}
			}
		}

		ASSERT(mode == 1);
		ASSERT(model == 2);
		ASSERT(language == 3);
		ASSERT(maxTokens == 512);
		ASSERT(temperature > 0.84f && temperature < 0.86f);
		ASSERT(numThreads == 8);
		ASSERT(chatInput == "hello\nworld");
		ASSERT(scriptOutput == "void main() {\n\treturn;\n}");
		ASSERT(msgRoles.size() == 2);
		ASSERT(msgRoles[0] == "user");
		ASSERT(msgRoles[1] == "assistant");
		ASSERT(msgTexts[0] == "test message\nwith newline");
		ASSERT(msgTexts[1] == "response\ttab");
	}

	// Cleanup.
	std::filesystem::remove_all(tempDir, ec);

	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Edge case: session file with missing fields loads gracefully
// ---------------------------------------------------------------------------

static bool testSessionPartialLoad() {
	TEST("session partial file");

	std::string tempDir = "/tmp/test_ggml_session_partial_" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::error_code ec;
	std::filesystem::create_directories(tempDir, ec);
	ASSERT(!ec);

	std::string path = tempDir + "/partial.session";

	// Write minimal valid session.
	{
		std::ofstream out(path);
		ASSERT(out.is_open());
		out << "[session_v1]\n";
		out << "mode=0\n";
		out << "[/session_v1]\n";
		out.close();
	}

	// Read and verify only mode was set.
	{
		std::ifstream in(path);
		ASSERT(in.is_open());
		std::string line;
		ASSERT(std::getline(in, line) && line == "[session_v1]");

		int mode = -1;
		while (std::getline(in, line)) {
			if (line == "[/session_v1]") break;
			size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			std::string key = line.substr(0, eq);
			std::string value = line.substr(eq + 1);
			if (key == "mode") mode = std::stoi(value);
		}

		ASSERT(mode == 0);
	}

	std::filesystem::remove_all(tempDir, ec);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Edge case: invalid session header rejects
// ---------------------------------------------------------------------------

static bool testSessionInvalidHeader() {
	TEST("session invalid header rejects");

	std::string tempDir = "/tmp/test_ggml_session_invalid_" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::error_code ec;
	std::filesystem::create_directories(tempDir, ec);
	ASSERT(!ec);

	std::string path = tempDir + "/invalid.session";

	{
		std::ofstream out(path);
		ASSERT(out.is_open());
		out << "not_a_session\n";
		out.close();
	}

	{
		std::ifstream in(path);
		ASSERT(in.is_open());
		std::string line;
		std::getline(in, line);
		// Should NOT match session header.
		ASSERT(line != "[session_v1]");
	}

	std::filesystem::remove_all(tempDir, ec);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_session ===\n");

	bool ok = true;
	ok = testEscapeEmpty() && ok;
	ok = testEscapePlain() && ok;
	ok = testEscapeNewline() && ok;
	ok = testEscapeCarriageReturn() && ok;
	ok = testEscapeTab() && ok;
	ok = testEscapeBackslash() && ok;
	ok = testEscapeMixed() && ok;
	ok = testUnescapeEmpty() && ok;
	ok = testUnescapePlain() && ok;
	ok = testUnescapeNewline() && ok;
	ok = testUnescapeCarriageReturn() && ok;
	ok = testUnescapeTab() && ok;
	ok = testUnescapeBackslash() && ok;
	ok = testUnescapeUnknownEscape() && ok;
	ok = testUnescapeTrailingBackslash() && ok;
	ok = testRoundTrips() && ok;
	ok = testSessionFileRoundTrip() && ok;
	ok = testSessionPartialLoad() && ok;
	ok = testSessionInvalidHeader() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
