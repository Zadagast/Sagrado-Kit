# SagradoKit contract

SagradoKit is the **authoritative appearance kit**. Apps do not invent their
own look — they load a skin through the Appearance Engine and paint kit
surfaces from resolved tokens.

```
┌─────────────────┐     .skin.toml      ┌──────────────────┐
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
| Skin format | `format/` | Named colour roles + reserved art/icon slots (`.skin.toml`) |
| Appearance Engine | `engine/` | Load skin, resolve tokens, paint kit surfaces into a software framebuffer |
| Editor | `editor/` | Win32 AppearanceEdit-style app — author the format against a live kit preview |

## Skin format (`.skin.toml`)

Human-authored TOML. Schema: [`format/schema.json`](../format/schema.json).
Example: [`format/skins/stock.skin.toml`](../format/skins/stock.skin.toml).

```toml
format = "sagrado-skin"
version = 1

[meta]
name = "Stock"
creator = "SagradoKit"
description = "Built-in stock appearance"

[colors.primary]
background = "#c0c0c0"
label = "#000000"
# …

[art]
# reserved: "window.frame.normal" = "relative/path.png"

[icons]
# reserved: "file.generic.16" = "relative/path.png"
```

Colours are `#RRGGBB` or `#RRGGBBAA`. Omitted roles fall through to stock.
Art/icon maps are reserved in this slice (paths accepted, painting later).

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
| `scrollbar` | `frame`, `light`, `face`, `dark`, `label`, `hilite_light`, `hilite`, `hilite_dark`, `hilite_label`, `indicator_light`, `indicator`, `indicator_dark`, `track_light2`, `track_light1`, `track`, `track_dark1`, `track_dark2` |
| `column_header` | `frame`, `light`, `face`, `dark`, `label`, `hilite_light`, `hilite`, `hilite_dark`, `hilite_label` |
| `workspace` | `background1` … `background4` |

Title and header ink should prefer `primary.label` when window/header labels
are still stock-white (Haxial authoring practice).

### Reserved art / icon slots

| Namespace | Examples (reserved) |
|---|---|
| `art` | `window.frame.normal`, `window.frame.focus`, `button.push.normal`, `button.push.hilite`, `menu.popup`, `slider.indicator`, `scrollbar.v.arrows`, `scrollbar.v.indicator`, `column_header.normal`, `column_header.hilite` |
| `icons` | `file.generic.16`, `file.generic.32`, `folder.16`, `folder.32`, `user.16`, `user.32` |

## Token resolution

```
resolve(token):
  if skin has art for this surface → art
  else if skin has colour role(s)  → colour plates
  else                             → stock defaults
```

The engine never leaves a hole: every kit surface paints from stock when the
skin is incomplete.

## Kit surfaces

Painted by the engine into a software framebuffer (no OS widgets, no CSS):

1. **Gel window** — framed window with title bar, traffic-light boxes, client fill
2. **Button** — raised bevel push button (pressed / default variants)
3. **Field** — sunken text field with focus ring and caret
4. **Dropdown** — popup button (`paint_dropdown`) + open menu (`paint_menu`)
5. **Slider** — bar + draggable indicator thumb
6. **List + header** — column header plate + list rows with hilite
7. **Scrollbar** — track, arrows, proportional thumb

Apps that speak SagradoKit call these paint helpers (or compose from the same
resolved colour roles). They do not hardcode a parallel palette.

## Editor responsibilities

- Load / save `.skin.toml`
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
