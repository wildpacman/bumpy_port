# 3D Mode: Widescreen From 4:3, No Parallax — Design

Date: 2026-07-08
Status: approved (fixes.md item 1)
Builds on: `2026-07-08-3d-render-mode-design.md` (feat/3d-render-mode)

## Problem

The 3D diorama currently letterboxes to the 2D logical size (320:200 by default,
320:240 after Alt+A) and builds its frustum from that viewport. Two problems:

1. The stage is proportioned from square pixels (16:10), not from the 4:3 CRT
   presentation the art was targeted at. The user wants the 3D scene to look like
   the 2D game in 4:3 — same field size, same (1.2x taller) pixel proportions —
   with 3D built *around* that picture.
2. The Alt+A 4:3 case in 3D is actually broken: the vertical frustum is fixed at
   200 board px while the aspect drops to 1.333, so it crops ~27 px off each side
   instead of stretching taller (the comment in scene_renderer.cpp claims
   otherwise).

Additionally, the ball-following parallax camera is unwanted: the scene should sit
still, like the original.

## Decisions

- **Parallax removed entirely.** Camera fixed at the board centre. `SceneCamera`,
  `kParallaxGain`, `kParallaxEase`, the easing block in sdl_app.cpp, and the wall's
  parallax-travel margin are deleted.
- **4:3 is the base.** Board pixels present 1.2x taller than wide (240/200 CRT
  correction), exactly matching the flat path's Alt+A 4:3 presentation. In a 4:3
  window the 3D stage is pixel-for-pixel congruent with 2D 4:3.
- **Fill the window, never letterbox, never crop the field.** The viewport is the
  full window at any aspect (no cap on ultrawide, no letterbox on portrait). The
  frustum always contains the whole 320x200 field:
  - window wider than 4:3 → field fills the height, extra frustum width reveals
    wall left/right;
  - window narrower than 4:3 → field fills the width, extra frustum height reveals
    wall above/below.
  Camera distance and the field's on-screen proportions never change; only the
  frustum widens. Perspective/depth character is identical at every window shape.
- **Wall extends by mirroring.** The 320x200 mural texture gets
  `GL_MIRRORED_REPEAT` on both axes. The wall quad is sized to the frustum extent
  at wall depth; UVs keep the central 320x200 texels exactly behind the field (at
  the current apparent scale) and everything beyond continues as a mirrored copy.
  The existing ball spotlight + vignette darken the periphery; no shader changes.
- **Alt+A no longer affects the 3D scene.** The hotkey stays live and keeps
  toggling `square_pixels` for the flat path (menus, world map, 2D gameplay).
  There is no aspect switching inside 3D mode.

## Projection math

Let `a = 1.2 * win_w / win_h` (frustum aspect in world units; 1.2 = 240/200).

- Half-height at z=0: `hh = max(100, 160 / a)` board px.
- Camera distance stays `scene_camera_distance()` (unchanged); the vertical fov
  becomes `2 * atan(hh / dist)` — equal to `kCameraFovYDeg` whenever the window is
  4:3 or wider.
- Perspective: `mat4_perspective(fovy, a, ...)`, view = `translate(0, 0, -dist)`.

Check: 4:3 window → a = 1.6, hh = 100 → frustum sees exactly 320x200 at z=0.
16:9 → a = 2.133, ~53 px of wall each side. 3:4 portrait → a = 0.9, hh = 178,
~78 px of wall above and below.

## Changes by file

- `src/video3d/scene3d.h` — drop `kParallaxGain`/`kParallaxEase`; stale comments.
- `src/platform_gl3/scene_renderer.{h,cpp}` — drop `SceneCamera` and the `cam`
  parameter; new aspect/fov math; wall quad sized to frustum at wall depth with
  centre-anchored UVs; fix the wrong Alt+A comment.
- `src/platform_gl3/gl_util.*` (or set_scene) — mirrored-repeat wrap for the wall
  texture only.
- `src/platform_sdl3/sdl_app.cpp` — delete cam state/easing; 3D viewport = full
  window (drop `compute_letterbox_viewport` and `square_pixels` from the 3D path).
- Docs: 3D mode section (README / PROJECT_STATUS) — no aspect toggle in 3D,
  widescreen behaviour.

## Out of scope

fixes.md items 2 (shadow offset too far right) and 3 (lighting rework) — separate
follow-ups.

## Testing

- Unit: projection helper (hh/fov selection) if extracted; existing viewport tests
  untouched (2D still uses them).
- By eye: Alt+3 in a level at 16:10, 16:9, 4:3, and a portrait-ish window — field
  centred, proportions identical to 2D Alt+A 4:3, mirrored wall filling the rest;
  Alt+A inside 3D changes nothing until returning to flat screens.
