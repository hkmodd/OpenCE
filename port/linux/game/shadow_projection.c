/*
SHADOW_PROJECTION.C

The scissor of a shadow's projection onto the environment: each projection
draws every environment triangle near its object, and changes only the
pixels inside a box around the object, so the draw is cut to that box's
rectangle on the screen (port/linux/src/d3d8_gl.c, halo_scissor_volume). The
pixels it no longer draws are the ones it left as they were: the picture is
the same.

The projection's vertex program (_shadow_vertex_shader_index) gives its
first stage, the shadow's map (128 texels each way, or more, addressed to a
border of zero), u = (1 + forward.d/r)/2 and v = (1 - left.d/r)/2, for d the
position from the shadow's and r the object's radius; and its second, the
globals' linear corner fade (clamped), s = up.d/4r and t = -2 up.d/r. Its
pixel shader's colour is 1 - ((1 - x) + x(1 - y)), for x the vertex's alpha
times one minus the fade's sample and y the map's sample times one minus the
shadow's colour: exactly 0 where x or y is 0 (the sum then rounds to 1
exactly), that is where the fade's sample is 1 or the map's is 0, and the
blend then keeps the pixel (ZERO, INVSRCCOLOR); the depth and stencil are
only tested. So outside the prism
|forward.d|, |left.d| <= r, more than half a map texel beyond it, the map's
sample is its border's 0; and outside the slab -r/2 <= up.d <= 4r the fade's
coordinates are clamped to its corner (width - 1, 0) or (0, height - 1),
which are 1 in the stock globals. The box is the prism and the slab a
twentieth wider, and is used only when the fade's corners are 1.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "shadow_projection.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "bitmaps/bitmaps_sampling.h"
#include "game/game_globals.h"
#include "rasterizer/common/rasterizer_common.h"
#include "rasterizer/rasterizer_console_vars.h"

/* (the bitmaps' types, formats and flags, as bitmaps.c numbers them) */
enum
{
	_fade_bitmap_type_2d = 0,
	_fade_bitmap_format_ay8 = 2,
	_fade_bitmap_linear_bit = 4
};

/* the corners of the box outside which a draw changes no pixel, or NULL
(port/linux/src/d3d8_gl.c) */
void halo_scissor_volume(const float *corners);

static boolean shadow_projection_scissor_set = FALSE;

/* whether the fade's two corners the projection clamps to are 1 in every
channel: an intensity texture of one level, whose upload repeats its byte in
each channel */
static boolean shadow_projection_fade_saturated(void)
{
	struct bitmap_data *fade = bitmap_group_try_and_get_bitmap(global_rasterizer_data->linear_corner_fade.index, 0);
	real_point2d corner;

	if (!fade || !fade->base_address ||
		fade->type != _fade_bitmap_type_2d ||
		fade->format != _fade_bitmap_format_ay8 ||
		fade->mipmap_count != 0 ||
		TEST_FLAG(fade->flags, _fade_bitmap_linear_bit) ||
		fade->width < 1 || (fade->width & (fade->width - 1)) ||
		fade->height < 1 || (fade->height & (fade->height - 1)))
	{
		return FALSE;
	}
	/* (texel centres: exact at powers of two) */
	corner.x = ((real)fade->width - 0.5f) / (real)fade->width;
	corner.y = 0.5f / (real)fade->height;
	if (bitmap_2d_get_pixel(fade, &corner, 1.0f) != 0xFFFFFFFF)
	{
		return FALSE;
	}
	corner.x = 0.5f / (real)fade->width;
	corner.y = ((real)fade->height - 0.5f) / (real)fade->height;
	return bitmap_2d_get_pixel(fade, &corner, 1.0f) == 0xFFFFFFFF;
}

void shadow_projection_scissor_begin(real_matrix4x3 const *shadow_matrix, real radius)
{
	float corners[8][3];
	real half = radius * 1.05f;
	short corner;

	if (rasterizer_debug_options.shadow_debug_enabled ||
		!(radius > 0.0f && radius < 1.0e30f) ||
		!shadow_projection_fade_saturated())
	{
		return;
	}
	for (corner = 0; corner < 8; corner++)
	{
		real a = (corner & 1) ? half : -half;
		real b = (corner & 2) ? half : -half;
		real c = (corner & 4) ? radius * 4.2f : radius * -0.525f;

		corners[corner][0] = shadow_matrix->position.x + shadow_matrix->forward.i * a + shadow_matrix->left.i * b + shadow_matrix->up.i * c;
		corners[corner][1] = shadow_matrix->position.y + shadow_matrix->forward.j * a + shadow_matrix->left.j * b + shadow_matrix->up.j * c;
		corners[corner][2] = shadow_matrix->position.z + shadow_matrix->forward.k * a + shadow_matrix->left.k * b + shadow_matrix->up.k * c;
	}
	halo_scissor_volume(corners[0]);
	shadow_projection_scissor_set = TRUE;
}

void shadow_projection_scissor_end(void)
{
	if (shadow_projection_scissor_set)
	{
		halo_scissor_volume(NULL);
		shadow_projection_scissor_set = FALSE;
	}
}
