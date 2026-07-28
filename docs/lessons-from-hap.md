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

- **Images (art)** = widget chrome. **Caps** = 9-slice; **Positions** = travel,
  frame thickness, or placement — meaning is per slot (see the surface map).
- **Icons** = sparse marks (file, folder, user) — a separate namespace.
- Many real themes are colour-only; art coverage is uneven. Fallbacks are
  mandatory.
- **Hilited** means pressed/active in Haxial, not modern hover.

## Editor practice

AppearanceEdit let authors edit colours (and art) against a live preview of
kit controls. SagradoKit’s editor is the same job for `.skin.toml`: load,
tweak named roles, watch the gel / button / field / list / scrollbar preview,
save.
