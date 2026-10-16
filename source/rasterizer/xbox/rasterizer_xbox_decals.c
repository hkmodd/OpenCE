/*
RASTERIZER_XBOX_DECALS.C

symbols in this file:
0014A5F0 01b0:
	_D3DDevice_SetRenderState (0000)
0014A7A0 0050:
	_D3DDevice_SetTextureStageState (0000)
0014A7F0 0120:
	_rasterizer_decal_vertices_purge_proc (0000)
0014A910 0090:
	_rasterizer_decal_vertices_locked_proc (0000)
0014A9A0 0040:
	__rasterizer_decals_update_function_pointers (0000)
0014A9E0 0220:
	_IDirect3DDevice8_SetRenderState@12 (0000)
0014AC00 0010:
	__rasterizer_decals_initialize_for_new_map (0000)
0014AC10 0040:
	__rasterizer_decals_dispose_from_old_map (0000)
0014AC50 0040:
	__rasterizer_decals_flush (0000)
0014AC90 0010:
	_rasterizer_decal_vertices_begin_update (0000)
0014ACA0 0010:
	_rasterizer_decal_vertices_end_update (0000)
0014ACB0 0070:
	__rasterizer_decal_vertices_new (0000)
0014AD20 0060:
	_IDirect3DDevice8_SetTextureStageState@16 (0000)
0014AD80 0030:
	_IDirect3DDevice8_DrawPrimitive@16 (0000)
0014ADB0 0070:
	__rasterizer_decal_vertices_delete (0000)
0014AE20 0010:
	_IDirect3DDevice8_SetStreamSource@16 (0000)
0014AE30 0070:
	__rasterizer_decals_end (0000)
0014AEA0 0020:
	_IDirect3DDevice8_SetVertexData4ub@24 (0000)
0014AEC0 0010:
	_D3DVertexBuffer_Unlock@4 (0000)
0014AED0 0010:
	_IDirect3DVertexBuffer8_Release@4 (0000)
0014AEE0 0010:
	_IDirect3DVertexBuffer8_Register@8 (0000)
0014AEF0 0020:
	_IDirect3DVertexBuffer8_Lock@20 (0000)
0014AF10 0010:
	_IDirect3DVertexBuffer8_Unlock@4 (0000)
0014AF20 0110:
	__rasterizer_decals_initialize (0000)
0014B030 00b0:
	__rasterizer_decals_dispose (0000)
0014B0E0 00d0:
	__rasterizer_decal_vertices_lock (0000)
0014B1B0 0010:
	__rasterizer_decal_vertices_unlock (0000)
0014B1C0 02d0:
	__rasterizer_decals_begin (0000)
0014B490 0350:
	__rasterizer_decals_draw (0000)
0028DD18 0058:
	_D3DPRIMITIVETOVERTEXCOUNT (0000)
0028DD70 0012:
	??_C@_0BC@JENGFNKJ@decal_index?$CB?$DNNONE?$AA@ (0000)
0028DD88 005d:
	??_C@_0FN@KMDPMKOA@?$CD?$CD?$CD?5ERROR?5decals?3?5deleting?5perma@ (0000)
0028DDE8 005a:
	??_C@_0FK@BJHCLOEJ@?$CD?$CD?$CD?5ERROR?5decals?3?5deleting?5locke@ (0000)
0028DE44 000f:
	??_C@_0P@MKLHCCBN@decal_index?$CB?$DN0?$AA@ (0000)
0028DE54 000c:
	??_C@_0M@LJKLFPHM@decal_index?$AA@ (0000)
0028DE60 0029:
	??_C@_0CJ@FLPDNPAK@lruv_has_locked_proc?$CIlocal_verte@ (0000)
0028DE8C 0038:
	??_C@_0DI@DFPBIFJA@c?3?2halo?2SOURCE?2rasterizer?2xbox?2r@ (0000)
0028DEC4 0013:
	??_C@_0BD@GMDLIMJM@local_vertex_cache?$AA@ (0000)
0028DED8 002a:
	??_C@_0CK@DKKOCKIP@cache_size?$CFsizeof?$CIstruct?5decal_v@ (0000)
0028DF04 0027:
	??_C@_0CH@FLDEKCGM@cache_size?$DOsizeof?$CIstruct?5decal_v@ (0000)
0028DF2C 0012:
	??_C@_0BC@NONEJICN@cache_index?$CB?$DNNONE?$AA@ (0000)
0028DF40 0013:
	??_C@_0BD@NBMEPDOH@decal?5vertex?5cache?$AA@ (0000)
0028DF54 001e:
	??_C@_0BO@OHOOIFNI@local_d3d_vertex_buffer?9?$DOData?$AA@ (0000)
0028DF74 000f:
	??_C@_0P@PKAEFIHO@decal?5vertices?$AA@ (0000)
0028DF84 0018:
	??_C@_0BI@PCGDDGMG@local_d3d_vertex_buffer?$AA@ (0000)
0028DF9C 0032:
	??_C@_0DC@BMDAFKAI@vertex_data_offset?$CFsizeof?$CIstruct@ (0000)
0028DFD0 0022:
	??_C@_0CC@GHCFEIDO@intensity?$DM?$DNPIXEL32_COMPONENT_MAS@ (0000)
0028DFF4 0031:
	??_C@_0DB@GLODBMEM@?$CD?$CD?$CD?5ERROR?5unsupported?5framebuffe@ (0000)
0030CEF4 0004:
	_last_decal_index_queried_by_lruv_cache (0000)
0045E8E8 0002:
	_local_layer (0000)
0045E8EC 0004:
	_rasterizer_decal_cached_bitmap_group_index (0000)
0045E8F0 0002:
	_rasterizer_decal_cached_bitmap_index (0000)
0045E8F4 0002:
	_local_framebuffer_blend_function (0000)
0045E8F8 0004:
	_local_d3d_vertex_buffer (0000)
0045E8FC 0004:
	_local_vertex_cache (0000)
0045E900 0001:
	_locked_decal_reported (0000)
0045E901 0001:
	_permanent_decal_reported (0000)
0045E902 0001:
	_local_filthy_decal_fog_hack_enabled (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "effects/decal_definitions.h"
#include "effects/decals.h"
#include "math/integer_math.h"
#include "memory/data.h"
#include "memory/lruv_cache.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_console_vars.h"
/* The XDK's stock D3DINLINE (static __forceinline) definitions supply both the
 * inline expansions used below and the out-of-line wrapper bodies January
 * retains in this object. Do not redefine D3DINLINE, take a wrapper's address
 * or hand-write a wrapper body: any of those changes the emitted ABI. */
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/xbox/rasterizer_xbox_pixel_shader.h"
#include "saved games/game_state.h"

