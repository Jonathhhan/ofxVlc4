#include "ofxVlc4MidiPlayback.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr unsigned char kMidiClock = 0xF8;
constexpr unsigned char kMidiStart = 0xFA;
constexpr unsigned char kMidiContinue = 0xFB;
constexpr unsigned char kMidiStop = 0xFC;
constexpr unsigned char kMidiSongPositionPointer = 0xF2;
}

bool MidiPlaybackSession::load(const std::string & newPath, const MidiAnalysisReport & report, const std::vector<MidiChannelMessage> & newMessages) {
	clear();
	if (!report.valid) {
		return false;
	}

	path = newPath;
	durationSeconds = std::max(0.0, report.durationSeconds);
	messages = newMessages;
	tempoChanges = report.tempoChanges;
	usesSmpteTiming = report.usesSmpteTiming;
	ticksPerQuarterNote = report.ticksPerQuarterNote;
	cursor.reset(&messages);
	syncSettings.timecodeFps = report.usesSmpteTiming && report.smpteFramesPerSecond > 0
		? static_cast<double>(report.smpteFramesPerSecond)
		: syncSettings.timecodeFps;
	loaded = true;
	return true;
}

void MidiPlaybackSession::clear() {
	stop();
	path.clear();
	durationSeconds = 0.0;
	playheadSeconds = 0.0;
	playbackStartWallSeconds = 0.0;
	playbackStartPositionSeconds = 0.0;
	tempoMultiplier = 1.0;
	loaded = false;
	playing = false;
	finished = false;
	messages.clear();
	tempoChanges.clear();
	usesSmpteTiming = false;
	ticksPerQuarterNote = 0;
	lastDispatchBegin = 0;
	lastDispatchEnd = 0;
	nextSyncSeconds = 0.0;
	nextQuarterFramePiece = 0;
	cursor.reset(nullptr);
}

void MidiPlaybackSession::update(double nowSeconds) {
	lastDispatchBegin = cursor.position();
	lastDispatchEnd = cursor.position();
	if (!loaded || !playing) {
		return;
	}

	const double previousPlayheadSeconds = playheadSeconds;
	playheadSeconds = std::max(0.0, playbackStartPositionSeconds + (nowSeconds - playbackStartWallSeconds) * tempoMultiplier);
	if (durationSeconds > 0.0 && playheadSeconds >= durationSeconds) {
		playheadSeconds = durationSeconds;
		lastDispatchBegin = cursor.position();
		lastDispatchEnd = cursor.advanceUntil(playheadSeconds);
		dispatchRange(lastDispatchBegin, lastDispatchEnd);
		emitSyncBetween(previousPlayheadSeconds, playheadSeconds);
		playing = false;
		finished = true;
		sendTransportStop();
		allNotesOff();
		return;
	}

	lastDispatchBegin = cursor.position();
	lastDispatchEnd = cursor.advanceUntil(playheadSeconds);
	dispatchRange(lastDispatchBegin, lastDispatchEnd);
	emitSyncBetween(previousPlayheadSeconds, playheadSeconds);
}

void MidiPlaybackSession::play(double nowSeconds) {
	if (!loaded) {
		return;
	}

	if (finished && playheadSeconds >= durationSeconds) {
		seek(0.0);
		finished = false;
	}

	playbackStartWallSeconds = nowSeconds;
	playbackStartPositionSeconds = playheadSeconds;
	playing = true;
	nextSyncSeconds = playheadSeconds;
	nextQuarterFramePiece = 0;
	sendTransportStart(playheadSeconds > 0.0);
}

void MidiPlaybackSession::pause(double nowSeconds) {
	if (!loaded || !playing) {
		return;
	}

	playheadSeconds = std::max(0.0, playbackStartPositionSeconds + (nowSeconds - playbackStartWallSeconds) * tempoMultiplier);
	playbackStartPositionSeconds = playheadSeconds;
	playing = false;
	sendTransportStop();
	allNotesOff();
}

