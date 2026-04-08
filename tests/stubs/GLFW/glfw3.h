#pragma once
// Minimal stub of GLFW used by ofxVlc4Utils.h.
typedef struct GLFWwindow GLFWwindow;
inline GLFWwindow * glfwGetCurrentContext() { return nullptr; }
inline void glfwMakeContextCurrent(GLFWwindow *) {}
