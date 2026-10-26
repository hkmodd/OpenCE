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
grip sits from the game's own camera), along where the hand points (its
aim pose, the runtime's pointing ray for the controller; vr.aim_pitch
turns it),
so that it is held where the controller is; and in place of the HUD's crosshair a reticle is
held in the world where the barrel, as the hand holds it this frame, meets
it: the weapon's own crosshair, drawn alone by the HUD (vr_render_hud_begin),
red over a target as in the game (vr_reticle).

The scope. While the hand's weapon is zoomed, a window more is drawn
before the HUD's: the world from the muzzle along the aim, into a square of
the back buffer, whose picture is the lens of a sight held on the weapon
(vr_openxr.c), its field the lens's as the eyes see it by the zoom's
magnification, so that it shows the world magnified; the eyes are not
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
#include "game/aim_assist.h"
#include "interface/first_person_weapons.h"
#include "interface/hud.h"
#include "items/items.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "models/model_animation_definitions.h"
#include "objects/objects.h"
#include "interface/hud_definitions.h"
#include "interface/hud_weapon.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "tag_files/tag_files.h"
#include "render/render_cameras.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/unit_definitions.h"
#include "units/units.h"
#include "units/vehicle_datum.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "network_vr.h"
#include "dual_wield.h"
#include "passenger_camera.h"

#include <math.h>

double config_real(const char *name);
int config_boolean(const char *name);
int config_write(const char *name, const char *value);
const char *config_string(const char *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);
/* d3d8_gl.c: the back buffer cleared to nothing (the HUD's ground), and
the back buffer's picture into an eye's image */
void halo_vr_clear_transparent(void);
void halo_vr_mirror_winding(int mirrored);
void halo_vr_two_sided(int two_sided);
void halo_vr_resolve_eye(int eye);
/* ... and a rectangle of it (the screen's coordinates) into the scope's,
or the reticle's picture */
void halo_vr_resolve_scope(short x0, short y0, short x1, short y1);
void halo_vr_resolve_reticle(short reticle, short x0, short y0, short x1, short y1);

/* the body's neck under the head (first_person_legs.c): this far behind
the eyes, along the body (metres: further than a person's, 10 cm, as the
armour's chest stands out further than a chest; looking down, the eyes
look past it at the feet), and this far below them (a person's eyes over
the base of their neck) */
#define VR_LEGS_BACK 0.25f
#define VR_LEGS_NECK_DROP 0.22f
/* a weapon on the ground within this of a hand's grip (along its length,
metres) is in its reach, for the grip to take */
#define VR_GRAB_REACH 0.25f

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
/* the comfort vignette (vr.vignette): closed fully at this speed of the
view's own (metres a second) or this turn (radians a second), in this long
(seconds) and opened again in this long; a frame's move past this far
(metres) or turn past this (radians: a snap turn) is a jump, not motion */
#define VR_VIGNETTE_SPEED 3.0f
#define VR_VIGNETTE_TURN 1.5f
#define VR_VIGNETTE_CLOSE 0.1f
#define VR_VIGNETTE_OPEN 0.3f
#define VR_VIGNETTE_JUMP 1.0f
#define VR_VIGNETTE_SNAP 0.35f

/* a turn of the head in a frame (radians) that counts as looking around,
more than its tremor: about 15 degrees a second at 60 frames */
#define VR_LOOK_TURN 0.004f

/* where the first-person weapon's grip is from the game's camera, in
metres: right, up and back (the aim's axes); and its muzzle from the grip */
static float const vr_weapon_grip[3] = { 0.10f, -0.12f, -0.20f };
static float const vr_weapon_muzzle[3] = { 0.0f, 0.04f, -0.25f };
/* the scope's lens: where it is held from the grip (on the weapon, above
it and a little ahead: its sight), its width in metres, and its side in the
back buffer's pixels */
static float const vr_scope_offset[3] = { 0.0f, 0.10f, -0.06f };
#define VR_SCOPE_WIDTH 0.05f
#define VR_SCOPE_PIXELS 512
/* the reticle: the game's crosshair drawn alone, its square about the
HUD's middle (half its side, in the HUD's 640 by 480) */
#define VR_CROSSHAIR_HALF 64

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
/* a gunner's seat: the handles' distance behind the gun's pivot (metres):
the hands moved 10 cm turn the gun about 22 degrees */
#define VR_HANDLES_LEVER 0.25f
/* the off stick's full turn of a Ghost's facing from its forward (degrees):
a Ghost turns toward its facing however far it is (it has no wheels whose
turn limits it: vehicles.c's update_alien_scout_physics), short of the
hard turn (45 degrees) that slows it on the ground */
#define VR_DRIVE_SCOUT_TURN 40.0f

/* a vehicle's definition as far as its type and its wheels' turn
(vehicles.c's layout; its header keeps it opaque, as input_abstraction.c's
does): the types the off stick steers, and the wheels' turn's limits in
degrees, left positive and right negative (a Warthog's) */
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
	/* the reticles placed this frame (vr_reticle: the weapon in hand's
	and the other hand's weapon's, and whether that one is on a target),
	and the game's crosshair is being drawn for them (vr_render_hud_begin);
	the barrel's direction in the world, as the hand holds it this frame */
	boolean reticle_placed[VR_RETICLE_COUNT];
	boolean other_reticle_aimed;
	boolean drawing_reticle;
	real_vector3d barrel;
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
	real vignette_strength;
	real aim_pitch;

	/* the weapon in the hand is held as a pistol (vr_pistol_grip) */
	boolean pistol_grip;

	/* the hands bare: the weapon put away at a shoulder (vr_shoulders); the
	inventory's slot each shoulder keeps (VR_HAND_LEFT, VR_HAND_RIGHT), and
	the unit it is for; the main grip's presses at a shoulder taken; the
	weapon held the last frame; a weapon on the ground within each hand's
	reach (vr_grab_reach) */
	boolean bare;
	/* the rules played by (vr_rules_active), as last logged: -1 none yet */
	int rules;
	short shoulder_slot[VR_HAND_COUNT];
	long shoulder_unit;
	/* by a VR server's rules, with the hands as they hold, each shoulder
	keeps one weapon or none (the places, vr_places), shoulder_slot NONE
	for none: the slot the main hand holds (NONE: none), set as it is drawn
	or passed to it and taken from the game's once that is readied (or a
	second on), and when; the weapon last let fall and when (no shoulder
	keeps it for a second: the host's word that it is gone comes later);
	the slot the hand held as the other took one from the ground, and the
	one the other hand then goes on holding; the inventory's weapons the
	last frame (to see the one a grab took come); the other hand's lettings
	go taken */
	boolean places;
	short hand_slot;
	long hand_slot_time;
	long falling_weapon;
	long falling_time;
	short grab_previous_slot;
	short off_keep_slot;
	long inventory[MAXIMUM_WEAPONS_PER_UNIT];
	unsigned long off_releases;
	unsigned long shoulder_grabs;
	long held_weapon;
	boolean grab_reach[VR_HAND_COUNT];
	/* the grenades thrown with the arm, as last seen (vr_input.c) */
	unsigned long throws;

	/* the comfort vignette: where LOCAL's origin was in the world and the
	heading, the last frame, the runtime's recentres then, and how far it is
	closed */
	boolean vignette_valid;
	unsigned long vignette_recentres;
	real_point3d vignette_origin;
	real vignette_heading;
	real vignette;

	/* vr.eye_height: the unit's crouch at the last two ticks (blended as
	the drawn camera is) and the tick of the later; the head's highest
	since the runtime last recentred (LOCAL's y) and the recentres then */
	real eye_crouch[2];
	long eye_crouch_tick;
	boolean head_top_valid;
	real head_top;
	unsigned long head_top_recentres;

	/* the body's forward this frame (the world's, level: vr_arms_body) */
	real_vector3d body_forward;

	/* this frame's heading and the controllers' grips in the world, for
	the arms (vr_arms.c) */
	real frame_heading;
	boolean grip_valid[VR_HAND_COUNT];
	struct render_camera grips[VR_HAND_COUNT];
	/* each hand at its grip, along where it points (vr_hand_pointing), as
	the other machines pose it (network_vr.c) */
	struct render_camera pointings[VR_HAND_COUNT];
	/* the arms are mirrored this frame (the weapon in the left hand:
	vr_arms_mirror) */
	boolean arms_mirrored;
	/* the hands as the last weapon's animations posed them (the graph, its
	nodes' count, the camera they were posed from), and with no weapon left
	their pose this frame, carried by the hand (vr_render_empty_hands) */
	boolean empty_valid;
	struct animation_graph *empty_graph;
	short empty_count;
	struct render_camera empty_camera;
	real_matrix4x3 empty_base[MAXIMUM_NODES_PER_ANIMATION];
	boolean empty_posed;
	real_matrix4x3 empty_pose[MAXIMUM_NODES_PER_ANIMATION];
	/* the other hand's grip as a weapon it holds is held (dual_wield.c):
	at the grip, along where the hand points */
	boolean off_grip_valid;
	struct render_camera off_grip;
	/* the weapon the other hand holds (dual_wield.c), posed and drawn in
	first person as the weapon in hand is (first_person_weapons.c's other
	hand's): its camera behind that hand, its muzzle and its barrel's way in
	the world, as the hand holds it this frame, and its arms mirrored (in the
	left hand) */
	boolean other_valid;
	struct render_camera other_camera;
	real_point3d other_muzzle;
	real_vector3d other_barrel;
	boolean other_arms_mirrored;
	/* the times the other hand's weapon went to the main hand, as last
	seen (vr_input.c) */
	unsigned long off_transfers;

	/* in a seat seen from it (vr.vehicle_view "first_person"): the vehicle
	and the seat, the view's yaw from the vehicle's (the seat's own turn, less
	the head's as the player sat, and the stick's turns since), and whether
	the off stick steers it this frame */
	boolean seat_view;
	long seat_vehicle_index;
	short seat_index;
	real seat_yaw;
	boolean driving;
	/* a gunner's seat seen from it, aimed by the hands on the gun's handles
	(vr_handles): the hands that hold them (a bit for each, 0 let go), the
	gun's pivot in LOCAL space, placed as they took them, and whether they
	have yet (till then the gun faces ahead of the seat's view) */
	int handles_held;
	float handles_pivot[3];
	boolean handles_taken;
	/* the seat has a view of the game's own behind it, which the move
	stick's click switches to and from (vr_seat_view_switch); the clicks
	taken (vr_input.c's) */
	boolean seat_switch;
	unsigned long view_switches;
	/* a pistol held with both hands (vr_two_hands_steady): the other hand's
	pointing's turn from the main hand's as it took hold, the aim steadied,
	and the frame it was last taken for */
	float steady_offset[4];
	struct vr_steady steady;
	float steady_aim[4];
	unsigned long steady_frame;
	/* the eyes' place in the seat (vr_seat_eye), in its node's frame, and
	whether it is taken */
	real_point3d seat_eye;
	boolean seat_eye_valid;

	/* room scale: where the body stands in LOCAL space (x and z), what the
	player is doing (VR_ROOM_*), whether the unit walks after the head this
	frame, and the game's camera last drawn */
	boolean room_valid;
	short room_state;
	float room_body[2];
	/* ... and in a seat, the head's height as the player sat (LOCAL y), a
	lean's up and down taken from it */
	float room_body_height;
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
	vr_view.eye_height_player = strcmp(config_string("vr.eye_height"), "player") == 0;
	vr_view.arms_free = strcmp(config_string("vr.arms"), "weapon") != 0;
	vr_view.aim_pitch = (real)config_real("vr.aim_pitch");
	vr_view.aim_pitch = PIN(vr_view.aim_pitch, -45.0f, 45.0f) * (_pi / 180.0f);
	if (!(vr_view.aim_pitch == vr_view.aim_pitch))
		vr_view.aim_pitch = 0.0f;
	vr_view.vignette_strength = (real)config_real("vr.vignette");
	vr_view.vignette_strength = PIN(vr_view.vignette_strength, 0.0f, 1.0f);
}

