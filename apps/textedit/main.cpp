// Sagrado TextEdit — first consumer app of the SagradoKit Appearance Engine.
// Haxial TextEdit-shaped: gel chrome, menu bar, plain-text view, Find & Replace,
// Load Appearance. Entire UI is a software framebuffer → SetDIBitsToDevice.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../engine/appearance.h"
#include "../../engine/hfnt.h"

namespace {

constexpr int kWinW = 720;
constexpr int kWinH = 520;
constexpr int kStatusH = 20;
constexpr int kTextPad = 4;

// --- Menu tables -----------------------------------------------------------

static const char *kMenuTitles[] = {"File", "Edit", "Find", "Appearance", "Help"};
enum MenuId : int { MenuFile = 0, MenuEdit, MenuFind, MenuAppearance, MenuHelp, MenuCount };

static const char *kFileItems[] = {
    "New", "Open...", "-", "Save", "Save As...", "-", "Quit",
};
static const char *kEditItems[] = {
    "Undo", "-", "Cut", "Copy", "Paste", "Clear", "-", "Select All", "Sort Lines",
};
static const char *kFindItems[] = {
    "Find...", "Find Again", "Replace...", "-", "Count Occurrences...",
};
static const char *kAppearanceItems[] = {
    "Load Appearance...", "Stock Appearance", "-", "Soft Wrap",
};
static const char *kHelpItems[] = {
    "About Sagrado TextEdit",
};

struct MenuDef {
    const char *const *items;
    int count;
};
static const MenuDef kMenus[MenuCount] = {
    {kFileItems, 7},
    {kEditItems, 9},
    {kFindItems, 5},
    {kAppearanceItems, 4},
    {kHelpItems, 1},
};

// --- Document --------------------------------------------------------------

struct VisLine {
    size_t start = 0;
    size_t len = 0;
};

struct Doc {
    std::string text;
    size_t caret = 0;
    size_t anchor = 0; // selection other end; equal → no selection
    bool dirty = false;
    std::string path;
    std::string undo_text;
    size_t undo_caret = 0;

    size_t sel_lo() const { return std::min(caret, anchor); }
    size_t sel_hi() const { return std::max(caret, anchor); }
    bool has_sel() const { return caret != anchor; }

    void push_undo() {
        undo_text = text;
        undo_caret = caret;
    }

    void undo() {
        if (undo_text == text && undo_caret == caret) return;
        std::string cur = text;
        size_t cc = caret;
        text = undo_text;
        caret = anchor = undo_caret;
        undo_text = cur;
        undo_caret = cc;
        dirty = true;
    }

    void replace_range(size_t lo, size_t hi, const std::string &ins) {
        if (lo > text.size()) lo = text.size();
        if (hi > text.size()) hi = text.size();
        if (lo > hi) std::swap(lo, hi);
        push_undo();
        text.replace(lo, hi - lo, ins);
        caret = anchor = lo + ins.size();
        dirty = true;
    }

    void insert(const std::string &s) {
        if (has_sel()) replace_range(sel_lo(), sel_hi(), s);
        else replace_range(caret, caret, s);
    }

    void backspace() {
        if (has_sel()) {
            replace_range(sel_lo(), sel_hi(), "");
            return;
        }
        if (caret == 0) return;
        replace_range(caret - 1, caret, "");
    }

    void del_forward() {
        if (has_sel()) {
            replace_range(sel_lo(), sel_hi(), "");
            return;
        }
        if (caret >= text.size()) return;
        replace_range(caret, caret + 1, "");
    }

    void select_all() {
        anchor = 0;
        caret = text.size();
    }

    std::string selected() const {
        if (!has_sel()) return {};
        return text.substr(sel_lo(), sel_hi() - sel_lo());
    }

    void sort_lines() {
        push_undo();
        std::vector<std::string> lines;
        size_t i = 0;
        while (i <= text.size()) {
            size_t nl = text.find('\n', i);
            if (nl == std::string::npos) {
                lines.push_back(text.substr(i));
                break;
            }
            std::string line = text.substr(i, nl - i);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
            i = nl + 1;
        }
        std::sort(lines.begin(), lines.end());
        std::string out;
        for (size_t n = 0; n < lines.size(); ++n) {
            if (n) out.push_back('\n');
            out += lines[n];
        }
        text = std::move(out);
        caret = anchor = 0;
        dirty = true;
    }
};

// Soft-wrap (or hard) visual lines for a given pixel width.
inline std::vector<VisLine> layout_lines(const Canvas &cv, const std::string &text,
                                         int wrap_w, bool soft_wrap) {
    std::vector<VisLine> out;
    if (wrap_w < 8) wrap_w = 8;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n || (i == n && out.empty())) {
        if (i >= n) {
            out.push_back({i, 0});
            break;
        }
        size_t line_end = text.find('\n', i);
        if (line_end == std::string::npos) line_end = n;
        size_t para = line_end; // exclusive content end (before \n)
        // Drop CR before LF
        size_t content_end = para;
        if (content_end > i && text[content_end - 1] == '\r') --content_end;

        if (!soft_wrap || wrap_w <= 0) {
            out.push_back({i, content_end - i});
            i = (para < n) ? para + 1 : n;
            if (i >= n && para < n) out.push_back({n, 0}); // trailing newline → empty line
            continue;
        }

        size_t pos = i;
        while (pos < content_end) {
            size_t start = pos;
            int w = 0;
            size_t last_break = start;
            size_t p = start;
            while (p < content_end) {
                char ch = text[p];
                char tmp[2] = {ch, 0};
                int aw = cv.text_width(tmp);
                if (w + aw > wrap_w && p > start) break;
                w += aw;
                ++p;
                if (ch == ' ' || ch == '\t') last_break = p;
            }
            if (p < content_end && last_break > start) p = last_break;
            if (p == start) p = start + 1; // at least one char
            out.push_back({start, p - start});
            pos = p;
            while (pos < content_end && text[pos] == ' ') ++pos; // skip wrap spaces
        }
        if (pos == i) out.push_back({i, 0}); // empty paragraph
        i = (para < n) ? para + 1 : n;
        if (i >= n && para < n) out.push_back({n, 0});
    }
    if (out.empty()) out.push_back({0, 0});
    return out;
}

inline int vis_index_at(const std::vector<VisLine> &lines, size_t offset) {
    for (int i = 0; i < (int)lines.size(); ++i) {
        size_t end = lines[i].start + lines[i].len;
        if (offset < end || (offset == end && i + 1 == (int)lines.size()))
            return i;
        if (offset == end && i + 1 < (int)lines.size() &&
            lines[i + 1].start > end)
            return i; // at end of wrapped segment before next
        if (offset <= lines[i].start) return i;
    }
    return (int)lines.size() - 1;
}

inline size_t offset_at_xy(const Canvas &cv, const std::vector<VisLine> &lines,
                           const std::string &text, int line, int x_in_line) {
    if (lines.empty()) return 0;
    line = std::clamp(line, 0, (int)lines.size() - 1);
    const VisLine &vl = lines[size_t(line)];
    int x = 0;
    for (size_t i = 0; i < vl.len; ++i) {
        char tmp[2] = {text[vl.start + i], 0};
        int aw = cv.text_width(tmp);
        if (x + aw / 2 >= x_in_line) return vl.start + i;
        x += aw;
    }
    return vl.start + vl.len;
}

