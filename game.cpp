// game.cpp
//
// Menu/UI state machine, input dispatch, WndProc, and the UI render
// pass -- see game.h for what crosses into main.cpp's own loop.

#ifndef NOMINMAX // also set project-wide (Voxistics.vcxproj)
#define NOMINMAX
#endif
#include <windows.h>
#include "game.h"
#include "world.h"
#include "render.h"
#include "audio.h"
#include "worldsound.h"
#include "persist.h"
#include "profiler.h"
#include "pulse.h"
#include "theline.h"
#include "essence.h"
#include "essencemap.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

// =======================================================================
// Section 4.6 - UI pass support: quad builders on top of render.h's
// font-glyph atlas layout (UIVertex, the font bands, UI_WHITE_H live
// there -- InitTextures/InitD3D need them too, this
// file only needs the drawing functions built on top).
// =======================================================================

// Nearest baked text size for a requested scale (1.0 == 28px cells).
static int UIBandForScale(float scale) {
    float want = 28.0f * scale;
    int best = 0;
    for (int i = 1; i < UI_FONT_BAND_COUNT; i++) {
        float d = UI_BAND_CELL_H[i] - want, dBest = UI_BAND_CELL_H[best] - want;
        if (d * d < dBest * dBest) best = i;
    }
    return best;
}

