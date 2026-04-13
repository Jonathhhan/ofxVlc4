#pragma once

#include "ofxVlc4Impl.h"
#include "vlc/vlc.h"

#include <cstdint>
#include <memory>

class VlcEventRouter;

class ofxVlc4 {
public:
	class CallbackScope {
	public:
		CallbackScope() = default;
		explicit CallbackScope(ofxVlc4 * ownerRef)
			: owner(ownerRef) {
		}
		explicit operator bool() const { return owner != nullptr; }
		ofxVlc4 * get() const { return owner; }

	private:
		ofxVlc4 * owner = nullptr;
	};

	ofxVlc4()
		: m_impl(std::make_unique<ofxVlc4Impl>()) {
	}

	CallbackScope enterCallbackScope() const {
		return CallbackScope(const_cast<ofxVlc4 *>(this));
	}

	void vlcMediaPlayerEvent(const libvlc_event_t *) { ++playerEventCount; }
	void vlcMediaEvent(const libvlc_event_t *) { ++mediaEventCount; }
	void mediaDiscovererMediaListEvent(const libvlc_event_t *) { ++discovererEventCount; }
	void rendererDiscovererEvent(const libvlc_event_t *) { ++rendererEventCount; }
	void handleDialogDisplayLogin(libvlc_dialog_id *, const char *, const char *, const char *, bool) { ++dialogLoginCount; }
	void handleDialogDisplayQuestion(
		libvlc_dialog_id *,
		const char *,
		const char *,
		libvlc_dialog_question_type,
		const char *,
		const char *,
		const char *) {
		++dialogQuestionCount;
	}
	void handleDialogDisplayProgress(libvlc_dialog_id *, const char *, const char *, bool, float, const char *) {
		++dialogProgressCount;
	}
	void handleDialogCancel(libvlc_dialog_id *) { ++dialogCancelCount; }
	void handleDialogUpdateProgress(libvlc_dialog_id *, float, const char *) { ++dialogUpdateCount; }
	void handleDialogError(const char *, const char *) { ++dialogErrorCount; }

	std::unique_ptr<ofxVlc4Impl> m_impl;

	int playerEventCount = 0;
	int mediaEventCount = 0;
	int discovererEventCount = 0;
	int rendererEventCount = 0;
	int dialogLoginCount = 0;
	int dialogQuestionCount = 0;
	int dialogProgressCount = 0;
	int dialogCancelCount = 0;
	int dialogUpdateCount = 0;
	int dialogErrorCount = 0;

private:
	friend class VlcEventRouter;
};
