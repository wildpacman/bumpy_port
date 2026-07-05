#include <catch2/catch_test_macros.hpp>
#include "resources/midi_song.h"

TEST_CASE("BUMPY.MID parses as a 7-track SMF") {
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    REQUIRE(song.division() == 192);
    REQUIRE(song.tempo_map().front().usec_per_qn == 800000);  // FF 51 03 0C 35 00
    REQUIRE(song.tempo_map().front().tick == 0);

    // Events are tick-sorted and non-empty; there is at least one Note-On (0x90..0x9F).
    const auto& ev = song.events();
    REQUIRE(!ev.empty());
    for (std::size_t i = 1; i < ev.size(); ++i) REQUIRE(ev[i].tick >= ev[i - 1].tick);
    bool has_note_on = false;
    for (const auto& e : ev) if ((e.status & 0xF0) == 0x90 && e.data2 != 0) { has_note_on = true; break; }
    REQUIRE(has_note_on);
    REQUIRE(song.end_tick() == 54095);  // true last tick (incl. trailing delta before FF 2F)
    REQUIRE(song.end_tick() >= ev.back().tick);
}
