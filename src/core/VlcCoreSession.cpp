#include "VlcCoreSession.h"

#include <algorithm>
#include <cerrno>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>

namespace {

using EventTypeName = std::pair<libvlc_event_type_t, const char *>;

void logEventAttachFailure(VlcCoreSession & session, const std::string & eventName) {
	VlcCoreLogEntry entry;
	entry.level = LIBVLC_WARNING;
	entry.module = "VlcCoreSession";
	entry.message = "libvlc_event_attach failed for " + eventName + ".";
	session.appendLog(entry);
}

void attachNamedEvents(
	VlcCoreSession & session,
	libvlc_event_manager_t * manager,
	void * data,
	VlcCoreSession::EventCallback callback,
	const std::initializer_list<EventTypeName> & events) {
	if (!manager || !callback) {
		return;
	}

	for (const auto & event : events) {
		if (libvlc_event_attach(manager, event.first, callback, data) != 0) {
			logEventAttachFailure(session, event.second ? event.second : "unknown");
		}
	}
}

void detachNamedEvents(
	libvlc_event_manager_t * manager,
	void * data,
	VlcCoreSession::EventCallback callback,
	const std::initializer_list<EventTypeName> & events) {
	if (!manager || !callback) {
		return;
	}

	for (const auto & event : events) {
		libvlc_event_detach(manager, event.first, callback, data);
	}
}

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
	// RAII handles release their owned resources automatically.
	mediaPlayerHandle.reset();
	mediaHandleOwner.reset();
	libvlcHandle.reset();
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
	return libvlcHandle.get();
}

void VlcCoreSession::setInstance(libvlc_instance_t * value) {
	libvlcHandle.reset(value);
}

libvlc_media_t * VlcCoreSession::media() const {
	return mediaHandleOwner.get();
}

void VlcCoreSession::setMedia(libvlc_media_t * value) {
	mediaHandleOwner.reset(value);
}

libvlc_media_player_t * VlcCoreSession::player() const {
	return mediaPlayerHandle.get();
}

void VlcCoreSession::setPlayer(libvlc_media_player_t * value) {
	mediaPlayerHandle.reset(value);
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
		if (libvlc_event_attach(mediaPlayerEventManager, type, callback, data) != 0) {
			logEventAttachFailure(*this, "player event type " + std::to_string(static_cast<int>(type)));
		}
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
	attachNamedEvents(*this, mediaEventManager, data, callback, {
		{ libvlc_MediaParsedChanged, "MediaParsedChanged" },
		{ libvlc_MediaThumbnailGenerated, "MediaThumbnailGenerated" },
		{ libvlc_MediaAttachedThumbnailsFound, "MediaAttachedThumbnailsFound" },
	});
}

void VlcCoreSession::detachMediaEvents(void * data, EventCallback callback) {
	detachNamedEvents(mediaEventManager, data, callback, {
		{ libvlc_MediaParsedChanged, "MediaParsedChanged" },
		{ libvlc_MediaThumbnailGenerated, "MediaThumbnailGenerated" },
		{ libvlc_MediaAttachedThumbnailsFound, "MediaAttachedThumbnailsFound" },
	});
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
	attachNamedEvents(*this, mediaDiscovererMediaListEventManager, data, callback, {
		{ libvlc_MediaListItemAdded, "MediaListItemAdded" },
		{ libvlc_MediaListItemDeleted, "MediaListItemDeleted" },
		{ libvlc_MediaListEndReached, "MediaListEndReached" },
	});
}

void VlcCoreSession::detachMediaDiscovererListEvents(void * data, EventCallback callback) {
	detachNamedEvents(mediaDiscovererMediaListEventManager, data, callback, {
		{ libvlc_MediaListItemAdded, "MediaListItemAdded" },
		{ libvlc_MediaListItemDeleted, "MediaListItemDeleted" },
		{ libvlc_MediaListEndReached, "MediaListEndReached" },
	});
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
	attachNamedEvents(*this, rendererDiscovererEventManager, data, callback, {
		{ libvlc_RendererDiscovererItemAdded, "RendererDiscovererItemAdded" },
		{ libvlc_RendererDiscovererItemDeleted, "RendererDiscovererItemDeleted" },
	});
}

void VlcCoreSession::detachRendererEvents(void * data, EventCallback callback) {
	detachNamedEvents(rendererDiscovererEventManager, data, callback, {
		{ libvlc_RendererDiscovererItemAdded, "RendererDiscovererItemAdded" },
		{ libvlc_RendererDiscovererItemDeleted, "RendererDiscovererItemDeleted" },
	});
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
		std::cerr << "[ofxVlc4] Failed to flush libVLC log file (errno " << errno << ")." << std::endl;
	}
	if (std::fclose(libVlcLogFileHandle) != 0) {
		std::cerr << "[ofxVlc4] Failed to close libVLC log file (errno " << errno << ")." << std::endl;
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
