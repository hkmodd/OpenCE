/*
VR_INPUT.H

The headset's controllers as the game's controller (display.vr,
port/linux/src/vr_input.c). Each frame their buttons, sticks and gestures
make an Xbox controller's state, merged into the first controller's
(xinput_sdl.c), so that the game, the menus and the player's chosen button
preset take them as they take a pad's; the main hand's stick turns the
heading (vr_view.c); and while a menu is up a hand points at its panel
(d3d8_gl.c's halo_ui_pointer_update). vr_input_update is a function of the
frame and of its own state alone, for the harness.
*/

#ifndef VR_INPUT_H
#define VR_INPUT_H

#include "halo_vr.h"

#ifndef HALO_GLES

/* the controller's buttons, as XInput's (port/include/xdk/xdk_xbox.h) */
enum
{
	VR_PAD_DPAD_UP = 0x0001,
	VR_PAD_DPAD_DOWN = 0x0002,
	VR_PAD_DPAD_LEFT = 0x0004,
	VR_PAD_DPAD_RIGHT = 0x0008,
	VR_PAD_START = 0x0010,
	VR_PAD_BACK = 0x0020,
	VR_PAD_LEFT_THUMB = 0x0040,
	VR_PAD_RIGHT_THUMB = 0x0080,
};

/* its analog buttons, as XInput's */
enum
{
	VR_PAD_A,
	VR_PAD_B,
	VR_PAD_X,
	VR_PAD_Y,
	VR_PAD_BLACK,
	VR_PAD_WHITE,
	VR_PAD_LEFT_TRIGGER,
	VR_PAD_RIGHT_TRIGGER,
	VR_PAD_ANALOG_COUNT
};

struct vr_pad
{
	unsigned short buttons;
	unsigned char analog[VR_PAD_ANALOG_COUNT];
	/* the left stick's x and y, then the right's, as XInput's (+y up) */
	short thumb[4];
};

/* what a controller's button does in the game (vr.bindings): an action of
the keys' (controls.<action>), as the standard preset's pad button for it */
enum
{
	VR_BIND_NONE,
	VR_BIND_FIRE,
	VR_BIND_THROW_GRENADE,
	VR_BIND_JUMP,
	VR_BIND_ACTION,
	VR_BIND_MELEE,
	VR_BIND_SWITCH_WEAPON,
	VR_BIND_SWITCH_GRENADE,
	VR_BIND_FLASHLIGHT,
	VR_BIND_CROUCH,
	VR_BIND_ZOOM,
	VR_BIND_SCOREBOARD,
	VR_BIND_COUNT
};

/* the buttons bound, of each hand: the trigger, the grip, the lower and
upper face buttons and the stick's click; and the hands, the one that aims
(main) and the other (off) */
enum
{
	VR_CONTROL_TRIGGER,
	VR_CONTROL_GRIP,
	VR_CONTROL_LOWER,
	VR_CONTROL_UPPER,
	VR_CONTROL_CLICK,
	VR_CONTROL_COUNT
};

enum
{
	VR_ROLE_MAIN,
	VR_ROLE_OFF,
	VR_ROLE_COUNT
};

struct vr_input_settings
{
	/* the left hand aims and fires, and the right moves (until the hands
	swap the weapon) */
	int left_handed;
	/* each hand's buttons' actions (vr_input_bindings_parse) */
	unsigned char bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT];
	/* the main hand's stick turns by this a push (radians), or, at 0,
	smoothly at turn_rate (radians a second) */
	float snap_angle;
	float turn_rate;
	/* the hands' and head's motions: a punch is a melee attack, the off
	hand's grip at the head its action (the flashlight), with the palms
	together it swaps the hands, the head lowered a crouch */
	int gestures;
	/* the head's yaw from the aiming hand's (radians to the left): the off
	stick moves where the head faces, the game's facing being the hand's */
	float move_turn;
	/* in a seat (vr.vehicle_aim "stick"): the main hand's stick is the
	pad's right stick, not a turn */
	int seat_stick;
	/* room scale (vr.roomscale), on foot: how far the head has stepped from
	where the body stands (LOCAL x and z, metres), which the body walks
	after while the off stick is still */
	int room;
	float room_offset[2];
};

