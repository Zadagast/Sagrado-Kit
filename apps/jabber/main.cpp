// Sagrado Jabber — “You’ve Got Mail” IM on the Appearance Engine.
// Buddy list, presence, tabbed chats, Get an Account (XEP-0077 + CAPTCHA in gel).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "../../engine/appearance.h"
#include "../../engine/hfnt.h"
#include "xmpp/client.h"

namespace {

constexpr int kWinW = 860;
constexpr int kWinH = 560;
constexpr int kMinWinW = 420;
constexpr int kMinWinH = 280;
constexpr int kStatusH = 22;
constexpr int kRosterW = 200;
constexpr int kTabH = 22;
constexpr int kComposeH = 56;
constexpr int kTextPad = 4;

enum : UINT {
    WM_JABBER_EVENT = WM_APP + 40,
};

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
    "Add Buddy...", "-", "Available", "Away", "Busy", "Invisible",
};
static const char *kChatItems[] = {
    "Send File...", "Join Chat Room...",
};
static const char *kAppearanceItems[] = {
    "Load Appearance...", "Stock Appearance",
};
static const char *kHelpItems[] = {
    "About Sagrado Jabber",
};
static const char *kWindowItems[] = {"Minimize", "Zoom", "-", "Close"};

struct MenuDef {
    const char *const *items;
    int count;
};
static const MenuDef kMenus[MenuCount] = {
    {kFileItems, 5}, {kBuddyItems, 6}, {kChatItems, 2},
    {kAppearanceItems, 2}, {kHelpItems, 1},
};

enum Drag : int {
    DragNone = 0,
    DragClose,
    DragMax,
    DragMin,
    DragSize,
    DragMenuBar,
};
enum SizeEdge : int {
    SizeLeft = 1,
    SizeRight = 2,
    SizeTop = 4,
    SizeBottom = 8,
};

enum DialogKind { DlgNone = 0, DlgSignOn, DlgRegister, DlgAddBuddy, DlgJoinMuc };

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
    std::vector<Tab> tabs;
    int active_tab = -1;
    int roster_hot = -1;
    int roster_scroll = 0;
    int chat_scroll = 0;

    DialogKind dialog = DlgNone;
    std::string field_jid;
    std::string field_pass;
    std::string field_server = "yax.im";
    std::string field_user;
    std::string field_captcha;
    std::string field_buddy;
    std::string field_room;
    std::string field_nick;
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
    int provider_scroll = 0;
    Rect provider_list_r{};

    std::string compose;
    Rect roster_r{}, tabs_r{}, transcript_r{}, compose_r{}, status_r{};
    Rect btn_send{}, occ_r{}, progress_r{};
};

App g;

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

std::string find_default_skin() {
    std::string base = exe_dir();
    const char *cands[] = {
        "\\..\\research\\haps\\Milk Redux.hap",
        "\\research\\haps\\Milk Redux.hap",
        "\\..\\..\\research\\haps\\Milk Redux.hap",
        "\\format\\skins\\milk-redux\\milk-redux.sap",
        "\\..\\format\\skins\\milk-redux\\milk-redux.sap",
    };
    for (const char *c : cands) {
        std::string p = base + c;
        if (file_exists(p)) return p;
    }
    return {};
}

void set_status(const std::string &s) { g.status = s; }

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
    g.roster_r = {cl.x, top, kRosterW, body_h};
    int cx = g.roster_r.right();
    int cw = cl.right() - cx;
    g.tabs_r = {cx, top, cw, kTabH};
    g.compose_r = {cx, g.status_r.y - kComposeH, cw, kComposeH};
    g.transcript_r = {cx, g.tabs_r.bottom(), cw,
                      g.compose_r.y - g.tabs_r.bottom()};
    g.btn_send = {g.compose_r.right() - 72, g.compose_r.y + 8, 64,
                  g.compose_r.h - 16};
    g.progress_r = {};
    if (g.file_progress >= 0) {
        g.progress_r = {g.status_r.right() - 140, g.status_r.y + 3, 128,
                        g.status_r.h - 6};
    }
    g.occ_r = {};
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size() &&
        g.tabs[g.active_tab].muc) {
        g.occ_r = {g.transcript_r.right() - 120, g.transcript_r.y, 120,
                   g.transcript_r.h};
        g.transcript_r.w -= 120;
        g.compose_r.w -= 120;
        g.btn_send.x = g.compose_r.right() - 72;
    }
}