static boolean vr_gunner_seat(long unit_index);

/* the hand that aims the weapon it holds: the main controller, tracked,
with vr.aim "hand", the player on foot or, in a seat seen from it with
vr.vehicle_aim "hand", holding a weapon (a passenger: not a gunner, whose
hands hold the gun's handles); else NULL */
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
		(!vr_view.seat_view || vr_view.seat_aim != _vr_seat_aim_hand || vr_gunner_seat(unit_index) ||
			unit->unit.current_weapon_index < 0 ||
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

/* the unit is in a gunner's seat (not a driver's: a tank's cannon, a
flier's guns) seen from it, with vr.vehicle_aim "hand": the hands hold the
gun's handles (vr_handles), not a weapon of their own */
static boolean vr_gunner_seat(
	long unit_index)
{
	long vehicle_index;
	struct unit_seat const *seat;

	return vr_view.seat_view && vr_view.seat_aim == _vr_seat_aim_hand &&
		vr_seat_of(unit_index, &vehicle_index, &seat) && TEST_FLAG(seat->flags, _unit_seat_gunner_bit) &&
		!TEST_FLAG(seat->flags, _unit_seat_driver_bit);
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

/* where the eyes are in a seat seen from it: the seated unit's head as it
sat down (its entry animation over), held there in the frame of its seat's
node turned about the vehicle's up alone. Not the head as drawn, whose
animations carry the eyes about while the player's head is still: a
gunner's with the gun's elevation (the eyes sinking as the gun aims up,
its breech rising into them), a Ghost's driver's leaning into a turn and
bowing as the facing pitches with the head. A turret's seat (a Warthog's
gun's) still carries them round as the gun turns, as its gunner stands
behind it. FALSE: no head to take it from */
static boolean vr_seat_eye(
	real_point3d *eye)
{
	long vehicle_index;
	struct unit_seat const *seat;
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	struct object_marker head, marker;
	real_matrix4x3 const *root, *node;
	real_point3d origin, place;
	real_vector3d forward;
	real yaw, cosine, sine;
	short node_index = 0;

	if (!unit || !vr_seat_of(vr_view.unit_index, &vehicle_index, &seat) ||
		object_get_marker_by_name(vr_view.unit_index, "head", &head, 1) <= 0)
	{
		return FALSE;
	}
	if (object_get_marker_by_name(vehicle_index, seat->marker_name, &marker, 1) > 0)
		node_index = marker.node_index;
	/* (the seat's node in the vehicle's root node's frame: its origin, and
	its turn about the up) */
	root = object_get_node_matrix(vehicle_index, 0);
	node = object_get_node_matrix(vehicle_index, node_index);
	matrix4x3_inverse_transform_point(root, &node->position, &origin);
	matrix4x3_inverse_transform_normal(root, &node->forward, &forward);
	yaw = forward.i * forward.i + forward.j * forward.j > 1e-6f ? (real)atan2(forward.j, forward.i) : 0.0f;
	cosine = (real)cos(yaw);
	sine = (real)sin(yaw);
	if (!vr_view.seat_eye_valid || unit->unit.animation.state == _unit_state_entering_seat)
	{
		matrix4x3_inverse_transform_point(root, &head.matrix.position, &place);
		place.x -= origin.x;
		place.y -= origin.y;
		vr_view.seat_eye.x = place.x * cosine + place.y * sine;
		vr_view.seat_eye.y = place.y * cosine - place.x * sine;
		vr_view.seat_eye.z = place.z - origin.z;
		vr_view.seat_eye_valid = unit->unit.animation.state != _unit_state_entering_seat;
	}
	place.x = origin.x + vr_view.seat_eye.x * cosine - vr_view.seat_eye.y * sine;
	place.y = origin.y + vr_view.seat_eye.x * sine + vr_view.seat_eye.y * cosine;
	place.z = origin.z + vr_view.seat_eye.z;
	matrix4x3_transform_point(root, &place, eye);
	return TRUE;
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

/* following_camera_update: the headset's player's camera behind a vehicle
is level. The eyes pitch with the head; a camera pitched with the facing
(the head's pitch, driving: vr_drive_facing) went up and down round the
vehicle as the head nodded, a tick or two after it. For the local view
only: the shots' deterministic camera is the unit's aim
(following_camera_deterministic) */
int vr_render_chase_level(
	short local_player_index)
{
	return vr_active() && local_player_index == local_player_get_next(NONE) && !cinematic_in_progress();
}

/* as the game takes the player's input: in a seat with a view of the
game's own behind it (the seat's third-person camera, or the passenger
camera: passenger_camera.c), a click of the move stick (vr_input.c)
switches between the seat's view and that one. vr.vehicle_view is written,
as the settings' screen writes it, so that it holds for the next seat and
the next start; the eyes blink, and the director cuts between the cameras
(director_choose_game_perspective) */
static void vr_seat_view_switch(
	short local_player_index)
{
	long unit_index = player_control_get_unit_index(local_player_index);
	long vehicle_index;
	struct unit_seat const *seat;
	unsigned long switches = vr_input_view_switches();
	boolean seated = vr_seat_of(unit_index, &vehicle_index, &seat);

	/* (the seated unit, for the drive's facing seen from either view) */
	if (seated)
		vr_view.unit_index = unit_index;
	vr_view.seat_switch = seated &&
		(TEST_FLAG(seat->flags, _unit_seat_third_person_camera_bit) || passenger_camera_enabled(local_player_index));
	/* (a click taken as the seat was left is dropped) */
	if (switches == vr_view.view_switches)
		return;
	vr_view.view_switches = switches;
	if (!vr_view.seat_switch)
		return;
	vr_settings_read();
	if (!config_write("vr.vehicle_view", vr_view.seat_first_person ? "chase" : "first_person"))
		platform_log("vr: vr.vehicle_view set for now, not written to config.toml");
	vr_blink(VR_SEAT_BLINK);
	platform_log("vr: vehicle %lx's seat %d seen %s (the move stick's click)", vehicle_index,
		(int)unit_get(unit_index)->unit.parent_seat_index, vr_view.seat_first_person ? "from behind" : "from it");
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
		vr_view.handles_held = 0;
		vr_view.handles_taken = FALSE;
		vr_view.seat_eye_valid = FALSE;
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
	float const *stick = frame->hand[vr_input_move_hand()].stick;
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
	/* (a Warthog's wheels turn as far as the facing, reversed backing up, as
	vehicles.c turns them; a Ghost turns toward it, either way) */
	yaw = vr_yaw_of(vehicle->object.forward.n, 0.0f) + (definition->type == _vr_vehicle_alien_scout ?
		vr_drive_steer(stick[0], VR_DRIVE_SCOUT_TURN, VR_DRIVE_SCOUT_TURN, FALSE) :
		vr_drive_steer(stick[0], definition->turn[0], definition->turn[1], vehicle->vehicle.speed < 0.0f));
	level = (real)sqrt(head_forward[0] * head_forward[0] + head_forward[1] * head_forward[1]);
	pitch = (real)atan2(head_forward[2], level);
	facing->i = (real)(cos(yaw) * cos(pitch));
	facing->j = (real)(sin(yaw) * cos(pitch));
	facing->k = (real)sin(pitch);
	return TRUE;
}

/* a pistol held with both hands (vr_two_handed's support, its main hand's
pointing given in `orientation`) aims steadier than with one, as a rifle
held by its fore-end does: from the middle of the main hand's pointing and
the other hand's, turned as it was against the main hand's as it took
hold (whichever way its fingers close about the grip), smoothed against
the hands' tremor (vr_steady). Once a frame, however often the aim is
asked for; started over as the hands take hold */
static void vr_two_hands_steady(
	struct vr_frame const *frame,
	float orientation[4])
{
	struct vr_hand const *off_hand = &frame->hand[vr_view.main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT];
	float off_pointing[4], inverse[4], other[4], middle[4];

	if (vr_view.steady.valid && vr_view.steady_frame == frame->index)
	{
		memcpy(orientation, vr_view.steady_aim, sizeof(vr_view.steady_aim));
		return;
	}
	vr_hand_pointing(off_hand, vr_view.aim_pitch, off_pointing);
	if (!vr_view.steady.valid || vr_view.steady_frame + 1 != frame->index)
	{
		/* (the turn from the other hand's pointing to the main's: the
		conjugate's, the poses' turns being of length 1) */
		inverse[0] = -off_pointing[0];
		inverse[1] = -off_pointing[1];
		inverse[2] = -off_pointing[2];
		inverse[3] = off_pointing[3];
		vr_quaternion_multiply(inverse, orientation, vr_view.steady_offset);
		vr_view.steady.valid = 0;
	}
	vr_quaternion_multiply(off_pointing, vr_view.steady_offset, other);
	vr_quaternion_toward(orientation, other, 0.5f, middle);
	vr_steady(&vr_view.steady, middle, frame->display_period, orientation);
	memcpy(vr_view.steady_aim, orientation, sizeof(vr_view.steady_aim));
	vr_view.steady_frame = frame->index;
}

/* the aim's orientation: the hand's, or with the off hand holding the
weapon too, from the main hand to it (vr_two_handed), a pistol steadied by
both (vr_two_hands_steady); never with a weapon of the off hand's own in
it (dual_wield.c) */
static void vr_hand_aim(
	struct vr_frame const *frame,
	struct vr_hand const *hand,
	float orientation[4])
{
	int held = dual_wield_weapon(vr_view.unit_index) != NONE ? VR_HELD_ONE :
		vr_two_handed(frame, vr_view.main_hand == VR_HAND_LEFT, vr_view.pistol_grip, vr_view.aim_pitch, orientation);

	if (held == VR_HELD_ONE)
	{
		vr_view.steady.valid = 0;
		vr_hand_pointing(hand, vr_view.aim_pitch, orientation);
	}
	else if (held == VR_HELD_SUPPORT)
	{
		vr_two_hands_steady(frame, orientation);
	}
}

/* the gun's aim (LOCAL) from the hands on its handles: the grips held (the
grip pressed), their middle, through the pivot placed as they took them
along the gun's `current` aim (again whenever a hand takes them or lets
go, so that the gun does not jump); FALSE with none held, the gun staying
where it aims */
static boolean vr_handles(
	struct vr_frame const *frame,
	float const current[3],
	float aim[3])
{
	float grip[3] = { 0.0f, 0.0f, 0.0f };
	int held = 0, count = 0, hand, axis;

	for (hand = 0; hand < VR_HAND_COUNT; hand++)
	{
		struct vr_hand const *state = &frame->hand[hand];

		if (!state->tracked || !(state->buttons & VR_BUTTON_GRIP))
			continue;
		held |= 1 << hand;
		count++;
		for (axis = 0; axis < 3; axis++)
			grip[axis] += state->grip.position[axis];
	}
	if (held != vr_view.handles_held)
	{
		platform_log("vr: the gun's handles %s", !held ? "let go" : held == 3 ? "held by both hands" :
			held == 1 << VR_HAND_LEFT ? "held by the left hand" : "held by the right hand");
	}
	if (!count)
	{
		vr_view.handles_held = 0;
		return FALSE;
	}
	for (axis = 0; axis < 3; axis++)
		grip[axis] /= (float)count;
	if (held != vr_view.handles_held)
	{
		vr_handles_pivot(grip, current, VR_HANDLES_LEVER, vr_view.handles_pivot);
		vr_view.handles_held = held;
	}
	return vr_handles_aim(grip, vr_view.handles_pivot, VR_HANDLES_LEVER, aim);
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

/* the main grip pressed at a shoulder (vr_input.c), on foot: each shoulder
keeps a slot of the inventory (at first the main hand's the first, the
other's the second). There, the weapon held, if it is the one kept there,
is put away (the hands bare: first_person_weapons.c draws the arms alone,
vr_input.c neither fires nor zooms); else the one kept there is drawn, the
one held going to the other shoulder. A shoulder keeping nothing takes the
weapon held, put away. A weapon that comes otherwise (taken, switched, a
new life's) is in the hand. With the hands as they hold (vr.hands
"physical"), the hands are bare while no hand holds the weapon; a grip at a
shoulder draws the weapon kept there (the one held going to the other
shoulder), and the weapon let go at a shoulder is kept there (the one kept
there going to the other). Only the player's view and buttons change: the
game's player holds the weapon still, as the other machines see it */
/* the weapon let fall from the hand, thrown with the hand's velocity
(the room's turned into the world's, metres a second into world units a
tick) by the game itself or its host (network_vr.c) */
static void vr_let_fall(
	struct unit_datum const *unit,
	short slot,
	float const local[3],
	int hand)
{
	real_vector3d velocity;
	float place[3] = { 0.0f, 0.0f, 0.0f };

	vr_local_to_world(local, vr_view.frame_heading, velocity.n);
	scale_vector3d(&velocity, VR_UNITS_PER_METRE / 30.0f, &velocity);
	vr_view.falling_weapon = slot == NONE ? vr_held_weapon() : unit_inventory_get_weapon(vr_view.unit_index, slot);
	vr_view.falling_time = game_time_get();
	network_vr_drop(unit->unit.player_index, &velocity, slot);
	vr_haptic(hand, 0.3f, 0.04f);
	if (hand >= 0 && hand < VR_HAND_COUNT)
		vr_input_from_head(vr_frame_get(), vr_frame_get()->hand[hand].grip.position, place);
	platform_log("vr: %s let fall (%.2f, %.2f, %.2f m/s), the hand %.2f right, %.2f up, %.2f back of the head",
		slot == NONE ? "the weapon" : "the other hand's weapon", local[0], local[1], local[2], place[0], place[1],
		place[2]);
}

/* a grenade thrown with the arm (vr.throw_gesture): the way and the speed
the hand threw it with and where the hand let go of it from the eyes (the
room's turned into the world's: world units a tick, world units), for the
game's throw, which the game itself or its host then makes from the hand
(network_vr.c) */
static void vr_throw(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	struct vr_frame const *frame = vr_frame_get();
	float velocity[3], place[3], local[3], from_head[3] = { 0.0f, 0.0f, 0.0f };
	real_vector3d world_velocity, offset;
	unsigned long throws;
	int hand, axis;

	throws = vr_input_throw(velocity, place, &hand);
	if (throws == vr_view.throws)
		return;
	vr_view.throws = throws;
	if (!unit || unit->object.parent_object_index != NONE || !vr_view.hand_aiming || unit->unit.player_index == NONE)
		return;
	for (axis = 0; axis < 3; axis++)
		local[axis] = place[axis] - frame->head.position[axis];
	vr_local_to_world(local, vr_view.frame_heading, offset.n);
	scale_vector3d(&offset, VR_UNITS_PER_METRE, &offset);
	vr_local_to_world(velocity, vr_view.frame_heading, world_velocity.n);
	scale_vector3d(&world_velocity, VR_UNITS_PER_METRE / (real)TICKS_PER_SECOND, &world_velocity);
	network_vr_throw(unit->unit.player_index, &world_velocity, &offset);
	vr_input_from_head(frame, place, from_head);
	platform_log("vr: a grenade thrown by the %s hand at %.1f m/s, let go %.2f right, %.2f up, %.2f back of the head",
		hand == VR_HAND_LEFT ? "left" : "right",
		(double)sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]),
		(double)from_head[0], (double)from_head[1], (double)from_head[2]);
}

static void vr_drop(
	struct unit_datum const *unit)
{
	float local[3];

	vr_input_drop_velocity(local);
	vr_let_fall(unit, NONE, local, vr_view.main_hand);
}

/* the main hand holds the slot, readied for it (vr_places) */
static void vr_hand_takes(
	short slot)
{
	vr_view.hand_slot = slot;
	vr_view.hand_slot_time = game_time_get();
}

static void vr_shoulders(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	long held = vr_held_weapon();
	unsigned long grabs;
	short current, kept;
	int side, kind, physical = vr_input_physical();

	grabs = vr_input_shoulder_grabs(&side, &kind);
	if (held != vr_view.held_weapon)
	{
		if (held != NONE)
		{
			int grabbed = vr_input_grab_pending();
			short previous = vr_view.grab_previous_slot;

			vr_input_weapon_came();
			/* (taken from the ground by the other hand: the hand that held a
			weapon goes on holding it, by a VR server's rules) */
			if (grabbed && vr_view.places && unit && previous >= 0 && previous < MAXIMUM_WEAPONS_PER_UNIT &&
				previous != unit->unit.current_weapon_index &&
				unit_inventory_get_weapon(vr_view.unit_index, previous) != NONE && vr_input_off_keep())
			{
				vr_view.off_keep_slot = previous;
			}
			vr_view.grab_previous_slot = NONE;
		}
		vr_view.held_weapon = held;
		vr_view.bare = FALSE;
	}
	if (!unit || unit->object.parent_object_index != NONE || !vr_view.hand_aiming)
	{
		vr_view.shoulder_grabs = grabs;
		vr_view.bare = FALSE;
		return;
	}
	if (vr_view.shoulder_unit != vr_view.unit_index || vr_view.places != (physical && vr_render_dual_wield()))
	{
		float velocity[3];
		int released;
		short slot;

		vr_view.shoulder_unit = vr_view.unit_index;
		vr_view.places = physical && vr_render_dual_wield();
		vr_view.shoulder_slot[vr_view.main_hand] = vr_view.places ? NONE : 0;
		vr_view.shoulder_slot[vr_view.main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT] = vr_view.places ? NONE : 1;
		vr_view.bare = FALSE;
		vr_view.hand_slot = vr_view.grab_previous_slot = vr_view.off_keep_slot = NONE;
		vr_view.falling_weapon = NONE;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			vr_view.inventory[slot] = unit_inventory_get_weapon(vr_view.unit_index, slot);
		vr_view.off_releases = vr_input_off_released(&released, velocity);
	}
	if (physical)
		vr_view.bare = !vr_input_holding();
	if (grabs == vr_view.shoulder_grabs)
		return;
	vr_view.shoulder_grabs = grabs;
	current = unit->unit.current_weapon_index;
	/* (let go at no shoulder by a VR server's rules: the weapon falls) */
	if (physical && kind == VR_SHOULDER_DROP && held != NONE)
	{
		vr_drop(unit);
		vr_view.hand_slot = NONE;
		return;
	}
	if (held == NONE || side < 0 || side >= VR_HAND_COUNT || current < 0 || current >= MAXIMUM_WEAPONS_PER_UNIT)
		return;
	kept = vr_view.shoulder_slot[side];
	/* (by a VR server's rules: the one kept there drawn, that shoulder then
	keeping none; or the one held kept at the shoulder, the one kept there
	moving to the other if it keeps none) */
	if (vr_view.places)
	{
		if (kind == VR_SHOULDER_DRAW && kept >= 0 && kept < MAXIMUM_WEAPONS_PER_UNIT &&
			unit_inventory_get_weapon(vr_view.unit_index, kept) != NONE)
		{
			player_control_set_desired_weapon(vr_view.unit_index, kept);
			vr_view.shoulder_slot[side] = NONE;
			vr_hand_takes(kept);
		}
		else if (kind == VR_SHOULDER_PUT && (kept == NONE || vr_view.shoulder_slot[!side] == NONE))
		{
			if (kept != NONE)
				vr_view.shoulder_slot[!side] = kept;
			vr_view.shoulder_slot[side] = vr_view.hand_slot != NONE ? vr_view.hand_slot : current;
			vr_view.hand_slot = NONE;
		}
		else
		{
			return;
		}
	}
	else if (physical)
	{
		/* (the one kept there drawn, or the one held kept there) */
		if (kind == VR_SHOULDER_DRAW && kept != current && unit_inventory_get_weapon(vr_view.unit_index, kept) != NONE)
		{
			player_control_set_desired_weapon(vr_view.unit_index, kept);
			vr_view.shoulder_slot[!side] = current;
		}
		else if (kind == VR_SHOULDER_PUT && kept != current)
		{
			vr_view.shoulder_slot[side] = current;
			vr_view.shoulder_slot[!side] = kept;
		}
		else if (kind != VR_SHOULDER_DRAW && kind != VR_SHOULDER_PUT)
		{
			return;
		}
	}
	else if (kept == current)
	{
		vr_view.bare = !vr_view.bare;
	}
	else if (unit_inventory_get_weapon(vr_view.unit_index, kept) != NONE)
	{
		player_control_set_desired_weapon(vr_view.unit_index, kept);
		vr_view.shoulder_slot[!side] = current;
		vr_view.bare = FALSE;
	}
	else if (!vr_view.bare)
	{
		vr_view.shoulder_slot[side] = current;
		vr_view.shoulder_slot[!side] = kept;
		vr_view.bare = TRUE;
	}
	else
	{
		return;
	}
	vr_haptic(vr_view.main_hand, 0.4f, 0.05f);
	platform_log("vr: the %s shoulder: %s", side == VR_HAND_LEFT ? "left" : "right",
		vr_view.bare ? "the weapon put away, the hands bare" : "a weapon drawn");
}

/* the other hand's weapon (by a VR server's rules; the other machines
follow this one's, network_vr.c): taken from the shoulder it drew at as it holds one, put back as it
lets go, the weapon in hand's as the main hand lets go of its own; its
trigger told (dual_wield.c) */
static void vr_dual(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	unsigned long transfers, releases;
	float trigger, velocity[3];
	int side, released_side, holding = vr_input_off_holding(&side, &trigger, &transfers);
	int off = vr_view.main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT;
	long held = dual_wield_weapon(vr_view.unit_index);
	short slot = dual_wield_slot(vr_view.unit_index), take;

	releases = vr_input_off_released(&released_side, velocity);
	if (transfers != vr_view.off_transfers)
	{
		vr_view.off_transfers = transfers;
		if (unit && slot != NONE)
		{
			dual_wield_end(vr_view.unit_index);
			player_control_set_desired_weapon(vr_view.unit_index, slot);
			vr_hand_takes(slot);
			held = NONE;
		}
	}
	/* (let go: kept at the shoulder it was let go at, the one kept there
	moving to the other if it keeps none, or let fall, by a VR server's
	rules) */
	if (releases != vr_view.off_releases)
	{
		vr_view.off_releases = releases;
		if (unit && held != NONE)
		{
			dual_wield_end(vr_view.unit_index);
			held = NONE;
			if (vr_view.places && released_side >= 0 && released_side < VR_HAND_COUNT &&
				(vr_view.shoulder_slot[released_side] == NONE || vr_view.shoulder_slot[!released_side] == NONE))
			{
				if (vr_view.shoulder_slot[released_side] != NONE)
					vr_view.shoulder_slot[!released_side] = vr_view.shoulder_slot[released_side];
				vr_view.shoulder_slot[released_side] = slot;
			}
			else if (vr_view.places && released_side < 0)
			{
				vr_let_fall(unit, slot, velocity, off);
			}
		}
	}
	if (!unit || !holding || !vr_render_dual_wield() || unit->object.parent_object_index != NONE ||
		!vr_view.hand_aiming)
	{
		if (held != NONE)
			dual_wield_end(vr_view.unit_index);
		vr_view.off_keep_slot = NONE;
		return;
	}
	take = side >= 0 && side < VR_HAND_COUNT ? vr_view.shoulder_slot[side] : vr_view.off_keep_slot;
	if (held == NONE && take != NONE && dual_wield_begin(vr_view.unit_index, take))
	{
		if (vr_view.places && side >= 0 && side < VR_HAND_COUNT)
			vr_view.shoulder_slot[side] = NONE;
		vr_view.off_keep_slot = NONE;
		vr_haptic(off, 0.4f, 0.05f);
	}
	dual_wield_set_trigger(vr_view.unit_index, trigger);
}

/* by a VR server's rules, with the hands as they hold, the shoulders as
places (vr_shoulders): each keeps one weapon or none, the hands hold theirs,
and a weapon nowhere (picked up, back from the other hand, readied by the
game) goes to a shoulder that keeps none, the other hand's first; one none
can take waits for one. A weapon that comes to the inventory as a hand
takes one from the ground goes to that hand. */
static void vr_places(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	int off = vr_view.main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT;
	long now = game_time_get();
	short dual, current, slot;
	int side;

	if (!vr_view.places || !unit || unit->object.parent_object_index != NONE ||
		vr_view.shoulder_unit != vr_view.unit_index)
	{
		return;
	}
	dual = dual_wield_slot(vr_view.unit_index);
	current = unit->unit.current_weapon_index;
	if (vr_input_grab_pending())
	{
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			if (vr_view.inventory[slot] == NONE && unit_inventory_get_weapon(vr_view.unit_index, slot) != NONE &&
				slot != current)
			{
				vr_view.grab_previous_slot = vr_view.hand_slot;
				player_control_set_desired_weapon(vr_view.unit_index, slot);
				vr_hand_takes(slot);
				break;
			}
		}
	}
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		vr_view.inventory[slot] = unit_inventory_get_weapon(vr_view.unit_index, slot);
	/* (the hand's: the game's once it has readied the one chosen, or a
	second on) */
	if (!vr_input_holding())
	{
		vr_view.hand_slot = NONE;
	}
	else if (current >= 0 && current < MAXIMUM_WEAPONS_PER_UNIT)
	{
		if (current == vr_view.hand_slot)
			vr_view.hand_slot_time = now - TICKS_PER_SECOND - 1;
		else if (vr_view.hand_slot == NONE || now < vr_view.hand_slot_time ||
			now - vr_view.hand_slot_time > TICKS_PER_SECOND)
		{
			vr_view.hand_slot = current;
		}
	}
	for (side = 0; side < VR_HAND_COUNT; side++)
	{
		slot = vr_view.shoulder_slot[side];
		if (slot != NONE && (slot < 0 || slot >= MAXIMUM_WEAPONS_PER_UNIT || vr_view.inventory[slot] == NONE ||
			slot == dual || slot == vr_view.hand_slot || slot == vr_view.off_keep_slot ||
			(side == VR_HAND_RIGHT && slot == vr_view.shoulder_slot[VR_HAND_LEFT])))
		{
			vr_view.shoulder_slot[side] = NONE;
		}
	}
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		if (vr_view.inventory[slot] == NONE || slot == vr_view.hand_slot || slot == dual ||
			slot == vr_view.off_keep_slot || slot == vr_view.shoulder_slot[VR_HAND_LEFT] ||
			slot == vr_view.shoulder_slot[VR_HAND_RIGHT] ||
			(vr_view.inventory[slot] == vr_view.falling_weapon && now >= vr_view.falling_time &&
				now - vr_view.falling_time < TICKS_PER_SECOND))
		{
			continue;
		}
		side = vr_view.shoulder_slot[off] == NONE ? off : vr_view.shoulder_slot[!off] == NONE ? !off : NONE;
		if (side != NONE)
			vr_view.shoulder_slot[side] = slot;
	}
}

