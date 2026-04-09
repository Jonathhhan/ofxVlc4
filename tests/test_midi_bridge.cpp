// Tests for MidiBridge and MidiTimelineCursor from ofxVlc4MidiBridge.h/.cpp.
// These classes have no dependencies on OF, GLFW, or VLC.

#include "ofxVlc4MidiBridge.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness (mirrors other test files)
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

static void beginSuite(const char * name) {
	std::printf("\n[%s]\n", name);
}

static void check(bool condition, const char * expr, const char * file, int line) {
	if (condition) {
		++g_passed;
		std::printf("  PASS  %s\n", expr);
	} else {
		++g_failed;
		std::printf("  FAIL  %s  (%s:%d)\n", expr, file, line);
	}
}

#define CHECK(expr)    check((expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check((a) == (b), #a " == " #b, __FILE__, __LINE__)

// ---------------------------------------------------------------------------
// Helper: build a MidiEventRecord for a channel voice message.
// ---------------------------------------------------------------------------

static MidiEventRecord makeChannelVoiceEvent(
	int status,
	int data1,
	int data2,
	const std::string & kind,
	int channel,
	double seconds = 0.0,
	uint32_t tick = 0) {
	MidiEventRecord e;
	e.status = status;
	e.data1 = data1;
	e.data2 = data2;
	e.kind = kind;
	e.channel = channel;
	e.seconds = seconds;
	e.tick = tick;
	return e;
}

// ---------------------------------------------------------------------------
// MidiBridge::toMessage
// ---------------------------------------------------------------------------

static void testToMessageNoteOn() {
	beginSuite("toMessage: Note On");

	MidiEventRecord event = makeChannelVoiceEvent(0x90, 60, 100, "note_on", 0);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	CHECK_EQ(message.status, 0x90);
	CHECK_EQ(message.data1, 60);
	CHECK_EQ(message.data2, 100);
	CHECK_EQ(message.channel, 0);
	CHECK_EQ(message.kind, "note_on");
	CHECK_EQ(message.bytes.size(), 3u);
	CHECK_EQ(message.bytes[0], (unsigned char)0x90);
	CHECK_EQ(message.bytes[1], (unsigned char)60);
	CHECK_EQ(message.bytes[2], (unsigned char)100);
}

static void testToMessageNoteOff() {
	beginSuite("toMessage: Note Off");

	MidiEventRecord event = makeChannelVoiceEvent(0x80, 60, 64, "note_off", 0);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	CHECK_EQ(message.bytes[0], (unsigned char)0x80);
}

static void testToMessageNoteOffAsZeroVelocity() {
	beginSuite("toMessage: Note Off remapped as Note On with velocity 0");

	MidiEventRecord event = makeChannelVoiceEvent(0x80, 60, 64, "note_off", 3);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message, /*noteOffAsZeroVelocity=*/true);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	// Status byte should be Note On on channel 3
	CHECK_EQ(message.bytes[0], (unsigned char)(0x90 | 3));
	// Velocity should be 0
	CHECK_EQ(message.bytes[2], (unsigned char)0);
}

static void testToMessageControlChange() {
	beginSuite("toMessage: Control Change");

	MidiEventRecord event = makeChannelVoiceEvent(0xB0, 7, 100, "control_change", 0);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	CHECK_EQ(message.bytes.size(), 3u);
}

static void testToMessageProgramChange() {
	beginSuite("toMessage: Program Change (single data byte)");

	MidiEventRecord event = makeChannelVoiceEvent(0xC0, 5, -1, "program_change", 1);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	// Program Change only has status + data1
	CHECK_EQ(message.bytes.size(), 2u);
	CHECK_EQ(message.bytes[0], (unsigned char)0xC0);
	CHECK_EQ(message.bytes[1], (unsigned char)5);
}

static void testToMessageChannelPressure() {
	beginSuite("toMessage: Channel Pressure (single data byte)");

	MidiEventRecord event = makeChannelVoiceEvent(0xD2, 80, -1, "channel_pressure", 2);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	CHECK_EQ(message.bytes.size(), 2u);
}

