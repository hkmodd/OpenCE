/*
VR_INPUT.C

The headset's controllers as the game's controller (display.vr,
port/linux/include/vr_input.h).

The buttons. In the game, for the right-handed (vr.left_handed swaps the
hands), unless vr.bindings binds them otherwise:
	right trigger    fire                left trigger     grenade
	right A          jump                left X           switch grenades
	right B          action, reload      left Y           switch weapons
	right grip       melee               left grip        flashlight
	right stick      turn                left stick       move
	right click      zoom                left click       crouch
	menu             pause (Start); held a second, the scores (Back)
each action pressing the Xbox button the game's standard preset gives it,
so the player's own preset applies to them as to a pad. In the menus either hand's
lower face button is A and its upper is B, either stick moves about them,
and a hand points at the panel and its trigger clicks (the hand whose
trigger was pressed last points). Index's A and B and Vive's and Windows
Mixed Reality's trackpads stand in for the face buttons
(vr_openxr.c's bindings).

The gestures (vr.gestures). The main hand punched forward, faster than
VR_PUNCH_SPEED, is a melee attack; its grip pressed at a shoulder (a
holster) puts the weapon away there or draws the one there (vr_view.c), not
its action; either hand's grip pressed with a weapon on the ground within
its reach (vr_view.c) holds the action, which takes it (VR_GRAB_SECONDS at
most), and the weapon that comes goes to that hand; the off hand's grip does its action (the
flashlight) only at the head; taking the weapon at the main hand, it is
handed over if the main hand then pulls away while the off holds on (the
hands swap: after astromaddie's VR port), and stays with the main hand if
both stay together (a pistol held in both hands); near the weapon the off
grip holds it with both hands (vr_two_handed: at the main hand it supports
it, ahead of it, but for a pistol, the weapon then points from the main
hand to the off); and the head lowered
VR_CROUCH_DEPTH below its standing height (the highest since the runtime
last recentred) crouches.

The hands as they hold (vr.hands "physical", with the gestures). The
main hand holds the weapon by its grip (one that came otherwise, at a new
life or a switch, until its grip is pressed and let go). Let go, it lets go
of it: to the other hand if that holds it too (vr_two_handed: a hand-over),
else to the shoulder it is let go at, else back to the shoulder that keeps
it (vr_view.c's; the hands then bare). An empty hand's grip pressed at a
shoulder that keeps a weapon draws it into that hand (the other's going to
its shoulder), at the head is the flashlight, at a weapon on the ground
takes it. A trigger fires in the hand that holds the weapon, holds a
grenade in an empty one (thrown as below), and does nothing in one that
holds the weapon too; the grips strike nothing, a punch of either hand
does. With vr.grip_toggle a press of the grip holds, another lets go.

The throw (vr.throw_gesture). The grenade's button (the off trigger) held
takes a grenade and presses nothing; let go with the hand moving faster
than VR_THROW_SPEED it throws it (the game's throw: where the aim points),
let go slowly it keeps it. The menus' clicks throw nothing.

The turn. The main hand's stick turns the heading (vr_view.c) by
vr.snap_turn degrees a push, or smoothly at vr.smooth_turn_speed with
snap_turn 0. In the game the stick's turns are the heading's alone: the
pad's right stick stays still, so the game's own look speed does not apply.
*/

#ifndef HALO_GLES

#include "vr_input.h"

#include <math.h>
#include <string.h>

#ifndef VR_INPUT_HARNESS
#include "port_config.h"
#endif

/* how long a press made from a short action (a click of the menu button, a
punch, a tap at the head) is held, so that a game tick sees it */
#define VR_PRESS_SECONDS 0.1f
/* the menu button held this long is Back */
#define VR_MENU_LONG_SECONDS 1.0f
/* a stick pushed past this snaps a turn, and must come back within the
second before the next */
#define VR_SNAP_PUSH 0.7f
#define VR_SNAP_RELEASE 0.3f
/* the smooth turn's dead zone */
#define VR_TURN_DEAD_ZONE 0.15f
/* a punch: the main hand's speed (metres a second), mostly along the
head's forward, and the time before another */
#define VR_PUNCH_SPEED 2.5f
#define VR_PUNCH_ALONG 0.5f
#define VR_PUNCH_WAIT 0.5f
/* the off hand's grip within this of the head (metres) is the flashlight */
#define VR_HEAD_REACH 0.25f
/* a shoulder (a holster): from the head, to the side, down and back, along
the head's yaw, and how near the main hand's grip must be */
#define VR_SHOULDER_SIDE 0.18f
#define VR_SHOULDER_DOWN 0.25f
#define VR_SHOULDER_BACK 0.05f
#define VR_SHOULDER_REACH 0.2f
/* the off hand holds the weapon within this of the main hand */
#define VR_TWO_HANDED_REACH 0.6f
/* room scale: the head this far from the body (metres) starts it walking,
this near stops it, and this far walks it at full speed; the off stick
pushed less than this is still */
#define VR_ROOM_START 0.10f
#define VR_ROOM_STOP 0.03f
#define VR_ROOM_FULL 0.50f
#define VR_ROOM_STILL 0.15f
/* the off grip pressed this near the main hand (their grips, metres) takes
the weapon there; it is handed over if, within VR_HANDOVER_SECONDS, the
hands are VR_HANDOVER_APART apart, the main hand having moved more */
#define VR_SWAP_REACH 0.15f
#define VR_HANDOVER_APART 0.25f
#define VR_HANDOVER_SECONDS 1.5f
/* a grenade's throw: the hand's speed (metres a second) as its button is
let go, its fastest of late, which falls by VR_THROW_FALL a second (a
throw's hand slows as it lets go) */
#define VR_THROW_SPEED 1.5f
#define VR_THROW_FALL 8.0f
/* a hand's velocity, for the weapon it lets fall (a VR server's rules):
its frames' over this long (seconds) */
#define VR_DROP_SMOOTH 0.05f
/* a hand's grip takes a weapon within its reach by holding the action this
long at most (seconds: long enough for the game's swap) */
#define VR_GRAB_SECONDS 0.6f
/* the head this far below standing crouches, and back above this rises */
#define VR_CROUCH_DEPTH 0.30f
#define VR_CROUCH_RISE 0.25f

