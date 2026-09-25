// Drillder_proto_1.cpp
// Drillder voxel slice prototype (final version)
// - Correct yaw (fixed)
// - Pitch Mode A (continuous rotation) implemented
// - Headers drawn inside each slice
// - DEBUG quadrant bottom-right showing position & dir
// - Unicode-safe (escaped \u2022 and \u25CB)
// + TYPE 5 ADDED: permanent non-obstacle decoration (blue stripes)

#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// -------------------- Configuration --------------------
constexpr int CELL_PX = 32;
constexpr int VISIBLE = 15; // must be odd
static_assert(VISIBLE % 2 == 1, "VISIBLE must be odd");
constexpr int QUAD_PX = CELL_PX * VISIBLE;
constexpr int WINDOW_PX = QUAD_PX * 2;

constexpr int WORLD_SIZE_X = 64;
constexpr int WORLD_SIZE_Y = 64;
constexpr int WORLD_SIZE_Z = 32;

// -------------------- Types --------------------
// <<< MODIFIED: added type + durability to Voxel (minimal intrusion) >>>
struct Voxel {
    int type;
    bool isObstacle;
    int durability; // new: only used for obstacles like type 4
    Voxel(int t = -1, bool obs = false, int d = 0) : type(t), isObstacle(obs), durability(d) {}
};

static std::vector<Voxel> world;

inline int WorldIndex(int x, int y, int z) {
    return (z * WORLD_SIZE_Y + y) * WORLD_SIZE_X + x;
}

struct Player3 {
    int x, y, z;
    int dirX, dirY, dirZ;   // axis-aligned unit vector
};

static Player3 player3;

// -------------------- Movement / Obstacle config (NEW globals) --------------------
// <<< NEW: minimal-intrusion globals for continuous movement, digging, inventory >>>
static bool movingForward = false;
static bool movingBackward = false;
static int MOVE_DELAY_MS = 160;         // ms between grid steps while holding forward/back
static int moveTimerAccumMs = 0;        // accumulator
static int DIG_PER_TICK = 1;            // durability removed per attempted advance tick
static int DEFAULT_OBSTACLE_DUR = 6;    // default durability for placed obstacle type 4

// inventory: simple vector of (type,count)
struct InventoryEntry { int type; int count; };
static std::vector<InventoryEntry> playerInventory;
static int currentPlaceType = 4; // defaults to type 4 for placement selection

// helper to add to inventory (idempotent addition)
void AddToInventory(int type, int amount = 1) {
    for (auto& e : playerInventory) {
        if (e.type == type) { e.count += amount; return; }
    }
    playerInventory.push_back({ type, amount });
}

// <<< NEW: gravity globals >>>
static int gravityDirX = 0;
static int gravityDirY = 0;
static int gravityDirZ = -1; // starting down (negative Z)
static int GRAVITY_DELAY_MS = 160;
static int gravityTimerAccumMs = 0;

// <<< ONLY ADDITION: forward declarations so InitWorldAndPlayer can see these functions >>>
void PlaceVoxelAt(int wx, int wy, int wz, int type);
void RemoveVoxelAt(int wx, int wy, int wz);

// -------------------- GDI+ backbuffer --------------------
ULONG_PTR gdiToken = 0;
HDC hdcMem = nullptr;
HBITMAP hbmMem = nullptr;
HBITMAP hbmOld = nullptr;

Font* gHeaderFont = nullptr;
Font* gIdFont = nullptr;
Font* gDebugFont = nullptr;
SolidBrush* gHeaderBrush = nullptr;
SolidBrush* gIdBrush = nullptr;
SolidBrush* gCellBgBrush = nullptr;
SolidBrush* gPlayerBrush = nullptr;
SolidBrush* gDebugBrush = nullptr;
Pen* gGridPen = nullptr;

// <<< NEW: procedural texture for block type 4 >>>
static Bitmap* patternBlockType4 = nullptr;
// <<< ADDED: procedural texture for type 5 (permanent decoration) >>>
static Bitmap* patternBlockType5 = nullptr;

