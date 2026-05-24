// Classic Tetris in C++ with raylib.
//
// Build:  make
// Run:    make run     (or ./tetris)
//
// Controls (keyboard):
//   Left / Right  - move
//   Down          - soft drop
//   Up / X        - rotate clockwise
//   Z / Ctrl      - rotate counter-clockwise
//   Space         - hard drop
//   C / Shift     - hold piece
//   P / Esc       - pause
//   Enter         - start / restart
//   Q             - quit to menu (from pause/game-over)
//
// On the web build, the same actions are wired to on-screen touch
// buttons via the touchDown/touchUp JS bridge below.

#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// =================================================================
// Touch input bridge (for mobile / web)
// JS calls touchDown(action) / touchUp(action). Native build never
// sets these flags, so behaviour is unchanged off the web.
// =================================================================
enum TouchAction {
    TA_LEFT = 0, TA_RIGHT, TA_SOFT, TA_HARD,
    TA_ROTATE, TA_HOLD, TA_START, TA_PAUSE, TA_COUNT
};
static bool g_tkDown[TA_COUNT]    = {false};
static int  g_tkPressed[TA_COUNT] = {0};   // counter, so fast swipes can queue N presses per frame

static inline bool tkPressed(int a) { return g_tkPressed[a] > 0; }
static inline bool tkDown(int a)    { return g_tkDown[a]; }

#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE void touchDown(int a) {
    if (a >= 0 && a < TA_COUNT) {
        if (!g_tkDown[a]) g_tkPressed[a]++;   // edge → queue one press
        g_tkDown[a] = true;
    }
}
EMSCRIPTEN_KEEPALIVE void touchUp(int a) {
    if (a >= 0 && a < TA_COUNT) g_tkDown[a] = false;
}
} // extern "C"
// (Leaderboard getters live further down, after the static game state.)
#endif

// =================================================================
// Easing helpers — t in [0,1]; back/elastic may overshoot [0,1].
// =================================================================
static inline float easeOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

