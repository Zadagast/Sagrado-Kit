# SagradoKit — native Win32 Appearance Engine + Editor
# Cross-compile from Linux with MinGW-w64; run under Wine.

# Force MinGW — Make's built-in CXX=g++ would otherwise win over ?=
CXX      := i686-w64-mingw32-g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iengine -static -static-libgcc -static-libstdc++
LDFLAGS  := -mwindows -lgdi32 -luser32 -lcomdlg32 -lcomctl32

BUILD    := build
EDITOR   := $(BUILD)/SagradoKitEditor.exe
TEXTEDIT := $(BUILD)/SagradoTextEdit.exe
JABBER   := $(BUILD)/SagradoJabber.exe
JABBER_CONNECT_SMOKE := $(BUILD)/jabber_connect_smoke.exe
JABBER_LDFLAGS := $(LDFLAGS) -lws2_32 -lwinhttp -lcrypt32 -lshell32
include apps/jabber/mbedtls_sources.mk
MBEDTLS_LIB := $(BUILD)/libmbedtls_jabber.a
MBEDTLS_CFLAGS := -O2 -Wall \
	-Iapps/jabber/xmpp \
	-Iapps/jabber/third_party/mbedtls/include \
	-DMBEDTLS_CONFIG_FILE='<jabber_mbedtls_config.h>'
CC_MINGW := i686-w64-mingw32-gcc
JABBER_CXXFLAGS := $(CXXFLAGS) -Iapps/jabber -Iapps/jabber/xmpp \
	-Iapps/jabber/third_party/mbedtls/include \
	-DMBEDTLS_CONFIG_FILE='<jabber_mbedtls_config.h>'

.PHONY: all clean run run-textedit run-jabber skins smoke jabber-connect-smoke emoji-pack

all: $(EDITOR) $(TEXTEDIT) $(JABBER) skins

# Full Noto emoji pack for the kit Emoji Picker (on-disk; not embedded).
emoji-pack: | $(BUILD)
	python3 tools/gen_emoji_pack.py --out $(BUILD)/emoji_pack
	@test -f $(BUILD)/emoji_pack/catalog.txt

$(BUILD):
	mkdir -p $(BUILD)

$(EDITOR): editor/main.cpp engine/*.h | $(BUILD)
	$(CXX) $(CXXFLAGS) editor/main.cpp -o $@ $(LDFLAGS)

$(TEXTEDIT): apps/textedit/main.cpp engine/*.h | $(BUILD)
	$(CXX) $(CXXFLAGS) apps/textedit/main.cpp -o $@ $(LDFLAGS)

$(MBEDTLS_LIB): $(MBEDTLS_SRCS) apps/jabber/xmpp/jabber_mbedtls_config.h | $(BUILD)
	@mkdir -p $(BUILD)/mbedtls
	@for f in $(MBEDTLS_SRCS); do \
	  obj=$(BUILD)/mbedtls/$$(basename $$f .c).o; \
	  echo "CC $$f"; \
	  $(CC_MINGW) $(MBEDTLS_CFLAGS) -c $$f -o $$obj || exit 1; \
	done
	i686-w64-mingw32-ar rcs $@ $(BUILD)/mbedtls/*.o

$(JABBER): apps/jabber/main.cpp apps/jabber/xmpp/*.h \
           apps/jabber/third_party/stb_image.h $(MBEDTLS_LIB) engine/*.h | $(BUILD)
	$(CXX) $(JABBER_CXXFLAGS) apps/jabber/main.cpp $(MBEDTLS_LIB) -o $@ $(JABBER_LDFLAGS)
	cp -f apps/jabber/providers.txt $(BUILD)/providers.txt
	@if [ ! -f $(BUILD)/emoji_pack/catalog.txt ]; then \
	  $(MAKE) emoji-pack; \
	fi

$(JABBER_CONNECT_SMOKE): apps/jabber/connect_smoke.cpp apps/jabber/xmpp/*.h $(MBEDTLS_LIB) | $(BUILD)
	$(CXX) $(JABBER_CXXFLAGS) -mconsole apps/jabber/connect_smoke.cpp $(MBEDTLS_LIB) \
	  -o $@ -lws2_32 -lcrypt32

jabber-connect-smoke: $(JABBER_CONNECT_SMOKE)
	@WINE=$$(command -v wine64 2>/dev/null || command -v wine 2>/dev/null); \
	if [ -z "$$WINE" ]; then echo "wine not found"; exit 127; fi; \
	$$WINE $(JABBER_CONNECT_SMOKE) yax.im

# Copy example skins next to the binary for Load dialog convenience.
skins: | $(BUILD)
	mkdir -p $(BUILD)/format/skins
	cp -f format/skins/*.sap $(BUILD)/format/skins/ 2>/dev/null || true
	# Default theme Hap (Gamespot) — preferred by editor / TextEdit / Jabber.
	cp -f "research/haps/Gamespot-1100.hap" $(BUILD)/format/skins/Gamespot-1100.hap
	# Art skins live in subfolders (images + .sap)
	for d in format/skins/*/; do \
	  [ -d "$$d" ] || continue; \
	  name=$$(basename "$$d"); \
	  mkdir -p "$(BUILD)/format/skins/$$name"; \
	  cp -f "$$d"* "$(BUILD)/format/skins/$$name/"; \
	done

