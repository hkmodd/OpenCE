/*
STRUCTURE_PASS_CHUNKS.C (test)

structure_render_pass (structure_render.c) over fake bsps' lightmaps and
materials, drawing the environment's surfaces from the chunks
structure_render_preprocess found and other lists by walking them, against a
transcription of the original's walk: every lightmap begun and ended, every
bitmap and shader looked up, every surface asked after and every triangle
drawn, in order and with the same arguments, and the walk's assertion, must
be the same. test_structure_pass_chunks.py takes the code (under_test.inc)
from the sources.
*/

#include "harness.h"
#include "enums.inc"

typedef unsigned short word;
typedef struct real_plane3d { real_vector3d n; real d; } real_plane3d;

/* tags: the members the passes read */
struct tag_block { long count; void *address; void *definition; };
struct tag_reference { long group_tag; char *name; long name_length; long index; };
struct render_lighting { real values[8]; };
struct vertex_buffer { long type; long count; void *data; };
struct bitmap_data { short index; };
struct shader { struct { short type; } base; };
struct structure_material
{
	struct tag_reference shader;
	short permutation_index;
	word flags;
	long first_surface_index;
	long surface_count;
	real_point3d centroid;
	struct render_lighting lighting;
	real_plane3d plane;
	short breakable_surface_index;
	struct vertex_buffer vertices;
};
struct structure_lightmap { short bitmap_index; struct tag_block materials; };
struct structure_bsp { struct tag_block lightmaps; struct tag_reference lightmap_group; };

/* the world */
enum { LIGHTMAPS = 12, MATERIALS = 10, SHADERS = 16, BITMAPS = 8, SURFACES_PER_MATERIAL = 40 };
static struct structure_bsp bsp;
static struct structure_lightmap lightmaps[LIGHTMAPS];
static struct structure_material materials[LIGHTMAPS][MATERIALS];
static struct shader shaders[SHADERS];
static struct bitmap_data bitmaps[BITMAPS];
static long world_surface_count;
static unsigned long breakable_state;
static struct { long environment_surface_count; long environment_surface_indices[MAXIMUM_RENDERED_ENVIRONMENT_SURFACES]; } render;
static real_vector3d zero_vector;
#define global_zero_vector3d (&zero_vector)
#define global_structure_bsp_get() (&bsp)
#define shader_type_is_transparent(type) ((type) >= 3)

/* what the passes did, in order */
enum { MAXIMUM_CALLS = 1 << 18 };
struct call { long kind, a[7]; };
static struct call *calls;
static long call_count, element_lookups, assertions;
static void note(long kind, long a0, long a1, long a2, long a3, long a4, long a5, long a6)
{
	struct call *call;

	CHECK(call_count < MAXIMUM_CALLS, "too many calls");
	call = &calls[call_count++];
	call->kind = kind;
	call->a[0] = a0; call->a[1] = a1; call->a[2] = a2; call->a[3] = a3;
	call->a[4] = a4; call->a[5] = a5; call->a[6] = a6;
}
#define ADDRESS(pointer) ((long)(size_t)(pointer))

static void *tag_block_element(struct tag_block *block, long index, long size)
{
	CHECK(index >= 0 && index < block->count, "element %ld of %ld", index, block->count);
	element_lookups++;
	return (char *)block->address + index * size;
}
#define TAG_BLOCK_GET_ELEMENT(block, index, type) ((type *)tag_block_element((block), (index), sizeof(type)))
static struct bitmap_data *bitmap_group_try_and_get_bitmap(long group_index, short bitmap_index)
{
	note('b', group_index, bitmap_index, 0, 0, 0, 0, 0);
	return &bitmaps[bitmap_index];
}
static struct shader *shader_definition_get(long shader_index)
{
	note('s', shader_index, 0, 0, 0, 0, 0, 0);
	return &shaders[shader_index];
}
static boolean breakable_surface_extant(short breakable_surface_index)
{
	note('x', breakable_surface_index, 0, 0, 0, 0, 0, 0);
	return breakable_surface_index == NONE || ((breakable_state >> (breakable_surface_index & 31)) & 1);
}
#define match_vassert(file, line, condition, message) ((condition) ? (void)0 : (void)assertions++)

