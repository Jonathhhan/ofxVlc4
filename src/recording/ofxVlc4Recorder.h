#pragma once

#include "ofMain.h"
#include "support/ofxVlc4RingBuffer.h"
#include "vlc/vlc.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

class ofxVlc4;

enum class ofxVlc4VideoReadbackPolicy {
	DropLateFrames,
	BlockForFreshestFrame
};

struct ofxVlc4RecorderPerformanceInfo {
	bool asyncVideoReadbackEnabled = false;
	bool asyncVideoReadbackPrimed = false;
	ofxVlc4VideoReadbackPolicy readbackPolicy = ofxVlc4VideoReadbackPolicy::DropLateFrames;
	size_t readbackBufferCount = 0;
	uint64_t captureStartTimeUs = 0;
	uint64_t submittedFrameCount = 0;
	uint64_t readyFrameCount = 0;
	uint64_t synchronousFrameCount = 0;
	uint64_t fallbackFrameCount = 0;
	uint64_t droppedFrameCount = 0;
	uint64_t policyDroppedFrameCount = 0;
	uint64_t mapFailureCount = 0;
	uint64_t pendingFrameCount = 0;
	uint64_t maxPendingFrameCount = 0;
	uint64_t lastCaptureMicros = 0;
	uint64_t averageCaptureMicros = 0;
	uint64_t maxCaptureMicros = 0;
	uint64_t lastReadbackLatencyMicros = 0;
	uint64_t averageReadbackLatencyMicros = 0;
	uint64_t maxReadbackLatencyMicros = 0;
	uint64_t waitCount = 0;
	uint64_t averageWaitMicros = 0;
	uint64_t maxWaitMicros = 0;
	double submittedFramesPerSecond = 0.0;
	double readyFramesPerSecond = 0.0;
};

class ofxVlc4Recorder {
public:
private:
	friend class ofxVlc4;

	static constexpr uint64_t kMaxWavDataBytes = 0xFFFFFFFFull;
	static constexpr double kWavWarningThresholdRatio = 0.9;
	static constexpr double kMinAudioRingBufferSeconds = 1.0;
	void resetAudioCaptureState();
	void resetAudioCaptureBuffer(int sampleRate, int channelCount);
	void prepareAudioCaptureBuffer(int sampleRate, int channelCount);
	void captureAudioSamples(const float * samples, size_t sampleCount);
	void resetCapturedAudio();
	bool isAudioCaptureActive() const;
	bool isVideoCaptureActive() const;
	bool hasActiveCaptureSession() const;
	bool needsCaptureUpdate() const;
	bool hasCleanupState() const;
	ofxVlc4RecorderPerformanceInfo getPerformanceInfo() const;
	void clearCaptureState();
	bool startAudioCapture(const std::string & audioPath, int sampleRate, int channelCount, std::string & errorOut);
	std::string finishAudioCapture();
	const std::string & getLastFinishedAudioPath() const;
	const std::string & getLastFinishedVideoPath() const;
	bool setVideoCaptureFrameRate(int fps, std::string & errorOut);
	int getVideoCaptureFrameRate() const;
	bool setVideoCaptureBitrateKbps(int bitrateKbps, std::string & errorOut);
	int getVideoCaptureBitrateKbps() const;
	bool setVideoCaptureCodec(const std::string & codec, std::string & errorOut);
	const std::string & getVideoCaptureCodec() const;
	void setVideoReadbackPolicy(ofxVlc4VideoReadbackPolicy policy);
	ofxVlc4VideoReadbackPolicy getVideoReadbackPolicy() const;
	void setVideoReadbackBufferCount(size_t bufferCount);
	size_t getVideoReadbackBufferCount() const;
	void setAudioRingBufferSeconds(double seconds);
	double getAudioRingBufferSeconds() const;
	libvlc_media_t * beginVideoCapture(
		const ofTexture & texture,
		const std::string & videoPath,
		int outputWidth,
		int outputHeight,
		std::string & errorOut);
	std::string updateCaptureState();
	bool initializeVideoReadbackBuffersLocked(size_t frameBytes);
	void destroyVideoReadbackBuffersLocked();
	void publishCapturedFrameLocked();
	bool updateCaptureTextureLocked();
	void captureVideoFrameLocked();
	bool waitForSubmittedReadbackLocked(size_t bufferIndex, uint64_t & waitMicrosOut);
	bool consumeReadbackBufferLocked(size_t bufferIndex);
	bool tryConsumeSubmittedReadbackLocked(
		size_t bufferIndex,
		bool blockUntilReady,
		bool & consumedOut,
		uint64_t & waitMicrosOut);
	bool drainAvailableReadbackBuffersLocked(bool blockUntilReady);
	void clearBufferedAudioCapture();
	void drainBufferedAudioLocked();
	void finalizeAudioCaptureStream();
	void closeAudioCapture();
	void failAudioCapture(const std::string & message);
	std::string takePendingError();
	void clearVideoRecording();
	void clearAudioRecording();
	static void writeWavHeader(std::ofstream & stream, int sampleRate, int channels, uint32_t dataBytes);
	void writeInterleaved(const float * samples, size_t sampleCount);
	static int textureOpen(void * data, void ** datap, uint64_t * sizep);
	static long long textureRead(void * data, unsigned char * buffer, size_t size);
	static int textureSeek(void * data, uint64_t offset);
	static void textureClose(void * data);

