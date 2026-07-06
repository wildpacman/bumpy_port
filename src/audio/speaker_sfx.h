#pragma once
#include "resources/sfx_tables.h"
#include <cstddef>
#include <cstdint>

namespace bumpy {

// PC-speaker PIT tone frequency for a given timer-2 reload divisor (the same formula
// the 6e30 preset table's `init_divisor` and per-tick `divisor` use throughout).
float sfx_tone_hz(std::uint16_t divisor);

// Simulates the PC-speaker "sweep engine" ISR at the audio sample rate rather than via
// a real periodic interrupt. The timing model below was recovered exactly from the
// binary (capstone), NOT guessed:
//
//   * The scheduler (FUN_1000_7cde/7f9a) programs PIT timer-0 ONCE to reload 2385, so
//     IRQ0 fires at a FIXED base rate F0 = 1193182/2385 = 500.286 Hz (kBaseTickHz).
//   * The active sound is advanced by a Bresenham/DDS divider in the ISR
//     (FUN_1000_7c02): every base tick `acc += period`; when `acc >= 500` it fires the
//     step handler and does `acc -= 500`. `period` starts at the preset's `rate_seed`
//     (so the step handler runs at ~`rate_seed` Hz -- LARGER rate_seed => FASTER, the
//     inverse of a divisor). Re-installing the slot (which the glide path does every
//     `rate_count` fires) ZEROES the accumulator -- this reset is load-bearing and
//     roughly doubles the duration of rate_count==1 presets.
//
// Step handlers:
//   * Tone (0x9631): each fire advances the PIT ch-2 divisor `DAT_978a += divisor_step`
//     (signed) and reprograms ch 2, so freq = 1193182/divisor. A separate `rate_count`
//     sub-divider glides `period` by `rate_step` (and re-installs the slot, zeroing
//     `acc`). The sound ends after `steps` fires (the terminating fire makes no tone
//     step). The inner tone-step divider (a 7th preset field the port folds away) is 1
//     for every preset, so one tone-step per fire.
//   * Noise (0x96c4): NOT a swept square. ch2 is held at a constant (~ultrasonic)
//     divisor; the audible noise is a 16-bit shift register `L1` clocked ONE BIT PER
//     FIRE straight to the speaker-data line (bit 8 of L1 -> port 0x61 bit 1). Every
//     fire `L1 = rotl(L1,1)`; every 16 fires it is reseeded `L1 = (L1+0x2345)^L3;
//     L3 = L1+0x4567` (seeds L1=L3=0 at power-on -- they persist across sounds). The
//     burst length / rate glide use the same `steps`/`rate_count`/`rate_step` fields.
class SpeakerVoice {
public:
    // The shared engine sample rate (ymfm/OPL2's native rate: clock/72).
    static constexpr std::uint32_t kSampleRate = 49715;

    // Fixed PIT timer-0 base-tick rate: 1193182 / 2385 (reload 0x951). Recovered from
    // FUN_1000_7db1 (`mov ax,0x951`) feeding the PIT program FUN_1000_7f9a.
    static constexpr double kBaseTickHz = 1193182.0 / 2385.0;   // 500.286 Hz
    // DDS threshold the ISR compares the accumulator against (FUN_1000_7c44 `cmp ax,0x1f4`).
    static constexpr std::int32_t kDdsThreshold = 500;

    // Noise LFSR reseed constants (adds at 0x96e6 / 0x96f7); the 0x96c4 in-game path
    // applies NO 0xdb6d mask (that belongs to the unused 0x95b5 reseed-only handler).
    static constexpr std::uint16_t kNoiseAddA = 0x2345;
    static constexpr std::uint16_t kNoiseAddB = 0x4567;

    // (Re)starts this voice from a preset, replacing any in-flight sweep.
    void start(const SfxPreset& preset);

    // Advances the voice by `frames` samples, adding its 1-bit speaker waveform into
    // `out` (does not overwrite -- callers mix multiple voices into the same buffer).
    // Stops early (leaving the remainder of `out` untouched) once the voice finishes.
    void render_add(float* out, std::size_t frames);

    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    void base_tick();          // one fixed-rate ISR tick: DDS accumulate + maybe fire
    void fire_tone();          // one 0x9631 step-handler invocation
    void fire_noise();         // one 0x96c4 step-handler invocation
    [[nodiscard]] float current_hz() const;

    static constexpr float kAmplitude = 0.25f;

    bool active_ = false;
    SweepKind kind_ = SweepKind::tone;

    // DDS divider (shared by both handlers).
    std::int32_t acc_ = 0;             // Bresenham accumulator (DAT slot [bx+2])
    std::int32_t period_ = 1;          // DDS increment, starts rate_seed, glides
    std::int32_t steps_left_ = 0;      // outer step counter (DAT_9788)
    std::int16_t rate_step_ = 0;       // period glide increment (DAT_9794)
    std::int32_t rate_count_ = 1;      // glide sub-divider reload (DAT_9792)
    std::int32_t glide_left_ = 1;      // glide sub-divider counter (DAT_9798)
    double samples_until_base_tick_ = 0.0;

    // Tone voice.
    std::int32_t divisor_ = 1;         // PIT ch-2 reload (DAT_978a)
    std::int16_t divisor_step_ = 0;    // per-fire divisor advance (DAT_978e)
    double phase_ = 0.0;               // square-wave phase, 0..1 cycles

    // Noise voice (persist across sounds, like the original's BSS registers).
    std::uint16_t l1_ = 0;             // shift register clocked to the speaker (DAT_979b)
    std::uint16_t l3_ = 0;             // reseed feedback word (DAT_979d)
    std::uint8_t noise_reseed_ctr_ = 0x0f;  // reseed cadence counter (DAT_979a)
    float noise_level_ = 0.0f;         // current speaker bit as +/-kAmplitude
};

}  // namespace bumpy
