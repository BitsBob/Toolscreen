#pragma once

// Platform seam for shader compilation.
//
// Toolscreen's shaders are written for GLSL 3.30 core. On Windows every supported driver accepts them
// as-is, so these helpers are pass-throughs. On macOS, Minecraft 1.16 and older run on Apple's legacy
// OpenGL 2.1 context, which only accepts GLSL 1.20; the macOS implementation translates the sources
// (and binds the vertex attribute locations GLSL 1.20 cannot declare) when such a context is current.

#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>

#include <string>

#if defined(TOOLSCREEN_MACOS)
// Returns the source to hand to glShaderSource for the current context.
std::string ToolscreenPrepareShaderSource(GLenum shaderType, const char* source);
// Binds `layout(location = N)` vertex inputs by name; call between glAttachShader and glLinkProgram.
void ToolscreenBindShaderAttribLocations(GLuint program, const char* vertexSource);
// GLSL version string for ImGui_ImplOpenGL3_Init in the current context.
const char* ToolscreenImGuiGlslVersion();
// Called once GLEW has been initialised for Minecraft's context.
void ToolscreenOnGlewInitialized();
#else
inline std::string ToolscreenPrepareShaderSource(GLenum, const char* source) { return source ? source : ""; }
inline void ToolscreenBindShaderAttribLocations(GLuint, const char*) {}
inline const char* ToolscreenImGuiGlslVersion() { return "#version 330"; }
inline void ToolscreenOnGlewInitialized() {}
#endif
