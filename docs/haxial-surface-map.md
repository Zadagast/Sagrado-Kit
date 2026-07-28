# Haxial surface map (research)

How Haxial’s Appearance Engine plugs **colours** and **images** into controls.
Sources, in priority order:

1. **Haxial AppearanceEdit 1.200 Documentation.pdf** (bundled with AppearanceEdit 1.24)
2. **AppearanceEdit.exe** Images / Colors panel name tables (string extract)
3. **Probed `.hap` files** + Sagrado’s verified slot indices (`native/src/hap.h`)
4. Sagrado traps (`hap-first.md`, `hap-color-table.md`, `hap-surfaces.md`)

This is the map SagradoKit must follow before painting art. We do **not** invent
a parallel PNG grammar.

---

## Engine rules (from AppearanceEdit docs)

### Incomplete skins

Images are optional. If a slot is empty, the engine draws that control from the
**Colors** table. Preview on the Colors panel intentionally hides images so you
can see colour plates alone.

Paint path (same as Haxial / Sagrado):

```
art slot present → 9-slice (or place) image → draw label/ink in Text Color / role
else             → colour plates for that control
else             → stock
```

### Caps (9-slice)

Every image has **Left / Top / Right / Bottom Cap** (0–255 px):

| Region | Behaviour |
|---|---|
| Four corners | Copied 1:1 |
| Edges between corners | Repeated / stretched |
| Centre | Filled from the centre patch |

Some controls only stretch on one axis (progress, many scroll pieces); the other
caps are disabled in the editor.

### Positions (per-image layout)

Meaning depends on the image. Common patterns:

| Use | Positions mean |
|---|---|
| Window / popup frames | Frame thickness on each side (must be **even**: 2,4,6…) |
| Window title buttons | Placement in the title bar (left **or** right; top) |
| Scroll / slider bars | Indicator **travel limits** inset from each end |
| Slider indicators | Offset of thumb relative to the bar |
| Popup button symbol | Placement of the arrow glyph on the button |
| Progress fill | Inset of the fill inside the empty bar |
| Scroll arrow hilite overlays | Where to stamp the pressed-arrow glyph |

If Positions are disabled for a slot, they are unused.

### States

| State | Meaning |
|---|---|
| Normal | Idle |
| Hilited | Mouse held / active press (not “hover” in modern UI sense) |
| Disabled | Non-interactive |
| Focus | Keyboard focus / front window variant |

### Images vs Icons

- **Images** = widget chrome (buttons, frames, scrollbars, menus…).
- **Icons** = sparse 16×16 / 32×32 marks (files, folders, users). Separate section.

---

## Colours (verified)

Full 204-entry map: Sagrado `docs/hap-color-table.md` (AppearanceEdit probe).

For the four kit surfaces we care about first:

| Control | Colour groups | Notes |
|---|---|---|
| **Button** | Button 29–35, Button Hilite 36–42, Button Disable 43–49, Default Button 50–53 | Label = Button Label. Default ring often stays stock-red in art themes → prefer **Focus Box (9)** when Default looks untouched |
| **Popup / menu** | Menu 104–112; outer ring Window Focus or **Focus Box** | Popup **menu frame** is **not** `Window Menu Button` art (that’s the title-bar menu glyph) |
| **Slider** | Slider Indicator 158–161, Indicator Hilite 162–165, Bar 166–169, Disable 170–173 | |
| **Scrollbar** | ScrollBar 128–147 (+ Disable 153–157) | Track = Bkgnd group; thumb = Indicator |

**Authoring traps (Haxial practice, not optional):**

- Title / column-header **ink** → **Primary Label (5)** when Window/Header labels are still default white (common in bitmap themes).
- Art overrides colours for that chrome; colour-only preview is how authors check plates.

---

## Images — authoring names → Hap slots → paint contract

AppearanceEdit Images panel order (from the exe). Hap **slot indices** below are
those verified by Sagrado / theme probes. Rows marked **index TBD** need a
probe pass before we paint them in SagradoKit.

### Push button

| AppearanceEdit name | Hap slot | Caps | Positions | Paint |
|---|---|---|---|---|
| Button Normal | **25** | 9-slice both axes | unused (0) | Face of push button; label centred in Text Color / Button Label |
| Button Hilited | **26** | same | unused | While mouse held |
| Button Disabled | **27** (occupied with button-like geometry in probes) | same | unused | Disabled |

Docs: usually **20 px tall**; caps allow other sizes. Label drawn **on top** of
the 9-sliced image. Kit layout constant: `kButtonH = 20`. Default outer =
face + 3 px on each side (`kDefaultButtonPad`).

| Default Button Normal/Hilited/Disabled | **37** / **38** / **39** | 9-slice | unused | Default is authored **3 px larger on all sides** than a regular button so a border can fit; or 3 px transparent if no border |

**SagradoKit names:** `button.normal`, `button.hilited`, `button.disabled`,
`default_button.normal`, `default_button.hilited`, `default_button.disabled`.

### Popup button (dropdown)

| AppearanceEdit name | Hap slot | Caps / Positions | Paint |
|---|---|---|---|
| Popup Button Normal/Hilited/Disabled | **89** / **90** / **91** | Usually ~20 px tall; 9-slice | Closed dropdown field+arrow chrome; title inside |
| Popup Button No Title Normal/Hilited/Disabled | **93** / **94** / **95** | Often ~20×20 | Well next to a text field |
| Popup Button Symbol Normal/Hilited/Disabled | **97** / **98** / **99** | **Positions** place the arrow on a titled popup; ignored (centred) on No Title | Down-arrow overlay |

**SagradoKit names:** `popup.normal`, `popup.hilited`, `popup.disabled`,
`popup.no_title.*`, `popup.symbol.*`.

