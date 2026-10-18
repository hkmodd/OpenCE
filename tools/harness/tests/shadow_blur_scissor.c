/*
SHADOW_BLUR_SCISSOR.C (test)

The shadow maps' bounds and the blur drawn within them (d3d8_gl.c's target
bounds and shadow_instances_draw) against a model of what the game draws into
the maps: a part's silhouette, each vertex placed as the shadows' model vertex
program places it (blended between two nodes' rows, the registers it reads,
its epilogue, OpenGL's viewport with rows from the top) and its triangles
filled at pixel centres; the maps' clears; and each pass of the blur, whose
four taps (rasterizer_shadow_convolve's corners) are sampled bilinearly
around each pixel's centre, with the hardware's fixed-point weights, and are
black together only where every texel they read is. A pass drawn through the
bounds must leave every destination exactly as the whole pass leaves it, and
a map's pixels outside its bound must be black.
test_shadow_blur_scissor.py takes the code (under_test.inc) from the sources.
*/

#include "harness.h"
#include <math.h>

#include "under_test_defines.inc"

/* the graphics layer: the members the bounds read */
typedef int BOOL;
typedef int GLint;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLsizei;
typedef unsigned long DWORD;
typedef DWORD D3DCOLOR;
typedef struct { long x1, y1, x2, y2; } D3DRECT;
#define CONST const
#define GL_SCISSOR_TEST 0x0C11
#define GL_TRIANGLE_FAN 0x0006
enum { D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE, D3DRS_COLORWRITEENABLE, D3DRS_MAX };
enum { D3DTSS_ALPHAKILL, D3DTSS_MAX };
#define D3DTALPHAKILL_ENABLE 4
#define D3DCOLORWRITEENABLE_RED 0x1
#define D3DCOLORWRITEENABLE_GREEN 0x2
#define D3DCOLORWRITEENABLE_BLUE 0x4
#define D3DCOLORWRITEENABLE_ALPHA 0x8
#define D3DCOLORWRITEENABLE_ALL 0xF
#define D3DCLEAR_TARGET_R 0x10
#define D3DCLEAR_TARGET_G 0x20
#define D3DCLEAR_TARGET_B 0x40
#define D3DCLEAR_TARGET_A 0x80
#define D3DCLEAR_RGB (D3DCLEAR_TARGET_R | D3DCLEAR_TARGET_G | D3DCLEAR_TARGET_B)
#define D3DSCM_NORESERVEDCONSTANTS 0x2
static float ui_offset;
#define UI_OFFSET ((GLint)ui_offset)

enum { _render_target_texture, _render_target_shadow_map };

struct render_target_entry
{
	int kind;
	struct { unsigned long written; unsigned short gl_width, gl_height; } target;
	GLint bound[4];
	GLint bound_edge;
	unsigned long bound_written;
	/* the model's: a pixel's colour, 0 for black, rows from the top */
	unsigned char *pixels;
};

static struct
{
	float constants[XGPU_VERTEX_CONSTANT_COUNT][4];
	float viewport_scale[4], viewport_offset[4];
	DWORD shader_constant_mode;
	struct { DWORD X, Y, Width, Height; } viewport;
} device;
static float target_scale[2] = { 1.0f, 1.0f };
static struct { GLint viewport[4]; GLint scissor[4]; BOOL scissor_test; } gl_state;
static DWORD render_state[D3DRS_MAX];
static DWORD texture_state[4][D3DTSS_MAX];

/* OpenGL: each viewport index's scissor, and the draws, which the model draws */
static GLint gl_scissors[SHADOW_LAYER_INSTANCES][4];
static BOOL gl_scissor_enabled;
static int scissor_calls, draw_calls;
static void model_draw(GLsizei instances);

static void glEnable(GLenum capability)
{
	if (capability == GL_SCISSOR_TEST)
		gl_scissor_enabled = TRUE;
}

static void glScissorIndexed(GLuint index, GLint x, GLint y, GLsizei width, GLsizei height)
{
	CHECK(index < SHADOW_LAYER_INSTANCES, "scissor %u", index);
	CHECK(width >= 0 && height >= 0, "scissor %d %d %d %d", x, y, width, height);
	gl_scissors[index][0] = x;
	gl_scissors[index][1] = y;
	gl_scissors[index][2] = width;
	gl_scissors[index][3] = height;
	scissor_calls++;
}

static void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
	CHECK(mode == GL_TRIANGLE_FAN && first == 0 && count == 4, "the draw changed");
	draw_calls++;
	model_draw(instances);
}

#include "under_test.inc"

/* ---------- the model */

enum
{
	MAP_MAXIMUM = 512,
	PART_VERTICES = 48,
	PART_TRIANGLES = 64,
};

static unsigned long seed = 12345;
static float uniform(float low, float high)
{
	seed = seed * 1103515245UL + 12345UL;
	return low + (high - low) * (float)((seed >> 8) & 0xFFFFFF) / (float)0xFFFFFF;
}

static int pick(int count)
{
	seed = seed * 1103515245UL + 12345UL;
	return (int)((seed >> 8) % (unsigned long)count);
}

static unsigned long serial = 1000;
/* the maps' size: the game takes them to be 128 wide */
static int map_size = 128;
static struct render_target_entry maps[2][SHADOW_LAYER_INSTANCES];
/* the draw's viewport, which the blur's quad covers */
static GLint draw_viewport[4];
static float pass_reach;
static BOOL taps_clamped;

