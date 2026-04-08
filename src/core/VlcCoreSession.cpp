#include "VlcCoreSession.h"

#include <algorithm>

namespace {

static const libvlc_event_type_t kPlayerEventTypes[] = {
	libvlc_MediaPlayerLengthChanged,
	libvlc_MediaPlayerStopped,
	libvlc_MediaPlayerStopping,
	libvlc_MediaPlayerPlaying,
	libvlc_MediaPlayerMuted,
	libvlc_MediaPlayerUnmuted,
	libvlc_MediaPlayerAudioVolume,
	libvlc_MediaPlayerAudioDevice,
	libvlc_MediaPlayerSnapshotTaken,
	libvlc_MediaPlayerRecordChanged,
	libvlc_MediaPlayerESAdded,
	libvlc_MediaPlayerESDeleted,
	libvlc_MediaPlayerESSelected,
	libvlc_MediaPlayerESUpdated,
	libvlc_MediaPlayerProgramAdded,
	libvlc_MediaPlayerProgramDeleted,
	libvlc_MediaPlayerProgramSelected,
	libvlc_MediaPlayerProgramUpdated,
	libvlc_MediaPlayerTitleListChanged,
	libvlc_MediaPlayerTitleSelectionChanged,
	libvlc_MediaPlayerChapterChanged,
	libvlc_MediaPlayerPaused,
	libvlc_MediaPlayerBuffering,
	libvlc_MediaPlayerVout,
	libvlc_MediaPlayerSeekableChanged,
	libvlc_MediaPlayerPausableChanged,
	libvlc_MediaPlayerEncounteredError,
	libvlc_MediaPlayerCorked,
	libvlc_MediaPlayerUncorked,
};

} // namespace

VlcCoreSession::~VlcCoreSession() {
	closeLogFile();
}

void VlcCoreSession::reset() {
	closeLogFile();
	libvlc = nullptr;
	mediaHandle = nullptr;
	mediaPlayer = nullptr;
	mediaPlayerEventManager = nullptr;
	mediaEventManager = nullptr;
	mediaDiscovererHandle = nullptr;
	mediaDiscovererMediaList = nullptr;
	mediaDiscovererMediaListEventManager = nullptr;
	rendererDiscovererHandle = nullptr;
	rendererDiscovererEventManager = nullptr;
	libVlcLoggingEnabled = false;
	libVlcLogFileEnabled = false;
	libVlcLogFilePath.clear();
	libVlcLogEntries.clear();
}

libvlc_instance_t * VlcCoreSession::instance() const {
	return libvlc;
}

void VlcCoreSession::setInstance(libvlc_instance_t * value) {
	libvlc = value;
}

libvlc_media_t * VlcCoreSession::media() const {
	return mediaHandle;
}

void VlcCoreSession::setMedia(libvlc_media_t * value) {
	mediaHandle = value;
}

libvlc_media_player_t * VlcCoreSession::player() const {
	return mediaPlayer;
}

void VlcCoreSession::setPlayer(libvlc_media_player_t * value) {
	mediaPlayer = value;
}

libvlc_event_manager_t * VlcCoreSession::playerEvents() const {
	return mediaPlayerEventManager;
}

void VlcCoreSession::setPlayerEvents(libvlc_event_manager_t * value) {
	mediaPlayerEventManager = value;
}

void VlcCoreSession::attachPlayerEvents(void * data, EventCallback callback) {
	if (!mediaPlayerEventManager || !callback) {
		return;
	}

	for (libvlc_event_type_t type : kPlayerEventTypes) {
		libvlc_event_attach(mediaPlayerEventManager, type, callback, data);
	}
}

void VlcCoreSession::detachPlayerEvents(void * data, EventCallback callback) {
	if (!mediaPlayerEventManager || !callback) {
		return;
	}

	for (libvlc_event_type_t type : kPlayerEventTypes) {
		libvlc_event_detach(mediaPlayerEventManager, type, callback, data);
	}
	// Do not set mediaPlayerEventManager to nullptr here.
}

libvlc_event_manager_t * VlcCoreSession::mediaEvents() const {
	return mediaEventManager;
}

void VlcCoreSession::setMediaEvents(libvlc_event_manager_t * value) {
	mediaEventManager = value;
}

void VlcCoreSession::attachMediaEvents(void * data, EventCallback callback) {
	if (!mediaEventManager || !callback) {
		return;
	}

	libvlc_event_attach(mediaEventManager, libvlc_MediaParsedChanged, callback, data);
	libvlc_event_attach(mediaEventManager, libvlc_MediaThumbnailGenerated, callback, data);
	libvlc_event_attach(mediaEventManager, libvlc_MediaAttachedThumbnailsFound, callback, data);
}

void VlcCoreSession::detachMediaEvents(void * data, EventCallback callback) {
	if (!mediaEventManager || !callback) {
		return;
	}

	libvlc_event_detach(mediaEventManager, libvlc_MediaParsedChanged, callback, data);
	libvlc_event_detach(mediaEventManager, libvlc_MediaThumbnailGenerated, callback, data);
	libvlc_event_detach(mediaEventManager, libvlc_MediaAttachedThumbnailsFound, callback, data);
	// Do not set mediaEventManager to nullptr here.
}

