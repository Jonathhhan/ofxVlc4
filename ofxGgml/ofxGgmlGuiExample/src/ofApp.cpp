#include "ofApp.h"
#include "support/ofxGgmlHelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

const char * ofApp::modeLabels[kModeCount] = {
"Chat", "Script", "Summarize", "Write", "Custom"
};

// ---------------------------------------------------------------------------
// Presets — models
// ---------------------------------------------------------------------------

void ofApp::initModelPresets() {
modelPresets = {
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

// ---------------------------------------------------------------------------
// Presets — script languages
// ---------------------------------------------------------------------------

void ofApp::initScriptLanguages() {
scriptLanguages = {
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
// Lifecycle
// ---------------------------------------------------------------------------

void ofApp::setup() {
ofSetWindowTitle("ofxGgml AI Studio");
ofSetFrameRate(60);
ofSetBackgroundColor(ofColor(30, 30, 34));

gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
ImGui::GetIO().IniFilename = "imgui_ggml_studio.ini";

// Initialize presets.
initModelPresets();
initScriptLanguages();

// Default branch for GitHub.
std::strncpy(scriptSourceBranch, "main", sizeof(scriptSourceBranch) - 1);
scriptSourceBranch[sizeof(scriptSourceBranch) - 1] = '\0';

// Session directory.
sessionDir = ofToDataPath("sessions", true);
std::error_code ec;
std::filesystem::create_directories(sessionDir, ec);
lastSessionPath = ofFilePath::join(sessionDir, "autosave.session");

// Initialize ggml.
ofxGgmlSettings settings;
settings.threads = numThreads;
engineReady = ggml.setup(settings);
if (engineReady) {
engineStatus = "Ready (" + ggml.getBackendName() + ")";
devices = ggml.listDevices();
} else {
engineStatus = "Failed to initialize ggml engine";
}

// Log callback.
ggml.setLogCallback([this](int level, const std::string & msg) {
std::lock_guard<std::mutex> lock(logMutex);
logMessages.push_back("[" + ofToString(level) + "] " + msg);
if (logMessages.size() > 500) {
logMessages.pop_front();
}
});

// Pre-fill example system prompt.
std::strncpy(customSystemPrompt,
"You are a helpful assistant. Respond concisely.", sizeof(customSystemPrompt) - 1);

// Auto-load last session if available.
autoLoadSession();
}

void ofApp::update() {
applyPendingOutput();
}

void ofApp::draw() {
ofBackground(30, 30, 34);
gui.begin();
drawMenuBar();

const float windowW = static_cast<float>(ofGetWidth());
const float windowH = static_cast<float>(ofGetHeight());
const float menuBarH = ImGui::GetFrameHeight() + 4.0f;
const float statusBarH = 28.0f;
const float sidebarW = 220.0f;

// Sidebar.
ImGui::SetNextWindowPos(ImVec2(0.0f, menuBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(sidebarW, windowH - menuBarH - statusBarH), ImGuiCond_Always);
drawSidebar();

// Main panel.
ImGui::SetNextWindowPos(ImVec2(sidebarW, menuBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(windowW - sidebarW, windowH - menuBarH - statusBarH), ImGuiCond_Always);
drawMainPanel();

// Status bar.
ImGui::SetNextWindowPos(ImVec2(0.0f, windowH - statusBarH), ImGuiCond_Always);
ImGui::SetNextWindowSize(ImVec2(windowW, statusBarH), ImGuiCond_Always);
drawStatusBar();

// Optional floating windows.
if (showDeviceInfo) drawDeviceInfoWindow();
if (showLog) drawLogWindow();

gui.end();
}

void ofApp::exit() {
autoSaveSession();
if (workerThread.joinable()) {
generating.store(false);
workerThread.join();
}
ggml.close();
gui.exit();
}

void ofApp::keyPressed(int key) {
// Global shortcuts.
if (key == OF_KEY_F1) showDeviceInfo = !showDeviceInfo;
if (key == OF_KEY_F2) showLog = !showLog;
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void ofApp::drawMenuBar() {
if (ImGui::BeginMainMenuBar()) {
if (ImGui::BeginMenu("File")) {
if (ImGui::MenuItem("Save Session", "Ctrl+S")) {
ofFileDialogResult result = ofSystemSaveDialog(
"session.txt", "Save Session");
if (result.bSuccess) {
saveSession(result.getPath());
}
}
if (ImGui::MenuItem("Load Session", "Ctrl+L")) {
ofFileDialogResult result = ofSystemLoadDialog(
"Load Session", false, sessionDir);
if (result.bSuccess) {
loadSession(result.getPath());
}
}
ImGui::Separator();
if (ImGui::MenuItem("Clear Output")) {
chatMessages.clear();
scriptOutput.clear();
summarizeOutput.clear();
writeOutput.clear();
customOutput.clear();
}
ImGui::Separator();
if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
ofExit(0);
}
ImGui::EndMenu();
}
if (ImGui::BeginMenu("View")) {
ImGui::MenuItem("Device Info (F1)", nullptr, &showDeviceInfo);
ImGui::MenuItem("Engine Log  (F2)", nullptr, &showLog);
ImGui::MenuItem("Script Source Panel", nullptr, &showScriptSourcePanel);
ImGui::EndMenu();
}
if (ImGui::BeginMenu("Settings")) {
ImGui::SliderInt("Max Tokens", &maxTokens, 32, 2048);
ImGui::SliderFloat("Temperature", &temperature, 0.0f, 2.0f, "%.2f");
ImGui::SliderInt("Threads", &numThreads, 1, 16);
ImGui::EndMenu();
}
ImGui::EndMainMenuBar();
}
}

// ---------------------------------------------------------------------------
// Sidebar — mode selection + model preset + quick settings
// ---------------------------------------------------------------------------

void ofApp::drawSidebar() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

if (ImGui::Begin("##Sidebar", nullptr, flags)) {
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "AI Studio");
ImGui::Separator();
ImGui::Spacing();
ImGui::Text("Mode:");
ImGui::Spacing();

for (int i = 0; i < kModeCount; i++) {
bool selected = (static_cast<int>(activeMode) == i);
if (ImGui::Selectable(modeLabels[i], selected, ImGuiSelectableFlags_None, ImVec2(0, 28))) {
activeMode = static_cast<AiMode>(i);
}
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// Model preset selector.
ImGui::Text("Model:");
ImGui::SetNextItemWidth(-1);
if (!modelPresets.empty()) {
if (ImGui::BeginCombo("##ModelSel", modelPresets[static_cast<size_t>(selectedModelIndex)].name.c_str())) {
for (int i = 0; i < static_cast<int>(modelPresets.size()); i++) {
bool isSelected = (selectedModelIndex == i);
const auto & preset = modelPresets[static_cast<size_t>(i)];
std::string label = preset.name + "  " + preset.sizeMB;
if (ImGui::Selectable(label.c_str(), isSelected)) {
selectedModelIndex = i;
}
if (ImGui::IsItemHovered()) {
ImGui::SetTooltip("%s\nBest for: %s\nFile: %s",
preset.description.c_str(),
preset.bestFor.c_str(),
preset.filename.c_str());
}
if (isSelected) ImGui::SetItemDefaultFocus();
}
ImGui::EndCombo();
}
}

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();
ImGui::Text("Quick Settings");
ImGui::Spacing();
ImGui::SetNextItemWidth(-1);
ImGui::SliderInt("##MaxTok", &maxTokens, 32, 2048, "Tokens: %d");
ImGui::SetNextItemWidth(-1);
ImGui::SliderFloat("##Temp", &temperature, 0.0f, 2.0f, "Temp: %.2f");

ImGui::Spacing();
ImGui::Separator();
ImGui::Spacing();

// Engine status indicator.
if (engineReady) {
ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Engine: OK");
} else {
ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.2f, 1.0f), "Engine: Error");
}
ImGui::Text("Backend: %s", ggml.getBackendName().c_str());

if (generating.load()) {
ImGui::Spacing();
ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Generating...");
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Main panel — dispatches to mode-specific panels
// ---------------------------------------------------------------------------

void ofApp::drawMainPanel() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

if (ImGui::Begin("##MainPanel", nullptr, flags)) {
switch (activeMode) {
case AiMode::Chat:      drawChatPanel();      break;
case AiMode::Script:    drawScriptPanel();    break;
case AiMode::Summarize: drawSummarizePanel(); break;
case AiMode::Write:     drawWritePanel();     break;
case AiMode::Custom:    drawCustomPanel();    break;
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Chat panel
// ---------------------------------------------------------------------------

void ofApp::drawChatPanel() {
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Chat");
ImGui::SameLine();
ImGui::TextDisabled("(conversation with the ggml engine)");
ImGui::Separator();

// Message history.
float inputH = 60.0f;
ImGui::BeginChild("##ChatHistory", ImVec2(0, -inputH), true);
for (const auto & msg : chatMessages) {
if (msg.role == "user") {
ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "You:");
} else if (msg.role == "assistant") {
ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "AI:");
} else {
ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "System:");
}
ImGui::SameLine();
ImGui::TextWrapped("%s", msg.text.c_str());
ImGui::Spacing();
}
// Auto-scroll.
if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
ImGui::SetScrollHereY(1.0f);
}
ImGui::EndChild();

// Input.
ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
bool submitted = ImGui::InputText("##ChatIn", chatInput, sizeof(chatInput),
ImGuiInputTextFlags_EnterReturnsTrue);
ImGui::SameLine();
bool sendClicked = ImGui::Button("Send", ImVec2(70, 0));

if ((submitted || sendClicked) && std::strlen(chatInput) > 0 && !generating.load()) {
std::string userText(chatInput);
chatMessages.push_back({"user", userText, ofGetElapsedTimef()});
std::memset(chatInput, 0, sizeof(chatInput));
runInference(AiMode::Chat, userText);
}
}

// ---------------------------------------------------------------------------
// Script panel — with language selector and source browser
// ---------------------------------------------------------------------------

void ofApp::drawScriptPanel() {
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Script Generation");
ImGui::SameLine();
ImGui::TextDisabled("(generate or explain code)");
ImGui::Separator();

// Language selector and source controls on same row.
ImGui::Text("Language:");
ImGui::SameLine();
ImGui::SetNextItemWidth(140);
if (!scriptLanguages.empty()) {
if (ImGui::BeginCombo("##LangSel", scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name.c_str())) {
for (int i = 0; i < static_cast<int>(scriptLanguages.size()); i++) {
bool isSelected = (selectedLanguageIndex == i);
if (ImGui::Selectable(scriptLanguages[static_cast<size_t>(i)].name.c_str(), isSelected)) {
selectedLanguageIndex = i;
}
if (isSelected) ImGui::SetItemDefaultFocus();
}
ImGui::EndCombo();
}
}

ImGui::SameLine();
ImGui::Text("Source:");
ImGui::SameLine();

// Source type buttons.
bool isNone = (scriptSourceType == ScriptSourceType::None);
bool isLocal = (scriptSourceType == ScriptSourceType::LocalFolder);
bool isGitHub = (scriptSourceType == ScriptSourceType::GitHubRepo);

if (isNone) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
if (ImGui::SmallButton("None")) { scriptSourceType = ScriptSourceType::None; scriptSourceFiles.clear(); }
if (isNone) ImGui::PopStyleColor();
ImGui::SameLine();

if (isLocal) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
if (ImGui::SmallButton("Local Folder")) {
ofFileDialogResult result = ofSystemLoadDialog("Select Script Folder", true);
if (result.bSuccess) {
scriptSourceType = ScriptSourceType::LocalFolder;
scriptSourcePath = result.getPath();
scanLocalFolder(scriptSourcePath);
}
}
if (isLocal) ImGui::PopStyleColor();
ImGui::SameLine();

if (isGitHub) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.6f, 1.0f));
if (ImGui::SmallButton("GitHub")) {
scriptSourceType = ScriptSourceType::GitHubRepo;
showScriptSourcePanel = true;
}
if (isGitHub) ImGui::PopStyleColor();

// Script source file browser (inline when active).
if (scriptSourceType != ScriptSourceType::None && !scriptSourceFiles.empty()) {
ImGui::BeginChild("##ScriptFiles", ImVec2(-1, 80), true);
if (scriptSourceType == ScriptSourceType::LocalFolder) {
ImGui::TextDisabled("Folder: %s", scriptSourcePath.c_str());
} else {
ImGui::TextDisabled("GitHub: %s (%s)", scriptSourceGitHub, scriptSourceBranch);
}
for (int i = 0; i < static_cast<int>(scriptSourceFiles.size()); i++) {
const auto & entry = scriptSourceFiles[static_cast<size_t>(i)];
ImGui::PushID(i);
std::string icon = entry.isDirectory ? "[dir] " : "      ";
bool isSelected = (selectedScriptFileIndex == i);
if (ImGui::Selectable((icon + entry.name).c_str(), isSelected) && !entry.isDirectory) {
selectedScriptFileIndex = i;
loadScriptFile(i);
}
ImGui::PopID();
}
ImGui::EndChild();
}

if (scriptSourceType == ScriptSourceType::GitHubRepo && showScriptSourcePanel) {
drawScriptSourcePanel();
}

ImGui::Text("Describe what you want:");
ImGui::InputTextMultiline("##ScriptIn", scriptInput, sizeof(scriptInput),
ImVec2(-1, 100));

ImGui::BeginDisabled(generating.load() || std::strlen(scriptInput) == 0);
if (ImGui::Button("Generate Code", ImVec2(140, 0))) {
std::string langPrompt;
if (!scriptLanguages.empty()) {
langPrompt = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].systemPrompt + "\n";
}
runInference(AiMode::Script, langPrompt + scriptInput);
}
ImGui::SameLine();
if (ImGui::Button("Explain Code", ImVec2(140, 0))) {
std::string prompt = std::string("Explain the following code:\n") + scriptInput;
runInference(AiMode::Script, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Debug Code", ImVec2(140, 0))) {
std::string prompt = std::string("Find bugs in the following code:\n") + scriptInput;
runInference(AiMode::Script, prompt);
}
ImGui::EndDisabled();

// Save output to source.
if (!scriptOutput.empty() && scriptSourceType == ScriptSourceType::LocalFolder) {
ImGui::SameLine();
if (ImGui::Button("Save to Folder", ImVec2(130, 0))) {
std::string filename = buildScriptFilename();
saveScriptToSource(filename, scriptOutput);
}
}

ImGui::Separator();
ImGui::Text("Output:");
ImGui::BeginChild("##ScriptOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", scriptOutput.empty() ? "(no output yet)" : scriptOutput.c_str());
ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Script source panel — GitHub repo connection UI
// ---------------------------------------------------------------------------

void ofApp::drawScriptSourcePanel() {
ImGui::Spacing();
ImGui::TextColored(ImVec4(0.6f, 0.7f, 1.0f, 1.0f), "GitHub Repository:");
ImGui::SetNextItemWidth(250);
ImGui::InputText("##GHRepo", scriptSourceGitHub, sizeof(scriptSourceGitHub));
ImGui::SameLine();
ImGui::Text("Branch:");
ImGui::SameLine();
ImGui::SetNextItemWidth(100);
ImGui::InputText("##GHBranch", scriptSourceBranch, sizeof(scriptSourceBranch));
ImGui::SameLine();
if (ImGui::Button("Fetch", ImVec2(60, 0))) {
if (std::strlen(scriptSourceGitHub) > 0) {
std::string branch = std::strlen(scriptSourceBranch) > 0
? std::string(scriptSourceBranch) : "main";
scanGitHubRepo(scriptSourceGitHub, branch);
}
}
if (!scriptSourceStatus.empty()) {
ImGui::SameLine();
ImGui::TextDisabled("%s", scriptSourceStatus.c_str());
}
ImGui::Spacing();
}

// ---------------------------------------------------------------------------
// Script source — local folder scanning
// ---------------------------------------------------------------------------

void ofApp::scanLocalFolder(const std::string & path) {
scriptSourceFiles.clear();
selectedScriptFileIndex = -1;
scriptSourceStatus.clear();

std::error_code ec;
if (!std::filesystem::is_directory(path, ec)) {
scriptSourceStatus = "Not a directory";
return;
}

// Collect matching source files by extension.
std::string targetExt;
if (!scriptLanguages.empty()) {
targetExt = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt;
}

for (const auto & entry : std::filesystem::directory_iterator(path, ec)) {
ScriptFileEntry fe;
fe.name = entry.path().filename().string();
fe.fullPath = entry.path().string();
fe.isDirectory = entry.is_directory(ec);

if (fe.isDirectory) {
scriptSourceFiles.push_back(fe);
} else {
// Show all source-like files, or filter to selected language.
const std::string ext = entry.path().extension().string();
bool match = targetExt.empty() || ext == targetExt;
if (!match) {
// Also show common source extensions.
static const std::vector<std::string> commonExts = {
".cpp", ".h", ".py", ".js", ".ts", ".rs", ".go",
".glsl", ".vert", ".frag", ".sh", ".c", ".hpp",
".java", ".kt", ".swift", ".lua", ".rb", ".cs"
};
for (const auto & ce : commonExts) {
if (ext == ce) { match = true; break; }
}
}
if (match) {
scriptSourceFiles.push_back(fe);
}
}
}

// Sort: directories first, then alphabetical.
std::sort(scriptSourceFiles.begin(), scriptSourceFiles.end(),
[](const ScriptFileEntry & a, const ScriptFileEntry & b) {
if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory;
return a.name < b.name;
});

scriptSourceStatus = ofToString(scriptSourceFiles.size()) + " items";
}

// ---------------------------------------------------------------------------
// Script source — GitHub repo scanning (via GitHub API)
// ---------------------------------------------------------------------------

void ofApp::scanGitHubRepo(const std::string & ownerRepo, const std::string & branch) {
scriptSourceFiles.clear();
selectedScriptFileIndex = -1;
scriptSourceStatus = "Fetching...";

// Validate: ownerRepo must be "owner/repo" with only alphanumeric, dash, underscore, dot.
// Branch must be alphanumeric, dash, underscore, dot, slash.
auto isValidGitHubPath = [](const std::string & s) -> bool {
	if (s.empty() || s.find('/') == std::string::npos) return false;
	if (s.find("..") != std::string::npos) return false;
	for (char c : s) {
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '/' && c != '-' && c != '_' && c != '.') {
			return false;
		}
	}
	return true;
};
auto isValidBranch = [](const std::string & s) -> bool {
	for (char c : s) {
		if (!std::isalnum(static_cast<unsigned char>(c)) &&
			c != '-' && c != '_' && c != '.' && c != '/') {
			return false;
		}
	}
	return !s.empty();
};

if (!isValidGitHubPath(ownerRepo)) {
	scriptSourceStatus = "Invalid repo format (use owner/repo)";
	return;
}
if (!isValidBranch(branch)) {
	scriptSourceStatus = "Invalid branch name";
	return;
}

// Build the GitHub API tree URL.
// GET https://api.github.com/repos/{owner}/{repo}/git/trees/{branch}
const std::string apiUrl = "https://api.github.com/repos/" + ownerRepo +
"/git/trees/" + branch + "?recursive=1";

// Fetch using curl in a background thread to avoid blocking the UI.
// We parse the raw JSON response for "path" and "type" fields.
std::string owner = ownerRepo;
std::string br = branch;
std::thread([this, apiUrl, owner, br]() {
std::string tempFile = ofFilePath::join(ofToDataPath("", true), "gh_tree_response.tmp");
std::string cmd = "curl -s -H \"Accept: application/vnd.github.v3+json\" "
"\"" + apiUrl + "\" -o \"" + tempFile + "\" 2>/dev/null";
int ret = std::system(cmd.c_str());

std::vector<ScriptFileEntry> entries;
std::string status;

if (ret != 0) {
status = "curl failed";
} else {
// Simple JSON parse: look for "path":"..." and "type":"blob"/"tree" pairs.
std::ifstream file(tempFile);
std::string content((std::istreambuf_iterator<char>(file)),
std::istreambuf_iterator<char>());
file.close();

// Remove temp file.
std::error_code ec;
std::filesystem::remove(tempFile, ec);

if (content.find("\"message\"") != std::string::npos &&
content.find("Not Found") != std::string::npos) {
status = "Repo not found";
} else {
// Extract entries: find "path":"value","type":"value" pairs.
size_t pos = 0;
while (pos < content.size()) {
size_t pathKey = content.find("\"path\":", pos);
if (pathKey == std::string::npos) break;

size_t pathStart = content.find('"', pathKey + 7);
if (pathStart == std::string::npos) break;
pathStart++;
size_t pathEnd = content.find('"', pathStart);
if (pathEnd == std::string::npos) break;
std::string path = content.substr(pathStart, pathEnd - pathStart);

size_t typeKey = content.find("\"type\":", pathEnd);
if (typeKey == std::string::npos || typeKey - pathEnd > 100) {
pos = pathEnd + 1;
continue;
}
size_t typeStart = content.find('"', typeKey + 7);
if (typeStart == std::string::npos) break;
typeStart++;
size_t typeEnd = content.find('"', typeStart);
if (typeEnd == std::string::npos) break;
std::string type = content.substr(typeStart, typeEnd - typeStart);

ScriptFileEntry fe;
fe.name = path;
fe.fullPath = "https://raw.githubusercontent.com/" + owner + "/" + br + "/" + path;
fe.isDirectory = (type == "tree");

// Filter to source-like files.
if (!fe.isDirectory) {
std::filesystem::path p(path);
std::string ext = p.extension().string();
static const std::vector<std::string> sourceExts = {
".cpp", ".h", ".py", ".js", ".ts", ".rs", ".go",
".glsl", ".vert", ".frag", ".sh", ".c", ".hpp",
".java", ".kt", ".swift", ".lua", ".rb", ".cs",
".md", ".txt", ".json", ".yaml", ".yml", ".toml"
};
bool isSource = false;
for (const auto & se : sourceExts) {
if (ext == se) { isSource = true; break; }
}
if (isSource) entries.push_back(fe);
}
pos = typeEnd + 1;
}
status = ofToString(entries.size()) + " files from GitHub";
}
}

// Update on main thread via mutex-protected state.
std::lock_guard<std::mutex> lock(outputMutex);
scriptSourceFiles = entries;
scriptSourceStatus = status;
}).detach();
}

// ---------------------------------------------------------------------------
// Script source — load file content into script input
// ---------------------------------------------------------------------------

void ofApp::loadScriptFile(int index) {
if (index < 0 || index >= static_cast<int>(scriptSourceFiles.size())) return;
const auto & entry = scriptSourceFiles[static_cast<size_t>(index)];
if (entry.isDirectory) return;

if (scriptSourceType == ScriptSourceType::LocalFolder) {
// Read local file.
ofBuffer buf = ofBufferFromFile(entry.fullPath);
if (buf.size() > 0) {
std::string content = buf.getText();
size_t maxLen = sizeof(scriptInput) - 1;
std::strncpy(scriptInput, content.c_str(), maxLen);
scriptInput[maxLen] = '\0';
scriptSourceStatus = "Loaded: " + entry.name;
}
} else if (scriptSourceType == ScriptSourceType::GitHubRepo) {
// Fetch raw file from GitHub — URL was built from validated owner/repo/branch.
// Additional safety: verify URL starts with expected prefix.
const std::string expectedPrefix = "https://raw.githubusercontent.com/";
if (entry.fullPath.substr(0, expectedPrefix.size()) != expectedPrefix) {
	scriptSourceStatus = "Invalid URL: " + entry.name;
	return;
}
std::string tempFile = ofFilePath::join(ofToDataPath("", true), "gh_file_dl.tmp");
std::string cmd = "curl -sL \"" + entry.fullPath + "\" -o \"" + tempFile + "\" 2>/dev/null";
int ret = std::system(cmd.c_str());
if (ret == 0) {
ofBuffer buf = ofBufferFromFile(tempFile);
if (buf.size() > 0) {
std::string content = buf.getText();
size_t maxLen = sizeof(scriptInput) - 1;
std::strncpy(scriptInput, content.c_str(), maxLen);
scriptInput[maxLen] = '\0';
scriptSourceStatus = "Loaded: " + entry.name;
}
std::error_code ec;
std::filesystem::remove(tempFile, ec);
} else {
scriptSourceStatus = "Failed to download: " + entry.name;
}
}
}

// ---------------------------------------------------------------------------
// Script source — save generated output to local folder
// ---------------------------------------------------------------------------

void ofApp::saveScriptToSource(const std::string & filename, const std::string & content) {
if (scriptSourceType != ScriptSourceType::LocalFolder || scriptSourcePath.empty()) return;

std::string fullPath = ofFilePath::join(scriptSourcePath, filename);
std::ofstream out(fullPath);
if (out.is_open()) {
out << content;
out.close();
scriptSourceStatus = "Saved: " + filename;
// Refresh file list.
scanLocalFolder(scriptSourcePath);
} else {
scriptSourceStatus = "Failed to save: " + filename;
}
}

std::string ofApp::buildScriptFilename() const {
std::string ext = ".txt";
if (!scriptLanguages.empty()) {
ext = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].fileExt;
}
auto now = std::chrono::system_clock::now();
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
now.time_since_epoch()).count();
return "generated_" + ofToString(ms) + ext;
}

