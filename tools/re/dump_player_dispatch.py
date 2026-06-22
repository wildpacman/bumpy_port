#!/usr/bin/env python3
"""Dump BUMPY's two player state-machine dispatch tables, resolving each near
pointer to its FUN_1000_xxxx handler.

The in-level player tick (FUN_1000_1d26) runs one of two dispatch tables each
frame (see analysis/specs/game-loop.md):

  * DS:0x7ca  -- the DECIDE table, indexed by the player state byte (DAT_792c).
                FUN_1000_1e02 calls it when no scripted move is in progress
                (DAT_824d == 0): pick the next action from the tile + input.
  * DS:0x43c0 -- the ANIMATION-STEP table, a 2-D table indexed by
                state*0x22 + step*2 (step = DAT_792a). FUN_1000_238e calls it
                while a scripted move plays (DAT_824d != 0): per-step micro-ops
                (advance the ball's cell, re-read input to chain, sfx, ...).

Both tables hold 2-byte near offsets into code segment 0x1000, so a value V is
FUN_1000_<V>. Table offsets resolve in BUMPY.UNPACKED.EXE at 0x11440 + off.

Usage:
    python tools/re/dump_player_dispatch.py [BUMPY.UNPACKED.EXE] [all_functions.c]
"""
import re
import sys

DATA = 0x11440
DECIDE = 0x7ca
ANIM = 0x43c0
ANIM_ROW = 0x22   # bytes per state row (17 word slots)
ANIM_STEPS = 0x11
STATES = 0x41

# The per-step micro-ops whose mechanics are confirmed by reading the decomp.
LEGEND = {
    0x7111: "nop(render frame)",
    0x64e2: "cell-=8 UP", 0x64ff: "cell+=8 DOWN",
    0x651c: "cell-=1 LEFT", 0x6535: "cell+=1 RIGHT",
    0x6611: "mask 8244&=0x0f", 0x65e5: "mask 8244&=0x10", 0x65fb: "mask 8244&=0x1d",
    0x6717: "collect-adjacent(6d26)", 0x654e: "latch held-bump", 0x6587: "lane-0x02+RIGHT",
    0x4437: "input-tree(fire/L/R/U/D)", 0x4344: "input-tree(no-fire)",
}


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else r"analysis/generated/BUMPY.UNPACKED.EXE"
    src = sys.argv[2] if len(sys.argv) > 2 else r"analysis/generated/decomp/all_functions.c"
    f = open(exe, "rb").read()
    known = set(re.findall(r"FUN_1000_([0-9a-f]+)", open(src).read()))

    def u16(off):
        a = DATA + off
        return f[a] | (f[a + 1] << 8)

    def name(p):
        h = f"{p:04x}"
        tag = "" if (h in known or p == 0x7111) else "?"
        return f"{h}{tag}"

    print("=== DS:0x7ca  decide table (state -> handler) ===")
    for st in range(STATES):
        print(f"  {st:#04x} -> FUN_1000_{name(u16(DECIDE + st * 2))}")

    print("\n=== DS:0x43c0  animation-step table (state: step0 step1 ...) ===")
    print("legend: " + ", ".join(f"{v:#x}={d}" for v, d in LEGEND.items()))
    for st in range(STATES):
        row = [u16(ANIM + st * ANIM_ROW + s * 2) for s in range(ANIM_STEPS)]
        if all(v == 0 for v in row):
            continue
        cells = " ".join(name(v) for v in row)
        print(f"  {st:#04x}: {cells}")


if __name__ == "__main__":
    main()