std::vector<jabber::Buddy> roster_sorted() {
    std::vector<jabber::Buddy> v;
    std::lock_guard<std::mutex> lock(g.client.mu);
    for (auto &kv : g.client.roster) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](const jabber::Buddy &a, const jabber::Buddy &b) {
        bool ao = a.show != jabber::Show::Unavailable;
        bool bo = b.show != jabber::Show::Unavailable;
        if (ao != bo) return ao > bo;
        return a.name < b.name;
    });
    return v;
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
            g.active_tab = i;
            g.chat_scroll = 0;
            redraw();
            return;
        }
    }
    g.tabs.push_back({bare, muc});
    g.active_tab = (int)g.tabs.size() - 1;
    g.chat_scroll = 0;
    redraw();
}

void ding() { MessageBeep(MB_OK); }

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
    Rect box{(win.w - dw) / 2, (win.h - dh) / 2, dw, dh};
    const char *title = "Sign On";
    if (g.dialog == DlgRegister) title = "Get an Account";
    if (g.dialog == DlgAddBuddy) title = "Add Buddy";
    if (g.dialog == DlgJoinMuc) title = "Join Chat Room";
    paint_gel(cv, g.ap, box, title, true, 0, GelStyle::Dialog);
    GelLayout gl = gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
    Rect cl = gl.client;
    int y = cl.y + 8;
    int lh = cv.line_height();
    auto field = [&](const char *lab, const std::string &val, int idx, bool secret) {
        cv.text(cl.x + 12, y, lab, g.ap.c("primary.label"));
        y += lh + 2;
        Rect fr{cl.x + 12, y, cl.w - 24, 24};
        paint_field(cv, g.ap, fr, secret ? std::string(val.size(), '*').c_str()
                                         : val.c_str(),
                    g.focus_field == idx, true);
        y += 30;
    };
    if (g.dialog == DlgSignOn) {
        field("JID (you@server)", g.field_jid, 0, false);
        field("Password", g.field_pass, 1, true);
    } else if (g.dialog == DlgRegister) {
        // Screen name first (AIM-shaped), then home server, then password.
        cv.text(cl.x + 12, y, "Screen name", g.ap.c("primary.label"));
        y += lh + 2;
        Rect fr{cl.x + 12, y, cl.w - 24, 24};
        paint_field(cv, g.ap, fr, g.field_user.c_str(), g.focus_field == 0, true);
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
        g.provider_list_r = {cl.x + 12, y, cl.w - 24, list_h};
        cv.fill(g.provider_list_r, g.ap.c("list.background"));
        {
            CanvasClip clip(cv, g.provider_list_r);
            int row_h = lh + 4;
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
        y = g.provider_list_r.bottom() + 10;

        cv.text(cl.x + 12, y, "Password", g.ap.c("primary.label"));
        y += lh + 2;
        fr = {cl.x + 12, y, cl.w - 24, 24};
        paint_field(cv, g.ap, fr, std::string(g.field_pass.size(), '*').c_str(),
                    g.focus_field == 1, true);
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
        }
    } else if (g.dialog == DlgAddBuddy) {
        field("Buddy JID", g.field_buddy, 0, false);
    } else if (g.dialog == DlgJoinMuc) {
        field("Room (room@conference.server)", g.field_room, 0, false);
        field("Nickname", g.field_nick, 1, false);
    }
    Rect ok{cl.x + cl.w - 160, cl.bottom() - 36, 70, 26};
    Rect cancel{cl.x + cl.w - 80, cl.bottom() - 36, 70, 26};
    paint_button(cv, g.ap, ok, g.dialog == DlgRegister ? "Create" : "OK", false,
                 true);
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
    g.menu_bar = paint_menu_bar(cv, g.ap,
                                {g.gel.client.x, g.gel.client.y, g.gel.client.w, kMenuBarH},
                                kMenuTitles, MenuCount, g.menu_hot);

    // Roster
    cv.fill(g.roster_r, g.ap.c("list.background"));
    cv.vline(g.roster_r.right() - 1, g.roster_r.y, g.roster_r.bottom(),
             g.ap.c("list.separator"));
    cv.text(g.roster_r.x + 8, g.roster_r.y + 4, "Buddies", g.ap.c("primary.label"));
    auto buddies = roster_sorted();
    int y = g.roster_r.y + 22 - g.roster_scroll;
    int lh = cv.line_height() + 6;
    for (int i = 0; i < (int)buddies.size(); ++i) {
        Rect row{g.roster_r.x + 2, y, g.roster_r.w - 4, lh};
        if (row.bottom() < g.roster_r.y + 20) {
            y += lh;
            continue;
        }
        if (row.y > g.roster_r.bottom()) break;
        bool online = buddies[i].show != jabber::Show::Unavailable;
        if (i == g.roster_hot ||
            (g.active_tab >= 0 && g.tabs[g.active_tab].jid == buddies[i].jid))
            cv.fill(row, g.ap.c("list.hilite_background"));
        Color ink = online ? g.ap.c("list.label") : g.ap.c("menu.disable_label");
        if (i == g.roster_hot) ink = g.ap.c("list.hilite_foreground");
        std::string lab = buddies[i].name.empty() ? buddies[i].jid : buddies[i].name;
        if (online) {
            lab += " (";
            lab += show_label(buddies[i].show);
            lab += ")";
        }
        CanvasClip clip(cv, g.roster_r);
        cv.text_elided(row.x + 6, row.y + 3, lab.c_str(), row.w - 12, ink);
        y += lh;
    }

    // Tabs
    cv.fill(g.tabs_r, g.ap.c("primary.background"));
    int tx = g.tabs_r.x + 4;
    for (int i = 0; i < (int)g.tabs.size(); ++i) {
        std::string lab = jabber::jid_node(g.tabs[i].jid);
        int tw = cv.text_width(lab.c_str()) + 16;
        Rect tr{tx, g.tabs_r.y + 2, tw, g.tabs_r.h - 3};
        if (i == g.active_tab)
            cv.fill(tr, g.ap.c("list.hilite_background"));
        cv.text(tr.x + 8, tr.y + 3, lab.c_str(),
                i == g.active_tab ? g.ap.c("list.hilite_foreground")
                                  : g.ap.c("primary.label"));
        tx += tw + 4;
    }

    // Transcript
    cv.fill(g.transcript_r, g.ap.c("text.background"));
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size()) {
        std::string key = g.tabs[g.active_tab].jid;
        std::vector<jabber::ChatLine> lines;
        {
            std::lock_guard<std::mutex> lock(g.client.mu);
            lines = g.client.chats[key];
        }
        CanvasClip clip(cv, g.transcript_r);
        int ty = g.transcript_r.y + 4 - g.chat_scroll;
        int clh = cv.line_height() + 2;
        for (auto &ln : lines) {
            std::string who = ln.mine ? "You" : jabber::jid_node(ln.from);
            std::string text = who + ": " + ln.body;
            Color ink = ln.mine ? g.ap.c("text.foreground") : g.ap.c("primary.label");
            if (ln.file) ink = g.ap.c("menu.hilite_label");
            cv.text_elided(g.transcript_r.x + 6, ty, text.c_str(),
                           g.transcript_r.w - 12, ink);
            ty += clh;
        }
    } else {
        cv.text(g.transcript_r.x + 12, g.transcript_r.y + 12,
                "Select a buddy or open a chat.", g.ap.c("menu.disable_label"));
    }

    // Occupants
    if (g.occ_r.w > 0) {
        cv.fill(g.occ_r, g.ap.c("list.background"));
        cv.vline(g.occ_r.x, g.occ_r.y, g.occ_r.bottom(), g.ap.c("list.separator"));
        cv.text(g.occ_r.x + 6, g.occ_r.y + 4, "In room", g.ap.c("primary.label"));
        std::vector<std::string> occ;
        {
            std::lock_guard<std::mutex> lock(g.client.mu);
            occ = g.client.muc_occupants[g.tabs[g.active_tab].jid];
        }
        int oy = g.occ_r.y + 22;
        for (auto &n : occ) {
            cv.text_elided(g.occ_r.x + 6, oy, n.c_str(), g.occ_r.w - 12,
                           g.ap.c("list.label"));
            oy += cv.line_height() + 4;
        }
    }

    // Compose
    cv.fill(g.compose_r, g.ap.c("primary.background"));
    Rect field{g.compose_r.x + 8, g.compose_r.y + 8,
               g.btn_send.x - g.compose_r.x - 16, g.compose_r.h - 16};
    paint_field(cv, g.ap, field, g.compose.c_str(), g.dialog == DlgNone, true);
    paint_button(cv, g.ap, g.btn_send, "Send", false, true);

    // Status
    cv.fill(g.status_r, g.ap.c("primary.background"));
    {
        int sw = g.progress_r.w > 0 ? g.status_r.w - g.progress_r.w - 16 : g.status_r.w - 16;
        CanvasClip clip(cv, {g.status_r.x, g.status_r.y, sw, g.status_r.h});
        cv.text(g.status_r.x + 8,
                g.status_r.y + (g.status_r.h - cv.line_height()) / 2, g.status.c_str(),
                g.ap.c("primary.label"));
    }
    if (g.file_progress >= 0 && g.progress_r.w > 0)
        paint_progress(cv, g.ap, g.progress_r, g.file_progress, 100);

    paint_gel_grip(cv, g.ap, g.gel.grip, g.focused);

    if (g.menu_open >= 0) {
        const MenuDef &md =
            g.menu_open == MenuWindow ? MenuDef{kWindowItems, 4} : kMenus[g.menu_open];
        int mw = 120;
        for (int i = 0; i < md.count; ++i)
            if (md.items[i][0] != '-')
                mw = std::max(mw, cv.text_width(md.items[i]) + 28);
        int mx = 0, my = 0;
        Rect win{0, 0, cv.width(), cv.height()};
        if (g.menu_open == MenuWindow) {
            menu_place(win, g.gel.hatch_box, mw, menu_estimate_h(md.count), &mx, &my);
        } else {
            Rect anchor = g.menu_bar.item_rects[g.menu_open];
            menu_place(win, anchor, mw, menu_estimate_h(md.count), &mx, &my);
        }
        g.popup = paint_menu(cv, g.ap, mx, my, mw, md.items, md.count, g.menu_item_hot);
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
            "You've Got Mail IM on SagradoKit — buddy list, presence, "
            "tabbed chats, HTTP Upload files.\n\n"
            "Get an Account: XEP-0077 + CAPTCHA in gel (no browser).",
            AlertKind::Note, g.focused, g.pressed_box, g.about_ok_pressed);
    }
}

