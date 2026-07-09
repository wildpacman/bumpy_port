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

TEST_CASE("AudioEngine SFX is monophonic: a new sound preempts the one in flight") {
    // The original PC-speaker sweep engine has ONE global sound slot
    // (DAT_1000_9788..979f + the single active step-handler DAT_1000_97a1); every
    // play call overwrites it, cutting off whatever tone was mid-sweep. So starting
    // the short chirp 0x01 while the ~999 ms sweep 0x03 (the side-spring/deflector
    // sound) is running MUST silence 0x03 -- otherwise that long sweep runs to
    // completion and is heard as the "long, drawn-out spring" bug.
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    bumpy::AudioEngine engine(song, bank);

    engine.play_sfx(0x03);                     // long ~999 ms swept tone
    engine.play_sfx(0x01);                     // short chirp -- must replace 0x03

    // ~603 ms: well past the short chirp, but far short of 0x03's ~999 ms.
    std::vector<float> buf(30000, 0.0f);
    engine.render(buf.data(), buf.size());

    // The chirp near the start actually sounded...
    double head = 0.0;
    for (std::size_t i = 0; i < 5000; ++i) head += double(buf[i]) * buf[i];
    REQUIRE(head > 0.0);

    // ...and the last ~100 ms is silent: the chirp has ended and the long sweep was
    // preempted, not left running on a second voice. Monophonic leaves only the
    // persistent low-pass filter's float-underflow tail here (<1e-6); the polyphonic
    // pool left 0x03 sounding -- tail energy ~132.
    double tail = 0.0;
    for (std::size_t i = 25000; i < buf.size(); ++i) tail += double(buf[i]) * buf[i];
    REQUIRE(tail < 1e-6);
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
