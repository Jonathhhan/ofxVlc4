#include "ofVlcPlayer4GuiVisualizer.h"
#include "ofVlcPlayer4GuiControls.h"
#include "ofVlcPlayer4GuiStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace {
constexpr size_t kEqualizerSpectrumPointCount = 512;
constexpr int kAnalyzerBarCount = 72;
constexpr float kAnalyzerPeakHoldSeconds = 0.32f;
constexpr float kAnalyzerPeakReleasePerSecond = 1.10f;
constexpr size_t kVisualizerAudioFrameCount = 1024;

constexpr std::array<ofVlcPlayer4GuiVisualizer::DisplayStyle, 6> kDisplayStyles = {
	ofVlcPlayer4GuiVisualizer::DisplayStyle::Studio,
	ofVlcPlayer4GuiVisualizer::DisplayStyle::Mastering,
	ofVlcPlayer4GuiVisualizer::DisplayStyle::RtaBars,
	ofVlcPlayer4GuiVisualizer::DisplayStyle::Hybrid,
	ofVlcPlayer4GuiVisualizer::DisplayStyle::Waveform,
	ofVlcPlayer4GuiVisualizer::DisplayStyle::Vectorscope
};

constexpr std::array<ofVlcPlayer4GuiVisualizer::DbScale, 3> kDbScales = {
	ofVlcPlayer4GuiVisualizer::DbScale::Broadcast,
	ofVlcPlayer4GuiVisualizer::DbScale::Studio,
	ofVlcPlayer4GuiVisualizer::DbScale::Wide
};

using ofVlcPlayer4GuiStyle::themedAccentBrightColorWithAlpha;
using ofVlcPlayer4GuiStyle::themedAccentColorWithAlpha;
using ofVlcPlayer4GuiStyle::themedBorderColor;
using ofVlcPlayer4GuiStyle::themedCheckColor;
using ofVlcPlayer4GuiStyle::themedChildBgColor;
using ofVlcPlayer4GuiStyle::themedFrameColor;
using ofVlcPlayer4GuiStyle::themedGuideColor;

const char * displayStyleLabel(ofVlcPlayer4GuiVisualizer::DisplayStyle style) {
	switch (style) {
	case ofVlcPlayer4GuiVisualizer::DisplayStyle::Waveform:
		return "Waveform";
	case ofVlcPlayer4GuiVisualizer::DisplayStyle::Vectorscope:
		return "Vectorscope";
	case ofVlcPlayer4GuiVisualizer::DisplayStyle::Mastering:
		return "Spectrum + Peaks";
	case ofVlcPlayer4GuiVisualizer::DisplayStyle::RtaBars:
		return "Bars + Peaks";
	case ofVlcPlayer4GuiVisualizer::DisplayStyle::Hybrid:
		return "Hybrid";
	case ofVlcPlayer4GuiVisualizer::DisplayStyle::Studio:
	default:
		return "Filled Spectrum";
	}
}

const char * dbScaleLabel(ofVlcPlayer4GuiVisualizer::DbScale scale) {
	switch (scale) {
	case ofVlcPlayer4GuiVisualizer::DbScale::Broadcast:
		return "-48 to +3 dBFS";
	case ofVlcPlayer4GuiVisualizer::DbScale::Wide:
		return "-96 to 0 dBFS";
	case ofVlcPlayer4GuiVisualizer::DbScale::Studio:
	default:
		return "-72 to 0 dBFS";
	}
}

const char * audioVisualizerModuleLabel(ofxVlc4AudioVisualizerModule module) {
	switch (module) {
	case ofxVlc4AudioVisualizerModule::Visual:
		return "Visual";
	case ofxVlc4AudioVisualizerModule::Goom:
		return "Goom";
	case ofxVlc4AudioVisualizerModule::Glspectrum:
		return "GL Spectrum";
	case ofxVlc4AudioVisualizerModule::ProjectM:
		return "VLC projectM";
	case ofxVlc4AudioVisualizerModule::None:
	default:
		return "None";
	}
}

const char * audioVisualizerEffectLabel(ofxVlc4AudioVisualizerEffect effect) {
	switch (effect) {
	case ofxVlc4AudioVisualizerEffect::Scope:
		return "Scope";
	case ofxVlc4AudioVisualizerEffect::Spectrometer:
		return "Spectrometer";
	case ofxVlc4AudioVisualizerEffect::VuMeter:
		return "VU Meter";
	case ofxVlc4AudioVisualizerEffect::Spectrum:
	default:
		return "Spectrum";
	}
}

std::string defaultProjectMPresetPath() {
	const std::string presetPath = ofToDataPath("presets", true);
	if (ofDirectory::doesDirectoryExist(presetPath, true)) {
		return ofFilePath::getAbsolutePath(presetPath, true);
	}
	return "";
}

void applyModuleSpecificVisualizerDefaults(ofxVlc4AudioVisualizerSettings & settings) {
	switch (settings.module) {
	case ofxVlc4AudioVisualizerModule::Goom:
		settings.width = 640;
		settings.height = 480;
		settings.goomSpeed = ofClamp(settings.goomSpeed, 1, 10);
		break;
	case ofxVlc4AudioVisualizerModule::ProjectM:
		settings.width = 1280;
		settings.height = 720;
		if (settings.projectMPresetPath.empty()) {
			settings.projectMPresetPath = defaultProjectMPresetPath();
		}
		break;
	case ofxVlc4AudioVisualizerModule::Visual:
	case ofxVlc4AudioVisualizerModule::Glspectrum:
		settings.width = 1280;
		settings.height = 720;
		break;
	case ofxVlc4AudioVisualizerModule::None:
	default:
		break;
	}
}

