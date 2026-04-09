#pragma once

// ofxVlc4GlOps.h – Low-level GL operation helpers for playback and recording.
//
// Each helper encapsulates a recurring GL call pattern so that:
//   1. The pattern is documented in one place.
//   2. The pattern is independently testable with GL call-recording stubs.
//
// All functions are inline and stateless – they call through to the GL API
// functions provided by the platform (or by test stubs).

#include "ofMain.h"

#include <cstddef>
#include <vector>

namespace ofxVlc4GlOps {

// ---------------------------------------------------------------------------
// Fence-sync helpers (used in both playback and recording)
// ---------------------------------------------------------------------------

// Delete a GL sync object if it is non-null, then set the pointer to nullptr.
inline void deleteFenceSync(GLsync & fence) {
	if (fence != nullptr) {
		glDeleteSync(fence);
		fence = nullptr;
	}
}

// Insert a new GPU fence sync (GL_SYNC_GPU_COMMANDS_COMPLETE).
inline GLsync insertFenceSync() {
	return glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

// CPU-wait on a fence.  Returns the raw GLenum result from glClientWaitSync:
//   GL_ALREADY_SIGNALED   – the fence was already complete.
//   GL_CONDITION_SATISFIED – the fence completed within the timeout.
//   GL_TIMEOUT_EXPIRED     – the timeout elapsed before the fence completed;
//                            the caller may hand the fence to the GPU pipeline
//                            via gpuWaitFenceSync() and then delete it.
//   GL_WAIT_FAILED         – an error occurred; the caller still owns the
//                            fence and should clean up with deleteFenceSync().
//
// Does NOT delete the fence – the caller decides what to do based on the
// result.  This matches the recording path where the fence lifetime is
// managed separately.
inline GLenum clientWaitFenceSync(GLsync fence, GLbitfield flags, GLuint64 timeoutNs) {
	return glClientWaitSync(fence, flags, timeoutNs);
}

// Hand a fence wait to the GPU command pipeline.  Used in the playback path
// when the CPU wait times out but the consumer still needs ordering
// guarantees.
inline void gpuWaitFenceSync(GLsync fence) {
	glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
}

// ---------------------------------------------------------------------------
// Framebuffer helpers (playback render target)
// ---------------------------------------------------------------------------

// Set up an FBO backed by a texture at color-attachment 0.  If *fboId is
// zero a new framebuffer is generated; otherwise the existing one is reused.
// The FBO is cleared to transparent black after attachment.
// Returns true when the FBO is ready for use.
inline bool setupFboWithTexture(GLuint & fboId, GLenum texTarget, GLuint texId) {
	if (texId == 0) {
		return false;
	}

	if (fboId == 0) {
		glGenFramebuffers(1, &fboId);
		if (fboId == 0) {
			return false;
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fboId);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texTarget, texId, 0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	ofClear(0, 0, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return true;
}

// Bind an FBO and optionally re-attach the texture when the dirty flag is
// set.  The dirty flag is cleared after a successful re-attach.
inline void bindFbo(GLuint fboId, bool & attachmentDirty, GLenum texTarget, GLuint texId) {
	glBindFramebuffer(GL_FRAMEBUFFER, fboId);
	if (attachmentDirty) {
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texTarget, texId, 0);
		attachmentDirty = false;
	}
}

// Unbind the currently bound FBO (bind framebuffer 0).
inline void unbindFbo() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Delete a framebuffer if its id is non-zero, then reset to zero.
inline void deleteFbo(GLuint & fboId) {
	if (fboId != 0) {
		glDeleteFramebuffers(1, &fboId);
		fboId = 0;
	}
}

// ---------------------------------------------------------------------------
// Texture helpers (playback render target setup)
// ---------------------------------------------------------------------------

// Set bilinear (GL_LINEAR) min/mag filtering on a texture.
inline void setTextureLinearFiltering(GLenum texTarget, GLuint texId) {
	glBindTexture(texTarget, texId);
	glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(texTarget, 0);
}

// ---------------------------------------------------------------------------
// Pixel-pack buffer (PBO) helpers (recording readback)
// ---------------------------------------------------------------------------

// Allocate and size a set of PBOs for asynchronous pixel-pack readback.
// Returns true on success.  On failure the caller should fall back to
// synchronous readback.
inline bool allocatePixelPackBuffers(std::vector<GLuint> & pbos, size_t count, size_t frameBytes) {
	if (count == 0 || frameBytes == 0) {
		return false;
	}

	pbos.assign(count, 0);
	glGenBuffers(static_cast<GLsizei>(count), pbos.data());

	for (GLuint pbo : pbos) {
		if (pbo == 0) {
			return false;
		}
	}

	for (GLuint pbo : pbos) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
		glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(frameBytes), nullptr, GL_STREAM_READ);
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	return true;
}

// Destroy PBO buffers and their associated fence objects.  Both vectors are
// cleared on return.
inline void destroyPixelPackBuffers(std::vector<GLuint> & pbos, std::vector<GLsync> & fences) {
	for (GLsync & fence : fences) {
		if (fence != nullptr) {
			glDeleteSync(fence);
			fence = nullptr;
		}
	}

	if (!pbos.empty()) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glDeleteBuffers(static_cast<GLsizei>(pbos.size()), pbos.data());
	}

	pbos.clear();
	fences.clear();
}

// Submit a texture-readback into a PBO.  Reads RGB / GL_UNSIGNED_BYTE,
// which matches the recording pipeline's pixel format.  The texture data
// is copied asynchronously; a fence sync is inserted and returned so the
// caller can later wait for completion.
inline GLsync submitTextureReadback(GLuint pbo, GLenum texTarget, GLuint texId) {
	glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
	glBindTexture(texTarget, texId);
	glGetTexImage(texTarget, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glBindTexture(texTarget, 0);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	return glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

// Map a PBO for CPU read.  Returns the mapped pointer, or nullptr on
// failure.  On success the PBO remains bound (call unmapPixelPackBuffer()
// when done).  On failure the PBO is unbound.
inline void * mapPixelPackBuffer(GLuint pbo) {
	glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
	void * mapped = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
	if (!mapped) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
	return mapped;
}

// Unmap the currently mapped PBO and unbind it.
inline void unmapPixelPackBuffer() {
	glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// Miscellaneous
// ---------------------------------------------------------------------------

// Flush all pending GL commands to the GPU.  Used when releasing a shared
// GL context so that fences inserted by the producer are visible to the
// consumer context.
inline void flushCommands() {
	glFlush();
}

} // namespace ofxVlc4GlOps
