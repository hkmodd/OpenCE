/*
HUD_MAKE_AHEAD.C (test)

The high-res HUD's GL textures made as a map loads (hud_hires.c's decoding
ahead, textures_make_ahead and hud_hires_override_texture;
test_hud_make_ahead.py takes them as under_test.inc), with the PNG decoder,
GL and the render thread faked: with display.render_thread the textures are
made by a command queued for the render thread, never on the game's, and
without it as the map loads; each PNG is decoded once, whether the decoding
thread or the command reaches it first, and its texels are freed once
uploaded; none is left for its first draw; the driver's work is finished
with them; titles, those drawn already or failed and those with the setting
off or no renderer are not made; one that cannot be decoded is marked so;
without the decoding thread nothing is made ahead. The decoding thread is
run by hand (pthread_create only records it), so either order is tried.
*/

#include "harness.h"

typedef unsigned int GLuint;

/* (the thread and the lock, run and checked by the test) */
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_cond_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_COND_INITIALIZER 0

static void *(*thread_routine)(void *);
static int threads, threads_refused, threads_detached;

static int pthread_create(pthread_t *thread, const void *attributes, void *(*routine)(void *), void *parameter)
{
	(void)attributes;
	(void)parameter;
	if (threads_refused)
		return 1;
	CHECK(!thread_routine, "a second decoding thread");
	thread_routine = routine;
	*thread = ++threads;
	return 0;
}

static int pthread_detach(pthread_t thread)
{
	CHECK(thread == threads, "an unknown thread detached");
	threads_detached++;
	return 0;
}

static int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	CHECK(!*mutex, "the lock taken twice");
	*mutex = 1;
	return 0;
}

static int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	CHECK(*mutex, "the lock let go without being taken");
	*mutex = 0;
	return 0;
}

static int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
	(void)condition;
	(void)mutex;
	CHECK(0, "waited for a decoding no thread is doing");
	return 0;
}

static int pthread_cond_broadcast(pthread_cond_t *condition)
{
	(void)condition;
	return 0;
}

static void run_thread(void)
{
	CHECK(thread_routine, "no decoding thread to run");
	thread_routine(NULL);
	thread_routine = NULL;
}

#define HUD_HIRES_DECODE_AHEAD

#include "world.inc"

static struct hud_hires_embedded hud_hires_embedded[MAXIMUM_TEXTURES];
static long asset_count;

static long hud_hires_asset_count(void)
{
	return asset_count;
}

static int hud_enabled = 1, null_renderer;

static int config_boolean(const char *name)
{
	if (!strcmp(name, "display.high_res_hud"))
		return hud_enabled;
	if (!strcmp(name, "debug.null_renderer"))
		return null_renderer;
	CHECK(0, "an unknown setting %s", name);
	return 0;
}

static void platform_log(const char *format, ...)
{
	(void)format;
}

/* (the render thread: commands recorded, run by the test) */
static int render_thread_on, on_render_thread;
static void (*recorded)(void *);
static int records, submits;

static int render_thread_elsewhere(void)
{
	return render_thread_on && !on_render_thread;
}

static void *render_thread_record(void (*command)(void *), unsigned long size)
{
	CHECK(render_thread_on && !on_render_thread, "a command recorded without the render thread");
	CHECK(!size, "a payload for the command");
	CHECK(!recorded, "a second command");
	recorded = command;
	records++;
	return NULL;
}

static void render_thread_submit(void)
{
	CHECK(recorded, "nothing to submit");
	submits++;
}

static void run_command(void)
{
	CHECK(recorded && submits == records, "no command handed to the render thread");
	on_render_thread = 1;
	recorded(NULL);
	on_render_thread = 0;
	recorded = NULL;
}

/* (GL: textures made where the context is, and the driver's work finished) */
static int finishes, pending;

static int on_gl_thread(void)
{
	return !render_thread_on || on_render_thread;
}

static void glFinish(void)
{
	CHECK(on_gl_thread(), "glFinish on a thread without the GL context");
	finishes++;
	pending = 0;
}

/* (an asset's png_size is its index) */
static unsigned long decodes[MAXIMUM_TEXTURES];
static char undecodable[MAXIMUM_TEXTURES];
static long live;

static unsigned char *png_decode(const unsigned char *png, unsigned long size, unsigned long *width,
	unsigned long *height)
{
	(void)png;
	decodes[size]++;
	if (undecodable[size])
		return NULL;
	*width = *height = 1;
	live++;
	return malloc(4);
}

static unsigned int png_pixels_texture(unsigned char *pixels, unsigned long width, unsigned long height,
	unsigned long *levels)
{
	static GLuint next = 1000;

	CHECK(on_gl_thread(), "a texture made on a thread without the GL context");
	if (!pixels)
		return 0;
	CHECK(width == 1 && height == 1, "other texels uploaded");
	free(pixels);
	live--;
	*levels = 1;
	pending = 1;
	return next++;
}

