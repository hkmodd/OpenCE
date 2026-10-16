/*
RENDER_PARTICLES.C

symbols in this file:
0017BD20 00b0:
	_local_player_is_first_person (0000)
0017BDD0 0030:
	_compare_rendered_particles (0000)
0017BE00 0490:
	_render_particles (0000)
0030E180 05f8:
	_render_particles_section (0000)
*/

/* ---------- headers */

#include <stdlib.h>

#include "camera/director.h"
#include "cseries/cseries.h"
#include "cseries/profile.h"
#include "effects/particles.h"
#include "game/players.h"
#include "interface/first_person_weapons.h"
#include "render.h"
#include "render_cameras_internal.h"
#include "render_particles.h"
#include "render_sprite.h"
#include "tag_files/tag_groups.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "view_fov.h" /* port: port/linux/game/view_fov.c */

/* ---------- constants */

enum
{
	/* render_particles lists every visible particle in an array of this size
	without a bound check, so it must stay the size of the native builds'
	particle pool (effects/particles.c, halo_port_capacity.h) */
	MAXIMUM_RENDERED_PARTICLES = HALO_PORT_MAXIMUM_PARTICLES,
	/* the native builds' (halo_port_capacity.h: the Xbox's 512 on Android
	and the Xbox builds) */
	MAXIMUM_RENDERED_PARTICLE_GROUPS = HALO_PORT_MAXIMUM_RENDERED_PARTICLE_GROUPS,
	/* port: the rendered particles a thread works out at a time */
	PREPARED_PARTICLE_CHUNK_SIZE = 16,
};

/* port: what render_particle_prepare made of a rendered particle */
enum
{
	/* for the game's thread to work out as it reaches it */
	_prepared_particle_serial,
	/* its object gone: to be deleted */
	_prepared_particle_deleted,
	/* smaller than its definition's lod cutoff */
	_prepared_particle_hidden,
	_prepared_particle_built,
};

/* ---------- macros */

/* ---------- structures */

struct rendered_particle_datum
{
	short particle_index;
	short definition_index;
	short cluster_index;
	boolean attached_to_first_person_weapon;
	byte pad;
};

typedef char rendered_particle_size_assert[
	sizeof(struct rendered_particle_datum) == 8 ? 1 : -1];

/* port: a rendered particle worked out (render_particle_prepare) */
struct prepared_particle
{
	struct build_sprite_prepared sprite;
	real radius;
	short state;
};

/* ---------- prototypes */

/* port: (render_particles' sprites worked out ahead) */
static void render_particle_prepare(
	struct rendered_particle_datum const *rendered_particle,
	struct particle_definition const *definition,
	struct build_sprite_data const *sprite_data,
	struct render_frustum const *frustum,
	boolean reached,
	struct prepared_particle *prepared);
static void render_particles_prepare_chunk(
	long chunk_index);
static void render_particles_prepare_job(
	void);
static struct prepared_particle const *render_particles_get_prepared(
	short index);

/* ---------- globals */

static struct profile_section render_particles_section =
{
	"render_particles",
	NONE,
	TRUE,
};

/* port: the sprites of the particles render_particles draws, worked out
ahead a chunk at a time by the worker threads (display.interpolation_threads)
and by the game's thread as it reaches them, then put in their groups in
order on the game's thread; a chunk's generation is prepared_generation
once it is worked out */
static struct prepared_particle prepared_particles[MAXIMUM_RENDERED_PARTICLES];
static unsigned long prepared_chunk_generations[
	MAXIMUM_RENDERED_PARTICLES / PREPARED_PARTICLE_CHUNK_SIZE];
static struct rendered_particle_datum const *prepared_rendered_particles;
static struct render_frustum prepared_frustum;
static unsigned long prepared_generation;
static long prepared_particle_total;
static long prepared_chunk_count;
static long prepared_next_chunk;
static short prepared_deleted_particles[MAXIMUM_RENDERED_PARTICLES];

typedef char prepared_chunk_size_assert[
	MAXIMUM_RENDERED_PARTICLES % PREPARED_PARTICLE_CHUNK_SIZE == 0 ? 1 : -1];

/* ---------- public code */