/* bind_targets: the colour target recorded, then written */
static void target_bind(struct render_target_entry *map)
{
	bound_target = map;
	bound_target_previous = map->target.written;
	map->target.written = ++serial;
}

/* apply_raster_state: the viewport, the scissor the viewport (every index's, glScissor) */
static void raster_state(GLint x, GLint y, GLint width, GLint height)
{
	int index;

	target_scale[0] = target_scale[1] = (float)map_size / 128.0f;
	device.viewport.X = (DWORD)((float)x / target_scale[0]);
	device.viewport.Y = (DWORD)((float)y / target_scale[1]);
	device.viewport.Width = (DWORD)((float)width / target_scale[0]);
	device.viewport.Height = (DWORD)((float)height / target_scale[1]);
	gl_state.viewport[0] = gl_state.scissor[0] = draw_viewport[0] = x;
	gl_state.viewport[1] = gl_state.scissor[1] = draw_viewport[1] = y;
	gl_state.viewport[2] = gl_state.scissor[2] = draw_viewport[2] = width;
	gl_state.viewport[3] = gl_state.scissor[3] = draw_viewport[3] = height;
	gl_state.scissor_test = gl_scissor_enabled = width > 0 && height > 0;
	for (index = 0; index < SHADOW_LAYER_INSTANCES; index++)
		memcpy(gl_scissors[index], gl_state.scissor, sizeof(gl_state.scissor));
}

static void blur_states(void)
{
	memset(render_state, 0, sizeof(render_state));
	memset(texture_state, 0, sizeof(texture_state));
	render_state[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
}

/* a map the game made, holding anything, its bound left from before */
static void map_new(struct render_target_entry *map, int size)
{
	int index;

	free(map->pixels);
	map->kind = _render_target_shadow_map;
	map->target.gl_width = map->target.gl_height = (unsigned short)size;
	map->pixels = malloc((size_t)size * size);
	for (index = 0; index < size * size; index++)
		map->pixels[index] = (unsigned char)(pick(4) ? 0 : 1 + pick(255));
	map->bound[0] = map->bound[1] = size / 2 - 2;
	map->bound[2] = map->bound[3] = size / 2 + 2;
	map->bound_edge = 0;
	map->bound_written = ++serial;
	map->target.written = ++serial;
}

/* a texel the taps read: a border of zero (rasterizer_shadow_convolve's), or clamped to the edge */
static unsigned char texel(const struct render_target_entry *map, int x, int y)
{
	int width = map->target.gl_width, height = map->target.gl_height;

	if (x < 0 || y < 0 || x >= width || y >= height)
	{
		if (!taps_clamped)
			return 0;
		x = x < 0 ? 0 : x >= width ? width - 1 : x;
		y = y < 0 ? 0 : y >= height ? height - 1 : y;
	}
	return map->pixels[y * width + x];
}

/* a pixel of a pass: the four taps at its centre's texture coordinate, the
pass's reach away diagonally, each bilinear with weights in 256ths; their sum
is black only where every texel weighed is */
static unsigned char blur_pixel(const struct render_target_entry *source, int x, int y)
{
	static const int corners[4][2] = { { -1, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 } };
	float width = (float)source->target.gl_width, height = (float)source->target.gl_height;
	double sum = 0.0;
	int tap, i, j;

	for (tap = 0; tap < 4; tap++)
	{
		float u = ((float)x + 0.5f) / width + (float)corners[tap][0] * pass_reach;
		float v = ((float)y + 0.5f) / height + (float)corners[tap][1] * pass_reach;
		float tx = u * width - 0.5f, ty = v * height - 0.5f;
		int x0 = (int)floorf(tx), y0 = (int)floorf(ty);
		long a = (long)((tx - (float)x0) * 256.0f), b = (long)((ty - (float)y0) * 256.0f);

		for (j = 0; j < 2; j++)
		{
			for (i = 0; i < 2; i++)
				sum += (double)((i ? a : 256 - a) * (j ? b : 256 - b)) * texel(source, x0 + i, y0 + j);
		}
	}
	return sum > 0.0 ? (unsigned char)(1.0 + (sum - 1.0) * 254.0 / (4.0 * 65536.0 * 255.0)) : 0;
}

static BOOL rect_holds(const GLint *rect, int x, int y)
{
	return x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3];
}

/* the layered draw: each instance's pass from its source into its destination,
at the pixels of its viewport, in its scissor */
static void model_draw(GLsizei instances)
{
	int instance, x, y;

	CHECK(instances == shadow_instances.count, "instances %d of %d", instances, shadow_instances.count);
	for (instance = 0; instance < instances; instance++)
	{
		const struct render_target_entry *source = shadow_instances.sources[instance];
		struct render_target_entry *destination = shadow_instances.destinations[instance];
		int width = destination->target.gl_width, height = destination->target.gl_height;

		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				if (rect_holds(draw_viewport, x, y) && (!gl_scissor_enabled || rect_holds(gl_scissors[instance], x, y)))
					destination->pixels[y * width + x] = blur_pixel(source, x, y);
			}
		}
	}
}

