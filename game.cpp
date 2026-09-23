// game.cpp
//
// Menu/UI state machine, input dispatch, WndProc, and the UI render
// pass -- see game.h for what crosses into main.cpp's own loop.

#define NOMINMAX
#include <windows.h>
#include "game.h"
#include "world.h"
#include "render.h"
#include "audio.h"
#include "persist.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// =======================================================================
// Section 4.6 - UI pass support: quad builders on top of render.h's
// font-glyph atlas layout (UIVertex/UI_CELL_W/H/UI_ATLAS_COLS/ROWS/
// UI_WHITE_CELL live there -- InitTextures/InitD3D need them too, this
// file only needs the drawing functions built on top).
// =======================================================================

static void UIAtlasRect(int cell, float& u0, float& v0, float& u1, float& v1) {
    int col = cell % UI_ATLAS_COLS;
    int row = cell / UI_ATLAS_COLS;
    float texW = (float)(UI_ATLAS_COLS * UI_CELL_W);
    float texH = (float)(UI_ATLAS_ROWS * UI_CELL_H);
    float insetU = 0.5f / texW, insetV = 0.5f / texH;
    u0 = (float)(col * UI_CELL_W) / texW + insetU;
    u1 = (float)((col + 1) * UI_CELL_W) / texW - insetU;
    v0 = (float)(row * UI_CELL_H) / texH + insetV;
    v1 = (float)((row + 1) * UI_CELL_H) / texH - insetV;
}

static int UICharCell(char c) {
    if (c < 32 || c > 126) return -1;
    return (int)c - 32;
}

static void UIAddQuad(std::vector<UIVertex>& v, float x0, float y0, float x1, float y1,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a) {
    v.push_back({ x0, y0, u0, v0, r, g, b, a });
    v.push_back({ x1, y0, u1, v0, r, g, b, a });
    v.push_back({ x1, y1, u1, v1, r, g, b, a });
    v.push_back({ x0, y0, u0, v0, r, g, b, a });
    v.push_back({ x1, y1, u1, v1, r, g, b, a });
    v.push_back({ x0, y1, u0, v1, r, g, b, a });
}

// Untextured tinted rectangle -- samples the reserved white cell.
static void UIDrawRect(std::vector<UIVertex>& v, float x0, float y0, float x1, float y1,
                        float r, float g, float b, float a) {
    float u0, v0, u1, v1;
    UIAtlasRect(UI_WHITE_CELL, u0, v0, u1, v1);
    UIAddQuad(v, x0, y0, x1, y1, u0, v0, u1, v1, r, g, b, a);
}

static float UITextWidth(const std::string& text, float scale) {
    return (float)text.size() * UI_CELL_W * scale;
}

// Fixed-advance (monospace-grid) text -- each glyph cell is centered on
// its own character during atlas generation, so a constant per-char
// advance is enough for a "rudimentary" HUD/menu without a real text
// shaping pass.
static void UIDrawText(std::vector<UIVertex>& v, const std::string& text, float x, float y,
                        float scale, float r, float g, float b, float a) {
    float w = UI_CELL_W * scale, h = UI_CELL_H * scale;
    float curX = x;
    for (char c : text) {
        int cell = UICharCell(c);
        if (cell >= 0) {
            float u0, v0, u1, v1;
            UIAtlasRect(cell, u0, v0, u1, v1);
            UIAddQuad(v, curX, y, curX + w, y + h, u0, v0, u1, v1, r, g, b, a);
        }
        curX += w;
    }
}

// =======================================================================
// Menu/game state
// =======================================================================

static int g_currentSlot = 0; // which of the MAX_SAVE_SLOTS files Save/Load/QuickSave/QuickLoad act on this session
bool g_mouseCaptured = false;
static bool g_keyDown[256] = {};
// Starts at the title screen (Section 12) rather than dropping straight
// into gameplay -- the game begins with no world loaded until New Game
// or Load Game picks a slot.
MenuScreen g_menuScreen = MenuScreen::TitleMain;
enum class GameState { Title, InGame };
static GameState g_gameState = GameState::Title;
// Where OptionsHub's BACK row returns to -- Pause if Options was opened
// mid-game, TitleMain if opened from the title screen, since the same
// hub and the same six settings submenus serve both contexts.
static MenuScreen g_optionsReturnScreen = MenuScreen::TitleMain;
enum class SlotPickerMode { New, Load };
static SlotPickerMode g_slotPickerMode = SlotPickerMode::New;
// New Game on an already-occupied slot needs a confirmation rather than
// silently overwriting -- a second click within a few seconds confirms;
// otherwise the arm times out and a third click starts over.
int g_confirmOverwriteSlot = -1;
float g_confirmOverwriteTimer = 0.0f;
static int g_mouseX = 0, g_mouseY = 0;
static std::string g_toastMessage;
float g_toastTimer = 0.0f; // seconds remaining; drawn by RenderUIPass
float g_fpsTimer = 0.0f;
int g_fpsFrameCount = 0, g_fpsDisplay = 0; // updated once/sec, shown when Display Settings' FPS counter is on

static void PickAndAct(bool breakBlock) {
    Vec3 f, r, u;
    GetCameraVectors(g_player, f, r, u);
    float dx = f.x, dy = f.y, dz = f.z;
    float ex = g_player.x, ey = g_player.y + PLAYER_EYE, ez = g_player.z;

    int hx, hy, hz, px, py, pz;
    if (!Raycast(g_world, ex, ey, ez, dx, dy, dz, 6.0f, hx, hy, hz, px, py, pz)) return;

    if (breakBlock) {
        LiveEdit(g_world, hx, hy, hz, BLOCK_AIR);
    } else {
        BlockID toPlace = g_placeable[g_player.hotbarIndex];
        LiveEdit(g_world, px, py, pz, toPlace);
    }
}

// Captures and hides the cursor, re-centering it, to enter FPS look mode.
static void CaptureMouseForPlay() {
    g_mouseCaptured = true;
    ShowCursor(FALSE);
    SetCapture(g_hwnd);
    RECT rc; GetClientRect(g_hwnd, &rc);
    POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
    ClientToScreen(g_hwnd, &center);
    SetCursorPos(center.x, center.y);
}

// Releases the cursor so it can move freely over menu buttons.
static void ReleaseMouseForMenu() {
    g_mouseCaptured = false;
    ShowCursor(TRUE);
    ReleaseCapture();
}

// Generic submenu layout: every menu screen (Pause and each settings
// submenu) is a titled panel of stacked full-width rows plus, for a few
// rows, an inline slider. One shared layout system parameterized by row
// count/height means Keybindings' 12 compact rows and Look Settings'
// taller slider rows don't need duplicated panel/row-rect math.
struct UIRect { float x0, y0, x1, y1; };
struct SubmenuLayout { float panelW, rowH, rowGap, topMargin, bottomMargin; int rowCount; };

