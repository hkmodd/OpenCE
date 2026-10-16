#include "harness.h"
#include <math.h>
#include <pthread.h>
#include <time.h>

/* render_particles (render_particles.c) with display.interpolation_threads:
the real worker threads (worker_pool.c) work the particles' sprites out
while the game's thread puts them in their groups. The world is made of
particles of every kind: loose, on an object, on an object gone, on a node
an object does not have, on a local player's first-person weapon, drawn
and too small to draw, of a definition that is not a tag or that its index
names wrongly, and of a sprite whose tags build_sprite_prepare would log
reading. build_sprite's halves are stand-ins that keep all
they were given, so that what is drawn, in what order, what is deleted and
what each particle is left with can be compared with the original's loop
(reference_render_particles, below) run on the same world. Each case draws
three frames of three worlds. A watchdog fails a case that hangs. */
#define WORKERS 4
#define PARTICLES 3000
#define OBJECTS 64
#define MAXIMUM_LOCAL_PLAYERS 4
#define MAXIMUM_LOG 40000
#define OBJECT_INDEX(index) (0x56780000L | (index))
#define PARTICLE_TAG 0x70617274L
/* the salt of a definition's index that names it (an index without one
names it too) */
#define DEFINITION_SALT 0x7AB0L

typedef unsigned short word;
typedef unsigned long pixel32;
typedef struct { real alpha, red, green, blue; } real_argb_color;

#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))

#include "config.inc"

enum
{
	_particle_datum_dont_draw_first_person_bit,
	_particle_datum_dont_draw_third_person_bit,
	_particle_datum_attached_to_local_player_bit,
	_particle_datum_u_mirror_bit,
	_particle_datum_v_mirror_bit,
};

enum
{
	_build_sprites_first_person_bit,
	_build_sprites_screen_space_bit,
	_build_sprites_valid_bit,
};

enum
{
	_build_sprite_u_mirror_bit,
	_build_sprite_v_mirror_bit,
};

struct bitmap_group_sprite { int unused; };
struct bitmap_data { int unused; };

#include "structures.inc"

struct render_frustum { real projection_j; real depth; real tag; };
struct shader_effect_definition { real secondary_map_radius; };
struct particle_definition
{
	struct { long index; } bitmap;
	struct shader_effect_definition shader;
	real lod_cutoff, runtime_oo_width, minimum_pixels, fade_in_time, fade_out_time;
	short sprite_orientation;
};
struct build_sprite_data
{
	long bitmap_group_index;
	struct shader_effect_definition const *shader;
	unsigned long flags;
	short maximum_sprite_count;
};
struct particle_location { short cluster_index; };
struct particle_datum
{
	struct datum_header header;
	short local_player_index;
	word flags;
	long definition_index;
	struct particle_location location;
	long object_index;
	short node_index;
	real_point3d position;
	real_vector3d direction;
	real lifespan, age, radius, rotation;
	short sequence_index, frame_index;
	real_argb_color color;
	long last_rendered_frame_index;
};
struct object_datum { struct { struct { short size; } node_matrices; } object; };

static void platform_log(const char *format, ...) { (void)format; }

#include "worker_pool.inc"

#define profile_enter(section) ((void)0)
#define profile_exit(section) ((void)0)

static struct
{
	short local_player_index;
	struct render_frustum frustum;
	long frame_index;
} render;
static boolean render_particles_enabled = TRUE;
static struct data_array *particle_data;
static struct particle_definition definitions[64];
static short definition_count;
static struct object_datum objects[OBJECTS];
static real_matrix4x3 nodes[OBJECTS][4];
static real_matrix4x3 first_person_nodes[4];
static struct bitmap_group_sprite sprite_stand_in;
static struct bitmap_data bitmap_stand_ins[4];
static struct render_frustum frustum_outside_first_person;
static pthread_t game_thread;
/* workers inside render_particle_prepare, and the particles they worked out */
static long workers_preparing;
static long worker_prepared;
/* every worker's render_particle_prepare slowed, and every hundredth
commit on the game's thread */
static int slow_workers;
static int slow_commits;

