# VR

The desktop builds (Windows and Linux, OpenGL) can draw into a VR headset
through any OpenXR runtime that gives OpenGL images: SteamVR, Meta's PC
runtime and Monado. It is off by default (`display.vr`), and nothing changes
for a player who does not turn it on. How to use it is in "VR" in
`port/linux/README.md`; this document is how it is built, and what is still
to come.

Two VR conversions of the decompilation, both released under CC0, showed
that the game's side of it can be done, and much of the design comes from
them: [astromaddie/HaloCE-VR](https://github.com/astromaddie/HaloCE-VR)
(stereo windows in `main_game_render`, the gun at the controller, arm IK,
a picture-in-picture scope, the HUD on a quad layer) and
[moistman42069/HaloCE-DecompRemix-VR-PC-Android](https://github.com/moistman42069/HaloCE-DecompRemix-VR-PC-Android)
(the hook points in `source/`, immersive cinematics, holsters, the
left-handed mirror of the buttons, a first network pose). None of their code
is taken as it is: what is used is rewritten to the project's rules and
tested, and each commit that uses it says where it comes from.

## Off costs nothing

- `display.vr` gates all of it. Off, the runtime is not loaded, nothing is
  allocated, and each frame tests one flag.
- The flat picture with it off is compared byte for byte with `main`'s on
  fixed scenes.
- `HALO_GLES` builds (Android, the web) compile none of it.

## The runtime

The builds are 32-bit, so they need the runtime's 32-bit library: Meta's
runtime has one, SteamVR has one, and Windows lists them in the registry's
32-bit view. The Khronos loader is C++, so the game loads the runtime itself,
as the OpenXR loader specification describes
(`port/linux/src/vr_openxr.c`): `XR_RUNTIME_JSON` or the active runtime's
manifest (`port/windows/src/win32_vr.c`, `port/linux/src/posix_vr.c`), its
`library_path`, then `xrNegotiateLoaderRuntimeInterface`. Only the Khronos
headers are vendored (`port/third_party/openxr`). API layers are not loaded.

An OpenGL image's rows are from the bottom, in OpenXR as in OpenGL, and
Meta's runtime shows them so. SteamVR's (2.18, 32-bit, a Rift) showed the
game upside down in the headset: it takes them from the top, as
Direct3D's. `vr.image_rows` (`"auto"`) writes every image the runtime is
given, the eyes', the panel's, the scope's, the reticle's and the beam's,
turned over for a runtime named SteamVR; `"top"` or `"bottom"` choose.

The session draws on the game's own context with `XR_KHR_opengl_enable`
(WGL; GLX, or EGL with `XR_MNDX_egl_enable`). Windows Mixed Reality's and
Virtual Desktop's runtimes give Direct3D and Vulkan images only; they could
be served later with `XR_KHR_D3D11_enable` and `WGL_NV_DX_interop2`.

## The world's size

What sets the size the world is seen at, each measured or derived:

- A world unit is 10 feet, 3.048 m (`VR_UNITS_PER_METRE` 0.328): the
  Master Chief, 2.08 m tall in armour, has his eyes at 0.62 units (1.893 m
  over the ground, measured in Blood Gulch). LivingFray's HaloCEVR and
  astromaddie's port take the same unit.
- The eyes are the runtime's, apart by its IPD in those units: the stereo
  gives distances and sizes in metres.
- Each eye's projection is its own off-centre field, the runtime's angles:
  `render_camera_build_frustum` with a 90-degree field and bounds of the
  angles' tangents (`vr_fov_bounds`) scales x by 2 / (tan right - tan
  left) and y by 2 / (tan up - tan down), the OpenXR projection.
  `display.fov` is not applied to the eyes.
- The eyes' height (`vr.eye_height`). "chief", the default: the Chief's
  1.89 m, with the head as high as it has been since the runtime last
  recentred, and lower by as much as the head is below that; a crouch of
  the button lowers them to the game's crouching camera, and a real crouch
  and the button's do not add up (the lower of the two). The player is the
  cyborg: their eyes over its legs and between its shoulders, as tall as
  it (at a player's own height, the legs drawn under them put the eyes at
  the cyborg's chest). "player" puts them at the player's height over the
  floor, the runtime's STAGE space; the game's crouch lowers them in
  proportion (its crouching camera is 57% of its standing one), less what
  the head has gone down already (checked in a simulated game: 1.632,
  0.922, 0.922 and, the head 0.9 m down, 0.732 m). On foot the body
  follows the head across the room, not up and down.
- The legs (`display.first_person_legs`) are drawn under the head, facing
  as the body the arms hang from does (`first_person_legs.c`,
  `vr_render_legs`): not set back from the eye, which looks about on its
  own. In the headset the body keeps its spine and collarbones (the
  torso, seen looking down) and folds its head, neck and arms into the
  joint each hangs from. The base of its neck goes 25 cm behind the eyes
  (the armour's chest stands out past a chest: looking down, the eyes
  look past it at the feet) and 22 cm below them, the body grown or shrunk
  from the feet (0.7 to 1.3 times) to put it there: at any eye height
  (`vr.eye_height`, or the head lowered) the eyes are never inside the
  chest and the feet stay on the ground. No lid over the waist hides the
  floor.

## A frame

- `xrWaitFrame` at the top of the main loop paces the game in place of the
  window's vsync; the eyes and the head are located at the frame's predicted
  display time. The game's ticks stay at 30 a second, and each frame is
  blended between them, as `display.interpolation` does.
- `main_game_render`'s player window becomes the left eye's, the right
  eye's and the HUD's windows (`port/linux/game/vr_view.c`). Each eye's
  camera is the game's camera as the game made it (in a vehicle, dead, in a
  script), with the head put on it last: its offset from where the body
  stands in the room, stopped short of walls, the eye's own offset and the
  head's orientation, roll included. Its frustum is the eye's off-centre
  field, which `render_camera_build_frustum` takes as it is. The heading
  (the yaw LOCAL's forward has in the world) is the aiming player's; with
  no player aiming (the main menu's scene) it is the game camera's less
  the head's yaw as the view began, so that the head turns about the
  scene.
- Room scale (`vr.roomscale`, `vr_room_follow`): the body's place in the
  room is set under the head as the player stands up, sits, dies or a
  cinematic's shot begins. On foot, with the off stick still, the unit
  walks after the head (`vr_input.c` pushes the stick toward it, in the
  head's axes, from 10 cm until 3 cm), and the game's camera's moves while
  it walks and coasts carry the body along, so the room stays still around
  the head: the unit's own physics keeps it out of walls and the network
  sees only its input. The head reaches 60 cm from the body on foot (the
  unit held back, or the stick moving it), 35 cm in a seat (a lean), and
  anywhere in the menus, cinematics and while dead.
- The comfort vignette (`vr.vignette`, `vr_comfort`): each frame the
  world's place of LOCAL's origin (the head's place less the head's own
  offset in the room) and the heading are compared with the last frame's:
  their speed (against 3 m/s) and turn (against 1.5 radians a second)
  close it, in 0.1 s, and it opens in 0.3 s. A move past 1 m or a turn past
  0.35 radians in a frame (a teleport, a snap turn) is not counted, and
  none is drawn in a cinematic. `vr_resolve_eye` darkens each eye's image
  after the copy, in the eye's own field: the clear part is a cone about
  the eye's axis, from 85 degrees to 35, the dark 20 degrees beyond.
- What moves once a frame (the sky's animation, weather, glows, the
  first-person weapon's pose) moves in the left eye only.
- The HUD is drawn alone over a clear ground and shown on a quad layer held
  before the head. While a menu is up its picture is a quad held in the
  room instead, where the head faced as the menu came up; the main menu's
  scene is seen in stereo behind it. Loading screens and cinematics are
  shown on a quad held in the room. The window shows the left eye.

## The controllers

- `vr_openxr.c` makes one action set: each hand's grip and aim poses,
  trigger, grip, stick, stick click, two face buttons, menu and vibration,
  with suggested bindings for the Touch, Index, Vive, Windows Mixed Reality
  and simple controllers. They are read each frame for its display time,
  only while the session is focused, the trigger and grip with hysteresis.
- `vr_input.c` turns them into the first Xbox controller's state, so the
  game, the menus and the player's button preset take them as a pad's: a
  layout of the VR conversions' (the table in the README), each button of
  the aiming (main) hand and the other (off) bound to an action
  (`vr.bindings` binds them otherwise), the action pressing the standard
  preset's button for it. The sticks and the face buttons are each
  controller's, by `vr.left_handed`'s layout; the trigger and the grip are
  the weapon's hand's or the other's, so a weapon passed to the other
  hand fires from it and nothing else moves. With the gestures (a punch,
  none while a grenade is held or for half a second after; the off grip
  at the visor's front; the head lowered) as presses of the same
  buttons. The main grip pressed at a shoulder (along the head's yaw, 8
  to 50 cm to its side, 30 cm under to 30 cm over the eyes, 12 cm before
  to 50 cm behind them: by the ear and back over the shoulder, where a
  player's hand let go putting a weapon over the shoulder in a headset;
  not before the chest, nor before the face) is the shoulder's
  (`vr_view.c`'s `vr_shoulders`): each keeps a slot of the inventory (at
  first the main hand's side the first, the other the second); the weapon
  held is put away if it is the one kept there (the hands bare: the
  first-person weapon is not drawn, the arms are, the weapon's hand's
  fingers its controller's; the trigger and the zoom press nothing), else
  the one kept there is drawn (`player_control_set_desired_weapon`, as a
  switch of the player's own, which the action sends), the one held going
  to the other shoulder. Nothing else changes: the game's player holds its
  weapon, as the other machines see it. Either grip pressed with a weapon
  on the ground (no one holds it) within 25 cm of it, along its length
  (its origin, about its grip, through its middle and as far again: its
  bounding sphere is far wider; a hand that holds no weapon feels a light
  pulse, 0.02 s, as one comes within it), holds the action
  (0.6 s at most, as long as the grip and the weapon stay), which takes a
  weapon as the game takes it; one that comes to the player within a
  second goes to that hand (the hands swap if it was the off hand's). The off grip pressed at the
  main hand takes the weapon there; if within 1.5 s the hands are 25 cm
  apart, the main hand having moved more, the weapon changes hands until
  `vr.left_handed` is changed (a hand-over, after astromaddie's gesture;
  the buttons stay);
  hands that stay together hold it with both (a pistol's grip), and the
  off hand pulling away lets go. With `vr.hands` `"physical"` (and the
  gestures) the hands hold as hands do (`vr_physical_hands`): the main hand
  holds the weapon while its grip is held (a weapon that comes otherwise, at
  a new life or a switch, until the grip is pressed and let go; with
  `vr.grip_toggle` a press holds, another lets go). Let go with the other
  hand holding it too (`vr_two_handed`), the hands swap; else the weapon is
  let go at the shoulder there (kept there, the one kept there going to the
  other shoulder) or at none (back at its own), and the hands are bare. An
  empty hand's grip pressed at a shoulder keeping a weapon not in the hand
  draws it, into that hand; at the visor it is the flashlight; at a weapon
  on the ground it takes it. The triggers are bound by what the hand holds:
  the main hand's fires while it holds the weapon, an empty hand's is the
  grenade's button, a hand holding the weapon too presses nothing; the main
  grip binds nothing, and a punch of the off hand strikes too while it is
  empty. vr_view.c is told what a grip did at a shoulder (pressed, drew, or
  let go) and keeps the slots as above. The grenade's button held takes a grenade
  (`vr.throw_gesture`): let go with the hand faster than 1.5 m/s (its
  fastest of the last moments, falling by 8 m/s a second, as a hand slows
  to let go; still counted where the tracking lost it in the swing) it
  presses the button a tenth of a second, the game's throw, and let go
  slower it presses nothing (in a vehicle's seat the button is held as a
  pad's: the seat's secondary trigger). The throw keeps the hand's smoothed velocity
  at its fastest and where the hand let go; `vr_view.c` gives them, in
  the world's axes, to `network_vr_throw` (a VR server's client sends
  them to the host, distributed message 95, reliably; the host, or a
  game with no network, keeps them a second), and when the game lets
  the grenade go (`unit_throw_grenade_release`) it leaves from the hand,
  unless a wall is between the hand and the eyes, the way the hand threw
  it: a firm throw (the hand at 7.5 m/s) as fast as the game's own throw
  (30 m/s by the world's metres, which no hand throws), a softer or a
  harder one in proportion, between 0.4 and 1.25 times the game's
  (`network_vr_grenade_throw`). The hands' speeds start again after the menus,
  so a hand moved there neither strikes nor throws. The game's motors are felt in the hands:
  the weapon's hand feels both, the other the left (the heavy one: hits,
  blasts) and, while it holds the weapon too, the right (the shots). The menu button is Start, or Back held a
  second. It is a function of the frame and its own state, tested in the
  harness (`tools/harness/tests/test_vr_input.py`). Rebinding is the
  setting's, not the runtime's: actions bound by each runtime's own tool
  (SteamVR's has one, Oculus's none) would not follow a swap of hands.
- The main hand's stick turns the heading by snaps or smoothly; it never
  reaches the game's right stick.
- The hand aims on foot (`vr.aim`): the facing is where the hand points,
  its aim pose's -z (the pointing ray each runtime gives its controllers;
  the grip pose's -z runs along the handle, 60 degrees higher on a Touch
  controller), turned up by `vr.aim_pitch`; with the off hand's
  grip ahead of the main hand on a weapon not held as a pistol (by its
  label: "pistol", "plasma pistol", "needler"), from the main hand to the
  off; with it at the main hand (supporting it), as the main hand. The
  off stick is turned to move where the head faces, and the first-person
  weapon is posed from a camera behind the hand.
- The weapon stays as the hand holds it: `first_person_weapons.c` gives it
  no walking bob, no sway as the player moves or turns (the hand turns the
  player) and no idle posing; firing, reloading, throwing and meleeing keep
  their animations. Once posed, while it is idle, its model's muzzle (the
  "primary trigger" marker) is read in the camera's axes, by weapon type
  and eased over frames: the camera is turned so that the barrel lies along
  the aim (the stock models' already do), and the muzzle is where the shot
  starts. Until then a fixed offset from the grip stands in.
- In the left hand the weapon is not mirrored: the left controller holds
  it as the right holds it, so it is the same weapon in either hand, and
  the camera, the muzzle and the scope keep their offsets. The arms are
  mirrored (the hands' model's nodes that the weapon's is not,
  `first_person_weapon_arm_nodes`) across the weapon's middle plane
  (upright along the weapon camera, through the muzzle's line), so that
  the right arm that holds it is a left arm; while the hands' model is
  drawn its triangles' winding is turned over (`halo_vr_mirror_winding`).
  The empty hands are mirrored the same way.
- With no other machine in the game (whose players would see the shot from
  the facing alone), `unit_adjust_projectile_ray` starts the shot at that
  muzzle, unless a wall is between it and the eye, and `aim_assist.c` aims
  it from there along the aim, not turned to where the eye's ray meets the
  world (as a weapon away from the eye is on a screen). A reticle (a quad
  layer) is held where the shot's ray (`weapon_get_projectile_ray`, the
  game's own) meets the world, facing the head and as wide to the eye at any
  distance, red when the game's crosshair would be; the HUD draws no
  crosshair. The prompts' button icons are the controllers' buttons.
- Each arm is its own (`vr.arms`, `port/linux/game/vr_arms.c`). The game
  poses its first-person model, both arms and the weapon, from the camera
  behind the aiming hand; the model's node matrices are then posed again:
  the shoulders are the player's (22 cm under the eyes, 11 cm behind them,
  18 cm either side of a body that turns after the head once it is 35
  degrees off), the weapon's hand stays where the game holds the weapon
  (its fingers and the weapon as animated), and the other hand holds its
  controller as the weapon's hand holds the weapon's grip, mirrored (its
  place and its palm's way, from the wrist to the middle finger and across
  the knuckles, against the main controller's grip pose: OpenXR's is where
  a held gun's grip is). That hand goes to the weapon, blended over 0.15 s,
  while both hands hold it (`vr_two_handed`) and while an animation of its
  own plays (readied, put away, reloaded, a grenade thrown, a melee:
  `first_person_weapon_off_hand_busy`). The free hand's fingers are the
  controller's: open at rest, closed as its grip is squeezed, the index by
  its trigger (so that the grip alone points it), each bending at the
  knuckle and the middle joint toward the palm; the thumb as animated.
  Each arm reaches its wrist with its
  own bones' lengths (0.312 and 0.332 m for the stock arms) by two-bone IK
  (`vr_arm_elbow`, the elbow down and out); a wrist out of reach moves the
  shoulder after it; each bone turns the least that takes it there, and the
  forearm takes half the wrist's twist about it (`vr_twist_about`). The
  arms' root goes between the shoulders, so that the skin it shares with
  the upper arms does not stretch to the weapon's camera. HaloCEVR
  (LivingFray's) draws the hands alone, at the controllers; here the arms
  are whole.
- A zoomed weapon in the hand stays in it, and draws one window more,
  before the HUD's: the world from the muzzle with the zoom's field (without
  the weapon), in a square of the back buffer, copied into the scope's
  image, a quad held on the weapon. The eyes keep their field, and the HUD
  leaves out the zoomed view's elements (`hud_weapon.c`).
- A vehicle's seat is seen from it (`vr.vehicle_view`, astromaddie's
  seat view): `director_choose_game_perspective` takes the first person's
  camera there for the local view only (`director_camera_deterministic`,
  which aims the shots, is unchanged), so the eyes are at the unit's and
  its body is not drawn. The heading is the vehicle's yaw, blended between
  the ticks as the vehicle is drawn, and the seat's own turn: level, the
  vehicle's pitch and roll not tilting the horizon. Getting in, the seat's
  forward is put ahead of the head; getting out, the heading on foot goes
  on from the seat's; both with a blink. The main hand's stick turns the
  seat's view.
- What aims in a seat (`vr.vehicle_aim`): the main hand's aim (a gun, a
  tank's cannon, where a flier goes; a passenger's own weapon is held in
  the hand as on foot), its stick as the pad's right stick (`vr_input.c`:
  the facing turns as a pad turns it, the view with the facing or, seen
  from the seat, with the vehicle), or the head. A Warthog's or a Ghost's
  driver steers with the off stick (`vr.vehicle_steering`,
  `vr_drive_steer`): the facing is the vehicle's forward turned in
  proportion to its sideways push up to the wheels' limits (read from the
  vehicle's definition), mirrored while it backs, and the pad's left stick
  keeps its throttle alone. The reticle stands for the HUD's crosshair in
  seats, along the ray of the weapon the HUD would show (the passenger's
  own, a gunner's the vehicle's), the vehicle not in its way.
- A cinematic (`vr.cinematics`) is seen around the head, from its camera:
  each shot is set ahead of where the head faced as it began and turns as
  its camera turns (its pitch and roll are the head's), and a cut (the
  camera moved or turned further than it does in a frame) blacks the eyes
  for a tenth of a second (moistman42069's anchor and blink). The
  letterbox is the console window's and is not in the eyes. Meanwhile the
  facing is left to the scripts, and their turns turn the heading after.
- In the menus a hand's ray meets the panel shown (`vr_ray_panel`), and
  `halo_ui_pointer_update` takes it as the mouse; its trigger clicks, a dot
  is drawn on the panel, and a beam (a quad layer over the panel, turned
  about its length to face the head: astromaddie's) runs from the hand to
  it. A menu's panel and the flat screen are each placed where the head
  faced as it came up, and kept there through a moment of the other (a
  menu's change).
- The headset's settings have screens of their own (`tools/port_settings.py`:
  VR HEADSET, under Gamepad Setup and the pause menu's Settings, and its
  HANDS AND BODY), whose changes the game takes at once (`config_changes`),
  but `display.vr`'s. Their rows but the switch are the menus' platform
  `vr` (`menu_files.c`): the desktop's with `display.vr` on as the menus
  are read, so without the headset the screen has the switch alone and the
  pause menu no VR HEADSET; the generator keeps those rows after the
  desktop's others, so that they leave no gap.
- `debug.vr_simulate` gives a headset and two hands of the game's own, and
  `debug.vr_simulate_input` scripts their buttons, sticks and poses, so the
  views and the controls can be checked without a headset
  (`debug.vr_dump_frame` writes the images).

## VR servers

A host decides how its game's headsets play (`network.vr_rules`, VR SERVER
on the VR HEADSET screen, false by default). On any other server, a VR
player plays as any player does: the hands' holding, shoulders, bare hands
and taking with the hand all come down to the buttons and the weapon
switches a gamepad presses, and nothing a network game sends changes. A VR
server says so with the game's settings (a bit of the game's port switches,
`_network_game_port_vr_rules_bit`, network version 39), which every machine
has, and in its LAN advertisement and internet listing (a bit of its kind,
`GAME_MODES_KIND_VR`: the server browser shows "VR" before its gametype).
Its own rules (weapons let go falling or thrown, a weapon in each hand, the
other players' bodies as their headsets pose them) come with the steps
below; a game with no network, the campaign or a local game, plays by them.

In a VR server's game each headset's player's machine sends the host the
head's and the hands' places from the player's unit and their axes once a
tick (`port/linux/game/network_vr.c`, network version 40). The host takes a
pose only of the sending machine's own player, newer than the last, its head
within 0.5 world units (1.5 m) of the unit and its hands within 0.4 (1.2 m)
of the head, and sends it on to each machine that sees the player; every
machine keeps each player's latest for a second, for its body to be posed by.
The pose is only drawn: the game's hits, shots and movement stay the unit's.

Every machine, a flat one too, draws such a player's body as the headset
poses it (`port/linux/game/network_vr_body.c`): the head turned from where
the game has it look to where the headset looks, each arm reaching from its
shoulder to its controller with its own bones' lengths, the elbow down, out
and back, and the hand turned from the game's aim to the controller's way,
holding the weapon as the game's hand holds it (the model's right arm is
the weapon's hand's). The fingers and the weapon move with the hand. A copy
of the pose is posed as a frame is drawn, so the body's hit boxes and
markers stay the game's.

A VR server's headset's player who lets go of the weapon at no shoulder
lets it fall, thrown with the hand's velocity (its last 50 ms): the game
itself drops it in a game with no network, the host when the player's
machine asks it (network version 41), checking that the player is the
machine's own, on foot and alive, no oftener than every half second and
no faster than 0.3 world units a tick (27 m/s). On any other server the
weapon let go goes back to its shoulder, as before.

By a VR server's rules (and in a game with no network) the other hand that
draws at a shoulder while the main hand holds its weapon holds the
shoulder's weapon itself (`port/linux/game/dual_wield.c`). The weapon is
readied as the game readies the weapon in hand, held at the unit's left
hand, and told its controls each tick from that hand's trigger (and the
reload button) as the unit tells the weapon in hand, so that its rate of
fire, heat, ammunition and effects are its own; its shots leave its
muzzle along where the hand points, and it is drawn at the hand. That
hand's trigger throws no grenade meanwhile, and the hands holding
weapons apart do not hold one with both. Let go at a shoulder that keeps
none, the weapon is kept there; elsewhere it falls, thrown as the hand
moved; the main hand letting go of its own (to a shoulder, or falling)
passes the other hand's weapon to the main hand, the hands swapping. Before the game readies a weapon in hand, drops the unit's
inventory or deletes it, the other hand's weapon goes back first. The
HUD shows the weapon in hand's ammunition only. In a network game the
player's machine says with the pose which slot the other hand holds and
how far its trigger is pulled (network version 42); every other
machine's copy of the unit, the host's too, holds that weapon in its
other hand (the posed body's) and fires it with that trigger, so its shots
are seen everywhere and its ammunition is the host's. Its hits are the
player's machine's to report, as any shot of theirs; the host takes them
at twice a player's rate for three seconds after the player last held
one. A simulated headset that is to join a network game keeps time with
`debug.vr_simulate_paced` (a host drops a client whose clock runs fast).

By those rules, with the hands as they hold, a headset's player carries
four weapons, not two: one in each hand and one at each shoulder. Each
shoulder keeps one weapon or none (`vr_places` in `vr_view.c`): a weapon
drawn leaves its shoulder keeping none, one let go at a shoulder that
keeps none is kept there, and one let go at a shoulder that keeps another,
or at none, falls. A weapon that is nowhere (picked up, readied by the
game) goes to a shoulder that keeps none, the other hand's first. The
weapons past the second are taken by the hand (the swap its grab asks
for adds the weapon, none dropped, in `players.c`; the host's for a VR
server's player whose pose comes, `network_vr_carries_four`): the one
taken goes to the hand that took it, and the hand that held a weapon
then, still gripping it, goes on holding it in the other hand. The other
hand's weapon let fall is asked of the host with its slot (network
version 43), which drops it as a swap drops a weapon
(`unit_drop_inventory_slot`). On any other server a player carries two,
the shoulders as before.

## To come

Each step is a pull request of its own, with harness tests and the pixel
check of the flat picture:

1. The runtime, the stereo view, the HUD's layer and the window's mirror.
2. The controllers, the gun held in the hand, the floating crosshair, the
   picture-in-picture scope, the gestures (melee, flashlight, crouch,
   holsters, two hands), haptics, the menus' pointer and the immersive
   menus.
3. Swapping hands, rebinding.
4. Vehicles seen from the seat, aimed by the hand and driven by the
   stick, immersive cinematics (moistman's: the camera anchored at each
   cut, with a blink), and the camera kept right across seats,
   checkpoints and loads (with 2 and 3, this branch).
5. Other players' heads and hands online. The pose is only drawn: hit boxes,
   shots and movement do not change. A VR player sends its head and hands
   relative to its unit, in millimetres and smallest-three quaternions
   (about 30 bytes a tick); the host passes them on only to players who can
   see that player, and each machine poses the arms with two-bone IK on the
   stock biped for the VR players near it. Everything received is checked
   (finite, within reach, the sender's unit, rate, sequence), and
   `HALO_PORT_NETWORK_VERSION` is raised.
6. Comfort: height calibration (snap and smooth turning, the vignette, the
   eyes at the player's height and the settings screen are in this branch).