/* ---------- constants */

enum
{
	NUMBER_OF_DECAL_LAYERS = 5
};

enum
{
	_decal_layer_primary = 0,
	_decal_layer_secondary,
	_decal_layer_light,
	_decal_layer_alpha_tested,
	_decal_layer_water
};

enum
{
	_decal_locked_bit,
	_decal_permanent_bit
};

enum
{
	_shader_framebuffer_blend_function_alpha_blend = 0,
	_shader_framebuffer_blend_function_multiply,
	_shader_framebuffer_blend_function_double_multiply,
	_shader_framebuffer_blend_function_add,
	_shader_framebuffer_blend_function_reverse_subtract,
	_shader_framebuffer_blend_function_min,
	_shader_framebuffer_blend_function_max,
	_shader_framebuffer_blend_function_alpha_multiply_add,
	NUMBER_OF_SHADER_FRAMEBUFFER_BLEND_FUNCTIONS
};

enum
{
	_rasterizer_drawing_mode_normal = 0
};

enum
{
	_rasterizer_statistics_mode_geometry = 2
};

enum
{
	PIXEL32_COMPONENT_MASK = 0xff
};

enum
{
	RASTERIZER_STENCIL_MODE_REJECT = 2,
	RASTERIZER_STENCIL_MODE_WRITE_ALPHA_TESTED_DECAL = 4
};

enum
{
	MAXIMUM_DECALS_PER_MAP = 2048,
	DECAL_VERTEX_CACHE_PAGE_SIZE_BITS = 6,
	DECAL_VERTEX_CACHE_PAGE_COUNT = 2560,
	DECAL_VERTEX_CACHE_SIZE = DECAL_VERTEX_CACHE_PAGE_COUNT << DECAL_VERTEX_CACHE_PAGE_SIZE_BITS
};

/* ---------- macros */

#define DECAL_GET(index) ((struct decal_datum *)datum_get(global_decal_data, (index)))

/* ---------- structures */

struct decal_vertex
{
	real_point3d position;
	unsigned long texcoord;
};

struct decal_datum
{
	short identifier;
	unsigned short flags;
	short cluster_index;
	short layer;
	real_point3d position;
	long creation_time;
	byte sequence_index;
	byte unused_was_frames_remaining;
	byte sprite_index;
	byte bitmap_index;
	real lifetime;
	real decay_time;
	pixel32 color;
	byte intensity;
	byte unused;
	short quad_count;
	long definition_index;
	long previous_decal_index;
	long next_decal_index;
};

typedef char verify_decal_datum_size[
	sizeof(struct decal_datum) == 0x38 ? 1 : -1];

/* Only the fields the rasterizer touches are recovered; the complete decal
   tag definition is not yet authenticated. */
struct decal_shader_definition
{
	byte reserved0000[4];
	short framebuffer_blend_function;
	byte reserved0006[0x16];
	struct tag_reference map;
};

struct decal_definition
{
	byte reserved0000[0xbc];
	struct decal_shader_definition shader;
};

typedef char verify_decal_definition_framebuffer_blend_function_offset[
	offsetof(
		struct decal_definition,
		shader.framebuffer_blend_function) == 0xc0 ? 1 : -1];
typedef char verify_decal_definition_map_index_offset[
	offsetof(
		struct decal_definition,
		shader.map.index) == 0xe4 ? 1 : -1];

/* ---------- prototypes */

static void rasterizer_decal_vertices_purge_proc(
	long decal_index);
static boolean rasterizer_decal_vertices_locked_proc(
	long decal_index);

/* ---------- globals */

