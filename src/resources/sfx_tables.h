#pragma once
#include <cstdint>

namespace bumpy {

// PC-speaker "sweep engine" waveform kinds (handlers 1000:9488 tone / 1000:9502 noise).
enum class SweepKind { tone, noise };

// One recovered SFX preset from the 1000:6e30 switch (cases 1..0x15). A sweep starts
// at `init_divisor` (tone pitch = 1193182/divisor), runs `steps` ISR ticks stepping the
// divisor by `divisor_step` each tick; the ISR period starts at `rate_seed` and shifts by
// `rate_step` every `rate_count` ticks. `divisor_step`/`rate_step` are raw u16 (two's
// complement for negative sweeps).
struct SfxPreset {
    bool used;
    SweepKind kind;
    std::uint16_t init_divisor, steps, divisor_step, rate_seed, rate_count, rate_step;
};

// Preset table indexed by the 6e30 sound id (1..0x15). [0] and [0x13] are unused.
// Baked from the disassembly of 1000:6e30; see tools/re/dump_sfx.py.
extern const SfxPreset kSfxPresets[0x16];

// Per-tile speaker SFX maps (id = table[index]); 0 = silent. Copied verbatim from the
// data segment of BUMPY.UNPACKED.EXE (file offset 0x11440 + the noted DS offset).
extern const std::uint8_t kSfxIdleRest[0x30];     // DS:0x25de  (idle-rest, 6648)
extern const std::uint8_t kSfxRollBump[0x30];     // DS:0x263e  (roll/hop bump, 63be)
extern const std::uint8_t kSfxFallRoute[0x30];    // DS:0x269e  (fall routing, 2810)
extern const std::uint8_t kSfxHeldBump[0x30];     // DS:0x26fe  (held-bump in bounce, 647e)
extern const std::uint8_t kSfxLayerBBlock[0x20];  // DS:0x274e  (layer-B block, 6a89)
extern const std::uint8_t kSfxPictureBlock[0x20]; // DS:0x278e  (picture block, 640c)

// Bounded speaker-profile lookup helpers. The original tables have fixed extents;
// out-of-range selectors should stay silent instead of wrapping onto another element.
[[nodiscard]] std::uint8_t sfx_idle_rest(std::uint8_t tile) noexcept;
[[nodiscard]] std::uint8_t sfx_roll_bump(std::uint8_t tile) noexcept;
[[nodiscard]] std::uint8_t sfx_fall_route(std::uint8_t tile) noexcept;
[[nodiscard]] std::uint8_t sfx_held_bump(std::uint8_t tile) noexcept;
[[nodiscard]] std::uint8_t sfx_layer_b_block(std::uint8_t event_id) noexcept;
[[nodiscard]] std::uint8_t sfx_picture_block(std::uint8_t plane_b_value) noexcept;

}  // namespace bumpy
