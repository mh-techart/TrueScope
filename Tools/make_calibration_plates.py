#!/usr/bin/env python3
# Copyright 2026 Michael Herber
# SPDX-License-Identifier: Apache-2.0

"""Generates TrueScope's calibration plates: synthetic targets whose pixel
populations are EXACTLY known.

Why these exist
---------------
`docs/dev-rules.md` ("Measure both sides with known-population targets — never
assume one") requires that a scope be calibrated against a reference tool by
feeding BOTH tools an image whose per-column and per-value pixel counts are known
by construction, then fitting the reference's transfer from its rendered output.
These are those images. The M6.3 density gains (31.25 for parade and waveform)
and the histogram clip threshold (1.58 % of frame pixels) were derived from them
on 2026-08-19.

Stdlib only, deliberately: the whole point is that any plate can be regenerated
in seconds on any machine with a python3, without installing anything.

Usage
-----
    python3 make_calibration_plates.py <output-directory>

Reading them
------------
Import each PNG into a DaVinci Resolve timeline of THE SAME pixel dimensions
(sRGB, clip Data Levels = Full) — one timeline per image size. A mismatched
timeline upscales the image before it reaches the scope, which destroys the known
counts the whole method depends on. Full conditions: `docs/dev-rules.md`,
"Resolve cross-check — conditions & procedure".

The plates
----------
1. `M63_count_wedge_1920x1080.png` — DENSITY transfer.
   16 bands of 120 px. Every column of band i holds exactly COUNTS_1080[i]
   pixels of code value 128, anchored at the top of the frame; the rest is
   black. Read the waveform trace brightness at the 128 line per band: count ->
   brightness. Measured 2026-08-19 to be linear (ratio constant at 6.00-6.13
   across seven bands) and to reach full white at ~34 px per column, i.e. 3.2 %
   of a 1080-row column -> TrueScope gain = 1 / 0.032 = 31.25.

2. `M63_count_wedge_1920x540.png` — the FRACTION-VS-ABSOLUTE disambiguation.
   Same construction at 540 rows, with counts chosen to straddle both candidate
   answers, so the ONLY thing that differs from plate 1 is the column height:
       clip near count 17 -> Resolve keys on the FRACTION of column height
                             (what TrueScope's 31.25 default assumes)
       clip near count 34 -> Resolve keys on an ABSOLUTE pixel count
                             (TrueScope's default would have to be 15.625)
   The two predictions are a clean factor of two apart. This single reading
   settles the open question in `docs/backlog.md`, "Waveform density basis";
   the 1080-only measurement could not.

3. `M63_histogram_strips_1920x1080.png` — HISTOGRAM clip threshold.
   Full-height strips, so each code value occupies a known FRACTION OF THE
   FRAME rather than of a column. Widths double from 0.4167 % to 13.3333 %,
   which brackets the threshold from both sides. Measured 2026-08-19: the 0.4167 %
   strip filled 26.3 % of the band and the 0.8333 % strip 52.7 %, giving
   1.584 % and 1.581 % for a full band; the 1.6667 % strip clipped. Hence
   0.0158 in `TrueScopeDisplayGains.h`.

Two properties that matter when reading any of them
---------------------------------------------------
* Code value 128 sits between Resolve's graticule lines (multiples of 32 in
  8-bit), so no band is masked by a graticule line — see the wedge-values rule
  in `dev-rules.md`. It lands on 512 of Resolve's 10-bit scale under Data Level
  Scopes.
* An 8-bit plate populates only every fourth container of Resolve's 1024-bin
  histogram, so it draws there as a comb while TrueScope (256 bins, one per code
  value) draws a smooth curve. Peak heights must match; fine structure must not.
  That is an artefact of the plate, not of either tool.
"""

import os
import struct
import sys
import zlib

GREY = 128        # 8-bit code value; between Resolve's 8-bit graticule lines
BAND_W = 120      # 16 bands x 120 px = 1920

