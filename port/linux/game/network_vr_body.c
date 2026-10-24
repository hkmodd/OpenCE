/* The other headsets' players' bodies in a VR server's game, as their
headsets pose them (network_vr.c keeps each player's latest pose: the head's
and the hands' places from the unit's and their axes). Only the drawing
changes: as a frame is drawn, a copy of the body's pose (blended between the
ticks, render_interpolation.c) is posed again, and the body's hit boxes,
markers and shots stay the game's.

- The head turns from where the game has it look (the unit's looking
  vector, which it is animated to) to where the headset looks, its roll
  included; a head turned further than NETWORK_VR_BODY_HEAD_TURN from it is
  left as animated.
- Each arm reaches from its upper arm's place to its wrist, a little behind
  the controller's grip (NETWORK_VR_BODY_WRIST_BACK), with the arm's own
  bones' lengths, its elbow bent down, out and back; a grip beyond the
  arm's reach is reached for, the arm straight. Each bone is turned the
  least that takes it to its new way. The hand turns from the way the game
  aims (the unit's aiming vector, which the weapon's hand is animated to)
  to the way its controller points, so that it holds the weapon as the
  game's hand holds it. The model's right arm is the weapon's hand's,
  whichever hand that is.
- The nodes under a node posed again (the fingers) move with it, and so
  does what the body holds at a node (the weapon, at the hand).

The bones are the model's "head", and "upperarm", "forearm" and "hand" of
each side (" l ", " r "), as the game's characters name them ("bip01 l
upperarm"); a model without them is drawn as the game poses it. */
#include "cseries.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "units/units.h"
#include "network_vr.h"
#include "network_vr_body.h"
#include <string.h>

/* port/linux/game/render_interpolation.c */
real_matrix4x3 *render_interpolation_object_node_matrices(long object_index);

/* the bodies and what they hold posed in one frame, and their nodes */
#define NETWORK_VR_BODY_OBJECTS 32
#define NETWORK_VR_BODY_NODES 64
/* the most a head is turned from where the game has it look (cosine: 75
degrees) */
#define NETWORK_VR_BODY_HEAD_TURN 0.26f
/* the wrist behind the controller's grip, along the way it points (world
units: 8 cm) */
#define NETWORK_VR_BODY_WRIST_BACK 0.027f
/* an arm straight: of its bones' lengths */
#define NETWORK_VR_BODY_STRAIGHT 0.995f

enum
{
	_network_vr_body_left,
	_network_vr_body_right,
	NUMBER_OF_NETWORK_VR_BODY_SIDES
};

/* a model's bones, found once a model (NONE: none) */
struct network_vr_body_bones
{
	long model_index;
	boolean valid;
	short head;
	short upperarm[NUMBER_OF_NETWORK_VR_BODY_SIDES];
	short forearm[NUMBER_OF_NETWORK_VR_BODY_SIDES];
	short hand[NUMBER_OF_NETWORK_VR_BODY_SIDES];
};

/* an object posed in a frame: its nodes as the game posed them (a body's:
what it holds moves by them) and as drawn */
struct network_vr_body_object
{
	long object_index;
	long frame;
	short node_count;
	boolean body;
	real_matrix4x3 game[NETWORK_VR_BODY_NODES];
	real_matrix4x3 nodes[NETWORK_VR_BODY_NODES];
};

static struct network_vr_body_bones network_vr_body_bones_found = { NONE };
static struct network_vr_body_object network_vr_body_objects[NETWORK_VR_BODY_OBJECTS];

/* whether a node's name has part in it, whatever its case */
static boolean network_vr_body_name_has(
	char const *name,
	char const *part)
{
	char lower[TAG_STRING_LENGTH + 1];
	short index;

	for (index = 0; index < TAG_STRING_LENGTH && name[index]; index++)
		lower[index] = (char)(name[index] >= 'A' && name[index] <= 'Z' ? name[index] - 'A' + 'a' : name[index]);
	lower[index] = 0;
	return strstr(lower, part) != NULL;
}