Bitmap* CreateSimpleType4Pattern(int size) {
    // Simple procedural dot/texture inspired by LGR.cpp patterns (kept small & fast)
    Bitmap* bmp = new Bitmap(size, size, PixelFormat32bppARGB);
    Graphics gr(bmp);
    SolidBrush bg(Color(255, 40, 20, 20));
    SolidBrush fg(Color(255, 220, 80, 80));
    gr.FillRectangle(&bg, 0, 0, size, size);
    int step = max(2, size / 6);
    for (int y = 0; y < size; y += step) {
        for (int x = 0; x < size; x += step) {
            if (((x / step) + (y / step)) % 3 == 0) {
                gr.FillRectangle(&fg, x, y, 1, 1);
            }
        }
    }
    return bmp;
}

// <<< ADDED: blue diagonal striped pattern for type 5 >>>
Bitmap* CreateSimpleType5Pattern(int size) {
    Bitmap* bmp = new Bitmap(size, size, PixelFormat32bppARGB);
    Graphics gr(bmp);
    SolidBrush bg(Color(255, 15, 30, 90));     // deep blue
    SolidBrush fg(Color(255, 80, 160, 255));   // bright cyan-blue stripes
    gr.FillRectangle(&bg, 0, 0, size, size);
    int step = size / 7;
    for (int i = -size; i < size * 2; i += step) {
        int x1 = i;
        int y1 = 0;
        int x2 = i + size;
        int y2 = size;
        if (x1 < 0) { y1 = -x1; x1 = 0; }
        if (x2 > size) { y2 = size - (x2 - size); x2 = size; }
        gr.FillRectangle(&fg, x1, y1, step * 2, y2 - y1);
    }
    return bmp;
}

// -------------------- Init / Cleanup --------------------
void InitWorldAndPlayer() {
    world.assign(WORLD_SIZE_X * WORLD_SIZE_Y * WORLD_SIZE_Z, Voxel());
    player3.x = WORLD_SIZE_X / 2;
    player3.y = WORLD_SIZE_Y / 2;
    player3.z = WORLD_SIZE_Z / 2;
    player3.dirX = 1; player3.dirY = 0; player3.dirZ = 0;

    // <<< NEW: starter ground layer of type 4 one level below player >>>
    int groundZ = player3.z - 1;
    if (groundZ >= 0 && groundZ < WORLD_SIZE_Z) {
        for (int x = 0; x < WORLD_SIZE_X; ++x) {
            for (int y = 0; y < WORLD_SIZE_Y; ++y) {
                PlaceVoxelAt(x, y, groundZ, 4);
            }
        }
    }
}

void InitGdi(HWND hwnd) {
    if (!gdiToken) {
        GdiplusStartupInput si;
        GdiplusStartup(&gdiToken, &si, nullptr);
    }

    HDC hdc = GetDC(hwnd);
    hdcMem = CreateCompatibleDC(hdc);
    hbmMem = CreateCompatibleBitmap(hdc, WINDOW_PX, WINDOW_PX);
    hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
    ReleaseDC(hwnd, hdc);

    gHeaderFont = new Font(L"Consolas", 12, FontStyleBold, UnitPixel);
    gIdFont = new Font(L"Consolas", 10, FontStyleRegular, UnitPixel);
    gDebugFont = new Font(L"Consolas", 11, FontStyleRegular, UnitPixel);

    gHeaderBrush = new SolidBrush(Color(255, 220, 220, 220));
    gIdBrush = new SolidBrush(Color(255, 240, 240, 240));
    gCellBgBrush = new SolidBrush(Color(255, 30, 30, 30));
    gPlayerBrush = new SolidBrush(Color(255, 255, 80, 80));
    gDebugBrush = new SolidBrush(Color(255, 200, 200, 200));
    gGridPen = new Pen(Color(255, 60, 60, 60));

    // <<< NEW: create procedural pattern for block type 4 >>>
    if (!patternBlockType4) patternBlockType4 = CreateSimpleType4Pattern(CELL_PX);
    // <<< ADDED: create pattern for type 5 >>>
    if (!patternBlockType5) patternBlockType5 = CreateSimpleType5Pattern(CELL_PX);
}

