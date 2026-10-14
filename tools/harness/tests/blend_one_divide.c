#include "harness.h"
#include <math.h>

/* A node's matrix blended between two ticks (render_interpolation.c), its
rotation as a quaternion normalised in the basis: against a reference that
normalises the quaternion first, with a square root, as the blend once did. */
typedef struct { real scale; real_vector3d forward, left, up; real_point3d position; } real_matrix4x3;
#include "under_test.inc"

#define TOLERANCE 2e-6f

static real lerp_reference(real a, real b, real t)
{
	return a + (b - a) * t;
}

/* the blend's rotation, normalised with a square root and four divisions */
static boolean reference_basis(struct interpolation_quaternion qa, struct interpolation_quaternion qb, real t,
	real_matrix4x3 *matrix)
{
	struct interpolation_quaternion q;
	double length;
	double ii, jj, kk, ij, ik, jk, wi, wj, wk;

	if (qa.i * qb.i + qa.j * qb.j + qa.k * qb.k + qa.w * qb.w < 0.0f)
	{
		qb.i = -qb.i;
		qb.j = -qb.j;
		qb.k = -qb.k;
		qb.w = -qb.w;
	}
	q.i = lerp_reference(qa.i, qb.i, t);
	q.j = lerp_reference(qa.j, qb.j, t);
	q.k = lerp_reference(qa.k, qb.k, t);
	q.w = lerp_reference(qa.w, qb.w, t);
	length = sqrt((double)q.i * q.i + (double)q.j * q.j + (double)q.k * q.k + (double)q.w * q.w);
	if (!(length > 1e-6))
		return FALSE;
	ii = q.i / length * (q.i / length); jj = q.j / length * (q.j / length); kk = q.k / length * (q.k / length);
	ij = q.i / length * (q.j / length); ik = q.i / length * (q.k / length); jk = q.j / length * (q.k / length);
	wi = q.w / length * (q.i / length); wj = q.w / length * (q.j / length); wk = q.w / length * (q.k / length);
	matrix->forward.i = (real)(1.0 - 2.0 * (jj + kk));
	matrix->forward.j = (real)(2.0 * (ij + wk));
	matrix->forward.k = (real)(2.0 * (ik - wj));
	matrix->left.i = (real)(2.0 * (ij - wk));
	matrix->left.j = (real)(1.0 - 2.0 * (ii + kk));
	matrix->left.k = (real)(2.0 * (jk + wi));
	matrix->up.i = (real)(2.0 * (ik + wj));
	matrix->up.j = (real)(2.0 * (jk - wi));
	matrix->up.k = (real)(1.0 - 2.0 * (ii + jj));
	return TRUE;
}

static unsigned long seed = 12345;

static real random_real(void)
{
	seed = seed * 1664525UL + 1013904223UL;
	return (real)((seed >> 8) & 0xFFFFFF) / 16777216.0f;
}

static struct interpolation_quaternion random_quaternion(real length)
{
	struct interpolation_quaternion q;
	real magnitude;

	do
	{
		q.i = random_real() * 2.0f - 1.0f;
		q.j = random_real() * 2.0f - 1.0f;
		q.k = random_real() * 2.0f - 1.0f;
		q.w = random_real() * 2.0f - 1.0f;
		magnitude = sqrtf(q.i * q.i + q.j * q.j + q.k * q.k + q.w * q.w);
	} while (magnitude < 0.1f);
	q.i *= length / magnitude;
	q.j *= length / magnitude;
	q.k *= length / magnitude;
	q.w *= length / magnitude;
	return q;
}

static void matrix_fill(real_matrix4x3 *matrix, real base)
{
	matrix->scale = base;
	matrix->forward.i = base + 0.1f; matrix->forward.j = base + 0.2f; matrix->forward.k = base + 0.3f;
	matrix->left.i = base + 0.4f; matrix->left.j = base + 0.5f; matrix->left.k = base + 0.6f;
	matrix->up.i = base + 0.7f; matrix->up.j = base + 0.8f; matrix->up.k = base + 0.9f;
	matrix->position.x = base + 1.0f; matrix->position.y = base + 2.0f; matrix->position.z = base + 3.0f;
}

