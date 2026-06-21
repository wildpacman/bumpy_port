#include "game/app.h"

namespace bumpy {

App::App(std::size_t board_count) noexcept : board_count_(board_count) {}

AppOutcome App::update(const MenuInput& input) noexcept {
    // Edge detection for the keys the App owns directly. The Menu debounces
    // up/down/confirm itself, so those are excluded here; left/right only count on
    // the level screen (the menu ignores them).
    const bool app_key =
        input.cancel || (screen_ == Screen::level && (input.left || input.right));
    const bool app_edge = app_key && !waiting_for_release_;
    waiting_for_release_ = app_key;

    if (screen_ == Screen::menu) {
        switch (menu_.update(input)) {
        case MenuAction::start_first_level:
            screen_ = Screen::level;
            board_index_ = 0;
            return AppOutcome::running;
        case MenuAction::quit:
            return AppOutcome::quit;
        case MenuAction::none:
            break;
        }
        if (app_edge && input.cancel) {
            return AppOutcome::quit;  // Escape from the menu exits the game
        }
        return AppOutcome::running;
    }

    // Screen::level
    if (app_edge && input.cancel) {
        screen_ = Screen::menu;
        return AppOutcome::running;
    }
    if (app_edge && board_count_ > 0) {
        if (input.right) {
            board_index_ = (board_index_ + 1) % board_count_;
        } else if (input.left) {
            board_index_ = (board_index_ + board_count_ - 1) % board_count_;
        }
    }
    return AppOutcome::running;
}

}  // namespace bumpy