extern struct data_array *global_decal_data;
extern D3DDevice *global_d3d_device;
static short local_layer = 0;
static long rasterizer_decal_cached_bitmap_group_index = 0;
static short rasterizer_decal_cached_bitmap_index = 0;
static short local_framebuffer_blend_function = 0;
static D3DVertexBuffer *local_d3d_vertex_buffer = NULL;
static struct lruv_cache *local_vertex_cache = NULL;
static boolean locked_decal_reported = FALSE;
static boolean permanent_decal_reported = FALSE;
static boolean local_filthy_decal_fog_hack_enabled = FALSE;
extern struct pixel_shader_definition pixel_shader;

static long last_decal_index_queried_by_lruv_cache = NONE;

/* port: the decals drawn one after another with the same blend function and
bitmap are one draw, a list of their quads with each decal's color in its
vertices, instead of a draw each. A long fight leaves hundreds of decals in
view, and a draw is a call into the browser on the web and work for the
driver everywhere; on the Xbox each was a few words of push buffer. The
vertices are the decal's own (position, then its texture coordinates as the
declaration's normalized shorts), given as immediate data. */
#define DECAL_BATCH_MAXIMUM_DECALS 256
/* (the vertices a draw hands over at once) */
#define DECAL_BATCH_VERTEX_CHUNK 256
static struct
{
	struct decal_vertex const *vertices;
	short vertex_count;
	byte color[4];
} decal_batch[DECAL_BATCH_MAXIMUM_DECALS];
static short decal_batch_count = 0;

static void rasterizer_decals_draw_batch(void);

/* port: the order the decals of a layer are drawn in. Each decal joins the
last batch (decal_batch) of its blend function and bitmap, so long as no
batch after that one holds a decal it must stay after; otherwise it starts
a batch of its own after the others. A decal must stay after an earlier one
unless the two can be drawn in either order: their blend function is the
same and commutes with itself (multiplying, adding, subtracting, the least
or the most of the framebuffer and them comes to the same in any order, but
for rounding in the last bit), except in the alpha-tested layer, which
writes the stencil; or they cover no pixel in common, which their
rectangles on the screen show (rasterizer_decal_screen_bounds). In a fight
most decals are bullet holes, whose blend function (doubling the product)
does not commute, and a definition picks among its bitmaps at random: in
the order they were made, one decal after another changed bitmap. Every
pair of decals that cover a pixel in common is drawn in the order it was
before, so the picture is the same. */
#define DECAL_ORDER_MAXIMUM_DECALS MAXIMUM_DECALS_PER_MAP
/* (the decals and batches a decal is tested against, past which it starts a
batch of its own) */
#define DECAL_ORDER_MAXIMUM_TESTS 256
/* (each rectangle is widened by this much of the normalized device
coordinates, 1/128 of the screen each way, which the rounding of
rasterization is well inside) */
#define DECAL_ORDER_SCREEN_MARGIN (1.0f / 64.0f)
/* (a vertex nearer the camera's plane than this, in w, makes its decal's
rectangle the whole plane) */
#define DECAL_ORDER_MINIMUM_W 0.0001f
static struct
{
	long decal_index;
	long next_entry_index;
	real_rectangle2d bounds;
} decal_order_entries[DECAL_ORDER_MAXIMUM_DECALS];
static struct
{
	short framebuffer_blend_function;
	long bitmap_group_index;
	byte bitmap_index;
	long first_entry_index;
	long last_entry_index;
	real_rectangle2d bounds;
} decal_order_batches[DECAL_ORDER_MAXIMUM_DECALS];
static long decal_order_entry_count = 0;
static long decal_order_batch_count = 0;
static long decal_draw_order[DECAL_ORDER_MAXIMUM_DECALS];

static void rasterizer_decals_draw_ordered(void);

static boolean rasterizer_decal_blend_function_commutes(
	short framebuffer_blend_function)
{
	return framebuffer_blend_function == _shader_framebuffer_blend_function_multiply ||
		framebuffer_blend_function == _shader_framebuffer_blend_function_add ||
		framebuffer_blend_function == _shader_framebuffer_blend_function_min ||
		framebuffer_blend_function == _shader_framebuffer_blend_function_max ||
		framebuffer_blend_function == _shader_framebuffer_blend_function_reverse_subtract;
}

