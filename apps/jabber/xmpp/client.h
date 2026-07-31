// Minimal XMPP client: register (XEP-0077 + CAPTCHA), roster, chat, MUC, HTTP Upload.
#pragma once
#include "http_win.h"
#include "image_dec.h"
#include "socket_tls.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <thread>

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

struct ChatLine {
    std::string from;
    std::string body;
    bool mine = false;
    bool file = false;
    bool system = false; // subject / join notices
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
        Identity
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
    std::map<std::string, std::vector<ChatLine>> chats;
    std::map<std::string, std::vector<std::string>> muc_occupants;
    std::map<std::string, std::string> muc_nicks;     // room → our nick
    std::map<std::string, std::string> muc_subjects;  // room → subject
    std::set<std::string> muc_joined;                 // rooms we have joined
    std::string conference_host;                      // e.g. conference.yax.im
    std::vector<MucRoomInfo> muc_rooms;               // public disco list
    std::vector<MucBookmark> muc_bookmarks;
    SkinImage captcha_image;
    std::string captcha_sid;
    std::string captcha_form_type;
    std::vector<std::pair<std::string, std::string>> register_fields; // var, label
    std::string last_error;
    std::string http_upload_host;
    bool upload_available = false;

    // Own identity (Yahoo-shaped strip): presence + status + vCard.
    Show own_show = Show::Unavailable;
    std::string own_status;
    std::string own_nick;
    SkinImage own_avatar;

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

    void send_message(const std::string &to, const std::string &body) {
        std::string stanza =
            "<message to='" + xml_escape(to) + "' type='chat'><body>" +
            xml_escape(body) + "</body></message>";
        {
            std::lock_guard<std::mutex> lock(mu);
            chats[bare_jid(to)].push_back({jid, body, true, false});
        }
        queue_send(stanza);
    }

    void send_muc_message(const std::string &room, const std::string &body) {
        std::string stanza =
            "<message to='" + xml_escape(room) + "' type='groupchat'><body>" +
            xml_escape(body) + "</body></message>";
        {
            std::lock_guard<std::mutex> lock(mu);
            chats[bare_jid(room)].push_back({jid, body, true, false, false});
        }
        queue_send(stanza);
    }