static char const *const vr_binding_names[VR_BIND_COUNT] =
{
	"none", "fire", "throw_grenade", "jump", "action", "melee", "switch_weapon", "switch_grenade", "flashlight",
	"crouch", "zoom", "scoreboard"
};
static char const *const vr_control_names[VR_CONTROL_COUNT] = { "trigger", "grip", "lower", "upper", "click" };
static char const *const vr_role_names[VR_ROLE_COUNT] = { "main", "off" };
static unsigned char const vr_default_bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT] =
{
	{ VR_BIND_FIRE, VR_BIND_MELEE, VR_BIND_JUMP, VR_BIND_ACTION, VR_BIND_ZOOM },
	{ VR_BIND_THROW_GRENADE, VR_BIND_FLASHLIGHT, VR_BIND_SWITCH_GRENADE, VR_BIND_SWITCH_WEAPON, VR_BIND_CROUCH }
};

/* the pad's button each action presses (the standard preset's): an analog
button, or with VR_PAD_DIGITAL a digital one */
#define VR_PAD_DIGITAL 0x100
static unsigned short const vr_binding_pad[VR_BIND_COUNT] =
{
	0, VR_PAD_RIGHT_TRIGGER, VR_PAD_LEFT_TRIGGER, VR_PAD_A, VR_PAD_X, VR_PAD_B, VR_PAD_Y, VR_PAD_BLACK, VR_PAD_WHITE,
	VR_PAD_DIGITAL | VR_PAD_LEFT_THUMB, VR_PAD_DIGITAL | VR_PAD_RIGHT_THUMB, VR_PAD_DIGITAL | VR_PAD_BACK
};

/* the name of `names` that is the text from `at` to `end`, or -1 */
static int vr_name_at(char const *at, char const *end, char const *const *names, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		size_t length = strlen(names[index]);

		if ((size_t)(end - at) == length && !strncmp(at, names[index], length))
			return index;
	}
	return -1;
}

int vr_input_bindings_parse(char const *text, unsigned char bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT])
{
	int bad = 0;

	memcpy(bindings, vr_default_bindings, sizeof(vr_default_bindings));
	while (text && *text)
	{
		char const *item, *end, *dot, *equals;
		int role, control, action;

		while (*text == ' ' || *text == ',' || *text == ';' || *text == '\t')
			text++;
		if (!*text)
			break;
		item = text;
		while (*text && *text != ' ' && *text != ',' && *text != ';' && *text != '\t')
			text++;
		end = text;
		for (dot = item; dot < end && *dot != '.'; dot++)
			;
		for (equals = dot; equals < end && *equals != '='; equals++)
			;
		role = dot < end ? vr_name_at(item, dot, vr_role_names, VR_ROLE_COUNT) : -1;
		control = equals < end ? vr_name_at(dot + 1, equals, vr_control_names, VR_CONTROL_COUNT) : -1;
		action = equals < end ? vr_name_at(equals + 1, end, vr_binding_names, VR_BIND_COUNT) : -1;
		if (role < 0 || control < 0 || action < 0)
		{
			bad++;
			continue;
		}
		bindings[role][control] = (unsigned char)action;
	}
	return bad;
}

/* an action pressed, by `value` (0 to 1: a trigger's pull for an analog
button) */
static void vr_bind(struct vr_pad *pad, int action, float value)
{
	unsigned short target;
	unsigned char amount;

	if (action <= VR_BIND_NONE || action >= VR_BIND_COUNT || !(value > 0.0f))
		return;
	target = vr_binding_pad[action];
	if (target & VR_PAD_DIGITAL)
	{
		pad->buttons |= (unsigned short)(target & ~VR_PAD_DIGITAL);
		return;
	}
	amount = (unsigned char)((value > 1.0f ? 1.0f : value) * 255.0f + 0.5f);
	if (amount > pad->analog[target])
		pad->analog[target] = amount;
}

/* a hand's bound buttons pressed: the trigger by its pull when it fires or
throws, otherwise each pressed or not; `grip` is the grip's action (the
gestures may change it) */
static void vr_bind_hand(struct vr_pad *pad, struct vr_hand const *hand, unsigned char const bindings[VR_CONTROL_COUNT],
	int grip)
{
	int trigger = bindings[VR_CONTROL_TRIGGER];

	vr_bind(pad, trigger, trigger == VR_BIND_FIRE || trigger == VR_BIND_THROW_GRENADE ? hand->trigger :
		(hand->buttons & VR_BUTTON_TRIGGER) ? 1.0f : 0.0f);
	vr_bind(pad, grip, (hand->buttons & VR_BUTTON_GRIP) ? 1.0f : 0.0f);
	vr_bind(pad, bindings[VR_CONTROL_LOWER], (hand->buttons & VR_BUTTON_LOWER) ? 1.0f : 0.0f);
	vr_bind(pad, bindings[VR_CONTROL_UPPER], (hand->buttons & VR_BUTTON_UPPER) ? 1.0f : 0.0f);
	vr_bind(pad, bindings[VR_CONTROL_CLICK], (hand->buttons & VR_BUTTON_STICK) ? 1.0f : 0.0f);
}

static short vr_thumb(float value)
{
	if (value > 1.0f)
		value = 1.0f;
	else if (value < -1.0f)
		value = -1.0f;
	return (short)(value * 32767.0f);
}

/* the stick of larger push */
static void vr_merge_stick(short thumb[2], float const stick[2])
{
	short x = vr_thumb(stick[0]), y = vr_thumb(stick[1]);

	if ((long)x * x + (long)y * y > (long)thumb[0] * thumb[0] + (long)thumb[1] * thumb[1])
	{
		thumb[0] = x;
		thumb[1] = y;
	}
}

static void vr_press(struct vr_pad *pad, int analog, int down)
{
	if (down)
		pad->analog[analog] = 255;
}

static float vr_distance(float const a[3], float const b[3])
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];

	return sqrtf(x * x + y * y + z * z);
}

