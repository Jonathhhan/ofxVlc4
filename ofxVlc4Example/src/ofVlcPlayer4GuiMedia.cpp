#include "ofVlcPlayer4GuiMedia.h"
#include "ofVlcPlayer4GuiControls.h"
#include "ofMain.h"
#include "ofxVlc4.h"

#include "imgui_stdlib.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <vector>

namespace {
using MenuContentPolicy = ofVlcPlayer4GuiControls::MenuContentPolicy;

constexpr float kSingleActionButtonWidthRatio = 124.0f / 220.0f;
const ImVec2 kLogWindowPadding(12.0f, 8.0f);
const ImVec2 kLogItemSpacing(8.0f, 6.0f);
constexpr float kLogWindowHeight = 124.0f;
constexpr float kLogTextIndent = 8.0f;
constexpr float kLogTextEdgePadding = 2.0f;

std::string defaultLibVlcGuiLogPath() {
	ofDirectory::createDirectory(ofToDataPath("logs", true), true, true);
	return ofToDataPath("logs/ofxVlc4-libvlc.log", true);
}

std::string displayLabelForPathOrUri(const std::string & value) {
	if (value.empty()) {
		return "";
	}
	if (value.find("://") != std::string::npos) {
		return value;
	}
	return ofFilePath::getFileName(value);
}

std::string fileUrlForPath(std::string path) {
	if (path.empty()) {
		return "";
	}
	ofStringReplace(path, "\\", "/");
	ofStringReplace(path, " ", "%20");
	ofStringReplace(path, "#", "%23");
	return "file:///" + path;
}

std::string resolvedRecordFolderPath(const ofxVlc4::NativeRecordingStateInfo & state) {
	if (!state.directory.empty()) {
		return state.directory;
	}
	if (state.lastOutputPathAvailable && !state.lastOutputPath.empty()) {
		return ofFilePath::getEnclosingDirectory(state.lastOutputPath, false);
	}
	return "";
}

std::string metadataSizeSuffix(bool available, uint64_t bytes) {
	if (!available) {
		return "";
	}

	const double value = static_cast<double>(bytes);
	if (bytes >= 1024ull * 1024ull * 1024ull) {
		return "   " + ofToString(value / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
	}
	if (bytes >= 1024ull * 1024ull) {
		return "   " + ofToString(value / (1024.0 * 1024.0), 2) + " MB";
	}
	if (bytes >= 1024ull) {
		return "   " + ofToString(value / 1024.0, 1) + " KB";
	}
	return "   " + ofToString(bytes) + " B";
}

void subtitleColorToFloat3(int color, float rgb[3]) {
	const ofColor converted = ofColor::fromHex(static_cast<unsigned int>(ofClamp(color, 0x000000, 0xFFFFFF)));
	rgb[0] = converted.r / 255.0f;
	rgb[1] = converted.g / 255.0f;
	rgb[2] = converted.b / 255.0f;
}

int subtitleColorFromFloat3(const float rgb[3]) {
	const ofColor converted(
		static_cast<unsigned char>(ofClamp(rgb[0], 0.0f, 1.0f) * 255.0f),
		static_cast<unsigned char>(ofClamp(rgb[1], 0.0f, 1.0f) * 255.0f),
		static_cast<unsigned char>(ofClamp(rgb[2], 0.0f, 1.0f) * 255.0f));
	return static_cast<int>(converted.getHex());
}

std::string formatMediaTrackLabel(const ofxVlc4::MediaTrackInfo & track) {
	if (!track.name.empty()) {
		return track.name;
	}
	if (!track.language.empty()) {
		return track.language;
	}
	if (!track.description.empty()) {
		return track.description;
	}
	return track.id.empty() ? "Track" : track.id;
}

std::string formatCodecLabel(const ofxVlc4::MediaTrackInfo & track) {
	if (!track.codecName.empty()) {
		return track.codecName;
	}
	if (!track.codecFourcc.empty()) {
		return track.codecFourcc;
	}
	return "Unknown codec";
}

std::string formatTrackBitrate(unsigned bitrate) {
	if (bitrate == 0) {
		return "";
	}
	if (bitrate >= 1000000u) {
		return ofToString(static_cast<double>(bitrate) / 1000000.0, 2) + " Mbps";
	}
	if (bitrate >= 1000u) {
		return ofToString(static_cast<double>(bitrate) / 1000.0, 1) + " kbps";
	}
	return ofToString(bitrate) + " bps";
}

std::string formatTrackFrameRate(const ofxVlc4::MediaTrackInfo & track) {
	if (track.frameRateNum == 0 || track.frameRateDen == 0) {
		return "";
	}
	return ofToString(static_cast<double>(track.frameRateNum) / static_cast<double>(track.frameRateDen), 2) + " fps";
}

std::string formatTrackDetailLine(const ofxVlc4::MediaTrackInfo & track, const char * typeLabel) {
	std::vector<std::string> parts;
	parts.reserve(8);

	std::string label = formatMediaTrackLabel(track);
	if (label.empty()) {
		label = typeLabel;
	}
	if (track.selected) {
		label += " [Selected]";
	}
	parts.push_back(label);
	parts.push_back(formatCodecLabel(track));

	const std::string bitrateLabel = formatTrackBitrate(track.bitrate);
	if (track.width > 0 && track.height > 0) {
		parts.push_back(ofToString(track.width) + "x" + ofToString(track.height));
		const std::string frameRateLabel = formatTrackFrameRate(track);
		if (!frameRateLabel.empty()) {
			parts.push_back(frameRateLabel);
		}
		if (track.sampleAspectNum > 0 && track.sampleAspectDen > 0) {
			parts.push_back("SAR " + ofToString(track.sampleAspectNum) + ":" + ofToString(track.sampleAspectDen));
		}
	} else if (track.channels > 0 || track.sampleRate > 0) {
		if (track.channels > 0) {
			parts.push_back(ofToString(track.channels) + " ch");
		}
		if (track.sampleRate > 0) {
			parts.push_back(ofToString(track.sampleRate) + " Hz");
		}
	} else if (!track.subtitleEncoding.empty()) {
		parts.push_back(track.subtitleEncoding);
	}

	if (!bitrateLabel.empty()) {
		parts.push_back(bitrateLabel);
	}

	std::ostringstream stream;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (parts[i].empty()) {
			continue;
		}
		if (stream.tellp() > 0) {
			stream << "  |  ";
		}
		stream << parts[i];
	}
	return stream.str();
}

void drawTrackDetailsBlock(
	const char * heading,
	const std::vector<ofxVlc4::MediaTrackInfo> & tracks,
	const char * typeLabel) {
	if (tracks.empty()) {
		return;
	}

	ImGui::TextDisabled("%s", heading);
	for (const auto & track : tracks) {
		const std::string detailLine = formatTrackDetailLine(track, typeLabel);
		ImGui::BulletText("%s", detailLine.c_str());
		if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
			if (!track.description.empty()) {
				ImGui::TextWrapped("%s", track.description.c_str());
			}
			if (!track.language.empty()) {
				ImGui::Text("Language: %s", track.language.c_str());
			}
			if (!track.codecFourcc.empty()) {
				ImGui::Text("Codec Tag: %s", track.codecFourcc.c_str());
			}
			if (!track.originalFourcc.empty()) {
				ImGui::Text("Original Tag: %s", track.originalFourcc.c_str());
			}
			if (track.profile != 0 || track.level != 0) {
				ImGui::Text("Profile/Level: %d / %d", track.profile, track.level);
			}
			if (!track.id.empty()) {
				ImGui::Text("ID: %s%s", track.id.c_str(), track.idStable ? " [stable]" : "");
			}
			ImGui::EndTooltip();
		}
	}
}

std::string formatTitleLabel(const ofxVlc4::TitleInfo & title) {
	std::string label = title.name.empty() ? ("Title " + ofToString(title.index + 1)) : title.name;
	if (title.isMenu) {
		label += " [Menu]";
	} else if (title.isInteractive) {
		label += " [Interactive]";
	}
	return label;
}

std::string formatChapterLabel(const ofxVlc4::ChapterInfo & chapter) {
	const std::string baseLabel = chapter.name.empty() ? ("Chapter " + ofToString(chapter.index + 1)) : chapter.name;
	return baseLabel + " (" + ofToString(chapter.timeOffsetMs / 1000.0f, 1) + " s)";
}

std::string formatProgramLabel(const ofxVlc4::ProgramInfo & program) {
	std::string label = program.name.empty() ? ("Program " + ofToString(program.id)) : program.name;
	if (program.scrambled) {
		label += " [Scrambled]";
	}
	return label;
}

std::string formatAbLoopLabel(const ofxVlc4::AbLoopInfo & loop) {
	if (loop.state == ofxVlc4::AbLoopInfo::State::None) {
		return "A-B Loop: Off";
	}

	std::string label = "A-B Loop: ";
	label += (loop.state == ofxVlc4::AbLoopInfo::State::A) ? "A" : "A-B";
	if (loop.aTimeMs >= 0) {
		label += "  A " + ofToString(loop.aTimeMs / 1000.0f, 2) + " s";
	}
	if (loop.state == ofxVlc4::AbLoopInfo::State::B && loop.bTimeMs >= 0) {
		label += "  B " + ofToString(loop.bTimeMs / 1000.0f, 2) + " s";
	}
	return label;
}

std::string formatBookmarkLabel(const ofxVlc4::BookmarkInfo & bookmark) {
	std::string label = bookmark.label.empty() ? "Bookmark" : bookmark.label;
	label += " (" + ofToString(bookmark.timeMs / 1000.0f, 2) + " s)";
	if (bookmark.current) {
		label += " [Now]";
	}
	return label;
}

std::string formatMediaSlaveLabel(const ofxVlc4::MediaSlaveInfo & slave) {
	std::string label = (slave.type == ofxVlc4::MediaSlaveType::Audio) ? "Audio" : "Subtitle";
	const std::string source = displayLabelForPathOrUri(slave.uri);
	if (!source.empty()) {
		label += ": " + source;
	}
	if (slave.priority > 0) {
		label += " (P" + ofToString(slave.priority) + ")";
	}
	return label;
}

std::string formatRendererDiscovererLabel(const ofxVlc4::RendererDiscovererInfo & discoverer) {
	if (!discoverer.longName.empty()) {
		return discoverer.longName;
	}
	if (!discoverer.name.empty()) {
		return discoverer.name;
	}
	return "Discoverer";
}

std::string formatRendererLabel(const ofxVlc4::RendererInfo & renderer) {
	std::string label = renderer.name.empty() ? "Renderer" : renderer.name;
	if (!renderer.type.empty()) {
		label += " (" + renderer.type + ")";
	}
	return label;
}

