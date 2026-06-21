#include "platform_sdl3/sdl_app.h"

#include "video/board_renderer.h"
#include "video/map_renderer.h"

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

// The original advances its game logic exactly once per displayed frame, pacing each
// frame on the VGA vertical retrace -- a two-phase poll of port 0x3DA bit 3, reached via
// the per-video-mode dispatch at the tail of FUN_1ab9_0351 (the `7bdd` wait); see
// analysis/specs/screen-flow.md ("Frame timing"). For VGA's 320x200 16-colour mode the
// vertical refresh is 70.086 Hz, so the world-map slide, the cloud-jump, and (later)
// gameplay all step at that rate. We reproduce it with a fixed tick, decoupled from the
// host monitor's refresh, rather than the old ~60 Hz SDL_Delay(16).
constexpr double kVgaRefreshHz = 70.086;

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

int SdlApp::run(App& app, const MenuRenderer& menu_renderer, const LevelResources& level,
                std::span<const std::uint8_t> backdrop_screen,
                std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& frame) {
    bool running = true;
    MenuInput input{};

    // Fixed-rate pacing at the VGA vertical refresh (see kVgaRefreshHz). One game tick
    // per loop iteration, then sleep/spin to the next frame boundary so the logic runs
    // at the original's rate regardless of how fast the host can render.
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();
    const Uint64 tick_period = static_cast<Uint64>(static_cast<double>(perf_freq) / kVgaRefreshHz);
    Uint64 next_frame = SDL_GetPerformanceCounter();

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                update_key_state(input, event.key.key, true);
            } else if (event.type == SDL_EVENT_KEY_UP) {
                update_key_state(input, event.key.key, false);
            }
        }

        // The App owns all screen transitions, including Escape/cancel (menu ->
        // quit, level -> menu), so the event loop no longer special-cases Escape.
        if (app.update(input) == AppOutcome::quit) {
            running = false;
        }

        if (app.screen() == Screen::menu) {
            menu_renderer.render(app.menu().view(), frame);
        } else if (app.screen() == Screen::map) {
            render_map(backdrop_screen, app.world_map().view(), sprite_bank, frame);
        } else {
            render_board(level, app.board_index(), backdrop_screen, frame);
            draw_bum_entities(level.bum_entities(app.board_index()), sprite_bank, frame);
        }

        const auto rgba = frame.to_rgba();
        require(SDL_UpdateTexture(
            texture_, nullptr, rgba.data(), frame.width() * sizeof(std::uint32_t)));
        require(SDL_RenderClear(renderer_));
        require(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
        require(SDL_RenderPresent(renderer_));

        // Wait until the next 70.086 Hz frame boundary: sleep the bulk (1ms granularity)
        // then spin the final sub-millisecond for an accurate cadence. If a frame ran
        // long, resync instead of accumulating debt.
        next_frame += tick_period;
        const Uint64 now = SDL_GetPerformanceCounter();
        if (now < next_frame) {
            const Uint64 remaining = next_frame - now;
            const Uint64 remaining_ms = (remaining * 1000) / perf_freq;
            if (remaining_ms > 1) {
                SDL_Delay(static_cast<Uint32>(remaining_ms - 1));
            }
            while (SDL_GetPerformanceCounter() < next_frame) {
                // spin the last <=1ms
            }
        } else {
            next_frame = now;  // behind schedule -> resync
        }
    }
    return 0;
}

}  // namespace bumpy
