#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "audio/ofxVlc4Audio.h"
#include "media/MediaLibrary.h"
#include "ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "video/ofxVlc4Video.h"
#include "support/ofxVlc4Utils.h"
#include "core/VlcCoreSession.h"
#include "core/VlcEventRouter.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>

using ofxVlc4Utils::clearAllocatedFbo;
using ofxVlc4Utils::fallbackIndexedLabel;
using ofxVlc4Utils::formatProgramName;
using ofxVlc4Utils::isUri;
using ofxVlc4Utils::isStoppedOrIdleState;
using ofxVlc4Utils::isTransientPlaybackState;
using ofxVlc4Utils::mediaLabelForPath;
using ofxVlc4Utils::normalizeOptionalPath;
using ofxVlc4Utils::sanitizeFileStem;
using ofxVlc4Utils::trimWhitespace;

namespace {

// Use the same capacity constant as VlcCoreSession to avoid duplication.
constexpr size_t kLibVlcLogCapacity = VlcCoreSession::kLogCapacity;

std::string defaultLibVlcLogFilePath() {
	return normalizeOptionalPath(ofToDataPath("logs/ofxVlc4-libvlc.log", true));
}

std::string formatLibVlcLogMessage(const char * fmt, va_list args) {
	if (!fmt || *fmt == '\0') {
		return "";
	}

	va_list argsCopy;
	va_copy(argsCopy, args);
	const int requiredSize = std::vsnprintf(nullptr, 0, fmt, argsCopy);
	va_end(argsCopy);
	if (requiredSize <= 0) {
		return "";
	}

	std::string message(static_cast<size_t>(requiredSize), '\0');
	va_copy(argsCopy, args);
	std::vsnprintf(message.data(), static_cast<size_t>(requiredSize) + 1, fmt, argsCopy);
	va_end(argsCopy);
	return trimWhitespace(message);
}

std::string mapLibVlcLogToFriendlyError(const std::string & message) {
	const std::string normalized = ofToLower(trimWhitespace(message));
	if (normalized.empty()) {
		return "";
	}

	const bool missingSoundFont =
		normalized.find("sound font file") != std::string::npos &&
		normalized.find("midi synthesis") != std::string::npos;
	const bool midiDecodeFailure =
		normalized.find("could not decode the format \"midi\"") != std::string::npos ||
		(normalized.find("midi audio") != std::string::npos &&
		 normalized.find("could not decode") != std::string::npos);

	if (missingSoundFont || midiDecodeFailure) {
		return "MIDI playback requires a configured sound font (.sf2/.sf3). "
			   "Set one in VLC Preferences > Input / Codecs > Audio codecs > FluidSynth.";
	}

	return "";
}

ofxVlc4::DialogQuestionSeverity toDialogQuestionSeverity(libvlc_dialog_question_type type) {
	switch (type) {
	case LIBVLC_DIALOG_QUESTION_WARNING:
		return ofxVlc4::DialogQuestionSeverity::Warning;
	case LIBVLC_DIALOG_QUESTION_CRITICAL:
		return ofxVlc4::DialogQuestionSeverity::Critical;
	case LIBVLC_DIALOG_QUESTION_NORMAL:
	default:
		return ofxVlc4::DialogQuestionSeverity::Normal;
	}
}

ofxVlc4::WatchTimeInfo buildWatchTimeInfoSnapshot(
	const libvlc_media_player_time_point_t & point,
	bool enabled,
	bool registered,
	bool available,
	bool paused,
	bool seeking,
	int64_t minPeriodUs,
	int64_t pausedSystemDateUs,
	ofxVlc4::WatchTimeEventType eventType,
	uint64_t sequence,
	bool interpolate) {
	ofxVlc4::WatchTimeInfo info;
	info.eventType = eventType;
	info.sequence = sequence;
	info.enabled = enabled;
	info.registered = registered;
	info.available = available;
	info.paused = paused;
	info.seeking = seeking;
	info.minPeriodUs = minPeriodUs;

	if (!info.available) {
		return info;
	}

	info.position = point.position;
	info.rate = point.rate;
	info.timeUs = point.ts_us;
	info.lengthUs = point.length_us;
	info.systemDateUs = point.system_date_us;
	info.interpolatedTimeUs = point.ts_us;
	info.interpolatedPosition = point.position;

	if (!interpolate) {
		return info;
	}

	const bool pointLooksValid =
		std::isfinite(point.position) &&
		std::isfinite(point.rate) &&
		point.ts_us >= 0 &&
		point.length_us >= 0 &&
		(point.system_date_us > 0 || (paused && pausedSystemDateUs > 0));
	if (!pointLooksValid) {
		return info;
	}

	const int64_t systemNowUs =
		(info.paused && pausedSystemDateUs > 0) ? pausedSystemDateUs : libvlc_clock();
	if (systemNowUs <= 0 || systemNowUs == INT64_MAX) {
		return info;
	}
	int64_t interpolatedTimeUs = point.ts_us;
	double interpolatedPosition = point.position;
	if (libvlc_media_player_time_point_interpolate(&point, systemNowUs, &interpolatedTimeUs, &interpolatedPosition) == 0) {
		if (std::isfinite(interpolatedPosition) && interpolatedTimeUs >= 0) {
			info.interpolatedTimeUs = interpolatedTimeUs;
			info.interpolatedPosition = interpolatedPosition;
		}
	}

	return info;
}

std::string formatPlaybackTimecodeValue(int64_t timeUs, double fps) {
	if (timeUs < 0 || !std::isfinite(fps) || fps <= 0.0) {
		return "--:--:--:--";
	}

	const int64_t totalSeconds = timeUs / 1000000;
	const int64_t remainderUs = timeUs % 1000000;
	const int hours = static_cast<int>(totalSeconds / 3600);
	const int minutes = static_cast<int>((totalSeconds / 60) % 60);
	const int seconds = static_cast<int>(totalSeconds % 60);
	const int maxFrames = std::max(1, static_cast<int>(std::ceil(fps)));
	int frames = static_cast<int>(std::floor((static_cast<double>(remainderUs) * fps) / 1000000.0));
	frames = std::clamp(frames, 0, maxFrames - 1);

	std::ostringstream stream;
	stream << std::setfill('0')
		   << std::setw(2) << hours << ":"
		   << std::setw(2) << minutes << ":"
		   << std::setw(2) << seconds << ":"
		   << std::setw(2) << frames;
	return stream.str();
}

bool hasLibVlcLogFilePath(const std::string & path) {
	return !normalizeOptionalPath(path).empty();
}

ofxVlc4::LibVlcLogEntry toPublicLogEntry(const VlcCoreLogEntry & entry) {
	ofxVlc4::LibVlcLogEntry publicEntry;
	publicEntry.level = entry.level;
	publicEntry.module = entry.module;
	publicEntry.file = entry.file;
	publicEntry.line = entry.line;
	publicEntry.objectName = entry.objectName;
	publicEntry.objectHeader = entry.objectHeader;
	publicEntry.objectId = entry.objectId;
	publicEntry.message = entry.message;
	return publicEntry;
}

}

