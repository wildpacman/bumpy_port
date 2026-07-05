# Audio / Sound System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Bumpy port sound — the intro/credits MIDI music rendered on an OPL2/AdLib synth, and all in-game/menu sound effects synthesized from the recovered PC-speaker sweep engine.

**Architecture:** A new pure `src/audio` module (OPL2 wrapper, MIDI→OPL player, PC-speaker sweep synth, a mixing `audio_engine`) plus `resources` decoders for `BUMPY.MID` and `BUMPY.BNK`, plus a thin `platform_sdl3` audio adapter. Game logic stays SDL-independent by emitting sound-event ids per tick, which the SDL shell drains into `audio_engine.play_sfx`.

**Tech Stack:** C++20, CMake + FetchContent, Catch2 tests, SDL3 audio, the **ymfm** YM3812 (OPL2) core (BSD-3-Clause, fetched). Python + capstone for the table-dump tool.

## Global Constraints

- Language/std: **C++20**, `CMAKE_CXX_EXTENSIONS OFF` (copied from `CMakeLists.txt`).
- Namespace: everything in `namespace bumpy`.
- Decoders live in `src/resources`, pure synth/sequencing in `src/audio`, SDL only in `src/platform_sdl3` — never include `<SDL3/*>` outside `platform_sdl3`.
- Decoder factory pattern (match `src/resources/font.h`): `static X load(const std::filesystem::path&)` + `static X from_bytes(std::vector<std::uint8_t>)`.
- Tests are Catch2 (`#include <catch2/catch_test_macros.hpp>`, `TEST_CASE`/`REQUIRE`), run from the project root, and load originals by bare filename. New test files must be added to `bumpy_tests` in `CMakeLists.txt`.
- Originals are read-only: never modify `BUMPY.MID`/`BUMPY.BNK`/etc.; after any reference run verify with `python tools/assets/manifest.py verify`. Do not commit generated audio; generated `*.gen.cpp` tables ARE committed (matches the project's other `dump_*` outputs).
- Audio engine internal sample rate = **49716 Hz** (the OPL2 native rate, clock 3'579'545 / 72); SDL3's `SDL_AudioStream` resamples to the device. Output is mono `float` in `[-1, 1]`.
- Build/test commands (Windows):
  - Configure/build: `cmake --build --preset windows-debug`
  - Run tests: `& build/windows-debug/Debug/bumpy_tests.exe`

---

## File Structure

**New — resources (pure decoders):**
- `src/resources/adlib_bank.{h,cpp}` — parse `BUMPY.BNK` (AdLib `.BNK`).
- `src/resources/midi_song.{h,cpp}` — parse `BUMPY.MID` (SMF format 1).
- `src/resources/sfx_tables.h` + `src/resources/sfx_tables.gen.cpp` — recovered SFX presets + trigger/tile tables (generated).
- `tools/re/dump_sfx.py` — extracts the SFX tables from `BUMPY.UNPACKED.EXE`.

**New — audio (pure synth/sequencing):**
- `src/audio/opl2.{h,cpp}` — ymfm YM3812 wrapper (register write + sample generate).
- `src/audio/midi_opl_player.{h,cpp}` — MIDI→OPL2 sequencer/driver.
- `src/audio/speaker_sfx.{h,cpp}` — the sweep-engine SFX synth (voices).
- `src/audio/audio_engine.{h,cpp}` — owns the music player + SFX voice pool; mixes.

**New — platform:**
- `src/platform_sdl3/sdl_audio.{h,cpp}` — SDL3 audio stream pulling from `audio_engine`.

**Modified:**
- `CMakeLists.txt` — FetchContent ymfm; add the new sources to `bumpy_core`; add tests.
- `src/game/level_game.{h,cpp}` — add the per-tick sound-event queue + `emit_sfx` at the recovered trigger sites.
- `src/game/world_map.{h,cpp}` — emit the cloud-jump-launch SFX.
- `src/platform_sdl3/sdl_app.{h,cpp}` + `src/app/main.cpp` — construct `AudioEngine`, open `SdlAudio`, start/stop music on `Screen::splash`, drain SFX events.

---

## Task 1: `adlib_bank` decoder (BUMPY.BNK)

**Files:**
- Create: `src/resources/adlib_bank.h`, `src/resources/adlib_bank.cpp`
- Test: `tests/cpp/adlib_bank_test.cpp`
- Modify: `CMakeLists.txt` (add `src/resources/adlib_bank.cpp` to `bumpy_core`; add the test file to `bumpy_tests`)

**Interfaces:**
- Consumes: `bumpy::read_binary_file` (`resources/binary_reader.h`).
- Produces:
  - `struct AdLibOperator { std::uint8_t ksl, mult, feedback, attack, sustain, eg, decay, release, level, am, vib, ksr, connection; };`
  - `struct AdLibInstrument { std::uint8_t mode; std::uint8_t perc_voice; AdLibOperator mod, car; std::uint8_t wave_mod, wave_car; };`
  - `class AdLibBank { static AdLibBank load(const std::filesystem::path&); static AdLibBank from_bytes(std::vector<std::uint8_t>); std::size_t size() const; const AdLibInstrument& instrument(std::size_t index) const; const AdLibInstrument* by_name(std::string_view name) const; };`

Format (confirmed by parsing `BUMPY.BNK`, 6748 bytes): header `[0..1]` version `{1,0}`, `[2..7]` `"ADLIB-"`, `[8..9]` u16 used=129, `[10..11]` u16 total=160, `[12..15]` u32 name_offset=0x1c, `[16..19]` u32 inst_offset=0x79c, `[20..27]` reserved. Name list = `total` records of 12 bytes at `name_offset`: `{u16 inst_index, u8 used, char name[9]}`. Instrument data = `total` records of 30 bytes at `inst_offset`: `[0]mode [1]perc_voice [2..14]mod(13 fields, order ksl,mult,feedback,attack,sustain,eg,decay,release,level,am,vib,ksr,connection) [15..27]car(same 13) [28]wave_mod [29]wave_car`.

- [ ] **Step 1: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "resources/adlib_bank.h"

TEST_CASE("BUMPY.BNK decodes the AdLib instrument bank") {
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    REQUIRE(bank.size() == 160);

    // Name index: name[0] = "rol000" -> instrument slot 27.
    const auto* rol000 = bank.by_name("rol000");
    REQUIRE(rol000 != nullptr);
    REQUIRE(rol000 == &bank.instrument(27));

    // rol000 raw fields (confirmed from the file).
    REQUIRE(rol000->mode == 0);
    REQUIRE(rol000->mod.ksl == 1);
    REQUIRE(rol000->mod.mult == 1);
    REQUIRE(rol000->mod.feedback == 3);
    REQUIRE(rol000->mod.attack == 11);
    REQUIRE(rol000->mod.level == 15);
    REQUIRE(rol000->mod.connection == 1);
    REQUIRE(rol000->car.ksl == 0);
    REQUIRE(rol000->car.feedback == 12);
    REQUIRE(rol000->car.attack == 9);
    REQUIRE(rol000->car.decay == 2);
    REQUIRE(rol000->wave_mod == 0);
    REQUIRE(rol000->wave_car == 0);

    // A missing name returns nullptr.
    REQUIRE(bank.by_name("nope__") == nullptr);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset windows-debug` (expected: compile error — `adlib_bank.h` missing).

- [ ] **Step 3: Write `src/resources/adlib_bank.h`**

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bumpy {

struct AdLibOperator {
    std::uint8_t ksl, mult, feedback, attack, sustain, eg, decay, release, level, am, vib, ksr, connection;
};
struct AdLibInstrument {
    std::uint8_t mode{};
    std::uint8_t perc_voice{};
    AdLibOperator mod{};
    AdLibOperator car{};
    std::uint8_t wave_mod{};
    std::uint8_t wave_car{};
    std::string name;  // from the bank's name index (empty if unnamed)
};

// AdLib .BNK instrument bank (BUMPY.BNK). Header "ADLIB-", a 12-byte name index and
// 30-byte instrument records. Renders the intro MIDI on OPL2 (see the audio design spec).
class AdLibBank {
public:
    static AdLibBank load(const std::filesystem::path& path);
    static AdLibBank from_bytes(std::vector<std::uint8_t> bytes);

    [[nodiscard]] std::size_t size() const noexcept { return instruments_.size(); }
    [[nodiscard]] const AdLibInstrument& instrument(std::size_t index) const { return instruments_.at(index); }
    [[nodiscard]] const AdLibInstrument* by_name(std::string_view name) const;

private:
    std::vector<AdLibInstrument> instruments_;
};

}  // namespace bumpy
```

- [ ] **Step 4: Write `src/resources/adlib_bank.cpp`**

```cpp
#include "resources/adlib_bank.h"
#include "resources/binary_reader.h"
#include <cstring>
#include <stdexcept>

namespace bumpy {
namespace {
std::uint16_t u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t{p[3]} << 24));
}
AdLibOperator read_op(const std::uint8_t* f) {
    return {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12]};
}
}  // namespace

AdLibBank AdLibBank::load(const std::filesystem::path& path) {
    return from_bytes(read_binary_file(path));
}

AdLibBank AdLibBank::from_bytes(std::vector<std::uint8_t> b) {
    if (b.size() < 28 || std::memcmp(&b[2], "ADLIB-", 6) != 0)
        throw std::runtime_error("BUMPY.BNK: bad header");
    const std::uint16_t total = u16(&b[10]);
    const std::uint32_t name_off = u32(&b[12]);
    const std::uint32_t inst_off = u32(&b[16]);
    if (name_off + std::size_t{total} * 12 > b.size() || inst_off + std::size_t{total} * 30 > b.size())
        throw std::runtime_error("BUMPY.BNK: truncated");

    AdLibBank bank;
    bank.instruments_.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint8_t* r = &b[inst_off + i * 30];
        AdLibInstrument& in = bank.instruments_[i];
        in.mode = r[0];
        in.perc_voice = r[1];
        in.mod = read_op(r + 2);
        in.car = read_op(r + 15);
        in.wave_mod = r[28];
        in.wave_car = r[29];
    }
    for (std::size_t i = 0; i < total; ++i) {
        const std::uint8_t* n = &b[name_off + i * 12];
        const std::uint16_t idx = u16(n);
        const char* name = reinterpret_cast<const char*>(n + 3);
        std::size_t len = 0;
        while (len < 9 && name[len] != '\0') ++len;
        if (idx < total) bank.instruments_[idx].name.assign(name, len);
    }
    return bank;
}

const AdLibInstrument* AdLibBank::by_name(std::string_view name) const {
    for (const auto& in : instruments_)
        if (in.name == name) return &in;
    return nullptr;
}

}  // namespace bumpy
```

- [ ] **Step 5: Wire CMake** — add `src/resources/adlib_bank.cpp` to `bumpy_core` (after `font.cpp`, line ~29) and `tests/cpp/adlib_bank_test.cpp` to `bumpy_tests` (after `font_test.cpp`, line ~72).

- [ ] **Step 6: Build and run the test**

Run: `cmake --build --preset windows-debug` then `& build/windows-debug/Debug/bumpy_tests.exe "BUMPY.BNK decodes the AdLib instrument bank"`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/resources/adlib_bank.h src/resources/adlib_bank.cpp tests/cpp/adlib_bank_test.cpp CMakeLists.txt
git commit -m "feat(audio): decode BUMPY.BNK AdLib instrument bank"
```

---

## Task 2: `midi_song` decoder (BUMPY.MID)

**Files:**
- Create: `src/resources/midi_song.h`, `src/resources/midi_song.cpp`
- Test: `tests/cpp/midi_song_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `bumpy::read_binary_file`.
- Produces:
  - `struct MidiEvent { std::uint32_t tick; std::uint8_t status; std::uint8_t data1, data2; };` (channel voice messages only: status high-nibble 0x8/0x9/0xB/0xC/0xE; running status already resolved; `data2`=0 for 2-byte messages)
  - `struct TempoChange { std::uint32_t tick; std::uint32_t usec_per_qn; };`
  - `class MidiSong { static MidiSong load(const std::filesystem::path&); static MidiSong from_bytes(std::vector<std::uint8_t>); int division() const; const std::vector<MidiEvent>& events() const; const std::vector<TempoChange>& tempo_map() const; std::uint32_t end_tick() const; };`

The parser merges all `MTrk` tracks into one tick-sorted `events()` list, extracts `FF 51` tempo changes into `tempo_map()` (seeded with 500000 if none at tick 0), records the last event tick as `end_tick()`, and drops SysEx/other meta. Confirmed facts for the test: format 1, 7 tracks, division 192, first tempo `FF 51 03 0C 35 00` = 0x0C3500 = 800000 µs/qn.

- [ ] **Step 1: Write the failing test**

```cpp
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
    REQUIRE(song.end_tick() >= ev.back().tick);
}
```

- [ ] **Step 2: Run test to verify it fails** — `cmake --build --preset windows-debug` (compile error: header missing).

- [ ] **Step 3: Write `src/resources/midi_song.h`**

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace bumpy {

struct MidiEvent { std::uint32_t tick; std::uint8_t status; std::uint8_t data1; std::uint8_t data2; };
struct TempoChange { std::uint32_t tick; std::uint32_t usec_per_qn; };

// Standard MIDI File (BUMPY.MID, format 1, 7 tracks). All tracks merged into one
// tick-sorted channel-voice event stream + a tempo map. SysEx and non-tempo meta dropped.
class MidiSong {
public:
    static MidiSong load(const std::filesystem::path& path);
    static MidiSong from_bytes(std::vector<std::uint8_t> bytes);

    [[nodiscard]] int division() const noexcept { return division_; }
    [[nodiscard]] const std::vector<MidiEvent>& events() const noexcept { return events_; }
    [[nodiscard]] const std::vector<TempoChange>& tempo_map() const noexcept { return tempo_; }
    [[nodiscard]] std::uint32_t end_tick() const noexcept { return end_tick_; }

private:
    int division_{};
    std::uint32_t end_tick_{};
    std::vector<MidiEvent> events_;
    std::vector<TempoChange> tempo_;
};

}  // namespace bumpy
```

- [ ] **Step 4: Write `src/resources/midi_song.cpp`**

```cpp
#include "resources/midi_song.h"
#include "resources/binary_reader.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bumpy {
namespace {
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
// Read an SMF variable-length quantity; advances `pos`.
std::uint32_t read_vlq(const std::vector<std::uint8_t>& b, std::size_t& pos) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint8_t c = b.at(pos++);
        v = (v << 7) | (c & 0x7F);
        if (!(c & 0x80)) break;
    }
    return v;
}
}  // namespace

