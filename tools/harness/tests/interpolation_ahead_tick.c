#include "harness.h"
#include <math.h>

/* The frames between two ticks draw a blend of them (render_interpolation.c).
With display.tick_slices the next tick runs in pieces between those frames,
begun ahead of its time or left under way: what the frames draw from must be
as the tick drawn left it, not as the tick under way leaves it. */
#define MAXIMUM_LOCAL_PLAYERS 4
#define MAXIMUM_INTERPOLATED_OBJECTS 8
struct observer_result { real_point3d position; real_vector3d forward, up; real field_of_view; };
#include "types.inc"

struct render_camera { real_point3d position; real_vector3d forward, up; };
static real_vector3d const zero_vector;
static real_vector3d const *const global_zero_vector3d = &zero_vector;

static void cross_product3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *result)
{
	result->i = a->j * b->k - a->k * b->j;
	result->j = a->k * b->i - a->i * b->k;
	result->k = a->i * b->j - a->j * b->i;
}
static void set_real_point3d(real_point3d *point, real x, real y, real z) { point->x = x; point->y = y; point->z = z; }

/* the world: objects of one node, by absolute index */
#define _object_mask_all 0xFFFF
#define WORLD_OBJECTS 4
struct object_block { short size; };
struct object_datum
{
	struct { struct object_block node_matrices; long first_child_object_index, next_object_index; } object;
};
struct object_iterator { long index; short next; };
static struct { long index; boolean exists; struct object_datum datum; real_matrix4x3 node; } world[WORLD_OBJECTS];
static boolean in_flight;

static boolean halo_interpolation_enabled(void) { return TRUE; }
static boolean tick_slices_in_flight(void) { return in_flight; }
static long player_control_get_unit_index(short local_player_index) { return NONE; }
static void object_iterator_new(struct object_iterator *iterator, long mask, long flags) { iterator->next = 0; iterator->index = NONE; }
static struct object_datum *object_iterator_next(struct object_iterator *iterator)
{
	while (iterator->next < WORLD_OBJECTS)
	{
		short slot = iterator->next++;

		if (world[slot].exists)
		{
			iterator->index = world[slot].index;
			return &world[slot].datum;
		}
	}
	return NULL;
}
static short world_slot(long object_index)
{
	short slot = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	CHECK(slot < WORLD_OBJECTS && world[slot].exists && world[slot].index == object_index, "no object %08lx", object_index);
	return slot;
}
static struct object_datum *object_get(long object_index) { return &world[world_slot(object_index)].datum; }
/* (as the tick leaves them: the tick takes its snapshots outside a frame) */
static real_matrix4x3 *object_get_node_matrices(long object_index) { return &world[world_slot(object_index)].node; }

static struct interpolated_object *interpolated_objects;
static struct interpolated_camera interpolated_cameras[MAXIMUM_LOCAL_PLAYERS];
static struct interpolated_first_person interpolated_first_person[MAXIMUM_LOCAL_PLAYERS];
static long interpolation_tick;
static long first_person_updated_tick;
static long interpolation_frame;
static boolean interpolation_rendering;
static real interpolation_fraction = 1.0f;

#include "under_test.inc"

#define EPSILON 1e-4f

static void identity(real_matrix4x3 *matrix)
{
	memset(matrix, 0, sizeof(*matrix));
	matrix->scale = 1.f;
	matrix->forward.i = 1.f;
	matrix->left.j = 1.f;
	matrix->up.k = 1.f;
}

/* ---------- the first-person weapon */

/* posed from a camera turned and away from the origin, its nodes moving
0.1 world units a tick (short of a snap in two) relative to it, the second
turning; another weapon is half a world unit away */
#define FIRST_PERSON_NODES 3
static real_matrix4x3 camera_matrix;
static struct render_camera camera;
static long animation_tick; /* the tick the weapons' animation has run in, as the game's state has it */
static short weapon;
static short node_count = 2;

static void first_person_pose(real animation, short pose_weapon, real_matrix4x3 *nodes)
{
	real angle = 0.05f * animation;

	identity(&nodes[0]);
	set_real_point3d(&nodes[0].position, 0.3f + 0.1f * animation, 0.05f + 0.5f * pose_weapon, -0.1f);
	identity(&nodes[1]);
	nodes[1].forward.i = (real)cos(angle);
	nodes[1].forward.j = (real)sin(angle);
	nodes[1].left.i = -(real)sin(angle);
	nodes[1].left.j = (real)cos(angle);
	set_real_point3d(&nodes[1].position, 0.2f, 0.02f * animation + 0.5f * pose_weapon, 0.f);
	identity(&nodes[2]);
	set_real_point3d(&nodes[2].position, 0.1f, 0.f, 0.05f);
}

static void first_person_begin(void)
{
	set_real_point3d(&camera.position, 3.f, -2.f, 1.f);
	camera.forward.i = 0.f;
	camera.forward.j = 1.f;
	camera.forward.k = 0.f;
	camera.up.i = 0.f;
	camera.up.j = 0.f;
	camera.up.k = 1.f;
	matrix4x3_from_point_and_vectors(&camera_matrix, &camera.position, &camera.forward, &camera.up);
}

