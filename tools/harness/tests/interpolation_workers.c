#include "harness.h"
#include <math.h>
#include <pthread.h>
#include <time.h>

/* OBJECTS objects drawn between two ticks (render_interpolation.c), of
NODES nodes each, with the real worker threads (worker_pool.c) blending
them while the game's thread (main) asks for them. Every blend is counted,
by object and by the thread that made it; one, of the first object on a
worker, can be held (slow_blend) so that the game's thread meets it, and
every blend on a worker slowed (slow_blends). A watchdog fails a case
that hangs. */
#define OBJECTS 200
#define NODES 4
#define MAXIMUM_INTERPOLATED_OBJECTS OBJECTS
#define _object_mask_all 0xFFFFFFFFUL
#define MAXIMUM_LOCAL_PLAYERS 4
#define WORKERS 4
#define OBJECT_INDEX(index) (0x12340000L | (index))

struct observer_result { real_point3d position; };
struct object_datum
{
	struct
	{
		struct { short size; } node_matrices;
		long first_child_object_index, next_object_index;
	} object;
};
struct object_iterator { long index; int done; };

#include "config.inc"

static void platform_log(const char *format, ...) { (void)format; }

#include "worker_pool.inc"

static real_vector3d const zero_vector;
#define global_zero_vector3d (&zero_vector)
static real_matrix4x3 nodes[OBJECTS][NODES];
static struct object_datum objects[OBJECTS];
static real tick_fraction;
static long workers_asked;
static pthread_t game_thread;
static long blends[OBJECTS];
static long worker_blends;
/* the first object's next blend on a worker held until released, and
whether what the blend reads changed while it was held */
static int slow_blend;
static int slow_blend_held;
static int changed_under_blend;
/* every blend on a worker slowed */
static int slow_blends;

static boolean halo_interpolation_enabled(void) { return TRUE; }
static real game_time_get_tick_fraction(void) { return tick_fraction; }
static long player_control_get_unit_index(short local_player_index) { return NONE; }
static long config_integer(const char *name) { return workers_asked; }
static struct object_datum *object_get(long index) { return &objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)]; }
static void object_iterator_new(struct object_iterator *iterator, unsigned long type_flags, byte flags)
{
	iterator->index = NONE;
	iterator->done = FALSE;
}
static void *object_iterator_next(struct object_iterator *iterator)
{
	long next = iterator->index == NONE ? 0 : DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator->index) + 1;

	if (next >= OBJECTS)
		return NULL;
	iterator->index = OBJECT_INDEX(next);
	return &objects[next];
}
static real_matrix4x3 *object_get_node_matrices(long index) { return nodes[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)]; }
static real_point3d *matrix4x3_inverse_transform_point(real_matrix4x3 const *matrix, real_point3d const *point,
	real_point3d *result)
{
	real x = point->x - matrix->position.x, y = point->y - matrix->position.y, z = point->z - matrix->position.z;

	result->x = (x * matrix->forward.i + y * matrix->forward.j + z * matrix->forward.k) / matrix->scale;
	result->y = (x * matrix->left.i + y * matrix->left.j + z * matrix->left.k) / matrix->scale;
	result->z = (x * matrix->up.i + y * matrix->up.j + z * matrix->up.k) / matrix->scale;
	return result;
}

static struct interpolated_object *interpolated_objects;
static struct interpolated_camera interpolated_cameras[MAXIMUM_LOCAL_PLAYERS];
static struct interpolated_first_person interpolated_first_person[MAXIMUM_LOCAL_PLAYERS];
static long interpolated_objects_used;
static long interpolation_tick;
static long interpolation_frame;
static boolean interpolation_rendering;
static real interpolation_fraction = 1.0f;
static long interpolation_workers = NONE;
static long worker_next_record;

static void workers_begin(void);
static void workers_end(void);
/* (counts, then record_blend_real: the generated code's record_blend) */
static void record_blend(struct interpolated_object *record);

#include "under_test.inc"

