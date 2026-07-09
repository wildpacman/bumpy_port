#pragma once

#include "core/indexed_framebuffer.h"
#include "game/settings_overlay.h"  // SettingsView, SettingsPage

#include <cstdint>
#include <span>

namespace bumpy {

// Draws the Tab settings overlay as an opaque full-screen page on the SCORE.VEC
// palette, in the same big sprite-glyph style as GAME OVER / PASSWORD / HIGH SCORES.
// Constructed once (the asset spans must outlive it) and reused per frame.
class SettingsRenderer {
public:
    // score_vec    : raw SCORE.VEC screen bytes (palette source)
    // sprite_bank  : BUMSPJEU sprite bank (glyph frames)
    // cursor_sprite: FLECHE.BIN bytes (arrow marker, frame 0)
    SettingsRenderer(std::span<const std::uint8_t> score_vec,
                     std::span<const std::uint8_t> sprite_bank,
                     std::span<const std::uint8_t> cursor_sprite);

    void render(const SettingsView& view, IndexedFramebuffer& target) const;

private:
    std::span<const std::uint8_t> score_vec_;
    std::span<const std::uint8_t> sprite_bank_;
    std::span<const std::uint8_t> cursor_sprite_;
};

}  // namespace bumpy
