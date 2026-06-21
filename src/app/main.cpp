#include <SDL3/SDL_main.h>

#include "core/asset_manifest.h"
#include "core/indexed_framebuffer.h"
#include "game/app.h"
#include "game/menu.h"
#include "platform_sdl3/sdl_app.h"
#include "resources/level_resources.h"
#include "resources/menu_resources.h"
#include "resources/vec.h"
#include "game/world_map.h"
#include "video/board_renderer.h"
#include "video/map_renderer.h"
#include "video/menu_renderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
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

// Compose the world-map screen (MONDE backdrop + the Bumpy avatar at node 1) and
// dump it to a BMP for by-eye comparison with the original world-select capture.
int render_map_to_bmp(const std::filesystem::path& asset_root, const std::filesystem::path& monde_path,
                      const std::filesystem::path& out_path) {
    const auto backdrop = bumpy::decode_vec_resource(monde_path);
    const auto bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");
    bumpy::WorldMap map;  // node 1
    bumpy::IndexedFramebuffer frame(320, 200);
    const auto stats = bumpy::render_map(backdrop.decoded_bytes(), map.view(), bank.bytes(), frame);
    write_24bit_bmp(out_path, frame);
    std::cout << "wrote " << out_path.string() << " (avatar "
              << (stats.avatar_drawn ? "drawn" : "skipped") << " at node "
              << map.current_node() << ")\n";
    return 0;
}

int run_sdl_menu(const std::filesystem::path& asset_root) {
    warn_if_assets_changed(asset_root);
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const bumpy::MenuRenderer renderer(resources);

    // Load level 1 and its world-1 backdrop up front so confirming "start" can
    // show the static board in-window (Stage 3 wiring). MONDE1.VEC supplies the
    // per-world VGA palette the board renders under; the decoded resource must
    // outlive run() because decoded_bytes() is a view into it.
    const auto level = bumpy::LevelResources::load(asset_root, 1);
    const auto backdrop = bumpy::decode_vec_resource(asset_root / "MONDE1.VEC");
    const auto backdrop_bytes = backdrop.decoded_bytes();

    // The BUM entity sprites are drawn from the uncompressed BUMSPJEU.BIN bank; it
    // must outlive run() because the sprite span is a view into it.
    const auto sprite_bank = bumpy::decode_sprite_archive(asset_root / "BUMSPJEU.BIN");

    bumpy::App app(level.board_count());
    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(app.menu().view(), frame);

    bumpy::SdlApp sdl;
    return sdl.run(app, renderer, level, backdrop_bytes, sprite_bank.bytes(), frame);
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
        if (argc == 5 && std::string_view(argv[1]) == "--render-map") {
            // --render-map <world> <MONDE.VEC> <out.bmp>
            // world is currently informational (world 1 only); MONDE.VEC supplies the
            // backdrop + palette, and the avatar is drawn at node 1.
            return render_map_to_bmp(asset_root, argv[3], argv[4]);
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
        if (argc != 1) {
            std::cerr << "usage: bumpy_port.exe [--render-title [out.bmp] | --dump-title-raw out.bin "
                         "| --decode-vec in.vec out.bin]\n";
            return 2;
        }
        return run_sdl_menu(asset_root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
