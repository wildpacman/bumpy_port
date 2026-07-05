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
    quit,
};

struct MenuView {
    bool draw_title{true};
    bool draw_cursor_marker{};
    int cursor_row{};
};

class Menu {
public:
    [[nodiscard]] const MenuView& view() const noexcept;
    [[nodiscard]] std::uint8_t cycle_value() const noexcept;

    MenuAction update(const MenuInput& input) noexcept;

private:
    MenuView view_{true, true, 0};
    std::uint8_t cycle_value_{};
    bool waiting_for_release_{};
};

}  // namespace bumpy
