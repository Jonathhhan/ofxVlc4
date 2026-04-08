#include "ofxVlc4.h"
#include "audio/ofxVlc4Audio.h"
#include "media/MediaLibrary.h"
#include "ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "video/ofxVlc4Video.h"
#include "support/ofxVlc4Utils.h"
#include "core/VlcCoreSession.h"
#include "core/VlcEventRouter.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>

using ofxVlc4Utils::clearAllocatedFbo;
using ofxVlc4Utils::fallbackIndexedLabel;
using ofxVlc4Utils::formatProgramName;
using ofxVlc4Utils::isUri;
using ofxVlc4Utils::isStoppedOrIdleState;
using ofxVlc4Utils::isTransientPlaybackState;
using ofxVlc4Utils::mediaLabelForPath;
using ofxVlc4Utils::normalizeOptionalPath;
using ofxVlc4Utils::sanitizeFileStem;
using ofxVlc4Utils::trimWhitespace;

namespace {

constexpr size_t kLibVlcLogCapacity = 200;
constexpr const char * kMediaLogChannel = "ofxVlc4";

std::string defaultLibVlcLogFilePath() {
	return normalizeOptionalPath(ofToDataPath("logs/ofxVlc4-libvlc.log", true));
}

std::string formatLibVlcLogMessage(const char * fmt, va_list args) {
	if (!fmt || *fmt == '\0') {
		return "";
	}

	va_list argsCopy;
	va_copy(argsCopy, args);
	const int requiredSize = std::vsnprintf(nullptr, 0, fmt, argsCopy);
	va_end(argsCopy);
	if (requiredSize <= 0) {
		return "";
	}

	std::string message(static_cast<size_t>(requiredSize), '\0');
	va_copy(argsCopy, args);
	std::vsnprintf(message.data(), static_cast<size_t>(requiredSize) + 1, fmt, argsCopy);
	va_end(argsCopy);
	return trimWhitespace(message);
}

std::string mapLibVlcLogToFriendlyError(const std::string & message) {
	const std::string normalized = ofToLower(trimWhitespace(message));
	if (normalized.empty()) {
		return "";
	}

	const bool missingSoundFont =
		normalized.find("sound font file") != std::string::npos &&
		normalized.find("midi synthesis") != std::string::npos;
	const bool midiDecodeFailure =
		normalized.find("could not decode the format \"midi\"") != std::string::npos ||
		(normalized.find("midi audio") != std::string::npos &&
		 normalized.find("could not decode") != std::string::npos);

	if (missingSoundFont || midiDecodeFailure) {
		return "MIDI playback requires a configured sound font (.sf2/.sf3). "
			   "Set one in VLC Preferences > Input / Codecs > Audio codecs > FluidSynth.";
	}

	return "";
}

bool hasLibVlcLogFilePath(const std::string & path);
ofxVlc4::LibVlcLogEntry toPublicLogEntry(const VlcCoreLogEntry & entry);
std::string mediaDiscovererDisplayLabel(const ofxVlc4::MediaDiscovererInfo & discoverer);
ofxVlc4::WatchTimeInfo buildWatchTimeInfoSnapshot(
	const libvlc_media_player_time_point_t & point,
	bool enabled,
	bool registered,
	bool available,
	bool paused,
	bool seeking,
	int64_t minPeriodUs,
	int64_t pausedSystemDateUs,
	ofxVlc4::WatchTimeEventType eventType,
	uint64_t sequence,
	bool interpolate);
std::string formatPlaybackTimecodeValue(int64_t timeUs, double fps);

std::string rendererDisplayLabel(const ofxVlc4::RendererInfo & renderer) {
	std::string label = trimWhitespace(renderer.name);
	if (label.empty()) {
		label = "Renderer";
	}

	const std::string type = trimWhitespace(renderer.type);
	if (!type.empty()) {
		label += " (" + type + ")";
	}

	return label;
}

std::string rendererStableId(
	const std::string & discovererName,
	const std::string & rendererName,
	const std::string & rendererType,
	const std::string & iconUri,
	int flags) {
	return discovererName + "|" + rendererType + "|" + rendererName + "|" + iconUri + "|" + ofToString(flags);
}

const std::set<std::string> & defaultMediaExtensions() {
	static const std::set<std::string> extensions = {
		".wav", ".mp3", ".flac", ".ogg", ".opus",
		".m4a", ".aac", ".aiff", ".wma", ".mid", ".midi",
		".mp4", ".mov", ".mkv", ".avi", ".wmv", ".asf",
		".webm", ".m4v", ".mpg", ".mpeg", ".ts", ".mts",
		".m2ts", ".m2v", ".vob", ".ogv", ".3gp", ".m3u8",
		".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff"
	};
	return extensions;
}

std::string mediaExtensionForPath(const std::string & absolutePath) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(absolutePath));
	return extension.empty() ? "" : ("." + extension);
}

bool isImageExtension(const std::string & absolutePath) {
	const std::string extension = mediaExtensionForPath(absolutePath);
	return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp";
}

bool tryLoadImageDimensions(const std::string & absolutePath, int & width, int & height) {
	ofImage image;
	if (!image.load(absolutePath) || !image.isAllocated()) {
		return false;
	}

	width = image.getWidth();
	height = image.getHeight();
	return true;
}

bool supportsVlcImageInterop(const std::string & absolutePath) {
	if (!isImageExtension(absolutePath)) {
		return true;
	}

	int imageWidth = 0;
	int imageHeight = 0;
	if (!tryLoadImageDimensions(absolutePath, imageWidth, imageHeight)) {
		ofLogWarning(kMediaLogChannel) << "Playlist image could not be inspected: " << absolutePath;
		return false;
	}
	if (imageWidth <= 1 || imageHeight <= 1) {
		ofLogWarning(kMediaLogChannel)
			<< "Rejected playlist image for VLC playback because dimensions are too small for reliable GL interop ("
			<< imageWidth << "x" << imageHeight << "): " << absolutePath;
		return false;
	}

	return true;
}

bool isSupportedMediaPath(
	const std::string & absolutePath,
	const std::set<std::string> & activeExtensions) {
	const std::string extension = mediaExtensionForPath(absolutePath);
	return !extension.empty() &&
		activeExtensions.count(extension) > 0 &&
		supportsVlcImageInterop(absolutePath);
}

std::string percentEncodeUriPath(const std::string & value) {
	std::ostringstream stream;
	stream << std::uppercase << std::hex;
	for (const unsigned char ch : value) {
		if (std::isalnum(ch) || ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
			stream << static_cast<char>(ch);
		} else {
			stream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
		}
	}
	return stream.str();
}

std::string pathToFileUri(const std::string & value) {
	std::error_code ec;
	std::filesystem::path absolutePath = std::filesystem::absolute(std::filesystem::path(value), ec);
	if (ec) {
		absolutePath = std::filesystem::path(value);
	}

	std::string genericPath = absolutePath.generic_string();
	if (!genericPath.empty() && genericPath.front() != '/') {
		genericPath.insert(genericPath.begin(), '/');
	}

	return "file://" + percentEncodeUriPath(genericPath);
}

std::string normalizeMediaSlaveUri(const std::string & value) {
	const std::string trimmedValue = trimWhitespace(value);
	if (trimmedValue.empty()) {
		return "";
	}
	if (isUri(trimmedValue)) {
		return trimmedValue;
	}
	return pathToFileUri(trimmedValue);
}

libvlc_media_slave_type_t toLibvlcMediaSlaveType(ofxVlc4::MediaSlaveType type) {
	switch (type) {
	case ofxVlc4::MediaSlaveType::Audio:
		return libvlc_media_slave_type_audio;
	case ofxVlc4::MediaSlaveType::Subtitle:
	default:
		return libvlc_media_slave_type_subtitle;
	}
}

const char * mediaSlaveTypeLabel(ofxVlc4::MediaSlaveType type) {
	switch (type) {
	case ofxVlc4::MediaSlaveType::Audio:
		return "audio";
	case ofxVlc4::MediaSlaveType::Subtitle:
	default:
		return "subtitle";
	}
}

std::string describeTrackLabel(const ofxVlc4::MediaTrackInfo & track) {
	std::string label = trimWhitespace(track.name);
	if (label.empty()) {
		label = trimWhitespace(track.description);
	}
	if (label.empty()) {
		label = trimWhitespace(track.language);
	}
	if (label.empty()) {
		label = trimWhitespace(track.id);
	}
	if (label.empty()) {
		label = "Track";
	}

	const std::string language = trimWhitespace(track.language);
	if (!language.empty() && language != label) {
		label += " (" + language + ")";
	}
	return label;
}

libvlc_media_t * retainCurrentPlayerMedia(libvlc_media_player_t * mediaPlayer) {
	return mediaPlayer ? libvlc_media_player_get_media(mediaPlayer) : nullptr;
}

libvlc_media_discoverer_category_t toLibvlcMediaDiscovererCategory(ofxVlc4::MediaDiscovererCategory category) {
	switch (category) {
	case ofxVlc4::MediaDiscovererCategory::Devices:
		return libvlc_media_discoverer_devices;
	case ofxVlc4::MediaDiscovererCategory::Podcasts:
		return libvlc_media_discoverer_podcasts;
	case ofxVlc4::MediaDiscovererCategory::LocalDirs:
		return libvlc_media_discoverer_localdirs;
	case ofxVlc4::MediaDiscovererCategory::Lan:
	default:
		return libvlc_media_discoverer_lan;
	}
}

ofxVlc4::MediaDiscovererCategory toMediaDiscovererCategory(libvlc_media_discoverer_category_t category) {
	switch (category) {
	case libvlc_media_discoverer_devices:
		return ofxVlc4::MediaDiscovererCategory::Devices;
	case libvlc_media_discoverer_podcasts:
		return ofxVlc4::MediaDiscovererCategory::Podcasts;
	case libvlc_media_discoverer_localdirs:
		return ofxVlc4::MediaDiscovererCategory::LocalDirs;
	case libvlc_media_discoverer_lan:
	default:
		return ofxVlc4::MediaDiscovererCategory::Lan;
	}
}

std::string mediaDisplayName(libvlc_media_t * media) {
	if (!media) {
		return "";
	}

	char * rawTitle = libvlc_media_get_meta(media, libvlc_meta_Title);
	if (rawTitle) {
		const std::string title = trimWhitespace(rawTitle);
		libvlc_free(rawTitle);
		if (!title.empty()) {
			return title;
		}
	}

	char * rawMrl = libvlc_media_get_mrl(media);
	if (!rawMrl) {
		return "";
	}

	const std::string mrl = trimWhitespace(rawMrl);
	libvlc_free(rawMrl);
	return mediaLabelForPath(mrl);
}

ofxVlc4::DialogQuestionSeverity toDialogQuestionSeverity(libvlc_dialog_question_type type) {
	switch (type) {
	case LIBVLC_DIALOG_QUESTION_WARNING:
		return ofxVlc4::DialogQuestionSeverity::Warning;
	case LIBVLC_DIALOG_QUESTION_CRITICAL:
		return ofxVlc4::DialogQuestionSeverity::Critical;
	case LIBVLC_DIALOG_QUESTION_NORMAL:
	default:
		return ofxVlc4::DialogQuestionSeverity::Normal;
	}
}

ofxVlc4::AbLoopInfo::State toAbLoopState(libvlc_abloop_t state) {
	switch (state) {
	case libvlc_abloop_a:
		return ofxVlc4::AbLoopInfo::State::A;
	case libvlc_abloop_b:
		return ofxVlc4::AbLoopInfo::State::B;
	case libvlc_abloop_none:
	default:
		return ofxVlc4::AbLoopInfo::State::None;
	}
}

std::string fourccToString(uint32_t value) {
	if (value == 0) {
		return "";
	}

	char chars[5] = {
		static_cast<char>(value & 0xFF),
		static_cast<char>((value >> 8) & 0xFF),
		static_cast<char>((value >> 16) & 0xFF),
		static_cast<char>((value >> 24) & 0xFF),
		'\0'
	};

	bool printable = true;
	for (int i = 0; i < 4; ++i) {
		if (chars[i] == '\0' || !std::isprint(static_cast<unsigned char>(chars[i]))) {
			printable = false;
			break;
		}
	}

	if (printable) {
		return std::string(chars, 4);
	}

	std::ostringstream stream;
	stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
	return stream.str();
}

std::string codecDescriptionForTrack(libvlc_track_type_t type, uint32_t codec) {
	if (codec == 0) {
		return "";
	}

	const char * rawDescription = libvlc_media_get_codec_description(type, codec);
	if (!rawDescription) {
		return "";
	}

	return trimWhitespace(rawDescription);
}

std::set<std::string> collectSelectedTrackIds(libvlc_media_player_t * mediaPlayer, libvlc_track_type_t type) {
	std::set<std::string> selectedTrackIds;
	if (!mediaPlayer) {
		return selectedTrackIds;
	}

	libvlc_media_tracklist_t * selectedTrackList = libvlc_media_player_get_tracklist(mediaPlayer, type, false);
	if (!selectedTrackList) {
		return selectedTrackIds;
	}

	const size_t trackCount = libvlc_media_tracklist_count(selectedTrackList);
	for (size_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
		libvlc_media_track_t * track = libvlc_media_tracklist_at(selectedTrackList, trackIndex);
		if (!track || !track->selected || !track->psz_id || track->psz_id[0] == '\0') {
			continue;
		}
		selectedTrackIds.insert(track->psz_id);
	}

	libvlc_media_tracklist_delete(selectedTrackList);
	return selectedTrackIds;
}

libvlc_media_parse_flag_t toLibvlcMediaParseFlags(const ofxVlc4::MediaParseOptions & options) {
	unsigned flags = 0;
	if (options.parseLocal) {
		flags |= libvlc_media_parse_local;
	}
	if (options.parseNetwork) {
		flags |= libvlc_media_parse_network;
	}
	if (options.forced) {
		flags |= libvlc_media_parse_forced;
	}
	if (options.fetchLocal) {
		flags |= libvlc_media_fetch_local;
	}
	if (options.fetchNetwork) {
		flags |= libvlc_media_fetch_network;
	}
	if (options.doInteract) {
		flags |= libvlc_media_do_interact;
	}
	return static_cast<libvlc_media_parse_flag_t>(flags);
}

libvlc_media_player_role_t toLibvlcMediaPlayerRole(ofxVlc4::MediaPlayerRole role) {
	switch (role) {
	case ofxVlc4::MediaPlayerRole::Music:
		return libvlc_role_Music;
	case ofxVlc4::MediaPlayerRole::Video:
		return libvlc_role_Video;
	case ofxVlc4::MediaPlayerRole::Communication:
		return libvlc_role_Communication;
	case ofxVlc4::MediaPlayerRole::Game:
		return libvlc_role_Game;
	case ofxVlc4::MediaPlayerRole::Notification:
		return libvlc_role_Notification;
	case ofxVlc4::MediaPlayerRole::Animation:
		return libvlc_role_Animation;
	case ofxVlc4::MediaPlayerRole::Production:
		return libvlc_role_Production;
	case ofxVlc4::MediaPlayerRole::Accessibility:
		return libvlc_role_Accessibility;
	case ofxVlc4::MediaPlayerRole::Test:
		return libvlc_role_Test;
	case ofxVlc4::MediaPlayerRole::None:
	default:
		return libvlc_role_None;
	}
}

ofxVlc4::MediaPlayerRole toMediaPlayerRole(int role) {
	switch (static_cast<libvlc_media_player_role_t>(role)) {
	case libvlc_role_Music:
		return ofxVlc4::MediaPlayerRole::Music;
	case libvlc_role_Video:
		return ofxVlc4::MediaPlayerRole::Video;
	case libvlc_role_Communication:
		return ofxVlc4::MediaPlayerRole::Communication;
	case libvlc_role_Game:
		return ofxVlc4::MediaPlayerRole::Game;
	case libvlc_role_Notification:
		return ofxVlc4::MediaPlayerRole::Notification;
	case libvlc_role_Animation:
		return ofxVlc4::MediaPlayerRole::Animation;
	case libvlc_role_Production:
		return ofxVlc4::MediaPlayerRole::Production;
	case libvlc_role_Accessibility:
		return ofxVlc4::MediaPlayerRole::Accessibility;
	case libvlc_role_Test:
		return ofxVlc4::MediaPlayerRole::Test;
	case libvlc_role_None:
	default:
		return ofxVlc4::MediaPlayerRole::None;
	}
}

libvlc_navigate_mode_t toLibvlcNavigateMode(ofxVlc4::NavigationMode mode) {
	switch (mode) {
	case ofxVlc4::NavigationMode::Up:
		return libvlc_navigate_up;
	case ofxVlc4::NavigationMode::Down:
		return libvlc_navigate_down;
	case ofxVlc4::NavigationMode::Left:
		return libvlc_navigate_left;
	case ofxVlc4::NavigationMode::Right:
		return libvlc_navigate_right;
	case ofxVlc4::NavigationMode::Popup:
		return libvlc_navigate_popup;
	case ofxVlc4::NavigationMode::Activate:
	default:
		return libvlc_navigate_activate;
	}
}

}

ofxVlc4::MediaComponent::MediaComponent(ofxVlc4 & owner)
	: owner(owner) {}

void ofxVlc4::applyCurrentMediaPlayerSettings() {
	audioComponent->applyCurrentPlayerSettings();
	videoComponent->applyCurrentPlayerSettings();
	mediaComponent->applyCurrentPlayerSettings();
}

MediaLibrary & ofxVlc4::MediaComponent::mediaLibrary() const {
	return *owner.mediaLibraryController;
}

ofxVlc4::AudioComponent & ofxVlc4::MediaComponent::audio() const {
	return *owner.audioComponent;
}

ofxVlc4::VideoComponent & ofxVlc4::MediaComponent::video() const {
	return *owner.videoComponent;
}

PlaybackController & ofxVlc4::MediaComponent::playback() const {
	return *owner.playbackController;
}

void ofxVlc4::MediaComponent::applyCurrentPlayerSettings() {
	applyMediaPlayerRole();
	applyNativeRecording();
}
bool ofxVlc4::isSupportedMediaFile(const ofFile & file, const std::set<std::string> * extensions) const {
	if (!file.exists() || !file.isFile()) {
		return false;
	}

	const std::set<std::string> & activeExtensions = extensions ? *extensions : defaultMediaExtensions();
	const std::string absolutePath = file.getAbsolutePath();
	return isSupportedMediaPath(absolutePath, activeExtensions);
}

void ofxVlc4::MediaComponent::applySafeLoadedMediaPlayerSettings() {
	audio().applyCurrentPlayerSettings();
	video().applyCurrentPlayerSettings();
	applyCurrentPlayerSettings();
	applySelectedRenderer();
}

void ofxVlc4::MediaComponent::prepareStartupMediaResources() {
	prepareStartupPlaybackState();
	audio().prepareStartupAudioResources();
	video().prepareStartupVideoResources();
}

bool ofxVlc4::MediaComponent::reapplyCurrentMediaForFilterChainChange(const std::string & label) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	libvlc_media_t * currentMedia = owner.sessionMedia();
	if (!player || !currentMedia || playback().hasPendingManualStopEvents()) {
		return false;
	}

	const int64_t resumeTimeMs = std::max<int64_t>(0, playback().getTime());
	const bool wasPlaying = libvlc_media_player_is_playing(player);
	const bool wasPaused = libvlc_media_player_get_state(player) == libvlc_Paused;

	bool reloaded = false;
	const int current = getCurrentIndex();
	if (current >= 0) {
		reloaded = loadMediaAtIndex(current);
	} else if (playback().hasActiveDirectMedia()) {
		reloaded = playback().reloadActiveDirectMedia();
	}

	if (!reloaded) {
		return false;
	}

	if (resumeTimeMs > 0 && libvlc_media_player_is_seekable(player)) {
		libvlc_media_player_set_time(player, resumeTimeMs, true);
	}

	if (wasPlaying || wasPaused) {
		audio().applyEqualizerSettings();
		audio().clearPendingEqualizerApplyOnPlay();
		libvlc_media_player_play(player);
		if (wasPaused) {
			libvlc_media_player_set_pause(player, 1);
		}
	}

	owner.setStatus(label + " filter chain applied to current media.");
	owner.logNotice(label + " filter chain applied to current media.");
	return true;
}

