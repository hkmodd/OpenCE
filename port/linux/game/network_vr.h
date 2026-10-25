#ifndef NETWORK_VR_H
#define NETWORK_VR_H
#include "cseries.h"
/* the headsets' players' heads and hands in a VR server's game
(network.vr_rules, network_vr.c): each VR player's machine sends its pose
to the host once a tick, which checks it and sends it on to the machines
that can see that player, so that each draws the player's body as its
headset poses it; a pose not heard for a second is none */

/* a pose: the head's and each hand's place from the unit's (its
position, world units, along the world's axes) and their axes; a hand's
only with its bit; and the slot of the weapon the other hand holds
(dual_wield.c; NONE: none) and that hand's trigger (0 to 1) */
enum
{
	_network_vr_pose_left_hand_bit,
	_network_vr_pose_right_hand_bit,
	/* the left hand holds the weapon */
	_network_vr_pose_left_handed_bit,
	/* no hand holds the weapon (the hands bare) */
	_network_vr_pose_bare_bit,
	NUMBER_OF_NETWORK_VR_POSE_BITS
};

struct network_vr_pose
{
	byte flags;
	real_vector3d head;
	real_vector3d head_forward;
	real_vector3d head_up;
	real_vector3d hands[2];
	real_vector3d hand_forwards[2];
	real_vector3d hand_ups[2];
	short off_slot;
	real off_trigger;
};

/* how far from the unit a head may be, and a hand from the head (world
units: 1.5 m and 1.2 m) */
#define NETWORK_VR_HEAD_REACH 0.5f
#define NETWORK_VR_HAND_REACH 0.4f
/* a pose's life: the ticks after it was taken */
#define NETWORK_VR_POSE_TICKS 30
/* a weapon let fall: the fastest it is thrown (world units a tick: 27
m/s), and the ticks before a player's next */
#define NETWORK_VR_DROP_SPEED 0.3f
#define NETWORK_VR_DROP_TICKS 15
/* a grenade thrown with the arm: the ticks the game's throw that follows
may take it in, and its speed: the game's own throw's for a hand's firm
throw, NETWORK_VR_THROW_FULL (world units a tick: 7.5 m/s; the game's
throw, 30 m/s by the world's metres, is no hand's), a softer or a harder
one's in proportion, between NETWORK_VR_THROW_LEAST and
NETWORK_VR_THROW_MOST of the game's */
#define NETWORK_VR_THROW_TICKS 30
#define NETWORK_VR_THROW_FULL 0.082f
#define NETWORK_VR_THROW_LEAST 0.4f
#define NETWORK_VR_THROW_MOST 1.25f

void network_vr_reset(void);
/* any player's pose was taken this game (none: no body to pose) */
boolean network_vr_poses_kept(void);
/* (a VR player's machine, once a tick) this machine's player's pose, of
its unit (player: the player's datum index) */
void network_vr_send(long player, struct network_vr_pose const *pose);
/* a wire entry's size, and its two messages' handlers: a client's pose to
the host, and the host's to a client */
word network_vr_entry_size(void);
void network_vr_handle_up(long machine_index, long game_time, void const *entries, short count);
void network_vr_handle_down(long game_time, void const *entries, short count);
/* the pose of a player to draw (another machine's: a VR server's), NULL
for none; what it says the other hand holds, every machine's copy of the
unit holds there (dual_wield.c) */
struct network_vr_pose const *network_vr_pose_get(long player);
/* whether a pose is fit to take: its head near the unit, its hands near
the head, its axes whole */
boolean network_vr_pose_valid(struct network_vr_pose const *pose);
/* units.c: where another machine's headset's player's melee strike goes:
through the hand of its pose farther ahead of the head (the one that
struck; the world's); FALSE for none (no pose: the unit's aim) */
boolean network_vr_melee_point(long unit_index, real_point3d *point);
/* (a headset's player's machine) the weapon in the player's hand (slot
NONE), or the other hand's (its slot), let fall, thrown with a velocity
(the world's axes, world units a tick): by a game with no network itself,
by a VR server's host when it asks, which checks it; nothing on another
server */
void network_vr_drop(long player, real_vector3d const *velocity, short slot);
/* the request's entry size and its handler (the host: over the
machine's stream alone) */
word network_vr_drop_entry_size(void);
void network_vr_handle_drop(long machine_index, void const *entries, short count, boolean stream);
/* (players.c) the unit carries four weapons, not two, those past the
second taken by the hand (a swap that drops none): this machine's
headset's player's by a VR server's rules (or in a game with no
network), or a VR server's player whose pose comes */
boolean network_vr_carries_four(long unit_index);
/* (a headset's player's machine) the player's grenade thrown with the
arm (vr.throw_gesture): the hand's velocity and its place from the eyes as
it let go (the world's axes: world units a tick, world units), for the
game's throw that follows: kept by a game with no network itself, by a
VR server's host when it is told, which checks it; nothing on another
server */
void network_vr_throw(long player, real_vector3d const *velocity, real_vector3d const *offset);
/* the word's entry size and its handler (the host: over the machine's
stream alone) */
word network_vr_throw_entry_size(void);
void network_vr_handle_throw(long machine_index, void const *entries, short count, boolean stream);
/* (units.c, the game's throw of a player's grenade) a throw with the arm
kept for the unit's player (taken once): `velocity`, the game's, becomes
the hand's way, as fast as the hand (between the least and the most of
the game's), and `origin` the hand's place unless a wall stands between
it and the eyes; FALSE (both untouched) if none is kept */
boolean network_vr_grenade_throw(long unit_index, real_point3d *origin, real_vector3d *velocity);
#endif
