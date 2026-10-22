/*
VR_VIEW.C

The headset's view (display.vr, port/linux/include/halo_vr.h): the local
player's window drawn as two eyes and a HUD.

The eyes. main_game_render's player window becomes the left eye's, the
right eye's and the HUD's, and the console window follows them. Each eye is
the game's own camera, as the game made it (in a vehicle, dead, carried by
a script), with the headset put on it last: the head's offset from the
recentred origin (limited to a lean, and stopped short of walls), the eye's
own offset (the IPD), and the eye's orientation, its roll included, turned
by the heading. Its frustum is the eye's own off-centre field
(vr_fov_bounds), which render_camera_build_frustum takes as it is.

The heading. The head aims: the player's facing is the head's direction
turned by the heading, set each frame as the game's input is taken
(vr_player_control_facing), so that shots, the crosshair and the scripts
that wait for the player to look around all follow the head. The mouse's or
the stick's turns, and the game's own (a respawn, a script), move the
heading. Pitch is the head's alone.

The HUD is drawn alone, over a clear ground, from the head between the
eyes with the field of the panel it is shown on (vr_openxr.c holds the
panel ahead of the head), so that what it marks (nav points, friends'
names) lands on what it marks. The first-person weapon is posed once a
frame from the head between the eyes, and what changes once a frame (the
sky's animation, weather, glows) moves on in the left eye only.

The menus, loading screens and cutscenes are drawn as always, onto the
flat screen held in the room.
*/

#ifndef HALO_GLES

#include "cseries.h"
#include "halo_vr.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"

#include <math.h>

double config_real(const char *name);
/* d3d8_gl.c: the back buffer cleared to nothing (the HUD's ground), and
the back buffer's picture into an eye's image */
void halo_vr_clear_transparent(void);
void halo_vr_resolve_eye(int eye);

/* how far the head may lean the view from the game's camera, in metres,
and how far short of a wall it stops (world units) */
#define VR_HEAD_REACH 0.35f
#define VR_WALL_MARGIN 0.04f

/* a turn of the head in a frame (radians) that counts as looking around,
more than its tremor: about 15 degrees a second at 60 frames */
#define VR_LOOK_TURN 0.004f

int vr_render_pass;

static struct
{
	/* this frame's windows are the headset's */
	boolean stereo;
	short pass_of_window[MAXIMUM_WINDOWS + 1];
	real_rectangle2d eye_bounds[2];
	real_rectangle2d hud_bounds;
	/* between the eyes, looking as the head does */
	struct render_camera head_camera;

	/* the heading: the yaw the headset's forward has in the world */
	boolean heading_valid;
	real heading;
	/* the yaw the game's facing was last set to, and whether the head set it */
	real set_yaw;
	boolean aiming;
} vr_view;

/* ---------- the eyes */

/* the head's offset from the origin (LOCAL metres), limited to a lean and
stopped short of the walls around the game's camera */
static void vr_head_offset(
	struct vr_frame const *frame,
	real_point3d const *anchor,
	real heading,
	float offset[3])
{
	real_vector3d vector;
	float world[3];
	struct collision_result collision;
	real length;

	offset[0] = frame->head.position[0];
	offset[1] = frame->head.position[1];
	offset[2] = frame->head.position[2];
	vr_limit_reach(offset, VR_HEAD_REACH);
	vr_local_to_world(offset, heading, world);
	vector.i = world[0] * VR_UNITS_PER_METRE;
	vector.j = world[1] * VR_UNITS_PER_METRE;
	vector.k = world[2] * VR_UNITS_PER_METRE;
	length = magnitude3d(&vector);
	if (length < 1e-4f)
		return;
	/* (tested a margin further, so that the eyes beside the head and the
	near plane stay out of the wall too) */
	scale_vector3d(&vector, (length + VR_WALL_MARGIN) / length, &vector);
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		anchor, &vector, NONE, &collision))
	{
		real allowed = collision.t * (length + VR_WALL_MARGIN) - VR_WALL_MARGIN;
		float scale = allowed > 0.0f ? (float)(allowed / length) : 0.0f;

		offset[0] *= scale;
		offset[1] *= scale;
		offset[2] *= scale;
	}
}

