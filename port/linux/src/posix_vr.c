/*
POSIX_VR.C

The headset's OpenXR runtime on Linux (vr_openxr.c): its manifest, found as
the OpenXR loader finds it (XR_RUNTIME_JSON, then active_runtime.i686.json
or active_runtime.json under openxr/1 in $XDG_CONFIG_HOME, each of
$XDG_CONFIG_DIRS and /etc), its library, and the session's binding to the
game's OpenGL context: EGL's (XR_MNDX_egl_enable) when SDL made the context
with EGL, otherwise GLX's. The game is 32-bit, so the runtime must be too.
*/

#ifndef HALO_GLES

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* the types the bindings name, as Xlib, GLX and EGL declare them */
typedef struct _XDisplay Display;
typedef struct __GLXFBConfigRec *GLXFBConfig;
typedef unsigned long GLXDrawable;
typedef struct __GLXcontextRec *GLXContext;
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void (*__eglMustCastToProperFunctionPointerType)(void);
typedef __eglMustCastToProperFunctionPointerType (*PFNEGLGETPROCADDRESSPROC)(const char *name);

#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_PLATFORM_XLIB
#define XR_USE_PLATFORM_EGL
#include "../../third_party/openxr/openxr/openxr.h"
#include "../../third_party/openxr/openxr/openxr_platform.h"

void platform_log(const char *format, ...);

/* GLX's and EGL's names, as their headers number them */
#define VR_GLX_FBCONFIG_ID 0x8013
#define VR_GLX_VISUAL_ID 0x800B
#define VR_GLX_SCREEN 0x800C
#define VR_EGL_CONFIG_ID 0x3028
#define VR_EGL_NONE 0x3038

static int vr_manifest_in(const char *folder, char *path, size_t size)
{
	static const char *const names[] = { "openxr/1/active_runtime.i686.json", "openxr/1/active_runtime.json" };
	int name;

	if (!folder || !*folder)
		return 0;
	for (name = 0; name < 2; name++)
	{
		if (snprintf(path, size, "%s/%s", folder, names[name]) < (int)size && access(path, R_OK) == 0)
			return 1;
	}
	return 0;
}

int vr_platform_runtime_manifest(char *path, size_t size)
{
	const char *chosen = getenv("XR_RUNTIME_JSON"), *home, *directories;
	char folder[1024];

	if (chosen && *chosen)
	{
		if (strlen(chosen) >= size)
			return 0;
		strcpy(path, chosen);
		platform_log("vr: XR_RUNTIME_JSON names the runtime");
		return 1;
	}
	home = getenv("XDG_CONFIG_HOME");
	if (home && *home)
	{
		if (vr_manifest_in(home, path, size))
			return 1;
	}
	else if ((home = getenv("HOME")) != NULL && snprintf(folder, sizeof(folder), "%s/.config", home) < (int)sizeof(folder) &&
		vr_manifest_in(folder, path, size))
	{
		return 1;
	}
	directories = getenv("XDG_CONFIG_DIRS");
	if (!directories || !*directories)
		directories = "/etc/xdg";
	while (*directories)
	{
		size_t length = strcspn(directories, ":");

		if (length && length < sizeof(folder))
		{
			memcpy(folder, directories, length);
			folder[length] = '\0';
			if (vr_manifest_in(folder, path, size))
				return 1;
		}
		directories += length;
		if (*directories == ':')
			directories++;
	}
	return vr_manifest_in("/etc", path, size);
}

void *vr_platform_library_open(const char *path)
{
	void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);

	if (!library)
		platform_log("vr: cannot load the OpenXR runtime (a 32-bit one is needed): %s", dlerror());
	return library;
}

void *vr_platform_library_symbol(void *library, const char *name)
{
	return dlsym(library, name);
}

/* a function of a library SDL has loaded already, or NULL */
static void *vr_loaded_symbol(const char *library_name, const char *name)
{
	void *library = dlopen(library_name, RTLD_NOW | RTLD_NOLOAD);

	return library ? dlsym(library, name) : NULL;
}

static XrGraphicsBindingEGLMNDX vr_egl_binding;
static XrGraphicsBindingOpenGLXlibKHR vr_glx_binding;

