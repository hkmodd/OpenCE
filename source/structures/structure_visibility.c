/*
STRUCTURE_VISIBILITY.C

symbols in this file:
00185E20 0020:
	_debug_pvs (0000)
00185E40 00c0:
	_structure_visibility_find_camera (0000)
00185F00 01a0:
	_structure_visibility_traverse_subclusters (0000)
001860A0 0210:
	_structure_visibility_traverse_surface_lists (0000)
001862B0 00b0:
	_bounding_rectangles_intersect (0000)
00186360 0180:
	_planes_intersect_rectangle (0000)
001864E0 00d0:
	_structure_visibility_find_objects (0000)
001865B0 00b0:
	_grow_clipping_rectangle_by_portal_hull (0000)
00186660 00a0:
	_structure_visibility_render_debug_portal_hull (0000)
00186700 0120:
	_dequantize_byte_to_real_rectangle3d (0000)
00186820 0160:
	_structure_visibility_build_surfaces_traverse_clusters (0000)
00186980 01e0:
	_structure_visibility_build_surfaces_traverse_leaf (0000)
00186B60 01e0:
	_portal_hull_from_points (0000)
00186D40 0080:
	_portal_hull_from_portal (0000)
00186DC0 0070:
	_points_within_distance (0000)
00186E30 02c0:
	_structure_visibility_find_mirror (0000)
001870F0 0260:
	_structure_visibility_build_surfaces_traverse_node (0000)
00187350 0390:
	_structure_visibility_traverse_cluster (0000)
001876E0 01e0:
	_structure_visibility_build_surfaces (0000)
001878C0 0110:
	_structure_visibility_find_clusters (0000)
001879D0 0240:
	_structure_visibility_compute (0000)
002A1AF4 0010:
	_screen_plane (0000)
002A1B04 002e:
	??_C@_0CO@GHPNGJCB@render_structure_visibility_surf@ (0000)
002A1B34 0031:
	??_C@_0DB@DIILMMGG@render_structure_visibility_subc@ (0000)
002A1B68 002d:
	??_C@_0CN@MLHMCLEC@render_structure_visibility_port@ (0000)
002A1B98 005f:
	??_C@_0FP@IPNIKPHC@surface_index_buffer?9?$CIlong?5?$CK?$CJ?5cl@ (0000)
002A1BF8 0031:
	??_C@_0DB@MPCJDDJE@c?3?2halo?2SOURCE?2structures?2struct@ (0000)
002A1C2C 0018:
	??_C@_0BI@CMHFMAEP@valid_portal_hull?$CIhull?$CJ?$AA@ (0000)
002A1C44 000a:
	??_C@_09KEGCCCGI@rectangle?$AA@ (0000)
002A1C50 0048:
	??_C@_0EI@NFFPBLL@leaf?9?$DOcluster_index?$DO?$DN0?5?$CG?$CG?5leaf?9?$DO@ (0000)
002A1C98 000c:
	??_C@_0M@ONJFEIPI@cull_bounds?$AA@ (0000)
002A1CA4 0013:
	??_C@_0BD@FKHEAHGO@cull_sphere_center?$AA@ (0000)
002A1CB8 000e:
	??_C@_0O@FEFHBCMC@parent_bounds?$AA@ (0000)
002A1CC8 000d:
	??_C@_0N@JIFGGPLM@intersection?$AA@ (0000)
002A1CD8 0008:
	??_C@_07KLBPNJKH@ooz?$DO0?4f?$AA@ (0000)
002A1CE0 001b:
	??_C@_0BL@FCCKLIAE@result?9?$DOvertex_count?$CB?$DNNONE?$AA@ (0000)
002A1CFC 002a:
	??_C@_0CK@MFEAOING@two?5mirrors?5visible?5with?5differe@ (0000)
002A1D28 001c:
	??_C@_0BM@EAJICMMM@portal?5intersection?5failed?4?$AA@ (0000)
002A1D44 002f:
	??_C@_0CP@KGOLAMLM@rendered_cluster?9?$DOcluster_index?$DN@ (0000)
002A1D74 0020:
	??_C@_0CA@HGGGAHMD@raise?5MAXIMUM_RENDERED_CLUSTERS?$AA@ (0000)
002A1D94 0022:
	??_C@_0CC@IDJIEBBJ@valid_portal_hull?$CIvisible_region@ (0000)
002A1DB8 002d:
	??_C@_0CN@EHONMIFE@?$CBbounding_surface_count?5?$HM?$HM?5bound@ (0000)
002A1DE8 0017:
	??_C@_0BH@ODLPNGPJ@bounding_sphere_center?$AA@ (0000)
002A1E00 0053:
	??_C@_0FD@MFIJDILG@?$CD?$CD?$CD?5WARNING?3?5this?5structure_bsp?5@ (0000)
00313AF0 11ec:
	_render_structure_visibility_portal_traversal (0000)
	_render_structure_visibility_subcluster_traversal (05f8)
	_render_structure_visibility_surface_traversal (0bf0)
	_structure_sphere_threshold (11e8)
004C0CF0 040c:
	_warned_about_missing_subclusters (0000)
	_structure_visibility_globals (0008)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/errors.h"
#include "cseries/profile.h"
#include "ai/ai_debug.h"
#include "structures/structure_visibility.h"
#include "math/geometry.h"
#include "physics/bsp3d.h"
#include "physics/collision_bsp_definitions.h"
#include "objects/objects.h"
#include "render/render.h"
#include "render/render_cameras_internal.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "scenario/sky_definitions.h"
#include "shaders/shader_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"
#include "structure_culling.h" /* port: port/linux/game/structure_culling.c */

/* ---------- constants */

enum
{
	_intersection_out,
	_intersection_spanning,
	_intersection_in,
	_portal_hull_from_portal_succeeded = 0,
	_portal_hull_from_portal_discarded,
	_portal_hull_from_portal_degenerate,
	MAXIMUM_PORTAL_HULL_VERTICES = 256,
	/* port: how deep structure_visibility_build_surfaces_traverse_node goes
	in a bsp (from the map) before it stops: four times the deepest bsp the
	tools' leaf maps hold (leaf_map.c's 256-node stack), where the retail
	bsps are at most 66 deep */
	MAXIMUM_STRUCTURE_BSP_TRAVERSAL_DEPTH = 1024,
	COMPRESSED_STRUCTURE_VERTEX_SIZE = 0x20,
	/* port: the hulls one portal traversal remembers (each direction of
	the first 512 portals, the most a bsp has) and their vertices */
	MAXIMUM_PORTAL_HULL_CACHE_ENTRIES = 1024,
	MAXIMUM_PORTAL_HULL_CACHE_VERTICES = 8192,
	/* port: a portal traversal shared with the worker threads: the depth
	of the clusters that each begin a part of it, the parts and the visits
	before them it holds, the vertices of the parts' first regions, the
	threads, and the visits (of the last traversal from the same cluster)
	that make sharing it worth it */
	PORTAL_TRAVERSAL_SPLIT_DEPTH = 2,
	MAXIMUM_PORTAL_TRAVERSAL_TASKS = 128,
	MAXIMUM_PORTAL_TRAVERSAL_EVENTS = 512,
	MAXIMUM_PORTAL_TRAVERSAL_VERTICES = 8192,
	MAXIMUM_PORTAL_TRAVERSAL_RUNNERS = 9,
	PORTAL_TRAVERSAL_SHARED_VISITS = 256,
	_shader_type_environment = 3,
};

/* ---------- macros */

#define valid_portal_hull(hull) \
	((hull) && (hull)->vertex_count >= 0 && \
	(hull)->vertex_count <= MAXIMUM_PORTAL_HULL_VERTICES)
#define structure_visibility_shader_environment_get(shader) \
	((struct structure_visibility_shader_environment *)shader_get_and_verify_type( \
		(shader), _shader_type_environment))

/* ---------- structures */

struct structure_visibility_sky
{
	struct tag_reference model;
};

struct portal_hull
{
	short vertex_count;
	word pad;
	real_point2d vertices[MAXIMUM_PORTAL_HULL_VERTICES];
};

/* port: a portal's hull in one direction, as the traversal numbered
traversal projected it (claimed: the traversal whose thread projecting it
writes it here; traversal, written last, says it is written) */
struct portal_hull_cache_entry
{
	unsigned long traversal;
	unsigned long claimed;
	short result;
	short vertex_count;
	long first_vertex_index;
};

struct portal_hull_cache
{
	unsigned long traversal;
	long vertex_count;
	struct portal_hull_cache_entry entries[MAXIMUM_PORTAL_HULL_CACHE_ENTRIES];
	real_point2d vertices[MAXIMUM_PORTAL_HULL_CACHE_VERTICES];
};

struct structure_visibility_cluster
{
	short sky_index;
	short fog_designator;
	short background_sound_palette_index;
	short sound_environment_palette_index;
	short weather_palette_index;
	short transition_structure_bsp_index;
	short first_runtime_decal_index;
	word runtime_decal_count;
	long unused[6];
	struct tag_block predicted_resources;
	struct tag_block subclusters;
	word first_lens_flare_marker_index;
	word lens_flare_marker_count;
	struct tag_block surface_indices;
	struct tag_block mirrors;
	struct tag_block portal_indices;
};

struct structure_visibility_subcluster
{
	real_rectangle3d world_bounds;
	struct tag_block surface_indices;
};

struct structure_visibility_surface_reference
{
	long surface_index;
	long bsp3d_node_index;
};

struct structure_visibility_surface
{
	word vertex_indices[NUMBER_OF_VERTICES_PER_TRIANGLE];
};

struct structure_visibility_portal
{
	short cluster_indices[2];
	long plane_index;
	real_point3d centroid;
	real bounding_radius;
	unsigned long flags;
	long unused[6];
	struct tag_block vertices;
};

struct structure_visibility_mirror
{
	real_plane3d plane;
	long unused[5];
	struct tag_reference shader;
	struct tag_block points;
};

struct structure_visibility_globals
{
	unsigned long *visited_cluster_flags;
	short rendered_cluster_indices[MAXIMUM_CLUSTERS_PER_STRUCTURE];
};

/* port: a part of a shared portal traversal: the traversal from a cluster
through the region of it seen (vertex_count vertices from
first_vertex_index), the clusters on the way to it visited, and the
clusters it reached (NONE: too many, or it cannot be shared), each with its
clip bounds as the part grew them from none */
struct portal_traversal_cluster
{
	short cluster_index;
	word pad;
	real_rectangle2d clip_bounds;
};

struct portal_traversal_task
{
	short cluster_index;
	short vertex_count;
	long first_vertex_index;
	long visit_count;
	short cluster_count;
	word pad;
	unsigned long visited_cluster_flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
	struct portal_traversal_cluster clusters[MAXIMUM_RENDERED_CLUSTERS];
};

