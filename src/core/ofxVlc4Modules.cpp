#include "ofxVlc4Modules.h"

// ---------------------------------------------------------------------------
// AudioModule
// ---------------------------------------------------------------------------

ofxVlc4::AudioModule::AudioModule(ofxVlc4 & owner)
	: m_owner(owner) {}

bool ofxVlc4::AudioModule::isReady() const { return m_owner.audioIsReady(); }
int ofxVlc4::AudioModule::getVolume() const { return m_owner.getVolume(); }
void ofxVlc4::AudioModule::setVolume(int volume) { m_owner.setVolume(volume); }
bool ofxVlc4::AudioModule::isMuted() const { return m_owner.isMuted(); }
void ofxVlc4::AudioModule::toggleMute() { m_owner.toggleMute(); }
uint64_t ofxVlc4::AudioModule::getOverrunCount() const { return m_owner.getAudioOverrunCount(); }
uint64_t ofxVlc4::AudioModule::getUnderrunCount() const { return m_owner.getAudioUnderrunCount(); }
int ofxVlc4::AudioModule::getChannelCount() const { return m_owner.getChannelCount(); }
int ofxVlc4::AudioModule::getSampleRate() const { return m_owner.getSampleRate(); }

// ---------------------------------------------------------------------------
// VideoModule
// ---------------------------------------------------------------------------

ofxVlc4::VideoModule::VideoModule(ofxVlc4 & owner)
	: m_owner(owner) {}

float ofxVlc4::VideoModule::getWidth() const { return m_owner.getWidth(); }
float ofxVlc4::VideoModule::getHeight() const { return m_owner.getHeight(); }
ofTexture & ofxVlc4::VideoModule::getTexture() { return m_owner.getTexture(); }
bool ofxVlc4::VideoModule::isAdjustmentsEnabled() const { return m_owner.isVideoAdjustmentsEnabled(); }
void ofxVlc4::VideoModule::setAdjustmentsEnabled(bool enabled) { m_owner.setVideoAdjustmentsEnabled(enabled); }
float ofxVlc4::VideoModule::getContrast() const { return m_owner.getVideoContrast(); }
void ofxVlc4::VideoModule::setContrast(float contrast) { m_owner.setVideoContrast(contrast); }
float ofxVlc4::VideoModule::getBrightness() const { return m_owner.getVideoBrightness(); }
void ofxVlc4::VideoModule::setBrightness(float brightness) { m_owner.setVideoBrightness(brightness); }
float ofxVlc4::VideoModule::getSaturation() const { return m_owner.getVideoSaturation(); }
void ofxVlc4::VideoModule::setSaturation(float saturation) { m_owner.setVideoSaturation(saturation); }

// ---------------------------------------------------------------------------
// MediaModule
// ---------------------------------------------------------------------------

ofxVlc4::MediaModule::MediaModule(ofxVlc4 & owner)
	: m_owner(owner) {}

void ofxVlc4::MediaModule::addToPlaylist(const std::string & path) { m_owner.addToPlaylist(path); }
void ofxVlc4::MediaModule::clearPlaylist() { m_owner.clearPlaylist(); }
void ofxVlc4::MediaModule::playIndex(int index) { m_owner.playIndex(index); }
std::string ofxVlc4::MediaModule::getCurrentPath() const { return m_owner.getCurrentPath(); }
std::string ofxVlc4::MediaModule::getCurrentFileName() const { return m_owner.getCurrentFileName(); }
int ofxVlc4::MediaModule::getCurrentIndex() const { return m_owner.getCurrentIndex(); }
bool ofxVlc4::MediaModule::hasPlaylist() const { return m_owner.hasPlaylist(); }
ofxVlc4::PlaylistStateInfo ofxVlc4::MediaModule::getPlaylistState() const { return m_owner.getPlaylistStateInfo(); }

// ---------------------------------------------------------------------------
// RecordingModule
// ---------------------------------------------------------------------------

ofxVlc4::RecordingModule::RecordingModule(ofxVlc4 & owner)
	: m_owner(owner) {}

bool ofxVlc4::RecordingModule::startSession(const ofxVlc4RecordingSessionConfig & config) { return m_owner.startRecordingSession(config); }
bool ofxVlc4::RecordingModule::stopSession() { return m_owner.stopRecordingSession(); }
ofxVlc4RecordingSessionState ofxVlc4::RecordingModule::getSessionState() const { return m_owner.getRecordingSessionState(); }
bool ofxVlc4::RecordingModule::isAudioRecording() const { return m_owner.isAudioRecording(); }
bool ofxVlc4::RecordingModule::isVideoRecording() const { return m_owner.isVideoRecording(); }
void ofxVlc4::RecordingModule::setPreset(const ofxVlc4RecordingPreset & preset) { m_owner.setRecordingPreset(preset); }
ofxVlc4RecordingPreset ofxVlc4::RecordingModule::getPreset() const { return m_owner.getRecordingPreset(); }

// ---------------------------------------------------------------------------
// MidiModule
// ---------------------------------------------------------------------------

ofxVlc4::MidiModule::MidiModule(ofxVlc4 & owner)
	: m_owner(owner) {}

bool ofxVlc4::MidiModule::loadFile(const std::string & path, bool noteOffAsZeroVelocity) { return m_owner.loadMidiFile(path, noteOffAsZeroVelocity); }
void ofxVlc4::MidiModule::clear() { m_owner.clearMidiTransport(); }
bool ofxVlc4::MidiModule::isLoaded() const { return m_owner.hasMidiLoaded(); }
bool ofxVlc4::MidiModule::isPlaying() const { return m_owner.isMidiPlaying(); }
void ofxVlc4::MidiModule::play() { m_owner.playMidi(); }
void ofxVlc4::MidiModule::pause() { m_owner.pauseMidi(); }
void ofxVlc4::MidiModule::stop() { m_owner.stopMidi(); }
void ofxVlc4::MidiModule::seek(double seconds) { m_owner.seekMidi(seconds); }
double ofxVlc4::MidiModule::getDurationSeconds() const { return m_owner.getMidiDurationSeconds(); }
double ofxVlc4::MidiModule::getPositionSeconds() const { return m_owner.getMidiPositionSeconds(); }
