#!/usr/bin/env python3
"""Turn any picture into the sleep-screen image (Settings > Sleep > Sleep
screen > Image).

Writes a 1-bit "XT" bitmap — the reader's cover-thumb format
(src/reader/CoverThumb.h): 8-byte header (magic 0x5854 LE, version 1,
reserved 0, width LE, height LE), then MSB-first rows of ceil(w/8) bytes,
SET bit = black ink. Copy the output to the SD card root as /sleep.bin.

The firmware centers the image and clips it to the panel, so it is sized to
the logical portrait panel of the chosen device (X3 528x792, X4 480x800).

  pip install pillow
  python3 tools/make_sleep_image.py photo.jpg sleep.bin --preview sleep.png
  python3 tools/make_sleep_image.py art.png sleep.bin --fit contain --dither none
  python3 tools/make_sleep_image.py art.png sleep.bin --fit contain --pad black
  python3 tools/make_sleep_image.py photo.jpg src/art/SleepImage.h --header  # built-in default
"""

import argparse
import struct

from PIL import Image, ImageOps

PANELS = {"x3": (528, 792), "x4": (480, 800)}


def pad_color(img, pad):
    """Gray level for the --fit contain margins. auto = the median of the
    picture's outermost pixels, so a dark or tinted background carries on
    into the margins instead of meeting a white band. Chosen before
    --contrast/--invert, which then treat margins and picture alike."""
    if pad == "white":
        return 255
    if pad == "black":
        return 0
    if pad != "auto":
        return max(0, min(255, int(pad)))
    px = img.load()
    w, h = img.size
    edge = [px[x, y] for x in range(w) for y in (0, h - 1)]
    edge += [px[x, y] for y in range(h) for x in (0, w - 1)]
    edge.sort()
    return edge[len(edge) // 2]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image", help="source picture (jpg, png, ...)")
    ap.add_argument("out", help="output .bin (copy to SD root as sleep.bin)")
    ap.add_argument("--device", choices=PANELS, default="x3")
    ap.add_argument(
        "--fit",
        choices=["cover", "contain"],
        default="cover",
        help="cover: fill the panel and crop (default); contain: whole picture, margins filled per --pad",
    )
    ap.add_argument(
        "--pad",
        default="auto",
        help="--fit contain margin color: auto (match the picture's edges, default), white, black, or a gray level 0-255",
    )
    ap.add_argument(
        "--dither",
        choices=["floyd", "none"],
        default="floyd",
        help="floyd: error diffusion, good for photos (default); none: hard threshold, good for line art",
    )
    ap.add_argument("--threshold", type=int, default=128, help="gray level for --dither none (0-255)")
    ap.add_argument("--contrast", action="store_true", help="auto-stretch contrast before dithering")
    ap.add_argument("--invert", action="store_true", help="swap black and white")
    ap.add_argument("--preview", help="also write a PNG of exactly what the panel will show")
    ap.add_argument(
        "--header",
        action="store_true",
        help="write `out` as a C header (the firmware's built-in default, src/art/SleepImage.h) instead of a .bin",
    )
    a = ap.parse_args()

    w, h = PANELS[a.device]
    img = ImageOps.exif_transpose(Image.open(a.image))
    # Flatten transparency onto white paper.
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        paper = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(paper, img)
    img = img.convert("L")

    if a.fit == "cover":
        img = ImageOps.fit(img, (w, h), Image.LANCZOS)
    else:
        img = ImageOps.contain(img, (w, h), Image.LANCZOS)
        canvas = Image.new("L", (w, h), pad_color(img, a.pad))
        canvas.paste(img, ((w - img.width) // 2, (h - img.height) // 2))
        img = canvas
    if a.contrast:
        img = ImageOps.autocontrast(img, cutoff=1)
    if a.invert:
        img = ImageOps.invert(img)

    if a.dither == "floyd":
        bw = img.convert("1", dither=Image.FLOYDSTEINBERG)
    else:
        bw = img.point(lambda v: 255 if v >= a.threshold else 0).convert("1", dither=Image.NONE)

    row_bytes = (w + 7) // 8
    px = bw.load()
    body = bytearray(row_bytes * h)
    for y in range(h):
        for x in range(w):
            if px[x, y] == 0:  # black pixel -> ink bit
                body[y * row_bytes + (x >> 3)] |= 0x80 >> (x & 7)

    if a.header:
        lines = [
            "// Built-in sleep image (Settings > Sleep > Sleep screen > Image), shown",
            "// when the SD card has no /sleep.bin. Generated, do not hand-edit:",
            "//   python3 tools/make_sleep_image.py <picture> src/art/SleepImage.h --header",
            "// Rows of ceil(w/8) bytes, MSB-first, SET bit = black ink.",
            "#pragma once",
            "#include <cstdint>",
            "",
            "constexpr int kSleepImageW = %d;" % w,
            "constexpr int kSleepImageH = %d;" % h,
            "static const uint8_t kSleepImageBits[%d] = {" % len(body),
        ]
        for i in range(0, len(body), 16):
            lines.append("    " + ", ".join("0x%02X" % b for b in body[i : i + 16]) + ",")
        lines += ["};", ""]
        with open(a.out, "w") as f:
            f.write("\n".join(lines))
    else:
        with open(a.out, "wb") as f:
            f.write(struct.pack("<HBBHH", 0x5854, 1, 0, w, h))
            f.write(body)
    print("%s: %dx%d, %d bytes" % (a.out, w, h, 8 + len(body)))

    if a.preview:
        bw.save(a.preview)
        print("preview: %s" % a.preview)


if __name__ == "__main__":
    main()