static UIRect SubmenuPanelRect(const SubmenuLayout& L) {
    float h = L.topMargin + L.rowCount * (L.rowH + L.rowGap) - L.rowGap + L.bottomMargin;
    float px = (SCREEN_W - L.panelW) / 2.0f, py = (SCREEN_H - h) / 2.0f;
    return { px, py, px + L.panelW, py + h };
}
static UIRect SubmenuRowRect(const SubmenuLayout& L, int rowIndex) {
    UIRect panel = SubmenuPanelRect(L);
    float bw = L.panelW - 60.0f;
    float bx = panel.x0 + 30.0f;
    float by = panel.y0 + L.topMargin + rowIndex * (L.rowH + L.rowGap);
    return { bx, by, bx + bw, by + L.rowH };
}
static bool PointInRect(int px, int py, const UIRect& r) {
    return px >= r.x0 && px <= r.x1 && py >= r.y0 && py <= r.y1;
}

// Pause itself only handles resuming, save/load, and quitting -- every
// settings category now lives one level down in OptionsHub (Section
// 13), shared with the title screen's own Options button, rather than
// listing all six categories directly in both places.
static const SubmenuLayout PAUSE_LAYOUT    = { 320.0f, 40.0f, 12.0f, 70.0f, 20.0f, 6 };
enum PauseRow { PROW_RESUME = 0, PROW_OPTIONS = 1, PROW_SAVE = 2, PROW_LOAD = 3, PROW_QUIT_TO_TITLE = 4, PROW_QUIT = 5 };

// The options hub: one settings-category picker shared by Pause (mid-
// game) and the title screen (pre-game) alike, since every submenu
// underneath it is pure global-preference state with no dependency on
// a loaded world.
static const SubmenuLayout OPTIONS_HUB_LAYOUT = { 340.0f, 40.0f, 12.0f, 70.0f, 20.0f, 7 };
enum OptionsHubRow { OHROW_LOOK = 0, OHROW_GRAPHICS = 1, OHROW_DISPLAY = 2, OHROW_AUDIO = 3, OHROW_ACCESSIBILITY = 4, OHROW_KEYBINDS = 5, OHROW_BACK = 6 };

// The title screen (Section 12): shown at startup instead of dropping
// straight into gameplay, and again after "Quit to Title" from Pause.
static const SubmenuLayout TITLE_LAYOUT = { 320.0f, 44.0f, 14.0f, 90.0f, 20.0f, 4 };
enum TitleRow { TROW_NEW_GAME = 0, TROW_LOAD_GAME = 1, TROW_OPTIONS = 2, TROW_QUIT = 3 };

// One row per save slot plus BACK. Rows beyond MAX_SAVE_SLOTS-1 are
// BACK; see SLOTROW_BACK below rather than a fixed enum, since the slot
// count is a constant, not a fixed small set of named rows.
static const SubmenuLayout SLOT_PICKER_LAYOUT = { 420.0f, 48.0f, 10.0f, 90.0f, 20.0f, MAX_SAVE_SLOTS + 1 };
static const int SLOTROW_BACK = MAX_SAVE_SLOTS;

// Look Settings: separate X/Y sensitivity sliders and separate X/Y
// inversion, per the request -- a single combined sensitivity value
// didn't let the two axes be tuned independently.
static const SubmenuLayout LOOK_LAYOUT     = { 400.0f, 56.0f, 12.0f, 70.0f, 20.0f, 6 };
enum LookRow { LROW_INVERT_X = 0, LROW_SENS_X = 1, LROW_INVERT_Y = 2, LROW_SENS_Y = 3, LROW_RESET = 4, LROW_BACK = 5 };

// Graphics: one real setting -- render distance -- rather than stubbing
// out controls (fog distance, shadow quality, etc.) this prototype has
// no rendering path for yet.
static const SubmenuLayout GRAPHICS_LAYOUT = { 380.0f, 56.0f, 12.0f, 70.0f, 20.0f, 3 };
enum GraphicsRow { GROW_RENDER_DIST = 0, GROW_RESET = 1, GROW_BACK = 2 };

// Display: one real setting -- an FPS counter toggle. Resolution/
// fullscreen switching would need swap-chain resize and WM_SIZE
// handling this prototype doesn't have yet, so it isn't faked here.
static const SubmenuLayout DISPLAY_LAYOUT  = { 340.0f, 40.0f, 12.0f, 70.0f, 20.0f, 3 };
enum DisplayRow { DROW_SHOW_FPS = 0, DROW_RESET = 1, DROW_BACK = 2 };

// Audio: Master and Music sliders, backed by a real XAudio2 voice
// (Section 10) playing the procedural ambient track. Separate channels
// now even though Music is the only one with anything to play yet, so a
// future SFX channel is one more slider, not a remix of this one.
static const SubmenuLayout AUDIO_LAYOUT    = { 380.0f, 56.0f, 12.0f, 70.0f, 20.0f, 4 };
enum AudioRow { AROW_MASTER_VOLUME = 0, AROW_MUSIC_VOLUME = 1, AROW_RESET = 2, AROW_BACK = 3 };

// Accessibility: a real, working slice rather than every idea discussed
// -- a field-of-view slider (motion/vestibular comfort: neither wider
// nor narrower is universally more comfortable, so this is a slider a
// player tunes either direction, not a binary toggle), a toggle-to-move
// mode for WASD (motor accessibility: movement no longer requires
// holding a key down for the whole duration), and a high-contrast UI
// palette (low-vision legibility). Deliberately NOT here yet: a "reduce
// flashing" toggle, since nothing in this prototype flashes or strobes
// today -- the actual commitment (Section 11) is that no future effect
// introduces uncontrolled flashing/strobing at all, which a toggle
// controlling zero real effects wouldn't strengthen; a colorblind-safe
// palette, since nothing in the current UI conveys meaning through hue
// alone yet (nothing to remap); and a UI scale slider, which (unlike
// the above) is real future work, just architecturally bigger -- every
// hit-rect, not only the visuals, would need to move in lockstep.
static const SubmenuLayout ACCESSIBILITY_LAYOUT = { 400.0f, 56.0f, 12.0f, 70.0f, 20.0f, 6 };
enum AccessibilityRow { ARROW_FOV = 0, ARROW_TOGGLE_MOVE = 1, ARROW_HIGH_CONTRAST = 2, ARROW_MUSIC_INTENSITY = 3, ARROW_RESET = 4, ARROW_BACK = 5 };

// Keybindings: every action bindable to any keyboard key or the left/
// right/middle mouse button (GameAction/g_actionNames/g_keyBindings/
// MOUSE_LEFT etc. are declared in persist.h since Part VII's save/load
// code needs them too). Scope decisions worth being explicit about: no
// gamepad support exists in this prototype to bind to; mouse wheel and
// side (X1/X2) buttons aren't bindable inputs yet; the 9 hotbar-select
// keys stay fixed rather than adding 9 more rows; and rebinding does
// not warn about or prevent two actions sharing the same input.
static const char* g_actionLabels[ACT_COUNT] = { // on-screen text
    "MOVE FORWARD", "MOVE BACK", "MOVE LEFT", "MOVE RIGHT", "JUMP",
    "BREAK BLOCK", "PLACE BLOCK", "PAUSE MENU", "QUICK SAVE", "QUICK LOAD"
};
static const SubmenuLayout KEYBIND_LAYOUT = { 480.0f, 32.0f, 8.0f, 92.0f, 20.0f, ACT_COUNT + 2 }; // +reset +back

