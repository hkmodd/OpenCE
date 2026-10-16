/* The decals off the screen (rasterizer_xbox_decals.c): the real
rasterizer_decals_draw_ordered, after the real rasterizer_decal_order_add,
logs every state it sets and every decal it hands over, and every decal's
place against the screen worked out exactly, with a camera that looks down
-z (view = world, x and y the normalized device coordinates at z = -1).
test_decal_cull.py compares the log with that of the same code that takes
every decal to be on the screen: the same states set in the same order,
and only decals that cover no pixel left out. */
#include "harness.h"
#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#include "types.inc"

/* the device and the rasterizer: what the drawing calls, logged */
typedef unsigned long pixel32;
#define PIXEL32_COMPONENT_MASK 0xFF
enum { D3DRS_COLORWRITEENABLE = 67, D3DLOCK_READONLY = 0x10 };
#define D3DCOLORWRITEENABLE_RED 4
#define D3DCOLORWRITEENABLE_GREEN 2
#define D3DCOLORWRITEENABLE_BLUE 1
#define D3DCOLORWRITEENABLE_ALL 15
enum { _rasterizer_statistics_mode_none, _rasterizer_statistics_mode_geometry };
#define match_vassert(file, line, condition, message) ((void)0)
static int *global_d3d_device;
static struct { short statistics_mode; } rasterizer_debug_options;
static struct
{
	long decal_shader_change_count, decal_texture_change_count, decal_draw_count, decal_triangle_count, decal_vertex_count;
} rasterizer_frame_statistics;
static struct { unsigned long rgb_inputs[8], alpha_inputs[8]; } pixel_shader;
static void IDirect3DDevice8_SetRenderState(int *device, long state, unsigned long value)
{
	(void)device;
	printf("state %ld %lu\n", state, value);
}
static void rasterizer_set_framebuffer_blend_function(short function)
{
	printf("blend %d\n", function);
}
static void rasterizer_set_pixel_shader(void *shader)
{
	printf("pixel-shader %lx %lx %lx\n", pixel_shader.rgb_inputs[0], pixel_shader.rgb_inputs[1], pixel_shader.alpha_inputs[1]);
	(void)shader;
}
static void rasterizer_set_texture(short stage, short type, short usage, long bitmap_group_index, short bitmap_index)
{
	printf("texture %d %d %d %ld %d\n", stage, type, usage, bitmap_group_index, bitmap_index);
}

/* the decals */
#define TEST_MAXIMUM_DECALS 2048
struct decal_shader_definition { short framebuffer_blend_function; struct { long index; } map; };
struct decal_definition { struct decal_shader_definition shader; };
struct decal_datum { long definition_index; byte bitmap_index; pixel32 color; unsigned long intensity; short quad_count; };
static struct decal_definition definitions[TEST_MAXIMUM_DECALS];
static struct decal_datum decal_data[TEST_MAXIMUM_DECALS];
static struct decal_vertex vertex_buffer[TEST_MAXIMUM_DECALS * 4];
#define DECAL_GET(index) (&decal_data[index])
#define decal_definition_get(index) (&definitions[index])
static int *local_d3d_vertex_buffer, *local_vertex_cache;
static void *lruv_block_get_address(int *cache, long decal_index)
{
	(void)cache;
	return (void *)(size_t)(decal_index * 4 * sizeof(struct decal_vertex));
}
static void IDirect3DVertexBuffer8_Lock(int *buffer, long offset, long size, byte **data, unsigned long flags)
{
	(void)buffer; (void)offset; (void)size; (void)flags;
	*data = (byte *)vertex_buffer;
}

static struct
{
	struct
	{
		real_matrix4x3 world_to_view;
		boolean projection_valid;
		real projection_matrix[4][4];
	} frustum;
} global_window_parameters;
static short local_layer;
static long rasterizer_decal_cached_bitmap_group_index = NONE;
static short rasterizer_decal_cached_bitmap_index = NONE;
static short local_framebuffer_blend_function = 0;

#include "under_test.inc"