// ---------------------------------------------------------------------------
// Summarize panel
// ---------------------------------------------------------------------------

void ofApp::drawSummarizePanel() {
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Summarize");
ImGui::SameLine();
ImGui::TextDisabled("(condense text into key points)");
ImGui::Separator();

ImGui::Text("Paste text to summarize:");
ImGui::InputTextMultiline("##SumIn", summarizeInput, sizeof(summarizeInput),
ImVec2(-1, 150));

ImGui::BeginDisabled(generating.load() || std::strlen(summarizeInput) == 0);
if (ImGui::Button("Summarize", ImVec2(140, 0))) {
runInference(AiMode::Summarize, summarizeInput);
}
ImGui::SameLine();
if (ImGui::Button("Key Points", ImVec2(140, 0))) {
std::string prompt = std::string("Extract key points from:\n") + summarizeInput;
runInference(AiMode::Summarize, prompt);
}
ImGui::SameLine();
if (ImGui::Button("TL;DR", ImVec2(140, 0))) {
std::string prompt = std::string("Give a one-sentence TL;DR of:\n") + summarizeInput;
runInference(AiMode::Summarize, prompt);
}
ImGui::EndDisabled();

ImGui::Separator();
ImGui::Text("Summary:");
ImGui::BeginChild("##SumOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", summarizeOutput.empty() ? "(no output yet)" : summarizeOutput.c_str());
ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Write panel
// ---------------------------------------------------------------------------

void ofApp::drawWritePanel() {
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Writing Assistant");
ImGui::SameLine();
ImGui::TextDisabled("(rewrite, expand, polish text)");
ImGui::Separator();

ImGui::Text("Enter your text:");
ImGui::InputTextMultiline("##WriteIn", writeInput, sizeof(writeInput),
ImVec2(-1, 120));

ImGui::BeginDisabled(generating.load() || std::strlen(writeInput) == 0);
if (ImGui::Button("Rewrite", ImVec2(110, 0))) {
std::string prompt = std::string("Rewrite the following more clearly:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Expand", ImVec2(110, 0))) {
std::string prompt = std::string("Expand on the following:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Make Formal", ImVec2(110, 0))) {
std::string prompt = std::string("Make this text more formal:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Make Casual", ImVec2(110, 0))) {
std::string prompt = std::string("Make this text more casual:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::SameLine();
if (ImGui::Button("Fix Grammar", ImVec2(110, 0))) {
std::string prompt = std::string("Fix grammar and spelling in:\n") + writeInput;
runInference(AiMode::Write, prompt);
}
ImGui::EndDisabled();

ImGui::Separator();
ImGui::Text("Output:");
ImGui::BeginChild("##WriteOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", writeOutput.empty() ? "(no output yet)" : writeOutput.c_str());
ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Custom panel
// ---------------------------------------------------------------------------

void ofApp::drawCustomPanel() {
ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Custom Prompt");
ImGui::SameLine();
ImGui::TextDisabled("(configure system prompt + user input)");
ImGui::Separator();

ImGui::Text("System prompt:");
ImGui::InputTextMultiline("##CustSys", customSystemPrompt, sizeof(customSystemPrompt),
ImVec2(-1, 60));

ImGui::Text("Your input:");
ImGui::InputTextMultiline("##CustIn", customInput, sizeof(customInput),
ImVec2(-1, 100));

ImGui::BeginDisabled(generating.load() || std::strlen(customInput) == 0);
if (ImGui::Button("Run", ImVec2(100, 0))) {
runInference(AiMode::Custom, customInput, customSystemPrompt);
}
ImGui::EndDisabled();

ImGui::Separator();
ImGui::Text("Output:");
ImGui::BeginChild("##CustOut", ImVec2(0, 0), true);
ImGui::TextWrapped("%s", customOutput.empty() ? "(no output yet)" : customOutput.c_str());
ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void ofApp::drawStatusBar() {
ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
ImGuiWindowFlags_NoScrollbar;

if (ImGui::Begin("##StatusBar", nullptr, flags)) {
ImGui::Text("Engine: %s", engineStatus.c_str());
ImGui::SameLine();
if (!modelPresets.empty()) {
ImGui::Text(" | Model: %s", modelPresets[static_cast<size_t>(selectedModelIndex)].name.c_str());
ImGui::SameLine();
}
ImGui::Text(" | Mode: %s", modeLabels[static_cast<int>(activeMode)]);
if (activeMode == AiMode::Script && !scriptLanguages.empty()) {
ImGui::SameLine();
ImGui::Text(" | Lang: %s", scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name.c_str());
}
ImGui::SameLine();
ImGui::Text(" | Tokens: %d  Temp: %.2f", maxTokens, temperature);
if (generating.load()) {
ImGui::SameLine();
ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), " | Generating...");
}
ImGui::SameLine();
ImGui::Text(" | FPS: %.0f", ofGetFrameRate());
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Device info window
// ---------------------------------------------------------------------------

void ofApp::drawDeviceInfoWindow() {
ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_FirstUseEver);
if (ImGui::Begin("Device Info", &showDeviceInfo)) {
ImGui::Text("Backend: %s", ggml.getBackendName().c_str());
ImGui::Text("State: %s", ofxGgmlHelpers::stateName(ggml.getState()).c_str());
ImGui::Separator();

if (devices.empty()) {
ImGui::TextDisabled("No devices discovered.");
} else {
for (size_t i = 0; i < devices.size(); i++) {
const auto & d = devices[i];
ImGui::PushID(static_cast<int>(i));
ImGui::Text("%s", d.name.c_str());
ImGui::SameLine();
ImGui::TextDisabled("(%s)", d.description.c_str());
ImGui::Text("  Type: %s  Memory: %s / %s",
ofxGgmlHelpers::backendTypeName(d.type).c_str(),
ofxGgmlHelpers::formatBytes(d.memoryFree).c_str(),
ofxGgmlHelpers::formatBytes(d.memoryTotal).c_str());
ImGui::Separator();
ImGui::PopID();
}
}
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Log window
// ---------------------------------------------------------------------------

void ofApp::drawLogWindow() {
ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
if (ImGui::Begin("Engine Log", &showLog)) {
if (ImGui::Button("Clear")) {
std::lock_guard<std::mutex> lock(logMutex);
logMessages.clear();
}
ImGui::Separator();
ImGui::BeginChild("##LogScroll", ImVec2(0, 0), false);
std::lock_guard<std::mutex> lock(logMutex);
for (const auto & line : logMessages) {
ImGui::TextWrapped("%s", line.c_str());
}
if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
ImGui::SetScrollHereY(1.0f);
}
ImGui::EndChild();
}
ImGui::End();
}

// ---------------------------------------------------------------------------
// Session persistence — escape / unescape helpers
// ---------------------------------------------------------------------------

std::string ofApp::escapeSessionText(const std::string & text) const {
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

std::string ofApp::unescapeSessionText(const std::string & text) const {
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
// Session persistence — save
// ---------------------------------------------------------------------------

bool ofApp::saveSession(const std::string & path) {
std::ofstream out(path);
if (!out.is_open()) return false;

out << "[session_v1]\n";

// Settings.
out << "mode=" << static_cast<int>(activeMode) << "\n";
out << "model=" << selectedModelIndex << "\n";
out << "language=" << selectedLanguageIndex << "\n";
out << "maxTokens=" << maxTokens << "\n";
out << "temperature=" << ofToString(temperature, 4) << "\n";
out << "numThreads=" << numThreads << "\n";

// Script source.
out << "scriptSourceType=" << static_cast<int>(scriptSourceType) << "\n";
out << "scriptSourcePath=" << escapeSessionText(scriptSourcePath) << "\n";
out << "scriptSourceGitHub=" << escapeSessionText(scriptSourceGitHub) << "\n";
out << "scriptSourceBranch=" << escapeSessionText(scriptSourceBranch) << "\n";

// Input buffers.
out << "chatInput=" << escapeSessionText(chatInput) << "\n";
out << "scriptInput=" << escapeSessionText(scriptInput) << "\n";
out << "summarizeInput=" << escapeSessionText(summarizeInput) << "\n";
out << "writeInput=" << escapeSessionText(writeInput) << "\n";
out << "customInput=" << escapeSessionText(customInput) << "\n";
out << "customSystemPrompt=" << escapeSessionText(customSystemPrompt) << "\n";

// Outputs.
out << "scriptOutput=" << escapeSessionText(scriptOutput) << "\n";
out << "summarizeOutput=" << escapeSessionText(summarizeOutput) << "\n";
out << "writeOutput=" << escapeSessionText(writeOutput) << "\n";
out << "customOutput=" << escapeSessionText(customOutput) << "\n";

// Chat messages.
out << "chatMessageCount=" << chatMessages.size() << "\n";
for (const auto & msg : chatMessages) {
out << "msg=" << escapeSessionText(msg.role) << "|"
<< ofToString(msg.timestamp, 2) << "|"
<< escapeSessionText(msg.text) << "\n";
}

out << "[/session_v1]\n";
out.close();
return true;
}

// ---------------------------------------------------------------------------
// Session persistence — load
// ---------------------------------------------------------------------------

bool ofApp::loadSession(const std::string & path) {
std::ifstream in(path);
if (!in.is_open()) return false;

std::string line;
if (!std::getline(in, line) || line != "[session_v1]") {
return false;
}

chatMessages.clear();

auto copyToBuf = [this](char * buf, size_t bufSize, const std::string & value) {
std::string text = unescapeSessionText(value);
std::strncpy(buf, text.c_str(), bufSize - 1);
buf[bufSize - 1] = '\0';
};

while (std::getline(in, line)) {
if (line == "[/session_v1]") break;

size_t eq = line.find('=');
if (eq == std::string::npos) continue;
std::string key = line.substr(0, eq);
std::string value = line.substr(eq + 1);

if (key == "mode") activeMode = static_cast<AiMode>(std::stoi(value));
else if (key == "model") {
	int maxIdx = static_cast<int>(modelPresets.size()) - 1;
	selectedModelIndex = std::clamp(std::stoi(value), 0, maxIdx);
}
else if (key == "language") {
	int maxIdx = static_cast<int>(scriptLanguages.size()) - 1;
	selectedLanguageIndex = std::clamp(std::stoi(value), 0, maxIdx);
}
else if (key == "maxTokens") maxTokens = std::clamp(std::stoi(value), 32, 2048);
else if (key == "temperature") temperature = std::clamp(std::stof(value), 0.0f, 2.0f);
else if (key == "numThreads") numThreads = std::clamp(std::stoi(value), 1, 16);
else if (key == "scriptSourceType") scriptSourceType = static_cast<ScriptSourceType>(std::stoi(value));
else if (key == "scriptSourcePath") scriptSourcePath = unescapeSessionText(value);
else if (key == "scriptSourceGitHub") copyToBuf(scriptSourceGitHub, sizeof(scriptSourceGitHub), value);
else if (key == "scriptSourceBranch") copyToBuf(scriptSourceBranch, sizeof(scriptSourceBranch), value);
else if (key == "chatInput") copyToBuf(chatInput, sizeof(chatInput), value);
else if (key == "scriptInput") copyToBuf(scriptInput, sizeof(scriptInput), value);
else if (key == "summarizeInput") copyToBuf(summarizeInput, sizeof(summarizeInput), value);
else if (key == "writeInput") copyToBuf(writeInput, sizeof(writeInput), value);
else if (key == "customInput") copyToBuf(customInput, sizeof(customInput), value);
else if (key == "customSystemPrompt") copyToBuf(customSystemPrompt, sizeof(customSystemPrompt), value);
else if (key == "scriptOutput") scriptOutput = unescapeSessionText(value);
else if (key == "summarizeOutput") summarizeOutput = unescapeSessionText(value);
else if (key == "writeOutput") writeOutput = unescapeSessionText(value);
else if (key == "customOutput") customOutput = unescapeSessionText(value);
else if (key == "msg") {
// Parse: role|timestamp|text
size_t sep1 = value.find('|');
if (sep1 != std::string::npos) {
size_t sep2 = value.find('|', sep1 + 1);
if (sep2 != std::string::npos) {
Message msg;
msg.role = unescapeSessionText(value.substr(0, sep1));
msg.timestamp = std::stof(value.substr(sep1 + 1, sep2 - sep1 - 1));
msg.text = unescapeSessionText(value.substr(sep2 + 1));
chatMessages.push_back(msg);
}
}
}
}

in.close();

// Re-scan the script source if one was loaded.
if (scriptSourceType == ScriptSourceType::LocalFolder && !scriptSourcePath.empty()) {
scanLocalFolder(scriptSourcePath);
}

return true;
}

void ofApp::autoSaveSession() {
saveSession(lastSessionPath);
}

void ofApp::autoLoadSession() {
std::error_code ec;
if (std::filesystem::exists(lastSessionPath, ec) && !ec) {
loadSession(lastSessionPath);
}
}

// ---------------------------------------------------------------------------
// Inference — background thread
// ---------------------------------------------------------------------------

void ofApp::runInference(AiMode mode, const std::string & userText,
const std::string & systemPrompt) {
if (generating.load() || !engineReady) return;

generating.store(true);

// Detach previous thread if any.
if (workerThread.joinable()) {
workerThread.join();
}

workerThread = std::thread([this, mode, userText, systemPrompt]() {
std::string result = runDemoComputation(userText, mode, systemPrompt);

{
std::lock_guard<std::mutex> lock(outputMutex);
pendingOutput = result;
pendingRole = "assistant";
pendingMode = mode;
}
generating.store(false);
});
}

void ofApp::applyPendingOutput() {
std::lock_guard<std::mutex> lock(outputMutex);
if (pendingOutput.empty()) return;

switch (pendingMode) {
case AiMode::Chat:
chatMessages.push_back({"assistant", pendingOutput, ofGetElapsedTimef()});
break;
case AiMode::Script:
scriptOutput = pendingOutput;
break;
case AiMode::Summarize:
summarizeOutput = pendingOutput;
break;
case AiMode::Write:
writeOutput = pendingOutput;
break;
case AiMode::Custom:
customOutput = pendingOutput;
break;
}
pendingOutput.clear();
}

// ---------------------------------------------------------------------------
// Demo computation — runs a ggml graph and produces illustrative output.
//
// NOTE: This demonstrates the ofxGgml compute pipeline.  Real inference
// requires loading model weights from a GGUF file.  The build-ggml.sh and
// download-model.sh scripts in scripts/ prepare the prerequisites.
// ---------------------------------------------------------------------------

std::string ofApp::runDemoComputation(const std::string & input, AiMode mode,
const std::string & systemPrompt) {

// Build a small ggml graph that processes the input length as a "feature"
// through a random linear layer + softmax to produce demo probabilities.
// This shows the full ofxGgml pipeline: tensor creation -> graph build ->
// data upload -> compute -> result retrieval.

const int inputLen = static_cast<int>(input.size());
const int featureDim = 8;
const int outputDim = 4;

ofxGgmlGraph graph;

// Input: encode the string length + character stats as features.
auto inputTensor = graph.newTensor2d(ofxGgmlType::F32, featureDim, 1);
auto weights     = graph.newTensor2d(ofxGgmlType::F32, featureDim, outputDim);
auto bias        = graph.newTensor1d(ofxGgmlType::F32, outputDim);

inputTensor.setName("input");
weights.setName("weights");
bias.setName("bias");

graph.setInput(inputTensor);
graph.setInput(weights);
graph.setInput(bias);

// Linear layer: softmax(W * x + b)
auto h = graph.matMul(weights, inputTensor);
auto hb = graph.add(h, bias);
auto output = graph.softmax(hb);

output.setName("output");
graph.setOutput(output);
graph.build(output);

// Prepare input features from the text.
std::vector<float> features(featureDim, 0.0f);
const float normalizer = std::max(1.0f, static_cast<float>(inputLen));
features[0] = static_cast<float>(inputLen) / 100.0f;
int spaceCount = 0, upperCount = 0, digitCount = 0, punctCount = 0;
for (char c : input) {
if (c == ' ') spaceCount++;
if (std::isupper(static_cast<unsigned char>(c))) upperCount++;
if (std::isdigit(static_cast<unsigned char>(c))) digitCount++;
if (std::ispunct(static_cast<unsigned char>(c))) punctCount++;
}
features[1] = static_cast<float>(spaceCount) / normalizer;
features[2] = static_cast<float>(upperCount) / normalizer;
features[3] = static_cast<float>(digitCount) / normalizer;
features[4] = static_cast<float>(punctCount) / normalizer;
features[5] = temperature;
features[6] = static_cast<float>(maxTokens) / 2048.0f;

float modeValue = 0.0f;
switch (mode) {
case AiMode::Chat:      modeValue = 1.0f; break;
case AiMode::Script:    modeValue = 2.0f; break;
case AiMode::Summarize: modeValue = 3.0f; break;
case AiMode::Write:     modeValue = 4.0f; break;
case AiMode::Custom:    modeValue = 5.0f; break;
}
features[7] = modeValue / 5.0f;

// Random weights seeded from input hash.
std::hash<std::string> hasher;
std::mt19937 rng(static_cast<unsigned>(hasher(input + systemPrompt)));
std::normal_distribution<float> dist(0.0f, 0.5f);

std::vector<float> wData(featureDim * outputDim);
std::vector<float> bData(outputDim);
for (auto & v : wData) v = dist(rng);
for (auto & v : bData) v = dist(rng) * 0.1f;

ggml.setTensorData(inputTensor, features.data(), features.size() * sizeof(float));
ggml.setTensorData(weights, wData.data(), wData.size() * sizeof(float));
ggml.setTensorData(bias, bData.data(), bData.size() * sizeof(float));

// Compute.
auto result = ggml.compute(graph);

if (!result.success) {
return "[Error] Computation failed: " + result.error;
}

// Read output probabilities.
std::vector<float> probs(outputDim);
ggml.getTensorData(output, probs.data(), probs.size() * sizeof(float));

// Build response string based on mode.
std::ostringstream oss;
oss << "[ggml compute OK - " << ofToString(result.elapsedMs, 2) << " ms, "
<< graph.getNumNodes() << " nodes, backend: " << ggml.getBackendName() << "]\n\n";

// Include selected model info.
if (!modelPresets.empty()) {
oss << "[Model: " << modelPresets[static_cast<size_t>(selectedModelIndex)].name << "]\n\n";
}

switch (mode) {
case AiMode::Chat:
oss << "Input analyzed (" << inputLen << " chars). ";
oss << "Confidence distribution: ";
for (int i = 0; i < outputDim; i++) {
oss << "class" << i << "=" << ofToString(probs[static_cast<size_t>(i)] * 100.0f, 1) << "% ";
}
oss << "\n\nThis is a demo - to run real language-model inference, "
<< "use scripts/build-ggml.sh to compile ggml, then "
<< "scripts/download-model.sh to fetch a GGUF model.";
break;

case AiMode::Script: {
std::string langName = "Text";
if (!scriptLanguages.empty()) {
langName = scriptLanguages[static_cast<size_t>(selectedLanguageIndex)].name;
}
oss << "// Generated " << langName << " code skeleton (demo)\n";
oss << "// Input: \"" << input.substr(0, 60) << (input.size() > 60 ? "..." : "") << "\"\n";
oss << "// Features: len=" << inputLen << " words~=" << (spaceCount + 1) << "\n\n";
oss << "void generatedFunction() {\n";
oss << "    // Probability distribution from ggml softmax:\n";
for (int i = 0; i < outputDim; i++) {
oss << "    float p" << i << " = " << ofToString(probs[static_cast<size_t>(i)], 4) << "f;\n";
}
oss << "    // TODO: load a real model for code generation.\n";
oss << "}\n";
break;
}

case AiMode::Summarize:
oss << "Summary of " << inputLen << "-character text:\n";
oss << "  Word count (approx): " << (spaceCount + 1) << "\n";
oss << "  Uppercase ratio: " << ofToString(features[2] * 100.0f, 1) << "%\n";
oss << "  Digit content: " << ofToString(features[3] * 100.0f, 1) << "%\n";
oss << "  Computed class probabilities: [";
for (int i = 0; i < outputDim; i++) {
if (i > 0) oss << ", ";
oss << ofToString(probs[static_cast<size_t>(i)], 3);
}
oss << "]\n\nFor real summarization, load a language model via GGUF.";
break;

case AiMode::Write:
oss << "Processed " << inputLen << " characters through ggml pipeline.\n";
oss << "Softmax output: [";
for (int i = 0; i < outputDim; i++) {
if (i > 0) oss << ", ";
oss << ofToString(probs[static_cast<size_t>(i)], 4);
}
oss << "]\n\nTo rewrite/expand text, a full language model is needed. "
<< "Run scripts/download-model.sh to get one.";
break;

case AiMode::Custom:
oss << "System: " << (systemPrompt.empty() ? "(none)" : systemPrompt.substr(0, 80)) << "\n";
oss << "Input: " << input.substr(0, 80) << (input.size() > 80 ? "..." : "") << "\n\n";
oss << "ggml output (" << outputDim << "-dim softmax): [";
for (int i = 0; i < outputDim; i++) {
if (i > 0) oss << ", ";
oss << ofToString(probs[static_cast<size_t>(i)], 4);
}
oss << "]\nElapsed: " << ofToString(result.elapsedMs, 2) << " ms";
break;
}

return oss.str();
}
