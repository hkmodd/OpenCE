/*
VR_MATH.H

The headset's poses and fields of view in the game's terms (vr_math.c).
*/

#ifndef VR_MATH_H
#define VR_MATH_H

/* a pose in OpenXR's LOCAL space: metres, +x right, +y up, -z forward;
its orientation a unit quaternion (x, y, z, w) */
struct vr_pose
{
	float position[3];
	float orientation[4];
};

/* an eye's field as OpenXR gives it (XrFovf): radians, left and down
negative */
struct vr_fov
{
	float left, right, up, down;
};

/* the game's world units in a metre (one unit is 10 feet) */
#define VR_UNITS_PER_METRE 0.32808399f

/* q turning v */
void vr_rotate(const float q[4], const float v[3], float out[3]);
/* a turned by b (b first, then a) */
void vr_quaternion_multiply(const float a[4], const float b[4], float out[4]);
/* the shortest turn from one direction to another (each of length 1); half
a turn about an axis across `from` when they are opposite */
void vr_quaternion_between(const float from[3], const float to[3], float out[4]);

/* a LOCAL direction or offset in the game's axes (+x forward, +y left, +z
up) for a viewer whose heading is `yaw` (radians, growing to the left from
the world's +x) */
void vr_local_to_world(const float local[3], float yaw, float out[3]);
/* the inverse: a direction or offset in the game's axes as a LOCAL one */
void vr_world_to_local(const float world[3], float yaw, float out[3]);

/* where a pose looks from in the game's world: `anchor` (world units) moved
by the pose's offset from `origin` (LOCAL metres), and its forward and up,
turned by the heading `yaw` */
void vr_pose_view(const struct vr_pose *pose, const float origin[3], const float anchor[3], float yaw,
	float out_position[3], float out_forward[3], float out_up[3]);

/* an eye's field as render_camera_build_frustum's bounds (x0, x1, y0, y1)
for a camera of a 90-degree vertical field of view whose viewport is
`aspect` (width over height): it spans x as bounds times aspect times the
field's tangent (1), and y as bounds times the tangent */
void vr_fov_bounds(const struct vr_fov *fov, float aspect, float bounds[4]);
/* the bounds covering both eyes' (x0, x1, y0, y1), widened by `margin` */
void vr_bounds_union(const float a[4], const float b[4], float margin, float out[4]);

/* the yaw (radians, from the world's +x toward +y) of a direction in the
game's axes, or `otherwise` when it is (nearly) straight up or down */
float vr_yaw_of(const float direction[3], float otherwise);
/* an angle wrapped to -pi..pi */
float vr_wrap_angle(float angle);

/* the offset held to `reach` metres, its direction kept; returns whether it
was shortened */
int vr_limit_reach(float offset[3], float reach);

/* whether every value is finite */
int vr_finite(const float *values, int count);

#endif