boolean local_player_is_first_person(
	short local_player_index)
{
	boolean result =
		director_get_perspective(local_player_index) ==
		_director_perspective_first_person;

	if (!result)
	{
		long player_index = local_player_get_player_index(local_player_index);
		struct player_datum *player = player_get(player_index);

		if (player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);

			if (unit->object.parent_object_index != NONE &&
				unit->unit.parent_seat_index != NONE)
			{
				struct unit_datum *parent_unit =
					unit_get(unit->object.parent_object_index);
				struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(
					&unit_definition_get(parent_unit->definition_index)->unit.seats,
					unit->unit.parent_seat_index,
					struct unit_seat);

				if (TEST_FLAG(seat->flags, _unit_seat_gunner_bit))
					result = TRUE;
			}
		}
	}

	return result;
}

static int __cdecl compare_rendered_particles(
	void const *left_pointer,
	void const *right_pointer)
{
	struct rendered_particle_datum const *left = left_pointer;
	struct rendered_particle_datum const *right = right_pointer;
	int result = left->definition_index - right->definition_index;

	if (!result)
	{
		result = left->cluster_index - right->cluster_index;
		if (!result)
		{
			result =
				left->attached_to_first_person_weapon -
				right->attached_to_first_person_weapon;
		}
	}

	return result;
}

/* port: one byte of a rendered particle's place in compare_rendered_particles'
order, the least significant first: its first-person flag, then its cluster
and its definition, each a short taken in its signed order */
static unsigned int rendered_particle_sort_digit(
	struct rendered_particle_datum const *particle,
	short digit)
{
	unsigned int cluster = (unsigned short)particle->cluster_index ^ 0x8000;
	unsigned int definition = (unsigned short)particle->definition_index ^ 0x8000;
	unsigned int result;

	switch (digit)
	{
	case 0: result = particle->attached_to_first_person_weapon; break;
	case 1: result = cluster & 0xFF; break;
	case 2: result = cluster >> 8; break;
	case 3: result = definition & 0xFF; break;
	default: result = definition >> 8; break;
	}

	return result;
}

/* port: sorts the rendered particles as compare_rendered_particles orders
them, keeping the order of equal ones (their order in particle_data): a
radix sort, a byte at a time, in place of qsort, which left equal particles
in each C library's own order and spent hundreds of microseconds calling
the comparison in a fight (thousands of particles in view). A byte that
every particle shares is skipped, as most of the high ones are. */
static void sort_rendered_particles(
	struct rendered_particle_datum *particles,
	long count)
{
	enum { NUMBER_OF_DIGITS = 5 };
	static struct rendered_particle_datum scratch[MAXIMUM_RENDERED_PARTICLES];
	static long digit_counts[NUMBER_OF_DIGITS][256];
	struct rendered_particle_datum *from = particles;
	struct rendered_particle_datum *to = scratch;
	struct rendered_particle_datum *swap;
	long index;
	short digit;

	csmemset(digit_counts, 0, sizeof(digit_counts));
	for (index = 0; index < count; index++)
	{
		for (digit = 0; digit < NUMBER_OF_DIGITS; digit++)
			digit_counts[digit][rendered_particle_sort_digit(&particles[index], digit)]++;
	}
	for (digit = 0; digit < NUMBER_OF_DIGITS; digit++)
	{
		long *counts = digit_counts[digit];
		long first = 0;
		short value;

		if (count == 0 || counts[rendered_particle_sort_digit(&particles[0], digit)] == count)
			continue;
		/* (each value's first place, then the particles in their order) */
		for (value = 0; value < 256; value++)
		{
			long value_count = counts[value];

			counts[value] = first;
			first += value_count;
		}
		for (index = 0; index < count; index++)
			to[counts[rendered_particle_sort_digit(&from[index], digit)]++] = from[index];
		swap = from;
		from = to;
		to = swap;
	}
	if (from != particles)
		csmemcpy(particles, from, count * sizeof(*particles));

	return;
}

