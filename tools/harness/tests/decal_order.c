/* The decals' draw order (rasterizer_xbox_decals.c): the batch each decal of
a layer joins, with a camera that looks down -z (view = world, x and y the
normalized device coordinates at z = -1). The order is checked against its
rule: two decals whose rectangles on the screen come within a pixel's
rounding, or with a corner behind the camera, stay in their order unless
their blend function is the same and commutes (outside the alpha-tested
layer). */
#include "harness.h"
#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#include "types.inc"

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

#include "under_test.inc"

#define TEST_MAXIMUM_DECALS 4096
/* (as near as two rectangles may come without a pixel in common: 1/256 of
the normalized device coordinates, a pixel at 512 pixels across) */
#define TEST_ROUNDING (1.0f / 256.0f)

static struct
{
	short framebuffer_blend_function;
	long bitmap_group_index;
	byte bitmap_index;
	struct decal_vertex vertices[4];
} decals[TEST_MAXIMUM_DECALS];
static long decal_count;
static long drawn[TEST_MAXIMUM_DECALS];
static long drawn_count;
static long flush_count;

/* as the game's walks the batches */
static void rasterizer_decals_draw_ordered(
	void)
{
	long batch_index, entry_index;

	for (batch_index = 0; batch_index < decal_order_batch_count; batch_index++)
	{
		for (entry_index = decal_order_batches[batch_index].first_entry_index;
			entry_index != NONE;
			entry_index = decal_order_entries[entry_index].next_entry_index)
		{
			CHECK(drawn_count < TEST_MAXIMUM_DECALS, "more drawn than added");
			drawn[drawn_count++] = decal_order_entries[entry_index].decal_index;
		}
	}
	decal_order_entry_count = 0;
	decal_order_batch_count = 0;
	flush_count++;
}

static void camera(void)
{
	memset(&global_window_parameters, 0, sizeof(global_window_parameters));
	global_window_parameters.frustum.world_to_view.scale = 1.0f;
	global_window_parameters.frustum.world_to_view.n[0][0] = 1.0f;
	global_window_parameters.frustum.world_to_view.n[1][1] = 1.0f;
	global_window_parameters.frustum.world_to_view.n[2][2] = 1.0f;
	global_window_parameters.frustum.projection_valid = TRUE;
	global_window_parameters.frustum.projection_matrix[0][0] = 1.0f;
	global_window_parameters.frustum.projection_matrix[1][1] = 1.0f;
	global_window_parameters.frustum.projection_matrix[2][2] = -1.0f;
	global_window_parameters.frustum.projection_matrix[2][3] = -1.0f;
	global_window_parameters.frustum.projection_matrix[3][2] = -0.1f;
}

/* a square decal size wide from (x, y) at z = -1 */
static long decal(short framebuffer_blend_function, long bitmap_group_index, byte bitmap_index, real x, real y, real size)
{
	long index = decal_count++;
	short vertex_index;

	CHECK(index < TEST_MAXIMUM_DECALS, "too many decals for the test");
	decals[index].framebuffer_blend_function = framebuffer_blend_function;
	decals[index].bitmap_group_index = bitmap_group_index;
	decals[index].bitmap_index = bitmap_index;
	for (vertex_index = 0; vertex_index < 4; vertex_index++)
	{
		decals[index].vertices[vertex_index].position.x = x + ((vertex_index == 1 || vertex_index == 2) ? size : 0.0f);
		decals[index].vertices[vertex_index].position.y = y + (vertex_index >= 2 ? size : 0.0f);
		decals[index].vertices[vertex_index].position.z = -1.0f;
		decals[index].vertices[vertex_index].texcoord = 0;
	}
	return index;
}

static void add_all(void)
{
	long index;

	for (index = 0; index < decal_count; index++)
	{
		rasterizer_decal_order_add(index, decals[index].framebuffer_blend_function, decals[index].bitmap_group_index,
			decals[index].bitmap_index, decals[index].vertices, 4);
	}
	rasterizer_decals_draw_ordered();
}

static boolean behind(long index)
{
	short vertex_index;

	for (vertex_index = 0; vertex_index < 4; vertex_index++)
	{
		if (decals[index].vertices[vertex_index].position.z >= 0.0f)
			return TRUE;
	}
	return FALSE;
}

static void screen_rectangle(long index, real *x0, real *x1, real *y0, real *y1)
{
	short vertex_index;

	*x0 = *y0 = 1e30f;
	*x1 = *y1 = -1e30f;
	for (vertex_index = 0; vertex_index < 4; vertex_index++)
	{
		real_point3d const *p = &decals[index].vertices[vertex_index].position;
		real x = p->x / -p->z, y = p->y / -p->z;

		*x0 = MIN(*x0, x);
		*x1 = MAX(*x1, x);
		*y0 = MIN(*y0, y);
		*y1 = MAX(*y1, y);
	}
}