// --- App state -------------------------------------------------------------

enum Drag : int {
    DragNone = 0,
    DragClose,
    DragMax,
    DragMin,
    DragMenuBar,
    DragThumbV,
    DragThumbH,
    DragArrowV,
    DragArrowH,
    DragText,
    DragFindClose,
    DragFindMin,
    DragFindBtn,
    DragFindTick,
    DragAboutClose,
    DragAboutMin,
    DragAboutOk,
};

enum FindFocus : int { FindFocusFind = 0, FindFocusRepl = 1 };

struct FindState {
    HWND hwnd = nullptr;
    Canvas canvas;
    GelLayout gel{};
    bool visible = false;
    bool focused = true;
    bool caret_on = true;
    int pressed_box = 0;
    int focus = FindFocusFind;
    std::string find;
    std::string repl;
    bool case_sensitive = false;
    bool stop_at_end = true;
    int pressed_btn = 0; // 1=all 2=repl 3=cancel 4=find
    int pressed_tick = 0; // 1=case 2=stop
    Rect field_find{}, field_repl{};
    Rect tick_case{}, tick_stop{};
    Rect btn_all{}, btn_repl{}, btn_cancel{}, btn_find{};
};

struct AboutState {
    HWND hwnd = nullptr;
    Canvas canvas;
    AlertLayout lay{};
    bool visible = false;
    bool focused = true;
    int pressed_box = 0;
    bool ok_pressed = false;
};

struct App {
    HWND hwnd = nullptr;
    HINSTANCE hinst = nullptr;
    Canvas canvas;
    Font font;
    Appearance ap;
    GelLayout gel{};
    MenuBarLayout menu_bar{};
    MenuLayout popup{};
    Doc doc;
    FindState find;
    AboutState about;

    bool focused = true;
    bool caret_on = true;
    bool soft_wrap = true;
    int pressed_box = 0;
    int drag = DragNone;
    int thumb_grab = 0;
    ScrollArrowHot arrow_hot = ScrollArrowHot::None;
    int arrow_dir = 0;

    int menu_hot = -1;     // hilited menu-bar title
    int menu_open = -1;    // open popup menu id, or -1
    int menu_item_hot = -1;

    std::vector<VisLine> lines;
    int scroll_y = 0; // first visible visual line
    int scroll_x = 0; // pixel offset when !soft_wrap
    Rect menu_rect{};
    Rect text_rect{};
    Rect status_rect{};
    Rect sbar_v{}, sbar_h{};
    bool show_hscroll = false;

    std::string status = "Sagrado TextEdit — first SagradoKit app";
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
        "\\..\\..\\research\\haps\\Milk Redux.hap",
        "\\research\\haps\\Milk Redux.hap",
        "\\..\\research\\haps\\Gamespot-1100.hap",
        "\\..\\..\\research\\haps\\Gamespot-1100.hap",
        "\\format\\skins\\milk-redux\\milk-redux.sap",
        "\\..\\format\\skins\\milk-redux\\milk-redux.sap",
        "\\..\\..\\format\\skins\\milk-redux\\milk-redux.sap",
        "\\format\\skins\\stock.sap",
        "\\..\\format\\skins\\stock.sap",
        "\\..\\..\\format\\skins\\stock.sap",
    };
    for (const char *rel : cands) {
        std::string p = base + rel;
        if (file_exists(p)) return p;
    }
    return {};
}

std::string window_title() {
    std::string name = g.doc.path.empty() ? "Untitled" : g.doc.path;
    auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (g.doc.dirty) name += " *";
    // Em-dash is fine: Canvas folds U+2014 onto '-' when the face lacks it.
    return name + " — Sagrado TextEdit";
}

// Keep dialog gels on the same face as the main window (stock or --font).
void sync_dialog_font(Canvas &cv) {
    if (!g.font.name.empty())
        cv.set_font(&g.font);
    else
        cv.set_font(nullptr); // stock face
}

void sync_find_font() {
    if (!g.find.hwnd) return;
    sync_dialog_font(g.find.canvas);
}

void sync_about_font() {
    if (!g.about.hwnd) return;
    sync_dialog_font(g.about.canvas);
}

void set_status(const std::string &s) { g.status = s; }

// --- Clipboard -------------------------------------------------------------

