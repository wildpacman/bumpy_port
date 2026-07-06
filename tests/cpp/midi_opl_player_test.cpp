#include <catch2/catch_test_macros.hpp>
#include "audio/midi_opl_player.h"
#include "resources/midi_song.h"
#include "resources/adlib_bank.h"
#include <vector>

TEST_CASE("MidiOplPlayer renders audible output from BUMPY.MID") {
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    bumpy::MidiOplPlayer player(song, bank, /*loop=*/true);

    std::vector<float> buf(49715 * 3);  // ~3 seconds
    player.render(buf.data(), buf.size());
    double energy = 0.0;
    for (float s : buf) energy += double(s) * s;
    REQUIRE(energy > 0.0);              // the song produced sound
    REQUIRE_FALSE(player.finished());  // looping -> never finishes
}
