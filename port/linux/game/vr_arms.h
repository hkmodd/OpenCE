/*
VR_ARMS.H

The first-person arms in the headset (vr_arms.c).
*/

#ifndef VR_ARMS_H
#define VR_ARMS_H

#include "math/real_math.h"

struct animation_graph;

/* what the arms are posed to this frame: the world's places and axes */
struct vr_arms_pose
{
	/* the head between the eyes */
	real_point3d head;
	real_vector3d head_forward, head_up;
	/* the heading the eyes turn from (radians: LOCAL's forward in the
	world's yaw), which a turn of the stick turns */
	real heading;
	/* the weapon is in the player's left hand: the model is mirrored, its
	right arm the left */
	boolean main_left;
	/* the controllers' grips, the weapon's hand's and the other's: their
	places, their forward (the way a held gun fires) and up */
	boolean main_valid, off_valid;
	real_point3d main_grip, off_grip;
	real_vector3d main_forward, main_up, off_forward, off_up;
	/* the other hand belongs on the weapon: held there with both hands, or
	in an animation of its own (a reload, a grenade's throw) */
	boolean off_on_weapon;
	/* how far the other controller's grip and trigger are pressed (0 to 1):
	its hand's fingers close with them */
	real off_squeeze, off_trigger;
	/* since the last frame */
	real seconds;
};

/* the first-person weapon's node matrices (world space, posed by the game
from the weapon's camera): each arm from its shoulder, the weapon's hand
where the game holds the weapon, the other at its controller */
void vr_arms_pose(real_matrix4x3 *matrices, short count, struct animation_graph *graph,
	struct vr_arms_pose const *pose);
/* a new map, a new view: the body faces the head again */
void vr_arms_reset(void);

#endif
