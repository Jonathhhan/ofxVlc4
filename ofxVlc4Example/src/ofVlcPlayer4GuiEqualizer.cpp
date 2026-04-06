#include "ofVlcPlayer4GuiEqualizer.h"
#include "ofVlcPlayer4GuiControls.h"
#include "ofVlcPlayer4GuiStyle.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace {
constexpr size_t kEqualizerDrawSampleCount = 512;
constexpr float kEqualizerAmpWriteEpsilon = 0.01f;
constexpr float kEqualizerGraphVerticalInset = 12.0f;
using ofVlcPlayer4GuiStyle::kExtraWideSliderWidth;
using ofVlcPlayer4GuiStyle::themedAccentBrightColor;
using ofVlcPlayer4GuiStyle::themedAccentColor;
using ofVlcPlayer4GuiStyle::themedAccentColorWithAlpha;
using ofVlcPlayer4GuiStyle::themedBorderColor;
using ofVlcPlayer4GuiStyle::themedCheckColor;
using ofVlcPlayer4GuiStyle::themedChildBgColor;
using ofVlcPlayer4GuiStyle::themedFrameColor;
using ofVlcPlayer4GuiStyle::themedGuideColor;
using ofVlcPlayer4GuiStyle::themedWindowBgColor;
constexpr std::array<float, 3> kEqualizerDbLabelMarkers = { 20.0f, 0.0f, -20.0f };
constexpr float kEqualizerBandMinDb = -20.0f;
constexpr float kEqualizerBandMaxDb = 20.0f;

int findNearestPointIndex(const std::vector<ImVec2> & points, const ImVec2 & target) {
	int nearestIndex = -1;
	float nearestDistance = std::numeric_limits<float>::max();
	for (int pointIndex = 0; pointIndex < static_cast<int>(points.size()); ++pointIndex) {
		const float dx = target.x - points[pointIndex].x;
		const float dy = target.y - points[pointIndex].y;
		const float distance = std::sqrt((dx * dx) + (dy * dy));
		if (distance < nearestDistance) {
			nearestDistance = distance;
			nearestIndex = pointIndex;
		}
	}

	return nearestIndex;
}

int findNearestPointIndexByX(const std::vector<ImVec2> & points, float targetX) {
	int nearestIndex = -1;
	float nearestDistance = std::numeric_limits<float>::max();
	for (int pointIndex = 0; pointIndex < static_cast<int>(points.size()); ++pointIndex) {
		const float distance = std::abs(targetX - points[pointIndex].x);
		if (distance < nearestDistance) {
			nearestDistance = distance;
			nearestIndex = pointIndex;
		}
	}

	return nearestIndex;
}

std::vector<ImVec2> buildAnchoredPolyline(const std::vector<ImVec2> & points, float minX, float maxX) {
	std::vector<ImVec2> linePoints;
	linePoints.reserve(points.size() + 2);
	if (points.empty()) {
		return linePoints;
	}

	if (std::abs(points.front().x - minX) > 1.0f) {
		linePoints.emplace_back(minX, points.front().y);
	}
	linePoints.insert(linePoints.end(), points.begin(), points.end());
	if (std::abs(points.back().x - maxX) > 1.0f) {
		linePoints.emplace_back(maxX, points.back().y);
	}

	if (linePoints.size() < 2) {
		return points;
	}

	return linePoints;
}

