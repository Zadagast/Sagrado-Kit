# Sagrado Kit — Roadmap

Living “where are we?” doc. Update this when a slice lands on `main`.

**Everyday loop (the only one):**

```sh
git checkout main
git pull
make
make run             # Sagrado Jabber (Wine). Alias: make run-jabber
```

Stay on `main`. Do **not** check out draft feature branches for daily use —
that is why launches feel like an “old or different version.” Review a PR
branch only while reviewing that PR.

Ignore stale draft PRs that already landed via [#28](https://github.com/Zadagast/Sagrado-Kit/pull/28)
(TextEdit / Jabber / kit authoring stack) and older kit research drafts.

---

## You are here

| Area | Status | Notes |
|---|---|---|
| Kit + Ooze Gel + Hap import | **On main** | Appearance editor, skins, Hap → .sap |
| TextEdit | **On main** | First kit consumer app |
| Jabber foundation | **On main** | Roster, 1:1, MUC, upload, reactions, compose |
| Zoom bounce fix | **On main** | [#29](https://github.com/Zadagast/Sagrado-Kit/pull/29) |
| MAM scroll-back (1:1) | **On main** | [#30](https://github.com/Zadagast/Sagrado-Kit/pull/30) |
| Modern chat + bundled skins | **Landing** | This PR — Gajim transcript, Gamespot default, full `format/skins/` ship, Ooze pack; `make run` = Jabber |
| OMEMO 1:1 | **Next** | [#31](https://github.com/Zadagast/Sagrado-Kit/pull/31) — rebase onto main after this lands |
| Jingle (voice/video) | **After OMEMO** | Not started |
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

### 3. Landing — modern chat + bundled skins
Gajim-shaped transcript (avatars, nick colors, multi-react, compose toolbar),
Gamespot cold-start default, all Haps + Ooze/Milk under `format/skins/`,
Appearance / Themes pickers. Everyday: `make run` launches Jabber.

### 4. Next — OMEMO 1:1 ([#31](https://github.com/Zadagast/Sagrado-Kit/pull/31))
Conversations/Gajim wire (`eu.siacs.conversations.axolotl`), disk keystore,
PEP device list + bundle, encrypt/decrypt, `*` on encrypted lines.
**After merge:** `git checkout main && git pull && make && make run`

Still later (not blocking Jingle):
- Fingerprint / trust UI
- MUC OMEMO
- OMEMO 0.8+ SCE

### 5. After that — Jingle
AIM-era “call buddy” feeling over XMPP:
- Jingle session setup (audio first; video only if it stays simple)
- ICE / STUN path that works under Wine + real Windows
- Gel call UI (one job: connect / mute / hang up — no dashboard)

### 6. Later candidates (unordered)
- MUC archived history (MAM for rooms)
- Inline chat media (HTTP Upload URLs rendered in transcript)
- Move-to-group UI for roster
- Saved passwords / keyring (explicitly deferred)
- Adium-style HTML message styles (explicitly deferred)

---

## Kit / editor track

Mostly landed on main via [#28](https://github.com/Zadagast/Sagrado-Kit/pull/28).
Polish when something hurts authoring — do not block Jabber features for kit nits.
Editor launch: `make run-editor` (not `make run`).

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
everyday         → git checkout main && git pull && make && make run
after this lands → Kit + TextEdit + modern Jabber + bundled skins
next             → OMEMO 1:1 (#31)
after that       → Jingle
parked           → trust UI, MUC OMEMO, room MAM, inline media, AppImage
```
