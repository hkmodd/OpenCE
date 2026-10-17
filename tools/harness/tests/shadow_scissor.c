/*
SHADOW_SCISSOR.C (test)

The scissor of a shadow's projection (port/linux/game/shadow_projection.c and
d3d8_gl.c's scissor_volume_cut) against a model of what the projection
draws: its vertex program's texture coordinates, its two textures sampled
bilinearly (the map to a border of zero, the fade clamped), its pixel
shader's sum in single precision, and the position of each point on the
screen as the vertex programs' epilogue and OpenGL's viewport place it.
Every point of the world whose colour the projection changes must be in the
scissor. test_shadow_scissor.py takes the code (under_test.inc) from the
sources.
*/

#include "harness.h"
#include <math.h>

/* the graphics layer: the members the cut reads */
typedef int BOOL;
typedef int GLint;
typedef unsigned int GLenum;
typedef unsigned long DWORD;
enum { GL_CLAMP_TO_EDGE = 0x812F, GL_CLAMP_TO_BORDER = 0x812D, GL_REPEAT = 0x2901 };
enum { D3DTADDRESS_WRAP = 1, D3DTADDRESS_MIRROR, D3DTADDRESS_CLAMP, D3DTADDRESS_BORDER, D3DTADDRESS_CLAMPTOEDGE };
enum { D3DTSS_ADDRESSU, D3DTSS_ADDRESSV, D3DTSS_BORDERCOLOR, D3DTSS_MAX };
#define D3DSCM_NORESERVEDCONSTANTS 0x2
#define XGPU_VERTEX_CONSTANT_BIAS 96
#define UI_OFFSET 0
static struct
{
	float constants[192][4];
	float viewport_scale[4], viewport_offset[4];
	DWORD shader_constant_mode;
} device;
static DWORD D3D__TextureState[4][D3DTSS_MAX];
static struct { GLint viewport[4]; } gl_state;
static BOOL border_clamp = TRUE; /* (FALSE: ES without GL_EXT_texture_border_clamp) */
static GLenum address_mode(DWORD mode)
{
	return mode == D3DTADDRESS_BORDER ? (border_clamp ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE) :
		mode == D3DTADDRESS_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT;
}
/* (without the render thread: the renderer reads the game's states) */
static DWORD (*texture_state)[D3DTSS_MAX] = D3D__TextureState;
static BOOL queued(void)
{
	return FALSE;
}
static void *render_thread_record(void (*command)(void *), unsigned long size)
{
	(void)command;
	(void)size;
	return NULL;
}

/* the game: the shadow's matrix, the globals' fade and the debug option */
typedef union { real n[2]; struct { real x, y; }; } real_point2d;
typedef struct { real scale; real_vector3d forward, left, up; real_point3d position; } real_matrix4x3;
struct bitmap_data { short width, height, type, format; unsigned short flags; short mipmap_count; void *base_address; };
enum { FADE = 16 };
static unsigned char fade_texels[FADE][FADE]; /* [y][x], an intensity texture */
static struct bitmap_data fade = { FADE, FADE, 0, 2, 0x89, 0, fade_texels };
static struct { struct { long index; } linear_corner_fade; } rasterizer_data = { { 7 } };
static struct { boolean shadow_debug_enabled; } rasterizer_debug_options;
#define global_rasterizer_data (&rasterizer_data)
static struct bitmap_data *bitmap_group_try_and_get_bitmap(long group, short index)
{
	return group == 7 && index == 0 ? &fade : NULL;
}
/* (bitmaps.c's, for an intensity texture of one level: the texel under a point) */
static unsigned long bitmap_2d_get_pixel(struct bitmap_data *bitmap, real_point2d const *point, real lod)
{
	int x = (int)floorf(bitmap->width * point->x - 0.5f + 0.5f) & (bitmap->width - 1);
	int y = (int)floorf(bitmap->height * point->y - 0.5f + 0.5f) & (bitmap->height - 1);
	unsigned long texel = ((unsigned char *)bitmap->base_address)[y * bitmap->width + x];

	(void)lod;
	return texel << 24 | texel << 16 | texel << 8 | texel;
}