void clipboard_set(const std::string &s) {
    if (!OpenClipboard(g.hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
    if (h) {
        char *p = (char *)GlobalLock(h);
        if (p) {
            std::memcpy(p, s.c_str(), s.size() + 1);
            GlobalUnlock(h);
            SetClipboardData(CF_TEXT, h);
        }
    }
    CloseClipboard();
}

std::string clipboard_get() {
    std::string out;
    if (!OpenClipboard(g.hwnd)) return out;
    HANDLE h = GetClipboardData(CF_TEXT);
    if (h) {
        const char *p = (const char *)GlobalLock(h);
        if (p) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

// --- Layout / paint (main) -------------------------------------------------

void relayout_lines() {
    int wrap = g.text_rect.w - 2 * kTextPad;
    if (g.show_hscroll) wrap = 100000; // hard wrap off → no soft break
    if (!g.soft_wrap) wrap = 100000;
    g.lines = layout_lines(g.canvas, g.doc.text, wrap, g.soft_wrap);
    int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / g.canvas.line_height());
    int max_y = std::max(0, (int)g.lines.size() - page);
    g.scroll_y = std::clamp(g.scroll_y, 0, max_y);
}

void ensure_caret_visible() {
    if (g.lines.empty()) return;
    int li = vis_index_at(g.lines, g.doc.caret);
    int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / g.canvas.line_height());
    if (li < g.scroll_y) g.scroll_y = li;
    if (li >= g.scroll_y + page) g.scroll_y = li - page + 1;
}

void layout_main() {
    int W = g.canvas.width(), H = g.canvas.height();
    g.gel = gel_layout(0, 0, W, H, GelStyle::Main, &g.ap, g.focused);
    Rect cl = g.gel.client;
    g.menu_rect = {cl.x, cl.y, cl.w, kMenuBarH};
    g.status_rect = {cl.x, cl.bottom() - kStatusH, cl.w, kStatusH};

    g.show_hscroll = !g.soft_wrap;
    int sb = kScrollbarW;
    int text_bottom = g.status_rect.y;
    int text_right = cl.right();
    if (g.show_hscroll) text_bottom -= sb;
    text_right -= sb; // always show V scrollbar like TextEdit

    g.text_rect = {cl.x, g.menu_rect.bottom(), text_right - cl.x,
                   text_bottom - g.menu_rect.bottom()};
    g.sbar_v = {g.text_rect.right(), g.text_rect.y, sb, g.text_rect.h};
    if (g.show_hscroll)
        g.sbar_h = {g.text_rect.x, g.text_rect.bottom(), g.text_rect.w, sb};
    else
        g.sbar_h = {0, 0, 0, 0};

    relayout_lines();
}

void paint_document(Canvas &cv) {
    Appearance &ap = g.ap;
    cv.fill(g.text_rect, ap.c("text.background"));
    CanvasClip clip(cv, g.text_rect);

    const int lh = cv.line_height();
    const int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / lh);
    const size_t lo = g.doc.sel_lo();
    const size_t hi = g.doc.sel_hi();
    Color fg = ap.c("text.foreground");
    Color hi_bg = ap.c("text.hilite_background");
    Color hi_fg = ap.c("text.hilite_foreground");
    Color caret_c = ap.c("text.insertion_point");

    for (int row = 0; row < page; ++row) {
        int li = g.scroll_y + row;
        if (li < 0 || li >= (int)g.lines.size()) break;
        const VisLine &vl = g.lines[size_t(li)];
        int y = g.text_rect.y + kTextPad + row * lh;
        int x = g.text_rect.x + kTextPad - g.scroll_x;

        // Selection background per glyph
        size_t a = vl.start;
        size_t b = vl.start + vl.len;
        size_t s0 = std::max(a, lo);
        size_t s1 = std::min(b, hi);
        if (s0 < s1) {
            int x0 = x;
            for (size_t i = a; i < s0; ++i) {
                char t[2] = {g.doc.text[i], 0};
                x0 += cv.text_width(t);
            }
            int x1 = x0;
            for (size_t i = s0; i < s1; ++i) {
                char t[2] = {g.doc.text[i], 0};
                x1 += cv.text_width(t);
            }
            if (x1 == x0) x1 = x0 + 4; // empty line / end mark
            cv.fill({x0, y, x1 - x0, lh}, hi_bg);
        }

        // Draw text (split around selection for hilite ink)
        int pen = x;
        for (size_t i = a; i < b; ++i) {
            char t[2] = {g.doc.text[i], 0};
            Color ink = (i >= lo && i < hi) ? hi_fg : fg;
            cv.text(pen, y, t, ink);
            pen += cv.text_width(t);
        }

        // Caret
        if (g.focused && g.caret_on && g.menu_open < 0 && g.doc.caret >= a &&
            g.doc.caret <= b &&
            !(g.doc.caret == b && li + 1 < (int)g.lines.size() &&
              g.lines[size_t(li + 1)].start == b && g.doc.caret == b &&
              vl.len > 0 && li + 1 < (int)g.lines.size() &&
              g.doc.caret == g.lines[size_t(li + 1)].start)) {
            // Prefer the line that owns the caret start; at wrap boundary put
            // caret on the next line when caret == next.start.
            bool at_wrap_end = (g.doc.caret == b && vl.len > 0 &&
                                li + 1 < (int)g.lines.size() &&
                                g.lines[size_t(li + 1)].start == b);
            if (!at_wrap_end) {
                int cx = x;
                for (size_t i = a; i < g.doc.caret; ++i) {
                    char t[2] = {g.doc.text[i], 0};
                    cx += cv.text_width(t);
                }
                cv.vline(cx, y, y + lh, caret_c);
            }
        }
    }
}

void paint_main() {
    Canvas &cv = g.canvas;
    Appearance &ap = g.ap;
    layout_main();

    std::string title = window_title();
    paint_gel(cv, ap, {0, 0, cv.width(), cv.height()}, title.c_str(), g.focused,
              g.pressed_box, GelStyle::Main);

    // Menu bar
    unsigned wrap_check_mask = 0;
    g.menu_bar = paint_menu_bar(cv, ap, g.menu_rect, kMenuTitles, MenuCount,
                                g.menu_open >= 0 ? g.menu_open : g.menu_hot,
                                wrap_check_mask);

    // Document + scrollbars
    paint_document(cv);

    int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / cv.line_height());
    int max_y = std::max(0, (int)g.lines.size() - page);
    bool thumb_v = g.drag == DragThumbV;
    paint_scrollbar(cv, ap, g.sbar_v, g.scroll_y, max_y, page, thumb_v, false,
                    false, g.drag == DragArrowV ? g.arrow_hot : ScrollArrowHot::None);

    if (g.show_hscroll) {
        // Estimate content width from longest visual line
        int max_w = 0;
        for (const auto &vl : g.lines) {
            int w = 0;
            for (size_t i = 0; i < vl.len; ++i) {
                char t[2] = {g.doc.text[vl.start + i], 0};
                w += cv.text_width(t);
            }
            max_w = std::max(max_w, w);
        }
        int view = std::max(1, g.text_rect.w - 2 * kTextPad);
        int max_x = std::max(0, max_w - view);
        g.scroll_x = std::clamp(g.scroll_x, 0, max_x);
        paint_scrollbar_h(cv, ap, g.sbar_h, g.scroll_x, max_x, view,
                          g.drag == DragThumbH, false, false,
                          g.drag == DragArrowH ? g.arrow_hot : ScrollArrowHot::None);
    }

    // Status
    cv.fill(g.status_rect, ap.c("primary.background"));
    cv.hline(g.status_rect.x, g.status_rect.right(), g.status_rect.y,
             ap.c("primary.dark"));
    char stats[160];
    int line = vis_index_at(g.lines, g.doc.caret) + 1;
    std::snprintf(stats, sizeof(stats), "%s    Ln %d    %s    %zu chars",
                  g.status.c_str(), line, g.soft_wrap ? "Soft Wrap" : "Hard Wrap",
                  g.doc.text.size());
    cv.text(g.status_rect.x + 8,
            g.status_rect.y + (g.status_rect.h - cv.line_height()) / 2, stats,
            ap.c("primary.label"));

    // Open menu popup
    if (g.menu_open >= 0 && g.menu_open < MenuCount) {
        Rect item = g.menu_bar.item_rects[g.menu_open];
        const MenuDef &md = kMenus[g.menu_open];
        int mw = 72;
        for (int i = 0; i < md.count; ++i)
            mw = std::max(mw, cv.text_width(md.items[i]) + 28);
        // Soft Wrap checkmark via prefix
        const char *items_buf[12];
        char wrap_lab[32];
        for (int i = 0; i < md.count; ++i) items_buf[i] = md.items[i];
        if (g.menu_open == MenuAppearance) {
            std::snprintf(wrap_lab, sizeof(wrap_lab), "%s Soft Wrap",
                          g.soft_wrap ? "[x]" : "[ ]");
            items_buf[3] = wrap_lab;
        }
        g.popup = paint_menu(cv, ap, item.x, item.bottom(), mw, items_buf, md.count,
                             g.menu_item_hot);
    }

    paint_gel_grip(cv, ap, g.gel.grip, g.focused);
}

void blit(HWND hwnd, Canvas &cv) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cv.width();
    bi.bmiHeader.biHeight = -cv.height();
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC hdc = GetDC(hwnd);
    SetDIBitsToDevice(hdc, 0, 0, cv.width(), cv.height(), 0, 0, 0, cv.height(),
                      cv.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, hdc);
}

void redraw() {
    if (!g.hwnd) return;
    paint_main();
    blit(g.hwnd, g.canvas);
}

// --- Find dialog -----------------------------------------------------------