void ofxVlc4::prepareStartupPlaybackState() {
	mediaComponent->prepareStartupPlaybackState();
}

void ofxVlc4::prepareStartupMediaResources() {
	mediaComponent->prepareStartupMediaResources();
}

void ofxVlc4::applySafeLoadedMediaPlayerSettings() {
	mediaComponent->applySafeLoadedMediaPlayerSettings();
}

bool ofxVlc4::reapplyCurrentMediaForFilterChainChange(const std::string & label) {
	return mediaComponent->reapplyCurrentMediaForFilterChainChange(label);
}

bool ofxVlc4::loadMediaSource(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool parseAsNetwork) {
	return mediaComponent->loadMediaSource(source, isLocation, options, parseAsNetwork);
}

bool ofxVlc4::loadMediaAtIndex(int index) {
	return mediaComponent->loadMediaAtIndex(index);
}

void ofxVlc4::addToPlaylistInternal(const std::string & path, bool preloadMetadata) {
	mediaComponent->addToPlaylistInternal(path, preloadMetadata);
}

void ofxVlc4::addToPlaylist(const std::string & path) {
	mediaComponent->addToPlaylist(path);
}

int ofxVlc4::addPathToPlaylist(const std::string & path) {
	return mediaComponent->addPathToPlaylist(path);
}

int ofxVlc4::addPathToPlaylist(const std::string & path, std::initializer_list<std::string> extensions) {
	return mediaComponent->addPathToPlaylist(path, extensions);
}

void ofxVlc4::clearPlaylist() {
	mediaComponent->clearPlaylist();
}

void ofxVlc4::removeFromPlaylist(int index) {
	mediaComponent->removeFromPlaylist(index);
}

void ofxVlc4::movePlaylistItem(int fromIndex, int toIndex) {
	mediaComponent->movePlaylistItem(fromIndex, toIndex);
}

void ofxVlc4::movePlaylistItems(const std::vector<int> & fromIndices, int toIndex) {
	mediaComponent->movePlaylistItems(fromIndices, toIndex);
}

std::vector<std::pair<std::string, std::string>> ofxVlc4::buildMetadataForMedia(libvlc_media_t * sourceMedia) const {
	return mediaLibraryController->buildMetadataForMedia(sourceMedia);
}

bool ofxVlc4::MediaComponent::loadMediaSource(
	const std::string & source,
	bool isLocation,
	const std::vector<std::string> & options,
	bool parseAsNetwork) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!player || trimWhitespace(source).empty()) {
		return false;
	}

	clearCurrentMedia();

	owner.media = isLocation
		? libvlc_media_new_location(source.c_str())
		: libvlc_media_new_path(source.c_str());
	owner.coreSession->setMedia(owner.media);
	owner.syncLegacyStateFromCoreSession();
	if (!owner.media) {
		owner.setError("Failed to create media source.");
		return false;
	}

	for (const auto & option : options) {
		const std::string trimmedOption = trimWhitespace(option);
		if (!trimmedOption.empty()) {
			libvlc_media_add_option(owner.media, trimmedOption.c_str());
		}
	}

	if (!owner.audioFilterChain.empty()) {
		libvlc_media_add_option(owner.media, (":audio-filter=" + owner.audioFilterChain).c_str());
	}
	if (!owner.videoFilterChain.empty() && owner.canApplyNativeVideoFilters()) {
		libvlc_media_add_option(owner.media, (":video-filter=" + owner.videoFilterChain).c_str());
	}

	owner.mediaEventManager = libvlc_media_event_manager(owner.media);
	owner.coreSession->setMediaEvents(owner.mediaEventManager);
	owner.syncLegacyStateFromCoreSession();
	if (owner.mediaEventManager && owner.coreSession && owner.eventRouter) {
		owner.coreSession->attachMediaEvents(owner.eventRouter.get(), VlcEventRouter::vlcMediaEventStatic);
	}

	if (instance) {
		libvlc_media_parse_flag_t parseFlags = libvlc_media_parse_forced;
		parseFlags = static_cast<libvlc_media_parse_flag_t>(
			parseFlags |
			(parseAsNetwork ? libvlc_media_parse_network : libvlc_media_parse_local) |
			libvlc_media_do_interact);
		if (libvlc_media_parse_request(instance, owner.media, parseFlags, 1000) != 0) {
			owner.logNotice("Media parse request failed: " + source);
		}
	}

	libvlc_media_player_set_media(player, owner.media);
	applySafeLoadedMediaPlayerSettings();
	prepareStartupMediaResources();
	return true;
}

bool ofxVlc4::MediaComponent::loadMediaAtIndex(int index) {
	if (!owner.mediaPlayer) {
		return false;
	}
	if (index < 0 || index >= static_cast<int>(mediaLibrary().getPlaylistSize())) {
		return false;
	}

	const std::string path = mediaLibrary().getPathAtIndex(index);
	if (!loadMediaSource(path, isUri(path), {}, isUri(path))) {
		owner.setError("Failed to create media for playlist item.");
		return false;
	}

	return true;
}

void ofxVlc4::MediaComponent::clearCurrentMedia(bool clearVideoResources) {
	prepareForMediaDetach();
	libvlc_media_player_t * player = owner.sessionPlayer();
	libvlc_media_t * currentMedia = owner.sessionMedia();

	if (player) {
		libvlc_media_player_set_media(player, nullptr);
	}

	if (owner.mediaEventManager) {
		if (owner.coreSession && owner.eventRouter) {
			owner.coreSession->detachMediaEvents(owner.eventRouter.get(), VlcEventRouter::vlcMediaEventStatic);
			owner.syncLegacyStateFromCoreSession();
		} else {
			owner.mediaEventManager = nullptr;
		}
	}

	if (currentMedia) {
		libvlc_media_release(currentMedia);
		owner.media = nullptr;
		if (owner.coreSession) {
			owner.coreSession->setMedia(nullptr);
		}
	}

	{
		std::lock_guard<std::mutex> lock(owner.videoMutex);
		owner.renderWidth.store(0);
		owner.renderHeight.store(0);
		owner.videoWidth.store(0);
		owner.videoHeight.store(0);
		owner.pixelAspectNumerator.store(1);
		owner.pixelAspectDenominator.store(1);
		owner.pendingRenderWidth.store(0);
		owner.pendingRenderHeight.store(0);
		owner.pendingResize.store(false);
		owner.displayAspectRatio.store(1.0f);
		owner.isVideoLoaded.store(false);
		owner.hasReceivedVideoFrame.store(false);
		owner.exposedTextureDirty.store(true);
		owner.vlcFramebufferAttachmentDirty.store(true);
		if (clearVideoResources) {
			clearAllocatedFbo(owner.exposedTextureFbo);
		}
	}

	playback().setAudioPauseSignaled(false);
	owner.isAudioReady.store(false);
	audio().clearAudioPtsState();
	owner.startupPlaybackStatePrepared.store(false);
	{
		std::lock_guard<std::mutex> lock(owner.audioStateMutex);
		owner.audioStateInfo.trackCount = 0;
		owner.audioStateInfo.tracksAvailable = false;
	}
	owner.cachedVideoTrackCount.store(0);
	owner.cachedVideoTrackFps.store(0.0);
	resetSubtitleStateInfo();
	resetNavigationStateInfo();
	owner.syncLegacyStateFromCoreSession();
}

void ofxVlc4::MediaComponent::applyNativeRecording() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		updateNativeRecordingFailureState("No media player available.");
		return;
	}

	if (!owner.nativeRecordingEnabled) {
		if (owner.nativeRecordingActive.load()) {
			libvlc_media_player_record(player, false, nullptr);
			owner.nativeRecordingActive.store(false);
			std::lock_guard<std::mutex> lock(owner.playbackStateMutex);
			setNativeRecordingStatusLocked("Recording stop requested.", "");
		}
		return;
	}

	if (owner.nativeRecordingActive.load()) {
		return;
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	if (state != libvlc_Playing && state != libvlc_Paused) {
		std::lock_guard<std::mutex> lock(owner.playbackStateMutex);
		setNativeRecordingStatusLocked("Waiting for playback before starting native recording.", "");
		return;
	}

	const std::string resolvedDirectory = normalizeOptionalPath(owner.nativeRecordDirectory);
	const char * directory = resolvedDirectory.empty() ? nullptr : resolvedDirectory.c_str();
	libvlc_media_player_record(player, true, directory);
	owner.nativeRecordingActive.store(true);
	{
		std::lock_guard<std::mutex> lock(owner.playbackStateMutex);
		setNativeRecordingStatusLocked(
			resolvedDirectory.empty()
				? "Recording requested."
				: ("Recording requested in " + resolvedDirectory + "."),
			"");
	}
}

bool ofxVlc4::MediaComponent::isNativeRecordingEnabled() const {
	return owner.nativeRecordingEnabled;
}

void ofxVlc4::MediaComponent::setNativeRecordingEnabled(bool enabled) {
	if (owner.nativeRecordingEnabled == enabled) {
		return;
	}

	setNativeRecordingEnabledValue(enabled);
	applyNativeRecording();
	owner.setStatus(std::string("Native VLC recording ") + (owner.nativeRecordingEnabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::MediaComponent::getNativeRecordDirectory() const {
	return owner.nativeRecordDirectory;
}

void ofxVlc4::MediaComponent::setNativeRecordDirectory(const std::string & directory) {
	if (owner.nativeRecordDirectory == directory) {
		return;
	}

	setNativeRecordDirectoryValue(directory);
	if (!owner.nativeRecordingEnabled || !owner.nativeRecordingActive.load()) {
		return;
	}

	ofxVlc4::logWarning("Native VLC recording directory changes apply on the next recording start.");
}

bool ofxVlc4::isNativeRecordingEnabled() const {
	return mediaComponent->isNativeRecordingEnabled();
}

void ofxVlc4::setNativeRecordingEnabled(bool enabled) {
	mediaComponent->setNativeRecordingEnabled(enabled);
}

std::string ofxVlc4::getNativeRecordDirectory() const {
	return mediaComponent->getNativeRecordDirectory();
}

void ofxVlc4::setNativeRecordDirectory(const std::string & directory) {
	mediaComponent->setNativeRecordDirectory(directory);
}

void ofxVlc4::MediaComponent::dismissAllDialogs() {
	std::vector<std::uintptr_t> tokens;
	{
		std::lock_guard<std::mutex> lock(owner.dialogMutex);
		tokens = getActiveDialogTokensLocked();
	}

	for (std::uintptr_t token : tokens) {
		dismissDialog(token);
	}
}

bool ofxVlc4::MediaComponent::postDialogLogin(
	std::uintptr_t token,
	const std::string & username,
	const std::string & password,
	bool store) {
	libvlc_dialog_id * dialogId = reinterpret_cast<libvlc_dialog_id *>(token);
	if (!dialogId || trimWhitespace(username).empty()) {
		return false;
	}

	if (libvlc_dialog_post_login(dialogId, username.c_str(), password.c_str(), store) != 0) {
		return false;
	}

	removeDialog(token);
	owner.setStatus("Dialog login sent.");
	return true;
}

bool ofxVlc4::MediaComponent::postDialogAction(std::uintptr_t token, int action) {
	libvlc_dialog_id * dialogId = reinterpret_cast<libvlc_dialog_id *>(token);
	if (!dialogId || action < 1 || action > 2) {
		return false;
	}

	if (libvlc_dialog_post_action(dialogId, action) != 0) {
		return false;
	}

	removeDialog(token);
	owner.setStatus("Dialog action sent.");
	return true;
}

bool ofxVlc4::MediaComponent::dismissDialog(std::uintptr_t token) {
	libvlc_dialog_id * dialogId = reinterpret_cast<libvlc_dialog_id *>(token);
	if (!dialogId) {
		return false;
	}

	if (libvlc_dialog_dismiss(dialogId) != 0) {
		return false;
	}

	removeDialog(token);
	return true;
}

void ofxVlc4::MediaComponent::upsertDialog(const DialogInfo & dialog) {
	std::lock_guard<std::mutex> lock(owner.dialogMutex);
	upsertActiveDialogLocked(dialog);
}

void ofxVlc4::MediaComponent::removeDialog(std::uintptr_t token) {
	std::lock_guard<std::mutex> lock(owner.dialogMutex);
	removeActiveDialogLocked(token);
}

const std::string & ofxVlc4::MediaComponent::getLastStatusMessage() const {
	return owner.lastStatusMessage;
}

const std::string & ofxVlc4::MediaComponent::getLastErrorMessage() const {
	return owner.lastErrorMessage;
}

void ofxVlc4::MediaComponent::clearLastMessages() {
	owner.lastStatusMessage.clear();
	owner.lastErrorMessage.clear();
}

std::vector<ofxVlc4::DialogInfo> ofxVlc4::MediaComponent::getActiveDialogs() const {
	std::lock_guard<std::mutex> lock(owner.dialogMutex);
	return owner.activeDialogs;
}

ofxVlc4::DialogErrorInfo ofxVlc4::MediaComponent::getLastDialogError() const {
	std::lock_guard<std::mutex> lock(owner.dialogMutex);
	return owner.lastDialogError;
}

void ofxVlc4::MediaComponent::clearLastDialogError() {
	std::lock_guard<std::mutex> lock(owner.dialogMutex);
	clearLastDialogErrorLocked();
}

bool ofxVlc4::MediaComponent::isLibVlcLoggingEnabled() const {
	return owner.coreSession ? owner.coreSession->loggingEnabled() : owner.libVlcLoggingEnabled;
}

void ofxVlc4::MediaComponent::setLibVlcLoggingEnabled(bool enabled) {
	if (isLibVlcLoggingEnabled() == enabled) {
		return;
	}

	setLibVlcLoggingEnabledValue(enabled);
	owner.syncLegacyStateFromCoreSession();
	if (owner.sessionInstance()) {
		applyLibVlcLogging();
	}
	owner.setStatus(std::string("libVLC logging ") + (enabled ? "enabled." : "disabled."));
}

bool ofxVlc4::MediaComponent::isLibVlcLogFileEnabled() const {
	return owner.coreSession ? owner.coreSession->logFileEnabled() : owner.libVlcLogFileEnabled;
}

void ofxVlc4::MediaComponent::setLibVlcLogFileEnabled(bool enabled) {
	std::string resolvedPath = owner.coreSession ? owner.coreSession->logFilePath() : owner.libVlcLogFilePath;
	if (enabled && !hasLibVlcLogFilePath(resolvedPath)) {
		resolvedPath = defaultLibVlcLogFilePath();
		setLibVlcLogFilePathValue(resolvedPath);
	}

	const bool canEnable = hasLibVlcLogFilePath(resolvedPath);
	if (enabled && !canEnable) {
		owner.setError("Could not resolve a libVLC log file path.");
		return;
	}

	if (isLibVlcLogFileEnabled() == enabled) {
		return;
	}

	setLibVlcLogFileEnabledValue(enabled);
	owner.syncLegacyStateFromCoreSession();
	if (owner.sessionInstance()) {
		applyLibVlcLogging();
	}
	owner.setStatus(std::string("libVLC file logging ") + (enabled ? "enabled." : "disabled."));
	if (enabled) {
		owner.logNotice("libVLC log file: " + getLibVlcLogFilePath());
	}
}

std::string ofxVlc4::MediaComponent::getLibVlcLogFilePath() const {
	return owner.coreSession ? owner.coreSession->logFilePath() : owner.libVlcLogFilePath;
}

void ofxVlc4::MediaComponent::setLibVlcLogFilePath(const std::string & path) {
	const std::string normalizedPath = normalizeOptionalPath(path);
	if (getLibVlcLogFilePath() == normalizedPath) {
		return;
	}

	const bool wasFileLoggingEnabled = isLibVlcLogFileEnabled();
	setLibVlcLogFilePathValue(normalizedPath);
	if (owner.libVlcLogFilePath.empty()) {
		setLibVlcLogFileEnabledValue(false);
	}
	owner.syncLegacyStateFromCoreSession();
	if ((wasFileLoggingEnabled || owner.libVlcLogFileEnabled) && owner.sessionInstance()) {
		applyLibVlcLogging();
	}
}

std::vector<ofxVlc4::LibVlcLogEntry> ofxVlc4::MediaComponent::getLibVlcLogEntries() const {
	std::lock_guard<std::mutex> lock(owner.libVlcLogMutex);
	if (!owner.coreSession) {
		return owner.libVlcLogEntries;
	}

	std::vector<LibVlcLogEntry> entries;
	entries.reserve(owner.coreSession->logEntries().size());
	for (const VlcCoreLogEntry & entry : owner.coreSession->logEntries()) {
		entries.push_back(toPublicLogEntry(entry));
	}
	return entries;
}

void ofxVlc4::MediaComponent::clearLibVlcLogEntries() {
	std::lock_guard<std::mutex> lock(owner.libVlcLogMutex);
	owner.libVlcLogEntries.clear();
	if (owner.coreSession) {
		owner.coreSession->clearLogEntries();
	}
}

void ofxVlc4::MediaComponent::applyLibVlcLogging() {
	if (!owner.coreSession) {
		return;
	}

	setLibVlcLoggingEnabledValue(owner.libVlcLoggingEnabled);
	setLibVlcLogFileEnabledValue(owner.libVlcLogFileEnabled);
	setLibVlcLogFilePathValue(owner.libVlcLogFilePath);
	owner.syncLegacyStateFromCoreSession();

	if (!owner.sessionInstance()) {
		return;
	}

	libvlc_log_unset(owner.sessionInstance());
	closeLibVlcLogFile();

	const auto applyBufferedLoggingFallback = [this]() {
		if (owner.libVlcLoggingEnabled) {
			libvlc_log_set(owner.sessionInstance(), ofxVlc4::libVlcLogStatic, &owner);
		}
	};

	if (owner.libVlcLogFileEnabled) {
		const std::string normalizedPath = normalizeOptionalPath(owner.coreSession->logFilePath());
		if (normalizedPath.empty()) {
			ofxVlc4::logWarning("libVLC file logging enabled without a log path.");
			applyBufferedLoggingFallback();
			return;
		}

		std::error_code ec;
		const std::filesystem::path logPath(normalizedPath);
		const std::filesystem::path parentPath = logPath.parent_path();
		if (!parentPath.empty()) {
			std::filesystem::create_directories(parentPath, ec);
			if (ec) {
				ofxVlc4::logWarning("Failed to create libVLC log directory: " + parentPath.string());
				applyBufferedLoggingFallback();
				return;
			}
		}

#ifdef _MSC_VER
		if (fopen_s(&owner.libVlcLogFileHandle, normalizedPath.c_str(), "ab") != 0) {
			owner.libVlcLogFileHandle = nullptr;
		}
#else
		owner.libVlcLogFileHandle = std::fopen(normalizedPath.c_str(), "ab");
#endif
		if (!owner.libVlcLogFileHandle) {
			ofxVlc4::logWarning("Failed to open libVLC log file: " + normalizedPath);
			applyBufferedLoggingFallback();
			return;
		}

		owner.coreSession->setLogFileHandle(owner.libVlcLogFileHandle);
		owner.syncLegacyStateFromCoreSession();
		libvlc_log_set_file(owner.sessionInstance(), owner.libVlcLogFileHandle);
		return;
	}

	applyBufferedLoggingFallback();
}

void ofxVlc4::MediaComponent::closeLibVlcLogFile() {
	if (owner.coreSession) {
		owner.coreSession->closeLogFile();
		owner.syncLegacyStateFromCoreSession();
		return;
	}

	if (owner.libVlcLogFileHandle) {
		std::fflush(owner.libVlcLogFileHandle);
		std::fclose(owner.libVlcLogFileHandle);
		owner.libVlcLogFileHandle = nullptr;
	}
}

void ofxVlc4::MediaComponent::appendLibVlcLog(const LibVlcLogEntry & entry) {
	if (entry.message.empty()) {
		return;
	}

	const std::string friendlyError = mapLibVlcLogToFriendlyError(entry.message);

	std::lock_guard<std::mutex> lock(owner.libVlcLogMutex);
	if (owner.coreSession) {
		VlcCoreLogEntry coreEntry;
		coreEntry.level = entry.level;
		coreEntry.module = entry.module;
		coreEntry.file = entry.file;
		coreEntry.line = entry.line;
		coreEntry.objectName = entry.objectName;
		coreEntry.objectHeader = entry.objectHeader;
		coreEntry.objectId = entry.objectId;
		coreEntry.message = entry.message;
		owner.coreSession->appendLog(coreEntry);
	} else {
		owner.libVlcLogEntries.push_back(entry);
		if (owner.libVlcLogEntries.size() > kLibVlcLogCapacity) {
			const size_t overflow = owner.libVlcLogEntries.size() - kLibVlcLogCapacity;
			owner.libVlcLogEntries.erase(
				owner.libVlcLogEntries.begin(),
				owner.libVlcLogEntries.begin() + overflow);
		}
	}

	if (!friendlyError.empty() && owner.lastErrorMessage != friendlyError) {
		owner.lastErrorMessage = friendlyError;
		owner.lastStatusMessage.clear();
		ofxVlc4::logError(friendlyError);
	}
}

void ofxVlc4::libVlcLogStatic(void * data, int level, const libvlc_log_t * ctx, const char * fmt, va_list args) {
	auto * player = static_cast<ofxVlc4 *>(data);
	if (!player || !ctx) {
		return;
	}

	LibVlcLogEntry entry;
	entry.level = level;
	entry.message = formatLibVlcLogMessage(fmt, args);

	const char * module = nullptr;
	const char * file = nullptr;
	unsigned line = 0;
	libvlc_log_get_context(ctx, &module, &file, &line);
	entry.module = module ? module : "";
	entry.file = file ? file : "";
	entry.line = line;

	const char * objectName = nullptr;
	const char * objectHeader = nullptr;
	uintptr_t objectId = 0;
	libvlc_log_get_object(ctx, &objectName, &objectHeader, &objectId);
	entry.objectName = objectName ? objectName : "";
	entry.objectHeader = objectHeader ? objectHeader : "";
	entry.objectId = objectId;

	player->mediaComponent->appendLibVlcLog(entry);
}

ofxVlc4::MediaPlayerRole ofxVlc4::MediaComponent::getMediaPlayerRole() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return owner.mediaPlayerRole;
	}

	return toMediaPlayerRole(libvlc_media_player_get_role(player));
}

void ofxVlc4::MediaComponent::setMediaPlayerRole(MediaPlayerRole role) {
	if (owner.mediaPlayerRole == role) {
		return;
	}

	owner.mediaPlayerRole = role;
	applyMediaPlayerRole();
}

bool ofxVlc4::MediaComponent::isWatchTimeEnabled() const {
	return owner.watchTimeEnabled;
}

void ofxVlc4::MediaComponent::setWatchTimeEnabled(bool enabled) {
	if (owner.watchTimeEnabled == enabled) {
		return;
	}

	owner.watchTimeEnabled = enabled;
	applyWatchTimeObserver();
}

int64_t ofxVlc4::MediaComponent::getWatchTimeMinPeriodUs() const {
	return owner.watchTimeMinPeriodUs;
}

void ofxVlc4::MediaComponent::setWatchTimeMinPeriodUs(int64_t minPeriodUs) {
	const int64_t clampedPeriodUs = std::max<int64_t>(0, minPeriodUs);
	if (owner.watchTimeMinPeriodUs == clampedPeriodUs) {
		return;
	}

	owner.watchTimeMinPeriodUs = clampedPeriodUs;
	if (owner.watchTimeEnabled) {
		applyWatchTimeObserver();
	}
}

void ofxVlc4::MediaComponent::clearWatchTimeState() {
	std::lock_guard<std::mutex> lock(owner.watchTimeMutex);
	owner.watchTimePointAvailable = false;
	owner.watchTimePaused = false;
	owner.watchTimeSeeking = false;
	owner.watchTimePauseSystemDateUs = 0;
	owner.watchTimeUpdateSequence = 0;
	owner.watchTimeLastEventType = WatchTimeEventType::Update;
	owner.lastWatchTimePoint = {};
}

ofxVlc4::WatchTimeInfo ofxVlc4::MediaComponent::getWatchTimeInfo() const {
	return buildWatchTimeInfo();
}

ofxVlc4::WatchTimeInfo ofxVlc4::MediaComponent::buildWatchTimeInfo() const {
	WatchTimeInfo info;
	info.enabled = owner.watchTimeEnabled;
	info.minPeriodUs = owner.watchTimeMinPeriodUs;

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || !owner.sessionMedia() || playback().isPlaybackLocallyStopped()) {
		return info;
	}

	libvlc_media_player_time_point_t point {};
	int64_t pausedSystemDateUs = 0;
	uint64_t updateSequence = 0;
	WatchTimeEventType eventType = WatchTimeEventType::Update;
	{
		std::lock_guard<std::mutex> lock(owner.watchTimeMutex);
		info.registered = owner.watchTimeRegistered;
		info.available = owner.watchTimePointAvailable;
		info.paused = owner.watchTimePaused;
		info.seeking = owner.watchTimeSeeking;
		point = owner.lastWatchTimePoint;
		pausedSystemDateUs = owner.watchTimePauseSystemDateUs;
		updateSequence = owner.watchTimeUpdateSequence;
		eventType = owner.watchTimeLastEventType;
	}

	return buildWatchTimeInfoSnapshot(
		point,
		owner.watchTimeEnabled,
		info.registered,
		info.available,
		info.paused,
		info.seeking,
		owner.watchTimeMinPeriodUs,
		pausedSystemDateUs,
		eventType,
		updateSequence,
		true);
}

