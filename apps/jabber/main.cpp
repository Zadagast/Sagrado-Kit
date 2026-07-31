// Sagrado Jabber — “You’ve Got Mail” IM on the Appearance Engine.
// Buddy list, presence, tabbed chats, Get an Account (XEP-0077 + CAPTCHA in gel).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
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
constexpr int kRosterW = 220;
constexpr int kIdentityH = 64;
constexpr int kAvatarSz = 40;
constexpr int kTabH = 22;
constexpr int kComposeH = 56;
constexpr int kTextPad = 4;
constexpr int kBuddyRowH = 36;
constexpr int kMaxRecentAccounts = 8; // JIDs only — never passwords

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
    "Add Buddy...", "Set Picture...", "-", "Available", "Away", "Busy", "Invisible",
};
static const char *kChatItems[] = {
    "Send File...",
    "Browse Chat Rooms...",
    "Join Chat Room...",
    "-",
    "Leave Room",
    "Bookmark Room",
    "Autojoin Room",
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
    {kFileItems, 5}, {kBuddyItems, 7}, {kChatItems, 7},
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

enum DialogKind {
    DlgNone = 0,
    DlgSignOn,
    DlgRegister,
    DlgAddBuddy,
    DlgJoinMuc,
    DlgBrowseMuc,
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

    // Recent account names (JIDs). Passwords are never stored.
    std::vector<std::string> recent_jids;
    int recent_sel = -1;
    int recent_scroll = 0;
    Rect recent_list_r{};

    // Browse Chat Rooms dialog
    int browse_sel = -1; // index into browse_rows
    int browse_scroll = 0;
    Rect browse_list_r{};
    struct BrowseRow {
        std::string jid;
        std::string label;
        bool bookmark = false;
        bool autojoin = false;
        bool section = false; // non-selectable header
    };
    std::vector<BrowseRow> browse_rows;

    std::string compose;
    std::string status_msg; // own presence <status> draft (identity field)
    bool status_field_focus = false;
    bool presence_menu = false; // popup anchored to identity strip

    Rect identity_r{}, avatar_r{}, presence_r{}, status_field_r{};
    Rect roster_r{}, tabs_r{}, transcript_r{}, compose_r{}, status_r{};
    Rect btn_send{}, occ_r{}, progress_r{};

    bool tray_added = false;
    bool in_tray = false; // window hidden; live in the notification area
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

void tray_update_tip();
void tray_balloon(const std::string &title, const std::string &body);
void open_sign_on();
void redraw();

void set_status(const std::string &s) {
    g.status = s;
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
    else if (!g.status.empty())
        tip = g.status.size() > 120 ? g.status.substr(0, 117) + "…" : g.status;
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
    g.menu_open = -1;
    g.presence_menu = false;
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
        // Full-height nick rail beside transcript + compose (no dead pad under list).
        int occ_w = 120;
        g.occ_r = {cl.right() - occ_w, g.tabs_r.bottom(), occ_w,
                   g.status_r.y - g.tabs_r.bottom()};
        g.transcript_r.w -= occ_w;
        g.compose_r.w -= occ_w;
        g.btn_send.x = g.compose_r.right() - 72;
    }
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
                       const std::string &initials) {
    cv.fill(r, ap.c("list.background"));
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
        cv.text(r.x + (r.w - tw) / 2, r.y + (r.h - cv.line_height()) / 2, t.c_str(),
                ap.c("primary.label"));
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
            if (muc) g.tabs[i].muc = true;
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
    g.browse_rows.push_back({"", "Public rooms", false, false, true});
    if (rooms.empty()) {
        std::string conf;
        {
            std::lock_guard<std::mutex> lock(g.client.mu);
            conf = g.client.conference_host;
        }
        if (conf.empty())
            g.browse_rows.push_back(
                {"", "(no chat service on this server yet)", false, false, true});
        else
            g.browse_rows.push_back({"", "(no public rooms listed)", false, false, true});
    } else {
        for (const auto &r : rooms) {
            std::string lab = r.name.empty() ? r.jid : (r.name + "  —  " + r.jid);
            g.browse_rows.push_back({r.jid, lab, false, false, false});
        }
    }
    if (g.browse_sel >= (int)g.browse_rows.size()) g.browse_sel = -1;
}

void open_browse_muc() {
    g.dialog = DlgBrowseMuc;
    g.focus_field = 0;
    g.browse_sel = -1;
    g.browse_scroll = 0;
    if (g.field_nick.empty() && !g.client.jid.empty())
        g.field_nick = jabber::jid_node(g.client.jid);
    rebuild_browse_rows();
    g.client.refresh_muc_rooms();
}

void ding() { MessageBeep(MB_OK); }

void browse_dialog_size(int *dw, int *dh) {
    *dw = 440;
    *dh = 380;
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
    Rect box{(win.w - dw) / 2, (win.h - dh) / 2, dw, dh};
    const char *title = "Sign On";
    if (g.dialog == DlgRegister) title = "Get an Account";
    if (g.dialog == DlgAddBuddy) title = "Add Buddy";
    if (g.dialog == DlgJoinMuc) title = "Join Chat Room";
    if (g.dialog == DlgBrowseMuc) title = "Browse Chat Rooms";
    paint_gel(cv, g.ap, box, title, true, 0, GelStyle::Dialog);
    GelLayout gl = gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
    Rect cl = gl.client;
    int y = cl.y + 8;
    int lh = cv.line_height();
    g.recent_list_r = {};
    g.browse_list_r = {};
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
        field("Screen name", g.field_jid, 0, false);
        // 2+ accounts → compact picker between name and password.
        if ((int)g.recent_jids.size() >= 2) {
            int rows = std::min(4, (int)g.recent_jids.size());
            int row_h = lh + 4;
            g.recent_list_r = {cl.x + 12, y, cl.w - 24, rows * row_h + 4};
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
            y = g.recent_list_r.bottom() + 8;
        }
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
    } else if (g.dialog == DlgBrowseMuc) {
        int list_h = cl.h - 8 - 30 - 40 - lh - 8;
        if (list_h < 80) list_h = 80;
        g.browse_list_r = {cl.x + 12, y, cl.w - 24, list_h};
        cv.fill(g.browse_list_r, g.ap.c("list.background"));
        {
            CanvasClip clip(cv, g.browse_list_r);
            int row_h = lh + 4;
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
        y = g.browse_list_r.bottom() + 8;
        field("Nickname", g.field_nick, 0, false);
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

    // Roster
    cv.fill(g.roster_r, g.ap.c("list.background"));
    cv.vline(g.roster_r.right() - 1, g.identity_r.y, g.roster_r.bottom(),
             g.ap.c("list.separator"));
    cv.text(g.roster_r.x + 8, g.roster_r.y + 4, "Buddies", g.ap.c("primary.label"));
    auto buddies = roster_sorted();
    int y = g.roster_r.y + 22 - g.roster_scroll;
    int lh = kBuddyRowH;
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
        if (i == g.roster_hot ||
            (g.active_tab >= 0 && g.tabs[g.active_tab].jid == buddies[i].jid))
            ink = g.ap.c("list.hilite_foreground");
        CanvasClip clip(cv, g.roster_r);
        constexpr int kAv = 28;
        Rect av{row.x + 4, row.y + (row.h - kAv) / 2, kAv, kAv};
        std::string initials =
            buddies[i].name.empty()
                ? jabber::jid_node(buddies[i].jid).substr(0, 1)
                : buddies[i].name.substr(0, 1);
        const SkinImage *aimg =
            buddies[i].avatar.empty() ? nullptr : &buddies[i].avatar;
        paint_avatar_tile(cv, g.ap, av, aimg, initials);
        Rect dot{av.right() - 8, av.bottom() - 8, 8, 8};
        cv.fill(dot, presence_color(g.ap, buddies[i].show));
        cv.frame(dot, g.ap.c("list.separator"));
        std::string lab =
            buddies[i].name.empty() ? buddies[i].jid : buddies[i].name;
        int text_x = av.right() + 6;
        int text_w = row.right() - 4 - text_x;
        cv.text_elided(text_x, row.y + 4, lab.c_str(), text_w, ink);
        if (!buddies[i].status.empty()) {
            Color stink = g.ap.c("menu.disable_label");
            if (i == g.roster_hot) stink = ink;
            cv.text_elided(text_x, row.y + 4 + cv.line_height(),
                           buddies[i].status.c_str(), text_w, stink);
        }
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

    // Transcript (+ sticky subject for MUC) — kit soft-wrap, not elide.
    cv.fill(g.transcript_r, g.ap.c("text.background"));
    if (g.active_tab >= 0 && g.active_tab < (int)g.tabs.size()) {
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
        const int pad = 6;
        const int lh = cv.line_height();
        const int wrap_w = std::max(8, g.transcript_r.w - 2 * pad);
        int top = g.transcript_r.y + 4;
        if (muc && !subject.empty()) {
            std::string sub = "Topic: " + subject;
            auto sub_lines = layout_lines(cv, sub, wrap_w, true);
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
        Rect body{g.transcript_r.x, top, g.transcript_r.w,
                  g.transcript_r.bottom() - top};
        // Measure content height, clamp scroll, then paint.
        int content_h = 0;
        for (auto &ln : lines) {
            std::string text;
            if (ln.system) text = ln.body;
            else if (muc) {
                std::string who = ln.mine ? "You" : ln.from;
                if (who.empty()) who = "?";
                text = who + ": " + ln.body;
            } else {
                std::string who = ln.mine ? "You" : jabber::jid_node(ln.from);
                text = who + ": " + ln.body;
            }
            content_h += text_content_height(layout_lines(cv, text, wrap_w, true), lh);
            content_h += 2; // gap between messages
        }
        int max_scroll = std::max(0, content_h - std::max(0, body.h));
        g.chat_scroll = std::clamp(g.chat_scroll, 0, max_scroll);

        CanvasClip clip(cv, body);
        int ty = body.y - g.chat_scroll;
        for (auto &ln : lines) {
            Color ink = ln.mine ? g.ap.c("text.foreground") : g.ap.c("primary.label");
            if (ln.file) ink = g.ap.c("menu.hilite_label");
            if (ln.system) ink = g.ap.c("menu.disable_label");
            std::string text;
            if (ln.system) {
                text = ln.body;
            } else if (muc) {
                std::string who = ln.mine ? "You" : ln.from;
                if (who.empty()) who = "?";
                text = who + ": " + ln.body;
            } else {
                std::string who = ln.mine ? "You" : jabber::jid_node(ln.from);
                text = who + ": " + ln.body;
            }
            auto vlines = layout_lines(cv, text, wrap_w, true);
            for (const auto &vl : vlines) {
                if (ty + lh > body.y && ty < body.bottom())
                    cv.text(body.x + pad, ty, text.substr(vl.start, vl.len).c_str(),
                            ink);
                ty += lh;
            }
            ty += 2;
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
        // Presence popup from identity strip: Available / Away / Busy / Invisible.
        const MenuDef md =
            g.presence_menu
                ? MenuDef{kPresenceItems, 4}
                : (g.menu_open == MenuWindow ? MenuDef{kWindowItems, 4}
                                             : kMenus[g.menu_open]);
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
        g.popup = paint_menu(cv, g.ap, mx, my, mw, md.items, md.count, g.menu_item_hot);
        if (g.presence_menu) paint_presence_menu_marks(cv, g.popup);
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
        else if (row == 1) {
            WINDOWPLACEMENT wp{};
            wp.length = sizeof(wp);
            GetWindowPlacement(g.hwnd, &wp);
            ShowWindow(g.hwnd, wp.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);
        } else if (row == 3) hide_to_tray();
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
            pick_and_set_picture();
        } else if (row == 3)
            g.client.set_show(jabber::Show::Chat);
        else if (row == 4)
            g.client.set_show(jabber::Show::Away);
        else if (row == 5)
            g.client.set_show(jabber::Show::Dnd);
        else if (row == 6)
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
            if (g.client.state != jabber::ConnState::Online) {
                set_status("Sign on first");
            } else {
                open_browse_muc();
            }
        } else if (row == 2) {
            g.dialog = DlgJoinMuc;
            g.focus_field = 0;
            if (g.field_nick.empty() && !g.client.jid.empty())
                g.field_nick = jabber::jid_node(g.client.jid);
        } else if (row == 4) {
            if (!tab_is_muc(g.active_tab)) {
                set_status("Leave Room is for chat rooms");
            } else {
                std::string room = g.tabs[g.active_tab].jid;
                g.client.leave_muc(room);
                close_tab_jid(room);
                set_status("Left " + jabber::jid_node(room));
            }
        } else if (row == 5) {
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
        } else if (row == 6) {
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
        // Remember JID only after Online succeeds (see WM_JABBER_EVENT).
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
    } else if (g.dialog == DlgBrowseMuc) {
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
        g.client.join_muc(room, nick);
        open_tab(room, true);
        g.dialog = DlgNone;
        set_status("Joined " + jabber::jid_node(room));
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
    // Main gel X always hides to tray — even with Sign On / dialogs up.
    if (g.gel.close_box.contains(x, y)) {
        g.drag = DragClose;
        g.pressed_box = 1;
        redraw();
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
        Rect box{(g.canvas.width() - dw) / 2, (g.canvas.height() - dh) / 2, dw, dh};
        GelLayout gl =
            gel_layout(box.x, box.y, box.w, box.h, GelStyle::Dialog, &g.ap, true);
        Rect cl = gl.client;
        // Dialog gel X = Cancel (dismiss sheet), not quit.
        if (gl.close_box.w > 0 && gl.close_box.contains(x, y)) {
            if (g.captcha_visible) g.client.cancel_register_captcha();
            g.dialog = DlgNone;
            g.captcha_visible = false;
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
        if (cl.contains(x, y)) {
            int n = 2;
            if (g.dialog == DlgRegister) n = g.captcha_visible ? 3 : 2;
            else if (g.dialog == DlgJoinMuc) n = 2;
            else if (g.dialog == DlgBrowseMuc) n = 1;
            else if (g.dialog == DlgAddBuddy) n = 1;
            else if (g.dialog == DlgSignOn) n = 2;
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
            g.presence_menu = false;
            g.menu_open = title;
            g.menu_hot = title;
            g.menu_item_hot = -1;
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

    if (g.roster_r.contains(x, y)) {
        auto buddies = roster_sorted();
        int y0 = g.roster_r.y + 22 - g.roster_scroll;
        int lh = kBuddyRowH;
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
            need = true;
        }
        if (need) redraw();
        return;
    }
    if (g.roster_r.contains(x, y)) {
        int y0 = g.roster_r.y + 22 - g.roster_scroll;
        int lh = kBuddyRowH;
        int idx = (y - y0) / lh;
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
        } else if (wp >= 32 && wp < 127 && g.status_msg.size() < 120) {
            g.status_msg.push_back(char(wp));
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
        }         else if (g.dialog == DlgAddBuddy) f = &g.field_buddy;
        else if (g.dialog == DlgJoinMuc)
            f = g.focus_field == 0 ? &g.field_room : &g.field_nick;
        else if (g.dialog == DlgBrowseMuc) f = &g.field_nick;
        if (wp == 8) {
            if (!f->empty()) f->pop_back();
        } else if (wp == '\r') {
            dialog_ok();
            return;
        } else if (wp == '\t') {
            int n = 2;
            if (g.dialog == DlgRegister) n = g.captcha_visible ? 3 : 2;
            else if (g.dialog == DlgJoinMuc) n = 2;
            else if (g.dialog == DlgBrowseMuc) n = 1;
            else if (g.dialog == DlgAddBuddy) n = 1;
            else if (g.dialog == DlgSignOn) n = 2;
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
        load_accounts();
        if (!g.recent_jids.empty()) g.field_jid = g.recent_jids[0];
        return 0;
    }
    case WM_JABBER_EVENT: {
        auto *e = (jabber::ClientEvent *)lp;
        if (!e) return 0;
        if (e->type == jabber::ClientEvent::StatusText ||
            e->type == jabber::ClientEvent::State)
            set_status(e->text);
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
        if (e->type == jabber::ClientEvent::Message) {
            bool muc = false;
            {
                std::lock_guard<std::mutex> lock(g.client.mu);
                muc = g.client.muc_joined.count(e->jid) != 0;
            }
            open_tab(e->jid, muc);
            if (!e->text.empty()) {
                if (!muc) {
                    ding();
                    set_status("You've got mail from " + jabber::jid_node(e->jid));
                    if (g.in_tray || !IsWindowVisible(g.hwnd))
                        tray_balloon("You've Got Mail",
                                     jabber::jid_node(e->jid) + ": " + e->text);
                } else {
                    set_status(jabber::jid_node(e->jid) + ": " + e->text);
                }
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
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (g.dialog == DlgBrowseMuc && g.browse_list_r.contains(x, y)) {
            int row_h = g.canvas.line_height() + 4;
            int idx = (y - (g.browse_list_r.y + 2 - g.browse_scroll)) / row_h;
            if (idx >= 0 && idx < (int)g.browse_rows.size() &&
                !g.browse_rows[idx].section) {
                g.browse_sel = idx;
                g.field_room = g.browse_rows[idx].jid;
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
        if (g.drag == DragSize || (wp & MK_LBUTTON) || g.menu_open >= 0)
            mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -24 : 24;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (g.dialog == DlgRegister && g.provider_list_r.contains(pt.x, pt.y)) {
            g.provider_scroll = std::max(0, g.provider_scroll + d);
        } else if (g.dialog == DlgSignOn && g.recent_list_r.contains(pt.x, pt.y)) {
            g.recent_scroll = std::max(0, g.recent_scroll + d);
        } else if (g.dialog == DlgBrowseMuc && g.browse_list_r.contains(pt.x, pt.y)) {
            g.browse_scroll = std::max(0, g.browse_scroll + d);
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
