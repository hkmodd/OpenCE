#include "harness.h"
#include "config.inc"
#include "header.inc"
/* The weapon a headset's player holds in the other hand, drawn as a
first-person weapon of its own (source/interface/first_person_weapons.c):
its index the local player's and FIRST_PERSON_WEAPON_OTHER_HAND more, the
unit's messages of that weapon sent to it, taken up as the hand takes it
(readied once, not each tick if it cannot be), let go of as the hand lets it
go, and effects and particles on it their local player's. */
struct first_person_weapon { long unit_index, weapon_index; boolean visible, rendered; };
/* (one more: an index past the local players' lands there, not on whatever lies after) */
static struct first_person_weapon local_weapons[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS+1];
static struct first_person_weapon *first_person_weapons = local_weapons;
static struct first_person_weapon *first_person_weapon_get(short local_player_index);
/* the local players (local player i is player i, its unit 100+i), their
units' weapons in hand (weapon 200+i, in slot 0), the weapons in their
other hands (NONE: none) and whether a weapon has a first-person model */
#define MAXIMUM_WEAPONS_PER_UNIT 4
struct player_datum { long unit_index; };
struct unit_datum { struct { short current_weapon_index; long weapon_object_indices[MAXIMUM_WEAPONS_PER_UNIT]; } unit; };
static struct player_datum players[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static struct unit_datum units[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static long other_weapons[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static boolean any_other;
static boolean drawable(long weapon_index){return weapon_index != 666;}
static long local_player_get_player_index(short local_player_index){return local_player_index < 2 ? local_player_index : NONE;}
static struct player_datum *player_get(long player_index){return &players[player_index];}
static struct unit_datum *unit_get(long unit_index){return &units[unit_index-100];}
static boolean dual_wield_any(void){return any_other;}
static long dual_wield_weapon(long unit_index){return other_weapons[unit_index-100];}
/* what each first-person weapon was told: readied (taken up), updated */
static int readied[NUMBER_OF_FIRST_PERSON_WEAPONS];
static int updated[NUMBER_OF_FIRST_PERSON_WEAPONS];
static void first_person_weapon_switch_weapons(short local_player_index)
{
 struct first_person_weapon *first_person_weapon = first_person_weapon_get(local_player_index);
 long weapon_index = NONE;
 first_person_weapon->weapon_index = NONE;
 if (first_person_weapon->unit_index != NONE)
 {
  weapon_index = local_player_index >= FIRST_PERSON_WEAPON_OTHER_HAND ? dual_wield_weapon(first_person_weapon->unit_index) :
   unit_get(first_person_weapon->unit_index)->unit.weapon_object_indices[0];
  if (weapon_index != NONE && drawable(weapon_index)) first_person_weapon->weapon_index = weapon_index;
 }
}
static void first_person_weapon_message(short local_player_index, short message_type)
{
 if (local_player_index != NONE && message_type == _first_person_weapon_message_ready)
 {
  readied[local_player_index]++;
  first_person_weapon_switch_weapons(local_player_index);
 }
}
static void first_person_weapon_update(short local_player_index){updated[local_player_index]++;}
#include "under_test.inc"

static void reset(void)
{
 short index;
 for (index = 0; index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; index++)
 {
  players[index].unit_index = 100+index;
  units[index].unit.current_weapon_index = 0;
  units[index].unit.weapon_object_indices[0] = 200+index;
  other_weapons[index] = NONE;
  local_weapons[index].unit_index = 100+index;
  local_weapons[index].weapon_index = 200+index;
  local_weapons[index].visible = TRUE;
  first_person_weapon_other_hands[index].unit_index = NONE;
  first_person_weapon_other_hands[index].weapon_index = NONE;
  first_person_weapon_other_hand_taken[index] = NONE;
 }
 memset(readied, 0, sizeof(readied));
 memset(updated, 0, sizeof(updated));
 any_other = FALSE;
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 reset();
 CASE("index") {
  /* the unit's messages of a weapon go to the first-person weapon that is it */
  other_weapons[1] = 301;
  CHECK(first_person_weapon_index_from_weapon_index(201)==1,"the weapon in hand: its local player's");
  CHECK(first_person_weapon_index_from_weapon_index(301)==NONE,"no weapon in an other hand anywhere: none");
  any_other = TRUE;
  CHECK(first_person_weapon_index_from_weapon_index(301)==1+FIRST_PERSON_WEAPON_OTHER_HAND,
   "the weapon in local player 1's other hand: %d",first_person_weapon_index_from_weapon_index(301));
  CHECK(first_person_weapon_index_from_weapon_index(200)==0,"local player 0's in hand still its own");
  CHECK(first_person_weapon_index_from_weapon_index(999)==NONE,"no one's: none");
 } else CASE("get") {
  /* each index its own weapon; a visible weapon found by its object, the
  other hand's too */
  short index;
  for (index = 0; index < NUMBER_OF_FIRST_PERSON_WEAPONS; index++)
  {
   struct first_person_weapon *expected = index < FIRST_PERSON_WEAPON_OTHER_HAND ? &local_weapons[index] :
    &first_person_weapon_other_hands[index-FIRST_PERSON_WEAPON_OTHER_HAND];
   CHECK(first_person_weapon_get(index)==expected,"index %d: its own weapon",index);
  }
  first_person_weapon_other_hands[0].weapon_index = 300;
  CHECK(first_person_weapon_get_local_index(300)==NONE,"the other hand's weapon not visible: not found");
  first_person_weapon_other_hands[0].visible = TRUE;
  CHECK(first_person_weapon_get_local_index(300)==FIRST_PERSON_WEAPON_OTHER_HAND,"visible: found at its index: %ld",
   first_person_weapon_get_local_index(300));
  CHECK(first_person_weapon_get_local_index(201)==1,"the weapon in hand still found");
  first_person_weapon_other_hands[3].weapon_index = 303;
  first_person_weapon_other_hands[3].visible = TRUE;
  CHECK(first_person_weapon_get_local_index(303)==3+FIRST_PERSON_WEAPON_OTHER_HAND,"the last other hand's found");
 } else CASE("owner") {
  /* an effect or a particle on either weapon is its local player's */
  CHECK(FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(1, 1) && FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(1+FIRST_PERSON_WEAPON_OTHER_HAND, 1),
   "local player 1's weapon in hand and in the other hand");
  CHECK(!FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(1+FIRST_PERSON_WEAPON_OTHER_HAND, 0),"not local player 0's");
  CHECK(!FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(NONE, 0) && !FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(NONE, 3),
   "no first-person weapon's: no one's");
  CHECK(!FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(NUMBER_OF_FIRST_PERSON_WEAPONS, 0),"past the last: no one's");
  CHECK(!FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(-FIRST_PERSON_WEAPON_OTHER_HAND, 0),"a negative index: no one's");
  CHECK(FIRST_PERSON_WEAPON_LOCAL_PLAYER(3+FIRST_PERSON_WEAPON_OTHER_HAND)==3,"the other hand's local player");
 } else CASE("take") {
  /* nothing while the hand holds none; taken up (readied once) as it takes
  one, updated each tick after; another weapon taken up again */
  short other = 1+FIRST_PERSON_WEAPON_OTHER_HAND;
  first_person_weapon_other_hand_update(1, 101);
  CHECK(readied[other]==0 && updated[other]==0 && first_person_weapon_other_hands[1].unit_index==NONE,
   "no weapon in the other hand: nothing");
  any_other = TRUE;
  other_weapons[1] = 301;
  first_person_weapon_other_hand_update(1, 101);
  CHECK(first_person_weapon_other_hands[1].unit_index==101 && first_person_weapon_other_hands[1].weapon_index==301,
   "taken up, of the unit: %ld",first_person_weapon_other_hands[1].weapon_index);
  CHECK(readied[other]==1 && updated[other]==1,"readied once, updated: %d %d",readied[other],updated[other]);
  first_person_weapon_other_hand_update(1, 101);
  CHECK(readied[other]==1 && updated[other]==2,"the next tick: updated only");
  other_weapons[1] = 302;
  first_person_weapon_other_hand_update(1, 101);
  CHECK(readied[other]==2 && first_person_weapon_other_hands[1].weapon_index==302,"another weapon: taken up");
  CHECK(readied[1]==0 && updated[1]==0,"the weapon in hand told nothing");
 } else CASE("undrawable") {
  /* a weapon with no first-person model is tried once, not each tick, and
  the next one is taken up */
  short other = FIRST_PERSON_WEAPON_OTHER_HAND, tick;
  any_other = TRUE;
  other_weapons[0] = 301;
  first_person_weapon_other_hand_update(0, 100);
  other_weapons[0] = 666;
  for (tick = 0; tick < 5; tick++) first_person_weapon_other_hand_update(0, 100);
  CHECK(readied[other]==2,"tried once: readied %d times",readied[other]);
  CHECK(first_person_weapon_other_hands[0].weapon_index==NONE,"none drawn");
  other_weapons[0] = 303;
  first_person_weapon_other_hand_update(0, 100);
  CHECK(readied[other]==3 && first_person_weapon_other_hands[0].weapon_index==303,"the next weapon taken up");
 } else CASE("let_go") {
  /* let go of as the hand lets it go, and nothing after; taken up anew */
  short other = FIRST_PERSON_WEAPON_OTHER_HAND;
  any_other = TRUE;
  other_weapons[0] = 301;
  first_person_weapon_other_hand_update(0, 100);
  other_weapons[0] = NONE;
  first_person_weapon_other_hand_update(0, 100);
  CHECK(first_person_weapon_other_hands[0].unit_index==NONE,"let go of");
  CHECK(updated[other]==1,"not updated once let go: %d",updated[other]);
  first_person_weapon_other_hand_update(0, 100);
  CHECK(updated[other]==1 && readied[other]==2,"nothing while the hand holds none: %d %d",updated[other],readied[other]);
  other_weapons[0] = 301;
  first_person_weapon_other_hand_update(0, 100);
  CHECK(first_person_weapon_other_hands[0].weapon_index==301 && updated[other]==2,"the same weapon taken up anew");
 } else CASE("unit") {
  /* a new unit (respawned, a body changed) takes its other hand's weapon up */
  short other = FIRST_PERSON_WEAPON_OTHER_HAND;
  any_other = TRUE;
  other_weapons[0] = 301;
  first_person_weapon_other_hand_update(0, 100);
  other_weapons[2] = 302;
  first_person_weapon_other_hand_update(0, 102);
  CHECK(first_person_weapon_other_hands[0].unit_index==102 && first_person_weapon_other_hands[0].weapon_index==302,
   "the new unit's weapon: %ld",first_person_weapon_other_hands[0].weapon_index);
  first_person_weapon_other_hand_update(0, NONE);
  CHECK(first_person_weapon_other_hands[0].unit_index==NONE && updated[other]==2,"no unit: let go of");
 } else return 2;
 return 0;
}
