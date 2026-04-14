#pragma once

#include "ofxVlc4.h"
#include "ofMain.h"

/**
 * @brief Simplified facade for common ofxVlc4 playback workflows
 *
 * This class provides a simplified ~15 method interface on top of the full
 * ofxVlc4 API (~400 methods), making it easier for beginners to get started
 * with basic video playback.
 *
 * For advanced features (recording, MIDI, NLE, 360°, etc.), use the full
 * ofxVlc4 class directly.
 *
 * Example usage:
 * @code
 * ofxVlc4SimplePlayer player;
 *
 * void ofApp::setup() {
 *     player.load("movie.mp4");
 *     player.play();
 * }
 *
 * void ofApp::update() {
 *     player.update();
 * }
 *
 * void ofApp::draw() {
 *     player.draw(0, 0, ofGetWidth(), ofGetHeight());
 * }
 * @endcode
 */
class ofxVlc4SimplePlayer {
public:
    ofxVlc4SimplePlayer();
    ~ofxVlc4SimplePlayer();

    // Lifecycle
    /**
     * @brief Load a video file and prepare for playback
     * @param path Path to video file (relative or absolute)
     * @return true if loading started successfully
     */
    bool load(const std::string & path);

    /**
     * @brief Close the player and release resources
     */
    void close();

    // Playback control
    /**
     * @brief Start or resume playback
     */
    void play();

    /**
     * @brief Pause playback
     */
    void pause();

    /**
     * @brief Stop playback and return to beginning
     */
    void stop();

    /**
     * @brief Toggle between play and pause states
     */
    void togglePlayPause();

    // State queries
    /**
     * @brief Check if media is currently playing
     * @return true if playing
     */
    bool isPlaying() const;

    /**
     * @brief Check if media is loaded and ready
     * @return true if media is loaded
     */
    bool isLoaded() const;

    // Seeking
    /**
     * @brief Seek to position (0.0 = start, 1.0 = end)
     * @param position Normalized position 0.0 to 1.0
     */
    void setPosition(float position);

    /**
     * @brief Get current playback position
     * @return Normalized position 0.0 to 1.0
     */
    float getPosition() const;

    // Audio
    /**
     * @brief Set volume level
     * @param volume Volume 0-100
     */
    void setVolume(int volume);

    /**
     * @brief Get current volume level
     * @return Volume 0-100
     */
    int getVolume() const;

    // Rendering
    /**
     * @brief Update player state (call every frame)
     */
    void update();

    /**
     * @brief Draw video at specified position and size
     * @param x X position
     * @param y Y position
     * @param w Width
     * @param h Height
     */
    void draw(float x, float y, float w, float h);

    /**
     * @brief Get direct access to the full ofxVlc4 player for advanced features
     * @return Reference to underlying player
     */
    ofxVlc4 & getPlayer();

private:
    ofxVlc4 m_player;
    bool m_initialized;
};