// Glyph cell `cell` (code-32) of font band `band`. The tiny inset only
// keeps float error from ever flooring into the next cell; at 1:1 with
// point sampling every screen pixel lands on a texel centre anyway.
static void UIGlyphRect(const UIFontBand& fb, int cell, float& u0, float& v0, float& u1, float& v1) {
    float texW = (float)UIAtlasWidth(), texH = (float)UIAtlasHeight();
    float x = (float)((cell % UI_ATLAS_COLS) * fb.cellW);
    float y = (float)(fb.atlasY + (cell / UI_ATLAS_COLS) * fb.cellH);
    const float e = 1.0f / 64.0f;
    u0 = (x + e) / texW;               u1 = (x + fb.cellW - e) / texW;
    v0 = (y + e) / texH;               v1 = (y + fb.cellH - e) / texH;
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

// Untextured tinted rectangle. All four corners sample the middle of the
// atlas's solid-white strip, so the fill is uniform right to its edges.
// (It used to stretch a whole white glyph cell, whose 1px transparent
// rim smeared into a wide fade around every panel and the menu dim.)
static void UIDrawRect(std::vector<UIVertex>& v, float x0, float y0, float x1, float y1,
                        float r, float g, float b, float a) {
    float u = 0.5f * UI_WHITE_H / UIAtlasWidth(), vv = 0.5f * UI_WHITE_H / UIAtlasHeight();
    UIAddQuad(v, x0, y0, x1, y1, u, vv, u, vv, r, g, b, a);
}

static float UITextWidth(const std::string& text, float scale) {
    return (float)(text.size() * UIGetFontBand(UIBandForScale(scale)).advance);
}
static float UITextHeight(float scale) {
    return (float)UIGetFontBand(UIBandForScale(scale)).cellH;
}

// Fixed-advance (monospace) text at the nearest baked size, drawn 1:1 on
// whole pixels -- see render.h. Quads are a padded cell wide but step by
// the font's advance, so neighbouring quads overlap only in their
// transparent padding.
static void UIDrawText(std::vector<UIVertex>& v, const std::string& text, float x, float y,
                        float scale, float r, float g, float b, float a) {
    UIFontBand fb = UIGetFontBand(UIBandForScale(scale));
    float curX = floorf(x + 0.5f) - UI_GLYPH_PAD, top = floorf(y + 0.5f);
    for (char c : text) {
        int cell = UICharCell(c);
        if (cell > 0) { // space (cell 0) has no ink
            float u0, v0, u1, v1;
            UIGlyphRect(fb, cell, u0, v0, u1, v1);
            UIAddQuad(v, curX, top, curX + fb.cellW, top + fb.cellH, u0, v0, u1, v1, r, g, b, a);
        }
        curX += fb.advance;
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
// Where the slot picker's BACK row returns to: TitleMain, or Pause when
// Load Game was opened mid-game.
static MenuScreen g_slotPickerReturnScreen = MenuScreen::TitleMain;
// New Game on an already-occupied slot needs a confirmation rather than
// silently overwriting -- a second click within a few seconds confirms;
// otherwise the arm times out and a third click starts over.
int g_confirmOverwriteSlot = -1;
float g_confirmOverwriteTimer = 0.0f;
static int g_mouseX = 0, g_mouseY = 0;
// A button press in progress (menus): buttons act on release, like real
// ones -- held down they show pressed in, and sliding off before letting
// go cancels. g_pressRect is the button under the press, found while the
// buttons draw (a press that lands on no drawn button acts on release as
// clicks always did).
static std::string g_toastMessage;
float g_toastTimer = 0.0f; // seconds remaining; drawn by RenderUIPass
float g_fpsTimer = 0.0f;
int g_fpsFrameCount = 0, g_fpsDisplay = 0; // updated once/sec, shown when Display Settings' FPS counter is on

static void OpenStore(int x, int y, int z);
static void PickAndAct(bool breakBlock) {
    Vec3 f, r, u;
    GetCameraVectors(g_player, f, r, u);
    float dx = f.x, dy = f.y, dz = f.z;
    float ex = g_player.x, ey = g_player.y + g_player.eyeHeight, ez = g_player.z;

    int hx, hy, hz, px, py, pz;
    if (!Raycast(g_world, ex, ey, ez, dx, dy, dz, 6.0f, hx, hy, hz, px, py, pz)) return;

    if (breakBlock) {
        // The world floor stays: nothing exists below it, so a hole there
        // would drop the player into an endless void.
        if (hy <= Y_MIN) return;
        BlockID taken = g_world.Get(hx, hy, hz);
        if (taken == BLOCK_ATTRACTOR) g_essence.RemoveAttractor(hx, hy, hz);
        LiveEdit(g_world, hx, hy, hz, BLOCK_AIR);
        WorldSoundBreak(taken, hx, hy, hz);
    } else {
        // Using a block that holds pulse opens it (crouch to place against it instead).
        if (PulseCapacity(g_world.Get(hx, hy, hz)) > 0 && !g_player.crouching) { OpenStore(hx, hy, hz); return; }
        // Refuse a placement that would overlap the player's own box --
        // it would only trap them (or, with physics' unstick rule, pop
        // them up on top of it).
        const Player& p = g_player;
        bool overlapsPlayer = px + 1 > p.x - PLAYER_HALFW && px < p.x + PLAYER_HALFW
                           && py + 1 > p.y && py < p.y + PlayerHeight(p)
                           && pz + 1 > p.z - PLAYER_HALFW && pz < p.z + PLAYER_HALFW;
        if (overlapsPlayer) { WorldSoundCue(SND_CANT); return; }
        BlockID toPlace = g_hotbar[g_player.hotbarIndex];
        // The state byte, by the block's placement rule (blocks.h).
        BlockFace look = fabsf(f.x) > fabsf(f.z) ? (f.x > 0 ? FACE_POS_X : FACE_NEG_X)
                                                  : (f.z > 0 ? FACE_POS_Z : FACE_NEG_Z);
        static const BlockFace opposite[FACE_COUNT] = { FACE_NEG_X, FACE_POS_X, FACE_NEG_Y, FACE_POS_Y, FACE_NEG_Z, FACE_POS_Z };
        int nx = px - hx, ny = py - hy, nz = pz - hz; // normal of the face clicked
        uint8_t state = 0;
        switch (g_blocks[toPlace].place) {
        case PLACE_FACE_PLAYER: state = opposite[look]; break;  // front toward the player
        case PLACE_AWAY:        state = look; break;            // ramp rises away from the player
        case PLACE_CLICKED_AXIS:
            state = nx > 0 ? FACE_POS_X : nx < 0 ? FACE_NEG_X : ny > 0 ? FACE_POS_Y : ny < 0 ? FACE_NEG_Y : nz > 0 ? FACE_POS_Z : FACE_NEG_Z;
            break;
        case PLACE_SLAB_HALF: {
            // Under a block -> top half; on top of one -> bottom half; on a
            // side -> whichever half of that face the crosshair was on.
            bool upper = ny < 0;
            if (nx != 0 || nz != 0) {
                float plane = nx > 0 ? hx + 1.0f : nx < 0 ? (float)hx : nz > 0 ? hz + 1.0f : (float)hz;
                float o = nx != 0 ? ex : ez, d = nx != 0 ? dx : dz;
                if (fabsf(d) > 1e-6f) upper = (ey + (plane - o) / d * dy) - py > 0.5f;
            }
            state = upper ? STATE_UPPER : 0;
            break;
        }
        default: break;
        }
        LiveEdit(g_world, px, py, pz, toPlace, state);
        if (toPlace == BLOCK_ATTRACTOR) g_essence.AddAttractor(px, py, pz);
        g_pulse.OnPlaced(px, py, pz, toPlace);
        WorldSoundPlace(toPlace, px, py, pz);
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
    float px = (g_screenW - L.panelW) / 2.0f, py = (g_screenH - h) / 2.0f;
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
// Row height 50 still fits the slider row (label, track, 50px hit area)
// while keeping six rows comfortably inside a 720p screen.
static const SubmenuLayout GRAPHICS_LAYOUT = { 400.0f, 50.0f, 10.0f, 70.0f, 20.0f, 9 };
enum GraphicsRow { GROW_RENDER_DIST = 0, GROW_FRAME_LIMIT = 1, GROW_VSYNC = 2, GROW_SHADOWS = 3, GROW_OUTLINES = 4, GROW_SSAO = 5, GROW_BLOOM = 6, GROW_RESET = 7, GROW_BACK = 8 };

// Display: one real setting -- an FPS counter toggle. Resolution/
// fullscreen switching would need swap-chain resize and WM_SIZE
// handling this prototype doesn't have yet, so it isn't faked here.
static const SubmenuLayout DISPLAY_LAYOUT  = { 340.0f, 40.0f, 12.0f, 70.0f, 20.0f, 5 };
enum DisplayRow { DROW_SHOW_FPS = 0, DROW_SHOW_PROFILER = 1, DROW_FULLSCREEN = 2, DROW_RESET = 3, DROW_BACK = 4 };

// Audio: Master and Music sliders, backed by a real XAudio2 voice
// (Section 10) playing the procedural ambient track. Separate channels
// now even though Music is the only one with anything to play yet, so a
// future SFX channel is one more slider, not a remix of this one.
static const SubmenuLayout AUDIO_LAYOUT    = { 380.0f, 56.0f, 12.0f, 70.0f, 20.0f, 5 };
enum AudioRow { AROW_MASTER_VOLUME = 0, AROW_MUSIC_VOLUME = 1, AROW_WORLD_VOLUME = 2, AROW_RESET = 3, AROW_BACK = 4 };

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
static const SubmenuLayout ACCESSIBILITY_LAYOUT = { 400.0f, 56.0f, 12.0f, 70.0f, 20.0f, 7 };
enum AccessibilityRow { ARROW_FOV = 0, ARROW_TOGGLE_MOVE = 1, ARROW_HIGH_CONTRAST = 2, ARROW_MUSIC_INTENSITY = 3, ARROW_MONO = 4, ARROW_RESET = 5, ARROW_BACK = 6 };

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
    "BREAK BLOCK", "PLACE BLOCK", "PAUSE MENU", "QUICK SAVE", "QUICK LOAD", "ESSENCE MAP",
    "SPRINT", "CROUCH / SLIDE", "BLOCK LIBRARY"
};
// Rows sized so all of them (+reset +back) fit the minimum 680 px window.
static const SubmenuLayout KEYBIND_LAYOUT = { 480.0f, 26.0f, 5.0f, 88.0f, 18.0f, ACT_COUNT + 2 };

static const int g_defaultBindings[ACT_COUNT] = {
    'W', 'S', 'A', 'D', VK_SPACE, MOUSE_LEFT, MOUSE_RIGHT, VK_ESCAPE, VK_F5, VK_F9, 'M', VK_SHIFT, VK_CONTROL, 'E'
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

static GameAction OppositeMove(GameAction a) {
    switch (a) {
    case ACT_FORWARD: return ACT_BACK;
    case ACT_BACK: return ACT_FORWARD;
    case ACT_LEFT: return ACT_RIGHT;
    default: return ACT_LEFT;
    }
}
// A fresh press of a movement binding flips its latch, whether the
// binding is a key or a mouse button. Latching a direction releases its
// opposite: with forward and back both latched they cancel out, and the
// next press un-latches the one the player didn't mean, which reads as
// inverted controls. Perpendicular latches still combine, for diagonals.
static void ToggleMoveLatches(int code) {
    if (!g_toggleMovement || g_menuScreen != MenuScreen::None) return;
    for (GameAction a : { ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT }) {
        if (code != g_keyBindings[a]) continue;
        g_moveToggleLatch[a] = !g_moveToggleLatch[a];
        if (g_moveToggleLatch[a]) g_moveToggleLatch[OppositeMove(a)] = false;
    }
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
    g_shadows = true; g_postEdges = false; g_postSSAO = false; g_bloom = true;
    g_vsync = true; g_frameLimit = 60;
    g_lastPlayerChunkX = INT32_MIN; g_lastPlayerChunkZ = INT32_MIN; // force a rescan at the new radius
}
static void ResetDisplaySettings() { g_showFPS = false; g_showProfiler = false; if (g_fullscreen) { g_fullscreen = false; ApplyFullscreen(false); } }
static void ResetAudioSettings() { g_masterVolume = 1.0f; g_musicVolume = 1.0f; g_worldVolume = 1.0f; ApplyAudioVolumes(); }
static void ResetAccessibilitySettings() {
    g_fov = 45.0f;
    g_toggleMovement = false;
    g_highContrastUI = false;
    g_monoAudio = false;
    g_musicIntensity = 1.0f;
    memset(g_moveToggleLatch, 0, sizeof(g_moveToggleLatch));
}

// A handful of settings are sliders rather than toggles/buttons. One
// small generic slider system (value/range/row-rect all looked up by
// ID) instead of one-off X-sensitivity-shaped code repeated per slider.
enum SliderId { SLIDER_NONE = -1, SLIDER_SENS_X = 0, SLIDER_SENS_Y = 1, SLIDER_RENDER_DIST = 2, SLIDER_MASTER_VOLUME = 3, SLIDER_MUSIC_VOLUME = 4, SLIDER_FOV = 5, SLIDER_MUSIC_INTENSITY = 6, SLIDER_WORLD_VOLUME = 7, SLIDER_FRAME_LIMIT = 8 };
static int g_draggingSlider = SLIDER_NONE;

struct SliderRange { float minV, maxV; };
static SliderRange GetSliderRange(int id) {
    switch (id) {
    case SLIDER_SENS_X: case SLIDER_SENS_Y: return { SENS_MIN, SENS_MAX };
    case SLIDER_RENDER_DIST: return { 1.0f, 8.0f };
    case SLIDER_FRAME_LIMIT: return { 30.0f, 200.0f };
    case SLIDER_MASTER_VOLUME: case SLIDER_MUSIC_VOLUME: case SLIDER_WORLD_VOLUME: return { 0.0f, 1.0f };
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
    case SLIDER_FRAME_LIMIT: return SubmenuRowRect(GRAPHICS_LAYOUT, GROW_FRAME_LIMIT);
    case SLIDER_MASTER_VOLUME: return SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME);
    case SLIDER_MUSIC_VOLUME: return SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME);
    case SLIDER_WORLD_VOLUME: return SubmenuRowRect(AUDIO_LAYOUT, AROW_WORLD_VOLUME);
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
    case SLIDER_FRAME_LIMIT: return (float)g_frameLimit;
    case SLIDER_MASTER_VOLUME: return g_masterVolume;
    case SLIDER_MUSIC_VOLUME: return g_musicVolume;
    case SLIDER_WORLD_VOLUME: return g_worldVolume;
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
    case SLIDER_FRAME_LIMIT: g_frameLimit = (int)(v / 10.0f + 0.5f) * 10; break; // steps of 10
    case SLIDER_MASTER_VOLUME: g_masterVolume = v; ApplyAudioVolumes(); break;
    case SLIDER_MUSIC_VOLUME: g_musicVolume = v; ApplyAudioVolumes(); break;
    case SLIDER_WORLD_VOLUME: g_worldVolume = v; ApplyAudioVolumes(); break;
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
    case SLIDER_FRAME_LIMIT: snprintf(buf, sizeof(buf), "FRAME RATE LIMIT: %d FPS", g_frameLimit); break;
    case SLIDER_MASTER_VOLUME: snprintf(buf, sizeof(buf), "MASTER VOLUME: %d%%", (int)(g_masterVolume * 100.0f + 0.5f)); break;
    case SLIDER_MUSIC_VOLUME: snprintf(buf, sizeof(buf), "MUSIC VOLUME: %d%%", (int)(g_musicVolume * 100.0f + 0.5f)); break;
    case SLIDER_WORLD_VOLUME: snprintf(buf, sizeof(buf), "WORLD SOUNDS: %d%%", (int)(g_worldVolume * 100.0f + 0.5f)); break;
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
// Borderless fullscreen: the window loses its frame and covers its
// monitor (no exclusive mode -- alt-tab and other monitors behave
// normally); WM_SIZE then resizes the backbuffer to match.
static WINDOWPLACEMENT g_windowedPlacement = {};
static bool g_isFullscreen = false;
void ApplyFullscreen(bool on) {
    if (on == g_isFullscreen || !g_hwnd) return;
    if (on) {
        g_windowedPlacement.length = sizeof(g_windowedPlacement);
        GetWindowPlacement(g_hwnd, &g_windowedPlacement);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(g_hwnd, &g_windowedPlacement);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g_isFullscreen = on;
}

static void ToggleFullscreenSetting() {
    g_fullscreen = !g_fullscreen;
    ApplyFullscreen(g_fullscreen);
    SaveSettings();
}

void ShowToast(const std::string& message, float seconds) {
    g_toastMessage = message;
    g_toastTimer = seconds;
}

// ---- Debug time control (Section 13) --------------------------------
// There is one clock -- g_dayTimeSeconds -- and the sky, sun, shadows,
// light and music all read it, so moving it moves everything. F8 jumps to
// the next time of day; holding ] or Page Up runs it forward (a whole day
// in 15 s), [ or Page Down backward. The music stops while scrubbing and
// re-anchors to the new time on release. A testing aid, like F3 and F7.
static const float DEBUG_SCRUB_RATE = DAY_LENGTH_SECONDS / 15.0f; // clock seconds per real second
static const float kTimePresets[] = { 60.0f, 600.0f, 1500.0f, 2400.0f, 2940.0f, 3300.0f };
static bool g_timeScrubbing = false;

static std::string DayTimeLabel(float t) {
    const char* phase = t < 300 ? "DAWN" : t < 1200 ? "MORNING" : t < 1800 ? "NOON" : t < 2700 ? "AFTERNOON" : t < 3000 ? "DUSK" : "NIGHT";
    char buf[48];
    snprintf(buf, sizeof(buf), "TIME %02d:%02d  %s", (int)t / 60, (int)t % 60, phase);
    return buf;
}

static bool DebugKeyFree(int vk) {
    for (int a = 0; a < ACT_COUNT; a++) if (g_keyBindings[a] == vk) return false; // a bound action wins
    return true;
}

static void JumpToNextTimeOfDay() {
    float next = kTimePresets[0];
    for (float p : kTimePresets) if (p > g_dayTimeSeconds + 1.0f) { next = p; break; }
    g_dayTimeSeconds = next;
    StartMusicPlayback(); // re-anchor to the new time
    ShowToast(DayTimeLabel(g_dayTimeSeconds), 2.0f);
}

void UpdateDebugTimeScrub(float frameSeconds) {
    bool playing = g_gameState == GameState::InGame && g_menuScreen == MenuScreen::None;
    auto held = [](int vk) { return vk >= 0 && vk < 256 && g_keyDown[vk] && DebugKeyFree(vk); };
    int dir = (held(VK_OEM_6) || held(VK_PRIOR) ? 1 : 0) - (held(VK_OEM_4) || held(VK_NEXT) ? 1 : 0);
    if (playing && dir != 0) {
        if (!g_timeScrubbing) { StopMusicPlayback(); g_timeScrubbing = true; }
        float t = fmodf(g_dayTimeSeconds + dir * DEBUG_SCRUB_RATE * frameSeconds, DAY_LENGTH_SECONDS);
        g_dayTimeSeconds = t < 0 ? t + DAY_LENGTH_SECONDS : t;
        ShowToast(DayTimeLabel(g_dayTimeSeconds), 1.5f);
    } else if (g_timeScrubbing) {
        g_timeScrubbing = false;
        if (playing) StartMusicPlayback();
    }
}

static void DoSave() {
    bool ok = SaveGame(g_world, g_player, g_currentSlot);
    if (ok) WorldSoundCue(SND_SEALED);
    g_toastMessage = ok ? "GAME SAVED" : "SAVE FAILED";
    g_toastTimer = 2.0f;
}
// Autosave (Section 7.3): every AUTOSAVE_SECONDS of actual play (paused
// time doesn't count), plus on Quit to Title and on closing the window
// mid-game. A delta save (7.2) is small, so this doesn't hitch.
static const float AUTOSAVE_SECONDS = 300.0f;
static float g_autosaveTimer = 0.0f;
static void AutosaveNow(bool announce) {
    if (g_gameState != GameState::InGame) return;
    bool ok = SaveGame(g_world, g_player, g_currentSlot);
    if (announce || !ok) ShowToast(ok ? "AUTOSAVED" : "AUTOSAVE FAILED", ok ? 1.5f : 3.0f);
    g_autosaveTimer = 0.0f;
}
bool IsInGame() { return g_gameState == GameState::InGame; }

// The top of a performance report: what was measured, and on what.
static std::string PerfReportHeader() {
    char b[512];
#ifdef _DEBUG
    const char* build = "Debug";
#else
    const char* build = "Release";
#endif
    snprintf(b, sizeof b,
             "Voxistics performance report\n"
             "build: %s\nwindow: %d x %d%s\n"
             "render distance: %d  shadows: %s  bloom: %s  SSAO: %s  edges: %s  music intensity: %.0f%%\n"
             "day time: %s  position: %.0f, %.0f, %.0f\n\n",
             build, g_screenW, g_screenH, g_fullscreen ? " (fullscreen)" : "",
             g_loadRadius, g_shadows ? "on" : "off", g_bloom ? "on" : "off", g_postSSAO ? "on" : "off", g_postEdges ? "on" : "off",
             g_musicIntensity * 100.0f, DayTimeLabel(g_dayTimeSeconds).c_str(), g_player.x, g_player.y, g_player.z);
    return b + ProfBootSummary(true) + "\n";
}

void PollPerfCapture() {
    std::string text;
    if (!ProfTakeCaptureReport(text)) return;
    std::string path = WriteTextToSaveFolder("perf_report.txt", text);
    // The full path: Documents is often moved (into OneDrive, say), so
    // "next to your saves" alone can send the player to the wrong folder.
    ShowToast(path.empty() ? "PERF REPORT COULD NOT BE SAVED" : "PERF REPORT SAVED: " + path, 12.0f);
}
void TickAutosave(float dt) {
    if (g_gameState != GameState::InGame || g_menuScreen != MenuScreen::None) return;
    g_autosaveTimer += dt;
    if (g_autosaveTimer >= AUTOSAVE_SECONDS) AutosaveNow(true);
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
        // Same slot list as the title screen's Load Game (the bound
        // quick-load input is what reloads the current slot directly).
        g_slotPickerMode = SlotPickerMode::Load;
        g_slotPickerReturnScreen = MenuScreen::Pause;
        g_confirmOverwriteSlot = -1;
        g_menuScreen = MenuScreen::SlotPicker;
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT_TO_TITLE))) {
        AutosaveNow(false);
        g_gameState = GameState::Title;
        g_menuScreen = MenuScreen::TitleMain;
        StopMusicPlayback(); // already stopped (Pause is only reachable with music already stopped), but explicit/idempotent
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(PAUSE_LAYOUT, PROW_QUIT))) { AutosaveNow(false); PostQuitMessage(0); return; }
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
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_FRAME_LIMIT)))) { BeginSliderDrag(SLIDER_FRAME_LIMIT, mx); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_VSYNC))) { g_vsync = !g_vsync; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_SHADOWS))) { g_shadows = !g_shadows; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_OUTLINES))) { g_postEdges = !g_postEdges; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_SSAO))) { g_postSSAO = !g_postSSAO; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BLOOM))) { g_bloom = !g_bloom; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RESET))) { ResetGraphicsSettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleDisplayClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_FPS))) { g_showFPS = !g_showFPS; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_PROFILER))) { g_showProfiler = !g_showProfiler; SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_FULLSCREEN))) { ToggleFullscreenSetting(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_RESET))) { ResetDisplaySettings(); SaveSettings(); return; }
    if (PointInRect(mx, my, SubmenuRowRect(DISPLAY_LAYOUT, DROW_BACK))) { g_menuScreen = MenuScreen::OptionsHub; return; }
}
static void HandleAudioClick(int mx, int my) {
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME)))) { BeginSliderDrag(SLIDER_MASTER_VOLUME, mx); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME)))) { BeginSliderDrag(SLIDER_MUSIC_VOLUME, mx); return; }
    if (PointInRect(mx, my, GetSliderHitRect(SubmenuRowRect(AUDIO_LAYOUT, AROW_WORLD_VOLUME)))) { BeginSliderDrag(SLIDER_WORLD_VOLUME, mx); return; }
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
    if (PointInRect(mx, my, SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MONO))) { g_monoAudio = !g_monoAudio; SaveSettings(); return; }
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
    g_worldGen = DefaultNewWorldGen(); // TerrainHeight below reads it
    ResetLine(g_line); // a new world has no history to pivot around
    g_pulse.Reset();   // ...and nothing in its pipes
    g_essence.Reset(g_worldGen.seed); // nothing discovered yet
    // Start standing on the surface (terrain height is a pure function
    // of x/z, so this needs no generated chunks), taking the highest of
    // the cells the player's footprint overlaps.
    int top = 0;
    for (float ox : { -PLAYER_HALFW, PLAYER_HALFW })
        for (float oz : { -PLAYER_HALFW, PLAYER_HALFW })
            top = std::max(top, TerrainHeight((int)floorf(g_player.x + ox), (int)floorf(g_player.z + oz)));
    g_player.y = (float)(top + 1);
    g_dayTimeSeconds = 0.0f; // dawn -- first light in a land they've never seen (Section 13)
    g_residentColumns.clear();
    g_evictedChunks.clear();
    ClearScheduledUpdates();
    g_pendingColumns.clear();
    g_pendingColumnSet.clear();
    g_pendingEvictions.clear();
    g_pendingEvictionSet.clear();
    g_lastPlayerChunkX = INT32_MIN;
    g_lastPlayerChunkZ = INT32_MIN;
}