void ofxVlc4::MediaComponent::applyWatchTimeObserver() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (owner.watchTimeRegistered) {
		libvlc_media_player_unwatch_time(player);
		owner.watchTimeRegistered = false;
	}

	clearWatchTimeState();

	if (!owner.watchTimeEnabled) {
		return;
	}

	owner.watchTimeRegistered = libvlc_media_player_watch_time(
		player,
		std::max<int64_t>(0, owner.watchTimeMinPeriodUs),
		ofxVlc4::watchTimeUpdateStatic,
		ofxVlc4::watchTimePausedStatic,
		ofxVlc4::watchTimeSeekStatic,
		&owner) == 0;
}

void ofxVlc4::MediaComponent::applyMediaPlayerRole() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_set_role(player, static_cast<unsigned>(toLibvlcMediaPlayerRole(owner.mediaPlayerRole)));
}

void ofxVlc4::MediaComponent::prepareForMediaDetach() {
	owner.pendingAbLoopStartTimeMs = -1;
	owner.pendingAbLoopStartPosition = -1.0f;
	mediaLibrary().resetCurrentMediaParseState();
	cancelThumbnailRequest();
	clearGeneratedThumbnailInfo();
	clearPendingSnapshotState();

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (player && owner.nativeRecordingActive.load()) {
		libvlc_media_player_record(player, false, nullptr);
		owner.nativeRecordingActive.store(false);
	}
}

void ofxVlc4::MediaComponent::updateSnapshotStateOnRequest(const std::string & requestedPath) {
	mediaLibrary().updateSnapshotStateOnRequest(requestedPath);
}

void ofxVlc4::MediaComponent::updateSnapshotStateFromEvent(const std::string & savedPath) {
	mediaLibrary().updateSnapshotStateFromEvent(savedPath);
}

void ofxVlc4::MediaComponent::clearPendingSnapshotState() {
	mediaLibrary().clearPendingSnapshotState();
}

void ofxVlc4::MediaComponent::updateSnapshotFailureState(const std::string & failureReason) {
	mediaLibrary().updateSnapshotFailureState(failureReason);
}

ofxVlc4::ThumbnailInfo ofxVlc4::MediaComponent::getLastGeneratedThumbnail() const {
	return mediaLibrary().getLastGeneratedThumbnail();
}

bool ofxVlc4::MediaComponent::requestThumbnailByTime(
	int timeMs,
	unsigned width,
	unsigned height,
	bool crop,
	ThumbnailImageType type,
	ThumbnailSeekSpeed speed,
	int timeoutMs) {
	return mediaLibrary().requestThumbnailByTime(timeMs, width, height, crop, type, speed, timeoutMs);
}

bool ofxVlc4::MediaComponent::requestThumbnailByPosition(
	float position,
	unsigned width,
	unsigned height,
	bool crop,
	ThumbnailImageType type,
	ThumbnailSeekSpeed speed,
	int timeoutMs) {
	return mediaLibrary().requestThumbnailByPosition(
		position, width, height, crop, type, speed, timeoutMs);
}

void ofxVlc4::MediaComponent::cancelThumbnailRequest() {
	mediaLibrary().cancelThumbnailRequest();
}

ofxVlc4::MediaParseInfo ofxVlc4::MediaComponent::getCurrentMediaParseInfo() const {
	return mediaLibrary().getCurrentMediaParseInfo();
}

ofxVlc4::MediaParseStatus ofxVlc4::MediaComponent::getCurrentMediaParseStatus() const {
	return mediaLibrary().getCurrentMediaParseStatus();
}

ofxVlc4::MediaParseOptions ofxVlc4::MediaComponent::getMediaParseOptions() const {
	return mediaLibrary().getMediaParseOptions();
}

void ofxVlc4::MediaComponent::setMediaParseOptions(const MediaParseOptions & options) {
	mediaLibrary().setMediaParseOptions(options);
}

bool ofxVlc4::MediaComponent::requestCurrentMediaParse() {
	return mediaLibrary().requestCurrentMediaParse();
}

void ofxVlc4::MediaComponent::stopCurrentMediaParse() {
	mediaLibrary().stopCurrentMediaParse();
}

void ofxVlc4::MediaComponent::handleMediaParsedChanged(libvlc_media_parsed_status_t status) {
	mediaLibrary().handleMediaParsedChanged(status);
}

void ofxVlc4::MediaComponent::handleAttachedThumbnailsFound(libvlc_picture_list_t * thumbnails) {
	mediaLibrary().handleAttachedThumbnailsFound(thumbnails);
}

void ofxVlc4::MediaComponent::handleThumbnailGenerated(libvlc_picture_t * thumbnail) {
	mediaLibrary().handleThumbnailGenerated(thumbnail);
}

void ofxVlc4::MediaComponent::updateNativeRecordingStateFromEvent(bool active, const std::string & recordedFilePath) {
	owner.nativeRecordingActive.store(active);

	const std::string resolvedPath = trimWhitespace(recordedFilePath);
	uint64_t recordedBytes = 0;
	if (!resolvedPath.empty()) {
		std::error_code ec;
		const std::filesystem::path resolvedFilePath(resolvedPath);
		if (std::filesystem::is_regular_file(resolvedFilePath, ec)) {
			recordedBytes = static_cast<uint64_t>(std::filesystem::file_size(resolvedFilePath, ec));
		}
		if (ec) {
			recordedBytes = 0;
		}
	}

	{
		std::lock_guard<std::mutex> lock(owner.playbackStateMutex);
		if (active) {
			clearNativeRecordingOutputLocked();
			setNativeRecordingStatusLocked("Recording started.", "");
		}
		if (!resolvedPath.empty()) {
			setNativeRecordingOutputLocked(
				resolvedPath,
				recordedBytes,
				ofGetTimestampString("%Y-%m-%d %H:%M:%S"));
			setNativeRecordingStatusLocked("Saved: " + resolvedPath, "");
		}
	}

	if (active) {
		owner.setStatus("Native VLC recording started.");
		return;
	}

	if (!resolvedPath.empty()) {
		owner.setStatus("Native VLC recording saved: " + resolvedPath);
		owner.logNotice("Native VLC recording saved: " + resolvedPath);
	}
}

void ofxVlc4::MediaComponent::updateNativeRecordingFailureState(const std::string & failureReason) {
	const std::string trimmedReason = trimWhitespace(failureReason);
	owner.nativeRecordingActive.store(false);
	std::lock_guard<std::mutex> lock(owner.playbackStateMutex);
	setNativeRecordingStatusLocked(
		trimmedReason.empty() ? "" : ("Failed: " + trimmedReason),
		trimmedReason);
}

std::vector<ofxVlc4::MediaDiscovererInfo> ofxVlc4::MediaComponent::getMediaDiscoverers(MediaDiscovererCategory category) const {
	std::vector<MediaDiscovererInfo> discoverers;
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return discoverers;
	}

	std::string currentDiscovererName;
	{
		std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
		currentDiscovererName = owner.mediaDiscovererName;
	}

	libvlc_media_discoverer_description_t ** services = nullptr;
	const size_t serviceCount = libvlc_media_discoverer_list_get(
		instance,
		toLibvlcMediaDiscovererCategory(category),
		&services);
	if (serviceCount == 0 || !services) {
		return discoverers;
	}

	discoverers.reserve(serviceCount);
	for (size_t serviceIndex = 0; serviceIndex < serviceCount; ++serviceIndex) {
		const libvlc_media_discoverer_description_t * service = services[serviceIndex];
		if (!service) {
			continue;
		}

		MediaDiscovererInfo info;
		info.name = service->psz_name ? service->psz_name : "";
		info.longName = trimWhitespace(service->psz_longname ? service->psz_longname : "");
		info.category = toMediaDiscovererCategory(service->i_cat);
		info.current = info.name == currentDiscovererName;
		discoverers.push_back(std::move(info));
	}

	libvlc_media_discoverer_list_release(services, serviceCount);
	return discoverers;
}

std::string ofxVlc4::MediaComponent::getSelectedMediaDiscovererName() const {
	std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
	return owner.mediaDiscovererName;
}

ofxVlc4::MediaDiscoveryStateInfo ofxVlc4::MediaComponent::getMediaDiscoveryState() const {
	std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
	return buildMediaDiscoveryStateInfoLocked();
}

std::vector<ofxVlc4::DiscoveredMediaItemInfo> ofxVlc4::MediaComponent::getDiscoveredMediaItems() const {
	std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
	return owner.discoveredMediaItems;
}

void ofxVlc4::MediaComponent::stopMediaDiscoveryInternal() {
	if (owner.mediaDiscovererMediaListEventManager) {
		if (owner.coreSession && owner.eventRouter) {
			owner.coreSession->detachMediaDiscovererListEvents(owner.eventRouter.get(), VlcEventRouter::mediaDiscovererMediaListEventStatic);
			owner.syncLegacyStateFromCoreSession();
		} else {
			owner.mediaDiscovererMediaListEventManager = nullptr;
		}
	}

	if (owner.mediaDiscovererMediaList) {
		libvlc_media_list_release(owner.mediaDiscovererMediaList);
		owner.mediaDiscovererMediaList = nullptr;
		if (owner.coreSession) {
			owner.coreSession->setMediaDiscovererList(nullptr);
		}
	}

	if (owner.mediaDiscoverer) {
		libvlc_media_discoverer_stop(owner.mediaDiscoverer);
		libvlc_media_discoverer_release(owner.mediaDiscoverer);
		owner.mediaDiscoverer = nullptr;
		if (owner.coreSession) {
			owner.coreSession->setMediaDiscoverer(nullptr);
		}
	}

	std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
	clearMediaDiscoveryStateLocked();
	owner.syncLegacyStateFromCoreSession();
}

void ofxVlc4::MediaComponent::refreshDiscoveredMediaItems() {
	std::vector<DiscoveredMediaItemInfo> refreshedItems;
	std::set<std::string> seenKeys;
	if (owner.mediaDiscovererMediaList) {
		libvlc_media_list_lock(owner.mediaDiscovererMediaList);
		const int itemCount = libvlc_media_list_count(owner.mediaDiscovererMediaList);
		if (itemCount > 0) {
			refreshedItems.reserve(static_cast<size_t>(itemCount));
		}

		for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
			libvlc_media_t * item = libvlc_media_list_item_at_index(owner.mediaDiscovererMediaList, itemIndex);
			if (!item) {
				continue;
			}

			DiscoveredMediaItemInfo info;
			char * rawMrl = libvlc_media_get_mrl(item);
			info.mrl = rawMrl ? trimWhitespace(rawMrl) : "";
			if (rawMrl) {
				libvlc_free(rawMrl);
			}
			info.name = mediaDisplayName(item);
			info.isDirectory = libvlc_media_get_type(item) == libvlc_media_type_directory;
			if (info.name.empty()) {
				info.name = info.mrl.empty() ? ("Item " + ofToString(itemIndex + 1)) : mediaLabelForPath(info.mrl);
			}

			const std::string dedupeKey = !info.mrl.empty()
				? info.mrl
				: (info.name + "|" + (info.isDirectory ? "dir" : "file"));
			if (!dedupeKey.empty() && !seenKeys.insert(dedupeKey).second) {
				libvlc_media_release(item);
				continue;
			}

			refreshedItems.push_back(std::move(info));
			libvlc_media_release(item);
		}
		libvlc_media_list_unlock(owner.mediaDiscovererMediaList);
	}

	std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
	setDiscoveredMediaItemsLocked(std::move(refreshedItems));
}

bool ofxVlc4::MediaComponent::startMediaDiscovery(const std::string & discovererName) {
	const std::string trimmedName = trimWhitespace(discovererName);
	if (trimmedName.empty()) {
		stopMediaDiscovery();
		return true;
	}

	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		owner.setError("Initialize libvlc before starting media discovery.");
		return false;
	}

	if (owner.mediaDiscoverer && owner.mediaDiscovererName == trimmedName) {
		return true;
	}

	stopMediaDiscoveryInternal();

	owner.mediaDiscoverer = libvlc_media_discoverer_new(instance, trimmedName.c_str());
	owner.coreSession->setMediaDiscoverer(owner.mediaDiscoverer);
	if (!owner.mediaDiscoverer) {
		owner.setError("Media discovery could not be created.");
		return false;
	}

	owner.mediaDiscovererMediaList = libvlc_media_discoverer_media_list(owner.mediaDiscoverer);
	owner.coreSession->setMediaDiscovererList(owner.mediaDiscovererMediaList);
	if (!owner.mediaDiscovererMediaList) {
		stopMediaDiscoveryInternal();
		owner.setError("Media discovery list is unavailable.");
		return false;
	}

	owner.mediaDiscovererMediaListEventManager = libvlc_media_list_event_manager(owner.mediaDiscovererMediaList);
	if (owner.mediaDiscovererMediaListEventManager) {
		owner.coreSession->setMediaDiscovererListEvents(owner.mediaDiscovererMediaListEventManager);
		if (owner.coreSession && owner.eventRouter) {
			owner.coreSession->attachMediaDiscovererListEvents(owner.eventRouter.get(), VlcEventRouter::mediaDiscovererMediaListEventStatic);
			owner.syncLegacyStateFromCoreSession();
		}
	}

	if (libvlc_media_discoverer_start(owner.mediaDiscoverer) != 0) {
		stopMediaDiscoveryInternal();
		owner.setError("Media discovery could not be started.");
		return false;
	}

	std::string discovererLongName;
	MediaDiscovererCategory discovererCategory = MediaDiscovererCategory::Lan;
	const auto allCategories = {
		MediaDiscovererCategory::Devices,
		MediaDiscovererCategory::Lan,
		MediaDiscovererCategory::Podcasts,
		MediaDiscovererCategory::LocalDirs
	};
	for (const MediaDiscovererCategory category : allCategories) {
		const std::vector<MediaDiscovererInfo> discoverers = getMediaDiscoverers(category);
		const auto it = std::find_if(
			discoverers.begin(),
			discoverers.end(),
			[&trimmedName](const MediaDiscovererInfo & info) { return info.name == trimmedName; });
		if (it != discoverers.end()) {
			discovererLongName = mediaDiscovererDisplayLabel(*it);
			discovererCategory = it->category;
			break;
		}
	}
	{
		std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
		setMediaDiscoveryDescriptorLocked(trimmedName, discovererLongName, discovererCategory, false);
	}
	refreshDiscoveredMediaItems();
	owner.setStatus("Media discovery started.");
	const std::string discoveryLabel = discovererLongName.empty()
		? trimmedName
		: (discovererLongName + " (" + trimmedName + ")");
	owner.logNotice("Media discovery started: " + discoveryLabel + ".");
	return true;
}

void ofxVlc4::MediaComponent::stopMediaDiscovery() {
	if (!owner.mediaDiscoverer && owner.mediaDiscovererName.empty()) {
		return;
	}

	stopMediaDiscoveryInternal();
	owner.setStatus("Media discovery stopped.");
	owner.logNotice("Media discovery stopped.");
}

