#pragma once

#include <vector>

// Minimal stub for openFrameworks ofSoundBuffer.
// Provides only the interface used by ofxVlc4RingBuffer.
class ofSoundBuffer {
public:
	ofSoundBuffer() = default;

	explicit ofSoundBuffer(size_t numSamples) {
		_data.resize(numSamples, 0.0f);
	}

	std::vector<float> & getBuffer() { return _data; }
	const std::vector<float> & getBuffer() const { return _data; }

	size_t size() const { return _data.size(); }
	bool empty() const { return _data.empty(); }

	void resize(size_t n, float value = 0.0f) {
		_data.resize(n, value);
	}

private:
	std::vector<float> _data;
};
