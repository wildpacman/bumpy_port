#pragma once

#include "game/menu.h"
#include "game/world_map.h"

#include <cstddef>

namespace bumpy {

// Which screen the running game is showing.
enum class Screen {
    menu,
    map,
    level,
};

// What the platform shell should do after an App update.
enum class AppOutcome {
    running,
    quit,
};

// SDL-independent top-level state machine tying the menu to the world map and
// level screens. The Menu keeps its own input debounce for the keys it owns
// (up/down/confirm); the App adds edge detection for cancel so a held key cannot
// bounce across a screen transition (e.g. holding Escape must not go
// map -> menu -> quit in one press). WorldMap owns confirm debounce on the map.
//
// Transitions:
//   menu  --confirm "start"--> map (world map, node 1)
//   menu  --cancel-----------> quit
//   map   --fire (confirm)----> level (board = selected node - 1)
//   map   --cancel-----------> menu
//   level --cancel-----------> menu
class App {
public:
    explicit App(std::size_t board_count) noexcept;

    [[nodiscard]] Screen screen() const noexcept { return screen_; }
    [[nodiscard]] const Menu& menu() const noexcept { return menu_; }
    [[nodiscard]] const WorldMap& world_map() const noexcept { return world_map_; }
    [[nodiscard]] std::size_t board_index() const noexcept { return board_index_; }
    [[nodiscard]] std::size_t board_count() const noexcept { return board_count_; }

    AppOutcome update(const MenuInput& input) noexcept;

private:
    Menu menu_;
    WorldMap world_map_;
    Screen screen_{Screen::menu};
    std::size_t board_count_{};
    std::size_t board_index_{};
    bool waiting_for_release_{};
};

}  // namespace bumpy