void ofxVlc4::MediaComponent::dismissAllDialogs() {
	std::vector<std::uintptr_t> tokens;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.dialogMutex);
		tokens = getActiveDialogTokensLocked();
	}

	for (std::uintptr_t token : tokens) {
		dismissDialog(token);
	}
}

bool ofxVlc4::MediaComponent::postDialogLogin(
	std::uintptr_t token,
	const std::string & username,
	const std::string & password,
	bool store) {
	libvlc_dialog_id * dialogId = reinterpret_cast<libvlc_dialog_id *>(token);
	if (!dialogId || trimWhitespace(username).empty()) {
		return false;
	}

	if (libvlc_dialog_post_login(dialogId, username.c_str(), password.c_str(), store) != 0) {
		return false;
	}

	removeDialog(token);
	owner.setStatus("Dialog login sent.");
	return true;
}

bool ofxVlc4::MediaComponent::postDialogAction(std::uintptr_t token, int action) {
	libvlc_dialog_id * dialogId = reinterpret_cast<libvlc_dialog_id *>(token);
	if (!dialogId || action < 1 || action > 2) {
		return false;
	}

	if (libvlc_dialog_post_action(dialogId, action) != 0) {
		return false;
	}

	removeDialog(token);
	owner.setStatus("Dialog action sent.");
	return true;
}

bool ofxVlc4::MediaComponent::dismissDialog(std::uintptr_t token) {
	libvlc_dialog_id * dialogId = reinterpret_cast<libvlc_dialog_id *>(token);
	if (!dialogId) {
		return false;
	}

	if (libvlc_dialog_dismiss(dialogId) != 0) {
		return false;
	}

	removeDialog(token);
	return true;
}

void ofxVlc4::MediaComponent::upsertDialog(const DialogInfo & dialog) {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.dialogMutex);
	upsertActiveDialogLocked(dialog);
}

void ofxVlc4::MediaComponent::removeDialog(std::uintptr_t token) {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.dialogMutex);
	removeActiveDialogLocked(token);
}


std::vector<ofxVlc4::DialogInfo> ofxVlc4::MediaComponent::getActiveDialogs() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.dialogMutex);
	return owner.m_impl->diagnosticsRuntime.activeDialogs;
}

ofxVlc4::DialogErrorInfo ofxVlc4::MediaComponent::getLastDialogError() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.dialogMutex);
	return owner.m_impl->diagnosticsRuntime.lastDialogError;
}

void ofxVlc4::MediaComponent::clearLastDialogError() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.dialogMutex);
	clearLastDialogErrorLocked();
}

bool ofxVlc4::MediaComponent::isLibVlcLoggingEnabled() const {
	return owner.m_impl->subsystemRuntime.coreSession->loggingEnabled();
}

