/* A weapon in the other hand. By a VR server's rules (vr_rules_active) a
headset's player holds a second weapon of their unit's inventory in their
other hand: readied as the game readies the weapon in hand, held at the
unit's left hand (as the other players see it, posed there by its headset:
network_vr_body.c), told its controls once a tick from that hand's trigger
(weapon_owner_update, as the unit tells its weapon in hand), so that its
rate of fire, heat, ammunition, reloads and effects are the weapon's own.
Its shots leave its muzzle along where that hand points. The player sees
it in their hand, as the world model it is (the first-person weapon is the
weapon in hand's alone).

In a network game the player's machine holds it as it chooses and says so
with the player's pose (network_vr.c); every other machine's copy of the
unit, the host's too, holds what the pose says and fires it with the
pose's trigger: its shots are seen everywhere, its ammunition is the
host's, and its hits are the player's machine's to report, as any shot
of theirs (network_damage.c). There it is held in the posed body's hand
(network_vr_body.c) and its shots leave that hand.

The game's rules for the weapon in hand still hold: before a weapon is
readied in the unit's hand the other hand's is put back if it is that one,
and before the unit's inventory is dropped (its death) or deleted, so that
no weapon of an inventory is ever connected to the map twice. */
#include "cseries.h"
#include "items/items.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "units/units.h"
#include "dual_wield.h"
#include "halo_vr.h"
#include "network_distributed.h"
#include "network_vr.h"
#include <string.h>

void platform_log(char const *format, ...);

/* the units with a weapon in the other hand at once (each player's) */
#define DUAL_WIELD_UNITS 16
/* the weapons drawn at the hand in a frame, and their nodes */
#define DUAL_WIELD_DRAWN 4
#define DUAL_WIELD_NODES 32
/* the other hand's trigger pressed */
#define DUAL_WIELD_TRIGGER 0.5f

/* each unit's weapon in the other hand (NONE: none), its slot, and that
hand's trigger */
static struct
{
	long unit_index;
	long weapon_index;
	short slot;
	real trigger;
} dual_wield_units[DUAL_WIELD_UNITS];
static boolean dual_wield_initialized = FALSE;
/* any unit has one (none: nothing to do) */
static short dual_wield_count = 0;

/* a weapon drawn at the hand this frame */
static struct
{
	long object_index;
	long frame;
	short node_count;
	real_matrix4x3 nodes[DUAL_WIELD_NODES];
} dual_wield_drawn[DUAL_WIELD_DRAWN];

static short dual_wield_find(
	long unit_index)
{
	short index;

	if (!dual_wield_initialized)
	{
		for (index = 0; index < DUAL_WIELD_UNITS; index++)
			dual_wield_units[index].unit_index = NONE;
		for (index = 0; index < DUAL_WIELD_DRAWN; index++)
			dual_wield_drawn[index].object_index = NONE;
		dual_wield_initialized = TRUE;
	}
	for (index = 0; index < DUAL_WIELD_UNITS; index++)
	{
		if (unit_index != NONE && dual_wield_units[index].unit_index == unit_index)
			return index;
	}
	return NONE;
}

/* the weapon taken out of the other hand back into the inventory, as the
game puts away its weapon in hand */
static void dual_wield_put_back(
	short index)
{
	long weapon_index = dual_wield_units[index].weapon_index;
	struct unit_datum *unit = unit_try_and_get(dual_wield_units[index].unit_index);

	if (unit && object_try_and_get(weapon_index) &&
		unit_inventory_get_weapon(dual_wield_units[index].unit_index, dual_wield_units[index].slot) == weapon_index &&
		TEST_FLAG(object_get(weapon_index)->object.flags, _object_connected_to_map_bit))
	{
		weapon_put_away(weapon_index, TRUE);
		object_detach(weapon_index);
		object_disconnect_from_map(weapon_index);
		object_activate(weapon_index);
		object_set_visibility(weapon_index, FALSE);
		item_in_unit_inventory(weapon_index, dual_wield_units[index].unit_index);
	}
	dual_wield_units[index].unit_index = NONE;
	dual_wield_units[index].weapon_index = NONE;
	dual_wield_count--;
}

boolean dual_wield_begin(
	long unit_index,
	short slot)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	long weapon_index;
	short index;

	if (!unit || slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT || slot == unit->unit.current_weapon_index ||
		slot == unit->unit.desired_weapon_index || unit->object.parent_object_index != NONE ||
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || dual_wield_find(unit_index) != NONE)
	{
		return FALSE;
	}
	weapon_index = unit_inventory_get_weapon(unit_index, slot);
	if (weapon_index == NONE || TEST_FLAG(object_get(weapon_index)->object.flags, _object_connected_to_map_bit))
		return FALSE;
	for (index = 0; index < DUAL_WIELD_UNITS && dual_wield_units[index].unit_index != NONE; index++)
		;
	if (index == DUAL_WIELD_UNITS)
		return FALSE;
	dual_wield_units[index].unit_index = unit_index;
	dual_wield_units[index].weapon_index = weapon_index;
	dual_wield_units[index].slot = slot;
	dual_wield_units[index].trigger = 0.0f;
	dual_wield_count++;
	/* (readied at the left hand, as unit_ready_desired_weapon readies the
	weapon in hand at its own) */
	object_reconnect_to_map(weapon_index, NULL);
	object_set_visibility(weapon_index, TRUE);
	object_attach_to_marker(unit_index, "left hand", weapon_index, "");
	weapon_ready(weapon_index);
	platform_log("dual wield: unit %08lx holds slot %d's weapon in the other hand", unit_index, slot);
	return TRUE;
}

