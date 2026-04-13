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
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr || !event) {
		return;
	}

	// Drop player events while the session is shutting down.  The
	// shuttingDown flag is set before player release to prevent callbacks
	// from accessing the player after it has been freed.
	if (isShuttingDown(*ownerPtr)) {
		return;
	}

	ofxVlc4::vlcMediaPlayerEventStatic(event, ownerPtr->m_controlBlock.get());
}

void VlcEventRouter::vlcMediaEventStatic(const libvlc_event_t * event, void * data) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr || !event) {
		return;
	}

	// Drop media events while the session is shutting down.  A late
	// MediaParsedChanged event from a recently-cancelled parse could
	// otherwise call into teardown-unsafe code paths (e.g.
	// queryVideoTrackGeometry on an already-released player).
	if (isShuttingDown(*ownerPtr)) {
		return;
	}

	ofxVlc4::vlcMediaEventStatic(event, ownerPtr->m_controlBlock.get());
}

void VlcEventRouter::mediaDiscovererMediaListEventStatic(const libvlc_event_t * event, void * data) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr || !event) {
		return;
	}

	if (isShuttingDown(*ownerPtr)) {
		return;
	}

	ofxVlc4::mediaDiscovererMediaListEventStatic(event, ownerPtr->m_controlBlock.get());
}

void VlcEventRouter::rendererDiscovererEventStatic(const libvlc_event_t * event, void * data) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr || !event) {
		return;
	}

	if (isShuttingDown(*ownerPtr)) {
		return;
	}

	ofxVlc4::rendererDiscovererEventStatic(event, ownerPtr->m_controlBlock.get());
}

void VlcEventRouter::dialogDisplayLoginStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	const char * defaultUsername,
	bool askStore) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr) {
		return;
	}

	ofxVlc4::dialogDisplayLoginStatic(
		ownerPtr->m_controlBlock.get(),
		id,
		title,
		text,
		defaultUsername,
		askStore);
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
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr) {
		return;
	}

	ofxVlc4::dialogDisplayQuestionStatic(
		ownerPtr->m_controlBlock.get(),
		id,
		title,
		text,
		type,
		cancel,
		action1,
		action2);
}

void VlcEventRouter::dialogDisplayProgressStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	bool indeterminate,
	float position,
	const char * cancel) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr) {
		return;
	}

	ofxVlc4::dialogDisplayProgressStatic(
		ownerPtr->m_controlBlock.get(),
		id,
		title,
		text,
		indeterminate,
		position,
		cancel);
}

void VlcEventRouter::dialogCancelStatic(void * data, libvlc_dialog_id * id) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr) {
		return;
	}

	ofxVlc4::dialogCancelStatic(ownerPtr->m_controlBlock.get(), id);
}

void VlcEventRouter::dialogUpdateProgressStatic(void * data, libvlc_dialog_id * id, float position, const char * text) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr) {
		return;
	}

	ofxVlc4::dialogUpdateProgressStatic(ownerPtr->m_controlBlock.get(), id, position, text);
}

void VlcEventRouter::dialogErrorStatic(void * data, const char * title, const char * text) {
	ofxVlc4 * ownerPtr = resolveOwnerFromRouterData(data);
	if (!ownerPtr) {
		return;
	}

	ofxVlc4::dialogErrorStatic(ownerPtr->m_controlBlock.get(), title, text);
}
