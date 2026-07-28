# Lessons from Haxial Appearance (`.hap`)

Short reuse notes distilled from Sagrado’s HAP docs
(`hap-first.md`, `hap-surfaces.md`, `hap-color-table.md`).
SagradoKit does **not** clone the `.hap` binary layout — it keeps the ideas
in a clean, named skin format.

## Architecture shape (match this)

1. **One appearance engine** every window paints through.
2. **One skin format** that engine loads (metadata + named colour roles +
   reserved art/icon slots).
3. **A separate editor** that authors that format against a live kit preview.
4. **Incomplete skins are OK.** Resolution order:
   **art → colour → stock**.

## Colour ideas (not the 204-slot table)

Haxial used a fixed 204-entry RGB table with semantic names
(Primary, Button, Text Box, List, Window, ScrollBar, Column Header, …).
SagradoKit keeps the *roles*, expressed as **named tokens**
(`primary.background`, `button.label`, …) instead of opaque indices.

Authoring traps carried forward:

- Prefer **primary.label** for titles and column-header ink. Bitmap themes
  often leave window/header label slots at default white.
- **Focus** outlines are a first-class role; default-button / window-focus
  rings often stayed stock-red in art themes — use the focus role when those
  look untouched.
- Do **not** invent colour roles the kit does not expose. Reuse primary /
  focus / button / list / text groups.

## Surfaces

Each control has a shopping list: which colour groups, which art slots, which
icons. The authoritative plug-in map (AppearanceEdit docs + probes) lives in
[`haxial-surface-map.md`](haxial-surface-map.md). Kit contract:
[`contract.md`](contract.md).

- **Images (art)** = widget chrome. **Caps** = 9-slice fill into the app’s Rect;
  **Positions** = travel, frame thickness, or placement — meaning is per slot
  (see the surface map **Fill model**). Outer size is never taken from art pixels.
- **Icons** = sparse marks (file, folder, user) — a separate namespace.
- Many real themes are colour-only; art coverage is uneven. Fallbacks are
  mandatory.
- **Hilited** means pressed/active in Haxial, not modern hover.

Authoring rules from AppearanceEdit 1.200 (Groups, Import Colors, Transparent
Color, 256 indexed, Preview colours-only, tick ≤18, sep ≤4, even frame
thickness, title Disabled→hide, WonderLight 16×16, `Appearances/` host folder)
and the full Images inventory (Primary Background tile, Focus Box, Medium
disclosure, popup frame, window Disabled, menu item patterns) are recorded in
the surface map — start there before inventing paint behaviour.

## Editor practice

AppearanceEdit let authors edit colours (and art) against a live preview of
kit controls. SagradoKit’s editor is the same job for `.sap`: load,
tweak named roles, watch the gel / button / field / list / scrollbar preview,
save. Load a `.hap` for live import; **Save** writes a `.sap` plus `.skimg`
art beside it so the authored format can carry the same colours, caps,
positions, and images the Hap provided.

## Live Hap load

Live `.hap` import copies **only occupied** Hap slots (no blind donor of
Primary Background or window frames). Incomplete themes still resolve
**art → colour → stock**.

After import, the Kit applies a small **soft-completion** pack
(`format/skins/completion/`) that fills **empty** art/icon keys only
(icons, WonderLight, menu plates, focus box). Authored Hap art is never
overwritten. Scroll **grips** are not soft-filled — themes that omit them
usually bake thumb chrome into the indicator (e.g. Aluminum Alloy). Soft-complete
never invents `menu.background` / `menu.item.*` over an authored menu pattern
or other menu chrome. Pure Hap without the pack is still a valid colour path.

Hap image records also carry AppearanceEdit **Text Color** (+8..+11). When
set, paint prefers that ink for button / header / menu / title labels on
the matching plate. When unset, stock near-white Window / Column Header /
Hilite / Button / Menu label roles remap to **Primary Label** (KDX practice)
so light bitmap chrome stays readable.