[[maybe_unused]] static inline float easeOutElastic(float t) {
    const float c4 = (2.0f * 3.14159265358979323846f) / 3.0f;
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

[[maybe_unused]] static inline float easeOutCubic(float t) {
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

[[maybe_unused]] static inline float easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

// =================================================================
// Constants
// =================================================================
constexpr int BOARD_W = 10;
constexpr int BOARD_H = 20;
constexpr int CELL    = 30;
constexpr int BOARD_PX_W = BOARD_W * CELL;
constexpr int BOARD_PX_H = BOARD_H * CELL;
constexpr int WIN_W   = 480;                              // portrait — mobile-first (logical units)
constexpr int WIN_H   = 800;
constexpr int RENDER_SCALE = 2;                           // framebuffer is RENDER_SCALE × logical;
                                                          // the browser then downscales, which is
                                                          // sharper than upscaling. Every draw call
                                                          // still uses logical (480×800) coords —
                                                          // a Camera2D in frame() applies the zoom.
constexpr int BOARD_X = (WIN_W - BOARD_PX_W) / 2;         // 90 → board horizontally centered
constexpr int BOARD_Y = 140;                              // 140 → leaves 130px top bar
constexpr int HUD_TOP_Y    = 10;                          // top bar (HOLD / SCORE / NEXT)
constexpr int HUD_BOTTOM_Y = BOARD_Y + BOARD_PX_H + 12;   // 752 → strip below board

constexpr float DAS_DELAY   = 0.16f;   // delayed auto-shift initial wait
constexpr float DAS_REPEAT  = 0.04f;   // shift repeat interval
constexpr float LOCK_DELAY  = 0.50f;   // grace period before locking on ground
constexpr float CLEAR_FLASH = 0.40f;   // line-clear animation length (5-frame shatter + L→R stagger)

// =================================================================
// Tetromino shapes — 7 pieces, 4 rotations, 4x4 grid each
// =================================================================
const int PIECES[7][4][4][4] = {
    // I
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}
    },
    // O
    {
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    },
    // T
    {
        {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    // S
    {
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
        {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    // Z
    {
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}
    },
    // J
    {
        {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    },
    // L
    {
        {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }
};

// index 0 = empty, 1..7 = piece colors in PIECES order (I,O,T,S,Z,J,L)
const Color PIECE_COLOR[8] = {
    {  20,  20,  30, 255 },   // empty
    {   0, 240, 240, 255 },   // I  cyan
    { 240, 220,  40, 255 },   // O  yellow
    { 180,  60, 240, 255 },   // T  purple
    {  40, 220,  60, 255 },   // S  green
    { 240,  60,  60, 255 },   // Z  red
    {  60, 100, 240, 255 },   // J  blue
    { 240, 150,  40, 255 }    // L  orange
};

// Background gradient changes per stage (level), giving "stages" a feel.
const Color STAGE_BG[10] = {
    {  20,  22,  36, 255 },
    {  22,  30,  48, 255 },
    {  34,  20,  52, 255 },
    {  48,  20,  44, 255 },
    {  52,  30,  20, 255 },
    {  46,  44,  18, 255 },
    {  20,  46,  30, 255 },
    {  18,  46,  46, 255 },
    {  22,  22,  60, 255 },
    {  44,  16,  16, 255 }
};

// =================================================================
// State
// =================================================================
struct Piece { int type; int rot; int x; int y; };

enum class GameState { Menu, Playing, Paused, GameOver };

static int        board[BOARD_H][BOARD_W];
static Piece      current;
static Piece      nextP;
static int        holdT = -1;
static bool       canHold = true;
static int        score = 0;
static int        lines = 0;
static int        level = 1;
static int        highScore = 0;
static GameState  state = GameState::Menu;
static double     gameStartTime = 0.0;   // GetTime() at last resetGame(); used for duration export

#ifdef __EMSCRIPTEN__
extern "C" {
// Read-only getters for the leaderboard overlay (JS polls these).
EMSCRIPTEN_KEEPALIVE int  getGameState()   { return (int)state; }
EMSCRIPTEN_KEEPALIVE int  getScore()       { return score; }
EMSCRIPTEN_KEEPALIVE int  getLines()       { return lines; }
EMSCRIPTEN_KEEPALIVE int  getLevel()       { return level; }
EMSCRIPTEN_KEEPALIVE long getDurationMs()  { return (long)((GetTime() - gameStartTime) * 1000.0); }
EMSCRIPTEN_KEEPALIVE void setHighScore(int v) { if (v > highScore) highScore = v; }
} // extern "C"
#endif
static float      dropTimer = 0.0f;
static float      dropInterval = 1.0f;
static float      lockTimer = 0.0f;
static float      dasTimer = 0.0f;
static int        dasDir = 0;
static bool       clearAnimating = false;
static float      clearAnimTimer = 0.0f;
static int        clearingLines[4];
static int        numClearing = 0;
static int        lastClearCount = 0;
static float      lastClearText = 0.0f;
static int        softDropPending = 0;   // accumulated soft-drop points awaiting popup

static std::mt19937       rng;
static std::vector<int>   bag;

// ---- Juice / animation state (purely visual; never affects tick logic) ----
static float  lockTime[BOARD_H][BOARD_W] = {{0}};   // GetTime() when each cell was locked; -1 = never
static double spawnTime  = -1.0;                    // GetTime() when `current` was last (re)spawned
static double shakeStart = -1.0;                    // GetTime() when screen-shake started
static float  shakeMag   = 0.0f;                    // peak shake magnitude in pixels

struct ScorePopup { float x, y; int value; double bornAt; bool active; };
static ScorePopup popups[16] = {};

static void spawnPopup(int x, int y, int value) {
    if (value <= 0) return;
    for (int i = 0; i < 16; i++) {
        if (!popups[i].active) {
            popups[i].x      = (float)x;
            popups[i].y      = (float)y;
            popups[i].value  = value;
            popups[i].bornAt = GetTime();
            popups[i].active = true;
            return;
        }
    }
    // No free slot — overwrite the oldest popup so newer scores stay visible.
    int oldest = 0;
    for (int i = 1; i < 16; i++) {
        if (popups[i].bornAt < popups[oldest].bornAt) oldest = i;
    }
    popups[oldest].x      = (float)x;
    popups[oldest].y      = (float)y;
    popups[oldest].value  = value;
    popups[oldest].bornAt = GetTime();
    popups[oldest].active = true;
}

static void drawPopups() {
    double now = GetTime();
    for (int i = 0; i < 16; i++) {
        if (!popups[i].active) continue;
        double elapsed = now - popups[i].bornAt;
        if (elapsed >= 0.8) { popups[i].active = false; continue; }
        float t = (float)(elapsed / 0.8);
        float yOff = (float)(elapsed * 60.0);
        unsigned char a = (unsigned char)(255.0f * std::max(0.0f, 1.0f - t));
        int fontSize = (popups[i].value >= 500) ? 28
                      : (popups[i].value >= 100) ? 24 : 22;
        Color col = (popups[i].value >= 500) ? ORANGE : YELLOW;
        col.a = a;
        const char* txt = TextFormat("+%d", popups[i].value);
        int tw = MeasureText(txt, fontSize);
        // Subtle shadow for legibility on bright cells.
        Color sh = { 0, 0, 0, a };
        DrawText(txt, (int)popups[i].x - tw / 2 + 1,
                 (int)(popups[i].y - yOff) + 1, fontSize, sh);
        DrawText(txt, (int)popups[i].x - tw / 2,
                 (int)(popups[i].y - yOff), fontSize, col);
    }
}

// =================================================================
// Utilities
// =================================================================
static inline int cellOf(const Piece& p, int r, int c) {
    return PIECES[p.type][p.rot][r][c];
}

static int drawNextType() {
    if (bag.empty()) {
        bag = {0, 1, 2, 3, 4, 5, 6};
        std::shuffle(bag.begin(), bag.end(), rng);
    }
    int t = bag.back();
    bag.pop_back();
    return t;
}

static Piece spawnPiece(int type) {
    Piece p;
    p.type = type;
    p.rot  = 0;
    p.x    = 3;
    p.y    = (type == 0) ? -1 : 0;   // I spawns one row higher (matches NES feel)
    return p;
}

static bool isValid(const Piece& p) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cellOf(p, r, c)) continue;
            int bx = p.x + c;
            int by = p.y + r;
            if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) return false;
            if (by >= 0 && board[by][bx])                 return false;
        }
    }
    return true;
}

static void updateDropInterval() {
    // NES-style speed curve: each level ~17% faster, capped at 50ms.
    float v = 1.0f * std::pow(0.83f, (float)(level - 1));
    if (v < 0.05f) v = 0.05f;
    dropInterval = v;
}

static bool tryMove(int dx, int dy) {
    Piece p = current;
    p.x += dx;
    p.y += dy;
    if (!isValid(p)) return false;
    current = p;
    lockTimer = 0.0f;
    return true;
}

