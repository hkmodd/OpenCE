/*
WIN32_VR.C

The headset's OpenXR runtime on Windows (port/linux/src/vr_openxr.c): its
manifest, from XR_RUNTIME_JSON or the registry's ActiveRuntime (a 32-bit
program reads it from WOW6432Node, where 32-bit runtimes are listed), its
library, and the session's binding to the game's OpenGL context (WGL's).
Meta's runtime has one of its DLLs mended as it is loaded
(vr_meta_ipc_client).
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

/* Meta's 32-bit runtime (1.208) names its IPC client's threads by calling
SetThreadDescription, a WINAPI function that takes its arguments off the
stack, as a cdecl one, which leaves them to the caller: in
RuntimeIPCServiceClient_32.dll the caller then takes them off again, its
registers come back wrong and the thread dies (writing at 0x80) a moment
after the session starts. Its code leaves the name out when GetProcAddress
finds no SetThreadDescription, so that DLL is loaded here, before the
runtime starts its threads, and its GetProcAddress (from its imports) is
one that finds none; its threads are only unnamed. The runtime's other DLLs
call it as WINAPI and are left as they are. */
static FARPROC WINAPI vr_ipc_get_proc_address(HMODULE module, LPCSTR name)
{
	if (HIWORD((ULONG_PTR)name) && !strcmp(name, "SetThreadDescription"))
		return NULL;
	return GetProcAddress(module, name);
}

/* `module`'s import of `function` from `library` replaced by `replacement` */
static int vr_replace_import(HMODULE module, const char *library, const char *function, void *replacement)
{
	BYTE *base = (BYTE *)module;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	IMAGE_NT_HEADERS *headers;
	IMAGE_DATA_DIRECTORY *directory;
	IMAGE_IMPORT_DESCRIPTOR *import;

	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return 0;
	headers = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
	if (headers->Signature != IMAGE_NT_SIGNATURE)
		return 0;
	directory = &headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!directory->VirtualAddress)
		return 0;
	for (import = (IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress); import->Name; import++)
	{
		IMAGE_THUNK_DATA *names, *slots;

		if (lstrcmpiA((const char *)(base + import->Name), library) || !import->OriginalFirstThunk)
			continue;
		names = (IMAGE_THUNK_DATA *)(base + import->OriginalFirstThunk);
		slots = (IMAGE_THUNK_DATA *)(base + import->FirstThunk);
		for (; names->u1.AddressOfData; names++, slots++)
		{
			IMAGE_IMPORT_BY_NAME *by_name;
			DWORD protection;

			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			by_name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
			if (strcmp((const char *)by_name->Name, function))
				continue;
			if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protection))
				return 0;
			slots->u1.Function = (ULONG_PTR)replacement;
			VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), protection, &protection);
			return 1;
		}
	}
	return 0;
}

/* Meta's IPC client, beside the runtime's library `runtime`, if it is there */
static void vr_meta_ipc_client(const wchar_t *runtime)
{
	static const wchar_t name[] = L"RuntimeIPCServiceClient_32.dll";
	wchar_t path[1024];
	wchar_t *slash, *other;
	HMODULE module;

	if (wcslen(runtime) >= sizeof(path) / sizeof(path[0]))
		return;
	wcscpy(path, runtime);
	slash = wcsrchr(path, L'\\');
	other = wcsrchr(path, L'/');
	if (other > slash)
		slash = other;
	if (!slash || (size_t)(slash + 1 - path) + sizeof(name) / sizeof(name[0]) > sizeof(path) / sizeof(path[0]))
		return;
	wcscpy(slash + 1, name);
	if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
		return;
	module = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module)
		return;
	if (vr_replace_import(module, "KERNEL32.dll", "GetProcAddress", (void *)vr_ipc_get_proc_address))
		platform_log("vr: Meta's RuntimeIPCServiceClient_32.dll: its threads left unnamed (its calls to name them "
			"unbalance the stack)");
	else
		platform_log("vr: Meta's RuntimeIPCServiceClient_32.dll: its import of GetProcAddress not found");
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
	{
		vr_meta_ipc_client(text);
		return library;
	}
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
