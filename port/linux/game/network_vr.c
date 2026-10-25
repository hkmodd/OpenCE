/* The headsets' players' poses in a VR server's game (network.vr_rules):
each VR player's machine sends its head and hands, from its unit, to the
host once a tick, unreliably (a lost one is overtaken by the next); the
host takes a pose only of a player of the machine that sent it, of a VR
server's game, newer than the last, its head near the unit, its hands near
the head and every axis whole, and sends it on to each client machine that
can see the player. Every machine keeps each player's latest, which is none
a second after it was sent: what the player's body is to be posed by. It
is only drawn: the game's hits, shots and movement are the unit's. It
says what the other hand holds and how far its trigger is pulled, which
every machine's copy of the unit holds and fires (dual_wield.c). */
#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "network_distributed.h"
#include "network_vr.h"
#include "halo_vr.h"
#include "infection.h"
#include "items/items.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "units/units.h"
#include <math.h>
#include <string.h>

void platform_log(char const *format, ...);

/* a place's parts of 1/4096 of a world unit (8 world units each way) */
#define NETWORK_VR_PLACE_SCALE 4096.0f

/* a pose on the wire: its player, flags, the head's and hands' places
and axes, the other hand's weapon's slot (-1: none) and its trigger (of
255) */
struct network_vr_pose_entry
{
	byte player_index;
	byte flags;
	struct distributed_vector head;
	struct distributed_vector head_forward;
	struct distributed_vector head_up;
	struct distributed_vector hands[2];
	struct distributed_vector hand_forwards[2];
	struct distributed_vector hand_ups[2];
	char off_slot;
	byte off_trigger;
};

typedef char network_vr_pose_entry_size_assert[sizeof(struct network_vr_pose_entry) == 2 + 9 * 6 + 2 ? 1 : -1];

struct network_vr_pose_message
{
	struct distributed_message_header header;
	struct network_vr_pose_entry entry;
};

/* each player's latest pose, the tick it was sent at (NONE: none), by
its sender's clock, which orders them, and the tick this machine took it
at, by its own, which ages it (a machine's clock may run ahead of
another's); the tick this machine last sent its own at */
static struct
{
	long time;
	long taken_time;
	struct network_vr_pose pose;
} network_vr_poses[MAXIMUM_TRACKED_PLAYERS];
static long network_vr_sent_time = NONE;
/* any player's pose was taken this game */
static boolean network_vr_any_pose = FALSE;
/* the tick each player's weapon last fell at (NONE: none this game) */
static long network_vr_drop_times[MAXIMUM_TRACKED_PLAYERS];
/* each player's grenade last thrown with the arm, for the game's throw
that follows: the tick it was taken at (NONE: none), the hand's velocity
(world units a tick) and its place from the eyes (world units) */
static struct
{
	long time;
	real_vector3d velocity;
	real_vector3d offset;
} network_vr_throws[MAXIMUM_TRACKED_PLAYERS];

void network_vr_reset(void)
{
	short index;

	for (index = 0; index < MAXIMUM_TRACKED_PLAYERS; index++)
	{
		network_vr_poses[index].time = NONE;
		network_vr_drop_times[index] = NONE;
		network_vr_throws[index].time = NONE;
	}
	network_vr_sent_time = NONE;
	network_vr_any_pose = FALSE;
}

boolean network_vr_poses_kept(void)
{
	return network_vr_any_pose;
}

word network_vr_entry_size(void)
{
	return (word)sizeof(struct network_vr_pose_entry);
}

static void network_vr_pose_pack(short player_index, struct network_vr_pose const *pose,
	struct network_vr_pose_entry *entry)
{
	short hand;

