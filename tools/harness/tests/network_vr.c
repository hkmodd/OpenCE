#include "harness.h"
#include <math.h>
#include "enums.inc"
/* The headsets' players' poses in a VR server's game
(port/linux/game/network_vr.c): a pose as it goes on the wire and back, the
poses fit to take, the host taking a pose only of the sending machine's own
player in a VR server's game and sending it on to the machines that see the
player but the sender, a client keeping the others' poses for a second. */
typedef unsigned short word;
typedef word message_header;
#define MAXIMUM_TRACKED_PLAYERS 16
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 8
#define NUMBEROF(array) ((short)(sizeof(array) / sizeof((array)[0])))
#define csmemcpy memcpy
#define csmemset memset
#define CEILING(n,ceiling) ((n)>(ceiling)?(ceiling):(n))
#define PIN(n,floor,ceiling) ((n)<(floor) ? (floor) : CEILING((n),(ceiling)))
#define DISTRIBUTED_UNIT_SCALE 32767.0f
struct network_game { byte cooperative_flags; };
static struct network_game game;
static short connection = _game_connection_local;
static long now = 100;
static struct network_game *network_game_get_game(void){return &game;}
static long game_time_get(void){return now;}
static boolean game_in_progress(void){return TRUE;}
static short game_connection(void){return connection;}
static void platform_log(char const *format, ...){(void)format;}
/* the game's players: whether each is, is this machine's, and its machine
(the host: 0); which machines see which players */
static boolean present[MAXIMUM_TRACKED_PLAYERS];
static boolean local[MAXIMUM_TRACKED_PLAYERS];
static long owner[MAXIMUM_TRACKED_PLAYERS];
static boolean sees[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static long distributed_player_from_byte(byte player_index)
{
 return player_index < MAXIMUM_TRACKED_PLAYERS && present[player_index] ? (0x1234L << 16) | player_index : NONE;
}
static boolean distributed_player_is_local(long player_index)
{
 return player_index != NONE && local[DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)];
}
static boolean distributed_machine_has_player(long machine_index, short player_index)
{
 return player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS && owner[player_index] == machine_index;
}
static short distributed_client_machines(long *machine_indices, short maximum)
{
 short index, count = 0;
 for (index = 1; index < 4 && count < maximum; index++) machine_indices[count++] = index;
 return count;
}
static boolean distributed_machine_sees_player(long machine_index, short player_index)
{
 return sees[machine_index][player_index];
}
/* what was sent: to the host, and to each machine */
static int sent_up;
static int asked_drops;
static int sent_throws;
static int sent_grabs;
static int sent_down[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static byte last_message[128];
static void distributed_send(void *message, byte type, short count, word size, short destination)
{
 if (type == _distributed_message_vr_pose_up && count == 1 && destination == _distributed_to_host) sent_up++;
 if (type == _distributed_message_vr_drop && count == 1 && destination == _distributed_to_host_reliably) asked_drops++;
 if (type == _distributed_message_vr_throw && count == 1 && destination == _distributed_to_host_reliably) sent_throws++;
 if (type == _distributed_message_vr_grab && count == 1 && destination == _distributed_to_host_reliably) sent_grabs++;
 memcpy(last_message, message, size);
}
static void distributed_send_to_machine(long machine_index, void *message, byte type, short count, word size)
{
 if (type == _distributed_message_vr_pose_down && count == 1) sent_down[machine_index]++;
 memcpy(last_message, message, size);
}
/* the players' units (player i's unit i), their weapons in hand (NONE:
none), riding or dead; the drops done and the last weapon's acceleration */
enum { _object_dead_bit };
struct player_datum { long unit_index; short local_player_index; };
struct unit_datum { struct { long parent_object_index; word damage_flags; real_point3d position; } object;
 struct { short current_weapon_index; long player_index; } unit; };
static struct player_datum player_data[MAXIMUM_TRACKED_PLAYERS];
static struct unit_datum unit_data[MAXIMUM_TRACKED_PLAYERS];
/* (each unit's weapon in slot 0, in hand, and in slot 1; the other
slots none) */
static long held_weapon[MAXIMUM_TRACKED_PLAYERS];
static long other_weapon[MAXIMUM_TRACKED_PLAYERS];
static int drops, slot_drops;
static short dropped_slot = NONE;
/* this machine's headset's player carries four */
static boolean headset_four;
static int vr_render_carries_four(long unit_index){(void)unit_index;return headset_four;}
/* a local game's server, which takes no one else (vr_rules_active) */
static boolean server_alone;
static int vr_rules_active(void){return server_alone;}
static real_vector3d accelerated;
static struct player_datum *distributed_player(short player_index)
{
 return player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS && present[player_index] ? &player_data[player_index] : NULL;
}
static struct unit_datum *unit_try_and_get(long unit_index)
{
 return unit_index >= 0 && unit_index < MAXIMUM_TRACKED_PLAYERS ? &unit_data[unit_index] : NULL;
}
static long unit_inventory_get_weapon(long unit_index, short slot)
{
 return slot == unit_data[unit_index].unit.current_weapon_index ? held_weapon[unit_index] :
  slot == 1 ? other_weapon[unit_index] : NONE;
}
static boolean unit_drop_inventory_slot(long unit_index, short slot)
{
 if (unit_inventory_get_weapon(unit_index, slot) == NONE) return FALSE;
 slot_drops++;dropped_slot=slot;
 return TRUE;
}
static boolean infection_allow_weapon_drop(long unit_index, long weapon_index){(void)unit_index;(void)weapon_index;return TRUE;}
static boolean unit_drop_current_weapon(long unit_index, boolean immediate)
{
 (void)immediate;
 if (held_weapon[unit_index] == NONE) return FALSE;
 drops++;
 return TRUE;
}
static void *object_try_and_get(long object_index){return object_index != NONE ? (void *)&drops : NULL;}
static void object_get_velocities(long object_index, real_vector3d *translational, real_vector3d *angular)
{
 (void)object_index;(void)angular;
 translational->i = translational->j = translational->k = 0.0f;
}
static void item_accelerate(long item_index, real_vector3d const *acceleration, boolean detonates)
{
 (void)item_index;(void)detonates;
 accelerated = *acceleration;
}
/* a grenade's throw: the unit's eyes, and whether a wall stands between
them and the hand */
struct collision_result { short type; };
static real_point3d eyes_at;
static boolean wall_before_hand;
static void unit_get_camera_position(long unit_index, real_point3d *position){(void)unit_index;*position=eyes_at;}
static boolean collision_test_vector(unsigned long flags, real_point3d const *point, real_vector3d const *vector,
 long ignore_object_index, struct collision_result *collision)
{
 (void)flags;(void)point;(void)vector;(void)ignore_object_index;(void)collision;
 return wall_before_hand;
}
static real_point3d *point_from_line3d(real_point3d const *p, real_vector3d const *v, real t, real_point3d *result)
{
 result->x=v->i*t+p->x;result->y=v->j*t+p->y;result->z=v->k*t+p->z;return result;
}
static real_vector3d *scale_vector3d(real_vector3d const *a, real c, real_vector3d *result)
{
 result->i=c*a->i;result->j=c*a->j;result->k=c*a->k;return result;
}
/* the weapons on the ground a hand takes (object index 500 + i: weapon
i), their state, the units' weapon counts, the weapons added and how, the
pickups shown */
enum { _object_connected_to_map_bit = 0 };
enum { _item_attached_to_unit_bit = 0 };
#define _object_mask_weapon 0x4
#define GROUND_WEAPONS 4
struct item_datum { long definition_index; struct { long parent_object_index; unsigned long flags;
 real_point3d position; real_point3d bounding_sphere_center; } object; struct { unsigned long flags; } item; };
static struct item_datum ground[GROUND_WEAPONS];
static short weapon_count[MAXIMUM_TRACKED_PLAYERS];
static int added;
static long added_unit, added_weapon, added_how;
static int shown_pickups;
static int told_pickups;
static boolean distributed_object_index_valid(long object_index){return object_index != NONE && (object_index >> 16) != 0;}
static void *object_try_and_get_and_verify_type(long object_index, unsigned long types)
{
 long index = object_index & 0xFFFF;
 return (types & _object_mask_weapon) && index >= 500 && index < 500 + GROUND_WEAPONS ? (void *)&ground[index - 500] : NULL;
}
static short unit_get_weapon_count(long unit_index){return weapon_count[unit_index];}
static boolean unit_can_use_weapon(long unit_index, long weapon_index){(void)unit_index;(void)weapon_index;return TRUE;}
static boolean unit_add_weapon_to_inventory(long unit_index, long weapon_index, long how)
{
 added++;added_unit=unit_index;added_weapon=weapon_index;added_how=how;weapon_count[unit_index]++;
 return TRUE;
}
static void hud_picked_up_weapon(short local_player_index, long definition_index){(void)local_player_index;(void)definition_index;shown_pickups++;}
static void network_distributed_player_picked_up(long player_index, short kind, long definition_index, short count)
{
 (void)player_index;(void)kind;(void)definition_index;(void)count;told_pickups++;
}
#include "under_test.inc"

static void vector_set(real_vector3d *vector, real i, real j, real k){vector->i=i;vector->j=j;vector->k=k;}

/* a pose fit to take: the head over the unit, the hands at its sides */
static struct network_vr_pose pose_of(void)
{
 struct network_vr_pose pose;
 short hand;
 memset(&pose,0,sizeof(pose));
 pose.flags=FLAG(_network_vr_pose_left_hand_bit)|FLAG(_network_vr_pose_right_hand_bit);
 pose.off_slot=NONE;
 vector_set(&pose.head,0.05f,-0.02f,0.6f);
 vector_set(&pose.head_forward,1.0f,0.0f,0.0f);
 vector_set(&pose.head_up,0.0f,0.0f,1.0f);
 for (hand=0;hand<2;hand++)
 {
  vector_set(&pose.hands[hand],0.15f,hand?-0.1f:0.1f,0.4f);
  vector_set(&pose.hand_forwards[hand],0.0f,0.0f,-1.0f);
  vector_set(&pose.hand_ups[hand],1.0f,0.0f,0.0f);
 }
 return pose;
}

/* a pose's wire entry, as a client's machine sends it, of a player */
static struct distributed_vr_pose entry_of(short player_index, struct network_vr_pose const *pose)
{
 struct distributed_vr_pose entry;
 network_vr_pose_pack(player_index,pose,&entry);
 return entry;
}

static void players(void)
{
 memset(present,0,sizeof(present));memset(local,0,sizeof(local));memset(sees,0,sizeof(sees));
 memset(sent_down,0,sizeof(sent_down));sent_up=0;
 present[0]=present[1]=present[2]=TRUE;
 owner[0]=0;owner[1]=2;owner[2]=3;
 sees[1][1]=sees[2][1]=sees[3][1]=TRUE;
 network_vr_reset();
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 CASE("wire") {
  struct network_vr_pose pose=pose_of(),back;
  struct distributed_vr_pose entry=entry_of(5,&pose);
  CHECK(network_vr_entry_size()==58,"the entry is 58 bytes: %d",(int)network_vr_entry_size());
  CHECK(entry.player_index==5,"its player");
  network_vr_pose_unpack(&entry,&back);
  CHECK(back.flags==pose.flags,"the flags");
  CHECK(fabsf(back.head.k-pose.head.k)<0.5f/4096.0f && fabsf(back.hands[1].j-pose.hands[1].j)<0.5f/4096.0f,
   "places to 1/4096 of a world unit: %f %f",back.head.k,back.hands[1].j);
  CHECK(fabsf(back.hand_forwards[0].k+1.0f)<1e-4f && fabsf(back.head_up.k-1.0f)<1e-4f,"axes whole");
  CHECK(network_vr_pose_valid(&back),"fit to take after the wire");
  CHECK(back.off_slot==NONE && back.off_trigger==0.0f,"no weapon in the other hand: %d %f",back.off_slot,
   back.off_trigger);
  pose.off_slot=1;pose.off_trigger=0.6f;
  entry=entry_of(5,&pose);
  network_vr_pose_unpack(&entry,&back);
  CHECK(back.off_slot==1 && fabsf(back.off_trigger-0.6f)<0.5f/255.0f,"the other hand's slot and trigger: %d %f",
   back.off_slot,back.off_trigger);
  pose.off_trigger=7.0f;
  entry=entry_of(5,&pose);
  CHECK(entry.off_trigger==255,"a trigger past its end sent pulled: %d",entry.off_trigger);
  pose.off_slot=NONE;pose.off_trigger=0.0f;
  pose.flags=FLAG(_network_vr_pose_right_hand_bit);
  vector_set(&pose.hands[0],100.0f,100.0f,100.0f);
  entry=entry_of(5,&pose);
  CHECK(entry.hands[0].i==0 && entry.hand_forwards[0].i==0,"an untracked hand's parts not sent");
 } else CASE("valid") {
  struct network_vr_pose pose=pose_of();
  CHECK(network_vr_pose_valid(&pose),"a pose fit to take");
  pose=pose_of();pose.flags|=FLAG(NUMBER_OF_NETWORK_VR_POSE_BITS);
  CHECK(!network_vr_pose_valid(&pose),"an unknown flag refused");
  pose=pose_of();pose.flags=0;pose.head.i=0.45f;pose.head.j=0.3f;
  CHECK(!network_vr_pose_valid(&pose),"the head further than 0.5 world units across from the unit refused");
  pose=pose_of();pose.flags=0;pose.head.k=1.2f;
  CHECK(!network_vr_pose_valid(&pose),"the head above the unit's reach refused");
  pose=pose_of();pose.flags=0;pose.head.k=-0.6f;
  CHECK(!network_vr_pose_valid(&pose),"the head below the unit's reach refused");
  pose=pose_of();vector_set(&pose.hands[1],0.5f,0.0f,0.6f);
  CHECK(!network_vr_pose_valid(&pose),"a hand further than 0.4 world units from the head refused");
  pose.flags=FLAG(_network_vr_pose_left_hand_bit);
  CHECK(network_vr_pose_valid(&pose),"an untracked hand's place not looked at");
  pose=pose_of();vector_set(&pose.head_up,1.0f,0.0f,0.0f);
  CHECK(!network_vr_pose_valid(&pose),"the head's axes not square refused");
  pose=pose_of();vector_set(&pose.hand_forwards[0],0.0f,0.0f,0.0f);
  CHECK(!network_vr_pose_valid(&pose),"a hand's axes of no length refused");
  pose=pose_of();pose.off_slot=MAXIMUM_WEAPONS_PER_UNIT-1;pose.off_trigger=1.0f;
  CHECK(network_vr_pose_valid(&pose),"the last slot in the other hand, its trigger pulled");
  pose.off_slot=MAXIMUM_WEAPONS_PER_UNIT;
  CHECK(!network_vr_pose_valid(&pose),"a slot past the inventory refused");
  pose.off_slot=-2;
  CHECK(!network_vr_pose_valid(&pose),"a slot below none refused");
  pose.off_slot=0;pose.off_trigger=sqrtf(-1.0f);
  CHECK(!network_vr_pose_valid(&pose),"a trigger not a number refused");
  pose.off_trigger=1.5f;
  CHECK(!network_vr_pose_valid(&pose),"a trigger past its end refused");
 } else CASE("host") {
  struct network_vr_pose pose=pose_of();
  struct distributed_vr_pose entry;
  connection=_game_connection_network_server;
  players();
  game.cooperative_flags=0;
  entry=entry_of(1,&pose);
  network_vr_handle_up(2,now,&entry,1);
  CHECK(sent_down[1]+sent_down[3]==0,"an ordinary server's game: no pose taken");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  network_vr_handle_up(3,now,&entry,1);
  CHECK(sent_down[1]+sent_down[2]==0,"another machine's player's pose refused");
  entry=entry_of(9,&pose);
  network_vr_handle_up(2,now,&entry,1);
  CHECK(sent_down[1]+sent_down[3]==0,"a player who is not refused");
  entry=entry_of(1,&pose);
  network_vr_handle_up(2,now,&entry,1);
  CHECK(sent_down[1]==1 && sent_down[3]==1,"its own player's pose sent on to the machines that see the player");
  CHECK(sent_down[2]==0,"not back to the machine it came from");
  network_vr_handle_up(2,now,&entry,1);
  network_vr_handle_up(2,now-1,&entry,1);
  CHECK(sent_down[1]==1,"a pose no newer than the last refused");
  pose.head.i=2.0f;entry=entry_of(1,&pose);
  network_vr_handle_up(2,now+1,&entry,1);
  CHECK(sent_down[1]==1,"a pose not fit to take refused");
  pose=pose_of();entry=entry_of(1,&pose);
  network_vr_handle_up(2,now+2,&entry,0);
  CHECK(sent_down[1]==1,"a message of no entry refused");
  sees[3][1]=FALSE;
  network_vr_handle_up(2,now+2,&entry,1);
  CHECK(sent_down[1]==2 && sent_down[3]==1,"not to a machine that does not see the player");
  CHECK(network_vr_pose_get(distributed_player_from_byte(1))!=NULL,"the host keeps it, to draw");
 } else CASE("send") {
  struct network_vr_pose pose=pose_of();
  connection=_game_connection_network_client;
  players();local[1]=TRUE;
  game.cooperative_flags=0;
  network_vr_send(distributed_player_from_byte(1),&pose);
  CHECK(sent_up==0,"none to the host of an ordinary server");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  network_vr_send(distributed_player_from_byte(1),&pose);
  network_vr_send(distributed_player_from_byte(1),&pose);
  CHECK(sent_up==1,"a VR server's host: once a tick");
  CHECK(((struct distributed_vr_pose_message *)last_message)->entry.player_index==1,"of the player");
  now++;pose.head.k=3.0f;
  network_vr_send(distributed_player_from_byte(1),&pose);
  CHECK(sent_up==1,"a pose not fit to take not sent");
  CHECK(network_vr_pose_get(distributed_player_from_byte(1))==NULL,"this machine's own player's not drawn from one");
  connection=_game_connection_network_server;
  players();local[0]=TRUE;sees[1][0]=sees[2][0]=TRUE;
  now++;pose=pose_of();
  network_vr_send(distributed_player_from_byte(0),&pose);
  CHECK(sent_up==0 && sent_down[1]==1 && sent_down[2]==1 && sent_down[3]==0,
   "the host: its own player's to the machines that see the player");
 } else CASE("client") {
  struct network_vr_pose pose=pose_of();
  struct distributed_vr_pose entry;
  connection=_game_connection_network_client;
  players();local[2]=TRUE;
  entry=entry_of(1,&pose);
  network_vr_handle_down(now,&entry,1);
  CHECK(network_vr_pose_get(distributed_player_from_byte(1))!=NULL,"another machine's player's pose kept");
  CHECK(network_vr_pose_get(distributed_player_from_byte(0))==NULL,"none for a player not heard of");
  entry=entry_of(2,&pose);
  network_vr_handle_down(now,&entry,1);
  CHECK(network_vr_poses[2].time==NONE,"this machine's own player's refused");
  pose.head.k=2.0f;entry=entry_of(1,&pose);
  network_vr_handle_down(now+1,&entry,1);
  CHECK(fabsf(network_vr_pose_get(distributed_player_from_byte(1))->head.k-0.6f)<0.01f,"a pose not fit refused");
  now+=NETWORK_VR_POSE_TICKS;
  CHECK(network_vr_pose_get(distributed_player_from_byte(1))!=NULL,"kept for a second");
  now++;
  CHECK(network_vr_pose_get(distributed_player_from_byte(1))==NULL,"none once a second old");
  CHECK(network_vr_pose_get(NONE)==NULL,"none for no player");
  pose=pose_of();entry=entry_of(1,&pose);
  network_vr_handle_down(now+500,&entry,1);
  CHECK(network_vr_pose_get(distributed_player_from_byte(1))!=NULL,
   "a pose sent by a clock ahead of this machine's aged from when it was taken");
 } else CASE("drop") {
  struct distributed_vr_drop entry;
  real_vector3d velocity;
  short index;
  connection=_game_connection_network_server;
  players();
  for (index=0;index<MAXIMUM_TRACKED_PLAYERS;index++)
  {
   player_data[index].unit_index=index;held_weapon[index]=100+index;
   unit_data[index].object.parent_object_index=NONE;unit_data[index].object.damage_flags=0;
  }
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  memset(&entry,0,sizeof(entry));
  entry.player_index=1;
  vector_set(&velocity,0.1f,0.0f,0.05f);
  distributed_vector_pack(&velocity,NETWORK_VR_VELOCITY_SCALE,&entry.velocity);
  network_vr_handle_drop(2,&entry,1,FALSE);
  CHECK(drops==0,"a request in a datagram refused");
  network_vr_handle_drop(3,&entry,1,TRUE);
  CHECK(drops==0,"another machine's player's refused");
  game.cooperative_flags=0;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==0,"an ordinary server's refused");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  entry.slot=MAXIMUM_WEAPONS_PER_UNIT+1;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==0 && slot_drops==0,"a slot past the inventory refused");
  entry.slot=0;
  unit_data[1].object.parent_object_index=7;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==0,"a player riding refused");
  unit_data[1].object.parent_object_index=NONE;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==1,"its own player's weapon dropped");
  CHECK(fabsf(accelerated.i-0.1f)<0.001f && fabsf(accelerated.k-0.05f)<0.001f,"thrown with its velocity");
  now+=NETWORK_VR_DROP_TICKS-1;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==1,"not again within NETWORK_VR_DROP_TICKS");
  now++;
  vector_set(&velocity,1.5f,0.0f,0.0f);
  distributed_vector_pack(&velocity,NETWORK_VR_VELOCITY_SCALE,&entry.velocity);
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==2,"again after it");
  CHECK(fabsf(accelerated.i-NETWORK_VR_DROP_SPEED)<0.001f,"no faster than NETWORK_VR_DROP_SPEED: %f",accelerated.i);
  now+=NETWORK_VR_DROP_TICKS;
  held_weapon[1]=NONE;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==2,"no weapon in hand, none dropped");
  /* the other hand's weapon: a slot not in hand, of the inventory */
  now+=NETWORK_VR_DROP_TICKS;
  entry.slot=1;
  held_weapon[1]=101;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==2 && slot_drops==0,"the slot in hand refused as the other hand's");
  entry.slot=1+1;other_weapon[1]=NONE;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(slot_drops==0,"an empty slot refused");
  other_weapon[1]=201;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(drops==2 && slot_drops==1 && dropped_slot==1,"the other hand's weapon dropped: %d %d",slot_drops,dropped_slot);
  CHECK(fabsf(accelerated.i-NETWORK_VR_DROP_SPEED)<0.001f,"thrown, no faster than NETWORK_VR_DROP_SPEED");
  now+=1;
  network_vr_handle_drop(2,&entry,1,TRUE);
  CHECK(slot_drops==1,"not again within NETWORK_VR_DROP_TICKS");
  entry.slot=0;
  /* a game with no network drops it itself; a VR server's client asks */
  connection=_game_connection_local;game.cooperative_flags=0;held_weapon[0]=50;now+=NETWORK_VR_DROP_TICKS;
  vector_set(&velocity,0.0f,0.0f,0.1f);
  network_vr_drop(distributed_player_from_byte(0),&velocity,NONE);
  CHECK(drops==3,"a game with no network: dropped at once");
  connection=_game_connection_network_server;now+=NETWORK_VR_DROP_TICKS;
  network_vr_drop(distributed_player_from_byte(0),&velocity,NONE);
  CHECK(drops==3 && asked_drops==0,"an ordinary server's host: nothing dropped");
  server_alone=TRUE;
  network_vr_drop(distributed_player_from_byte(0),&velocity,NONE);
  CHECK(drops==4 && asked_drops==0,"a local game's server: dropped at once");
  server_alone=FALSE;
  connection=_game_connection_network_client;now+=NETWORK_VR_DROP_TICKS;
  network_vr_drop(distributed_player_from_byte(0),&velocity,NONE);
  CHECK(drops==4 && asked_drops==0,"an ordinary server's client: nothing asked");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  network_vr_drop(distributed_player_from_byte(0),&velocity,NONE);
  CHECK(drops==4 && asked_drops==1,"a VR server's client: asked, reliably, not dropped itself");
  CHECK(((struct distributed_vr_drop_message *)last_message)->entry.player_index==0 &&
   ((struct distributed_vr_drop_message *)last_message)->entry.slot==0,"for its player, the weapon in hand");
  network_vr_drop(distributed_player_from_byte(0),&velocity,1);
  CHECK(asked_drops==2 && ((struct distributed_vr_drop_message *)last_message)->entry.slot==2,
   "the other hand's: 1 and its slot");
  network_vr_drop(distributed_player_from_byte(0),&velocity,MAXIMUM_WEAPONS_PER_UNIT);
  CHECK(asked_drops==2,"a slot past the inventory not asked");
 } else CASE("four") {
  struct network_vr_pose pose=pose_of();
  connection=_game_connection_network_server;
  players();
  unit_data[0].unit.player_index=distributed_player_from_byte(0);local[0]=TRUE;
  unit_data[1].unit.player_index=distributed_player_from_byte(1);
  unit_data[2].unit.player_index=NONE;
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  CHECK(!network_vr_carries_four(1),"a VR server's player with no pose: two");
  {
   struct distributed_vr_pose entry=entry_of(1,&pose);
   network_vr_handle_up(2,now,&entry,1);
   CHECK(!network_vr_carries_four(1),"a VR server's player whose pose comes, its weapons not in their places: two");
   pose.flags|=FLAG(_network_vr_pose_places_bit);
   entry=entry_of(1,&pose);
   network_vr_handle_up(2,now+1,&entry,1);
  }
  CHECK(network_vr_carries_four(1),"a VR server's player whose pose comes, its weapons in their places: four");
  game.cooperative_flags=0;
  CHECK(!network_vr_carries_four(1),"an ordinary server's: two");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  now+=NETWORK_VR_POSE_TICKS+2;
  CHECK(!network_vr_carries_four(1),"a pose no longer coming: two");
  CHECK(!network_vr_carries_four(2),"a unit no player's: two");
  headset_four=FALSE;
  CHECK(!network_vr_carries_four(0),"this machine's player not by the rules: two");
  headset_four=TRUE;
  CHECK(network_vr_carries_four(0),"this machine's headset's player: four");
 } else CASE("melee") {
  /* another machine's headset's player strikes through the hand of its
  pose farther ahead of the head; none without a pose, nor for this
  machine's own player (its headset's, vr_view.c) */
  struct network_vr_pose pose=pose_of();
  struct distributed_vr_pose entry;
  real_point3d point;
  connection=_game_connection_network_server;
  players();
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  unit_data[1].unit.player_index=distributed_player_from_byte(1);
  unit_data[1].object.position.x=10.0f;unit_data[1].object.position.y=20.0f;unit_data[1].object.position.z=1.0f;
  CHECK(!network_vr_melee_point(1,&point),"no pose: the unit's aim");
  vector_set(&pose.hands[1],0.3f,-0.1f,0.5f);
  entry=entry_of(1,&pose);
  network_vr_handle_up(2,now,&entry,1);
  CHECK(network_vr_melee_point(1,&point),"a pose: a strike");
  CHECK(fabsf(point.x-10.3f)<1e-3f && fabsf(point.y-19.9f)<1e-3f && fabsf(point.z-1.5f)<1e-3f,
   "through the right hand, ahead of the left: %f %f %f",point.x,point.y,point.z);
  vector_set(&pose.hands[0],0.35f,0.1f,0.5f);
  entry=entry_of(1,&pose);
  network_vr_handle_up(2,now+1,&entry,1);
  CHECK(network_vr_melee_point(1,&point) && fabsf(point.x-10.35f)<1e-3f && fabsf(point.y-20.1f)<1e-3f,
   "the left hand ahead: through it: %f %f",point.x,point.y);
  pose.flags=FLAG(_network_vr_pose_right_hand_bit);
  vector_set(&pose.hands[1],-0.1f,-0.1f,0.5f);
  entry=entry_of(1,&pose);
  network_vr_handle_up(2,now+2,&entry,1);
  CHECK(network_vr_melee_point(1,&point) && fabsf(point.x-9.9f)<1e-3f,
   "the left hand untracked: the right, even behind the head: %f",point.x);
  local[1]=TRUE;
  CHECK(!network_vr_melee_point(1,&point),"this machine's own player: its headset's, not a pose");
 } else CASE("grab") {
  /* the host gives a headset's hand the weapon on the ground it reaches:
  another machine's player by its pose, the hand empty and near the
  weapon, its own machine asking reliably; a player of the host's by its
  own hand; each added readied by nothing, shown and told */
  struct network_vr_pose pose=pose_of();
  struct distributed_vr_pose pose_entry;
  struct distributed_vr_grab entry;
  short index;
  connection=_game_connection_network_server;
  players();
  for (index=0;index<MAXIMUM_TRACKED_PLAYERS;index++)
  {
   player_data[index].unit_index=index;player_data[index].local_player_index=NONE;weapon_count[index]=1;
   unit_data[index].object.parent_object_index=NONE;unit_data[index].object.damage_flags=0;
  }
  for (index=0;index<GROUND_WEAPONS;index++)
  {
   ground[index].definition_index=700+index;ground[index].object.parent_object_index=NONE;
   ground[index].object.flags=FLAG(_object_connected_to_map_bit);ground[index].item.flags=0;
   /* along x, by the right hand (10.15 19.9 1.4) */
   ground[index].object.position.x=10.0f;ground[index].object.position.y=19.9f;ground[index].object.position.z=1.4f;
   ground[index].object.bounding_sphere_center=ground[index].object.position;
   ground[index].object.bounding_sphere_center.x+=0.1f;
  }
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  unit_data[1].unit.player_index=distributed_player_from_byte(1);
  unit_data[1].object.position.x=10.0f;unit_data[1].object.position.y=20.0f;unit_data[1].object.position.z=1.0f;
  pose.flags|=FLAG(_network_vr_pose_places_bit);
  pose_entry=entry_of(1,&pose);
  network_vr_handle_up(2,now,&pose_entry,1);
  memset(&entry,0,sizeof(entry));
  entry.player_index=1;entry.weapon_index=(0x55L<<16)|500;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==0,"the main hand holding: refused");
  pose.flags|=FLAG(_network_vr_pose_bare_bit);
  pose_entry=entry_of(1,&pose);
  network_vr_handle_up(2,now+1,&pose_entry,1);
  network_vr_handle_grab(2,&entry,1,FALSE);
  CHECK(added==0,"a request in a datagram refused");
  network_vr_handle_grab(3,&entry,1,TRUE);
  CHECK(added==0,"another machine's player's refused");
  network_vr_handle_grab(2,&entry,2,TRUE);
  CHECK(added==0,"two at once refused");
  game.cooperative_flags=0;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==0,"an ordinary server's refused");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  entry.flags=0x80;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==0,"unknown flags refused");
  entry.flags=0;
  ground[0].object.position.z=1.7f;ground[0].object.bounding_sphere_center.z=1.7f;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==0,"a weapon 0.3 from the hand refused");
  ground[0].object.position.z=1.55f;ground[0].object.bounding_sphere_center.z=1.55f;
  ground[0].object.position.x=9.9f;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==1,"one 0.15 from the hand, reached along its length (its far end past the hand): taken");
  CHECK(added_unit==1 && added_weapon==entry.weapon_index && added_how==_unit_add_weapon_normal,
   "to the player's unit, readied by nothing: %ld %ld",added_unit,added_how);
  CHECK(shown_pickups==1 && told_pickups==1,"shown and told: %d %d",shown_pickups,told_pickups);
  entry.weapon_index=(0x55L<<16)|501;
  now+=NETWORK_VR_GRAB_TICKS-1;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==1,"another within NETWORK_VR_GRAB_TICKS refused");
  now+=1;
  unit_data[1].object.parent_object_index=7;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==1,"a player riding refused");
  unit_data[1].object.parent_object_index=NONE;
  ground[1].object.parent_object_index=33;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==1,"a weapon someone holds refused");
  ground[1].object.parent_object_index=NONE;
  ground[1].object.flags=0;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==1,"a weapon off the map refused");
  ground[1].object.flags=FLAG(_object_connected_to_map_bit);
  weapon_count[1]=MAXIMUM_WEAPONS_PER_UNIT;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==1,"four carried: refused");
  weapon_count[1]=2;
  entry.weapon_index=(0x55L<<16)|1;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==1,"no weapon's index refused");
  entry.weapon_index=(0x55L<<16)|501;
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==2,"the next after NETWORK_VR_GRAB_TICKS taken");
  /* the other hand, by its own place: holding, refused; empty, taken */
  now+=NETWORK_VR_GRAB_TICKS;
  entry.weapon_index=(0x55L<<16)|502;entry.flags=FLAG(_network_vr_grab_left_hand_bit);
  ground[2].object.position.y=20.1f;ground[2].object.bounding_sphere_center.y=20.1f;
  pose.off_slot=1;pose_entry=entry_of(1,&pose);
  network_vr_handle_up(2,now,&pose_entry,1);
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==2,"the other hand holding: refused");
  pose.off_slot=NONE;pose_entry=entry_of(1,&pose);
  network_vr_handle_up(2,now+1,&pose_entry,1);
  network_vr_handle_grab(2,&entry,1,TRUE);
  CHECK(added==3,"the other hand empty: taken");
  /* a player of the host's takes by its own hand, no pose */
  now+=NETWORK_VR_GRAB_TICKS;
  local[0]=TRUE;headset_four=TRUE;player_data[0].local_player_index=0;
  unit_data[0].unit.player_index=distributed_player_from_byte(0);
  ground[3].object.position.x=50.0f;ground[3].object.bounding_sphere_center.x=50.1f;
  network_vr_grab(distributed_player_from_byte(0),(0x55L<<16)|503,1);
  CHECK(added==4 && added_unit==0 && told_pickups==3 && shown_pickups==4,
   "the host's own player: taken, shown, not told: %d %d %d",added,told_pickups,shown_pickups);
  /* a VR server's client asks, reliably; an ordinary server's asks nothing */
  connection=_game_connection_network_client;
  network_vr_grab(distributed_player_from_byte(0),(0x55L<<16)|503,0);
  CHECK(sent_grabs==1 && added==4,"a VR server's client asks the host");
  CHECK(((struct distributed_vr_grab_message *)last_message)->entry.player_index==0 &&
   ((struct distributed_vr_grab_message *)last_message)->entry.flags==FLAG(_network_vr_grab_left_hand_bit) &&
   ((struct distributed_vr_grab_message *)last_message)->entry.weapon_index==((0x55L<<16)|503),
   "for its player, the left hand, the weapon");
  game.cooperative_flags=0;
  network_vr_grab(distributed_player_from_byte(0),(0x55L<<16)|503,0);
  CHECK(sent_grabs==1,"an ordinary server's client asks nothing");
 } else CASE("throw") {
  struct distributed_vr_throw entry;
  real_vector3d hand,offset,velocity,game_velocity;
  real_point3d origin;
  real speed;
  connection=_game_connection_local;
  players();
  unit_data[0].unit.player_index=distributed_player_from_byte(0);
  unit_data[1].unit.player_index=distributed_player_from_byte(1);
  unit_data[2].unit.player_index=NONE;
  eyes_at.x=10.0f;eyes_at.y=20.0f;eyes_at.z=1.0f;
  vector_set(&game_velocity,0.5f,0.0f,0.0f);
  /* a game with no network keeps its player's throw for the grenade that
  follows: from the hand, the hand's way, the game's own speed by the
  hand's against a firm throw's */
  vector_set(&hand,0.0f,0.04f,0.02f);vector_set(&offset,0.1f,0.05f,-0.1f);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  origin.x=origin.y=origin.z=0.0f;velocity=game_velocity;
  CHECK(network_vr_grenade_throw(0,&origin,&velocity),"the throw taken by the player's grenade");
  speed=sqrtf(hand.j*hand.j+hand.k*hand.k)/NETWORK_VR_THROW_FULL*0.5f;
  CHECK(fabsf(velocity.i)<1e-5f && fabsf(velocity.j-0.04f/sqrtf(0.002f)*speed)<1e-4f &&
   fabsf(velocity.k-0.02f/sqrtf(0.002f)*speed)<1e-4f,"the hand's way and speed: %f %f %f",velocity.i,velocity.j,velocity.k);
  vector_set(&hand,NETWORK_VR_THROW_FULL,0.0f,0.0f);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  velocity=game_velocity;network_vr_grenade_throw(0,&origin,&velocity);
  CHECK(fabsf(velocity.i-0.5f)<1e-5f,"a firm throw's: the game's own speed: %f",velocity.i);
  CHECK(fabsf(origin.x-10.1f)<1e-4f && fabsf(origin.y-20.05f)<1e-4f && fabsf(origin.z-0.9f)<1e-4f,
   "from the hand: %f %f %f",origin.x,origin.y,origin.z);
  velocity=game_velocity;
  CHECK(!network_vr_grenade_throw(0,&origin,&velocity) && velocity.i==0.5f,"one throw, one grenade");
  /* too old, of no player, or another player's: the game's own throw */
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  now+=NETWORK_VR_THROW_TICKS+1;
  CHECK(!network_vr_grenade_throw(0,&origin,&velocity) && velocity.i==0.5f,"a throw too old not taken");
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  CHECK(!network_vr_grenade_throw(2,&origin,&velocity),"a unit no player's: the game's throw");
  CHECK(!network_vr_grenade_throw(1,&origin,&velocity),"another player's throw not taken");
  now+=NETWORK_VR_THROW_TICKS;
  CHECK(network_vr_grenade_throw(0,&origin,&velocity),"the throw at NETWORK_VR_THROW_TICKS old taken");
  /* the speed between the game's least and most */
  vector_set(&hand,0.01f,0.0f,0.0f);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  velocity=game_velocity;network_vr_grenade_throw(0,&origin,&velocity);
  CHECK(fabsf(velocity.i-0.5f*NETWORK_VR_THROW_LEAST)<1e-5f,"a slow hand's: the least: %f",velocity.i);
  vector_set(&hand,0.0f,0.0f,9.0f);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  vector_set(&velocity,0.2f,0.0f,0.0f);network_vr_grenade_throw(0,&origin,&velocity);
  CHECK(fabsf(velocity.k-0.2f*NETWORK_VR_THROW_MOST)<1e-5f && velocity.i==0.0f,"a fast hand's: the most: %f",velocity.k);
  /* a wall between the eyes and the hand: from where the game throws */
  vector_set(&hand,0.0f,0.2f,0.0f);
  wall_before_hand=TRUE;
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  origin.x=origin.y=origin.z=0.0f;velocity=game_velocity;
  CHECK(network_vr_grenade_throw(0,&origin,&velocity) && origin.x==0.0f && velocity.j>0.0f,
   "a wall: the hand's way, the game's place");
  wall_before_hand=FALSE;
  /* a hand out of reach brought to it; not a number refused */
  vector_set(&offset,3.0f,0.0f,0.0f);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  network_vr_grenade_throw(0,&origin,&velocity);
  CHECK(fabsf(origin.x-10.0f-NETWORK_VR_HAND_REACH)<1e-4f,"no further than a hand's reach: %f",origin.x);
  vector_set(&hand,sqrtf(-1.0f),0.0f,0.0f);vector_set(&offset,0.1f,0.0f,0.0f);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  velocity=game_velocity;
  CHECK(!network_vr_grenade_throw(0,&origin,&velocity) && velocity.i==0.5f,"a velocity not a number refused");
  vector_set(&hand,0.0f,0.2f,0.0f);vector_set(&offset,0.0f,sqrtf(-1.0f),0.0f);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  origin.x=origin.y=origin.z=0.0f;
  CHECK(!network_vr_grenade_throw(0,&origin,&velocity) && origin.y==0.0f,"a place not a number refused");
  /* an ordinary server's host keeps none; a VR server's client tells the
  host, reliably, and keeps none itself */
  vector_set(&hand,0.0f,0.04f,0.0f);vector_set(&offset,0.1f,0.0f,0.0f);
  connection=_game_connection_network_server;game.cooperative_flags=0;
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  CHECK(!network_vr_grenade_throw(0,&origin,&velocity),"an ordinary server's host: none kept");
  server_alone=TRUE;
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  CHECK(network_vr_grenade_throw(0,&origin,&velocity),"a local game's server: kept");
  server_alone=FALSE;
  connection=_game_connection_network_client;
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  CHECK(sent_throws==0 && !network_vr_grenade_throw(0,&origin,&velocity),"an ordinary server's client: nothing sent, none kept");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  network_vr_throw(distributed_player_from_byte(0),&hand,&offset);
  CHECK(sent_throws==1 && ((struct distributed_vr_throw_message *)last_message)->entry.player_index==0,
   "a VR server's client: sent reliably, of its player");
  CHECK(network_vr_grenade_throw(0,&origin,&velocity),"kept by the client too, for the throw it predicts");
  /* the host takes a throw only reliably, only of the sender's own player,
  only in a VR server's game */
  connection=_game_connection_network_server;
  CHECK(network_vr_throw_entry_size()==14,"the entry is 14 bytes");
  entry=((struct distributed_vr_throw_message *)last_message)->entry;
  entry.player_index=1;
  network_vr_handle_throw(2,&entry,1,FALSE);
  CHECK(!network_vr_grenade_throw(1,&origin,&velocity),"a throw in a datagram refused");
  network_vr_handle_throw(3,&entry,1,TRUE);
  CHECK(!network_vr_grenade_throw(1,&origin,&velocity),"another machine's player's refused");
  game.cooperative_flags=0;
  network_vr_handle_throw(2,&entry,1,TRUE);
  CHECK(!network_vr_grenade_throw(1,&origin,&velocity),"an ordinary server's refused");
  game.cooperative_flags=FLAG(_network_game_port_vr_rules_bit);
  network_vr_handle_throw(2,&entry,2,TRUE);
  CHECK(!network_vr_grenade_throw(1,&origin,&velocity),"two entries refused");
  network_vr_handle_throw(2,&entry,1,TRUE);
  velocity=game_velocity;
  CHECK(network_vr_grenade_throw(1,&origin,&velocity) && fabsf(velocity.j-0.5f*0.04f/NETWORK_VR_THROW_FULL)<1e-3f,
   "its own player's taken, its velocity through the wire: %f",velocity.j);
 } else return 2;
 return 0;
}
