#pragma once

// Minimal OF stub for the GL tests (test_gl and test_gl_ops).
// Provides GL types, constants, and recordable GL function stubs, plus the
// OF symbols that ofxVlc4Utils.h and ofxVlc4GlOps.h need for compilation.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// VLC types required by ofxVlc4Utils.h
// ---------------------------------------------------------------------------

typedef enum {
	libvlc_NothingSpecial = 0,
	libvlc_Opening,
	libvlc_Buffering,
	libvlc_Playing,
	libvlc_Paused,
	libvlc_Stopped,
	libvlc_Ended,
	libvlc_Error,
	libvlc_Stopping
} libvlc_state_t;

typedef struct libvlc_media_player_t libvlc_media_player_t;

// ---------------------------------------------------------------------------
// GL types
// ---------------------------------------------------------------------------

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long long GLsizeiptr; // Simplified for test stubs; real GL uses ptrdiff_t.
typedef unsigned long long GLuint64;
typedef unsigned char GLboolean;
typedef void * GLsync;

// ---------------------------------------------------------------------------
// GL constants
// ---------------------------------------------------------------------------

#ifndef GL_RGBA
#define GL_RGBA                         0x1908
#endif
#ifndef GL_RGB
#define GL_RGB                          0x1907
#endif
#ifndef GL_RGB10_A2
#define GL_RGB10_A2                     0x8059
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE                0x1401
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY                    0x88B8
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                  0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0            0x8CE0
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D                   0x0DE1
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER           0x2801
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER           0x2800
#endif
#ifndef GL_LINEAR
#define GL_LINEAR                       0x2601
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER            0x88EB
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ                  0x88E1
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT               0x0D05
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE   0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT      0x00000001
#endif
#ifndef GL_TIMEOUT_IGNORED
#define GL_TIMEOUT_IGNORED              0xFFFFFFFFFFFFFFFFull
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED                  0x911D
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED              0x911B
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED             0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED          0x911C
#endif

// ---------------------------------------------------------------------------
// GL call log – records every GL stub invocation for test assertions.
// ---------------------------------------------------------------------------

struct GlCallEntry {
	std::string name;
	std::vector<uint64_t> args;
};

inline std::vector<GlCallEntry> & glCallLog() {
	static std::vector<GlCallEntry> log;
	return log;
}

inline void glCallLogClear() {
	glCallLog().clear();
}

// ---------------------------------------------------------------------------
// Controllable GL stub state
// ---------------------------------------------------------------------------

inline GLenum g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;
inline void * g_glMapBufferResult = nullptr;
inline bool g_glGenBuffersFail = false;
inline bool g_glGenFramebuffersFail = false;
inline GLuint g_glNextObjectId = 1;

// ---------------------------------------------------------------------------
// GL function stubs
// ---------------------------------------------------------------------------

inline void glDeleteSync(GLsync fence) {
	glCallLog().push_back({"glDeleteSync", {reinterpret_cast<uint64_t>(fence)}});
}

inline GLsync glFenceSync(GLenum condition, GLbitfield flags) {
	GLsync sync = reinterpret_cast<GLsync>(static_cast<uintptr_t>(g_glNextObjectId++));
	glCallLog().push_back({"glFenceSync", {condition, flags}});
	return sync;
}

inline GLenum glClientWaitSync(GLsync fence, GLbitfield flags, GLuint64 timeout) {
	glCallLog().push_back({"glClientWaitSync", {reinterpret_cast<uint64_t>(fence), flags, timeout}});
	return g_glClientWaitSyncResult;
}

inline void glWaitSync(GLsync fence, GLbitfield flags, GLuint64 timeout) {
	glCallLog().push_back({"glWaitSync", {reinterpret_cast<uint64_t>(fence), flags, timeout}});
}

inline void glGenFramebuffers(GLsizei n, GLuint * ids) {
	glCallLog().push_back({"glGenFramebuffers", {static_cast<uint64_t>(n)}});
	for (GLsizei i = 0; i < n; ++i) {
		ids[i] = g_glGenFramebuffersFail ? 0 : g_glNextObjectId++;
	}
}

inline void glBindFramebuffer(GLenum target, GLuint framebuffer) {
	glCallLog().push_back({"glBindFramebuffer", {target, framebuffer}});
}

inline void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum texTarget, GLuint texture, GLint level) {
	glCallLog().push_back({"glFramebufferTexture2D", {target, attachment, texTarget, texture, static_cast<uint64_t>(level)}});
}

inline void glDrawBuffer(GLenum buf) {
	glCallLog().push_back({"glDrawBuffer", {buf}});
}

inline void glDeleteFramebuffers(GLsizei n, const GLuint * framebuffers) {
	glCallLog().push_back({"glDeleteFramebuffers", {static_cast<uint64_t>(n), framebuffers ? static_cast<uint64_t>(*framebuffers) : 0u}});
}

inline void glBindTexture(GLenum target, GLuint texture) {
	glCallLog().push_back({"glBindTexture", {target, texture}});
}