/* the rectangle the vertices cover on the screen, in normalized device
coordinates, widened by DECAL_ORDER_SCREEN_MARGIN: the window's world to
view and projection, as rasterizer_set_view_transform gives the vertex
shaders. A quad seen from in front covers only the rectangle of its
corners; one with a corner near or behind the camera may cover any of the
screen. */
static void rasterizer_decal_screen_bounds(
	struct decal_vertex const *vertices,
	short vertex_count,
	real_rectangle2d *bounds)
{
	real_matrix4x3 const *world_to_view = &global_window_parameters.frustum.world_to_view;
	real (*projection)[4] = global_window_parameters.frustum.projection_matrix;
	short vertex_index;

	bounds->x0 = REAL_MAX;
	bounds->x1 = REAL_MIN;
	bounds->y0 = REAL_MAX;
	bounds->y1 = REAL_MIN;
	for (vertex_index = 0; vertex_index < vertex_count; vertex_index++)
	{
		real_point3d const *point = &vertices[vertex_index].position;
		real view[3];
		real x, y, w;
		short axis;

		for (axis = 0; axis < 3; axis++)
		{
			view[axis] = point->x * world_to_view->n[0][axis] + point->y * world_to_view->n[1][axis] +
				point->z * world_to_view->n[2][axis] + world_to_view->n[3][axis];
		}
		w = view[0] * projection[0][3] + view[1] * projection[1][3] + view[2] * projection[2][3] + projection[3][3];
		if (!global_window_parameters.frustum.projection_valid || !(w > DECAL_ORDER_MINIMUM_W))
		{
			bounds->x0 = REAL_MIN;
			bounds->x1 = REAL_MAX;
			bounds->y0 = REAL_MIN;
			bounds->y1 = REAL_MAX;
			return;
		}
		x = (view[0] * projection[0][0] + view[1] * projection[1][0] + view[2] * projection[2][0] + projection[3][0]) / w;
		y = (view[0] * projection[0][1] + view[1] * projection[1][1] + view[2] * projection[2][1] + projection[3][1]) / w;
		bounds->x0 = MIN(bounds->x0, x);
		bounds->x1 = MAX(bounds->x1, x);
		bounds->y0 = MIN(bounds->y0, y);
		bounds->y1 = MAX(bounds->y1, y);
	}
	bounds->x0 -= DECAL_ORDER_SCREEN_MARGIN;
	bounds->x1 += DECAL_ORDER_SCREEN_MARGIN;
	bounds->y0 -= DECAL_ORDER_SCREEN_MARGIN;
	bounds->y1 += DECAL_ORDER_SCREEN_MARGIN;

	return;
}

static boolean rasterizer_decal_rectangles_overlap(
	real_rectangle2d const *a,
	real_rectangle2d const *b)
{
	return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

/* the decal into its batch (decal_order_batches), as the first comment above
says; vertices NULL when they cannot be read, which lets it pass nothing */
static void rasterizer_decal_order_add(
	long decal_index,
	short framebuffer_blend_function,
	long bitmap_group_index,
	byte bitmap_index,
	struct decal_vertex const *vertices,
	short vertex_count)
{
	boolean commutes = local_layer != _decal_layer_alpha_tested &&
		rasterizer_decal_blend_function_commutes(framebuffer_blend_function);
	long entry_index, batch_index, tests = 0;

	/* (more than a layer's decals cannot come: those so far are drawn first) */
	if (decal_order_entry_count == DECAL_ORDER_MAXIMUM_DECALS)
		rasterizer_decals_draw_ordered();
	entry_index = decal_order_entry_count++;
	decal_order_entries[entry_index].decal_index = decal_index;
	decal_order_entries[entry_index].next_entry_index = NONE;
	if (vertices)
	{
		rasterizer_decal_screen_bounds(vertices, vertex_count, &decal_order_entries[entry_index].bounds);
	}
	else
	{
		decal_order_entries[entry_index].bounds.x0 = REAL_MIN;
		decal_order_entries[entry_index].bounds.x1 = REAL_MAX;
		decal_order_entries[entry_index].bounds.y0 = REAL_MIN;
		decal_order_entries[entry_index].bounds.y1 = REAL_MAX;
	}

	for (batch_index = decal_order_batch_count - 1; batch_index >= 0; batch_index--)
	{
		long other_index;

		if (decal_order_batches[batch_index].framebuffer_blend_function == framebuffer_blend_function &&
			decal_order_batches[batch_index].bitmap_group_index == bitmap_group_index &&
			decal_order_batches[batch_index].bitmap_index == bitmap_index)
		{
			break;
		}
		if (commutes && decal_order_batches[batch_index].framebuffer_blend_function == framebuffer_blend_function)
			continue;
		if (++tests > DECAL_ORDER_MAXIMUM_TESTS)
		{
			batch_index = NONE;
			break;
		}
		if (!rasterizer_decal_rectangles_overlap(&decal_order_entries[entry_index].bounds,
			&decal_order_batches[batch_index].bounds))
		{
			continue;
		}
		for (other_index = decal_order_batches[batch_index].first_entry_index;
			other_index != NONE;
			other_index = decal_order_entries[other_index].next_entry_index)
		{
			if (++tests > DECAL_ORDER_MAXIMUM_TESTS ||
				rasterizer_decal_rectangles_overlap(&decal_order_entries[entry_index].bounds,
					&decal_order_entries[other_index].bounds))
			{
				break;
			}
		}
		if (other_index != NONE)
		{
			batch_index = NONE;
			break;
		}
	}

	if (batch_index == NONE)
	{
		batch_index = decal_order_batch_count++;
		decal_order_batches[batch_index].framebuffer_blend_function = framebuffer_blend_function;
		decal_order_batches[batch_index].bitmap_group_index = bitmap_group_index;
		decal_order_batches[batch_index].bitmap_index = bitmap_index;
		decal_order_batches[batch_index].first_entry_index = entry_index;
		decal_order_batches[batch_index].bounds = decal_order_entries[entry_index].bounds;
	}
	else
	{
		real_rectangle2d *bounds = &decal_order_batches[batch_index].bounds;

		decal_order_entries[decal_order_batches[batch_index].last_entry_index].next_entry_index = entry_index;
		bounds->x0 = MIN(bounds->x0, decal_order_entries[entry_index].bounds.x0);
		bounds->x1 = MAX(bounds->x1, decal_order_entries[entry_index].bounds.x1);
		bounds->y0 = MIN(bounds->y0, decal_order_entries[entry_index].bounds.y0);
		bounds->y1 = MAX(bounds->y1, decal_order_entries[entry_index].bounds.y1);
	}
	decal_order_batches[batch_index].last_entry_index = entry_index;

	return;
}

/* ---------- public code */

void rasterizer_decal_vertices_end_update(
	void)
{
	return;
}

void *_rasterizer_decal_vertices_lock(
	long cache_index,
	long cache_size)
{
	byte *vertex_data = NULL;
	unsigned long vertex_data_offset;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		217,
		cache_index!=NONE);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		218,
		local_vertex_cache);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		219,
		global_d3d_device);
	vertex_data_offset = (unsigned long)lruv_block_get_address(
		local_vertex_cache,
		cache_index);
	rasterizer_globals.current_lock_operation = _rasterizer_lock_decal_vertices;
	IDirect3DVertexBuffer8_Lock(
		local_d3d_vertex_buffer,
		vertex_data_offset,
		cache_size,
		&vertex_data,
		D3DLOCK_READONLY);
	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;

	return vertex_data;
}

