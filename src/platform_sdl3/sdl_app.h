#pragma once

#include <SDL3/SDL.h>

#include "core/indexed_framebuffer.h"
#include "game/app.h"
#include "resources/level_resources.h"
#include "video/menu_renderer.h"

#include <cstdint>
#include <span>

namespace bumpy {

class SdlApp {
public:
    SdlApp();
    ~SdlApp();
    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;

    // Drive the top-level App: render the menu when on the menu screen and the
    // level board (over the per-world backdrop palette, with the real BUM entity
    // sprites from the sprite_bank) when on the level screen, presenting each frame
    // until the App requests quit. sprite_bank is the whole BUMSPJEU.BIN.
    int run(App& app, const MenuRenderer& menu_renderer, const LevelResources& level,
            std::span<const std::uint8_t> backdrop_screen,
            std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& frame);

private:
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
};

}  // namespace bumpy
