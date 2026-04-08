#pragma once

#include "ofxVlc4MidiAnalysis.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class MidiMessageType {
	ChannelVoice = 0,
	SysEx,
	SystemCommon,
	SystemRealTime,
	Meta,
	Unknown
};

struct MidiChannelMessage {
	uint32_t trackIndex = 0;
	uint32_t tick = 0;
	double seconds = 0.0;
	std::string kind;
	MidiMessageType type = MidiMessageType::Unknown;
	int channel = -1;
	int status = -1;
	int data1 = -1;
	int data2 = -1;
	std::vector<unsigned char> bytes;
};

enum class MidiSyncMode {
	None = 0,
	MidiClock,
	MidiTimecodeQuarterFrame
};

struct MidiSyncSettings {
	MidiSyncMode mode = MidiSyncMode::None;
	double timecodeFps = 30.0;
	bool sendTransportMessages = true;
	bool sendSongPositionOnSeek = true;
};

using MidiMessageCallback = std::function<void(const MidiChannelMessage &)>;
using MidiFinishedCallback = std::function<void()>;

class MidiBridge {
public:
	static bool toChannelMessage(const MidiEventRecord & event, MidiChannelMessage & message, bool noteOffAsZeroVelocity = false);
	static bool toMessage(const MidiEventRecord & event, MidiChannelMessage & message, bool noteOffAsZeroVelocity = false);
	static std::vector<MidiChannelMessage> toChannelMessages(const MidiAnalysisReport & report, bool noteOffAsZeroVelocity = false);
	static std::vector<MidiChannelMessage> toMessages(const MidiAnalysisReport & report, bool noteOffAsZeroVelocity = false);
	static std::vector<MidiChannelMessage> filterByChannel(const std::vector<MidiChannelMessage> & messages, int channel);
	static std::string describe(const MidiChannelMessage & message);
};

class MidiTimelineCursor {
public:
	void reset(const std::vector<MidiChannelMessage> * source);
	void rewind();
	void seekSeconds(double seconds);
	size_t advanceUntil(double seconds);
	size_t size() const;
	size_t position() const;

private:
	const std::vector<MidiChannelMessage> * source = nullptr;
	size_t index = 0;
};
