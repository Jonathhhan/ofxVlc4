#pragma once

#include "vlc/vlc.h"
#include "VlcHandleWrappers.h"

#include <cstdio>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

typedef struct libvlc_renderer_discoverer_t libvlc_renderer_discoverer_t;
typedef struct libvlc_media_discoverer_t libvlc_media_discoverer_t;
typedef struct libvlc_media_list_t libvlc_media_list_t;

class ofxVlc4;

struct VlcCoreLogEntry {
	int level = LIBVLC_NOTICE;
	std::string module;
	std::string file;
	unsigned line = 0;
	std::string objectName;
	std::string objectHeader;
	std::uintptr_t objectId = 0;
	std::string message;
};

class VlcCoreSession {
public:
	static constexpr size_t kLogCapacity = 200;
	using EventCallback = void (*)(const libvlc_event_t *, void *);

	VlcCoreSession() = default;
	~VlcCoreSession();

	VlcCoreSession(const VlcCoreSession &) = delete;
	VlcCoreSession & operator=(const VlcCoreSession &) = delete;

	void reset();

	libvlc_instance_t * instance() const;
	void setInstance(libvlc_instance_t * value);

	libvlc_media_t * media() const;
	void setMedia(libvlc_media_t * value);

	libvlc_media_player_t * player() const;
	void setPlayer(libvlc_media_player_t * value);

	libvlc_event_manager_t * playerEvents() const;
	void setPlayerEvents(libvlc_event_manager_t * value);
	void attachPlayerEvents(void * data, EventCallback callback);
	void detachPlayerEvents(void * data, EventCallback callback);

	libvlc_event_manager_t * mediaEvents() const;
	void setMediaEvents(libvlc_event_manager_t * value);
	void attachMediaEvents(void * data, EventCallback callback);
	void detachMediaEvents(void * data, EventCallback callback);

	libvlc_media_discoverer_t * mediaDiscoverer() const;
	void setMediaDiscoverer(libvlc_media_discoverer_t * value);

	libvlc_media_list_t * mediaDiscovererList() const;
	void setMediaDiscovererList(libvlc_media_list_t * value);

	libvlc_event_manager_t * mediaDiscovererListEvents() const;
	void setMediaDiscovererListEvents(libvlc_event_manager_t * value);
	void attachMediaDiscovererListEvents(void * data, EventCallback callback);
	void detachMediaDiscovererListEvents(void * data, EventCallback callback);

	libvlc_renderer_discoverer_t * rendererDiscoverer() const;
	void setRendererDiscoverer(libvlc_renderer_discoverer_t * value);

	libvlc_event_manager_t * rendererDiscovererEvents() const;
	void setRendererDiscovererEvents(libvlc_event_manager_t * value);
	void attachRendererEvents(void * data, EventCallback callback);
	void detachRendererEvents(void * data, EventCallback callback);

	bool loggingEnabled() const;
	void setLoggingEnabled(bool enabled);

	bool logFileEnabled() const;
	void setLogFileEnabled(bool enabled);

	const std::string & logFilePath() const;
	void setLogFilePath(const std::string & path);

	FILE * logFileHandle() const;
	void setLogFileHandle(FILE * handle);
	void closeLogFile();

	const std::deque<VlcCoreLogEntry> & logEntries() const;
	void clearLogEntries();
	void appendLog(const VlcCoreLogEntry & entry);

private:
	vlc::InstanceHandle libvlcHandle;
	vlc::MediaHandle mediaHandleOwner;
	vlc::PlayerHandle mediaPlayerHandle;
	libvlc_event_manager_t * mediaPlayerEventManager = nullptr;
	libvlc_event_manager_t * mediaEventManager = nullptr;
	libvlc_media_discoverer_t * mediaDiscovererHandle = nullptr;
	libvlc_media_list_t * mediaDiscovererMediaList = nullptr;
	libvlc_event_manager_t * mediaDiscovererMediaListEventManager = nullptr;
	libvlc_renderer_discoverer_t * rendererDiscovererHandle = nullptr;
	libvlc_event_manager_t * rendererDiscovererEventManager = nullptr;

	bool libVlcLoggingEnabled = false;
	bool libVlcLogFileEnabled = false;
	std::string libVlcLogFilePath;
	FILE * libVlcLogFileHandle = nullptr;
	std::deque<VlcCoreLogEntry> libVlcLogEntries;
};
