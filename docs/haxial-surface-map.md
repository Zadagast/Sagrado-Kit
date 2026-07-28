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

## AppearanceEdit panels (1.200)

Four authoring panels, in order:

| Panel | What it edits | SagradoKit today |
|---|---|---|
| **Information** | name, version, creator, description | `[meta]` in `.sap`; no editor UI |
| **Colors** | 204 named roles + ♦ Groups + Import Colors + RGB/hex/decimal | Named roles + RGB/hex; no Groups / Import / decimal CSV UI |
| **Images** | paste bitmap, Caps, Positions, Transparent Color, Text Color | Runtime Caps/Positions + Hap import; no Images authoring panel |
| **Icons** | full 16×16 / 32×32 catalog | `file.generic` + `folder` only; no Icons panel |

### Authoring rules (PDF — record these)

| Rule | Contract |
|---|---|
| **Colors Preview** | Preview on the Colors panel = **colours only** (images/icons hidden). Other panels, or **Alt/Option+Preview**, = full chrome |
| **♦ Groups** | Diamond-flanked rows (e.g. Primary Group) set a related cluster from one base colour (light / background / dark / frame) |
| **Import Colors** | Window Menu → Import Colors copies the colour table from another `.hap` into the current file |
| **Transparent Color** | Per-image menu: None / White / 100% Red / 100% Green / 100% Blue. Exact channel match only |
| **Text Color** | Per-image label ink when the engine draws text on that plate |
| **256 indexed** | Hap save reduces images to ≤256 colours; Adaptive Indexed guidance in the PDF. Sap `.skimg` stays full ARGB by design |
| **Tick height** | ≤ **18** px |
| **Separator** | ≤ **4** px thick; centred in its gap |
| **Frame Positions** | Window / popup frame thickness must be **even** (2, 4, 6…) |
| **Resize** | No transparent colour on Window Resize |
| **Title Disabled** | Close/Min/Max/Menu **Disabled** art optional — if absent, **hide** the button when disabled |
| **WonderLight** | Must be **16×16**; Flash On1/On2 alternate when flashing |
| **Host layout** | `Appearances/` folder beside the Haxial program + `Appearance.hap`; folder may be an alias/shortcut |

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

### Fill model (sizing)

**Outer size is the program’s job.** The Appearance Engine never invents a
control’s destination rect from art pixel size. The app (TextEdit, KDX, kit
preview) requests a `Rect`; Caps/Positions only say how to fill or place inside it.

```
App requests Rect R for a control
        │
        ├─ stretchy chrome (button, bar, frame, header, menu…)
        │     nine_slice(art, R) using Caps
        │     Positions may inset travel / thickness / fill area inside R
        │
        └─ fixed chrome (title btn, grip, symbol, arrow hilite, icon)
              blit 1:1 at Positions (or centre); natural art size
```

| Rule | Hap contract |
|---|---|
| Buttons / headers / menus / frames | Stretch freely via Caps into requested R |
| Scrollbar cross-axis | Layout thickness **exactly 16**; thinner art (e.g. Milk 15) is **centred** in the trough — not stretched cross-axis |
| Scroll / slider bar Positions | Limit **indicator travel** inside the full bar — do **not** shrink the bar |
| Progress height | ≤ **16**; fill Positions = inset inside the trough |
| Slider band | Height (H) or width (V) including indicator ≤ **30** |
| Window / popup frame Positions | Frame **thickness** (even); client = inside |
| Title buttons / WonderLight / icons | Place 1:1; never 9-slice into the control rect |

TextEdit-measured kit metrics (`kButtonH=24`, `kTitleH=22`, `kBorder=6`) are
layout constants from real Haxial chrome. AppearanceEdit’s “buttons usually 20”
is authoring guidance for art — both are Haxial; do not “correct” Find metrics
to 20.

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

AppearanceEdit Images panel order (from the exe; full list in
`research/probe_haps.py` `IMAGE_NAMES`). Hap **slot indices** below are
verified by Sagrado / cross-theme probes (`research/probe_haps.py`).

### Primary Background (tiled client fill)

| AppearanceEdit name | Hap slot | Caps / Positions | Paint |
|---|---|---|---|
| Primary Background | **17** | usually caps 0; tiled 1:1 | Optional pattern under gel **client**. Colour role `primary.background` is the solid fill; art key **`primary.background`** tiles on top when present. Occupied in 4/11 probed themes (Boilerplate 128×128, Terminal-TRON 64×64, …). Solid-only themes leave the slot empty — use the colour |

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

### Focus Box

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Focus Box Normal / Hilited / Disabled | **101** / **102** / **103** | 3 px thick; centre transparent. Occupied in 4–6 / 11 themes (typically 7×7 with caps 3). Colour fallback: `focus.box` / `primary.disable_frame` |

**SagradoKit names:** `focus_box.normal`, `focus_box.hilited`, `focus_box.disabled`.

### Disclosure / separators / box / progress

| AppearanceEdit name | Hap slot | SagradoKit |
|---|---|---|
| Small Plus / Minus | **81** / **85** | `disclosure.plus.small`, `disclosure.minus.small` — docs: usually 12×12 |
| Medium Plus / Minus | **263** / **267** | `disclosure.plus.medium`, `disclosure.minus.medium` — docs: 16×16; occupied in 7/11 themes (stride matches Small ±4) |
| Horiz / Vert Separator | **105** / **106** | `separator.h`, `separator.v` — ≤4 px |
| Box / Framed Raised Box | **107** / **108** | `box`, `framed_raised` |
| Progress Bar / Fill | **111** / **112** | `progress.bar`, `progress.fill` |
| WonderLight Off/Pause/Ready/Go/Finished | **251**–**255** | `wonderlight.*` — **must** 16×16 |
| WonderLight Flash Off/On1/On2 | **256**–**258** | Flash attention lamp; On1/On2 alternate |

