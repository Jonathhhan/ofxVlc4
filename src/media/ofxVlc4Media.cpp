#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
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

constexpr const char * kMediaLogChannel = "ofxVlc4";


const std::set<std::string> & defaultMediaExtensions() {
	static const std::set<std::string> extensions = {
		// Audio
		".wav", ".mp3", ".flac", ".ogg", ".opus",
		".m4a", ".aac", ".aiff", ".wma", ".mid", ".midi",
		".mka", ".ac3", ".dts", ".ape", ".wv", ".amr",
		".caf", ".spx", ".ra",
		// Video
		".mp4", ".mov", ".mkv", ".avi", ".wmv", ".asf",
		".webm", ".m4v", ".mpg", ".mpeg", ".ts", ".mts",
		".m2ts", ".m2v", ".vob", ".ogv", ".3gp", ".m3u8",
		".flv", ".f4v", ".ogm", ".dv", ".mxf", ".qt",
		".rm", ".rmvb", ".divx",
		// Images
		".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff", ".gif",
		// Playlists
		".m3u", ".pls", ".xspf"
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
	return *owner.m_impl->subsystemRuntime.mediaLibraryController;
}

ofxVlc4::AudioComponent & ofxVlc4::MediaComponent::audio() const {
	return *owner.m_impl->subsystemRuntime.audioComponent;
}

ofxVlc4::VideoComponent & ofxVlc4::MediaComponent::video() const {
	return *owner.m_impl->subsystemRuntime.videoComponent;
}

PlaybackController & ofxVlc4::MediaComponent::playback() const {
	return *owner.m_impl->subsystemRuntime.playbackController;
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
		if (libvlc_media_player_set_time(player, resumeTimeMs, true) != 0) {
			owner.logWarning("libvlc_media_player_set_time failed while reapplying filter chain.");
		}
	}

	if (wasPlaying || wasPaused) {
		audio().applyEqualizerSettings();
		audio().clearPendingEqualizerApplyOnPlay();
		video().clearPendingVideoAdjustApplyOnPlay();
		if (libvlc_media_player_play(player) != 0) {
			owner.logWarning("libvlc_media_player_play failed while reapplying filter chain.");
		}
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

	owner.m_impl->subsystemRuntime.coreSession->setMedia(isLocation
		? libvlc_media_new_location(source.c_str())
		: libvlc_media_new_path(source.c_str()));
	if (!owner.m_impl->subsystemRuntime.coreSession->media()) {
		owner.setError("Failed to create media source.");
		return false;
	}

	for (const auto & option : options) {
		const std::string trimmedOption = trimWhitespace(option);
		if (!trimmedOption.empty()) {
			libvlc_media_add_option(owner.m_impl->subsystemRuntime.coreSession->media(), trimmedOption.c_str());
		}
	}

	if (!owner.m_impl->playerConfigRuntime.audioFilterChain.empty()) {
		libvlc_media_add_option(owner.m_impl->subsystemRuntime.coreSession->media(), (":audio-filter=" + owner.m_impl->playerConfigRuntime.audioFilterChain).c_str());
	}
	if (!owner.m_impl->playerConfigRuntime.videoFilterChain.empty() && owner.canApplyNativeVideoFilters()) {
		libvlc_media_add_option(owner.m_impl->subsystemRuntime.coreSession->media(), (":video-filter=" + owner.m_impl->playerConfigRuntime.videoFilterChain).c_str());
	}

	owner.m_impl->subsystemRuntime.coreSession->setMediaEvents(libvlc_media_event_manager(owner.m_impl->subsystemRuntime.coreSession->media()));
	if (owner.m_impl->subsystemRuntime.coreSession && owner.m_impl->subsystemRuntime.coreSession->mediaEvents() && owner.m_impl->subsystemRuntime.eventRouter) {
		owner.m_impl->subsystemRuntime.coreSession->attachMediaEvents(owner.m_impl->subsystemRuntime.eventRouter.get(), VlcEventRouter::vlcMediaEventStatic);
	}

	if (instance) {
		libvlc_media_parse_flag_t parseFlags = libvlc_media_parse_forced;
		parseFlags = static_cast<libvlc_media_parse_flag_t>(
			parseFlags |
			(parseAsNetwork ? libvlc_media_parse_network : libvlc_media_parse_local) |
			libvlc_media_do_interact);
		if (libvlc_media_parse_request(instance, owner.m_impl->subsystemRuntime.coreSession->media(), parseFlags, 1000) != 0) {
			owner.logNotice("Media parse request failed: " + source);
		}
	}

	libvlc_media_player_set_media(player, owner.m_impl->subsystemRuntime.coreSession->media());
	applySafeLoadedMediaPlayerSettings();
	prepareStartupMediaResources();
	return true;
}

bool ofxVlc4::MediaComponent::loadMediaAtIndex(int index) {
	if (!owner.m_impl->subsystemRuntime.coreSession->player()) {
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

	if (owner.m_impl->subsystemRuntime.coreSession->mediaEvents()) {
		if (owner.m_impl->subsystemRuntime.coreSession && owner.m_impl->subsystemRuntime.eventRouter) {
			owner.m_impl->subsystemRuntime.coreSession->detachMediaEvents(owner.m_impl->subsystemRuntime.eventRouter.get(), VlcEventRouter::vlcMediaEventStatic);
		} else {
			owner.m_impl->subsystemRuntime.coreSession->setMediaEvents(nullptr);
		}
	}

	if (currentMedia) {
		libvlc_media_release(currentMedia);
		owner.m_impl->subsystemRuntime.coreSession->setMedia(nullptr);
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.videoMutex);
		owner.m_impl->videoGeometryRuntime.renderWidth.store(0);
		owner.m_impl->videoGeometryRuntime.renderHeight.store(0);
		owner.m_impl->videoGeometryRuntime.videoWidth.store(0);
		owner.m_impl->videoGeometryRuntime.videoHeight.store(0);
		owner.m_impl->videoGeometryRuntime.pixelAspectNumerator.store(1);
		owner.m_impl->videoGeometryRuntime.pixelAspectDenominator.store(1);
		owner.m_impl->videoGeometryRuntime.pendingRenderWidth.store(0);
		owner.m_impl->videoGeometryRuntime.pendingRenderHeight.store(0);
		owner.m_impl->videoGeometryRuntime.pendingResize.store(false);
		owner.m_impl->videoGeometryRuntime.displayAspectRatio.store(1.0f);
		owner.m_impl->videoFrameRuntime.isVideoLoaded.store(false);
		owner.m_impl->videoFrameRuntime.hasReceivedVideoFrame.store(false);
		owner.m_impl->videoFrameRuntime.exposedTextureDirty.store(true);
		owner.m_impl->videoFrameRuntime.vlcFramebufferAttachmentDirty.store(true);
		if (clearVideoResources) {
			clearAllocatedFbo(owner.m_impl->videoResourceRuntime.exposedTextureFbo);
		}
	}

	playback().setAudioPauseSignaled(false);
	owner.m_impl->audioRuntime.ready.store(false);
	audio().clearAudioPtsState();
	owner.m_impl->videoFrameRuntime.startupPlaybackStatePrepared.store(false);
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
		owner.m_impl->stateCacheRuntime.audio.trackCount = 0;
		owner.m_impl->stateCacheRuntime.audio.tracksAvailable = false;
	}
	owner.m_impl->stateCacheRuntime.cachedVideoTrackCount.store(0);
	owner.m_impl->stateCacheRuntime.cachedVideoTrackFps.store(0.0);
	resetSubtitleStateInfo();
	resetNavigationStateInfo();
}

