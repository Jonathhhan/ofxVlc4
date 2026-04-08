#pragma once

// Minimal stub for GLFW glfw3.h.
// Provides only the symbols referenced by ofxVlc4Utils.h.

struct GLFWwindow;

inline GLFWwindow * glfwGetCurrentContext() {
	return nullptr;
}