	memset(entry, 0, sizeof(*entry));
	entry->player_index = (byte)player_index;
	entry->flags = pose->flags;
	entry->off_slot = (char)pose->off_slot;
	entry->off_trigger = (byte)(long)floor(PIN(pose->off_trigger, 0.0f, 1.0f) * 255.0f + 0.5f);
	distributed_vector_pack(&pose->head, NETWORK_VR_PLACE_SCALE, &entry->head);
	distributed_vector_pack(&pose->head_forward, DISTRIBUTED_UNIT_SCALE, &entry->head_forward);
	distributed_vector_pack(&pose->head_up, DISTRIBUTED_UNIT_SCALE, &entry->head_up);
	for (hand = 0; hand < 2; hand++)
	{
		if (!TEST_FLAG(pose->flags, _network_vr_pose_left_hand_bit + hand))
			continue;
		distributed_vector_pack(&pose->hands[hand], NETWORK_VR_PLACE_SCALE, &entry->hands[hand]);
		distributed_vector_pack(&pose->hand_forwards[hand], DISTRIBUTED_UNIT_SCALE, &entry->hand_forwards[hand]);
		distributed_vector_pack(&pose->hand_ups[hand], DISTRIBUTED_UNIT_SCALE, &entry->hand_ups[hand]);
	}
}

static void network_vr_pose_unpack(struct network_vr_pose_entry const *entry, struct network_vr_pose *pose)
{
	short hand;

	memset(pose, 0, sizeof(*pose));
	pose->flags = entry->flags;
	pose->off_slot = entry->off_slot;
	pose->off_trigger = (real)entry->off_trigger / 255.0f;
	distributed_vector_unpack(&entry->head, NETWORK_VR_PLACE_SCALE, &pose->head);
	distributed_unit_vector_unpack(&entry->head_forward, &pose->head_forward);
	distributed_unit_vector_unpack(&entry->head_up, &pose->head_up);
	for (hand = 0; hand < 2; hand++)
	{
		distributed_vector_unpack(&entry->hands[hand], NETWORK_VR_PLACE_SCALE, &pose->hands[hand]);
		distributed_unit_vector_unpack(&entry->hand_forwards[hand], &pose->hand_forwards[hand]);
		distributed_unit_vector_unpack(&entry->hand_ups[hand], &pose->hand_ups[hand]);
	}
}

static real network_vr_distance_squared(real_vector3d const *a, real_vector3d const *b)
{
	real i = a->i - b->i, j = a->j - b->j, k = a->k - b->k;

	return i * i + j * j + k * k;
}

boolean network_vr_pose_valid(struct network_vr_pose const *pose)
{
	struct network_vr_pose copy = *pose;
	short hand;

	if (pose->flags >= FLAG(NUMBER_OF_NETWORK_VR_POSE_BITS) ||
		pose->head.i * pose->head.i + pose->head.j * pose->head.j > NETWORK_VR_HEAD_REACH * NETWORK_VR_HEAD_REACH ||
		pose->head.k < -NETWORK_VR_HEAD_REACH || pose->head.k > 2.0f * NETWORK_VR_HEAD_REACH ||
		!distributed_axes_make_valid(&copy.head_forward, &copy.head_up) ||
		pose->off_slot < NONE || pose->off_slot >= MAXIMUM_WEAPONS_PER_UNIT ||
		!(pose->off_trigger >= 0.0f && pose->off_trigger <= 1.0f))
	{
		return FALSE;
	}
	for (hand = 0; hand < 2; hand++)
	{
		if (TEST_FLAG(pose->flags, _network_vr_pose_left_hand_bit + hand) &&
			(network_vr_distance_squared(&pose->hands[hand], &pose->head) >
					NETWORK_VR_HAND_REACH * NETWORK_VR_HAND_REACH ||
				!distributed_axes_make_valid(&copy.hand_forwards[hand], &copy.hand_ups[hand])))
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* a pose taken: kept if newer than the player's last by its sender's
clock (its axes made whole), and told once a game */
static boolean network_vr_pose_take(short player_index, long time, struct network_vr_pose const *pose)
{
	short hand;

	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS || !network_vr_pose_valid(pose) ||
		(network_vr_poses[player_index].time != NONE && time <= network_vr_poses[player_index].time))
	{
		return FALSE;
	}
	if (network_vr_poses[player_index].time == NONE)
		platform_log("network vr: player %d's headset pose", player_index);
	network_vr_poses[player_index].time = time;
	network_vr_poses[player_index].taken_time = game_time_get();
	network_vr_poses[player_index].pose = *pose;
	network_vr_any_pose = TRUE;
	distributed_axes_make_valid(&network_vr_poses[player_index].pose.head_forward,
		&network_vr_poses[player_index].pose.head_up);
	for (hand = 0; hand < 2; hand++)
	{
		if (TEST_FLAG(pose->flags, _network_vr_pose_left_hand_bit + hand))
		{
			distributed_axes_make_valid(&network_vr_poses[player_index].pose.hand_forwards[hand],
				&network_vr_poses[player_index].pose.hand_ups[hand]);
		}
	}
	return TRUE;
}

/* the host's game is a VR server's */
static boolean network_vr_server(void)
{
	struct network_game *game = network_game_get_game();

	return game && TEST_FLAG(game->cooperative_flags, _network_game_port_vr_rules_bit);
}

/* (the host) a player's pose to each client machine that can see the
player, but the one it came from (NONE: the host's) */
static void network_vr_relay(short player_index, long from_machine_index)
{
	long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	struct network_vr_pose_message message;
	short count, index;

	network_vr_pose_pack(player_index, &network_vr_poses[player_index].pose, &message.entry);
	count = distributed_client_machines(machines, NUMBEROF(machines));
	for (index = 0; index < count; index++)
	{
		if (machines[index] != from_machine_index && distributed_machine_sees_player(machines[index], player_index))
		{
			distributed_send_to_machine(machines[index], &message, _distributed_message_vr_pose_down, 1,
				sizeof(message));
		}
	}
}

void network_vr_send(long player, struct network_vr_pose const *pose)
{
	struct network_vr_pose_message message;
	long time = game_time_get();
	short player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player);

	if (player == NONE || player_index >= MAXIMUM_TRACKED_PLAYERS || time == network_vr_sent_time ||
		!game_in_progress() || !network_vr_server())
	{
		return;
	}
	network_vr_sent_time = time;
	if (game_connection() == _game_connection_network_server)
	{
		if (network_vr_pose_take(player_index, time, pose))
			network_vr_relay(player_index, NONE);
	}
	else if (game_connection() == _game_connection_network_client && network_vr_pose_valid(pose))
	{
		network_vr_pose_pack(player_index, pose, &message.entry);
		distributed_send(&message, _distributed_message_vr_pose_up, 1, sizeof(message), _distributed_to_host);
	}
}

