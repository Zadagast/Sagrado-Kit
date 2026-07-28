#!/usr/bin/env python3
"""Probe .hap image slots: occupancy, size, caps, positions across themes."""
from __future__ import annotations
import struct
from pathlib import Path

# AppearanceEdit Images panel order (from AppearanceEdit.exe string table).
# Slot indices are sparse in the file; this list is the authoring name order,
# not a guarantee of contiguous indices. Known mappings from Sagrado probes
# are noted in KNOWN.
IMAGE_NAMES = [
    "Primary Background",
    "Button Normal",
    "Button Hilited",
    "Button Disabled",
    "Default Button Normal",
    "Default Button Hilited",
    "Default Button Disabled",
    "Icon Button Normal",
    "Icon Button Hilited",
    "Icon Button Disabled",
    "Tick Button Blank Normal",
    "Tick Button Blank Hilited",
    "Tick Button Blank Disabled",
    "Tick Button Ticked Normal",
    "Tick Button Ticked Hilited",
    "Tick Button Ticked Disabled",
    "Tick Button Tristated Normal",
    "Tick Button Tristated Hilited",
    "Tick Button Tristated Disabled",
    "Mutex Button Blank Normal",
    "Mutex Button Blank Hilited",
    "Mutex Button Blank Disabled",
    "Mutex Button Ticked Normal",
    "Mutex Button Ticked Hilited",
    "Mutex Button Ticked Disabled",
    "Mutex Button Tristated Normal",
    "Mutex Button Tristated Hilited",
    "Mutex Button Tristated Disabled",
    "Small Plus Button Normal",
    "Small Minus Button Normal",
    "Medium Plus Button Normal",
    "Medium Minus Button Normal",
    "Popup Button Normal",
    "Popup Button Hilited",
    "Popup Button Disabled",
    "Popup Button No Title Normal",
    "Popup Button No Title Hilited",
    "Popup Button No Title Disabled",
    "Popup Button Symbol Normal",
    "Popup Button Symbol Hilited",
    "Popup Button Symbol Disabled",
    "Focus Box Normal",
    "Focus Box Hilited",
    "Focus Box Disabled",
    "Horiz Separator Line",
    "Vert Separator Line",
    "Framed Raised Box",  # Box / Framed Raised Box — see docs
    "Progress Bar",
    "Progress Bar Fill",
    "H Slider Bar Normal",
    "H Slider Bar Hilited",
    "H Slider Bar Disabled",
    "H Slider Indicator Normal",
    "H Slider Indicator Hilited",
    "H Slider Indicator Disabled",
    "H Slider Pointed Indicator Normal",
    "H Slider Pointed Indicator Hilited",
    "H Slider Pointed Indicator Disabled",
    "V Slider Bar Normal",
    "V Slider Bar Hilited",
    "V Slider Bar Disabled",
    "V Slider Indicator Normal",
    "V Slider Indicator Hilited",
    "V Slider Indicator Disabled",
    "V Slider Pointed Indicator Normal",
    "V Slider Pointed Indicator Hilited",
    "V Slider Pointed Indicator Disabled",
    "Column Header Normal",
    "Column Header Hilited",
    "Column Header Disabled",
    "H Scroll Bar Double Arrows",
    "H Scroll Bar Single Arrows",
    "H Scroll Bar Disabled",
    "H Scroll Bar Too Small",
    "H Scroll Bar Indicator Normal",
    "H Scroll Bar Indicator Hilited",
    "H Scroll Bar Indicator Grips Normal",
    "H Scroll Bar Indicator Grips Hilited",
    "H Scroll Bar First Left Arrow Hilited",
    "H Scroll Bar First Right Arrow Hilited",
    "H Scroll Bar Second Left Arrow Hilited",
    "H Scroll Bar Second Right Arrow Hilited",
    "H Scroll Bar Single Left Arrow Hilited",
    "H Scroll Bar Single Right Arrow Hilited",
    "V Scroll Bar Double Arrows",
    "V Scroll Bar Single Arrows",
    "V Scroll Bar Disabled",
    "V Scroll Bar Too Small",
    "V Scroll Bar Indicator Normal",
    "V Scroll Bar Indicator Hilited",
    "V Scroll Bar Indicator Grips Normal",
    "V Scroll Bar Indicator Grips Hilited",
    "V Scroll Bar First Up Arrow Hilited",
    "V Scroll Bar First Down Arrow Hilited",
    "V Scroll Bar Second Up Arrow Hilited",
    "V Scroll Bar Second Down Arrow Hilited",
    "V Scroll Bar Single Up Arrow Hilited",
    "V Scroll Bar Single Down Arrow Hilited",
    "Menu Bar Pattern",
    "Menu Bar",
    "Menu Bar Title Pattern Normal",
    "Menu Bar Title Pattern Hilited",
    "Menu Bar Title Pattern Disabled",
    "Menu Bar Title Normal",
    "Menu Bar Title Hilited",
    "Menu Bar Title Disabled",
    "Menu Background Pattern",
    "Menu Background",
    "Menu Item Pattern Normal",
    "Menu Item Pattern Hilited",
    "Menu Item Pattern Disabled",
    "Menu Item Normal",
    "Menu Item Hilited",
    "Menu Item Disabled",
    "Menu Separator",
    "Popup Window Frame Normal",
    "Popup Window Frame Focus",
    "Window Frame Normal",
    "Window Frame Focus",
    "Window Close Button Normal",
    "Window Close Button Focus",
    "Window Close Button Hilited",
    "Window Close Button Disabled",
    "Window Minimize Button Normal",
    "Window Minimize Button Focus",
    "Window Minimize Button Hilited",
    "Window Minimize Button Disabled",
    "Window Maximize Button Normal",
    "Window Maximize Button Focus",
    "Window Maximize Button Hilited",
    "Window Maximize Button Disabled",
    "Window Menu Button Normal",
    "Window Menu Button Focus",
    "Window Menu Button Hilited",
    "Window Menu Button Disabled",
    "Window Resize Button Normal",
    "Window Resize Button Focus",
    "WonderLight Off",
    "WonderLight Pause",
    "WonderLight Ready",
    "WonderLight Go",
    "WonderLight Finished",
    "WonderLight Flash Off",
    "WonderLight Flash On 1",
    "WonderLight Flash On 2",
]