/* the batch drawn: its decals, by where their vertices are, and colors */
static void rasterizer_decals_draw_batch(
	void)
{
	short batch_index;

	if (!decal_batch_count)
		return;
	for (batch_index = 0; batch_index < decal_batch_count; batch_index++)
	{
		long decal_index = (long)(decal_batch[batch_index].vertices - vertex_buffer) / 4;

		printf("decal %ld %d %d %d %d %d\n", decal_index, decal_batch[batch_index].vertex_count,
			decal_batch[batch_index].color[0], decal_batch[batch_index].color[1], decal_batch[batch_index].color[2],
			decal_batch[batch_index].color[3]);
	}
	decal_batch_count = 0;
}

static void camera(boolean valid)
{
	memset(&global_window_parameters, 0, sizeof(global_window_parameters));
	global_window_parameters.frustum.world_to_view.scale = 1.0f;
	global_window_parameters.frustum.world_to_view.n[0][0] = 1.0f;
	global_window_parameters.frustum.world_to_view.n[1][1] = 1.0f;
	global_window_parameters.frustum.world_to_view.n[2][2] = 1.0f;
	global_window_parameters.frustum.projection_valid = valid;
	global_window_parameters.frustum.projection_matrix[0][0] = 1.0f;
	global_window_parameters.frustum.projection_matrix[1][1] = 1.0f;
	global_window_parameters.frustum.projection_matrix[2][2] = -1.0f;
	global_window_parameters.frustum.projection_matrix[2][3] = -1.0f;
	global_window_parameters.frustum.projection_matrix[3][2] = -0.1f;
}

static unsigned long random_state = 12345;

static real random_real(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (real)((random_state >> 8) & 0xFFFF) / 65536.0f;
}

static short const blend_functions[] =
{
	_shader_framebuffer_blend_function_alpha_blend,
	_shader_framebuffer_blend_function_multiply,
	_shader_framebuffer_blend_function_double_multiply,
	_shader_framebuffer_blend_function_add,
	_shader_framebuffer_blend_function_min,
	_shader_framebuffer_blend_function_max,
	_shader_framebuffer_blend_function_reverse_subtract,
	_shader_framebuffer_blend_function_alpha_multiply_add,
};

/* a decal of quads size wide, side by side, at depth z, from (x, y) in the
normalized device coordinates */
static void make_decal(long index, short quad_count, real x, real y, real size, real z)
{
	short vertex_index;

	definitions[index].shader.framebuffer_blend_function = blend_functions[(long)(random_real() * 8.0f)];
	definitions[index].shader.map.index = 1 + (long)(random_real() * 3.0f);
	decal_data[index].definition_index = index;
	decal_data[index].bitmap_index = (byte)(random_real() * 4.0f);
	decal_data[index].color = (pixel32)(random_state ^ (random_state << 9));
	decal_data[index].intensity = (unsigned long)(random_real() * 255.0f);
	decal_data[index].quad_count = quad_count;
	for (vertex_index = 0; vertex_index < 4 * quad_count; vertex_index++)
	{
		struct decal_vertex *vertex = &vertex_buffer[index * 4 + vertex_index];
		short corner = vertex_index % 4;

		vertex->position.x = (x + ((corner == 1 || corner == 2) ? size : 0.0f) + (vertex_index / 4) * size) * -z;
		vertex->position.y = (y + (corner >= 2 ? size : 0.0f)) * -z;
		vertex->position.z = z;
		vertex->texcoord = (unsigned long)vertex_index * 0x10001UL;
	}
}