#include "under_test.inc"

/* ---------- the model of the projection */

enum { MAP = 128 };
static unsigned char map_texels[MAP][MAP];
static float shadow_color;

static unsigned long seed = 12345;
static float uniform(float low, float high)
{
	seed = seed * 1103515245UL + 12345UL;
	return low + (high - low) * (float)((seed >> 8) & 0xFFFFFF) / (float)0xFFFFFF;
}

/* bilinear sample of a size x size texture: clamped to its edge, or a border
of zero; its weights in 256ths, as the hardware's are, so that they sum to
one exactly */
static float sample(unsigned char const *texels, int size, float u, float v, boolean border)
{
	float x = u * size - 0.5f, y = v * size - 0.5f;
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	long a = (long)((x - x0) * 256.0f), b = (long)((y - y0) * 256.0f), sum = 0;
	int i, j;

	for (j = 0; j < 2; j++)
		for (i = 0; i < 2; i++)
		{
			int tx = x0 + i, ty = y0 + j;
			long weight = (i ? a : 256 - a) * (j ? b : 256 - b);

			if (tx < 0 || tx >= size || ty < 0 || ty >= size)
			{
				if (border)
					continue;
				tx = tx < 0 ? 0 : tx >= size ? size - 1 : tx;
				ty = ty < 0 ? 0 : ty >= size ? size - 1 : ty;
			}
			sum += weight * texels[ty * size + tx];
		}
	return (float)sum / (65536.0f * 255.0f);
}

static float dot(real_vector3d const *v, float const *d)
{
	return v->i * d[0] + v->j * d[1] + v->k * d[2];
}

/* the colour the projection's blend takes away at a point: 0 keeps the pixel */
static float projection_color(real_matrix4x3 const *m, real radius, float const *point)
{
	float d[3], u, v, s, t, t0, t1, x, y;

	d[0] = point[0] - m->position.x;
	d[1] = point[1] - m->position.y;
	d[2] = point[2] - m->position.z;
	/* (the vertex constants _rasterizer_environment_shadow_draw sets) */
	u = (1.0f + dot(&m->forward, d) / radius) * 0.5f;
	v = (1.0f - dot(&m->left, d) / radius) * 0.5f;
	s = dot(&m->up, d) / (radius * 4.0f);
	t = -dot(&m->up, d) / (radius * 0.5f);
	t0 = sample(map_texels[0], MAP, u, v, TRUE);
	t1 = sample(fade_texels[0], FADE, s, t, FALSE);
	x = 1.0f - t1;
	y = t0 * (1.0f - shadow_color);
	return 1.0f - ((1.0f - x) + x * (1.0f - y));
}

/* the pixel a point lands on, rows from the top: FALSE outside the view */
static boolean project(float const *p, int *px, int *py)
{
	float const (*rows)[4] = (float const (*)[4])device.constants[XGPU_VERTEX_CONSTANT_BIAS - 96];
	float const *c38 = device.constants[XGPU_VERTEX_CONSTANT_BIAS - 38];
	float const *c37 = device.constants[XGPU_VERTEX_CONSTANT_BIAS - 37];
	float x = rows[0][0] * p[0] + rows[0][1] * p[1] + rows[0][2] * p[2] + rows[0][3];
	float y = rows[1][0] * p[0] + rows[1][1] * p[1] + rows[1][2] * p[2] + rows[1][3];
	float w = rows[3][0] * p[0] + rows[3][1] * p[1] + rows[3][2] * p[2] + rows[3][3];
	float nx, ny;

	if (!(w > 0.0f))
		return FALSE;
	/* nv2a_vsh.c's epilogue, then OpenGL's divide */
	nx = (x * c38[0] + (c37[0] + 0.5f - device.viewport_offset[0]) * w) / device.viewport_scale[0] / w;
	ny = (y * c38[1] + (c37[1] + 0.5f - device.viewport_offset[1]) * w) / device.viewport_scale[1] / w;
	if (nx < -1.0f || nx > 1.0f || ny < -1.0f || ny > 1.0f)
		return FALSE;
	/* the viewport transform, upper left (glClipControl, or the epilogue's flip on ES) */
	*px = (int)floorf(gl_state.viewport[0] + (nx + 1.0f) * 0.5f * gl_state.viewport[2]);
	*py = (int)floorf(gl_state.viewport[1] + (1.0f - ny) * 0.5f * gl_state.viewport[3]);
	return TRUE;
}