void ofxVlc4::MediaComponent::setLibVlcLoggingEnabled(bool enabled) {
	if (isLibVlcLoggingEnabled() == enabled) {
		return;
	}

	setLibVlcLoggingEnabledValue(enabled);
	if (owner.sessionInstance()) {
		applyLibVlcLogging();
	}
	owner.setStatus(std::string("libVLC logging ") + (enabled ? "enabled." : "disabled."));
}

bool ofxVlc4::MediaComponent::isLibVlcLogFileEnabled() const {
	return owner.m_impl->subsystemRuntime.coreSession->logFileEnabled();
}

void ofxVlc4::MediaComponent::setLibVlcLogFileEnabled(bool enabled) {
	std::string resolvedPath = owner.m_impl->subsystemRuntime.coreSession->logFilePath();
	if (enabled && !hasLibVlcLogFilePath(resolvedPath)) {
		resolvedPath = defaultLibVlcLogFilePath();
		setLibVlcLogFilePathValue(resolvedPath);
	}

	const bool canEnable = hasLibVlcLogFilePath(resolvedPath);
	if (enabled && !canEnable) {
		owner.setError("Could not resolve a libVLC log file path.");
		return;
	}

	if (isLibVlcLogFileEnabled() == enabled) {
		return;
	}

	setLibVlcLogFileEnabledValue(enabled);
	if (owner.sessionInstance()) {
		applyLibVlcLogging();
	}
	owner.setStatus(std::string("libVLC file logging ") + (enabled ? "enabled." : "disabled."));
	if (enabled) {
		owner.logNotice("libVLC log file: " + getLibVlcLogFilePath());
	}
}

std::string ofxVlc4::MediaComponent::getLibVlcLogFilePath() const {
	return owner.m_impl->subsystemRuntime.coreSession->logFilePath();
}

void ofxVlc4::MediaComponent::setLibVlcLogFilePath(const std::string & path) {
	const std::string normalizedPath = normalizeOptionalPath(path);
	if (getLibVlcLogFilePath() == normalizedPath) {
		return;
	}

	const bool wasFileLoggingEnabled = isLibVlcLogFileEnabled();
	setLibVlcLogFilePathValue(normalizedPath);
	if (owner.m_impl->subsystemRuntime.coreSession->logFilePath().empty()) {
		setLibVlcLogFileEnabledValue(false);
	}
	if ((wasFileLoggingEnabled || owner.m_impl->subsystemRuntime.coreSession->logFileEnabled()) && owner.sessionInstance()) {
		applyLibVlcLogging();
	}
}

std::vector<ofxVlc4::LibVlcLogEntry> ofxVlc4::MediaComponent::getLibVlcLogEntries() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.libVlcLogMutex);
	std::vector<LibVlcLogEntry> entries;
	entries.reserve(owner.m_impl->subsystemRuntime.coreSession->logEntries().size());
	for (const VlcCoreLogEntry & entry : owner.m_impl->subsystemRuntime.coreSession->logEntries()) {
		entries.push_back(toPublicLogEntry(entry));
	}
	return entries;
}

void ofxVlc4::MediaComponent::clearLibVlcLogEntries() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.libVlcLogMutex);
	owner.m_impl->subsystemRuntime.coreSession->clearLogEntries();
}

void ofxVlc4::MediaComponent::applyLibVlcLogging() {
	if (!owner.sessionInstance()) {
		return;
	}

	libvlc_log_unset(owner.sessionInstance());
	closeLibVlcLogFile();

	const auto applyBufferedLoggingFallback = [this]() {
		if (owner.m_impl->subsystemRuntime.coreSession->loggingEnabled()) {
			libvlc_log_set(owner.sessionInstance(), ofxVlc4::libVlcLogStatic, owner.m_controlBlock.get());
		}
	};

	if (owner.m_impl->subsystemRuntime.coreSession->logFileEnabled()) {
		const std::string normalizedPath = normalizeOptionalPath(owner.m_impl->subsystemRuntime.coreSession->logFilePath());
		if (normalizedPath.empty()) {
			ofxVlc4::logWarning("libVLC file logging enabled without a log path.");
			applyBufferedLoggingFallback();
			return;
		}

		std::error_code ec;
		const std::filesystem::path logPath(normalizedPath);
		const std::filesystem::path parentPath = logPath.parent_path();
		if (!parentPath.empty()) {
			std::filesystem::create_directories(parentPath, ec);
			if (ec) {
				ofxVlc4::logWarning("Failed to create libVLC log directory: " + parentPath.string());
				applyBufferedLoggingFallback();
				return;
			}
		}

		FILE * logFile = nullptr;
#ifdef _MSC_VER
		if (fopen_s(&logFile, normalizedPath.c_str(), "ab") != 0) {
			logFile = nullptr;
		}
#else
		logFile = std::fopen(normalizedPath.c_str(), "ab");
#endif
		if (!logFile) {
			ofxVlc4::logWarning("Failed to open libVLC log file: " + normalizedPath);
			applyBufferedLoggingFallback();
			return;
		}

		owner.m_impl->subsystemRuntime.coreSession->setLogFileHandle(logFile);
		libvlc_log_set_file(owner.sessionInstance(), owner.m_impl->subsystemRuntime.coreSession->logFileHandle());
		return;
	}

	applyBufferedLoggingFallback();
}

