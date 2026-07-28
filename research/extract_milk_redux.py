#!/usr/bin/env python3
"""Extract first-wave Hap image slots into a SagradoKit art skin (.skimg + .skin.toml)."""
from __future__ import annotations
import struct
import zlib
from pathlib import Path

# Verified / probed Hap slot → SagradoKit art key.
SLOT_MAP = {
    25: "button.normal",
    26: "button.hilited",
    27: "button.disabled",
    37: "default_button.normal",
    38: "default_button.hilited",
    39: "default_button.disabled",
    # Tick Blank / Ticked / Tristate × Normal/Hilited/Disabled
    57: "tick.blank.normal",
    58: "tick.blank.hilited",
    59: "tick.blank.disabled",
    61: "tick.ticked.normal",
    62: "tick.ticked.hilited",
    63: "tick.ticked.disabled",
    65: "tick.tristate.normal",
    66: "tick.tristate.hilited",
    67: "tick.tristate.disabled",
    # Mutex Blank / Ticked / Tristate × Normal/Hilited/Disabled
    69: "mutex.blank.normal",
    70: "mutex.blank.hilited",
    71: "mutex.blank.disabled",
    73: "mutex.ticked.normal",
    74: "mutex.ticked.hilited",
    75: "mutex.ticked.disabled",
    77: "mutex.tristate.normal",
    78: "mutex.tristate.hilited",
    79: "mutex.tristate.disabled",
    81: "disclosure.plus.small",
    85: "disclosure.minus.small",
    89: "popup.normal",
    90: "popup.hilited",
    91: "popup.disabled",
    97: "popup.symbol.normal",
    98: "popup.symbol.hilited",
    99: "popup.symbol.disabled",
    105: "separator.h",
    106: "separator.v",
    107: "box",
    108: "framed_raised",
    111: "progress.bar",
    112: "progress.fill",
    220: "window.frame.normal",
    221: "window.frame.focus",
    223: "window.close.normal",
    224: "window.close.focus",
    225: "window.close.hilited",
    228: "window.minimize.normal",
    229: "window.minimize.focus",
    230: "window.minimize.hilited",
    233: "window.maximize.normal",
    234: "window.maximize.focus",
    235: "window.maximize.hilited",
    243: "window.resize.normal",
    244: "window.resize.focus",
}

# Hap colour index → SagradoKit role (subset used by kit surfaces).
COLOR_MAP = {
    1: "primary.light",
    2: "primary.background",
    3: "primary.dark",
    4: "primary.frame",
    5: "primary.label",
    6: "primary.disable_frame",
    7: "primary.disable_label",
    8: "important.label",
    9: "focus.box",
    10: "text.background",
    11: "text.foreground",
    12: "text.hilite_background",
    13: "text.hilite_foreground",
    14: "text.insertion_point",
    15: "list.background",
    16: "list.label",
    17: "list.hilite_background",
    18: "list.hilite_foreground",
    19: "list.sort_column_background",
    20: "list.separator",
    29: "button.light2",
    30: "button.light1",
    31: "button.face",
    32: "button.dark1",
    33: "button.dark2",
    34: "button.frame",
    35: "button.label",
    36: "button_hilite.light2",
    37: "button_hilite.light1",
    38: "button_hilite.face",
    39: "button_hilite.dark1",
    40: "button_hilite.dark2",
    41: "button_hilite.frame",
    42: "button_hilite.label",
    43: "button_disable.light2",
    44: "button_disable.light1",
    45: "button_disable.face",
    46: "button_disable.dark1",
    47: "button_disable.dark2",
    48: "button_disable.frame",
    49: "button_disable.label",
    50: "default_button.light",
    51: "default_button.face",
    52: "default_button.dark",
    53: "default_button.frame",
    54: "window.light2",
    55: "window.light1",
    56: "window.face",
    57: "window.dark1",
    58: "window.dark2",
    59: "window.frame",
    60: "window.label",
    79: "window_focus.light2",
    80: "window_focus.light1",
    81: "window_focus.face",
    82: "window_focus.dark1",
    83: "window_focus.dark2",
    84: "window_focus.frame",
    85: "window_focus.label",
    104: "menu.light",
    105: "menu.background",
    106: "menu.dark",
    107: "menu.label",
    108: "menu.hilite_light",
    109: "menu.hilite_background",
    110: "menu.hilite_dark",
    111: "menu.hilite_label",
    112: "menu.disable_label",
    123: "progress.bkgnd_light",
    124: "progress.bkgnd",
    125: "progress.bkgnd_dark",
    126: "progress.frame",
    127: "progress.label",
}


