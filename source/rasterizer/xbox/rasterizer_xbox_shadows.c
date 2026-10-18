/*
RASTERIZER_XBOX_SHADOWS.C

symbols in this file:
00161950 01b0:
	_D3DDevice_SetRenderState (0000)
00161B00 0050:
	_D3DDevice_SetTextureStageState (0000)
00161B50 0220:
	_IDirect3DDevice8_SetRenderState@12 (0000)
00161D70 0010:
	__rasterizer_environment_shadows_begin (0000)
00161D80 0060:
	_IDirect3DDevice8_SetTextureStageState@16 (0000)
00161DE0 0010:
	_IDirect3DDevice8_SetVertexShaderConstant@16 (0000)
00161DF0 00a0:
	__rasterizer_environment_shadow_model_begin (0000)
00161E90 0010:
	__rasterizer_environment_shadow_model_end (0000)
00161EA0 0020:
	_IDirect3DDevice8_SetVertexData2f@16 (0000)
00161EC0 0010:
	_IDirect3DDevice8_SetVertexData2s@16 (0000)
00161ED0 0010:
	_IDirect3DDevice8_Begin@8 (0000)
00161EE0 0010:
	_IDirect3DDevice8_End@4 (0000)
00161EF0 0080:
	__rasterizer_environment_shadow_end (0000)
00161F70 0010:
	__rasterizer_environment_shadows_end (0000)
00161F80 0300:
	_rasterizer_shadow_convolve (0000)
00162280 03b0:
	__rasterizer_environment_shadow_begin (0000)
00162630 02b0:
	__rasterizer_environment_shadow_model_draw (0000)
001628E0 05c0:
	__rasterizer_environment_shadow_draw (0000)
002929E0 0039:
	??_C@_0DJ@KIPLBCDL@c?3?2halo?2SOURCE?2rasterizer?2xbox?2r@ (0000)
00292A1C 0027:
	??_C@_0CH@OGPNPLFF@?$CD?$CD?$CD?5WARNING?5empty?5shadow?5has?5bee@ (0000)
00292A44 001c:
	??_C@_0BM@OJNHCFOK@object_bounding_radius?$DO0?40f?$AA@ (0000)
00292A60 0037:
	??_C@_0DH@LMLPNHOO@shadow_color?9?$DOblue?5?$DO?$DN0?40f?5?$CG?$CG?5sha@ (0000)
00292A98 0037:
	??_C@_0DH@OODBCLKP@shadow_color?9?$DOgreen?$DO?$DN0?40f?5?$CG?$CG?5sha@ (0000)
00292AD0 0035:
	??_C@_0DF@OLKILINN@shadow_color?9?$DOred?5?$DO?$DN0?40f?5?$CG?$CG?5shad@ (0000)
00292B08 000d:
	??_C@_0N@NKBPFDCL@shadow_color?$AA@ (0000)
00292B18 000e:
	??_C@_0O@MDCFBAFA@shadow_matrix?$AA@ (0000)
0030CF84 0001:
	_shadow_restored (0000)
0046628C 004a:
	_local_shadow_color (0000)
	_local_object_bounding_radius (000c)
	_local_shadow_matrix (0010)
	_local_parameters (0044)
	_shadow_setup (0048)
	_shadow_used (0049)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "models/model_definitions.h" /* port: the shadow cache */
#include "game/game_globals.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "interface/hud_draw.h"
#include "rasterizer/common/rasterizer_common.h"
#include "rasterizer/rasterizer_geometry.h"
#include "render/render.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"

/* January retains the stock XDK D3DINLINE out-of-line wrappers emitted by
 * the real device calls below. Keep the stock inline definitions and do not
 * replace these wrappers with handwritten bodies. */
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "rasterizer/rasterizer_model_types.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"
#include "rasterizer/xbox/rasterizer_xbox_pixel_shader.h"
#include "rasterizer/xbox/rasterizer_xbox_draw_primitives.h"
#include "shadow_projection.h" /* port: port/linux/game/shadow_projection.c */
#include "shadow_silhouette.h" /* port: port/linux/game/shadow_silhouette.c */

/* ---------- constants */

enum
{
	_rasterizer_target_render_primary = 0,
};

enum
{
	_rasterizer_statistics_mode_enabled = 2,
};

enum
{
	_shader_type_model = 4,
};

enum
{
	_shader_model_detail_after_reflection_bit = 0,
	_shader_model_two_sided_bit,
	_shader_model_not_alpha_tested_bit,
	_shader_model_alpha_blended_decal_bit,
	_shader_model_true_atmospheric_fog_bit,
	_shader_model_nocull_two_sided_bit,
};

enum
{
	RASTERIZER_STENCIL_MODE_NONE = 0,
	RASTERIZER_STENCIL_MODE_REJECT = 2,
};

enum
{
	_shadow_convolution_vertex_shader_index = 0x26,
	_shadow_vertex_shader_index = 0x1d,
	_shadow_model_vertex_shader_index = 0x27,
};

/* ---------- macros */

/* ---------- structures */

struct shader_model_definition
{
	struct shader shader;
	word flags;
	byte reserved2A[0x72];
	real_vector2d map_scale;
	struct tag_reference base_map;
	byte reservedB4[0x24];
	real detail_map_scale;
	struct tag_reference detail_map;
	real detail_map_v_scale;
	byte reservedF0[0xC];
	struct shader_texture_animation animation;
};

typedef char verify_rasterizer_shadows_model_shadow_count_offset[
	offsetof(
		struct rasterizer_frame_statistics_globals,
		model_shadow_count) == 0xF4 ? 1 : -1];
typedef char verify_rasterizer_shadows_model_parameters_size[
	sizeof(struct rasterizer_model_begin_parameters) == 0xCC ? 1 : -1];
typedef char verify_rasterizer_shadows_model_base_map_scale_offset[
	offsetof(struct rasterizer_model_begin_parameters, base_map_scale) == 0xC4
		? 1 : -1];