static bool tryRotate(int dir) {
    Piece p = current;
    p.rot = (p.rot + dir + 4) % 4;
    if (isValid(p)) { current = p; lockTimer = 0; return true; }
    // basic wall kicks: try nudging horizontally (and vertically for I).
    int kicks[] = { 1, -1, 2, -2 };
    for (int k : kicks) {
        Piece q = p;
        q.x += k;
        if (isValid(q)) { current = q; lockTimer = 0; return true; }
    }
    // give I-piece an upward kick attempt
    if (p.type == 0) {
        Piece q = p;
        q.y -= 1;
        if (isValid(q)) { current = q; lockTimer = 0; return true; }
    }
    return false;
}

static int ghostY() {
    Piece p = current;
    while (true) {
        Piece q = p;
        q.y++;
        if (!isValid(q)) return p.y;
        p = q;
    }
}

static void spawnNext(bool fromHold = false);

static void lockPiece() {
    // Stamp piece onto board.
    double now = GetTime();
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!cellOf(current, r, c)) continue;
            int by = current.y + r;
            int bx = current.x + c;
            if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
                board[by][bx] = current.type + 1;
                lockTime[by][bx] = (float)now;   // juice: per-cell pop timer
            }
        }
    }

    // Flush accumulated soft-drop points as a single floating "+N".
    if (softDropPending > 0) {
        spawnPopup(BOARD_X + current.x * CELL + 2 * CELL,
                   BOARD_Y + current.y * CELL + 2 * CELL, softDropPending);
        softDropPending = 0;
    }

    // Find full rows.
    numClearing = 0;
    for (int y = 0; y < BOARD_H; y++) {
        bool full = true;
        for (int x = 0; x < BOARD_W; x++) {
            if (!board[y][x]) { full = false; break; }
        }
        if (full) clearingLines[numClearing++] = y;
    }

    // Screen-shake on a Tetris (4-line clear).
    if (numClearing == 4) {
        shakeStart = now;
        shakeMag   = 8.0f;
    }

    if (numClearing > 0) {
        clearAnimating = true;
        clearAnimTimer = 0.0f;
    } else {
        spawnNext();
    }
}

static void finishLineClear() {
    static const int LINE_SCORE[5] = { 0, 100, 300, 500, 800 };
    int delta = LINE_SCORE[numClearing] * level;
    score += delta;
    // Floating "+N" centered over the cleared rows.
    int midRow = clearingLines[numClearing / 2];
    spawnPopup(BOARD_X + BOARD_PX_W / 2,
               BOARD_Y + midRow * CELL + CELL / 2, delta);
    lines += numClearing;
    lastClearCount = numClearing;
    lastClearText  = 1.5f;

    int newLevel = lines / 10 + 1;
    if (newLevel != level) {
        level = newLevel;
        updateDropInterval();
    }

    // Remove cleared lines, dropping rows above.
    for (int i = 0; i < numClearing; i++) {
        int line = clearingLines[i];
        for (int y = line; y > 0; y--) {
            for (int x = 0; x < BOARD_W; x++) board[y][x] = board[y - 1][x];
        }
        for (int x = 0; x < BOARD_W; x++) board[0][x] = 0;
    }

    numClearing = 0;
    clearAnimating = false;
    spawnNext();
}

static void spawnNext(bool fromHold) {
    if (!fromHold) {
        current = nextP;
        nextP   = spawnPiece(drawNextType());
        canHold = true;
    }
    lockTimer = 0;
    dropTimer = 0;
    spawnTime = GetTime();   // juice: trigger pop-in animation
    if (!isValid(current)) {
        state = GameState::GameOver;
        if (score > highScore) highScore = score;
    }
}

static void doHold() {
    if (!canHold) return;
    if (holdT == -1) {
        holdT  = current.type;
        spawnNext();
    } else {
        int t  = holdT;
        holdT  = current.type;
        current = spawnPiece(t);
        spawnNext(true);
    }
    canHold = false;
}

static void hardDrop() {
    int dropped = 0;
    while (tryMove(0, 1)) dropped++;
    int delta = dropped * 2;
    score += delta;
    // Floating "+N" centered on the piece's resting position.
    if (delta > 0) {
        spawnPopup(BOARD_X + current.x * CELL + 2 * CELL,
                   BOARD_Y + current.y * CELL + 2 * CELL, delta);
    }
    lockPiece();
}

static void resetGame() {
    for (int y = 0; y < BOARD_H; y++)
        for (int x = 0; x < BOARD_W; x++) board[y][x] = 0;
    // Sentinel -1 keeps the pop animation from firing on a fresh board.
    for (int y = 0; y < BOARD_H; y++)
        for (int x = 0; x < BOARD_W; x++) lockTime[y][x] = -1.0f;
    score = 0;
    lines = 0;
    level = 1;
    holdT = -1;
    canHold = true;
    bag.clear();
    dropTimer = 0;
    lockTimer = 0;
    dasTimer  = 0;
    dasDir    = 0;
    clearAnimating = false;
    numClearing = 0;
    lastClearCount = 0;
    lastClearText  = 0;
    softDropPending = 0;
    // Reset all juice/animation state.
    spawnTime  = -1.0;
    shakeStart = -1.0;
    shakeMag   = 0.0f;
    for (int i = 0; i < 16; i++) popups[i].active = false;
    nextP   = spawnPiece(drawNextType());
    current = spawnPiece(drawNextType());
    updateDropInterval();
    gameStartTime = GetTime();   // for getDurationMs() export
}