void render_particles(
	void)
{
	struct rendered_particle_datum rendered_particles[MAXIMUM_RENDERED_PARTICLES];
	short group_particle_counts[MAXIMUM_RENDERED_PARTICLE_GROUPS];
	short rendered_particle_count;
	struct build_sprite_data sprite_data;

	rendered_particle_count = 0;
	profile_enter(render_particles_section);

	if (render_particles_enabled)
	{
		short local_player_index = render.local_player_index;
		long particle_index;

		if (local_player_index == NONE ||
			!local_player_is_first_person(local_player_index))
		{
			local_player_index = MAXIMUM_LOCAL_PLAYERS;
		}

		for (particle_index = data_next_index(particle_data, NONE);
			particle_index != NONE;
			particle_index = data_next_index(particle_data, particle_index))
		{
			struct particle_datum *particle = particle_get(particle_index);
			boolean owned_by_local_player =
				particle->local_player_index == local_player_index;

			/* port: not the first-person weapon's with it hidden (view_fov.c) */
			if (render_location_visible(&particle->location) &&
				viewmodel_draws_geometry(owned_by_local_player &&
					TEST_FLAG(particle->flags, _particle_datum_dont_draw_third_person_bit)) &&
				(!TEST_FLAG(
					particle->flags,
					_particle_datum_dont_draw_first_person_bit) ||
					!owned_by_local_player) &&
				(!TEST_FLAG(
					particle->flags,
					_particle_datum_dont_draw_third_person_bit) ||
					owned_by_local_player))
			{
				struct rendered_particle_datum *rendered_particle =
					&rendered_particles[rendered_particle_count++];

				rendered_particle->particle_index = (short)particle_index;
				rendered_particle->definition_index =
					(short)particle->definition_index;
				rendered_particle->cluster_index =
					particle->location.cluster_index;
				/* port: a charge stuck to the viewmodel is drawn with it.
				One that also shows on the body was left on the world's
				view, and the near plane cut it down. */
				rendered_particle->attached_to_first_person_weapon =
					owned_by_local_player &&
					(TEST_FLAG(
						particle->flags,
						_particle_datum_dont_draw_third_person_bit) ||
					TEST_FLAG(
						particle->flags,
						_particle_datum_attached_to_local_player_bit));
			}
		}

		if (rendered_particle_count > 0)
		{
			short group_count = 0;

			/* port: (sort_rendered_particles, not qsort) */
			sort_rendered_particles(rendered_particles, rendered_particle_count);

			{
				struct rendered_particle_datum *rendered_particle =
					rendered_particles;
				short *current_sprite_group = NULL;
				short previous_definition_index = NONE;
				short previous_cluster_index = NONE;
				boolean previous_attached_to_first_person_weapon = FALSE;

				while (rendered_particle_count-- > 0)
				{
					if (rendered_particle->definition_index ==
							previous_definition_index &&
						rendered_particle->cluster_index ==
							previous_cluster_index &&
						rendered_particle->attached_to_first_person_weapon ==
							previous_attached_to_first_person_weapon)
					{
						(*current_sprite_group)++;
					}
					else
					{
						if (group_count >= MAXIMUM_RENDERED_PARTICLE_GROUPS)
							break;

						current_sprite_group =
							&group_particle_counts[group_count++];
						*current_sprite_group = 1;
						previous_definition_index =
							rendered_particle->definition_index;
						previous_cluster_index =
							rendered_particle->cluster_index;
						previous_attached_to_first_person_weapon =
							rendered_particle->attached_to_first_person_weapon;
					}

					rendered_particle++;
				}
			}

			{
				struct rendered_particle_datum *rendered_particle =
					rendered_particles;
				short rendered_particle_index = 0;
				short deleted_particle_count = 0;
				short group_index;

				/* port: the groups' sprites worked out ahead, on the workers
				and as each is reached (render_particles_prepare_job), once
				the workers are back from the last job that read these */
				prepared_particle_total = 0;
				for (group_index = 0; group_index < group_count; group_index++)
					prepared_particle_total += group_particle_counts[group_index];
				worker_pool_end();
				prepared_generation++;
				prepared_rendered_particles = rendered_particles;
				prepared_frustum = render.frustum;
				prepared_chunk_count =
					(prepared_particle_total + PREPARED_PARTICLE_CHUNK_SIZE - 1) /
					PREPARED_PARTICLE_CHUNK_SIZE;
				prepared_next_chunk = 0;
				if (prepared_chunk_count > 1)
					worker_pool_begin(render_particles_prepare_job);

				for (group_index = 0; group_index < group_count; group_index++)
				{
					struct particle_definition *definition =
						particle_definition_get(
							rendered_particle->definition_index);
					short particle_count = group_particle_counts[group_index];
					real total_radius = 0.0f;
					short built_particle_count = 0;

					build_sprites_begin(
						&sprite_data,
						particle_count,
						definition->bitmap.index,
						&definition->shader,
						rendered_particle->attached_to_first_person_weapon
							? FLAG(_build_sprites_first_person_bit)
							: 0);

					while (particle_count-- > 0)
					{
						struct prepared_particle const *prepared =
							render_particles_get_prepared(rendered_particle_index);
						struct prepared_particle serial_prepared;
						struct particle_datum *particle =
							particle_get(rendered_particle->particle_index);

						/* port: one a worker may not work out (the first-person
						weapon's) is worked out here, as the original did */
						if (prepared->state == _prepared_particle_serial)
						{
							render_particle_prepare(
								rendered_particle,
								definition,
								&sprite_data,
								&render.frustum,
								TRUE,
								&serial_prepared);
							prepared = &serial_prepared;
						}

						if (prepared->state == _prepared_particle_deleted)
						{
							/* port: deleted once none is being worked out */
							prepared_deleted_particles[deleted_particle_count++] =
								rendered_particle->particle_index;
						}
						else
						{
							if (particle->object_index == NONE)
							{
								/* January explicitly reasserts the detached sentinel here. */
								particle->object_index = NONE;
							}

							if (prepared->state == _prepared_particle_built)
							{
								built_particle_count++;
								total_radius += prepared->radius;
								build_sprite_commit(&sprite_data, &prepared->sprite);
								particle->last_rendered_frame_index =
									render.frame_index;
							}
						}

						rendered_particle++;
						rendered_particle_index++;
					}

					((struct shader_effect_definition *)sprite_data.shader)->
						secondary_map_radius = built_particle_count ?
						total_radius / built_particle_count : 0.0f;
					build_sprites_end(&sprite_data);
				}

				/* port: every chunk was waited for above, so no worker reads
				a particle now: those whose objects are gone are deleted, in
				the order the original deleted them */
				for (group_index = 0; group_index < deleted_particle_count; group_index++)
					particle_delete(prepared_deleted_particles[group_index]);
			}
		}
	}

	profile_exit(render_particles_section);
	return;
}

