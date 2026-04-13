#include <atomic>
#include <iostream>
#include <utility>

namespace {

bool g_failed = false;

void reportFailure(const char * expr, int line) {
	std::cerr << "[FAIL] line " << line << ": " << expr << std::endl;
	g_failed = true;
}

#define CHECK(expr) \
	do { \
		if (!(expr)) reportFailure(#expr, __LINE__); \
	} while (0)

struct MockEvent {};
struct MockDialogId {};

struct MockOwner {
	std::atomic<bool> shuttingDown { false };
	int playerEvents = 0;
	int mediaEvents = 0;
	int discovererEvents = 0;
	int rendererEvents = 0;
	int dialogLogin = 0;
	int dialogQuestion = 0;
	int dialogProgress = 0;
	int dialogCancel = 0;
	int dialogUpdate = 0;
	int dialogError = 0;
};

struct MockCoreSession {
	using Callback = void (*)(const MockEvent *, void *);

	void attachPlayerEvents(void * data, Callback callback) {
		playerAttachCount++;
		playerData = data;
		playerCb = callback;
	}
	void detachPlayerEvents(void * data, Callback callback) {
		playerDetachCount++;
		playerDetachData = data;
		playerDetachCb = callback;
	}
	void attachMediaEvents(void * data, Callback callback) {
		mediaAttachCount++;
		mediaData = data;
		mediaCb = callback;
	}
	void detachMediaEvents(void * data, Callback callback) {
		mediaDetachCount++;
		mediaDetachData = data;
		mediaDetachCb = callback;
	}
	void attachMediaDiscovererListEvents(void * data, Callback callback) {
		discovererAttachCount++;
		discovererData = data;
		discovererCb = callback;
	}
	void detachMediaDiscovererListEvents(void * data, Callback callback) {
		discovererDetachCount++;
		discovererDetachData = data;
		discovererDetachCb = callback;
	}
	void attachRendererEvents(void * data, Callback callback) {
		rendererAttachCount++;
		rendererData = data;
		rendererCb = callback;
	}
	void detachRendererEvents(void * data, Callback callback) {
		rendererDetachCount++;
		rendererDetachData = data;
		rendererDetachCb = callback;
	}

	int playerAttachCount = 0;
	int playerDetachCount = 0;
	int mediaAttachCount = 0;
	int mediaDetachCount = 0;
	int discovererAttachCount = 0;
	int discovererDetachCount = 0;
	int rendererAttachCount = 0;
	int rendererDetachCount = 0;

	void * playerData = nullptr;
	void * playerDetachData = nullptr;
	void * mediaData = nullptr;
	void * mediaDetachData = nullptr;
	void * discovererData = nullptr;
	void * discovererDetachData = nullptr;
	void * rendererData = nullptr;
	void * rendererDetachData = nullptr;

	Callback playerCb = nullptr;
	Callback playerDetachCb = nullptr;
	Callback mediaCb = nullptr;
	Callback mediaDetachCb = nullptr;
	Callback discovererCb = nullptr;
	Callback discovererDetachCb = nullptr;
	Callback rendererCb = nullptr;
	Callback rendererDetachCb = nullptr;
};

// Minimal seam model that mirrors the router-only callback ownership contract.
class EventRouterLifecycleModel {
public:
	using EventCallback = void (*)(const MockEvent *, void *);

	explicit EventRouterLifecycleModel(MockOwner & ownerRef)
		: owner(ownerRef) {}

	void * callbackData() { return this; }
	EventCallback playerEventCallback() const { return &EventRouterLifecycleModel::playerEventStatic; }
	EventCallback mediaEventCallback() const { return &EventRouterLifecycleModel::mediaEventStatic; }
	EventCallback discovererEventCallback() const { return &EventRouterLifecycleModel::discovererEventStatic; }
	EventCallback rendererEventCallback() const { return &EventRouterLifecycleModel::rendererEventStatic; }

	void attachPlayerEvents(MockCoreSession & session) { session.attachPlayerEvents(callbackData(), playerEventCallback()); }
	void detachPlayerEvents(MockCoreSession & session) { session.detachPlayerEvents(callbackData(), playerEventCallback()); }
	void attachMediaEvents(MockCoreSession & session) { session.attachMediaEvents(callbackData(), mediaEventCallback()); }
	void detachMediaEvents(MockCoreSession & session) { session.detachMediaEvents(callbackData(), mediaEventCallback()); }
	void attachMediaDiscovererListEvents(MockCoreSession & session) { session.attachMediaDiscovererListEvents(callbackData(), discovererEventCallback()); }
	void detachMediaDiscovererListEvents(MockCoreSession & session) { session.detachMediaDiscovererListEvents(callbackData(), discovererEventCallback()); }
	void attachRendererEvents(MockCoreSession & session) { session.attachRendererEvents(callbackData(), rendererEventCallback()); }
	void detachRendererEvents(MockCoreSession & session) { session.detachRendererEvents(callbackData(), rendererEventCallback()); }

	static void playerEventStatic(const MockEvent * event, void * data) {
		dispatchEvent(data, event, [](MockOwner & ownerRef) { ++ownerRef.playerEvents; });
	}
	static void mediaEventStatic(const MockEvent * event, void * data) {
		dispatchEvent(data, event, [](MockOwner & ownerRef) { ++ownerRef.mediaEvents; });
	}
	static void discovererEventStatic(const MockEvent * event, void * data) {
		dispatchEvent(data, event, [](MockOwner & ownerRef) { ++ownerRef.discovererEvents; });
	}
	static void rendererEventStatic(const MockEvent * event, void * data) {
		dispatchEvent(data, event, [](MockOwner & ownerRef) { ++ownerRef.rendererEvents; });
	}

	static void dialogLoginStatic(void * data, MockDialogId * id) {
		dispatchDialog(data, id, [](MockOwner & ownerRef) { ++ownerRef.dialogLogin; });
	}
	static void dialogQuestionStatic(void * data, MockDialogId * id) {
		dispatchDialog(data, id, [](MockOwner & ownerRef) { ++ownerRef.dialogQuestion; });
	}
	static void dialogProgressStatic(void * data, MockDialogId * id) {
		dispatchDialog(data, id, [](MockOwner & ownerRef) { ++ownerRef.dialogProgress; });
	}
	static void dialogCancelStatic(void * data, MockDialogId * id) {
		dispatchDialog(data, id, [](MockOwner & ownerRef) { ++ownerRef.dialogCancel; });
	}
	static void dialogUpdateStatic(void * data, MockDialogId * id) {
		dispatchDialog(data, id, [](MockOwner & ownerRef) { ++ownerRef.dialogUpdate; });
	}
	static void dialogErrorStatic(void * data) {
		dispatchWhenActive(data, [](MockOwner & ownerRef) { ++ownerRef.dialogError; });
	}

private:
	template <typename HandlerFn>
	static void dispatchWhenActive(void * data, HandlerFn && handler) {
		auto * router = static_cast<EventRouterLifecycleModel *>(data);
		if (!router) {
			return;
		}
		if (router->owner.shuttingDown.load(std::memory_order_acquire)) {
			return;
		}
		handler(router->owner);
	}

	template <typename HandlerFn>
	static void dispatchEvent(void * data, const MockEvent * event, HandlerFn && handler) {
		if (!event) {
			return;
		}
		dispatchWhenActive(data, std::forward<HandlerFn>(handler));
	}

	template <typename HandlerFn>
	static void dispatchDialog(void * data, MockDialogId * id, HandlerFn && handler) {
		if (!id) {
			return;
		}
		dispatchWhenActive(data, std::forward<HandlerFn>(handler));
	}

	MockOwner & owner;
};

void testAttachDetachLifecycle() {
	MockOwner owner;
	MockCoreSession session;
	EventRouterLifecycleModel router(owner);

	router.attachPlayerEvents(session);
	router.attachMediaEvents(session);
	router.attachMediaDiscovererListEvents(session);
	router.attachRendererEvents(session);

	CHECK(session.playerAttachCount == 1);
	CHECK(session.mediaAttachCount == 1);
	CHECK(session.discovererAttachCount == 1);
	CHECK(session.rendererAttachCount == 1);
	CHECK(session.playerData == router.callbackData());
	CHECK(session.mediaData == router.callbackData());
	CHECK(session.discovererData == router.callbackData());
	CHECK(session.rendererData == router.callbackData());
	CHECK(session.playerCb == router.playerEventCallback());
	CHECK(session.mediaCb == router.mediaEventCallback());
	CHECK(session.discovererCb == router.discovererEventCallback());
	CHECK(session.rendererCb == router.rendererEventCallback());

	router.detachPlayerEvents(session);
	router.detachMediaEvents(session);
	router.detachMediaDiscovererListEvents(session);
	router.detachRendererEvents(session);

	CHECK(session.playerDetachCount == 1);
	CHECK(session.mediaDetachCount == 1);
	CHECK(session.discovererDetachCount == 1);
	CHECK(session.rendererDetachCount == 1);
	CHECK(session.playerDetachData == router.callbackData());
	CHECK(session.mediaDetachData == router.callbackData());
	CHECK(session.discovererDetachData == router.callbackData());
	CHECK(session.rendererDetachData == router.callbackData());
	CHECK(session.playerDetachCb == router.playerEventCallback());
	CHECK(session.mediaDetachCb == router.mediaEventCallback());
	CHECK(session.discovererDetachCb == router.discovererEventCallback());
	CHECK(session.rendererDetachCb == router.rendererEventCallback());
}

void testShutdownDropBehavior() {
	MockOwner owner;
	EventRouterLifecycleModel router(owner);

	MockEvent event {};
	MockDialogId dialog {};
	void * data = router.callbackData();

	EventRouterLifecycleModel::playerEventStatic(&event, data);
	EventRouterLifecycleModel::mediaEventStatic(&event, data);
	EventRouterLifecycleModel::discovererEventStatic(&event, data);
	EventRouterLifecycleModel::rendererEventStatic(&event, data);
	EventRouterLifecycleModel::dialogLoginStatic(data, &dialog);
	EventRouterLifecycleModel::dialogQuestionStatic(data, &dialog);
	EventRouterLifecycleModel::dialogProgressStatic(data, &dialog);
	EventRouterLifecycleModel::dialogCancelStatic(data, &dialog);
	EventRouterLifecycleModel::dialogUpdateStatic(data, &dialog);
	EventRouterLifecycleModel::dialogErrorStatic(data);

	CHECK(owner.playerEvents == 1);
	CHECK(owner.mediaEvents == 1);
	CHECK(owner.discovererEvents == 1);
	CHECK(owner.rendererEvents == 1);
	CHECK(owner.dialogLogin == 1);
	CHECK(owner.dialogQuestion == 1);
	CHECK(owner.dialogProgress == 1);
	CHECK(owner.dialogCancel == 1);
	CHECK(owner.dialogUpdate == 1);
	CHECK(owner.dialogError == 1);

	owner.shuttingDown.store(true, std::memory_order_release);

	EventRouterLifecycleModel::playerEventStatic(&event, data);
	EventRouterLifecycleModel::mediaEventStatic(&event, data);
	EventRouterLifecycleModel::discovererEventStatic(&event, data);
	EventRouterLifecycleModel::rendererEventStatic(&event, data);
	EventRouterLifecycleModel::dialogLoginStatic(data, &dialog);
	EventRouterLifecycleModel::dialogQuestionStatic(data, &dialog);
	EventRouterLifecycleModel::dialogProgressStatic(data, &dialog);
	EventRouterLifecycleModel::dialogCancelStatic(data, &dialog);
	EventRouterLifecycleModel::dialogUpdateStatic(data, &dialog);
	EventRouterLifecycleModel::dialogErrorStatic(data);

	CHECK(owner.playerEvents == 1);
	CHECK(owner.mediaEvents == 1);
	CHECK(owner.discovererEvents == 1);
	CHECK(owner.rendererEvents == 1);
	CHECK(owner.dialogLogin == 1);
	CHECK(owner.dialogQuestion == 1);
	CHECK(owner.dialogProgress == 1);
	CHECK(owner.dialogCancel == 1);
	CHECK(owner.dialogUpdate == 1);
	CHECK(owner.dialogError == 1);
}

} // namespace

int main() {
	testAttachDetachLifecycle();
	testShutdownDropBehavior();
	if (g_failed) {
		return 1;
	}
	std::cout << "test_event_router_lifecycle: all checks passed" << std::endl;
	return 0;
}
