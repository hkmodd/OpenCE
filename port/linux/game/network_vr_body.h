#ifndef NETWORK_VR_BODY_H
#define NETWORK_VR_BODY_H
#include "cseries.h"
#include "math/real_math.h"
/* the other headsets' players' bodies in a VR server's game, drawn as their
headsets pose them (network_vr_body.c, from network_vr.c's poses) */

/* any player's pose is kept: the bodies are to be posed */
boolean network_vr_bodies(void);
/* (render_interpolation.c, while a frame is drawn) the node matrices to
draw an object by: a headset's player's body with its head and arms as the
headset poses them, and what that body holds moved with its hand; else
nodes (as drawn: blended between the ticks, or NULL for the object's own).
frame: the frame drawn, which the poses are found once in */
real_matrix4x3 *network_vr_body_node_matrices(long object_index, real_matrix4x3 *nodes, long frame);
#endif
