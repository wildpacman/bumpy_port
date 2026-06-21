#include "video/board_renderer.h"

#include "resources/entity_sprites.h"
#include "resources/sprite_frame.h"
#include "video/menu_renderer.h"  // vga_dac_to_rgba_component

#include <stdexcept>

namespace {

// Screen-format VEC geometry (TITRE/MONDE?): 99-byte header carrying the
// 16-colour VGA palette ending at the pixel data, then four 8000-byte
// plane-sequential bit-planes. Matches src/video/menu_renderer.cpp.
constexpr int screen_width = 320;
constexpr int screen_height = 200;
constexpr std::size_t screen_plane = static_cast<std::size_t>(screen_width) * screen_height / 8;  // 8000
constexpr std::size_t pixel_data_offset = 99;
constexpr int palette_colors = 16;
constexpr std::size_t palette_offset = pixel_data_offset - palette_colors * 3;  // 51

void apply_palette(std::span<const std::uint8_t> screen, bumpy::IndexedFramebuffer& target) {
    const std::uint8_t* palette = screen.data() + palette_offset;
    for (int color = 0; color < palette_colors; ++color) {
        const std::uint8_t* entry = palette + color * 3;
        target.set_palette(static_cast<std::uint8_t>(color),
                           bumpy::Rgba{bumpy::vga_dac_to_rgba_component(entry[0]),
                                       bumpy::vga_dac_to_rgba_component(entry[1]),
                                       bumpy::vga_dac_to_rgba_component(entry[2]), 0xff});
    }
}

void deplane_backdrop(std::span<const std::uint8_t> screen, bumpy::IndexedFramebuffer& target) {
    const std::uint8_t* planes = screen.data() + pixel_data_offset;
    for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(screen_width) * screen_height; ++pixel) {
        const std::size_t byte = pixel >> 3U;
        const unsigned shift = 7U - static_cast<unsigned>(pixel & 7U);
        std::uint8_t value = 0;
        for (int plane = 0; plane < 4; ++plane) {
            value = static_cast<std::uint8_t>(
                value | (((planes[plane * screen_plane + byte] >> shift) & 1U) << plane));
        }
        target.pixel(static_cast<int>(pixel % screen_width), static_cast<int>(pixel / screen_width)) = value;
    }
}

}  // namespace

namespace bumpy {

BoardRenderStats render_board(const LevelResources& level, std::size_t board_index,
                              std::span<const std::uint8_t> backdrop_screen,
                              IndexedFramebuffer& target, bool draw_map) {
    if (backdrop_screen.size() < pixel_data_offset + 4 * screen_plane) {
        throw std::runtime_error("backdrop is not a 320x200 screen-format VEC");
    }
    apply_palette(backdrop_screen, target);
    if (draw_map) {
        deplane_backdrop(backdrop_screen, target);  // debug: overlay the world-select map
    } else {
        target.clear(0);  // faithful base-tile pass: every cell cleared to colour index 0
    }

    BoardRenderStats stats;
    if (!level.has_object_sheet()) {
        return stats;  // e.g. level 3 ships no PAV; the flat field is the whole board.
    }

    const auto& board = level.board(board_index);
    const auto& sheet = level.object_sheet();
    constexpr int tile = LevelResources::tile_size;            // 16
    constexpr int sheet_cols = LevelResources::sheet_columns;  // 20

    // Stamp one PAV sheet tile (colour 0 transparent) into the cell at (col,row).
    // Returns false when the tile's source rect falls outside the 320x192 sheet
    // (only stacked markers can index that far), matching the original's layout.
    const auto stamp_tile = [&](int tile_index, int col, int row) -> bool {
        const int sx = (tile_index % sheet_cols) * tile;
        const int sy = (tile_index / sheet_cols) * tile;
        if (sx + tile > sheet.width || sy + tile > sheet.height || tile_index < 0) {
            return false;
        }
        const int dx = col * tile;
        const int dy = row * tile;
        for (int py = 0; py < tile; ++py) {
            const int ty = dy + py;
            if (ty < 0 || ty >= target.height()) {
                continue;
            }
            for (int px = 0; px < tile; ++px) {
                const int tx = dx + px;
                if (tx < 0 || tx >= target.width()) {
                    continue;
                }
                const auto color =
                    sheet.pixels[static_cast<std::size_t>((sy + py) * sheet.width + (sx + px))];
                if (color != 0) {  // colour index 0 is transparent
                    target.pixel(tx, ty) = color;
                }
            }
        }
        return true;
    };

    for (int col = 0; col < LevelBoard::columns; ++col) {
        for (int row = 0; row < LevelBoard::rows; ++row) {
            const std::uint8_t obj = board.object_index(col, row);
            if (obj == 0) {
                continue;
            }
            if (obj < 0xf1) {
                stamp_tile(obj - 1, col, row);
                ++stats.objects_drawn;
                continue;
            }
            // Stacked marker (FUN_1000_0a90): draw (uint8_t)(-obj-5) tiles whose
            // indices are the cell bytes that follow byte 0, all at this cell.
            ++stats.stacked_cells;
            const auto count = static_cast<std::uint8_t>(-static_cast<int>(obj) - 5);
            const std::size_t base =
                LevelBoard::grid_offset + col * LevelBoard::column_stride + row * LevelBoard::cell_bytes;
            for (std::uint8_t k = 1; k < count; ++k) {
                const std::size_t index = base + k;
                if (index >= board.bytes.size()) {
                    break;
                }
                if (stamp_tile(board.bytes[index] - 1, col, row)) {
                    ++stats.stacked_tiles;
                }
            }
        }
    }
    return stats;
}

namespace {

void plot(IndexedFramebuffer& target, int x, int y, std::uint8_t color) {
    if (x >= 0 && x < target.width() && y >= 0 && y < target.height()) {
        target.pixel(x, y) = color;
    }
}

void fill_rect(IndexedFramebuffer& target, int x, int y, int w, int h, std::uint8_t color) {
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            plot(target, x + dx, y + dy, color);
        }
    }
}

