/*
PORTAL_HULL_CACHE.C (test)

The real portal traversal (structure_visibility_find_clusters and
structure_visibility_traverse_cluster) with its hulls remembered
(portal_hull_from_portal_cached), over fake bsps: the clusters it renders, their
clip bounds and frustums and the visible clusters' flags must be those the
traversal gives when it projects every portal it reaches, and each portal's
hull must be projected at most once in each direction a traversal.
test_portal_hull_cache.py takes the code (under_test.inc) from the sources: the
traversal twice, the second as the game had it (projecting every time).
*/

#include "harness.h"

typedef unsigned short word;
typedef struct { real x, y; } real_point2d;
typedef struct { real x0, x1, y0, y1; } real_rectangle2d;
typedef struct { real alpha, red, green, blue; } real_argb_color;
struct tag_block { long count; void *address; void *definition; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) ((type *)(block)->address + (index))
#define SET_FLAG(f, b, v) ((v) ? ((f)|=(unsigned)FLAG(b)) : ((f)&=(unsigned)~FLAG(b)))
#define BIT_VECTOR_SIZE_IN_LONGS(bit_count) (((bit_count) + (LONG_BITS - 1)) >> LONG_BITS_BITS)
#define BIT_VECTOR_SIZE_IN_BYTES(bit_count) (sizeof(long) * BIT_VECTOR_SIZE_IN_LONGS(bit_count))
#define BIT_VECTOR_TEST_FLAG(bit_vector, bit) (TEST_FLAG((bit_vector)[(bit) >> LONG_BITS_BITS], ((bit) & (LONG_BITS - 1))))
#define BIT_VECTOR_SET_FLAG(bit_vector, bit, enable) (SET_FLAG((bit_vector)[(bit) >> LONG_BITS_BITS], ((bit) & (LONG_BITS - 1)), enable))
enum { LONG_BITS = 32, LONG_BITS_BITS = 5, NUMBER_OF_POINTS_PER_RECTANGLE = 4 };
#define csmemset memset
#define csmemcpy memcpy
#define error(...) ((void)0)
#define match_vassert(file, line, condition, message) ((void)0)
#define structure_visibility_render_debug_portal_hull(hull, color, offset) ((void)0)
#define ai_debug_highlight_cluster(cluster_index, color) FALSE

/* the camera is a number (view) that every hull and frustum depends on */
struct render_camera { long view; real z_far; };
struct render_frustum { real_rectangle2d bounds; long view; };

#include "context.inc"

struct structure_bsp { struct tag_block clusters; struct tag_block cluster_portals; };
static struct
{
	struct render_camera camera;
	struct render_frustum frustum;
	long cluster_index;
	boolean visible_sky_model;
	short rendered_cluster_count;
	unsigned long visible_cluster_flags[BIT_VECTOR_SIZE_IN_LONGS(MAXIMUM_CLUSTERS_PER_STRUCTURE)];
} render;
static struct structure_bsp bsp;
static struct structure_visibility_cluster clusters[MAXIMUM_CLUSTERS_PER_STRUCTURE];
static struct structure_visibility_portal portals[1024];
static short portal_lists[MAXIMUM_CLUSTERS_PER_STRUCTURE][64];
static unsigned long pvs[MAXIMUM_CLUSTERS_PER_STRUCTURE][BIT_VECTOR_SIZE_IN_LONGS(MAXIMUM_CLUSTERS_PER_STRUCTURE)];
static struct rendered_cluster rendered_clusters[MAXIMUM_RENDERED_CLUSTERS];
static boolean debug_portals;
static boolean warned_about_rendered_clusters;
static real_argb_color const *global_real_argb_red;
static real_rectangle2d const null_rectangle = { 1.0e30f, -1.0e30f, 1.0e30f, -1.0e30f };
static real_rectangle2d const *global_null_rectangle2d = &null_rectangle;
static struct structure_visibility_globals structure_visibility_globals;
static struct portal_hull_cache portal_hull_cache;