void _rasterizer_decal_vertices_unlock(
	void)
{
	IDirect3DVertexBuffer8_Unlock(local_d3d_vertex_buffer);

	return;
}

void _rasterizer_decals_initialize_for_new_map(
	void)
{
	return;
}

void _rasterizer_decals_dispose_from_old_map(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		131,
		local_vertex_cache);
	decals_unlock(TRUE);
	lruv_flush(local_vertex_cache);

	return;
}

void rasterizer_decal_vertices_begin_update(
	void)
{
	lruv_idle(local_vertex_cache);

	return;
}

void _rasterizer_decals_update_function_pointers(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		116,
		local_vertex_cache);
	lruv_update_function_pointers(
		local_vertex_cache,
		rasterizer_decal_vertices_purge_proc,
		rasterizer_decal_vertices_locked_proc);

	return;
}

void _rasterizer_decals_initialize(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		89,
		global_d3d_device);
	local_d3d_vertex_buffer = match_malloc(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		91,
		sizeof(D3DVertexBuffer));
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		92,
		local_d3d_vertex_buffer);
	local_d3d_vertex_buffer->Common = 1;
	local_d3d_vertex_buffer->Data = (unsigned long)game_state_gpu_malloc(
		"decal vertices",
		NULL,
		DECAL_VERTEX_CACHE_SIZE);
	local_d3d_vertex_buffer->Lock = 0;
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		96,
		local_d3d_vertex_buffer->Data);
	IDirect3DVertexBuffer8_Register(
		local_d3d_vertex_buffer,
		NULL);
	local_vertex_cache = game_state_lruv_cache_new(
		"decal vertex cache",
		DECAL_VERTEX_CACHE_PAGE_COUNT,
		DECAL_VERTEX_CACHE_PAGE_SIZE_BITS,
		MAXIMUM_DECALS_PER_MAP,
		rasterizer_decal_vertices_purge_proc,
		rasterizer_decal_vertices_locked_proc);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		106,
		local_vertex_cache);

	return;
}

void _rasterizer_decals_dispose(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		0x99,
		local_vertex_cache);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		0x9A,
		local_d3d_vertex_buffer);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		0x9B,
		global_d3d_device);
	if (local_d3d_vertex_buffer)
	{
		IDirect3DVertexBuffer8_Release(local_d3d_vertex_buffer);
		local_d3d_vertex_buffer = NULL;
	}
	lruv_delete(local_vertex_cache);

	return;
}

long _rasterizer_decal_vertices_new(
	long cache_size)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		204,
		cache_size>sizeof(struct decal_vertex));
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		205,
		cache_size%sizeof(struct decal_vertex)==0);

	return lruv_block_new(local_vertex_cache, cache_size);
}

void _rasterizer_decal_vertices_delete(
	long cache_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		0x106,
		cache_index!=NONE);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		0x107,
		local_vertex_cache);
	lruv_block_delete(local_vertex_cache, cache_index);

	return;
}