void ofxVlc4::MediaComponent::applyNativeRecording() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		updateNativeRecordingFailureState("No media player available.");
		return;
	}

	if (!owner.m_impl->nativeRecordingRuntime.enabled) {
		if (owner.m_impl->nativeRecordingRuntime.active.load()) {
			libvlc_media_player_record(player, false, nullptr);
			owner.m_impl->nativeRecordingRuntime.active.store(false);
			std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
			setNativeRecordingStatusLocked("Recording stop requested.", "");
		}
		return;
	}

	if (owner.m_impl->nativeRecordingRuntime.active.load()) {
		return;
	}

	const libvlc_state_t state = libvlc_media_player_get_state(player);
	if (state != libvlc_Playing && state != libvlc_Paused) {
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
		setNativeRecordingStatusLocked("Waiting for playback before starting native recording.", "");
		return;
	}

	const std::string resolvedDirectory = normalizeOptionalPath(owner.m_impl->nativeRecordingRuntime.directory);
	const char * directory = resolvedDirectory.empty() ? nullptr : resolvedDirectory.c_str();
	libvlc_media_player_record(player, true, directory);
	owner.m_impl->nativeRecordingRuntime.active.store(true);
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
		setNativeRecordingStatusLocked(
			resolvedDirectory.empty()
				? "Recording requested."
				: ("Recording requested in " + resolvedDirectory + "."),
			"");
	}
}

