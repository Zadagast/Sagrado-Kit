# SagradoKit

The authoritative appearance kit for native apps — built the way Haxial built
theirs.

- **Software framebuffer UI** — every pixel painted by the kit, blitted to the
  window with one GDI call (`SetDIBitsToDevice`). No OS widgets, no CSS.
- **One Appearance Engine** every window speaks (`engine/`).
- **One skin format** the engine loads (`.sap` — Sagrado Appearance; named
  colour roles; art/icon slots reserved). **Also loads Haxial `.hap` live**
  (colour table + image slots mapped into the same painters).
- **One editor** that authors that format against a live kit preview
  (`editor/`), like AppearanceEdit. **Load** accepts `.hap` or `.sap`.
- **Sagrado Apps standard** — Sagrado TextEdit (`apps/textedit/`) is the
  reference consumer: Haxial-shaped gel, kit-only paint, clip-don’t-hide,
  live `.hap`/`.sap`. Sagrado Jabber (`apps/jabber/`) is the second consumer
  (AIM-era IM on XMPP). Later apps copy that bar
  ([`docs/contract.md`](docs/contract.md), [`docs/jabber.md`](docs/jabber.md)).

Incomplete skins are valid. Token resolution: **art → colour → stock**.

This is not an Ooze project and not a web/Electron/npm app.

## Layout

```
format/          schema + stock / example skins
engine/          Appearance Engine (load, resolve, paint)
editor/          Win32 SagradoKit Editor
apps/textedit/   Sagrado TextEdit — reference Sagrado App
apps/jabber/     Sagrado Jabber — “You’ve Got Mail” IM (XMPP)
docs/            contract + lessons from HAP
```

Read [`docs/contract.md`](docs/contract.md) for the system contract and
**Sagrado Apps standard**.
Haxial surface map (how colours + images plug into controls — research before
art paint): [`docs/haxial-surface-map.md`](docs/haxial-surface-map.md).
Short lessons: [`docs/lessons-from-hap.md`](docs/lessons-from-hap.md).

## Stack

| Piece | Choice |
|---|---|
| Language | C++17 |
| Host | Win32 (native Windows, or Wine on Linux/macOS) |
| Drawing | Software framebuffer → `SetDIBitsToDevice` |
| Skin | Named `.sap` (schema in `format/schema.json`) |

## Everyday loop (stay on main)

```sh
git checkout main
git pull
make
make run           # Sagrado Jabber under Wine
```

Do **not** check out old draft feature branches for daily use — they diverge and
feel like “an old/different version.” Review PRs only when reviewing. Roadmap:
[`docs/roadmap.md`](docs/roadmap.md).

## Build

### Linux (cross-compile + Wine)

```sh
# Debian/Ubuntu
sudo apt install g++-mingw-w64-i686 wine

make               # → Editor + TextEdit + Jabber + bundled skins
make run           # Sagrado Jabber under Wine (prefers wine64)
make run-jabber    # same as make run
make run-textedit  # Sagrado TextEdit under Wine
make run-editor    # kit Appearance editor under Wine
# without Wine: copy the .exe to Windows, or install WineHQ and use wine64
```

Gel apps use a borderless `WS_POPUP` (no OS caption / sysmenu styles) so Wine
does not stack a host title bar on top of the kit gel. `WM_NCCALCSIZE` claims
the full window as client; startup nudges size by 1px so host decorations drop
without wedging hit-testing. Move/resize use gel `WM_NCHITTEST` (not
`DefWindowProc`'s HTCLIENT gate).


### Windows

With MinGW-w64 (or any C++17 toolchain that can link `gdi32`, `user32`,
`comdlg32`):

```sh
make
# or:
i686-w64-mingw32-g++ -std=c++17 -O2 -Iengine editor/main.cpp \
  -o build/SagradoKitEditor.exe -mwindows -lgdi32 -luser32 -lcomdlg32
```

Run `build/SagradoKitEditor.exe`. `make skins` ships **all** appearances next to
the binaries under `build/format/skins/` (every `research/haps/*.hap`, plus SAP
packs such as `milk-redux/`, `ooze/`, `completion/`). Apps do not need
`research/haps/` at runtime. Cold start prefers **Gamespot-1100**; pick others from
**Appearance** (Jabber / TextEdit) or the editor **Themes** button.
It also ships `ooze/ooze.sap` — aluminum gel, pinstripes, and traffic lights
(`python3 research/build_ooze_theme.py` to regenerate) — load via Appearance.

## Editor

- **Load / Save** — `.sap` files (same format apps load); **Load** also accepts `.hap`.
  Save after a Hap load writes a full `.sap` + `.skimg` art (Hap→Sap parity).
- **Themes** — menu of bundled skins from `format/skins/` (wheel to scroll)
- **Stock** — reset to built-in last-resort colours
- **Colour Roles** — scrollable named swatches; drag R/G/B sliders
- **Kit Preview** — live gel + controls (icon buttons, menu bar, Find, File
  Transfers, fields, sliders, list/scroll). Generic push buttons use Hap’s usual
  **20px** height; Find dialog keeps TextEdit-measured **24px**.

Shortcuts: `Ctrl+O` load, `Ctrl+S` save, `Esc` quit. Drag the title bar to
move; the close box quits.

## Sagrado TextEdit

Haxial TextEdit-shaped plain-text editor — reference Sagrado App (the standard
later apps follow).

- Gel main window (close / Window Menu → Minimize·Zoom·Close / max / min / grow box)
- Menu bar: File, Edit, Find, Appearance, Help
- Soft-wrapped text view + scrollbars, selection, clipboard, undo
- Find & Replace dialog (separate gel, TextEdit-measured 442×176)
- **Appearance** — bundled skins from `format/skins/`; Load… for extras

```sh
make run-textedit
# or: build/SagradoTextEdit.exe [file.txt] [--font face.fnt]
```

## Sagrado Jabber

AIM-era “You’ve Got Mail” IM on Jabber/XMPP — buddy list, presence, tabbed
chats, HTTP Upload files, and **Get an Account** with CAPTCHA painted in gel
(no browser signup). See [`docs/jabber.md`](docs/jabber.md).

```sh
make run
# or: make run-jabber
# or: build/SagradoJabber.exe
```

Shortcuts: `Ctrl+N/O/S`, `Ctrl+F` find, `Ctrl+H` replace, `Ctrl+G` find again,
`Ctrl+Z/X/C/V/A`. Esc quits (or closes Find / menu).

## For app authors

Start from Sagrado TextEdit’s patterns (`apps/textedit/`) and the
[Sagrado Apps standard](docs/contract.md#sagrado-apps-standard).

1. Include `engine/appearance.h` (pulls in canvas + skin).
2. Hold an `Appearance`, `load("path.sap")` or `load("path.hap")`, or start from `stock_skin()`.
   Saving always produces `.sap` (with art files when the appearance has image slots).
3. Paint with `paint_gel`, `paint_button`, `paint_field`, `paint_list`,
   `paint_scrollbar` (or compose from `ap.c("role.path")`).
4. Blit your `Canvas` with `SetDIBitsToDevice` — same as the editor.
5. When you need a new control, extend the schema + engine helpers. Do not
   invent a private palette.

Apps listen to SagradoKit first and build from it.
