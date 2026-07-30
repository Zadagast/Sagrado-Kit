# SagradoKit contract

SagradoKit is the **authoritative appearance kit**. Apps do not invent their
own look — they load a skin through the Appearance Engine and paint kit
surfaces from resolved tokens.

```
┌─────────────────┐        .sap         ┌──────────────────┐
│  SagradoKit      │ ─────────────────► │  Appearance       │
│  Editor          │   load / save      │  Engine           │
└─────────────────┘                     └────────┬─────────┘
                                                 │ resolve
                                                 │ art → colour → stock
                                                 ▼
                                        ┌──────────────────┐
                                        │  App windows     │
                                        │  (framebuffer)   │
                                        └──────────────────┘
```

## Pieces

| Piece | Path | Job |
|---|---|---|
| Skin format | `format/` | Named colour roles + reserved art/icon slots (`.sap`) |
| Appearance Engine | `engine/` | Load skin, resolve tokens, paint kit surfaces into a software framebuffer |
| Editor | `editor/` | Win32 AppearanceEdit-style app — author the format against a live kit preview |

## Skin format (`.sap`)

Human-authored TOML (Sagrado Appearance). Schema: [`format/schema.json`](../format/schema.json).
Example: [`format/skins/stock.sap`](../format/skins/stock.sap).

```toml
format = "sap"
version = 1

[meta]
name = "Stock"
creator = "SagradoKit"
description = "Built-in stock appearance"

[colors.primary]
background = "#c0c0c0"
label = "#000000"
# …

[art."button.normal"]
file = "button_normal.skimg"
caps = [13, 11, 12, 11]
positions = [0, 0, 0, 0]

[icons]
# reserved: "file.generic.16" = "relative/path.skimg"
```

Colours are `#RRGGBB` or `#RRGGBBAA`. Omitted roles fall through to stock.
Art slots use Hap names; each may carry `caps` (9-slice) and `positions`
(travel / thickness). Saving a live-loaded `.hap` writes a `.sap` plus `.skimg`
files beside it — same art the Hap imported.

### Hap ↔ Sap

| | `.hap` | `.sap` |
|---|---|---|
| Role | Haxial Appearance (binary import) | Sagrado Appearance (authored) |
| Colours | 204-index table → named roles | Named roles (+ transitions) |
| Images | Indexed slots + caps/positions | `[art."slot"]` + `.skimg` |
| Icons | Separate icon table | `[icons]` |
| Editor | Load only | Load / Save |

`.sap` is meant to express everything Hap carries that the kit uses. Load either;
author and ship `.sap`.

### Named colour roles (first slice)

| Group | Roles |
|---|---|
| `primary` | `light`, `background`, `dark`, `frame`, `label`, `disable_frame`, `disable_label` |
| `important` | `label` |
| `focus` | `box` |
| `text` | `background`, `foreground`, `hilite_background`, `hilite_foreground`, `insertion_point` |
| `list` | `background`, `label`, `hilite_background`, `hilite_foreground`, `sort_column_background`, `separator` |
| `button` | `light2`, `light1`, `face`, `dark1`, `dark2`, `frame`, `label` |
| `button_hilite` | same seven as `button` |
| `button_disable` | same seven as `button` |
| `default_button` | `light`, `face`, `dark`, `frame` |
| `window` | `light2`, `light1`, `face`, `dark1`, `dark2`, `frame`, `label` + `transition` (18 colours) |
| `window_focus` | same as `window` |
| `menu` | `light`, `background`, `dark`, `label`, `hilite_light`, `hilite_background`, `hilite_dark`, `hilite_label`, `disable_label` |
| `slider` | `bar`, `bar_frame`, `bar_hilite`, `bar_hilite_frame`, `indicator_light`, `indicator`, `indicator_dark`, `indicator_frame`, `indicator_hilite_*`, `disable_*` |
| `scrollbar` | `frame`, `light`, `face`, `dark`, `label`, `hilite_*`, `indicator_*`, `indicator_hilite_*`, `track_*`, `disable_*` |
| `column_header` | `frame`, `light`, `face`, `dark`, `label`, `hilite_light`, `hilite`, `hilite_dark`, `hilite_label` |
| `file_label` | `0` … `15` (list-item label tints) |
| `progress` | `bkgnd_light`, `bkgnd`, `bkgnd_dark`, `frame`, `label` + `transition` (10 colours) |
| `workspace` | `background1` … `background4` |

Title and header ink should prefer `primary.label` when window/header labels
are still stock-white (Haxial authoring practice).

### Reserved art / icon slots

Named after AppearanceEdit Images / Icons panel entries. **Do not invent new
slot names** — extend only from [`haxial-surface-map.md`](haxial-surface-map.md).
Each art asset carries `caps` (9-slice) and `positions` (per-slot layout); see
that map for what Positions mean on each control.