void close_menu() {
    g.menu_open = -1;
    g.menu_item_hot = -1;
    g.menu_hot = -1;
}

void run_menu(int menu, int row) {
    close_menu();
    if (menu == MenuWindow) {
        if (row == 0) ShowWindow(g.hwnd, SW_MINIMIZE);
        else if (row == 1) {
            WINDOWPLACEMENT wp{};
            wp.length = sizeof(wp);
            GetWindowPlacement(g.hwnd, &wp);
            ShowWindow(g.hwnd, wp.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);
        } else if (row == 3) PostQuitMessage(0);
        return;
    }
    if (menu == MenuFile) {
        if (row == 0) {
            g.dialog = DlgSignOn;
            g.focus_field = 0;
            g.captcha_visible = false;
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
        } else if (row == 4) PostQuitMessage(0);
    } else if (menu == MenuBuddy) {
        if (row == 0) {
            g.dialog = DlgAddBuddy;
            g.focus_field = 0;
        } else if (row == 2)
            g.client.set_show(jabber::Show::Chat);
        else if (row == 3)
            g.client.set_show(jabber::Show::Away);
        else if (row == 4)
            g.client.set_show(jabber::Show::Dnd);
        else if (row == 5)
            g.client.set_show(jabber::Show::Unavailable);
    } else if (menu == MenuChat) {
        if (row == 0) {
            if (g.active_tab < 0) {
                set_status("Open a chat first");
            } else {
                OPENFILENAMEA ofn{};
                char file[MAX_PATH] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = g.hwnd;
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileNameA(&ofn)) {
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
            }
        } else if (row == 1) {
            g.dialog = DlgJoinMuc;
            g.focus_field = 0;
            if (g.field_nick.empty() && !g.client.jid.empty())
                g.field_nick = jabber::jid_node(g.client.jid);
        }
    } else if (menu == MenuAppearance) {
        if (row == 0) {
            OPENFILENAMEA ofn{};
            char file[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g.hwnd;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = "Appearances (*.hap;*.sap)\0*.hap;*.sap\0";
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameA(&ofn) && g.ap.load(file))
                set_status(std::string("Appearance: ") + g.ap.skin.meta.name);
        } else if (row == 1) {
            g.ap.set_skin(stock_skin());
            set_status("Stock appearance");
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
            g.client.join_muc(g.field_room, g.field_nick);
            open_tab(g.field_room, true);
        }
        g.dialog = DlgNone;
    }
    redraw();
}