std::string formatDiscoveredMediaLabel(const ofxVlc4::DiscoveredMediaItemInfo & item) {
	std::string label = item.name.empty() ? "Media" : item.name;
	if (item.isDirectory) {
		label += " [Dir]";
	}
	return label;
}

const char * mediaDiscovererCategoryLabel(ofxVlc4::MediaDiscovererCategory category) {
	switch (category) {
	case ofxVlc4::MediaDiscovererCategory::Devices:
		return "Devices";
	case ofxVlc4::MediaDiscovererCategory::Podcasts:
		return "Podcasts";
	case ofxVlc4::MediaDiscovererCategory::LocalDirs:
		return "Local Dirs";
	case ofxVlc4::MediaDiscovererCategory::Lan:
	default:
		return "LAN";
	}
}

ImVec4 dialogSeverityColor(ofxVlc4::DialogQuestionSeverity severity) {
	switch (severity) {
	case ofxVlc4::DialogQuestionSeverity::Warning:
		return ImVec4(0.95f, 0.75f, 0.20f, 1.0f);
	case ofxVlc4::DialogQuestionSeverity::Critical:
		return ImVec4(0.95f, 0.35f, 0.30f, 1.0f);
	case ofxVlc4::DialogQuestionSeverity::Normal:
	default:
		return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
	}
}

std::string formatRendererCapabilities(const ofxVlc4::RendererInfo & renderer) {
	if (renderer.canAudio && renderer.canVideo) {
		return "Audio + Video";
	}
	if (renderer.canVideo) {
		return "Video";
	}
	if (renderer.canAudio) {
		return "Audio";
	}
	return "";
}

const char * enabledLabel(bool enabled) {
	return enabled ? "yes" : "no";
}

const char * mediaPlayerRoleLabel(ofxVlc4::MediaPlayerRole role) {
	switch (role) {
	case ofxVlc4::MediaPlayerRole::Music:
		return "Music";
	case ofxVlc4::MediaPlayerRole::Video:
		return "Video";
	case ofxVlc4::MediaPlayerRole::Communication:
		return "Communication";
	case ofxVlc4::MediaPlayerRole::Game:
		return "Game";
	case ofxVlc4::MediaPlayerRole::Notification:
		return "Notification";
	case ofxVlc4::MediaPlayerRole::Animation:
		return "Animation";
	case ofxVlc4::MediaPlayerRole::Production:
		return "Production";
	case ofxVlc4::MediaPlayerRole::Accessibility:
		return "Accessibility";
	case ofxVlc4::MediaPlayerRole::Test:
		return "Test";
	case ofxVlc4::MediaPlayerRole::None:
	default:
		return "None";
	}
}

std::string formatWatchTimeValue(int64_t timeUs) {
	if (timeUs < 0) {
		return "-";
	}
	return ofToString(static_cast<double>(timeUs) / 1000000.0, 3) + " s";
}

void syncEditableString(std::string & localValue, bool & loaded, const std::string & externalValue) {
	if (!loaded || (!ImGui::IsAnyItemActive() && localValue != externalValue)) {
		localValue = externalValue;
		loaded = true;
	}
}

const char * libVlcLogLevelLabel(int level) {
	switch (level) {
	case LIBVLC_ERROR:
		return "Error";
	case LIBVLC_WARNING:
		return "Warn";
	case LIBVLC_NOTICE:
		return "Notice";
	case LIBVLC_DEBUG:
	default:
		return "Debug";
	}
}

ImVec4 libVlcLogLevelColor(int level) {
	switch (level) {
	case LIBVLC_ERROR:
		return ImVec4(0.95f, 0.35f, 0.30f, 1.0f);
	case LIBVLC_WARNING:
		return ImVec4(0.95f, 0.75f, 0.20f, 1.0f);
	case LIBVLC_NOTICE:
		return ImVec4(0.70f, 0.82f, 0.98f, 1.0f);
	case LIBVLC_DEBUG:
	default:
		return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
	}
}

const char * mediaParseStatusLabel(ofxVlc4::MediaParseStatus status) {
	switch (status) {
	case ofxVlc4::MediaParseStatus::Pending:
		return "Pending";
	case ofxVlc4::MediaParseStatus::Skipped:
		return "Skipped";
	case ofxVlc4::MediaParseStatus::Failed:
		return "Failed";
	case ofxVlc4::MediaParseStatus::Timeout:
		return "Timeout";
	case ofxVlc4::MediaParseStatus::Cancelled:
		return "Cancelled";
	case ofxVlc4::MediaParseStatus::Done:
		return "Done";
	case ofxVlc4::MediaParseStatus::None:
	default:
		return "None";
	}
}