std::pair<float, float> dbScaleRange(ofVlcPlayer4GuiVisualizer::DbScale scale) {
	switch (scale) {
	case ofVlcPlayer4GuiVisualizer::DbScale::Broadcast:
		return { -48.0f, 3.0f };
	case ofVlcPlayer4GuiVisualizer::DbScale::Wide:
		return { -96.0f, 0.0f };
	case ofVlcPlayer4GuiVisualizer::DbScale::Studio:
	default:
		return { -72.0f, 0.0f };
	}
}

std::vector<float> readLatestInterleavedFrames(ofxVlc4 & player, size_t frameCount, int & channelCountOut) {
	channelCountOut = std::max(1, player.getChannelCount());
	std::vector<float> interleaved(frameCount * static_cast<size_t>(channelCountOut), 0.0f);
	if (interleaved.empty()) {
		return {};
	}

	player.peekLatestAudioSamples(interleaved.data(), interleaved.size());
	return interleaved;
}

std::vector<float> buildMonoFramesFromInterleaved(const std::vector<float> & interleaved, int channelCount) {
	if (channelCount <= 0 || interleaved.empty()) {
		return {};
	}

	const size_t frameCount = interleaved.size() / static_cast<size_t>(channelCount);
	std::vector<float> mono(frameCount, 0.0f);
	for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
		float sum = 0.0f;
		for (int channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
			sum += interleaved[(frameIndex * static_cast<size_t>(channelCount)) + static_cast<size_t>(channelIndex)];
		}
		mono[frameIndex] = sum / static_cast<float>(channelCount);
	}

	return mono;
}

std::vector<float> buildTriggeredScopeSamples(const std::vector<float> & monoSamples, size_t pointCount) {
	std::vector<float> points(pointCount, 0.0f);
	if (monoSamples.empty() || pointCount == 0) {
		return points;
	}

	size_t startIndex = 0;
	float bestSlope = 0.0f;
	for (size_t i = 1; i < monoSamples.size(); ++i) {
		const float previous = monoSamples[i - 1];
		const float current = monoSamples[i];
		if (previous <= 0.0f && current > 0.0f) {
			const float slope = current - previous;
			if (slope > bestSlope) {
				bestSlope = slope;
				startIndex = i;
			}
		}
	}

	const size_t available = monoSamples.size() - std::min(startIndex, monoSamples.size());
	if (available == 0) {
		return points;
	}

	for (size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
		const float t = pointCount <= 1
			? 0.0f
			: static_cast<float>(pointIndex) / static_cast<float>(pointCount - 1);
		const size_t sampleIndex = std::min(
			startIndex + static_cast<size_t>(std::round(t * static_cast<float>(available - 1))),
			monoSamples.size() - 1);
		points[pointIndex] = monoSamples[sampleIndex];
	}

	return points;
}

std::string formatFrequency(float frequencyHz) {
	if (frequencyHz <= 0.0f) {
		return "";
	}

	std::ostringstream stream;
	if (frequencyHz >= 1000.0f) {
		const float kiloHertz = frequencyHz / 1000.0f;
		const float roundedKiloHertz = std::round(kiloHertz);
		if (std::abs(kiloHertz - roundedKiloHertz) < 0.05f) {
			stream << std::fixed << std::setprecision(0) << roundedKiloHertz << " kHz";
		} else {
			stream << std::fixed << std::setprecision(1) << kiloHertz << " kHz";
		}
	} else {
		stream << std::fixed << std::setprecision(0) << frequencyHz << " Hz";
	}
	return stream.str();
}

std::string formatMilliseconds(float milliseconds) {
	std::ostringstream stream;
	if (std::abs(milliseconds) >= 1000.0f) {
		stream << std::fixed << std::setprecision(2) << (milliseconds / 1000.0f) << " s";
	} else {
		stream << std::fixed << std::setprecision(1) << milliseconds << " ms";
	}
	return stream.str();
}

std::string formatMicroseconds(double microseconds) {
	std::ostringstream stream;
	if (microseconds >= 1000.0) {
		stream << std::fixed << std::setprecision(3) << (microseconds / 1000.0) << " ms";
	} else {
		stream << std::fixed << std::setprecision(1) << microseconds << " us";
	}
	return stream.str();
}
}

