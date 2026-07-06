# Bumpy Port — Audio / Sound System Design

Status: approved design (2026-07-06). Target: **playable port**, faithful by ear,
not preservation-grade. Evidence levels per the project ladder
(Structural / Hypothesis / Confirmed). Code addresses are `segment:offset`; the
code segment is `0x1000` (raw file offset = `0x1091 + off`), the data segment is
`0x103b` (Ghidra `DAT_203b_*`; raw file offset = `0x11440 + off`). The SFX synth
engine, the software timer scheduler, and the sound-device model below were
recovered by spot-disassembling `analysis/generated/BUMPY.UNPACKED.EXE` with
capstone — **Ghidra fails on the timer ISR and the sweep step-handlers** (they are
reached only through an installed interrupt vector / a function pointer).

## Goal

Give the port sound: the intro/credits **music** and all in-game/menu **sound
effects**, matching the original's *AdLib* configuration. Today the port has **no
audio at all** (`src/audio` does not exist; every in-logic sound trigger is
stubbed). Game logic stays SDL-independent, integer-deterministic, and
refresh-rate-independent; SDL3 remains a thin platform adapter.

## Recovered sound model (Confirmed)

The DOS setup screen (`FUN_202c_0000`, overlay `202c:0000`) writes the sound-device
type into `DAT_203b_689c` (`ram0x00026c4c`, linear `0x26c4c`). Exactly four values,
from the menu strings at file `0x17c9a` (`"< F5 >: NO PITA / < F6 >: PC BASE /
< F7 >: ADLIB / < F8 >: MT32"`):

| `689c` | Menu | Meaning |
|---|---|---|
| `0x8000` (−0x8000) | NO PITA | sound disabled |
| `0` | PC BASE | PC speaker |
| `1` | ADLIB | AdLib / OPL2 FM |
| `4` | MT32 | Roland MT-32 via MPU-401 (MIDI) |

**Correction to earlier project notes:** `689c == 4` is **MT-32 (MPU-401)**, *not*
AdLib. AdLib is value `1`. The `game-loop.md` note that "`DAT_689c==4` selects the
AdLib table `0x27ae`" is wrong: that branch (`FUN_1000_6e30`, `1000:8524`) sends a
**3-byte MIDI drum Note-On (`0x99`) to the MPU-401**, so `0x27ae` is the **MT-32
percussion-SFX** note/velocity table, not AdLib registers. This spec supersedes
that note (fix pending in `game-loop.md`).

At startup `FUN_1000_6de3` validates the chosen device against detected hardware
(`FUN_1000_88e5` builds a mask: MPU via `FUN_1000_8a75` polling `0x331`/`0x330`;
AdLib via `FUN_1000_8fb6`/`8b2a`, the OPL2 timer test on `0x388`) and reduces it to
the music-driver selector `DAT_1000_85b3`. There is **no Sound Blaster / digital
path** anywhere.

### The three real hardware profiles

| Profile | Music | SFX |
|---|---|---|
| PC speaker (`0`) | speaker MIDI backend (`FUN_1000_9115`/`91df`) | speaker sweep engine |
| **AdLib (`1`)** | **OPL2 FM** (`BUMPY.MID` + `BUMPY.BNK`) | **speaker sweep engine** |
| MT-32 (`4`) | MIDI to MPU-401 | MIDI drum notes |

The key structural fact: the SFX sweep engine writes **only** PIT channel 2
(`out 0x42`) and the speaker gate (`0x61`) — never OPL. So **SFX are always
PC-speaker except under MT-32**; there is no "AdLib SFX". The authentic 1993 AdLib
experience is therefore **FM music + PC-speaker beeper SFX**.

**This port targets the AdLib profile.** (Out of scope, deferred: PC-speaker MIDI
music, MT-32, a runtime device switcher, and the scrolling-credits animation.)

## Music (intro/credits only)

`FUN_1000_30dd` (the port already renders its screen as `Screen::splash`, from
`FUN_1000_2fac`) loads resource 4 (the `BUMPRESE.VEC` credits image) and resource 5
(`BUMPY.MID`) into `a0c6:a0c8`, then — if sound is not disabled — loops
`FUN_1000_8977(song, …, loop=1)` while scrolling the credits, until a key is
pressed (`FUN_1000_89a8` stops it). Menu and gameplay start no music.

- **`BUMPY.MID`** (35 141 bytes) — a valid **Standard MIDI File, format 1, 7 tracks,
  division 192 ticks/quarter**, opening tempo `FF 51 03 0C 35 00` = 800 000 µs/qn
  (75 BPM), time-sig 4/4. Track 2 begins with an instrument text meta `"xylo"`.
  Reading/sequencing this is a **standard format — the original sequencer is not
  ported**.