/* the distance squared from `point` to the segment from `a` to `b` */
static real vr_segment_distance_squared(
	real_point3d const *point,
	real_point3d const *a,
	real_point3d const *b)
{
	real_vector3d along, to;
	real length_squared, t;
	real_point3d nearest;

	vector_from_points3d(a, b, &along);
	vector_from_points3d(a, point, &to);
	length_squared = dot_product3d(&along, &along);
	t = length_squared > 0.0f ? dot_product3d(&to, &along) / length_squared : 0.0f;
	t = PIN(t, 0.0f, 1.0f);
	point_from_line3d(a, &along, t, &nearest);
	return distance_squared3d(point, &nearest);
}

/* a weapon on the ground (none holds it) within each hand's reach, on foot:
the hand's grip within VR_GRAB_REACH of the weapon along its length (from
its origin, which is about its grip, through its middle and as far again:
a rifle's whole length, which its bounding sphere is far wider than); a
hand that holds no weapon feels one come within its reach, lightly */
static void vr_grab_reach(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	struct object_iterator iterator;
	real reach = VR_GRAB_REACH * VR_UNITS_PER_METRE;
	boolean was[VR_HAND_COUNT];
	int hand;

	was[VR_HAND_LEFT] = vr_view.grab_reach[VR_HAND_LEFT];
	was[VR_HAND_RIGHT] = vr_view.grab_reach[VR_HAND_RIGHT];
	vr_view.grab_reach[VR_HAND_LEFT] = vr_view.grab_reach[VR_HAND_RIGHT] = FALSE;
	if (!unit || unit->object.parent_object_index != NONE || !vr_view.hand_aiming)
		return;
	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);
		real_point3d far_end;

		if (item->object.parent_object_index != NONE ||
			!TEST_FLAG(item->object.flags, _object_connected_to_map_bit) ||
			TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
		{
			continue;
		}
		far_end.x = 2.0f * item->object.bounding_sphere_center.x - item->object.position.x;
		far_end.y = 2.0f * item->object.bounding_sphere_center.y - item->object.position.y;
		far_end.z = 2.0f * item->object.bounding_sphere_center.z - item->object.position.z;
		for (hand = 0; hand < VR_HAND_COUNT; hand++)
		{
			if (vr_view.grip_valid[hand] &&
				vr_segment_distance_squared(&vr_view.grips[hand].position, &item->object.position, &far_end) <
					reach * reach)
			{
				vr_view.grab_reach[hand] = TRUE;
			}
		}
	}
	for (hand = 0; hand < VR_HAND_COUNT; hand++)
	{
		if (vr_view.grab_reach[hand] && !was[hand] &&
			!(hand == vr_view.main_hand && (!vr_input_physical() || vr_input_holding())))
		{
			vr_haptic(hand, 0.15f, 0.02f);
		}
	}
}

