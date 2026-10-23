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

The heading. The player's facing is the aiming hand's direction (vr.aim
"hand": the main controller's, while the player is on foot) or else the
head's, turned by the heading, set each frame as the game's input is taken
(vr_player_control_facing), so that shots, the crosshair and the scripts
that wait for the player to look around follow them. The controller's
stick (vr_input.c), the mouse's and the pad's turns, and the game's own (a
respawn, a script), move the heading. Pitch is the aim's alone.

The weapon in the hand. While the hand aims, the first-person weapon is
posed from a camera behind the hand (vr_weapon_grip: where the weapon's
grip sits from the game's own camera), along its aim, so that it is held
where the controller is; and in place of the HUD's crosshair a reticle is
held in the world where the shot's ray (weapon_get_projectile_ray: the
game's own, from the muzzle) meets it, red when the game's crosshair would
be (vr_reticle).

The scope. While the hand's weapon is zoomed, a window more is drawn
before the HUD's: the world from the muzzle along the aim with the zoom's
field, into a square of the back buffer, whose picture is held on the
weapon (vr_openxr.c), where the eye looks along it; the eyes are not
zoomed, and the HUD's zoomed view (the sniper's ticks and numbers) is left
out of the HUD (hud_weapon.c).

The HUD is drawn alone, over a clear ground, from the head between the
eyes with the field of the panel it is shown on (vr_openxr.c holds the
panel ahead of the head), so that what it marks (nav points, friends'
names) lands on what it marks. The first-person weapon is posed once a
frame from the head between the eyes, and what changes once a frame (the
sky's animation, weather, glows) moves on in the left eye only.

The menus are a panel held in the room (vr_openxr.c): over the main
menu's scene, seen in the headset as the game's is, or over the game (the
pause menu), and a hand points at them (vr_input.c). Loading screens and
cutscenes are drawn as always, onto the flat screen held in the room.
*/

#ifndef HALO_GLES

#include "cseries.h"
#include "vr_arms.h"
#include "vr_input.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "game/game.h"
#include "interface/first_person_weapons.h"
#include "interface/hud.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "models/model_animation_definitions.h"
#include "objects/objects.h"
#include "interface/hud_definitions.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "units/vehicle_datum.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"

#include <math.h>

double config_real(const char *name);
int config_boolean(const char *name);
const char *config_string(const char *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);
/* d3d8_gl.c: the back buffer cleared to nothing (the HUD's ground), and
the back buffer's picture into an eye's image */
void halo_vr_clear_transparent(void);
void halo_vr_mirror_winding(int mirrored);
void halo_vr_two_sided(int two_sided);
void halo_vr_resolve_eye(int eye);
/* ... and a rectangle of it (the screen's coordinates) into the scope's */
void halo_vr_resolve_scope(short x0, short y0, short x1, short y1);

/* how far the head may lean the view from the game's camera, in metres
(in a seat, or anywhere without room scale), and how far short of a wall
it stops (world units) */
#define VR_HEAD_REACH 0.35f
/* room scale (vr_room_follow): how far the head may be from the body on
foot (when the unit is held back, or walked by the stick), and anywhere
else (the menus, a cinematic, dead: the room); a move of the game's camera
larger than this in a frame (world units) is not a walk */
#define VR_ROOM_REACH 0.6f
#define VR_ROOM_ANYWHERE 10.0f
#define VR_ROOM_JUMP 0.5f
/* how long after a walk the unit still coasts (seconds), its moves the
body's too */
#define VR_ROOM_COAST 0.3f
#define VR_WALL_MARGIN 0.04f

/* a cinematic's cut: its camera moved further than this in a frame (world
units) or turned more (radians); and the eyes' blink at a cut (seconds) */
#define VR_CUT_DISTANCE 1.5f
#define VR_CUT_TURN 0.5236f
#define VR_CUT_BLINK 0.1f

/* a turn of the head in a frame (radians) that counts as looking around,
more than its tremor: about 15 degrees a second at 60 frames */
#define VR_LOOK_TURN 0.004f

/* where the first-person weapon's grip is from the game's camera, in
metres: right, up and back (the aim's axes); and its muzzle from the grip */
static float const vr_weapon_grip[3] = { 0.10f, -0.12f, -0.20f };
static float const vr_weapon_muzzle[3] = { 0.0f, 0.04f, -0.25f };
/* the scope's picture: where it is held from the grip (on the line the eye
looks along, ahead of the weapon's camera), its width in metres, and its
side in the back buffer's pixels */
static float const vr_scope_offset[3] = { -0.10f, 0.12f, -0.10f };
#define VR_SCOPE_WIDTH 0.07f
#define VR_SCOPE_PIXELS 512

/* where a first-person model's barrel is, learnt while the weapon is idle
(vr_render_weapon_posed): its muzzle (the "primary trigger" marker) as the
weapon's camera holds it, LOCAL metres from the camera, and the direction
it points, by weapon type; eased towards each frame's by VR_WEAPON_FIT_EASE,
so that an idle animation's sway is not followed */
#define VR_WEAPON_FITS 8
#define VR_WEAPON_FIT_EASE 0.05f

struct vr_weapon_fit
{
	boolean valid;
	long definition_index;
	float muzzle[3];
	float forward[3];
};

int vr_render_pass;

/* what the player is doing, for room scale */
enum
{
	_vr_room_none,
	_vr_room_on_foot,
	_vr_room_seated,
	_vr_room_cinematic
};

/* what aims in a seat (vr.vehicle_aim) */
enum
{
	_vr_seat_aim_hand,
	_vr_seat_aim_stick,
	_vr_seat_aim_head
};

/* getting into a seat seen from it or out of it, the eyes blink (seconds):
the view is turned to the seat's forward */
#define VR_SEAT_BLINK 0.2f

/* a vehicle's definition as far as its type and its wheels' turn
(vehicles.c's layout; its header keeps it opaque, as input_abstraction.c's
does): the types the off stick steers, and the turn's limits in degrees,
left positive and right negative */
enum
{
	_vr_vehicle_human_jeep = 1,
	_vr_vehicle_alien_scout = 4
};

struct vr_vehicle_definition
{
	struct unit_definition unit;
	unsigned long flags;
	short type;
	short pad;
	real speed[4];
	real turn[2];
};

typedef char vr_vehicle_definition_turn_offset[offsetof(struct vr_vehicle_definition, turn) == 0x308 ? 1 : -1];

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
	/* until a player aims (the main menu's scene, a game's first frame): the
	head's yaw as the view began, and the recentre it was taken at */
	boolean unaimed_valid;
	real unaimed_head_yaw;
	unsigned long unaimed_recentres;
	/* the yaw the game's facing was last set to, and whether the head or a
	hand set it (and which) */
	real set_yaw;
	boolean aiming;
	boolean hand_aiming;
	/* in a seat, the facing turned by the main hand's stick, the head free;
	and the seat aimed by the stick, the hand or the drive (a reticle shown) */
	boolean seat_stick;
	boolean seat_aiming;
	/* the first-person weapon's camera, behind the aiming hand, its muzzle
	in the world, and the unit holding it */
	struct render_camera weapon_camera;
	boolean weapon_in_hand;
	real_point3d muzzle;
	long unit_index;
	/* the models' barrels, the next replaced */
	struct vr_weapon_fit fits[VR_WEAPON_FITS];
	short next_fit;
	/* the scope's window this frame, and its field */
	boolean scope;
	rectangle2d scope_window;
	real_rectangle2d scope_bounds;

	/* the settings, read again when they change */
	unsigned long settings_read_at;
	boolean settings_valid;
	short main_hand;
	boolean aim_with_hand;
	short seat_aim;
	boolean seat_first_person;
	boolean drive;
	boolean immersive_cinematics;
	boolean room_scale;
	boolean eye_height_player;
	boolean arms_free;

	/* vr.eye_height "player": the unit's crouch at the last two ticks
	(blended as the drawn camera is) and the tick of the later */
	real eye_crouch[2];
	long eye_crouch_tick;

	/* this frame's heading and the controllers' grips in the world, for
	the arms (vr_arms.c) */
	real frame_heading;
	boolean grip_valid[VR_HAND_COUNT];
	struct render_camera grips[VR_HAND_COUNT];

	/* in a seat seen from it (vr.vehicle_view "first_person"): the vehicle
	and the seat, the view's yaw from the vehicle's (the seat's own turn, less
	the head's as the player sat, and the stick's turns since), and whether
	the off stick steers it this frame */
	boolean seat_view;
	long seat_vehicle_index;
	short seat_index;
	real seat_yaw;
	boolean driving;

	/* room scale: where the body stands in LOCAL space (x and z), what the
	player is doing (VR_ROOM_*), whether the unit walks after the head this
	frame, and the game's camera last drawn */
	boolean room_valid;
	short room_state;
	float room_body[2];
	boolean room;
	boolean room_anchor_valid;
	real_point3d room_anchor;
	real room_coast;

	/* in a cinematic: the head's yaw as its shot began, and its camera's
	last place and yaw (to tell a cut) */
	boolean cinematic;
	real cinematic_head_yaw;
	real_point3d cinematic_position;
	real cinematic_yaw;
} vr_view;

