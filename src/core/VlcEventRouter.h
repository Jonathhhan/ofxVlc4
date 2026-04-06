#pragma once

#include "vlc/vlc.h"

typedef struct libvlc_dialog_id libvlc_dialog_id;
typedef struct libvlc_media_list_t libvlc_media_list_t;
typedef struct libvlc_renderer_discoverer_t libvlc_renderer_discoverer_t;

class ofxVlc4;

class VlcEventRouter {
public:
	explicit VlcEventRouter(ofxVlc4 & owner);

	ofxVlc4 & getOwner() const;

	// Phase-1 refactor landing zone: callback registration and forwarding move here
	// before playback/media/video logic is extracted.
	static void vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data);
	static void vlcMediaEventStatic(const libvlc_event_t * event, void * data);
	static void mediaDiscovererMediaListEventStatic(const libvlc_event_t * event, void * data);
	static void rendererDiscovererEventStatic(const libvlc_event_t * event, void * data);

	static void dialogDisplayLoginStatic(
		void * data,
		libvlc_dialog_id * id,
		const char * title,
		const char * text,
		const char * defaultUsername,
		bool askStore);
	static void dialogDisplayQuestionStatic(
		void * data,
		libvlc_dialog_id * id,
		const char * title,
		const char * text,
		libvlc_dialog_question_type type,
		const char * cancel,
		const char * action1,
		const char * action2);
	static void dialogDisplayProgressStatic(
		void * data,
		libvlc_dialog_id * id,
		const char * title,
		const char * text,
		bool indeterminate,
		float position,
		const char * cancel);
	static void dialogCancelStatic(void * data, libvlc_dialog_id * id);
	static void dialogUpdateProgressStatic(void * data, libvlc_dialog_id * id, float position, const char * text);
	static void dialogErrorStatic(void * data, const char * title, const char * text);

private:
	ofxVlc4 & owner;
};
