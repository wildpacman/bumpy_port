#pragma once

#include <SDL3/SDL.h>

#include "core/indexed_framebuffer.h"
#include "game/app.h"
#include "resources/font.h"
#include "resources/world_resources.h"
#include "video/menu_renderer.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace bumpy {

class SdlApp {
public:
    SdlApp();
    ~SdlApp();
    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;

    // Drive the top-level App. Owns the current world's resources (`world`, by value) and
    // reloads them from `asset_root` whenever App requests a new world (pending_world).
    // sprite_bank is the whole BUMSPJEU.BIN; font is DDFNT2.CAR; outro_screen is the decoded
    // DESSFIN.VEC ending image; score_screen is the raw SCORE.VEC high-score/GAME-OVER backdrop
    // (all world-independent, must outlive run()).
    int run(App& app, const MenuRenderer& menu_renderer, const std::filesystem::path& asset_root,
            WorldResources world, std::span<const std::uint8_t> sprite_bank, const Font& font,
            std::span<const std::uint8_t> outro_screen, std::span<const std::uint8_t> score_screen,
            IndexedFramebuffer& frame);

private:
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
};

}  // namespace bumpy