void ofxVlc4::MediaComponent::closeLibVlcLogFile() {
	owner.m_impl->subsystemRuntime.coreSession->closeLogFile();
}

void ofxVlc4::MediaComponent::appendLibVlcLog(const LibVlcLogEntry & entry) {
	if (entry.message.empty()) {
		return;
	}

	const std::string friendlyError = mapLibVlcLogToFriendlyError(entry.message);

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.libVlcLogMutex);
	VlcCoreLogEntry coreEntry;
	coreEntry.level = entry.level;
	coreEntry.module = entry.module;
	coreEntry.file = entry.file;
	coreEntry.line = entry.line;
	coreEntry.objectName = entry.objectName;
	coreEntry.objectHeader = entry.objectHeader;
	coreEntry.objectId = entry.objectId;
	coreEntry.message = entry.message;
	owner.m_impl->subsystemRuntime.coreSession->appendLog(coreEntry);

	if (!friendlyError.empty() && owner.m_impl->diagnosticsRuntime.lastErrorMessage != friendlyError) {
		owner.m_impl->diagnosticsRuntime.lastErrorMessage = friendlyError;
		owner.m_impl->diagnosticsRuntime.lastStatusMessage.clear();
		ofxVlc4::logError(friendlyError);
	}
}

void ofxVlc4::libVlcLogStatic(void * data, int level, const libvlc_log_t * ctx, const char * fmt, va_list args) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire) || !cb->owner || !ctx) {
		return;
	}
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) {
		return;
	}

	LibVlcLogEntry entry;
	entry.level = level;
	entry.message = formatLibVlcLogMessage(fmt, args);

	const char * module = nullptr;
	const char * file = nullptr;
	unsigned line = 0;
	libvlc_log_get_context(ctx, &module, &file, &line);
	entry.module = module ? module : "";
	entry.file = file ? file : "";
	entry.line = line;

	const char * objectName = nullptr;
	const char * objectHeader = nullptr;
	uintptr_t objectId = 0;
	libvlc_log_get_object(ctx, &objectName, &objectHeader, &objectId);
	entry.objectName = objectName ? objectName : "";
	entry.objectHeader = objectHeader ? objectHeader : "";
	entry.objectId = objectId;

	scope.get()->m_impl->subsystemRuntime.mediaComponent->appendLibVlcLog(entry);
}


bool ofxVlc4::MediaComponent::isWatchTimeEnabled() const {
	return owner.m_impl->watchTimeRuntime.enabled;
}

void ofxVlc4::MediaComponent::setWatchTimeEnabled(bool enabled) {
	if (owner.m_impl->watchTimeRuntime.enabled == enabled) {
		return;
	}

	owner.m_impl->watchTimeRuntime.enabled = enabled;
	applyWatchTimeObserver();
}

int64_t ofxVlc4::MediaComponent::getWatchTimeMinPeriodUs() const {
	return owner.m_impl->watchTimeRuntime.minPeriodUs;
}

void ofxVlc4::MediaComponent::setWatchTimeMinPeriodUs(int64_t minPeriodUs) {
	const int64_t clampedPeriodUs = std::max<int64_t>(0, minPeriodUs);
	if (owner.m_impl->watchTimeRuntime.minPeriodUs == clampedPeriodUs) {
		return;
	}

	owner.m_impl->watchTimeRuntime.minPeriodUs = clampedPeriodUs;
	if (owner.m_impl->watchTimeRuntime.enabled) {
		applyWatchTimeObserver();
	}
}

void ofxVlc4::MediaComponent::clearWatchTimeState() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.watchTimeMutex);
	owner.m_impl->watchTimeRuntime.pointAvailable = false;
	owner.m_impl->watchTimeRuntime.paused = false;
	owner.m_impl->watchTimeRuntime.seeking = false;
	owner.m_impl->watchTimeRuntime.pauseSystemDateUs = 0;
	owner.m_impl->watchTimeRuntime.updateSequence = 0;
	owner.m_impl->watchTimeRuntime.lastEventType = WatchTimeEventType::Update;
	owner.m_impl->watchTimeRuntime.lastPoint = {};
}

ofxVlc4::WatchTimeInfo ofxVlc4::MediaComponent::getWatchTimeInfo() const {
	return buildWatchTimeInfo();
}

