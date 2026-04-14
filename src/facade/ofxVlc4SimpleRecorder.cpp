#include "ofxVlc4SimpleRecorder.h"

ofxVlc4SimpleRecorder::ofxVlc4SimpleRecorder()
    : m_initialized(false)
    , m_recording(false) {
}

ofxVlc4SimpleRecorder::~ofxVlc4SimpleRecorder() {
    if (m_recording) {
        stopRecording();
    }
    if (m_initialized) {
        m_player.close();
    }
}

ofxVlc4RecordingPreset ofxVlc4SimpleRecorder::getPresetForQuality(Quality quality, int width, int height) {
    switch (quality) {
        case Quality::Low:
            return ofxVlc4RecordingPreset::makeH264_720p_30fps();

        case Quality::Medium:
            return ofxVlc4RecordingPreset::makeH264_1080p_30fps();

        case Quality::High: {
            auto preset = ofxVlc4RecordingPreset::makeH264_1080p_30fps();
            preset.videoFrameRate = 60;
            preset.videoBitrateKbps = 10000;
            return preset;
        }

        case Quality::Ultra: {
            auto preset = ofxVlc4RecordingPreset::makeH265_4K_30fps();
            if (width > 0 && height > 0) {
                preset.targetWidth = width;
                preset.targetHeight = height;
            }
            return preset;
        }

        default:
            return ofxVlc4RecordingPreset::makeH264_1080p_30fps();
    }
}

bool ofxVlc4SimpleRecorder::startRecording(
    const std::string & outputPath,
    int width,
    int height,
    Quality quality
) {
    // Initialize on first use
    if (!m_initialized) {
        m_player.init(0, nullptr);
        m_player.setAudioCaptureEnabled(true);
        m_initialized = true;
    }

    if (m_recording) {
        ofLogWarning("ofxVlc4SimpleRecorder") << "Already recording, stop current recording first";
        return false;
    }

    // Use current window dimensions if not specified
    int recordWidth = (width > 0) ? width : ofGetWidth();
    int recordHeight = (height > 0) ? height : ofGetHeight();

    // Get preset for quality level
    auto preset = getPresetForQuality(quality, recordWidth, recordHeight);
    preset.targetWidth = recordWidth;
    preset.targetHeight = recordHeight;

    // Convert to absolute path
    std::string absPath = outputPath;
    if (!ofFilePath::isAbsolute(outputPath)) {
        absPath = ofToDataPath(outputPath, true);
    }

    m_player.setRecordingPreset(preset);

    // Start texture recording session using the static helper
    bool success = m_player.startRecordingSession(ofxVlc4::textureRecordingSessionConfig(
        absPath,
        ofTexture(),  // Placeholder texture - will be updated via recordFrame
        preset,
        44100,  // Default sample rate
        2));    // Default channel count

    if (success) {
        m_recording = true;
        m_outputPath = absPath;
        ofLogNotice("ofxVlc4SimpleRecorder") << "Recording started: " << absPath;
    } else {
        ofLogError("ofxVlc4SimpleRecorder") << "Failed to start recording";
    }

    return success;
}

bool ofxVlc4SimpleRecorder::startWindowRecording(
    const std::string & outputPath,
    Quality quality
) {
    // Initialize on first use
    if (!m_initialized) {
        m_player.init(0, nullptr);
        m_player.setAudioCaptureEnabled(true);
        m_initialized = true;
    }

    if (m_recording) {
        ofLogWarning("ofxVlc4SimpleRecorder") << "Already recording, stop current recording first";
        return false;
    }

    // Get preset for quality level
    auto preset = getPresetForQuality(quality, ofGetWidth(), ofGetHeight());
    preset.targetWidth = ofGetWidth();
    preset.targetHeight = ofGetHeight();

    // Convert to absolute path
    std::string absPath = outputPath;
    if (!ofFilePath::isAbsolute(outputPath)) {
        absPath = ofToDataPath(outputPath, true);
    }

    m_player.setRecordingPreset(preset);

    // Start window recording session using the static helper
    bool success = m_player.startRecordingSession(ofxVlc4::windowRecordingSessionConfig(
        absPath,
        preset,
        44100,  // Default sample rate
        2));    // Default channel count

    if (success) {
        m_recording = true;
        m_outputPath = absPath;
        ofLogNotice("ofxVlc4SimpleRecorder") << "Window recording started: " << absPath;
    } else {
        ofLogError("ofxVlc4SimpleRecorder") << "Failed to start window recording";
    }

    return success;
}

void ofxVlc4SimpleRecorder::recordFrame(const ofTexture & texture) {
    if (!m_recording || !m_initialized) {
        return;
    }

    // The texture recording session automatically captures from the texture
    // No manual frame submission needed with current API
    m_player.update();
}

void ofxVlc4SimpleRecorder::stopRecording() {
    if (!m_recording) {
        return;
    }

    ofLogNotice("ofxVlc4SimpleRecorder") << "Stopping recording...";

    m_player.stopRecordingSession();

    m_recording = false;
    m_outputPath.clear();

    ofLogNotice("ofxVlc4SimpleRecorder") << "Recording stopped";
}

bool ofxVlc4SimpleRecorder::isRecording() const {
    return m_recording;
}

std::string ofxVlc4SimpleRecorder::getOutputPath() const {
    return m_outputPath;
}

ofxVlc4 & ofxVlc4SimpleRecorder::getPlayer() {
    return m_player;
}