std::vector<float> buildEqualizerDisplayCurve(
	const std::vector<float> & bandFrequencies,
	const std::vector<float> & bandAmps,
	size_t sampleCount,
	float minFrequency,
	float maxFrequency) {
	std::vector<float> curve(sampleCount, 0.0f);
	if (sampleCount == 0 || bandFrequencies.empty() || bandFrequencies.size() != bandAmps.size()) {
		return curve;
	}

	std::vector<float> logFrequencies(bandFrequencies.size(), 0.0f);
	for (size_t i = 0; i < bandFrequencies.size(); ++i) {
		logFrequencies[i] = std::log(std::max(bandFrequencies[i], 1.0f));
	}

	const float logMinFrequency = std::log(std::max(minFrequency, 1.0f));
	const float minLogMaxFrequency = minFrequency + 1.0f;
	const float logMaxFrequency = std::log(std::max(maxFrequency, minLogMaxFrequency));
	const size_t lastBandIndex = bandFrequencies.size() - 1;
	for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
		const float t = sampleCount <= 1
			? 0.0f
			: static_cast<float>(sampleIndex) / static_cast<float>(sampleCount - 1);
		const float logTarget = ofLerp(logMinFrequency, logMaxFrequency, t);

		float weightedGain = 0.0f;
		float totalWeight = 0.0f;
		for (size_t bandIndex = 0; bandIndex < bandFrequencies.size(); ++bandIndex) {
			const float leftSpan = bandIndex > 0
				? (logFrequencies[bandIndex] - logFrequencies[bandIndex - 1])
				: ((bandFrequencies.size() > 1) ? (logFrequencies[1] - logFrequencies[0]) : 0.45f);
			const float rightSpan = bandIndex < lastBandIndex
				? (logFrequencies[bandIndex + 1] - logFrequencies[bandIndex])
				: ((bandFrequencies.size() > 1) ? (logFrequencies[lastBandIndex] - logFrequencies[lastBandIndex - 1]) : 0.45f);
			const float sigma = std::max(0.18f, 0.85f * 0.5f * (leftSpan + rightSpan));

			float distance = std::abs(logTarget - logFrequencies[bandIndex]);
			if (bandIndex == 0 && logTarget < logFrequencies[bandIndex]) {
				distance *= 0.38f;
			} else if (bandIndex == lastBandIndex && logTarget > logFrequencies[bandIndex]) {
				distance *= 0.38f;
			}

			const float normalized = distance / sigma;
			const float weight = std::exp(-0.5f * normalized * normalized);
			weightedGain += bandAmps[bandIndex] * weight;
			totalWeight += weight;
		}

		curve[sampleIndex] = totalWeight > 0.0f ? (weightedGain / totalWeight) : 0.0f;
	}

	return curve;
}

float sampleEqualizerCurveAtXT(const std::vector<float> & curve, float xT) {
	if (curve.empty()) {
		return 0.0f;
	}
	if (curve.size() == 1) {
		return curve.front();
	}

	const float clampedXT = std::clamp(xT, 0.0f, 1.0f);
	const float samplePosition = clampedXT * static_cast<float>(curve.size() - 1);
	const size_t lowerIndex = static_cast<size_t>(std::floor(samplePosition));
	const size_t upperIndex = std::min(lowerIndex + 1, curve.size() - 1);
	const float blend = samplePosition - static_cast<float>(lowerIndex);
	return ofLerp(curve[lowerIndex], curve[upperIndex], blend);
}

std::string formatEqualizerFrequency(float frequencyHz) {
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

std::string defaultEqualizerPresetFileName(const ofxVlc4 & player) {
	std::string presetName = "custom";
	const int currentPresetIndex = player.getCurrentEqualizerPresetIndex();
	const std::vector<std::string> presetNames = player.getEqualizerPresetNames();
	if (currentPresetIndex >= 0 && currentPresetIndex < static_cast<int>(presetNames.size())) {
		presetName = presetNames[static_cast<size_t>(currentPresetIndex)];
	} else {
		const int matchedPresetIndex = player.findMatchingEqualizerPresetIndex();
		if (matchedPresetIndex >= 0 && matchedPresetIndex < static_cast<int>(presetNames.size())) {
			presetName = presetNames[static_cast<size_t>(matchedPresetIndex)];
		}
	}

	std::string sanitizedName;
	sanitizedName.reserve(presetName.size());
	for (unsigned char c : presetName) {
		if (std::isalnum(c)) {
			sanitizedName.push_back(static_cast<char>(std::tolower(c)));
		} else if (!sanitizedName.empty() && sanitizedName.back() != '-') {
			sanitizedName.push_back('-');
		}
	}
	while (!sanitizedName.empty() && sanitizedName.back() == '-') {
		sanitizedName.pop_back();
	}
	if (sanitizedName.empty()) {
		sanitizedName = "custom";
	}

	return "equalizer-" + sanitizedName + ".txt";
}
}