ofxVlc4::WatchTimeInfo ofxVlc4::MediaComponent::buildWatchTimeInfo() const {
	WatchTimeInfo info;
	info.enabled = owner.m_impl->watchTimeRuntime.enabled;
	info.minPeriodUs = owner.m_impl->watchTimeRuntime.minPeriodUs;

	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player || !owner.sessionMedia() || playback().isPlaybackLocallyStopped()) {
		return info;
	}

	libvlc_media_player_time_point_t point {};
	int64_t pausedSystemDateUs = 0;
	uint64_t updateSequence = 0;
	WatchTimeEventType eventType = WatchTimeEventType::Update;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.watchTimeMutex);
		info.registered = owner.m_impl->watchTimeRuntime.registered;
		info.available = owner.m_impl->watchTimeRuntime.pointAvailable;
		info.paused = owner.m_impl->watchTimeRuntime.paused;
		info.seeking = owner.m_impl->watchTimeRuntime.seeking;
		point = owner.m_impl->watchTimeRuntime.lastPoint;
		pausedSystemDateUs = owner.m_impl->watchTimeRuntime.pauseSystemDateUs;
		updateSequence = owner.m_impl->watchTimeRuntime.updateSequence;
		eventType = owner.m_impl->watchTimeRuntime.lastEventType;
	}

	return buildWatchTimeInfoSnapshot(
		point,
		owner.m_impl->watchTimeRuntime.enabled,
		info.registered,
		info.available,
		info.paused,
		info.seeking,
		owner.m_impl->watchTimeRuntime.minPeriodUs,
		pausedSystemDateUs,
		eventType,
		updateSequence,
		true);
}

void ofxVlc4::MediaComponent::applyWatchTimeObserver() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	if (owner.m_impl->watchTimeRuntime.registered) {
		libvlc_media_player_unwatch_time(player);
		owner.m_impl->watchTimeRuntime.registered = false;
	}

	clearWatchTimeState();

	if (!owner.m_impl->watchTimeRuntime.enabled) {
		return;
	}

	owner.m_impl->watchTimeRuntime.registered = libvlc_media_player_watch_time(
		player,
		std::max<int64_t>(0, owner.m_impl->watchTimeRuntime.minPeriodUs),
		ofxVlc4::watchTimeUpdateStatic,
		ofxVlc4::watchTimePausedStatic,
		ofxVlc4::watchTimeSeekStatic,
		owner.m_controlBlock.get()) == 0;
}


void ofxVlc4::dismissAllDialogs() {
	m_impl->subsystemRuntime.mediaComponent->dismissAllDialogs();
}

void ofxVlc4::upsertDialog(const DialogInfo & dialog) {
	m_impl->subsystemRuntime.mediaComponent->upsertDialog(dialog);
}

void ofxVlc4::removeDialog(std::uintptr_t token) {
	m_impl->subsystemRuntime.mediaComponent->removeDialog(token);
}

std::vector<ofxVlc4::DialogInfo> ofxVlc4::getActiveDialogs() const {
	return m_impl->subsystemRuntime.mediaComponent->getActiveDialogs();
}

ofxVlc4::DialogErrorInfo ofxVlc4::getLastDialogError() const {
	return m_impl->subsystemRuntime.mediaComponent->getLastDialogError();
}

void ofxVlc4::clearLastDialogError() {
	m_impl->subsystemRuntime.mediaComponent->clearLastDialogError();
}

bool ofxVlc4::isLibVlcLoggingEnabled() const {
	return m_impl->subsystemRuntime.mediaComponent->isLibVlcLoggingEnabled();
}

void ofxVlc4::setLibVlcLoggingEnabled(bool enabled) {
	m_impl->subsystemRuntime.mediaComponent->setLibVlcLoggingEnabled(enabled);
}

bool ofxVlc4::isLibVlcLogFileEnabled() const {
	return m_impl->subsystemRuntime.mediaComponent->isLibVlcLogFileEnabled();
}

void ofxVlc4::setLibVlcLogFileEnabled(bool enabled) {
	m_impl->subsystemRuntime.mediaComponent->setLibVlcLogFileEnabled(enabled);
}

std::string ofxVlc4::getLibVlcLogFilePath() const {
	return m_impl->subsystemRuntime.mediaComponent->getLibVlcLogFilePath();
}

void ofxVlc4::setLibVlcLogFilePath(const std::string & path) {
	m_impl->subsystemRuntime.mediaComponent->setLibVlcLogFilePath(path);
}

std::vector<ofxVlc4::LibVlcLogEntry> ofxVlc4::getLibVlcLogEntries() const {
	return m_impl->subsystemRuntime.mediaComponent->getLibVlcLogEntries();
}

void ofxVlc4::clearLibVlcLogEntries() {
	m_impl->subsystemRuntime.mediaComponent->clearLibVlcLogEntries();
}

bool ofxVlc4::isWatchTimeEnabled() const {
	return m_impl->subsystemRuntime.mediaComponent->isWatchTimeEnabled();
}

void ofxVlc4::setWatchTimeEnabled(bool enabled) {
	m_impl->subsystemRuntime.mediaComponent->setWatchTimeEnabled(enabled);
}

int64_t ofxVlc4::getWatchTimeMinPeriodUs() const {
	return m_impl->subsystemRuntime.mediaComponent->getWatchTimeMinPeriodUs();
}

