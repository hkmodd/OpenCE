/*
COLLISION_SPHERE_MEMO.C

The answers of collision's sphere test against a bsp (collision_bsp.c),
remembered by their question: the bsp, the breakable surfaces' flags, the
sphere's center and radius to the bit. A biped that stands still asks the
same question every tick, and over a crowded level a quarter of the questions
were asked before. A bsp's data does not change while it is loaded, and the
answers are forgotten as one is unloaded (cache_files.c) or the game state
replaced (game_state.c); the flags change (a surface broken), so an answer
counts only for the flags it was given. The same answer as the test's: what
the caller finds does not change. (Kept out of the game state, whose layout
the saved games share.)
*/

#include "cseries.h"
#include "math/real_math.h"
#include "physics/collision_bsp.h"

enum
{
	SPHERE_MEMO_ENTRIES = 2048,
	/* the indices an answer may hold to be remembered */
	SPHERE_MEMO_INDICES = 60,
	SPHERE_MEMO_FLAGS_BYTES = 256 / 8,
};

struct sphere_memo_entry
{
	long generation;
	void const *bsp;
	byte const *breakable_surface_flags;
	short breakable_surface_count;
	short surface_count;
	short edge_count;
	short vertex_count;
	short leaf_count;
	short pad;
	unsigned long sphere[4];
	long indices[SPHERE_MEMO_INDICES];
};

static struct sphere_memo_entry sphere_memo[SPHERE_MEMO_ENTRIES];
/* what an answer is remembered for: the flags as they were (snapshot) and
since the game state was last replaced; zero, which no entry holds, is never
current */
static long sphere_memo_generation = 1;
static byte const *sphere_memo_flags_address;
static short sphere_memo_flags_count = NONE;
static byte sphere_memo_flags[SPHERE_MEMO_FLAGS_BYTES];

void collision_sphere_memo_invalidate(
	void)
{
	sphere_memo_generation++;
	sphere_memo_flags_address = NULL;
	sphere_memo_flags_count = NONE;
}

/* the entry for the question, and whether it holds the answer; NULL when the
question cannot be remembered */
static struct sphere_memo_entry *sphere_memo_entry_get(
	void const *bsp,
	short breakable_surface_count,
	byte const *breakable_surface_flags,
	real_point3d const *center,
	real radius,
	unsigned long sphere[4],
	boolean *found)
{
	struct sphere_memo_entry *entry;
	unsigned long hash = 2166136261u;
	short index;

	if (breakable_surface_count < 0 || breakable_surface_count > SPHERE_MEMO_FLAGS_BYTES * 8 ||
		(breakable_surface_count > 0 && !breakable_surface_flags))
	{
		return NULL;
	}
	if (breakable_surface_count > 0)
	{
		short flags_bytes = (short)((breakable_surface_count + 7) / 8);

		if (breakable_surface_flags != sphere_memo_flags_address ||
			breakable_surface_count != sphere_memo_flags_count ||
			memcmp(breakable_surface_flags, sphere_memo_flags, flags_bytes))
		{
			sphere_memo_generation++;
			sphere_memo_flags_address = breakable_surface_flags;
			sphere_memo_flags_count = breakable_surface_count;
			memcpy(sphere_memo_flags, breakable_surface_flags, flags_bytes);
		}
	}

	memcpy(&sphere[0], &center->x, sizeof(real));
	memcpy(&sphere[1], &center->y, sizeof(real));
	memcpy(&sphere[2], &center->z, sizeof(real));
	memcpy(&sphere[3], &radius, sizeof(real));
	hash = (hash ^ (unsigned long)bsp) * 16777619u;
	for (index = 0; index < 4; index++)
	{
		hash = (hash ^ sphere[index]) * 16777619u;
	}
	entry = &sphere_memo[(hash ^ (hash >> 16)) % SPHERE_MEMO_ENTRIES];
	*found = entry->generation == sphere_memo_generation &&
		entry->bsp == bsp &&
		entry->breakable_surface_flags == breakable_surface_flags &&
		entry->breakable_surface_count == breakable_surface_count &&
		!memcmp(entry->sphere, sphere, sizeof(entry->sphere));

	return entry;
}

static void sphere_memo_copy(
	long *to,
	long const *from,
	long count)
{
	memcpy(to, from, count * sizeof(long));
}

/* whether the question was asked before (the answer is then in result) */
boolean collision_sphere_memo_find(
	void const *bsp,
	short breakable_surface_count,
	byte const *breakable_surface_flags,
	real_point3d const *center,
	real radius,
	struct collision_bsp_test_sphere_result *result)
{
	unsigned long sphere[4];
	boolean found;
	struct sphere_memo_entry const *entry = sphere_memo_entry_get(
		bsp, breakable_surface_count, breakable_surface_flags, center, radius, sphere, &found);
	long const *indices;

	if (!entry || !found)
	{
		return FALSE;
	}
	indices = entry->indices;
	result->surface_count = entry->surface_count;
	sphere_memo_copy(result->surface_indices, indices, result->surface_count);
	indices += result->surface_count;
	result->edge_count = entry->edge_count;
	sphere_memo_copy(result->edge_indices, indices, result->edge_count);
	indices += result->edge_count;
	result->vertex_count = entry->vertex_count;
	sphere_memo_copy(result->vertex_indices, indices, result->vertex_count);
	indices += result->vertex_count;
	result->leaf_count = entry->leaf_count;
	sphere_memo_copy(result->leaf_indices, indices, result->leaf_count);

	return TRUE;
}

/* remembers the test's answer to the question, if it is small enough */
void collision_sphere_memo_store(
	void const *bsp,
	short breakable_surface_count,
	byte const *breakable_surface_flags,
	real_point3d const *center,
	real radius,
	struct collision_bsp_test_sphere_result const *result)
{
	unsigned long sphere[4];
	boolean found;
	struct sphere_memo_entry *entry = sphere_memo_entry_get(
		bsp, breakable_surface_count, breakable_surface_flags, center, radius, sphere, &found);
	long *indices;

	if (!entry ||
		result->surface_count < 0 || result->edge_count < 0 || result->vertex_count < 0 || result->leaf_count < 0 ||
		result->surface_count + result->edge_count + result->vertex_count + result->leaf_count > SPHERE_MEMO_INDICES)
	{
		return;
	}
	entry->generation = sphere_memo_generation;
	entry->bsp = bsp;
	entry->breakable_surface_flags = breakable_surface_flags;
	entry->breakable_surface_count = breakable_surface_count;
	memcpy(entry->sphere, sphere, sizeof(entry->sphere));
	indices = entry->indices;
	entry->surface_count = (short)result->surface_count;
	sphere_memo_copy(indices, result->surface_indices, result->surface_count);
	indices += result->surface_count;
	entry->edge_count = (short)result->edge_count;
	sphere_memo_copy(indices, result->edge_indices, result->edge_count);
	indices += result->edge_count;
	entry->vertex_count = (short)result->vertex_count;
	sphere_memo_copy(indices, result->vertex_indices, result->vertex_count);
	indices += result->vertex_count;
	entry->leaf_count = (short)result->leaf_count;
	sphere_memo_copy(indices, result->leaf_indices, result->leaf_count);
}
