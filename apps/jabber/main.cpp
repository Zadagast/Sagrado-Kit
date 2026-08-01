// Sagrado Jabber — “You’ve Got Mail” IM on the Appearance Engine.
// Buddy list, presence, tabbed chats, Get an Account (XEP-0077 + CAPTCHA in gel).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../engine/appearance.h"
#include "../../engine/clipboard.h"
#include "../../engine/context_menu.h"
#include "../../engine/emoji_picker.h"
#include "../../engine/gel_host.h"
#include "../../engine/hfnt.h"
#include "../../engine/skin_catalog.h"
#include "../../engine/text_field.h"
#include "../../engine/utf8_win.h"
#include "../../engine/window_zoom.h"
#include "xmpp/client.h"

namespace {

constexpr int kWinW = 860;
constexpr int kWinH = 560;
constexpr int kMinWinW = 420;
constexpr int kMinWinH = 280;
constexpr int kStatusH = 22;
constexpr int kRosterW = 220;
constexpr int kIdentityH = 64;
constexpr int kAvatarSz = 40;
constexpr int kTabH = 22;
constexpr int kComposeH = 72;
constexpr int kTextPad = 4;
constexpr int kBuddyRowH = 36;
constexpr int kGroupHeaderH = 20;
constexpr UINT_PTR kTypingTimerId = 1;
constexpr UINT_PTR kStatusFlashTimerId = 2;
constexpr UINT_PTR kCaretTimerId = 3;
constexpr UINT kTypingPauseMs = 2000;
constexpr UINT kStatusFlashMs = 3500;
constexpr int kMaxRecentAccounts = 8; // JIDs only — never passwords

enum CtxKind : int { CtxNone = 0, CtxCompose, CtxTranscript };

enum : UINT {
    WM_JABBER_EVENT = WM_APP + 40,
    WM_TRAYICON = WM_APP + 41,
};
constexpr UINT kTrayId = 1;

static const char *kMenuTitles[] = {"File", "Buddy", "Chat", "Appearance", "Help"};
enum MenuId : int {
    MenuFile = 0,
    MenuBuddy,
    MenuChat,
    MenuAppearance,
    MenuHelp,
    MenuCount,
    MenuWindow = 100,
};
static const char *kFileItems[] = {
    "Sign On...", "Get an Account...", "Sign Off", "-", "Quit",
};
static const char *kBuddyItems[] = {
    "Add Buddy...", "Remove Buddy...", "Block Buddy", "Unblock Buddy",
    "Set Picture...", "-",
    "Available", "Away", "Busy", "Invisible",
};
static const char *kChatItems[] = {
    "Send File...",
    "React...",
    "Retract Message",
    "Browse Chat Rooms...",
    "Join Chat Room...",
    "-",
    "Set Topic...",
    "Invite...",
    "Leave Room",
    "Bookmark Room",
    "Autojoin Room",
};
static const char *kHelpItems[] = {
    "About Sagrado Jabber",
};
static const char *kWindowItems[] = {"Minimize", "Zoom", "-", "Close"};

struct MenuDef {
    const char *const *items;
    int count;
};
// Appearance is rebuilt from bundled skins (see rebuild_appearance_menu).
static MenuDef kMenus[MenuCount] = {
    {kFileItems, 5}, {kBuddyItems, 10}, {kChatItems, 11},
    {nullptr, 0}, {kHelpItems, 1},
};

enum Drag : int {
    DragNone = 0,
    DragClose,
    DragMax,
    DragMin,
    DragSize,
    DragMenuBar,
    DragThumbChat,
    DragThumbRoster,
    DragThumbBrowse,
    DragThumbProvider,
    DragThumbRecent,
    DragArrowChat,
    DragArrowRoster,
    DragArrowBrowse,
    DragArrowProvider,
    DragArrowRecent,
};
enum SizeEdge : int {
    SizeLeft = 1,
    SizeRight = 2,
    SizeTop = 4,
    SizeBottom = 8,
};

enum DialogKind {
    DlgNone = 0,
    DlgSignOn,
    DlgRegister,
    DlgAddBuddy,
    DlgJoinMuc,
    DlgBrowseMuc,
    DlgSetTopic,
    DlgInvite,
};

struct Tab {
    std::string jid;
    bool muc = false;
};

struct App {
    HWND hwnd = nullptr;
    HINSTANCE hinst = nullptr;
    Canvas canvas;
    Appearance ap;
    GelLayout gel{};
    MenuBarLayout menu_bar{};
    MenuLayout popup{};
    jabber::Client client;

    bool focused = true;
    int pressed_box = 0;
    int drag = DragNone;
    int size_edge = 0;
    int size_anchor_x = 0, size_anchor_y = 0;
    int size_orig_x = 0, size_orig_y = 0, size_orig_w = 0, size_orig_h = 0;
    int menu_hot = -1, menu_open = -1, menu_item_hot = -1;

    std::string status = "Signed off — File → Sign On or Get an Account";
    DWORD status_flash_at = 0; // GetTickCount; 0 = show durable bar only
    std::vector<Tab> tabs;
    int active_tab = -1;
    int roster_hot = -1; // index into roster_rows_
    int roster_scroll = 0; // pixels
    int chat_scroll = 0;   // pixels
    int chat_sel = -1;     // selected transcript line (for React…)
    // MAM scrollbookkeeping (applied on next transcript layout).
    bool mam_scroll_to_end = false;
    int mam_pending_prepend = 0; // older page: N lines just prepended
    // Subscribe ask (Accept / Deny) — gel sheet, not MessageBox.
    bool sub_ask_open = false;
    std::string sub_ask_jid;
    Rect sub_accept_r{}, sub_deny_r{};
    // MUC invite ask (Accept / Decline) — same gel pattern as buddy request.
    bool muc_invite_open = false;
    std::string muc_invite_room;
    std::string muc_invite_from;
    std::string muc_invite_reason;
    Rect muc_invite_accept_r{}, muc_invite_decline_r{};
    // XEP-0085 outbound composing for the active 1:1 tab.
    bool typing_sent = false;
    std::string typing_peer; // bare JID we last sent composing to
    // Cached peer “is typing…” for the active tab (from client.chat_states).
    std::string peer_typing;
    int thumb_grab = 0;
    ScrollArrowHot arrow_hot = ScrollArrowHot::None;
    int arrow_dir = 0;
    // Cached in paint for scrollbar hit-testing (pixels).
    int chat_page = 1, chat_max = 0;
    int roster_page = 1, roster_max = 0;
    int browse_page = 1, browse_max = 0;
    int provider_page = 1, provider_max = 0;
    int recent_page = 1, recent_max = 0;

    DialogKind dialog = DlgNone;
    std::string field_jid;
    std::string field_pass;
    std::string field_server = "yax.im";
    std::string field_user;
    std::string field_captcha;
    std::string field_buddy;
    std::string field_room;
    std::string field_nick;
    std::string field_room_pass;
    std::string field_room_search; // XEP-0433 query
    std::string field_topic;
    std::string field_invite;
    std::string field_invite_reason;
    std::string react_target_id; // XEP-0444 target for floating emoji host
    bool emoji_compose_mode = false; // picker inserts into compose (not react)
    sagrado::EmojiPickerState emoji_st{};
    sagrado::EmojiPickerLayout emoji_lay{};
    sagrado::GelHost emoji_host{};
    bool emoji_pack_ok = false;
    bool emoji_sbar_thumb = false;
    bool emoji_sbar_arrow = false;
    ScrollArrowHot emoji_sbar_ah = ScrollArrowHot::None;
    int focus_field = 0; // which dialog field
    bool captcha_visible = false;
    bool about_open = false;
    bool about_ok_pressed = false;
    AlertLayout about_lay{};
    int file_progress = -1; // -1 hidden, else 0..100

    struct Provider {
        std::string host;
        std::string name;
        std::string notes;
    };
    std::vector<Provider> providers;
    int provider_sel = 0;
    int provider_scroll = 0; // pixels
    Rect provider_list_r{}, provider_sbar{};

    // Recent account names (JIDs). Passwords are never stored.
    std::vector<std::string> recent_jids;
    int recent_sel = -1;
    int recent_scroll = 0; // pixels
    Rect recent_list_r{}, recent_sbar{};

    // Where each dialog text field landed this paint, so a click focuses the
    // field under the cursor instead of just cycling.
    std::vector<std::pair<int, Rect>> dlg_field_rs;
    // Browse Chat Rooms dialog
    int browse_sel = -1; // index into browse_rows
    int browse_scroll = 0; // pixels
    Rect browse_list_r{}, browse_sbar{};
    struct BrowseRow {
        std::string jid;
        std::string label;
        bool bookmark = false;
        bool autojoin = false;
        bool section = false; // non-selectable header
    };
    std::vector<BrowseRow> browse_rows;

    TextFieldState compose{};
    Rect compose_field_r{};
    ContextMenuState ctx{};
    int ctx_kind = CtxNone;
    std::string status_msg; // own presence <status> draft (identity field)
    std::string correct_id; // XEP-0308 — id of own message being edited
    std::string correct_jid; // chat the edit belongs to
    bool status_field_focus = false;
    bool presence_menu = false; // popup anchored to identity strip

    Rect identity_r{}, avatar_r{}, presence_r{}, status_field_r{};
    Rect roster_r{}, roster_sbar{}, tabs_r{}, transcript_r{}, chat_sbar{};
    Rect compose_r{}, status_r{};
    Rect btn_send{}, btn_compose_emoji{}, btn_compose_attach{};
    Rect occ_r{}, progress_r{};

    bool tray_added = false;
    bool in_tray = false; // window hidden; live in the notification area
    sagrado::WindowZoomState zoom{};

    // Appearance menu: bundled skins + Load… / Stock (scroll when tall).
    std::vector<sagrado::BundledSkin> bundled_skins;
    std::vector<std::string> appearance_labels;
    std::vector<const char *> appearance_ptrs;
    int appearance_scroll = 0; // first visible row when menu is clipped
};

App g;

bool pick_react_target(std::string *react_id_out);
void retract_selected_message();
void open_react_dialog();
void open_compose_emoji();
void pick_and_send_file();

std::string exe_dir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p = buf;
    auto slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : p.substr(0, slash);
}

bool file_exists(const std::string &p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void rebuild_appearance_menu() {
    g.bundled_skins = sagrado::list_bundled_skins(exe_dir());
    g.appearance_labels.clear();
    g.appearance_labels.reserve(g.bundled_skins.size() + 4);
    for (const auto &s : g.bundled_skins)
        g.appearance_labels.push_back(s.name);
    if (!g.bundled_skins.empty()) g.appearance_labels.push_back("-");
    g.appearance_labels.push_back("Load Appearance...");
    g.appearance_labels.push_back("Stock Appearance");
    g.appearance_ptrs.clear();
    g.appearance_ptrs.reserve(g.appearance_labels.size());
    for (const auto &lab : g.appearance_labels)
        g.appearance_ptrs.push_back(lab.c_str());
    kMenus[MenuAppearance] = {g.appearance_ptrs.data(),
                              (int)g.appearance_ptrs.size()};
    g.appearance_scroll = 0;
}

std::string find_default_skin() {
    return sagrado::find_default_bundled_skin(exe_dir());
}

void tray_update_tip();
void tray_balloon(const std::string &title, const std::string &body);
void open_sign_on();

std::string emoji_recent_path() { return exe_dir() + "\\emoji_recent.txt"; }

bool is_emoji_lead(unsigned cp) {
    return (cp >= 0x1f000 && cp <= 0x1faff) ||
           (cp >= 0x1f1e6 && cp <= 0x1f1ff) ||
           (cp >= 0x2600 && cp <= 0x27bf) ||
           (cp >= 0x2b00 && cp <= 0x2bff) ||
           (cp >= 0x2190 && cp <= 0x21ff) ||
           (cp >= 0x2300 && cp <= 0x23ff) || cp == 0x24c2 ||
           (cp >= 0x25aa && cp <= 0x25ff) ||
           (cp >= 0x2934 && cp <= 0x2935) || cp == 0x3030 ||
           cp == 0x303d || cp == 0x3297 || cp == 0x3299 || cp == 0x203c ||
           cp == 0x2049 || cp == 0x2122 || cp == 0x2139 || cp == 0x20e3 ||
           cp == 0x2764 || cp == 0xfe0f;
}

bool jabber_emoji_probe(const char *s, int px, int *out_len,
                        const SkinImage **out_img) {
    if (!sagrado::emoji_pack_ready() || !s || !*s) return false;
    const char *first_end = s;
    unsigned first = fontutil::next_cp(first_end);
    if (first < 0x80 || !is_emoji_lead(first)) return false;

    const char *end = first_end;
    while (*end) {
        const char *next = end;
        unsigned cp = fontutil::next_cp(next);
        if (cp == 0xfe0f || cp == 0x1f3fb || cp == 0x1f3fc ||
            cp == 0x1f3fd || cp == 0x1f3fe || cp == 0x1f3ff ||
            cp == 0x20e3 || (cp >= 0x1f1e6 && cp <= 0x1f1ff)) {
            end = next;
            continue;
        }
        if (cp == 0x200d) {
            end = next;
            if (*end) {
                const char *joined = end;
                fontutil::next_cp(joined);
                end = joined;
            }
            continue;
        }
        break;
    }

    int matched_len = int(end - s);
    const SkinImage *icon =
        sagrado::emoji_icon(std::string(s, size_t(matched_len)), px);
    if (!icon) {
        matched_len = int(first_end - s);
        icon = sagrado::emoji_icon(std::string(s, size_t(matched_len)), px);
    }
    if (!icon) return false;
    if (out_len) *out_len = matched_len;
    if (out_img) *out_img = icon;
    return true;
}

void load_emoji_recent() {
    g.emoji_st.recent.clear();
    std::ifstream in(emoji_recent_path());
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (!line.empty()) g.emoji_st.recent.push_back(line);
        if (g.emoji_st.recent.size() >= 48) break;
    }
}

void save_emoji_recent() {
    std::ofstream out(emoji_recent_path(), std::ios::trunc);
    for (const auto &w : g.emoji_st.recent) out << w << "\n";
}

void load_emoji_pack() {
    std::string base = exe_dir();
    const char *cands[] = {
        "\\emoji_pack",
        "\\..\\build\\emoji_pack",
        "\\..\\..\\build\\emoji_pack",
    };
    g.emoji_pack_ok = false;
    for (const char *c : cands) {
        if (sagrado::emoji_pack_set_root(base + c)) {
            g.emoji_pack_ok = true;
            break;
        }
    }
    set_kit_emoji_probe(&jabber_emoji_probe);
    load_emoji_recent();
}

// Gajim-like transcript rows: avatar + colored nick + time + body + reaction pills.
constexpr int kMsgAvatar = 32;
constexpr int kMsgGap = 12;     // between different senders
constexpr int kMsgGapCont = 4;  // continuation from same sender
constexpr int kMsgPadX = 12;
constexpr int kMsgAvatarGap = 10;
constexpr int kReactGlyph = 22;
constexpr int kReactPillPad = 4;
constexpr int kReactPillGap = 6;

// Height of the reaction pills row under a chat line.
int reaction_row_height(const jabber::ChatLine &ln, int lh) {
    if (ln.reactions.empty()) return 0;
    if (g.emoji_pack_ok) return std::max(lh + 4, 30);
    return lh + 4;
}

std::string chat_display_name(const jabber::ChatLine &ln, bool muc) {
    if (ln.system) return {};
    if (ln.mine) return "You";
    if (muc) return ln.from.empty() ? "?" : ln.from;
    return jabber::jid_node(ln.from).empty() ? ln.from : jabber::jid_node(ln.from);
}

// Stable color-key for XEP-0392 (own bare JID for "You").
std::string chat_color_key(const jabber::ChatLine &ln, bool muc) {
    if (ln.system) return {};
    if (ln.mine) {
        std::string bare = jabber::bare_jid(g.client.jid);
        return bare.empty() ? std::string("You") : bare;
    }
    if (muc) return ln.from.empty() ? "?" : ln.from;
    std::string bare = jabber::bare_jid(ln.from);
    return bare.empty() ? ln.from : bare;
}

std::string chat_initials(const std::string &name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            if (out.size() >= 2) break;
        }
    }
    return out.empty() ? "?" : out;
}

bool chat_same_sender(const jabber::ChatLine &a, const jabber::ChatLine &b, bool muc) {
    if (a.system || b.system || a.file != b.file) return false;
    if (a.mine != b.mine) return false;
    if (a.mine) return true;
    if (muc) return a.from == b.from;
    return jabber::jid_ieq(jabber::bare_jid(a.from), jabber::bare_jid(b.from));
}

// XEP-0392: SHA-1 → hue angle; HSL→RGB with lightness adapted to transcript bg.
Color xep0392_color(const std::string &id, Color bg) {
    if (id.empty()) return {160, 160, 160};
    unsigned char dig[20];
    if (!jabber::sha1_digest(reinterpret_cast<const uint8_t *>(id.data()), id.size(),
                             dig))
        return {160, 160, 160};
    // Little-endian least-significant 16 bits (first two bytes).
    unsigned v = unsigned(dig[0]) | (unsigned(dig[1]) << 8);
    double angle = (double(v) / 65536.0) * 360.0;
    double lum = (0.2126 * bg.r + 0.7152 * bg.g + 0.0722 * bg.b) / 255.0;
    double L = lum < 0.45 ? 0.72 : 0.38; // light on dark / dark on light
    double S = 0.70;
    double C = (1.0 - std::fabs(2.0 * L - 1.0)) * S;
    double Hp = angle / 60.0;
    double X = C * (1.0 - std::fabs(std::fmod(Hp, 2.0) - 1.0));
    double r1 = 0, g1 = 0, b1 = 0;
    if (Hp < 1) {
        r1 = C;
        g1 = X;
    } else if (Hp < 2) {
        r1 = X;
        g1 = C;
    } else if (Hp < 3) {
        g1 = C;
        b1 = X;
    } else if (Hp < 4) {
        g1 = X;
        b1 = C;
    } else if (Hp < 5) {
        r1 = X;
        b1 = C;
    } else {
        r1 = C;
        b1 = X;
    }
    double m = L - C / 2.0;
    auto ch = [&](double v) -> uint8_t {
        int n = int(std::lround((v + m) * 255.0));
        return uint8_t(std::clamp(n, 0, 255));
    };
    return {ch(r1), ch(g1), ch(b1)};
}

// XEP-0245 — "/me waves" renders as an action line: "* waves".
bool chat_is_action(const jabber::ChatLine &ln) {
    return !ln.system && ln.body.rfind("/me ", 0) == 0;
}

// XEP-0393 — message styling spans (*bold* _italic_ ~strike~ `mono`).
// Directive characters stay visible (allowed by the XEP); spans never cross
// newlines and require non-space chars just inside the directives.
struct StyleSpan {
    size_t start, end; // inclusive directive positions
    char kind;         // '*', '_', '~', '`'
};

std::vector<StyleSpan> styling_spans(const std::string &s) {
    std::vector<StyleSpan> spans;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c != '*' && c != '_' && c != '~' && c != '`') continue;
        if (i + 2 >= s.size() || s[i + 1] == c ||
            std::isspace((unsigned char)s[i + 1]))
            continue;
        size_t j = s.find(c, i + 1);
        while (j != std::string::npos &&
               std::isspace((unsigned char)s[j - 1]))
            j = s.find(c, j + 1);
        if (j == std::string::npos) continue;
        if (s.find('\n', i) < j) continue;
        spans.push_back({i, j, c});
        i = j;
    }
    return spans;
}

// Paint one wrapped visual line, applying any styling spans that overlap it.
void paint_styled_text(Canvas &cv, int x, int y, const std::string &body,
                       size_t start, size_t len,
                       const std::vector<StyleSpan> &spans, Color ink,
                       Color dim, Color mono_bg) {
    if (spans.empty()) {
        cv.text(x, y, body.substr(start, len).c_str(), ink);
        return;
    }
    int lh = cv.line_height();
    size_t pos = start, end = start + len;
    while (pos < end) {
        const StyleSpan *sp = nullptr;
        size_t next = end;
        for (const auto &s : spans) {
            if (s.end < pos || s.start >= end) continue;
            if (s.start <= pos && pos <= s.end) {
                sp = &s;
                break;
            }
            if (s.start > pos && s.start < next) next = s.start;
        }
        size_t run_end = sp ? std::min(sp->end + 1, end) : next;
        std::string run = body.substr(pos, run_end - pos);
        if (sp && sp->kind == '`') {
            int w = cv.text_width(run.c_str());
            cv.fill({x, y, w, lh}, mono_bg);
        }
        int nx = cv.text(x, y, run.c_str(), sp && sp->kind == '_' ? dim : ink);
        if (sp && sp->kind == '*') cv.text(x + 1, y, run.c_str(), ink);
        if (sp && sp->kind == '~') cv.hline(x, nx, y + lh / 2, ink);
        x = sp && sp->kind == '*' ? nx + 1 : nx;
        pos = run_end;
    }
}

std::string chat_body_text(const jabber::ChatLine &ln) {
    // XEP-0424/0425 — a retracted message keeps its slot, never its content.
    if (ln.retracted) {
        std::string b = ln.retracted_by.empty()
                            ? std::string("[message retracted]")
                            : "[message retracted by " + ln.retracted_by + "]";
        if (!ln.retract_reason.empty()) b += " — " + ln.retract_reason;
        return b;
    }
    std::string b = chat_is_action(ln) ? "*" + ln.body.substr(3) : ln.body;
    if (ln.edited) b += " (edited)";
    return b;
}

struct InlineImageEntry {
    int state = 0; // pending, ready, failed
    SkinImage img;
};

std::mutex g_inline_image_mu;
std::condition_variable g_inline_image_cv;
std::map<std::string, InlineImageEntry> g_inline_images;
int g_inline_image_workers = 0;
constexpr int kMaxInlineImageWorkers = 2;
constexpr int kInlineImageMax = 256;

bool inline_image_url(const std::string &url) {
    size_t end = url.find_first_of("?#");
    std::string path = url.substr(0, end);
    std::string lower = path;
    for (char &c : lower) c = (char)std::tolower((unsigned char)c);
    static const char *kExts[] = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp"};
    for (const char *ext : kExts) {
        size_t n = std::strlen(ext);
        if (lower.size() >= n &&
            lower.compare(lower.size() - n, n, ext) == 0)
            return true;
    }
    return false;
}