def rd16(d: bytes, o: int) -> int:
    return (d[o] << 8) | d[o + 1]


def rd32(d: bytes, o: int) -> int:
    return (d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]


def parse_image(d: bytes, o: int):
    w, h = rd16(d, o), rd16(d, o + 2)
    if w <= 0 or h <= 0 or w > 2048 or h > 2048:
        return None
    flags_bpp = rd16(d, o + 4)
    transparent_active = (flags_bpp & 0x0100) != 0
    bpp = flags_bpp & 0xFF
    if bpp not in (1, 2, 4, 8):
        return None
    palette_len = d[o + 6] + 1
    transparent_index = d[o + 7]
    caps = list(d[o + 12 : o + 16])
    pos = list(d[o + 16 : o + 20])
    palette = [rd32(d, o + 20 + 4 * i) & 0x00FFFFFF for i in range(palette_len)]
    pixels_off = o + 20 + 4 * palette_len
    stride = (w * bpp + 31) // 32 * 4
    if pixels_off + stride * h > len(d):
        return None
    px = []
    for y in range(h):
        row = d[pixels_off + y * stride :]
        for x in range(w):
            if bpp == 8:
                idx = row[x]
            elif bpp == 4:
                idx = (row[x // 2] >> (4 * (1 - x % 2))) & 0xF
            elif bpp == 2:
                idx = (row[x // 4] >> (6 - 2 * (x % 4))) & 0x3
            else:
                idx = (row[x // 8] >> (7 - x % 8)) & 0x1
            if transparent_active and idx == transparent_index:
                px.append(0)
            else:
                rgb = palette[idx] if idx < palette_len else 0
                px.append(0xFF000000 | rgb)
    return {"w": w, "h": h, "caps": caps, "pos": pos, "px": px}


def load_hap(path: Path):
    d = path.read_bytes()
    if d[:4] != b"%HAP" or rd32(d, 4) != 0x00010000:
        raise SystemExit(f"not a HAP: {path}")
    info_off, info_len = rd32(d, 0x2C), rd32(d, 0x30)
    img_off, img_len = rd32(d, 0x34), rd32(d, 0x38)
    col_off, col_len = rd32(d, 0x3C), rd32(d, 0x40)

    name = path.stem
    if info_len >= 0x34 and info_off + 0x34 <= len(d):
        s = info_off + 0x34
        l = d[info_off + 0x22]
        if l and s + l <= len(d):
            name = d[s : s + l].decode("latin-1", errors="replace")

    colors = []
    n = min(col_len // 4, 204)
    for i in range(n):
        colors.append(rd32(d, col_off + 4 * i) & 0x00FFFFFF)

    images = {}
    if img_len > 0:
        first = None
        offsets = []
        i = 0
        while True:
            if first is not None and 4 * i >= first:
                break
            if i > 4096:
                break
            v = rd32(d, img_off + 4 * i)
            if v != 0 and (first is None or v < first):
                first = v
            offsets.append(v)
            i += 1
        for slot, rel in enumerate(offsets):
            if not rel:
                continue
            img = parse_image(d, img_off + rel)
            if img:
                images[slot] = img
    return name, colors, images


def write_skimg(path: Path, img: dict):
    with path.open("wb") as f:
        f.write(b"SKIM")
        f.write(struct.pack("<HH", img["w"], img["h"]))
        f.write(bytes(img["caps"]))
        f.write(bytes(img["pos"]))
        for p in img["px"]:
            f.write(struct.pack("<I", p))


def write_png(path: Path, img: dict):
    """Optional viewer PNG (8-bit RGBA)."""
    w, h = img["w"], img["h"]
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter None
        for x in range(w):
            p = img["px"][y * w + x]
            a = (p >> 24) & 0xFF
            r = (p >> 16) & 0xFF
            g = (p >> 8) & 0xFF
            b = p & 0xFF
            raw.extend((r, g, b, a))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(
            ">I", zlib.crc32(tag + data) & 0xFFFFFFFF
        )

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", ihdr)
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")
    path.write_bytes(data)


def hex_rgb(v: int) -> str:
    return f"#{(v >> 16) & 0xFF:02x}{(v >> 8) & 0xFF:02x}{v & 0xFF:02x}"


def main():
    root = Path(__file__).resolve().parents[1]
    hap = root / "research" / "haps" / "Milk Redux.hap"
    out_dir = root / "format" / "skins" / "milk-redux"
    out_dir.mkdir(parents=True, exist_ok=True)

    name, colors, images = load_hap(hap)
    print(f"theme: {name}  images={len(images)}  colors={len(colors)}")

    art_entries = []
    for slot, key in sorted(SLOT_MAP.items()):
        img = images.get(slot)
        if not img:
            print(f"  missing slot {slot} ({key})")
            continue
        fname = key.replace(".", "_") + ".skimg"
        write_skimg(out_dir / fname, img)
        write_png(out_dir / (key.replace(".", "_") + ".png"), img)
        art_entries.append((key, fname, img["caps"], img["pos"]))
        print(
            f"  [{slot:3d}] {key:28s} {img['w']}x{img['h']} "
            f"caps={img['caps']} pos={img['pos']}"
        )

    # Group colours for TOML
    groups: dict[str, dict[str, str]] = {}
    for idx, role in COLOR_MAP.items():
        if idx >= len(colors):
            continue
        group, _, leaf = role.partition(".")
        groups.setdefault(group, {})[leaf] = hex_rgb(colors[idx])

    # Window transitions 61..78 and 86..103; progress fill 113..122
    for base, start, n in (("window", 61, 18), ("window_focus", 86, 18),
                           ("progress", 113, 10)):
        stops = []
        for i in range(n):
            idx = start + i
            if idx < len(colors):
                stops.append(hex_rgb(colors[idx]))
        if stops:
            groups.setdefault(base, {})["__transition__"] = stops

    lines = [
        "# SagradoKit skin — Milk Redux first-wave art (extracted from .hap)",
        'format = "sagrado-skin"',
        "version = 1",
        "",
        "[meta]",
        f'name = "{name}"',
        'creator = "extracted from Hap"',
        'description = "Art-first seed: button, default, popup, window frame/title boxes"',
        'version = "1.0"',
        "",
    ]
    for group, roles in sorted(groups.items()):
        lines.append(f"[colors.{group}]")
        for leaf, hx in sorted(roles.items()):
            if leaf == "__transition__":
                continue
            lines.append(f'{leaf} = "{hx}"')
        if "__transition__" in roles:
            lines.append("transition = [")
            stops = roles["__transition__"]
            for i, hx in enumerate(stops):
                comma = "," if i + 1 < len(stops) else ""
                lines.append(f'  "{hx}"{comma}')
            lines.append("]")
        lines.append("")

    for key, fname, caps, pos in art_entries:
        lines.append(f'[art."{key}"]')
        lines.append(f'file = "{fname}"')
        lines.append(f"caps = [{caps[0]}, {caps[1]}, {caps[2]}, {caps[3]}]")
        lines.append(
            f"positions = [{pos[0]}, {pos[1]}, {pos[2]}, {pos[3]}]"
        )
        lines.append("")

    lines.append("[icons]")
    lines.append("")

    toml_path = out_dir / "milk-redux.skin.toml"
    toml_path.write_text("\n".join(lines) + "\n")
    print(f"wrote {toml_path} ({len(art_entries)} art slots)")


if __name__ == "__main__":
    main()