#include "under_test.inc"

/* the passes' procedures */
static void begin_lightmap(struct bitmap_data const *lightmap_bitmap)
{
	note('B', ADDRESS(lightmap_bitmap), 0, 0, 0, 0, 0, 0);
}
static void draw_triangles(struct shader const *shader, short shader_permutation_index,
	long dynamic_triangle_buffer_index, long first_triangle_index, long triangle_count,
	struct vertex_buffer const *vertex_buffer)
{
	note('D', ADDRESS(shader), shader_permutation_index, dynamic_triangle_buffer_index, first_triangle_index,
		triangle_count, ADDRESS(vertex_buffer), 0);
}
static void end_lightmap(void)
{
	note('E', 0, 0, 0, 0, 0, 0, 0);
}
static void draw_transparent_triangles(struct shader const *shader, short shader_permutation_index,
	struct bitmap_data const *lightmap, long dynamic_triangle_buffer_index, long first_triangle_index,
	long triangle_count, struct vertex_buffer const *vertex_buffers, real_point3d const *centroid,
	real_plane3d const *plane, real_vector3d const *offset, void const *lighting, unsigned long geometry_flags)
{
	note('T', ADDRESS(shader), shader_permutation_index, ADDRESS(lightmap), dynamic_triangle_buffer_index,
		first_triangle_index, triangle_count, ADDRESS(vertex_buffers));
	note('t', ADDRESS(centroid), ADDRESS(plane), ADDRESS(offset), ADDRESS(lighting), (long)geometry_flags, 0, 0);
}

/* the original's structure_render_pass (structure_render.c before the chunks), transcribed */
static void original_structure_render_pass(
	long *surface_indices,
	long surface_count,
	long dynamic_triangles_index,
	void (*begin_lightmap)(struct bitmap_data const *lightmap_bitmap),
	void (*draw_triangles)(struct shader const *, short, long, long, long, struct vertex_buffer const *),
	void (*end_lightmap)(void),
	void (*draw_transparent_triangles)(struct shader const *, short, struct bitmap_data const *, long, long, long,
		struct vertex_buffer const *, real_point3d const *, real_plane3d const *, real_vector3d const *,
		void const *, unsigned long))
{
	struct structure_bsp *structure_bsp = global_structure_bsp_get();
	long *surface_index_cursor = surface_indices;
	long *surface_index_end = surface_indices + surface_count;
	long first_triangle_index = 0;
	short lightmap_index;

	for (lightmap_index = 0; lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap;
		struct structure_material *last_material;
		struct bitmap_data *lightmap_bitmap;
		short material_index;

		if (surface_index_cursor >= surface_index_end)
		{
			break;
		}
		lightmap = TAG_BLOCK_GET_ELEMENT(&structure_bsp->lightmaps, lightmap_index, struct structure_lightmap);
		last_material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, lightmap->materials.count - 1, struct structure_material);
		if (*surface_index_cursor >= last_material->first_surface_index + last_material->surface_count)
		{
			continue;
		}
		if (structure_bsp->lightmap_group.index == NONE)
		{
			lightmap_bitmap = NULL;
		}
		else
		{
			lightmap_bitmap = bitmap_group_try_and_get_bitmap(structure_bsp->lightmap_group.index, lightmap->bitmap_index);
		}
		if (begin_lightmap)
		{
			begin_lightmap(lightmap_bitmap);
		}
		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material;
			struct shader *shader;
			long *chunk_start;
			long chunk_count;

			if (surface_index_cursor >= surface_index_end)
			{
				break;
			}
			material = TAG_BLOCK_GET_ELEMENT(&lightmap->materials, material_index, struct structure_material);
			if (*surface_index_cursor >= material->first_surface_index + material->surface_count)
			{
				continue;
			}
			chunk_start = surface_index_cursor;
			shader = shader_definition_get(material->shader.index);
			do
			{
				surface_index_cursor++;
			}
			while (surface_index_cursor < surface_index_end
				&& *surface_index_cursor < material->first_surface_index + material->surface_count);
			chunk_count = surface_index_cursor - chunk_start;
			if (breakable_surface_extant(material->breakable_surface_index))
			{
				if (!shader_type_is_transparent(shader->base.type))
				{
					if (draw_triangles)
					{
						draw_triangles(shader, material->permutation_index, dynamic_triangles_index,
							first_triangle_index, chunk_count, &material->vertices);
					}
				}
				else if (draw_transparent_triangles)
				{
					draw_transparent_triangles(shader, material->permutation_index, lightmap_bitmap,
						dynamic_triangles_index, first_triangle_index, chunk_count, &material->vertices,
						&material->centroid,
						TEST_FLAG(material->flags, _structure_material_coplanar_bit) ? &material->plane : NULL,
						TEST_FLAG(material->flags, _structure_material_fog_plane_bit)
							? &structure_render_globals.fog_offset_vector
							: global_zero_vector3d,
						&material->lighting, 0);
				}
			}
			first_triangle_index += chunk_count;
		}
		if (end_lightmap)
		{
			end_lightmap();
		}
	}
	match_vassert("structure_render.c", 599, surface_index_cursor >= surface_index_end,
		"there are more surfaces than materials that reference them, stupid.");
}

