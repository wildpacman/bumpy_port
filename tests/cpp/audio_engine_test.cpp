#include <catch2/catch_test_macros.hpp>
#include "audio/audio_engine.h"
#include "resources/midi_song.h"
#include "resources/adlib_bank.h"
#include <vector>

TEST_CASE("AudioEngine mixes an SFX one-shot") {
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    bumpy::AudioEngine engine(song, bank);
    std::vector<float> buf(4096, 0.0f);

    engine.render(buf.data(), buf.size());     // no music, no sfx -> silence
    double idle = 0.0; for (float s : buf) idle += double(s) * s;
    REQUIRE(idle == 0.0);

    engine.play_sfx(1);                        // fire a chirp
    engine.render(buf.data(), buf.size());
    double active = 0.0; for (float s : buf) active += double(s) * s;
    REQUIRE(active > 0.0);
}

TEST_CASE("AudioEngine SFX gate silences play_sfx when disabled") {
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    bumpy::AudioEngine engine(song, bank);
    std::vector<float> buf(4096, 0.0f);

    engine.set_sfx_enabled(false);
    engine.play_sfx(1);                        // ignored while disabled
    engine.render(buf.data(), buf.size());
    double off = 0.0; for (float s : buf) off += double(s) * s;
    REQUIRE(off == 0.0);

    engine.set_sfx_enabled(true);
    engine.play_sfx(1);                        // audible again
    engine.render(buf.data(), buf.size());
    double on = 0.0; for (float s : buf) on += double(s) * s;
    REQUIRE(on > 0.0);
}
