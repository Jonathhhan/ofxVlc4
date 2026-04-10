// ---------------------------------------------------------------------------
// test_ggml_script_source.cpp — Unit tests for script source validation
//
// Validates the GitHub owner/repo path validation, branch name validation,
// file extension filtering logic, and script filename generation used by
// the ofxGgml GUI example.
// ---------------------------------------------------------------------------

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
// Extracted validation — mirrors ofApp.cpp scanGitHubRepo()
// ---------------------------------------------------------------------------

static bool isValidGitHubPath(const std::string & s) {
	if (s.empty() || s.find('/') == std::string::npos) return false;
	if (s.find("..") != std::string::npos) return false;
	for (char c : s) {
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '/' && c != '-' && c != '_' && c != '.') {
			return false;
		}
	}
	return true;
}

static bool isValidBranch(const std::string & s) {
	for (char c : s) {
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '-' && c != '_' && c != '.' && c != '/') {
			return false;
		}
	}
	return !s.empty();
}

// Extracted URL prefix validation — mirrors ofApp.cpp loadScriptFile()
static bool isValidRawGitHubUrl(const std::string & url) {
	const std::string expectedPrefix = "https://raw.githubusercontent.com/";
	return url.substr(0, expectedPrefix.size()) == expectedPrefix;
}

// Extracted file extension filtering — mirrors ofApp.cpp scanLocalFolder()
static bool isSourceExtension(const std::string & ext) {
	static const std::vector<std::string> commonExts = {
		".cpp", ".h", ".py", ".js", ".ts", ".rs", ".go",
		".glsl", ".vert", ".frag", ".sh", ".c", ".hpp",
		".java", ".kt", ".swift", ".lua", ".rb", ".cs"
	};
	for (const auto & ce : commonExts) {
		if (ext == ce) return true;
	}
	return false;
}

