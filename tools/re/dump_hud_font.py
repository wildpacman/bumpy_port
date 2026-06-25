#!/usr/bin/env python3
"""
dump_hud_font.py -- decode the BUMPY HUD bitmap font (DDFNT2.CAR) and dump
the '0'..'9' / space glyphs as ASCII-art + a raw byte table.

Provenance (recovered 2026-06-26, see analysis/specs/screen-flow.md "HUD score font"):

  * The 7-digit HUD score is drawn by FUN_1000_0816 -> FUN_1000_07f0 ->
    FUN_1000_9804 (FUN_1ab9_13ec), which rasterizes each char with the VGA
    glyph routine FUN_1ab9_1607 (dispatch DS:0x6952, video-mode index 1).
  * The active font is loaded by FUN_1000_0a07: FUN_1000_736f(4,4) opens
    LEVEL-table index 4 == "DDFNT2.CAR" (size 0x7c3) and reads it raw into a
    malloc'd buffer (FUN_1000_808e == DOS AH=48h alloc), then FUN_1000_97d5
    (FUN_1ab9_132b) makes it the active font (far ptr DAT_68a4:DAT_68a2).
  * FUN_1ab9_14d3 locates a glyph: idx = char - desc[0]; rec = desc_base +
    BE16(desc + 6 + idx*2).

Font descriptor (DDFNT2.CAR) header:
   [0] u8  first_char            (0x20 == ' ')
   [1] u8  last_char_exclusive   (0xff)
   [2] u8  baseline ascent       (7)  -> glyph top scanline = cursorY - desc[2]
   [3] u8  line metric           (8)  -> DAT_693e = desc[3] + 2
   [4] u8  inter-char spacing    (1)  -> x_advance = glyph_width + desc[4]
   [5] u8  (reserved / 0)
   [6..] BE16 offset table, (last-first) entries, offset is relative to base.

Per-glyph record:
   [0] u8  width  (pixels)
   [1] u8  height (rows)
   [2] u8  y-offset (rows below the glyph top; 0 for digits)
   [3..] bitmap, row-major, ceil(width/8) bytes per row, MSB-first.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FONT = os.path.join(ROOT, "DDFNT2.CAR")


def load_font(path=FONT):
    with open(path, "rb") as f:
        return f.read()


def header(d):
    return {
        "first": d[0],
        "last_excl": d[1],
        "ascent": d[2],   # desc[2]
        "metric": d[3],   # desc[3]
        "spacing": d[4],  # desc[4]
        "resv": d[5],
    }


def glyph_record_offset(d, ch):
    h = header(d)
    if not (h["first"] <= ch < h["last_excl"]):
        return None
    idx = ch - h["first"]
    e = 6 + idx * 2
    return (d[e] << 8) | d[e + 1]   # BE16, relative to descriptor base


def decode_glyph(d, ch):
    rec = glyph_record_offset(d, ch)
    if rec is None:
        return None
    w = d[rec]
    hgt = d[rec + 1]
    yoff = d[rec + 2]
    bpr = ((w - 1) // 8 + 1) if w > 0 else 0
    bmp = d[rec + 3: rec + 3 + bpr * hgt]
    rows = []
    for r in range(hgt):
        rowbytes = bmp[r * bpr:(r + 1) * bpr]
        bits = 0
        for b in rowbytes:
            bits = (bits << 8) | b
        # keep only the top `w` bits of the (bpr*8)-bit row, MSB-first
        line = []
        for c in range(w):
            bitpos = bpr * 8 - 1 - c
            line.append("#" if (bits >> bitpos) & 1 else ".")
        rows.append("".join(line))
    return {
        "char": ch, "rec": rec, "w": w, "h": hgt, "yoff": yoff,
        "bpr": bpr, "advance": w + header(d)["spacing"],
        "bytes": bmp.hex(), "rows": rows,
    }


def main():
    d = load_font()
    out = []

    def p(s=""):
        out.append(s)

    h = header(d)
    p("BUMPY HUD font: DDFNT2.CAR  (%d bytes)" % len(d))
    p("descriptor header: first=0x%02x(%r) last_excl=0x%02x ascent=%d metric=%d spacing=%d resv=%d"
      % (h["first"], chr(h["first"]), h["last_excl"], h["ascent"], h["metric"], h["spacing"], h["resv"]))
    p("glyph format: width x height bitmap, ceil(width/8) bytes/row, MSB-first.")
    p("x_advance = width + spacing(%d).  glyph top scanline = cursorY - ascent(%d)." % (h["spacing"], h["ascent"]))
    p("")

    chars = [0x20] + list(range(0x30, 0x3a))   # space then '0'..'9'
    for ch in chars:
        g = decode_glyph(d, ch)
        label = "SPACE" if ch == 0x20 else chr(ch)
        p("=== '%s' (0x%02x)  rec=0x%03x  w=%d h=%d yoff=%d advance=%d  bytes=%s ==="
          % (label, ch, g["rec"], g["w"], g["h"], g["yoff"], g["advance"], g["bytes"] or "(none)"))
        for row in g["rows"]:
            p("    " + row)
        if not g["rows"]:
            p("    (blank glyph)")
        p("")

    # combined strip "0123456789" so the digits can be eyeballed in a row
    p("=== rendered strip '0123456789' (rows of all digits side by side) ===")
    digits = [decode_glyph(d, c) for c in range(0x30, 0x3a)]
    maxh = max(g["h"] for g in digits)
    for r in range(maxh):
        line = []
        for g in digits:
            cell = g["rows"][r] if r < len(g["rows"]) else "." * g["w"]
            line.append(cell)
        p("  " + "  ".join(line))
    p("")

    text = "\n".join(out)
    print(text)
    dst = os.path.join(ROOT, "analysis", "generated", "hud_font_glyphs.txt")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        f.write(text + "\n")
    sys.stderr.write("\n[wrote %s]\n" % dst)


if __name__ == "__main__":
    main()
