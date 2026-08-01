# Sagrado Kit — Roadmap

Living “where are we?” doc. Update this when a slice lands on `main`.

**Everyday loop (preferred):**

```sh
git checkout main
git pull
make                 # or: make run-jabber
```

Do **not** sit on stacked feature branches unless you are reviewing that PR.
Ignore stale draft PRs that already landed via [#28](https://github.com/Zadagast/Sagrado-Kit/pull/28)
(TextEdit / Jabber / kit authoring stack).

---

## You are here

| Area | Status | Notes |
|---|---|---|
| Kit + Ooze Gel + Hap import | **On main** | Appearance editor, skins, Hap → .sap |
| TextEdit | **On main** | First kit consumer app |
| Jabber foundation | **On main** | Roster, 1:1, MUC, upload, reactions, compose |
| Zoom bounce fix | **On main** | [#29](https://github.com/Zadagast/Sagrado-Kit/pull/29) |
| MAM scroll-back (1:1) | **On main** | [#30](https://github.com/Zadagast/Sagrado-Kit/pull/30) |
| OMEMO 1:1 | **In review** | [#31](https://github.com/Zadagast/Sagrado-Kit/pull/31) — merge next |
| Jingle (voice/video) | **Next after OMEMO** | Not started |
| AppImage packaging | **Opt-in only** | Do not build unless asked |

---

## Jabber track (priority order)

Ship one PR onto `main` at a time. Preferred order:

### 1. Done — foundation
Sign on / get account, roster, presence, 1:1 chat, carbons, receipts, typing,
HTTP Upload, reactions, MUC (+ bookmarks), kit compose + context menus.
See [jabber.md](jabber.md).

### 2. Done — MAM scroll-back
Cold-open ~40 lines; scroll-up pages older archive; viewport preserved.
Rooms still live-only.

### 3. Now — OMEMO 1:1 ([#31](https://github.com/Zadagast/Sagrado-Kit/pull/31))
Conversations/Gajim wire (`eu.siacs.conversations.axolotl`), disk keystore,
PEP device list + bundle, encrypt/decrypt, `*` on encrypted lines.
**After merge:** `git checkout main && git pull && make run-jabber`

Still later (not blocking Jingle):
- Fingerprint / trust UI
- MUC OMEMO
- OMEMO 0.8+ SCE

### 4. Next — Jingle
AIM-era “call buddy” feeling over XMPP:
- Jingle session setup (audio first; video only if it stays simple)
- ICE / STUN path that works under Wine + real Windows
- Gel call UI (one job: connect / mute / hang up — no dashboard)

### 5. Later candidates (unordered)
- MUC archived history (MAM for rooms)
- Move-to-group UI for roster
- Saved passwords / keyring (explicitly deferred)
- Adium-style HTML message styles (explicitly deferred)

---

## Kit / editor track

Mostly landed on main via [#28](https://github.com/Zadagast/Sagrado-Kit/pull/28).
Polish when something hurts authoring — do not block Jabber features for kit nits.

Open research (not a build queue): Haxial surface gaps in
[haxial-surface-map.md](haxial-surface-map.md).

---

## How we work

1. **Base = `main`.** New work: `cursor/<name>-99c1` off current `main`, one PR, merge, delete branch.
2. **No deep stacks.** If you are lost, return to `main` and pull.
3. **Product language:** Ooze Gel = window frame (header, traffic lights, drag/resize). Not “chrome.”
4. **Do not** build the Ooze AppImage unless explicitly asked.
5. When a roadmap item ships, move it to **Done** in this file in the same PR (or a tiny follow-up).

---

## Quick status cheat-sheet

```
main today     → Kit + TextEdit + Jabber + MAM
open PR #31    → OMEMO 1:1          ← merge this
after that     → Jingle
parked         → trust UI, MUC OMEMO, room MAM, AppImage
```