static void record_blend(struct interpolated_object *record)
{
	long index = (long)(record - interpolated_objects);

	__atomic_add_fetch(&blends[index], 1, __ATOMIC_RELAXED);
	if (!pthread_equal(pthread_self(), game_thread))
	{
		__atomic_add_fetch(&worker_blends, 1, __ATOMIC_RELAXED);
		if (index == 0 && __atomic_load_n(&slow_blend, __ATOMIC_ACQUIRE))
		{
			struct interpolated_object before = *record;
			long tick = interpolation_tick, frame = interpolation_frame;
			real fraction = interpolation_fraction;

			__atomic_store_n(&slow_blend_held, 1, __ATOMIC_RELEASE);
			while (__atomic_load_n(&slow_blend, __ATOMIC_ACQUIRE))
				;
			if (interpolation_tick != tick || interpolation_frame != frame || interpolation_fraction != fraction ||
				record->object_index != before.object_index || record->tick != before.tick ||
				record->latest != before.latest ||
				memcmp(&record->correction, &before.correction, sizeof(before.correction)) != 0 ||
				memcmp(&record->correction_pending, &before.correction_pending, sizeof(before.correction_pending)) != 0)
			{
				__atomic_store_n(&changed_under_blend, 1, __ATOMIC_RELEASE);
			}
		}
		if (__atomic_load_n(&slow_blends, __ATOMIC_ACQUIRE))
		{
			struct timespec wait = { 0, 2 * 1000 * 1000 };

			nanosleep(&wait, NULL);
		}
	}
	record_blend_real(record);
	if (index == 0)
		__atomic_store_n(&slow_blend_held, 0, __ATOMIC_RELEASE);
}

static void *watchdog(void *parameter)
{
	struct timespec wait = { 10, 0 };

	nanosleep(&wait, NULL);
	fprintf(stderr, "the case hung\n");
	exit(1);
	return NULL;
}

static void *release_slow_blend_later(void *parameter)
{
	struct timespec wait = { 0, 300 * 1000 * 1000 };

	nanosleep(&wait, NULL);
	__atomic_store_n(&slow_blend, 0, __ATOMIC_RELEASE);
	return NULL;
}

/* every object posed apart, moving by step */
static void pose(real step)
{
	short index, node_index;

	for (index = 0; index < OBJECTS; index++)
	{
		for (node_index = 0; node_index < NODES; node_index++)
		{
			real_matrix4x3 *node = &nodes[index][node_index];
			real angle = 0.01f * index + 0.1f * node_index + step;

			node->scale = 1.0f;
			node->forward.i = cosf(angle);
			node->forward.j = sinf(angle);
			node->forward.k = 0.0f;
			node->left.i = -sinf(angle);
			node->left.j = cosf(angle);
			node->left.k = 0.0f;
			node->up.i = 0.0f;
			node->up.j = 0.0f;
			node->up.k = 1.0f;
			node->position.x = 0.5f * index + 0.1f * node_index + step;
			node->position.y = 0.05f * node_index;
			node->position.z = 0.1f;
		}
	}
}

/* what blending the object's two snapshots gives */
static void blend(real_matrix4x3 const *previous, real_matrix4x3 const *latest, real fraction, real_matrix4x3 *result)
{
	short node_index;

	for (node_index = 0; node_index < NODES; node_index++)
	{
		struct interpolation_rotation a, b;

		rotation_from_matrix(&previous[node_index], &a);
		rotation_from_matrix(&latest[node_index], &b);
		matrix_blend_rotations(&previous[node_index], &latest[node_index], &a, &b, fraction, &result[node_index]);
	}
}

static real_matrix4x3 previous_nodes[OBJECTS][NODES];

/* two ticks: the objects moving from one pose to the next */
static void two_ticks(void)
{
	pose(0.0f);
	render_interpolation_tick();
	memcpy(previous_nodes, nodes, sizeof(nodes));
	pose(0.05f);
	render_interpolation_tick();
}

static void check_drawn(long index, real fraction, const char *what)
{
	real_matrix4x3 expected[NODES];
	real_matrix4x3 const *drawn = render_interpolation_object_node_matrices(OBJECT_INDEX(index));

	CHECK(drawn != NULL, "%s: object %ld not drawn from", what, index);
	blend(previous_nodes[index], nodes[index], fraction, expected);
	CHECK(memcmp(drawn, expected, sizeof(expected)) == 0, "%s: object %ld drawn x %.9g forward %.9g, expected %.9g %.9g",
		what, index, drawn[1].position.x, drawn[1].forward.i, expected[1].position.x, expected[1].forward.i);
}

