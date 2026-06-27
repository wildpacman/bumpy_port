#include "game/app.h"

#include "game/level_game.h"  // LevelStatus
#include "game/world_graphs.h"  // kWorldCount

#include <algorithm>

namespace bumpy {

App::App(std::size_t board_count, int start_world) noexcept
    : world_map_(start_world),
      board_count_(board_count),
      world_(start_world),
      start_world_(start_world),
      cleared_(board_count, 0) {}

void App::reset_run() noexcept {
    lives_ = 5;
    score_ = 0;
    if (world_ == start_world_) {
        // The start world's resources are already loaded: clear progress in place and
        // reset the map to node 1 (arming the release guard).
        std::fill(cleared_.begin(), cleared_.end(), std::uint8_t{0});
        world_map_.enter();
    } else {
        // A later world is loaded (advanced past the start, then game over): ask the
        // shell to reload the start world. enter_world resizes cleared_ + resets the map.
        request_world(start_world_);
    }
}

void App::request_world(int world) noexcept {
    pending_world_ = world;
}

void App::enter_world(int world, std::size_t board_count) noexcept {
    world_ = world;
    pending_world_ = 0;
    board_count_ = board_count;
    cleared_.assign(board_count, 0);
    world_map_.load_world(world);  // snap to node 1 + arm the release guard
}

bool App::all_boards_cleared() const noexcept {
    return !cleared_.empty() && std::all_of(cleared_.begin(), cleared_.end(),
                                            [](std::uint8_t cleared) { return cleared != 0; });
}

AppOutcome App::update(const MenuInput& input) noexcept {
    // App owns the cancel key on every screen so a cancel that causes a transition
    // (e.g. map -> menu) cannot bounce into the next screen's cancel. confirm is owned
    // per-screen: Menu debounces it on the menu, WorldMap on the map.
    const bool cancel_edge = input.cancel && !waiting_for_release_;
    waiting_for_release_ = input.cancel;

    if (screen_ == Screen::menu) {
        switch (menu_.update(input)) {
        case MenuAction::start_first_level:
            reset_run();  // resets lives/score/progress; reloads the start world if needed
            screen_ = Screen::map;
            return AppOutcome::running;
        case MenuAction::quit:
            return AppOutcome::quit;
        case MenuAction::none:
            break;
        }
        if (cancel_edge) {
            return AppOutcome::quit;  // Escape from the menu exits the game
        }
        return AppOutcome::running;
    }

    if (screen_ == Screen::map) {
        const auto action = world_map_.update(input);
        switch (action.result) {
        case WorldMapResult::select_board:
            board_index_ = action.board_index;  // map node N -> board N-1
            screen_ = Screen::level;
            return AppOutcome::running;
        case WorldMapResult::back_to_menu:
            screen_ = Screen::menu;
            return AppOutcome::running;
        case WorldMapResult::none:
            break;
        }
        return AppOutcome::running;
    }

    // Screen::level: the shell ticks the in-level LevelGame; cancel returns to the menu.
    if (cancel_edge) {
        screen_ = Screen::menu;
        return AppOutcome::running;
    }
    return AppOutcome::running;
}

void App::leave_level() noexcept {
    if (screen_ == Screen::level) {
        screen_ = Screen::map;        // back to the world map to pick the next node
        waiting_for_release_ = true;  // require key release so the trigger key can't carry over
    }
}

void App::finish_level(LevelStatus status, std::uint8_t lives, std::uint32_t score) noexcept {
    if (screen_ != Screen::level) {
        return;
    }
    score_ = score;  // carry the board's result into the run
    lives_ = lives;

    switch (status) {
    case LevelStatus::won:
        if (board_index_ < cleared_.size()) {
            cleared_[board_index_] = 1;  // FUN_1000_1e3d marks the node (*_9baa = 1)
        }
        if (all_boards_cleared()) {
            // World complete (FUN_1000_3e8a).
            if (world_ < kWorldCount) {
                // Advance to the next world: ask the shell to load it, return to the map.
                request_world(world_ + 1);
                screen_ = Screen::map;
                waiting_for_release_ = true;
            } else {
                // World 9 cleared: game complete. The outro (FUN_1000_3ed4) is deferred;
                // reset the run and return to the menu.
                reset_run();
                screen_ = Screen::menu;
            }
        } else {
            screen_ = Screen::map;        // pick the next node
            waiting_for_release_ = true;  // a held fire must not carry across
        }
        break;
    case LevelStatus::dead:
        // The life was already decremented inside LevelGame; the node is not marked,
        // so the board can be replayed from the map.
        screen_ = Screen::map;
        waiting_for_release_ = true;
        break;
    case LevelStatus::quit:
        // Out of lives (FUN_1000_22fc set DAT_928d = 0xff) -> game over.
        reset_run();
        screen_ = Screen::menu;
        break;
    case LevelStatus::playing:
        break;  // not a terminal status; the shell only calls this when != playing
    }
}

}  // namespace bumpy