static void check_close(real value, real expected, const char *what, int index)
{
	CHECK(fabsf(value - expected) <= TOLERANCE, "%s of blend %d: %.9g, not %.9g", what, index, value, expected);
}

static void check_basis(real_matrix4x3 const *result, real_matrix4x3 const *expected, int index)
{
	check_close(result->forward.i, expected->forward.i, "forward.i", index);
	check_close(result->forward.j, expected->forward.j, "forward.j", index);
	check_close(result->forward.k, expected->forward.k, "forward.k", index);
	check_close(result->left.i, expected->left.i, "left.i", index);
	check_close(result->left.j, expected->left.j, "left.j", index);
	check_close(result->left.k, expected->left.k, "left.k", index);
	check_close(result->up.i, expected->up.i, "up.i", index);
	check_close(result->up.j, expected->up.j, "up.j", index);
	check_close(result->up.k, expected->up.k, "up.k", index);
}

/* blends a to b with these quaternions, and checks the basis against the
reference: a rotation, or a and b's bases blended as they are */
static void check_blend(struct interpolation_quaternion qa, struct interpolation_quaternion qb, real t, int index)
{
	real_matrix4x3 a, b, result, expected;
	struct interpolation_rotation rotation_a, rotation_b;

	matrix_fill(&a, 1.0f);
	matrix_fill(&b, 2.0f);
	rotation_a.quaternion = qa;
	rotation_a.is_rotation = TRUE;
	rotation_b.quaternion = qb;
	rotation_b.is_rotation = TRUE;
	matrix_blend_rotations(&a, &b, &rotation_a, &rotation_b, t, &result);
	if (!reference_basis(qa, qb, t, &expected))
	{
		expected.forward.i = lerp_reference(a.forward.i, b.forward.i, t);
		expected.forward.j = lerp_reference(a.forward.j, b.forward.j, t);
		expected.forward.k = lerp_reference(a.forward.k, b.forward.k, t);
		expected.left.i = lerp_reference(a.left.i, b.left.i, t);
		expected.left.j = lerp_reference(a.left.j, b.left.j, t);
		expected.left.k = lerp_reference(a.left.k, b.left.k, t);
		expected.up.i = lerp_reference(a.up.i, b.up.i, t);
		expected.up.j = lerp_reference(a.up.j, b.up.j, t);
		expected.up.k = lerp_reference(a.up.k, b.up.k, t);
	}
	check_basis(&result, &expected, index);
	check_close(result.scale, lerp_reference(a.scale, b.scale, t), "scale", index);
	check_close(result.position.x, lerp_reference(a.position.x, b.position.x, t), "position.x", index);
	check_close(result.position.z, lerp_reference(a.position.z, b.position.z, t), "position.z", index);
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	int index;

	CASE("rotation")
	{
		/* unit quaternions, as quaternion_from_basis finds them, a little
		long or short (rounding), and taken the longer way round */
		for (index = 0; index < 200000; index++)
		{
			real length = (index & 1) ? 1.0f : 0.999f + random_real() * 0.002f;

			check_blend(random_quaternion(length), random_quaternion(length), random_real(), index);
		}
		check_blend(random_quaternion(1.0f), random_quaternion(1.0f), 0.0f, -1);
		check_blend(random_quaternion(1.0f), random_quaternion(1.0f), 1.0f, -2);
	}
	CASE("small")
	{
		/* quaternions so short that the blend takes the bases as they are,
		and some only just long enough to be rotations */
		check_blend(random_quaternion(1e-7f), random_quaternion(1e-7f), 0.5f, 1);
		check_blend(random_quaternion(5e-7f), random_quaternion(5e-7f), 0.25f, 2);
		check_blend(random_quaternion(5e-6f), random_quaternion(5e-6f), 0.5f, 3);
		check_blend(random_quaternion(2e-5f), random_quaternion(2e-5f), 0.75f, 4);
		check_blend(random_quaternion(3.0f), random_quaternion(3.0f), 0.5f, 5);
	}
	CASE("not_a_number")
	{
		struct interpolation_quaternion q = random_quaternion(1.0f);

		q.w = NAN;
		check_blend(q, random_quaternion(1.0f), 0.5f, 1);
	}
	return 0;
}
