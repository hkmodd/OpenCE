/*
FIRST_PERSON_WEAPONS.H

header included in hcex build.
*/

#ifndef __FIRST_PERSON_WEAPONS_H
#define __FIRST_PERSON_WEAPONS_H
#pragma once

/* ---------- headers */

#include "cseries/cseries.h"
#include "math/real_math.h"

/* ---------- constants */

/* port: the first-person weapons: each local player's weapon in hand, its
index the local player's, and the weapon a headset's player holds in the
other hand, FIRST_PERSON_WEAPON_OTHER_HAND more (port/linux/game/dual_wield.c);
effects and particles on either take its index for their local player's */
enum
{
	FIRST_PERSON_WEAPON_OTHER_HAND= 4,
	NUMBER_OF_FIRST_PERSON_WEAPONS= 2*FIRST_PERSON_WEAPON_OTHER_HAND,
};

/* ---------- macros */

/* port: the local player a first-person weapon's index is of, and whether
an index (an effect's, a particle's) is a first-person weapon of that local
player's */
#define FIRST_PERSON_WEAPON_LOCAL_PLAYER(index) ((short)((index)%FIRST_PERSON_WEAPON_OTHER_HAND))
#define FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(index, local_player_index) \
	((index)>=0 && (index)<NUMBER_OF_FIRST_PERSON_WEAPONS && \
	FIRST_PERSON_WEAPON_LOCAL_PLAYER(index)==(local_player_index))

/* ---------- structures */

struct real_matrix4x3;
struct animation_state;
struct object_marker;

/* ---------- prototypes/FIRST_PERSON_WEAPONS.C */

short animation_update_render_only(
	long animation_graph_index,
	struct animation_state *state,
	long *sound_index);
void first_person_weapons_initialize(
	void);
void first_person_weapons_dispose(
	void);
void first_person_weapons_initialize_for_new_map(
	void);
void first_person_weapons_dispose_from_old_map(
	void);
void first_person_weapons_update(
	void);
long first_person_weapon_get_local_index(
	long weapon_index);
void first_person_weapon_draw(
	void);
short first_person_weapon_get_marker_by_name(
	long weapon_index,
	char const *name,
	struct object_marker *markers,
	short maximum_marker_count);
void first_person_weapon_render_update(
	void);
short first_person_weapon_get_marker_by_name_render(
	long weapon_index,
	char const *name,
	struct object_marker *markers,
	short maximum_marker_count);
void first_person_weapon_center_flashlight(
	long unit_index,
	real_point3d *position,
	real_vector3d *forward,
	real_vector3d *up);
boolean first_person_weapon_adjust_light(
	long weapon_index,
	char const *marker_name,
	real_point3d *position,
	real_vector3d *forward,
	real_vector3d *up);
void first_person_weapon_message_from_unit(
	long unit_index,
	short message_type);
void first_person_weapon_message_from_weapon(
	long weapon_index,
	short message_type);
struct real_matrix4x3 *first_person_weapon_get_node_matrix(
	short local_player_index,
	short node_index);
/* port: whether the local player's first-person weapon rests in its idle
pose (port/linux/game/vr_view.c) */
boolean first_person_weapon_idle(
	short local_player_index);
/* port: the matrices the local player's first-person weapon was last posed
with, and their count (port/linux/game/vr_view.c) */
struct real_matrix4x3 *first_person_weapon_get_node_matrices(
	short local_player_index,
	short *node_count);
/* port: the first-person animations those matrices were posed with, or NULL
(port/linux/game/vr_arms.c) */
struct animation_graph *first_person_weapon_get_animation_graph(
	short local_player_index);
/* port: an animation of the off hand's own plays: the weapon readied or put
away, reloaded, a grenade thrown, a melee (port/linux/game/vr_arms.c) */
boolean first_person_weapon_off_hand_busy(
	short local_player_index);
/* port: marks the animation graph's nodes the hands' model is drawn with and
the weapon's is not (the arms, the hands; `node_count` of them), and
returns the count marked (port/linux/game/vr_view.c) */
short first_person_weapon_arm_nodes(
	short local_player_index,
	boolean *arm_nodes,
	short node_count);

/* ---------- globals */

/* ---------- public code */

#endif // __FIRST_PERSON_WEAPONS_H
