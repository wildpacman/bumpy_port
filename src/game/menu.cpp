#include "game/menu.h"

namespace {

bool confirmed_menu_input_pressed(const bumpy::MenuInput& input) noexcept {
    return input.up || input.down || input.confirm;
}

}  // namespace

namespace bumpy {

const MenuView& Menu::view() const noexcept {
    return view_;
}

std::uint8_t Menu::cycle_value() const noexcept {
    return cycle_value_;
}

MenuAction Menu::update(const MenuInput& input) noexcept {
    const auto has_input = confirmed_menu_input_pressed(input);
    if (!has_input) {
        waiting_for_release_ = false;
        return MenuAction::none;
    }

    if (waiting_for_release_) {
        return MenuAction::none;
    }
    waiting_for_release_ = true;

    if (input.up) {
        if (view_.cursor_row > 0) {
            --view_.cursor_row;
        }
        return MenuAction::none;
    }

    if (input.down) {
        if (view_.cursor_row < 3) {
            ++view_.cursor_row;
        }
        return MenuAction::none;
    }

    if (input.confirm) {
        if (view_.cursor_row == 0) {
            return MenuAction::start_first_level;
        }
        if (view_.cursor_row == 1) {
            return MenuAction::high_scores;  // FUN_1000_5681
        }
        if (view_.cursor_row == 2) {
            cycle_value_ = static_cast<std::uint8_t>((cycle_value_ + 1U) % 3U);
            return MenuAction::none;
        }
        if (view_.cursor_row == 3) {
            return MenuAction::quit;
        }
    }

    return MenuAction::none;
}

}  // namespace bumpy