void MidiPlaybackSession::stop() {
	if (!loaded && messages.empty()) {
		return;
	}

	playing = false;
	finished = false;
	playheadSeconds = 0.0;
	playbackStartWallSeconds = 0.0;
	playbackStartPositionSeconds = 0.0;
	lastDispatchBegin = 0;
	lastDispatchEnd = 0;
	nextSyncSeconds = 0.0;
	nextQuarterFramePiece = 0;
	cursor.rewind();
	sendTransportStop();
	allNotesOff();
}

void MidiPlaybackSession::seek(double seconds, double nowSeconds) {
	if (!loaded) {
		return;
	}

	playheadSeconds = std::clamp(seconds, 0.0, durationSeconds);
	playbackStartPositionSeconds = playheadSeconds;
	if (playing && nowSeconds > 0.0) {
		playbackStartWallSeconds = nowSeconds;
	}
	cursor.seekSeconds(playheadSeconds);
	lastDispatchBegin = cursor.position();
	lastDispatchEnd = cursor.position();
	finished = loaded && durationSeconds > 0.0 && playheadSeconds >= durationSeconds;
	nextSyncSeconds = playheadSeconds;
	nextQuarterFramePiece = 0;
	sendSongPositionPointer();
	allNotesOff();
}

void MidiPlaybackSession::setTempoMultiplier(double multiplier, double nowSeconds) {
	const double clamped = std::clamp(multiplier, 0.1, 4.0);
	if (tempoMultiplier == clamped) {
		return;
	}

	if (loaded && playing) {
		playheadSeconds = std::max(0.0, playbackStartPositionSeconds + (nowSeconds - playbackStartWallSeconds) * tempoMultiplier);
		playbackStartPositionSeconds = std::clamp(playheadSeconds, 0.0, durationSeconds);
		playbackStartWallSeconds = nowSeconds;
	}
	tempoMultiplier = clamped;
}

void MidiPlaybackSession::setMessageCallback(MidiMessageCallback callback) {
	messageCallback = std::move(callback);
}

void MidiPlaybackSession::clearMessageCallback() {
	messageCallback = {};
}

bool MidiPlaybackSession::hasMessageCallback() const {
	return static_cast<bool>(messageCallback);
}

void MidiPlaybackSession::setSyncSettings(const MidiSyncSettings & settings) {
	syncSettings = settings;
}

MidiSyncSettings MidiPlaybackSession::getSyncSettings() const {
	return syncSettings;
}

bool MidiPlaybackSession::isLoaded() const {
	return loaded;
}

bool MidiPlaybackSession::isPlaying() const {
	return loaded && playing;
}

bool MidiPlaybackSession::isPaused() const {
	return loaded && !playing && playheadSeconds > 0.0 && !finished;
}

bool MidiPlaybackSession::isStopped() const {
	return !loaded || (!playing && playheadSeconds <= 0.0);
}

bool MidiPlaybackSession::isFinished() const {
	return loaded && finished;
}

double MidiPlaybackSession::getDurationSeconds() const {
	return durationSeconds;
}

double MidiPlaybackSession::getPositionSeconds() const {
	return playheadSeconds;
}

double MidiPlaybackSession::getTempoMultiplier() const {
	return tempoMultiplier;
}

const std::string & MidiPlaybackSession::getPath() const {
	return path;
}

const std::vector<MidiChannelMessage> & MidiPlaybackSession::getMessages() const {
	return messages;
}

size_t MidiPlaybackSession::getLastDispatchBegin() const {
	return lastDispatchBegin;
}

size_t MidiPlaybackSession::getLastDispatchEnd() const {
	return lastDispatchEnd;
}

size_t MidiPlaybackSession::getDispatchedCount() const {
	return cursor.position();
}

void MidiPlaybackSession::dispatchRange(size_t beginIndex, size_t endIndex) {
	if (!messageCallback || beginIndex >= endIndex || beginIndex >= messages.size()) {
		return;
	}

	const size_t clampedEnd = std::min(endIndex, messages.size());
	for (size_t index = beginIndex; index < clampedEnd; ++index) {
		dispatchMessage(messages[index]);
	}
}