std::string formatByteCount(uint64_t bytes) {
	const double value = static_cast<double>(bytes);
	if (bytes >= 1024ull * 1024ull * 1024ull) {
		return ofToString(value / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
	}
	if (bytes >= 1024ull * 1024ull) {
		return ofToString(value / (1024.0 * 1024.0), 2) + " MB";
	}
	if (bytes >= 1024ull) {
		return ofToString(value / 1024.0, 1) + " KB";
	}
	return ofToString(bytes) + " B";
}

std::string formatBitrateValue(float bitrate) {
	if (bitrate <= 0.0f) {
		return "0";
	}
	if (bitrate >= 1000.0f) {
		return ofToString(bitrate / 1000.0f, 2) + " Mbps";
	}
	return ofToString(bitrate, 2) + " Kbps";
}

std::string formatProblemRate(uint64_t problemCount, uint64_t referenceCount) {
	if (problemCount == 0 || referenceCount == 0) {
		return "0.00%";
	}
	return ofToString((static_cast<double>(problemCount) / static_cast<double>(referenceCount)) * 100.0, 2) + "%";
}

const char * colorSpaceLabel(libvlc_video_color_space_t value);
const char * colorPrimariesLabel(libvlc_video_color_primaries_t value);
const char * transferFuncLabel(libvlc_video_transfer_func_t value);

void drawHdrMetadataSummary(const ofxVlc4::VideoHdrMetadataInfo & hdrMetadata) {
	ImGui::TextDisabled(
		"HDR metadata backend: %s   HDR10 frame metadata: %s",
		enabledLabel(hdrMetadata.supported),
		enabledLabel(hdrMetadata.available));
	if (!hdrMetadata.supported) {
		return;
	}

	ImGui::TextDisabled(
		"Video signal: %ux%u   %u-bit   %s   %s / %s / %s",
		hdrMetadata.width,
		hdrMetadata.height,
		hdrMetadata.bitDepth,
		enabledLabel(hdrMetadata.fullRange),
		colorSpaceLabel(hdrMetadata.colorspace),
		colorPrimariesLabel(hdrMetadata.primaries),
		transferFuncLabel(hdrMetadata.transfer));
	if (!hdrMetadata.available) {
		return;
	}

	ImGui::TextDisabled(
		"HDR10: MaxCLL %u   MaxFALL %u   Master %.2f / %.2f nits",
		hdrMetadata.maxContentLightLevel,
		hdrMetadata.maxFrameAverageLightLevel,
		static_cast<double>(hdrMetadata.maxMasteringLuminance) / 10000.0,
		static_cast<double>(hdrMetadata.minMasteringLuminance) / 10000.0);
}

void drawMediaStatsSummary(const ofxVlc4::MediaStats & stats) {
	if (!stats.available) {
		ImGui::TextDisabled("Stats: unavailable");
		return;
	}

	const uint64_t problemPictures = stats.latePictures + stats.lostPictures;
	ImGui::TextDisabled(
		"Input: %s   bitrate %s",
		formatByteCount(stats.readBytes).c_str(),
		formatBitrateValue(stats.inputBitrate).c_str());
	ImGui::TextDisabled(
		"Demux: %s   bitrate %s   corrupt %llu   disc %llu",
		formatByteCount(stats.demuxReadBytes).c_str(),
		formatBitrateValue(stats.demuxBitrate).c_str(),
		static_cast<unsigned long long>(stats.demuxCorrupted),
		static_cast<unsigned long long>(stats.demuxDiscontinuity));
	ImGui::TextDisabled(
		"Video: dec %llu   disp %llu   dropped %llu (%s)",
		static_cast<unsigned long long>(stats.decodedVideo),
		static_cast<unsigned long long>(stats.displayedPictures),
		static_cast<unsigned long long>(problemPictures),
		formatProblemRate(problemPictures, stats.decodedVideo).c_str());
	ImGui::TextDisabled(
		"       late %llu   lost %llu",
		static_cast<unsigned long long>(stats.latePictures),
		static_cast<unsigned long long>(stats.lostPictures));
	ImGui::TextDisabled(
		"Audio: dec %llu   played %llu   lost %llu",
		static_cast<unsigned long long>(stats.decodedAudio),
		static_cast<unsigned long long>(stats.playedAudioBuffers),
		static_cast<unsigned long long>(stats.lostAudioBuffers));
}

void drawLibVlcLogEntries(const std::vector<ofxVlc4::LibVlcLogEntry> & libVlcLogs) {
	if (libVlcLogs.empty()) {
		ImGui::TextDisabled("libVLC logs: none");
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, kLogWindowPadding);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, kLogItemSpacing);
	if (ImGui::BeginChild("LibVlcLogs", ImVec2(0.0f, kLogWindowHeight), true)) {
		ImGui::Dummy(ImVec2(kLogTextIndent, kLogTextEdgePadding));
		ImGui::Indent(kLogTextIndent);
		const int firstIndex = std::max(0, static_cast<int>(libVlcLogs.size()) - 24);
		for (int i = static_cast<int>(libVlcLogs.size()) - 1; i >= firstIndex; --i) {
			const auto & entry = libVlcLogs[static_cast<size_t>(i)];
			ImGui::PushStyleColor(ImGuiCol_Text, libVlcLogLevelColor(entry.level));
			ImGui::TextWrapped("[%s] %s", libVlcLogLevelLabel(entry.level), entry.message.c_str());
			ImGui::PopStyleColor();

			if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
				if (!entry.module.empty()) {
					ImGui::Text("Module: %s", entry.module.c_str());
				}
				if (!entry.objectName.empty()) {
					ImGui::Text("Object: %s", entry.objectName.c_str());
				}
				if (!entry.objectHeader.empty()) {
					ImGui::Text("Header: %s", entry.objectHeader.c_str());
				}
				if (!entry.file.empty()) {
					if (entry.line > 0) {
						ImGui::Text("Source: %s:%u", entry.file.c_str(), entry.line);
					} else {
						ImGui::Text("Source: %s", entry.file.c_str());
					}
				}
				if (entry.objectId != 0) {
					ImGui::Text("Object ID: %llu", static_cast<unsigned long long>(entry.objectId));
				}
				ImGui::EndTooltip();
			}
		}
		ImGui::Unindent(kLogTextIndent);
		ImGui::Dummy(ImVec2(0.0f, kLogTextEdgePadding));
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

const char * colorSpaceLabel(libvlc_video_color_space_t value) {
	switch (value) {
	case libvlc_video_colorspace_BT601:
		return "BT.601";
	case libvlc_video_colorspace_BT709:
		return "BT.709";
	case libvlc_video_colorspace_BT2020:
		return "BT.2020";
	default:
		return "Undefined";
	}
}

const char * colorPrimariesLabel(libvlc_video_color_primaries_t value) {
	switch (value) {
	case libvlc_video_primaries_BT601_525:
		return "BT.601 525";
	case libvlc_video_primaries_BT601_625:
		return "BT.601 625";
	case libvlc_video_primaries_BT709:
		return "BT.709";
	case libvlc_video_primaries_BT2020:
		return "BT.2020";
	case libvlc_video_primaries_DCI_P3:
		return "DCI-P3";
	case libvlc_video_primaries_BT470_M:
		return "BT.470 M";
	default:
		return "Undefined";
	}
}

const char * transferFuncLabel(libvlc_video_transfer_func_t value) {
	switch (value) {
	case libvlc_video_transfer_func_LINEAR:
		return "Linear";
	case libvlc_video_transfer_func_SRGB:
		return "sRGB";
	case libvlc_video_transfer_func_BT470_BG:
		return "BT.470 BG";
	case libvlc_video_transfer_func_BT470_M:
		return "BT.470 M";
	case libvlc_video_transfer_func_PQ:
		return "PQ";
	case libvlc_video_transfer_func_HLG:
		return "HLG";
	case libvlc_video_transfer_func_BT709:
		return "BT.709";
	case libvlc_video_transfer_func_SMPTE_240:
		return "SMPTE 240";
	default:
		return "Undefined";
	}
}

void drawTrackSelector(
	const char * label,
	const std::vector<ofxVlc4::MediaTrackInfo> & tracks,
	bool includeOffOption,
	const std::function<void(const std::string &)> & selectTrackById) {
	std::vector<std::string> trackLabels;
	std::vector<const char *> trackLabelPointers;
	trackLabels.reserve(tracks.size() + (includeOffOption ? 1u : 0u));
	trackLabelPointers.reserve(tracks.size() + (includeOffOption ? 1u : 0u));

	int selectedTrackIndex = 0;
	if (includeOffOption) {
		trackLabels.push_back("Off");
		trackLabelPointers.push_back(trackLabels.back().c_str());
	}

	for (int trackIndex = 0; trackIndex < static_cast<int>(tracks.size()); ++trackIndex) {
		trackLabels.push_back(formatMediaTrackLabel(tracks[static_cast<size_t>(trackIndex)]));
		trackLabelPointers.push_back(trackLabels.back().c_str());
		if (tracks[static_cast<size_t>(trackIndex)].selected) {
			selectedTrackIndex = trackIndex + (includeOffOption ? 1 : 0);
		}
	}

	if (trackLabelPointers.empty()) {
		return;
	}

	const auto commitSelection = [&]() {
		if (includeOffOption && selectedTrackIndex == 0) {
			selectTrackById("");
			return;
		}

		const int trackOffset = includeOffOption ? 1 : 0;
		const int trackIndex = selectedTrackIndex - trackOffset;
		if (trackIndex >= 0 && trackIndex < static_cast<int>(tracks.size())) {
			selectTrackById(tracks[static_cast<size_t>(trackIndex)].id);
		}
	};

	if (ofVlcPlayer4GuiControls::drawComboWithWheel(label, selectedTrackIndex, trackLabelPointers)) {
		commitSelection();
	}
}

void drawProgramSelector(ofxVlc4 & player, const std::vector<ofxVlc4::ProgramInfo> & programs) {
	if (programs.empty()) {
		return;
	}

	std::vector<std::string> programLabels;
	std::vector<const char *> programLabelPointers;
	programLabels.reserve(programs.size());
	programLabelPointers.reserve(programs.size());

	int selectedProgramIndex = 0;
	for (int i = 0; i < static_cast<int>(programs.size()); ++i) {
		programLabels.push_back(formatProgramLabel(programs[static_cast<size_t>(i)]));
		programLabelPointers.push_back(programLabels.back().c_str());
		if (programs[static_cast<size_t>(i)].current) {
			selectedProgramIndex = i;
		}
	}

	if (ofVlcPlayer4GuiControls::drawComboWithWheel("Program", selectedProgramIndex, programLabelPointers)) {
		player.selectProgramId(programs[static_cast<size_t>(selectedProgramIndex)].id);
	}
}

void drawNavigationButtonRow(
	ofxVlc4 & player,
	const char * const labels[],
	const ofxVlc4::NavigationMode modes[],
	int itemCount,
	float buttonWidth,
	float buttonSpacing,
	const char * idSuffix) {
	for (int i = 0; i < itemCount; ++i) {
		if (i > 0) {
			ImGui::SameLine(0.0f, buttonSpacing);
		}

		const std::string buttonId = std::string(labels[static_cast<size_t>(i)]) + "##" + idSuffix;
		if (ImGui::Button(buttonId.c_str(), ImVec2(buttonWidth, 0.0f))) {
			player.navigate(modes[static_cast<size_t>(i)]);
		}
	}
}

void drawSubtitleStylingControls(ofxVlc4 & player, float compactControlWidth) {
	static const char * subtitleRenderers[] = { "Auto", "Freetype", "Sapi", "Dummy", "None" };
	int rendererIndex = static_cast<int>(player.getSubtitleTextRenderer());
	if (ofVlcPlayer4GuiControls::drawComboWithWheel("Renderer", rendererIndex, subtitleRenderers, IM_ARRAYSIZE(subtitleRenderers))) {
		player.setSubtitleTextRenderer(static_cast<ofxVlc4SubtitleTextRenderer>(rendererIndex));
	}

	bool subtitleBold = player.isSubtitleBold();
	if (ImGui::Checkbox("Bold", &subtitleBold)) {
		player.setSubtitleBold(subtitleBold);
	}

	int subtitleOpacity = player.getSubtitleTextOpacity();
	ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(compactControlWidth));
	if (ImGui::SliderInt("Subtitle Opacity", &subtitleOpacity, 0, 255)) {
		player.setSubtitleTextOpacity(subtitleOpacity);
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(subtitleOpacity, 0, 255, 5, 1)) {
		player.setSubtitleTextOpacity(subtitleOpacity);
	}

	float subtitleColorRgb[3];
	subtitleColorToFloat3(player.getSubtitleTextColor(), subtitleColorRgb);
	if (ImGui::ColorEdit3("Subtitle Color", subtitleColorRgb, ImGuiColorEditFlags_NoAlpha)) {
		player.setSubtitleTextColor(subtitleColorFromFloat3(subtitleColorRgb));
	}
}

const char * thumbnailImageTypeLabel(ofxVlc4::ThumbnailImageType type) {
	switch (type) {
		case ofxVlc4::ThumbnailImageType::Png: return "PNG";
		case ofxVlc4::ThumbnailImageType::Jpg: return "JPG";
		case ofxVlc4::ThumbnailImageType::WebP: return "WebP";
		default: return "PNG";
	}
}
}

bool ofVlcPlayer4GuiMedia::hasDetachedDiagnosticsWindow() const {
	return ofVlcPlayer4GuiControls::isDetachedSubMenuOpen("Diagnostics");
}