void _rasterizer_decals_begin(
	short layer)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		283,
		global_d3d_device);

	{
		short decal_layer_profiles[NUMBER_OF_DECAL_LAYERS] =
		{
			_rasterizer_profile_environment_decals_primary,
			_rasterizer_profile_environment_decals_secondary,
			_rasterizer_profile_environment_decals_light,
			_rasterizer_profile_environment_decals_alpha_tested,
			_rasterizer_profile_environment_decals_water
		};

		if (layer >= 0 && layer < NUMBER_OF_DECAL_LAYERS)
			rasterizer_profile_begin(decal_layer_profiles[layer]);
	}

	local_layer = layer;
	if (rasterizer_debug_options.drawing_mode != _rasterizer_drawing_mode_normal)
		return;
	if (!rasterizer_debug_options.draw_environment_decals)
		return;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		307,
		layer>=0 && layer<NUMBER_OF_DECAL_LAYERS);
	local_framebuffer_blend_function = NONE;
	rasterizer_decal_cached_bitmap_index = NONE;
	rasterizer_decal_cached_bitmap_group_index = NONE;
	local_filthy_decal_fog_hack_enabled = FALSE;
	rasterizer_set_texture(0, 0, 1, NONE, 0);
	IDirect3DDevice8_SetTextureStageState(
		global_d3d_device,
		0,
		D3DTSS_ADDRESSU,
		D3DTADDRESS_CLAMP);
	IDirect3DDevice8_SetTextureStageState(
		global_d3d_device,
		0,
		D3DTSS_ADDRESSV,
		D3DTADDRESS_CLAMP);
	IDirect3DDevice8_SetTextureStageState(
		global_d3d_device,
		0,
		D3DTSS_MAGFILTER,
		D3DTEXF_LINEAR);
	IDirect3DDevice8_SetTextureStageState(
		global_d3d_device,
		0,
		D3DTSS_MINFILTER,
		D3DTEXF_LINEAR);
	IDirect3DDevice8_SetTextureStageState(
		global_d3d_device,
		0,
		D3DTSS_MIPFILTER,
		D3DTEXF_LINEAR);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_CULLMODE,
		D3DCULL_CCW);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_ALPHABLENDENABLE,
		TRUE);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_ZENABLE,
		TRUE);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_ZWRITEENABLE,
		FALSE);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_ZFUNC,
		D3DCMP_LESSEQUAL);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_ZBIAS,
		rasterizer_debug_options.zbias);
	if (layer == _decal_layer_alpha_tested)
	{
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ALPHATESTENABLE,
			TRUE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ALPHAREF,
			0x7f);
		rasterizer_set_stencil_mode(RASTERIZER_STENCIL_MODE_WRITE_ALPHA_TESTED_DECAL);
	}
	else
	{
		if (rasterizer_debug_options.filthy_decal_fog_hack_enabled &&
			global_window_parameters.fog.atmospheric_maximum_density == 1.0f)
			local_filthy_decal_fog_hack_enabled = TRUE;
		if (local_filthy_decal_fog_hack_enabled)
		{
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHATESTENABLE,
				TRUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHAREF,
				0);
		}
		else
		{
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHATESTENABLE,
				FALSE);
		}
	}
	rasterizer_set_vertex_shader_permutation(1, 10, 0);
	csmemset(&pixel_shader, 0, sizeof(pixel_shader));
	pixel_shader.texture_modes = 1;
	pixel_shader.rgb_outputs[0] = 0xc00;
	pixel_shader.alpha_outputs[1] = 0xc00;
	pixel_shader.rgb_outputs[1] = 0xc00;
	if (local_filthy_decal_fog_hack_enabled)
	{
		pixel_shader.combiner_count = 3;
		pixel_shader.constant_0[0] = 0x1000000;
		pixel_shader.alpha_inputs[2] = 0x1c151115;
		pixel_shader.alpha_outputs[2] = 0xc00;
	}
	else
	{
		pixel_shader.combiner_count = 2;
	}
	pixel_shader.final_combiner_inputs_abcd = 0xc;
	pixel_shader.final_combiner_inputs_efg = 0x1c00;
	IDirect3DDevice8_SetStreamSource(
		global_d3d_device,
		0,
		local_d3d_vertex_buffer,
		sizeof(struct decal_vertex));

	return;
}

void _rasterizer_decals_draw(
	short cluster_index)
{
	long decal_index;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		408,
		global_d3d_device);
	if (rasterizer_debug_options.drawing_mode != _rasterizer_drawing_mode_normal)
		return;
	if (!rasterizer_debug_options.draw_environment_decals)
		return;

	/* port: into the layer's order (rasterizer_decal_order_add), which
	_rasterizer_decals_end draws */
	{
		byte *vertex_data;
		long count = 0;

		IDirect3DVertexBuffer8_Lock(local_d3d_vertex_buffer, 0, 0, &vertex_data, D3DLOCK_READONLY);
		for (decal_index = decal_get_first_decal_index(cluster_index, local_layer);
			decal_index != NONE && count < MAXIMUM_DECALS_PER_MAP;
			decal_index = DECAL_GET(decal_index)->next_decal_index)
		{
			struct decal_datum *decal = DECAL_GET(decal_index);
			struct decal_definition *definition = decal_definition_get(decal->definition_index);

			rasterizer_decal_order_add(
				decal_index,
				definition->shader.framebuffer_blend_function,
				definition->shader.map.index,
				decal->bitmap_index,
				vertex_data ?
					(struct decal_vertex const *)(vertex_data +
						(unsigned long)lruv_block_get_address(local_vertex_cache, decal_index)) :
					NULL,
				(short)(4 * decal->quad_count));
			count++;
		}
	}

	return;
}

