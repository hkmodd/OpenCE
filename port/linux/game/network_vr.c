/* The headsets' players' poses in a VR server's game (network.vr_rules):
each VR player's machine sends its head and hands, from its unit, to the
host once a tick, unreliably (a lost one is overtaken by the next); the
host takes a pose only of a player of the machine that sent it, of a VR
server's game, newer than the last, its head near the unit, its hands near
the head and every axis whole, and sends it on to each client machine that
can see the player. Every machine keeps each player's latest, which is none
a second after it was sent: what the player's body is to be posed by. It
is only drawn: the game's hits, shots and movement are the unit's. */
#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "network_distributed.h"
#include "network_vr.h"
#include <string.h>

void platform_log(char const *format, ...);

/* a place's parts of 1/4096 of a world unit (8 world units each way) */
#define NETWORK_VR_PLACE_SCALE 4096.0f

/* a pose on the wire: its player, flags, and the head's and hands' places
and axes */
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
};

typedef char network_vr_pose_entry_size_assert[sizeof(struct network_vr_pose_entry) == 2 + 9 * 6 ? 1 : -1];

struct network_vr_pose_message
{
	struct distributed_message_header header;
	struct network_vr_pose_entry entry;
};

/* each player's latest pose and the tick it was sent at (NONE: none);
the tick this machine last sent its own at */
static struct
{
	long time;
	struct network_vr_pose pose;
} network_vr_poses[MAXIMUM_TRACKED_PLAYERS];
static long network_vr_sent_time = NONE;

void network_vr_reset(void)
{
	short index;

	for (index = 0; index < MAXIMUM_TRACKED_PLAYERS; index++)
		network_vr_poses[index].time = NONE;
	network_vr_sent_time = NONE;
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
		!distributed_axes_make_valid(&copy.head_forward, &copy.head_up))
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

/* a pose taken: kept if newer than the player's last (its axes made
whole), and told once a game */
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
	network_vr_poses[player_index].pose = *pose;
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
	age = game_time_get() - network_vr_poses[player_index].time;
	return age >= -NETWORK_VR_POSE_TICKS && age <= NETWORK_VR_POSE_TICKS ? &network_vr_poses[player_index].pose : NULL;
}