static void vr_settings_read(
	void)
{
	if (vr_view.settings_valid && vr_view.settings_read_at == config_changes())
		return;
	vr_view.settings_read_at = config_changes();
	vr_view.settings_valid = TRUE;
	vr_view.aim_with_hand = strcmp(config_string("vr.aim"), "head") != 0;
	vr_view.seat_aim = !strcmp(config_string("vr.vehicle_aim"), "stick") ? _vr_seat_aim_stick :
		!strcmp(config_string("vr.vehicle_aim"), "head") ? _vr_seat_aim_head : _vr_seat_aim_hand;
	vr_view.seat_first_person = strcmp(config_string("vr.vehicle_view"), "chase") != 0;
	vr_view.drive = strcmp(config_string("vr.vehicle_steering"), "aim") != 0;
	vr_view.immersive_cinematics = strcmp(config_string("vr.cinematics"), "screen") != 0;
	vr_view.room_scale = config_boolean("vr.roomscale");
	vr_view.eye_height_player = strcmp(config_string("vr.eye_height"), "game") != 0;
	vr_view.arms_free = strcmp(config_string("vr.arms"), "weapon") != 0;
}

/* the hand that aims the weapon it holds: the main controller, tracked,
with vr.aim "hand", the player on foot or, in a seat seen from it with
vr.vehicle_aim "hand", holding a weapon (a passenger); else NULL */
static struct vr_hand const *vr_aiming_hand(
	struct vr_frame const *frame,
	short local_player_index)
{
	struct vr_hand const *hand;
	long unit_index;
	struct unit_datum *unit;

	vr_settings_read();
	vr_view.main_hand = vr_input_left_handed() ? VR_HAND_LEFT : VR_HAND_RIGHT;
	hand = &frame->hand[vr_view.main_hand];
	if (!vr_view.aim_with_hand || !frame->controllers || !hand->tracked)
		return NULL;
	unit_index = player_get(local_player_get_player_index(local_player_index))->unit_index;
	unit = unit_index == NONE ? NULL : unit_try_and_get(unit_index);
	if (!unit)
		return NULL;
	if (unit->object.parent_object_index != NONE &&
		(!vr_view.seat_view || vr_view.seat_aim != _vr_seat_aim_hand || unit->unit.current_weapon_index < 0 ||
			unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index) == NONE))
	{
		return NULL;
	}
	vr_view.unit_index = unit_index;
	return hand;
}

/* the player in a seat, its facing the main hand's stick's to turn
(vr.vehicle_aim), with controllers */
static boolean vr_seat_stick(
	struct vr_frame const *frame,
	short local_player_index)
{
	long unit_index = player_get(local_player_get_player_index(local_player_index))->unit_index;
	struct unit_datum *unit = unit_index == NONE ? NULL : unit_try_and_get(unit_index);

	vr_settings_read();
	if (vr_view.seat_aim != _vr_seat_aim_stick || !frame->controllers || !unit || unit->object.parent_object_index == NONE)
		return FALSE;
	vr_view.unit_index = unit_index;
	return TRUE;
}

/* the unit's seat: the vehicle it sits in and the seat's definition; FALSE
on foot (or carried by anything but a vehicle) */
static boolean vr_seat_of(
	long unit_index,
	long *vehicle_index,
	struct unit_seat const **seat)
{
	struct unit_datum *unit = unit_index == NONE ? NULL : unit_try_and_get(unit_index);
	struct unit_datum *vehicle;
	struct unit_definition const *definition;

	if (!unit || unit->object.parent_object_index == NONE || unit->unit.parent_seat_index < 0)
		return FALSE;
	vehicle = vehicle_try_and_get(unit->object.parent_object_index);
	if (!vehicle)
		return FALSE;
	definition = unit_definition_get(vehicle->definition_index);
	if (unit->unit.parent_seat_index >= definition->unit.seats.count)
		return FALSE;
	*vehicle_index = unit->object.parent_object_index;
	*seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, unit->unit.parent_seat_index, struct unit_seat);
	return TRUE;
}

/* the view's heading in a seat seen from it: the vehicle's yaw, as drawn
this frame (blended between the ticks: its own yaw, taken as the game ticks,
would turn the view in 30 Hz steps against the smoothly drawn vehicle) when
`drawn`, and the seat's. Level: the vehicle's pitch and roll would tilt the
horizon (astromaddie's seat view) */
static real vr_seat_heading(
	boolean drawn)
{
	struct object_datum *vehicle = (struct object_datum *)object_try_and_get_and_verify_type(vr_view.seat_vehicle_index,
		_object_mask_vehicle);
	real_vector3d forward;
	real yaw;

	if (!vehicle)
		return vr_view.heading;
	/* (in the world: a vehicle carried, by a Pelican, has its forward its
	carrier's node's) */
	object_get_orientation(vr_view.seat_vehicle_index, &forward, NULL);
	yaw = vr_yaw_of(forward.n, 0.0f);
	if (drawn)
	{
		real_matrix4x3 const *blended = render_interpolation_object_node_matrices(vr_view.seat_vehicle_index);
		real_matrix4x3 const *ticked = (real_matrix4x3 const *)object_header_block_get(vr_view.seat_vehicle_index,
			&vehicle->object.node_matrices);

		/* (the root node's turn from the tick to the frame: whichever way the
		node faces in the vehicle cancels out) */
		if (blended && ticked)
			yaw += vr_wrap_angle(vr_yaw_of(blended[0].forward.n, 0.0f) - vr_yaw_of(ticked[0].forward.n, 0.0f));
	}
	return vr_wrap_angle(yaw + vr_view.seat_yaw);
}