- **`BUMPY.BNK`** (6 748 bytes) — a classic **AdLib `.BNK` instrument bank**
  (`01 00 "ADLIB-"` header, patch names `rol000`, `rol001`, …). Holds the OPL2 FM
  patch registers (modulator + carrier operator params, feedback/connection) the
  game used to render the MIDI on an AdLib card. This bank **defines the authentic
  timbres**.
- **Original OPL2 MIDI driver** (for the voice-allocation/patch-mapping reference):
  dispatch `FUN_1000_85b5`; note engine `FUN_1000_905d`/`8bc8`/`8eeb`/`8e58`; OPL
  register write `FUN_1000_9007` (ports `0x388`/`0x389`); note→block/F-number tables
  at `DS:0x5593`, `0x559c`, `0x55b4`, `0x5614`. These are the faithful mapping we
  reproduce; Ghidra's register/param reconstruction here is garbled, so the exact
  byte offsets are verified against raw bytes during implementation.

## SFX (gameplay + menu) — the PC-speaker sweep engine (Confirmed)

`FUN_1000_6e11(id)` → `FUN_1000_6e30(id)`. Under the AdLib/PC-speaker profile,
`6e30`'s `switch(id)` (cases `1..0x15`) loads a preset and arms one of three
timer-ISR **step handlers** via `FUN_1000_9488` / `9502` / `956d`. Each handler runs
off a hooked timer interrupt and drives the speaker directly.

### Software timer scheduler (Confirmed, capstone)

`FUN_1000_7cde` hooks the timer vector (saved at `DS:0x54d0`) and reprograms the PIT
via `FUN_1000_7f9a` (`out 0x43,0x36; out 0x40,lo; out 0x40,hi`). A slot table at
`DS:0x549c` (`{period, counter, seg, handler_off}`, indexed `slot*8`) holds the
active sound's re-arm period; `FUN_1000_7df9`→`7e62`→`7e61` install a slot,
`FUN_1000_7e1e` clears it. The speaker gate is toggled by `FUN_1000_9440`/`9451`
(read `0x61`, set bits 0–1) and silenced by `FUN_1000_9450` (`and al,0xfc`).

### The three step-handlers (Confirmed, capstone)

- **`0x9631` — swept tone** (armed by `FUN_1000_9488`). Per tick: decrement the
  inner-duration counter; on zero decrement the outer step counter (`DAT_9788`,
  ends the sound at zero → restore timer + speaker off) and advance the tone —
  `DAT_978a` (PIT ch-2 divisor) `+= DAT_978e` (signed step), then reprogram ch 2
  (`out 0x43,0xb6; out 0x42,lo/hi`). A second accumulator (`DAT_9790 += DAT_9794`
  every `DAT_9792` ticks) **re-arms the ISR period itself**, gliding the sweep
  rate. `freq = 1193182 / divisor`.
- **`0x96c4` — noise + rate-glide** (armed by `FUN_1000_9502`). Every 16 ticks an
  LFSR (`xor/add 0x2345 / xor / and 0xdb6d`, then `+0x4567`) reseeds ch 2 from
  `DAT_978a` → pseudo-random period = noise; plus the same secondary rate-glide and
  the `DAT_9788` outer counter for a fixed-length burst. Used by presets
  `0xb/0x11/0x12`.
- **`0x95b5` — pure noise** (armed by `FUN_1000_956d`). The LFSR reseed only, with a
  speaker-bit toggle (`rol DAT_979b`). (Caller outside the `6e30` presets; retained
  for completeness.)

### Preset & trigger inventory (Confirmed)

`6e30` presets `1..0x12, 0x14, 0x15` are all reachable (**only `0x13` is dead**);
each is 6 integers `{init_divisor, steps, divisor_step, rate_seed, rate_count,
rate_step}` (e.g. preset 1 = `{1000, 0x1e, 10, 0x1c2, 1, 1}` — a rising chirp;
presets `0xb/0x11/0x12` route to the noise handler). **33 call sites** trigger SFX;
27 use a constant id, 6 read the id from per-tile / per-block map tables (idle-rest
`6648`, roll/hop-bump `63be`, fall-routing `2810`, held-bump `647e`, layer-B block
`6a89`, picture-block `640c`) — each is a 48- or 32-byte table (`DS:0x25de`,
`0x263e`, `0x269e`, `0x26fe`, `0x274e`, `0x278e` for the speaker column). Events
include: collect (2 variants, `6c14`), monster death (`50fb`) and spike death
(`6326`/`6372`), cloud-jump launch (`3cf7`), block-top land/hop/smash
(`1e5e`/`1e90`/`1fbe`/`207d`/`2138`/`21e7`), warp/chute/fall, and hop entries. All
are already the points where the port's game logic currently drops sound.

