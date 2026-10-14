/*
SPHERE_MEMO.C (test)

Collision's real sphere test against a bsp (collision_bsp_test_sphere) with its
memo (collision_sphere_memo.c), over a fake recursion whose answer depends on
everything the question holds: the memo must give the answer the recursion
gives, and ask the recursion only for a question it has not seen.
test_sphere_memo.py takes the code (under_test.inc) from the sources.
*/

#include "harness.h"

typedef struct { real x, y; } real_point2d;
#define MAXIMUM_BSP3D_DEPTH 64
struct collision_bsp { struct { struct { long count; } nodes; } bsp3d; };
struct collision_bsp_test_sphere_result
{
	long surface_count;
	long surface_indices[256];
	long edge_count;
	long edge_indices[256];
	long vertex_count;
	long vertex_indices[256];
	long leaf_count;
	long leaf_indices[256];
};
static struct collision_bsp const *global_collision_bsp;
static struct { long sphere; } collision_bsp_usage_times;
#define collision_log_usage(...) ((void)0)
#define collision_log_start_time(...) ((void)0)
#define collision_log_end_time(...) ((void)0)

#include "context.inc"

/* the recursion: indices hashed from the bsp, the flags' content, the center and radius to the bit; small answers mostly,
some too large to be remembered */
static long recursions;
static unsigned long answer_hash(struct test_sphere_data const *data)
{
	unsigned long hash = 2166136261UL, word;
	int index;

	hash = (hash ^ (unsigned long)data->bsp) * 16777619UL;
	for (index = 0; index < (data->breakable_surface_count + 7) / 8; index++)
		hash = (hash ^ data->breakable_surface_flags[index]) * 16777619UL;
	hash = (hash ^ (unsigned long)data->breakable_surface_count) * 16777619UL;
	memcpy(&word, &data->center->x, sizeof(word)); hash = (hash ^ word) * 16777619UL;
	memcpy(&word, &data->center->y, sizeof(word)); hash = (hash ^ word) * 16777619UL;
	memcpy(&word, &data->center->z, sizeof(word)); hash = (hash ^ word) * 16777619UL;
	memcpy(&word, &data->radius, sizeof(word)); hash = (hash ^ word) * 16777619UL;
	return hash;
}
static void bsp3d_test_sphere_recursive(struct test_sphere_data *data, long node_index, short depth)
{
	struct collision_bsp_test_sphere_result *result = data->result;
	unsigned long hash = answer_hash(data);
	long scale = hash % 17 ? 1 : 12, index;

	recursions++;
	result->surface_count = (long)(hash % 7) * scale;
	result->edge_count = (long)(hash >> 3) % 5 * scale;
	result->vertex_count = (long)(hash >> 6) % 4 * scale;
	result->leaf_count = 1 + (long)(hash >> 9) % 3;
	for (index = 0; index < 256; index++)
	{
		result->surface_indices[index] = (long)(hash ^ (unsigned long)index * 3U);
		result->edge_indices[index] = (long)(hash ^ (unsigned long)index * 5U);
		result->vertex_indices[index] = (long)(hash ^ (unsigned long)index * 7U);
		result->leaf_indices[index] = (long)(hash ^ (unsigned long)index * 11U);
	}
}

#include "under_test.inc"

static struct collision_bsp bsps[2] = { { { { 10 } } }, { { { 10 } } } };
static byte flags[2][32];

/* whether two answers hold the same indices (only the counted ones mean anything) */
static boolean same_answer(struct collision_bsp_test_sphere_result const *a, struct collision_bsp_test_sphere_result const *b)
{
	return a->surface_count == b->surface_count && a->edge_count == b->edge_count && a->vertex_count == b->vertex_count &&
		a->leaf_count == b->leaf_count &&
		!memcmp(a->surface_indices, b->surface_indices, a->surface_count * sizeof(long)) &&
		!memcmp(a->edge_indices, b->edge_indices, a->edge_count * sizeof(long)) &&
		!memcmp(a->vertex_indices, b->vertex_indices, a->vertex_count * sizeof(long)) &&
		!memcmp(a->leaf_indices, b->leaf_indices, a->leaf_count * sizeof(long));
}

