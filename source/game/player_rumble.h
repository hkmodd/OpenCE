/*
PLAYER_RUMBLE.H
*/

#ifndef __PLAYER_RUMBLE_H
#define __PLAYER_RUMBLE_H
#pragma once

/* ---------- headers */

#include "cseries/cseries.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	NUMBER_OF_RUMBLE_MOTORS = 2,
};

/* port: what an impulse is felt in: the body (hits, blasts: the motors as
always), or the shots of the weapon in hand or of the one in a headset's
other hand (dual_wield.c), which its controllers feel in the hand that
holds it (rumble_update, vr_input.c's vr_rumble_hands) */
enum
{
	_rumble_hand_body = 0,
	_rumble_hand_weapon,
	_rumble_hand_other,
	NUMBER_OF_RUMBLE_HANDS
};

/* ---------- structures */

struct rumble_motor
{
	real scale;
	real duration;
	short transition_function;
	short pad;
	real unused[2];
};

struct rumble_definition
{
	struct rumble_motor motors[NUMBER_OF_RUMBLE_MOTORS];
	real scale_floor;
	/* port: an impulse's (rumble_player_impulse's copy): what it is felt
	in, a _rumble_hand_* (unused in the tags) */
	byte hand;
	byte unused[15];
};

/* ---------- prototypes/PLAYER_RUMBLE.C */

void rumble_initialize(
	void);
void rumble_dispose(
	void);
void rumble_initialize_for_new_map(
	void);
void rumble_player_set_scripted_values(
	real left_motor,
	real right_motor);
void rumble_player_set_scale(
	real scale);
void rumble_player_impulse(
	short local_player_index,
	struct rumble_definition *rumble_definition,
	real scale,
	real duration_scale);
void rumble_player_clear(
	short local_player_index);
void rumble_clear_all_now(
	void);
void rumble_player_continuous(
	short local_player_index,
	real left_motor,
	real right_motor);
void rumble_dispose_from_old_map(
	void);
void rumble_update(
	void);
/* port: the impulses started from now on are felt in this (a
_rumble_hand_*: weapons.c, about a shot's own damage effect) */
void rumble_set_impulse_hand(
	short hand);

#endif // __PLAYER_RUMBLE_H
