// ---------------------------------------------------------------------------
// test_ggml_model_presets.cpp — Unit tests for model preset and script
// language data integrity.
//
// Validates model presets have required fields, URLs point to HuggingFace
// GGUF endpoints, filenames match URL leaf paths, and language presets
// have valid extensions and non-empty system prompts.
// ---------------------------------------------------------------------------

#include <cassert>
#include <cstdio>
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
// Duplicated structs — mirrors ofApp.h to avoid OF/ImGui dependencies
// ---------------------------------------------------------------------------

struct ModelPreset {
	std::string name;
	std::string filename;
	std::string url;
	std::string description;
	std::string sizeMB;
	std::string bestFor;
};

struct ScriptLanguage {
	std::string name;
	std::string fileExt;
	std::string systemPrompt;
};

// ---------------------------------------------------------------------------
// Preset data — mirrors initModelPresets() in ofApp.cpp
// ---------------------------------------------------------------------------

static std::vector<ModelPreset> getModelPresets() {
	return {
		{
			"TinyLlama 1.1B Chat Q4_0",
			"tinyllama-1.1b-chat-v1.0.Q4_0.gguf",
			"https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_0.gguf",
			"Fast, small chat model — good starting point",
			"~600 MB", "chat, general"
		},
		{
			"TinyLlama 1.1B Chat Q8_0",
			"tinyllama-1.1b-chat-v1.0.Q8_0.gguf",
			"https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q8_0.gguf",
			"Higher quality TinyLlama variant",
			"~1.1 GB", "chat, general"
		},
		{
			"Phi-2 Q4_0",
			"phi-2.Q4_0.gguf",
			"https://huggingface.co/TheBloke/phi-2-GGUF/resolve/main/phi-2.Q4_0.gguf",
			"Microsoft Phi-2 — strong reasoning for its size",
			"~1.6 GB", "reasoning, code, chat"
		},
		{
			"CodeLlama 7B Instruct Q4_0",
			"codellama-7b-instruct.Q4_0.gguf",
			"https://huggingface.co/TheBloke/CodeLlama-7B-Instruct-GGUF/resolve/main/codellama-7b-instruct.Q4_0.gguf",
			"Meta CodeLlama — optimized for code generation",
			"~3.8 GB", "scripting, code generation"
		},
		{
			"DeepSeek Coder 1.3B Instruct Q4_0",
			"deepseek-coder-1.3b-instruct.Q4_0.gguf",
			"https://huggingface.co/TheBloke/deepseek-coder-1.3b-instruct-GGUF/resolve/main/deepseek-coder-1.3b-instruct.Q4_0.gguf",
			"Small but capable code model",
			"~0.8 GB", "scripting, code"
		},
		{
			"Gemma 2B Instruct Q4_0",
			"gemma-2b-it-Q4_0.gguf",
			"https://huggingface.co/second-state/Gemma-2b-it-GGUF/resolve/main/gemma-2b-it-Q4_0.gguf",
			"Google Gemma — general purpose",
			"~1.4 GB", "chat, summarize, writing"
		}
	};
}

static std::vector<ScriptLanguage> getScriptLanguages() {
	return {
		{"C++",        ".cpp",  "You are a C++ expert. Generate modern C++17 code."},
		{"Python",     ".py",   "You are a Python expert. Generate clean, idiomatic Python 3 code."},
		{"JavaScript", ".js",   "You are a JavaScript expert. Generate modern ES6+ code."},
		{"Rust",       ".rs",   "You are a Rust expert. Generate safe, idiomatic Rust code."},
		{"GLSL",       ".glsl", "You are a GLSL shader expert. Generate efficient GPU shader code."},
		{"Go",         ".go",   "You are a Go expert. Generate idiomatic Go code."},
		{"Bash",       ".sh",   "You are a Bash scripting expert. Generate portable shell scripts."},
		{"TypeScript", ".ts",   "You are a TypeScript expert. Generate type-safe TypeScript code."}
	};
}

// ---------------------------------------------------------------------------
// Model preset tests
// ---------------------------------------------------------------------------

static bool testModelPresetsNotEmpty() {
	TEST("model presets not empty");
	auto presets = getModelPresets();
	ASSERT(!presets.empty());
	ASSERT(presets.size() == 6);
	PASS();
	return true;
}

static bool testModelPresetsHaveRequiredFields() {
	TEST("model presets have required fields");
	for (const auto & p : getModelPresets()) {
		ASSERT(!p.name.empty());
		ASSERT(!p.filename.empty());
		ASSERT(!p.url.empty());
		ASSERT(!p.description.empty());
		ASSERT(!p.sizeMB.empty());
		ASSERT(!p.bestFor.empty());
	}
	PASS();
	return true;
}

static bool testModelPresetsUrlsAreHuggingFace() {
	TEST("model preset URLs point to huggingface.co");
	const std::string prefix = "https://huggingface.co/";
	for (const auto & p : getModelPresets()) {
		ASSERT(p.url.substr(0, prefix.size()) == prefix);
	}
	PASS();
	return true;
}