// GitHub tree response source extensions — mirrors ofApp.cpp scanGitHubRepo()
static bool isGitHubSourceExtension(const std::string & ext) {
	static const std::vector<std::string> sourceExts = {
		".cpp", ".h", ".py", ".js", ".ts", ".rs", ".go",
		".glsl", ".vert", ".frag", ".sh", ".c", ".hpp",
		".java", ".kt", ".swift", ".lua", ".rb", ".cs",
		".md", ".txt", ".json", ".yaml", ".yml", ".toml"
	};
	for (const auto & se : sourceExts) {
		if (ext == se) return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// GitHub path validation tests
// ---------------------------------------------------------------------------

static bool testValidGitHubPaths() {
	TEST("valid GitHub paths accepted");
	ASSERT(isValidGitHubPath("owner/repo"));
	ASSERT(isValidGitHubPath("my-org/my-repo"));
	ASSERT(isValidGitHubPath("user_name/project.name"));
	ASSERT(isValidGitHubPath("Abc123/Def456"));
	PASS();
	return true;
}

static bool testInvalidGitHubPathsRejected() {
	TEST("invalid GitHub paths rejected");
	ASSERT(!isValidGitHubPath(""));
	ASSERT(!isValidGitHubPath("noslash"));
	ASSERT(!isValidGitHubPath("has spaces/repo"));
	ASSERT(!isValidGitHubPath("owner/repo;cmd"));
	ASSERT(!isValidGitHubPath("owner/repo|pipe"));
	ASSERT(!isValidGitHubPath("owner/repo&bg"));
	ASSERT(!isValidGitHubPath("owner/repo`tick"));
	ASSERT(!isValidGitHubPath("owner/repo$(cmd)"));
	ASSERT(!isValidGitHubPath("owner/repo'quote"));
	ASSERT(!isValidGitHubPath("owner/repo\"dq"));
	PASS();
	return true;
}

static bool testPathTraversalRejected() {
	TEST("path traversal (..) rejected");
	ASSERT(!isValidGitHubPath("../../../etc/passwd"));
	ASSERT(!isValidGitHubPath("owner/../etc/passwd"));
	ASSERT(!isValidGitHubPath("owner/repo/.."));
	ASSERT(!isValidGitHubPath("..owner/repo"));
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Branch validation tests
// ---------------------------------------------------------------------------

static bool testValidBranches() {
	TEST("valid branch names accepted");
	ASSERT(isValidBranch("main"));
	ASSERT(isValidBranch("develop"));
	ASSERT(isValidBranch("feature/my-feature"));
	ASSERT(isValidBranch("v1.0.0"));
	ASSERT(isValidBranch("release-2024"));
	ASSERT(isValidBranch("user_branch.name"));
	PASS();
	return true;
}

static bool testInvalidBranches() {
	TEST("invalid branch names rejected");
	ASSERT(!isValidBranch(""));
	ASSERT(!isValidBranch("branch name"));
	ASSERT(!isValidBranch("branch;cmd"));
	ASSERT(!isValidBranch("branch|pipe"));
	ASSERT(!isValidBranch("branch&bg"));
	ASSERT(!isValidBranch("branch`tick"));
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// URL prefix validation tests
// ---------------------------------------------------------------------------

static bool testValidRawUrls() {
	TEST("valid raw GitHub URLs accepted");
	ASSERT(isValidRawGitHubUrl("https://raw.githubusercontent.com/owner/repo/main/file.txt"));
	ASSERT(isValidRawGitHubUrl("https://raw.githubusercontent.com/a/b/c"));
	PASS();
	return true;
}

static bool testInvalidRawUrls() {
	TEST("invalid raw GitHub URLs rejected");
	ASSERT(!isValidRawGitHubUrl("http://raw.githubusercontent.com/owner/repo"));
	ASSERT(!isValidRawGitHubUrl("https://evil.com/raw.githubusercontent.com/owner/repo"));
	ASSERT(!isValidRawGitHubUrl("ftp://raw.githubusercontent.com/owner/repo"));
	ASSERT(!isValidRawGitHubUrl(""));
	ASSERT(!isValidRawGitHubUrl("https://github.com/owner/repo"));
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// File extension filtering tests
// ---------------------------------------------------------------------------

static bool testSourceExtensionsAccepted() {
	TEST("source extensions accepted");
	ASSERT(isSourceExtension(".cpp"));
	ASSERT(isSourceExtension(".h"));
	ASSERT(isSourceExtension(".py"));
	ASSERT(isSourceExtension(".js"));
	ASSERT(isSourceExtension(".ts"));
	ASSERT(isSourceExtension(".rs"));
	ASSERT(isSourceExtension(".go"));
	ASSERT(isSourceExtension(".glsl"));
	ASSERT(isSourceExtension(".vert"));
	ASSERT(isSourceExtension(".frag"));
	ASSERT(isSourceExtension(".sh"));
	ASSERT(isSourceExtension(".c"));
	ASSERT(isSourceExtension(".hpp"));
	ASSERT(isSourceExtension(".java"));
	ASSERT(isSourceExtension(".kt"));
	ASSERT(isSourceExtension(".swift"));
	ASSERT(isSourceExtension(".lua"));
	ASSERT(isSourceExtension(".rb"));
	ASSERT(isSourceExtension(".cs"));
	PASS();
	return true;
}

static bool testNonSourceExtensionsRejected() {
	TEST("non-source extensions rejected by local filter");
	ASSERT(!isSourceExtension(".exe"));
	ASSERT(!isSourceExtension(".dll"));
	ASSERT(!isSourceExtension(".zip"));
	ASSERT(!isSourceExtension(".png"));
	ASSERT(!isSourceExtension(".mp4"));
	ASSERT(!isSourceExtension(""));
	PASS();
	return true;
}

static bool testGitHubExtensionsIncludeMetadata() {
	TEST("GitHub extensions include metadata files");
	// GitHub source also includes doc/config files.
	ASSERT(isGitHubSourceExtension(".md"));
	ASSERT(isGitHubSourceExtension(".txt"));
	ASSERT(isGitHubSourceExtension(".json"));
	ASSERT(isGitHubSourceExtension(".yaml"));
	ASSERT(isGitHubSourceExtension(".yml"));
	ASSERT(isGitHubSourceExtension(".toml"));
	// But still rejects binary types.
	ASSERT(!isGitHubSourceExtension(".exe"));
	ASSERT(!isGitHubSourceExtension(".zip"));
	ASSERT(!isGitHubSourceExtension(".png"));
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Local folder scan simulation
// ---------------------------------------------------------------------------

static bool testLocalFolderScan() {
	TEST("local folder scan finds source files");

	std::string tempDir = "/tmp/test_ggml_scan_" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::error_code ec;
	std::filesystem::create_directories(tempDir, ec);
	ASSERT(!ec);

	// Create some test files.
	std::ofstream(tempDir + "/main.cpp").close();
	std::ofstream(tempDir + "/helper.h").close();
	std::ofstream(tempDir + "/script.py").close();
	std::ofstream(tempDir + "/data.bin").close();   // Should be filtered out.
	std::ofstream(tempDir + "/image.png").close();   // Should be filtered out.
	std::filesystem::create_directory(tempDir + "/subdir", ec);

	// Simulate scan: collect files with source extensions.
	std::vector<std::string> sourceFiles;
	std::vector<std::string> nonSourceFiles;
	for (const auto & entry : std::filesystem::directory_iterator(tempDir, ec)) {
		if (entry.is_directory()) continue;
		std::string ext = entry.path().extension().string();
		if (isSourceExtension(ext)) {
			sourceFiles.push_back(entry.path().filename().string());
		} else {
			nonSourceFiles.push_back(entry.path().filename().string());
		}
	}

	ASSERT(sourceFiles.size() == 3);
	ASSERT(nonSourceFiles.size() == 2);

	// Cleanup.
	std::filesystem::remove_all(tempDir, ec);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// GitHub raw URL construction validation
// ---------------------------------------------------------------------------

static bool testGitHubRawUrlConstruction() {
	TEST("GitHub raw URL construction");
	std::string owner = "Jonathhhan";
	std::string repo = "ofxVlc4";
	std::string branch = "main";
	std::string path = "src/core/ofxVlc4.h";

	std::string url = "https://raw.githubusercontent.com/" +
		owner + "/" + repo + "/" + branch + "/" + path;

	ASSERT(isValidRawGitHubUrl(url));
	ASSERT(url.find(owner) != std::string::npos);
	ASSERT(url.find(path) != std::string::npos);
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_script_source ===\n");

	bool ok = true;

	// GitHub path validation.
	ok = testValidGitHubPaths() && ok;
	ok = testInvalidGitHubPathsRejected() && ok;
	ok = testPathTraversalRejected() && ok;

	// Branch validation.
	ok = testValidBranches() && ok;
	ok = testInvalidBranches() && ok;

	// URL prefix validation.
	ok = testValidRawUrls() && ok;
	ok = testInvalidRawUrls() && ok;

	// File extension filtering.
	ok = testSourceExtensionsAccepted() && ok;
	ok = testNonSourceExtensionsRejected() && ok;
	ok = testGitHubExtensionsIncludeMetadata() && ok;

	// Local folder scan.
	ok = testLocalFolderScan() && ok;

	// URL construction.
	ok = testGitHubRawUrlConstruction() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