static void testToMessagePitchBend() {
	beginSuite("toMessage: Pitch Bend");

	MidiEventRecord event = makeChannelVoiceEvent(0xE0, 0, 64, "pitch_bend", 0);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	CHECK_EQ(message.bytes.size(), 3u);
}

static void testToMessageAftertouch() {
	beginSuite("toMessage: Polyphonic Aftertouch");

	MidiEventRecord event = makeChannelVoiceEvent(0xA5, 60, 50, "aftertouch", 5);
	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
	CHECK_EQ(message.bytes.size(), 3u);
}

static void testToMessageSysEx() {
	beginSuite("toMessage: SysEx");

	MidiEventRecord e;
	e.kind = "sysex";
	e.status = -1;
	e.data1 = -1;
	e.data2 = -1;
	e.channel = -1;
	e.bytes = { 0xF0, 0x41, 0xF7 };

	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(e, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::SysEx);
	CHECK_EQ(message.bytes.size(), 3u);
}

static void testToMessageMeta() {
	beginSuite("toMessage: Meta event (tempo) → returns false");

	MidiEventRecord e;
	e.kind = "tempo";
	e.status = -1;
	e.data1 = -1;
	e.data2 = -1;
	e.channel = -1;

	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(e, message);

	CHECK(!result);
	CHECK_EQ(message.type, MidiMessageType::Meta);
}

// System Real-Time events from MIDI files (e.g. MIDI Clock 0xF8) have
// data1 = -1, so toMessage() returns false before reaching the switch — these
// messages are dispatched directly by MidiPlaybackSession, not via toMessage.
static void testToMessageSystemRealTime() {
	beginSuite("toMessage: System Real-Time (MIDI Clock 0xF8) returns false");

	MidiEventRecord e;
	e.kind = "clock";
	e.status = 0xF8;
	e.data1 = -1;
	e.data2 = -1;
	e.channel = -1;
	e.bytes = { 0xF8 };

	MidiChannelMessage message;
	const bool result = MidiBridge::toMessage(e, message);

	// RT messages with no data byte short-circuit out (data1 < 0).
	CHECK(!result);
}

static void testToMessageBytesPreserved() {
	beginSuite("toMessage: pre-populated bytes field is kept");

	MidiEventRecord event = makeChannelVoiceEvent(0x90, 60, 100, "note_on", 0);
	event.bytes = { 0x90, 60, 100 };

	MidiChannelMessage message;
	MidiBridge::toMessage(event, message);

	// The pre-populated bytes must come through unchanged.
	CHECK_EQ(message.bytes.size(), 3u);
	CHECK_EQ(message.bytes[0], (unsigned char)0x90);
}

// ---------------------------------------------------------------------------
// MidiBridge::toChannelMessage
// ---------------------------------------------------------------------------

static void testToChannelMessageAcceptsChannelVoice() {
	beginSuite("toChannelMessage: accepts channel voice");

	MidiEventRecord event = makeChannelVoiceEvent(0x91, 64, 80, "note_on", 1);
	MidiChannelMessage message;
	const bool result = MidiBridge::toChannelMessage(event, message);

	CHECK(result);
	CHECK_EQ(message.type, MidiMessageType::ChannelVoice);
}

static void testToChannelMessageRejectsSysEx() {
	beginSuite("toChannelMessage: rejects SysEx");

	MidiEventRecord e;
	e.kind = "sysex";
	e.status = -1;
	e.data1 = -1;
	e.data2 = -1;
	e.channel = -1;
	e.bytes = { 0xF0, 0xF7 };

	MidiChannelMessage message;
	const bool result = MidiBridge::toChannelMessage(e, message);

	CHECK(!result);
}

static void testToChannelMessageRejectsMeta() {
	beginSuite("toChannelMessage: rejects meta");

	MidiEventRecord e;
	e.kind = "marker";
	e.status = -1;
	e.data1 = -1;
	e.data2 = -1;
	e.channel = -1;

	MidiChannelMessage message;
	const bool result = MidiBridge::toChannelMessage(e, message);

	CHECK(!result);
}

// ---------------------------------------------------------------------------
// MidiBridge::toChannelMessages / toMessages
// ---------------------------------------------------------------------------