/* every pixel outside a valid bound and its edges' band is black */
static void check_bound(const struct render_target_entry *map, const char *what)
{
	int width = map->target.gl_width, height = map->target.gl_height, edge = map->bound_edge, x, y;

	if (!target_bound_valid(map, map->target.written))
		return;
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			BOOL inside = (x >= map->bound[0] && x < map->bound[2] && y >= map->bound[1] && y < map->bound[3]) ||
				x < edge || y < edge || x >= width - edge || y >= height - edge;

			CHECK(inside || !map->pixels[y * width + x], "%s: pixel %d,%d is %u outside the bound %d %d %d %d edge %d",
				what, x, y, map->pixels[y * width + x], map->bound[0], map->bound[1], map->bound[2], map->bound[3], edge);
		}
	}
}

/* a clear of a map (run_clear), and what it does to the pixels */
static void map_clear(struct render_target_entry *map, DWORD count, const D3DRECT *rectangles, DWORD flags, D3DCOLOR color)
{
	int width = map->target.gl_width, height = map->target.gl_height, x, y;
	BOOL black = !(color & 0xFFFFFF) || !(flags & D3DCLEAR_RGB);
	BOOL every_channel = (flags & D3DCLEAR_RGB) == D3DCLEAR_RGB;
	DWORD index;

	target_bind(map);
	target_bound_clear(count, rectangles, flags, color);
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			BOOL cleared = rect_holds(draw_viewport, x, y);

			if (count && rectangles)
			{
				cleared = FALSE;
				for (index = 0; index < count; index++)
				{
					if (x >= rectangles[index].x1 && x < rectangles[index].x2 && y >= rectangles[index].y1 &&
						y < rectangles[index].y2 && rect_holds(draw_viewport, x, y))
					{
						cleared = TRUE;
					}
				}
			}
			if (!cleared || !(flags & D3DCLEAR_RGB))
				continue;
			/* (a channel left as it was may hold colour) */
			if (!black)
				map->pixels[y * width + x] = 200;
			else if (every_channel)
				map->pixels[y * width + x] = 0;
		}
	}
}

/* ---------- silhouettes */

struct part
{
	int vertex_count, triangle_count, boxes;
	float positions[PART_VERTICES][3];
	byte nodes[PART_VERTICES][2];
	float weights[PART_VERTICES];
	int triangles[PART_TRIANGLES][3];
};

/* the first of the node rows a node byte selects (vertex program: c[60 + b / 255 * c[7].w], each row's
register at most the last) */
static int node_register(byte node)
{
	return (int)floorf((float)node / 255.0f * device.constants[7][3] + 0.001f) + 60;
}

static void rotation(float (*rows)[3])
{
	float a = uniform(0.0f, 6.2831853f), b = uniform(0.0f, 6.2831853f), c = uniform(-1.0f, 1.0f), s = sqrtf(1.0f - c * c);
	float axis[3], ca = cosf(b), sa = sinf(b), t = 1.0f - ca;

	axis[0] = s * cosf(a);
	axis[1] = s * sinf(a);
	axis[2] = c;
	rows[0][0] = t * axis[0] * axis[0] + ca;
	rows[0][1] = t * axis[0] * axis[1] - sa * axis[2];
	rows[0][2] = t * axis[0] * axis[2] + sa * axis[1];
	rows[1][0] = t * axis[0] * axis[1] + sa * axis[2];
	rows[1][1] = t * axis[1] * axis[1] + ca;
	rows[1][2] = t * axis[1] * axis[2] - sa * axis[0];
	rows[2][0] = t * axis[0] * axis[2] - sa * axis[1];
	rows[2][1] = t * axis[1] * axis[2] + sa * axis[0];
	rows[2][2] = t * axis[2] * axis[2] + ca;
}

/* the constants: noise everywhere, then a light's view onto a map the game
takes to be 128 texels wide, as the device converts it: orthographic, or a
perspective from near the object; and every node's rows (a rotation, a scale
and a place near the origin) */
static void constants_random(BOOL perspective, BOOL nodes)
{
	float (*c)[4] = device.constants;
	float frame[3][3], eye[3];
	int reg, row, i;

	for (reg = 0; reg < XGPU_VERTEX_CONSTANT_COUNT; reg++)
	{
		for (i = 0; i < 4; i++)
			c[reg][i] = uniform(-2.0f, 2.0f);
	}
	rotation(frame);
	if (perspective)
	{
		float focal = uniform(0.8f, 3.0f), distance = uniform(0.2f, 6.0f);

		for (i = 0; i < 3; i++)
			eye[i] = -frame[2][i] * distance + uniform(-0.3f, 0.3f);
		for (i = 0; i < 3; i++)
		{
			c[28][i] = frame[0][i] * focal;
			c[29][i] = frame[1][i] * focal;
			c[30][i] = frame[2][i] * 0.01f;
			c[31][i] = frame[2][i];
		}
		c[28][3] = -(c[28][0] * eye[0] + c[28][1] * eye[1] + c[28][2] * eye[2]);
		c[29][3] = -(c[29][0] * eye[0] + c[29][1] * eye[1] + c[29][2] * eye[2]);
		c[30][3] = 0.0f;
		c[31][3] = -(c[31][0] * eye[0] + c[31][1] * eye[1] + c[31][2] * eye[2]);
	}
	else
	{
		float extent = uniform(1.2f, 6.0f), x = uniform(-1.0f, 1.0f), y = uniform(-1.0f, 1.0f);

		for (i = 0; i < 3; i++)
		{
			c[28][i] = frame[0][i] / extent;
			c[29][i] = frame[1][i] / extent;
			c[30][i] = frame[2][i] * 0.01f;
			c[31][i] = 0.0f;
		}
		c[28][3] = x;
		c[29][3] = y;
		c[30][3] = 0.5f;
		c[31][3] = 1.0f;
	}
	/* the game's screen conversion for a map of 128, and the device's for its viewport */
	c[58][0] = 64.0f; c[58][1] = -64.0f; c[58][2] = 1.0f; c[58][3] = 0.0f;
	c[59][0] = 63.5f; c[59][1] = 63.5f; c[59][2] = 0.0f; c[59][3] = 0.0f;
	device.viewport_scale[0] = 64.0f; device.viewport_scale[1] = -64.0f;
	device.viewport_scale[2] = 1.0f; device.viewport_scale[3] = 1.0f;
	device.viewport_offset[0] = 64.0f; device.viewport_offset[1] = 64.0f;
	device.viewport_offset[2] = 0.0f; device.viewport_offset[3] = 0.0f;
	c[7][3] = 765.0f;
	device.shader_constant_mode = 0;
	for (reg = 60; nodes && reg + 2 < XGPU_VERTEX_CONSTANT_COUNT; reg += 3)
	{
		float scale = uniform(0.5f, 1.5f);

		rotation(frame);
		for (row = 0; row < 3; row++)
		{
			for (i = 0; i < 3; i++)
				c[reg + row][i] = frame[row][i] * scale;
			c[reg + row][3] = uniform(-0.8f, 0.8f);
		}
	}
}