bool ofxVlc4::MediaComponent::isMediaDiscoveryActive() const {
	return owner.mediaDiscoverer != nullptr;
}

bool ofxVlc4::MediaComponent::addDiscoveredMediaItemToPlaylist(int index) {
	const std::vector<DiscoveredMediaItemInfo> items = getDiscoveredMediaItems();
	if (index < 0 || index >= static_cast<int>(items.size())) {
		return false;
	}

	const auto & item = items[static_cast<size_t>(index)];
	if (item.mrl.empty()) {
		return false;
	}

	addToPlaylist(item.mrl);
	return true;
}

bool ofxVlc4::MediaComponent::playDiscoveredMediaItem(int index) {
	const int previousCount = static_cast<int>(mediaLibrary().getPlaylistSize());
	if (!addDiscoveredMediaItemToPlaylist(index)) {
		return false;
	}

	const int newIndex = static_cast<int>(mediaLibrary().getPlaylistSize()) - 1;
	if (newIndex < previousCount || newIndex < 0) {
		return false;
	}

	playback().playIndex(newIndex);
	return true;
}

int ofxVlc4::MediaComponent::addAllDiscoveredMediaItemsToPlaylist() {
	const std::vector<DiscoveredMediaItemInfo> items = getDiscoveredMediaItems();
	int addedCount = 0;
	for (const auto & item : items) {
		if (item.mrl.empty()) {
			continue;
		}
		addToPlaylist(item.mrl);
		++addedCount;
	}
	return addedCount;
}

void ofxVlc4::MediaComponent::clearMetadataCache() {
	mediaLibrary().clearMetadataCache();
}

void ofxVlc4::MediaComponent::cacheArtworkPathForMediaPath(const std::string & mediaPath, const std::string & artworkPath) {
	mediaLibrary().cacheArtworkPathForMediaPath(mediaPath, artworkPath);
}

void ofxVlc4::MediaComponent::clearGeneratedThumbnailInfo() {
	mediaLibrary().clearGeneratedThumbnailInfo();
}

std::string ofxVlc4::MediaComponent::getCurrentMediaMeta(MediaMetaField field) const {
	return mediaLibrary().getCurrentMediaMeta(field);
}

bool ofxVlc4::MediaComponent::setCurrentMediaMeta(MediaMetaField field, const std::string & value) {
	return mediaLibrary().setCurrentMediaMeta(field, value);
}

bool ofxVlc4::MediaComponent::saveCurrentMediaMeta() {
	return mediaLibrary().saveCurrentMediaMeta();
}

std::vector<std::string> ofxVlc4::MediaComponent::getCurrentMediaMetaExtraNames() const {
	return mediaLibrary().getCurrentMediaMetaExtraNames();
}

std::string ofxVlc4::MediaComponent::getCurrentMediaMetaExtra(const std::string & name) const {
	return mediaLibrary().getCurrentMediaMetaExtra(name);
}

bool ofxVlc4::MediaComponent::setCurrentMediaMetaExtra(const std::string & name, const std::string & value) {
	return mediaLibrary().setCurrentMediaMetaExtra(name, value);
}

bool ofxVlc4::MediaComponent::removeCurrentMediaMetaExtra(const std::string & name) {
	return mediaLibrary().removeCurrentMediaMetaExtra(name);
}

std::vector<ofxVlc4::BookmarkInfo> ofxVlc4::MediaComponent::getBookmarksForPath(const std::string & path) const {
	return mediaLibrary().getBookmarksForPath(path);
}

std::vector<ofxVlc4::BookmarkInfo> ofxVlc4::MediaComponent::getCurrentBookmarks() const {
	return mediaLibrary().getCurrentBookmarks();
}

bool ofxVlc4::MediaComponent::addBookmarkAtTime(int timeMs, const std::string & label) {
	return mediaLibrary().addBookmarkAtTime(timeMs, label);
}

bool ofxVlc4::MediaComponent::addCurrentBookmark(const std::string & label) {
	return mediaLibrary().addCurrentBookmark(label);
}

bool ofxVlc4::MediaComponent::seekToBookmark(const std::string & bookmarkId) {
	return mediaLibrary().seekToBookmark(bookmarkId);
}

bool ofxVlc4::MediaComponent::removeBookmark(const std::string & bookmarkId) {
	return mediaLibrary().removeBookmark(bookmarkId);
}

void ofxVlc4::MediaComponent::clearBookmarksForPath(const std::string & path) {
	mediaLibrary().clearBookmarksForPath(path);
}

void ofxVlc4::MediaComponent::clearCurrentBookmarks() {
	mediaLibrary().clearCurrentBookmarks();
}

void ofxVlc4::MediaComponent::mediaDiscovererMediaListEvent(const libvlc_event_t * event) {
	if (!event) {
		return;
	}

	switch (event->type) {
	case libvlc_MediaListItemAdded:
	case libvlc_MediaListItemDeleted:
		{
			std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
			setMediaDiscoveryEndReachedLocked(false);
		}
		refreshDiscoveredMediaItems();
		break;
	case libvlc_MediaListEndReached:
		{
			std::lock_guard<std::mutex> lock(owner.mediaDiscovererMutex);
			setMediaDiscoveryEndReachedLocked(true);
		}
		refreshDiscoveredMediaItems();
		break;
	default:
		break;
	}
}

void ofxVlc4::clearMetadataCache() {
	mediaComponent->clearMetadataCache();
}

void ofxVlc4::cacheArtworkPathForMediaPath(const std::string & mediaPath, const std::string & artworkPath) {
	mediaComponent->cacheArtworkPathForMediaPath(mediaPath, artworkPath);
}

void ofxVlc4::clearGeneratedThumbnailInfo() {
	mediaComponent->clearGeneratedThumbnailInfo();
}

void ofxVlc4::dismissAllDialogs() {
	mediaComponent->dismissAllDialogs();
}

void ofxVlc4::upsertDialog(const DialogInfo & dialog) {
	mediaComponent->upsertDialog(dialog);
}

void ofxVlc4::removeDialog(std::uintptr_t token) {
	mediaComponent->removeDialog(token);
}

std::vector<ofxVlc4::DialogInfo> ofxVlc4::getActiveDialogs() const {
	return mediaComponent->getActiveDialogs();
}

ofxVlc4::DialogErrorInfo ofxVlc4::getLastDialogError() const {
	return mediaComponent->getLastDialogError();
}

void ofxVlc4::clearLastDialogError() {
	mediaComponent->clearLastDialogError();
}

namespace {

ofxVlc4::WatchTimeInfo buildWatchTimeInfoSnapshot(
	const libvlc_media_player_time_point_t & point,
	bool enabled,
	bool registered,
	bool available,
	bool paused,
	bool seeking,
	int64_t minPeriodUs,
	int64_t pausedSystemDateUs,
	ofxVlc4::WatchTimeEventType eventType,
	uint64_t sequence,
	bool interpolate) {
	ofxVlc4::WatchTimeInfo info;
	info.eventType = eventType;
	info.sequence = sequence;
	info.enabled = enabled;
	info.registered = registered;
	info.available = available;
	info.paused = paused;
	info.seeking = seeking;
	info.minPeriodUs = minPeriodUs;

	if (!info.available) {
		return info;
	}

	info.position = point.position;
	info.rate = point.rate;
	info.timeUs = point.ts_us;
	info.lengthUs = point.length_us;
	info.systemDateUs = point.system_date_us;
	info.interpolatedTimeUs = point.ts_us;
	info.interpolatedPosition = point.position;

	if (!interpolate) {
		return info;
	}

	const bool pointLooksValid =
		std::isfinite(point.position) &&
		std::isfinite(point.rate) &&
		point.ts_us >= 0 &&
		point.length_us >= 0 &&
		(point.system_date_us > 0 || (paused && pausedSystemDateUs > 0));
	if (!pointLooksValid) {
		return info;
	}

	const int64_t systemNowUs =
		(info.paused && pausedSystemDateUs > 0) ? pausedSystemDateUs : libvlc_clock();
	if (systemNowUs <= 0 || systemNowUs == INT64_MAX) {
		return info;
	}
	int64_t interpolatedTimeUs = point.ts_us;
	double interpolatedPosition = point.position;
	if (libvlc_media_player_time_point_interpolate(&point, systemNowUs, &interpolatedTimeUs, &interpolatedPosition) == 0) {
		if (std::isfinite(interpolatedPosition) && interpolatedTimeUs >= 0) {
			info.interpolatedTimeUs = interpolatedTimeUs;
			info.interpolatedPosition = interpolatedPosition;
		}
	}

	return info;
}

std::string formatPlaybackTimecodeValue(int64_t timeUs, double fps) {
	if (timeUs < 0 || !std::isfinite(fps) || fps <= 0.0) {
		return "--:--:--:--";
	}

	const int64_t totalSeconds = timeUs / 1000000;
	const int64_t remainderUs = timeUs % 1000000;
	const int hours = static_cast<int>(totalSeconds / 3600);
	const int minutes = static_cast<int>((totalSeconds / 60) % 60);
	const int seconds = static_cast<int>(totalSeconds % 60);
	const int maxFrames = std::max(1, static_cast<int>(std::ceil(fps)));
	int frames = static_cast<int>(std::floor((static_cast<double>(remainderUs) * fps) / 1000000.0));
	frames = std::clamp(frames, 0, maxFrames - 1);

	std::ostringstream stream;
	stream << std::setfill('0')
		   << std::setw(2) << hours << ":"
		   << std::setw(2) << minutes << ":"
		   << std::setw(2) << seconds << ":"
		   << std::setw(2) << frames;
	return stream.str();
}

std::string mediaDiscovererDisplayLabel(const ofxVlc4::MediaDiscovererInfo & discoverer) {
	const std::string longName = trimWhitespace(discoverer.longName);
	if (!longName.empty()) {
		return longName;
	}

	const std::string name = trimWhitespace(discoverer.name);
	if (!name.empty()) {
		return name;
	}

	return "Discoverer";
}

bool hasLibVlcLogFilePath(const std::string & path) {
	return !normalizeOptionalPath(path).empty();
}

ofxVlc4::LibVlcLogEntry toPublicLogEntry(const VlcCoreLogEntry & entry) {
	ofxVlc4::LibVlcLogEntry publicEntry;
	publicEntry.level = entry.level;
	publicEntry.module = entry.module;
	publicEntry.file = entry.file;
	publicEntry.line = entry.line;
	publicEntry.objectName = entry.objectName;
	publicEntry.objectHeader = entry.objectHeader;
	publicEntry.objectId = entry.objectId;
	publicEntry.message = entry.message;
	return publicEntry;
}

}

bool ofxVlc4::isLibVlcLoggingEnabled() const {
	return mediaComponent->isLibVlcLoggingEnabled();
}

void ofxVlc4::setLibVlcLoggingEnabled(bool enabled) {
	mediaComponent->setLibVlcLoggingEnabled(enabled);
}

bool ofxVlc4::isLibVlcLogFileEnabled() const {
	return mediaComponent->isLibVlcLogFileEnabled();
}

void ofxVlc4::setLibVlcLogFileEnabled(bool enabled) {
	mediaComponent->setLibVlcLogFileEnabled(enabled);
}

std::string ofxVlc4::getLibVlcLogFilePath() const {
	return mediaComponent->getLibVlcLogFilePath();
}

void ofxVlc4::setLibVlcLogFilePath(const std::string & path) {
	mediaComponent->setLibVlcLogFilePath(path);
}

std::vector<ofxVlc4::LibVlcLogEntry> ofxVlc4::getLibVlcLogEntries() const {
	return mediaComponent->getLibVlcLogEntries();
}

void ofxVlc4::clearLibVlcLogEntries() {
	mediaComponent->clearLibVlcLogEntries();
}

ofxVlc4::MediaPlayerRole ofxVlc4::getMediaPlayerRole() const {
	return mediaComponent->getMediaPlayerRole();
}

void ofxVlc4::setMediaPlayerRole(MediaPlayerRole role) {
	mediaComponent->setMediaPlayerRole(role);
}

bool ofxVlc4::isWatchTimeEnabled() const {
	return mediaComponent->isWatchTimeEnabled();
}

void ofxVlc4::setWatchTimeEnabled(bool enabled) {
	mediaComponent->setWatchTimeEnabled(enabled);
}

int64_t ofxVlc4::getWatchTimeMinPeriodUs() const {
	return mediaComponent->getWatchTimeMinPeriodUs();
}

void ofxVlc4::setWatchTimeMinPeriodUs(int64_t minPeriodUs) {
	mediaComponent->setWatchTimeMinPeriodUs(minPeriodUs);
}

bool ofxVlc4::isPlaybackLocallyStopped() const {
	return playbackController->isPlaybackLocallyStopped();
}

void ofxVlc4::clearWatchTimeState() {
	mediaComponent->clearWatchTimeState();
}

ofxVlc4::WatchTimeInfo ofxVlc4::getWatchTimeInfo() const {
	return mediaComponent->getWatchTimeInfo();
}

void ofxVlc4::setWatchTimeCallback(WatchTimeCallback callback) {
	std::lock_guard<std::mutex> lock(watchTimeMutex);
	this->watchTimeCallback = std::move(callback);
}

void ofxVlc4::clearWatchTimeCallback() {
	setWatchTimeCallback({});
}

bool ofxVlc4::hasWatchTimeCallback() const {
	std::lock_guard<std::mutex> lock(watchTimeMutex);
	return static_cast<bool>(watchTimeCallback);
}

double ofxVlc4::getPlaybackClockFramesPerSecond() const {
	const double cachedFps = cachedVideoTrackFps.load();
	if (std::isfinite(cachedFps) && cachedFps > 0.0) {
		return cachedFps;
	}

	libvlc_media_player_t * player = sessionPlayer();
	if (player && libvlc_media_player_is_playing(player)) {
		return 0.0;
	}

	const auto tracks = getVideoTracks();
	auto resolveFps = [](const MediaTrackInfo & track) -> double {
		if (track.frameRateNum == 0 || track.frameRateDen == 0) {
			return 0.0;
		}
		return static_cast<double>(track.frameRateNum) / static_cast<double>(track.frameRateDen);
	};

	for (const auto & track : tracks) {
		if (!track.selected) {
			continue;
		}
		const double fps = resolveFps(track);
		if (std::isfinite(fps) && fps > 0.0) {
			return fps;
		}
	}

	for (const auto & track : tracks) {
		const double fps = resolveFps(track);
		if (std::isfinite(fps) && fps > 0.0) {
			cachedVideoTrackFps.store(fps);
			return fps;
		}
	}

	return 0.0;
}

std::string ofxVlc4::formatCurrentPlaybackTimecode(double fps, bool interpolated) const {
	const WatchTimeInfo watchTime = getWatchTimeInfo();
	const int64_t timeUs = interpolated ? watchTime.interpolatedTimeUs : watchTime.timeUs;
	const double resolvedFps = (std::isfinite(fps) && fps > 0.0) ? fps : getPlaybackClockFramesPerSecond();
	return formatPlaybackTimecode(timeUs, resolvedFps);
}

std::string ofxVlc4::formatPlaybackTimecode(int64_t timeUs, double fps) {
	return formatPlaybackTimecodeValue(timeUs, fps);
}

void ofxVlc4::applyMediaPlayerRole() {
	mediaComponent->applyMediaPlayerRole();
}

void ofxVlc4::applyWatchTimeObserver() {
	mediaComponent->applyWatchTimeObserver();
}

void ofxVlc4::watchTimeUpdateStatic(const libvlc_media_player_time_point_t * value, void * data) {
	auto * player = static_cast<ofxVlc4 *>(data);
	if (!player || !value) {
		return;
	}

	WatchTimeCallback callback;
	WatchTimeInfo info;
	{
		std::lock_guard<std::mutex> lock(player->watchTimeMutex);
		player->lastWatchTimePoint = *value;
		player->watchTimePointAvailable = true;
		player->watchTimePaused = value->system_date_us == INT64_MAX;
		player->watchTimeSeeking = false;
		player->watchTimeLastEventType = WatchTimeEventType::Update;
		++player->watchTimeUpdateSequence;
		if (!player->watchTimePaused) {
			player->watchTimePauseSystemDateUs = 0;
		}
		callback = player->watchTimeCallback;
		info = buildWatchTimeInfoSnapshot(
			*value,
			player->watchTimeEnabled,
			player->watchTimeRegistered,
			player->watchTimePointAvailable,
			player->watchTimePaused,
			player->watchTimeSeeking,
			player->watchTimeMinPeriodUs,
			player->watchTimePauseSystemDateUs,
			player->watchTimeLastEventType,
			player->watchTimeUpdateSequence,
			false);
	}
	if (callback) {
		callback(info);
	}
}

void ofxVlc4::watchTimePausedStatic(int64_t system_date_us, void * data) {
	auto * player = static_cast<ofxVlc4 *>(data);
	if (!player) {
		return;
	}

	WatchTimeCallback callback;
	WatchTimeInfo info;
	{
		std::lock_guard<std::mutex> lock(player->watchTimeMutex);
		player->watchTimePaused = true;
		player->watchTimePauseSystemDateUs = system_date_us;
		player->watchTimeSeeking = false;
		player->watchTimeLastEventType = WatchTimeEventType::Paused;
		++player->watchTimeUpdateSequence;
		callback = player->watchTimeCallback;
		info = buildWatchTimeInfoSnapshot(
			player->lastWatchTimePoint,
			player->watchTimeEnabled,
			player->watchTimeRegistered,
			player->watchTimePointAvailable,
			player->watchTimePaused,
			player->watchTimeSeeking,
			player->watchTimeMinPeriodUs,
			player->watchTimePauseSystemDateUs,
			player->watchTimeLastEventType,
			player->watchTimeUpdateSequence,
			false);
	}
	if (callback) {
		callback(info);
	}
}

void ofxVlc4::watchTimeSeekStatic(const libvlc_media_player_time_point_t * value, void * data) {
	auto * player = static_cast<ofxVlc4 *>(data);
	if (!player) {
		return;
	}

	WatchTimeCallback callback;
	WatchTimeInfo info;
	{
		std::lock_guard<std::mutex> lock(player->watchTimeMutex);
		player->watchTimeSeeking = value != nullptr;
		player->watchTimeLastEventType = value ? WatchTimeEventType::Seek : WatchTimeEventType::SeekEnd;
		++player->watchTimeUpdateSequence;
		if (value) {
			player->lastWatchTimePoint = *value;
			player->watchTimePointAvailable = true;
			player->watchTimePaused = value->system_date_us == INT64_MAX;
			if (!player->watchTimePaused) {
				player->watchTimePauseSystemDateUs = 0;
			}
		}
		callback = player->watchTimeCallback;
		info = buildWatchTimeInfoSnapshot(
			player->lastWatchTimePoint,
			player->watchTimeEnabled,
			player->watchTimeRegistered,
			player->watchTimePointAvailable,
			player->watchTimePaused,
			player->watchTimeSeeking,
			player->watchTimeMinPeriodUs,
			player->watchTimePauseSystemDateUs,
			player->watchTimeLastEventType,
			player->watchTimeUpdateSequence,
			false);
	}
	if (callback) {
		callback(info);
	}
}

bool ofxVlc4::postDialogLogin(
	std::uintptr_t token,
	const std::string & username,
	const std::string & password,
	bool store) {
	return mediaComponent->postDialogLogin(token, username, password, store);
}

bool ofxVlc4::postDialogAction(std::uintptr_t token, int action) {
	return mediaComponent->postDialogAction(token, action);
}

bool ofxVlc4::dismissDialog(std::uintptr_t token) {
	return mediaComponent->dismissDialog(token);
}

void ofxVlc4::detachEvents() {
	if (mediaPlayerEventManager) {
		if (coreSession && eventRouter) {
			coreSession->detachPlayerEvents(eventRouter.get(), VlcEventRouter::vlcMediaPlayerEventStatic);
			syncLegacyStateFromCoreSession();
		} else {
			mediaPlayerEventManager = nullptr;
		}
	}

	if (mediaEventManager) {
		if (coreSession && eventRouter) {
			coreSession->detachMediaEvents(eventRouter.get(), VlcEventRouter::vlcMediaEventStatic);
			syncLegacyStateFromCoreSession();
		} else {
			mediaEventManager = nullptr;
		}
	}
}

