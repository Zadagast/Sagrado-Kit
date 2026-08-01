// Headless check: bundled skins root after `make skins` lists Gamespot + Ooze.
#include "skin_catalog.h"

#include <cctype>
#include <cstdio>
#include <string>

int main(int argc, char **argv) {
    std::string root = argc > 1 ? argv[1] : "build";
    // Pretend exe_dir is build/ (where Sagrado*.exe live after make).
    auto skins = sagrado::list_bundled_skins(root);
    bool gamespot = false, ooze = false, ooze_dark = false, ashen = false;
    for (const auto &s : skins) {
        std::string n = s.name;
        for (char &c : n)
            c = (char)std::tolower((unsigned char)c);
        if (n.find("gamespot") != std::string::npos) gamespot = true;
        if (n == "ooze") ooze = true;
        if (n == "ooze dark") ooze_dark = true;
        if (n == "ashen") ashen = true;
    }
    std::string def = sagrado::find_default_bundled_skin(root);
    std::printf(
        "bundled=%d gamespot=%d ooze=%d ooze_dark=%d ashen=%d default=%s\n",
        (int)skins.size(), gamespot ? 1 : 0, ooze ? 1 : 0, ooze_dark ? 1 : 0,
        ashen ? 1 : 0, def.c_str());
    if ((int)skins.size() < 50) return 1;
    if (!gamespot || !ooze || !ooze_dark || !ashen) return 2;
    if (def.find("Gamespot") == std::string::npos) return 3;
    return 0;
}