/* a model's bones: each found once, each arm's in order down it (a bone
after its parent: the nodes are posed down the model in their order) */
static struct network_vr_body_bones const *network_vr_body_bones(
	long model_index,
	short node_count)
{
	static char const *const sides[NUMBER_OF_NETWORK_VR_BODY_SIDES][3] = {
		{ " l upperarm", " l forearm", " l hand" },
		{ " r upperarm", " r forearm", " r hand" } };
	struct network_vr_body_bones *bones = &network_vr_body_bones_found;
	struct model const *model;
	short index, side, bone;

	if (bones->model_index == model_index)
		return bones->valid ? bones : NULL;
	bones->model_index = model_index;
	bones->valid = FALSE;
	model = model_definition_get(model_index);
	if (!model || model->nodes.count < node_count)
		return NULL;
	bones->head = NONE;
	for (side = 0; side < NUMBER_OF_NETWORK_VR_BODY_SIDES; side++)
		bones->upperarm[side] = bones->forearm[side] = bones->hand[side] = NONE;
	for (index = 0; index < node_count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);

		/* (a parent after its node: the copy is posed down the model in
		order, which this one's is not) */
		if (node->parent_node_index >= index)
			return NULL;
		if (bones->head == NONE && network_vr_body_name_has(node->name, " head"))
			bones->head = index;
		for (side = 0; side < NUMBER_OF_NETWORK_VR_BODY_SIDES; side++)
		{
			for (bone = 0; bone < 3; bone++)
			{
				short *found = bone == 0 ? &bones->upperarm[side] : bone == 1 ? &bones->forearm[side] :
					&bones->hand[side];

				if (*found == NONE && network_vr_body_name_has(node->name, sides[side][bone]))
					*found = index;
			}
		}
	}
	if (bones->head == NONE)
		return NULL;
	for (side = 0; side < NUMBER_OF_NETWORK_VR_BODY_SIDES; side++)
	{
		if (bones->upperarm[side] == NONE || bones->forearm[side] <= bones->upperarm[side] ||
			bones->hand[side] <= bones->forearm[side])
		{
			return NULL;
		}
	}
	bones->valid = TRUE;
	return bones;
}

/* a frame from a forward and an up (made square to it): FALSE for none */
static boolean network_vr_body_frame(
	real_vector3d const *forward,
	real_vector3d const *up,
	real_matrix4x3 *frame)
{
	frame->scale = 1.0f;
	frame->forward = *forward;
	if (normalize3d(&frame->forward) <= 0.0f)
		return FALSE;
	cross_product3d(up, &frame->forward, &frame->left);
	if (normalize3d(&frame->left) <= 0.0f)
		return FALSE;
	cross_product3d(&frame->forward, &frame->left, &frame->up);
	frame->position.x = frame->position.y = frame->position.z = 0.0f;
	return TRUE;
}

/* a vector turned as from one frame to another */
static void network_vr_body_turn_frames(
	real_matrix4x3 const *from,
	real_matrix4x3 const *to,
	real_vector3d const *vector,
	real_vector3d *result)
{
	real forward = dot_product3d(vector, &from->forward);
	real left = dot_product3d(vector, &from->left);
	real up = dot_product3d(vector, &from->up);

	result->i = to->forward.i * forward + to->left.i * left + to->up.i * up;
	result->j = to->forward.j * forward + to->left.j * left + to->up.j * up;
	result->k = to->forward.k * forward + to->left.k * left + to->up.k * up;
}

/* a node's axes turned as from one frame to another, about its place */
static void network_vr_body_turn_node(
	real_matrix4x3 const *from,
	real_matrix4x3 const *to,
	real_matrix4x3 *node)
{
	real_vector3d axis;

	network_vr_body_turn_frames(from, to, &node->forward, &axis);
	node->forward = axis;
	network_vr_body_turn_frames(from, to, &node->left, &axis);
	node->left = axis;
	network_vr_body_turn_frames(from, to, &node->up, &axis);
	node->up = axis;
}

