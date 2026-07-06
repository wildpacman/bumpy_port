#include <catch2/catch_test_macros.hpp>
#include "audio/speaker_sfx.h"
#include "resources/sfx_tables.h"
#include <vector>

TEST_CASE("speaker sweep tone frequency follows the divisor") {
    REQUIRE(bumpy::sfx_tone_hz(1000) > 1100.0f);   // 1193182/1000 ~= 1193 Hz
    REQUIRE(bumpy::sfx_tone_hz(1000) < 1300.0f);
}

TEST_CASE("a tone preset renders a finite, non-silent, terminating voice") {
    bumpy::SpeakerVoice v;
    v.start(bumpy::kSfxPresets[1]);   // rising chirp
    REQUIRE(v.active());
    std::vector<float> buf(49715);    // 1 second is far more than the sweep lasts
    for (float& s : buf) s = 0.0f;
    v.render_add(buf.data(), buf.size());
    double energy = 0.0;
    for (float s : buf) energy += double(s) * s;
    REQUIRE(energy > 0.0);            // it made sound
    REQUIRE_FALSE(v.active());        // and it finished within a second
}