/* whether a decal can cover a pixel, worked out from its corners: one with a
corner at or behind the camera may; otherwise the rectangle of its corners on
the screen must reach the screen's within a pixel's rounding (1/256) */
static boolean exactly_on_screen(long index)
{
	real x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
	short vertex_index;

	if (!global_window_parameters.frustum.projection_valid)
		return TRUE;
	for (vertex_index = 0; vertex_index < 4 * decal_data[index].quad_count; vertex_index++)
	{
		real_point3d const *p = &vertex_buffer[index * 4 + vertex_index].position;

		if (p->z >= 0.0f)
			return TRUE;
		x0 = MIN(x0, p->x / -p->z);
		x1 = MAX(x1, p->x / -p->z);
		y0 = MIN(y0, p->y / -p->z);
		y1 = MAX(y1, p->y / -p->z);
	}
	return x0 <= 1.0f + 1.0f / 256.0f && x1 >= -1.0f - 1.0f / 256.0f &&
		y0 <= 1.0f + 1.0f / 256.0f && y1 >= -1.0f - 1.0f / 256.0f;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	long decal_count = 0, index, layer;
	boolean valid = TRUE, edges = FALSE, behind = FALSE;

	if (!strcmp(case_name, "fight"))
	{
		/* a fight's decals, most beyond the edges of the screen, some across
		them, some with a corner behind the camera */
		decal_count = 1000;
	}
	else if (!strcmp(case_name, "edges"))
	{
		/* each decal at an edge: across it, or beyond it by up to 3/100,
		in and out of the margin (DECAL_ORDER_SCREEN_MARGIN) */
		decal_count = 1000;
		edges = TRUE;
	}
	else if (!strcmp(case_name, "behind-camera"))
	{
		/* decals beyond the right edge with a corner behind the camera,
		which also projects beyond it: the side across the camera's plane
		comes back across the screen */
		decal_count = 300;
		behind = TRUE;
	}
	else if (!strcmp(case_name, "invalid-projection"))
	{
		decal_count = 300;
		valid = FALSE;
	}
	else
	{
		fprintf(stderr, "unknown case: %s\n", case_name);
		return 2;
	}
	camera(valid);
	for (index = 0; index < decal_count; index++)
	{
		short quad_count = random_real() < 0.2f ? 2 : 1;
		real size = 0.005f + random_real() * 0.1f;
		real x = (random_real() * 2.0f - 1.0f) * 3.0f, y = (random_real() * 2.0f - 1.0f) * 3.0f;

		if (edges)
		{
			/* (the side nearer the screen this far beyond the edge) */
			real beyond = random_real() * 0.06f - 0.03f;
			real along = random_real() * 2.0f - 1.0f;

			switch ((long)(random_real() * 4.0f))
			{
				case 0: x = 1.0f + beyond; y = along; break;
				case 1: x = -1.0f - beyond - size * quad_count; y = along; break;
				case 2: x = along; y = 1.0f + beyond; break;
				default: x = along; y = -1.0f - beyond - size; break;
			}
		}
		if (behind)
			x = 1.1f + random_real() * 1.5f;
		make_decal(index, quad_count, x, y, size, -0.5f - random_real() * 20.0f);
		if (behind)
		{
			real_point3d *p = &vertex_buffer[index * 4 + (long)(random_real() * 4.0f)].position;
			real z = 0.5f + random_real() * 2.0f;

			p->y = p->y / -p->z * -z;
			p->x = (1.1f + random_real() * 2.0f) * -z;
			p->z = z;
		}
		if (!edges && !behind && random_real() < 0.03f)
			vertex_buffer[index * 4 + (long)(random_real() * 4.0f)].position.z = 0.25f;
		printf("%s %ld\n", exactly_on_screen(index) ? "on" : "off", index);
	}
	for (layer = 0; layer < 2; layer++)
	{
		local_layer = layer ? _decal_layer_alpha_tested : _decal_layer_primary;
		rasterizer_debug_options.statistics_mode = (short)layer;
		printf("layer %ld\n", layer);
		for (index = 0; index < decal_count; index++)
		{
			rasterizer_decal_order_add(index, definitions[index].shader.framebuffer_blend_function,
				definitions[index].shader.map.index, decal_data[index].bitmap_index, &vertex_buffer[index * 4],
				(short)(4 * decal_data[index].quad_count));
		}
		rasterizer_decals_draw_ordered();
		rasterizer_decals_draw_batch();
	}
	printf("statistics %ld %ld %ld %ld %ld\n", rasterizer_frame_statistics.decal_shader_change_count,
		rasterizer_frame_statistics.decal_texture_change_count, rasterizer_frame_statistics.decal_draw_count,
		rasterizer_frame_statistics.decal_triangle_count, rasterizer_frame_statistics.decal_vertex_count);
	return 0;
}