bool ofxVlc4::MediaComponent::isNativeRecordingEnabled() const {
	return owner.m_impl->nativeRecordingRuntime.enabled;
}

void ofxVlc4::MediaComponent::setNativeRecordingEnabled(bool enabled) {
	if (owner.m_impl->nativeRecordingRuntime.enabled == enabled) {
		return;
	}

	setNativeRecordingEnabledValue(enabled);
	applyNativeRecording();
	owner.setStatus(std::string("Native VLC recording ") + (owner.m_impl->nativeRecordingRuntime.enabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::MediaComponent::getNativeRecordDirectory() const {
	return owner.m_impl->nativeRecordingRuntime.directory;
}

void ofxVlc4::MediaComponent::setNativeRecordDirectory(const std::string & directory) {
	if (owner.m_impl->nativeRecordingRuntime.directory == directory) {
		return;
	}

	setNativeRecordDirectoryValue(directory);
	if (!owner.m_impl->nativeRecordingRuntime.enabled || !owner.m_impl->nativeRecordingRuntime.active.load()) {
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

const std::string & ofxVlc4::MediaComponent::getLastStatusMessage() const {
	return owner.m_impl->diagnosticsRuntime.lastStatusMessage;
}

const std::string & ofxVlc4::MediaComponent::getLastErrorMessage() const {
	return owner.m_impl->diagnosticsRuntime.lastErrorMessage;
}

void ofxVlc4::MediaComponent::clearLastMessages() {
	owner.m_impl->diagnosticsRuntime.lastStatusMessage.clear();
	owner.m_impl->diagnosticsRuntime.lastErrorMessage.clear();
}


ofxVlc4::MediaPlayerRole ofxVlc4::MediaComponent::getMediaPlayerRole() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return owner.m_impl->playerConfigRuntime.mediaPlayerRole;
	}

	return toMediaPlayerRole(libvlc_media_player_get_role(player));
}

void ofxVlc4::MediaComponent::setMediaPlayerRole(MediaPlayerRole role) {
	if (owner.m_impl->playerConfigRuntime.mediaPlayerRole == role) {
		return;
	}

	owner.m_impl->playerConfigRuntime.mediaPlayerRole = role;
	applyMediaPlayerRole();
}

void ofxVlc4::MediaComponent::applyMediaPlayerRole() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_set_role(player, static_cast<unsigned>(toLibvlcMediaPlayerRole(owner.m_impl->playerConfigRuntime.mediaPlayerRole)));
}

void ofxVlc4::MediaComponent::prepareForMediaDetach() {
	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs = -1;
	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition = -1.0f;
	mediaLibrary().resetCurrentMediaParseState();
	cancelThumbnailRequest();
	clearGeneratedThumbnailInfo();
	clearPendingSnapshotState();

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (player && owner.m_impl->nativeRecordingRuntime.active.load()) {
		libvlc_media_player_record(player, false, nullptr);
		owner.m_impl->nativeRecordingRuntime.active.store(false);
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
	owner.m_impl->nativeRecordingRuntime.active.store(active);

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
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
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
	owner.m_impl->nativeRecordingRuntime.active.store(false);
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
	setNativeRecordingStatusLocked(
		trimmedReason.empty() ? "" : ("Failed: " + trimmedReason),
		trimmedReason);
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


void ofxVlc4::clearMetadataCache() {
	mediaComponent->clearMetadataCache();
}

void ofxVlc4::cacheArtworkPathForMediaPath(const std::string & mediaPath, const std::string & artworkPath) {
	mediaComponent->cacheArtworkPathForMediaPath(mediaPath, artworkPath);
}

void ofxVlc4::clearGeneratedThumbnailInfo() {
	mediaComponent->clearGeneratedThumbnailInfo();
}


ofxVlc4::MediaPlayerRole ofxVlc4::getMediaPlayerRole() const {
	return mediaComponent->getMediaPlayerRole();
}

void ofxVlc4::setMediaPlayerRole(MediaPlayerRole role) {
	mediaComponent->setMediaPlayerRole(role);
}


bool ofxVlc4::isPlaybackLocallyStopped() const {
	return playbackController->isPlaybackLocallyStopped();
}


void ofxVlc4::applyMediaPlayerRole() {
	mediaComponent->applyMediaPlayerRole();
}


void ofxVlc4::detachEvents() {
	if (mediaPlayerEventManager) {
		if (coreSession && eventRouter) {
			coreSession->detachPlayerEvents(eventRouter.get(), VlcEventRouter::vlcMediaPlayerEventStatic);
		} else {
			mediaPlayerEventManager = nullptr;
		}
	}

	if (mediaEventManager) {
		if (coreSession && eventRouter) {
			coreSession->detachMediaEvents(eventRouter.get(), VlcEventRouter::vlcMediaEventStatic);
		} else {
			mediaEventManager = nullptr;
		}
	}
}


void ofxVlc4::MediaComponent::resetSubtitleStateInfo() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.subtitleStateMutex);
	owner.m_impl->stateCacheRuntime.subtitle = buildSubtitleStateInfo(false);
}

void ofxVlc4::MediaComponent::resetNavigationStateInfo() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.navigationStateMutex);
	owner.m_impl->stateCacheRuntime.navigation = buildNavigationStateInfo();
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
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.audioStateMutex);
		owner.m_impl->stateCacheRuntime.audio.trackCount = audioTrackCount;
		owner.m_impl->stateCacheRuntime.audio.tracksAvailable = audioTrackCount > 0;
	}

	owner.m_impl->stateCacheRuntime.cachedVideoTrackCount.store(std::max(0, videoTrackCount));
	owner.m_impl->stateCacheRuntime.cachedVideoTrackFps.store((std::isfinite(resolvedVideoFps) && resolvedVideoFps > 0.0) ? resolvedVideoFps : 0.0);
}


