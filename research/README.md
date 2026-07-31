# Research notes

Working files for mapping Haxial’s Appearance Engine into SagradoKit.

| File | Purpose |
|---|---|
| `AppearanceEdit-Documentation.txt` | Official AppearanceEdit 1.200 PDF, extracted |
| `probe_haps.py` | Slot occupancy / caps / positions across `.hap` samples |
| `probe-report.txt` | Last probe output |
| `build_completion_pack.py` | Rebuild `format/skins/completion/` soft-fill pack |
| `extract_milk_redux.py` | Extract Milk Redux `.hap` → donor-filled `.sap` |
| `build_ooze_theme.py` | Generate `format/skins/ooze/` (aluminum gel, pinstripes, traffic lights) |
| `ft_qa.cpp` | Headless File Transfers / WonderLight / LED dump harness |

Sample `.hap` files live in [`haps/`](haps/) (copied from Sagrado
`themes/Appearances/`). Load them in the editor via **Load**, or probe with
`python3 research/probe_haps.py`.

AppearanceEdit itself stays out of git — download when probing:

```sh
mkdir -p research/bin
wget -O research/bin/AppearanceEdit1240-Win.zip \
  https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip
unzip -d research/bin research/bin/AppearanceEdit1240-Win.zip
```

Canonical write-up for the kit: [`../docs/haxial-surface-map.md`](../docs/haxial-surface-map.md).

Regenerate the Ooze-look skin (original art; not imported from the Linux desktop):

```sh
python3 research/build_ooze_theme.py   # → format/skins/ooze/ooze.sap + .skimg
```
