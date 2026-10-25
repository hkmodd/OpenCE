#include "harness.h"
#include <math.h>
/* The ray a player's shot would take, without the shot
(source/items/weapons.c's weapon_get_projectile_ray, the headset's
reticle): a weapon in a unit's hands is its player's, along the unit's aim;
a vehicle's (the Scorpion's cannon, a Warthog's gun) its gunner's player's,
along the barrel, as trigger_create_projectiles shoots it, and none
unmanned. */
enum { _object_dead_bit = 0 };
enum { _unit_fires_from_camera_bit = 0 };
enum { _weapon_trigger_projectiles_cannot_be_aimed_bit = 0, _weapon_trigger_uses_weapon_origin_bit = 1 };
struct tag_block { long count; void *elements; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->elements)[index])
struct weapon_trigger_definition
{
 unsigned long flags;
 struct { long index; } projectile;
 real_point3d first_person_weapon_offset;
};
struct weapon_definition { struct { struct tag_block triggers; } weapon; };
struct weapon_datum { long definition_index; };
struct unit_definition { struct { unsigned long flags; } unit; };
struct unit_datum
{
 long definition_index;
 struct { unsigned long damage_flags; } object;
 struct { long player_index, gunner_object_index; } unit;
};
struct object_marker { struct { real_point3d position; real_vector3d forward; } matrix; };
/* the world: a weapon 300 held by unit 100 (a biped, player 0) or mounted on
unit 101 (a vehicle, no player) whose gunner is unit 100; one trigger */
static struct weapon_trigger_definition trigger;
static struct weapon_definition definition;
static struct weapon_datum weapon;
static struct unit_definition unit_definition;
static struct unit_datum units[2];
static long weapon_owner;
static struct weapon_datum *weapon_get(long weapon_index){(void)weapon_index; return &weapon;}
static struct weapon_definition *weapon_definition_get(long definition_index){(void)definition_index; return &definition;}
static long weapon_get_owner_object_index(long weapon_index){(void)weapon_index; return weapon_owner;}
static long weapon_get_effect_object_index(long weapon_index){return weapon_index;}
static struct unit_datum *unit_get(long unit_index){return &units[unit_index-100];}
static struct unit_datum *unit_try_and_get(long unit_index){return unit_index >= 100 && unit_index < 102 ? unit_get(unit_index) : NULL;}
static struct unit_definition *unit_definition_get(long definition_index){(void)definition_index; return &unit_definition;}
/* the barrel's marker points along +x; the unit's aim is +y */
static long object_get_marker_by_name(long object_index, const char *name, struct object_marker *marker, long count)
{
 (void)object_index; (void)name; (void)count;
 memset(marker, 0, sizeof(*marker));
 marker->matrix.forward.i = 1.0f;
 return 1;
}
static int aimed;
static void unit_adjust_projectile_ray(long unit_index, real_point3d *origin, real_vector3d *forward, real *velocity,
 boolean adjust_origin, boolean use_aiming_vector)
{
 (void)unit_index; (void)origin; (void)velocity; (void)adjust_origin;
 aimed = use_aiming_vector;
 if (use_aiming_vector)
 {
  forward->i = 0.0f;
  forward->j = 1.0f;
  forward->k = 0.0f;
 }
}
static boolean dual_wield_projectile_ray(long weapon_index, real_point3d *origin, real_vector3d *forward)
{(void)weapon_index; (void)origin; (void)forward; return FALSE;}
static boolean vr_render_shot_from_hand(long unit_index){(void)unit_index; return FALSE;}
static long previewed;
static void player_aim_projectile_preview(long player_index, real_point3d *origin, real_vector3d *forward)
{(void)origin; (void)forward; previewed = player_index;}
static const real_vector3d world_up = {0, 0, 1}, world_left = {0, 1, 0};
static const real_vector3d *global_up3d = &world_up, *global_left3d = &world_left;
static void cross_product3d(const real_vector3d *a, const real_vector3d *b, real_vector3d *out)
{
 real_vector3d result;
 result.i = a->j * b->k - a->k * b->j;
 result.j = a->k * b->i - a->i * b->k;
 result.k = a->i * b->j - a->j * b->i;
 *out = result;
}
static real normalize3d(real_vector3d *vector)
{
 real length = sqrtf(vector->i * vector->i + vector->j * vector->j + vector->k * vector->k);
 if (length == 0.0f) return 0.0f;
 vector->i /= length; vector->j /= length; vector->k /= length;
 return length;
}
static void point_from_line3d(const real_point3d *point, const real_vector3d *vector, real distance, real_point3d *out)
{
 out->x = point->x + vector->i * distance;
 out->y = point->y + vector->j * distance;
 out->z = point->z + vector->k * distance;
}
#include "under_test.inc"

static void reset(long owner, long gunner)
{
 memset(&trigger, 0, sizeof(trigger));
 trigger.projectile.index = 7;
 definition.weapon.triggers.count = 1;
 definition.weapon.triggers.elements = &trigger;
 memset(units, 0, sizeof(units));
 units[0].unit.player_index = 0;
 units[0].unit.gunner_object_index = NONE;
 units[1].unit.player_index = NONE;
 units[1].unit.gunner_object_index = gunner;
 weapon_owner = owner;
 aimed = -1;
 previewed = -2;
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 real_point3d origin;
 real_vector3d forward;
 boolean ray;
 CASE("vehicle-gunner") {
  /* a vehicle's gun: its gunner's player's, along the barrel, not the vehicle's aim, which a slow turret lags */
  reset(101, 100);
  ray = weapon_get_projectile_ray(300, 0, &origin, &forward);
  CHECK(ray, "a ray");
  CHECK(!aimed && forward.i == 1.0f && forward.j == 0.0f, "along the barrel (%d: %f %f)", aimed, forward.i, forward.j);
  CHECK(previewed == 0, "the gunner's player's aim assist (%ld)", previewed);
  return 0;
 }
 CASE("vehicle-unmanned") {
  /* no gunner: no player's shot, no ray */
  reset(101, NONE);
  CHECK(!weapon_get_projectile_ray(300, 0, &origin, &forward), "no ray");
  return 0;
 }
 CASE("on-foot") {
  /* a weapon in the hands: the player's, along the unit's aim, as before */
  reset(100, NONE);
  ray = weapon_get_projectile_ray(300, 0, &origin, &forward);
  CHECK(ray && aimed == 1 && forward.j == 1.0f, "along the aim (%d, %d: %f)", ray, aimed, forward.j);
  CHECK(previewed == 0, "the player's aim assist (%ld)", previewed);
  return 0;
 }
 fprintf(stderr, "no case %s\n", case_name);
 return 2;
}
