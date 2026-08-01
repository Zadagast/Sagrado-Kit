#!/usr/bin/env python3
"""Build format/skins/ooze/ — Ooze-look Sagrado .sap (original art, no Ooze code).

Aluminum pinstripe gel, classic traffic lights, aqua-tinted controls.
"""
from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "format" / "skins" / "ooze"

# ── Palette (Ooze-inspired; authored here, not imported) ───────────────────
ALUM = (0xF0, 0xF0, 0xF0)
ALUM_DARK = (0xD8, 0xD8, 0xD8)
ALUM_EDGE = (0xA8, 0xA8, 0xA8)
ALUM_HI = (0xFF, 0xFF, 0xFF)
CLIENT = (0xFF, 0xFF, 0xFF)
INK = (0x22, 0x22, 0x22)
ACCENT = (0x28, 0x68, 0xC8)  # Ooze blue
CLOSE = (0xFF, 0x5F, 0x56)
MINI = (0xFF, 0xBC, 0x2E)
ZOOM = (0x27, 0xC9, 0x40)
PIN_STRIDE = 4


def argb(a: int, r: int, g: int, b: int) -> int:
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF)


def rgb(r: int, g: int, b: int, a: int = 255) -> int:
    return argb(a, r, g, b)


def lerp(a: tuple, b: tuple, t: float) -> tuple[int, int, int]:
    return (
        int(a[0] + (b[0] - a[0]) * t),
        int(a[1] + (b[1] - a[1]) * t),
        int(a[2] + (b[2] - a[2]) * t),
    )


def hex_of(c: tuple[int, int, int]) -> str:
    return f"#{c[0]:02x}{c[1]:02x}{c[2]:02x}"