typedef char verify_rasterizer_shadows_model_animation_offset[
	offsetof(struct rasterizer_model_begin_parameters, animation) == 0x84
		? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_flags_offset[
	offsetof(struct shader_model_definition, flags) == 0x28 ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_map_scale_offset[
	offsetof(struct shader_model_definition, map_scale) == 0x9C ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_base_map_offset[
	offsetof(struct shader_model_definition, base_map) == 0xA4 ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_detail_scale_offset[
	offsetof(struct shader_model_definition, detail_map_scale) == 0xD8 ? 1 : -1];
typedef char verify_rasterizer_shadows_shader_model_animation_offset[
	offsetof(struct shader_model_definition, animation) == 0xFC ? 1 : -1];

/* port: the shadow cache (rasterizer_environment_shadow_cast): a record's
longs at most, the maps kept, the objects whose last record is known. A
crowded scene casts over 60 shadows a pass, most of them standing; each map
is made when it is first kept (4 MB each at display.shadow_resolution 1024) */
#define SHADOW_RECORD_MAXIMUM 8192
#define SHADOW_CACHE_ENTRIES 128
#define SHADOW_CACHE_OBJECTS 1024
#define SHADOW_RECORD_LONGS(size) (((size) + sizeof(long) - 1) / sizeof(long))

enum
{
	_shadow_record_model = 1,
	_shadow_record_draw,
};

/* port: what a shadow's silhouette is drawn from: the begin's values, then
each model's nodes and each draw */
struct shadow_record_header
{
	long scale;
	long debug;
	long convolution;
	real screenproj[20];
};

struct shadow_record_model
{
	long type;
	long node_matrix_count;
	/* the node matrices follow */
};

struct shadow_record_draw
{
	long type;
	long two_sided;
	long alpha_tested;
	long base_map_index;
	long bitmap_index;
	struct triangle_buffer const *triangle_buffer_address;
	struct vertex_buffer const *vertex_buffer_address;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
	real_vector4d vertex_constants[3];
};

struct shadow_cache_entry
{
	D3DTexture *texture;
	D3DSurface *surface;
	unsigned long hash;
	/* the record's longs once its map is kept, else 0 */
	long size;
	long recorded_size;
	/* the pass that last drew or kept it */
	unsigned long pass;
	long record[SHADOW_RECORD_MAXIMUM];
};

/* ---------- prototypes */

static void rasterizer_shadow_convolve(
	boolean widen);
static void rasterizer_shadow_convolve_pass(
	short source_target,
	short destination_target,
	real offset);
static void shadow_begin_device(
	real const *vsh_constants__screenproj);
static void shadow_model_draw_device(
	boolean two_sided,
	boolean alpha_tested,
	long base_map_index,
	short bitmap_index,
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer,
	real_vector4d const *vertex_constants);
static void shadow_record_begin(
	real const *vsh_constants__screenproj);
static boolean shadow_record_model(
	struct render_skinning const *skinning);
static boolean shadow_record_draw(
	boolean two_sided,
	boolean alpha_tested,
	long base_map_index,
	short bitmap_index,
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer,
	real_vector4d const *vertex_constants);
static void shadow_record_replay(
	void);

/* ---------- globals */

extern D3DDevice *global_d3d_device;
extern struct pixel_shader_definition pixel_shader;

static boolean shadow_restored = TRUE;

static real_rgb_color local_shadow_color = { 0 };
static real local_object_bounding_radius = 0.0f;
static real_matrix4x3 local_shadow_matrix = { 0 };
static struct rasterizer_model_begin_parameters const *local_parameters = NULL;
static boolean shadow_setup = FALSE;
static boolean shadow_used = FALSE;
/* port: whether the shadow begun is drawn, and whether its maps are blurred
already (rasterizer_environment_shadows_convolve); the state of each
shadow of a batch (rasterizer_environment_shadow_slot) */
static boolean shadow_active = FALSE;
static boolean shadow_convolved = FALSE;
static short shadow_slot = 0;
static struct
{
	real_matrix4x3 matrix;
	real_rgb_color color;
	real radius;
	boolean setup;
	boolean used;
	boolean restored;
	boolean active;
	boolean convolved;
	short cache_entry;
	short store_entry;
} shadow_slots[RASTERIZER_SHADOW_BATCH_MAXIMUM];
/* port: the shadow cache (rasterizer_environment_shadow_cast): whether the
shadow begun is recorded, its record, the kept map it is drawn from or the
one its map is kept in */
static boolean shadow_recording = FALSE;
static long shadow_record_size = 0;
static long shadow_record[SHADOW_RECORD_MAXIMUM];
static short shadow_cache_entry = NONE;
static short shadow_store_entry = NONE;
static unsigned long shadow_cache_pass = 0;
static long shadow_cache_scale = 0;
static struct shadow_cache_entry shadow_cache[SHADOW_CACHE_ENTRIES];
static struct
{
	long object_index;
	unsigned long hash;
	unsigned long pass;
} shadow_cache_objects[SHADOW_CACHE_OBJECTS];

/* ---------- public code */

void _rasterizer_environment_shadows_begin(
	void)
{
	rasterizer_profile_begin(_rasterizer_profile_environment_shadows);
	shadow_cache_pass++; /* port */

	return;
}

boolean _rasterizer_environment_shadow_begin(
	long object_index,
	real_matrix4x3 const *shadow_matrix,
	real_rgb_color const *shadow_color,
	real object_bounding_radius,
	real *shadow_volume_bounding_radius)
{
	boolean success = TRUE;
	real inverse_radius;
	real vsh_constants__screenproj[20];

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		147,
		global_d3d_device);

	/* port */
	shadow_active = FALSE;
	shadow_cache_entry = NONE;
	shadow_store_entry = NONE;
	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			153,
			shadow_matrix);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			154,
			shadow_color);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			155,
			shadow_color->red >=0.0f && shadow_color->red <=1.0f);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			156,
			shadow_color->green>=0.0f && shadow_color->green<=1.0f);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			157,
			shadow_color->blue >=0.0f && shadow_color->blue <=1.0f);
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			158,
			object_bounding_radius>0.0f);

		inverse_radius = 1.0f / object_bounding_radius;

		vsh_constants__screenproj[0] = inverse_radius * shadow_matrix->forward.i;
		vsh_constants__screenproj[1] = inverse_radius * shadow_matrix->forward.j;
		vsh_constants__screenproj[2] = inverse_radius * shadow_matrix->forward.k;
		vsh_constants__screenproj[3] = -inverse_radius * dot_product3d(
			&shadow_matrix->forward,
			(real_vector3d const *)&shadow_matrix->position);
		vsh_constants__screenproj[4] = inverse_radius * shadow_matrix->left.i;
		vsh_constants__screenproj[5] = inverse_radius * shadow_matrix->left.j;
		vsh_constants__screenproj[6] = inverse_radius * shadow_matrix->left.k;
		vsh_constants__screenproj[7] = -inverse_radius * dot_product3d(
			&shadow_matrix->left,
			(real_vector3d const *)&shadow_matrix->position);
		vsh_constants__screenproj[8] = 0.0f;
		vsh_constants__screenproj[9] = 0.0f;
		vsh_constants__screenproj[10] = 0.0f;
		vsh_constants__screenproj[11] = 0.5f;
		vsh_constants__screenproj[12] = 0.0f;
		vsh_constants__screenproj[13] = 0.0f;
		vsh_constants__screenproj[14] = 0.0f;
		vsh_constants__screenproj[15] = 1.0f;
		vsh_constants__screenproj[16] = 0.0f;
		vsh_constants__screenproj[17] = 0.0f;
		vsh_constants__screenproj[18] = 0.0f;
		vsh_constants__screenproj[19] = 0.0f;

		/* port: recorded, to be drawn or found in the shadow cache
		(rasterizer_environment_shadow_cast) */
		if (halo_shadow_cache_supported())
		{
			shadow_record_begin(vsh_constants__screenproj);
		}
		else
		{
			shadow_begin_device(vsh_constants__screenproj);
		}

		local_shadow_matrix = *shadow_matrix;
		local_shadow_color = *shadow_color;
		local_object_bounding_radius = object_bounding_radius;
		if (shadow_volume_bounding_radius)
		{
			*shadow_volume_bounding_radius = object_bounding_radius;
		}

		local_parameters = 0;
		shadow_setup = FALSE;
		shadow_used = FALSE;
		shadow_restored = FALSE;
		/* port */
		shadow_active = TRUE;
		shadow_convolved = FALSE;

		if (rasterizer_debug_options.statistics_mode ==
			_rasterizer_statistics_mode_enabled)
		{
			rasterizer_frame_statistics.shadow_count++;
		}
	}

	return success;
}

