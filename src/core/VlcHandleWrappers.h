#pragma once

/// RAII wrappers for the core libVLC handle types.
/// Each wrapper calls the matching `libvlc_*_release()` in its destructor,
/// eliminating double-free and leak paths in the teardown sequence.
///
/// These are move-only value types following unique-ownership semantics.

#include "vlc/vlc.h"
#include <utility>

namespace vlc {

/// Unique-ownership wrapper for `libvlc_instance_t *`.
class InstanceHandle {
public:
	InstanceHandle() = default;
	explicit InstanceHandle(libvlc_instance_t * raw) : handle(raw) {}
	~InstanceHandle() { reset(); }

	InstanceHandle(const InstanceHandle &) = delete;
	InstanceHandle & operator=(const InstanceHandle &) = delete;

	InstanceHandle(InstanceHandle && other) noexcept : handle(other.handle) { other.handle = nullptr; }
	InstanceHandle & operator=(InstanceHandle && other) noexcept {
		if (this != &other) { reset(); handle = other.handle; other.handle = nullptr; }
		return *this;
	}

	[[nodiscard]] libvlc_instance_t * get() const noexcept { return handle; }
	explicit operator bool() const noexcept { return handle != nullptr; }

	/// Release the owned handle and take ownership of `raw`.
	void reset(libvlc_instance_t * raw = nullptr) {
		if (handle != nullptr) { libvlc_release(handle); }
		handle = raw;
	}

	/// Release ownership without calling the destructor.
	[[nodiscard]] libvlc_instance_t * release() noexcept { auto * tmp = handle; handle = nullptr; return tmp; }

private:
	libvlc_instance_t * handle = nullptr;
};

/// Unique-ownership wrapper for `libvlc_media_player_t *`.
class PlayerHandle {
public:
	PlayerHandle() = default;
	explicit PlayerHandle(libvlc_media_player_t * raw) : handle(raw) {}
	~PlayerHandle() { reset(); }

	PlayerHandle(const PlayerHandle &) = delete;
	PlayerHandle & operator=(const PlayerHandle &) = delete;

	PlayerHandle(PlayerHandle && other) noexcept : handle(other.handle) { other.handle = nullptr; }
	PlayerHandle & operator=(PlayerHandle && other) noexcept {
		if (this != &other) { reset(); handle = other.handle; other.handle = nullptr; }
		return *this;
	}

	[[nodiscard]] libvlc_media_player_t * get() const noexcept { return handle; }
	explicit operator bool() const noexcept { return handle != nullptr; }

	void reset(libvlc_media_player_t * raw = nullptr) {
		if (handle != nullptr) { libvlc_media_player_release(handle); }
		handle = raw;
	}

	[[nodiscard]] libvlc_media_player_t * release() noexcept { auto * tmp = handle; handle = nullptr; return tmp; }

private:
	libvlc_media_player_t * handle = nullptr;
};

/// Unique-ownership wrapper for `libvlc_media_t *`.
class MediaHandle {
public:
	MediaHandle() = default;
	explicit MediaHandle(libvlc_media_t * raw) : handle(raw) {}
	~MediaHandle() { reset(); }

	MediaHandle(const MediaHandle &) = delete;
	MediaHandle & operator=(const MediaHandle &) = delete;

	MediaHandle(MediaHandle && other) noexcept : handle(other.handle) { other.handle = nullptr; }
	MediaHandle & operator=(MediaHandle && other) noexcept {
		if (this != &other) { reset(); handle = other.handle; other.handle = nullptr; }
		return *this;
	}

	[[nodiscard]] libvlc_media_t * get() const noexcept { return handle; }
	explicit operator bool() const noexcept { return handle != nullptr; }

	void reset(libvlc_media_t * raw = nullptr) {
		if (handle != nullptr) { libvlc_media_release(handle); }
		handle = raw;
	}

	[[nodiscard]] libvlc_media_t * release() noexcept { auto * tmp = handle; handle = nullptr; return tmp; }

private:
	libvlc_media_t * handle = nullptr;
};

} // namespace vlc