// Shared tail end of both New Game and Load Game: leave the slot
// picker, mark a real game as running, and hand control to the player.
static void EnterGameplay() {
    WorldSoundReset(); // a fresh session: discoveries start over
    g_gameState = GameState::InGame;
    g_autosaveTimer = 0.0f;
    g_menuScreen = MenuScreen::None;
    CaptureMouseForPlay();
    StartMusicPlayback();
}

static void HandleTitleClick(int mx, int my) {
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_NEW_GAME))) {
        g_slotPickerMode = SlotPickerMode::New;
        g_slotPickerReturnScreen = MenuScreen::TitleMain;
        g_confirmOverwriteSlot = -1;
        g_menuScreen = MenuScreen::SlotPicker;
        return;
    }
    if (PointInRect(mx, my, SubmenuRowRect(TITLE_LAYOUT, TROW_LOAD_GAME))) {
        g_slotPickerMode = SlotPickerMode::Load;
        g_slotPickerReturnScreen = MenuScreen::TitleMain;
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
            // May change g_dayTimeSeconds -- EnterGameplay's
            // StartMusicPlayback re-anchors to whatever it loaded.
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
    if (PointInRect(mx, my, SubmenuRowRect(SLOT_PICKER_LAYOUT, SLOTROW_BACK))) { g_menuScreen = g_slotPickerReturnScreen; return; }
}

// Centralizes what a just-pressed input does, whatever its source (a
// VK_* from WM_KEYDOWN, or a MOUSE_* sentinel from a mouse-button-down
// message) -- the single place Menu/Save/Load/Break/Place dispatch is
// gated, so every input source is guaranteed to agree on the rules
// instead of each caller re-deriving them.
// ---- Essence network map (Part XIX): a menu screen, so the world is
// frozen and the cursor free while it's open, like every other menu. ----
static MapCamera g_mapCamera;
static bool g_mapDragging = false;
static int g_mapDragX = 0, g_mapDragY = 0;
static void OpenMap() {
    // Opens on The Line's pivot (the player's favourite place) at a
    // zoom showing a few hundred blocks around it (the view origin only;
    // no node moves).
    g_mapCamera.centerX = g_line.pivotX;
    g_mapCamera.centerZ = g_line.pivotZ;
    g_mapCamera.scale = 0.5f;
    g_essence.RefreshRoutes();
    g_menuScreen = MenuScreen::Map;
    ReleaseMouseForMenu();
    StopMusicPlayback();
}
static void CloseMap() {
    g_mapDragging = false;
    g_menuScreen = MenuScreen::None;
    CaptureMouseForPlay();
    StartMusicPlayback();
}

