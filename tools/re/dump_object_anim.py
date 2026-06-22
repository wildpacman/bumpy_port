#!/usr/bin/env python3
"""Dump BUMPY's tile bump/spring animation tables (the playfield "objects react"
behaviour) from BUMPY.UNPACKED.EXE.

When the ball bumps a peg/block the per-step handlers in the DS:0x43c0 dispatch
call FUN_1000_69aa (3 layer-A peg slots) or FUN_1000_6a89 (4 layer-B block
slots). Each call (see analysis/specs/game-loop.md):

  * looks up a descriptor far-pointer table -- layer A at DS:0x2ede/0x2ee0,
    layer B at DS:0x3256/0x3258 -- indexed by an event id;
  * writes the descriptor's first byte (the "pressed"/settled tile value) into
    the live grid (plane A at cell, plane B at cell+0x30);
  * arms an animation slot with the descriptor's frame-byte stream pointer.

Each frame FUN_1000_14e4 / FUN_1000_15a1 reads one stream byte (0xff = end,
0x00 = hold the previous frame). A non-zero byte is a *sprite index* into the
same per-layer record table the static draw uses -- layer A at DS:0x37be, layer
B at DS:0x3ad2 -- giving {y_offset (the record's "count" word), frame_index}.
Layer-B frame indices are submitted with +0xf1 (FUN_1000_17c7), which is the
bank region the static path also needs.

Which event id fires is chosen by:
  * layer-B neighbour bump: eight 32-byte tables DS:0x35be..0x369e indexed by the
    bumped cell's plane-B value (DAT_8551);
  * layer-A rest/idle spring: DS:0x3d0a indexed by the tile under the ball
    (DAT_7924); held-bump: DS:0x3cda (same value also drives the forced fall);
  * a few direct ids in the decide handlers (0x24 chute, 0x27 warp, 0x34 lane,
    0x59/0x5a level-clear door).

Table offsets resolve in BUMPY.UNPACKED.EXE at 0x11440 + off (DS = seg 0x103b).

Usage:
    python tools/re/dump_object_anim.py [BUMPY.UNPACKED.EXE]          # human dump
    python tools/re/dump_object_anim.py --cpp [exe] > src/game/object_anim.gen.cpp
"""
import sys

DATA = 0x11440
DS_SEG = 0x103b

# descriptor far-pointer tables (off table / seg table) and record tables.
DESC_A_OFF, DESC_A_SEG = 0x2ede, 0x2ee0
DESC_B_OFF, DESC_B_SEG = 0x3256, 0x3258
REC_A = 0x37be          # {count(=y_offset), frame_index} 4-byte records, idx from 1
REC_B = 0x3ad2
LAYER_B_FRAME_BIAS = 0xf1
HIDDEN_BIT = 0x200      # frame word bit: skip drawing this step (FUN_1000_165e/17c7)
HIDDEN_FRAME = 0xffff   # AnimRecord.frame_index sentinel for a hidden step

# event-selection tables.
SEL_B = [0x35be, 0x35de, 0x35fe, 0x361e, 0x363e, 0x365e, 0x367e, 0x369e]
SEL_B_LABELS = ["6699_L", "66d8_R", "6748_L", "6789_R", "67e2_L", "6813_R", "68fe_L", "693a_R"]
IDLE_SPRING_A = 0x3d0a   # tile under ball -> layer-A spring id (FUN_1000_6987)
HELD_BUMP_A = 0x3cda     # tile under ball -> held-bump id (FUN_1000_695e; also the 69aa id)
ROLL_SPRING_L = 0x3c7a   # tile under ball -> spring id when a roll-LEFT starts (6699 -> 6d6a)
ROLL_SPRING_R = 0x3caa   # tile under ball -> spring id when a roll-RIGHT starts (66d8 -> 6d6a)

A_EVENT_COUNT = 0x60    # ids 0..0x5f
B_EVENT_COUNT = 0x19    # ids 0..0x18
SEL_WIDTH = 0x20        # 32 plane-B values

MAX_STREAM = 64         # streams are short; guard against a missing terminator


def _u16(f, off):
    a = DATA + off
    return f[a] | (f[a + 1] << 8)


def _record(f, base, idx, bias):
    """Resolve record idx (1-based) to (frame_index, y_offset). A frame word with
    the 0x200 bit is a hidden step -> HIDDEN_FRAME sentinel, no bias."""
    o = base + (idx - 1) * 4
    y = _u16(f, o)
    raw = _u16(f, o + 2)
    if raw & HIDDEN_BIT:
        return HIDDEN_FRAME, y
    return (raw + bias) & 0xffff, y