void ofxVlc4::MediaComponent::clearRendererItems() {
	std::lock_guard<std::mutex> lock(owner.rendererMutex);
	for (auto & renderer : owner.discoveredRenderers) {
		if (renderer.item) {
			libvlc_renderer_item_release(renderer.item);
			renderer.item = nullptr;
		}
	}
	owner.discoveredRenderers.clear();
}

void ofxVlc4::MediaComponent::stopRendererDiscoveryInternal() {
	if (owner.rendererDiscovererEventManager) {
		if (owner.coreSession && owner.eventRouter) {
			owner.coreSession->detachRendererEvents(owner.eventRouter.get(), VlcEventRouter::rendererDiscovererEventStatic);
			owner.syncLegacyStateFromCoreSession();
		} else {
			owner.rendererDiscovererEventManager = nullptr;
		}
	}

	if (owner.rendererDiscoverer) {
		libvlc_renderer_discoverer_stop(owner.rendererDiscoverer);
		libvlc_renderer_discoverer_release(owner.rendererDiscoverer);
		owner.rendererDiscoverer = nullptr;
		if (owner.coreSession) {
			owner.coreSession->setRendererDiscoverer(nullptr);
		}
	}

	clearRendererItems();
	owner.syncLegacyStateFromCoreSession();
}

void ofxVlc4::stopMediaDiscoveryInternal() {
	mediaComponent->stopMediaDiscoveryInternal();
}

void ofxVlc4::clearRendererItems() {
	mediaComponent->clearRendererItems();
}

void ofxVlc4::stopRendererDiscoveryInternal() {
	mediaComponent->stopRendererDiscoveryInternal();
}

void ofxVlc4::refreshDiscoveredMediaItems() {
	mediaComponent->refreshDiscoveredMediaItems();
}

bool ofxVlc4::MediaComponent::applySelectedRenderer() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	libvlc_renderer_item_t * rendererItem = nullptr;
	libvlc_renderer_item_t * heldRendererItem = nullptr;
	std::string rendererLabel = "local";
	if (!owner.selectedRendererId.empty()) {
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		const RendererItemEntry * rendererEntry = findRendererEntryByIdLocked(owner.selectedRendererId);
		if (!rendererEntry || !rendererEntry->item) {
			owner.logVerbose("Selected renderer is not currently available; using local output until it appears.");
			rendererLabel = "local";
		} else {
			heldRendererItem = libvlc_renderer_item_hold(rendererEntry->item);
			rendererItem = heldRendererItem;
			rendererLabel = rendererEntry->name.empty() ? rendererEntry->id : rendererEntry->name;
		}
	}

	const int result = libvlc_media_player_set_renderer(player, rendererItem);
	if (heldRendererItem) {
		libvlc_renderer_item_release(heldRendererItem);
	}

	if (result != 0) {
		refreshRendererStateInfo();
		owner.logWarning("Renderer could not be applied: " + rendererLabel + ".");
		return false;
	}

	refreshRendererStateInfo();
	owner.logVerbose("Renderer applied: " + rendererLabel + ".");
	return true;
}

bool ofxVlc4::applySelectedRenderer() {
	return mediaComponent->applySelectedRenderer();
}

std::vector<ofxVlc4::MediaDiscovererInfo> ofxVlc4::getMediaDiscoverers(MediaDiscovererCategory category) const {
	return mediaComponent->getMediaDiscoverers(category);
}

std::string ofxVlc4::getSelectedMediaDiscovererName() const {
	return mediaComponent->getSelectedMediaDiscovererName();
}

ofxVlc4::MediaDiscoveryStateInfo ofxVlc4::getMediaDiscoveryState() const {
	return mediaComponent->getMediaDiscoveryState();
}

bool ofxVlc4::startMediaDiscovery(const std::string & discovererName) {
	return mediaComponent->startMediaDiscovery(discovererName);
}

void ofxVlc4::stopMediaDiscovery() {
	mediaComponent->stopMediaDiscovery();
}

bool ofxVlc4::isMediaDiscoveryActive() const {
	return mediaComponent->isMediaDiscoveryActive();
}

std::vector<ofxVlc4::DiscoveredMediaItemInfo> ofxVlc4::getDiscoveredMediaItems() const {
	return mediaComponent->getDiscoveredMediaItems();
}

bool ofxVlc4::addDiscoveredMediaItemToPlaylist(int index) {
	return mediaComponent->addDiscoveredMediaItemToPlaylist(index);
}

bool ofxVlc4::playDiscoveredMediaItem(int index) {
	return mediaComponent->playDiscoveredMediaItem(index);
}

int ofxVlc4::addAllDiscoveredMediaItemsToPlaylist() {
	return mediaComponent->addAllDiscoveredMediaItemsToPlaylist();
}

std::vector<ofxVlc4::RendererDiscovererInfo> ofxVlc4::getRendererDiscoverers() const {
	return mediaComponent->getRendererDiscoverers();
}

std::vector<ofxVlc4::RendererDiscovererInfo> ofxVlc4::MediaComponent::getRendererDiscoverers() const {
	std::vector<RendererDiscovererInfo> discoverers;
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return discoverers;
	}

	libvlc_rd_description_t ** services = nullptr;
	const size_t serviceCount = libvlc_renderer_discoverer_list_get(instance, &services);
	if (serviceCount == 0 || !services) {
		return discoverers;
	}

	discoverers.reserve(serviceCount);
	for (size_t serviceIndex = 0; serviceIndex < serviceCount; ++serviceIndex) {
		const libvlc_rd_description_t * service = services[serviceIndex];
		if (!service) {
			continue;
		}

		RendererDiscovererInfo info;
		info.name = service->psz_name ? service->psz_name : "";
		info.longName = trimWhitespace(service->psz_longname ? service->psz_longname : "");
		discoverers.push_back(std::move(info));
	}

	libvlc_renderer_discoverer_list_release(services, serviceCount);
	return discoverers;
}

std::string ofxVlc4::getSelectedRendererDiscovererName() const {
	return mediaComponent->getSelectedRendererDiscovererName();
}

std::string ofxVlc4::MediaComponent::getSelectedRendererDiscovererName() const {
	std::lock_guard<std::mutex> lock(owner.rendererMutex);
	return owner.rendererDiscovererName;
}

void ofxVlc4::resetRendererStateInfo() {
	mediaComponent->resetRendererStateInfo();
}

void ofxVlc4::MediaComponent::resetRendererStateInfo() {
	std::lock_guard<std::mutex> lock(owner.rendererMutex);
	owner.rendererStateInfo = buildRendererStateInfoLocked();
}

void ofxVlc4::MediaComponent::resetSubtitleStateInfo() {
	std::lock_guard<std::mutex> lock(owner.subtitleStateMutex);
	owner.subtitleStateInfo = buildSubtitleStateInfo(false);
}

void ofxVlc4::MediaComponent::resetNavigationStateInfo() {
	std::lock_guard<std::mutex> lock(owner.navigationStateMutex);
	owner.navigationStateInfo = buildNavigationStateInfo();
}

void ofxVlc4::MediaComponent::refreshPrimaryTrackStateInfo() {
	const int audioTrackCount = static_cast<int>(getAudioTracks().size());
	const std::vector<MediaTrackInfo> videoTracks = getVideoTracks();
	const int videoTrackCount = static_cast<int>(videoTracks.size());
	auto resolveTrackFps = [](const MediaTrackInfo & track) -> double {
		if (track.frameRateNum == 0 || track.frameRateDen == 0) {
			return 0.0;
		}
		return static_cast<double>(track.frameRateNum) / static_cast<double>(track.frameRateDen);
	};
	double resolvedVideoFps = 0.0;
	for (const auto & track : videoTracks) {
		if (!track.selected) {
			continue;
		}
		const double fps = resolveTrackFps(track);
		if (std::isfinite(fps) && fps > 0.0) {
			resolvedVideoFps = fps;
			break;
		}
	}
	if (!(std::isfinite(resolvedVideoFps) && resolvedVideoFps > 0.0)) {
		for (const auto & track : videoTracks) {
			const double fps = resolveTrackFps(track);
			if (std::isfinite(fps) && fps > 0.0) {
				resolvedVideoFps = fps;
				break;
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(owner.audioStateMutex);
		owner.audioStateInfo.trackCount = audioTrackCount;
		owner.audioStateInfo.tracksAvailable = audioTrackCount > 0;
	}

	owner.cachedVideoTrackCount.store(std::max(0, videoTrackCount));
	owner.cachedVideoTrackFps.store((std::isfinite(resolvedVideoFps) && resolvedVideoFps > 0.0) ? resolvedVideoFps : 0.0);
}

void ofxVlc4::refreshRendererStateInfo() {
	mediaComponent->refreshRendererStateInfo();
}

ofxVlc4::MediaDiscoveryStateInfo ofxVlc4::MediaComponent::buildMediaDiscoveryStateInfoLocked() const {
	MediaDiscoveryStateInfo state;
	state.discovererName = owner.mediaDiscovererName;
	state.discovererLongName = owner.mediaDiscovererLongName;
	state.category = owner.mediaDiscovererCategory;
	state.active = !state.discovererName.empty();
	state.endReached = owner.mediaDiscovererEndReached;
	state.itemCount = owner.discoveredMediaItems.size();
	state.directoryCount = static_cast<size_t>(std::count_if(
		owner.discoveredMediaItems.begin(),
		owner.discoveredMediaItems.end(),
		[](const DiscoveredMediaItemInfo & item) { return item.isDirectory; }));
	return state;
}

void ofxVlc4::MediaComponent::clearMediaDiscoveryStateLocked() {
	owner.discoveredMediaItems.clear();
	owner.mediaDiscovererName.clear();
	owner.mediaDiscovererLongName.clear();
	owner.mediaDiscovererCategory = MediaDiscovererCategory::Lan;
	owner.mediaDiscovererEndReached = false;
}

void ofxVlc4::MediaComponent::setMediaDiscoveryDescriptorLocked(
	const std::string & name,
	const std::string & longName,
	MediaDiscovererCategory category,
	bool endReached) {
	owner.mediaDiscovererName = name;
	owner.mediaDiscovererLongName = longName;
	owner.mediaDiscovererCategory = category;
	owner.mediaDiscovererEndReached = endReached;
}

void ofxVlc4::MediaComponent::setMediaDiscoveryEndReachedLocked(bool endReached) {
	owner.mediaDiscovererEndReached = endReached;
}

void ofxVlc4::MediaComponent::setDiscoveredMediaItemsLocked(std::vector<DiscoveredMediaItemInfo> items) {
	owner.discoveredMediaItems = std::move(items);
}

void ofxVlc4::MediaComponent::setLibVlcLoggingEnabledValue(bool enabled) {
	owner.libVlcLoggingEnabled = enabled;
	if (owner.coreSession) {
		owner.coreSession->setLoggingEnabled(enabled);
	}
}

void ofxVlc4::MediaComponent::setLibVlcLogFileEnabledValue(bool enabled) {
	owner.libVlcLogFileEnabled = enabled;
	if (owner.coreSession) {
		owner.coreSession->setLogFileEnabled(enabled);
	}
}

void ofxVlc4::MediaComponent::setLibVlcLogFilePathValue(const std::string & path) {
	owner.libVlcLogFilePath = path;
	if (owner.coreSession) {
		owner.coreSession->setLogFilePath(path);
	}
}

void ofxVlc4::MediaComponent::setNativeRecordingEnabledValue(bool enabled) {
	owner.nativeRecordingEnabled = enabled;
}

void ofxVlc4::MediaComponent::setNativeRecordDirectoryValue(const std::string & directory) {
	owner.nativeRecordDirectory = directory;
}

const ofxVlc4::RendererItemEntry * ofxVlc4::MediaComponent::findRendererEntryByIdLocked(
	const std::string & rendererId) const {
	if (rendererId.empty()) {
		return nullptr;
	}

	const auto it = std::find_if(
		owner.discoveredRenderers.begin(),
		owner.discoveredRenderers.end(),
		[&rendererId](const RendererItemEntry & entry) { return entry.id == rendererId; });
	return it != owner.discoveredRenderers.end() ? &(*it) : nullptr;
}

ofxVlc4::RendererStateInfo ofxVlc4::MediaComponent::buildRendererStateInfoLocked() const {
	RendererStateInfo info;
	info.discoveryActive = owner.rendererDiscoverer != nullptr;
	info.discovererName = owner.rendererDiscovererName;
	info.discoveredRendererCount = owner.discoveredRenderers.size();
	info.requestedRendererId = owner.selectedRendererId;
	info.usingLocalFallback = true;

	if (!owner.selectedRendererId.empty()) {
		info.selectedRendererKnown = true;
		info.selectedRenderer.id = owner.selectedRendererId;
		info.selectedRenderer.name = owner.selectedRendererId;
		info.selectedRenderer.selected = true;
		info.reconnectPending = true;

		if (const RendererItemEntry * selectedEntry = findRendererEntryByIdLocked(owner.selectedRendererId)) {
			info.selectedRenderer = buildRendererInfoLocked(*selectedEntry, true);
			info.selectedRendererAvailable = true;
			info.usingLocalFallback = false;
			info.reconnectPending = false;
		}
	}

	return info;
}

ofxVlc4::RendererInfo ofxVlc4::MediaComponent::buildRendererInfoLocked(
	const RendererItemEntry & entry,
	bool selected) const {
	RendererInfo info;
	info.id = entry.id;
	info.name = entry.name;
	info.type = entry.type;
	info.iconUri = entry.iconUri;
	info.canAudio = entry.canAudio;
	info.canVideo = entry.canVideo;
	info.selected = selected;
	return info;
}

std::vector<ofxVlc4::RendererInfo> ofxVlc4::MediaComponent::buildDiscoveredRendererInfosLocked() const {
	std::vector<RendererInfo> renderers;
	renderers.reserve(owner.discoveredRenderers.size());
	for (const RendererItemEntry & entry : owner.discoveredRenderers) {
		renderers.push_back(buildRendererInfoLocked(
			entry,
			!owner.selectedRendererId.empty() && entry.id == owner.selectedRendererId));
	}
	return renderers;
}

bool ofxVlc4::MediaComponent::canApplyRendererImmediately() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	return player && isStoppedOrIdleState(libvlc_media_player_get_state(player));
}

void ofxVlc4::MediaComponent::clearSelectedRendererLocked() {
	owner.selectedRendererId.clear();
}

void ofxVlc4::MediaComponent::setSelectedRendererIdLocked(const std::string & rendererId) {
	owner.selectedRendererId = rendererId;
}

void ofxVlc4::MediaComponent::setRendererDiscovererNameLocked(const std::string & discovererName) {
	owner.rendererDiscovererName = discovererName;
}

ofxVlc4::SubtitleStateInfo ofxVlc4::MediaComponent::getCachedSubtitleStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.subtitleStateMutex);
	return owner.subtitleStateInfo;
}

ofxVlc4::NavigationStateInfo ofxVlc4::MediaComponent::getCachedNavigationStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.navigationStateMutex);
	return owner.navigationStateInfo;
}

void ofxVlc4::MediaComponent::refreshRendererStateInfo() {
	std::lock_guard<std::mutex> lock(owner.rendererMutex);
	owner.rendererStateInfo = buildRendererStateInfoLocked();
}

ofxVlc4::RendererStateInfo ofxVlc4::getRendererStateInfo() const {
	return mediaComponent->getRendererStateInfo();
}

ofxVlc4::RendererStateInfo ofxVlc4::MediaComponent::getRendererStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.rendererMutex);
	return buildRendererStateInfoLocked();
}

ofxVlc4::SubtitleStateInfo ofxVlc4::MediaComponent::buildSubtitleStateInfo(bool includeTrackDetails) const {
	SubtitleStateInfo info;
	info.delayMs = audio().getSubtitleDelayMs();
	info.textScale = audio().getSubtitleTextScale();
	info.teletextPage = video().getTeletextPage();
	info.teletextTransparencyEnabled = video().isTeletextTransparencyEnabled();
	info.teletextAvailable = info.teletextPage > 0;

	libvlc_media_player_t * player = owner.sessionPlayer();
	info.trackListAvailable = player != nullptr;
	if (!includeTrackDetails || !player) {
		return info;
	}

	info.tracks = getSubtitleTracks();
	info.trackCount = static_cast<int>(info.tracks.size());
	for (const MediaTrackInfo & track : info.tracks) {
		const std::string id = ofToLower(track.id);
		const std::string name = ofToLower(track.name);
		const std::string description = ofToLower(track.description);
		if (id.find("teletext") != std::string::npos ||
			name.find("teletext") != std::string::npos ||
			description.find("teletext") != std::string::npos) {
			info.teletextAvailable = true;
		}
	}
	for (const MediaTrackInfo & track : info.tracks) {
		if (!track.selected) {
			continue;
		}

		info.trackSelected = true;
		info.selectedTrackId = track.id;
		info.selectedTrackLabel = describeTrackLabel(track);
		break;
	}

	return info;
}

void ofxVlc4::MediaComponent::refreshSubtitleStateInfo() {
	std::lock_guard<std::mutex> lock(owner.subtitleStateMutex);
	owner.subtitleStateInfo = buildSubtitleStateInfo(true);
}

ofxVlc4::SubtitleStateInfo ofxVlc4::MediaComponent::getSubtitleStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.subtitleStateMutex);
	return owner.subtitleStateInfo;
}

ofxVlc4::NavigationStateInfo ofxVlc4::MediaComponent::buildNavigationStateInfo() const {
	NavigationStateInfo info;
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		info.available = true;
		info.currentProgramId = getSelectedProgramId();
		info.programCount = static_cast<int>(getPrograms().size());
		info.currentTitleIndex = getCurrentTitleIndex();
		info.titleCount = std::max(0, libvlc_media_player_get_title_count(player));
		info.currentChapterIndex = getCurrentChapterIndex();
		int chapterCount = -1;
		if (info.currentTitleIndex >= 0) {
			chapterCount = libvlc_media_player_get_chapter_count_for_title(player, info.currentTitleIndex);
		}
		if (chapterCount < 0) {
			chapterCount = libvlc_media_player_get_chapter_count(player);
		}
		info.chapterCount = std::max(0, chapterCount);
	}

	return info;
}

void ofxVlc4::MediaComponent::refreshNavigationStateInfo() {
	std::lock_guard<std::mutex> lock(owner.navigationStateMutex);
	owner.navigationStateInfo = buildNavigationStateInfo();
}

ofxVlc4::NavigationStateInfo ofxVlc4::MediaComponent::getNavigationStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.navigationStateMutex);
	return owner.navigationStateInfo;
}

ofxVlc4::SnapshotStateInfo ofxVlc4::MediaComponent::buildSnapshotStateInfoLocked() const {
	SnapshotStateInfo info;
	info.pending = owner.snapshotPending;
	info.available = owner.snapshotAvailable;
	info.lastRequestedPath = owner.pendingSnapshotPath;
	info.lastSavedPath = owner.lastSnapshotPath;
	info.lastSavedMetadataAvailable = owner.lastSnapshotBytes > 0 || !owner.lastSnapshotTimestamp.empty();
	info.lastSavedBytes = owner.lastSnapshotBytes;
	info.lastSavedTimestamp = owner.lastSnapshotTimestamp;
	info.lastEventMessage = owner.lastSnapshotEventMessage;
	info.lastFailureReason = owner.lastSnapshotFailureReason;
	return info;
}

ofxVlc4::NativeRecordingStateInfo ofxVlc4::MediaComponent::buildNativeRecordingStateInfoLocked() const {
	NativeRecordingStateInfo info;
	info.enabled = owner.nativeRecordingEnabled;
	info.active = owner.nativeRecordingActive.load();
	info.directory = owner.nativeRecordDirectory;
	info.lastOutputPathAvailable = !owner.lastNativeRecordedFilePath.empty();
	info.lastOutputPath = owner.lastNativeRecordedFilePath;
	info.lastOutputMetadataAvailable =
		owner.lastNativeRecordedFileBytes > 0 || !owner.lastNativeRecordedFileTimestamp.empty();
	info.lastOutputBytes = owner.lastNativeRecordedFileBytes;
	info.lastOutputTimestamp = owner.lastNativeRecordedFileTimestamp;
	info.lastEventMessage = owner.lastNativeRecordingEventMessage;
	info.lastFailureReason = owner.lastNativeRecordingFailureReason;
	return info;
}

