#include "VlcEventRouter.h"

#include <atomic>
#include <iostream>

namespace {

bool g_failed = false;

libvlc_dialog_cbs g_dialogCallbacks {};
void * g_dialogCallbackData = nullptr;
void (*g_dialogErrorCallback)(void *, const char *, const char *) = nullptr;
void * g_dialogErrorCallbackData = nullptr;

void reportFailure(const char * expr, int line) {
	std::cerr << "[FAIL] line " << line << ": " << expr << std::endl;
	g_failed = true;
}

#define CHECK(expr) \
	do { \
		if (!(expr)) reportFailure(#expr, __LINE__); \
	} while (0)

void testAttachDetachLifecycle() {
	ofxVlc4 owner;
	VlcEventRouter router(owner);
	VlcCoreSession session;

	router.attachPlayerEvents(session);
	router.attachMediaEvents(session);
	router.attachMediaDiscovererListEvents(session);
	router.attachRendererEvents(session);

	CHECK(session.playerAttachCount == 1);
	CHECK(session.mediaAttachCount == 1);
	CHECK(session.discovererAttachCount == 1);
	CHECK(session.rendererAttachCount == 1);

	CHECK(session.lastPlayerData == router.callbackData());
	CHECK(session.lastMediaData == router.callbackData());
	CHECK(session.lastDiscovererData == router.callbackData());
	CHECK(session.lastRendererData == router.callbackData());

	CHECK(session.lastPlayerCallback == router.playerEventCallback());
	CHECK(session.lastMediaCallback == router.mediaEventCallback());
	CHECK(session.lastDiscovererCallback == router.mediaDiscovererListEventCallback());
	CHECK(session.lastRendererCallback == router.rendererDiscovererEventCallback());

	router.detachPlayerEvents(session);
	router.detachMediaEvents(session);
	router.detachMediaDiscovererListEvents(session);
	router.detachRendererEvents(session);

	CHECK(session.playerDetachCount == 1);
	CHECK(session.mediaDetachCount == 1);
	CHECK(session.discovererDetachCount == 1);
	CHECK(session.rendererDetachCount == 1);

	CHECK(session.lastPlayerDetachData == router.callbackData());
	CHECK(session.lastMediaDetachData == router.callbackData());
	CHECK(session.lastDiscovererDetachData == router.callbackData());
	CHECK(session.lastRendererDetachData == router.callbackData());

	CHECK(session.lastPlayerDetachCallback == router.playerEventCallback());
	CHECK(session.lastMediaDetachCallback == router.mediaEventCallback());
	CHECK(session.lastDiscovererDetachCallback == router.mediaDiscovererListEventCallback());
	CHECK(session.lastRendererDetachCallback == router.rendererDiscovererEventCallback());

	libvlc_instance_t instance {};
	router.applyDialogCallbacks(&instance);

	CHECK(g_dialogCallbackData == router.callbackData());
	CHECK(g_dialogErrorCallbackData == router.callbackData());
	CHECK(g_dialogErrorCallback == router.dialogErrorCallback());
	CHECK(g_dialogCallbacks.pf_display_login == router.dialogCallbacks().pf_display_login);
	CHECK(g_dialogCallbacks.pf_display_question == router.dialogCallbacks().pf_display_question);
	CHECK(g_dialogCallbacks.pf_display_progress == router.dialogCallbacks().pf_display_progress);
	CHECK(g_dialogCallbacks.pf_cancel == router.dialogCallbacks().pf_cancel);
	CHECK(g_dialogCallbacks.pf_update_progress == router.dialogCallbacks().pf_update_progress);
}

void testShutdownDropsEventsAndDialogs() {
	ofxVlc4 owner;
	VlcEventRouter router(owner);

	libvlc_event_t event {};
	libvlc_dialog_id id {};

	void * data = router.callbackData();

	VlcEventRouter::vlcMediaPlayerEventStatic(&event, data);
	VlcEventRouter::vlcMediaEventStatic(&event, data);
	VlcEventRouter::mediaDiscovererMediaListEventStatic(&event, data);
	VlcEventRouter::rendererDiscovererEventStatic(&event, data);
	VlcEventRouter::dialogDisplayLoginStatic(data, &id, "title", "text", "user", true);
	VlcEventRouter::dialogDisplayQuestionStatic(
		data,
		&id,
		"title",
		"text",
		libvlc_dialog_question_warning,
		"cancel",
		"a1",
		"a2");
	VlcEventRouter::dialogDisplayProgressStatic(data, &id, "title", "text", false, 0.5f, "cancel");
	VlcEventRouter::dialogCancelStatic(data, &id);
	VlcEventRouter::dialogUpdateProgressStatic(data, &id, 0.8f, "update");
	VlcEventRouter::dialogErrorStatic(data, "err", "msg");

	CHECK(owner.playerEventCount == 1);
	CHECK(owner.mediaEventCount == 1);
	CHECK(owner.discovererEventCount == 1);
	CHECK(owner.rendererEventCount == 1);
	CHECK(owner.dialogLoginCount == 1);
	CHECK(owner.dialogQuestionCount == 1);
	CHECK(owner.dialogProgressCount == 1);
	CHECK(owner.dialogCancelCount == 1);
	CHECK(owner.dialogUpdateCount == 1);
	CHECK(owner.dialogErrorCount == 1);

	owner.m_impl->lifecycleRuntime.shuttingDown.store(true, std::memory_order_release);

	VlcEventRouter::vlcMediaPlayerEventStatic(&event, data);
	VlcEventRouter::vlcMediaEventStatic(&event, data);
	VlcEventRouter::mediaDiscovererMediaListEventStatic(&event, data);
	VlcEventRouter::rendererDiscovererEventStatic(&event, data);
	VlcEventRouter::dialogDisplayLoginStatic(data, &id, "title", "text", "user", true);
	VlcEventRouter::dialogDisplayQuestionStatic(
		data,
		&id,
		"title",
		"text",
		libvlc_dialog_question_warning,
		"cancel",
		"a1",
		"a2");
	VlcEventRouter::dialogDisplayProgressStatic(data, &id, "title", "text", false, 0.5f, "cancel");
	VlcEventRouter::dialogCancelStatic(data, &id);
	VlcEventRouter::dialogUpdateProgressStatic(data, &id, 0.8f, "update");
	VlcEventRouter::dialogErrorStatic(data, "err", "msg");

	CHECK(owner.playerEventCount == 1);
	CHECK(owner.mediaEventCount == 1);
	CHECK(owner.discovererEventCount == 1);
	CHECK(owner.rendererEventCount == 1);
	CHECK(owner.dialogLoginCount == 1);
	CHECK(owner.dialogQuestionCount == 1);
	CHECK(owner.dialogProgressCount == 1);
	CHECK(owner.dialogCancelCount == 1);
	CHECK(owner.dialogUpdateCount == 1);
	CHECK(owner.dialogErrorCount == 1);
}

} // namespace

extern "C" void libvlc_dialog_set_callbacks(libvlc_instance_t *, const libvlc_dialog_cbs * callbacks, void * data) {
	g_dialogCallbacks = callbacks ? *callbacks : libvlc_dialog_cbs {};
	g_dialogCallbackData = data;
}

extern "C" void libvlc_dialog_set_error_callback(
	libvlc_instance_t *,
	void (*cb)(void *, const char *, const char *),
	void * data) {
	g_dialogErrorCallback = cb;
	g_dialogErrorCallbackData = data;
}

int main() {
	testAttachDetachLifecycle();
	testShutdownDropsEventsAndDialogs();
	if (g_failed) {
		return 1;
	}
	std::cout << "test_event_router: all checks passed" << std::endl;
	return 0;
}