static void normalize(real_vector3d *v)
{
	float length = sqrtf(v->i * v->i + v->j * v->j + v->k * v->k);

	v->i /= length; v->j /= length; v->k /= length;
}

static real_vector3d cross(real_vector3d a, real_vector3d b)
{
	real_vector3d c;

	c.i = a.j * b.k - a.k * b.j;
	c.j = a.k * b.i - a.i * b.k;
	c.k = a.i * b.j - a.j * b.i;
	return c;
}

/* a shadow: a random frame, its up away from the light, around the origin */
static void random_shadow(real_matrix4x3 *m, real *radius)
{
	real_vector3d any;

	m->up.i = uniform(-1.0f, 1.0f); m->up.j = uniform(-1.0f, 1.0f); m->up.k = uniform(0.2f, 1.0f);
	normalize(&m->up);
	any.i = uniform(-1.0f, 1.0f); any.j = uniform(-1.0f, 1.0f); any.k = uniform(-1.0f, 1.0f);
	m->forward = cross(m->up, any);
	normalize(&m->forward);
	m->left = cross(m->up, m->forward);
	m->position.x = uniform(-2.0f, 2.0f); m->position.y = uniform(-2.0f, 2.0f); m->position.z = uniform(-1.0f, 1.0f);
	m->scale = 1.0f;
	*radius = uniform(0.2f, 1.5f);
}

/* the camera's constants as the game and the device set them: a 960x540
viewport drawn at 2560x1440, a perspective view from the eye toward a point */
static void set_camera(float const *eye, float const *target)
{
	real_vector3d forward, right, up = { 0.0f, 0.0f, 1.0f };
	float (*rows)[4] = (float (*)[4])device.constants[XGPU_VERTEX_CONSTANT_BIAS - 96];
	float focal = 1.0f / tanf(0.6f), aspect = 960.0f / 540.0f;
	int column;

	forward.i = target[0] - eye[0]; forward.j = target[1] - eye[1]; forward.k = target[2] - eye[2];
	normalize(&forward);
	right = cross(forward, up);
	normalize(&right);
	up = cross(right, forward);
	memset(rows, 0, sizeof(float) * 16);
	for (column = 0; column < 3; column++)
	{
		float r = column == 0 ? right.i : column == 1 ? right.j : right.k;
		float u = column == 0 ? up.i : column == 1 ? up.j : up.k;
		float f = column == 0 ? forward.i : column == 1 ? forward.j : forward.k;

		rows[0][column] = r * focal / aspect;
		rows[1][column] = u * focal;
		rows[2][column] = f;
		rows[3][column] = f;
	}
	rows[0][3] = -(rows[0][0] * eye[0] + rows[0][1] * eye[1] + rows[0][2] * eye[2]);
	rows[1][3] = -(rows[1][0] * eye[0] + rows[1][1] * eye[1] + rows[1][2] * eye[2]);
	rows[2][3] = -(rows[2][0] * eye[0] + rows[2][1] * eye[1] + rows[2][2] * eye[2]);
	rows[3][3] = rows[2][3];
	device.constants[XGPU_VERTEX_CONSTANT_BIAS - 38][0] = 480.0f;
	device.constants[XGPU_VERTEX_CONSTANT_BIAS - 38][1] = -270.0f;
	device.constants[XGPU_VERTEX_CONSTANT_BIAS - 37][0] = 480.0f - 0.5f;
	device.constants[XGPU_VERTEX_CONSTANT_BIAS - 37][1] = 270.0f - 0.5f;
	device.viewport_scale[0] = 480.0f;
	device.viewport_scale[1] = -270.0f;
	device.viewport_offset[0] = 480.0f;
	device.viewport_offset[1] = 270.0f;
	gl_state.viewport[0] = 0;
	gl_state.viewport[1] = 0;
	gl_state.viewport[2] = 2560;
	gl_state.viewport[3] = 1440;
}

