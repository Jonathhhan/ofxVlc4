#pragma once

#include "ofxVlc4.h"

// ---------------------------------------------------------------------------
// ofxVlc4::AudioModule
// Thin proxy providing typed access to the most-used audio methods.
// ---------------------------------------------------------------------------

class ofxVlc4::AudioModule {
public:
	explicit AudioModule(ofxVlc4 & owner);

	bool isReady() const;
	int getVolume() const;
	void setVolume(int volume);
	bool isMuted() const;
	void toggleMute();
	uint64_t getOverrunCount() const;
	uint64_t getUnderrunCount() const;
	int getChannelCount() const;
	int getSampleRate() const;

private:
	ofxVlc4 & m_owner;
};

// ---------------------------------------------------------------------------
// ofxVlc4::VideoModule
// Thin proxy providing typed access to the most-used video methods.
// ---------------------------------------------------------------------------

class ofxVlc4::VideoModule {
public:
	explicit VideoModule(ofxVlc4 & owner);

	float getWidth() const;
	float getHeight() const;
	ofTexture & getTexture();
	bool isAdjustmentsEnabled() const;
	void setAdjustmentsEnabled(bool enabled);
	float getContrast() const;
	void setContrast(float contrast);
	float getBrightness() const;
	void setBrightness(float brightness);
	float getSaturation() const;
	void setSaturation(float saturation);

private:
	ofxVlc4 & m_owner;
};

// ---------------------------------------------------------------------------
// ofxVlc4::MediaModule
// Thin proxy providing typed access to the most-used media/playlist methods.
// ---------------------------------------------------------------------------

class ofxVlc4::MediaModule {
public:
	explicit MediaModule(ofxVlc4 & owner);

	void addToPlaylist(const std::string & path);
	void clearPlaylist();
	void playIndex(int index);
	std::string getCurrentPath() const;
	std::string getCurrentFileName() const;
	int getCurrentIndex() const;
	bool hasPlaylist() const;
	ofxVlc4::PlaylistStateInfo getPlaylistState() const;

private:
	ofxVlc4 & m_owner;
};

// ---------------------------------------------------------------------------
// ofxVlc4::RecordingModule
// Thin proxy providing typed access to the most-used recording methods.
// ---------------------------------------------------------------------------

class ofxVlc4::RecordingModule {
public:
	explicit RecordingModule(ofxVlc4 & owner);

	bool startSession(const ofxVlc4RecordingSessionConfig & config);
	bool stopSession();
	ofxVlc4RecordingSessionState getSessionState() const;
	bool isAudioRecording() const;
	bool isVideoRecording() const;
	void setPreset(const ofxVlc4RecordingPreset & preset);
	ofxVlc4RecordingPreset getPreset() const;

private:
	ofxVlc4 & m_owner;
};

// ---------------------------------------------------------------------------
// ofxVlc4::MidiModule
// Thin proxy providing typed access to the most-used MIDI methods.
// ---------------------------------------------------------------------------

class ofxVlc4::MidiModule {
public:
	explicit MidiModule(ofxVlc4 & owner);

	bool loadFile(const std::string & path, bool noteOffAsZeroVelocity = false);
	void clear();
	bool isLoaded() const;
	bool isPlaying() const;
	void play();
	void pause();
	void stop();
	void seek(double seconds);
	double getDurationSeconds() const;
	double getPositionSeconds() const;

private:
	ofxVlc4 & m_owner;
};