/* port: what the traversal did before its parts, in order: a visit of a
cluster (task_index NONE) with the clip bounds it grew from none, or a part */
struct portal_traversal_event
{
	short task_index;
	short cluster_index;
	real_rectangle2d clip_bounds;
};

/* port: a thread's clusters visited on the way and reached in a part, and
the place of each reached in the part's clusters */
struct portal_traversal_runner
{
	unsigned long visited_cluster_flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
	unsigned long reached_cluster_flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
	short cluster_indices[MAXIMUM_CLUSTERS_PER_STRUCTURE];
};

struct portal_traversal
{
	short task_count;
	short event_count;
	long vertex_count;
	long visit_count;
	long next_task;
	long next_runner;
	struct structure_bsp *structure;
	/* the visits of the last traversal from each cluster, and the clusters
	whose traversal could not be shared */
	unsigned short cluster_visit_counts[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	unsigned long unshared_cluster_flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
	struct portal_traversal_event events[MAXIMUM_PORTAL_TRAVERSAL_EVENTS];
	struct portal_traversal_task tasks[MAXIMUM_PORTAL_TRAVERSAL_TASKS];
	real_point2d vertices[MAXIMUM_PORTAL_TRAVERSAL_VERTICES];
	struct portal_traversal_runner runners[MAXIMUM_PORTAL_TRAVERSAL_RUNNERS];
};

struct structure_visibility_shader_environment
{
	struct shader shader;
	byte unused[0x2E4];
	real mirror_index_of_refraction;
	real mirror_depth;
};

typedef char structure_visibility_cluster_size_assert[
	sizeof(struct structure_visibility_cluster) == 0x68 ? 1 : -1];
typedef char structure_visibility_subcluster_size_assert[
	sizeof(struct structure_visibility_subcluster) == 0x24 ? 1 : -1];
typedef char structure_visibility_portal_size_assert[
	sizeof(struct structure_visibility_portal) == 0x40 ? 1 : -1];
typedef char structure_visibility_mirror_size_assert[
	sizeof(struct structure_visibility_mirror) == 0x40 ? 1 : -1];
typedef char structure_visibility_shader_environment_refraction_offset_assert[
	offsetof(struct structure_visibility_shader_environment, mirror_index_of_refraction) == 0x30C ? 1 : -1];

/* ---------- prototypes */

static short bounding_rectangles_intersect(
	real_rectangle3d const *test_rectangle,
	real_rectangle3d const *rectangle);
static long planes_intersect_rectangle(
	real_rectangle3d const *bounds,
	short cull_plane_count,
	real_plane3d const *cull_planes);
static void grow_clipping_rectangle_by_portal_hull(
	real_rectangle2d *rectangle,
	struct portal_hull const *hull);
static boolean points_within_distance(
	short point_count,
	real_point3d const *points,
	real distance);
static short portal_hull_from_points(
	struct render_camera const *camera,
	struct render_frustum const *frustum,
	real_plane3d const *plane,
	short vertex_count,
	real_point3d const *vertices,
	short winding,
	struct portal_hull *result);
static short portal_hull_from_portal(
	short portal_index,
	boolean direction,
	struct portal_hull *result);
static short portal_hull_from_portal_cached(
	short portal_index,
	boolean direction,
	struct portal_hull *result);
static void structure_visibility_render_debug_portal_hull(
	struct portal_hull const *hull,
	real_argb_color const *color,
	real offset);
static void structure_visibility_traverse_cluster(
	short cluster_index,
	struct portal_hull const *visible_region);
static void structure_visibility_find_clusters(
	void);
static boolean portal_traversal_visit(
	struct portal_traversal_runner *runner,
	struct portal_traversal_task *task,
	short cluster_index,
	struct portal_hull const *visible_region,
	short depth);
static void portal_traversal_run_tasks(
	struct portal_traversal_runner *runner);
static void portal_traversal_job(
	void);
static boolean portal_traversal_merge_cluster(
	short cluster_index,
	real_rectangle2d const *clip_bounds);
static boolean structure_visibility_find_clusters_shared(
	struct portal_hull const *screen_hull);
static short structure_visibility_build_surfaces_traverse_clusters(
	long *surface_indices,
	short maximum_count,
	real_point3d const *bounding_sphere_center,
	real bounding_sphere_radius,
	real_rectangle3d const *bounding_box,
	short bounding_surface_count,
	real_plane3d const *bounding_surfaces,
	unsigned long *surface_flags,
	short cluster_count,
	short const *cluster_indices);
static short structure_visibility_build_surfaces_traverse_leaf(
	long leaf_index,
	real_rectangle3d const *parent_bounds,
	unsigned long *surface_flags,
	long *surface_indices,
	short maximum_count,
	real_point3d const *cull_sphere_center,
	real cull_sphere_radius,
	real_rectangle3d const *cull_bounds,
	short cull_plane_count,
	real_plane3d const *cull_planes,
	short intersection);
static short structure_visibility_build_surfaces_traverse_node(
	long node_index,
	real_rectangle3d const *parent_bounds,
	unsigned long *surface_flags,
	long *surface_indices,
	short maximum_count,
	real_point3d const *cull_sphere_center,
	real cull_sphere_radius,
	real_rectangle3d const *cull_bounds,
	short cull_plane_count,
	real_plane3d const *cull_planes,
	short intersection,
	short depth);
static void structure_visibility_traverse_subclusters(
	struct structure_bsp *structure);
static void structure_visibility_traverse_surface_lists(
	struct structure_bsp *structure);

/* ---------- globals */

boolean structures_use_pvs_for_vs;
boolean debug_portals;

static const real_plane3d screen_plane =
{
	{ 0.0f, 0.0f, -1.0f },
	0.01f,
};

static boolean warned_about_missing_subclusters;
/* port: whether a map's malformed bsp was reported (once each) */
static boolean warned_about_portal_hull_vertices;
static boolean warned_about_invalid_structure_bsp;
static boolean warned_about_rendered_clusters;
static boolean warned_about_structure_bsp_depth;
static boolean warned_about_environment_surfaces;
static struct structure_visibility_globals structure_visibility_globals;
/* port: the hulls projected in the current portal traversal */
static struct portal_hull_cache portal_hull_cache;
/* port: the traversal shared with the worker threads */
static struct portal_traversal portal_traversal;
static struct profile_section render_structure_visibility_portal_traversal =
	{ "render_structure_visibility_portal_traversal", NONE, TRUE };
/* port: shared with the culling walk so both paths use the same profile sections */
struct profile_section render_structure_visibility_subcluster_traversal =
	{ "render_structure_visibility_subcluster_traversal", NONE, TRUE };
struct profile_section render_structure_visibility_surface_traversal =
	{ "render_structure_visibility_surface_traversal", NONE, TRUE };
real structure_sphere_threshold = 2.0f;

/* ---------- public code */

void debug_pvs(
	boolean on)
{
	debug_portals = on;
	structures_use_pvs_for_vs = on;

	return;
}

void structure_visibility_find_camera(
	const struct render_camera *camera)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	long leaf_index = bsp3d_test_point(
		&TAG_BLOCK_GET_ELEMENT(&structure->collision_bsp, 0, struct collision_bsp)->bsp3d,
		0,
		&camera->position);

	if (leaf_index == NONE)
	{
		leaf_index = render.leaf_index;
		if (leaf_index < structure->leaves.count)
		{
			goto leaf_index_ready;
		}

		leaf_index = NONE;
	}

	render.leaf_index = leaf_index;

leaf_index_ready:
	render.cluster_index = NONE;
	render.visible_sky_index = NONE;
	render.visible_sky_model = FALSE;

	/* port: a leaf (from the map) that is no leaf, or in no cluster, is
	in none */
	if (leaf_index != NONE &&
		(leaf_index & LONG_MAX) < structure->leaves.count)
	{
		struct structure_leaf *leaf = TAG_BLOCK_GET_ELEMENT(
			&structure->leaves,
			leaf_index & LONG_MAX,
			struct structure_leaf);
		struct structure_cluster *cluster;
		struct structure_visibility_sky *sky;

		if (leaf->cluster_index < 0 || leaf->cluster_index >= structure->clusters.count)
		{
			return;
		}
		render.cluster_index = leaf->cluster_index;
		cluster = TAG_BLOCK_GET_ELEMENT(&structure->clusters, render.cluster_index, struct structure_cluster);
		render.visible_sky_index = cluster->sky_index;
		sky = (struct structure_visibility_sky *)scenario_get_sky(render.visible_sky_index);
		if (sky && sky->model.index != NONE)
		{
			render.visible_sky_model = TRUE;
		}
	}

	return;
}

/* ---------- private code */

static void structure_visibility_traverse_subclusters(
	struct structure_bsp *structure)
{
	long rendered_cluster_index;

	profile_enter(render_structure_visibility_subcluster_traversal);
	for (rendered_cluster_index = 0;
		(short)rendered_cluster_index < render.rendered_cluster_count;
		rendered_cluster_index++)
	{
		struct rendered_cluster *rendered_cluster;
		struct structure_visibility_cluster *cluster;
		struct render_frustum *frustum;
		long subcluster_index;

		if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
		{
			break;
		}

		rendered_cluster = rendered_cluster_get(rendered_cluster_index);
		cluster = TAG_BLOCK_GET_ELEMENT(
			&structure->clusters,
			rendered_cluster->cluster_index,
			struct structure_visibility_cluster);
		frustum = structures_use_pvs_for_vs || render.cluster_index == NONE
			? &render.frustum
			: &rendered_cluster->frustum;

		for (subcluster_index = 0;
			(short)subcluster_index < cluster->subclusters.count;
			subcluster_index++)
		{
			struct structure_visibility_subcluster *subcluster;
			long *surface_index_buffer;
			long surface_list_index;

			if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
			{
				break;
			}

			subcluster = TAG_BLOCK_GET_ELEMENT(
				&cluster->subclusters,
				(short)subcluster_index,
				struct structure_visibility_subcluster);
			if (!render_frustum_cube_visible(frustum, &subcluster->world_bounds, FALSE))
			{
				continue;
			}

			surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
			/* port: compared whole: a short index wraps past 32767 surfaces
			and the walk never ends */
			for (surface_list_index = 0;
				surface_list_index < subcluster->surface_indices.count;
				surface_index_buffer++, surface_list_index++)
			{
				/* port: a surface index (from the map) that is no surface is
				skipped (the bsp's surfaces fit the flags, as
				structure_bsp_port_verify checked) */
				if ((unsigned long)*surface_index_buffer >= (unsigned long)structure->surfaces.count)
				{
					continue;
				}

				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer))
				{
					if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
					{
						break;
					}

					BIT_VECTOR_SET_FLAG(render.environment_surface_flags, *surface_index_buffer, TRUE);
					render.environment_surface_count++;
				}
			}
		}
	}
	profile_exit(render_structure_visibility_subcluster_traversal);

	return;
}