/* ---------- private code */

/* port: works out a rendered particle's sprite as render_particles did
before building it, changing nothing. Worked out ahead (reached FALSE), on
any thread, one on the first-person weapon or on a node its object has
not is left for the game's thread to work out as it reaches it (TRUE) */
static void render_particle_prepare(
	struct rendered_particle_datum const *rendered_particle,
	struct particle_definition const *definition,
	struct build_sprite_data const *sprite_data,
	struct render_frustum const *frustum,
	boolean reached,
	struct prepared_particle *prepared)
{
	struct particle_datum const *particle =
		particle_get(rendered_particle->particle_index);
	real_point3d position;
	real_vector3d direction;

	prepared->state = _prepared_particle_serial;
	if (!reached &&
		(rendered_particle->attached_to_first_person_weapon ||
			!tag_get_gives_tag(PARTICLE_TAG, particle->definition_index)))
	{
		return;
	}

	prepared->radius = particle_get_radius(rendered_particle->particle_index);
	if (particle->object_index != NONE)
	{
		real_matrix4x3 const *node_matrix;

		if (TEST_FLAG(
			particle->flags,
			_particle_datum_attached_to_local_player_bit))
		{
			if (!reached)
				return;
			node_matrix =
				first_person_weapon_get_node_matrix(
					particle->local_player_index,
					particle->node_index);
		}
		else if (object_try_and_get(particle->object_index))
		{
			/* a node the object has no matrix for logs an error */
			if (!reached)
			{
				struct object_datum const *object =
					object_get(particle->object_index);

				if (particle->node_index < 0 ||
					particle->node_index >=
						object->object.node_matrices.size / (short)sizeof(real_matrix4x3))
				{
					return;
				}
			}
			node_matrix = object_get_node_matrix(
				particle->object_index,
				particle->node_index);
		}
		else
		{
			prepared->state = _prepared_particle_deleted;
			return;
		}

		matrix4x3_transform_point(
			node_matrix,
			&particle->position,
			&position);
		matrix4x3_transform_normal(
			node_matrix,
			&particle->direction,
			&direction);
	}
	else
	{
		position = particle->position;
		direction = particle->direction;
	}