void ofxVlc4::MediaComponent::setNativeRecordingStatusLocked(
	const std::string & eventMessage,
	const std::string & failureReason) {
	owner.lastNativeRecordingEventMessage = eventMessage;
	owner.lastNativeRecordingFailureReason = failureReason;
}

void ofxVlc4::MediaComponent::clearNativeRecordingOutputLocked() {
	owner.lastNativeRecordedFilePath.clear();
	owner.lastNativeRecordedFileBytes = 0;
	owner.lastNativeRecordedFileTimestamp.clear();
}

void ofxVlc4::MediaComponent::setNativeRecordingOutputLocked(
	const std::string & path,
	uint64_t bytes,
	const std::string & timestamp) {
	owner.lastNativeRecordedFilePath = path;
	owner.lastNativeRecordedFileBytes = bytes;
	owner.lastNativeRecordedFileTimestamp = timestamp;
}

std::vector<std::uintptr_t> ofxVlc4::MediaComponent::getActiveDialogTokensLocked() const {
	std::vector<std::uintptr_t> tokens;
	tokens.reserve(owner.activeDialogs.size());
	for (const DialogInfo & dialog : owner.activeDialogs) {
		tokens.push_back(dialog.token);
	}
	return tokens;
}

void ofxVlc4::MediaComponent::upsertActiveDialogLocked(const DialogInfo & dialog) {
	const auto it = std::find_if(
		owner.activeDialogs.begin(),
		owner.activeDialogs.end(),
		[&dialog](const DialogInfo & existing) { return existing.token == dialog.token; });
	if (it != owner.activeDialogs.end()) {
		*it = dialog;
	} else {
		owner.activeDialogs.push_back(dialog);
	}
}

void ofxVlc4::MediaComponent::removeActiveDialogLocked(std::uintptr_t token) {
	owner.activeDialogs.erase(
		std::remove_if(
			owner.activeDialogs.begin(),
			owner.activeDialogs.end(),
			[token](const DialogInfo & dialog) { return dialog.token == token; }),
		owner.activeDialogs.end());
}

void ofxVlc4::MediaComponent::clearLastDialogErrorLocked() {
	owner.lastDialogError = DialogErrorInfo {};
}

ofxVlc4::PlaybackStateInfo ofxVlc4::MediaComponent::getPlaybackStateInfo() const {
	PlaybackStateInfo state;
	const bool locallyStopped = playback().isPlaybackLocallyStopped();
	libvlc_media_player_t * player = owner.sessionPlayer();
	const bool mediaAttached = owner.sessionMedia() != nullptr;
	const bool hasMediaPlayer = player != nullptr;
	const libvlc_state_t playerState = hasMediaPlayer ? libvlc_media_player_get_state(player) : libvlc_Stopped;
	state.mode = playback().getPlaybackMode();
	state.shuffleEnabled = playback().isShuffleEnabled();
	state.mediaAttached = mediaAttached;
	state.playbackWanted = playback().isPlaybackWanted();
	state.pauseRequested = playback().isPauseRequested();
	state.playing = !locallyStopped && mediaAttached && hasMediaPlayer ? libvlc_media_player_is_playing(player) : false;
	state.stopped = locallyStopped ||
		!hasMediaPlayer ||
		!mediaAttached ||
		isStoppedOrIdleState(playerState);
	state.transitioning = !locallyStopped &&
		mediaAttached &&
		hasMediaPlayer &&
		isTransientPlaybackState(playerState);
	state.restartPending = !locallyStopped && playback().isPlaybackRestartPending();
	state.seekable = !locallyStopped && mediaAttached && hasMediaPlayer ? libvlc_media_player_is_seekable(player) : false;
	state.bufferCache = playback().getBufferCache();
	state.corked = playback().isCorked();
	state.position = playback().getPosition();
	state.timeMs = playback().getTime();
	state.lengthMs = playback().getLength();
	state.rate = audio().getPlaybackRate();
	state.watchTime = buildWatchTimeInfo();
	state.audio = audio().getAudioStateInfo();
	state.video = video().getVideoStateInfo();
	state.startupPrepared = state.video.startupPrepared;
	state.geometryKnown = state.video.geometryKnown;
	state.hasReceivedVideoFrame = state.video.frameReceived;

	{
		std::lock_guard<std::mutex> lock(owner.playbackStateMutex);
		state.snapshot = buildSnapshotStateInfoLocked();
		state.nativeRecording = buildNativeRecordingStateInfoLocked();
	}
	state.midi = owner.getMidiTransportInfo();

	return state;
}

ofxVlc4::MediaReadinessInfo ofxVlc4::MediaComponent::getMediaReadinessInfo() const {
	MediaReadinessInfo readiness;
	const AudioStateInfo audioState = audio().getAudioStateInfo();
	const VideoStateInfo videoState = video().getVideoStateInfo();
	libvlc_media_player_t * player = owner.sessionPlayer();
	const bool mediaAttached = owner.sessionMedia() != nullptr;
	const bool locallyStopped = playback().isPlaybackLocallyStopped();
	const bool hasMediaPlayer = player != nullptr;
	const SubtitleStateInfo subtitleState = getCachedSubtitleStateInfo();
	const NavigationStateInfo navigationState = getCachedNavigationStateInfo();

	readiness.mediaAttached = mediaAttached;
	readiness.startupPrepared = videoState.startupPrepared;
	readiness.geometryKnown = videoState.geometryKnown;
	readiness.videoLoaded = videoState.loaded;
	readiness.hasReceivedVideoFrame = videoState.frameReceived;
	readiness.audioReady = audioState.ready;
	readiness.playbackActive =
		!locallyStopped &&
		mediaAttached &&
		hasMediaPlayer &&
		libvlc_media_player_is_playing(player);
	const MediaParseInfo parseInfo = getCurrentMediaParseInfo();
	readiness.parseStatus = parseInfo.status;
	readiness.lastCompletedParseStatus = parseInfo.lastCompletedStatus;
	readiness.parseRequested = parseInfo.requested;
	readiness.parseActive = parseInfo.active;

	readiness.videoTrackCount = videoState.trackCount;
	readiness.audioTrackCount = audioState.trackCount;
	readiness.videoTracksReady = videoState.tracksAvailable || videoState.geometryKnown || videoState.loaded || videoState.hasVideoOutput;
	readiness.audioTracksReady = audioState.tracksAvailable || audioState.ready || audioState.audioPtsAvailable;
	readiness.subtitleTrackCount = static_cast<int>(subtitleState.trackCount);
	readiness.subtitleTracksReady = subtitleState.trackCount > 0;
	readiness.titleCount = navigationState.titleCount;
	readiness.chapterCount = navigationState.chapterCount;
	readiness.programCount = navigationState.programCount;
	readiness.navigationReady = navigationState.available &&
		(navigationState.titleCount > 0 || navigationState.chapterCount > 0 || navigationState.programCount > 0);
	return readiness;
}

void ofxVlc4::MediaComponent::prepareStartupPlaybackState() {
	owner.startupPlaybackStatePrepared.store(owner.sessionPlayer() != nullptr && owner.sessionMedia() != nullptr);
	owner.hasReceivedVideoFrame.store(false);
	refreshPrimaryTrackStateInfo();
	refreshSubtitleStateInfo();
	refreshNavigationStateInfo();
}

void ofxVlc4::MediaComponent::addToPlaylistInternal(const std::string & path, bool preloadMetadata) {
	mediaLibrary().addToPlaylistInternal(path, preloadMetadata);
}

void ofxVlc4::MediaComponent::addToPlaylist(const std::string & path) {
	mediaLibrary().addToPlaylist(path);
}

int ofxVlc4::MediaComponent::addPathToPlaylist(const std::string & path) {
	return mediaLibrary().addPathToPlaylist(path);
}

int ofxVlc4::MediaComponent::addPathToPlaylist(
	const std::string & path,
	std::initializer_list<std::string> extensions) {
	return mediaLibrary().addPathToPlaylist(path, extensions);
}

void ofxVlc4::MediaComponent::clearPlaylist() {
	playback().stop();
	mediaLibrary().clearPlaylistState();
	clearCurrentMedia();
	owner.setStatus("Playlist cleared.");
	owner.logNotice("Playlist cleared.");
}

void ofxVlc4::MediaComponent::removeFromPlaylist(int index) {
	const MediaLibrary::RemovePlaylistItemResult removal = mediaLibrary().removePlaylistItem(index);
	if (!removal.removed) {
		return;
	}

	const bool shouldPlayReplacement = removal.wasCurrent && playback().isPlaybackWanted();
	if (removal.wasCurrent) {
		playback().stop();
		audio().resetBuffer();
	}

	if (removal.playlistEmpty) {
		clearCurrentMedia();
		return;
	}
	if (removal.wasCurrent && removal.replacementIndex >= 0) {
		playback().activatePlaylistIndex(removal.replacementIndex, shouldPlayReplacement);
	}
}

void ofxVlc4::MediaComponent::movePlaylistItem(int fromIndex, int toIndex) {
	mediaLibrary().movePlaylistItem(fromIndex, toIndex);
}

void ofxVlc4::MediaComponent::movePlaylistItems(const std::vector<int> & fromIndices, int toIndex) {
	mediaLibrary().movePlaylistItems(fromIndices, toIndex);
}

void ofxVlc4::resetSubtitleStateInfo() {
	mediaComponent->resetSubtitleStateInfo();
}

void ofxVlc4::resetNavigationStateInfo() {
	mediaComponent->resetNavigationStateInfo();
}

void ofxVlc4::refreshPrimaryTrackStateInfo() {
	mediaComponent->refreshPrimaryTrackStateInfo();
}

void ofxVlc4::refreshSubtitleStateInfo() {
	mediaComponent->refreshSubtitleStateInfo();
}

ofxVlc4::SubtitleStateInfo ofxVlc4::getSubtitleStateInfo() const {
	return mediaComponent->getSubtitleStateInfo();
}

void ofxVlc4::refreshNavigationStateInfo() {
	mediaComponent->refreshNavigationStateInfo();
}

ofxVlc4::NavigationStateInfo ofxVlc4::getNavigationStateInfo() const {
	return mediaComponent->getNavigationStateInfo();
}

bool ofxVlc4::startRendererDiscovery(const std::string & discovererName) {
	return mediaComponent->startRendererDiscovery(discovererName);
}

bool ofxVlc4::MediaComponent::startRendererDiscovery(const std::string & discovererName) {
	const std::string trimmedName = trimWhitespace(discovererName);
	if (trimmedName.empty()) {
		stopRendererDiscovery();
		return true;
	}

	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		owner.setError("Initialize libvlc before starting renderer discovery.");
		return false;
	}

	if (owner.rendererDiscoverer && getSelectedRendererDiscovererName() == trimmedName) {
		refreshRendererStateInfo();
		return true;
	}

	if (getSelectedRendererDiscovererName() != trimmedName) {
		{
			std::lock_guard<std::mutex> lock(owner.rendererMutex);
			clearSelectedRendererLocked();
		}
		if (canApplyRendererImmediately()) {
			applySelectedRenderer();
		}
	}

	stopRendererDiscoveryInternal();

	owner.rendererDiscoverer = libvlc_renderer_discoverer_new(instance, trimmedName.c_str());
	owner.coreSession->setRendererDiscoverer(owner.rendererDiscoverer);
	if (!owner.rendererDiscoverer) {
		owner.setError("Renderer discovery could not be created.");
		return false;
	}

	owner.rendererDiscovererEventManager = libvlc_renderer_discoverer_event_manager(owner.rendererDiscoverer);
	if (owner.rendererDiscovererEventManager) {
		owner.coreSession->setRendererDiscovererEvents(owner.rendererDiscovererEventManager);
		if (owner.coreSession && owner.eventRouter) {
			owner.coreSession->attachRendererEvents(owner.eventRouter.get(), VlcEventRouter::rendererDiscovererEventStatic);
			owner.syncLegacyStateFromCoreSession();
		}
	}

	if (libvlc_renderer_discoverer_start(owner.rendererDiscoverer) != 0) {
		stopRendererDiscoveryInternal();
		refreshRendererStateInfo();
		owner.setError("Renderer discovery could not be started.");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		setRendererDiscovererNameLocked(trimmedName);
	}
	refreshRendererStateInfo();
	owner.setStatus("Renderer discovery started.");
	owner.logNotice("Renderer discovery started: " + trimmedName + ".");
	return true;
}

void ofxVlc4::stopRendererDiscovery() {
	mediaComponent->stopRendererDiscovery();
}

void ofxVlc4::MediaComponent::stopRendererDiscovery() {
	if (!owner.rendererDiscoverer && getSelectedRendererDiscovererName().empty()) {
		return;
	}

	stopRendererDiscoveryInternal();
	{
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		clearSelectedRendererLocked();
		setRendererDiscovererNameLocked("");
	}
	if (canApplyRendererImmediately()) {
		applySelectedRenderer();
	}
	refreshRendererStateInfo();
	owner.setStatus("Renderer discovery stopped.");
	owner.logNotice("Renderer discovery stopped.");
}

bool ofxVlc4::isRendererDiscoveryActive() const {
	return mediaComponent->isRendererDiscoveryActive();
}

std::vector<ofxVlc4::RendererInfo> ofxVlc4::getDiscoveredRenderers() const {
	return mediaComponent->getDiscoveredRenderers();
}

bool ofxVlc4::MediaComponent::isRendererDiscoveryActive() const {
	return owner.rendererDiscoverer != nullptr;
}

std::vector<ofxVlc4::RendererInfo> ofxVlc4::MediaComponent::getDiscoveredRenderers() const {
	std::lock_guard<std::mutex> lock(owner.rendererMutex);
	return buildDiscoveredRendererInfosLocked();
}

void ofxVlc4::MediaComponent::handleRendererDiscovererEvent(const libvlc_event_t * event) {
	if (!event) {
		return;
	}

	if (event->type == libvlc_RendererDiscovererItemAdded) {
		bool shouldReconnectSelectedRenderer = false;
		std::string rendererLabel;
		if (!handleRendererItemAdded(
				event->u.renderer_discoverer_item_added.item,
				rendererLabel,
				shouldReconnectSelectedRenderer)) {
			return;
		}

		refreshRendererStateInfo();
		if (shouldReconnectSelectedRenderer && owner.sessionPlayer()) {
			applySelectedRenderer();
			refreshRendererStateInfo();
			owner.setStatus("Selected renderer is available again.");
			owner.logNotice("Renderer available again: " + rendererLabel + ".");
		}
		return;
	}

	if (event->type == libvlc_RendererDiscovererItemDeleted) {
		bool removedSelectedRenderer = false;
		if (!handleRendererItemDeleted(
				event->u.renderer_discoverer_item_deleted.item,
				removedSelectedRenderer)) {
			return;
		}

		refreshRendererStateInfo();
		if (removedSelectedRenderer && owner.sessionPlayer()) {
			applySelectedRenderer();
			refreshRendererStateInfo();
			owner.setStatus("Selected renderer unavailable; using local output until it returns.");
			owner.logNotice("Selected renderer became unavailable; using local output.");
		}
	}
}

void ofxVlc4::MediaComponent::handleMediaEvent(const libvlc_event_t * event) {
	if (!event) {
		return;
	}

	if (event->type == libvlc_MediaParsedChanged && owner.sessionMedia()) {
		handleMediaParsedChanged(
			static_cast<libvlc_media_parsed_status_t>(event->u.media_parsed_changed.new_status));
		return;
	}

	if (event->type == libvlc_MediaAttachedThumbnailsFound) {
		handleAttachedThumbnailsFound(event->u.media_attached_thumbnails_found.thumbnails);
		return;
	}

	if (event->type == libvlc_MediaThumbnailGenerated) {
		handleThumbnailGenerated(event->u.media_thumbnail_generated.p_thumbnail);
	}
}

bool ofxVlc4::MediaComponent::handleRendererItemAdded(
	libvlc_renderer_item_t * item,
	std::string & rendererLabel,
	bool & shouldReconnectSelectedRenderer) {
	rendererLabel.clear();
	shouldReconnectSelectedRenderer = false;
	if (!item) {
		return false;
	}

	libvlc_renderer_item_t * heldItem = libvlc_renderer_item_hold(item);
	if (!heldItem) {
		return false;
	}

	RendererItemEntry entry;
	entry.name = trimWhitespace(libvlc_renderer_item_name(heldItem) ? libvlc_renderer_item_name(heldItem) : "");
	entry.type = trimWhitespace(libvlc_renderer_item_type(heldItem) ? libvlc_renderer_item_type(heldItem) : "");
	entry.iconUri = trimWhitespace(libvlc_renderer_item_icon_uri(heldItem) ? libvlc_renderer_item_icon_uri(heldItem) : "");
	const int flags = libvlc_renderer_item_flags(heldItem);
	entry.canAudio = (flags & LIBVLC_RENDERER_CAN_AUDIO) != 0;
	entry.canVideo = (flags & LIBVLC_RENDERER_CAN_VIDEO) != 0;
	entry.id = rendererStableId(owner.rendererDiscovererName, entry.name, entry.type, entry.iconUri, flags);
	entry.item = heldItem;

	{
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		if (findRendererEntryByIdLocked(entry.id)) {
			libvlc_renderer_item_release(heldItem);
			return false;
		}

		rendererLabel = entry.name.empty() ? entry.id : entry.name;
		shouldReconnectSelectedRenderer = !owner.selectedRendererId.empty() && entry.id == owner.selectedRendererId;
		owner.discoveredRenderers.push_back(std::move(entry));
	}

	return true;
}

bool ofxVlc4::MediaComponent::handleRendererItemDeleted(
	libvlc_renderer_item_t * item,
	bool & removedSelectedRenderer) {
	removedSelectedRenderer = false;
	if (!item) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		auto it = std::find_if(
			owner.discoveredRenderers.begin(),
			owner.discoveredRenderers.end(),
			[item](const RendererItemEntry & existing) { return existing.item == item; });
		if (it == owner.discoveredRenderers.end()) {
			return false;
		}

		removedSelectedRenderer = !owner.selectedRendererId.empty() && it->id == owner.selectedRendererId;
		if (it->item) {
			libvlc_renderer_item_release(it->item);
		}
		owner.discoveredRenderers.erase(it);
	}

	return true;
}

void ofxVlc4::mediaDiscovererMediaListEventStatic(const libvlc_event_t * event, void * data) {
	if (!data) {
		return;
	}

	static_cast<ofxVlc4 *>(data)->mediaDiscovererMediaListEvent(event);
}

void ofxVlc4::rendererDiscovererEventStatic(const libvlc_event_t * event, void * data) {
	if (!data) {
		return;
	}

	static_cast<ofxVlc4 *>(data)->rendererDiscovererEvent(event);
}

void ofxVlc4::vlcMediaEventStatic(const libvlc_event_t * event, void * data) {
	if (!data) {
		return;
	}

	static_cast<ofxVlc4 *>(data)->vlcMediaEvent(event);
}

void ofxVlc4::mediaDiscovererMediaListEvent(const libvlc_event_t * event) {
	mediaComponent->mediaDiscovererMediaListEvent(event);
}

void ofxVlc4::rendererDiscovererEvent(const libvlc_event_t * event) {
	mediaComponent->handleRendererDiscovererEvent(event);
}

void ofxVlc4::vlcMediaEvent(const libvlc_event_t * event) {
	mediaComponent->handleMediaEvent(event);
}

std::string ofxVlc4::getSelectedRendererId() const {
	return mediaComponent->getSelectedRendererId();
}

std::string ofxVlc4::MediaComponent::getSelectedRendererId() const {
	std::lock_guard<std::mutex> lock(owner.rendererMutex);
	return owner.selectedRendererId;
}

bool ofxVlc4::selectRenderer(const std::string & rendererId) {
	return mediaComponent->selectRenderer(rendererId);
}