// =================================================================
// Update
// =================================================================
static void updateGame(float dt) {
    if (clearAnimating) {
        clearAnimTimer += dt;
        if (clearAnimTimer >= CLEAR_FLASH) finishLineClear();
        if (lastClearText > 0) lastClearText -= dt;
        return;
    }
    if (lastClearText > 0) lastClearText -= dt;

    // ---- Touch horizontal pulses (drain everything queued by JS this frame) ----
    while (g_tkPressed[TA_LEFT]  > 0) { tryMove(-1, 0); g_tkPressed[TA_LEFT]--;  }
    while (g_tkPressed[TA_RIGHT] > 0) { tryMove( 1, 0); g_tkPressed[TA_RIGHT]--; }

    // ---- Keyboard horizontal with DAS ----
    if (IsKeyPressed(KEY_LEFT))  { tryMove(-1, 0); dasDir = -1; dasTimer = -DAS_DELAY; }
    if (IsKeyPressed(KEY_RIGHT)) { tryMove( 1, 0); dasDir =  1; dasTimer = -DAS_DELAY; }

    bool held = (dasDir == -1 && IsKeyDown(KEY_LEFT)) ||
                (dasDir ==  1 && IsKeyDown(KEY_RIGHT));
    if (!held) {
        dasDir = 0;
    } else {
        dasTimer += dt;
        while (dasTimer >= 0.0f) {
            tryMove(dasDir, 0);
            dasTimer -= DAS_REPEAT;
        }
    }

    // ---- Rotation ----
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_X) || tkPressed(TA_ROTATE))  tryRotate( 1);
    if (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_LEFT_CONTROL) ||
        IsKeyPressed(KEY_RIGHT_CONTROL))                                     tryRotate(-1);

    // ---- Soft drop / gravity ----
    bool soft = IsKeyDown(KEY_DOWN) || tkDown(TA_SOFT);
    float effInterval = soft ? std::min(dropInterval, 0.04f) : dropInterval;

    dropTimer += dt;
    while (dropTimer >= effInterval) {
        dropTimer -= effInterval;
        if (!tryMove(0, 1)) break;
        if (soft) { score += 1; softDropPending += 1; }
    }

    // ---- Lock delay when on ground ----
    Piece below = current;
    below.y++;
    bool onGround = !isValid(below);
    if (onGround) {
        lockTimer += dt;
        if (lockTimer >= LOCK_DELAY) lockPiece();
    } else {
        lockTimer = 0;
    }

    // ---- Hard drop ----
    if (IsKeyPressed(KEY_SPACE) || tkPressed(TA_HARD)) hardDrop();

    // ---- Hold ----
    if (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_LEFT_SHIFT) ||
        IsKeyPressed(KEY_RIGHT_SHIFT) || tkPressed(TA_HOLD)) doHold();

    // ---- Pause ----
    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) || tkPressed(TA_PAUSE)) state = GameState::Paused;
}

// =================================================================
// Rendering
// =================================================================
static Color lighten(Color c, int by) {
    c.r = (unsigned char)std::min(255, (int)c.r + by);
    c.g = (unsigned char)std::min(255, (int)c.g + by);
    c.b = (unsigned char)std::min(255, (int)c.b + by);
    return c;
}
static Color darken(Color c, float k) {
    c.r = (unsigned char)((float)c.r * k);
    c.g = (unsigned char)((float)c.g * k);
    c.b = (unsigned char)((float)c.b * k);
    return c;
}

// Single-row 8-frame animation sheet (sprites/cell_sprites.png):
//   col = animation frame (0 = intact, 1-4 = crack→burst, 5-7 = drift)
// The source is max-channel-grayscale, so multiplicative tint with
// PIECE_COLOR[type] recolors each frame to the matching piece hue
// while preserving the shading. Loaded in main().
static Texture2D animSheet = {};
static const float SHEET_COLS    = 8.0f;
static const float SHEET_ROWS    = 1.0f;
static const float SHEET_W       = 1648.0f;
static const float SHEET_H       = 208.0f;
static const float SHEET_CELL_W  = SHEET_W / SHEET_COLS;   // 206
static const float SHEET_CELL_H  = SHEET_H / SHEET_ROWS;   // 208