/* a view from `pose` (its offset taken as `offset` from the anchor) */
static void vr_camera_from_pose(
	struct render_camera *camera,
	struct vr_pose const *pose,
	float const offset[3],
	real_point3d const *anchor,
	real heading)
{
	static float const origin[3] = { 0.0f, 0.0f, 0.0f };
	struct vr_pose moved = *pose;

	moved.position[0] = offset[0];
	moved.position[1] = offset[1];
	moved.position[2] = offset[2];
	vr_pose_view(&moved, origin, anchor->n, heading, camera->position.n, camera->forward.n, camera->up.n);
	/* the bounds are tangents of a 90-degree field (vr_fov_bounds) */
	camera->vertical_field_of_view = _pi * 0.5f;
}

/* the heading the eyes turn from: the one the head aims with, else the
game camera's less the head's yaw (before the first frame of aiming) */
static real vr_heading(
	struct vr_frame const *frame,
	struct render_camera const *camera)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float head[3], forward[3];

	if (vr_view.heading_valid)
		return vr_view.heading;
	vr_rotate(frame->head.orientation, local_forward, head);
	vr_local_to_world(head, 0.0f, forward);
	return vr_wrap_angle(vr_yaw_of(camera->forward.n, 0.0f) - vr_yaw_of(forward, 0.0f));
}

/* the HUD panel's field (vr.hud_distance and vr.hud_width, as vr_openxr.c
places it): x and y each half the width over the distance, in the bounds'
units for the HUD window's aspect */
static void vr_hud_bounds(
	struct render_camera const *camera,
	real_rectangle2d *bounds)
{
	real distance = (real)config_real("vr.hud_distance");
	real width = (real)config_real("vr.hud_width");
	real aspect = (real)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0) /
		(real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	real tangent;

	if (!(distance >= 0.5f && distance <= 100.0f))
		distance = 15.0f;
	if (!(width >= 0.2f && width <= 100.0f))
		width = 10.0f;
	tangent = width * 0.5f / distance;
	bounds->x0 = -tangent / aspect;
	bounds->x1 = tangent / aspect;
	/* (the panel is 4:3: its height is three quarters of its width) */
	bounds->y0 = -tangent * 0.75f;
	bounds->y1 = tangent * 0.75f;
}

short vr_render_windows(
	struct render_window *windows,
	short window_count)
{
	struct vr_frame const *frame;
	struct render_window player, console;
	real_point3d anchor;
	float head[3], offset[3];
	real heading, aspect;
	short window_index;
	int eye;

	vr_view.stereo = FALSE;
	for (window_index = 0; window_index < MAXIMUM_WINDOWS + 1; window_index++)
		vr_view.pass_of_window[window_index] = _vr_render_pass_none;
	frame = vr_frame_get();
	if (!frame->stereo ||
		window_count != 2 ||
		windows[0].local_player_index == NONE ||
		windows[0].console_window ||
		!windows[1].console_window ||
		cinematic_in_progress() ||
		/* the main menu's scene stays behind its menus, on the flat screen */
		global_scenario_get()->type == _scenario_type_main_menu)
	{
		return window_count;
	}
	player = windows[0];
	console = windows[1];
	anchor = player.render_camera.position;
	heading = vr_heading(frame, &player.render_camera);
	aspect = (real)(player.render_camera.viewport_bounds.x1 - player.render_camera.viewport_bounds.x0) /
		(real)(player.render_camera.viewport_bounds.y1 - player.render_camera.viewport_bounds.y0);
	vr_head_offset(frame, &anchor, heading, head);
	for (eye = 0; eye < 2; eye++)
	{
		struct render_camera *camera = &windows[eye].rasterizer_camera;

		windows[eye] = player;
		offset[0] = head[0] + frame->eye[eye].position[0] - frame->head.position[0];
		offset[1] = head[1] + frame->eye[eye].position[1] - frame->head.position[1];
		offset[2] = head[2] + frame->eye[eye].position[2] - frame->head.position[2];
		vr_camera_from_pose(camera, &frame->eye[eye], offset, &anchor, heading);
		windows[eye].render_camera = *camera;
		vr_fov_bounds(&frame->fov[eye], aspect, vr_view.eye_bounds[eye].n);
		vr_view.pass_of_window[eye] = eye ? _vr_render_pass_right_eye : _vr_render_pass_left_eye;
	}
	/* the head between the eyes: the HUD's view and the weapon's */
	vr_view.head_camera = player.rasterizer_camera;
	vr_camera_from_pose(&vr_view.head_camera, &frame->head, head, &anchor, heading);
	windows[2] = player;
	windows[2].rasterizer_camera = vr_view.head_camera;
	windows[2].render_camera = vr_view.head_camera;
	vr_hud_bounds(&vr_view.head_camera, &vr_view.hud_bounds);
	vr_view.pass_of_window[2] = _vr_render_pass_hud;
	windows[3] = console;
	vr_view.stereo = TRUE;
	return 4;
}

