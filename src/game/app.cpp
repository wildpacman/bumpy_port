#include "game/app.h"

#include "game/level_game.h"  // LevelStatus

#include <algorithm>

namespace bumpy {

App::App(std::size_t board_count) noexcept
    : board_count_(board_count), cleared_(board_count, 0) {}

void App::reset_run() noexcept {
    lives_ = 5;
    score_ = 0;
    std::fill(cleared_.begin(), cleared_.end(), std::uint8_t{0});
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
            reset_run();         // starting a game reloads the world (FUN_1000_2d14)
            world_map_.enter();  // reset to node 1; require key release before acting
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
            // World complete (FUN_1000_3e8a). Worlds 2-9 are not loaded yet (milestone
            // C); treat the run as finished -> menu. The next "start" reloads the world.
            screen_ = Screen::menu;
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