void network_vr_handle_up(long machine_index, long game_time, void const *entries, short count)
{
	struct network_vr_pose_entry entry;
	struct network_vr_pose pose;
	short player_index;

	if (count != 1 || !network_vr_server())
		return;
	csmemcpy(&entry, entries, sizeof(entry));
	player_index = entry.player_index;
	if (player_index >= MAXIMUM_TRACKED_PLAYERS || distributed_player_from_byte(entry.player_index) == NONE ||
		!distributed_machine_has_player(machine_index, player_index))
	{
		return;
	}
	network_vr_pose_unpack(&entry, &pose);
	if (network_vr_pose_take(player_index, game_time, &pose))
		network_vr_relay(player_index, machine_index);
}

void network_vr_handle_down(long game_time, void const *entries, short count)
{
	struct network_vr_pose_entry entry;
	struct network_vr_pose pose;
	short player_index;

	if (count != 1)
		return;
	csmemcpy(&entry, entries, sizeof(entry));
	player_index = entry.player_index;
	if (player_index >= MAXIMUM_TRACKED_PLAYERS || distributed_player_from_byte(entry.player_index) == NONE ||
		distributed_player_is_local(distributed_player_from_byte(entry.player_index)))
	{
		return;
	}
	network_vr_pose_unpack(&entry, &pose);
	network_vr_pose_take(player_index, game_time, &pose);
}

struct network_vr_pose const *network_vr_pose_get(long player)
{
	short player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player);
	long age;

	if (player == NONE || player_index >= MAXIMUM_TRACKED_PLAYERS || network_vr_poses[player_index].time == NONE ||
		distributed_player_is_local(player))
	{
		return NULL;
	}
	age = game_time_get() - network_vr_poses[player_index].taken_time;
	return age >= 0 && age <= NETWORK_VR_POSE_TICKS ? &network_vr_poses[player_index].pose : NULL;
}

