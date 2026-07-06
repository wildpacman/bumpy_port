#include <catch2/catch_test_macros.hpp>
#include "audio/opl2.h"
#include <cmath>

TEST_CASE("Opl2 produces a tone after a minimal key-on") {
    bumpy::Opl2 opl;
    REQUIRE(opl.sample_rate() == 49715u);
    // Minimal OPL2 single-voice key-on (channel 0): operator regs then note-on.
    opl.write(0x20, 0x01); opl.write(0x23, 0x01);  // mult=1 mod+car
    opl.write(0x40, 0x10); opl.write(0x43, 0x00);  // levels (carrier loud)
    opl.write(0x60, 0xF0); opl.write(0x63, 0xF0);  // fast attack
    opl.write(0x80, 0x77); opl.write(0x83, 0x77);  // sustain/release
    opl.write(0xA0, 0x98); opl.write(0xB0, 0x31);  // F-number lo/hi + key-on (block 4)
    double energy = 0.0;
    for (int i = 0; i < 4000; ++i) { float s = opl.sample(); energy += double(s) * s; }
    REQUIRE(energy > 0.0);  // the chip emitted a non-silent waveform
}