// ---- Block library (library.h): every placeable block in a grid; click
// one to place it now, or drag it onto a hotbar slot to keep it there.
static LibraryGesture g_libGesture;
static int g_libScroll = 0; // first visible row
static void OpenLibrary() {
    g_libGesture = LibraryGesture();
    g_menuScreen = MenuScreen::Library;
    ReleaseMouseForMenu();
    StopMusicPlayback();
    WorldSoundCue(SND_LIBRARY_OPEN);
}
// `quiet`: a pick already made its own sound (Drop).
static void CloseLibrary(bool quiet = false) {
    if (!quiet) WorldSoundCue(SND_LIBRARY_CLOSE);
    g_libGesture = LibraryGesture();
    g_menuScreen = MenuScreen::None;
    CaptureMouseForPlay();
    StartMusicPlayback();
}
static void LibraryMouseDown(int mx, int my) {
    LibraryLayout L = ComputeLibraryLayout(g_screenW, g_screenH, g_placeableList.count);
    int entry = LibraryCellAt(L, g_placeableList.count, g_libScroll, (float)mx, (float)my);
    if (entry >= 0) { LibraryPress(g_libGesture, entry, (float)mx, (float)my); return; }
    int slot = HotbarSlotAt(g_screenW, g_screenH, (float)mx, (float)my);
    if (slot >= 0) g_player.hotbarIndex = slot; // pick which slot a click fills
}
static void LibraryMouseUp(int mx, int my) {
    LibraryResult r = LibraryRelease(g_libGesture, HotbarSlotAt(g_screenW, g_screenH, (float)mx, (float)my));
    if (r.outcome == LibraryOutcome::Select) {
        g_hotbar[g_player.hotbarIndex] = g_placeableList.ids[r.entry];
        SaveSettings();
        WorldSoundCue(SND_DROP, g_player.hotbarIndex, 0.0f);
        CloseLibrary(true);
    } else if (r.outcome == LibraryOutcome::Assign) {
        g_hotbar[r.slot] = g_placeableList.ids[r.entry];
        g_player.hotbarIndex = r.slot;
        SaveSettings();
        WorldSoundCue(SND_DROP, r.slot, 1.0f);
    }
}

// ---- A store's contents (Part VI): the pulse it holds, one slot per
// spin, each a heap of beads that grows with the amount -- a feel, not a
// number. Place-button on a store opens it; Esc, E or the place button
// again closes it.
static int g_storeX, g_storeY, g_storeZ;
static void OpenStore(int x, int y, int z) {
    g_storeX = x; g_storeY = y; g_storeZ = z;
    g_menuScreen = MenuScreen::Store;
    ReleaseMouseForMenu();
    WorldSoundCue(SND_LIBRARY_OPEN);
}
static void CloseStore() {
    WorldSoundCue(SND_LIBRARY_CLOSE);
    g_menuScreen = MenuScreen::None;
    CaptureMouseForPlay();
}

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
            FadeWorldSounds(0.3f); // pause is silence: time stopped
        } else if (g_menuScreen == MenuScreen::Pause) {
            g_menuScreen = MenuScreen::None;
            CaptureMouseForPlay();
            StartMusicPlayback();
        } else if (g_menuScreen == MenuScreen::Map) {
            CloseMap();
        } else if (g_menuScreen == MenuScreen::Library) {
            CloseLibrary();
        } else if (g_menuScreen == MenuScreen::Store) {
            CloseStore();
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
    if (code == g_keyBindings[ACT_MAP]) {
        if (g_menuScreen == MenuScreen::None) OpenMap();
        else if (g_menuScreen == MenuScreen::Map) CloseMap();
        return;
    }
    if (g_menuScreen == MenuScreen::Store && (code == g_keyBindings[ACT_LIBRARY] || code == g_keyBindings[ACT_PLACE])) { CloseStore(); return; }
    if (code == g_keyBindings[ACT_LIBRARY]) {
        if (g_menuScreen == MenuScreen::None) OpenLibrary();
        else if (g_menuScreen == MenuScreen::Library) CloseLibrary();
        return;
    }
    if (code == g_keyBindings[ACT_SAVE]) { DoSave(); return; }
    if (code == g_keyBindings[ACT_LOAD]) {
        DoLoad();
        // DoLoad may change g_dayTimeSeconds, so re-anchor the music -- but
        // only if play is live. Quick-loading from a menu leaves it paused
        // and silent; Resume restarts the music at the loaded time.
        if (g_menuScreen == MenuScreen::None) StartMusicPlayback();
        return;
    }
    if (g_menuScreen != MenuScreen::None) return; // Break/Place only fire during actual play
    if (!g_mouseCaptured) return;
    if (code == g_keyBindings[ACT_BREAK]) { PickAndAct(true); return; }
    if (code == g_keyBindings[ACT_PLACE]) { PickAndAct(false); return; }
}

// Dispatches a click to whichever submenu is currently open. Only
// called for the left button -- menus never respond to right/middle
// click, matching ordinary UI convention.
static bool g_pressActive = false;
static int g_pressX = 0, g_pressY = 0;
static UIRect g_pressRect = {};
static bool g_pressRectValid = false;