static void check_blends(long count, const char *what)
{
	long index;

	for (index = 0; index < OBJECTS; index++)
		CHECK(blends[index] == count, "%s: object %ld blended %ld times, not %ld", what, index, blends[index], count);
}

/* a worker in the middle of the first object's blend */
static void hold_slow_blend(void)
{
	__atomic_store_n(&slow_blend, 1, __ATOMIC_RELEASE);
}

static void wait_for_slow_blend_held(void)
{
	while (!__atomic_load_n(&slow_blend_held, __ATOMIC_ACQUIRE))
		;
}

/* a change to the records, made while a worker holds a blend: it must
wait for the worker */
static void check_waits_for_workers(void (*change)(void), const char *what)
{
	workers_end();
	memset(blends, 0, sizeof(blends));
	changed_under_blend = 0;
	hold_slow_blend();
	tick_fraction += 0.125f;
	render_interpolation_frame_begin();
	wait_for_slow_blend_held();
	{
		pthread_t releaser;

		pthread_create(&releaser, NULL, release_slow_blend_later, NULL);
		change();
		CHECK(!__atomic_load_n(&slow_blend_held, __ATOMIC_ACQUIRE), "%s with a worker blending", what);
		CHECK(!__atomic_load_n(&changed_under_blend, __ATOMIC_ACQUIRE), "%s under a worker's blend", what);
		pthread_join(releaser, NULL);
	}
	render_interpolation_frame_end();
}

static real step = 0.05f;

/* a tick, the objects moving on */
static void next_tick(void)
{
	memcpy(previous_nodes, nodes, sizeof(nodes));
	step += 0.05f;
	pose(step);
	render_interpolation_tick();
}

static void change_tick(void) { next_tick(); }
static void change_frame(void) { tick_fraction += 0.125f; render_interpolation_frame_begin(); }
static void change_reset(void) { render_interpolation_reset(); }
static void change_correction(void)
{
	real_vector3d offset = { 0.3f, -0.7f, 0.2f };

	render_interpolation_correct_object(OBJECT_INDEX(0), &offset);
}

