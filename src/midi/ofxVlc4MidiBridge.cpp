#include "ofxVlc4MidiBridge.h"

#include <algorithm>
#include <sstream>

bool MidiBridge::toMessage(const MidiEventRecord & event, MidiChannelMessage & message, bool noteOffAsZeroVelocity) {
	message = {};
	message.trackIndex = event.trackIndex;
	message.tick = event.tick;
	message.seconds = event.seconds;
	message.kind = event.kind;
	message.channel = event.channel;
	message.status = event.status;
	message.data1 = event.data1;
	message.data2 = event.data2;

	if (!event.bytes.empty()) {
		message.bytes = event.bytes;
	} else if (event.status >= 0) {
		message.bytes.push_back(static_cast<unsigned char>(event.status & 0xFF));
		if (event.data1 >= 0) {
			message.bytes.push_back(static_cast<unsigned char>(event.data1 & 0x7F));
		}
		if (event.data2 >= 0) {
			message.bytes.push_back(static_cast<unsigned char>(event.data2 & 0x7F));
		}
	}

	if (event.kind == "sysex" && !message.bytes.empty()) {
		message.type = MidiMessageType::SysEx;
		return true;
	}

	if (event.status < 0 || event.data1 < 0) {
		if (event.kind == "tempo" || event.kind == "marker" || event.kind == "cue_point" || event.kind == "text" ||
			event.kind == "track_name" || event.kind == "instrument_name" || event.kind == "lyrics") {
			message.type = MidiMessageType::Meta;
		}
		return false;
	}

	const int statusType = event.status & 0xF0;
	switch (statusType) {
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xC0:
	case 0xD0:
	case 0xE0:
		message.type = MidiMessageType::ChannelVoice;
		break;
	default:
		message.type = ((event.status & 0xF8) == 0xF8) ? MidiMessageType::SystemRealTime : MidiMessageType::SystemCommon;
		return !message.bytes.empty();
	}

	const bool singleDataByte = (statusType == 0xC0 || statusType == 0xD0);
	unsigned char statusByte = static_cast<unsigned char>(event.status & 0xFF);
	unsigned char data1 = static_cast<unsigned char>(event.data1 & 0x7F);
	unsigned char data2 = static_cast<unsigned char>((event.data2 < 0 ? 0 : event.data2) & 0x7F);

	if (statusType == 0x80 && noteOffAsZeroVelocity) {
		statusByte = static_cast<uint8_t>(0x90 | (event.channel & 0x0F));
		data2 = 0;
	}

	message.bytes.clear();
	message.bytes.push_back(statusByte);
	message.bytes.push_back(data1);
	if (!singleDataByte) {
		message.bytes.push_back(data2);
	}
	return true;
}

bool MidiBridge::toChannelMessage(const MidiEventRecord & event, MidiChannelMessage & message, bool noteOffAsZeroVelocity) {
	if (!toMessage(event, message, noteOffAsZeroVelocity)) {
		return false;
	}
	return message.type == MidiMessageType::ChannelVoice;
}

std::vector<MidiChannelMessage> MidiBridge::toChannelMessages(const MidiAnalysisReport & report, bool noteOffAsZeroVelocity) {
	std::vector<MidiChannelMessage> out;
	out.reserve(report.events.size());
	for (const MidiEventRecord & event : report.events) {
		MidiChannelMessage message;
		if (toChannelMessage(event, message, noteOffAsZeroVelocity)) {
			out.push_back(std::move(message));
		}
	}
	return out;
}

std::vector<MidiChannelMessage> MidiBridge::toMessages(const MidiAnalysisReport & report, bool noteOffAsZeroVelocity) {
	std::vector<MidiChannelMessage> out;
	out.reserve(report.events.size());
	for (const MidiEventRecord & event : report.events) {
		MidiChannelMessage message;
		if (toMessage(event, message, noteOffAsZeroVelocity)) {
			out.push_back(std::move(message));
		}
	}
	return out;
}

std::vector<MidiChannelMessage> MidiBridge::filterByChannel(const std::vector<MidiChannelMessage> & messages, int channel) {
	std::vector<MidiChannelMessage> out;
	for (const MidiChannelMessage & message : messages) {
		if (message.type == MidiMessageType::ChannelVoice && message.channel == channel) {
			out.push_back(message);
		}
	}
	return out;
}

std::string MidiBridge::describe(const MidiChannelMessage & message) {
	std::ostringstream stream;
	stream << "t=" << message.seconds
		   << " tick=" << message.tick
		   << " " << message.kind;
	if (message.type == MidiMessageType::ChannelVoice) {
		stream << " ch=" << (message.channel + 1)
			   << " d1=" << message.data1;
	}
	if (message.type == MidiMessageType::ChannelVoice && !message.bytes.empty() && message.bytes.size() > 2) {
		stream << " d2=" << message.data2;
	} else if (message.type == MidiMessageType::SysEx) {
		stream << " bytes=" << message.bytes.size();
	}
	return stream.str();
}

void MidiTimelineCursor::reset(const std::vector<MidiChannelMessage> * newSource) {
	source = newSource;
	index = 0;
}

void MidiTimelineCursor::rewind() {
	index = 0;
}

void MidiTimelineCursor::seekSeconds(double seconds) {
	if (!source) {
		index = 0;
		return;
	}

	MidiChannelMessage key;
	key.seconds = seconds;
	const auto it = std::lower_bound(source->begin(), source->end(), key,
		[](const MidiChannelMessage & a, const MidiChannelMessage & b) {
			return a.seconds < b.seconds;
		});
	index = static_cast<size_t>(std::distance(source->begin(), it));
}

size_t MidiTimelineCursor::advanceUntil(double seconds) {
	if (!source) {
		return index;
	}

	while (index < source->size() && (*source)[index].seconds <= seconds) {
		++index;
	}
	return index;
}

size_t MidiTimelineCursor::size() const {
	return source ? source->size() : 0;
}

size_t MidiTimelineCursor::position() const {
	return index;
}