/* a tick runs first_person_weapons_update (game.c) */
static void tick_animates(void)
{
	animation_tick++;
	render_interpolation_first_person_updated();
}

/* the clock takes a tick that has ended (game_time.c) */
static void tick_taken(void)
{
	in_flight = FALSE;
	render_interpolation_tick();
}

/* a frame: the nodes posed from the game's state, then drawn; the position
of each drawn, relative to the camera */
static void frame(real fraction, real_matrix4x3 *drawn)
{
	real_matrix4x3 nodes[FIRST_PERSON_NODES];
	real_matrix4x3 inverse_camera;
	short node_index;

	first_person_pose((real)animation_tick, weapon, drawn);
	for (node_index = 0; node_index < node_count; node_index++)
		matrix4x3_multiply(&camera_matrix, &drawn[node_index], &nodes[node_index]);
	interpolation_rendering = TRUE;
	interpolation_frame++;
	interpolation_fraction = fraction;
	render_interpolation_first_person(0, nodes, node_count, &camera);
	interpolation_rendering = FALSE;
	matrix4x3_inverse(&camera_matrix, &inverse_camera);
	for (node_index = 0; node_index < node_count; node_index++)
		matrix4x3_multiply(&inverse_camera, &nodes[node_index], &drawn[node_index]);
}

/* the frame draws the pose of the animation at a time (in ticks) */
static void frame_draws(real fraction, real animation, short pose_weapon)
{
	real_matrix4x3 drawn[FIRST_PERSON_NODES];
	real_matrix4x3 expected[FIRST_PERSON_NODES];
	short node_index;

	frame(fraction, drawn);
	first_person_pose(animation, pose_weapon, expected);
	for (node_index = 0; node_index < node_count; node_index++)
	{
		CHECK(sqrt(distance_squared(&drawn[node_index].position, &expected[node_index].position)) < EPSILON,
			"tick %ld at %.2f: node %d drawn at (%.4f %.4f %.4f), the pose of the animation at %.2f is (%.4f %.4f %.4f)",
			interpolation_tick, fraction, node_index, drawn[node_index].position.x, drawn[node_index].position.y,
			drawn[node_index].position.z, animation, expected[node_index].position.x, expected[node_index].position.y,
			expected[node_index].position.z);
	}
}

/* the first node's distance along the way the animation moves it */
static real frame_drawn_along(real fraction)
{
	real_matrix4x3 drawn[FIRST_PERSON_NODES];

	frame(fraction, drawn);
	return (drawn[0].position.x - 0.3f) / 0.1f;
}

static void first_person_whole(void)
{
	long tick;
	int index;
	static real const fractions[] = { 0.f, 0.25f, 0.5f, 0.75f };

	first_person_begin();
	tick_animates();
	tick_taken();
	/* (no pose before it: drawn as it stands) */
	frame_draws(0.5f, 1.f, 0);
	for (tick = 2; tick <= 6; tick++)
	{
		tick_animates();
		tick_taken();
		for (index = 0; index < 4; index++)
			frame_draws(fractions[index], (real)(tick - 1) + fractions[index], 0);
	}
}

/* each tick begun ahead of its time, after three of the frames of the
tick before */
static void first_person_ahead(void)
{
	long tick;

	first_person_begin();
	tick_animates();
	tick_taken();
	frame_draws(0.5f, 1.f, 0);
	tick_animates();
	tick_taken();
	for (tick = 2; tick <= 7; tick++)
	{
		frame_draws(0.f, (real)(tick - 1), 0);
		frame_draws(0.3f, (real)(tick - 1) + 0.3f, 0);
		frame_draws(0.6f, (real)(tick - 1) + 0.6f, 0);
		in_flight = TRUE;
		tick_animates();
		frame_draws(0.8f, (real)(tick - 1) + 0.8f, 0);
		frame_draws(0.95f, (real)(tick - 1) + 0.95f, 0);
		tick_taken();
	}
}

/* the tick after one was begun before any frame drew that one: its pose is
the latest there is, drawn toward and then held, never back */
static void first_person_ahead_before_frame(void)
{
	long tick;
	real along = 0.f;
	int index;
	static real const fractions[] = { 0.25f, 0.5f, 0.75f };

	first_person_begin();
	for (tick = 1; tick <= 3; tick++)
	{
		tick_animates();
		tick_taken();
		frame_draws(0.5f, tick == 1 ? 1.f : (real)(tick - 1) + 0.5f, 0);
	}
	/* (the last frame drew tick 3's pose as the latest) */
	frame_draws(1.f, 3.f, 0);
	tick_animates();
	tick_taken();
	in_flight = TRUE;
	tick_animates();
	for (index = 0; index < 3; index++)
	{
		real drawn = frame_drawn_along(fractions[index]);

		CHECK(drawn > 3.f + 0.5f * fractions[index] && drawn <= 5.f + EPSILON && drawn >= along - EPSILON,
			"tick 4 at %.2f: drawn at %.3f of the animation (last %.3f)", fractions[index], drawn, along);
		along = drawn;
	}
	tick_taken();
	for (index = 0; index < 3; index++)
	{
		real drawn = frame_drawn_along(fractions[index]);

		CHECK(drawn <= 5.f + EPSILON && drawn >= along - EPSILON,
			"tick 5 at %.2f: drawn at %.3f of the animation (last %.3f)", fractions[index], drawn, along);
		along = drawn;
	}
	tick_animates();
	tick_taken();
	frame_draws(0.5f, 5.5f, 0);
}

