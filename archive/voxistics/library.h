// library.h
//
// The block library (DESIGN.md 11.x) and the hotbar it fills: layout and
// the click-or-drag gesture, as plain data and functions so they're
// tested natively. game.cpp draws what these describe and feeds them the
// mouse.
//
// Like the block menu in cc_2_2_2.cpp -- a grid of every block's swatch
// where a click picks the type -- plus one gesture on top: press on a
// block and release without moving (a click) and that block goes into the
// selected hotbar slot and the library closes; press and drag it onto a
// hotbar slot and it goes into that slot, and the library stays open.

#pragma once

#include "blocks.h"
#include <cmath>
#include <cstdlib>

static const int HOTBAR_SLOTS = 10;       // keys 1-9 and 0
static const int HOTBAR_SLOT_PX = 48, HOTBAR_GAP_PX = 4;
static const int LIBRARY_CELL_PX = 56;    // one icon cell in the library grid
static const int LIBRARY_DRAG_PX = 6;     // movement beyond this turns a press into a drag

struct UiRect { float x0, y0, x1, y1; };
static inline bool InUiRect(const UiRect& r, float x, float y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

// Hotbar slot `i`, centred along the bottom of the screen.
static inline UiRect HotbarSlotRect(int screenW, int screenH, int i) {
    float total = (float)(HOTBAR_SLOTS * HOTBAR_SLOT_PX + (HOTBAR_SLOTS - 1) * HOTBAR_GAP_PX);
    float x0 = (float)(int)((screenW - total) / 2.0f) + i * (float)(HOTBAR_SLOT_PX + HOTBAR_GAP_PX);
    float y0 = (float)(screenH - HOTBAR_SLOT_PX - 16);
    return { x0, y0, x0 + HOTBAR_SLOT_PX, y0 + HOTBAR_SLOT_PX };
}
static inline int HotbarSlotAt(int screenW, int screenH, float x, float y) {
    for (int i = 0; i < HOTBAR_SLOTS; i++) if (InUiRect(HotbarSlotRect(screenW, screenH, i), x, y)) return i;
    return -1;
}

// The library panel: a grid of `count` cells, as many columns as fit
// (at most 10), centred above the hotbar; rows past what fits scroll.
struct LibraryLayout {
    UiRect panel;       // the whole panel, title included
    UiRect grid;        // the visible part of the grid
    int columns, rows, visibleRows;
};
static inline LibraryLayout ComputeLibraryLayout(int screenW, int screenH, int count) {
    LibraryLayout L;
    int maxCols = (screenW - 80) / LIBRARY_CELL_PX;
    L.columns = maxCols < 1 ? 1 : (maxCols > 10 ? 10 : maxCols);
    L.rows = (count + L.columns - 1) / L.columns;
    int room = (screenH - HOTBAR_SLOT_PX - 16 - 40 - 80) / LIBRARY_CELL_PX; // above the hotbar, below a title
    L.visibleRows = room < 1 ? 1 : (L.rows < room ? L.rows : room);
    float gw = (float)(L.columns * LIBRARY_CELL_PX), gh = (float)(L.visibleRows * LIBRARY_CELL_PX);
    float gx = (float)(int)((screenW - gw) / 2.0f);
    float bottom = (float)(screenH - HOTBAR_SLOT_PX - 16 - 40);
    float gy = (float)(int)(bottom - gh);
    L.grid = { gx, gy, gx + gw, gy + gh };
    L.panel = { gx - 16, gy - 56, gx + gw + 16, gy + gh + 16 };
    return L;
}
// Which entry (index into the placeable list) is under (x, y), given the
// first visible row; -1 if none.
static inline int LibraryCellAt(const LibraryLayout& L, int count, int scrollRow, float x, float y) {
    if (!InUiRect(L.grid, x, y)) return -1;
    int col = (int)((x - L.grid.x0) / LIBRARY_CELL_PX), row = (int)((y - L.grid.y0) / LIBRARY_CELL_PX) + scrollRow;
    int i = row * L.columns + col;
    return i < count ? i : -1;
}
static inline UiRect LibraryCellRect(const LibraryLayout& L, int scrollRow, int i) {
    int row = i / L.columns - scrollRow, col = i % L.columns;
    float x0 = L.grid.x0 + col * LIBRARY_CELL_PX, y0 = L.grid.y0 + row * LIBRARY_CELL_PX;
    return { x0, y0, x0 + LIBRARY_CELL_PX, y0 + LIBRARY_CELL_PX };
}

// The press / move / release gesture.
struct LibraryGesture {
    int pressed = -1;          // entry pressed on, or -1
    float downX = 0, downY = 0;
    bool dragging = false;     // moved past LIBRARY_DRAG_PX since the press
};
enum class LibraryOutcome { None, Select, Assign };
struct LibraryResult { LibraryOutcome outcome = LibraryOutcome::None; int entry = -1; int slot = -1; };

static inline void LibraryPress(LibraryGesture& g, int entry, float x, float y) {
    g.pressed = entry; g.downX = x; g.downY = y; g.dragging = false;
}
static inline void LibraryMove(LibraryGesture& g, float x, float y) {
    if (g.pressed >= 0 && !g.dragging && (fabsf(x - g.downX) > LIBRARY_DRAG_PX || fabsf(y - g.downY) > LIBRARY_DRAG_PX)) g.dragging = true;
}
// On release: a click selects the pressed entry (for the current slot,
// closing the library); a drag released over hotbar slot `slotUnder`
// assigns it there; a drag released anywhere else does nothing.
static inline LibraryResult LibraryRelease(LibraryGesture& g, int slotUnder) {
    LibraryResult r;
    if (g.pressed >= 0) {
        if (!g.dragging) { r.outcome = LibraryOutcome::Select; r.entry = g.pressed; }
        else if (slotUnder >= 0) { r.outcome = LibraryOutcome::Assign; r.entry = g.pressed; r.slot = slotUnder; }
    }
    g = LibraryGesture();
    return r;
}

// The hotbar a fresh install starts with: a spread of what there is.
static inline void DefaultHotbar(BlockID out[HOTBAR_SLOTS]) {
    const BlockID d[HOTBAR_SLOTS] = { BLOCK_STONE, BLOCK_DIRT, BLOCK_WOOD, BLOCK_LOG, BLOCK_SAND,
                                      BLOCK_SANDSTONE, BLOCK_GLASS, BLOCK_MUSIC, BLOCK_TIMESTREAM, BLOCK_STONE_SLAB };
    for (int i = 0; i < HOTBAR_SLOTS; i++) out[i] = d[i];
}