	int sampleRate = 0;
	int channelCount = 0;
	uint64_t dataBytes = 0;
	bool wavSizeLimitWarned = false;
	double audioRingBufferSeconds = 4.0;
	std::string outputPath;
	std::string lastFinishedAudioPath;
	std::string videoOutputPath;
	std::string lastFinishedVideoPath;
	std::string lastError;
	mutable std::mutex errorMutex;
	std::ofstream stream;
	int videoFrameRate = 30;
	uint64_t videoFrameIntervalUs = 33333;
	int videoBitrateKbps = 8000;
	std::string videoCodec = "MJPG";

	std::atomic<bool> videoRecordingActive { false };
	std::atomic<bool> audioRecordingActive { false };
	std::atomic<bool> errorPending { false };
	ofTexture recordingSourceTexture;
	ofTexture recordingTexture;
	ofFbo recordingResizeFbo;
	ofPixels recordingPixels;
	mutable std::mutex recordingMutex;
	mutable std::mutex audioRecordingMutex;
	size_t recordingFrameSize = 0;
	uint64_t recordingReadOffset = 0;
	uint64_t recordingFrameSerial = 0;
	uint64_t recordingReadFrameSerial = 0;
	uint64_t lastVideoCaptureTimeUs = 0;
	std::condition_variable recordingFrameReadyCondition;
	std::vector<GLuint> recordingPixelPackBuffers;
	std::vector<GLsync> recordingPixelPackFences;
	std::vector<uint64_t> recordingPboSubmitTimesUs;
	std::deque<size_t> recordingPboPendingIndices;
	size_t recordingPboWriteIndex = 0;
	bool recordingPboPrimed = false;
	bool recordingPboEnabled = false;
	size_t recordingPboBufferCount = 3;
	ofxVlc4VideoReadbackPolicy readbackPolicy = ofxVlc4VideoReadbackPolicy::DropLateFrames;
	uint64_t videoCaptureStartTimeUs = 0;
	uint64_t videoFramesSubmitted = 0;
	uint64_t videoFramesReady = 0;
	uint64_t videoAsyncFramesReady = 0;
	uint64_t videoSynchronousFrames = 0;
	uint64_t videoFallbackFrames = 0;
	uint64_t videoDroppedFrames = 0;
	uint64_t videoPolicyDroppedFrames = 0;
	uint64_t videoMapFailureCount = 0;
	uint64_t videoMaxPendingFrames = 0;
	uint64_t videoLastCaptureMicros = 0;
	uint64_t videoTotalCaptureMicros = 0;
	uint64_t videoMaxCaptureMicros = 0;
	uint64_t videoLastReadbackLatencyMicros = 0;
	uint64_t videoTotalReadbackLatencyMicros = 0;
	uint64_t videoMaxReadbackLatencyMicros = 0;
	uint64_t videoWaitCount = 0;
	uint64_t videoTotalWaitMicros = 0;
	uint64_t videoMaxWaitMicros = 0;
	std::vector<float> audioTransferScratch;
	ofxVlc4RingBuffer audioRingBuffer;
};