void layout_find() {
    FindState &f = g.find;
    int W = f.canvas.width(), H = f.canvas.height();
    f.gel = gel_layout(0, 0, W, H, GelStyle::Dialog, &g.ap, f.focused);
    Rect cl = f.gel.client;
    int lx = cl.x + 10;
    int fx = cl.x + 84;
    int fw = cl.right() - 14 - fx;
    if (fw < 40) fw = 40;
    f.field_find = {fx, cl.y + 10, fw, kFieldH};
    f.field_repl = {fx, cl.y + 40, fw, kFieldH};
    int cy = cl.y + 74;
    f.tick_case = {lx, cy, 160, kTickBox + 4};
    f.tick_stop = {cl.x + 190, cy, 200, kTickBox + 4};
    int by = cl.bottom() - 34;
    if (by < cl.y + 4) by = cl.y + 4;
    f.btn_all = {lx, by, 96, kButtonH};
    f.btn_repl = {f.btn_all.right() + 8, by, 76, kButtonH};
    f.btn_find = {cl.right() - 14 - 84, by, 84, kDefaultButtonH};
    if (f.btn_find.bottom() > cl.bottom() - 2)
        f.btn_find.y = cl.bottom() - 2 - f.btn_find.h;
    f.btn_cancel = {f.btn_find.x - 10 - 76, by, 76, kButtonH};
}

void paint_find() {
    FindState &f = g.find;
    Canvas &cv = f.canvas;
    layout_find();
    paint_gel(cv, g.ap, {0, 0, cv.width(), cv.height()}, "Find and Replace",
              f.focused, f.pressed_box, GelStyle::Dialog);
    CanvasClip clip(cv, f.gel.client);
    Rect cl = f.gel.client;
    int lx = cl.x + 10;
    Color lab = label_ink(g.ap, g.ap.c("primary.label"));
    cv.text(lx, label_y_centered(cv, f.field_find, "Find:"), "Find:", lab);
    cv.text(lx, label_y_centered(cv, f.field_repl, "Replace:"), "Replace:", lab);
    paint_field(cv, g.ap, f.field_find, f.find.c_str(), f.focus == FindFocusFind,
                f.caret_on && f.focus == FindFocusFind);
    paint_field(cv, g.ap, f.field_repl, f.repl.c_str(), f.focus == FindFocusRepl,
                f.caret_on && f.focus == FindFocusRepl);
    paint_tick(cv, g.ap, f.tick_case.x, f.tick_case.y,
               f.case_sensitive ? TickMark::Ticked : TickMark::Blank,
               "Case Sensitive", f.pressed_tick == 1);
    paint_tick(cv, g.ap, f.tick_stop.x, f.tick_stop.y,
               f.stop_at_end ? TickMark::Ticked : TickMark::Blank,
               "Stop at End of File", f.pressed_tick == 2);
    paint_button(cv, g.ap, f.btn_all, "Replace All", f.pressed_btn == 1, false);
    paint_button(cv, g.ap, f.btn_repl, "Replace", f.pressed_btn == 2, false);
    paint_button(cv, g.ap, f.btn_cancel, "Cancel", f.pressed_btn == 3, false);
    paint_button(cv, g.ap, f.btn_find, "Find", f.pressed_btn == 4, true);
}

void redraw_find() {
    if (!g.find.hwnd || !g.find.visible) return;
    paint_find();
    blit(g.find.hwnd, g.find.canvas);
}

void show_find(bool replace_mode) {
    (void)replace_mode;
    if (!g.find.hwnd) return;
    g.find.visible = true;
    ShowWindow(g.find.hwnd, SW_SHOW);
    SetForegroundWindow(g.find.hwnd);
    g.find.focus = FindFocusFind;
    redraw_find();
}

void hide_find() {
    g.find.visible = false;
    if (g.find.hwnd) ShowWindow(g.find.hwnd, SW_HIDE);
    if (g.hwnd) SetForegroundWindow(g.hwnd);
}

bool ci_equal(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
    return a == b;
}

size_t find_in_doc(const std::string &needle, size_t from, bool case_sens,
                   bool wrap) {
    if (needle.empty()) return std::string::npos;
    const std::string &t = g.doc.text;
    auto match_at = [&](size_t i) {
        if (i + needle.size() > t.size()) return false;
        for (size_t k = 0; k < needle.size(); ++k) {
            if (case_sens) {
                if (t[i + k] != needle[k]) return false;
            } else if (!ci_equal(t[i + k], needle[k]))
                return false;
        }
        return true;
    };
    for (size_t i = from; i < t.size(); ++i)
        if (match_at(i)) return i;
    if (wrap) {
        size_t end = std::min(from, t.size());
        for (size_t i = 0; i < end; ++i)
            if (match_at(i)) return i;
    }
    return std::string::npos;
}

void do_find_next() {
    if (g.find.find.empty()) {
        set_status("Nothing to find");
        redraw();
        return;
    }
    size_t from = g.doc.has_sel() ? g.doc.sel_hi() : g.doc.caret;
    size_t hit =
        find_in_doc(g.find.find, from, g.find.case_sensitive, !g.find.stop_at_end);
    if (hit == std::string::npos) {
        set_status("Not found");
        redraw();
        return;
    }
    g.doc.anchor = hit;
    g.doc.caret = hit + g.find.find.size();
    ensure_caret_visible();
    set_status("Found");
    redraw();
}

void do_replace_one() {
    if (g.doc.has_sel() && g.doc.selected() == g.find.find) {
        g.doc.replace_range(g.doc.sel_lo(), g.doc.sel_hi(), g.find.repl);
    }
    do_find_next();
}

void do_replace_all() {
    if (g.find.find.empty()) return;
    g.doc.push_undo();
    int count = 0;
    size_t i = 0;
    while (true) {
        size_t hit = find_in_doc(g.find.find, i, g.find.case_sensitive, false);
        if (hit == std::string::npos) break;
        g.doc.text.replace(hit, g.find.find.size(), g.find.repl);
        i = hit + g.find.repl.size();
        ++count;
    }
    g.doc.caret = g.doc.anchor = 0;
    g.doc.dirty = count > 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Replaced %d", count);
    set_status(buf);
    redraw();
}

void do_count() {
    if (g.find.find.empty()) {
        show_find(false);
        return;
    }
    int count = 0;
    size_t i = 0;
    while (true) {
        size_t hit = find_in_doc(g.find.find, i, g.find.case_sensitive, false);
        if (hit == std::string::npos) break;
        ++count;
        i = hit + g.find.find.size();
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d occurrences", count);
    set_status(buf);
    redraw();
}

// --- File / appearance -----------------------------------------------------

bool load_text_file(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string data;
    char buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof(buf), f)) data.append(buf, n);
    std::fclose(f);
    // Strip UTF-8 BOM
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);
    g.doc.push_undo();
    g.doc.text = std::move(data);
    g.doc.path = path;
    g.doc.caret = g.doc.anchor = 0;
    g.doc.dirty = false;
    g.scroll_y = g.scroll_x = 0;
    set_status("Opened");
    return true;
}

bool save_text_file(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(g.doc.text.data(), 1, g.doc.text.size(), f);
    std::fclose(f);
    g.doc.path = path;
    g.doc.dirty = false;
    set_status("Saved");
    return true;
}

void do_open() {
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Text (*.txt)\0*.txt\0All\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) load_text_file(file);
}

