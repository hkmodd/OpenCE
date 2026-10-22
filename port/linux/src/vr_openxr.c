/*
VR_OPENXR.C

The headset, through OpenXR (display.vr, port/linux/include/halo_vr.h).

The runtime. The builds are 32-bit, so they need a 32-bit runtime (Windows
lists it apart, under WOW6432Node: Meta's PC runtime has one, SteamVR has
had one since 2.17). The runtime is loaded here rather than through the
Khronos loader, which is C++: its manifest is found as the loader finds it
(XR_RUNTIME_JSON, else the system's active runtime: posix_vr.c,
win32_vr.c), its library_path loaded, and xrNegotiateLoaderRuntimeInterface
gives its xrGetInstanceProcAddr, as the OpenXR loader specification
describes. API layers are not loaded. port/third_party/openxr has the
headers.

The session draws with OpenGL (XR_KHR_opengl_enable) on the game's own
context, on the thread that draws. Its images are SRGB8_ALPHA8, written
with the game's pixels as they are (they are gamma encoded already, and
GL_FRAMEBUFFER_SRGB stays off): the runtime reads them as sRGB.

A frame. vr_frame_begin, at the top of the main loop, waits for the runtime
(xrWaitFrame paces the game, in place of the window's vsync), begins the
runtime's frame and locates the eyes and the head at its predicted display
time. The game then draws the eyes (each copied into its image by
vr_resolve_eye as its window ends) and the HUD, and D3DDevice_Present hands
over the back buffer, now the HUD: it goes into the panel's image, and the
frame ends with a projection layer of the eyes and a quad of the HUD, held
ahead of the head. A frame without the eyes (the menus, a loading screen)
shows the back buffer on a quad held in the room. The window shows the left
eye (the mirror), and its swap no longer waits for the display.
*/

#ifndef HALO_GLES

/* (platform.h first, as sdl_platform.c has it: on Windows it settles the
Win32 names before SDL_opengl.h's windows.h) */
#include "platform.h"
#include "sdl_platform.h"
#include "gl.h"
#include "port_config.h"
#include "xgpu.h"
#include "halo_vr.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_OPENGL
#include "../../third_party/openxr/openxr/openxr.h"
#include "../../third_party/openxr/openxr/openxr_platform.h"
#include "../../third_party/openxr/openxr/openxr_loader_negotiation.h"

/* posix_vr.c, win32_vr.c: the active runtime's manifest (a UTF-8 path),
its library, and the session's binding to the current OpenGL context (an
XrGraphicsBinding*, or NULL) with the extension it needs besides
XR_KHR_opengl_enable (or NULL) */
int vr_platform_runtime_manifest(char *path, size_t size);
void *vr_platform_library_open(const char *path);
void *vr_platform_library_symbol(void *library, const char *name);
const char *vr_platform_graphics_extension(void);
const void *vr_platform_graphics_binding(void);

/* ---------- constants */

/* the image formats, as OpenGL numbers them */
#define VR_FORMAT_SRGB8_ALPHA8 0x8C43
#define VR_FORMAT_RGBA8 0x8058

/* the HUD's and the flat screen's image */
#define VR_PANEL_WIDTH 1280
#define VR_PANEL_HEIGHT 960

#define VR_SWAPCHAIN_IMAGES 8

/* ---------- the runtime's functions */

#define VR_GLOBAL_FUNCTIONS(F) \
	F(xrEnumerateInstanceExtensionProperties) \
	F(xrCreateInstance)

#define VR_INSTANCE_FUNCTIONS(F) \
	F(xrDestroyInstance) \
	F(xrGetInstanceProperties) \
	F(xrResultToString) \
	F(xrPollEvent) \
	F(xrGetSystem) \
	F(xrGetSystemProperties) \
	F(xrEnumerateViewConfigurationViews) \
	F(xrGetOpenGLGraphicsRequirementsKHR) \
	F(xrCreateSession) \
	F(xrDestroySession) \
	F(xrBeginSession) \
	F(xrEndSession) \
	F(xrRequestExitSession) \
	F(xrCreateReferenceSpace) \
	F(xrDestroySpace) \
	F(xrLocateSpace) \
	F(xrLocateViews) \
	F(xrEnumerateSwapchainFormats) \
	F(xrCreateSwapchain) \
	F(xrDestroySwapchain) \
	F(xrEnumerateSwapchainImages) \
	F(xrAcquireSwapchainImage) \
	F(xrWaitSwapchainImage) \
	F(xrReleaseSwapchainImage) \
	F(xrWaitFrame) \
	F(xrBeginFrame) \
	F(xrEndFrame)

#define VR_DECLARE(name) static PFN_##name name;
VR_GLOBAL_FUNCTIONS(VR_DECLARE)
VR_INSTANCE_FUNCTIONS(VR_DECLARE)
#undef VR_DECLARE

/* ---------- structures */

struct vr_swapchain
{
	XrSwapchain handle;
	long width, height;
	unsigned long image_count;
	GLuint images[VR_SWAPCHAIN_IMAGES];
	int acquired;
	unsigned long index;
};

static struct
{
	void *library;
	PFN_xrGetInstanceProcAddr get_instance_proc_address;
	XrInstance instance;
	XrSystemId system;
	XrSession session;
	XrSpace local, view;
	XrSessionState state;
	int running, exiting, lost;
	int64_t format;

	struct vr_swapchain eyes[2], panel;
	GLuint framebuffers[2];
	GLuint mirror;
	long mirror_width, mirror_height;
	GLuint panel_program, vertex_array;