inline void glTexParameteri(GLenum target, GLenum pname, GLint param) {
	glCallLog().push_back({"glTexParameteri", {target, pname, static_cast<uint64_t>(param)}});
}

inline void glGenBuffers(GLsizei n, GLuint * buffers) {
	glCallLog().push_back({"glGenBuffers", {static_cast<uint64_t>(n)}});
	for (GLsizei i = 0; i < n; ++i) {
		buffers[i] = g_glGenBuffersFail ? 0 : g_glNextObjectId++;
	}
}

inline void glBindBuffer(GLenum target, GLuint buffer) {
	glCallLog().push_back({"glBindBuffer", {target, buffer}});
}

inline void glBufferData(GLenum target, GLsizeiptr size, const void * /*data*/, GLenum usage) {
	glCallLog().push_back({"glBufferData", {target, static_cast<uint64_t>(size), usage}});
}

inline void glDeleteBuffers(GLsizei n, const GLuint * /*buffers*/) {
	glCallLog().push_back({"glDeleteBuffers", {static_cast<uint64_t>(n)}});
}

inline void * glMapBuffer(GLenum target, GLenum access) {
	glCallLog().push_back({"glMapBuffer", {target, access}});
	return g_glMapBufferResult;
}

inline GLboolean glUnmapBuffer(GLenum target) {
	glCallLog().push_back({"glUnmapBuffer", {target}});
	return 1;
}

inline void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void * /*pixels*/) {
	glCallLog().push_back({"glGetTexImage", {target, static_cast<uint64_t>(level), format, type}});
}

inline void glFlush() {
	glCallLog().push_back({"glFlush", {}});
}

inline void glPixelStorei(GLenum pname, GLint param) {
	glCallLog().push_back({"glPixelStorei", {pname, static_cast<uint64_t>(param)}});
}

inline void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void * /*data*/) {
	glCallLog().push_back({"glReadPixels", {static_cast<uint64_t>(x), static_cast<uint64_t>(y),
		static_cast<uint64_t>(width), static_cast<uint64_t>(height), format, type}});
}

// ---------------------------------------------------------------------------
// Reset all controllable GL stub state (call between tests).
// ---------------------------------------------------------------------------

inline void resetGlStubs() {
	glCallLogClear();
	g_glClientWaitSyncResult = GL_ALREADY_SIGNALED;
	g_glMapBufferResult = nullptr;
	g_glGenBuffersFail = false;
	g_glGenFramebuffersFail = false;
	g_glNextObjectId = 1;
}

// ---------------------------------------------------------------------------
// OF utility functions used by ofxVlc4Utils.h
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ofBuffer stub (used by readTextFileIfPresent via ofBufferFromFile)
// ---------------------------------------------------------------------------

class ofBuffer {
public:
	ofBuffer() = default;
	explicit ofBuffer(const std::string & text) : _data(text) {}

	std::string getText() const { return _data; }
	std::size_t size() const { return _data.size(); }
	const char * getData() const { return _data.data(); }

private:
	std::string _data;
};

inline ofBuffer ofBufferFromFile(const std::string & path, bool /*binary*/ = false) {
	std::ifstream input(path, std::ios::in | std::ios::binary);
	if (!input.is_open()) {
		return ofBuffer();
	}
	std::ostringstream contents;
	contents << input.rdbuf();
	return ofBuffer(contents.str());
}

inline int ofStringTimesInString(const std::string & big, const std::string & small) {
	if (small.empty()) return 0;
	int count = 0;
	size_t pos = 0;
	while ((pos = big.find(small, pos)) != std::string::npos) {
		++count;
		pos += small.size();
	}
	return count;
}

template<typename T>
inline std::string ofToString(T value) {
	std::ostringstream ss;
	ss << value;
	return ss.str();
}

// ---------------------------------------------------------------------------
// ofFilePath stub
// ---------------------------------------------------------------------------

namespace ofFilePath {
inline std::string getFileName(const std::string & path) {
	const auto pos = path.find_last_of("/\\");
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

inline std::string getAbsolutePath(const std::string & path) {
	if (path.empty()) return path;
	std::error_code ec;
	const auto absolute = std::filesystem::absolute(path, ec);
	return ec ? path : absolute.string();
}
} // namespace ofFilePath

// ---------------------------------------------------------------------------
// ofFbo stub with observable call counters
// ---------------------------------------------------------------------------

class ofFbo {
public:
	bool isAllocated() const { return _allocated; }
	void setAllocated(bool v) { _allocated = v; }
	void begin() { ++beginCount; }
	void end() { ++endCount; }

	bool _allocated = false;
	int beginCount = 0;
	int endCount = 0;
};

// ---------------------------------------------------------------------------
// ofClear stub with call counter
// ---------------------------------------------------------------------------

inline int g_ofClearCallCount = 0;

inline void ofClear(float, float, float, float) {
	++g_ofClearCallCount;
}
