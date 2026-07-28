# Research notes

Working files for mapping Haxial’s Appearance Engine into SagradoKit.

| File | Purpose |
|---|---|
| `AppearanceEdit-Documentation.txt` | Official AppearanceEdit 1.200 PDF, extracted |
| `KDX-Client-Documentation.txt` | KDX Client Documentation.pdf, extracted (TechnoWiki Client zip) |
| `haxial-docs-gap-inventory.md` | Live Haxial doc sources + tiered kit gaps |
| `probe_haps.py` | Slot occupancy / caps / positions across `.hap` samples |
| `probe-report.txt` | Last probe output |

Download AppearanceEdit / KDX Client docs (do not commit the binaries):

```sh
mkdir -p research/bin research/haps
wget -O research/bin/AppearanceEdit1240-Win.zip \
  https://kdx.technowiki.info/downloads/AppearanceEdit1240-Win.zip
wget -O research/bin/KDXClient1600-Win.zip \
  https://kdx.technowiki.info/downloads/KDXClient1600-Win.zip
unzip -d research/bin research/bin/AppearanceEdit1240-Win.zip
```

Sample `.hap` files: copy from Sagrado `themes/Appearances/` into `research/haps/`,
then `python3 research/probe_haps.py`.

Canonical write-up for the kit: [`../docs/haxial-surface-map.md`](../docs/haxial-surface-map.md).
Gap inventory vs internet sources: [`haxial-docs-gap-inventory.md`](haxial-docs-gap-inventory.md).
