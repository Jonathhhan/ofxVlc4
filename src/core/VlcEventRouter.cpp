#include "VlcEventRouter.h"
#include "VlcCoreSession.h"
#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"

namespace {

ofxVlc4 * resolveOwnerFromRouterData(void * data) {
	auto * router = static_cast<VlcEventRouter *>(data);
	return router ? &router->getOwner() : nullptr;
}

bool isShuttingDown(const ofxVlc4 & owner) {
	return owner.m_impl->lifecycleRuntime.shuttingDown.load(std::memory_order_acquire);
}

template <typename HandlerFn>
void dispatchToOwner(void * data, bool dropDuringShutdown, HandlerFn && handler) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr) {
		return;
	}
	if (dropDuringShutdown && isShuttingDown(*ownerPtr)) {
		return;
	}

	ofxVlc4::CallbackScope scope = ownerPtr->enterCallbackScope();
	if (!scope) {
		return;
	}

	handler(*scope.get());
}

} // namespace

VlcEventRouter::VlcEventRouter(ofxVlc4 & ownerRef)
	: owner(ownerRef) {
}

ofxVlc4 & VlcEventRouter::getOwner() const {
	return owner;
}

void * VlcEventRouter::callbackData() const {
	return const_cast<VlcEventRouter *>(this);
}

VlcEventRouter::EventCallback VlcEventRouter::playerEventCallback() const {
	return VlcEventRouter::vlcMediaPlayerEventStatic;
}

VlcEventRouter::EventCallback VlcEventRouter::mediaEventCallback() const {
	return VlcEventRouter::vlcMediaEventStatic;
}

VlcEventRouter::EventCallback VlcEventRouter::mediaDiscovererListEventCallback() const {
	return VlcEventRouter::mediaDiscovererMediaListEventStatic;
}

VlcEventRouter::EventCallback VlcEventRouter::rendererDiscovererEventCallback() const {
	return VlcEventRouter::rendererDiscovererEventStatic;
}

libvlc_dialog_cbs VlcEventRouter::dialogCallbacks() const {
	return {
		VlcEventRouter::dialogDisplayLoginStatic,
		VlcEventRouter::dialogDisplayQuestionStatic,
		VlcEventRouter::dialogDisplayProgressStatic,
		VlcEventRouter::dialogCancelStatic,
		VlcEventRouter::dialogUpdateProgressStatic
	};
}

VlcEventRouter::DialogErrorCallback VlcEventRouter::dialogErrorCallback() const {
	return VlcEventRouter::dialogErrorStatic;
}

void VlcEventRouter::attachPlayerEvents(VlcCoreSession & coreSession) const {
	coreSession.attachPlayerEvents(callbackData(), playerEventCallback());
}

void VlcEventRouter::detachPlayerEvents(VlcCoreSession & coreSession) const {
	coreSession.detachPlayerEvents(callbackData(), playerEventCallback());
}

void VlcEventRouter::attachMediaEvents(VlcCoreSession & coreSession) const {
	coreSession.attachMediaEvents(callbackData(), mediaEventCallback());
}

void VlcEventRouter::detachMediaEvents(VlcCoreSession & coreSession) const {
	coreSession.detachMediaEvents(callbackData(), mediaEventCallback());
}

void VlcEventRouter::attachMediaDiscovererListEvents(VlcCoreSession & coreSession) const {
	coreSession.attachMediaDiscovererListEvents(
		callbackData(),
		mediaDiscovererListEventCallback());
}

void VlcEventRouter::detachMediaDiscovererListEvents(VlcCoreSession & coreSession) const {
	coreSession.detachMediaDiscovererListEvents(
		callbackData(),
		mediaDiscovererListEventCallback());
}

void VlcEventRouter::attachRendererEvents(VlcCoreSession & coreSession) const {
	coreSession.attachRendererEvents(callbackData(), rendererDiscovererEventCallback());
}

void VlcEventRouter::detachRendererEvents(VlcCoreSession & coreSession) const {
	coreSession.detachRendererEvents(callbackData(), rendererDiscovererEventCallback());
}

void VlcEventRouter::applyDialogCallbacks(libvlc_instance_t * instance) const {
	if (!instance) {
		return;
	}
	libvlc_dialog_cbs callbacks = dialogCallbacks();
	libvlc_dialog_set_callbacks(instance, &callbacks, callbackData());
	libvlc_dialog_set_error_callback(instance, dialogErrorCallback(), callbackData());
}

void VlcEventRouter::vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data) {
	if (!event) {
		return;
	}
	dispatchToOwner(data, true, [event](ofxVlc4 & owner) { owner.vlcMediaPlayerEvent(event); });
}

void VlcEventRouter::vlcMediaEventStatic(const libvlc_event_t * event, void * data) {
	if (!event) {
		return;
	}
	dispatchToOwner(data, true, [event](ofxVlc4 & owner) { owner.vlcMediaEvent(event); });
}

void VlcEventRouter::mediaDiscovererMediaListEventStatic(const libvlc_event_t * event, void * data) {
	if (!event) {
		return;
	}
	dispatchToOwner(data, true, [event](ofxVlc4 & owner) { owner.mediaDiscovererMediaListEvent(event); });
}

void VlcEventRouter::rendererDiscovererEventStatic(const libvlc_event_t * event, void * data) {
	if (!event) {
		return;
	}
	dispatchToOwner(data, true, [event](ofxVlc4 & owner) { owner.rendererDiscovererEvent(event); });
}

void VlcEventRouter::dialogDisplayLoginStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	const char * defaultUsername,
	bool askStore) {
	if (!id) {
		return;
	}
	dispatchToOwner(
		data,
		true,
		[id, title, text, defaultUsername, askStore](ofxVlc4 & owner) {
			owner.handleDialogDisplayLogin(id, title, text, defaultUsername, askStore);
		});
}

void VlcEventRouter::dialogDisplayQuestionStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	libvlc_dialog_question_type type,
	const char * cancel,
	const char * action1,
	const char * action2) {
	if (!id) {
		return;
	}
	dispatchToOwner(
		data,
		true,
		[id, title, text, type, cancel, action1, action2](ofxVlc4 & owner) {
			owner.handleDialogDisplayQuestion(id, title, text, type, cancel, action1, action2);
		});
}

void VlcEventRouter::dialogDisplayProgressStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	bool indeterminate,
	float position,
	const char * cancel) {
	if (!id) {
		return;
	}
	dispatchToOwner(
		data,
		true,
		[id, title, text, indeterminate, position, cancel](ofxVlc4 & owner) {
			owner.handleDialogDisplayProgress(id, title, text, indeterminate, position, cancel);
		});
}

void VlcEventRouter::dialogCancelStatic(void * data, libvlc_dialog_id * id) {
	if (!id) {
		return;
	}
	dispatchToOwner(data, true, [id](ofxVlc4 & owner) { owner.handleDialogCancel(id); });
}

void VlcEventRouter::dialogUpdateProgressStatic(void * data, libvlc_dialog_id * id, float position, const char * text) {
	if (!id) {
		return;
	}
	dispatchToOwner(
		data,
		true,
		[id, position, text](ofxVlc4 & owner) { owner.handleDialogUpdateProgress(id, position, text); });
}

void VlcEventRouter::dialogErrorStatic(void * data, const char * title, const char * text) {
	dispatchToOwner(data, true, [title, text](ofxVlc4 & owner) { owner.handleDialogError(title, text); });
}
