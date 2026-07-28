# Haxial surface map (research)

How Haxial’s Appearance Engine plugs **colours** and **images** into controls.
Sources, in priority order:

1. **Haxial AppearanceEdit 1.200 Documentation.pdf** (bundled with AppearanceEdit 1.24)
2. **AppearanceEdit.exe** Images / Colors panel name tables (string extract)
3. **Probed `.hap` files** + Sagrado’s verified slot indices (`native/src/hap.h`)
4. **KDX Client Documentation.pdf** (host meaning — WonderLight, Appearances folder, FT)
5. Sagrado traps (`hap-first.md`, `hap-color-table.md`, `hap-surfaces.md`)

Internet source catalog + tiered gaps:
[`research/haxial-docs-gap-inventory.md`](../research/haxial-docs-gap-inventory.md).

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
| Window / popup frames | Frame thickness on each side (must be **even**: 2,4,6…). **Caps may exceed** thickness |
| Window title buttons | Placement in the title bar (left **or** right; top) |
| Scroll / slider bars | Indicator **travel limits** inset from each end |
| Slider indicators | Offset of thumb relative to the bar |
| Popup button symbol | L if set else R; T if set else B; B=0 → vertical centre. Ignored on No Title (centred) |
| Progress fill | L/R begin/end insets; T/B vertical clamp inside the empty bar |
| Scroll arrow hilite overlays | Where to stamp the pressed-arrow glyph |

If Positions are disabled for a slot, they are unused.

**Scrollbar Disabled** is one shared image for double and single arrow styles.
**Too Small** replaces the bar when it is too small for Single Arrows **per its
caps**. Transparent Color **cannot** cut a non-rectangular Window Frame.

### Compositing / layer stacks (AppearanceEdit)

Menu Bar (bottom → top), when art is supplied:

1. **Menu Bar Pattern** (tiled)
2. **Menu Bar** (9-slice; transparent middle reveals pattern)
3. Per-title **Title Pattern** (obliterates 1–2 in that title’s rect)
4. Per-title **Title** Normal/Hilited/Disabled (obliterates below except transparent)

Open menu (same idea):

1. **Menu Background Pattern** → **Menu Background**
2. Per-item **Item Pattern** → **Item** chrome
3. **Menu Separator** centred in its gap (≤4 px)

**Framed Raised Box** is a placard over an edge-to-edge scrolling area: that
scroll region has **no focus box** — the window itself indicates focus.

**WonderLight:** Off/Pause/Ready/Go/Finished = activity (e.g. file transfers).
Flash Off/On1/On2 = attention (KDX Button Bar blinks for unread messages);
On1/On2 alternate — period undocumented.

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

### Tick / Mutex (checkbox / radio)

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Tick Button Blank Normal/Hilited/Disabled | **57** / **58** / **59** | Height ≤ 18; title outside |
| Tick Button Ticked Normal/Hilited/Disabled | **61** / **62** / **63** | |
| Tick Button Tristated Normal/Hilited/Disabled | **65** / **66** / **67** | |
| Mutex Button Blank Normal/Hilited/Disabled | **69** / **70** / **71** | Mutually exclusive group |
| Mutex Button Ticked Normal/Hilited/Disabled | **73** / **74** / **75** | |
| Mutex Button Tristated Normal/Hilited/Disabled | **77** / **78** / **79** | |

**SagradoKit names:** `tick.blank.*`, `tick.ticked.*`, `tick.tristate.*`,
`mutex.blank.*`, `mutex.ticked.*`, `mutex.tristate.*`.

### Disclosure / separators / box / progress

| AppearanceEdit name | Hap slot | SagradoKit |
|---|---|---|
| Small Plus / Minus | **81** / **85** | `disclosure.plus.small`, `disclosure.minus.small` |
| Horiz / Vert Separator | **105** / **106** | `separator.h`, `separator.v` |
| Box / Framed Raised Box | **107** / **108** | `box`, `framed_raised` (placard over edge-to-edge scroll; no focus box) |
| Progress Bar / Fill | **111** / **112** | `progress.bar`, `progress.fill` — Continuous stretch; KDX FT tiles Fill as LEDs |
| WonderLight Off/Pause/Ready/Go/Finished | **251**–**255** | `wonderlight.*` — **must** 16×16 activity lamps |
| WonderLight Flash Off/On1/On2 | **256**–**258** | Attention flash (e.g. messages); On1/On2 alternate |

### Menu (open list)

| AppearanceEdit name | Hap slot | Paint |
|---|---|---|
| Menu Background Pattern | **200** | Optional tiled pattern (else Menu Background colour) |
| Menu Background | **201** | 9-slice over item area; may use transparency over pattern |
| Menu Item Pattern/Normal/Hilited/Disabled | **206** / **207** / TBD | Per-row chrome above background |
| Menu Separator | **208** | Horizontal rule; L/R caps; vertically centred |
| Popup Window Frame Normal/Focus | rarely authored | Frame around popup/menu; **Positions = thickness** (even); centre transparent |

Colours always apply for label / hilite / disable ink even when art is present.

**SagradoKit names:** `menu.background_pattern`, `menu.background`,
`menu.item.normal`, `menu.item.hilited`, `menu.item.disabled`,
`menu.separator`, `popup_frame.normal`, `popup_frame.focus`.

### Slider

