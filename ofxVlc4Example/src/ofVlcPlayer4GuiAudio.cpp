#include "ofVlcPlayer4GuiAudio.h"
#include "ofVlcPlayer4GuiControls.h"
#include "ofxVlc4.h"

#include <cstdlib>
#include <cstdio>
#include <vector>

namespace {
using MenuContentPolicy = ofVlcPlayer4GuiControls::MenuContentPolicy;

std::string formatAudioOutputModuleLabel(const ofxVlc4::AudioOutputModuleInfo & module) {
	if (!module.description.empty()) {
		return module.description;
	}
	if (!module.name.empty()) {
		return module.name;
	}
	return "Output";
}

std::string formatAudioOutputDeviceLabel(const ofxVlc4::AudioOutputDeviceInfo & device) {
	if (!device.description.empty()) {
		return device.description;
	}
	if (!device.id.empty()) {
		return device.id;
	}
	return "Device";
}

std::string formatAudioFilterLabel(const ofxVlc4::AudioFilterInfo & filter) {
	if (!filter.shortName.empty()) {
		return filter.shortName;
	}
	if (!filter.description.empty()) {
		return filter.description;
	}
	if (!filter.name.empty()) {
		return filter.name;
	}
	return "Filter";
}

void drawAudioFilterTooltip(const ofxVlc4::AudioFilterInfo & filter) {
	if (!ImGui::IsItemHovered()) {
		return;
	}

	if (!ImGui::BeginTooltip()) {
		return;
	}

	if (!filter.name.empty()) {
		ImGui::Text("Name: %s", filter.name.c_str());
	}
	if (!filter.description.empty()) {
		ImGui::TextWrapped("%s", filter.description.c_str());
	}
	if (!filter.help.empty()) {
		ImGui::Separator();
		ImGui::TextWrapped("%s", filter.help.c_str());
	}
	ImGui::EndTooltip();
}

void replayAudioWindow(ofxVlc4 & player, int rewindMs) {
	const int currentTimeMs = player.getTime();
	player.setTime(std::max(0, currentTimeMs - rewindMs));
}
}

