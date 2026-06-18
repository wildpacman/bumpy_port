#include "platform_sdl3/sdl_app.h"

#include <stdexcept>

namespace {

void require(bool ok) {
    if (!ok) {
        throw std::runtime_error(SDL_GetError());
    }
}

void update_key_state(bumpy::MenuInput& input, SDL_Keycode key, bool pressed) {
    switch (key) {
    case SDLK_UP:
        input.up = pressed;
        break;
    case SDLK_DOWN:
        input.down = pressed;
        break;
    case SDLK_LEFT:
        input.left = pressed;
        break;
    case SDLK_RIGHT:
        input.right = pressed;
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        input.confirm = pressed;
        break;
    case SDLK_ESCAPE:
        input.cancel = pressed;
        break;
    default:
        break;
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

int SdlApp::run(Menu& menu, const MenuRenderer& menu_renderer, IndexedFramebuffer& frame) {
    bool running = true;
    MenuInput input{};
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                update_key_state(input, event.key.key, true);
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                update_key_state(input, event.key.key, false);
            }
        }

        const auto action = menu.update(input);
        menu_renderer.render(menu.view(), frame);

        const auto rgba = frame.to_rgba();
        require(SDL_UpdateTexture(
            texture_, nullptr, rgba.data(), frame.width() * sizeof(std::uint32_t)));
        require(SDL_RenderClear(renderer_));
        require(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
        require(SDL_RenderPresent(renderer_));

        if (action == MenuAction::start_first_level || action == MenuAction::quit) {
            running = false;
        }
        SDL_Delay(16);
    }
    return 0;
}

}  // namespace bumpy
