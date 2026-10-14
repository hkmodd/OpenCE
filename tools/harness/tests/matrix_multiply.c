/*
MATRIX_MULTIPLY.C (test)

The real matrix4x3_multiply of source/math/matrix_math.c, which every node
of every model goes through: its SSE block (the original's, as intrinsics)
must give each element exactly as the loop it stands in for does, which
the builds without SSE (Android, the web) run, since machines of a network
game must agree; and result may be a or b. test_matrix_multiply.py takes
the structure (config.inc) and the function twice, the SSE block as
matrix4x3_multiply and the loop as loop_multiply (under_test.inc).
*/

#include "harness.h"

#include <math.h>
#include <xmmintrin.h>

#ifndef __SSE__
#error the SSE block is not compiled: the test would compare the loop with itself
#endif

#include "config.inc"
#include "under_test.inc"

#define TRIALS 200000

static unsigned long random_state = 0x12345678UL;

static unsigned long random_next(void)
{
	random_state = random_state * 1664525UL + 1013904223UL;
	return random_state;
}

/* a value of any sign and of magnitudes from tiny to large, and some
exactly zero, exactly one or negative zero, as matrices hold */
static real random_real(void)
{
	unsigned long bits = random_next();
	real value;

	switch ((bits >> 28) & 7)
	{
	case 0:
		return 0.f;
	case 1:
		return (bits & 1) ? 1.f : -0.f;
	default:
		value = (real)(bits & 0xFFFFFF) / (real)0x1000000;
		value = ldexpf(value, (int)((bits >> 24) & 15) * 3 - 24);
		return (bits & 0x80000000UL) ? -value : value;
	}
}

static void random_matrix(real_matrix4x3 *matrix)
{
	short index;

	matrix->scale = random_real();
	for (index = 0; index < 12; index++)
		matrix->n[0][index] = random_real();
}

static boolean same_bits(real_matrix4x3 const *x, real_matrix4x3 const *y)
{
	return !memcmp(x, y, sizeof(*x));
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long trial;

	/* every element exactly as the loop gives it */
	CASE("same-as-the-loop")
	{
		for (trial = 0; trial < TRIALS; trial++)
		{
			real_matrix4x3 a, b, by_sse, by_loop;

			random_matrix(&a);
			random_matrix(&b);
			matrix4x3_multiply(&a, &b, &by_sse);
			loop_multiply(&a, &b, &by_loop);
			CHECK(same_bits(&by_sse, &by_loop), "trial %ld: the SSE block and the loop differ", trial);
		}
		return 0;
	}

	/* the result written over a, as models.c composes a node's matrix */
	CASE("result-is-a")
	{
		for (trial = 0; trial < TRIALS; trial++)
		{
			real_matrix4x3 a, b, expected;

			random_matrix(&a);
			random_matrix(&b);
			loop_multiply(&a, &b, &expected);
			matrix4x3_multiply(&a, &b, &a);
			CHECK(same_bits(&a, &expected), "trial %ld: result over a differs", trial);
		}
		return 0;
	}

	/* the result written over b */
	CASE("result-is-b")
	{
		for (trial = 0; trial < TRIALS; trial++)
		{
			real_matrix4x3 a, b, expected;

			random_matrix(&a);
			random_matrix(&b);
			loop_multiply(&a, &b, &expected);
			matrix4x3_multiply(&a, &b, &b);
			CHECK(same_bits(&b, &expected), "trial %ld: result over b differs", trial);
		}
		return 0;
	}

	/* nothing written outside the result */
	CASE("writes-only-the-result")
	{
		real_matrix4x3 matrices[3], a, b;
		unsigned char before[sizeof(matrices)];

		random_matrix(&a);
		random_matrix(&b);
		memset(matrices, 0x5A, sizeof(matrices));
		memcpy(before, matrices, sizeof(matrices));
		matrix4x3_multiply(&a, &b, &matrices[1]);
		CHECK(!memcmp(&matrices[0], &before[0], sizeof(matrices[0])), "the matrix before the result was written");
		CHECK(!memcmp(&matrices[2], &before[2 * sizeof(matrices[0])], sizeof(matrices[2])),
			"the matrix after the result was written");
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
