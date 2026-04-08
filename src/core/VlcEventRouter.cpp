#include "VlcEventRouter.h"
#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"

VlcEventRouter::VlcEventRouter(ofxVlc4 & ownerRef)
	: owner(ownerRef) {
}

ofxVlc4 & VlcEventRouter::getOwner() const {
	return owner;
}

void VlcEventRouter::vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router || !event) {
		return;
	}

	router->owner.vlcMediaPlayerEvent(event);
}

void VlcEventRouter::vlcMediaEventStatic(const libvlc_event_t * event, void * data) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router || !event) {
		return;
	}

	router->owner.vlcMediaEvent(event);
}

void VlcEventRouter::mediaDiscovererMediaListEventStatic(const libvlc_event_t * event, void * data) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router || !event) {
		return;
	}

	router->owner.mediaDiscovererMediaListEvent(event);
}

void VlcEventRouter::rendererDiscovererEventStatic(const libvlc_event_t * event, void * data) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router || !event) {
		return;
	}

	router->owner.rendererDiscovererEvent(event);
}

void VlcEventRouter::dialogDisplayLoginStatic(
	void * data,
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	const char * defaultUsername,
	bool askStore) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router) {
		return;
	}

	ofxVlc4::dialogDisplayLoginStatic(
		&router->owner,
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
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router) {
		return;
	}

	ofxVlc4::dialogDisplayQuestionStatic(
		&router->owner,
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
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router) {
		return;
	}

	ofxVlc4::dialogDisplayProgressStatic(
		&router->owner,
		id,
		title,
		text,
		indeterminate,
		position,
		cancel);
}

void VlcEventRouter::dialogCancelStatic(void * data, libvlc_dialog_id * id) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router) {
		return;
	}

	ofxVlc4::dialogCancelStatic(&router->owner, id);
}

void VlcEventRouter::dialogUpdateProgressStatic(void * data, libvlc_dialog_id * id, float position, const char * text) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router) {
		return;
	}

	ofxVlc4::dialogUpdateProgressStatic(&router->owner, id, position, text);
}

void VlcEventRouter::dialogErrorStatic(void * data, const char * title, const char * text) {
	auto * router = static_cast<VlcEventRouter *>(data);
	if (!router) {
		return;
	}

	ofxVlc4::dialogErrorStatic(&router->owner, title, text);
}
