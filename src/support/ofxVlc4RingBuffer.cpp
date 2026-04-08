#include "ofxVlc4RingBuffer.h"

#include <algorithm>
#include <cstring>

namespace {
size_t nextPowerOfTwo(size_t value) {
	if (value < 2) return 2;
	--value;
	for (size_t shift = 1; shift < sizeof(size_t) * 8; shift <<= 1) {
		value |= value >> shift;
	}
	return value + 1;
}
} // namespace

ofxVlc4RingBuffer::ofxVlc4RingBuffer(size_t size) {
	allocate(size);
}

void ofxVlc4RingBuffer::allocate(size_t size) {
	_capacity = nextPowerOfTwo(std::max<size_t>(size, 2));
	_mask = _capacity - 1;
	_buffer.assign(_capacity, 0.0f);
	_readStart.store(0, std::memory_order_relaxed);
	_writeStart.store(0, std::memory_order_relaxed);
	_version.fetch_add(1, std::memory_order_release);
	_overruns.store(0, std::memory_order_relaxed);
	_underruns.store(0, std::memory_order_relaxed);
}

void ofxVlc4RingBuffer::clear() {
	_readStart.store(0, std::memory_order_relaxed);
	_writeStart.store(0, std::memory_order_relaxed);
	_version.fetch_add(1, std::memory_order_release);
	_overruns.store(0, std::memory_order_relaxed);
	_underruns.store(0, std::memory_order_relaxed);
	std::fill(_buffer.begin(), _buffer.end(), 0.0f);
}

void ofxVlc4RingBuffer::reset() {
	_readStart.store(0, std::memory_order_release);
	_writeStart.store(0, std::memory_order_release);
	_version.fetch_add(1, std::memory_order_release);
	_overruns.store(0, std::memory_order_relaxed);
	_underruns.store(0, std::memory_order_relaxed);
}