void ofVlcPlayer4GuiVisualizer::drawVlcModuleControls(
	ofxVlc4 & player,
	const ImVec2 & labelInnerSpacing,
	float compactControlWidth,
	const std::function<void()> & applyAudioVisualizerSettings) {
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(compactControlWidth);

	if (!vlcVisualizerStateInitialized) {
		pendingVlcVisualizerSettings = player.getAudioVisualizerSettings();
		vlcVisualizerStateInitialized = true;
	}

	if (projectMPresetPath[0] == '\0' && !pendingVlcVisualizerSettings.projectMPresetPath.empty()) {
		strncpy_s(
			projectMPresetPath,
			sizeof(projectMPresetPath),
			pendingVlcVisualizerSettings.projectMPresetPath.c_str(),
			_TRUNCATE);
	}

	int moduleIndex = static_cast<int>(pendingVlcVisualizerSettings.module);
	const auto previousModule = pendingVlcVisualizerSettings.module;
	if (ImGui::BeginCombo("VLC Module", audioVisualizerModuleLabel(pendingVlcVisualizerSettings.module))) {
		for (int candidateIndex = static_cast<int>(ofxVlc4AudioVisualizerModule::None);
			candidateIndex <= static_cast<int>(ofxVlc4AudioVisualizerModule::ProjectM);
			++candidateIndex) {
			const auto candidate = static_cast<ofxVlc4AudioVisualizerModule>(candidateIndex);
			const bool selected = (pendingVlcVisualizerSettings.module == candidate);
			if (ImGui::Selectable(audioVisualizerModuleLabel(candidate), selected)) {
				moduleIndex = candidateIndex;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	pendingVlcVisualizerSettings.module = static_cast<ofxVlc4AudioVisualizerModule>(moduleIndex);
	if (pendingVlcVisualizerSettings.module != previousModule) {
		applyModuleSpecificVisualizerDefaults(pendingVlcVisualizerSettings);
		if (pendingVlcVisualizerSettings.module == ofxVlc4AudioVisualizerModule::ProjectM) {
			strncpy_s(
				projectMPresetPath,
				sizeof(projectMPresetPath),
				pendingVlcVisualizerSettings.projectMPresetPath.c_str(),
				_TRUNCATE);
		}
	}

	if (pendingVlcVisualizerSettings.module == ofxVlc4AudioVisualizerModule::Visual) {
		int effectIndex = static_cast<int>(pendingVlcVisualizerSettings.visualEffect);
		if (ImGui::BeginCombo("Visual Effect", audioVisualizerEffectLabel(pendingVlcVisualizerSettings.visualEffect))) {
			for (int candidateIndex = static_cast<int>(ofxVlc4AudioVisualizerEffect::Spectrum);
				candidateIndex <= static_cast<int>(ofxVlc4AudioVisualizerEffect::VuMeter);
				++candidateIndex) {
				const auto candidate = static_cast<ofxVlc4AudioVisualizerEffect>(candidateIndex);
				const bool selected = (pendingVlcVisualizerSettings.visualEffect == candidate);
				if (ImGui::Selectable(audioVisualizerEffectLabel(candidate), selected)) {
					effectIndex = candidateIndex;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		pendingVlcVisualizerSettings.visualEffect = static_cast<ofxVlc4AudioVisualizerEffect>(effectIndex);
	}

	if (pendingVlcVisualizerSettings.module != ofxVlc4AudioVisualizerModule::None) {
		int width = pendingVlcVisualizerSettings.width;
		int height = pendingVlcVisualizerSettings.height;
		ImGui::InputInt("Module Width", &width);
		ImGui::InputInt("Module Height", &height);
		pendingVlcVisualizerSettings.width = std::max(64, width);
		pendingVlcVisualizerSettings.height = std::max(64, height);

		if (pendingVlcVisualizerSettings.module == ofxVlc4AudioVisualizerModule::Goom) {
			int goomSpeed = pendingVlcVisualizerSettings.goomSpeed;
			ImGui::SliderInt("Goom Speed", &goomSpeed, 1, 10);
			pendingVlcVisualizerSettings.goomSpeed = goomSpeed;
		}

		if (pendingVlcVisualizerSettings.module == ofxVlc4AudioVisualizerModule::ProjectM) {
			if (pendingVlcVisualizerSettings.projectMPresetPath.empty()) {
				pendingVlcVisualizerSettings.projectMPresetPath = defaultProjectMPresetPath();
				strncpy_s(
					projectMPresetPath,
					sizeof(projectMPresetPath),
					pendingVlcVisualizerSettings.projectMPresetPath.c_str(),
					_TRUNCATE);
			}
			ImGui::InputText("Preset Path", projectMPresetPath, IM_ARRAYSIZE(projectMPresetPath));
			pendingVlcVisualizerSettings.projectMPresetPath = ofTrim(std::string(projectMPresetPath));
		}
	}

	if (ImGui::Button("Apply VLC Visualizer", ImVec2(compactControlWidth, 0.0f))) {
		player.setAudioVisualizerSettings(pendingVlcVisualizerSettings);
		if (applyAudioVisualizerSettings) {
			applyAudioVisualizerSettings();
		}
	}
	ImGui::TextDisabled("Reinitializes the VLC player. 'None' disables VLC visualizer modules.");
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4GuiVisualizer::drawContent(
	ofxVlc4 & player,
	const ImVec2 & labelInnerSpacing,
	float compactControlWidth) {
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(compactControlWidth);

	ImGui::Separator();

	int styleIndex = static_cast<int>(displayStyle);
	if (ImGui::BeginCombo("Style", displayStyleLabel(displayStyle))) {
		for (DisplayStyle style : kDisplayStyles) {
			const bool selected = (displayStyle == style);
			if (ImGui::Selectable(displayStyleLabel(style), selected)) {
				displayStyle = style;
				styleIndex = static_cast<int>(displayStyle);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ofVlcPlayer4GuiControls::applyHoveredComboWheelStep(styleIndex, 0, static_cast<int>(kDisplayStyles.size()) - 1)) {
		displayStyle = kDisplayStyles[static_cast<size_t>(styleIndex)];
	}

	int scaleIndex = static_cast<int>(dbScale);
	if (ImGui::BeginCombo("Scale", dbScaleLabel(dbScale))) {
		for (DbScale scale : kDbScales) {
			const bool selected = (dbScale == scale);
			if (ImGui::Selectable(dbScaleLabel(scale), selected)) {
				dbScale = scale;
				scaleIndex = static_cast<int>(dbScale);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ofVlcPlayer4GuiControls::applyHoveredComboWheelStep(scaleIndex, 0, static_cast<int>(kDbScales.size()) - 1)) {
		dbScale = kDbScales[static_cast<size_t>(scaleIndex)];
	}

	constexpr float kWaveformGain = 1.2f;
	constexpr float kVectorscopeGain = 1.15f;
	constexpr float kVuGain = 1.8f;
	constexpr float kVuPeakHoldSeconds = 0.7f;
	constexpr float kVuReleasePerSecond = 1.8f;
	constexpr float kVuMeterWidth = 14.0f;
	constexpr float kVuChannelGap = 0.0f;
	constexpr float kVuPanelInset = 1.0f;
	const std::vector<float> spectrumLevels = player.getEqualizerSpectrumLevels(kEqualizerSpectrumPointCount);
	int audioChannelCount = 1;
	const int sampleRate = player.getSampleRate();
	const auto audioState = player.getAudioStateInfo();
	const auto & callbackPerformance = audioState.callbackPerformance;
	const std::vector<float> interleavedAudioFrames = readLatestInterleavedFrames(player, kVisualizerAudioFrameCount, audioChannelCount);
	const std::vector<float> monoAudioFrames = buildMonoFramesFromInterleaved(interleavedAudioFrames, audioChannelCount);
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const float visualizerHeight = 150.0f;
	const float vuPanelWidth = (kVuPanelInset * 2.0f) + (kVuMeterWidth * 2.0f) + kVuChannelGap;
	const float vuPanelGap = 12.0f;
	const bool squareVisualizerGraph = (displayStyle == DisplayStyle::Vectorscope);
	const float graphWidthTarget = squareVisualizerGraph
		? 150.0f
		: std::max(60.0f, availableWidth - vuPanelWidth - vuPanelGap);
	const ImVec2 rowSize(
		squareVisualizerGraph
			? std::min(availableWidth, vuPanelWidth + vuPanelGap + graphWidthTarget)
			: availableWidth,
		visualizerHeight);
	const ImVec2 rowPos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##visualizerRow", rowSize);

	ImDrawList * drawList = ImGui::GetWindowDrawList();
	const ImVec2 meterMin = rowPos;
	const ImVec2 meterMax(meterMin.x + vuPanelWidth, meterMin.y + rowSize.y);
	const ImVec2 graphMin(meterMax.x + vuPanelGap, rowPos.y);
	const ImVec2 graphMax(rowPos.x + rowSize.x, rowPos.y + rowSize.y);
	const float graphWidth = std::max(1.0f, graphMax.x - graphMin.x);
	const float graphHeight = std::max(1.0f, graphMax.y - graphMin.y);
	const ImVec2 mousePos = ImGui::GetIO().MousePos;
	const bool graphHovered =
		mousePos.x >= graphMin.x && mousePos.x <= graphMax.x &&
		mousePos.y >= graphMin.y && mousePos.y <= graphMax.y;
	const auto [visualizerMinDb, visualizerMaxDb] = dbScaleRange(dbScale);
	const ImVec4 panelBgColor = themedChildBgColor();
	const ImVec4 borderColor = themedBorderColor();
	const ImVec4 gridColor = themedFrameColor();
	const ImVec4 guideColor = themedGuideColor();
	const ImVec4 analyzerFillColor = themedAccentColorWithAlpha(0.20f);
	const ImVec4 analyzerLineColor = themedCheckColor(0.60f);
	const ImVec4 analyzerPeakColor = themedAccentBrightColorWithAlpha(0.86f);
	const ImVec4 vuTrackColor = themedFrameColor();
	const ImVec4 vuFillColor = themedAccentColorWithAlpha(0.95f);
	const ImVec4 vuPeakColor = themedAccentBrightColorWithAlpha(0.95f);

	const auto normalizedSpectrumToDb = [&](float normalized) {
		return ofMap(ofClamp(normalized, 0.0f, 1.0f), 0.0f, 1.0f, visualizerMinDb, visualizerMaxDb, true);
	};

	drawList->AddRectFilled(graphMin, graphMax, ImGui::GetColorU32(panelBgColor));
	drawList->AddRect(graphMin, graphMax, ImGui::GetColorU32(borderColor), 0.0f, 0, 1.0f);

	for (int line = 1; line < 4; ++line) {
		const float t = static_cast<float>(line) / 4.0f;
		const float y = graphMin.y + graphHeight * t;
		drawList->AddLine(ImVec2(graphMin.x, y), ImVec2(graphMax.x, y), ImGui::GetColorU32(gridColor));
	}

	std::vector<ImVec2> spectrumPoints;
	spectrumPoints.reserve(spectrumLevels.size());
	for (int i = 0; i < static_cast<int>(spectrumLevels.size()); ++i) {
		const float xT = spectrumLevels.size() <= 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(spectrumLevels.size() - 1);
		const float level = ofClamp(spectrumLevels[static_cast<size_t>(i)], 0.0f, 1.0f);
		spectrumPoints.emplace_back(graphMin.x + graphWidth * xT, graphMax.y - graphHeight * level);
	}

	const float nowSeconds = static_cast<float>(ImGui::GetTime());
	float deltaSeconds = 1.0f / 60.0f;
	if (lastUpdateTime > 0.0) {
		deltaSeconds = ofClamp(static_cast<float>(nowSeconds - lastUpdateTime), 1.0f / 240.0f, 0.25f);
	}
	lastUpdateTime = nowSeconds;

	if (peakHoldLevels.size() != spectrumLevels.size() || peakHoldTimers.size() != spectrumLevels.size()) {
		peakHoldLevels = spectrumLevels;
		peakHoldTimers.assign(spectrumLevels.size(), kAnalyzerPeakHoldSeconds);
	} else {
		for (size_t i = 0; i < spectrumLevels.size(); ++i) {
			if (spectrumLevels[i] >= peakHoldLevels[i]) {
				peakHoldLevels[i] = spectrumLevels[i];
				peakHoldTimers[i] = kAnalyzerPeakHoldSeconds;
			} else if (peakHoldTimers[i] > 0.0f) {
				peakHoldTimers[i] = std::max(0.0f, peakHoldTimers[i] - deltaSeconds);
			} else {
				peakHoldLevels[i] = std::max(spectrumLevels[i], peakHoldLevels[i] - (kAnalyzerPeakReleasePerSecond * deltaSeconds));
			}
		}
	}

	std::vector<ImVec2> peakHoldPoints;
	peakHoldPoints.reserve(peakHoldLevels.size());
	for (int i = 0; i < static_cast<int>(peakHoldLevels.size()); ++i) {
		const float xT = peakHoldLevels.size() <= 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(peakHoldLevels.size() - 1);
		const float level = ofClamp(peakHoldLevels[static_cast<size_t>(i)], 0.0f, 1.0f);
		peakHoldPoints.emplace_back(graphMin.x + graphWidth * xT, graphMax.y - graphHeight * level);
	}

	const auto drawSpectrumLine = [&]() {
		if (spectrumPoints.size() < 2) return;
		drawList->AddPolyline(spectrumPoints.data(), static_cast<int>(spectrumPoints.size()), ImGui::GetColorU32(analyzerLineColor), ImDrawFlags_None, 1.5f);
	};
	const auto drawSpectrumFill = [&]() {
		for (size_t i = 1; i < spectrumPoints.size(); ++i) {
			const ImVec2 & left = spectrumPoints[i - 1];
			const ImVec2 & right = spectrumPoints[i];
			drawList->AddQuadFilled(ImVec2(left.x, graphMax.y), left, right, ImVec2(right.x, graphMax.y), ImGui::GetColorU32(analyzerFillColor));
		}
	};
	const auto drawPeakHoldLine = [&]() {
		if (peakHoldPoints.size() < 2) return;
		drawList->AddPolyline(peakHoldPoints.data(), static_cast<int>(peakHoldPoints.size()), ImGui::GetColorU32(analyzerPeakColor), ImDrawFlags_None, 1.0f);
	};
	const auto drawSpectrumBars = [&](bool drawBarPeakHold) {
		const int barCount = std::min(kAnalyzerBarCount, static_cast<int>(spectrumLevels.size()));
		if (barCount <= 0) return;
		const float step = static_cast<float>(spectrumLevels.size()) / static_cast<float>(barCount);
		const float gap = 1.0f;
		for (int barIndex = 0; barIndex < barCount; ++barIndex) {
			const int start = static_cast<int>(std::floor(step * static_cast<float>(barIndex)));
			const int end = std::min(static_cast<int>(spectrumLevels.size()), static_cast<int>(std::floor(step * static_cast<float>(barIndex + 1))));
			if (end <= start) continue;
			float peakLevel = 0.0f;
			for (int sampleIndex = start; sampleIndex < end; ++sampleIndex) {
				peakLevel = std::max(peakLevel, spectrumLevels[static_cast<size_t>(sampleIndex)]);
			}
			const float x0 = graphMin.x + (graphWidth * static_cast<float>(barIndex) / static_cast<float>(barCount));
			const float x1 = graphMin.x + (graphWidth * static_cast<float>(barIndex + 1) / static_cast<float>(barCount));
			const float topY = graphMax.y - (graphHeight * ofClamp(peakLevel, 0.0f, 1.0f));
			drawList->AddRectFilled(ImVec2(x0 + gap, topY), ImVec2(std::max(x0 + gap, x1 - gap), graphMax.y), ImGui::GetColorU32(analyzerFillColor));
			if (drawBarPeakHold && barIndex < static_cast<int>(peakHoldLevels.size())) {
				const int peakSample = std::min(static_cast<int>(peakHoldLevels.size()) - 1,
					static_cast<int>(std::round((static_cast<float>(barIndex) / static_cast<float>(std::max(barCount - 1, 1))) * static_cast<float>(peakHoldLevels.size() - 1))));
				const float heldPeakLevel = ofClamp(peakHoldLevels[static_cast<size_t>(peakSample)], 0.0f, 1.0f);
				const float peakY = graphMax.y - (graphHeight * heldPeakLevel);
				drawList->AddLine(ImVec2(x0 + gap, peakY), ImVec2(std::max(x0 + gap, x1 - gap), peakY), ImGui::GetColorU32(analyzerPeakColor), 1.0f);
			}
		}
	};
	const auto drawWaveformLine = [&](const std::vector<float> & samples, float thickness) {
		if (samples.size() < 2) return;
		std::vector<ImVec2> waveformPoints;
		waveformPoints.reserve(samples.size());
		for (size_t sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
			const float xT = samples.size() <= 1 ? 0.5f : static_cast<float>(sampleIndex) / static_cast<float>(samples.size() - 1);
			const float sample = ofClamp(samples[sampleIndex] * kWaveformGain, -1.0f, 1.0f);
			const float yT = 0.5f - (sample * 0.42f);
			waveformPoints.emplace_back(graphMin.x + graphWidth * xT, graphMin.y + graphHeight * yT);
		}
		drawList->AddPolyline(waveformPoints.data(), static_cast<int>(waveformPoints.size()), ImGui::GetColorU32(analyzerLineColor), ImDrawFlags_None, thickness);
	};
	const auto drawVectorscope = [&]() {
		if (interleavedAudioFrames.empty() || audioChannelCount < 2) {
			drawWaveformLine(buildTriggeredScopeSamples(monoAudioFrames, 256), 1.6f);
			return;
		}
		const size_t frameCount = interleavedAudioFrames.size() / static_cast<size_t>(audioChannelCount);
		if (frameCount < 2) return;
		float meanLeft = 0.0f;
		float meanRight = 0.0f;
		for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			meanLeft += interleavedAudioFrames[(frameIndex * static_cast<size_t>(audioChannelCount))];
			meanRight += interleavedAudioFrames[(frameIndex * static_cast<size_t>(audioChannelCount)) + 1u];
		}
		meanLeft /= static_cast<float>(frameCount);
		meanRight /= static_cast<float>(frameCount);
		float peakSide = 1.0e-4f;
		float peakMid = 1.0e-4f;
		for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			const float left = interleavedAudioFrames[(frameIndex * static_cast<size_t>(audioChannelCount))] - meanLeft;
			const float right = interleavedAudioFrames[(frameIndex * static_cast<size_t>(audioChannelCount)) + 1u] - meanRight;
			peakSide = std::max(peakSide, std::abs((left - right) * 0.70710678f * kVectorscopeGain));
			peakMid = std::max(peakMid, std::abs((left + right) * 0.70710678f * kVectorscopeGain));
		}
		const size_t maxPointCount = 768;
		const size_t pointStride = std::max<size_t>(1, frameCount / maxPointCount);
		std::vector<ImVec2> points;
		points.reserve((frameCount / pointStride) + 1);
		const float actualScopeSide = std::min(graphHeight, graphWidth);
		const float scopeOffsetX = std::max(0.0f, (graphWidth - actualScopeSide) * 0.5f);
		const ImVec2 scopeMin(graphMin.x + scopeOffsetX, graphMin.y);
		const ImVec2 scopeMax(scopeMin.x + actualScopeSide, scopeMin.y + actualScopeSide);
		const float scopePadding = 3.0f;
		const float centerX = scopeMin.x + actualScopeSide * 0.5f;
		const float centerY = scopeMin.y + actualScopeSide * 0.5f;
		const float radiusX = std::max(1.0f, (actualScopeSide * 0.5f) - scopePadding);
		const float radiusY = std::max(1.0f, (actualScopeSide * 0.5f) - scopePadding);
		drawList->AddRectFilled(scopeMin, scopeMax, ImGui::GetColorU32(panelBgColor));
		drawList->AddRect(scopeMin, scopeMax, ImGui::GetColorU32(borderColor), 0.0f, 0, 1.0f);
		drawList->AddLine(ImVec2(centerX, scopeMin.y + scopePadding), ImVec2(centerX, scopeMax.y - scopePadding), ImGui::GetColorU32(guideColor));
		drawList->AddLine(ImVec2(scopeMin.x + scopePadding, centerY), ImVec2(scopeMax.x - scopePadding, centerY), ImGui::GetColorU32(guideColor));
		for (size_t frameIndex = 0; frameIndex < frameCount; frameIndex += pointStride) {
			const float left = interleavedAudioFrames[(frameIndex * static_cast<size_t>(audioChannelCount))] - meanLeft;
			const float right = interleavedAudioFrames[(frameIndex * static_cast<size_t>(audioChannelCount)) + 1u] - meanRight;
			const float side = ofClamp(((left - right) * 0.70710678f * kVectorscopeGain) / peakSide, -1.0f, 1.0f);
			const float mid = ofClamp(((left + right) * 0.70710678f * kVectorscopeGain) / peakMid, -1.0f, 1.0f);
			points.emplace_back(centerX + side * radiusX, centerY - mid * radiusY);
		}
		if (points.size() >= 2) {
			drawList->AddPolyline(points.data(), static_cast<int>(points.size()), ImGui::GetColorU32(analyzerLineColor), ImDrawFlags_None, 1.1f);
			for (const ImVec2 & point : points) {
				drawList->AddCircleFilled(point, 1.1f, ImGui::GetColorU32(analyzerPeakColor));
			}
		}
	};

	const auto drawVuMeters = [&]() {
		if (interleavedAudioFrames.empty()) return;
		std::array<float, 2> energy = { 0.0f, 0.0f };
		std::array<float, 2> peak = { 0.0f, 0.0f };
		const size_t frameCount = interleavedAudioFrames.size() / static_cast<size_t>(audioChannelCount);
		for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			for (int channelIndex = 0; channelIndex < std::min(audioChannelCount, 2); ++channelIndex) {
				const float sample = std::abs(interleavedAudioFrames[(frameIndex * static_cast<size_t>(audioChannelCount)) + static_cast<size_t>(channelIndex)]);
				energy[static_cast<size_t>(channelIndex)] += sample * sample;
				peak[static_cast<size_t>(channelIndex)] = std::max(peak[static_cast<size_t>(channelIndex)], sample);
			}
		}
		if (audioChannelCount == 1) {
			energy[1] = energy[0];
			peak[1] = peak[0];
		}
		const float sampleFrameCount = std::max<size_t>(1, frameCount);
		const auto amplitudeToDb = [&](float amplitude) {
			return 20.0f * std::log10(std::max(1.0e-6f, amplitude * kVuGain));
		};
		const auto dbToNormalized = [&](float dbValue) {
			return ofClamp(ofMap(dbValue, visualizerMinDb, visualizerMaxDb, 0.0f, 1.0f, true), 0.0f, 1.0f);
		};
		const std::array<float, 2> rmsLevels = {
			dbToNormalized(amplitudeToDb(std::sqrt(energy[0] / sampleFrameCount))),
			dbToNormalized(amplitudeToDb(std::sqrt(energy[1] / sampleFrameCount)))
		};
		const std::array<float, 2> peakLevels = {
			dbToNormalized(amplitudeToDb(peak[0])),
			dbToNormalized(amplitudeToDb(peak[1]))
		};
		const std::array<float, 2> rmsDbLevels = {
			amplitudeToDb(std::sqrt(energy[0] / sampleFrameCount)),
			amplitudeToDb(std::sqrt(energy[1] / sampleFrameCount))
		};
		const std::array<float, 2> peakDbLevels = { amplitudeToDb(peak[0]), amplitudeToDb(peak[1]) };
		const float releasePerFrame = kVuReleasePerSecond * deltaSeconds;
		for (int channelIndex = 0; channelIndex < 2; ++channelIndex) {
			const size_t index = static_cast<size_t>(channelIndex);
			vuMeterDisplayedLevels[index] = std::max(rmsLevels[index], vuMeterDisplayedLevels[index] - releasePerFrame);
			if (peakLevels[index] >= vuMeterDisplayedPeaks[index]) {
				vuMeterDisplayedPeaks[index] = peakLevels[index];
				vuMeterPeakTimers[index] = kVuPeakHoldSeconds;
			} else if (vuMeterPeakTimers[index] > 0.0f) {
				vuMeterPeakTimers[index] = std::max(0.0f, vuMeterPeakTimers[index] - deltaSeconds);
			} else {
				vuMeterDisplayedPeaks[index] = std::max(peakLevels[index], vuMeterDisplayedPeaks[index] - releasePerFrame);
			}
		}
		const float meterTop = meterMin.y + kVuPanelInset;
		const float meterBottom = meterMax.y - kVuPanelInset;
		const float meterHeight = std::max(1.0f, meterBottom - meterTop);
		const float totalMeterWidth = (kVuMeterWidth * 2.0f) + kVuChannelGap;
		const float meterStartX = meterMin.x + ((meterMax.x - meterMin.x) - totalMeterWidth) * 0.5f;
		drawList->AddRectFilled(meterMin, meterMax, ImGui::GetColorU32(panelBgColor));
		drawList->AddRect(meterMin, meterMax, ImGui::GetColorU32(borderColor), 0.0f, 0, 1.0f);
		for (int channelIndex = 0; channelIndex < 2; ++channelIndex) {
			const float meterX0 = meterStartX + channelIndex * (kVuMeterWidth + kVuChannelGap);
			const float meterX1 = meterX0 + kVuMeterWidth;
			const float levelTopY = meterBottom - (meterHeight * vuMeterDisplayedLevels[static_cast<size_t>(channelIndex)]);
			drawList->AddRectFilled(ImVec2(meterX0, meterTop), ImVec2(meterX1, meterBottom), ImGui::GetColorU32(vuTrackColor));
			drawList->AddRectFilled(ImVec2(meterX0, levelTopY), ImVec2(meterX1, meterBottom), ImGui::GetColorU32(vuFillColor));
			const float peakY = meterBottom - (meterHeight * vuMeterDisplayedPeaks[static_cast<size_t>(channelIndex)]);
			drawList->AddLine(ImVec2(meterX0, peakY), ImVec2(meterX1, peakY), ImGui::GetColorU32(vuPeakColor), 2.0f);
		}
		const float dividerX = meterStartX + kVuMeterWidth;
		drawList->AddLine(ImVec2(dividerX, meterMin.y + 1.0f), ImVec2(dividerX, meterMax.y - 1.0f), ImGui::GetColorU32(borderColor), 1.0f);
		if (mousePos.x >= meterMin.x && mousePos.x <= meterMax.x && mousePos.y >= meterMin.y && mousePos.y <= meterMax.y && ImGui::BeginTooltip()) {
			ImGui::Text("L RMS: %.1f dBFS", rmsDbLevels[0]);
			ImGui::Text("L Peak: %.1f dBFS", peakDbLevels[0]);
			ImGui::Separator();
			ImGui::Text("R RMS: %.1f dBFS", rmsDbLevels[1]);
			ImGui::Text("R Peak: %.1f dBFS", peakDbLevels[1]);
			ImGui::EndTooltip();
		}
	};

	drawVuMeters();
	switch (displayStyle) {
	case DisplayStyle::Waveform:
		drawWaveformLine(monoAudioFrames, 1.6f);
		break;
	case DisplayStyle::Vectorscope:
		drawVectorscope();
		break;
	case DisplayStyle::Mastering:
		drawSpectrumLine();
		drawPeakHoldLine();
		break;
	case DisplayStyle::RtaBars:
		drawSpectrumBars(true);
		break;
	case DisplayStyle::Hybrid:
		drawSpectrumBars(false);
		drawSpectrumLine();
		drawPeakHoldLine();
		break;
	case DisplayStyle::Studio:
	default:
		drawSpectrumFill();
		drawSpectrumLine();
		break;
	}

	if (graphHovered) {
		switch (displayStyle) {
		case DisplayStyle::Waveform: {
			if (!monoAudioFrames.empty()) {
				const float xT = ofClamp((mousePos.x - graphMin.x) / graphWidth, 0.0f, 1.0f);
				const size_t sampleIndex = std::min(static_cast<size_t>(std::round(xT * static_cast<float>(monoAudioFrames.size() - 1))), monoAudioFrames.size() - 1);
				const float amplitude = ofClamp(monoAudioFrames[sampleIndex] * kWaveformGain, -1.0f, 1.0f);
				const float timeMs = sampleRate > 0 ? (1000.0f * static_cast<float>(sampleIndex) / static_cast<float>(sampleRate)) : 0.0f;
				if (ImGui::BeginTooltip()) {
					ImGui::TextUnformatted(formatMilliseconds(timeMs).c_str());
					ImGui::Text("Amp: %.2f", amplitude);
					ImGui::EndTooltip();
				}
			}
			break;
		}
		case DisplayStyle::Vectorscope: {
			const float actualScopeSide = std::min(graphHeight, graphWidth);
			const float scopeOffsetX = std::max(0.0f, (graphWidth - actualScopeSide) * 0.5f);
			const ImVec2 scopeMin(graphMin.x + scopeOffsetX, graphMin.y);
			const ImVec2 scopeMax(scopeMin.x + actualScopeSide, scopeMin.y + actualScopeSide);
			if (mousePos.x >= scopeMin.x && mousePos.x <= scopeMax.x && mousePos.y >= scopeMin.y && mousePos.y <= scopeMax.y) {
				const float centerX = scopeMin.x + actualScopeSide * 0.5f;
				const float centerY = scopeMin.y + actualScopeSide * 0.5f;
				const float radius = std::max(1.0f, (actualScopeSide * 0.5f) - 3.0f);
				const float side = ofClamp((mousePos.x - centerX) / radius, -1.0f, 1.0f);
				const float mid = ofClamp((centerY - mousePos.y) / radius, -1.0f, 1.0f);
				if (ImGui::BeginTooltip()) {
					ImGui::Text("Side: %.2f", side);
					ImGui::Text("Mid: %.2f", mid);
					ImGui::Separator();
					ImGui::TextDisabled("x = stereo width");
					ImGui::TextDisabled("y = mono energy");
					ImGui::EndTooltip();
				}
			}
			break;
		}
		case DisplayStyle::Studio:
		case DisplayStyle::Mastering:
		case DisplayStyle::RtaBars:
		case DisplayStyle::Hybrid:
		default: {
			if (!spectrumLevels.empty()) {
				const float xT = ofClamp((mousePos.x - graphMin.x) / graphWidth, 0.0f, 1.0f);
				const size_t sampleIndex = std::min(static_cast<size_t>(std::round(xT * static_cast<float>(spectrumLevels.size() - 1))), spectrumLevels.size() - 1);
				const int eqBandCount = player.getEqualizerBandCount();
				const float nyquist = std::max(1.0f, static_cast<float>(sampleRate) * 0.5f);
				const float minSpectrumFrequency = std::max(eqBandCount > 0 ? player.getEqualizerBandFrequency(0) : 20.0f, 20.0f);
				const float maxSpectrumFrequency = std::min(nyquist, std::max(eqBandCount > 0 ? player.getEqualizerBandFrequency(eqBandCount - 1) : (minSpectrumFrequency * 2.0f), minSpectrumFrequency * 2.0f));
				const float frequencyHz = minSpectrumFrequency * std::pow(maxSpectrumFrequency / minSpectrumFrequency, xT);
				const float levelDb = normalizedSpectrumToDb(spectrumLevels[sampleIndex]);
				if (ImGui::BeginTooltip()) {
					ImGui::TextUnformatted(formatFrequency(frequencyHz).c_str());
					ImGui::Text("Level: %.1f dBFS", levelDb);
					if (sampleIndex < peakHoldLevels.size()) {
						ImGui::Text("Peak: %.1f dBFS", normalizedSpectrumToDb(peakHoldLevels[sampleIndex]));
					}
					ImGui::EndTooltip();
				}
			}
			break;
		}
		}
	}

	if (callbackPerformance.available) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text(
			"Callback %.1f Hz  |  %.0f frames/cb  |  avg %s  |  max %s",
			static_cast<float>(callbackPerformance.callbackRateHz),
			static_cast<float>(callbackPerformance.averageFramesPerCallback),
			formatMicroseconds(callbackPerformance.averageCallbackMicros).c_str(),
			formatMicroseconds(static_cast<double>(callbackPerformance.maxCallbackMicros)).c_str());
		ImGui::Text(
			"Convert %s / %s  |  Ring %s / %s",
			formatMicroseconds(callbackPerformance.averageConversionMicros).c_str(),
			formatMicroseconds(static_cast<double>(callbackPerformance.maxConversionMicros)).c_str(),
			formatMicroseconds(callbackPerformance.averageRingWriteMicros).c_str(),
			formatMicroseconds(static_cast<double>(callbackPerformance.maxRingWriteMicros)).c_str());
		ImGui::Text(
			"Recorder %s / %s  |  Other avg %s",
			formatMicroseconds(callbackPerformance.averageRecorderMicros).c_str(),
			formatMicroseconds(static_cast<double>(callbackPerformance.maxRecorderMicros)).c_str(),
			formatMicroseconds(callbackPerformance.averageOtherMicros).c_str());
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}