static void structure_visibility_traverse_surface_lists(
	struct structure_bsp *structure)
{
	long rendered_cluster_index;

	profile_enter(render_structure_visibility_surface_traversal);
	for (rendered_cluster_index = 0;
		(short)rendered_cluster_index < render.rendered_cluster_count;
		rendered_cluster_index++)
	{
		struct rendered_cluster *rendered_cluster = rendered_cluster_get(rendered_cluster_index);
		struct structure_visibility_cluster *cluster = TAG_BLOCK_GET_ELEMENT(
			&structure->clusters,
			rendered_cluster->cluster_index,
			struct structure_visibility_cluster);
		long *surface_index_buffer = (long *)cluster->surface_indices.address;
		struct render_frustum *frustum = structures_use_pvs_for_vs || render.cluster_index == NONE
			? &render.frustum
			: &rendered_cluster->frustum;
		long consumed_surface_index_count = 0;

		while (consumed_surface_index_count < cluster->surface_indices.count)
		{
			struct tag_block *materials;
			struct structure_material *material;
			long group_end;
			long vertex_limit;

			/* port: a group (from the map) whose header is not all in the
			list, or names no lightmap's material, ends the list; its
			surfaces end with the list */
			if (consumed_surface_index_count > cluster->surface_indices.count - 3 ||
				surface_index_buffer[0] < 0 ||
				surface_index_buffer[0] >= structure->lightmaps.count)
			{
				break;
			}
			materials = &TAG_BLOCK_GET_ELEMENT(
				&structure->lightmaps,
				surface_index_buffer[0],
				struct structure_lightmap)->materials;
			if (surface_index_buffer[1] < 0 ||
				surface_index_buffer[1] >= materials->count)
			{
				break;
			}
			material = TAG_BLOCK_GET_ELEMENT(
				materials,
				surface_index_buffer[1],
				struct structure_material);
			/* port: how many vertices the material's compressed vertex data
			(from the map) holds: a surface with one past them is skipped
			(the retail clusters have no surface lists) */
			vertex_limit = material->compressed_vertex_data.size / COMPRESSED_STRUCTURE_VERTEX_SIZE;

			surface_index_buffer += 2;
			group_end = consumed_surface_index_count + *surface_index_buffer++ + 3;
			consumed_surface_index_count += 3;
			if (group_end > cluster->surface_indices.count)
			{
				group_end = cluster->surface_indices.count;
			}

			while (consumed_surface_index_count < group_end)
			{
				long surface_index;

				if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES)
				{
					break;
				}

				surface_index = *surface_index_buffer++;
				match_assert(
					"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
					0x1A0,
					surface_index_buffer-(long *) cluster->surface_indices.address<=cluster->surface_indices.count);
				/* port: (as in structure_visibility_traverse_subclusters) */
				if ((unsigned long)surface_index >= (unsigned long)structure->surfaces.count)
				{
					consumed_surface_index_count++;
					continue;
				}
				if (!BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, surface_index))
				{
					struct structure_visibility_surface *surface = TAG_BLOCK_GET_ELEMENT(
						&structure->surfaces,
						surface_index,
						struct structure_visibility_surface);
					real_point3d const *vertex0 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[0] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex1 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[1] * COMPRESSED_STRUCTURE_VERTEX_SIZE);
					real_point3d const *vertex2 = (real_point3d const *)(
						(byte *)material->compressed_vertex_data.address +
						surface->vertex_indices[2] * COMPRESSED_STRUCTURE_VERTEX_SIZE);

					/* port: (vertex_limit) */
					if (surface->vertex_indices[0] < vertex_limit &&
						surface->vertex_indices[1] < vertex_limit &&
						surface->vertex_indices[2] < vertex_limit &&
						render_frustum_triangle_visible(
						frustum,
						vertex0,
						vertex1,
						vertex2))
					{
						BIT_VECTOR_SET_FLAG(render.environment_surface_flags, surface_index, TRUE);
						render.environment_surface_count++;
					}
				}

				consumed_surface_index_count++;
			}
		}
	}
	profile_exit(render_structure_visibility_surface_traversal);

	return;
}

static short bounding_rectangles_intersect(
	real_rectangle3d const *test_rectangle,
	real_rectangle3d const *rectangle)
{
	short result;

	if (rectangle->x1 < test_rectangle->x0 ||
		rectangle->x0 > test_rectangle->x1 ||
		rectangle->y1 < test_rectangle->y0 ||
		rectangle->y0 > test_rectangle->y1 ||
		rectangle->z1 < test_rectangle->z0 ||
		rectangle->z0 > test_rectangle->z1)
	{
		result = _intersection_out;
	}
	else if (test_rectangle->x0 < rectangle->x0 ||
		test_rectangle->x1 > rectangle->x1 ||
		test_rectangle->y0 < rectangle->y0 ||
		test_rectangle->y1 > rectangle->y1 ||
		test_rectangle->z0 < rectangle->z0 ||
		test_rectangle->z1 > rectangle->z1)
	{
		result = _intersection_spanning;
	}
	else
	{
		result = _intersection_in;
	}

	return result;
}

static long planes_intersect_rectangle(
	real_rectangle3d const *bounds,
	short cull_plane_count,
	real_plane3d const *cull_planes)
{
	short accumulated_behind = 0;
	real_rectangle3d local_bounds = *bounds;
	short plane_index;

	for (plane_index = 0; plane_index < cull_plane_count; plane_index++)
	{
		real_plane3d plane = cull_planes[plane_index];
		short corner_mask = 0;

		if (plane.n.i * local_bounds.x0 + plane.n.j * local_bounds.y0 + plane.n.k * local_bounds.z0 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(0);
		}
		if (plane.n.i * local_bounds.x1 + plane.n.j * local_bounds.y0 + plane.n.k * local_bounds.z0 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(1);
		}
		if (plane.n.i * local_bounds.x0 + plane.n.j * local_bounds.y1 + plane.n.k * local_bounds.z0 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(2);
		}
		if (plane.n.i * local_bounds.x1 + plane.n.j * local_bounds.y1 + plane.n.k * local_bounds.z0 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(3);
		}
		if (plane.n.i * local_bounds.x0 + plane.n.j * local_bounds.y0 + plane.n.k * local_bounds.z1 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(4);
		}
		if (plane.n.i * local_bounds.x1 + plane.n.j * local_bounds.y0 + plane.n.k * local_bounds.z1 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(5);
		}
		if (plane.n.i * local_bounds.x0 + plane.n.j * local_bounds.y1 + plane.n.k * local_bounds.z1 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(6);
		}
		if (plane.n.i * local_bounds.x1 + plane.n.j * local_bounds.y1 + plane.n.k * local_bounds.z1 - plane.d < 0.0f)
		{
			corner_mask |= FLAG(7);
		}

		if (corner_mask == UNSIGNED_CHAR_MAX)
		{
			return _intersection_out;
		}

		accumulated_behind |= corner_mask;
	}

	if (accumulated_behind)
	{
		return _intersection_spanning;
	}

	return _intersection_in;
}

short structure_visibility_find_objects(
	long *result_indices,
	short maximum_count,
	long (*cluster_get_first)(long *iterator, short cluster_index),
	long (*cluster_get_next)(long *iterator),
	void (*get_bounding_sphere)(long object_index, real_point3d *center, real *radius),
	boolean (*unmarked)(long object_index),
	boolean (*mark)(long object_index))
{
	struct structure_bsp *structure = global_structure_bsp_get();
	short found_count = 0;
	long rendered_cluster_index;

	for (rendered_cluster_index = 0;
		(short)rendered_cluster_index < render.rendered_cluster_count;
		rendered_cluster_index++)
	{
		struct rendered_cluster *rendered_cluster = rendered_cluster_get(rendered_cluster_index);
		long iterator;
		long object_index;

		for (object_index = cluster_get_first(&iterator, rendered_cluster->cluster_index);
			object_index != NONE;
			object_index = cluster_get_next(&iterator))
		{
			real_point3d center;
			real radius;

			if (!unmarked(object_index))
			{
				continue;
			}

			get_bounding_sphere(object_index, &center, &radius);
			if (found_count < maximum_count &&
				(render.cluster_index == NONE ||
				render_frustum_sphere_visible(&rendered_cluster->frustum, &center, radius)))
			{
				result_indices[found_count++] = object_index;
				mark(object_index);
			}
		}
	}

	(void)structure;
	return found_count;
}

static void grow_clipping_rectangle_by_portal_hull(
	real_rectangle2d *rectangle,
	struct portal_hull const *hull)
{
	short vertex_index;
	real_point2d const *vertex;

	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x4CF,
		rectangle);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x4D0,
		valid_portal_hull(hull));

	vertex = hull->vertices;
	for (vertex_index = 0;
		vertex_index < hull->vertex_count;)
	{
		if (rectangle->x0 > vertex->x)
		{
			rectangle->x0 = vertex->x;
		}
		if (rectangle->x1 < vertex->x)
		{
			rectangle->x1 = vertex->x;
		}
		if (rectangle->y0 > vertex->y)
		{
			rectangle->y0 = vertex->y;
		}
		if (rectangle->y1 < vertex->y)
		{
			rectangle->y1 = vertex->y;
		}

		vertex++;
		vertex_index++;
	}

	return;
}

