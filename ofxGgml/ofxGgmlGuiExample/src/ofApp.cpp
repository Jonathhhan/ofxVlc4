#include "ofApp.h"
#include "support/ofxGgmlHelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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
// Lifecycle
// ---------------------------------------------------------------------------

void ofApp::setup() {
	ofSetWindowTitle("ofxGgml AI Studio");
	ofSetFrameRate(60);
	ofSetBackgroundColor(ofColor(30, 30, 34));

	gui.setup(nullptr, true, ImGuiConfigFlags_None, true);
	ImGui::GetIO().IniFilename = "imgui_ggml_studio.ini";

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
	const float sidebarW = 200.0f;

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
// Sidebar — mode selection + quick settings
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
// Script panel
// ---------------------------------------------------------------------------

void ofApp::drawScriptPanel() {
	ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Script Generation");
	ImGui::SameLine();
	ImGui::TextDisabled("(generate or explain code)");
	ImGui::Separator();

	ImGui::Text("Describe what you want:");
	ImGui::InputTextMultiline("##ScriptIn", scriptInput, sizeof(scriptInput),
		ImVec2(-1, 100));

	ImGui::BeginDisabled(generating.load() || std::strlen(scriptInput) == 0);
	if (ImGui::Button("Generate Code", ImVec2(140, 0))) {
		runInference(AiMode::Script, scriptInput);
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

	ImGui::Separator();
	ImGui::Text("Output:");
	ImGui::BeginChild("##ScriptOut", ImVec2(0, 0), true);
	ImGui::TextWrapped("%s", scriptOutput.empty() ? "(no output yet)" : scriptOutput.c_str());
	ImGui::EndChild();
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
		ImGui::Text(" | Mode: %s", modeLabels[static_cast<int>(activeMode)]);
		ImGui::SameLine();
		ImGui::Text(" | Tokens: %d  Temp: %.2f  Threads: %d",
			maxTokens, temperature, numThreads);
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
	// This shows the full ofxGgml pipeline: tensor creation → graph build →
	// data upload → compute → result retrieval.

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
	features[0] = static_cast<float>(inputLen) / 100.0f;
	int spaceCount = 0, upperCount = 0, digitCount = 0, punctCount = 0;
	for (char c : input) {
		if (c == ' ') spaceCount++;
		if (std::isupper(static_cast<unsigned char>(c))) upperCount++;
		if (std::isdigit(static_cast<unsigned char>(c))) digitCount++;
		if (std::ispunct(static_cast<unsigned char>(c))) punctCount++;
	}
	features[1] = static_cast<float>(spaceCount) / std::max(1.0f, static_cast<float>(inputLen));
	features[2] = static_cast<float>(upperCount) / std::max(1.0f, static_cast<float>(inputLen));
	features[3] = static_cast<float>(digitCount) / std::max(1.0f, static_cast<float>(inputLen));
	features[4] = static_cast<float>(punctCount) / std::max(1.0f, static_cast<float>(inputLen));
	features[5] = temperature;
	features[6] = static_cast<float>(maxTokens) / 2048.0f;
	features[7] = static_cast<float>(mode == AiMode::Chat ? 1 : (mode == AiMode::Script ? 2 : 3)) / 5.0f;

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
	oss << "[ggml compute OK — " << ofToString(result.elapsedMs, 2) << " ms, "
		<< graph.getNumNodes() << " nodes, backend: " << ggml.getBackendName() << "]\n\n";

	switch (mode) {
	case AiMode::Chat:
		oss << "Input analyzed (" << inputLen << " chars). ";
		oss << "Confidence distribution: ";
		for (int i = 0; i < outputDim; i++) {
			oss << "class" << i << "=" << ofToString(probs[static_cast<size_t>(i)] * 100.0f, 1) << "% ";
		}
		oss << "\n\nThis is a demo — to run real language-model inference, "
			<< "use scripts/build-ggml.sh to compile ggml, then "
			<< "scripts/download-model.sh to fetch a GGUF model.";
		break;

	case AiMode::Script:
		oss << "// Generated code skeleton (demo)\n";
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

	case AiMode::Summarize:
		oss << "Summary of " << inputLen << "-character text:\n";
		oss << "• Word count (approx): " << (spaceCount + 1) << "\n";
		oss << "• Uppercase ratio: " << ofToString(features[2] * 100.0f, 1) << "%\n";
		oss << "• Digit content: " << ofToString(features[3] * 100.0f, 1) << "%\n";
		oss << "• Computed class probabilities: [";
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
