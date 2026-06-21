#pragma once

#include "game/menu.h"

#include <cstddef>

namespace bumpy {

// Which screen the running game is showing.
enum class Screen {
    menu,
    level,
};

// What the platform shell should do after an App update.
enum class AppOutcome {
    running,
    quit,
};

// SDL-independent top-level state machine tying the menu to the in-level board
// view. The Menu keeps its own input debounce for the keys it owns
// (up/down/confirm); the App adds edge detection for the keys it owns directly --
// cancel (both screens) and left/right board paging on the level screen -- so a
// held key cannot bounce across a screen transition (e.g. holding Escape must not
// go level -> menu -> quit in one press).
//
// Transitions:
//   menu  --confirm "start"--> level (board 0)
//   menu  --cancel-----------> quit
//   level --cancel-----------> menu
//   level --left/right-------> page boards (wrap within [0, board_count))
//
// This is only the in-window wiring of the already-recovered static board; the
// level screen is not yet interactive gameplay (no entities/physics).
class App {
public:
    explicit App(std::size_t board_count) noexcept;

    [[nodiscard]] Screen screen() const noexcept { return screen_; }
    [[nodiscard]] const Menu& menu() const noexcept { return menu_; }
    [[nodiscard]] std::size_t board_index() const noexcept { return board_index_; }
    [[nodiscard]] std::size_t board_count() const noexcept { return board_count_; }

    AppOutcome update(const MenuInput& input) noexcept;

private:
    Menu menu_;
    Screen screen_{Screen::menu};
    std::size_t board_count_{};
    std::size_t board_index_{};
    bool waiting_for_release_{};
};

}  // namespace bumpy
