# Haxial docs gap inventory

Cross-check of live Haxial documentation sources against SagradoKit
([`docs/haxial-surface-map.md`](../docs/haxial-surface-map.md),
[`docs/contract.md`](../docs/contract.md)). Research only — no paint changes
in this pass.

Fetched / extracted: 2026-07-28.

---

## Sources

| Source | What it gives | Local / URL |
|---|---|---|
| TechnoWiki downloads | AppearanceEdit, KDX Client/Server/Tracker, Sound List Editor | https://kdx.technowiki.info/downloads.php |
| AppearanceEdit **1.200 Documentation.pdf** | Official Appearance Engine authoring (23 pp.) | Zip: `research/bin/AppearanceEdit1240-Win.zip` (gitignored). Extract: [`AppearanceEdit-Documentation.txt`](AppearanceEdit-Documentation.txt) |
| KDX Client **Documentation.pdf** (1.520 text, Client 1.600 zip) | App chrome: Button Bar, FT, Settings → Appearance, WonderLight blink, columns | Zip: `research/bin/KDXClient1600-Win.zip` (gitignored). Extract: [`KDX-Client-Documentation.txt`](KDX-Client-Documentation.txt) |
| Haxial Wiki – Appearances | Theme list + `%HAP` header | https://haxial.fandom.com/wiki/Appearances (often Cloudflare-blocked; thin vs our probes) |
| Softpile / willsoftware blurbs | Marketing (Appearance Engine, `.hap` load) | Unverified “mouse icons” claim — **not** in AppearanceEdit 1.200 PDF |
| ProfDrLuigi/Haxial-KDX | Install notes → bundled Documentation.pdf | https://github.com/ProfDrLuigi/Haxial-KDX |
| Sagrado-ref colour map | Full 204-entry Hap colour semantics | `/tmp/sagrado-ref/docs/hap-color-table.md` (absorbed into Kit colour roles) |

**Dead / non-sources:** `haxial.com`, `haxialsoftware.com` (down). **Sound List Editor** is sounds, not themes.

### Re-fetch recipe

```sh
mkdir -p research/bin
curl -fsSL -o research/bin/AppearanceEdit1240-Win.zip \
  https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip
curl -fsSL -o research/bin/KDXClient1600-Win.zip \
  https://kdx.technowiki.info/downloads/KDXClient1600-Win.zip
# PDFs live inside the zips; text extracts are tracked under research/
```

Do not commit `research/bin/` (see `.gitignore`).

---

## Already covered by the kit (do not re-litigate)

- Hap live load + soft-complete; Save `.sap` (+ `.skimg`); Hap write out of scope
- Editor: Info typing, Colors + Import + Colors Preview + ♦ Groups, Images/Icons full catalogs + Paste
- Paint: gel, buttons, ticks/mutex, field, dropdown/menu, menu bar (colour + optional art keys), sliders, scrollbars, progress Continuous + Segmented, WonderLight, icons, File Transfers sample
- Known deferred already listed: Menu Bar Hap indices (empty in themes), `menu.item.disabled` Hap index, Flash frame timing, decimal/CSV colour import

---

## Tier A — Appearance Engine semantics (docs debt; small paint risk)

Under-documented in the surface map vs AppearanceEdit PDF. Kit paint often already matches; the gap is **contract clarity**.

| # | Rule | PDF / KDX cue | Kit status |
|---|---|---|---|
| A1 | **Menu Bar / open-menu draw order** | Pattern → Bar → Title/Item Pattern → Title/Item; transparent middle of Bar reveals Pattern (pp. 18–19) | `paint_menu_bar` / menu painters exist; stack not spelled in surface map |
| A2 | **Scrollbar Disabled shared** | One Disabled image for double *and* single arrows (p. 15 / 17) | Slots mapped; shared-art rule not written |
| A3 | **Too Small** | When bar too small for Single Arrows **per its caps**, show Too Small instead (p. 15 / 17) | Slot painted; trigger rule under-documented |
| A4 | **Progress Fill Positions** | L/R begin/end insets; T/B vertical clamp (p. 13) | Honored in Continuous path; map said only “inset” |
| A5 | **No non-rectangular window frames** | Transparent Color cannot cut window frame shape (p. 9) | Not in authoring-rules table |
| A6 | **Frame caps may exceed thickness** | Caps can be larger than Positions thickness (PDF window-frame notes) | Partially implied; not explicit |
| A7 | **Popup Symbol Positions** | L then R; T then B; 0 falls through / centre (p. 12) | Map: “Positions place the arrow” only |
| A8 | **Framed Raised Box placard** | Edge-to-edge scroll; no focus box; window = focus (p. 13) | Slot only |
| A9 | **WonderLight host meaning** | AE: FT activity + Flash for attention (p. 21). KDX: Button Bar blinks for unread messages (Client doc p. 10) | Paint + static Flash strip; host timing / FT vs messages split not in surface map |

---

## Tier B — Editor authoring polish (not blockers)

| Item | Notes |
|---|---|
| Decimal / CSV colour entry | AppearanceEdit Colors panel; Kit = RGB sliders + `#RRGGBB` |
| Image paste **context menu** | AE second-click / control-click on paste area (p. 8) |
| Hap write | Explicitly out of scope — Load `.hap`, Save `.sap` |

---

## Tier C — App-host concerns (first-app territory)

From KDX Client Documentation (not AppearanceEdit chrome slots):

| Item | Notes |
|---|---|
| `Appearances/` folder + Settings Appearance popup | Client doc Appearance Panel (p. 14) |
| Button Bar WonderLight flash animation | Blink when messages arrive; stop on focus/send — **period undocumented** |
| Live File Transfers queue | Start/Stop/Re-queue/Clear Finished; Kit has static sample only |
| Column resize / reorder / Save Window Location | Generic list chrome (Client doc ~p. 30) |
| Small icons in file/user lists | Settings toggles 16 vs 32 |

---

## Tier D — Unverified / ignore

| Claim | Verdict |
|---|---|
| Marketing “mouse icons” / arbitrary filetype icons beyond Hap Icons catalog | **No evidence** in AppearanceEdit 1.200 PDF |
| Sound List Editor | Separate product; not theme chrome |

---

## Recommended follow-ups (after this inventory)

1. **Docs** — fold Tier A into the surface map (this PR).
2. **Optional paint QA** — Menu Bar stack against any theme that authors `menu_bar.*`; Too Small trigger; Popup Symbol fall-through.
3. **First app** — host `Appearances/` load path + Flash timer when building KDX-like chrome (Tier C).

---

## Checklist vs kit readiness

| Area | Ready for first app? |
|---|---|
| Theme load / preview / author `.sap` | Yes |
| Core widget paint contract | Yes (Tier A = write-up, not blockers) |
| Hap round-trip write | No (by design) |
| Live FT / message Flash | No (app feature) |
| Sound List | N/A |
