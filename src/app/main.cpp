#include <SDL3/SDL_main.h>

#include "core/asset_manifest.h"
#include "core/indexed_framebuffer.h"
#include "game/app.h"
#include "game/level_game.h"
#include "game/menu.h"
#include "platform_sdl3/sdl_app.h"
#include "resources/font.h"
#include "resources/level_resources.h"
#include "resources/menu_resources.h"
#include "resources/vec.h"
#include "resources/world_resources.h"
#include "game/world_graphs.h"
#include "game/world_map.h"
#include "video/board_renderer.h"
#include "video/hud.h"
#include "video/map_renderer.h"
#include "video/menu_renderer.h"
#include "video/screen_image.h"
#include "video/screen_transition.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool has_asset_manifest(const std::filesystem::path& root) {
    return std::filesystem::is_regular_file(root / "config/original-assets.sha256");
}

void add_root_candidates(std::vector<std::filesystem::path>& candidates, std::filesystem::path path) {
    if (path.empty()) {
        return;
    }
    std::error_code error;
    path = std::filesystem::weakly_canonical(path, error);
    if (error) {
        path = std::filesystem::absolute(path, error);
    }
    if (error) {
        return;
    }
    if (std::filesystem::is_regular_file(path, error)) {
        path = path.parent_path();
    }
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        if (std::find(candidates.begin(), candidates.end(), current) == candidates.end()) {
            candidates.push_back(current);
        }
        if (current == current.root_path()) {
            break;
        }
    }
}