/* the turn of the main hand's stick this frame */
static float vr_stick_turn(struct vr_input *input, struct vr_input_settings const *settings, float x, float seconds)
{
	float push = fabsf(x);

	if (settings->snap_angle > 0.0f)
	{
		if (input->snapped)
		{
			if (push < VR_SNAP_RELEASE)
				input->snapped = 0;
			return 0.0f;
		}
		if (push < VR_SNAP_PUSH)
			return 0.0f;
		input->snapped = 1;
		/* (a push to the right turns to the right: the yaw grows to the left) */
		return x > 0.0f ? -settings->snap_angle : settings->snap_angle;
	}
	input->snapped = 0;
	if (push <= VR_TURN_DEAD_ZONE)
		return 0.0f;
	push = (push - VR_TURN_DEAD_ZONE) / (1.0f - VR_TURN_DEAD_ZONE);
	return (x > 0.0f ? -push : push) * settings->turn_rate * seconds;
}

/* room scale: the head stepped `room` (LOCAL x and z) from where the body
stands; whether the body walks after it, and if so `move`, the push toward
it in the head's axes, as the off stick's (right, ahead) */
static int vr_room_walk(struct vr_input const *input, struct vr_frame const *frame, float const room[2],
	float move[2])
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float forward[3], length, push;
	float distance = sqrtf(room[0] * room[0] + room[1] * room[1]);

	if (!(distance > (input->room_walking ? VR_ROOM_STOP : VR_ROOM_START)))
		return 0;
	vr_rotate(frame->head.orientation, local_forward, forward);
	length = sqrtf(forward[0] * forward[0] + forward[2] * forward[2]);
	if (!(length > 1e-3f))
		return 0;
	push = (distance - VR_ROOM_STOP) / VR_ROOM_FULL;
	if (push > 1.0f)
		push = 1.0f;
	push /= distance * length;
	/* (the head's right is its forward turned a quarter to the right) */
	move[0] = (-room[0] * forward[2] + room[1] * forward[0]) * push;
	move[1] = (room[0] * forward[0] + room[1] * forward[2]) * push;
	return 1;
}

/* the hand punched forward this frame (its last position and whether it
is known: `position`, `valid`) */
static int vr_punched(struct vr_input *input, struct vr_frame const *frame, struct vr_hand const *main_hand,
	float position[3], int *valid, float seconds)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float velocity[3], forward[3], speed, along;
	int punched = 0, axis;

	if (!main_hand->tracked)
	{
		*valid = 0;
		return 0;
	}
	if (*valid && seconds > 0.0f)
	{
		for (axis = 0; axis < 3; axis++)
			velocity[axis] = (main_hand->grip.position[axis] - position[axis]) / seconds;
		speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
		vr_rotate(frame->head.orientation, local_forward, forward);
		along = velocity[0] * forward[0] + velocity[1] * forward[1] + velocity[2] * forward[2];
		punched = speed > VR_PUNCH_SPEED && along > VR_PUNCH_ALONG * speed && input->melee_wait <= 0.0f;
	}
	memcpy(position, main_hand->grip.position, sizeof(float) * 3);
	*valid = 1;
	return punched;
}

/* the hand let go of its grenade's button (`bindings`' throw_grenade) while
it moved fast: a throw (vr.throw_gesture); untracked, as a fast swing may
leave it, its speed of late stands */
static int vr_thrown(struct vr_input *input, struct vr_hand const *hand, int index,
	unsigned char const bindings[VR_CONTROL_COUNT], float seconds)
{
	static unsigned long const control_buttons[VR_CONTROL_COUNT] =
		{ VR_BUTTON_TRIGGER, VR_BUTTON_GRIP, VR_BUTTON_LOWER, VR_BUTTON_UPPER, VR_BUTTON_STICK };
	float speed = 0.0f;
	int held = 0, was_held = 0, control, axis;

	for (control = 0; control < VR_CONTROL_COUNT; control++)
	{
		if (bindings[control] == VR_BIND_THROW_GRENADE)
		{
			held |= (hand->buttons & control_buttons[control]) != 0;
			was_held |= (input->held[index] & control_buttons[control]) != 0;
		}
	}
	if (hand->tracked && input->throw_position_valid[index] && seconds > 0.0f)
	{
		float velocity[3];

		for (axis = 0; axis < 3; axis++)
			velocity[axis] = (hand->grip.position[axis] - input->throw_position[index][axis]) / seconds;
		speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
	}
	memcpy(input->throw_position[index], hand->grip.position, sizeof(input->throw_position[index]));
	input->throw_position_valid[index] = hand->tracked;
	input->throw_speed[index] -= VR_THROW_FALL * seconds;
	if (input->throw_speed[index] < speed)
		input->throw_speed[index] = speed;
	return was_held && !held && input->throw_speed[index] > VR_THROW_SPEED;
}

/* the shoulder the hand is at, about the head's yaw: VR_HAND_LEFT or
VR_HAND_RIGHT (the head's sides), or -1 */
static int vr_at_shoulder(struct vr_frame const *frame, struct vr_hand const *hand)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float forward[3], length, side;
	int shoulder;

	if (!hand->tracked)
		return -1;
	vr_rotate(frame->head.orientation, local_forward, forward);
	length = sqrtf(forward[0] * forward[0] + forward[2] * forward[2]);
	if (length < 1e-3f)
		return -1;
	forward[0] /= length;
	forward[2] /= length;
	for (shoulder = 0, side = -VR_SHOULDER_SIDE; shoulder < 2; shoulder++, side = VR_SHOULDER_SIDE)
	{
		float place[3];

		/* (right of the head's forward (x, z) is (-z, x)) */
		place[0] = frame->head.position[0] - forward[2] * side - forward[0] * VR_SHOULDER_BACK;
		place[1] = frame->head.position[1] - VR_SHOULDER_DOWN;
		place[2] = frame->head.position[2] + forward[0] * side - forward[2] * VR_SHOULDER_BACK;
		if (vr_distance(hand->grip.position, place) < VR_SHOULDER_REACH)
			return shoulder ? VR_HAND_RIGHT : VR_HAND_LEFT;
	}
	return -1;
}

