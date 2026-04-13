#include "core/ofxVlc4InitArgsState.h"
#include "media/MediaLibraryState.h"
#include "playback/PlaybackTransportState.h"

#include <cassert>

int main() {
	ofxVlc4InitArgsState initArgs;
	assert(initArgs.extraInitArgs.empty());
	assert(initArgs.audioVisualizerSettings.module == ofxVlc4AudioVisualizerModule::None);

	initArgs.extraInitArgs.push_back("--network-caching=300");
	assert(initArgs.extraInitArgs.size() == 1);

	PlaybackTransportState transport;
	assert(!transport.playbackWanted.load());
	assert(!transport.hasPendingDirectMedia.load());
	assert(transport.pendingActivateIndex.load() == -1);

	transport.pendingDirectMediaSource = "screen://";
	transport.pendingDirectMediaOptions = { ":screen-fps=30" };
	transport.hasPendingDirectMedia.store(true);
	assert(transport.hasPendingDirectMedia.load());
	assert(transport.pendingDirectMediaSource == "screen://");
	assert(transport.pendingDirectMediaOptions.size() == 1);

	MediaLibraryState mediaState;
	assert(mediaState.currentIndex == -1);
	assert(mediaState.playlist.empty());
	mediaState.playlist.push_back("/tmp/sample.mp4");
	mediaState.currentIndex = 0;
	assert(mediaState.playlist.size() == 1);
	assert(mediaState.currentIndex == 0);

	return 0;
}