// Sliders act on press (they're dragged), and so do the map and library.
static bool PressActsImmediately(int mx, int my) {
    if (g_menuScreen == MenuScreen::Map || g_menuScreen == MenuScreen::Library) return true;
    static const struct { int id; MenuScreen screen; } sliders[] = {
        { SLIDER_SENS_X, MenuScreen::LookSettings }, { SLIDER_SENS_Y, MenuScreen::LookSettings },
        { SLIDER_RENDER_DIST, MenuScreen::Graphics }, { SLIDER_FRAME_LIMIT, MenuScreen::Graphics }, { SLIDER_MASTER_VOLUME, MenuScreen::Audio },
        { SLIDER_MUSIC_VOLUME, MenuScreen::Audio }, { SLIDER_WORLD_VOLUME, MenuScreen::Audio },
        { SLIDER_FOV, MenuScreen::Accessibility }, { SLIDER_MUSIC_INTENSITY, MenuScreen::Accessibility },
    };
    for (const auto& s : sliders)
        if (s.screen == g_menuScreen && PointInRect(mx, my, GetSliderHitRect(GetSliderRowRect(s.id)))) return true;
    return false;
}

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
    case MenuScreen::Map: g_mapDragging = true; g_mapDragX = mx; g_mapDragY = my; break; // drag to pan
    case MenuScreen::Library: LibraryMouseDown(mx, my); break;
    default: break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        AutosaveNow(false); // the window's X button mid-game
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_GETMINMAXINFO: {
        // Below this client size the taller menus (Keybindings: 13 rows)
        // and the hotbar would run off the screen.
        RECT r = { 0, 0, MIN_CLIENT_W, MIN_CLIENT_H };
        AdjustWindowRect(&r, (DWORD)GetWindowLongW(hwnd, GWL_STYLE), FALSE);
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_SIZE:
        // The backbuffer follows the client area (resizing, maximising,
        // fullscreen); a minimised window keeps its old size.
        if (wParam != SIZE_MINIMIZED) ResizeRenderTargets((int)LOWORD(lParam), (int)HIWORD(lParam));
        return 0;
    case WM_MOUSEMOVE:
        g_mouseX = (int)(short)LOWORD(lParam);
        g_mouseY = (int)(short)HIWORD(lParam);
        ApplySliderDrag(g_mouseX); // no-op unless a slider is actively held
        if (g_mapDragging && g_menuScreen == MenuScreen::Map) {
            g_mapCamera.centerX -= (g_mouseX - g_mapDragX) / g_mapCamera.scale;
            g_mapCamera.centerZ += (g_mouseY - g_mapDragY) / g_mapCamera.scale; // screen down = world -Z
            g_mapDragX = g_mouseX; g_mapDragY = g_mouseY;
        }
        if (g_menuScreen == MenuScreen::Library) {
            bool wasDragging = g_libGesture.dragging;
            LibraryMove(g_libGesture, (float)g_mouseX, (float)g_mouseY);
            if (g_libGesture.dragging && !wasDragging) WorldSoundCue(SND_PICK);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (g_menuScreen == MenuScreen::Map) {
            // Zoom about the cursor (wheel messages carry screen coordinates).
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            ScreenToClient(hwnd, &pt);
            MapZoomAt(g_mapCamera, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.25f : 0.8f, (float)pt.x, (float)pt.y);
            return 0;
        }
        if (g_menuScreen == MenuScreen::Library) { // scroll the grid's rows, if they overflow
            LibraryLayout L = ComputeLibraryLayout(g_screenW, g_screenH, g_placeableList.count);
            g_libScroll += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
            g_libScroll = std::max(0, std::min(g_libScroll, L.rows - L.visibleRows));
            return 0;
        }
        // Cycle the hotbar's slots.
        if (g_menuScreen == MenuScreen::None && g_gameState == GameState::InGame) {
            int step = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
            g_player.hotbarIndex = (g_player.hotbarIndex + step + HOTBAR_SLOTS) % HOTBAR_SLOTS;
            WorldSoundCue(SND_SLOT, g_player.hotbarIndex);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        g_mouseButtonDown[0] = true;
        int mx = (int)(short)LOWORD(lParam), my = (int)(short)HIWORD(lParam);
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_LEFT; g_rebindingAction = -1; SaveSettings(); return 0; }
        if (g_menuScreen != MenuScreen::None) {
            if (PressActsImmediately(mx, my)) { DispatchMenuClick(mx, my); return 0; }
            g_pressActive = true; g_pressX = mx; g_pressY = my; g_pressRectValid = false; // acts on release
            return 0;
        }
        if (!g_mouseCaptured) { CaptureMouseForPlay(); return 0; }
        ToggleMoveLatches(MOUSE_LEFT);
        FireBoundAction(MOUSE_LEFT);
        return 0;
    }
    case WM_LBUTTONUP:
        g_mouseButtonDown[0] = false;
        g_mapDragging = false;
        if (g_pressActive) {
            g_pressActive = false;
            int mx = (int)(short)LOWORD(lParam), my = (int)(short)HIWORD(lParam);
            // Released on the button it pressed (or on something that isn't a
            // drawn button): act, at the press point. Slid off: cancelled.
            if (g_menuScreen != MenuScreen::None && (!g_pressRectValid || PointInRect(mx, my, g_pressRect)))
                DispatchMenuClick(g_pressX, g_pressY);
            g_pressRectValid = false;
            return 0;
        }
        if (g_menuScreen == MenuScreen::Library) LibraryMouseUp((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam));
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
        ToggleMoveLatches(MOUSE_RIGHT);
        FireBoundAction(MOUSE_RIGHT);
        return 0;
    case WM_RBUTTONUP:
        g_mouseButtonDown[1] = false;
        return 0;
    case WM_MBUTTONDOWN:
        g_mouseButtonDown[2] = true;
        if (g_rebindingAction != -1) { g_keyBindings[g_rebindingAction] = MOUSE_MIDDLE; g_rebindingAction = -1; SaveSettings(); return 0; }
        ToggleMoveLatches(MOUSE_MIDDLE);
        FireBoundAction(MOUSE_MIDDLE);
        return 0;
    case WM_MBUTTONUP:
        g_mouseButtonDown[2] = false;
        return 0;
    case WM_KEYDOWN:
        if (wParam < 256) g_keyDown[wParam] = true;
        if (g_rebindingAction != -1) {
            // Escape cancels a rebind -- except on the Pause Menu row,
            // where Escape is that action's own natural key: treating it
            // as cancel there meant a Pause Menu moved off Escape could
            // never be put back without resetting every binding. Every
            // other key or mouse button commits as the new binding,
            // wherever it's pressed (including, e.g., on the Back row).
            if (wParam != VK_ESCAPE || g_rebindingAction == ACT_MENU) {
                g_keyBindings[g_rebindingAction] = (int)wParam;
                SaveSettings();
            }
            g_rebindingAction = -1;
            return 0;
        }
        // F3 toggles the profiler overlay (Part XVI), F11 fullscreen --
        // unless the player has bound that key to an action, in which case
        // the action wins and the toggle stays reachable from Display settings.
        if ((wParam == VK_F3 || wParam == VK_F7 || wParam == VK_F8 || wParam == VK_F11) && !(lParam & (1 << 30))) {
            bool bound = false;
            for (int a = 0; a < ACT_COUNT; a++) if (g_keyBindings[a] == (int)wParam) bound = true;
            if (!bound && wParam == VK_F3 && (GetKeyState(VK_CONTROL) & 0x8000)) { // Ctrl+F3: a 30 s performance report
                if (g_gameState == GameState::InGame && !ProfCapturing()) {
                    ProfStartCapture(30.0f, PerfReportHeader());
                    ShowToast("RECORDING PERFORMANCE FOR 30 S - PLAY AS USUAL", 3.0f);
                }
                return 0;
            }
            if (!bound && wParam == VK_F3) { g_showProfiler = !g_showProfiler; SaveSettings(); return 0; }
            if (!bound && wParam == VK_F11) { ToggleFullscreenSetting(); return 0; }
            if (!bound && wParam == VK_F7) { g_lineDebug = !g_lineDebug; return 0; } // The Line's test marker (not saved)
            if (!bound && wParam == VK_F8) {                                          // debug: next time of day
                if (g_gameState == GameState::InGame && g_menuScreen == MenuScreen::None) JumpToNextTimeOfDay();
                return 0;
            }
        }
        if (wParam >= '0' && wParam <= '9' && (g_menuScreen == MenuScreen::None || g_menuScreen == MenuScreen::Library)) {
            int slot = wParam == '0' ? 9 : (int)(wParam - '1'); // 1-9, then 0 for the tenth
            if (slot != g_player.hotbarIndex) WorldSoundCue(SND_SLOT, slot);
            g_player.hotbarIndex = slot;
            return 0;
        }
        // Toggle-to-move (Accessibility, Section 11): genuine presses only --
        // bit 30 of lParam marks Windows' own key-repeat, which would
        // otherwise flip the latch back and forth while the key is held.
        if (!(lParam & (1 << 30))) ToggleMoveLatches((int)wParam);
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
        g_mapDragging = false;
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

// Draws a run of UI vertices through the UI pipeline, splitting it into
// pieces that fit g_uiVB (whole quads only) rather than truncating --
// a dense menu like Keybindings can exceed one buffer's worth of text.
static void UIDrawBatch(const UIVertex* verts, size_t count, ID3D11ShaderResourceView* srv) {
    if (count == 0) return;
    const size_t PIECE = (UI_VB_CAPACITY / 6) * 6;
    UINT stride = sizeof(UIVertex), offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_uiVB, &stride, &offset);
    g_context->PSSetShaderResources(0, 1, &srv);
    for (size_t start = 0; start < count; start += PIECE) {
        size_t n = std::min(PIECE, count - start);
        D3D11_MAPPED_SUBRESOURCE mapped;
        g_context->Map(g_uiVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, verts + start, n * sizeof(UIVertex));
        g_context->Unmap(g_uiVB, 0);
        g_context->Draw((UINT)n, 0);
    }
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
        float cx = g_screenW / 2.0f, cy = g_screenH / 2.0f;
        UIDrawRect(glyphVerts, cx - 8, cy - 1, cx + 8, cy + 1, 1, 1, 1, 0.85f);
        UIDrawRect(glyphVerts, cx - 1, cy - 8, cx + 1, cy + 8, 1, 1, 1, 0.85f);
    }

    // Every placeable block is a plain textured cube, so all hotbar icons
    // sample the one block atlas and share a single batch (drawn between
    // the HUD and menu glyph runs -- see the end of this function).
    // The hotbar: ten slots the player fills from the block library (E).
    if (g_player.hotbarIndex < 0 || g_player.hotbarIndex >= HOTBAR_SLOTS) g_player.hotbarIndex = 0; // an older save's index
    std::vector<UIVertex> iconVerts;
    for (int i = 0; i < HOTBAR_SLOTS; i++) {
        UiRect sr = HotbarSlotRect(g_screenW, g_screenH, i);
        float x0 = sr.x0, x1 = sr.x1, y0 = sr.y0, y1 = sr.y1;
        bool selected = (i == g_player.hotbarIndex);
        if (selected) UIDrawRect(glyphVerts, x0 - 4, y0 - 4, x1 + 4, y1 + 4, 1.0f, 0.9f, 0.2f, 0.9f);
        UIDrawRect(glyphVerts, x0, y0, x1, y1, 0.12f, 0.12f, 0.12f, 0.75f);
        float iu0, iv0, iu1, iv1;
        IconRect(g_hotbar[i], iu0, iv0, iu1, iv1);
        // 32px icon = exactly half the 64px tile, so point sampling keeps
        // every other texel evenly instead of an irregular 64->36 pick.
        UIAddQuad(iconVerts, x0 + 8, y0 + 8, x1 - 8, y1 - 8, iu0, iv0, iu1, iv1, 1, 1, 1, 1);
        // The slot's key, small in the corner: 1-9, then 0.
        char key[2] = { (char)(i == 9 ? '0' : '1' + i), 0 };
        UIDrawText(glyphVerts, key, x0 + 3, y0 + 2, 0.5f, 1, 1, 1, 0.55f);
    }
    const float hbY0 = HotbarSlotRect(g_screenW, g_screenH, 0).y0;

    if (!menuIsOpen) {
        std::string name = g_blocks[g_hotbar[g_player.hotbarIndex]].name;
        for (char& ch : name) ch = ch == '_' ? ' ' : (char)toupper((unsigned char)ch); // "stone_slab" -> "STONE SLAB"
        float scale = 0.8f;
        float tw = UITextWidth(name, scale);
        UIDrawText(glyphVerts, name, (g_screenW - tw) / 2.0f, hbY0 - 26.0f, scale, 1, 1, 1, 0.9f);
    }

    if (!g_mouseCaptured && !menuIsOpen) {
        std::string hint = "CLICK TO PLAY";
        float scale = 1.3f;
        float tw = UITextWidth(hint, scale);
        UIDrawText(glyphVerts, hint, (g_screenW - tw) / 2.0f, g_screenH * 0.42f, scale, 1, 1, 1, 0.9f);
    }

    // Toggle-to-move indicator (Accessibility, Section 11): a small arrow
    // cross in the bottom-left corner, each arrow lit while that direction
    // is latched, so a still-active latch is never invisible. Static --
    // it changes only when the player presses something.
    if (g_toggleMovement && !menuIsOpen && g_gameState == GameState::InGame) {
        const float S = 26.0f, G = 3.0f, x0 = 16.0f;
        const float yTop = g_screenH - 16.0f - (3.0f * S + 2.0f * G);
        struct Cell { GameAction act; const char* glyph; float cx, cy; };
        const Cell cells[4] = {
            { ACT_FORWARD, "^", 1, 0 }, { ACT_LEFT, "<", 0, 1 },
            { ACT_RIGHT, ">", 2, 1 },   { ACT_BACK, "v", 1, 2 },
        };
        UIDrawText(glyphVerts, "TOGGLE MOVE", x0, yTop - 20.0f, 0.65f, 1, 1, 1, 0.75f);
        for (const Cell& c : cells) {
            float cx0 = x0 + c.cx * (S + G), cy0 = yTop + c.cy * (S + G);
            bool on = g_moveToggleLatch[c.act];
            if (on) UIDrawRect(glyphVerts, cx0, cy0, cx0 + S, cy0 + S, 1.0f, g_highContrastUI ? 0.9f : 0.8f, g_highContrastUI ? 0.0f : 0.2f, 0.95f);
            else UIDrawRect(glyphVerts, cx0, cy0, cx0 + S, cy0 + S, 0.08f, 0.08f, 0.08f, g_highContrastUI ? 0.9f : 0.55f);
            float gs = 0.8f;
            float gx = cx0 + (S - UITextWidth(c.glyph, gs)) / 2.0f, gy = cy0 + (S - UITextHeight(gs)) / 2.0f;
            if (on) UIDrawText(glyphVerts, c.glyph, gx, gy, gs, 0, 0, 0, 1);
            else UIDrawText(glyphVerts, c.glyph, gx, gy, gs, 0.7f, 0.7f, 0.7f, g_highContrastUI ? 1.0f : 0.8f);
        }
    }

    // High-contrast mode (Accessibility, Section 11) pushes every panel/
    // button/track toward the luminance extremes -- near-black
    // backgrounds, a strongly saturated hover/handle color -- rather
    // than the subtle gray-shade steps used otherwise. Text is already
    // white-on-dark in both modes, at effectively maximum contrast, so
    // only the fill colors below need to branch.
    // A mechanical key: a raised cap on a darker base. Hovered, the cap
    // brightens; pressed, it sinks into the base (its lit top edge gone, a
    // shadow along its top), the label going down with it; released, it
    // springs back and the button acts. All flat rectangles: free.
    auto drawRowButton = [&](const UIRect& r, const std::string& label, float scale = 1.0f) {
        bool hover = PointInRect(g_mouseX, g_mouseY, r);
        bool held = g_pressActive && PointInRect(g_pressX, g_pressY, r);
        if (held) { g_pressRect = r; g_pressRectValid = true; }
        bool pressed = held && hover;
        const float h = r.y1 - r.y0;
        const float depth = std::max(2.0f, std::min(5.0f, floorf(h * 0.12f)));
        const float sink = pressed ? depth - 1.0f : 0.0f;
        const float capY0 = r.y0 + sink, capY1 = r.y1 - depth + sink;
        float face, lip, base, shadow;
        if (g_highContrastUI) {
            face = pressed ? 0.75f : hover ? 0.9f : 0.06f; lip = hover ? 1.0f : 0.35f; base = 0.0f; shadow = 0.0f;
        } else {
            face = pressed ? 0.27f : hover ? 0.35f : 0.27f; lip = hover ? 0.50f : 0.40f; base = 0.16f; shadow = 0.13f;
        }
        float blue = g_highContrastUI ? (hover || pressed ? 0.1f : face) : 0.06f;
        if (!g_highContrastUI) UIDrawRect(glyphVerts, r.x0 - 1, r.y0 + sink - 1, r.x1 + 1, r.y1 + 1, 0.04f, 0.04f, 0.05f, 1); // outline
        UIDrawRect(glyphVerts, r.x0, r.y0 + depth, r.x1, r.y1, base, base, base + (g_highContrastUI ? 0.0f : 0.03f), 1); // the base / body
        UIDrawRect(glyphVerts, r.x0, capY0, r.x1, capY1, face, face, face + blue, 1);                                    // the cap
        if (pressed) UIDrawRect(glyphVerts, r.x0, capY0, r.x1, capY0 + 2.0f, shadow, shadow, shadow + 0.02f, 1);         // pressed in: shadow along the top
        else UIDrawRect(glyphVerts, r.x0, capY0, r.x1, capY0 + 2.0f, lip, lip, lip + (g_highContrastUI ? 0.0f : 0.05f), 1); // raised: a lit top edge
        UIDrawRect(glyphVerts, r.x0, capY1 - 1.0f, r.x1, capY1, base + 0.05f, base + 0.05f, base + 0.08f, 1);          // the cap's lower edge
        float lw = UITextWidth(label, scale);
        float tr = g_highContrastUI && (hover || pressed) ? 0.0f : 1.0f;
        UIDrawText(glyphVerts, label, r.x0 + ((r.x1 - r.x0) - lw) / 2.0f, capY0 + (capY1 - capY0 - UITextHeight(scale)) / 2.0f, scale, tr, tr, tr, 1);
    };
    // A slider row: label above, track+handle below. Value/range/label
    // text all come from the generic slider-by-ID lookups, so adding a
    // slider anywhere else only means adding cases there, not another
    // copy of this drawing code.
    auto drawSliderRow = [&](UIRect r, int sliderId) {
        UIDrawRect(glyphVerts, r.x0, r.y0, r.x1, r.y1, g_highContrastUI ? 0.03f : 0.16f, g_highContrastUI ? 0.03f : 0.16f, g_highContrastUI ? 0.03f : 0.19f, 1);
        UIDrawText(glyphVerts, GetSliderLabel(sliderId), r.x0 + 8, r.y0 + 2.0f, 0.8f, 1, 1, 1, 1);

        // A bead on a string: a taut thread across the track, brighter on the
        // side the bead has travelled, knotted at both ends; the bead is a
        // round (stacked-rectangle) disc with a lit crown, brighter under the
        // cursor and a touch bigger while it's being dragged.
        UIRect track = GetSliderTrackRect(r);
        SliderRange rng = GetSliderRange(sliderId);
        float t = (GetSliderValue(sliderId) - rng.minV) / (rng.maxV - rng.minV);
        float bx = track.x0 + t * (track.x1 - track.x0);
        float cy = floorf((track.y0 + track.y1) * 0.5f);
        bool hover = PointInRect(g_mouseX, g_mouseY, GetSliderHitRect(r));
        bool dragging = g_draggingSlider == sliderId;
        const bool hc = g_highContrastUI;
        float slack = hc ? 0.55f : 0.30f, taut = hc ? 1.0f : 0.62f;
        UIDrawRect(glyphVerts, track.x0, cy - 1, track.x1, cy + 1, slack, slack, slack + (hc ? 0.0f : 0.04f), 1); // the string
        UIDrawRect(glyphVerts, track.x0, cy - 1, bx, cy + 1, taut, taut * (hc ? 0.9f : 0.95f), hc ? 0.0f : taut * 0.8f, 1); // travelled
        for (float kx : { track.x0, track.x1 }) UIDrawRect(glyphVerts, kx - 2, cy - 3, kx + 2, cy + 3, slack, slack, slack, 1); // knots
        const float R = dragging ? 9.0f : 8.0f;
        float bead = hover || dragging ? 1.0f : (hc ? 0.95f : 0.82f);
        float br = bead, bg = bead * (hc ? 0.9f : 0.9f), bb = hc ? 0.0f : bead * 0.55f;
        for (int k = 0; k < 8; k++) { // a disc in eight horizontal slices
            float y0 = cy - R + k * (2 * R / 8), y1 = y0 + 2 * R / 8;
            float yc = (y0 + y1) * 0.5f - cy;
            float half = sqrtf(std::max(0.0f, R * R - yc * yc));
            float shade = k < 2 ? 1.12f : k > 5 ? 0.78f : 1.0f; // lit crown, shaded underside
            UIDrawRect(glyphVerts, bx - half, y0, bx + half, y1, std::min(1.0f, br * shade), std::min(1.0f, bg * shade), std::min(1.0f, bb * shade), 1);
        }
        UIDrawRect(glyphVerts, bx - R * 0.45f, cy - R * 0.7f, bx - R * 0.05f, cy - R * 0.35f, 1, 1, hc ? 0.6f : 0.95f, hc ? 0.9f : 0.55f); // a glint
    };
    auto drawPanelTitle = [&](const UIRect& panel, float panelW, const char* title, float scale) {
        float tw = UITextWidth(title, scale);
        UIDrawText(glyphVerts, title, panel.x0 + (panelW - tw) / 2.0f, panel.y0 + 16.0f, scale, 1, 1, 1, 1);
    };
    auto drawPanelBg = [&](const UIRect& panel) {
        if (g_highContrastUI) UIDrawRect(glyphVerts, panel.x0, panel.y0, panel.x1, panel.y1, 0.0f, 0.0f, 0.0f, 0.98f);
        else UIDrawRect(glyphVerts, panel.x0, panel.y0, panel.x1, panel.y1, 0.10f, 0.10f, 0.13f, 0.95f);
    };

    // Everything before this point is HUD; menus, FPS and toasts after it
    // must draw over the hotbar icons, which are a separate texture batch.
    size_t hudVertCount = glyphVerts.size();

    // A flat, even dim behind any menu (uniform to the screen edges now
    // that UIDrawRect no longer fades its borders -- no vignette).
    if (g_menuScreen != MenuScreen::None && g_menuScreen != MenuScreen::Map && g_menuScreen != MenuScreen::Library) {
        UIDrawRect(glyphVerts, 0, 0, (float)g_screenW, (float)g_screenH, 0, 0, 0, 0.45f);
    }

    // Block library (library.h): the dim stops short of the hotbar, which
    // stays bright as the drop target. Its icons and the one being dragged
    // go in their own batches after the menu glyphs, so the panel can't
    // cover them.
    std::vector<UIVertex> libIconVerts, dragIconVerts;
    if (g_menuScreen == MenuScreen::Library) {
        const int count = g_placeableList.count;
        LibraryLayout L = ComputeLibraryLayout(g_screenW, g_screenH, count);
        g_libScroll = std::max(0, std::min(g_libScroll, L.rows - L.visibleRows));
        float hotbarTop = HotbarSlotRect(g_screenW, g_screenH, 0).y0 - 8.0f;
        UIDrawRect(glyphVerts, 0, 0, (float)g_screenW, hotbarTop, 0, 0, 0, 0.45f);
        UIDrawRect(glyphVerts, L.panel.x0, L.panel.y0, L.panel.x1, L.panel.y1, 0.10f, 0.10f, 0.13f, 0.95f);
        UIDrawText(glyphVerts, "BLOCK LIBRARY", L.panel.x0 + 16, L.panel.y0 + 12, 1.0f, 0.9f, 0.9f, 1.0f, 1.0f);
        int hover = LibraryCellAt(L, count, g_libScroll, (float)g_mouseX, (float)g_mouseY);
        for (int i = g_libScroll * L.columns; i < std::min(count, (g_libScroll + L.visibleRows) * L.columns); i++) {
            UiRect c = LibraryCellRect(L, g_libScroll, i);
            bool hot = i == hover || i == g_libGesture.pressed;
            UIDrawRect(glyphVerts, c.x0 + 3, c.y0 + 3, c.x1 - 3, c.y1 - 3, hot ? 0.30f : 0.16f, hot ? 0.28f : 0.16f, hot ? 0.18f : 0.19f, 0.95f);
            float iu0, iv0, iu1, iv1;
            IconRect(g_placeableList.ids[i], iu0, iv0, iu1, iv1);
            UIAddQuad(libIconVerts, c.x0 + 12, c.y0 + 12, c.x1 - 12, c.y1 - 12, iu0, iv0, iu1, iv1, 1, 1, 1, 1);
        }
        if (L.rows > L.visibleRows) {
            char more[48]; snprintf(more, sizeof(more), "ROWS %d-%d OF %d (WHEEL)", g_libScroll + 1, g_libScroll + L.visibleRows, L.rows);
            UIDrawText(glyphVerts, more, L.panel.x1 - 16 - UITextWidth(more, 0.6f), L.panel.y0 + 16, 0.6f, 0.7f, 0.7f, 0.8f, 0.9f);
        }
        int named = g_libGesture.pressed >= 0 ? g_libGesture.pressed : hover;
        std::string label = named >= 0 ? g_blocks[g_placeableList.ids[named]].name
                                       : "CLICK A BLOCK TO USE IT - DRAG IT ONTO A SLOT TO KEEP IT";
        for (char& ch : label) ch = ch == '_' ? ' ' : (char)toupper((unsigned char)ch);
        float ls = named >= 0 ? 0.85f : 0.6f;
        UIDrawText(glyphVerts, label, (g_screenW - UITextWidth(label, ls)) / 2.0f, L.panel.y1 + 6, ls, 1, 1, 1, 0.9f);
        // The dragged block follows the cursor; the slot it would land in lights up.
        if (g_libGesture.dragging && g_libGesture.pressed >= 0) {
            int slot = HotbarSlotAt(g_screenW, g_screenH, (float)g_mouseX, (float)g_mouseY);
            if (slot >= 0) {
                UiRect sr = HotbarSlotRect(g_screenW, g_screenH, slot);
                UIDrawRect(glyphVerts, sr.x0 - 4, sr.y0 - 4, sr.x1 + 4, sr.y1 + 4, 0.4f, 0.9f, 1.0f, 0.6f);
            }
            float iu0, iv0, iu1, iv1;
            IconRect(g_placeableList.ids[g_libGesture.pressed], iu0, iv0, iu1, iv1);
            float mx = (float)g_mouseX, my = (float)g_mouseY;
            UIAddQuad(dragIconVerts, mx - 16, my - 16, mx + 16, my + 16, iu0, iv0, iu1, iv1, 1, 1, 1, 0.9f);
        }
    }

    // A store's contents (Part VI): one slot per spin -- none, clockwise,
    // anticlockwise -- each a heap of beads in that pulse's colour that
    // grows with the amount (one more bead each time it doubles), marked
    // with a swirl turning its way. No numbers: the heap is the reading.
    if (g_menuScreen == MenuScreen::Store) {
        BlockID sb = g_world.Get(g_storeX, g_storeY, g_storeZ);
        if (PulseCapacity(sb) <= 0) { g_menuScreen = MenuScreen::None; CaptureMouseForPlay(); } // it's gone
        PulseCounts held = PulseHeld(g_world, g_storeX, g_storeY, g_storeZ);
        const float slotW = 150.0f, gap = 24.0f, pw = 3 * slotW + 4 * gap, ph = 250.0f;
        float px0 = (g_screenW - pw) / 2.0f, py0 = (g_screenH - ph) / 2.0f;
        UIDrawRect(glyphVerts, px0, py0, px0 + pw, py0 + ph, 0.10f, 0.10f, 0.13f, 0.95f);
        std::string title = g_blocks[sb].name;
        for (char& ch : title) ch = ch == '_' ? ' ' : (char)toupper((unsigned char)ch);
        UIDrawText(glyphVerts, title, px0 + (pw - UITextWidth(title, 1.0f)) / 2.0f, py0 + 14.0f, 1.0f, 0.95f, 0.92f, 0.85f, 1.0f);
        const float colours[3][3] = { { 1.0f, 0.80f, 0.36f }, { 0.22f, 0.42f, 1.0f }, { 1.0f, 0.20f, 0.18f } }; // plain, clockwise, anticlockwise: as in the pipes
        auto bead = [&](float cx, float cy, float r, const float* c, float a) {
            // A faceted bead, like the ones in the pipes: a diamond, lit from above.
            const int rows = 6;
            for (int k = 0; k < rows; k++) {
                float y0 = cy - r + k * (2 * r / rows), y1 = y0 + 2 * r / rows;
                float mid = (y0 + y1) * 0.5f - cy;
                float half = r - fabsf(mid);
                float shade = k < rows / 2 ? 1.0f : 0.72f;
                UIDrawRect(glyphVerts, cx - half, y0, cx + half, y1, c[0] * shade, c[1] * shade, c[2] * shade, a);
            }
            UIDrawRect(glyphVerts, cx - r * 0.35f, cy - r * 0.55f, cx - r * 0.05f, cy - r * 0.25f, 1, 1, 1, 0.7f * a); // a glint
        };
        for (int k = 0; k < 3; k++) {
            float sx0 = px0 + gap + k * (slotW + gap), sy0 = py0 + 56.0f, sy1 = sy0 + slotW;
            int n = held.n[k];
            UIDrawRect(glyphVerts, sx0, sy0, sx0 + slotW, sy1, n ? 0.16f : 0.12f, n ? 0.16f : 0.12f, n ? 0.19f : 0.14f, 0.95f);
            const float* c = colours[k];
            // The swirl: a small spiral of dots turning the pulse's way, ending in a larger one.
            if (k > 0) {
                float cx = sx0 + slotW - 26.0f, cy = sy0 + 24.0f, dir = k == 1 ? 1.0f : -1.0f;
                for (int i = 0; i <= 12; i++) {
                    float th = i / 12.0f * 5.0f, rr = 3.0f + i * 1.1f, d = i == 12 ? 3.5f : 1.8f;
                    float x = cx + dir * cosf(th - 1.57f) * rr, y = cy + sinf(th - 1.57f) * rr;
                    UIDrawRect(glyphVerts, x - d, y - d, x + d, y + d, c[0], c[1], c[2], n ? 0.95f : 0.35f);
                }
            }
            if (n <= 0) continue;
            int beads = 1;
            for (int v = n; v > 1 && beads < 12; v >>= 1) beads++;
            // A heap: rows of 4, 3, 3, 2 from the bottom.
            const int rowCap[4] = { 4, 3, 3, 2 };
            const float r = 11.0f;
            int placed = 0;
            for (int row = 0; row < 4 && placed < beads; row++) {
                int inRow = std::min(rowCap[row], beads - placed);
                float y = sy1 - 18.0f - row * (1.55f * r);
                float x0 = sx0 + slotW / 2.0f - (inRow - 1) * r;
                for (int i = 0; i < inRow; i++) bead(x0 + i * 2.0f * r, y, r, c, 1.0f);
                placed += inRow;
            }
        }
        std::string hint = "ESC TO CLOSE";
        UIDrawText(glyphVerts, hint, px0 + (pw - UITextWidth(hint, 0.6f)) / 2.0f, py0 + ph - 26.0f, 0.6f, 0.7f, 0.7f, 0.8f, 0.9f);
    }

    // Essence network map (Part XIX): the draw list is plain coloured
    // triangles plus label requests, batched through the same white-texel
    // quads and crisp text every menu uses.
    if (g_menuScreen == MenuScreen::Map) {
        g_mapCamera.screenW = g_screenW;
        g_mapCamera.screenH = g_screenH;
        float animTime = (float)(GetTickCount64() % 1000000ull) / 1000.0f;
        MapDrawList dl;
        BuildMapDrawList(g_essence, g_mapCamera, MapTuning(), animTime,
                         g_player.x, g_player.z, g_player.yaw, g_line.pivotX, g_line.pivotZ,
                         UITextWidth, UITextHeight(0.65f), dl);
        float wu = 0.5f * UI_WHITE_H / UIAtlasWidth(), wv = 0.5f * UI_WHITE_H / UIAtlasHeight();
        for (size_t i = 0; i + 5 < dl.tris.size(); i += 6) {
            const float* v = &dl.tris[i];
            glyphVerts.push_back({ v[0], v[1], wu, wv, v[2], v[3], v[4], v[5] });
        }
        for (const MapLabel& l : dl.labels) UIDrawText(glyphVerts, l.text, l.x, l.y, l.scale, l.r, l.g, l.b, l.a);
        UIDrawText(glyphVerts, "ESSENCE NETWORK", 16.0f, 14.0f, 1.0f, 0.85f, 0.82f, 1.0f, 1.0f);
        std::string hint = "DRAG TO PAN - WHEEL TO ZOOM - " + GetInputDisplayName(g_keyBindings[ACT_MAP]) + " OR ESC TO CLOSE";
        UIDrawText(glyphVerts, hint, 16.0f, (float)g_screenH - 16.0f - UITextHeight(0.65f), 0.65f, 0.7f, 0.7f, 0.8f, 0.9f);
        if (g_essence.Nodes().empty()) {
            std::string empty = "NO ESSENCE DISCOVERED YET";
            UIDrawText(glyphVerts, empty, (g_screenW - UITextWidth(empty, 1.0f)) / 2.0f, g_screenH * 0.42f, 1.0f, 0.75f, 0.72f, 0.9f, 1.0f);
        }
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
                if (g_gameState == GameState::InGame && slot == g_currentSlot) label += " (CURRENT)";
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
        drawSliderRow(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_FRAME_LIMIT), SLIDER_FRAME_LIMIT);
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_VSYNC), g_vsync ? "VSYNC: ON" : "VSYNC: OFF");
        // An effect whose shader didn't compile here says so (shader_errors.txt
        // has the details) instead of a toggle that silently does nothing.
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_SHADOWS), !ShadowsAvailable() ? "SUN SHADOWS: UNAVAILABLE" : g_shadows ? "SUN SHADOWS: ON" : "SUN SHADOWS: OFF");
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_OUTLINES), !PostEffectsAvailable() ? "EDGE OUTLINES: UNAVAILABLE" : g_postEdges ? "EDGE OUTLINES: ON" : "EDGE OUTLINES: OFF");
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_SSAO), !PostEffectsAvailable() ? "SCREEN-SPACE AO: UNAVAILABLE" : g_postSSAO ? "SCREEN-SPACE AO: ON" : "SCREEN-SPACE AO: OFF");
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BLOOM), !BloomAvailable() ? "GLOW (BLOOM): UNAVAILABLE" : g_bloom ? "GLOW (BLOOM): ON" : "GLOW (BLOOM): OFF");
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(GRAPHICS_LAYOUT, GROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Display) {
        UIRect panel = SubmenuPanelRect(DISPLAY_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, DISPLAY_LAYOUT.panelW, "DISPLAY SETTINGS", 1.0f);

        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_FPS), g_showFPS ? "SHOW FPS COUNTER: ON" : "SHOW FPS COUNTER: OFF");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_SHOW_PROFILER), g_showProfiler ? "PROFILER (F3): ON" : "PROFILER (F3): OFF");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_FULLSCREEN), g_fullscreen ? "FULLSCREEN (F11): ON" : "FULLSCREEN (F11): OFF");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(DISPLAY_LAYOUT, DROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Audio) {
        UIRect panel = SubmenuPanelRect(AUDIO_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, AUDIO_LAYOUT.panelW, "AUDIO SETTINGS", 1.0f);

        drawSliderRow(SubmenuRowRect(AUDIO_LAYOUT, AROW_MASTER_VOLUME), SLIDER_MASTER_VOLUME);
        drawSliderRow(SubmenuRowRect(AUDIO_LAYOUT, AROW_MUSIC_VOLUME), SLIDER_MUSIC_VOLUME);
        drawSliderRow(SubmenuRowRect(AUDIO_LAYOUT, AROW_WORLD_VOLUME), SLIDER_WORLD_VOLUME);
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
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_MONO), g_monoAudio ? "MONO AUDIO: ON" : "MONO AUDIO: OFF");
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_RESET), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(ACCESSIBILITY_LAYOUT, ARROW_BACK), "BACK");
    } else if (g_menuScreen == MenuScreen::Keybindings) {
        UIRect panel = SubmenuPanelRect(KEYBIND_LAYOUT);
        drawPanelBg(panel);
        drawPanelTitle(panel, KEYBIND_LAYOUT.panelW, "KEYBINDINGS", 1.0f);
        std::string hint = "CLICK A ROW, THEN PRESS THE NEW INPUT";
        UIDrawText(glyphVerts, hint, panel.x0 + (KEYBIND_LAYOUT.panelW - UITextWidth(hint, 0.65f)) / 2.0f, panel.y0 + 44.0f, 0.65f, 0.8f, 0.8f, 0.8f, 0.8f);

        for (int i = 0; i < ACT_COUNT; i++) {
            std::string label;
            if (g_rebindingAction == i) label = std::string(g_actionLabels[i]) + (i == ACT_MENU ? ": PRESS INPUT (ESC = ESCAPE)" : ": PRESS INPUT (ESC CANCELS)");
            else label = std::string(g_actionLabels[i]) + ": [" + GetInputDisplayName(g_keyBindings[i]) + "]";
            drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, i), label, 0.7f);
        }
        drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT), "RESET TO DEFAULT");
        drawRowButton(SubmenuRowRect(KEYBIND_LAYOUT, ACT_COUNT + 1), "BACK");
    }

    if (ProfCapturing()) { // Ctrl+F3 recording: a quiet countdown, bottom left
        char buf[48];
        snprintf(buf, sizeof(buf), "RECORDING PERFORMANCE %d", (int)ceilf(ProfCaptureSecondsLeft()));
        UIDrawText(glyphVerts, buf, 12.0f, g_screenH - 30.0f, 0.6f, 1.0f, 0.55f, 0.45f, 0.9f);
    }
    if (g_showFPS) {
        char buf[32];
        snprintf(buf, sizeof(buf), "FPS: %d", g_fpsDisplay);
        UIDrawText(glyphVerts, buf, 12.0f, 12.0f, 0.9f, 1, 1, 0.6f, 0.9f);
    }

    // Profiler overlay (Part XVI): per-system CPU ms, average and worst
    // over the last ~2 s, then load counters. Monospace, so printf
    // padding lines the columns up.
    if (g_showProfiler) {
        const ProfReport& r = ProfGetReport();
        const float scale = 0.65f, lineH = UITextHeight(scale);
        std::vector<std::string> lines;
        char buf[96];
        snprintf(buf, sizeof(buf), "%-15s %6s %6s", "MS", "AVG", "WORST"); lines.push_back(buf);
        snprintf(buf, sizeof(buf), "%-15s %6.2f %6.2f", "FRAME", r.frameAvgMs, r.frameMaxMs); lines.push_back(buf);
        snprintf(buf, sizeof(buf), "%-15s %6.2f %6.2f", "WORK (NO VSYNC)", r.workAvgMs, r.workMaxMs); lines.push_back(buf);
        for (int i = 0; i < PROF_COUNT; i++) {
            snprintf(buf, sizeof(buf), "  %-13s %6.2f %6.2f", ProfSectionName((ProfSection)i), r.avgMs[i], r.maxMs[i]);
            lines.push_back(buf);
        }
        lines.push_back("");
        snprintf(buf, sizeof(buf), "%-15s %6s %6s", "COUNT", "NOW", "PEAK"); lines.push_back(buf);
        for (int i = 0; i < PCOUNT_COUNT; i++) {
            snprintf(buf, sizeof(buf), "%-15s %6lld %6lld", ProfCounterName((ProfCounter)i),
                     (long long)r.counters[i], (long long)r.countersMax[i]);
            lines.push_back(buf);
        }
        lines.push_back("");
        lines.push_back(ProfBootSummary(false)); // how long start-up took, and on what
        if (g_gameState == GameState::InGame) {
            // Pulse logistics (Part VI): totals, and what's held by the block in view.
            snprintf(buf, sizeof(buf), "PULSE %d HARVESTING %d MOVING %lld IN %lld LOST",
                     g_pulse.Harvesters(), g_pulse.InFlight(), g_pulse.delivered, g_pulse.lost);
            lines.push_back(buf);
            Vec3 f, r, u;
            GetCameraVectors(g_player, f, r, u);
            int hx, hy, hz, px, py, pz;
            if (Raycast(g_world, g_player.x, g_player.y + g_player.eyeHeight, g_player.z, f.x, f.y, f.z, 6.0f, hx, hy, hz, px, py, pz)) {
                BlockID b = g_world.Get(hx, hy, hz);
                if (PulseCapacity(b) > 0) {
                    PulseCounts h = PulseHeld(g_world, hx, hy, hz);
                    snprintf(buf, sizeof(buf), "  HOLDS %d PLAIN %d CW %d CCW", h.n[0], h.n[1], h.n[2]);
                    lines.push_back(buf);
                }
            }
        }
        // The world sound palette's three axes (docs/SOUND_PALETTE.md 3).
        if (g_gameState == GameState::InGame) {
            SoundAxes ax = WorldSoundAxes();
            auto bar = [](float v01) {
                std::string b(11, '-');
                int i = (int)(v01 * 10.0f + 0.5f);
                b[i < 0 ? 0 : i > 10 ? 10 : i] = '#';
                return b;
            };
            lines.push_back("");
            lines.push_back(std::string("FACING ") + CompassPoint(g_player.yaw));
            lines.push_back("SOUNDSCAPE");
            lines.push_back("  NEGATIVE " + bar(0.5f + 0.5f * ax.positive) + " POSITIVE");
            lines.push_back("  CALM     " + bar(ax.activity) + " ACTIVE");
            lines.push_back("  ORGANIC  " + bar(ax.mechanical) + " MECHANICAL");
        }
        float x = 12.0f, y = g_showFPS ? 44.0f : 12.0f;
        float w = 0;
        for (const std::string& l : lines) w = std::max(w, UITextWidth(l, scale));
        UIDrawRect(glyphVerts, x - 6, y - 4, x + w + 6, y + lines.size() * lineH + 4, 0, 0, 0, 0.6f);
        for (const std::string& l : lines) {
            UIDrawText(glyphVerts, l, x, y, scale, 0.85f, 1.0f, 0.85f, 1.0f);
            y += lineH;
        }
    }

    // The Line's debug readout (F7), top right: a testing aid, not UI.
    if (g_lineDebug && g_gameState == GameState::InGame) {
        const LineState& L = g_line;
        char buf[96];
        std::vector<std::string> lines;
        lines.push_back("THE LINE (F7 DEBUG)");
        snprintf(buf, sizeof(buf), "DISTANCE %6.1f  (%4.1f PER DECADE)", L.distance, L.blocksPerDecade); lines.push_back(buf);
        snprintf(buf, sizeof(buf), "INTENSITY %.4f", L.intensity); lines.push_back(buf);
        snprintf(buf, sizeof(buf), "SPIN %s  ALIGN %+.2f", L.spin > 0 ? "COUNTERCLOCKWISE" : "CLOCKWISE", L.alignment); lines.push_back(buf);
        snprintf(buf, sizeof(buf), "PIVOT %.0f, %.0f  ANGLE %.0f", L.pivotX, L.pivotZ, L.theta * 57.29578f); lines.push_back(buf);
        snprintf(buf, sizeof(buf), "SKY CLOCK X%.2f  %.0f S AHEAD", L.skyRate, L.skyLead); lines.push_back(buf);
        const float scale = 0.65f, lineH = UITextHeight(scale);
        float w = 0;
        for (const std::string& l : lines) w = std::max(w, UITextWidth(l, scale));
        float x = g_screenW - w - 12.0f, y = 12.0f;
        UIDrawRect(glyphVerts, x - 6, y - 4, x + w + 6, y + lines.size() * lineH + 4, 0, 0, 0, 0.6f);
        for (const std::string& l : lines) { UIDrawText(glyphVerts, l, x, y, scale, 0.75f, 0.95f, 1.0f, 1.0f); y += lineH; }
    }

    // Transient save/load confirmation -- fades over its last half
    // second so it doesn't just vanish abruptly.
    if (g_toastTimer > 0.0f) {
        float alpha = g_toastTimer < 0.5f ? g_toastTimer / 0.5f : 1.0f;
        float scale = 1.1f;
        float tw = UITextWidth(g_toastMessage, scale);
        UIDrawText(glyphVerts, g_toastMessage, (g_screenW - tw) / 2.0f, 40.0f, scale, 1.0f, 0.95f, 0.55f, alpha);
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
        f[0] = (float)g_screenW; f[1] = (float)g_screenH; f[2] = 0; f[3] = 0;
        g_context->Unmap(g_uiCBuffer, 0);
    }

    UIDrawBatch(glyphVerts.data(), hudVertCount, g_uiSRV);
    UIDrawBatch(iconVerts.data(), iconVerts.size(), g_iconSRV);
    UIDrawBatch(glyphVerts.data() + hudVertCount, glyphVerts.size() - hudVertCount, g_uiSRV);
    if (!libIconVerts.empty()) UIDrawBatch(libIconVerts.data(), libIconVerts.size(), g_iconSRV);
    if (!dragIconVerts.empty()) UIDrawBatch(dragIconVerts.data(), dragIconVerts.size(), g_iconSRV);

    // Restore world-pass defaults so next frame's world draws don't
    // inherit UI blend/depth state.
    g_context->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_depthState, 0);
}