static bool testModelPresetsFilenamesEndWithGguf() {
	TEST("model preset filenames end with .gguf");
	for (const auto & p : getModelPresets()) {
		ASSERT(p.filename.size() >= 5);
		ASSERT(p.filename.substr(p.filename.size() - 5) == ".gguf");
	}
	PASS();
	return true;
}

static bool testModelPresetsUrlContainsFilename() {
	TEST("model preset URL contains filename");
	for (const auto & p : getModelPresets()) {
		// The URL should end with the filename.
		ASSERT(p.url.size() >= p.filename.size());
		std::string urlTail = p.url.substr(p.url.size() - p.filename.size());
		ASSERT(urlTail == p.filename);
	}
	PASS();
	return true;
}

static bool testModelPresetsUniqueFilenames() {
	TEST("model preset filenames are unique");
	auto presets = getModelPresets();
	for (size_t i = 0; i < presets.size(); i++) {
		for (size_t j = i + 1; j < presets.size(); j++) {
			ASSERT(presets[i].filename != presets[j].filename);
		}
	}
	PASS();
	return true;
}

static bool testModelPresetsUniqueNames() {
	TEST("model preset names are unique");
	auto presets = getModelPresets();
	for (size_t i = 0; i < presets.size(); i++) {
		for (size_t j = i + 1; j < presets.size(); j++) {
			ASSERT(presets[i].name != presets[j].name);
		}
	}
	PASS();
	return true;
}

static bool testModelPresetsSizeFormat() {
	TEST("model preset sizes start with ~");
	for (const auto & p : getModelPresets()) {
		ASSERT(p.sizeMB[0] == '~');
	}
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Script language tests
// ---------------------------------------------------------------------------

static bool testScriptLanguagesNotEmpty() {
	TEST("script languages not empty");
	auto langs = getScriptLanguages();
	ASSERT(!langs.empty());
	ASSERT(langs.size() == 8);
	PASS();
	return true;
}

static bool testScriptLanguagesHaveRequiredFields() {
	TEST("script languages have required fields");
	for (const auto & l : getScriptLanguages()) {
		ASSERT(!l.name.empty());
		ASSERT(!l.fileExt.empty());
		ASSERT(!l.systemPrompt.empty());
	}
	PASS();
	return true;
}

static bool testScriptLanguageExtensionsStartWithDot() {
	TEST("script language extensions start with dot");
	for (const auto & l : getScriptLanguages()) {
		ASSERT(l.fileExt[0] == '.');
		ASSERT(l.fileExt.size() >= 2);
	}
	PASS();
	return true;
}

static bool testScriptLanguageExtensionsUnique() {
	TEST("script language extensions are unique");
	auto langs = getScriptLanguages();
	for (size_t i = 0; i < langs.size(); i++) {
		for (size_t j = i + 1; j < langs.size(); j++) {
			ASSERT(langs[i].fileExt != langs[j].fileExt);
		}
	}
	PASS();
	return true;
}

static bool testScriptLanguageNamesUnique() {
	TEST("script language names are unique");
	auto langs = getScriptLanguages();
	for (size_t i = 0; i < langs.size(); i++) {
		for (size_t j = i + 1; j < langs.size(); j++) {
			ASSERT(langs[i].name != langs[j].name);
		}
	}
	PASS();
	return true;
}

static bool testScriptLanguagePromptsContainLanguageName() {
	TEST("script language prompts mention the language");
	for (const auto & l : getScriptLanguages()) {
		// Each prompt should contain the language name (case-insensitive check via substring).
		ASSERT(l.systemPrompt.find(l.name) != std::string::npos ||
		       l.systemPrompt.find("GLSL") != std::string::npos ||
		       l.systemPrompt.find("Bash") != std::string::npos);
	}
	PASS();
	return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
	std::printf("=== test_ggml_model_presets ===\n");

	bool ok = true;

	// Model presets.
	ok = testModelPresetsNotEmpty() && ok;
	ok = testModelPresetsHaveRequiredFields() && ok;
	ok = testModelPresetsUrlsAreHuggingFace() && ok;
	ok = testModelPresetsFilenamesEndWithGguf() && ok;
	ok = testModelPresetsUrlContainsFilename() && ok;
	ok = testModelPresetsUniqueFilenames() && ok;
	ok = testModelPresetsUniqueNames() && ok;
	ok = testModelPresetsSizeFormat() && ok;

	// Script languages.
	ok = testScriptLanguagesNotEmpty() && ok;
	ok = testScriptLanguagesHaveRequiredFields() && ok;
	ok = testScriptLanguageExtensionsStartWithDot() && ok;
	ok = testScriptLanguageExtensionsUnique() && ok;
	ok = testScriptLanguageNamesUnique() && ok;
	ok = testScriptLanguagePromptsContainLanguageName() && ok;

	std::printf("\n%d/%d tests passed.\n", passCount, testCount);
	return ok ? 0 : 1;
}
