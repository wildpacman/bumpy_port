#include "platform_sdl3/sdl_app.h"

#include "game/level_game.h"
#include "resources/level_resources.h"
#include "video/board_renderer.h"
#include "video/hud.h"
#include "video/map_renderer.h"
#include "video/screen_transition.h"

#include <algorithm>
#include <array>
#include <optional>
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

// The original paces every game step on the VGA vertical retrace -- a two-phase poll of
// port 0x3DA bit 3, reached via the per-video-mode dispatch at the tail of FUN_1ab9_0351
// (the `7bdd` wait); see analysis/specs/screen-flow.md ("Frame timing"). For VGA's
// 320x200 16-colour mode the vertical refresh is 70.086 Hz.
constexpr double kVgaRefreshHz = 70.086;

// The sequences driven by the {frame,dx,dy} script stepper FUN_1000_13df -- in-level
// gameplay and the world-map cloud-jump -- advance one step per *two* retraces, i.e.
// 35.043 Hz. Confirmed by side-by-side comparison with the original under DosBox: paced
// at the full 70 Hz the in-level ball ran a clean, stable 2x too fast (it bounced twice
// per original bounce), and the cloud-jump likewise matched only at the halved rate.
// World-map navigation (the FUN_1000_3ab2..3bc9 slide) and the menu instead step once
// per retrace -- at 35 Hz the node-to-node slide visibly dragged. The retrace handler
// sits behind a jump table Ghidra could not recover, so the /2 is pinned empirically
// rather than read from the disassembly. The run loop selects per phase (see half_rate).
constexpr double kGameTickHz = kVgaRefreshHz / 2.0;

