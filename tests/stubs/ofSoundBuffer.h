#pragma once
#include <vector>
#include <cstddef>

// Minimal stub of openFrameworks ofSoundBuffer used by ofxVlc4RingBuffer.
class ofSoundBuffer {
public:
	ofSoundBuffer() = default;

	explicit ofSoundBuffer(std::size_t numSamples)
		: _data(numSamples, 0.0f) {}

	std::vector<float> & getBuffer() { return _data; }
	const std::vector<float> & getBuffer() const { return _data; }

	std::size_t size() const { return _data.size(); }
	void resize(std::size_t n, float v = 0.0f) { _data.resize(n, v); }

private:
	std::vector<float> _data;
};