/* the least turn that takes one way to another (unit vectors), as a pair
of frames to turn by: FALSE for none (the same way, or the opposite) */
static boolean network_vr_body_least_turn(
	real_vector3d const *from_way,
	real_vector3d const *to_way,
	real_matrix4x3 *from,
	real_matrix4x3 *to)
{
	real_vector3d axis;

	cross_product3d(from_way, to_way, &axis);
	if (normalize3d(&axis) <= 0.0f)
		return FALSE;
	/* (each way's frame about the axis they turn about: the one is the
	other turned) */
	return network_vr_body_frame(from_way, &axis, from) && network_vr_body_frame(to_way, &axis, to);
}

/* the way from one point to another, and how far (0: none) */
static real network_vr_body_way(
	real_point3d const *from,
	real_point3d const *to,
	real_vector3d *way)
{
	vector_from_points3d(from, to, way);
	return normalize3d(way);
}

/* an arm posed to reach its wrist's place, its hand turned from the game's
aim to its controller's */
static void network_vr_body_arm(
	real_matrix4x3 const *game,
	real_matrix4x3 *nodes,
	short upperarm,
	short forearm,
	short hand,
	real_point3d const *grip,
	real_vector3d const *grip_forward,
	real_vector3d const *grip_up,
	real_matrix4x3 const *aim,
	real_vector3d const *outward)
{
	real_point3d const *shoulder = &game[upperarm].position;
	real_point3d wrist, elbow;
	real_vector3d reach, upper_way, fore_way, way, pole;
	real_matrix4x3 from, to, controller;
	real upper, fore, distance, along, height;

	if (!network_vr_body_frame(grip_forward, grip_up, &controller))
		return;
	upper = network_vr_body_way(shoulder, &game[forearm].position, &upper_way);
	fore = network_vr_body_way(&game[forearm].position, &game[hand].position, &fore_way);
	point_from_line3d(grip, &controller.forward, -NETWORK_VR_BODY_WRIST_BACK, &wrist);
	distance = network_vr_body_way(shoulder, &wrist, &reach);
	if (upper <= 0.0f || fore <= 0.0f || distance <= 0.0f)
		return;
	/* (a wrist beyond the arm's reach reached for, the arm straight; one
	nearer than its bones fold to, as near as they do) */
	if (distance > (upper + fore) * NETWORK_VR_BODY_STRAIGHT)
		distance = (upper + fore) * NETWORK_VR_BODY_STRAIGHT;
	if (distance < (real)fabs(upper - fore) + 0.01f)
		distance = (real)fabs(upper - fore) + 0.01f;
	point_from_line3d(shoulder, &reach, distance, &wrist);
	/* (the elbow: down, out and back, square to the reach) */
	pole.i = outward->i * 0.5f - aim->forward.i * 0.3f;
	pole.j = outward->j * 0.5f - aim->forward.j * 0.3f;
	pole.k = outward->k * 0.5f - aim->forward.k * 0.3f - 1.0f;
	along = dot_product3d(&pole, &reach);
	pole.i -= reach.i * along;
	pole.j -= reach.j * along;
	pole.k -= reach.k * along;
	if (normalize3d(&pole) <= 0.0f)
		return;
	along = (upper * upper + distance * distance - fore * fore) / (2.0f * distance);
	height = upper * upper - along * along;
	height = height > 0.0f ? square_root(height) : 0.0f;
	point_from_line3d(shoulder, &reach, along, &elbow);
	point_from_line3d(&elbow, &pole, height, &elbow);

	/* the upper arm turned to the elbow, the forearm with it and then to
	the wrist; the hand from the game's aim to the controller's way */
	network_vr_body_way(shoulder, &elbow, &way);
	if (network_vr_body_least_turn(&upper_way, &way, &from, &to))
	{
		network_vr_body_turn_node(&from, &to, &nodes[upperarm]);
		network_vr_body_turn_node(&from, &to, &nodes[forearm]);
		network_vr_body_turn_frames(&from, &to, &fore_way, &fore_way);
	}
	nodes[forearm].position = elbow;
	network_vr_body_way(&elbow, &wrist, &way);
	if (network_vr_body_least_turn(&fore_way, &way, &from, &to))
		network_vr_body_turn_node(&from, &to, &nodes[forearm]);
	network_vr_body_turn_node(aim, &controller, &nodes[hand]);
	nodes[hand].position = wrist;
}

