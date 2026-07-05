#pragma once

#include "game/high_score_screen.h"
#include "game/high_scores.h"
#include "game/menu.h"
#include "game/world_graphs.h"
#include "game/world_map.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bumpy {

enum class LevelStatus;  // game/level_game.h -- the in-level loop's terminal status

// Which screen the running game is showing.
enum class Screen {
    menu,
    map,
    level,
    outro,        // DESSFIN.VEC ending screen after world 9 (FUN_1000_3ed4); any key -> menu
    game_over,    // FUN_1000_11eb: SCORE.VEC + "GAME OVER", timed, then high_scores
    high_scores,  // FUN_1000_5681/57e1: the high-score table (+ name entry on game over)
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
//   level --win last world 9 board--> outro (DESSFIN.VEC ending); any key --> menu
//
// The App is the persistent run: score, lives, and per-board completion carry across
// boards (the world map is the hub). Starting a game from the menu reloads the world
// (FUN_1000_2d14): score 0, lives 5, no boards cleared.
class App {
public:
    explicit App(std::size_t board_count, int start_world = 1) noexcept;

    [[nodiscard]] Screen screen() const noexcept { return screen_; }
    [[nodiscard]] const Menu& menu() const noexcept { return menu_; }
    [[nodiscard]] const WorldMap& world_map() const noexcept { return world_map_; }
    [[nodiscard]] std::size_t board_index() const noexcept { return board_index_; }
    [[nodiscard]] std::size_t board_count() const noexcept { return board_count_; }
    [[nodiscard]] int world() const noexcept { return world_; }
    // The world the shell must load (0 = none). When non-zero, the shell loads that
    // world's resources and calls enter_world; until then App freezes the world state.
    [[nodiscard]] int pending_world() const noexcept { return pending_world_; }

    // Persistent run state, carried into each LevelGame and read back on finish.
    [[nodiscard]] std::uint8_t lives() const noexcept { return lives_; }
    [[nodiscard]] std::uint32_t score() const noexcept { return score_; }
    // Whether board B (= world-map node B+1) has been cleared this run; drives the
    // completed-node markers on the map.
    [[nodiscard]] bool is_board_cleared(std::size_t board) const noexcept {
        return board < cleared_.size() && cleared_[board] != 0;
    }
    // Per-board cleared flags (0/1), indexed by board = node - 1, for the map renderer.
    [[nodiscard]] std::span<const std::uint8_t> cleared_boards() const noexcept {
        return cleared_;
    }

    // The session high-score table and the transient high-score screen state, for the
    // renderer on Screen::high_scores.
    [[nodiscard]] const HighScoreTable& high_scores() const noexcept { return high_scores_; }
    [[nodiscard]] const HighScoreScreen& high_score_screen() const noexcept {
        return high_score_screen_;
    }

    AppOutcome update(const MenuInput& input) noexcept;

    // Leave the playfield back to the world map without scoring (the shell uses this
    // when a selected board has no entity data). No-op unless on the level screen.
    void leave_level() noexcept;

    // Finish the in-level board with the LevelGame's terminal status, carrying the
    // resulting lives/score back into the run. won -> mark the board cleared (world
    // complete returns to the menu); dead -> back to the map (board replayable); quit
    // (out of lives, FUN_1000_22fc set 928d=0xff) -> game over, reset run, menu.
    // No-op unless on the level screen.
    void finish_level(LevelStatus status, std::uint8_t lives, std::uint32_t score) noexcept;

    // The shell calls this after loading world `world`'s resources: rebind the map to
    // that world's node graph (snap to node 1), resize/clear progress to board_count,
    // and clear the pending request. Does not change the current screen.
    void enter_world(int world, std::size_t board_count) noexcept;

private:
    void reset_run() noexcept;             // new-game / world-load reset (FUN_1000_2d14)
    void request_world(int world) noexcept;  // ask the shell to load `world` (sets pending)
    [[nodiscard]] bool all_boards_cleared() const noexcept;  // FUN_1000_3e8a

    Menu menu_;
    WorldMap world_map_;
    Screen screen_{Screen::menu};
    std::size_t board_count_{};
    std::size_t board_index_{};
    int world_{1};          // current world (1..kWorldCount)
    int start_world_{1};    // where a fresh run begins (dev override via the constructor)
    int pending_world_{0};  // world the shell must load (0 = none)
    bool waiting_for_release_{};

    std::uint8_t lives_{5};            // DAT_791a
    std::uint32_t score_{0};           // DAT_a0d4 / a0d6
    std::vector<std::uint8_t> cleared_;  // per board index (0/1); node N -> board N-1

    HighScoreTable high_scores_;         // session table (baked defaults, no persistence)
    HighScoreScreen high_score_screen_;  // transient view/entry screen state
    int game_over_frames_{0};            // frames the GAME OVER screen has been shown
};

}  // namespace bumpy
