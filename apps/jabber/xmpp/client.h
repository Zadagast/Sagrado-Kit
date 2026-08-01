// Minimal XMPP client: register (XEP-0077 + CAPTCHA), roster, chat, MUC, HTTP Upload.
#pragma once
#include "http_win.h"
#include "image_dec.h"
#include "socket_tls.h"

#include <mbedtls/sha1.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace jabber {

enum class ConnState { Disconnected, Connecting, Registering, Online, Error };

enum class Show {
    Chat,    // available
    Away,
    Xa,
    Dnd,
    Unavailable
};

struct Buddy {
    std::string jid;
    std::string name;
    std::string group;
    Show show = Show::Unavailable;
    std::string status;
    bool subscription_to = false;
    SkinImage avatar;
    bool vcard_fetched = false;
};

// Aggregated reaction mark for paint (ASCII alias in UI; emoji on the wire).
struct ReactionMark {
    std::string emoji;
    int count = 0;
    bool mine = false;
};

struct ChatLine {
    std::string from;
    std::string body;
    bool mine = false;
    bool file = false;
    bool system = false; // subject / join notices
    std::string id;      // message @id (XEP-0184)
    bool delivered = false;
    std::string react_id; // XEP-0444 target: 1:1 @id, MUC stanza-id
    std::vector<ReactionMark> reactions;
    time_t when = 0; // wall clock; 0 = unknown (omit in UI)
};

struct MucRoomInfo {
    std::string jid;
    std::string name;
    std::string description;
    int occupants = -1; // -1 unknown
};

struct MucBookmark {
    std::string jid;
    std::string name;
    std::string nick;
    bool autojoin = false;
};

struct MucInvite {
    std::string room;
    std::string from; // inviter bare JID
    std::string reason;
};

// MUC occupant row — real_jid only when muc#user item/@jid is disclosed.
struct MucOccupant {
    std::string nick;
    std::string real_jid; // bare; empty in anonymous rooms
};

struct UploadSlot {
    std::string put_url;
    std::string get_url;
};

struct ClientEvent {
    enum Type {
        State,
        StatusText,
        Roster,
        Presence,
        Message,
        RegisterOk,
        RegisterFail,
        CaptchaReady,
        FileProgress,
        MucOccupants,
        MucRooms,
        MucSubject,
        Bookmarks,
        Identity,
        SubscribeAsk, // inbound presence type=subscribe
        ChatState,    // XEP-0085; text = composing|paused|active|inactive|gone
        Receipt,      // XEP-0184 delivered; jid = peer bare
        History,      // XEP-0313 MAM batch done; jid = peer bare
        MucInviteAsk, // inbound muc#user; jid = room, text = inviter
        Reaction      // XEP-0444; jid = chat bare, text = react_id
    } type = State;
    std::string text;
    std::string jid;
    SkinImage captcha;
    int progress = 0;
};

inline std::string xml_escape(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&apos;"; break;
        default: o += c;
        }
    }
    return o;
}

inline std::string xml_unescape(std::string s) {
    auto repl = [&](const char *a, char b) {
        size_t p = 0;
        std::string from = a;
        while ((p = s.find(from, p)) != std::string::npos) {
            s.replace(p, from.size(), 1, b);
            p += 1;
        }
    };
    repl("&lt;", '<');
    repl("&gt;", '>');
    repl("&quot;", '"');
    repl("&apos;", '\'');
    repl("&amp;", '&');
    return s;
}

inline std::string bare_jid(const std::string &j) {
    size_t s = j.find('/');
    return s == std::string::npos ? j : j.substr(0, s);
}

inline std::string jid_node(const std::string &j) {
    size_t a = j.find('@');
    return a == std::string::npos ? j : j.substr(0, a);
}

inline std::string jid_domain(const std::string &j) {
    size_t a = j.find('@');
    if (a == std::string::npos) return j;
    size_t s = j.find('/', a);
    return s == std::string::npos ? j.substr(a + 1) : j.substr(a + 1, s - a - 1);
}

inline std::string jid_resource(const std::string &j) {
    size_t s = j.find('/');
    return s == std::string::npos ? std::string{} : j.substr(s + 1);
}