void ofxVlc4::setWatchTimeMinPeriodUs(int64_t minPeriodUs) {
	m_impl->subsystemRuntime.mediaComponent->setWatchTimeMinPeriodUs(minPeriodUs);
}

void ofxVlc4::clearWatchTimeState() {
	m_impl->subsystemRuntime.mediaComponent->clearWatchTimeState();
}

ofxVlc4::WatchTimeInfo ofxVlc4::getWatchTimeInfo() const {
	return m_impl->subsystemRuntime.mediaComponent->getWatchTimeInfo();
}

void ofxVlc4::setWatchTimeCallback(WatchTimeCallback callback) {
	std::lock_guard<std::mutex> lock(m_impl->synchronizationRuntime.watchTimeMutex);
	m_impl->watchTimeRuntime.callback = std::move(callback);
}

void ofxVlc4::clearWatchTimeCallback() {
	setWatchTimeCallback({});
}

bool ofxVlc4::hasWatchTimeCallback() const {
	std::lock_guard<std::mutex> lock(m_impl->synchronizationRuntime.watchTimeMutex);
	return static_cast<bool>(m_impl->watchTimeRuntime.callback);
}

double ofxVlc4::getPlaybackClockFramesPerSecond() const {
	const double cachedFps = m_impl->stateCacheRuntime.cachedVideoTrackFps.load();
	if (std::isfinite(cachedFps) && cachedFps > 0.0) {
		return cachedFps;
	}

	libvlc_media_player_t * player = sessionPlayer();
	if (player && libvlc_media_player_is_playing(player)) {
		return 0.0;
	}

	const auto tracks = getVideoTracks();
	auto resolveFps = [](const MediaTrackInfo & track) -> double {
		if (track.frameRateNum == 0 || track.frameRateDen == 0) {
			return 0.0;
		}
		return static_cast<double>(track.frameRateNum) / static_cast<double>(track.frameRateDen);
	};

	for (const auto & track : tracks) {
		if (!track.selected) {
			continue;
		}
		const double fps = resolveFps(track);
		if (std::isfinite(fps) && fps > 0.0) {
			return fps;
		}
	}

	for (const auto & track : tracks) {
		const double fps = resolveFps(track);
		if (std::isfinite(fps) && fps > 0.0) {
			m_impl->stateCacheRuntime.cachedVideoTrackFps.store(fps);
			return fps;
		}
	}

	return 0.0;
}

std::string ofxVlc4::formatCurrentPlaybackTimecode(double fps, bool interpolated) const {
	const WatchTimeInfo watchTime = getWatchTimeInfo();
	const int64_t timeUs = interpolated ? watchTime.interpolatedTimeUs : watchTime.timeUs;
	const double resolvedFps = (std::isfinite(fps) && fps > 0.0) ? fps : getPlaybackClockFramesPerSecond();
	return formatPlaybackTimecode(timeUs, resolvedFps);
}

std::string ofxVlc4::formatPlaybackTimecode(int64_t timeUs, double fps) {
	return formatPlaybackTimecodeValue(timeUs, fps);
}

void ofxVlc4::applyWatchTimeObserver() {
	m_impl->subsystemRuntime.mediaComponent->applyWatchTimeObserver();
}

void ofxVlc4::watchTimeUpdateStatic(const libvlc_media_player_time_point_t * value, void * data) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire) || !cb->owner || !value) {
		return;
	}
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) {
		return;
	}
	auto * player = scope.get();

	WatchTimeCallback callback;
	WatchTimeInfo info;
	{
		std::lock_guard<std::mutex> lock(player->m_impl->synchronizationRuntime.watchTimeMutex);
		player->m_impl->watchTimeRuntime.lastPoint = *value;
		player->m_impl->watchTimeRuntime.pointAvailable = true;
		player->m_impl->watchTimeRuntime.paused = value->system_date_us == INT64_MAX;
		player->m_impl->watchTimeRuntime.seeking = false;
		player->m_impl->watchTimeRuntime.lastEventType = WatchTimeEventType::Update;
		++player->m_impl->watchTimeRuntime.updateSequence;
		if (!player->m_impl->watchTimeRuntime.paused) {
			player->m_impl->watchTimeRuntime.pauseSystemDateUs = 0;
		}
		callback = player->m_impl->watchTimeRuntime.callback;
		info = buildWatchTimeInfoSnapshot(
			*value,
			player->m_impl->watchTimeRuntime.enabled,
			player->m_impl->watchTimeRuntime.registered,
			player->m_impl->watchTimeRuntime.pointAvailable,
			player->m_impl->watchTimeRuntime.paused,
			player->m_impl->watchTimeRuntime.seeking,
			player->m_impl->watchTimeRuntime.minPeriodUs,
			player->m_impl->watchTimeRuntime.pauseSystemDateUs,
			player->m_impl->watchTimeRuntime.lastEventType,
			player->m_impl->watchTimeRuntime.updateSequence,
			false);
	}
	if (callback) {
		callback(info);
	}
}