/* what was drawn: groups begun and ended, sprites put in them */
enum { _log_begin, _log_commit, _log_end };
struct log_entry
{
	int kind;
	long bitmap_group_index;
	unsigned long flags;
	short count;
	real secondary_map_radius;
	struct build_sprite_prepared sprite;
};
static struct log_entry log_entries[MAXIMUM_LOG];
static long log_count;
static short deleted[PARTICLES];
static long deleted_count;

static boolean on_game_thread(void) { return pthread_equal(pthread_self(), game_thread); }

/* a check made on any thread: a worker's failure is kept for the game's
thread to report (a worker's exit ends the process before it is told) */
static const char *worker_failure;
#define THREAD_CHECK(condition, message) \
	do \
	{ \
		if (!(condition)) \
		{ \
			CHECK(!on_game_thread(), "%s", message); \
			__atomic_store_n(&worker_failure, message, __ATOMIC_RELEASE); \
		} \
	} while (0)

static void pause_for(long microseconds)
{
	struct timespec wait;

	wait.tv_sec = 0;
	wait.tv_nsec = microseconds * 1000;
	nanosleep(&wait, NULL);
}

static long data_next_index(struct data_array *array, long index)
{
	long absolute_index = index == NONE ? 0 : DATUM_INDEX_TO_ABSOLUTE_INDEX(index) + 1;

	for (; absolute_index < array->maximum_count; absolute_index++)
	{
		struct datum_header *header = (struct datum_header *)(array->data + absolute_index * array->size);

		if (header->identifier)
			return ((long)(unsigned short)header->identifier << 16) | absolute_index;
	}
	return NONE;
}

#define particle_get(index) ((struct particle_datum *)datum_get(particle_data, (index)))

/* every sixth definition is not a tag, and an index of the wrong salt names
none: the real tag_get logs these and zeroes the empty tag again, which
only the game's thread may do */
static boolean definition_is_tag(long index)
{
	return (index & 0xFFFF) % 6 != 5 && (!(index & 0xFFFF0000) || ((index >> 16) & 0xFFFF) == DEFINITION_SALT);
}

static boolean tag_get_gives_tag(long group_tag, long tag_index)
{
	CHECK(group_tag == PARTICLE_TAG, "tag group %lx asked for", group_tag);
	return definition_is_tag(tag_index);
}

static struct particle_definition *particle_definition_get(long index)
{
	THREAD_CHECK(on_game_thread() || definition_is_tag(index), "a definition that is not a tag got off the game's thread");
	return &definitions[index & 0xFFFF];
}

/* a sprite whose sequence, sprite or bitmap is not in its block, or whose
bitmap group is not a tag, logs as it is worked out */
static boolean build_sprite_reads_tags_only(long bitmap_group_index, short sequence_index, short sprite_index)
{
	return (bitmap_group_index + sequence_index * 16 + sprite_index) % 11 != 5;
}

static void particle_delete(long particle_index)
{
	THREAD_CHECK(on_game_thread(), "a particle deleted off the game's thread");
	CHECK(__atomic_load_n(&workers_preparing, __ATOMIC_ACQUIRE) == 0,
		"particle %ld deleted while a worker works particles out", particle_index);
	deleted[deleted_count++] = (short)particle_index;
	datum_delete(particle_data, particle_index);
}

static real particle_get_radius(long particle_index)
{
	struct particle_datum *particle = particle_get(particle_index);

	/* (the real one reads the particle's definition) */
	particle_definition_get(particle->definition_index);
	return particle->radius * (1.0f + particle->age / particle->lifespan);
}

static boolean local_player_is_first_person(short local_player_index) { return TRUE; }
static boolean render_location_visible(struct particle_location const *location)
{
	return location->cluster_index % 7 != 3;
}
static boolean viewmodel_draws_geometry(boolean first_person_weapon) { return TRUE; }

