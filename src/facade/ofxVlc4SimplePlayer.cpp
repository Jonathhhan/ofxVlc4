#include "ofxVlc4SimplePlayer.h"

ofxVlc4SimplePlayer::ofxVlc4SimplePlayer()
    : m_initialized(false) {
}

ofxVlc4SimplePlayer::~ofxVlc4SimplePlayer() {
    close();
}

bool ofxVlc4SimplePlayer::load(const std::string & path) {
    // Initialize on first load
    if (!m_initialized) {
        m_player.init(0, nullptr);
        m_player.setWatchTimeEnabled(true);
        m_player.setWatchTimeMinPeriodUs(50000);
        m_initialized = true;
    }

    // Convert to absolute path if needed
    std::string absPath = path;
    if (!ofFilePath::isAbsolute(path)) {
        absPath = ofToDataPath(path, true);
    }

    m_player.clearPlaylist();
    m_player.addPathToPlaylist(absPath);

    return true;
}

void ofxVlc4SimplePlayer::close() {
    if (m_initialized) {
        m_player.close();
        m_initialized = false;
    }
}

void ofxVlc4SimplePlayer::play() {
    if (!m_initialized) {
        return;
    }

    // If first play after load, start playback
    if (!m_player.isMediaAttached()) {
        m_player.playIndex(0);
    } else {
        m_player.play();
    }
}

void ofxVlc4SimplePlayer::pause() {
    if (m_initialized) {
        m_player.pause();
    }
}

void ofxVlc4SimplePlayer::stop() {
    if (m_initialized) {
        m_player.stop();
    }
}

void ofxVlc4SimplePlayer::togglePlayPause() {
    if (m_initialized) {
        m_player.togglePlayPause();
    }
}

bool ofxVlc4SimplePlayer::isPlaying() const {
    return m_initialized && m_player.isPlaying();
}

bool ofxVlc4SimplePlayer::isLoaded() const {
    return m_initialized && m_player.isMediaAttached();
}

void ofxVlc4SimplePlayer::setPosition(float position) {
    if (m_initialized) {
        m_player.setPosition(position);
    }
}

float ofxVlc4SimplePlayer::getPosition() const {
    if (m_initialized) {
        return m_player.getPosition();
    }
    return 0.0f;
}

void ofxVlc4SimplePlayer::setVolume(int volume) {
    if (m_initialized) {
        m_player.setVolume(volume);
    }
}

int ofxVlc4SimplePlayer::getVolume() const {
    if (m_initialized) {
        return m_player.getVolume();
    }
    return 100;
}

void ofxVlc4SimplePlayer::update() {
    if (m_initialized) {
        m_player.update();
    }
}

void ofxVlc4SimplePlayer::draw(float x, float y, float w, float h) {
    if (m_initialized && m_player.isMediaAttached()) {
        m_player.draw(x, y, w, h);
    }
}

ofxVlc4 & ofxVlc4SimplePlayer::getPlayer() {
    return m_player;
}
