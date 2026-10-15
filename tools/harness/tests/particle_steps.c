#include "harness.h"
#include <math.h>

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define NUMBEROF(a) ((long)(sizeof(a)/sizeof((a)[0])))
#define profile_enter(section) ((void)0)
#define profile_exit(section) ((void)0)
#include "config.inc"

/* Particles in a straight line at their velocity, spinning at their angular
velocity, drawn every frame and never old enough to die; the physics of each
call recorded (particles.c). */
#define HALO_PORT_MAXIMUM_PARTICLES 8
struct particle_datum
{
	struct datum_header header;
	long definition_index;
	long object_index;
	long last_rendered_frame_index;
	real age;
	real lifespan;
	real_point3d position;
	real_vector3d translational_velocity;
	real rotation;
	real angular_velocity;
};
struct particle_definition { short final_sequence_count; };
static struct data_array *particle_data;
static struct particle_definition the_definition;
static struct { long frame_index; } render;
static real particles_leftover_ticks;
static long particles_update_ticks;
static long particles_tick_frame_indices[MAXIMUM_PARTICLE_UNRENDERED_FRAME_COUNT + 1];
static long particles_tick_frame_head;
static int physics_calls;
static real physics_dt_lowest, physics_dt_highest;
static int physics_fails_on_call = -1;

#define particle_get(index) ((struct particle_datum *)datum_get(particle_data, (index)))
#define particle_definition_get(index) (&the_definition)
static long data_next_index(struct data_array *array, long index)
{
	long absolute = index == NONE ? 0 : DATUM_INDEX_TO_ABSOLUTE_INDEX(index) + 1;

	for (; absolute < array->maximum_count; absolute++)
	{
		struct datum_header *header = (struct datum_header *)(array->data + absolute * array->size);

		if (header->identifier)
			return ((long)(unsigned short)header->identifier << 16) | absolute;
	}
	return NONE;
}
static void particle_die(long particle_index) { CHECK(FALSE, "a particle died of age"); }
static boolean particle_update_frame_time(long particle_index, real dt) { return TRUE; }
static boolean particle_update_physics(long particle_index, real dt)
{
	struct particle_datum *particle = particle_get(particle_index);

	physics_calls++;
	physics_dt_lowest = physics_calls == 1 ? dt : MIN(physics_dt_lowest, dt);
	physics_dt_highest = physics_calls == 1 ? dt : MAX(physics_dt_highest, dt);
	particle->position.x += particle->translational_velocity.i * dt;
	particle->position.y += particle->translational_velocity.j * dt;
	particle->position.z += particle->translational_velocity.k * dt;
	particle->rotation += particle->angular_velocity * dt;
	return physics_calls != physics_fails_on_call;
}
#include "under_test.inc"

#define STEP (1.0f / 30.0f)
#define FAST (1.0f / 360.0f)

static long particle_new_at(real x, real speed, long object_index)
{
	long index = datum_new(particle_data);
	struct particle_datum *particle = particle_get(index);

	particle->definition_index = 1;
	particle->object_index = object_index;
	particle->lifespan = 1000.0f;
	particle->position.x = x;
	particle->translational_velocity.i = speed;
	particle->angular_velocity = speed / 10.0f;
	particle->rotation = x / 10.0f;
	return index;
}

