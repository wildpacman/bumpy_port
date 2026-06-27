#include "video/board_renderer.h"

#include "resources/entity_sprites.h"
#include "resources/sprite_frame.h"
#include "video/menu_renderer.h"  // vga_dac_to_rgba_component
#include "video/screen_image.h"

#include <stdexcept>

namespace bumpy {

namespace {

// Set framebuffer palette entries 0..15 from a board's own 16-colour DAC palette
// (16 RGB triplets of 6-bit values), the gameplay palette the original builds from
// the DEC board header (FUN_1000_063b / 08d1). Mirrors apply_screen_image_palette.
void apply_board_palette(const LevelBoard& board, IndexedFramebuffer& target) {
    const auto dac = board.palette();
    for (int color = 0; color < LevelBoard::palette_colors; ++color) {
        const std::uint8_t* entry = dac.data() + static_cast<std::size_t>(color) * 3;
        target.set_palette(static_cast<std::uint8_t>(color),
                           Rgba{vga_dac_to_rgba_component(entry[0]),
                                vga_dac_to_rgba_component(entry[1]),
                                vga_dac_to_rgba_component(entry[2]), 0xff});
    }
}

}  // namespace

BoardRenderStats render_board(const LevelResources& level, std::size_t board_index,
                              std::span<const std::uint8_t> backdrop_screen,
                              IndexedFramebuffer& target, bool draw_map) {
    if (draw_map) {
        // Debug overlay only: paint the world-select MONDE screen with its own
        // palette so the board layout can be eyeballed against the map art.
        if (!is_screen_image(backdrop_screen)) {
            throw std::runtime_error("backdrop is not a 320x200 screen-format VEC");
        }
        apply_screen_image_palette(backdrop_screen, target);
        draw_screen_image(backdrop_screen, target);
    } else {
        // Faithful playfield: the board's own DEC-header palette, then the base-tile
        // pass as a flat colour-index-0 clear (the recovered base-tile behaviour).
        apply_board_palette(level.board(board_index), target);
        target.clear(0);
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
                    const auto pos = entity_layer_b_position(col, row);
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

bool draw_ball(std::span<const std::uint8_t> sprite_bank, int frame, int ball_x, int ball_y,
               IndexedFramebuffer& target) {
    if (frame == 100) {  // the blitter skips the hidden sentinel (FUN_1000_1cb2)
        return false;
    }
    MenuImage sprite;
    try {
        sprite = decode_sprite_frame(sprite_bank, frame);
    } catch (const std::exception&) {
        return false;
    }
    // Anchor X by half-width: the ball content is box-centred in EVERY frame (the wide
    // 32px jump/bonk frames 0x0d..0x11, 0x21, 0x2d..0x37 carry it at content-centre ~= 16
    // with side sparks), so half-width keeps the visual centre on ball_x across frame
    // changes; header origin_x there (5..10) flung the ball ~9px right on a jump/bonk frame.
    // Anchor Y by min(origin_y, height/2). The content sits in the upper part of the box,
    // never the lower, so the anchor is never below the box centre. For the 16x19 exit
    // descent (origin_y 7 < h/2 9) this keeps origin_y, clipping the sinking ball into the
    // pit at the right line; for the wide bonce/jump frames (origin_y 15 > h/2) it falls
    // back to half-height, which centres the content -- using the raw origin_y=15 there
    // flung the bounce-apex ball ~9px UP (it "punched through" the platform) before
    // snapping back. Rolling frames are unaffected (origin_y 7 == h/2 7).
    const int anchor_y = sprite.origin_y < sprite.height / 2 ? sprite.origin_y : sprite.height / 2;
    const int top_x = ball_x - sprite.width / 2;
    const int top_y = ball_y - anchor_y;
    for (int py = 0; py < sprite.height; ++py) {
        for (int px = 0; px < sprite.width; ++px) {
            const auto color = sprite.pixels[static_cast<std::size_t>(py) * sprite.width + px];
            if (color != sprite_transparent_index) {
                plot(target, top_x + px, top_y + py, color);
            }
        }
    }
    return true;
}

bool draw_monster(std::span<const std::uint8_t> sprite_bank, int frame, int mon_x, int mon_y,
                  IndexedFramebuffer& target) {
    MenuImage sprite;
    try {
        sprite = decode_sprite_frame(sprite_bank, frame);
    } catch (const std::exception&) {
        return false;
    }
    // Like the ball: centre X by half-width, anchor Y by min(origin_y, height/2). The
    // monster frames are 16x16 origin (8,7), so X is unchanged (w/2 = 8) and Y uses the
    // hotspot (7 < h/2 8).
    const int anchor_y = sprite.origin_y < sprite.height / 2 ? sprite.origin_y : sprite.height / 2;
    const int top_x = mon_x - sprite.width / 2;
    const int top_y = mon_y - anchor_y;
    for (int py = 0; py < sprite.height; ++py) {
        for (int px = 0; px < sprite.width; ++px) {
            const auto color = sprite.pixels[static_cast<std::size_t>(py) * sprite.width + px];
            if (color != sprite_transparent_index) {
                plot(target, top_x + px, top_y + py, color);
            }
        }
    }
    return true;
}

int draw_object_anims(std::span<const ObjectAnimSprite> anims,
                      std::span<const std::uint8_t> sprite_bank, IndexedFramebuffer& target) {
    int drawn = 0;
    for (const auto& a : anims) {
        if (a.frame_index == kAnimHiddenFrame) {
            continue;  // a blink-off step draws nothing (the original skips the raster)
        }
        const int col = a.cell % 8;
        const int row = a.cell / 8;
        // Layer-B springs sit at the layer-B position table (DS:0x3f4); layer A at 0xf4.
        const auto pos = a.layer_b ? entity_layer_b_position(col, row)
                                   : entity_layer_ab_position(col, row);
        if (blit_bank_frame(sprite_bank, a.frame_index, pos.x, pos.y + a.y_offset, target)) {
            ++drawn;
        }
    }
    return drawn;
}

}  // namespace bumpy
