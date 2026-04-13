#pragma once

#include "vlc/vlc.h"

class VlcCoreSession {
public:
	using EventCallback = void (*)(const libvlc_event_t *, void *);

	void attachPlayerEvents(void * data, EventCallback callback) {
		playerAttachCount++;
		lastPlayerData = data;
		lastPlayerCallback = callback;
	}

	void detachPlayerEvents(void * data, EventCallback callback) {
		playerDetachCount++;
		lastPlayerDetachData = data;
		lastPlayerDetachCallback = callback;
	}

	void attachMediaEvents(void * data, EventCallback callback) {
		mediaAttachCount++;
		lastMediaData = data;
		lastMediaCallback = callback;
	}

	void detachMediaEvents(void * data, EventCallback callback) {
		mediaDetachCount++;
		lastMediaDetachData = data;
		lastMediaDetachCallback = callback;
	}

	void attachMediaDiscovererListEvents(void * data, EventCallback callback) {
		discovererAttachCount++;
		lastDiscovererData = data;
		lastDiscovererCallback = callback;
	}

	void detachMediaDiscovererListEvents(void * data, EventCallback callback) {
		discovererDetachCount++;
		lastDiscovererDetachData = data;
		lastDiscovererDetachCallback = callback;
	}

	void attachRendererEvents(void * data, EventCallback callback) {
		rendererAttachCount++;
		lastRendererData = data;
		lastRendererCallback = callback;
	}

	void detachRendererEvents(void * data, EventCallback callback) {
		rendererDetachCount++;
		lastRendererDetachData = data;
		lastRendererDetachCallback = callback;
	}

	int playerAttachCount = 0;
	int playerDetachCount = 0;
	int mediaAttachCount = 0;
	int mediaDetachCount = 0;
	int discovererAttachCount = 0;
	int discovererDetachCount = 0;
	int rendererAttachCount = 0;
	int rendererDetachCount = 0;

	void * lastPlayerData = nullptr;
	void * lastPlayerDetachData = nullptr;
	void * lastMediaData = nullptr;
	void * lastMediaDetachData = nullptr;
	void * lastDiscovererData = nullptr;
	void * lastDiscovererDetachData = nullptr;
	void * lastRendererData = nullptr;
	void * lastRendererDetachData = nullptr;

	EventCallback lastPlayerCallback = nullptr;
	EventCallback lastPlayerDetachCallback = nullptr;
	EventCallback lastMediaCallback = nullptr;
	EventCallback lastMediaDetachCallback = nullptr;
	EventCallback lastDiscovererCallback = nullptr;
	EventCallback lastDiscovererDetachCallback = nullptr;
	EventCallback lastRendererCallback = nullptr;
	EventCallback lastRendererDetachCallback = nullptr;
};
