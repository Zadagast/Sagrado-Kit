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

The two sections use **different record headers** — verified over the 111-theme
corpus, where all 4101 image records tile their section exactly with a 20-byte
header and all 1696 icon records tile exactly with an 8-byte one (each icon
record overruns its successor if read as an image):

| Record | Header | Contents |
|---|---|---|
| Image | 20 bytes | w, h, flags/bpp, palette len, transparent index, transparent colour, caps[4], positions[4] |
| Icon | 8 bytes | w, h, flags/bpp, palette len, transparent index — **no** transparent colour, caps or positions |

Icon slots come in (16 px, 32 px) pairs at `slot`, `slot + 1`.
AppearanceEdit exposes icons across **four** panels — Icons, File Icons and
Mouse Icons in addition to Images — which is why the icon section holds ~100
authored records in art-heavy themes.

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
| Primary Background Pattern | **17** | `primary.background_pattern` — tiled behind window interiors (24/111 themes, up to 280×1600) |
| Small Plus / Minus | **81** / **85** | `disclosure.plus.small`, `disclosure.minus.small` |
| Medium Plus / Minus | **263** / **267** | `disclosure.plus.medium`, `disclosure.minus.medium` — same 26 themes as 81/85, larger glyphs |
| Focus Box Normal/Hilited/Disabled | **101** / **102** / **103** | `focus_box.*` — 7×7-ish frame, caps 3 on all sides |
| Horiz / Vert Separator | **105** / **106** | `separator.h`, `separator.v` |
| Box / Framed Raised Box | **107** / **108** | `box`, `framed_raised` |
| Progress Bar / Fill | **111** / **112** | `progress.bar`, `progress.fill` |
| Progress Bar Non-Fill | **113** | `progress.non_fill` — 9-sliced over the *unfilled* remainder, after the fill |
| Progress Bar Digit 0–9 | **114**–**123** | `progress.digit.0`…`.9` — bitmap glyphs stamped centred; all ten required |
| Progress Bar Digit 100% | **124** | `progress.digit.full` — single record replacing "100" at completion |
| Color Chooser Normal/Hilited/Disabled | **281** / **282** / **283** | `color_chooser.*` — swatch well; Positions = well inset |
| WonderLight Off/Pause/Ready/Go/Finished | **251**–**255** | `wonderlight.*` — 16×16 status lamp |
| WonderLight Flash Off/On1/On2 | **256**–**258** | Flash attention lamp |

### Menu (open list)

| AppearanceEdit name | Hap slot | Paint |
|---|---|---|
| Menu Background Pattern | **200** | Optional tiled pattern (else Menu Background colour) |
| Menu Background | **201** | 9-slice over item area; may use transparency over pattern |
| Menu Item Pattern Normal/Hilited/Disabled | **202** / **203** / **204** | Optional per-row tile under the item chrome |
| Menu Item Normal/Hilited/Disabled | **205** / **206** / **207** | Per-row chrome above background |
| Menu Separator | **208** | Horizontal rule; L/R caps; vertically centred |
| Popup Window Frame Normal/Focus | **248** / **249** | Frame around popup/menu; **Positions = thickness** (even); centre transparent |
| Menu First / Last Item Hilited | **209** / **210** | Replaces 206 on the top/bottom row so the hilite meets the menu's rounded ends |

The menu group is numbered **densely** (200…208, unlike the 4-slot blocks used
by buttons), so the item states are 205/206/207 — the kit previously read
206/207 as normal/hilited, i.e. it painted the hilite art on idle rows.
Occupancy backs the dense reading: 203 and 206 (the *hilited* pattern and item)
are the most authored rows, while normals are usually left to colours.
Menu **Bar** art (a Mac-only surface) has no occupancy in any corpus theme, but
AppearanceEdit 1.4 does expose it, and its rows sit at **271** Menu Bar Pattern,
**272** Menu Bar, **273**/**274**/**275** Menu Bar Title Pattern
Normal/Hilited/Disabled and **277**/**278**/**279** Menu Bar Title
Normal/Hilited/Disabled (276 is a hole between the two triples).

Colours always apply for label / hilite / disable ink even when art is present.

**SagradoKit names:** `menu.background_pattern`, `menu.background`,
`menu.item.normal`, `menu.item.hilited`, `menu.item.disabled`,
`menu.item.first_hilited`, `menu.item.last_hilited`, `menu.separator`,
`popup_frame.normal`, `popup_frame.focus`, `menu_bar.pattern`,
`menu_bar.background`, `menu_bar.title_pattern.*`, `menu_bar.title.*`.

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