void _rasterizer_environment_shadow_model_begin(
	struct rasterizer_model_begin_parameters const *parameters)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		239,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			245,
			parameters);

		/* port: recorded (rasterizer_environment_shadow_cast) */
		if (!shadow_recording || !shadow_record_model(&parameters->skinning))
		{
			rasterizer_set_model_skinning(&parameters->skinning);
		}

		local_parameters = parameters;
		shadow_used = TRUE;

		if (rasterizer_debug_options.statistics_mode ==
			_rasterizer_statistics_mode_enabled)
		{
			rasterizer_frame_statistics.model_shadow_count++;
		}
	}

	return;
}

void _rasterizer_environment_shadow_model_end(
	void)
{
	local_parameters = 0;

	return;
}

void _rasterizer_environment_shadow_model_draw(
	struct shader const *shader,
	short bitmap_index,
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer)
{
	struct shader_model_definition const *model;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		274,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		match_assert(
			"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
			280,
			shader);

		if (shader->base.type == _shader_type_model)
		{
			model = (struct shader_model_definition *)shader_get_and_verify_type(
				(struct shader *)shader,
				_shader_type_model);

			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
				286,
				vertex_buffer);
			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
				287,
				triangle_buffer);
			match_assert(
				"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
				288,
				local_parameters);

			{
				real_vector4d vertex_constants[3] =
				{
					{
						model->detail_map_scale,
						model->detail_map_scale * model->detail_map_v_scale,
						1.0f,
						1.0f
					},
					{1.0f, 0.0f, 0.0f, 0.0f},
					{0.0f, 1.0f, 0.0f, 0.0f}
				};

				shader_texture_animation_evaluate(
					&model->animation,
					&local_parameters->animation,
					local_parameters->base_map_scale.i * model->map_scale.i,
					local_parameters->base_map_scale.j * model->map_scale.j,
					0.0f,
					0.0f,
					0.0f,
					global_frame_parameters.game_time_sec,
					&vertex_constants[1],
					&vertex_constants[2]);

				/* port: recorded (rasterizer_environment_shadow_cast) */
				if (!shadow_recording ||
					!shadow_record_draw(
						TEST_FLAG(model->flags, _shader_model_two_sided_bit),
						!TEST_FLAG(model->flags, _shader_model_not_alpha_tested_bit),
						model->base_map.index,
						bitmap_index,
						triangle_buffer,
						vertex_buffer,
						vertex_constants))
				{
					shadow_model_draw_device(
						TEST_FLAG(model->flags, _shader_model_two_sided_bit),
						!TEST_FLAG(model->flags, _shader_model_not_alpha_tested_bit),
						model->base_map.index,
						bitmap_index,
						triangle_buffer,
						vertex_buffer,
						vertex_constants);
				}
			}

			if (rasterizer_debug_options.statistics_mode ==
				_rasterizer_statistics_mode_enabled)
			{
				rasterizer_frame_statistics.model_shadow_draw_count++;
				rasterizer_frame_statistics.model_shadow_triangle_count +=
					triangle_buffer->count;
				rasterizer_frame_statistics.model_shadow_vertex_count +=
					rasterizer_frame_statistics_count_static_vertices(
						triangle_buffer,
						vertex_buffer);
			}
		}
	}

	return;
}

void _rasterizer_environment_shadow_end(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		563,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		if (!shadow_used)
		{
			error(
				_error_silent,
				"### WARNING empty shadow has been cast");
		}

		if (!shadow_restored)
		{
			rasterizer_set_target(
				global_window_parameters.rasterizer_target,
				FALSE,
				FALSE,
				FALSE,
				TRUE);
			shadow_restored = TRUE;
		}

		/* port: its map kept, once blurred (rasterizer_environment_shadow_cast) */
		if (shadow_store_entry != NONE &&
			(!rasterizer_debug_options.shadow_convolution_enabled || shadow_convolved || shadow_setup))
		{
			D3DSurface *primary;
			D3DSurface *secondary;

			rasterizer_shadow_maps_get(&primary, &secondary);
			halo_shadow_map_copy(
				rasterizer_debug_options.shadow_convolution_enabled ? secondary : primary,
				shadow_cache[shadow_store_entry].surface);
			shadow_cache[shadow_store_entry].size = shadow_cache[shadow_store_entry].recorded_size;
		}
		shadow_store_entry = NONE;
	}

	return;
}