/* the binding with EGL, when the current context is EGL's */
static int vr_egl_current(void)
{
	EGLContext (*get_current_context)(void) = (EGLContext (*)(void))vr_loaded_symbol("libEGL.so.1",
		"eglGetCurrentContext");
	EGLDisplay (*get_current_display)(void) = (EGLDisplay (*)(void))vr_loaded_symbol("libEGL.so.1",
		"eglGetCurrentDisplay");
	unsigned (*query_context)(EGLDisplay, EGLContext, int, int *) =
		(unsigned (*)(EGLDisplay, EGLContext, int, int *))vr_loaded_symbol("libEGL.so.1", "eglQueryContext");
	unsigned (*choose_config)(EGLDisplay, const int *, EGLConfig *, int, int *) =
		(unsigned (*)(EGLDisplay, const int *, EGLConfig *, int, int *))vr_loaded_symbol("libEGL.so.1",
		"eglChooseConfig");
	PFNEGLGETPROCADDRESSPROC get_proc_address = (PFNEGLGETPROCADDRESSPROC)vr_loaded_symbol("libEGL.so.1",
		"eglGetProcAddress");
	EGLContext context;
	EGLDisplay display;
	EGLConfig config = NULL;
	int identifier = 0, count = 0, attributes[3];

	if (!get_current_context || !get_current_display || !query_context || !choose_config || !get_proc_address)
		return 0;
	context = get_current_context();
	display = get_current_display();
	if (!context || !display || !query_context(display, context, VR_EGL_CONFIG_ID, &identifier))
		return 0;
	attributes[0] = VR_EGL_CONFIG_ID;
	attributes[1] = identifier;
	attributes[2] = VR_EGL_NONE;
	if (!choose_config(display, attributes, &config, 1, &count) || count < 1)
		return 0;
	memset(&vr_egl_binding, 0, sizeof(vr_egl_binding));
	vr_egl_binding.type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX;
	vr_egl_binding.getProcAddress = (PFN_xrEglGetProcAddressMNDX)get_proc_address;
	vr_egl_binding.display = display;
	vr_egl_binding.config = config;
	vr_egl_binding.context = context;
	return 1;
}

static int vr_glx_current(void)
{
	GLXContext (*get_current_context)(void) = (GLXContext (*)(void))vr_loaded_symbol("libGL.so.1",
		"glXGetCurrentContext");
	Display *(*get_current_display)(void) = (Display *(*)(void))vr_loaded_symbol("libGL.so.1",
		"glXGetCurrentDisplay");
	GLXDrawable (*get_current_drawable)(void) = (GLXDrawable (*)(void))vr_loaded_symbol("libGL.so.1",
		"glXGetCurrentDrawable");
	int (*query_context)(Display *, GLXContext, int, int *) =
		(int (*)(Display *, GLXContext, int, int *))vr_loaded_symbol("libGL.so.1", "glXQueryContext");
	GLXFBConfig *(*choose_config)(Display *, int, const int *, int *) =
		(GLXFBConfig *(*)(Display *, int, const int *, int *))vr_loaded_symbol("libGL.so.1", "glXChooseFBConfig");
	int (*config_attribute)(Display *, GLXFBConfig, int, int *) =
		(int (*)(Display *, GLXFBConfig, int, int *))vr_loaded_symbol("libGL.so.1", "glXGetFBConfigAttrib");
	GLXContext context;
	Display *display;
	GLXFBConfig *configs;
	int identifier = 0, screen = 0, visual = 0, count = 0, attributes[3];

	if (!get_current_context || !get_current_display || !get_current_drawable || !query_context ||
		!choose_config || !config_attribute)
	{
		return 0;
	}
	context = get_current_context();
	display = get_current_display();
	if (!context || !display || query_context(display, context, VR_GLX_FBCONFIG_ID, &identifier) ||
		query_context(display, context, VR_GLX_SCREEN, &screen))
	{
		return 0;
	}
	attributes[0] = VR_GLX_FBCONFIG_ID;
	attributes[1] = identifier;
	attributes[2] = 0;
	configs = choose_config(display, screen, attributes, &count);
	if (!configs || count < 1)
		return 0;
	config_attribute(display, configs[0], VR_GLX_VISUAL_ID, &visual);
	memset(&vr_glx_binding, 0, sizeof(vr_glx_binding));
	vr_glx_binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR;
	vr_glx_binding.xDisplay = display;
	vr_glx_binding.visualid = (uint32_t)visual;
	vr_glx_binding.glxFBConfig = configs[0];
	vr_glx_binding.glxDrawable = get_current_drawable();
	vr_glx_binding.glxContext = context;
	/* (the configs' list is Xlib's to free, with XFree; one is kept) */
	return 1;
}

const char *vr_platform_graphics_extension(void)
{
	return vr_egl_current() ? XR_MNDX_EGL_ENABLE_EXTENSION_NAME : NULL;
}

const void *vr_platform_graphics_binding(void)
{
	if (vr_egl_current())
		return &vr_egl_binding;
	if (vr_glx_current())
		return &vr_glx_binding;
	return NULL;
}

#endif