bool ofxVlc4::MediaComponent::selectRenderer(const std::string & rendererId) {
	const std::string trimmedId = trimWhitespace(rendererId);
	if (trimmedId.empty()) {
		return clearRenderer();
	}

	RendererInfo selectedRenderer;
	bool foundRenderer = false;
	{
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		if (const RendererItemEntry * selectedEntry = findRendererEntryByIdLocked(trimmedId)) {
			selectedRenderer = buildRendererInfoLocked(*selectedEntry, true);
			foundRenderer = true;
		}
	}

	if (!foundRenderer) {
		owner.setError("Selected renderer is not currently available.");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		setSelectedRendererIdLocked(trimmedId);
	}
	const bool canApplyImmediately = canApplyRendererImmediately();
	if (canApplyImmediately) {
		applySelectedRenderer();
	}
	refreshRendererStateInfo();

	owner.setStatus(canApplyImmediately ? "Renderer selected." : "Renderer will apply on next play.");
	owner.logNotice("Renderer selected: " + rendererDisplayLabel(selectedRenderer) + ".");
	return true;
}

bool ofxVlc4::clearRenderer() {
	return mediaComponent->clearRenderer();
}

bool ofxVlc4::MediaComponent::clearRenderer() {
	{
		std::lock_guard<std::mutex> lock(owner.rendererMutex);
		clearSelectedRendererLocked();
	}
	const bool canApplyImmediately = canApplyRendererImmediately();
	if (canApplyImmediately) {
		applySelectedRenderer();
	}
	refreshRendererStateInfo();

	owner.setStatus(canApplyImmediately ? "Renderer reset to local output." : "Local output will apply on next play.");
	owner.logNotice("Renderer reset to local output.");
	return true;
}

std::vector<ofxVlc4::MediaSlaveInfo> ofxVlc4::MediaComponent::getMediaSlaves() const {
	std::vector<MediaSlaveInfo> slaves;
	libvlc_media_t * currentMedia = owner.sessionMedia();
	if (!currentMedia) {
		return slaves;
	}

	libvlc_media_slave_t ** slaveArray = nullptr;
	const unsigned int slaveCount = libvlc_media_slaves_get(currentMedia, &slaveArray);
	if (slaveCount == 0 || !slaveArray) {
		return slaves;
	}

	slaves.reserve(slaveCount);
	for (unsigned int slaveIndex = 0; slaveIndex < slaveCount; ++slaveIndex) {
		const libvlc_media_slave_t * slave = slaveArray[slaveIndex];
		if (!slave) {
			continue;
		}

		MediaSlaveInfo info;
		info.uri = slave->psz_uri ? slave->psz_uri : "";
		info.type = (slave->i_type == libvlc_media_slave_type_subtitle)
			? MediaSlaveType::Subtitle
			: MediaSlaveType::Audio;
		info.priority = slave->i_priority;
		slaves.push_back(std::move(info));
	}

	libvlc_media_slaves_release(slaveArray, slaveCount);
	return slaves;
}

bool ofxVlc4::MediaComponent::addMediaSlave(MediaSlaveType type, const std::string & uri, unsigned priority) {
	libvlc_media_t * currentMedia = owner.sessionMedia();
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!currentMedia) {
		owner.setError("Load media before adding a media slave.");
		return false;
	}

	const std::string slaveUri = normalizeMediaSlaveUri(uri);
	if (slaveUri.empty()) {
		owner.setError("Media slave path is empty.");
		return false;
	}

	const unsigned clampedPriority = std::min(priority, 4u);
	if (libvlc_media_slaves_add(currentMedia, toLibvlcMediaSlaveType(type), clampedPriority, slaveUri.c_str()) != 0) {
		owner.setError("Media slave could not be added.");
		return false;
	}

	if (player) {
		libvlc_media_player_add_slave(player, toLibvlcMediaSlaveType(type), slaveUri.c_str(), true);
	}

	owner.setStatus(std::string("Media slave added: ") + slaveUri);
	owner.logNotice(std::string("Media slave added: ") + mediaSlaveTypeLabel(type) + " " + slaveUri + ".");
	return true;
}

void ofxVlc4::MediaComponent::clearMediaSlaves() {
	libvlc_media_t * currentMedia = owner.sessionMedia();
	if (!currentMedia) {
		return;
	}

	libvlc_media_slaves_clear(currentMedia);
	owner.setStatus("Media slaves cleared.");
	owner.logNotice("Media slaves cleared.");
}

std::vector<ofxVlc4::MediaSlaveInfo> ofxVlc4::getMediaSlaves() const {
	return mediaComponent->getMediaSlaves();
}

bool ofxVlc4::addMediaSlave(MediaSlaveType type, const std::string & uri, unsigned priority) {
	return mediaComponent->addMediaSlave(type, uri, priority);
}

bool ofxVlc4::addSubtitleSlave(const std::string & uri, unsigned priority) {
	return addMediaSlave(MediaSlaveType::Subtitle, uri, priority);
}

bool ofxVlc4::addAudioSlave(const std::string & uri, unsigned priority) {
	return addMediaSlave(MediaSlaveType::Audio, uri, priority);
}

void ofxVlc4::clearMediaSlaves() {
	mediaComponent->clearMediaSlaves();
}

std::string ofxVlc4::takeSnapshot(const std::string & directory) {
	libvlc_media_player_t * player = sessionPlayer();
	if (!player) {
		updateSnapshotFailureState("No media player available.");
		setError("No media player available for snapshot.");
		return "";
	}

	const std::filesystem::path snapshotDirectory = directory.empty()
		? std::filesystem::path(ofToDataPath("snapshots", true))
		: std::filesystem::path(directory);
	std::error_code ec;
	std::filesystem::create_directories(snapshotDirectory, ec);
	if (ec) {
		updateSnapshotFailureState("Failed to create snapshot directory.");
		setError("Failed to create snapshot directory.");
		return "";
	}

	std::string fileStem = sanitizeFileStem(ofFilePath::removeExt(mediaLabelForPath(getCurrentFileName())));
	if (fileStem == "snapshot") {
		fileStem = sanitizeFileStem(ofFilePath::removeExt(mediaLabelForPath(getCurrentPath())));
	}
	const std::filesystem::path outputPath =
		snapshotDirectory / (fileStem + "_" + ofGetTimestampString("%Y%m%d_%H%M%S") + ".png");

	updateSnapshotStateOnRequest(outputPath.string());

	if (libvlc_video_take_snapshot(player, 0, outputPath.string().c_str(), 0, 0) != 0) {
		updateSnapshotFailureState("Snapshot could not be captured.");
		setError("Snapshot could not be captured.");
		return "";
	}

	setStatus("Snapshot requested: " + outputPath.string());
	logNotice("Snapshot requested: " + outputPath.string());
	return outputPath.string();
}

ofxVlc4::ThumbnailInfo ofxVlc4::getLastGeneratedThumbnail() const {
	return mediaComponent->getLastGeneratedThumbnail();
}

bool ofxVlc4::requestThumbnailByTime(
	int timeMs,
	unsigned width,
	unsigned height,
	bool crop,
	ThumbnailImageType type,
	ThumbnailSeekSpeed speed,
	int timeoutMs) {
	return mediaComponent->requestThumbnailByTime(timeMs, width, height, crop, type, speed, timeoutMs);
}

bool ofxVlc4::requestThumbnailByPosition(
	float position,
	unsigned width,
	unsigned height,
	bool crop,
	ThumbnailImageType type,
	ThumbnailSeekSpeed speed,
	int timeoutMs) {
	return mediaComponent->requestThumbnailByPosition(position, width, height, crop, type, speed, timeoutMs);
}

void ofxVlc4::cancelThumbnailRequest() {
	mediaComponent->cancelThumbnailRequest();
}

void ofxVlc4::dialogDisplayLoginStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	const char * defaultUsername,
	bool askStore) {
	if (!data || !id) {
		return;
	}

	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Login;
	dialog.title = title ? title : "";
	dialog.text = text ? text : "";
	dialog.defaultUsername = defaultUsername ? defaultUsername : "";
	dialog.askStore = askStore;
	dialog.cancellable = true;
	static_cast<ofxVlc4 *>(data)->upsertDialog(dialog);
}

void ofxVlc4::dialogDisplayQuestionStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	libvlc_dialog_question_type type,
	const char * cancel,
	const char * action1,
	const char * action2) {
	if (!data || !id) {
		return;
	}

	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Question;
	dialog.severity = toDialogQuestionSeverity(type);
	dialog.title = title ? title : "";
	dialog.text = text ? text : "";
	dialog.cancelLabel = cancel ? cancel : "";
	dialog.action1Label = action1 ? action1 : "";
	dialog.action2Label = action2 ? action2 : "";
	dialog.cancellable = !dialog.cancelLabel.empty();
	static_cast<ofxVlc4 *>(data)->upsertDialog(dialog);
}

void ofxVlc4::dialogDisplayProgressStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	bool indeterminate,
	float position,
	const char * cancel) {
	if (!data || !id) {
		return;
	}

	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Progress;
	dialog.title = title ? title : "";
	dialog.text = text ? text : "";
	dialog.progressIndeterminate = indeterminate;
	dialog.progressPosition = ofClamp(position, 0.0f, 1.0f);
	dialog.cancelLabel = cancel ? cancel : "";
	dialog.cancellable = !dialog.cancelLabel.empty();
	static_cast<ofxVlc4 *>(data)->upsertDialog(dialog);
}

void ofxVlc4::dialogCancelStatic(void * data, libvlc_dialog_id * id) {
	if (!data || !id) {
		return;
	}

	ofxVlc4 * owner = static_cast<ofxVlc4 *>(data);
	const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(id);
	owner->removeDialog(token);
	libvlc_dialog_dismiss(id);
}

void ofxVlc4::dialogUpdateProgressStatic(void * data, libvlc_dialog_id * id, float position, const char * text) {
	if (!data || !id) {
		return;
	}

	ofxVlc4 * owner = static_cast<ofxVlc4 *>(data);
	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Progress;
	dialog.progressPosition = ofClamp(position, 0.0f, 1.0f);
	dialog.text = text ? text : "";

	{
		std::lock_guard<std::mutex> lock(owner->dialogMutex);
		const auto it = std::find_if(
			owner->activeDialogs.begin(),
			owner->activeDialogs.end(),
			[&dialog](const DialogInfo & existing) { return existing.token == dialog.token; });
		if (it != owner->activeDialogs.end()) {
			dialog.title = it->title;
			dialog.cancelLabel = it->cancelLabel;
			dialog.cancellable = it->cancellable;
			dialog.progressIndeterminate = it->progressIndeterminate;
			*it = dialog;
			return;
		}
	}

	owner->upsertDialog(dialog);
}

void ofxVlc4::dialogErrorStatic(void * data, const char * title, const char * text) {
	if (!data) {
		return;
	}

	ofxVlc4 * owner = static_cast<ofxVlc4 *>(data);
	{
		std::lock_guard<std::mutex> lock(owner->dialogMutex);
		owner->lastDialogError.available = true;
		owner->lastDialogError.title = title ? title : "";
		owner->lastDialogError.text = text ? text : "";
	}

	if (text && *text) {
		owner->setError(text);
	}
}

void ofxVlc4::resetCurrentMediaParseState() {
	mediaLibraryController->resetCurrentMediaParseState();
}

libvlc_media_t * ofxVlc4::retainCurrentOrLoadedMedia() const {
	return mediaLibraryController->retainCurrentOrLoadedMedia();
}

ofxVlc4::MediaParseInfo ofxVlc4::getCurrentMediaParseInfo() const {
	return mediaComponent->getCurrentMediaParseInfo();
}

ofxVlc4::MediaParseStatus ofxVlc4::getCurrentMediaParseStatus() const {
	return mediaComponent->getCurrentMediaParseStatus();
}

ofxVlc4::MediaParseOptions ofxVlc4::getMediaParseOptions() const {
	return mediaComponent->getMediaParseOptions();
}

void ofxVlc4::setMediaParseOptions(const MediaParseOptions & options) {
	mediaComponent->setMediaParseOptions(options);
}

std::string ofxVlc4::getCurrentMediaMeta(MediaMetaField field) const {
	return mediaComponent->getCurrentMediaMeta(field);
}

bool ofxVlc4::setCurrentMediaMeta(MediaMetaField field, const std::string & value) {
	return mediaComponent->setCurrentMediaMeta(field, value);
}

bool ofxVlc4::saveCurrentMediaMeta() {
	return mediaComponent->saveCurrentMediaMeta();
}

std::vector<std::string> ofxVlc4::getCurrentMediaMetaExtraNames() const {
	return mediaComponent->getCurrentMediaMetaExtraNames();
}

std::string ofxVlc4::getCurrentMediaMetaExtra(const std::string & name) const {
	return mediaComponent->getCurrentMediaMetaExtra(name);
}

bool ofxVlc4::setCurrentMediaMetaExtra(const std::string & name, const std::string & value) {
	return mediaComponent->setCurrentMediaMetaExtra(name, value);
}

bool ofxVlc4::removeCurrentMediaMetaExtra(const std::string & name) {
	return mediaComponent->removeCurrentMediaMetaExtra(name);
}

std::vector<ofxVlc4::BookmarkInfo> ofxVlc4::getBookmarksForPath(const std::string & path) const {
	return mediaComponent->getBookmarksForPath(path);
}

std::vector<ofxVlc4::BookmarkInfo> ofxVlc4::getCurrentBookmarks() const {
	return mediaComponent->getCurrentBookmarks();
}

bool ofxVlc4::addBookmarkAtTime(int timeMs, const std::string & label) {
	return mediaComponent->addBookmarkAtTime(timeMs, label);
}

bool ofxVlc4::addCurrentBookmark(const std::string & label) {
	return mediaComponent->addCurrentBookmark(label);
}

bool ofxVlc4::seekToBookmark(const std::string & bookmarkId) {
	return mediaComponent->seekToBookmark(bookmarkId);
}

bool ofxVlc4::removeBookmark(const std::string & bookmarkId) {
	return mediaComponent->removeBookmark(bookmarkId);
}

void ofxVlc4::clearBookmarksForPath(const std::string & path) {
	mediaComponent->clearBookmarksForPath(path);
}

void ofxVlc4::clearCurrentBookmarks() {
	mediaComponent->clearCurrentBookmarks();
}

std::string ofxVlc4::getPathAtIndex(int index) const {
	return mediaComponent->getPathAtIndex(index);
}

std::vector<ofxVlc4::PlaylistItemInfo> ofxVlc4::getPlaylistItems() const {
	return mediaComponent->getPlaylistItems();
}

ofxVlc4::PlaylistStateInfo ofxVlc4::getPlaylistStateInfo() const {
	return mediaComponent->getPlaylistStateInfo();
}

ofxVlc4::PlaylistItemInfo ofxVlc4::getCurrentPlaylistItemInfo() const {
	return mediaComponent->getCurrentPlaylistItemInfo();
}

std::string ofxVlc4::getFileNameAtIndex(int index) const {
	return mediaComponent->getFileNameAtIndex(index);
}

std::string ofxVlc4::getCurrentPath() const {
	return mediaComponent->getCurrentPath();
}

int ofxVlc4::getCurrentIndex() const {
	return mediaComponent->getCurrentIndex();
}

std::string ofxVlc4::getCurrentFileName() const {
	return mediaComponent->getCurrentFileName();
}

std::vector<std::pair<std::string, std::string>> ofxVlc4::getMetadataAtIndex(int index) const {
	return mediaComponent->getMetadataAtIndex(index);
}

std::vector<std::pair<std::string, std::string>> ofxVlc4::getCurrentMetadata() const {
	return mediaComponent->getCurrentMetadata();
}

bool ofxVlc4::hasPlaylist() const {
	return mediaComponent->hasPlaylist();
}

const std::string & ofxVlc4::getLastStatusMessage() const {
	return mediaComponent->getLastStatusMessage();
}

const std::string & ofxVlc4::getLastErrorMessage() const {
	return mediaComponent->getLastErrorMessage();
}

void ofxVlc4::clearLastMessages() {
	mediaComponent->clearLastMessages();
}

bool ofxVlc4::requestCurrentMediaParse() {
	return mediaComponent->requestCurrentMediaParse();
}

void ofxVlc4::stopCurrentMediaParse() {
	mediaComponent->stopCurrentMediaParse();
}

ofxVlc4::PlaybackStateInfo ofxVlc4::getPlaybackStateInfo() const {
	return mediaComponent->getPlaybackStateInfo();
}

ofxVlc4::MediaReadinessInfo ofxVlc4::getMediaReadinessInfo() const {
	return mediaComponent->getMediaReadinessInfo();
}

void ofxVlc4::updateSnapshotStateOnRequest(const std::string & requestedPath) {
	mediaComponent->updateSnapshotStateOnRequest(requestedPath);
}

void ofxVlc4::updateSnapshotStateFromEvent(const std::string & savedPath) {
	mediaComponent->updateSnapshotStateFromEvent(savedPath);
}

void ofxVlc4::clearPendingSnapshotState() {
	mediaComponent->clearPendingSnapshotState();
}

void ofxVlc4::updateSnapshotFailureState(const std::string & failureReason) {
	mediaComponent->updateSnapshotFailureState(failureReason);
}

void ofxVlc4::updateNativeRecordingStateFromEvent(bool active, const std::string & recordedFilePath) {
	mediaComponent->updateNativeRecordingStateFromEvent(active, recordedFilePath);
}

void ofxVlc4::updateNativeRecordingFailureState(const std::string & failureReason) {
	mediaComponent->updateNativeRecordingFailureState(failureReason);
}

ofxVlc4::MediaStats ofxVlc4::MediaComponent::getMediaStats() const {
	return mediaLibrary().getMediaStats();
}

std::string ofxVlc4::MediaComponent::getPathAtIndex(int index) const {
	return mediaLibrary().getPathAtIndex(index);
}

std::vector<ofxVlc4::PlaylistItemInfo> ofxVlc4::MediaComponent::getPlaylistItems() const {
	return mediaLibrary().getPlaylistItems();
}

ofxVlc4::PlaylistStateInfo ofxVlc4::MediaComponent::getPlaylistStateInfo() const {
	return mediaLibrary().getPlaylistStateInfo();
}

ofxVlc4::PlaylistItemInfo ofxVlc4::MediaComponent::getCurrentPlaylistItemInfo() const {
	return mediaLibrary().getCurrentPlaylistItemInfo();
}

std::string ofxVlc4::MediaComponent::getFileNameAtIndex(int index) const {
	return mediaLibrary().getFileNameAtIndex(index);
}

std::string ofxVlc4::MediaComponent::getCurrentPath() const {
	return mediaLibrary().getCurrentPath();
}

int ofxVlc4::MediaComponent::getCurrentIndex() const {
	return mediaLibrary().getCurrentIndex();
}

std::string ofxVlc4::MediaComponent::getCurrentFileName() const {
	return mediaLibrary().getCurrentFileName();
}

std::vector<std::pair<std::string, std::string>> ofxVlc4::MediaComponent::getMetadataAtIndex(int index) const {
	return mediaLibrary().getMetadataAtIndex(index);
}

std::vector<std::pair<std::string, std::string>> ofxVlc4::MediaComponent::getCurrentMetadata() const {
	return mediaLibrary().getCurrentMetadata();
}

bool ofxVlc4::MediaComponent::hasPlaylist() const {
	return mediaLibrary().hasPlaylist();
}

std::vector<ofxVlc4::ProgramInfo> ofxVlc4::MediaComponent::getPrograms() const {
	std::vector<ProgramInfo> programs;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return programs;
	}

	libvlc_media_t * sourceMedia = retainCurrentPlayerMedia(player);
	if (!sourceMedia) {
		return programs;
	}
	libvlc_media_release(sourceMedia);

	libvlc_player_programlist_t * programList = libvlc_media_player_get_programlist(player);
	if (!programList) {
		return programs;
	}

	const size_t programCount = libvlc_player_programlist_count(programList);
	programs.reserve(programCount);
	for (size_t programIndex = 0; programIndex < programCount; ++programIndex) {
		const libvlc_player_program_t * program = libvlc_player_programlist_at(programList, programIndex);
		if (!program) {
			continue;
		}

		ProgramInfo info;
		info.id = program->i_group_id;
		info.name = formatProgramName(program->i_group_id, program->psz_name ? program->psz_name : "");
		info.current = program->b_selected;
		info.scrambled = program->b_scrambled;
		programs.push_back(std::move(info));
	}

	libvlc_player_programlist_delete(programList);
	return programs;
}

int ofxVlc4::MediaComponent::getSelectedProgramId() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return -1;
	}

	libvlc_player_program_t * program = libvlc_media_player_get_selected_program(player);
	if (!program) {
		return -1;
	}

	const int selectedProgramId = program->i_group_id;
	libvlc_player_program_delete(program);
	return selectedProgramId;
}