void _rasterizer_environment_shadow_draw(
	struct shader const *shader,
	short bitmap_index,
	long dynamic_triangle_buffer_index,
	long first_triangle_index,
	long triangle_count,
	struct vertex_buffer const *vertex_buffer)
{
	real vertex_constants[5][4];
	real inverse_radius;
	real inverse_radius_quarter;
	real inverse_radius_double;
	real position_dot_up;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		404,
		global_d3d_device);

	if (global_window_parameters.rasterizer_target ==
			_rasterizer_target_render_primary &&
		rasterizer_debug_options.draw_environment_shadows)
	{
		if (!shadow_setup)
		{
			/* port: unless its batch's blurs did it */
			if (rasterizer_debug_options.shadow_convolution_enabled && !shadow_convolved)
			{
				rasterizer_shadow_convolve(TRUE);
			}

			rasterizer_set_target_as_texture(
				0,
				(rasterizer_debug_options.shadow_convolution_enabled != FALSE) + 2,
				FALSE);
			/* port: a kept shadow's kept map (rasterizer_environment_shadow_cast) */
			if (shadow_cache_entry != NONE)
			{
				IDirect3DDevice8_SetTexture(
					global_d3d_device,
					0,
					(D3DBaseTexture *)shadow_cache[shadow_cache_entry].texture);
			}
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				0,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_BORDER);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				0,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_BORDER);
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

			rasterizer_set_texture_direct(
				1,
				global_rasterizer_data->linear_corner_fade.index,
				0);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_MAGFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_MINFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				1,
				D3DTSS_MIPFILTER,
				D3DTEXF_LINEAR);

			rasterizer_set_texture_direct(
				2,
				global_rasterizer_data->vector_normalization.index,
				0);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_ADDRESSW,
				D3DTADDRESS_CLAMP);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_MAGFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_MINFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				2,
				D3DTSS_MIPFILTER,
				D3DTEXF_LINEAR);

			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_CULLMODE,
				D3DCULL_CCW);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_COLORWRITEENABLE,
				D3DCOLORWRITEENABLE_ALL);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHABLENDENABLE,
				TRUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_SRCBLEND,
				D3DBLEND_ZERO);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_DESTBLEND,
				D3DBLEND_INVSRCCOLOR);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_BLENDOP,
				D3DBLENDOP_ADD);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHATESTENABLE,
				TRUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ALPHAREF,
				0);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZENABLE,
				TRUE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZFUNC,
				D3DCMP_EQUAL);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZWRITEENABLE,
				FALSE);
			IDirect3DDevice8_SetRenderState(
				global_d3d_device,
				D3DRS_ZBIAS,
				0);

			csmemset(&pixel_shader, 0, sizeof(pixel_shader));
			pixel_shader.texture_modes = 0x00000021;
			pixel_shader.combiner_count = 4;
			pixel_shader.constant_0[0] = real_rgb_color_to_pixel32(&local_shadow_color);
			pixel_shader.constant_1[0] = 0x00FFFFFF;
			pixel_shader.rgb_inputs[0] = 0x14200000;
			pixel_shader.rgb_outputs[0] = 0x000000C0;
			pixel_shader.rgb_inputs[1] = 0x290C0821;
			pixel_shader.rgb_outputs[1] = 0x000000CD;
			pixel_shader.rgb_inputs[2] = 0x2C200C2D;
			pixel_shader.rgb_outputs[2] = 0x00000C00;
			pixel_shader.rgb_inputs[3] = 0x2C020000;
			pixel_shader.rgb_outputs[3] = 0x000020D0;
			pixel_shader.final_combiner_inputs_abcd = 0x0000002C;
			pixel_shader.final_combiner_inputs_efg = 0x00000D00;
			if (rasterizer_debug_options.shadow_debug_enabled)
			{
				pixel_shader.final_combiner_inputs_abcd = 0x0000000C;
				IDirect3DDevice8_SetRenderState(
					global_d3d_device,
					D3DRS_ALPHATESTENABLE,
					FALSE);
			}
			rasterizer_set_pixel_shader(&pixel_shader);

			rasterizer_set_vertex_shader_permutation(
				_shadow_vertex_shader_index,
				vertex_buffer->type,
				shader_get_vertex_shader_permutation(shader));

			inverse_radius = 1.0f / local_object_bounding_radius;
			inverse_radius_quarter = 1.0f / (local_object_bounding_radius * 4.0f);
			inverse_radius_double = 1.0f / (local_object_bounding_radius * 0.5f);

			vertex_constants[0][0] = local_shadow_matrix.forward.i * inverse_radius * 0.5f;
			vertex_constants[0][1] = local_shadow_matrix.forward.j * inverse_radius * 0.5f;
			vertex_constants[0][2] = local_shadow_matrix.forward.k * inverse_radius * 0.5f;
			vertex_constants[0][3] =
				(1.0f -
				dot_product3d(
					(real_vector3d const *)&local_shadow_matrix.position,
					&local_shadow_matrix.forward) *
				inverse_radius) * 0.5f;
			vertex_constants[1][0] = local_shadow_matrix.left.i * inverse_radius * -0.5f;
			vertex_constants[1][1] = local_shadow_matrix.left.j * inverse_radius * -0.5f;
			vertex_constants[1][2] = local_shadow_matrix.left.k * inverse_radius * -0.5f;
			vertex_constants[1][3] =
				(dot_product3d(
					(real_vector3d const *)&local_shadow_matrix.position,
					&local_shadow_matrix.left) *
				inverse_radius + 1.0f) * 0.5f;
			vertex_constants[2][0] = local_shadow_matrix.up.i * inverse_radius_quarter;
			vertex_constants[2][1] = local_shadow_matrix.up.j * inverse_radius_quarter;
			vertex_constants[2][2] = local_shadow_matrix.up.k * inverse_radius_quarter;
			position_dot_up = dot_product3d(
				(real_vector3d const *)&local_shadow_matrix.position,
				&local_shadow_matrix.up);
			vertex_constants[2][3] =
				-(position_dot_up * inverse_radius_quarter);
			vertex_constants[3][0] = -(local_shadow_matrix.up.i * inverse_radius_double);
			vertex_constants[3][1] = -(local_shadow_matrix.up.j * inverse_radius_double);
			vertex_constants[3][2] = -(local_shadow_matrix.up.k * inverse_radius_double);
			vertex_constants[3][3] = position_dot_up * inverse_radius_double;
			vertex_constants[4][0] = local_shadow_matrix.up.i;
			vertex_constants[4][1] = local_shadow_matrix.up.j;
			vertex_constants[4][2] = local_shadow_matrix.up.k;
			vertex_constants[4][3] = 0.0f;
			IDirect3DDevice8_SetVertexShaderConstant(
				global_d3d_device,
				-81,
				vertex_constants,
				5);

			if (!shadow_restored)
			{
				rasterizer_set_target(
					global_window_parameters.rasterizer_target,
					0,
					0,
					FALSE,
					TRUE);
				shadow_restored = TRUE;
			}

			shadow_setup = TRUE;
		}

		rasterizer_set_stencil_mode(RASTERIZER_STENCIL_MODE_REJECT);
		/* port: only the pixels the projection can change */
		shadow_projection_scissor_begin(&local_shadow_matrix, local_object_bounding_radius);
		rasterizer_draw_dynamic_triangles_static_vertices(
			dynamic_triangle_buffer_index,
			first_triangle_index,
			triangle_count,
			vertex_buffer);
		shadow_projection_scissor_end(); /* port */

		if (rasterizer_debug_options.statistics_mode ==
			_rasterizer_statistics_mode_enabled)
		{
			rasterizer_frame_statistics.shadow_draw_count++;
			rasterizer_frame_statistics.shadow_triangle_count += triangle_count;
			rasterizer_frame_statistics.shadow_vertex_count +=
				rasterizer_frame_statistics_count_dynamic_vertices(
					dynamic_triangle_buffer_index,
					first_triangle_index,
					triangle_count);
		}
	}

	return;
}

