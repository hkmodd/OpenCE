/*
VR_PLACES.C

A headset's player's weapons in their places, by a VR server's rules with
the hands as they hold ("what you see is what you have"): four places, the
two hands and the two shoulders, each holding a slot of the inventory or
none. A weapon comes to a hand only as the hand takes it from the ground,
draws it from a shoulder or exchanges one for it there, or as the switch
button exchanges the main hand's; the game readies none by itself, and one
it readies on its own (another dropped, a script's) stays at its place, the
main hand empty (vr_places_reconcile). The main hand's weapon is the one
the game holds; the other hand's is dual_wield.c's. Pure: vr_view.c gives
it the game's state and carries out its orders.
*/

#include "vr_places.h"

/* the place that holds the slot (-1: none) */
static short vr_places_find(
	struct vr_places const *places,
	short slot)
{
	short place;

	for (place = 0; place < VR_PLACE_COUNT; place++)
	{
		if (places->slot[place] == slot)
			return place;
	}
	return -1;
}

static void vr_places_put(
	struct vr_places *places,
	short place,
	short slot,
	long weapon)
{
	places->slot[place] = slot;
	places->weapon[place] = slot < 0 ? -1 : weapon;
}

/* a shoulder that keeps nothing, the main hand's side first (-1: none) */
static short vr_places_free_shoulder(
	struct vr_places const *places,
	short main_hand)
{
	if (places->slot[VR_PLACE_SHOULDER(main_hand)] < 0)
		return VR_PLACE_SHOULDER(main_hand);
	if (places->slot[VR_PLACE_SHOULDER(!main_hand)] < 0)
		return VR_PLACE_SHOULDER(!main_hand);
	return -1;
}

/* the weapon let fall in the last second: no place takes it while the
host drops it */
static int vr_places_falling(
	struct vr_places const *places,
	struct vr_places_game const *game,
	long weapon)
{
	return weapon == places->falling_weapon && game->now >= places->falling_time &&
		game->now - places->falling_time < game->ticks_per_second;
}

void vr_places_clear(
	struct vr_places *places)
{
	short place;

	for (place = 0; place < VR_PLACE_COUNT; place++)
		vr_places_put(places, place, -1, -1);
	places->pending_hand = -1;
	places->pending_weapon = -1;
	places->pending_time = 0;
	places->falling_weapon = -1;
	places->falling_time = 0;
}

void vr_places_spawn(
	struct vr_places *places,
	struct vr_places_game const *game)
{
	short readied = game->desired >= 0 ? game->desired : game->current;
	short slot, place;

	vr_places_clear(places);
	if (readied >= 0 && readied < VR_PLACES_SLOTS && game->inventory[readied] != -1)
		vr_places_put(places, game->main_hand, readied, game->inventory[readied]);
	for (slot = 0; slot < VR_PLACES_SLOTS; slot++)
	{
		if (game->inventory[slot] == -1 || vr_places_find(places, slot) >= 0)
			continue;
		place = vr_places_free_shoulder(places, game->main_hand);
		if (place >= 0)
			vr_places_put(places, place, slot, game->inventory[slot]);
	}
}

void vr_places_reconcile(
	struct vr_places *places,
	struct vr_places_game const *game,
	struct vr_places_orders *orders)
{
	short main_hand = game->main_hand, off, place, slot, readied, main_slot;

