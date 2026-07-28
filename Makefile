# SagradoKit — native Win32 Appearance Engine + Editor
# Cross-compile from Linux with MinGW-w64; run under Wine.

# Force MinGW — Make's built-in CXX=g++ would otherwise win over ?=
CXX      := i686-w64-mingw32-g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iengine -static -static-libgcc -static-libstdc++
LDFLAGS  := -mwindows -lgdi32 -luser32 -lcomdlg32 -lcomctl32

BUILD    := build
EDITOR   := $(BUILD)/SagradoKitEditor.exe

.PHONY: all clean run skins smoke

all: $(EDITOR) skins

$(BUILD):
	mkdir -p $(BUILD)

$(EDITOR): editor/main.cpp engine/*.h | $(BUILD)
	$(CXX) $(CXXFLAGS) editor/main.cpp -o $@ $(LDFLAGS)

# Copy example skins next to the binary for Load dialog convenience.
skins: | $(BUILD)
	mkdir -p $(BUILD)/format/skins
	cp -f format/skins/*.skin.toml $(BUILD)/format/skins/

run: $(EDITOR) skins
	wine $(EDITOR)

# Host-native smoke test (no Win32) — load/resolve/paint/roundtrip.
smoke: engine/smoke_test.cpp engine/*.h | $(BUILD)
	g++ -std=c++17 -O2 -Wall -Wextra -Iengine engine/smoke_test.cpp -o $(BUILD)/smoke_test
	$(BUILD)/smoke_test format/skins/stock.skin.toml format/skins/slate.skin.toml

clean:
	rm -rf $(BUILD)