| Namespace | Keys (first wave + reserved) |
|---|---|
| `art` | `primary.background` (tiled client pattern; colour role shares the name), `button.normal`, `button.hilited`, `button.disabled`, `default_button.*`, `icon_button.normal`, `icon_button.hilited`, `icon_button.disabled`, `tick.*`, `mutex.*`, `disclosure.*`, `popup.*`, `popup.no_title.*`, `popup.symbol.*`, `popup_frame.*`, `menu.background(_pattern)`, `menu.item.pattern.*`, `menu.item.*`, `menu.separator`, `menu_bar.pattern`, `menu_bar.background`, `menu_bar.title*`, `separator.h/v`, `box`, `framed_raised`, `progress.*`, `focus_box.*`, `slider.h/v.bar.*`, `slider.h/v.indicator.*`, `slider.h/v.indicator_pointed.*`, `scrollbar.v/h.double_arrows`, `scrollbar.v/h.single_arrows`, `scrollbar.v/h.disabled`, `scrollbar.v/h.too_small`, `scrollbar.v/h.indicator.*`, `scrollbar.v/h.grips.*`, `scrollbar.v/h.arrow_hilite.*`, `column_header.normal/hilited/disabled`, `window.frame.*`, `window.close/minimize/maximize/menu/resize.*` (incl. `.disabled`), `wonderlight.*` |
| `icons` | Full AppearanceEdit catalog (`stop`, `note`, `caution`, `question`, `program`, `plugin`, `shared_library`, `unattached_alias`, `document` (+ text/image/audio/video/font/archive/partial/saved), `folder` (+ uploads/dropbox/programs/programming/games/internet/pictures/sounds), disks, `settings`, `tools`, `exit`, `about`, `information`, `address_book`, `launch`, `create_folder`, `connect`, `disconnect`, `data_transfer`, `news`, `chat`, `message`, `users`/`user`, `haxial`, `server`, `files`) each as `.16` / `.32`. Aliases: `file.generic` → `document`, `user` → `users`. Unmapped Hap images preserved as `hap.image.N`. |

Art painting is gated on the surface map (verified Hap index + caps/positions
meaning). Colour fallbacks remain mandatory.

## Token resolution

```
resolve(token):
  if skin has art for this surface → art (9-slice / place via caps+positions)
  else if skin has colour role(s)  → colour plates
  else                             → stock defaults
```

The engine never leaves a hole: every kit surface paints from stock when the
skin is incomplete. How each art slot plugs in is defined in
[`haxial-surface-map.md`](haxial-surface-map.md) — research before paint code.

## Kit surfaces

Painted by the engine into a software framebuffer (no OS widgets, no CSS):

1. **Gel window** — framed window with title bar, close + Window Menu rectangle + min/max boxes, client fill
2. **Button** — raised bevel push button (pressed uses `button_hilite.*` / default / disabled)
3. **Icon Button** — `paint_icon_button` (Hap 49–51 + optional title)
4. **Tick / Mutex** — checkbox and radio (`paint_tick` / `paint_mutex`, blank/ticked/tristate)
5. **Field** — sunken text field with focus ring and caret
6. **Dropdown** — popup button (`paint_dropdown`) + open menu (`paint_menu`)
7. **Menu Bar** — `paint_menu_bar` (colour path; optional `menu_bar.*` art)
8. **Slider** — bar + indicator; optional pointed indicators (`paint_slider(..., pointed)`)
9. **Progress** — empty bar + fill (`paint_progress`)
10. **List + header** — column header normal/hilited/disabled + list rows with hilite / `file_label` tints
11. **Scrollbar** — V/H art-first (double/single arrows, disabled, too-small, grips hilited, arrow-hilite overlays)
12. **Separators / box / disclosure** — `paint_separator_h/v`, `paint_box`, `paint_disclosure`
13. **WonderLight** — 16×16 status lamp (`paint_wonderlight`)
14. **Icons** — `paint_icon` from `[icons]` (file.generic.16/32)
15. **File Transfers window** — KDX download sample (`paint_file_transfers_window`)

Apps that speak SagradoKit call these paint helpers (or compose from the same
resolved colour roles). They do not hardcode a parallel palette.

## Editor responsibilities

- Load / save `.sap` (also load Haxial `.hap`; Hap is import-only — Save always writes `.sap`)
- Panels: Colors (typed `#RRGGBB`, Import Colors, Colors Preview), Info meta typing, Images / Icons (full Hap catalogs + Paste), ♦ Groups
- Present named colour roles for editing
- Live preview of the kit surfaces above, driven by the same engine apps use
- Leave incomplete skins valid (partial colour tables are fine)

## Stack

C++17 · Win32 host · software framebuffer · `SetDIBitsToDevice` blit.
Same shape as Haxial and Sagrado `native/`. No OS widgets, no CSS, no web host.

## What apps must do

1. Include / link the Appearance Engine (`engine/`).
2. Load the active skin (or stock) at startup / when the user switches.
3. Paint every window through the Appearance Engine into a software framebuffer.
4. When adding a new control, extend the skin schema + engine helpers — do not
   invent a private colour table.