SkinImage scale_inline_image(const SkinImage &src) {
    SkinImage out;
    if (src.empty()) return out;
    int side = std::max(src.w, src.h);
    int scale = std::min(side, kInlineImageMax);
    int w = std::max(1, src.w * scale / side);
    int h = std::max(1, src.h * scale / side);
    out.w = w;
    out.h = h;
    out.px.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.px[size_t(y) * size_t(w) + size_t(x)] =
                src.at(x * src.w / w, y * src.h / h);
    return out;
}

void redraw();

void inline_image_worker(const std::string &url) {
    {
        std::unique_lock<std::mutex> lock(g_inline_image_mu);
        g_inline_image_cv.wait(lock, [] {
            return g_inline_image_workers < kMaxInlineImageWorkers;
        });
        ++g_inline_image_workers;
    }

    SkinImage decoded;
    jabber::HttpResult result = jabber::http_get(url);
    bool ok = result.ok && jabber::decode_image_vec(result.body, decoded);
    if (ok) decoded = scale_inline_image(decoded);

    {
        std::lock_guard<std::mutex> lock(g_inline_image_mu);
        auto it = g_inline_images.find(url);
        if (it != g_inline_images.end()) {
            it->second.state = ok ? 2 : 3;
            if (ok) it->second.img = std::move(decoded);
        }
        --g_inline_image_workers;
    }
    g_inline_image_cv.notify_one();
    redraw();
}

void ensure_inline_image(const std::string &url) {
    if (!inline_image_url(url)) return;
    bool start = false;
    {
        std::lock_guard<std::mutex> lock(g_inline_image_mu);
        auto [it, inserted] = g_inline_images.emplace(url, InlineImageEntry{1, {}});
        start = inserted;
        (void)it;
    }
    if (start) std::thread(inline_image_worker, url).detach();
}

bool inline_image_snapshot(const std::string &url, SkinImage *img, int *state) {
    std::lock_guard<std::mutex> lock(g_inline_image_mu);
    auto it = g_inline_images.find(url);
    if (it == g_inline_images.end()) return false;
    if (state) *state = it->second.state;
    if (img && it->second.state == 2) *img = it->second.img;
    return true;
}

void inline_image_box(const SkinImage &img, int max_w, int *w, int *h) {
    if (!w || !h || img.empty()) return;
    int dw = std::min({kInlineImageMax, max_w, img.w});
    int dh = std::max(1, img.h * dw / img.w);
    *w = dw;
    *h = dh;
}

std::string format_hhmm(time_t when) {
    if (!when) return {};
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &when) != 0) return {};
#else
    if (!localtime_r(&when, &local)) return {};
#endif
    char buf[8];
    if (std::strftime(buf, sizeof(buf), "%H:%M", &local) == 0) return {};
    return buf;
}

// Body wrap width — avatar gutter kept for headed and continuation rows.
int chat_body_wrap(int transcript_w, int pad, bool /*show_header*/) {
    return std::max(8, transcript_w - 2 * pad - kMsgAvatar - kMsgAvatarGap);
}

int chat_block_height(Canvas &cv, const jabber::ChatLine &ln, bool show_header,
                      int body_wrap) {
    const int lh = cv.line_height();
    if (ln.system) {
        int h = text_content_height(
            layout_lines(cv, ln.body, body_wrap + kMsgAvatar + kMsgAvatarGap, true),
            lh);
        return h + kMsgGapCont;
    }
    int h = 0;
    if (show_header) h += lh + 2; // nick + time row
    int image_state = 0;
    SkinImage image;
    bool image_cached = ln.file && inline_image_snapshot(ln.body, &image, &image_state);
    int image_w = 0, image_h = 0;
    if (image_cached && image_state == 2)
        inline_image_box(image, body_wrap, &image_w, &image_h);
    if (image_h > 0)
        h += image_h + 4;
    else
        h += text_content_height(layout_lines(cv, chat_body_text(ln), body_wrap, true),
                                 lh);
    h += reaction_row_height(ln, lh);
    if (ln.mine && ln.delivered) h += lh; // "Delivered" meta line
    if (show_header) h = std::max(h, kMsgAvatar);
    h += show_header ? kMsgGap : kMsgGapCont;
    return h;
}

bool same_local_day(time_t a, time_t b) {
    if (!a || !b) return false;
    std::tm aa{}, bb{};
#ifdef _WIN32
    if (localtime_s(&aa, &a) != 0 || localtime_s(&bb, &b) != 0) return false;
#else
    if (!localtime_r(&a, &aa) || !localtime_r(&b, &bb)) return false;
#endif
    return aa.tm_year == bb.tm_year && aa.tm_yday == bb.tm_yday;
}

int date_sep_h(const std::vector<jabber::ChatLine> &lines, int i, int lh) {
    if (i < 0 || i >= (int)lines.size() || !lines[size_t(i)].when) return 0;
    if (i > 0 && (!lines[size_t(i - 1)].when ||
                  same_local_day(lines[size_t(i)].when, lines[size_t(i - 1)].when)))
        return 0;
    return lh + 10;
}

std::string date_sep_label(time_t when) {
    if (!when) return {};
    std::time_t now = std::time(nullptr);
    std::tm d{}, n{};
#ifdef _WIN32
    if (localtime_s(&d, &when) != 0 || localtime_s(&n, &now) != 0) return {};
#else
    if (!localtime_r(&when, &d) || !localtime_r(&now, &n)) return {};
#endif
    if (d.tm_year == n.tm_year && d.tm_yday == n.tm_yday) return "Today";
    std::time_t yesterday = now - 24 * 60 * 60;
    std::tm y{};
#ifdef _WIN32
    if (localtime_s(&y, &yesterday) == 0 && d.tm_year == y.tm_year &&
        d.tm_yday == y.tm_yday)
#else
    if (localtime_r(&yesterday, &y) && d.tm_year == y.tm_year &&
        d.tm_yday == y.tm_yday)
#endif
        return "Yesterday";
    char buf[64] = {};
    if (std::strftime(buf, sizeof(buf), "%A, %B %d, %Y", &d) != 0) {
        char *day = std::strstr(buf, " 0");
        if (day) std::memmove(day + 1, day + 2, std::strlen(day + 2) + 1);
        return buf;
    }
    return {};
}

int full_block_h(Canvas &cv, const std::vector<jabber::ChatLine> &lines, int i,
                 bool muc, int transcript_w, int pad) {
    bool headed = lines[size_t(i)].system || i == 0 ||
                  !chat_same_sender(lines[size_t(i - 1)], lines[size_t(i)], muc);
    int bw = chat_body_wrap(transcript_w, pad, headed);
    return date_sep_h(lines, i, cv.line_height()) +
           chat_block_height(cv, lines[size_t(i)], headed, bw);
}

struct ReactPill {
    Rect r;
    std::string emoji;
};

// Layout reaction pills (optionally paint). Returns row height.
int layout_reaction_pills(Canvas &cv, const Appearance &ap, int x, int y,
                          const jabber::ChatLine &ln, Color count_ink, Color bg,
                          bool paint, std::vector<ReactPill> *out) {
    if (ln.reactions.empty()) return 0;
    const int row_h = reaction_row_height(ln, cv.line_height());
    const int lh = cv.line_height();
    int px = x;
    for (const auto &rx : ln.reactions) {
        int glyph = g.emoji_pack_ok ? kReactGlyph : 0;
        int count_w = 0;
        std::string count_s;
        if (rx.count > 1) {
            count_s = std::to_string(rx.count);
            count_w = cv.text_width(count_s.c_str()) + 4;
        }
        int inner = (glyph > 0 ? glyph : cv.text_width("+")) + count_w;
        int pill_w = inner + kReactPillPad * 2 + 4;
        Rect pill{px, y, pill_w, row_h - 2};
        if (out) out->push_back({pill, rx.emoji});
        if (paint) {
            Color fill = rx.mine ? ap.c("list.hilite_background") : ap.c("list.background");
            cv.fill(pill, fill);
            rounded_frame(cv, pill, ap.c("list.separator"), bg);
            int ix = pill.x + kReactPillPad;
            if (glyph > 0) {
                const SkinImage *ic = sagrado::emoji_icon(rx.emoji, 32);
                if (ic && !ic->empty()) {
                    int iy = pill.y + (pill.h - glyph) / 2;
                    cv.blit_image_scaled(*ic, ix, iy, glyph, glyph);
                    ix += glyph + 2;
                }
            }
            if (!count_s.empty()) {
                Color ink = rx.mine ? ap.c("list.hilite_foreground") : count_ink;
                cv.text(ix, pill.y + (pill.h - lh) / 2, count_s.c_str(), ink);
            }
        }
        px += pill_w + kReactPillGap;
    }
    return row_h;
}

int paint_reaction_row(Canvas &cv, int x, int y, const jabber::ChatLine &ln,
                       Color count_ink, Color bg) {
    return layout_reaction_pills(cv, g.ap, x, y, ln, count_ink, bg, true, nullptr);
}
void redraw();
void stop_typing_indicator();
void open_subscribe_ask(const std::string &jid);
void close_subscribe_ask(bool accepted);
void maybe_show_next_muc_invite();
void open_muc_invite_ask(const std::string &room, const std::string &from,
                         const std::string &reason);
void close_muc_invite_ask(bool accepted);
const char *show_label(jabber::Show s);

std::string durable_status_text() {
    using jabber::ConnState;
    ConnState st = g.client.state;
    if (st == ConnState::Connecting || st == ConnState::Registering) {
        std::lock_guard<std::mutex> lock(g.client.mu);
        if (!g.client.status_text.empty()) return g.client.status_text;
        return st == ConnState::Registering ? "Creating account…" : "Signing on…";
    }
    if (st == ConnState::Error) {
        std::lock_guard<std::mutex> lock(g.client.mu);
        if (!g.client.last_error.empty()) return g.client.last_error;
        if (!g.client.status_text.empty()) return g.client.status_text;
        return "Connection error";
    }
    if (st != ConnState::Online) {
        if (!g.recent_jids.empty())
            return "Signed off — Enter password to join " + g.recent_jids[0];
        return "Signed off — File → Sign On or Get an Account";
    }

    std::string jid;
    jabber::Show show = jabber::Show::Chat;
    int online = 0, total = 0, occ_n = 0;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        jid = g.client.jid;
        show = g.client.own_show;
        for (const auto &kv : g.client.roster) {
            ++total;
            if (kv.second.show != jabber::Show::Unavailable) ++online;
        }
        if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size() &&
            g.tabs[g.active_tab].muc) {
            auto it = g.client.muc_occupants.find(g.tabs[g.active_tab].jid);
            if (it != g.client.muc_occupants.end())
                occ_n = (int)it->second.size();
        }
    }

    const char *presence = "Invisible";
    switch (show) {
    case jabber::Show::Chat: presence = "Available"; break;
    case jabber::Show::Away:
    case jabber::Show::Xa: presence = "Away"; break;
    case jabber::Show::Dnd: presence = "Busy"; break;
    default: break;
    }
    std::string s = "Signed on as " + jid;
    s += "  ·  ";
    s += presence;
    s += "  ·  ";
    s += std::to_string(online) + " of " + std::to_string(total) + " buddies online";
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size()) {
        const auto &tab = g.tabs[g.active_tab];
        if (tab.muc) {
            s += "  ·  ";
            s += jabber::jid_node(tab.jid);
            s += " (";
            s += std::to_string(occ_n);
            s += " in room)";
        } else {
            s += "  ·  Chat with ";
            s += jabber::jid_node(tab.jid);
        }
    }
    return s;
}

std::string status_bar_text() {
    if (g.status_flash_at != 0) {
        DWORD age = GetTickCount() - g.status_flash_at;
        if (age < kStatusFlashMs && !g.status.empty()) return g.status;
        g.status_flash_at = 0;
    }
    return durable_status_text();
}

// Brief alert in the status strip; durable account/roster line returns afterward.
void set_status(const std::string &s) {
    g.status = s;
    g.status_flash_at = GetTickCount();
    if (g.hwnd) SetTimer(g.hwnd, kStatusFlashTimerId, kStatusFlashMs, nullptr);
    tray_update_tip();
}

void tray_add() {
    if (g.tray_added || !g.hwnd) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g.hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    lstrcpynA(nid.szTip, "Sagrado Jabber", sizeof(nid.szTip));
    if (Shell_NotifyIconA(NIM_ADD, &nid)) g.tray_added = true;
}

void tray_remove() {
    if (!g.tray_added || !g.hwnd) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g.hwnd;
    nid.uID = kTrayId;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    g.tray_added = false;
}

void tray_update_tip() {
    if (!g.tray_added || !g.hwnd) return;
    std::string tip = "Sagrado Jabber";
    if (g.client.state == jabber::ConnState::Online && !g.client.jid.empty())
        tip = g.client.jid + " — Sagrado Jabber";
    else {
        std::string line = durable_status_text();
        tip = line.size() > 120 ? line.substr(0, 117) + "…" : line;
    }
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g.hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_TIP;
    lstrcpynA(nid.szTip, tip.c_str(), sizeof(nid.szTip));
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

void tray_balloon(const std::string &title, const std::string &body) {
    if (!g.tray_added || !g.hwnd) return;
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g.hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 8000;
    lstrcpynA(nid.szInfoTitle, title.c_str(), sizeof(nid.szInfoTitle));
    lstrcpynA(nid.szInfo, body.c_str(), sizeof(nid.szInfo));
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

void hide_to_tray() {
    if (!g.hwnd) return;
    if (!g.tray_added) tray_add();
    // Close any modal UI so a later restore is not stuck on Sign On / etc.
    if (g.captcha_visible) g.client.cancel_register_captcha();
    g.dialog = DlgNone;
    g.captcha_visible = false;
    g.about_open = false;
    g.sub_ask_open = false;
    g.sub_ask_jid.clear();
    g.menu_open = -1;
    g.presence_menu = false;
    stop_typing_indicator();
    ShowWindow(g.hwnd, SW_HIDE);
    g.in_tray = true;
    g.pressed_box = 0;
    g.drag = DragNone;
    tray_update_tip();
}

void show_from_tray() {
    if (!g.hwnd) return;
    ShowWindow(g.hwnd, SW_SHOW);
    ShowWindow(g.hwnd, SW_RESTORE);
    SetForegroundWindow(g.hwnd);
    g.in_tray = false;
    redraw();
}

void quit_app() {
    tray_remove();
    g.client.disconnect();
    PostQuitMessage(0);
}

void tray_popup_menu() {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuA(menu, MF_STRING, 1, "Open Sagrado Jabber");
    AppendMenuA(menu, MF_STRING, 2, "Sign On...");
    AppendMenuA(menu, MF_STRING, 3, "Sign Off");
    AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(menu, MF_STRING, 4, "Quit");
    SetForegroundWindow(g.hwnd);
    int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y,
                                  0, g.hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageA(g.hwnd, WM_NULL, 0, 0);
    if (cmd == 1) {
        show_from_tray();
    } else if (cmd == 2) {
        show_from_tray();
        open_sign_on();
        redraw();
    } else if (cmd == 3) {
        g.client.disconnect();
        set_status("Signed off");
        redraw();
    } else if (cmd == 4) {
        quit_app();
    }
}

std::string find_providers_path() {
    std::string base = exe_dir();
    const char *cands[] = {
        "\\providers.txt",
        "\\..\\apps\\jabber\\providers.txt",
        "\\..\\..\\apps\\jabber\\providers.txt",
    };
    for (const char *c : cands) {
        std::string p = base + c;
        if (file_exists(p)) return p;
    }
    return "apps/jabber/providers.txt";
}

void trim_inplace(std::string &s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i) s.erase(0, i);
}

void load_providers() {
    g.providers.clear();
    std::ifstream in(find_providers_path());
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto p1 = line.find('|');
            std::string host =
                p1 == std::string::npos ? line : line.substr(0, p1);
            std::string name, notes;
            if (p1 != std::string::npos) {
                auto p2 = line.find('|', p1 + 1);
                name = p2 == std::string::npos ? line.substr(p1 + 1)
                                               : line.substr(p1 + 1, p2 - p1 - 1);
                if (p2 != std::string::npos) notes = line.substr(p2 + 1);
            }
            trim_inplace(host);
            trim_inplace(name);
            trim_inplace(notes);
            if (host.empty()) continue;
            if (name.empty()) name = host;
            g.providers.push_back({host, name, notes});
        }
    }
    if (g.providers.empty()) {
        // Hard fallback — public Category A IBR hosts (no localhost).
        g.providers.push_back({"yax.im", "yax.im", {}});
        g.providers.push_back({"jabber.fr", "jabber.fr", {}});
        g.providers.push_back({"xmpp.party", "xmpp.party", {}});
    }
    g.provider_sel = 0;
    g.provider_scroll = 0;
    g.field_server = g.providers[0].host;
}

void select_provider(int idx) {
    if (idx < 0 || idx >= (int)g.providers.size()) return;
    g.provider_sel = idx;
    g.field_server = g.providers[idx].host;
}

std::string accounts_path() { return exe_dir() + "\\accounts.txt"; }

void load_accounts() {
    g.recent_jids.clear();
    g.recent_sel = -1;
    g.recent_scroll = 0;
    std::ifstream in(accounts_path());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        trim_inplace(line);
        if (line.empty() || line.find('@') == std::string::npos) continue;
        bool dup = false;
        for (const auto &j : g.recent_jids)
            if (_stricmp(j.c_str(), line.c_str()) == 0) {
                dup = true;
                break;
            }
        if (dup) continue;
        g.recent_jids.push_back(line);
        if ((int)g.recent_jids.size() >= kMaxRecentAccounts) break;
    }
}

void save_accounts() {
    std::ofstream out(accounts_path(), std::ios::trunc);
    if (!out) return;
    out << "# Sagrado Jabber recent accounts (JIDs only — never passwords)\n";
    for (const auto &j : g.recent_jids) out << j << "\n";
}

void remember_jid(const std::string &jid_in) {
    std::string jid = jid_in;
    trim_inplace(jid);
    if (jid.empty() || jid.find('@') == std::string::npos) return;
    g.recent_jids.erase(
        std::remove_if(g.recent_jids.begin(), g.recent_jids.end(),
                       [&](const std::string &j) {
                           return _stricmp(j.c_str(), jid.c_str()) == 0;
                       }),
        g.recent_jids.end());
    g.recent_jids.insert(g.recent_jids.begin(), jid);
    if ((int)g.recent_jids.size() > kMaxRecentAccounts)
        g.recent_jids.resize(kMaxRecentAccounts);
    g.recent_sel = 0;
    save_accounts();
}

void select_recent_jid(int idx) {
    if (idx < 0 || idx >= (int)g.recent_jids.size()) return;
    g.recent_sel = idx;
    g.field_jid = g.recent_jids[idx];
    g.focus_field = 1; // password next
}

void sign_on_dialog_size(int *dw, int *dh) {
    *dw = 360;
    *dh = 200;
    // Account picker only when 2+ remembered JIDs (between name and password).
    if ((int)g.recent_jids.size() < 2) return;
    int rows = std::min(4, (int)g.recent_jids.size());
    *dh += rows * 20 + 8;
}

void open_sign_on() {
    g.dialog = DlgSignOn;
    g.captcha_visible = false;
    g.recent_scroll = 0;
    if (!g.recent_jids.empty()) {
        if (g.field_jid.empty()) g.field_jid = g.recent_jids[0];
        g.recent_sel = 0;
        for (int i = 0; i < (int)g.recent_jids.size(); ++i)
            if (_stricmp(g.recent_jids[i].c_str(), g.field_jid.c_str()) == 0) {
                g.recent_sel = i;
                break;
            }
        if (g.recent_sel >= 0) g.field_jid = g.recent_jids[g.recent_sel];
        g.focus_field = 1; // password
    } else {
        g.recent_sel = -1;
        g.focus_field = g.field_jid.empty() ? 0 : 1;
    }
}

bool identity_visible() {
    return g.client.state == jabber::ConnState::Online || !g.recent_jids.empty();
}

std::string remembered_jid() {
    if (!g.recent_jids.empty()) return g.recent_jids[0];
    return g.field_jid;
}

void blit(HWND hwnd, Canvas &cv) {
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cv.width();
    bi.bmiHeader.biHeight = -cv.height();
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(hdc, 0, 0, cv.width(), cv.height(), 0, 0, 0, cv.height(),
                      cv.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, hdc);
}