void CleanupGdi() {
    if (hdcMem) {
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        hdcMem = nullptr;
        hbmMem = nullptr;
        hbmOld = nullptr;
    }

    delete gHeaderFont; gHeaderFont = nullptr;
    delete gIdFont;     gIdFont = nullptr;
    delete gDebugFont;  gDebugFont = nullptr;

    delete gHeaderBrush; gHeaderBrush = nullptr;
    delete gIdBrush;     gIdBrush = nullptr;
    delete gCellBgBrush; gCellBgBrush = nullptr;
    delete gPlayerBrush; gPlayerBrush = nullptr;
    delete gDebugBrush;  gDebugBrush = nullptr;
    delete gGridPen;     gGridPen = nullptr;

    // <<< NEW: cleanup procedural pattern >>>
    if (patternBlockType4) { delete patternBlockType4; patternBlockType4 = nullptr; }
    if (patternBlockType5) { delete patternBlockType5; patternBlockType5 = nullptr; }

    if (gdiToken) {
        GdiplusShutdown(gdiToken);
        gdiToken = 0;
    }
}

// -------------------- Orientation (corrected) --------------------
inline void SetDir(int x, int y, int z) {
    player3.dirX = x; player3.dirY = y; player3.dirZ = z;
}

// Yaw: rotate around Z (visual clockwise/counterclockwise)
void YawRight90() {
    int dx = player3.dirX, dy = player3.dirY, dz = player3.dirZ;
    SetDir(-dy, dx, dz); // clockwise visually
}

void YawLeft90() {
    int dx = player3.dirX, dy = player3.dirY, dz = player3.dirZ;
    SetDir(dy, -dx, dz); // counter-clockwise visually
}

// -------------------- Pitch Mode A (continuous flight-sim rotation) --------------------
void PitchUp90() {
    int dx = player3.dirX;
    int dy = player3.dirY;
    int dz = player3.dirZ;

    // Horizontal -> Up (positive Z)
    if (dx == 1) { SetDir(0, 0, 1); return; }
    if (dx == -1) { SetDir(0, 0, -1); return; }
    if (dy == 1) { SetDir(0, 0, 1); return; }
    if (dy == -1) { SetDir(0, 0, -1); return; }

    // Vertical -> Opposite horizontal
    if (dz == 1) { SetDir(-1, 0, 0); return; }
    if (dz == -1) { SetDir(1, 0, 0); return; }
}

void PitchDown90() {
    int dx = player3.dirX;
    int dy = player3.dirY;
    int dz = player3.dirZ;

    // Horizontal -> Down (negative Z)
    if (dx == 1) { SetDir(0, 0, -1); return; }
    if (dx == -1) { SetDir(0, 0, 1); return; }
    if (dy == 1) { SetDir(0, 0, -1); return; }
    if (dy == -1) { SetDir(0, 0, 1); return; }

    // Vertical -> Opposite horizontal
    if (dz == 1) { SetDir(1, 0, 0); return; }
    if (dz == -1) { SetDir(-1, 0, 0); return; }
}

// -------------------- Wrapping Helpers --------------------
int WrapX(int x) {
    return (x % WORLD_SIZE_X + WORLD_SIZE_X) % WORLD_SIZE_X;
}

int WrapY(int y) {
    return (y % WORLD_SIZE_Y + WORLD_SIZE_Y) % WORLD_SIZE_Y;
}

// -------------------- Movement --------------------
bool IsObstacle(int x, int y, int z) {
    x = WrapX(x);
    y = WrapY(y);
    if (z < 0 || z >= WORLD_SIZE_Z) return true;
    return world[WorldIndex(x, y, z)].isObstacle;
}

// <<< MODIFIED: TryMove now digs ONLY when moving FORWARD (matches player.dir) >>>
bool TryMove(int dx, int dy, int dz) {
    int nx = WrapX(player3.x + dx);
    int ny = WrapY(player3.y + dy);
    int nz = player3.z + dz;
    if (nz < 0 || nz >= WORLD_SIZE_Z) return false;

    Voxel& v = world[WorldIndex(nx, ny, nz)];
    if (!v.isObstacle) {
        player3.x = nx; player3.y = ny; player3.z = nz;
        return true;
    }

    // NEW: only dig if this is FORWARD direction (not backward)
    bool isForward = (dx == player3.dirX && dy == player3.dirY && dz == player3.dirZ);
    if (!isForward) return false;  // backward into wall: blocked

    // Dig (forward only)
    v.durability -= DIG_PER_TICK;
    if (v.durability <= 0) {
        int recoveredType = v.type;
        v.type = -1;
        v.isObstacle = false;
        v.durability = 0;
        if (recoveredType == 4) AddToInventory(4, 1);
        player3.x = nx; player3.y = ny; player3.z = nz;
        return true;
    }
    return false;
}

