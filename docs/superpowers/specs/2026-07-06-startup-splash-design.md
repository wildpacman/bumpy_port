# Startup Splash Design

Evidence: the original main loop `FUN_1000_0c18` calls `FUN_1000_2ef8` once before
entering the menu restart label. `FUN_1000_2ef8` calls `FUN_1000_2fac`; `2fac`
selects the common resource table and loads resource index 2, `BUMPRESE.VEC`,
then draws it as a full 320x200 screen image. After the draw it calls
`FUN_1000_30dd`, whose normal music path waits until the input mask contains
fire (`0x10`). The menu loop `FUN_1000_35a5` then loads `TITRE.VEC` and draws the
cursor menu.

The port currently starts `App` at `Screen::menu`, so the startup-only
`BUMPRESE.VEC` splash path is missing.

Design:
- Add `Screen::splash` as the initial `App` screen.
- Dismiss splash on fresh confirm/fire input and transition to `Screen::menu`.
- Require release after dismissal so a held confirm cannot continue into
  `menu -> map`.
- Do not show splash again when gameplay returns to the menu.
- Render splash in the SDL shell by drawing decoded `BUMPRESE.VEC` with the
  existing `screen_image` helpers.

Testing:
- Add an `App` regression test for initial splash and confirm dismissal.
- Add a held-confirm regression test proving splash dismissal does not bounce
  through the menu into the map.
- Add a resource/render test proving `MenuResources` loads a screen-format
  `BUMPRESE.VEC`.
