/*
VR_MATH.C

The headset's poses and fields of view in the game's terms (display.vr).
OpenXR's LOCAL space has +x to the right, +y up and -z forward, in metres;
the game's world has +x forward, +y to the left and +z up, in units of 10
feet. A heading (the yaw the headset's forward has in the world) turns the
one into the other. Plain floats, so that the platform layer and the game's
units can both use them; tools/harness/tests/vr_math.c tests them against
the game's own frustum.
*/

#ifndef HALO_GLES

#include "vr_math.h"

#include <math.h>

#define VR_PI 3.14159265358979

void vr_rotate(const float q[4], const float v[3], float out[3])
{
	/* v + 2w(q x v) + 2q x (q x v), for q = (x, y, z, w) */
	float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);

	out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

void vr_quaternion_multiply(const float a[4], const float b[4], float out[4])
{
	float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];

	out[0] = x;
	out[1] = y;
	out[2] = z;
	out[3] = w;
}

void vr_local_to_world(const float local[3], float yaw, float out[3])
{
	float forward = -local[2], left = -local[0], up = local[1];
	float cosine = (float)cos(yaw), sine = (float)sin(yaw);

	out[0] = forward * cosine - left * sine;
	out[1] = forward * sine + left * cosine;
	out[2] = up;
}

void vr_pose_view(const struct vr_pose *pose, const float origin[3], const float anchor[3], float yaw,
	float out_position[3], float out_forward[3], float out_up[3])
{
	static const float local_forward[3] = { 0.0f, 0.0f, -1.0f };
	static const float local_up[3] = { 0.0f, 1.0f, 0.0f };
	float offset[3], turned[3];
	int axis;

	for (axis = 0; axis < 3; axis++)
		offset[axis] = pose->position[axis] - origin[axis];
	vr_local_to_world(offset, yaw, turned);
	for (axis = 0; axis < 3; axis++)
		out_position[axis] = anchor[axis] + turned[axis] * VR_UNITS_PER_METRE;
	vr_rotate(pose->orientation, local_forward, turned);
	vr_local_to_world(turned, yaw, out_forward);
	vr_rotate(pose->orientation, local_up, turned);
	vr_local_to_world(turned, yaw, out_up);
}

void vr_fov_bounds(const struct vr_fov *fov, float aspect, float bounds[4])
{
	bounds[0] = (float)tan(fov->left) / aspect;
	bounds[1] = (float)tan(fov->right) / aspect;
	bounds[2] = (float)tan(fov->down);
	bounds[3] = (float)tan(fov->up);
}

void vr_bounds_union(const float a[4], const float b[4], float margin, float out[4])
{
	out[0] = (a[0] < b[0] ? a[0] : b[0]) - margin;
	out[1] = (a[1] > b[1] ? a[1] : b[1]) + margin;
	out[2] = (a[2] < b[2] ? a[2] : b[2]) - margin;
	out[3] = (a[3] > b[3] ? a[3] : b[3]) + margin;
}

float vr_yaw_of(const float direction[3], float otherwise)
{
	if (direction[0] * direction[0] + direction[1] * direction[1] < 1e-8f)
		return otherwise;
	return (float)atan2(direction[1], direction[0]);
}

float vr_wrap_angle(float angle)
{
	double wrapped;

	if (angle - angle != 0.0f)
		return 0.0f;
	wrapped = fmod((double)angle + VR_PI, 2.0 * VR_PI);
	if (wrapped < 0.0)
		wrapped += 2.0 * VR_PI;
	return (float)(wrapped - VR_PI);
}

int vr_limit_reach(float offset[3], float reach)
{
	float length = (float)sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
	float scale;

	if (length <= reach)
		return 0;
	scale = reach / length;
	offset[0] *= scale;
	offset[1] *= scale;
	offset[2] *= scale;
	return 1;
}

int vr_finite(const float *values, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		/* (infinity less itself, and anything not a number, is not 0) */
		if (values[index] - values[index] != 0.0f)
			return 0;
	}
	return 1;
}

#endif