boolean network_vr_melee_point(long unit_index, real_point3d *point)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	struct network_vr_pose const *pose = unit ? network_vr_pose_get(unit->unit.player_index) : NULL;
	real ahead, farthest = 0.0f;
	short hand, struck = NONE;

	if (!pose)
		return FALSE;
	for (hand = 0; hand < 2; hand++)
	{
		real_vector3d reach;

		if (!TEST_FLAG(pose->flags, hand ? _network_vr_pose_right_hand_bit : _network_vr_pose_left_hand_bit))
			continue;
		subtract_vectors3d(&pose->hands[hand], &pose->head, &reach);
		ahead = dot_product3d(&reach, &pose->head_forward);
		if (struck == NONE || ahead > farthest)
		{
			struck = hand;
			farthest = ahead;
		}
	}
	if (struck == NONE)
		return FALSE;
	point_from_line3d(&unit->object.position, &pose->hands[struck], 1.0f, point);
	return TRUE;
}

/* ---------- the weapon let fall */

/* a dropped weapon's velocity on the wire: parts of 1/16384 of a world
unit a tick */
#define NETWORK_VR_VELOCITY_SCALE 16384.0f

/* a client's request to the host: its player's weapon let fall (0: the
one in hand; else 1 and the slot of the other hand's), thrown with a
velocity (the world's axes, world units a tick) */
struct network_vr_drop_entry
{
	byte player_index;
	byte slot;
	struct distributed_vector velocity;
};

typedef char network_vr_drop_entry_size_assert[sizeof(struct network_vr_drop_entry) == 8 ? 1 : -1];

struct network_vr_drop_message
{
	struct distributed_message_header header;
	struct network_vr_drop_entry entry;
};

word network_vr_drop_entry_size(void)
{
	return (word)sizeof(struct network_vr_drop_entry);
}

/* the player's unit lets its weapon in hand (slot NONE), or another of
its inventory's, the other hand's, fall, thrown with the velocity
(no faster than NETWORK_VR_DROP_SPEED) on top of the unit's own, as the
game drops one (not oftener than every NETWORK_VR_DROP_TICKS, not while it
rides, dead or keeping it, an Infection zombie's skull) */
static boolean network_vr_drop_weapon(short player_index, real_vector3d const *velocity, short slot)
{
	struct player_datum *player = player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS ?
		distributed_player((byte)player_index) : NULL;
	struct unit_datum *unit = player ? unit_try_and_get(player->unit_index) : NULL;
	long now = game_time_get();
	long weapon_index;
	real_vector3d thrown = *velocity, unit_velocity, weapon_velocity;
	real speed;

	if (!unit || unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
		(network_vr_drop_times[player_index] != NONE && now >= network_vr_drop_times[player_index] &&
			now - network_vr_drop_times[player_index] < NETWORK_VR_DROP_TICKS))
	{
		return FALSE;
	}
	if (slot == NONE)
	{
		weapon_index = unit_inventory_get_weapon(player->unit_index, unit->unit.current_weapon_index);
		if (weapon_index == NONE || !infection_allow_weapon_drop(player->unit_index, weapon_index) ||
			!unit_drop_current_weapon(player->unit_index, TRUE))
		{
			return FALSE;
		}
	}
	else
	{
		if (slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT || slot == unit->unit.current_weapon_index)
			return FALSE;
		weapon_index = unit_inventory_get_weapon(player->unit_index, slot);
		if (weapon_index == NONE || !infection_allow_weapon_drop(player->unit_index, weapon_index) ||
			!unit_drop_inventory_slot(player->unit_index, slot))
		{
			return FALSE;
		}
	}
	network_vr_drop_times[player_index] = now;
	/* (no faster than the most; a velocity not a number, none) */
	speed = (real)sqrt(thrown.i * thrown.i + thrown.j * thrown.j + thrown.k * thrown.k);
	if (!(speed <= NETWORK_VR_DROP_SPEED))
	{
		real scale = speed > NETWORK_VR_DROP_SPEED ? NETWORK_VR_DROP_SPEED / speed : 0.0f;

		thrown.i *= scale;
		thrown.j *= scale;
		thrown.k *= scale;
	}
	/* (the game may have deleted it: one that cannot be fired, or out of
	the world) */
	if (object_try_and_get(weapon_index))
	{
		object_get_velocities(player->unit_index, &unit_velocity, NULL);
		object_get_velocities(weapon_index, &weapon_velocity, NULL);
		thrown.i += unit_velocity.i - weapon_velocity.i;
		thrown.j += unit_velocity.j - weapon_velocity.j;
		thrown.k += unit_velocity.k - weapon_velocity.k;
		item_accelerate(weapon_index, &thrown, FALSE);
	}
	return TRUE;
}