int vr_render_seat_view(
	short local_player_index)
{
	long vehicle_index;
	struct unit_seat const *seat;

	if (!vr_active() || local_player_index != local_player_get_next(NONE) || cinematic_in_progress())
		return FALSE;
	vr_settings_read();
	return vr_view.seat_first_person &&
		vr_seat_of(player_control_get_unit_index(local_player_index), &vehicle_index, &seat);
}

/* as the game takes the player's input: whether the seat is seen from it
(the director's camera the first person's there), and if so its heading.
Getting in, the seat's forward is put ahead of the head, with a blink;
getting out, the heading on foot goes on from the seat's, so that the view
does not jump */
static boolean vr_seat_view_update(
	short local_player_index,
	float const head_forward[3],
	real facing_yaw,
	real stick_turn)
{
	long unit_index = player_control_get_unit_index(local_player_index);
	long vehicle_index;
	struct unit_seat const *seat;
	struct unit_datum *unit;

	if (!vr_render_seat_view(local_player_index) ||
		director_get_perspective(local_player_index) != _director_perspective_first_person ||
		!vr_seat_of(unit_index, &vehicle_index, &seat))
	{
		if (vr_view.seat_view)
		{
			vr_view.heading = vr_seat_heading(FALSE);
			vr_view.heading_valid = TRUE;
			vr_view.set_yaw = facing_yaw;
			vr_blink(VR_SEAT_BLINK);
		}
		vr_view.seat_view = FALSE;
		return FALSE;
	}
	unit = unit_get(unit_index);
	if (!vr_view.seat_view || vehicle_index != vr_view.seat_vehicle_index ||
		unit->unit.parent_seat_index != vr_view.seat_index)
	{
		real_vector3d seat_forward, vehicle_forward;

		/* (the seated unit's forward is its seat's node's: the world's taken) */
		object_get_orientation(unit_index, &seat_forward, NULL);
		object_get_orientation(vehicle_index, &vehicle_forward, NULL);
		vr_view.seat_vehicle_index = vehicle_index;
		vr_view.seat_index = unit->unit.parent_seat_index;
		vr_view.seat_yaw = vr_wrap_angle(vr_yaw_of(seat_forward.n, 0.0f) - vr_yaw_of(vehicle_forward.n, 0.0f) -
			vr_yaw_of(head_forward, 0.0f));
		vr_blink(VR_SEAT_BLINK);
		platform_log("vr: seated in vehicle %lx's seat %d, seen from it: the seat %.0f degrees from the vehicle's "
			"forward", vehicle_index, (int)vr_view.seat_index,
			(double)(vr_wrap_angle(vr_yaw_of(seat_forward.n, 0.0f) - vr_yaw_of(vehicle_forward.n, 0.0f)) * 57.2958f));
	}
	vr_view.seat_yaw = vr_wrap_angle(vr_view.seat_yaw + stick_turn);
	vr_view.seat_view = TRUE;
	vr_view.unit_index = unit_index;
	return TRUE;
}

/* a driver steered by the off stick (vr.vehicle_steering "drive"), in a
Warthog or a Ghost: the facing the vehicle's forward turned by the stick
(vr_drive_steer), level with it, pitched as the head looks (the Ghost's guns
follow it); FALSE for any other seat or vehicle (a tank's facing is its
cannon's, a flier's where it flies) */
static boolean vr_drive_facing(
	struct vr_frame const *frame,
	float const head_forward[3],
	real_vector3d *facing)
{
	long vehicle_index;
	struct unit_seat const *seat;
	struct vehicle_datum *vehicle;
	struct vr_vehicle_definition const *definition;
	float const *stick = frame->hand[vr_view.main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT].stick;
	real yaw, pitch, level;

	if (!vr_view.drive || !frame->controllers || !vr_seat_of(vr_view.unit_index, &vehicle_index, &seat) ||
		!TEST_FLAG(seat->flags, _unit_seat_driver_bit))
	{
		return FALSE;
	}
	vehicle = vehicle_datum_get(vehicle_index);
	definition = (struct vr_vehicle_definition const *)tag_get(VEHICLE_DEFINITION_TAG, vehicle->definition_index);
	if (definition->type != _vr_vehicle_human_jeep && definition->type != _vr_vehicle_alien_scout)
		return FALSE;
	yaw = vr_yaw_of(vehicle->object.forward.n, 0.0f) +
		vr_drive_steer(stick[0], definition->turn[0], definition->turn[1], vehicle->vehicle.speed < 0.0f);
	level = (real)sqrt(head_forward[0] * head_forward[0] + head_forward[1] * head_forward[1]);
	pitch = (real)atan2(head_forward[2], level);
	facing->i = (real)(cos(yaw) * cos(pitch));
	facing->j = (real)(sin(yaw) * cos(pitch));
	facing->k = (real)sin(pitch);
	return TRUE;
}

/* the aim's orientation: the hand's, or with the off hand holding the
weapon too, from the main hand to it (vr_two_handed) */
static void vr_hand_aim(
	struct vr_frame const *frame,
	struct vr_hand const *hand,
	float orientation[4])
{
	if (!vr_two_handed(frame, vr_view.main_hand == VR_HAND_LEFT, orientation))
		memcpy(orientation, hand->aim.orientation, sizeof(hand->aim.orientation));
}

/* ---------- the weapon in the hand */

/* the weapon the hand's unit holds, or NONE */
static long vr_held_weapon(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);

	if (!unit || unit->unit.current_weapon_index < 0)
		return NONE;
	return unit_inventory_get_weapon(vr_view.unit_index, unit->unit.current_weapon_index);
}

/* an offset (LOCAL metres, right, up and back) on the side of the hand
that aims: the right hand's mirrored for the left */
static void vr_weapon_side(
	float const right[3],
	float side[3])
{
	side[0] = vr_view.main_hand == VR_HAND_LEFT ? -right[0] : right[0];
	side[1] = right[1];
	side[2] = right[2];
}

/* the weapon the unit aims: its own, or in a gunner's seat the vehicle's
(as the HUD's crosshair takes it, hud_weapon.c) */
static long vr_aimed_weapon(
	void)
{
	long weapon_index = vr_held_weapon();
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	struct unit_datum *parent;
	struct unit_seat const *seat;

	if (weapon_index != NONE || !unit || unit->object.parent_object_index == NONE ||
		unit->unit.parent_seat_index == NONE)
	{
		return weapon_index;
	}
	parent = unit_get(unit->object.parent_object_index);
	seat = TAG_BLOCK_GET_ELEMENT(&unit_definition_get(parent->definition_index)->unit.seats,
		unit->unit.parent_seat_index, struct unit_seat);
	if (!TEST_FLAG(seat->flags, _unit_seat_gunner_bit) || parent->unit.current_weapon_index == NONE)
		return NONE;
	return unit_inventory_get_weapon(unit->object.parent_object_index, parent->unit.current_weapon_index);
}

/* what is learnt of a weapon's type's model, or NULL */
static struct vr_weapon_fit *vr_weapon_fit(
	long weapon_index)
{
	long definition_index;
	short index;

	if (weapon_index == NONE)
		return NULL;
	definition_index = object_get(weapon_index)->definition_index;
	for (index = 0; index < VR_WEAPON_FITS; index++)
	{
		if (vr_view.fits[index].valid && vr_view.fits[index].definition_index == definition_index)
			return &vr_view.fits[index];
	}
	return NULL;
}