void redraw() {
    if (!g.hwnd) return;
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void post_client_event(const jabber::ClientEvent &e) {
    auto *heap = new jabber::ClientEvent(e);
    if (!PostMessageA(g.hwnd, WM_JABBER_EVENT, 0, (LPARAM)heap))
        delete heap;
}

void layout() {
    int W = g.canvas.width(), H = g.canvas.height();
    g.gel = gel_layout(0, 0, W, H, GelStyle::Main, &g.ap, g.focused);
    Rect cl = g.gel.client;
    g.status_r = {cl.x, cl.bottom() - kStatusH, cl.w, kStatusH};
    int top = cl.y + kMenuBarH;
    int body_h = g.status_r.y - top;
    bool signed_on = g.client.state == jabber::ConnState::Online;
    // Strip when online, or when a remembered account gives us a “you”.
    int id_h = identity_visible() ? kIdentityH : 0;
    g.identity_r = {cl.x, top, kRosterW, id_h};
    g.roster_r = {cl.x, top + id_h, kRosterW, body_h - id_h};
    if (id_h > 0) {
        g.avatar_r = {g.identity_r.x + 8,
                      g.identity_r.y + (g.identity_r.h - kAvatarSz) / 2, kAvatarSz,
                      kAvatarSz};
        g.presence_r = {g.avatar_r.right() - 10, g.avatar_r.bottom() - 10, 10, 10};
        if (signed_on) {
            int fx = g.avatar_r.right() + 8;
            int fw = g.identity_r.right() - 8 - fx;
            g.status_field_r = {fx, g.identity_r.bottom() - 26, fw, 20};
        } else {
            g.status_field_r = {};
        }
    } else {
        g.avatar_r = {};
        g.presence_r = {};
        g.status_field_r = {};
    }
    // Roster/chat scrollbars are claimed in paint when content overflows —
    // leave prior rects intact so hit-testing between paints still works.

    int cx = g.roster_r.right();
    int cw = cl.right() - cx;
    g.tabs_r = {cx, top, cw, kTabH};
    g.compose_r = {cx, g.status_r.y - kComposeH, cw, kComposeH};
    g.transcript_r = {cx, g.tabs_r.bottom(), cw,
                      g.compose_r.y - g.tabs_r.bottom()};

    auto layout_compose_btns = [&]() {
        const int bh = 28;
        const int by = g.compose_r.y + (g.compose_r.h - bh) / 2;
        g.btn_send = {g.compose_r.right() - 72, by, 64, bh};
        g.btn_compose_attach = {g.btn_send.x - 36, by, 28, bh};
        g.btn_compose_emoji = {g.compose_r.x + 8, by, 28, bh};
        g.compose_field_r = {g.btn_compose_emoji.right() + 6, g.compose_r.y + 8,
                             g.btn_compose_attach.x - g.btn_compose_emoji.right() - 12,
                             g.compose_r.h - 16};
    };
    layout_compose_btns();

    g.progress_r = {};
    if (g.file_progress >= 0) {
        g.progress_r = {g.status_r.right() - 140, g.status_r.y + 3, 128,
                        g.status_r.h - 6};
    }
    g.occ_r = {};
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size() &&
        g.tabs[g.active_tab].muc) {
        // Full-height nick rail beside transcript + compose.
        int occ_w = 120;
        g.occ_r = {cl.right() - occ_w, g.tabs_r.bottom(), occ_w,
                   g.status_r.y - g.tabs_r.bottom()};
        g.transcript_r.w -= occ_w;
        g.compose_r.w -= occ_w;
        layout_compose_btns();
    }
}

// Shared V-scrollbar hit: arrows / thumb / page. Returns true if consumed.
bool sbar_mouse_down(Rect sbar, int x, int y, int &scroll, int max_v, int page,
                     int step, Drag thumb_drag, Drag arrow_drag) {
    if (sbar.w <= 0 || max_v <= 0 || !sbar.contains(x, y)) return false;
    if (max_v < 0) max_v = 0;
    if (page < 1) page = 1;
    if (step < 1) step = 1;
    ScrollLayout sl = scroll_layout(g.ap, sbar, scroll, max_v, page);
    ScrollArrowHot ah = scroll_arrow_hit(sl, x, y);
    if (ah != ScrollArrowHot::None) {
        g.drag = arrow_drag;
        g.arrow_hot = ah;
        g.arrow_dir = scroll_arrow_dir(ah);
        scroll = std::clamp(scroll + g.arrow_dir * step, 0, max_v);
        redraw();
        return true;
    }
    if (sl.thumb.contains(x, y)) {
        g.drag = thumb_drag;
        g.thumb_grab = y - sl.thumb.y;
        redraw();
        return true;
    }
    if (y < sl.thumb.y) scroll = std::max(0, scroll - page);
    else scroll = std::min(max_v, scroll + page);
    redraw();
    return true;
}

bool sbar_thumb_drag(Rect sbar, int y, int &scroll, int max_v, int page) {
    if (sbar.w <= 0 || max_v <= 0) return false;
    if (page < 1) page = 1;
    ScrollLayout sl = scroll_layout(g.ap, sbar, scroll, max_v, page);
    int track = sl.track.h - sl.thumb.h;
    if (track <= 0) return false;
    int ty = y - g.thumb_grab - sl.track.y;
    scroll = std::clamp(ty * max_v / track, 0, max_v);
    redraw();
    return true;
}

void paint_v_sbar(Canvas &cv, Rect sbar, int value, int max_v, int page,
                  Drag thumb_drag, Drag arrow_drag) {
    if (sbar.w <= 0 || max_v <= 0) return;
    bool thumb = (g.drag == thumb_drag);
    ScrollArrowHot ah =
        (g.drag == arrow_drag) ? g.arrow_hot : ScrollArrowHot::None;
    paint_scrollbar(cv, g.ap, sbar, value, max_v, page, thumb, false, false, ah);
}

// Place a V-bar on the right of `list` only when there is overflow.
void claim_v_sbar(Rect &list, Rect &sbar, int max_v) {
    sbar = {};
    if (max_v <= 0 || list.w <= kScrollbarW + 40 || list.h <= 0) return;
    sbar = {list.right() - kScrollbarW, list.y, kScrollbarW, list.h};
    list.w -= kScrollbarW;
}

// Fixed AIM/Yahoo presence ink — readable on every Hap, not skin roles.
Color presence_color(const Appearance &, jabber::Show s) {
    switch (s) {
    case jabber::Show::Chat:
        return {0x2e, 0xc2, 0x4a}; // Available — green
    case jabber::Show::Away:
    case jabber::Show::Xa:
        return {0xf0, 0xc0, 0x20}; // Away — amber
    case jabber::Show::Dnd:
        return {0xe0, 0x40, 0x40}; // Busy — red
    default:
        return {0x88, 0x88, 0x88}; // Invisible / signed off — grey
    }
}

// Leading spaces leave a gutter; marks stamp into it after paint_menu.
static const char *kPresenceItems[] = {
    "    Available",
    "    Away",
    "    Busy",
    "    Invisible",
};
static const jabber::Show kPresenceShows[] = {
    jabber::Show::Chat,
    jabber::Show::Away,
    jabber::Show::Dnd,
    jabber::Show::Unavailable,
};

void paint_presence_menu_marks(Canvas &cv, const MenuLayout &lay) {
    constexpr int kMark = 8;
    for (int i = 0; i < 4; ++i) {
        Rect row{lay.items_bounds.x, lay.items_bounds.y + i * lay.item_h,
                 lay.items_bounds.w, lay.item_h};
        Rect mark{row.x + 5, row.y + (row.h - kMark) / 2, kMark, kMark};
        cv.fill(mark, presence_color(g.ap, kPresenceShows[i]));
        bool hot = (i == g.menu_item_hot);
        cv.frame(mark, hot ? g.ap.c("menu.hilite_dark") : g.ap.c("menu.dark"));
    }
}

std::string mime_for_path(const std::string &path) {
    std::string lower = path;
    for (char &c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".png") == 0)
        return "image/png";
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".gif") == 0)
        return "image/gif";
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".bmp") == 0)
        return "image/bmp";
    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0)
        return "image/jpeg";
    if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".jpg") == 0)
        return "image/jpeg";
    return "image/png";
}

void pick_and_set_picture() {
    if (g.client.state != jabber::ConnState::Online) {
        set_status("Sign on first");
        return;
    }
    OPENFILENAMEA ofn{};
    char file[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        "Pictures (*.png;*.jpg;*.jpeg;*.gif;*.bmp)\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0"
        "All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        set_status("Could not open picture");
        return;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    g.client.set_own_photo(data, mime_for_path(file));
}

void paint_avatar_tile(Canvas &cv, const Appearance &ap, Rect r, const SkinImage *img,
                       const std::string &initials, Color fill = {}) {
    Color tile_bg = (fill.r | fill.g | fill.b) ? fill : ap.c("list.background");
    cv.fill(r, tile_bg);
    cv.frame(r, ap.c("list.separator"));
    if (img && !img->empty()) {
        CanvasClip clip(cv, r);
        // Nearest-neighbour scale into tile.
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                int sx = x * img->w / r.w;
                int sy = y * img->h / r.h;
                uint32_t p = img->at(sx, sy);
                if (((p >> 24) & 255) < 8) continue;
                cv.data()[size_t(r.y + y) * cv.width() + size_t(r.x + x)] = p | 0xFF000000;
            }
    } else {
        std::string t = initials.empty() ? "?" : initials.substr(0, 2);
        int tw = cv.text_width(t.c_str());
        // Prefer light ink on saturated XEP-0392 fills.
        Color ink = (fill.r | fill.g | fill.b) ? Color{245, 245, 245}
                                               : ap.c("primary.label");
        cv.text(r.x + (r.w - tw) / 2, r.y + (r.h - cv.line_height()) / 2, t.c_str(),
                ink);
    }
}

void commit_status_message() {
    if (g.client.state != jabber::ConnState::Online) return;
    g.client.set_status_message(g.status_msg);
    g.status_field_focus = false;
}

void open_presence_menu() {
    g.presence_menu = true;
    g.menu_open = MenuBuddy;
    g.menu_item_hot = -1;
}

struct RosterRow {
    bool section = false;
    std::string group; // section label, or buddy's group
    jabber::Buddy buddy;
};

std::vector<RosterRow> build_roster_rows() {
    std::vector<jabber::Buddy> v;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        for (auto &kv : g.client.roster) v.push_back(kv.second);
    }
    for (auto &b : v)
        if (b.group.empty()) b.group = "Buddies";
    std::sort(v.begin(), v.end(), [](const jabber::Buddy &a, const jabber::Buddy &b) {
        if (a.group != b.group) return a.group < b.group;
        bool ao = a.show != jabber::Show::Unavailable;
        bool bo = b.show != jabber::Show::Unavailable;
        if (ao != bo) return ao > bo;
        return a.name < b.name;
    });
    std::vector<RosterRow> rows;
    std::string cur;
    for (auto &b : v) {
        if (b.group != cur) {
            cur = b.group;
            rows.push_back({true, cur, {}});
        }
        rows.push_back({false, b.group, b});
    }
    return rows;
}

std::string selected_buddy_jid() {
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size() &&
        !g.tabs[g.active_tab].muc)
        return g.tabs[g.active_tab].jid;
    auto rows = build_roster_rows();
    if (g.roster_hot >= 0 && g.roster_hot < (int)rows.size() &&
        !rows[g.roster_hot].section)
        return rows[g.roster_hot].buddy.jid;
    return {};
}

void open_subscribe_ask(const std::string &jid) {
    if (jid.empty()) return;
    if (g.sub_ask_open && !g.sub_ask_jid.empty() &&
        !jabber::jid_ieq(g.sub_ask_jid, jid)) {
        // Keep queue in client; show next when this sheet closes.
        return;
    }
    g.sub_ask_open = true;
    g.sub_ask_jid = jid;
    if (g.in_tray || !IsWindowVisible(g.hwnd))
        tray_balloon("Buddy request",
                     jabber::jid_node(jid) + " wants to add you");
}

void close_subscribe_ask(bool accepted) {
    std::string jid = g.sub_ask_jid;
    g.sub_ask_open = false;
    g.sub_ask_jid.clear();
    g.sub_accept_r = {};
    g.sub_deny_r = {};
    if (!jid.empty()) {
        if (accepted) g.client.authorize_buddy(jid);
        else g.client.deny_buddy(jid);
    }
    // Show next pending ask if any.
    std::string next;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        if (!g.client.pending_subscribe.empty())
            next = g.client.pending_subscribe.front();
    }
    if (!next.empty()) open_subscribe_ask(next);
    else maybe_show_next_muc_invite();
}

void open_muc_invite_ask(const std::string &room, const std::string &from,
                         const std::string &reason) {
    if (room.empty()) return;
    if (g.sub_ask_open || g.about_open || g.dialog != DlgNone) {
        // Stay queued in client until the current sheet closes.
        return;
    }
    if (g.muc_invite_open && !g.muc_invite_room.empty() &&
        !jabber::jid_ieq(g.muc_invite_room, room)) {
        // Keep queue in client; show next when this sheet closes.
        return;
    }
    g.muc_invite_open = true;
    g.muc_invite_room = room;
    g.muc_invite_from = from;
    g.muc_invite_reason = reason;
}

void maybe_show_next_muc_invite() {
    if (g.muc_invite_open || g.sub_ask_open || g.dialog != DlgNone || g.about_open)
        return;
    jabber::MucInvite next{};
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        if (!g.client.pending_muc_invites.empty()) {
            next = g.client.pending_muc_invites.front();
            have = true;
        }
    }
    if (have) open_muc_invite_ask(next.room, next.from, next.reason);
}

void close_muc_invite_ask(bool accepted) {
    std::string room = g.muc_invite_room;
    std::string from = g.muc_invite_from;
    g.muc_invite_open = false;
    g.muc_invite_room.clear();
    g.muc_invite_from.clear();
    g.muc_invite_reason.clear();
    g.muc_invite_accept_r = {};
    g.muc_invite_decline_r = {};
    if (!room.empty()) {
        // Decline = ignore (no protocol). Accept also drops the queue entry
        // before opening Join so the sheet does not reappear.
        g.client.decline_muc_invite(room);
        if (accepted) {
            g.dialog = DlgJoinMuc;
            g.focus_field = 1;
            g.field_room = room;
            g.field_room_pass.clear();
            if (g.field_nick.empty() && !g.client.jid.empty())
                g.field_nick = jabber::jid_node(g.client.jid);
            set_status(jabber::jid_node(from) + " invited you — join when ready");
            return;
        }
    }
    maybe_show_next_muc_invite();
}

void stop_typing_indicator() {
    if (g.typing_sent && !g.typing_peer.empty())
        g.client.send_chat_state(g.typing_peer, "active");
    g.typing_sent = false;
    g.typing_peer.clear();
    if (g.hwnd) KillTimer(g.hwnd, kTypingTimerId);
}

void bump_typing_composing() {
    if (g.active_tab < 0 || g.active_tab >= (int)g.tabs.size()) return;
    if (g.tabs[g.active_tab].muc) return;
    if (g.client.state != jabber::ConnState::Online) return;
    std::string to = g.tabs[g.active_tab].jid;
    if (!g.typing_sent || !jabber::jid_ieq(g.typing_peer, to)) {
        g.client.send_chat_state(to, "composing");
        g.typing_sent = true;
        g.typing_peer = to;
    }
    if (g.hwnd) SetTimer(g.hwnd, kTypingTimerId, kTypingPauseMs, nullptr);
}

void clear_compose() {
    g.compose.doc.text.clear();
    g.compose.doc.caret = g.compose.doc.anchor = 0;
    g.compose.scroll_y = 0;
    g.compose.lines.clear();
}

void send_compose() {
    std::string body = text_field_trimmed(g.compose);
    if (g.active_tab < 0 || body.empty()) return;
    auto &tab = g.tabs[g.active_tab];
    if (tab.muc)
        g.client.send_muc_message(tab.jid, body);
    else {
        stop_typing_indicator();
        // XEP-0308 — an Up-arrow edit replaces the targeted message.
        if (!g.correct_id.empty() && g.correct_jid == tab.jid)
            g.client.send_message(tab.jid, body, {}, g.correct_id);
        else
            g.client.send_message(tab.jid, body);
    }
    g.correct_id.clear();
    g.correct_jid.clear();
    clear_compose();
}

// XEP-0308 — Up in an empty compose loads your last message for editing.
bool begin_edit_last_message() {
    if (g.active_tab < 0 || !g.compose.doc.text.empty()) return false;
    auto &tab = g.tabs[g.active_tab];
    if (tab.muc) return false;
    std::string body, id;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        auto it = g.client.chats.find(tab.jid);
        if (it == g.client.chats.end()) return false;
        for (auto ln = it->second.rbegin(); ln != it->second.rend(); ++ln) {
            if (ln->mine && !ln->system && !ln->id.empty() && !ln->file) {
                body = ln->body;
                id = ln->id;
                break;
            }
        }
    }
    if (id.empty()) return false;
    g.correct_id = id;
    g.correct_jid = tab.jid;
    g.compose.doc.text = body;
    g.compose.doc.caret = g.compose.doc.anchor = body.size();
    set_status("Editing last message — Enter to save");
    return true;
}

void close_ctx_menu() {
    context_menu_close(g.ctx);
    g.ctx_kind = CtxNone;
}

// Clipboard / debug string — nick and body on separate lines (not IRC "nick:").
std::string format_chat_line(const jabber::ChatLine &ln, bool muc) {
    if (ln.system) return ln.body;
    std::string who = chat_display_name(ln, muc);
    if (ln.omemo) who += " *";
    std::string text = who + "\n" + chat_body_text(ln);
    if (ln.mine && ln.delivered) text += "\nDelivered";
    return text;
}

// Hit-test transcript line under cursor; sets chat_sel when non-system.
int hit_transcript_line(int x, int y) {
    if (g.active_tab < 0 || !g.transcript_r.contains(x, y)) return -1;
    if (g.chat_sbar.w > 0 && g.chat_sbar.contains(x, y)) return -1;
    std::string key = g.tabs[g.active_tab].jid;
    bool muc = g.tabs[g.active_tab].muc;
    std::vector<jabber::ChatLine> lines;
    std::string subject;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        lines = g.client.chats[key];
        if (muc) {
            auto it = g.client.muc_subjects.find(key);
            if (it != g.client.muc_subjects.end()) subject = it->second;
        }
    }
    const int pad = kMsgPadX;
    const int lh = g.canvas.line_height();
    const int full_wrap = std::max(8, g.transcript_r.w - 2 * pad);
    int top = g.transcript_r.y + 4;
    if (muc && !subject.empty()) {
        std::string sub = "Topic: " + subject;
        top += text_content_height(layout_lines(g.canvas, sub, full_wrap, true), lh) +
               4;
    }
    Rect body{g.transcript_r.x, top, g.transcript_r.w,
              g.transcript_r.bottom() - top};
    if (!body.contains(x, y)) return -1;
    int ty = body.y - g.chat_scroll;
    for (int i = 0; i < (int)lines.size(); ++i) {
        int h = full_block_h(g.canvas, lines, i, muc, g.transcript_r.w, pad);
        if (y >= ty && y < ty + h) return lines[i].system ? -1 : i;
        ty += h;
    }
    return -1;
}

// Hit a reaction pill under a message; returns line index or -1.
int hit_reaction_pill(int x, int y, std::string *emoji_out) {
    if (emoji_out) emoji_out->clear();
    if (g.active_tab < 0 || !g.transcript_r.contains(x, y)) return -1;
    if (g.chat_sbar.w > 0 && g.chat_sbar.contains(x, y)) return -1;
    std::string key = g.tabs[g.active_tab].jid;
    bool muc = g.tabs[g.active_tab].muc;
    std::vector<jabber::ChatLine> lines;
    std::string subject;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        lines = g.client.chats[key];
        if (muc) {
            auto it = g.client.muc_subjects.find(key);
            if (it != g.client.muc_subjects.end()) subject = it->second;
        }
    }
    const int pad = kMsgPadX;
    const int lh = g.canvas.line_height();
    const int full_wrap = std::max(8, g.transcript_r.w - 2 * pad);
    int top = g.transcript_r.y + 4;
    if (muc && !subject.empty()) {
        std::string sub = "Topic: " + subject;
        top += text_content_height(layout_lines(g.canvas, sub, full_wrap, true), lh) +
               4;
    }
    Rect body{g.transcript_r.x, top, g.transcript_r.w,
              g.transcript_r.bottom() - top};
    if (!body.contains(x, y)) return -1;
    int ty = body.y - g.chat_scroll;
    Color tbg = g.ap.c("text.background");
    for (int i = 0; i < (int)lines.size(); ++i) {
        bool headed = lines[i].system || i == 0 ||
                      !chat_same_sender(lines[size_t(i - 1)], lines[size_t(i)], muc);
        int bw = chat_body_wrap(g.transcript_r.w, pad, headed);
        int h = full_block_h(g.canvas, lines, i, muc, g.transcript_r.w, pad);
        if (y >= ty && y < ty + h && !lines[i].system && !lines[i].reactions.empty()) {
            int text_x = body.x + pad + kMsgAvatar + kMsgAvatarGap;
            int row_y = ty;
            row_y += date_sep_h(lines, i, lh);
            if (headed) row_y += lh + 2;
            SkinImage image;
            int image_state = 0, image_w = 0, image_h = 0;
            if (lines[i].file &&
                inline_image_snapshot(lines[i].body, &image, &image_state) &&
                image_state == 2) {
                inline_image_box(image, bw, &image_w, &image_h);
            }
            if (image_h > 0)
                row_y += image_h + 4;
            else
                row_y += text_content_height(
                    layout_lines(g.canvas, chat_body_text(lines[i]), bw, true), lh);
            std::vector<ReactPill> pills;
            layout_reaction_pills(g.canvas, g.ap, text_x, row_y, lines[i],
                                  g.ap.c("menu.disable_label"), tbg, false, &pills);
            for (const auto &p : pills) {
                if (p.r.contains(x, y)) {
                    if (emoji_out) *emoji_out = p.emoji;
                    return i;
                }
            }
        }
        ty += h;
    }
    return -1;
}

void run_ctx_menu(int row) {
    if (!g.ctx.open || row < 0 || row >= (int)g.ctx.items.size()) {
        close_ctx_menu();
        return;
    }
    if (g.ctx.items[size_t(row)].sep || !g.ctx.items[size_t(row)].enabled) {
        close_ctx_menu();
        return;
    }
    const char *lab = g.ctx.items[size_t(row)].label;
    int kind = g.ctx_kind;
    close_ctx_menu();
    if (!lab) return;
    if (std::strcmp(lab, "Copy") == 0) {
        if (kind == CtxCompose) {
            text_field_copy(g.hwnd, g.compose);
        } else if (kind == CtxTranscript && g.active_tab >= 0 &&
                   g.chat_sel >= 0) {
            std::string key = g.tabs[g.active_tab].jid;
            bool muc = g.tabs[g.active_tab].muc;
            std::vector<jabber::ChatLine> lines;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                lines = g.client.chats[key];
            }
            if (g.chat_sel < (int)lines.size())
                sagrado::clipboard_set(
                    g.hwnd, format_chat_line(lines[g.chat_sel], muc));
        }
    } else if (std::strcmp(lab, "Paste") == 0) {
        g.compose.focused = true;
        text_field_paste(g.hwnd, g.compose);
        if (!g.compose.doc.text.empty()) bump_typing_composing();
    } else if (std::strcmp(lab, "React…") == 0 ||
               std::strcmp(lab, "React...") == 0) {
        open_react_dialog();
    } else if (std::strcmp(lab, "Retract") == 0) {
        retract_selected_message();
    }
}

void open_compose_ctx(int x, int y) {
    ContextMenuItem items[] = {
        {"Copy", g.compose.doc.has_sel(), false},
        {"Paste", true, false},
    };
    g.ctx_kind = CtxCompose;
    context_menu_open(g.ctx, x, y, items, 2);
}