int menu_bar_hit(int x, int y) {
    for (int i = 0; i < g.menu_bar.count; ++i)
        if (g.menu_bar.item_rects[i].contains(x, y)) return i;
    return -1;
}

void begin_size_drag(int edge) {
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
        Rect box{(g.canvas.width() - dw) / 2, (g.canvas.height() - dh) / 2, dw, dh};
        GelLayout gl =
            gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
        Rect cl = gl.client;
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
            redraw();
            return;
        }
        if (g.dialog == DlgRegister && g.provider_list_r.contains(x, y)) {
            int lh = g.canvas.line_height() + 6;
            int idx = (y - (g.provider_list_r.y + 2 - g.provider_scroll)) / lh;
            if (idx >= 0 && idx < (int)g.providers.size()) {
                select_provider(idx);
                redraw();
            }
            return;
        }
        if (cl.contains(x, y)) {
            int n = g.dialog == DlgRegister ? (g.captcha_visible ? 3 : 2) : 4;
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
            g.menu_open = title;
            g.menu_item_hot = -1;
            redraw();
            return;
        }
        close_menu();
        redraw();
    }

    if (g.gel.close_box.contains(x, y)) {
        g.drag = DragClose;
        g.pressed_box = 1;
        redraw();
        return;
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
        g.menu_hot = title;
        g.menu_item_hot = -1;
        g.drag = DragMenuBar;
        redraw();
        return;
    }

    if (g.btn_send.contains(x, y) && g.active_tab >= 0 && !g.compose.empty()) {
        auto &tab = g.tabs[g.active_tab];
        if (tab.muc)
            g.client.send_muc_message(tab.jid, g.compose);
        else
            g.client.send_message(tab.jid, g.compose);
        g.compose.clear();
        redraw();
        return;
    }

    if (g.roster_r.contains(x, y)) {
        auto buddies = roster_sorted();
        int y0 = g.roster_r.y + 22 - g.roster_scroll;
        int lh = g.canvas.line_height() + 6;
        int idx = (y - y0) / lh;
        if (idx >= 0 && idx < (int)buddies.size()) {
            open_tab(buddies[idx].jid, false);
            g.roster_hot = idx;
        }
        return;
    }

    if (g.tabs_r.contains(x, y)) {
        int tx = g.tabs_r.x + 4;
        for (int i = 0; i < (int)g.tabs.size(); ++i) {
            std::string lab = jabber::jid_node(g.tabs[i].jid);
            int tw = g.canvas.text_width(lab.c_str()) + 16;
            if (x >= tx && x < tx + tw) {
                g.active_tab = i;
                g.chat_scroll = 0;
                redraw();
                return;
            }
            tx += tw + 4;
        }
    }
}

