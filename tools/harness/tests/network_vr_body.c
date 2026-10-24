#include "harness.h"
#include <math.h>
/* The other headsets' players' arms (port/linux/game/network_vr_body.c): an
arm reaches its wrist's place with its own bones' lengths, its elbow bent
down, straight toward a place beyond its reach, its hand turned from the
game's aim to its controller's way. */
typedef struct real_matrix4x3
{
 real scale;
 real_vector3d forward, left, up;
 real_point3d position;
} real_matrix4x3;
#define square_root(x) ((real)sqrt(x))
static real dot_product3d(real_vector3d const *a, real_vector3d const *b){return a->i*b->i+a->j*b->j+a->k*b->k;}
static real_vector3d *cross_product3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *result)
{
 real_vector3d c;
 c.i=a->j*b->k-a->k*b->j;c.j=a->k*b->i-a->i*b->k;c.k=a->i*b->j-a->j*b->i;
 *result=c;return result;
}
static real normalize3d(real_vector3d *v)
{
 real length=(real)sqrt(dot_product3d(v,v));
 if (length<1e-6f) return 0.0f;
 v->i/=length;v->j/=length;v->k/=length;return length;
}
static real_vector3d *vector_from_points3d(real_point3d const *a, real_point3d const *b, real_vector3d *result)
{
 result->i=b->x-a->x;result->j=b->y-a->y;result->k=b->z-a->z;return result;
}
static real_point3d *point_from_line3d(real_point3d const *p, real_vector3d const *v, real t, real_point3d *result)
{
 result->x=v->i*t+p->x;result->y=v->j*t+p->y;result->z=v->k*t+p->z;return result;
}
static real_vector3d *scale_vector3d(real_vector3d const *a, real c, real_vector3d *result)
{
 result->i=c*a->i;result->j=c*a->j;result->k=c*a->k;return result;
}
#include "under_test.inc"

enum { UPPER, FORE, HAND, NODES };

static void set(real_vector3d *v, real i, real j, real k){v->i=i;v->j=j;v->k=k;}
static void place(real_point3d *p, real x, real y, real z){p->x=x;p->y=y;p->z=z;}
static real distance(real_point3d const *a, real_point3d const *b)
{
 real_vector3d v;
 vector_from_points3d(a,b,&v);
 return (real)sqrt(dot_product3d(&v,&v));
}

/* a right arm held down from its shoulder at (0, -0.06, 0.6), facing +x
(left +y, up +z): upper arm 0.1, forearm 0.1, each node's forward along
its bone */
static void arm_of(real_matrix4x3 *nodes)
{
 short index;
 memset(nodes,0,NODES*sizeof(*nodes));
 for (index=0;index<NODES;index++)
 {
  nodes[index].scale=1.0f;
  set(&nodes[index].forward,0,0,-1);set(&nodes[index].left,0,1,0);set(&nodes[index].up,1,0,0);
 }
 place(&nodes[UPPER].position,0,-0.06f,0.6f);
 place(&nodes[FORE].position,0,-0.06f,0.5f);
 place(&nodes[HAND].position,0,-0.06f,0.4f);
}

static void reach(real_matrix4x3 *nodes, real_point3d const *grip, real_vector3d const *forward, real_vector3d const *up)
{
 real_matrix4x3 game[NODES], aim;
 real_vector3d outward, aim_up;
 memcpy(game,nodes,sizeof(game));
 set(&aim.forward,1,0,0);set(&aim_up,0,0,1);
 network_vr_body_frame(&aim.forward,&aim_up,&aim);
 set(&outward,0,-1,0);
 network_vr_body_arm(game,nodes,UPPER,FORE,HAND,grip,forward,up,&aim,&outward);
}

int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 real_matrix4x3 nodes[NODES];
 real_point3d grip, wrist;
 real_vector3d forward, up, bone;
 CASE("reach") {
  arm_of(nodes);
  place(&grip,0.15f,-0.1f,0.55f);set(&forward,1,0,0);set(&up,0,0,1);
  reach(nodes,&grip,&forward,&up);
  point_from_line3d(&grip,&forward,-NETWORK_VR_BODY_WRIST_BACK,&wrist);
  CHECK(distance(&nodes[HAND].position,&wrist)<1e-4f,"the wrist behind the grip: %f",distance(&nodes[HAND].position,&wrist));
  CHECK(fabsf(distance(&nodes[UPPER].position,&nodes[FORE].position)-0.1f)<1e-4f,"the upper arm's length kept");
  CHECK(fabsf(distance(&nodes[FORE].position,&nodes[HAND].position)-0.1f)<1e-4f,"the forearm's length kept");
  CHECK(nodes[FORE].position.z<0.6f && nodes[FORE].position.z<nodes[HAND].position.z,"the elbow bent down: %f",
   nodes[FORE].position.z);
  CHECK(nodes[UPPER].position.y==-0.06f && nodes[UPPER].position.z==0.6f,"the shoulder kept");
  vector_from_points3d(&nodes[UPPER].position,&nodes[FORE].position,&bone);normalize3d(&bone);
  CHECK(dot_product3d(&nodes[UPPER].forward,&bone)>0.9999f,"the upper arm turned along its bone");
  vector_from_points3d(&nodes[FORE].position,&nodes[HAND].position,&bone);normalize3d(&bone);
  CHECK(dot_product3d(&nodes[FORE].forward,&bone)>0.9999f,"the forearm turned along its bone");
  CHECK(fabsf(dot_product3d(&nodes[UPPER].forward,&nodes[UPPER].left))<1e-4f &&
   fabsf(dot_product3d(&nodes[FORE].up,&nodes[FORE].left))<1e-4f,"the bones' axes square");
 } else CASE("straight") {
  arm_of(nodes);
  place(&grip,0.6f,-0.06f,0.6f);set(&forward,1,0,0);set(&up,0,0,1);
  reach(nodes,&grip,&forward,&up);
  CHECK(fabsf(distance(&nodes[UPPER].position,&nodes[HAND].position)-0.2f*NETWORK_VR_BODY_STRAIGHT)<1e-4f,
   "beyond its reach the arm straight: %f",distance(&nodes[UPPER].position,&nodes[HAND].position));
  CHECK(fabsf(nodes[HAND].position.y+0.06f)<1e-4f && fabsf(nodes[HAND].position.z-0.6f)<1e-4f,
   "toward the place it reaches for");
 } else CASE("hand") {
  arm_of(nodes);
  /* (the game's aim +x: the hand's forward -z, as held; the controller
  pointing +y turns it as the aim turns to +y) */
  place(&grip,0.1f,0.0f,0.5f);set(&forward,0,1,0);set(&up,0,0,1);
  reach(nodes,&grip,&forward,&up);
  CHECK(fabsf(nodes[HAND].forward.k+1.0f)<1e-4f,"the hand's forward, square to the turn, kept");
  CHECK(fabsf(nodes[HAND].up.j-1.0f)<1e-4f,"the hand's up, along the aim, turned with the aim to the controller's: %f %f %f",
   nodes[HAND].up.i,nodes[HAND].up.j,nodes[HAND].up.k);
  arm_of(nodes);
  set(&forward,0,0,0);
  reach(nodes,&grip,&forward,&up);
  CHECK(nodes[HAND].position.z==0.4f && nodes[FORE].position.z==0.5f,"a controller with no way: the arm as the game poses it");
 } else return 2;
 return 0;
}