std::filesystem::path find_asset_root(std::string_view executable_path) {
    std::vector<std::filesystem::path> candidates;
    add_root_candidates(candidates, std::filesystem::current_path());
    add_root_candidates(candidates, std::filesystem::path(executable_path));
    for (const auto& candidate : candidates) {
        if (has_asset_manifest(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error(
        "cannot find original Bumpy assets; run from the project root or keep the executable below it");
}

// A faithful port reads the original files, so it is worth warning when they are
// missing or altered -- but it must not refuse to launch over a hash mismatch.
void warn_if_assets_changed(const std::filesystem::path& asset_root) {
    try {
        const auto verification =
            bumpy::AssetManifest::load(asset_root / "config/original-assets.sha256").verify(asset_root);
        if (!verification.missing.empty() || !verification.changed.empty()) {
            std::cerr << "warning: some original Bumpy assets are missing or changed\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "warning: could not verify original assets: " << error.what() << '\n';
    }
}

void write_u16(std::ostream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xffU));
    output.put(static_cast<char>((value >> 8U) & 0xffU));
}

void write_u32(std::ostream& output, std::uint32_t value) {
    write_u16(output, static_cast<std::uint16_t>(value & 0xffffU));
    write_u16(output, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

// Minimal 24-bit BMP writer for headless inspection of a rendered frame.
void write_24bit_bmp(const std::filesystem::path& path, const bumpy::IndexedFramebuffer& frame) {
    const auto width = static_cast<std::uint32_t>(frame.width());
    const auto height = static_cast<std::uint32_t>(frame.height());
    const auto row_stride = ((width * 3U) + 3U) & ~3U;
    const auto pixel_bytes = row_stride * height;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create BMP: " + path.string());
    }
    output.write("BM", 2);
    write_u32(output, 14U + 40U + pixel_bytes);
    write_u16(output, 0);
    write_u16(output, 0);
    write_u32(output, 54);
    write_u32(output, 40);
    write_u32(output, width);
    write_u32(output, height);
    write_u16(output, 1);
    write_u16(output, 24);
    write_u32(output, 0);
    write_u32(output, pixel_bytes);
    write_u32(output, 0);
    write_u32(output, 0);
    write_u32(output, 0);
    write_u32(output, 0);

    std::vector<std::uint8_t> row(row_stride);
    const auto pixels = frame.pixels();
    const auto& palette = frame.palette();
    for (int y = frame.height() - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < frame.width(); ++x) {
            const auto color = palette[pixels[static_cast<std::size_t>(y * frame.width() + x)]];
            const auto offset = static_cast<std::size_t>(x * 3);
            row[offset] = color.b;
            row[offset + 1] = color.g;
            row[offset + 2] = color.r;
        }
        output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

bumpy::IndexedFramebuffer render_menu_frame(const std::filesystem::path& asset_root) {
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const bumpy::MenuRenderer renderer(resources);
    bumpy::Menu menu;
    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(menu.view(), frame);
    return frame;
}

int render_title_to_bmp(const std::filesystem::path& asset_root, const std::filesystem::path& out_path) {
    write_24bit_bmp(out_path, render_menu_frame(asset_root));
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Dump the raw decoded TITRE.VEC bytes for offline format analysis.
int dump_title_raw(const std::filesystem::path& asset_root, const std::filesystem::path& out_path) {
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const auto bytes = resources.title.decoded_bytes();
    std::ofstream output(out_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create dump: " + out_path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::cout << "wrote " << bytes.size() << " bytes to " << out_path.string() << '\n';
    return 0;
}

// Decode any layered-VEC file (TITRE.VEC, MONDE?.VEC, and the level blobs
// D?.PAV/D?.DEC/D?.BUM, which share the same container) and dump the decoded
// bytes for offline format analysis. Prints the layer chain that produced them.
int decode_vec_file(const std::filesystem::path& in_path, const std::filesystem::path& out_path) {
    const auto resource = bumpy::decode_vec_resource(in_path);
    const auto bytes = resource.decoded_bytes();
    std::cout << in_path.string() << ": " << resource.layers().size() << " layer(s)\n";
    for (std::size_t i = 0; i < resource.layers().size(); ++i) {
        const auto& layer = resource.layers()[i];
        std::cout << "  layer " << i << ": method " << static_cast<int>(layer.method)
                  << (layer.final_layer ? " (final)" : "") << ", input " << layer.input_size
                  << " -> output " << layer.output_size << '\n';
    }
    std::ofstream output(out_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create dump: " + out_path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::cout << "wrote " << bytes.size() << " bytes to " << out_path.string() << '\n';
    return 0;
}

// Render a decoded 320x200 screen-format VEC (TITRE/MONDE?/BUMPRESE/DESSFIN: a
// 99-byte header with a 16-colour palette at offset 51, then four 8000-byte
// plane-sequential bit-planes) to a BMP, for visual format confirmation.
int render_screen_vec(const std::filesystem::path& in_path, const std::filesystem::path& out_path) {
    const auto resource = bumpy::decode_vec_resource(in_path);
    const auto decoded = resource.decoded_bytes();
    constexpr std::size_t header = 99;
    constexpr std::size_t plane = 320 * 200 / 8;
    if (decoded.size() < header + 4 * plane) {
        throw std::runtime_error("decoded VEC is not a 320x200 screen: " + in_path.string());
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto* pal = decoded.data() + (header - 48);
    for (int c = 0; c < 16; ++c) {
        const auto* e = pal + c * 3;
        frame.set_palette(static_cast<std::uint8_t>(c),
                          bumpy::Rgba{bumpy::vga_dac_to_rgba_component(e[0]),
                                      bumpy::vga_dac_to_rgba_component(e[1]),
                                      bumpy::vga_dac_to_rgba_component(e[2]), 0xff});
    }
    const auto* planes = decoded.data() + header;
    for (std::size_t p = 0; p < static_cast<std::size_t>(320) * 200; ++p) {
        const std::size_t byte = p >> 3U;
        const unsigned shift = 7U - static_cast<unsigned>(p & 7U);
        std::uint8_t v = 0;
        for (int pl = 0; pl < 4; ++pl) {
            v = static_cast<std::uint8_t>(v | (((planes[pl * plane + byte] >> shift) & 1U) << pl));
        }
        frame.pixel(static_cast<int>(p % 320), static_cast<int>(p / 320)) = v;
    }
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Probe the pixel geometry of a decoded D?.PAV (or any raw blob) by rendering it
// under a selectable layout and dimensions. layout: planeseq | rowint | lin4 |
// lin8. Palette is "DEBUG" (16 high-contrast colours) or a screen-format VEC.
int render_pav(const std::filesystem::path& pav_path, const std::string& pal_path,
               const std::filesystem::path& out_path, const std::string& layout, int w, int h,
               int hdr) {
    const auto pav_resource = bumpy::decode_vec_resource(pav_path);
    const auto pav = pav_resource.decoded_bytes();
    std::vector<std::uint8_t> pal_screen;
    if (pal_path != "DEBUG") {
        const auto pal_resource = bumpy::decode_vec_resource(pal_path);
        const auto decoded = pal_resource.decoded_bytes();
        pal_screen.assign(decoded.begin(), decoded.end());
    }
    bumpy::IndexedFramebuffer frame(w, h);
    if (pal_path == "DEBUG") {
        static const bumpy::Rgba dbg[16] = {
            {0, 0, 0, 255},      {255, 0, 0, 255},    {0, 255, 0, 255},   {255, 255, 0, 255},
            {0, 0, 255, 255},    {255, 0, 255, 255},  {0, 255, 255, 255}, {255, 255, 255, 255},
            {128, 128, 128, 255},{128, 0, 0, 255},    {0, 128, 0, 255},   {128, 128, 0, 255},
            {0, 0, 128, 255},    {128, 0, 128, 255},  {0, 128, 128, 255}, {200, 200, 200, 255}};
        for (int c = 0; c < 16; ++c) {
            frame.set_palette(static_cast<std::uint8_t>(c), dbg[c]);
        }
    } else {
        const auto* pal = pal_screen.data() + (99 - 48);
        for (int c = 0; c < 16; ++c) {
            const auto* e = pal + c * 3;
            frame.set_palette(static_cast<std::uint8_t>(c),
                              bumpy::Rgba{bumpy::vga_dac_to_rgba_component(e[0]),
                                          bumpy::vga_dac_to_rgba_component(e[1]),
                                          bumpy::vga_dac_to_rgba_component(e[2]), 0xff});
        }
    }
    const auto* d = pav.data() + hdr;
    const std::size_t avail = pav.size() - hdr;
    auto get = [&](std::size_t i) -> std::uint8_t { return i < avail ? d[i] : 0; };
    const std::size_t plane_bytes = static_cast<std::size_t>(w) / 8;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t v = 0;
            if (layout == "planeseq") {
                const std::size_t plane = static_cast<std::size_t>(w) * h / 8;
                const std::size_t bit = static_cast<std::size_t>(y) * w + x;
                const unsigned sh = 7U - static_cast<unsigned>(bit & 7U);
                for (int pl = 0; pl < 4; ++pl)
                    v |= ((get(pl * plane + (bit >> 3)) >> sh) & 1U) << pl;
            } else if (layout == "rowint") {
                const std::size_t row = static_cast<std::size_t>(y) * plane_bytes * 4;
                const unsigned sh = 7U - static_cast<unsigned>(x & 7U);
                for (int pl = 0; pl < 4; ++pl)
                    v |= ((get(row + pl * plane_bytes + (x >> 3)) >> sh) & 1U) << pl;
            } else if (layout == "lin4") {
                const std::size_t idx = (static_cast<std::size_t>(y) * w + x) >> 1;
                const std::uint8_t b = get(idx);
                v = (x & 1) ? (b & 0x0f) : (b >> 4);
            } else {  // lin8
                v = get(static_cast<std::size_t>(y) * w + x) & 0x0f;
            }
            frame.pixel(x, y) = v;
        }
    }
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (" << layout << " " << w << "x" << h << ")\n";
    return 0;
}

// Render the post-world-9 ending screen (DESSFIN.VEC, drawn by FUN_1000_3ed4) to a BMP,
// through the exact shared screen_image helpers the SDL shell uses for Screen::outro, for
// by-eye confirmation of the outro render path.
int render_outro_to_bmp(const std::filesystem::path& in_path, const std::filesystem::path& out_path) {
    const auto resource = bumpy::decode_vec_resource(in_path);
    const auto screen = resource.decoded_bytes();
    if (!bumpy::is_screen_image(screen)) {
        throw std::runtime_error("decoded VEC is not a 320x200 screen: " + in_path.string());
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    bumpy::apply_screen_image_palette(screen, frame);
    bumpy::draw_screen_image(screen, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << '\n';
    return 0;
}

// Compose a static playfield board (MONDE backdrop + D?.PAV objects on the 16x16
// grid) and dump it to a BMP for by-eye comparison with the original.
int render_board_to_bmp(const std::filesystem::path& asset_root, int level_number,
                        const std::filesystem::path& monde_path, std::size_t board_index,
                        const std::filesystem::path& out_path, bool draw_map, bool draw_entities,
                        bool draw_sprites) {
    const auto level = bumpy::LevelResources::load(asset_root, level_number);
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto stats =
        bumpy::render_board(level, board_index, backdrop.decoded_bytes(), frame, draw_map);
    std::cout << "wrote " << out_path.string() << " (level " << level_number << " board "
              << board_index << ": " << stats.objects_drawn << " objects, " << stats.stacked_cells
              << " stacked cells -> " << stats.stacked_tiles << " tiles";
    if (draw_sprites) {
        const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
        const auto ent = bumpy::draw_bum_entities(level.bum_entities(board_index), bank.bytes(), frame);
        std::cout << "; entity sprites: " << ent.layer_a << " A / " << ent.layer_b << " B / "
                  << ent.layer_c << " C (" << ent.skipped << " skipped)";
    } else if (draw_entities) {
        const auto ent = bumpy::overlay_bum_entities(level.bum_entities(board_index), frame);
        std::cout << "; BUM markers: " << ent.layer_a << " A / " << ent.layer_b << " B / "
                  << ent.layer_c << " C";
    }
    write_24bit_bmp(out_path, frame);
    std::cout << ")\n";
    return 0;
}

// Drive the in-level LevelGame on a board for a number of frames with a held
// direction, dumping <prefix>NN.bmp every few frames (board art + live entities +
// the ball) so the playfield gameplay can be verified by eye without the window.
int render_play_to_bmps(const std::filesystem::path& asset_root, int level_number,
                        const std::filesystem::path& monde_path, std::size_t board_index,
                        const std::string& dir, const std::string& prefix) {
    const auto level = bumpy::LevelResources::load(asset_root, level_number);
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto backdrop_bytes = backdrop.decoded_bytes();
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");

    bumpy::LevelGame game(level.bum_entities(board_index));
    bumpy::LevelInput input{};
    input.left = dir == "left" || dir == "leftfire";
    input.right = dir == "right";
    input.up = dir == "up";
    input.down = dir == "down";
    input.fire = dir == "fire" || dir == "leftfire";

    auto dump = [&](int index) {
        bumpy::IndexedFramebuffer frame(320, 200);
        bumpy::render_board(level, board_index, backdrop_bytes, frame);
        bumpy::BumEntities live{};
        std::copy(game.grid().begin(), game.grid().begin() + bumpy::BumEntities::record_size,
                  live.bytes.begin());
        // Tile bump/spring animations, drawn exactly as the SDL app does: blank the
        // static tile under each animating cell, then overlay the spring frames.
        std::array<bumpy::ObjectAnimSprite, 7> anims{};
        const std::size_t anim_count = game.object_anims(anims);
        for (std::size_t k = 0; k < anim_count; ++k) {
            const std::size_t cell = anims[k].cell;
            const std::size_t off = anims[k].layer_b ? bumpy::BumEntities::layer_b_offset
                                                     : bumpy::BumEntities::layer_a_offset;
            live.bytes[cell + off] = 0;
        }
        bumpy::draw_bum_entities(live, bank.bytes(), frame);
        bumpy::draw_object_anims({anims.data(), anim_count}, bank.bytes(), frame);
        if (game.monster_present()) {
            bumpy::draw_monster(bank.bytes(), game.monster_frame(), game.monster_x(),
                                game.monster_y(), frame);
        }
        bumpy::draw_ball(bank.bytes(), game.ball_frame(), game.ball_x(), game.ball_y(), frame);
        std::ostringstream name;
        name << prefix << std::setw(2) << std::setfill('0') << index << ".bmp";
        write_24bit_bmp(name.str(), frame);
        std::cout << "step " << std::setw(2) << index << ": cell 0x" << std::hex
                  << static_cast<int>(game.ball_cell()) << " state 0x"
                  << static_cast<int>(game.player_state()) << " frame 0x" << game.ball_frame()
                  << std::dec << " at (" << game.ball_x() << "," << game.ball_y() << ") gems "
                  << static_cast<int>(game.collectibles_left()) << " score " << game.score()
                  << " springs " << anim_count;
        if (anim_count > 0) {
            std::cout << " [cell 0x" << std::hex << static_cast<int>(anims[0].cell) << " frame 0x"
                      << anims[0].frame_index << std::dec << (anims[0].layer_b ? " B]" : " A]");
        }
        if (game.monster_present()) {
            std::cout << " | mob cell 0x" << std::hex << static_cast<int>(game.monster_cell())
                      << " frame 0x" << game.monster_frame() << std::dec << " at ("
                      << game.monster_x() << "," << game.monster_y() << ")";
        }
        std::cout << "\n";
    };

    constexpr int kFrames = 40;
    dump(0);
    for (int i = 1; i <= kFrames; ++i) {
        game.tick(input);
        dump(i);
    }
    std::cout << "wrote " << (kFrames + 1) << " frames to " << prefix << "NN.bmp\n";
    return 0;
}

// Compose the world-map screen (MONDE backdrop + the Bumpy avatar at node 1) and
// dump it to a BMP for by-eye comparison with the original world-select capture.
int render_map_to_bmp(const std::filesystem::path& asset_root, int world_number,
                      const std::filesystem::path& monde_path,
                      const std::filesystem::path& out_path, int cleared_count) {
    if (world_number < 1 || world_number > bumpy::kWorldCount) {
        std::cerr << "--render-map world must be 1.." << bumpy::kWorldCount << '\n';
        return 2;
    }
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::WorldMap map(world_number);  // node 1 of the requested world
    std::vector<std::uint8_t> cleared(
        static_cast<std::size_t>(bumpy::world_node_count(world_number)), 0);
    for (int i = 0; i < cleared_count && i < static_cast<int>(cleared.size()); ++i) {
        cleared[static_cast<std::size_t>(i)] = 1;
    }
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto stats =
        bumpy::render_map(backdrop.decoded_bytes(), map.view(), bank.bytes(), frame, cleared);
    bumpy::draw_lives(bank.bytes(), 5, frame);
    const auto font = bumpy::Font::load(asset_root / "DDFNT2.CAR");
    bumpy::draw_score(font, 1234567, bumpy::kMapScoreX, bumpy::kMapScoreBaselineY,
                      bumpy::kScoreColor, frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (world " << world_number << ", avatar "
              << (stats.avatar_drawn ? "drawn" : "skipped") << " at node " << map.current_node()
              << ", " << stats.markers_drawn << " node markers)\n";
    return 0;
}

// Drive the world map through the fire-to-enter cloud-jump on the given node and dump
// every animation step to <prefix>NN.bmp for by-eye verification of the recovered
// sequence (frames + bounce + cloud).
int render_jump_to_bmps(const std::filesystem::path& asset_root,
                        const std::filesystem::path& monde_path, int node,
                        const std::string& prefix) {
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    const auto screen = backdrop.decoded_bytes();

    bumpy::WorldMap map;  // node 1
    // Walk to the requested node so the jump plays somewhere clearly visible.
    for (int n = 1; n < node; ++n) {
        map.update(bumpy::MenuInput{.right = true});
        int guard = 0;
        while (map.is_sliding() && guard++ < 1000) {
            map.update(bumpy::MenuInput{});
        }
        map.update(bumpy::MenuInput{});  // release
    }

    auto dump = [&](int index) {
        bumpy::IndexedFramebuffer frame(320, 200);
        bumpy::render_map(screen, map.view(), bank.bytes(), frame);
        std::ostringstream name;
        name << prefix << std::setw(2) << std::setfill('0') << index << ".bmp";
        write_24bit_bmp(name.str(), frame);
        std::cout << "step " << index << ": frame 0x" << std::hex << map.view().avatar_frame
                  << std::dec << " offset_y " << map.view().avatar_offset_y << " cloud "
                  << (map.view().cloud_visible ? "on" : "off") << "\n";
    };

    int index = 0;
    map.update(bumpy::MenuInput{.confirm = true});  // start the jump (poses step 0)
    dump(index++);
    int guard = 0;
    while (map.is_jumping() && guard++ < 1000) {
        map.update(bumpy::MenuInput{});
        dump(index++);
    }
    std::cout << "wrote " << index << " frames to " << prefix << "NN.bmp\n";
    return 0;
}

// Dump the edge-to-centre screen-change darken (FUN_1000_3467) over a real screen --
// the world-map backdrop -- as <prefix>NN.bmp, one per step, so the wipe's geometry and
// pacing can be checked by eye against the original (see analysis/specs/screen-flow.md).
int render_transition_to_bmps(const std::filesystem::path& asset_root,
                              const std::filesystem::path& monde_path, const std::string& prefix) {
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::WorldMap map;  // node 1
    bumpy::IndexedFramebuffer outgoing(320, 200);
    bumpy::render_map(backdrop.decoded_bytes(), map.view(), bank.bytes(), outgoing);

    auto dump = [&](int index, const bumpy::IndexedFramebuffer& f) {
        std::ostringstream name;
        name << prefix << std::setw(2) << std::setfill('0') << index << ".bmp";
        write_24bit_bmp(name.str(), f);
    };

    dump(0, outgoing);  // step 0: the un-darkened outgoing screen
    bumpy::ScreenTransition transition;
    transition.begin(outgoing);
    int index = 1;
    while (transition.active()) {
        bumpy::IndexedFramebuffer frame(320, 200);
        transition.render(frame);
        dump(index, frame);
        std::cout << "step " << std::setw(2) << index << ": border "
                  << bumpy::ScreenTransition::kCellW * transition.step() << "x"
                  << bumpy::ScreenTransition::kCellH * transition.step() << " px\n";
        transition.advance();
        ++index;
    }
    std::cout << "wrote " << index << " frames to " << prefix << "NN.bmp\n";
    return 0;
}

int run_sdl_menu(const std::filesystem::path& asset_root, int start_world) {
    warn_if_assets_changed(asset_root);
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const bumpy::MenuRenderer renderer(resources);

    // Load the starting world (D{n} + MONDE{n}.VEC). The shell owns this bundle and
    // reloads it on each world change; the world-independent sprite bank + font are
    // loaded once and must outlive run().
    auto world = bumpy::WorldResources::load(asset_root, start_world);
    const auto sprite_bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    const auto font = bumpy::Font::load(asset_root / "DDFNT2.CAR");
    // The post-world-9 ending screen (FUN_1000_3ed4). World-independent, so decode it once;
    // decoded_bytes() is a view into this resource, which must outlive run().
    const auto outro = bumpy::decode_vec_resource(asset_root / "DESSFIN.VEC");

    bumpy::App app(world.board_count(), start_world);
    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(app.menu().view(), frame);

    bumpy::SdlApp sdl;
    return sdl.run(app, renderer, asset_root, std::move(world), sprite_bank.bytes(), font,
                   outro.decoded_bytes(), frame);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto asset_root =
            find_asset_root(argc > 0 ? std::string_view(argv[0]) : std::string_view{});
        if ((argc == 2 || argc == 3) && std::string_view(argv[1]) == "--render-title") {
            const auto out_path = argc == 3 ? std::filesystem::path(argv[2])
                                            : asset_root / "analysis/generated/menu_with_marker.bmp";
            return render_title_to_bmp(asset_root, out_path);
        }
        if (argc == 3 && std::string_view(argv[1]) == "--dump-title-raw") {
            return dump_title_raw(asset_root, argv[2]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--decode-vec") {
            return decode_vec_file(argv[2], argv[3]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-screen") {
            return render_screen_vec(argv[2], argv[3]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-outro") {
            // --render-outro <DESSFIN.VEC> <out.bmp>: the ending screen via the shell's path.
            return render_outro_to_bmp(argv[2], argv[3]);
        }
        if ((argc == 5 || argc == 6) && std::string_view(argv[1]) == "--render-map") {
            // --render-map <world> <MONDE.VEC> <out.bmp> [cleared_node_count]
            const int cleared = argc == 6 ? std::stoi(argv[5]) : 0;
            return render_map_to_bmp(asset_root, std::stoi(argv[2]), argv[3], argv[4], cleared);
        }
        if (argc == 7 && std::string_view(argv[1]) == "--render-play") {
            // --render-play <level> <MONDE.VEC> <board> <dir> <out-prefix>
            // dir = none|left|right|up|down|fire (held for every frame).
            return render_play_to_bmps(asset_root, std::stoi(argv[2]), argv[3],
                                       static_cast<std::size_t>(std::stoi(argv[4])), argv[5], argv[6]);
        }
        if (argc == 4 && std::string_view(argv[1]) == "--render-transition") {
            // --render-transition <MONDE.VEC> <out-prefix>
            // Dumps the edge-to-centre screen-change darken over the world map as
            // <prefix>NN.bmp (step 00 = un-darkened, then one per ring).
            return render_transition_to_bmps(asset_root, argv[2], argv[3]);
        }
        if (argc == 5 && std::string_view(argv[1]) == "--render-jump") {
            // --render-jump <node> <MONDE.VEC> <out-prefix>
            // Dumps the fire-to-enter cloud-jump animation on <node> as <prefix>NN.bmp.
            return render_jump_to_bmps(asset_root, argv[3], std::stoi(argv[2]), argv[4]);
        }
        if ((argc == 6 || argc == 7) && std::string_view(argv[1]) == "--render-board") {
            // --render-board <level> <MONDE.VEC> <board_index> <out.bmp> [map|entities]
            // Faithful by default: the base-tile pass clears to colour index 0 and the
            // PAV objects compose the board over it, using the MONDE per-world palette.
            // The optional "map" token overlays the MONDE world-select screen instead
            // of the flat clear (a debug aid; that screen is not the playfield).
            // "entities" overlays the decoded BUM entity grid as inspection markers;
            // "sprites" draws the real BUM entity sprites from BUMSPJEU.BIN.
            const auto token = argc == 7 ? std::string_view(argv[6]) : std::string_view{};
            const bool draw_map = token == "map";
            const bool draw_entities = token == "entities";
            const bool draw_sprites = token == "sprites";
            return render_board_to_bmp(asset_root, std::stoi(argv[2]), argv[3],
                                       static_cast<std::size_t>(std::stoi(argv[4])), argv[5],
                                       draw_map, draw_entities, draw_sprites);
        }
        if (argc == 9 && std::string_view(argv[1]) == "--render-pav") {
            // --render-pav <pav> <pal|DEBUG> <out.bmp> <layout> <w> <h> <hdr>
            return render_pav(argv[2], argv[3], argv[4], argv[5], std::stoi(argv[6]),
                              std::stoi(argv[7]), std::stoi(argv[8]));
        }
        int start_world = 1;
        if (argc == 3 && std::string_view(argv[1]) == "--start-world") {
            start_world = std::stoi(argv[2]);
            if (start_world < 1 || start_world > bumpy::kWorldCount) {
                std::cerr << "--start-world must be 1.." << bumpy::kWorldCount << '\n';
                return 2;
            }
            return run_sdl_menu(asset_root, start_world);
        }
        if (argc != 1) {
            std::cerr << "usage: bumpy_port.exe [--render-title [out.bmp] | --start-world N | ...]\n";
            return 2;
        }
        return run_sdl_menu(asset_root, start_world);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