/* a grip's doing at a shoulder (VR_SHOULDER_*), for vr_view.c */
static void vr_shoulder(struct vr_input *input, int kind, int side)
{
	input->shoulder_kind = kind;
	input->shoulder_side = side;
	input->shoulder_grabs++;
}

/* each hand's velocity of late, from its places frame to frame, smoothed
over VR_DROP_SMOOTH seconds (none while it is not tracked) */
static void vr_hand_velocities(struct vr_input *input, struct vr_frame const *frame, float seconds)
{
	int hand, axis;
	float blend = seconds < VR_DROP_SMOOTH ? seconds / VR_DROP_SMOOTH : 1.0f;

	for (hand = 0; hand < VR_HAND_COUNT; hand++)
	{
		struct vr_hand const *state = &frame->hand[hand];

		for (axis = 0; axis < 3; axis++)
		{
			float now = state->tracked && input->hand_place_valid[hand] && seconds > 0.0f ?
				(state->grip.position[axis] - input->hand_place[hand][axis]) / seconds : 0.0f;

			input->hand_velocity[hand][axis] += (now - input->hand_velocity[hand][axis]) * blend;
		}
		memcpy(input->hand_place[hand], state->grip.position, sizeof(input->hand_place[hand]));
		input->hand_place_valid[hand] = state->tracked;
	}
}

/* the hands swap: the other aims and holds the weapon */
static void vr_swap_hands(struct vr_input *input)
{
	input->left_handed = !input->left_handed;
	input->swaps++;
	input->main_position_valid = 0;
	input->off_position_valid = 0;
	input->holster = 0;
	input->handover = 0;
}

static void vr_grab(struct vr_input *input, int hand);

/* the hands as they hold (vr.hands "physical"): what this frame's grips
pressed and let go do (the main hand may change) */
static void vr_physical_hands(struct vr_input *input, struct vr_frame const *frame,
	struct vr_input_settings const *settings)
{
	int main_index = input->left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT, hand, side;
	unsigned long pressed[VR_HAND_COUNT], released[VR_HAND_COUNT];
	float orientation[4];

	for (hand = 0; hand < VR_HAND_COUNT; hand++)
	{
		pressed[hand] = frame->hand[hand].buttons & ~input->held[hand] & VR_BUTTON_GRIP;
		released[hand] = input->held[hand] & ~frame->hand[hand].buttons & VR_BUTTON_GRIP;
	}
	input->supporting = input->holding &&
		vr_two_handed(frame, input->left_handed, settings->pistol_grip, settings->aim_pitch, orientation) != VR_HELD_ONE;
	/* (the weapon let go: to the other hand holding it too, to the shoulder
	it is let go at, else falling by a VR server's rules, or back to its own) */
	if (input->holding && input->gripped && (settings->grip_toggle ? pressed[main_index] : released[main_index]))
	{
		pressed[main_index] = 0;
		if (input->supporting)
		{
			vr_swap_hands(input);
			input->supporting = 0;
			return;
		}
		side = vr_at_shoulder(frame, &frame->hand[main_index]);
		if (side < 0 && settings->drop)
		{
			memcpy(input->drop_velocity, input->hand_velocity[main_index], sizeof(input->drop_velocity));
			vr_shoulder(input, VR_SHOULDER_DROP, -1);
		}
		else
		{
			vr_shoulder(input, VR_SHOULDER_PUT, side);
		}
		input->holding = input->gripped = 0;
	}
	for (hand = 0; hand < VR_HAND_COUNT; hand++)
	{
		struct vr_hand const *state = &frame->hand[hand];

		if (!pressed[hand] || !state->tracked)
			continue;
		/* (the weapon's hand takes hold of it; the other draws, taps or takes
		before it holds the weapon too) */
		if (input->holding && hand == main_index)
		{
			input->gripped = 1;
			continue;
		}
		side = vr_at_shoulder(frame, state);
		if (side >= 0 && settings->shoulder_full[side])
		{
			vr_shoulder(input, VR_SHOULDER_DRAW, side);
			if (hand != main_index)
				vr_swap_hands(input);
			input->holding = input->gripped = 1;
			return;
		}
		if (vr_distance(state->grip.position, frame->head.position) < VR_HEAD_REACH)
			input->tap_seconds = VR_PRESS_SECONDS;
		else if (settings->grab_reach[hand])
			vr_grab(input, hand);
	}
}

/* the hand's grip takes the weapon within its reach: the action held (to
VR_GRAB_SECONDS), and the weapon that comes goes to the hand
(vr_input_weapon_taken) */
static void vr_grab(struct vr_input *input, int hand)
{
	input->grabbing[hand] = 1;
	input->grab_seconds[hand] = 0.0f;
	input->grab_hand = hand;
	input->grab_pending = VR_GRAB_SECONDS + 0.5f;
}

/* the head lowered from standing */
static int vr_crouching(struct vr_input *input, struct vr_frame const *frame)
{
	float height = frame->head.position[1];

	if (!input->standing_valid || input->recentres != frame->recentres)
	{
		input->standing = height;
		input->standing_valid = 1;
		input->recentres = frame->recentres;
		input->crouched = 0;
	}
	if (height > input->standing)
		input->standing = height;
	if (input->crouched)
		input->crouched = height < input->standing - VR_CROUCH_RISE;
	else
		input->crouched = height < input->standing - VR_CROUCH_DEPTH;
	return input->crouched;
}