static void drawCell(int px, int py, int type, float alpha = 1.0f, int sz = CELL, int frame = 0) {
    if (animSheet.id > 0 && type >= 1 && type <= 7 && frame >= 0 && frame < (int)SHEET_COLS) {
        // Multiplicative tint: gray source × piece color = piece-colored candy.
        Color tint = PIECE_COLOR[type];
        tint.a = (unsigned char)(255.0f * alpha);
        const float bleed = (float)sz * 0.05f;
        Rectangle src = {
            (float)frame * SHEET_CELL_W,
            0.0f,
            SHEET_CELL_W,
            SHEET_CELL_H
        };
        Rectangle dst = {
            (float)px - bleed * 0.5f,
            (float)py - bleed * 0.5f,
            (float)sz + bleed,
            (float)sz + bleed
        };
        DrawTexturePro(animSheet, src, dst, {0, 0}, 0.0f, tint);
        return;
    }

    // Fallback: procedural candy rendering (used if the sprite ever fails to load).
    Color base = PIECE_COLOR[type];
    base.a = (unsigned char)(255.0f * alpha);

    const float fsz       = (float)sz;
    const float roundness = 0.28f;                  // ~28% of sz -> candy-like corners
    const int   segments  = 8;                      // smoothness of rounded corners
    const int   shadowOff = std::max(1, sz / 12);   // drop shadow offset, scales with sz

    // --------------------------------------------------------------
    // 1) Soft drop shadow (slightly offset down/right, dark, low alpha)
    // --------------------------------------------------------------
    {
        Rectangle sr = { (float)(px + shadowOff), (float)(py + shadowOff), fsz, fsz };
        Color shadow = { 0, 0, 0, (unsigned char)(90.0f * alpha) };
        DrawRectangleRounded(sr, roundness, segments, shadow);
    }

    // --------------------------------------------------------------
    // 2) Rounded body in the piece color
    // --------------------------------------------------------------
    Rectangle body = { (float)px, (float)py, fsz, fsz };
    DrawRectangleRounded(body, roundness, segments, base);

    // --------------------------------------------------------------
    // 3) Darker gradient wash on the bottom half (candy curve / depth)
    //    Inset slightly so the gradient stays inside the rounded body.
    // --------------------------------------------------------------
    {
        int inset = std::max(1, sz / 16);
        int gx = px + inset;
        int gy = py + sz / 2;
        int gw = sz - 2 * inset;
        int gh = sz / 2 - inset;
        Color top = { 0, 0, 0, 0 };                                       // transparent
        Color bot = darken(base, 0.55f);
        bot.a = (unsigned char)(140.0f * alpha);                          // gentle darken
        DrawRectangleGradientV(gx, gy, gw, gh, top, bot);
    }

    // --------------------------------------------------------------
    // 4) Soft lighter wash on the top half (overall glossy bias)
    //    Helps the body read as 3D before the specular highlight.
    // --------------------------------------------------------------
    {
        int inset = std::max(1, sz / 16);
        int gx = px + inset;
        int gy = py + inset;
        int gw = sz - 2 * inset;
        int gh = sz / 2 - inset;
        Color top = lighten(base, 60);
        top.a = (unsigned char)(110.0f * alpha);
        Color bot = { 255, 255, 255, 0 };                                 // transparent
        DrawRectangleGradientV(gx, gy, gw, gh, top, bot);
    }

    // --------------------------------------------------------------
    // 5) Bright rim light along the very top edge
    // --------------------------------------------------------------
    {
        int rimInset = std::max(2, sz / 8);
        int ry = py + std::max(1, sz / 20);
        int rh = std::max(1, sz / 14);
        Color rim = lighten(base, 110);
        rim.a = (unsigned char)(170.0f * alpha);
        DrawRectangle(px + rimInset, ry, sz - 2 * rimInset, rh, rim);
    }

    // --------------------------------------------------------------
    // 6) Specular highlight ellipse near the top-left.
    //    Stack a couple of ellipses with decreasing alpha + growing
    //    radius to fake a soft blur without per-pixel work.
    // --------------------------------------------------------------
    {
        int hx = px + sz / 4;
        int hy = py + sz / 4;
        int hrx = std::max(2, sz / 5);
        int hry = std::max(1, sz / 9);
        // Outer soft glow
        Color glow = { 255, 255, 255, (unsigned char)(40.0f * alpha) };
        DrawEllipse(hx, hy, (float)(hrx + sz / 14), (float)(hry + sz / 18), glow);
        // Mid layer
        Color mid = { 255, 255, 255, (unsigned char)(90.0f * alpha) };
        DrawEllipse(hx, hy, (float)hrx, (float)hry, mid);
        // Bright core
        Color core = { 255, 255, 255, (unsigned char)(170.0f * alpha) };
        int crx = std::max(1, hrx * 2 / 3);
        int cry = std::max(1, hry * 2 / 3);
        DrawEllipse(hx, hy, (float)crx, (float)cry, core);
    }

    // --------------------------------------------------------------
    // 7) Subtle outer rim light along the rounded outline
    //    (just inside the dark outline, for that polished edge).
    // --------------------------------------------------------------
    {
        Color innerRim = lighten(base, 90);
        innerRim.a = (unsigned char)(110.0f * alpha);
        DrawRectangleRoundedLines(body, roundness, segments, innerRim);
    }

    // --------------------------------------------------------------
    // 8) Final crisp dark outline so cells pop against the background
    // --------------------------------------------------------------
    {
        Color border = { 0, 0, 0, (unsigned char)(190.0f * alpha) };
        Rectangle ob = { (float)px - 0.5f, (float)py - 0.5f, fsz + 1.0f, fsz + 1.0f };
        DrawRectangleRoundedLines(ob, roundness, segments, border);
    }
}

static void drawGhostCell(int px, int py, int type) {
    Color c = PIECE_COLOR[type];
    Color fill = c; fill.a = 40;
    DrawRectangle(px + 2, py + 2, CELL - 4, CELL - 4, fill);
    Color border = c; border.a = 140;
    DrawRectangleLines(px, py, CELL, CELL, border);
}