/* port: draws the layer's decals in their order (rasterizer_decal_order_add),
as _rasterizer_decals_draw drew a cluster's */
static void rasterizer_decals_draw_ordered(
	void)
{
	long decal_index;
	long decal_count = 0, decal_order_index, batch_index, entry_index;

	for (batch_index = 0; batch_index < decal_order_batch_count; batch_index++)
	{
		for (entry_index = decal_order_batches[batch_index].first_entry_index;
			entry_index != NONE;
			entry_index = decal_order_entries[entry_index].next_entry_index)
		{
			decal_draw_order[decal_count++] = decal_order_entries[entry_index].decal_index;
		}
	}
	decal_order_entry_count = 0;
	decal_order_batch_count = 0;
	for (decal_order_index = 0; decal_order_index < decal_count; decal_order_index++)
	{
		struct decal_datum *decal = DECAL_GET(decal_index = decal_draw_order[decal_order_index]);
		struct decal_definition *definition = decal_definition_get(decal->definition_index);
		struct decal_shader_definition *shader = &definition->shader;
		short framebuffer_blend_function = shader->framebuffer_blend_function;
		unsigned long vertex_data_offset;
		pixel32 color;
		unsigned long intensity;

		if (local_framebuffer_blend_function != framebuffer_blend_function)
		{
			rasterizer_decals_draw_batch(); /* port: (decal_batch) */
			local_framebuffer_blend_function = framebuffer_blend_function;
			if (framebuffer_blend_function == _shader_framebuffer_blend_function_multiply ||
				framebuffer_blend_function == _shader_framebuffer_blend_function_double_multiply)
			{
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_COLORWRITEENABLE,
					D3DCOLORWRITEENABLE_ALL);
			}
			else
			{
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_COLORWRITEENABLE,
					D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE);
			}
			switch (local_framebuffer_blend_function)
			{
				case _shader_framebuffer_blend_function_add:
				case _shader_framebuffer_blend_function_reverse_subtract:
				case _shader_framebuffer_blend_function_max:
					pixel_shader.rgb_inputs[0] = 0x08040000;
					pixel_shader.rgb_inputs[1] = 0x340c0000;
					break;

				case _shader_framebuffer_blend_function_multiply:
				case _shader_framebuffer_blend_function_min:
					pixel_shader.rgb_inputs[0] = 0x28240820;
					pixel_shader.rgb_inputs[1] = 0x340c1420;
					pixel_shader.alpha_inputs[1] = 0x341c1420;
					break;

				case _shader_framebuffer_blend_function_double_multiply:
					pixel_shader.rgb_inputs[0] = 0xa8240820;
					pixel_shader.rgb_inputs[1] = 0x340c14a0;
					pixel_shader.alpha_inputs[1] = 0x341c14a0;
					break;

				case _shader_framebuffer_blend_function_alpha_blend:
					pixel_shader.rgb_inputs[0] = 0x08040000;
					pixel_shader.rgb_inputs[1] = 0x200c0000;
					pixel_shader.alpha_inputs[1] = 0x34180000;
					break;

				case _shader_framebuffer_blend_function_alpha_multiply_add:
					pixel_shader.rgb_inputs[0] = 0x08040000;
					pixel_shader.rgb_inputs[1] = 0x340c0000;
					pixel_shader.alpha_inputs[1] = 0x34180000;
					break;

				default:
					match_vassert(
						"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
						470,
						FALSE,
						"### ERROR unsupported framebuffer blend function");
					break;
			}
			rasterizer_set_framebuffer_blend_function(local_framebuffer_blend_function);
			rasterizer_set_pixel_shader(&pixel_shader);
			if (rasterizer_debug_options.statistics_mode == _rasterizer_statistics_mode_geometry)
				rasterizer_frame_statistics.decal_shader_change_count++;
		}

		if (rasterizer_decal_cached_bitmap_group_index != shader->map.index ||
			rasterizer_decal_cached_bitmap_index != (char)decal->bitmap_index)
		{
			rasterizer_decals_draw_batch(); /* port: (decal_batch) */
			rasterizer_decal_cached_bitmap_group_index = shader->map.index;
			rasterizer_decal_cached_bitmap_index = (char)decal->bitmap_index;
			rasterizer_set_texture(
				0,
				0,
				1,
				rasterizer_decal_cached_bitmap_group_index,
				rasterizer_decal_cached_bitmap_index);
			if (rasterizer_debug_options.statistics_mode == _rasterizer_statistics_mode_geometry)
				rasterizer_frame_statistics.decal_texture_change_count++;
		}

		vertex_data_offset = (unsigned long)lruv_block_get_address(local_vertex_cache, decal_index);
		color = decal->color;
		intensity = (decal->intensity * (color >> 24) + 127) >> 8;
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
			510,
			intensity<=PIXEL32_COMPONENT_MASK);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
			511,
			vertex_data_offset%sizeof(struct decal_vertex)==0);
		/* port: batched (decal_batch), in place of
		SetVertexData4ub(9, color) and DrawPrimitive(D3DPT_QUADLIST) */
		{
			byte *vertex_data;

			IDirect3DVertexBuffer8_Lock(local_d3d_vertex_buffer, 0, 0, &vertex_data, D3DLOCK_READONLY);
			if (vertex_data)
			{
				if (decal_batch_count == DECAL_BATCH_MAXIMUM_DECALS)
					rasterizer_decals_draw_batch();
				decal_batch[decal_batch_count].vertices = (struct decal_vertex const *)(vertex_data + vertex_data_offset);
				decal_batch[decal_batch_count].vertex_count = (short)(4 * decal->quad_count);
				decal_batch[decal_batch_count].color[0] = (byte)(color >> 16);
				decal_batch[decal_batch_count].color[1] = (byte)(color >> 8);
				decal_batch[decal_batch_count].color[2] = (byte)color;
				decal_batch[decal_batch_count].color[3] = (byte)(PIXEL32_COMPONENT_MASK - intensity);
				decal_batch_count++;
			}
		}
		if (rasterizer_debug_options.statistics_mode == _rasterizer_statistics_mode_geometry)
		{
			rasterizer_frame_statistics.decal_draw_count++;
			rasterizer_frame_statistics.decal_triangle_count += 2*decal->quad_count;
			rasterizer_frame_statistics.decal_vertex_count += 4*decal->quad_count;
		}

	}

	return;
}