void network_vr_drop(long player, real_vector3d const *velocity, short slot)
{
	struct network_vr_drop_message message;
	short player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player);

	if (player == NONE || player_index >= MAXIMUM_TRACKED_PLAYERS || !game_in_progress())
		return;
	if (slot < NONE || slot >= MAXIMUM_WEAPONS_PER_UNIT)
		return;
	/* (a local game's server, which takes no one else, plays by a VR
	server's rules too: vr_rules_active) */
	if (game_connection() == _game_connection_local ||
		(game_connection() == _game_connection_network_server && (network_vr_server() || vr_rules_active())))
	{
		network_vr_drop_weapon(player_index, velocity, slot);
	}
	else if (game_connection() == _game_connection_network_client && network_vr_server())
	{
		csmemset(&message, 0, sizeof(message));
		message.entry.player_index = (byte)player_index;
		message.entry.slot = (byte)(slot + 1);
		distributed_vector_pack(velocity, NETWORK_VR_VELOCITY_SCALE, &message.entry.velocity);
		distributed_send(&message, _distributed_message_vr_drop, 1, sizeof(message), _distributed_to_host_reliably);
	}
}

void network_vr_handle_drop(long machine_index, void const *entries, short count, boolean stream)
{
	struct network_vr_drop_entry entry;
	real_vector3d velocity;

	if (count != 1 || !stream || !network_vr_server())
		return;
	csmemcpy(&entry, entries, sizeof(entry));
	if (entry.player_index >= MAXIMUM_TRACKED_PLAYERS || entry.slot > MAXIMUM_WEAPONS_PER_UNIT ||
		distributed_player_from_byte(entry.player_index) == NONE ||
		!distributed_machine_has_player(machine_index, entry.player_index))
	{
		return;
	}
	distributed_vector_unpack(&entry.velocity, NETWORK_VR_VELOCITY_SCALE, &velocity);
	if (network_vr_drop_weapon(entry.player_index, &velocity, (short)entry.slot - 1))
	{
		platform_log("network vr: player %d let their %s fall", entry.player_index,
			entry.slot ? "other hand's weapon" : "weapon");
	}
}

/* ---------- the grenade thrown with the arm */

/* a client's word to the host: its player's grenade thrown with the arm,
the hand's velocity and its place from the eyes as it let go (the
world's axes: world units a tick, world units) */
struct network_vr_throw_entry
{
	byte player_index;
	byte pad;
	struct distributed_vector velocity;
	struct distributed_vector offset;
};

typedef char network_vr_throw_entry_size_assert[sizeof(struct network_vr_throw_entry) == 14 ? 1 : -1];

struct network_vr_throw_message
{
	struct distributed_message_header header;
	struct network_vr_throw_entry entry;
};

word network_vr_throw_entry_size(void)
{
	return (word)sizeof(struct network_vr_throw_entry);
}

/* the throw kept for the player's next grenade: its place no further
than a hand's reach (a part not a number, or past any throw: none) */
static boolean network_vr_throw_take(short player_index, real_vector3d const *velocity, real_vector3d const *offset)
{
	real speed = (real)sqrt(velocity->i * velocity->i + velocity->j * velocity->j + velocity->k * velocity->k);
	real reach = (real)sqrt(offset->i * offset->i + offset->j * offset->j + offset->k * offset->k);

	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS || !(speed <= 1e3f) || !(reach <= 1e3f))
		return FALSE;
	network_vr_throws[player_index].velocity = *velocity;
	network_vr_throws[player_index].offset = *offset;
	if (reach > NETWORK_VR_HAND_REACH)
	{
		scale_vector3d(&network_vr_throws[player_index].offset, NETWORK_VR_HAND_REACH / reach,
			&network_vr_throws[player_index].offset);
	}
	network_vr_throws[player_index].time = game_time_get();
	return TRUE;
}