void vr_render_window_begin(
	short window_index)
{
	vr_render_pass = vr_view.stereo && window_index >= 0 && window_index <= MAXIMUM_WINDOWS ?
		vr_view.pass_of_window[window_index] : _vr_render_pass_none;
}

void vr_render_window_end(
	short window_index)
{
	if (VR_RENDER_EYE())
		halo_vr_resolve_eye(vr_render_pass == _vr_render_pass_right_eye);
	vr_render_pass = _vr_render_pass_none;
}

void vr_render_frustum_bounds(
	union real_rectangle2d *bounds)
{
	if (VR_RENDER_EYE())
		*bounds = vr_view.eye_bounds[vr_render_pass == _vr_render_pass_right_eye];
	else if (VR_RENDER_HUD())
		*bounds = vr_view.hud_bounds;
}

void vr_render_weapon_camera(
	struct render_camera *camera)
{
	if (!vr_view.stereo)
		return;
	camera->position = vr_view.head_camera.position;
	camera->forward = vr_view.head_camera.forward;
	camera->up = vr_view.head_camera.up;
}

/* the HUD's ground: nothing, so that the panel shows only the HUD */
void vr_render_hud_begin(
	void)
{
	halo_vr_clear_transparent();
}

/* ---------- aiming */

void vr_player_control_facing(
	short local_player_index)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	struct vr_frame const *frame = vr_frame_get();
	static real_vector2d const still = { 0.0f, 0.0f };
	real_euler_angles2d const *angles;
	real_euler_angles2d before, turn;
	real_vector3d facing;
	float head[3], forward[3];

	vr_view.aiming = FALSE;
	/* (the first local player's alone: the headset is one) */
	if (!frame->stereo || local_player_index != local_player_get_next(NONE) ||
		local_player_get_player_index(local_player_index) == NONE)
	{
		return;
	}
	angles = player_control_get_facing_angles(local_player_index);
	before = *angles;
	/* the game's turns since the head last aimed (the input's this frame,
	a respawn's, a script's) turn the heading */
	vr_rotate(frame->head.orientation, local_forward, head);
	vr_local_to_world(head, 0.0f, forward);
	if (!vr_view.heading_valid)
	{
		vr_view.heading = vr_wrap_angle(before.yaw - vr_yaw_of(forward, 0.0f));
		vr_view.heading_valid = TRUE;
	}
	else
	{
		vr_view.heading = vr_wrap_angle(vr_view.heading + vr_wrap_angle(before.yaw - vr_view.set_yaw));
	}
	vr_local_to_world(head, vr_view.heading, facing.n);
	/* (straight up or down the game's angles have no yaw: kept as it was) */
	if (facing.i * facing.i + facing.j * facing.j < 1e-6f)
		return;
	player_control_set_facing(local_player_index, &facing);
	angles = player_control_get_facing_angles(local_player_index);
	vr_view.set_yaw = angles->yaw;
	vr_view.aiming = TRUE;

	/* the head's turns count as looking, for the scripts that wait for the
	player to look around (the first level's look test watches the stick;
	a host counts a client's from its facing, network_distributed.c) */
	turn.yaw = vr_wrap_angle(angles->yaw - before.yaw);
	turn.pitch = angles->pitch - before.pitch;
	if (fabs(turn.yaw) <= VR_LOOK_TURN)
		turn.yaw = 0.0f;
	if (fabs(turn.pitch) <= VR_LOOK_TURN)
		turn.pitch = 0.0f;
	if (turn.yaw != 0.0f || turn.pitch != 0.0f)
		player_control_action_test_note(0, &turn, &still, 0.0f);
}

int vr_render_aiming(
	void)
{
	return vr_view.aiming;
}

/* the game state was loaded or the map changed: the heading is taken
again from the game's facing */
void vr_view_reset(
	void)
{
	vr_view.heading_valid = FALSE;
	vr_view.stereo = FALSE;
	vr_render_pass = _vr_render_pass_none;
}

#endif
