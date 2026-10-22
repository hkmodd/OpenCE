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

void vr_quaternion_between(const float from[3], const float to[3], float out[4])
{
	float w = 1.0f + from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
	float length;

	if (w < 1e-6f)
	{
		/* (opposite: about x crossed with `from`, or y when `from` is
		along x) */
		w = 0.0f;
		if (from[0] * from[0] < 0.9f)
		{
			out[0] = 0.0f;
			out[1] = -from[2];
			out[2] = from[1];
		}
		else
		{
			out[0] = from[2];
			out[1] = 0.0f;
			out[2] = -from[0];
		}
	}
	else
	{
		out[0] = from[1] * to[2] - from[2] * to[1];
		out[1] = from[2] * to[0] - from[0] * to[2];
		out[2] = from[0] * to[1] - from[1] * to[0];
	}
	out[3] = w;
	length = (float)sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + w * w);
	out[0] /= length;
	out[1] /= length;
	out[2] /= length;
	out[3] /= length;
}

void vr_local_to_world(const float local[3], float yaw, float out[3])
{
	float forward = -local[2], left = -local[0], up = local[1];
	float cosine = (float)cos(yaw), sine = (float)sin(yaw);

	out[0] = forward * cosine - left * sine;
	out[1] = forward * sine + left * cosine;
	out[2] = up;
}

void vr_world_to_local(const float world[3], float yaw, float out[3])
{
	float cosine = (float)cos(yaw), sine = (float)sin(yaw);
	float forward = world[0] * cosine + world[1] * sine, left = world[1] * cosine - world[0] * sine;

	out[0] = -left;
	out[1] = world[2];
	out[2] = -forward;
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

void vr_quaternion_from_axes(const float x[3], const float y[3], const float z[3], float orientation[4])
{
	float trace, scale;

	trace = x[0] + y[1] + z[2];
	if (trace > 0.0f)
	{
		scale = sqrtf(trace + 1.0f) * 2.0f;
		orientation[3] = 0.25f * scale;
		orientation[0] = (y[2] - z[1]) / scale;
		orientation[1] = (z[0] - x[2]) / scale;
		orientation[2] = (x[1] - y[0]) / scale;
	}
	else if (x[0] > y[1] && x[0] > z[2])
	{
		scale = sqrtf(1.0f + x[0] - y[1] - z[2]) * 2.0f;
		orientation[3] = (y[2] - z[1]) / scale;
		orientation[0] = 0.25f * scale;
		orientation[1] = (y[0] + x[1]) / scale;
		orientation[2] = (z[0] + x[2]) / scale;
	}
	else if (y[1] > z[2])
	{
		scale = sqrtf(1.0f + y[1] - x[0] - z[2]) * 2.0f;
		orientation[3] = (z[0] - x[2]) / scale;
		orientation[0] = (y[0] + x[1]) / scale;
		orientation[1] = 0.25f * scale;
		orientation[2] = (z[1] + y[2]) / scale;
	}
	else
	{
		scale = sqrtf(1.0f + z[2] - x[0] - y[1]) * 2.0f;
		orientation[3] = (x[1] - y[0]) / scale;
		orientation[0] = (z[0] + x[2]) / scale;
		orientation[1] = (z[1] + y[2]) / scale;
		orientation[2] = 0.25f * scale;
	}
}

float vr_drive_steer(float x, float left_limit, float right_limit, int backing)
{
	float limit, angle;

	if (!vr_finite(&x, 1))
		return 0.0f;
	if (x > 1.0f)
		x = 1.0f;
	else if (x < -1.0f)
		x = -1.0f;
	limit = (float)fabs(x > 0.0f ? right_limit : left_limit);
	if (!vr_finite(&limit, 1))
		return 0.0f;
	angle = (float)(-x * limit * VR_PI / 180.0);
	return backing ? -angle : angle;
}

#endif
