#include "harness.h"
/* A headset's player's weapons in their places (port/linux/game/vr_places.c):
two hands and two shoulders, each a slot of the inventory or none. A new
unit's readied weapon in the main hand and the others at the shoulders,
the main hand's side first; a weapon asked for by a hand comes to that
hand (the main one's weapon staying in it: the reported bug); the exchange
at a shoulder; the switch button's side; what the game readies by itself
stays where it is, but for a new weapon or one that must be readied. */
#include "vr_places.h"
#include "under_test.inc"

enum { LEFT = VR_PLACE_LEFT_HAND, RIGHT = VR_PLACE_RIGHT_HAND,
	LEFT_SHOULDER = VR_PLACE_LEFT_SHOULDER, RIGHT_SHOULDER = VR_PLACE_RIGHT_SHOULDER };

static struct vr_places places;
static struct vr_places_game game;
static struct vr_places_orders orders;

/* a game of the weapons 100 + slot in the slots given (the rest none),
the slot readied, right-handed */
static void world(int count, short readied)
{
	short slot;

	memset(&game, 0, sizeof(game));
	for (slot = 0; slot < VR_PLACES_SLOTS; slot++)
		game.inventory[slot] = slot < count ? 100 + slot : -1;
	game.current = game.desired = readied;
	game.dual = game.forced = -1;
	game.main_hand = RIGHT;
	game.now = 1000;
	game.ticks_per_second = 30;
}

static void reconcile(void)
{
	vr_places_reconcile(&places, &game, &orders);
}

