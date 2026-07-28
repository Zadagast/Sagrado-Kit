#!/usr/bin/env python3
"""Build format/skins/completion/ — Kit soft-fill pack for empty Hap slots.

Pulls icons from Ashen and chrome fillers from Boilerplate / Function.
Never includes Primary Background. Menu separator only if h ≤ 4.
"""
from __future__ import annotations
import shutil
from pathlib import Path

# Reuse Hap parse from extract script.
from extract_milk_redux import load_hap, write_skimg, write_png

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "format" / "skins" / "completion"

# Hap slot → art key (empty slots only filled at runtime).
ART_SLOTS = {
    101: "focus_box.normal",
    102: "focus_box.hilited",
    103: "focus_box.disabled",
    # Grips intentionally omitted — many themes bake thumb chrome into the
    # indicator; foreign grips look like a fake centre button.
    200: "menu.background_pattern",
    201: "menu.background",
    206: "menu.item.normal",
    207: "menu.item.hilited",
    208: "menu.separator",
    248: "popup_frame.normal",
    249: "popup_frame.focus",
    251: "wonderlight.off",
    252: "wonderlight.pause",
    253: "wonderlight.ready",
    254: "wonderlight.go",
    255: "wonderlight.finished",
    256: "wonderlight.flash_off",
    257: "wonderlight.flash_on1",
    258: "wonderlight.flash_on2",
}

ICON_SLOTS = {
    4: "file.generic.16",
    5: "file.generic.32",
    8: "folder.16",
    9: "folder.32",
}


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    donors = [
        ROOT / "research" / "haps" / "Boilerplate.hap",
        ROOT / "research" / "haps" / "Ashen.hap",
        ROOT / "research" / "haps" / "Function 2.0.hap",
        ROOT / "research" / "haps" / "BeOS.hap",
        ROOT / "research" / "haps" / "Mjolnir.hap",
    ]
    images: dict[int, dict] = {}
    icons: dict[int, dict] = {}
    for dpath in donors:
        if not dpath.exists():
            continue
        _, _, imgs, icos = load_hap(dpath)
        for slot, key in ART_SLOTS.items():
            if slot in images or slot not in imgs:
                continue
            img = imgs[slot]
            if key == "menu.separator" and img["h"] > 4:
                print(f"  skip [{slot}] {key} h={img['h']} from {dpath.name}")
                continue
            if key.startswith("popup_frame") and img["w"] * img["h"] < 25:
                print(f"  skip [{slot}] {key} tiny {img['w']}x{img['h']} from {dpath.name}")
                continue
            images[slot] = img
            print(f"  art [{slot}] {key} from {dpath.name} {img['w']}x{img['h']}")
        for slot, key in ICON_SLOTS.items():
            if slot in icons or slot not in icos:
                continue
            icons[slot] = icos[slot]
            print(f"  icon [{slot}] {key} from {dpath.name}")

    # If normal focus box missing, clone hilited.
    if 101 not in images and 102 in images:
        images[101] = images[102]
        print("  clone focus_box.normal from hilited")

    art_entries = []
    for slot, key in sorted(ART_SLOTS.items()):
        img = images.get(slot)
        if not img:
            print(f"  missing art {slot} {key}")
            continue
        fname = key.replace(".", "_") + ".skimg"
        write_skimg(OUT / fname, img)
        write_png(OUT / (key.replace(".", "_") + ".png"), img)
        art_entries.append((key, fname, img["caps"], img["pos"]))

    icon_entries = []
    for slot, key in sorted(ICON_SLOTS.items()):
        img = icons.get(slot)
        if not img:
            print(f"  missing icon {slot} {key}")
            continue
        fname = key.replace(".", "_") + ".skimg"
        write_skimg(OUT / fname, img)
        write_png(OUT / (key.replace(".", "_") + ".png"), img)
        icon_entries.append((key, fname))

    lines = [
        "# SagradoKit Hap soft-completion pack",
        "# Fills empty art/icon slots after live Hap import. Never Primary Background.",
        'format = "sap"',
        "version = 1",
        "",
        "[meta]",
        'name = "Hap Completion"',
        'creator = "SagradoKit"',
        'description = "Soft-fill icons, WonderLight, menu, focus box for incomplete Haps"',
        "",
        "[colors]",
        "",
    ]
    for key, fname, caps, pos in art_entries:
        lines.append(f'[art."{key}"]')
        lines.append(f'file = "{fname}"')
        lines.append(f"caps = [{caps[0]}, {caps[1]}, {caps[2]}, {caps[3]}]")
        lines.append(f"positions = [{pos[0]}, {pos[1]}, {pos[2]}, {pos[3]}]")
        lines.append("")
    lines.append("[icons]")
    for key, fname in icon_entries:
        lines.append(f'"{key}" = "{fname}"')
    lines.append("")

    toml = OUT / "completion.sap"
    toml.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {toml} ({len(art_entries)} art, {len(icon_entries)} icons)")


if __name__ == "__main__":
    main()