// How many retraces each ring of the edge-to-centre darken (FUN_1000_3467) is held for.
// The original runs the fill un-paced (one CPU-bound burst); the port spreads the 10
// rings over frames so the wipe is visible. At 1 frame/ring (70 Hz) the close is ~0.14 s;
// holding each ring 2 frames (~35 Hz) gives ~0.29 s. This is the knob for the wipe speed.
constexpr int kDarkenFramesPerRing = 2;

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
                std::span<const std::uint8_t> sprite_bank, const Font& font,
                IndexedFramebuffer& frame) {
    bool running = true;
    MenuInput input{};

    // The in-level game state machine, created when the level screen is entered for a
    // board and destroyed when it is left. nullopt off the playfield.
    std::optional<LevelGame> game;
    auto live_entities = [&]() {
        // Build a BumEntities view of LevelGame's live grid so collected collectibles
        // (cleared in plane C) stop being drawn.
        BumEntities live{};
        const auto& grid = game->grid();
        std::copy(grid.begin(), grid.begin() + BumEntities::record_size, live.bytes.begin());
        return live;
    };

    // Per-phase pacing. The engine has two frame loops with different retrace-wait
    // counts (see kGameTickHz): sequences driven by the {frame,dx,dy} script stepper
    // FUN_1000_13df -- in-level gameplay and the world-map cloud-jump -- step once per
    // *two* retraces (35.043 Hz), while world-map navigation (the FUN_1000_3ab2..3bc9
    // slide) and the menu step once per retrace (70.086 Hz). Confirmed by side-by-side
    // DosBox comparison: at a uniform 35 Hz the node-to-node slide dragged, while the
    // cloud-jump and gameplay matched. We pick the period each frame from the live phase.
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();
    const Uint64 period_full = static_cast<Uint64>(static_cast<double>(perf_freq) / kVgaRefreshHz);
    const Uint64 period_half = static_cast<Uint64>(static_cast<double>(perf_freq) / kGameTickHz);
    Uint64 next_frame = SDL_GetPerformanceCounter();

    // The original darkens the screen from the edges to the centre on every screen change
    // (FUN_1000_3467; see analysis/specs/screen-flow.md). We snapshot the outgoing screen
    // and play the closing-box wipe over it before the incoming screen renders. Each ring
    // is held for kDarkenFramesPerRing retraces to pace the close.
    ScreenTransition transition;
    int darken_hold = 0;  // retraces the current ring has been shown

    auto present_frame = [&]() {
        const auto rgba = frame.to_rgba();
        require(SDL_UpdateTexture(
            texture_, nullptr, rgba.data(), frame.width() * sizeof(std::uint32_t)));
        require(SDL_RenderClear(renderer_));
        require(SDL_RenderTexture(renderer_, texture_, nullptr, nullptr));
        require(SDL_RenderPresent(renderer_));
    };

    // Wait until the next tick boundary: sleep the bulk (1ms granularity) then spin the
    // final sub-millisecond for an accurate cadence. If a frame ran long, resync instead
    // of accumulating debt.
    auto wait_next_tick = [&](Uint64 tick_period) {
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
    };

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
        if (!running) {
            break;
        }

        // While the edge-to-centre darken is playing, freeze all game logic (the original
        // runs FUN_1000_3467 synchronously before the next screen loads) and just step the
        // wipe over the snapshotted outgoing screen. Each ring is shown kDarkenFramesPerRing
        // retraces before advancing inward.
        if (transition.active()) {
            transition.render(frame);
            present_frame();
            wait_next_tick(period_full);
            if (++darken_hold >= kDarkenFramesPerRing) {
                darken_hold = 0;
                transition.advance();  // may deactivate after the final (fully black) ring
            }
            continue;
        }

        // The App owns all screen transitions, including Escape/cancel (menu -> quit,
        // level -> menu), so the event loop no longer special-cases Escape.
        const Screen before = app.screen();
        if (app.update(input) == AppOutcome::quit) {
            running = false;
        }
        if (!running) {
            break;
        }
        bool screen_changed = app.screen() != before;

        // Drive the in-level game state machine on the playfield. Arrow keys move the
        // ball; confirm (Enter/Space) is the fire button. Skipped on the frame a screen
        // change starts so the board is not created/ticked under the darken (the original
        // loads the board only after FUN_1000_3467 finishes).
        if (!screen_changed) {
            const Screen pre_game = app.screen();
            if (app.screen() == Screen::level) {
                if (!game) {
                    if (app.board_index() < level.bum_board_count()) {
                        // Carry the run's lives/score into the board.
                        game.emplace(level.bum_entities(app.board_index()), app.lives(),
                                     app.score());
                    } else {
                        app.leave_level();  // no entity data for this board
                    }
                }
                if (game) {
                    game->tick(
                        LevelInput{input.left, input.right, input.up, input.down, input.confirm});
                    if (game->status() != LevelStatus::playing) {
                        // Win/lose/game-over: carry lives+score back and update the run.
                        app.finish_level(game->status(), game->lives(), game->score());
                        game.reset();
                    }
                }
            } else {
                game.reset();
            }
            // A board that won/lost/quit flips level -> map here, not via app.update().
            screen_changed = app.screen() != pre_game;
        }

        // On any screen change, `frame` still holds the previous iteration's render of the
        // outgoing screen: snapshot it and start the darken instead of rendering anew. The
        // outermost ring shows this frame; the active()-block above paces the rest.
        if (screen_changed) {
            transition.begin(frame);
            transition.render(frame);
            present_frame();
            wait_next_tick(period_full);
            darken_hold = 1;  // ring 1 shown once this frame
            continue;
        }

        if (app.screen() == Screen::menu) {
            menu_renderer.render(app.menu().view(), frame);
        } else if (app.screen() == Screen::map) {
            render_map(backdrop_screen, app.world_map().view(), sprite_bank, frame,
                       app.cleared_boards());
            draw_lives(sprite_bank, app.lives(), frame);  // lives row HUD (FUN_1000_6130)
            draw_score(font, app.score(), kMapScoreX, kMapScoreBaselineY, kScoreColor, frame);
        } else {
            render_board(level, app.board_index(), backdrop_screen, frame);
            if (game) {
                BumEntities live = live_entities();
                // Tile bump/spring animations: pull the live slots, blank the static
                // tile under each so only the moving spring sprite draws (matching the
                // original's background restore), then overlay the spring frames.
                std::array<ObjectAnimSprite, 7> anims{};
                const std::size_t anim_count = game->object_anims(anims);
                for (std::size_t k = 0; k < anim_count; ++k) {
                    const std::size_t cell = anims[k].cell;
                    const std::size_t off = anims[k].layer_b ? BumEntities::layer_b_offset
                                                             : BumEntities::layer_a_offset;
                    live.bytes[cell + off] = 0;
                }
                draw_bum_entities(live, sprite_bank, frame);
                draw_object_anims({anims.data(), anim_count}, sprite_bank, frame);
                if (game->monster_present()) {
                    draw_monster(sprite_bank, game->monster_frame(), game->monster_x(),
                                 game->monster_y(), frame);
                }
                draw_ball(sprite_bank, game->ball_frame(), game->ball_x(), game->ball_y(), frame);
            } else {
                draw_bum_entities(level.bum_entities(app.board_index()), sprite_bank, frame);
            }
        }

        present_frame();

        // Pick this frame's period from the live phase: half-rate for the 13df-driven
        // sequences (in-level gameplay, world-map cloud-jump), full retrace rate
        // otherwise (menu, world-map navigation/slide). See period_full/period_half.
        const bool half_rate = app.screen() == Screen::level ||
                               (app.screen() == Screen::map && app.world_map().is_jumping());
        wait_next_tick(half_rate ? period_half : period_full);
    }
    return 0;
}

}  // namespace bumpy