class Img:
    def __init__(self, w: int, h: int, caps=(0, 0, 0, 0), pos=(0, 0, 0, 0), fill=0):
        self.w, self.h = w, h
        self.caps = list(caps)
        self.pos = list(pos)
        self.px = [fill] * (w * h)

    def set(self, x: int, y: int, c: int) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def get(self, x: int, y: int) -> int:
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return 0

    def fill_rect(self, x: int, y: int, w: int, h: int, c: int) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def blend(self, x: int, y: int, r: int, g: int, b: int, a: float) -> None:
        if a <= 0:
            return
        if a >= 1:
            self.set(x, y, rgb(r, g, b))
            return
        p = self.get(x, y)
        if ((p >> 24) & 0xFF) == 0:
            self.set(x, y, rgb(r, g, b, int(255 * a)))
            return
        br, bg, bb = (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
        self.set(
            x,
            y,
            rgb(
                int(br + (r - br) * a),
                int(bg + (g - bg) * a),
                int(bb + (b - bb) * a),
            ),
        )


def write_skimg(path: Path, img: Img) -> None:
    with path.open("wb") as f:
        f.write(b"SKIM")
        f.write(struct.pack("<HH", img.w, img.h))
        f.write(bytes(img.caps))
        f.write(bytes(img.pos))
        for p in img.px:
            f.write(struct.pack("<I", p))


def write_png(path: Path, img: Img) -> None:
    w, h = img.w, img.h
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            p = img.px[y * w + x]
            a, r, g, b = (p >> 24) & 0xFF, (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
            raw.extend((r, g, b, a))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(
            ">I", zlib.crc32(tag + data) & 0xFFFFFFFF
        )

    comp = zlib.compress(bytes(raw), 9)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", comp)
        + chunk(b"IEND", b"")
    )


def pinstripe_fill(img: Img, x0: int, y0: int, w: int, h: int, base=ALUM, alpha=0.07) -> None:
    hi = ALUM_HI
    for y in range(y0, y0 + h):
        row = lerp(base, ALUM_DARK, 0.08) if (y - y0) % 2 else base
        stripe = ((y - y0) % PIN_STRIDE) == 0
        for x in range(x0, x0 + w):
            c = row
            if stripe:
                c = lerp(row, hi, alpha / 0.07 * 0.55 if alpha < 0.07 else 0.55)
            # slight vertical bevel toward edges of band
            img.set(x, y, rgb(*c))


def draw_circle(img: Img, cx: float, cy: float, rad: float, color: tuple, gloss=True) -> None:
    r0 = rad
    for y in range(img.h):
        for x in range(img.w):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            if d > r0 + 0.6:
                continue
            edge = max(0.0, min(1.0, r0 + 0.55 - d))
            # sphere shading
            nx, ny = dx / r0, dy / r0
            shade = 0.55 + 0.45 * max(0.0, 1.0 - (nx * nx + (ny + 0.35) * (ny + 0.35)))
            cr, cg, cb = (
                int(color[0] * shade),
                int(color[1] * shade),
                int(color[2] * shade),
            )
            # rim darken
            if d > r0 - 1.1:
                cr, cg, cb = lerp((cr, cg, cb), (40, 40, 40), 0.35)
            if gloss and d < r0 * 0.55 and ny < -0.1:
                gamt = max(0.0, 0.55 - d / (r0 * 0.55))
                cr, cg, cb = lerp((cr, cg, cb), (255, 255, 255), gamt * 0.55)
            a = int(255 * edge)
            if a >= 250:
                img.set(x, y, rgb(cr, cg, cb))
            else:
                img.blend(x, y, cr, cg, cb, a / 255.0)


def traffic_light(color: tuple, size=14, lit=True, pos=(0, 0, 0, 0)) -> Img:
    img = Img(size, size, pos=pos, fill=0)
    c = color if lit else lerp(color, (180, 180, 180), 0.55)
    draw_circle(img, size / 2, size / 2, size / 2 - 0.8, c, gloss=lit)
    if not lit:
        # muted
        for i, p in enumerate(img.px):
            if (p >> 24) & 0xFF:
                r, g, b = (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
                img.px[i] = rgb(r, g, b, 160)
    return img


def gel_frame(focused: bool) -> Img:
    # 9-slice with 1px middle band: w = L+1+R, h = T+1+B
    # title band 22, sides 2, bottom 2 → caps [2,22,2,2] wait — positions are
    # frame thickness; caps for 9-slice stretch. Milk: size 19×25 caps [9,22,9,2]
    # positions [2,22,2,2]. Mid band: 19-9-9=1, 25-22-2=1. Good.
    L, T, R, B = 9, 22, 9, 2
    w, h = L + 1 + R, T + 1 + B  # 19×25
    img = Img(w, h, caps=(L, T, R, B), pos=(2, 22, 2, 2), fill=0)
    base = ALUM if focused else lerp(ALUM, (200, 200, 200), 0.25)

    # Fill entire frame plate with aluminum
    for y in range(h):
        for x in range(w):
            img.set(x, y, rgb(*base))

    # Title band pinstripes (top T rows)
    pinstripe_fill(img, 0, 0, w, T, base=base, alpha=0.08 if focused else 0.04)

    # Side/bottom rails: flatter aluminum
    for y in range(T, h):
        for x in range(w):
            img.set(x, y, rgb(*lerp(base, ALUM_DARK, 0.12)))

    # Outer rim
    rim_hi = ALUM_HI if focused else (230, 230, 230)
    rim_lo = ALUM_EDGE
    for x in range(w):
        img.set(x, 0, rgb(*rim_hi))
        img.set(x, h - 1, rgb(*rim_lo))
    for y in range(h):
        img.set(0, y, rgb(*rim_hi))
        img.set(w - 1, y, rgb(*rim_lo))

    # Inner client edge (under title / inside sides)
    # The stretchable mid pixel column/row carries into the frame interior.
    for x in range(L, L + 1):
        for y in range(T, h - B):
            img.set(x, y, rgb(*CLIENT))
    for y in range(T, T + 1):
        for x in range(L, w - R):
            # under-title hairline
            img.set(x, y, rgb(*ALUM_EDGE))

    # Soft bottom of title
    for x in range(w):
        img.set(x, T - 1, rgb(*lerp(base, ALUM_EDGE, 0.5)))

    if focused:
        # faint blue wash on title for focus read
        for y in range(T):
            for x in range(w):
                p = img.get(x, y)
                r, g, b = (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
                nr, ng, nb = lerp((r, g, b), ACCENT, 0.04)
                img.set(x, y, rgb(nr, ng, nb))
    return img


def aqua_button(w: int, h: int, caps, state: str) -> Img:
    """Rounded gel button; state: normal|hilited|disabled."""
    img = Img(w, h, caps=caps, fill=0)
    if state == "hilited":
        top, bot = lerp(ACCENT, (255, 255, 255), 0.45), ACCENT
        edge = lerp(ACCENT, (0, 0, 0), 0.35)
    elif state == "disabled":
        top, bot = (0xE8, 0xE8, 0xE8), (0xC8, 0xC8, 0xC8)
        edge = (0xA0, 0xA0, 0xA0)
    else:
        top, bot = (0xFC, 0xFC, 0xFC), (0xD0, 0xD0, 0xD0)
        edge = ALUM_EDGE

    rad = min(h // 2, 6)
    for y in range(h):
        t = y / max(1, h - 1)
        fill = lerp(top, bot, t)
        # gloss band
        if 0.08 < t < 0.42 and state != "disabled":
            fill = lerp(fill, (255, 255, 255), 0.35 * (1 - abs(t - 0.22) / 0.2))
        for x in range(w):
            # rounded rect SDF-ish
            cx = min(x, w - 1 - x)
            cy = min(y, h - 1 - y)
            inside = True
            if cx < rad and cy < rad:
                dx, dy = rad - cx - 0.5, rad - cy - 0.5
                if dx * dx + dy * dy > rad * rad:
                    inside = False
            if not inside:
                continue
            on_edge = cx == 0 or cy == 0 or x == w - 1 or y == h - 1
            if cx < rad and cy < rad:
                dx, dy = rad - cx - 0.5, rad - cy - 0.5
                if abs(math.hypot(dx, dy) - rad) < 1.2:
                    on_edge = True
            img.set(x, y, rgb(*(edge if on_edge else fill)))
    return img


def default_button(state: str) -> Img:
    # Slightly stronger accent pill
    img = aqua_button(28, 23, (14, 11, 13, 11), "hilited" if state != "disabled" else "disabled")
    if state == "normal":
        # tone down from hilited
        img = aqua_button(28, 23, (14, 11, 13, 11), "hilited")
        for i, p in enumerate(img.px):
            if (p >> 24) & 0xFF == 0:
                continue
            r, g, b = (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
            r, g, b = lerp((r, g, b), ACCENT, 0.15)
            img.px[i] = rgb(r, g, b)
    elif state == "disabled":
        img = aqua_button(28, 23, (14, 11, 13, 11), "disabled")
    return img


def column_header(state: str) -> Img:
    # 5×20 with 1px mid: caps [2,10,2,9]
    img = Img(5, 20, caps=(2, 10, 2, 9))
    base = ALUM if state != "hilited" else lerp(ALUM, ACCENT, 0.12)
    if state == "disabled":
        base = (0xDD, 0xDD, 0xDD)
    pinstripe_fill(img, 0, 0, 5, 20, base=base, alpha=0.06)
    for x in range(5):
        img.set(x, 0, rgb(*ALUM_HI))
        img.set(x, 19, rgb(*ALUM_EDGE))
    return img


def scrollbar_track_h(kind: str) -> Img:
    # double 62×15 caps [30,0,31,0] pos travel [30,0,30,0]
    # single 32×15, disabled 32×15, too_small 5×15
    sizes = {
        "double_arrows": (62, 15, (30, 0, 31, 0), (30, 0, 30, 0)),
        "single_arrows": (32, 15, (15, 0, 16, 0), (15, 0, 15, 0)),
        "disabled": (32, 15, (15, 0, 16, 0), (0, 0, 0, 0)),
        "too_small": (5, 15, (2, 0, 2, 0), (0, 0, 0, 0)),
    }
    w, h, caps, pos = sizes[kind]
    img = Img(w, h, caps=caps, pos=pos)
    track = (0xE4, 0xE4, 0xE4) if kind != "disabled" else (0xEE, 0xEE, 0xEE)
    img.fill_rect(0, 0, w, h, rgb(*track))
    for x in range(w):
        img.set(x, 0, rgb(*ALUM_EDGE))
        img.set(x, h - 1, rgb(*ALUM_HI))

    def arrow(x0: int, dir_right: bool) -> None:
        cx, cy = x0 + 7, h // 2
        for dy in range(-4, 5):
            span = 4 - abs(dy)
            for dx in range(span):
                xx = cx + dx if dir_right else cx - dx
                img.set(xx, cy + dy, rgb(*INK))

    if kind == "double_arrows":
        arrow(0, False)
        arrow(15, False)
        arrow(w - 30, True)
        arrow(w - 15, True)
    elif kind in ("single_arrows", "disabled"):
        arrow(0, False)
        arrow(w - 15, True)
        if kind == "disabled":
            for i, p in enumerate(img.px):
                if (p >> 24) & 0xFF:
                    r, g, b = (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
                    img.px[i] = rgb(*lerp((r, g, b), track, 0.5))
    return img


def scrollbar_track_v(kind: str) -> Img:
    sizes = {
        "double_arrows": (15, 62, (0, 30, 0, 31), (0, 30, 0, 30)),
        "single_arrows": (15, 32, (0, 15, 0, 16), (0, 15, 0, 15)),
        "disabled": (15, 32, (0, 15, 0, 16), (0, 0, 0, 0)),
        "too_small": (15, 5, (0, 2, 0, 2), (0, 0, 0, 0)),
    }
    w, h, caps, pos = sizes[kind]
    img = Img(w, h, caps=caps, pos=pos)
    track = (0xE4, 0xE4, 0xE4) if kind != "disabled" else (0xEE, 0xEE, 0xEE)
    img.fill_rect(0, 0, w, h, rgb(*track))
    for y in range(h):
        img.set(0, y, rgb(*ALUM_EDGE))
        img.set(w - 1, y, rgb(*ALUM_HI))

    def arrow(y0: int, dir_down: bool) -> None:
        cx, cy = w // 2, y0 + 7
        for dx in range(-4, 5):
            span = 4 - abs(dx)
            for dy in range(span):
                yy = cy + dy if dir_down else cy - dy
                img.set(cx + dx, yy, rgb(*INK))

    if kind == "double_arrows":
        arrow(0, False)
        arrow(15, False)
        arrow(h - 30, True)
        arrow(h - 15, True)
    elif kind in ("single_arrows", "disabled"):
        arrow(0, False)
        arrow(h - 15, True)
        if kind == "disabled":
            for i, p in enumerate(img.px):
                if (p >> 24) & 0xFF:
                    r, g, b = (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF
                    img.px[i] = rgb(*lerp((r, g, b), track, 0.5))
    return img


def scrollbar_indicator_h(hilited: bool) -> Img:
    img = Img(47, 14, caps=(23, 0, 23, 0))
    face = lerp(ALUM, ACCENT, 0.2) if hilited else ALUM
    for y in range(14):
        t = y / 13
        c = lerp(lerp(face, ALUM_HI, 0.35), lerp(face, ALUM_DARK, 0.25), t)
        for x in range(47):
            img.set(x, y, rgb(*c))
    for x in range(47):
        img.set(x, 0, rgb(*ALUM_EDGE))
        img.set(x, 13, rgb(*ALUM_EDGE))
    # grips dots
    for gx in (20, 23, 26):
        for gy in (5, 7, 9):
            img.set(gx, gy, rgb(*ALUM_EDGE))
    return img


def scrollbar_indicator_v(hilited: bool) -> Img:
    img = Img(14, 47, caps=(0, 23, 0, 23))
    face = lerp(ALUM, ACCENT, 0.2) if hilited else ALUM
    for x in range(14):
        t = x / 13
        c = lerp(lerp(face, ALUM_HI, 0.35), lerp(face, ALUM_DARK, 0.25), t)
        for y in range(47):
            img.set(x, y, rgb(*c))
    for y in range(47):
        img.set(0, y, rgb(*ALUM_EDGE))
        img.set(13, y, rgb(*ALUM_EDGE))
    for gy in (20, 23, 26):
        for gx in (5, 7, 9):
            img.set(gx, gy, rgb(*ALUM_EDGE))
    return img


def grips_h() -> Img:
    img = Img(6, 15)
    img.fill_rect(0, 0, 6, 15, rgb(*ALUM))
    for x in (2, 3):
        for y in range(3, 12):
            img.set(x, y, rgb(*ALUM_EDGE))
    return img


def grips_v() -> Img:
    img = Img(15, 6)
    img.fill_rect(0, 0, 15, 6, rgb(*ALUM))
    for y in (2, 3):
        for x in range(3, 12):
            img.set(x, y, rgb(*ALUM_EDGE))
    return img


def arrow_hilite(size=15) -> Img:
    img = Img(size, size)
    img.fill_rect(0, 0, size, size, rgb(*lerp(ALUM, ACCENT, 0.25)))
    return img


def tick_box(ticked: bool, state: str) -> Img:
    img = Img(14, 14)
    face = ALUM if state != "disabled" else (0xE0, 0xE0, 0xE0)
    img.fill_rect(0, 0, 14, 14, rgb(*face))
    for i in range(14):
        img.set(i, 0, rgb(*ALUM_EDGE))
        img.set(i, 13, rgb(*ALUM_EDGE))
        img.set(0, i, rgb(*ALUM_EDGE))
        img.set(13, i, rgb(*ALUM_EDGE))
    if ticked:
        mark = ACCENT if state == "hilited" else INK
        # simple check
        pts = [(3, 7), (4, 8), (5, 9), (6, 8), (7, 7), (8, 6), (9, 5), (10, 4)]
        for x, y in pts:
            img.set(x, y, rgb(*mark))
            img.set(x, y + 1, rgb(*mark))
    return img


def mutex_box(ticked: bool, state: str) -> Img:
    img = Img(14, 14, fill=0)
    face = ALUM if state != "disabled" else (0xE0, 0xE0, 0xE0)
    draw_circle(img, 7, 7, 6.2, face, gloss=False)
    if ticked:
        draw_circle(img, 7, 7, 3.2, ACCENT if state == "hilited" else INK, gloss=False)
    return img


def progress_bar() -> Img:
    img = Img(40, 12, caps=(4, 5, 4, 5))
    img.fill_rect(0, 0, 40, 12, rgb(0xE8, 0xE8, 0xE8))
    for x in range(40):
        img.set(x, 0, rgb(*ALUM_EDGE))
        img.set(x, 11, rgb(*ALUM_EDGE))
    return img


def progress_fill() -> Img:
    img = Img(40, 12, caps=(4, 5, 4, 5))
    for y in range(12):
        t = y / 11
        c = lerp(lerp(ACCENT, (255, 255, 255), 0.35), ACCENT, t)
        for x in range(40):
            img.set(x, y, rgb(*c))
    return img


def slider_bar_h(state: str) -> Img:
    img = Img(40, 5, caps=(2, 2, 2, 2))
    c = ACCENT if state == "hilited" else ALUM_EDGE
    img.fill_rect(0, 1, 40, 3, rgb(*c))
    return img


def slider_ind_h(state: str, pointed=False) -> Img:
    h = 16 if pointed else 14
    img = Img(12, h, fill=0)
    col = ACCENT if state == "hilited" else ALUM
    if pointed:
        # triangle-ish thumb
        for y in range(h):
            span = max(1, 6 - abs(y - 6) // 2)
            for x in range(6 - span, 6 + span):
                img.set(x, y, rgb(*col))
    else:
        draw_circle(img, 6, h / 2, 5.5, col)
    return img


def slider_bar_v(state: str) -> Img:
    img = Img(5, 40, caps=(2, 2, 2, 2))
    c = ACCENT if state == "hilited" else ALUM_EDGE
    img.fill_rect(1, 0, 3, 40, rgb(*c))
    return img


def slider_ind_v(state: str, pointed=False) -> Img:
    w = 16 if pointed else 14
    img = Img(w, 12, fill=0)
    col = ACCENT if state == "hilited" else ALUM
    if pointed:
        for x in range(w):
            span = max(1, 6 - abs(x - 6) // 2)
            for y in range(6 - span, 6 + span):
                img.set(x, y, rgb(*col))
    else:
        draw_circle(img, w / 2, 6, 5.5, col)
    return img


def popup_btn(state: str) -> Img:
    img = aqua_button(40, 20, (12, 9, 18, 9), state)
    # disclosure triangle on the right
    ink = INK if state != "disabled" else (0x99, 0x99, 0x99)
    for dy, span in enumerate((0, 1, 2, 3, 2, 1, 0)):
        for dx in range(span + 1):
            img.set(30 + dx, 6 + dy, rgb(*ink))
    return img


def popup_symbol(state: str) -> Img:
    img = Img(12, 12, fill=0)
    ink = ACCENT if state == "hilited" else INK
    for dy, span in enumerate((0, 1, 2, 3, 4)):
        for dx in range(-span, span + 1):
            img.set(6 + dx, 3 + dy, rgb(*ink))
    return img


def popup_frame() -> Img:
    img = Img(3, 3, caps=(1, 1, 1, 1))
    img.fill_rect(0, 0, 3, 3, rgb(*ALUM_EDGE))
    img.set(1, 1, rgb(*CLIENT))
    return img


def resize_grip() -> Img:
    img = Img(12, 12, fill=0)
    for i in range(4):
        for j in range(3):
            x, y = 8 - i * 2 + j, 8 + i
            if 0 <= x < 12 and 0 <= y < 12:
                img.set(x, y, rgb(*ALUM_EDGE))
    return img


def wonderlight(kind: str) -> Img:
    colors = {
        "off": (0x88, 0x88, 0x88),
        "pause": (0xFF, 0xBC, 0x2E),
        "ready": ACCENT,
        "go": ZOOM,
        "finished": CLOSE,
        "flash_off": (0x66, 0x66, 0x66),
        "flash_on1": (0xFF, 0xFF, 0xAA),
        "flash_on2": (0xFF, 0xEE, 0x66),
    }
    img = Img(16, 16, fill=0)
    draw_circle(img, 8, 8, 6.5, colors.get(kind, ACCENT))
    return img


def disclosure(plus: bool, medium: bool) -> Img:
    s = 13 if medium else 11
    img = Img(s, s)
    img.fill_rect(0, 0, s, s, rgb(*ALUM))
    for i in range(s):
        img.set(i, 0, rgb(*ALUM_EDGE))
        img.set(i, s - 1, rgb(*ALUM_EDGE))
        img.set(0, i, rgb(*ALUM_EDGE))
        img.set(s - 1, i, rgb(*ALUM_EDGE))
    m = s // 2
    for x in range(3, s - 3):
        img.set(x, m, rgb(*INK))
    if plus:
        for y in range(3, s - 3):
            img.set(m, y, rgb(*INK))
    return img


def separator(h: bool) -> Img:
    if h:
        img = Img(8, 2, caps=(3, 0, 3, 0))
        img.fill_rect(0, 0, 8, 1, rgb(*ALUM_EDGE))
        img.fill_rect(0, 1, 8, 1, rgb(*ALUM_HI))
    else:
        img = Img(2, 8, caps=(0, 3, 0, 3))
        img.fill_rect(0, 0, 1, 8, rgb(*ALUM_EDGE))
        img.fill_rect(1, 0, 1, 8, rgb(*ALUM_HI))
    return img


def box_plate() -> Img:
    img = Img(12, 12, caps=(3, 3, 3, 3))
    img.fill_rect(0, 0, 12, 12, rgb(*CLIENT))
    for i in range(12):
        img.set(i, 0, rgb(*ALUM_EDGE))
        img.set(i, 11, rgb(*ALUM_EDGE))
        img.set(0, i, rgb(*ALUM_EDGE))
        img.set(11, i, rgb(*ALUM_EDGE))
    return img


def focus_box() -> Img:
    img = Img(8, 8, caps=(3, 3, 3, 3))
    for i in range(8):
        img.set(i, 0, rgb(*ACCENT))
        img.set(i, 7, rgb(*ACCENT))
        img.set(0, i, rgb(*ACCENT))
        img.set(7, i, rgb(*ACCENT))
    return img


def icon16(folder: bool) -> Img:
    img = Img(16, 16, fill=0)
    if folder:
        img.fill_rect(1, 4, 14, 10, rgb(0xF0, 0xD0, 0x60))
        img.fill_rect(1, 3, 6, 3, rgb(0xE8, 0xC0, 0x40))
    else:
        img.fill_rect(3, 1, 10, 14, rgb(*CLIENT))
        for i in range(1, 15):
            img.set(3, i, rgb(*ALUM_EDGE))
            img.set(12, i, rgb(*ALUM_EDGE))
        img.fill_rect(3, 1, 10, 1, rgb(*ALUM_EDGE))
        img.fill_rect(3, 14, 10, 1, rgb(*ALUM_EDGE))
        for y in (5, 7, 9):
            img.fill_rect(5, y, 6, 1, rgb(0xCC, 0xCC, 0xCC))
    return img


def icon32(folder: bool) -> Img:
    img = Img(32, 32, fill=0)
    if folder:
        img.fill_rect(2, 8, 28, 20, rgb(0xF0, 0xD0, 0x60))
        img.fill_rect(2, 6, 12, 6, rgb(0xE8, 0xC0, 0x40))
    else:
        img.fill_rect(6, 2, 20, 28, rgb(*CLIENT))
        for i in range(2, 30):
            img.set(6, i, rgb(*ALUM_EDGE))
            img.set(25, i, rgb(*ALUM_EDGE))
        for y in (10, 14, 18, 22):
            img.fill_rect(10, y, 12, 1, rgb(0xCC, 0xCC, 0xCC))
    return img


def transition_stops(n: int, a, b) -> list[str]:
    return [hex_of(lerp(a, b, i / max(1, n - 1))) for i in range(n)]


def build() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    arts: dict[str, Img] = {}

    # Gel frame + traffic lights (Mac order: close, minimize, zoom)
    arts["window.frame.normal"] = gel_frame(False)
    arts["window.frame.focus"] = gel_frame(True)

    # 14px lights; title 22 → y=4. Gaps ~8px between.
    # close @ x=10, mini @ 10+14+8=32, zoom @ 32+14+8=54
    for state, lit in (("normal", True), ("focus", True), ("hilited", True)):
        arts[f"window.close.{state}"] = traffic_light(CLOSE, 14, lit, pos=(10, 4, 0, 0))
        arts[f"window.minimize.{state}"] = traffic_light(MINI, 14, lit, pos=(32, 4, 0, 0))
        arts[f"window.maximize.{state}"] = traffic_light(ZOOM, 14, lit, pos=(54, 4, 0, 0))
    arts["window.close.disabled"] = traffic_light(CLOSE, 14, False, pos=(10, 4, 0, 0))
    arts["window.minimize.disabled"] = traffic_light(MINI, 14, False, pos=(32, 4, 0, 0))
    arts["window.maximize.disabled"] = traffic_light(ZOOM, 14, False, pos=(54, 4, 0, 0))

    # Invisible menu stub so kit doesn't invent a hatch between lights
    stub = Img(1, 1, pos=(2, 2, 0, 0), fill=0)
    for s in ("normal", "focus", "hilited", "disabled"):
        arts[f"window.menu.{s}"] = stub

    arts["window.resize.normal"] = resize_grip()
    arts["window.resize.focus"] = resize_grip()
    arts["window.resize.focus"].pos = [0, 0, 0, 0]

    # Buttons
    for state in ("normal", "hilited", "disabled"):
        caps = (13, 11, 12, 11) if state != "hilited" else (15, 11, 15, 11)
        wh = (26, 23) if state != "hilited" else (31, 23)
        arts[f"button.{state}"] = aqua_button(wh[0], wh[1], caps, state)
        arts[f"default_button.{state}"] = default_button(state)
        arts[f"icon_button.{state}"] = aqua_button(22, 22, (10, 10, 10, 10), state)
        arts[f"popup.{state}"] = popup_btn(state)
        arts[f"popup.no_title.{state}"] = aqua_button(20, 20, (9, 9, 9, 9), state)
        arts[f"popup.symbol.{state}"] = popup_symbol(state)

    for state in ("normal", "hilited", "disabled"):
        arts[f"tick.blank.{state}"] = tick_box(False, state)
        arts[f"tick.ticked.{state}"] = tick_box(True, state)
        arts[f"tick.tristate.{state}"] = tick_box(True, state)
        arts[f"mutex.blank.{state}"] = mutex_box(False, state)
        arts[f"mutex.ticked.{state}"] = mutex_box(True, state)
        arts[f"mutex.tristate.{state}"] = mutex_box(True, state)

    arts["disclosure.plus.small"] = disclosure(True, False)
    arts["disclosure.minus.small"] = disclosure(False, False)
    arts["disclosure.plus.medium"] = disclosure(True, True)
    arts["disclosure.minus.medium"] = disclosure(False, True)

    arts["focus_box.hilited"] = focus_box()
    arts["focus_box.disabled"] = focus_box()
    arts["separator.h"] = separator(True)
    arts["separator.v"] = separator(False)
    arts["box"] = box_plate()
    arts["framed_raised"] = box_plate()
    arts["progress.bar"] = progress_bar()
    arts["progress.fill"] = progress_fill()

    for state in ("normal", "hilited", "disabled"):
        arts[f"slider.h.bar.{state}"] = slider_bar_h(state)
        arts[f"slider.h.indicator.{state}"] = slider_ind_h(state, False)
        arts[f"slider.h.indicator_pointed.{state}"] = slider_ind_h(state, True)
        arts[f"slider.v.bar.{state}"] = slider_bar_v(state)
        arts[f"slider.v.indicator.{state}"] = slider_ind_v(state, False)
        arts[f"slider.v.indicator_pointed.{state}"] = slider_ind_v(state, True)

    for state in ("normal", "hilited", "disabled"):
        arts[f"column_header.{state}"] = column_header(state)

    for kind in ("double_arrows", "single_arrows", "disabled", "too_small"):
        arts[f"scrollbar.h.{kind}"] = scrollbar_track_h(kind)
        arts[f"scrollbar.v.{kind}"] = scrollbar_track_v(kind)
    for state in ("normal", "hilited"):
        arts[f"scrollbar.h.indicator.{state}"] = scrollbar_indicator_h(state == "hilited")
        arts[f"scrollbar.v.indicator.{state}"] = scrollbar_indicator_v(state == "hilited")
        arts[f"scrollbar.h.grips.{state}"] = grips_h()
        arts[f"scrollbar.v.grips.{state}"] = grips_v()

    for name in (
        "first_left",
        "first_right",
        "second_left",
        "second_right",
        "single_left",
        "single_right",
    ):
        arts[f"scrollbar.h.arrow_hilite.{name}"] = arrow_hilite()
    for name in (
        "first_up",
        "first_down",
        "second_up",
        "second_down",
        "single_up",
        "single_down",
    ):
        arts[f"scrollbar.v.arrow_hilite.{name}"] = arrow_hilite()

    arts["popup_frame.normal"] = popup_frame()
    arts["popup_frame.focus"] = popup_frame()

    for kind in (
        "off",
        "pause",
        "ready",
        "go",
        "finished",
        "flash_off",
        "flash_on1",
        "flash_on2",
    ):
        arts[f"wonderlight.{kind}"] = wonderlight(kind)

    icons = {
        "file.generic.16": icon16(False),
        "file.generic.32": icon32(False),
        "folder.16": icon16(True),
        "folder.32": icon32(True),
    }

    # Write assets
    file_map: dict[str, str] = {}
    for key, img in arts.items():
        fname = key.replace(".", "_") + ".skimg"
        write_skimg(OUT / fname, img)
        write_png(OUT / (key.replace(".", "_") + ".png"), img)
        file_map[key] = fname

    icon_map: dict[str, str] = {}
    for key, img in icons.items():
        fname = "icon_" + key.replace(".", "_") + ".skimg"
        write_skimg(OUT / fname, img)
        write_png(OUT / (fname.replace(".skimg", ".png")), img)
        icon_map[key] = fname

    # Colours
    win_trans = transition_stops(18, (0xE8, 0xE8, 0xE8), ALUM)
    win_focus_trans = transition_stops(18, lerp(ALUM, ACCENT, 0.08), ALUM)

    def art_block(key: str, img: Img) -> str:
        fname = file_map[key]
        caps = ", ".join(str(c) for c in img.caps)
        pos = ", ".join(str(c) for c in img.pos)
        return (
            f'[art."{key}"]\n'
            f'file = "{fname}"\n'
            f"caps = [{caps}]\n"
            f"positions = [{pos}]\n"
        )

    lines = [
        "# SagradoKit skin — Ooze look (original art; aluminum gel + traffic lights)",
        'format = "sap"',
        "version = 1",
        "",
        "[meta]",
        'name = "Ooze"',
        'creator = "SagradoKit"',
        'description = "Aqua-inspired aluminum gel, pinstripes, and traffic lights"',
        'version = "1.0"',
        "",
        "[colors.primary]",
        f'background = "{hex_of(CLIENT)}"',
        f'dark = "{hex_of(ALUM_EDGE)}"',
        'disable_frame = "#bbbbbb"',
        'disable_label = "#999999"',
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'label = "{hex_of(INK)}"',
        f'light = "{hex_of(ALUM_HI)}"',
        "",
        "[colors.focus]",
        f'box = "{hex_of(ACCENT)}"',
        "",
        "[colors.text]",
        f'background = "{hex_of(CLIENT)}"',
        f'foreground = "{hex_of(INK)}"',
        f'hilite_background = "{hex_of(ACCENT)}"',
        'hilite_foreground = "#ffffff"',
        f'insertion_point = "{hex_of(ACCENT)}"',
        "",
        "[colors.list]",
        f'background = "{hex_of(CLIENT)}"',
        f'label = "{hex_of(INK)}"',
        f'hilite_background = "{hex_of(ACCENT)}"',
        'hilite_foreground = "#ffffff"',
        'sort_column_background = "#f6f6f6"',
        f'separator = "{hex_of(ALUM_DARK)}"',
        "",
        "[colors.button]",
        f'light2 = "{hex_of(ALUM_HI)}"',
        f'light1 = "{hex_of(ALUM)}"',
        f'face = "{hex_of(ALUM_DARK)}"',
        f'dark1 = "{hex_of(ALUM_EDGE)}"',
        'dark2 = "#888888"',
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'label = "{hex_of(INK)}"',
        "",
        "[colors.button_disable]",
        'light2 = "#f0f0f0"',
        'light1 = "#e0e0e0"',
        'face = "#d8d8d8"',
        'dark1 = "#c0c0c0"',
        'dark2 = "#b0b0b0"',
        'frame = "#bbbbbb"',
        'label = "#999999"',
        "",
        "[colors.button_hilite]",
        f'light2 = "{hex_of(lerp(ACCENT, (255,255,255), 0.4))}"',
        f'light1 = "{hex_of(lerp(ACCENT, (255,255,255), 0.2))}"',
        f'face = "{hex_of(ACCENT)}"',
        f'dark1 = "{hex_of(lerp(ACCENT, (0,0,0), 0.2))}"',
        f'dark2 = "{hex_of(lerp(ACCENT, (0,0,0), 0.35))}"',
        f'frame = "{hex_of(lerp(ACCENT, (0,0,0), 0.4))}"',
        'label = "#ffffff"',
        "",
        "[colors.default_button]",
        f'light = "{hex_of(lerp(ACCENT, (255,255,255), 0.35))}"',
        f'face = "{hex_of(ACCENT)}"',
        f'dark = "{hex_of(lerp(ACCENT, (0,0,0), 0.25))}"',
        f'frame = "{hex_of(lerp(ACCENT, (0,0,0), 0.4))}"',
        "",
        "[colors.window]",
        f'light2 = "{hex_of(ALUM_HI)}"',
        f'light1 = "{hex_of(ALUM)}"',
        f'face = "{hex_of(ALUM)}"',
        f'dark1 = "{hex_of(ALUM_DARK)}"',
        f'dark2 = "{hex_of(ALUM_EDGE)}"',
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'label = "{hex_of(INK)}"',
        "transition = [",
        *[f'  "{c}",' for c in win_trans],
        "]",
        "",
        "[colors.window_focus]",
        f'light2 = "{hex_of(ALUM_HI)}"',
        f'light1 = "{hex_of(ALUM)}"',
        f'face = "{hex_of(ALUM)}"',
        f'dark1 = "{hex_of(ALUM_DARK)}"',
        f'dark2 = "{hex_of(ALUM_EDGE)}"',
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'label = "{hex_of(INK)}"',
        "transition = [",
        *[f'  "{c}",' for c in win_focus_trans],
        "]",
        "",
        "[colors.scrollbar]",
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'light = "{hex_of(ALUM_HI)}"',
        f'face = "{hex_of(ALUM)}"',
        f'dark = "{hex_of(ALUM_EDGE)}"',
        f'label = "{hex_of(INK)}"',
        f'hilite_light = "{hex_of(lerp(ACCENT, (255,255,255), 0.3))}"',
        f'hilite = "{hex_of(ACCENT)}"',
        f'hilite_dark = "{hex_of(lerp(ACCENT, (0,0,0), 0.25))}"',
        'hilite_label = "#ffffff"',
        f'indicator_light = "{hex_of(ALUM_HI)}"',
        f'indicator = "{hex_of(ALUM)}"',
        f'indicator_dark = "{hex_of(ALUM_DARK)}"',
        'track_light2 = "#ececec"',
        'track_light1 = "#e4e4e4"',
        'track = "#e0e0e0"',
        'track_dark1 = "#d0d0d0"',
        'track_dark2 = "#c0c0c0"',
        "",
        "[colors.column_header]",
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'light = "{hex_of(ALUM_HI)}"',
        f'face = "{hex_of(ALUM)}"',
        f'dark = "{hex_of(ALUM_DARK)}"',
        f'label = "{hex_of(INK)}"',
        f'hilite_light = "{hex_of(lerp(ACCENT, (255,255,255), 0.3))}"',
        f'hilite = "{hex_of(ACCENT)}"',
        f'hilite_dark = "{hex_of(lerp(ACCENT, (0,0,0), 0.2))}"',
        'hilite_label = "#ffffff"',
        "",
        "[colors.menu]",
        f'background = "{hex_of(ALUM)}"',
        f'label = "{hex_of(INK)}"',
        'disabled_label = "#999999"',
        f'hilite_background = "{hex_of(ACCENT)}"',
        'hilite_label = "#ffffff"',
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'separator = "{hex_of(ALUM_DARK)}"',
        "",
        "[colors.progress]",
        'bkgnd = "#e8e8e8"',
        'bkgnd_dark = "#d0d0d0"',
        'bkgnd_light = "#f5f5f5"',
        f'frame = "{hex_of(ALUM_EDGE)}"',
        f'label = "{hex_of(ACCENT)}"',
        "transition = [",
        *[f'  "{c}",' for c in transition_stops(10, lerp(ACCENT, (255, 255, 255), 0.4), ACCENT)],
        "]",
        "",
        "[colors.slider]",
        'bar = "#d0d0d0"',
        f'bar_frame = "{hex_of(ALUM_EDGE)}"',
        f'bar_hilite = "{hex_of(ACCENT)}"',
        f'bar_hilite_frame = "{hex_of(lerp(ACCENT, (0,0,0), 0.3))}"',
        'disable = "#e0e0e0"',
        'disable_dark = "#d0d0d0"',
        'disable_frame = "#bbbbbb"',
        'disable_light = "#f0f0f0"',
        f'indicator = "{hex_of(ALUM)}"',
        f'indicator_dark = "{hex_of(ALUM_DARK)}"',
        f'indicator_frame = "{hex_of(ALUM_EDGE)}"',
        f'indicator_hilite = "{hex_of(ACCENT)}"',
        f'indicator_hilite_dark = "{hex_of(lerp(ACCENT, (0,0,0), 0.2))}"',
        f'indicator_hilite_frame = "{hex_of(lerp(ACCENT, (0,0,0), 0.35))}"',
        f'indicator_hilite_light = "{hex_of(lerp(ACCENT, (255,255,255), 0.3))}"',
        f'indicator_light = "{hex_of(ALUM_HI)}"',
        "",
        "[colors.workspace]",
        'background1 = "#c8d4e8"',
        'background2 = "#a8bcd8"',
        "",
        "[colors.important]",
        f'label = "{hex_of(CLOSE)}"',
        "",
        "[colors.file_label]",
        f'label = "{hex_of(INK)}"',
        "",
    ]

    # Prefer a stable art order (milk-like)
    order = sorted(arts.keys())
    for key in order:
        lines.append(art_block(key, arts[key]))

    lines.append("[icons]")
    for key, fname in sorted(icon_map.items()):
        lines.append(f'{key} = "{fname}"')
    lines.append("")

    (OUT / "ooze.sap").write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT}/ooze.sap with {len(arts)} art slots + {len(icons)} icons")


if __name__ == "__main__":
    build()
