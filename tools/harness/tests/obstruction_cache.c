/*
OBSTRUCTION_CACHE.C (test)

The sound manager's obstruction (game_sound.c, compute_sound_obstruction) for a
battle's sounds over a fake world, frame after frame: within a tick each sound
must be tested from the camera once, and every result must be what the game
computes without the cache. test_obstruction_cache.py takes the enums
(enums.inc) and the code (under_test.inc) from the sources.
*/

#include "harness.h"
#include "enums.inc"

/* the sound and the camera: the members the code reads */
enum { _sound_spatialization_mode_absolute };
struct sound_source
{
	struct
	{
		real_point3d position;
		struct { short cluster_index; } game_location;
	} location;
	short spatialization_mode;
	real obstruction, occlusion;
};
struct observer_result
{
	real_point3d position;
	struct { short cluster_index; } location;
};
enum { LOCAL_PLAYERS = 2 };
static struct observer_result cameras[LOCAL_PLAYERS];
#define observer_get_camera(local_player_index) (&cameras[local_player_index])
static long now_time;
#define game_time_get() now_time

/* the world: sixteen clusters, all in each other's sight, each pair its own sound distance */
enum { CLUSTERS = 16 };
struct structure_bsp;
static unsigned long cluster_pvs[1] = { 0xFFFFFFFFUL };
#define global_structure_bsp_get() ((struct structure_bsp *)NULL)
#define structure_bsp_get_cluster_encoded_sound_distance(bsp, from, to) ((byte)(1 + ((from) * 5 + (to) * 3) % 60))
#define structure_bsp_get_cluster_pvs(bsp, cluster) cluster_pvs

/* the collision test from the camera: counted, and blocked or not by where it starts and goes */
struct collision_result { long unused; };
static unsigned long rays;
static boolean ray_blocked(real_point3d const *point, real_vector3d const *vector)
{
	unsigned long words[6];
	unsigned long hash = 2166136261UL;
	int index;

	memcpy(&words[0], point, sizeof(*point));
	memcpy(&words[3], vector, sizeof(*vector));
	for (index = 0; index < 6; index++)
		hash = (hash ^ words[index]) * 16777619UL;
	return (hash >> 7) % 4 != 0;
}
#define collision_test_vector(flags, point, vector, ignore, result) (rays++, ray_blocked(point, vector))
short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];
short global_current_collision_user_depth = 1;

#include "under_test.inc"

static unsigned long seed = 12345;
static real random_real(real low, real high)
{
	seed = seed * 1103515245UL + 12345UL;
	return low + (high - low) * (real)((seed >> 8) & 0xFFFF) / 65535.0f;
}

/* as many sounds as the sound manager has (sound_data, 0x200), about a map */
enum { SOURCES = 0x200, BATTLE = 64, COLLIDING = 12 };
static struct sound_source sources[SOURCES];
static real distances[SOURCES];
static void place(struct sound_source *source)
{
	source->location.position.x = random_real(-60.0f, 60.0f);
	source->location.position.y = random_real(-60.0f, 60.0f);
	source->location.position.z = random_real(-5.0f, 15.0f);
	source->location.game_location.cluster_index = (short)(seed % CLUSTERS);
	source->spatialization_mode = _sound_spatialization_mode_absolute;
}
static void build_world(void)
{
	int index;

	for (index = 0; index < SOURCES; index++)
	{
		place(&sources[index]);
		distances[index] = random_real(1.0f, 40.0f);
	}
	for (index = 0; index < LOCAL_PLAYERS; index++)
	{
		cameras[index].position.x = random_real(-20.0f, 20.0f);
		cameras[index].position.y = random_real(-20.0f, 20.0f);
		cameras[index].position.z = random_real(0.0f, 3.0f);
		cameras[index].location.cluster_index = (short)index;
	}
	now_time = 100;
}

/* a frame's refresh of the first count sounds, for a local player: the rays it cast */
static unsigned long frame(int count, short local_player_index)
{
	unsigned long before = rays;
	int index;

	for (index = 0; index < count; index++)
		compute_sound_obstruction(local_player_index, &sources[index], distances[index]);
	return rays - before;
}

/* the cameras move between two frames of a tick, as render interpolation moves them */
static void move_cameras(void)
{
	int index;

	for (index = 0; index < LOCAL_PLAYERS; index++)
	{
		cameras[index].position.x += 0.05f;
		cameras[index].position.y -= 0.03f;
	}
}

/* what the game computes for a sound without the cache: the cache emptied around the call and put back */
static unsigned char saved_cache[sizeof(obstruction_cache)];
static struct sound_source computed(short local_player_index, int index)
{
	struct sound_source source = sources[index];
	unsigned long saved_rays = rays;

	memcpy(saved_cache, obstruction_cache, sizeof(obstruction_cache));
	memset(obstruction_cache, 0, sizeof(obstruction_cache));
	compute_sound_obstruction(local_player_index, &source, distances[index]);
	memcpy(obstruction_cache, saved_cache, sizeof(obstruction_cache));
	rays = saved_rays;
	return source;
}