inline void MoveForward() { TryMove(player3.dirX, player3.dirY, player3.dirZ); }
inline void MoveBackward() { TryMove(-player3.dirX, -player3.dirY, -player3.dirZ); }
static bool strafingLeft = false;
static bool strafingRight = false;
// Add these two functions next to MoveForward/MoveBackward
void StrafeLeft() {
    if (player3.dirZ != 0) return;
    TryMove(player3.dirY, -player3.dirX, 0);   // ← this now feels correct in all 4 horizontal directions
}

void StrafeRight() {
    if (player3.dirZ != 0) return;
    TryMove(-player3.dirY, player3.dirX, 0);
}

// -------------------- Rendering Helpers --------------------
std::wstring ShortId(int x, int y, int z) {
    x = WrapX(x);
    y = WrapY(y);
    if (z < 0 || z >= WORLD_SIZE_Z) return L"###";
    int id = WorldIndex(x, y, z) % 1000;
    std::wstringstream ss;
    ss << std::setw(3) << id;
    return ss.str();
}

// Glyph mapping (Set 1): use escaped unicode for dot/circle
std::wstring GlyphForSlice(int slice) {
    int dx = player3.dirX;
    int dy = player3.dirY;
    int dz = player3.dirZ;

    if (slice == 0) { // XY => perp = Z
        if (dz == 1) return L"\u2022"; // solid dot toward camera
        if (dz == -1) return L"\u25CB"; // hollow circle away
        if (dx == 1) return L">";
        if (dx == -1) return L"<";
        if (dy == 1) return L"v";
        if (dy == -1) return L"^";
    }
    else if (slice == 1) { // XZ => perp = Y
        if (dy == 1) return L"\u2022";
        if (dy == -1) return L"\u25CB";
        if (dx == 1) return L">";
        if (dx == -1) return L"<";
        if (dz == 1) return L"^";
        if (dz == -1) return L"v";
    }
    else { // slice == 2, YZ => perp = X
        if (dx == 1) return L"\u2022";
        if (dx == -1) return L"\u25CB";
        if (dy == 1) return L">";
        if (dy == -1) return L"<";
        if (dz == 1) return L"^";
        if (dz == -1) return L"v";
    }
    return L"?";
}

std::wstring FacingText() {
    if (player3.dirX == 1) return L"+X";
    if (player3.dirX == -1) return L"-X";
    if (player3.dirY == 1) return L"+Y";
    if (player3.dirY == -1) return L"-Y";
    if (player3.dirZ == 1) return L"+Z";
    if (player3.dirZ == -1) return L"-Z";
    return L"(unknown)";
}

// header drawn inside top-left of each quadrant (option 2)
void DrawHeaderInsideSlice(Graphics& g, const std::wstring& txt, int sx, int sy) {
    RectF bg((REAL)sx + 2.0f, (REAL)sy + 2.0f, (REAL)(CELL_PX * 6 - 4), 18.0f);
    SolidBrush bgBrush(Color(200, 30, 30, 30)); // dark translucent
    g.FillRectangle(&bgBrush, bg);
    g.DrawString(txt.c_str(), -1, gHeaderFont, PointF((REAL)sx + 4.0f, (REAL)sy + 2.0f), gHeaderBrush);
}

