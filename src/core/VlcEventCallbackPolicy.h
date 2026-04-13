#pragma once

namespace VlcEventCallbackPolicy {

inline void * selectCallbackData(void * eventRouter, void * controlBlock) {
	return eventRouter ? eventRouter : controlBlock;
}

template <typename Callback>
inline constexpr Callback selectCallback(
	bool useEventRouter,
	Callback routerCallback,
	Callback directCallback) {
	return useEventRouter ? routerCallback : directCallback;
}

template <typename Callback>
inline constexpr Callback selectCallback(
	void * eventRouter,
	Callback routerCallback,
	Callback directCallback) {
	return selectCallback(eventRouter != nullptr, routerCallback, directCallback);
}

} // namespace VlcEventCallbackPolicy
