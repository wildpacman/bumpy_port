#pragma once

#include <cstdint>

namespace bumpy {

struct MenuInput {
    bool up{};
    bool down{};
    bool left{};
    bool right{};
    bool confirm{};
    bool cancel{};
};

enum class MenuAction {
    none,
    start_first_level,
    high_scores,  // row 1 (FUN_1000_5681)
    password,     // row 3 (FUN_1000_0f7a)
};

struct MenuView {
    bool draw_title{true};
    bool draw_cursor_marker{};
    int cursor_row{};
    // The LEVEL difficulty selection (DAT_203b_79b5) 0/1/2 = EASY/MEDIUM/HARD, drives
    // the EASY/MEDIUM/HARD label the renderer blits over the LEVEL row.
    int level_value{};
};

class Menu {
public:
    [[nodiscard]] const MenuView& view() const noexcept;
    [[nodiscard]] std::uint8_t cycle_value() const noexcept;

    MenuAction update(const MenuInput& input) noexcept;

    // Reset the LEVEL difficulty selection to EASY (DAT_203b_79b5 = 0). The original
    // clears 79b5 on every fresh menu entry (LAB_1000_0c2c); the App calls this when a
    // run ends and control returns to the menu. Cursor position is left untouched.
    void reset_selection() noexcept;

private:
    MenuView view_{true, true, 0};
    std::uint8_t cycle_value_{};
    bool waiting_for_release_{};
};

}  // namespace bumpy