#include "under_test.inc"

/* ---------- cases */

static void world(long count)
{
	long asset;

	asset_count = count;
	for (asset = 0; asset < count; asset++)
	{
		hud_hires_embedded[asset].tag = "ui\\hud\\bitmaps\\test";
		hud_hires_embedded[asset].bitmap = (int)asset;
		hud_hires_embedded[asset].png_size = (unsigned int)asset;
	}
}

static void map_loaded(long count)
{
	long assets[MAXIMUM_TEXTURES], index;

	for (index = 0; index < count; index++)
		assets[index] = index;
	hud_hires_map_loaded(assets, count);
}

/* each of the first count made once, from one decoding, and drawn without another */
static void check_made(long count)
{
	long asset;

	for (asset = 0; asset < count; asset++)
	{
		unsigned long levels = 0;
		unsigned int texture = textures[asset].texture;

		CHECK(texture >= 1000, "asset %ld not made", asset);
		CHECK(decodes[asset] == 1, "asset %ld decoded %lu times", asset, decodes[asset]);
		CHECK(hud_hires_override_texture(asset, &levels) == texture && levels == 1 && decodes[asset] == 1,
			"asset %ld not drawn with the texture made", asset);
	}
	CHECK(!live, "%ld textures' texels not freed", live);
	CHECK(finishes == 1 && !pending, "the driver's work not finished with them (%d)", finishes);
}

static void check_nothing_ahead(long count)
{
	long asset;

	for (asset = 0; asset < count; asset++)
		CHECK(!textures[asset].texture && !decodes[asset] && textures[asset].decode_state == _decode_none,
			"asset %ld made or decoded ahead", asset);
	CHECK(!records && !finishes, "something made ahead");
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "render-thread"))
	{
		render_thread_on = 1;
		world(10);
		map_loaded(10);
		CHECK(threads == 1 && threads_detached == 1, "no decoding thread");
		CHECK(records == 1 && submits == 1, "the textures not queued for the render thread");
		CHECK(!textures[0].texture && !decodes[0], "made on the game's thread");
		run_thread();
		run_command();
		check_made(10);
	}
	else if (!strcmp(name, "render-thread-first"))
	{
		/* (the command before the decoding thread: it decodes them itself) */
		render_thread_on = 1;
		world(10);
		map_loaded(10);
		run_command();
		check_made(10);
		run_thread();
		check_made(10);
	}
	else if (!strcmp(name, "no-render-thread"))
	{
		world(10);
		map_loaded(10);
		CHECK(!records, "a command without the render thread");
		check_made(10);
		run_thread();
		check_made(10);
	}
	else if (!strcmp(name, "skipped"))
	{
		render_thread_on = 1;
		world(6);
		hud_hires_embedded[1].title = 1;
		textures[2].texture = 77;
		textures[3].failed = 1;
		map_loaded(6);
		run_thread();
		run_command();
		CHECK(textures[0].texture >= 1000 && decodes[0] == 1, "a texture of the map not made");
		CHECK(!textures[1].texture && !decodes[1], "a title made ahead");
		CHECK(textures[2].texture == 77 && !decodes[2], "a texture drawn already made again");
		CHECK(!textures[3].texture && !decodes[3], "a texture that failed made again");
		memset(textures, 0, sizeof(textures));
		memset(decodes, 0, sizeof(decodes));
		records = submits = finishes = 0;
		hud_enabled = 0;
		map_loaded(6);
		CHECK(!thread_routine, "decoded with display.high_res_hud off");
		check_nothing_ahead(6);
		hud_enabled = 1;
		null_renderer = 1;
		map_loaded(6);
		CHECK(!thread_routine, "decoded with debug.null_renderer");
		check_nothing_ahead(6);
	}
	else if (!strcmp(name, "undecodable"))
	{
		render_thread_on = 1;
		world(6);
		undecodable[3] = 1;
		map_loaded(6);
		run_thread();
		run_command();
		CHECK(textures[3].failed && !textures[3].texture, "a texture that could not be decoded not marked so");
		CHECK(textures[2].texture >= 1000 && textures[5].texture >= 1000, "the others not made");
		CHECK(!live, "texels not freed");
	}
	else if (!strcmp(name, "no-thread"))
	{
		unsigned long levels = 0;

		render_thread_on = 1;
		threads_refused = 1;
		world(6);
		map_loaded(6);
		check_nothing_ahead(6);
		on_render_thread = 1;
		CHECK(hud_hires_override_texture(4, &levels) >= 1000 && decodes[4] == 1, "not made when first drawn");
		on_render_thread = 0;
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