void dual_wield_end(
	long unit_index)
{
	short index = dual_wield_find(unit_index);

	if (index != NONE)
	{
		dual_wield_put_back(index);
		platform_log("dual wield: unit %08lx's other hand put its weapon back", unit_index);
	}
}

long dual_wield_weapon(
	long unit_index)
{
	short index = dual_wield_count ? dual_wield_find(unit_index) : NONE;

	return index != NONE ? dual_wield_units[index].weapon_index : NONE;
}

short dual_wield_slot(
	long unit_index)
{
	short index = dual_wield_count ? dual_wield_find(unit_index) : NONE;

	return index != NONE ? dual_wield_units[index].slot : NONE;
}

void dual_wield_set_trigger(
	long unit_index,
	real trigger)
{
	short index = dual_wield_count ? dual_wield_find(unit_index) : NONE;

	if (index != NONE)
		dual_wield_units[index].trigger = trigger > 0.0f ? (trigger < 1.0f ? trigger : 1.0f) : 0.0f;
}

/* another machine's player's unit holds what the player's pose says,
its trigger the pose's (none once no pose has come for a while) */
static void dual_wield_follow_pose(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct network_vr_pose const *pose;
	short index;

	if (!unit || unit->unit.player_index == NONE || distributed_player_is_local(unit->unit.player_index))
		return;
	pose = network_vr_pose_get(unit->unit.player_index);
	index = dual_wield_find(unit_index);
	if (index != NONE && (!pose || dual_wield_units[index].slot != pose->off_slot))
	{
		dual_wield_put_back(index);
		index = NONE;
	}
	if (!pose || pose->off_slot == NONE || (index == NONE && !dual_wield_begin(unit_index, pose->off_slot)))
		return;
	dual_wield_set_trigger(unit_index, pose->off_trigger);
}

void dual_wield_unit_update(
	long unit_index)
{
	struct unit_datum *unit;
	short index;
	word flags = 0;

	if (network_vr_poses_kept())
		dual_wield_follow_pose(unit_index);
	if (!dual_wield_count || (index = dual_wield_find(unit_index)) == NONE)
		return;
	unit = unit_try_and_get(unit_index);
	/* (gone from the inventory, dead or riding: put back) */
	if (!unit || unit_inventory_get_weapon(unit_index, dual_wield_units[index].slot) != dual_wield_units[index].weapon_index ||
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || unit->object.parent_object_index != NONE ||
		dual_wield_units[index].slot == unit->unit.current_weapon_index)
	{
		dual_wield_put_back(index);
		return;
	}
	if (dual_wield_units[index].trigger >= DUAL_WIELD_TRIGGER)
		SET_FLAG(flags, _weapon_control_primary_trigger_bit, TRUE);
	if (TEST_FLAG(unit->unit.control_flags, _unit_control_weapon_reload_bit))
		SET_FLAG(flags, _weapon_control_reload_bit, TRUE);
	weapon_owner_update(dual_wield_units[index].weapon_index, flags, dual_wield_units[index].trigger);
}

void dual_wield_unit_readying(
	long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	short index = dual_wield_count ? dual_wield_find(unit_index) : NONE;
	long desired_weapon_index;

	if (!unit)
		return;
	if (index != NONE && dual_wield_units[index].slot == unit->unit.desired_weapon_index)
		dual_wield_put_back(index);
	/* (a weapon left in an other hand by a saved game: put back before it is
	readied in this one) */
	desired_weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.desired_weapon_index);
	if (desired_weapon_index != NONE && unit->unit.desired_weapon_index != unit->unit.current_weapon_index &&
		TEST_FLAG(object_get(desired_weapon_index)->object.flags, _object_connected_to_map_bit))
	{
		weapon_put_away(desired_weapon_index, TRUE);
		object_detach(desired_weapon_index);
		object_disconnect_from_map(desired_weapon_index);
		object_activate(desired_weapon_index);
		object_set_visibility(desired_weapon_index, FALSE);
		item_in_unit_inventory(desired_weapon_index, unit_index);
	}
}

void dual_wield_unit_dropping(
	long unit_index)
{
	short index = dual_wield_count ? dual_wield_find(unit_index) : NONE;

	if (index != NONE)
		dual_wield_put_back(index);
}

void dual_wield_unit_releasing(
	long unit_index,
	short slot)
{
	short index = dual_wield_count ? dual_wield_find(unit_index) : NONE;

	if (index != NONE && dual_wield_units[index].slot == slot)
		dual_wield_put_back(index);
}

