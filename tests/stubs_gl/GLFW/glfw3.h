#pragma once

// Minimal GLFW stub for test_gl.
// Provides a controllable inline global so tests can simulate the presence
// or absence of an active OpenGL context without a real display.

struct GLFWwindow {};

// Tests set this to point at a local GLFWwindow instance to fake a context,
// or leave it as nullptr to simulate no context.
inline GLFWwindow * g_glfwCurrentContext = nullptr;

inline GLFWwindow * glfwGetCurrentContext() {
	return g_glfwCurrentContext;
}