void do_save_as() {
    char file[MAX_PATH] = "untitled.txt";
    if (!g.doc.path.empty()) {
        std::snprintf(file, sizeof(file), "%s", g.doc.path.c_str());
    }
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Text (*.txt)\0*.txt\0All\0*.*\0";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = "txt";
    if (GetSaveFileNameA(&ofn)) save_text_file(file);
}

void do_save() {
    if (g.doc.path.empty()) do_save_as();
    else save_text_file(g.doc.path);
}

void do_new() {
    g.doc.push_undo();
    g.doc.text.clear();
    g.doc.path.clear();
    g.doc.caret = g.doc.anchor = 0;
    g.doc.dirty = false;
    g.scroll_y = g.scroll_x = 0;
    set_status("New document");
}

void paint_about();
void redraw_about();
void show_about();
void hide_about();
void create_about_window(HINSTANCE hinst);

void do_load_appearance() {
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        "Appearance (*.hap;*.sap)\0*.hap;*.sap\0"
        "Haxial Appearance (*.hap)\0*.hap\0"
        "Sagrado Appearance (*.sap)\0*.sap\0All\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return;
    if (g.ap.load(file)) {
        set_status(std::string("Appearance: ") + g.ap.skin.meta.name);
        sync_find_font();
        sync_about_font();
        redraw_find();
        redraw_about();
    } else
        set_status("Failed to load appearance");
}

void do_stock_appearance() {
    g.ap.set_skin(stock_skin());
    set_status("Stock appearance");
    sync_find_font();
    sync_about_font();
    redraw_find();
    redraw_about();
}

void about_box() { show_about(); }

void close_menu() {
    g.menu_open = -1;
    g.menu_item_hot = -1;
    g.menu_hot = -1;
}

void run_menu_command(int menu, int item) {
    close_menu();
    if (menu == MenuFile) {
        if (item == 0) do_new();
        else if (item == 1) do_open();
        else if (item == 3) do_save();
        else if (item == 4) do_save_as();
        else if (item == 6) PostQuitMessage(0);
    } else if (menu == MenuEdit) {
        if (item == 0) g.doc.undo();
        else if (item == 2) { // Cut
            if (g.doc.has_sel()) {
                clipboard_set(g.doc.selected());
                g.doc.replace_range(g.doc.sel_lo(), g.doc.sel_hi(), "");
            }
        } else if (item == 3) { // Copy
            if (g.doc.has_sel()) clipboard_set(g.doc.selected());
        } else if (item == 4) { // Paste
            std::string s = clipboard_get();
            // Normalize CRLF → LF for editing
            std::string n;
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '\r') {
                    if (i + 1 < s.size() && s[i + 1] == '\n') continue;
                    n.push_back('\n');
                } else
                    n.push_back(s[i]);
            }
            if (!n.empty()) g.doc.insert(n);
        } else if (item == 5) { // Clear
            if (g.doc.has_sel())
                g.doc.replace_range(g.doc.sel_lo(), g.doc.sel_hi(), "");
        } else if (item == 7) g.doc.select_all();
        else if (item == 8) g.doc.sort_lines();
    } else if (menu == MenuFind) {
        if (item == 0) show_find(false);
        else if (item == 1) do_find_next();
        else if (item == 2) show_find(true);
        else if (item == 4) {
            if (g.find.find.empty()) show_find(false);
            else do_count();
        }
    } else if (menu == MenuAppearance) {
        if (item == 0) do_load_appearance();
        else if (item == 1) do_stock_appearance();
        else if (item == 3) {
            g.soft_wrap = !g.soft_wrap;
            g.scroll_x = 0;
            set_status(g.soft_wrap ? "Soft Wrap on" : "Hard Wrap on");
        }
    } else if (menu == MenuHelp) {
        if (item == 0) about_box();
    }
    ensure_caret_visible();
    redraw();
}

// --- Hit testing / mouse (main) --------------------------------------------

int menu_bar_hit(int x, int y) {
    for (int i = 0; i < g.menu_bar.count; ++i)
        if (g.menu_bar.item_rects[i].contains(x, y)) return i;
    return -1;
}

void mouse_down(int x, int y) {
    layout_main();

    // Menu popup click
    if (g.menu_open >= 0) {
        int row = menu_hit_row(g.popup, x, y);
        if (row >= 0) {
            const char *lab = kMenus[g.menu_open].items[row];
            if (lab && std::strcmp(lab, "-") != 0)
                run_menu_command(g.menu_open, row);
            else {
                close_menu();
                redraw();
            }
            return;
        }
        // Click outside closes; may open another title
        int title = menu_bar_hit(x, y);
        if (title >= 0) {
            g.menu_open = title;
            g.menu_item_hot = -1;
            redraw();
            return;
        }
        close_menu();
        redraw();
        // fall through to other hits
    }

    if (g.gel.close_box.contains(x, y)) {
        g.drag = DragClose;
        g.pressed_box = 1;
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

    // Scrollbar V
    if (g.sbar_v.contains(x, y)) {
        int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / g.canvas.line_height());
        int max_y = std::max(0, (int)g.lines.size() - page);
        ScrollLayout sl = scroll_layout(g.ap, g.sbar_v, g.scroll_y, max_y, page);
        ScrollArrowHot ah = scroll_arrow_hit(sl, x, y);
        if (ah != ScrollArrowHot::None) {
            g.drag = DragArrowV;
            g.arrow_hot = ah;
            g.arrow_dir = scroll_arrow_dir(ah);
            g.scroll_y = std::clamp(g.scroll_y + g.arrow_dir, 0, max_y);
            redraw();
            return;
        }
        if (sl.thumb.contains(x, y)) {
            g.drag = DragThumbV;
            g.thumb_grab = y - sl.thumb.y;
            redraw();
            return;
        }
        // page jump
        if (y < sl.thumb.y) g.scroll_y = std::max(0, g.scroll_y - page);
        else g.scroll_y = std::min(max_y, g.scroll_y + page);
        redraw();
        return;
    }

    if (g.show_hscroll && g.sbar_h.contains(x, y)) {
        int max_w = 0;
        for (const auto &vl : g.lines) {
            int w = 0;
            for (size_t i = 0; i < vl.len; ++i) {
                char t[2] = {g.doc.text[vl.start + i], 0};
                w += g.canvas.text_width(t);
            }
            max_w = std::max(max_w, w);
        }
        int view = std::max(1, g.text_rect.w - 2 * kTextPad);
        int max_x = std::max(0, max_w - view);
        ScrollLayout sl = scroll_layout_h(g.ap, g.sbar_h, g.scroll_x, max_x, view);
        ScrollArrowHot ah = scroll_arrow_hit(sl, x, y);
        if (ah != ScrollArrowHot::None) {
            g.drag = DragArrowH;
            g.arrow_hot = ah;
            g.arrow_dir = scroll_arrow_dir(ah);
            g.scroll_x = std::clamp(g.scroll_x + g.arrow_dir * 16, 0, max_x);
            redraw();
            return;
        }
        if (sl.thumb.contains(x, y)) {
            g.drag = DragThumbH;
            g.thumb_grab = x - sl.thumb.x;
            redraw();
            return;
        }
        if (x < sl.thumb.x) g.scroll_x = std::max(0, g.scroll_x - view);
        else g.scroll_x = std::min(max_x, g.scroll_x + view);
        redraw();
        return;
    }

    if (g.text_rect.contains(x, y)) {
        int lh = g.canvas.line_height();
        int row = (y - g.text_rect.y - kTextPad) / lh;
        int li = g.scroll_y + row;
        size_t off =
            offset_at_xy(g.canvas, g.lines, g.doc.text, li,
                         x - g.text_rect.x - kTextPad + g.scroll_x);
        g.doc.caret = g.doc.anchor = off;
        g.drag = DragText;
        redraw();
        return;
    }
}