// XEP-0424 for our own messages; XEP-0425 when a moderator retracts somebody
// else's room message. Falls back to our last message when nothing is selected.
void retract_selected_message() {
    if (g.active_tab < 0 || g.active_tab >= (int)g.tabs.size()) {
        set_status("Open a chat first");
        return;
    }
    std::string key = g.tabs[g.active_tab].jid;
    bool muc = g.tabs[g.active_tab].muc;
    std::string target;
    bool mine = false;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        const auto &lines = g.client.chats[key];
        int idx = g.chat_sel;
        if (idx < 0 || idx >= (int)lines.size()) {
            for (int i = (int)lines.size() - 1; i >= 0; --i) {
                if (lines[i].mine && !lines[i].system && !lines[i].retracted) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0 || idx >= (int)lines.size()) return;
        const jabber::ChatLine &ln = lines[idx];
        if (ln.system || ln.retracted) return;
        target = ln.react_id.empty() ? ln.id : ln.react_id;
        mine = ln.mine;
    }
    if (target.empty()) {
        set_status("That message can't be retracted");
        return;
    }
    if (mine) {
        g.client.send_retract(key, target, muc);
        set_status("Retracted");
    } else if (muc) {
        g.client.moderate_retract(key, target);
        set_status("Asked the room to retract that message");
    } else {
        set_status("Only your own messages can be retracted");
    }
    redraw();
}

bool can_retract_selected() {
    if (g.active_tab < 0 || g.active_tab >= (int)g.tabs.size()) return false;
    if (g.chat_sel < 0) return false;
    std::lock_guard<std::mutex> lock(g.client.mu);
    const auto &lines = g.client.chats[g.tabs[g.active_tab].jid];
    if (g.chat_sel >= (int)lines.size()) return false;
    const jabber::ChatLine &ln = lines[g.chat_sel];
    if (ln.system || ln.retracted) return false;
    if (ln.react_id.empty() && ln.id.empty()) return false;
    return ln.mine || g.tabs[g.active_tab].muc;
}

void open_transcript_ctx(int x, int y) {
    bool can_react = false;
    if (g.chat_sel >= 0 && g.active_tab >= 0) {
        std::string key = g.tabs[g.active_tab].jid;
        std::lock_guard<std::mutex> lock(g.client.mu);
        const auto &lines = g.client.chats[key];
        if (g.chat_sel < (int)lines.size() && !lines[g.chat_sel].system &&
            !lines[g.chat_sel].react_id.empty())
            can_react = true;
    }
    ContextMenuItem items[] = {
        {"Copy", g.chat_sel >= 0, false},
        {"Paste", true, false},
        {"React…", can_react && g.emoji_pack_ok, false},
        {"Retract", can_retract_selected(), false},
    };
    g.ctx_kind = CtxTranscript;
    context_menu_open(g.ctx, x, y, items, 4);
}

const char *show_label(jabber::Show s) {
    switch (s) {
    case jabber::Show::Chat: return "Available";
    case jabber::Show::Away: return "Away";
    case jabber::Show::Xa: return "Away";
    case jabber::Show::Dnd: return "Busy";
    default: return "Offline";
    }
}

void open_tab(const std::string &jid, bool muc) {
    std::string bare = jabber::bare_jid(jid);
    for (int i = 0; i < (int)g.tabs.size(); ++i) {
        if (g.tabs[i].jid == bare) {
            if (muc) g.tabs[i].muc = true;
            if (g.active_tab != i) stop_typing_indicator();
            g.active_tab = i;
            g.chat_scroll = 0;
            g.chat_sel = -1;
            // Cold open: pull recent archive if this chat is still empty.
            if (!muc) {
                g.client.request_mam_history(bare);
                g.client.ensure_omemo_peer(bare);
            }
            redraw();
            return;
        }
    }
    stop_typing_indicator();
    g.tabs.push_back({bare, muc});
    g.active_tab = (int)g.tabs.size() - 1;
    g.chat_scroll = 0;
    g.chat_sel = -1;
    if (!muc) {
        g.client.request_mam_history(bare);
        g.client.ensure_omemo_peer(bare);
    }
    redraw();
}

void close_tab_jid(const std::string &jid) {
    std::string bare = jabber::bare_jid(jid);
    for (int i = 0; i < (int)g.tabs.size(); ++i) {
        if (g.tabs[i].jid != bare) continue;
        g.tabs.erase(g.tabs.begin() + i);
        if (g.active_tab >= (int)g.tabs.size())
            g.active_tab = (int)g.tabs.size() - 1;
        else if (g.active_tab > i)
            --g.active_tab;
        g.chat_scroll = 0;
        return;
    }
}

// Prefer selected line; else last non-system line with a react_id.
bool pick_react_target(std::string *react_id_out) {
    if (!react_id_out) return false;
    react_id_out->clear();
    if (g.active_tab < 0 || g.active_tab >= (int)g.tabs.size()) return false;
    std::string key = g.tabs[g.active_tab].jid;
    std::vector<jabber::ChatLine> lines;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        lines = g.client.chats[key];
    }
    if (g.chat_sel >= 0 && g.chat_sel < (int)lines.size() &&
        !lines[g.chat_sel].system && !lines[g.chat_sel].react_id.empty()) {
        *react_id_out = lines[g.chat_sel].react_id;
        return true;
    }
    for (int i = (int)lines.size() - 1; i >= 0; --i) {
        if (!lines[i].system && !lines[i].react_id.empty()) {
            *react_id_out = lines[i].react_id;
            g.chat_sel = i;
            return true;
        }
    }
    return false;
}

void close_emoji_host() {
    g.react_target_id.clear();
    g.emoji_compose_mode = false;
    g.emoji_sbar_thumb = false;
    g.emoji_sbar_arrow = false;
    g.emoji_sbar_ah = ScrollArrowHot::None;
    g.emoji_st.hot_cell = -1;
    g.emoji_st.hot_nav = -1;
    g.emoji_st.pressed_cell = -1;
    sagrado::gel_host_show(g.emoji_host, false);
    if (g.hwnd) SetForegroundWindow(g.hwnd);
}

void emoji_host_paint(sagrado::GelHost &host, Canvas &cv, const Appearance &ap,
                      Rect client, void *) {
    g.emoji_lay = sagrado::paint_emoji_picker_client(
        cv, ap, client, g.emoji_st, host.focused, g.emoji_sbar_thumb,
        g.emoji_sbar_ah, &host.gel);
}

void emoji_host_close(sagrado::GelHost &, void *) { close_emoji_host(); }

bool emoji_host_input(sagrado::GelHost &host, UINT msg, WPARAM wp, LPARAM lp,
                      void *) {
    using HK = sagrado::EmojiPickerHitKind;
    if (msg == WM_MOUSEMOVE) {
        if (g.emoji_sbar_thumb && g.emoji_lay.grid_max > 0) {
            int y = GET_Y_LPARAM(lp);
            ScrollLayout sl =
                scroll_layout(g.ap, g.emoji_lay.sbar, g.emoji_st.scroll,
                              g.emoji_lay.grid_max, g.emoji_lay.grid_page);
            int track = sl.track.h - sl.thumb.h;
            if (track > 0) {
                int ty = y - g.thumb_grab - sl.track.y;
                g.emoji_st.scroll =
                    std::clamp(ty * g.emoji_lay.grid_max / track, 0,
                               g.emoji_lay.grid_max);
            }
            return true;
        }
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        auto hit = sagrado::emoji_picker_hit(g.emoji_lay, x, y);
        int hot_cell = -1, hot_nav = -1;
        if (hit.kind == HK::Cell) hot_cell = hit.index;
        if (hit.kind == HK::Nav) hot_nav = hit.index;
        if (hot_cell != g.emoji_st.hot_cell || hot_nav != g.emoji_st.hot_nav) {
            g.emoji_st.hot_cell = hot_cell;
            g.emoji_st.hot_nav = hot_nav;
            return true;
        }
        return false;
    }
    if (msg == WM_LBUTTONUP) {
        g.emoji_sbar_thumb = false;
        g.emoji_sbar_arrow = false;
        g.emoji_sbar_ah = ScrollArrowHot::None;
        g.emoji_st.pressed_cell = -1;
        return true;
    }
    if (msg == WM_LBUTTONDOWN) {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        auto hit = sagrado::emoji_picker_hit(g.emoji_lay, x, y);
        if (hit.kind == HK::Cancel || hit.kind == HK::Close) {
            close_emoji_host();
            return true;
        }
        if (hit.kind == HK::Search) {
            g.emoji_st.search_focus = true;
            return true;
        }
        if (hit.kind == HK::Nav && hit.index >= 0) {
            g.emoji_st.category = hit.index;
            g.emoji_st.query.clear();
            g.emoji_st.scroll = 0;
            g.emoji_st.search_focus = false;
            return true;
        }
        if (hit.kind == HK::Cell && hit.index >= 0) {
            std::string wire =
                sagrado::emoji_picker_wire_at(g.emoji_st, hit.index);
            if (!wire.empty() && g.active_tab >= 0) {
                if (!g.react_target_id.empty()) {
                    bool muc = g.tabs[g.active_tab].muc;
                    g.client.send_reaction(g.tabs[g.active_tab].jid,
                                           g.react_target_id, wire, muc);
                    sagrado::emoji_recent_push(g.emoji_st, wire);
                    save_emoji_recent();
                    set_status("Reacted");
                    // Stay open so more reactions can be added (Esc closes).
                    redraw();
                    return true;
                }
                if (g.emoji_compose_mode) {
                    g.compose.doc.insert(wire);
                    sagrado::emoji_recent_push(g.emoji_st, wire);
                    save_emoji_recent();
                    close_emoji_host();
                    g.compose.focused = true;
                    redraw();
                    return true;
                }
            }
            close_emoji_host();
            redraw();
            return true;
        }
        if (hit.kind == HK::Sbar && g.emoji_lay.grid_max > 0) {
            ScrollLayout sl =
                scroll_layout(g.ap, g.emoji_lay.sbar, g.emoji_st.scroll,
                              g.emoji_lay.grid_max, g.emoji_lay.grid_page);
            ScrollArrowHot ah = scroll_arrow_hit(sl, x, y);
            if (ah != ScrollArrowHot::None) {
                g.emoji_sbar_arrow = true;
                g.emoji_sbar_ah = ah;
                int dir = scroll_arrow_dir(ah);
                g.emoji_st.scroll =
                    std::clamp(g.emoji_st.scroll + dir * sagrado::kEmojiCell, 0,
                               g.emoji_lay.grid_max);
                return true;
            }
            if (sl.thumb.contains(x, y)) {
                g.emoji_sbar_thumb = true;
                g.thumb_grab = y - sl.thumb.y;
                return true;
            }
            if (y < sl.thumb.y)
                g.emoji_st.scroll =
                    std::max(0, g.emoji_st.scroll - g.emoji_lay.grid_page);
            else
                g.emoji_st.scroll =
                    std::min(g.emoji_lay.grid_max,
                             g.emoji_st.scroll + g.emoji_lay.grid_page);
            return true;
        }
        (void)host;
        return false;
    }
    if (msg == WM_MOUSEWHEEL) {
        if (g.emoji_lay.grid_max <= 0) return false;
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -sagrado::kEmojiCell
                                               : sagrado::kEmojiCell;
        g.emoji_st.scroll =
            std::clamp(g.emoji_st.scroll + d, 0, g.emoji_lay.grid_max);
        return true;
    }
    if (msg == WM_CHAR) {
        if (wp == 27) {
            close_emoji_host();
            return true;
        }
        if (wp == 8) {
            if (!g.emoji_st.query.empty()) g.emoji_st.query.pop_back();
            g.emoji_st.scroll = 0;
            return true;
        }
        if (wp >= 32 && wp < 127 && g.emoji_st.query.size() < 40) {
            g.emoji_st.query.push_back(char(wp));
            g.emoji_st.scroll = 0;
            g.emoji_st.search_focus = true;
            return true;
        }
        return false;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        close_emoji_host();
        return true;
    }
    return false;
}

void ensure_emoji_host() {
    if (g.emoji_host.hwnd) return;
    int dw = 520, dh = 400;
    sagrado::emoji_picker_size(&dw, &dh);
    sagrado::GelHostDesc desc{};
    desc.title = "Emoji";
    desc.class_name = "SagradoJabberEmoji";
    desc.kind = sagrado::GelHostKind::Floating;
    desc.w = dw;
    desc.h = dh;
    desc.min_w = 400;
    desc.min_h = 280;
    desc.owner = g.hwnd;
    desc.ap = &g.ap;
    desc.close_on_deactivate = true;
    if (!sagrado::gel_host_create(g.emoji_host, g.hinst, desc)) return;
    sagrado::gel_host_set_handlers(g.emoji_host, emoji_host_paint,
                                   emoji_host_input, nullptr, emoji_host_close);
}

void show_emoji_host() {
    ensure_emoji_host();
    if (!g.emoji_host.hwnd) {
        set_status("Could not open emoji window");
        g.react_target_id.clear();
        g.emoji_compose_mode = false;
        return;
    }
    g.emoji_st.query.clear();
    g.emoji_st.category = 0;
    g.emoji_st.scroll = 0;
    g.emoji_st.hot_cell = -1;
    g.emoji_st.pressed_cell = -1;
    g.emoji_st.hot_nav = -1;
    g.emoji_st.search_focus = true;
    g.emoji_sbar_thumb = false;
    g.emoji_sbar_arrow = false;
    g.emoji_sbar_ah = ScrollArrowHot::None;
    sagrado::gel_host_show(g.emoji_host, true);
}

void open_react_dialog() {
    if (g.active_tab < 0) {
        set_status("Open a chat first");
        return;
    }
    if (!g.emoji_pack_ok) {
        set_status("Emoji pack missing — run make emoji-pack");
        return;
    }
    g.emoji_compose_mode = false;
    if (!pick_react_target(&g.react_target_id)) {
        set_status("No message to react to yet");
        return;
    }
    show_emoji_host();
}

void open_compose_emoji() {
    if (g.active_tab < 0) {
        set_status("Open a chat first");
        return;
    }
    if (!g.emoji_pack_ok) {
        set_status("Emoji pack missing — run make emoji-pack");
        return;
    }
    g.react_target_id.clear();
    g.emoji_compose_mode = true;
    show_emoji_host();
}