/* a vertex placed by the shadows' model vertex program (vs 0x27): its two
node indices (bytes over 255) times c[7].w, the first node's rows times the
weight plus the second's times one less it, then c[28]..c[31], and the
epilogue's conversion with c[58] and c[59]; then OpenGL's viewport, rows from
the top. FALSE behind the eye */
static BOOL vertex_place(const struct part *part, int vertex, float *place)
{
	const float (*c)[4] = (const float (*)[4])device.constants;
	const float *p = part->positions[vertex];
	float weight = part->weights[vertex], rows[3][4], world[3], clip[4], x, y;
	int first = node_register(part->nodes[vertex][0]), second = node_register(part->nodes[vertex][1]), row, i;

	for (row = 0; row < 3; row++)
	{
		int a = first + row > 191 ? 191 : first + row, b = second + row > 191 ? 191 : second + row;

		for (i = 0; i < 4; i++)
			rows[row][i] = (1.0f - weight) * c[b][i] + weight * c[a][i];
		world[row] = p[0] * rows[row][0] + p[1] * rows[row][1] + p[2] * rows[row][2] + rows[row][3];
	}
	for (i = 0; i < 4; i++)
		clip[i] = world[0] * c[28 + i][0] + world[1] * c[28 + i][1] + world[2] * c[28 + i][2] + c[28 + i][3];
	if (!(clip[3] > 0.0f))
		return FALSE;
	x = (clip[0] * c[58][0] + (c[59][0] + 0.5f + ui_offset - device.viewport_offset[0]) * clip[3]) /
		device.viewport_scale[0] / clip[3];
	y = (clip[1] * c[58][1] + (c[59][1] + 0.5f - device.viewport_offset[1]) * clip[3]) /
		device.viewport_scale[1] / clip[3];
	place[0] = (float)gl_state.viewport[0] + (x + 1.0f) * 0.5f * (float)gl_state.viewport[2];
	place[1] = (float)gl_state.viewport[1] + (1.0f - y) * 0.5f * (float)gl_state.viewport[3];
	return place[0] == place[0] && place[1] == place[1];
}

/* a part: boxes (each of its eight corners a vertex of one node, its faces'
triangles) and vertices weighted between two of its nodes, or loose triangles */
static void part_random(struct part *part)
{
	byte node_list[4];
	int node_count = 1 + pick(4), index, corner, box;

	for (index = 0; index < node_count; index++)
		node_list[index] = (byte)(pick(8) ? pick(44) : pick(256));
	part->vertex_count = 0;
	part->triangle_count = 0;
	part->boxes = 0;
	for (box = 0; box < node_count && pick(3); box++, part->boxes++)
	{
		static const int faces[12][3] = { { 0, 1, 3 }, { 0, 3, 2 }, { 4, 6, 7 }, { 4, 7, 5 }, { 0, 4, 5 }, { 0, 5, 1 },
			{ 2, 3, 7 }, { 2, 7, 6 }, { 0, 2, 6 }, { 0, 6, 4 }, { 1, 5, 7 }, { 1, 7, 3 } };
		float low[3], high[3];
		int base = part->vertex_count;

		for (index = 0; index < 3; index++)
		{
			low[index] = uniform(-0.6f, 0.2f);
			high[index] = low[index] + uniform(0.05f, 0.6f);
		}
		for (corner = 0; corner < 8; corner++)
		{
			part->positions[base + corner][0] = corner & 1 ? high[0] : low[0];
			part->positions[base + corner][1] = corner & 2 ? high[1] : low[1];
			part->positions[base + corner][2] = corner & 4 ? high[2] : low[2];
			part->nodes[base + corner][0] = part->nodes[base + corner][1] = node_list[box];
			part->weights[base + corner] = 1.0f;
		}
		part->vertex_count += 8;
		for (index = 0; index < 12; index++)
		{
			for (corner = 0; corner < 3; corner++)
				part->triangles[part->triangle_count][corner] = base + faces[index][corner];
			part->triangle_count++;
		}
	}
	while (part->vertex_count < 24)
	{
		index = part->vertex_count++;
		for (corner = 0; corner < 3; corner++)
			part->positions[index][corner] = uniform(-0.7f, 0.7f);
		part->nodes[index][0] = node_list[pick(node_count)];
		part->nodes[index][1] = node_list[pick(node_count)];
		part->weights[index] = pick(4) ? uniform(0.0f, 1.0f) : (float)pick(2);
	}
	while (part->triangle_count < PART_TRIANGLES - 24)
	{
		for (corner = 0; corner < 3; corner++)
			part->triangles[part->triangle_count][corner] = pick(part->vertex_count);
		part->triangle_count++;
	}
}