/* a random world */
static unsigned long random_seed;
static long random_below(long limit)
{
	random_seed = random_seed * 1103515245UL + 12345UL;
	return (long)((random_seed >> 8) % (unsigned long)limit);
}

static void make_world(boolean ordered, boolean lightmap_group)
{
	long ranges[LIGHTMAPS * MATERIALS][2];
	long range_count = 0, range_index, lightmap_index, first = 0;

	memset(&bsp, 0, sizeof(bsp));
	bsp.lightmaps.count = 1 + random_below(LIGHTMAPS);
	bsp.lightmaps.address = lightmaps;
	bsp.lightmap_group.index = lightmap_group ? 0x12340005 : NONE;
	for (lightmap_index = 0; lightmap_index < bsp.lightmaps.count; lightmap_index++)
	{
		struct structure_lightmap *lightmap = &lightmaps[lightmap_index];
		long material_index;

		lightmap->bitmap_index = (short)random_below(BITMAPS);
		lightmap->materials.count = 1 + random_below(MATERIALS);
		lightmap->materials.address = materials[lightmap_index];
		for (material_index = 0; material_index < lightmap->materials.count; material_index++)
		{
			struct structure_material *material = &materials[lightmap_index][material_index];

			memset(material, 0, sizeof(*material));
			material->shader.index = random_below(SHADERS);
			material->permutation_index = (short)random_below(4);
			material->flags = (word)random_below(4);
			material->breakable_surface_index = random_below(4) ? NONE : (short)random_below(32);
			ranges[range_count][0] = first;
			ranges[range_count][1] = random_below(6) ? 1 + random_below(SURFACES_PER_MATERIAL) : 0;
			first += ranges[range_count][1];
			range_count++;
		}
	}
	world_surface_count = first;
	if (!ordered)
	{
		/* the ranges are the same surfaces, given to the materials in another order */
		for (range_index = range_count - 1; range_index > 0; range_index--)
		{
			long other = random_below(range_index + 1), swap[2];

			swap[0] = ranges[range_index][0]; swap[1] = ranges[range_index][1];
			ranges[range_index][0] = ranges[other][0]; ranges[range_index][1] = ranges[other][1];
			ranges[other][0] = swap[0]; ranges[other][1] = swap[1];
		}
	}
	range_index = 0;
	for (lightmap_index = 0; lightmap_index < bsp.lightmaps.count; lightmap_index++)
	{
		long material_index;

		for (material_index = 0; material_index < lightmaps[lightmap_index].materials.count; material_index++)
		{
			materials[lightmap_index][material_index].first_surface_index = ranges[range_index][0];
			materials[lightmap_index][material_index].surface_count = ranges[range_index][1];
			range_index++;
		}
	}
	for (range_index = 0; range_index < SHADERS; range_index++)
	{
		shaders[range_index].base.type = (short)random_below(6);
	}
	breakable_state = random_seed ^ (random_seed << 7);
}

