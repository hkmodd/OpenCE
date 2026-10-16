#include "harness.h"

/* A string's batched characters (rasterizer_xbox_text.c), drawn by
rasterizer_text_flush through immediate mode (d3d8_gl.c): the draw must hold
what the three calls a vertex the characters were drawn with
(SetVertexDataColor(9), SetVertexData2f(4) and SetVertexData2f(0)) gave,
and leave the registers as they did. */
typedef int BOOL;
typedef int INT;
typedef float FLOAT;
typedef unsigned long D3DCOLOR;
typedef int D3DPRIMITIVETYPE;
#define WINAPI
#define D3DVSDE_VERTEX (-1) /* xdk_d3d8.h */
#define D3DPT_QUADLIST 8
#define DEVICE_CHANGED_ATTRIBUTES 1

#include "config.inc"

struct gl_device
{
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	BOOL immediate_active;
	D3DPRIMITIVETYPE immediate_type;
	unsigned long immediate_written;
	unsigned long immediate_registers;
	float *immediate_vertices;
	unsigned long immediate_count;
	unsigned long immediate_capacity;
};
static struct gl_device devices[2];
static struct gl_device *game = &devices[0];
unsigned long device_changed;

/* a draw as End found it */
struct drawn
{
	int draws;
	D3DPRIMITIVETYPE type;
	unsigned long written, registers, count;
	float vertices[1024 * XGPU_VERTEX_ATTRIBUTE_COUNT * 4];
};
static struct drawn drawn[2];

static void *global_d3d_device;
static int errors;

static int end_draw(void)
{
	struct drawn *to = &drawn[game - devices];

	CHECK(game->immediate_count <= 1024, "%lu vertices drawn", game->immediate_count);
	to->draws++;
	to->type = game->immediate_type;
	to->written = game->immediate_written;
	to->registers = game->immediate_registers;
	to->count = game->immediate_count;
	memcpy(to->vertices, game->immediate_vertices, game->immediate_count * game->immediate_registers * 4 * sizeof(float));
	game->immediate_active = FALSE;
	return 0;
}

static void error(int level, char const *message)
{
	(void)level;
	(void)message;
	errors++;
}

#define IDirect3DDevice8_Begin(device, type) D3DDevice_Begin(type)
#define IDirect3DDevice8_SetVertexDataColor(device, reg, color) D3DDevice_SetVertexDataColor(reg, color)
#define IDirect3DDevice8_SetVertexData2f(device, reg, a, b) D3DDevice_SetVertexData2f(reg, a, b)
#define IDirect3DDevice8_End(device) end_draw()

#include "under_test.inc"

/* the characters drawn as they were, a call a register */
static void reference_flush(struct rasterizer_text_vertex const *vertices, short vertex_count)
{
	short vertex_index;

	if (!vertex_count)
		return;
	D3DDevice_Begin(D3DPT_QUADLIST);
	for (vertex_index = 0; vertex_index < vertex_count; vertex_index++)
	{
		D3DDevice_SetVertexDataColor(9, vertices[vertex_index].color);
		D3DDevice_SetVertexData2f(4, vertices[vertex_index].u, vertices[vertex_index].v);
		D3DDevice_SetVertexData2f(0, vertices[vertex_index].x, vertices[vertex_index].y);
	}
	end_draw();
}

static unsigned long random_state = 12345;

static unsigned long random_next(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return random_state >> 8;
}

static real random_real(void)
{
	return ((real)(long)(random_next() & 0xFFFFF) - 0x80000) / 1024.0f;
}

/* characters characters into the batch, their colours covering every value
of each byte over a full batch; then both draw them */
static void flush(short characters)
{
	static struct rasterizer_text_vertex vertices[TEXT_BATCH_MAXIMUM_CHARACTERS * 4];
	static unsigned long serial;
	short vertex_index;

	CHECK(characters <= TEXT_BATCH_MAXIMUM_CHARACTERS, "%d characters", characters);
	for (vertex_index = 0; vertex_index < characters * 4; vertex_index++, serial++)
	{
		vertices[vertex_index].x = random_real();
		vertices[vertex_index].y = random_real();
		vertices[vertex_index].u = random_real() / 512.0f;
		vertices[vertex_index].v = random_real() / 512.0f;
		vertices[vertex_index].color = ((serial & 0xFF) << 24) | (((serial + 85) & 0xFF) << 16) |
			(((serial * 3 + 170) & 0xFF) << 8) | ((serial * 7) & 0xFF);
	}
	memcpy(text_batch_vertices, vertices, characters * 4 * sizeof(*vertices));
	text_batch_character_count = characters;
	game = &devices[0];
	rasterizer_text_flush();
	CHECK(text_batch_character_count == 0, "%d characters left in the batch", text_batch_character_count);
	game = &devices[1];
	reference_flush(vertices, characters * 4);
}

static void check(void)
{
	CHECK(drawn[0].draws == drawn[1].draws, "%d draws, %d expected", drawn[0].draws, drawn[1].draws);
	CHECK(drawn[0].type == drawn[1].type, "primitive %d, %d expected", drawn[0].type, drawn[1].type);
	CHECK(drawn[0].written == drawn[1].written, "registers 0x%lx drawn, 0x%lx expected", drawn[0].written,
		drawn[1].written);
	CHECK(drawn[0].registers == drawn[1].registers, "%lu registers, %lu expected", drawn[0].registers,
		drawn[1].registers);
	CHECK(drawn[0].count == drawn[1].count, "%lu vertices, %lu expected", drawn[0].count, drawn[1].count);
	CHECK(!memcmp(drawn[0].vertices, drawn[1].vertices, drawn[1].count * drawn[1].registers * 4 * sizeof(float)),
		"the vertices drawn differ");
	CHECK(!memcmp(devices[0].attributes, devices[1].attributes, sizeof(devices[0].attributes)),
		"the registers do not hold what the calls left");
	CHECK(errors == 0, "%d errors", errors);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	int device, reg;

	/* the registers' values before the string */
	for (device = 0; device < 2; device++)
	{
		for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
		{
			devices[device].attributes[reg][0] = -1.0f - reg;
			devices[device].attributes[reg][1] = 0.5f * reg;
			devices[device].attributes[reg][2] = 2.0f + reg;
			devices[device].attributes[reg][3] = 0.25f;
		}
	}
	CASE("full")
	{
		/* a full batch: every value of each colour byte */
		flush(TEXT_BATCH_MAXIMUM_CHARACTERS);
		check();
		return 0;
	}
	CASE("strings")
	{
		/* strings of 1 to 40 characters, one after another */
		short characters;

		for (characters = 1; characters <= 40; characters++)
		{
			flush(characters);
			check();
		}
		return 0;
	}
	CASE("empty")
	{
		/* an empty batch draws nothing and leaves the registers */
		flush(0);
		check();
		CHECK(drawn[0].draws == 0, "%d draws", drawn[0].draws);
		flush(3);
		flush(0);
		check();
		return 0;
	}
	return 2;
}