	orders->ready = -1;
	orders->ready_hand = -1;
	orders->off_take = -1;
	orders->off_end = 0;
	orders->bare = 0;
	/* (a place whose weapon is gone, or another's in its slot, is empty) */
	for (place = 0; place < VR_PLACE_COUNT; place++)
	{
		slot = places->slot[place];
		if (slot >= 0 && (slot >= VR_PLACES_SLOTS || game->inventory[slot] != places->weapon[place]))
			vr_places_put(places, place, -1, -1);
	}
	/* (a weapon asked for by a hand, come: in that hand, the main one if the
	main hand holds none; refused if none comes in a second and a half) */
	if (places->pending_hand >= 0)
	{
		for (slot = 0; slot < VR_PLACES_SLOTS; slot++)
		{
			if (game->inventory[slot] == places->pending_weapon && vr_places_find(places, slot) < 0)
				break;
		}
		if (slot < VR_PLACES_SLOTS)
		{
			short hand = places->pending_hand;

			places->pending_hand = -1;
			if (places->slot[main_hand] < 0 && places->slot[hand] < 0)
			{
				vr_places_put(places, hand, slot, game->inventory[slot]);
				orders->ready = slot;
				orders->ready_hand = hand;
				main_hand = hand;
			}
			else if (hand != main_hand && places->slot[hand] < 0)
			{
				vr_places_put(places, hand, slot, game->inventory[slot]);
				orders->off_take = slot;
			}
		}
		else if (game->now < places->pending_time ||
			game->now - places->pending_time > game->ticks_per_second * 3 / 2)
		{
			places->pending_hand = -1;
		}
	}
	off = !main_hand;
	/* (the main hand's weapon gone while the other hand holds one: that one
	is the main hand's, as when the main hand lets go) */
	if (places->slot[main_hand] < 0 && places->slot[off] >= 0 && orders->ready < 0 && orders->off_take < 0)
	{
		orders->off_end = game->dual == places->slot[off];
		orders->ready = places->slot[off];
		orders->ready_hand = off;
		main_hand = off;
		off = !main_hand;
	}
	/* (the other hand's: what dual_wield.c holds; let go, empty) */
	if (places->slot[off] >= 0 && orders->off_take < 0 && (!game->off_holding || game->dual != places->slot[off]))
	{
		orders->off_end = game->dual == places->slot[off];
		vr_places_put(places, off, -1, -1);
	}
	/* (the main hand's: one the game readied by itself new to the places, or
	that must be readied, comes to it, its own going to a shoulder; else the
	hand's own readied again, or, the hand holding none, the one readied
	stays where it is) */
	readied = game->desired >= 0 ? game->desired : game->current;
	main_slot = places->slot[main_hand];
	if (orders->ready < 0 && readied >= 0 && readied < VR_PLACES_SLOTS && readied != main_slot &&
		game->inventory[readied] != -1 && !vr_places_falling(places, game, game->inventory[readied]))
	{
		place = vr_places_find(places, readied);
		if (place < 0 || readied == game->forced)
		{
			if (place >= 0)
				vr_places_put(places, place, -1, -1);
			if (main_slot >= 0)
			{
				short kept = vr_places_free_shoulder(places, main_hand);

				if (kept < 0 && places->slot[off] < 0)
				{
					kept = off;
					orders->off_take = main_slot;
				}
				if (kept >= 0)
					vr_places_put(places, kept, main_slot, game->inventory[main_slot]);
			}
			vr_places_put(places, main_hand, readied, game->inventory[readied]);
			orders->ready_hand = main_hand;
		}
		else if (main_slot >= 0)
		{
			orders->ready = main_slot;
			orders->ready_hand = main_hand;
		}
	}
	/* (a slot no place holds, not falling nor asked for: to a shoulder that
	keeps none, the main hand's side first, else the main hand, readied,
	else the other hand) */
	for (slot = 0; slot < VR_PLACES_SLOTS; slot++)
	{
		if (game->inventory[slot] == -1 || vr_places_find(places, slot) >= 0 ||
			vr_places_falling(places, game, game->inventory[slot]) ||
			(places->pending_hand >= 0 && game->inventory[slot] == places->pending_weapon))
		{
			continue;
		}
		place = vr_places_free_shoulder(places, main_hand);
		if (place < 0 && places->slot[main_hand] < 0 && orders->ready < 0)
		{
			place = main_hand;
			orders->ready = slot;
			orders->ready_hand = main_hand;
		}
		else if (place < 0 && places->slot[main_hand] >= 0 && places->slot[off] < 0 && orders->off_take < 0)
		{
			place = off;
			orders->off_take = slot;
		}
		if (place >= 0)
			vr_places_put(places, place, slot, game->inventory[slot]);
	}
	orders->bare = places->slot[main_hand] < 0;
}

short vr_places_exchange(
	struct vr_places *places,
	short hand,
	short side)
{
	short shoulder = VR_PLACE_SHOULDER(side);
	short slot = places->slot[shoulder];
	long weapon = places->weapon[shoulder];

	vr_places_put(places, shoulder, places->slot[hand], places->weapon[hand]);
	vr_places_put(places, hand, slot, weapon);
	return slot;
}

short vr_places_switch_side(
	struct vr_places const *places,
	short main_hand)
{
	if (places->slot[VR_PLACE_SHOULDER(main_hand)] >= 0)
		return main_hand;
	if (places->slot[VR_PLACE_SHOULDER(!main_hand)] >= 0)
		return (short)!main_hand;
	return -1;
}