void DrawSlice(Graphics& g, int type, int sx, int sy) {
    int half = VISIBLE / 2;

    for (int cy = 0; cy < VISIBLE; ++cy) {
        for (int cx = 0; cx < VISIBLE; ++cx) {
            int px = sx + cx * CELL_PX;
            int py = sy + cy * CELL_PX;

            int wx = 0, wy = 0, wz = 0;
            int offsetX = cx - half;
            int offsetY = cy - half;
            int offsetZ = half - cy;  // For vertical slices

            if (type == 0) { // XY
                wx = WrapX(player3.x + offsetX);
                wy = WrapY(player3.y + offsetY);
                wz = player3.z;
            }
            else if (type == 1) { // XZ
                wx = WrapX(player3.x + offsetX);
                wy = player3.y;
                wz = player3.z + offsetZ;
            }
            else { // YZ
                wx = player3.x;
                wy = WrapY(player3.y + offsetX);
                wz = player3.z + offsetZ;
            }

            g.FillRectangle(gCellBgBrush, Rect(px, py, CELL_PX - 1, CELL_PX - 1));
            g.DrawRectangle(gGridPen, px, py, CELL_PX - 1, CELL_PX - 1);

            bool inBoundsZ = (wz >= 0 && wz < WORLD_SIZE_Z);
            if (inBoundsZ) {
                Voxel& v = world[WorldIndex(wx, wy, wz)];
                if (v.type == 4 && patternBlockType4) {
                    // draw the procedural pattern for type 4
                    g.DrawImage(patternBlockType4, px, py, CELL_PX - 1, CELL_PX - 1);
                }
                else if (v.type == 5 && patternBlockType5) {  // <<< ADDED: type 5 decoration
                    g.DrawImage(patternBlockType5, px, py, CELL_PX - 1, CELL_PX - 1);
                }
                else {
                    std::wstring id = ShortId(wx, wy, wz);
                    RectF r((REAL)px + 2.0f, (REAL)py + 2.0f, (REAL)CELL_PX - 4.0f, (REAL)CELL_PX - 4.0f);
                    g.DrawString(id.c_str(), -1, gIdFont, r, nullptr, gIdBrush);
                }
            }
            else {
                // out-of-bounds cell ID
                std::wstring id = L"###";
                RectF r((REAL)px + 2.0f, (REAL)py + 2.0f, (REAL)CELL_PX - 4.0f, (REAL)CELL_PX - 4.0f);
                g.DrawString(id.c_str(), -1, gIdFont, r, nullptr, gIdBrush);
            }
        }
    }

    // draw player center block
    int cpx = sx + half * CELL_PX;
    int cpy = sy + half * CELL_PX;
    g.FillRectangle(gPlayerBrush, Rect(cpx, cpy, CELL_PX - 1, CELL_PX - 1));

    // draw direction glyph projected for this slice
    std::wstring glyph = GlyphForSlice(type);
    RectF glyphLayout((REAL)cpx + 6.0f, (REAL)cpy + 6.0f, (REAL)CELL_PX - 12.0f, (REAL)CELL_PX - 12.0f);
    g.DrawString(glyph.c_str(), -1, gIdFont, glyphLayout, nullptr, gHeaderBrush);

    // <<< NEW: draw 3x3 red translucent placement overlay centered on player >>>
    int overlayHalf = 1;
    SolidBrush overlayBrush(Color(120, 255, 0, 0)); // translucent red
    for (int oy = -overlayHalf; oy <= overlayHalf; ++oy) {
        for (int ox = -overlayHalf; ox <= overlayHalf; ++ox) {
            int wx = 0, wy = 0, wz = 0;
            if (type == 0) { wx = player3.x + ox; wy = player3.y + oy; wz = player3.z; }
            else if (type == 1) { wx = player3.x + ox; wy = player3.y; wz = player3.z + oy; }
            else { wx = player3.x; wy = player3.y + ox; wz = player3.z + oy; }

            int drawCx = sx + (half + ox) * CELL_PX;
            int drawCy = sy + (half + oy) * CELL_PX;
            g.FillRectangle(&overlayBrush, Rect(drawCx, drawCy, CELL_PX - 1, CELL_PX - 1));
        }
    }
}
void DrawCardinalLabels(Graphics& g, int type, int sx, int sy) {
    int half = VISIBLE / 2;
    int centerX = sx + half * CELL_PX;
    int centerY = sy + half * CELL_PX;
    int edge = CELL_PX * 2;  // distance from center to label

    SolidBrush labelBrush(Color(255, 255, 220, 100));
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    Font* f = gIdFont;  // reuse existing font

    if (type == 0) { // XY slice
        g.DrawString(L"N", 1, f, PointF(centerX, sy + edge), &format, &labelBrush);  // North (+Y)
        g.DrawString(L"S", 1, f, PointF(centerX, sy + QUAD_PX - edge), &format, &labelBrush);  // South (-Y)
        g.DrawString(L"E", 1, f, PointF(sx + QUAD_PX - edge, centerY), &format, &labelBrush);  // East  (+X)
        g.DrawString(L"W", 1, f, PointF(sx + edge, centerY), &format, &labelBrush);  // West  (-X)
    }
    else if (type == 1) { // XZ slice
        g.DrawString(L"E", 1, f, PointF(sx + QUAD_PX - edge, centerY), &format, &labelBrush);  // East  (+X)
        g.DrawString(L"W", 1, f, PointF(sx + edge, centerY), &format, &labelBrush);  // West  (-X)
        // Extra U/D labels on vertical axis
        g.DrawString(L"U", 1, f, PointF(centerX, sy + edge), &format, &labelBrush);
        g.DrawString(L"D", 1, f, PointF(centerX, sy + QUAD_PX - edge), &format, &labelBrush);
    }
    else { // type == 2 → YZ slice
        g.DrawString(L"N", 1, f, PointF(sx + edge, centerY), &format, &labelBrush);  // +Z → U(p)
        g.DrawString(L"S", 1, f, PointF(sx + QUAD_PX - edge, centerY), &format, &labelBrush);  // –Z → D(own)
        g.DrawString(L"U", 1, f, PointF(centerX, sy + edge), &format, &labelBrush);
        g.DrawString(L"D", 1, f, PointF(centerX, sy + QUAD_PX - edge), &format, &labelBrush);
    }
}
// -------------------- Scene Drawing --------------------
void DrawScene(HDC hdcPaint) {
    Graphics g(hdcMem);
    g.Clear(Color(255, 20, 20, 20));

    int TLx = 0, TLy = 0;
    int TRx = QUAD_PX, TRy = 0;
    int BLx = 0, BLy = QUAD_PX;
    int BRx = QUAD_PX, BRy = QUAD_PX;

    // prepare header strings (we will draw them inside slices)
    std::wstringstream ssxy; ssxy << L"XY (Z=" << player3.z << L")";
    std::wstringstream ssxz; ssxz << L"XZ (Y=" << player3.y << L")";
    std::wstringstream ssyz; ssyz << L"YZ (X=" << player3.x << L")";

    // draw slices
    DrawSlice(g, 0, TLx, TLy); // XY
    DrawCardinalLabels(g, 0, TLx, TLy);

    DrawSlice(g, 1, TRx, TRy); // XZ
    DrawCardinalLabels(g, 1, TRx, TRy);

    DrawSlice(g, 2, BLx, BLy); // YZ
    DrawCardinalLabels(g, 2, BLx, BLy);

    // draw headers inside slices (option 2)
    DrawHeaderInsideSlice(g, ssxy.str(), TLx, TLy);
    DrawHeaderInsideSlice(g, ssxz.str(), TRx, TRy);
    DrawHeaderInsideSlice(g, ssyz.str(), BLx, BLy);

    // bottom-right debug quadrant background and border
    g.FillRectangle(gCellBgBrush, Rect(BRx, BRy, QUAD_PX, QUAD_PX));
    g.DrawRectangle(gGridPen, BRx, BRy, QUAD_PX - 1, QUAD_PX - 1);

    // Label DEBUG in top-left of debug quadrant (inside)
    DrawHeaderInsideSlice(g, std::wstring(L"DEBUG"), BRx, BRy);

    // Debug info: position, dir, facing text
    std::wstringstream dbg;
    dbg << L"Player Position:\n";
    dbg << L" X = " << player3.x << L"\n";
    dbg << L" Y = " << player3.y << L"\n";
    dbg << L" Z = " << player3.z << L"\n\n";

    dbg << L"Direction Vector:\n";
    dbg << L" dirX = " << player3.dirX << L"\n";
    dbg << L" dirY = " << player3.dirY << L"\n";
    dbg << L" dirZ = " << player3.dirZ << L"\n\n";

    dbg << L"Facing: " << FacingText() << L"\n";

    // <<< NEW: show gravity direction >>>
    dbg << L"\nGravity Direction:\n";
    dbg << L" gravX = " << gravityDirX << L"\n";
    dbg << L" gravY = " << gravityDirY << L"\n";
    dbg << L" gravZ = " << gravityDirZ << L"\n\n";

    dbg << L"Controls:\n W/S = forward/back (hold for continuous)\n A/D = yaw\n 2/X = pitch\n V = set gravity to facing dir\n Left-click places block (within 1 cell)\n Right-click removes block (within 1 cell)\n 4-9 select block type (4 = obstacle, 5 = permanent decoration)\n";

    RectF dbgRect((REAL)BRx + 8.0f, (REAL)BRy + 24.0f, (REAL)QUAD_PX - 16.0f, (REAL)QUAD_PX - 40.0f);
    g.DrawString(dbg.str().c_str(), -1, gDebugFont, dbgRect, nullptr, gDebugBrush);

    // <<< NEW: draw inventory in bottom-right debug quadrant under debug text >>>
    {
        int invX = BRx + 12;
        int invY = BRy + 340;
        std::wstringstream invHeader;
        invHeader << L"Inventory:";
        g.DrawString(invHeader.str().c_str(), -1, gIdFont, PointF((REAL)invX, (REAL)invY), gIdBrush);
        invY += 18;
        if (playerInventory.empty()) {
            g.DrawString(L"(empty)", -1, gIdFont, PointF((REAL)invX, (REAL)invY), gIdBrush);
        }
        else {
            for (auto& e : playerInventory) {
                std::wstringstream line;
                line << L"Type " << e.type << L": " << e.count;
                g.DrawString(line.str().c_str(), -1, gIdFont, PointF((REAL)invX, (REAL)invY), gIdBrush);
                invY += 14;
            }
        }
    }

    // final blit to the window DC
    BitBlt(hdcPaint, 0, 0, WINDOW_PX, WINDOW_PX, hdcMem, 0, 0, SRCCOPY);
}

