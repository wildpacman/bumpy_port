#include "game/app.h"

#include "game/level_game.h"  // LevelStatus
#include "game/world_graphs.h"  // kWorldCount

#include <algorithm>

namespace bumpy {
namespace {
// How long the timed GAME OVER screen (FUN_1000_11eb, ~100 un-paced commits) is shown
// before auto-advancing to the high-score table. Tuned by eye (~0.5 s at 70 Hz).
constexpr int kGameOverFrames = 35;
}  // namespace

App::App(std::size_t board_count, int start_world) noexcept
    : world_map_(start_world),
      board_count_(board_count),
      world_(start_world),
      start_world_(start_world),
      selected_world_(start_world),
      cleared_(board_count, 0) {}

void App::reset_run() noexcept {
    lives_ = 5;
    score_ = 0;
    menu_.reset_selection();  // DAT_79b5 = 0: the menu shows EASY again on return

    // The run starts at selected_world_ (the start world, or a world set by a valid password).
    const int target = selected_world_;
    if (world_ == target) {
        // That world's resources are already loaded: clear progress in place and reset the
        // map to node 1 (arming the release guard).
        std::fill(cleared_.begin(), cleared_.end(), std::uint8_t{0});
        world_map_.enter();
    } else {
        // A different world is loaded: ask the shell to (re)load the target world.
        // enter_world resizes cleared_ + resets the map.
        request_world(target);
    }
    // Consume the selection: unless another password is entered, the next fresh PLAY returns
    // to the default start world (the original persists DAT_79b2, but a one-shot start-here is
    // cleaner and avoids replaying the game-over world).
    selected_world_ = start_world_;
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
    // Startup splash (FUN_1000_2ef8 -> FUN_1000_2fac): BUMPRESE.VEC is shown once before
    // the menu. FUN_1000_30dd's normal path waits for fire (input bit 0x10). Feed the
    // opening confirm to Menu and ignore the returned action so a held key cannot bounce
    // splash -> menu -> map on the next tick; Menu will require release before PLAY works.
    if (screen_ == Screen::splash) {
        if (input.confirm) {
            (void)menu_.update(input);
            screen_ = Screen::menu;
        }
        return AppOutcome::running;
    }

    // The outro (FUN_1000_3ed4 -> FUN_1000_328f) is a static ending screen that blocks on a
    // keypress. Mirror 328f, which clears the input latch (DAT_8244 = 0) then waits for any
    // key: require the keys held on entry to be released first, then the next fresh press
    // resets the run (DAT_79b2 = 1) and returns to the menu (DAT_928d = 1). Handled before
    // the cancel-edge tracking below so a held Escape that won the last board cannot bounce
    // straight through the menu into a quit.
    if (screen_ == Screen::outro) {
        const bool any_key = input.up || input.down || input.left || input.right ||
                             input.confirm || input.cancel;
        if (!any_key) {
            waiting_for_release_ = false;  // latch cleared; a fresh press now dismisses
        } else if (!waiting_for_release_) {
            reset_run();                  // DAT_79b2 = 1 (reload the start world if advanced)
            screen_ = Screen::menu;       // DAT_928d = 1 -> main loop returns to the menu
            waiting_for_release_ = true;  // guard the menu's cancel until the key releases
        }
        return AppOutcome::running;
    }

    // The between-world password display (FUN_1000_0d9d): after 3e8a increments DAT_79b2,
    // the original shows "YOUR PASSWORD" plus the new world's code on a black page and waits
    // for fire (bit 0x10). The next world's resources are loaded only after this screen exits.
    if (screen_ == Screen::password_display) {
        if (input.confirm) {
            request_world(password_display_world_);
            screen_ = Screen::map;
            waiting_for_release_ = true;  // a held fire must not enter node 1 after load
        }
        return AppOutcome::running;
    }

    // The timed GAME OVER screen (FUN_1000_11eb): show it for kGameOverFrames, then hand off.
    // Input is ignored while it flashes. Two callers, and they differ (see FUN_1000_0c18):
    //   - out of lives (FUN_1000_22fc set 928d=0xff): the loop runs 11eb THEN FUN_1000_5681
    //     (the high-score table) -> menu.
    //   - Escape on the world map: caught right after FUN_1000_3852 as 11eb then `goto
    //     LAB_0c2c` -- straight to the menu, WITHOUT the high-score table.
    if (screen_ == Screen::game_over) {
        if (++game_over_frames_ >= kGameOverFrames) {
            if (game_over_to_menu_) {
                // World-map Escape path: reset the run and return straight to the menu
                // (LAB_0c2c resets lives=5/score=0), no high-score table.
                game_over_to_menu_ = false;
                reset_run();
                screen_ = Screen::menu;
                waiting_for_release_ = true;  // a held Escape must not bounce into a menu quit
            } else {
                high_score_screen_.enter_entry(high_scores_, score_);  // FUN_1000_5681 -> 57e1
                screen_ = Screen::high_scores;
            }
        }
        return AppOutcome::running;
    }

    // The PASSWORD entry screen (FUN_1000_0f7a). On commit it validates the 6-char code and,
    // once the result flash finishes, returns to the menu. A valid code (world 2..9) sets the
    // next PLAY's start world; an invalid one resets it to world 1 (the original sets 79b2=1).
    // Cancel is ignored here (5c87 leaves only on fire), so this is handled before cancel-edge.
    if (screen_ == Screen::password) {
        if (password_screen_.update(input) == PasswordResult::done) {
            const int world = password_screen_.matched_world();
            selected_world_ = world >= 2 ? world : 1;
            screen_ = Screen::menu;
            waiting_for_release_ = true;  // guard the menu's cancel until keys release
        }
        return AppOutcome::running;
    }

    // The high-score table (FUN_1000_5681). done -> menu; a game-over path (entry mode) also
    // resets the run first (the original returns to a fresh menu after 5681).
    if (screen_ == Screen::high_scores) {
        if (high_score_screen_.update(input) == HighScoreResult::done) {
            const bool from_game_over = high_score_screen_.view().mode == HighScoreMode::entry;
            if (from_game_over) {
                reset_run();
            }
            screen_ = Screen::menu;
            waiting_for_release_ = true;  // guard the menu's cancel until the dismiss key releases
        }
        return AppOutcome::running;
    }

    // App owns the cancel key on every screen so a cancel that causes a transition
    // (e.g. map -> menu) cannot bounce into the next screen's cancel. confirm is owned
    // per-screen: Menu debounces it on the menu, WorldMap on the map.
    const bool cancel_edge = input.cancel && !waiting_for_release_;
    waiting_for_release_ = input.cancel;

    if (screen_ == Screen::menu) {
        switch (menu_.update(input)) {
        case MenuAction::start_first_level:
            // Latch the LEVEL difficulty (DAT_854f = table[79b5]) before reset_run clears
            // the menu selection; it drives the in-level speed for the whole run.
            difficulty_ = menu_.cycle_value();
            reset_run();  // resets lives/score/progress; reloads the start world if needed
            screen_ = Screen::map;
            return AppOutcome::running;
        case MenuAction::high_scores:
            high_score_screen_.enter_view();  // FUN_1000_5681 from the menu (score 0 -> view)
            screen_ = Screen::high_scores;
            return AppOutcome::running;
        case MenuAction::password:
            password_screen_.enter();  // FUN_1000_0f7a: enter a world code
            screen_ = Screen::password;
            return AppOutcome::running;
        case MenuAction::none:
            break;
        }
        if (cancel_edge) {
            return AppOutcome::quit;  // Escape from the menu exits the game (port addition)
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
            // FUN_1000_3852: Escape on the world map sets DAT_928d = 0xff; back in
            // FUN_1000_0c18 that runs FUN_1000_11eb (the timed GAME OVER flash) and then
            // `goto LAB_0c2c` -- straight to the menu, NOT the high-score table. So map
            // Escape is a game over: level progress is lost and the run resets.
            screen_ = Screen::game_over;
            game_over_frames_ = 0;
            game_over_to_menu_ = true;  // this path skips FUN_1000_5681 (see the game_over branch)
            return AppOutcome::running;
        case WorldMapResult::none:
            break;
        }
        return AppOutcome::running;
    }

    // Screen::level: the shell ticks the in-level LevelGame, which owns Escape (scancode
    // 0x01 -> FUN_1000_22fc, lose a life -> back to the map, or GAME OVER on the last life;
    // see LevelGame::f_1d26). The App does NOT treat in-level cancel as a jump to the menu
    // -- doing so discarded the whole run. cancel_edge is intentionally unused here.
    (void)cancel_edge;
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
                // Advance DAT_79b2 and show FUN_1000_0d9d before loading the next world.
                password_display_world_ = world_ + 1;
                screen_ = Screen::password_display;
            } else {
                // World 9 cleared: the game is complete. Show the DESSFIN.VEC ending screen
                // (FUN_1000_3ed4); the run is reset and the menu restored only once the
                // player dismisses it with a keypress (see the outro branch in update()).
                screen_ = Screen::outro;
                waiting_for_release_ = true;  // ignore the key that won the board until released
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
        // Out of lives (FUN_1000_22fc set DAT_928d = 0xff) -> the GAME OVER screen
        // (FUN_1000_11eb) then the high-score table (FUN_1000_5681). The run is reset only
        // once the player leaves the high-score screen (see the high_scores branch in update()).
        screen_ = Screen::game_over;
        game_over_frames_ = 0;
        break;
    case LevelStatus::playing:
        break;  // not a terminal status; the shell only calls this when != playing
    }
}

}  // namespace bumpy