static void *object_try_and_get(long object_index)
{
	long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	return index % 5 == 4 ? NULL : &objects[index];
}
static struct object_datum *object_get(long object_index)
{
	CHECK(object_try_and_get(object_index), "object %lx gone", object_index);
	return &objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)];
}

/* the real one logs an error for a node the object has no matrix for */
static real_matrix4x3 *object_get_node_matrix(long object_index, short node_index)
{
	struct object_datum *object = object_get(object_index);

	if (node_index < 0 || node_index >= object->object.node_matrices.size / (short)sizeof(real_matrix4x3))
	{
		THREAD_CHECK(on_game_thread(), "a node an object has not asked for off the game's thread");
		node_index = 0;
	}
	return &nodes[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)][node_index];
}

static real_matrix4x3 const *first_person_weapon_get_node_matrix(short local_player_index, short node_index)
{
	THREAD_CHECK(on_game_thread(), "a first-person weapon's node asked for off the game's thread");
	return &first_person_nodes[node_index & 3];
}

static void matrix4x3_transform_point(real_matrix4x3 const *matrix, real_point3d const *point, real_point3d *result)
{
	result->x = point->x * matrix->scale * matrix->forward.i + matrix->position.x;
	result->y = point->y * matrix->scale * matrix->left.j + matrix->position.y;
	result->z = point->z * matrix->scale * matrix->up.k + matrix->position.z;
}

static void matrix4x3_transform_normal(real_matrix4x3 const *matrix, real_vector3d const *vector,
	real_vector3d *result)
{
	result->i = vector->i * matrix->forward.i;
	result->j = vector->j * matrix->left.j;
	result->k = vector->k * matrix->up.k;
}

static real render_frustum_sphere_diameter_in_pixels(struct render_frustum const *frustum, real_point3d const *point,
	real radius)
{
	real depth = fabsf(point->z - frustum->depth);

	THREAD_CHECK(on_game_thread() || frustum != &render.frustum,
		"a worker given the frustum the first-person weapon's projection changes");
	return (frustum->projection_j / (depth > 0.1f ? depth : 0.1f)) * radius * 2.0f;
}

/* the first-person weapon's projection (view_fov.c) changes the frustum while
its groups are built */
static void build_sprites_begin(struct build_sprite_data *data, short maximum_sprite_count, long bitmap_group_index,
	struct shader_effect_definition const *shader, unsigned long flags)
{
	struct log_entry *entry = &log_entries[log_count++];

	CHECK(log_count < MAXIMUM_LOG, "log full");
	if (TEST_FLAG(flags, _build_sprites_first_person_bit))
	{
		frustum_outside_first_person = render.frustum;
		render.frustum.projection_j *= 1.5f;
		render.frustum.tag += 100.0f;
	}
	data->bitmap_group_index = bitmap_group_index;
	data->shader = shader;
	data->flags = flags | FLAG(_build_sprites_valid_bit);
	data->maximum_sprite_count = maximum_sprite_count;
	memset(entry, 0, sizeof(*entry));
	entry->kind = _log_begin;
	entry->bitmap_group_index = bitmap_group_index;
	entry->flags = flags;
	entry->count = maximum_sprite_count;
}

static void build_sprites_end(struct build_sprite_data *data)
{
	struct log_entry *entry = &log_entries[log_count++];

	CHECK(log_count < MAXIMUM_LOG, "log full");
	memset(entry, 0, sizeof(*entry));
	entry->kind = _log_end;
	entry->secondary_map_radius = data->shader->secondary_map_radius;
	if (TEST_FLAG(data->flags, _build_sprites_first_person_bit))
		render.frustum = frustum_outside_first_person;
	data->flags = 0;
}