/* the rule: whether a must be drawn before b, of two decals added in that order */
static boolean ordered(long a, long b)
{
	real ax0, ax1, ay0, ay1, bx0, bx1, by0, by1;
	short function = decals[a].framebuffer_blend_function;

	if (function == decals[b].framebuffer_blend_function && local_layer != _decal_layer_alpha_tested &&
		(function == _shader_framebuffer_blend_function_multiply || function == _shader_framebuffer_blend_function_add ||
		function == _shader_framebuffer_blend_function_reverse_subtract ||
		function == _shader_framebuffer_blend_function_min || function == _shader_framebuffer_blend_function_max))
	{
		return FALSE;
	}
	if (!global_window_parameters.frustum.projection_valid || behind(a) || behind(b))
		return TRUE;
	screen_rectangle(a, &ax0, &ax1, &ay0, &ay1);
	screen_rectangle(b, &bx0, &bx1, &by0, &by1);
	return ax0 <= bx1 + TEST_ROUNDING && bx0 <= ax1 + TEST_ROUNDING &&
		ay0 <= by1 + TEST_ROUNDING && by0 <= ay1 + TEST_ROUNDING;
}

/* every decal drawn once, every pair the rule orders in its order; the draws
(changes of blend function or bitmap along the order) */
static long check_order(void)
{
	static long position[TEST_MAXIMUM_DECALS];
	long index, other, draws = 0;

	CHECK(drawn_count == decal_count, "%ld drawn of %ld", drawn_count, decal_count);
	for (index = 0; index < decal_count; index++)
		position[index] = NONE;
	for (index = 0; index < drawn_count; index++)
	{
		CHECK(drawn[index] >= 0 && drawn[index] < decal_count && position[drawn[index]] == NONE,
			"decal %ld drawn twice or unknown", drawn[index]);
		position[drawn[index]] = index;
		if (index == 0 ||
			decals[drawn[index]].framebuffer_blend_function != decals[drawn[index - 1]].framebuffer_blend_function ||
			decals[drawn[index]].bitmap_group_index != decals[drawn[index - 1]].bitmap_group_index ||
			decals[drawn[index]].bitmap_index != decals[drawn[index - 1]].bitmap_index)
		{
			draws++;
		}
	}
	for (index = 0; index < decal_count; index++)
	{
		for (other = index + 1; other < decal_count; other++)
		{
			CHECK(!ordered(index, other) || position[index] < position[other],
				"decal %ld (drawn %ld) must stay before decal %ld (drawn %ld)", index, position[index], other, position[other]);
		}
	}
	return draws;
}

static unsigned long random_state = 12345;

