/*
TEXTURE_ROTATION.C (test)

A texture animation's turn (shaders.c, shader_texture_animation_evaluate)
with the sine and cosine of its angles remembered (texture_rotation_get): the
transforms must be the ones the angle's own sine and cosine give, and a
repeated angle must not take them again. test_texture_rotation.py takes the
code (under_test.inc) from the sources.
*/

#include <math.h>
#include "harness.h"

typedef struct { real x, y; } real_point2d;
typedef struct { real n[4]; } real_vector4d;
#define NUMBER_OF_OBJECT_FUNCTION_REFERENCES 5
struct shader_texture_animation
{
	short u_source, u_function;
	real u_period, u_phase, u_scale;
	short v_source, v_function;
	real v_period, v_phase, v_scale;
	short r_source, r_function;
	real r_period, r_phase, r_scale;
	real_point2d r_center;
};
struct render_animation { real values[4]; };

/* the game's sine and cosine (real_math.h), counted */
static long trigonometry;
static real sine(real angle) { trigonometry++; return (real)sin(angle); }
static real cosine(real angle) { trigonometry++; return (real)cos(angle); }

/* the periodic function: its input */
static real periodic_function_evaluate(short function_type, real time) { return time; }

#include "under_test.inc"

static boolean same_bits(real a, real b)
{
	return !memcmp(&a, &b, sizeof(real));
}

/* the angle's sine and cosine through the memo, checked against its own; whether they were taken */
static boolean turn(real angle_degrees)
{
	real cosine_value, sine_value, radians = DEGREES_TO_RADIANS(angle_degrees);
	long before = trigonometry;

	texture_rotation_get(angle_degrees, &cosine_value, &sine_value);
	CHECK(same_bits(cosine_value, (real)cos(radians)) && same_bits(sine_value, (real)sin(radians)),
		"%.9g degrees: cosine %.9g and sine %.9g, not %.9g and %.9g", angle_degrees, cosine_value, sine_value,
		(real)cos(radians), (real)sin(radians));
	return trigonometry != before;
}

static unsigned long seed = 12345;
static real random_real(real low, real high)
{
	seed = seed * 1103515245UL + 12345UL;
	return low + (high - low) * (real)((seed >> 8) & 0xFFFF) / 65535.0f;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	CASE("repeated")
	{
		CHECK(turn(30.0f), "the first angle's sine and cosine were not taken");
		CHECK(!turn(30.0f), "a repeated angle's sine and cosine were taken again");
		CHECK(turn(30.000002f), "an angle a bit away was answered from the memo");
		CHECK(turn(-30.0f), "the negative angle was answered from the memo");
		return 0;
	}
	/* many angles, most of them repeated, more than the memo holds: every value the angle's own */
	CASE("crowd")
	{
		static real angles[300];
		int index, repeats = 0;

		for (index = 0; index < 300; index++)
			angles[index] = random_real(-720.0f, 720.0f);
		for (index = 0; index < 30000; index++)
			repeats += !turn(angles[index % 7 ? index % 40 : (index * 13) % 300]);
		CHECK(repeats > 10000, "only %d angles answered from the memo: the case does not exercise it", repeats);
		return 0;
	}
	/* the whole animation: the transforms the angle's own sine and cosine give, and none taken for no turn */
	CASE("transforms")
	{
		struct shader_texture_animation animation;
		struct render_animation render;
		real_vector4d u, v;
		int index;

		memset(&animation, 0, sizeof(animation));
		animation.r_source = 2;
		animation.r_scale = 90.0f;
		animation.r_center.x = 0.5f;
		animation.r_center.y = 0.25f;
		render.values[1] = 0.75f;
		for (index = 0; index < 200; index++)
		{
			real time = (real)(index % 50) * 0.37f, angle, radians, c, s, u_input, v_input;

			shader_texture_animation_evaluate(&animation, &render, 2.0f, 3.0f, 0.1f, 0.2f, 15.0f, time, &u, &v);
			angle = time * 90.0f * 0.75f + 15.0f;
			radians = DEGREES_TO_RADIANS(angle);
			c = (real)cos(radians);
			s = (real)sin(radians);
			u_input = (0.1f - 0.5f) + 0.0f;
			v_input = (0.2f - 0.25f) + 0.0f;
			CHECK(same_bits(u.n[0], c * 2.0f) && same_bits(u.n[1], -(s * 3.0f)) &&
				same_bits(u.n[3], c * u_input - s * v_input + 0.5f) &&
				same_bits(v.n[0], s * 2.0f) && same_bits(v.n[1], c * 3.0f) &&
				same_bits(v.n[3], c * v_input + s * u_input + 0.25f),
				"the transforms at time %g differ from the angle's own", time);
		}
		trigonometry = 0;
		shader_texture_animation_evaluate(&animation, NULL, 2.0f, 3.0f, 0.1f, 0.2f, 90.0f, -1.0f, &u, &v);
		CHECK(trigonometry == 0, "a turn of zero took a sine or cosine");
		CHECK(u.n[0] == 2.0f && u.n[1] == 0.0f && v.n[0] == 0.0f && v.n[1] == 3.0f, "a turn of zero turned the texture");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