void box_outline(IndexedFramebuffer& target, int x, int y, int w, int h, std::uint8_t color) {
    for (int dx = 0; dx < w; ++dx) {
        plot(target, x + dx, y, color);
        plot(target, x + dx, y + h - 1, color);
    }
    for (int dy = 0; dy < h; ++dy) {
        plot(target, x, y + dy, color);
        plot(target, x + w - 1, y + dy, color);
    }
}

}  // namespace

EntityOverlayStats overlay_bum_entities(const BumEntities& bum, IndexedFramebuffer& target,
                                        std::uint8_t color_a, std::uint8_t color_b,
                                        std::uint8_t color_c) {
    EntityOverlayStats stats;
    for (int row = 0; row < BumEntities::rows; ++row) {
        for (int col = 0; col < BumEntities::columns; ++col) {
            const auto pos = bum_cell_position(col, row);
            if (bum.layer_a(col, row) != 0) {
                fill_rect(target, pos.x + 6, pos.y + 6, 4, 4, color_a);  // peg dot, cell-centred
                ++stats.layer_a;
            }
            if (bum.layer_b(col, row) != 0 && col != 7) {  // col 7 is never drawn (FUN_1000_2a78)
                fill_rect(target, pos.x + 4, pos.y + 4, 8, 8, color_b);
                ++stats.layer_b;
            }
            if (bum.layer_c(col, row) != 0) {
                box_outline(target, pos.x, pos.y, 16, 16, color_c);  // collectible footprint
                ++stats.layer_c;
            }
        }
    }
    return stats;
}

namespace {

// Decode bank frame `index` and blit it at (x,y); sprite colour index 0 is
// transparent (decode_sprite_frame maps it to sprite_transparent_index). Returns
// false if the frame did not decode (out-of-range/compressed/invalid).
bool blit_bank_frame(std::span<const std::uint8_t> bank, int index, int x, int y,
                     IndexedFramebuffer& target) {
    MenuImage frame;
    try {
        frame = decode_sprite_frame(bank, index);
    } catch (const std::exception&) {
        return false;
    }
    for (int py = 0; py < frame.height; ++py) {
        for (int px = 0; px < frame.width; ++px) {
            const auto color = frame.pixels[static_cast<std::size_t>(py) * frame.width + px];
            if (color != sprite_transparent_index) {
                plot(target, x + px, y + py, color);
            }
        }
    }
    return true;
}

}  // namespace

EntitySpriteStats draw_bum_entities(const BumEntities& bum,
                                    std::span<const std::uint8_t> sprite_bank,
                                    IndexedFramebuffer& target) {
    EntitySpriteStats stats;
    // Faithful to FUN_1000_2a78: row-major, per cell draw layer A, then B, then C.
    for (int row = 0; row < BumEntities::rows; ++row) {
        for (int col = 0; col < BumEntities::columns; ++col) {
            if (const auto a = entity_layer_a_sprite(bum.layer_a(col, row)); a.present()) {
                const auto pos = entity_layer_ab_position(col, row);
                if (blit_bank_frame(sprite_bank, a.frame_index, pos.x, pos.y + a.y_offset, target)) {
                    ++stats.layer_a;
                } else {
                    ++stats.skipped;
                }
            }
            if (const std::uint8_t bv = bum.layer_b(col, row); bv != 0 && col != 7) {
                if (const auto b = entity_layer_b_sprite(bv); b.present()) {
                    const auto pos = entity_layer_ab_position(col, row);
                    if (blit_bank_frame(sprite_bank, b.frame_index, pos.x, pos.y + b.y_offset,
                                        target)) {
                        ++stats.layer_b;
                    } else {
                        ++stats.skipped;
                    }
                }
            }
            if (const std::uint8_t cv = bum.layer_c(col, row); cv != 0) {
                const auto pos = bum_cell_position(col, row);
                if (blit_bank_frame(sprite_bank, entity_layer_c_frame(cv), pos.x, pos.y, target)) {
                    ++stats.layer_c;
                } else {
                    ++stats.skipped;
                }
            }
        }
    }
    return stats;
}

}  // namespace bumpy
