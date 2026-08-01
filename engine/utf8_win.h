// UTF-8 ⇄ UTF-16 helpers for Win32 text entry and the clipboard. Windows hands
// us UTF-16 (WM_CHAR on a Unicode window, CF_UNICODETEXT); the kit stores and
// paints UTF-8, so every crossing goes through here.
#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace sagrado {

inline void utf8_append_cp(std::string &out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xc0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xe0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(char(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(char(0xf0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(char(0x80 | (cp & 0x3f)));
    }
}

inline std::string utf8_from_wide(const wchar_t *w, int wlen = -1) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, wlen, out.data(), n, nullptr, nullptr);
    if (wlen == -1 && !out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

inline std::wstring wide_from_utf8(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

// WM_CHAR on a Unicode window delivers UTF-16 code units, so astral characters
// (emoji) arrive as a surrogate pair across two messages. Returns the UTF-8 for
// a completed character, or empty while a high surrogate is still pending.
inline std::string utf8_from_wm_char(unsigned unit) {
    static unsigned pending_high = 0;
    if (unit >= 0xd800 && unit <= 0xdbff) {
        pending_high = unit;
        return {};
    }
    std::string out;
    if (unit >= 0xdc00 && unit <= 0xdfff) {
        if (!pending_high) return {};
        unsigned cp = 0x10000 + ((pending_high - 0xd800) << 10) + (unit - 0xdc00);
        pending_high = 0;
        utf8_append_cp(out, cp);
        return out;
    }
    pending_high = 0;
    utf8_append_cp(out, unit);
    return out;
}

} // namespace sagrado
#endif // _WIN32