static const int g_defaultBindings[ACT_COUNT] = {
    'W', 'S', 'A', 'D', VK_SPACE, MOUSE_LEFT, MOUSE_RIGHT, VK_ESCAPE, VK_F5, VK_F9
};
static int g_rebindingAction = -1; // -1 = not capturing; else a GameAction index

static bool g_mouseButtonDown[3] = {}; // 0=left,1=right,2=middle
static int MouseButtonIndex(int code) {
    if (code == MOUSE_LEFT) return 0;
    if (code == MOUSE_RIGHT) return 1;
    if (code == MOUSE_MIDDLE) return 2;
    return -1;
}
static bool IsInputDown(int code) {
    int mi = MouseButtonIndex(code);
    if (mi >= 0) return g_mouseButtonDown[mi];
    if (code >= 0 && code < 256) return g_keyDown[code];
    return false;
}
// In toggle-move mode (Accessibility, Section 11) the four movement
// actions report a latched state that a key PRESS flips, rather than
// whether the key is currently physically held -- the whole point being
// that a player no longer needs to hold it down for the entire duration
// of movement. Every other action (jump, break, place, menu, ...) is
// unaffected and keeps the ordinary held-state behavior.
static bool IsMovementAction(GameAction a) {
    return a == ACT_FORWARD || a == ACT_BACK || a == ACT_LEFT || a == ACT_RIGHT;
}
bool IsActionDown(GameAction a) {
    if (g_toggleMovement && IsMovementAction(a)) return g_moveToggleLatch[a];
    return IsInputDown(g_keyBindings[a]);
}

// Human-readable name for a bound input code, for the Keybindings rows.
static std::string GetInputDisplayName(int code) {
    if (code == MOUSE_LEFT) return "MOUSE LEFT";
    if (code == MOUSE_RIGHT) return "MOUSE RIGHT";
    if (code == MOUSE_MIDDLE) return "MOUSE MIDDLE";
    UINT scan = MapVirtualKeyW((UINT)code, MAPVK_VK_TO_VSC);
    LONG fakeLParam = (LONG)(scan << 16);
    wchar_t buf[64] = {};
    int len = GetKeyNameTextW(fakeLParam, buf, 64);
    if (len <= 0) return "?";
    std::string s;
    for (int i = 0; i < len; i++) s.push_back((char)buf[i]); // default bindings are all plain-ASCII key names
    return s;
}
static void ResetKeybindingsToDefault() {
    for (int i = 0; i < ACT_COUNT; i++) g_keyBindings[i] = g_defaultBindings[i];
}

// Look/Graphics/Display/Audio preference VALUES (g_sensitivityMultX/Y,
// g_invertX/Y, g_showFPS, g_masterVolume) live in persist.h for the same
// reason as the keybinding data above -- Section VII needs them.
static const float SENS_MIN = 0.25f, SENS_MAX = 3.0f;

static void ResetLookSettings() { g_sensitivityMultX = 1.0f; g_sensitivityMultY = 1.0f; g_invertX = false; g_invertY = false; }
static void ResetGraphicsSettings() {
    g_loadRadius = 3;
    g_lastPlayerChunkX = INT32_MIN; g_lastPlayerChunkZ = INT32_MIN; // force a rescan at the new radius
}
static void ResetDisplaySettings() { g_showFPS = false; }
static void ResetAudioSettings() { g_masterVolume = 1.0f; g_musicVolume = 1.0f; ApplyAudioVolumes(); }
static void ResetAccessibilitySettings() {
    g_fov = 45.0f;
    g_toggleMovement = false;
    g_highContrastUI = false;
    g_musicIntensity = 1.0f;
    memset(g_moveToggleLatch, 0, sizeof(g_moveToggleLatch));
}

// A handful of settings are sliders rather than toggles/buttons. One
// small generic slider system (value/range/row-rect all looked up by
// ID) instead of one-off X-sensitivity-shaped code repeated per slider.
enum SliderId { SLIDER_NONE = -1, SLIDER_SENS_X = 0, SLIDER_SENS_Y = 1, SLIDER_RENDER_DIST = 2, SLIDER_MASTER_VOLUME = 3, SLIDER_MUSIC_VOLUME = 4, SLIDER_FOV = 5, SLIDER_MUSIC_INTENSITY = 6 };
static int g_draggingSlider = SLIDER_NONE;

struct SliderRange { float minV, maxV; };
static SliderRange GetSliderRange(int id) {
    switch (id) {
    case SLIDER_SENS_X: case SLIDER_SENS_Y: return { SENS_MIN, SENS_MAX };
    case SLIDER_RENDER_DIST: return { 1.0f, 8.0f };
    case SLIDER_MASTER_VOLUME: case SLIDER_MUSIC_VOLUME: return { 0.0f, 1.0f };
    case SLIDER_FOV: return { 45.0f, 100.0f };
    case SLIDER_MUSIC_INTENSITY: return { 0.0f, 1.0f };
    default: return { 0.0f, 1.0f };
    }
}
// Only meaningful while the slider's own submenu is the active screen
// -- the only time it can be dragged or needs drawing.
static UIRect GetSliderRowRect(int id) {
    switch (id) {
    case SLIDER_SENS_X: return SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_X);
    case SLIDER_SENS_Y: return SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_Y);
    case SLIDER_RENDER_DIST: return SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RENDER_DIST);
    case SLIDER_MASTER_VOLUME: return SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME);
    case SLIDER_MUSIC_VOLUME: return SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME);
    case SLIDER_FOV: return SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_FOV);
    case SLIDER_MUSIC_INTENSITY: return SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MUSIC_INTENSITY);
    default: return { 0, 0, 0, 0 };
    }
}
static float GetSliderValue(int id) {
    switch (id) {
    case SLIDER_SENS_X: return g_sensitivityMultX;
    case SLIDER_SENS_Y: return g_sensitivityMultY;
    case SLIDER_RENDER_DIST: return (float)g_loadRadius;
    case SLIDER_MASTER_VOLUME: return g_masterVolume;
    case SLIDER_MUSIC_VOLUME: return g_musicVolume;
    case SLIDER_FOV: return g_fov;
    case SLIDER_MUSIC_INTENSITY: return g_musicIntensity;
    default: return 0.0f;
    }
}
static void SetSliderValue(int id, float v) {
    switch (id) {
    case SLIDER_SENS_X: g_sensitivityMultX = v; break;
    case SLIDER_SENS_Y: g_sensitivityMultY = v; break;
    case SLIDER_RENDER_DIST: {
        int newRadius = (int)(v + 0.5f);
        if (newRadius != g_loadRadius) {
            g_loadRadius = newRadius;
            g_lastPlayerChunkX = INT32_MIN; g_lastPlayerChunkZ = INT32_MIN;
        }
        break;
    }
    case SLIDER_MASTER_VOLUME: g_masterVolume = v; ApplyAudioVolumes(); break;
    case SLIDER_MUSIC_VOLUME: g_musicVolume = v; ApplyAudioVolumes(); break;
    case SLIDER_FOV: g_fov = v; break;
    case SLIDER_MUSIC_INTENSITY: g_musicIntensity = v; break;
    }
}
static std::string GetSliderLabel(int id) {
    char buf[64];
    switch (id) {
    case SLIDER_SENS_X: snprintf(buf, sizeof(buf), "X SENSITIVITY: %.2fx", g_sensitivityMultX); break;
    case SLIDER_SENS_Y: snprintf(buf, sizeof(buf), "Y SENSITIVITY: %.2fx", g_sensitivityMultY); break;
    case SLIDER_RENDER_DIST: snprintf(buf, sizeof(buf), "RENDER DISTANCE: %d CHUNKS", g_loadRadius); break;
    case SLIDER_MASTER_VOLUME: snprintf(buf, sizeof(buf), "MASTER VOLUME: %d%%", (int)(g_masterVolume * 100.0f + 0.5f)); break;
    case SLIDER_MUSIC_VOLUME: snprintf(buf, sizeof(buf), "MUSIC VOLUME: %d%%", (int)(g_musicVolume * 100.0f + 0.5f)); break;
    case SLIDER_FOV: snprintf(buf, sizeof(buf), "FIELD OF VIEW: %d DEG", (int)(g_fov + 0.5f)); break;
    case SLIDER_MUSIC_INTENSITY: snprintf(buf, sizeof(buf), "MUSIC INTENSITY: %d%%", (int)(g_musicIntensity * 100.0f + 0.5f)); break;
    default: buf[0] = 0;
    }
    return buf;
}
// The slider track sits in the lower half of its row, with the label
// above it. The hit rect is a bit taller than the visible track so it's
// not fiddly to grab.
static UIRect GetSliderTrackRect(UIRect r) { return { r.x0 + 8, r.y0 + 34, r.x1 - 8, r.y0 + 42 }; }
static UIRect GetSliderHitRect(UIRect r) { return { r.x0 + 8, r.y0 + 24, r.x1 - 8, r.y0 + 50 }; }

