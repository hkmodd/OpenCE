/*
SHADOW_SILHOUETTE.C

The extent of a model part's shadow in its shadow map, for the shadow maps'
blurs to draw only where a shadow is (port/linux/src/d3d8_gl.c, target
bounds): for each node a part's compressed vertices name, the least and the
greatest x, y and z of the vertices its triangles use that name the node, in
the node's space, read from the vertex and index buffers the game draws them
from (as object_mesh.c reads them), once a part. The device places each box's
corners as the shadows' model vertex programs place a vertex, with the draw's
own node matrices. A part that cannot be read so (an index past its vertices,
a weight below zero, a position not a number, more nodes than the device
takes) names nothing, and its shadow map is blurred whole.
*/

#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"

#include "shadow_silhouette.h"

#include <xtl.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	SHADOW_SILHOUETTE_NODES = 32, /* (d3d8_gl.c's DRAW_BOUNDS_NODES) */
	/* (the parts read, a slot each by their buffers) */
	SHADOW_SILHOUETTE_PARTS = 2048,
};

/* ---------- structures */

struct shadow_silhouette_part
{
	void const *vertex_hardware_format;
	void const *triangle_hardware_format;
	long vertex_count;
	long triangle_count;
	short triangle_type;
	/* NONE where the part cannot be read */
	short node_count;
	/* each node's index as the vertices carry it, then its box's least x, y
	and z and its greatest (halo_draw_bounds) */
	real nodes[SHADOW_SILHOUETTE_NODES][7];
};

/* ---------- globals */

static struct shadow_silhouette_part *shadow_silhouette_parts;
static boolean shadow_silhouette_named;

/* ---------- private code */

static boolean shadow_silhouette_point_valid(
	real_point3d const *point)
{
	/* (not a number, or infinite) */
	return point->x >= -FLT_MAX && point->x <= FLT_MAX && point->y >= -FLT_MAX && point->y <= FLT_MAX &&
		point->z >= -FLT_MAX && point->z <= FLT_MAX;
}

static void shadow_silhouette_read(
	struct shadow_silhouette_part *part,
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer)
{
	short node_slots[256];
	byte *vertices = NULL;
	BYTE *indices = NULL;
	long index_count = triangle_buffer->type == _triangle_buffer_type_precompiled_strip ?
		triangle_buffer->count + 2 : triangle_buffer->count * 3;
	boolean valid;
	long index;

	IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format, 0, 0, &vertices, D3DLOCK_READONLY);
	IDirect3DIndexBuffer8_Lock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format, 0, 0, &indices, D3DLOCK_READONLY);
	valid = vertices && indices;
	part->node_count = 0;
	memset(node_slots, 0xff, sizeof(node_slots));
	for (index = 0; valid && index < index_count; index++)
	{
		unsigned short vertex_index = ((unsigned short const *)indices)[index];
		struct model_vertex_compressed const *vertex;
		short slot;

		if (vertex_index >= vertex_buffer->count)
		{
			valid = FALSE;
			break;
		}
		vertex = (struct model_vertex_compressed const *)(vertices + vertex_index * sizeof(struct model_vertex_compressed));
		if (vertex->node_weight < 0 || !shadow_silhouette_point_valid(&vertex->position))
		{
			valid = FALSE;
			break;
		}
		for (slot = 0; slot < 2; slot++)
		{
			byte node = vertex->nodes[slot];
			real *box;

			if (node_slots[node] == NONE)
			{
				if (part->node_count == SHADOW_SILHOUETTE_NODES)
				{
					valid = FALSE;
					break;
				}
				node_slots[node] = part->node_count++;
				box = part->nodes[node_slots[node]];
				box[0] = (real)node;
				box[1] = box[4] = vertex->position.x;
				box[2] = box[5] = vertex->position.y;
				box[3] = box[6] = vertex->position.z;
			}
			else
			{
				box = part->nodes[node_slots[node]];
				box[1] = MIN(box[1], vertex->position.x);
				box[2] = MIN(box[2], vertex->position.y);
				box[3] = MIN(box[3], vertex->position.z);
				box[4] = MAX(box[4], vertex->position.x);
				box[5] = MAX(box[5], vertex->position.y);
				box[6] = MAX(box[6], vertex->position.z);
			}
		}
	}
	if (vertices)
	{
		IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format);
	}
	if (indices)
	{
		IDirect3DIndexBuffer8_Unlock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format);
	}
	if (!valid)
	{
		part->node_count = NONE;
	}

	return;
}

static struct shadow_silhouette_part const *shadow_silhouette_part_get(
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer)
{
	struct shadow_silhouette_part *part;
	unsigned long hash;

	if (!shadow_silhouette_parts)
	{
		shadow_silhouette_parts = calloc(SHADOW_SILHOUETTE_PARTS, sizeof(*shadow_silhouette_parts));
		if (!shadow_silhouette_parts)
		{
			return NULL;
		}
	}
	hash = ((unsigned long)vertex_buffer->hardware_format >> 4) * 31 ^ ((unsigned long)triangle_buffer->hardware_format >> 4);
	part = &shadow_silhouette_parts[hash % SHADOW_SILHOUETTE_PARTS];
	if (part->vertex_hardware_format != vertex_buffer->hardware_format ||
		part->triangle_hardware_format != triangle_buffer->hardware_format ||
		part->vertex_count != vertex_buffer->count || part->triangle_count != triangle_buffer->count ||
		part->triangle_type != triangle_buffer->type)
	{
		part->vertex_hardware_format = vertex_buffer->hardware_format;
		part->triangle_hardware_format = triangle_buffer->hardware_format;
		part->vertex_count = vertex_buffer->count;
		part->triangle_count = triangle_buffer->count;
		part->triangle_type = triangle_buffer->type;
		shadow_silhouette_read(part, triangle_buffer, vertex_buffer);
	}

	return part;
}

/* ---------- public code */

void shadow_silhouette_bounds_begin(
	struct triangle_buffer const *triangle_buffer,
	struct vertex_buffer const *vertex_buffer)
{
	struct shadow_silhouette_part const *part;

	/* (only the layered blur draws within the bounds) */
	if (!halo_shadow_layers_supported() || !triangle_buffer || !vertex_buffer ||
		!triangle_buffer->hardware_format || !vertex_buffer->hardware_format ||
		vertex_buffer->type != _rasterizer_vertex_type_model_compressed ||
		vertex_buffer->count <= 0 || triangle_buffer->count <= 0 ||
		(triangle_buffer->type != _triangle_buffer_type_triangles &&
		triangle_buffer->type != _triangle_buffer_type_precompiled_strip))
	{
		return;
	}
	part = shadow_silhouette_part_get(triangle_buffer, vertex_buffer);
	if (part && part->node_count > 0)
	{
		halo_draw_bounds(part->node_count, (float const (*)[7])part->nodes);
		shadow_silhouette_named = TRUE;
	}

	return;
}

void shadow_silhouette_bounds_end(
	void)
{
	if (shadow_silhouette_named)
	{
		halo_draw_bounds(0, NULL);
		shadow_silhouette_named = FALSE;
	}

	return;
}

void shadow_silhouette_flush(
	void)
{
	if (shadow_silhouette_parts)
	{
		memset(shadow_silhouette_parts, 0, SHADOW_SILHOUETTE_PARTS * sizeof(*shadow_silhouette_parts));
	}

	return;
}
