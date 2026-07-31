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

#include <cstring>

namespace sagrado {

inline bool clipboard_set(HWND hwnd, std::string_view s) {
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
    if (!h) {
        CloseClipboard();
        return false;
    }
    char *p = static_cast<char *>(GlobalLock(h));
    if (!p) {
        GlobalFree(h);
        CloseClipboard();
        return false;
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
    HANDLE h = GetClipboardData(CF_TEXT);
    if (h) {
        const char *p = static_cast<const char *>(GlobalLock(h));
        if (p) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

} // namespace sagrado
#endif // _WIN32