static void drawBoard() {
    DrawRectangle(BOARD_X, BOARD_Y, BOARD_PX_W, BOARD_PX_H, { 12, 12, 20, 255 });

    // empty grid lines + locked cells (with per-cell "pop" bounce on freshly-locked tiles)
    double now = GetTime();

    // Line-clear animation parameters: 4-frame shatter (frames 1..4) per cell,
    // staggered left-to-right so the row blows up like a sweep.
    constexpr float CLEAR_STAGGER     = 0.020f;   // 20 ms per column delay
    constexpr float CLEAR_PER_CELL    = 0.20f;    // each cell's animation duration

    for (int y = 0; y < BOARD_H; y++) {
        // Is this row in the clearingLines[] set?
        bool clearing = false;
        if (clearAnimating) {
            for (int i = 0; i < numClearing; i++) {
                if (clearingLines[i] == y) { clearing = true; break; }
            }
        }

        for (int x = 0; x < BOARD_W; x++) {
            int px = BOARD_X + x * CELL;
            int py = BOARD_Y + y * CELL;

            if (board[y][x]) {
                if (clearing) {
                    // Staggered shatter: column x starts at t = x * CLEAR_STAGGER
                    float localT = (clearAnimTimer - (float)x * CLEAR_STAGGER) / CLEAR_PER_CELL;
                    if (localT >= 1.0f) {
                        // animation done — skip drawing (cell will be removed by finishLineClear)
                    } else {
                        int frame = 0;
                        if (localT > 0.0f) {
                            // map localT 0..1 → frame 1..4 (skip frame 0; we want the crack to *start* immediately)
                            frame = 1 + (int)(localT * 4.0f);
                            if (frame > 4) frame = 4;
                        }
                        drawCell(px, py, board[y][x], 1.0f, CELL, frame);
                    }
                } else {
                    float lt = lockTime[y][x];
                    float elapsed = (lt >= 0.0f) ? (float)(now - lt) : 1.0f;
                    if (elapsed >= 0.0f && elapsed < 0.15f) {
                        float t = elapsed / 0.15f;
                        float overshoot = easeOutBack(t);
                        float pulse = 1.0f + 0.12f * (1.0f - std::pow(2.0f * t - 1.0f, 2.0f)) * overshoot;
                        int sz = (int)std::round((float)CELL * pulse);
                        int off = (sz - CELL) / 2;
                        drawCell(px - off, py - off, board[y][x], 1.0f, sz);
                    } else {
                        drawCell(px, py, board[y][x]);
                    }
                }
            } else {
                DrawRectangleLines(px, py, CELL, CELL, { 30, 30, 45, 255 });
            }
        }
    }

    if (clearAnimating) {
        // No flash overlay — the shatter sprite frames 3-4 have a flash baked in.
    } else {
        // Ghost piece
        int gy = ghostY();
        if (gy != current.y) {
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++)
                    if (cellOf(current, r, c) && gy + r >= 0)
                        drawGhostCell(BOARD_X + (current.x + c) * CELL,
                                      BOARD_Y + (gy + r) * CELL,
                                      current.type + 1);
        }
        // Current piece (slightly transparent when in lock-delay window for feedback)
        float a = 1.0f;
        Piece below = current; below.y++;
        if (!isValid(below) && lockTimer > 0) {
            a = 0.65f + 0.35f * (1.0f - std::min(1.0f, lockTimer / LOCK_DELAY));
        }
        // Spawn-in pop: scale 0.4 → 1.0 over 120ms using easeOutBack.
        float spawnElapsed = (spawnTime >= 0.0) ? (float)(GetTime() - spawnTime) : 1.0f;
        bool popping = (spawnElapsed >= 0.0f && spawnElapsed < 0.12f);
        float spawnScale = 1.0f;
        if (popping) {
            float st = spawnElapsed / 0.12f;
            spawnScale = 0.4f + 0.6f * easeOutBack(st);
        }
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (cellOf(current, r, c) && current.y + r >= 0) {
                    int basePx = BOARD_X + (current.x + c) * CELL;
                    int basePy = BOARD_Y + (current.y + r) * CELL;
                    if (popping) {
                        int sz  = (int)std::round((float)CELL * spawnScale);
                        int off = (CELL - sz) / 2;
                        drawCell(basePx + off, basePy + off,
                                 current.type + 1, a, sz);
                    } else {
                        drawCell(basePx, basePy, current.type + 1, a);
                    }
                }
    }

    DrawRectangleLinesEx({ (float)BOARD_X - 2, (float)BOARD_Y - 2,
                           (float)BOARD_PX_W + 4, (float)BOARD_PX_H + 4 }, 2, WHITE);
}

static void drawPiecePreview(int x, int y, int type, const char* label, int cellPx = CELL) {
    int labelSize = (cellPx >= 24) ? 20 : 14;
    DrawText(label, x, y - (labelSize + 4), labelSize, WHITE);
    int boxW = 4 * cellPx + 16;
    int boxH = 4 * cellPx + 16;
    DrawRectangle(x, y, boxW, boxH, { 12, 12, 20, 255 });
    DrawRectangleLinesEx({ (float)x, (float)y, (float)boxW, (float)boxH }, 2, WHITE);
    if (type < 0) return;

    int minR = 4, maxR = -1, minC = 4, maxC = -1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (PIECES[type][0][r][c]) {
                minR = std::min(minR, r);
                maxR = std::max(maxR, r);
                minC = std::min(minC, c);
                maxC = std::max(maxC, c);
            }
    int pieceW = (maxC - minC + 1) * cellPx;
    int pieceH = (maxR - minR + 1) * cellPx;
    int ox = x + (boxW - pieceW) / 2 - minC * cellPx;
    int oy = y + (boxH - pieceH) / 2 - minR * cellPx;

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (PIECES[type][0][r][c])
                drawCell(ox + c * cellPx, oy + r * cellPx, type + 1, 1.0f, cellPx);
}

