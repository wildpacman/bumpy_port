#!/usr/bin/env python3
"""Build the Windows exe icon (bumpy.ico) from a BUMSPJEU sprite frame.

The sprite frame is produced by the port itself, faithfully decoded with the
in-game 16-colour board palette:

    bumpy_port.exe --render-sprite <level> <frame> frame.rgba

That dump is a little-endian int32 width, int32 height, then width*height RGBA
bytes (transparent sprite pixels are 0,0,0,0). This script pads the frame to a
square, upscales it nearest-neighbour (crisp pixels -- no blur) to each icon
size, and writes a multi-resolution .ico with one PNG-encoded image per size.

Usage:
    python tools/re/build_icon.py frame.rgba src/app/bumpy.ico
"""

import io
import struct
import sys

from PIL import Image

# Even multiples of the 16px master so every layer stays pixel-crisp.
ICON_SIZES = [16, 32, 48, 64, 128, 256]


def load_rgba(path):
    with open(path, "rb") as handle:
        width, height = struct.unpack("<ii", handle.read(8))
        data = handle.read()
    return Image.frombytes("RGBA", (width, height), data)


def to_square(image):
    """Trim transparent border, then centre on a transparent square canvas."""
    bbox = image.getbbox()
    content = image.crop(bbox) if bbox else image
    side = max(content.width, content.height)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.alpha_composite(content, ((side - content.width) // 2, (side - content.height) // 2))
    return canvas


def build_ico(master, sizes):
    """Assemble a PNG-entry .ico by hand so each layer is a nearest upscale."""
    images = []
    for size in sizes:
        layer = master.resize((size, size), Image.NEAREST)
        buffer = io.BytesIO()
        layer.save(buffer, format="PNG")
        images.append(buffer.getvalue())

    header = struct.pack("<HHH", 0, 1, len(images))  # reserved, type=icon, count
    offset = len(header) + 16 * len(images)
    entries = bytearray()
    for size, png in zip(sizes, images):
        entries += struct.pack(
            "<BBBBHHII",
            size & 0xFF,  # width (0 == 256)
            size & 0xFF,  # height (0 == 256)
            0,            # palette size (none)
            0,            # reserved
            1,            # colour planes
            32,           # bits per pixel
            len(png),
            offset,
        )
        offset += len(png)
    return header + bytes(entries) + b"".join(images)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    master = to_square(load_rgba(argv[1]))
    with open(argv[2], "wb") as handle:
        handle.write(build_ico(master, ICON_SIZES))
    print(f"wrote {argv[2]} ({master.width}x{master.height} master -> sizes {ICON_SIZES})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
