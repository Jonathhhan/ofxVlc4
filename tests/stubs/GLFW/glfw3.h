#pragma once

// Minimal stub of GLFW for the ofxVlc4 unit tests.
// Only provides the symbols that ofxVlc4Utils.h needs.

typedef struct GLFWwindow GLFWwindow;

inline GLFWwindow * glfwGetCurrentContext() { return nullptr; }