void mouse_move(int x, int y) {
    if (g.menu_open >= 0) {
        int row = menu_hit_row(g.popup, x, y);
        int title = menu_bar_hit(x, y);
        bool need = false;
        if (row != g.menu_item_hot) {
            g.menu_item_hot = row;
            need = true;
        }
        if (title >= 0 && title != g.menu_open && (GetKeyState(VK_LBUTTON) & 0x8000)) {
            g.menu_open = title;
            g.menu_item_hot = -1;
            need = true;
        }
        if (need) redraw();
        return;
    }

    if (g.drag == DragClose) {
        int p = g.gel.close_box.contains(x, y) ? 1 : 0;
        if (p != g.pressed_box) {
            g.pressed_box = p;
            redraw();
        }
        return;
    }
    if (g.drag == DragMax) {
        int p = g.gel.max_box.contains(x, y) ? 3 : 0;
        if (p != g.pressed_box) {
            g.pressed_box = p;
            redraw();
        }
        return;
    }
    if (g.drag == DragMin) {
        int p = g.gel.min_box.contains(x, y) ? 4 : 0;
        if (p != g.pressed_box) {
            g.pressed_box = p;
            redraw();
        }
        return;
    }
    if (g.drag == DragThumbV) {
        int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / g.canvas.line_height());
        int max_y = std::max(0, (int)g.lines.size() - page);
        ScrollLayout sl = scroll_layout(g.ap, g.sbar_v, g.scroll_y, max_y, page);
        int track = sl.track.h - sl.thumb.h;
        if (track > 0 && max_y > 0) {
            int ty = y - g.thumb_grab - sl.track.y;
            g.scroll_y = std::clamp(ty * max_y / track, 0, max_y);
            redraw();
        }
        return;
    }
    if (g.drag == DragThumbH && g.show_hscroll) {
        int max_w = 0;
        for (const auto &vl : g.lines) {
            int w = 0;
            for (size_t i = 0; i < vl.len; ++i) {
                char t[2] = {g.doc.text[vl.start + i], 0};
                w += g.canvas.text_width(t);
            }
            max_w = std::max(max_w, w);
        }
        int view = std::max(1, g.text_rect.w - 2 * kTextPad);
        int max_x = std::max(0, max_w - view);
        ScrollLayout sl = scroll_layout_h(g.ap, g.sbar_h, g.scroll_x, max_x, view);
        int track = sl.track.w - sl.thumb.w;
        if (track > 0 && max_x > 0) {
            int tx = x - g.thumb_grab - sl.track.x;
            g.scroll_x = std::clamp(tx * max_x / track, 0, max_x);
            redraw();
        }
        return;
    }
    if (g.drag == DragText) {
        int lh = g.canvas.line_height();
        int row = (y - g.text_rect.y - kTextPad) / lh;
        int li = g.scroll_y + row;
        g.doc.caret =
            offset_at_xy(g.canvas, g.lines, g.doc.text, li,
                         x - g.text_rect.x - kTextPad + g.scroll_x);
        ensure_caret_visible();
        redraw();
    }
}

void mouse_up(int x, int y) {
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
    g.arrow_hot = ScrollArrowHot::None;
    redraw();
}

// --- Keyboard --------------------------------------------------------------

void move_caret(size_t pos, bool extend) {
    g.doc.caret = std::min(pos, g.doc.text.size());
    if (!extend) g.doc.anchor = g.doc.caret;
    ensure_caret_visible();
}

void caret_line_nav(int dir, bool extend) {
    // dir -1 previous visual line, +1 next
    int li = vis_index_at(g.lines, g.doc.caret);
    const VisLine &cur = g.lines[size_t(li)];
    int x = 0;
    for (size_t i = cur.start; i < g.doc.caret && i < cur.start + cur.len; ++i) {
        char t[2] = {g.doc.text[i], 0};
        x += g.canvas.text_width(t);
    }
    int ni = std::clamp(li + dir, 0, (int)g.lines.size() - 1);
    move_caret(offset_at_xy(g.canvas, g.lines, g.doc.text, ni, x), extend);
}

// --- Window procs ----------------------------------------------------------

LRESULT CALLBACK FindWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void create_find_window(HINSTANCE hinst) {
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = FindWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SagradoTextEditFind";
    RegisterClassA(&wc);

    DWORD style = WS_POPUP | WS_SYSMENU | WS_CLIPCHILDREN;
    g.find.hwnd =
        CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW, wc.lpszClassName,
                        "Find and Replace", style, 120, 120, kFindDlgW, kFindDlgH,
                        g.hwnd, nullptr, hinst, nullptr);
    g.find.canvas.resize(kFindDlgW, kFindDlgH);
    // Bug fix: do NOT key off g.canvas.font().name — that is always "Stock"
    // (or a loaded face), while g.font is empty unless --font was passed.
    // Pointing Find at an empty Font drew chrome with zero-width glyphs (no
    // labels on buttons, ticks, fields, or the title).
    sync_find_font();
    SetTimer(g.find.hwnd, 1, 500, nullptr);
}

// --- About dialog (kit gel — never MessageBox) ----------------------------

static const char *kAboutBody =
    "Sagrado TextEdit\n\n"
    "First consumer app of SagradoKit - the authoritative "
    "appearance kit (Haxial-faithful gel, menus, fields).\n\n"
    "Load any .hap or .sap via Appearance -> Load Appearance.";

void paint_about() {
    AboutState &a = g.about;
    Canvas &cv = a.canvas;
    a.lay = paint_alert(cv, g.ap, {0, 0, cv.width(), cv.height()},
                        "About Sagrado TextEdit", kAboutBody, AlertKind::Note,
                        a.focused, a.pressed_box, a.ok_pressed);
}

void redraw_about() {
    if (!g.about.hwnd || !g.about.visible) return;
    paint_about();
    blit(g.about.hwnd, g.about.canvas);
}

void hide_about() {
    g.about.visible = false;
    g.about.pressed_box = 0;
    g.about.ok_pressed = false;
    if (g.about.hwnd) ShowWindow(g.about.hwnd, SW_HIDE);
    if (g.hwnd) SetForegroundWindow(g.hwnd);
}