void ofVlcPlayer4GuiEqualizer::drawContent(
	ofxVlc4 & player,
	float actionButtonWidth) {
	const int bandCount = player.getEqualizerBandCount();
	if (bandCount <= 0) {
		return;
	}

	std::vector<float> bandFrequencies;
	std::vector<float> bandAmps;
	bandFrequencies.reserve(static_cast<size_t>(bandCount));
	bandAmps.reserve(static_cast<size_t>(bandCount));
	for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
		bandFrequencies.push_back(player.getEqualizerBandFrequency(bandIndex));
		bandAmps.push_back(player.getEqualizerBandAmp(bandIndex));
	}

	if (bandFrequencies.empty()) {
		return;
	}

	bool equalizerEnabled = player.isEqualizerEnabled();
	if (ImGui::Checkbox("Enable", &equalizerEnabled)) {
		player.setEqualizerEnabled(equalizerEnabled);
		if (!equalizerEnabled) {
			activeGuidePoint = -1;
		}
	}

	ImGui::BeginDisabled(!equalizerEnabled);

	const std::vector<std::string> presetNames = player.getEqualizerPresetNames();
	const int currentPresetIndex = player.getCurrentEqualizerPresetIndex();
	const char * presetPreview =
		(currentPresetIndex >= 0 && currentPresetIndex < static_cast<int>(presetNames.size()))
			? presetNames[static_cast<size_t>(currentPresetIndex)].c_str()
			: "Custom";
	ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(kExtraWideSliderWidth));
	if (ImGui::BeginCombo("Preset", presetPreview)) {
		for (int presetIndex = 0; presetIndex < static_cast<int>(presetNames.size()); ++presetIndex) {
			const bool selected = (presetIndex == currentPresetIndex);
			if (ImGui::Selectable(presetNames[static_cast<size_t>(presetIndex)].c_str(), selected)) {
				player.applyEqualizerPreset(presetIndex);
				editingPresetIndex = presetIndex;
				serializedPresetBuffer = player.exportCurrentEqualizerPreset();
				activeGuidePoint = -1;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	const int matchedPresetIndex = player.findMatchingEqualizerPresetIndex();
	if (currentPresetIndex >= 0 && currentPresetIndex < static_cast<int>(presetNames.size())) {
		editingPresetIndex = currentPresetIndex;
		const ofxVlc4::EqualizerPresetInfo presetInfo = player.getEqualizerPresetInfo(currentPresetIndex);
		ImGui::TextDisabled(
			"Preset meta: %.1f dB preamp   %d bands",
			presetInfo.preamp,
			static_cast<int>(presetInfo.bandAmps.size()));
	} else if (editingPresetIndex >= 0 && editingPresetIndex < static_cast<int>(presetNames.size())) {
		ImGui::TextDisabled(
			"Editing from: %s",
			presetNames[static_cast<size_t>(editingPresetIndex)].c_str());
	} else if (matchedPresetIndex >= 0 && matchedPresetIndex < static_cast<int>(presetNames.size())) {
		editingPresetIndex = matchedPresetIndex;
		ImGui::TextDisabled(
			"Preset match: %s",
			presetNames[static_cast<size_t>(matchedPresetIndex)].c_str());
	} else {
		editingPresetIndex = -1;
		ImGui::TextDisabled("Preset match: Custom");
	}

	float preamp = player.getEqualizerPreamp();
	ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(kExtraWideSliderWidth));
	if (ImGui::SliderFloat("Preamp", &preamp, -20.0f, 20.0f, "%.1f dB")) {
		player.setEqualizerPreamp(preamp);
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(preamp, -20.0f, 20.0f, 0.5f)) {
		player.setEqualizerPreamp(preamp);
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
	}

	if (serializedPresetBuffer.empty()) {
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
	}

	if (ImGui::Button("Copy Preset", ImVec2(actionButtonWidth, 0.0f))) {
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
		ImGui::SetClipboardText(serializedPresetBuffer.c_str());
	}
	ImGui::SameLine();
	if (ImGui::Button("Edit Text", ImVec2(actionButtonWidth, 0.0f))) {
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
		ImGui::OpenPopup("EqualizerPresetText");
	}
	if (ImGui::Button("Save File", ImVec2(actionButtonWidth, 0.0f))) {
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
		const std::string defaultFileName =
			presetFilePathBuffer.empty() ? defaultEqualizerPresetFileName(player) : ofFilePath::getFileName(presetFilePathBuffer);
		const ofFileDialogResult saveResult = ofSystemSaveDialog(defaultFileName, "Save Equalizer Preset");
		if (saveResult.bSuccess) {
			std::ofstream saveStream(saveResult.filePath, std::ios::binary | std::ios::trunc);
			if (saveStream.is_open()) {
				saveStream.write(serializedPresetBuffer.data(), static_cast<std::streamsize>(serializedPresetBuffer.size()));
			}
			if (saveStream.good()) {
				presetFilePathBuffer = saveResult.filePath;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Load File", ImVec2(actionButtonWidth, 0.0f))) {
		const ofFileDialogResult loadResult = ofSystemLoadDialog("Load Equalizer Preset", false, presetFilePathBuffer);
		if (loadResult.bSuccess) {
			presetFilePathBuffer = loadResult.filePath;
			serializedPresetBuffer = ofBufferFromFile(loadResult.filePath).getText();
			if (player.importEqualizerPreset(serializedPresetBuffer)) {
				editingPresetIndex = player.getCurrentEqualizerPresetIndex();
				serializedPresetBuffer = player.exportCurrentEqualizerPreset();
			}
		}
	}

	if (ImGui::BeginPopupModal("EqualizerPresetText", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::PushItemWidth(420.0f);
		ImGui::InputTextMultiline("##PresetTextEditor", &serializedPresetBuffer, ImVec2(420.0f, 110.0f));
		ImGui::PopItemWidth();
		if (ImGui::Button("Apply Text", ImVec2(actionButtonWidth, 0.0f))) {
			if (player.importEqualizerPreset(serializedPresetBuffer)) {
				editingPresetIndex = player.getCurrentEqualizerPresetIndex();
				serializedPresetBuffer = player.exportCurrentEqualizerPreset();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Close", ImVec2(actionButtonWidth, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	const ImVec2 graphSize(ImGui::GetContentRegionAvail().x, 170.0f);
	const ImVec2 graphPos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##equalizerGraph", graphSize);

	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();

	ImDrawList * drawList = ImGui::GetWindowDrawList();
	const ImVec2 graphMin = graphPos;
	const ImVec2 graphMax(graphPos.x + graphSize.x, graphPos.y + graphSize.y);
	const float graphWidth = std::max(1.0f, graphSize.x);
	const float graphHeight = std::max(1.0f, graphSize.y);
	const float minDb = kEqualizerBandMinDb;
	const float maxDb = kEqualizerBandMaxDb;
	const float graphContentMinY = graphMin.y + kEqualizerGraphVerticalInset;
	const float graphContentMaxY = graphMax.y - kEqualizerGraphVerticalInset;
	const float graphContentHeight = std::max(1.0f, graphContentMaxY - graphContentMinY);
	const float minEqFrequency = std::max(bandFrequencies.front(), 20.0f);
	const float maxEqFrequency = std::max(bandFrequencies.back(), minEqFrequency * 2.0f);

	const auto frequencyToGraphXT = [&](float frequencyHz) {
		const float clampedFrequency = std::clamp(frequencyHz, minEqFrequency, maxEqFrequency);
		if (maxEqFrequency <= minEqFrequency) {
			return 0.5f;
		}

		return std::clamp(
			std::log(clampedFrequency / minEqFrequency) / std::log(maxEqFrequency / minEqFrequency),
			0.0f,
			1.0f);
	};

	const auto ampToGraphYT = [&](float amp) {
		const float normalized = std::clamp(1.0f - ((amp - minDb) / (maxDb - minDb)), 0.0f, 1.0f);
		return graphContentMinY + (graphContentHeight * normalized);
	};

	auto buildEqualizerGraphPoints = [&](const std::vector<float> & amps) {
		std::vector<ImVec2> graphPoints;
		graphPoints.reserve(bandFrequencies.size());
		for (size_t i = 0; i < bandFrequencies.size(); ++i) {
			graphPoints.emplace_back(
				graphMin.x + graphWidth * frequencyToGraphXT(bandFrequencies[i]),
				ampToGraphYT(amps[i]));
		}
		return graphPoints;
	};

	std::vector<ImVec2> points = buildEqualizerGraphPoints(bandAmps);

	const auto applyBandAmpAtMouse = [&](int bandIndex, float mouseY) {
		if (bandIndex < 0 || bandIndex >= static_cast<int>(bandAmps.size())) {
			return;
		}

		const float mouseYT = std::clamp((mouseY - graphContentMinY) / graphContentHeight, 0.0f, 1.0f);
		const float targetBandAmp = ofMap(mouseYT, 1.0f, 0.0f, minDb, maxDb, true);
		const float newAmp = ofClamp(targetBandAmp, kEqualizerBandMinDb, kEqualizerBandMaxDb);
		if (std::abs(bandAmps[static_cast<size_t>(bandIndex)] - newAmp) < kEqualizerAmpWriteEpsilon) {
			return;
		}

		bandAmps[static_cast<size_t>(bandIndex)] = newAmp;
		player.setEqualizerBandAmp(bandIndex, newAmp);
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
	};

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const int nearestGuidePoint = findNearestPointIndexByX(points, ImGui::GetIO().MousePos.x);
		if (nearestGuidePoint < 0 || nearestGuidePoint >= static_cast<int>(bandFrequencies.size())) {
			activeGuidePoint = -1;
		} else {
			activeGuidePoint = nearestGuidePoint;
			applyBandAmpAtMouse(nearestGuidePoint, ImGui::GetIO().MousePos.y);
			points = buildEqualizerGraphPoints(bandAmps);
		}
	}

	if (active && activeGuidePoint >= 0) {
		const float mouseY = std::clamp(ImGui::GetIO().MousePos.y, graphContentMinY, graphContentMaxY);
		const int nearestGuidePoint = findNearestPointIndexByX(points, ImGui::GetIO().MousePos.x);
		if (nearestGuidePoint >= 0 && nearestGuidePoint < static_cast<int>(bandFrequencies.size())) {
			activeGuidePoint = nearestGuidePoint;
			applyBandAmpAtMouse(nearestGuidePoint, mouseY);
			points = buildEqualizerGraphPoints(bandAmps);
		}
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		activeGuidePoint = -1;
	}

	std::vector<float> currentDisplayCurve = buildEqualizerDisplayCurve(
		bandFrequencies,
		bandAmps,
		kEqualizerDrawSampleCount,
		minEqFrequency,
		maxEqFrequency);
	const ImVec4 panelBgColor = themedChildBgColor();
	const ImVec4 borderColor = themedBorderColor();
	const ImVec4 gridColor = themedFrameColor();
	const ImVec4 guideColor = themedGuideColor();
	const ImVec4 curveColor = themedCheckColor(0.96f);
	const ImVec4 handleLineColor = themedAccentColorWithAlpha(0.42f);
	const ImVec4 handleColor = themedAccentColor();
	const ImVec4 handleHighlightColor = themedAccentBrightColor();
	const ImVec4 handleOutlineColor = themedWindowBgColor();

	drawList->AddRectFilled(graphMin, graphMax, ImGui::GetColorU32(panelBgColor));
	drawList->AddRect(graphMin, graphMax, ImGui::GetColorU32(borderColor), 0.0f, 0, 1.0f);

	for (int line = 1; line < 4; ++line) {
		const float y = graphContentMinY + graphContentHeight * (static_cast<float>(line) / 4.0f);
		drawList->AddLine(ImVec2(graphMin.x, y), ImVec2(graphMax.x, y), ImGui::GetColorU32(gridColor));
	}

	for (float guideFrequencyHz : bandFrequencies) {
		if (guideFrequencyHz <= minEqFrequency || guideFrequencyHz >= maxEqFrequency) {
			continue;
		}

		const float x = graphMin.x + (graphWidth * frequencyToGraphXT(guideFrequencyHz));
		drawList->AddLine(
			ImVec2(x, graphMin.y),
			ImVec2(x, graphMax.y),
			ImGui::GetColorU32(guideColor));
	}

	for (float dbMarker : kEqualizerDbLabelMarkers) {
		const float y = ampToGraphYT(dbMarker);
		char label[8];
		std::snprintf(label, sizeof(label), dbMarker > 0.0f ? "+%.0f" : "%.0f", dbMarker);
		drawList->AddText(
			ImVec2(graphMin.x + 8.0f, y - ImGui::GetFontSize() * 0.5f),
			ImGui::GetColorU32(borderColor),
			label);
	}

	std::vector<ImVec2> curvePoints;
	curvePoints.reserve(currentDisplayCurve.size());
	for (size_t sampleIndex = 0; sampleIndex < currentDisplayCurve.size(); ++sampleIndex) {
		const float t = currentDisplayCurve.size() <= 1
			? 0.0f
			: static_cast<float>(sampleIndex) / static_cast<float>(currentDisplayCurve.size() - 1);
		curvePoints.emplace_back(
			graphMin.x + (graphWidth * t),
			ampToGraphYT(currentDisplayCurve[sampleIndex]));
	}
	if (curvePoints.size() < 2) {
		curvePoints = points;
	}

	const std::vector<ImVec2> handleLinePoints = buildAnchoredPolyline(points, graphMin.x, graphMax.x);
	if (curvePoints.size() >= 2) {
		drawList->AddPolyline(
			curvePoints.data(),
			static_cast<int>(curvePoints.size()),
			ImGui::GetColorU32(curveColor),
			ImDrawFlags_None,
			2.0f);
	}
	if (handleLinePoints.size() >= 2) {
		drawList->AddPolyline(
			handleLinePoints.data(),
			static_cast<int>(handleLinePoints.size()),
			ImGui::GetColorU32(handleLineColor),
			ImDrawFlags_None,
			1.0f);
	}

	int hoveredBand = -1;
	float hoveredAmp = 0.0f;
	float hoveredFrequencyHz = minEqFrequency;
	if (hovered && !currentDisplayCurve.empty()) {
		const float mouseXT = std::clamp((ImGui::GetIO().MousePos.x - graphMin.x) / graphWidth, 0.0f, 1.0f);
		const float logFrequency = ofLerp(std::log(minEqFrequency), std::log(maxEqFrequency), mouseXT);
		hoveredFrequencyHz = std::exp(logFrequency);
		hoveredAmp = sampleEqualizerCurveAtXT(currentDisplayCurve, mouseXT);
		hoveredBand = findNearestPointIndex(points, ImGui::GetIO().MousePos);
	}

	const float baseHandleRadius = points.size() >= 24 ? 2.6f : (points.size() >= 16 ? 3.2f : 4.5f);
	for (int i = 0; i < static_cast<int>(points.size()); ++i) {
		const bool highlighted = (i == hoveredBand) || (i == activeGuidePoint);
		const float handleRadius = highlighted ? (baseHandleRadius + 1.0f) : baseHandleRadius;
		drawList->AddCircleFilled(points[static_cast<size_t>(i)], handleRadius, ImGui::GetColorU32(highlighted ? handleHighlightColor : handleColor));
		drawList->AddCircle(points[static_cast<size_t>(i)], handleRadius, ImGui::GetColorU32(handleOutlineColor), 0, 1.0f);
	}

	if (hovered && ImGui::BeginTooltip()) {
		ImGui::TextUnformatted(formatEqualizerFrequency(hoveredFrequencyHz).c_str());
		ImGui::Text("Curve: %.1f dB", hoveredAmp);
		if (hoveredBand >= 0 && hoveredBand < static_cast<int>(bandFrequencies.size())) {
			ImGui::Separator();
			const float bandAmp = bandAmps[static_cast<size_t>(hoveredBand)];
			const float totalBandAmp = bandAmp + preamp;
			ImGui::TextDisabled(
				"%s",
				formatEqualizerFrequency(bandFrequencies[static_cast<size_t>(hoveredBand)]).c_str());
			ImGui::Text("Band: %.1f dB", bandAmp);
			ImGui::Text("Total: %.1f dB", totalBandAmp);
		}
		ImGui::EndTooltip();
	}

	if (ImGui::Button("Reset", ImVec2(actionButtonWidth, 0.0f))) {
		player.resetEqualizer();
		editingPresetIndex = -1;
		serializedPresetBuffer = player.exportCurrentEqualizerPreset();
		activeGuidePoint = -1;
	}

	ImGui::EndDisabled();
}