**Open item (implementation):** the absolute ISR tick rate (Hz) sets the sweep
tempo and duration. `freq = 1193182/divisor` is exact and rate-independent; only
step *duration* depends on the tick rate. Pin it from the scheduler's PIT-reload
math and cross-check against a DOSBox-X capture (the project's existing reference
harness).

## Port architecture

New `src/audio` module plus a `resources` decoder trio and a `platform_sdl3`
adapter, following the project's existing layer boundaries. Everything except the
SDL adapter is pure, integer/float-deterministic, and unit-testable on the real
files.

### `resources` (pure decoders, no SDL)

- **`midi_song`** — parse `BUMPY.MID` (SMF fmt 1, ≤16 tracks): merged event stream
  with absolute ticks + a tempo map. Standard SMF; not ported from the binary.
- **`adlib_bank`** — parse `BUMPY.BNK` (`"ADLIB-"`): named instruments → OPL2
  operator/channel register sets.
- **`sfx_data`** — the recovered SFX tables baked to a `.gen.cpp` by a new
  `tools/re/dump_sfx.py`: the `6e30` presets (1..0x15), the six tile/block map
  tables, and the event→id trigger map. Source of truth = the binary, matching the
  project's other `dump_*` generators.

### `audio` (pure synthesis / sequencing, no SDL)

- **`opl2`** — thin wrapper over the vendored **ymfm** YM3812 core (BSD-3-Clause;
  under `third_party/ymfm`). Register writes → PCM at the engine's output rate.
- **`midi_opl_player`** — the MIDI→OPL2 driver: steps `midi_song` by tempo, owns the
  9 OPL2 channels, binds `adlib_bank` patches per MIDI program/channel following the
  original driver's mapping, emits note-on/off (block/F-number). Loops. The partial
  port of the original OPL driver — the heart of music fidelity.
- **`speaker_sfx`** — the ported sweep engine: the `0x9631`/`0x96c4`/`0x95b5`
  handler math + the scheduler tick, run from `sfx_data` presets, producing 1-bit
  speaker output low-pass-filtered to PCM. One-shot polyphonic voices.
- **`audio_engine`** — owns the music player and a small SFX-voice pool; API
  `start_music()`, `stop_music()`, `play_sfx(id)`, `render(buffer, frames)`. Pure
  and testable (feed a frame count, get PCM).

### `platform_sdl3`

- **`sdl_audio`** — opens an SDL3 audio stream (mono float32 at 49715 Hz; SDL
  resamples to the device) and pulls `audio_engine.render` in the stream
  callback. Bridges game sound-events → `play_sfx` and `Screen::splash`
  enter/leave → `start_music`/`stop_music`.

### Game integration (the boundary that keeps logic pure)

`LevelGame` (and other logic that triggers sound) **accumulates a list of
sound-event ids per `tick()`** instead of calling SDL. After each tick the SDL
shell drains the list into `audio_engine.play_sfx(id)`. This keeps game logic
deterministic and testable, and mirrors the original's inline `6e11(id)` calls
exactly. `App`/`sdl_app` start/stop the music on the `splash`↔`menu` transition.

## Testing (playable-port rigor — no bit-exact gates)