/* every weapon the game has in exactly one place, none twice; the other
hand holding one only while the main one does */
static int consistent(void)
{
	short slot, place, count;

	for (slot = 0; slot < VR_PLACES_SLOTS; slot++)
	{
		for (count = 0, place = 0; place < VR_PLACE_COUNT; place++)
			count += places.slot[place] == slot;
		if (count > 1 || (game.inventory[slot] != -1 && count != 1 && places.pending_hand < 0 &&
			game.inventory[slot] != places.falling_weapon))
		{
			return 0;
		}
	}
	return places.slot[!game.main_hand] < 0 || places.slot[game.main_hand] >= 0;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("spawn") {
		world(3, 1);
		vr_places_spawn(&places, &game);
		CHECK(places.slot[RIGHT] == 1, "the readied weapon in the main hand");
		CHECK(places.slot[RIGHT_SHOULDER] == 0, "the first other at the main hand's shoulder");
		CHECK(places.slot[LEFT_SHOULDER] == 2, "the next at the other's");
		CHECK(places.slot[LEFT] == -1, "the other hand empty");
		world(2, 0);
		game.main_hand = LEFT;
		vr_places_spawn(&places, &game);
		CHECK(places.slot[LEFT] == 0 && places.slot[LEFT_SHOULDER] == 1, "left-handed, mirrored");
	} else CASE("pending_off") {
		/* (the right hand holds slot 0, the left asks for a weapon: the host
		adds it in slot 1) */
		world(1, 0);
		vr_places_spawn(&places, &game);
		places.pending_hand = LEFT;
		places.pending_weapon = 555;
		places.pending_time = game.now;
		reconcile();
		CHECK(orders.off_take == -1 && places.pending_hand == LEFT, "not come yet: waits");
		game.inventory[1] = 555;
		game.now++;
		reconcile();
		CHECK(places.slot[LEFT] == 1 && orders.off_take == 1, "in the left hand, held there");
		CHECK(orders.ready == -1 && places.slot[RIGHT] == 0, "the right hand's weapon stays in it");
		CHECK(places.pending_hand == -1 && consistent(), "asked for no more");
	} else CASE("pending_main") {
		world(1, 0);
		vr_places_spawn(&places, &game);
		vr_places_exchange(&places, RIGHT, RIGHT);
		CHECK(places.slot[RIGHT] == -1 && places.slot[RIGHT_SHOULDER] == 0, "kept at the shoulder");
		reconcile();
		CHECK(orders.bare && orders.ready == -1, "the main hand empty, nothing readied");
		places.pending_hand = RIGHT;
		places.pending_weapon = 555;
		places.pending_time = game.now;
		game.inventory[1] = 555;
		reconcile();
		CHECK(places.slot[RIGHT] == 1 && orders.ready == 1 && orders.ready_hand == RIGHT, "readied in that hand");
		CHECK(!orders.bare && consistent(), "the hand holds it");
	} else CASE("pending_off_main_empty") {
		/* (no weapon at all: the left hand takes one, and is the main one) */
		world(0, -1);
		vr_places_spawn(&places, &game);
		places.pending_hand = LEFT;
		places.pending_weapon = 555;
		places.pending_time = game.now;
		game.inventory[0] = 555;
		game.current = game.desired = 0;
		reconcile();
		CHECK(places.slot[LEFT] == 0 && orders.ready == 0 && orders.ready_hand == LEFT, "readied in the left hand");
		CHECK(orders.off_take == -1 && !orders.off_end, "not as the other hand's");
		CHECK(!orders.bare, "the hand that took it is the main one");
		game.main_hand = LEFT;
		reconcile();
		CHECK(places.slot[LEFT] == 0 && orders.ready == -1 && !orders.bare && consistent(), "kept so");
	} else CASE("refused") {
		world(1, 0);
		vr_places_spawn(&places, &game);
		places.pending_hand = LEFT;
		places.pending_weapon = 555;
		places.pending_time = game.now;
		game.now += 45;
		reconcile();
		CHECK(places.pending_hand == LEFT, "still waited for at a second and a half");
		game.now += 1;
		reconcile();
		CHECK(places.pending_hand == -1 && orders.off_take == -1 && orders.ready == -1, "refused after");
		game.inventory[1] = 555;
		reconcile();
		CHECK(places.slot[LEFT] == -1 && places.slot[RIGHT_SHOULDER] == 1, "come late: to a shoulder");
	} else CASE("exchange") {
		world(3, 0);
		vr_places_spawn(&places, &game);
		CHECK(vr_places_exchange(&places, RIGHT, LEFT) == 2, "the shoulder's comes to the hand");
		CHECK(places.slot[RIGHT] == 2 && places.slot[LEFT_SHOULDER] == 0, "and the hand's to the shoulder");
		world(2, 0);
		vr_places_spawn(&places, &game);
		CHECK(vr_places_exchange(&places, RIGHT, LEFT) == -1 && places.slot[RIGHT] == -1 &&
			places.slot[LEFT_SHOULDER] == 0, "a shoulder keeping none: kept there, the hand empty");
		CHECK(vr_places_exchange(&places, LEFT, LEFT) == 0 && places.slot[LEFT] == 0 &&
			places.slot[LEFT_SHOULDER] == -1, "an empty hand: drawn");
		CHECK(vr_places_exchange(&places, LEFT, RIGHT) == 1 && places.slot[LEFT] == 1 &&
			places.slot[RIGHT_SHOULDER] == 0, "the other hand at the other shoulder");
	} else CASE("switch_side") {
		world(2, 0);
		vr_places_spawn(&places, &game);
		CHECK(vr_places_switch_side(&places, RIGHT) == RIGHT, "the main hand's own side");
		vr_places_exchange(&places, LEFT, RIGHT);
		vr_places_exchange(&places, LEFT, LEFT);
		CHECK(places.slot[LEFT_SHOULDER] == 1 && places.slot[RIGHT_SHOULDER] == -1, "moved to the other shoulder");
		CHECK(vr_places_switch_side(&places, RIGHT) == LEFT, "its own side keeping none: the other's");
		vr_places_exchange(&places, LEFT, LEFT);
		CHECK(vr_places_switch_side(&places, RIGHT) == -1, "none keeping any: none");
	} else CASE("game_readies_shoulder") {
		/* (the main hand's weapon gone: the game readies the next, kept at a
		shoulder) */
		world(2, 0);
		vr_places_spawn(&places, &game);
		game.inventory[0] = -1;
		game.current = game.desired = 1;
		reconcile();
		CHECK(places.slot[RIGHT] == -1 && places.slot[RIGHT_SHOULDER] == 1, "it stays at its shoulder");
		CHECK(orders.bare && orders.ready == -1 && orders.ready_hand == -1, "the main hand empty");
	} else CASE("game_readies_new") {
		/* (a script gives a weapon and readies it) */
		world(2, 0);
		vr_places_spawn(&places, &game);
		game.inventory[2] = 102;
		game.current = game.desired = 2;
		reconcile();
		CHECK(places.slot[RIGHT] == 2 && orders.ready_hand == RIGHT, "the new one in the main hand");
		CHECK(places.slot[LEFT_SHOULDER] == 0 && places.slot[RIGHT_SHOULDER] == 1, "the hand's to a free shoulder");
		CHECK(!orders.bare && consistent(), "all placed");
	} else CASE("forced") {
		/* (the flag, which must be readied, at a shoulder: to the hand) */
		world(2, 0);
		vr_places_spawn(&places, &game);
		game.forced = 1;
		game.current = game.desired = 1;
		reconcile();
		CHECK(places.slot[RIGHT] == 1 && places.slot[RIGHT_SHOULDER] == 0, "the flag in the hand, the weapon kept");
	} else CASE("seat_change") {
		/* (the game readied a shoulder's weapon while the hand's is still
		had: the hand's readied again) */
		world(2, 0);
		vr_places_spawn(&places, &game);
		game.desired = 1;
		reconcile();
		CHECK(orders.ready == 0 && orders.ready_hand == RIGHT, "the hand's own readied again");
		CHECK(places.slot[RIGHT] == 0 && places.slot[RIGHT_SHOULDER] == 1, "nothing moved");
	} else CASE("off_let_go") {
		world(2, 0);
		vr_places_spawn(&places, &game);
		vr_places_exchange(&places, LEFT, RIGHT);
		game.dual = 1;
		game.off_holding = 1;
		reconcile();
		CHECK(places.slot[LEFT] == 1 && !orders.off_end, "held by the other hand");
		game.off_holding = 0;
		reconcile();
		CHECK(orders.off_end && places.slot[LEFT] == -1, "let go: the other hand lets go of it");
		CHECK(places.slot[RIGHT_SHOULDER] == 1 && consistent(), "kept at a free shoulder");
	} else CASE("falling") {
		world(2, 0);
		vr_places_spawn(&places, &game);
		places.falling_weapon = places.weapon[RIGHT];
		places.falling_time = game.now;
		places.slot[RIGHT] = -1;
		reconcile();
		CHECK(places.slot[RIGHT] == -1 && places.slot[LEFT_SHOULDER] == -1, "the falling one not placed");
		CHECK(places.slot[RIGHT_SHOULDER] == 1 && orders.bare, "the main hand empty");
		game.now += 31;
		reconcile();
		CHECK(places.slot[RIGHT] == 0 && !orders.bare, "still had a second on: back in the hand, readied");
	} else CASE("invariants") {
		unsigned seed = 12345;
		int step;

		world(2, 0);
		vr_places_spawn(&places, &game);
		for (step = 0; step < 20000; step++)
		{
			unsigned choice;
			short slot;

			seed = seed * 1103515245u + 12345u;
			choice = (seed >> 16) % 7;
			slot = (short)((seed >> 8) % VR_PLACES_SLOTS);
			if (choice == 0)
				vr_places_exchange(&places, (short)((seed >> 4) & 1), (short)((seed >> 5) & 1));
			else if (choice == 1)
				game.inventory[slot] = -1;
			else if (choice == 2 && game.inventory[slot] == -1)
				game.inventory[slot] = 200 + step;
			else if (choice == 3)
				game.desired = places.slot[game.main_hand];
			else if (choice == 4)
				game.desired = slot;
			else if (choice == 5)
				game.off_holding = !game.off_holding;
			else
				game.dual = places.slot[!game.main_hand];
			if (places.slot[!game.main_hand] >= 0 && places.slot[game.main_hand] < 0)
				vr_places_exchange(&places, game.main_hand, (short)((seed >> 6) & 1));
			game.now++;
			reconcile();
			if (orders.ready_hand >= 0)
				game.main_hand = orders.ready_hand;
			if (orders.off_take >= 0)
			{
				game.dual = orders.off_take;
				game.off_holding = 1;
			}
			CHECK(consistent(), "step %d (choice %u): places %d %d %d %d, inventory %ld %ld %ld %ld, main %d, "
				"desired %d, dual %d, off holding %d, pending %d", step, choice, places.slot[0], places.slot[1],
				places.slot[2], places.slot[3], game.inventory[0], game.inventory[1], game.inventory[2],
				game.inventory[3], game.main_hand, game.desired, game.dual, game.off_holding, places.pending_hand);
		}
	} else return 2;
	return 0;
}