static real random_real(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (real)((random_state >> 8) & 0xFFFF) / 65536.0f;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	long index, draws;

	camera();
	local_layer = _decal_layer_primary;

	/* bullet holes about the screen, four bitmaps picked in turn: a draw a bitmap */
	CASE("scattered-bullet-holes")
	{
		for (index = 0; index < 64; index++)
			decal(_shader_framebuffer_blend_function_double_multiply, 7, (byte)(index % 4), -0.9f + 0.22f * (index % 8), -0.9f + 0.22f * (index / 8), 0.05f);
		add_all();
		draws = check_order();
		CHECK(draws == 4, "%ld draws for four bitmaps", draws);
		return 0;
	}
	/* bullet holes on one spot stay in the order they were made */
	CASE("stacked-bullet-holes")
	{
		for (index = 0; index < 16; index++)
			decal(_shader_framebuffer_blend_function_double_multiply, 7, (byte)(index % 2), 0.0f, 0.0f, 0.1f);
		add_all();
		draws = check_order();
		CHECK(draws == 16, "%ld draws", draws);
		return 0;
	}
	/* two decals nearer than the margin are as good as overlapping */
	CASE("rounding")
	{
		decal(_shader_framebuffer_blend_function_double_multiply, 7, 0, -0.5f, 0.0f, 0.1f);
		decal(_shader_framebuffer_blend_function_double_multiply, 7, 1, 0.3f, 0.0f, 0.1f);
		decal(_shader_framebuffer_blend_function_double_multiply, 7, 0, 0.401f, 0.0f, 0.1f);
		add_all();
		draws = check_order();
		CHECK(draws == 3, "%ld draws", draws);
		return 0;
	}
	/* different blend functions do not commute with each other */
	CASE("multiply-then-add")
	{
		decal(_shader_framebuffer_blend_function_add, 1, 0, 0.0f, 0.0f, 0.1f);
		decal(_shader_framebuffer_blend_function_multiply, 2, 0, 0.0f, 0.0f, 0.1f);
		decal(_shader_framebuffer_blend_function_add, 1, 0, 0.0f, 0.0f, 0.1f);
		add_all();
		draws = check_order();
		CHECK(draws == 3, "%ld draws", draws);
		return 0;
	}
	/* the same blend function that commutes: a draw a bitmap, overlapping or not */
	CASE("same-function-commutes")
	{
		for (index = 0; index < 8; index++)
			decal(_shader_framebuffer_blend_function_multiply, 3, (byte)(index % 2), 0.0f, 0.0f, 0.1f);
		add_all();
		draws = check_order();
		CHECK(draws == 2, "%ld draws", draws);
		return 0;
	}
	/* the alpha-tested layer writes the stencil: overlapping decals keep their order */
	CASE("alpha-tested")
	{
		local_layer = _decal_layer_alpha_tested;
		for (index = 0; index < 8; index++)
			decal(_shader_framebuffer_blend_function_multiply, 3, (byte)(index % 2), 0.0f, 0.0f, 0.1f);
		add_all();
		draws = check_order();
		CHECK(draws == 8, "%ld draws", draws);
		return 0;
	}
	/* a decal with a corner behind the camera may cover any of the screen */
	CASE("behind-camera")
	{
		decal(_shader_framebuffer_blend_function_double_multiply, 7, 0, 0.0f, 2.0f, 0.1f);
		index = decal(_shader_framebuffer_blend_function_double_multiply, 7, 1, 5.0f, 0.0f, 1.0f);
		decals[index].vertices[2].position.y = -0.5f;
		decals[index].vertices[2].position.z = 1.0f;
		decals[index].vertices[3].position.y = -0.5f;
		decals[index].vertices[3].position.z = 1.0f;
		decal(_shader_framebuffer_blend_function_double_multiply, 7, 0, 0.0f, 2.0f, 0.1f);
		add_all();
		draws = check_order();
		CHECK(draws == 3, "%ld draws", draws);
		return 0;
	}
	/* without a projection nothing is known of where the decals are */
	CASE("invalid-projection")
	{
		global_window_parameters.frustum.projection_valid = FALSE;
		for (index = 0; index < 64; index++)
			decal(_shader_framebuffer_blend_function_double_multiply, 7, (byte)(index % 4), -0.9f + 0.22f * (index % 8), -0.9f + 0.22f * (index / 8), 0.05f);
		add_all();
		draws = check_order();
		CHECK(draws == 64, "%ld draws", draws);
		return 0;
	}
	/* more decals than a layer's: those so far are drawn, then the rest */
	CASE("full")
	{
		for (index = 0; index < DECAL_ORDER_MAXIMUM_DECALS + 100; index++)
			decal(_shader_framebuffer_blend_function_double_multiply, 7, (byte)(index % 4), random_real() * 2.0f - 1.0f, random_real() * 2.0f - 1.0f, 0.02f);
		add_all();
		check_order();
		CHECK(flush_count == 2, "%ld flushes", flush_count);
		for (index = 0; index < DECAL_ORDER_MAXIMUM_DECALS; index++)
			CHECK(drawn[index] < DECAL_ORDER_MAXIMUM_DECALS, "decal %ld drawn among the first", drawn[index]);
		return 0;
	}
	/* a decal tested against more than the bound starts a batch of its own */
	CASE("bounded")
	{
		decal(_shader_framebuffer_blend_function_double_multiply, 1, 0, -0.9f, -0.9f, 0.001f);
		for (index = 0; index < DECAL_ORDER_MAXIMUM_TESTS + 10; index++)
			decal(_shader_framebuffer_blend_function_double_multiply, 2 + index, 0, 0.5f, 0.5f, 0.001f);
		decal(_shader_framebuffer_blend_function_double_multiply, 1, 0, -0.9f, -0.9f, 0.001f);
		add_all();
		draws = check_order();
		CHECK(drawn[drawn_count - 1] == decal_count - 1, "the last decal joined a batch past the bound");
		return 0;
	}
	/* a fight's worth of decals of every blend function, some behind the camera */
	CASE("random")
	{
		long before = 0;

		for (index = 0; index < 1500; index++)
		{
			long made = decal((short)(random_real() * 8.0f), 1 + (long)(random_real() * 3.0f), (byte)(random_real() * 4.0f),
				random_real() * 2.4f - 1.2f, random_real() * 2.4f - 1.2f, 0.01f + random_real() * 0.1f);

			if (random_real() < 0.02f)
				decals[made].vertices[(long)(random_real() * 4.0f)].position.z = 0.5f;
			if (index == 0 ||
				decals[made].framebuffer_blend_function != decals[made - 1].framebuffer_blend_function ||
				decals[made].bitmap_group_index != decals[made - 1].bitmap_group_index ||
				decals[made].bitmap_index != decals[made - 1].bitmap_index)
			{
				before++;
			}
		}
		add_all();
		draws = check_order();
		CHECK(draws < before, "%ld draws, %ld in the order made", draws, before);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
