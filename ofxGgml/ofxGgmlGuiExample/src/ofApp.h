#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxGgml.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Mode — the active AI task tab.
// ---------------------------------------------------------------------------

enum class AiMode {
	Chat,
	Script,
	Summarize,
	Write,
	Custom
};

// ---------------------------------------------------------------------------
// Message — a single chat/output entry.
// ---------------------------------------------------------------------------

struct Message {
	std::string role;   // "user", "assistant", "system"
	std::string text;
	float timestamp = 0.0f;
};

// ---------------------------------------------------------------------------
// ofApp — ofxGgml AI Studio with ofxImGui
// ---------------------------------------------------------------------------

class ofApp : public ofBaseApp {
public:
	void setup();
	void update();
	void draw();
	void exit();
	void keyPressed(int key);

private:
	// -- ggml engine --
	ofxGgml ggml;
	bool engineReady = false;
	std::string engineStatus;
	std::vector<ofxGgmlDeviceInfo> devices;

	// -- ImGui --
	ofxImGui::Gui gui;

	// -- mode --
	AiMode activeMode = AiMode::Chat;
	static constexpr int kModeCount = 5;
	static const char * modeLabels[kModeCount];

	// -- input buffers --
	char chatInput[4096] = {};
	char scriptInput[8192] = {};
	char summarizeInput[8192] = {};
	char writeInput[4096] = {};
	char customInput[4096] = {};
	char customSystemPrompt[2048] = {};

	// -- conversation / output --
	std::deque<Message> chatMessages;
	std::string scriptOutput;
	std::string summarizeOutput;
	std::string writeOutput;
	std::string customOutput;

	// -- generation state --
	std::atomic<bool> generating{false};
	std::string generatingStatus;
	std::thread workerThread;
	std::mutex outputMutex;
	std::string pendingOutput;
	std::string pendingRole;
	AiMode pendingMode = AiMode::Chat;

	// -- settings --
	int maxTokens = 256;
	float temperature = 0.7f;
	int numThreads = 4;
	bool showDeviceInfo = false;
	bool showLog = false;
	std::deque<std::string> logMessages;
	std::mutex logMutex;

	// -- graph execution helper --
	void runInference(AiMode mode, const std::string & userText,
		const std::string & systemPrompt = "");
	void applyPendingOutput();

	// -- UI panels --
	void drawMenuBar();
	void drawSidebar();
	void drawMainPanel();
	void drawChatPanel();
	void drawScriptPanel();
	void drawSummarizePanel();
	void drawWritePanel();
	void drawCustomPanel();
	void drawStatusBar();
	void drawDeviceInfoWindow();
	void drawLogWindow();

	// -- ggml demo computation --
	std::string runDemoComputation(const std::string & input, AiMode mode,
		const std::string & systemPrompt);
};
