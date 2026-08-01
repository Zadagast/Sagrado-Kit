// Kit clipboard — CF_TEXT helpers for every Sagrado app.
// Win32 only; keep out of paint headers that stay host-agnostic where possible.
#pragma once

#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "utf8_win.h"

#include <cstring>

namespace sagrado {

// Text crosses the clipboard as UTF-16 — CF_TEXT would fold anything outside
// the ANSI codepage (emoji, accents) down to '?'. CF_TEXT is published too so
// ANSI-only apps still get the ASCII part.
inline bool clipboard_set(HWND hwnd, std::string_view s) {
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    std::wstring w = wide_from_utf8(std::string(s));
    HGLOBAL hw = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (hw) {
        wchar_t *wp = static_cast<wchar_t *>(GlobalLock(hw));
        if (wp) {
            if (!w.empty()) std::memcpy(wp, w.data(), w.size() * sizeof(wchar_t));
            wp[w.size()] = L'\0';
            GlobalUnlock(hw);
            SetClipboardData(CF_UNICODETEXT, hw);
        } else {
            GlobalFree(hw);
            hw = nullptr;
        }
    }
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
    if (!h) {
        CloseClipboard();
        return hw != nullptr;
    }
    char *p = static_cast<char *>(GlobalLock(h));
    if (!p) {
        GlobalFree(h);
        CloseClipboard();
        return hw != nullptr;
    }
    if (!s.empty()) std::memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
    GlobalUnlock(h);
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();
    return true;
}

inline std::string clipboard_get(HWND hwnd) {
    std::string out;
    if (!OpenClipboard(hwnd)) return out;
    if (HANDLE hw = GetClipboardData(CF_UNICODETEXT)) {
        const wchar_t *wp = static_cast<const wchar_t *>(GlobalLock(hw));
        if (wp) {
            out = utf8_from_wide(wp);
            GlobalUnlock(hw);
        }
    }
    if (out.empty()) {
        HANDLE h = GetClipboardData(CF_TEXT);
        if (h) {
            const char *p = static_cast<const char *>(GlobalLock(h));
            if (p) {
                out = p;
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    return out;
}

} // namespace sagrado
#endif // _WIN32
