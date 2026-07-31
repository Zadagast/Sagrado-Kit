// Headless paint smoke for Sagrado Jabber chrome (no Win32 / no network).
// Loads a skin, paints buddy list + IM tabs + compose + status, writes PPM.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "appearance.h"
#include "text_field.h"

int main(int argc, char **argv) {
    Appearance ap;
    std::string path = argc > 1 ? argv[1] : "format/skins/milk-redux/milk-redux.sap";
    if (!ap.load(path)) {
        std::fprintf(stderr, "load failed: %s\n", path.c_str());
        ap.set_skin(stock_skin());
    } else {
        std::printf("loaded: %s (%s)\n", path.c_str(), ap.skin.meta.name.c_str());
    }

    constexpr int W = 860, H = 560;
    constexpr int kStatusH = 22;
    constexpr int kRosterW = 220;
    constexpr int kIdentityH = 64;
    constexpr int kAvatarSz = 40;
    constexpr int kBuddyRowH = 36;
    constexpr int kTabH = 22;
    constexpr int kComposeH = 56;
    Canvas cv;
    cv.resize(W, H);

    GelLayout gel = gel_layout(0, 0, W, H, GelStyle::Main, &ap, true);
    paint_gel(cv, ap, {0, 0, W, H}, "roachclip@yax.im — Sagrado Jabber", true, 0,
              GelStyle::Main);

    Rect cl = gel.client;
    Rect menu_r{cl.x, cl.y, cl.w, kMenuBarH};
    static const char *titles[] = {"File", "Buddy", "Chat", "Appearance", "Help"};
    paint_menu_bar(cv, ap, menu_r, titles, 5, 0);

    Rect status{cl.x, cl.bottom() - kStatusH, cl.w, kStatusH};
    int top = menu_r.bottom();
    int body_h = status.y - top;
    Rect identity{cl.x, top, kRosterW, kIdentityH};
    Rect roster{cl.x, top + kIdentityH, kRosterW, body_h - kIdentityH};
    Rect tabs{roster.right(), top, cl.right() - roster.right(), kTabH};
    Rect compose{tabs.x, status.y - kComposeH, tabs.w, kComposeH};
    Rect transcript{tabs.x, tabs.bottom(), tabs.w, compose.y - tabs.bottom()};

    // Identity strip (Yahoo-shaped)
    cv.fill(identity, ap.c("primary.background"));
    cv.hline(identity.x, identity.right(), identity.bottom() - 1, ap.c("list.separator"));
    Rect av{identity.x + 8, identity.y + (identity.h - kAvatarSz) / 2, kAvatarSz, kAvatarSz};
    cv.fill(av, ap.c("list.background"));
    cv.frame(av, ap.c("list.separator"));
    cv.text(av.x + 14, av.y + 12, "R", ap.c("primary.label"));
    Rect dot{av.right() - 10, av.bottom() - 10, 10, 10};
    cv.fill(dot, ap.c("list.hilite_background"));
    cv.frame(dot, ap.c("list.separator"));
    cv.text(av.right() + 8, identity.y + 6, "Roachclip", ap.c("primary.label"));
    cv.text(av.right() + 8, identity.y + 6 + cv.line_height(), "Available",
            ap.c("menu.disable_label"));
    paint_field(cv, ap, {av.right() + 8, identity.bottom() - 26, identity.right() - 8 - (av.right() + 8), 20},
                "Is it Friday yet?", false, false);

    cv.fill(roster, ap.c("list.background"));
    cv.vline(roster.right() - 1, identity.y, roster.bottom(), ap.c("list.separator"));
    cv.text(roster.x + 8, roster.y + 4, "Buddies", ap.c("primary.label"));
    const char *names[] = {"Alice", "Bob", "Carol"};
    const char *stats[] = {"At the desk", "brb", ""};
    Color dots[] = {ap.c("list.hilite_background"), ap.c("primary.dark"),
                    ap.c("menu.disable_label")};
    int y = roster.y + 22;
    for (int i = 0; i < 3; ++i) {
        Rect row{roster.x + 2, y, roster.w - 4, kBuddyRowH};
        if (i == 0) cv.fill(row, ap.c("list.hilite_background"));
        Color ink = i < 2 ? ap.c("list.label") : ap.c("menu.disable_label");
        if (i == 0) ink = ap.c("list.hilite_foreground");
        CanvasClip clip(cv, roster);
        Rect d{row.x + 6, row.y + (row.h - 8) / 2, 8, 8};
        cv.fill(d, dots[i]);
        cv.text_elided(d.right() + 6, row.y + 4, names[i], row.w - 24, ink);
        if (stats[i][0])
            cv.text_elided(d.right() + 6, row.y + 4 + cv.line_height(), stats[i],
                           row.w - 24, ap.c("menu.disable_label"));
        y += kBuddyRowH;
    }

    cv.fill(tabs, ap.c("primary.background"));
    cv.fill({tabs.x + 4, tabs.y + 2, 56, tabs.h - 3}, ap.c("list.hilite_background"));
    cv.text(tabs.x + 12, tabs.y + 3, "Alice", ap.c("list.hilite_foreground"));

    cv.fill(transcript, ap.c("text.background"));
    {
        CanvasClip clip(cv, transcript);
        cv.text(transcript.x + 6, transcript.y + 4, "Alice: You've got mail!",
                ap.c("primary.label"));
        cv.text(transcript.x + 6, transcript.y + 4 + cv.line_height() + 2,
                "You: Signed on — Sagrado Jabber", ap.c("text.foreground"));
    }

    cv.fill(compose, ap.c("primary.background"));
    Rect field{compose.x + 8, compose.y + 8, compose.w - 96, compose.h - 16};
    TextFieldState compose_st;
    compose_st.doc.text = "Type a message…\nShift+Enter for a new line";
    compose_st.doc.caret = compose_st.doc.anchor = compose_st.doc.text.size();
    compose_st.focused = true;
    paint_text_field(cv, ap, field, compose_st, true);
    paint_button(cv, ap, {compose.right() - 72, compose.y + 8, 64, compose.h - 16},
                 "Send", false, true);

    cv.fill(status, ap.c("primary.background"));
    cv.text(status.x + 8, status.y + (status.h - cv.line_height()) / 2,
            "Signed on as roachclip@yax.im — Alice signed on",
            ap.c("primary.label"));
    paint_progress(cv, ap, {status.right() - 140, status.y + 3, 128, status.h - 6},
                   40, 100);
    paint_gel_grip(cv, ap, gel.grip, true);

    // Open File menu (Sign On / Get an Account) — pattern tiles must not spill.
    static const char *file_items[] = {
        "Sign On...", "Get an Account...", "Sign Off", "-", "Quit",
    };
    int mw = 120;
    for (int i = 0; i < 5; ++i)
        if (file_items[i][0] != '-')
            mw = std::max(mw, cv.text_width(file_items[i]) + 28);
    MenuLayout open =
        paint_menu(cv, ap, menu_r.x + 4, menu_r.bottom(), mw, file_items, 5, 1);

    if (path.find("Milk Redux") != std::string::npos ||
        path.find("milk-redux") != std::string::npos) {
        if (ap.art("menu.background") || ap.art("menu.item.normal")) {
            std::fprintf(stderr, "Milk menu art leaked into Jabber paint\n");
            return 1;
        }
        Color expect = ap.c("menu.background");
        int sx = open.items_bounds.x + open.items_bounds.w / 2;
        int sy = open.items_bounds.y + 2;
        uint32_t px = cv.data()[size_t(sy) * W + sx];
        int pr = int((px >> 16) & 255), pg = int((px >> 8) & 255),
            pb = int(px & 255);
        int dr = pr - expect.r, dg = pg - expect.g, db = pb - expect.b;
        if (dr * dr + dg * dg + db * db > 40 * 40) {
            std::fprintf(stderr,
                         "Milk open menu fill #%02x%02x%02x != colour #%02x%02x%02x\n",
                         pr, pg, pb, expect.r, expect.g, expect.b);
            return 1;
        }
        std::printf("Milk File menu colour-path OK (mw=%d)\n", mw);
    }

    if (ap.art("menu.background_pattern")) {
        Canvas pad;
        pad.resize(320, 320);
        Color bg{20, 20, 20};
        pad.clear(bg);
        MenuLayout m = paint_menu(pad, ap, 24, 24, mw, file_items, 5, 1);
        size_t spill = 0;
        for (int yy = 0; yy < pad.height(); ++yy)
            for (int xx = 0; xx < pad.width(); ++xx) {
                if (m.frame.contains(xx, yy)) continue;
                uint32_t p = pad.data()[size_t(yy) * pad.width() + xx];
                int r = int((p >> 16) & 255), g = int((p >> 8) & 255),
                    b = int(p & 255);
                if (r != bg.r || g != bg.g || b != bg.b) ++spill;
            }
        std::printf("menu pattern spill outside frame: %zu pixels\n", spill);
        if (spill != 0) {
            std::fprintf(stderr, "open-menu pattern spilled past frame\n");
            return 1;
        }
    }

    // Get an Account dialog — screen name, home server list, password.
    Rect dlg{(W - 400) / 2, (H - 340) / 2, 400, 340};
    paint_gel(cv, ap, dlg, "Get an Account", true, 0, GelStyle::Dialog);
    GelLayout dgl = gel_layout(dlg.x, dlg.y, dlg.w, dlg.h, GelStyle::Dialog, &ap, true);
    paint_field(cv, ap, {dgl.client.x + 12, dgl.client.y + 24, dgl.client.w - 24, 24},
                "roachclip", true, true);
    Rect list{dgl.client.x + 12, dgl.client.y + 72, dgl.client.w - 24, 96};
    cv.fill(list, ap.c("list.background"));
    cv.fill({list.x + 2, list.y + 2, list.w - 4, cv.line_height() + 4},
            ap.c("list.hilite_background"));
    cv.text(list.x + 8, list.y + 4, "yax.im", ap.c("list.hilite_foreground"));
    paint_field(cv, ap, {dgl.client.x + 12, list.bottom() + 28, dgl.client.w - 24, 24},
                "********", false, false);

    size_t lit = 0;
    for (int i = 0; i < W * H; ++i)
        if (cv.data()[i]) ++lit;
    std::printf("painted %zu non-black pixels\n", lit);
    if (lit < 1000) {
        std::fprintf(stderr, "too few pixels painted\n");
        return 1;
    }

    FILE *f = std::fopen("build/jabber-preview.ppm", "wb");
    if (f) {
        std::fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; ++i) {
            uint32_t p = cv.data()[i];
            uint8_t rgb[3] = {uint8_t((p >> 16) & 255), uint8_t((p >> 8) & 255),
                              uint8_t(p & 255)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        std::printf("wrote build/jabber-preview.ppm\n");
    }
    return 0;
}
