/* The rendered particles' sort (render_particles.c) on lists of made-up particles. */
#include "harness.h"

#define __cdecl
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define csmemcpy memcpy
#define csmemset memset
/* (the game's boolean is a byte: the datum is 8 bytes) */
#define boolean byte
#include "types.inc"
#undef boolean

#include "under_test.inc"

static struct rendered_particle_datum particles[MAXIMUM_RENDERED_PARTICLES];
static struct rendered_particle_datum original[MAXIMUM_RENDERED_PARTICLES];
static unsigned long seed = 1;

static unsigned long next_random(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (seed >> 16) & 0x7FFF;
}

/* count particles in particle_data's order, their keys drawn from small ranges
so that many are equal */
static void fill(long count, int definitions, int clusters, int attached)
{
	long i;

	for (i = 0; i < count; i++)
	{
		particles[i].particle_index = (short)i;
		particles[i].definition_index = (short)(next_random() % definitions);
		particles[i].cluster_index = (short)(next_random() % clusters) - 1;
		particles[i].attached_to_first_person_weapon = (byte)(next_random() % attached);
		particles[i].pad = 0;
	}
	memcpy(original, particles, sizeof(particles));
}

/* in compare_rendered_particles' order, equal ones in particle_data's, each
particle once and unchanged */
static void check_sorted(long count)
{
	static unsigned char seen[MAXIMUM_RENDERED_PARTICLES];
	long i;

	memset(seen, 0, sizeof(seen));
	for (i = 0; i < count; i++)
	{
		short index = particles[i].particle_index;

		CHECK(index >= 0 && index < count && !seen[index], "particle %d listed twice or made up (count %ld)", index, count);
		seen[index] = 1;
		CHECK(!memcmp(&particles[i], &original[index], sizeof(particles[i])), "particle %d changed", index);
		if (i > 0)
		{
			int order = compare_rendered_particles(&particles[i - 1], &particles[i]);

			CHECK(order <= 0, "out of order at %ld of %ld", i, count);
			CHECK(order != 0 || particles[i - 1].particle_index < particles[i].particle_index,
				"equal particles %d and %d swapped (count %ld)", particles[i - 1].particle_index,
				particles[i].particle_index, count);
		}
	}
}

static void sort_and_check(long count, int definitions, int clusters, int attached)
{
	fill(count, definitions, clusters, attached);
	sort_rendered_particles(particles, count);
	check_sorted(count);
}

int main(int argc, char **argv)
{
	char const *case_name;
	long count;

	CHECK(argc == 2, "one named case");
	case_name = argv[1];

	CASE("small")
	{
		/* every count up to a few passes, odd and even numbers of passes */
		for (count = 0; count <= 70; count++)
			sort_and_check(count, 3, 3, 2);
		return 0;
	}
	CASE("equal")
	{
		/* all equal: the order is particle_data's */
		sort_and_check(MAXIMUM_RENDERED_PARTICLES, 1, 1, 1);
		for (count = 0; count < MAXIMUM_RENDERED_PARTICLES; count++)
			CHECK(particles[count].particle_index == count, "equal particles reordered at %ld", count);
		return 0;
	}
	CASE("full")
	{
		sort_and_check(MAXIMUM_RENDERED_PARTICLES, 40, 8, 2);
		sort_and_check(MAXIMUM_RENDERED_PARTICLES - 1, 400, 30, 2);
		sort_and_check(5000, 7, 2, 2);
		return 0;
	}
	CASE("wide")
	{
		/* keys over all of a short (negative ones too) and every flag value:
		each byte of them differs */
		fill(MAXIMUM_RENDERED_PARTICLES, 1, 1, 1);
		for (count = 0; count < MAXIMUM_RENDERED_PARTICLES; count++)
		{
			particles[count].definition_index = (short)(next_random() << 1 | (next_random() & 1));
			particles[count].cluster_index = (short)(next_random() << 1 | (next_random() & 1));
			particles[count].attached_to_first_person_weapon = (byte)next_random();
			original[count] = particles[count];
		}
		sort_rendered_particles(particles, MAXIMUM_RENDERED_PARTICLES);
		check_sorted(MAXIMUM_RENDERED_PARTICLES);
		return 0;
	}
	CASE("reversed")
	{
		/* sorted backwards to begin with */
		fill(1000, 1, 1, 1);
		for (count = 0; count < 1000; count++)
		{
			particles[count].definition_index = (short)(999 - count) / 4;
			particles[count].cluster_index = 0;
			particles[count].attached_to_first_person_weapon = 0;
		}
		memcpy(original, particles, sizeof(particles));
		sort_rendered_particles(particles, 1000);
		check_sorted(1000);
		return 0;
	}

	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