// Sets a slider's value directly from a mouse x position along its
// track -- shared by the initial click and every subsequent drag
// update while the button stays held.
static void ApplySliderDrag(int mx) {
    if (g_draggingSlider == SLIDER_NONE) return;
    UIRect track = GetSliderTrackRect(GetSliderRowRect(g_draggingSlider));
    float t = (mx - track.x0) / (track.x1 - track.x0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    SliderRange rng = GetSliderRange(g_draggingSlider);
    SetSliderValue(g_draggingSlider, rng.minV + t * (rng.maxV - rng.minV));
}
static void BeginSliderDrag(int id, int mx) {
    g_draggingSlider = id;
    SetCapture(g_hwnd); // keep receiving WM_MOUSEMOVE if the drag leaves the client area
    ApplySliderDrag(mx);
}

// Wrap Save/Load so every call site (F5/F9-equivalent bound inputs and
// the pause-menu buttons) gets the same on-screen confirmation instead
// of failing or succeeding silently.
static void DoSave() {
    bool ok = SaveGame(g_world, g_player, g_currentSlot);
    g_toastMessage = ok ? "GAME SAVED" : "SAVE FAILED";
    g_toastTimer = 2.0f;
}
static void DoLoad() {
    bool ok = LoadGame(g_world, g_player, g_currentSlot);
    g_toastMessage = ok ? "GAME LOADED" : "LOAD FAILED (no save?)";
    g_toastTimer = 2.0f;
}

static void HandleMenuClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_RESUME))) { g_menuScreen = MenuScreen::None; CaptureMouseForPlay(); StartMusicPlayback(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_OPTIONS))) { g_optionsReturnScreen = MenuScreen::Pause; g_menuScreen = MenuScreen::OptionsHub; return; }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_SAVE))) { DoSave(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_LOAD))) {
        DoLoad(); // may change g_dayTimeSeconds -- StartMusicPlayback re-anchors to whatever it loaded
        g_menuScreen = MenuScreen::None;
        CaptureMouseForPlay();
        StartMusicPlayback();
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT_TO_TITLE))) {
        g_gameState = GameState::Title;
        g_menuScreen = MenuScreen::TitleMain;
        StopMusicPlayback(); // already stopped (Pause is only reachable with music already stopped), but explicit/idempotent
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT))) { PostQuitMessage(0); return; }
}

static void HandleOptionsHubClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_LOOK))) { g_menuScreen = MenuScreen::LookSettings; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_GRAPHICS))) { g_menuScreen = MenuScreen::Graphics; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_DISPLAY))) { g_menuScreen = MenuScreen::Display; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_AUDIO))) { g_menuScreen = MenuScreen::Audio; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_ACCESSIBILITY))) { g_menuScreen = MenuScreen::Accessibility; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_KEYBINDS))) { g_menuScreen = MenuScreen::Keybindings; return; }
    if (PointInRect(mx, my, SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_BACK))) { g_menuScreen = g_optionsReturnScreen; return; }
}

static void HandleLookSettingsClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_X))) { g_invertX = !g_invertX; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_Y))) { g_invertY = !g_invertY; SaveSettings(); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_X)))) { BeginSliderDrag(SLIDER_SENS_X, mx); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_Y)))) { BeginSliderDrag(SLIDER_SENS_Y, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_RESET))) { ResetLookSettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(LOOK_LAYOUT, LROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleGraphicsClick(int mx, int my) {
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RENDER_DIST)))) { BeginSliderDrag(SLIDER_RENDER_DIST, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RESET))) { ResetGraphicsSettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleDisplayClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_FPS))) { g_showFPS = !g_showFPS; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_RESET))) { ResetDisplaySettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleAudioClick(int mx, int my) {
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME)))) { BeginSliderDrag(SLIDER_MASTER_VOLUME, mx); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME)))) { BeginSliderDrag(SLIDER_MUSIC_VOLUME, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(AUDIO_LAYOUT, AROW_RESET))) { ResetAudioSettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(AUDIO_LAYOUT, AROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleAccessibilityClick(int mx, int my) {
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_FOV)))) { BeginSliderDrag(SLIDER_FOV, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_TOGGLE_MOVE))) {
        g_toggleMovement = !g_toggleMovement;
        memset(g_moveToggleLatch, 0, sizeof(g_moveToggleLatch)); // switching modes shouldn't leave a stale latch active
        SaveSettings();
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_HIGH_CONTRAST))) { g_highContrastUI = !g_highContrastUI; SaveSettings(); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MUSIC_INTENSITY)))) { BeginSliderDrag(SLIDER_MUSIC_INTENSITY, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_RESET))) { ResetAccessibilitySettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleKeybindingsClick(int mx, int my) {
    for (int i = 0; i < ACT_COUNT; i++) {
        if (PointInRect(mx, my, SubmenuRowRect(KEYBIND_LAYOUT, i))) { g_rebindingAction = i; return; }
    }
    if (PointInRect(mx, my, SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT))) { ResetKeybindingsToDefault(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT + 1))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}

