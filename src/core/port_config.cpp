#include "core/port_config.h"

#include <fstream>
#include <sstream>

namespace bumpy {

namespace {

// "1"/"0" only; anything else leaves `out` untouched (tolerate hand-edits).
void parse_bool(std::string_view value, bool& out) {
    if (value == "1") {
        out = true;
    } else if (value == "0") {
        out = false;
    }
}

}  // namespace

PortConfig parse_port_config(std::string_view text) noexcept {
    PortConfig config;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) {
            continue;
        }
        const std::string_view key = line.substr(0, eq);
        const std::string_view value = line.substr(eq + 1);
        if (key == "render3d") {
            parse_bool(value, config.render3d);
        } else if (key == "square_pixels") {
            parse_bool(value, config.square_pixels);
        } else if (key == "fullscreen") {
            parse_bool(value, config.fullscreen);
        } else if (key == "music") {
            parse_bool(value, config.music);
        } else if (key == "sfx") {
            parse_bool(value, config.sfx);
        }
    }
    return config;
}

std::string serialize_port_config(const PortConfig& config) {
    std::ostringstream out;
    out << "# Bumpy's Arcade Fantasy port settings (auto-written; hand-edits are kept\n"
        << "# for known keys, unknown keys are ignored)\n"
        << "render3d=" << (config.render3d ? 1 : 0) << '\n'
        << "square_pixels=" << (config.square_pixels ? 1 : 0) << '\n'
        << "fullscreen=" << (config.fullscreen ? 1 : 0) << '\n'
        << "music=" << (config.music ? 1 : 0) << '\n'
        << "sfx=" << (config.sfx ? 1 : 0) << '\n';
    return out.str();
}

PortConfig load_port_config(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return {};
        }
        std::ostringstream text;
        text << in.rdbuf();
        return parse_port_config(text.str());
    } catch (...) {
        return {};
    }
}

bool save_port_config(const std::filesystem::path& path, const PortConfig& config) noexcept {
    try {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << serialize_port_config(config);
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}

}  // namespace bumpy