void ofxVlc4::watchTimePausedStatic(int64_t system_date_us, void * data) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire) || !cb->owner) {
		return;
	}
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) {
		return;
	}
	auto * player = scope.get();

	WatchTimeCallback callback;
	WatchTimeInfo info;
	{
		std::lock_guard<std::mutex> lock(player->m_impl->synchronizationRuntime.watchTimeMutex);
		player->m_impl->watchTimeRuntime.paused = true;
		player->m_impl->watchTimeRuntime.pauseSystemDateUs = system_date_us;
		player->m_impl->watchTimeRuntime.seeking = false;
		player->m_impl->watchTimeRuntime.lastEventType = WatchTimeEventType::Paused;
		++player->m_impl->watchTimeRuntime.updateSequence;
		callback = player->m_impl->watchTimeRuntime.callback;
		info = buildWatchTimeInfoSnapshot(
			player->m_impl->watchTimeRuntime.lastPoint,
			player->m_impl->watchTimeRuntime.enabled,
			player->m_impl->watchTimeRuntime.registered,
			player->m_impl->watchTimeRuntime.pointAvailable,
			player->m_impl->watchTimeRuntime.paused,
			player->m_impl->watchTimeRuntime.seeking,
			player->m_impl->watchTimeRuntime.minPeriodUs,
			player->m_impl->watchTimeRuntime.pauseSystemDateUs,
			player->m_impl->watchTimeRuntime.lastEventType,
			player->m_impl->watchTimeRuntime.updateSequence,
			false);
	}
	if (callback) {
		callback(info);
	}
}

void ofxVlc4::watchTimeSeekStatic(const libvlc_media_player_time_point_t * value, void * data) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire) || !cb->owner) {
		return;
	}
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope) {
		return;
	}
	auto * player = scope.get();

	WatchTimeCallback callback;
	WatchTimeInfo info;
	{
		std::lock_guard<std::mutex> lock(player->m_impl->synchronizationRuntime.watchTimeMutex);
		player->m_impl->watchTimeRuntime.seeking = value != nullptr;
		player->m_impl->watchTimeRuntime.lastEventType = value ? WatchTimeEventType::Seek : WatchTimeEventType::SeekEnd;
		++player->m_impl->watchTimeRuntime.updateSequence;
		if (value) {
			player->m_impl->watchTimeRuntime.lastPoint = *value;
			player->m_impl->watchTimeRuntime.pointAvailable = true;
			player->m_impl->watchTimeRuntime.paused = value->system_date_us == INT64_MAX;
			if (!player->m_impl->watchTimeRuntime.paused) {
				player->m_impl->watchTimeRuntime.pauseSystemDateUs = 0;
			}
		}
		callback = player->m_impl->watchTimeRuntime.callback;
		info = buildWatchTimeInfoSnapshot(
			player->m_impl->watchTimeRuntime.lastPoint,
			player->m_impl->watchTimeRuntime.enabled,
			player->m_impl->watchTimeRuntime.registered,
			player->m_impl->watchTimeRuntime.pointAvailable,
			player->m_impl->watchTimeRuntime.paused,
			player->m_impl->watchTimeRuntime.seeking,
			player->m_impl->watchTimeRuntime.minPeriodUs,
			player->m_impl->watchTimeRuntime.pauseSystemDateUs,
			player->m_impl->watchTimeRuntime.lastEventType,
			player->m_impl->watchTimeRuntime.updateSequence,
			false);
	}
	if (callback) {
		callback(info);
	}
}

bool ofxVlc4::postDialogLogin(
	std::uintptr_t token,
	const std::string & username,
	const std::string & password,
	bool store) {
	return m_impl->subsystemRuntime.mediaComponent->postDialogLogin(token, username, password, store);
}

bool ofxVlc4::postDialogAction(std::uintptr_t token, int action) {
	return m_impl->subsystemRuntime.mediaComponent->postDialogAction(token, action);
}

bool ofxVlc4::dismissDialog(std::uintptr_t token) {
	return m_impl->subsystemRuntime.mediaComponent->dismissDialog(token);
}

void ofxVlc4::MediaComponent::setLibVlcLoggingEnabledValue(bool enabled) {
	owner.m_impl->subsystemRuntime.coreSession->setLoggingEnabled(enabled);
}

void ofxVlc4::MediaComponent::setLibVlcLogFileEnabledValue(bool enabled) {
	owner.m_impl->subsystemRuntime.coreSession->setLogFileEnabled(enabled);
}

void ofxVlc4::MediaComponent::setLibVlcLogFilePathValue(const std::string & path) {
	owner.m_impl->subsystemRuntime.coreSession->setLogFilePath(path);
}

std::vector<std::uintptr_t> ofxVlc4::MediaComponent::getActiveDialogTokensLocked() const {
	std::vector<std::uintptr_t> tokens;
	tokens.reserve(owner.m_impl->diagnosticsRuntime.activeDialogs.size());
	for (const DialogInfo & dialog : owner.m_impl->diagnosticsRuntime.activeDialogs) {
		tokens.push_back(dialog.token);
	}
	return tokens;
}

