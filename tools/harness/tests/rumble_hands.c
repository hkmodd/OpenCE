#include "harness.h"
typedef unsigned short word;
#define csmemset memset
#define fast_ftol(value) ((long)(value))
#define PIN(n,floor,ceiling) ((n)<(floor) ? (floor) : ((n)>(ceiling) ? (ceiling) : (n)))
#include "config.inc"
/* What each rumble impulse is felt in (source/game/player_rumble.c): the
body, or the shots of the weapon in hand or of the one in a headset's other
hand (weapons.c says which about a shot's own damage effect); the motors of
the impulses felt in one hand alone, the continuous and the scripted
rumble the body's, and every impulse's as always for any other gamepad. */
static real transition_function_evaluate(short function, real value){(void)function; return value;}
static struct rumble_globals globals;
static struct rumble_globals *rumble_globals = &globals;
#include "under_test.inc"

static struct rumble_definition shot(real left, real right)
{
 struct rumble_definition definition;
 memset(&definition, 0xCD, sizeof(definition));
 definition.motors[0].scale = left;
 definition.motors[0].duration = 1.0f;
 definition.motors[0].transition_function = 0;
 definition.motors[1].scale = right;
 definition.motors[1].duration = 1.0f;
 definition.motors[1].transition_function = 0;
 definition.scale_floor = 1.0f;
 return definition;
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 struct rumble_player *player = &rumble_globals->players[0];
 memset(&globals, 0, sizeof(globals));
 CASE("hands") {
  /* each weapon's shot felt in its own hand, everything as always */
  struct rumble_definition weapon = shot(0.0f, 0.5f), other = shot(0.25f, 0.0f), blast = shot(0.75f, 0.0f);
  struct rumble_motor_values values;
  short slot;
  for (slot = 0; slot < MAXIMUM_RUMBLE_IMPULSES; slot++) player->impulse_time[slot] = 2.0f;
  rumble_set_impulse_hand(_rumble_hand_weapon);
  rumble_player_impulse(0, &weapon, 1.0f, 1.0f);
  rumble_set_impulse_hand(_rumble_hand_other);
  rumble_player_impulse(0, &other, 1.0f, 1.0f);
  rumble_set_impulse_hand(_rumble_hand_body);
  rumble_player_impulse(0, &blast, 1.0f, 1.0f);
  values = rumble_calculate(player, _rumble_hand_weapon);
  CHECK(values.left == 0 && values.right == 32767, "the weapon in hand's shot alone: %u %u", values.left, values.right);
  values = rumble_calculate(player, _rumble_hand_other);
  CHECK(values.left == 16383 && values.right == 0, "the other hand's shot alone: %u %u", values.left, values.right);
  values = rumble_calculate(player, _rumble_hand_body);
  CHECK(values.left == 49151 && values.right == 0, "the blast alone: %u %u", values.left, values.right);
  values = rumble_calculate(player, NONE);
  CHECK(values.left == 65535 && values.right == 32767, "every impulse, as always: %u %u", values.left, values.right);
 } else CASE("body") {
  /* the continuous and the scripted rumble are the body's */
  struct rumble_motor_values values;
  short slot;
  for (slot = 0; slot < MAXIMUM_RUMBLE_IMPULSES; slot++) player->impulse_time[slot] = 2.0f;
  player->continuous_left = 0.5f;
  rumble_globals->scripted_right_motor = 0.25f;
  rumble_globals->scripted_scale = 1.0f;
  values = rumble_calculate(player, _rumble_hand_body);
  CHECK(values.left == 32767 && values.right == 16383, "the body's: %u %u", values.left, values.right);
  values = rumble_calculate(player, NONE);
  CHECK(values.left == 32767 && values.right == 16383, "as always: %u %u", values.left, values.right);
  values = rumble_calculate(player, _rumble_hand_weapon);
  CHECK(values.left == 0 && values.right == 0, "not a hand's: %u %u", values.left, values.right);
  values = rumble_calculate(player, _rumble_hand_other);
  CHECK(values.left == 0 && values.right == 0, "nor the other's: %u %u", values.left, values.right);
 } else CASE("tag") {
  /* a tag's unused bytes do not say what an impulse is felt in */
  struct rumble_definition blast = shot(0.75f, 0.0f);
  struct rumble_motor_values values;
  short slot;
  for (slot = 0; slot < MAXIMUM_RUMBLE_IMPULSES; slot++) player->impulse_time[slot] = 2.0f;
  blast.hand = _rumble_hand_other;
  rumble_player_impulse(0, &blast, 1.0f, 1.0f);
  values = rumble_calculate(player, _rumble_hand_body);
  CHECK(values.left == 49151, "the body's, whatever the tag holds: %u", values.left);
 } else return 2;
 return 0;
}
