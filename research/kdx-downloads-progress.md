# KDX downloads & progress — research findings

Visual QA + unknowns pass for the Kit File Transfers sample.
Harness: [`research/ft_qa.cpp`](ft_qa.cpp) → `build/ft_qa`.
Artifacts: [`research/kdx-ft-qa/`](kdx-ft-qa/) (and `/opt/cursor/artifacts/kdx-ft-qa/`).

**No paint/protocol code changes in this pass** — findings only.

---

## Visual QA (`qa-ft-sample`)

Rendered `paint_file_transfers_window` + Continuous/Segmented compare strips under **stock**, **Milk Redux**, and **Ashen**.

### Hap art geometry

| Theme | `progress.bar` | `progress.fill` | WonderLight slots |
|---|---|---|---|
| Milk Redux | **7×15**, pos all 0, L/R caps 3 | **4×15**, pos all 0 | All **16×16** (AppearanceEdit-correct) |
| Ashen | **14×13**, pos 0 | **12×13**, pos 0 | All **12×12** (non-compliant; Kit centres in 16×16) |
| Stock | missing (colour path) | missing | missing (synthetic spheres) |

Other themes (spot check): Aluminum Alloy fill **3×15**; Boilerplate **15×14**; Function 2.0 **17×14**. Fill width is theme-authored — KDX LEDs are whatever Hap put in slot 112.

### LED geometry (Kit Segmented)

Code path (`paint_progress` Segmented): `kPad = 1`, `kGap = 1`, `seg_w = fill->w` (else 4).

**Milk mid-row scan** of a 400×15 Segmented strip @ 42%:

- Pattern settles to **4 px fill + 1 px gap** (matches authored fill width + Kit gap).
- Glossy mid-band of the LED is close to trough grey, so gaps are subtle — expected Hap look, not a missing gap.
- Continuous @ 65% is a single stretched fill (AppearanceEdit path); Segmented is clearly tiled columns.

**Ashen:** fill is **12 px** wide → few, fat LEDs. At ~42% the Progress cell can *read* almost continuous even though the painter is Segmented. That is theme art, not a Continuous branch inside FT.

**Stock colour path:** recessed trough + glossy `progress.transition.*` blocks; `led_tint` works when passed (see `led-tint-stock.png`).

### File Transfers window sample

| Check | Milk | Ashen | Stock |
|---|---|---|---|
| Gel title **File Transfers** | yes | yes | yes |
| Columns Lamp · Name · Size · Progress · Status · Rate | yes | yes | yes |
| Progress header hilited | yes | yes | yes |
| WonderLight per row (Finished / Go / Pause) | Hap spheres | 12×12 centred | synthetic spheres |
| Progress always Segmented | yes | yes (wide LEDs) | yes (tinted API unused — see below) |
| Name-column icons (file / folder / user) | yes | yes | fallback |
| Footer Close / Stop All / Clear Finished | yes | yes | yes |
| Continuous bar inside FT | **no** | **no** | **no** |

Sample rows exercise Finished@100%, Go@42% selected, Pause@10%.

### WonderLight Hap colours (Milk centre pixels)

| State | Milk Hap | Ashen Hap | Stock fallback (`wonderlight_color`) |
|---|---|---|---|
| Off | grey | grey | grey |
| Pause | **dark red** | red | **amber** (mismatch vs Hap) |
| Ready | **amber** | orange | **green** (same as Go — mismatch) |
| Go | green | green | green |
| Finished | blue | blue | blue |
| Flash Off | grey | grey | grey |
| Flash On1 | red | red | red |
| Flash On2 | amber | orange | red |

Hap art distinguishes **Ready (amber)** from **Go (green)**. Kit’s colour-sphere fallback collapses Ready→Go green and paints Pause as amber — fine for stock-only, wrong if anyone expects Hap semantics without art.

### Kit vs Hap paint notes (QA verdict)