void vr_input_update(struct vr_input *input, struct vr_frame const *frame, struct vr_input_settings const *settings,
	int menus, float seconds, struct vr_pad *pad)
{
	int main_index, off_index, menu_held, hand;
	struct vr_hand const *main_hand, *off_hand;
	unsigned long off_pressed, main_pressed;
	unsigned char bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT], gesture_bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT];
	int role, control;

	/* (the setting's hand, from when it changes; until then the hands may
	have swapped) */
	if (!input->left_handed_valid || input->setting_left_handed != settings->left_handed)
	{
		input->left_handed = settings->left_handed != 0;
		input->setting_left_handed = settings->left_handed;
		input->left_handed_valid = 1;
	}
	/* (in a seat, dead or in the menus before play, the hands hold as the
	game's; back on foot, the weapon is held, not by its grip) */
	input->physical = settings->physical && settings->gestures && settings->on_foot;
	if (!input->physical)
	{
		input->holding = settings->physical && settings->gestures;
		input->gripped = input->supporting = 0;
	}
	vr_hand_velocities(input, frame, menus ? 0.0f : seconds);
	if (input->physical && !menus)
		vr_physical_hands(input, frame, settings);
	main_index = input->left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT;
	off_index = main_index == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT;
	main_hand = &frame->hand[main_index];
	off_hand = &frame->hand[off_index];
	off_pressed = off_hand->buttons & ~input->held[off_index];
	main_pressed = main_hand->buttons & ~input->held[main_index];
	memset(pad, 0, sizeof(*pad));
	input->turn = 0.0f;
	if (!(seconds > 0.0f && seconds < 1.0f))
		seconds = 0.0f;
	if (input->melee_wait > 0.0f)
		input->melee_wait -= seconds;
	if (input->grab_pending > 0.0f)
		input->grab_pending -= seconds;

	/* the menu button: Start for a short press, Back held after a long one */
	menu_held = ((main_hand->buttons | off_hand->buttons) & VR_BUTTON_MENU) != 0;
	if (menu_held)
	{
		input->menu_seconds += seconds;
		if (input->menu_seconds >= VR_MENU_LONG_SECONDS)
			input->menu_long = 1;
	}
	else
	{
		if (input->menu_seconds > 0.0f && !input->menu_long)
			input->start_seconds = VR_PRESS_SECONDS;
		input->menu_seconds = 0.0f;
		input->menu_long = 0;
	}
	if (input->start_seconds > 0.0f)
	{
		pad->buttons |= VR_PAD_START;
		input->start_seconds -= seconds;
	}
	if (menu_held && input->menu_long)
		pad->buttons |= VR_PAD_BACK;

	if (menus)
	{
		/* (the main hand points as a menu opens; another hand's trigger
		makes it the pointer, without a click) */
		if (!input->menus)
		{
			input->pointer_hand = main_index;
			input->clicks = 0;
		}
		for (hand = 0; hand < VR_HAND_COUNT; hand++)
		{
			struct vr_hand const *state = &frame->hand[hand];
			unsigned long pressed = state->buttons & ~input->held[hand];

			vr_press(pad, VR_PAD_A, (state->buttons & VR_BUTTON_LOWER) != 0);
			vr_press(pad, VR_PAD_B, (state->buttons & VR_BUTTON_UPPER) != 0);
			vr_merge_stick(&pad->thumb[0], state->stick);
			if (pressed & VR_BUTTON_TRIGGER)
			{
				if (hand == input->pointer_hand)
					input->clicks++;
				else
					input->pointer_hand = hand;
			}
		}
		input->snapped = 0;
		input->melee_seconds = input->tap_seconds = input->throw_seconds = 0.0f;
		input->room_walking = 0;
		input->grabbing[VR_HAND_LEFT] = input->grabbing[VR_HAND_RIGHT] = 0;
		/* (the hands' speeds start again after the menus, from where the
		hands are then) */
		input->main_position_valid = 0;
		for (hand = 0; hand < VR_HAND_COUNT; hand++)
		{
			input->throw_position_valid[hand] = 0;
			input->throw_speed[hand] = 0.0f;
		}
		input->off_position_valid = 0;
	}
	else
	{
		/* (the main grip pressed at a shoulder puts the weapon away there or
		draws the one there, vr_view.c; by a weapon on the ground, takes it;
		held on, it strikes nothing) */
		if ((main_pressed & VR_BUTTON_GRIP) && settings->gestures && !input->physical)
		{
			int side = vr_at_shoulder(frame, main_hand);

			if (side >= 0)
			{
				input->holster = 1;
				vr_shoulder(input, VR_SHOULDER_PRESS, side);
			}
			else if (settings->grab_reach[main_index])
			{
				vr_grab(input, main_index);
			}
		}
		if (!(main_hand->buttons & VR_BUTTON_GRIP))
			input->holster = 0;
		/* (a grenade thrown with the arm: its buttons held press nothing,
		the throw presses it; bare hands neither fire nor zoom) */
		memcpy(bindings, settings->bindings, sizeof(bindings));
		/* (the hands as they hold: a trigger fires in the hand that holds the
		weapon, holds a grenade in an empty one, nothing in one that holds the
		weapon too) */
		if (input->physical)
		{
			for (role = 0; role < VR_ROLE_COUNT; role++)
			{
				int action = bindings[role][VR_CONTROL_TRIGGER];

				if (action != VR_BIND_FIRE && action != VR_BIND_THROW_GRENADE)
					continue;
				if (role == VR_ROLE_MAIN)
					action = input->holding ? VR_BIND_FIRE : VR_BIND_THROW_GRENADE;
				else
					action = input->supporting ? VR_BIND_NONE : VR_BIND_THROW_GRENADE;
				bindings[role][VR_CONTROL_TRIGGER] = (unsigned char)action;
			}
		}
		memcpy(gesture_bindings, bindings, sizeof(bindings));
		for (role = 0; role < VR_ROLE_COUNT; role++)
		{
			for (control = 0; control < VR_CONTROL_COUNT; control++)
			{
				int action = bindings[role][control];

				if ((settings->throw_gesture && action == VR_BIND_THROW_GRENADE) ||
					((settings->bare_hands || (input->physical && !input->holding)) &&
						(action == VR_BIND_FIRE || action == VR_BIND_ZOOM)))
				{
					bindings[role][control] = VR_BIND_NONE;
				}
			}
		}
		vr_bind_hand(pad, main_hand, bindings[VR_ROLE_MAIN],
			input->physical || input->holster || input->grabbing[main_index] ? VR_BIND_NONE :
			bindings[VR_ROLE_MAIN][VR_CONTROL_GRIP]);
		/* (with the gestures the off grip's action is at the head alone) */
		vr_bind_hand(pad, off_hand, bindings[VR_ROLE_OFF],
			settings->gestures ? VR_BIND_NONE : bindings[VR_ROLE_OFF][VR_CONTROL_GRIP]);
		for (hand = 0; hand < VR_HAND_COUNT; hand++)
		{
			if (settings->throw_gesture)
			{
				if (vr_thrown(input, &frame->hand[hand], hand,
					gesture_bindings[hand == main_index ? VR_ROLE_MAIN : VR_ROLE_OFF], seconds))
				{
					input->throw_seconds = VR_PRESS_SECONDS;
					input->throws++;
					input->throw_hand = hand;
				}
			}
			else
			{
				input->throw_position_valid[hand] = 0;
				input->throw_speed[hand] = 0.0f;
			}
		}
		{
			float cosine = cosf(settings->move_turn), sine = sinf(settings->move_turn);
			float move[2];

			move[0] = off_hand->stick[0];
			move[1] = off_hand->stick[1];
			/* (room scale: with the off stick still, the body walks after the
			head) */
			input->room_walking = settings->room &&
				move[0] * move[0] + move[1] * move[1] < VR_ROOM_STILL * VR_ROOM_STILL &&
				vr_room_walk(input, frame, settings->room_offset, move);
			/* (turned to the left by move_turn: ahead of the head; driving,
			the throttle alone) */
			if (settings->drive)
				move[0] = 0.0f;
			pad->thumb[0] = vr_thumb(move[0] * cosine - move[1] * sine);
			pad->thumb[1] = vr_thumb(move[0] * sine + move[1] * cosine);
		}
		if (settings->seat_stick)
		{
			pad->thumb[2] = vr_thumb(main_hand->stick[0]);
			pad->thumb[3] = vr_thumb(main_hand->stick[1]);
			input->snapped = 0;
		}
		else
		{
			input->turn = vr_stick_turn(input, settings, main_hand->stick[0], seconds);
		}

		if (settings->gestures)
		{
			/* (with the hands as they hold, the other hand's punch too, but
			while it holds the weapon with the main) */
			if (vr_punched(input, frame, main_hand, input->main_position, &input->main_position_valid, seconds) |
				(input->physical && !input->supporting &&
					vr_punched(input, frame, off_hand, input->off_position, &input->off_position_valid, seconds)))
			{
				input->melee_seconds = VR_PRESS_SECONDS;
				input->melee_wait = VR_PUNCH_WAIT;
			}
			if (!input->physical || input->supporting)
				input->off_position_valid = 0;
			/* (the off grip's action on a press at the head, a tap: the
			flashlight toggles; at the main hand, the main grip let go, it
			takes the weapon there: vr_physical_hands' with the hands as they
			hold) */
			if ((off_pressed & VR_BUTTON_GRIP) && off_hand->tracked && !input->physical)
			{
				if (vr_distance(off_hand->grip.position, frame->head.position) < VR_HEAD_REACH)
				{
					input->tap_seconds = VR_PRESS_SECONDS;
				}
				else if (main_hand->tracked && !(main_hand->buttons & VR_BUTTON_GRIP) &&
					vr_distance(off_hand->grip.position, main_hand->grip.position) < VR_SWAP_REACH)
				{
					input->handover = 1;
					input->handover_seconds = 0.0f;
					memcpy(input->handover_main, main_hand->grip.position, sizeof(input->handover_main));
					memcpy(input->handover_off, off_hand->grip.position, sizeof(input->handover_off));
				}
				else if (settings->grab_reach[off_index])
				{
					vr_grab(input, off_index);
				}
			}
			/* (a weapon taken: the action held while the grip is, there) */
			for (hand = 0; hand < VR_HAND_COUNT; hand++)
			{
				if (!(frame->hand[hand].buttons & VR_BUTTON_GRIP))
					input->grabbing[hand] = 0;
				if (input->grabbing[hand])
				{
					input->grab_seconds[hand] += seconds;
					if (input->grab_seconds[hand] <= VR_GRAB_SECONDS && settings->grab_reach[hand])
						vr_bind(pad, VR_BIND_ACTION, 1.0f);
				}
			}
			/* (the hand-over: the main hand pulls away from the weapon the off
			holds, the hands swap; held together, both hold it) */
			if (input->handover)
			{
				input->handover_seconds += seconds;
				if (!(off_hand->buttons & VR_BUTTON_GRIP) || !off_hand->tracked || !main_hand->tracked ||
					(main_hand->buttons & VR_BUTTON_GRIP) || input->handover_seconds > VR_HANDOVER_SECONDS)
				{
					input->handover = 0;
				}
				else if (vr_distance(off_hand->grip.position, main_hand->grip.position) > VR_HANDOVER_APART &&
					vr_distance(main_hand->grip.position, input->handover_main) >
						vr_distance(off_hand->grip.position, input->handover_off))
				{
					input->left_handed = !input->left_handed;
					input->swaps++;
					input->main_position_valid = 0;
					input->holster = 0;
					input->handover = 0;
				}
			}
			if (vr_crouching(input, frame))
				vr_bind(pad, VR_BIND_CROUCH, 1.0f);
		}
		else
		{
			input->main_position_valid = 0;
			input->standing_valid = 0;
			input->handover = 0;
			input->grabbing[VR_HAND_LEFT] = input->grabbing[VR_HAND_RIGHT] = 0;
		}
		if (input->melee_seconds > 0.0f)
		{
			vr_bind(pad, VR_BIND_MELEE, 1.0f);
			input->melee_seconds -= seconds;
		}
		if (input->tap_seconds > 0.0f)
		{
			vr_bind(pad, settings->bindings[VR_ROLE_OFF][VR_CONTROL_GRIP], 1.0f);
			input->tap_seconds -= seconds;
		}
		if (input->throw_seconds > 0.0f)
		{
			vr_bind(pad, VR_BIND_THROW_GRENADE, 1.0f);
			input->throw_seconds -= seconds;
		}
	}
	input->menus = menus;
	input->held[VR_HAND_LEFT] = frame->hand[VR_HAND_LEFT].buttons;
	input->held[VR_HAND_RIGHT] = frame->hand[VR_HAND_RIGHT].buttons;
}

