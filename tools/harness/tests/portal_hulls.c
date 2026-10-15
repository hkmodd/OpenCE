/*
PORTAL_HULLS.C (test)

The clusters the camera sees (structure_visibility.c,
structure_visibility_find_clusters), traversed through the portals of a fake
structure, frame after frame as the camera moves. Each portal's hull must be
made at most once a frame from each side, and what the traversal finds must
be what it finds making every hull each time it reaches the portal (the same
code with the memo off, compared by test_portal_hulls.py).
test_portal_hulls.py takes the enums (enums.inc) and the code (under_test.inc)
from the sources.
*/

#include <math.h>

#include "harness.h"
#include "enums.inc"

/* the game's types the code reads */
typedef unsigned short word;
typedef union { real n[2]; struct { real x, y; }; } real_point2d;
typedef union { real n[4]; struct { real x0, x1, y0, y1; }; } real_rectangle2d;
typedef struct { real_vector3d n; real d; } real_plane3d;
typedef struct { real alpha, red, green, blue; } real_argb_color;
struct tag_block { long count; void *address; void *definition; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) ((type *)(block)->address + (index))
#define csmemset memset
#define csmemcpy memcpy
#define error(priority, ...) ((void)0)
#define match_vassert(file, line, condition, message) ((void)0)
static real_rectangle2d null_rectangle = { { 1e30f, -1e30f, 1e30f, -1e30f } };
static real_rectangle2d const *global_null_rectangle2d = &null_rectangle;

/* the camera: a basis (view space looks down -z) and the bounds a cluster is seen in */
typedef struct { real_vector3d right, up, back; real_point3d position; } real_matrix4x3;
struct render_camera { real_point3d position; real_vector3d forward; real z_far; boolean mirrored; };
struct render_frustum { real_matrix4x3 world_to_view; real_rectangle2d bounds; };
struct rendered_cluster { short cluster_index; real_rectangle2d clip_bounds; struct render_frustum frustum; };
static struct
{
	short cluster_index;
	unsigned long visible_cluster_flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
	short rendered_cluster_count;
	struct rendered_cluster rendered_clusters[MAXIMUM_RENDERED_CLUSTERS];
	struct render_camera camera;
	struct render_frustum frustum;
	boolean visible_sky_model;
} render;
#define rendered_cluster_get(index) (&render.rendered_clusters[index])
static void render_frustum_get_projection_bounds(struct render_frustum const *frustum, real_rectangle2d *bounds)
{
	bounds->x0 = -1.0f; bounds->x1 = 1.0f; bounds->y0 = -0.75f; bounds->y1 = 0.75f;
	(void)frustum;
}
#define render_camera_build_clipped_frustum_bounds(camera, clip_bounds, frustum_bounds) (*(frustum_bounds) = *(clip_bounds))
#define render_camera_build_frustum(camera, frustum_bounds, frustum, projection) ((frustum)->bounds = *(frustum_bounds))

/* the debugging the traversal offers: off, but each cluster it reaches is counted */
static boolean debug_portals;
static real_argb_color const *global_real_argb_red;
static long visits;
#define ai_debug_highlight_cluster(cluster_index, color) (visits++, FALSE)
#define structure_visibility_render_debug_portal_hull(hull, color, offset) ((void)0)
static boolean warned_about_portal_hull_vertices, warned_about_rendered_clusters;