void pick_and_send_file() {
    if (g.active_tab < 0) {
        set_status("Open a chat first");
        return;
    }
    OPENFILENAMEA ofn{};
    char file[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return;
    std::ifstream f(file, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    std::string name = file;
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (!g.client.send_file(g.tabs[g.active_tab].jid, file, name, data,
                            "application/octet-stream"))
        set_status("HTTP Upload not available on this server");
    else
        set_status("Uploading " + name + "…");
}

bool tab_is_muc(int idx) {
    return idx >= 0 && idx < (int)g.tabs.size() && g.tabs[idx].muc;
}

void rebuild_browse_rows() {
    g.browse_rows.clear();
    std::vector<jabber::MucBookmark> bms;
    std::vector<jabber::MucRoomInfo> rooms;
    {
        std::lock_guard<std::mutex> lock(g.client.mu);
        bms = g.client.muc_bookmarks;
        rooms = g.client.muc_rooms;
    }
    if (!bms.empty()) {
        g.browse_rows.push_back({"", "Bookmarks", false, false, true});
        for (const auto &b : bms) {
            std::string lab = b.name.empty() ? b.jid : b.name;
            if (b.autojoin) lab += "  (autojoin)";
            g.browse_rows.push_back({b.jid, lab, true, b.autojoin, false});
        }
    }
    bool searched = !g.field_room_search.empty();
    g.browse_rows.push_back(
        {"", searched ? "Search results" : "Public rooms", false, false, true});
    if (rooms.empty()) {
        std::string conf;
        {
            std::lock_guard<std::mutex> lock(g.client.mu);
            conf = g.client.conference_host;
        }
        if (searched)
            g.browse_rows.push_back({"", "(no rooms matched)", false, false, true});
        else if (conf.empty())
            g.browse_rows.push_back(
                {"", "(no chat service on this server yet)", false, false, true});
        else
            g.browse_rows.push_back({"", "(no public rooms listed)", false, false, true});
    } else {
        for (const auto &r : rooms) {
            std::string lab = r.name.empty() ? r.jid : (r.name + "  —  " + r.jid);
            // XEP-0433 results carry an occupant count worth showing.
            if (r.occupants > 0) lab += "  (" + std::to_string(r.occupants) + ")";
            g.browse_rows.push_back({r.jid, lab, false, false, false});
        }
    }
    if (g.browse_sel >= (int)g.browse_rows.size()) g.browse_sel = -1;
}

void open_browse_muc() {
    g.dialog = DlgBrowseMuc;
    g.focus_field = 1;
    g.field_room_search.clear();
    g.browse_sel = -1;
    g.browse_scroll = 0;
    g.field_room_pass.clear();
    if (g.field_nick.empty() && !g.client.jid.empty())
        g.field_nick = jabber::jid_node(g.client.jid);
    rebuild_browse_rows();
    g.client.refresh_muc_rooms();
}

void ding() { MessageBeep(MB_OK); }

void browse_dialog_size(int *dw, int *dh) {
    *dw = 440;
    *dh = 420;
}

void join_dialog_size(int *dw, int *dh) {
    *dw = 360;
    *dh = 260;
}

void invite_dialog_size(int *dw, int *dh) {
    *dw = 360;
    *dh = 240;
}

void register_dialog_size(int *dw, int *dh) {
    *dw = 400;
    *dh = g.captcha_visible ? 400 : 340;
}

void paint_dialog(Canvas &cv) {
    if (g.dialog == DlgNone) return;
    Rect win{0, 0, cv.width(), cv.height()};
    // dim
    for (int y = 0; y < win.h; ++y)
        for (int x = 0; x < win.w; ++x) {
            uint32_t p = cv.data()[size_t(y) * win.w + x];
            int r = int((p >> 16) & 255) / 2;
            int gch = int((p >> 8) & 255) / 2;
            int b = int(p & 255) / 2;
            cv.data()[size_t(y) * win.w + x] =
                (uint32_t(r) << 16) | (uint32_t(gch) << 8) | uint32_t(b);
        }
    int dw = 360, dh = 220;
    if (g.dialog == DlgRegister) register_dialog_size(&dw, &dh);
    else if (g.dialog == DlgSignOn) sign_on_dialog_size(&dw, &dh);
    else if (g.dialog == DlgBrowseMuc) browse_dialog_size(&dw, &dh);
    else if (g.dialog == DlgJoinMuc) join_dialog_size(&dw, &dh);
    else if (g.dialog == DlgInvite) invite_dialog_size(&dw, &dh);
    else if (g.dialog == DlgSetTopic) {
        dw = 360;
        dh = 180;
    }
    Rect box{(win.w - dw) / 2, (win.h - dh) / 2, dw, dh};
    const char *title = "Sign On";
    if (g.dialog == DlgRegister) title = "Get an Account";
    if (g.dialog == DlgAddBuddy) title = "Add Buddy";
    if (g.dialog == DlgJoinMuc) title = "Join Chat Room";
    if (g.dialog == DlgBrowseMuc) title = "Browse Chat Rooms";
    if (g.dialog == DlgSetTopic) title = "Set Topic";
    if (g.dialog == DlgInvite) title = "Invite";
    paint_gel(cv, g.ap, box, title, true, 0, GelStyle::Dialog);
    GelLayout gl = gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
    Rect cl = gl.client;
    int y = cl.y + 8;
    int lh = cv.line_height();
    g.recent_list_r = {};
    g.recent_sbar = {};
    g.browse_list_r = {};
    g.browse_sbar = {};
    g.provider_list_r = {};
    g.provider_sbar = {};
    g.dlg_field_rs.clear();
    auto field = [&](const char *lab, const std::string &val, int idx, bool secret) {
        cv.text(cl.x + 12, y, lab, g.ap.c("primary.label"));
        y += lh + 2;
        Rect fr{cl.x + 12, y, cl.w - 24, 24};
        paint_field(cv, g.ap, fr, secret ? std::string(val.size(), '*').c_str()
                                         : val.c_str(),
                    g.focus_field == idx, true);
        g.dlg_field_rs.push_back({idx, fr});
        y += 30;
    };
    if (g.dialog == DlgSignOn) {
        field("Screen name", g.field_jid, 0, false);
        // 2+ accounts → compact picker between name and password.
        if ((int)g.recent_jids.size() >= 2) {
            int rows = std::min(4, (int)g.recent_jids.size());
            int row_h = lh + 4;
            int box_w = cl.w - 24;
            g.recent_list_r = {cl.x + 12, y, box_w, rows * row_h + 4};
            int content_h = (int)g.recent_jids.size() * row_h;
            g.recent_page = std::max(1, g.recent_list_r.h - 4);
            g.recent_max = std::max(0, content_h - g.recent_page);
            g.recent_scroll = std::clamp(g.recent_scroll, 0, g.recent_max);
            claim_v_sbar(g.recent_list_r, g.recent_sbar, g.recent_max);
            cv.fill(g.recent_list_r, g.ap.c("list.background"));
            {
                CanvasClip clip(cv, g.recent_list_r);
                int yy = g.recent_list_r.y + 2 - g.recent_scroll;
                for (int i = 0; i < (int)g.recent_jids.size(); ++i) {
                    Rect row{g.recent_list_r.x + 2, yy, g.recent_list_r.w - 4, row_h};
                    if (i == g.recent_sel)
                        cv.fill(row, g.ap.c("list.hilite_background"));
                    Color ink = i == g.recent_sel ? g.ap.c("list.hilite_foreground")
                                                  : g.ap.c("list.label");
                    cv.text_elided(row.x + 6, row.y + 2, g.recent_jids[i].c_str(),
                                   row.w - 12, ink);
                    yy += row_h;
                }
            }
            paint_v_sbar(cv, g.recent_sbar, g.recent_scroll, g.recent_max, g.recent_page,
                         DragThumbRecent, DragArrowRecent);
            y = std::max(g.recent_list_r.bottom(), g.recent_sbar.bottom()) + 8;
        }
        field("Password", g.field_pass, 1, true);
    } else if (g.dialog == DlgRegister) {
        // Screen name first (AIM-shaped), then home server, then password.
        cv.text(cl.x + 12, y, "Screen name", g.ap.c("primary.label"));
        y += lh + 2;
        Rect fr{cl.x + 12, y, cl.w - 24, 24};
        paint_field(cv, g.ap, fr, g.field_user.c_str(), g.focus_field == 0, true);
        g.dlg_field_rs.push_back({0, fr});
        y += 28;
        {
            std::string preview =
                (g.field_user.empty() ? "you" : g.field_user) + "@" + g.field_server;
            cv.text(cl.x + 12, y, preview.c_str(), g.ap.c("menu.disable_label"));
        }
        y += lh + 8;

        cv.text(cl.x + 12, y, "Home server", g.ap.c("primary.label"));
        y += lh + 2;
        int list_h = g.captcha_visible ? 72 : 96;
        int box_w = cl.w - 24;
        g.provider_list_r = {cl.x + 12, y, box_w, list_h};
        int row_h = lh + 4;
        int content_h = (int)g.providers.size() * row_h;
        g.provider_page = std::max(1, list_h - 4);
        g.provider_max = std::max(0, content_h - g.provider_page);
        g.provider_scroll = std::clamp(g.provider_scroll, 0, g.provider_max);
        claim_v_sbar(g.provider_list_r, g.provider_sbar, g.provider_max);
        cv.fill(g.provider_list_r, g.ap.c("list.background"));
        {
            CanvasClip clip(cv, g.provider_list_r);
            int yy = g.provider_list_r.y + 2 - g.provider_scroll;
            for (int i = 0; i < (int)g.providers.size(); ++i) {
                Rect row{g.provider_list_r.x + 2, yy, g.provider_list_r.w - 4, row_h};
                if (i == g.provider_sel)
                    cv.fill(row, g.ap.c("list.hilite_background"));
                Color ink = i == g.provider_sel ? g.ap.c("list.hilite_foreground")
                                                : g.ap.c("list.label");
                cv.text_elided(row.x + 6, row.y + 2, g.providers[i].host.c_str(),
                               row.w - 12, ink);
                if (!g.providers[i].notes.empty()) {
                    int nw = cv.text_width(g.providers[i].notes.c_str());
                    Color note = i == g.provider_sel ? ink : g.ap.c("menu.disable_label");
                    cv.text(row.right() - nw - 8, row.y + 2, g.providers[i].notes.c_str(),
                            note);
                }
                yy += row_h;
            }
        }
        paint_v_sbar(cv, g.provider_sbar, g.provider_scroll, g.provider_max,
                     g.provider_page, DragThumbProvider, DragArrowProvider);
        y = std::max(g.provider_list_r.bottom(), g.provider_sbar.bottom()) + 10;

        cv.text(cl.x + 12, y, "Password", g.ap.c("primary.label"));
        y += lh + 2;
        fr = {cl.x + 12, y, cl.w - 24, 24};
        paint_field(cv, g.ap, fr, std::string(g.field_pass.size(), '*').c_str(),
                    g.focus_field == 1, true);
        g.dlg_field_rs.push_back({1, fr});
        y += 30;

        if (g.captcha_visible && !g.client.captcha_image.empty()) {
            cv.text(cl.x + 12, y, "CAPTCHA", g.ap.c("primary.label"));
            y += lh + 2;
            SkinImage &img = g.client.captcha_image;
            int iw = std::min(img.w, cl.w - 24);
            int ih = std::min(img.h, 56);
            CanvasClip clip(cv, {cl.x + 12, y, iw, ih});
            cv.blit_image(img, cl.x + 12, y);
            y += ih + 6;
            fr = {cl.x + 12, y, cl.w - 24, 24};
            paint_field(cv, g.ap, fr, g.field_captcha.c_str(), g.focus_field == 2, true);
            g.dlg_field_rs.push_back({2, fr});
        }
    } else if (g.dialog == DlgAddBuddy) {
        field("Buddy JID", g.field_buddy, 0, false);
    } else if (g.dialog == DlgJoinMuc) {
        field("Room (room@conference.server)", g.field_room, 0, false);
        field("Nickname", g.field_nick, 1, false);
        field("Password (optional)", g.field_room_pass, 2, true);
    } else if (g.dialog == DlgBrowseMuc) {
        // XEP-0433 search, then list + nick + optional password above OK/Cancel.
        field("Search rooms (Enter to search)", g.field_room_search, 0, false);
        int list_h = cl.h - 8 - 30 - 40 - lh - 8 - 30 - lh - 8 - (lh + 2 + 30);
        if (list_h < 80) list_h = 80;
        int box_w = cl.w - 24;
        g.browse_list_r = {cl.x + 12, y, box_w, list_h};
        int row_h = lh + 4;
        int content_h = (int)g.browse_rows.size() * row_h;
        g.browse_page = std::max(1, list_h - 4);
        g.browse_max = std::max(0, content_h - g.browse_page);
        g.browse_scroll = std::clamp(g.browse_scroll, 0, g.browse_max);
        claim_v_sbar(g.browse_list_r, g.browse_sbar, g.browse_max);
        cv.fill(g.browse_list_r, g.ap.c("list.background"));
        {
            CanvasClip clip(cv, g.browse_list_r);
            int yy = g.browse_list_r.y + 2 - g.browse_scroll;
            for (int i = 0; i < (int)g.browse_rows.size(); ++i) {
                const auto &row = g.browse_rows[i];
                Rect rr{g.browse_list_r.x + 2, yy, g.browse_list_r.w - 4, row_h};
                if (row.section) {
                    cv.text_elided(rr.x + 6, rr.y + 2, row.label.c_str(), rr.w - 12,
                                   g.ap.c("menu.disable_label"));
                } else {
                    if (i == g.browse_sel)
                        cv.fill(rr, g.ap.c("list.hilite_background"));
                    Color ink = i == g.browse_sel ? g.ap.c("list.hilite_foreground")
                                                  : g.ap.c("list.label");
                    cv.text_elided(rr.x + 6, rr.y + 2, row.label.c_str(), rr.w - 12,
                                   ink);
                }
                yy += row_h;
            }
        }
        paint_v_sbar(cv, g.browse_sbar, g.browse_scroll, g.browse_max, g.browse_page,
                     DragThumbBrowse, DragArrowBrowse);
        y = std::max(g.browse_list_r.bottom(), g.browse_sbar.bottom()) + 8;
        field("Nickname", g.field_nick, 1, false);
        field("Password (optional)", g.field_room_pass, 2, true);
    } else if (g.dialog == DlgSetTopic) {
        field("Topic", g.field_topic, 0, false);
    } else if (g.dialog == DlgInvite) {
        field("Buddy JID", g.field_invite, 0, false);
        field("Reason (optional)", g.field_invite_reason, 1, false);
    }
    Rect ok{cl.x + cl.w - 160, cl.bottom() - 36, 70, 26};
    Rect cancel{cl.x + cl.w - 80, cl.bottom() - 36, 70, 26};
    const char *ok_lab = "OK";
    if (g.dialog == DlgRegister) ok_lab = "Create";
    else if (g.dialog == DlgBrowseMuc) ok_lab = "Join";
    paint_button(cv, g.ap, ok, ok_lab, false, true);
    paint_button(cv, g.ap, cancel, "Cancel", false, false);
}

void paint() {
    Canvas &cv = g.canvas;
    layout();
    std::string title = "Sagrado Jabber";
    if (!g.client.jid.empty() && g.client.state == jabber::ConnState::Online)
        title = g.client.jid + " — Sagrado Jabber";
    paint_gel(cv, g.ap, {0, 0, cv.width(), cv.height()}, title.c_str(), g.focused,
              g.pressed_box, GelStyle::Main);
    // Open menu owns the bar hilite (TextEdit parity); presence popup is not a bar menu.
    int bar_hot = g.menu_hot;
    if (!g.presence_menu && g.menu_open >= 0 && g.menu_open < MenuCount)
        bar_hot = g.menu_open;
    g.menu_bar = paint_menu_bar(cv, g.ap,
                                {g.gel.client.x, g.gel.client.y, g.gel.client.w, kMenuBarH},
                                kMenuTitles, MenuCount, bar_hot);

    // Identity strip (Yahoo-shaped: you + presence + status).
    // Signed off but remembered → still “you”, click opens Sign On.
    if (g.identity_r.h > 0) {
        cv.fill(g.identity_r, g.ap.c("primary.background"));
        cv.hline(g.identity_r.x, g.identity_r.right(), g.identity_r.bottom() - 1,
                 g.ap.c("list.separator"));
        bool online = g.client.state == jabber::ConnState::Online;
        std::string nick, jid;
        jabber::Show own = jabber::Show::Unavailable;
        SkinImage *av = nullptr;
        if (online) {
            std::lock_guard<std::mutex> lock(g.client.mu);
            jid = g.client.jid;
            nick = g.client.own_nick.empty() ? jabber::jid_node(jid) : g.client.own_nick;
            own = g.client.own_show;
            if (!g.client.own_avatar.empty()) av = &g.client.own_avatar;
            if (g.status_msg.empty() && !g.status_field_focus)
                g.status_msg = g.client.own_status;
        } else {
            jid = remembered_jid();
            nick = jabber::jid_node(jid);
            if (nick.empty()) nick = jid;
        }
        std::string initials = nick.empty() ? "?" : nick.substr(0, 1);
        paint_avatar_tile(cv, g.ap, g.avatar_r, av, initials);
        Color pcol = presence_color(g.ap, own);
        cv.fill(g.presence_r, pcol);
        cv.frame(g.presence_r, g.ap.c("list.separator"));
        int tx = g.avatar_r.right() + 8;
        int tw = g.identity_r.right() - 8 - tx;
        int text_bottom =
            online && g.status_field_r.h > 0 ? g.status_field_r.y : g.identity_r.bottom();
        {
            CanvasClip clip(cv, {tx, g.identity_r.y, tw, text_bottom - g.identity_r.y});
            cv.text_elided(tx, g.identity_r.y + 6, nick.c_str(), tw, g.ap.c("primary.label"));
            std::string sub = online ? show_label(own) : "Signed off";
            if (online && !jid.empty() && nick != jabber::jid_node(jid))
                sub = jabber::jid_node(jid) + " · " + sub;
            cv.text_elided(tx, g.identity_r.y + 6 + cv.line_height(), sub.c_str(), tw,
                           g.ap.c("menu.disable_label"));
        }
        if (online && g.status_field_r.h > 0)
            paint_field(cv, g.ap, g.status_field_r,
                        g.status_msg.empty() && !g.status_field_focus
                            ? "Status message…"
                            : g.status_msg.c_str(),
                        g.status_field_focus, true);
    }

    // Roster + kit V scrollbar only when the buddy list overflows
    cv.fill(g.roster_r, g.ap.c("list.background"));
    cv.vline(g.roster_r.right() - 1, g.identity_r.y, g.roster_r.bottom(),
             g.ap.c("list.separator"));
    cv.text(g.roster_r.x + 8, g.roster_r.y + 4, "Buddies", g.ap.c("primary.label"));
    auto rows = build_roster_rows();
    int list_top = g.roster_r.y + 22;
    Rect roster_body{g.roster_r.x, list_top, g.roster_r.w,
                     g.roster_r.bottom() - list_top};
    int content_h = 0;
    for (const auto &rr : rows)
        content_h += rr.section ? kGroupHeaderH : kBuddyRowH;
    g.roster_page = std::max(1, roster_body.h);
    g.roster_max = std::max(0, content_h - g.roster_page);
    g.roster_scroll = std::clamp(g.roster_scroll, 0, g.roster_max);
    claim_v_sbar(roster_body, g.roster_sbar, g.roster_max);
    int y = list_top - g.roster_scroll;
    for (int i = 0; i < (int)rows.size(); ++i) {
        int rh = rows[i].section ? kGroupHeaderH : kBuddyRowH;
        Rect row{roster_body.x + 2, y, roster_body.w - 4, rh};
        if (row.bottom() < list_top) {
            y += rh;
            continue;
        }
        if (row.y > g.roster_r.bottom()) break;
        CanvasClip clip(cv, roster_body);
        if (rows[i].section) {
            cv.text_elided(row.x + 6, row.y + 3, rows[i].group.c_str(), row.w - 12,
                           g.ap.c("menu.disable_label"));
            y += rh;
            continue;
        }
        const auto &buddy = rows[i].buddy;
        bool online = buddy.show != jabber::Show::Unavailable;
        bool hilite =
            i == g.roster_hot ||
            (g.active_tab >= 0 && g.tabs[g.active_tab].jid == buddy.jid);
        if (hilite) cv.fill(row, g.ap.c("list.hilite_background"));
        Color ink = online ? g.ap.c("list.label") : g.ap.c("menu.disable_label");
        if (hilite) ink = g.ap.c("list.hilite_foreground");
        constexpr int kAv = 28;
        Rect av{row.x + 4, row.y + (row.h - kAv) / 2, kAv, kAv};
        std::string initials =
            buddy.name.empty() ? jabber::jid_node(buddy.jid).substr(0, 1)
                               : buddy.name.substr(0, 1);
        const SkinImage *aimg = buddy.avatar.empty() ? nullptr : &buddy.avatar;
        paint_avatar_tile(cv, g.ap, av, aimg, initials);
        Rect dot{av.right() - 8, av.bottom() - 8, 8, 8};
        cv.fill(dot, presence_color(g.ap, buddy.show));
        cv.frame(dot, g.ap.c("list.separator"));
        std::string lab = buddy.name.empty() ? buddy.jid : buddy.name;
        int text_x = av.right() + 6;
        int text_w = row.right() - 4 - text_x;
        cv.text_elided(text_x, row.y + 4, lab.c_str(), text_w, ink);
        if (!buddy.status.empty()) {
            Color stink = hilite ? ink : g.ap.c("menu.disable_label");
            cv.text_elided(text_x, row.y + 4 + cv.line_height(),
                           buddy.status.c_str(), text_w, stink);
        }
        y += rh;
    }
    paint_v_sbar(cv, g.roster_sbar, g.roster_scroll, g.roster_max, g.roster_page,
                 DragThumbRoster, DragArrowRoster);

    // Tabs (label + close ×)
    cv.fill(g.tabs_r, g.ap.c("primary.background"));
    int tx = g.tabs_r.x + 4;
    for (int i = 0; i < (int)g.tabs.size(); ++i) {
        std::string lab = jabber::jid_node(g.tabs[i].jid);
        int tw = cv.text_width(lab.c_str()) + 28;
        Rect tr{tx, g.tabs_r.y + 2, tw, g.tabs_r.h - 3};
        if (i == g.active_tab)
            cv.fill(tr, g.ap.c("list.hilite_background"));
        Color ink = i == g.active_tab ? g.ap.c("list.hilite_foreground")
                                      : g.ap.c("primary.label");
        cv.text(tr.x + 8, tr.y + 3, lab.c_str(), ink);
        cv.text(tr.right() - 14, tr.y + 3, "x", ink);
        tx += tw + 4;
    }

    // Transcript (+ sticky subject for MUC) — kit soft-wrap; bar only if overflow.
    cv.fill(g.transcript_r, g.ap.c("text.background"));
    g.chat_sbar = {};
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size()) {
        std::string key = g.tabs[g.active_tab].jid;
        bool muc = g.tabs[g.active_tab].muc;
        // When MAM disco arrives after the tab opened, still pull history once.
        if (!muc) g.client.request_mam_history(key);
        else g.client.request_muc_history(key);
        std::vector<jabber::ChatLine> lines;
        std::string subject;
        {
            std::lock_guard<std::mutex> lock(g.client.mu);
            lines = g.client.chats[key];
            if (muc) {
                auto it = g.client.muc_subjects.find(key);
                if (it != g.client.muc_subjects.end()) subject = it->second;
            }
        }
        const int pad = kMsgPadX;
        const int lh = cv.line_height();
        auto row_headed = [&](int i) -> bool {
            if (i <= 0) return true;
            return !chat_same_sender(lines[size_t(i - 1)], lines[size_t(i)], muc);
        };
        auto line_block_h = [&](int i) {
            return full_block_h(cv, lines, i, muc, g.transcript_r.w, pad);
        };
        auto measure_chat = [&]() {
            int h = 0;
            for (int i = 0; i < (int)lines.size(); ++i) {
                if (lines[i].file) ensure_inline_image(lines[i].body);
                h += line_block_h(i);
            }
            return h;
        };
        // Snapshot avatars for this paint (self / 1:1 peer / MUC real JIDs).
        SkinImage own_av;
        SkinImage peer_av;
        bool have_own = false, have_peer = false;
        std::map<std::string, SkinImage> muc_avs; // nick → photo
        {
            std::lock_guard<std::mutex> lock(g.client.mu);
            if (!g.client.own_avatar.empty()) {
                own_av = g.client.own_avatar;
                have_own = true;
            }
            if (!muc) {
                auto it = g.client.roster.find(key);
                if (it != g.client.roster.end() && !it->second.avatar.empty()) {
                    peer_av = it->second.avatar;
                    have_peer = true;
                }
            } else {
                auto oit = g.client.muc_occupants.find(key);
                if (oit != g.client.muc_occupants.end()) {
                    for (const auto &o : oit->second) {
                        if (o.real_jid.empty()) continue;
                        const SkinImage *img =
                            g.client.avatar_for_bare_locked(o.real_jid);
                        if (img && !img->empty()) muc_avs[o.nick] = *img;
                    }
                }
                // Our nick in this room → own photo when present.
                auto nit = g.client.muc_nicks.find(key);
                if (have_own && nit != g.client.muc_nicks.end())
                    muc_avs[nit->second] = own_av;
            }
        }

        int top = g.transcript_r.y + 4;
        Rect body{g.transcript_r.x, top, g.transcript_r.w,
                  g.transcript_r.bottom() - top};
        int wrap_full = std::max(8, body.w - 2 * pad);
        int subject_h = 0;
        if (muc && !subject.empty()) {
            std::string sub = "Topic: " + subject;
            subject_h =
                text_content_height(layout_lines(cv, sub, wrap_full, true), lh) + 4;
        }
        int content_h = measure_chat();
        g.chat_page = std::max(1, body.h - subject_h);
        g.chat_max = std::max(0, content_h - g.chat_page);
        if (g.chat_max > 0) {
            claim_v_sbar(g.transcript_r, g.chat_sbar, g.chat_max);
            body.w = g.transcript_r.w;
            wrap_full = std::max(8, body.w - 2 * pad);
            if (muc && !subject.empty()) {
                std::string sub = "Topic: " + subject;
                subject_h =
                    text_content_height(layout_lines(cv, sub, wrap_full, true), lh) +
                    4;
            }
            content_h = measure_chat();
            g.chat_page = std::max(1, body.h - subject_h);
            g.chat_max = std::max(0, content_h - g.chat_page);
            g.chat_scroll = std::clamp(g.chat_scroll, 0, g.chat_max);
            if (g.chat_max <= 0) {
                g.transcript_r.w += g.chat_sbar.w;
                g.chat_sbar = {};
                body.w = g.transcript_r.w;
            }
        } else {
            g.chat_scroll = 0;
        }
        top = g.transcript_r.y + 4;
        if (muc && !subject.empty()) {
            std::string sub = "Topic: " + subject;
            auto sub_lines = layout_lines(cv, sub, wrap_full, true);
            int sy = top;
            for (const auto &vl : sub_lines) {
                cv.text(g.transcript_r.x + pad, sy,
                        sub.substr(vl.start, vl.len).c_str(),
                        g.ap.c("menu.disable_label"));
                sy += lh;
            }
            top = sy + 4;
            cv.hline(g.transcript_r.x + 4, g.transcript_r.right() - 4, top - 2,
                     g.ap.c("list.separator"));
        }
        body = {g.transcript_r.x, top, g.transcript_r.w, g.transcript_r.bottom() - top};
        g.chat_page = std::max(1, body.h);
        g.chat_max = std::max(0, content_h - g.chat_page);
        if (g.mam_pending_prepend > 0) {
            int n = std::min(g.mam_pending_prepend, (int)lines.size());
            int add_h = 0;
            for (int i = 0; i < n; ++i) add_h += line_block_h(i);
            g.chat_scroll += add_h;
            if (g.chat_sel >= 0) g.chat_sel += n;
            g.mam_pending_prepend = 0;
        }
        if (g.mam_scroll_to_end) {
            g.chat_scroll = g.chat_max;
            g.mam_scroll_to_end = false;
        }
        g.chat_scroll = std::clamp(g.chat_scroll, 0, g.chat_max);

        if (!muc && g.client.mam_loading(key)) {
            cv.text(body.x + pad, body.y + 2, "Loading older messages…",
                    g.ap.c("menu.disable_label"));
        }

        {
            CanvasClip clip(cv, body);
            int ty = body.y - g.chat_scroll;
            for (int li = 0; li < (int)lines.size(); ++li) {
                auto &ln = lines[size_t(li)];
                bool headed = row_headed(li);
                int bw = chat_body_wrap(body.w, pad, headed);
                int sep_h = date_sep_h(lines, li, lh);
                int block_h = full_block_h(cv, lines, li, muc, body.w, pad);
                int gap = headed ? kMsgGap : kMsgGapCont;
                int content_block = block_h - sep_h - gap;

                if (sep_h > 0) {
                    std::string label = date_sep_label(ln.when);
                    int label_w = cv.text_width(label.c_str());
                    int center = body.x + body.w / 2;
                    int rule_y = ty + sep_h / 2;
                    int rule_gap = 8;
                    cv.hline(body.x + pad, center - label_w / 2 - rule_gap, rule_y,
                             g.ap.c("list.separator"));
                    cv.hline(center + label_w / 2 + rule_gap, body.right() - pad,
                             rule_y, g.ap.c("list.separator"));
                    if (!label.empty())
                        cv.text(center - label_w / 2, ty + (sep_h - lh) / 2,
                                label.c_str(), g.ap.c("menu.disable_label"));
                }
                ty += sep_h;

                if (li == g.chat_sel && !ln.system &&
                    ty + content_block > body.y && ty < body.bottom()) {
                    Rect hilite{body.x + 2, ty, body.w - 4, content_block};
                    cv.fill(hilite, g.ap.c("list.hilite_background"));
                }

                int text_x = body.x + pad + kMsgAvatar + kMsgAvatarGap;
                int row_y = ty;
                Color tbg = g.ap.c("text.background");

                if (ln.system) {
                    Color ink = g.ap.c("menu.disable_label");
                    auto vlines = layout_lines(cv, ln.body, wrap_full, true);
                    for (const auto &vl : vlines) {
                        if (row_y + lh > body.y && row_y < body.bottom())
                            cv.text(body.x + pad, row_y,
                                    ln.body.substr(vl.start, vl.len).c_str(), ink);
                        row_y += lh;
                    }
                    ty += block_h - sep_h;
                    continue;
                }

                Color nick_col = xep0392_color(chat_color_key(ln, muc), tbg);
                if (headed) {
                    Rect av{body.x + pad, ty, kMsgAvatar, kMsgAvatar};
                    if (ty + kMsgAvatar > body.y && ty < body.bottom()) {
                        const SkinImage *img = nullptr;
                        if (ln.mine && have_own) {
                            img = &own_av;
                        } else if (!muc && !ln.mine && have_peer) {
                            img = &peer_av;
                        } else if (muc) {
                            auto ait = muc_avs.find(ln.from);
                            if (ait != muc_avs.end()) img = &ait->second;
                        }
                        paint_avatar_tile(cv, g.ap, av, img,
                                          chat_initials(chat_display_name(ln, muc)),
                                          img ? Color{} : nick_col);
                    }
                    std::string who = chat_display_name(ln, muc);
                    if (ln.omemo) who += " *";
                    Color nick_ink =
                        (li == g.chat_sel) ? g.ap.c("list.hilite_foreground") : nick_col;
                    if (ty + lh > body.y && ty < body.bottom()) {
                        cv.text(text_x, ty, who.c_str(), nick_ink);
                        std::string clock = format_hhmm(ln.when);
                        if (!clock.empty()) {
                            int tw = cv.text_width(clock.c_str());
                            cv.text(body.right() - pad - tw, ty, clock.c_str(),
                                    g.ap.c("menu.disable_label"));
                        }
                    }
                    row_y = ty + lh + 2;
                }

                Color ink =
                    ln.mine ? g.ap.c("text.foreground") : g.ap.c("primary.label");
                if (ln.file) ink = g.ap.c("menu.hilite_label");
                if (chat_is_action(ln)) ink = nick_col;
                if (li == g.chat_sel) ink = g.ap.c("list.hilite_foreground");
                std::string btext = chat_body_text(ln);
                SkinImage inline_image;
                int inline_state = 0;
                bool has_inline_image =
                    ln.file && inline_image_snapshot(ln.body, &inline_image, &inline_state) &&
                    inline_state == 2 && !inline_image.empty();
                int inline_w = 0, inline_h = 0;
                if (has_inline_image) inline_image_box(inline_image, bw, &inline_w, &inline_h);
                if (inline_h > 0) {
                    if (row_y + inline_h > body.y && row_y < body.bottom())
                        cv.blit_image_scaled(inline_image, text_x, row_y, inline_w,
                                            inline_h);
                    row_y += inline_h + 4;
                } else {
                    auto spans = ln.system ? std::vector<StyleSpan>{}
                                           : styling_spans(btext);
                    auto vlines = layout_lines(cv, btext, bw, true);
                    for (const auto &vl : vlines) {
                        if (row_y + lh > body.y && row_y < body.bottom())
                            paint_styled_text(cv, text_x, row_y, btext, vl.start,
                                              vl.len, spans, ink,
                                              g.ap.c("menu.disable_label"),
                                              g.ap.c("list.hilite_background"));
                        row_y += lh;
                    }
                }
                if (!ln.reactions.empty()) {
                    Color rink = (li == g.chat_sel) ? g.ap.c("list.hilite_foreground")
                                                    : g.ap.c("menu.disable_label");
                    if (row_y + reaction_row_height(ln, lh) > body.y &&
                        row_y < body.bottom())
                        row_y += paint_reaction_row(cv, text_x, row_y, ln, rink, tbg);
                    else
                        row_y += reaction_row_height(ln, lh);
                }
                if (ln.mine && ln.delivered) {
                    if (row_y + lh > body.y && row_y < body.bottom())
                        cv.text(text_x, row_y, "Delivered",
                                (li == g.chat_sel) ? g.ap.c("list.hilite_foreground")
                                                   : g.ap.c("menu.disable_label"));
                    row_y += lh;
                }
                ty += block_h - sep_h;
            }
        }
    } else {
        g.chat_page = 1;
        g.chat_max = 0;
        g.chat_scroll = 0;
        cv.text(g.transcript_r.x + 12, g.transcript_r.y + 12,
                "Select a buddy or open a chat.", g.ap.c("menu.disable_label"));
    }
    paint_v_sbar(cv, g.chat_sbar, g.chat_scroll, g.chat_max, g.chat_page,
                 DragThumbChat, DragArrowChat);

    // Occupants
    if (g.occ_r.w > 0) {
        cv.fill(g.occ_r, g.ap.c("list.background"));
        cv.vline(g.occ_r.x, g.occ_r.y, g.occ_r.bottom(), g.ap.c("list.separator"));
        cv.text(g.occ_r.x + 6, g.occ_r.y + 4, "In room", g.ap.c("primary.label"));
        std::vector<jabber::MucOccupant> occ;
        std::map<std::string, SkinImage> occ_avs;
        Color tbg = g.ap.c("list.background");
        {
            std::lock_guard<std::mutex> lock(g.client.mu);
            occ = g.client.muc_occupants[g.tabs[g.active_tab].jid];
            for (const auto &o : occ) {
                if (o.real_jid.empty()) continue;
                const SkinImage *img = g.client.avatar_for_bare_locked(o.real_jid);
                if (img && !img->empty()) occ_avs[o.nick] = *img;
            }
            auto nit = g.client.muc_nicks.find(g.tabs[g.active_tab].jid);
            if (nit != g.client.muc_nicks.end() && !g.client.own_avatar.empty())
                occ_avs[nit->second] = g.client.own_avatar;
        }
        constexpr int kOccAv = 20;
        int oy = g.occ_r.y + 22;
        for (auto &o : occ) {
            Color ncol = xep0392_color(o.nick, tbg);
            Rect av{g.occ_r.x + 4, oy + 1, kOccAv, kOccAv};
            const SkinImage *img = nullptr;
            auto ait = occ_avs.find(o.nick);
            if (ait != occ_avs.end()) img = &ait->second;
            paint_avatar_tile(cv, g.ap, av, img, chat_initials(o.nick),
                              img ? Color{} : ncol);
            cv.text_elided(av.right() + 4, oy + 2, o.nick.c_str(),
                           g.occ_r.w - (av.right() + 4 - g.occ_r.x) - 4,
                           g.ap.c("list.label"));
            oy += std::max(kOccAv, cv.line_height()) + 6;
        }
    }

    // Peer typing (XEP-0085) — subtle line above compose, not a card.
    g.peer_typing.clear();
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size() &&
        !g.tabs[g.active_tab].muc) {
        std::lock_guard<std::mutex> lock(g.client.mu);
        auto it = g.client.chat_states.find(g.tabs[g.active_tab].jid);
        if (it != g.client.chat_states.end() && it->second == "composing")
            g.peer_typing = jabber::jid_node(g.tabs[g.active_tab].jid) + " is typing…";
    }
    if (!g.peer_typing.empty() && g.transcript_r.h > 16) {
        cv.text(g.transcript_r.x + 8, g.compose_r.y - cv.line_height() - 2,
                g.peer_typing.c_str(), g.ap.c("menu.disable_label"));
    }

    // Compose — emoji + attach + field + Send (Enter sends; Shift+Enter newline).
    cv.fill(g.compose_r, g.ap.c("primary.background"));
    cv.hline(g.compose_r.x, g.compose_r.right(), g.compose_r.y, g.ap.c("list.separator"));
    g.compose.focused = (g.dialog == DlgNone && g.focused && !g.ctx.open);
    paint_button(cv, g.ap, g.btn_compose_emoji, ":)", false, false);
    paint_text_field(cv, g.ap, g.compose_field_r, g.compose, g.compose.focused);
    paint_button(cv, g.ap, g.btn_compose_attach, "+", false, false);
    paint_button(cv, g.ap, g.btn_send, "Send", false, true);

    // Status — durable account/roster/context line (not a chat firehose).
    cv.fill(g.status_r, g.ap.c("primary.background"));
    {
        std::string line = status_bar_text();
        int sw = g.progress_r.w > 0 ? g.status_r.w - g.progress_r.w - 16 : g.status_r.w - 16;
        CanvasClip clip(cv, {g.status_r.x, g.status_r.y, sw, g.status_r.h});
        cv.text_elided(g.status_r.x + 8,
                       g.status_r.y + (g.status_r.h - cv.line_height()) / 2,
                       line.c_str(), sw - 8, g.ap.c("primary.label"));
    }
    if (g.file_progress >= 0 && g.progress_r.w > 0)
        paint_progress(cv, g.ap, g.progress_r, g.file_progress, 100);

    paint_gel_grip(cv, g.ap, g.gel.grip, g.focused);

    if (g.menu_open >= 0) {
        // Presence popup from identity strip: Available / Away / Busy / Invisible.
        MenuDef md =
            g.presence_menu
                ? MenuDef{kPresenceItems, 4}
                : (g.menu_open == MenuWindow ? MenuDef{kWindowItems, 4}
                                             : kMenus[g.menu_open]);
        int scroll = 0;
        int vis_count = md.count;
        int hot = g.menu_item_hot;
        if (!g.presence_menu && g.menu_open == MenuAppearance && md.count > 0 &&
            md.items) {
            Rect win{0, 0, cv.width(), cv.height()};
            Rect anchor = g.menu_bar.item_rects[MenuAppearance];
            int max_h = std::max(kMenuItemH + 8, win.bottom() - anchor.bottom() - 4);
            int max_rows = std::max(1, (max_h - 4) / kMenuItemH);
            if (md.count > max_rows) {
                scroll = std::clamp(g.appearance_scroll, 0, md.count - max_rows);
                g.appearance_scroll = scroll;
                vis_count = max_rows;
                md = MenuDef{md.items + scroll, vis_count};
                // menu_item_hot is visible-row while Appearance is scrolled.
                if (hot < 0 || hot >= vis_count) hot = -1;
            } else {
                g.appearance_scroll = 0;
            }
        }
        int mw = 120;
        for (int i = 0; i < md.count; ++i)
            if (md.items[i][0] != '-')
                mw = std::max(mw, cv.text_width(md.items[i]) + 28);
        int mx = 0, my = 0;
        Rect win{0, 0, cv.width(), cv.height()};
        if (g.presence_menu) {
            menu_place(win, g.presence_r.w > 0 ? g.presence_r : g.avatar_r, mw,
                       menu_estimate_h(md.count), &mx, &my);
        } else if (g.menu_open == MenuWindow) {
            menu_place(win, g.gel.hatch_box, mw, menu_estimate_h(md.count), &mx, &my);
        } else {
            Rect anchor = g.menu_bar.item_rects[g.menu_open];
            menu_place(win, anchor, mw, menu_estimate_h(md.count), &mx, &my);
        }
        // menu_item_hot is in visible-row space while Appearance is scrolled.
        g.popup = paint_menu(cv, g.ap, mx, my, mw, md.items, md.count, hot);
        if (g.presence_menu) paint_presence_menu_marks(cv, g.popup);
    }

    if (g.ctx.open) {
        Rect win{0, 0, cv.width(), cv.height()};
        paint_context_menu(cv, g.ap, win, g.ctx);
    }

    paint_dialog(cv);

    if (g.about_open) {
        // Dim main gel, then kit About dialog (never MessageBox).
        for (int y = 0; y < cv.height(); ++y)
            for (int x = 0; x < cv.width(); ++x) {
                uint32_t p = cv.data()[size_t(y) * cv.width() + x];
                int r = int((p >> 16) & 255) / 2;
                int gc = int((p >> 8) & 255) / 2;
                int b = int(p & 255) / 2;
                cv.data()[size_t(y) * cv.width() + x] =
                    (uint32_t(r) << 16) | (uint32_t(gc) << 8) | uint32_t(b);
            }
        Rect box{(cv.width() - kAlertDlgW) / 2, (cv.height() - kAlertDlgH) / 2,
                 kAlertDlgW, kAlertDlgH};
        g.about_lay = paint_alert(
            cv, g.ap, box, "About Sagrado Jabber",
            "Sagrado Jabber\n"
            "1.0\n\n"
            "By Zadagast",
            AlertKind::Note, g.focused, g.pressed_box, g.about_ok_pressed);
    }

    if (g.sub_ask_open && !g.sub_ask_jid.empty()) {
        for (int y = 0; y < cv.height(); ++y)
            for (int x = 0; x < cv.width(); ++x) {
                uint32_t p = cv.data()[size_t(y) * cv.width() + x];
                int r = int((p >> 16) & 255) / 2;
                int gc = int((p >> 8) & 255) / 2;
                int b = int(p & 255) / 2;
                cv.data()[size_t(y) * cv.width() + x] =
                    (uint32_t(r) << 16) | (uint32_t(gc) << 8) | uint32_t(b);
            }
        const int dw = 340, dh = 160;
        Rect box{(cv.width() - dw) / 2, (cv.height() - dh) / 2, dw, dh};
        paint_gel(cv, g.ap, box, "Buddy request", true, 0, GelStyle::Dialog);
        GelLayout gl =
            gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
        Rect cl = gl.client;
        std::string who = jabber::jid_node(g.sub_ask_jid);
        std::string body = who + " wants to add you.\n\n" + g.sub_ask_jid;
        paint_wrapped_text(cv, {cl.x + 12, cl.y + 10, cl.w - 24, cl.h - 56},
                           body.c_str(), g.ap.c("primary.label"));
        g.sub_deny_r = {cl.x + cl.w - 160, cl.bottom() - 36, 70, 26};
        g.sub_accept_r = {cl.x + cl.w - 80, cl.bottom() - 36, 70, 26};
        paint_button(cv, g.ap, g.sub_deny_r, "Deny", false, false);
        paint_button(cv, g.ap, g.sub_accept_r, "Accept", false, true);
    }

    if (g.muc_invite_open && !g.muc_invite_room.empty()) {
        for (int y = 0; y < cv.height(); ++y)
            for (int x = 0; x < cv.width(); ++x) {
                uint32_t p = cv.data()[size_t(y) * cv.width() + x];
                int r = int((p >> 16) & 255) / 2;
                int gc = int((p >> 8) & 255) / 2;
                int b = int(p & 255) / 2;
                cv.data()[size_t(y) * cv.width() + x] =
                    (uint32_t(r) << 16) | (uint32_t(gc) << 8) | uint32_t(b);
            }
        const int dw = 340, dh = 170;
        Rect box{(cv.width() - dw) / 2, (cv.height() - dh) / 2, dw, dh};
        paint_gel(cv, g.ap, box, "Chat room invite", true, 0, GelStyle::Dialog);
        GelLayout gl =
            gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
        Rect cl = gl.client;
        std::string who = jabber::jid_node(g.muc_invite_from);
        std::string body = who + " invited you to " +
                           jabber::jid_node(g.muc_invite_room) + ".\n\n" +
                           g.muc_invite_room;
        if (!g.muc_invite_reason.empty())
            body += "\n\n\"" + g.muc_invite_reason + "\"";
        paint_wrapped_text(cv, {cl.x + 12, cl.y + 10, cl.w - 24, cl.h - 56},
                           body.c_str(), g.ap.c("primary.label"));
        g.muc_invite_decline_r = {cl.x + cl.w - 160, cl.bottom() - 36, 70, 26};
        g.muc_invite_accept_r = {cl.x + cl.w - 80, cl.bottom() - 36, 70, 26};
        paint_button(cv, g.ap, g.muc_invite_decline_r, "Decline", false, false);
        paint_button(cv, g.ap, g.muc_invite_accept_r, "Accept", false, true);
    }
}