MidiSong MidiSong::load(const std::filesystem::path& path) { return from_bytes(read_binary_file(path)); }

MidiSong MidiSong::from_bytes(std::vector<std::uint8_t> b) {
    if (b.size() < 14 || std::memcmp(&b[0], "MThd", 4) != 0) throw std::runtime_error("BUMPY.MID: no MThd");
    const std::uint16_t ntrks = static_cast<std::uint16_t>((b[10] << 8) | b[11]);
    MidiSong song;
    song.division_ = (b[12] << 8) | b[13];

    std::size_t pos = 8 + be32(&b[4]);  // past the header chunk
    for (std::uint16_t t = 0; t < ntrks; ++t) {
        if (pos + 8 > b.size() || std::memcmp(&b[pos], "MTrk", 4) != 0) break;
        const std::uint32_t len = be32(&b[pos + 4]);
        std::size_t p = pos + 8;
        const std::size_t end = p + len;
        std::uint32_t tick = 0;
        std::uint8_t running = 0;
        while (p < end) {
            tick += read_vlq(b, p);
            std::uint8_t status = b.at(p);
            if (status & 0x80) { ++p; running = status; }
            else status = running;  // running status
            if (status == 0xFF) {           // meta
                std::uint8_t type = b.at(p++);
                std::uint32_t mlen = read_vlq(b, p);
                if (type == 0x51 && mlen == 3)
                    song.tempo_.push_back({tick, static_cast<std::uint32_t>((b[p] << 16) | (b[p + 1] << 8) | b[p + 2])});
                p += mlen;
            } else if (status == 0xF0 || status == 0xF7) {  // sysex
                std::uint32_t mlen = read_vlq(b, p);
                p += mlen;
            } else {                        // channel voice
                const std::uint8_t hi = status & 0xF0;
                const std::uint8_t d1 = b.at(p++);
                const std::uint8_t d2 = (hi == 0xC0 || hi == 0xD0) ? 0 : b.at(p++);
                song.events_.push_back({tick, status, d1, d2});
                song.end_tick_ = std::max(song.end_tick_, tick);
            }
        }
        pos = end;
    }
    std::stable_sort(song.events_.begin(), song.events_.end(),
                     [](const MidiEvent& a, const MidiEvent& c) { return a.tick < c.tick; });
    std::stable_sort(song.tempo_.begin(), song.tempo_.end(),
                     [](const TempoChange& a, const TempoChange& c) { return a.tick < c.tick; });
    if (song.tempo_.empty() || song.tempo_.front().tick != 0)
        song.tempo_.insert(song.tempo_.begin(), {0, 500000});
    return song;
}

}  // namespace bumpy
```

- [ ] **Step 5: Wire CMake** — add `src/resources/midi_song.cpp` and `tests/cpp/midi_song_test.cpp`.

- [ ] **Step 6: Build and run** — `& build/windows-debug/Debug/bumpy_tests.exe "BUMPY.MID parses as a 7-track SMF"` → PASS.

- [ ] **Step 7: Commit**

```bash
git add src/resources/midi_song.h src/resources/midi_song.cpp tests/cpp/midi_song_test.cpp CMakeLists.txt
git commit -m "feat(audio): parse BUMPY.MID standard MIDI file"
```

---

## Task 3: `sfx_tables` — dump SFX presets + trigger/tile tables

**Files:**
- Create: `tools/re/dump_sfx.py`, `src/resources/sfx_tables.h`, `src/resources/sfx_tables.gen.cpp`
- Test: `tests/cpp/sfx_tables_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces (`sfx_tables.h`):
  - `enum class SweepKind { tone, noise };`
  - `struct SfxPreset { bool used; SweepKind kind; std::uint16_t init_divisor, steps, divisor_step, rate_seed, rate_count, rate_step; };`
  - `extern const SfxPreset kSfxPresets[0x16];  // index = 6e30 id (1..0x15); [0] and [0x13] unused`
  - `// per-tile speaker SFX maps (id = table[tile]); 0 = silent`
  - `extern const std::uint8_t kSfxIdleRest[0x30];   // DS:0x25de (6648)`
  - `extern const std::uint8_t kSfxRollBump[0x30];   // DS:0x263e (63be)`
  - `extern const std::uint8_t kSfxFallRoute[0x30];  // DS:0x269e (2810)`
  - `extern const std::uint8_t kSfxHeldBump[0x30];   // DS:0x26fe (647e)`
  - `extern const std::uint8_t kSfxLayerBBlock[0x20];// DS:0x274e (6a89)`
  - `extern const std::uint8_t kSfxPictureBlock[0x20];// DS:0x278e (640c)`