// -------------------- Mouse place/remove helpers (NEW + MODIFIED) --------------------
bool IsWrappedAdjacent(int a, int b, int size) {
    int d = abs(a - b);
    return d <= 1 || d >= size - 1;
}

void PlaceVoxelAt(int wx, int wy, int wz, int type) {
    wx = WrapX(wx);
    wy = WrapY(wy);
    if (wz < 0 || wz >= WORLD_SIZE_Z) return;
    if (type == 4) {
        world[WorldIndex(wx, wy, wz)].type = 4;
        world[WorldIndex(wx, wy, wz)].isObstacle = true;
        world[WorldIndex(wx, wy, wz)].durability = DEFAULT_OBSTACLE_DUR;
    }
    else if (type == 5) {  // <<< ADDED: permanent non-obstacle decoration
        world[WorldIndex(wx, wy, wz)].type = 5;
        world[WorldIndex(wx, wy, wz)].isObstacle = false;
        world[WorldIndex(wx, wy, wz)].durability = 0;
    }
    else {
        world[WorldIndex(wx, wy, wz)].type = type;
        world[WorldIndex(wx, wy, wz)].isObstacle = false;
        world[WorldIndex(wx, wy, wz)].durability = 0;
    }
}

void RemoveVoxelAt(int wx, int wy, int wz) {
    wx = WrapX(wx);
    wy = WrapY(wy);
    if (wz < 0 || wz >= WORLD_SIZE_Z) return;
    Voxel& v = world[WorldIndex(wx, wy, wz)];
    if (v.type == 4) {  // only type 4 can be removed
        AddToInventory(4, 1);
        v = Voxel();
    }
    // type 5 is permanent - cannot be destroyed
}