def _descriptor(f, off_tbl, seg_tbl, ev):
    """Return (new_tile, stream_off) or None when the slot is unused."""
    seg = _u16(f, seg_tbl + ev * 4)
    if seg != DS_SEG:
        return None
    off = _u16(f, off_tbl + ev * 4)
    new_tile = f[DATA + off]
    stream_off = _u16(f, off + 2)  # far ptr to the stream lives at descriptor +2..+5
    return new_tile, stream_off


def _stream(f, off):
    """The sprite-index byte stream up to and including the 0xff terminator."""
    out = []
    for i in range(MAX_STREAM):
        v = f[DATA + off + i]
        out.append(v)
        if v == 0xff:
            return out
    raise ValueError(f"stream at 0x{off:04x} not terminated within {MAX_STREAM} bytes")


def _max_sprite_index(f, events):
    hi = 0
    for ev in events:
        for v in ev["stream"]:
            if v not in (0x00, 0xff):
                hi = max(hi, v)
    return hi


def _events(f, off_tbl, seg_tbl, count):
    evs = []
    for ev in range(count):
        d = _descriptor(f, off_tbl, seg_tbl, ev)
        if d is None:
            evs.append(None)
            continue
        new_tile, soff = d
        evs.append({"new_tile": new_tile, "stream": _stream(f, soff)})
    return evs


def _records(f, base, count, bias):
    """List of (frame_index, y_offset), idx 1..count."""
    return [_record(f, base, i, bias) for i in range(1, count + 1)]


def collect(f):
    a_events = _events(f, DESC_A_OFF, DESC_A_SEG, A_EVENT_COUNT)
    b_events = _events(f, DESC_B_OFF, DESC_B_SEG, B_EVENT_COUNT)
    rec_a_count = _max_sprite_index(f, [e for e in a_events if e])
    rec_b_count = _max_sprite_index(f, [e for e in b_events if e])
    rec_a = _records(f, REC_A, rec_a_count, 0)
    rec_b = _records(f, REC_B, rec_b_count, LAYER_B_FRAME_BIAS)
    sel_b = [[f[DATA + base + i] for i in range(SEL_WIDTH)] for base in SEL_B]
    idle_a = [f[DATA + IDLE_SPRING_A + i] for i in range(0x30)]
    held_a = [f[DATA + HELD_BUMP_A + i] for i in range(0x30)]
    roll_l = [f[DATA + ROLL_SPRING_L + i] for i in range(0x30)]
    roll_r = [f[DATA + ROLL_SPRING_R + i] for i in range(0x30)]
    return {
        "a_events": a_events, "b_events": b_events,
        "rec_a": rec_a, "rec_b": rec_b,
        "sel_b": sel_b, "idle_a": idle_a, "held_a": held_a,
        "roll_l": roll_l, "roll_r": roll_r,
    }


def emit_human(d):
    def show_stream(rec, stream):
        parts = []
        for v in stream:
            if v == 0xff:
                parts.append("END")
            elif v == 0:
                parts.append("·hold")
            else:
                fr, yoff = rec[v - 1]
                parts.append("%d->hidden" % v if fr == HIDDEN_FRAME else f"{v}->f{fr:02x}/y{yoff}")
        return " ".join(parts)

    print("== layer-A records (sprite idx -> frame, y_offset) ==")
    for i, (fr, y) in enumerate(d["rec_a"], 1):
        print(f"  {i:3d}: frame={'hidden' if fr == HIDDEN_FRAME else f'0x{fr:02x}'} y_offset={y}")
    print("== layer-B records (sprite idx -> frame+0xf1, y_offset) ==")
    for i, (fr, y) in enumerate(d["rec_b"], 1):
        print(f"  {i:3d}: frame={'hidden' if fr == HIDDEN_FRAME else f'0x{fr:02x}'} y_offset={y}")
    print("\n== layer-A bump events ==")
    for ev, e in enumerate(d["a_events"]):
        if e:
            print(f"  id 0x{ev:02x}: settle=0x{e['new_tile']:02x}  {show_stream(d['rec_a'], e['stream'])}")
    print("\n== layer-B bump events ==")
    for ev, e in enumerate(d["b_events"]):
        if e:
            print(f"  id 0x{ev:02x}: settle=0x{e['new_tile']:02x}  {show_stream(d['rec_b'], e['stream'])}")
    print("\n== layer-B neighbour-bump selection (plane-B value -> event id) ==")
    for lbl, row in zip(SEL_B_LABELS, d["sel_b"]):
        print(f"  {lbl}: " + " ".join(f"{v:02x}" for v in row[:0x14]))
    print("== layer-A idle/rest spring (tile -> event id) ==")
    print("  " + " ".join(f"{v:02x}" for v in d["idle_a"]))
    print("== layer-A held-bump (tile -> event id) ==")
    print("  " + " ".join(f"{v:02x}" for v in d["held_a"]))


