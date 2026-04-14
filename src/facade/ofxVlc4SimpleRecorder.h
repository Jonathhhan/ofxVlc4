#pragma once

#include "ofxVlc4.h"
#include "ofMain.h"

/**
 * @brief Simplified facade for common ofxVlc4 recording workflows
 *
 * This class provides a simplified interface for recording video textures or
 * windows with minimal configuration. Uses sensible defaults and common presets.
 *
 * For advanced recording features (custom codecs, mux settings, performance
 * tuning), use the full ofxVlc4 recorder API directly.
 *
 * Example usage:
 * @code
 * ofxVlc4SimpleRecorder recorder;
 *
 * void ofApp::keyPressed(int key) {
 *     if (key == 'r') {
 *         if (recorder.isRecording()) {
 *             recorder.stopRecording();
 *         } else {
 *             recorder.startRecording("output.mp4", 1920, 1080);
 *         }
 *     }
 * }
 *
 * void ofApp::update() {
 *     if (recorder.isRecording()) {
 *         recorder.recordFrame(myTexture);
 *     }
 * }
 * @endcode
 */
class ofxVlc4SimpleRecorder {
public:
    /**
     * @brief Quality presets for easy configuration
     */
    enum class Quality {
        Low,      ///< 720p 30fps, 2 Mbps
        Medium,   ///< 1080p 30fps, 5 Mbps
        High,     ///< 1080p 60fps, 10 Mbps
        Ultra     ///< 4K 30fps, 20 Mbps H265
    };

    ofxVlc4SimpleRecorder();
    ~ofxVlc4SimpleRecorder();

    // Recording control
    /**
     * @brief Start recording with automatic settings
     * @param outputPath Output file path (.mp4, .mkv, etc.)
     * @param width Recording width (0 = use current window width)
     * @param height Recording height (0 = use current window height)
     * @param quality Quality preset (default: Medium)
     * @return true if recording started successfully
     */
    bool startRecording(
        const std::string & outputPath,
        int width = 0,
        int height = 0,
        Quality quality = Quality::Medium
    );

    /**
     * @brief Start recording the current window
     * @param outputPath Output file path
     * @param quality Quality preset (default: Medium)
     * @return true if recording started successfully
     */
    bool startWindowRecording(
        const std::string & outputPath,
        Quality quality = Quality::Medium
    );

    /**
     * @brief Record a single frame from a texture
     * @param texture Texture to record
     *
     * Call this every frame while recording to capture texture content
     */
    void recordFrame(const ofTexture & texture);

    /**
     * @brief Stop recording and finalize the file
     *
     * This will block briefly to ensure file is properly written
     */
    void stopRecording();

    // State queries
    /**
     * @brief Check if currently recording
     * @return true if recording is active
     */
    bool isRecording() const;

    /**
     * @brief Get the current output file path
     * @return Output file path (empty if not recording)
     */
    std::string getOutputPath() const;

    /**
     * @brief Get direct access to the full ofxVlc4 player for advanced features
     * @return Reference to underlying player
     */
    ofxVlc4 & getPlayer();

private:
    ofxVlc4 m_player;
    bool m_initialized;
    bool m_recording;
    std::string m_outputPath;

    ofxVlc4RecordingPreset getPresetForQuality(Quality quality, int width, int height);
};
