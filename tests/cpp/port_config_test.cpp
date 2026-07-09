#include <catch2/catch_test_macros.hpp>

#include "core/port_config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using bumpy::PortConfig;

TEST_CASE("defaults: 3D on, 4:3 aspect, fullscreen, audio on") {
    const PortConfig c{};
    REQUIRE(c.render3d);
    REQUIRE_FALSE(c.square_pixels);  // false == 4:3
    REQUIRE(c.fullscreen);
    REQUIRE(c.music);
    REQUIRE(c.sfx);
}

TEST_CASE("serialize/parse round-trips every field") {
    PortConfig c;
    c.render3d = false;
    c.square_pixels = true;
    c.fullscreen = false;
    c.music = false;
    c.sfx = false;
    const auto back = bumpy::parse_port_config(bumpy::serialize_port_config(c));
    REQUIRE_FALSE(back.render3d);
    REQUIRE(back.square_pixels);
    REQUIRE_FALSE(back.fullscreen);
    REQUIRE_FALSE(back.music);
    REQUIRE_FALSE(back.sfx);
}

TEST_CASE("an old file without the audio keys parses them as enabled") {
    const auto c = bumpy::parse_port_config("render3d=0\nsquare_pixels=1\nfullscreen=0\n");
    REQUIRE(c.music);   // key absent -> default true
    REQUIRE(c.sfx);
}

TEST_CASE("parse ignores unknown keys, blank lines and comments; partial file keeps defaults") {
    const auto c = bumpy::parse_port_config("# comment\n\nrender3d=1\nfuture_key=42\n");
    REQUIRE(c.render3d);
    REQUIRE_FALSE(c.square_pixels);   // untouched default
    REQUIRE(c.fullscreen);
}

TEST_CASE("parse tolerates garbage values") {
    const auto c = bumpy::parse_port_config("render3d=banana\nsquare_pixels=\n=1\n");
    REQUIRE(c.render3d);
    REQUIRE_FALSE(c.square_pixels);
}

TEST_CASE("load returns defaults for a missing file; save/load round-trips") {
    const auto dir = std::filesystem::temp_directory_path();
    const auto path = dir / "bumpy_port_config_test.cfg";
    std::filesystem::remove(path);

    const auto missing = bumpy::load_port_config(path);
    REQUIRE(missing.render3d);

    PortConfig c;
    c.render3d = true;
    c.fullscreen = true;
    REQUIRE(bumpy::save_port_config(path, c));
    const auto back = bumpy::load_port_config(path);
    REQUIRE(back.render3d);
    REQUIRE(back.fullscreen);
    std::filesystem::remove(path);
}