static void testToChannelMessages() {
	beginSuite("toChannelMessages");

	MidiAnalysisReport report;
	report.valid = true;

	// Two Note On events and one meta event.
	report.events.push_back(makeChannelVoiceEvent(0x90, 60, 100, "note_on", 0, 0.0, 0));
	report.events.push_back(makeChannelVoiceEvent(0x90, 64, 90, "note_on", 1, 0.5, 240));

	MidiEventRecord meta;
	meta.kind = "tempo";
	meta.status = -1;
	meta.data1 = -1;
	meta.data2 = -1;
	meta.channel = -1;
	report.events.push_back(meta);

	const auto messages = MidiBridge::toChannelMessages(report);

	// Only the two channel voice events should be included.
	CHECK_EQ(messages.size(), 2u);
	for (const auto & m : messages) {
		CHECK_EQ(m.type, MidiMessageType::ChannelVoice);
	}
}

static void testToMessages() {
	beginSuite("toMessages");

	MidiAnalysisReport report;
	report.valid = true;

	report.events.push_back(makeChannelVoiceEvent(0x90, 60, 100, "note_on", 0, 0.0, 0));

	MidiEventRecord sysex;
	sysex.kind = "sysex";
	sysex.status = -1;
	sysex.data1 = -1;
	sysex.data2 = -1;
	sysex.channel = -1;
	sysex.bytes = { 0xF0, 0xF7 };
	report.events.push_back(sysex);

	MidiEventRecord meta;
	meta.kind = "tempo";
	meta.status = -1;
	meta.data1 = -1;
	meta.data2 = -1;
	meta.channel = -1;
	report.events.push_back(meta);

	const auto messages = MidiBridge::toMessages(report);

	// Note On (returns true) and SysEx (returns true) should be included;
	// meta (returns false) should not.
	CHECK_EQ(messages.size(), 2u);
}

// ---------------------------------------------------------------------------
// MidiBridge::filterByChannel
// ---------------------------------------------------------------------------

static void testFilterByChannel() {
	beginSuite("filterByChannel");

	std::vector<MidiChannelMessage> messages;
	{
		MidiChannelMessage m;
		m.type = MidiMessageType::ChannelVoice;
		m.channel = 0;
		m.kind = "note_on";
		messages.push_back(m);
	}
	{
		MidiChannelMessage m;
		m.type = MidiMessageType::ChannelVoice;
		m.channel = 1;
		m.kind = "note_on";
		messages.push_back(m);
	}
	{
		MidiChannelMessage m;
		m.type = MidiMessageType::ChannelVoice;
		m.channel = 0;
		m.kind = "control_change";
		messages.push_back(m);
	}

	const auto ch0 = MidiBridge::filterByChannel(messages, 0);
	CHECK_EQ(ch0.size(), 2u);

	const auto ch1 = MidiBridge::filterByChannel(messages, 1);
	CHECK_EQ(ch1.size(), 1u);

	const auto ch5 = MidiBridge::filterByChannel(messages, 5);
	CHECK(ch5.empty());
}

static void testFilterByChannelExcludesNonChannelVoice() {
	beginSuite("filterByChannel: ignores non-ChannelVoice types");

	std::vector<MidiChannelMessage> messages;
	{
		MidiChannelMessage m;
		m.type = MidiMessageType::SysEx;
		m.channel = 0;
		messages.push_back(m);
	}
	{
		MidiChannelMessage m;
		m.type = MidiMessageType::ChannelVoice;
		m.channel = 0;
		messages.push_back(m);
	}

	const auto ch0 = MidiBridge::filterByChannel(messages, 0);
	CHECK_EQ(ch0.size(), 1u);
	CHECK_EQ(ch0[0].type, MidiMessageType::ChannelVoice);
}

// ---------------------------------------------------------------------------
// MidiBridge::describe
// ---------------------------------------------------------------------------

static void testDescribe() {
	beginSuite("describe");

	MidiChannelMessage m;
	m.type = MidiMessageType::ChannelVoice;
	m.kind = "note_on";
	m.channel = 0;
	m.status = 0x90;
	m.data1 = 60;
	m.data2 = 100;
	m.seconds = 1.5;
	m.tick = 480;
	m.bytes = { 0x90, 60, 100 };

	const std::string description = MidiBridge::describe(m);
	CHECK(!description.empty());
	// Should contain the kind and channel
	CHECK(description.find("note_on") != std::string::npos);
}