void close_menu() {
    g.menu_open = -1;
    g.menu_item_hot = -1;
    g.menu_hot = -1;
    g.presence_menu = false;
}

void run_menu(int menu, int row) {
    bool from_presence = g.presence_menu;
    close_menu();
    if (from_presence) {
        if (row == 0) g.client.set_show(jabber::Show::Chat);
        else if (row == 1) g.client.set_show(jabber::Show::Away);
        else if (row == 2) g.client.set_show(jabber::Show::Dnd);
        else if (row == 3) g.client.set_show(jabber::Show::Unavailable);
        redraw();
        return;
    }
    if (menu == MenuWindow) {
        if (row == 0) ShowWindow(g.hwnd, SW_MINIMIZE);
        else if (row == 1)
            sagrado::window_zoom_toggle(g.hwnd, g.zoom);
        else if (row == 3) hide_to_tray();
        return;
    }
    if (menu == MenuFile) {
        if (row == 0) {
            open_sign_on();
        } else if (row == 1) {
            if (g.providers.empty()) load_providers();
            g.dialog = DlgRegister;
            g.focus_field = 0;
            g.captcha_visible = false;
            g.field_captcha.clear();
            g.provider_scroll = 0;
            if (g.provider_sel < 0 || g.provider_sel >= (int)g.providers.size())
                select_provider(0);
            else
                select_provider(g.provider_sel);
        } else if (row == 2) {
            g.client.disconnect();
            set_status("Signed off");
        } else if (row == 4) {
            quit_app();
            return;
        }
    } else if (menu == MenuBuddy) {
        if (row == 0) {
            g.dialog = DlgAddBuddy;
            g.focus_field = 0;
        } else if (row == 1) {
            std::string jid = selected_buddy_jid();
            if (jid.empty()) {
                set_status("Select a buddy or open their chat first");
            } else {
                g.client.remove_buddy(jid);
                close_tab_jid(jid);
                set_status("Removed " + jabber::jid_node(jid));
            }
        } else if (row == 2 || row == 3) {
            // XEP-0191 — block / unblock the selected buddy server-side.
            std::string jid = selected_buddy_jid();
            if (jid.empty())
                set_status("Select a buddy or open their chat first");
            else if (row == 2)
                g.client.block_jid(jid);
            else
                g.client.unblock_jid(jid);
        } else if (row == 4) {
            pick_and_set_picture();
        } else if (row == 6)
            g.client.set_show(jabber::Show::Chat);
        else if (row == 7)
            g.client.set_show(jabber::Show::Away);
        else if (row == 8)
            g.client.set_show(jabber::Show::Dnd);
        else if (row == 9)
            g.client.set_show(jabber::Show::Unavailable);
    } else if (menu == MenuChat) {
        if (row == 0) {
            pick_and_send_file();
        } else if (row == 1) {
            open_react_dialog();
        } else if (row == 2) {
            retract_selected_message();
        } else if (row == 3) {
            if (g.client.state != jabber::ConnState::Online) {
                set_status("Sign on first");
            } else {
                open_browse_muc();
            }
        } else if (row == 4) {
            g.dialog = DlgJoinMuc;
            g.focus_field = 0;
            g.field_room_pass.clear();
            if (g.field_nick.empty() && !g.client.jid.empty())
                g.field_nick = jabber::jid_node(g.client.jid);
        } else if (row == 6) {
            if (!tab_is_muc(g.active_tab)) {
                set_status("Open a chat room first");
            } else {
                g.dialog = DlgSetTopic;
                g.focus_field = 0;
                g.field_topic.clear();
                {
                    std::lock_guard<std::mutex> lock(g.client.mu);
                    auto it = g.client.muc_subjects.find(g.tabs[g.active_tab].jid);
                    if (it != g.client.muc_subjects.end()) g.field_topic = it->second;
                }
            }
        } else if (row == 7) {
            if (!tab_is_muc(g.active_tab)) {
                set_status("Open a chat room first");
            } else {
                g.dialog = DlgInvite;
                g.focus_field = 0;
                g.field_invite = selected_buddy_jid();
                g.field_invite_reason.clear();
            }
        } else if (row == 8) {
            if (!tab_is_muc(g.active_tab)) {
                set_status("Leave Room is for chat rooms");
            } else {
                std::string room = g.tabs[g.active_tab].jid;
                g.client.leave_muc(room);
                close_tab_jid(room);
                set_status("Left " + jabber::jid_node(room));
            }
        } else if (row == 9) {
            if (!tab_is_muc(g.active_tab)) {
                set_status("Bookmark a chat room tab");
            } else {
                std::string room = g.tabs[g.active_tab].jid;
                std::string nick = jabber::jid_node(g.client.jid);
                {
                    std::lock_guard<std::mutex> lock(g.client.mu);
                    auto it = g.client.muc_nicks.find(room);
                    if (it != g.client.muc_nicks.end()) nick = it->second;
                }
                bool aj = g.client.bookmark_autojoin(room);
                g.client.bookmark_muc(room, jabber::jid_node(room), nick, aj);
                set_status("Bookmarked " + jabber::jid_node(room));
            }
        } else if (row == 10) {
            if (!tab_is_muc(g.active_tab)) {
                set_status("Autojoin is for chat rooms");
            } else {
                std::string room = g.tabs[g.active_tab].jid;
                bool on = !g.client.bookmark_autojoin(room);
                g.client.set_bookmark_autojoin(room, on);
                set_status(on ? "Autojoin on for " + jabber::jid_node(room)
                              : "Autojoin off for " + jabber::jid_node(room));
            }
        }
    } else if (menu == MenuAppearance) {
        // `row` is an absolute index into appearance_labels (caller adds scroll).
        const int n_skins = (int)g.bundled_skins.size();
        const int load_row = n_skins + (n_skins > 0 ? 1 : 0);
        const int stock_row = load_row + 1;
        if (row >= 0 && row < n_skins) {
            if (g.ap.load(g.bundled_skins[row].path)) {
                set_status(std::string("Appearance: ") + g.ap.skin.meta.name);
                if (sagrado::gel_host_is_visible(g.emoji_host))
                    sagrado::gel_host_invalidate(g.emoji_host);
            }
        } else if (row == load_row) {
            OPENFILENAMEA ofn{};
            char file[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g.hwnd;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = "Appearances (*.hap;*.sap)\0*.hap;*.sap\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameA(&ofn) && g.ap.load(file)) {
                set_status(std::string("Appearance: ") + g.ap.skin.meta.name);
                if (sagrado::gel_host_is_visible(g.emoji_host))
                    sagrado::gel_host_invalidate(g.emoji_host);
            }
        } else if (row == stock_row) {
            g.ap.set_skin(stock_skin());
            set_status("Stock appearance");
            if (sagrado::gel_host_is_visible(g.emoji_host))
                sagrado::gel_host_invalidate(g.emoji_host);
        }
    } else if (menu == MenuHelp) {
        g.about_open = true;
        g.about_ok_pressed = false;
    }
    redraw();
}