/* the question's answer through the memo, checked against the recursion's own; whether the recursion was asked */
static struct collision_bsp_test_sphere_result with, without;
static boolean ask(struct collision_bsp const *bsp, short count, byte const *breakable, real x, real y, real z, real radius)
{
	real_point3d center;
	struct test_sphere_data data;
	long before = recursions;
	boolean recursed;

	center.x = x; center.y = y; center.z = z;
	memset(&with, 0xCD, sizeof(with));
	collision_bsp_test_sphere(bsp, count, breakable, &center, radius, &with);
	recursed = recursions != before;
	memset(&data, 0, sizeof(data));
	data.bsp = bsp;
	data.breakable_surface_count = count;
	data.breakable_surface_flags = breakable;
	data.center = &center;
	data.radius = radius;
	data.result = &without;
	bsp3d_test_sphere_recursive(&data, 0, 0);
	recursions--;
	CHECK(same_answer(&with, &without), "a different answer for (%g %g %g) r %g on bsp %d with %d flags", x, y, z, radius,
		(int)(bsp - bsps), count);
	return recursed;
}

static unsigned long seed = 12345;
static real random_real(real low, real high)
{
	seed = seed * 1103515245UL + 12345UL;
	return low + (high - low) * (real)((seed >> 8) & 0xFFFF) / 65535.0f;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	global_collision_bsp = &bsps[0];
	CASE("repeated")
	{
		CHECK(ask(&bsps[0], 0, NULL, 1.0f, 2.0f, 3.0f, 0.5f), "the first question was not put to the recursion");
		CHECK(!ask(&bsps[0], 0, NULL, 1.0f, 2.0f, 3.0f, 0.5f), "a repeated question was put to the recursion");
		flags[0][0] = 0x5A;
		CHECK(ask(&bsps[0], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5f), "the first question with flags was not put to the recursion");
		CHECK(!ask(&bsps[0], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5f), "a repeated question with flags was put to the recursion");
		return 0;
	}
	/* a question that differs in any part, to the bit, is a new one */
	CASE("different")
	{
		ask(&bsps[0], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5f);
		CHECK(ask(&bsps[0], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5000001f), "a different radius was answered from the memo");
		CHECK(ask(&bsps[0], 12, flags[0], 1.0f, 2.0000002f, 3.0f, 0.5f), "a different center was answered from the memo");
		CHECK(ask(&bsps[1], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5f), "another bsp was answered from the memo");
		CHECK(ask(&bsps[0], 11, flags[0], 1.0f, 2.0f, 3.0f, 0.5f), "another flag count was answered from the memo");
		CHECK(ask(&bsps[0], 12, flags[1], 1.0f, 2.0f, 3.0f, 0.5f), "other flags were answered from the memo");
		ask(&bsps[0], 0, NULL, 0.0f, 2.0f, 3.0f, 0.5f);
		CHECK(ask(&bsps[0], 0, NULL, -0.0f, 2.0f, 3.0f, 0.5f), "-0 was answered as +0 from the memo");
		return 0;
	}
	/* a surface broken: the flags' content changes at the same address */
	CASE("flags-change")
	{
		flags[0][0] = 0x5A;
		ask(&bsps[0], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5f);
		flags[0][1] = 0x08;
		CHECK(ask(&bsps[0], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5f), "changed flags were answered from the memo");
		CHECK(!ask(&bsps[0], 12, flags[0], 1.0f, 2.0f, 3.0f, 0.5f), "a repeated question was put to the recursion");
		return 0;
	}
	CASE("invalidated")
	{
		ask(&bsps[0], 0, NULL, 1.0f, 2.0f, 3.0f, 0.5f);
		collision_sphere_memo_invalidate();
		CHECK(ask(&bsps[0], 0, NULL, 1.0f, 2.0f, 3.0f, 0.5f), "a question was answered from the memo after it was invalidated");
		return 0;
	}
	/* many questions, most of them asked before, over two bsps and flags that change now and then: every answer the
	recursion's own */
	CASE("crowd")
	{
		static real points[500][4];
		int index, repeats = 0;

		for (index = 0; index < 500; index++)
		{
			points[index][0] = random_real(-50.0f, 50.0f);
			points[index][1] = random_real(-50.0f, 50.0f);
			points[index][2] = random_real(0.0f, 5.0f);
			points[index][3] = random_real(0.1f, 3.0f);
		}
		for (index = 0; index < 20000; index++)
		{
			real const *point = points[index % 4 ? index % 500 : (index * 7) % 500];

			if (index % 997 == 0)
				flags[0][index % 32] ^= (byte)(1 << (index % 8));
			repeats += !ask(&bsps[index % 3 == 0], index % 5 ? 256 : 0, flags[0], point[0], point[1], point[2], point[3]);
		}
		CHECK(repeats > 1000, "only %d questions answered from the memo: the case does not exercise it", repeats);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