void vr_input_weapon_taken(struct vr_input *input)
{
	/* (the left hand takes it to aim with, or the right) */
	if (input->grab_pending > 0.0f && (input->grab_hand == VR_HAND_LEFT) != (input->left_handed != 0))
	{
		vr_swap_hands(input);
	}
	/* (the hands as they hold: the main hand holds it, by the grip that took
	it) */
	if (input->physical)
	{
		if (input->grab_pending > 0.0f)
			input->gripped = 1;
		input->holding = 1;
	}
	input->grab_pending = 0.0f;
}

void vr_hand_pointing(struct vr_hand const *hand, float pitch, float orientation[4])
{
	float turn[4];

	/* (a turn about x by a positive angle takes -z toward +y) */
	turn[0] = sinf(pitch * 0.5f);
	turn[1] = turn[2] = 0.0f;
	turn[3] = cosf(pitch * 0.5f);
	vr_quaternion_multiply(hand->grip.orientation, turn, orientation);
}

int vr_two_handed(struct vr_frame const *frame, int left_handed, int pistol_grip, float pitch,
	float orientation[4])
{
	static float const local_up[3] = { 0.0f, 1.0f, 0.0f };
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	struct vr_hand const *main_hand = &frame->hand[left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT];
	struct vr_hand const *off_hand = &frame->hand[left_handed ? VR_HAND_RIGHT : VR_HAND_LEFT];
	float z[3], x[3], y[3], up[3], forward[3], pointing[4], length;
	int axis;

	if (!main_hand->tracked || !off_hand->tracked || !(off_hand->buttons & VR_BUTTON_GRIP) ||
		vr_distance(off_hand->grip.position, frame->head.position) < VR_HEAD_REACH)
	{
		return VR_HELD_ONE;
	}
	vr_hand_pointing(main_hand, pitch, pointing);
	/* (at the main hand the off supports it: it points as the main does) */
	length = vr_distance(main_hand->grip.position, off_hand->grip.position);
	if (length < VR_SWAP_REACH)
	{
		memcpy(orientation, pointing, sizeof(pointing));
		return VR_HELD_SUPPORT;
	}
	/* (the rotation's -z from the main hand to the off, its y the main
	hand's up made square to it) */
	if (pistol_grip || length > VR_TWO_HANDED_REACH)
		return VR_HELD_ONE;
	for (axis = 0; axis < 3; axis++)
		z[axis] = (main_hand->grip.position[axis] - off_hand->grip.position[axis]) / length;
	vr_rotate(pointing, local_up, up);
	/* (ahead of the main hand: the off hand's in front, not behind it) */
	vr_rotate(pointing, local_forward, forward);
	if (-(z[0] * forward[0] + z[1] * forward[1] + z[2] * forward[2]) < 0.5f)
		return VR_HELD_ONE;
	x[0] = up[1] * z[2] - up[2] * z[1];
	x[1] = up[2] * z[0] - up[0] * z[2];
	x[2] = up[0] * z[1] - up[1] * z[0];
	length = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	if (length < 1e-3f)
		return VR_HELD_ONE;
	for (axis = 0; axis < 3; axis++)
		x[axis] /= length;
	y[0] = z[1] * x[2] - z[2] * x[1];
	y[1] = z[2] * x[0] - z[0] * x[2];
	y[2] = z[0] * x[1] - z[1] * x[0];
	vr_quaternion_from_axes(x, y, z, orientation);
	return VR_HELD_BOTH;
}