void ofxVlc4::MediaComponent::setNativeRecordingEnabledValue(bool enabled) {
	owner.m_impl->nativeRecordingRuntime.enabled = enabled;
}

void ofxVlc4::MediaComponent::setNativeRecordDirectoryValue(const std::string & directory) {
	owner.m_impl->nativeRecordingRuntime.directory = directory;
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
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.subtitleStateMutex);
	owner.m_impl->stateCacheRuntime.subtitle = buildSubtitleStateInfo(true);
}

ofxVlc4::SubtitleStateInfo ofxVlc4::MediaComponent::getSubtitleStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.subtitleStateMutex);
	return owner.m_impl->stateCacheRuntime.subtitle;
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
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.navigationStateMutex);
	owner.m_impl->stateCacheRuntime.navigation = buildNavigationStateInfo();
}

ofxVlc4::NavigationStateInfo ofxVlc4::MediaComponent::getNavigationStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.navigationStateMutex);
	return owner.m_impl->stateCacheRuntime.navigation;
}

ofxVlc4::SnapshotStateInfo ofxVlc4::MediaComponent::buildSnapshotStateInfoLocked() const {
	SnapshotStateInfo info;
	info.pending = owner.m_impl->mediaRuntime.snapshotPending;
	info.available = owner.m_impl->mediaRuntime.snapshotAvailable;
	info.lastRequestedPath = owner.m_impl->mediaRuntime.pendingSnapshotPath;
	info.lastSavedPath = owner.m_impl->mediaRuntime.lastSnapshotPath;
	info.lastSavedMetadataAvailable = owner.m_impl->mediaRuntime.lastSnapshotBytes > 0 || !owner.m_impl->mediaRuntime.lastSnapshotTimestamp.empty();
	info.lastSavedBytes = owner.m_impl->mediaRuntime.lastSnapshotBytes;
	info.lastSavedTimestamp = owner.m_impl->mediaRuntime.lastSnapshotTimestamp;
	info.lastEventMessage = owner.m_impl->mediaRuntime.lastSnapshotEventMessage;
	info.lastFailureReason = owner.m_impl->mediaRuntime.lastSnapshotFailureReason;
	return info;
}

ofxVlc4::NativeRecordingStateInfo ofxVlc4::MediaComponent::buildNativeRecordingStateInfoLocked() const {
	NativeRecordingStateInfo info;
	info.enabled = owner.m_impl->nativeRecordingRuntime.enabled;
	info.active = owner.m_impl->nativeRecordingRuntime.active.load();
	info.directory = owner.m_impl->nativeRecordingRuntime.directory;
	info.lastOutputPathAvailable = !owner.m_impl->nativeRecordingRuntime.lastOutputPath.empty();
	info.lastOutputPath = owner.m_impl->nativeRecordingRuntime.lastOutputPath;
	info.lastOutputMetadataAvailable =
		owner.m_impl->nativeRecordingRuntime.lastOutputBytes > 0 || !owner.m_impl->nativeRecordingRuntime.lastOutputTimestamp.empty();
	info.lastOutputBytes = owner.m_impl->nativeRecordingRuntime.lastOutputBytes;
	info.lastOutputTimestamp = owner.m_impl->nativeRecordingRuntime.lastOutputTimestamp;
	info.lastEventMessage = owner.m_impl->nativeRecordingRuntime.lastEventMessage;
	info.lastFailureReason = owner.m_impl->nativeRecordingRuntime.lastFailureReason;
	return info;
}