bool ofxVlc4::MediaComponent::selectProgramId(int programId) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	libvlc_player_program_t * program = libvlc_media_player_get_program_from_id(player, programId);
	if (!program) {
		return false;
	}

	const std::string programName = formatProgramName(programId, program->psz_name ? program->psz_name : "");
	libvlc_player_program_delete(program);

	libvlc_media_player_select_program_id(player, programId);
	refreshNavigationStateInfo();
	owner.setStatus("Program set.");
	owner.logNotice("Program set: " + programName + ".");
	return true;
}

std::vector<ofxVlc4::ProgramInfo> ofxVlc4::getPrograms() const {
	return mediaComponent->getPrograms();
}

int ofxVlc4::getSelectedProgramId() const {
	return mediaComponent->getSelectedProgramId();
}

bool ofxVlc4::selectProgramId(int programId) {
	return mediaComponent->selectProgramId(programId);
}

ofxVlc4::MediaStats ofxVlc4::getMediaStats() const {
	return mediaComponent->getMediaStats();
}

bool ofxVlc4::canPause() const {
	return playbackController->canPause();
}

unsigned ofxVlc4::getVideoOutputCount() const {
	return videoComponent->getVideoOutputCount();
}

bool ofxVlc4::hasVideoOutput() const {
	return videoComponent->hasVideoOutput();
}

bool ofxVlc4::isScrambled() const {
	return videoComponent->isScrambled();
}

ofxVlc4::AbLoopInfo ofxVlc4::MediaComponent::getAbLoop() const {
	AbLoopInfo info;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		if (owner.pendingAbLoopStartPosition >= 0.0f) {
			info.state = AbLoopInfo::State::A;
			info.aTimeMs = owner.pendingAbLoopStartTimeMs;
			info.aPosition = owner.pendingAbLoopStartPosition;
		}
		return info;
	}

	libvlc_time_t aTime = -1;
	libvlc_time_t bTime = -1;
	double aPosition = -1.0;
	double bPosition = -1.0;
	const libvlc_abloop_t state = libvlc_media_player_get_abloop(player, &aTime, &aPosition, &bTime, &bPosition);
	info.state = toAbLoopState(state);
	info.aTimeMs = static_cast<int64_t>(aTime);
	info.aPosition = static_cast<float>(aPosition);
	info.bTimeMs = static_cast<int64_t>(bTime);
	info.bPosition = static_cast<float>(bPosition);

	if (info.state == AbLoopInfo::State::None && owner.pendingAbLoopStartPosition >= 0.0f) {
		info.state = AbLoopInfo::State::A;
		info.aTimeMs = owner.pendingAbLoopStartTimeMs;
		info.aPosition = owner.pendingAbLoopStartPosition;
	}

	return info;
}

bool ofxVlc4::MediaComponent::setAbLoopA() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	owner.pendingAbLoopStartTimeMs = static_cast<int64_t>(playback().getTime());
	owner.pendingAbLoopStartPosition = playback().getPosition();
	libvlc_media_player_reset_abloop(player);
	owner.setStatus("A-B loop A set.");
	owner.logNotice("A-B loop A set.");
	return true;
}

bool ofxVlc4::MediaComponent::setAbLoopB() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || owner.pendingAbLoopStartPosition < 0.0f) {
		return false;
	}

	const int64_t endTimeMs = static_cast<int64_t>(playback().getTime());
	const float endPosition = playback().getPosition();
	if (endPosition <= owner.pendingAbLoopStartPosition) {
		owner.setError("A-B loop end must be after the start.");
		return false;
	}

	const int result = (owner.pendingAbLoopStartTimeMs >= 0 && endTimeMs >= 0)
		? libvlc_media_player_set_abloop_time(player, owner.pendingAbLoopStartTimeMs, endTimeMs)
		: libvlc_media_player_set_abloop_position(player, owner.pendingAbLoopStartPosition, endPosition);
	if (result != 0) {
		owner.setError("A-B loop could not be applied.");
		return false;
	}

	owner.pendingAbLoopStartTimeMs = -1;
	owner.pendingAbLoopStartPosition = -1.0f;
	owner.setStatus("A-B loop enabled.");
	owner.logNotice("A-B loop enabled.");
	return true;
}

void ofxVlc4::MediaComponent::clearAbLoop() {
	owner.pendingAbLoopStartTimeMs = -1;
	owner.pendingAbLoopStartPosition = -1.0f;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (player) {
		libvlc_media_player_reset_abloop(player);
	}
	owner.setStatus("A-B loop cleared.");
	owner.logNotice("A-B loop cleared.");
}

ofxVlc4::AbLoopInfo ofxVlc4::getAbLoop() const {
	return mediaComponent->getAbLoop();
}

bool ofxVlc4::setAbLoopA() {
	return mediaComponent->setAbLoopA();
}

bool ofxVlc4::setAbLoopB() {
	return mediaComponent->setAbLoopB();
}

void ofxVlc4::clearAbLoop() {
	mediaComponent->clearAbLoop();
}

std::vector<ofxVlc4::TitleInfo> ofxVlc4::MediaComponent::getTitles() const {
	std::vector<TitleInfo> titles;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return titles;
	}

	libvlc_media_t * sourceMedia = retainCurrentPlayerMedia(player);
	if (!sourceMedia) {
		return titles;
	}
	libvlc_media_release(sourceMedia);

	libvlc_title_description_t ** titleDescriptions = nullptr;
	const int titleCount = libvlc_media_player_get_full_title_descriptions(player, &titleDescriptions);
	if (titleCount <= 0 || !titleDescriptions) {
		return titles;
	}

	const int currentTitleIndex = libvlc_media_player_get_title(player);
	titles.reserve(static_cast<size_t>(titleCount));
	for (int titleIndex = 0; titleIndex < titleCount; ++titleIndex) {
		const libvlc_title_description_t * title = titleDescriptions[titleIndex];
		if (!title) {
			continue;
		}

		TitleInfo info;
		info.index = titleIndex;
		info.name = fallbackIndexedLabel("Title", titleIndex, title->psz_name ? title->psz_name : "");
		info.durationMs = title->i_duration;
		info.current = titleIndex == currentTitleIndex;
		info.isMenu = (title->i_flags & libvlc_title_menu) != 0;
		info.isInteractive = (title->i_flags & libvlc_title_interactive) != 0;
		titles.push_back(std::move(info));
	}

	libvlc_title_descriptions_release(titleDescriptions, static_cast<unsigned>(titleCount));
	return titles;
}

int ofxVlc4::MediaComponent::getCurrentTitleIndex() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	return player ? libvlc_media_player_get_title(player) : -1;
}

bool ofxVlc4::MediaComponent::selectTitleIndex(int index) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || index < 0) {
		return false;
	}

	libvlc_media_player_set_title(player, index);
	refreshNavigationStateInfo();
	owner.setStatus("Title set.");
	owner.logNotice("Title set: " + ofToString(index + 1) + ".");
	return true;
}

std::vector<ofxVlc4::TitleInfo> ofxVlc4::getTitles() const {
	return mediaComponent->getTitles();
}

int ofxVlc4::getCurrentTitleIndex() const {
	return mediaComponent->getCurrentTitleIndex();
}

bool ofxVlc4::selectTitleIndex(int index) {
	return mediaComponent->selectTitleIndex(index);
}

std::vector<ofxVlc4::ChapterInfo> ofxVlc4::MediaComponent::getChapters(int titleIndex) const {
	std::vector<ChapterInfo> chapters;
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return chapters;
	}

	libvlc_media_t * sourceMedia = retainCurrentPlayerMedia(player);
	if (!sourceMedia) {
		return chapters;
	}
	libvlc_media_release(sourceMedia);

	libvlc_chapter_description_t ** chapterDescriptions = nullptr;
	const int chapterCount = libvlc_media_player_get_full_chapter_descriptions(player, titleIndex, &chapterDescriptions);
	if (chapterCount <= 0 || !chapterDescriptions) {
		return chapters;
	}

	const int currentChapterIndex = libvlc_media_player_get_chapter(player);
	chapters.reserve(static_cast<size_t>(chapterCount));
	for (int chapterIndex = 0; chapterIndex < chapterCount; ++chapterIndex) {
		const libvlc_chapter_description_t * chapter = chapterDescriptions[chapterIndex];
		if (!chapter) {
			continue;
		}

		ChapterInfo info;
		info.index = chapterIndex;
		info.name = fallbackIndexedLabel("Chapter", chapterIndex, chapter->psz_name ? chapter->psz_name : "");
		info.timeOffsetMs = chapter->i_time_offset;
		info.durationMs = chapter->i_duration;
		info.current = chapterIndex == currentChapterIndex;
		chapters.push_back(std::move(info));
	}

	libvlc_chapter_descriptions_release(chapterDescriptions, static_cast<unsigned>(chapterCount));
	return chapters;
}

int ofxVlc4::MediaComponent::getCurrentChapterIndex() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	return player ? libvlc_media_player_get_chapter(player) : -1;
}

bool ofxVlc4::MediaComponent::selectChapterIndex(int index) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || index < 0) {
		return false;
	}

	libvlc_media_player_set_chapter(player, index);
	refreshNavigationStateInfo();
	owner.setStatus("Chapter set.");
	owner.logNotice("Chapter set: " + ofToString(index + 1) + ".");
	return true;
}

std::vector<ofxVlc4::ChapterInfo> ofxVlc4::getChapters(int titleIndex) const {
	return mediaComponent->getChapters(titleIndex);
}

int ofxVlc4::getCurrentChapterIndex() const {
	return mediaComponent->getCurrentChapterIndex();
}

bool ofxVlc4::selectChapterIndex(int index) {
	return mediaComponent->selectChapterIndex(index);
}

void ofxVlc4::MediaComponent::previousChapter() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_previous_chapter(player);
	refreshNavigationStateInfo();
	owner.setStatus("Previous chapter.");
	owner.logNotice("Previous chapter.");
}

void ofxVlc4::MediaComponent::nextChapter() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_next_chapter(player);
	refreshNavigationStateInfo();
	owner.setStatus("Next chapter.");
	owner.logNotice("Next chapter.");
}

void ofxVlc4::MediaComponent::nextFrame() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_next_frame(player);
	owner.setStatus("Advanced one frame.");
	owner.logVerbose("Advanced one frame.");
}

void ofxVlc4::MediaComponent::navigate(NavigationMode mode) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_navigate(player, toLibvlcNavigateMode(mode));
	owner.setStatus("Menu navigation sent.");
	owner.logVerbose("Menu navigation sent.");
}

void ofxVlc4::previousChapter() {
	mediaComponent->previousChapter();
}

void ofxVlc4::nextChapter() {
	mediaComponent->nextChapter();
}

void ofxVlc4::nextFrame() {
	mediaComponent->nextFrame();
}

void ofxVlc4::navigate(NavigationMode mode) {
	mediaComponent->navigate(mode);
}

bool ofxVlc4::executePlayerCommand(PlayerCommand command) {
	const auto seekByMilliseconds = [this](int deltaMs) {
		const int currentTimeMs = std::max(0, getTime());
		setTime(std::max(0, currentTimeMs + deltaMs));
	};

	switch (command) {
	case PlayerCommand::PlayPause:
		if (isPlaying()) {
			pause();
		} else {
			play();
		}
		return true;
	case PlayerCommand::Play:
		play();
		return true;
	case PlayerCommand::Pause:
		pause();
		return true;
	case PlayerCommand::Stop:
		stop();
		return true;
	case PlayerCommand::NextItem:
		nextMediaListItem();
		return true;
	case PlayerCommand::PreviousItem:
		previousMediaListItem();
		return true;
	case PlayerCommand::SeekForwardSmall:
		seekByMilliseconds(5000);
		return true;
	case PlayerCommand::SeekBackwardSmall:
		seekByMilliseconds(-5000);
		return true;
	case PlayerCommand::SeekForwardLarge:
		seekByMilliseconds(30000);
		return true;
	case PlayerCommand::SeekBackwardLarge:
		seekByMilliseconds(-30000);
		return true;
	case PlayerCommand::VolumeUp:
		setVolume(getVolume() + 5);
		return true;
	case PlayerCommand::VolumeDown:
		setVolume(getVolume() - 5);
		return true;
	case PlayerCommand::ToggleMute:
		toggleMute();
		return true;
	case PlayerCommand::NextFrame:
		nextFrame();
		return true;
	case PlayerCommand::PreviousChapter:
		previousChapter();
		return true;
	case PlayerCommand::NextChapter:
		nextChapter();
		return true;
	case PlayerCommand::MenuActivate:
		navigate(NavigationMode::Activate);
		return true;
	case PlayerCommand::MenuUp:
		navigate(NavigationMode::Up);
		return true;
	case PlayerCommand::MenuDown:
		navigate(NavigationMode::Down);
		return true;
	case PlayerCommand::MenuLeft:
		navigate(NavigationMode::Left);
		return true;
	case PlayerCommand::MenuRight:
		navigate(NavigationMode::Right);
		return true;
	case PlayerCommand::MenuPopup:
		navigate(NavigationMode::Popup);
		return true;
	case PlayerCommand::TeletextRed:
		sendTeletextKey(TeletextKey::Red);
		return true;
	case PlayerCommand::TeletextGreen:
		sendTeletextKey(TeletextKey::Green);
		return true;
	case PlayerCommand::TeletextYellow:
		sendTeletextKey(TeletextKey::Yellow);
		return true;
	case PlayerCommand::TeletextBlue:
		sendTeletextKey(TeletextKey::Blue);
		return true;
	case PlayerCommand::TeletextIndex:
		sendTeletextKey(TeletextKey::Index);
		return true;
	case PlayerCommand::ToggleTeletextTransparency:
		setTeletextTransparencyEnabled(!isTeletextTransparencyEnabled());
		return true;
	}

	return false;
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::MediaComponent::getTrackInfos(libvlc_track_type_t type) const {
	std::vector<MediaTrackInfo> trackInfos;
	libvlc_media_player_t * player = owner.sessionPlayer();
	libvlc_media_t * sourceMedia = nullptr;
	libvlc_media_tracklist_t * tracklist = nullptr;
	if (player) {
		tracklist = libvlc_media_player_get_tracklist(player, type, false);
	}
	if (!tracklist) {
		sourceMedia = mediaLibrary().retainCurrentOrLoadedMedia();
		if (!sourceMedia) {
			return trackInfos;
		}
		tracklist = libvlc_media_get_tracklist(sourceMedia, type);
		if (!tracklist) {
			libvlc_media_release(sourceMedia);
			return trackInfos;
		}
	}

	const std::set<std::string> selectedTrackIds = collectSelectedTrackIds(player, type);
	const size_t trackCount = libvlc_media_tracklist_count(tracklist);
	trackInfos.reserve(trackCount);
	for (size_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
		libvlc_media_track_t * track = libvlc_media_tracklist_at(tracklist, trackIndex);
		if (!track || track->i_type != type) {
			continue;
		}

		if ((type == libvlc_track_audio || type == libvlc_track_text) &&
			(!track->psz_id || track->psz_id[0] == '\0')) {
			continue;
		}

		MediaTrackInfo info;
		info.id = track->psz_id ? track->psz_id : "";
		info.name = track->psz_name ? trimWhitespace(track->psz_name) : "";
		info.language = track->psz_language ? trimWhitespace(track->psz_language) : "";
		info.description = track->psz_description ? trimWhitespace(track->psz_description) : "";
		info.codecName = codecDescriptionForTrack(track->i_type, track->i_codec);
		info.codecFourcc = fourccToString(track->i_codec);
		info.originalFourcc = fourccToString(track->i_original_fourcc);
		info.bitrate = track->i_bitrate;
		info.profile = track->i_profile;
		info.level = track->i_level;
		info.idStable = track->id_stable;
		info.selected = track->selected || (!info.id.empty() && selectedTrackIds.find(info.id) != selectedTrackIds.end());

		if (track->audio) {
			info.channels = track->audio->i_channels;
			info.sampleRate = track->audio->i_rate;
		}

		if (track->video) {
			info.width = track->video->i_width;
			info.height = track->video->i_height;
			info.sampleAspectNum = track->video->i_sar_num;
			info.sampleAspectDen = track->video->i_sar_den;
			info.frameRateNum = track->video->i_frame_rate_num;
			info.frameRateDen = track->video->i_frame_rate_den;
		}

		if (track->subtitle && track->subtitle->psz_encoding) {
			info.subtitleEncoding = trimWhitespace(track->subtitle->psz_encoding);
		}

		trackInfos.push_back(std::move(info));
	}

	libvlc_media_tracklist_delete(tracklist);
	if (sourceMedia) {
		libvlc_media_release(sourceMedia);
	}
	return trackInfos;
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::MediaComponent::getVideoTracks() const {
	return getTrackInfos(libvlc_track_video);
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::MediaComponent::getAudioTracks() const {
	return getTrackInfos(libvlc_track_audio);
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::MediaComponent::getSubtitleTracks() const {
	return getTrackInfos(libvlc_track_text);
}

bool ofxVlc4::MediaComponent::selectTrackById(libvlc_track_type_t type, const std::string & trackId) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	if (trackId.empty()) {
		libvlc_media_player_unselect_track_type(player, type);
		return true;
	}

	libvlc_media_tracklist_t * tracklist = libvlc_media_player_get_tracklist(player, type, false);
	if (!tracklist) {
		return false;
	}

	const size_t trackCount = libvlc_media_tracklist_count(tracklist);
	for (size_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
		libvlc_media_track_t * track = libvlc_media_tracklist_at(tracklist, trackIndex);
		if (!track || !track->psz_id || track->i_type != type) {
			continue;
		}

		if (trackId == track->psz_id) {
			MediaTrackInfo info;
			info.id = track->psz_id;
			info.name = track->psz_name ? trimWhitespace(track->psz_name) : "";
			info.language = track->psz_language ? trimWhitespace(track->psz_language) : "";
			info.description = track->psz_description ? trimWhitespace(track->psz_description) : "";
			info.codecName = codecDescriptionForTrack(track->i_type, track->i_codec);
			info.codecFourcc = fourccToString(track->i_codec);
			info.selected = track->selected;
			libvlc_media_player_select_track(player, track);
			libvlc_media_tracklist_delete(tracklist);
			owner.setStatus(std::string(type == libvlc_track_audio ? "Audio" : "Subtitle") + " track set.");
			owner.logNotice(std::string(type == libvlc_track_audio ? "Audio" : "Subtitle") + " track: " + describeTrackLabel(info) + ".");
			return true;
		}
	}

	libvlc_media_tracklist_delete(tracklist);
	return false;
}

bool ofxVlc4::MediaComponent::selectAudioTrackById(const std::string & trackId) {
	return selectTrackById(libvlc_track_audio, trackId);
}

bool ofxVlc4::MediaComponent::selectSubtitleTrackById(const std::string & trackId) {
	const bool applied = selectTrackById(libvlc_track_text, trackId);
	if (applied) {
		refreshSubtitleStateInfo();
	}
	if (applied && trackId.empty()) {
		owner.setStatus("Subtitle track disabled.");
		owner.logNotice("Subtitle track disabled.");
	}
	return applied;
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::getTrackInfos(libvlc_track_type_t type) const {
	return mediaComponent->getTrackInfos(type);
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::getVideoTracks() const {
	return mediaComponent->getVideoTracks();
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::getAudioTracks() const {
	return mediaComponent->getAudioTracks();
}

std::vector<ofxVlc4::MediaTrackInfo> ofxVlc4::getSubtitleTracks() const {
	return mediaComponent->getSubtitleTracks();
}

bool ofxVlc4::selectTrackById(libvlc_track_type_t type, const std::string & trackId) {
	return mediaComponent->selectTrackById(type, trackId);
}

bool ofxVlc4::selectAudioTrackById(const std::string & trackId) {
	return mediaComponent->selectAudioTrackById(trackId);
}

bool ofxVlc4::selectSubtitleTrackById(const std::string & trackId) {
	return mediaComponent->selectSubtitleTrackById(trackId);
}

void ofxVlc4::clearCurrentMedia(bool clearVideoResources) {
	mediaComponent->clearCurrentMedia(clearVideoResources);
}
