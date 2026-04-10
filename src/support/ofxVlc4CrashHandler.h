#pragma once

// ---------------------------------------------------------------------------
// Lightweight crash handler for ofxVlc4 applications.
//
// Call ofxVlc4CrashHandler::install() early in main() to register signal
// handlers for fatal signals (SIGSEGV, SIGABRT, SIGFPE, SIGILL, and on
// POSIX SIGBUS).  When a crash occurs the handler writes signal details
// and, where available, a stack-trace to stderr *and* to a crash-log file.
//
// The handler deliberately uses only async-signal-safe functions inside the
// signal callback so it is safe to call from any thread or context.
// ---------------------------------------------------------------------------

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#endif

namespace ofxVlc4CrashHandler {

// ---------------------------------------------------------------------------
// Internal helpers — async-signal-safe write utilities.
// ---------------------------------------------------------------------------
namespace detail {

// Async-signal-safe write of a C string to a file descriptor (POSIX) or
// a HANDLE (Windows).
#ifndef _WIN32
inline void safeWrite(int fd, const char * buf, size_t len) {
	while (len > 0) {
		const ssize_t n = ::write(fd, buf, len);
		if (n <= 0) break;
		buf += n;
		len -= static_cast<size_t>(n);
	}
}

inline void safeWriteStr(int fd, const char * str) {
	if (!str) return;
	size_t len = 0;
	while (str[len] != '\0') ++len;
	safeWrite(fd, str, len);
}

// Async-signal-safe unsigned-to-decimal string (writes into caller buffer).
inline int unsignedToStr(unsigned long long value, char * buf, int bufSize) {
	if (bufSize < 2) return 0;
	char tmp[24];
	int pos = 0;
	do {
		tmp[pos++] = static_cast<char>('0' + (value % 10));
		value /= 10;
	} while (value > 0 && pos < static_cast<int>(sizeof(tmp)));
	if (pos >= bufSize) pos = bufSize - 1;
	for (int i = 0; i < pos; ++i) {
		buf[i] = tmp[pos - 1 - i];
	}
	buf[pos] = '\0';
	return pos;
}
#endif

inline const char * signalName(int sig) {
	switch (sig) {
	case SIGSEGV: return "SIGSEGV";
	case SIGABRT: return "SIGABRT";
	case SIGFPE:  return "SIGFPE";
	case SIGILL:  return "SIGILL";
#ifndef _WIN32
	case SIGBUS:  return "SIGBUS";
#endif
	default:      return "UNKNOWN";
	}
}

inline const char * signalDescription(int sig) {
	switch (sig) {
	case SIGSEGV: return "Segmentation fault (invalid memory access)";
	case SIGABRT: return "Aborted (assertion failure or abort() call)";
	case SIGFPE:  return "Floating-point exception (division by zero or overflow)";
	case SIGILL:  return "Illegal instruction";
#ifndef _WIN32
	case SIGBUS:  return "Bus error (misaligned or non-existent memory access)";
#endif
	default:      return "Unknown fatal signal";
	}
}

// Stored crash-log file path (set once during install).
inline char gCrashLogPath[1024] = {};
inline std::atomic<bool> gInstalled { false };

#ifndef _WIN32
// POSIX signal handler — async-signal-safe.
inline void signalHandler(int sig) {
	// Prevent re-entrance.
	static volatile sig_atomic_t reentrant = 0;
	if (reentrant) {
		_exit(128 + sig);
	}
	reentrant = 1;

	const char * name = signalName(sig);
	const char * desc = signalDescription(sig);

	// ---- Write to stderr ----
	safeWriteStr(STDERR_FILENO, "\n[ofxVlc4] FATAL SIGNAL: ");
	safeWriteStr(STDERR_FILENO, name);
	safeWriteStr(STDERR_FILENO, " (");
	{
		char numBuf[24];
		unsignedToStr(static_cast<unsigned long long>(sig), numBuf, sizeof(numBuf));
		safeWriteStr(STDERR_FILENO, numBuf);
	}
	safeWriteStr(STDERR_FILENO, ")\n");
	safeWriteStr(STDERR_FILENO, "[ofxVlc4] ");
	safeWriteStr(STDERR_FILENO, desc);
	safeWriteStr(STDERR_FILENO, "\n");

	// ---- Write to crash-log file (if configured) ----
	int logFd = -1;
	if (gCrashLogPath[0] != '\0') {
		logFd = ::open(gCrashLogPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
	}
	if (logFd >= 0) {
		safeWriteStr(logFd, "[ofxVlc4] FATAL SIGNAL: ");
		safeWriteStr(logFd, name);
		safeWriteStr(logFd, " (");
		{
			char numBuf[24];
			unsignedToStr(static_cast<unsigned long long>(sig), numBuf, sizeof(numBuf));
			safeWriteStr(logFd, numBuf);
		}
		safeWriteStr(logFd, ")\n");
		safeWriteStr(logFd, "[ofxVlc4] ");
		safeWriteStr(logFd, desc);
		safeWriteStr(logFd, "\n");
	}

#if defined(__APPLE__) || defined(__linux__)
	// Stack trace via backtrace() — not guaranteed async-signal-safe but
	// works reliably on glibc and macOS in practice.
	constexpr int kMaxFrames = 64;
	void * frames[kMaxFrames];
	const int frameCount = backtrace(frames, kMaxFrames);
	if (frameCount > 0) {
		safeWriteStr(STDERR_FILENO, "[ofxVlc4] Stack trace:\n");
		backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
		if (logFd >= 0) {
			safeWriteStr(logFd, "[ofxVlc4] Stack trace:\n");
			backtrace_symbols_fd(frames, frameCount, logFd);
		}
	}
#endif

	if (logFd >= 0) {
		safeWriteStr(logFd, "---\n");
		::close(logFd);
	}

	// Restore default handler and re-raise to produce a core dump / normal exit.
	signal(sig, SIG_DFL);
	raise(sig);
}
#endif // !_WIN32

#ifdef _WIN32
inline LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS * info) {
	const DWORD code = info ? info->ExceptionRecord->ExceptionCode : 0;

	FILE * logFile = nullptr;
	if (gCrashLogPath[0] != '\0') {
		logFile = std::fopen(gCrashLogPath, "a");
	}

	auto writeAll = [&](const char * msg) {
		if (msg) {
			std::fputs(msg, stderr);
			if (logFile) std::fputs(msg, logFile);
		}
	};

	writeAll("\n[ofxVlc4] FATAL EXCEPTION: code 0x");
	{
		char hexBuf[20];
		std::snprintf(hexBuf, sizeof(hexBuf), "%08lX", static_cast<unsigned long>(code));
		writeAll(hexBuf);
	}
	writeAll("\n");

	// Map well-known SEH codes.
	const char * desc = nullptr;
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:     desc = "Access violation"; break;
	case EXCEPTION_STACK_OVERFLOW:       desc = "Stack overflow"; break;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:   desc = "Integer division by zero"; break;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:   desc = "Float division by zero"; break;
	case EXCEPTION_ILLEGAL_INSTRUCTION:  desc = "Illegal instruction"; break;
	default: break;
	}
	if (desc) {
		writeAll("[ofxVlc4] ");
		writeAll(desc);
		writeAll("\n");
	}