/* port: the traversal reaches a portal once on each path through the
portals before it and projected the portal again each time, with the same
camera and frustum, so it now takes a hull projected before in the same
traversal from portal_hull_cache: the same result and vertices, copied.
The threads of a shared traversal (structure_visibility_find_clusters_shared)
share it: the first to claim a hull writes it, and one finding it claimed
but not yet written projects it for itself */
static short portal_hull_from_portal_cached(
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	long entry_index = 2 * (long)portal_index + (direction ? 1 : 0);
	unsigned long traversal = portal_hull_cache.traversal;
	struct portal_hull_cache_entry *entry;
	unsigned long claimed;
	long first_vertex_index;
	short hull_result;

	if (entry_index < 0 || entry_index >= MAXIMUM_PORTAL_HULL_CACHE_ENTRIES)
	{
		return portal_hull_from_portal(portal_index, direction, result);
	}

	entry = &portal_hull_cache.entries[entry_index];
	if (__atomic_load_n(&entry->traversal, __ATOMIC_ACQUIRE) == traversal)
	{
		result->vertex_count = entry->vertex_count;
		csmemcpy(
			result->vertices,
			&portal_hull_cache.vertices[entry->first_vertex_index],
			entry->vertex_count * sizeof(real_point2d));
		return entry->result;
	}

	hull_result = portal_hull_from_portal(portal_index, direction, result);
	claimed = __atomic_load_n(&entry->claimed, __ATOMIC_RELAXED);
	if (claimed != traversal &&
		__atomic_compare_exchange_n(&entry->claimed, &claimed, traversal, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
	{
		first_vertex_index = __atomic_load_n(&portal_hull_cache.vertex_count, __ATOMIC_RELAXED);
		while (result->vertex_count <= MAXIMUM_PORTAL_HULL_CACHE_VERTICES - first_vertex_index)
		{
			if (__atomic_compare_exchange_n(&portal_hull_cache.vertex_count, &first_vertex_index,
				first_vertex_index + result->vertex_count, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
			{
				entry->result = hull_result;
				entry->vertex_count = result->vertex_count;
				entry->first_vertex_index = first_vertex_index;
				csmemcpy(
					&portal_hull_cache.vertices[first_vertex_index],
					result->vertices,
					result->vertex_count * sizeof(real_point2d));
				__atomic_store_n(&entry->traversal, traversal, __ATOMIC_RELEASE);
				break;
			}
		}
	}

	return hull_result;
}

static void structure_visibility_render_debug_portal_hull(
	struct portal_hull const *hull,
	real_argb_color const *color,
	real offset)
{
	real_point3d previous_point;
	short vertex_index;

	if (hull->vertex_count)
	{
		previous_point.x = hull->vertices[hull->vertex_count - 1].x;
		previous_point.y = hull->vertices[hull->vertex_count - 1].y;
		previous_point.z = -1.0f;
		matrix4x3_transform_point(
			&render.frustum.view_to_world,
			&previous_point,
			&previous_point);
	}

	for (vertex_index = 0; vertex_index < hull->vertex_count; vertex_index++)
	{
		real_point3d point;

		point.x = hull->vertices[vertex_index].x;
		point.y = hull->vertices[vertex_index].y;
		point.z = -1.0f;
		matrix4x3_transform_point(
			&render.frustum.view_to_world,
			&point,
			&point);
		render_debug_line(TRUE, &point, &previous_point, color);
		previous_point = point;
	}

	(void)offset;
	return;
}

static boolean points_within_distance(
	short point_count,
	real_point3d const *points,
	real distance)
{
	short point_index = 0;

	while (point_index < point_count)
	{
		real_point3d const *point = &points[point_index];
		real_vector3d camera_to_point;
		real camera_distance;

		vector_from_points3d(
			&render.camera.position,
			point,
			&camera_to_point);
		camera_distance = dot_product3d(&render.camera.forward, &camera_to_point);

		if (camera_distance <= distance)
		{
			return TRUE;
		}

		point_index++;
	}

	return FALSE;
}

static __inline real structure_visibility_dequantize_byte_to_real(
	real min,
	real max,
	byte value)
{
	if (value == UNSIGNED_CHAR_MAX)
	{
		return max;
	}

	return (max - min) * ((real)value / 255.f) + min;
}

real_rectangle3d *dequantize_byte_to_real_rectangle3d(
	real_rectangle3d const *parent,
	byte_rectangle3d const *compressed_rectangle,
	real_rectangle3d *result)
{
	result->x0 = structure_visibility_dequantize_byte_to_real(parent->x0, parent->x1, compressed_rectangle->x0);
	result->x1 = structure_visibility_dequantize_byte_to_real(parent->x0, parent->x1, compressed_rectangle->x1);
	result->y0 = structure_visibility_dequantize_byte_to_real(parent->y0, parent->y1, compressed_rectangle->y0);
	result->y1 = structure_visibility_dequantize_byte_to_real(parent->y0, parent->y1, compressed_rectangle->y1);
	result->z0 = structure_visibility_dequantize_byte_to_real(parent->z0, parent->z1, compressed_rectangle->z0);
	result->z1 = structure_visibility_dequantize_byte_to_real(parent->z0, parent->z1, compressed_rectangle->z1);

	return result;
}

static short portal_hull_from_points(
	struct render_camera const *camera,
	struct render_frustum const *frustum,
	real_plane3d const *plane,
	short vertex_count,
	real_point3d const *vertices,
	short winding,
	struct portal_hull *result)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	real facing;
	real_point3d viewer_points[MAXIMUM_PORTAL_HULL_VERTICES];
	short vertex_index;
	short first_vertex_index;
	short terminator;
	short output_index;
	short hull_result;

	result->vertex_count = 0;

	/* port: a portal or mirror (from the map) with more vertices than
	viewer_points holds is not seen through */
	if (vertex_count < 0 || vertex_count > MAXIMUM_PORTAL_HULL_VERTICES)
	{
		if (!warned_about_portal_hull_vertices)
		{
			error(_error_silent, "a portal or mirror has %d vertices (maximum %d)",
				vertex_count, MAXIMUM_PORTAL_HULL_VERTICES);
			warned_about_portal_hull_vertices = TRUE;
		}
		return _portal_hull_from_portal_discarded;
	}

	facing = plane3d_distance_to_point(plane, &camera->position) * winding;

	if (camera->mirrored)
	{
		winding = -winding;
	}

	if (fabs(facing) < 0.1f)
	{
		hull_result = _portal_hull_from_portal_degenerate;
	}
	else if (facing > 0.0f && vertex_count < NUMBER_OF_VERTICES_PER_TRIANGLE)
	{
		/* port: nor is one with too few to clip (no hull was made of it) */
		hull_result = _portal_hull_from_portal_discarded;
	}
	else if (facing > 0.0f)
	{
		for (vertex_index = 0; vertex_index < vertex_count; vertex_index++)
		{
			matrix4x3_transform_point(
				&frustum->world_to_view,
				&vertices[vertex_index],
				&viewer_points[vertex_index]);
		}

		result->vertex_count = convex_polygon3d_clip_to_plane(
			vertex_count,
			viewer_points,
			&screen_plane,
			MAXIMUM_PORTAL_HULL_VERTICES,
			viewer_points,
			NULL,
			0.0001f,
			TRUE);
		match_assert(
			"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
			0x485,
			result->vertex_count!=NONE);
		/* port: a polygon (from the map) that clips to more vertices than a
		hull holds makes none (the hull's vertices are not written past) */
		if (result->vertex_count == NONE)
		{
			result->vertex_count = 0;
			return _portal_hull_from_portal_discarded;
		}

		if (winding == 1)
		{
			first_vertex_index = 0;
			terminator = result->vertex_count;
		}
		else
		{
			first_vertex_index = result->vertex_count - 1;
			terminator = NONE;
		}

		for (output_index = 0, vertex_index = first_vertex_index;
			vertex_index != terminator;
			vertex_index += winding, output_index++)
		{
			real ooz = -1.0f / viewer_points[vertex_index].z;

			match_assert(
				"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
				0x497,
				ooz>0.f);
			result->vertices[output_index].x = viewer_points[vertex_index].x * ooz;
			result->vertices[output_index].y = viewer_points[vertex_index].y * ooz;
		}

		hull_result = result->vertex_count >= NUMBER_OF_VERTICES_PER_TRIANGLE
			? _portal_hull_from_portal_succeeded
			: _portal_hull_from_portal_discarded;
	}
	else
	{
		hull_result = _portal_hull_from_portal_discarded;
	}

	return hull_result;
}

static short portal_hull_from_portal(
	short portal_index,
	boolean direction,
	struct portal_hull *result)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct structure_visibility_portal *portal = TAG_BLOCK_GET_ELEMENT(
		&structure->cluster_portals,
		portal_index,
		struct structure_visibility_portal);

	/* port: a portal (from the map) whose plane is no plane is not seen
	through */
	if (portal->plane_index < 0 ||
		portal->plane_index >= TAG_BLOCK_GET_ELEMENT(
			&structure->collision_bsp,
			0,
			struct collision_bsp)->bsp3d.planes.count)
	{
		result->vertex_count = 0;
		return _portal_hull_from_portal_discarded;
	}

	return portal_hull_from_points(
		&render.camera,
		&render.frustum,
		TAG_BLOCK_GET_ELEMENT(
			&TAG_BLOCK_GET_ELEMENT(
				&structure->collision_bsp,
				0,
				struct collision_bsp)->bsp3d.planes,
			portal->plane_index,
			real_plane3d),
		(short)portal->vertices.count,
		(real_point3d const *)portal->vertices.address,
		direction ? -1 : 1,
		result);
}

boolean structure_visibility_find_mirror(
	struct render_camera const *camera,
	struct render_frustum const *frustum,
	struct render_mirror *result)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	boolean found = FALSE;

	/* port: (nor mirrors) */
	if (render.cluster_index != NONE &&
		structure_bsp_port_verify(structure))
	{
		real_rectangle2d projection_bounds;
		struct portal_hull projection_hull;
		struct portal_hull mirror_hull;
		struct portal_hull intersection_hull;
		unsigned long *cluster_pvs;
		long cluster_index;

		render_frustum_get_projection_bounds(frustum, &projection_bounds);
		projection_hull.vertex_count = NUMBER_OF_POINTS_PER_RECTANGLE;
		projection_hull.vertices[0].x = projection_bounds.x0;
		projection_hull.vertices[0].y = projection_bounds.y0;
		projection_hull.vertices[1].x = projection_bounds.x1;
		projection_hull.vertices[1].y = projection_bounds.y0;
		projection_hull.vertices[2].x = projection_bounds.x1;
		projection_hull.vertices[2].y = projection_bounds.y1;
		projection_hull.vertices[3].x = projection_bounds.x0;
		projection_hull.vertices[3].y = projection_bounds.y1;

		cluster_pvs = structure_bsp_get_cluster_pvs(structure, (short)render.cluster_index);
		for (cluster_index = 0;
			(short)cluster_index < structure->clusters.count;
			cluster_pvs++)
		{
			if (*cluster_pvs)
			{
				short bit_index;

				for (bit_index = 0;
					bit_index < LONG_BITS && (short)cluster_index < structure->clusters.count;
					bit_index++, cluster_index++)
				{
					struct structure_visibility_cluster *cluster;
					long mirror_index;

					if (!TEST_FLAG(*cluster_pvs, bit_index))
					{
						continue;
					}

					cluster = TAG_BLOCK_GET_ELEMENT(
						&structure->clusters,
						(short)cluster_index,
						struct structure_visibility_cluster);
					for (mirror_index = 0;
						(short)mirror_index < cluster->mirrors.count;
						mirror_index++)
					{
						struct structure_visibility_mirror *mirror = TAG_BLOCK_GET_ELEMENT(
							&cluster->mirrors,
							(short)mirror_index,
							struct structure_visibility_mirror);
						short hull_result = portal_hull_from_points(
							camera,
							frustum,
							&mirror->plane,
							(short)mirror->points.count,
							(real_point3d const *)mirror->points.address,
							1,
							&mirror_hull);

						if (hull_result == _portal_hull_from_portal_succeeded)
						{
							intersection_hull.vertex_count = convex_hull2d_intersect(
								projection_hull.vertex_count,
								projection_hull.vertices,
								mirror_hull.vertex_count,
								mirror_hull.vertices,
								MAXIMUM_PORTAL_HULL_VERTICES,
								intersection_hull.vertices,
								0.0001f);
						}

						if ((hull_result == _portal_hull_from_portal_succeeded && intersection_hull.vertex_count) ||
							hull_result == _portal_hull_from_portal_degenerate)
						{
							struct shader *shader = shader_definition_get(mirror->shader.index);

							if (shader->base.type == _shader_type_environment)
							{
								struct structure_visibility_shader_environment *environment_shader =
									structure_visibility_shader_environment_get(shader);

								result->index_of_refraction = environment_shader->mirror_index_of_refraction;
								result->depth = environment_shader->mirror_depth;
							}
							else
							{
								result->index_of_refraction = 0.0f;
								result->depth = 0.0f;
							}

							if (found &&
								!(fabs(mirror->plane.n.i - result->plane.n.i) < 0.0001f &&
								fabs(mirror->plane.n.j - result->plane.n.j) < 0.0001f &&
								fabs(mirror->plane.n.k - result->plane.n.k) < 0.0001f &&
								fabs(mirror->plane.d - result->plane.d) < 0.0001f))
							{
								error(
									_error_silent,
									"two mirrors visible with different planes");
							}

							result->plane = mirror->plane;
							result->cluster_index = (short)cluster_index;
							found = TRUE;
						}
					}
				}
			}
			else
			{
				cluster_index += LONG_BITS;
			}
		}
	}

	return found;
}

static short structure_visibility_build_surfaces_traverse_clusters(
	long *surface_indices,
	short maximum_count,
	real_point3d const *bounding_sphere_center,
	real bounding_sphere_radius,
	real_rectangle3d const *bounding_box,
	short bounding_surface_count,
	real_plane3d const *bounding_surfaces,
	unsigned long *surface_flags,
	short cluster_count,
	short const *cluster_indices)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	short found_count = 0;
	long cluster_list_index;

	for (cluster_list_index = 0;
		(short)cluster_list_index < cluster_count && found_count < maximum_count;
		cluster_list_index++)
	{
		struct structure_visibility_cluster *cluster;
		long subcluster_index;

		/* port: a cluster index that fits no cluster is skipped */
		if (cluster_indices[(short)cluster_list_index] < 0 ||
			cluster_indices[(short)cluster_list_index] >= structure->clusters.count)
		{
			continue;
		}
		cluster = TAG_BLOCK_GET_ELEMENT(
			&structure->clusters,
			cluster_indices[(short)cluster_list_index],
			struct structure_visibility_cluster);

		for (subcluster_index = 0;
			(short)subcluster_index < cluster->subclusters.count && found_count < maximum_count;
			subcluster_index++)
		{
			struct structure_visibility_subcluster *subcluster = TAG_BLOCK_GET_ELEMENT(
				&cluster->subclusters,
				(short)subcluster_index,
				struct structure_visibility_subcluster);
			short rectangle_intersection = bounding_rectangles_intersect(bounding_box, &subcluster->world_bounds);
			short plane_intersection = rectangle_intersection
				? planes_intersect_rectangle(&subcluster->world_bounds, bounding_surface_count, bounding_surfaces)
				: _intersection_out;

			if (plane_intersection)
			{
				long *surface_index_buffer = TAG_BLOCK_GET_ELEMENT(&subcluster->surface_indices, 0, long);
				long surface_list_index;

				for (surface_list_index = 0;
					surface_list_index < subcluster->surface_indices.count;
					surface_index_buffer++, surface_list_index++)
				{
					/* port: (as in structure_visibility_traverse_subclusters) */
					if ((unsigned long)*surface_index_buffer >= (unsigned long)structure->surfaces.count)
					{
						continue;
					}

					if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, *surface_index_buffer) &&
						!BIT_VECTOR_TEST_FLAG(surface_flags, *surface_index_buffer))
					{
						if (found_count >= maximum_count)
						{
							break;
						}

						BIT_VECTOR_SET_FLAG(surface_flags, *surface_index_buffer, TRUE);
						surface_indices[found_count++] = *surface_index_buffer;
					}
				}
			}
		}
	}

	(void)bounding_sphere_center;
	(void)bounding_sphere_radius;
	return found_count;
}