# Slot indices verified by Sagrado (AppearanceEdit cross-check / theme probes).
KNOWN = {
    25: "Button Normal",
    26: "Button Hilited",
    27: "Button Disabled",
    37: "Default Button Normal",
    38: "Default Button Hilited",
    39: "Default Button Disabled",
    57: "Tick Button Blank Normal",
    58: "Tick Button Blank Hilited",
    59: "Tick Button Blank Disabled",
    61: "Tick Button Ticked Normal",
    62: "Tick Button Ticked Hilited",
    63: "Tick Button Ticked Disabled",
    65: "Tick Button Tristated Normal",
    66: "Tick Button Tristated Hilited",
    67: "Tick Button Tristated Disabled",
    69: "Mutex Button Blank Normal",
    70: "Mutex Button Blank Hilited",
    71: "Mutex Button Blank Disabled",
    73: "Mutex Button Ticked Normal",
    74: "Mutex Button Ticked Hilited",
    75: "Mutex Button Ticked Disabled",
    77: "Mutex Button Tristated Normal",
    78: "Mutex Button Tristated Hilited",
    79: "Mutex Button Tristated Disabled",
    81: "Small Plus Button Normal",
    85: "Small Minus Button Normal",
    89: "Popup Button Normal",
    90: "Popup Button Hilited",
    91: "Popup Button Disabled",
    93: "Popup Button No Title Normal",
    94: "Popup Button No Title Hilited",
    95: "Popup Button No Title Disabled",
    97: "Popup Button Symbol Normal",
    98: "Popup Button Symbol Hilited",
    99: "Popup Button Symbol Disabled",
    105: "Horiz Separator Line",
    106: "Vert Separator Line",
    107: "Box",
    108: "Framed Raised Box",
    111: "Progress Bar",
    112: "Progress Bar Fill",
    49: "Icon Button Normal",
    50: "Icon Button Hilited",
    51: "Icon Button Disabled",
    134: "H Slider Pointed Indicator Normal",
    135: "H Slider Pointed Indicator Hilited",
    136: "H Slider Pointed Indicator Disabled",
    146: "V Slider Pointed Indicator Normal",
    147: "V Slider Pointed Indicator Hilited",
    148: "V Slider Pointed Indicator Disabled",
    150: "Column Header Normal",
    151: "Column Header Hilited",
    152: "Column Header Disabled",
    162: "H Scroll Bar Double Arrows",
    163: "H Scroll Bar Single Arrows",
    164: "H Scroll Bar Disabled",
    165: "H Scroll Bar Too Small",
    166: "H Scroll Bar Indicator Normal",
    169: "H Scroll Bar Indicator Grips Normal",
    170: "H Scroll Bar Indicator Grips Hilited",
    181: "V Scroll Bar Double Arrows",
    182: "V Scroll Bar Single Arrows",
    183: "V Scroll Bar Disabled",
    184: "V Scroll Bar Too Small",
    185: "V Scroll Bar Indicator Normal",
    188: "V Scroll Bar Indicator Grips Normal",
    189: "V Scroll Bar Indicator Grips Hilited",
    220: "Window Frame Normal",
    221: "Window Frame Focus",
    223: "Window Close Button Normal",
    224: "Window Close Button Focus",
    225: "Window Close Button Hilited",
    228: "Window Minimize Button Normal",
    229: "Window Minimize Button Focus",
    230: "Window Minimize Button Hilited",
    233: "Window Maximize Button Normal",
    234: "Window Maximize Button Focus",
    235: "Window Maximize Button Hilited",
    238: "Window Menu Button Normal",
    239: "Window Menu Button Focus",
    243: "Window Resize Button Normal",
    244: "Window Resize Button Focus",
    251: "WonderLight Off",
    252: "WonderLight Pause",
    253: "WonderLight Ready",
    254: "WonderLight Go",
    255: "WonderLight Finished",
    256: "WonderLight Flash Off",
    257: "WonderLight Flash On 1",
    258: "WonderLight Flash On 2",
}


