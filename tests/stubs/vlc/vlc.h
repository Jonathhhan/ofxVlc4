#pragma once
// Minimal stub of libVLC types used by ofxVlc4Utils.h.
typedef enum libvlc_state_t {
	libvlc_NothingSpecial = 0,
	libvlc_Opening,
	libvlc_Buffering,
	libvlc_Playing,
	libvlc_Paused,
	libvlc_Stopped,
	libvlc_Stopping,
	libvlc_Error
} libvlc_state_t;

struct libvlc_media_player_t;