1. **Milk FT sample matches the intended model** — Hap trough + tiled 4×15 fill LEDs, 16×16 lamps, Segmented only.
2. **Ashen is faithful to its Hap** — wider LEDs and undersized WonderLights are authored; Kit already centres non-16 lamps.
3. **Colour-path FT rows pass `led_tint = nullptr`** today, so stock LEDs stay transition grey even when the row’s WonderLight is Go/Finished/Pause. The tint API works (`led-tint-stock.png`); wiring it in the FT row painter is an optional polish follow-up, not a Hap requirement.
4. Kit still paints a **document/folder/user icon in Name** in addition to the lamp column. Prior FT plan preferred lamp-led rows; whether official KDX also shows type icons remains an open screenshot question (below).

---

## Open unknowns (`unknowns-kdx`)

Sources checked: AppearanceEdit docs, KDX Client manual excerpt (File Transfers Window), Kit plans/artifacts, Sagrado-ref `kdx.cpp` (FT stub only), TechnoWiki KDX site, public web search for FT screenshots. **No authentic in-app KDX File Transfers screenshot was found** in-repo or online in this pass.

| # | Question | Finding |
|---|---|---|
| 1 | Exact official column set / widths; Name `@server`; elapsed time | **Partially resolved.** Official KDX docs describe the FT window (Start/Stop/Re-queue, Clear All Finished Transfers, Open Downloads Folder) but **do not name columns or widths**. Kit’s Lamp · Name · Size · Progress · Status · Rate matches prior fidelity plans and is consistent with a column list UI. `@server` / elapsed (`0:02`) appeared only in older Kit stacked-card samples — **not** in AppearanceEdit or the KDX manual. Treat as Kit fiction until a real screenshot says otherwise. |
| 2 | LED colours Hap grey vs runtime tint by state | **Resolved for Hap path.** Segmented blit uses authored `progress.fill` as-is (Milk = glossy grey LEDs). Tint is colour-path only. No evidence KDX recolours Hap LEDs by WonderLight. Stock *may* tint; Kit FT currently does not call tint. |
| 3 | Exact LED gap/pad vs Kit gap=1, pad=1 | **Best available match.** Milk 4×15 fill + Kit gap/pad 1 produces a clean 4+1 cadence. No official pixel spec; AppearanceEdit only documents continuous fill positions. Unlikely to change without a screenshot. |
| 4 | Continuous ever inside File Transfers? | **No evidence for yes.** AppearanceEdit = continuous only. Kit FT = Segmented only. Docs never describe a second progress style. Treat Continuous-in-FT as out of scope. |
| 5 | WonderLight Ready vs Go; Flash On1/On2 timing | **Semantics partly resolved; timing open.** Docs: Off/Pause/Ready/Go/Finished = activity lamps for transfers; Flash Off/On1/On2 = attention (e.g. messages), On1/On2 alternate when flashing. Hap Milk paints Ready≠Go (amber vs green). **Flash period / frame rate not documented** anywhere found. Sagrado-ref uses WonderLight as a Messages badge, not FT LEDs. |

### Manual crumbs (not screenshots)

From the KDX Client documentation (File Transfers Window):

- Context: Start, Stop, Re-queue, Show/Open Local File, Move To Top, Remove Item.
- Window menu: **Clear All Finished Transfers**, Open Downloads Folder.
- Setting: Auto-Clear Finished Transfers.
- WonderLight on the **main Button Bar** blinks for unread messages (Flash path) — orthogonal to FT row lamps.

Footer buttons **Stop All** / **Clear Finished** in the Kit sample are a reasonable chrome stand-in; exact official button strip is still unverified without a screenshot.

---

## Sensible follow-ups (build work — not done here)

1. Optional: pass WonderLight-derived `led_tint` on colour-path FT rows only.
2. Optional: align stock `wonderlight_color` Pause/Ready with Hap Milk hues if stock spheres should preview Hap semantics.
3. Live transfer protocol / queue — separate app feature; out of AppearanceEdit scope.
4. If a real KDX FT screenshot appears: re-check Name `@server`, type icons beside lamp, footer labels, Flash timing.

---

## How to re-run QA

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Iengine research/ft_qa.cpp -o build/ft_qa
./build/ft_qa   # writes /opt/cursor/artifacts/kdx-ft-qa/*.ppm
```