static void drawHud() {
    // ---- Top bar (HUD_TOP_Y .. ~130): HOLD on left, SCORE centered, NEXT on right ----
    constexpr int HUD_CELL = 18;
    int boxW = 4 * HUD_CELL + 16;            // 88
    int holdX = 15;
    int previewY = HUD_TOP_Y + 22;           // leaves space for label above
    int nextX = WIN_W - 15 - boxW;

    drawPiecePreview(holdX, previewY, holdT,      "HOLD", HUD_CELL);
    drawPiecePreview(nextX, previewY, nextP.type, "NEXT", HUD_CELL);

    // SCORE big in the center of the top bar
    const char* scoreStr = TextFormat("%d", score);
    int scoreSize = 36;
    int scoreW = MeasureText(scoreStr, scoreSize);
    DrawText(scoreStr, (WIN_W - scoreW) / 2, HUD_TOP_Y + 30, scoreSize, YELLOW);
    const char* scoreLabel = "SCORE";
    int slw = MeasureText(scoreLabel, 12);
    DrawText(scoreLabel, (WIN_W - slw) / 2, HUD_TOP_Y + 14, 12, GRAY);

    // ---- Last clear callout (centered over board) ----
    if (lastClearText > 0 && lastClearCount > 0) {
        const char* tag = "";
        Color col = YELLOW;
        switch (lastClearCount) {
            case 1: tag = "SINGLE";  col = LIGHTGRAY; break;
            case 2: tag = "DOUBLE";  col = SKYBLUE;   break;
            case 3: tag = "TRIPLE";  col = ORANGE;    break;
            case 4: tag = "TETRIS!"; col = RED;       break;
        }
        unsigned char a = (unsigned char)(255 * std::min(1.0f, lastClearText));
        Color c = col; c.a = a;
        int tw = MeasureText(tag, 30);
        DrawText(tag, BOARD_X + (BOARD_PX_W - tw) / 2,
                 BOARD_Y + BOARD_PX_H / 2 - 20, 30, c);
    }

    // ---- Bottom strip: LV · LINES · HIGH ----
    int by = HUD_BOTTOM_Y;
    int fs = 20;
    const char* lvS    = TextFormat("LV %d",    level);
    const char* linesS = TextFormat("LINES %d", lines);
    const char* highS  = TextFormat("HIGH %d",  highScore);
    DrawText(lvS, 20, by, fs, GREEN);
    int lw = MeasureText(linesS, fs);
    DrawText(linesS, (WIN_W - lw) / 2, by, fs, ORANGE);
    int hw = MeasureText(highS, fs);
    DrawText(highS, WIN_W - 20 - hw, by, fs, { 255, 180, 180, 255 });

    // Floating "+N" score popups drawn on top of the board/HUD.
    drawPopups();
}

static void drawMenu() {
    // Animated falling pieces in the background.
    static float t = 0; t += GetFrameTime();
    for (int i = 0; i < 8; i++) {
        float fx = std::fmod((float)(i * 137) + t * 40.0f * (1 + i * 0.1f), (float)WIN_W);
        float fy = std::fmod((float)(i * 211) + t * 60.0f, (float)WIN_H + 120) - 60.0f;
        int t1 = (i + (int)(t * 0.3f)) % 7;
        DrawRectangle((int)fx, (int)fy, CELL, CELL,
                      { PIECE_COLOR[t1+1].r, PIECE_COLOR[t1+1].g, PIECE_COLOR[t1+1].b, 60 });
    }

    const char* title = "TETRIS";
    int titleSize = 96;
    int tw = MeasureText(title, titleSize);
    // shadow
    DrawText(title, (WIN_W - tw) / 2 + 4, 160 + 4, titleSize, { 0, 0, 0, 180 });
    DrawText(title, (WIN_W - tw) / 2, 160, titleSize, WHITE);

    const char* sub = "Classic C++ Edition";
    int sw = MeasureText(sub, 22);
    DrawText(sub, (WIN_W - sw) / 2, 280, 22, GRAY);

#ifdef __EMSCRIPTEN__
    const char* start = "TAP to START";
#else
    const char* start = "Press ENTER to START";
#endif
    int stw = MeasureText(start, 28);
    bool blink = ((int)(GetTime() * 2) % 2) == 0;
    if (blink) DrawText(start, (WIN_W - stw) / 2, 430, 28, YELLOW);

    if (highScore > 0) {
        const char* hsLabel = TextFormat("HIGH SCORE: %d", highScore);
        int hsw = MeasureText(hsLabel, 22);
        DrawText(hsLabel, (WIN_W - hsw) / 2, 510, 22, ORANGE);
    }

#ifdef __EMSCRIPTEN__
    const char* ctrl1 = "Swipe to move  \xC2\xB7  Tap to rotate";
    const char* ctrl2 = "Flick down to drop  \xC2\xB7  Swipe up to hold";
#else
    const char* ctrl1 = "Arrows: Move / Rotate    Space: Hard Drop";
    const char* ctrl2 = "Z: Counter-rotate    C/Shift: Hold";
#endif
    DrawText(ctrl1, (WIN_W - MeasureText(ctrl1, 16)) / 2, 620, 16, LIGHTGRAY);
    DrawText(ctrl2, (WIN_W - MeasureText(ctrl2, 16)) / 2, 645, 16, LIGHTGRAY);
}

static void drawPause() {
    DrawRectangle(0, 0, WIN_W, WIN_H, { 0, 0, 0, 180 });
    const char* p = "PAUSED";
    int pw = MeasureText(p, 64);
    DrawText(p, (WIN_W - pw) / 2, 280, 64, WHITE);

    const char* r = "Press P or ESC to resume";
    DrawText(r, (WIN_W - MeasureText(r, 22)) / 2, 370, 22, LIGHTGRAY);
    const char* q = "Press Q to quit to menu";
    DrawText(q, (WIN_W - MeasureText(q, 20)) / 2, 405, 20, GRAY);
}