static void testDescribeSysEx() {
	beginSuite("describe: SysEx");

	MidiChannelMessage m;
	m.type = MidiMessageType::SysEx;
	m.kind = "sysex";
	m.channel = -1;
	m.status = -1;
	m.data1 = -1;
	m.data2 = -1;
	m.seconds = 0.0;
	m.bytes = { 0xF0, 0x41, 0xF7 };

	const std::string description = MidiBridge::describe(m);
	CHECK(!description.empty());
	// Should mention "bytes"
	CHECK(description.find("bytes") != std::string::npos);
}

// ---------------------------------------------------------------------------
// MidiTimelineCursor
// ---------------------------------------------------------------------------

static void testCursorNullSource() {
	beginSuite("MidiTimelineCursor: null source");

	MidiTimelineCursor cursor;
	cursor.reset(nullptr);

	CHECK_EQ(cursor.size(), 0u);
	CHECK_EQ(cursor.position(), 0u);

	// seekSeconds and advanceUntil must be safe with a null source.
	cursor.seekSeconds(1.0);
	CHECK_EQ(cursor.position(), 0u);

	const size_t end = cursor.advanceUntil(5.0);
	CHECK_EQ(end, 0u);
}

static void testCursorRewind() {
	beginSuite("MidiTimelineCursor: rewind");

	std::vector<MidiChannelMessage> messages(5);
	for (int i = 0; i < 5; ++i) {
		messages[i].seconds = static_cast<double>(i);
	}

	MidiTimelineCursor cursor;
	cursor.reset(&messages);
	cursor.advanceUntil(3.0);
	CHECK(cursor.position() > 0u);

	cursor.rewind();
	CHECK_EQ(cursor.position(), 0u);
}

static void testCursorAdvanceUntil() {
	beginSuite("MidiTimelineCursor: advanceUntil");

	std::vector<MidiChannelMessage> messages(6);
	for (int i = 0; i < 6; ++i) {
		messages[i].seconds = static_cast<double>(i) * 0.5; // 0.0, 0.5, 1.0, 1.5, 2.0, 2.5
	}

	MidiTimelineCursor cursor;
	cursor.reset(&messages);

	// Advance to t=1.0 (inclusive) → messages at 0.0, 0.5, 1.0 dispatched = position 3
	const size_t pos = cursor.advanceUntil(1.0);
	CHECK_EQ(pos, 3u);
	CHECK_EQ(cursor.position(), 3u);

	// Advance further to t=2.0 → messages at 1.5, 2.0 dispatched = position 5
	cursor.advanceUntil(2.0);
	CHECK_EQ(cursor.position(), 5u);

	// Advance past end → position stays at end of messages
	cursor.advanceUntil(100.0);
	CHECK_EQ(cursor.position(), messages.size());
}

static void testCursorSeekSeconds() {
	beginSuite("MidiTimelineCursor: seekSeconds");

	std::vector<MidiChannelMessage> messages(5);
	for (int i = 0; i < 5; ++i) {
		messages[i].seconds = static_cast<double>(i);
	}

	MidiTimelineCursor cursor;
	cursor.reset(&messages);

	// Seek to t=2.0 → position should be the first message with seconds >= 2.0 (index 2)
	cursor.seekSeconds(2.0);
	CHECK_EQ(cursor.position(), 2u);

	// Seek to t=0.0 → back to start
	cursor.seekSeconds(0.0);
	CHECK_EQ(cursor.position(), 0u);

	// Seek beyond end → position at end
	cursor.seekSeconds(100.0);
	CHECK_EQ(cursor.position(), messages.size());
}

static void testCursorSize() {
	beginSuite("MidiTimelineCursor: size");

	std::vector<MidiChannelMessage> messages(4);
	MidiTimelineCursor cursor;
	cursor.reset(&messages);
	CHECK_EQ(cursor.size(), 4u);

	cursor.reset(nullptr);
	CHECK_EQ(cursor.size(), 0u);
}

// ---------------------------------------------------------------------------
// MidiBridge::describe — more message types
// ---------------------------------------------------------------------------