/* a list of visible surfaces in ascending order, some past the materials' last if asked */
static long make_list(long *indices, long extra)
{
	long count = 0, surface_index, chance = 1 + random_below(8);

	for (surface_index = 0; surface_index < world_surface_count + extra; surface_index++)
	{
		if (random_below(8) < chance)
		{
			indices[count++] = surface_index;
		}
	}
	return count;
}

/* structure_render_preprocess' part that finds the environment's chunks */
static void preprocess(long environment_triangles_index)
{
	structure_render_globals.environment_triangles_index = environment_triangles_index;
	structure_render_globals.environment_geometry_valid = environment_triangles_index != NONE;
	find_environment_chunks();
}

/* one pass by both, compared */
static struct call *original_calls;
static long total_lookups[2];
static void compare_pass(long *indices, long count, long dynamic_triangles_index, long procedures)
{
	long original_count, original_assertions, call_index;
	struct call *saved = calls;
	void (*begin)(struct bitmap_data const *) = (procedures & 1) ? NULL : begin_lightmap;
	void (*draw)(struct shader const *, short, long, long, long, struct vertex_buffer const *) =
		(procedures & 2) ? NULL : draw_triangles;
	void (*end)(void) = (procedures & 4) ? NULL : end_lightmap;
	void (*transparent)(struct shader const *, short, struct bitmap_data const *, long, long, long,
		struct vertex_buffer const *, real_point3d const *, real_plane3d const *, real_vector3d const *,
		void const *, unsigned long) = (procedures & 8) ? NULL : draw_transparent_triangles;

	calls = original_calls;
	call_count = element_lookups = assertions = 0;
	original_structure_render_pass(indices, count, dynamic_triangles_index, begin, draw, end, transparent);
	original_count = call_count;
	original_assertions = assertions;
	total_lookups[0] += element_lookups;

	calls = saved;
	call_count = element_lookups = assertions = 0;
	structure_render_pass(indices, count, dynamic_triangles_index, begin, draw, end, transparent);
	total_lookups[1] += element_lookups;

	CHECK(call_count == original_count, "%ld calls, the original made %ld", call_count, original_count);
	for (call_index = 0; call_index < call_count; call_index++)
	{
		CHECK(!memcmp(&calls[call_index], &original_calls[call_index], sizeof(struct call)),
			"call %ld is '%c', the original's '%c'", call_index, (int)calls[call_index].kind,
			(int)original_calls[call_index].kind);
	}
	CHECK(assertions == original_assertions, "%ld assertions, the original %ld", assertions, original_assertions);
}

/* frames of passes over the environment's list after structure_render_preprocess */
static void environment_frames(boolean ordered, long extra, long worlds)
{
	long world;

	for (world = 0; world < worlds; world++)
	{
		long frame;

		make_world(ordered, random_below(5) != 0);
		for (frame = 0; frame < 3; frame++)
		{
			long pass, triangles_index = 3 + random_below(100);

			render.environment_surface_count = make_list(render.environment_surface_indices, extra);
			preprocess(triangles_index);
			for (pass = 0; pass < 10; pass++)
			{
				if (pass == 5)
				{
					breakable_state = ~breakable_state ^ (unsigned long)pass;
				}
				compare_pass(render.environment_surface_indices, render.environment_surface_count,
					triangles_index, pass < 3 ? 0 : random_below(16));
			}
		}
	}
}

