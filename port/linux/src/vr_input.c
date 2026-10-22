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
VR_PUNCH_SPEED, is a melee attack; its grip at a shoulder (a holster)
switches weapons, not its action; the off hand's grip does its action (the
flashlight) only at the head, with the palms together takes the weapon
into that hand (the hands swap: an idea of astromaddie's VR port), and
elsewhere near the weapon holds it with both hands (vr_two_handed: the
weapon then points from the main hand to the off); and the head lowered
VR_CROUCH_DEPTH below its standing height (the highest since the runtime
last recentred) crouches.

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
/* the hands this near (their grips, metres) swap the weapon */
#define VR_SWAP_REACH 0.15f
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

/* the main hand punched forward this frame */
static int vr_punched(struct vr_input *input, struct vr_frame const *frame, struct vr_hand const *main_hand,
	float seconds)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float velocity[3], forward[3], speed, along;
	int punched = 0, axis;

	if (!main_hand->tracked)
	{
		input->main_position_valid = 0;
		return 0;
	}
	if (input->main_position_valid && seconds > 0.0f)
	{
		for (axis = 0; axis < 3; axis++)
			velocity[axis] = (main_hand->grip.position[axis] - input->main_position[axis]) / seconds;
		speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
		vr_rotate(frame->head.orientation, local_forward, forward);
		along = velocity[0] * forward[0] + velocity[1] * forward[1] + velocity[2] * forward[2];
		punched = speed > VR_PUNCH_SPEED && along > VR_PUNCH_ALONG * speed && input->melee_wait <= 0.0f;
	}
	memcpy(input->main_position, main_hand->grip.position, sizeof(input->main_position));
	input->main_position_valid = 1;
	return punched;
}

/* the main hand at either shoulder, about the head's yaw */
static int vr_at_shoulder(struct vr_frame const *frame, struct vr_hand const *hand)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float forward[3], length, side;
	int shoulder;

	if (!hand->tracked)
		return 0;
	vr_rotate(frame->head.orientation, local_forward, forward);
	length = sqrtf(forward[0] * forward[0] + forward[2] * forward[2]);
	if (length < 1e-3f)
		return 0;
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
			return 1;
	}
	return 0;
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
	unsigned long off_pressed;

	/* (the setting's hand, from when it changes; until then the hands may
	have swapped) */
	if (!input->left_handed_valid || input->setting_left_handed != settings->left_handed)
	{
		input->left_handed = settings->left_handed != 0;
		input->setting_left_handed = settings->left_handed;
		input->left_handed_valid = 1;
	}
	main_index = input->left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT;
	off_index = main_index == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT;
	main_hand = &frame->hand[main_index];
	off_hand = &frame->hand[off_index];
	off_pressed = off_hand->buttons & ~input->held[off_index];
	memset(pad, 0, sizeof(*pad));
	input->turn = 0.0f;
	if (!(seconds > 0.0f && seconds < 1.0f))
		seconds = 0.0f;
	if (input->melee_wait > 0.0f)
		input->melee_wait -= seconds;

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
		input->melee_seconds = input->tap_seconds = 0.0f;
	}
	else
	{
		/* (the grip at a shoulder draws the other weapon from its holster) */
		if ((main_hand->buttons & VR_BUTTON_GRIP) && settings->gestures && vr_at_shoulder(frame, main_hand))
			input->holster = 1;
		if (!(main_hand->buttons & VR_BUTTON_GRIP))
			input->holster = 0;
		vr_bind_hand(pad, main_hand, settings->bindings[VR_ROLE_MAIN],
			input->holster ? VR_BIND_SWITCH_WEAPON : settings->bindings[VR_ROLE_MAIN][VR_CONTROL_GRIP]);
		/* (with the gestures the off grip's action is at the head alone) */
		vr_bind_hand(pad, off_hand, settings->bindings[VR_ROLE_OFF],
			settings->gestures ? VR_BIND_NONE : settings->bindings[VR_ROLE_OFF][VR_CONTROL_GRIP]);
		{
			float cosine = cosf(settings->move_turn), sine = sinf(settings->move_turn);

			/* (turned to the left by move_turn: ahead of the head) */
			pad->thumb[0] = vr_thumb(off_hand->stick[0] * cosine - off_hand->stick[1] * sine);
			pad->thumb[1] = vr_thumb(off_hand->stick[0] * sine + off_hand->stick[1] * cosine);
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
			if (vr_punched(input, frame, main_hand, seconds))
			{
				input->melee_seconds = VR_PRESS_SECONDS;
				input->melee_wait = VR_PUNCH_WAIT;
			}
			/* (the off grip's action on a press at the head, a tap: the
			flashlight toggles; with the palms together, the main grip let
			go, the hands swap) */
			if ((off_pressed & VR_BUTTON_GRIP) && off_hand->tracked)
			{
				if (vr_distance(off_hand->grip.position, frame->head.position) < VR_HEAD_REACH)
				{
					input->tap_seconds = VR_PRESS_SECONDS;
				}
				else if (main_hand->tracked && !(main_hand->buttons & VR_BUTTON_GRIP) &&
					vr_distance(off_hand->grip.position, main_hand->grip.position) < VR_SWAP_REACH)
				{
					input->left_handed = !input->left_handed;
					input->swaps++;
					input->main_position_valid = 0;
					input->holster = 0;
				}
			}
			if (vr_crouching(input, frame))
				vr_bind(pad, VR_BIND_CROUCH, 1.0f);
		}
		else
		{
			input->main_position_valid = 0;
			input->standing_valid = 0;
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
	}
	input->menus = menus;
	input->held[VR_HAND_LEFT] = frame->hand[VR_HAND_LEFT].buttons;
	input->held[VR_HAND_RIGHT] = frame->hand[VR_HAND_RIGHT].buttons;
}