/* the first count sounds' results are what the game computes without the cache, for a local player */
static int wrong_results(int count, short local_player_index)
{
	int index, wrong = 0;

	for (index = 0; index < count; index++)
	{
		struct sound_source source = computed(local_player_index, index);

		wrong += source.obstruction != sources[index].obstruction || source.occlusion != sources[index].occlusion;
	}
	return wrong;
}

/* the results the first count sounds have now, kept and compared */
static struct { real obstruction, occlusion; } kept[SOURCES];
static void keep(int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		kept[index].obstruction = sources[index].obstruction;
		kept[index].occlusion = sources[index].occlusion;
	}
}
static int changed(int count)
{
	int index, different = 0;

	for (index = 0; index < count; index++)
		different += kept[index].obstruction != sources[index].obstruction || kept[index].occlusion != sources[index].occlusion;
	return different;
}

/* sounds that all start their search in one slot, a few metres apart in height: the first count sounds */
static void collide(int count)
{
	long home = obstruction_cache_slot(0, &sources[0].location.position);
	int found = 1;

	while (found < count)
	{
		sources[found] = sources[0];
		sources[found].location.position.z = random_real(-5.0f, 15.0f);
		if (obstruction_cache_slot(0, &sources[found].location.position) == home)
			distances[found] = random_real(1.0f, 40.0f), found++;
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	unsigned long first, again;
	int extra;

	build_world();
	/* a battle's sounds are tested in a tick's first frame, then reused as they were by its other frames */
	CASE("once-per-tick")
	{
		first = frame(BATTLE, 0);
		CHECK(first == BATTLE, "%lu rays for %d sounds in the tick's first frame", first, BATTLE);
		CHECK(!wrong_results(BATTLE, 0), "%d sounds differ from what the game computes", wrong_results(BATTLE, 0));
		keep(BATTLE);
		for (extra = 0; extra < 3; extra++)
		{
			move_cameras();
			again = frame(BATTLE, 0);
			CHECK(again == 0, "%lu of %d sounds tested again in frame %d of the tick", again, BATTLE, extra + 2);
			CHECK(!changed(BATTLE), "%d sounds changed within the tick", changed(BATTLE));
		}
		return 0;
	}
	/* every sound the manager can have: a few may be pushed out (two of
	the 512 here), never more than one in a hundred */
	CASE("all-sounds")
	{
		first = frame(SOURCES, 0);
		CHECK(first == SOURCES, "%lu rays for %d sounds in the tick's first frame", first, SOURCES);
		move_cameras();
		again = frame(SOURCES, 0);
		CHECK(again * 100 <= SOURCES, "%lu of %d sounds tested again in the tick's second frame", again, SOURCES);
		return 0;
	}
	/* the next tick tests every sound again, from where the camera is then */
	CASE("next-tick")
	{
		frame(BATTLE, 0);
		move_cameras();
		now_time++;
		again = frame(BATTLE, 0);
		CHECK(again == BATTLE, "%lu of %d sounds tested in the next tick", again, BATTLE);
		CHECK(!wrong_results(BATTLE, 0), "%d sounds differ from what the game computes", wrong_results(BATTLE, 0));
		return 0;
	}
	/* two local players hear the same sounds, each from its own camera */
	CASE("players")
	{
		int wrong;

		first = frame(BATTLE, 0);
		first += frame(BATTLE, 1);
		CHECK(first == 2 * BATTLE, "%lu rays for %d sounds and two players", first, BATTLE);
		wrong = wrong_results(BATTLE, 1);
		CHECK(!wrong, "%d sounds of the second player differ from what the game computes", wrong);
		again = frame(BATTLE, 0) + frame(BATTLE, 1);
		CHECK(again == 0, "%lu tested again in the tick's second frame", again);
		frame(BATTLE, 0);
		wrong = wrong_results(BATTLE, 0);
		CHECK(!wrong, "%d sounds of the first player differ from what the game computes", wrong);
		return 0;
	}
	/* more sounds wanting one slot than the search reaches: the results stay right, the others are kept */
	CASE("crowded-slot")
	{
		int index, differ = 0;

		collide(COLLIDING);
		for (index = 1; index < COLLIDING; index++)
		{
			struct sound_source a = computed(0, 0), b = computed(0, index);

			differ += a.obstruction != b.obstruction || a.occlusion != b.occlusion;
		}
		CHECK(differ >= COLLIDING / 2, "only %d of the colliding sounds differ from the first: the case sees nothing", differ);
		frame(COLLIDING, 0);
		CHECK(!wrong_results(COLLIDING, 0), "%d sounds differ from what the game computes", wrong_results(COLLIDING, 0));
		again = frame(COLLIDING, 0);
		CHECK(!wrong_results(COLLIDING, 0), "%d sounds differ from what the game computes", wrong_results(COLLIDING, 0));
		CHECK(again <= COLLIDING - 7, "%lu of %d sounds wanting one slot tested again", again, COLLIDING);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