/* keeps everything it was given (build_sprite_prepare reads only these of
the data: its bitmap group, its shader and whether it is in screen space) */
static void build_sprite_prepare(struct build_sprite_data const *data, struct render_frustum const *frustum,
	short mode, short sequence_index, short sprite_index, real_point3d const *untransformed_origin,
	real_vector3d const *untransformed_direction, real rotation, real scale, real_argb_color const *color, real fade,
	unsigned long flags, struct build_sprite_prepared *prepared)
{
	THREAD_CHECK(on_game_thread() || build_sprite_reads_tags_only(data->bitmap_group_index, sequence_index, sprite_index),
		"a sprite whose tags log worked out off the game's thread");
	memset(prepared, 0, sizeof(*prepared));
	prepared->sprite = &sprite_stand_in;
	prepared->bitmap = &bitmap_stand_ins[data->bitmap_group_index & 3];
	prepared->sequence_index = sequence_index;
	prepared->sprite_index = sprite_index;
	prepared->color = flags | (unsigned long)mode << 4 |
		(unsigned long)TEST_FLAG(data->flags, _build_sprites_screen_space_bit) << 8;
	prepared->coverage = fade;
	prepared->origin = *untransformed_origin;
	prepared->points[0].x = untransformed_direction->i;
	prepared->points[0].y = untransformed_direction->j;
	prepared->points[0].z = untransformed_direction->k;
	prepared->points[1].x = rotation;
	prepared->points[1].y = scale;
	prepared->points[1].z = frustum->tag + frustum->projection_j;
	prepared->points[2].x = color->alpha;
	prepared->points[2].y = color->red;
	prepared->points[2].z = color->green;
	prepared->points[3].x = color->blue;
	prepared->points[3].y = (real)(long)(size_t)data->shader;
	prepared->points[3].z = (real)data->bitmap_group_index;
}

static void build_sprite_commit(struct build_sprite_data *data, struct build_sprite_prepared const *prepared)
{
	struct log_entry *entry = &log_entries[log_count++];

	THREAD_CHECK(on_game_thread(), "a sprite committed off the game's thread");
	CHECK(TEST_FLAG(data->flags, _build_sprites_valid_bit), "a sprite committed outside a group");
	CHECK(log_count < MAXIMUM_LOG, "log full");
	memset(entry, 0, sizeof(*entry));
	entry->kind = _log_commit;
	entry->sprite = *prepared;
	if (__atomic_load_n(&slow_commits, __ATOMIC_RELAXED) && log_count % 100 == 0)
		pause_for(1000);
}

static void build_sprite(struct build_sprite_data *data, short mode, short sequence_index, short sprite_index,
	real_point3d const *untransformed_origin, real_vector3d const *untransformed_direction, real rotation, real scale,
	real_argb_color const *color, real fade, unsigned long flags)
{
	struct build_sprite_prepared prepared;

	build_sprite_prepare(data, &render.frustum, mode, sequence_index, sprite_index, untransformed_origin,
		untransformed_direction, rotation, scale, color, fade, flags, &prepared);
	build_sprite_commit(data, &prepared);
}

#include "under_test.inc"

/* render_particle_prepare counted and, on a worker, slowed */
static void render_particle_prepare(struct rendered_particle_datum const *rendered_particle,
	struct particle_definition const *definition, struct build_sprite_data const *sprite_data,
	struct render_frustum const *frustum, boolean game_thread_only, struct prepared_particle *prepared)
{
	if (on_game_thread())
	{
		render_particle_prepare_real(rendered_particle, definition, sprite_data, frustum, game_thread_only, prepared);
		return;
	}
	__atomic_add_fetch(&workers_preparing, 1, __ATOMIC_ACQ_REL);
	if (__atomic_load_n(&slow_workers, __ATOMIC_RELAXED))
		pause_for(20);
	render_particle_prepare_real(rendered_particle, definition, sprite_data, frustum, game_thread_only, prepared);
	__atomic_add_fetch(&worker_prepared, 1, __ATOMIC_RELAXED);
	__atomic_sub_fetch(&workers_preparing, 1, __ATOMIC_ACQ_REL);
}

