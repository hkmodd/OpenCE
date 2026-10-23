/*
VR_ARMS.C

The first-person arms in the headset (display.vr, vr.arms "free"). The game
poses its first-person model (both arms, the hands and the weapon, one
animation graph) from a camera, which the headset puts behind the hand
that aims (vr_view.c): the whole model then moves with that hand, the
other arm included, so that the weapon is always held with both. Here the
model's node matrices, once posed, are posed again, arm by arm:

- The shoulders are the player's: under and behind the head, either side
  of a body that faces where the head does (it follows the head once that
  has turned more than VR_ARMS_BODY_TURN from it, and a turn of the stick
  turns it with the view).
- The weapon's hand stays where the game holds the weapon in it, with the
  weapon and the fingers as they are animated (a reload's, a shot's).
- The other hand holds its controller as the weapon's hand holds the
  weapon's grip, mirrored: its place and its palm's way from the weapon's
  hand's against the main controller's grip (OpenXR's grip pose is where a
  held gun's grip is), its fingers as animated. It goes to the weapon, as
  animated, while the weapon is held with both hands (vr_input.c's
  vr_two_handed) and while an animation of its own plays (the weapon
  readied, reloaded, a grenade thrown, a melee: first_person_weapon_off_
  hand_busy), blended there and back over VR_ARMS_BLEND seconds.
- With the weapon put away (the hands bare, vr_view.c) the weapon's hand
  is free too: where it held the weapon, its fingers its controller's.
- The free hand's fingers are the controller's: open at rest, closed as
  its grip is squeezed, the index finger by its trigger (the thumb as
  animated). Each finger bends at its knuckle and its middle joint toward
  the palm (the side the animation curls them to), from straight along the
  hand.
- Each arm reaches from its shoulder to its wrist with its own bones'
  lengths (vr_arm_elbow), its elbow bent down and out. A wrist beyond the
  arm's reach moves the shoulder after it. Each bone is turned the least
  that takes it to its new way, and the forearm then takes half the
  wrist's twist about it, so that the skin between them does not wring.
  The node both arms hang from (the graph's root) goes between the
  shoulders: the skin it holds with the upper arms would otherwise stretch
  from them to the weapon's camera.

The bones are the graph's "upperarm", "forearm" and "wriste" (or "hand")
of each side, " l " and " r ", and the fingers' bases under the wrists
("index", "middle", "pinky"), as every first-person animation graph of the
game names them; a graph without them is drawn as the game poses it.
Only the drawn pose changes: markers on the weapon (its muzzle, its
flashlight) are the weapon's hand's, which keeps its place.
*/

#ifndef HALO_GLES

#include "cseries.h"
#include "vr_arms.h"
#include "vr_math.h"
#include "math/real_math.h"
#include "models/model_animation_definitions.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <string.h>

/* ---------- constants */

/* from the eyes to the neck's pivot, back and down along the head's axes
(metres; an adult's, 1.75 m tall) */
#define VR_ARMS_NECK_BACK 0.08f
#define VR_ARMS_NECK_DOWN 0.12f
/* from the neck to each shoulder: down, back and to the side */
#define VR_ARMS_SHOULDER_DOWN 0.10f
#define VR_ARMS_SHOULDER_BACK 0.03f
#define VR_ARMS_SHOULDER_SIDE 0.18f
/* the head turns this far from the body before the body follows (radians,
35 degrees) */
#define VR_ARMS_BODY_TURN 0.61f
/* the other hand goes to the weapon, or back to its controller, in this
long (seconds) */
#define VR_ARMS_BLEND 0.15f
/* the free hand's fingers: how far closed at rest (of their whole curl),
and how far a closed finger bends at its knuckle and at its middle joint
(radians: 70 and 100 degrees) */
#define VR_ARMS_CURL_RELAXED 0.15f
#define VR_ARMS_CURL_KNUCKLE 1.22f
#define VR_ARMS_CURL_JOINT 1.75f

enum
{
	_vr_arm_upper,
	_vr_arm_lower,
	_vr_arm_wrist,
	NUMBER_OF_VR_ARM_BONES
};

/* the fingers' bases, for the hand's own axes */
enum
{
	_vr_finger_index,
	_vr_finger_middle,
	_vr_finger_pinky,
	NUMBER_OF_VR_FINGERS
};