### Menu (open list)

| AppearanceEdit name | Hap slot | Paint |
|---|---|---|
| Menu Background Pattern | index TBD | Optional tiled pattern (else Menu Background colour) |
| Menu Background | index TBD | 9-slice over whole menu; may use transparency over pattern |
| Menu Item Pattern/Normal/Hilited/Disabled | index TBD | Per-row chrome above background |
| Menu Separator | index TBD | Horizontal rule; L/R caps; vertically centred |
| Popup Window Frame Normal/Focus | index TBD | Frame around popup/menu; **Positions = thickness** (even); centre transparent |

Colours always apply for label / hilite / disable ink even when art is present.

**SagradoKit names:** `menu.background_pattern`, `menu.background`,
`menu.item.normal`, `menu.item.hilited`, `menu.item.disabled`,
`menu.separator`, `popup_frame.normal`, `popup_frame.focus`.

### Slider

| AppearanceEdit name | Hap slot | Caps / Positions | Paint |
|---|---|---|---|
| H Slider Bar Normal/Hilited/Disabled | index TBD | Bar vertically centred; height+indicator ≤ 30 | Track; **L/R Positions = travel limits** |
| H Slider Indicator Normal/Hilited/Disabled | index TBD | **Top Position** = px above bar top | Thumb |
| H Slider Pointed Indicator * | index TBD | Points down (scale below) | Optional pointed thumb |
| V Slider Bar * | index TBD | Horizontally centred; width+indicator ≤ 30 | **T/B Positions = travel** |
| V Slider Indicator * | index TBD | **Left Position** = px left of bar | Thumb |

**SagradoKit names:** `slider.h.bar.normal` …, `slider.h.indicator.normal` …,
`slider.v.*` (pointed variants optional).

### Scrollbar

| AppearanceEdit name | Hap slot | Caps / Positions | Paint |
|---|---|---|---|
| V Scroll Bar Double Arrows | **181** | Width **exactly 16**; T/B caps | 9-slice whole bar; **T/B Positions = thumb travel** (fallback: caps) |
| V Scroll Bar Single Arrows | index TBD | same idea | Single arrow ends |
| V Scroll Bar Disabled / Too Small | index TBD | | Disabled / undersized bar |
| V Scroll Bar Indicator Normal/Hilited | **185** / +1 | | Thumb 9-slice into thumb rect |
| V Scroll Bar Indicator Grips Normal/Hilited | **188** / +1 | often caps 0 | Optional, **centred on thumb** |
| V Scroll Bar * Arrow Hilited | index TBD | Position = stamp offset | Pressed arrow overlays |
| H Scroll Bar Double Arrows | **162** | Height **exactly 16**; L/R caps | Same as V, horizontal |
| H Scroll Bar Indicator Normal | **166** | | Thumb |
| H Scroll Bar Indicator Grips Normal | **169** | | Optional grips |

Probe note: Double Arrows images are long strips (~16×64); `pos` travel insets
are ~28–36 px — matching the docs (“indicator can be dragged no further than
this many pixels from the end”).

**SagradoKit names:** `scrollbar.v.double_arrows`, `scrollbar.v.indicator.normal`,
`scrollbar.v.grips.normal`, `scrollbar.h.*`, plus single/disabled/arrow-hilite
when mapped.

### Column header / gel (related)

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Column Header Normal/Hilited/Disabled | **150** / **151** / TBD | List headers + often tabs |
| Window Frame Normal/Focus | **220** / **221** | Positions = frame thickness; centre transparent |
| Window Close/Min/Max/Menu … | **223+** | Positions place buttons; Disabled optional (else hide) |
| Window Resize Normal/Focus | **243** / **244** | Bottom-right; no transparent colour |

---

## What went wrong in clones (why this doc exists)

Sagrado already implements `nine_slice` and many slots, but placements still
drift when:

1. **Wrong colour role** for labels (Window Label vs Primary Label).
2. **Wrong art family** (title-bar Window Menu vs popup menu frame).
3. **`positions` ignored** (travel / thickness / button placement).
4. **Assuming every theme has art** — many are colour-only; half the scroll grip
   slots are missing even in art themes.
5. **Hilited ≠ hover** — Hilited is press/active in Haxial.

SagradoKit freezes the contract here, then implements paint helpers against it.

---

## SagradoKit implementation gate

Before painting art for a control in `engine/`:

1. Row in this file has a **verified Hap slot** (or explicit “colour-only”).
2. Caps/positions meaning recorded.
3. Named `.skin.toml` `[art]` key listed in `docs/contract.md`.
4. Fallback colour roles listed.
5. Probe at least 3 art-heavy themes for that slot (size/caps sanity).

**First wave (ready enough to implement next):**

| Surface | Art | Colour fallback |
|---|---|---|
| Push button | 25 / 26 / (27) | `button.*` |
| V/H scrollbar body + thumb + grips | 181/185/188, 162/166/169 | `scrollbar.*` |
| Column header | 150 / 151 | `column_header.*` + Primary Label |
| Window gel frame + title boxes (close/hatch/min/max) | 220+ | `window*` + Primary Label |

**Needs index probe before art paint:**

Popup button + symbol, menu backgrounds/items, popup window frame, sliders,
default button, scroll arrow hilite overlays.

---

## Local research artefacts

| Path | What |
|---|---|
| `research/AppearanceEdit-Documentation.txt` | Extracted official PDF |
| `research/probe_haps.py` | Slot occupancy / geometry probe |
| `research/probe-report.txt` | Last probe run |
| AppearanceEdit 1.24 zip | https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip |

Do not commit the `.exe` / `.hap` binaries; re-download when probing.