/* the original's loop over the groups, building each sprite whole where
it is reached (January's render_particles after its sort) */
static void reference_render_particles(void)
{
	struct rendered_particle_datum rendered_particles[MAXIMUM_RENDERED_PARTICLES];
	short group_particle_counts[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	short rendered_particle_count = 0;
	short group_count = 0;
	struct build_sprite_data sprite_data;
	real_point3d position;
	real_vector3d direction;
	short local_player_index = render.local_player_index;
	long particle_index;

	for (particle_index = data_next_index(particle_data, NONE); particle_index != NONE;
		particle_index = data_next_index(particle_data, particle_index))
	{
		struct particle_datum *particle = particle_get(particle_index);
		boolean owned_by_local_player = particle->local_player_index == local_player_index;

		if (render_location_visible(&particle->location) &&
			(!TEST_FLAG(particle->flags, _particle_datum_dont_draw_first_person_bit) || !owned_by_local_player) &&
			(!TEST_FLAG(particle->flags, _particle_datum_dont_draw_third_person_bit) || owned_by_local_player))
		{
			struct rendered_particle_datum *rendered_particle = &rendered_particles[rendered_particle_count++];

			rendered_particle->particle_index = (short)particle_index;
			rendered_particle->definition_index = (short)particle->definition_index;
			rendered_particle->cluster_index = particle->location.cluster_index;
			rendered_particle->attached_to_first_person_weapon = owned_by_local_player &&
				TEST_FLAG(particle->flags, _particle_datum_dont_draw_third_person_bit);
		}
	}
	if (!rendered_particle_count)
		return;
	qsort(rendered_particles, rendered_particle_count, sizeof(struct rendered_particle_datum),
		compare_rendered_particles);
	{
		struct rendered_particle_datum *rendered_particle = rendered_particles;
		short *current = NULL;
		short previous_definition_index = NONE, previous_cluster_index = NONE;
		boolean previous_first_person = FALSE;

		while (rendered_particle_count-- > 0)
		{
			if (rendered_particle->definition_index == previous_definition_index &&
				rendered_particle->cluster_index == previous_cluster_index &&
				rendered_particle->attached_to_first_person_weapon == previous_first_person)
			{
				(*current)++;
			}
			else
			{
				if (group_count >= MAXIMUM_RENDERED_PARTICLE_GROUPS)
					break;
				current = &group_particle_counts[group_count++];
				*current = 1;
				previous_definition_index = rendered_particle->definition_index;
				previous_cluster_index = rendered_particle->cluster_index;
				previous_first_person = rendered_particle->attached_to_first_person_weapon;
			}
			rendered_particle++;
		}
	}
	{
		struct rendered_particle_datum *rendered_particle = rendered_particles;
		short group_index;

		for (group_index = 0; group_index < group_count; group_index++)
		{
			struct particle_definition *definition = particle_definition_get(rendered_particle->definition_index);
			short particle_count = group_particle_counts[group_index];
			real total_radius = 0.0f;
			short built_particle_count = 0;

			build_sprites_begin(&sprite_data, particle_count, definition->bitmap.index, &definition->shader,
				rendered_particle->attached_to_first_person_weapon ? FLAG(_build_sprites_first_person_bit) : 0);
			while (particle_count-- > 0)
			{
				struct particle_datum *particle = particle_get(rendered_particle->particle_index);
				real radius = particle_get_radius(rendered_particle->particle_index);

				if (particle->object_index != NONE)
				{
					real_matrix4x3 const *node_matrix;

					if (TEST_FLAG(particle->flags, _particle_datum_attached_to_local_player_bit))
						node_matrix = first_person_weapon_get_node_matrix(particle->local_player_index,
							particle->node_index);
					else if (object_try_and_get(particle->object_index))
						node_matrix = object_get_node_matrix(particle->object_index, particle->node_index);
					else
					{
						particle_delete(rendered_particle->particle_index);
						rendered_particle++;
						continue;
					}
					matrix4x3_transform_point(node_matrix, &particle->position, &position);
					matrix4x3_transform_normal(node_matrix, &particle->direction, &direction);
				}
				else
				{
					position = particle->position;
					direction = particle->direction;
					particle->object_index = NONE;
				}
				{
					real diameter = render_frustum_sphere_diameter_in_pixels(&render.frustum, &position, radius);

					if (diameter > definition->lod_cutoff)
					{
						real fade = 1.0f;
						real scale = (radius + radius) * definition->runtime_oo_width;
						real remaining_life = particle->lifespan - particle->age;
						unsigned long flags = 0;

						built_particle_count++;
						total_radius += radius;
						if (diameter < definition->minimum_pixels)
							scale = (definition->minimum_pixels / diameter) * scale;
						if (definition->fade_in_time > 0.0f && particle->age < definition->fade_in_time)
							fade = particle->age / definition->fade_in_time;
						if (definition->fade_out_time > 0.0f && remaining_life < definition->fade_out_time)
							fade *= remaining_life / definition->fade_out_time;
						SET_FLAG(flags, _build_sprite_u_mirror_bit,
							TEST_FLAG(particle->flags, _particle_datum_u_mirror_bit));
						SET_FLAG(flags, _build_sprite_v_mirror_bit,
							TEST_FLAG(particle->flags, _particle_datum_v_mirror_bit));
						build_sprite(&sprite_data, definition->sprite_orientation, particle->sequence_index,
							particle->frame_index, &position, &direction, particle->rotation, scale,
							&particle->color, fade, flags);
						particle->last_rendered_frame_index = render.frame_index;
					}
				}
				rendered_particle++;
			}
			((struct shader_effect_definition *)sprite_data.shader)->secondary_map_radius =
				built_particle_count ? total_radius / built_particle_count : 0.0f;
			build_sprites_end(&sprite_data);
		}
	}
}

static unsigned long random_state;

static unsigned long random_next(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (random_state >> 8) & 0xFFFFFF;
}

static real random_real(real low, real high)
{
	return low + (high - low) * (real)random_next() / (real)0xFFFFFF;
}

/* a world of every kind of particle, the same for the same seed */
static void make_world(unsigned long seed, short definitions_made)
{
	long index;

	random_state = seed;
	if (particle_data)
	{
		free(particle_data->data);
		free(particle_data);
	}
	particle_data = game_state_data_new("particle", MAXIMUM_RENDERED_PARTICLES, sizeof(struct particle_datum));
	definition_count = definitions_made;
	for (index = 0; index < definition_count; index++)
	{
		struct particle_definition *definition = &definitions[index];

		memset(definition, 0, sizeof(*definition));
		definition->bitmap.index = 0x1000 + index;
		definition->lod_cutoff = random_real(0.5f, 4.0f);
		definition->runtime_oo_width = random_real(0.01f, 0.2f);
		definition->minimum_pixels = random_real(2.0f, 8.0f);
		definition->fade_in_time = index % 3 ? random_real(0.1f, 1.0f) : 0.0f;
		definition->fade_out_time = index % 4 ? random_real(0.1f, 1.0f) : 0.0f;
		definition->sprite_orientation = (short)(index % 3);
	}
	for (index = 0; index < OBJECTS; index++)
	{
		short node_index;

		objects[index].object.node_matrices.size = (short)(((index % 4) + 1) * sizeof(real_matrix4x3));
		for (node_index = 0; node_index < 4; node_index++)
		{
			real_matrix4x3 *node = &nodes[index][node_index];

			node->scale = random_real(0.5f, 2.0f);
			node->forward.i = random_real(-1.0f, 1.0f);
			node->left.j = random_real(-1.0f, 1.0f);
			node->up.k = random_real(-1.0f, 1.0f);
			node->position.x = random_real(-10.0f, 10.0f);
			node->position.y = random_real(-10.0f, 10.0f);
			node->position.z = random_real(-10.0f, 10.0f);
		}
	}
	for (index = 0; index < 4; index++)
	{
		first_person_nodes[index].scale = 1.0f;
		first_person_nodes[index].forward.i = 1.0f;
		first_person_nodes[index].left.j = 1.0f;
		first_person_nodes[index].up.k = 1.0f;
		first_person_nodes[index].position.z = random_real(0.1f, 1.0f);
	}
	for (index = 0; index < PARTICLES; index++)
	{
		long particle_index = datum_new(particle_data);
		struct particle_datum *particle = particle_get(particle_index);
		unsigned long kind = random_next() % 12;

		particle->local_player_index = random_next() % 3 ? 1 : 0;
		particle->definition_index = (long)(random_next() % definition_count);
		if (kind == 10)
			particle->definition_index |= DEFINITION_SALT << 16;
		else if (kind == 11)
			particle->definition_index |= 0x5555L << 16;
		particle->location.cluster_index = (short)(random_next() % 21);
		particle->flags = (word)(random_next() & (FLAG(_particle_datum_dont_draw_first_person_bit) |
			FLAG(_particle_datum_dont_draw_third_person_bit) | FLAG(_particle_datum_u_mirror_bit) |
			FLAG(_particle_datum_v_mirror_bit)));
		particle->object_index = NONE;
		if (kind < 4)
		{
			/* on an object, of the nodes it has, or of one it has not, or gone */
			particle->object_index = OBJECT_INDEX(random_next() % OBJECTS);
			particle->node_index = (short)(random_next() % 6) - 1;
		}
		else if (kind == 4)
		{
			particle->object_index = OBJECT_INDEX(random_next() % OBJECTS);
			particle->node_index = (short)(random_next() % 4);
			particle->flags |= FLAG(_particle_datum_attached_to_local_player_bit);
		}
		particle->position.x = random_real(-20.0f, 20.0f);
		particle->position.y = random_real(-20.0f, 20.0f);
		particle->position.z = random_real(-20.0f, 20.0f);
		particle->direction.i = random_real(-1.0f, 1.0f);
		particle->direction.j = random_real(-1.0f, 1.0f);
		particle->direction.k = random_real(-1.0f, 1.0f);
		particle->lifespan = random_real(1.0f, 3.0f);
		particle->age = random_real(0.0f, particle->lifespan);
		particle->radius = random_real(0.01f, 0.5f);
		particle->rotation = random_real(-3.0f, 3.0f);
		particle->sequence_index = (short)(random_next() % 4);
		particle->frame_index = (short)(random_next() % 16);
		particle->color.alpha = random_real(0.0f, 1.0f);
		particle->color.red = random_real(0.0f, 1.0f);
		particle->color.green = random_real(0.0f, 1.0f);
		particle->color.blue = random_real(0.0f, 1.0f);
		particle->last_rendered_frame_index = NONE;
	}
	/* a few gaps, deleted before the frame */
	for (index = 0; index < PARTICLES; index += 37)
		datum_delete(particle_data, index);
	render.local_player_index = 0;
	render.frustum.projection_j = random_real(200.0f, 600.0f);
	render.frustum.depth = random_real(-5.0f, 5.0f);
	render.frustum.tag = random_real(0.0f, 1.0f);
	render.frame_index = (long)(seed & 0xFFFF);
	log_count = 0;
	deleted_count = 0;
}

struct outcome
{
	struct log_entry *log;
	long log_count;
	short deleted[PARTICLES];
	long deleted_count;
	struct particle_datum particles[MAXIMUM_RENDERED_PARTICLES];
	unsigned char headers[MAXIMUM_RENDERED_PARTICLES];
	long count;
};

static struct outcome reference_outcome, test_outcome;

static void keep_outcome(struct outcome *outcome)
{
	long index;

	if (!outcome->log)
		outcome->log = malloc(sizeof(log_entries));
	memcpy(outcome->log, log_entries, sizeof(struct log_entry) * log_count);
	outcome->log_count = log_count;
	memcpy(outcome->deleted, deleted, sizeof(short) * deleted_count);
	outcome->deleted_count = deleted_count;
	memcpy(outcome->particles, particle_data->data, sizeof(struct particle_datum) * MAXIMUM_RENDERED_PARTICLES);
	for (index = 0; index < MAXIMUM_RENDERED_PARTICLES; index++)
		outcome->headers[index] = outcome->particles[index].header.identifier != 0;
	outcome->count = particle_data->count;
}

static void compare_outcomes(int frame)
{
	long index;

	CHECK(test_outcome.log_count == reference_outcome.log_count, "frame %d: %ld drawn, not %ld", frame,
		test_outcome.log_count, reference_outcome.log_count);
	for (index = 0; index < reference_outcome.log_count; index++)
	{
		struct log_entry const *expected = &reference_outcome.log[index];
		struct log_entry const *got = &test_outcome.log[index];

		CHECK(memcmp(expected, got, sizeof(*expected)) == 0,
			"frame %d: entry %ld of %ld (kind %d) not the original's", frame, index, reference_outcome.log_count,
			expected->kind);
	}
	CHECK(test_outcome.deleted_count == reference_outcome.deleted_count, "frame %d: %ld deleted, not %ld", frame,
		test_outcome.deleted_count, reference_outcome.deleted_count);
	CHECK(memcmp(test_outcome.deleted, reference_outcome.deleted, sizeof(short) * reference_outcome.deleted_count) == 0,
		"frame %d: not deleted in the original's order", frame);
	CHECK(test_outcome.count == reference_outcome.count, "frame %d: %ld particles left, not %ld", frame,
		test_outcome.count, reference_outcome.count);
	CHECK(memcmp(test_outcome.particles, reference_outcome.particles, sizeof(test_outcome.particles)) == 0,
		"frame %d: the particles not left as the original leaves them", frame);
}

/* three frames of three worlds, each drawn by the original's loop and by
render_particles, compared; the workers' share of the particles */
static long frames_compared(short definitions_made)
{
	int frame;
	long shared = 0;

	for (frame = 0; frame < 3; frame++)
	{
		unsigned long seed = 0x1234UL + 7919UL * (unsigned long)frame + (unsigned long)definitions_made;

		make_world(seed, definitions_made);
		reference_render_particles();
		keep_outcome(&reference_outcome);
		CHECK(reference_outcome.deleted_count > 0, "frame %d: no particle on an object gone", frame);
		make_world(seed, definitions_made);
		worker_prepared = 0;
		render_particles();
		/* (what a worker may still be returning from) */
		worker_pool_end();
		CHECK(!worker_failure, "frame %d: %s", frame, worker_failure);
		shared += worker_prepared;
		keep_outcome(&test_outcome);
		compare_outcomes(frame);
	}
	return shared;
}

static void *watchdog(void *parameter)
{
	struct timespec wait = { 20, 0 };

	nanosleep(&wait, NULL);
	fprintf(stderr, "the case hung\n");
	exit(1);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *test = argc > 1 ? argv[1] : "";
	pthread_t watchdog_thread;

	game_thread = pthread_self();
	pthread_create(&watchdog_thread, NULL, watchdog, NULL);
	if (!strcmp(test, "same-as-original"))
	{
		/* the workers take a share (the game's thread slowed a little), and
		what is drawn and deleted is the original's */
		CHECK(worker_pool_start(WORKERS) == WORKERS, "no workers");
		slow_commits = 1;
		CHECK(frames_compared(6) > 0, "the workers worked out no particle");
	}
	else if (!strcmp(test, "without-workers"))
	{
		/* (Android and the web) the game's thread works every chunk out */
		CHECK(frames_compared(6) == 0, "a worker without workers");
	}
	else if (!strcmp(test, "waits-for-workers"))
	{
		/* the workers slowed, so that the game's thread reaches chunks they
		have in hand and waits for them */
		CHECK(worker_pool_start(WORKERS) == WORKERS, "no workers");
		slow_workers = 1;
		CHECK(frames_compared(6) > 0, "the workers worked out no particle");
	}
	else if (!strcmp(test, "group-limit"))
	{
		/* more groups than are drawn: those past the last drawn are not
		worked out, and the rest is the original's */
		CHECK(worker_pool_start(WORKERS) == WORKERS, "no workers");
		slow_workers = 1;
		CHECK(frames_compared(60) > 0, "the workers worked out no particle");
	}
	else
	{
		fprintf(stderr, "no case %s\n", test);
		return 2;
	}
	return 0;
}