static long other_list[MAXIMUM_RENDERED_ENVIRONMENT_SURFACES];

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	calls = calloc(MAXIMUM_CALLS, sizeof(struct call));
	original_calls = calloc(MAXIMUM_CALLS, sizeof(struct call));
	random_seed = 0x5EED;

	if (!strcmp(name, "environment"))
	{
		/* drawn from the chunks: as the original, and without looking up the lightmaps it passes over */
		environment_frames(TRUE, 0, 300);
		CHECK(total_lookups[1] < total_lookups[0], "%ld tag lookups, the original %ld: the chunks are not drawn from",
			total_lookups[1], total_lookups[0]);
	}
	else if (!strcmp(name, "unordered"))
	{
		/* materials whose surfaces are not in the lightmaps' order */
		environment_frames(FALSE, 0, 300);
	}
	else if (!strcmp(name, "uncovered"))
	{
		/* surfaces past the materials' last: the original asserts, so no chunks are kept */
		environment_frames(TRUE, 1 + random_below(30), 200);
		environment_frames(FALSE, 1 + random_below(30), 200);
	}
	else if (!strcmp(name, "other-lists"))
	{
		/* the shadows' and lights' lists, walked */
		long world;

		for (world = 0; world < 300; world++)
		{
			long count, pass;

			make_world(world & 1, TRUE);
			render.environment_surface_count = make_list(render.environment_surface_indices, 0);
			preprocess(7);
			for (pass = 0; pass < 4; pass++)
			{
				count = pass ? make_list(other_list, 0) : render.environment_surface_count;
				if (!pass)
				{
					memcpy(other_list, render.environment_surface_indices, count * sizeof(long));
					/* another list, at the environment's count, with other surfaces */
					if (count > 1)
					{
						other_list[count - 1] = world_surface_count - 1;
						other_list[0] = 0;
					}
				}
				compare_pass(other_list, count, 7, pass * 3);
			}
		}
	}
	else if (!strcmp(name, "stale"))
	{
		/* the environment's list or triangles changed since the chunks were found */
		long world;

		for (world = 0; world < 300; world++)
		{
			long count, surface_index;

			make_world(world & 1, TRUE);
			render.environment_surface_count = count = make_list(render.environment_surface_indices, 0);
			preprocess(11);
			if (count > 1)
			{
				render.environment_surface_count = count - 1;
				compare_pass(render.environment_surface_indices, count - 1, 11, 0);
				render.environment_surface_count = count;
			}
			compare_pass(render.environment_surface_indices, count, 12, 0);
			compare_pass(render.environment_surface_indices, count, 11, 0);
			/* the list written again at its count without the chunks found, for other triangles */
			for (surface_index = 0; surface_index < count; surface_index++)
			{
				render.environment_surface_indices[surface_index] = surface_index;
			}
			compare_pass(render.environment_surface_indices, count, 12, 0);
		}
	}
	else if (!strcmp(name, "invalid"))
	{
		/* a frame without the environment's triangles after one with them */
		long world;

		for (world = 0; world < 300; world++)
		{
			long count, surface_index;

			make_world(world & 1, TRUE);
			render.environment_surface_count = count = make_list(render.environment_surface_indices, 0);
			preprocess(9);
			for (surface_index = 0; surface_index < count; surface_index++)
			{
				render.environment_surface_indices[surface_index] = surface_index;
			}
			preprocess(NONE);
			compare_pass(render.environment_surface_indices, count, 9, 0);
		}
	}
	else if (!strcmp(name, "empty"))
	{
		make_world(TRUE, TRUE);
		render.environment_surface_count = 0;
		preprocess(4);
		compare_pass(render.environment_surface_indices, 0, 4, 0);
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
