// Forced include for imgui_impl_opengl3.cpp on macOS (IMGUI_IMPL_OPENGL_LOADER_CUSTOM).
// Routing the backend through GLEW lets mac_gl_compat alias core entry points (glBindVertexArray,
// ...) to their APPLE/ARB equivalents when Minecraft runs on Apple's legacy OpenGL 2.1 context.
#pragma once
#if defined(__cplusplus)
#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>
#endif