void MidiPlaybackSession::dispatchMessage(const MidiChannelMessage & message) const {
	if (!messageCallback || message.bytes.empty()) {
		return;
	}
	messageCallback(message);
}

void MidiPlaybackSession::allNotesOff() {
	if (!messageCallback) {
		return;
	}

	for (int channel = 0; channel < 16; ++channel) {
		MidiChannelMessage cc120;
		cc120.type = MidiMessageType::ChannelVoice;
		cc120.kind = "all_sound_off";
		cc120.channel = channel;
		cc120.status = 0xB0 | channel;
		cc120.data1 = 120;
		cc120.data2 = 0;
		cc120.bytes = {
			static_cast<unsigned char>(0xB0 | channel),
			static_cast<unsigned char>(120),
			static_cast<unsigned char>(0)
		};
		dispatchMessage(cc120);

		MidiChannelMessage cc123 = cc120;
		cc123.kind = "all_notes_off";
		cc123.data1 = 123;
		cc123.bytes[1] = 123;
		dispatchMessage(cc123);
	}
}

void MidiPlaybackSession::sendTransportStart(bool resume) {
	if (!messageCallback || !syncSettings.sendTransportMessages) {
		return;
	}

	MidiChannelMessage message;
	message.type = MidiMessageType::SystemRealTime;
	message.kind = resume ? "continue" : "start";
	message.bytes = { static_cast<unsigned char>(resume ? kMidiContinue : kMidiStart) };
	dispatchMessage(message);
}

void MidiPlaybackSession::sendTransportStop() {
	if (!messageCallback || !syncSettings.sendTransportMessages) {
		return;
	}

	MidiChannelMessage message;
	message.type = MidiMessageType::SystemRealTime;
	message.kind = "stop";
	message.bytes = { static_cast<unsigned char>(kMidiStop) };
	dispatchMessage(message);
}

void MidiPlaybackSession::sendSongPositionPointer() {
	if (!messageCallback || !syncSettings.sendSongPositionOnSeek) {
		return;
	}

	const int songPosition = songPositionAtSeconds(playheadSeconds);
	MidiChannelMessage message;
	message.type = MidiMessageType::SystemCommon;
	message.kind = "song_position";
	message.bytes = {
		static_cast<unsigned char>(kMidiSongPositionPointer),
		static_cast<unsigned char>(songPosition & 0x7F),
		static_cast<unsigned char>((songPosition >> 7) & 0x7F)
	};
	dispatchMessage(message);
}

void MidiPlaybackSession::emitSyncBetween(double fromSeconds, double toSeconds) {
	if (!messageCallback || syncSettings.mode == MidiSyncMode::None || toSeconds < fromSeconds) {
		return;
	}

	if (syncSettings.mode == MidiSyncMode::MidiClock) {
		double cursorSeconds = std::max(nextSyncSeconds, fromSeconds);
		while (cursorSeconds <= toSeconds) {
			MidiChannelMessage message;
			message.type = MidiMessageType::SystemRealTime;
			message.kind = "clock";
			message.seconds = cursorSeconds;
			message.bytes = { static_cast<unsigned char>(kMidiClock) };
			dispatchMessage(message);

			const double bpm = std::max(1.0, tempoAtSeconds(cursorSeconds));
			const double intervalSeconds = 60.0 / (bpm * 24.0);
			cursorSeconds += intervalSeconds;
		}
		nextSyncSeconds = cursorSeconds;
		return;
	}

	if (syncSettings.mode == MidiSyncMode::MidiTimecodeQuarterFrame) {
		const double fps = std::clamp(syncSettings.timecodeFps, 1.0, 120.0);
		const double intervalSeconds = 1.0 / (fps * 4.0);
		double cursorSeconds = std::max(nextSyncSeconds, fromSeconds);
		while (cursorSeconds <= toSeconds) {
			MidiChannelMessage message;
			message.type = MidiMessageType::SystemCommon;
			message.kind = "mtc_quarter_frame";
			message.seconds = cursorSeconds;
			message.bytes = makeQuarterFrameMessage(cursorSeconds, fps, nextQuarterFramePiece);
			dispatchMessage(message);
			cursorSeconds += intervalSeconds;
			nextQuarterFramePiece = (nextQuarterFramePiece + 1) % 8;
		}
		nextSyncSeconds = cursorSeconds;
	}
}