static short structure_visibility_build_surfaces_traverse_leaf(
	long leaf_index,
	real_rectangle3d const *parent_bounds,
	unsigned long *surface_flags,
	long *surface_indices,
	short maximum_count,
	real_point3d const *cull_sphere_center,
	real cull_sphere_radius,
	real_rectangle3d const *cull_bounds,
	short cull_plane_count,
	real_plane3d const *cull_planes,
	short intersection)
{
	short found_count = 0;
	struct structure_bsp *structure = global_structure_bsp_get();
	struct structure_leaf *leaf;
	real_rectangle3d leaf_bounds;

	/* port: a leaf (from the map) that is no leaf, or whose surfaces'
	references are not all references, is not traversed */
	if ((leaf_index & LONG_MAX) >= structure->leaves.count)
	{
		return 0;
	}
	leaf = TAG_BLOCK_GET_ELEMENT(
		&structure->leaves,
		leaf_index & LONG_MAX,
		struct structure_leaf);
	if (leaf->first_surface_reference_index < 0 ||
		leaf->surface_reference_count < 0 ||
		leaf->first_surface_reference_index >
			structure->surface_references.count - leaf->surface_reference_count)
	{
		return 0;
	}

	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2F0,
		intersection);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2F1,
		parent_bounds);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2F2,
		cull_sphere_center);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2F3,
		cull_bounds);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2F4,
		leaf->cluster_index>=0 && leaf->cluster_index<structure->clusters.count);

	dequantize_byte_to_real_rectangle3d(parent_bounds, &leaf->bounds, &leaf_bounds);
	if (intersection != _intersection_in)
	{
		short rectangle_intersection = bounding_rectangles_intersect(&leaf_bounds, cull_bounds);
		short plane_intersection = planes_intersect_rectangle(
			&leaf_bounds,
			cull_plane_count,
			cull_planes);

		intersection = MIN(rectangle_intersection, plane_intersection);
	}

	if (intersection)
	{
		long reference_index;

		for (reference_index = leaf->first_surface_reference_index;
			reference_index < leaf->surface_reference_count + leaf->first_surface_reference_index;
			reference_index++)
		{
			struct structure_visibility_surface_reference *reference = TAG_BLOCK_GET_ELEMENT(
				&structure->surface_references,
				reference_index,
				struct structure_visibility_surface_reference);
			long surface_index = reference->surface_index;

			/* port: (as in structure_visibility_traverse_subclusters) */
			if ((unsigned long)surface_index >= (unsigned long)structure->surfaces.count)
			{
				continue;
			}

			if (BIT_VECTOR_TEST_FLAG(render.environment_surface_flags, surface_index) &&
				!BIT_VECTOR_TEST_FLAG(surface_flags, surface_index))
			{
				if (found_count >= maximum_count)
				{
					return found_count;
				}

				BIT_VECTOR_SET_FLAG(surface_flags, surface_index, TRUE);
				surface_indices[found_count++] = surface_index;
			}
		}
	}

	(void)cull_sphere_radius;
	return found_count;
}

static short structure_visibility_build_surfaces_traverse_node(
	long node_index,
	real_rectangle3d const *parent_bounds,
	unsigned long *surface_flags,
	long *surface_indices,
	short maximum_count,
	real_point3d const *cull_sphere_center,
	real cull_sphere_radius,
	real_rectangle3d const *cull_bounds,
	short cull_plane_count,
	real_plane3d const *cull_planes,
	short intersection,
	short depth)
{
	short found_count = 0;
	struct structure_bsp *structure = global_structure_bsp_get();
	struct collision_bsp *collision = TAG_BLOCK_GET_ELEMENT(
		&structure->collision_bsp,
		0,
		struct collision_bsp);
	real_rectangle3d node_bounds;

	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2AB,
		parent_bounds);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2AC,
		cull_sphere_center);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2AD,
		cull_bounds);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x2AE,
		intersection);

	/* port: a node (from the map) that is no node, or deeper than any bsp
	is (a cycle of nodes would never end), is not traversed */
	if (node_index < 0 ||
		node_index >= structure->nodes.count ||
		node_index >= collision->bsp3d.nodes.count)
	{
		return 0;
	}
	if (depth >= MAXIMUM_STRUCTURE_BSP_TRAVERSAL_DEPTH)
	{
		if (!warned_about_structure_bsp_depth)
		{
			error(_error_silent, "structure bsp is more than %d nodes deep",
				MAXIMUM_STRUCTURE_BSP_TRAVERSAL_DEPTH);
			warned_about_structure_bsp_depth = TRUE;
		}
		return 0;
	}

	dequantize_byte_to_real_rectangle3d(
		parent_bounds,
		TAG_BLOCK_GET_ELEMENT(&structure->nodes, node_index, byte_rectangle3d),
		&node_bounds);
	if (intersection != _intersection_in)
	{
		short rectangle_intersection = bounding_rectangles_intersect(&node_bounds, cull_bounds);
		short plane_intersection = rectangle_intersection
			? planes_intersect_rectangle(&node_bounds, cull_plane_count, cull_planes)
			: _intersection_out;

		if (plane_intersection == _intersection_in)
		{
			cull_plane_count = 0;
		}
		intersection = MIN(rectangle_intersection, plane_intersection);
	}

	if (intersection)
	{
		struct bsp3d_node *node = TAG_BLOCK_GET_ELEMENT(
			&collision->bsp3d.nodes,
			node_index,
			struct bsp3d_node);
		real_plane3d *plane;
		real distance;
		boolean descend_side[2];
		short side;

		/* port: (nor is one whose plane is no plane) */
		if (node->plane_designator < 0 ||
			node->plane_designator >= collision->bsp3d.planes.count)
		{
			return 0;
		}
		plane = TAG_BLOCK_GET_ELEMENT(
			&collision->bsp3d.planes,
			node->plane_designator,
			real_plane3d);
		distance =
			cull_sphere_center->x * plane->n.i +
			cull_sphere_center->y * plane->n.j +
			cull_sphere_center->z * plane->n.k - plane->d;

		descend_side[0] = distance < cull_sphere_radius;
		descend_side[1] = distance > -cull_sphere_radius;
		for (side = 0; side < NUMBEROF(descend_side); side++)
		{
			if (descend_side[side])
			{
				if (!(node->children[side] & LONG_MIN))
				{
					found_count += structure_visibility_build_surfaces_traverse_node(
						node->children[side],
						&node_bounds,
						surface_flags,
						&surface_indices[found_count],
						maximum_count - found_count,
						cull_sphere_center,
						cull_sphere_radius,
						cull_bounds,
						cull_plane_count,
						cull_planes,
						intersection,
						(short)(depth + 1));
				}
				else if (node->children[side] != NONE)
				{
					found_count += structure_visibility_build_surfaces_traverse_leaf(
						node->children[side],
						&node_bounds,
						surface_flags,
						&surface_indices[found_count],
						maximum_count - found_count,
						cull_sphere_center,
						cull_sphere_radius,
						cull_bounds,
						cull_plane_count,
						cull_planes,
						intersection);
				}
			}
		}
	}

	return found_count;
}

