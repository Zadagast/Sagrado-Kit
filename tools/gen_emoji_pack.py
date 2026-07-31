#!/usr/bin/env python3
"""Generate Sagrado kit emoji pack from Noto Color Emoji + Unicode emoji-test.txt.

Output (default build/emoji_pack/):
  catalog.txt   — CAT / EMOJI lines (easy C++ parse)
  png48/<SEQ>.png
  png32/<SEQ>.png

SEQ is hyphen-joined uppercase hex codepoints (e.g. 1F44D, 2764-FE0F).
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Ubuntu Characters–aligned groups (skip Component).
GROUP_TO_CAT = {
    "Smileys & Emotion": ("smileys", "Smileys"),
    "People & Body": ("people", "People"),
    "Animals & Nature": ("animals", "Animals"),
    "Food & Drink": ("food", "Food"),
    "Travel & Places": ("travel", "Travel"),
    "Activities": ("activities", "Activities"),
    "Objects": ("objects", "Objects"),
    "Symbols": ("symbols", "Symbols"),
    "Flags": ("flags", "Flags"),
}

# Representative icon seq per category (must exist in pack).
CAT_ICONS = {
    "smileys": "1F600",
    "people": "1F44D",
    "animals": "1F43B",
    "food": "1F354",
    "travel": "1F30D",
    "activities": "1F3C0",
    "objects": "1F4A1",
    "symbols": "2764-FE0F",
    "flags": "1F3F3-FE0F",
}

LINE_RE = re.compile(
    r"^([0-9A-Fa-f ]+)\s*;\s*(fully-qualified)\s*#\s*(\S+)\s+E[\d.]+\s+(.*)$"
)


def parse_emoji_test(path: Path):
    group = None
    entries = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("# group:"):
            group = raw.split(":", 1)[1].strip()
            continue
        if raw.startswith("#") or not raw.strip():
            continue
        m = LINE_RE.match(raw)
        if not m or group is None:
            continue
        if group not in GROUP_TO_CAT:
            continue
        cps = [c.upper() for c in m.group(1).split()]
        seq = "-".join(cps)
        emoji = m.group(3)
        name = m.group(4).strip().lower()
        cat_id, _ = GROUP_TO_CAT[group]
        entries.append((seq, cat_id, name, emoji))
    return entries


def _worker_init(font_path: str):
    global _FONT
    _FONT = ImageFont.truetype(font_path, 109)


def render_one(args):
    seq, emoji, out48, out32 = args
    try:
        canvas = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
        d = ImageDraw.Draw(canvas)
        d.text((16, 8), emoji, font=_FONT, embedded_color=True)
        bb = canvas.getbbox()
        if not bb:
            return seq, False, "empty"
        cropped = canvas.crop(bb)
        side = max(cropped.size) + 8
        sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        sq.paste(
            cropped,
            ((side - cropped.size[0]) // 2, (side - cropped.size[1]) // 2),
            cropped,
        )
        hi = sq.resize((96, 96), Image.Resampling.LANCZOS)
        img48 = hi.resize((48, 48), Image.Resampling.LANCZOS)
        img32 = hi.resize((32, 32), Image.Resampling.LANCZOS)
        Path(out48).parent.mkdir(parents=True, exist_ok=True)
        Path(out32).parent.mkdir(parents=True, exist_ok=True)
        img48.save(out48, "PNG", optimize=True)
        img32.save(out32, "PNG", optimize=True)
        return seq, True, ""
    except Exception as e:
        return seq, False, str(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--emoji-test",
        type=Path,
        default=Path(__file__).resolve().parent / "data" / "emoji-test.txt",
    )
    ap.add_argument(
        "--font",
        type=Path,
        default=Path("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"),
    )
    ap.add_argument(
        "--out", type=Path, default=Path(__file__).resolve().parents[1] / "build" / "emoji_pack"
    )
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    args = ap.parse_args()

    if not args.emoji_test.is_file():
        print(f"missing emoji-test: {args.emoji_test}", file=sys.stderr)
        return 1
    if not args.font.is_file():
        print(f"missing font: {args.font}", file=sys.stderr)
        return 1

    entries = parse_emoji_test(args.emoji_test)
    print(f"parsed {len(entries)} fully-qualified emoji")

    out = args.out
    png48 = out / "png48"
    png32 = out / "png32"
    png48.mkdir(parents=True, exist_ok=True)
    png32.mkdir(parents=True, exist_ok=True)

    jobs = []
    for seq, cat_id, name, emoji in entries:
        jobs.append(
            (
                seq,
                emoji,
                str(png48 / f"{seq}.png"),
                str(png32 / f"{seq}.png"),
            )
        )

    ok_seqs = set()
    fail = 0
    with ProcessPoolExecutor(
        max_workers=args.jobs, initializer=_worker_init, initargs=(str(args.font),)
    ) as ex:
        futs = [ex.submit(render_one, j) for j in jobs]
        done = 0
        for fut in as_completed(futs):
            seq, good, err = fut.result()
            done += 1
            if good:
                ok_seqs.add(seq)
            else:
                fail += 1
            if done % 250 == 0 or done == len(futs):
                print(f"  rendered {done}/{len(futs)} (ok={len(ok_seqs)} fail={fail})")

    # Stable category order
    cat_order = [
        ("smileys", "Smileys"),
        ("people", "People"),
        ("animals", "Animals"),
        ("food", "Food"),
        ("travel", "Travel"),
        ("activities", "Activities"),
        ("objects", "Objects"),
        ("symbols", "Symbols"),
        ("flags", "Flags"),
    ]

    catalog = out / "catalog.txt"
    with catalog.open("w", encoding="utf-8") as f:
        f.write("# Sagrado emoji pack catalog\n")
        f.write(f"# count {len(ok_seqs)}\n")
        for cid, label in cat_order:
            icon = CAT_ICONS.get(cid, "")
            if icon not in ok_seqs:
                # first available in category
                for seq, cat_id, name, emoji in entries:
                    if cat_id == cid and seq in ok_seqs:
                        icon = seq
                        break
            f.write(f"CAT {cid} {label} {icon}\n")
        for seq, cat_id, name, emoji in entries:
            if seq not in ok_seqs:
                continue
            # names may contain spaces; rest of line is name
            safe = name.replace("\n", " ").strip()
            f.write(f"EMOJI {seq} {cat_id} {safe}\n")

    print(f"wrote {catalog} ({len(ok_seqs)} emoji, {fail} skipped)")
    return 0 if ok_seqs else 1


if __name__ == "__main__":
    sys.exit(main())