static void reset(void)
{
	int x, y;

	for (y = 0; y < MAP; y++)
		for (x = 0; x < MAP; x++)
			map_texels[y][x] = (unsigned char)uniform(1.0f, 255.9f);
	for (y = 0; y < FADE; y++)
		for (x = 0; x < FADE; x++)
			fade_texels[y][x] = (unsigned char)uniform(0.0f, 255.9f);
	fade_texels[0][FADE - 1] = fade_texels[FADE - 1][0] = 255;
	fade.format = 2;
	fade.mipmap_count = 0;
	rasterizer_debug_options.shadow_debug_enabled = FALSE;
	device.shader_constant_mode = 0;
	border_clamp = TRUE;
	D3D__TextureState[0][D3DTSS_ADDRESSU] = D3D__TextureState[0][D3DTSS_ADDRESSV] = D3DTADDRESS_BORDER;
	D3D__TextureState[0][D3DTSS_BORDERCOLOR] = 0;
	D3D__TextureState[1][D3DTSS_ADDRESSU] = D3D__TextureState[1][D3DTSS_ADDRESSV] = D3DTADDRESS_CLAMP;
}

/* the scissor apply_raster_state gives the projection's draw */
static void draw_scissor(GLint *scissor)
{
	memcpy(scissor, gl_state.viewport, sizeof(GLint) * 4);
	if (scissor_volume.set)
		scissor_volume_cut(scissor);
}

static boolean whole(GLint const *scissor)
{
	return !memcmp(scissor, gl_state.viewport, sizeof(GLint) * 4);
}

/* a camera that sees the shadow from 2 to 40 units away, now and then from inside its box */
static void random_camera(real_matrix4x3 const *m, real radius)
{
	float eye[3], target[3], distance = uniform(0.0f, 1.0f) < 0.1f ? uniform(0.0f, radius) : uniform(2.0f, 40.0f);
	float i = uniform(-1.0f, 1.0f), j = uniform(-1.0f, 1.0f), k = uniform(-0.2f, 1.0f);
	float length = sqrtf(i * i + j * j + k * k);

	eye[0] = m->position.x + i / length * distance;
	eye[1] = m->position.y + j / length * distance;
	eye[2] = m->position.z + k / length * distance;
	target[0] = m->position.x + uniform(-3.0f, 3.0f);
	target[1] = m->position.y + uniform(-3.0f, 3.0f);
	target[2] = m->position.z + uniform(-1.0f, 1.0f);
	set_camera(eye, target);
}

