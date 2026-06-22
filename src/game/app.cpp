#include "game/app.h"

namespace bumpy {

App::App(std::size_t board_count) noexcept : board_count_(board_count) {}

AppOutcome App::update(const MenuInput& input) noexcept {
    // App owns the cancel key on every screen so a cancel that causes a transition
    // (e.g. map -> menu) cannot bounce into the next screen's cancel. confirm is owned
    // per-screen: Menu debounces it on the menu, WorldMap on the map.
    const bool cancel_edge = input.cancel && !waiting_for_release_;
    waiting_for_release_ = input.cancel;

    if (screen_ == Screen::menu) {
        switch (menu_.update(input)) {
        case MenuAction::start_first_level:
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

}  // namespace bumpy
