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

Incomplete skins are valid. Token resolution: **art → colour → stock**.

This is not an Ooze project and not a web/Electron/npm app.

## Layout

```
format/          schema + stock / example skins
engine/          Appearance Engine (load, resolve, paint)
editor/          Win32 SagradoKit Editor
docs/            contract + lessons from HAP
```

Read [`docs/contract.md`](docs/contract.md) for the system contract.
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

## Build

### Linux (cross-compile + Wine)

```sh
# Debian/Ubuntu
sudo apt install g++-mingw-w64-i686 wine

# Draft PR waves live on feature branches (main is behind):
#   git fetch origin && git checkout cursor/haxial-p2-polish-9daa

make          # → build/SagradoKitEditor.exe + copied example skins
make run      # launch under Wine (defaults to Milk Redux when present)
```

### Windows

With MinGW-w64 (or any C++17 toolchain that can link `gdi32`, `user32`,
`comdlg32`):

```sh
make
# or:
i686-w64-mingw32-g++ -std=c++17 -O2 -Iengine editor/main.cpp \
  -o build/SagradoKitEditor.exe -mwindows -lgdi32 -luser32 -lcomdlg32
```

Run `build/SagradoKitEditor.exe`. Example skins are in
`build/format/skins/` (and `format/skins/` in the repo). The editor prefers
`milk-redux/milk-redux.sap` when that folder was copied by `make skins`.

## Editor

- **Load / Save** — `.sap` files (same format apps load); **Load** also accepts `.hap`.
  Save after a Hap load writes a full `.sap` + `.skimg` art (Hap→Sap parity).
- **Stock** — reset to built-in last-resort colours
- **Colour Roles** — scrollable named swatches; drag R/G/B sliders
- **Kit Preview** — live gel + controls (icon buttons, menu bar, Find, File
  Transfers, fields, sliders, list/scroll). Generic push buttons use Hap’s usual
  **20px** height; Find dialog keeps TextEdit-measured **24px**.

Shortcuts: `Ctrl+O` load, `Ctrl+S` save, `Esc` quit. Drag the title bar to
move; the close box quits.

## For app authors

1. Include `engine/appearance.h` (pulls in canvas + skin).
2. Hold an `Appearance`, `load("path.sap")` or `load("path.hap")`, or start from `stock_skin()`.
   Saving always produces `.sap` (with art files when the appearance has image slots).
3. Paint with `paint_gel`, `paint_button`, `paint_field`, `paint_list`,
   `paint_scrollbar` (or compose from `ap.c("role.path")`).
4. Blit your `Canvas` with `SetDIBitsToDevice` — same as the editor.
5. When you need a new control, extend the schema + engine helpers. Do not
   invent a private palette.

Apps listen to SagradoKit first and build from it.
