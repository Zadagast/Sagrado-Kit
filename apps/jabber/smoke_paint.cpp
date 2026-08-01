// Headless paint smoke for Sagrado Jabber chrome (no Win32 / no network).
// Loads a skin, paints buddy list + IM tabs + compose + status, writes PPM.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "appearance.h"
#include "text_field.h"

static Color xep0392_smoke(const std::string &id, Color bg) {
    // Lightweight stand-in for the app's XEP-0392 helper (angle via FNV-ish hash).
    uint32_t h = 2166136261u;
    for (unsigned char c : id) {
        h ^= c;
        h *= 16777619u;
    }
    double angle = (double(h & 0xffff) / 65536.0) * 360.0;
    double lum = (0.2126 * bg.r + 0.7152 * bg.g + 0.0722 * bg.b) / 255.0;
    double L = lum < 0.45 ? 0.72 : 0.38;
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
    constexpr int kComposeH = 72;
    constexpr int kMsgAvatar = 32;
    constexpr int kMsgPadX = 12;
    constexpr int kMsgAvatarGap = 10;
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

    // Transcript: colored nick + time + body + reaction pill (no chat header strip)
    cv.fill(transcript, ap.c("text.background"));
    {
        CanvasClip clip(cv, transcript);
        const int pad = kMsgPadX;
        const int lh = cv.line_height();
        const int text_x = transcript.x + pad + kMsgAvatar + kMsgAvatarGap;
        int ty = transcript.y + 8;
        Color tbg = ap.c("text.background");
        Color alice_col = xep0392_smoke("alice@yax.im", tbg);

        auto paint_av = [&](int yy, const char *initials, Color fill) {
            Rect a{transcript.x + pad, yy, kMsgAvatar, kMsgAvatar};
            cv.fill(a, fill);
            cv.frame(a, ap.c("list.separator"));
            int tw = cv.text_width(initials);
            cv.text(a.x + (a.w - tw) / 2, a.y + (a.h - lh) / 2, initials,
                    Color{245, 245, 245});
        };

        // Alice (headed)
        paint_av(ty, "AL", alice_col);
        cv.text(text_x, ty, "Alice", alice_col);
        const char *clock = "12:34";
        cv.text(transcript.right() - pad - cv.text_width(clock), ty, clock,
                ap.c("menu.disable_label"));
        cv.text(text_x, ty + lh + 2, "You've got mail!", ap.c("primary.label"));
        // Reaction pill
        {
            int py = ty + lh + 2 + lh + 4;
            Rect pill{text_x, py, 36, 26};
            cv.fill(pill, ap.c("list.background"));
            rounded_frame(cv, pill, ap.c("list.separator"), tbg);
            cv.text(pill.x + 10, pill.y + 5, "+1", ap.c("menu.disable_label"));
        }
        ty += std::max(kMsgAvatar, lh + 2 + lh + 34) + 12;

        // You (headed) — no soft band
        Color you_col = xep0392_smoke("roachclip@yax.im", tbg);
        paint_av(ty, "YO", you_col);
        cv.text(text_x, ty, "You", you_col);
        cv.text(transcript.right() - pad - cv.text_width("12:35"), ty, "12:35",
                ap.c("menu.disable_label"));
        cv.text(text_x, ty + lh + 2, "Signed on — Sagrado Jabber",
                ap.c("text.foreground"));
        cv.text(text_x, ty + lh + 2 + lh, "Delivered", ap.c("menu.disable_label"));
        ty += std::max(kMsgAvatar, lh + 2 + lh + lh) + 4;

        // Continuation
        cv.text(text_x, ty, "Shift+Enter for a new line in compose.",
                ap.c("text.foreground"));
    }

    cv.fill(compose, ap.c("primary.background"));
    cv.hline(compose.x, compose.right(), compose.y, ap.c("list.separator"));
    const int bh = 28;
    const int by = compose.y + (compose.h - bh) / 2;
    paint_button(cv, ap, {compose.x + 8, by, 28, bh}, ":)", false, false);
    Rect field{compose.x + 42, compose.y + 10, compose.w - 42 - 108, compose.h - 20};
    TextFieldState compose_st;
    compose_st.doc.text = "Type a message…\nShift+Enter for a new line";
    compose_st.doc.caret = compose_st.doc.anchor = compose_st.doc.text.size();
    compose_st.focused = true;
    paint_text_field(cv, ap, field, compose_st, true);
    paint_button(cv, ap, {compose.right() - 108, by, 28, bh}, "+", false, false);
    paint_button(cv, ap, {compose.right() - 72, by, 64, bh}, "Send", false, true);

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
            std::fprintf(stderr, "menu art spilled outside frame\n");
            return 1;
        }
    }

    size_t painted = 0;
    for (int i = 0; i < W * H; ++i) {
        uint32_t p = cv.data()[size_t(i)];
        if ((p & 0xffffff) != 0) ++painted;
    }
    std::printf("painted %zu non-black pixels\n", painted);

    FILE *f = std::fopen("build/jabber-preview.ppm", "wb");
    if (!f) {
        std::fprintf(stderr, "could not write build/jabber-preview.ppm\n");
        return 1;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) {
        uint32_t p = cv.data()[size_t(i)];
        unsigned char rgb[3] = {uint8_t((p >> 16) & 255), uint8_t((p >> 8) & 255),
                                uint8_t(p & 255)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    std::printf("wrote build/jabber-preview.ppm\n");
    return 0;
}
