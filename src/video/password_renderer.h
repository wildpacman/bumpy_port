#pragma once

#include "core/indexed_framebuffer.h"
#include "game/password_screen.h"  // PasswordScreenView

#include <cstdint>
#include <span>
#include <array>

namespace bumpy {

// The PASSWORD entry screen (FUN_1000_0f7a): text on a BLACK page (like GAME OVER,
// FUN_1000_11eb) -- SCORE.VEC (menu resource 3) is loaded only for its palette, never blitted,
// so the "HALL OF FAME" art does NOT show. Draws the "ENTER YOUR PASSWORD" prompt, the 6-cell
// entry field with a blinking caret, and the " PASSWORD OK " / "PASSWORD ERROR" result flash.
// `score_vec` is the raw 320x200 SCORE.VEC (palette source only).
// See analysis/specs/menu-behavior.md ("Password screen").
void render_password(std::span<const std::uint8_t> score_vec,
                     std::span<const std::uint8_t> sprite_bank,
                     const PasswordScreenView& view, IndexedFramebuffer& target);

// The between-world password display (FUN_1000_0d9d): black page, "YOUR PASSWORD" at y=80,
// and the supplied 6-character password at y=112. SCORE.VEC supplies only the palette.
void render_password_display(std::span<const std::uint8_t> score_vec,
                             std::span<const std::uint8_t> sprite_bank,
                             const std::array<char, 6>& code, IndexedFramebuffer& target);

}  // namespace bumpy
