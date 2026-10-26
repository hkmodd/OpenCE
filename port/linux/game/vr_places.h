/*
VR_PLACES.H

A headset's player's weapons in their places (vr_places.c), by a VR
server's rules with the hands as they hold: each slot of the unit's
inventory in a hand or at a shoulder.
*/

#ifndef VR_PLACES_H
#define VR_PLACES_H

/* the places: the hands (VR_HAND_LEFT, VR_HAND_RIGHT) and the shoulders
on their sides */
enum
{
	VR_PLACE_LEFT_HAND,
	VR_PLACE_RIGHT_HAND,
	VR_PLACE_LEFT_SHOULDER,
	VR_PLACE_RIGHT_SHOULDER,
	VR_PLACE_COUNT
};
#define VR_PLACE_SHOULDER(side) (VR_PLACE_LEFT_SHOULDER + (side))
/* the inventory's slots (MAXIMUM_WEAPONS_PER_UNIT) */
#define VR_PLACES_SLOTS 4

struct vr_places
{
	/* each place's slot (-1: empty) and the weapon it was (a slot taken
	again by another weapon is not the same) */
	short slot[VR_PLACE_COUNT];
	long weapon[VR_PLACE_COUNT];
	/* a weapon asked for by a hand (the host adds it): the hand, the
	weapon and the tick it was asked at (-1: none) */
	short pending_hand;
	long pending_weapon;
	long pending_time;
	/* a weapon let fall and when: no place takes it while the host drops
	it */
	long falling_weapon;
	long falling_time;
};

/* the game as this frame has it: the inventory's weapons (-1: none), the
weapon readied and to be (slots, -1: none), the other hand's
(dual_wield.c), one that must be readied (the flag), the main hand and
whether each hand holds, by the input (vr_input.c), and the time */
struct vr_places_game
{
	long inventory[VR_PLACES_SLOTS];
	short current;
	short desired;
	short dual;
	short forced;
	short main_hand;
	int off_holding;
	long now;
	long ticks_per_second;
};

/* what vr_view.c is to do: the slot to ready in the main hand
(player_control_set_desired_weapon) and the hand that is then the main one
(it took the weapon with the main hand empty), the slot the other hand
takes (dual_wield_begin), the other hand to let go of its weapon
(dual_wield_end), and the main hand empty (bare); -1 or 0: nothing */
struct vr_places_orders
{
	short ready;
	short ready_hand;
	short off_take;
	int off_end;
	int bare;
};

void vr_places_clear(struct vr_places *places);
/* a new unit's: the weapon readied in the main hand, the others at the
shoulders, the main hand's side first */
void vr_places_spawn(struct vr_places *places, struct vr_places_game const *game);
/* the places made to agree with the game, and what to do for the game to
agree with them */
void vr_places_reconcile(struct vr_places *places, struct vr_places_game const *game,
	struct vr_places_orders *orders);
/* the hand's weapon to the shoulder on the side and the shoulder's to the
hand (either may be none); the slot that comes to the hand (-1: none) */
short vr_places_exchange(struct vr_places *places, short hand, short side);
/* the side whose shoulder the switch button exchanges the main hand with:
its own if it keeps a weapon, else the other's, else -1 */
short vr_places_switch_side(struct vr_places const *places, short main_hand);

#endif