size_t ofxVlc4RingBuffer::getNumReadableSamples() const {
	const auto writeStart = _writeStart.load(std::memory_order_acquire);
	const auto readStart = _readStart.load(std::memory_order_acquire);
	return (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
}

size_t ofxVlc4RingBuffer::getNumWritableSamples() const {
	return _capacity - getNumReadableSamples();
}

size_t ofxVlc4RingBuffer::writeBegin(float *& first, size_t & firstCount, float *& second, size_t & secondCount) {
	const auto writeStart = _writeStart.load(std::memory_order_relaxed);
	const auto readStart = _readStart.load(std::memory_order_acquire);

	const size_t readable = (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
	const size_t writable = _capacity - readable;

	const auto readPosition = readStart & _mask;
	const auto writePosition = writeStart & _mask;

	first = &_buffer[writePosition];
	second = &_buffer[0];

	if (writePosition >= readPosition) {
		firstCount = std::min(_capacity - writePosition, writable);
		secondCount = writable - firstCount;
	} else {
		firstCount = writable;
		secondCount = 0;
	}
	return writable;
}

void ofxVlc4RingBuffer::writeEnd(size_t numSamples) {
	const auto writeStart = _writeStart.load(std::memory_order_relaxed);
	_writeStart.store(writeStart + numSamples, std::memory_order_release);
	if (numSamples > 0) {
		_version.fetch_add(1, std::memory_order_release);
	}
}

size_t ofxVlc4RingBuffer::readBegin(const float *& first, size_t & firstCount, const float *& second, size_t & secondCount) {
	const auto readStart = _readStart.load(std::memory_order_relaxed);
	const auto writeStart = _writeStart.load(std::memory_order_acquire);

	const size_t readable = (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
	const auto readPosition = readStart & _mask;
	const auto writePosition = writeStart & _mask;

	first = &_buffer[readPosition];
	second = &_buffer[0];

	if (writePosition > readPosition) {
		firstCount = readable;
		secondCount = 0;
	} else if (readable == 0) {
		firstCount = 0;
		secondCount = 0;
	} else {
		firstCount = _capacity - readPosition;
		secondCount = readable - firstCount;
	}

	return readable;
}

void ofxVlc4RingBuffer::readEnd(size_t numSamples) {
	const auto readStart = _readStart.load(std::memory_order_relaxed);
	_readStart.store(readStart + numSamples, std::memory_order_release);
}

size_t ofxVlc4RingBuffer::getReadPosition() {
	return _capacity ? (_readStart.load(std::memory_order_acquire) & _mask) : 0;
}

size_t ofxVlc4RingBuffer::write(const float * src, size_t wanted) {
	if (!src || wanted == 0 || _capacity == 0) return 0;

	float * dst[2];
	size_t count[2] = { 0, 0 };
	const size_t writable = writeBegin(dst[0], count[0], dst[1], count[1]);
	const size_t consumed = std::min(wanted, writable);
	if (consumed == 0) {
		_overruns.fetch_add(1, std::memory_order_relaxed);
		return 0;
	}

	if (consumed <= count[0]) {
		std::memcpy(dst[0], src, consumed * sizeof(float));
	} else {
		std::memcpy(dst[0], src, count[0] * sizeof(float));
		std::memcpy(dst[1], src + count[0], (consumed - count[0]) * sizeof(float));
	}

	writeEnd(consumed);

	if (consumed < wanted) {
		_overruns.fetch_add(1, std::memory_order_relaxed);
	}

	return consumed;
}

size_t ofxVlc4RingBuffer::read(float * dst, size_t wanted) {
	if (!dst || wanted == 0 || _capacity == 0) return 0;

	const float * src[2];
	size_t count[2] = { 0, 0 };
	const size_t readable = readBegin(src[0], count[0], src[1], count[1]);
	const size_t filled = std::min(wanted, readable);

	if (filled > 0) {
		if (filled <= count[0]) {
			std::memcpy(dst, src[0], filled * sizeof(float));
		} else {
			std::memcpy(dst, src[0], count[0] * sizeof(float));
			std::memcpy(dst + count[0], src[1], (filled - count[0]) * sizeof(float));
		}
	}

	readEnd(filled);

	if (filled < wanted) {
		std::memset(dst + filled, 0, (wanted - filled) * sizeof(float));
		_underruns.fetch_add(1, std::memory_order_relaxed);
	}

	return filled;
}

size_t ofxVlc4RingBuffer::read(float * dst, size_t wanted, float gain) {
	const size_t filled = read(dst, wanted);

	if (gain != 1.0f) {
		for (size_t i = 0; i < wanted; ++i) {
			dst[i] *= gain;
		}
	}

	return filled;
}

size_t ofxVlc4RingBuffer::peekLatest(float * dst, size_t wanted) const {
	if (!dst || wanted == 0 || _capacity == 0) return 0;

	const auto writeStart = _writeStart.load(std::memory_order_acquire);
	const auto readStart = _readStart.load(std::memory_order_acquire);
	const size_t readable = (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
	const size_t copied = std::min(wanted, readable);
	const size_t zeroPad = wanted - copied;

	if (zeroPad > 0) {
		std::memset(dst, 0, zeroPad * sizeof(float));
	}
	if (copied == 0) {
		return 0;
	}

	const size_t startIndex = (writeStart - copied) & _mask;
	const size_t firstCount = std::min(_capacity - startIndex, copied);
	if (copied <= firstCount) {
		std::memcpy(dst + zeroPad, _buffer.data() + startIndex, copied * sizeof(float));
	} else {
		std::memcpy(dst + zeroPad, _buffer.data() + startIndex, firstCount * sizeof(float));
		std::memcpy(dst + zeroPad + firstCount, _buffer.data(), (copied - firstCount) * sizeof(float));
	}

	return copied;
}

size_t ofxVlc4RingBuffer::peekLatest(float * dst, size_t wanted, float gain) const {
	const size_t copied = peekLatest(dst, wanted);
	if (gain != 1.0f) {
		for (size_t i = 0; i < wanted; ++i) {
			dst[i] *= gain;
		}
	}

	return copied;
}

void ofxVlc4RingBuffer::readIntoVector(std::vector<float> & data) {
	if (!data.empty()) {
		read(data.data(), data.size());
	}
}

void ofxVlc4RingBuffer::readIntoVector(std::vector<float> & data, float gain) {
	if (!data.empty()) {
		read(data.data(), data.size(), gain);
	}
}

void ofxVlc4RingBuffer::readIntoBuffer(ofSoundBuffer & buffer) {
	auto & data = buffer.getBuffer();
	if (!data.empty()) {
		read(data.data(), data.size());
	}
}

void ofxVlc4RingBuffer::readIntoBuffer(ofSoundBuffer & buffer, float gain) {
	auto & data = buffer.getBuffer();
	if (!data.empty()) {
		read(data.data(), data.size(), gain);
	}
}

void ofxVlc4RingBuffer::writeFromBuffer(const ofSoundBuffer & buffer) {
	const auto & data = buffer.getBuffer();
	if (!data.empty()) {
		write(data.data(), data.size());
	}
}