/* the fingers the controller closes (the thumb stays as animated) */
enum
{
	_vr_bent_index,
	_vr_bent_middle,
	_vr_bent_ring,
	_vr_bent_pinky,
	NUMBER_OF_VR_BENT_FINGERS
};

/* the model's arms: its right holds the weapon (the left hand's model is
the right's mirrored) */
enum
{
	_vr_arm_weapon,
	_vr_arm_off,
	NUMBER_OF_VR_ARMS
};

/* ---------- structures */

/* a node of an animation graph (model_animations.c's) */
struct vr_arms_node
{
	char name[TAG_STRING_LENGTH+1];
	short next_sibling_node_index;
	short first_child_node_index;
	short parent_node_index;
	word pad;
	unsigned long flags;
	real_vector3d base_vector;
	real range;
	long pad1;
};

typedef char verify_vr_arms_node_size[sizeof(struct vr_arms_node) == 0x40 ? 1 : -1];

/* ---------- globals */

static struct
{
	/* the graph the bones were found in, and whether it has them all */
	struct animation_graph *graph;
	boolean found;
	short bones[NUMBER_OF_VR_ARMS][NUMBER_OF_VR_ARM_BONES];
	short fingers[NUMBER_OF_VR_ARMS][NUMBER_OF_VR_FINGERS];
	/* the node both upper arms hang from, or NONE */
	short shoulders;
	/* each arm's fingers that bend: their knuckles, their middle joints and
	their tips (NONE where a graph has not one) */
	short bent[NUMBER_OF_VR_ARMS][NUMBER_OF_VR_BENT_FINGERS][3];

	/* the body's yaw from the heading (LOCAL's) */
	boolean body_valid;
	real body_yaw;
	/* how far the other hand is on the weapon: 0 at its controller, 1 on
	the weapon as animated */
	real off_on_weapon;
} vr_arms;

/* ---------- private code */

static struct vr_arms_node const *vr_arms_node_get(
	struct animation_graph *graph,
	short index)
{
	return TAG_BLOCK_GET_ELEMENT(&graph->nodes, index, struct vr_arms_node);
}

/* the first node named with both `side` and `bone` (under `parent`, unless
NONE), or NONE */
static short vr_arms_find(
	struct animation_graph *graph,
	short count,
	char const *side,
	char const *bone,
	short parent)
{
	short index;

	for (index = 0; index < count; index++)
	{
		struct vr_arms_node const *node = vr_arms_node_get(graph, index);

		if ((parent == NONE || node->parent_node_index == parent) && strstr(node->name, side) &&
			strstr(node->name, bone))
		{
			return index;
		}
	}
	return NONE;
}

static void vr_arms_find_bones(
	struct animation_graph *graph,
	short count)
{
	static char const *const sides[NUMBER_OF_VR_ARMS] = { " r ", " l " };
	static char const *const bones[NUMBER_OF_VR_ARM_BONES] = { "upperarm", "forearm", "wriste" };
	static char const *const fingers[NUMBER_OF_VR_FINGERS] = { "index", "middle", "pinky" };
	short arm, index;

	vr_arms.graph = graph;
	vr_arms.found = TRUE;
	vr_arms.shoulders = NONE;
	for (arm = 0; arm < NUMBER_OF_VR_ARMS; arm++)
	{
		short *chain = vr_arms.bones[arm];

		for (index = 0; index < NUMBER_OF_VR_ARM_BONES; index++)
			chain[index] = vr_arms_find(graph, count, sides[arm], bones[index], NONE);
		if (chain[_vr_arm_wrist] == NONE)
			chain[_vr_arm_wrist] = vr_arms_find(graph, count, sides[arm], "hand", NONE);
		for (index = 0; index < NUMBER_OF_VR_ARM_BONES; index++)
		{
			if (chain[index] == NONE)
				vr_arms.found = FALSE;
		}
		/* (each bone the one before's child: an arm, not three names) */
		if (vr_arms.found && (vr_arms_node_get(graph, chain[_vr_arm_lower])->parent_node_index != chain[_vr_arm_upper] ||
			vr_arms_node_get(graph, chain[_vr_arm_wrist])->parent_node_index != chain[_vr_arm_lower]))
		{
			vr_arms.found = FALSE;
		}
		for (index = 0; index < NUMBER_OF_VR_FINGERS; index++)
		{
			vr_arms.fingers[arm][index] = vr_arms.found ?
				vr_arms_find(graph, count, sides[arm], fingers[index], chain[_vr_arm_wrist]) : NONE;
			if (vr_arms.fingers[arm][index] == NONE)
				vr_arms.found = FALSE;
		}
	}
	for (arm = 0; arm < NUMBER_OF_VR_ARMS; arm++)
	{
		static char const *const bent[NUMBER_OF_VR_BENT_FINGERS] = { "index", "middle", "ring", "pinky" };

		for (index = 0; index < NUMBER_OF_VR_BENT_FINGERS; index++)
		{
			short *joints = vr_arms.bent[arm][index], joint;

			joints[0] = vr_arms.found ? vr_arms_find(graph, count, sides[arm], bent[index], vr_arms.bones[arm][_vr_arm_wrist]) :
				NONE;
			for (joint = 1; joint < 3; joint++)
			{
				short child = joints[joint - 1] == NONE ? NONE :
					vr_arms_node_get(graph, joints[joint - 1])->first_child_node_index;

				joints[joint] = child >= 0 && child < count ? child : NONE;
			}
			if (joints[2] == NONE)
				joints[0] = joints[1] = NONE;
		}
	}
	if (vr_arms.found)
	{
		short parent = vr_arms_node_get(graph, vr_arms.bones[_vr_arm_weapon][_vr_arm_upper])->parent_node_index;

		if (parent != NONE && parent < count &&
			parent == vr_arms_node_get(graph, vr_arms.bones[_vr_arm_off][_vr_arm_upper])->parent_node_index)
		{
			vr_arms.shoulders = parent;
		}
	}
}

