// macOS build: GLEW #undefs APIENTRY/CALLBACK at the end of its header on non-Windows platforms.
// The shared sources use both as (empty) calling-convention markers, so restore them.
#pragma once
#include_next <GL/glew.h>
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef WINAPI
#define WINAPI
#endif
