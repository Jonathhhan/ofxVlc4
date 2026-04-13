#pragma once

#include <atomic>

struct ofxVlc4Impl {
	struct LifecycleRuntimeState {
		std::atomic<bool> shuttingDown { false };
	} lifecycleRuntime;
};
