# Sagrado Jabber — “You’ve Got Mail” IM

AIM-era buddy-list IM painted in Sagrado gel, speaking **Jabber (XMPP)** — not
Matrix. Matrix is room/space-first; AOL Instant Messenger was contact-first.
XMPP’s roster + presence + 1:1 chat is that shape.

**App:** `apps/jabber/` → `build/SagradoJabber.exe`  
**Standard:** follows the [Sagrado Apps standard](contract.md#sagrado-apps-standard)
(TextEdit is still the chrome reference; Jabber is the second consumer).

## AOL / AIM UX ↔ XMPP

| AIM feeling | Sagrado Jabber | Wire |
|---|---|---|
| Buddy list | Left roster pane | Roster IQ + presence |
| Buddy signed on / off | Status strip alert | Presence stanzas |
| IM windows / tabs | Center tab strip + transcript | `<message type='chat'>` |
| Available / Away / Busy / Invisible | Buddy menu | `<show>` / unavailable |
| You’ve got mail ding | `MessageBeep` hook on inbound IM | Client event |
| Send a file to a buddy | Chat → Send File… | [XEP-0363](https://xmpp.org/extensions/xep-0363.html) HTTP Upload |
| Chat rooms | Chat → Join Chat Room… | [XEP-0045](https://xmpp.org/extensions/xep-0045.html) MUC |
| Get a screen name | File → Get an Account… | [XEP-0077](https://xmpp.org/extensions/xep-0077.html) + [XEP-0158](https://xmpp.org/extensions/xep-0158.html) CAPTCHA **in gel** |

There is **no browser signup happy path**. If a server only offers a website, the
UI tells you to pick a recommended host from `apps/jabber/providers.txt`.

## Layout

1. **Left** — Buddies (online first; offline dimmed)
2. **Center** — IM tabs, transcript, compose + Send
3. **Menus** — File, Buddy, Chat, Appearance, Help (Ooze Gel frame)
4. **Status strip** — “Signed on as …”, presence alerts, upload progress
5. **MUC** — optional occupants column when a room tab is focused

## Accounts

Jabber is federated: a screen name is a **JID** (`name@server`).

**Sign On** — existing JID + password (SASL PLAIN over STARTTLS).  
**Get an Account** — nick + password on a curated/local server:

1. IQ-get `jabber:iq:register`
2. If the form embeds a CAPTCHA image URI → HTTPS GET → decode → blit in gel
3. User answers in the same dialog → IQ-set → auto Sign On

Prosody dogfood: enable `allow_registration` (CAPTCHA module optional for QA).

## Stack

| Piece | Choice |
|---|---|
| UI thread | Win32 + Appearance Engine canvas |
| XMPP | Thin C++ client (`apps/jabber/xmpp/`) — Winsock + Schannel STARTTLS |
| HTTPS | WinHTTP (CAPTCHA media + HTTP Upload PUT) |
| Images | `stb_image` → `SkinImage` for gel blit |
| Events | Worker thread → `WM_JABBER_EVENT` on the UI queue |

libstrophe / OpenSSL were skipped for the MinGW cross-build: Schannel + WinHTTP
cover the same product surface (TLS XMPP, CAPTCHA fetch, upload PUT).

## Build / run

```sh
make                 # includes SagradoJabber.exe
make run-jabber      # Wine
make smoke           # includes jabber paint smoke (no network)
```

Curated providers live in `apps/jabber/providers.txt` (copied next to the exe).

## Non-goals (v1)

- Matrix / Spaces / Discord server rail
- Browser or OOB web registration as a supported path
- OMEMO, Jingle, full MAM browser, Adium HTML message styles
- libpurple multi-protocol