double MidiPlaybackSession::tempoAtSeconds(double seconds) const {
	double bpm = 120.0;
	for (const MidiTempoChange & change : tempoChanges) {
		if (change.seconds <= seconds) {
			bpm = std::max(1.0, change.bpm);
		} else {
			break;
		}
	}
	return bpm;
}

double MidiPlaybackSession::tickAtSeconds(double seconds) const {
	const double clampedSeconds = std::max(0.0, seconds);
	if (usesSmpteTiming || ticksPerQuarterNote <= 0) {
		return 0.0;
	}

	MidiTempoChange activeTempo;
	activeTempo.tick = 0;
	activeTempo.seconds = 0.0;
	activeTempo.microsecondsPerQuarter = 500000;
	activeTempo.bpm = 120.0;

	for (const MidiTempoChange & change : tempoChanges) {
		if (change.seconds <= clampedSeconds) {
			activeTempo = change;
		} else {
			break;
		}
	}

	const double ticksPerSecond =
		static_cast<double>(ticksPerQuarterNote) * 1000000.0 /
		static_cast<double>(std::max(1, activeTempo.microsecondsPerQuarter));
	const double deltaSeconds = std::max(0.0, clampedSeconds - activeTempo.seconds);
	return static_cast<double>(activeTempo.tick) + (deltaSeconds * ticksPerSecond);
}

int MidiPlaybackSession::songPositionAtSeconds(double seconds) const {
	if (!usesSmpteTiming && ticksPerQuarterNote > 0) {
		const double ticksPerSongPositionUnit = static_cast<double>(ticksPerQuarterNote) / 4.0;
		if (ticksPerSongPositionUnit > 0.0) {
			const double songPosition = tickAtSeconds(seconds) / ticksPerSongPositionUnit;
			return static_cast<int>(std::clamp<long long>(
				static_cast<long long>(std::llround(std::max(0.0, songPosition))),
				0,
				0x3FFF));
		}
	}

	return static_cast<int>(std::clamp<long long>(
		static_cast<long long>(std::llround(std::max(0.0, seconds) * 2.0)),
		0,
		0x3FFF));
}

std::vector<unsigned char> MidiPlaybackSession::makeQuarterFrameMessage(double seconds, double fps, int piece) {
	const int totalFrames = static_cast<int>(std::floor(std::max(0.0, seconds) * fps));
	const int frames = totalFrames % static_cast<int>(std::round(fps));
	const int totalSeconds = totalFrames / static_cast<int>(std::round(fps));
	const int secondsPart = totalSeconds % 60;
	const int totalMinutes = totalSeconds / 60;
	const int minutes = totalMinutes % 60;
	const int hours = (totalMinutes / 60) % 24;

	int value = 0;
	switch (piece & 0x07) {
	case 0: value = frames & 0x0F; break;
	case 1: value = (frames >> 4) & 0x01; break;
	case 2: value = secondsPart & 0x0F; break;
	case 3: value = (secondsPart >> 4) & 0x03; break;
	case 4: value = minutes & 0x0F; break;
	case 5: value = (minutes >> 4) & 0x03; break;
	case 6: value = hours & 0x0F; break;
	default: {
		int rateCode = 3;
		if (fps <= 24.5) {
			rateCode = 0;
		} else if (fps <= 25.5) {
			rateCode = 1;
		} else if (fps < 29.8) {
			rateCode = 2;
		}
		value = ((hours >> 4) & 0x01) | (rateCode << 1);
		break;
	}
	}

	return {
		static_cast<unsigned char>(0xF1),
		static_cast<unsigned char>(((piece & 0x07) << 4) | (value & 0x0F))
	};
}