### Menu (open list)

| AppearanceEdit name | Hap slot | Paint |
|---|---|---|
| Menu Background Pattern | **200** | Optional tiled pattern (else Menu Background colour) |
| Menu Background | **201** | 9-slice over item area; may use transparency over pattern |
| Menu Item Pattern Normal/Hilited/Disabled | **202** / **203** / **204** | Optional per-row pattern under item chrome (sparse — Function / Boilerplate) |
| Menu Item Normal / Hilited | **206** / **207** | Per-row chrome above background / pattern |
| Menu Item Disabled | **TBD** | Paint key `menu.item.disabled` exists; **no occupancy** in probed themes after 207 (208 is separator) |
| Menu Separator | **208** | Horizontal rule; L/R caps; vertically centred |
| Popup Window Frame Normal/Focus | **248** / **249** | Frame around popup/menu; **Positions = thickness** (even); centre transparent. Occupied in 9/11 themes |

Colours always apply for label / hilite / disable ink even when art is present.

**SagradoKit names:** `menu.background_pattern`, `menu.background`,
`menu.item.pattern.normal/hilited/disabled`, `menu.item.normal`,
`menu.item.hilited`, `menu.item.disabled`, `menu.separator`,
`popup_frame.normal`, `popup_frame.focus`.

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

### Icon Button / Column header

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Icon Button Normal/Hilited/Disabled | **49** / **50** / **51** | `icon_button.*`; icon + optional title |
| Column Header Normal/Hilited/Disabled | **150** / **151** / **152** | List headers; disabled often donor-filled |

### Window gel (frame + title buttons)

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Window Frame Normal/Focus | **220** / **221** | Positions = frame thickness (even); centre transparent |
| Window Close N/F/H/**D** | **223** / **224** / **225** / **226** | Positions place the button; Disabled optional (else **hide**) |
| Window Minimize N/F/H/**D** | **228** / **229** / **230** / **231** | same |
| Window Maximize N/F/H/**D** | **233** / **234** / **235** / **236** | same |
| Window Menu N/F/**H**/**D** | **238** / **239** / **240** / **241** | Hilited used when pressed; Disabled optional (else hide). Slot 240 empty across probed themes but reserved |
| Window Resize Normal/Focus | **243** / **244** | Bottom-right; **no transparent colour** |

**SagradoKit names:** `window.frame.*`, `window.close.*`, `window.minimize.*`,
`window.maximize.*`, `window.menu.*` (incl. `.hilited` / `.disabled`),
`window.resize.*`.

### Menu Bar family

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Menu Bar Pattern / Menu Bar / Title / Title Pattern | **empty** in probed themes | Paint keys `menu_bar.*` exist; colour path is the real fallback |

---

## Icons catalog (AppearanceEdit)

Icons are a separate Hap table (16×16 and 32×32). Programs pick a size.
Transparent Color + 256-colour rules apply. SagradoKit currently imports only
`file.generic.16/32` and `folder.16/32` (Hap icon slots 4/5, 8/9).

Exe name table (summary — each name has 16 and 32 variants in the panel):

Stop, Note, Caution, Question, Program, Plugin, Shared Library, Unattached Alias,
Document (+ Text / Image / Audio / Video / Font / Archive / Partial),
Folder (+ Uploads / DropBox / Programs / Programming / Games / Internet /
Pictures / Sounds), RAM Disk, Hard Disk, Network Disk, Compact Disk,
Removable Media, Settings, Tools, Exit, About, Information, Address Book,
Launch, Create Folder, Connect, Disconnect, Data Transfer, News, Chat,
Message, Users, Haxial, Server, Files, Document Saved, Document Unsaved.

Full Icons catalog import beyond file/folder is out of scope until a later wave.

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
3. Named `.sap` `[art]` key listed in `docs/contract.md`.
4. Fallback colour roles listed.
5. Probe at least 3 art-heavy themes for that slot (size/caps sanity).

**First wave (ready enough to implement next):**

| Surface | Art | Colour fallback |
|---|---|---|
| Push button | 25 / 26 / (27) | `button.*` |
| V/H scrollbar body + thumb + grips | 181/185/188, 162/166/169 | `scrollbar.*` |
| Column header | 150 / 151 | `column_header.*` + Primary Label |
| Window gel frame + title boxes (close / **Window Menu** / min / max) | 220+ | `window.frame.*`, `window.close.*`, `window.menu.*`, `window.minimize.*`, `window.maximize.*` + Primary Label |

**Still open / deferred:**

| Item | Status |
|---|---|
| Menu Bar Hap indices | Empty across probed themes — colour/`menu_bar.*` keys only |
| `menu.item.disabled` Hap index | Paint key ready; no probed occupancy (TBD) |
| Full Icons catalog | Names recorded above; import beyond file/folder deferred |
| Editor Images / Icons / Information / Groups UI | Later wave (not Hap import) |

---

## Local research artefacts

| Path | What |
|---|---|
| `research/AppearanceEdit-Documentation.txt` | Extracted official PDF |
| `research/probe_haps.py` | Slot occupancy / geometry probe |
| `research/probe-report.txt` | Last probe run |
| `research/haps/` | Vendored Sagrado Appearances samples for Load / probes |
| AppearanceEdit 1.24 zip | https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip |

Do not commit AppearanceEdit `.exe` / zip downloads (`research/bin/`).
`.hap` samples under `research/haps/` are tracked so the editor can Load them.