short structure_visibility_build_surfaces(
	long *surface_indices,
	short maximum_count,
	real_point3d const *bounding_sphere_center,
	real bounding_sphere_radius,
	real_rectangle3d const *bounding_box,
	short bounding_surface_count,
	real_plane3d const *bounding_surfaces,
	short cluster_count,
	short const *cluster_indices)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	unsigned long surface_flags[MAXIMUM_SURFACES_PER_STRUCTURE / LONG_BITS];
	real_rectangle3d sphere_bounds;
	struct location center_location;
	short found_cluster_indices[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	short found_cluster_count;

	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x265,
		bounding_sphere_center);
	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x266,
		!bounding_surface_count || bounding_surfaces);

	/* port: a bsp (from the map) bigger than the flags hold has no surfaces
	found in it (structure_visibility_compute reports it) */
	if (!structure_bsp_port_verify(structure))
	{
		return 0;
	}

	csmemset(
		surface_flags,
		0,
		BIT_VECTOR_SIZE_IN_BYTES(structure->surfaces.count));

	if (!bounding_box)
	{
		sphere_bounds.x0 = bounding_sphere_center->x - bounding_sphere_radius;
		sphere_bounds.x1 = bounding_sphere_center->x + bounding_sphere_radius;
		sphere_bounds.y0 = bounding_sphere_center->y - bounding_sphere_radius;
		sphere_bounds.y1 = bounding_sphere_center->y + bounding_sphere_radius;
		sphere_bounds.z0 = bounding_sphere_center->z - bounding_sphere_radius;
		sphere_bounds.z1 = bounding_sphere_center->z + bounding_sphere_radius;
		bounding_box = &sphere_bounds;
	}

	if (bounding_sphere_radius < structure_sphere_threshold)
	{
		return structure_visibility_build_surfaces_traverse_node(
			0,
			&structure->world_bounds,
			surface_flags,
			surface_indices,
			maximum_count,
			bounding_sphere_center,
			bounding_sphere_radius,
			bounding_box,
			bounding_surface_count,
			bounding_surfaces,
			_intersection_spanning,
			0);
	}

	if (cluster_indices)
	{
		return structure_visibility_build_surfaces_traverse_clusters(
			surface_indices,
			maximum_count,
			bounding_sphere_center,
			bounding_sphere_radius,
			bounding_box,
			bounding_surface_count,
			bounding_surfaces,
			surface_flags,
			cluster_count,
			cluster_indices);
	}

	scenario_location_from_point(&center_location, bounding_sphere_center);
	if (center_location.cluster_index != NONE)
	{
		found_cluster_count = structure_clusters_in_sphere(
			center_location.cluster_index,
			bounding_sphere_center,
			bounding_sphere_radius,
			MAXIMUM_CLUSTERS_PER_STRUCTURE,
			found_cluster_indices);
		return structure_visibility_build_surfaces_traverse_clusters(
			surface_indices,
			maximum_count,
			bounding_sphere_center,
			bounding_sphere_radius,
			bounding_box,
			bounding_surface_count,
			bounding_surfaces,
			surface_flags,
			found_cluster_count,
			found_cluster_indices);
	}

	return structure_visibility_build_surfaces_traverse_node(
		0,
		&structure->world_bounds,
		surface_flags,
		surface_indices,
		maximum_count,
		bounding_sphere_center,
		bounding_sphere_radius,
		bounding_box,
		bounding_surface_count,
		bounding_surfaces,
		_intersection_spanning,
		0);
}

static void structure_visibility_traverse_cluster(
	short cluster_index,
	struct portal_hull const *visible_region)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct tag_block *clusters = &structure->clusters;
	struct structure_visibility_cluster *cluster = TAG_BLOCK_GET_ELEMENT(
		clusters,
		cluster_index,
		struct structure_visibility_cluster);
	unsigned long *cluster_pvs = structure_bsp_get_cluster_pvs(
		structure,
		(short)render.cluster_index);
	struct rendered_cluster *rendered_cluster;
	long portal_list_index;

	match_assert(
		"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
		0x3EE,
		valid_portal_hull(visible_region));

	/* port: a cluster past the rendered clusters' room is not rendered, nor
	is any past it traversed (render.rendered_clusters is not written past).
	As the clusters being traversed are each rendered, this bounds how deep
	the traversal goes, however a map's portals lead */
	if (!BIT_VECTOR_TEST_FLAG(render.visible_cluster_flags, cluster_index) &&
		render.rendered_cluster_count >= MAXIMUM_RENDERED_CLUSTERS)
	{
		if (!warned_about_rendered_clusters)
		{
			error(_error_silent, "more than %d clusters visible", MAXIMUM_RENDERED_CLUSTERS);
			warned_about_rendered_clusters = TRUE;
		}
		return;
	}

	/* port: counted for structure_visibility_find_clusters_shared */
	portal_traversal.visit_count++;
	BIT_VECTOR_SET_FLAG(
		structure_visibility_globals.visited_cluster_flags,
		cluster_index,
		TRUE);

	if (!BIT_VECTOR_TEST_FLAG(render.visible_cluster_flags, cluster_index))
	{
		match_vassert(
			"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
			0x3F5,
			render.rendered_cluster_count < MAXIMUM_RENDERED_CLUSTERS,
			"raise MAXIMUM_RENDERED_CLUSTERS");
		match_assert(
			"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
			0x3F8,
			cluster_index>=0 && cluster_index<MAXIMUM_CLUSTERS_PER_STRUCTURE);

		structure_visibility_globals.rendered_cluster_indices[cluster_index] = render.rendered_cluster_count++;
		rendered_cluster = rendered_cluster_get(structure_visibility_globals.rendered_cluster_indices[cluster_index]);
		rendered_cluster->cluster_index = cluster_index;
		rendered_cluster->clip_bounds = *global_null_rectangle2d;
	}
	else
	{
		rendered_cluster = rendered_cluster_get(
			structure_visibility_globals.rendered_cluster_indices[cluster_index]);
		match_assert(
			"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
			0x403,
			rendered_cluster->cluster_index==cluster_index);
	}

	BIT_VECTOR_SET_FLAG(render.visible_cluster_flags, cluster_index, TRUE);
	grow_clipping_rectangle_by_portal_hull(
		&rendered_cluster->clip_bounds,
		visible_region);

	{
		real_argb_color const *color;

		if (debug_portals)
		{
			structure_visibility_render_debug_portal_hull(
				visible_region,
				global_real_argb_red,
				0.05f);
		}
		else if (ai_debug_highlight_cluster(cluster_index, &color))
		{
			structure_visibility_render_debug_portal_hull(
				visible_region,
				color,
				0.05f);
		}
	}

	for (portal_list_index = 0;
		(short)portal_list_index < cluster->portal_indices.count;
		portal_list_index++)
	{
		short portal_index = *TAG_BLOCK_GET_ELEMENT(
			&cluster->portal_indices,
			(short)portal_list_index,
			short);
		struct structure_visibility_portal *portal;
		boolean direction;
		short neighbor_cluster_index;

		/* port: a portal (from the map) that is no portal is skipped */
		if (portal_index < 0 || portal_index >= structure->cluster_portals.count)
		{
			continue;
		}
		portal = TAG_BLOCK_GET_ELEMENT(
			&structure->cluster_portals,
			portal_index,
			struct structure_visibility_portal);
		direction = portal->cluster_indices[0] == cluster_index;
		neighbor_cluster_index = portal->cluster_indices[direction];

		if (neighbor_cluster_index >= 0 &&
			neighbor_cluster_index < clusters->count &&
			!BIT_VECTOR_TEST_FLAG(
				structure_visibility_globals.visited_cluster_flags,
				neighbor_cluster_index) &&
			BIT_VECTOR_TEST_FLAG(cluster_pvs, neighbor_cluster_index))
		{
			struct portal_hull portal_hull;
			/* port: remembered (portal_hull_from_portal_cached) */
			short portal_result = portal_hull_from_portal_cached(
				portal_index,
				direction,
				&portal_hull);

			if (portal_result == _portal_hull_from_portal_degenerate)
			{
				structure_visibility_traverse_cluster(
					neighbor_cluster_index,
					visible_region);
			}
			else if (portal_result == _portal_hull_from_portal_succeeded &&
				(render.visible_sky_model ||
					points_within_distance(
						(short)portal->vertices.count,
						(real_point3d const *)portal->vertices.address,
						render.camera.z_far)))
			{
				struct portal_hull clipped_hull;

				clipped_hull.vertex_count = convex_hull2d_intersect(
					visible_region->vertex_count,
					visible_region->vertices,
					portal_hull.vertex_count,
					portal_hull.vertices,
					MAXIMUM_PORTAL_HULL_VERTICES,
					clipped_hull.vertices,
					0.0001f);
				if (clipped_hull.vertex_count > 0)
				{
					structure_visibility_traverse_cluster(
						neighbor_cluster_index,
						&clipped_hull);
				}
				else if (clipped_hull.vertex_count == NONE)
				{
					error(_error_silent, "portal intersection failed.");
					structure_visibility_traverse_cluster(
						neighbor_cluster_index,
						visible_region);
				}
			}
		}
	}

	BIT_VECTOR_SET_FLAG(
		structure_visibility_globals.visited_cluster_flags,
		cluster_index,
		FALSE);

	return;
}