/* the part's extent as the game names it (shadow_silhouette.c): for each node
byte its vertices carry, the box of the positions of the vertices that carry it */
static void part_extent(const struct part *part, struct draw_bounds_request *request)
{
	int slots[256], vertex, slot;

	memset(slots, 0xff, sizeof(slots));
	request->count = 0;
	for (vertex = 0; vertex < part->vertex_count; vertex++)
	{
		for (slot = 0; slot < 2; slot++)
		{
			byte node = part->nodes[vertex][slot];
			const float *p = part->positions[vertex];
			float *box;
			int i;

			if (slots[node] < 0)
			{
				CHECK(request->count < DRAW_BOUNDS_NODES, "nodes");
				slots[node] = request->count++;
				box = request->nodes[slots[node]];
				box[0] = (float)node;
				for (i = 0; i < 3; i++)
					box[1 + i] = box[4 + i] = p[i];
			}
			box = request->nodes[slots[node]];
			for (i = 0; i < 3; i++)
			{
				box[1 + i] = p[i] < box[1 + i] ? p[i] : box[1 + i];
				box[4 + i] = p[i] > box[4 + i] ? p[i] : box[4 + i];
			}
		}
	}
}

/* fills a triangle at the pixel centres inside it or on its edges, in the
viewport and the scissor; FALSE where a vertex is behind the eye (clipped,
not drawn by the model) */
static BOOL triangle_fill(const struct part *part, const int *triangle, unsigned char *pixels, int width, int height,
	unsigned char value)
{
	float v[3][2], area, low[2], high[2];
	int corner, x, y, x0, x1, y0, y1;

	for (corner = 0; corner < 3; corner++)
	{
		if (!vertex_place(part, triangle[corner], v[corner]))
			return FALSE;
	}
	area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[1][1] - v[0][1]) * (v[2][0] - v[0][0]);
	if (area == 0.0f)
		return TRUE;
	for (corner = 0; corner < 2; corner++)
	{
		low[corner] = v[0][corner] < v[1][corner] ? v[0][corner] : v[1][corner];
		low[corner] = v[2][corner] < low[corner] ? v[2][corner] : low[corner];
		high[corner] = v[0][corner] > v[1][corner] ? v[0][corner] : v[1][corner];
		high[corner] = v[2][corner] > high[corner] ? v[2][corner] : high[corner];
	}
	x0 = low[0] < 0.0f ? 0 : low[0] > (float)width ? width : (int)low[0];
	y0 = low[1] < 0.0f ? 0 : low[1] > (float)height ? height : (int)low[1];
	x1 = high[0] < 0.0f ? 0 : high[0] + 1.0f > (float)width ? width : (int)high[0] + 1;
	y1 = high[1] < 0.0f ? 0 : high[1] + 1.0f > (float)height ? height : (int)high[1] + 1;
	for (y = y0; y < y1; y++)
	{
		for (x = x0; x < x1; x++)
		{
			float px = (float)x + 0.5f, py = (float)y + 0.5f, e[3];

			for (corner = 0; corner < 3; corner++)
			{
				const float *a = v[corner], *b = v[(corner + 1) % 3];

				e[corner] = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
			}
			if (((area > 0.0f && e[0] >= 0.0f && e[1] >= 0.0f && e[2] >= 0.0f) ||
				(area < 0.0f && e[0] <= 0.0f && e[1] <= 0.0f && e[2] <= 0.0f)) &&
				rect_holds(gl_state.viewport, x, y) && (!gl_state.scissor_test || rect_holds(gl_state.scissor, x, y)))
			{
				pixels[y * width + x] = value;
			}
		}
	}
	return TRUE;
}

/* a silhouette drawn into a map: its extent named, the draw recorded
(prepare_draw_as, alone or not), its triangles filled */
static void silhouette_draw(struct render_target_entry *map, BOOL named, BOOL alone)
{
	struct draw_bounds_request request, none;
	struct part part;
	int index;
	unsigned char value = (unsigned char)(1 + pick(255));

	part_random(&part);
	constants_random(pick(3) == 0, TRUE);
	part_extent(&part, &request);
	none.count = 0;
	draw_bounds_command(named ? &request : &none);
	target_bind(map);
	target_bound_draw(alone);
	for (index = 0; index < part.triangle_count; index++)
		triangle_fill(&part, part.triangles[index], map->pixels, map->target.gl_width, map->target.gl_height, value);
	draw_bounds_command(&none);
}

/* a map cleared to black and drawn a silhouette into, until it has a bound */
static void silhouette_bounded(struct render_target_entry *map)
{
	do
	{
		map_clear(map, 0, NULL, D3DCLEAR_RGB, 0);
		silhouette_draw(map, TRUE, TRUE);
	}
	while (!target_bound_valid(map, map->target.written));
}

/* ---------- passes */

static unsigned char *expected[SHADOW_LAYER_INSTANCES];