void _rasterizer_environment_shadows_end(
	void)
{
	rasterizer_profile_end(_rasterizer_profile_environment_shadows);

	return;
}

/* port: render_object_shadows draws its shadows in batches of up to
RASTERIZER_SHADOW_BATCH_MAXIMUM (render_objects.c), where the renderer can
draw a pass of every shadow's blur at once (halo_shadow_layers): each
shadow's silhouette into maps of its own (rasterizer_shadow_maps_select),
then the blurs of them all, each pass one draw, then each shadow on the
screen in turn. Each pass of a blur reads the map the pass before it drew,
so the GPU finishes one pass before it starts the next; one shadow at a
time, a larger map's seven passes (rasterizer_shadow_convolve_pass) left the
GPU mostly waiting, about 50 microseconds a shadow. The pixels are the same:
each shadow's maps are drawn by the same draws, and the shadows are drawn
on the screen in the same order. */

/* selects a slot of the batch: its maps, and the state of its shadow. Returns
FALSE if its maps could not be made */
boolean rasterizer_environment_shadow_slot(
	short slot)
{
	if (!rasterizer_shadow_maps_select(slot))
	{
		return FALSE;
	}
	shadow_slots[shadow_slot].matrix = local_shadow_matrix;
	shadow_slots[shadow_slot].color = local_shadow_color;
	shadow_slots[shadow_slot].radius = local_object_bounding_radius;
	shadow_slots[shadow_slot].setup = shadow_setup;
	shadow_slots[shadow_slot].used = shadow_used;
	shadow_slots[shadow_slot].restored = shadow_restored;
	shadow_slots[shadow_slot].active = shadow_active;
	shadow_slots[shadow_slot].convolved = shadow_convolved;
	shadow_slots[shadow_slot].cache_entry = shadow_cache_entry;
	shadow_slots[shadow_slot].store_entry = shadow_store_entry;
	shadow_slot = slot;
	local_shadow_matrix = shadow_slots[slot].matrix;
	local_shadow_color = shadow_slots[slot].color;
	local_object_bounding_radius = shadow_slots[slot].radius;
	shadow_setup = shadow_slots[slot].setup;
	shadow_used = shadow_slots[slot].used;
	shadow_restored = shadow_slots[slot].restored;
	shadow_active = shadow_slots[slot].active;
	shadow_convolved = shadow_slots[slot].convolved;
	shadow_cache_entry = shadow_slots[slot].cache_entry;
	shadow_store_entry = shadow_slots[slot].store_entry;

	return TRUE;
}

/* blurs the maps of the shadows begun in the batch's first count slots, if
there are two or more: each pass of the blur once, drawn by the first's
draw for each of them; one alone is blurred as it is drawn */
void rasterizer_environment_shadows_convolve(
	short count)
{
	D3DSurface *maps[2][RASTERIZER_SHADOW_BATCH_MAXIMUM];
	short slot;
	short first = NONE;
	short active = 0;
	long scale = halo_shadow_map_scale();
	long distance;

	if (rasterizer_debug_options.draw_environment_shadows &&
		rasterizer_debug_options.shadow_convolution_enabled)
	{
		for (slot = 0; slot < count; slot++)
		{
			rasterizer_environment_shadow_slot(slot);
			if (shadow_active)
			{
				if (first == NONE)
				{
					first = slot;
				}
				rasterizer_shadow_maps_get(&maps[0][active], &maps[1][active]);
				active++;
			}
		}
		if (active > 1)
		{
			for (slot = 0; slot < count; slot++)
			{
				rasterizer_environment_shadow_slot(slot);
				if (shadow_active)
				{
					shadow_convolved = TRUE;
				}
			}
			rasterizer_environment_shadow_slot(first);
			/* (each pass's taps' distance from a texel, rasterizer_shadow_convolve's
			and rasterizer_shadow_convolve_pass's) */
			halo_shadow_layers(active, maps[0], maps[1], 0.00390625f / (real)scale);
			rasterizer_shadow_convolve(FALSE);
			for (distance = 1; distance < scale; distance *= 2)
			{
				halo_shadow_layers(active, maps[1], maps[0], (real)distance / (real)(128 * scale));
				rasterizer_shadow_convolve_pass(3, 2, (real)distance / (real)(128 * scale));
				halo_shadow_layers(active, maps[0], maps[1], (real)distance / (real)(128 * scale));
				rasterizer_shadow_convolve_pass(2, 3, (real)distance / (real)(128 * scale));
			}
			halo_shadow_layers(0, NULL, NULL, 0.f);
		}
	}

	return;
}

