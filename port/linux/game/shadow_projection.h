/*
SHADOW_PROJECTION.H

The scissor of a shadow's projection onto the environment (shadow_projection.c).
*/

#ifndef SHADOW_PROJECTION_H
#define SHADOW_PROJECTION_H

/* around the draw of a shadow's projection (rasterizer_xbox_shadows.c):
cuts the pixels drawn to those the projection can change, from the shadow's
matrix and its object's bounding radius */
void shadow_projection_scissor_begin(real_matrix4x3 const *shadow_matrix, real radius);
void shadow_projection_scissor_end(void);

#endif
