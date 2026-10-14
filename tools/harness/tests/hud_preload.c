/*
HUD_PRELOAD.C (test)

The high-res HUD's textures decoded as a map loads (hud_hires.c's preloading,
and hud_hires_override_texture; test_hud_preload.py takes them as
under_test.inc), with the PNG decoder and GL faked: each texture is decoded
once, on the threads, and none is left for its first draw; those drawn
already, failed or with their setting off are not; one that cannot be
decoded is marked so; the texels held stay within the budget; without
threads nothing is decoded ahead; without a GL context (debug.null_renderer)
nothing is decoded or uploaded.
*/

#include "harness.h"

#include <pthread.h>
#include <stdarg.h>

typedef unsigned int GLuint;

#include "world.inc"

static struct hud_hires_embedded hud_hires_embedded[MAXIMUM_TEXTURES];
static long asset_count;

static long hud_hires_asset_count(void)
{
	return asset_count;
}

static int hud_enabled = 1, titles_enabled = 1;

static int config_boolean(const char *name)
{
	return !strcmp(name, "display.high_res_text") ? titles_enabled : hud_enabled;
}

static void platform_log(const char *format, ...)
{
	(void)format;
}

/* whether the device has a GL context */
static int gl_ready = 1;

static int xgpu_gl_ready(void)
{
	return gl_ready;
}

static int create_fails;
#define pthread_create(thread, attributes, start, argument) \
	(create_fails ? 1 : pthread_create(thread, attributes, start, argument))

static unsigned char *png_decode(const unsigned char *data, unsigned long size, unsigned long *png_width,
	unsigned long *png_height);
static GLuint pixels_texture(const unsigned char *pixels, unsigned long width, unsigned long height,
	unsigned long *levels);
static unsigned int hud_hires_png_texture(const void *png, unsigned long size, unsigned long *levels);

/* texels handed out and freed */
static long live;

static void counted_free(void *pointer)
{
	if (pointer)
		__atomic_sub_fetch(&live, 1, __ATOMIC_RELAXED);
	free(pointer);
}
#define free(pointer) counted_free(pointer)

#include "under_test.inc"

#undef free

/* ---------- fakes */

static pthread_mutex_t counting = PTHREAD_MUTEX_INITIALIZER;
static unsigned long decodes[MAXIMUM_TEXTURES];
static int decoding, most_decoding, over_budget;
static unsigned long first_draw_decodes;
static char undecodable[MAXIMUM_TEXTURES];

/* (an asset's png_size is its index) */
static unsigned char *png_decode(const unsigned char *data, unsigned long size, unsigned long *png_width,
	unsigned long *png_height)
{
	long asset = (long)size;
	struct timespec pause = { 0, 2000000 };
	unsigned char *pixels;

	(void)data;
	pthread_mutex_lock(&preload.lock);
	/* (more than the budget only for one texture, alone) */
	if (preload.held > PRELOAD_BUDGET && preload.held != preload_size(asset))
		over_budget = 1;
	pthread_mutex_unlock(&preload.lock);
	pthread_mutex_lock(&counting);
	decodes[asset]++;
	if (++decoding > most_decoding)
		most_decoding = decoding;
	pthread_mutex_unlock(&counting);
	nanosleep(&pause, NULL);
	pthread_mutex_lock(&counting);
	decoding--;
	pthread_mutex_unlock(&counting);
	if (undecodable[asset])
		return NULL;
	*png_width = 1;
	*png_height = 1;
	pixels = malloc(4);
	__atomic_add_fetch(&live, 1, __ATOMIC_RELAXED);
	return pixels;
}

static GLuint pixels_texture(const unsigned char *pixels, unsigned long width, unsigned long height,
	unsigned long *levels)
{
	static GLuint next = 1000;

	CHECK(gl_ready, "a texture uploaded without a GL context");
	CHECK(pixels && width == 1 && height == 1, "uploaded other texels");
	*levels = 1;
	return next++;
}

/* (decoding a texture as it is first drawn) */
static unsigned int hud_hires_png_texture(const void *png, unsigned long size, unsigned long *levels)
{
	(void)png;
	(void)size;
	first_draw_decodes++;
	*levels = 1;
	return 500;
}

/* ---------- cases */

