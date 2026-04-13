#pragma once

#include <cstdint>

typedef struct libvlc_event_t {
	int type;
} libvlc_event_t;

typedef struct libvlc_dialog_id {
	int placeholder;
} libvlc_dialog_id;

typedef struct libvlc_instance_t {
	int placeholder;
} libvlc_instance_t;

typedef enum libvlc_dialog_question_type {
	libvlc_dialog_question_normal = 0,
	libvlc_dialog_question_warning = 1,
	libvlc_dialog_question_critical = 2
} libvlc_dialog_question_type;

typedef struct libvlc_dialog_cbs {
	void (*pf_display_login)(
		void *,
		libvlc_dialog_id *,
		const char *,
		const char *,
		const char *,
		bool);
	void (*pf_display_question)(
		void *,
		libvlc_dialog_id *,
		const char *,
		const char *,
		libvlc_dialog_question_type,
		const char *,
		const char *,
		const char *);
	void (*pf_display_progress)(
		void *,
		libvlc_dialog_id *,
		const char *,
		const char *,
		bool,
		float,
		const char *);
	void (*pf_cancel)(void *, libvlc_dialog_id *);
	void (*pf_update_progress)(void *, libvlc_dialog_id *, float, const char *);
} libvlc_dialog_cbs;

extern "C" {
void libvlc_dialog_set_callbacks(libvlc_instance_t * instance, const libvlc_dialog_cbs * callbacks, void * data);
void libvlc_dialog_set_error_callback(
	libvlc_instance_t * instance,
	void (*cb)(void *, const char *, const char *),
	void * data);
}