/* `text` (at most `length` characters) names a pistol or the needler, in
any case */
static boolean vr_names_pistol(
	char const *text,
	short length)
{
	char lower[256];
	short index;

	for (index = 0; index < length && index < (short)sizeof(lower) - 1 && text[index]; index++)
		lower[index] = text[index] >= 'A' && text[index] <= 'Z' ? (char)(text[index] - 'A' + 'a') : text[index];
	lower[index] = 0;
	return strstr(lower, "pistol") != NULL || strstr(lower, "needler") != NULL;
}

/* the weapon in the hand is held as a pistol: its tag's name (the stock
maps' "weapons\pistol\pistol", "weapons\plasma pistol\plasma pistol",
"weapons\needler\mp_needler") or its label names a pistol or the needler;
the other hand supports it at the main hand */
static boolean vr_pistol_grip(
	void)
{
	long weapon_index = vr_held_weapon();
	long definition_index;
	char const *name;

	if (weapon_index == NONE)
		return FALSE;
	definition_index = weapon_get(weapon_index)->definition_index;
	name = tag_get_name(definition_index);
	return (name && vr_names_pistol(name, 255)) ||
		vr_names_pistol(weapon_definition_get(definition_index)->weapon.label,
			(short)sizeof(weapon_definition_get(definition_index)->weapon.label));
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
offset from the grip (vr_weapon_muzzle, until the model's is learnt). In
either hand the model is as the right hand holds it, never mirrored (the
arms are: vr_arms_mirror) */
static void vr_weapon_in_hand(
	long weapon_index,
	float const aim[4],
	float model[4],
	float muzzle[3])
{
	static float const local_forward[3] = { 0.0f, 0.0f, -1.0f };
	struct vr_weapon_fit const *fit = vr_weapon_fit(weapon_index);
	float turn[4], from_grip[3];
	int axis;

	if (!fit)
	{
		memcpy(model, aim, sizeof(float) * 4);
		vr_rotate(aim, vr_weapon_muzzle, muzzle);
		return;
	}
	vr_quaternion_between(fit->forward, local_forward, turn);
	vr_quaternion_multiply(aim, turn, model);
	/* (the camera is VR_WEAPON_GRIP behind the grip, the muzzle where it
	holds it) */
	for (axis = 0; axis < 3; axis++)
		from_grip[axis] = fit->muzzle[axis] - vr_weapon_grip[axis];
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
		vr_view.room_body_height = 0.0f;
		vr_view.room_valid = FALSE;
		vr_view.room_anchor_valid = FALSE;
		return VR_HEAD_REACH;
	}
	if (!vr_view.room_valid || state != vr_view.room_state)
	{
		vr_view.room_body[0] = frame->head.position[0];
		vr_view.room_body[1] = frame->head.position[2];
		vr_view.room_body_height = state == _vr_room_seated ? frame->head.position[1] : 0.0f;
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
	offset[1] = frame->head.position[1] - vr_view.room_body_height;
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

/* vr.eye_height, on foot: where the eyes are over the unit's feet.
"chief": at the game's camera (the Master Chief's eyes, 0.62 world units,
1.89 m, its crouch included) while the head is as high as it has been since
the runtime last recentred (standing, or a seated player sitting), and as
much lower as the head is below that: the button's crouch and the player's
own (vr_input.c makes it the unit's) do not add up, the eyes going to the
lower of the two. The eyes and the body the player sees (the legs, the
arms' shoulders) are then the cyborg's, whatever the player's height.
"player": as high over the ground as the player's eyes are over the floor
(the runtime's STAGE): the cyborg is a head taller than most players, whose
eyes then sit at its chest; the unit's crouch lowers them as it lowers the
game's camera, in proportion to the player's height (the crouching camera
is 57% of the standing one), less what the head has already gone down. The
game's own camera, its shots and its collision are unchanged either way */
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

	if (!vr_view.head_top_valid || vr_view.head_top_recentres != frame->recentres ||
		frame->head.position[1] > vr_view.head_top)
	{
		vr_view.head_top = frame->head.position[1];
		vr_view.head_top_valid = TRUE;
		vr_view.head_top_recentres = frame->recentres;
	}
	if ((vr_view.eye_height_player && !frame->floor_known) || !biped ||
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
	if (!vr_view.eye_height_player)
	{
		/* (world units: how far the unit's crouch has lowered the camera,
		and how far the head is below its highest) */
		lowered = definition->biped.standing_camera_height - height;
		ducked = (vr_view.head_top - frame->head.position[1]) * VR_UNITS_PER_METRE;
		anchor->z += lowered - (ducked > lowered ? ducked : lowered) - vr_view.head_top * VR_UNITS_PER_METRE;
		return;
	}
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

/* the heading behind a vehicle (the director's following camera) while
the facing turns it (the drive's steering, the main hand's stick): the
game's camera as drawn this frame (blended between the ticks,
render_interpolation.c), turned from it as the heading is from the facing.
The heading itself follows the facing a frame late, and the drive takes
the facing from the vehicle's yaw as the last tick left it: the view
turned in steps, one a tick, ahead of the camera whose place goes round
the vehicle as the drawn camera faces. Else the heading */
static real vr_chase_heading(
	struct vr_frame const *frame,
	struct render_window const *player)
{
	struct observer_result const *camera;

	if (!vr_view.heading_valid || !(vr_view.driving || vr_view.seat_stick) ||
		director_get_perspective(player->local_player_index) != _director_perspective_third_person)
	{
		return vr_heading(frame, &player->render_camera);
	}
	/* (the frame's blend again: main_game_render has taken this frame's
	snapshot, so it is the same camera) */
	camera = render_interpolation_camera(player->local_player_index,
		observer_get_camera(player->local_player_index));
	if (!camera)
		return vr_view.heading;
	return vr_wrap_angle(vr_yaw_of(camera->forward.n, vr_view.set_yaw) +
		vr_wrap_angle(vr_view.heading - vr_view.set_yaw));
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

/* a reticle where a shot from `origin` along `forward` meets the world, in
LOCAL space (the eyes' mapping of it reversed) */
static void vr_reticle_along(
	struct vr_frame const *frame,
	real_point3d const *anchor,
	real heading,
	float const head[3],
	int reticle,
	real_point3d const *origin,
	real_vector3d const *forward,
	boolean aimed)
{
	real_point3d target;
	real_vector3d ray;
	struct collision_result collision;
	float world[3], local[3], position[3];
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	long ignored = vr_view.unit_index;
	int axis;

	/* (in a seat, the vehicle is not in the way) */
	if (unit && unit->object.parent_object_index != NONE)
		ignored = unit->object.parent_object_index;
	/* (as far as the passengers' reticle looks: 128 units) */
	scale_vector3d(forward, 128.0f, &ray);
	if (collision_test_vector(_collision_test_for_projectiles_flags, origin, &ray, ignored, &collision))
		target = collision.point;
	else
		point_from_line3d(origin, forward, 128.0f, &target);
	for (axis = 0; axis < 3; axis++)
		world[axis] = (target.n[axis] - anchor->n[axis]) / VR_UNITS_PER_METRE;
	vr_world_to_local(world, heading, local);
	for (axis = 0; axis < 3; axis++)
		position[axis] = frame->head.position[axis] - head[axis] + local[axis];
	vr_reticle_place(reticle, position, aimed);
	vr_view.reticle_placed[reticle] = TRUE;
}

/* the reticles: where the held weapon's shot meets the world, red when the
game's crosshair would be (the aim assist's target fully in the aim). A
weapon in the hand shoots from its muzzle along its barrel as the hand
holds it this frame (the shot's own ray is the last tick's); in a seat,
the shot's ray. The other hand's weapon's (dual_wield.c) from its own
muzzle, red as the aim assist takes its own shot's target */
static void vr_reticle(
	struct vr_frame const *frame,
	short local_player_index,
	real_point3d const *anchor,
	real heading,
	float const head[3])
{
	real_point3d origin;
	real_vector3d forward;
	long weapon_index = vr_aimed_weapon();
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);

	if (weapon_index == NONE)
		return;
	if (vr_view.weapon_in_hand)
	{
		origin = vr_view.muzzle;
		forward = vr_view.barrel;
	}
	else if (!weapon_get_projectile_ray(weapon_index, 0, &origin, &forward))
	{
		return;
	}
	vr_reticle_along(frame, anchor, heading, head, 0, &origin, &forward,
		player_control_get_autoaim_level(local_player_index) >= 1.0f);
	if (vr_view.other_valid && unit && unit->unit.player_index != NONE &&
		first_person_weapon_get_local_index(dual_wield_weapon(vr_view.unit_index)) >= FIRST_PERSON_WEAPON_OTHER_HAND)
	{
		vr_view.other_reticle_aimed = player_aim_assist_level(unit->unit.player_index, &vr_view.other_muzzle,
			&vr_view.other_barrel) >= 1.0f;
		vr_reticle_along(frame, anchor, heading, head, 1, &vr_view.other_muzzle, &vr_view.other_barrel,
			vr_view.other_reticle_aimed);
	}
}

/* the scope's window, while the aiming hand's weapon is zoomed: from its
muzzle along the aim, in a square of the back buffer's pixels at its top
left; and its lens, held on the weapon (vr_scope_offset). The field is the
lens's as the eyes see it, by the zoom's magnification
(weapon_get_zoom_magnification): a 2x zoom shows the world twice as large
as around the lens */
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
	float offset[3], along[3], model[4], to_eye[3];
	long width, height, weapon_index = vr_held_weapon();
	real aspect, half, magnification, distance;
	int axis;

	if (!unit || unit->unit.current_zoom_level == NONE || weapon_index == NONE || !vr_screen_size(&width, &height) ||
		width <= 0 || height <= 0)
	{
		return FALSE;
	}
	magnification = weapon_get_zoom_magnification(weapon_index, unit->unit.current_zoom_level);
	if (!(magnification >= 1.0f && magnification <= 100.0f))
		magnification = 1.0f;
	vr_hand_aim(frame, hand, aim.orientation);
	vr_weapon_in_hand(weapon_index, aim.orientation, model, along);
	for (axis = 0; axis < 3; axis++)
		offset[axis] = head[axis] + hand->grip.position[axis] + along[axis] - frame->head.position[axis];
	vr_camera_from_pose(&camera, &aim, offset, anchor, heading);
	/* (square in pixels: the screen's 640x480 are drawn at the eyes' size) */
	camera.viewport_bounds.x0 = camera.viewport_bounds.y0 = 0;
	camera.viewport_bounds.x1 = (short)(VR_SCOPE_PIXELS * 640L / width);
	camera.viewport_bounds.y1 = (short)(VR_SCOPE_PIXELS * 480L / height);
	camera.window_bounds = camera.viewport_bounds;
	vr_view.scope_window = camera.viewport_bounds;
	/* (the lens, on the weapon, facing back along it) */
	vr_rotate(aim.orientation, vr_scope_offset, along);
	for (axis = 0; axis < 3; axis++)
	{
		place.position[axis] = hand->grip.position[axis] + along[axis];
		to_eye[axis] = place.position[axis] - frame->head.position[axis];
	}
	memcpy(place.orientation, aim.orientation, sizeof(place.orientation));
	vr_scope_place(&place, VR_SCOPE_WIDTH);
	/* (the lens's half width as the head sees it, shown magnified) */
	distance = (real)sqrt(to_eye[0] * to_eye[0] + to_eye[1] * to_eye[1] + to_eye[2] * to_eye[2]);
	if (distance < 0.05f)
		distance = 0.05f;
	half = (real)atan(VR_SCOPE_WIDTH * 0.5f / distance / magnification);
	aspect = (real)camera.viewport_bounds.x1 / (real)camera.viewport_bounds.y1;
	field.left = field.down = -half;
	field.right = field.up = half;
	vr_fov_bounds(&field, aspect, vr_view.scope_bounds.n);
	*window = *player;
	window->rasterizer_camera = camera;
	window->render_camera = camera;
	return TRUE;
}

/* the comfort vignette this frame (vr.vignette): closed as the view moves
and turns by itself (the stick, a vehicle, a fall), not as the head does.
`head` is the head's offset in LOCAL space as the eyes are placed */
static void vr_comfort(
	struct vr_frame const *frame,
	real_point3d const *anchor,
	real heading,
	float const head[3])
{
	struct render_camera origin;
	float offset[3];
	real target = 0.0f, period = frame->display_period, rate;
	int axis;

	if (!(vr_view.vignette_strength > 0.0f) || !(period > 0.0f))
	{
		vr_view.vignette_valid = FALSE;
		vr_view.vignette = 0.0f;
		vr_vignette(0.0f);
		return;
	}
	/* (LOCAL's origin in the world: it stays put while only the head moves) */
	for (axis = 0; axis < 3; axis++)
		offset[axis] = head[axis] - frame->head.position[axis];
	vr_camera_from_pose(&origin, &frame->head, offset, anchor, heading);
	/* (a recentre moves LOCAL's origin: not the view's own move) */
	if (vr_view.vignette_valid && vr_view.vignette_recentres == frame->recentres)
	{
		real moved = distance3d(&origin.position, &vr_view.vignette_origin) / VR_UNITS_PER_METRE;
		real turned = (real)fabs(vr_wrap_angle(heading - vr_view.vignette_heading));

		if (moved < VR_VIGNETTE_JUMP && turned < VR_VIGNETTE_SNAP)
		{
			target = moved / period / VR_VIGNETTE_SPEED;
			if (turned / period / VR_VIGNETTE_TURN > target)
				target = turned / period / VR_VIGNETTE_TURN;
			target = PIN(target, 0.0f, 1.0f);
		}
		else
		{
			target = vr_view.vignette;
		}
	}
	vr_view.vignette_valid = TRUE;
	vr_view.vignette_recentres = frame->recentres;
	vr_view.vignette_origin = origin.position;
	vr_view.vignette_heading = heading;
	rate = period / (target > vr_view.vignette ? VR_VIGNETTE_CLOSE : VR_VIGNETTE_OPEN);
	vr_view.vignette += (target - vr_view.vignette) * (rate < 1.0f ? rate : 1.0f);
	vr_vignette(vr_view.vignette * vr_view.vignette_strength);
}

/* on foot, the head's and the hands' places from the unit's and their axes,
to the host of a VR server's game (network_vr.c: it does nothing in any
other), for the other machines to pose the player's body by */
static void vr_pose_send(
	short local_player_index)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	struct network_vr_pose pose;
	unsigned long transfers;
	float trigger;
	int side;
	short hand;

	if (!unit || unit->object.parent_object_index != NONE || !vr_view.hand_aiming ||
		game_connection() == _game_connection_local)
	{
		return;
	}
	csmemset(&pose, 0, sizeof(pose));
	vector_from_points3d(&unit->object.position, &vr_view.head_camera.position, &pose.head);
	pose.head_forward = vr_view.head_camera.forward;
	pose.head_up = vr_view.head_camera.up;
	for (hand = 0; hand < VR_HAND_COUNT; hand++)
	{
		if (!vr_view.grip_valid[hand])
			continue;
		SET_FLAG(pose.flags, hand == VR_HAND_LEFT ? _network_vr_pose_left_hand_bit : _network_vr_pose_right_hand_bit,
			TRUE);
		vector_from_points3d(&unit->object.position, &vr_view.grips[hand].position,
			&pose.hands[hand == VR_HAND_LEFT ? 0 : 1]);
		pose.hand_forwards[hand == VR_HAND_LEFT ? 0 : 1] = vr_view.pointings[hand].forward;
		pose.hand_ups[hand == VR_HAND_LEFT ? 0 : 1] = vr_view.pointings[hand].up;
	}
	SET_FLAG(pose.flags, _network_vr_pose_left_handed_bit, vr_view.main_hand == VR_HAND_LEFT);
	SET_FLAG(pose.flags, _network_vr_pose_bare_bit, vr_view.bare);
	pose.off_slot = dual_wield_slot(vr_view.unit_index);
	vr_input_off_holding(&side, &trigger, &transfers);
	pose.off_trigger = pose.off_slot != NONE ? PIN(trigger, 0.0f, 1.0f) : 0.0f;
	network_vr_send(local_player_get_player_index(local_player_index), &pose);
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
	csmemset(vr_view.reticle_placed, 0, sizeof(vr_view.reticle_placed));
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
		vr_view.vignette_valid = FALSE;
		vr_view.vignette = 0.0f;
		vr_vignette(0.0f);
		vr_input_weapon_hands(FALSE);
		return window_count;
	}
	player = windows[0];
	console = windows[1];
	anchor = player.render_camera.position;
	/* (behind a vehicle, the observer's camera: not its shake, which moves
	the game's camera once a tick, player_effects.c) */
	if (!vr_view.seat_view && !cinematic_in_progress() &&
		director_get_perspective(player.local_player_index) == _director_perspective_third_person)
	{
		struct observer_result const *camera = render_interpolation_camera(player.local_player_index,
			observer_get_camera(player.local_player_index));

		if (camera)
			anchor = camera->position;
	}
	/* (seen from a seat, the eyes at the unit's head, held in the seat
	(vr_seat_eye): the first person's camera there is the unit's standing
	eye height over its origin, inside the vehicle's body; astromaddie's) */
	if (vr_view.seat_view && !cinematic_in_progress())
		vr_seat_eye(&anchor);
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
		heading = vr_view.seat_view ? vr_seat_heading(TRUE) : vr_chase_heading(frame, &player);
	}
	aspect = (real)(player.render_camera.viewport_bounds.x1 - player.render_camera.viewport_bounds.x0) /
		(real)(player.render_camera.viewport_bounds.y1 - player.render_camera.viewport_bounds.y0);
	vr_head_offset(frame, &anchor, heading, vr_room_follow(frame, player.local_player_index, &anchor, heading), head);
	/* (none in a cinematic: its shots are the head's to look around) */
	if (cinematic_in_progress())
	{
		vr_view.vignette_valid = FALSE;
		vr_view.vignette = 0.0f;
		vr_vignette(0.0f);
	}
	else
	{
		vr_comfort(frame, &anchor, heading, head);
	}
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
	{
		float body[3];

		vr_arms_body(&vr_view.head_camera.forward, heading, body);
		vr_view.body_forward.i = body[0];
		vr_view.body_forward.j = body[1];
		vr_view.body_forward.k = body[2];
	}
	for (hand_index = 0; hand_index < VR_HAND_COUNT; hand_index++)
	{
		struct vr_hand const *grip = &frame->hand[hand_index];
		int axis;

		vr_view.grip_valid[hand_index] = frame->controllers && grip->tracked;
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + grip->grip.position[axis] - frame->head.position[axis];
		vr_view.grips[hand_index] = player.rasterizer_camera;
		vr_camera_from_pose(&vr_view.grips[hand_index], &grip->grip, offset, &anchor, heading);
		{
			struct vr_pose pointing = grip->grip;

			vr_hand_pointing(grip, vr_view.aim_pitch, pointing.orientation);
			vr_view.pointings[hand_index] = player.rasterizer_camera;
			vr_camera_from_pose(&vr_view.pointings[hand_index], &pointing, offset, &anchor, heading);
		}
	}
	/* the weapon's camera, behind the aiming hand: the grip's place less
	VR_WEAPON_GRIP along the axes of the aim, turned so that the model's
	barrel lies along it (vr_weapon_in_hand) */
	vr_view.weapon_in_hand = vr_view.hand_aiming && !vr_view.cinematic;
	vr_view.pistol_grip = vr_view.weapon_in_hand && vr_pistol_grip();
	/* (the rules the headset plays by, logged as they change) */
	{
		int rules = vr_rules_active();

		if (rules != vr_view.rules)
		{
			vr_view.rules = rules;
			platform_log("vr: %s", rules ? "a VR server's rules (or a game with no network)" :
				"an ordinary server's rules: the hands as any player's buttons");
		}
	}
	vr_shoulders();
	vr_dual();
	vr_places();
	vr_grab_reach();
	vr_throw();
	vr_pose_send(player.local_player_index);
	/* (the other hand's grip, for the weapon it may hold) */
	{
		int off = vr_view.main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT, axis;
		struct vr_hand const *hand = &frame->hand[off];
		struct vr_pose pointing = hand->aim;

		vr_view.off_grip_valid = frame->controllers && hand->tracked && !vr_view.cinematic;
		vr_hand_pointing(hand, vr_view.aim_pitch, pointing.orientation);
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + hand->grip.position[axis] - frame->head.position[axis];
		vr_view.off_grip = player.rasterizer_camera;
		vr_camera_from_pose(&vr_view.off_grip, &pointing, offset, &anchor, heading);
	}
	{
		float both[4];

		/* (not with a weapon of the off hand's own in it, dual_wield.c) */
		vr_input_weapon_hands(vr_view.weapon_in_hand && !vr_view.bare &&
			dual_wield_weapon(vr_view.unit_index) == NONE &&
			vr_two_handed(frame, vr_view.main_hand == VR_HAND_LEFT, vr_view.pistol_grip, vr_view.aim_pitch, both));
	}
	if (vr_view.weapon_in_hand)
	{
		struct vr_hand const *hand = &frame->hand[vr_view.main_hand];
		struct vr_pose aim = hand->aim, model;
		float grip[3], along[3];
		struct render_camera muzzle = player.rasterizer_camera;
		int axis;

		vr_hand_aim(frame, hand, aim.orientation);
		model = aim;
		vr_weapon_in_hand(vr_held_weapon(), aim.orientation, model.orientation, along);
		vr_rotate(model.orientation, vr_weapon_grip, grip);
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + hand->grip.position[axis] - grip[axis] - frame->head.position[axis];
		vr_view.weapon_camera = player.rasterizer_camera;
		vr_camera_from_pose(&vr_view.weapon_camera, &model, offset, &anchor, heading);
		/* (the muzzle's place: a camera there, its position) */
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + hand->grip.position[axis] + along[axis] - frame->head.position[axis];
		vr_camera_from_pose(&muzzle, &aim, offset, &anchor, heading);
		vr_view.muzzle = muzzle.position;
		vr_view.barrel = muzzle.forward;
	}
	/* (the other hand's weapon, behind that hand as the weapon in hand is
	behind its own, along where that hand points) */
	vr_view.other_valid = FALSE;
	if (vr_view.weapon_in_hand && vr_view.off_grip_valid && dual_wield_weapon(vr_view.unit_index) != NONE)
	{
		struct vr_hand const *hand = &frame->hand[vr_view.main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT];
		struct vr_pose aim = hand->aim, model;
		float grip[3], along[3];
		struct render_camera muzzle = player.rasterizer_camera;
		int axis;

		vr_hand_pointing(hand, vr_view.aim_pitch, aim.orientation);
		model = aim;
		vr_weapon_in_hand(dual_wield_weapon(vr_view.unit_index), aim.orientation, model.orientation, along);
		vr_rotate(model.orientation, vr_weapon_grip, grip);
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + hand->grip.position[axis] - grip[axis] - frame->head.position[axis];
		vr_view.other_camera = player.rasterizer_camera;
		vr_camera_from_pose(&vr_view.other_camera, &model, offset, &anchor, heading);
		for (axis = 0; axis < 3; axis++)
			offset[axis] = head[axis] + hand->grip.position[axis] + along[axis] - frame->head.position[axis];
		vr_camera_from_pose(&muzzle, &aim, offset, &anchor, heading);
		vr_view.other_muzzle = muzzle.position;
		vr_view.other_barrel = muzzle.forward;
		vr_view.other_valid = TRUE;
	}
	if ((vr_view.weapon_in_hand && !vr_view.bare) || (vr_view.seat_aiming && !vr_view.cinematic))
		vr_reticle(frame, player.local_player_index, &anchor, heading, head);
	window_index = 2;
	vr_view.scope = vr_view.weapon_in_hand && !vr_view.bare && vr_scope_window(frame, &player, &anchor, heading,
		head, &windows[window_index]);
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

/* where the model's barrel is, from its pose (before the arms are
mirrored: the weapon never is): the first-person weapon `weapon`'s (the
weapon in hand's or the other hand's, first_person_weapons.h), the weapon
`weapon_index`, posed from `camera` */
static void vr_weapon_learn(
	short weapon,
	struct render_camera const *camera,
	long weapon_index)
{
	struct vr_weapon_fit *fit;
	struct object_marker marker;
	real_vector3d left, relative;
	float muzzle[3], forward[3], length;
	int axis;

	if (weapon_index == NONE || !first_person_weapon_idle(weapon) ||
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
		platform_log("vr: weapon %lx (%s, %.32s%s)'s barrel at %.3f %.3f %.3f, along %.3f %.3f %.3f",
			fit->definition_index, tag_get_name(fit->definition_index),
			weapon_definition_get(fit->definition_index)->weapon.label,
			vr_pistol_grip() ? ", a pistol's grip" : "", (double)muzzle[0], (double)muzzle[1], (double)muzzle[2],
			(double)forward[0], (double)forward[1], (double)forward[2]);
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

/* in the left hand, the arms (the nodes the hands' model has and the
weapon's has not; with `all`, every node: no weapon) mirrored across the
weapon's own upright plane, along its barrel (the weapon camera's left its
normal, the barrel's side of the camera from the model's learnt muzzle):
the right hand that holds the weapon becomes a left hand on its grip, the
other a right hand. The weapon stays as it is, as the right hand holds it,
its markers (its muzzle's, its flashlight's) with it; FALSE if not
mirrored. The first-person weapon `weapon`'s (the weapon in hand's or the
other hand's, first_person_weapons.h), the weapon `weapon_index`, posed from
`camera` */
static boolean vr_arms_mirror(
	real_matrix4x3 *matrices,
	short node_count,
	boolean all,
	short weapon,
	struct render_camera const *camera,
	long weapon_index)
{
	struct vr_weapon_fit const *fit = all ? NULL : vr_weapon_fit(weapon_index);
	boolean arms[MAXIMUM_NODES_PER_ANIMATION];
	real_vector3d normal;
	real_point3d middle;
	short index;

	if (node_count > MAXIMUM_NODES_PER_ANIMATION)
		return FALSE;
	if (!all && first_person_weapon_arm_nodes(weapon, arms, node_count) <= 0)
		return FALSE;
	cross_product3d(&camera->up, &camera->forward, &normal);
	if (!(normalize3d(&normal) > 0.0f))
		return FALSE;
	/* (the camera's right of the barrel: against its left) */
	point_from_line3d(&camera->position, &normal,
		-(fit ? fit->muzzle[0] : vr_weapon_grip[0]) * VR_UNITS_PER_METRE, &middle);
	for (index = 0; index < node_count; index++)
	{
		real_matrix4x3 *matrix = &matrices[index];
		real_vector3d *axes[3];
		real_vector3d offset;
		real along;
		int axis;

		if (!all && !arms[index])
			continue;
		axes[0] = &matrix->forward;
		axes[1] = &matrix->left;
		axes[2] = &matrix->up;
		vector_from_points3d(&middle, &matrix->position, &offset);
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
	return TRUE;
}

/* each arm its own (vr.arms "free"): from its shoulder, the weapon's hand
holding the weapon (or with `empty` hands, none: as the hands are bare),
the other its controller (vr_arms.c). With `other`, the arms of the weapon
the other hand holds (dual_wield.c): its weapon's hand that other hand, its
other arm not drawn; and while that weapon is drawn, the weapon in hand's
other arm is not either (each hand holds its own) */
static void vr_arms_update(
	struct animation_graph *graph,
	real_matrix4x3 *matrices,
	short count,
	boolean empty,
	boolean other)
{
	struct vr_frame const *frame = vr_frame_get();
	struct vr_arms_pose pose;
	short main_hand = vr_view.main_hand, off_hand = main_hand == VR_HAND_LEFT ? VR_HAND_RIGHT : VR_HAND_LEFT;
	boolean other_drawn = other ||
		first_person_weapon_get_animation_graph(render.local_player_index + FIRST_PERSON_WEAPON_OTHER_HAND) != NULL;
	float orientation[4];

	if (!graph)
		return;
	/* (the hand that holds this one weapon its weapon's hand) */
	if (other)
	{
		short hand = main_hand;

		main_hand = off_hand;
		off_hand = hand;
	}
	if (!vr_view.arms_free)
	{
		if (other_drawn)
			vr_arms_hide_other(matrices, count, graph);
		return;
	}
	pose.head = vr_view.head_camera.position;
	pose.head_forward = vr_view.head_camera.forward;
	pose.head_up = vr_view.head_camera.up;
	pose.heading = vr_view.frame_heading;
	pose.body_forward = vr_view.body_forward;
	pose.main_left = main_hand == VR_HAND_LEFT;
	pose.main_valid = vr_view.grip_valid[main_hand];
	pose.main_grip = vr_view.grips[main_hand].position;
	pose.main_forward = vr_view.grips[main_hand].forward;
	pose.main_up = vr_view.grips[main_hand].up;
	pose.off_valid = vr_view.grip_valid[off_hand];
	pose.off_grip = vr_view.grips[off_hand].position;
	pose.off_forward = vr_view.grips[off_hand].forward;
	pose.off_up = vr_view.grips[off_hand].up;
	/* (a pistol held with both hands: the other hand cups the weapon's, not
	where its animations hold it, away from a pistol; not with a weapon of
	its own, its arm then not drawn) */
	pose.off_hidden = other_drawn;
	pose.off_supports = !empty && !other_drawn && !vr_view.bare && vr_view.pistol_grip &&
		vr_two_handed(frame, pose.main_left, vr_view.pistol_grip, vr_view.aim_pitch, orientation) == VR_HELD_SUPPORT;
	pose.off_on_weapon = !empty && !other_drawn && ((!vr_view.bare && !vr_view.pistol_grip &&
		vr_two_handed(frame, pose.main_left, vr_view.pistol_grip, vr_view.aim_pitch, orientation)) ||
		first_person_weapon_off_hand_busy(render.local_player_index));
	pose.off_squeeze = frame->hand[off_hand].squeeze;
	pose.off_trigger = frame->hand[off_hand].trigger;
	pose.main_bare = !other && (vr_view.bare || empty);
	pose.main_squeeze = frame->hand[main_hand].squeeze;
	pose.main_trigger = frame->hand[main_hand].trigger;
	pose.seconds = frame->display_period;
	vr_arms_pose(matrices, count, graph, &pose);
	if (other_drawn)
		vr_arms_hide_other(matrices, count, graph);
}

/* `in`, along `from`'s axes (its left `from_left`), along `to`'s instead */
static void vr_carried(
	real_vector3d const *in,
	struct render_camera const *from,
	real_vector3d const *from_left,
	struct render_camera const *to,
	real_vector3d const *to_left,
	real_vector3d *out)
{
	real forward = dot_product3d(in, &from->forward);
	real left = dot_product3d(in, from_left);
	real up = dot_product3d(in, &from->up);
	int axis;

	for (axis = 0; axis < 3; axis++)
		out->n[axis] = to->forward.n[axis] * forward + to_left->n[axis] * left + to->up.n[axis] * up;
}

/* on foot with no weapon at all: the hands as the last weapon's animations
posed them, carried from the camera they were posed from to this frame's
behind the hand (vr_render_empty_hands) */
static boolean vr_empty_hands_pose(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	struct render_camera const *from = &vr_view.empty_camera, *to = &vr_view.weapon_camera;
	real_vector3d from_left, to_left;
	short index;

	if (!vr_view.empty_valid || !unit || unit->object.parent_object_index != NONE ||
		unit_get_weapon_count(vr_view.unit_index) != 0)
	{
		return FALSE;
	}
	cross_product3d(&from->up, &from->forward, &from_left);
	cross_product3d(&to->up, &to->forward, &to_left);
	for (index = 0; index < vr_view.empty_count; index++)
	{
		real_matrix4x3 const *in = &vr_view.empty_base[index];
		real_matrix4x3 *out = &vr_view.empty_pose[index];
		real_vector3d offset, carried;

		out->scale = in->scale;
		vector_from_points3d(&from->position, &in->position, &offset);
		vr_carried(&offset, from, &from_left, to, &to_left, &carried);
		point_from_line3d(&to->position, &carried, 1.0f, &out->position);
		vr_carried(&in->forward, from, &from_left, to, &to_left, &out->forward);
		vr_carried(&in->left, from, &from_left, to, &to_left, &out->left);
		vr_carried(&in->up, from, &from_left, to, &to_left, &out->up);
	}
	return TRUE;
}

/* the weapon the other hand holds (dual_wield.c), posed from behind that
hand (first_person_weapon_other_hand_render_update): where its model's
barrel is; in the left hand its arms mirrored (the weapon never is); its
weapon's arm from that hand's shoulder, its other arm not drawn */
static void vr_other_hand_posed(
	void)
{
	short weapon = (short)(render.local_player_index + FIRST_PERSON_WEAPON_OTHER_HAND);
	struct animation_graph *graph = first_person_weapon_get_animation_graph(weapon);
	long weapon_index = dual_wield_weapon(vr_view.unit_index);
	real_matrix4x3 *matrices;
	short count;

	vr_view.other_arms_mirrored = FALSE;
	matrices = first_person_weapon_get_node_matrices(weapon, &count);
	if (!vr_view.other_valid || !graph || count <= 0 || count > MAXIMUM_NODES_PER_ANIMATION)
		return;
	vr_weapon_learn(weapon, &vr_view.other_camera, weapon_index);
	if (vr_view.main_hand == VR_HAND_RIGHT)
	{
		vr_view.other_arms_mirrored = vr_arms_mirror(matrices, count, FALSE, weapon, &vr_view.other_camera,
			weapon_index);
	}
	vr_arms_update(graph, matrices, count, FALSE, TRUE);
}

void vr_render_weapon_posed(
	void)
{
	struct animation_graph *graph;
	real_matrix4x3 *matrices;
	short count;

	vr_view.empty_posed = FALSE;
	vr_view.arms_mirrored = FALSE;
	if (!vr_view.stereo || !vr_view.weapon_in_hand || render.local_player_index == NONE)
		return;
	graph = first_person_weapon_get_animation_graph(render.local_player_index);
	matrices = first_person_weapon_get_node_matrices(render.local_player_index, &count);
	if (graph && count > 0 && count <= MAXIMUM_NODES_PER_ANIMATION)
	{
		/* (the hands as posed, kept for when no weapon is left) */
		csmemcpy(vr_view.empty_base, matrices, sizeof(*matrices) * count);
		vr_view.empty_graph = graph;
		vr_view.empty_count = count;
		vr_view.empty_camera = vr_view.weapon_camera;
		vr_view.empty_valid = TRUE;
	}
	else if (vr_empty_hands_pose())
	{
		if (vr_view.main_hand == VR_HAND_LEFT)
		{
			vr_view.arms_mirrored = vr_arms_mirror(vr_view.empty_pose, vr_view.empty_count, TRUE,
				render.local_player_index, &vr_view.weapon_camera, NONE);
		}
		vr_arms_update(vr_view.empty_graph, vr_view.empty_pose, vr_view.empty_count, TRUE, FALSE);
		vr_view.empty_posed = TRUE;
		return;
	}
	vr_weapon_learn(render.local_player_index, &vr_view.weapon_camera, vr_held_weapon());
	if (vr_view.main_hand == VR_HAND_LEFT)
	{
		vr_view.arms_mirrored = vr_arms_mirror(matrices, count, FALSE, render.local_player_index,
			&vr_view.weapon_camera, vr_held_weapon());
	}
	vr_arms_update(graph, matrices, count, FALSE, FALSE);
	vr_other_hand_posed();
}

int vr_render_empty_hands(
	struct animation_graph *graph,
	real_matrix4x3 *matrices)
{
	if (!vr_view.empty_posed || !graph || graph != vr_view.empty_graph)
		return FALSE;
	csmemcpy(matrices, vr_view.empty_pose, sizeof(*matrices) * vr_view.empty_count);
	return TRUE;
}

void vr_render_weapon_drawing(
	int drawing)
{
	static boolean both_faces;
	/* (the arms posed from the player's shoulders show sides of them the
	stock view never shows, which the model has no faces for: their backs
	are drawn too) */
	boolean two_sided = drawing && VR_RENDER_WORLD() && vr_view.stereo && vr_view.weapon_in_hand &&
		vr_view.arms_free;

	if (two_sided != both_faces)
	{
		both_faces = two_sided;
		halo_vr_two_sided(two_sided);
	}
}

void vr_render_hands_drawing(
	int drawing,
	short weapon)
{
	boolean mirrored = weapon >= FIRST_PERSON_WEAPON_OTHER_HAND ? vr_view.other_arms_mirrored : vr_view.arms_mirrored;

	halo_vr_mirror_winding(drawing && VR_RENDER_WORLD() && vr_view.stereo && mirrored);
}

int vr_render_other_hand_camera(
	long unit_index,
	struct render_camera *camera)
{
	if (!vr_view.stereo || !vr_view.other_valid || unit_index == NONE || unit_index != vr_view.unit_index)
		return FALSE;
	camera->position = vr_view.other_camera.position;
	camera->forward = vr_view.other_camera.forward;
	camera->up = vr_view.other_camera.up;
	return TRUE;
}

int vr_render_other_hand_muzzle(
	long unit_index,
	real_point3d *origin,
	real_vector3d *forward)
{
	long weapon_index = unit_index == NONE ? NONE : dual_wield_weapon(unit_index);

	/* (the weapon drawn in first person, in the other hand) */
	if (!vr_view.stereo || !vr_view.other_valid || unit_index == NONE || unit_index != vr_view.unit_index ||
		weapon_index == NONE || first_person_weapon_get_local_index(weapon_index) < FIRST_PERSON_WEAPON_OTHER_HAND)
	{
		return FALSE;
	}
	*origin = vr_view.other_muzzle;
	*forward = vr_view.other_barrel;
	return TRUE;
}

/* the HUD's ground: nothing, so that the panel shows only the HUD. Before
it, where a reticle is placed, the game's crosshair is drawn alone at the
HUD's middle (hud_weapon_port_draw_crosshairs: the weapon's own, red over a
target, as the game draws it), its square taken for the reticle's picture
(vr_openxr.c), and cleared again; the HUD then draws none (passenger_camera.c) */
void vr_render_hud_begin(
	void)
{
	long player_index = render.local_player_index == NONE ? NONE :
		local_player_get_player_index(render.local_player_index);
	int reticle;

	halo_vr_clear_transparent();
	if (player_index == NONE || player_get(player_index)->unit_index == NONE || !hud_scripted_globals->show_hud)
		return;
	for (reticle = 0; reticle < VR_RETICLE_COUNT; reticle++)
	{
		if (!vr_view.reticle_placed[reticle])
			continue;
		vr_view.drawing_reticle = TRUE;
		rasterizer_hud_begin();
		if (reticle)
		{
			hud_weapon_port_draw_crosshairs(player_get(player_index), dual_wield_weapon(vr_view.unit_index),
				(short)vr_view.other_reticle_aimed);
		}
		else
		{
			hud_weapon_port_draw_crosshairs(player_get(player_index), NONE, NONE);
		}
		rasterizer_hud_end();
		vr_view.drawing_reticle = FALSE;
		halo_vr_resolve_reticle((short)reticle, 320 - VR_CROSSHAIR_HALF, 240 - VR_CROSSHAIR_HALF,
			320 + VR_CROSSHAIR_HALF, 240 + VR_CROSSHAIR_HALF);
		halo_vr_clear_transparent();
	}
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
	vr_view.seat_switch = FALSE;
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
	vr_seat_view_switch(local_player_index);
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
	else if (vr_drive_facing(frame, forward, &facing))
	{
		/* a driver steers with the off stick; the head looks around (seen
		from behind, the view turns with the facing, which the game's camera
		follows) */
		player_control_set_facing(local_player_index, &facing);
		vr_view.set_yaw = vr_view.seat_view ? player_control_get_facing_angles(local_player_index)->yaw : before.yaw;
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
	else if (frame->controllers && vr_gunner_seat(player_control_get_unit_index(local_player_index)))
	{
		/* a gunner's seat seen from it: the hands on the gun's handles turn it
		about its pivot (vr_handles); let go, it stays where it aims. Till
		they first take them, the gun faces ahead of the seat's view, not the
		way the player faced on foot */
		real_vector3d current;
		float level[3];

		vector3d_from_euler_angles2d(&current, &before);
		vr_world_to_local(current.n, vr_view.heading, level);
		if (!vr_view.handles_taken)
			memcpy(level, local_forward, sizeof(level));
		vr_view.seat_aiming = TRUE;
		if (vr_handles(frame, level, aim))
		{
			vr_view.handles_taken = TRUE;
		}
		else if (vr_view.handles_taken)
		{
			vr_view.set_yaw = before.yaw;
			vr_view.aiming = TRUE;
			vr_input_aim_turn(0.0f);
			return;
		}
		else
		{
			memcpy(aim, level, sizeof(aim));
		}
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

int vr_render_melee_point(
	long unit_index,
	real_point3d *point)
{
	int hand = vr_input_melee_hand();

	if (!vr_view.stereo || !vr_view.hand_aiming || unit_index == NONE || unit_index != vr_view.unit_index ||
		hand < 0 || hand >= VR_HAND_COUNT || !vr_view.grip_valid[hand])
	{
		return FALSE;
	}
	*point = vr_view.grips[hand].position;
	platform_log("vr: a melee strike through the %s hand's fist%s", hand == VR_HAND_LEFT ? "left" : "right",
		hand == vr_view.main_hand ? "" : " (the other hand)");
	return TRUE;
}

int vr_render_off_hand_strike(
	long unit_index)
{
	int hand = vr_input_melee_hand();

	return vr_view.stereo && vr_view.hand_aiming && unit_index != NONE && unit_index == vr_view.unit_index &&
		hand >= 0 && hand != vr_view.main_hand;
}

int vr_render_rumble(
	short gamepad_index)
{
	return gamepad_index == 0 && vr_active();
}

void vr_render_rumble_hands(
	float const body[2],
	float const weapon[2],
	float const other[2],
	float hands[2])
{
	vr_input_rumble_hands(body, weapon, other, hands);
}

int vr_render_reticle(
	void)
{
	return !vr_view.drawing_reticle && ((vr_view.aiming && vr_view.hand_aiming) || vr_view.seat_aiming);
}

int vr_render_handles(
	void)
{
	return vr_view.handles_held != 0 || (vr_view.seat_aiming && vr_gunner_seat(vr_view.unit_index));
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

int vr_render_seat_switch(
	void)
{
	return vr_view.seat_switch;
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

int vr_render_legs(
	float under[3],
	float forward[3])
{
	if (!VR_RENDER_WORLD() || !vr_view.stereo || vr_view.seat_view || vr_view.cinematic)
		return FALSE;
	forward[0] = (float)vr_view.body_forward.i;
	forward[1] = (float)vr_view.body_forward.j;
	forward[2] = 0.0f;
	under[0] = (float)vr_view.head_camera.position.x - forward[0] * VR_LEGS_BACK * VR_UNITS_PER_METRE;
	under[1] = (float)vr_view.head_camera.position.y - forward[1] * VR_LEGS_BACK * VR_UNITS_PER_METRE;
	under[2] = (float)vr_view.head_camera.position.z - VR_LEGS_NECK_DROP * VR_UNITS_PER_METRE;
	return TRUE;
}

float vr_render_aim_pitch(
	void)
{
	vr_settings_read();
	return vr_view.aim_pitch;
}

int vr_render_pistol_grip(
	void)
{
	return vr_view.pistol_grip;
}

int vr_render_bare_hands(
	void)
{
	return vr_view.stereo && vr_view.weapon_in_hand && vr_view.bare;
}

int vr_render_grab_reach(
	int hand)
{
	return vr_view.stereo && hand >= 0 && hand < VR_HAND_COUNT && vr_view.grab_reach[hand];
}

int vr_rules_active(
	void)
{
	struct network_game *game = network_game_get_game();

	/* (a local game has a server of its own, which takes no one else) */
	return !game || network_game_is_splitscreen_local() ||
		TEST_FLAG(game->cooperative_flags, _network_game_port_vr_rules_bit);
}

int vr_render_on_foot(
	void)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);

	return vr_view.stereo && vr_view.hand_aiming && unit && unit->object.parent_object_index == NONE;
}

int vr_render_seated(
	void)
{
	short local_player_index = local_player_get_next(NONE);
	long vehicle_index;
	struct unit_seat const *seat;

	/* (the local player's unit: vr_view.unit_index is not kept in a seat
	without a weapon of the hand's) */
	return vr_active() && local_player_index != NONE &&
		vr_seat_of(player_control_get_unit_index(local_player_index), &vehicle_index, &seat);
}

int vr_render_shoulder_full(
	int side)
{
	struct unit_datum *unit = unit_try_and_get(vr_view.unit_index);
	short kept;

	if (!vr_view.stereo || !vr_view.hand_aiming || !unit || unit->object.parent_object_index != NONE ||
		side < 0 || side >= VR_HAND_COUNT || vr_view.shoulder_unit != vr_view.unit_index)
	{
		return FALSE;
	}
	kept = vr_view.shoulder_slot[side];
	if (kept < 0 || kept >= MAXIMUM_WEAPONS_PER_UNIT)
		return FALSE;
	return unit_inventory_get_weapon(vr_view.unit_index, kept) != NONE &&
		(kept != unit->unit.current_weapon_index || !vr_input_holding()) && kept != dual_wield_slot(vr_view.unit_index);
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

int vr_render_dual_wield(
	void)
{
	return vr_rules_active();
}

int vr_render_carries_four(
	long unit_index)
{
	return vr_render_dual_wield() && vr_view.stereo && vr_view.hand_aiming && unit_index != NONE &&
		unit_index == vr_view.unit_index && vr_input_physical();
}

int vr_render_off_hand_grip(
	long unit_index,
	struct real_matrix4x3 *grip)
{
	if (!vr_view.stereo || !vr_view.off_grip_valid || !vr_view.hand_aiming || unit_index == NONE ||
		unit_index != vr_view.unit_index)
	{
		return FALSE;
	}
	grip->scale = 1.0f;
	grip->forward = vr_view.off_grip.forward;
	grip->up = vr_view.off_grip.up;
	cross_product3d(&grip->up, &grip->forward, &grip->left);
	grip->position = vr_view.off_grip.position;
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
	vr_view.handles_held = 0;
	vr_view.handles_taken = FALSE;
	vr_view.steady.valid = 0;
	vr_view.seat_switch = FALSE;
	vr_view.view_switches = vr_input_view_switches();
	vr_view.seat_eye_valid = FALSE;
	vr_view.eye_crouch_tick = NONE;
	vr_view.head_top_valid = FALSE;
	vr_view.pistol_grip = FALSE;
	vr_view.bare = FALSE;
	vr_view.empty_valid = vr_view.empty_posed = vr_view.arms_mirrored = FALSE;
	{
		float velocity[3], place[3];
		int hand;

		vr_view.throws = vr_input_throw(velocity, place, &hand);
	}
	vr_view.rules = -1;
	vr_view.shoulder_unit = NONE;
	vr_view.held_weapon = NONE;
	vr_view.grab_reach[VR_HAND_LEFT] = vr_view.grab_reach[VR_HAND_RIGHT] = FALSE;
	vr_view.vignette_valid = FALSE;
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