	prepared->state = _prepared_particle_hidden;
	{
		real radius = prepared->radius;
		real diameter =
			render_frustum_sphere_diameter_in_pixels(
				frustum,
				&position,
				radius);

		if (diameter > definition->lod_cutoff)
		{
			real fade;
			real scale;
			real remaining_life;
			unsigned long flags;
			word particle_flags;

			fade = 1.0f;
			scale =
				(radius + radius) *
				definition->runtime_oo_width;
			remaining_life =
				particle->lifespan - particle->age;

			if (diameter < definition->minimum_pixels)
			{
				scale =
					(definition->minimum_pixels / diameter) * scale;
			}

			if (definition->fade_in_time > 0.0f &&
				particle->age < definition->fade_in_time)
			{
				fade = particle->age / definition->fade_in_time;
			}

			if (definition->fade_out_time > 0.0f &&
				remaining_life < definition->fade_out_time)
			{
				fade *=
					remaining_life / definition->fade_out_time;
			}

			flags = 0;
			particle_flags = particle->flags;
			SET_FLAG(
				flags,
				_build_sprite_u_mirror_bit,
				TEST_FLAG(
					particle_flags,
					_particle_datum_u_mirror_bit));
			SET_FLAG(
				flags,
				_build_sprite_v_mirror_bit,
				TEST_FLAG(
					particle_flags,
					_particle_datum_v_mirror_bit));

			if (!reached &&
				!build_sprite_reads_tags_only(
					sprite_data->bitmap_group_index,
					particle->sequence_index,
					particle->frame_index))
			{
				prepared->state = _prepared_particle_serial;
				return;
			}
			build_sprite_prepare(
				sprite_data,
				frustum,
				definition->sprite_orientation,
				particle->sequence_index,
				particle->frame_index,
				&position,
				&direction,
				particle->rotation,
				scale,
				&particle->color,
				fade,
				flags,
				&prepared->sprite);
			prepared->state = _prepared_particle_built;
		}
	}
	return;
}

/* port: a chunk of the rendered particles worked out, on any thread, with
the frustum render_particles was called with */
static void render_particles_prepare_chunk(
	long chunk_index)
{
	long index = chunk_index * PREPARED_PARTICLE_CHUNK_SIZE;
	long end = index + PREPARED_PARTICLE_CHUNK_SIZE;

	if (end > prepared_particle_total)
		end = prepared_particle_total;
	for (; index < end; index++)
	{
		struct rendered_particle_datum const *rendered_particle =
			&prepared_rendered_particles[index];
		struct particle_definition const *definition;
		struct build_sprite_data sprite_data;

		/* (its definition not a tag: the game's thread logs it) */
		if (!tag_get_gives_tag(PARTICLE_TAG, rendered_particle->definition_index))
		{
			prepared_particles[index].state = _prepared_particle_serial;
			continue;
		}
		definition = particle_definition_get(rendered_particle->definition_index);
		/* what build_sprite_prepare reads of a group not in screen space */
		sprite_data.bitmap_group_index = definition->bitmap.index;
		sprite_data.shader = &definition->shader;
		sprite_data.flags = 0;
		render_particle_prepare(
			rendered_particle,
			definition,
			&sprite_data,
			&prepared_frustum,
			FALSE,
			&prepared_particles[index]);
	}
	__atomic_store_n(
		&prepared_chunk_generations[chunk_index],
		prepared_generation,
		__ATOMIC_RELEASE);
	return;
}

/* port: (the workers) the chunks no thread has taken yet worked out */
static void render_particles_prepare_job(
	void)
{
	long chunk_index;

	while ((chunk_index = __atomic_fetch_add(&prepared_next_chunk, 1, __ATOMIC_RELAXED)) <
		prepared_chunk_count)
	{
		render_particles_prepare_chunk(chunk_index);
	}
	return;
}

/* port: a rendered particle's sprite, its chunk worked out by this thread if
no worker has taken it, or waited for if one has */
static struct prepared_particle const *render_particles_get_prepared(
	short index)
{
	long chunk_index = index / PREPARED_PARTICLE_CHUNK_SIZE;

	while (__atomic_load_n(&prepared_chunk_generations[chunk_index], __ATOMIC_ACQUIRE) !=
		prepared_generation)
	{
		if (__atomic_load_n(&prepared_next_chunk, __ATOMIC_RELAXED) <= chunk_index)
		{
			long claimed_index =
				__atomic_fetch_add(&prepared_next_chunk, 1, __ATOMIC_RELAXED);

			if (claimed_index < prepared_chunk_count)
				render_particles_prepare_chunk(claimed_index);
		}
		else
		{
#if defined(__i386__) || defined(__x86_64__)
			__builtin_ia32_pause();
#endif
		}
	}
	return &prepared_particles[index];
}
