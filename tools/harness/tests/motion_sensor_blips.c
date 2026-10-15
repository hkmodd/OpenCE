/* The motion sensor's blips drawn batched (rasterizer_xbox_motion_sensor.c):
the quads drawn, against those the blips gave a draw each, a list of quads
for each bitmap, and the bitmap left set after them, the last blip's. */
#include "harness.h"
#include "types.inc"

enum
{
	_interface_bitmap_motion_blip,
	_interface_bitmap_iface_map1
};
enum
{
	D3DPT_QUADLIST = 8,
	D3DPT_TRIANGLEFAN = 6
};
struct bitmap_data
{
	int unused;
};

static struct bitmap_data bitmaps[2];
static void *global_d3d_device = &bitmaps;
static struct
{
	boolean draw_hud_motion_sensor;
} rasterizer_debug_options;
static boolean bss_00465e27;

#define interface_get_tag_index(bitmap) (bitmap)
#define bitmap_group_try_and_get_bitmap(group, index) (&bitmaps[(group)])

/* the device: what the draws give */
#define TEST_MAXIMUM_VERTICES 8192
static struct test_vertex
{
	struct bitmap_data *bitmap;
	real red, green, blue, alpha;
	short u, v;
	real x, y;
} vertices[TEST_MAXIMUM_VERTICES];
static long vertex_count;
static long draw_count;
static boolean drawing;
static struct test_vertex current;

static void rasterizer_set_texture_bitmap_data(short stage, struct bitmap_data *bitmap)
{
	CHECK(stage == 0 && !drawing, "texture set at stage %d, %s a draw", stage, drawing ? "inside" : "outside");
	current.bitmap = bitmap;
}

static void IDirect3DDevice8_Begin(void *device, long type)
{
	CHECK(device == global_d3d_device && !drawing && type == D3DPT_QUADLIST, "begun as %ld", type);
	drawing = TRUE;
}

static void IDirect3DDevice8_SetVertexData4f(void *device, long reg, real a, real b, real c, real d)
{
	CHECK(drawing && reg == 9, "color into %ld", reg);
	current.red = a;
	current.green = b;
	current.blue = c;
	current.alpha = d;
}

static void IDirect3DDevice8_SetVertexData2s(void *device, long reg, short a, short b)
{
	CHECK(drawing && reg == 4, "texture coordinate into %ld", reg);
	current.u = a;
	current.v = b;
}

static void IDirect3DDevice8_SetVertexData2f(void *device, long reg, real a, real b)
{
	CHECK(drawing && reg == 0 && vertex_count < TEST_MAXIMUM_VERTICES, "position into %ld", reg);
	current.x = a;
	current.y = b;
	vertices[vertex_count++] = current;
}

static void IDirect3DDevice8_End(void *device)
{
	CHECK(drawing && vertex_count % 4 == 0, "a draw of %ld vertices", vertex_count);
	drawing = FALSE;
	draw_count++;
}

#include "under_test.inc"

#define TEST_MAXIMUM_BLIPS 2048
static struct
{
	real_point2d position;
	real intensity, size;
	real_rgb_color color;
	boolean large;
} blips[TEST_MAXIMUM_BLIPS];
static long blip_count;

static unsigned long random_state = 777;

static real random_real(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (real)((random_state >> 8) & 0xFFFF) / 65536.0f;
}

static void draw(long count, int large_every)
{
	long index;

	for (index = 0; index < count; index++)
	{
		CHECK(blip_count < TEST_MAXIMUM_BLIPS, "too many blips for the test");
		blips[blip_count].position.x = random_real() * 64.0f - 32.0f;
		blips[blip_count].position.y = random_real() * 64.0f - 32.0f;
		blips[blip_count].intensity = random_real();
		blips[blip_count].size = 1.0f + random_real() * 8.0f;
		blips[blip_count].color.red = random_real();
		blips[blip_count].color.green = random_real();
		blips[blip_count].color.blue = random_real();
		blips[blip_count].large = large_every && index % large_every == 0;
		_rasterizer_hud_motion_sensor_blip_draw(&blips[blip_count].position, blips[blip_count].intensity,
			blips[blip_count].size, &blips[blip_count].color, blips[blip_count].large);
		blip_count++;
	}
	/* as _rasterizer_hud_motion_sensor_blip_end */
	rasterizer_hud_motion_sensor_blips_draw();
	blip_batch_last_bitmap = NULL;
}