/* a body posed again by its player's pose (nodes as the game posed them,
in the copy); FALSE: as the game poses it */
static boolean network_vr_body_pose(
	struct unit_datum const *unit,
	real_matrix4x3 const *own_nodes,
	struct network_vr_body_object *posed)
{
	static real_vector3d const world_up = { 0.0f, 0.0f, 1.0f };
	struct network_vr_pose const *pose = network_vr_pose_get(unit->unit.player_index);
	struct object_definition *definition =
		object_definition_get(((struct object_datum const *)unit)->definition_index);
	struct network_vr_body_bones const *bones;
	struct model const *model;
	real_matrix4x3 look, headset, aim;
	real_point3d origin, place;
	real_vector3d outward;
	boolean changed[NETWORK_VR_BODY_NODES];
	short side, index;

	if (!pose || !definition || definition->object.model.index == NONE ||
		!(bones = network_vr_body_bones(definition->object.model.index, posed->node_count)))
	{
		return FALSE;
	}
	model = model_definition_get(definition->object.model.index);
	/* (the pose's places are from the unit's: from where it is drawn) */
	origin.x = unit->object.position.x + posed->game[0].position.x - own_nodes[0].position.x;
	origin.y = unit->object.position.y + posed->game[0].position.y - own_nodes[0].position.y;
	origin.z = unit->object.position.z + posed->game[0].position.z - own_nodes[0].position.z;
	if (!network_vr_body_frame(&unit->unit.aiming_vector, &world_up, &aim))
		return FALSE;
	csmemset(changed, 0, sizeof(changed));

	/* the head, from where the game has it look to the headset's way */
	if (network_vr_body_frame(&unit->unit.looking_vector, &world_up, &look) &&
		network_vr_body_frame(&pose->head_forward, &pose->head_up, &headset) &&
		dot_product3d(&look.forward, &headset.forward) > NETWORK_VR_BODY_HEAD_TURN)
	{
		network_vr_body_turn_node(&look, &headset, &posed->nodes[bones->head]);
		changed[bones->head] = TRUE;
	}
	/* each arm to its hand's controller (the model's right the weapon's
	hand's) */
	for (side = 0; side < NUMBER_OF_NETWORK_VR_BODY_SIDES; side++)
	{
		short hand = (side == _network_vr_body_right) != TEST_FLAG(pose->flags, _network_vr_pose_left_handed_bit);

		if (!TEST_FLAG(pose->flags, _network_vr_pose_left_hand_bit + hand))
			continue;
		point_from_line3d(&origin, &pose->hands[hand], 1.0f, &place);
		scale_vector3d(&aim.left, side == _network_vr_body_left ? 1.0f : -1.0f, &outward);
		network_vr_body_arm(posed->game, posed->nodes, bones->upperarm[side], bones->forearm[side],
			bones->hand[side], &place, &pose->hand_forwards[hand], &pose->hand_ups[hand], &aim, &outward);
		changed[bones->upperarm[side]] = changed[bones->forearm[side]] = changed[bones->hand[side]] = TRUE;
	}
	/* the nodes under one posed again, with it (each after its parent) */
	for (index = 1; index < posed->node_count; index++)
	{
		short parent = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)->parent_node_index;
		real_matrix4x3 inverse, moved;

		if (changed[index] || parent == NONE || !changed[parent])
			continue;
		matrix4x3_inverse(&posed->game[parent], &inverse);
		matrix4x3_multiply(&inverse, &posed->game[index], &moved);
		matrix4x3_multiply(&posed->nodes[parent], &moved, &posed->nodes[index]);
		changed[index] = TRUE;
	}
	return TRUE;
}