void ofxVlc4::MediaComponent::setNativeRecordingStatusLocked(
	const std::string & eventMessage,
	const std::string & failureReason) {
	owner.m_impl->nativeRecordingRuntime.lastEventMessage = eventMessage;
	owner.m_impl->nativeRecordingRuntime.lastFailureReason = failureReason;
}

void ofxVlc4::MediaComponent::clearNativeRecordingOutputLocked() {
	owner.m_impl->nativeRecordingRuntime.lastOutputPath.clear();
	owner.m_impl->nativeRecordingRuntime.lastOutputBytes = 0;
	owner.m_impl->nativeRecordingRuntime.lastOutputTimestamp.clear();
}

void ofxVlc4::MediaComponent::setNativeRecordingOutputLocked(
	const std::string & path,
	uint64_t bytes,
	const std::string & timestamp) {
	owner.m_impl->nativeRecordingRuntime.lastOutputPath = path;
	owner.m_impl->nativeRecordingRuntime.lastOutputBytes = bytes;
	owner.m_impl->nativeRecordingRuntime.lastOutputTimestamp = timestamp;
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
	state.pausable = playback().isPausableLatched();
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
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
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
	owner.m_impl->videoFrameRuntime.startupPlaybackStatePrepared.store(owner.sessionPlayer() != nullptr && owner.sessionMedia() != nullptr);
	owner.m_impl->videoFrameRuntime.hasReceivedVideoFrame.store(false);
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
	playback().invalidateShuffleQueue();
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

	playback().invalidateShuffleQueue();
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
	playback().invalidateShuffleQueue();
}

void ofxVlc4::MediaComponent::movePlaylistItems(const std::vector<int> & fromIndices, int toIndex) {
	mediaLibrary().movePlaylistItems(fromIndices, toIndex);
	playback().invalidateShuffleQueue();
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


void ofxVlc4::vlcMediaEventStatic(const libvlc_event_t * event, void * data) {
	if (!data) {
		return;
	}
	auto * that = static_cast<ofxVlc4 *>(data);
	if (that->m_impl->lifecycleRuntime.shuttingDown.load(std::memory_order_acquire)) {
		return;
	}
	that->vlcMediaEvent(event);
}


void ofxVlc4::vlcMediaEvent(const libvlc_event_t * event) {
	mediaComponent->handleMediaEvent(event);
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

std::vector<std::string> ofxVlc4::getPlaylist() const {
	return mediaComponent->getPlaylist();
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

std::vector<std::string> ofxVlc4::MediaComponent::getPlaylist() const {
	return mediaLibrary().getPlaylist();
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
		if (owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition >= 0.0f) {
			info.state = AbLoopInfo::State::A;
			info.aTimeMs = owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs;
			info.aPosition = owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition;
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

	if (info.state == AbLoopInfo::State::None && owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition >= 0.0f) {
		info.state = AbLoopInfo::State::A;
		info.aTimeMs = owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs;
		info.aPosition = owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition;
	}

	return info;
}

bool ofxVlc4::MediaComponent::setAbLoopA() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs = static_cast<int64_t>(playback().getTime());
	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition = playback().getPosition();
	libvlc_media_player_reset_abloop(player);
	owner.setStatus("A-B loop A set.");
	owner.logNotice("A-B loop A set.");
	return true;
}

bool ofxVlc4::MediaComponent::setAbLoopB() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition < 0.0f) {
		return false;
	}

	const int64_t endTimeMs = static_cast<int64_t>(playback().getTime());
	const float endPosition = playback().getPosition();
	if (endPosition <= owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition) {
		owner.setError("A-B loop end must be after the start.");
		return false;
	}

	const int result = (owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs >= 0 && endTimeMs >= 0)
		? libvlc_media_player_set_abloop_time(player, owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs, endTimeMs)
		: libvlc_media_player_set_abloop_position(player, owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition, endPosition);
	if (result != 0) {
		owner.setError("A-B loop could not be applied.");
		return false;
	}

	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs = -1;
	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition = -1.0f;
	owner.setStatus("A-B loop enabled.");
	owner.logNotice("A-B loop enabled.");
	return true;
}

void ofxVlc4::MediaComponent::clearAbLoop() {
	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartTimeMs = -1;
	owner.m_impl->playbackPolicyRuntime.pendingAbLoopStartPosition = -1.0f;
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