// Resets world/player/chunk-generation bookkeeping to a brand-new game
// -- the same bookkeeping reset LoadGame performs after a load (Section
// 7.4), just starting from nothing instead of loaded data. The normal
// per-tick EnsureChunksLoaded/ProcessColumnGeneration path (Section 2.4)
// then lazily generates terrain around the spawn point exactly as it
// always has, once ticking resumes.
static void ResetWorldForNewGame() {
    g_world = World();
    g_player = Player();
    g_dayTimeSeconds = 0.0f; // dawn -- first light in a land they've never seen (Section 13)
    g_generatedColumns.clear();
    g_pendingColumns.clear();
    g_pendingColumnSet.clear();
    g_lastPlayerChunkX = INT32_MIN;
    g_lastPlayerChunkZ = INT32_MIN;
}

// Shared tail end of both New Game and Load Game: leave the slot
// picker, mark a real game as running, and hand control to the player.
static void EnterGameplay() {
    g_gameState = GameState::InGame;
    g_menuScreen = MenuScreen::None;
    CaptureMouseForPlay();
    StartMusicPlayback();
}

static void HandleTitleClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_NEW_GAME))) {
        g_slotPickerMode = SlotPickerMode::New;
        g_confirmOverwriteSlot = -1;
        g_menuScreen = MenuScreen::SlotPicker;
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_LOAD_GAME))) {
        g_slotPickerMode = SlotPickerMode::Load;
        g_confirmOverwriteSlot = -1;
        g_menuScreen = MenuScreen::SlotPicker;
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_OPTIONS))) { g_optionsReturnScreen = MenuScreen::TitleMain; g_menuScreen = MenuScreen::OptionsHub; return; }
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_QUIT))) { PostQuitMessage(0); return; }
}

static void HandleSlotPickerClick(int mx, int my) {
    for (int slot = 0; slot < MAX_SAVE_SLOTS; slot++) {
        if (!PointInRect(mx, my, SubmenuRowRect(SLOT_PICKER_LAYOUT, slot))) continue;

        if (g_slotPickerMode == SlotPickerMode::Load) {
            if (!SlotExists(slot)) { g_toastMessage = "EMPTY SLOT"; g_toastTimer = 1.5f; return; }
            g_currentSlot = slot;
            if (!LoadGame(g_world, g_player, slot)) {
                g_toastMessage = "LOAD FAILED (corrupt save?)";
                g_toastTimer = 2.0f;
                return;
            }
            EnterGameplay();
            return;
        }

        // New Game: an empty slot starts immediately; an occupied one
        // needs a second click within a few seconds to confirm the
        // overwrite, rather than silently destroying an existing world.
        if (SlotExists(slot) && g_confirmOverwriteSlot != slot) {
            g_confirmOverwriteSlot = slot;
            g_confirmOverwriteTimer = 4.0f;
            return;
        }
        g_currentSlot = slot;
        ResetWorldForNewGame();
        SaveGame(g_world, g_player, slot); // write immediately so the slot is no longer "empty" from this point on
        EnterGameplay();
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(SLOT_PICKER_LAYOUT, SLOTROW_BACK))) { g_menuScreen = MenuScreen::TitleMain; return; }
}

// Centralizes what a just-pressed input does, whatever its source (a
// VK_* from WM_KEYDOWN, or a MOUSE_* sentinel from a mouse-button-down
// message) -- the single place Menu/Save/Load/Break/Place dispatch is
// gated, so every input source is guaranteed to agree on the rules
// instead of each caller re-deriving them.
static bool IsSettingsSubmenu(MenuScreen s) {
    return s == MenuScreen::LookSettings || s == MenuScreen::Graphics || s == MenuScreen::Display
        || s == MenuScreen::Audio || s == MenuScreen::Accessibility || s == MenuScreen::Keybindings;
}
static void FireBoundAction(int code) {
    if (code == g_keyBindings[ACT_MENU]) {
        if (g_gameState == GameState::Title) {
            // ESC only ever backs out one level while at the title
            // screen -- there's no "resume gameplay" state to return to,
            // and TitleMain itself is the top of this tree.
            if (g_menuScreen == MenuScreen::SlotPicker) g_menuScreen = MenuScreen::TitleMain;
            else if (g_menuScreen == MenuScreen::OptionsHub) g_menuScreen = g_optionsReturnScreen;
            else if (IsSettingsSubmenu(g_menuScreen)) g_menuScreen = MenuScreen::OptionsHub;
            return;
        }
        if (g_menuScreen == MenuScreen::None) {
            g_menuScreen = MenuScreen::Pause;
            ReleaseMouseForMenu();
            StopMusicPlayback();
        } else if (g_menuScreen == MenuScreen::Pause) {
            g_menuScreen = MenuScreen::None;
            CaptureMouseForPlay();
            StartMusicPlayback();
        } else if (g_menuScreen == MenuScreen::OptionsHub) {
            g_menuScreen = g_optionsReturnScreen;
        } else if (IsSettingsSubmenu(g_menuScreen)) {
            g_menuScreen = MenuScreen::OptionsHub;
        } else {
            g_menuScreen = MenuScreen::Pause;
        }
        return;
    }
    if (g_gameState == GameState::Title) return; // Save/Load/Break/Place all require an actual game running
    if (code == g_keyBindings[ACT_SAVE]) { DoSave(); return; }
    if (code == g_keyBindings[ACT_LOAD]) { DoLoad(); StartMusicPlayback(); return; } // DoLoad may change g_dayTimeSeconds -- re-anchor, same as the pause-menu Load button
    if (g_menuScreen != MenuScreen::None) return; // Break/Place only fire during actual play
    if (!g_mouseCaptured) return;
    if (code == g_keyBindings[ACT_BREAK]) { PickAndAct(true); return; }
    if (code == g_keyBindings[ACT_PLACE]) { PickAndAct(false); return; }
}

