#pragma once
#include "resources/sfx_tables.h"
#include <cstddef>
#include <cstdint>

namespace bumpy {

// PC-speaker PIT tone frequency for a given timer-2 reload divisor (the same formula
// the 6e30 preset table's `init_divisor` and per-tick `divisor` use throughout).
float sfx_tone_hz(std::uint16_t divisor);

// Simulates the PC-speaker "sweep engine" ISR (setup stubs 0x9488 tone / 0x9502 noise /
// 0x956d noise-reseed; the sweep bodies proper are installed as the raw ISR handlers at
// 0x9631 tone / 0x96c4 noise / 0x95b5 LFSR-reseed) at the audio sample rate, rather than
// via a real periodic interrupt.
//
// Voice state: `divisor_` (the PIT-style reload value driving the square wave),
// `steps_left_` (ISR ticks remaining before the voice silences), `isr_period_` (the
// ISR's own re-trigger period, starts at `rate_seed` and is itself swept by `rate_step`
// every `rate_count` ticks), and `phase_` (a free-running square-wave phase
// accumulator). Noise voices additionally carry a 16-bit `lfsr_` register.
//
// Each simulated ISR tick: a tone voice advances `divisor_` by the (signed) preset
// `divisor_step`; a noise voice instead reseeds `divisor_` from the LFSR (see the
// note on `kNoiseLfsrMul`/`kNoiseLfsrAdd` below). `steps_left_` always decrements,
// silencing the voice at 0. Independently, every `rate_count` ticks `isr_period_`
// is stepped by (signed) `rate_step`. Between ticks, a square wave at
// `sfx_tone_hz(divisor_)` (amplitude ~0.25) is added into the output.
class SpeakerVoice {
public:
    // The shared engine sample rate (ymfm/OPL2's native rate: clock/72).
    static constexpr std::uint32_t kSampleRate = 49715;

    // TUNING CONSTANT (audio design spec "open item 1"): the absolute rate the sweep
    // ISR itself runs at was not recovered from the disassembly (handlers 0x9631/
    // 0x96c4/0x95b5 are raw interrupt bodies, not auto-decompiled to C). The design
    // brief's illustrative starting guess of 1193182/256 (~4661 Hz) makes
    // `kSfxPresets[1]` (the canonical "rising chirp", 30 ISR ticks) take ~3.0s to
    // finish -- far too slow for a bump/pickup SFX, and it blows past the 1-second
    // budget the engine's own terminating-voice test allows. 1193182/64 (~18.6 kHz)
    // is used instead, which keeps the same "PIT-derived base clock" shape but scales
    // it so that preset finishes in ~0.75s (comfortable margin under 1s). This is
    // still a placeholder: tune it against a DOSBox-X capture of real in-game SFX
    // durations/pitch contours (Step 6), which is a by-ear human follow-up.
    static constexpr double kSfxIsrBaseHz = 1193182.0 / 64.0;

    // Noise-voice LFSR reseed constants named in the design brief (from handler
    // 0x95b5). The exact bit-level recurrence used by the original ISR was not
    // recovered (that address range wasn't auto-decompiled), so this implements a
    // 16-bit multiplicative-XOR-style scramble seeded at kNoiseLfsrSeed and iterated
    // as `lfsr = lfsr * kNoiseLfsrMul + kNoiseLfsrAdd` each tick; the full-range
    // pseudorandom result is used directly as the next tick's PIT-style divisor,
    // which (via phase-accumulator aliasing above Nyquist) yields broadband noise.
    static constexpr std::uint16_t kNoiseLfsrSeed = 0x2345;
    static constexpr std::uint16_t kNoiseLfsrMul = 0x4567;
    static constexpr std::uint16_t kNoiseLfsrAdd = 0xdb6d;

    // (Re)starts this voice from a preset, replacing any in-flight sweep.
    void start(const SfxPreset& preset);

    // Advances the voice by `frames` samples, adding its square wave into `out`
    // (does not overwrite -- callers mix multiple voices into the same buffer).
    // Stops early (leaving the remainder of `out` untouched) once the voice finishes.
    void render_add(float* out, std::size_t frames);

    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    void tick();
    [[nodiscard]] float current_hz() const;
    [[nodiscard]] double samples_per_tick() const;

    bool active_ = false;
    SweepKind kind_ = SweepKind::tone;

    std::int32_t divisor_ = 1;
    std::int16_t divisor_step_ = 0;
    std::int32_t steps_left_ = 0;

    double isr_period_ = 1.0;
    std::int16_t rate_step_ = 0;
    std::int32_t rate_count_ = 1;
    std::int32_t rate_left_ = 1;

    double phase_ = 0.0;              // square-wave phase, 0..1 cycles
    double samples_until_tick_ = 0.0;  // countdown to the next simulated ISR tick

    std::uint16_t lfsr_ = kNoiseLfsrSeed;
};

}  // namespace bumpy