libvlc_media_discoverer_t * VlcCoreSession::mediaDiscoverer() const {
	return mediaDiscovererHandle;
}

void VlcCoreSession::setMediaDiscoverer(libvlc_media_discoverer_t * value) {
	mediaDiscovererHandle = value;
}

libvlc_media_list_t * VlcCoreSession::mediaDiscovererList() const {
	return mediaDiscovererMediaList;
}

void VlcCoreSession::setMediaDiscovererList(libvlc_media_list_t * value) {
	mediaDiscovererMediaList = value;
}

libvlc_event_manager_t * VlcCoreSession::mediaDiscovererListEvents() const {
	return mediaDiscovererMediaListEventManager;
}

void VlcCoreSession::setMediaDiscovererListEvents(libvlc_event_manager_t * value) {
	mediaDiscovererMediaListEventManager = value;
}

void VlcCoreSession::attachMediaDiscovererListEvents(void * data, EventCallback callback) {
	if (!mediaDiscovererMediaListEventManager || !callback) {
		return;
	}

	libvlc_event_attach(mediaDiscovererMediaListEventManager, libvlc_MediaListItemAdded, callback, data);
	libvlc_event_attach(mediaDiscovererMediaListEventManager, libvlc_MediaListItemDeleted, callback, data);
	libvlc_event_attach(mediaDiscovererMediaListEventManager, libvlc_MediaListEndReached, callback, data);
}

void VlcCoreSession::detachMediaDiscovererListEvents(void * data, EventCallback callback) {
	if (!mediaDiscovererMediaListEventManager || !callback) {
		return;
	}

	libvlc_event_detach(mediaDiscovererMediaListEventManager, libvlc_MediaListItemAdded, callback, data);
	libvlc_event_detach(mediaDiscovererMediaListEventManager, libvlc_MediaListItemDeleted, callback, data);
	libvlc_event_detach(mediaDiscovererMediaListEventManager, libvlc_MediaListEndReached, callback, data);
	// Do not set mediaDiscovererMediaListEventManager to nullptr here.
}

libvlc_renderer_discoverer_t * VlcCoreSession::rendererDiscoverer() const {
	return rendererDiscovererHandle;
}

void VlcCoreSession::setRendererDiscoverer(libvlc_renderer_discoverer_t * value) {
	rendererDiscovererHandle = value;
}

libvlc_event_manager_t * VlcCoreSession::rendererDiscovererEvents() const {
	return rendererDiscovererEventManager;
}

void VlcCoreSession::setRendererDiscovererEvents(libvlc_event_manager_t * value) {
	rendererDiscovererEventManager = value;
}

void VlcCoreSession::attachRendererEvents(void * data, EventCallback callback) {
	if (!rendererDiscovererEventManager || !callback) {
		return;
	}

	libvlc_event_attach(rendererDiscovererEventManager, libvlc_RendererDiscovererItemAdded, callback, data);
	libvlc_event_attach(rendererDiscovererEventManager, libvlc_RendererDiscovererItemDeleted, callback, data);
}

void VlcCoreSession::detachRendererEvents(void * data, EventCallback callback) {
	if (!rendererDiscovererEventManager || !callback) {
		return;
	}

	libvlc_event_detach(rendererDiscovererEventManager, libvlc_RendererDiscovererItemAdded, callback, data);
	libvlc_event_detach(rendererDiscovererEventManager, libvlc_RendererDiscovererItemDeleted, callback, data);
	// Do not set rendererDiscovererEventManager to nullptr here.
}

bool VlcCoreSession::loggingEnabled() const {
	return libVlcLoggingEnabled;
}

void VlcCoreSession::setLoggingEnabled(bool enabled) {
	libVlcLoggingEnabled = enabled;
}

bool VlcCoreSession::logFileEnabled() const {
	return libVlcLogFileEnabled;
}

void VlcCoreSession::setLogFileEnabled(bool enabled) {
	libVlcLogFileEnabled = enabled;
}

const std::string & VlcCoreSession::logFilePath() const {
	return libVlcLogFilePath;
}

void VlcCoreSession::setLogFilePath(const std::string & path) {
	libVlcLogFilePath = path;
}

FILE * VlcCoreSession::logFileHandle() const {
	return libVlcLogFileHandle;
}

void VlcCoreSession::setLogFileHandle(FILE * handle) {
	if (libVlcLogFileHandle == handle) {
		return;
	}

	closeLogFile();
	libVlcLogFileHandle = handle;
}

void VlcCoreSession::closeLogFile() {
	if (!libVlcLogFileHandle) {
		return;
	}

	if (std::fflush(libVlcLogFileHandle) != 0) {
		// Optionally log error
	}
	if (std::fclose(libVlcLogFileHandle) != 0) {
		// Optionally log error
	}
	libVlcLogFileHandle = nullptr;
}

const std::deque<VlcCoreLogEntry> & VlcCoreSession::logEntries() const {
	return libVlcLogEntries;
}

void VlcCoreSession::clearLogEntries() {
	libVlcLogEntries.clear();
}

void VlcCoreSession::appendLog(const VlcCoreLogEntry & entry) {
	if (entry.message.empty()) {
		return;
	}

	while (libVlcLogEntries.size() >= kLogCapacity) {
		libVlcLogEntries.pop_front();
	}
	libVlcLogEntries.push_back(entry);
}
