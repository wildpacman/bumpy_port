#pragma once

#include "core/indexed_framebuffer.h"
#include "game/menu.h"
#include "resources/menu_resources.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace bumpy {

struct MenuImage {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
};

struct MenuDrawCommand {
    MenuImage image;
    int source_x{};
    int source_y{};
    int width{};
    int height{};
    int destination_x{};
    int destination_y{};
    std::optional<std::uint8_t> transparent_index{};
    std::vector<std::uint8_t> mask;
};

std::uint8_t vga_dac_to_rgba_component(std::uint8_t value) noexcept;
void draw_menu_command(const MenuDrawCommand& command, IndexedFramebuffer& target);

class MenuRenderer {
public:
    explicit MenuRenderer(const MenuResources& resources);

    void render(const MenuView& view, IndexedFramebuffer& target) const;

private:
    const MenuResources& resources_;
};

}  // namespace bumpy
