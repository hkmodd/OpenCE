/*
HALO_VR.H

The headset (display.vr): what the game's sources, the port's game units and
the platform layer say to one another. The platform layer
(port/linux/src/vr_openxr.c) runs the OpenXR session and its frames; the
game side (port/linux/game/vr_view.c) turns the player's window into the
eyes' and the HUD's. With display.vr off, none of it runs: each hook tests
one global first. The OpenGL ES builds (Android, the web) compile none of
it; the macros at the end stand in for the hooks there.
*/

#ifndef HALO_VR_H
#define HALO_VR_H

#ifndef HALO_GLES

#include "vr_math.h"

struct render_window;
struct render_camera;
union real_rectangle2d;

/* a pass of the headset's frame (vr_view.c) */
enum
{
	_vr_render_pass_none,
	_vr_render_pass_left_eye,
	_vr_render_pass_right_eye,
	_vr_render_pass_hud,
};

/* the pass being drawn, _vr_render_pass_none outside the headset's windows */
extern int vr_render_pass;

#define VR_RENDER_EYE() (vr_render_pass == _vr_render_pass_left_eye || vr_render_pass == _vr_render_pass_right_eye)
#define VR_RENDER_HUD() (vr_render_pass == _vr_render_pass_hud)
/* a pass after the frame's first, which must not move what changes once a
frame on again (the sky's animation, weather, glows) */
#define VR_RENDER_REPEAT() (vr_render_pass == _vr_render_pass_right_eye || vr_render_pass == _vr_render_pass_hud)

/* the headset's poses for the frame being drawn (vr_frame_get), in
OpenXR's LOCAL space (vr_math.h) */
struct vr_frame
{
	/* the eyes are tracked and the runtime wants them drawn */
	int stereo;
	/* the headset is worn and the game has its input */
	int focused;
	/* seconds between the headset's frames */
	float display_period;
	struct vr_pose head;
	struct vr_pose eye[2];
	struct vr_fov fov[2];
	/* counts the runtime's recentres (its LOCAL space moved) */
	unsigned long recentres;
};

/* ---------- the platform layer (vr_openxr.c) */

/* display.vr: loads the OpenXR runtime and starts its session on the
current OpenGL context; without it, or if that fails, the game is drawn as
always */
void vr_initialize(void);
void vr_dispose(void);
/* the session is running: the game's frames are the headset's */
int vr_active(void);
/* the headset's frame, at the top of each pass of the main loop: waits for
the runtime's frame (which paces the game), begins it and takes the poses
for its display time; whether the eyes are to be drawn */
int vr_frame_begin(void);
/* the frame's poses (stereo 0 outside one) */
struct vr_frame const *vr_frame_get(void);
/* the time from this frame's display to the last's, in seconds; 0 when
the headset is not pacing the frames */
int vr_frame_seconds(double *seconds);
/* the end of the main loop's pass: a frame begun and never presented ends
with nothing shown */
void vr_frame_finish(void);
/* the eyes' resolution while the session is running (the screen's targets
are drawn at it: d3d8_gl.c's screen_mode_choose) */
int vr_screen_size(long *width, long *height);
/* the back buffer's picture (a texture of width by height, its rows from
the top) is an eye's: into that eye's image */
void vr_resolve_eye(int eye, unsigned int texture, int width, int height);
/* D3DDevice_Present: the back buffer's picture is the HUD over the eyes
drawn this frame, or (no eyes drawn) the menus' and loading screens' flat
screen: into the panel's image, and the frame ends. Then the window: 1 if
the mirror (the left eye) was drawn into it, 0 to draw the back buffer as
always */
int vr_present(unsigned int texture, int width, int height);

/* ---------- the game side (vr_view.c) */

/* main_game_render: the player's window and the console's become the eyes',
the HUD's and the console's; the new count */
short vr_render_windows(struct render_window *windows, short window_count);
/* render_frame: around each window */
void vr_render_window_begin(short window_index);
void vr_render_window_end(short window_index);
/* render_player_frame: an eye's own, off-centre field (or the HUD panel's) */
void vr_render_frustum_bounds(union real_rectangle2d *bounds);
/* render_window: the first-person weapon posed from the head between the
eyes, once a frame */
void vr_render_weapon_camera(struct render_camera *camera);
/* render_window: the HUD's pass starts on a clear ground */
void vr_render_hud_begin(void);
/* a new map: the heading is taken again from the game's facing
(render_interpolation_reset) */
void vr_view_reset(void);
/* player_control_update: the head aims (the local player's facing), and
its turns count as looking */
void vr_player_control_facing(short local_player_index);
/* no aim assist under the head */
int vr_render_aiming(void);

#else

#define VR_RENDER_EYE() 0
#define VR_RENDER_HUD() 0
#define VR_RENDER_REPEAT() 0
#define vr_render_windows(windows, window_count) (window_count)
#define vr_render_window_begin(window_index) ((void)0)
#define vr_render_window_end(window_index) ((void)0)
#define vr_render_frustum_bounds(bounds) ((void)0)
#define vr_render_weapon_camera(camera) ((void)0)
#define vr_render_hud_begin() ((void)0)
#define vr_view_reset() ((void)0)
#define vr_player_control_facing(local_player_index) ((void)0)
#define vr_render_aiming() 0
#define vr_frame_begin() 0
#define vr_frame_finish() ((void)0)
#define vr_frame_seconds(seconds) 0

#endif

#endif