static boolean vr_arms_under(
	struct animation_graph *graph,
	short node,
	short root)
{
	short depth;

	for (depth = 0; node != NONE && depth < MAXIMUM_NODES_PER_ANIMATION; depth++)
	{
		if (node == root)
			return TRUE;
		node = vr_arms_node_get(graph, node)->parent_node_index;
	}
	return FALSE;
}

static void vr_arms_point_offset(
	real_point3d const *from,
	real_point3d const *to,
	float out[3])
{
	out[0] = (float)(to->x - from->x);
	out[1] = (float)(to->y - from->y);
	out[2] = (float)(to->z - from->z);
}

static float vr_arms_length(
	float const v[3])
{
	return (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static boolean vr_arms_normalize(
	float v[3])
{
	float length = vr_arms_length(v);

	if (!(length > 1e-6f))
		return FALSE;
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
	return TRUE;
}

static void vr_arms_cross(
	float const a[3],
	float const b[3],
	float out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

/* a frame of axes from a direction and another off it: x along the first,
z across both, y square to them (FALSE when they make none) */
static boolean vr_arms_frame(
	float const along[3],
	float const off[3],
	float axes[3][3])
{
	memcpy(axes[0], along, sizeof(axes[0]));
	if (!vr_arms_normalize(axes[0]))
		return FALSE;
	vr_arms_cross(axes[0], off, axes[2]);
	if (!vr_arms_normalize(axes[2]))
		return FALSE;
	vr_arms_cross(axes[2], axes[0], axes[1]);
	return TRUE;
}

/* the matrix's axes turned by `q` */
static void vr_arms_turn(
	real_matrix4x3 *matrix,
	float const q[4])
{
	float turned[3];

	vr_rotate(q, matrix->forward.n, turned);
	memcpy(matrix->forward.n, turned, sizeof(turned));
	vr_rotate(q, matrix->left.n, turned);
	memcpy(matrix->left.n, turned, sizeof(turned));
	vr_rotate(q, matrix->up.n, turned);
	memcpy(matrix->up.n, turned, sizeof(turned));
}

/* the turn `q` taken `t` of the way from none (the shorter way round) */
static void vr_arms_partial(
	float const q[4],
	float t,
	float out[4])
{
	float sign = q[3] < 0.0f ? -1.0f : 1.0f, length;
	int axis;

	for (axis = 0; axis < 3; axis++)
		out[axis] = q[axis] * sign * t;
	out[3] = (1.0f - t) + q[3] * sign * t;
	length = (float)sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
	for (axis = 0; axis < 4; axis++)
		out[axis] /= length;
}

/* the nodes under `root` (it too) turned by `q` about `pivot` */
static void vr_arms_turn_under(
	real_matrix4x3 *matrices,
	struct animation_graph *graph,
	short count,
	short root,
	float const pivot[3],
	float const q[4])
{
	short index;

	for (index = 0; index < count; index++)
	{
		real_matrix4x3 *matrix = &matrices[index];
		float offset[3], turned[3];
		int axis;

		if (!vr_arms_under(graph, index, root))
			continue;
		for (axis = 0; axis < 3; axis++)
			offset[axis] = (float)matrix->position.n[axis] - pivot[axis];
		vr_rotate(q, offset, turned);
		for (axis = 0; axis < 3; axis++)
			matrix->position.n[axis] = pivot[axis] + turned[axis];
		vr_arms_turn(matrix, q);
	}
}

/* the hand's own axes in the world: from its wrist to its middle finger's
base, and across its knuckles from the index's to the pinky's */
static boolean vr_arms_hand_axes(
	real_matrix4x3 const *matrices,
	short arm,
	float axes[3][3])
{
	float along[3], across[3];

	vr_arms_point_offset(&matrices[vr_arms.bones[arm][_vr_arm_wrist]].position,
		&matrices[vr_arms.fingers[arm][_vr_finger_middle]].position, along);
	vr_arms_point_offset(&matrices[vr_arms.fingers[arm][_vr_finger_index]].position,
		&matrices[vr_arms.fingers[arm][_vr_finger_pinky]].position, across);
	return vr_arms_frame(along, across, axes);
}

/* a grip's axes (forward, left, up), square */
static boolean vr_arms_grip_axes(
	real_vector3d const *forward,
	real_vector3d const *up,
	float axes[3][3])
{
	float left[3];

	/* (vr_arms_frame's z is then forward across left, up, and its y left) */
	vr_arms_cross(up->n, forward->n, left);
	return vr_arms_frame(forward->n, left, axes);
}

static void vr_arms_into(
	float const axes[3][3],
	float const v[3],
	float out[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		out[axis] = axes[axis][0] * v[0] + axes[axis][1] * v[1] + axes[axis][2] * v[2];
}

static void vr_arms_out_of(
	float const axes[3][3],
	float const v[3],
	float out[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		out[axis] = axes[0][axis] * v[0] + axes[1][axis] * v[1] + axes[2][axis] * v[2];
}

/* where the other hand holds its controller, as the weapon's hand holds
the weapon's grip against the main controller, mirrored across the
controller's upright plane: the turn of the other hand from its animated
pose, and its wrist's place (FALSE when the hands have no axes) */
static boolean vr_arms_free_hand(
	real_matrix4x3 const *matrices,
	struct vr_arms_pose const *pose,
	float turn[4],
	float wrist[3])
{
	float main_axes[3][3], off_axes[3][3], hand[3][3], animated[3][3], wanted[3][3], columns[3][3];
	float offset[3], local[3], along[3], across[3];
	int axis, k;

	if (!vr_arms_grip_axes(&pose->main_forward, &pose->main_up, main_axes) ||
		!vr_arms_grip_axes(&pose->off_forward, &pose->off_up, off_axes) ||
		!vr_arms_hand_axes(matrices, _vr_arm_weapon, hand) || !vr_arms_hand_axes(matrices, _vr_arm_off, animated))
	{
		return FALSE;
	}
	/* the weapon's hand against its grip, mirrored (the left axis turned
	over), against the other grip */
	vr_arms_point_offset(&pose->main_grip, &matrices[vr_arms.bones[_vr_arm_weapon][_vr_arm_wrist]].position, offset);
	vr_arms_into(main_axes, offset, local);
	local[1] = -local[1];
	vr_arms_out_of(off_axes, local, offset);
	for (axis = 0; axis < 3; axis++)
		wrist[axis] = (float)pose->off_grip.n[axis] + offset[axis];
	vr_arms_into(main_axes, hand[0], local);
	local[1] = -local[1];
	vr_arms_out_of(off_axes, local, along);
	/* (the knuckles' line, from the index to the pinky, mirrored too: the
	other hand's runs the mirrored way) */
	vr_arms_into(main_axes, hand[1], local);
	local[1] = -local[1];
	vr_arms_out_of(off_axes, local, across);
	if (!vr_arms_frame(along, across, wanted))
		return FALSE;
	/* the turn from the animated hand's axes to the wanted's: its columns
	are where it takes the world's axes */
	for (axis = 0; axis < 3; axis++)
	{
		for (k = 0; k < 3; k++)
		{
			columns[axis][k] = wanted[0][k] * animated[0][axis] + wanted[1][k] * animated[1][axis] +
				wanted[2][k] * animated[2][axis];
		}
	}
	vr_quaternion_from_axes(columns[0], columns[1], columns[2], turn);
	return vr_finite(turn, 4) && vr_finite(wrist, 3);
}

/* the free hand's fingers closed `curl` (0 open, 1 a fist; the index's
`index_curl`), `t` of the way from as they are */
static void vr_arms_fingers(
	real_matrix4x3 *matrices,
	struct animation_graph *graph,
	short count,
	short arm,
	float index_curl,
	float curl,
	float t)
{
	float hand[3][3], axis[3], tip[3], palm;
	short finger;
	int k;

	if (!(t > 0.0f) || !vr_arms_hand_axes(matrices, arm, hand))
		return;
	/* (the palm's side of the hand: where the animation curls the middle
	finger, against the hand's z, which is its back on one hand and its palm
	on the other) */
	finger = vr_arms.bent[arm][_vr_bent_middle][0];
	if (finger == NONE)
		return;
	vr_arms_point_offset(&matrices[finger].position, &matrices[vr_arms.bent[arm][_vr_bent_middle][2]].position, tip);
	palm = tip[0] * hand[2][0] + tip[1] * hand[2][1] + tip[2] * hand[2][2] > 0.0f ? 1.0f : -1.0f;
	/* (a turn about y by a positive angle takes x toward -z) */
	for (k = 0; k < 3; k++)
		axis[k] = -palm * hand[1][k];
	for (finger = 0; finger < NUMBER_OF_VR_BENT_FINGERS; finger++)
	{
		short const *joints = vr_arms.bent[arm][finger];
		float closed = finger == _vr_bent_index ? index_curl : curl, angle = 0.0f;
		short joint;

		if (joints[0] == NONE)
			continue;
		closed = VR_ARMS_CURL_RELAXED + (1.0f - VR_ARMS_CURL_RELAXED) * PIN(closed, 0.0f, 1.0f);
		for (joint = 0; joint < 2; joint++)
		{
			float was[3], now[3], bend[4], turn[4], partial[4], pivot[3];

			angle += closed * (joint ? VR_ARMS_CURL_JOINT : VR_ARMS_CURL_KNUCKLE);
			vr_arms_point_offset(&matrices[joints[joint]].position, &matrices[joints[joint + 1]].position, was);
			if (!vr_arms_normalize(was))
				break;
			for (k = 0; k < 3; k++)
				bend[k] = axis[k] * (float)sin(angle * 0.5f);
			bend[3] = (float)cos(angle * 0.5f);
			vr_rotate(bend, hand[0], now);
			vr_quaternion_between(was, now, turn);
			vr_arms_partial(turn, t, partial);
			memcpy(pivot, matrices[joints[joint]].position.n, sizeof(pivot));
			vr_arms_turn_under(matrices, graph, count, joints[joint], pivot, partial);
		}
	}
}

void vr_arms_body(
	real_vector3d const *head_forward,
	real heading,
	float forward[3])
{
	real head_yaw = vr_yaw_of(head_forward->n, vr_arms.body_valid ? vr_arms.body_yaw + heading : heading);
	real local = vr_wrap_angle(head_yaw - heading), turned;

	if (!vr_arms.body_valid)
	{
		vr_arms.body_yaw = local;
		vr_arms.body_valid = TRUE;
	}
	turned = vr_wrap_angle(local - vr_arms.body_yaw);
	if (turned > VR_ARMS_BODY_TURN)
		vr_arms.body_yaw = vr_wrap_angle(vr_arms.body_yaw + turned - VR_ARMS_BODY_TURN);
	else if (turned < -VR_ARMS_BODY_TURN)
		vr_arms.body_yaw = vr_wrap_angle(vr_arms.body_yaw + turned + VR_ARMS_BODY_TURN);
	forward[0] = (float)cos(vr_arms.body_yaw + heading);
	forward[1] = (float)sin(vr_arms.body_yaw + heading);
	forward[2] = 0.0f;
}

/* one arm from `shoulder` to where its wrist now is: the upper arm and the
forearm turned and moved there, the forearm then taking half of
`hand_turn`'s twist about it (the hand's turn from its animated pose) */
static void vr_arms_reach(
	real_matrix4x3 *matrices,
	real_matrix4x3 const *animated,
	short arm,
	float shoulder[3],
	float const bend[3],
	float const hand_turn[4])
{
	short const *chain = vr_arms.bones[arm];
	real_matrix4x3 *upper = &matrices[chain[_vr_arm_upper]], *lower = &matrices[chain[_vr_arm_lower]];
	float wrist[3], elbow[3], was[3], now[3], turn[4], residual[4], unturn[4], twist[4], upper_length, lower_length;
	float angle;
	int axis;

	vr_arms_point_offset(&animated[chain[_vr_arm_upper]].position, &animated[chain[_vr_arm_lower]].position, was);
	upper_length = vr_arms_length(was);
	vr_arms_point_offset(&animated[chain[_vr_arm_lower]].position, &animated[chain[_vr_arm_wrist]].position, was);
	lower_length = vr_arms_length(was);
	memcpy(wrist, matrices[chain[_vr_arm_wrist]].position.n, sizeof(wrist));
	if (vr_arm_elbow(shoulder, wrist, upper_length, lower_length, bend, elbow) < 0)
		return;

	/* the upper arm: from the shoulder, toward the elbow */
	vr_arms_point_offset(&animated[chain[_vr_arm_upper]].position, &animated[chain[_vr_arm_lower]].position, was);
	for (axis = 0; axis < 3; axis++)
		now[axis] = elbow[axis] - shoulder[axis];
	if (!vr_arms_normalize(was) || !vr_arms_normalize(now))
		return;
	vr_quaternion_between(was, now, turn);
	vr_arms_turn(upper, turn);
	memcpy(upper->position.n, shoulder, sizeof(shoulder[0]) * 3);

	/* the forearm: from the elbow, toward the wrist */
	vr_arms_point_offset(&animated[chain[_vr_arm_lower]].position, &animated[chain[_vr_arm_wrist]].position, was);
	for (axis = 0; axis < 3; axis++)
		now[axis] = wrist[axis] - elbow[axis];
	if (!vr_arms_normalize(was) || !vr_arms_normalize(now))
		return;
	vr_quaternion_between(was, now, turn);
	vr_arms_turn(lower, turn);
	memcpy(lower->position.n, elbow, sizeof(elbow));
	/* (the hand turned other than the forearm did: half of that, about the
	forearm, is the forearm's) */
	unturn[0] = -turn[0];
	unturn[1] = -turn[1];
	unturn[2] = -turn[2];
	unturn[3] = turn[3];
	vr_quaternion_multiply(hand_turn, unturn, residual);
	angle = vr_twist_about(residual, now) * 0.5f;
	for (axis = 0; axis < 3; axis++)
		twist[axis] = now[axis] * (float)sin(angle * 0.5f);
	twist[3] = (float)cos(angle * 0.5f);
	vr_arms_turn(lower, twist);
}

/* ---------- public code */

void vr_arms_reset(
	void)
{
	vr_arms.body_valid = FALSE;
	vr_arms.off_on_weapon = 1.0f;
}

void vr_arms_pose(
	real_matrix4x3 *matrices,
	short count,
	struct animation_graph *graph,
	struct vr_arms_pose const *pose)
{
	static float const none[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	real_matrix4x3 animated[MAXIMUM_NODES_PER_ANIMATION];
	float forward[3], right[3], neck[3], shoulder[3], between[3], bend[3], free_turn[4], free_wrist[3], turn[4];
	float target, step;
	boolean free_hand;
	short arm, index;
	int axis;

	if (!graph || count <= 0 || count > MAXIMUM_NODES_PER_ANIMATION || count > graph->nodes.count)
		return;
	if (graph != vr_arms.graph)
		vr_arms_find_bones(graph, count);
	if (!vr_arms.found)
		return;
	for (arm = 0; arm < NUMBER_OF_VR_ARMS; arm++)
	{
		for (index = 0; index < NUMBER_OF_VR_ARM_BONES; index++)
		{
			if (vr_arms.bones[arm][index] >= count)
				return;
		}
		for (index = 0; index < NUMBER_OF_VR_FINGERS; index++)
		{
			if (vr_arms.fingers[arm][index] >= count)
				return;
		}
	}
	memcpy(animated, matrices, sizeof(*matrices) * count);

	/* the other hand: on the weapon, at its controller, or between */
	target = pose->off_on_weapon || !pose->main_valid || !pose->off_valid ? 1.0f : 0.0f;
	step = pose->seconds > 0.0f && pose->seconds < 0.1f ? pose->seconds / VR_ARMS_BLEND : 0.1f / VR_ARMS_BLEND;
	vr_arms.off_on_weapon = target > vr_arms.off_on_weapon ? MIN(target, vr_arms.off_on_weapon + step) :
		MAX(target, vr_arms.off_on_weapon - step);
	free_hand = vr_arms.off_on_weapon < 1.0f && pose->main_valid && pose->off_valid &&
		vr_arms_free_hand(animated, pose, free_turn, free_wrist);
	if (!free_hand)
		memcpy(free_turn, none, sizeof(free_turn));
	else
	{
		short wrist = vr_arms.bones[_vr_arm_off][_vr_arm_wrist];
		real_point3d pivot = animated[wrist].position;
		float place[3];

		vr_arms_partial(free_turn, 1.0f - vr_arms.off_on_weapon, turn);
		memcpy(free_turn, turn, sizeof(turn));
		for (axis = 0; axis < 3; axis++)
			place[axis] = free_wrist[axis] + ((float)pivot.n[axis] - free_wrist[axis]) * vr_arms.off_on_weapon;
		/* (the hand, its fingers and what it holds, turned about the wrist and
		moved with it) */
		for (index = 0; index < count; index++)
		{
			real_matrix4x3 *matrix = &matrices[index];
			float offset[3], turned[3];

			if (!vr_arms_under(graph, index, wrist))
				continue;
			vr_arms_point_offset(&pivot, &animated[index].position, offset);
			vr_rotate(free_turn, offset, turned);
			for (axis = 0; axis < 3; axis++)
				matrix->position.n[axis] = place[axis] + turned[axis];
			vr_arms_turn(matrix, free_turn);
		}
		vr_arms_fingers(matrices, graph, count, _vr_arm_off, pose->off_trigger, pose->off_squeeze,
			1.0f - vr_arms.off_on_weapon);
	}
	if (pose->main_bare)
		vr_arms_fingers(matrices, graph, count, _vr_arm_weapon, pose->main_trigger, pose->main_squeeze, 1.0f);

	/* the shoulders, either side of the neck */
	for (axis = 0; axis < 3; axis++)
		forward[axis] = (float)pose->body_forward.n[axis];
	right[0] = forward[1];
	right[1] = -forward[0];
	right[2] = 0.0f;
	for (axis = 0; axis < 3; axis++)
	{
		neck[axis] = (float)pose->head.n[axis] -
			((float)pose->head_forward.n[axis] * VR_ARMS_NECK_BACK + (float)pose->head_up.n[axis] * VR_ARMS_NECK_DOWN) *
			VR_UNITS_PER_METRE;
	}
	for (axis = 0; axis < 3; axis++)
		between[axis] = neck[axis] - forward[axis] * VR_ARMS_SHOULDER_BACK * VR_UNITS_PER_METRE;
	between[2] -= VR_ARMS_SHOULDER_DOWN * VR_UNITS_PER_METRE;
	if (vr_arms.shoulders != NONE)
		memcpy(matrices[vr_arms.shoulders].position.n, between, sizeof(between));
	for (arm = 0; arm < NUMBER_OF_VR_ARMS; arm++)
	{
		/* (the weapon's arm on the weapon's hand's side) */
		float side = (arm == _vr_arm_weapon) != (pose->main_left != FALSE) ? 1.0f : -1.0f;

		for (axis = 0; axis < 3; axis++)
		{
			shoulder[axis] = neck[axis] + (right[axis] * side * VR_ARMS_SHOULDER_SIDE - forward[axis] *
				VR_ARMS_SHOULDER_BACK) * VR_UNITS_PER_METRE;
			/* (the elbow down, out to the side and a little back) */
			bend[axis] = right[axis] * side * 0.7f - forward[axis] * 0.2f;
		}
		shoulder[2] -= VR_ARMS_SHOULDER_DOWN * VR_UNITS_PER_METRE;
		bend[2] -= 1.0f;
		vr_arms_reach(matrices, animated, arm, shoulder, bend, arm == _vr_arm_off ? free_turn : none);
	}
}

#endif