/* the unit whose other hand holds the weapon (NONE: none's) */
static long dual_wield_holder(
	long weapon_index)
{
	short index;

	if (!dual_wield_count)
		return NONE;
	for (index = 0; index < DUAL_WIELD_UNITS; index++)
	{
		if (dual_wield_units[index].unit_index != NONE && dual_wield_units[index].weapon_index == weapon_index)
			return dual_wield_units[index].unit_index;
	}
	return NONE;
}

/* another machine's player's other hand, as their pose has it: at the
hand, along the controller (NONE: no pose, or no such hand) */
static boolean dual_wield_posed_grip(
	long unit_index,
	real_matrix4x3 *grip)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct network_vr_pose const *pose = unit && unit->unit.player_index != NONE ?
		network_vr_pose_get(unit->unit.player_index) : NULL;
	short hand;

	if (!pose)
		return FALSE;
	/* (the other hand: the right of a left-handed player) */
	hand = TEST_FLAG(pose->flags, _network_vr_pose_left_handed_bit) ? 1 : 0;
	if (!TEST_FLAG(pose->flags, _network_vr_pose_left_hand_bit + hand))
		return FALSE;
	grip->scale = 1.0f;
	grip->forward = pose->hand_forwards[hand];
	grip->up = pose->hand_ups[hand];
	cross_product3d(&grip->up, &grip->forward, &grip->left);
	point_from_line3d(&unit->object.position, &pose->hands[hand], 1.0f, &grip->position);
	return TRUE;
}

boolean dual_wield_projectile_ray(
	long weapon_index,
	real_point3d *origin,
	real_vector3d *forward)
{
	long unit_index = dual_wield_holder(weapon_index);
	struct object_marker marker;
	real_matrix4x3 grip, inverse;
	real_matrix4x3 const *nodes;
	real_point3d muzzle;

	if (unit_index == NONE || (!vr_render_off_hand_grip(unit_index, &grip) && !dual_wield_posed_grip(unit_index, &grip)))
		return FALSE;
	/* (its muzzle where it is on the weapon held at the hand: from its
	root's frame as the game holds it) */
	nodes = (real_matrix4x3 const *)object_header_block_get(weapon_index, &object_get(weapon_index)->object.node_matrices);
	if (object_get_marker_by_name(weapon_index, "primary trigger", &marker, 1) <= 0 ||
		object_get(weapon_index)->object.node_matrices.size < (long)sizeof(real_matrix4x3))
	{
		marker.matrix.position = nodes[0].position;
	}
	matrix4x3_inverse(&nodes[0], &inverse);
	matrix4x3_transform_point(&inverse, &marker.matrix.position, &muzzle);
	matrix4x3_transform_point(&grip, &muzzle, origin);
	*forward = grip.forward;
	return TRUE;
}

boolean dual_wield_any(
	void)
{
	return dual_wield_count != 0;
}

long dual_wield_first_person_weapon(
	long unit_index)
{
	real_matrix4x3 grip;
	long weapon_index = dual_wield_weapon(unit_index);

	return weapon_index != NONE && vr_render_off_hand_grip(unit_index, &grip) ? weapon_index : NONE;
}

real_matrix4x3 *dual_wield_node_matrices(
	long object_index,
	real_matrix4x3 *nodes,
	long frame)
{
	long unit_index = dual_wield_holder(object_index);
	struct object_datum *object;
	real_matrix4x3 grip, inverse, moved;
	short index, free_index = NONE, node_index;
	long count;

	if (unit_index == NONE)
		return nodes;
	for (index = 0; index < DUAL_WIELD_DRAWN; index++)
	{
		if (dual_wield_drawn[index].object_index == object_index && dual_wield_drawn[index].frame == frame)
			return dual_wield_drawn[index].node_count ? dual_wield_drawn[index].nodes : nodes;
		if (free_index == NONE && dual_wield_drawn[index].frame != frame)
			free_index = index;
	}
	object = object_get(object_index);
	count = object->object.node_matrices.size / (long)sizeof(real_matrix4x3);
	if (free_index == NONE || count <= 0 || count > DUAL_WIELD_NODES || !vr_render_off_hand_grip(unit_index, &grip))
		return nodes;
	if (!nodes)
		nodes = (real_matrix4x3 *)object_header_block_get(object_index, &object->object.node_matrices);
	dual_wield_drawn[free_index].object_index = object_index;
	dual_wield_drawn[free_index].frame = frame;
	dual_wield_drawn[free_index].node_count = (short)count;
	/* (the whole weapon moved so that its root is at the hand's grip,
	turned as the hand points) */
	matrix4x3_inverse(&nodes[0], &inverse);
	matrix4x3_multiply(&grip, &inverse, &moved);
	for (node_index = 0; node_index < count; node_index++)
		matrix4x3_multiply(&moved, &nodes[node_index], &dual_wield_drawn[free_index].nodes[node_index]);
	return dual_wield_drawn[free_index].nodes;
}