/* the tick under way puts another weapon in the hands: drawn as it stands
(as many nodes, or more) until the tick is taken, then blended from */
static void first_person_weapon_changed(short changed_node_count, short changed_weapon)
{
	first_person_begin();
	tick_animates();
	tick_taken();
	frame_draws(0.5f, 1.f, 0);
	tick_animates();
	tick_taken();
	frame_draws(0.2f, 1.2f, 0);
	in_flight = TRUE;
	weapon = changed_weapon;
	node_count = changed_node_count;
	tick_animates();
	frame_draws(0.5f, 3.f, changed_weapon);
	frame_draws(0.7f, 3.f, changed_weapon);
	tick_taken();
	frame_draws(0.3f, 3.f, changed_weapon);
	tick_animates();
	tick_taken();
	frame_draws(0.5f, 3.5f, changed_weapon);
}

/* ---------- an object first taken on the tick drawn */

/* one node, moving a world unit a tick; another object had its index, a
little behind it */
#define OBJECT 0x00020001L
#define EARLIER_OBJECT 0x00010001L

static void object_place(long object_index, real x)
{
	short slot = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	world[slot].index = object_index;
	world[slot].exists = TRUE;
	world[slot].datum.object.node_matrices.size = (short)sizeof(real_matrix4x3);
	world[slot].datum.object.first_child_object_index = NONE;
	world[slot].datum.object.next_object_index = NONE;
	identity(&world[slot].node);
	world[slot].node.position.x = x;
}

/* the object as a frame draws it: the pose between the last two ticks, or
as it stands (objects.c) */
static void object_draws(real fraction, real expected)
{
	real_matrix4x3 const *interpolated;
	real drawn;

	interpolation_rendering = TRUE;
	interpolation_frame++;
	interpolation_fraction = fraction;
	interpolated = render_interpolation_object_node_matrices(OBJECT);
	drawn = interpolated ? interpolated[0].position.x : object_get_node_matrices(OBJECT)[0].position.x;
	interpolation_rendering = FALSE;
	CHECK(fabs(drawn - expected) < EPSILON, "tick %ld at %.2f%s: drawn at %.4f, not %.4f", interpolation_tick, fraction,
		in_flight ? " (the next under way)" : "", drawn, expected);
}

/* the earlier object for two ticks, then this one made on the third */
static void object_made(void)
{
	object_place(EARLIER_OBJECT, 12.4f);
	tick_taken();
	object_place(EARLIER_OBJECT, 12.5f);
	tick_taken();
	object_place(OBJECT, 13.f);
	tick_taken();
}

static void new_object_whole(void)
{
	object_made();
	object_draws(0.f, 13.f);
	object_draws(0.6f, 13.f);
	object_place(OBJECT, 14.f);
	tick_taken();
	object_draws(0.25f, 13.25f);
}

static void new_object_ahead(void)
{
	object_made();
	object_draws(0.2f, 13.f);
	/* the next tick begun ahead of its time, the object moved in it */
	in_flight = TRUE;
	object_place(OBJECT, 14.f);
	object_draws(0.6f, 13.f);
	object_draws(0.9f, 13.f);
	tick_taken();
	object_draws(0.1f, 13.1f);
	object_draws(0.5f, 13.5f);
}

/* moved by the netcode before the next tick is begun: where it stands,
under way or not (render_interpolation_correct_object) */
static void new_object_corrected_ahead(void)
{
	real_vector3d offset = { 0.5f, 0.f, 0.f };

	object_made();
	object_place(OBJECT, 12.5f);
	render_interpolation_correct_object(OBJECT, &offset);
	object_draws(0.2f, 12.5f);
	in_flight = TRUE;
	object_place(OBJECT, 13.5f);
	object_draws(0.6f, 12.5f);
}

int main(int argc, char **argv)
{
	char const *case_name = argv[1];

	CASE("first-person-whole")
		first_person_whole();
	CASE("first-person-ahead")
		first_person_ahead();
	CASE("first-person-ahead-before-frame")
		first_person_ahead_before_frame();
	CASE("first-person-weapon-changed")
		first_person_weapon_changed(2, 1);
	CASE("first-person-weapon-more-nodes")
		first_person_weapon_changed(3, 0);
	CASE("new-object-whole")
		new_object_whole();
	CASE("new-object-ahead")
		new_object_ahead();
	CASE("new-object-corrected-ahead")
		new_object_corrected_ahead();
	return 0;
}