static struct structure_bsp *global_structure_bsp_get(void) { return &bsp; }
static unsigned long *structure_bsp_get_cluster_pvs(struct structure_bsp *structure, short cluster_index)
{
	return pvs[cluster_index];
}
static struct rendered_cluster *rendered_cluster_get(short rendered_cluster_index)
{
	return &rendered_clusters[rendered_cluster_index];
}
static void render_frustum_get_projection_bounds(struct render_frustum const *frustum, real_rectangle2d *bounds)
{
	*bounds = frustum->bounds;
}
static void render_camera_build_clipped_frustum_bounds(struct render_camera const *camera, real_rectangle2d const *clip_bounds,
	real_rectangle2d *bounds)
{
	*bounds = *clip_bounds;
}
static void render_camera_build_frustum(struct render_camera const *camera, real_rectangle2d const *bounds,
	struct render_frustum *frustum, boolean clip)
{
	frustum->bounds = *bounds;
	frustum->view = camera->view;
}
static boolean points_within_distance(short point_count, real_point3d const *points, real distance)
{
	return point_count % 3 != 0 || distance > 100.0f;
}

static unsigned long hash_word(unsigned long hash, unsigned long word)
{
	return (hash ^ word) * 16777619UL;
}
static unsigned long hash_real(unsigned long hash, real value)
{
	unsigned long word;

	memcpy(&word, &value, sizeof(word));
	return hash_word(hash, word);
}

/* the projection: a result and vertices hashed from the portal, the direction and the camera; counted, by portal and
direction, in the traversal that asks */
static long large_hulls;
static long projections;
static unsigned char projected[2048];
static boolean projected_twice;
static short portal_hull_from_portal(short portal_index, boolean direction, struct portal_hull *result)
{
	unsigned long hash = hash_word(hash_word(hash_word(2166136261UL, (unsigned long)portal_index), (unsigned long)direction),
		(unsigned long)render.camera.view);
	short index;
	long key = 2 * (long)portal_index + (direction ? 1 : 0);

	projections++;
	if (projected[key])
	{
		projected_twice = TRUE;
	}
	projected[key] = 1;

	result->vertex_count = 0;
	if (hash % 10 == 0)
	{
		return _portal_hull_from_portal_degenerate;
	}
	result->vertex_count = (short)((hash >> 4) % (large_hulls ? 250 : 12) + 1);
	for (index = 0; index < result->vertex_count; index++)
	{
		hash = hash_word(hash, (unsigned long)index);
		result->vertices[index].x = (real)(hash % 2001) / 1000.0f - 1.0f;
		result->vertices[index].y = (real)((hash >> 11) % 2001) / 1000.0f - 1.0f;
	}
	return result->vertex_count >= 3 && (hash >> 22) % 4 ? _portal_hull_from_portal_succeeded : _portal_hull_from_portal_discarded;
}

/* the intersection: hashed from both hulls' vertices to the bit, empty a third of the time, so that the paths end */
static short convex_hull2d_intersect(short p_count, real_point2d const *p, short q_count, real_point2d const *q,
	short maximum_count, real_point2d *result, real epsilon)
{
	unsigned long hash = 2166136261UL;
	short index, count;

	for (index = 0; index < p_count; index++)
		hash = hash_real(hash_real(hash, p[index].x), p[index].y);
	for (index = 0; index < q_count; index++)
		hash = hash_real(hash_real(hash, q[index].x), q[index].y);
	if (hash % 3 == 0)
	{
		return 0;
	}
	count = q_count < maximum_count ? q_count : maximum_count;
	for (index = 0; index < count; index++)
	{
		result[index].x = (q[index].x + p[index % p_count].x) * 0.5f;
		result[index].y = (q[index].y + p[(index + 1) % p_count].y) * 0.5f;
	}
	return count;
}

#include "under_test.inc"

static unsigned long seed;
static unsigned long next_random(unsigned long range)
{
	seed = seed * 1103515245UL + 12345UL;
	return (seed >> 8) % range;
}