inline bool jid_ieq(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

inline std::string sha1_hex(const uint8_t *data, size_t len) {
    unsigned char dig[20];
    if (mbedtls_sha1(data, len, dig) != 0) return {};
    char hex[41];
    for (int i = 0; i < 20; ++i)
        std::snprintf(hex + i * 2, 3, "%02x", dig[i]);
    return std::string(hex, 40);
}

inline std::string sha1_hex(const std::vector<uint8_t> &data) {
    return sha1_hex(data.data(), data.size());
}

// Raw SHA-1 digest (20 bytes). Used by XEP-0392 angle generation.
inline bool sha1_digest(const uint8_t *data, size_t len, unsigned char out[20]) {
    return mbedtls_sha1(data, len, out) == 0;
}

// Remove <PHOTO>…</PHOTO> / self-closing PHOTO from a vCard fragment.
inline std::string vcard_strip_photo(std::string vcard) {
    for (;;) {
        size_t a = vcard.find("<PHOTO");
        if (a == std::string::npos) a = vcard.find("<photo");
        if (a == std::string::npos) break;
        size_t gt = vcard.find('>', a);
        if (gt == std::string::npos) break;
        if (gt > a && vcard[gt - 1] == '/') {
            vcard.erase(a, gt + 1 - a);
            continue;
        }
        size_t b = vcard.find("</PHOTO>", gt);
        size_t b2 = vcard.find("</photo>", gt);
        if (b == std::string::npos || (b2 != std::string::npos && b2 < b)) b = b2;
        if (b == std::string::npos) break;
        vcard.erase(a, b + 8 - a);
    }
    return vcard;
}

inline ClientEvent make_event(ClientEvent::Type t, const std::string &text = {},
                              const std::string &jid = {}, int progress = 0) {
    ClientEvent e;
    e.type = t;
    e.text = text;
    e.jid = jid;
    e.progress = progress;
    return e;
}

inline std::string b64(const std::string &in) {
    static const char *t =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    int val = 0, valb = -6;
    for (uint8_t c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            o.push_back(t[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) o.push_back(t[((val << 8) >> (valb + 8)) & 0x3F]);
    while (o.size() % 4) o.push_back('=');
    return o;
}

inline bool b64_decode(const std::string &in, std::vector<uint8_t> *out) {
    if (!out) return false;
    out->clear();
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1};
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int d = T[c];
        if (d < 0) continue;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out->push_back(uint8_t((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return !out->empty();
}

inline bool extract_tag(const std::string &xml, const std::string &name,
                        std::string *inner) {
    std::string open = "<" + name;
    size_t a = xml.find(open);
    if (a == std::string::npos) return false;
    size_t gt = xml.find('>', a);
    if (gt == std::string::npos) return false;
    if (xml[gt - 1] == '/') {
        if (inner) *inner = "";
        return true;
    }
    std::string close = "</" + name + ">";
    size_t b = xml.find(close, gt + 1);
    if (b == std::string::npos) return false;
    if (inner) *inner = xml.substr(gt + 1, b - gt - 1);
    return true;
}

inline std::string attr(const std::string &tag, const std::string &key) {
    std::string k = key + "='";
    size_t a = tag.find(k);
    char q = '\'';
    if (a == std::string::npos) {
        k = key + "=\"";
        a = tag.find(k);
        q = '"';
    }
    if (a == std::string::npos) return {};
    a += k.size();
    size_t b = tag.find(q, a);
    if (b == std::string::npos) return {};
    return tag.substr(a, b - a);
}

// Parse XEP-0082 / delay stamp → time_t (UTC). Empty / bad → 0.
inline time_t parse_iso8601_stamp(const std::string &stamp) {
    if (stamp.size() < 19) return 0;
    int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
    if (std::sscanf(stamp.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) < 6)
        return 0;
    std::tm t{};
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    t.tm_sec = s;
#ifdef _WIN32
    return _mkgmtime(&t);
#else
    return timegm(&t);
#endif
}

// XEP-0203 / jabber:x:delay stamp from a stanza (first match).
inline time_t delay_stamp_from_stanza(const std::string &st) {
    auto find_stamp = [&](const char *marker) -> time_t {
        size_t p = 0;
        while ((p = st.find(marker, p)) != std::string::npos) {
            size_t tag_start = st.rfind('<', p);
            size_t gt = st.find('>', p);
            if (tag_start == std::string::npos || gt == std::string::npos) {
                p += 1;
                continue;
            }
            std::string tag = st.substr(tag_start, gt - tag_start + 1);
            std::string stamp = attr(tag, "stamp");
            if (!stamp.empty()) {
                time_t tt = parse_iso8601_stamp(stamp);
                if (tt) return tt;
            }
            p = gt + 1;
        }
        return 0;
    };
    time_t t = find_stamp("urn:xmpp:delay");
    if (t) return t;
    return find_stamp("jabber:x:delay");
}

// muc#user <item jid='user@host'/> when the room discloses real JIDs.
inline std::string muc_item_real_jid(const std::string &st) {
    if (st.find("muc#user") == std::string::npos) return {};
    size_t ip = st.find("<item");
    while (ip != std::string::npos) {
        size_t gt = st.find('>', ip);
        if (gt == std::string::npos) break;
        std::string tag = st.substr(ip, gt - ip + 1);
        std::string j = attr(tag, "jid");
        if (!j.empty()) return bare_jid(j);
        ip = st.find("<item", gt + 1);
    }
    return {};
}

// XEP-0359 stanza-id assigned by `by` (bare JID).
inline std::string stanza_id_by(const std::string &st, const std::string &by) {
    if (by.empty()) return {};
    size_t p = 0;
    while ((p = st.find("<stanza-id", p)) != std::string::npos) {
        size_t gt = st.find('>', p);
        if (gt == std::string::npos) break;
        std::string tag = st.substr(p, gt - p + 1);
        if (tag.find("urn:xmpp:sid:0") != std::string::npos ||
            st.find("urn:xmpp:sid:0") != std::string::npos) {
            if (jid_ieq(bare_jid(attr(tag, "by")), bare_jid(by))) {
                std::string id = attr(tag, "id");
                if (!id.empty()) return id;
            }
        }
        p = gt + 1;
    }
    return {};
}

// True when `p` is `<reaction` / `<reaction>` / `<reaction …`, not `<reactions`.
inline bool is_reaction_open_tag(const std::string &st, size_t p) {
    if (st.compare(p, 9, "<reaction") != 0) return false;
    if (p + 9 >= st.size()) return false;
    char c = st[p + 9];
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// `</reaction>` but not the `</reaction` prefix of `</reactions>`.
inline size_t find_reaction_close_tag(const std::string &st, size_t from) {
    size_t p = from;
    while ((p = st.find("</reaction>", p)) != std::string::npos) {
        size_t after = p + 11;
        if (after >= st.size() || st[after] != 's') return p;
        p = after;
    }
    return std::string::npos;
}

// Parse <reaction>…</reaction> children inside a reactions payload.
// Must not treat <reactions> / </reactions> as a reaction element (prefix trap).
inline std::vector<std::string> parse_reaction_emojis(const std::string &st) {
    std::vector<std::string> out;
    size_t p = 0;
    while ((p = st.find("<reaction", p)) != std::string::npos) {
        if (!is_reaction_open_tag(st, p)) {
            p += 9;
            continue;
        }
        size_t gt = st.find('>', p);
        if (gt == std::string::npos) break;
        if (gt > 0 && st[gt - 1] == '/') {
            p = gt + 1;
            continue;
        }
        size_t end = find_reaction_close_tag(st, gt);
        if (end == std::string::npos) break;
        std::string emoji = xml_unescape(st.substr(gt + 1, end - gt - 1));
        while (!emoji.empty() &&
               (emoji.front() == ' ' || emoji.front() == '\n' || emoji.front() == '\r'))
            emoji.erase(emoji.begin());
        while (!emoji.empty() &&
               (emoji.back() == ' ' || emoji.back() == '\n' || emoji.back() == '\r'))
            emoji.pop_back();
        // Ignore accidental nested markup (bad parse residue).
        if (!emoji.empty() && emoji.find('<') == std::string::npos) {
            bool dup = false;
            for (const auto &e : out)
                if (e == emoji) {
                    dup = true;
                    break;
                }
            if (!dup) out.push_back(emoji);
        }
        p = end + 11;
    }
    return out;
}

// Kit faces are Latin-1 — map wire emoji to short AIM-shaped labels for paint.
inline const char *reaction_label(const std::string &emoji) {
    if (emoji == u8"👍" || emoji == "+1") return "+1";
    if (emoji == u8"❤️" || emoji == u8"❤" || emoji == "<3") return "<3";
    if (emoji == u8"😂" || emoji == "haha") return "haha";
    if (emoji == u8"😮" || emoji == "wow") return "wow";
    if (emoji == u8"😢" || emoji == "sad") return "sad";
    if (emoji == u8"🎉" || emoji == "yay") return "yay";
    return "*";
}

inline std::string reaction_wire(const std::string &label_or_emoji) {
    if (label_or_emoji == "+1" || label_or_emoji == u8"👍") return u8"👍";
    if (label_or_emoji == "<3" || label_or_emoji == u8"❤️" || label_or_emoji == u8"❤")
        return u8"❤️";
    if (label_or_emoji == "haha" || label_or_emoji == u8"😂") return u8"😂";
    if (label_or_emoji == "wow" || label_or_emoji == u8"😮") return u8"😮";
    if (label_or_emoji == "sad" || label_or_emoji == u8"😢") return u8"😢";
    if (label_or_emoji == "yay" || label_or_emoji == u8"🎉") return u8"🎉";
    return label_or_emoji;
}

inline std::string first_element(const std::string &xml, const std::string &name) {
    std::string open = "<" + name;
    size_t a = xml.find(open);
    if (a == std::string::npos) return {};
    size_t gt = xml.find('>', a);
    if (gt == std::string::npos) return {};
    if (xml[gt - 1] == '/') return xml.substr(a, gt - a + 1);
    std::string close = "</" + name + ">";
    size_t b = xml.find(close, gt);
    if (b == std::string::npos) return {};
    return xml.substr(a, b + close.size() - a);
}

class Client {
public:
    std::mutex mu;
    ConnState state = ConnState::Disconnected;
    std::string status_text = "Signed off";
    std::string jid;
    std::string resource = "SagradoJabber";
    std::map<std::string, Buddy> roster;
    std::vector<std::string> pending_subscribe; // bare JIDs waiting for Accept/Deny
    std::map<std::string, std::string> chat_states; // bare → composing|paused|…
    std::map<std::string, std::vector<ChatLine>> chats;
    // XEP-0444: chat bare → react_id → from_key → emoji list (full replace per sender).
    std::map<std::string,
             std::map<std::string, std::map<std::string, std::vector<std::string>>>>
        reaction_sets;
    std::map<std::string, std::vector<MucOccupant>> muc_occupants;
    std::map<std::string, std::string> muc_nicks;     // room → our nick
    std::map<std::string, std::string> muc_subjects;  // room → subject
    std::set<std::string> muc_joined;                 // rooms we have joined
    // vCard PHOTO cache for bare JIDs (roster + MUC real JIDs).
    std::map<std::string, SkinImage> vcard_avatars;
    std::string conference_host;                      // e.g. conference.yax.im
    std::vector<MucRoomInfo> muc_rooms;               // public disco list
    std::vector<MucBookmark> muc_bookmarks;
    std::vector<MucInvite> pending_muc_invites;
    SkinImage captcha_image;
    std::string captcha_sid;
    std::string captcha_form_type;
    std::vector<std::pair<std::string, std::string>> register_fields; // var, label
    std::string last_error;
    std::string http_upload_host;
    bool upload_available = false;
    bool mam_available = false; // urn:xmpp:mam:2 advertised

    // Own identity (Yahoo-shaped strip): presence + status + vCard.
    Show own_show = Show::Unavailable;
    std::string own_status;
    std::string own_nick;
    SkinImage own_avatar;
    static constexpr size_t kMaxPhotoBytes = 96 * 1024;

    using EventFn = std::function<void(const ClientEvent &)>;
    EventFn on_event;

    ~Client() { disconnect(); }

    void disconnect() {
        stop_ = true;
        captcha_ready_cv_.notify_all();
        if (thread_.joinable()) thread_.join();
        sock_.close();
        stop_ = false;
        {
            std::lock_guard<std::mutex> lock(mu);
            own_show = Show::Unavailable;
            muc_joined.clear();
            muc_nicks.clear();
            muc_occupants.clear();
            muc_subjects.clear();
            muc_rooms.clear();
            conference_host.clear();
            pending_subscribe.clear();
            pending_muc_invites.clear();
            chat_states.clear();
            reaction_sets.clear();
            vcard_avatars.clear();
            vcard_attempted_.clear();
            mam_available = false;
            mam_initial_done_.clear();
            mam_complete_.clear();
            mam_oldest_.clear();
            mam_pending_.clear();
            mam_inflight_.clear();
            own_vcard_xml_.clear();
            own_photo_hash_.clear();
            pending_photo_bytes_.clear();
            pending_photo_mime_.clear();
            vcard_inflight_.clear();
            while (!vcard_queue_.empty()) vcard_queue_.pop();
        }
        set_state(ConnState::Disconnected, "Signed off");
        emit(make_event(ClientEvent::Identity));
    }

    void sign_on(const std::string &full_jid, const std::string &password) {
        disconnect();
        mode_ = Mode::Auth;
        jid = bare_jid(full_jid);
        password_ = password;
        host_ = jid_domain(jid);
        user_ = jid_node(jid);
        stop_ = false;
        thread_ = std::thread([this] { run(); });
    }

    void begin_register(const std::string &host, const std::string &user,
                        const std::string &password) {
        disconnect();
        mode_ = Mode::Register;
        host_ = host;
        user_ = user;
        password_ = password;
        jid = user + "@" + host;
        stop_ = false;
        thread_ = std::thread([this] { run(); });
    }

    void submit_register_captcha(const std::string &answer) {
        std::lock_guard<std::mutex> lock(mu);
        captcha_answer_ = answer;
        captcha_submitted_ = true;
        captcha_ready_cv_.notify_all();
    }

    void cancel_register_captcha() {
        std::lock_guard<std::mutex> lock(mu);
        captcha_answer_.clear();
        captcha_submitted_ = true;
        captcha_ready_cv_.notify_all();
    }

    void send_chat_state(const std::string &to, const char *state) {
        if (!to.empty() && state && *state)
            queue_send("<message to='" + xml_escape(to) +
                       "' type='chat'><" + std::string(state) +
                       " xmlns='http://jabber.org/protocol/chatstates'/></message>");
    }

    // XEP-0313 — cold open: last `max` messages with this bare JID (1:1).
    // Results are buffered then merged on IQ fin (dedupe with any live lines).
    void request_mam_history(const std::string &with_bare, int max = 40) {
        std::string with = bare_jid(with_bare);
        if (with.empty() || state != ConnState::Online) return;
        if (max < 1) max = 1;
        if (max > 100) max = 100;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!mam_available) return;
            if (mam_initial_done_.count(with) || mam_inflight_.count(with)) return;
            mam_initial_done_.insert(with);
            mam_inflight_.insert(with);
        }
        send_mam_query(with, max, /*older=*/false, {});
    }

    // XEP-0313 — page older than the current window (RSM before=oldest archive id).
    void request_mam_older(const std::string &with_bare, int max = 40) {
        std::string with = bare_jid(with_bare);
        if (with.empty() || state != ConnState::Online) return;
        if (max < 1) max = 1;
        if (max > 100) max = 100;
        std::string before;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!mam_available) return;
            if (mam_complete_.count(with) && mam_complete_[with]) return;
            if (mam_inflight_.count(with)) return;
            auto it = mam_oldest_.find(with);
            if (it == mam_oldest_.end() || it->second.empty()) return;
            before = it->second;
            mam_inflight_.insert(with);
        }
        send_mam_query(with, max, /*older=*/true, before);
    }

    bool mam_can_load_older(const std::string &with_bare) {
        std::string with = bare_jid(with_bare);
        std::lock_guard<std::mutex> lock(mu);
        if (!mam_available || with.empty()) return false;
        if (mam_inflight_.count(with)) return false;
        if (mam_complete_.count(with) && mam_complete_[with]) return false;
        auto it = mam_oldest_.find(with);
        return it != mam_oldest_.end() && !it->second.empty();
    }

    bool mam_loading(const std::string &with_bare) {
        std::string with = bare_jid(with_bare);
        std::lock_guard<std::mutex> lock(mu);
        return mam_inflight_.count(with) != 0;
    }

    void send_message(const std::string &to, const std::string &body) {
        std::string mid = "m" + std::to_string(msg_seq_++);
        std::string stanza =
            "<message to='" + xml_escape(to) + "' type='chat' id='" + mid +
            "'><body>" + xml_escape(body) +
            "</body><request xmlns='urn:xmpp:receipts'/>"
            "<active xmlns='http://jabber.org/protocol/chatstates'/>"
            "</message>";
        {
            std::lock_guard<std::mutex> lock(mu);
            ChatLine ln;
            ln.from = jid;
            ln.body = body;
            ln.mine = true;
            ln.id = mid;
            ln.react_id = mid;
            ln.when = std::time(nullptr);
            chats[bare_jid(to)].push_back(std::move(ln));
        }
        queue_send(stanza);
    }

    void send_muc_message(const std::string &room, const std::string &body) {
        std::string mid = "m" + std::to_string(msg_seq_++);
        std::string stanza =
            "<message to='" + xml_escape(room) + "' type='groupchat' id='" + mid +
            "'><body>" + xml_escape(body) + "</body></message>";
        {
            std::lock_guard<std::mutex> lock(mu);
            ChatLine ln;
            ln.from = jid;
            ln.body = body;
            ln.mine = true;
            ln.id = mid;
            ln.when = std::time(nullptr);
            // react_id filled from room stanza-id when our echo arrives.
            chats[bare_jid(room)].push_back(std::move(ln));
        }
        queue_send(stanza);
    }

    // XEP-0444 — add/toggle one emoji in our reaction set (multi-react).
    // Empty emoji clears our reactions. muc=true → type=groupchat.
    void send_reaction(const std::string &to_in, const std::string &react_id,
                       const std::string &emoji_in, bool muc) {
        std::string to = bare_jid(to_in);
        if (to.empty() || react_id.empty()) return;
        std::string emoji = reaction_wire(emoji_in);
        std::string from_key = muc ? [&] {
            std::lock_guard<std::mutex> lock(mu);
            auto it = muc_nicks.find(to);
            return it != muc_nicks.end() ? it->second : jid_node(jid);
        }()
                                         : bare_jid(jid);
        std::vector<std::string> mine;
        {
            std::lock_guard<std::mutex> lock(mu);
            auto &cur = reaction_sets[to][react_id][from_key];
            if (emoji.empty()) {
                mine.clear();
            } else {
                mine = cur;
                auto it = std::find(mine.begin(), mine.end(), emoji);
                if (it != mine.end())
                    mine.erase(it); // toggle off that emoji only
                else
                    mine.push_back(emoji);
            }
            apply_reaction_locked(to, react_id, from_key, mine);
        }
        std::string mid = "r" + std::to_string(msg_seq_++);
        std::string type = muc ? "groupchat" : "chat";
        std::string stanza = "<message to='" + xml_escape(to) + "' type='" + type +
                             "' id='" + mid +
                             "'><reactions xmlns='urn:xmpp:reactions:0' id='" +
                             xml_escape(react_id) + "'>";
        for (const auto &e : mine)
            stanza += "<reaction>" + xml_escape(e) + "</reaction>";
        stanza += "</reactions><store xmlns='urn:xmpp:hints'/></message>";
        queue_send(stanza);
        emit(make_event(ClientEvent::Reaction, react_id, to));
    }

    void join_muc(const std::string &room_in, const std::string &nick_in,
                  const std::string &password = {}) {
        std::string room = bare_jid(room_in);
        std::string nick = nick_in.empty() ? jid_node(jid) : nick_in;
        if (room.empty() || nick.empty()) return;
        std::string to = room + "/" + nick;
        {
            std::lock_guard<std::mutex> lock(mu);
            muc_nicks[room] = nick;
            muc_joined.insert(room);
            if (!chats.count(room)) chats[room] = {};
            muc_occupants[room]; // ensure key
            // Drop matching pending invites once we join.
            pending_muc_invites.erase(
                std::remove_if(pending_muc_invites.begin(),
                               pending_muc_invites.end(),
                               [&](const MucInvite &iv) {
                                   return jid_ieq(iv.room, room);
                               }),
                pending_muc_invites.end());
        }
        std::string x = "<x xmlns='http://jabber.org/protocol/muc'/>";
        if (!password.empty()) {
            x = "<x xmlns='http://jabber.org/protocol/muc'><password>" +
                xml_escape(password) + "</password></x>";
        }
        queue_send("<presence to='" + xml_escape(to) + "'>" + x + "</presence>");
    }

    void set_muc_subject(const std::string &room_in, const std::string &subject) {
        std::string room = bare_jid(room_in);
        if (room.empty()) return;
        queue_send("<message to='" + xml_escape(room) +
                   "' type='groupchat'><subject>" + xml_escape(subject) +
                   "</subject></message>");
        {
            std::lock_guard<std::mutex> lock(mu);
            muc_subjects[room] = subject;
            ChatLine ln;
            ln.body = "Topic: " + subject;
            ln.system = true;
            ln.when = std::time(nullptr);
            chats[room].push_back(std::move(ln));
        }
        emit(make_event(ClientEvent::MucSubject, subject, room));
    }

    void invite_muc(const std::string &room_in, const std::string &buddy_in,
                    const std::string &reason = {}) {
        std::string room = bare_jid(room_in);
        std::string buddy = bare_jid(buddy_in);
        if (room.empty() || buddy.empty()) return;
        std::string inv =
            "<message to='" + xml_escape(room) + "'>"
            "<x xmlns='http://jabber.org/protocol/muc#user'>"
            "<invite to='" + xml_escape(buddy) + "'>";
        if (!reason.empty())
            inv += "<reason>" + xml_escape(reason) + "</reason>";
        inv += "</invite></x></message>";
        queue_send(inv);
        emit(make_event(ClientEvent::StatusText,
                        "Invited " + jid_node(buddy) + " to " + jid_node(room)));
    }

    void decline_muc_invite(const std::string &room_in) {
        std::string room = bare_jid(room_in);
        std::lock_guard<std::mutex> lock(mu);
        pending_muc_invites.erase(
            std::remove_if(pending_muc_invites.begin(), pending_muc_invites.end(),
                           [&](const MucInvite &iv) { return jid_ieq(iv.room, room); }),
            pending_muc_invites.end());
    }

    void leave_muc(const std::string &room_in) {
        std::string room = bare_jid(room_in);
        std::string nick;
        {
            std::lock_guard<std::mutex> lock(mu);
            auto it = muc_nicks.find(room);
            nick = it != muc_nicks.end() ? it->second : jid_node(jid);
            muc_joined.erase(room);
            muc_nicks.erase(room);
            muc_occupants.erase(room);
        }
        if (!room.empty() && !nick.empty())
            queue_send("<presence to='" + xml_escape(room + "/" + nick) +
                       "' type='unavailable'/>");
        emit(make_event(ClientEvent::MucOccupants, {}, room));
    }

    void refresh_muc_rooms() {
        std::string conf;
        {
            std::lock_guard<std::mutex> lock(mu);
            conf = conference_host;
        }
        if (conf.empty()) {
            emit(make_event(ClientEvent::StatusText,
                            "No chat service found on this server yet"));
            return;
        }
        queue_send("<iq type='get' id='mucrooms1' to='" + xml_escape(conf) +
                   "'><query xmlns='http://jabber.org/protocol/disco#items'/></iq>");
        emit(make_event(ClientEvent::StatusText, "Fetching chat rooms…"));
    }

    void request_bookmarks() {
        // Prefer PEP Native Bookmarks (XEP-0402); Private XML fallback follows if empty.
        queue_send(
            "<iq type='get' id='bmpep1'><pubsub xmlns='http://jabber.org/protocol/pubsub'>"
            "<items node='urn:xmpp:bookmarks:1'/></pubsub></iq>");
        queue_send(
            "<iq type='get' id='bmpriv1'><query xmlns='jabber:iq:private'>"
            "<storage xmlns='storage:bookmarks'/></query></iq>");
    }

    void bookmark_muc(const std::string &room_in, const std::string &name,
                      const std::string &nick, bool autojoin) {
        std::string room = bare_jid(room_in);
        if (room.empty()) return;
        MucBookmark bm;
        bm.jid = room;
        bm.name = name.empty() ? jid_node(room) : name;
        bm.nick = nick.empty() ? jid_node(jid) : nick;
        bm.autojoin = autojoin;
        {
            std::lock_guard<std::mutex> lock(mu);
            bool found = false;
            for (auto &b : muc_bookmarks) {
                if (jid_ieq(b.jid, room)) {
                    b = bm;
                    found = true;
                    break;
                }
            }
            if (!found) muc_bookmarks.push_back(bm);
        }
        publish_bookmarks();
        emit(make_event(ClientEvent::Bookmarks));
    }

    void set_bookmark_autojoin(const std::string &room_in, bool autojoin) {
        std::string room = bare_jid(room_in);
        std::string nick = jid_node(jid);
        std::string name = jid_node(room);
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(mu);
            auto nit = muc_nicks.find(room);
            if (nit != muc_nicks.end()) nick = nit->second;
            for (auto &b : muc_bookmarks) {
                if (jid_ieq(b.jid, room)) {
                    b.autojoin = autojoin;
                    if (!b.nick.empty()) nick = b.nick;
                    if (!b.name.empty()) name = b.name;
                    found = true;
                    break;
                }
            }
        }
        if (found) {
            publish_bookmarks();
            emit(make_event(ClientEvent::Bookmarks));
            return;
        }
        bookmark_muc(room, name, nick, autojoin);
    }

    bool is_bookmarked(const std::string &room_in) {
        std::string room = bare_jid(room_in);
        std::lock_guard<std::mutex> lock(mu);
        for (const auto &b : muc_bookmarks)
            if (jid_ieq(b.jid, room)) return true;
        return false;
    }

    bool bookmark_autojoin(const std::string &room_in) {
        std::string room = bare_jid(room_in);
        std::lock_guard<std::mutex> lock(mu);
        for (const auto &b : muc_bookmarks)
            if (jid_ieq(b.jid, room)) return b.autojoin;
        return false;
    }

    void set_show(Show s) {
        {
            std::lock_guard<std::mutex> lock(mu);
            own_show = s;
            show_ = s;
        }
        publish_presence();
        emit(make_event(ClientEvent::Identity));
    }

    void set_status_message(const std::string &status) {
        {
            std::lock_guard<std::mutex> lock(mu);
            own_status = status;
        }
        publish_presence();
        emit(make_event(ClientEvent::Identity, status));
    }

    void request_vcard(const std::string &bare = {}) {
        std::string to = bare_jid(bare.empty() ? jid : bare);
        if (to.empty()) return;
        bool self = bare.empty() || jid_ieq(to, bare_jid(jid));
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!self && vcard_attempted_.count(to)) return; // already fetched
            if (vcard_inflight_.count(to)) return;
            if ((int)vcard_inflight_.size() >= 4) {
                vcard_queue_.push(to);
                return;
            }
            vcard_inflight_.insert(to);
        }
        std::string id = "vc" + std::to_string(iq_seq_++);
        std::string iq = "<iq type='get' id='" + id + "'";
        if (!self) iq += " to='" + xml_escape(to) + "'";
        iq += "><vCard xmlns='vcard-temp'/></iq>";
        queue_send(iq);
    }

    // Bare real JID for a room nick (empty if anonymous / unknown). Caller holds mu.
    std::string muc_real_jid_locked(const std::string &room, const std::string &nick) const {
        auto it = muc_occupants.find(bare_jid(room));
        if (it == muc_occupants.end()) return {};
        for (const auto &o : it->second)
            if (o.nick == nick) return o.real_jid;
        return {};
    }

    // Best photo for a bare JID. Caller holds mu. Pointers valid until unlock.
    const SkinImage *avatar_for_bare_locked(const std::string &bare_in) const {
        std::string bare = bare_jid(bare_in);
        if (bare.empty()) return nullptr;
        if (jid_ieq(bare, bare_jid(jid)) && !own_avatar.empty()) return &own_avatar;
        auto rit = roster.find(bare);
        if (rit != roster.end() && !rit->second.avatar.empty()) return &rit->second.avatar;
        // Case-insensitive lookup in vcard_avatars (keys stored as bare_jid).
        auto vit = vcard_avatars.find(bare);
        if (vit != vcard_avatars.end() && !vit->second.empty()) return &vit->second;
        for (const auto &kv : vcard_avatars)
            if (jid_ieq(kv.first, bare) && !kv.second.empty()) return &kv.second;
        return nullptr;
    }

    // Publish own icon via XEP-0054 vCard PHOTO (AIM-shaped Set Picture).
    bool set_own_photo(const std::vector<uint8_t> &bytes, const std::string &mime_in) {
        if (bytes.empty()) {
            emit(make_event(ClientEvent::StatusText, "No image selected"));
            return false;
        }
        if (bytes.size() > kMaxPhotoBytes) {
            emit(make_event(ClientEvent::StatusText, "Picture too large (max 96 KB)"));
            return false;
        }
        SkinImage av;
        if (!decode_image_vec(bytes, av)) {
            emit(make_event(ClientEvent::StatusText, "Could not read that image"));
            return false;
        }
        std::string mime = mime_in.empty() ? "image/png" : mime_in;
        std::string hash = sha1_hex(bytes);
        std::string nick;
        std::string cached;
        {
            std::lock_guard<std::mutex> lock(mu);
            own_avatar = std::move(av);
            vcard_avatars[bare_jid(jid)] = own_avatar;
            own_photo_hash_ = hash;
            pending_photo_bytes_ = bytes;
            pending_photo_mime_ = mime;
            nick = own_nick.empty() ? jid_node(jid) : own_nick;
            cached = own_vcard_xml_;
        }
        emit(make_event(ClientEvent::Identity));
        emit(make_event(ClientEvent::StatusText, "Updating picture…"));

        std::string photo =
            "<PHOTO><TYPE>" + xml_escape(mime) + "</TYPE><BINVAL>" +
            b64(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size())) +
            "</BINVAL></PHOTO>";
        std::string vcard;
        if (!cached.empty()) {
            vcard = vcard_strip_photo(cached);
            size_t close = vcard.rfind("</vCard>");
            if (close == std::string::npos) close = vcard.rfind("</vcard>");
            if (close != std::string::npos)
                vcard.insert(close, photo);
            else
                vcard.clear();
        }
        if (vcard.empty()) {
            vcard = "<vCard xmlns='vcard-temp'><FN>" + xml_escape(nick) +
                    "</FN><NICKNAME>" + xml_escape(nick) + "</NICKNAME>" + photo +
                    "</vCard>";
        } else if (vcard.find("xmlns=") == std::string::npos) {
            // Ensure xmlns on root if stripped cache lacked it.
            size_t gt = vcard.find('>');
            if (gt != std::string::npos && vcard.find("vcard-temp") == std::string::npos)
                vcard.insert(gt, " xmlns='vcard-temp'");
        }
        {
            std::lock_guard<std::mutex> lock(mu);
            own_vcard_xml_ = vcard;
        }
        std::string id = "vcset" + std::to_string(iq_seq_++);
        queue_send("<iq type='set' id='" + id + "'>" + vcard + "</iq>");
        return true;
    }

    void add_buddy(const std::string &buddy) {
        std::string bare = bare_jid(buddy);
        if (bare.empty()) return;
        queue_send("<iq type='set' id='roster_add'><query xmlns='jabber:iq:roster'>"
                   "<item jid='" +
                   xml_escape(bare) + "'><group>Buddies</group></item></query></iq>");
        queue_send("<presence to='" + xml_escape(bare) + "' type='subscribe'/>");
    }

    // Accept inbound subscribe: allow them + ask back (AIM “authorize”).
    void authorize_buddy(const std::string &buddy) {
        std::string bare = bare_jid(buddy);
        if (bare.empty()) return;
        {
            std::lock_guard<std::mutex> lock(mu);
            pending_subscribe.erase(
                std::remove_if(pending_subscribe.begin(), pending_subscribe.end(),
                               [&](const std::string &j) { return jid_ieq(j, bare); }),
                pending_subscribe.end());
        }
        queue_send("<presence to='" + xml_escape(bare) + "' type='subscribed'/>");
        queue_send("<iq type='set' id='roster_auth'><query xmlns='jabber:iq:roster'>"
                   "<item jid='" +
                   xml_escape(bare) + "'><group>Buddies</group></item></query></iq>");
        queue_send("<presence to='" + xml_escape(bare) + "' type='subscribe'/>");
        emit(make_event(ClientEvent::StatusText,
                        jid_node(bare) + " can see you — asked them back"));
    }

    void deny_buddy(const std::string &buddy) {
        std::string bare = bare_jid(buddy);
        if (bare.empty()) return;
        {
            std::lock_guard<std::mutex> lock(mu);
            pending_subscribe.erase(
                std::remove_if(pending_subscribe.begin(), pending_subscribe.end(),
                               [&](const std::string &j) { return jid_ieq(j, bare); }),
                pending_subscribe.end());
        }
        queue_send("<presence to='" + xml_escape(bare) + "' type='unsubscribed'/>");
        emit(make_event(ClientEvent::StatusText,
                        "Denied " + jid_node(bare)));
    }

    void remove_buddy(const std::string &buddy) {
        std::string bare = bare_jid(buddy);
        if (bare.empty()) return;
        queue_send("<iq type='set' id='roster_rm'><query xmlns='jabber:iq:roster'>"
                   "<item jid='" +
                   xml_escape(bare) + "' subscription='remove'/></query></iq>");
        queue_send("<presence to='" + xml_escape(bare) + "' type='unsubscribe'/>");
        queue_send("<presence to='" + xml_escape(bare) + "' type='unsubscribed'/>");
        {
            std::lock_guard<std::mutex> lock(mu);
            roster.erase(bare);
            chat_states.erase(bare);
        }
        emit(make_event(ClientEvent::Roster));
        emit(make_event(ClientEvent::StatusText, "Removed " + jid_node(bare)));
    }

    bool send_file(const std::string &to, const std::string &path,
                   const std::string &filename, const std::vector<uint8_t> &data,
                   const std::string &mime) {
        if (!upload_available || http_upload_host.empty()) return false;
        std::string id = "upl1";
        std::string req =
            "<iq type='get' id='" + id + "' to='" + xml_escape(http_upload_host) +
            "'><request xmlns='urn:xmpp:http:upload:0' filename='" +
            xml_escape(filename) + "' size='" + std::to_string(data.size()) +
            "' content-type='" + xml_escape(mime) + "'/></iq>";
        pending_upload_to_ = to;
        pending_upload_data_ = data;
        pending_upload_mime_ = mime;
        pending_upload_name_ = filename;
        queue_send(req);
        (void)path;
        return true;
    }