void ofxVlc4::MediaComponent::upsertActiveDialogLocked(const DialogInfo & dialog) {
	const auto it = std::find_if(
		owner.m_impl->diagnosticsRuntime.activeDialogs.begin(),
		owner.m_impl->diagnosticsRuntime.activeDialogs.end(),
		[&dialog](const DialogInfo & existing) { return existing.token == dialog.token; });
	if (it != owner.m_impl->diagnosticsRuntime.activeDialogs.end()) {
		*it = dialog;
	} else {
		owner.m_impl->diagnosticsRuntime.activeDialogs.push_back(dialog);
	}
}

void ofxVlc4::MediaComponent::removeActiveDialogLocked(std::uintptr_t token) {
	owner.m_impl->diagnosticsRuntime.activeDialogs.erase(
		std::remove_if(
			owner.m_impl->diagnosticsRuntime.activeDialogs.begin(),
			owner.m_impl->diagnosticsRuntime.activeDialogs.end(),
			[token](const DialogInfo & dialog) { return dialog.token == token; }),
		owner.m_impl->diagnosticsRuntime.activeDialogs.end());
}

void ofxVlc4::MediaComponent::clearLastDialogErrorLocked() {
	owner.m_impl->diagnosticsRuntime.lastDialogError = DialogErrorInfo {};
}


void ofxVlc4::handleDialogDisplayLogin(
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	const char * defaultUsername,
	bool askStore) {
	if (!id) {
		return;
	}

	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Login;
	dialog.title = title ? title : "";
	dialog.text = text ? text : "";
	dialog.defaultUsername = defaultUsername ? defaultUsername : "";
	dialog.askStore = askStore;
	dialog.cancellable = true;
	upsertDialog(dialog);
}

void ofxVlc4::handleDialogDisplayQuestion(
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	libvlc_dialog_question_type type,
	const char * cancel,
	const char * action1,
	const char * action2) {
	if (!id) {
		return;
	}

	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Question;
	dialog.severity = toDialogQuestionSeverity(type);
	dialog.title = title ? title : "";
	dialog.text = text ? text : "";
	dialog.cancelLabel = cancel ? cancel : "";
	dialog.action1Label = action1 ? action1 : "";
	dialog.action2Label = action2 ? action2 : "";
	dialog.cancellable = !dialog.cancelLabel.empty();
	upsertDialog(dialog);
}

void ofxVlc4::handleDialogDisplayProgress(
	libvlc_dialog_id * id,
	const char * title,
	const char * text,
	bool indeterminate,
	float position,
	const char * cancel) {
	if (!id) {
		return;
	}

	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Progress;
	dialog.title = title ? title : "";
	dialog.text = text ? text : "";
	dialog.progressIndeterminate = indeterminate;
	dialog.progressPosition = ofClamp(position, 0.0f, 1.0f);
	dialog.cancelLabel = cancel ? cancel : "";
	dialog.cancellable = !dialog.cancelLabel.empty();
	upsertDialog(dialog);
}

void ofxVlc4::handleDialogCancel(libvlc_dialog_id * id) {
	if (!id) {
		return;
	}

	const std::uintptr_t token = reinterpret_cast<std::uintptr_t>(id);
	removeDialog(token);
	libvlc_dialog_dismiss(id);
}

void ofxVlc4::handleDialogUpdateProgress(libvlc_dialog_id * id, float position, const char * text) {
	if (!id) {
		return;
	}
	DialogInfo dialog;
	dialog.token = reinterpret_cast<std::uintptr_t>(id);
	dialog.type = DialogType::Progress;
	dialog.progressPosition = ofClamp(position, 0.0f, 1.0f);
	dialog.text = text ? text : "";

	{
		std::lock_guard<std::mutex> lock(m_impl->synchronizationRuntime.dialogMutex);
		const auto it = std::find_if(
			m_impl->diagnosticsRuntime.activeDialogs.begin(),
			m_impl->diagnosticsRuntime.activeDialogs.end(),
			[&dialog](const DialogInfo & existing) { return existing.token == dialog.token; });
		if (it != m_impl->diagnosticsRuntime.activeDialogs.end()) {
			dialog.title = it->title;
			dialog.cancelLabel = it->cancelLabel;
			dialog.cancellable = it->cancellable;
			dialog.progressIndeterminate = it->progressIndeterminate;
			*it = dialog;
			return;
		}
	}

	upsertDialog(dialog);
}

void ofxVlc4::handleDialogError(const char * title, const char * text) {
	{
		std::lock_guard<std::mutex> lock(m_impl->synchronizationRuntime.dialogMutex);
		m_impl->diagnosticsRuntime.lastDialogError.available = true;
		m_impl->diagnosticsRuntime.lastDialogError.title = title ? title : "";
		m_impl->diagnosticsRuntime.lastDialogError.text = text ? text : "";
	}

	if (text && *text) {
		setError(text);
	}
}