	/* this frame */
	XrFrameState frame_state;
	int frame_begun, eyes_drawn[2];
	XrView views[2];
	struct vr_frame frame;
	/* the last frame's display time, for vr_frame_seconds */
	XrTime last_display_time;
	double display_seconds;
	/* the head's last tracked position (kept while tracking has only the
	head's turn) */
	float positions[3][3];
	int positions_valid;
	unsigned long frames, frames_without_eyes;
} vr;

static int vr_failed;

/* ---------- the manifest */

/* the string value of "key" in a JSON document (a runtime manifest: its
runtime.name and runtime.library_path), decoded into UTF-8; 1 if found and
it fits */
static int vr_manifest_string(const char *text, const char *key, char *out, size_t size)
{
	size_t key_length = strlen(key), used = 0;
	const char *at = text;

	if (!size)
		return 0;
	while ((at = strstr(at, key)) != NULL)
	{
		const char *value = at + key_length;

		if (at == text || at[-1] != '"' || *value != '"')
		{
			at++;
			continue;
		}
		value++;
		while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
			value++;
		if (*value != ':')
		{
			at++;
			continue;
		}
		value++;
		while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
			value++;
		if (*value != '"')
			return 0;
		value++;
		while (*value && *value != '"')
		{
			unsigned long code = (unsigned char)*value++;

			/* (the text's own bytes, UTF-8 already, pass as they are) */
			if (code == '\\')
			{
				char escaped = *value++;

				switch (escaped)
				{
				case '"': case '\\': case '/': code = (unsigned char)escaped; break;
				case 'b': code = '\b'; break;
				case 'f': code = '\f'; break;
				case 'n': code = '\n'; break;
				case 'r': code = '\r'; break;
				case 't': code = '\t'; break;
				case 'u':
				{
					int digit;

					code = 0;
					for (digit = 0; digit < 4; digit++)
					{
						char c = *value++;

						code <<= 4;
						if (c >= '0' && c <= '9')
							code |= (unsigned long)(c - '0');
						else if (c >= 'a' && c <= 'f')
							code |= (unsigned long)(c - 'a' + 10);
						else if (c >= 'A' && c <= 'F')
							code |= (unsigned long)(c - 'A' + 10);
						else
							return 0;
					}
					/* (a surrogate pair's halves are not paths' characters
					a manifest needs) */
					if (!code || (code >= 0xD800 && code <= 0xDFFF))
						return 0;
					/* the code point in UTF-8 */
					if (code >= 0x800)
					{
						if (used + 3 >= size)
							return 0;
						out[used++] = (char)(0xE0 | (code >> 12));
						out[used++] = (char)(0x80 | ((code >> 6) & 0x3F));
						code = 0x80 | (code & 0x3F);
					}
					else if (code >= 0x80)
					{
						if (used + 2 >= size)
							return 0;
						out[used++] = (char)(0xC0 | (code >> 6));
						code = 0x80 | (code & 0x3F);
					}
					break;
				}
				default:
					return 0;
				}
			}
			if (used + 1 >= size)
				return 0;
			out[used++] = (char)code;
		}
		if (*value != '"')
			return 0;
		out[used] = '\0';
		return 1;
	}
	return 0;
}

/* the runtime library's path from its manifest's: a bare file name is
looked for as the system looks for libraries; any other relative path is
from the manifest's folder. 1 if it fits */
static int vr_manifest_library_path(const char *manifest, const char *library, char *out, size_t size)
{
	const char *separator = NULL, *at;
	size_t folder;
	int absolute = library[0] == '/' || library[0] == '\\' ||
		(((library[0] | 0x20) >= 'a' && (library[0] | 0x20) <= 'z') && library[1] == ':');

	if (absolute || (!strchr(library, '/') && !strchr(library, '\\')))
	{
		if (strlen(library) >= size)
			return 0;
		strcpy(out, library);
		return 1;
	}
	for (at = manifest; *at; at++)
	{
		if (*at == '/' || *at == '\\')
			separator = at;
	}
	folder = separator ? (size_t)(separator - manifest) + 1 : 0;
	if (folder + strlen(library) >= size)
		return 0;
	memcpy(out, manifest, folder);
	strcpy(out + folder, library);
	return 1;
}

/* ---------- the runtime */

static const char *vr_result(XrResult result)
{
	static char text[XR_MAX_RESULT_STRING_SIZE];

	if (vr.instance != XR_NULL_HANDLE && xrResultToString &&
		XR_SUCCEEDED(xrResultToString(vr.instance, result, text)))
	{
		return text;
	}
	snprintf(text, sizeof(text), "XrResult %d", (int)result);
	return text;
}

static int vr_check(XrResult result, const char *what)
{
	if (XR_SUCCEEDED(result))
		return 1;
	platform_log("vr: %s failed: %s", what, vr_result(result));
	return 0;
}

