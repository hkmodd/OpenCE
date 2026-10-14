/*
PROGRAM_CONSTANTS.C (test)

The vertex constants c[] of a linked program (d3d8_gl.c: program_get's
lookup, constant_location and the upload of a changed range;
test_program_constants.py takes them as under_test.inc), with GL faked as a
driver that drops the registers past the last one the program reads and puts
the others at consecutive or scattered locations: the count is that of the
registers kept, a program without c asks for nothing more, the lookup asks a
handful of times, not once a register, each location is asked for once, and
a changed range is uploaded from its first register's location.
*/

#include "harness.h"

typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;

#include "world.inc"

struct program_entry
{
	GLuint program;
	GLint constants;
	unsigned long constant_count;
	GLint constant_locations[XGPU_VERTEX_CONSTANT_COUNT];
};

/* the registers the program keeps (c[0..kept)), none without c, and where */
static unsigned long kept;
static int has_constants, scattered;
static unsigned long lookups;

static GLint register_location(unsigned long index)
{
	return scattered ? 1000 - 3 * (GLint)index : 5 + (GLint)index;
}

static GLint glGetUniformLocation(GLuint program, const char *name)
{
	unsigned long index;
	char end;

	CHECK(program == 7, "asked program %u", program);
	lookups++;
	if (!strcmp(name, "c") || !strcmp(name, "c[0]"))
		return has_constants ? register_location(0) : -1;
	if (sscanf(name, "c[%lu%c", &index, &end) == 2 && end == ']' && has_constants)
		return index < kept ? register_location(index) : -1;
	return -1;
}

static struct { GLint location; GLsizei count; const GLfloat *values; int calls; } uploaded;

static void glUniform4fv(GLint location, GLsizei count, const GLfloat *values)
{
	uploaded.location = location;
	uploaded.count = count;
	uploaded.values = values;
	uploaded.calls++;
}

static struct { float constants[XGPU_VERTEX_CONSTANT_COUNT][4]; } device;

#include "under_test.inc"

static void constants_lookup(struct program_entry *entry)
{
#include "lookup.inc"
}

static void constants_upload(struct program_entry *entry, unsigned long first, unsigned long last)
{
#include "upload.inc"
}

static struct program_entry entry;

static void linked(unsigned long registers, int with_constants, int scatter)
{
	memset(&entry, 0xee, sizeof(entry));
	entry.program = 7;
	kept = registers;
	has_constants = with_constants;
	scattered = scatter;
	lookups = 0;
	constants_lookup(&entry);
}

static const unsigned long counts[] = { 1, 2, 3, 37, 96, 100, 128, 190, 191, XGPU_VERTEX_CONSTANT_COUNT };
#define COUNTS (sizeof(counts) / sizeof(counts[0]))

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "";
	unsigned long index, layout;

	if (!strcmp(which, "count"))
	{
		for (layout = 0; layout < 2; layout++)
		{
			for (index = 0; index < COUNTS; index++)
			{
				linked(counts[index], 1, (int)layout);
				CHECK(entry.constants == register_location(0), "c at %d", entry.constants);
				CHECK(entry.constant_count == counts[index], "%lu registers kept, counted %lu (%s)",
					counts[index], entry.constant_count, layout ? "scattered" : "consecutive");
			}
		}
		return 0;
	}
	if (!strcmp(which, "unused"))
	{
		linked(0, 0, 0);
		CHECK(entry.constants < 0, "a program without c given it at %d", entry.constants);
		CHECK(lookups == 1, "%lu lookups for a program without c", lookups);
		return 0;
	}
	if (!strcmp(which, "lookups"))
	{
		for (index = 0; index < COUNTS; index++)
		{
			linked(counts[index], 1, 0);
			/* c, then halving 191 registers */
			CHECK(lookups <= 9, "%lu lookups for %lu registers kept", lookups, counts[index]);
		}
		return 0;
	}
	if (!strcmp(which, "cached"))
	{
		linked(100, 1, 1);
		for (index = 0; index < 100; index++)
		{
			unsigned long before;

			CHECK(constant_location(&entry, index) == register_location(index), "c[%lu] at %d, not %d", index,
				constant_location(&entry, index), register_location(index));
			before = lookups;
			constant_location(&entry, index);
			CHECK(lookups == before, "c[%lu]'s location asked for again", index);
		}
		return 0;
	}
	if (!strcmp(which, "upload"))
	{
		for (layout = 0; layout < 2; layout++)
		{
			linked(100, 1, (int)layout);
			memset(&uploaded, 0, sizeof(uploaded));
			constants_upload(&entry, 5, 9);
			CHECK(uploaded.calls == 1 && uploaded.location == register_location(5),
				"c[5..9] uploaded at %d, c[5] is at %d (%s)", uploaded.location, register_location(5),
				layout ? "scattered" : "consecutive");
			CHECK(uploaded.count == 5 && uploaded.values == device.constants[5], "uploaded %d registers from %p",
				uploaded.count, (const void *)uploaded.values);
			memset(&uploaded, 0, sizeof(uploaded));
			constants_upload(&entry, 100, 0);
			CHECK(!uploaded.calls, "registers past those kept uploaded");
		}
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", which);
	return 2;
}