/* count assets, sized as the HUD's are (some 2048 by 2048, one 4096 by
4096, larger than the budget, if large) */
static void world(long count, int large)
{
	long asset;

	asset_count = count;
	for (asset = 0; asset < count; asset++)
	{
		hud_hires_embedded[asset].tag = "ui\\hud\\bitmaps\\test";
		hud_hires_embedded[asset].bitmap = (int)asset;
		hud_hires_embedded[asset].width = asset % 3 ? 512 : 1024;
		hud_hires_embedded[asset].height = 256;
		if (large && asset % 7 == 0)
			hud_hires_embedded[asset].width = hud_hires_embedded[asset].height = 2048;
		if (large && asset == 5)
			hud_hires_embedded[asset].width = hud_hires_embedded[asset].height = 4096;
		hud_hires_embedded[asset].png_size = (unsigned int)asset;
	}
}

static void preload_all(long count)
{
	long assets[MAXIMUM_TEXTURES], index;

	for (index = 0; index < count; index++)
		assets[index] = index;
	hud_hires_preload(assets, count);
}

static void check_drawn_without_decoding(long count)
{
	long asset;

	for (asset = 0; asset < count; asset++)
	{
		unsigned long levels = 0;

		CHECK(decodes[asset] == 1, "asset %ld decoded %lu times", asset, decodes[asset]);
		CHECK(textures[asset].texture >= 1000, "asset %ld has no texture", asset);
		CHECK(hud_hires_override_texture(asset, &levels) == textures[asset].texture && levels == 1,
			"asset %ld drawn without its texture", asset);
	}
	CHECK(!first_draw_decodes, "%lu textures decoded when first drawn", first_draw_decodes);
}

static void check_done(void)
{
	CHECK(!preload.threads, "%d threads left running", preload.threads);
	CHECK(!live, "%ld textures' texels not freed", live);
	CHECK(!preload.held, "%lu bytes still reckoned held", preload.held);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "decoded-ahead"))
	{
		world(60, 0);
		preload_all(60);
		check_drawn_without_decoding(60);
		check_done();
		CHECK(most_decoding > 1, "decoded one at a time");
	}
	else if (!strcmp(name, "skipped"))
	{
		long assets[] = { 0, 1, 2, 3, 4, 5 };

		world(6, 0);
		textures[1].texture = 77;
		textures[2].failed = 1;
		hud_hires_embedded[3].title = 1;
		titles_enabled = 0;
		hud_hires_preload(assets, 6);
		CHECK(decodes[0] == 1 && textures[0].texture >= 1000, "a texture not drawn yet not decoded");
		CHECK(!decodes[1] && textures[1].texture == 77, "a texture drawn already decoded again");
		CHECK(!decodes[2] && !textures[2].texture, "a texture that failed decoded again");
		CHECK(!decodes[3] && !textures[3].texture, "a title decoded with display.high_res_text off");
		memset(textures, 0, sizeof(textures));
		memset(decodes, 0, sizeof(decodes));
		titles_enabled = 1;
		hud_enabled = 0;
		hud_hires_preload(assets, 6);
		CHECK(!decodes[0] && !textures[0].texture, "a HUD texture decoded with display.high_res_hud off");
		CHECK(decodes[3] == 1 && textures[3].texture >= 1000, "a title not decoded with display.high_res_text on");
		check_done();
	}
	else if (!strcmp(name, "undecodable"))
	{
		world(12, 0);
		undecodable[3] = 1;
		preload_all(12);
		CHECK(textures[3].failed && !textures[3].texture, "a texture that could not be decoded not marked so");
		CHECK(textures[4].texture >= 1000 && textures[11].texture >= 1000, "the others not decoded");
		check_done();
	}
	else if (!strcmp(name, "budget"))
	{
		world(40, 1);
		preload_all(40);
		CHECK(!over_budget, "more texels held than the budget");
		check_drawn_without_decoding(40);
		check_done();
	}
	else if (!strcmp(name, "no-threads"))
	{
		unsigned long levels;

		create_fails = 1;
		world(8, 0);
		preload_all(8);
		CHECK(!decodes[0] && !textures[0].texture, "decoded without threads");
		CHECK(hud_hires_override_texture(0, &levels) == 500 && first_draw_decodes == 1,
			"not decoded when first drawn");
		check_done();
	}
	else if (!strcmp(name, "no-renderer"))
	{
		gl_ready = 0;
		world(8, 0);
		preload_all(8);
		CHECK(!decodes[0] && !textures[0].texture && !textures[0].failed, "decoded without a GL context");
		check_done();
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
