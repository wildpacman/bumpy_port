#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace bumpy {

// Port-level presentation settings persisted in bumpy_port.cfg next to the exe.
// This is deliberately the port's ONLY on-disk persistence (high scores stay
// session-only like the original). Simple key=value lines; unknown keys are
// ignored so future versions can add fields without breaking older builds.
struct PortConfig {
    bool render3d = true;       // Alt+3 diorama mode (on by default)
    bool square_pixels = false; // Alt+A: true = 16:10, false = 4:3 (CRT, default)
    bool fullscreen = true;     // Alt+Enter (fullscreen by default)
    bool music = true;          // intro-music gate (Tab overlay AUDIO page)
    bool sfx = true;            // SFX gate (Tab overlay AUDIO page)
};

[[nodiscard]] PortConfig parse_port_config(std::string_view text) noexcept;
[[nodiscard]] std::string serialize_port_config(const PortConfig& config);
// Missing/unreadable/garbage file -> defaults; never throws.
[[nodiscard]] PortConfig load_port_config(const std::filesystem::path& path) noexcept;
// Best-effort write; false on failure (callers log, the game keeps running).
bool save_port_config(const std::filesystem::path& path, const PortConfig& config) noexcept;

}  // namespace bumpy
