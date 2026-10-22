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
  before the head. The menus, loading screens and cinematics are shown on a
  quad held in the room. The window shows the left eye.
- Until the controllers come, the head aims: the player's facing follows
  it, and the mouse and the stick turn the body.

## To come

Each step is a pull request of its own, with harness tests and the pixel
check of the flat picture:

1. The runtime, the stereo view, the HUD's layer and the window's mirror
   (this one).
2. The controllers: OpenXR actions with suggested bindings for Touch, Index,
   Vive and WMR controllers, rebinding, a left-handed preset, the gun at the
   hand, weapons and grenades aimed by it, a floating crosshair, and button
   prompts that name the controller's buttons
   (`xrGetInputSourceLocalizedName`).
3. Gestures: melee by a swing, the flashlight by touching the head,
   crouching, shoulder holsters, swapping hands, two-handed aiming, a
   picture-in-picture scope, haptics.
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