	// Stack trace via CaptureStackBackTrace.
	{
		void * stack[64];
		const USHORT frames = CaptureStackBackTrace(0, 64, stack, nullptr);
		if (frames > 0) {
			writeAll("[ofxVlc4] Stack trace (");
			{
				char numBuf[12];
				std::snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(frames));
				writeAll(numBuf);
			}
			writeAll(" frames):\n");
			for (USHORT i = 0; i < frames; ++i) {
				char frameBuf[64];
				std::snprintf(frameBuf, sizeof(frameBuf), "  [%u] 0x%p\n",
					static_cast<unsigned>(i), stack[i]);
				writeAll(frameBuf);
			}
		}
	}

	if (logFile) {
		std::fputs("---\n", logFile);
		std::fflush(logFile);
		std::fclose(logFile);
	}

	return EXCEPTION_CONTINUE_SEARCH;
}
#endif // _WIN32

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// @brief Install the crash handler.
/// @param crashLogPath  Optional file path for the crash log.  When empty,
///   only stderr output is produced.  The file is opened in append mode so
///   successive crashes accumulate in the same log.
/// @return true if the handler was installed, false if already installed.
inline bool install(const std::string & crashLogPath = "") {
	bool expected = false;
	if (!detail::gInstalled.compare_exchange_strong(expected, true)) {
		return false;
	}

	// Store the log path for the signal handler (fixed-size buffer to stay
	// async-signal-safe on the read side).
	if (!crashLogPath.empty()) {
		const size_t len = crashLogPath.size() < sizeof(detail::gCrashLogPath) - 1
			? crashLogPath.size()
			: sizeof(detail::gCrashLogPath) - 1;
		std::memcpy(detail::gCrashLogPath, crashLogPath.c_str(), len);
		detail::gCrashLogPath[len] = '\0';
	}

#ifdef _WIN32
	SetUnhandledExceptionFilter(detail::unhandledExceptionFilter);
#else
	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sa_handler = detail::signalHandler;
	sigemptyset(&sa.sa_mask);
	// SA_RESETHAND restores the default handler after the first invocation
	// so that the re-raised signal terminates the process normally.
	sa.sa_flags = SA_RESETHAND;

	sigaction(SIGSEGV, &sa, nullptr);
	sigaction(SIGABRT, &sa, nullptr);
	sigaction(SIGFPE, &sa, nullptr);
	sigaction(SIGILL, &sa, nullptr);
	sigaction(SIGBUS, &sa, nullptr);
#endif

	// Also install a C++ terminate handler for uncaught exceptions.
	std::set_terminate([]() {
		std::fprintf(stderr, "\n[ofxVlc4] std::terminate() called — uncaught exception or double-exception.\n");
		if (detail::gCrashLogPath[0] != '\0') {
			FILE * f = std::fopen(detail::gCrashLogPath, "a");
			if (f) {
				std::fputs("[ofxVlc4] std::terminate() called — uncaught exception or double-exception.\n---\n", f);
				std::fflush(f);
				std::fclose(f);
			}
		}
		std::abort();
	});

	return true;
}

/// @brief Check whether the crash handler has been installed.
inline bool isInstalled() {
	return detail::gInstalled.load();
}

} // namespace ofxVlc4CrashHandler