void network_vr_throw(long player, real_vector3d const *velocity, real_vector3d const *offset)
{
	struct network_vr_throw_message message;
	short player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player);

	if (player == NONE || player_index >= MAXIMUM_TRACKED_PLAYERS || !game_in_progress())
		return;
	/* (a local game's server, which takes no one else, plays by a VR
	server's rules too: vr_rules_active) */
	if (game_connection() == _game_connection_local ||
		(game_connection() == _game_connection_network_server && (network_vr_server() || vr_rules_active())))
	{
		network_vr_throw_take(player_index, velocity, offset);
	}
	else if (game_connection() == _game_connection_network_client && network_vr_server())
	{
		/* (kept here too, for a throw this machine predicts) */
		network_vr_throw_take(player_index, velocity, offset);
		csmemset(&message, 0, sizeof(message));
		message.entry.player_index = (byte)player_index;
		distributed_vector_pack(velocity, NETWORK_VR_VELOCITY_SCALE, &message.entry.velocity);
		distributed_vector_pack(offset, NETWORK_VR_VELOCITY_SCALE, &message.entry.offset);
		distributed_send(&message, _distributed_message_vr_throw, 1, sizeof(message), _distributed_to_host_reliably);
	}
}

void network_vr_handle_throw(long machine_index, void const *entries, short count, boolean stream)
{
	struct network_vr_throw_entry entry;
	real_vector3d velocity, offset;

	if (!stream || count != 1 || !network_vr_server())
		return;
	csmemcpy(&entry, entries, sizeof(entry));
	if (entry.player_index >= MAXIMUM_TRACKED_PLAYERS || distributed_player_from_byte(entry.player_index) == NONE ||
		!distributed_machine_has_player(machine_index, entry.player_index))
	{
		return;
	}
	distributed_vector_unpack(&entry.velocity, NETWORK_VR_VELOCITY_SCALE, &velocity);
	distributed_vector_unpack(&entry.offset, NETWORK_VR_VELOCITY_SCALE, &offset);
	network_vr_throw_take(entry.player_index, &velocity, &offset);
}

boolean network_vr_grenade_throw(long unit_index, real_point3d *origin, real_vector3d *velocity)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);
	short player_index;
	long age;
	real game_speed, speed, scale;
	real_point3d eyes, hand;
	real_vector3d thrown, offset;
	struct collision_result collision;

	if (!unit || unit->unit.player_index == NONE)
		return FALSE;
	player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(unit->unit.player_index);
	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS || network_vr_throws[player_index].time == NONE)
		return FALSE;
	age = game_time_get() - network_vr_throws[player_index].time;
	thrown = network_vr_throws[player_index].velocity;
	offset = network_vr_throws[player_index].offset;
	network_vr_throws[player_index].time = NONE;
	game_speed = (real)sqrt(velocity->i * velocity->i + velocity->j * velocity->j + velocity->k * velocity->k);
	speed = (real)sqrt(thrown.i * thrown.i + thrown.j * thrown.j + thrown.k * thrown.k);
	if (age < 0 || age > NETWORK_VR_THROW_TICKS || !(game_speed > 0.0f) || !(speed > 1e-4f))
		return FALSE;
	/* (the game's own for a firm throw, a softer or a harder one's in
	proportion, between the least and the most) */
	scale = PIN(speed / NETWORK_VR_THROW_FULL, NETWORK_VR_THROW_LEAST, NETWORK_VR_THROW_MOST) * game_speed / speed;
	scale_vector3d(&thrown, scale, velocity);
	/* (from the hand, unless a wall stands between it and the eyes) */
	unit_get_camera_position(unit_index, &eyes);
	point_from_line3d(&eyes, &offset, 1.0f, &hand);
	if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&eyes, &offset, unit_index, &collision))
	{
		*origin = hand;
	}
	platform_log("network vr: player %d's grenade thrown by the arm (%.3f, %.3f, %.3f a tick, the game's %.3f), "
		"from %.3f, %.3f, %.3f of the eyes", player_index, velocity->i, velocity->j, velocity->k, game_speed,
		origin->x - eyes.x, origin->y - eyes.y, origin->z - eyes.z);
	return TRUE;
}

boolean network_vr_carries_four(long unit_index)
{
	struct unit_datum *unit = unit_try_and_get(unit_index);

	if (!unit || unit->unit.player_index == NONE)
		return FALSE;
	if (distributed_player_is_local(unit->unit.player_index))
		return vr_render_carries_four(unit_index);
	return network_vr_server() && network_vr_pose_get(unit->unit.player_index) != NULL;
}