private:
    enum class Mode { Auth, Register };
    Mode mode_ = Mode::Auth;
    TlsSocket sock_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::string host_, user_, password_;
    Show show_ = Show::Chat;
    std::mutex send_mu_;
    std::queue<std::string> send_q_;
    std::string captcha_answer_;
    bool captcha_submitted_ = false;
    std::condition_variable captcha_ready_cv_;
    std::string stream_buf_;
    int iq_seq_ = 1;
    int msg_seq_ = 1;
    struct MamPending {
        std::string with;
        bool older = false;
        std::vector<ChatLine> buf;
        std::string first_uid; // archive result ids (RSM)
        std::string last_uid;
    };
    std::set<std::string> mam_initial_done_;           // cold-open attempted
    std::map<std::string, bool> mam_complete_;         // with → fin complete
    std::map<std::string, std::string> mam_oldest_;    // with → RSM <first> (page up)
    std::map<std::string, MamPending> mam_pending_;    // query/iq id → batch
    std::set<std::string> mam_inflight_;               // with currently querying
    std::string pending_upload_to_, pending_upload_mime_, pending_upload_name_;
    std::vector<uint8_t> pending_upload_data_;
    std::set<std::string> vcard_inflight_;
    std::queue<std::string> vcard_queue_;
    std::set<std::string> vcard_attempted_; // bare JIDs we already IQ-got
    std::string own_vcard_xml_;   // last self vCard fragment for PHOTO merge
    std::string own_photo_hash_;  // SHA-1 hex of PHOTO BINVAL (XEP-0153)
    std::vector<uint8_t> pending_photo_bytes_;
    std::string pending_photo_mime_;

    void emit(const ClientEvent &e) {
        EventFn fn;
        {
            std::lock_guard<std::mutex> lock(mu);
            fn = on_event;
        }
        if (fn) fn(e);
    }

    void set_state(ConnState s, const std::string &text) {
        {
            std::lock_guard<std::mutex> lock(mu);
            state = s;
            status_text = text;
            if (s == ConnState::Error) last_error = text;
        }
        emit(make_event(ClientEvent::State, text));
        emit(make_event(ClientEvent::StatusText, text));
    }

    void queue_send(const std::string &s) {
        std::lock_guard<std::mutex> lock(send_mu_);
        send_q_.push(s);
    }

    void publish_presence() {
        Show s;
        std::string st;
        std::string photo_hash;
        {
            std::lock_guard<std::mutex> lock(mu);
            s = own_show;
            st = own_status;
            photo_hash = own_photo_hash_;
            show_ = s;
        }
        std::string stanza = "<presence";
        if (s == Show::Unavailable) {
            stanza += " type='unavailable'/>";
        } else {
            stanza += ">";
            const char *sh = "chat";
            if (s == Show::Away) sh = "away";
            else if (s == Show::Xa) sh = "xa";
            else if (s == Show::Dnd) sh = "dnd";
            else if (s == Show::Chat) sh = "chat";
            stanza += std::string("<show>") + sh + "</show>";
            if (!st.empty())
                stanza += "<status>" + xml_escape(st) + "</status>";
            // XEP-0153 — tell buddies the vCard photo changed.
            stanza += "<x xmlns='vcard-temp:x:update'>";
            if (!photo_hash.empty())
                stanza += "<photo>" + photo_hash + "</photo>";
            else
                stanza += "<photo/>";
            stanza += "</x>";
            stanza += "</presence>";
        }
        queue_send(stanza);
    }

    void pump_vcard_queue() {
        std::string next;
        {
            std::lock_guard<std::mutex> lock(mu);
            while (!vcard_queue_.empty() && (int)vcard_inflight_.size() < 4) {
                next = vcard_queue_.front();
                vcard_queue_.pop();
                if (vcard_inflight_.count(next)) {
                    next.clear();
                    continue;
                }
                vcard_inflight_.insert(next);
                break;
            }
        }
        if (next.empty()) return;
        std::string id = "vc" + std::to_string(iq_seq_++);
        std::string iq = "<iq type='get' id='" + id + "' to='" + xml_escape(next) +
                         "'><vCard xmlns='vcard-temp'/></iq>";
        queue_send(iq);
    }

    void apply_vcard(const std::string &bare, const std::string &st) {
        std::string nick, fn, binval;
        extract_tag(st, "NICKNAME", &nick);
        if (nick.empty()) extract_tag(st, "nickname", &nick);
        extract_tag(st, "FN", &fn);
        if (fn.empty()) extract_tag(st, "fn", &fn);
        std::string photo;
        if (extract_tag(st, "PHOTO", &photo) || extract_tag(st, "photo", &photo)) {
            if (!extract_tag(photo, "BINVAL", &binval))
                extract_tag(photo, "binval", &binval);
            if (binval.empty()) extract_tag(st, "BINVAL", &binval);
        }
        nick = xml_unescape(nick);
        fn = xml_unescape(fn);
        SkinImage avatar;
        std::string photo_hash;
        if (!binval.empty()) {
            std::vector<uint8_t> bytes;
            // Strip whitespace from base64
            std::string cleaned;
            cleaned.reserve(binval.size());
            for (char c : binval)
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t') cleaned.push_back(c);
            if (b64_decode(cleaned, &bytes)) {
                decode_image_vec(bytes, avatar);
                photo_hash = sha1_hex(bytes);
            }
        }
        bool self = bare.empty() || jid_ieq(bare_jid(bare), bare_jid(jid));
        {
            std::lock_guard<std::mutex> lock(mu);
            std::string key = self ? bare_jid(jid) : bare_jid(bare);
            vcard_inflight_.erase(key);
            if (self) vcard_inflight_.erase(jid);
            vcard_attempted_.insert(key);
            if (!avatar.empty()) vcard_avatars[key] = avatar; // shared roster + MUC cache
            if (self) {
                size_t a = st.find("<vCard");
                if (a == std::string::npos) a = st.find("<vcard");
                size_t b = st.find("</vCard>");
                if (b == std::string::npos) b = st.find("</vcard>");
                if (a != std::string::npos && b != std::string::npos)
                    own_vcard_xml_ = st.substr(a, b + 8 - a);
                if (!nick.empty()) own_nick = nick;
                else if (!fn.empty()) own_nick = fn;
                if (!avatar.empty()) own_avatar = std::move(avatar);
                if (!photo_hash.empty()) own_photo_hash_ = photo_hash;
            } else if (roster.count(key)) {
                if (!nick.empty()) roster[key].name = nick;
                else if (!fn.empty() &&
                         (roster[key].name.empty() || roster[key].name == jid_node(key)))
                    roster[key].name = fn;
                if (!avatar.empty()) roster[key].avatar = avatar;
                roster[key].vcard_fetched = true;
            }
        }
        emit(make_event(ClientEvent::Identity, {}, self ? bare_jid(jid) : bare_jid(bare)));
        if (!self) {
            emit(make_event(ClientEvent::Roster));
            emit(make_event(ClientEvent::MucOccupants)); // refresh room avatar tiles
        }
        pump_vcard_queue();
    }

    void flush_send() {
        for (;;) {
            std::string s;
            {
                std::lock_guard<std::mutex> lock(send_mu_);
                if (send_q_.empty()) break;
                s = send_q_.front();
                send_q_.pop();
            }
            sock_.send_all(s);
        }
    }

    bool open_stream(bool after_tls) {
        std::string s =
            "<?xml version='1.0'?><stream:stream to='" + xml_escape(host_) +
            "' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' "
            "version='1.0'>";
        if (!sock_.send_all(s)) return false;
        (void)after_tls;
        return read_until("stream:features");
    }

    // Handshake helper: accumulate bytes until `needle` appears.
    // Do NOT pump/dispatch stanzas here — register/bind IQs must stay in
    // stream_buf_ for the synchronous flows to parse (pump would eat them).
    bool read_until(const std::string &needle) {
        char buf[4096];
        for (int i = 0; i < 200; ++i) {
            if (stream_buf_.find(needle) != std::string::npos) return true;
            int n = sock_.recv_some(buf, sizeof(buf));
            if (n <= 0) return false;
            stream_buf_.append(buf, buf + n);
        }
        return stream_buf_.find(needle) != std::string::npos;
    }

    // Match the outer close when <message> nests (MAM/carbons forwarded).
    static size_t stanza_end_nested(const std::string &buf, const char *kind) {
        const std::string open = std::string("<") + kind;
        const std::string close = std::string("</") + kind + ">";
        size_t i = 0;
        int depth = 0;
        while (i < buf.size()) {
            if (buf.compare(i, open.size(), open) == 0) {
                char c = (i + open.size() < buf.size()) ? buf[i + open.size()] : 0;
                if (c == ' ' || c == '>' || c == '/' || c == '\t' || c == '\n' ||
                    c == '\r') {
                    size_t gt = buf.find('>', i);
                    if (gt == std::string::npos) return std::string::npos;
                    bool self = gt > i && buf[gt - 1] == '/';
                    if (!self) ++depth;
                    i = gt + 1;
                    continue;
                }
            }
            if (buf.compare(i, close.size(), close) == 0) {
                --depth;
                i += close.size();
                if (depth == 0) return i;
                continue;
            }
            ++i;
        }
        return std::string::npos;
    }

    void pump_incoming() {
        // Pull complete top-level stanzas from stream_buf_.
        for (;;) {
            size_t msg = stream_buf_.find("<message");
            size_t pres = stream_buf_.find("<presence");
            size_t iq = stream_buf_.find("<iq");
            size_t start = std::string::npos;
            const char *kind = nullptr;
            if (msg != std::string::npos) {
                start = msg;
                kind = "message";
            }
            if (pres != std::string::npos && (start == std::string::npos || pres < start)) {
                start = pres;
                kind = "presence";
            }
            if (iq != std::string::npos && (start == std::string::npos || iq < start)) {
                start = iq;
                kind = "iq";
            }
            if (!kind || start == std::string::npos) break;
            if (start > 0) stream_buf_.erase(0, start);
            bool self_close = false;
            size_t gt = stream_buf_.find('>');
            if (gt == std::string::npos) break;
            if (stream_buf_[gt - 1] == '/') self_close = true;
            size_t end = std::string::npos;
            if (self_close) {
                end = gt + 1;
            } else if (std::strcmp(kind, "message") == 0) {
                end = stanza_end_nested(stream_buf_, kind);
                if (end == std::string::npos) break;
            } else {
                std::string close = std::string("</") + kind + ">";
                end = stream_buf_.find(close);
                if (end == std::string::npos) break;
                end += close.size();
            }
            std::string stanza = stream_buf_.substr(0, end);
            stream_buf_.erase(0, end);
            handle_stanza(stanza);
        }
    }

    // Inner <message> from a carbon <forwarded> wrapper.
    static std::string extract_forwarded_message(const std::string &st) {
        size_t f = st.find("<forwarded");
        if (f == std::string::npos) return {};
        size_t m = st.find("<message", f);
        if (m == std::string::npos) return {};
        size_t end = st.find("</message>", m);
        if (end == std::string::npos) return {};
        return st.substr(m, end + 10 - m);
    }

    static std::string extract_open_tag(const std::string &st, const char *name) {
        std::string open = std::string("<") + name;
        size_t a = st.find(open);
        if (a == std::string::npos) return {};
        size_t gt = st.find('>', a);
        if (gt == std::string::npos) return {};
        return st.substr(a, gt - a + 1);
    }

    static bool mam_attr_true(const std::string &tag, const char *key) {
        std::string v = attr(tag, key);
        return v == "true" || v == "1";
    }

    void send_mam_query(const std::string &with, int max, bool older,
                        const std::string &before_uid) {
        std::string id = "mam" + std::to_string(iq_seq_++);
        {
            std::lock_guard<std::mutex> lock(mu);
            MamPending pend;
            pend.with = with;
            pend.older = older;
            mam_pending_[id] = std::move(pend);
        }
        std::string before_xml =
            before_uid.empty() ? "<before/>"
                               : ("<before>" + xml_escape(before_uid) + "</before>");
        queue_send(
            "<iq type='set' id='" + id + "'><query xmlns='urn:xmpp:mam:2' queryid='" +
            id + "'><x xmlns='jabber:x:data' type='submit'>"
            "<field var='FORM_TYPE' type='hidden'><value>urn:xmpp:mam:2</value></field>"
            "<field var='with'><value>" + xml_escape(with) +
            "</value></field></x>"
            "<set xmlns='http://jabber.org/protocol/rsm'><max>" +
            std::to_string(max) + "</max>" + before_xml + "</set></query></iq>");
    }

    // Build a transcript line from an archive/carbon message. Returns false if
    // there is no body line to store (reactions applied separately).
    bool make_archive_chat_line(const std::string &st, bool sent_carbon,
                                std::string *key_out, ChatLine *ln_out) {
        if (!key_out || !ln_out) return false;
        std::string from = attr(st, "from");
        std::string to = attr(st, "to");
        std::string type = attr(st, "type");
        std::string mid = attr(st, "id");
        std::string body;
        extract_tag(st, "body", &body);
        body = xml_unescape(body);
        if (body.empty()) return false;

        bool mine = sent_carbon;
        std::string key;
        if (!sent_carbon) {
            mine = jid_ieq(bare_jid(from), bare_jid(jid));
            key = mine ? bare_jid(to) : bare_jid(from);
        } else {
            key = bare_jid(to);
        }
        if (key.empty()) key = bare_jid(from);
        bool is_muc = (type == "groupchat");
        if (!is_muc) {
            std::lock_guard<std::mutex> lock(mu);
            is_muc = muc_joined.count(key) != 0;
        }
        std::string who =
            is_muc ? (mine ? jid_node(jid) : jid_resource(from)) : (mine ? jid : from);
        if (who.empty()) who = from;
        std::string react_id = is_muc ? stanza_id_by(st, key) : mid;
        if (react_id.empty() && !is_muc) react_id = mid;

        ChatLine ln;
        ln.from = who;
        ln.body = body;
        ln.mine = mine;
        ln.id = mid;
        ln.react_id = react_id;
        ln.when = delay_stamp_from_stanza(st);
        if (!ln.when) ln.when = std::time(nullptr);
        *key_out = key;
        *ln_out = std::move(ln);
        return true;
    }

    // Merge a finished MAM page into chats[with]. Returns lines prepended (older)
    // or 0 for initial merge. Caller holds mu.
    int finish_mam_locked(MamPending &pend, bool complete, const std::string &rsm_first,
                          const std::string &rsm_last) {
        const std::string &with = pend.with;
        std::string oldest = !rsm_first.empty() ? rsm_first : pend.first_uid;
        if (!oldest.empty()) mam_oldest_[with] = oldest;
        else if (!rsm_last.empty())
            mam_oldest_[with] = rsm_last;
        mam_complete_[with] = complete || pend.buf.empty();
        mam_inflight_.erase(with);

        if (pend.older) {
            if (pend.buf.empty()) {
                mam_complete_[with] = true;
                return 0;
            }
            auto &lines = chats[with];
            std::set<std::string> have;
            for (const auto &ln : lines)
                if (!ln.id.empty()) have.insert(ln.id);
            std::vector<ChatLine> add;
            add.reserve(pend.buf.size());
            for (auto &ln : pend.buf) {
                if (!ln.id.empty() && have.count(ln.id)) continue;
                if (!ln.id.empty()) have.insert(ln.id);
                add.push_back(std::move(ln));
            }
            if (add.empty()) {
                mam_complete_[with] = true;
                return 0;
            }
            int n = (int)add.size();
            lines.insert(lines.begin(), std::make_move_iterator(add.begin()),
                         std::make_move_iterator(add.end()));
            for (auto &ln : lines)
                if (!ln.react_id.empty())
                    refresh_line_reactions_locked(with, ln.react_id);
            return n;
        }

        // Initial (last page): buffer is the authoritative recent window.
        auto &lines = chats[with];
        std::set<std::string> in_buf;
        for (const auto &ln : pend.buf)
            if (!ln.id.empty()) in_buf.insert(ln.id);
        std::vector<ChatLine> live_extra;
        for (auto &ln : lines) {
            if (!ln.id.empty() && in_buf.count(ln.id)) continue;
            live_extra.push_back(std::move(ln));
        }
        const bool buf_was_empty = pend.buf.empty();
        lines = std::move(pend.buf);
        for (auto &ln : live_extra) lines.push_back(std::move(ln));
        for (auto &ln : lines)
            if (!ln.react_id.empty())
                refresh_line_reactions_locked(with, ln.react_id);
        if (buf_was_empty && lines.empty()) mam_complete_[with] = true;
        return 0;
    }

    void mark_delivered(const std::string &peer, const std::string &rid) {
        if (rid.empty()) return;
        std::string key = bare_jid(peer);
        std::lock_guard<std::mutex> lock(mu);
        auto it = chats.find(key);
        if (it == chats.end()) return;
        for (auto &ln : it->second) {
            if (ln.mine && ln.id == rid) {
                ln.delivered = true;
                break;
            }
        }
    }

    // Rebuild ChatLine.reactions from reaction_sets (caller holds mu).
    void refresh_line_reactions_locked(const std::string &chat,
                                       const std::string &react_id) {
        auto cit = chats.find(chat);
        if (cit == chats.end()) return;
        ChatLine *target = nullptr;
        for (auto &ln : cit->second) {
            if (!ln.react_id.empty() && ln.react_id == react_id) {
                target = &ln;
                break;
            }
        }
        if (!target) return;
        std::map<std::string, ReactionMark> agg;
        auto rit = reaction_sets.find(chat);
        if (rit != reaction_sets.end()) {
            auto mid = rit->second.find(react_id);
            if (mid != rit->second.end()) {
                std::string my_key;
                if (muc_joined.count(chat)) {
                    auto nit = muc_nicks.find(chat);
                    my_key = nit != muc_nicks.end() ? nit->second : jid_node(jid);
                } else {
                    my_key = bare_jid(jid);
                }
                for (const auto &kv : mid->second) {
                    bool mine = jid_ieq(kv.first, my_key) || kv.first == my_key;
                    for (const auto &emoji : kv.second) {
                        auto &m = agg[emoji];
                        m.emoji = emoji;
                        m.count += 1;
                        if (mine) m.mine = true;
                    }
                }
            }
        }
        target->reactions.clear();
        for (auto &kv : agg) target->reactions.push_back(kv.second);
    }

    void apply_reaction_locked(const std::string &chat, const std::string &react_id,
                               const std::string &from_key,
                               const std::vector<std::string> &emojis) {
        if (chat.empty() || react_id.empty() || from_key.empty()) return;
        if (emojis.empty())
            reaction_sets[chat][react_id].erase(from_key);
        else
            reaction_sets[chat][react_id][from_key] = emojis;
        if (reaction_sets[chat][react_id].empty()) reaction_sets[chat].erase(react_id);
        if (reaction_sets[chat].empty()) reaction_sets.erase(chat);
        refresh_line_reactions_locked(chat, react_id);
    }

    // XEP-0444 inbound / carbon / archive. Returns true if stanza was reactions-only.
    bool try_apply_reactions(const std::string &st, bool carbon_sent) {
        if (st.find("urn:xmpp:reactions:0") == std::string::npos) return false;
        size_t rp = st.find("<reactions");
        if (rp == std::string::npos) return false;
        size_t gt = st.find('>', rp);
        if (gt == std::string::npos) return false;
        std::string tag = st.substr(rp, gt - rp + 1);
        if (tag.find("urn:xmpp:reactions:0") == std::string::npos &&
            st.find("xmlns='urn:xmpp:reactions:0'") == std::string::npos &&
            st.find("xmlns=\"urn:xmpp:reactions:0\"") == std::string::npos)
            return false;
        std::string react_id = attr(tag, "id");
        if (react_id.empty()) return false;
        std::string from = attr(st, "from");
        std::string to = attr(st, "to");
        std::string type = attr(st, "type");
        bool is_muc = (type == "groupchat");
        std::string key = carbon_sent ? bare_jid(to) : bare_jid(from);
        if (key.empty()) key = bare_jid(from);
        if (!is_muc) {
            std::lock_guard<std::mutex> lock(mu);
            is_muc = muc_joined.count(key) != 0;
        }
        std::string from_key =
            is_muc ? jid_resource(from) : bare_jid(carbon_sent ? jid : from);
        if (from_key.empty()) from_key = bare_jid(from);
        if (carbon_sent && !is_muc) from_key = bare_jid(jid);
        if (carbon_sent && is_muc) {
            std::lock_guard<std::mutex> lock(mu);
            auto nit = muc_nicks.find(key);
            from_key = nit != muc_nicks.end() ? nit->second : jid_node(jid);
        }
        std::vector<std::string> emojis = parse_reaction_emojis(st);
        {
            std::lock_guard<std::mutex> lock(mu);
            apply_reaction_locked(key, react_id, from_key, emojis);
        }
        emit(make_event(ClientEvent::Reaction, react_id, key));
        // Reactions-only (no body) — consume stanza.
        std::string body;
        extract_tag(st, "body", &body);
        return body.empty();
    }

    // Process a chat/groupchat message (plain, carbon, or MAM archive).
    // carbon_sent: XEP-0280 <sent> — we wrote this from another resource.
    // from_archive: XEP-0313 — no receipts / chat-state / per-line ding.
    void ingest_message(const std::string &st, bool carbon_sent,
                        bool from_archive = false) {
        std::string from = attr(st, "from");
        std::string to = attr(st, "to");
        std::string type = attr(st, "type");
        std::string mid = attr(st, "id");
        std::string body, subject;
        extract_tag(st, "body", &body);
        extract_tag(st, "subject", &subject);
        body = xml_unescape(body);
        subject = xml_unescape(subject);

        // XEP-0444 reactions (often body-less).
        if (try_apply_reactions(st, carbon_sent)) return;

        // XEP-0184 delivery receipt (no body).
        if (body.empty() && subject.empty() && !carbon_sent &&
            st.find("urn:xmpp:receipts") != std::string::npos) {
            size_t rp = st.find("<received");
            while (rp != std::string::npos) {
                size_t gt = st.find('>', rp);
                if (gt == std::string::npos) break;
                std::string tag = st.substr(rp, gt - rp + 1);
                if (tag.find("urn:xmpp:carbons") == std::string::npos) {
                    std::string rid = attr(tag, "id");
                    if (!rid.empty()) {
                        mark_delivered(from, rid);
                        emit(make_event(ClientEvent::Receipt, rid, bare_jid(from)));
                        return;
                    }
                }
                rp = st.find("<received", gt);
            }
        }

        std::string key = carbon_sent ? bare_jid(to) : bare_jid(from);
        if (key.empty()) key = bare_jid(from);
        bool is_muc = (type == "groupchat");
        if (!is_muc) {
            std::lock_guard<std::mutex> lock(mu);
            is_muc = muc_joined.count(key) != 0;
        }

        // XEP-0085 chat states (1:1 only; never from archive).
        const char *cs = nullptr;
        if (!is_muc && !carbon_sent && !from_archive) {
            if (st.find("<composing") != std::string::npos) cs = "composing";
            else if (st.find("<paused") != std::string::npos) cs = "paused";
            else if (st.find("<active") != std::string::npos) cs = "active";
            else if (st.find("<inactive") != std::string::npos) cs = "inactive";
            else if (st.find("<gone") != std::string::npos) cs = "gone";
        }
        if (!subject.empty()) {
            {
                std::lock_guard<std::mutex> lock(mu);
                muc_subjects[key] = subject;
                ChatLine ln;
                ln.body = "Topic: " + subject;
                ln.system = true;
                ln.when = delay_stamp_from_stanza(st);
                if (!ln.when) ln.when = std::time(nullptr);
                chats[key].push_back(std::move(ln));
            }
            emit(make_event(ClientEvent::MucSubject, subject, key));
        }
        if (body.empty()) {
            if (cs) {
                {
                    std::lock_guard<std::mutex> lock(mu);
                    if (std::strcmp(cs, "composing") == 0)
                        chat_states[key] = cs;
                    else
                        chat_states.erase(key);
                }
                emit(make_event(ClientEvent::ChatState, cs, key));
            }
            return;
        }

        bool mine = carbon_sent;
        if (from_archive && !carbon_sent) {
            // Archive: ours if from matches our bare JID.
            mine = jid_ieq(bare_jid(from), bare_jid(jid));
            if (mine) key = bare_jid(to);
            else key = bare_jid(from);
            if (key.empty()) key = bare_jid(from);
        }
        std::string who =
            is_muc ? (mine ? jid_node(jid) : jid_resource(from)) : (mine ? jid : from);
        if (who.empty()) who = from;
        // XEP-0444: MUC reactions target stanza-id; 1:1 uses message @id.
        std::string react_id = is_muc ? stanza_id_by(st, key) : mid;
        if (react_id.empty() && !is_muc) react_id = mid;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!mid.empty()) {
                auto &lines = chats[key];
                for (const auto &ln : lines) {
                    if (!ln.id.empty() && ln.id == mid) return; // dedupe
                }
            }
            if (is_muc && type == "groupchat" && !carbon_sent && !from_archive) {
                auto nit = muc_nicks.find(key);
                if (nit != muc_nicks.end() && who == nit->second) {
                    auto &lines = chats[key];
                    if (!lines.empty() && lines.back().mine &&
                        lines.back().body == body) {
                        // Our echo — stamp room stanza-id so others can react.
                        if (!react_id.empty()) lines.back().react_id = react_id;
                        if (lines.back().id.empty() && !mid.empty())
                            lines.back().id = mid;
                        return;
                    }
                }
            }
            ChatLine ln;
            ln.from = who;
            ln.body = body;
            ln.mine = mine;
            ln.id = mid;
            ln.react_id = react_id;
            ln.when = delay_stamp_from_stanza(st);
            if (!ln.when) ln.when = std::time(nullptr);
            chats[key].push_back(std::move(ln));
            if (!mine && !from_archive) chat_states.erase(key);
            // Attach any reactions that arrived before the message (rare).
            if (!react_id.empty()) refresh_line_reactions_locked(key, react_id);
        }

        // Reply to XEP-0184 receipt requests on inbound 1:1 (live only).
        if (!from_archive && !mine && !is_muc && !mid.empty() &&
            st.find("<request") != std::string::npos &&
            st.find("urn:xmpp:receipts") != std::string::npos) {
            queue_send("<message to='" + xml_escape(bare_jid(from)) +
                       "' type='chat'><received xmlns='urn:xmpp:receipts' id='" +
                       xml_escape(mid) + "'/></message>");
        }
        if (!from_archive)
            emit(make_event(ClientEvent::Message, body, key));
    }

    // Mediated MUC invite (XEP-0045 muc#user) — queue for Accept/Decline UI.
    bool try_queue_muc_invite(const std::string &st) {
        if (st.find("muc#user") == std::string::npos ||
            st.find("<invite") == std::string::npos)
            return false;
        if (st.find("<decline") != std::string::npos) return false;
        size_t ip = st.find("<invite");
        if (ip == std::string::npos) return false;
        size_t gt = st.find('>', ip);
        if (gt == std::string::npos) return false;
        std::string tag = st.substr(ip, gt - ip + 1);
        // Mediated: message from=room, invite from=inviter.
        std::string room = bare_jid(attr(st, "from"));
        std::string inviter = bare_jid(attr(tag, "from"));
        if (room.empty() || inviter.empty()) return false;
        if (jid_ieq(room, bare_jid(jid))) return false;
        std::string reason;
        size_t rp = st.find("<reason", ip);
        if (rp != std::string::npos) {
            extract_tag(st.substr(rp), "reason", &reason);
            reason = xml_unescape(reason);
        }
        MucInvite iv{room, inviter, reason};
        {
            std::lock_guard<std::mutex> lock(mu);
            for (const auto &e : pending_muc_invites)
                if (jid_ieq(e.room, iv.room) && jid_ieq(e.from, iv.from))
                    return true;
            pending_muc_invites.push_back(iv);
        }
        emit(make_event(ClientEvent::MucInviteAsk, iv.from, iv.room));
        return true;
    }

    void handle_stanza(const std::string &st) {
        if (st.find("<message") == 0) {
            // XEP-0313 MAM result — unwrap into the pending query buffer.
            if (st.find("urn:xmpp:mam:2") != std::string::npos &&
                st.find("<result") != std::string::npos) {
                std::string rtag = extract_open_tag(st, "result");
                std::string qid = attr(rtag, "queryid");
                std::string archive_id = attr(rtag, "id");
                std::string inner = extract_forwarded_message(st);
                if (inner.empty()) return;
                bool sent = jid_ieq(bare_jid(attr(inner, "from")), bare_jid(jid));
                if (try_apply_reactions(inner, sent)) return;
                std::string key;
                ChatLine ln;
                if (!make_archive_chat_line(inner, sent, &key, &ln)) return;
                {
                    std::lock_guard<std::mutex> lock(mu);
                    MamPending *pend = nullptr;
                    if (!qid.empty()) {
                        auto it = mam_pending_.find(qid);
                        if (it != mam_pending_.end()) pend = &it->second;
                    }
                    if (!pend) {
                        // Orphan result — quiet append (legacy path).
                        if (!ln.id.empty()) {
                            for (const auto &e : chats[key])
                                if (!e.id.empty() && e.id == ln.id) return;
                        }
                        chats[key].push_back(std::move(ln));
                        return;
                    }
                    if (pend->first_uid.empty() && !archive_id.empty())
                        pend->first_uid = archive_id;
                    if (!archive_id.empty()) pend->last_uid = archive_id;
                    // Prefer the query's with= peer as the chat key.
                    if (!pend->with.empty()) key = pend->with;
                    if (!ln.id.empty()) {
                        for (const auto &e : pend->buf)
                            if (!e.id.empty() && e.id == ln.id) return;
                    }
                    pend->buf.push_back(std::move(ln));
                }
                return;
            }
            // XEP-0280 Message Carbons — unwrap forwarded payload.
            if (st.find("urn:xmpp:carbons:2") != std::string::npos) {
                bool sent = st.find("<sent") != std::string::npos;
                std::string inner = extract_forwarded_message(st);
                if (!inner.empty()) ingest_message(inner, sent);
                return;
            }
            if (try_queue_muc_invite(st)) return;
            ingest_message(st, false);
            return;
        }
        if (st.find("<presence") == 0) {
            std::string from = attr(st, "from");
            std::string type = attr(st, "type");
            std::string bare = bare_jid(from);
            if (type == "subscribe") {
                bool queued = false;
                {
                    std::lock_guard<std::mutex> lock(mu);
                    bool have = false;
                    for (const auto &j : pending_subscribe)
                        if (jid_ieq(j, bare)) {
                            have = true;
                            break;
                        }
                    if (!have) {
                        pending_subscribe.push_back(bare);
                        queued = true;
                    }
                }
                if (queued) {
                    emit(make_event(ClientEvent::SubscribeAsk,
                                    jid_node(bare) + " wants to add you", bare));
                    emit(make_event(ClientEvent::StatusText,
                                    jid_node(bare) + " wants to add you"));
                }
                return;
            }
            if (type == "subscribed") {
                emit(make_event(ClientEvent::StatusText,
                                jid_node(bare) + " accepted your request"));
                return;
            }
            if (type == "unsubscribed") {
                emit(make_event(ClientEvent::StatusText,
                                jid_node(bare) + " removed or denied you"));
                return;
            }
            if (type == "unsubscribe") {
                // They stopped watching us — no UI dialog this pass.
                return;
            }
            std::string show_s, status_s;
            extract_tag(st, "show", &show_s);
            extract_tag(st, "status", &status_s);
            Show sh = Show::Chat;
            if (type == "unavailable") sh = Show::Unavailable;
            else if (show_s == "away") sh = Show::Away;
            else if (show_s == "xa") sh = Show::Xa;
            else if (show_s == "dnd") sh = Show::Dnd;
            bool muc = false;
            {
                std::lock_guard<std::mutex> lock(mu);
                if (roster.count(bare)) {
                    Show prev = roster[bare].show;
                    roster[bare].show = sh;
                    roster[bare].status = xml_unescape(status_s);
                    std::string alert;
                    if (prev == Show::Unavailable && sh != Show::Unavailable)
                        alert = jid_node(bare) + " signed on";
                    else if (prev != Show::Unavailable && sh == Show::Unavailable)
                        alert = jid_node(bare) + " signed off";
                    if (!alert.empty()) status_text = alert;
                }
                // MUC occupant: from = room/nick (only for rooms we joined)
                size_t slash = from.find('/');
                if (slash != std::string::npos && muc_joined.count(bare)) {
                    muc = true;
                    std::string nick = from.substr(slash + 1);
                    std::string real = muc_item_real_jid(st);
                    auto &occ = muc_occupants[bare];
                    if (type == "unavailable") {
                        occ.erase(std::remove_if(occ.begin(), occ.end(),
                                                 [&](const MucOccupant &o) {
                                                     return o.nick == nick;
                                                 }),
                                  occ.end());
                    } else {
                        auto oit = std::find_if(occ.begin(), occ.end(),
                                                [&](const MucOccupant &o) {
                                                    return o.nick == nick;
                                                });
                        if (oit != occ.end()) {
                            if (!real.empty()) oit->real_jid = real;
                        } else {
                            occ.push_back(MucOccupant{nick, real});
                        }
                    }
                }
            }
            emit(make_event(ClientEvent::Presence, status_s, bare));
            if (muc) emit(make_event(ClientEvent::MucOccupants, {}, bare));
            // Fetch vCard PHOTO only when a real bare JID is disclosed (privacy).
            if (muc && type != "unavailable") {
                std::string real = muc_item_real_jid(st);
                bool need = false;
                bool photo_update =
                    st.find("vcard-temp:x:update") != std::string::npos;
                if (!real.empty()) {
                    std::lock_guard<std::mutex> lock(mu);
                    if (photo_update) {
                        vcard_attempted_.erase(real);
                        if (roster.count(real)) roster[real].vcard_fetched = false;
                    }
                    need = !vcard_attempted_.count(real) &&
                           !vcard_inflight_.count(real);
                }
                if (need) request_vcard(real);
            }
            // Buddy published a new picture — refresh their vCard.
            if (!muc && st.find("vcard-temp:x:update") != std::string::npos) {
                bool on_roster = false;
                {
                    std::lock_guard<std::mutex> lock(mu);
                    if (roster.count(bare)) {
                        roster[bare].vcard_fetched = false;
                        vcard_attempted_.erase(bare);
                        on_roster = true;
                    }
                }
                if (on_roster) request_vcard(bare);
            }
            return;
        }
        if (st.find("<iq") == 0) {
            std::string iq_id = attr(st, "id");
            std::string iq_type = attr(st, "type");
            if (iq_id.rfind("vcset", 0) == 0) {
                if (iq_type == "result") {
                    emit(make_event(ClientEvent::StatusText, "Picture updated"));
                    publish_presence();
                } else if (iq_type == "error") {
                    emit(make_event(ClientEvent::StatusText, "Server rejected picture"));
                }
            } else if (st.find("vcard-temp") != std::string::npos ||
                       st.find("<vCard") != std::string::npos ||
                       st.find("<vcard") != std::string::npos) {
                std::string from = attr(st, "from");
                std::string bare = from.empty() ? bare_jid(jid) : bare_jid(from);
                apply_vcard(bare, st);
            }
            if (st.find("jabber:iq:roster") != std::string::npos) {
                parse_roster(st);
                emit(make_event(ClientEvent::Roster));
            }
            if (st.find("disco#items") != std::string::npos)
                handle_disco_items(st);
            if (st.find("urn:xmpp:http:upload:0") != std::string::npos) {
                if (st.find("<slot") != std::string::npos) {
                    std::string put, get;
                    size_t p = st.find("<put");
                    if (p != std::string::npos) put = attr(st.substr(p), "url");
                    p = st.find("<get");
                    if (p != std::string::npos) get = attr(st.substr(p), "url");
                    if (!put.empty() && !get.empty()) finish_upload(put, get);
                }
            }
            if (st.find("disco#info") != std::string::npos)
                handle_disco_info(st);
            if (st.find("urn:xmpp:bookmarks:1") != std::string::npos ||
                st.find("storage:bookmarks") != std::string::npos)
                handle_bookmarks_iq(st);
            if (iq_id.rfind("mam", 0) == 0) {
                MamPending pend;
                bool have = false;
                {
                    std::lock_guard<std::mutex> lock(mu);
                    auto it = mam_pending_.find(iq_id);
                    if (it != mam_pending_.end()) {
                        pend = std::move(it->second);
                        mam_pending_.erase(it);
                        have = true;
                    }
                }
                if (iq_type == "error") {
                    {
                        std::lock_guard<std::mutex> lock(mu);
                        if (have) {
                            mam_inflight_.erase(pend.with);
                            if (!pend.older) mam_initial_done_.erase(pend.with);
                        }
                    }
                    if (have && !pend.with.empty())
                        emit(make_event(ClientEvent::History, "error", pend.with));
                } else if (have && !pend.with.empty()) {
                    bool complete = false;
                    std::string fin_tag = extract_open_tag(st, "fin");
                    if (!fin_tag.empty()) complete = mam_attr_true(fin_tag, "complete");
                    std::string rsm_first, rsm_last;
                    extract_tag(st, "first", &rsm_first);
                    extract_tag(st, "last", &rsm_last);
                    int prepended = 0;
                    {
                        std::lock_guard<std::mutex> lock(mu);
                        prepended =
                            finish_mam_locked(pend, complete, rsm_first, rsm_last);
                    }
                    std::string kind =
                        pend.older ? ("older:" + std::to_string(prepended)) : "initial";
                    emit(make_event(ClientEvent::History, kind, pend.with));
                }
            }
            if (st.find("jabber:iq:register") != std::string::npos) {
                // handled in register_flow synchronously via stream_buf
            }
        }
    }

    void handle_disco_items(const std::string &st) {
        std::string from = attr(st, "from");
        std::string id = attr(st, "id");
        std::string conf;
        {
            std::lock_guard<std::mutex> lock(mu);
            conf = conference_host;
        }
        bool room_list = (id.rfind("mucrooms", 0) == 0) ||
                         (!conf.empty() && !from.empty() && jid_ieq(from, conf));
        if (room_list) {
            std::vector<MucRoomInfo> rooms;
            size_t pos = 0;
            while ((pos = st.find("<item", pos)) != std::string::npos) {
                size_t end = st.find('>', pos);
                if (end == std::string::npos) break;
                std::string tag = st.substr(pos, end - pos + 1);
                MucRoomInfo r;
                r.jid = attr(tag, "jid");
                r.name = attr(tag, "name");
                if (r.name.empty()) r.name = jid_node(r.jid);
                if (!r.jid.empty() && r.jid.find('@') != std::string::npos)
                    rooms.push_back(r);
                pos = end + 1;
            }
            size_t nrooms = rooms.size();
            {
                std::lock_guard<std::mutex> lock(mu);
                muc_rooms = std::move(rooms);
            }
            emit(make_event(ClientEvent::MucRooms));
            emit(make_event(ClientEvent::StatusText,
                            "Found " + std::to_string(nrooms) + " chat rooms"));
            return;
        }
        // Host disco#items → probe each child for upload + conference.
        size_t pos = 0;
        int n = 0;
        while ((pos = st.find("<item", pos)) != std::string::npos && n < 24) {
            size_t end = st.find('>', pos);
            if (end == std::string::npos) break;
            std::string j = attr(st.substr(pos, end - pos + 1), "jid");
            if (!j.empty()) {
                queue_send("<iq type='get' id='du" + std::to_string(n) +
                           "' to='" + xml_escape(j) +
                           "'><query xmlns='http://jabber.org/protocol/disco#info'/>"
                           "</iq>");
                ++n;
            }
            pos = end + 1;
        }
    }

    void handle_disco_info(const std::string &st) {
        std::string from = attr(st, "from");
        if (st.find("urn:xmpp:http:upload:0") != std::string::npos) {
            std::lock_guard<std::mutex> lock(mu);
            upload_available = true;
            if (!from.empty()) http_upload_host = from;
        }
        if (st.find("urn:xmpp:mam:2") != std::string::npos) {
            std::lock_guard<std::mutex> lock(mu);
            mam_available = true;
        }
        bool is_conf =
            st.find("category='conference'") != std::string::npos ||
            st.find("category=\"conference\"") != std::string::npos ||
            st.find("http://jabber.org/protocol/muc") != std::string::npos;
        if (is_conf && !from.empty()) {
            bool first = false;
            {
                std::lock_guard<std::mutex> lock(mu);
                if (conference_host.empty()) {
                    conference_host = from;
                    first = true;
                }
            }
            if (first) {
                queue_send("<iq type='get' id='mucrooms0' to='" + xml_escape(from) +
                           "'><query xmlns='http://jabber.org/protocol/disco#items'/>"
                           "</iq>");
                emit(make_event(ClientEvent::StatusText,
                                "Chat service: " + from));
            }
        }
    }

    void handle_bookmarks_iq(const std::string &st) {
        if (attr(st, "type") == "error") return;
        std::vector<MucBookmark> found;
        // XEP-0402: <conference xmlns='urn:xmpp:bookmarks:1' …/> inside <item id='jid'>
        size_t pos = 0;
        while ((pos = st.find("<item", pos)) != std::string::npos) {
            size_t end = st.find("</item>", pos);
            size_t self = st.find("/>", pos);
            std::string chunk;
            if (end != std::string::npos && (self == std::string::npos || end < self))
                chunk = st.substr(pos, end + 7 - pos);
            else if (self != std::string::npos)
                chunk = st.substr(pos, self + 2 - pos);
            else
                break;
            MucBookmark bm;
            bm.jid = attr(chunk, "id");
            size_t c = chunk.find("<conference");
            if (c != std::string::npos) {
                size_t cend = chunk.find('>', c);
                std::string ctag = chunk.substr(c, cend - c + 1);
                if (bm.jid.empty()) bm.jid = attr(ctag, "jid");
                bm.name = attr(ctag, "name");
                std::string aj = attr(ctag, "autojoin");
                bm.autojoin = (aj == "true" || aj == "1");
                extract_tag(chunk, "nick", &bm.nick);
            }
            if (!bm.jid.empty() && bm.jid.find('@') != std::string::npos)
                found.push_back(bm);
            pos = pos + 5;
        }
        // XEP-0048 private XML: <conference jid='…' …/>
        pos = 0;
        while ((pos = st.find("<conference", pos)) != std::string::npos) {
            size_t end = st.find('>', pos);
            if (end == std::string::npos) break;
            std::string tag = st.substr(pos, end - pos + 1);
            MucBookmark bm;
            bm.jid = attr(tag, "jid");
            bm.name = attr(tag, "name");
            std::string aj = attr(tag, "autojoin");
            bm.autojoin = (aj == "true" || aj == "1");
            if (st[end - 1] != '/') {
                size_t close = st.find("</conference>", end);
                if (close != std::string::npos) {
                    std::string inner = st.substr(end + 1, close - end - 1);
                    extract_tag(inner, "nick", &bm.nick);
                }
            }
            if (!bm.jid.empty() && bm.jid.find('@') != std::string::npos) {
                bool dup = false;
                for (const auto &f : found)
                    if (jid_ieq(f.jid, bm.jid)) {
                        dup = true;
                        break;
                    }
                if (!dup) found.push_back(bm);
            }
            pos = end + 1;
        }
        if (found.empty() && st.find("id='bmpep1'") != std::string::npos)
            return; // wait for private XML fallback
        if (!found.empty()) {
            {
                std::lock_guard<std::mutex> lock(mu);
                // Prefer richer list; merge by jid.
                for (const auto &bm : found) {
                    bool hit = false;
                    for (auto &e : muc_bookmarks) {
                        if (jid_ieq(e.jid, bm.jid)) {
                            e = bm;
                            hit = true;
                            break;
                        }
                    }
                    if (!hit) muc_bookmarks.push_back(bm);
                }
            }
            emit(make_event(ClientEvent::Bookmarks));
            autojoin_bookmarks();
        }
    }

    void publish_bookmarks() {
        std::vector<MucBookmark> copy;
        {
            std::lock_guard<std::mutex> lock(mu);
            copy = muc_bookmarks;
        }
        // PEP Native Bookmarks — publish each room as its own item.
        for (const auto &b : copy) {
            std::string iq =
                "<iq type='set' id='bmset" + std::to_string(iq_seq_++) + "'>"
                "<pubsub xmlns='http://jabber.org/protocol/pubsub'>"
                "<publish node='urn:xmpp:bookmarks:1'>"
                "<item id='" +
                xml_escape(b.jid) + "'><conference xmlns='urn:xmpp:bookmarks:1' name='" +
                xml_escape(b.name.empty() ? jid_node(b.jid) : b.name) +
                "' autojoin='" + (b.autojoin ? "true" : "false") + "'>";
            if (!b.nick.empty())
                iq += "<nick>" + xml_escape(b.nick) + "</nick>";
            iq += "</conference></item></publish></pubsub></iq>";
            queue_send(iq);
        }
        // Private XML storage:bookmarks (legacy clients / servers).
        std::string storage =
            "<iq type='set' id='bmprivset'><query xmlns='jabber:iq:private'>"
            "<storage xmlns='storage:bookmarks'>";
        for (const auto &b : copy) {
            storage += "<conference jid='" + xml_escape(b.jid) + "' name='" +
                       xml_escape(b.name.empty() ? jid_node(b.jid) : b.name) +
                       "' autojoin='" + (b.autojoin ? "true" : "false") + "'>";
            if (!b.nick.empty())
                storage += "<nick>" + xml_escape(b.nick) + "</nick>";
            storage += "</conference>";
        }
        storage += "</storage></query></iq>";
        queue_send(storage);
    }

    void autojoin_bookmarks() {
        std::vector<MucBookmark> join;
        {
            std::lock_guard<std::mutex> lock(mu);
            for (const auto &b : muc_bookmarks)
                if (b.autojoin && !muc_joined.count(b.jid)) join.push_back(b);
        }
        for (const auto &b : join) {
            std::string nick = b.nick.empty() ? jid_node(jid) : b.nick;
            join_muc(b.jid, nick);
            emit(make_event(ClientEvent::StatusText, "Autojoining " + b.jid));
            // UI opens tabs via Message/Bookmarks — emit a synthetic cue.
            emit(make_event(ClientEvent::Message, {}, b.jid));
        }
    }

    void parse_roster(const std::string &st) {
        std::vector<std::string> need_vcard;
        {
            std::lock_guard<std::mutex> lock(mu);
            size_t pos = 0;
            while ((pos = st.find("<item", pos)) != std::string::npos) {
                size_t end = st.find('>', pos);
                if (end == std::string::npos) break;
                std::string tag = st.substr(pos, end - pos + 1);
                bool self_close =
                    tag.size() >= 2 && tag[tag.size() - 2] == '/';
                Buddy b;
                b.jid = attr(tag, "jid");
                b.name = attr(tag, "name");
                if (b.name.empty()) b.name = jid_node(b.jid);
                std::string sub = attr(tag, "subscription");
                if (sub == "remove") {
                    if (!b.jid.empty()) roster.erase(b.jid);
                    pos = end + 1;
                    continue;
                }
                b.subscription_to = (sub == "both" || sub == "to");
                if (!self_close) {
                    size_t close = st.find("</item>", end);
                    if (close != std::string::npos) {
                        std::string body = st.substr(end + 1, close - (end + 1));
                        std::string grp;
                        if (extract_tag(body, "group", &grp) && !grp.empty())
                            b.group = xml_unescape(grp);
                        pos = close + 7;
                    } else {
                        pos = end + 1;
                    }
                } else {
                    pos = end + 1;
                }
                if (b.group.empty()) b.group = "Buddies";
                if (!b.jid.empty()) {
                    auto it = roster.find(b.jid);
                    if (it != roster.end()) {
                        b.show = it->second.show;
                        b.status = it->second.status;
                        b.avatar = std::move(it->second.avatar);
                        b.vcard_fetched = it->second.vcard_fetched;
                    }
                    if (!b.vcard_fetched) need_vcard.push_back(b.jid);
                    roster[b.jid] = std::move(b);
                }
            }
        }
        for (auto &j : need_vcard) request_vcard(j);
    }

    void finish_upload(const std::string &put, const std::string &get) {
        emit(make_event(ClientEvent::FileProgress, "Uploading…", {}, 10));
        std::wstring ctype(pending_upload_mime_.begin(), pending_upload_mime_.end());
        HttpResult hr = http_put(put, pending_upload_data_.data(),
                                 pending_upload_data_.size(), ctype.c_str());
        if (!hr.ok) {
            emit(make_event(ClientEvent::StatusText, "Upload failed: " + hr.error));
            return;
        }
        emit(make_event(ClientEvent::FileProgress, "Uploaded", {}, 100));
        std::string body = get;
        send_message(pending_upload_to_, body);
        {
            std::lock_guard<std::mutex> lock(mu);
            auto &lines = chats[bare_jid(pending_upload_to_)];
            if (!lines.empty()) lines.back().file = true;
        }
    }

    bool sasl_plain() {
        std::string authzid;
        std::string payload;
        payload.push_back('\0');
        payload += user_;
        payload.push_back('\0');
        payload += password_;
        std::string stanza =
            "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>" +
            b64(payload) + "</auth>";
        if (!sock_.send_all(stanza)) return false;
        if (!read_until("success") && stream_buf_.find("<success") == std::string::npos) {
            if (stream_buf_.find("failure") != std::string::npos) {
                set_state(ConnState::Error, "Sign on failed (bad password?)");
                return false;
            }
        }
        stream_buf_.clear();
        return open_stream(true);
    }

    bool bind_session() {
        std::string bind =
            "<iq type='set' id='bind1'><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'>"
            "<resource>" +
            xml_escape(resource) + "</resource></bind></iq>";
        if (!sock_.send_all(bind)) return false;
        if (!read_until("jid")) return false;
        std::string j;
        // <jid>user@host/res</jid>
        extract_tag(stream_buf_, "jid", &j);
        if (!j.empty()) {
            std::lock_guard<std::mutex> lock(mu);
            jid = bare_jid(xml_unescape(j));
        }
        sock_.send_all(
            "<iq type='set' id='sess1'><session xmlns='urn:ietf:params:xml:ns:xmpp-session'/>"
            "</iq>");
        read_until("sess1");
        return true;
    }

    bool register_flow() {
        set_state(ConnState::Registering, "Requesting registration form…");
        if (!sock_.send_all("<iq type='get' id='reg1'><query xmlns='jabber:iq:register'/></iq>"))
            return false;
        if (!read_until("jabber:iq:register")) {
            set_state(ConnState::Error,
                      "This server doesn’t allow creating accounts in-app. "
                      "Pick another public server from the list.");
            return false;
        }
        std::string iq = first_element(stream_buf_, "iq");
        if (iq.find("type='error'") != std::string::npos ||
            iq.find("type=\"error\"") != std::string::npos) {
            set_state(ConnState::Error,
                      "This server doesn’t allow creating accounts in-app. "
                      "Pick another public server from the list.");
            return false;
        }
        // CAPTCHA only when we can fetch an image (or ocr field + media URI).
        // Do not treat "captcha-fallback-text" alone as a hard challenge.
        std::string captcha_url;
        bool need_captcha = false;
        if (iq.find("jabber:x:data") != std::string::npos) {
            size_t m = iq.find("http");
            while (m != std::string::npos) {
                size_t e = iq.find_first_of("'\"<", m);
                if (e == std::string::npos) break;
                std::string url = iq.substr(m, e - m);
                if (url.rfind("http", 0) == 0 &&
                    (url.find(".png") != std::string::npos ||
                     url.find(".jpg") != std::string::npos ||
                     url.find(".jpeg") != std::string::npos ||
                     url.find("captcha") != std::string::npos ||
                     url.find("challenge") != std::string::npos)) {
                    captcha_url = url;
                    need_captcha = true;
                    break;
                }
                m = iq.find("http", m + 1);
            }
            std::string uri;
            if (extract_tag(iq, "uri", &uri) || extract_tag(iq, "URL", &uri)) {
                captcha_url = xml_unescape(uri);
                need_captcha = !captcha_url.empty();
            }
        }

        std::string answer;
        if (need_captcha) {
            if (captcha_url.empty()) {
                set_state(ConnState::Error,
                          "Server asked for a CAPTCHA type we can’t show yet. "
                          "Try another recommended server.");
                return false;
            }
            set_state(ConnState::Registering, "Loading CAPTCHA…");
            HttpResult img = http_get(captcha_url);
            SkinImage simg;
            if (!img.ok || !decode_image_vec(img.body, simg)) {
                set_state(ConnState::Error, "Could not load CAPTCHA image.");
                return false;
            }
            {
                std::lock_guard<std::mutex> lock(mu);
                captcha_image = std::move(simg);
                captcha_answer_.clear();
                captcha_submitted_ = false;
            }
            {
                ClientEvent ce;
                ce.type = ClientEvent::CaptchaReady;
                ce.text = "Enter the CAPTCHA";
                emit(ce);
            }
            {
                std::unique_lock<std::mutex> lock(mu);
                captcha_ready_cv_.wait(lock, [&] {
                    return stop_ || captcha_submitted_;
                });
                answer = captcha_answer_;
                if (stop_ || answer.empty()) {
                    set_state(ConnState::Disconnected, "Account create cancelled");
                    return false;
                }
            }
        }

        // Submit register — legacy fields + data form (what Prosody/ejabberd ship).
        std::string query = "<query xmlns='jabber:iq:register'>"
                            "<username>" +
                            xml_escape(user_) + "</username><password>" +
                            xml_escape(password_) + "</password>";
        query += "<x xmlns='jabber:x:data' type='submit'>";
        query += "<field var='FORM_TYPE'><value>jabber:iq:register</value></field>";
        query += "<field var='username'><value>" + xml_escape(user_) +
                 "</value></field>";
        query += "<field var='password'><value>" + xml_escape(password_) +
                 "</value></field>";
        if (!answer.empty()) {
            query += "<field var='ocr'><value>" + xml_escape(answer) +
                     "</value></field>";
        }
        query += "</x></query>";
        std::string setiq =
            "<iq type='set' id='reg2'>" + query + "</iq>";
        // Drop the get-form IQ so read_until("reg2") sees the set result.
        stream_buf_.clear();
        if (!sock_.send_all(setiq)) return false;
        if (!read_until("reg2") && stream_buf_.find("type='result'") == std::string::npos &&
            stream_buf_.find("type=\"result\"") == std::string::npos) {
            if (stream_buf_.find("error") != std::string::npos) {
                set_state(ConnState::Error, "Registration failed (name taken or CAPTCHA).");
                emit(make_event(ClientEvent::RegisterFail, last_error));
                return false;
            }
        }
        if (stream_buf_.find("type='error'") != std::string::npos ||
            stream_buf_.find("type=\"error\"") != std::string::npos) {
            set_state(ConnState::Error, "Registration failed (name taken or CAPTCHA).");
            emit(make_event(ClientEvent::RegisterFail, last_error));
            return false;
        }
        emit(make_event(ClientEvent::RegisterOk, "Account created"));
        set_state(ConnState::Connecting, "Account created — signing on…");
        // Re-stream and auth
        stream_buf_.clear();
        sock_.close();
        if (!sock_.connect_tcp(host_, 5222)) {
            set_state(ConnState::Error, "Reconnect failed after register");
            return false;
        }
        mode_ = Mode::Auth;
        return auth_flow_from_tcp();
    }

    bool auth_flow_from_tcp() {
        set_state(ConnState::Connecting, "Opening stream to " + host_ + "…");
        if (!open_stream(false)) {
            set_state(ConnState::Error,
                      sock_.last_error.empty() ? "No stream from server"
                                               : sock_.last_error);
            return false;
        }
        if (stream_buf_.find("starttls") != std::string::npos) {
            set_state(ConnState::Connecting, "Starting TLS…");
            sock_.send_all(
                "<starttls xmlns='urn:ietf:params:xml:ns:xmpp-tls'/>");
            if (!read_until("proceed")) {
                set_state(ConnState::Error,
                          sock_.last_error.empty() ? "STARTTLS failed"
                                                   : sock_.last_error);
                return false;
            }
            stream_buf_.clear();
            set_state(ConnState::Connecting, "Securing connection…");
            if (!sock_.start_tls(host_)) {
                set_state(ConnState::Error,
                          sock_.last_error.empty() ? "TLS handshake failed"
                                                   : sock_.last_error);
                return false;
            }
            set_state(ConnState::Connecting, "Opening secure stream…");
            if (!open_stream(true)) {
                set_state(ConnState::Error,
                          sock_.last_error.empty() ? "TLS stream failed"
                                                   : sock_.last_error);
                return false;
            }
        }
        if (mode_ == Mode::Register) return register_flow();
        set_state(ConnState::Connecting, "Signing on…");
        if (stream_buf_.find("PLAIN") == std::string::npos) {
            set_state(ConnState::Error, "Server does not offer PLAIN auth");
            return false;
        }
        stream_buf_.clear();
        if (!sasl_plain()) return false;
        stream_buf_.clear();
        if (!bind_session()) {
            set_state(ConnState::Error, "Resource bind failed");
            return false;
        }
        sock_.send_all(
            "<iq type='get' id='roster1'><query xmlns='jabber:iq:roster'/></iq>");
        {
            std::lock_guard<std::mutex> lock(mu);
            own_show = Show::Chat;
            show_ = Show::Chat;
            if (own_nick.empty()) own_nick = jid_node(jid);
        }
        publish_presence();
        request_vcard(); // self
        // disco for upload + conference service
        sock_.send_all("<iq type='get' id='disco1' to='" + xml_escape(host_) +
                       "'><query xmlns='http://jabber.org/protocol/disco#items'/></iq>");
        sock_.send_all("<iq type='get' id='disco2' to='" + xml_escape(host_) +
                       "'><query xmlns='http://jabber.org/protocol/disco#info'/></iq>");
        // Account disco — MAM usually advertises on the bare JID.
        sock_.send_all("<iq type='get' id='disco_mam' to='" + xml_escape(bare_jid(jid)) +
                       "'><query xmlns='http://jabber.org/protocol/disco#info'/></iq>");
        request_bookmarks();
        // XEP-0280 Message Carbons — mirror other resources into this session.
        sock_.send_all(
            "<iq type='set' id='carb1'><enable xmlns='urn:xmpp:carbons:2'/></iq>");
        set_state(ConnState::Online, "Signed on as " + jid);
        emit(make_event(ClientEvent::Identity));
        return true;
    }

    void run() {
        set_state(ConnState::Connecting, "Connecting to " + host_ + "…");
        if (!sock_.connect_tcp(host_, 5222)) {
            set_state(ConnState::Error,
                      sock_.last_error.empty()
                          ? ("Could not reach " + host_ +
                             ". Check your network, or pick another public server.")
                          : sock_.last_error);
            return;
        }
        stream_buf_.clear();
        if (!auth_flow_from_tcp()) return;

        // Event loop
        while (!stop_) {
            flush_send();
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock_.sock, &rfds);
            timeval tv{0, 200000};
            int sel = select(0, &rfds, nullptr, nullptr, &tv);
            if (sel > 0 && FD_ISSET(sock_.sock, &rfds)) {
                char buf[4096];
                int n = sock_.recv_some(buf, sizeof(buf));
                if (n <= 0) {
                    set_state(ConnState::Disconnected, "Disconnected");
                    break;
                }
                stream_buf_.append(buf, buf + n);
                pump_incoming();
            } else {
                flush_send();
            }
        }
        sock_.close();
    }
};

} // namespace jabber
