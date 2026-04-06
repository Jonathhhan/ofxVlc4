#pragma once

#include "ofxVlc4MidiBridge.h"

#include <cstddef>
#include <string>
#include <vector>

class MidiPlaybackSession {
public:
	bool load(const std::string & path, const MidiAnalysisReport & report, const std::vector<MidiChannelMessage> & messages);
	void clear();

	void update(double nowSeconds);
	void play(double nowSeconds);
	void pause(double nowSeconds);
	void stop();
	void seek(double seconds);
	void setTempoMultiplier(double multiplier, double nowSeconds);
	void setMessageCallback(MidiMessageCallback callback);
	void clearMessageCallback();
	bool hasMessageCallback() const;
	void setSyncSettings(const MidiSyncSettings & settings);
	MidiSyncSettings getSyncSettings() const;

	bool isLoaded() const;
	bool isPlaying() const;
	bool isPaused() const;
	bool isStopped() const;
	bool isFinished() const;

	double getDurationSeconds() const;
	double getPositionSeconds() const;
	double getTempoMultiplier() const;
	const std::string & getPath() const;
	const std::vector<MidiChannelMessage> & getMessages() const;
	size_t getLastDispatchBegin() const;
	size_t getLastDispatchEnd() const;
	size_t getDispatchedCount() const;

private:
	void dispatchRange(size_t beginIndex, size_t endIndex);
	void dispatchMessage(const MidiChannelMessage & message) const;
	void allNotesOff();
	void sendTransportStart(bool resume);
	void sendTransportStop();
	void sendSongPositionPointer();
	void emitSyncBetween(double fromSeconds, double toSeconds);
	double tempoAtSeconds(double seconds) const;
	static std::vector<unsigned char> makeQuarterFrameMessage(double seconds, double fps, int piece);

	std::string path;
	double durationSeconds = 0.0;
	double playheadSeconds = 0.0;
	double playbackStartWallSeconds = 0.0;
	double playbackStartPositionSeconds = 0.0;
	double tempoMultiplier = 1.0;
	bool loaded = false;
	bool playing = false;
	bool finished = false;
	std::vector<MidiChannelMessage> messages;
	std::vector<MidiTempoChange> tempoChanges;
	size_t lastDispatchBegin = 0;
	size_t lastDispatchEnd = 0;
	MidiMessageCallback messageCallback;
	MidiSyncSettings syncSettings;
	double nextSyncSeconds = 0.0;
	int nextQuarterFramePiece = 0;
	MidiTimelineCursor cursor;
};