void ofVlcPlayer4GuiMedia::drawContent(
	ofxVlc4 & player,
	const ImVec2 & labelInnerSpacing,
	float compactControlWidth,
	float inputLabelPadding,
	float dualActionButtonWidth,
	float buttonSpacing,
	bool detachedOnly) {
	const float singleActionButtonWidth = compactControlWidth * kSingleActionButtonWidthRatio;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, labelInnerSpacing);
	ImGui::PushItemWidth(compactControlWidth);
	const auto beginSubMenu = [detachedOnly](const char * label, MenuContentPolicy policy) {
		return detachedOnly
			? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu(label, policy)
			: ofVlcPlayer4GuiControls::beginSectionSubMenu(label, policy, false);
	};

	if (detachedOnly && player.isPlaying()) {
		drawDiagnosticsSubMenu(player, inputLabelPadding, singleActionButtonWidth, dualActionButtonWidth, buttonSpacing, true);
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();
		return;
	}

	if (beginSubMenu("Capture & Record", MenuContentPolicy::Leaf)) {
		const ofxVlc4::PlaybackStateInfo playbackState = player.getPlaybackStateInfo();
		const std::string snapshotSizeSuffix = metadataSizeSuffix(
			playbackState.snapshot.lastSavedMetadataAvailable,
			playbackState.snapshot.lastSavedBytes);
		const std::string nativeRecordSizeSuffix = metadataSizeSuffix(
			playbackState.nativeRecording.lastOutputMetadataAvailable,
			playbackState.nativeRecording.lastOutputBytes);
		const std::string recordFolderPath = resolvedRecordFolderPath(playbackState.nativeRecording);
		if (ImGui::Button("Snapshot", ImVec2(singleActionButtonWidth, 0.0f))) {
			player.takeSnapshot();
		}

		bool nativeRecordingEnabled = player.isNativeRecordingEnabled();
		if (ImGui::Checkbox("Native Record", &nativeRecordingEnabled)) {
			player.setNativeRecordingEnabled(nativeRecordingEnabled);
		}

		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - inputLabelPadding);
		std::string nativeRecordDirectory = player.getNativeRecordDirectory();
		if (ImGui::InputText("Path", &nativeRecordDirectory)) {
			player.setNativeRecordDirectory(nativeRecordDirectory);
		}
		ImGui::PopItemWidth();

		ImGui::BeginDisabled(!playbackState.snapshot.available || playbackState.snapshot.lastSavedPath.empty());
		if (ImGui::Button("Copy Snapshot Path", ImVec2(dualActionButtonWidth, 0.0f))) {
			ImGui::SetClipboardText(playbackState.snapshot.lastSavedPath.c_str());
		}
		ImGui::EndDisabled();
		ImGui::SameLine(0.0f, buttonSpacing);
		ImGui::BeginDisabled(recordFolderPath.empty());
		if (ImGui::Button("Open Record Folder", ImVec2(dualActionButtonWidth, 0.0f))) {
			const std::string absoluteFolderPath = ofFilePath::getAbsolutePath(recordFolderPath);
			ofLaunchBrowser(fileUrlForPath(absoluteFolderPath));
		}
		ImGui::EndDisabled();

		if (playbackState.snapshot.pending) {
			ImGui::TextDisabled("Snapshot: pending");
		} else if (!playbackState.snapshot.lastFailureReason.empty()) {
			ImGui::TextDisabled("Snapshot: %s", playbackState.snapshot.lastFailureReason.c_str());
		} else if (playbackState.snapshot.available) {
			ImGui::TextDisabled(
				"Snapshot: %s%s",
				playbackState.snapshot.lastSavedPath.c_str(),
				snapshotSizeSuffix.c_str());
		}
		if (!playbackState.snapshot.lastSavedTimestamp.empty()) {
			ImGui::TextDisabled("Snapshot time: %s", playbackState.snapshot.lastSavedTimestamp.c_str());
		}

		if (playbackState.nativeRecording.active) {
			ImGui::TextDisabled("Native record: active");
		} else if (!playbackState.nativeRecording.lastFailureReason.empty()) {
			ImGui::TextDisabled("Native record: %s", playbackState.nativeRecording.lastFailureReason.c_str());
		} else if (playbackState.nativeRecording.lastOutputPathAvailable) {
			ImGui::TextDisabled(
				"Native record: %s%s",
				playbackState.nativeRecording.lastOutputPath.c_str(),
				nativeRecordSizeSuffix.c_str());
		}
		if (!playbackState.nativeRecording.lastEventMessage.empty()) {
			ImGui::TextDisabled("%s", playbackState.nativeRecording.lastEventMessage.c_str());
		}

		ImGui::Separator();
		ImGui::TextDisabled("Thumbnail");

		static const char * thumbnailImageTypes[] = { "PNG", "JPG", "WebP" };
		static const char * thumbnailSeekSpeeds[] = { "Precise", "Fast" };

		ofVlcPlayer4GuiControls::drawComboWithWheel("Image Type", thumbnailImageTypeIndex, thumbnailImageTypes, IM_ARRAYSIZE(thumbnailImageTypes));
		ofVlcPlayer4GuiControls::drawComboWithWheel("Seek Speed", thumbnailSeekSpeedIndex, thumbnailSeekSpeeds, IM_ARRAYSIZE(thumbnailSeekSpeeds));

		ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(compactControlWidth));
		ImGui::SliderInt("Width##Thumb", &thumbnailWidth, 0, 1920, thumbnailWidth == 0 ? "auto" : "%d");
		ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(compactControlWidth));
		ImGui::SliderInt("Height##Thumb", &thumbnailHeight, 0, 1080, thumbnailHeight == 0 ? "auto" : "%d");

		const ofxVlc4::ThumbnailInfo thumbInfo = player.getLastGeneratedThumbnail();

		const float thumbButtonWidth =
			(ImGui::GetContentRegionAvail().x - (buttonSpacing * 2.0f)) / 3.0f;

		ImGui::BeginDisabled(thumbInfo.requestActive);
		if (ImGui::Button("By Time", ImVec2(thumbButtonWidth, 0.0f))) {
			const int64_t timeMs = player.getTime();
			player.requestThumbnailByTime(
				static_cast<int>(timeMs),
				static_cast<unsigned>(thumbnailWidth),
				static_cast<unsigned>(thumbnailHeight),
				false,
				static_cast<ofxVlc4::ThumbnailImageType>(thumbnailImageTypeIndex),
				static_cast<ofxVlc4::ThumbnailSeekSpeed>(thumbnailSeekSpeedIndex));
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		if (ImGui::Button("By Position", ImVec2(thumbButtonWidth, 0.0f))) {
			const float position = player.getPosition();
			player.requestThumbnailByPosition(
				position,
				static_cast<unsigned>(thumbnailWidth),
				static_cast<unsigned>(thumbnailHeight),
				false,
				static_cast<ofxVlc4::ThumbnailImageType>(thumbnailImageTypeIndex),
				static_cast<ofxVlc4::ThumbnailSeekSpeed>(thumbnailSeekSpeedIndex));
		}
		ImGui::EndDisabled();
		ImGui::SameLine(0.0f, buttonSpacing);
		ImGui::BeginDisabled(!thumbInfo.requestActive);
		if (ImGui::Button("Cancel", ImVec2(thumbButtonWidth, 0.0f))) {
			player.cancelThumbnailRequest();
		}
		ImGui::EndDisabled();

		if (thumbInfo.requestActive) {
			ImGui::TextDisabled("Thumbnail: generating...");
		} else if (thumbInfo.available) {
			ImGui::TextDisabled("Thumbnail: %s", thumbInfo.path.c_str());
			ImGui::TextDisabled("   %ux%u at %lld ms", thumbInfo.width, thumbInfo.height, static_cast<long long>(thumbInfo.timeMs));
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("DVD / Disc", MenuContentPolicy::Leaf)) {
		const ofxVlc4::NavigationStateInfo navigationState = player.getNavigationStateInfo();
		if (navigationState.available) {
			ImGui::TextDisabled(
				"Programs: %d   Titles: %d   Chapters: %d",
				navigationState.programCount,
				navigationState.titleCount,
				navigationState.chapterCount);
		} else {
			ImGui::TextDisabled("Disc navigation is not active for the current media.");
		}

		const std::vector<ofxVlc4::TitleInfo> titles = player.getTitles();
		if (!titles.empty()) {
			std::vector<std::string> titleLabels;
			std::vector<const char *> titleLabelPointers;
			titleLabels.reserve(titles.size());
			titleLabelPointers.reserve(titles.size());

			int selectedTitleIndex = 0;
			for (int i = 0; i < static_cast<int>(titles.size()); ++i) {
				titleLabels.push_back(formatTitleLabel(titles[static_cast<size_t>(i)]));
				titleLabelPointers.push_back(titleLabels.back().c_str());
				if (titles[static_cast<size_t>(i)].current) {
					selectedTitleIndex = i;
				}
			}

			if (ofVlcPlayer4GuiControls::drawComboWithWheel("Title", selectedTitleIndex, titleLabelPointers)) {
				player.selectTitleIndex(titles[static_cast<size_t>(selectedTitleIndex)].index);
			}
		}

		const std::vector<ofxVlc4::ChapterInfo> chapters = player.getChapters();
		if (!chapters.empty()) {
			std::vector<std::string> chapterLabels;
			std::vector<const char *> chapterLabelPointers;
			chapterLabels.reserve(chapters.size());
			chapterLabelPointers.reserve(chapters.size());

			int selectedChapterIndex = 0;
			for (int i = 0; i < static_cast<int>(chapters.size()); ++i) {
				chapterLabels.push_back(formatChapterLabel(chapters[static_cast<size_t>(i)]));
				chapterLabelPointers.push_back(chapterLabels.back().c_str());
				if (chapters[static_cast<size_t>(i)].current) {
					selectedChapterIndex = i;
				}
			}

			if (ofVlcPlayer4GuiControls::drawComboWithWheel("Chapter", selectedChapterIndex, chapterLabelPointers)) {
				player.selectChapterIndex(chapters[static_cast<size_t>(selectedChapterIndex)].index);
			}

			if (ImGui::Button("Prev Chapter", ImVec2(dualActionButtonWidth, 0.0f))) {
				player.previousChapter();
			}
			ImGui::SameLine(0.0f, buttonSpacing);
			if (ImGui::Button("Next Chapter", ImVec2(dualActionButtonWidth, 0.0f))) {
				player.nextChapter();
			}
		}

		drawProgramSelector(player, player.getPrograms());

		const float tripleActionButtonWidth =
			(ImGui::GetContentRegionAvail().x - (buttonSpacing * 2.0f)) / 3.0f;
		static const char * topNavigationLabels[] = { "Up", "OK", "Popup" };
		static const ofxVlc4::NavigationMode topNavigationModes[] = {
			ofxVlc4::NavigationMode::Up,
			ofxVlc4::NavigationMode::Activate,
			ofxVlc4::NavigationMode::Popup
		};
		static const char * bottomNavigationLabels[] = { "Left", "Down", "Right" };
		static const ofxVlc4::NavigationMode bottomNavigationModes[] = {
			ofxVlc4::NavigationMode::Left,
			ofxVlc4::NavigationMode::Down,
			ofxVlc4::NavigationMode::Right
		};
		drawNavigationButtonRow(
			player,
			topNavigationLabels,
			topNavigationModes,
			3,
			tripleActionButtonWidth,
			buttonSpacing,
			"dvdnav_top");
		drawNavigationButtonRow(
			player,
			bottomNavigationLabels,
			bottomNavigationModes,
			3,
			tripleActionButtonWidth,
			buttonSpacing,
			"dvdnav_bottom");

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Navigation & Input", MenuContentPolicy::Leaf)) {

		if (ImGui::Button("Next Frame", ImVec2(singleActionButtonWidth, 0.0f))) {
			player.nextFrame();
		}

		bool keyInputEnabled = player.isKeyInputEnabled();
		if (ImGui::Checkbox("Key Input", &keyInputEnabled)) {
			player.setKeyInputEnabled(keyInputEnabled);
		}

		bool mouseInputEnabled = player.isMouseInputEnabled();
		if (ImGui::Checkbox("Mouse Input", &mouseInputEnabled)) {
			player.setMouseInputEnabled(mouseInputEnabled);
		}

		int cursorX = 0;
		int cursorY = 0;
		if (player.getCursorPosition(cursorX, cursorY)) {
			ImGui::TextDisabled("Cursor: %d, %d", cursorX, cursorY);
		} else {
			ImGui::TextDisabled("Cursor: unavailable");
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	drawDiagnosticsSubMenu(player, inputLabelPadding, singleActionButtonWidth, dualActionButtonWidth, buttonSpacing, detachedOnly);
	drawMetadataSubMenu(player, inputLabelPadding, singleActionButtonWidth, dualActionButtonWidth, buttonSpacing, detachedOnly);
	drawDialogsSubMenu(player, inputLabelPadding, singleActionButtonWidth, dualActionButtonWidth, buttonSpacing, detachedOnly);

	if (beginSubMenu("Tracks & Subtitles", MenuContentPolicy::Leaf)) {
		const ofxVlc4::SubtitleStateInfo subtitleState = player.getSubtitleStateInfo();
		const bool playbackActive = player.isPlaying();
		if (playbackActive) {
			drawTrackSelector("Subtitle Track", subtitleState.tracks, true, [&](const std::string & trackId) {
				player.selectSubtitleTrackById(trackId);
			});

			const float subtitleButtonWidth =
				(ImGui::GetContentRegionAvail().x - buttonSpacing) / 2.0f;
			if (ImGui::Button("Load Subtitle...", ImVec2(subtitleButtonWidth, 0.0f))) {
				ofFileDialogResult result = ofSystemLoadDialog("Select subtitle file");
				if (result.bSuccess) {
					const std::string selectedPath = result.getPath();
					mediaSlaveTypeIndex = 0;
					mediaSlavePath = selectedPath;
					player.addSubtitleSlave(selectedPath);
				}
			}
			ImGui::SameLine(0.0f, buttonSpacing);
			ImGui::BeginDisabled(!subtitleState.trackSelected);
			if (ImGui::Button("Disable Subtitle", ImVec2(subtitleButtonWidth, 0.0f))) {
				player.selectSubtitleTrackById("");
			}
			ImGui::EndDisabled();

			int subtitleDelayMs = player.getSubtitleDelayMs();
			ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(compactControlWidth));
			if (ImGui::SliderInt("Subtitle Delay", &subtitleDelayMs, -5000, 5000, "%d ms")) {
				player.setSubtitleDelayMs(subtitleDelayMs);
			}
			if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(subtitleDelayMs, -5000, 5000, 50, 10)) {
				player.setSubtitleDelayMs(subtitleDelayMs);
			}

			float subtitleTextScale = player.getSubtitleTextScale();
			ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(compactControlWidth));
			if (ImGui::SliderFloat("Subtitle Scale", &subtitleTextScale, 0.5f, 3.0f, "%.2fx")) {
				player.setSubtitleTextScale(subtitleTextScale);
			}
			if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(subtitleTextScale, 0.5f, 3.0f, 0.1f, 0.05f)) {
				player.setSubtitleTextScale(subtitleTextScale);
			}

			drawSubtitleStylingControls(player, compactControlWidth);
		} else {
			const std::vector<ofxVlc4::MediaTrackInfo> videoTracks = player.getVideoTracks();
			const std::vector<ofxVlc4::MediaTrackInfo> audioTracks = player.getAudioTracks();
			const std::vector<ofxVlc4::MediaTrackInfo> subtitleTracks = player.getSubtitleTracks();
			const ofxVlc4::SubtitleStateInfo subtitleState = player.getSubtitleStateInfo();
			if (!audioTracks.empty()) {
				drawTrackSelector("Audio Track", audioTracks, false, [&](const std::string & trackId) {
					player.selectAudioTrackById(trackId);
				});
			}

			drawTrackSelector("Subtitle Track", subtitleTracks, true, [&](const std::string & trackId) {
				player.selectSubtitleTrackById(trackId);
			});

			const float subtitleButtonWidth =
				(ImGui::GetContentRegionAvail().x - buttonSpacing) / 2.0f;
			if (ImGui::Button("Load Subtitle...", ImVec2(subtitleButtonWidth, 0.0f))) {
				ofFileDialogResult result = ofSystemLoadDialog("Select subtitle file");
				if (result.bSuccess) {
					const std::string selectedPath = result.getPath();
					mediaSlaveTypeIndex = 0;
					mediaSlavePath = selectedPath;
					player.addSubtitleSlave(selectedPath);
				}
			}
			ImGui::SameLine(0.0f, buttonSpacing);
			ImGui::BeginDisabled(subtitleTracks.empty() && !subtitleState.trackSelected);
			if (ImGui::Button("Disable Subtitle", ImVec2(subtitleButtonWidth, 0.0f))) {
				player.selectSubtitleTrackById("");
			}
			ImGui::EndDisabled();

			int subtitleDelayMs = player.getSubtitleDelayMs();
			ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(compactControlWidth));
			if (ImGui::SliderInt("Subtitle Delay", &subtitleDelayMs, -5000, 5000, "%d ms")) {
				player.setSubtitleDelayMs(subtitleDelayMs);
			}
			if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(subtitleDelayMs, -5000, 5000, 50, 10)) {
				player.setSubtitleDelayMs(subtitleDelayMs);
			}

			float subtitleTextScale = player.getSubtitleTextScale();
			ImGui::SetNextItemWidth(ofVlcPlayer4GuiControls::getCompactLabeledControlWidth(compactControlWidth));
			if (ImGui::SliderFloat("Subtitle Scale", &subtitleTextScale, 0.5f, 3.0f, "%.2fx")) {
				player.setSubtitleTextScale(subtitleTextScale);
			}
			if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(subtitleTextScale, 0.5f, 3.0f, 0.1f, 0.05f)) {
				player.setSubtitleTextScale(subtitleTextScale);
			}

			drawSubtitleStylingControls(player, compactControlWidth);

			drawTrackDetailsBlock("Video Details", videoTracks, "Video");
			drawTrackDetailsBlock("Audio Details", audioTracks, "Audio");
			drawTrackDetailsBlock("Subtitle Details", subtitleTracks, "Subtitle");
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Loop & Bookmarks", MenuContentPolicy::Leaf)) {
		const ofxVlc4::AbLoopInfo abLoop = player.getAbLoop();
		const float abLoopButtonWidth =
			(ImGui::GetContentRegionAvail().x - (buttonSpacing * 2.0f)) / 3.0f;
		if (ImGui::Button("Set A", ImVec2(abLoopButtonWidth, 0.0f))) {
			player.setAbLoopA();
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		if (ImGui::Button("Set B", ImVec2(abLoopButtonWidth, 0.0f))) {
			player.setAbLoopB();
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		ImGui::BeginDisabled(abLoop.state == ofxVlc4::AbLoopInfo::State::None);
		if (ImGui::Button("Clear A-B", ImVec2(abLoopButtonWidth, 0.0f))) {
			player.clearAbLoop();
		}
		ImGui::EndDisabled();
		ImGui::TextDisabled("%s", formatAbLoopLabel(abLoop).c_str());

		const std::vector<ofxVlc4::BookmarkInfo> bookmarks = player.getCurrentBookmarks();
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - inputLabelPadding);
		ImGui::InputText("Label##Bookmark", &bookmarkLabel);
		ImGui::PopItemWidth();

		if (ImGui::Button("Add Bookmark", ImVec2(dualActionButtonWidth, 0.0f))) {
			if (player.addCurrentBookmark(bookmarkLabel)) {
				bookmarkLabel.clear();
				selectedBookmarkIndex = 0;
			}
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		ImGui::BeginDisabled(bookmarks.empty());
		if (ImGui::Button("Clear Bookmarks", ImVec2(dualActionButtonWidth, 0.0f))) {
			player.clearCurrentBookmarks();
			selectedBookmarkIndex = 0;
		}
		ImGui::EndDisabled();

		if (!bookmarks.empty()) {
			std::vector<std::string> bookmarkLabels;
			std::vector<const char *> bookmarkLabelPointers;
			bookmarkLabels.reserve(bookmarks.size());
			bookmarkLabelPointers.reserve(bookmarks.size());
			for (const auto & bookmark : bookmarks) {
				bookmarkLabels.push_back(formatBookmarkLabel(bookmark));
				bookmarkLabelPointers.push_back(bookmarkLabels.back().c_str());
			}

			selectedBookmarkIndex = ofClamp(selectedBookmarkIndex, 0, static_cast<int>(bookmarks.size()) - 1);
			ofVlcPlayer4GuiControls::drawComboWithWheel(
				"Bookmark",
				selectedBookmarkIndex,
				bookmarkLabelPointers);

			if (ImGui::Button("Jump", ImVec2(dualActionButtonWidth, 0.0f))) {
				player.seekToBookmark(bookmarks[static_cast<size_t>(selectedBookmarkIndex)].id);
			}
			ImGui::SameLine(0.0f, buttonSpacing);
			if (ImGui::Button("Remove", ImVec2(dualActionButtonWidth, 0.0f))) {
				player.removeBookmark(bookmarks[static_cast<size_t>(selectedBookmarkIndex)].id);
				selectedBookmarkIndex = 0;
			}
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("External Media", MenuContentPolicy::Leaf)) {
		static const char * slaveTypes[] = { "Subtitle", "Audio" };
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - inputLabelPadding);
		const bool submitSlavePath = ImGui::InputText("Path", &mediaSlavePath, ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopItemWidth();

		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Slave Type", mediaSlaveTypeIndex, slaveTypes, IM_ARRAYSIZE(slaveTypes))) {
			mediaSlaveTypeIndex = ofClamp(mediaSlaveTypeIndex, 0, IM_ARRAYSIZE(slaveTypes) - 1);
		}

		const auto addCurrentMediaSlave = [&]() {
			const auto slaveType = mediaSlaveTypeIndex == 0
				? ofxVlc4::MediaSlaveType::Subtitle
				: ofxVlc4::MediaSlaveType::Audio;
			const bool added = (slaveType == ofxVlc4::MediaSlaveType::Subtitle)
				? player.addSubtitleSlave(mediaSlavePath)
				: player.addAudioSlave(mediaSlavePath);
			if (added) {
				mediaSlavePath.clear();
			}
		};

		if (ImGui::Button("Add Slave", ImVec2(dualActionButtonWidth, 0.0f)) || submitSlavePath) {
			addCurrentMediaSlave();
		}
		ImGui::SameLine(0.0f, buttonSpacing);
		const std::vector<ofxVlc4::MediaSlaveInfo> mediaSlaves = player.getMediaSlaves();
		ImGui::BeginDisabled(mediaSlaves.empty());
		if (ImGui::Button("Clear Slaves", ImVec2(dualActionButtonWidth, 0.0f))) {
			player.clearMediaSlaves();
		}
		ImGui::EndDisabled();

		if (!mediaSlaves.empty()) {
			for (const ofxVlc4::MediaSlaveInfo & slave : mediaSlaves) {
				ImGui::BulletText("%s", formatMediaSlaveLabel(slave).c_str());
				if (ImGui::IsItemHovered() && !slave.uri.empty() && ImGui::BeginTooltip()) {
					ImGui::TextWrapped("%s", slave.uri.c_str());
					ImGui::EndTooltip();
				}
			}
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	if (beginSubMenu("Renderer & Discovery", MenuContentPolicy::Leaf)) {
		const ofxVlc4::RendererStateInfo rendererState = player.getRendererStateInfo();
		const std::vector<ofxVlc4::RendererDiscovererInfo> discoverers = player.getRendererDiscoverers();
		std::vector<std::string> discovererLabels;
		std::vector<const char *> discovererLabelPointers;
		discovererLabels.reserve(discoverers.size() + 1);
		discovererLabelPointers.reserve(discoverers.size() + 1);
		discovererLabels.push_back("Off");
		discovererLabelPointers.push_back(discovererLabels.back().c_str());

		int selectedDiscovererIndex = 0;
		const std::string currentDiscovererName = rendererState.discovererName;
		for (int i = 0; i < static_cast<int>(discoverers.size()); ++i) {
			discovererLabels.push_back(formatRendererDiscovererLabel(discoverers[static_cast<size_t>(i)]));
			discovererLabelPointers.push_back(discovererLabels.back().c_str());
			if (discoverers[static_cast<size_t>(i)].name == currentDiscovererName) {
				selectedDiscovererIndex = i + 1;
			}
		}

		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Discoverer", selectedDiscovererIndex, discovererLabelPointers)) {
			if (selectedDiscovererIndex <= 0) {
				player.stopRendererDiscovery();
			} else {
				player.startRendererDiscovery(discoverers[static_cast<size_t>(selectedDiscovererIndex - 1)].name);
			}
		}

		ImGui::TextDisabled(
			"Renderer discovery: %s   found: %u",
			enabledLabel(rendererState.discoveryActive),
			static_cast<unsigned>(rendererState.discoveredRendererCount));

		const std::vector<ofxVlc4::RendererInfo> renderers = player.getDiscoveredRenderers();
		std::vector<std::string> rendererLabels;
		std::vector<const char *> rendererLabelPointers;
		rendererLabels.reserve(renderers.size() + 1);
		rendererLabelPointers.reserve(renderers.size() + 1);
		rendererLabels.push_back("Local");
		rendererLabelPointers.push_back(rendererLabels.back().c_str());

		int selectedRendererIndex = 0;
		const std::string selectedRendererId = rendererState.requestedRendererId;
		for (int i = 0; i < static_cast<int>(renderers.size()); ++i) {
			rendererLabels.push_back(formatRendererLabel(renderers[static_cast<size_t>(i)]));
			rendererLabelPointers.push_back(rendererLabels.back().c_str());
			if (renderers[static_cast<size_t>(i)].id == selectedRendererId) {
				selectedRendererIndex = i + 1;
			}
		}

		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Renderer", selectedRendererIndex, rendererLabelPointers)) {
			if (selectedRendererIndex <= 0) {
				player.clearRenderer();
			} else {
				player.selectRenderer(renderers[static_cast<size_t>(selectedRendererIndex - 1)].id);
			}
		}

		if (rendererState.reconnectPending && !rendererState.requestedRendererId.empty()) {
			ImGui::TextDisabled("Renderer: reconnect pending, using local output");
		} else if (rendererState.selectedRendererAvailable && !rendererState.selectedRenderer.name.empty()) {
			ImGui::TextDisabled("Renderer active: %s", rendererState.selectedRenderer.name.c_str());
		} else if (rendererState.usingLocalFallback) {
			ImGui::TextDisabled("Renderer active: local output");
		}

		if (selectedRendererIndex > 0 && selectedRendererIndex - 1 < static_cast<int>(renderers.size())) {
			const ofxVlc4::RendererInfo & renderer = renderers[static_cast<size_t>(selectedRendererIndex - 1)];
			const std::string capabilities = formatRendererCapabilities(renderer);
			if (!capabilities.empty()) {
				ImGui::TextDisabled("%s", capabilities.c_str());
			}
			if (ImGui::IsItemHovered() && (!renderer.iconUri.empty() || !renderer.type.empty()) && ImGui::BeginTooltip()) {
				if (!renderer.type.empty()) {
					ImGui::Text("Type: %s", renderer.type.c_str());
				}
				if (!capabilities.empty()) {
					ImGui::Text("Caps: %s", capabilities.c_str());
				}
				if (!renderer.iconUri.empty()) {
					ImGui::Separator();
					ImGui::TextWrapped("%s", renderer.iconUri.c_str());
				}
				ImGui::EndTooltip();
			}
		} else if (rendererState.discoveryActive && renderers.empty()) {
			ImGui::TextDisabled("Searching renderer...");
		}

		ImGui::Separator();
		static const ofxVlc4::MediaDiscovererCategory discovererCategories[] = {
			ofxVlc4::MediaDiscovererCategory::Devices,
			ofxVlc4::MediaDiscovererCategory::Lan,
			ofxVlc4::MediaDiscovererCategory::Podcasts,
			ofxVlc4::MediaDiscovererCategory::LocalDirs
		};
		static const char * discovererCategoryLabels[] = {
			"Devices", "LAN", "Podcasts", "Local Dirs"
		};
		mediaDiscovererCategoryIndex = ofClamp(mediaDiscovererCategoryIndex, 0, IM_ARRAYSIZE(discovererCategoryLabels) - 1);
		ofVlcPlayer4GuiControls::drawComboWithWheel(
			"Category",
			mediaDiscovererCategoryIndex,
			discovererCategoryLabels,
			IM_ARRAYSIZE(discovererCategoryLabels));

		const auto activeCategory = discovererCategories[static_cast<size_t>(mediaDiscovererCategoryIndex)];
		const std::vector<ofxVlc4::MediaDiscovererInfo> mediaDiscoverers = player.getMediaDiscoverers(activeCategory);
		const ofxVlc4::MediaDiscoveryStateInfo mediaDiscoveryState = player.getMediaDiscoveryState();
		std::vector<std::string> mediaDiscovererLabels;
		std::vector<const char *> mediaDiscovererLabelPointers;
		mediaDiscovererLabels.reserve(mediaDiscoverers.size() + 1);
		mediaDiscovererLabelPointers.reserve(mediaDiscoverers.size() + 1);
		mediaDiscovererLabels.push_back("Off");
		mediaDiscovererLabelPointers.push_back(mediaDiscovererLabels.back().c_str());

		int selectedMediaDiscovererIndex = 0;
		const std::string selectedMediaDiscovererName = player.getSelectedMediaDiscovererName();
		for (int i = 0; i < static_cast<int>(mediaDiscoverers.size()); ++i) {
			const auto & discoverer = mediaDiscoverers[static_cast<size_t>(i)];
			mediaDiscovererLabels.push_back(discoverer.longName.empty() ? discoverer.name : discoverer.longName);
			mediaDiscovererLabelPointers.push_back(mediaDiscovererLabels.back().c_str());
			if (discoverer.name == selectedMediaDiscovererName) {
				selectedMediaDiscovererIndex = i + 1;
			}
		}

		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Discover", selectedMediaDiscovererIndex, mediaDiscovererLabelPointers)) {
			selectedDiscoveredMediaIndex = 0;
			if (selectedMediaDiscovererIndex <= 0) {
				player.stopMediaDiscovery();
			} else {
				player.startMediaDiscovery(mediaDiscoverers[static_cast<size_t>(selectedMediaDiscovererIndex - 1)].name);
			}
		}

		ImGui::TextDisabled(
			"Media discovery: %s   items: %u   dirs: %u   completed: %s",
			enabledLabel(mediaDiscoveryState.active),
			static_cast<unsigned>(mediaDiscoveryState.itemCount),
			static_cast<unsigned>(mediaDiscoveryState.directoryCount),
			enabledLabel(mediaDiscoveryState.endReached));
		if (!mediaDiscoveryState.discovererName.empty()) {
			if (!mediaDiscoveryState.discovererLongName.empty()) {
				ImGui::TextDisabled(
					"Active service: %s (%s)",
					mediaDiscoveryState.discovererLongName.c_str(),
					mediaDiscoveryState.discovererName.c_str());
			} else {
				ImGui::TextDisabled("Active service: %s", mediaDiscoveryState.discovererName.c_str());
			}
			ImGui::TextDisabled("Service category: %s", mediaDiscovererCategoryLabel(mediaDiscoveryState.category));
		}

		const std::vector<ofxVlc4::DiscoveredMediaItemInfo> discoveredMediaItems = player.getDiscoveredMediaItems();
		if (!discoveredMediaItems.empty()) {
			std::vector<std::string> discoveredMediaLabels;
			std::vector<const char *> discoveredMediaLabelPointers;
			discoveredMediaLabels.reserve(discoveredMediaItems.size());
			discoveredMediaLabelPointers.reserve(discoveredMediaItems.size());
			for (const auto & item : discoveredMediaItems) {
				discoveredMediaLabels.push_back(formatDiscoveredMediaLabel(item));
				discoveredMediaLabelPointers.push_back(discoveredMediaLabels.back().c_str());
			}

			selectedDiscoveredMediaIndex = ofClamp(selectedDiscoveredMediaIndex, 0, static_cast<int>(discoveredMediaItems.size()) - 1);
			ofVlcPlayer4GuiControls::drawComboWithWheel(
				"Item",
				selectedDiscoveredMediaIndex,
				discoveredMediaLabelPointers);

			const float discoveryActionButtonWidth =
				(ImGui::GetContentRegionAvail().x - (buttonSpacing * 2.0f)) / 3.0f;
			if (ImGui::Button("Add Item", ImVec2(discoveryActionButtonWidth, 0.0f))) {
				player.addDiscoveredMediaItemToPlaylist(selectedDiscoveredMediaIndex);
			}
			ImGui::SameLine(0.0f, buttonSpacing);
			if (ImGui::Button("Play Item", ImVec2(discoveryActionButtonWidth, 0.0f))) {
				player.playDiscoveredMediaItem(selectedDiscoveredMediaIndex);
			}
			ImGui::SameLine(0.0f, buttonSpacing);
			if (ImGui::Button("Add All", ImVec2(discoveryActionButtonWidth, 0.0f))) {
				player.addAllDiscoveredMediaItemsToPlaylist();
			}

			const auto & item = discoveredMediaItems[static_cast<size_t>(selectedDiscoveredMediaIndex)];
			if (!item.mrl.empty()) {
				ImGui::TextDisabled("Selected MRL:");
				ImGui::TextWrapped("%s", item.mrl.c_str());
			}
		} else if (player.isMediaDiscoveryActive()) {
			ImGui::TextDisabled("Searching media...");
		}

		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4GuiMedia::drawDiagnosticsSubMenu(
	ofxVlc4 & player,
	float inputLabelPadding,
	float singleActionButtonWidth,
	float dualActionButtonWidth,
	float buttonSpacing,
	bool detachedOnly) {
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Diagnostics", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Diagnostics", MenuContentPolicy::Leaf, false);
	if (!open) {
		return;
	}

	static const ofxVlc4::MediaPlayerRole roleValues[] = {
		ofxVlc4::MediaPlayerRole::None,
		ofxVlc4::MediaPlayerRole::Music,
		ofxVlc4::MediaPlayerRole::Video,
		ofxVlc4::MediaPlayerRole::Communication,
		ofxVlc4::MediaPlayerRole::Game,
		ofxVlc4::MediaPlayerRole::Notification,
		ofxVlc4::MediaPlayerRole::Animation,
		ofxVlc4::MediaPlayerRole::Production,
		ofxVlc4::MediaPlayerRole::Accessibility,
		ofxVlc4::MediaPlayerRole::Test
	};
	static const char * roleLabels[] = {
		"None",
		"Music",
		"Video",
		"Communication",
		"Game",
		"Notification",
		"Animation",
		"Production",
		"Accessibility",
		"Test"
	};

	int selectedRoleIndex = 0;
	const ofxVlc4::MediaPlayerRole currentRole = player.getMediaPlayerRole();
	for (int i = 0; i < IM_ARRAYSIZE(roleValues); ++i) {
		if (roleValues[i] == currentRole) {
			selectedRoleIndex = i;
			break;
		}
	}
	if (ofVlcPlayer4GuiControls::drawComboWithWheel("Role", selectedRoleIndex, roleLabels, IM_ARRAYSIZE(roleLabels))) {
		player.setMediaPlayerRole(roleValues[selectedRoleIndex]);
	}

	bool watchTimeEnabled = player.isWatchTimeEnabled();
	if (ImGui::Checkbox("Watch Time", &watchTimeEnabled)) {
		player.setWatchTimeEnabled(watchTimeEnabled);
	}
	ImGui::SameLine(0.0f, buttonSpacing);
	int watchTimePeriodMs = static_cast<int>(player.getWatchTimeMinPeriodUs() / 1000);
	if (ImGui::InputInt("Watch Period", &watchTimePeriodMs, 10, 100)) {
		player.setWatchTimeMinPeriodUs(static_cast<int64_t>(std::max(0, watchTimePeriodMs)) * 1000);
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(watchTimePeriodMs, 0, 5000, 10, 1)) {
		player.setWatchTimeMinPeriodUs(static_cast<int64_t>(watchTimePeriodMs) * 1000);
	}

	const ofxVlc4::WatchTimeInfo watchTimeInfo = player.getWatchTimeInfo();
	ImGui::TextDisabled(
		"Watch: %s   paused: %s   seeking: %s   role: %s",
		enabledLabel(watchTimeInfo.registered),
		enabledLabel(watchTimeInfo.paused),
		enabledLabel(watchTimeInfo.seeking),
		mediaPlayerRoleLabel(currentRole));
	if (watchTimeInfo.available) {
		ImGui::TextDisabled(
			"Time: %s   interp: %s   pos %.3f / %.3f   rate %.2f",
			formatWatchTimeValue(watchTimeInfo.timeUs).c_str(),
			formatWatchTimeValue(watchTimeInfo.interpolatedTimeUs).c_str(),
			watchTimeInfo.position,
			watchTimeInfo.interpolatedPosition,
			watchTimeInfo.rate);
		if (!player.isPlaying()) {
			const double watchFps = player.getPlaybackClockFramesPerSecond();
			ImGui::TextDisabled(
				"Timecode: %s   interp: %s   fps %.3f",
				ofxVlc4::formatPlaybackTimecode(watchTimeInfo.timeUs, watchFps).c_str(),
				player.formatCurrentPlaybackTimecode(watchFps, true).c_str(),
				watchFps);
		} else {
			ImGui::TextDisabled("Timecode is hidden during active playback while FPS probing is isolated.");
		}
	} else {
		ImGui::TextDisabled("Time watch: no point received yet");
	}

	ofxVlc4::MediaParseOptions parseOptions = player.getMediaParseOptions();
	bool parseOptionsChanged = false;
	parseOptionsChanged |= ImGui::Checkbox("Parse Local", &parseOptions.parseLocal);
	ImGui::SameLine(0.0f, buttonSpacing);
	parseOptionsChanged |= ImGui::Checkbox("Parse Network", &parseOptions.parseNetwork);
	parseOptionsChanged |= ImGui::Checkbox("Forced", &parseOptions.forced);
	ImGui::SameLine(0.0f, buttonSpacing);
	parseOptionsChanged |= ImGui::Checkbox("Interact", &parseOptions.doInteract);
	parseOptionsChanged |= ImGui::Checkbox("Fetch Local", &parseOptions.fetchLocal);
	ImGui::SameLine(0.0f, buttonSpacing);
	parseOptionsChanged |= ImGui::Checkbox("Fetch Network", &parseOptions.fetchNetwork);

	int parseTimeoutMs = parseOptions.timeoutMs;
	if (ImGui::InputInt("Parse Timeout", &parseTimeoutMs, 250, 1000)) {
		parseOptions.timeoutMs = parseTimeoutMs;
		parseOptionsChanged = true;
	}
	if (ofVlcPlayer4GuiControls::applyHoveredWheelStep(parseTimeoutMs, -1, 60000, 250, 50)) {
		parseOptions.timeoutMs = parseTimeoutMs;
		parseOptionsChanged = true;
	}
	ImGui::TextDisabled("-1 = default timeout");
	if (parseOptionsChanged) {
		player.setMediaParseOptions(parseOptions);
	}

	if (ImGui::Button("Parse", ImVec2(dualActionButtonWidth, 0.0f))) {
		player.requestCurrentMediaParse();
	}
	ImGui::SameLine(0.0f, buttonSpacing);
	if (ImGui::Button("Stop Parse", ImVec2(dualActionButtonWidth, 0.0f))) {
		player.stopCurrentMediaParse();
	}

	const ofxVlc4::MediaParseInfo parseInfo = player.getCurrentMediaParseInfo();
	const ofxVlc4::MediaReadinessInfo readiness = player.getMediaReadinessInfo();
	ImGui::TextDisabled(
		"Parse: %s   active: %s   requested: %s",
		mediaParseStatusLabel(parseInfo.status),
		enabledLabel(parseInfo.active),
		enabledLabel(parseInfo.requested));
	if (parseInfo.lastCompletedStatus != ofxVlc4::MediaParseStatus::None) {
		ImGui::TextDisabled("Last parse result: %s", mediaParseStatusLabel(parseInfo.lastCompletedStatus));
	}

	const ofxVlc4::VideoStateInfo videoState = player.getVideoStateInfo();
	ImGui::TextDisabled(
		"Ready: media %s   prep %s   geom %s   frame %s   play %s",
		enabledLabel(readiness.mediaAttached),
		enabledLabel(readiness.startupPrepared),
		enabledLabel(readiness.geometryKnown),
		enabledLabel(readiness.hasReceivedVideoFrame),
		enabledLabel(readiness.playbackActive));
	ImGui::TextDisabled(
		"Tracks: V %d (%s)   A %d (%s)   S %d (%s)   Nav %s",
		readiness.videoTrackCount,
		enabledLabel(readiness.videoTracksReady),
		readiness.audioTrackCount,
		enabledLabel(readiness.audioTracksReady),
		readiness.subtitleTrackCount,
		enabledLabel(readiness.subtitleTracksReady),
		enabledLabel(readiness.navigationReady));
	ImGui::TextDisabled(
		"canPause: %s   hasVout: %s (%u)   scrambled: %s",
		enabledLabel(player.canPause()),
		enabledLabel(videoState.hasVideoOutput),
		videoState.videoOutputCount,
		enabledLabel(player.isScrambled()));
	drawHdrMetadataSummary(videoState.hdrMetadata);
	drawMediaStatsSummary(player.getMediaStats());

	bool libVlcLoggingEnabled = player.isLibVlcLoggingEnabled();
	if (ImGui::Checkbox("libVLC Logging", &libVlcLoggingEnabled)) {
		player.setLibVlcLoggingEnabled(libVlcLoggingEnabled);
	}
	ImGui::SameLine(0.0f, buttonSpacing);
	bool libVlcLogFileEnabled = player.isLibVlcLogFileEnabled();
	if (ImGui::Checkbox("Log File", &libVlcLogFileEnabled)) {
		if (libVlcLogFileEnabled && player.getLibVlcLogFilePath().empty()) {
			const std::string defaultLogPath = defaultLibVlcGuiLogPath();
			player.setLibVlcLogFilePath(defaultLogPath);
			libVlcLogFilePath = defaultLogPath;
			libVlcLogFilePathLoaded = true;
		}
		player.setLibVlcLogFileEnabled(libVlcLogFileEnabled);
	}

	syncLibVlcLogFilePath(player);
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - inputLabelPadding);
	if (ImGui::InputText("Log Path", &libVlcLogFilePath)) {
		player.setLibVlcLogFilePath(libVlcLogFilePath);
	}
	ImGui::PopItemWidth();
	ImGui::TextDisabled("File logging temporarily overrides the in-memory libVLC view while enabled.");
	if (ImGui::Button("Clear Logs", ImVec2(dualActionButtonWidth, 0.0f))) {
		player.clearLibVlcLogEntries();
	}
	ImGui::SameLine(0.0f, buttonSpacing);
	if (ImGui::Button("Print Report", ImVec2(dualActionButtonWidth, 0.0f))) {
		const std::string report = player.getDiagnosticsReport();
		ofLogNotice("ofxVlc4") << "\n" << report;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Print a full diagnostics report to the openFrameworks log (ofLogNotice).");
	}
	ImGui::SameLine(0.0f, buttonSpacing);
	if (ImGui::Button("Copy Report", ImVec2(dualActionButtonWidth, 0.0f))) {
		ImGui::SetClipboardText(player.getDiagnosticsReport().c_str());
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Copy a full diagnostics report to the clipboard.");
	}
	const std::vector<ofxVlc4::LibVlcLogEntry> libVlcLogs = player.getLibVlcLogEntries();
	drawLibVlcLogEntries(libVlcLogs);
	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4GuiMedia::drawMetadataSubMenu(
	ofxVlc4 & player,
	float inputLabelPadding,
	float singleActionButtonWidth,
	float dualActionButtonWidth,
	float buttonSpacing,
	bool detachedOnly) {
	const std::string currentPath = player.getCurrentPath();
	const std::string currentMediaId = !currentPath.empty() ? currentPath : player.getCurrentFileName();
	const bool hasCurrentMedia = !currentMediaId.empty();

	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Metadata", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Metadata", MenuContentPolicy::Leaf, false);
	if (!open) {
		return;
	}

	const bool freezeLiveMetadata = detachedOnly && player.isPlaying();
	if (freezeLiveMetadata) {
		ImGui::TextDisabled("Metadata editing is paused while detached playback is active.");
		ImGui::TextDisabled("Pause or stop playback to inspect and edit live metadata here.");
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
		return;
	}

	syncMetadataEditor(player, currentMediaId, hasCurrentMedia);

	if (!hasCurrentMedia) {
		ImGui::TextDisabled("Load media to edit metadata.");
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
		return;
	}

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - inputLabelPadding);
	if (ImGui::InputText("Title", &editMetaTitle)) {
		player.setCurrentMediaMeta(ofxVlc4::MediaMetaField::Title, editMetaTitle);
	}
	if (ImGui::InputText("Artist", &editMetaArtist)) {
		player.setCurrentMediaMeta(ofxVlc4::MediaMetaField::Artist, editMetaArtist);
	}
	if (ImGui::InputText("Album", &editMetaAlbum)) {
		player.setCurrentMediaMeta(ofxVlc4::MediaMetaField::Album, editMetaAlbum);
	}
	ImGui::PopItemWidth();

	const std::vector<std::string> metaExtraNames = player.getCurrentMediaMetaExtraNames();
	if (!metaExtraNames.empty()) {
		selectedMetaExtraIndex = ofClamp(selectedMetaExtraIndex, 0, static_cast<int>(metaExtraNames.size()) - 1);
		std::vector<const char *> metaExtraNamePointers;
		metaExtraNamePointers.reserve(metaExtraNames.size());
		for (const std::string & name : metaExtraNames) {
			metaExtraNamePointers.push_back(name.c_str());
		}

		if (ofVlcPlayer4GuiControls::drawComboWithWheel("Meta Extra", selectedMetaExtraIndex, metaExtraNamePointers)) {
			metaExtraName = metaExtraNames[static_cast<size_t>(selectedMetaExtraIndex)];
			metaExtraValue = player.getCurrentMediaMetaExtra(metaExtraName);
		}
	}

	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - inputLabelPadding);
	ImGui::InputText("Extra Name", &metaExtraName);
	ImGui::InputText("Extra Value", &metaExtraValue);
	ImGui::PopItemWidth();
	if (ImGui::Button("Set Extra", ImVec2(dualActionButtonWidth, 0.0f))) {
		player.setCurrentMediaMetaExtra(metaExtraName, metaExtraValue);
	}
	ImGui::SameLine(0.0f, buttonSpacing);
	if (ImGui::Button("Remove Extra", ImVec2(dualActionButtonWidth, 0.0f))) {
		if (player.removeCurrentMediaMetaExtra(metaExtraName)) {
			metaExtraValue.clear();
		}
	}
	if (ImGui::Button("Save Meta", ImVec2(singleActionButtonWidth, 0.0f))) {
		player.saveCurrentMediaMeta();
	}

	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4GuiMedia::drawDialogsSubMenu(
	ofxVlc4 & player,
	float inputLabelPadding,
	float singleActionButtonWidth,
	float dualActionButtonWidth,
	float buttonSpacing,
	bool detachedOnly) {
	const bool open = detachedOnly
		? ofVlcPlayer4GuiControls::beginDetachedOnlySubMenu("Dialogs", MenuContentPolicy::Leaf)
		: ofVlcPlayer4GuiControls::beginSectionSubMenu("Dialogs", MenuContentPolicy::Leaf, false);
	if (!open) {
		return;
	}

	const ofxVlc4::DialogErrorInfo dialogError = player.getLastDialogError();
	if (dialogError.available) {
		if (!dialogError.title.empty()) {
			ImGui::TextWrapped("%s", dialogError.title.c_str());
		}
		if (!dialogError.text.empty()) {
			ImGui::TextWrapped("%s", dialogError.text.c_str());
		}
		if (ImGui::Button("Clear Error", ImVec2(singleActionButtonWidth, 0.0f))) {
			player.clearLastDialogError();
		}
		ImGui::Separator();
	}

	const std::vector<ofxVlc4::DialogInfo> activeDialogs = player.getActiveDialogs();
	if (activeDialogs.empty()) {
		activeLoginDialogToken = 0;
		dialogUsername.clear();
		dialogPassword.clear();
		dialogStore = false;
		if (!dialogError.available) {
			ImGui::TextDisabled("No active dialogs.");
		}
		ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
		return;
	}

	for (size_t dialogIndex = 0; dialogIndex < activeDialogs.size(); ++dialogIndex) {
		const ofxVlc4::DialogInfo & dialog = activeDialogs[dialogIndex];
		if (!dialog.title.empty()) {
			ImGui::TextWrapped("%s", dialog.title.c_str());
		}
		if (!dialog.text.empty()) {
			if (dialog.type == ofxVlc4::DialogType::Question) {
				ImGui::PushStyleColor(ImGuiCol_Text, dialogSeverityColor(dialog.severity));
				ImGui::TextWrapped("%s", dialog.text.c_str());
				ImGui::PopStyleColor();
			} else {
				ImGui::TextWrapped("%s", dialog.text.c_str());
			}
		}

		if (dialog.type == ofxVlc4::DialogType::Login) {
			if (activeLoginDialogToken != dialog.token) {
				activeLoginDialogToken = dialog.token;
				dialogUsername = dialog.defaultUsername;
				dialogPassword.clear();
				dialogStore = false;
			}
			ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - inputLabelPadding);
			ImGui::InputText(("User##Dialog" + ofToString(dialog.token)).c_str(), &dialogUsername);
			ImGui::InputText(("Pass##Dialog" + ofToString(dialog.token)).c_str(), &dialogPassword, ImGuiInputTextFlags_Password);
			ImGui::PopItemWidth();
			if (dialog.askStore) {
				ImGui::Checkbox(("Store##Dialog" + ofToString(dialog.token)).c_str(), &dialogStore);
			}
			if (ImGui::Button(("Login##Dialog" + ofToString(dialog.token)).c_str(), ImVec2(dualActionButtonWidth, 0.0f))) {
				if (player.postDialogLogin(dialog.token, dialogUsername, dialogPassword, dialogStore)) {
					dialogPassword.clear();
				}
			}
			ImGui::SameLine(0.0f, buttonSpacing);
			if (ImGui::Button(("Cancel##Dialog" + ofToString(dialog.token)).c_str(), ImVec2(dualActionButtonWidth, 0.0f))) {
				player.dismissDialog(dialog.token);
			}
		} else if (dialog.type == ofxVlc4::DialogType::Question) {
			if (!dialog.action1Label.empty()) {
				if (ImGui::Button((dialog.action1Label + "##Dialog" + ofToString(dialog.token) + "_1").c_str(), ImVec2(dualActionButtonWidth, 0.0f))) {
					player.postDialogAction(dialog.token, 1);
				}
				if (!dialog.action2Label.empty() || dialog.cancellable) {
					ImGui::SameLine(0.0f, buttonSpacing);
				}
			}
			if (!dialog.action2Label.empty()) {
				if (ImGui::Button((dialog.action2Label + "##Dialog" + ofToString(dialog.token) + "_2").c_str(), ImVec2(dualActionButtonWidth, 0.0f))) {
					player.postDialogAction(dialog.token, 2);
				}
				if (dialog.cancellable) {
					ImGui::SameLine(0.0f, buttonSpacing);
				}
			}
			if (dialog.cancellable) {
				const std::string cancelLabel = dialog.cancelLabel.empty() ? "Cancel" : dialog.cancelLabel;
				if (ImGui::Button((cancelLabel + "##Dialog" + ofToString(dialog.token) + "_c").c_str(), ImVec2(dualActionButtonWidth, 0.0f))) {
					player.dismissDialog(dialog.token);
				}
			}
		} else {
			if (!dialog.progressIndeterminate) {
				ImGui::ProgressBar(dialog.progressPosition, ImVec2(-1.0f, 0.0f));
			}
			if (dialog.cancellable && ImGui::Button(("Cancel##Dialog" + ofToString(dialog.token) + "_p").c_str(), ImVec2(singleActionButtonWidth, 0.0f))) {
				player.dismissDialog(dialog.token);
			}
		}

		if (dialogIndex + 1 < activeDialogs.size()) {
			ImGui::Separator();
		}
	}

	ofVlcPlayer4GuiControls::endSectionSubMenu(MenuContentPolicy::Leaf);
}