static void drawGameOver() {
    DrawRectangle(0, 0, WIN_W, WIN_H, { 0, 0, 0, 190 });
    const char* g = "GAME OVER";
    int gw = MeasureText(g, 64);
    DrawText(g, (WIN_W - gw) / 2 + 3, 220 + 3, 64, { 0, 0, 0, 200 });
    DrawText(g, (WIN_W - gw) / 2, 220, 64, RED);

    const char* sc = TextFormat("Score: %d", score);
    DrawText(sc, (WIN_W - MeasureText(sc, 32)) / 2, 320, 32, YELLOW);

    const char* ln = TextFormat("Lines: %d   Level: %d", lines, level);
    DrawText(ln, (WIN_W - MeasureText(ln, 22)) / 2, 365, 22, LIGHTGRAY);

    if (score == highScore && score > 0) {
        const char* nh = "NEW HIGH SCORE!";
        DrawText(nh, (WIN_W - MeasureText(nh, 24)) / 2, 410, 24, ORANGE);
    }

    bool blink = ((int)(GetTime() * 2) % 2) == 0;
#ifdef __EMSCRIPTEN__
    const char* r = "TAP to play again";
#else
    const char* r = "Press ENTER to play again";
#endif
    if (blink) DrawText(r, (WIN_W - MeasureText(r, 22)) / 2, 480, 22, WHITE);

#ifndef __EMSCRIPTEN__
    const char* q = "Press Q for menu";
    DrawText(q, (WIN_W - MeasureText(q, 20)) / 2, 520, 20, GRAY);
#endif
}

// =================================================================
// Main
// =================================================================
static void frame() {
    float dt = GetFrameTime();

    switch (state) {
        case GameState::Menu:
            if (IsKeyPressed(KEY_ENTER) || tkPressed(TA_START)) {
                resetGame();
                state = GameState::Playing;
            }
            break;
        case GameState::Playing:
            updateGame(dt);
            break;
        case GameState::Paused:
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) ||
                tkPressed(TA_PAUSE) || tkPressed(TA_START))
                state = GameState::Playing;
            if (IsKeyPressed(KEY_Q)) state = GameState::Menu;
            break;
        case GameState::GameOver:
            if (IsKeyPressed(KEY_ENTER) || tkPressed(TA_START)) {
                resetGame();
                state = GameState::Playing;
            }
            if (IsKeyPressed(KEY_Q)) state = GameState::Menu;
            break;
    }

    BeginDrawing();

    Color bg = STAGE_BG[(level - 1) % 10];
    ClearBackground(bg);

    // One Camera2D does double duty: (1) zoom = RENDER_SCALE upscales every
    // logical draw to fill the framebuffer at RENDER_SCALE × resolution,
    // (2) optional shake jitter on a Tetris clear.
    float shakeOffX = 0.0f, shakeOffY = 0.0f;
    if (shakeStart >= 0.0) {
        float t = (float)((GetTime() - shakeStart) / 0.20);
        if (t < 1.0f) {
            float mag = shakeMag * (1.0f - t);
            shakeOffX = ((float)GetRandomValue(-1000, 1000) / 1000.0f) * mag;
            shakeOffY = ((float)GetRandomValue(-1000, 1000) / 1000.0f) * mag;
        }
    }
    Camera2D cam = {};
    cam.target   = { -shakeOffX, -shakeOffY };
    cam.offset   = { 0.0f, 0.0f };
    cam.rotation = 0.0f;
    cam.zoom     = (float)RENDER_SCALE;
    BeginMode2D(cam);

    for (int i = 0; i < WIN_W; i += 60)
        DrawLine(i, 0, i, WIN_H, { 255, 255, 255, 8 });
    for (int i = 0; i < WIN_H; i += 60)
        DrawLine(0, i, WIN_W, i, { 255, 255, 255, 8 });

    if (state == GameState::Menu) {
        drawMenu();
    } else {
        drawBoard();
        drawHud();
        if (state == GameState::Paused)   drawPause();
        if (state == GameState::GameOver) drawGameOver();
    }

    EndMode2D();
    EndDrawing();

    // Clear edge-triggered touch counters after this frame consumed them.
    for (int i = 0; i < TA_COUNT; i++) g_tkPressed[i] = 0;
}

int main() {
    // Render the framebuffer at RENDER_SCALE × logical size. The browser
    // (or native OS) then downscales to fit the viewport — downsampling is
    // always sharper than upsampling, so this kills the canvas blur on
    // high-DPR phone screens without needing to refactor every coord.
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(WIN_W * RENDER_SCALE, WIN_H * RENDER_SCALE, "Tetris");
    SetExitKey(KEY_NULL);     // ESC is used for pause, not quit
    SetTargetFPS(60);
    rng.seed((unsigned)(GetTime() * 1e6) ^ 0xC0FFEEu);

    // Animation sheet — single 7×10 sprite sheet. Row = piece type-1, col 0
    // is the intact candy used during normal gameplay; cols 1..4 play during
    // line-clear (crack → burst). Mipmaps + trilinear give crisp downsampling.
    animSheet = LoadTexture("sprites/cell_sprites.png");
    if (animSheet.id > 0) {
        GenTextureMipmaps(&animSheet);
        SetTextureFilter(animSheet, TEXTURE_FILTER_TRILINEAR);
    }

    resetGame();
    state = GameState::Menu;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!WindowShouldClose()) frame();
    CloseWindow();
#endif
    return 0;
}