void dialog_ok() {
    if (g.dialog == DlgSignOn) {
        if (g.field_jid.empty() || g.field_pass.empty()) return;
        // Remember JID only after Online succeeds (see WM_JABBER_EVENT).
        g.client.store_root = exe_dir();
        g.client.sign_on(g.field_jid, g.field_pass);
        g.dialog = DlgNone;
        set_status("Signing on…");
    } else if (g.dialog == DlgRegister) {
        if (g.captcha_visible) {
            g.client.submit_register_captcha(g.field_captcha);
            set_status("Submitting CAPTCHA…");
            g.dialog = DlgNone;
            g.captcha_visible = false;
        } else {
            if (g.field_server.empty() || g.field_user.empty() || g.field_pass.empty())
                return;
            g.client.store_root = exe_dir();
            g.client.begin_register(g.field_server, g.field_user, g.field_pass);
            g.field_jid = g.field_user + "@" + g.field_server;
            g.dialog = DlgNone;
            set_status("Creating " + g.field_jid + " on " + g.field_server + "…");
        }
    } else if (g.dialog == DlgAddBuddy) {
        if (!g.field_buddy.empty()) g.client.add_buddy(g.field_buddy);
        g.dialog = DlgNone;
    } else if (g.dialog == DlgJoinMuc) {
        if (!g.field_room.empty() && !g.field_nick.empty()) {
            g.client.join_muc(g.field_room, g.field_nick, g.field_room_pass);
            open_tab(g.field_room, true);
            set_status("Joined " + jabber::jid_node(g.field_room));
        }
        g.field_room_pass.clear();
        g.dialog = DlgNone;
        maybe_show_next_muc_invite();
    } else if (g.dialog == DlgBrowseMuc) {
        // XEP-0433 — Enter in the search field looks rooms up instead of joining.
        if (g.focus_field == 0) {
            g.browse_sel = -1;
            g.browse_scroll = 0;
            g.client.search_channels(g.field_room_search);
            return;
        }
        std::string room;
        std::string nick = g.field_nick;
        if (g.browse_sel >= 0 && g.browse_sel < (int)g.browse_rows.size() &&
            !g.browse_rows[g.browse_sel].section)
            room = g.browse_rows[g.browse_sel].jid;
        if (room.empty()) {
            set_status("Pick a room first");
            return;
        }
        if (nick.empty() && !g.client.jid.empty())
            nick = jabber::jid_node(g.client.jid);
        if (nick.empty()) {
            set_status("Enter a nickname");
            return;
        }
        // Prefer bookmark nick when joining from Bookmarks.
        if (g.browse_sel >= 0 && g.browse_rows[g.browse_sel].bookmark) {
            std::lock_guard<std::mutex> lock(g.client.mu);
            for (const auto &b : g.client.muc_bookmarks) {
                if (jabber::jid_ieq(b.jid, room) && !b.nick.empty()) {
                    nick = b.nick;
                    break;
                }
            }
        }
        g.field_nick = nick;
        g.client.join_muc(room, nick, g.field_room_pass);
        open_tab(room, true);
        g.field_room_pass.clear();
        g.dialog = DlgNone;
        set_status("Joined " + jabber::jid_node(room));
    } else if (g.dialog == DlgSetTopic) {
        if (!tab_is_muc(g.active_tab)) {
            set_status("Open a chat room first");
            g.dialog = DlgNone;
        } else {
            std::string room = g.tabs[g.active_tab].jid;
            g.client.set_muc_subject(room, g.field_topic);
            set_status(g.field_topic.empty() ? "Cleared topic"
                                             : ("Topic: " + g.field_topic));
            g.dialog = DlgNone;
        }
    } else if (g.dialog == DlgInvite) {
        if (!tab_is_muc(g.active_tab)) {
            set_status("Open a chat room first");
            g.dialog = DlgNone;
        } else if (g.field_invite.empty()) {
            set_status("Enter a buddy JID");
            return;
        } else {
            std::string room = g.tabs[g.active_tab].jid;
            g.client.invite_muc(room, g.field_invite, g.field_invite_reason);
            set_status("Invited " + jabber::jid_node(g.field_invite) + " to " +
                       jabber::jid_node(room));
            g.dialog = DlgNone;
        }
    }
    redraw();
}

int menu_bar_hit(int x, int y) {
    for (int i = 0; i < g.menu_bar.count; ++i)
        if (g.menu_bar.item_rects[i].contains(x, y)) return i;
    return -1;
}

void begin_size_drag(int edge) {
    sagrado::window_zoom_clear(g.zoom);
    RECT wr{};
    GetWindowRect(g.hwnd, &wr);
    POINT pt{};
    GetCursorPos(&pt);
    g.drag = DragSize;
    g.size_edge = edge;
    g.size_anchor_x = pt.x;
    g.size_anchor_y = pt.y;
    g.size_orig_x = wr.left;
    g.size_orig_y = wr.top;
    g.size_orig_w = wr.right - wr.left;
    g.size_orig_h = wr.bottom - wr.top;
}

void apply_size_drag() {
    POINT pt{};
    GetCursorPos(&pt);
    int dx = pt.x - g.size_anchor_x;
    int dy = pt.y - g.size_anchor_y;
    int x = g.size_orig_x, y = g.size_orig_y;
    int w = g.size_orig_w, h = g.size_orig_h;
    if (g.size_edge & SizeRight) w = g.size_orig_w + dx;
    if (g.size_edge & SizeBottom) h = g.size_orig_h + dy;
    if (g.size_edge & SizeLeft) {
        w = g.size_orig_w - dx;
        x = g.size_orig_x + dx;
    }
    if (g.size_edge & SizeTop) {
        h = g.size_orig_h - dy;
        y = g.size_orig_y + dy;
    }
    if (w < kMinWinW) {
        if (g.size_edge & SizeLeft) x -= (kMinWinW - w);
        w = kMinWinW;
    }
    if (h < kMinWinH) {
        if (g.size_edge & SizeTop) y -= (kMinWinH - h);
        h = kMinWinH;
    }
    SetWindowPos(g.hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void mouse_down(int x, int y) {
    layout();
    if (g.ctx.open) {
        int row = context_menu_hit(g.ctx, x, y);
        if (row >= 0) {
            run_ctx_menu(row);
            redraw();
            return;
        }
        close_ctx_menu();
        // Fall through so the click still lands on the surface under the menu.
    }
    // Main gel X always hides to tray — even with Sign On / dialogs up.
    if (g.gel.close_box.contains(x, y)) {
        g.drag = DragClose;
        g.pressed_box = 1;
        redraw();
        return;
    }
    if (g.sub_ask_open) {
        if (g.sub_accept_r.contains(x, y)) {
            close_subscribe_ask(true);
            redraw();
            return;
        }
        if (g.sub_deny_r.contains(x, y)) {
            close_subscribe_ask(false);
            redraw();
            return;
        }
        // Gel X on the sheet = Deny (dismiss without accepting).
        const int dw = 340, dh = 160;
        Rect box{(g.canvas.width() - dw) / 2, (g.canvas.height() - dh) / 2, dw, dh};
        GelLayout gl =
            gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
        if (gl.close_box.w > 0 && gl.close_box.contains(x, y)) {
            close_subscribe_ask(false);
            redraw();
        }
        return;
    }
    if (g.muc_invite_open) {
        if (g.muc_invite_accept_r.contains(x, y)) {
            close_muc_invite_ask(true);
            redraw();
            return;
        }
        if (g.muc_invite_decline_r.contains(x, y)) {
            close_muc_invite_ask(false);
            redraw();
            return;
        }
        const int dw = 340, dh = 170;
        Rect box{(g.canvas.width() - dw) / 2, (g.canvas.height() - dh) / 2, dw, dh};
        GelLayout gl =
            gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
        if (gl.close_box.w > 0 && gl.close_box.contains(x, y)) {
            close_muc_invite_ask(false);
            redraw();
        }
        return;
    }
    if (g.about_open) {
        if (g.about_lay.btn_ok.contains(x, y) ||
            g.about_lay.gel.close_box.contains(x, y)) {
            g.about_open = false;
            g.about_ok_pressed = false;
            redraw();
        }
        return;
    }
    if (g.dialog != DlgNone) {
        int dw = 360, dh = 220;
        if (g.dialog == DlgRegister) register_dialog_size(&dw, &dh);
        else if (g.dialog == DlgSignOn) sign_on_dialog_size(&dw, &dh);
        else if (g.dialog == DlgBrowseMuc) browse_dialog_size(&dw, &dh);
        else if (g.dialog == DlgJoinMuc) join_dialog_size(&dw, &dh);
        else if (g.dialog == DlgInvite) invite_dialog_size(&dw, &dh);
        else if (g.dialog == DlgSetTopic) {
            dw = 360;
            dh = 180;
        }
        Rect box{(g.canvas.width() - dw) / 2, (g.canvas.height() - dh) / 2, dw, dh};
        GelLayout gl =
            gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
        Rect cl = gl.client;
        // Dialog gel X = Cancel (dismiss sheet), not quit.
        if (gl.close_box.w > 0 && gl.close_box.contains(x, y)) {
            if (g.captcha_visible) g.client.cancel_register_captcha();
            g.dialog = DlgNone;
            g.captcha_visible = false;
            g.field_room_pass.clear();
            maybe_show_next_muc_invite();
            redraw();
            return;
        }
        Rect ok{cl.x + cl.w - 160, cl.bottom() - 36, 70, 26};
        Rect cancel{cl.x + cl.w - 80, cl.bottom() - 36, 70, 26};
        if (ok.contains(x, y)) {
            dialog_ok();
            return;
        }
        if (cancel.contains(x, y)) {
            if (g.captcha_visible) g.client.cancel_register_captcha();
            g.dialog = DlgNone;
            g.captcha_visible = false;
            g.field_room_pass.clear();
            maybe_show_next_muc_invite();
            redraw();
            return;
        }
        int step = g.canvas.line_height() + 4;
        if (g.dialog == DlgRegister &&
            sbar_mouse_down(g.provider_sbar, x, y, g.provider_scroll, g.provider_max,
                            g.provider_page, step, DragThumbProvider,
                            DragArrowProvider))
            return;
        if (g.dialog == DlgSignOn &&
            sbar_mouse_down(g.recent_sbar, x, y, g.recent_scroll, g.recent_max,
                            g.recent_page, step, DragThumbRecent, DragArrowRecent))
            return;
        if (g.dialog == DlgBrowseMuc &&
            sbar_mouse_down(g.browse_sbar, x, y, g.browse_scroll, g.browse_max,
                            g.browse_page, step, DragThumbBrowse, DragArrowBrowse))
            return;
        if (g.dialog == DlgRegister && g.provider_list_r.contains(x, y)) {
            int row_h = g.canvas.line_height() + 4;
            int idx = (y - (g.provider_list_r.y + 2 - g.provider_scroll)) / row_h;
            if (idx >= 0 && idx < (int)g.providers.size()) {
                select_provider(idx);
                redraw();
            }
            return;
        }
        if (g.dialog == DlgSignOn && g.recent_list_r.contains(x, y)) {
            int row_h = g.canvas.line_height() + 4;
            int idx = (y - (g.recent_list_r.y + 2 - g.recent_scroll)) / row_h;
            if (idx >= 0 && idx < (int)g.recent_jids.size()) {
                select_recent_jid(idx);
                redraw();
            }
            return;
        }
        if (g.dialog == DlgBrowseMuc && g.browse_list_r.contains(x, y)) {
            int row_h = g.canvas.line_height() + 4;
            int idx = (y - (g.browse_list_r.y + 2 - g.browse_scroll)) / row_h;
            if (idx >= 0 && idx < (int)g.browse_rows.size() &&
                !g.browse_rows[idx].section) {
                g.browse_sel = idx;
                g.field_room = g.browse_rows[idx].jid;
                // Picking a room means Enter should join it, not re-search.
                if (g.focus_field == 0) g.focus_field = 1;
                if (g.browse_rows[idx].bookmark) {
                    std::lock_guard<std::mutex> lock(g.client.mu);
                    for (const auto &b : g.client.muc_bookmarks) {
                        if (jabber::jid_ieq(b.jid, g.field_room) && !b.nick.empty()) {
                            g.field_nick = b.nick;
                            break;
                        }
                    }
                }
                redraw();
            }
            return;
        }
        for (const auto &f : g.dlg_field_rs) {
            if (!f.second.contains(x, y)) continue;
            g.focus_field = f.first;
            redraw();
            return;
        }
        if (cl.contains(x, y)) {
            int n = 2;
            if (g.dialog == DlgRegister) n = g.captcha_visible ? 3 : 2;
            else if (g.dialog == DlgJoinMuc) n = 3;
            else if (g.dialog == DlgBrowseMuc) n = 3;
            else if (g.dialog == DlgAddBuddy) n = 1;
            else if (g.dialog == DlgSignOn) n = 2;
            else if (g.dialog == DlgSetTopic) n = 1;
            else if (g.dialog == DlgInvite) n = 2;
            g.focus_field = (g.focus_field + 1) % n;
            redraw();
        }
        return;
    }

    if (g.gel.grip.contains(x, y)) {
        begin_size_drag(SizeRight | SizeBottom);
        return;
    }
    const int edge = 4;
    int W = g.canvas.width(), H = g.canvas.height();
    int e = 0;
    if (x < edge) e |= SizeLeft;
    if (x >= W - edge) e |= SizeRight;
    if (y < edge) e |= SizeTop;
    if (y >= H - edge) e |= SizeBottom;
    if (e && e != SizeTop) {
        begin_size_drag(e);
        return;
    }

    if (g.menu_open >= 0) {
        int row = menu_hit_row(g.popup, x, y);
        if (row >= 0) {
            if (!g.presence_menu && g.menu_open == MenuAppearance)
                row += g.appearance_scroll;
            run_menu(g.menu_open, row);
            return;
        }
        if (g.menu_open == MenuWindow && g.gel.hatch_box.contains(x, y)) {
            close_menu();
            redraw();
            return;
        }
        int title = menu_bar_hit(x, y);
        if (title >= 0) {
            // Clicking the already-open menu's title closes it (toggle).
            if (!g.presence_menu && title == g.menu_open) {
                close_menu();
                redraw();
                return;
            }
            g.presence_menu = false;
            g.menu_open = title;
            g.menu_hot = title;
            g.menu_item_hot = -1;
            if (title == MenuAppearance) {
                rebuild_appearance_menu();
                g.appearance_scroll = 0;
            }
            redraw();
            return;
        }
        close_menu();
        redraw();
    }

    if (g.gel.hatch_box.w > 0 && g.gel.hatch_box.contains(x, y)) {
        g.menu_open = MenuWindow;
        g.menu_item_hot = -1;
        g.pressed_box = 2;
        redraw();
        return;
    }
    if (g.gel.max_box.contains(x, y)) {
        g.drag = DragMax;
        g.pressed_box = 3;
        redraw();
        return;
    }
    if (g.gel.min_box.contains(x, y)) {
        g.drag = DragMin;
        g.pressed_box = 4;
        redraw();
        return;
    }
    int title = menu_bar_hit(x, y);
    if (title >= 0) {
        g.menu_open = title;
        if (title == MenuAppearance) {
            rebuild_appearance_menu();
            g.appearance_scroll = 0;
        }
        g.menu_hot = title;
        g.menu_item_hot = -1;
        g.drag = DragMenuBar;
        redraw();
        return;
    }

    if (g.btn_send.contains(x, y) && g.active_tab >= 0) {
        send_compose();
        redraw();
        return;
    }
    if (g.btn_compose_emoji.contains(x, y) && g.active_tab >= 0) {
        open_compose_emoji();
        redraw();
        return;
    }
    if (g.btn_compose_attach.contains(x, y) && g.active_tab >= 0) {
        pick_and_send_file();
        redraw();
        return;
    }

    if (g.dialog == DlgNone &&
        text_field_mouse_down(g.compose, g.canvas, g.compose_field_r, x, y,
                              (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
        g.status_field_focus = false;
        redraw();
        return;
    }

    if (g.identity_r.h > 0 && g.identity_r.contains(x, y)) {
        if (g.client.state != jabber::ConnState::Online) {
            open_sign_on();
            redraw();
            return;
        }
        if (g.avatar_r.contains(x, y)) {
            pick_and_set_picture();
            redraw();
            return;
        }
        if (g.status_field_r.contains(x, y)) {
            g.status_field_focus = true;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                if (g.status_msg.empty()) g.status_msg = g.client.own_status;
            }
            redraw();
            return;
        }
        if (g.status_field_focus) commit_status_message();
        open_presence_menu();
        redraw();
        return;
    }
    if (g.status_field_focus) commit_status_message();

    if (sbar_mouse_down(g.chat_sbar, x, y, g.chat_scroll, g.chat_max, g.chat_page,
                        g.canvas.line_height(), DragThumbChat, DragArrowChat)) {
        if (g.chat_scroll <= 0 && g.active_tab >= 0 &&
            !g.tabs[g.active_tab].muc)
            g.client.request_mam_older(g.tabs[g.active_tab].jid);
        return;
    }
    if (sbar_mouse_down(g.roster_sbar, x, y, g.roster_scroll, g.roster_max,
                        g.roster_page, kBuddyRowH, DragThumbRoster, DragArrowRoster))
        return;

    int roster_body_w =
        g.roster_sbar.w > 0 ? g.roster_r.w - kScrollbarW : g.roster_r.w;
    Rect roster_body{g.roster_r.x, g.roster_r.y + 22, roster_body_w,
                     g.roster_r.h - 22};
    if (roster_body.contains(x, y)) {
        auto rows = build_roster_rows();
        int y0 = g.roster_r.y + 22 - g.roster_scroll;
        int yy = y0;
        for (int i = 0; i < (int)rows.size(); ++i) {
            int rh = rows[i].section ? kGroupHeaderH : kBuddyRowH;
            if (y >= yy && y < yy + rh) {
                if (!rows[i].section) {
                    open_tab(rows[i].buddy.jid, false);
                    g.roster_hot = i;
                }
                break;
            }
            yy += rh;
        }
        return;
    }

    if (g.tabs_r.contains(x, y)) {
        int tx = g.tabs_r.x + 4;
        for (int i = 0; i < (int)g.tabs.size(); ++i) {
            std::string lab = jabber::jid_node(g.tabs[i].jid);
            int tw = g.canvas.text_width(lab.c_str()) + 28;
            if (x >= tx && x < tx + tw) {
                // Close hit on the trailing "x" — hides the tab; Leave Room still
                // does MUC unavailable. 1:1 just closes the transcript tab.
                if (x >= tx + tw - 16) {
                    std::string jid = g.tabs[i].jid;
                    if (!g.tabs[i].muc) stop_typing_indicator();
                    close_tab_jid(jid);
                    redraw();
                    return;
                }
                if (g.active_tab != i) stop_typing_indicator();
                g.active_tab = i;
                g.chat_scroll = 0;
                g.chat_sel = -1;
                redraw();
                return;
            }
            tx += tw + 4;
        }
    }

    // Click a reaction pill to toggle that emoji (multi-react).
    if (g.active_tab >= 0 && g.transcript_r.contains(x, y) &&
        !(g.chat_sbar.w > 0 && g.chat_sbar.contains(x, y))) {
        std::string emoji;
        int rhit = hit_reaction_pill(x, y, &emoji);
        if (rhit >= 0 && !emoji.empty()) {
            g.chat_sel = rhit;
            std::string key = g.tabs[g.active_tab].jid;
            bool muc = g.tabs[g.active_tab].muc;
            std::string react_id;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                const auto &lines = g.client.chats[key];
                if (rhit < (int)lines.size()) react_id = lines[size_t(rhit)].react_id;
            }
            if (!react_id.empty()) {
                g.client.send_reaction(key, react_id, emoji, muc);
                set_status("Reacted");
            }
            redraw();
            return;
        }
        // Click a transcript line to select it (Chat → React…). Clicking an
        // already-selected file/image row opens the attachment (second click),
        // so a first click never launches the browser unexpectedly.
        int hit = hit_transcript_line(x, y);
        bool open_file = (hit >= 0 && hit == g.chat_sel && g.active_tab >= 0);
        if (open_file) {
            std::string key = g.tabs[g.active_tab].jid;
            std::string url;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                const auto &lines = g.client.chats[key];
                if (hit < (int)lines.size() && lines[size_t(hit)].file)
                    url = lines[size_t(hit)].body;
            }
            if (!url.empty()) ShellExecuteA(g.hwnd, "open", url.c_str(), nullptr,
                                            nullptr, SW_SHOWNORMAL);
        }
        if (hit != g.chat_sel) {
            g.chat_sel = hit;
            redraw();
        }
        return;
    }
}

void mouse_right_down(int x, int y) {
    layout();
    if (g.dialog != DlgNone || g.menu_open >= 0 || g.sub_ask_open ||
        g.muc_invite_open || g.about_open)
        return;
    if (g.compose_field_r.contains(x, y)) {
        g.compose.focused = true;
        open_compose_ctx(x, y);
        redraw();
        return;
    }
    if (g.active_tab >= 0 && g.transcript_r.contains(x, y) &&
        !(g.chat_sbar.w > 0 && g.chat_sbar.contains(x, y))) {
        int hit = hit_transcript_line(x, y);
        if (hit >= 0) g.chat_sel = hit;
        open_transcript_ctx(x, y);
        redraw();
        return;
    }
    if (g.ctx.open) {
        close_ctx_menu();
        redraw();
    }
}

void mouse_up(int x, int y) {
    if (text_field_mouse_up(g.compose)) {
        redraw();
        return;
    }
    if (g.drag == DragSize) {
        apply_size_drag();
        g.drag = DragNone;
        redraw();
        return;
    }
    if (g.drag == DragClose) {
        // Pressed on main X: release completes hide-to-tray (modern tray apps).
        if (g.gel.close_box.contains(x, y)) {
            hide_to_tray();
            return;
        }
        g.pressed_box = 0;
        g.drag = DragNone;
        redraw();
        return;
    }
    if (g.drag == DragMax) {
        if (g.gel.max_box.contains(x, y))
            sagrado::window_zoom_toggle(g.hwnd, g.zoom);
        g.pressed_box = 0;
        g.drag = DragNone;
        redraw();
        return;
    }
    if (g.drag == DragMin) {
        if (g.gel.min_box.contains(x, y)) ShowWindow(g.hwnd, SW_MINIMIZE);
        g.pressed_box = 0;
        g.drag = DragNone;
        redraw();
        return;
    }
    g.drag = DragNone;
    g.arrow_hot = ScrollArrowHot::None;
    g.arrow_dir = 0;
    g.pressed_box = 0;
}

void mouse_move(int x, int y) {
    if (g.compose.dragging) {
        text_field_mouse_move(g.compose, g.canvas, g.compose_field_r, x, y);
        redraw();
        return;
    }
    if (g.ctx.open) {
        int row = context_menu_hit(g.ctx, x, y);
        if (row != g.ctx.hot) {
            g.ctx.hot = row;
            redraw();
        }
        return;
    }
    if (g.drag == DragSize) {
        apply_size_drag();
        return;
    }
    if (g.drag == DragThumbChat) {
        sbar_thumb_drag(g.chat_sbar, y, g.chat_scroll, g.chat_max, g.chat_page);
        return;
    }
    if (g.drag == DragThumbRoster) {
        sbar_thumb_drag(g.roster_sbar, y, g.roster_scroll, g.roster_max,
                        g.roster_page);
        return;
    }
    if (g.drag == DragThumbBrowse) {
        sbar_thumb_drag(g.browse_sbar, y, g.browse_scroll, g.browse_max,
                        g.browse_page);
        return;
    }
    if (g.drag == DragThumbProvider) {
        sbar_thumb_drag(g.provider_sbar, y, g.provider_scroll, g.provider_max,
                        g.provider_page);
        return;
    }
    if (g.drag == DragThumbRecent) {
        sbar_thumb_drag(g.recent_sbar, y, g.recent_scroll, g.recent_max,
                        g.recent_page);
        return;
    }
    if (g.menu_open >= 0) {
        int row = menu_hit_row(g.popup, x, y);
        int title = menu_bar_hit(x, y);
        bool need = false;
        if (row != g.menu_item_hot) {
            g.menu_item_hot = row;
            need = true;
        }
        // Drag/click across menu-bar titles switches menus (not Window hatch).
        if (!g.presence_menu && title >= 0 && title != g.menu_open &&
            (GetKeyState(VK_LBUTTON) & 0x8000)) {
            g.menu_open = title;
            g.menu_hot = title;
            g.menu_item_hot = -1;
            if (title == MenuAppearance) {
                rebuild_appearance_menu();
                g.appearance_scroll = 0;
            }
            need = true;
        }
        if (need) redraw();
        return;
    }
    int roster_body_w =
        g.roster_sbar.w > 0 ? g.roster_r.w - kScrollbarW : g.roster_r.w;
    Rect roster_body{g.roster_r.x, g.roster_r.y + 22, roster_body_w,
                     g.roster_r.h - 22};
    if (roster_body.contains(x, y)) {
        auto rows = build_roster_rows();
        int y0 = g.roster_r.y + 22 - g.roster_scroll;
        int yy = y0;
        int idx = -1;
        for (int i = 0; i < (int)rows.size(); ++i) {
            int rh = rows[i].section ? kGroupHeaderH : kBuddyRowH;
            if (y >= yy && y < yy + rh) {
                idx = rows[i].section ? -1 : i;
                break;
            }
            yy += rh;
        }
        if (idx != g.roster_hot) {
            g.roster_hot = idx;
            redraw();
        }
    }
}

void handle_char(WPARAM wp) {
    if (g.status_field_focus && g.dialog == DlgNone && !g.about_open) {
        if (wp == 8) {
            if (!g.status_msg.empty()) g.status_msg.pop_back();
        } else if (wp == '\r') {
            commit_status_message();
        } else if (wp == 27) {
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                g.status_msg = g.client.own_status;
            }
            g.status_field_focus = false;
        } else if (wp >= 32 && wp != 127 && g.status_msg.size() < 120) {
            g.status_msg += sagrado::utf8_from_wm_char(unsigned(wp));
        }
        redraw();
        return;
    }
    if (g.dialog != DlgNone) {
        std::string *f = &g.field_jid;
        if (g.dialog == DlgSignOn) f = g.focus_field == 0 ? &g.field_jid : &g.field_pass;
        else if (g.dialog == DlgRegister) {
            if (g.focus_field == 0) f = &g.field_user;
            else if (g.focus_field == 1) f = &g.field_pass;
            else f = &g.field_captcha;
        } else if (g.dialog == DlgAddBuddy) f = &g.field_buddy;
        else if (g.dialog == DlgJoinMuc) {
            if (g.focus_field == 0) f = &g.field_room;
            else if (g.focus_field == 1) f = &g.field_nick;
            else f = &g.field_room_pass;
        } else if (g.dialog == DlgBrowseMuc) {
            if (g.focus_field == 0) f = &g.field_room_search;
            else if (g.focus_field == 1) f = &g.field_nick;
            else f = &g.field_room_pass;
        }
        else if (g.dialog == DlgSetTopic) f = &g.field_topic;
        else if (g.dialog == DlgInvite)
            f = g.focus_field == 0 ? &g.field_invite : &g.field_invite_reason;
        if (wp == 8) {
            if (!f->empty()) f->pop_back();
        } else if (wp == '\r') {
            dialog_ok();
            return;
        } else if (wp == '\t') {
            int n = 2;
            if (g.dialog == DlgRegister) n = g.captcha_visible ? 3 : 2;
            else if (g.dialog == DlgJoinMuc) n = 3;
            else if (g.dialog == DlgBrowseMuc) n = 3;
            else if (g.dialog == DlgAddBuddy) n = 1;
            else if (g.dialog == DlgSignOn) n = 2;
            else if (g.dialog == DlgSetTopic) n = 1;
            else if (g.dialog == DlgInvite) n = 2;
            g.focus_field = (g.focus_field + 1) % n;
        } else if (wp >= 32 && wp != 127) {
            *f += sagrado::utf8_from_wm_char(unsigned(wp));
        }
        redraw();
        return;
    }
    if (g.menu_open >= 0 || g.ctx.open) return;
    if (g.sub_ask_open || g.muc_invite_open || g.about_open) return;
    g.compose.focused = true;
    // Enter / Shift+Enter handled in WM_KEYDOWN (enter_sends).
    if (wp == '\r' || wp == '\n') return;
    if (text_field_char(g.compose, g.canvas, g.compose_field_r, wp, true)) {
        if (g.compose.doc.text.empty()) stop_typing_indicator();
        else bump_typing_composing();
        redraw();
    }
}