    void join_muc(const std::string &room_in, const std::string &nick_in) {
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
        }
        queue_send("<presence to='" + xml_escape(to) +
                   "'><x xmlns='http://jabber.org/protocol/muc'/></presence>");
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
        bool self = bare.empty() || to == bare_jid(jid);
        {
            std::lock_guard<std::mutex> lock(mu);
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

    void add_buddy(const std::string &buddy) {
        queue_send("<iq type='set' id='roster_add'><query xmlns='jabber:iq:roster'>"
                   "<item jid='" +
                   xml_escape(buddy) + "'/></query></iq>");
        queue_send("<presence to='" + xml_escape(buddy) + "' type='subscribe'/>");
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
    std::string pending_upload_to_, pending_upload_mime_, pending_upload_name_;
    std::vector<uint8_t> pending_upload_data_;
    std::set<std::string> vcard_inflight_;
    std::queue<std::string> vcard_queue_;

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
        {
            std::lock_guard<std::mutex> lock(mu);
            s = own_show;
            st = own_status;
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
        if (!binval.empty()) {
            std::vector<uint8_t> bytes;
            // Strip whitespace from base64
            std::string cleaned;
            cleaned.reserve(binval.size());
            for (char c : binval)
                if (c != ' ' && c != '\n' && c != '\r' && c != '\t') cleaned.push_back(c);
            if (b64_decode(cleaned, &bytes)) decode_image_vec(bytes, avatar);
        }
        bool self = bare.empty() || bare_jid(bare) == bare_jid(jid);
        {
            std::lock_guard<std::mutex> lock(mu);
            std::string key = self ? bare_jid(jid) : bare_jid(bare);
            vcard_inflight_.erase(key);
            if (self) vcard_inflight_.erase(jid);
            if (self) {
                if (!nick.empty()) own_nick = nick;
                else if (!fn.empty()) own_nick = fn;
                if (!avatar.empty()) own_avatar = std::move(avatar);
            } else if (roster.count(key)) {
                if (!nick.empty()) roster[key].name = nick;
                else if (!fn.empty() &&
                         (roster[key].name.empty() || roster[key].name == jid_node(key)))
                    roster[key].name = fn;
                if (!avatar.empty()) roster[key].avatar = std::move(avatar);
                roster[key].vcard_fetched = true;
            }
        }
        emit(make_event(ClientEvent::Identity, {}, self ? bare_jid(jid) : bare_jid(bare)));
        if (!self) emit(make_event(ClientEvent::Roster));
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

    void pump_incoming() {
        // Pull complete top-level stanzas from stream_buf_ (naive).
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

    void handle_stanza(const std::string &st) {
        if (st.find("<message") == 0) {
            std::string from = attr(st, "from");
            std::string type = attr(st, "type");
            std::string body, subject;
            extract_tag(st, "body", &body);
            extract_tag(st, "subject", &subject);
            body = xml_unescape(body);
            subject = xml_unescape(subject);
            std::string key = bare_jid(from);
            bool is_muc = (type == "groupchat");
            if (!is_muc) {
                std::lock_guard<std::mutex> lock(mu);
                is_muc = muc_joined.count(key) != 0;
            }
            if (!subject.empty()) {
                {
                    std::lock_guard<std::mutex> lock(mu);
                    muc_subjects[key] = subject;
                    chats[key].push_back(
                        {"", "Topic: " + subject, false, false, true});
                }
                emit(make_event(ClientEvent::MucSubject, subject, key));
            }
            if (body.empty()) return;
            // Prefer occupant nick for MUC lines.
            std::string who = is_muc ? jid_resource(from) : from;
            if (who.empty()) who = from;
            {
                std::lock_guard<std::mutex> lock(mu);
                // Skip echo of our own groupchat if we already appended locally.
                if (is_muc && type == "groupchat") {
                    auto nit = muc_nicks.find(key);
                    if (nit != muc_nicks.end() && who == nit->second) {
                        auto &lines = chats[key];
                        if (!lines.empty() && lines.back().mine &&
                            lines.back().body == body)
                            return;
                    }
                }
                chats[key].push_back({who, body, false, false, false});
            }
            emit(make_event(ClientEvent::Message, body, key));
            return;
        }
        if (st.find("<presence") == 0) {
            std::string from = attr(st, "from");
            std::string type = attr(st, "type");
            std::string show_s, status_s;
            extract_tag(st, "show", &show_s);
            extract_tag(st, "status", &status_s);
            Show sh = Show::Chat;
            if (type == "unavailable") sh = Show::Unavailable;
            else if (show_s == "away") sh = Show::Away;
            else if (show_s == "xa") sh = Show::Xa;
            else if (show_s == "dnd") sh = Show::Dnd;
            std::string bare = bare_jid(from);
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
                    auto &occ = muc_occupants[bare];
                    if (type == "unavailable") {
                        occ.erase(std::remove(occ.begin(), occ.end(), nick), occ.end());
                    } else if (std::find(occ.begin(), occ.end(), nick) == occ.end()) {
                        occ.push_back(nick);
                    }
                }
            }
            emit(make_event(ClientEvent::Presence, status_s, bare));
            if (muc) emit(make_event(ClientEvent::MucOccupants, {}, bare));
            return;
        }
        if (st.find("<iq") == 0) {
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
            if (st.find("jabber:iq:register") != std::string::npos) {
                // handled in register_flow synchronously via stream_buf
            }
            if (st.find("vcard-temp") != std::string::npos ||
                st.find("<vCard") != std::string::npos) {
                std::string from = attr(st, "from");
                std::string bare = from.empty() ? bare_jid(jid) : bare_jid(from);
                apply_vcard(bare, st);
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
                Buddy b;
                b.jid = attr(tag, "jid");
                b.name = attr(tag, "name");
                if (b.name.empty()) b.name = jid_node(b.jid);
                std::string sub = attr(tag, "subscription");
                b.subscription_to = (sub == "both" || sub == "to");
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
                pos = end + 1;
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
        request_bookmarks();
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