/* the weapon as the hand holds it (LOCAL space): `model`, the aim turned so
that the first-person model's barrel lies along the aim, and the muzzle's
offset from the grip (vr_weapon_muzzle, until the model's is learnt); in
the left hand, the model mirrored */
static void vr_weapon_in_hand(
	float const aim[4],
	float model[4],
	float muzzle[3])
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	struct vr_weapon_fit const *fit = vr_weapon_fit(vr_held_weapon());
	float turn[4], forward[3], fit_muzzle[3], grip[3], from_grip[3];
	int axis;

	if (!fit)
	{
		memcpy(model, aim, sizeof(float) * 4);
		vr_rotate(aim, vr_weapon_muzzle, muzzle);
		return;
	}
	vr_weapon_side(fit->forward, forward);
	vr_weapon_side(fit->muzzle, fit_muzzle);
	vr_weapon_side(vr_weapon_grip, grip);
	vr_quaternion_between(forward, local_forward, turn);
	vr_quaternion_multiply(aim, turn, model);
	/* (the camera is VR_WEAPON_GRIP behind the grip, the muzzle where it
	holds it) */
	for (axis = 0; axis < 3; axis++)
		from_grip[axis] = fit_muzzle[axis] - grip[axis];
	vr_rotate(model, from_grip, muzzle);
}

/* ---------- the eyes */

/* room scale (vr.roomscale): the body's place under the head, in LOCAL
space. It is set under the head as the player starts a thing (stands up
on foot, sits, a cinematic's shot begins, dies). On foot the unit walks
after the head (vr_input.c), and while it does, its camera's moves carry
the body along, so that the room stays still around the head; anywhere
else the body stays. The head's reach from the body: a lean in a seat, a
step on foot, the room elsewhere (without room scale, a lean from the
room's centre) */
static float vr_room_follow(
	struct vr_frame const *frame,
	short local_player_index,
	real_point3d const *anchor,
	real heading)
{
	long player_index = local_player_get_player_index(local_player_index);
	long unit_index = player_index == NONE ? NONE : player_get(player_index)->unit_index;
	struct unit_datum *unit = unit_index == NONE ? NULL : unit_try_and_get(unit_index);
	short state = vr_view.cinematic ? _vr_room_cinematic : !unit ? _vr_room_none :
		unit->object.parent_object_index != NONE ? _vr_room_seated : _vr_room_on_foot;
	boolean walked;

	/* (the unit walked, or coasts after a walk) */
	if (vr_view.room && vr_input_room_walking())
		vr_view.room_coast = VR_ROOM_COAST;
	else
		vr_view.room_coast -= frame->display_period > 0.0f ? frame->display_period : 1.0f / 90.0f;
	walked = vr_view.room_coast > 0.0f;

	vr_view.room = FALSE;
	if (!vr_view.room_scale)
	{
		vr_view.room_body[0] = vr_view.room_body[1] = 0.0f;
		vr_view.room_valid = FALSE;
		vr_view.room_anchor_valid = FALSE;
		return VR_HEAD_REACH;
	}
	if (!vr_view.room_valid || state != vr_view.room_state)
	{
		vr_view.room_body[0] = frame->head.position[0];
		vr_view.room_body[1] = frame->head.position[2];
		vr_view.room_valid = TRUE;
		vr_view.room_state = state;
		vr_view.room_coast = 0.0f;
		walked = FALSE;
	}
	if (walked && vr_view.room_anchor_valid)
	{
		real_vector3d moved;
		float world[3], local[3];

		vector_from_points3d(&vr_view.room_anchor, anchor, &moved);
		moved.k = 0.0f;
		if (magnitude_squared3d(&moved) < VR_ROOM_JUMP * VR_ROOM_JUMP)
		{
			world[0] = (float)(moved.i / VR_UNITS_PER_METRE);
			world[1] = (float)(moved.j / VR_UNITS_PER_METRE);
			world[2] = 0.0f;
			vr_world_to_local(world, heading, local);
			vr_view.room_body[0] += local[0];
			vr_view.room_body[1] += local[2];
		}
	}
	vr_view.room_anchor = *anchor;
	vr_view.room_anchor_valid = TRUE;
	vr_view.room = state == _vr_room_on_foot;
	return state == _vr_room_seated ? VR_HEAD_REACH : state == _vr_room_on_foot ? VR_ROOM_REACH : VR_ROOM_ANYWHERE;
}

/* a cinematic's heading: each shot is seen ahead of where the head faced
as it began, and turns as its camera turns; at a cut (the camera moved or
turned further than it does in a frame) the eyes blink (moistman42069's
way) */
static real vr_cinematic_heading(
	struct vr_frame const *frame,
	struct render_camera const *camera)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float head[3], forward[3];
	real yaw = vr_yaw_of(camera->forward.n, 0.0f);

	if (!vr_view.cinematic ||
		distance_squared3d(&camera->position, &vr_view.cinematic_position) > VR_CUT_DISTANCE * VR_CUT_DISTANCE ||
		fabs(vr_wrap_angle(yaw - vr_view.cinematic_yaw)) > VR_CUT_TURN)
	{
		vr_rotate(frame->head.orientation, local_forward, head);
		vr_local_to_world(head, 0.0f, forward);
		vr_view.cinematic_head_yaw = vr_yaw_of(forward, 0.0f);
		vr_view.room_valid = FALSE;
		vr_blink(VR_CUT_BLINK);
	}
	vr_view.cinematic = TRUE;
	vr_view.cinematic_position = camera->position;
	vr_view.cinematic_yaw = yaw;
	return vr_wrap_angle(yaw - vr_view.cinematic_head_yaw);
}

