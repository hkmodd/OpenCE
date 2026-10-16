#include "harness.h"

/* Immediate mode's vertices (d3d8_gl.c): SetVertexData writes an input
register, writing register 0 completes a vertex, which holds only the
registers written since Begin, in order. Each vertex must read what a
vertex of all sixteen registers would: a register first written after some
vertices holds, in them, the value it had before. */
typedef int BOOL;
typedef int INT;
typedef int D3DPRIMITIVETYPE;
#define WINAPI
#define D3DVSDE_VERTEX (-1) /* xdk_d3d8.h */
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
static struct gl_device game_device;
static struct gl_device *game = &game_device;
unsigned long device_changed;

#include "under_test.inc"

/* the reference: every vertex a copy of all sixteen registers */
#define MAXIMUM_VERTICES 2048
static float reference_attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
static float reference_vertices[MAXIMUM_VERTICES][XGPU_VERTEX_ATTRIBUTE_COUNT][4];
static unsigned long reference_count;
/* the registers written since Begin */
static unsigned long reference_written;
static float next_value = 1.0f;

static void write_register(int reg)
{
	int target = reg == D3DVSDE_VERTEX ? 0 : reg;
	float a = next_value, b = next_value + 0.25f, c = next_value + 0.5f, d = next_value + 0.75f;

	next_value += 1.0f;
	set_attribute(reg, a, b, c, d);
	/* (out of range: ignored) */
	if (target >= XGPU_VERTEX_ATTRIBUTE_COUNT)
		return;
	reference_attributes[target][0] = a;
	reference_attributes[target][1] = b;
	reference_attributes[target][2] = c;
	reference_attributes[target][3] = d;
	reference_written |= 1UL << target;
	if (target == 0)
	{
		CHECK(reference_count < MAXIMUM_VERTICES, "too many vertices for the reference");
		memcpy(reference_vertices[reference_count++], reference_attributes, sizeof(reference_attributes));
	}
}

/* count vertices of registers (with register 0) at once, each the
registers' four floats, lowest first (halo_immediate_vertices); the
reference writes them a register at a time */
static void write_vertices(unsigned long registers, unsigned long count)
{
	static float data[MAXIMUM_VERTICES * XGPU_VERTEX_ATTRIBUTE_COUNT * 4];
	unsigned long vertex, i = 0;
	int reg, component;

	CHECK((registers & 1) && reference_count + count <= MAXIMUM_VERTICES, "vertices the reference can make");
	for (vertex = 0; vertex < count; vertex++)
	{
		for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
		{
			if (!(registers & (1UL << reg)))
				continue;
			for (component = 0; component < 4; component++)
				reference_attributes[reg][component] = data[i++] = next_value + 0.25f * component;
			next_value += 1.0f;
		}
		memcpy(reference_vertices[reference_count++], reference_attributes, sizeof(reference_attributes));
	}
	reference_written |= registers;
	halo_immediate_vertices(registers, data, count);
}

static void check_registers(void)
{
	CHECK(!memcmp(game->attributes, reference_attributes, sizeof(reference_attributes)),
		"the registers do not hold the last values written");
}

static void begin(void)
{
	D3DDevice_Begin(4);
	reference_count = 0;
	reference_written = 0;
}