void ofVlcPlayer4GuiAudio::drawContent(
	ofxVlc4 & player,
	const ImVec2 & labelInnerSpacing,
	float compactControlWidth,
	float wideSliderWidth) {
	static const char * mixModes[] = { "Auto", "Stereo", "Binaural", "4.0", "5.1", "7.1" };
	static const char * stereoModes[] = { "Auto", "Stereo", "Reverse", "Left", "Right", "Dolby", "Mono" };
	static const char * callbackFormats[] = { "Float32", "S16", "S32" };
	static const char * callbackRates[] = { "22050", "32000", "44100", "48000", "88200", "96000", "192000" };
	static const char * callbackChannels[] = { "Mono", "Stereo" };
	const ofxVlc4::AudioStateInfo audioState = player.getAudioStateInfo();

	int mixModeIndex = 0;
	switch (audioState.mixMode) {
	case ofxVlc4::AudioMixMode::Stereo:
		mixModeIndex = 1;
		break;
	case ofxVlc4::AudioMixMode::Binaural:
		mixModeIndex = 2;
		break;
	case ofxVlc4::AudioMixMode::Surround4_0:
		mixModeIndex = 3;
		break;
	case ofxVlc4::AudioMixMode::Surround5_1:
		mixModeIndex = 4;
		break;
	case ofxVlc4::AudioMixMode::Surround7_1:
		mixModeIndex = 5;
		break;
	case ofxVlc4::AudioMixMode::Auto:
	default:
		break;
	}

	int stereoModeIndex = 0;
	switch (audioState.stereoMode) {
	case ofxVlc4::AudioStereoMode::Stereo:
		stereoModeIndex = 1;
		break;
	case ofxVlc4::AudioStereoMode::ReverseStereo:
		stereoModeIndex = 2;
		break;
	case ofxVlc4::AudioStereoMode::Left:
		stereoModeIndex = 3;
		break;
	case ofxVlc4::AudioStereoMode::Right:
		stereoModeIndex = 4;
		break;
	case ofxVlc4::AudioStereoMode::DolbySurround:
		stereoModeIndex = 5;
		break;
	case ofxVlc4::AudioStereoMode::Mono:
		stereoModeIndex = 6;
		break;
	case ofxVlc4::AudioStereoMode::Auto:
	default:
		break;
	}

	int callbackFormatIndex = 0;
	switch (player.getAudioCaptureSampleFormat()) {
	case ofxVlc4::AudioCaptureSampleFormat::Signed16:
		callbackFormatIndex = 1;
		break;
	case ofxVlc4::AudioCaptureSampleFormat::Signed32:
		callbackFormatIndex = 2;
		break;
	case ofxVlc4::AudioCaptureSampleFormat::Float32:
	default:
		break;
	}

	int callbackRateIndex = 2;
	switch (player.getAudioCaptureSampleRate()) {
	case 22050:
		callbackRateIndex = 0;
		break;
	case 32000:
		callbackRateIndex = 1;
		break;
	case 44100:
		callbackRateIndex = 2;
		break;
	case 48000:
		callbackRateIndex = 3;
		break;
	case 88200:
		callbackRateIndex = 4;
		break;
	case 96000:
		callbackRateIndex = 5;
		break;
	case 192000:
		callbackRateIndex = 6;
		break;
	default:
		break;
	}

	int callbackChannelIndex = player.getAudioCaptureChannelCount() <= 1 ? 0 : 1;
	float callbackBufferSeconds = static_cast<float>(player.getAudioCaptureBufferSeconds());
	volume = audioState.volumeKnown ? audioState.volume : player.getVolume();

	if (!ImGui::IsAnyItemActive()) {
		const std::string currentAudioFilterChain = player.getAudioFilterChain();
		if (std::string(audioFilterChain) != currentAudioFilterChain) {
			std::snprintf(audioFilterChain, sizeof(audioFilterChain), "%s", currentAudioFilterChain.c_str());
		}
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(compactControlWidth);

	if (ofVlcPlayer4GuiControls::beginSectionSubMenu("Playback", MenuContentPolicy::Leaf, false)) {
		ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(wideSliderWidth));
		if (ImGui::SliderInt("Volume", &volume, 0, 100)) {
			player.setVolume(volume);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(volume, 0, 100, 5, 1)) {
			player.setVolume(volume);
		}

		bool muted = audioState.mutedKnown ? audioState.muted : player.isMuted();
		if (ImGui::Checkbox("Mute", &muted)) {
			player.toggleMute();
			volume = player.getVolume();
		}

		bool captureEnabled = player.isAudioCaptureEnabled();
		if (ImGui::Checkbox("Audio Capture", &captureEnabled)) {
			player.setAudioCaptureEnabled(captureEnabled);
		}

		ImGui::Separator();
		ImGui::TextDisabled(
			"State: %s%s",
			audioState.ready ? "ready" : "waiting",
			audioState.paused ? " (paused)" : "");
		ImGui::TextDisabled(
			"Tracks: %d%s",
			audioState.trackCount,
			audioState.tracksAvailable ? "" : " (pending)");
		if (audioState.deviceKnown && !audioState.deviceId.empty()) {
			ImGui::TextDisabled("Active device: %s", audioState.deviceId.c_str());
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (ofVlcPlayer4GuiControls::beginSectionSubMenu("Live Controls", MenuContentPolicy::Leaf, false)) {
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Mixmode", mixModeIndex, mixModes, IM_ARRAYSIZE(mixModes))) {
			player.setAudioMixMode(static_cast<ofxVlc4::AudioMixMode>(mixModeIndex));
		}

		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Stereo", stereoModeIndex, stereoModes, IM_ARRAYSIZE(stereoModes))) {
			player.setAudioStereoMode(static_cast<ofxVlc4::AudioStereoMode>(stereoModeIndex));
		}

		const std::vector<ofxVlc4::AudioOutputModuleInfo> audioOutputModules = player.getAudioOutputModules();
		if (!audioOutputModules.empty()) {
			std::vector<std::string> moduleLabels;
			std::vector<const char *> moduleLabelPointers;
			moduleLabels.reserve(audioOutputModules.size() + 1u);
			moduleLabelPointers.reserve(audioOutputModules.size() + 1u);

			moduleLabels.push_back("Default");
			moduleLabelPointers.push_back(moduleLabels.back().c_str());

			int selectedModuleIndex = 0;
			const std::string currentModuleName = player.getSelectedAudioOutputModuleName();
			for (size_t moduleIndex = 0; moduleIndex < audioOutputModules.size(); ++moduleIndex) {
				moduleLabels.push_back(formatAudioOutputModuleLabel(audioOutputModules[moduleIndex]));
				moduleLabelPointers.push_back(moduleLabels.back().c_str());
				if (!currentModuleName.empty() && audioOutputModules[moduleIndex].name == currentModuleName) {
					selectedModuleIndex = static_cast<int>(moduleIndex) + 1;
				}
			}

			if (ofVlcPlayer4GuiControls::drawComboWithWheel("Output", selectedModuleIndex, moduleLabelPointers)) {
				if (selectedModuleIndex <= 0) {
					player.selectAudioOutputModule("");
				} else {
					player.selectAudioOutputModule(audioOutputModules[static_cast<size_t>(selectedModuleIndex - 1)].name);
				}
			}
		}

		const std::vector<ofxVlc4::AudioOutputDeviceInfo> audioOutputDevices = player.getAudioOutputDevices();
		if (!audioOutputDevices.empty()) {
			std::vector<std::string> deviceLabels;
			std::vector<const char *> deviceLabelPointers;
			deviceLabels.reserve(audioOutputDevices.size() + 1u);
			deviceLabelPointers.reserve(audioOutputDevices.size() + 1u);

			deviceLabels.push_back("Default");
			deviceLabelPointers.push_back(deviceLabels.back().c_str());

			int selectedDeviceIndex = 0;
			const std::string currentDeviceId =
				audioState.deviceKnown ? audioState.deviceId : player.getSelectedAudioOutputDeviceId();
			for (size_t deviceIndex = 0; deviceIndex < audioOutputDevices.size(); ++deviceIndex) {
				deviceLabels.push_back(formatAudioOutputDeviceLabel(audioOutputDevices[deviceIndex]));
				deviceLabelPointers.push_back(deviceLabels.back().c_str());
				if (!currentDeviceId.empty() && audioOutputDevices[deviceIndex].id == currentDeviceId) {
					selectedDeviceIndex = static_cast<int>(deviceIndex) + 1;
				}
			}

			if (ofVlcPlayer4GuiControls::drawComboWithWheel("Output Device", selectedDeviceIndex, deviceLabelPointers)) {
				if (selectedDeviceIndex <= 0) {
					player.selectAudioOutputDevice("");
				} else {
					player.selectAudioOutputDevice(audioOutputDevices[static_cast<size_t>(selectedDeviceIndex - 1)].id);
				}
			}
		}

		ImGui::TextDisabled("These controls apply live while audio is playing.");

		const std::vector<ofxVlc4::AudioFilterInfo> audioFilters = player.getAudioFilters();
		ImGui::Separator();
		ImGui::TextUnformatted("Reopen-Style VLC Filters");
		ImGui::TextDisabled("These filters rebuild or reopen the VLC audio pipeline when changed.");
		if (!audioFilters.empty()) {
			const std::vector<std::string> selectedAudioFilters = player.getAudioFilterChainEntries();
			ImGui::TextDisabled("%d selected", static_cast<int>(selectedAudioFilters.size()));
			for (const auto & filter : audioFilters) {
				bool enabled = player.hasAudioFilter(filter.name);
				if (ImGui::Checkbox(formatAudioFilterLabel(filter).c_str(), &enabled)) {
					player.toggleAudioFilter(filter.name);
					const std::string updatedChain = player.getAudioFilterChain();
					std::snprintf(audioFilterChain, sizeof(audioFilterChain), "%s", updatedChain.c_str());
					if (!audioFiltersBypassed) {
						bypassedAudioFilterChain.clear();
					}
				}
				drawAudioFilterTooltip(filter);
			}
		}

		ImGui::InputText("Filter Chain", audioFilterChain, sizeof(audioFilterChain));
		const float actionWidth = (compactControlWidth - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		if (ImGui::Button("Apply", ImVec2(actionWidth, 0.0f))) {
			player.setAudioFilterChain(audioFilterChain);
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear", ImVec2(actionWidth, 0.0f))) {
			audioFilterChain[0] = '\0';
			player.setAudioFilterChain("");
			audioFiltersBypassed = false;
			bypassedAudioFilterChain.clear();
		}
		ImGui::TextDisabled("Changing this chain usually reopens audio instead of applying seamlessly.");

		ImGui::Separator();
		ImGui::TextUnformatted("Audition");
		const float tripleActionWidth =
			(compactControlWidth - (ImGui::GetStyle().ItemSpacing.x * 2.0f)) / 3.0f;
		if (!audioFiltersBypassed) {
			if (ImGui::Button("Bypass", ImVec2(tripleActionWidth, 0.0f))) {
				bypassedAudioFilterChain = player.getAudioFilterChain();
				audioFiltersBypassed = !bypassedAudioFilterChain.empty();
				player.setAudioFilterChain("");
				audioFilterChain[0] = '\0';
			}
		} else {
			if (ImGui::Button("Restore", ImVec2(tripleActionWidth, 0.0f))) {
				player.setAudioFilterChain(bypassedAudioFilterChain);
				std::snprintf(audioFilterChain, sizeof(audioFilterChain), "%s", bypassedAudioFilterChain.c_str());
				audioFiltersBypassed = false;
				bypassedAudioFilterChain.clear();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Replay -3s", ImVec2(tripleActionWidth, 0.0f))) {
			replayAudioWindow(player, 3000);
		}
		ImGui::SameLine();
		if (ImGui::Button("Replay -8s", ImVec2(tripleActionWidth, 0.0f))) {
			replayAudioWindow(player, 8000);
		}
		ImGui::TextDisabled("Use Bypass/Restore for A/B comparison, then replay a few seconds to hear the change.");

		if (audioState.audioPtsAvailable) {
			ImGui::TextDisabled("Audio callback clock active.");
		}
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Callback Format", callbackFormatIndex, callbackFormats, IM_ARRAYSIZE(callbackFormats))) {
			player.setAudioCaptureSampleFormat(static_cast<ofxVlc4::AudioCaptureSampleFormat>(callbackFormatIndex));
		}
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Callback Rate", callbackRateIndex, callbackRates, IM_ARRAYSIZE(callbackRates))) {
			player.setAudioCaptureSampleRate(std::atoi(callbackRates[callbackRateIndex]));
		}
		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Callback Ch", callbackChannelIndex, callbackChannels, IM_ARRAYSIZE(callbackChannels))) {
			player.setAudioCaptureChannelCount(callbackChannelIndex == 0 ? 1 : 2);
		}
		if (ImGui::DragFloat("Callback Buffer", &callbackBufferSeconds, 0.05f, 0.05f, 5.0f, "%.2f s")) {
			player.setAudioCaptureBufferSeconds(callbackBufferSeconds);
		}
		ImGui::TextDisabled(
			"Live callback: %d Hz   %d ch",
			player.getSampleRate(),
			player.getChannelCount());
		ImGui::TextDisabled("Format/rate/ch apply on next audio setup/reopen.");
		ImGui::TextDisabled("Buffer applies immediately to the capture ringbuffer.");

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (ofVlcPlayer4GuiControls::beginSectionSubMenu("Sync", MenuContentPolicy::Leaf, false)) {
		float playbackRate = player.getPlaybackRate();
		ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(wideSliderWidth));
		if (ImGui::SliderFloat("Rate", &playbackRate, 0.25f, 4.0f, "%.2fx")) {
			player.setPlaybackRate(playbackRate);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(playbackRate, 0.25f, 4.0f, 0.05f)) {
			player.setPlaybackRate(playbackRate);
		}

		int audioDelayMs = audioState.audioDelayMs;
		ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(wideSliderWidth));
		if (ImGui::SliderInt("Audio Delay", &audioDelayMs, -5000, 5000, "%d ms")) {
			player.setAudioDelayMs(audioDelayMs);
		}
		if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(audioDelayMs, -5000, 5000, 10, 1)) {
			player.setAudioDelayMs(audioDelayMs);
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}
