#include <SDL3/SDL_main.h>

#include "core/asset_manifest.h"
#include "core/indexed_framebuffer.h"
#include "game/menu.h"
#include "platform_sdl3/sdl_app.h"
#include "resources/menu_resources.h"
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
    if (frame.width() != 320 || frame.height() != 200) {
        throw std::runtime_error("frame is not 320x200");
    }
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

int run_sdl_menu(const std::filesystem::path& asset_root) {
    warn_if_assets_changed(asset_root);
    const auto resources = bumpy::MenuResources::load_from(asset_root);
    const bumpy::MenuRenderer renderer(resources);
    bumpy::Menu menu;
    bumpy::IndexedFramebuffer frame(320, 200);
    renderer.render(menu.view(), frame);

    bumpy::SdlApp app;
    return app.run(menu, renderer, frame);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const auto asset_root =
            find_asset_root(argc > 0 ? std::string_view(argv[0]) : std::string_view{});
        if (argc == 3 && std::string_view(argv[1]) == "--render-title") {
            return render_title_to_bmp(asset_root, argv[2]);
        }
        if (argc == 3 && std::string_view(argv[1]) == "--dump-title-raw") {
            return dump_title_raw(asset_root, argv[2]);
        }
        if (argc != 1) {
            std::cerr << "usage: bumpy_port.exe [--render-title out.bmp | --dump-title-raw out.bin]\n";
            return 2;
        }
        return run_sdl_menu(asset_root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