void mouse_up(int x, int y) {
    if (g.drag == DragSize) {
        apply_size_drag();
        g.drag = DragNone;
        redraw();
        return;
    }
    if (g.drag == DragClose) {
        if (g.gel.close_box.contains(x, y)) PostQuitMessage(0);
        g.pressed_box = 0;
        g.drag = DragNone;
        redraw();
        return;
    }
    if (g.drag == DragMax) {
        if (g.gel.max_box.contains(x, y)) {
            WINDOWPLACEMENT wp{};
            wp.length = sizeof(wp);
            GetWindowPlacement(g.hwnd, &wp);
            ShowWindow(g.hwnd, wp.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);
        }
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
    g.pressed_box = 0;
}

void mouse_move(int x, int y) {
    if (g.drag == DragSize) {
        apply_size_drag();
        return;
    }
    if (g.menu_open >= 0) {
        int row = menu_hit_row(g.popup, x, y);
        if (row != g.menu_item_hot) {
            g.menu_item_hot = row;
            redraw();
        }
        return;
    }
    if (g.roster_r.contains(x, y)) {
        int y0 = g.roster_r.y + 22 - g.roster_scroll;
        int lh = g.canvas.line_height() + 6;
        int idx = (y - y0) / lh;
        if (idx != g.roster_hot) {
            g.roster_hot = idx;
            redraw();
        }
    }
}

void handle_char(WPARAM wp) {
    if (g.dialog != DlgNone) {
        std::string *f = &g.field_jid;
        if (g.dialog == DlgSignOn) f = g.focus_field == 0 ? &g.field_jid : &g.field_pass;
        else if (g.dialog == DlgRegister) {
            if (g.focus_field == 0) f = &g.field_user;
            else if (g.focus_field == 1) f = &g.field_pass;
            else f = &g.field_captcha;
        } else if (g.dialog == DlgAddBuddy) f = &g.field_buddy;
        else if (g.dialog == DlgJoinMuc)
            f = g.focus_field == 0 ? &g.field_room : &g.field_nick;
        if (wp == 8) {
            if (!f->empty()) f->pop_back();
        } else if (wp == '\r') {
            dialog_ok();
            return;
        } else if (wp == '\t') {
            int n = g.dialog == DlgRegister ? (g.captcha_visible ? 3 : 2) : 4;
            g.focus_field = (g.focus_field + 1) % n;
        } else if (wp >= 32 && wp < 127) {
            f->push_back(char(wp));
        }
        redraw();
        return;
    }
    if (g.menu_open >= 0) return;
    if (wp == 8) {
        if (!g.compose.empty()) g.compose.pop_back();
    } else if (wp == '\r') {
        if (g.active_tab >= 0 && !g.compose.empty()) {
            auto &tab = g.tabs[g.active_tab];
            if (tab.muc)
                g.client.send_muc_message(tab.jid, g.compose);
            else
                g.client.send_message(tab.jid, g.compose);
            g.compose.clear();
        }
    } else if (wp >= 32 && wp < 127) {
        g.compose.push_back(char(wp));
    }
    redraw();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.canvas.resize(kWinW, kWinH);
        std::string skin = find_default_skin();
        if (!skin.empty() && g.ap.load(skin))
            set_status("Appearance: " + g.ap.skin.meta.name + " — Signed off");
        else
            g.ap.set_skin(stock_skin());
        g.client.on_event = [](const jabber::ClientEvent &e) { post_client_event(e); };
        load_providers();
        return 0;
    }
    case WM_JABBER_EVENT: {
        auto *e = (jabber::ClientEvent *)lp;
        if (!e) return 0;
        if (e->type == jabber::ClientEvent::StatusText ||
            e->type == jabber::ClientEvent::State)
            set_status(e->text);
        if (e->type == jabber::ClientEvent::CaptchaReady) {
            g.captcha_visible = true;
            g.dialog = DlgRegister;
            g.focus_field = 2;
            g.field_captcha.clear();
            set_status("Solve the CAPTCHA in this window — no browser needed");
        }
        if (e->type == jabber::ClientEvent::Message) {
            ding();
            open_tab(e->jid, false);
            set_status("You've got mail from " + jabber::jid_node(e->jid));
        }
        if (e->type == jabber::ClientEvent::Presence) {
            std::lock_guard<std::mutex> lock(g.client.mu);
            if (!g.client.status_text.empty()) set_status(g.client.status_text);
        }
        if (e->type == jabber::ClientEvent::RegisterOk)
            set_status("Account created — signed on");
        if (e->type == jabber::ClientEvent::FileProgress) {
            g.file_progress = e->progress;
            set_status(e->text);
            if (e->progress >= 100) g.file_progress = -1;
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
        redraw();
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        if (g.drag == DragSize || (wp & MK_LBUTTON) || g.menu_open >= 0)
            mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -24 : 24;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (g.dialog == DlgRegister && g.provider_list_r.contains(pt.x, pt.y)) {
            g.provider_scroll = std::max(0, g.provider_scroll + d);
        } else if (g.roster_r.contains(pt.x, pt.y))
            g.roster_scroll = std::max(0, g.roster_scroll + d);
        else
            g.chat_scroll = std::max(0, g.chat_scroll + d);
        redraw();
        return 0;
    }
    case WM_CHAR:
        handle_char(wp);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g.about_open) {
                g.about_open = false;
                redraw();
            } else if (g.dialog != DlgNone) {
                if (g.captcha_visible) g.client.cancel_register_captcha();
                g.dialog = DlgNone;
                g.captcha_visible = false;
                redraw();
            } else if (g.menu_open >= 0) {
                close_menu();
                redraw();
            }
            return 0;
        }
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
        if (g.menu_open >= 0 || g.dialog != DlgNone) return HTCLIENT;
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
    case WM_DESTROY:
        g.client.disconnect();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int show) {
    g.hinst = hinst;
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SagradoJabber";
    RegisterClassA(&wc);
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN;
    g.hwnd = CreateWindowExA(WS_EX_APPWINDOW, wc.lpszClassName, "Sagrado Jabber",
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
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return int(msg.wParam);
}
