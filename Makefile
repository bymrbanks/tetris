CXX      := clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers

RAYLIB_PREFIX := $(shell brew --prefix raylib)
INCLUDES      := -I$(RAYLIB_PREFIX)/include
FRAMEWORKS    := -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
LIBS          := -L$(RAYLIB_PREFIX)/lib -lraylib $(FRAMEWORKS)
RAYLIB_STATIC := $(RAYLIB_PREFIX)/lib/libraylib.a

# ---- dev build: standalone binary, links against Homebrew dylib ----
tetris: main.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp -o tetris $(LIBS)

run: tetris
	./tetris

# ---- macOS .app bundle: static-linked, portable, ad-hoc signed ----
APP_NAME  := Tetris
APP_DIR   := $(APP_NAME).app
APP_BIN   := $(APP_DIR)/Contents/MacOS/$(APP_NAME)
APP_PLIST := $(APP_DIR)/Contents/Info.plist

app: $(APP_BIN) $(APP_PLIST)
	codesign --force --deep --sign - $(APP_DIR)
	@echo ""
	@echo "Built $(APP_DIR) — double-click it, or drag it into /Applications."

$(APP_BIN): main.cpp
	@mkdir -p $(APP_DIR)/Contents/MacOS $(APP_DIR)/Contents/Resources
	$(CXX) $(CXXFLAGS) $(INCLUDES) main.cpp -o $@ $(RAYLIB_STATIC) $(FRAMEWORKS)

$(APP_PLIST): Makefile
	@mkdir -p $(APP_DIR)/Contents
	@printf '%s\n' \
	  '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0">' \
	  '<dict>' \
	  '  <key>CFBundleName</key><string>$(APP_NAME)</string>' \
	  '  <key>CFBundleDisplayName</key><string>$(APP_NAME)</string>' \
	  '  <key>CFBundleIdentifier</key><string>com.example.tetris</string>' \
	  '  <key>CFBundleVersion</key><string>1.0</string>' \
	  '  <key>CFBundleShortVersionString</key><string>1.0</string>' \
	  '  <key>CFBundleExecutable</key><string>$(APP_NAME)</string>' \
	  '  <key>CFBundlePackageType</key><string>APPL</string>' \
	  '  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>' \
	  '  <key>CFBundleIconFile</key><string>AppIcon</string>' \
	  '  <key>NSHighResolutionCapable</key><true/>' \
	  '  <key>LSMinimumSystemVersion</key><string>11.0</string>' \
	  '  <key>NSPrincipalClass</key><string>NSApplication</string>' \
	  '</dict>' \
	  '</plist>' > $@

# ---- Web build (Emscripten / WebAssembly) ----
RAYLIB_SRC := raylib/src
WEB_DIR    := web
EMCC       := emcc

RAYLIB_C_FILES := \
	$(RAYLIB_SRC)/rcore.c     \
	$(RAYLIB_SRC)/rshapes.c   \
	$(RAYLIB_SRC)/rtextures.c \
	$(RAYLIB_SRC)/rtext.c     \
	$(RAYLIB_SRC)/rmodels.c   \
	$(RAYLIB_SRC)/raudio.c

EMCC_COMMON := \
	-DPLATFORM_WEB                \
	-DGRAPHICS_API_OPENGL_ES2     \
	-I$(RAYLIB_SRC)               \
	-I$(RAYLIB_SRC)/external/glfw/include \
	-O2

EMCC_LINK := \
	-s USE_GLFW=3                 \
	-s ASYNCIFY                   \
	-s ALLOW_MEMORY_GROWTH=1      \
	-s TOTAL_MEMORY=67108864      \
	-s FORCE_FILESYSTEM=1         \
	-s EXPORTED_FUNCTIONS=_main,_touchDown,_touchUp,_getGameState,_getScore,_getLines,_getLevel,_getDurationMs,_setHighScore \
	-s EXPORTED_RUNTIME_METHODS=ccall,cwrap

BUILD_DIR   := build
RAYLIB_OBJS := $(patsubst $(RAYLIB_SRC)/%.c,$(BUILD_DIR)/%.o,$(RAYLIB_C_FILES))

web: $(WEB_DIR)/index.html

$(BUILD_DIR)/%.o: $(RAYLIB_SRC)/%.c
	@mkdir -p $(BUILD_DIR)
	$(EMCC) $(EMCC_COMMON) -c $< -o $@

$(BUILD_DIR)/main.o: main.cpp
	@mkdir -p $(BUILD_DIR)
	$(EMCC) $(EMCC_COMMON) -std=c++17 -c main.cpp -o $@

SPRITES := sprites/cell_sprites.png

$(WEB_DIR)/index.html: $(BUILD_DIR)/main.o $(RAYLIB_OBJS) shell.html $(SPRITES)
	@mkdir -p $(WEB_DIR)
	$(EMCC) -o $@ $(BUILD_DIR)/main.o $(RAYLIB_OBJS) $(EMCC_COMMON) $(EMCC_LINK) \
	    --preload-file $(SPRITES) \
	    --shell-file shell.html

serve: web
	@echo "Serving on http://localhost:8000"
	@cd $(WEB_DIR) && python3 -m http.server 8000

clean:
	rm -f tetris
	rm -rf $(APP_DIR) $(WEB_DIR) $(BUILD_DIR)

.PHONY: run app web serve clean
