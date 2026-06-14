#include "platform_sdl3/sdl_app.h"

#include <stdexcept>

namespace {

void require(bool ok) {
    if (!ok) {
        throw std::runtime_error(SDL_GetError());
    }
}

}  // namespace

namespace bumpy {

SdlApp::SdlApp() {
    require(SDL_Init(SDL_INIT_VIDEO));
    window_ = SDL_CreateWindow("Bumpy accurate port", 960, 600, 0);
    if (!window_) {
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
    texture_ = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 320, 200);
    if (!texture_) {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        SDL_Quit();
        throw std::runtime_error(SDL_GetError());
    }
    require(SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST));
}

SdlApp::~SdlApp() {
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

int SdlApp::run(IndexedFramebuffer& frame) {
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }

        const auto rgba = frame.to_rgba();
        require(SDL_UpdateTexture(
            texture_, nullptr, rgba.data(), frame.width() * sizeof(std::uint32_t)));
        require(SDL_RenderClear(renderer_));
        require(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
        require(SDL_RenderPresent(renderer_));
    }
    return 0;
}

}  // namespace bumpy