/* a pass of the blur from each source into its destination, as
shadow_layers_resolve and run_end draw it, against the whole pass */
static void pass(struct render_target_entry *const *sources, struct render_target_entry *const *destinations, int count,
	float reach)
{
	int instance, index, size = destinations[0]->target.gl_width, x, y;

	for (instance = 0; instance < count; instance++)
	{
		shadow_instances.sources[instance] = sources[instance];
		shadow_instances.destinations[instance] = destinations[instance];
		shadow_instances.written[instance] = destinations[instance]->target.written;
	}
	for (instance = 1; instance < count; instance++)
		destinations[instance]->target.written = ++serial;
	shadow_instances.count = count;
	shadow_instances.layered = TRUE;
	shadow_instances.reach = reach;
	pass_reach = reach;
	target_bind(destinations[0]);
	/* (the whole pass, as it was drawn) */
	for (instance = 0; instance < count; instance++)
	{
		free(expected[instance]);
		expected[instance] = malloc((size_t)size * size);
		memcpy(expected[instance], destinations[instance]->pixels, (size_t)size * size);
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
			{
				if (rect_holds(draw_viewport, x, y) && (!gl_state.scissor_test || rect_holds(gl_state.scissor, x, y)))
					expected[instance][y * size + x] = blur_pixel(sources[instance], x, y);
			}
		}
	}
	scissor_calls = draw_calls = 0;
	shadow_instances_draw(GL_TRIANGLE_FAN, 0, 4);
	for (instance = 0; instance < count; instance++)
	{
		for (index = 0; index < size * size; index++)
		{
			CHECK(destinations[instance]->pixels[index] == expected[instance][index],
				"instance %d of %d: pixel %d,%d is %u, the whole pass draws %u (reach %g, map %d, bound %d %d %d %d edge %d)",
				instance, count, index % size, index / size, destinations[instance]->pixels[index], expected[instance][index],
				reach, size, destinations[instance]->bound[0], destinations[instance]->bound[1],
				destinations[instance]->bound[2], destinations[instance]->bound[3], destinations[instance]->bound_edge);
		}
		check_bound(destinations[instance], "the pass's destination");
	}
	/* (glScissor sets every index again before the next draw) */
	raster_state(0, 0, size, size);
}

/* the batched blur of every map (rasterizer_xbox_shadows.c): from the
primary into the secondary, then back and forth, each pass's reach its taps' */
static void blur(int count, int size)
{
	struct render_target_entry *primary[SHADOW_LAYER_INSTANCES], *secondary[SHADOW_LAYER_INSTANCES];
	int scale = size / 128, distance, index;

	for (index = 0; index < count; index++)
	{
		primary[index] = &maps[0][index];
		secondary[index] = &maps[1][index];
	}
	pass(primary, secondary, count, 0.00390625f / (float)scale);
	for (distance = 1; distance < scale; distance *= 2)
	{
		pass(secondary, primary, count, (float)distance / (float)(128 * scale));
		pass(primary, secondary, count, (float)distance / (float)(128 * scale));
	}
}

static void maps_new(int size)
{
	int index;

	for (index = 0; index < SHADOW_LAYER_INSTANCES; index++)
	{
		map_new(&maps[0][index], size);
		map_new(&maps[1][index], size);
	}
	map_size = size;
	raster_state(0, 0, size, size);
}

/* ---------- cases */

/* every pixel of a silhouette is in the rectangle of its extent */
static void case_silhouette(void)
{
	static unsigned char pixels[MAP_MAXIMUM * MAP_MAXIMUM];
	int trial, placed = 0;

	for (trial = 0; trial < 4000; trial++)
	{
		struct draw_bounds_request request;
		struct part part;
		GLint rect[4];
		int size = 128 << pick(3), index, x, y;
		BOOL whole;

		map_size = size;
		raster_state(0, 0, size, size);
		if (!pick(4))
		{
			/* (a scissor the rectangle stays in) */
			gl_state.scissor[0] = pick(size / 2);
			gl_state.scissor[1] = pick(size / 2);
			gl_state.scissor[2] = 1 + pick(size - gl_state.scissor[0]);
			gl_state.scissor[3] = 1 + pick(size - gl_state.scissor[1]);
		}
		part_random(&part);
		constants_random(pick(2), TRUE);
		part_extent(&part, &request);
		draw_bounds_command(&request);
		if (!draw_bounds_rect(rect))
			continue;
		placed++;
		CHECK(rect[0] >= gl_state.scissor[0] && rect[1] >= gl_state.scissor[1] &&
			rect[2] <= gl_state.scissor[0] + gl_state.scissor[2] && rect[3] <= gl_state.scissor[1] + gl_state.scissor[3],
			"trial %d: rectangle %d %d %d %d outside the scissor", trial, rect[0], rect[1], rect[2], rect[3]);
		memset(pixels, 0, (size_t)size * size);
		whole = TRUE;
		for (index = 0; index < part.triangle_count; index++)
			whole = triangle_fill(&part, part.triangles[index], pixels, size, size, 1) && whole;
		CHECK(whole, "trial %d: a vertex behind the eye, with a rectangle", trial);
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
			{
				CHECK(!pixels[y * size + x] || (x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3]),
					"trial %d: pixel %d,%d of the silhouette outside its rectangle %d %d %d %d", trial, x, y, rect[0], rect[1],
					rect[2], rect[3]);
			}
		}
	}
	CHECK(placed > 2000, "only %d of 4000 silhouettes placed", placed);
}

