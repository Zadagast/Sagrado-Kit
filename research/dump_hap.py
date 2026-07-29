#!/usr/bin/env python3
"""Dump a .hap's image/icon sections as labelled PNG contact sheets.

Used to line the real AppearanceEdit panels (which list slots by name, in
slot order) up against the raw slot indices in the file, so the kit's
slot -> role map can be verified instead of guessed.

  python3 research/dump_hap.py research/haps/"Milk Redux.hap" /tmp/out
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def rd16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def rd32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def parse_image(d, o, header=20):
    """Decode one record. Image records carry a 20-byte header (transparent
    colour + caps + positions); icon records carry only the 8-byte core."""
    w, h = rd16(d, o), rd16(d, o + 2)
    if not (0 < w <= 2048 and 0 < h <= 2048):
        return None
    flags_bpp = rd16(d, o + 4)
    transparent_active = bool(flags_bpp & 0x0100)
    bpp = flags_bpp & 0xFF
    if bpp not in (1, 2, 4, 8):
        return None
    pal_len = d[o + 6] + 1
    tidx = d[o + 7]
    caps = list(d[o + 12 : o + 16]) if header >= 20 else [0, 0, 0, 0]
    pos = list(d[o + 16 : o + 20]) if header >= 20 else [0, 0, 0, 0]
    pal = [rd32(d, o + header + 4 * i) & 0xFFFFFF for i in range(pal_len)]
    px_off = o + header + 4 * pal_len
    stride = ((w * bpp + 31) // 32) * 4
    if px_off + stride * h > len(d):
        return None
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    put = im.load()
    for y in range(h):
        row = d[px_off + y * stride : px_off + (y + 1) * stride]
        for x in range(w):
            if bpp == 8:
                idx = row[x]
            elif bpp == 4:
                idx = (row[x // 2] >> (4 * (1 - x % 2))) & 0xF
            elif bpp == 2:
                idx = (row[x // 4] >> (6 - 2 * (x % 4))) & 0x3
            else:
                idx = (row[x // 8] >> (7 - x % 8)) & 0x1
            if transparent_active and idx == tidx:
                continue
            rgb = pal[idx] if idx < pal_len else 0
            put[x, y] = ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255)
    return {"im": im, "caps": caps, "pos": pos, "bpp": bpp,
            "transparent": transparent_active}


def section_records(d, off, length, cap, header=20):
    if length == 0:
        return {}
    first = None
    offsets = []
    i = 0
    while True:
        if off + 4 * i + 4 > len(d):
            break
        v = rd32(d, off + 4 * i)
        if v != 0 and (first is None or v < first):
            first = v
        offsets.append(v)
        i += 1
        if first is not None and 4 * i >= first:
            break
        if i > cap:
            break
    out = {}
    for slot, rel in enumerate(offsets):
        if rel == 0:
            continue
        rec = parse_image(d, off + rel, header)
        if rec:
            out[slot] = rec
    return out


def load(path):
    d = Path(path).read_bytes()
    assert d[:4] == b"%HAP", "not a .hap"
    images = section_records(d, rd32(d, 0x34), rd32(d, 0x38), 4096)
    icons = section_records(d, rd32(d, 0x44), rd32(d, 0x48), 512, header=8)
    return images, icons


def sheet(records, out_png, cols=8, cell=64):
    if not records:
        return
    slots = sorted(records)
    rows = (len(slots) + cols - 1) // cols
    img = Image.new("RGB", (cols * cell, rows * (cell + 12)), (48, 48, 48))
    dr = ImageDraw.Draw(img)
    for n, slot in enumerate(slots):
        cx = (n % cols) * cell
        cy = (n // cols) * (cell + 12)
        rec = records[slot]
        im = rec["im"]
        if im.width > cell or im.height > cell:
            im = im.copy()
            im.thumbnail((cell, cell))
        img.paste(im, (cx + (cell - im.width) // 2, cy + (cell - im.height) // 2),
                  im)
        dr.text((cx + 2, cy + cell), f"{slot} {rec['im'].width}x{rec['im'].height}",
                fill=(255, 255, 0))
    img.save(out_png)
    print("wrote", out_png, len(slots), "slots")


def main():
    hap = sys.argv[1]
    outdir = Path(sys.argv[2] if len(sys.argv) > 2 else "/tmp/hapdump")
    outdir.mkdir(parents=True, exist_ok=True)
    images, icons = load(hap)
    print(f"{hap}: {len(images)} images, {len(icons)} icons")
    for name, recs in (("images", images), ("icons", icons)):
        sheet(recs, outdir / f"{name}.png")
        for slot, rec in sorted(recs.items()):
            print(f"  {name} {slot:4d} {rec['im'].width:3d}x{rec['im'].height:<3d} "
                  f"bpp={rec['bpp']} transp={rec['transparent']} "
                  f"caps={rec['caps']} pos={rec['pos']}")


if __name__ == "__main__":
    main()