/* port: the shadow cache. Most shadows are drawn as they were the pass
before: their objects stand still, and their silhouettes are drawn by the
same draws from the same nodes. Each shadow's silhouette is recorded as it
is begun, not drawn (rasterizer_environment_shadow_begin and the model hooks
above): the values its draws are made from, the triangle and vertex buffers
by address and contents, the nodes' matrices, the texture's constants, and
the maps' size. Once its object's list has been drawn, a shadow whose record
is a kept map's, byte for byte, is drawn from that map, which the same draws
drew and blurred (halo_shadow_map_copy); its own maps are neither drawn nor
blurred. Any other is drawn as it was recorded, and its map is kept if its
object's record was the same the pass before. The kept maps are forgotten
with the map's data (rasterizer_environment_shadow_cache_flush) and when the
shadow maps' size changes. The pixels are the same: a kept map is the map
the same draws draw. */
void rasterizer_environment_shadow_cast(
	long object_index)
{
	struct shadow_record_header const *header = (struct shadow_record_header const *)shadow_record;
	unsigned long hash = 2166136261UL;
	long index;
	short entry;
	short store = NONE;

	if (!shadow_recording)
	{
		return;
	}
	if (header->scale != shadow_cache_scale)
	{
		rasterizer_environment_shadow_cache_flush();
		shadow_cache_scale = header->scale;
	}
	for (index = 0; index < shadow_record_size; index++)
	{
		hash = (hash ^ (unsigned long)shadow_record[index]) * 16777619UL;
	}
	for (entry = 0; entry < SHADOW_CACHE_ENTRIES; entry++)
	{
		if (shadow_cache[entry].size == shadow_record_size && shadow_cache[entry].hash == hash &&
			!memcmp(shadow_cache[entry].record, shadow_record, shadow_record_size * sizeof(long)))
		{
			break;
		}
	}
	if (entry < SHADOW_CACHE_ENTRIES)
	{
		/* drawn from the kept map: no maps of its own to blur */
		shadow_recording = FALSE;
		shadow_active = FALSE;
		shadow_convolved = TRUE;
		shadow_cache_entry = entry;
		shadow_cache[entry].pass = shadow_cache_pass;
	}
	else
	{
		shadow_record_replay();
		index = object_index & (SHADOW_CACHE_OBJECTS - 1);
		if (shadow_cache_objects[index].object_index == object_index &&
			shadow_cache_objects[index].hash == hash &&
			shadow_cache_objects[index].pass + 1 == shadow_cache_pass)
		{
			/* kept in the map no shadow of this pass is drawn from, the
			oldest */
			for (entry = 0; entry < SHADOW_CACHE_ENTRIES; entry++)
			{
				if (shadow_cache[entry].pass != shadow_cache_pass &&
					(store == NONE || shadow_cache[entry].pass < shadow_cache[store].pass))
				{
					store = entry;
				}
			}
			if (store != NONE && !shadow_cache[store].texture &&
				!rasterizer_shadow_map_create(&shadow_cache[store].texture, &shadow_cache[store].surface))
			{
				store = NONE;
			}
			if (store != NONE)
			{
				shadow_cache[store].size = 0;
				shadow_cache[store].hash = hash;
				shadow_cache[store].recorded_size = shadow_record_size;
				shadow_cache[store].pass = shadow_cache_pass;
				csmemcpy(shadow_cache[store].record, shadow_record, shadow_record_size * sizeof(long));
				shadow_store_entry = store;
			}
		}
		shadow_cache_objects[index].object_index = object_index;
		shadow_cache_objects[index].hash = hash;
		shadow_cache_objects[index].pass = shadow_cache_pass;
	}

	return;
}

/* forgets the kept maps (rasterizer_dispose_from_old_map: the buffers and
bitmaps their records name are the old map's) */
void rasterizer_environment_shadow_cache_flush(
	void)
{
	short entry;

	for (entry = 0; entry < SHADOW_CACHE_ENTRIES; entry++)
	{
		shadow_cache[entry].size = 0;
		shadow_cache[entry].pass = 0;
	}
	csmemset(shadow_cache_objects, 0, sizeof(shadow_cache_objects));
	/* port: the parts' extents (shadow_silhouette.c) */
	shadow_silhouette_flush();

	return;
}

/* ---------- private code */

/* port: the device calls of rasterizer_environment_shadow_begin, which a
recorded shadow makes as it is drawn (shadow_record_replay) */
static void shadow_begin_device(
	real const *vsh_constants__screenproj)
{
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_CCW);
	IDirect3DDevice8_SetRenderState(
		global_d3d_device,
		D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED |
		D3DCOLORWRITEENABLE_GREEN |
		D3DCOLORWRITEENABLE_BLUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHABLENDENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHATESTENABLE, TRUE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ALPHAREF, 0x7F);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZENABLE, FALSE);
	IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_ZBIAS, 0);

	csmemset(&pixel_shader, 0, sizeof(pixel_shader));
	pixel_shader.texture_modes = 1;
	pixel_shader.combiner_count = 1;
	pixel_shader.final_combiner_inputs_abcd = 0x00000020;
	pixel_shader.final_combiner_inputs_efg = 0x00001800;
	rasterizer_set_pixel_shader(&pixel_shader);

	IDirect3DDevice8_SetVertexShaderConstant(
		global_d3d_device,
		-68,
		vsh_constants__screenproj,
		5);

	rasterizer_set_target(
		2,
		0,
		rasterizer_debug_options.shadow_debug_enabled ? 0x88888888 : 0,
		TRUE,
		FALSE);
	rasterizer_set_stencil_mode(0);

	return;
}

/* port: the device calls of rasterizer_environment_shadow_model_draw, which
a recorded shadow makes as it is drawn (shadow_record_replay) */
static void shadow_model_draw_device(
	boolean two_sided,
	boolean alpha_tested,
	long base_map_index,
	short bitmap_index,
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer,
	real_vector4d const *vertex_constants)
{
	if (two_sided)
	{
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_NONE);
	}
	else
	{
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_CULLMODE, D3DCULL_CCW);
	}
	rasterizer_set_vertex_shader_permutation(
		_shadow_model_vertex_shader_index,
		vertex_buffer->type,
		0);

	if (!alpha_tested)
	{
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_PSTEXTUREMODES, 0);
	}
	else
	{
		IDirect3DDevice8_SetRenderState(global_d3d_device, D3DRS_PSTEXTUREMODES, 1);
		rasterizer_set_texture(
			0,
			0,
			1,
			base_map_index,
			bitmap_index);
		IDirect3DDevice8_SetTextureStageState(
			global_d3d_device,
			0,
			D3DTSS_ADDRESSU,
			D3DTADDRESS_WRAP);
		IDirect3DDevice8_SetTextureStageState(
			global_d3d_device,
			0,
			D3DTSS_ADDRESSV,
			D3DTADDRESS_WRAP);
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
	}

	IDirect3DDevice8_SetVertexShaderConstant(
		global_d3d_device,
		-84,
		vertex_constants,
		3);

	/* port: the extent of the part's shadow, which the blur draws within */
	shadow_silhouette_bounds_begin(triangle_buffer, vertex_buffer);
	rasterizer_draw_static_triangles_static_vertices(
		triangle_buffer,
		0,
		triangle_buffer->count,
		vertex_buffer);
	shadow_silhouette_bounds_end();

	return;
}

