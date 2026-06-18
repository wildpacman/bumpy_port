#include <catch2/catch_test_macros.hpp>

#include "resources/menu_resources.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path write_fixture(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
    return path;
}

}  // namespace

TEST_CASE("menu resource bundle exposes one typed field per confirmed resource role") {
    const auto resources = bumpy::MenuResources::load_from(".");

    REQUIRE(resources.title.terminal_offset() == std::filesystem::file_size("TITRE.VEC"));
    REQUIRE(resources.title.layers().size() == 1);
    REQUIRE(resources.title.decoded_bytes().size() == 32099);

    REQUIRE(resources.mask_bump.terminal_offset() == std::filesystem::file_size("MASKBUMP.VEC"));
    REQUIRE(resources.mask_bump.layers().size() == 3);
    REQUIRE(resources.mask_bump.decoded_bytes().size() == 32099);

    REQUIRE(resources.bumper_sprites.terminal_offset() == std::filesystem::file_size("BUMSPJEU.BIN"));
    REQUIRE(resources.bumper_sprites.root_offsets() == std::array<std::uint32_t, 3>{12, 144, 276});
    REQUIRE(resources.bumper_sprites.group_tables().size() == 3);
    REQUIRE(resources.bumper_sprites.child_blocks().size() == 99);
}

TEST_CASE("sprite archive decoder reproduces the confirmed offset-delimited layout") {
    const auto archive = bumpy::decode_sprite_archive("BUMSPJEU.BIN");

    REQUIRE(archive.terminal_offset() == std::filesystem::file_size("BUMSPJEU.BIN"));
    // root table + 3 group tables + 99 child blocks
    REQUIRE(archive.child_blocks().size() + archive.group_tables().size() + 1 == 103);
    REQUIRE(archive.root_offsets() == std::array<std::uint32_t, 3>{12, 144, 276});
    REQUIRE(archive.group_tables()[0].entry_count == 33);
    REQUIRE(archive.group_tables()[0].first_child_offset == 408);
    REQUIRE(archive.group_tables()[0].last_child_offset == 6080);
    REQUIRE(archive.group_tables()[2].first_child_offset == 12648);
    REQUIRE(archive.group_tables()[2].last_child_offset == 18040);

    const auto& first = archive.child_blocks().front();
    REQUIRE(first.index == 0);
    REQUIRE(first.group_index == 0);
    REQUIRE(first.entry_index == 0);
    REQUIRE(first.start_offset == 408);
    REQUIRE(first.end_offset == 540);
    REQUIRE(first.size == 132);

    const auto& last = archive.child_blocks().back();
    REQUIRE(last.index == 98);
    REQUIRE(last.group_index == 2);
    REQUIRE(last.entry_index == 32);
    REQUIRE(last.start_offset == 18040);
    REQUIRE(last.end_offset == 89116);
    REQUIRE(last.size == 71076);

    REQUIRE(archive.child(0).size() == 132);
    REQUIRE(archive.child(98).size() == 71076);
}

TEST_CASE("sprite archive decoder reports malformed fixtures with resource name and failing offset") {
    const auto path = write_fixture("bumpy-truncated-sprites.bin", {0x00, 0x00, 0x00, 0x0c});

    try {
        (void)bumpy::decode_sprite_archive(path);
        FAIL("expected malformed sprite archive to throw");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        REQUIRE(message.find("bumpy-truncated-sprites.bin") != std::string::npos);
        REQUIRE(message.find("0x00000004") != std::string::npos);
    }

    std::filesystem::remove(path);
}
