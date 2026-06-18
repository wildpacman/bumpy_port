# Stage 1 — `.VEC` format and title screen

**Goal:** recover the `.VEC` pixel/palette format from the binary, implement a
C++ decoder, and render `TITRE.VEC` correctly through the indexed framebuffer.

This is a lean plan: recover real behavior, implement, verify by eye. No
reproducibility ceremony, no pixel-exact gate.

## Context already recovered

See `analysis/RESOURCE_PIPELINE.md`. We know the resource table format, the
open→read→close path, and that `.VEC` files load **raw** into memory; pixel
decode and palette setup happen at draw time in `FUN_1000_7b5a` (blit) and
`FUN_1000_7b93` / `FUN_1000_08d1` (palette). The title-screen path is
`FUN_1000_2ef8`.

## Steps

1. **Recover the `.VEC` pixel format.** Read `FUN_1000_7b5a` and the routines it
   dispatches (`FUN_1000_7235` callees `0xa3ae` / `0x9dfe`). Determine how the
   raw `.VEC` bytes become indexed pixels: header fields, run/command encoding,
   row layout, transparency/mask handling. Cross-check against the raw bytes of
   several `.VEC` files until the interpretation consumes each file fully.

2. **Recover the palette.** Read `FUN_1000_7b93` / `FUN_1000_08d1`. Determine the
   palette source (embedded in the resource vs. a separate table) and the
   6-bit→8-bit VGA DAC transformation. Identify whether the title uses a 16- or
   256-color path.

3. **Document the format.** Write `analysis/specs/vec-format.md`: byte order,
   header, command/record layout, termination, palette, and worked hex examples
   from supplied files, with the loader/draw addresses as evidence.

4. **Implement the C++ decoder.** Add `src/resources/vec.{h,cpp}` and a
   bounds-checked `src/resources/binary_reader.{h,cpp}`. The decoder reads an
   original `.VEC` file and produces indexed pixels + palette. Keep it
   SDL-independent. Failures report the file name and the failing offset.

5. **Test on the real files.** Catch2 tests decode every supplied `.VEC` to full
   byte consumption with stable decoded output. Add a malformed/truncated
   fixture that must fail with file+offset diagnostics.

6. **Render the title screen.** Compose `TITRE.VEC` into `IndexedFramebuffer`
   with the recovered palette; present through `SdlApp`, and add a headless dump
   to a 24-bit BMP for inspection.

7. **Verify by eye.** Compare the rendered title to the original title screen
   (visual comparison only). Done when it looks right and the decoder consumes
   every `.VEC` file.

## Out of scope for Stage 1

Menu composition/input (Stage 2) and level formats/gameplay (Stage 3). The
decoder must read the whole `.VEC` family, but only the title screen is rendered
here.