def rd16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def rd32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def load_slots(path: Path):
    d = path.read_bytes()
    if d[:4] != b"%HAP" or rd32(d, 4) != 0x00010000:
        return None, {}
    img_off = rd32(d, 0x34)
    img_len = rd32(d, 0x38)
    if img_len == 0:
        return path.stem, {}
    first = None
    offsets = []
    i = 0
    while True:
        v = rd32(d, img_off + 4 * i)
        if v != 0 and (first is None or v < first):
            first = v
        offsets.append(v)
        i += 1
        if first is not None and 4 * i >= first:
            break
        if i > 4096:
            break
    slots = {}
    for slot, rel in enumerate(offsets):
        if rel == 0:
            continue
        o = img_off + rel
        if o + 20 > len(d):
            continue
        w, h = rd16(d, o), rd16(d, o + 2)
        caps = list(d[o + 12 : o + 16])
        pos = list(d[o + 16 : o + 20])
        slots[slot] = {"w": w, "h": h, "caps": caps, "pos": pos}
    return path.stem, slots


def main():
    haps = sorted(Path("/workspace/research/haps").glob("*.hap"))
    all_slots: dict[int, list] = {}
    theme_slots = {}
    for p in haps:
        name, slots = load_slots(p)
        if name is None:
            print("skip bad", p)
            continue
        theme_slots[name] = slots
        for s, meta in slots.items():
            all_slots.setdefault(s, []).append((name, meta))

    print(f"themes: {len(theme_slots)}  unique slots used: {len(all_slots)}")
    print()
    print("=== Known Sagrado mappings — presence & typical geometry ===")
    for slot, label in sorted(KNOWN.items()):
        hits = all_slots.get(slot, [])
        print(f"\n[{slot}] {label}  — in {len(hits)}/{len(theme_slots)} themes")
        for name, m in hits:
            print(
                f"  {name:20s} {m['w']:3d}x{m['h']:<3d}  "
                f"caps={m['caps']}  pos={m['pos']}"
            )

    print("\n=== High-occupancy unknown slots (candidates to map) ===")
    for slot, hits in sorted(all_slots.items(), key=lambda kv: -len(kv[1])):
        if slot in KNOWN:
            continue
        if len(hits) < 3:
            continue
        # summarize sizes
        sizes = {}
        for name, m in hits:
            sizes.setdefault((m["w"], m["h"]), 0)
            sizes[(m["w"], m["h"])] += 1
        top = sorted(sizes.items(), key=lambda x: -x[1])[:3]
        print(f"slot {slot:4d}  n={len(hits):2d}  sizes={top}  eg caps={hits[0][1]['caps']} pos={hits[0][1]['pos']}")

    # Focus controls for SagradoKit first map
    focus = {
        "button": [25, 26, 27],  # Normal/Hilited/Disabled guess for 27
        "column": [150, 151, 152],
        "hscroll": list(range(162, 175)),
        "vscroll": list(range(181, 195)),
        "window": list(range(220, 250)),
    }
    print("\n=== Neighborhood dumps (for index assignment) ===")
    for group, rng in focus.items():
        print(f"\n-- {group} --")
        for slot in rng:
            hits = all_slots.get(slot, [])
            label = KNOWN.get(slot, "?")
            if not hits:
                continue
            m = hits[0][1]
            print(
                f"  {slot:4d} {label:40s} n={len(hits)} "
                f"{m['w']}x{m['h']} caps={m['caps']} pos={m['pos']}"
            )


if __name__ == "__main__":
    main()