The generator (`dump_sfx.py`) reads `analysis/generated/BUMPY.UNPACKED.EXE` (code base file offset `0x1091 + off`; data base `0x11440 + off`). The `6e30` preset values are baked from the disassembly of `1000:6e30` (`switch` cases 1..0x15, each `9488`/`9502` call) — reproduce them as the literal table below (these are confirmed constants; the script emits them so the file stays regenerable and documents provenance). The six tile tables are copied from the data segment at the offsets above (`0x11440 + off`, 0x30 or 0x20 bytes).

Preset constants to emit (id → `{used, kind, init_divisor, steps, divisor_step, rate_seed, rate_count, rate_step}`; `divisor_step`/`rate_step` are stored as raw u16, i.e. two's-complement for negatives):

```
1  tone  1000 0x1e  10     0x1c2 1 1
2  tone  800  0x28  0xfff6 0x1c2 1 1
3  tone  440  400   0xffff 499   4 0xffff
4  tone  220  0x5a  0xffff 100   1 4
5  tone  1000 0x19  10     440   1 2
6  tone  1100 0x14  10     440   2 5
7  tone  1200 0x0f  10     440   1 3
8  tone  220  0x28  0xfffb 100   1 5
9  tone  50   0x1e  0x14   0x1c2 1 1
0xa tone 200  0x0f  0x32   0x15d 1 10
0xb noise 0    0x28  0x14  499   1 0xfffc   # 9502(2,0x28,0x14,499,1,0xfffc): steps=0x28 init=0x14
0xc tone  1200 0x1e  10    0x1a4 1 2
0xd tone  200  0x14  0x32  0x15d 2 0xf
0xe tone  10   0x32  4     200   10 0
0xf tone  300  400   2     100   2 1
0x10 tone 1200 0x1e  10    0x1a4 1 2
0x11 noise 0   0x28  0x14  499   1 0xfffc
0x12 noise 0   0x50  0x1e  499   2 0xfffc
0x13 tone  300  800   1     100   2 1        # UNUSED (used=false) but emit for completeness
0x14 tone  10   0x32  4     200   10 0
0x15 tone  600  0x1e  10    0x1c2 1 1
```

(For the noise presets, `init_divisor` holds the `9502` `init` arg and `steps` its `steps` arg; the tone fields map from `9488(2,steps,init_divisor,1,divisor_step,rate_seed,rate_count,rate_step)`.)

- [ ] **Step 1: Write `tools/re/dump_sfx.py`** — a `--cpp` emitter (mirror `tools/re/dump_player_dispatch.py`'s structure) that prints `sfx_tables.gen.cpp`: the literal `kSfxPresets` array above and the six tile tables read from the EXE at data offsets `0x25de/0x263e/0x269e/0x26fe/0x274e/0x278e` (`0x11440 + off`, sizes 0x30/0x30/0x30/0x30/0x20/0x20). Include the `used` flag = false for ids 0 and 0x13, true otherwise.

- [ ] **Step 2: Generate the file**

Run: `python tools/re/dump_sfx.py --cpp analysis/generated/BUMPY.UNPACKED.EXE > src/resources/sfx_tables.gen.cpp`
Expected: a `.gen.cpp` including `"resources/sfx_tables.h"`, in `namespace bumpy`, defining the seven arrays.

- [ ] **Step 3: Write `src/resources/sfx_tables.h`** — the declarations from the Interfaces block above (with the `SweepKind`/`SfxPreset` definitions and the `extern` arrays), plus a header comment citing `1000:6e30` and the tile-table offsets.

- [ ] **Step 4: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "resources/sfx_tables.h"

TEST_CASE("SFX preset table matches the recovered 6e30 switch") {
    using namespace bumpy;
    REQUIRE(kSfxPresets[1].used);
    REQUIRE(kSfxPresets[1].kind == SweepKind::tone);
    REQUIRE(kSfxPresets[1].init_divisor == 1000);
    REQUIRE(kSfxPresets[1].steps == 0x1e);
    REQUIRE(kSfxPresets[1].rate_seed == 0x1c2);
    REQUIRE(kSfxPresets[0x0b].kind == SweepKind::noise);
    REQUIRE_FALSE(kSfxPresets[0x13].used);        // dead case
    REQUIRE_FALSE(kSfxPresets[0].used);           // id 0 unused
    // Tile maps are the right size and silent-by-default fits (0 = no sound).
    REQUIRE(sizeof(kSfxIdleRest) == 0x30);
    REQUIRE(sizeof(kSfxLayerBBlock) == 0x20);
}
```

- [ ] **Step 5: Wire CMake** — add `src/resources/sfx_tables.gen.cpp` to `bumpy_core` and `tests/cpp/sfx_tables_test.cpp` to `bumpy_tests`.

- [ ] **Step 6: Build and run** — the test above → PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/re/dump_sfx.py src/resources/sfx_tables.h src/resources/sfx_tables.gen.cpp tests/cpp/sfx_tables_test.cpp CMakeLists.txt
git commit -m "feat(audio): dump SFX presets + tile trigger tables"
```

---

## Task 4: `opl2` — ymfm YM3812 wrapper

**Files:**
- Create: `src/audio/opl2.h`, `src/audio/opl2.cpp`
- Test: `tests/cpp/opl2_test.cpp`
- Modify: `CMakeLists.txt` (FetchContent ymfm; compile ymfm sources into `bumpy_core`; add sources + test)

**Interfaces:**
- Produces: `class Opl2 { public: Opl2(); void write(std::uint8_t reg, std::uint8_t value); float sample(); std::uint32_t sample_rate() const; void reset(); };` — `sample()` returns one mono sample in ~[-1,1]; `sample_rate()` returns the OPL2 native rate (49716).

- [ ] **Step 1: Wire ymfm into CMake.** In `CMakeLists.txt`, after the SDL/Catch2 `FetchContent_Declare`s add:

```cmake
FetchContent_Declare(ymfm
  GIT_REPOSITORY https://github.com/aaronsgiles/ymfm.git
  GIT_TAG e11d3fc7e56377acd34d90e6a89f9df1f4e79a1c)  # pin to a known-good commit
```

and add `ymfm` to the existing `FetchContent_MakeAvailable(...)`. Then add to the `bumpy_core` target (after `add_library`):

```cmake
target_sources(bumpy_core PRIVATE
  ${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_ssg.cpp
  src/audio/opl2.cpp)
target_include_directories(bumpy_core PRIVATE ${ymfm_SOURCE_DIR}/src)
```

- [ ] **Step 2: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/opl2.h"
#include <cmath>

TEST_CASE("Opl2 produces a tone after a minimal key-on") {
    bumpy::Opl2 opl;
    REQUIRE(opl.sample_rate() == 49716u);
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
```

- [ ] **Step 3: Run test to verify it fails** — build fails (header missing).

- [ ] **Step 4: Write `src/audio/opl2.h`**

```cpp
#pragma once
#include <cstdint>
#include <memory>

namespace bumpy {

// Thin wrapper over the ymfm YM3812 (OPL2) core. Register-level interface: write() sets a
// register, sample() advances the chip one output sample. Native rate = clock/72 = 49716 Hz.
class Opl2 {
public:
    Opl2();
    ~Opl2();
    Opl2(Opl2&&) noexcept;
    Opl2& operator=(Opl2&&) noexcept;

    void reset();
    void write(std::uint8_t reg, std::uint8_t value);
    float sample();
    [[nodiscard]] std::uint32_t sample_rate() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bumpy
```

- [ ] **Step 5: Write `src/audio/opl2.cpp`**

```cpp
#include "audio/opl2.h"
#include "ymfm_opl.h"

namespace bumpy {

struct Opl2::Impl : public ymfm::ymfm_interface {
    Impl() : chip(*this) { chip.reset(); }
    ymfm::ym3812 chip;
    static constexpr std::uint32_t kClock = 3579545;
};

Opl2::Opl2() : impl_(std::make_unique<Impl>()) {}
Opl2::~Opl2() = default;
Opl2::Opl2(Opl2&&) noexcept = default;
Opl2& Opl2::operator=(Opl2&&) noexcept = default;

void Opl2::reset() { impl_->chip.reset(); }

void Opl2::write(std::uint8_t reg, std::uint8_t value) {
    impl_->chip.write_address(reg);
    impl_->chip.write_data(value);
}

float Opl2::sample() {
    ymfm::ym3812::output_data out;
    impl_->chip.generate(&out, 1);
    return static_cast<float>(out.data[0]) / 32768.0f;
}

std::uint32_t Opl2::sample_rate() const { return impl_->chip.sample_rate(Impl::kClock); }

}  // namespace bumpy
```

- [ ] **Step 6: Wire the test** — add `tests/cpp/opl2_test.cpp` to `bumpy_tests`. Build and run → PASS. (If `sample_rate()` returns a value other than 49716, update the test/constant to the actual `chip.sample_rate(3579545)` result and the Global Constraint accordingly — ymfm computes it from the clock.)

- [ ] **Step 7: Commit**

```bash
git add src/audio/opl2.h src/audio/opl2.cpp tests/cpp/opl2_test.cpp CMakeLists.txt
git commit -m "feat(audio): vendor ymfm and add OPL2 (YM3812) wrapper"
```

---

## Task 5: `midi_opl_player` — MIDI→OPL2 driver

**Files:**
- Create: `src/audio/midi_opl_player.h`, `src/audio/midi_opl_player.cpp`
- Test: `tests/cpp/midi_opl_player_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MidiSong`, `AdLibBank`, `Opl2`, `AdLibInstrument`.
- Produces: `class MidiOplPlayer { public: MidiOplPlayer(const MidiSong& song, const AdLibBank& bank, bool loop); void render(float* out, std::size_t frames); bool finished() const; void reset(); };`

Design: `render` fills `out` with `frames` mono samples at the OPL2 rate. It advances an internal fractional tick clock derived from the active tempo (`samples_per_tick = sample_rate * usec_per_qn / (1e6 * division)`), dispatches all MIDI events due at each tick, and calls `opl_.sample()` per output frame. Voice allocation: 9 OPL2 melodic channels; on Note-On pick a free channel (else steal the oldest), load the instrument for the note's MIDI channel program into that OPL channel's registers, set F-number/block from the note, key-on; on Note-Off key-off the matching channel. Program→instrument: map MIDI Program Change to a bank instrument by index (`bank.instrument(program % bank.size())`) — the exact original mapping is refined in the by-ear task; default all channels to `bank.instrument(0)` until a Program Change arrives. Note→(block,fnum): standard OPL formula from an equal-tempered table (`fnum = 172.86 * 2^((note%12)/12)`, `block = note/12 - 1`, clamped).

Register loading for one OPL channel `ch` (operator base offsets `op_mod`, `op_car` from the standard OPL2 channel→slot map `{0,1,2,8,9,10,16,17,18}` for mod and `+3` for car):
- `0x20+op = (am<<7)|(vib<<6)|(eg<<5)|(ksr<<4)|(mult&0xf)`
- `0x40+op = (ksl<<6)|(level&0x3f)`
- `0x60+op = (attack<<4)|(decay&0xf)`
- `0x80+op = (sustain<<4)|(release&0xf)`
- `0xE0+op = wave&3`
- `0xC0+ch = (feedback<<1)|(connection&1)` (from the modulator operator fields)
- Key-on/off: `0xB0+ch = (keyon<<5)|(block<<2)|(fnum>>8)`, `0xA0+ch = fnum&0xff`.

- [ ] **Step 1: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/midi_opl_player.h"
#include "resources/midi_song.h"
#include "resources/adlib_bank.h"
#include <vector>

TEST_CASE("MidiOplPlayer renders audible output from BUMPY.MID") {
    const auto song = bumpy::MidiSong::load("BUMPY.MID");
    const auto bank = bumpy::AdLibBank::load("BUMPY.BNK");
    bumpy::MidiOplPlayer player(song, bank, /*loop=*/true);

    std::vector<float> buf(49716 * 3);  // ~3 seconds
    player.render(buf.data(), buf.size());
    double energy = 0.0;
    for (float s : buf) energy += double(s) * s;
    REQUIRE(energy > 0.0);              // the song produced sound
    REQUIRE_FALSE(player.finished());  // looping -> never finishes
}
```

- [ ] **Step 2: Run to verify it fails** — build fails (header missing).

- [ ] **Step 3: Write `src/audio/midi_opl_player.h`** — the class from Interfaces, holding `Opl2 opl_`, references/copies of the song events + tempo map + bank, a fractional-sample tick accumulator, an event cursor, per-OPL-channel voice state (`{active, midi_channel, note, age}`), and per-MIDI-channel current program.

- [ ] **Step 4: Write `src/audio/midi_opl_player.cpp`** — implement `render` per the design above (advance tick clock, dispatch due events through `note_on`/`note_off`/`program_change`, load instrument registers, key on/off; loop by resetting the cursor + tick clock + all-notes-off when the last event passes and `loop_` is set). Include the channel→operator-slot table and the note→fnum/block helper.

- [ ] **Step 5: Wire the test** — add sources + `tests/cpp/midi_opl_player_test.cpp` to CMake. Build and run → PASS.

- [ ] **Step 6: Add a WAV-dump dev tool for by-ear checking.** In `src/app/main.cpp` add a `--dump-music <BUMPY.MID> <BUMPY.BNK> <out.wav> [seconds]` flag that constructs the player, renders N seconds to a mono 49716 Hz WAV (write a minimal WAV header + int16 samples), and exits. (No SDL needed — this is offline render.)

- [ ] **Step 7: By-ear verification (fidelity gate).** Run `& build/windows-debug/Debug/bumpy_port.exe --dump-music BUMPY.MID BUMPY.BNK music.wav 20` and listen. It should play the intro tune with FM timbres (xylophone-like lead). Compare against a DOSBox-X capture of the intro. Note any wrong instruments as follow-ups (Program Change mapping refinement) — acceptable for the playable bar if the melody + tempo are right.

- [ ] **Step 8: Commit**

```bash
git add src/audio/midi_opl_player.h src/audio/midi_opl_player.cpp tests/cpp/midi_opl_player_test.cpp src/app/main.cpp CMakeLists.txt
git commit -m "feat(audio): MIDI-to-OPL2 player rendering BUMPY.MID via BUMPY.BNK"
```

---

## Task 6: `speaker_sfx` — PC-speaker sweep synth

**Files:**
- Create: `src/audio/speaker_sfx.h`, `src/audio/speaker_sfx.cpp`
- Test: `tests/cpp/speaker_sfx_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SfxPreset`, `SweepKind`, `kSfxPresets` (`resources/sfx_tables.h`).
- Produces: `class SpeakerVoice { public: void start(const SfxPreset& preset); void render_add(float* out, std::size_t frames); bool active() const; };` and a free function `float sfx_tone_hz(std::uint16_t divisor)` returning `1193182.0f / divisor`.

Design (transcribes handlers `0x9631`/`0x96c4`): one voice simulates the sweep ISR at the audio rate. State: `divisor`, `steps_left`, `isr_period` (starts `rate_seed`), `rate_left` (starts `rate_count`), `phase` (square-wave accumulator), plus an LFSR for noise. The ISR "tick" cadence is `samples_per_tick = SAMPLE_RATE * isr_period / kSfxIsrBaseHz` where **`kSfxIsrBaseHz` is a tuning constant** (start at 1'193'182 / 256 ≈ 4661; the true base is the open item — tune in Step 6). Each ISR tick: for a tone voice, `divisor += divisor_step`, `steps_left--` (end at 0); for a noise voice, reseed the frequency from the LFSR; and every `rate_count` ticks, `isr_period += rate_step`. Between ticks, generate a square wave at `sfx_tone_hz(divisor)` (or the noise frequency) into `out` (added, not overwritten), amplitude ~0.25. When `steps_left` hits 0 the voice goes inactive.

- [ ] **Step 1: Write the failing test**

```cpp
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
    std::vector<float> buf(49716);    // 1 second is far more than the sweep lasts
    for (float& s : buf) s = 0.0f;
    v.render_add(buf.data(), buf.size());
    double energy = 0.0;
    for (float s : buf) energy += double(s) * s;
    REQUIRE(energy > 0.0);            // it made sound
    REQUIRE_FALSE(v.active());        // and it finished within a second
}
```

- [ ] **Step 2: Run to verify it fails** — build fails (header missing).

- [ ] **Step 3: Write `src/audio/speaker_sfx.h`** — the `SpeakerVoice` class (voice state above) + `sfx_tone_hz`. Add `static constexpr std::uint32_t kSampleRate = 49716;` and `static constexpr double kSfxIsrBaseHz = 1193182.0 / 256.0;` with a comment marking it a tuning constant (the audio design spec "open item 1").

- [ ] **Step 4: Write `src/audio/speaker_sfx.cpp`** — implement `start`/`render_add`/`active` per the design (square-wave phase generator; LFSR reseed for `SweepKind::noise` using the `0x2345/0x4567/0xdb6d` constants from handler `0x95b5`; per-tick divisor/step/rate updates from handler `0x9631`).

- [ ] **Step 5: Wire the test** — add sources + test to CMake. Build and run → PASS.

- [ ] **Step 6: Tune `kSfxIsrBaseHz` by ear.** Extend the `--dump-music` tool (or add `--dump-sfx <preset_id> <out.wav>`) to render a single preset via `SpeakerVoice` to WAV. Render presets 1 (chirp), 0x0e (collect-ish), 0x0b (noise) and compare their pitch-contour + duration against a DOSBox-X capture of the same in-game events; adjust `kSfxIsrBaseHz` until durations match. Commit the tuned value with a one-line note on how it was measured.

- [ ] **Step 7: Commit**

```bash
git add src/audio/speaker_sfx.h src/audio/speaker_sfx.cpp tests/cpp/speaker_sfx_test.cpp src/app/main.cpp CMakeLists.txt
git commit -m "feat(audio): PC-speaker sweep-engine SFX synth"
```

---

## Task 7: `audio_engine` — mix music + SFX

**Files:**
- Create: `src/audio/audio_engine.h`, `src/audio/audio_engine.cpp`
- Test: `tests/cpp/audio_engine_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MidiSong`, `AdLibBank`, `MidiOplPlayer`, `SpeakerVoice`, `kSfxPresets`.
- Produces: `class AudioEngine { public: AudioEngine(const MidiSong& song, const AdLibBank& bank); void start_music(); void stop_music(); void play_sfx(std::uint8_t id); void render(float* out, std::size_t frames); static constexpr std::uint32_t kSampleRate = 49716; };`

Design: owns an optional music player (created on `start_music`, looping; destroyed/paused on `stop_music`) and a fixed pool (e.g. 6) of `SpeakerVoice`. `play_sfx(id)` looks up `kSfxPresets[id]`; if `used`, starts the preset on the oldest inactive voice (or steals). `render` zeroes `out`, adds the music (if playing) into it, then `render_add`s every active voice. Thread-safety: `play_sfx`/`start_music`/`stop_music` may be called from the game thread while `render` runs on the audio thread — guard the pool + music-active flag with a small lock or a lock-free command queue (use a `std::mutex` for the playable bar; the audio callback holds it only briefly).

- [ ] **Step 1: Write the failing test**

```cpp
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
```

- [ ] **Step 2: Run to verify it fails**; **Step 3–4:** write `audio_engine.h`/`.cpp` per the design; **Step 5:** wire CMake, build, run → PASS.

- [ ] **Step 6: Commit**

```bash
git add src/audio/audio_engine.h src/audio/audio_engine.cpp tests/cpp/audio_engine_test.cpp CMakeLists.txt
git commit -m "feat(audio): mixing audio engine (music + SFX pool)"
```

---

## Task 8: `sdl_audio` adapter + music on the splash screen

**Files:**
- Create: `src/platform_sdl3/sdl_audio.h`, `src/platform_sdl3/sdl_audio.cpp`
- Modify: `src/platform_sdl3/sdl_app.h`, `src/platform_sdl3/sdl_app.cpp`, `src/app/main.cpp`, `CMakeLists.txt` (add `sdl_audio.cpp` to `bumpy_platform_sdl3`)

**Interfaces:**
- Consumes: `AudioEngine`, SDL3 audio.
- Produces: `class SdlAudio { public: explicit SdlAudio(AudioEngine& engine); ~SdlAudio(); };` — opens an `SDL_AudioStream` (source spec: `SDL_AUDIO_F32`, 1 channel, 49716 Hz) with a `SDL_SetAudioStreamGetCallback` that pulls `engine.render` and feeds float samples; closes on destruction. No public methods beyond RAII.

- [ ] **Step 1: Write `src/platform_sdl3/sdl_audio.{h,cpp}`.** In the callback (`SDL_AudioStreamCallback`), compute `frames = additional_amount / sizeof(float)`, render into a scratch buffer via `engine.render`, and `SDL_PutAudioStreamData`. Open with `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this)` and `SDL_ResumeAudioStreamDevice`.

- [ ] **Step 2: Construct the engine + audio in `main.cpp`.** Load `BUMPY.MID` (`MidiSong`) and `BUMPY.BNK` (`AdLibBank`) once (world-independent, like the sprite bank/font), build an `AudioEngine`, and pass it into `SdlApp::run` (add an `AudioEngine&` parameter). Create the `SdlAudio` RAII object around the run loop.

- [ ] **Step 3: Start/stop music on the splash transition.** In `sdl_app.cpp`'s loop, track the previous screen; when entering `Screen::splash` call `engine.start_music()`, and when leaving it (to `menu`) call `engine.stop_music()`. (The splash is shown once at startup, so this fires the intro tune exactly as `FUN_1000_30dd` does — loop until keypress.)

- [ ] **Step 4: Manual verification.** `cmake --build --preset windows-debug` then `& build/windows-debug/Debug/bumpy_port.exe` — the intro splash should now play the looping FM music; pressing a key to enter the menu stops it. Menu/gameplay are silent (no music), matching the original.

- [ ] **Step 5: Verify originals unchanged** — `python tools/assets/manifest.py verify` → OK.

- [ ] **Step 6: Commit**

```bash
git add src/platform_sdl3/sdl_audio.h src/platform_sdl3/sdl_audio.cpp src/platform_sdl3/sdl_app.h src/platform_sdl3/sdl_app.cpp src/app/main.cpp CMakeLists.txt
git commit -m "feat(audio): SDL3 audio output; intro music on the splash screen"
```

---

## Task 9: wire in-game SFX triggers

**Files:**
- Modify: `src/game/level_game.h`, `src/game/level_game.cpp`, `src/game/world_map.h`, `src/game/world_map.cpp`, `src/platform_sdl3/sdl_app.cpp`
- Test: `tests/cpp/level_game_test.cpp` (add cases)

**Interfaces:**
- Produces (on `LevelGame`): `void emit_sfx(std::uint8_t id);` (private, pushes to `std::vector<std::uint8_t> pending_sfx_`), `std::vector<std::uint8_t> take_sfx_events();` (public, moves + clears the queue). Same pair on `WorldMap` for the cloud-jump launch.

Design: keep game logic SDL-free — the logic pushes the **speaker-profile** SFX id at each recovered `6e11` site; after each `tick()`/map update the SDL shell drains `take_sfx_events()` into `engine.play_sfx(id)`. Ids are the confirmed **speaker** column from the recovered inventory (the AdLib/MT-32 columns are not used in this profile).

Constant-id sites (add `emit_sfx(<id>)` in the port method named `f_<addr>` for each original address):

| Port method | id |
|---|---|
| `f_1e5e`, `f_1e90` (block-top land→walk) | `0x0f` |
| `f_1fbe`, `f_207d` (block-top hop-across) | `0x02` |
| `f_2138`, `f_21e7` (smash-down-through) | `0x15` |
| `f_23b6` (rolling DOWN bump) | `0x14` |
| `f_253f` (chute/deadly-pit step) | `0x14` |
| `f_25ad` (warp pop-out) | `0x03` |
| `f_2810` (fall-begin, constant path) | `0x14` |
| `f_28f9` (rest on open exit tile) | `0x03` |
| `f_29a6` (climb a picture block) | `0x14` |
| `f_42d9` (forced fall → 0x2d) | `0x03` |
| `f_4802` (enter hole/warp) | `0x03` |
| `f_50fb` (monster death) | `0x03` |
| `f_6305` (anim sfx-only) | `0x03` |
| `f_6326`, `f_6372` (spike death) | `0x03` |
| `f_645d` (anim sfx-only) | `0x0b` |
| `f_64c1` (anim sfx-only) | `0x0e` |
| `f_6587` (0x02-lane + DOWN) | `0x04` |
| `f_6748`, `f_6789` (hop-up entry) | `0x08` |
| `f_6c14` collect: portal-open branch (`a0cf==0`) | `0x0b` |
| `f_6c14` collect: ordinary-pickup branch | `0x0e` |
| `world_map` cloud-jump launch (`3cf7`) | `0x03` |

Variable-id sites (id = `table[index]`; skip if 0):

| Port method | table (from `sfx_tables.h`) | index |
|---|---|---|
| `f_6648` (idle-rest) | `kSfxIdleRest` | tile under ball (`DAT_7924`) |
| `f_63be` (roll/hop bump) | `kSfxRollBump` | tile (`DAT_7924`) |
| `f_2810` (fall routing) | `kSfxFallRoute` | fall lane (`DAT_7922`) |
| `f_647e` (held-bump in bounce) | `kSfxHeldBump` | plane-A of fallen-from cell (`DAT_79b9`) |
| `f_6a89` (layer-B block) | `kSfxLayerBBlock` | event id (`param_1`) |
| `f_640c` (picture block) | `kSfxPictureBlock` | plane-B value (`DAT_8551`) |

Reference: `analysis/specs/game-loop.md` and the audio design spec identify each site (each is currently a "sfx is a no-op / omitted" comment in `level_game.cpp`).

- [ ] **Step 1: Add the queue + accessor.** In `level_game.h` add `std::vector<std::uint8_t> pending_sfx_;`, `void emit_sfx(std::uint8_t id) { if (id) pending_sfx_.push_back(id); }` (private), and `std::vector<std::uint8_t> take_sfx_events();` (public, `return std::move(pending_sfx_);` then the moved-from vector is already empty). Include `<vector>`, `#include "resources/sfx_tables.h"` in `level_game.cpp`.

- [ ] **Step 2: Write the failing test** (a representative site — collect):

```cpp
// in level_game_test.cpp
TEST_CASE("collecting an item emits the pickup SFX id") {
    // Build board 0 of world 1 (has collectibles), run the ball onto a collectible,
    // and assert an emit. Reuse the test's existing board-setup helper.
    auto game = make_level_game(1, /*board=*/0);   // existing helper in this file
    // drive until a collect happens (existing helpers step with input); then:
    bool saw_pickup = false;
    for (int i = 0; i < 400 && !saw_pickup; ++i) {
        game.tick(/*input to roll into an item*/);
        for (std::uint8_t id : game.take_sfx_events())
            if (id == 0x0e || id == 0x0b) saw_pickup = true;
    }
    REQUIRE(saw_pickup);
}
```

(Adapt to the file's actual helpers; if a deterministic collect is awkward, instead assert that `f_6c14`'s ordinary branch emits `0x0e` via a direct unit call.)

- [ ] **Step 3: Run to verify it fails** — the test fails (no emit yet).

- [ ] **Step 4: Insert `emit_sfx` at every site** in the two tables above. For the ordinary collect branch in `f_6c14`, add `emit_sfx(0x0e);` (and `emit_sfx(0x0b);` in the portal-open branch). For variable sites, e.g. in `f_6648`: `emit_sfx(kSfxIdleRest[tile & 0x2f]);`.

- [ ] **Step 5: Run the test** → PASS. Run the whole suite `& build/windows-debug/Debug/bumpy_tests.exe` → all green.

- [ ] **Step 6: Drain events in the shell.** In `sdl_app.cpp`, after `game->tick(...)` add `for (std::uint8_t id : game->take_sfx_events()) engine.play_sfx(id);` and likewise after the world-map update for the cloud-jump. (The board-start pause already gates `tick()`, so no SFX fire while frozen.)

- [ ] **Step 7: Manual verification.** Launch, enter a board: collecting items, bumps, deaths, and the map cloud-jump should now beep with the recovered PC-speaker sounds. Spot-check a few against a DOSBox-X capture (faithful-by-ear).

- [ ] **Step 8: Verify originals + full suite**, then commit.

```bash
git add src/game/level_game.h src/game/level_game.cpp src/game/world_map.h src/game/world_map.cpp src/platform_sdl3/sdl_app.cpp tests/cpp/level_game_test.cpp
git commit -m "feat(audio): wire in-game PC-speaker SFX triggers"
```

---

## Self-Review

**Spec coverage:** music (BUMPY.MID + BUMPY.BNK → OPL2) = Tasks 1,2,4,5,8; SFX sweep engine (6e30 presets + handlers) = Tasks 3,6; trigger inventory (33 sites) = Task 9; audio_engine mixing = Task 7; SDL3 adapter + splash music = Task 8; game-logic sound-event boundary = Task 9. Device model / profile choice = design constants (AdLib profile: OPL2 music + speaker SFX), realized across Tasks 5/6/9. Open items (ISR base Hz, Program→instrument mapping) are explicit tuning steps (Tasks 5.7, 6.6), not silent gaps.

**Placeholder scan:** decoder/synth tasks carry full code; integration tasks (5.4, 6.4, 8.1, 9.4) describe concrete transcriptions with the exact register formulas, LFSR constants, and the full id table — no "handle the rest" left implicit.

**Type consistency:** `AdLibBank::instrument`/`by_name`, `MidiSong::events/tempo_map/division/end_tick`, `Opl2::write/sample/sample_rate`, `MidiOplPlayer::render/finished`, `SpeakerVoice::start/render_add/active`, `AudioEngine::start_music/stop_music/play_sfx/render`, and `LevelGame::emit_sfx/take_sfx_events` are named consistently across the tasks that consume them.

**Known follow-ups (out of scope, playable bar):** exact MIDI→OPL voice-allocation parity with the original driver (`905d`), the precise ISR base rate, and the compressed-sprite/other-profile paths remain refinements — recorded in the design spec's "Open items".