/* the runtime's library and its xrGetInstanceProcAddr */
static int vr_load_runtime(void)
{
	char manifest_path[1024], library_path[1024], library[1024], name[256];
	char *manifest;
	size_t manifest_size;
	PFN_xrNegotiateLoaderRuntimeInterface negotiate;

	if (!vr_platform_runtime_manifest(manifest_path, sizeof(manifest_path)))
	{
		platform_log("vr: no OpenXR runtime is installed for 32-bit programs (refer to \"VR\" in port/linux/README.md)");
		return 0;
	}
	manifest = config_file_read(manifest_path, &manifest_size);
	if (!manifest)
	{
		platform_log("vr: cannot read the OpenXR runtime's manifest %s", manifest_path);
		return 0;
	}
	if (!vr_manifest_string(manifest, "name", name, sizeof(name)))
		strcpy(name, "an unnamed runtime");
	if (!vr_manifest_string(manifest, "library_path", library, sizeof(library)) ||
		!vr_manifest_library_path(manifest_path, library, library_path, sizeof(library_path)))
	{
		platform_log("vr: the OpenXR runtime's manifest %s names no library", manifest_path);
		free(manifest);
		return 0;
	}
	free(manifest);
	platform_log("vr: OpenXR runtime %s (%s)", name, library_path);
	vr.library = vr_platform_library_open(library_path);
	if (!vr.library)
		return 0;
	negotiate = (PFN_xrNegotiateLoaderRuntimeInterface)vr_platform_library_symbol(vr.library,
		"xrNegotiateLoaderRuntimeInterface");
	if (negotiate)
	{
		XrNegotiateLoaderInfo loader;
		XrNegotiateRuntimeRequest request;

		memset(&loader, 0, sizeof(loader));
		memset(&request, 0, sizeof(request));
		loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
		loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
		loader.structSize = sizeof(loader);
		loader.minInterfaceVersion = 1;
		loader.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
		loader.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
		loader.maxApiVersion = XR_MAKE_VERSION(1, 0x3FF, 0xFFF);
		request.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
		request.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
		request.structSize = sizeof(request);
		if (!XR_SUCCEEDED(negotiate(&loader, &request)) || !request.getInstanceProcAddr)
		{
			platform_log("vr: the OpenXR runtime refused the loader's interface");
			return 0;
		}
		vr.get_instance_proc_address = request.getInstanceProcAddr;
	}
	else
	{
		/* (runtimes from before the negotiation export it themselves) */
		vr.get_instance_proc_address = (PFN_xrGetInstanceProcAddr)vr_platform_library_symbol(vr.library,
			"xrGetInstanceProcAddr");
	}
	if (!vr.get_instance_proc_address)
	{
		platform_log("vr: the OpenXR runtime has no xrGetInstanceProcAddr");
		return 0;
	}
#define VR_RESOLVE(name) \
	if (!XR_SUCCEEDED(vr.get_instance_proc_address(XR_NULL_HANDLE, #name, (PFN_xrVoidFunction *)&name)) || !name) \
	{ \
		platform_log("vr: the OpenXR runtime has no " #name); \
		return 0; \
	}
	VR_GLOBAL_FUNCTIONS(VR_RESOLVE)
#undef VR_RESOLVE
	return 1;
}

static int vr_create_instance(void)
{
	XrExtensionProperties *extensions;
	uint32_t count = 0, index;
	int opengl = 0, binding = 0;
	const char *enabled[2], *binding_extension = vr_platform_graphics_extension();
	XrInstanceCreateInfo create;
	XrInstanceProperties properties;
	XrResult result;

	if (!vr_check(xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL), "listing the extensions"))
		return 0;
	extensions = calloc(count ? count : 1, sizeof(*extensions));
	if (!extensions)
		return 0;
	for (index = 0; index < count; index++)
		extensions[index].type = XR_TYPE_EXTENSION_PROPERTIES;
	if (!vr_check(xrEnumerateInstanceExtensionProperties(NULL, count, &count, extensions), "listing the extensions"))
	{
		free(extensions);
		return 0;
	}
	for (index = 0; index < count; index++)
	{
		if (!strcmp(extensions[index].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
			opengl = 1;
		if (binding_extension && !strcmp(extensions[index].extensionName, binding_extension))
			binding = 1;
	}
	free(extensions);
	if (!opengl)
	{
		platform_log("vr: the OpenXR runtime cannot draw with OpenGL (XR_KHR_opengl_enable): "
			"refer to \"VR\" in port/linux/README.md");
		return 0;
	}
	if (binding_extension && !binding)
	{
		platform_log("vr: the OpenGL context is EGL's, and the OpenXR runtime has no %s", binding_extension);
		return 0;
	}
	enabled[0] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
	enabled[1] = binding_extension;
	memset(&create, 0, sizeof(create));
	create.type = XR_TYPE_INSTANCE_CREATE_INFO;
	strcpy(create.applicationInfo.applicationName, "OpenCE");
	create.applicationInfo.applicationVersion = 1;
	strcpy(create.applicationInfo.engineName, "OpenCE");
	create.applicationInfo.engineVersion = 1;
	create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	create.enabledExtensionCount = binding_extension ? 2 : 1;
	create.enabledExtensionNames = enabled;
	result = xrCreateInstance(&create, &vr.instance);
	if (result == XR_ERROR_RUNTIME_UNAVAILABLE)
	{
		platform_log("vr: the OpenXR runtime is not running (start Meta's app or SteamVR first)");
		return 0;
	}
	if (!vr_check(result, "xrCreateInstance"))
		return 0;
#define VR_RESOLVE(name) \
	if (!XR_SUCCEEDED(vr.get_instance_proc_address(vr.instance, #name, (PFN_xrVoidFunction *)&name)) || !name) \
	{ \
		platform_log("vr: the OpenXR runtime has no " #name); \
		return 0; \
	}
	VR_INSTANCE_FUNCTIONS(VR_RESOLVE)
#undef VR_RESOLVE
	memset(&properties, 0, sizeof(properties));
	properties.type = XR_TYPE_INSTANCE_PROPERTIES;
	if (XR_SUCCEEDED(xrGetInstanceProperties(vr.instance, &properties)))
	{
		platform_log("vr: %s %u.%u.%u", properties.runtimeName, XR_VERSION_MAJOR(properties.runtimeVersion),
			XR_VERSION_MINOR(properties.runtimeVersion), XR_VERSION_PATCH(properties.runtimeVersion));
	}
	return 1;
}

static int vr_create_swapchain(struct vr_swapchain *swapchain, long width, long height)
{
	XrSwapchainCreateInfo create;
	XrSwapchainImageOpenGLKHR images[VR_SWAPCHAIN_IMAGES];
	uint32_t count = 0, index;

	memset(&create, 0, sizeof(create));
	create.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	create.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	create.format = vr.format;
	create.sampleCount = 1;
	create.width = (uint32_t)width;
	create.height = (uint32_t)height;
	create.faceCount = 1;
	create.arraySize = 1;
	create.mipCount = 1;
	if (!vr_check(xrCreateSwapchain(vr.session, &create, &swapchain->handle), "xrCreateSwapchain") ||
		!vr_check(xrEnumerateSwapchainImages(swapchain->handle, 0, &count, NULL), "listing a swapchain's images"))
	{
		return 0;
	}
	if (count > VR_SWAPCHAIN_IMAGES)
	{
		platform_log("vr: a swapchain of %u images, more than %d", count, VR_SWAPCHAIN_IMAGES);
		return 0;
	}
	for (index = 0; index < count; index++)
	{
		memset(&images[index], 0, sizeof(images[index]));
		images[index].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
	}
	if (!vr_check(xrEnumerateSwapchainImages(swapchain->handle, count, &count,
		(XrSwapchainImageBaseHeader *)images), "listing a swapchain's images"))
	{
		return 0;
	}
	for (index = 0; index < count; index++)
		swapchain->images[index] = images[index].image;
	swapchain->image_count = count;
	swapchain->width = width;
	swapchain->height = height;
	return 1;
}

/* the panel's program: the HUD over a clear ground, its alpha made from its
brightness (the game draws the HUD's colours over the world as light; over
black, how bright a pixel is says how much of it there is) */
static GLuint vr_panel_program(void)
{
	static const char *const vertex_text =
		"#version 330 core\n"
		"out vec2 position;\n"
		"void main()\n"
		"{\n"
		"	vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
		"	position = corner;\n"
		"	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";
	static const char *const fragment_text =
		"#version 330 core\n"
		"uniform sampler2D picture;\n"
		"in vec2 position;\n"
		"out vec4 colour;\n"
		"void main()\n"
		"{\n"
		"	vec3 c = texture(picture, vec2(position.x, 1.0 - position.y)).rgb;\n"
		"	colour = vec4(c, max(c.r, max(c.g, c.b)));\n"
		"}\n";
	const char *const texts[2] = { vertex_text, fragment_text };
	const GLenum types[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	GLuint program = glCreateProgram(), shaders[2];
	GLint status = 0;
	int index;

	for (index = 0; index < 2; index++)
	{
		shaders[index] = glCreateShader(types[index]);
		glShaderSource(shaders[index], 1, &texts[index], NULL);
		glCompileShader(shaders[index]);
		glGetShaderiv(shaders[index], GL_COMPILE_STATUS, &status);
		if (!status)
		{
			char log[512];

			glGetShaderInfoLog(shaders[index], sizeof(log), NULL, log);
			platform_log("vr: the panel's shader: %s", log);
		}
		glAttachShader(program, shaders[index]);
	}
	glLinkProgram(program);
	for (index = 0; index < 2; index++)
		glDeleteShader(shaders[index]);
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status)
	{
		platform_log("vr: the panel's program does not link");
		return 0;
	}
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "picture"), 0);
	glUseProgram(0);
	return program;
}

static int vr_create_session(void)
{
	XrSystemGetInfo system_info;
	XrSystemProperties properties;
	XrGraphicsRequirementsOpenGLKHR requirements;
	XrSessionCreateInfo create;
	XrReferenceSpaceCreateInfo space;
	XrViewConfigurationView views[2];
	int64_t formats[64];
	uint32_t count = 0, index;
	long width, height, maximum_width, maximum_height;
	double scale;
	GLint major = 0, minor = 0;
	const void *binding;
	XrResult result;
	int eye;

	memset(&system_info, 0, sizeof(system_info));
	system_info.type = XR_TYPE_SYSTEM_GET_INFO;
	system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	result = xrGetSystem(vr.instance, &system_info, &vr.system);
	if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE)
	{
		platform_log("vr: the OpenXR runtime finds no headset (is it plugged in, and its service running?)");
		return 0;
	}
	if (!vr_check(result, "finding the headset (xrGetSystem)"))
		return 0;
	memset(&properties, 0, sizeof(properties));
	properties.type = XR_TYPE_SYSTEM_PROPERTIES;
	if (XR_SUCCEEDED(xrGetSystemProperties(vr.instance, vr.system, &properties)))
		platform_log("vr: headset %s", properties.systemName);

	/* (asked for before a session can be made, whether or not it is met) */
	memset(&requirements, 0, sizeof(requirements));
	requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
	if (!vr_check(xrGetOpenGLGraphicsRequirementsKHR(vr.instance, vr.system, &requirements),
		"xrGetOpenGLGraphicsRequirementsKHR"))
	{
		return 0;
	}
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	if (XR_MAKE_VERSION(major, minor, 0) < requirements.minApiVersionSupported)
	{
		platform_log("vr: the runtime needs OpenGL %u.%u", XR_VERSION_MAJOR(requirements.minApiVersionSupported),
			XR_VERSION_MINOR(requirements.minApiVersionSupported));
		return 0;
	}

	binding = vr_platform_graphics_binding();
	if (!binding)
	{
		platform_log("vr: no OpenGL context the runtime can take");
		return 0;
	}
	memset(&create, 0, sizeof(create));
	create.type = XR_TYPE_SESSION_CREATE_INFO;
	create.next = binding;
	create.systemId = vr.system;
	if (!vr_check(xrCreateSession(vr.instance, &create, &vr.session), "xrCreateSession"))
		return 0;

	memset(&space, 0, sizeof(space));
	space.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	space.poseInReferenceSpace.orientation.w = 1.0f;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (!vr_check(xrCreateReferenceSpace(vr.session, &space, &vr.local), "creating the LOCAL space"))
		return 0;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if (!vr_check(xrCreateReferenceSpace(vr.session, &space, &vr.view), "creating the VIEW space"))
		return 0;

	/* the image format: sRGB, as the game's pixels are */
	if (!vr_check(xrEnumerateSwapchainFormats(vr.session, 0, &count, NULL), "listing the image formats"))
		return 0;
	if (count > 64)
		count = 64;
	if (!vr_check(xrEnumerateSwapchainFormats(vr.session, count, &count, formats), "listing the image formats"))
		return 0;
	vr.format = 0;
	for (index = 0; index < count; index++)
	{
		if (formats[index] == VR_FORMAT_SRGB8_ALPHA8)
			vr.format = VR_FORMAT_SRGB8_ALPHA8;
		else if (formats[index] == VR_FORMAT_RGBA8 && !vr.format)
			vr.format = VR_FORMAT_RGBA8;
	}
	if (!vr.format)
	{
		platform_log("vr: the runtime offers no 8-bit RGBA images");
		return 0;
	}
	if (vr.format != VR_FORMAT_SRGB8_ALPHA8)
		platform_log("vr: no sRGB images: the picture will look pale");

	/* the eyes' size: the runtime's recommended, times vr.resolution_scale */
	memset(views, 0, sizeof(views));
	views[0].type = views[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	if (!vr_check(xrEnumerateViewConfigurationViews(vr.instance, vr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		2, &count, views), "listing the eyes") || count != 2)
	{
		return 0;
	}
	scale = config_real("vr.resolution_scale");
	if (!(scale >= 0.5 && scale <= 2.0))
		scale = 1.0;
	width = (long)(views[0].recommendedImageRectWidth * scale + 0.5);
	height = (long)(views[0].recommendedImageRectHeight * scale + 0.5);
	maximum_width = (long)views[0].maxImageRectWidth;
	maximum_height = (long)views[0].maxImageRectHeight;
	if (width > maximum_width)
		width = maximum_width;
	if (height > maximum_height)
		height = maximum_height;
	/* (the game's 640x480 screen at the least) */
	if (width < 640)
		width = 640;
	if (height < 480)
		height = 480;
	for (eye = 0; eye < 2; eye++)
	{
		if (!vr_create_swapchain(&vr.eyes[eye], width, height))
			return 0;
	}
	if (!vr_create_swapchain(&vr.panel, VR_PANEL_WIDTH, VR_PANEL_HEIGHT))
		return 0;
	platform_log("vr: eyes %ldx%ld (recommended %ux%u, vr.resolution_scale %.2f), %u images", width, height,
		views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight, scale,
		(unsigned)vr.eyes[0].image_count);

	glGenFramebuffers(2, vr.framebuffers);
	glGenVertexArrays(1, &vr.vertex_array);
	glGenTextures(1, &vr.mirror);
	glBindTexture(GL_TEXTURE_2D, vr.mirror);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	vr.mirror_width = width;
	vr.mirror_height = height;
	vr.panel_program = vr_panel_program();
	xgpu_gl_state_invalidate();
	return vr.panel_program != 0;
}

void vr_initialize(void)
{
	if (!config_boolean("display.vr") || vr.instance != XR_NULL_HANDLE || vr_failed)
		return;
	if (!vr_load_runtime() || !vr_create_instance() || !vr_create_session())
	{
		platform_log("vr: the headset is not used; the game is drawn on the display");
		vr_dispose();
		vr_failed = 1;
		return;
	}
	platform_log("vr: session made; waiting for the runtime to start it");
}

void vr_dispose(void)
{
	void *library = vr.library;
	PFN_xrGetInstanceProcAddr get_instance_proc_address = vr.get_instance_proc_address;
	int eye;

	if (vr.session != XR_NULL_HANDLE)
	{
		for (eye = 0; eye < 2; eye++)
		{
			if (vr.eyes[eye].handle != XR_NULL_HANDLE)
				xrDestroySwapchain(vr.eyes[eye].handle);
		}
		if (vr.panel.handle != XR_NULL_HANDLE)
			xrDestroySwapchain(vr.panel.handle);
		if (vr.local != XR_NULL_HANDLE)
			xrDestroySpace(vr.local);
		if (vr.view != XR_NULL_HANDLE)
			xrDestroySpace(vr.view);
		xrDestroySession(vr.session);
	}
	if (vr.instance != XR_NULL_HANDLE && xrDestroyInstance)
		xrDestroyInstance(vr.instance);
	/* (the vertex array and the program are a few bytes, left to the
	context) */
	if (vr.framebuffers[0])
	{
		glDeleteFramebuffers(2, vr.framebuffers);
		glDeleteTextures(1, &vr.mirror);
		xgpu_gl_state_invalidate();
	}
	/* (the runtime's library stays loaded: runtimes leave threads behind) */
	memset(&vr, 0, sizeof(vr));
	vr.library = library;
	vr.get_instance_proc_address = get_instance_proc_address;
}

int vr_active(void)
{
	return vr.running;
}

/* ---------- the session's states */

static void vr_session_state(XrSessionState state)
{
	static const char *const names[] =
	{
		"unknown", "idle", "ready", "synchronized", "visible", "focused", "stopping", "loss pending", "exiting"
	};

	platform_log("vr: session %s", state >= 0 && state <= XR_SESSION_STATE_EXITING ? names[state] : "?");
	vr.state = state;
	switch (state)
	{
	case XR_SESSION_STATE_READY:
	{
		XrSessionBeginInfo begin;

		memset(&begin, 0, sizeof(begin));
		begin.type = XR_TYPE_SESSION_BEGIN_INFO;
		begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		if (vr_check(xrBeginSession(vr.session, &begin), "xrBeginSession"))
			vr.running = 1;
		break;
	}
	case XR_SESSION_STATE_STOPPING:
		vr_check(xrEndSession(vr.session), "xrEndSession");
		vr.running = 0;
		break;
	case XR_SESSION_STATE_LOSS_PENDING:
		/* the runtime or the headset went away: the game goes on, on the
		display (vr_frame_begin lets the session go) */
		vr.running = 0;
		vr.lost = 1;
		break;
	case XR_SESSION_STATE_EXITING:
		vr.running = 0;
		if (!vr.exiting)
		{
			SDL_Event quit;

			/* the runtime's own menu quit the game: quit as the window's
			close button does */
			vr.exiting = 1;
			platform_log("vr: the runtime ended the session: quitting");
			memset(&quit, 0, sizeof(quit));
			quit.type = SDL_EVENT_QUIT;
			SDL_PushEvent(&quit);
		}
		break;
	default:
		break;
	}
}

static void vr_poll_events(void)
{
	XrEventDataBuffer event;

	for (;;)
	{
		memset(&event, 0, sizeof(event));
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
		if (xrPollEvent(vr.instance, &event) != XR_SUCCESS)
			break;
		switch (event.type)
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			vr_session_state(((const XrEventDataSessionStateChanged *)&event)->state);
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			vr_session_state(XR_SESSION_STATE_LOSS_PENDING);
			break;
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			if (((const XrEventDataReferenceSpaceChangePending *)&event)->referenceSpaceType ==
				XR_REFERENCE_SPACE_TYPE_LOCAL)
			{
				vr.frame.recentres++;
				platform_log("vr: recentred");
			}
			break;
		default:
			break;
		}
	}
}

/* ---------- frames */

static void vr_pose_of(const XrPosef *pose, struct vr_pose *out)
{
	out->position[0] = pose->position.x;
	out->position[1] = pose->position.y;
	out->position[2] = pose->position.z;
	out->orientation[0] = pose->orientation.x;
	out->orientation[1] = pose->orientation.y;
	out->orientation[2] = pose->orientation.z;
	out->orientation[3] = pose->orientation.w;
}

static void vr_end_frame(const XrCompositionLayerBaseHeader *const *layers, uint32_t layer_count)
{
	XrFrameEndInfo end;

	memset(&end, 0, sizeof(end));
	end.type = XR_TYPE_FRAME_END_INFO;
	end.displayTime = vr.frame_state.predictedDisplayTime;
	end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end.layerCount = layer_count;
	end.layers = layers;
	vr_check(xrEndFrame(vr.session, &end), "xrEndFrame");
	vr.frame_begun = 0;
}

int vr_frame_begin(void)
{
	XrFrameWaitInfo wait;
	XrFrameBeginInfo begin;
	XrViewLocateInfo locate;
	XrViewState view_state;
	XrSpaceLocation head;
	uint32_t count = 0;
	int eye, positions = 0;

	vr.frame.stereo = 0;
	vr.display_seconds = 0.0;
	if (vr.instance == XR_NULL_HANDLE)
		return 0;
	vr_poll_events();
	if (vr.lost)
	{
		platform_log("vr: the runtime lost the headset; the game is drawn on the display");
		vr_dispose();
		vr_failed = 1;
		return 0;
	}
	if (vr.frame_begun)
		vr_end_frame(NULL, 0);
	if (!vr.running)
		return 0;

	memset(&wait, 0, sizeof(wait));
	wait.type = XR_TYPE_FRAME_WAIT_INFO;
	memset(&vr.frame_state, 0, sizeof(vr.frame_state));
	vr.frame_state.type = XR_TYPE_FRAME_STATE;
	if (!vr_check(xrWaitFrame(vr.session, &wait, &vr.frame_state), "xrWaitFrame"))
		return 0;
	memset(&begin, 0, sizeof(begin));
	begin.type = XR_TYPE_FRAME_BEGIN_INFO;
	if (!vr_check(xrBeginFrame(vr.session, &begin), "xrBeginFrame"))
		return 0;
	vr.frame_begun = 1;
	vr.eyes_drawn[0] = vr.eyes_drawn[1] = 0;
	vr.frames++;
	if (vr.last_display_time && vr.frame_state.predictedDisplayTime > vr.last_display_time)
		vr.display_seconds = (double)(vr.frame_state.predictedDisplayTime - vr.last_display_time) * 1e-9;
	vr.last_display_time = vr.frame_state.predictedDisplayTime;
	vr.frame.display_period = (float)((double)vr.frame_state.predictedDisplayPeriod * 1e-9);
	vr.frame.focused = vr.state == XR_SESSION_STATE_FOCUSED;
	if (!vr.frame_state.shouldRender)
		return 0;

	memset(&locate, 0, sizeof(locate));
	locate.type = XR_TYPE_VIEW_LOCATE_INFO;
	locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate.displayTime = vr.frame_state.predictedDisplayTime;
	locate.space = vr.local;
	memset(&view_state, 0, sizeof(view_state));
	view_state.type = XR_TYPE_VIEW_STATE;
	memset(vr.views, 0, sizeof(vr.views));
	vr.views[0].type = vr.views[1].type = XR_TYPE_VIEW;
	if (!XR_SUCCEEDED(xrLocateViews(vr.session, &locate, &view_state, 2, &count, vr.views)) || count != 2 ||
		!(view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
	{
		return 0;
	}
	memset(&head, 0, sizeof(head));
	head.type = XR_TYPE_SPACE_LOCATION;
	if (!XR_SUCCEEDED(xrLocateSpace(vr.view, vr.local, vr.frame_state.predictedDisplayTime, &head)) ||
		!(head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
	{
		return 0;
	}
	vr_pose_of(&head.pose, &vr.frame.head);
	for (eye = 0; eye < 2; eye++)
	{
		vr_pose_of(&vr.views[eye].pose, &vr.frame.eye[eye]);
		vr.frame.fov[eye].left = vr.views[eye].fov.angleLeft;
		vr.frame.fov[eye].right = vr.views[eye].fov.angleRight;
		vr.frame.fov[eye].up = vr.views[eye].fov.angleUp;
		vr.frame.fov[eye].down = vr.views[eye].fov.angleDown;
	}
	/* tracking that has only the head's turn (a Rift's sensors out of
	sight) keeps the last positions: the eyes' offsets from the head stay
	the turned IPD */
	positions = (view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
		(head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
	if (positions)
	{
		memcpy(vr.positions[0], vr.frame.head.position, sizeof(vr.positions[0]));
		memcpy(vr.positions[1], vr.frame.eye[0].position, sizeof(vr.positions[1]));
		memcpy(vr.positions[2], vr.frame.eye[1].position, sizeof(vr.positions[2]));
		vr.positions_valid = 1;
	}
	else if (vr.positions_valid)
	{
		memcpy(vr.frame.head.position, vr.positions[0], sizeof(vr.positions[0]));
		memcpy(vr.frame.eye[0].position, vr.positions[1], sizeof(vr.positions[1]));
		memcpy(vr.frame.eye[1].position, vr.positions[2], sizeof(vr.positions[2]));
	}
	if (!vr_finite(vr.frame.head.position, 3) || !vr_finite(vr.frame.head.orientation, 4) ||
		!vr_finite(vr.frame.eye[0].position, 3) || !vr_finite(vr.frame.eye[1].position, 3) ||
		!vr_finite(&vr.frame.fov[0].left, 4) || !vr_finite(&vr.frame.fov[1].left, 4))
	{
		return 0;
	}
	vr.frame.stereo = 1;
	return 1;
}

struct vr_frame const *vr_frame_get(void)
{
	return &vr.frame;
}

int vr_frame_seconds(double *seconds)
{
	if (!vr.running || vr.display_seconds <= 0.0)
		return 0;
	*seconds = vr.display_seconds;
	return 1;
}

int vr_screen_size(long *width, long *height)
{
	/* (while the runtime runs the session: else the window's own) */
	if (!vr.running || !vr.eyes[0].width)
		return 0;
	*width = vr.eyes[0].width;
	*height = vr.eyes[0].height;
	return 1;
}

static GLuint vr_acquire(struct vr_swapchain *swapchain)
{
	XrSwapchainImageAcquireInfo acquire;
	XrSwapchainImageWaitInfo wait;
	uint32_t index = 0;

	memset(&acquire, 0, sizeof(acquire));
	acquire.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
	if (!vr_check(xrAcquireSwapchainImage(swapchain->handle, &acquire, &index), "xrAcquireSwapchainImage") ||
		index >= swapchain->image_count)
	{
		return 0;
	}
	memset(&wait, 0, sizeof(wait));
	wait.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	wait.timeout = XR_INFINITE_DURATION;
	if (!vr_check(xrWaitSwapchainImage(swapchain->handle, &wait), "xrWaitSwapchainImage"))
		return 0;
	swapchain->acquired = 1;
	swapchain->index = index;
	return swapchain->images[index];
}

static void vr_release(struct vr_swapchain *swapchain)
{
	XrSwapchainImageReleaseInfo release;

	if (!swapchain->acquired)
		return;
	memset(&release, 0, sizeof(release));
	release.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
	vr_check(xrReleaseSwapchainImage(swapchain->handle, &release), "xrReleaseSwapchainImage");
	swapchain->acquired = 0;
}

/* the picture (a texture whose rows are from the top) into a texture
whose rows are from the bottom, as OpenGL's and the runtime's are */
static void vr_copy(GLuint source, long source_width, long source_height, GLuint destination, long width, long height)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, vr.framebuffers[0]);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, vr.framebuffers[1]);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, destination, 0);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBlitFramebuffer(0, 0, (GLint)source_width, (GLint)source_height, 0, (GLint)height, (GLint)width, 0,
		GL_COLOR_BUFFER_BIT, source_width == width && source_height == height ? GL_NEAREST : GL_LINEAR);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
}

void vr_resolve_eye(int eye, unsigned int texture, int width, int height)
{
	GLuint image;

	if (!vr.frame_begun || !vr.frame.stereo || eye < 0 || eye > 1)
		return;
	image = vr_acquire(&vr.eyes[eye]);
	if (image)
	{
		vr_copy(texture, width, height, image, vr.eyes[eye].width, vr.eyes[eye].height);
		vr_release(&vr.eyes[eye]);
		vr.eyes_drawn[eye] = 1;
	}
	/* the window's mirror: the left eye */
	if (eye == 0)
		vr_copy(texture, width, height, vr.mirror, vr.mirror_width, vr.mirror_height);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

/* the HUD's panel: its picture over a clear ground */
static void vr_draw_panel(GLuint source, GLuint destination)
{
	glBindFramebuffer(GL_FRAMEBUFFER, vr.framebuffers[1]);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, destination, 0);
	glViewport(0, 0, VR_PANEL_WIDTH, VR_PANEL_HEIGHT);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(vr.panel_program);
	glBindVertexArray(vr.vertex_array);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, source);
	glBindSampler(0, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* the mirror (the left eye) over the whole window, cut to its shape */
static void vr_draw_mirror(void)
{
	int window_width, window_height;
	long width = vr.mirror_width, height = vr.mirror_height, x = 0, y = 0;

	platform_video_drawable_size(&window_width, &window_height);
	if (window_width <= 0 || window_height <= 0)
		return;
	if (width * window_height > height * window_width)
	{
		width = height * window_width / window_height;
		x = (vr.mirror_width - width) / 2;
	}
	else
	{
		height = width * window_height / window_width;
		y = (vr.mirror_height - height) / 2;
	}
	glBindFramebuffer(GL_READ_FRAMEBUFFER, vr.framebuffers[0]);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vr.mirror, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBlitFramebuffer((GLint)x, (GLint)y, (GLint)(x + width), (GLint)(y + height), 0, 0, window_width, window_height,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

/* a frame without the eyes in the window: the back buffer (the eyes' size,
holding the game's 4:3 screen) letterboxed to 4:3 */
static void vr_draw_flat(GLuint texture, long width, long height)
{
	int window_width, window_height, x, y, shown_width, shown_height;

	platform_video_drawable_size(&window_width, &window_height);
	if (window_width <= 0 || window_height <= 0)
		return;
	shown_width = window_width;
	shown_height = window_width * 3 / 4;
	if (shown_height > window_height)
	{
		shown_height = window_height;
		shown_width = window_height * 4 / 3;
	}
	x = (window_width - shown_width) / 2;
	y = (window_height - shown_height) / 2;
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glDisable(GL_SCISSOR_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, vr.framebuffers[0]);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	/* (row 0 of the back buffer is the top of the picture) */
	glBlitFramebuffer(0, 0, (GLint)width, (GLint)height, x, y + shown_height, x + shown_width, y,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

/* a panel's pose: the HUD's ahead of the head (VIEW space, so that it
turns with it at the compositor's rate), the flat screen's ahead of where
LOCAL faces, at the head's height */
static void vr_panel_layer(XrCompositionLayerQuad *quad, int hud)
{
	float distance = (float)config_real(hud ? "vr.hud_distance" : "vr.screen_distance");
	float width = (float)config_real(hud ? "vr.hud_width" : "vr.screen_width");

	if (!(distance >= 0.5f && distance <= 100.0f))
		distance = hud ? 15.0f : 2.0f;
	if (!(width >= 0.2f && width <= 100.0f))
		width = hud ? 10.0f : 2.4f;
	memset(quad, 0, sizeof(*quad));
	quad->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
	quad->layerFlags = hud ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
	quad->space = hud ? vr.view : vr.local;
	quad->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	quad->subImage.swapchain = vr.panel.handle;
	quad->subImage.imageRect.extent.width = VR_PANEL_WIDTH;
	quad->subImage.imageRect.extent.height = VR_PANEL_HEIGHT;
	quad->pose.orientation.w = 1.0f;
	quad->pose.position.z = -distance;
	quad->size.width = width;
	quad->size.height = width * VR_PANEL_HEIGHT / VR_PANEL_WIDTH;
}

int vr_present(unsigned int texture, int width, int height)
{
	XrCompositionLayerProjectionView views[2];
	XrCompositionLayerProjection projection;
	XrCompositionLayerQuad quad;
	const XrCompositionLayerBaseHeader *layers[2];
	uint32_t layer_count = 0;
	GLuint image;
	int eye, stereo;

	if (!vr.frame_begun)
		return 0;
	stereo = vr.frame.stereo && vr.eyes_drawn[0] && vr.eyes_drawn[1];
	if (!vr.frame_state.shouldRender)
	{
		/* (the runtime shows nothing of this frame: no layers) */
	}
	else if (stereo)
	{
		memset(views, 0, sizeof(views));
		for (eye = 0; eye < 2; eye++)
		{
			views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			views[eye].pose = vr.views[eye].pose;
			views[eye].fov = vr.views[eye].fov;
			views[eye].subImage.swapchain = vr.eyes[eye].handle;
			views[eye].subImage.imageRect.extent.width = (int32_t)vr.eyes[eye].width;
			views[eye].subImage.imageRect.extent.height = (int32_t)vr.eyes[eye].height;
		}
		memset(&projection, 0, sizeof(projection));
		projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projection.space = vr.local;
		projection.viewCount = 2;
		projection.views = views;
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&projection;
	}
	else
	{
		vr.frames_without_eyes++;
	}
	image = vr.frame_state.shouldRender ? vr_acquire(&vr.panel) : 0;
	if (image)
	{
		if (stereo)
			vr_draw_panel(texture, image);
		else
			vr_copy(texture, width, height, image, VR_PANEL_WIDTH, VR_PANEL_HEIGHT);
		vr_release(&vr.panel);
		vr_panel_layer(&quad, stereo);
		layers[layer_count++] = (const XrCompositionLayerBaseHeader *)&quad;
	}
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	vr_end_frame(layers, layer_count);
	if (stereo)
		vr_draw_mirror();
	else
		vr_draw_flat(texture, width, height);
	xgpu_gl_state_invalidate();
	return 1;
}

void vr_frame_finish(void)
{
	if (vr.frame_begun)
		vr_end_frame(NULL, 0);
}

#endif
