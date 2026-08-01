# Sagrado Jabber — XMPP Compliance Rundown

Target: **XMPP Compliance Suites 2023 (XEP-0479)** — Core + IM categories,
Advanced Client level. Audit of `apps/jabber/` against the required providers.
This is the living checklist; tick items as they land on main.

**Status: all required Core + IM providers (Advanced level) are implemented.**
Remaining ❌ entries below are non-required "of note" specs.

## Core Compliance Suite (Client)

| Feature | Provider | Status |
|---|---|---|
| Core features | RFC 6120 | ✅ stream, SASL PLAIN, STARTTLS |
| TLS | RFC 7590 | ✅ mbedTLS STARTTLS |
| Direct TLS (Advanced) | XEP-0368 SRV for XMPP-over-TLS | ✅ _xmpps-client SRV direct TLS, then _xmpp-client SRV, then host:5222 |
| Feature discovery | XEP-0030 Service Discovery | ✅ query + answer disco#info |
| Feature broadcasts | XEP-0115 Entity Capabilities | ✅ sha-1 ver hash in presence |
| Event publishing (Advanced) | XEP-0163 PEP | 🟡 PEP bookmarks + vCard photo update only |

## IM Compliance Suite (Client)

| Feature | Provider | Status |
|---|---|---|
| Core IM | RFC 6121 roster/presence | ✅ roster, subscriptions, presence |
| The /me Command | XEP-0245 | ✅ action lines in transcript |
| User Avatars (Advanced) | XEP-0084 User Avatar (PEP) | ✅ receive (metadata+notify → data fetch); vCard fallback kept |
| Avatar Compatibility (Advanced) | XEP-0153 vCard-Based Avatars | ✅ |
| vcard-temp | XEP-0054 | ✅ |
| Outbound Message Sync | XEP-0280 Carbons | ✅ |
| User Blocking (Advanced) | XEP-0191 Blocking Command | ✅ Buddy → Block/Unblock; blocklist fetch + server pushes |
| Group Chat | XEP-0045 MUC | ✅ join/topic/invite/leave/browse |
| Group Chat | XEP-0249 Direct MUC Invitations | ✅ send + receive (mediated receive kept) |
| Advanced Group Chat | XEP-0048 Bookmarks | ✅ (fallback) |
| Advanced Group Chat | XEP-0402 PEP Native Bookmarks | ✅ |
| Advanced Group Chat | XEP-0313 MAM | ✅ room archive on open + older paging |
| Advanced Group Chat | XEP-0410 MUC Self-Ping | ✅ 90s self-ping; auto-rejoin on error/timeout |
| Private data via PubSub (Adv.) | XEP-0223 | 🟡 via bookmarks path only |
| Private XML Storage (Adv.) | XEP-0049 | ✅ (legacy bookmarks) |
| Stream Management (Advanced) | XEP-0198 | ✅ enable + acks (r/a) + resume with retransmit |
| Message Acknowledgements (Adv.) | XEP-0184 Delivery Receipts | ✅ |
| History (Advanced) | XEP-0313 MAM | ✅ 1:1 paging |
| Chat States (Advanced) | XEP-0085 | ✅ typing |
| Message Correction (Advanced) | XEP-0308 | ✅ ↑ edits last 1:1 message; inbound replace + "(edited)" |
| File Upload | XEP-0363 HTTP Upload | ✅ |
| Direct File Transfer (Advanced) | XEP-0234 Jingle FT + XEP-0261 IBB | ✅ receive over IBB (accept, progress, save to downloads); sending stays on HTTP Upload |

## Specifications of note (non-required, listed by XEP-0479)

| Provider | Status |
|---|---|
| XEP-0077 In-Band Registration | ✅ (+ XEP-0158 CAPTCHA) |
| XEP-0066 Out-of-Band Data (mark uploads as attachments) | ✅ |
| XEP-0385 Stateless Inline Media Sharing | ❌ |
| XEP-0392 Consistent Color Generation | ✅ SHA-1 hue algorithm |
| XEP-0393 Message Styling | ✅ *bold* _italic_ ~strike~ `mono` spans in transcript |
| XEP-0433 Extended Channel Search | ❌ (disco#items browse only) |
| XEP-0424 Message Retraction / XEP-0425 Moderation | ❌ |

## Already beyond the suite

XEP-0444 Reactions, OMEMO (axolotl) 1:1, XEP-0158 CAPTCHA forms.

## Catch-up plan (waves, in order)

1. **Wave A — required client-level, small (✅ done):** XEP-0245 /me; XEP-0030
   answering disco#info; XEP-0115 caps (ver hash); XEP-0199 pong; XEP-0249
   direct invites (send + receive); XEP-0066 OOB on HTTP uploads.
2. **Wave B — advanced, medium (✅ done):** XEP-0308 last-message correction (UI: ↑ edit,
   "(edited)" mark); XEP-0191 blocking (Buddy → Block); XEP-0084 avatars with
   0153 fallback; XEP-0410 MUC self-ping keepalive;
   XEP-0393 styling (*bold* _italic_ ~strike~ `mono`).
3. **Wave C — advanced, big (✅ done):** XEP-0198 stream management (acks + resume);
   MUC MAM; XEP-0234 + XEP-0261 Jingle file transfer (IBB transport);
   XEP-0368 direct TLS.
