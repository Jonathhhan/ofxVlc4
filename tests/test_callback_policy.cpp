#include "core/VlcEventCallbackPolicy.h"

#include <cassert>

namespace {

void directEvent(const void *, void *) {}
void routedEvent(const void *, void *) {}

void directDialog(void *, int) {}
void routedDialog(void *, int) {}

void directError(void *, const char *, const char *) {}
void routedError(void *, const char *, const char *) {}

} // namespace

int main() {
	int controlBlock = 123;
	int routerState = 456;

	void * selected = VlcEventCallbackPolicy::selectCallbackData(&routerState, &controlBlock);
	assert(selected == &routerState);

	selected = VlcEventCallbackPolicy::selectCallbackData(nullptr, &controlBlock);
	assert(selected == &controlBlock);

	using EventCallback = void (*)(const void *, void *);
	const EventCallback routedEventResult = VlcEventCallbackPolicy::selectCallback(
		true,
		routedEvent,
		directEvent);
	assert(routedEventResult == routedEvent);

	const EventCallback directEventResult = VlcEventCallbackPolicy::selectCallback(
		false,
		routedEvent,
		directEvent);
	assert(directEventResult == directEvent);

	using DialogCallback = void (*)(void *, int);
	const DialogCallback routedDialogResult = VlcEventCallbackPolicy::selectCallback(
		&routerState,
		routedDialog,
		directDialog);
	assert(routedDialogResult == routedDialog);

	const DialogCallback directDialogResult = VlcEventCallbackPolicy::selectCallback(
		static_cast<void *>(nullptr),
		routedDialog,
		directDialog);
	assert(directDialogResult == directDialog);

	using ErrorCallback = void (*)(void *, const char *, const char *);
	const ErrorCallback routedErrorResult = VlcEventCallbackPolicy::selectCallback(
		true,
		routedError,
		directError);
	assert(routedErrorResult == routedError);

	const ErrorCallback directErrorResult = VlcEventCallbackPolicy::selectCallback(
		false,
		routedError,
		directError);
	assert(directErrorResult == directError);

	return 0;
}