/* the vertices the game's side holds, against the reference */
static void check_vertices(void)
{
	unsigned long vertex, registers = 0;
	int reg;

	for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
		registers += (game->immediate_written >> reg) & 1;
	CHECK(game->immediate_registers == registers, "%lu registers counted, %lu written", game->immediate_registers,
		registers);
	CHECK(game->immediate_written == reference_written, "registers 0x%lx held, 0x%lx written", game->immediate_written,
		reference_written);
	CHECK(game->immediate_count == reference_count, "%lu vertices, %lu made", game->immediate_count, reference_count);
	for (vertex = 0; vertex < reference_count; vertex++)
	{
		float const *held = game->immediate_vertices + vertex * registers * 4;

		for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
		{
			if (!(game->immediate_written & (1UL << reg)))
				continue;
			CHECK(!memcmp(held, reference_vertices[vertex][reg], 4 * sizeof(float)),
				"vertex %lu register %d holds %g, not %g", vertex, reg, held[0], reference_vertices[vertex][reg][0]);
			held += 4;
		}
	}
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	int reg;

	/* the registers' values before any Begin */
	for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		game->attributes[reg][0] = reference_attributes[reg][0] = -1.0f - reg;
		game->attributes[reg][3] = reference_attributes[reg][3] = 1.0f;
	}
	CASE("decal")
	{
		/* a decal's quad: colour, texture coordinates, position */
		int vertex;

		begin();
		for (vertex = 0; vertex < 4; vertex++)
		{
			write_register(9);
			write_register(2);
			write_register(D3DVSDE_VERTEX);
		}
		CHECK(game->immediate_registers == 3, "%lu registers held", game->immediate_registers);
		check_vertices();
		return 0;
	}
	CASE("late-register")
	{
		/* a register first written after two vertices, below one written
		before, and above */
		begin();
		write_register(9);
		write_register(0);
		write_register(0);
		write_register(3);
		write_register(12);
		write_register(D3DVSDE_VERTEX);
		write_register(1);
		write_register(0);
		check_vertices();
		return 0;
	}
	CASE("many")
	{
		/* past the first room for vertices, then a register more */
		int vertex;

		begin();
		for (vertex = 0; vertex < 1000; vertex++)
		{
			write_register(5);
			write_register(D3DVSDE_VERTEX);
		}
		write_register(7);
		write_register(D3DVSDE_VERTEX);
		check_vertices();
		return 0;
	}
	CASE("random")
	{
		unsigned long seed = 12345;
		int primitive;

		for (primitive = 0; primitive < 400; primitive++)
		{
			int writes, write;

			begin();
			seed = seed * 1103515245u + 12345u;
			writes = (int)((seed >> 16) % 1200);
			for (write = 0; write < writes; write++)
			{
				int roll;

				seed = seed * 1103515245u + 12345u;
				roll = (int)((seed >> 16) % 21);
				/* (every register, and two out of range) */
				write_register(roll < 3 ? D3DVSDE_VERTEX : roll - 3);
			}
			check_vertices();
		}
		return 0;
	}
	CASE("at-once")
	{
		/* a decal batch's chunks (rasterizer_decals_draw_batch) after a
		vertex of fewer registers, then fewer and more registers */
		int chunk;

		begin();
		write_register(4);
		write_register(D3DVSDE_VERTEX);
		for (chunk = 0; chunk < 5; chunk++)
			write_vertices((1UL << 0) | (1UL << 4) | (1UL << 9), 256);
		write_vertices((1UL << 0) | (1UL << 4) | (1UL << 9), 3);
		write_vertices((1UL << 0) | (1UL << 9), 2);
		write_register(12);
		write_register(D3DVSDE_VERTEX);
		write_vertices((1UL << 0) | (1UL << 2) | (1UL << 9) | (1UL << 15), 4);
		check_vertices();
		check_registers();
		return 0;
	}
	CASE("at-once-random")
	{
		unsigned long seed = 54321;
		int primitive;

		for (primitive = 0; primitive < 300; primitive++)
		{
			int steps, step;

			begin();
			seed = seed * 1103515245u + 12345u;
			steps = (int)((seed >> 16) % 40);
			for (step = 0; step < steps; step++)
			{
				int roll;

				seed = seed * 1103515245u + 12345u;
				roll = (int)((seed >> 16) % 24);
				if (roll < 18)
					write_register(roll < 2 ? D3DVSDE_VERTEX : roll - 2);
				else
				{
					seed = seed * 1103515245u + 12345u;
					write_vertices(((seed >> 8) & 0xFFFF) | 1, (seed >> 24) % 40 + 1);
				}
			}
			check_vertices();
			check_registers();
		}
		return 0;
	}
	CASE("at-once-outside")
	{
		/* outside Begin and End only the registers change; without register
		0, nothing */
		float data[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

		write_vertices((1UL << 0) | (1UL << 9), 3);
		CHECK(!game->immediate_count, "vertices outside Begin and End");
		check_registers();
		begin();
		halo_immediate_vertices((1UL << 4) | (1UL << 9), data, 1);
		halo_immediate_vertices(1UL << 0, data, 0);
		CHECK(!game->immediate_count && !game->immediate_written, "vertices without register 0");
		check_registers();
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