/* the head's offset from the origin (LOCAL metres), limited to a lean and
stopped short of the walls around the game's camera */
static void vr_head_offset(
	struct vr_frame const *frame,
	real_point3d const *anchor,
	real heading,
	float reach,
	float offset[3])
{
	real_vector3d vector;
	float world[3];
	struct collision_result collision;
	real length;

	offset[0] = frame->head.position[0] - vr_view.room_body[0];
	offset[1] = frame->head.position[1];
	offset[2] = frame->head.position[2] - vr_view.room_body[1];
	/* (on foot the body follows the head across the room, not up and down:
	a crouch, however deep, is the head's own, and the walls test below
	keeps it off the floor) */
	if (reach == VR_ROOM_REACH)
	{
		float height = offset[1];

		offset[1] = 0.0f;
		vr_limit_reach(offset, reach);
		offset[1] = height;
	}
	else
	{
		vr_limit_reach(offset, reach);
	}
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

/* vr.eye_height "player": on foot, the eyes as high over the unit's feet
as the player's are over the floor (the runtime's STAGE), not at the unit's
camera height: the cyborg's 0.62 world units are 1.89 m, a head taller than
most players, so that the world looks smaller than its metres. The unit's
crouch lowers the eyes as it lowers the game's camera, in proportion to the
player's height (the cyborg's crouching camera is 57% of its standing one),
less what the head has already gone down: a crouch of the button lowers
them all the way, the player's own crouch (vr_input.c makes it the unit's)
no further than either. The game's own camera, its shots and its collision
are unchanged */
static void vr_eye_height(
	struct vr_frame const *frame,
	short local_player_index,
	real_point3d *anchor)
{
	long unit_index = player_control_get_unit_index(local_player_index);
	struct biped_datum *biped = unit_index == NONE ? NULL : biped_try_and_get(unit_index);
	struct biped_definition const *definition;
	real crouch, height, lowered, ducked;
	long tick;

	if (!vr_view.eye_height_player || !frame->floor_known || !biped ||
		biped->object.parent_object_index != NONE || TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ||
		director_get_perspective(local_player_index) != _director_perspective_first_person)
	{
		vr_view.eye_crouch_tick = NONE;
		return;
	}
	definition = biped_definition_get(biped->definition_index);
	tick = game_time_get();
	if (tick != vr_view.eye_crouch_tick)
	{
		vr_view.eye_crouch[0] = vr_view.eye_crouch_tick == NONE ? biped->biped.crouch : vr_view.eye_crouch[1];
		vr_view.eye_crouch[1] = biped->biped.crouch;
		vr_view.eye_crouch_tick = tick;
	}
	crouch = vr_view.eye_crouch[0] + (vr_view.eye_crouch[1] - vr_view.eye_crouch[0]) * render_interpolation_fraction();
	height = (1.0f - crouch) * definition->biped.standing_camera_height +
		crouch * definition->biped.crouching_camera_height;
	if (!(definition->biped.standing_camera_height > 0.0f) || !(height > 0.0f))
		return;
	/* (in metres: how far the unit's crouch lowers the player's eyes, and
	how far the head is below where it stood as the runtime last centred) */
	lowered = frame->floor_height * (1.0f - height / definition->biped.standing_camera_height);
	ducked = frame->head.position[1] < 0.0f ? -frame->head.position[1] : 0.0f;
	anchor->z += (frame->floor_height - (lowered > ducked ? lowered - ducked : 0.0f)) * VR_UNITS_PER_METRE - height;
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

/* the heading the eyes turn from: the one the head aims with; else (no
player aims: the main menu's scene, a game's first frame) the game camera's
less the head's yaw as the view began, so that the head turns about the
scene, which turns as the camera does (taken again at a recentre) */
static real vr_heading(
	struct vr_frame const *frame,
	struct render_camera const *camera)
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	float head[3], forward[3];

	if (vr_view.heading_valid)
		return vr_view.heading;
	if (!vr_view.unaimed_valid || vr_view.unaimed_recentres != frame->recentres)
	{
		vr_rotate(frame->head.orientation, local_forward, head);
		vr_local_to_world(head, 0.0f, forward);
		vr_view.unaimed_head_yaw = vr_yaw_of(forward, 0.0f);
		vr_view.unaimed_recentres = frame->recentres;
		vr_view.unaimed_valid = TRUE;
	}
	return vr_wrap_angle(vr_yaw_of(camera->forward.n, 0.0f) - vr_view.unaimed_head_yaw);
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

/* the reticle: where the held weapon's shot meets the world, in LOCAL
space (the eyes' mapping of it reversed), red when the game's crosshair
would be (the aim assist's target fully in the aim) */
static void vr_reticle(
	struct vr_frame const *frame,
	short local_player_index,
	real_point3d const *anchor,
	real heading,
	float const head[3])
{
	real_point3d origin, target;
	real_vector3d forward, ray;
	struct collision_result collision;
	float world[3], local[3], position[3];
	long weapon_index = vr_aimed_weapon();
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	long ignored = vr_view.unit_index;
	int axis;

	if (weapon_index == NONE || !weapon_get_projectile_ray(weapon_index, 0, &origin, &forward))
		return;
	/* (in a seat, the vehicle is not in the way) */
	if (unit && unit->object.parent_object_index != NONE)
		ignored = unit->object.parent_object_index;
	/* (as far as the passengers' reticle looks: 128 units) */
	scale_vector3d(&forward, 128.0f, &ray);
	if (collision_test_vector(_collision_test_for_projectiles_flags, &origin, &ray, ignored, &collision))
		target = collision.point;
	else
		point_from_line3d(&origin, &forward, 128.0f, &target);
	for (axis = 0; axis < 3; axis++)
		world[axis] = (target.n[axis] - anchor->n[axis]) / VR_UNITS_PER_METRE;
	vr_world_to_local(world, heading, local);
	for (axis = 0; axis < 3; axis++)
		position[axis] = frame->head.position[axis] - head[axis] + local[axis];
	vr_reticle_place(position, player_control_get_autoaim_level(local_player_index) >= 1.0f);
}

/* the scope's window, while the aiming hand's weapon is zoomed: from its
muzzle along the aim, with the zoom's field, in a square of the back
buffer's pixels at its top left; and where its picture is held */
static boolean vr_scope_window(
	struct vr_frame const *frame,
	struct render_window const *player,
	real_point3d const *anchor,
	real heading,
	float const head[3],
	struct render_window *window)
{
	struct vr_hand const *hand = &frame->hand[vr_view.main_hand];
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	struct render_camera camera = player->rasterizer_camera;
	struct vr_pose aim = hand->aim, place;
	struct vr_fov field;
	float offset[3], along[3], model[4];
	long width, height;
	real aspect, half;
	int axis;

	if (!unit || unit->unit.current_zoom_level == NONE || !vr_screen_size(&width, &height) || width <= 0 ||
		height <= 0 || !(player->render_camera.vertical_field_of_view > 0.0f))
	{
		return FALSE;
	}
	vr_hand_aim(frame, hand, aim.orientation);
	vr_weapon_in_hand(aim.orientation, model, along);
	for (axis = 0; axis < 3; axis++)
		offset[axis] = head[axis] + hand->grip.position[axis] + along[axis] - frame->head.position[axis];
	vr_camera_from_pose(&camera, &aim, offset, anchor, heading);
	/* (square in pixels: the screen's 640x480 are drawn at the eyes' size) */
	camera.viewport_bounds.x0 = camera.viewport_bounds.y0 = 0;
	camera.viewport_bounds.x1 = (short)(VR_SCOPE_PIXELS * 640L / width);
	camera.viewport_bounds.y1 = (short)(VR_SCOPE_PIXELS * 480L / height);
	camera.window_bounds = camera.viewport_bounds;
	vr_view.scope_window = camera.viewport_bounds;
	aspect = (real)camera.viewport_bounds.x1 / (real)camera.viewport_bounds.y1;
	half = player->render_camera.vertical_field_of_view * 0.5f;
	field.left = field.down = -half;
	field.right = field.up = half;
	vr_fov_bounds(&field, aspect, vr_view.scope_bounds.n);
	*window = *player;
	window->rasterizer_camera = camera;
	window->render_camera = camera;

	vr_weapon_side(vr_scope_offset, offset);
	vr_rotate(aim.orientation, offset, along);
	for (axis = 0; axis < 3; axis++)
		place.position[axis] = hand->grip.position[axis] + along[axis];
	memcpy(place.orientation, aim.orientation, sizeof(place.orientation));
	vr_scope_place(&place, VR_SCOPE_WIDTH);
	return TRUE;
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
	int eye, hand_index;

	vr_view.stereo = FALSE;
	vr_view.weapon_in_hand = FALSE;
	vr_view.scope = FALSE;
	for (window_index = 0; window_index < MAXIMUM_WINDOWS + 1; window_index++)
		vr_view.pass_of_window[window_index] = _vr_render_pass_none;
	frame = vr_frame_get();
	vr_settings_read();
	if (!frame->stereo ||
		window_count != 2 ||
		windows[0].local_player_index == NONE ||
		windows[0].console_window ||
		!windows[1].console_window ||
		(cinematic_in_progress() && !vr_view.immersive_cinematics))
	{
		vr_view.cinematic = FALSE;
		vr_view.room = FALSE;
		return window_count;
	}
	player = windows[0];
	console = windows[1];
	anchor = player.render_camera.position;
	/* (seen from a seat, the eyes at the unit's head as it is drawn: the
	first person's camera there is the unit's standing eye height over its
	origin, inside the vehicle's body; astromaddie's) */
	if (vr_view.seat_view && !cinematic_in_progress())
	{
		struct object_marker marker;

		if (unit_try_and_get(vr_view.unit_index) &&
			object_get_marker_by_name(vr_view.unit_index, "head", &marker, 1) > 0)
		{
			anchor = marker.matrix.position;
		}
	}
	if (!vr_view.seat_view && !cinematic_in_progress())
		vr_eye_height(frame, player.local_player_index, &anchor);
	/* (in a cinematic the head looks around its shot, without a letterbox;
	back in the game, a blink too) */
	if (cinematic_in_progress())
	{
		heading = vr_cinematic_heading(frame, &player.render_camera);
	}
	else
	{
		if (vr_view.cinematic)
			vr_blink(VR_CUT_BLINK);
		vr_view.cinematic = FALSE;
		heading = vr_view.seat_view ? vr_seat_heading(TRUE) : vr_heading(frame, &player.render_camera);
	}
	aspect = (real)(player.render_camera.viewport_bounds.x1 - player.render_camera.viewport_bounds.x0) /
		(real)(player.render_camera.viewport_bounds.y1 - player.render_camera.viewport_bounds.y0);
	vr_head_offset(frame, &anchor, heading, vr_room_follow(frame, player.local_player_index, &anchor, heading), head);
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
	/* the controllers' grips, for the arms (vr_arms.c) */
	vr_view.frame_heading = heading;
	for (hand_index = 0; hand_index < VR_HAND_COUNT; hand_index++)
	{
		struct vr_hand const *grip = &frame->hand[hand_index];
		int axis;

		vr_view.grip_valid[hand_index] = frame->controllers && grip->tracked;
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + grip->grip.position[axis] - frame->head.position[axis];
		vr_view.grips[hand_index] = player.rasterizer_camera;
		vr_camera_from_pose(&vr_view.grips[hand_index], &grip->grip, offset, &anchor, heading);
	}
	/* the weapon's camera, behind the aiming hand: the grip's place less
	VR_WEAPON_GRIP along the axes of the aim, turned so that the model's
	barrel lies along it (vr_weapon_in_hand) */
	vr_view.weapon_in_hand = vr_view.hand_aiming && !vr_view.cinematic;
	if (vr_view.weapon_in_hand)
	{
		struct vr_hand const *hand = &frame->hand[vr_view.main_hand];
		struct vr_pose aim = hand->aim, model;
		float side[3], grip[3], along[3];
		struct render_camera muzzle = player.rasterizer_camera;
		int axis;

		vr_hand_aim(frame, hand, aim.orientation);
		model = aim;
		vr_weapon_in_hand(aim.orientation, model.orientation, along);
		vr_weapon_side(vr_weapon_grip, side);
		vr_rotate(model.orientation, side, grip);
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + hand->grip.position[axis] - grip[axis] - frame->head.position[axis];
		vr_view.weapon_camera = player.rasterizer_camera;
		vr_camera_from_pose(&vr_view.weapon_camera, &model, offset, &anchor, heading);
		/* (the muzzle's place: a camera there, its position) */
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + hand->grip.position[axis] + along[axis] - frame->head.position[axis];
		vr_camera_from_pose(&muzzle, &aim, offset, &anchor, heading);
		vr_view.muzzle = muzzle.position;
	}
	if (vr_view.weapon_in_hand || (vr_view.seat_aiming && !vr_view.cinematic))
		vr_reticle(frame, player.local_player_index, &anchor, heading, head);
	window_index = 2;
	vr_view.scope = vr_view.weapon_in_hand && vr_scope_window(frame, &player, &anchor, heading, head,
		&windows[window_index]);
	if (vr_view.scope)
		vr_view.pass_of_window[window_index++] = _vr_render_pass_scope;
	windows[window_index] = player;
	windows[window_index].rasterizer_camera = vr_view.head_camera;
	windows[window_index].render_camera = vr_view.head_camera;
	vr_hud_bounds(&vr_view.head_camera, &vr_view.hud_bounds);
	vr_view.pass_of_window[window_index++] = _vr_render_pass_hud;
	windows[window_index++] = console;
	vr_view.stereo = TRUE;
	return window_index;
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
	else if (VR_RENDER_SCOPE())
		halo_vr_resolve_scope(vr_view.scope_window.x0, vr_view.scope_window.y0, vr_view.scope_window.x1,
			vr_view.scope_window.y1);
	vr_render_pass = _vr_render_pass_none;
}

void vr_render_frustum_bounds(
	union real_rectangle2d *bounds)
{
	if (VR_RENDER_EYE())
		*bounds = vr_view.eye_bounds[vr_render_pass == _vr_render_pass_right_eye];
	else if (VR_RENDER_HUD())
		*bounds = vr_view.hud_bounds;
	else if (VR_RENDER_SCOPE())
		*bounds = vr_view.scope_bounds;
}

void vr_render_weapon_camera(
	struct render_camera *camera)
{
	struct render_camera const *pose = vr_view.weapon_in_hand ? &vr_view.weapon_camera : &vr_view.head_camera;

	if (!vr_view.stereo)
		return;
	camera->position = pose->position;
	camera->forward = pose->forward;
	camera->up = pose->up;
}

/* where the model's barrel is, from its pose before it is mirrored */
static void vr_weapon_learn(
	void)
{
	struct render_camera const *camera = &vr_view.weapon_camera;
	struct vr_weapon_fit *fit;
	struct object_marker marker;
	real_vector3d left, relative;
	float muzzle[3], forward[3], length;
	long weapon_index;
	int axis;

	weapon_index = vr_held_weapon();
	if (weapon_index == NONE || !first_person_weapon_idle(render.local_player_index) ||
		first_person_weapon_get_marker_by_name(weapon_index, "primary trigger", &marker, 1) < 1)
	{
		return;
	}
	/* (the game's forward, left and up are LOCAL's -z, -x and y) */
	cross_product3d(&camera->up, &camera->forward, &left);
	vector_from_points3d(&camera->position, &marker.matrix.position, &relative);
	muzzle[0] = (float)(-dot_product3d(&relative, &left) / VR_UNITS_PER_METRE);
	muzzle[1] = (float)(dot_product3d(&relative, &camera->up) / VR_UNITS_PER_METRE);
	muzzle[2] = (float)(-dot_product3d(&relative, &camera->forward) / VR_UNITS_PER_METRE);
	forward[0] = (float)-dot_product3d(&marker.matrix.forward, &left);
	forward[1] = (float)dot_product3d(&marker.matrix.forward, &camera->up);
	forward[2] = (float)-dot_product3d(&marker.matrix.forward, &camera->forward);
	length = (float)sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
	/* (not a barrel: one more than 45 degrees from the camera's forward, or
	a muzzle more than a metre from it) */
	if (!vr_finite(muzzle, 3) || !(length > 1e-4f) || !(forward[2] < -0.7f * length) ||
		muzzle[0] * muzzle[0] + muzzle[1] * muzzle[1] + muzzle[2] * muzzle[2] > 1.0f)
	{
		return;
	}
	for (axis = 0; axis < 3; axis++)
		forward[axis] /= length;
	fit = vr_weapon_fit(weapon_index);
	if (!fit)
	{
		fit = &vr_view.fits[vr_view.next_fit];
		vr_view.next_fit = (short)((vr_view.next_fit + 1) % VR_WEAPON_FITS);
		fit->valid = TRUE;
		fit->definition_index = object_get(weapon_index)->definition_index;
		memcpy(fit->muzzle, muzzle, sizeof(muzzle));
		memcpy(fit->forward, forward, sizeof(forward));
		platform_log("vr: weapon %lx's barrel at %.3f %.3f %.3f, along %.3f %.3f %.3f", fit->definition_index,
			(double)muzzle[0], (double)muzzle[1], (double)muzzle[2], (double)forward[0], (double)forward[1],
			(double)forward[2]);
		return;
	}
	for (axis = 0; axis < 3; axis++)
	{
		fit->muzzle[axis] += (muzzle[axis] - fit->muzzle[axis]) * VR_WEAPON_FIT_EASE;
		fit->forward[axis] += (forward[axis] - fit->forward[axis]) * VR_WEAPON_FIT_EASE;
	}
	length = (float)sqrt(fit->forward[0] * fit->forward[0] + fit->forward[1] * fit->forward[1] +
		fit->forward[2] * fit->forward[2]);
	for (axis = 0; axis < 3; axis++)
		fit->forward[axis] /= length;
}

/* the model, the arms' too, mirrored across the weapon camera's upright
plane (the camera's left its normal): the right hand's held in the left
(astromaddie's way); markers on it, its muzzle's and flashlight's, follow */
static void vr_weapon_mirror(
	void)
{
	struct render_camera const *camera = &vr_view.weapon_camera;
	real_matrix4x3 *matrices;
	real_vector3d normal;
	short node_count, index;

	matrices = first_person_weapon_get_node_matrices(render.local_player_index, &node_count);
	cross_product3d(&camera->up, &camera->forward, &normal);
	if (!(normalize3d(&normal) > 0.0f))
		return;
	for (index = 0; index < node_count; index++)
	{
		real_matrix4x3 *matrix = &matrices[index];
		real_vector3d *axes[3];
		real_vector3d offset;
		real along;
		int axis;

		axes[0] = &matrix->forward;
		axes[1] = &matrix->left;
		axes[2] = &matrix->up;
		vector_from_points3d(&camera->position, &matrix->position, &offset);
		along = 2.0f * dot_product3d(&offset, &normal);
		matrix->position.x -= normal.i * along;
		matrix->position.y -= normal.j * along;
		matrix->position.z -= normal.k * along;
		for (axis = 0; axis < 3; axis++)
		{
			along = 2.0f * dot_product3d(axes[axis], &normal);
			axes[axis]->i -= normal.i * along;
			axes[axis]->j -= normal.j * along;
			axes[axis]->k -= normal.k * along;
		}
	}
}

/* each arm its own (vr.arms "free"): from its shoulder, the weapon's hand
holding the weapon, the other its controller (vr_arms.c) */
static void vr_arms_update(
	void)
{
	struct vr_frame const *frame = vr_frame_get();
	struct animation_graph *graph = first_person_weapon_get_animation_graph(render.local_player_index);
	struct vr_arms_pose pose;
	real_matrix4x3 *matrices;
	short count, main_hand = vr_view.main_hand, off_hand = main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT;
	float orientation[4];

	if (!vr_view.arms_free || !graph)
		return;
	matrices = first_person_weapon_get_node_matrices(render.local_player_index, &count);
	pose.head = vr_view.head_camera.position;
	pose.head_forward = vr_view.head_camera.forward;
	pose.head_up = vr_view.head_camera.up;
	pose.heading = vr_view.frame_heading;
	pose.main_left = main_hand == VR_HAND_LEFT;
	pose.main_valid = vr_view.grip_valid[main_hand];
	pose.main_grip = vr_view.grips[main_hand].position;
	pose.main_forward = vr_view.grips[main_hand].forward;
	pose.main_up = vr_view.grips[main_hand].up;
	pose.off_valid = vr_view.grip_valid[off_hand];
	pose.off_grip = vr_view.grips[off_hand].position;
	pose.off_forward = vr_view.grips[off_hand].forward;
	pose.off_up = vr_view.grips[off_hand].up;
	pose.off_on_weapon = vr_two_handed(frame, pose.main_left, orientation) ||
		first_person_weapon_off_hand_busy(render.local_player_index);
	pose.seconds = frame->display_period;
	vr_arms_pose(matrices, count, graph, &pose);
}

void vr_render_weapon_posed(
	void)
{
	if (!vr_view.stereo || !vr_view.weapon_in_hand || render.local_player_index == NONE)
		return;
	vr_weapon_learn();
	if (vr_view.main_hand == VR_HAND_LEFT)
		vr_weapon_mirror();
	vr_arms_update();
}

void vr_render_weapon_drawing(
	int drawing)
{
	static boolean mirrored, both_faces;
	boolean mirror = drawing && VR_RENDER_WORLD() && vr_view.stereo && vr_view.weapon_in_hand &&
		vr_view.main_hand == VR_HAND_LEFT;
	/* (the arms posed from the player's shoulders show sides of them the
	stock view never shows, which the model has no faces for: their backs
	are drawn too) */
	boolean two_sided = drawing && VR_RENDER_WORLD() && vr_view.stereo && vr_view.weapon_in_hand &&
		vr_view.arms_free;

	if (mirror != mirrored)
	{
		mirrored = mirror;
		halo_vr_mirror_winding(mirror);
	}
	if (two_sided != both_faces)
	{
		both_faces = two_sided;
		halo_vr_two_sided(two_sided);
	}
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
	float head[3], forward[3], aim[3];
	/* (taken every frame, so that no turn waits for the next) */
	real stick_turn = vr_input_turn();
	struct vr_hand const *hand;

	vr_view.aiming = FALSE;
	vr_view.hand_aiming = FALSE;
	vr_view.seat_stick = FALSE;
	vr_view.seat_aiming = FALSE;
	/* (the first local player's alone: the headset is one; in a cinematic
	the scripts' facing is kept, and turns the heading after it) */
	if (!frame->stereo || local_player_index != local_player_get_next(NONE) ||
		local_player_get_player_index(local_player_index) == NONE || cinematic_in_progress())
	{
		return;
	}
	angles = player_control_get_facing_angles(local_player_index);
	before = *angles;
	vr_rotate(frame->head.orientation, local_forward, head);
	vr_local_to_world(head, 0.0f, forward);
	vr_view.driving = FALSE;
	if (vr_seat_view_update(local_player_index, forward, before.yaw, stick_turn))
	{
		/* in a seat seen from it the vehicle turns the heading, and the
		controller's stick turns the seat's view */
		vr_view.heading = vr_seat_heading(FALSE);
	}
	else
	{
		/* the game's turns since the head last aimed (the input's this frame,
		a respawn's, a script's) turn the heading, and the controller's stick
		turns it too */
		if (!vr_view.heading_valid)
		{
			vr_view.heading = vr_wrap_angle(before.yaw - vr_yaw_of(forward, 0.0f));
			vr_view.heading_valid = TRUE;
		}
		else
		{
			vr_view.heading = vr_wrap_angle(vr_view.heading + vr_wrap_angle(before.yaw - vr_view.set_yaw));
		}
		vr_view.heading = vr_wrap_angle(vr_view.heading + stick_turn);
	}
	hand = vr_aiming_hand(frame, local_player_index);
	if (hand)
	{
		float orientation[4];

		vr_hand_aim(frame, hand, orientation);
		vr_rotate(orientation, local_forward, aim);
	}
	else if (vr_view.seat_view && vr_drive_facing(frame, forward, &facing))
	{
		/* a driver steers with the off stick; the head looks around */
		player_control_set_facing(local_player_index, &facing);
		vr_view.set_yaw = player_control_get_facing_angles(local_player_index)->yaw;
		vr_view.driving = TRUE;
		/* (no aim assist turning the facing, and so the wheels) */
		vr_view.aiming = TRUE;
		vr_view.seat_aiming = TRUE;
		vr_input_aim_turn(0.0f);
		return;
	}
	else if (vr_seat_stick(frame, local_player_index))
	{
		/* in a seat the main stick turns the facing, as a pad's right stick
		does, and the head looks around: the view turns with the facing (or,
		seen from the seat, with the vehicle) */
		vr_view.set_yaw = before.yaw;
		vr_view.seat_stick = TRUE;
		vr_view.seat_aiming = TRUE;
		vr_input_aim_turn(0.0f);
		return;
	}
	else if (vr_view.seat_aim == _vr_seat_aim_hand && frame->controllers &&
		frame->hand[vr_view.main_hand].tracked && player_control_get_unit_index(local_player_index) != NONE &&
		unit_get(player_control_get_unit_index(local_player_index))->object.parent_object_index != NONE)
	{
		/* in a seat (a gunner's, a flier's, a tank's) the hand points where
		the vehicle aims or flies */
		float orientation[4];

		vr_hand_aim(frame, &frame->hand[vr_view.main_hand], orientation);
		vr_rotate(orientation, local_forward, aim);
		vr_view.seat_aiming = TRUE;
	}
	else
		memcpy(aim, head, sizeof(aim));
	vr_local_to_world(aim, vr_view.heading, facing.n);
	/* (straight up or down the game's angles have no yaw: kept as it was) */
	if (facing.i * facing.i + facing.j * facing.j < 1e-6f)
		return;
	player_control_set_facing(local_player_index, &facing);
	angles = player_control_get_facing_angles(local_player_index);
	vr_view.set_yaw = angles->yaw;
	vr_view.aiming = TRUE;
	vr_view.hand_aiming = hand != NULL;
	/* (the off stick then moves where the head faces, not the hand) */
	if (hand)
	{
		float aim_forward[3];

		vr_local_to_world(aim, 0.0f, aim_forward);
		vr_input_aim_turn(vr_wrap_angle(vr_yaw_of(forward, 0.0f) - vr_yaw_of(aim_forward, 0.0f)));
	}
	else
	{
		vr_input_aim_turn(0.0f);
	}

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

int vr_render_zoom_hidden(
	void)
{
	return vr_view.scope;
}

int vr_render_reticle(
	void)
{
	return (vr_view.aiming && vr_view.hand_aiming) || vr_view.seat_aiming;
}

int vr_render_seat_stick(
	void)
{
	return vr_view.seat_stick;
}

int vr_render_drive(
	void)
{
	return vr_view.driving;
}

int vr_render_seat_weapon_hidden(
	void)
{
	long vehicle_index;
	struct unit_seat const *seat;

	return vr_view.stereo && vr_view.seat_view && vr_seat_of(vr_view.unit_index, &vehicle_index, &seat) &&
		(TEST_FLAG(seat->flags, _unit_seat_driver_bit) || TEST_FLAG(seat->flags, _unit_seat_gunner_bit));
}

int vr_render_room(
	float offset[2])
{
	struct vr_frame const *frame = vr_frame_get();

	if (!vr_view.room)
		return 0;
	offset[0] = frame->head.position[0] - vr_view.room_body[0];
	offset[1] = frame->head.position[2] - vr_view.room_body[1];
	return 1;
}

int vr_render_hand_holds(
	long unit_index)
{
	return vr_view.aiming && vr_view.hand_aiming && unit_index != NONE && unit_index == vr_view.unit_index;
}

struct icon_hud_element_definition const *vr_render_prompt_icon(
	struct icon_hud_element_definition const *icon)
{
	/* the HUD's first button icons are A, B, X, Y, black and white: the
	controllers' A (jump), grip (melee: B's icon kept), B (action), Y
	(weapons), X (grenades) and grip (the flashlight: white's kept), for the
	right-handed or the left */
	static short const shown[6] = { 0, 1, 1, 3, 2, 5 };
	struct icon_hud_element_definition const *buttons;
	long index;

	if (!vr_active() || !vr_frame_get()->controllers || !hud_globals)
		return icon;
	buttons = hud_globals->messaging.button_icons.address;
	if (!buttons || icon < buttons)
		return icon;
	index = (long)(icon - buttons);
	if (index >= NUMBEROF(shown) || shown[index] >= hud_globals->messaging.button_icons.count)
		return icon;
	return buttons + shown[index];
}

/* every player is this machine's: no other machine plays a shot of
theirs from the facing alone (a network game's host with no one joined is
as a local game) */
static boolean vr_players_all_here(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (game_connection() == _game_connection_network_client)
		return FALSE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->local_player_index == NONE)
			return FALSE;
	}
	return TRUE;
}

int vr_render_shot_from_hand(
	long unit_index)
{
	real_point3d eye;
	real_vector3d vector;
	struct collision_result collision;

	if (!vr_view.weapon_in_hand || !vr_view.hand_aiming || unit_index == NONE || unit_index != vr_view.unit_index ||
		!vr_players_all_here())
	{
		return FALSE;
	}
	unit_get_camera_position(unit_index, &eye);
	vector_from_points3d(&eye, &vr_view.muzzle, &vector);
	return !collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&eye, &vector, unit_index, &collision);
}