void show_about() {
    if (!g.about.hwnd) return;
    g.about.visible = true;
    // Centre over the main window when possible.
    RECT rc{};
    if (g.hwnd && GetWindowRect(g.hwnd, &rc)) {
        int x = rc.left + ((rc.right - rc.left) - kAlertDlgW) / 2;
        int y = rc.top + ((rc.bottom - rc.top) - kAlertDlgH) / 2;
        SetWindowPos(g.about.hwnd, nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(g.about.hwnd, SW_SHOW);
    SetForegroundWindow(g.about.hwnd);
    redraw_about();
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

void create_about_window(HINSTANCE hinst) {
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = AboutWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SagradoTextEditAbout";
    RegisterClassA(&wc);

    DWORD style = WS_POPUP | WS_SYSMENU | WS_CLIPCHILDREN;
    g.about.hwnd =
        CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW, wc.lpszClassName,
                        "About Sagrado TextEdit", style, 160, 160, kAlertDlgW,
                        kAlertDlgH, g.hwnd, nullptr, hinst, nullptr);
    g.about.canvas.resize(kAlertDlgW, kAlertDlgH);
    sync_about_font();
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AboutState &a = g.about;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint_about();
        blit(hwnd, a.canvas);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        a.focused = true;
        redraw_about();
        return 0;
    case WM_KILLFOCUS:
        a.focused = false;
        redraw_about();
        return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        paint_about(); // refresh hit rects
        SetCapture(hwnd);
        if (a.lay.gel.close_box.contains(x, y)) {
            a.pressed_box = 1;
            g.drag = DragAboutClose;
        } else if (a.lay.gel.min_box.contains(x, y)) {
            a.pressed_box = 4;
            g.drag = DragAboutMin;
        } else if (a.lay.btn_ok.contains(x, y)) {
            a.ok_pressed = true;
            g.drag = DragAboutOk;
        }
        redraw_about();
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        ReleaseCapture();
        if (g.drag == DragAboutClose && a.lay.gel.close_box.contains(x, y))
            hide_about();
        if (g.drag == DragAboutMin && a.lay.gel.min_box.contains(x, y))
            ShowWindow(hwnd, SW_MINIMIZE);
        if (g.drag == DragAboutOk && a.lay.btn_ok.contains(x, y)) hide_about();
        a.pressed_box = 0;
        a.ok_pressed = false;
        g.drag = DragNone;
        redraw_about();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN) hide_about();
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcA(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            paint_about();
            if (pt.y >= 0 && pt.y < a.lay.gel.title_h &&
                !a.lay.gel.close_box.contains(pt.x, pt.y) &&
                !a.lay.gel.min_box.contains(pt.x, pt.y))
                return HTCAPTION;
        }
        return hit;
    }
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK FindWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    FindState &f = g.find;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint_find();
        blit(hwnd, f.canvas);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            f.caret_on = !f.caret_on;
            redraw_find();
        }
        return 0;
    case WM_SETFOCUS:
        f.focused = true;
        redraw_find();
        return 0;
    case WM_KILLFOCUS:
        f.focused = false;
        redraw_find();
        return 0;
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        layout_find();
        SetCapture(hwnd);
        if (f.gel.close_box.contains(x, y)) {
            f.pressed_box = 1;
            g.drag = DragFindClose;
        } else if (f.gel.min_box.contains(x, y)) {
            f.pressed_box = 4;
            g.drag = DragFindMin;
        } else if (f.field_find.contains(x, y)) {
            f.focus = FindFocusFind;
        } else if (f.field_repl.contains(x, y)) {
            f.focus = FindFocusRepl;
        } else if (f.tick_case.contains(x, y)) {
            g.drag = DragFindTick;
            f.pressed_tick = 1;
        } else if (f.tick_stop.contains(x, y)) {
            g.drag = DragFindTick;
            f.pressed_tick = 2;
        } else if (f.btn_all.contains(x, y)) {
            g.drag = DragFindBtn;
            f.pressed_btn = 1;
        } else if (f.btn_repl.contains(x, y)) {
            g.drag = DragFindBtn;
            f.pressed_btn = 2;
        } else if (f.btn_cancel.contains(x, y)) {
            g.drag = DragFindBtn;
            f.pressed_btn = 3;
        } else if (f.btn_find.contains(x, y)) {
            g.drag = DragFindBtn;
            f.pressed_btn = 4;
        }
        redraw_find();
        return 0;
    }
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        ReleaseCapture();
        if (g.drag == DragFindClose && f.gel.close_box.contains(x, y)) hide_find();
        if (g.drag == DragFindMin && f.gel.min_box.contains(x, y))
            ShowWindow(hwnd, SW_MINIMIZE);
        if (g.drag == DragFindTick) {
            if (f.pressed_tick == 1 && f.tick_case.contains(x, y))
                f.case_sensitive = !f.case_sensitive;
            if (f.pressed_tick == 2 && f.tick_stop.contains(x, y))
                f.stop_at_end = !f.stop_at_end;
        }
        if (g.drag == DragFindBtn) {
            int b = f.pressed_btn;
            if (b == 1 && f.btn_all.contains(x, y)) do_replace_all();
            if (b == 2 && f.btn_repl.contains(x, y)) do_replace_one();
            if (b == 3 && f.btn_cancel.contains(x, y)) hide_find();
            if (b == 4 && f.btn_find.contains(x, y)) do_find_next();
        }
        f.pressed_box = 0;
        f.pressed_btn = 0;
        f.pressed_tick = 0;
        g.drag = DragNone;
        redraw_find();
        return 0;
    }
    case WM_CHAR: {
        std::string &s = (f.focus == FindFocusRepl) ? f.repl : f.find;
        if (wp == 8) {
            if (!s.empty()) s.pop_back();
        } else if (wp == '\r' || wp == '\n') {
            do_find_next();
        } else if (wp == 27) {
            hide_find();
        } else if (wp >= 32 && wp < 127 && s.size() < 240) {
            s.push_back(char(wp));
        }
        redraw_find();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) hide_find();
        if (wp == VK_TAB) {
            f.focus = f.focus == FindFocusFind ? FindFocusRepl : FindFocusFind;
            redraw_find();
        }
        if (wp == VK_RETURN) do_find_next();
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcA(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            layout_find();
            if (pt.y >= 0 && pt.y < f.gel.title_h &&
                !f.gel.close_box.contains(pt.x, pt.y) &&
                !f.gel.min_box.contains(pt.x, pt.y))
                return HTCAPTION;
        }
        return hit;
    }
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.canvas.resize(kWinW, kWinH);
        std::string skin = find_default_skin();
        if (!skin.empty() && g.ap.load(skin))
            set_status("Appearance: " + g.ap.skin.meta.name);
        else
            g.ap.set_skin(stock_skin());
        g.doc.text =
            "Sagrado TextEdit\n"
            "================\n\n"
            "First app on SagradoKit — Haxial TextEdit-shaped, painted entirely\n"
            "through the Appearance Engine (gel, menus, fields, scrollbars).\n\n"
            "File menu opens and saves plain text. Appearance menu loads any\n"
            ".hap or .sap skin. Find opens the classic Find & Replace gel.\n\n"
            "Try Soft Wrap under Appearance, or Load Appearance → Milk Redux.\n";
        g.doc.caret = g.doc.anchor = 0;
        g.doc.dirty = false;
        SetTimer(hwnd, 1, 500, nullptr);
        create_find_window(g.hinst);
        create_about_window(g.hinst);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0) {
            g.canvas.resize(w, h);
            redraw();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        paint_main();
        blit(hwnd, g.canvas);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wp == 1) {
            g.caret_on = !g.caret_on;
            redraw();
        }
        return 0;
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
        if (wp & MK_LBUTTON || g.menu_open >= 0)
            mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int step = delta > 0 ? -3 : 3;
        int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / g.canvas.line_height());
        int max_y = std::max(0, (int)g.lines.size() - page);
        g.scroll_y = std::clamp(g.scroll_y + step, 0, max_y);
        redraw();
        return 0;
    }
    case WM_CHAR: {
        if (g.menu_open >= 0) {
            close_menu();
            redraw();
        }
        if (wp == 8) {
            g.doc.backspace();
        } else if (wp == 127) {
            g.doc.del_forward();
        } else if (wp == '\r') {
            g.doc.insert("\n");
        } else if (wp == 22) { // Ctrl+V (also handled in KEYDOWN)
            // ignore here; KEYDOWN handles
        } else if (wp == 3 || wp == 24 || wp == 1 || wp == 26) {
            // Ctrl+C/X/A/Z — KEYDOWN
        } else if (wp >= 32 && wp < 127) {
            char t[2] = {char(wp), 0};
            g.doc.insert(t);
        }
        ensure_caret_visible();
        redraw();
        return 0;
    }
    case WM_KEYDOWN: {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wp == VK_ESCAPE) {
            if (g.menu_open >= 0) {
                close_menu();
                redraw();
                return 0;
            }
            if (g.find.visible) {
                hide_find();
                return 0;
            }
            PostQuitMessage(0);
            return 0;
        }
        if (ctrl && wp == 'N') {
            do_new();
            redraw();
            return 0;
        }
        if (ctrl && wp == 'O') {
            do_open();
            redraw();
            return 0;
        }
        if (ctrl && wp == 'S') {
            do_save();
            redraw();
            return 0;
        }
        if (ctrl && wp == 'F') {
            show_find(false);
            return 0;
        }
        if (ctrl && wp == 'H') {
            show_find(true);
            return 0;
        }
        if (ctrl && wp == 'G') {
            do_find_next();
            return 0;
        }
        if (ctrl && wp == 'A') {
            g.doc.select_all();
            redraw();
            return 0;
        }
        if (ctrl && wp == 'Z') {
            g.doc.undo();
            ensure_caret_visible();
            redraw();
            return 0;
        }
        if (ctrl && wp == 'C') {
            if (g.doc.has_sel()) clipboard_set(g.doc.selected());
            return 0;
        }
        if (ctrl && wp == 'X') {
            if (g.doc.has_sel()) {
                clipboard_set(g.doc.selected());
                g.doc.replace_range(g.doc.sel_lo(), g.doc.sel_hi(), "");
                redraw();
            }
            return 0;
        }
        if (ctrl && wp == 'V') {
            std::string s = clipboard_get();
            std::string n;
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '\r') {
                    if (i + 1 < s.size() && s[i + 1] == '\n') continue;
                    n.push_back('\n');
                } else
                    n.push_back(s[i]);
            }
            if (!n.empty()) g.doc.insert(n);
            ensure_caret_visible();
            redraw();
            return 0;
        }
        if (wp == VK_LEFT) {
            if (g.doc.caret > 0) move_caret(g.doc.caret - 1, shift);
            redraw();
            return 0;
        }
        if (wp == VK_RIGHT) {
            if (g.doc.caret < g.doc.text.size()) move_caret(g.doc.caret + 1, shift);
            redraw();
            return 0;
        }
        if (wp == VK_UP) {
            caret_line_nav(-1, shift);
            redraw();
            return 0;
        }
        if (wp == VK_DOWN) {
            caret_line_nav(1, shift);
            redraw();
            return 0;
        }
        if (wp == VK_HOME) {
            int li = vis_index_at(g.lines, g.doc.caret);
            move_caret(g.lines[size_t(li)].start, shift);
            redraw();
            return 0;
        }
        if (wp == VK_END) {
            int li = vis_index_at(g.lines, g.doc.caret);
            const VisLine &vl = g.lines[size_t(li)];
            move_caret(vl.start + vl.len, shift);
            redraw();
            return 0;
        }
        if (wp == VK_PRIOR) {
            int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / g.canvas.line_height());
            for (int i = 0; i < page; ++i) caret_line_nav(-1, shift);
            redraw();
            return 0;
        }
        if (wp == VK_NEXT) {
            int page = std::max(1, (g.text_rect.h - 2 * kTextPad) / g.canvas.line_height());
            for (int i = 0; i < page; ++i) caret_line_nav(1, shift);
            redraw();
            return 0;
        }
        if (wp == VK_DELETE) {
            g.doc.del_forward();
            ensure_caret_visible();
            redraw();
            return 0;
        }
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcA(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            layout_main();
            if (g.menu_open >= 0) return HTCLIENT;
            if (pt.y >= 0 && pt.y < kTitleH &&
                !g.gel.close_box.contains(pt.x, pt.y) &&
                !g.gel.max_box.contains(pt.x, pt.y) &&
                !g.gel.min_box.contains(pt.x, pt.y) &&
                !g.gel.hatch_box.contains(pt.x, pt.y))
                return HTCAPTION;
            if (g.gel.grip.contains(pt.x, pt.y)) return HTBOTTOMRIGHT;
            const int grip = 4;
            int W = g.canvas.width(), H = g.canvas.height();
            bool left = pt.x < grip, right = pt.x >= W - grip;
            bool top = pt.y < grip, bottom = pt.y >= H - grip;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        return hit;
    }
    case WM_NCCALCSIZE:
        if (wp) return 0;
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g.find.hwnd) DestroyWindow(g.find.hwnd);
        if (g.about.hwnd) DestroyWindow(g.about.hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR cmd, int show) {
    g.hinst = hinst;
    for (int i = 1; i < __argc; ++i) {
        if (std::strcmp(__argv[i], "--font") || i + 1 >= __argc) continue;
        if (hfnt::load(__argv[++i], g.font)) {
            g.canvas.set_font(&g.font);
            set_status("Font: " + g.font.name);
        }
    }
    (void)cmd;

    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "SagradoTextEdit";
    RegisterClassA(&wc);

    // Gel paints the title bar. Do not set WS_CAPTION / WS_SYSMENU /
    // WS_MINIMIZEBOX / WS_MAXIMIZEBOX — Wine's window manager treats those as
    // "decorate me" and stacks a second host title bar on Linux.
    // WS_THICKFRAME keeps edge resize; gel buttons call ShowWindow/DestroyWindow.
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN;
    g.hwnd = CreateWindowExA(WS_EX_APPWINDOW, wc.lpszClassName, "Sagrado TextEdit",
                             style, CW_USEDEFAULT, CW_USEDEFAULT, kWinW, kWinH,
                             nullptr, nullptr, hinst, nullptr);
    ShowWindow(g.hwnd, show);
    UpdateWindow(g.hwnd);

    // Open file from command line if given
    for (int i = 1; i < __argc; ++i) {
        if (__argv[i][0] == '-') continue;
        load_text_file(__argv[i]);
        redraw();
        break;
    }

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return int(msg.wParam);
}