/* a bsp of cluster_count clusters and portal_count portals between random clusters (a few to clusters that are not
there), each cluster seeing the others through its pvs density_percent of the time */
static void build_bsp(long cluster_count, long portal_count, unsigned long density_percent)
{
	long index, other;

	memset(clusters, 0, sizeof(clusters));
	memset(portals, 0, sizeof(portals));
	memset(pvs, 0, sizeof(pvs));
	bsp.clusters.count = cluster_count;
	bsp.clusters.address = clusters;
	bsp.cluster_portals.count = portal_count;
	bsp.cluster_portals.address = portals;
	for (index = 0; index < cluster_count; index++)
	{
		clusters[index].portal_indices.address = portal_lists[index];
		for (other = 0; other < cluster_count; other++)
			if (next_random(100) < density_percent)
				BIT_VECTOR_SET_FLAG(pvs[index], other, TRUE);
	}
	for (index = 0; index < portal_count; index++)
	{
		short a = (short)next_random((unsigned long)cluster_count);
		short b = (short)next_random((unsigned long)cluster_count);
		short side;

		if (a == b)
			b = (short)((b + 1) % cluster_count);
		portals[index].cluster_indices[0] = a;
		portals[index].cluster_indices[1] = next_random(40) ? b : (short)(cluster_count + 3);
		portals[index].vertices.count = (long)next_random(7);
		for (side = 0; side < 2; side++)
		{
			short cluster_index = portals[index].cluster_indices[side];

			if (cluster_index < cluster_count && clusters[cluster_index].portal_indices.count < 64)
				portal_lists[cluster_index][clusters[cluster_index].portal_indices.count++] = (short)index;
		}
	}
}

/* what a traversal leaves */
struct outcome
{
	short rendered_cluster_count;
	struct rendered_cluster clusters[MAXIMUM_RENDERED_CLUSTERS];
	unsigned long visible_cluster_flags[BIT_VECTOR_SIZE_IN_LONGS(MAXIMUM_CLUSTERS_PER_STRUCTURE)];
	long projections;
};

static void traverse(boolean cached, long view, short from_cluster, struct outcome *outcome)
{
	render.camera.view = view;
	render.camera.z_far = (real)(view % 200);
	render.visible_sky_model = view % 2 == 0;
	render.frustum.bounds.x0 = -1.0f;
	render.frustum.bounds.x1 = 1.0f;
	render.frustum.bounds.y0 = -0.75f;
	render.frustum.bounds.y1 = 0.75f;
	render.frustum.view = view;
	render.cluster_index = from_cluster;
	memset(render.visible_cluster_flags, 0, sizeof(render.visible_cluster_flags));
	render.rendered_cluster_count = 0;
	memset(rendered_clusters, 0, sizeof(rendered_clusters));
	memset(projected, 0, sizeof(projected));
	projected_twice = FALSE;
	projections = 0;
	if (cached)
		structure_visibility_find_clusters();
	else
		structure_visibility_find_clusters_reference();
	memset(outcome, 0, sizeof(*outcome));
	outcome->rendered_cluster_count = render.rendered_cluster_count;
	memcpy(outcome->clusters, rendered_clusters, sizeof(rendered_clusters));
	memcpy(outcome->visible_cluster_flags, render.visible_cluster_flags, sizeof(render.visible_cluster_flags));
	outcome->projections = projections;
}

static boolean same_outcome(struct outcome const *a, struct outcome const *b)
{
	return a->rendered_cluster_count == b->rendered_cluster_count &&
		!memcmp(a->clusters, b->clusters, sizeof(a->clusters)) &&
		!memcmp(a->visible_cluster_flags, b->visible_cluster_flags, sizeof(a->visible_cluster_flags));
}

