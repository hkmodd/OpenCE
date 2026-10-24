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
static int sent_down[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static byte last_message[128];
static void distributed_send(void *message, byte type, short count, word size, short destination)
{
 if (type == _distributed_message_vr_pose_up && count == 1 && destination == _distributed_to_host) sent_up++;
 memcpy(last_message, message, size);
}
static void distributed_send_to_machine(long machine_index, void *message, byte type, short count, word size)
{
 if (type == _distributed_message_vr_pose_down && count == 1) sent_down[machine_index]++;
 memcpy(last_message, message, size);
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
static struct network_vr_pose_entry entry_of(short player_index, struct network_vr_pose const *pose)
{
 struct network_vr_pose_entry entry;
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
  struct network_vr_pose_entry entry=entry_of(5,&pose);
  CHECK(network_vr_entry_size()==56,"the entry is 56 bytes: %d",(int)network_vr_entry_size());
  CHECK(entry.player_index==5,"its player");
  network_vr_pose_unpack(&entry,&back);
  CHECK(back.flags==pose.flags,"the flags");
  CHECK(fabsf(back.head.k-pose.head.k)<0.5f/4096.0f && fabsf(back.hands[1].j-pose.hands[1].j)<0.5f/4096.0f,
   "places to 1/4096 of a world unit: %f %f",back.head.k,back.hands[1].j);
  CHECK(fabsf(back.hand_forwards[0].k+1.0f)<1e-4f && fabsf(back.head_up.k-1.0f)<1e-4f,"axes whole");
  CHECK(network_vr_pose_valid(&back),"fit to take after the wire");
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
 } else CASE("host") {
  struct network_vr_pose pose=pose_of();
  struct network_vr_pose_entry entry;
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
  CHECK(((struct network_vr_pose_message *)last_message)->entry.player_index==1,"of the player");
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
  struct network_vr_pose_entry entry;
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
 } else return 2;
 return 0;
}