/* this frame's copy of an object (NULL: none free) */
static struct network_vr_body_object *network_vr_body_object_get(
	long object_index,
	long frame,
	boolean *found)
{
	short index, free_index = NONE;

	for (index = 0; index < NETWORK_VR_BODY_OBJECTS; index++)
	{
		struct network_vr_body_object *posed = &network_vr_body_objects[index];

		if (posed->frame == frame && posed->object_index == object_index)
		{
			*found = TRUE;
			return posed;
		}
		if (free_index == NONE && posed->frame != frame)
			free_index = index;
	}
	*found = FALSE;
	return free_index != NONE ? &network_vr_body_objects[free_index] : NULL;
}

boolean network_vr_bodies(void)
{
	return network_vr_poses_kept();
}

real_matrix4x3 *network_vr_body_node_matrices(
	long object_index,
	real_matrix4x3 *nodes,
	long frame)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct network_vr_body_object *posed;
	real_matrix4x3 *own_nodes;
	long count;
	boolean found;

	if (!object)
		return nodes;
	/* (a body on foot, its player's; or what such a body holds) */
	if (object->object.type == _object_type_biped)
	{
		struct unit_datum const *unit = (struct unit_datum const *)object;

		if (object->object.parent_object_index != NONE || unit->unit.player_index == NONE ||
			TEST_FLAG(object->object.damage_flags, _object_dead_bit) || !network_vr_pose_get(unit->unit.player_index))
		{
			return nodes;
		}
	}
	else
	{
		struct object_datum *parent = object->object.parent_object_index != NONE ?
			object_try_and_get(object->object.parent_object_index) : NULL;

		if (!parent || parent->object.type != _object_type_biped ||
			((struct unit_datum const *)parent)->unit.player_index == NONE ||
			!network_vr_pose_get(((struct unit_datum const *)parent)->unit.player_index))
		{
			return nodes;
		}
	}
	posed = network_vr_body_object_get(object_index, frame, &found);
	if (found)
		return posed->node_count ? posed->nodes : nodes;
	if (!posed)
		return nodes;
	own_nodes = (real_matrix4x3 *)object_header_block_get(object_index, &object->object.node_matrices);
	count = object->object.node_matrices.size / (long)sizeof(real_matrix4x3);
	posed->object_index = object_index;
	posed->frame = frame;
	posed->node_count = 0;
	posed->body = object->object.type == _object_type_biped;
	if (count <= 0 || count > NETWORK_VR_BODY_NODES)
		return nodes;
	csmemcpy(posed->game, nodes ? nodes : own_nodes, count * sizeof(real_matrix4x3));
	csmemcpy(posed->nodes, posed->game, count * sizeof(real_matrix4x3));
	posed->node_count = (short)count;
	if (posed->body)
	{
		if (!network_vr_body_pose((struct unit_datum const *)object, own_nodes, posed))
			posed->node_count = 0;
	}
	else
	{
		/* (moved as the body's node it is held at moved: the body's copy
		posed first) */
		long parent_index = object->object.parent_object_index;
		short node_index = object->object.parent_node_index;
		struct network_vr_body_object *body;
		real_matrix4x3 inverse, moved;
		short index;

		render_interpolation_object_node_matrices(parent_index);
		body = network_vr_body_object_get(parent_index, frame, &found);
		if (!found || !body->body || node_index < 0 || node_index >= body->node_count)
		{
			posed->node_count = 0;
			return nodes;
		}
		matrix4x3_inverse(&body->game[node_index], &inverse);
		matrix4x3_multiply(&body->nodes[node_index], &inverse, &moved);
		for (index = 0; index < count; index++)
			matrix4x3_multiply(&moved, &posed->game[index], &posed->nodes[index]);
	}
	return posed->node_count ? posed->nodes : nodes;
}