/* port: the shadow cache's record (rasterizer_environment_shadow_cast) */

/* begins the record of a shadow's silhouette */
static void shadow_record_begin(
	real const *vsh_constants__screenproj)
{
	struct shadow_record_header *header = (struct shadow_record_header *)shadow_record;

	csmemset(shadow_record, 0, SHADOW_RECORD_LONGS(sizeof(*header)) * sizeof(long));
	header->scale = halo_shadow_map_scale();
	header->debug = rasterizer_debug_options.shadow_debug_enabled != FALSE;
	header->convolution = rasterizer_debug_options.shadow_convolution_enabled != FALSE;
	csmemcpy(header->screenproj, vsh_constants__screenproj, sizeof(header->screenproj));
	shadow_record_size = SHADOW_RECORD_LONGS(sizeof(*header));
	shadow_recording = TRUE;

	return;
}

/* the next size bytes of the record, cleared; NULL if they do not fit, and
the shadow drawn as it was recorded, to be drawn on directly */
static void *shadow_record_reserve(
	long size)
{
	long count = SHADOW_RECORD_LONGS(size);
	long *item;

	if (shadow_record_size + count > SHADOW_RECORD_MAXIMUM)
	{
		shadow_record_replay();
		return NULL;
	}
	item = &shadow_record[shadow_record_size];
	csmemset(item, 0, count * sizeof(long));
	shadow_record_size += count;

	return item;
}

/* records a model's nodes; FALSE if they are not recorded, to be given to the
device directly */
static boolean shadow_record_model(
	struct render_skinning const *skinning)
{
	struct shadow_record_model *model;
	long count = skinning->node_matrices ? skinning->node_matrix_count : 0;

	if (count <= 0 || count > MAXIMUM_NODES_PER_MODEL)
	{
		shadow_record_replay();
		return FALSE;
	}
	model = shadow_record_reserve(sizeof(*model) + count * sizeof(real_matrix4x3));
	if (!model)
	{
		return FALSE;
	}
	model->type = _shadow_record_model;
	model->node_matrix_count = count;
	csmemcpy(model + 1, skinning->node_matrices, count * sizeof(real_matrix4x3));

	return TRUE;
}

/* records a draw; FALSE if it is not recorded, to be drawn directly */
static boolean shadow_record_draw(
	boolean two_sided,
	boolean alpha_tested,
	long base_map_index,
	short bitmap_index,
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer,
	real_vector4d const *vertex_constants)
{
	struct shadow_record_draw *draw = shadow_record_reserve(sizeof(*draw));

	if (!draw)
	{
		return FALSE;
	}
	draw->type = _shadow_record_draw;
	draw->two_sided = two_sided != FALSE;
	draw->alpha_tested = alpha_tested != FALSE;
	/* (the texture is drawn with only where it is alpha tested) */
	if (alpha_tested)
	{
		draw->base_map_index = base_map_index;
		draw->bitmap_index = bitmap_index;
	}
	draw->triangle_buffer_address = triangle_buffer;
	draw->vertex_buffer_address = vertex_buffer;
	csmemcpy(&draw->triangle_buffer, triangle_buffer, sizeof(draw->triangle_buffer));
	csmemcpy(&draw->vertex_buffer, vertex_buffer, sizeof(draw->vertex_buffer));
	csmemcpy(draw->vertex_constants, vertex_constants, sizeof(draw->vertex_constants));

	return TRUE;
}

/* draws the shadow's silhouette as it was recorded, by the device calls it
would have made, and ends the record. The buffers are drawn from where they
were found, which are the same buffers in the same frame */
static void shadow_record_replay(
	void)
{
	struct shadow_record_header const *header = (struct shadow_record_header const *)shadow_record;
	long offset = SHADOW_RECORD_LONGS(sizeof(*header));

	shadow_recording = FALSE;
	shadow_begin_device(header->screenproj);
	while (offset < shadow_record_size)
	{
		if (shadow_record[offset] == _shadow_record_model)
		{
			struct shadow_record_model const *model = (struct shadow_record_model const *)&shadow_record[offset];
			struct render_skinning skinning;

			skinning.node_matrices = (real_matrix4x3 const *)(model + 1);
			skinning.node_matrix_count = (short)model->node_matrix_count;
			rasterizer_set_model_skinning(&skinning);
			offset += SHADOW_RECORD_LONGS(sizeof(*model) + model->node_matrix_count * sizeof(real_matrix4x3));
		}
		else
		{
			struct shadow_record_draw const *draw = (struct shadow_record_draw const *)&shadow_record[offset];

			shadow_model_draw_device(
				(boolean)draw->two_sided,
				(boolean)draw->alpha_tested,
				draw->base_map_index,
				(short)draw->bitmap_index,
				draw->triangle_buffer_address,
				draw->vertex_buffer_address,
				draw->vertex_constants);
			offset += SHADOW_RECORD_LONGS(sizeof(*draw));
		}
	}

	return;
}