/* the structure: clusters, the portals between them, each on a plane of its own */
enum { MAXIMUM_PORTALS = 0x300, PORTAL_VERTICES = 4 };
struct structure_visibility_cluster { struct tag_block portal_indices; };
struct structure_visibility_portal { short plane_index; short cluster_indices[2]; struct tag_block vertices; };
struct collision_bsp { struct { struct tag_block planes; } bsp3d; };
struct structure_bsp { struct tag_block clusters, cluster_portals, collision_bsp; };
static struct structure_bsp structure;
static struct structure_bsp *fake_structure_bsp(void) { return &structure; }
static struct structure_visibility_cluster clusters[MAXIMUM_CLUSTERS_PER_STRUCTURE];
static short portal_lists[MAXIMUM_CLUSTERS_PER_STRUCTURE][MAXIMUM_PORTALS];
static struct structure_visibility_portal portals[MAXIMUM_PORTALS];
static real_point3d portal_vertices[MAXIMUM_PORTALS][PORTAL_VERTICES];
static real_plane3d planes[MAXIMUM_PORTALS];
static struct collision_bsp collision_bsp;
static unsigned long pvs[MAXIMUM_CLUSTERS_PER_STRUCTURE][MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
#define global_structure_bsp_get() fake_structure_bsp()
#define structure_bsp_get_cluster_pvs(bsp, cluster_index) (pvs[cluster_index])

/* the hulls made: each test of the camera against a portal's plane, by portal */
static long hulls_made[MAXIMUM_PORTALS];

static real dot_product3d(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}

static void vector_from_points3d(real_point3d const *from, real_point3d const *to, real_vector3d *vector)
{
	vector->i = to->x - from->x;
	vector->j = to->y - from->y;
	vector->k = to->z - from->z;
}

static real plane3d_distance_to_point(real_plane3d const *plane, real_point3d const *point)
{
	if (point == &render.camera.position && plane >= planes && plane < planes + MAXIMUM_PORTALS)
	{
		hulls_made[plane - planes]++;
	}
	return plane->n.i * point->x + plane->n.j * point->y + plane->n.k * point->z - plane->d;
}

static void matrix4x3_transform_point(real_matrix4x3 const *matrix, real_point3d const *point, real_point3d *result)
{
	real_vector3d offset;

	vector_from_points3d(&matrix->position, point, &offset);
	result->x = dot_product3d(&matrix->right, &offset);
	result->y = dot_product3d(&matrix->up, &offset);
	result->z = dot_product3d(&matrix->back, &offset);
}

/* a convex polygon clipped to the front of a plane (as geometry.c's) */
static short convex_polygon3d_clip_to_plane(short count, real_point3d const *points, real_plane3d const *plane,
	short maximum_count, real_point3d *result, boolean *clipped, real epsilon, boolean keep_coplanar)
{
	real_point3d storage[MAXIMUM_PORTAL_HULL_VERTICES];
	short result_count = 0;
	short index;

	memcpy(storage, points, sizeof(*points) * count);
	for (index = 0; index < count; index++)
	{
		real_point3d const *a = &storage[index];
		real_point3d const *b = &storage[(index + 1) % count];
		real da = plane->n.i * a->x + plane->n.j * a->y + plane->n.k * a->z - plane->d;
		real db = plane->n.i * b->x + plane->n.j * b->y + plane->n.k * b->z - plane->d;

		if (da >= 0.0f)
		{
			if (result_count == maximum_count)
				return NONE;
			result[result_count++] = *a;
		}
		if ((da >= 0.0f) != (db >= 0.0f))
		{
			real scale = da / (da - db);

			if (result_count == maximum_count)
				return NONE;
			result[result_count].x = a->x + (b->x - a->x) * scale;
			result[result_count].y = a->y + (b->y - a->y) * scale;
			result[result_count].z = a->z + (b->z - a->z) * scale;
			result_count++;
		}
	}
	(void)clipped; (void)epsilon; (void)keep_coplanar;
	return result_count < 3 ? 0 : result_count;
}

/* the part of convex polygon q inside convex polygon p (as geometry.c's) */
static short convex_hull2d_intersect(short p_count, real_point2d const *p, short q_count, real_point2d const *q,
	short maximum_count, real_point2d *result, real epsilon)
{
	real_point2d buffers[2][MAXIMUM_PORTAL_HULL_VERTICES];
	real_point2d const *source = q;
	short count = q_count;
	real area = 0.0f;
	short edge;

	for (edge = 0; edge < p_count; edge++)
		area += p[edge].x * p[(edge + 1) % p_count].y - p[(edge + 1) % p_count].x * p[edge].y;
	for (edge = 0; edge < p_count && count > 0; edge++)
	{
		real_point2d const *a = &p[edge];
		real_point2d const *b = &p[(edge + 1) % p_count];
		real_point2d *output = edge == p_count - 1 ? result : buffers[edge & 1];
		short output_count = 0;
		short index;

		for (index = 0; index < count; index++)
		{
			real_point2d const *s = &source[index];
			real_point2d const *e = &source[(index + 1) % count];
			real ds = ((b->x - a->x) * (s->y - a->y) - (b->y - a->y) * (s->x - a->x)) * (area < 0.0f ? -1.0f : 1.0f);
			real de = ((b->x - a->x) * (e->y - a->y) - (b->y - a->y) * (e->x - a->x)) * (area < 0.0f ? -1.0f : 1.0f);

			if (ds >= 0.0f)
			{
				if (output_count == maximum_count)
					return NONE;
				output[output_count++] = *s;
			}
			if ((ds >= 0.0f) != (de >= 0.0f))
			{
				real scale = ds / (ds - de);

				if (output_count == maximum_count)
					return NONE;
				output[output_count].x = s->x + (e->x - s->x) * scale;
				output[output_count].y = s->y + (e->y - s->y) * scale;
				output_count++;
			}
		}
		count = output_count < 3 ? 0 : output_count;
		if (edge == p_count - 1 || !count)
		{
			if (output != result)
				memcpy(result, output, sizeof(*output) * count);
			return count;
		}
		source = output;
	}
	(void)epsilon;
	return count;
}

#include "under_test.inc"

/* a reproducible world */
static unsigned long seed;
static real random_real(real minimum, real maximum)
{
	seed = seed * 1664525UL + 1013904223UL;
	return minimum + (maximum - minimum) * (real)((seed >> 8) & 0xFFFF) / 65535.0f;
}

static void add_portal(short from, short to, real_point3d const *center, real size)
{
	short portal_index = (short)structure.cluster_portals.count++;
	struct structure_visibility_portal *portal = &portals[portal_index];
	real_vector3d normal, u, v;
	real length;
	short vertex_index;

	normal.i = random_real(-1.0f, 1.0f);
	normal.j = random_real(-1.0f, 1.0f);
	normal.k = random_real(-1.0f, 1.0f);
	length = sqrtf(dot_product3d(&normal, &normal)) + 1e-6f;
	normal.i /= length; normal.j /= length; normal.k /= length;
	/* two directions in the plane */
	u.i = -normal.j; u.j = normal.i; u.k = 0.0f;
	length = sqrtf(dot_product3d(&u, &u)) + 1e-6f;
	u.i /= length; u.j /= length;
	v.i = normal.j * u.k - normal.k * u.j;
	v.j = normal.k * u.i - normal.i * u.k;
	v.k = normal.i * u.j - normal.j * u.i;
	planes[portal_index].n = normal;
	planes[portal_index].d = dot_product3d(&normal, (real_vector3d const *)center);
	for (vertex_index = 0; vertex_index < PORTAL_VERTICES; vertex_index++)
	{
		real a = vertex_index == 1 || vertex_index == 2 ? size : -size;
		real b = vertex_index >= 2 ? size : -size;

		portal_vertices[portal_index][vertex_index].x = center->x + u.i * a + v.i * b;
		portal_vertices[portal_index][vertex_index].y = center->y + u.j * a + v.j * b;
		portal_vertices[portal_index][vertex_index].z = center->z + u.k * a + v.k * b;
	}
	portal->plane_index = portal_index;
	portal->cluster_indices[0] = from;
	portal->cluster_indices[1] = to;
	portal->vertices.count = PORTAL_VERTICES;
	portal->vertices.address = portal_vertices[portal_index];
	portal_lists[from][clusters[from].portal_indices.count++] = portal_index;
	portal_lists[to][clusters[to].portal_indices.count++] = portal_index;
}

/* clusters around the camera, most joined by a portal or two, each seeing most others */
static void build_world(boolean wide)
{
	short cluster_count = wide ? 3 : 10;
	short from, to;

	memset(&structure, 0, sizeof(structure));
	memset(clusters, 0, sizeof(clusters));
	memset(pvs, 0, sizeof(pvs));
	seed = wide ? 7 : 1;
	structure.clusters.count = cluster_count;
	structure.clusters.address = clusters;
	structure.cluster_portals.address = portals;
	structure.collision_bsp.count = 1;
	structure.collision_bsp.address = &collision_bsp;
	collision_bsp.bsp3d.planes.address = planes;
	for (from = 0; from < cluster_count; from++)
	{
		clusters[from].portal_indices.address = portal_lists[from];
		for (to = 0; to < cluster_count; to++)
		{
			if (from == to || random_real(0.0f, 1.0f) < 0.85f)
				BIT_VECTOR_SET_FLAG(pvs[from], to, TRUE);
		}
	}
	for (from = 0; from < cluster_count; from++)
	{
		for (to = from + 1; to < cluster_count; to++)
		{
			short count = wide ? 200 : random_real(0.0f, 1.0f) < 0.3f ? 1 : random_real(0.0f, 1.0f) < 0.5f ? 2 : 3;
			short index;

			for (index = 0; index < count; index++)
			{
				real_point3d center;

				center.x = random_real(-6.0f, 6.0f);
				center.y = random_real(-6.0f, 6.0f);
				center.z = random_real(-2.0f, 2.0f);
				add_portal(from, to, &center, random_real(2.0f, 6.0f));
			}
		}
	}
	structure.collision_bsp.count = 1;
	collision_bsp.bsp3d.planes.count = structure.cluster_portals.count;
}

/* the camera of a frame: walking round the middle of the world, turning */
static void place_camera(long frame)
{
	real angle = (real)frame * 0.05f;
	real_vector3d forward = { cosf(angle * 1.3f), sinf(angle * 1.3f), 0.2f * sinf(angle) };
	real length = sqrtf(dot_product3d(&forward, &forward));
	real_matrix4x3 *matrix = &render.frustum.world_to_view;

	forward.i /= length; forward.j /= length; forward.k /= length;
	render.camera.position.x = 3.0f * cosf(angle);
	render.camera.position.y = 3.0f * sinf(angle);
	render.camera.position.z = 0.5f * sinf(angle * 2.0f);
	render.camera.forward = forward;
	render.camera.z_far = 1000.0f;
	render.cluster_index = (short)(frame % structure.clusters.count);
	matrix->position = render.camera.position;
	matrix->back.i = -forward.i; matrix->back.j = -forward.j; matrix->back.k = -forward.k;
	matrix->right.i = forward.j; matrix->right.j = -forward.i; matrix->right.k = 0.0f;
	length = sqrtf(dot_product3d(&matrix->right, &matrix->right));
	matrix->right.i /= length; matrix->right.j /= length;
	matrix->up.i = matrix->back.j * matrix->right.k - matrix->back.k * matrix->right.j;
	matrix->up.j = matrix->back.k * matrix->right.i - matrix->back.i * matrix->right.k;
	matrix->up.k = matrix->back.i * matrix->right.j - matrix->back.j * matrix->right.i;
}

/* one frame's traversal, as structure_visibility_compute begins it */
static void frame(long index)
{
	place_camera(index);
	memset(render.visible_cluster_flags, 0, sizeof(render.visible_cluster_flags));
	render.rendered_cluster_count = 0;
	memset(hulls_made, 0, sizeof(hulls_made));
	structure_visibility_find_clusters();
}

/* what the traversal found, bit for bit */
static void print_frame(long index)
{
	short rendered_cluster_index;
	short word_index;

	printf("frame %ld clusters %d:", index, render.rendered_cluster_count);
	for (rendered_cluster_index = 0; rendered_cluster_index < render.rendered_cluster_count; rendered_cluster_index++)
	{
		struct rendered_cluster const *cluster = &render.rendered_clusters[rendered_cluster_index];

		printf(" %d [%a %a %a %a] [%a %a %a %a]", cluster->cluster_index,
			cluster->clip_bounds.x0, cluster->clip_bounds.x1, cluster->clip_bounds.y0, cluster->clip_bounds.y1,
			cluster->frustum.bounds.x0, cluster->frustum.bounds.x1, cluster->frustum.bounds.y0, cluster->frustum.bounds.y1);
	}
	for (word_index = 0; word_index < (short)(MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS); word_index++)
		printf(" %lx", render.visible_cluster_flags[word_index]);
	printf("\n");
}

enum { FRAMES = 128 };

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";
	long index;

	if (!strcmp(name, "results") || !strcmp(name, "results-wide"))
	{
		/* the traversal's clusters and bounds, for the test to compare; the
		world must be one where the traversal reaches clusters by many paths */
		long most_clusters = 0;
		long most_visits = 0;

		build_world(!strcmp(name, "results-wide"));
		CHECK(strcmp(name, "results-wide") || structure.cluster_portals.count > MAXIMUM_CLUSTER_PORTALS_PER_STRUCTURE,
			"the wide world has %ld portals", structure.cluster_portals.count);
		for (index = 0; index < FRAMES; index++)
		{
			visits = 0;
			frame(index);
			print_frame(index);
			if (render.rendered_cluster_count > most_clusters)
				most_clusters = render.rendered_cluster_count;
			if (visits > most_visits)
				most_visits = visits;
		}
		CHECK(most_clusters >= 3, "at most %ld clusters seen in a frame", most_clusters);
		CHECK(most_visits >= 2 * most_clusters, "at most %ld visits in a frame", most_visits);
		return 0;
	}
	if (!strcmp(name, "once"))
	{
		/* each portal's hull made at most once a frame from each side, though
		the traversal reaches portals many times */
		long reached = 0;
		long made = 0;

		build_world(FALSE);
		for (index = 0; index < FRAMES; index++)
		{
			short portal_index;

			visits = 0;
			frame(index);
			reached += visits;
			for (portal_index = 0; portal_index < structure.cluster_portals.count; portal_index++)
			{
				CHECK(hulls_made[portal_index] <= 2, "frame %ld: portal %d's hull made %ld times",
					index, portal_index, hulls_made[portal_index]);
				made += hulls_made[portal_index];
			}
		}
		CHECK(made > 0 && reached > 4 * FRAMES, "%ld hulls made, %ld clusters reached", made, reached);
		return 0;
	}
	fprintf(stderr, "unknown case '%s'\n", name);
	return 2;
}