void handle_keydown(WPARAM wp) {
    if (g.dialog != DlgNone || g.menu_open >= 0 || g.sub_ask_open ||
        g.muc_invite_open || g.about_open)
        return;
    if (g.ctx.open) {
        if (wp == VK_ESCAPE) {
            close_ctx_menu();
            redraw();
        }
        return;
    }
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    g.compose.focused = true;
    if (wp == VK_RETURN && !shift) {
        send_compose();
        redraw();
        return;
    }
    if (wp == VK_UP && begin_edit_last_message()) {
        redraw();
        return;
    }
    if (wp == VK_ESCAPE && !g.correct_id.empty()) {
        g.correct_id.clear();
        g.correct_jid.clear();
        clear_compose();
        set_status("Edit cancelled");
        redraw();
        return;
    }
    if (text_field_keydown(g.compose, g.canvas, g.hwnd, wp, shift, ctrl,
                           g.compose_field_r, true)) {
        bool nav = wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP ||
                   wp == VK_DOWN || wp == VK_HOME || wp == VK_END;
        bool copyish = ctrl && (wp == 'C' || wp == 'A');
        if (!nav && !copyish) {
            if (g.compose.doc.text.empty()) stop_typing_indicator();
            else bump_typing_composing();
        }
        redraw();
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.canvas.resize(kWinW, kWinH);
        rebuild_appearance_menu();
        std::string skin = find_default_skin();
        if (!skin.empty() && g.ap.load(skin))
            set_status("Appearance: " + g.ap.skin.meta.name + " — Signed off");
        else
            g.ap.set_skin(stock_skin());
        g.client.on_event = [](const jabber::ClientEvent &e) { post_client_event(e); };
        load_emoji_pack();
        load_providers();
        load_accounts();
        if (!g.recent_jids.empty()) g.field_jid = g.recent_jids[0];
        SetTimer(hwnd, kCaretTimerId, 500, nullptr);
        g.compose.focused = true;
        return 0;
    }
    case WM_JABBER_EVENT: {
        auto *e = (jabber::ClientEvent *)lp;
        if (!e) return 0;
        // StatusText/State no longer dump into the strip — durable_status_text()
        // owns connection + roster + active chat. Flash only curated alerts below.
        if (e->type == jabber::ClientEvent::StatusText) {
            // Keep hard failures visible briefly.
            if (g.client.state == jabber::ConnState::Error ||
                e->text.find("fail") != std::string::npos ||
                e->text.find("Fail") != std::string::npos ||
                e->text.find("rejected") != std::string::npos ||
                e->text.find("not available") != std::string::npos)
                set_status(e->text);
        }
        if (e->type == jabber::ClientEvent::State &&
            g.client.state == jabber::ConnState::Online) {
            std::string jid;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                jid = g.client.jid;
            }
            if (!jid.empty()) {
                remember_jid(jid);
                g.field_jid = jid;
            }
        }
        if (e->type == jabber::ClientEvent::CaptchaReady) {
            g.captcha_visible = true;
            g.dialog = DlgRegister;
            g.focus_field = 2;
            g.field_captcha.clear();
            set_status("Solve the CAPTCHA in this window — no browser needed");
        }
        if (e->type == jabber::ClientEvent::SubscribeAsk) {
            open_subscribe_ask(e->jid);
            set_status(e->text.empty()
                           ? jabber::jid_node(e->jid) + " wants to add you"
                           : e->text);
        }
        if (e->type == jabber::ClientEvent::MucInviteAsk) {
            std::string reason;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                for (const auto &iv : g.client.pending_muc_invites) {
                    if (jabber::jid_ieq(iv.room, e->jid)) {
                        reason = iv.reason;
                        break;
                    }
                }
            }
            if (g.in_tray || !IsWindowVisible(g.hwnd))
                tray_balloon("Chat room invite",
                             jabber::jid_node(e->text) + " invited you to " +
                                 jabber::jid_node(e->jid));
            open_muc_invite_ask(e->jid, e->text, reason);
            set_status(jabber::jid_node(e->text) + " invited you to " +
                       jabber::jid_node(e->jid));
        }
        if (e->type == jabber::ClientEvent::ChatState) {
            // Peer composing/paused — paint reads client.chat_states.
            (void)e;
        }
        if (e->type == jabber::ClientEvent::Message) {
            bool muc = false;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                muc = g.client.muc_joined.count(e->jid) != 0;
            }
            open_tab(e->jid, muc);
            if (!e->text.empty() && !muc) {
                ding();
                set_status("You've got mail from " + jabber::jid_node(e->jid));
                if (g.in_tray || !IsWindowVisible(g.hwnd))
                    tray_balloon("You've Got Mail",
                                 jabber::jid_node(e->jid) + ": " + e->text);
            }
        }
        if (e->type == jabber::ClientEvent::State ||
            e->type == jabber::ClientEvent::Identity)
            tray_update_tip();
        if (e->type == jabber::ClientEvent::MucSubject) {
            open_tab(e->jid, true);
        }
        if (e->type == jabber::ClientEvent::MucRooms ||
            e->type == jabber::ClientEvent::Bookmarks) {
            if (g.dialog == DlgBrowseMuc) rebuild_browse_rows();
        }
        if (e->type == jabber::ClientEvent::RegisterOk)
            set_status("Account created — signed on");
        if (e->type == jabber::ClientEvent::FileProgress) {
            g.file_progress = e->progress;
            if (!e->text.empty()) set_status(e->text);
            if (e->progress >= 100) g.file_progress = -1;
        }
        if (e->type == jabber::ClientEvent::Identity ||
            e->type == jabber::ClientEvent::Roster ||
            e->type == jabber::ClientEvent::Presence) {
            if (!g.status_field_focus) {
                std::lock_guard<std::mutex> lock(g.client.mu);
                g.status_msg = g.client.own_status;
            }
        }
        if (e->type == jabber::ClientEvent::State &&
            g.client.state == jabber::ConnState::Disconnected) {
            g.status_msg.clear();
            g.status_field_focus = false;
            g.presence_menu = false;
            g.sub_ask_open = false;
            g.sub_ask_jid.clear();
            g.muc_invite_open = false;
            g.muc_invite_room.clear();
            g.muc_invite_from.clear();
            g.muc_invite_reason.clear();
            g.mam_scroll_to_end = false;
            g.mam_pending_prepend = 0;
            stop_typing_indicator();
        }
        if (e->type == jabber::ClientEvent::History) {
            bool active = g.active_tab >= 0 && g.active_tab < (int)g.tabs.size() &&
                          jabber::jid_ieq(g.tabs[g.active_tab].jid, e->jid);
            if (active) {
                if (e->text == "initial") {
                    g.mam_scroll_to_end = true;
                } else if (e->text.rfind("older:", 0) == 0) {
                    int n = std::atoi(e->text.c_str() + 6);
                    if (n > 0) g.mam_pending_prepend = n;
                }
            }
        }
        delete e;
        redraw();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(lp);
        mmi->ptMinTrackSize.x = kMinWinW;
        mmi->ptMinTrackSize.y = kMinWinH;
        return 0;
    }
    case WM_SIZING: {
        auto *r = reinterpret_cast<RECT *>(lp);
        int w = r->right - r->left, h = r->bottom - r->top;
        if (w < kMinWinW) {
            if (wp == WMSZ_LEFT || wp == WMSZ_TOPLEFT || wp == WMSZ_BOTTOMLEFT)
                r->left = r->right - kMinWinW;
            else
                r->right = r->left + kMinWinW;
        }
        if (h < kMinWinH) {
            if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT)
                r->top = r->bottom - kMinWinH;
            else
                r->bottom = r->top + kMinWinH;
        }
        return TRUE;
    }
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0) {
            g.canvas.resize(std::max(w, kMinWinW), std::max(h, kMinWinH));
            redraw();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint();
        blit(hwnd, g.canvas);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
        g.focused = true;
        redraw();
        return 0;
    case WM_KILLFOCUS:
        g.focused = false;
        if (g.ctx.open) close_ctx_menu();
        redraw();
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONDOWN:
        mouse_right_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (g.dialog == DlgBrowseMuc && g.browse_list_r.contains(x, y)) {
            int row_h = g.canvas.line_height() + 4;
            int idx = (y - (g.browse_list_r.y + 2 - g.browse_scroll)) / row_h;
            if (idx >= 0 && idx < (int)g.browse_rows.size() &&
                !g.browse_rows[idx].section) {
                g.browse_sel = idx;
                g.field_room = g.browse_rows[idx].jid;
                // Picking a room means Enter should join it, not re-search.
                if (g.focus_field == 0) g.focus_field = 1;
                dialog_ok();
            }
            return 0;
        }
        mouse_down(x, y);
        return 0;
    }
    case WM_LBUTTONUP:
        ReleaseCapture();
        mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        if (g.drag == DragSize || g.drag == DragThumbChat ||
            g.drag == DragThumbRoster || g.drag == DragThumbBrowse ||
            g.drag == DragThumbProvider || g.drag == DragThumbRecent ||
            (wp & MK_LBUTTON) || g.menu_open >= 0 || g.ctx.open ||
            g.compose.dragging)
            mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -24 : 24;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (!g.presence_menu && g.menu_open == MenuAppearance) {
            int step = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -3 : 3;
            int total = (int)g.appearance_ptrs.size();
            int max_rows =
                std::max(1, (g.canvas.height() - 40) / kMenuItemH);
            int max_scroll = std::max(0, total - max_rows);
            g.appearance_scroll =
                std::clamp(g.appearance_scroll + step, 0, max_scroll);
            redraw();
            return 0;
        }
        auto in_list = [&](const Rect &list, const Rect &sbar) {
            return list.contains(pt.x, pt.y) ||
                   (sbar.w > 0 && sbar.contains(pt.x, pt.y));
        };
        if (g.dialog == DlgRegister &&
            in_list(g.provider_list_r, g.provider_sbar)) {
            g.provider_scroll =
                std::clamp(g.provider_scroll + d, 0, g.provider_max);
        } else if (g.dialog == DlgSignOn &&
                   in_list(g.recent_list_r, g.recent_sbar)) {
            g.recent_scroll = std::clamp(g.recent_scroll + d, 0, g.recent_max);
        } else if (g.dialog == DlgBrowseMuc &&
                   in_list(g.browse_list_r, g.browse_sbar)) {
            g.browse_scroll = std::clamp(g.browse_scroll + d, 0, g.browse_max);
        } else if (g.roster_r.contains(pt.x, pt.y) ||
                   (g.roster_sbar.w > 0 && g.roster_sbar.contains(pt.x, pt.y)))
            g.roster_scroll = std::clamp(g.roster_scroll + d, 0, g.roster_max);
        else if (g.dialog == DlgNone) {
            bool near_top = g.chat_scroll <= 0;
            g.chat_scroll = std::clamp(g.chat_scroll + d, 0, g.chat_max);
            // Scroll up at the top → page older MAM history (1:1).
            if (d < 0 && near_top && g.active_tab >= 0 &&
                g.active_tab < (int)g.tabs.size() && !g.tabs[g.active_tab].muc)
                g.client.request_mam_older(g.tabs[g.active_tab].jid);
        }
        redraw();
        return 0;
    }
    case WM_CHAR:
        handle_char(wp);
        return 0;
    case WM_TIMER:
        if (wp == kTypingTimerId) {
            if (g.typing_sent && !g.typing_peer.empty()) {
                g.client.send_chat_state(g.typing_peer, "paused");
                g.typing_sent = false;
            }
            KillTimer(hwnd, kTypingTimerId);
            return 0;
        }
        if (wp == kStatusFlashTimerId) {
            KillTimer(hwnd, kStatusFlashTimerId);
            g.status_flash_at = 0;
            redraw();
            return 0;
        }
        if (wp == kCaretTimerId) {
            g.compose.caret_on = !g.compose.caret_on;
            if (g.dialog == DlgNone && g.focused) redraw();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (sagrado::gel_host_is_visible(g.emoji_host)) {
                close_emoji_host();
            } else if (g.ctx.open) {
                close_ctx_menu();
                redraw();
            } else if (g.sub_ask_open) {
                close_subscribe_ask(false);
                redraw();
            } else if (g.muc_invite_open) {
                close_muc_invite_ask(false);
                redraw();
            } else if (g.about_open) {
                g.about_open = false;
                redraw();
            } else if (g.dialog != DlgNone) {
                if (g.captcha_visible) g.client.cancel_register_captcha();
                g.dialog = DlgNone;
                g.captcha_visible = false;
                g.field_room_pass.clear();
                maybe_show_next_muc_invite();
                redraw();
            } else if (g.menu_open >= 0) {
                close_menu();
                redraw();
            }
            return 0;
        }
        handle_keydown(wp);
        return 0;
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        int W = g.canvas.width(), H = g.canvas.height();
        if (pt.x < 0 || pt.y < 0 || pt.x >= W || pt.y >= H) return HTNOWHERE;
        layout();
        if (g.gel.close_box.contains(pt.x, pt.y) || g.gel.max_box.contains(pt.x, pt.y) ||
            g.gel.min_box.contains(pt.x, pt.y) || g.gel.hatch_box.contains(pt.x, pt.y))
            return HTCLIENT;
        if (g.menu_open >= 0 || g.ctx.open || g.dialog != DlgNone) return HTCLIENT;
        if (g.gel.grip.contains(pt.x, pt.y)) return HTCLIENT;
        const int edge = 4;
        bool left = pt.x < edge, right = pt.x >= W - edge;
        bool top = pt.y < edge, bottom = pt.y >= H - edge;
        if (left || right || bottom || (top && (left || right))) return HTCLIENT;
        if (pt.y < kTitleH) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCCALCSIZE:
        return 0;
    case WM_CLOSE:
        // Alt+F4 / system close → tray, same as the gel X (Quit is explicit).
        hide_to_tray();
        return 0;
    case WM_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK || lp == WM_LBUTTONUP) {
            show_from_tray();
        } else if (lp == WM_RBUTTONUP) {
            tray_popup_menu();
        }
        return 0;
    case WM_DESTROY:
        tray_remove();
        sagrado::gel_host_destroy(g.emoji_host);
        g.client.disconnect();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int show) {
    g.hinst = hinst;
    // Unicode class: WM_CHAR then carries UTF-16 units, so emoji and accented
    // text can be typed / pasted into the compose field.
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SagradoJabber";
    RegisterClassW(&wc);
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN;
    g.hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Sagrado Jabber",
                             style, CW_USEDEFAULT, CW_USEDEFAULT, kWinW, kWinH,
                             nullptr, nullptr, hinst, nullptr);
    ShowWindow(g.hwnd, show);
    {
        RECT wr{};
        GetWindowRect(g.hwnd, &wr);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        SetWindowPos(g.hwnd, nullptr, wr.left, wr.top, w + 1, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(g.hwnd, nullptr, wr.left, wr.top, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    UpdateWindow(g.hwnd);
    tray_add();
    // Auto join server: remembered JID → Sign On dialog (password still asked).
    if (!g.recent_jids.empty()) {
        open_sign_on();
        set_status("Enter password to join " + g.recent_jids[0]);
        redraw();
    }
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return int(msg.wParam);
}