/* the rectangle of a rigid box seen orthographically is its pixels' within a few */
static void case_cuts(void)
{
	static unsigned char pixels[MAP_MAXIMUM * MAP_MAXIMUM];
	int trial, small = 0, checked = 0;

	for (trial = 0; trial < 1000; trial++)
	{
		struct draw_bounds_request request;
		struct part part;
		GLint rect[4];
		int size = 128 << pick(3), index, x, y, low[2], high[2];

		map_size = size;
		raster_state(0, 0, size, size);
		do
			part_random(&part);
		while (!part.boxes);
		/* (the first box alone, its corners its extremes) */
		part.vertex_count = 8;
		part.triangle_count = 12;
		constants_random(FALSE, TRUE);
		part_extent(&part, &request);
		draw_bounds_command(&request);
		if (!draw_bounds_rect(rect))
		{
			/* (a node beyond the registers the vertices can name) */
			CHECK(part.nodes[0][0] >= 86, "trial %d: an orthographic box of node %d not placed", trial, part.nodes[0][0]);
			continue;
		}
		memset(pixels, 0, (size_t)size * size);
		for (index = 0; index < part.triangle_count; index++)
			triangle_fill(&part, part.triangles[index], pixels, size, size, 1);
		low[0] = low[1] = size;
		high[0] = high[1] = -1;
		for (y = 0; y < size; y++)
		{
			for (x = 0; x < size; x++)
			{
				if (pixels[y * size + x])
				{
					low[0] = x < low[0] ? x : low[0];
					low[1] = y < low[1] ? y : low[1];
					high[0] = x > high[0] ? x : high[0];
					high[1] = y > high[1] ? y : high[1];
				}
			}
		}
		if (high[0] < 0 || low[0] == 0 || low[1] == 0 || high[0] == size - 1 || high[1] == size - 1)
			continue;
		checked++;
		CHECK(rect[0] >= low[0] - 5 && rect[1] >= low[1] - 5 && rect[2] <= high[0] + 6 && rect[3] <= high[1] + 6,
			"trial %d: rectangle %d %d %d %d for pixels %d %d to %d %d", trial, rect[0], rect[1], rect[2], rect[3], low[0],
			low[1], high[0], high[1]);
		if ((rect[2] - rect[0]) * (rect[3] - rect[1]) * 2 < size * size)
			small++;
	}
	CHECK(checked > 300 && small * 2 > checked, "%d small of %d", small, checked);
}

/* frames of silhouettes and their blurs, every pass against the whole pass */
static void case_blur(void)
{
	int frame, size = 0, cut_passes = 0;

	for (frame = 0; frame < 48; frame++)
	{
		int count = 1 + pick(SHADOW_LAYER_INSTANCES), index, draws;

		if (!size || !pick(8))
		{
			size = 128 << pick(3);
			maps_new(size);
		}
		taps_clamped = !pick(4);
		blur_states();
		for (index = 0; index < count; index++)
		{
			struct render_target_entry *map = &maps[0][index];

			if (pick(10))
				map_clear(map, 0, NULL, D3DCLEAR_RGB | (pick(2) ? D3DCLEAR_TARGET_A : 0),
					pick(2) ? 0 : 0xFF000000);
			check_bound(map, "a cleared map");
			for (draws = pick(4); draws; draws--)
			{
				silhouette_draw(map, pick(16) != 0, pick(16) != 0);
				check_bound(map, "a map drawn into");
			}
		}
		raster_state(0, 0, size, size);
		blur(count, size);
		for (index = 0; index < count; index++)
			cut_passes += target_bound_valid(&maps[1][index], maps[1][index].target.written);
	}
	CHECK(cut_passes > 48, "only %d blurred maps with bounds", cut_passes);
}

/* sources whose colour reaches into their edges' bands, two passes */
static void case_edges(void)
{
	int trial;

	for (trial = 0; trial < 24; trial++)
	{
		struct render_target_entry *sources[SHADOW_LAYER_INSTANCES], *destinations[SHADOW_LAYER_INSTANCES];
		int count = 1 + pick(SHADOW_LAYER_INSTANCES), size = 128 << pick(2), index, x, y;
		float reach = (float)(1 << pick(4)) * 0.5f / (float)size;

		maps_new(size);
		blur_states();
		taps_clamped = !pick(3);
		for (index = 0; index < count; index++)
		{
			struct render_target_entry *map = &maps[0][index];
			int edge = trial % 6 == 5 ? size / 2 : 1 + pick(12);

			map->bound[0] = pick(size / 2);
			map->bound[1] = pick(size / 2);
			map->bound[2] = map->bound[0] + pick(size / 2);
			map->bound[3] = map->bound[1] + pick(size / 2);
			map->bound_edge = edge;
			map->bound_written = map->target.written;
			for (y = 0; y < size; y++)
			{
				for (x = 0; x < size; x++)
				{
					BOOL inside = (x >= map->bound[0] && x < map->bound[2] && y >= map->bound[1] && y < map->bound[3]) ||
						x < edge || y < edge || x >= size - edge || y >= size - edge;

					map->pixels[y * size + x] = (unsigned char)(inside && pick(3) ? 1 + pick(255) : 0);
				}
			}
			if (pick(2))
			{
				/* (a destination with a bound of its own) */
				silhouette_bounded(&maps[1][index]);
			}
			sources[index] = &maps[0][index];
			destinations[index] = &maps[1][index];
		}
		raster_state(0, 0, size, size);
		pass(sources, destinations, count, reach);
		pass(destinations, sources, count, reach);
	}
}