void ofVlcPlayer4GuiMedia::resetMetadataEditor() {
	metadataPath.clear();
	metadataLoadedFromPlayer = false;
	editMetaTitle.clear();
	editMetaArtist.clear();
	editMetaAlbum.clear();
	metaExtraName.clear();
	metaExtraValue.clear();
	selectedMetaExtraIndex = 0;
}

void ofVlcPlayer4GuiMedia::syncMetadataEditor(
	ofxVlc4 & player,
	const std::string & currentMediaId,
	bool hasCurrentMedia) {
	if (!hasCurrentMedia) {
		resetMetadataEditor();
		return;
	}

	if (metadataLoadedFromPlayer && metadataPath == currentMediaId) {
		return;
	}

	editMetaTitle = player.getCurrentMediaMeta(ofxVlc4::MediaMetaField::Title);
	editMetaArtist = player.getCurrentMediaMeta(ofxVlc4::MediaMetaField::Artist);
	editMetaAlbum = player.getCurrentMediaMeta(ofxVlc4::MediaMetaField::Album);
	metaExtraName.clear();
	metaExtraValue.clear();
	selectedMetaExtraIndex = 0;
	metadataPath = currentMediaId;
	metadataLoadedFromPlayer = true;
}

void ofVlcPlayer4GuiMedia::syncLibVlcLogFilePath(ofxVlc4 & player) {
	syncEditableString(libVlcLogFilePath, libVlcLogFilePathLoaded, player.getLibVlcLogFilePath());
}

void ofVlcPlayer4GuiMedia::setCustomSubtitleCallbacks(
	std::function<bool(const std::string &)> loadCallback,
	std::function<void()> clearCallback,
	std::function<std::string()> statusCallback,
	std::function<std::vector<std::string>()> fontLabelsCallback,
	std::function<int()> selectedFontIndexCallback,
	std::function<void(int)> setFontIndexCallback) {
	loadCustomSubtitleCallback = std::move(loadCallback);
	clearCustomSubtitleCallback = std::move(clearCallback);
	customSubtitleStatusCallback = std::move(statusCallback);
	customSubtitleFontLabelsCallback = std::move(fontLabelsCallback);
	customSubtitleSelectedFontIndexCallback = std::move(selectedFontIndexCallback);
	customSubtitleSetFontIndexCallback = std::move(setFontIndexCallback);
}