/* port: widen, the further passes of a larger map too */
static void rasterizer_shadow_convolve(
	boolean widen)
{
	short stage;
	real vertex_constants[8][4];
	/* port: the shadow maps' texels for each of the Xbox's 128 */
	long scale = halo_shadow_map_scale();
	long distance;

	match_assert(
		"c:\\halo\\SOURCE\\rasterizer\\xbox\\rasterizer_xbox_shadows.c",
		31,
		global_d3d_device);

	if (rasterizer_debug_options.draw_environment_shadows &&
		rasterizer_debug_options.shadow_convolution_enabled)
	{
		for (stage = 0; stage < 4; stage++)
		{
			rasterizer_set_target_as_texture(stage, 2, FALSE);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_ADDRESSU,
				D3DTADDRESS_BORDER);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_ADDRESSV,
				D3DTADDRESS_BORDER);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_MAGFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_MINFILTER,
				D3DTEXF_LINEAR);
			IDirect3DDevice8_SetTextureStageState(
				global_d3d_device,
				stage,
				D3DTSS_MIPFILTER,
				D3DTEXF_POINT);
		}

		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_CULLMODE,
			D3DCULL_CCW);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_COLORWRITEENABLE,
			D3DCOLORWRITEENABLE_RED |
			D3DCOLORWRITEENABLE_GREEN |
			D3DCOLORWRITEENABLE_BLUE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ALPHABLENDENABLE,
			FALSE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ALPHATESTENABLE,
			FALSE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ZENABLE,
			FALSE);
		IDirect3DDevice8_SetRenderState(
			global_d3d_device,
			D3DRS_ZBIAS,
			0);

		rasterizer_set_vertex_shader_permutation(
			_shadow_convolution_vertex_shader_index,
			8,
			0);

		vertex_constants[0][0] = 1.0f;
		vertex_constants[0][1] = 0.0f;
		vertex_constants[0][2] = 0.0f;
		vertex_constants[0][3] = -0.00390625f;
		vertex_constants[1][0] = 0.0f;
		vertex_constants[1][1] = 1.0f;
		vertex_constants[1][2] = 0.0f;
		vertex_constants[1][3] = -0.00390625f;
		vertex_constants[2][0] = 1.0f;
		vertex_constants[2][1] = 0.0f;
		vertex_constants[2][2] = 0.0f;
		vertex_constants[2][3] = 0.00390625f;
		vertex_constants[3][0] = 0.0f;
		vertex_constants[3][1] = 1.0f;
		vertex_constants[3][2] = 0.0f;
		vertex_constants[3][3] = 0.00390625f;
		vertex_constants[4][0] = 1.0f;
		vertex_constants[4][1] = 0.0f;
		vertex_constants[4][2] = 0.0f;
		vertex_constants[4][3] = -0.00390625f;
		vertex_constants[5][0] = 0.0f;
		vertex_constants[5][1] = 1.0f;
		vertex_constants[5][2] = 0.0f;
		vertex_constants[5][3] = 0.00390625f;
		vertex_constants[6][0] = 1.0f;
		vertex_constants[6][1] = 0.0f;
		vertex_constants[6][2] = 0.0f;
		vertex_constants[6][3] = 0.00390625f;
		vertex_constants[7][0] = 0.0f;
		vertex_constants[7][1] = 1.0f;
		vertex_constants[7][2] = 0.0f;
		vertex_constants[7][3] = -0.00390625f;
		/* port: half a texel of a larger map (rasterizer_shadow_convolve_pass) */
		if (scale > 1)
		{
			for (distance = 0; distance < 8; distance++)
			{
				vertex_constants[distance][3] /= (real)scale;
			}
		}
		IDirect3DDevice8_SetVertexShaderConstant(
			global_d3d_device,
			-81,
			vertex_constants,
			8);

		csmemset(&pixel_shader, 0, sizeof(pixel_shader));
		pixel_shader.texture_modes = 0x00008421;
		pixel_shader.combiner_count = 1;
		pixel_shader.alpha_inputs[0] = 0x08A009A0;
		pixel_shader.alpha_outputs[0] = 0x00030C00;
		pixel_shader.rgb_inputs[0] = 0x0AA00BA0;
		pixel_shader.rgb_outputs[0] = 0x00030C00;
		pixel_shader.final_combiner_inputs_abcd = 0x0C20001C;
		rasterizer_set_pixel_shader(&pixel_shader);

		rasterizer_set_target(3, 0, 0, FALSE, FALSE);

		IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 0);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			-1.0078125f,
			1.0078125f);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 0);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			0.9921875f,
			1.0078125f);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 1);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			0.9921875f,
			-0.9921875f);
		IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 1);
		IDirect3DDevice8_SetVertexData2f(
			global_d3d_device,
			0,
			-1.0078125f,
			-0.9921875f);
		IDirect3DDevice8_End(global_d3d_device);

		/* port: the passes that widen the blur of a larger map, back and
		forth between the maps, ending in the secondary */
		for (distance = 1; widen && distance < scale; distance *= 2)
		{
			rasterizer_shadow_convolve_pass(3, 2, (real)distance / (real)(128 * scale));
			rasterizer_shadow_convolve_pass(2, 3, (real)distance / (real)(128 * scale));
		}
	}

	return;
}

/* port: the shadow maps can be drawn larger than the Xbox's 128x128
(display.shadow_resolution, port/linux/src/d3d8_gl.c): scale times as many
texels each way, a power of two. The Xbox's blur takes four taps diagonally
half a texel off each texel, on the corners it shares with its neighbours.
Each tap averages the four texels there, and together they weigh the texel
and its neighbours 1, 2, 1 each way: a tent over three texels, which is a box
two texels wide taken twice. For the shadows to stay as soft, a larger map's
blur must cover as much of the map: a tent scale times as wide, a box of
2 * scale texels taken twice, which weighs the texels 2 * scale - |d| for |d|
up to 2 * scale - 1 texels away. The Xbox's pass, with its taps half a texel
of the larger map off, is the box of two texels taken twice. A further pass
with its taps diagonally distance texels off, on texels' centres, averages
texels 2 * distance apart each way, which doubles a box of 2 * distance
texels; two passes each at distance 1, 2, ... scale / 2 make both boxes
2 * scale texels wide. The quad's offsets of half a texel stay as they are:
they are half of the game's units, which the port undoes in the same units
at any size (port/linux/src/nv2a_vsh.c), so the quad covers the map. */
static void rasterizer_shadow_convolve_pass(
	short source_target,
	short destination_target,
	real offset)
{
	short stage;
	real vertex_constants[8][4];

	csmemset(vertex_constants, 0, sizeof(vertex_constants));
	for (stage = 0; stage < 4; stage++)
	{
		rasterizer_set_target_as_texture(stage, source_target, FALSE);
		vertex_constants[stage * 2][0] = 1.0f;
		vertex_constants[stage * 2 + 1][1] = 1.0f;
	}
	/* the Xbox pass's corners, in its order */
	vertex_constants[0][3] = -offset;
	vertex_constants[1][3] = -offset;
	vertex_constants[2][3] = offset;
	vertex_constants[3][3] = offset;
	vertex_constants[4][3] = -offset;
	vertex_constants[5][3] = offset;
	vertex_constants[6][3] = offset;
	vertex_constants[7][3] = -offset;
	IDirect3DDevice8_SetVertexShaderConstant(
		global_d3d_device,
		-81,
		vertex_constants,
		8);

	rasterizer_set_target(destination_target, 0, 0, FALSE, FALSE);

	IDirect3DDevice8_Begin(global_d3d_device, D3DPT_TRIANGLEFAN);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, -1.0078125f, 1.0078125f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 0);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, 0.9921875f, 1.0078125f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 1, 1);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, 0.9921875f, -0.9921875f);
	IDirect3DDevice8_SetVertexData2s(global_d3d_device, 4, 0, 1);
	IDirect3DDevice8_SetVertexData2f(global_d3d_device, 0, -1.0078125f, -0.9921875f);
	IDirect3DDevice8_End(global_d3d_device);

	return;
}