int vr_render_shot_origin(
	long unit_index,
	float origin[3])
{
	if (!vr_render_shot_from_hand(unit_index))
		return FALSE;
	origin[0] = vr_view.muzzle.x;
	origin[1] = vr_view.muzzle.y;
	origin[2] = vr_view.muzzle.z;
	return TRUE;
}

/* the game state was loaded or the map changed: the heading is taken
again from the game's facing */
void vr_view_reset(
	void)
{
	vr_view.heading_valid = FALSE;
	vr_view.unaimed_valid = FALSE;
	vr_view.stereo = FALSE;
	vr_view.hand_aiming = FALSE;
	vr_view.weapon_in_hand = FALSE;
	vr_view.seat_stick = FALSE;
	vr_view.seat_aiming = FALSE;
	vr_view.seat_view = FALSE;
	vr_view.driving = FALSE;
	vr_view.eye_crouch_tick = NONE;
	vr_view.cinematic = FALSE;
	vr_arms_reset();
	vr_view.room = FALSE;
	vr_view.room_valid = FALSE;
	vr_view.room_anchor_valid = FALSE;
	/* (a new map's weapon types are others) */
	memset(vr_view.fits, 0, sizeof(vr_view.fits));
	vr_view.next_fit = 0;
	vr_render_pass = _vr_render_pass_none;
}

#endif
