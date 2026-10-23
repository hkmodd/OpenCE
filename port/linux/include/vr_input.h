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
	/* a driver steered by the off stick (vr.vehicle_steering "drive"): its
	sideways push turns the facing (vr_view.c), its forward push is the
	throttle, and the pad's left stick has no sideways (no strafing) */
	int drive;
	/* room scale (vr.roomscale), on foot: how far the head has stepped from
	where the body stands (LOCAL x and z, metres), which the body walks
	after while the off stick is still */
	int room;
	float room_offset[2];
	/* the weapon's barrel from the grip pose's pointing (vr.aim_pitch,
	radians, up positive), and whether the weapon in the hand is held as a
	pistol: the other hand supports it at the main hand, not ahead of it */
	float aim_pitch;
	int pistol_grip;
	/* a grenade is thrown with the arm (vr.throw_gesture): its button held
	takes it, let go with the hand moving fast throws it, let go slowly
	keeps it */
	int throw_gesture;
	/* the weapon is put away, the hands bare (vr_view.c): it neither fires
	nor zooms */
	int bare_hands;
	/* a weapon lies within each hand's reach (vr_view.c): the hand's grip
	pressed there takes it */
	int grab_reach[VR_HAND_COUNT];
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
	/* the main grip was pressed at a shoulder: weapons, not a melee; how
	many times, and the last's shoulder (VR_HAND_LEFT or VR_HAND_RIGHT, the
	head's sides) */
	int holster;
	unsigned long shoulder_grabs;
	int shoulder_side;
	/* each hand's grip took a weapon within its reach: the action held
	while it is held there, VR_GRAB_SECONDS at most, and for how long; the
	last hand to take one, and the seconds since (a weapon that comes to
	the player then goes to that hand) */
	int grabbing[VR_HAND_COUNT];
	float grab_seconds[VR_HAND_COUNT];
	int grab_hand;
	float grab_pending;
	/* the grenade's throw (vr.throw_gesture): each hand's last position and
	its speed of late (its fastest, falling by VR_THROW_FALL a second), the
	throw's press in seconds, and the throws and the last hand to throw */
	float throw_position[VR_HAND_COUNT][3];
	int throw_position_valid[VR_HAND_COUNT];
	float throw_speed[VR_HAND_COUNT];
	float throw_seconds;
	unsigned long throws;
	int throw_hand;
	/* the off grip took the weapon at the main hand: the weapon goes to it
	if the main hand pulls away from it while it holds on (a hand-over);
	for how long, and where both hands were as it took it */
	int handover;
	float handover_seconds;
	float handover_main[3], handover_off[3];
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

/* a weapon came to the player's hand (vr_view.c): one a hand's grip took
just now goes to that hand, which then aims */
void vr_input_weapon_taken(struct vr_input *input);

/* each hand's buttons' actions: the defaults (the table of vr_input.c), with
each item of `text` in place of its default ("main.lower=action
off.click=none": the hand, the button, the action, separated by spaces,
commas or semicolons); the count of items not understood, which are left
out */
int vr_input_bindings_parse(char const *text, unsigned char bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT]);

/* where a hand points a weapon: its grip pose (the OpenXR specification's,
the same for every controller: -z along the index finger held straight),
its barrel turned `pitch` up (radians) about the grip's x */
void vr_hand_pointing(struct vr_hand const *hand, float pitch, float orientation[4]);

/* the weapon held with both hands: the off hand's grip held away from the
head, at the main hand (VR_HELD_SUPPORT: the weapon points as the main hand
does) or, unless it is held as a pistol, ahead of it (VR_HELD_BOTH: -z from
the main hand to the off, up as the main hand's); the orientation the
weapon aims along then, or VR_HELD_ONE */
enum
{
	VR_HELD_ONE,
	VR_HELD_SUPPORT,
	VR_HELD_BOTH
};

int vr_two_handed(struct vr_frame const *frame, int left_handed, int pistol_grip, float pitch,
	float orientation[4]);

/* each hand's vibration (0 to 1) for the game's motors: the weapon's hand
feels both, the other the left (heavy: hits, blasts) and, while it holds
the weapon too, the right (light: the shots) */
void vr_rumble_hands(float left, float right, int left_handed, int both, float amplitude[VR_HAND_COUNT]);

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
/* vr_view.c: the weapon is held with both hands this frame (the rumble) */
void vr_input_weapon_hands(int both);
/* vr_view.c: the main grip's presses at a shoulder (vr.gestures), and the
last's shoulder (VR_HAND_LEFT or VR_HAND_RIGHT) */
unsigned long vr_input_shoulder_grabs(int *side);
/* vr_view.c: a weapon came to the player's hand (vr_input_weapon_taken) */
void vr_input_weapon_came(void);
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
/* vr_openxr.c: the hand that points while a menu is up (VR_HAND_*), for
its beam; -1 without one */
int vr_input_pointer_hand(void);
/* vr_openxr.c: a menu is up (as the input was last read) */
int vr_input_menus(void);

/* vr_openxr.c: the panel shown last (the HUD's or the flat screen's) in
LOCAL space, and its size in metres; 0 if none */
int vr_panel_pose(struct vr_pose *pose, float *width, float *height);
/* vr_openxr.c: a hand's vibration (0 to 1) for a time */
void vr_haptic(int hand, float amplitude, float seconds);

#endif

#endif