int vr_two_handed(struct vr_frame const *frame, int left_handed, float orientation[4])
{
	static float const local_up[3] = { 0.0f, 1.0f, 0.0f };
	struct vr_hand const *main_hand = &frame->hand[left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT];
	struct vr_hand const *off_hand = &frame->hand[left_handed ? VR_HAND_RIGHT : VR_HAND_LEFT];
	float z[3], x[3], y[3], up[3], length, trace, scale;
	int axis;

	if (!main_hand->tracked || !off_hand->tracked || !(off_hand->buttons & VR_BUTTON_GRIP) ||
		vr_distance(off_hand->grip.position, frame->head.position) < VR_HEAD_REACH)
	{
		return 0;
	}
	/* (the rotation's -z from the main hand to the off, its y the main
	hand's up made square to it) */
	for (axis = 0; axis < 3; axis++)
		z[axis] = main_hand->grip.position[axis] - off_hand->grip.position[axis];
	length = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
	if (length > VR_TWO_HANDED_REACH || length < 0.05f)
		return 0;
	for (axis = 0; axis < 3; axis++)
		z[axis] /= length;
	vr_rotate(main_hand->aim.orientation, local_up, up);
	/* (ahead of the main hand: the off hand's in front, not behind it) */
	{
		static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
		float forward[3];

		vr_rotate(main_hand->aim.orientation, local_forward, forward);
		if (-(z[0] * forward[0] + z[1] * forward[1] + z[2] * forward[2]) < 0.5f)
			return 0;
	}
	x[0] = up[1] * z[2] - up[2] * z[1];
	x[1] = up[2] * z[0] - up[0] * z[2];
	x[2] = up[0] * z[1] - up[1] * z[0];
	length = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	if (length < 1e-3f)
		return 0;
	for (axis = 0; axis < 3; axis++)
		x[axis] /= length;
	y[0] = z[1] * x[2] - z[2] * x[1];
	y[1] = z[2] * x[0] - z[0] * x[2];
	y[2] = z[0] * x[1] - z[1] * x[0];
	/* the quaternion of the matrix whose columns are x, y and z */
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
	return 1;
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
static unsigned long vr_input_swaps;

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
		frame->display_period > 0.0f ? frame->display_period : 1.0f / 90.0f, pad);
	/* (the hands swapped the weapon: felt in both) */
	if (vr_input_state.swaps != vr_input_swaps)
	{
		vr_input_swaps = vr_input_state.swaps;
		vr_haptic(VR_HAND_LEFT, 0.5f, 0.06f);
		vr_haptic(VR_HAND_RIGHT, 0.5f, 0.06f);
		platform_log("vr: the weapon changes to the %s hand", vr_input_state.left_handed ? "left" : "right");
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
	vr_haptic(VR_HAND_LEFT, left, 0.1f);
	vr_haptic(VR_HAND_RIGHT, right, 0.1f);
}

void vr_input_aim_turn(float radians)
{
	vr_input_move_turn = radians;
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

#endif

#endif