/* port: draws the decals batched (decal_batch) */
static void rasterizer_decals_draw_batch(
	void)
{
	short batch_index;

	/* port: each vertex's registers 0 (its position), 4 (its texture
	coordinates) and 9 (its decal's color), handed over a few hundred at a
	time (halo_immediate_vertices): the values SetVertexData4ub(9),
	SetVertexData2f(4) and SetVertexData4f(0) for each gave, a call each */
	static real vertex_data[DECAL_BATCH_VERTEX_CHUNK][3][4];
	long vertex_data_count = 0;

	if (!decal_batch_count)
		return;
	IDirect3DDevice8_Begin(global_d3d_device, D3DPT_QUADLIST);
	for (batch_index = 0; batch_index < decal_batch_count; batch_index++)
	{
		byte const *color = decal_batch[batch_index].color;
		real red = color[0] / 255.0f, green = color[1] / 255.0f, blue = color[2] / 255.0f, alpha = color[3] / 255.0f;
		short vertex_index;

		for (vertex_index = 0; vertex_index < decal_batch[batch_index].vertex_count; vertex_index++)
		{
			struct decal_vertex const *vertex = &decal_batch[batch_index].vertices[vertex_index];
			real (*data)[4] = vertex_data[vertex_data_count];

			data[0][0] = vertex->position.x;
			data[0][1] = vertex->position.y;
			data[0][2] = vertex->position.z;
			data[0][3] = 1.0f;
			data[1][0] = (real)(short)(vertex->texcoord & 0xFFFF) / 32767.0f;
			data[1][1] = (real)(short)(vertex->texcoord >> 16) / 32767.0f;
			data[1][2] = 0.0f;
			data[1][3] = 1.0f;
			data[2][0] = red;
			data[2][1] = green;
			data[2][2] = blue;
			data[2][3] = alpha;
			if (++vertex_data_count == DECAL_BATCH_VERTEX_CHUNK)
			{
				halo_immediate_vertices((1UL << 0) | (1UL << 4) | (1UL << 9), &vertex_data[0][0][0], vertex_data_count);
				vertex_data_count = 0;
			}
		}
	}
	halo_immediate_vertices((1UL << 0) | (1UL << 4) | (1UL << 9), &vertex_data[0][0][0], vertex_data_count);
	IDirect3DDevice8_End(global_d3d_device);
	decal_batch_count = 0;

	return;
}

void _rasterizer_decals_end(
	void)
{
	rasterizer_decals_draw_ordered(); /* port: (rasterizer_decal_order_add) */
	rasterizer_decals_draw_batch(); /* port: (decal_batch) */
	if (local_layer == _decal_layer_alpha_tested)
		rasterizer_set_stencil_mode(RASTERIZER_STENCIL_MODE_REJECT);

	{
		short decal_layer_profiles[NUMBER_OF_DECAL_LAYERS] =
		{
			_rasterizer_profile_environment_decals_primary,
			_rasterizer_profile_environment_decals_secondary,
			_rasterizer_profile_environment_decals_light,
			_rasterizer_profile_environment_decals_alpha_tested,
			_rasterizer_profile_environment_decals_water
		};

		if (local_layer >= 0 && local_layer < NUMBER_OF_DECAL_LAYERS)
			rasterizer_profile_end(decal_layer_profiles[local_layer]);
	}

	return;
}

void _rasterizer_decals_flush(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		0x8E,
		local_vertex_cache);
	decals_unlock(FALSE);
	lruv_flush(local_vertex_cache);

	return;
}

/* ---------- private code */

static void rasterizer_decal_vertices_purge_proc(
	long decal_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		29,
		lruv_has_locked_proc(local_vertex_cache));
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		30,
		decal_index);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		31,
		decal_index!=NONE);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		32,
		decal_index!=0);
	if (TEST_FLAG(DECAL_GET(decal_index)->flags, _decal_locked_bit) &&
		!locked_decal_reported)
	{
		error(
			2,
			"### ERROR decals: deleting locked decal (#%d, queried=#%d) in rasterizer -- tell Bernie!!",
			decal_index,
			last_decal_index_queried_by_lruv_cache);
		locked_decal_reported = TRUE;
	}
	if (TEST_FLAG(DECAL_GET(decal_index)->flags, _decal_permanent_bit) &&
		!permanent_decal_reported)
	{
		error(
			2,
			"### ERROR decals: deleting permanent decal (#%d, queried=#%d) in rasterizer -- tell Bernie!!",
			decal_index,
			last_decal_index_queried_by_lruv_cache);
		permanent_decal_reported = TRUE;
	}
	decal_delete(decal_index);

	return;
}

static boolean rasterizer_decal_vertices_locked_proc(
	long decal_index)
{
	struct decal_datum *decal;
	boolean locked;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		71,
		decal_index);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		72,
		decal_index!=NONE);
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_decals.c",
		73,
		decal_index!=0);
	decal = DECAL_GET(decal_index);
	locked = TEST_FLAG(decal->flags, _decal_locked_bit) ||
		TEST_FLAG(decal->flags, _decal_permanent_bit);
	last_decal_index_queried_by_lruv_cache = decal_index;

	return locked;
}