static void testDescribeControlChange() {
	beginSuite("describe: Control Change");

	MidiChannelMessage m;
	m.type = MidiMessageType::ChannelVoice;
	m.kind = "control_change";
	m.channel = 2;
	m.status = 0xB2;
	m.data1 = 7;
	m.data2 = 100;
	m.seconds = 0.5;

	const std::string description = MidiBridge::describe(m);
	CHECK(!description.empty());
	CHECK(description.find("control_change") != std::string::npos);
}

static void testDescribePitchBend() {
	beginSuite("describe: Pitch Bend");

	MidiChannelMessage m;
	m.type = MidiMessageType::ChannelVoice;
	m.kind = "pitch_bend";
	m.channel = 0;
	m.status = 0xE0;
	m.data1 = 0;
	m.data2 = 64;

	const std::string description = MidiBridge::describe(m);
	CHECK(!description.empty());
	CHECK(description.find("pitch_bend") != std::string::npos);
}

static void testDescribeUnknownType() {
	beginSuite("describe: Unknown type");

	MidiChannelMessage m;
	m.type = MidiMessageType::Unknown;
	m.kind = "unknown";
	m.channel = -1;
	m.status = -1;

	const std::string description = MidiBridge::describe(m);
	CHECK(!description.empty());
}

// ---------------------------------------------------------------------------
// MidiBridge::toChannelMessages — empty report
// ---------------------------------------------------------------------------

static void testToChannelMessagesEmptyReport() {
	beginSuite("toChannelMessages: empty report");

	MidiAnalysisReport report;
	report.valid = true;
	// No events.

	const auto messages = MidiBridge::toChannelMessages(report);
	CHECK(messages.empty());
}

static void testToMessagesEmptyReport() {
	beginSuite("toMessages: empty report");

	MidiAnalysisReport report;
	report.valid = true;

	const auto messages = MidiBridge::toMessages(report);
	CHECK(messages.empty());
}

// ---------------------------------------------------------------------------
// MidiBridge::filterByChannel — empty input
// ---------------------------------------------------------------------------

static void testFilterByChannelEmptyInput() {
	beginSuite("filterByChannel: empty input");

	std::vector<MidiChannelMessage> messages;
	const auto result = MidiBridge::filterByChannel(messages, 0);
	CHECK(result.empty());
}

// ---------------------------------------------------------------------------
// MidiTimelineCursor: advanceUntil past end then rewind
// ---------------------------------------------------------------------------

static void testCursorAdvancePastEndThenRewind() {
	beginSuite("MidiTimelineCursor: advance past end then rewind");

	std::vector<MidiChannelMessage> messages(3);
	for (int i = 0; i < 3; ++i) {
		messages[i].seconds = static_cast<double>(i);
	}

	MidiTimelineCursor cursor;
	cursor.reset(&messages);

	// Advance past all messages.
	cursor.advanceUntil(100.0);
	CHECK_EQ(cursor.position(), messages.size());

	// Rewind and advance again.
	cursor.rewind();
	CHECK_EQ(cursor.position(), 0u);

	cursor.advanceUntil(1.0);
	CHECK_EQ(cursor.position(), 2u); // messages at 0.0 and 1.0
}

// ---------------------------------------------------------------------------
// MidiTimelineCursor: seekSeconds to negative time
// ---------------------------------------------------------------------------

static void testCursorSeekNegativeTime() {
	beginSuite("MidiTimelineCursor: seekSeconds negative time");

	std::vector<MidiChannelMessage> messages(3);
	for (int i = 0; i < 3; ++i) {
		messages[i].seconds = static_cast<double>(i);
	}

	MidiTimelineCursor cursor;
	cursor.reset(&messages);

	// Advance first.
	cursor.advanceUntil(2.0);
	CHECK(cursor.position() > 0u);

	// Seek to negative → should go to beginning.
	cursor.seekSeconds(-1.0);
	CHECK_EQ(cursor.position(), 0u);
}

// ---------------------------------------------------------------------------
// MidiTimelineCursor: empty source
// ---------------------------------------------------------------------------

