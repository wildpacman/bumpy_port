#pragma once

#include <SDL3/SDL.h>

#include "core/indexed_framebuffer.h"
#include "game/menu.h"
#include "video/menu_renderer.h"

namespace bumpy {

class SdlApp {
public:
    SdlApp();
    ~SdlApp();
    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;
    int run(Menu& menu, const MenuRenderer& menu_renderer, IndexedFramebuffer& frame);

private:
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
};

}  // namespace bumpy
