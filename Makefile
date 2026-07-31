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
JABBER_LDFLAGS := $(LDFLAGS) -lws2_32 -lwinhttp -lsecur32 -lcrypt32

.PHONY: all clean run run-textedit run-jabber skins smoke

all: $(EDITOR) $(TEXTEDIT) $(JABBER) skins

$(BUILD):
	mkdir -p $(BUILD)

$(EDITOR): editor/main.cpp engine/*.h | $(BUILD)
	$(CXX) $(CXXFLAGS) editor/main.cpp -o $@ $(LDFLAGS)

$(TEXTEDIT): apps/textedit/main.cpp engine/*.h | $(BUILD)
	$(CXX) $(CXXFLAGS) apps/textedit/main.cpp -o $@ $(LDFLAGS)

$(JABBER): apps/jabber/main.cpp apps/jabber/xmpp/*.h apps/jabber/third_party/stb_image.h engine/*.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -Iapps/jabber apps/jabber/main.cpp -o $@ $(JABBER_LDFLAGS)
	cp -f apps/jabber/providers.txt $(BUILD)/providers.txt

# Copy example skins next to the binary for Load dialog convenience.
skins: | $(BUILD)
	mkdir -p $(BUILD)/format/skins
	cp -f format/skins/*.sap $(BUILD)/format/skins/ 2>/dev/null || true
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

clean:
	rm -rf $(BUILD)