// Dispatches a click to whichever submenu is currently open. Only
// called for the left button -- menus never respond to right/middle
// click, matching ordinary UI convention.
static void DispatchMenuClick(int mx, int my) {
    switch (g_menuScreen) {
    case MenuScreen::Pause: HandleMenuClick(mx, my); break;
    case MenuScreen::OptionsHub: HandleOptionsHubClick(mx, my); break;
    case MenuScreen::LookSettings: HandleLookSettingsClick(mx, my); break;
    case MenuScreen::Graphics: HandleGraphicsClick(mx, my); break;
    case MenuScreen::Display: HandleDisplayClick(mx, my); break;
    case MenuScreen::Audio: HandleAudioClick(mx, my); break;
    case MenuScreen::Accessibility: HandleAccessibilityClick(mx, my); break;
    case MenuScreen::Keybindings: HandleKeybindingsClick(mx, my); break;
    case MenuScreen::TitleMain: HandleTitleClick(mx, my); break;
    case MenuScreen::SlotPicker: HandleSlotPickerClick(mx, my); break;
    default: break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEMOVE:
        g_mouseX = (int)(short)LOWORD(lParam);
        g_mouseY = (int)(short)HIWORD(lParam);
        ApplySliderDrag(g_mouseX); // no-op unless a slider is actively held
        return 0;
    case WM_LBUTTONDOWN: {
        g_mouseButtonDown[0] = true;
        int mx = (int)(short)LOWORD(lParam), my = (int)(short)HIWORD(lParam);
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_LEFT; g_rebindingAction = -1; SaveSettings(); return 0; }
        if (g_menuScreen != MenuScreen::None) { DispatchMenuClick(mx, my); return 0; }
        if (!g_mouseCaptured) { CaptureMouseForPlay(); return 0; }
        FireBoundAction(MOUSE_LEFT);
        return 0;
    }
    case WM_LBUTTONUP:
        g_mouseButtonDown[0] = false;
        // Only release capture if a slider drag actually set it --
        // unconditionally releasing here would also kick the player out
        // of FPS mouse-look capture (CaptureMouseForPlay's SetCapture)
        // on every ordinary left-click-to-break-block during gameplay.
        if (g_draggingSlider != SLIDER_NONE) {
            g_draggingSlider = SLIDER_NONE;
            ReleaseCapture();
            SaveSettings(); // once per completed drag, not per pixel of motion
        }
        return 0;
    case WM_RBUTTONDOWN:
        g_mouseButtonDown[1] = true;
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_RIGHT; g_rebindingAction = -1; SaveSettings(); return 0; }
        FireBoundAction(MOUSE_RIGHT);
        return 0;
    case WM_RBUTTONUP:
        g_mouseButtonDown[1] = false;
        return 0;
    case WM_MBUTTONDOWN:
        g_mouseButtonDown[2] = true;
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_MIDDLE; g_rebindingAction = -1; SaveSettings(); return 0; }
        FireBoundAction(MOUSE_MIDDLE);
        return 0;
    case WM_MBUTTONUP:
        g_mouseButtonDown[2] = false;
        return 0;
    case WM_KEYDOWN:
        if (wParam < 256) g_keyDown[wParam] = true;
        if (g_rebindingAction != -1) {
            // Escape is reserved as the universal "cancel this rebind"
            // input rather than something bindable mid-capture -- every
            // other key or mouse button commits as the new binding,
            // wherever it's pressed (including, e.g., on the Back row).
            if (wParam != VK_ESCAPE) { g_keyBindings[g_rebindingAction] = (int)wParam; SaveSettings(); }
            g_rebindingAction = -1;
            return 0;
        }
        if (wParam >= '1' && wParam <= '9' && g_menuScreen == MenuScreen::None) {
            int idx = (int)(wParam - '1');
            if (idx < g_placeableCount) g_player.hotbarIndex = idx;
            return 0;
        }
        // Toggle-to-move (Accessibility, Section 11): flip the latch on a
        // genuine press only -- bit 30 of lParam is set when this
        // WM_KEYDOWN is Windows' own key-repeat rather than a fresh
        // press, and without excluding it, holding the key would rapidly
        // flip the latch back and forth instead of toggling once.
        if (g_toggleMovement && g_menuScreen == MenuScreen::None && !(lParam & (1 << 30))) {
            for (GameAction a : { ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT }) {
                if ((int)wParam == g_keyBindings[a]) g_moveToggleLatch[a] = !g_moveToggleLatch[a];
            }
        }
        FireBoundAction((int)wParam);
        return 0;
    case WM_KEYUP:
        if (wParam < 256) g_keyDown[wParam] = false;
        return 0;
    case WM_KILLFOCUS:
        // Losing focus while a key is held would otherwise leave it
        // stuck "down" forever -- this window won't get the matching
        // WM_KEYUP if focus moved elsewhere. And losing focus while the
        // mouse is captured would otherwise keep yanking the real
        // cursor back to center every frame even while alt-tabbed away,
        // since the look-code's recenter loop only checked
        // g_mouseCaptured, not whether this window was still focused.
        // Auto-pausing (like most FPS games do on focus loss) fixes
        // both at once: it releases the cursor immediately, and the
        // key-state reset below prevents any stuck movement.
        memset(g_keyDown, 0, sizeof(g_keyDown));
        memset(g_mouseButtonDown, 0, sizeof(g_mouseButtonDown));
        memset(g_moveToggleLatch, 0, sizeof(g_moveToggleLatch)); // don't resume walking on refocus from a stale toggle
        g_rebindingAction = -1;
        // Mouse capture isn't guaranteed to be released automatically
        // just because keyboard focus was -- release whatever a slider
        // drag or FPS-look capture left behind explicitly (harmless
        // no-op if nothing was actually captured).
        g_draggingSlider = SLIDER_NONE;
        ReleaseCapture();
        if (g_mouseCaptured) {
            g_menuScreen = MenuScreen::Pause;
            ReleaseMouseForMenu();
            StopMusicPlayback();
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Draws one small dynamic-VB batch through the UI pipeline. Called
// several times per frame (once per bound texture) since the UI pass
// mixes the font/white atlas with the block atlases for hotbar icons.
static void UIDrawBatch(const std::vector<UIVertex>& verts, ID3D11ShaderResourceView* srv) {
    if (verts.empty()) return;
    D3D11_MAPPED_SUBRESOURCE mapped;
    g_context->Map(g_uiVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    size_t count = std::min<size_t>(verts.size(), UI_VB_CAPACITY);
    memcpy(mapped.pData, verts.data(), count * sizeof(UIVertex));
    g_context->Unmap(g_uiVB, 0);

    UINT stride = sizeof(UIVertex), offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_uiVB, &stride, &offset);
    g_context->PSSetShaderResources(0, 1, &srv);
    g_context->Draw((UINT)count, 0);
}

// Second pass: orthographic-in-pixel-space, depth off, alpha blend on
// (Section 4.6). Builds the crosshair, hotbar, "click to play" hint and
// pause menu as CPU-side quad lists, then draws them through the UI
// pipeline set up in InitD3D. Assumes the world pass already ran this
// frame (so g_context's shader/IA state gets fully re-set here rather
// than assumed).
void RenderUIPass() {
    std::vector<UIVertex> glyphVerts; // font atlas + white cell (panels, borders, text, crosshair)

    bool menuIsOpen = g_menuScreen != MenuScreen::None;

    if (g_mouseCaptured && !menuIsOpen) {
        float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
        UIDrawRect(glyphVerts, cx - 8, cy - 1, cx + 8, cy + 1, 1, 1, 1, 0.85f);
        UIDrawRect(glyphVerts, cx - 1, cy - 8, cx + 1, cy + 8, 1, 1, 1, 0.85f);
    }

    struct IconQuad { float x0, y0, x1, y1, u0, v0, u1, v1; bool pipe; };
    std::vector<IconQuad> icons;

    const int SLOT = 48, GAP = 4;
    int hotbarN = g_placeableCount;
    int totalW = hotbarN * SLOT + (hotbarN - 1) * GAP;
    float hbStartX = (SCREEN_W - totalW) / 2.0f;
    float hbY0 = SCREEN_H - SLOT - 16.0f;
    for (int i = 0; i < hotbarN; i++) {
        float x0 = hbStartX + i * (SLOT + GAP), x1 = x0 + SLOT;
        float y0 = hbY0, y1 = y0 + SLOT;
        bool selected = (i == g_player.hotbarIndex);
        if (selected) UIDrawRect(glyphVerts, x0 - 4, y0 - 4, x1 + 4, y1 + 4, 1.0f, 0.9f, 0.2f, 0.9f);
        UIDrawRect(glyphVerts, x0, y0, x1, y1, 0.12f, 0.12f, 0.12f, 0.75f);

        BlockID b = g_placeable[i];
        float iu0, iv0, iu1, iv1;
        bool pipe = g_info[b].shape != 0;
        if (pipe) { iu0 = 0.05f; iv0 = 0.05f; iu1 = 0.95f; iv1 = 0.95f; }
        else AtlasRect(g_info[b].tex, iu0, iv0, iu1, iv1);
        icons.push_back({ x0 + 6, y0 + 6, x1 - 6, y1 - 6, iu0, iv0, iu1, iv1, pipe });
    }

    if (!menuIsOpen) {
        std::string name = g_blockNames[g_placeable[g_player.hotbarIndex]];
        float scale = 0.8f;
        float tw = UITextWidth(name, scale);
        UIDrawText(glyphVerts, name, (SCREEN_W - tw) / 2.0f, hbY0 - 26.0f, scale, 1, 1, 1, 0.9f);
    }

    if (!g_mouseCaptured && !menuIsOpen) {
        std::string hint = "CLICK TO PLAY";
        float scale = 1.3f;
        float tw = UITextWidth(hint, scale);
        UIDrawText(glyphVerts, hint, (SCREEN_W - tw) / 2.0f, SCREEN_H * 0.42f, scale, 1, 1, 1, 0.9f);
    }

    // High-contrast mode (Accessibility, Section 11) pushes every panel/
    // button/track toward the luminance extremes -- near-black
    // backgrounds, a strongly saturated hover/handle color -- rather
    // than the subtle gray-shade steps used otherwise. Text is already
    // white-on-dark in both modes, at effectively maximum contrast, so
    // only the fill colors below need to branch.
    auto drawRowButton = [&](const UIRect& r, const std::string& label, float scale = 1.0f) {
        bool hover = PointInRect(g_mouseX, g_mouseY, r);
        if (g_highContrastUI) {
            float shade = hover ? 0.9f : 0.04f;
            UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, shade, shade, hover ? 0.1f : shade, 1);
        } else {
            float shade = hover ? 0.32f : 0.22f;
            UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, shade, shade, shade + 0.06f, 1);
        }
        float lw = UITextWidth(label, scale);
        UIDrawText(glyphVerts, label, r.x0 + ((r.x1 - r.x0) - lw) / 2.0f, r.y0 + (r.y1 - r.y0 - UI_CELL_H * scale) / 2.0f, scale, 1, 1, 1, 1);
    };
    // A slider row: label above, track+handle below. Value/range/label
    // text all come from the generic slider-by-ID lookups, so adding a
    // slider anywhere else only means adding cases there, not another
    // copy of this drawing code.
    auto drawSliderRow = [&](UIRect r, int sliderId) {
        UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, g_highContrastUI ? 0.03f : 0.16f, g_highContrastUI ? 0.03f : 0.16f, g_highContrastUI ? 0.03f : 0.19f, 1);
        UIDrawText(glyphVerts, GetSliderLabel(sliderId), r.x0 + 8, r.y0 + 2.0f, 0.8f, 1, 1, 1, 1);

        UIRect track = GetSliderTrackRect(r);
        UIDrawRect(glyphVerts, track.x0, track.y0, track.x1, track.y1, 0, 0, 0, 1);
        SliderRange rng = GetSliderRange(sliderId);
        float t = (GetSliderValue(sliderId) - rng.minV) / (rng.maxV - rng.minV);
        float handleCx = track.x0 + t * (track.x1 - track.x0);
        bool hover = PointInRect(g_mouseX, g_mouseY, GetSliderHitRect(r));
        float hc = hover ? 1.0f : (g_highContrastUI ? 0.95f : 0.85f);
        UIDrawRect(glyphVerts, handleCx - 6, track.y0 - 6, handleCx + 6, track.y1 + 6, hc, hc, g_highContrastUI ? 0.0f : 0.2f, 1);
    };
    auto drawPanelTitle = [&](const UIRect& panel, float panelW, const char* title, float scale) {
        float tw = UITextWidth(title, scale);
        UIDrawText(glyphVerts, title, panel.x0 + (panelW - tw) / 2.0f, panel.y0 + 16.0f, scale, 1, 1, 1, 1);
    };
    auto drawPanelBg = [&](const UIRect& panel) {
        if (g_highContrastUI) UIDrawRect(glyphVerts, panel.x0, panel.y0, panel.x1, panel.y1, 0.0f, 0.0f, 0.0f, 0.98f);
        else UIDrawRect(glyphVerts, panel.x0, panel.y0, panel.x1, panel.y1, 0.10f, 0.10f, 0.13f, 0.95f);
    };

    if (g_menuScreen != MenuScreen::None) {
        UIDrawRect(glyphVerts, 0, 0, (float)SCREEN_W, (float)SCREEN_H, 0, 0, 0, 0.55f);
    }

    if (g_menuScreen == MenuScreen::Pause) {
        UIRect panel = SubmenuPanelRect(PAUSE_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, PAUSE_LAYOUT.panelW, "PAUSED", 1.3f);

        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_RESUME), "RESUME");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_OPTIONS), "OPTIONS");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_SAVE), "SAVE GAME");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_LOAD), "LOAD GAME");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT_TO_TITLE), "QUIT TO TITLE");
        drawRowButton(SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT), "QUIT");
    } else if (g_menuScreen == MenuScreen::OptionsHub) {
        UIRect panel = SubmenuPanelRect(OPTIONS_HUB_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, OPTIONS_HUB_LAYOUT.panelW, "OPTIONS", 1.2f);

        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_LOOK), "LOOK SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_GRAPHICS), "GRAPHICS SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_DISPLAY), "DISPLAY SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_AUDIO), "AUDIO SETTINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_ACCESSIBILITY), "ACCESSIBILITY");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_KEYBINDS), "KEYBINDINGS");
        drawRowButton(SubmenuRowRect(OPTIONS_HUB_LAYOUT, OHROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::TitleMain) {
        UIRect panel = SubmenuPanelRect(TITLE_LAYOUT);
        drawPanelBg(panel);
        std::string gameTitle = "VOXISTICS";
        float titleScale = 1.6f;
        UIDrawText(glyphVerts, gameTitle, panel.x0 + (TITLE_LAYOUT.panelW - UITextWidth(gameTitle, titleScale)) / 2.0f, panel.y0 - 60.0f, titleScale, 1, 1, 1, 1);

        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_NEW_GAME), "NEW GAME");
        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_LOAD_GAME), "LOAD GAME");
        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_OPTIONS), "OPTIONS");
        drawRowButton(SubmenuRowRect(TITLE_LAYOUT, TROW_QUIT), "QUIT");
    } else if (g_menuScreen == MenuScreen::SlotPicker) {
        UIRect panel = SubmenuPanelRect(SLOT_PICKER_LAYOUT);
        drawPanelBg(panel);
        const char* title = g_slotPickerMode == SlotPickerMode::New ? "NEW GAME - CHOOSE A SLOT" : "LOAD GAME - CHOOSE A SLOT";
        drawPanelTitle(panel, SLOT_PICKER_LAYOUT.panelW, title, 0.9f);

        for (int slot = 0; slot < MAX_SAVE_SLOTS; slot++) {
            bool occupied = SlotExists(slot);
            std::string label;
            char slotNum[16];
            snprintf(slotNum, sizeof(slotNum), "WORLD %d", slot + 1);
            if (g_slotPickerMode == SlotPickerMode::New && g_confirmOverwriteSlot == slot) {
                label = std::string(slotNum) + " - CLICK AGAIN TO OVERWRITE";
            } else {
                label = std::string(slotNum) + (occupied ? " - SAVED" : " - EMPTY");
            }
            drawRowButton(SubmenuRowRect(SLOT_PICKER_LAYOUT, slot), label, 0.85f);
        }
        drawRowButton(SubmenuRowRect(SLOT_PICKER_LAYOUT, SLOTROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::LookSettings) {
        UIRect panel = SubmenuPanelRect(LOOK_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, LOOK_LAYOUT.panelW, "LOOK SETTINGS", 1.1f);

        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_X), g_invertX ? "INVERT X LOOK: ON" : "INVERT X LOOK: OFF");
        drawSliderRow(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_X), SLIDER_SENS_X);
        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_INVERT_Y), g_invertY ? "INVERT Y LOOK: ON" : "INVERT Y LOOK: OFF");
        drawSliderRow(SubmenuRowRect(LOOK_LAYOUT, LROW_SENS_Y), SLIDER_SENS_Y);
        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(LOOK_LAYOUT, LROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Graphics) {
        UIRect panel = SubmenuPanelRect(GRAPHICS_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, GRAPHICS_LAYOUT.panelW, "GRAPHICS SETTINGS", 1.0f);

        drawSliderRow(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RENDER_DIST), SLIDER_RENDER_DIST);
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Display) {
        UIRect panel = SubmenuPanelRect(DISPLAY_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, DISPLAY_LAYOUT.panelW, "DISPLAY SETTINGS", 1.0f);

        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_FPS), g_showFPS ? "SHOW FPS COUNTER: ON" : "SHOW FPS COUNTER: OFF");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Audio) {
        UIRect panel = SubmenuPanelRect(AUDIO_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, AUDIO_LAYOUT.panelW, "AUDIO SETTINGS", 1.0f);

        drawSliderRow(SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME), SLIDER_MASTER_VOLUME);
        drawSliderRow(SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME), SLIDER_MUSIC_VOLUME);
        drawRowButton(SubmenuRowRect(AUDIO_LAYOUT, AROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(AUDIO_LAYOUT, AROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Accessibility) {
        UIRect panel = SubmenuPanelRect(ACCESSIBILITY_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, ACCESSIBILITY_LAYOUT.panelW, "ACCESSIBILITY", 1.0f);

        drawSliderRow(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_FOV), SLIDER_FOV);
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_TOGGLE_MOVE), g_toggleMovement ? "TOGGLE-TO-MOVE: ON" : "TOGGLE-TO-MOVE: OFF");
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_HIGH_CONTRAST), g_highContrastUI ? "HIGH-CONTRAST UI: ON" : "HIGH-CONTRAST UI: OFF");
        drawSliderRow(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MUSIC_INTENSITY), SLIDER_MUSIC_INTENSITY);
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Keybindings) {
        UIRect panel = SubmenuPanelRect(KEYBIND_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, KEYBIND_LAYOUT.panelW, "KEYBINDINGS", 1.0f);
        std::string hint = "CLICK A ROW, THEN PRESS THE NEW INPUT";
        UIDrawText(glyphVerts, hint, panel.x0 + (KEYBIND_LAYOUT.panelW - UITextWidth(hint, 0.55f)) / 2.0f, panel.y0 + 44.0f, 0.55f, 0.8f, 0.8f, 0.8f, 0.8f);

        for (int i = 0; i < ACT_COUNT; i++) {
            std::string label;
            if (g_rebindingAction == i) label = std::string(g_actionLabels[i]) + ": PRESS INPUT (ESC CANCELS)";
            else label = std::string(g_actionLabels[i]) + ": [" + GetInputDisplayName(g_keyBindings[i]) + "]";
            drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, i), label, 0.7f);
        }
        drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT + 1), "BACK");
    }

    if (g_showFPS) {
        char buf[32];
        snprintf(buf, sizeof(buf), "FPS: %d", g_fpsDisplay);
        UIDrawText(glyphVerts, buf, 12.0f, 12.0f, 0.9f, 1, 1, 0.6f, 0.9f);
    }

    // Transient save/load confirmation -- fades over its last half
    // second so it doesn't just vanish abruptly.
    if (g_toastTimer > 0.0f) {
        float alpha = g_toastTimer < 0.5f ? g_toastTimer / 0.5f : 1.0f;
        float scale = 1.1f;
        float tw = UITextWidth(g_toastMessage, scale);
        UIDrawText(glyphVerts, g_toastMessage, (SCREEN_W - tw) / 2.0f, 40.0f, scale, 1.0f, 0.95f, 0.55f, alpha);
    }

    g_context->OMSetDepthStencilState(g_uiDepthState, 0);
    float blendFactor[4] = { 0, 0, 0, 0 };
    g_context->OMSetBlendState(g_uiBlendState, blendFactor, 0xFFFFFFFF);
    g_context->VSSetShader(g_uiVS, nullptr, 0);
    g_context->PSSetShader(g_uiPS, nullptr, 0);
    g_context->IASetInputLayout(g_uiLayout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetConstantBuffers(0, 1, &g_uiCBuffer);
    g_context->PSSetSamplers(0, 1, &g_uiSampler);

    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_uiCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        float* f = (float*)mapped.pData;
        f[0] = (float)SCREEN_W; f[1] = (float)SCREEN_H; f[2] = 0; f[3] = 0;
        g_context->Unmap(g_uiCBuffer, 0);
    }

    UIDrawBatch(glyphVerts, g_uiSRV);

    for (auto& ic : icons) {
        std::vector<UIVertex> iconVerts;
        UIAddQuad(iconVerts, ic.x0, ic.y0, ic.x1, ic.y1, ic.u0, ic.v0, ic.u1, ic.v1, 1, 1, 1, 1);
        UIDrawBatch(iconVerts, ic.pipe ? g_pipeSRV : g_atlasSRV);
    }

    // Restore world-pass defaults so next frame's world draws don't
    // inherit UI blend/depth state.
    g_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_depthState, 0);
}