static void testCursorEmptySource() {
	beginSuite("MidiTimelineCursor: empty source");

	std::vector<MidiChannelMessage> messages;
	MidiTimelineCursor cursor;
	cursor.reset(&messages);

	CHECK_EQ(cursor.size(), 0u);
	CHECK_EQ(cursor.position(), 0u);

	cursor.advanceUntil(1.0);
	CHECK_EQ(cursor.position(), 0u);

	cursor.seekSeconds(0.0);
	CHECK_EQ(cursor.position(), 0u);
}

// ---------------------------------------------------------------------------
// MidiBridge struct defaults
// ---------------------------------------------------------------------------

static void testMidiChannelMessageDefaults() {
	beginSuite("MidiChannelMessage: default values");

	MidiChannelMessage m;
	CHECK_EQ(m.trackIndex, 0u);
	CHECK_EQ(m.tick, 0u);
	CHECK_EQ(m.seconds, 0.0);
	CHECK(m.kind.empty());
	CHECK_EQ(m.type, MidiMessageType::Unknown);
	CHECK_EQ(m.channel, -1);
	CHECK_EQ(m.status, -1);
	CHECK_EQ(m.data1, -1);
	CHECK_EQ(m.data2, -1);
	CHECK(m.bytes.empty());
}

static void testMidiSyncSettingsDefaults() {
	beginSuite("MidiSyncSettings: default values");

	MidiSyncSettings s;
	CHECK_EQ(s.mode, MidiSyncMode::None);
	CHECK_EQ(s.timecodeFps, 30.0);
	CHECK(s.sendTransportMessages);
	CHECK(s.sendSongPositionOnSeek);
}

static void testMidiMessageTypeEnum() {
	beginSuite("MidiMessageType enum values");

	CHECK(static_cast<int>(MidiMessageType::ChannelVoice) == 0);
	CHECK(static_cast<int>(MidiMessageType::SysEx) != static_cast<int>(MidiMessageType::ChannelVoice));
	CHECK(static_cast<int>(MidiMessageType::SystemCommon) != static_cast<int>(MidiMessageType::SysEx));
	CHECK(static_cast<int>(MidiMessageType::SystemRealTime) != static_cast<int>(MidiMessageType::SystemCommon));
	CHECK(static_cast<int>(MidiMessageType::Meta) != static_cast<int>(MidiMessageType::SystemRealTime));
	CHECK(static_cast<int>(MidiMessageType::Unknown) != static_cast<int>(MidiMessageType::Meta));
}

static void testMidiSyncModeEnum() {
	beginSuite("MidiSyncMode enum values");

	CHECK(static_cast<int>(MidiSyncMode::None) == 0);
	CHECK(static_cast<int>(MidiSyncMode::MidiClock) != static_cast<int>(MidiSyncMode::None));
	CHECK(static_cast<int>(MidiSyncMode::MidiTimecodeQuarterFrame) != static_cast<int>(MidiSyncMode::MidiClock));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
	testToMessageNoteOn();
	testToMessageNoteOff();
	testToMessageNoteOffAsZeroVelocity();
	testToMessageControlChange();
	testToMessageProgramChange();
	testToMessageChannelPressure();
	testToMessagePitchBend();
	testToMessageAftertouch();
	testToMessageSysEx();
	testToMessageMeta();
	testToMessageSystemRealTime();
	testToMessageBytesPreserved();
	testToChannelMessageAcceptsChannelVoice();
	testToChannelMessageRejectsSysEx();
	testToChannelMessageRejectsMeta();
	testToChannelMessages();
	testToMessages();
	testFilterByChannel();
	testFilterByChannelExcludesNonChannelVoice();
	testDescribe();
	testDescribeSysEx();
	testCursorNullSource();
	testCursorRewind();
	testCursorAdvanceUntil();
	testCursorSeekSeconds();
	testCursorSize();
	testDescribeControlChange();
	testDescribePitchBend();
	testDescribeUnknownType();
	testToChannelMessagesEmptyReport();
	testToMessagesEmptyReport();
	testFilterByChannelEmptyInput();
	testCursorAdvancePastEndThenRewind();
	testCursorSeekNegativeTime();
	testCursorEmptySource();
	testMidiChannelMessageDefaults();
	testMidiSyncSettingsDefaults();
	testMidiMessageTypeEnum();
	testMidiSyncModeEnum();

	std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
	return g_failed == 0 ? 0 : 1;
}