static void frame(real dt)
{
	long index;

	render.frame_index++;
	particles_update(dt);
	for (index = data_next_index(particle_data, NONE); index != NONE; index = data_next_index(particle_data, index))
		particle_get(index)->last_rendered_frame_index = render.frame_index;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	particle_data = game_state_data_new("particle", HALO_PORT_MAXIMUM_PARTICLES, sizeof(struct particle_datum));

	/* a free particle at 360 frames a second: a step a 30th of a second, the
	first on its first frame, drawn where it is at that frame's time */
	CASE("stepped")
	{
		long index = particle_new_at(0.0f, 30.0f, NONE);
		int frame_index;

		for (frame_index = 1; frame_index <= 720; frame_index++)
		{
			struct particle_datum *particle;
			real time = frame_index * FAST;
			int steps = (frame_index + 11) / 12;

			frame(FAST);
			particle = particle_get(index);
			CHECK(fabsf(particle->position.x - 30.0f * time) < 0.002f, "frame %d: at %f, not %f",
				frame_index, particle->position.x, 30.0f * time);
			CHECK(fabsf(particle->rotation - 3.0f * time) < 0.0002f, "frame %d: turned %f, not %f",
				frame_index, particle->rotation, 3.0f * time);
			CHECK(physics_calls == steps || physics_calls == steps + 1, "frame %d: %d steps, not %d",
				frame_index, physics_calls, steps);
		}
		CHECK(fabsf(physics_dt_lowest - STEP) < 1e-6f && fabsf(physics_dt_highest - STEP) < 1e-6f,
			"steps of %f to %f seconds", physics_dt_lowest, physics_dt_highest);
		return 0;
	}
	/* at 20 frames a second, a step a frame of the frame's time, as before */
	CASE("slow")
	{
		long index = particle_new_at(1.0f, 30.0f, NONE);
		real expected_x = 1.0f, expected_rotation = 0.1f;
		int frame_index;

		for (frame_index = 1; frame_index <= 40; frame_index++)
		{
			struct particle_datum *particle;

			frame(0.05f);
			expected_x += 30.0f * 0.05f;
			expected_rotation += 3.0f * 0.05f;
			particle = particle_get(index);
			CHECK(particle->position.x == expected_x && particle->rotation == expected_rotation,
				"frame %d: at %f turned %f, not %f %f", frame_index, particle->position.x, particle->rotation,
				expected_x, expected_rotation);
		}
		CHECK(physics_calls == 40 && physics_dt_lowest == 0.05f && physics_dt_highest == 0.05f,
			"%d steps of %f to %f seconds", physics_calls, physics_dt_lowest, physics_dt_highest);
		return 0;
	}
	/* one on an object, every frame */
	CASE("attached")
	{
		int frame_index;

		particle_new_at(0.0f, 30.0f, 7);
		for (frame_index = 1; frame_index <= 120; frame_index++)
			frame(FAST);
		CHECK(physics_calls == 120 && physics_dt_lowest == FAST && physics_dt_highest == FAST,
			"%d updates of %f to %f seconds", physics_calls, physics_dt_lowest, physics_dt_highest);
		return 0;
	}
	/* a new particle in a slot, where the last was drawn, starts from there */
	CASE("reused")
	{
		long first = particle_new_at(0.0f, 30.0f, NONE), second;
		struct particle_datum *particle;
		real_point3d shown;
		real shown_rotation;
		int frame_index;

		for (frame_index = 1; frame_index <= 5; frame_index++)
			frame(FAST);
		particle = particle_get(first);
		shown = particle->position;
		shown_rotation = particle->rotation;
		datum_delete(particle_data, first);
		second = particle_new_at(0.0f, 0.0f, NONE);
		CHECK(DATUM_INDEX_TO_ABSOLUTE_INDEX(second) == DATUM_INDEX_TO_ABSOLUTE_INDEX(first) && second != first,
			"not the same slot");
		particle = particle_get(second);
		particle->position = shown;
		particle->rotation = shown_rotation;
		for (frame_index = 1; frame_index <= 30; frame_index++)
		{
			frame(FAST);
			CHECK(particle->position.x == shown.x && particle->rotation == shown_rotation,
				"frame %d: at %f turned %f, not %f %f", frame_index, particle->position.x, particle->rotation,
				shown.x, shown_rotation);
		}
		return 0;
	}
	/* one moved or turned by something else starts from there */
	CASE("moved")
	{
		long index = particle_new_at(0.0f, 30.0f, NONE);
		struct particle_datum *particle = particle_get(index);
		int frame_index;

		for (frame_index = 1; frame_index <= 5; frame_index++)
			frame(FAST);
		particle->position.x = 100.0f;
		particle->translational_velocity.i = 0.0f;
		particle->angular_velocity = 0.0f;
		frame(FAST);
		CHECK(particle->position.x == 100.0f, "at %f, not 100", particle->position.x);
		particle->rotation = 5.0f;
		for (frame_index = 1; frame_index <= 30; frame_index++)
		{
			frame(FAST);
			CHECK(particle->position.x == 100.0f && particle->rotation == 5.0f, "frame %d: at %f turned %f",
				frame_index, particle->position.x, particle->rotation);
		}
		return 0;
	}
	/* dead or deleted in a step, it is left where it stepped to */
	CASE("died")
	{
		long index = particle_new_at(0.0f, 30.0f, NONE);
		struct particle_datum *particle = particle_get(index);

		physics_fails_on_call = 2;
		while (physics_calls < 2)
			frame(FAST);
		CHECK(fabsf(particle->position.x - 2.0f) < 0.0001f, "at %f, not 2", particle->position.x);
		return 0;
	}
	CHECK(FALSE, "no case %s", case_name);
	return 1;
}