/* port: structure_visibility_traverse_cluster's visit, in a traversal
shared with the worker threads. On the game's thread (task NULL), before
the traversal is shared, a visit is an event, and a cluster at
PORTAL_TRAVERSAL_SPLIT_DEPTH begins a part (a task) instead; in a part, on
any thread, a visit grows the clip bounds of the part's own clusters. As a
traversal from a cluster reads only the clusters visited on the way to it,
the parts may run in any order and at once. FALSE: the traversal cannot be
shared (more clusters reached than are rendered, a portal with too many
vertices, an intersection failed, or no room left) */
static boolean portal_traversal_visit(
	struct portal_traversal_runner *runner,
	struct portal_traversal_task *task,
	short cluster_index,
	struct portal_hull const *visible_region,
	short depth)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct tag_block *clusters = &structure->clusters;
	struct structure_visibility_cluster *cluster = TAG_BLOCK_GET_ELEMENT(
		clusters,
		cluster_index,
		struct structure_visibility_cluster);
	unsigned long *cluster_pvs = structure_bsp_get_cluster_pvs(
		structure,
		(short)render.cluster_index);
	real_rectangle2d *clip_bounds;
	long portal_list_index;

	if (task)
	{
		if (!BIT_VECTOR_TEST_FLAG(runner->reached_cluster_flags, cluster_index))
		{
			if (task->cluster_count >= MAXIMUM_RENDERED_CLUSTERS)
			{
				return FALSE;
			}
			BIT_VECTOR_SET_FLAG(runner->reached_cluster_flags, cluster_index, TRUE);
			runner->cluster_indices[cluster_index] = task->cluster_count;
			task->clusters[task->cluster_count].cluster_index = cluster_index;
			task->clusters[task->cluster_count].clip_bounds = *global_null_rectangle2d;
			task->cluster_count++;
		}
		clip_bounds = &task->clusters[runner->cluster_indices[cluster_index]].clip_bounds;
		task->visit_count++;
	}
	else if (depth < PORTAL_TRAVERSAL_SPLIT_DEPTH)
	{
		struct portal_traversal_event *event;

		if (portal_traversal.event_count >= MAXIMUM_PORTAL_TRAVERSAL_EVENTS)
		{
			return FALSE;
		}
		event = &portal_traversal.events[portal_traversal.event_count++];
		event->task_index = NONE;
		event->cluster_index = cluster_index;
		event->clip_bounds = *global_null_rectangle2d;
		clip_bounds = &event->clip_bounds;
		portal_traversal.visit_count++;
	}
	else
	{
		struct portal_traversal_task *new_task;
		struct portal_traversal_event *event;

		if (portal_traversal.event_count >= MAXIMUM_PORTAL_TRAVERSAL_EVENTS ||
			portal_traversal.task_count >= MAXIMUM_PORTAL_TRAVERSAL_TASKS ||
			visible_region->vertex_count > MAXIMUM_PORTAL_TRAVERSAL_VERTICES - portal_traversal.vertex_count)
		{
			return FALSE;
		}
		new_task = &portal_traversal.tasks[portal_traversal.task_count];
		new_task->cluster_index = cluster_index;
		new_task->vertex_count = visible_region->vertex_count;
		new_task->first_vertex_index = portal_traversal.vertex_count;
		csmemcpy(
			&portal_traversal.vertices[portal_traversal.vertex_count],
			visible_region->vertices,
			visible_region->vertex_count * sizeof(real_point2d));
		portal_traversal.vertex_count += visible_region->vertex_count;
		csmemcpy(
			new_task->visited_cluster_flags,
			runner->visited_cluster_flags,
			sizeof(new_task->visited_cluster_flags));
		event = &portal_traversal.events[portal_traversal.event_count++];
		event->task_index = portal_traversal.task_count++;
		event->cluster_index = cluster_index;
		return TRUE;
	}

	BIT_VECTOR_SET_FLAG(runner->visited_cluster_flags, cluster_index, TRUE);
	grow_clipping_rectangle_by_portal_hull(clip_bounds, visible_region);

	for (portal_list_index = 0;
		(short)portal_list_index < cluster->portal_indices.count;
		portal_list_index++)
	{
		short portal_index = *TAG_BLOCK_GET_ELEMENT(
			&cluster->portal_indices,
			(short)portal_list_index,
			short);
		struct structure_visibility_portal *portal;
		boolean direction;
		short neighbor_cluster_index;

		if (portal_index < 0 || portal_index >= structure->cluster_portals.count)
		{
			continue;
		}
		portal = TAG_BLOCK_GET_ELEMENT(
			&structure->cluster_portals,
			portal_index,
			struct structure_visibility_portal);
		direction = portal->cluster_indices[0] == cluster_index;
		neighbor_cluster_index = portal->cluster_indices[direction];

		if (neighbor_cluster_index >= 0 &&
			neighbor_cluster_index < clusters->count &&
			!BIT_VECTOR_TEST_FLAG(runner->visited_cluster_flags, neighbor_cluster_index) &&
			BIT_VECTOR_TEST_FLAG(cluster_pvs, neighbor_cluster_index))
		{
			struct portal_hull portal_hull;
			short portal_result;
			boolean traversed = TRUE;

			/* (portal_hull_from_points reports it, on the game's thread) */
			if ((short)portal->vertices.count < 0 ||
				(short)portal->vertices.count > MAXIMUM_PORTAL_HULL_VERTICES)
			{
				return FALSE;
			}
			portal_result = portal_hull_from_portal_cached(
				portal_index,
				direction,
				&portal_hull);

			if (portal_result == _portal_hull_from_portal_degenerate)
			{
				traversed = portal_traversal_visit(
					runner,
					task,
					neighbor_cluster_index,
					visible_region,
					depth + 1);
			}
			else if (portal_result == _portal_hull_from_portal_succeeded &&
				(render.visible_sky_model ||
					points_within_distance(
						(short)portal->vertices.count,
						(real_point3d const *)portal->vertices.address,
						render.camera.z_far)))
			{
				struct portal_hull clipped_hull;

				clipped_hull.vertex_count = convex_hull2d_intersect(
					visible_region->vertex_count,
					visible_region->vertices,
					portal_hull.vertex_count,
					portal_hull.vertices,
					MAXIMUM_PORTAL_HULL_VERTICES,
					clipped_hull.vertices,
					0.0001f);
				if (clipped_hull.vertex_count > 0)
				{
					traversed = portal_traversal_visit(
						runner,
						task,
						neighbor_cluster_index,
						&clipped_hull,
						depth + 1);
				}
				else if (clipped_hull.vertex_count == NONE)
				{
					traversed = FALSE;
				}
			}
			if (!traversed)
			{
				return FALSE;
			}
		}
	}

	BIT_VECTOR_SET_FLAG(runner->visited_cluster_flags, cluster_index, FALSE);

	return TRUE;
}

/* port: the parts of a shared traversal not yet taken, run on one thread */
static void portal_traversal_run_tasks(
	struct portal_traversal_runner *runner)
{
	long task_index;

	while ((task_index = __atomic_fetch_add(&portal_traversal.next_task, 1, __ATOMIC_RELAXED)) <
		portal_traversal.task_count)
	{
		struct portal_traversal_task *task = &portal_traversal.tasks[task_index];
		struct portal_hull visible_region;

		visible_region.vertex_count = task->vertex_count;
		csmemcpy(
			visible_region.vertices,
			&portal_traversal.vertices[task->first_vertex_index],
			task->vertex_count * sizeof(real_point2d));
		csmemcpy(
			runner->visited_cluster_flags,
			task->visited_cluster_flags,
			sizeof(runner->visited_cluster_flags));
		csmemset(runner->reached_cluster_flags, 0, sizeof(runner->reached_cluster_flags));
		task->cluster_count = 0;
		task->visit_count = 0;
		if (!portal_traversal_visit(runner, task, task->cluster_index, &visible_region, 0))
		{
			task->cluster_count = NONE;
		}
	}

	return;
}

/* port: a worker thread's job in a shared traversal */
static void portal_traversal_job(
	void)
{
	long runner_index = __atomic_fetch_add(&portal_traversal.next_runner, 1, __ATOMIC_RELAXED);

	if (runner_index < MAXIMUM_PORTAL_TRAVERSAL_RUNNERS)
	{
		portal_traversal_run_tasks(&portal_traversal.runners[runner_index]);
	}

	return;
}

/* port: a cluster a shared traversal reached, written as
structure_visibility_traverse_cluster writes it: rendered when first
reached, its clip bounds grown by the bounds a visit or part grew from none
(the smaller and the larger of each kept, the earlier when they are equal,
as growing them by the vertices keeps them). FALSE: more clusters than are
rendered */
static boolean portal_traversal_merge_cluster(
	short cluster_index,
	real_rectangle2d const *clip_bounds)
{
	struct rendered_cluster *rendered_cluster;

	if (!BIT_VECTOR_TEST_FLAG(render.visible_cluster_flags, cluster_index))
	{
		if (render.rendered_cluster_count >= MAXIMUM_RENDERED_CLUSTERS)
		{
			return FALSE;
		}
		structure_visibility_globals.rendered_cluster_indices[cluster_index] = render.rendered_cluster_count++;
		rendered_cluster = rendered_cluster_get(structure_visibility_globals.rendered_cluster_indices[cluster_index]);
		rendered_cluster->cluster_index = cluster_index;
		rendered_cluster->clip_bounds = *global_null_rectangle2d;
		BIT_VECTOR_SET_FLAG(render.visible_cluster_flags, cluster_index, TRUE);
	}
	else
	{
		rendered_cluster = rendered_cluster_get(
			structure_visibility_globals.rendered_cluster_indices[cluster_index]);
	}

	if (rendered_cluster->clip_bounds.x0 > clip_bounds->x0)
	{
		rendered_cluster->clip_bounds.x0 = clip_bounds->x0;
	}
	if (rendered_cluster->clip_bounds.x1 < clip_bounds->x1)
	{
		rendered_cluster->clip_bounds.x1 = clip_bounds->x1;
	}
	if (rendered_cluster->clip_bounds.y0 > clip_bounds->y0)
	{
		rendered_cluster->clip_bounds.y0 = clip_bounds->y0;
	}
	if (rendered_cluster->clip_bounds.y1 < clip_bounds->y1)
	{
		rendered_cluster->clip_bounds.y1 = clip_bounds->y1;
	}

	return TRUE;
}

