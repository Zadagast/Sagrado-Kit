# Research notes

Working files for mapping Haxial’s Appearance Engine into SagradoKit.

| File | Purpose |
|---|---|
| `AppearanceEdit-Documentation.txt` | Official AppearanceEdit 1.200 PDF, extracted |
| `probe_haps.py` | Slot occupancy / caps / positions across `.hap` samples |
| `probe-report.txt` | Last probe output |

Download AppearanceEdit (do not commit the binary):

```sh
mkdir -p research/bin research/haps
wget -O research/bin/AppearanceEdit1240-Win.zip \
  https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip
unzip -d research/bin research/bin/AppearanceEdit1240-Win.zip
```

Sample `.hap` files: copy from Sagrado `themes/Appearances/` into `research/haps/`,
then `python3 research/probe_haps.py`.

Canonical write-up for the kit: [`../docs/haxial-surface-map.md`](../docs/haxial-surface-map.md).