### Icons (Icons section, 8-byte records)

Slots verified by decoding the section across the corpus (contact sheets from
`research/dump_hap.py`) — the four alert icons match AppearanceEdit's Icons
panel row order, the rest are identified by artwork that is identical in every
theme that authors them. Each entry is a 16 px / 32 px pair.

| Icon | Hap slots | SagradoKit |
|---|---|---|
| Stop / Note / Caution / Question | **4**, **8**, **12**, **16** (+1 = 32 px) | `alert.stop.*`, `alert.note.*`, `alert.caution.*`, `alert.question.*` |
| Generic file / Folder | **68** / **160** | `file.generic.*`, `folder.*` |
| Server / Data Transfer | **240** / **248** | `server.*`, `data_transfer.*` |
| Help / Information | **300** / **308** | `help.*`, `information.*` |
| Message / Launch | **312** / **324** | `message.*`, `launch.*` |
| Connect / Disconnect | **336** / **340** | `connect.*`, `disconnect.*` |
| News / Chat / Users / Files | **348** / **352** / **360** / **372** | `news.*`, `chat.*`, `users.*`, `files.*` |
| Document Saved / Unsaved | **376** / **380** | `document_saved.*`, `document_unsaved.*` |

Icon slot **ids are not the panel row order** beyond the four alerts (e.g. row
17 "Download" is not slot 68), so no further names are claimed here. The
remaining occupied ranges are the file-type and label-coloured folder icons
(164–213) and the Mouse Icons set.

### Icon Button / Menu Bar / Column header

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Icon Button Normal/Hilited/Disabled | **49** / **50** / **51** | `icon_button.*`; icon + optional title |
| Column Header Normal/Hilited/Disabled | **150** / **151** / **152** | List headers; disabled often donor-filled |
| Menu Bar Pattern / Menu Bar | **271** / **272** | `menu_bar.pattern` (tiled, clipped to the strip), `menu_bar.background` |
| Menu Bar Title Pattern N/H/D | **273** / **274** / **275** | `menu_bar.title_pattern.*` tiled under each title cell |
| Menu Bar Title N/H/D | **277** / **278** / **279** | `menu_bar.title.*`; colour path stays the fallback (rarely authored) |

### Column header / gel (related)

| AppearanceEdit name | Hap slot | Notes |
|---|---|---|
| Column Header Normal/Hilited/Disabled | **150** / **151** / **152** | List headers + often tabs |
| Window Frame Normal/Focus | **220** / **221** | Positions = frame thickness; centre transparent |
| Window Close/Min/Max/Menu … | **223+** | Groups of 5: Normal, Focus, Hilited, **Disabled**, (spare) |
| Window Close Normal/Focus/Hilited/Disabled | **223**–**226** | `window.close.*` |
| Window Minimize … | **228**–**231** | `window.minimize.*` |
| Window Maximize … | **233**–**236** | `window.maximize.*` |
| Window Menu Normal/Focus/Hilited/Disabled | **238**–**241** | Hilited (240) is AppearanceEdit 1.4-only; unauthored in corpus themes |
| Window Resize Normal/Focus | **243** / **244** | Bottom-right; no transparent colour |

The Disabled variants share their group's Positions exactly (same glyph
placement as Normal/Focus/Hilited) across all corpus themes that author them —
that is how 226/231/236/241 were identified.

**How the last slots were pinned:** AppearanceEdit ticks a checkmark beside
every row a theme authors, so injecting one record at a candidate slot into a
known-good theme and reading which row gains a tick names that slot outright.
That is the evidence for 113/114–124, 209/210, 271–275, 277–279 and 281–283 —
the previously "occupied but unnamed" 271 and 272/277/278/279 among them. No
image slot authored anywhere in the 111-theme corpus is unmapped now.

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

**Needs index probe before art paint:**

Menu bar Hap indices (empty across probed themes), popup window frame (rare),
full Hap icon catalog beyond file/folder/user.

---

## Local research artefacts

| Path | What |
|---|---|
| `research/AppearanceEdit-Documentation.txt` | Extracted official PDF |
| `research/probe_haps.py` | Slot occupancy / geometry probe |
| `research/probe-report.txt` | Last probe run |
| AppearanceEdit 1.24 zip | https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip |

Do not commit the `.exe` / `.hap` binaries; re-download when probing.