/* blip's quad, as its draw of its own gave it: false if the vertices differ */
static boolean quad_is_blip(struct test_vertex const *quad, long index)
{
	static short const corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	real radius = blips[index].size * 0.0625f;
	real x = blips[index].position.x * -0.03125f;
	real y = blips[index].position.y * -0.03125f;
	real red = blips[index].color.red * blips[index].intensity;
	real green = blips[index].color.green * blips[index].intensity;
	real blue = blips[index].color.blue * blips[index].intensity;
	short corner;

	for (corner = 0; corner < 4; corner++)
	{
		real corner_x = corners[corner][0] ? x + radius : x - radius;
		real corner_y = corners[corner][1] ? y - radius : y + radius;

		if (quad[corner].bitmap != &bitmaps[blips[index].large ? 1 : 0] ||
			quad[corner].red != red || quad[corner].green != green || quad[corner].blue != blue ||
			quad[corner].alpha != 1.0f || quad[corner].u != corners[corner][0] || quad[corner].v != corners[corner][1] ||
			quad[corner].x != corner_x || quad[corner].y != corner_y)
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* every blip's quad once; within a bitmap in the order drawn */
static void check_quads(void)
{
	static boolean found[TEST_MAXIMUM_BLIPS];
	long quad, index, last[2] = {NONE, NONE};

	CHECK(vertex_count == blip_count * 4, "%ld vertices for %ld blips", vertex_count, blip_count);
	memset(found, 0, sizeof(found));
	for (quad = 0; quad < vertex_count / 4; quad++)
	{
		for (index = 0; index < blip_count; index++)
		{
			if (!found[index] && quad_is_blip(&vertices[quad * 4], index))
				break;
		}
		CHECK(index < blip_count, "quad %ld is no blip's", quad);
		CHECK(index > last[blips[index].large], "blip %ld drawn after a later one of its bitmap", index);
		found[index] = TRUE;
		last[blips[index].large] = index;
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	rasterizer_debug_options.draw_hud_motion_sensor = TRUE;
	bss_00465e27 = TRUE;
	current.bitmap = &bitmaps[0];

	/* small and large blips: a draw each bitmap, the last blip's bitmap left set */
	CASE("mixed")
	{
		draw(100, 2);
		check_quads();
		CHECK(draw_count == 2, "%ld draws", draw_count);
		CHECK(current.bitmap == &bitmaps[0], "the large blips' bitmap left set after a small blip");
		return 0;
	}
	CASE("small")
	{
		draw(30, 0);
		check_quads();
		CHECK(draw_count == 1, "%ld draws", draw_count);
		CHECK(current.bitmap == &bitmaps[0], "bitmap left set");
		return 0;
	}
	CASE("last-large")
	{
		draw(31, 3);
		check_quads();
		CHECK(draw_count == 2, "%ld draws", draw_count);
		CHECK(current.bitmap == &bitmaps[1], "the small blips' bitmap left set after a large blip");
		return 0;
	}
	/* more blips than the batch holds: drawn as it fills, none lost */
	CASE("full")
	{
		draw(BLIP_BATCH_MAXIMUM_BLIPS * 2 + 100, 0);
		check_quads();
		CHECK(draw_count == 3, "%ld draws", draw_count);
		return 0;
	}
	/* the motion sensor not drawn: no draw, but the bitmap set as each blip set it */
	CASE("hidden")
	{
		rasterizer_debug_options.draw_hud_motion_sensor = FALSE;
		current.bitmap = NULL;
		draw(10, 2);
		CHECK(draw_count == 0 && vertex_count == 0, "%ld draws", draw_count);
		CHECK(current.bitmap == &bitmaps[0], "the bitmap the last blip set");
		return 0;
	}
	CASE("bitmaps-unready")
	{
		bss_00465e27 = FALSE;
		current.bitmap = NULL;
		draw(9, 3);
		CHECK(draw_count == 0 && vertex_count == 0, "%ld draws", draw_count);
		CHECK(current.bitmap == &bitmaps[0], "the bitmap the last blip set");
		return 0;
	}
	/* no blips: nothing drawn or set */
	CASE("none")
	{
		current.bitmap = NULL;
		draw(0, 0);
		CHECK(draw_count == 0 && current.bitmap == NULL, "drawn or set without blips");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