| AppearanceEdit name | Hap slot | Caps / Positions | Paint |
|---|---|---|---|
| H Slider Bar Normal/Hilited/Disabled | **126** / **127** / **128** | Bar vertically centred; height+indicator ≤ 30 | Track; **L/R Positions = travel limits** |
| H Slider Indicator Normal/Hilited/Disabled | **130** / **131** / **132** | **Top Position** = px above bar top | Thumb (blit or 9-slice) |
| H Slider Pointed Indicator * | **134** / **135** / **136** | Points down (scale below) | Optional; `slider.h.indicator_pointed.*` |
| V Slider Bar * | **138** / **139** / **140** | Horizontally centred; width+indicator ≤ 30 | **T/B Positions = travel** |
| V Slider Indicator * | **142** / **143** / **144** | **Left Position** = px left of bar | Thumb |
| V Slider Pointed Indicator * | **146** / **147** / **148** | Points right (scale beside) | Optional; `slider.v.indicator_pointed.*` |

**SagradoKit names:** `slider.h.bar.normal` …, `slider.h.indicator.normal` …,
`slider.h.indicator_pointed.*`, `slider.v.*`.

### Scrollbar

| AppearanceEdit name | Hap slot | Caps / Positions | Paint |
|---|---|---|---|
| V Scroll Bar Double Arrows | **181** | Width **exactly 16**; T/B caps | 9-slice whole bar; **T/B Positions = thumb travel** (fallback: caps) |
| V Scroll Bar Single Arrows | **182** | same idea | Single arrow ends |
| V Scroll Bar Disabled / Too Small | **183** / **184** | | Disabled / undersized bar |
| V Scroll Bar Indicator Normal/Hilited | **185** / **186** | | Thumb 9-slice into thumb rect |
| V Scroll Bar Indicator Grips Normal/Hilited | **188** / **189** | often caps 0 | Optional, **centred on thumb** |
| V Scroll Bar * Arrow Hilited | **191–196** | Position = stamp offset | Pressed arrow overlays |
| H Scroll Bar Double Arrows | **162** | Height **exactly 16**; L/R caps | Same as V, horizontal |
| H Scroll Bar Single / Disabled / Too Small | **163** / **164** / **165** | | |
| H Scroll Bar Indicator Normal/Hilited | **166** / **167** | | Thumb |
| H Scroll Bar Indicator Grips Normal/Hilited | **169** / **170** | | Optional grips |
| H Scroll Bar * Arrow Hilited | **172–177** | Position = stamp offset | Pressed arrow overlays |

Probe note: Double Arrows images are long strips (~16×64); `pos` travel insets
are ~28–36 px — matching the docs (“indicator can be dragged no further than
this many pixels from the end”).

**SagradoKit names:** `scrollbar.v.double_arrows`, `scrollbar.v.single_arrows`,
`scrollbar.v.disabled`, `scrollbar.v.too_small`, `scrollbar.v.indicator.*`,
`scrollbar.v.grips.*`, `scrollbar.v.arrow_hilite.*`, and `scrollbar.h.*`.

### Icon Button / Menu Bar / Column header

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Icon Button Normal/Hilited/Disabled | **49** / **50** / **51** | `icon_button.*`; icon + optional title |
| Column Header Normal/Hilited/Disabled | **150** / **151** / **152** | List headers; disabled often donor-filled |
| Menu Bar Pattern / Menu Bar / Titles | rarely authored | Colour path `paint_menu_bar`; optional `menu_bar.*` art. Draw order: **Compositing** above |

### Column header / gel (related)

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Column Header Normal/Hilited/Disabled | **150** / **151** / **152** | List headers + often tabs |
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
| Window gel frame + title boxes (close / **Window Menu** / min / max) | 220+ | `window.frame.*`, `window.close.*`, `window.menu.*`, `window.minimize.*`, `window.maximize.*` + Primary Label |

**Still open / deferred** — full tiered list in
[`research/haxial-docs-gap-inventory.md`](../research/haxial-docs-gap-inventory.md).

| Tier | Item | Status |
|---|---|---|
| A (docs) | Menu Bar / menu compositing, scroll Disabled/Too Small, Progress Positions, frame caps, Popup Symbol fall-through, Framed Raised placard, WonderLight host meaning | Recorded above |
| B (editor) | Menu Bar Hap indices (empty in themes); `menu.item.disabled` Hap index; decimal/CSV colour import; image paste context menu; Hap write | Colour paths work; Hap write out of scope |
| C (first app) | `Appearances/` host folder; Button Bar Flash timing; live FT queue; column resize/reorder | Not kit chrome |
| D | Marketing “mouse icons”; Sound List Editor | Ignore / N/A |

---

## Local research artefacts

| Path | What |
|---|---|
| `research/AppearanceEdit-Documentation.txt` | Extracted AppearanceEdit 1.200 PDF |
| `research/KDX-Client-Documentation.txt` | Extracted KDX Client Documentation.pdf (Client 1.600 zip) |
| `research/haxial-docs-gap-inventory.md` | Internet sources + tiered gap inventory |
| `research/probe_haps.py` | Slot occupancy / geometry probe |
| `research/probe-report.txt` | Last probe run |
| AppearanceEdit 1.24 zip | https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip |
| KDX Client 1.600 zip | https://kdx.technowiki.info/downloads/KDXClient1600-Win.zip |

Do not commit `.exe` / zip downloads (`research/bin/`); re-download when probing.