void vr_rumble_hands(float left, float right, int left_handed, int both, float amplitude[VR_HAND_COUNT])
{
	int main_index = left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT;

	amplitude[main_index] = left > right ? left : right;
	amplitude[!main_index] = both ? amplitude[main_index] : left;
}

int vr_ray_panel(struct vr_pose const *aim, struct vr_pose const *panel, float width, float height, float *u,
	float *v)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float inverse[4], relative[3], origin[3], ray[3], direction[3], t, x, y;

	inverse[0] = -panel->orientation[0];
	inverse[1] = -panel->orientation[1];
	inverse[2] = -panel->orientation[2];
	inverse[3] = panel->orientation[3];
	relative[0] = aim->position[0] - panel->position[0];
	relative[1] = aim->position[1] - panel->position[1];
	relative[2] = aim->position[2] - panel->position[2];
	vr_rotate(inverse, relative, origin);
	vr_rotate(aim->orientation, local_forward, ray);
	vr_rotate(inverse, ray, direction);
	/* (from before the panel's face, toward it) */
	if (!(origin[2] > 0.0f) || !(direction[2] < -1e-4f) || !(width > 0.0f) || !(height > 0.0f))
		return 0;
	t = -origin[2] / direction[2];
	x = origin[0] + direction[0] * t;
	y = origin[1] + direction[1] * t;
	*u = x / width + 0.5f;
	*v = 0.5f - y / height;
	return *u >= 0.0f && *u <= 1.0f && *v >= 0.0f && *v <= 1.0f;
}

#ifndef VR_INPUT_HARNESS

static struct vr_input vr_input_state;
static float vr_input_move_turn;
/* a menu is up, with controllers or without */
static int vr_input_menus_up;
/* vr.bindings, as it was last read */
static unsigned char vr_input_bindings[VR_ROLE_COUNT][VR_CONTROL_COUNT];
static unsigned long vr_input_bindings_read_at;
static int vr_input_bindings_valid;
static unsigned long vr_input_swaps, vr_input_throws;
/* the weapon is held with both hands (vr_view.c) */
static int vr_input_both_hands;

void platform_log(char const *format, ...);