int main(int argc, char **argv)
{
	const char *test = argc > 1 ? argv[1] : "";
	pthread_t watchdog_thread;
	long index;

	game_thread = pthread_self();
	pthread_create(&watchdog_thread, NULL, watchdog, NULL);
	for (index = 0; index < OBJECTS; index++)
	{
		objects[index].object.node_matrices.size = (short)sizeof(nodes[index]);
		objects[index].object.first_child_object_index = NONE;
	}
	workers_asked = WORKERS;
	two_ticks();
	if (!strcmp(test, "blends-beside-workers"))
	{
		/* each object blended once a frame, by a worker or the game's
		thread, and drawn as blended alone; the workers have a share */
		real fractions[3] = { 0.2f, 0.5f, 0.9f };
		short frame;

		for (frame = 0; frame < 3; frame++)
		{
			/* (the last tick's workers) */
			workers_end();
			memset(blends, 0, sizeof(blends));
			worker_blends = 0;
			tick_fraction = fractions[frame];
			render_interpolation_frame_begin();
			CHECK(interpolation_workers == WORKERS, "%ld workers, not %d", interpolation_workers, WORKERS);
			while (!__atomic_load_n(&worker_blends, __ATOMIC_ACQUIRE))
				;
			for (index = OBJECTS - 1; index >= 0; index--)
				check_drawn(index, fractions[frame], "drawn");
			render_interpolation_frame_end();
			workers_end();
			check_blends(1, "a frame");
			next_tick();
		}
	}
	else if (!strcmp(test, "waits-for-worker"))
	{
		/* the first object asked for while a worker blends it: its blend
		waited for, not made twice */
		pthread_t releaser;

		memset(blends, 0, sizeof(blends));
		hold_slow_blend();
		tick_fraction = 0.4f;
		render_interpolation_frame_begin();
		wait_for_slow_blend_held();
		pthread_create(&releaser, NULL, release_slow_blend_later, NULL);
		check_drawn(0, 0.4f, "held by a worker");
		CHECK(blends[0] == 1, "blended %ld times", blends[0]);
		pthread_join(releaser, NULL);
		render_interpolation_frame_end();
	}
	else if (!strcmp(test, "changes-wait-for-workers"))
	{
		/* a tick, a frame, a correction and a new map wait for the
		workers before they change what they read */
		tick_fraction = 0.25f;
		check_waits_for_workers(change_tick, "a tick");
		check_waits_for_workers(change_frame, "a frame begun");
		check_waits_for_workers(change_correction, "a correction");
		check_waits_for_workers(change_reset, "a new map");
	}
	else if (!strcmp(test, "workers-stopped"))
	{
		/* a change with the workers part of the way through the objects:
		they stop at those they have in hand, the rest left */
		real_vector3d offset = { 0.3f, -0.7f, 0.2f };

		worker_blends = 0;
		slow_blends = 1;
		tick_fraction = 0.4f;
		render_interpolation_frame_begin();
		while (!__atomic_load_n(&worker_blends, __ATOMIC_ACQUIRE))
			;
		render_interpolation_correct_object(OBJECT_INDEX(OBJECTS - 1), &offset);
		CHECK(worker_blends < OBJECTS, "the workers went on through all %d objects", OBJECTS);
		slow_blends = 0;
		render_interpolation_frame_end();
	}
	else if (!strcmp(test, "tick-restarts-workers"))
	{
		/* a tick in game_frame: the workers blend its objects for the
		renderer's frame, at the same fraction */
		int waited;

		tick_fraction = 0.3f;
		render_interpolation_frame_begin();
		for (index = 0; index < OBJECTS; index++)
			check_drawn(index, 0.3f, "game_frame");
		memset(blends, 0, sizeof(blends));
		worker_blends = 0;
		next_tick();
		for (waited = 0; waited < 2000 && !__atomic_load_n(&worker_blends, __ATOMIC_ACQUIRE); waited++)
		{
			struct timespec wait = { 0, 1000 * 1000 };

			nanosleep(&wait, NULL);
		}
		CHECK(__atomic_load_n(&worker_blends, __ATOMIC_ACQUIRE) > 0, "no worker blended after the tick");
		render_interpolation_frame_end();
		render_interpolation_frame_begin();
		for (index = OBJECTS - 1; index >= 0; index--)
			check_drawn(index, 0.3f, "the renderer");
		render_interpolation_frame_end();
		workers_end();
		check_blends(1, "after the tick");
	}
	else if (!strcmp(test, "same-frame-blends-kept"))
	{
		/* (no workers) a second frame of the same tick at the same
		fraction draws the blends made; another fraction or another tick
		blends anew */
		workers_asked = 0;
		memset(blends, 0, sizeof(blends));
		tick_fraction = 0.3f;
		render_interpolation_frame_begin();
		for (index = 0; index < OBJECTS; index++)
			check_drawn(index, 0.3f, "game_frame");
		render_interpolation_frame_end();
		render_interpolation_frame_begin();
		for (index = 0; index < OBJECTS; index++)
			check_drawn(index, 0.3f, "the renderer");
		render_interpolation_frame_end();
		check_blends(1, "two frames at one fraction");
		tick_fraction = 0.6f;
		render_interpolation_frame_begin();
		for (index = 0; index < OBJECTS; index++)
			check_drawn(index, 0.6f, "another fraction");
		render_interpolation_frame_end();
		check_blends(2, "another fraction");
		next_tick();
		render_interpolation_frame_begin();
		for (index = 0; index < OBJECTS; index++)
			check_drawn(index, 0.6f, "another tick");
		render_interpolation_frame_end();
		check_blends(3, "another tick");
	}
	else if (!strcmp(test, "changed-mid-frame"))
	{
		/* (no workers) a tick or a correction between two askings of one
		frame: blended anew */
		real_vector3d offset = { 0.3f, -0.7f, 0.2f };

		workers_asked = 0;
		tick_fraction = 0.5f;
		render_interpolation_frame_begin();
		check_drawn(0, 0.5f, "before the tick");
		next_tick();
		check_drawn(0, 0.5f, "after the tick");
		memset(blends, 0, sizeof(blends));
		render_interpolation_correct_object(OBJECT_INDEX(0), &offset);
		CHECK(render_interpolation_object_node_matrices(OBJECT_INDEX(0)) != NULL, "not drawn from after the correction");
		CHECK(blends[0] == 1, "blended %ld times after the correction", blends[0]);
		render_interpolation_frame_end();
	}
	else
	{
		fprintf(stderr, "no case %s\n", test);
		return 2;
	}
	return 0;
}