def emit_cpp(d):
    o = []
    p = o.append
    p("// Auto-generated by tools/re/dump_object_anim.py --cpp -- do not edit.")
    p("// Tile bump/spring animation tables from BUMPY.UNPACKED.EXE. See")
    p("// analysis/specs/game-loop.md and tools/re/dump_object_anim.py for the layout.")
    p('#include "game/object_anim.h"')
    p("")
    p("namespace bumpy {")
    p("")

    def rec_table(name, recs):
        p(f"// sprite index -> {{frame_index, y_offset}} ({len(recs)} records, idx 1-based;")
        p("// frame_index kAnimHiddenFrame = skip drawing this step).")
        p(f"const AnimRecord {name}[] = {{")
        p("    {0x0000, 0},  // idx 0 unused")
        for fr, y in recs:
            p(f"    {{0x{fr:04x}, {y}}},")
        p("};")
        p(f"const std::size_t {name}Count = sizeof({name}) / sizeof({name}[0]);")
        p("")

    rec_table("kAnimRecordA", d["rec_a"])
    rec_table("kAnimRecordB", d["rec_b"])

    # pooled streams (raw, including the 0xff terminator) + per-id {settle, off, len}.
    def event_tables(name, events):
        pool = []
        meta = []
        for e in events:
            if e is None:
                meta.append((0, 0, 0, False))
            else:
                off = len(pool)
                pool.extend(e["stream"])
                meta.append((e["new_tile"], off, len(e["stream"]), True))
        p(f"// {name} stream byte pool (sprite indices; 0x00 = hold, 0xff = end).")
        p(f"const std::uint8_t {name}Stream[] = {{")
        for i in range(0, len(pool), 16):
            p("    " + " ".join(f"0x{v:02x}," for v in pool[i:i + 16]))
        p("};")
        p(f"// {name} events: settle tile + stream slice, indexed by event id.")
        p(f"const BumpEvent {name}[] = {{")
        for tile, off, ln, present in meta:
            tag = "" if present else "  // unused"
            p(f"    {{0x{tile:02x}, {off}, {ln}}},{tag}")
        p("};")
        p(f"const std::size_t {name}Count = sizeof({name}) / sizeof({name}[0]);")
        p("")
        return pool

    event_tables("kBumpEventA", d["a_events"])
    event_tables("kBumpEventB", d["b_events"])

    p("// layer-B neighbour bump: plane-B value -> event id, one row per step handler.")
    p(f"const std::uint8_t kBumpSelectB[{len(SEL_B)}][{SEL_WIDTH}] = {{")
    for lbl, row in zip(SEL_B_LABELS, d["sel_b"]):
        cells = ", ".join(f"0x{v:02x}" for v in row)
        p(f"    {{{cells}}},  // {lbl}")
    p("};")
    p("")
    def tile_table(name, comment, vals):
        p(f"// {comment}")
        p(f"const std::uint8_t {name}[0x30] = {{")
        for i in range(0, 0x30, 16):
            p("    " + " ".join(f"0x{v:02x}," for v in vals[i:i + 16]))
        p("};")
        p("")

    tile_table("kIdleSpringA", "layer-A rest/idle spring: tile under the ball -> event id (FUN_1000_6987).",
               d["idle_a"])
    tile_table("kRollSpringL", "layer-A roll-LEFT entry spring: tile under the ball -> event id (6699 -> 6d6a).",
               d["roll_l"])
    tile_table("kRollSpringR", "layer-A roll-RIGHT entry spring: tile under the ball -> event id (66d8 -> 6d6a).",
               d["roll_r"])
    p("}  // namespace bumpy")
    p("")
    sys.stdout.write("\n".join(o))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    exe = args[0] if args else r"analysis/generated/BUMPY.UNPACKED.EXE"
    f = open(exe, "rb").read()
    d = collect(f)
    if "--cpp" in sys.argv:
        emit_cpp(d)
    else:
        emit_human(d)


if __name__ == "__main__":
    main()
