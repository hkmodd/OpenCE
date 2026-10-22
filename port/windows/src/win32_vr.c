/*
WIN32_VR.C

The headset's OpenXR runtime on Windows (port/linux/src/vr_openxr.c): its
manifest, from XR_RUNTIME_JSON or the registry's ActiveRuntime (a 32-bit
program reads it from WOW6432Node, where 32-bit runtimes are listed), its
library, and the session's binding to the game's OpenGL context (WGL's).
*/

#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_PLATFORM_WIN32
#include "../../third_party/openxr/openxr/openxr.h"
#include "../../third_party/openxr/openxr/openxr_platform.h"

void platform_log(const char *format, ...);

static int vr_utf8(const wchar_t *text, char *out, size_t size)
{
	return WideCharToMultiByte(CP_UTF8, 0, text, -1, out, (int)size, NULL, NULL) > 0;
}

int vr_platform_runtime_manifest(char *path, size_t size)
{
	wchar_t text[1024];
	DWORD bytes = sizeof(text);
	DWORD length = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", text, sizeof(text) / sizeof(text[0]));

	if (length > 0 && length < sizeof(text) / sizeof(text[0]))
	{
		platform_log("vr: XR_RUNTIME_JSON names the runtime");
		return vr_utf8(text, path, size);
	}
	/* (redirected to WOW6432Node for this 32-bit program) */
	if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_RT_REG_SZ, NULL, text,
		&bytes) != ERROR_SUCCESS)
	{
		return 0;
	}
	return vr_utf8(text, path, size);
}

void *vr_platform_library_open(const char *path)
{
	wchar_t text[1024];
	HMODULE library;
	DWORD error;

	if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, text, sizeof(text) / sizeof(text[0])))
		return NULL;
	/* (a runtime's own DLLs beside it are found from its folder) */
	library = LoadLibraryExW(text, NULL, wcschr(text, L'\\') || wcschr(text, L'/') ? LOAD_WITH_ALTERED_SEARCH_PATH : 0);
	if (library)
		return library;
	error = GetLastError();
	if (error == ERROR_BAD_EXE_FORMAT)
		platform_log("vr: the OpenXR runtime's library is not 32-bit: the game needs a 32-bit runtime");
	else
		platform_log("vr: cannot load the OpenXR runtime's library (error %lu)", (unsigned long)error);
	return NULL;
}

void *vr_platform_library_symbol(void *library, const char *name)
{
	return (void *)GetProcAddress((HMODULE)library, name);
}

const char *vr_platform_graphics_extension(void)
{
	return NULL;
}

const void *vr_platform_graphics_binding(void)
{
	static XrGraphicsBindingOpenGLWin32KHR binding;
	HMODULE opengl = GetModuleHandleW(L"opengl32.dll");
	HDC (WINAPI *get_current_dc)(void);
	HGLRC (WINAPI *get_current_context)(void);

	if (!opengl)
		return NULL;
	get_current_dc = (HDC (WINAPI *)(void))GetProcAddress(opengl, "wglGetCurrentDC");
	get_current_context = (HGLRC (WINAPI *)(void))GetProcAddress(opengl, "wglGetCurrentContext");
	if (!get_current_dc || !get_current_context)
		return NULL;
	memset(&binding, 0, sizeof(binding));
	binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
	binding.hDC = get_current_dc();
	binding.hGLRC = get_current_context();
	return binding.hDC && binding.hGLRC ? &binding : NULL;
}