/* port: the portal traversal from the camera's cluster, when the last one
from it visited many clusters, shared with the worker threads
(display.interpolation_threads): the visits to PORTAL_TRAVERSAL_SPLIT_DEPTH
on the game's thread, the parts beyond them on every thread, then the
clusters reached written in the order the traversal reaches them. The
clusters, their order and their clip bounds are those of
structure_visibility_traverse_cluster. FALSE: nothing is written, and the
traversal is to run as it was (debug drawing, no worker threads, a light
traversal, or one that cannot be shared) */
static boolean structure_visibility_find_clusters_shared(
	struct portal_hull const *screen_hull)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	short cluster_index = (short)render.cluster_index;
	boolean shared = FALSE;
	long visit_count = 0;

	if (portal_traversal.structure != structure)
	{
		csmemset(portal_traversal.cluster_visit_counts, 0, sizeof(portal_traversal.cluster_visit_counts));
		csmemset(portal_traversal.unshared_cluster_flags, 0, sizeof(portal_traversal.unshared_cluster_flags));
		portal_traversal.structure = structure;
	}
	/* (ai_debug_highlight_cluster draws only when this holds) */
	if (debug_portals ||
		(ai_debug.render_encounter_activeregion && ai_debug.selected_squad_index != NONE) ||
		portal_traversal.cluster_visit_counts[cluster_index] < PORTAL_TRAVERSAL_SHARED_VISITS ||
		BIT_VECTOR_TEST_FLAG(portal_traversal.unshared_cluster_flags, cluster_index) ||
		worker_pool_start(0) == 0)
	{
		return FALSE;
	}

	portal_traversal.task_count = 0;
	portal_traversal.event_count = 0;
	portal_traversal.vertex_count = 0;
	portal_traversal.visit_count = 0;
	csmemset(
		portal_traversal.runners[0].visited_cluster_flags,
		0,
		sizeof(portal_traversal.runners[0].visited_cluster_flags));
	if (portal_traversal_visit(&portal_traversal.runners[0], NULL, cluster_index, screen_hull, 0))
	{
		short event_index;

		portal_traversal.next_task = 0;
		portal_traversal.next_runner = 1;
		if (portal_traversal.task_count > 1)
		{
			worker_pool_begin(portal_traversal_job);
		}
		portal_traversal_run_tasks(&portal_traversal.runners[0]);
		if (portal_traversal.task_count > 1)
		{
			worker_pool_end();
		}

		shared = TRUE;
		visit_count = portal_traversal.visit_count;
		for (event_index = 0; shared && event_index < portal_traversal.event_count; event_index++)
		{
			struct portal_traversal_event const *event = &portal_traversal.events[event_index];

			if (event->task_index == NONE)
			{
				shared = portal_traversal_merge_cluster(event->cluster_index, &event->clip_bounds);
			}
			else
			{
				struct portal_traversal_task const *task = &portal_traversal.tasks[event->task_index];
				short task_cluster_index;

				shared = task->cluster_count != NONE;
				for (task_cluster_index = 0;
					shared && task_cluster_index < task->cluster_count;
					task_cluster_index++)
				{
					shared = portal_traversal_merge_cluster(
						task->clusters[task_cluster_index].cluster_index,
						&task->clusters[task_cluster_index].clip_bounds);
				}
				visit_count += task->visit_count;
			}
		}
	}

	if (!shared)
	{
		csmemset(
			render.visible_cluster_flags,
			0,
			BIT_VECTOR_SIZE_IN_BYTES(structure->clusters.count));
		render.rendered_cluster_count = 0;
		BIT_VECTOR_SET_FLAG(portal_traversal.unshared_cluster_flags, cluster_index, TRUE);
		return FALSE;
	}

	portal_traversal.cluster_visit_counts[cluster_index] =
		(unsigned short)(visit_count < 0xFFFF ? visit_count : 0xFFFF);

	return TRUE;
}

static void structure_visibility_find_clusters(
	void)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	real_rectangle2d projection_bounds;
	unsigned long visited_cluster_flags[MAXIMUM_CLUSTERS_PER_STRUCTURE / LONG_BITS];
	struct portal_hull screen_hull;
	long rendered_cluster_index;

	if (render.cluster_index != NONE)
	{
		render_frustum_get_projection_bounds(&render.frustum, &projection_bounds);
		screen_hull.vertex_count = NUMBER_OF_POINTS_PER_RECTANGLE;
		screen_hull.vertices[0].x = projection_bounds.x0;
		screen_hull.vertices[0].y = projection_bounds.y0;
		screen_hull.vertices[1].x = projection_bounds.x1;
		screen_hull.vertices[1].y = projection_bounds.y0;
		screen_hull.vertices[2].x = projection_bounds.x1;
		screen_hull.vertices[2].y = projection_bounds.y1;
		screen_hull.vertices[3].x = projection_bounds.x0;
		screen_hull.vertices[3].y = projection_bounds.y1;
		structure_visibility_globals.visited_cluster_flags = visited_cluster_flags;
		/* port: the hulls of an earlier traversal are of another camera */
		if (++portal_hull_cache.traversal == 0)
		{
			csmemset(
				portal_hull_cache.entries,
				0,
				sizeof(portal_hull_cache.entries));
			portal_hull_cache.traversal = 1;
		}
		portal_hull_cache.vertex_count = 0;
		csmemset(
			visited_cluster_flags,
			0,
			sizeof(visited_cluster_flags));
		/* port: shared with the worker threads when that is worth it, and
		its visits counted for the next from the same cluster */
		if (!structure_visibility_find_clusters_shared(&screen_hull))
		{
			portal_traversal.visit_count = 0;
			structure_visibility_traverse_cluster(
				(short)render.cluster_index,
				&screen_hull);
			portal_traversal.cluster_visit_counts[render.cluster_index] = (unsigned short)
				(portal_traversal.visit_count < 0xFFFF ? portal_traversal.visit_count : 0xFFFF);
		}

		for (rendered_cluster_index = 0;
			(short)rendered_cluster_index < render.rendered_cluster_count;
			rendered_cluster_index++)
		{
			struct rendered_cluster *rendered_cluster = rendered_cluster_get(
				(short)rendered_cluster_index);
			real_rectangle2d frustum_bounds;

			TAG_BLOCK_GET_ELEMENT(
				&structure->clusters,
				rendered_cluster->cluster_index,
				struct structure_visibility_cluster);
			render_camera_build_clipped_frustum_bounds(
				&render.camera,
				&rendered_cluster->clip_bounds,
				&frustum_bounds);
			render_camera_build_frustum(
				&render.camera,
				&frustum_bounds,
				&rendered_cluster->frustum,
				TRUE);
		}
	}

	return;
}

void structure_visibility_compute(
	void)
{
	struct structure_bsp *structure = global_structure_bsp_get();

	/* port: a bsp (from the map) bigger than the engine's flags and arrays
	of its clusters and surfaces hold, or without its clusters' pvs, has
	nothing of it visible */
	if (!structure_bsp_port_verify(structure))
	{
		if (!warned_about_invalid_structure_bsp)
		{
			error(_error_silent, "structure bsp has %ld clusters and %ld surfaces (maximum %d and %d), or too little cluster data",
				structure->clusters.count, structure->surfaces.count,
				MAXIMUM_CLUSTERS_PER_STRUCTURE, MAXIMUM_SURFACES_PER_STRUCTURE);
			warned_about_invalid_structure_bsp = TRUE;
		}
		csmemset(render.visible_cluster_flags, 0, sizeof(render.visible_cluster_flags));
		render.environment_surface_count = 0;
		render.rendered_cluster_count = 0;
		return;
	}

	profile_enter(render_structure_visibility_portal_traversal);
	if (render.cluster_index != NONE)
	{
		csmemset(
			render.visible_cluster_flags,
			0,
			BIT_VECTOR_SIZE_IN_BYTES(structure->clusters.count));
	}
	else
	{
		csmemset(
			render.visible_cluster_flags,
			-1,
			BIT_VECTOR_SIZE_IN_BYTES(structure->clusters.count));
	}

	render.environment_surface_count = 0;
	csmemset(
		render.environment_surface_flags,
		0,
		BIT_VECTOR_SIZE_IN_BYTES(structure->surfaces.count));
	render.rendered_cluster_count = 0;
	structure_visibility_find_clusters();

	if (structures_use_pvs_for_vs)
	{
		short cluster_index;

		render.rendered_cluster_count = 0;
		csmemcpy(
			render.visible_cluster_flags,
			structure_bsp_get_cluster_pvs(structure, (short)render.cluster_index),
			BIT_VECTOR_SIZE_IN_BYTES(structure->clusters.count));

		for (cluster_index = 0; cluster_index < structure->clusters.count; cluster_index++)
		{
			if (BIT_VECTOR_TEST_FLAG(render.visible_cluster_flags, cluster_index))
			{
				struct rendered_cluster *rendered_cluster;

				/* port: (as in structure_visibility_traverse_cluster) */
				if (render.rendered_cluster_count >= MAXIMUM_RENDERED_CLUSTERS)
				{
					break;
				}

				TAG_BLOCK_GET_ELEMENT(
					&structure->clusters,
					cluster_index,
					struct structure_visibility_cluster);
				match_vassert(
					"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
					0x118,
					render.rendered_cluster_count < MAXIMUM_RENDERED_CLUSTERS,
					"raise MAXIMUM_RENDERED_CLUSTERS");
				match_assert(
					"c:\\halo\\SOURCE\\structures\\structure_visibility.c",
					0x11B,
					cluster_index>=0 && cluster_index<MAXIMUM_CLUSTERS_PER_STRUCTURE);

				structure_visibility_globals.rendered_cluster_indices[cluster_index] = render.rendered_cluster_count++;
				rendered_cluster = rendered_cluster_get(structure_visibility_globals.rendered_cluster_indices[cluster_index]);
				rendered_cluster->cluster_index = cluster_index;
				render_frustum_get_projection_bounds(
					&render.frustum,
					&rendered_cluster->clip_bounds);
			}
		}
	}
	profile_exit(render_structure_visibility_portal_traversal);

	if (TAG_BLOCK_GET_ELEMENT(
		&structure->clusters,
		0,
		struct structure_visibility_cluster)->subclusters.count)
	{
		/* port: retain the original walk when culling is disabled or unavailable */
		if (!structure_culling_traverse(structure))
		{
			structure_visibility_traverse_subclusters(structure);
		}
	}
	else
	{
		if (!warned_about_missing_subclusters)
		{
			if (structure->surfaces.count > 0)
			{
				error(
					_error_silent,
					"### WARNING: this structure_bsp needs to be reimported for new, faster visibility.");
			}

			warned_about_missing_subclusters = TRUE;
		}

		/* port: retain the original walk when culling is disabled or unavailable */
		if (!structure_culling_traverse(structure))
		{
			structure_visibility_traverse_surface_lists(structure);
		}
	}

	/* port: a frame that reaches the structure triangles' limit is said once */
	if (render.environment_surface_count >= MAXIMUM_RENDERED_ENVIRONMENT_SURFACES &&
		!warned_about_environment_surfaces)
	{
		error(_error_silent, "a frame stopped at %d structure triangles",
			MAXIMUM_RENDERED_ENVIRONMENT_SURFACES);
		warned_about_environment_surfaces = TRUE;
	}

	return;
}