# Per-column pixel counts. Plate 1 spans the full range coarsely to fit the
# transfer; plate 2 is dense around 17 and 34 to resolve which one clips first.
COUNTS_1080 = [1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 128, 192, 270, 432, 648, 1080]
COUNTS_540 = [4, 8, 12, 14, 16, 17, 18, 20, 24, 28, 32, 34, 36, 40, 48, 64]

# (code value, strip width in PIXELS). Widths are given as pixel counts rather
# than percentages so the frame fractions are exact by construction: at 1920x1080
# each strip is a clean doubling of the last, 8 px = 0.4167 % of the frame through
# 256 px = 13.3333 %. The 32 px strip (1.6667 %) was the one observed to clip.
#
# The delivered 2026-08-19 plate has 257 px in the last strip, not 256: the
# throwaway script that made it specified 13.36 % rather than 13.3333 %, which
# rounds up. Every other strip is pixel-identical, and the 1.58 % figure was
# fitted from the 8 px and 16 px strips, so nothing measured depends on it.
STRIP_SPEC = [(40, 8), (80, 16), (110, 32), (140, 64), (170, 128), (200, 256)]


def write_png(path, width, height, rows):
    """Writes an 8-bit RGB PNG. `rows` is a list of `bytearray`s of 3*width."""
    raw = bytearray()
    for row in rows:
        raw.append(0)          # filter type 0 (None) — these images compress fine
        raw += row

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def count_wedge(path, width, height, counts):
    """Band i: every column holds exactly counts[i] grey pixels, top-anchored.

    A 1-px black seam on each band's left edge stops adjacent bands bleeding
    into one another under any resampling, at the cost of one column: bands
    after the first are 119 px wide, the first is 120.
    """
    assert len(counts) * BAND_W == width
    rows = [bytearray(width * 3) for _ in range(height)]
    for i, count in enumerate(counts):
        x0 = i * BAND_W
        seam = 1 if i else 0
        for y in range(count):
            row = rows[y]
            for x in range(x0 + seam, x0 + BAND_W):
                row[3 * x] = row[3 * x + 1] = row[3 * x + 2] = GREY
    write_png(path, width, height, rows)
    return [(c, BAND_W - (1 if i else 0)) for i, c in enumerate(counts)]


def histogram_strips(path, width, height, spec):
    """Full-height strips, each a known fraction of the frame."""
    rows = [bytearray(width * 3) for _ in range(height)]
    x, out = 0, []
    for value, w in spec:
        for row in rows:
            for xx in range(x, min(x + w, width)):
                row[3 * xx] = row[3 * xx + 1] = row[3 * xx + 2] = value
        out.append((value, w, w * height, 100.0 * w * height / (width * height)))
        x += w
    write_png(path, width, height, rows)
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {os.path.basename(sys.argv[0])} <output-directory>")
    base = sys.argv[1]
    os.makedirs(base, exist_ok=True)

    for height, counts in ((1080, COUNTS_1080), (540, COUNTS_540)):
        path = os.path.join(base, f"M63_count_wedge_1920x{height}.png")
        bands = count_wedge(path, 1920, height, counts)
        print(f"{path}  (1920x{height}, value {GREY}, {BAND_W} px bands)")
        for i, (count, band_px) in enumerate(bands):
            print(f"    band {i:>2}  x {i*BAND_W:>4}-{(i+1)*BAND_W-1:<4}  "
                  f"{count:>4} px/column = {100.0*count/height:6.2f} % of the column, "
                  f"{band_px:>3} grey columns")
        print()

    path = os.path.join(base, "M63_histogram_strips_1920x1080.png")
    print(f"{path}  (1920x1080)")
    for value, w, pixels, pct in histogram_strips(path, 1920, 1080, STRIP_SPEC):
        print(f"    value {value:>3}  {w:>4} px wide = {pixels:>8} px = "
              f"{pct:5.3f} % of the frame")


if __name__ == "__main__":
    main()