// -------------------- Window Procedure --------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE:
        InitGdi(hwnd);
        InitWorldAndPlayer();
        SetTimer(hwnd, 1, 16, nullptr);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        CleanupGdi();
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        // <<< NEW: accumulate time and handle continuous movement here (minimal intrusion) >>>
        moveTimerAccumMs += 16; // timer is 16ms
        if (moveTimerAccumMs >= MOVE_DELAY_MS) {
            moveTimerAccumMs = 0;
            if (movingForward) MoveForward();
            if (movingBackward) MoveBackward();
            if (strafingLeft)  StrafeLeft();
            if (strafingRight) StrafeRight();
        }

        // <<< NEW: gravity tick >>>
        gravityTimerAccumMs += 16;
        if (gravityTimerAccumMs >= GRAVITY_DELAY_MS) {
            gravityTimerAccumMs = 0;
            int tx = WrapX(player3.x + gravityDirX);
            int ty = WrapY(player3.y + gravityDirY);
            int tz = player3.z + gravityDirZ;

            if (tz >= 0 && tz < WORLD_SIZE_Z &&
                !world[WorldIndex(tx, ty, tz)].isObstacle) {
                player3.x = tx;
                player3.y = ty;
                player3.z = tz;
            }
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        DrawScene(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN:
    {
        switch ((int)wParam) {
        case 'W':
            MoveForward(); // immediate move on press
            movingForward = true;
            moveTimerAccumMs = 0;
            break;
        case 'S':
            MoveBackward();
            movingBackward = true;
            moveTimerAccumMs = 0;
            break;
        case 'A': YawLeft90(); break;
        case 'D': YawRight90(); break;
        case '2': PitchUp90(); break;
        case 'X': PitchDown90(); break;
        case 'V': // <<< NEW: set gravity to current facing direction >>>
            gravityDirX = player3.dirX;
            gravityDirY = player3.dirY;
            gravityDirZ = player3.dirZ;
            break;
        case 'Q':
            StrafeLeft();                     // immediate step on press
            strafingLeft = true;             // start continuous
            moveTimerAccumMs = 0;             // reset so it doesn’t wait the full 160 ms
            InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case 'E':
            StrafeRight();
            strafingRight = true;
            moveTimerAccumMs = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case '4': currentPlaceType = 4; break;
        case '5': currentPlaceType = 5; break;
        case '6': currentPlaceType = 6; break;
        case '7': currentPlaceType = 7; break;
        case '8': currentPlaceType = 8; break;
        case '9': currentPlaceType = 9; break;
        default: break;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYUP:
    {
        switch ((int)wParam) {
        case 'W': movingForward = false; break;
        case 'S': movingBackward = false; break;
        case 'Q': strafingLeft = false; break;
        case 'E': strafingRight = false; break;
        default: break;
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        int qx = (mx >= QUAD_PX) ? 1 : 0;
        int qy = (my >= QUAD_PX) ? 1 : 0;

        if (qx == 1 && qy == 1) return 0; // debug quadrant

        int localX = mx - qx * QUAD_PX;
        int localY = my - qy * QUAD_PX;
        int cx = localX / CELL_PX;
        int cy = localY / CELL_PX;
        int half = VISIBLE / 2;

        int wx = 0, wy = 0, wz = 0;
        if (qx == 0 && qy == 0) { // XY
            wx = player3.x + (cx - half);
            wy = player3.y + (cy - half);
            wz = player3.z;
        }
        else if (qx == 1 && qy == 0) { // XZ
            wx = player3.x + (cx - half);
            wy = player3.y;
            wz = player3.z + (cy - half);
        }
        else { // YZ
            wx = player3.x;
            wy = player3.y + (cx - half);
            wz = player3.z + (cy - half);
        }

        wx = WrapX(wx);
        wy = WrapY(wy);
        // wz unchanged (no wrap)

        if (IsWrappedAdjacent(wx, player3.x, WORLD_SIZE_X) &&
            IsWrappedAdjacent(wy, player3.y, WORLD_SIZE_Y) &&
            abs(wz - player3.z) <= 1) {
            PlaceVoxelAt(wx, wy, wz, currentPlaceType);
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_RBUTTONDOWN: {
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        int qx = (mx >= QUAD_PX) ? 1 : 0;
        int qy = (my >= QUAD_PX) ? 1 : 0;

        if (qx == 1 && qy == 1) return 0; // debug quadrant

        int localX = mx - qx * QUAD_PX;
        int localY = my - qy * QUAD_PX;
        int cx = localX / CELL_PX;
        int cy = localY / CELL_PX;
        int half = VISIBLE / 2;

        int wx = 0, wy = 0, wz = 0;
        if (qx == 0 && qy == 0) { // XY
            wx = player3.x + (cx - half);
            wy = player3.y + (cy - half);
            wz = player3.z;
        }
        else if (qx == 1 && qy == 0) { // XZ
            wx = player3.x + (cx - half);
            wy = player3.y;
            wz = player3.z + (cy - half);
        }
        else { // YZ
            wx = player3.x;
            wy = player3.y + (cx - half);
            wz = player3.z + (cy - half);
        }

        wx = WrapX(wx);
        wy = WrapY(wy);
        // wz unchanged (no wrap)

        if (IsWrappedAdjacent(wx, player3.x, WORLD_SIZE_X) &&
            IsWrappedAdjacent(wy, player3.y, WORLD_SIZE_Y) &&
            abs(wz - player3.z) <= 1) {
            RemoveVoxelAt(wx, wy, wz);
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// -------------------- WinMain --------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DrillderProtoClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;

    RegisterClass(&wc);

    RECT r = { 0,0,WINDOW_PX,WINDOW_PX };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND hwnd = CreateWindowEx(
        0,
        L"DrillderProtoClass",
        L"Drillder Prototype",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left,
        r.bottom - r.top,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}