/* a pass that cannot be cut is drawn as it is, and leaves no bounds */
static void case_states(void)
{
	int state;

	for (state = 0; state < 15; state++)
	{
		struct render_target_entry *sources[2], *destinations[2];
		float reach = 1.0f / 256.0f;
		int index;

		maps_new(256);
		blur_states();
		taps_clamped = FALSE;
		for (index = 0; index < 2; index++)
		{
			silhouette_bounded(&maps[0][index]);
			map_clear(&maps[1][index], 0, NULL, D3DCLEAR_RGB, 0);
			sources[index] = &maps[0][index];
			destinations[index] = &maps[1][index];
		}
		raster_state(0, 0, 256, 256);
		switch (state)
		{
		case 0: break; /* (cut) */
		case 1: render_state[D3DRS_ALPHABLENDENABLE] = TRUE; break;
		case 2: render_state[D3DRS_ALPHATESTENABLE] = TRUE; break;
		case 3: render_state[D3DRS_FOGENABLE] = TRUE; break; /* (cut: the blur's combiners do not read the fog) */
		case 4: render_state[D3DRS_COLORWRITEENABLE] |= D3DCOLORWRITEENABLE_ALPHA; break;
		case 5: render_state[D3DRS_COLORWRITEENABLE] &= ~(DWORD)D3DCOLORWRITEENABLE_BLUE; break;
		case 6: case 7: case 8: case 9: texture_state[state - 6][D3DTSS_ALPHAKILL] = D3DTALPHAKILL_ENABLE; break;
		case 10: raster_state(0, 0, 255, 256); break;
		case 11: raster_state(1, 1, 255, 255); break;
		case 12: gl_state.scissor_test = gl_scissor_enabled = FALSE; break;
		case 13: reach = 0.0f; break;
		case 14: reach = 1.0f; break;
		}
		pass(sources, destinations, 2, reach);
		if (state && state != 3)
		{
			CHECK(!scissor_calls && draw_calls == 1, "state %d: the pass was cut (%d scissors, %d draws)", state,
				scissor_calls, draw_calls);
			for (index = 0; index < 2; index++)
				CHECK(!target_bound_valid(destinations[index], destinations[index]->target.written),
					"state %d: an uncut pass left a bound", state);
		}
		else
			CHECK(scissor_calls > 0, "the pass was not cut");
	}
}

/* what leaves a map without a bound, or keeps it, each followed by a pass */
static void case_invalidation(void)
{
	int scenario;

	for (scenario = 0; scenario < 9; scenario++)
	{
		struct render_target_entry *sources[1], *destinations[1];
		struct render_target_entry *map = &maps[0][0];
		D3DRECT rectangle;
		BOOL valid = TRUE;

		maps_new(256);
		blur_states();
		taps_clamped = FALSE;
		silhouette_bounded(map);
		rectangle.x1 = 10; rectangle.y1 = 20; rectangle.x2 = 200; rectangle.y2 = 230;
		switch (scenario)
		{
		case 0:
			silhouette_draw(map, FALSE, TRUE);
			valid = FALSE;
			break;
		case 1:
			silhouette_draw(map, TRUE, FALSE);
			valid = FALSE;
			break;
		case 2:
			/* (a draw deferred by instancing, drawn with another's extent) */
			instancing_drawing = TRUE;
			silhouette_draw(map, TRUE, TRUE);
			instancing_drawing = FALSE;
			valid = FALSE;
			break;
		case 3:
			map_clear(map, 1, &rectangle, D3DCLEAR_RGB, 0);
			break;
		case 4:
			map_clear(map, 1, &rectangle, D3DCLEAR_RGB, 0x00FFFFFF);
			valid = FALSE;
			break;
		case 5:
			map_clear(map, 0, NULL, D3DCLEAR_TARGET_G, 0);
			break;
		case 6:
			/* (a map without a bound: a partial black clear, then a silhouette) */
			map_new(map, 256);
			map_clear(map, 1, &rectangle, D3DCLEAR_RGB, 0);
			silhouette_draw(map, TRUE, TRUE);
			valid = FALSE;
			break;
		case 7:
			raster_state(0, 0, 128, 256);
			map_clear(map, 0, NULL, D3DCLEAR_RGB, 0);
			raster_state(0, 0, 256, 256);
			break;
		case 8:
			map_new(map, 256);
			raster_state(0, 0, 128, 256);
			map_clear(map, 0, NULL, D3DCLEAR_RGB, 0);
			raster_state(0, 0, 256, 256);
			silhouette_draw(map, TRUE, TRUE);
			valid = FALSE;
			break;
		}
		CHECK(!target_bound_valid(map, map->target.written) == !valid, "scenario %d: the bound is%s valid", scenario,
			valid ? " not" : "");
		check_bound(map, "the map");
		sources[0] = map;
		destinations[0] = &maps[1][0];
		raster_state(0, 0, 256, 256);
		pass(sources, destinations, 1, 1.0f / 256.0f);
	}
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "silhouette"))
		case_silhouette();
	else if (!strcmp(name, "cuts"))
		case_cuts();
	else if (!strcmp(name, "blur"))
		case_blur();
	else if (!strcmp(name, "edges"))
		case_edges();
	else if (!strcmp(name, "states"))
		case_states();
	else if (!strcmp(name, "invalidation"))
		case_invalidation();
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	printf("%s: ok\n", name);
	return 0;
}
