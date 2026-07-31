# Sagrado Jabber — “You’ve Got Mail” IM

AIM-era buddy-list IM painted in Sagrado gel, speaking **Jabber (XMPP)** — not
Matrix. Matrix is room/space-first; AOL Instant Messenger was contact-first.
XMPP’s roster + presence + 1:1 chat is that shape.

**App:** `apps/jabber/` → `build/SagradoJabber.exe`  
**Standard:** follows the [Sagrado Apps standard](contract.md#sagrado-apps-standard)
(TextEdit is still the chrome reference; Jabber is the second consumer).

## AOL / AIM UX ↔ XMPP

| AIM / Yahoo feeling | Sagrado Jabber | Wire |
|---|---|---|
| Identity strip (you) | Left column above Buddies | Own nick/avatar + `<presence>` |
| Custom status line | Editable field under your name | `<presence><status>` |
| Available / Away / Busy / Invisible | Click presence mark (or Buddy menu); marks are fixed green / amber / red / grey (not skin roles) | `<show>` / unavailable |
| Buddy list | Group headers + two-line rows (name + status); new adds land in **Buddies** | Roster IQ + `<group>` + presence |
| Authorize / deny add | Gel Accept/Deny sheet (“X wants to add you”); tray balloon if hidden | Inbound `subscribe` / `subscribed` / `unsubscribed` |
| Remove buddy | Buddy → Remove Buddy… (active 1:1 tab or roster selection) | Roster `subscription='remove'` + unsubscribe |
| Buddy signed on / off | Status strip alert | Presence stanzas |
| Avatar / display name | Tile + nick; Buddy → Set Picture… (or click your tile) | [XEP-0054](https://xmpp.org/extensions/xep-0054.html) vCard PHOTO (+ [XEP-0153](https://xmpp.org/extensions/xep-0153.html) update) |
| IM windows / tabs | Center tab strip + transcript; **x** closes a tab | `<message type='chat'>` |
| Typing | “X is typing…” above compose | [XEP-0085](https://xmpp.org/extensions/xep-0085.html) chat states |
| Delivered | “ ok” on your 1:1 lines when the peer acks | [XEP-0184](https://xmpp.org/extensions/xep-0184.html) receipts |
| Multi-device | Other resources’ chats land in this transcript | [XEP-0280](https://xmpp.org/extensions/xep-0280.html) carbons |
| Recent history | Opening an empty 1:1 chat loads the last ~40 lines | [XEP-0313](https://xmpp.org/extensions/xep-0313.html) MAM |
| You’ve got mail ding | `MessageBeep` hook on inbound IM | Client event |
| Send a file to a buddy | Chat → Send File… | [XEP-0363](https://xmpp.org/extensions/xep-0363.html) HTTP Upload |
| Chat rooms | Chat → Browse Chat Rooms… / Join… / Leave | [XEP-0045](https://xmpp.org/extensions/xep-0045.html) MUC |
| Room bookmarks + autojoin | Chat → Bookmark Room / Autojoin Room | [XEP-0402](https://xmpp.org/extensions/xep-0402.html) PEP + [XEP-0048](https://xmpp.org/extensions/xep-0048.html) fallback |
| Get a screen name | File → Get an Account… | [XEP-0077](https://xmpp.org/extensions/xep-0077.html) + [XEP-0158](https://xmpp.org/extensions/xep-0158.html) CAPTCHA **in gel** |

There is **no browser signup happy path**. If a server only offers a website, the
UI tells you to pick a recommended host from `apps/jabber/providers.txt`.

## Layout

1. **Left identity strip** — avatar, display name, presence mark, status message field.
   When signed off but a JID is remembered, the strip still shows you (`Signed off`;
   click → Sign On). When online, click your avatar (or Buddy → Set Picture…) to
   change your icon — published as your vCard photo (max ~96 KB).
2. **Left Buddies** — roster groups (default **Buddies**); online-first within each group;
   buddy icon + presence mark + name + status text
3. **Center** — IM tabs (close with **x**), soft-wrapped transcript (kit `layout_lines`),
   compose + Send; peer typing line when composing
4. **Kit V scrollbars** — `paint_scrollbar` on transcript, Buddies, Browse Chat Rooms, Get an Account providers, and Sign On recent JIDs when content overflows (hidden otherwise; wheel + thumb/arrows)
5. **Menus** — File, Buddy, Chat, Appearance, Help (Ooze Gel frame)
6. **Status strip** — durable line: signed on as / presence / buddies online / active
   chat or room (occupant count). Brief flashes for mail, errors, uploads — not every
   room chat line
7. **MUC** — full-height “In room” rail beside transcript **and** compose; sticky topic
   under the transcript top; Chat → Browse Chat Rooms… lists bookmarks + public disco rooms
8. **System tray** — gel close (X) hides to the notification area; minimize stays a
   normal taskbar minimize. Tray menu: Open / Sign On / Sign Off / Quit; inbound IM
   can balloon while hidden

Kit paint only: `list.*` / `primary.*` / `paint_field` / `paint_menu` — no OS widgets for identity.

## Auto join server

On launch, if `accounts.txt` has a remembered JID, **Sign On** opens with that name
and focus on the password field — passwords are still never stored. Enter the
password to join the server (and any bookmarked autojoin rooms). Quit from
**File → Quit** or the tray menu; the gel close box only hides to the tray.

## Chat rooms (everyday MUC)

Day-to-day room parity (Psi/Gajim/Conversations shape) — not operator tools.

- **Browse Chat Rooms…** — disco finds the conference service, then lists public rooms;
  Bookmarks section sits above the public list. Select + nick → Join (or double-click).
- **Join Chat Room…** — type `room@conference.host` + nick by hand.
- **Leave Room** — unavailable presence; closes the room tab.
- **Topic** — `<subject>` shown as a sticky line + system transcript line.
- **Nicks** — groupchat lines and the occupants column use the resource (`room/nick`),
  not the room localpart.
- **Bookmark Room / Autojoin Room** — saved via PEP `urn:xmpp:bookmarks:1` when the
  server supports it, with Private XML `storage:bookmarks` as fallback. Autojoin
  rooms rejoin on Sign On.

Still **not** Matrix: no spaces rail, kick/ban UI, room config forms, invites, or MAM
history browser in this pass.

## Accounts

Jabber is federated: a screen name is a **JID** (`name@server`).

**Sign On** — existing JID + password (SASL PLAIN over STARTTLS).  
Successful sign-ons remember the JID in `accounts.txt` next to the exe (names only —
**never passwords**). Sign On prefills the last account; with two or more, a compact
picker sits between screen name and password.  
**Get an Account** — screen name + password on a curated home server:

1. Choose a host from `providers.txt` (Category A IBR from [providers.xmpp.net](https://providers.xmpp.net/))
2. IQ-get `jabber:iq:register`
3. If the form embeds a CAPTCHA image URI → HTTPS GET → decode → blit in gel
4. User answers in the same dialog → IQ-set → auto Sign On

## Stack

| Piece | Choice |
|---|---|
| UI thread | Win32 + Appearance Engine canvas |
| XMPP | Thin C++ client (`apps/jabber/xmpp/`) — Winsock + **mbedTLS** STARTTLS |
| HTTPS | WinHTTP (CAPTCHA media + HTTP Upload PUT) |
| Images | `stb_image` → `SkinImage` for gel blit |
| Events | Worker thread → `WM_JABBER_EVENT` on the UI queue |

STARTTLS uses vendored mbedTLS (Wine-safe; select-based deadlines). WinHTTP
covers CAPTCHA fetch and HTTP Upload PUT. libstrophe / system OpenSSL are not
required for the MinGW cross-build.

## Build / run

```sh
make                 # includes SagradoJabber.exe
make run-jabber      # Wine
make smoke           # includes jabber paint smoke (no network)
make jabber-connect-smoke   # Wine: TCP + mbedTLS STARTTLS + register form (yax.im)
```

Home servers live in `apps/jabber/providers.txt` (copied next to the exe).
Default pick is the first entry (`yax.im`).
Recent account JIDs live in `accounts.txt` beside the exe (written on successful Online).

## Non-goals (v1)

- Matrix / Spaces / Discord server rail
- Browser or OOB web registration as a supported path
- Saved passwords / keyring
- OMEMO, Jingle, full MAM history browser (this pass only pulls recent lines into an empty chat), Adium HTML message styles
- Move-to-group UI (groups display from roster; adds use **Buddies**)
- libpurple multi-protocol