static void vr_input_settings_read(struct vr_input_settings *settings)
{
	double snap = config_real("vr.snap_turn");
	double rate = config_real("vr.smooth_turn_speed");

	if (!vr_input_bindings_valid || vr_input_bindings_read_at != config_changes())
	{
		int bad = vr_input_bindings_parse(config_string("vr.bindings"), vr_input_bindings);

		if (bad)
			platform_log("vr: vr.bindings: %d items not understood, left out", bad);
		vr_input_bindings_read_at = config_changes();
		vr_input_bindings_valid = 1;
	}
	memcpy(settings->bindings, vr_input_bindings, sizeof(vr_input_bindings));
	settings->left_handed = config_boolean("vr.left_handed");
	settings->snap_angle = snap > 0.0 && snap <= 180.0 ? (float)(snap * 0.017453292519943) : 0.0f;
	settings->turn_rate = (float)((rate > 0.0 && rate <= 720.0 ? rate : 120.0) * 0.017453292519943);
	settings->gestures = config_boolean("vr.gestures");
	settings->move_turn = vr_input_move_turn;
	settings->seat_stick = vr_render_seat_stick();
	settings->drive = vr_render_drive();
	settings->room = vr_render_room(settings->room_offset);
	settings->aim_pitch = vr_render_aim_pitch();
	settings->pistol_grip = vr_render_pistol_grip();
	settings->throw_gesture = config_boolean("vr.throw_gesture");
	settings->bare_hands = vr_render_bare_hands();
	settings->grab_reach[VR_HAND_LEFT] = vr_render_grab_reach(VR_HAND_LEFT);
	settings->grab_reach[VR_HAND_RIGHT] = vr_render_grab_reach(VR_HAND_RIGHT);
	settings->physical = strcmp(config_string("vr.hands"), "game") != 0;
	settings->grip_toggle = config_boolean("vr.grip_toggle");
	settings->on_foot = vr_render_on_foot();
	settings->shoulder_full[VR_HAND_LEFT] = vr_render_shoulder_full(VR_HAND_LEFT);
	settings->shoulder_full[VR_HAND_RIGHT] = vr_render_shoulder_full(VR_HAND_RIGHT);
	settings->drop = vr_rules_active();
}

int vr_input_gamepad(struct vr_pad *pad, int menus)
{
	struct vr_frame const *frame;
	struct vr_input_settings settings;

	if (!vr_active())
		return 0;
	vr_input_menus_up = menus != 0;
	frame = vr_frame_get();
	if (!frame->controllers)
		return 0;
	vr_input_settings_read(&settings);
	vr_input_update(&vr_input_state, frame, &settings, menus != 0,
		frame->display_period > 0.0f ? frame->display_period : 1.0f / 90.0f, pad);	/* (the hands swapped the weapon: felt in both) */
	if (vr_input_state.swaps != vr_input_swaps)
	{
		vr_input_swaps = vr_input_state.swaps;
		vr_haptic(VR_HAND_LEFT, 0.5f, 0.06f);
		vr_haptic(VR_HAND_RIGHT, 0.5f, 0.06f);
		platform_log("vr: the weapon changes to the %s hand", vr_input_state.left_handed ? "left" : "right");
	}
	/* (a grenade thrown: felt in the hand that threw it) */
	if (vr_input_state.throws != vr_input_throws)
	{
		vr_input_throws = vr_input_state.throws;
		vr_haptic(vr_input_state.throw_hand, 0.3f, 0.04f);
	}
	return 1;
}

int vr_input_left_handed(void)
{
	return vr_input_state.left_handed_valid ? vr_input_state.left_handed : config_boolean("vr.left_handed");
}

/* (the game sets the motors every frame: each pulse lasts a little longer,
as the SDL controllers' do, xinput_sdl.c) */
void vr_input_rumble(float left, float right)
{
	float amplitude[VR_HAND_COUNT];

	vr_rumble_hands(left, right, vr_input_left_handed(), vr_input_both_hands, amplitude);
	vr_haptic(VR_HAND_LEFT, amplitude[VR_HAND_LEFT], 0.1f);
	vr_haptic(VR_HAND_RIGHT, amplitude[VR_HAND_RIGHT], 0.1f);
}

void vr_input_weapon_hands(int both)
{
	vr_input_both_hands = both != 0;
}

unsigned long vr_input_shoulder_grabs(int *side, int *kind)
{
	*side = vr_input_state.shoulder_side;
	*kind = vr_input_state.shoulder_kind;
	return vr_input_state.shoulder_grabs;
}

void vr_input_drop_velocity(float velocity[3])
{
	memcpy(velocity, vr_input_state.drop_velocity, sizeof(vr_input_state.drop_velocity));
}

int vr_input_physical(void)
{
	return vr_input_state.physical;
}

int vr_input_holding(void)
{
	return vr_input_state.holding;
}

void vr_input_weapon_came(void)
{
	vr_input_weapon_taken(&vr_input_state);
}

void vr_input_aim_turn(float radians)
{
	vr_input_move_turn = radians;
}

int vr_input_room_walking(void)
{
	return vr_input_state.room_walking;
}

float vr_input_turn(void)
{
	float turn = vr_input_state.turn;

	vr_input_state.turn = 0.0f;
	return turn;
}

/* where the pointing hand meets the panel, while a menu is up */
static int vr_input_pointing(float *u, float *v)
{
	struct vr_frame const *frame;
	struct vr_hand const *hand;
	struct vr_pose panel;
	float width, height;

	if (!vr_active() || !vr_input_state.menus)
		return 0;
	frame = vr_frame_get();
	hand = &frame->hand[vr_input_state.pointer_hand];
	return frame->controllers && hand->tracked && vr_panel_pose(&panel, &width, &height) &&
		vr_ray_panel(&hand->aim, &panel, width, height, u, v);
}

int vr_input_pointer(float *u, float *v, int *clicks)
{
	*clicks = vr_input_state.clicks;
	vr_input_state.clicks = 0;
	if (!vr_input_pointing(u, v))
	{
		*clicks = 0;
		return 0;
	}
	return 1;
}

int vr_input_menus(void)
{
	return vr_input_menus_up;
}

int vr_input_pointer_shown(float *u, float *v)
{
	return vr_input_pointing(u, v);
}

int vr_input_pointer_hand(void)
{
	struct vr_frame const *frame;

	if (!vr_active() || !vr_input_state.menus)
		return -1;
	frame = vr_frame_get();
	if (!frame->controllers || !frame->hand[vr_input_state.pointer_hand].tracked)
		return -1;
	return vr_input_state.pointer_hand;
}

#endif

#endif
