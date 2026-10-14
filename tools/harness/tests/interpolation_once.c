#include "harness.h"
#include <math.h>

/* One object drawn between two ticks (render_interpolation.c), of NODES
nodes: the game's node matrices are nodes[], which a case sets before each
tick. Frames draw it at a fraction of the tick; inverse_transforms counts
the work of comparing the two snapshots' poses. */
#define OBJECT 0x12340002L
#define NODES 3
#define MAXIMUM_INTERPOLATED_OBJECTS 8
#define _object_mask_all 0xFFFFFFFFUL
#define MAXIMUM_LOCAL_PLAYERS 4

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

static real_vector3d const zero_vector;
#define global_zero_vector3d (&zero_vector)
static real_matrix4x3 nodes[NODES];
static struct object_datum object;
static real tick_fraction;
static int inverse_transforms;

static boolean halo_interpolation_enabled(void) { return TRUE; }
static real game_time_get_tick_fraction(void) { return tick_fraction; }
static long player_control_get_unit_index(short local_player_index) { return NONE; }
static struct object_datum *object_get(long index) { CHECK(index == OBJECT, "not the object"); return &object; }
static void object_iterator_new(struct object_iterator *iterator, unsigned long type_flags, byte flags)
{
	iterator->index = NONE;
	iterator->done = FALSE;
}
static void *object_iterator_next(struct object_iterator *iterator)
{
	if (iterator->done)
		return NULL;
	iterator->done = TRUE;
	iterator->index = OBJECT;
	return &object;
}
static real_matrix4x3 *object_get_node_matrices(long index) { CHECK(index == OBJECT, "not the object"); return nodes; }
static real_point3d *matrix4x3_inverse_transform_point(real_matrix4x3 const *matrix, real_point3d const *point,
	real_point3d *result)
{
	real x = point->x - matrix->position.x, y = point->y - matrix->position.y, z = point->z - matrix->position.z;

	inverse_transforms++;
	result->x = (x * matrix->forward.i + y * matrix->forward.j + z * matrix->forward.k) / matrix->scale;
	result->y = (x * matrix->left.i + y * matrix->left.j + z * matrix->left.k) / matrix->scale;
	result->z = (x * matrix->up.i + y * matrix->up.j + z * matrix->up.k) / matrix->scale;
	return result;
}

static struct interpolated_object *interpolated_objects;
static struct interpolated_camera interpolated_cameras[MAXIMUM_LOCAL_PLAYERS];
static long interpolation_tick;
static long interpolation_frame;
static boolean interpolation_rendering;
static real interpolation_fraction = 1.0f;

#include "under_test.inc"

/* a pose: its nodes along x from the root at (x, 0, 0), each turned by angle
about z, with a basis a little longer than one along forward (a rotation, as
basis_is_rotation allows, that a quaternion does not hold exactly) */
static void pose(real x, real angle)
{
	short node_index;

	for (node_index = 0; node_index < NODES; node_index++)
	{
		real_matrix4x3 *node = &nodes[node_index];

		node->scale = 1.0f;
		node->forward.i = 1.003f * cosf(angle);
		node->forward.j = 1.003f * sinf(angle);
		node->forward.k = 0.0f;
		node->left.i = -sinf(angle);
		node->left.j = cosf(angle);
		node->left.k = 0.0f;
		node->up.i = 0.0f;
		node->up.j = 0.0f;
		node->up.k = 1.0f;
		node->position.x = x + 0.1f * node_index;
		node->position.y = 0.05f * node_index;
		node->position.z = 0.1f;
	}
}

static real_matrix4x3 const *frame(real fraction)
{
	real_matrix4x3 const *drawn;

	tick_fraction = fraction;
	render_interpolation_frame_begin();
	drawn = render_interpolation_object_node_matrices(OBJECT);
	render_interpolation_frame_end();
	CHECK(drawn != NULL, "nothing drawn from");
	return drawn;
}

/* what blending the two snapshots gives */
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

static void check_drawn(real_matrix4x3 const *drawn, real_matrix4x3 const *expected, const char *what)
{
	CHECK(memcmp(drawn, expected, sizeof(nodes)) == 0, "%s: drawn x %.9g forward %.9g, expected %.9g %.9g", what,
		drawn[1].position.x, drawn[1].forward.i, expected[1].position.x, expected[1].forward.i);
}