- **Decoders on the real files:** `midi_song` (7 tracks, 192 tpqn, 75 BPM, track
  names incl. "xylo", event/EOT counts) and `adlib_bank` (instrument count, a known
  patch's register bytes).
- **Sweep engine:** deterministic PCM sanity per preset (e.g. preset 1 sweeps
  upward across the expected frequency band for the expected duration; the noise
  presets produce broadband output).
- **By ear / by capture (the fidelity bar):** render `BUMPY.MID` through
  `opl2`+`adlib_bank` to a WAV and listen; compare a few SFX against a DOSBox-X
  capture. Faithful-by-ear, not sample-exact.
- Originals stay read-only (`python tools/assets/manifest.py verify`); no generated
  audio is committed; `third_party/ymfm` is tracked source we compile.

## Open items carried into implementation

1. Absolute ISR tick rate for the sweep engine (pin from scheduler + DOSBox).
2. The MIDI→OPL2 voice allocation / per-channel patch selection details in
   `FUN_1000_905d`/`8bc8` (verify against raw bytes; Ghidra garbles them).
3. ymfm vendoring mechanism (submodule vs copied source) and the CMake wiring.

## Post-implementation recovery (2026-07-07) — items 1 & 2 resolved (Confirmed, capstone)

The first playable audio pass shipped placeholders for the two RE unknowns above;
both were then recovered exactly from `BUMPY.UNPACKED.EXE` and the port corrected.

**SFX tick rate (item 1).** Timer-0 is programmed ONCE (`FUN_1000_7db1` loads
reload `0x951 = 2385`), so the base ISR tick is a FIXED `1193182/2385 = 500.286 Hz`
— not a per-sound PIT reprogram. The active sound is advanced by a Bresenham/DDS
divider in the ISR (`FUN_1000_7c02`): each tick `acc += rate_seed`; when
`acc >= 500` it fires the step handler and subtracts 500. So `rate_seed` is a DDS
*increment* (bigger ⇒ faster), the inverse of a divisor; and re-installing the slot
on a glide zeroes `acc` (`FUN_1000_7e61`), which ~doubles the length of
`rate_count==1` presets. Real durations: preset `0x01`≈120 ms, `0x02`≈160 ms,
`0x03`≈999 ms (the placeholder `kSfxIsrBaseHz=1193182/64` made these 6–10× too
long — the never-ending pit whoosh). The 6e30 switch pushes **7** payload fields;
the 7th (inner tone-step divider `E`) is `1` for every preset, so the 6-field
`SfxPreset` stays valid. Noise (`0x96c4`) is NOT a swept square: ch2 is held at a
constant divisor and the audible signal is a 16-bit shift register (`L1 = rotl(L1,1)`
each fire; reseed `L1=(L1+0x2345)^L3; L3=L1+0x4567` every 16 fires) clocked one bit
per fire to the speaker-data line. The `&0xdb6d` mask belongs only to the unused
`0x95b5` reseed-only handler.

**Music patch selection & FM (item 2).** The driver (`FUN_1000_8b81`) maps MIDI
program → the `.BNK` **name index** (a program-ordered 12-byte table whose record N,
"rolNNN", holds the storage slot of program N's patch) → the 30-byte patch. Indexing
the raw instrument array by program number is wrong (storage is scrambled). Channels
0–5 default to program `channel+1` (`FUN_1000_8b45`) before the song plays. The
per-patch register packing (`FUN_1000_8bc8`) writes reg `0xC0` with the connection
bit **INVERTED** (`(patch&1)^1`); the bank stores `1` for its FM patches, so writing
it straight leaves every voice additive (two summed sines) = the thin "calculator"
timbre. It also computes KSL as `(byte>>2)&0xC0` (≈0 for the small BNK values, i.e.
key-scaling-of-level effectively off). **WSE is a trap:** reg `0x01` is written `0`
and never enabled, so a real YM3812/DOSBox ignores the per-operator waveform selects
and plays pure sine — enabling WSE in the port would DIVERGE from DOSBox. The
richness is 2-op FM + feedback + the inverted connection bit, not waveforms.

Deferred (not needed for faithful-by-ear): the exact velocity→carrier-TL curve in
`905d`, the real note→fnum/block tables (`DS:0x559c/55b4/5614`; the synthesized
equal-tempered table is within ±1 fnum), and the original's fixed 1-voice-per-channel
(mono) allocation vs the port's 9-voice polyphony.

## Key addresses (reference)

- Device model: setup `202c:0000`; var `DAT_203b_689c`; validate `1000:6de3` →
  `88e5`/`891e`; selector `DAT_1000_85b3`.
- SFX: `6e11`/`6e30`; arm `9488`/`9502`/`956d`; handlers `0x9631`/`0x96c4`/`0x95b5`;
  scheduler `7cde`/`7df9`/`7e62`/`7e61`/`7e1e`/`7f9a`; speaker `9440`/`9451`/`9450`;
  presets in `6e30`; map tables `DS:0x25de/263e/269e/26fe/274e/278e` (speaker).
- Music: intro `30dd` (port `Screen::splash`); start/stop `8977`/`8999`/`89a8`; SMF
  player `8809`/`873c`/`8891`; OPL driver `905d`/`8bc8`/`8eeb`/`9007` (0x388/0x389);
  note tables `DS:0x5593/559c/55b4/5614`.
- Assets: `BUMPY.MID` (resource 5), `BUMPY.BNK`, `BUMPRESE.VEC` (resource 4).