/* traversals of many bsps from several clusters and cameras, each cached one checked against the game's; the cached
ones run one after another, so that one camera's hulls are there when the next traverses */
static void check_bsps(long bsp_count, long cluster_count, long portal_count, unsigned long density_percent,
	boolean all_remembered, long *cached_projections, long *uncached_projections)
{
	static struct outcome cached, uncached;
	long bsp_index, traversal;

	for (bsp_index = 0; bsp_index < bsp_count; bsp_index++)
	{
		build_bsp(cluster_count, portal_count, density_percent);
		for (traversal = 0; traversal < 4; traversal++)
		{
			long view = (long)next_random(100000);
			short from_cluster = (short)next_random((unsigned long)cluster_count);

			traverse(FALSE, view, from_cluster, &uncached);
			traverse(TRUE, view, from_cluster, &cached);
			CHECK(same_outcome(&cached, &uncached), "bsp %ld traversal %ld (camera %ld from cluster %d): %d clusters rendered,"
				" the game's traversal renders %d", bsp_index, traversal, view, from_cluster, cached.rendered_cluster_count,
				uncached.rendered_cluster_count);
			CHECK(!all_remembered || !projected_twice, "bsp %ld traversal %ld projected a portal twice in one direction", bsp_index, traversal);
			*cached_projections += cached.projections;
			*uncached_projections += uncached.projections;
		}
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long cached_projections = 0, uncached_projections = 0;

	seed = 12345;

	CASE("same-as-uncached")
	{
		check_bsps(300, 14, 30, 85, TRUE, &cached_projections, &uncached_projections);
		CHECK(cached_projections * 2 < uncached_projections, "%ld projections, the game's traversal makes %ld:"
			" the bsps do not reach portals again", cached_projections, uncached_projections);
		return 0;
	}
	CASE("camera-moves")
	{
		static struct outcome first, cached, uncached;
		long bsp_index;

		for (bsp_index = 0; bsp_index < 200; bsp_index++)
		{
			build_bsp(12, 26, 90);
			traverse(TRUE, 7, 0, &first);
			traverse(TRUE, 8, 0, &cached);
			traverse(FALSE, 8, 0, &uncached);
			CHECK(same_outcome(&cached, &uncached), "bsp %ld: the second camera's traversal renders %d clusters, the game's %d",
				bsp_index, cached.rendered_cluster_count, uncached.rendered_cluster_count);
		}
		return 0;
	}
	CASE("many-portals")
	{
		/* portals past the 512 whose hulls are remembered are projected each time */
		check_bsps(60, 40, 1000, 12, FALSE, &cached_projections, &uncached_projections);
		return 0;
	}
	CASE("large-hulls")
	{
		/* hulls of up to 250 vertices: past the remembered vertices' room, hulls are projected each time */
		large_hulls = TRUE;
		check_bsps(200, 18, 60, 80, FALSE, &cached_projections, &uncached_projections);
		return 0;
	}
	CASE("numbering-wraps")
	{
		static struct outcome cached, uncached;
		long bsp_index, entry_index;

		for (bsp_index = 0; bsp_index < 100; bsp_index++)
		{
			unsigned long traversal;

			build_bsp(12, 26, 90);
			traverse(TRUE, 11, 0, &cached);
			/* the first camera's hulls, numbered as the first traversal after the numbering wraps is */
			traversal = portal_hull_cache.traversal;
			for (entry_index = 0; entry_index < MAXIMUM_PORTAL_HULL_CACHE_ENTRIES; entry_index++)
				if (portal_hull_cache.entries[entry_index].traversal == traversal)
					portal_hull_cache.entries[entry_index].traversal = 1;
			portal_hull_cache.traversal = (unsigned long)-1;
			traverse(TRUE, 12, 0, &cached);
			traverse(FALSE, 12, 0, &uncached);
			CHECK(same_outcome(&cached, &uncached), "bsp %ld: after the numbering wraps, %d clusters rendered, the game's %d",
				bsp_index, cached.rendered_cluster_count, uncached.rendered_cluster_count);
		}
		return 0;
	}

	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
