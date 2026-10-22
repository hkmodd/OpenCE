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
struct icon_hud_element_definition;

/* a pass of the headset's frame (vr_view.c) */
enum
{
	_vr_render_pass_none,
	_vr_render_pass_left_eye,
	_vr_render_pass_right_eye,
	_vr_render_pass_hud,
	/* the scope's view, while the aiming hand's weapon is zoomed */
	_vr_render_pass_scope,
};

/* the pass being drawn, _vr_render_pass_none outside the headset's windows */
extern int vr_render_pass;

#define VR_RENDER_EYE() (vr_render_pass == _vr_render_pass_left_eye || vr_render_pass == _vr_render_pass_right_eye)
#define VR_RENDER_HUD() (vr_render_pass == _vr_render_pass_hud)
#define VR_RENDER_SCOPE() (vr_render_pass == _vr_render_pass_scope)
/* a pass of the world without the HUD: an eye's or the scope's */
#define VR_RENDER_WORLD() (VR_RENDER_EYE() || VR_RENDER_SCOPE())
/* a pass after the frame's first, which must not move what changes once a
frame on again (the sky's animation, weather, glows) */
#define VR_RENDER_REPEAT() (vr_render_pass != _vr_render_pass_none && vr_render_pass != _vr_render_pass_left_eye)

/* a hand's buttons (vr_hand.buttons), the same on every controller: the
lower and upper face buttons (Touch's A and B, or X and Y; Index's A and
B), the stick's (or a trackpad's) click, the menu button, and the trigger
and grip pressed past their thresholds */
enum
{
	VR_BUTTON_LOWER = 1 << 0,
	VR_BUTTON_UPPER = 1 << 1,
	VR_BUTTON_STICK = 1 << 2,
	VR_BUTTON_MENU = 1 << 3,
	VR_BUTTON_TRIGGER = 1 << 4,
	VR_BUTTON_GRIP = 1 << 5,
};

enum
{
	VR_HAND_LEFT,
	VR_HAND_RIGHT,
	VR_HAND_COUNT
};

/* a controller in the frame (vr_frame.hand) */
struct vr_hand
{
	/* the poses are tracked (else they are the last tracked) */
	int tracked;
	/* where the hand holds it, and its pointing ray (OpenXR's grip and aim
	poses: -z along the ray) */
	struct vr_pose grip, aim;
	float trigger, squeeze;
	/* the stick (or trackpad): +x right, +y forward, -1 to 1 */
	float stick[2];
	unsigned long buttons;
};

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
	/* the controllers, for the frame's display time; `controllers` is set
	once the runtime has given any of their input */
	int controllers;
	struct vr_hand hand[VR_HAND_COUNT];
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
/* the scope's window has ended: the picture in the back buffer's rectangle
(x0, y0 to x1, y1 in its pixels, rows from the top) into the scope's image */
void vr_resolve_scope(unsigned int texture, int width, int height, int x0, int y0, int x1, int y1);
/* where the scope's image is held this frame (LOCAL space, facing +z) and
its width in metres */
void vr_scope_place(const struct vr_pose *pose, float width);
/* the aiming reticle this frame: where the shot lands (LOCAL space), red
over a target the game would aim at */
void vr_reticle_place(const float position[3], int target);

/* ---------- the game side (vr_view.c) */

/* main_game_render: the player's window and the console's become the eyes',
the HUD's and the console's; the new count */
short vr_render_windows(struct render_window *windows, short window_count);
/* render_frame: around each window */
void vr_render_window_begin(short window_index);
void vr_render_window_end(short window_index);
/* render_player_frame: an eye's own, off-centre field (or the HUD panel's) */
void vr_render_frustum_bounds(union real_rectangle2d *bounds);
/* render_window: the first-person weapon posed once a frame, from a camera
behind the aiming hand (or the head between the eyes) */
void vr_render_weapon_camera(struct render_camera *camera);
/* render_window: the weapon posed, where its model's barrel is; held in
the left hand, its model mirrored */
void vr_render_weapon_posed(void);
/* the first-person weapon's drawing begins (TRUE) or ends: a mirrored one's
triangles' winding is turned over (viewmodel_projection_begin, view_fov.c) */
void vr_render_weapon_drawing(int drawing);
/* render_window: the HUD's pass starts on a clear ground */
void vr_render_hud_begin(void);
/* a new map: the heading is taken again from the game's facing
(render_interpolation_reset) */
void vr_view_reset(void);
/* player_control_update: the head aims (the local player's facing), and
its turns count as looking */
void vr_player_control_facing(short local_player_index);
/* no aim assist under the head or the hand */
int vr_render_aiming(void);
/* the hand aims: the HUD draws no crosshair, a reticle is held where its
shot lands (passenger_camera.c) */
int vr_render_hand_aiming(void);
/* first_person_weapons.c: the unit's weapon is in the aiming hand: it
neither bobs, sways nor is posed */
int vr_render_hand_holds(long unit_index);
/* unit_adjust_projectile_ray: with no other machine in the game, the shot
of the unit the hand aims for starts at the weapon in the hand (its
muzzle), unless a wall stands between it and the unit's eye; 0 to fire as
always */
int vr_render_shot_origin(long unit_index, float origin[3]);
/* weapons.c: that unit's shots start at the hand: the first-person
weapon's offset from the eye is not added to them */
int vr_render_shot_from_hand(long unit_index);
/* hud_messaging.c: a prompt's button (one of the HUD's button icons) as the
headset's controllers name it (vr_input.c's buttons): the right B acts, the
left X switches grenades */
struct icon_hud_element_definition const *vr_render_prompt_icon(struct icon_hud_element_definition const *icon);
/* hud_weapon.c: the zoomed view is the scope's: the HUD's zoom-only
elements are not drawn */
int vr_render_zoom_hidden(void);

#else

#define VR_RENDER_EYE() 0
#define VR_RENDER_HUD() 0
#define VR_RENDER_REPEAT() 0
#define VR_RENDER_SCOPE() 0
#define VR_RENDER_WORLD() 0
#define vr_render_windows(windows, window_count) (window_count)
#define vr_render_window_begin(window_index) ((void)0)
#define vr_render_window_end(window_index) ((void)0)
#define vr_render_frustum_bounds(bounds) ((void)0)
#define vr_render_weapon_camera(camera) ((void)0)
#define vr_render_weapon_posed() ((void)0)
#define vr_render_weapon_drawing(drawing) ((void)0)
#define vr_render_hud_begin() ((void)0)
#define vr_view_reset() ((void)0)
#define vr_player_control_facing(local_player_index) ((void)0)
#define vr_render_aiming() 0
#define vr_render_hand_aiming() 0
#define vr_render_hand_holds(unit_index) 0
#define vr_render_shot_origin(unit_index, origin) 0
#define vr_render_shot_from_hand(unit_index) 0
#define vr_render_prompt_icon(icon) (icon)
#define vr_render_zoom_hidden() 0
#define vr_frame_begin() 0
#define vr_frame_finish() ((void)0)
#define vr_frame_seconds(seconds) 0

#endif

#endif