/* a point of the shadow's frame, often just past one of its box's faces */
static void random_point(real_matrix4x3 const *m, real radius, float *point)
{
	float a = uniform(-1.3f, 1.3f) * radius, b = uniform(-1.3f, 1.3f) * radius, h = uniform(-0.9f, 4.6f) * radius;
	float edge = uniform(0.0f, 1.0f);

	if (edge < 0.15f)
		a = (a < 0.0f ? -1.0f : 1.0f) * radius * uniform(1.0f, 1.02f);
	else if (edge < 0.3f)
		b = (b < 0.0f ? -1.0f : 1.0f) * radius * uniform(1.0f, 1.02f);
	else if (edge < 0.4f)
		h = radius * uniform(-0.52f, -0.48f);
	else if (edge < 0.5f)
		h = radius * uniform(3.9f, 4.1f);
	point[0] = m->position.x + m->forward.i * a + m->left.i * b + m->up.i * h;
	point[1] = m->position.y + m->forward.j * a + m->left.j * b + m->up.j * h;
	point[2] = m->position.z + m->forward.k * a + m->left.k * b + m->up.k * h;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	real_matrix4x3 m;
	real radius;
	GLint scissor[4];

	CASE("exact")
	{
		long trial, shaded = 0, cut = 0;

		for (trial = 0; trial < 400; trial++)
		{
			long n;

			reset();
			random_shadow(&m, &radius);
			random_camera(&m, radius);
			shadow_color = uniform(0.0f, 0.9f);
			shadow_projection_scissor_begin(&m, radius);
			draw_scissor(scissor);
			cut += !whole(scissor);
			for (n = 0; n < 20000; n++)
			{
				float point[3];
				int px, py;

				random_point(&m, radius, point);
				if (projection_color(&m, radius, point) != 0.0f && project(point, &px, &py))
				{
					shaded++;
					CHECK(px >= scissor[0] && px < scissor[0] + scissor[2] && py >= scissor[1] && py < scissor[1] + scissor[3],
						"trial %ld: a changed pixel (%d, %d) outside the scissor %d %d %d %d", trial, px, py,
						scissor[0], scissor[1], scissor[2], scissor[3]);
				}
			}
			shadow_projection_scissor_end();
			CHECK(!scissor_volume.set, "the volume outlives its draw");
		}
		CHECK(shaded > 100000 && cut > 200, "%ld changed points, %ld draws cut", shaded, cut);
		return 0;
	}
	CASE("cuts")
	{
		float eye[3] = { 0.0f, -30.0f, 5.0f }, target[3] = { 0.0f, 0.0f, 0.0f };

		reset();
		random_shadow(&m, &radius);
		m.position.x = m.position.y = m.position.z = 0.0f;
		radius = 0.5f;
		set_camera(eye, target);
		shadow_projection_scissor_begin(&m, radius);
		draw_scissor(scissor);
		shadow_projection_scissor_end();
		CHECK((double)scissor[2] * scissor[3] < 0.02 * 2560.0 * 1440.0, "%d x %d", scissor[2], scissor[3]);
		return 0;
	}
	CASE("inside")
	{
		float eye[3], target[3] = { 10.0f, 0.0f, 0.0f };

		reset();
		random_shadow(&m, &radius);
		eye[0] = m.position.x; eye[1] = m.position.y; eye[2] = m.position.z;
		set_camera(eye, target);
		shadow_projection_scissor_begin(&m, radius);
		draw_scissor(scissor);
		shadow_projection_scissor_end();
		CHECK(whole(scissor), "cut with the eye inside the box");
		return 0;
	}
	CASE("fade")
	{
		float eye[3] = { 0.0f, -30.0f, 5.0f }, target[3] = { 0.0f, 0.0f, 0.0f };
		int variant;

		for (variant = 0; variant < 4; variant++)
		{
			reset();
			random_shadow(&m, &radius);
			set_camera(eye, target);
			if (variant == 0)
				fade_texels[0][FADE - 1] = 254;
			else if (variant == 1)
				fade_texels[FADE - 1][0] = 0;
			else if (variant == 2)
				fade.mipmap_count = 4;
			else
				rasterizer_debug_options.shadow_debug_enabled = TRUE;
			shadow_projection_scissor_begin(&m, radius);
			draw_scissor(scissor);
			shadow_projection_scissor_end();
			CHECK(whole(scissor), "cut with variant %d of the fade", variant);
		}
		return 0;
	}
	CASE("states")
	{
		float eye[3] = { 0.0f, -30.0f, 5.0f }, target[3] = { 0.0f, 0.0f, 0.0f };
		int variant;

		for (variant = 0; variant < 5; variant++)
		{
			reset();
			random_shadow(&m, &radius);
			set_camera(eye, target);
			if (variant == 0)
				D3D__TextureState[0][D3DTSS_ADDRESSU] = D3DTADDRESS_CLAMP;
			else if (variant == 1)
				D3D__TextureState[0][D3DTSS_BORDERCOLOR] = 0x46000000;
			else if (variant == 2)
				border_clamp = FALSE;
			else if (variant == 3)
				D3D__TextureState[1][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
			else
				device.shader_constant_mode = D3DSCM_NORESERVEDCONSTANTS;
			shadow_projection_scissor_begin(&m, radius);
			draw_scissor(scissor);
			shadow_projection_scissor_end();
			CHECK(whole(scissor), "cut with variant %d of the draw's states", variant);
		}
		return 0;
	}
	fprintf(stderr, "no case '%s'\n", case_name);
	return 2;
}