run: $(EDITOR) skins
	@WINE=$$(command -v wine64 2>/dev/null || command -v wine 2>/dev/null); \
	if [ -z "$$WINE" ]; then \
	  echo "wine64/wine not found — this editor is a Win32 .exe (MinGW)."; \
	  echo "  WineHQ / Debian: install wine and use wine64, or: sudo apt install wine"; \
	  echo "  Or copy build/SagradoKitEditor.exe to a Windows box and run it there."; \
	  exit 127; \
	fi; \
	echo "launching with $$WINE"; \
	$$WINE $(EDITOR)

run-textedit: $(TEXTEDIT) skins
	@WINE=$$(command -v wine64 2>/dev/null || command -v wine 2>/dev/null); \
	if [ -z "$$WINE" ]; then \
	  echo "wine64/wine not found — Sagrado TextEdit is a Win32 .exe (MinGW)."; \
	  echo "  Or copy build/SagradoTextEdit.exe to a Windows box and run it there."; \
	  exit 127; \
	fi; \
	echo "launching TextEdit with $$WINE"; \
	$$WINE $(TEXTEDIT)

run-jabber: $(JABBER) skins
	@WINE=$$(command -v wine64 2>/dev/null || command -v wine 2>/dev/null); \
	if [ -z "$$WINE" ]; then \
	  echo "wine64/wine not found — Sagrado Jabber is a Win32 .exe (MinGW)."; \
	  echo "  Or copy build/SagradoJabber.exe to a Windows box and run it there."; \
	  exit 127; \
	fi; \
	echo "launching Jabber with $$WINE"; \
	$$WINE $(JABBER)

# Host-native smoke test (no Win32) — load/resolve/paint/roundtrip.
# HAPS=<dir> also runs the .hap checks over a directory of real themes.
smoke: engine/smoke_test.cpp engine/hap_test.cpp apps/textedit/smoke_paint.cpp apps/jabber/smoke_paint.cpp engine/*.h | $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -Iengine engine/smoke_test.cpp -o $(BUILD)/smoke_test
	$(BUILD)/smoke_test format/skins/stock.sap format/skins/slate.sap
	$(BUILD)/smoke_test format/skins/milk-redux/milk-redux.sap
	# Hap → .sap art/colour round-trip (parity); soft-complete fills empty slots
	$(BUILD)/smoke_test "research/haps/Milk Redux.hap"
	$(BUILD)/smoke_test "research/haps/Ashen.hap"
	$(BUILD)/smoke_test "research/haps/MacOS Classic.hap"
	$(BUILD)/smoke_test "research/haps/Aluminum Alloy - Toxic.hap"
	g++ -std=c++17 -O2 -Wall -Wextra -Iengine engine/hap_test.cpp -o $(BUILD)/hap_test
	$(BUILD)/hap_test $(HAPS)
	g++ -std=c++17 -O2 -Wall -Wextra -Iengine apps/textedit/smoke_paint.cpp \
	  -o $(BUILD)/textedit_smoke
	$(BUILD)/textedit_smoke format/skins/milk-redux/milk-redux.sap
	$(BUILD)/textedit_smoke "research/haps/Milk Redux.hap"
	$(BUILD)/textedit_smoke "research/haps/Boilerplate-Rusty.hap"
	g++ -std=c++17 -O2 -Wall -Wextra -Iengine apps/jabber/smoke_paint.cpp \
	  -o $(BUILD)/jabber_smoke
	$(BUILD)/jabber_smoke format/skins/milk-redux/milk-redux.sap
	$(BUILD)/jabber_smoke "research/haps/Milk Redux.hap"
	$(BUILD)/jabber_smoke "research/haps/Boilerplate-Rusty.hap"
	$(BUILD)/jabber_smoke "research/haps/International2.hap"

clean:
	rm -rf $(BUILD)