struct vr_input
{
	/* the left hand aims: the setting's (as it was last read), until the
	hands swap the weapon; and how many times they have */
	int left_handed;
	int setting_left_handed;
	int left_handed_valid;
	unsigned long swaps;
	/* each hand's buttons in the last frame */
	unsigned long held[VR_HAND_COUNT];
	/* the menu button: how long it has been held, and whether that was long
	enough for Back; Start's press after a short one, in seconds left */
	float menu_seconds;
	int menu_long;
	float start_seconds;
	/* the stick pushed far enough to snap, not yet let back */
	int snapped;
	/* this frame's turn, radians to the left */
	float turn;
	/* the main hand's last position (a punch's speed), the melee's press
	and the time until another, the press of the off grip's action at the
	head, in seconds */
	float main_position[3];
	int main_position_valid;
	float melee_seconds, melee_wait, tap_seconds;
	/* the main grip was pressed at a shoulder: weapons, not a melee */
	int holster;
	/* the head's height standing (the highest since the runtime last
	recentred), and the crouch it makes */
	float standing;
	int standing_valid;
	unsigned long recentres;
	int crouched;
	/* the menus: the hand that points, and its trigger's clicks since the
	menus last read them */
	int pointer_hand;
	int clicks;
	int menus;
	/* the body walks after the head this frame (room scale) */
	int room_walking;
};

/* the pad of this frame's controllers; `menus` while a menu is up, and the
seconds since the last frame */
void vr_input_update(struct vr_input *input, struct vr_frame const *frame, struct vr_input_settings const *settings,
	int menus, float seconds, struct vr_pad *pad);

/* each hand's buttons' actions: the defaults (the table of vr_input.c), with
each item of `text` in place of its default ("main.lower=action
off.click=none": the hand, the button, the action, separated by spaces,
commas or semicolons); the count of items not understood, which are left
out */
int vr_input_bindings_parse(char const *text, unsigned char bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT]);

/* the weapon held with both hands: the off hand's grip held near the main
hand, ahead of it and away from the head; the orientation (-z from the main
hand to the off, up as the main hand's) the weapon aims along then */
int vr_two_handed(struct vr_frame const *frame, int left_handed, float orientation[4]);

/* where a hand's ray (its aim pose, -z along the ray) meets a panel (width
by height metres, centred on `panel`, its face toward +z), as fractions of
the panel from its top left: 1 if it does */
int vr_ray_panel(struct vr_pose const *aim, struct vr_pose const *panel, float width, float height, float *u,
	float *v);

/* ---------- the game's (vr_input.c's state) */

/* xinput_sdl.c: the controllers' pad for the first controller, once a
frame; 0 (pad untouched) while the session is not running or has no
controllers */
int vr_input_gamepad(struct vr_pad *pad, int menus);
/* xinput_sdl.c's XInputSetState: the first controller's rumble, each
motor's speed 0 to 1, felt in the hand on its side */
void vr_input_rumble(float left, float right);
/* vr_view.c: the left hand aims (vr.left_handed, or the hands swapped) */
int vr_input_left_handed(void);
/* vr_view.c: the turn the stick made this frame (radians to the left),
taken */
float vr_input_turn(void);
/* vr_view.c: the body walked after the head this frame (room scale) */
int vr_input_room_walking(void);
/* vr_view.c: the head's yaw from the aiming hand's (radians to the left), 0
while the head aims */
void vr_input_aim_turn(float radians);
/* d3d8_gl.c: where the pointing hand meets the panel shown, as fractions
from its top left, and the trigger's clicks since the last call (taken); 0
if it points elsewhere */
int vr_input_pointer(float *u, float *v, int *clicks);
/* vr_openxr.c: the same point, not taken, for the dot drawn on the panel */
int vr_input_pointer_shown(float *u, float *v);
/* vr_openxr.c: a menu is up (as the input was last read) */
int vr_input_menus(void);

/* vr_openxr.c: the panel shown last (the HUD's or the flat screen's) in
LOCAL space, and its size in metres; 0 if none */
int vr_panel_pose(struct vr_pose *pose, float *width, float *height);
/* vr_openxr.c: a hand's vibration (0 to 1) for a time */
void vr_haptic(int hand, float amplitude, float seconds);

#endif

#endif