int main(int argc, char **argv)
{
	const char *test = argc > 1 ? argv[1] : "";
	real_matrix4x3 previous[NODES], expected[NODES];
	short node_index;

	object.object.node_matrices.size = (short)sizeof(nodes);
	object.object.first_child_object_index = NONE;
	pose(1.0f, 0.2f);
	render_interpolation_tick();
	if (!strcmp(test, "at_rest"))
	{
		/* not moved since the last tick: drawn as it is, every frame, not
		blended (its basis as the game has it) */
		render_interpolation_tick();
		check_drawn(frame(0.25f), nodes, "first frame");
		check_drawn(frame(0.75f), nodes, "second frame");
		CHECK(inverse_transforms == 0, "its poses compared (%d)", inverse_transforms);
		/* moving again: blended, from the next tick */
		memcpy(previous, nodes, sizeof(nodes));
		pose(1.05f, 0.25f);
		render_interpolation_tick();
		blend(previous, nodes, 0.5f, expected);
		check_drawn(frame(0.5f), expected, "moving again");
	}
	else if (!strcmp(test, "moving"))
	{
		/* blended every frame; its poses compared once a tick */
		memcpy(previous, nodes, sizeof(nodes));
		pose(1.05f, 0.25f);
		render_interpolation_tick();
		blend(previous, nodes, 0.2f, expected);
		check_drawn(frame(0.2f), expected, "first frame");
		blend(previous, nodes, 0.5f, expected);
		check_drawn(frame(0.5f), expected, "second frame");
		blend(previous, nodes, 0.9f, expected);
		check_drawn(frame(0.9f), expected, "third frame");
		CHECK(inverse_transforms == 2 * (NODES - 1), "its poses compared %d times", inverse_transforms);
	}
	else if (!strcmp(test, "snapped"))
	{
		/* moved further than a tick allows: the latest, every frame */
		pose(21.0f, 0.2f);
		render_interpolation_tick();
		check_drawn(frame(0.3f), nodes, "first frame");
		check_drawn(frame(0.6f), nodes, "second frame");
	}
	else if (!strcmp(test, "corrected"))
	{
		/* at rest, then corrected: the snapshots where it would have been,
		the difference drawn, from the next frame */
		real_vector3d offset = { 0.3f, -0.7f, 0.2f };

		render_interpolation_tick();
		frame(0.3f);
		render_interpolation_correct_object(OBJECT, &offset);
		for (node_index = 0; node_index < NODES; node_index++)
		{
			expected[node_index] = nodes[node_index];
			expected[node_index].position.x = (nodes[node_index].position.x - offset.i) + offset.i;
			expected[node_index].position.y = (nodes[node_index].position.y - offset.j) + offset.j;
			expected[node_index].position.z = (nodes[node_index].position.z - offset.k) + offset.k;
		}
		CHECK(memcmp(expected, nodes, sizeof(nodes)) != 0, "the case cannot tell a correction drawn");
		check_drawn(frame(0.6f), expected, "corrected");
	}
	else if (!strcmp(test, "fading"))
	{
		/* at rest, with a correction fading through the tick: drawn anew
		every frame */
		real_vector3d offset = { 0.5f, -0.25f, 0.125f };
		real fractions[2] = { 0.2f, 0.8f };
		struct interpolated_object const *record;
		short index;

		render_interpolation_tick();
		render_interpolation_correct_object(OBJECT, &offset);
		render_interpolation_tick();
		render_interpolation_tick();
		record = &interpolated_objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(OBJECT)];
		CHECK(correction_significant(&record->correction), "no correction left to fade");
		for (index = 0; index < 2; index++)
		{
			real fade = lerp(1.0f, CORRECTION_DECAY, fractions[index]);

			for (node_index = 0; node_index < NODES; node_index++)
			{
				expected[node_index] = nodes[node_index];
				expected[node_index].position.x += record->correction.i * fade;
				expected[node_index].position.y += record->correction.j * fade;
				expected[node_index].position.z += record->correction.k * fade;
			}
			check_drawn(frame(fractions[index]), expected, index ? "second frame" : "first frame");
		}
	}
	else
	{
		fprintf(stderr, "no case %s\n", test);
		return 2;
	}
	return 0;
}
