/*
SHADOW_SILHOUETTE.H
*/

#ifndef __SHADOW_SILHOUETTE_H
#define __SHADOW_SILHOUETTE_H

/* ---------- prototypes/SHADOW_SILHOUETTE.C */

/* a model part's shadow, drawn into a shadow map between the two: names the
extent of the part's vertices to the device (halo_draw_bounds) */
void shadow_silhouette_bounds_begin(
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer);
void shadow_silhouette_bounds_end(
	void);
/* forgets the parts read, whose buffers go with the map's data */
void shadow_silhouette_flush(
	void);

#endif // __SHADOW_SILHOUETTE_H
