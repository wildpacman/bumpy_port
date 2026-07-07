# 3D Render Mode Design

## Motivation

The port composites every scene into a 320x200 indexed framebuffer and presents
it flat. This mode adds an **optional "diorama" 3D presentation** of the in-level
playfield: the same original assets, pixel-for-pixel, arranged in a real 3D scene
with depth, light, and shader effects — in the spirit of `screenshots/3d-proto.png`.
No upscaling, no AI-generated or hand-drawn replacement art: the frame is built
only from elements that were on the DOS screen, plus lighting/blur/shadow
*effects* over them and side faces extruded from the sprites' own edge pixels.

The faithful flat path stays the default and stays byte-identical in its
320x200 composition; gameplay, timing, and positions are untouched — the 3D mode
only draws.

## Constraints (user-set)

- **Camera:** near-frontal diorama with light parallax following the ball. No
  dramatic camera moves.
- **Art liberties:** (1) effects over real art (blur, light, vignette, shadows)
  and (2) procedural geometry extruded from a sprite's own pixels are allowed;
  (3) new scene elements that never existed on the DOS screen (floor plane, dust
  motes) are **not**.
- **Relation to HD mode:** independent. Built from `master` on its own branch;
  `feat/hd-render-mode` stays parked, no xBRZ dependency. The "scene =
  background + sprite list" idea is reused conceptually.
- **Switching:** Alt+3 toggle (instant, no transition animation) + `--render3d`
  CLI flag + a small config file persisting the last chosen mode (plus
  aspect/fullscreen). A port-settings overlay is a **separate future spec**.

## Scene decomposition

The 2D engine's existing data layers map 1:1 onto depth layers — no heuristics:

| Depth layer | Source (existing code path) | 3D treatment |
|---|---|---|
| Back wall | `render_board` output: the DEC board mural, incl. awning and bottom bars | One 320x200 texture, pushed back, slightly overscaled so parallax never exposes its edge; baked gaussian blur (DOF) + darkening at board load |
| Interactive plane (z=0) | BUM planes A/B via the `draw_bum_entities` sprite set; spring anims substitute tiles as today | Rectangular-silhouette sprites (lanes, blocks) become **slabs**: front face = original pixels, side/top/bottom faces = the sprite's own edge pixels stretched to the extrusion depth, shaded per face. Irregular silhouettes (spiky bumpers, deflectors) stay crisp billboards |
| Live layer | Plane-C collectibles, ball, monster (`draw_ball` / `draw_monster` inputs) | Crisp billboards at their exact game positions |

Slab-vs-billboard classification is computed from the decoded sprite alpha at
board load (opaque silhouette == solid rectangle → slab), deterministic and
art-driven; no hand-maintained tables.

## Camera and effects

- Perspective projection, narrow FOV, camera on the board's central axis.
- **Parallax:** camera x/y eases toward a few percent of the ball's offset from
  board centre; wall shifts less than the z=0 plane, so depth reads in motion.
- **Effects (GLSL, phase 1 set):** DOF-blurred wall (baked), soft spotlight that
  follows the ball (wall brightens near the ball), vignette, soft shadows —
  blurred dark copies of platform/ball silhouettes projected onto the wall with
  a small offset.
- All art textures sample `GL_NEAREST`; no filtering of the pixels themselves.

## Rendering architecture

Presentation moves from `SDL_Renderer` to an in-house **OpenGL 3.3 core**
presenter (window gains a GL context; `SdlApp`'s `renderer_`/`texture_` go away).
One presenter, two paths:

- **Flat path (default, all screens):** the 320x200 `IndexedFramebuffer`
  composition is unchanged; the frame is uploaded to a GL texture and drawn as
  one quad with a pixel-art scaling shader replicating `SDL_SCALEMODE_PIXELART`
  ("sharp bilinear": crisp interiors, <=1px edge ramp). Letterbox and Alt+A
  (16:10 / 4:3) become viewport math; Alt+Enter fullscreen unchanged. This is
  the only risk point for the faithful path and is verified by eye and by
  headless dump against the current output.
- **3D path (Alt+3, in-level only):** the scene above, drawn with a depth
  buffer straight to the backbuffer. Off the playfield (menu, map, passwords,
  splash, outro, scores) 3D mode shows the flat path; dressing those screens is
  phase 2.

**Scene building** lives in a new `src/video3d/` module: on board entry a
`Scene3d` is built (wall texture + baked blur, slab meshes, billboard atlas);
per frame only live positions are updated from the same `LevelGame` state that
feeds `render_level()` today. Game logic is not touched.

**Dependencies:** vendor a GL loader (glad, MIT) only. GLSL shaders ship as
files in an assets subdirectory next to the exe for fast look iteration
(hot-reload hotkey in Debug builds); no shader build toolchain.

## Mode switching and persistence

- **Alt+3** toggles original/3D any time (peer of Alt+Enter / Alt+A); hard cut.
- **`--render3d`** starts in 3D mode.
- A small **config file** next to the exe persists the last render mode, aspect,
  and fullscreen state across sessions. This is the port's first on-disk
  persistence (high scores are deliberately session-only, like the original).
  CLI flag overrides config; hotkeys update it.
- Port-settings overlay (discoverable UI for all port options): separate spec,
  after this project. The config file from this phase is its foundation.

## Failure handling

If the GL 3.3 context or a shader fails to compile/link: log to stderr, run the
flat path, disable Alt+3. The game never dies because of the 3D mode.

## Testing

- **Unit (CPU, no GL):** slab/billboard classification on known sprites; slab
  geometry from a sprite with known edge pixels; billboard positions equal the
  faithful 2D path's draw positions.
- **Flat-path parity:** headless GL frame dump (hidden window + FBO readback)
  at an integer scale compared against a CPU nearest-neighbour reference upscale
  of the same 320x200 frame (board / menu / map) — must match 1:1; non-integer
  window fits are checked by eye against the old presenter.
- **3D look:** `--render-3d <level> <board> <out.png>` offline dump (peer of the
  existing `--render-*` tools) for by-eye review.

## Phasing

1. **Phase 1:** GL presenter + flat-path parity (independently mergeable) →
   `Scene3d` (wall + slabs + billboards) → parallax → light/shadow/vignette/DOF
   → Alt+3 + `--render3d` + config persistence + `--render-3d` dump.
2. **Phase 2 (later, user's stated second priority):** dress the non-level
   screens (menu, map, password) with light treatments in the same presenter.
3. **Separate spec (later):** port-settings overlay.

## Non-goals

- No change to gameplay, timing, integer positions, PRNG, or the `--render-*`
  RE dump outputs.
- No upscaling (xBRZ or otherwise), no AI/hand-drawn replacement assets, no
  scene elements absent from the DOS frame.
- No merge or deletion of `feat/hd-render-mode`.
