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

The session draws on the game's own context with `XR_KHR_opengl_enable`
(WGL; GLX, or EGL with `XR_MNDX_egl_enable`). Windows Mixed Reality's and
Virtual Desktop's runtimes give Direct3D and Vulkan images only; they could
be served later with `XR_KHR_D3D11_enable` and `WGL_NV_DX_interop2`.

## A frame

- `xrWaitFrame` at the top of the main loop paces the game in place of the
  window's vsync; the eyes and the head are located at the frame's predicted
  display time. The game's ticks stay at 30 a second, and each frame is
  blended between them, as `display.interpolation` does.
- `main_game_render`'s player window becomes the left eye's, the right
  eye's and the HUD's windows (`port/linux/game/vr_view.c`). Each eye's
  camera is the game's camera as the game made it (in a vehicle, dead, in a
  script), with the head put on it last: its offset, limited to a lean and
  stopped short of walls, the eye's own offset and the head's orientation,
  roll included. Its frustum is the eye's off-centre field, which
  `render_camera_build_frustum` takes as it is.
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
  preset's button for it; mirrored for the left-handed; with the gestures
  (a punch, the off grip at the head, the head lowered, the main grip at a
  shoulder) as presses of the same buttons. The palms together with the
  off grip pressed swap the hands until `vr.left_handed` is changed
  (astromaddie's gesture). The menu button is Start, or Back held a
  second. It is a function of the frame and its own state, tested in the
  harness (`tools/harness/tests/test_vr_input.py`). Rebinding is the
  setting's, not the runtime's: actions bound by each runtime's own tool
  (SteamVR's has one, Oculus's none) would not follow a swap of hands.
- The main hand's stick turns the heading by snaps or smoothly; it never
  reaches the game's right stick.
- The hand aims on foot (`vr.aim`): the facing is the aim pose's (or, with
  the off hand's grip near the weapon, from the main hand to the off), the
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
- In the left hand the posed model, arms and all, is mirrored across the
  weapon camera's upright plane (astromaddie's way), so that the right
  hand's model is the left's; the camera, the muzzle and the scope take the
  right hand's offsets mirrored, and while the first-person weapon is drawn
  (`viewmodel_projection_begin` to its end) its triangles' winding is
  turned over (`halo_vr_mirror_winding`). The barrel is learnt from the
  model before it is mirrored.
- With no other machine in the game (whose players would see the shot from
  the facing alone), `unit_adjust_projectile_ray` starts the shot at that
  muzzle, unless a wall is between it and the eye, and `aim_assist.c` aims
  it from there along the aim, not turned to where the eye's ray meets the
  world (as a weapon away from the eye is on a screen). A reticle (a quad
  layer) is held where the shot's ray (`weapon_get_projectile_ray`, the
  game's own) meets the world, facing the head and as wide to the eye at any
  distance, red when the game's crosshair would be; the HUD draws no
  crosshair. The prompts' button icons are the controllers' buttons.
- A zoomed weapon in the hand stays in it, and draws one window more,
  before the HUD's: the world from the muzzle with the zoom's field (without
  the weapon), in a square of the back buffer, copied into the scope's
  image, a quad held on the weapon. The eyes keep their field, and the HUD
  leaves out the zoomed view's elements (`hud_weapon.c`).
- In the menus a hand's ray meets the panel shown (`vr_ray_panel`), and
  `halo_ui_pointer_update` takes it as the mouse; its trigger clicks, and a
  dot is drawn on the panel.
- `debug.vr_simulate` gives a headset and two hands of the game's own, and
  `debug.vr_simulate_input` scripts their buttons, sticks and poses, so the
  views and the controls can be checked without a headset
  (`debug.vr_dump_frame` writes the images).

## To come

Each step is a pull request of its own, with harness tests and the pixel
check of the flat picture:

1. The runtime, the stereo view, the HUD's layer and the window's mirror.
2. The controllers, the gun held in the hand, the floating crosshair, the
   picture-in-picture scope, the gestures (melee, flashlight, crouch,
   holsters, two hands), haptics, the menus' pointer and the immersive
   menus.
3. Swapping hands, rebinding (with 2, this branch).
4. Vehicles steered by the stick, immersive cinematics (moistman's: the
   camera anchored at each cut, with a blink), and the camera kept right
   across seats, checkpoints and loads.
5. Other players' heads and hands online. The pose is only drawn: hit boxes,
   shots and movement do not change. A VR player sends its head and hands
   relative to its unit, in millimetres and smallest-three quaternions
   (about 30 bytes a tick); the host passes them on only to players who can
   see that player, and each machine poses the arms with two-bone IK on the
   stock biped for the VR players near it. Everything received is checked
   (finite, within reach, the sender's unit, rate, sequence), and
   `HALO_PORT_NETWORK_VERSION` is raised.
6. Comfort: snap and smooth turning, a vignette, height calibration and a
   VR settings screen.
