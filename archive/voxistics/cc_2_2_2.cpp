
#include <windows.h>   // Header for Windows API, provides access to Windows-specific functions and data types.
#include <vector>      // Standard vector library for dynamic arrays, allows for efficient storage and manipulation of elements.
#include <fstream>     // File input/output library, enables reading from and writing to files.
#include <string>      // String manipulation library, provides support for handling and manipulating strings.
#include <shlobj.h>    // Shell functions for folder paths (e.g., saving/loading game), provides access to shell utilities.
#include <locale>      // Locale-related functions for character conversion, enables localization and text formatting.
#include <codecvt>     // For converting between wide and UTF-8 strings, facilitates encoding conversions.
#include <set>         // Used for tracking unique positions, provides an associative container for unique elements.
#include <utility>     // Provides utility functions and classes, such as std::pair and std::swap.
#include <gdiplus.h>   // For GDI+ Graphics, supports advanced 2D graphics, including rendering and image processing.
#include <cmath>       // For mathematical functions, including trigonometric functions like sin.
#include <unordered_map> // Provides a hash table-based implementation for key-value pairs, allowing for fast lookups.
#include <functional>  // Defines function objects and standard function wrappers, used for callbacks and higher-order functions.

COLORREF BACKGROUND_COLOR = RGB(101, 67, 33);       // Brown background color

bool isFullscreen = false;  // Global flag to track fullscreen state

bool isMenuOpen = false;
bool isFullscreenMenuOpen = false;

using namespace Gdiplus;

ULONG_PTR gdiplusToken;
GdiplusStartupInput gdiplusStartupInput;
void CleanupSprites();

Bitmap* red_solid_pattern;
Bitmap* a_text_pattern;
Bitmap* b_text_pattern;
Bitmap* c_text_pattern;
Bitmap* d_text_pattern;
Bitmap* e_text_pattern;
Bitmap* f_text_pattern;
Bitmap* g_text_pattern;
Bitmap* h_text_pattern;
Bitmap* i_text_pattern;
Bitmap* j_text_pattern;
Bitmap* k_text_pattern;
Bitmap* l_text_pattern;
Bitmap* m_text_pattern;
Bitmap* n_text_pattern;
Bitmap* o_text_pattern;
Bitmap* p_text_pattern;
Bitmap* q_text_pattern;
Bitmap* r_text_pattern;
Bitmap* s_text_pattern;
Bitmap* t_text_pattern;
Bitmap* u_text_pattern;
Bitmap* v_text_pattern;
Bitmap* w_text_pattern;
Bitmap* x_text_pattern;
Bitmap* y_text_pattern;
Bitmap* z_text_pattern;
Bitmap* zero_text_pattern;
Bitmap* one_text_pattern;
Bitmap* two_text_pattern;
Bitmap* three_text_pattern;
Bitmap* four_text_pattern;
Bitmap* five_text_pattern;
Bitmap* six_text_pattern;
Bitmap* seven_text_pattern;
Bitmap* eight_text_pattern;
Bitmap* nine_text_pattern;
Bitmap* question_text_pattern;
Bitmap* exclaim_text_pattern;
Bitmap* plus_text_pattern;
Bitmap* equal_text_pattern;
Bitmap* diamond_pattern_1;
Bitmap* chevron_pattern_1;
Bitmap* offsetherringbone_pattern_1;
Bitmap* zigzagherringbone_pattern_1;
Bitmap* smoothzigzag_pattern_1;
Bitmap* vstripe_pattern_1;
Bitmap* herringbone_pattern_1;
Bitmap* denseplaid_pattern_1;
Bitmap* diagonalherringbone_pattern_1;
Bitmap* concentricflowers_pattern_1;
Bitmap* fractalhalftone_pattern_1;
Bitmap* herringbonechevron_pattern;
Bitmap* diagonalchevron_pattern;
Bitmap* feathertexture_pattern_1;
Bitmap* circuitledmatrix_pattern_1;
Bitmap* hexagonalcircuitgrid_pattern_1;
Bitmap* arcanecircuitglyphs_pattern_1;
Bitmap* alchemicalpowercore_pattern_1;
Bitmap* celestialdatarings_pattern_1;
Bitmap* fishscalepattern_pattern_1;
Bitmap* interlockingcirclespattern_pattern_1;
Bitmap* pixelatedcamouflage_pattern_1;
Bitmap* tormentedsoilpattern_pattern_1;
Bitmap* organicfractalpattern_pattern_1;
Bitmap* clockworkpattern_pattern_1;
Bitmap* neoncircuitpattern_pattern_1;
Bitmap* hyperspacepattern_pattern_1;
Bitmap* nightmarepattern_pattern_1;
Bitmap* spirallatticepattern_pattern_1;
Bitmap* lightfracturepattern_pattern_1;
Bitmap* negativegeometrypattern_pattern_1;
Bitmap* unrealitytidepattern_pattern_1;
Bitmap* recursivewellpattern_pattern_1;
Bitmap* neongrid_pattern_1;
Bitmap* crystallinestructure_pattern_1;
Bitmap* barktexture_pattern_1;
Bitmap* stonemosaicpattern_pattern_1;
Bitmap* organicmeshpattern_pattern_1;
Bitmap* timewarppattern_pattern_1;
Bitmap* quantummatrixpattern_pattern_1;
Bitmap* alienflorapattern_pattern_1;
Bitmap* leafdecay_pattern_1;
Bitmap* naturescamouflage_pattern_1;
Bitmap* icefracture_pattern_1;
Bitmap* nanobotswarm_pattern_1;
Bitmap* solarpanelarray_pattern_1;
Bitmap* factorysilhouettepattern_pattern_1;
Bitmap* nanotechgrid_pattern_1;
Bitmap* circuitoverloadpattern_pattern_1;
Bitmap* mysticalfog_pattern_1;
Bitmap* classicmaze_pattern_1;

Bitmap* canopyLeaves_pattern;
Bitmap* desertSand_pattern;
Bitmap* lilyPads_pattern;
Bitmap* brickPath_pattern;
Bitmap* algae_pattern;
Bitmap* flowers_pattern;
Bitmap* holeInGround_pattern;
Bitmap* trash_pattern;
Bitmap* bush_pattern;
Bitmap* gasolinePuddle_pattern;
Bitmap* tNT_pattern;
Bitmap* rock_pattern;
Bitmap* tallGrass_pattern;
Bitmap* shortGrass_pattern;
Bitmap* burntTree_pattern;
Bitmap* seawater_pattern;
Bitmap* freshwater_pattern;
Bitmap* fire_pattern;
Bitmap* grasslands_pattern;
Bitmap* boulderField_pattern;
Bitmap* sandDunes_pattern;
Bitmap* charredStump_pattern;
Bitmap* autumnTree_pattern;
Bitmap* wavyGrassPatch_pattern;
Bitmap* largeBoulder_pattern;
Bitmap* thornyShrub_pattern;
Bitmap* wildflowerPatch_pattern;
Bitmap* forestCanopy_pattern;
Bitmap* riverbedPebbles_pattern;
Bitmap* grassTuft_pattern;
Bitmap* rockyOutcrop_pattern;
Bitmap* sandyRipple_pattern;
Bitmap* leafVein_pattern;
Bitmap* mossyStone_pattern;
Bitmap* wetMud_pattern;
Bitmap* fernFrond_pattern;
Bitmap* snowDrift_pattern;
Bitmap* volcanicAsh_pattern;
Bitmap* grassChevron_pattern;
Bitmap* rockyDiamond_pattern;
Bitmap* dirtHerringbone_pattern;
Bitmap* barkPatch_pattern;
Bitmap* snowPatch_pattern;
Bitmap* volcanicCrater_pattern;
Bitmap* fernPatch_pattern;
Bitmap* enchantedForestGlade_pattern;
Bitmap* desertMirageRipple_pattern;
Bitmap* craggyMossyCliff_pattern;
Bitmap* twilightMeadowBloom_pattern;
Bitmap* icyRiverbedFusion_pattern;
Bitmap* shadyGrove_pattern;
Bitmap* mistyHighland_pattern;
Bitmap* pebbledStreambed_pattern;
Bitmap* autumnalLeafFall_pattern;
Bitmap* volcanicLavaFlow_pattern;
Bitmap* luminescentFernGrove_pattern;
Bitmap* crystalineFrostMeadow_pattern;
Bitmap* sunlitPrairieWave_pattern;
Bitmap* echoingCanyonEcho_pattern;
Bitmap* verdantSwampMist_pattern;
Bitmap* twilightWoodlandPatch_pattern;
Bitmap* lushRainforestUndergrowth_pattern;
Bitmap* frozenTundraCrack_pattern;
Bitmap* whisperingWindGrass_pattern;
Bitmap* sunbakedDuneCrest_pattern;
Bitmap* legionsNumberMosaic_pattern;
Bitmap* batrachionSpiral_pattern;
Bitmap* factorionGrid_pattern;
Bitmap* jugglerSpiral_pattern;
Bitmap* carotidKundalini_pattern;
Bitmap* leviathanNumber_pattern;
Bitmap* goldenRatioSpiral_pattern;
Bitmap* cellularAutomaton_pattern;
Bitmap* sierpinskiTriangle_pattern;
Bitmap* truchetArc_pattern;
Bitmap* vampireNumber_pattern;
Bitmap* leafLitter_pattern;


const wchar_t CLASS_NAME[] = L"LGR"; // Window class name for registration
const int GRID_SIZE = 32;                       // Size of each grid cell (pixels)
const int GRID_COLS_INITIAL = 60;              // Initial number of grid columns
const int GRID_ROWS_INITIAL = 33;              // Initial number of grid rows
int GRID_COLS = GRID_COLS_INITIAL;              // Current number of columns
int GRID_ROWS = GRID_ROWS_INITIAL;              // Current number of rows
int WIDTH = GRID_COLS * GRID_SIZE;              // Width of the game window in pixels
int HEIGHT = GRID_ROWS * GRID_SIZE;             // Height of the game window in pixels

std::vector<std::vector<bool>> grid(GRID_ROWS, std::vector<bool>(GRID_COLS, false));
struct Block {
    int x;
    int y;
    int type;
    // add other block aspects here

        // Default constructor with member initializer list
    Block(int x = 0, int y = 0, int type = 0)
        : x(x), y(y), type(type) {}
};

std::vector<Block> savedBlocks;  // Store overwritten blocks

std::vector<Block> savedFullscreenBlocks;  // Store game state when menu is open

// Global variables for reusable brushes and device context
HDC hdcMem;
HBITMAP hbmMem;
HBITMAP hbmOld;
HBRUSH backgroundBrush;

void InitializeBrushes() {
    // Create all brushes once
    backgroundBrush = CreateSolidBrush(BACKGROUND_COLOR);

}

void InitializeMemoryDC(HDC hdc) {
    if (!hdcMem) {  // Initialize once
        hdcMem = CreateCompatibleDC(hdc); // Create a compatible memory device context
        hbmMem = CreateCompatibleBitmap(hdc, WIDTH, HEIGHT); // Create a bitmap to hold the backbuffer
        hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem); // Select the bitmap into the memory DC
    }
}

void CleanUp() {
    // Delete brush(es) when the game closes
    DeleteObject(backgroundBrush);

    if (hdcMem) {
        SelectObject(hdcMem, hbmOld); // Restore the original bitmap
        DeleteObject(hbmMem); // Delete the compatible bitmap
        DeleteDC(hdcMem); // Delete the memory device context
        hdcMem = nullptr; // Reset pointer to avoid accidental reuse
    }
}

std::vector<Block> blocks; // List of blocks in the game
int currentBlockType = 0;
void DrawScene(HDC hdc);                 // Draws the game scene
void LoadGame();                         // Loads the game state from a file
void SaveGame();                         // Saves the game state to a file
std::wstring GetSaveFilePath();          // Gets the file path for saving/loading


std::string wstringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], sizeNeeded, nullptr, nullptr);
    result.pop_back(); // Remove the null terminator
    return result;
}

std::wstring stringToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], sizeNeeded);
    result.pop_back(); // Remove the null terminator
    return result;
}

void SaveGame() {
    std::wstring filePath = GetSaveFilePath();  // Get save file path
    std::ofstream saveFile(filePath, std::ios::binary); // Open file in binary write mode

    if (saveFile.is_open()) {


        // Save number of blocks
        size_t blockCount = blocks.size();
        saveFile.write(reinterpret_cast<const char*>(&blockCount), sizeof(blockCount));

        // Save each block's data
        for (const Block& block : blocks) {
            saveFile.write(reinterpret_cast<const char*>(&block.x), sizeof(block.x));
            saveFile.write(reinterpret_cast<const char*>(&block.y), sizeof(block.y));
            saveFile.write(reinterpret_cast<const char*>(&block.type), sizeof(block.type));
        }

        saveFile.close(); // Close the file
    }
    else {
        OutputDebugStringW((L"Failed to save game to " + filePath + L"\n").c_str()); // Error handling
    }
}

void LoadGame() {
    std::wstring filePath = GetSaveFilePath();  // Get save file path
    std::ifstream loadFile(filePath, std::ios::binary); // Open file in binary read mode

    if (loadFile.is_open()) {
        blocks.clear(); // Clear existing blocks


        // Load number of blocks
        size_t blockCount;
        loadFile.read(reinterpret_cast<char*>(&blockCount), sizeof(blockCount));

        // Load each block's data
        for (size_t i = 0; i < blockCount; ++i) {
            Block block;
            loadFile.read(reinterpret_cast<char*>(&block.x), sizeof(block.x));
            loadFile.read(reinterpret_cast<char*>(&block.y), sizeof(block.y));
            loadFile.read(reinterpret_cast<char*>(&block.type), sizeof(block.type));

            blocks.push_back(block); // Add block to the list
        }

        loadFile.close(); // Close the file
    }
    else {
        OutputDebugStringW((L"Failed to load game from " + filePath + L"\n").c_str()); // Error handling
    }
}

void SetFullscreen(HWND hwnd) {
    // Get screen width and height
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Set window style to remove title bar and borders
    SetWindowLong(hwnd, GWL_STYLE, WS_POPUP);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, screenWidth, screenHeight, SWP_FRAMECHANGED);

    ShowWindow(hwnd, SW_MAXIMIZE);  // Maximize window to cover entire screen
}


void SetWindowed(HWND hwnd) {
    SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    SetWindowPos(hwnd, HWND_TOP, 100, 100, WIDTH, HEIGHT, SWP_FRAMECHANGED);
    ShowWindow(hwnd, SW_SHOWNORMAL);
}

bool IsBlockOfType(int x, int y, int type) {
    for (const auto& block : blocks) {
        if (block.x == x && block.y == y) {
            return block.type == type;
        }
    }
    return false;
}

void RemoveBlockAtPosition(int x, int y) {
    // Find the block at the given (x, y) and remove it
    auto it = std::remove_if(blocks.begin(), blocks.end(), [x, y](const Block& block) {
        return block.x == x && block.y == y;
        });
    // Erase the removed block(s) from the vector
    if (it != blocks.end()) {
        blocks.erase(it, blocks.end());
    }
}

bool IsBlockAtPosition(int x, int y) {
    for (const Block& block : blocks) {
        if (block.x == x && block.y == y) {
            return true; // Return true if block exists
        }
    }
    return false;
}

bool IsBlockOfTypeAround(int x, int y, int type) {
    int directions[8][2] = {
        {0, -GRID_SIZE}, {0, GRID_SIZE}, {-GRID_SIZE, 0}, {GRID_SIZE, 0}, // Up, Down, Left, Right
        {-GRID_SIZE, -GRID_SIZE}, {GRID_SIZE, GRID_SIZE}, {-GRID_SIZE, GRID_SIZE}, {GRID_SIZE, -GRID_SIZE} // Diagonals
    };

    for (int i = 0; i < 8; ++i) {
        int newX = x + directions[i][0];
        int newY = y + directions[i][1];
        if (IsBlockOfType(newX, newY, type)) {
            return true; // If a block of the given type is found nearby, return true
        }
    }
    return false; // No blocks of the given type found nearby
}

void InitializeScene() {
    // Check if blocks are already loaded (e.g., from a save file)
    if (!blocks.empty()) {
        return; // Do nothing if there are already blocks present
    }

    // Set up the initial scene if no save file was loaded
    for (int y = 0; y < GRID_ROWS; ++y) {
        for (int x = 0; x < GRID_COLS; ++x) {
            if (y < GRID_ROWS / 4) {
                blocks.push_back(Block(x * GRID_SIZE, y * GRID_SIZE, 1)); // Top section uses block type 1
            }
            else if (y >= GRID_ROWS / 4 && y < GRID_ROWS / 2) {
                blocks.push_back(Block(x * GRID_SIZE, y * GRID_SIZE, 2)); // Middle section uses block type 2
            }
            else {
                blocks.push_back(Block(x * GRID_SIZE, y * GRID_SIZE, 3)); // Bottom section uses block type 3
            }
        }
    }
}

void OpenMenu() {
    if (!isMenuOpen) {
        isMenuOpen = true;
        savedBlocks.clear();

        // Save all existing blocks
        savedBlocks = blocks;

        // Clear the entire grid
        blocks.clear();

        // Number of blocks per cluster (e.g., 4 or 9)
        int clusterSize = 3; // Adjust to 2x2 (4 blocks) or 3x3 (9 blocks) clusters

        int blockType = 1;
        for (int y = 0; y < GRID_ROWS; y += clusterSize) {
            for (int x = 0; x < GRID_COLS; x += clusterSize) {
                if (blockType <= 200) {
                    // Create a cluster of blocks for each block type
                    for (int dy = 0; dy < clusterSize; ++dy) {
                        for (int dx = 0; dx < clusterSize; ++dx) {
                            blocks.push_back(Block((x + dx) * GRID_SIZE, (y + dy) * GRID_SIZE, blockType));
                        }
                    }
                    blockType++;
                }
                else {
                    // Fill remaining spaces with a fallback type
                    for (int dy = 0; dy < clusterSize; ++dy) {
                        for (int dx = 0; dx < clusterSize; ++dx) {
                            blocks.push_back(Block((x + dx) * GRID_SIZE, (y + dy) * GRID_SIZE, 1));
                        }
                    }
                }
            }
        }
    }
}




void CloseMenu() {
    if (isMenuOpen) {
        isMenuOpen = false;

        // Restore original blocks
        blocks = savedBlocks;
    }
}


void OpenFullscreenMenu() {
    if (!isFullscreenMenuOpen) {
        isFullscreenMenuOpen = true;
        savedFullscreenBlocks = blocks; // Save the current game state
        blocks.clear(); // Clear the grid for the fullscreen menu

        // Fill the screen with menu elements (Example: block type 99)
        for (int y = 0; y < GRID_ROWS; ++y) {
            for (int x = 0; x < GRID_COLS; ++x) {
                blocks.push_back(Block(x * GRID_SIZE, y * GRID_SIZE, 99));  // Example: menu uses block type 99
            }
        }
        InvalidateRect(nullptr, NULL, TRUE); // Refresh window
    }
}
void CloseFullscreenMenu() {
    if (isFullscreenMenuOpen) {
        isFullscreenMenuOpen = false;
        blocks = savedFullscreenBlocks; // Restore game state
        InvalidateRect(nullptr, NULL, TRUE); // Refresh window
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        InitializeMemoryDC(hdc);  // Initialize the memory DC once here
        ReleaseDC(hwnd, hdc);
        LoadGame();  // Load saved game
        InitializeScene();  // Only applies if no save file exists
        //SetFullscreen(hwnd); //at start of program
        return 0;
    }
    case WM_DESTROY: {// When the window is closed
        if (isMenuOpen) {
            CloseMenu(); // Restore game state before exiting
        }
        if (isFullscreenMenuOpen) {
            CloseFullscreenMenu(); // Restore game state before exiting
        }
        SaveGame();
        CleanUp();// Save the game
        CleanupSprites();
        PostQuitMessage(0); // End the program
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return 0;
    }
    case WM_PAINT: { // Repainting the window
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        DrawScene(hdc); // Draw the game scene
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        int x = LOWORD(lParam) / GRID_SIZE * GRID_SIZE;
        int y = HIWORD(lParam) / GRID_SIZE * GRID_SIZE;
        RemoveBlockAtPosition(x, y); // Remove block at the clicked position
        InvalidateRect(hwnd, NULL, TRUE); // Refresh window
        return 0;
    }
    case WM_KEYDOWN: {// KEY CONTROL INPUTS

        switch (wParam) {
        case '1':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 1;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 2;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 3;
            }
            break;

        case '2':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 4;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 5;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 6;
            }
            break;

        case '3':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 7;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 8;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 9;
            }
            break;

        case '4':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 10;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 11;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 12;
            }
            break;

        case '5':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 13;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 14;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 15;
            }
            break;

        case '6':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 16;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 17;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 18;
            }
            break;

        case '7':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 19;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 20;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 21;
            }
            break;

        case '8':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 22;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 23;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 24;
            }
            break;

        case '9':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 25;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 26;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 27;
            }
            break;

        case '0':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 28;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 29;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 30;
            }
            break;

        case VK_OEM_MINUS:  // Detects the '-' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '-' is pressed
                currentBlockType = 31; // Set block type for Shift + '-'
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '-' is pressed
                currentBlockType = 32; // Set block type for Ctrl + '-'
            }
            else {
                // No modifier key is pressed with '-'
                currentBlockType = 33; // Set block type for just '-'
            }
            break;

        case VK_OEM_PLUS:  // Detects the '=' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '=' (normally '+' with shift) is pressed
                currentBlockType = 34; // Set block type for Shift + '='
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '=' (normally '+' with shift) is pressed
                currentBlockType = 35; // Set block type for Ctrl + '='
            }
            else {
                // No modifier key is pressed with '='
                currentBlockType = 36; // Set block type for just '='
            }
            break;

        case 'Q':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 37;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 38;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 39;
            }
            break;

        case 'W':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 40;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 41;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 42;
            }
            break;

        case 'E':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 43;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 44;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 45;
            }
            break;

        case 'R':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 46;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 47;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 48;
            }
            break;

        case 'T':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 49;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 50;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 51;
            }
            break;

        case 'Y':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 52;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 53;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 54;
            }
            break;

        case 'U':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 55;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 56;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 57;
            }
            break;

        case 'I':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 58;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 59;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 60;
            }
            break;

        case 'O':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 61;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 62;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 63;
            }
            break;

        case 'P':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 64;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 65;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 66;
            }
            break;

        case VK_OEM_4:  // Detects the '[' key (Shift gives '{')
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                currentBlockType = 67; // Shift + '[' which gives '{'
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                currentBlockType = 68; // Ctrl + '['
            }
            else {
                currentBlockType = 69; // Just '['
            }
            break;

        case VK_OEM_6:  // Detects the ']' key (Shift gives '}')
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                currentBlockType = 70; // Shift + ']' which gives '}'
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                currentBlockType = 71; // Ctrl + ']'
            }
            else {
                currentBlockType = 72; // Just ']'
            }
            break;

        case 'A':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 73;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 74;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 75;
            }
            break;

        case 'S':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 76;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 77;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 78;
            }
            break;

        case 'D':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 79;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 80;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 81;
            }
            break;

        case 'F':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 82;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 83;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 84;
            }
            break;

        case 'G':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 85;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 86;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 87;
            }
            break;

        case 'H':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 88;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 89;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 90;
            }
            break;

        case 'J':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 91;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 92;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 93;
            }
            break;

        case 'K':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 94;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 95;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 96;
            }
            break;

        case 'L':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 97;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 98;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 99;
            }
            break;

        case VK_OEM_1:  // Detects the ''' key (single quote) (Shift gives '"')
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                currentBlockType = 100; // Shift + ''' which gives '"'
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                currentBlockType = 101; // Ctrl + '''
            }
            else {
                currentBlockType = 102; // Just '''
            }
            break;

        case VK_OEM_7:  // Detects the ''' key (single quote) (Shift gives '"')
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                currentBlockType = 103; // Shift + ''' which gives '"'
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                currentBlockType = 104; // Ctrl + '''
            }
            else {
                currentBlockType = 105; // Just '''
            }
            break;

        case 'Z':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 106;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 107;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 108;
            }
            break;

        case 'X':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 109;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 110;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 111;
            }
            break;

        case 'C':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 112;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 113;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 114;
            }
            break;

        case 'V':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 115;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 116;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 117;
            }
            break;

        case 'B':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 118;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 119;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 120;
            }
            break;

        case 'N':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 121;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 122;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 123;
            }
            break;

        case 'M':  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 124;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 125;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 126;
            }
            break;

        case VK_OEM_COMMA:  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 127;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 128;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 129;
            }
            break;

        case VK_OEM_PERIOD:  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 130;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 131;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 132;
            }
            break;

        case VK_OEM_2:  // Detects the '1' key
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                // Shift + '1' is pressed
                currentBlockType = 133;
            }
            else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                // Ctrl + '1' is pressed
                currentBlockType = 134;
            }
            else {
                // No modifier key is pressed with '1'
                currentBlockType = 135;
            }
            break;




        case VK_OEM_3:  // The tilde (`~`) key
            if (isFullscreenMenuOpen) {
                CloseFullscreenMenu();
            }
            else {
                OpenFullscreenMenu();
            }
            return 0;

        case VK_F11: // Toggle fullscreen mode
            isFullscreen = !isFullscreen;
            if (isFullscreen) {
                SetFullscreen(hwnd);
            }
            else {
                SetWindowed(hwnd);
            }
            return 0;

        case VK_ESCAPE:
            if (isMenuOpen) {
                CloseMenu();
            }
            else {
                OpenMenu();
            }
            InvalidateRect(hwnd, NULL, TRUE); // Refresh window
            return 0;

        }
        InvalidateRect(hwnd, NULL, FALSE); // Refresh window-- 11 and beyond unlisted intentionally
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam) / GRID_SIZE * GRID_SIZE;
        int y = HIWORD(lParam) / GRID_SIZE * GRID_SIZE;

        // Remove any block that may already exist at the clicked position
        RemoveBlockAtPosition(x, y);

        // Create a new block at the clicked position with the current block type
        Block newBlock = { x, y, currentBlockType };





        // Add the new block to the game's block list
        blocks.push_back(newBlock);
        InvalidateRect(hwnd, NULL, TRUE);  // Refresh the window to show the new block
        return 0;
    }

    case WM_TIMER:
        if (!isMenuOpen) {
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;

    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam); // Default message handling
}

//
Bitmap* CreateCanopyLeavesTexture(int width, int height, const Color& leafColor1, const Color& leafColor2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(leafColor2);  // Darker green as background
    SolidBrush leafBrush(leafColor1);  // Lighter green for leaves
    for (int y = 0; y < height; y += 2) {
        for (int x = (y / 2) % 2 == 0 ? 0 : 1; x < width; x += 2) {
            graphics.FillRectangle(&leafBrush, x, y, 2, 2);
        }
    }
    return bitmap;
}
Bitmap* CreateDesertSandTexture(int width, int height, const Color& bgColor, const Color& grainColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    SolidBrush grainBrush(grainColor);  // Wheat-colored grains
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if ((x + y) % 3 == 0) {
                graphics.FillRectangle(&grainBrush, x, y, 1, 1);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateLilyPadsTexture(int width, int height, const Color& padColor, const Color& waterColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(waterColor);  // Blue water background
    SolidBrush padBrush(padColor);  // Green lily pads
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            graphics.FillEllipse(&padBrush, x - 4, y - 4, 8, 8);
        }
    }
    return bitmap;
}
Bitmap* CreateBrickPathTexture(int width, int height, const Color& brickColor, const Color& mortarColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(mortarColor);  // Gray mortar background
    SolidBrush brickBrush(brickColor);  // Reddish-brown bricks
    int brickWidth = 8;
    int brickHeight = 4;
    int mortarThickness = 2;
    for (int y = 0; y < height; y += brickHeight + mortarThickness) {
        int xOffset = (y / (brickHeight + mortarThickness)) % 2 == 0 ? 0 : (brickWidth + mortarThickness) / 2;
        for (int x = xOffset; x < width; x += brickWidth + mortarThickness) {
            graphics.FillRectangle(&brickBrush, x, y, brickWidth, brickHeight);
        }
    }
    return bitmap;
}
Bitmap* CreateAlgaeTexture(int width, int height, const Color& bgColor, const Color& algaeColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Olive background
    SolidBrush algaeBrush(algaeColor);  // Sea-green algae
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if ((x * y) % 5 == 0) {
                graphics.FillRectangle(&algaeBrush, x, y, 2, 2);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateFlowersTexture(int width, int height, const Color& flowerColor1, const Color& flowerColor2, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    SolidBrush flowerBrush1(flowerColor1);  // Red flowers
    SolidBrush flowerBrush2(flowerColor2);  // Yellow flowers
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        for (int x = 0; x < width; x += spacing) {
            SolidBrush* brush = ((x / spacing + y / spacing) % 2 == 0) ? &flowerBrush1 : &flowerBrush2;
            graphics.FillEllipse(brush, x - 2, y - 2, 4, 4);
        }
    }
    return bitmap;
}
Bitmap* CreateHoleInGroundTexture(int width, int height, const Color& holeColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Brown background
    SolidBrush holeBrush(holeColor);  // Black hole
    graphics.FillEllipse(&holeBrush, width / 4, height / 4, width / 2, height / 2);
    return bitmap;
}
Bitmap* CreateTrashTexture(int width, int height, const Color& bgColor, const Color& lineColor1, const Color& lineColor2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Gray background
    Pen linePen1(lineColor1, 1.0f);  // Brown lines
    Pen linePen2(lineColor2, 1.0f);  // Rust lines
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            if ((x + y) % 4 == 0) {
                graphics.DrawLine(&linePen1, x, y, x + 2, y + 2);
            }
            else if ((x - y) % 4 == 0) {
                graphics.DrawLine(&linePen2, x, y + 2, x + 2, y);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateBushTexture(int width, int height, const Color& leafColor1, const Color& leafColor2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(leafColor2);  // Darker green background
    SolidBrush leafBrush(leafColor1);  // Lighter green leaves
    for (int y = 0; y < height; y += 2) {
        for (int x = (y / 2) % 2 == 0 ? 0 : 1; x < width; x += 2) {
            graphics.FillRectangle(&leafBrush, x, y, 2, 2);
        }
    }
    return bitmap;
}
Bitmap* CreateGasolinePuddleTexture(int width, int height, const Color& bgColor, const Color& oilColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Gray background
    Pen oilPen(oilColor, 1.0f);  // Amber oil
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            graphics.DrawLine(&oilPen, x, y, x + 2, y + 2);
            graphics.DrawLine(&oilPen, x + 2, y + 2, x + 4, y);
        }
    }
    return bitmap;
}
Bitmap* CreateTNTTexture(int width, int height, const Color& bodyColor, const Color& fuseColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bodyColor);  // Red background
    Pen fusePen(fuseColor, 1.0f);  // Black outlines
    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            graphics.DrawRectangle(&fusePen, x, y, 8, 8);
        }
    }
    return bitmap;
}
Bitmap* CreateRockTexture(int width, int height, const Color& bgColor, const Color& grainColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Slate gray background
    SolidBrush grainBrush(grainColor);  // Gray grains
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if ((x + y) % 2 == 0) {
                graphics.FillRectangle(&grainBrush, x, y, 1, 1);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateTallGrassTexture(int width, int height, const Color& grassColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    Pen grassPen(grassColor, 1.0f);  // Bright green grass
    for (int x = 0; x < width; x += 2) {
        graphics.DrawLine(&grassPen, x, 0, x, height - 1);
    }
    return bitmap;
}
Bitmap* CreateShortGrassTexture(int width, int height, const Color& grassColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    SolidBrush grassBrush(grassColor);  // Lime green grass
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            if ((x + y) % 8 == 0) {
                graphics.FillRectangle(&grassBrush, x, y, 2, 2);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateBurntTreeTexture(int width, int height, const Color& bgColor, const Color& charColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Gray background
    Pen charPen(charColor, 1.0f);  // Black char marks
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            if ((x * y) % 3 == 0) {
                graphics.DrawLine(&charPen, x, y, x + 2, y + 2);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateSeawaterTexture(int width, int height, const Color& waveColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Steel blue background
    Pen wavePen(waveColor, 1.0f);  // Deep sea blue waves
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            graphics.DrawLine(&wavePen, x, y + (x % 2), x + 4, y + ((x + 2) % 2));
        }
    }
    return bitmap;
}
Bitmap* CreateFreshwaterTexture(int width, int height, const Color& waveColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Sky blue background
    Pen wavePen(waveColor, 1.0f);  // Light blue waves
    for (int y = 0; y < height; y += 2) {
        graphics.DrawLine(&wavePen, 0, y, width, y);
    }
    return bitmap;
}
Bitmap* CreateFireTexture(int width, int height, const Color& flameColor1, const Color& flameColor2, const Color& flameColor3) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(Color(0, 0, 0, 0));  // Transparent background
    SolidBrush flameBrush1(flameColor1);  // Red flames
    SolidBrush flameBrush2(flameColor2);  // Orange flames
    SolidBrush flameBrush3(flameColor3);  // Gold flames
    int flameHeight = 4;
    for (int y = 0; y < height; y += flameHeight) {
        for (int x = 0; x < width; x += flameHeight) {
            SolidBrush* brush;
            int colorIndex = (x / flameHeight + y / flameHeight) % 3;
            if (colorIndex == 0) brush = &flameBrush1;
            else if (colorIndex == 1) brush = &flameBrush2;
            else brush = &flameBrush3;
            Point points[3] = { Point(x, y + flameHeight), Point(x + flameHeight / 2, y), Point(x + flameHeight, y + flameHeight) };
            graphics.FillPolygon(brush, points, 3);
        }
    }
    return bitmap;
}
Bitmap* CreateGrasslandsPattern(int width, int height, const Color& grassColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Lime green background
    Pen grassPen(grassColor, 1.0f);  // Bright green stripes
    for (int x = 0; x < width; x += 4) {
        graphics.DrawLine(&grassPen, x, 0, x, height - 1);
    }
    return bitmap;
}
Bitmap* CreateBoulderFieldPattern(int width, int height, const Color& bgColor, const Color& color1) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Light gray background
    SolidBrush boulderBrush(color1);  // Slate gray boulders
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            graphics.FillEllipse(&boulderBrush, x - 3, y - 3, 6, 6);
        }
    }
    return bitmap;
}
Bitmap* CreateSandDunesTexture(int width, int height, const Color& rippleColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Wheat background
    Pen ripplePen(rippleColor, 1.0f);  // Tan ripples
    int rippleSpacing = 4;
    for (int y = 0; y < height; y += rippleSpacing) {
        for (int x = 0; x < width; x += rippleSpacing) {
            int offset = (x + y) % 4;
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + offset)),
                PointF(static_cast<Gdiplus::REAL>(x + rippleSpacing), static_cast<Gdiplus::REAL>(y + rippleSpacing - offset))
            };
            graphics.DrawLine(&ripplePen, points[0], points[1]);
        }
    }
    return bitmap;
}
Bitmap* CreateCharredStumpTexture(int width, int height, const Color& bgColor, const Color& charColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Charcoal gray background
    Pen charPen(charColor, 1.0f);  // Black char marks
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            if ((x * y) % 3 == 0) {
                graphics.DrawLine(&charPen, x, y, x + 2, y + 2);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateAutumnTreeTexture(int width, int height, const Color& color1, const Color& color2, const Color& color3) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(Color(0, 0, 0, 0));  // Transparent background
    SolidBrush brush1(color1);  // Red
    SolidBrush brush2(color2);  // Orange
    SolidBrush brush3(color3);  // Gold
    int size = 4;
    for (int y = 0; y < height; y += size) {
        for (int x = 0; x < width; x += size) {
            int colorIndex = (x / size + y / size) % 3;
            SolidBrush* brush = &brush1;
            if (colorIndex == 1) brush = &brush2;
            else if (colorIndex == 2) brush = &brush3;
            graphics.FillRectangle(brush, x, y, size, size);
        }
    }
    return bitmap;
}
Bitmap* CreateWavyGrassPatchTexture(int width, int height, const Color& grassColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Lime green background
    Pen grassPen(grassColor, 1.0f);  // Bright green stripes
    for (int x = 0; x < width; x += 4) {
        graphics.DrawLine(&grassPen, x, 0, x, height - 1);
    }
    return bitmap;
}
Bitmap* CreateLargeBoulderTexture(int width, int height, const Color& bgColor, const Color& boulderColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Light gray background
    SolidBrush boulderBrush(boulderColor);  // Slate gray boulders
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            graphics.FillEllipse(&boulderBrush, x - 3, y - 3, 6, 6);
        }
    }
    return bitmap;
}
Bitmap* CreateThornyShrubTexture(int width, int height, const Color& leafColor, const Color& lineColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(leafColor);  // Green background
    Pen linePen(lineColor, 1.0f);  // Black lines
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            if ((x + y) % 8 == 0) {
                graphics.DrawLine(&linePen, x, y, x + 2, y + 2);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateWildflowerPatchTexture(int width, int height, const Color& flowerColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    SolidBrush flowerBrush(flowerColor);  // Red flowers
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        for (int x = 0; x < width; x += spacing) {
            graphics.FillEllipse(&flowerBrush, x - 2, y - 2, 4, 4);
        }
    }
    return bitmap;
}
Bitmap* CreateForestCanopyTexture(int width, int height, const Color& leafColor1, const Color& leafColor2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(leafColor2);  // Darker green as background
    SolidBrush leafBrush(leafColor1);  // Lighter green for leaves
    for (int y = 0; y < height; y += 2) {
        for (int x = (y / 2) % 2 == 0 ? 0 : 1; x < width; x += 2) {
            graphics.FillRectangle(&leafBrush, x, y, 2, 2);
        }
    }
    return bitmap;
}
Bitmap* CreateRiverbedPebblesPattern(int width, int height, const Color& pebbleColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Brown background
    SolidBrush pebbleBrush(pebbleColor);  // Gray pebbles
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            int size = (x + y) % 2 == 0 ? 4 : 6;  // Deterministic size variation
            graphics.FillEllipse(&pebbleBrush, x - size / 2, y - size / 2, size, size);
        }
    }
    return bitmap;
}
Bitmap* CreateGrassTuftPattern(int width, int height, const Color& tuftColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Lime green background
    SolidBrush tuftBrush(tuftColor);  // Bright green tufts
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            graphics.FillEllipse(&tuftBrush, x - 2, y - 2, 4, 4);
        }
    }
    return bitmap;
}
Bitmap* CreateRockyOutcropPattern(int width, int height, const Color& color1, const Color& color2, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Slate gray background
    int diamondSize = 4;
    for (int y = 0; y < height; y += diamondSize) {
        for (int x = 0; x < width; x += diamondSize) {
            SolidBrush brush(((x / diamondSize) + (y / diamondSize)) % 2 == 0 ? color1 : color2);
            PointF points[4] = {
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y)),
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize), static_cast<Gdiplus::REAL>(y + diamondSize / 2)),
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y + diamondSize)),
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + diamondSize / 2))
            };
            graphics.FillPolygon(&brush, points, 4);
        }
    }
    return bitmap;
}
Bitmap* CreateSandyRipplePattern(int width, int height, const Color& rippleColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    Pen ripplePen(rippleColor, 1.0f);  // Wheat-colored ripples

    int period = 8;
    float amplitude = 2.0f;
    for (int y = 0; y < height; y += period) {
        for (int x = 0; x < width; x++) {
            int offset = static_cast<int>(amplitude * sin(2.0f * 3.14159265f * x / period));
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + offset)),
                PointF(static_cast<Gdiplus::REAL>(x + 1), static_cast<Gdiplus::REAL>(y + offset))
            };
            graphics.DrawLines(&ripplePen, points, 2);
        }
    }

    return bitmap;
}
Bitmap* CreateLeafVeinPattern(int width, int height, const Color& bgColor, const Color& veinColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Forest green background
    Pen veinPen(veinColor, 1.0f);  // Moss green veins

    int lineSpacing = 4;
    for (int y = 0; y < height; y += lineSpacing) {
        for (int x = (y / lineSpacing) % 2 == 0 ? 0 : lineSpacing / 2; x < width; x += lineSpacing) {
            // Diagonal lines for veins, alternating direction
            if ((x / lineSpacing + y / lineSpacing) % 2 == 0) {
                graphics.DrawLine(&veinPen, x, y, x + lineSpacing, y + lineSpacing);
            }
            else {
                graphics.DrawLine(&veinPen, x + lineSpacing, y, x, y + lineSpacing);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateMossyStonePattern(int width, int height, const Color& mossColor, const Color& stoneColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Slate gray background
    SolidBrush mossBrush(mossColor);  // Moss green
    SolidBrush stoneBrush(stoneColor);  // Gray stones
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            if ((x / spacing + y / spacing) % 2 == 0) {
                graphics.FillEllipse(&mossBrush, x - 2, y - 2, 4, 4);
            }
            else {
                graphics.FillEllipse(&stoneBrush, x - 2, y - 2, 4, 4);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateWetMudPattern(int width, int height, const Color& wetColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Brown background
    SolidBrush wetBrush(wetColor);  // Saddle brown wet patches
    int blockSize = 4;
    for (int y = 0; y < height; y += blockSize) {
        for (int x = 0; x < width; x += blockSize) {
            if ((x / blockSize + y / blockSize) % 2 == 0) {
                graphics.FillRectangle(&wetBrush, x, y, blockSize, blockSize);
            }
        }
    }
    return bitmap;
}
Bitmap* CreateFernFrondPattern(int width, int height, const Color& frondColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Lime green background
    Pen frondPen(frondColor, 1.0f);  // Bright green fronds
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        for (int x = (y / spacing) % 2 == 0 ? 0 : spacing / 2; x < width; x += spacing) {
            graphics.DrawBezier(&frondPen,
                Point(x, y),
                Point(x + 2, y + 4),
                Point(x - 2, y + 6),
                Point(x, y + 8));
        }
    }
    return bitmap;
}
Bitmap* CreateSnowDriftPattern(int width, int height, const Color& snowColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Light gray background
    SolidBrush snowBrush(snowColor);  // White snow
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            int size = (x + y) % 2 == 0 ? 4 : 6;  // Deterministic size variation
            graphics.FillEllipse(&snowBrush, x - size / 2, y - size / 2, size, size);
        }
    }
    return bitmap;
}
Bitmap* CreateVolcanicAshPattern(int width, int height, const Color& ashColor, const Color& crackColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(ashColor);  // Charcoal gray background
    Pen crackPen(crackColor, 1.0f);  // Vermilion cracks

    // Vertical cracks
    for (int x = 8; x < width; x += 16) {
        graphics.DrawLine(&crackPen, x, 0, x, height - 1);
    }

    // Horizontal cracks
    for (int y = 8; y < height; y += 16) {
        graphics.DrawLine(&crackPen, 0, y, width - 1, y);
    }

    return bitmap;
}
Bitmap* CreateGrassChevronPattern(int width, int height, const Color& primaryColor, const Color& secondaryColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    // Size adjustments for 32x32
    int chevronWidth = 8; // 32 / 4
    int chevronHeight = 8; // 32 / 4

    // Iterate over the grid to fill with chevrons
    for (int y = 0; y < height; y += chevronHeight) {
        for (int x = 0; x < width; x += chevronWidth) {
            SolidBrush brush((x / chevronWidth + y / chevronHeight) % 2 == 0 ? primaryColor : secondaryColor);

            // Adjust chevron points for tessellation
            Point points[3] = {
                Point(x, y),
                Point(x + chevronWidth / 2, y + chevronHeight),
                Point(x + chevronWidth, y)
            };

            graphics.FillPolygon(&brush, points, 3);
        }
    }

    return bitmap;
}
Bitmap* CreateRockyDiamondPattern(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int diamondSize = 4;
    for (int y = 0; y < height; y += diamondSize) {
        for (int x = 0; x < width; x += diamondSize) {
            SolidBrush brush(((x / diamondSize) + (y / diamondSize)) % 2 == 0 ? color1 : color2);

            PointF points[4] = {
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y)),
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize), static_cast<Gdiplus::REAL>(y + diamondSize / 2)),
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y + diamondSize)),
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + diamondSize / 2))
            };
            graphics.FillPolygon(&brush, points, 4);
        }
    }

    return bitmap;
}
Bitmap* CreateDirtHerringbonePattern(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int brickWidth = 10;  // 32 / 3 ≈ 10
    int brickHeight = 5;  // 32 / 6 ≈ 5

    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 2; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? color1 : color2);
            graphics.FillRectangle(&brush, x, y, brickWidth / 2, brickHeight);
            graphics.FillRectangle(&brush, x + brickWidth / 2, y + brickHeight / 2, brickWidth / 2, brickHeight);
        }
    }

    return bitmap;
}
Bitmap* CreateBarkPatchPattern(int width, int height, const Color& barkColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Brown background

    SolidBrush barkBrush(barkColor);  // Saddle brown bark
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if ((x + y) % 4 == 0 || (x * y) % 16 == 0) {
                graphics.FillRectangle(&barkBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateLeafLitterPattern(int width, int height, const Color& leafColor, const Color& decayColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background

    SolidBrush leafBrush(leafColor);  // Moss green leaves
    SolidBrush decayBrush(decayColor);  // Brown decay
    Pen leafPen(leafColor, 1.0f);  // Thin green lines for skeleton

    // Simulate leaves in various stages of decay on an 8x8 grid
    for (int y = 0; y < height; y += 8) {
        for (int x = 0; x < width; x += 8) {
            if ((x + y) % 16 == 0) {
                graphics.FillEllipse(&leafBrush, x, y, 8, 8);  // Full leaf
                graphics.FillEllipse(&decayBrush, x + 2, y + 2, 4, 4);  // Decay effect
            }
            else {
                // Skeletonized leaf
                graphics.DrawEllipse(&leafPen, x, y, 8, 8);
                graphics.DrawLine(&leafPen, x, y + 4, x + 8, y + 4);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateSnowPatchPattern(int width, int height, const Color& snowColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Light gray background
    SolidBrush snowBrush(snowColor);  // White snow
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        int xOffset = (y / spacing) % 2 == 0 ? 0 : spacing / 2;
        for (int x = xOffset; x < width; x += spacing) {
            Point points[4] = {
                Point(x, y),
                Point(x + 4, y + 4),
                Point(x + 8, y),
                Point(x, y)
            };
            graphics.FillPolygon(&snowBrush, points, 4);
        }
    }
    return bitmap;
}
Bitmap* CreateVolcanicCraterPattern(int width, int height, const Color& ashColor, const Color& crackColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(ashColor);  // Charcoal gray background
    Pen crackPen(crackColor, 1.0f);  // Vermilion cracks

    // Vertical cracks
    for (int x = 4; x < width; x += 8) {
        graphics.DrawLine(&crackPen, x, 0, x, height - 1);
    }

    // Horizontal cracks
    for (int y = 4; y < height; y += 8) {
        graphics.DrawLine(&crackPen, 0, y, width - 1, y);
    }

    return bitmap;
}
Bitmap* CreateFernPatchPattern(int width, int height, const Color& frondColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Lime green background
    Pen frondPen(frondColor, 1.0f);  // Bright green fronds
    int spacing = 8;
    for (int y = 0; y < height; y += spacing) {
        for (int x = (y / spacing) % 2 == 0 ? 0 : spacing / 2; x < width; x += spacing) {
            graphics.DrawBezier(&frondPen,
                Point(x, y),
                Point(x + 2, y + 4),
                Point(x - 2, y + 6),
                Point(x, y + 8));
        }
    }
    return bitmap;
}
Bitmap* CreateEnchantedForestGladePattern(int width, int height, const Color& bgColor, const Color& veinColor, const Color& flowerColor, const Color& frondColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Lime green background
    Pen veinPen(veinColor, 1.0f);  // Moss green veins
    SolidBrush flowerBrush(flowerColor);  // Yellow flowers
    Pen frondPen(frondColor, 1.0f);  // Bright green fronds

    // Leaf veins (4x4 grid)
    int veinSpacing = 4;
    for (int y = 0; y < height; y += veinSpacing) {
        for (int x = (y / veinSpacing) % 2 == 0 ? 0 : veinSpacing / 2; x < width; x += veinSpacing) {
            if ((x / veinSpacing + y / veinSpacing) % 2 == 0) {
                graphics.DrawLine(&veinPen, x, y, x + veinSpacing, y + veinSpacing);
            }
            else {
                graphics.DrawLine(&veinPen, x + veinSpacing, y, x, y + veinSpacing);
            }
        }
    }

    // Flower petals (8x8 grid, concentric)
    int flowerSpacing = 8;
    for (int y = 0; y < height; y += flowerSpacing) {
        for (int x = (y / flowerSpacing) % 2 == 0 ? 0 : flowerSpacing / 2; x < width; x += flowerSpacing) {
            int centerX = x + flowerSpacing / 2;
            int centerY = y + flowerSpacing / 2;
            for (int r = 2; r < flowerSpacing / 2; r += 2) {
                if (abs((x - centerX) / r) == abs((y - centerY) / r)) {
                    graphics.FillEllipse(&flowerBrush, x - 2, y - 2, 4, 4);
                }
            }
        }
    }

    // Fern fronds (16x16 grid)
    int frondSpacing = 16;
    for (int y = 0; y < height; y += frondSpacing) {
        for (int x = (y / frondSpacing) % 2 == 0 ? 0 : frondSpacing / 2; x < width; x += frondSpacing) {
            graphics.DrawBezier(&frondPen,
                Point(x, y),
                Point(x + 4, y + 8),
                Point(x - 4, y + 12),
                Point(x, y + 16));
        }
    }

    return bitmap;
}
Bitmap* CreateDesertMirageRipplePattern(int width, int height, const Color& rippleColor, const Color& bgColor, const Color& diamondColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    Pen ripplePen(rippleColor, 1.0f);  // Wheat-colored ripples
    SolidBrush diamondBrush(diamondColor);  // Light gray diamonds

    // Sandy ripples (8-pixel period)
    int period = 8;
    float amplitude = 2.0f;
    for (int y = 0; y < height; y += period) {
        for (int x = 0; x < width; x++) {
            int offset = static_cast<int>(amplitude * sin(2.0f * 3.14159265f * x / period));
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + offset)),
                PointF(static_cast<Gdiplus::REAL>(x + 1), static_cast<Gdiplus::REAL>(y + offset))
            };
            graphics.DrawLines(&ripplePen, points, 2);
        }
    }

    // Diamond heat distortions (4x4 grid)
    int diamondSize = 4;
    for (int y = 0; y < height; y += diamondSize) {
        for (int x = 0; x < width; x += diamondSize) {
            if (((x / diamondSize) + (y / diamondSize)) % 2 == 0) {
                PointF points[4] = {
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize), static_cast<Gdiplus::REAL>(y + diamondSize / 2)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y + diamondSize)),
                    PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + diamondSize / 2))
                };
                graphics.FillPolygon(&diamondBrush, points, 4);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateCraggyMossyCliffPattern(int width, int height, const Color& mossColor, const Color& stoneColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Slate gray background
    SolidBrush mossBrush(mossColor);  // Moss green
    SolidBrush stoneBrush(stoneColor);  // Gray stones

    // Mossy chevrons (8x8 grid)
    int chevronWidth = 8;
    int chevronHeight = 8;
    for (int y = 0; y < height; y += chevronHeight) {
        int xOffset = (y / chevronHeight) % 2 == 0 ? 0 : chevronWidth / 2;
        for (int x = xOffset; x < width; x += chevronWidth) {
            SolidBrush brush((x / chevronWidth + y / chevronHeight) % 2 == 0 ? mossColor : bgColor);
            Point points[3] = {
                Point(x, y),
                Point(x + chevronWidth / 2, y + chevronHeight),
                Point(x + chevronWidth, y)
            };
            graphics.FillPolygon(&brush, points, 3);
        }
    }

    // Stone mosaics (4x4 grid)
    int stoneSize = 4;
    for (int y = 0; y < height; y += stoneSize) {
        for (int x = 0; x < width; x += stoneSize) {
            if (((x / stoneSize) + (y / stoneSize)) % 2 == 1) {
                graphics.FillEllipse(&stoneBrush, x - 2, y - 2, stoneSize, stoneSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateTwilightMeadowBloomPattern(int width, int height, const Color& grassColor, const Color& tuftColor, const Color& flowerColor, const Color& glowColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(grassColor);  // Bright green background
    SolidBrush tuftBrush(tuftColor);  // Lime green tufts
    SolidBrush flowerBrush(flowerColor);  // Yellow flowers
    SolidBrush glowBrush(glowColor);  // Sky blue glow

    // Grass tufts (8x8 staggered grid)
    int tuftSpacing = 8;
    for (int y = 0; y < height; y += tuftSpacing) {
        int xOffset = (y / tuftSpacing) % 2 == 0 ? 0 : tuftSpacing / 2;
        for (int x = xOffset; x < width; x += tuftSpacing) {
            graphics.FillEllipse(&tuftBrush, x - 2, y - 2, 4, 4);
        }
    }

    // Flower dots (8x8 grid, concentric)
    int flowerSpacing = 8;
    for (int y = 0; y < height; y += flowerSpacing) {
        for (int x = (y / flowerSpacing) % 2 == 0 ? 0 : flowerSpacing / 2; x < width; x += flowerSpacing) {
            int centerX = x + flowerSpacing / 2;
            int centerY = y + flowerSpacing / 2;
            for (int r = 1; r < flowerSpacing / 2; r += 2) {
                if (abs((x - centerX) / r) == abs((y - centerY) / r)) {
                    graphics.FillEllipse(&flowerBrush, x - 1, y - 1, 2, 2);
                }
            }
        }
    }

    // Twilight glow (4x4 checkerboard)
    int glowSize = 4;
    for (int y = 0; y < height; y += glowSize) {
        for (int x = 0; x < width; x += glowSize) {
            if (((x / glowSize) + (y / glowSize)) % 2 == 0) {
                graphics.FillRectangle(&glowBrush, x, y, glowSize, glowSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateIcyRiverbedFusionPattern(int width, int height, const Color& pebbleColor, const Color& iceColor, const Color& fractureColor, const Color& waveColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(iceColor);  // Sky blue background
    SolidBrush pebbleBrush(pebbleColor);  // Gray pebbles
    SolidBrush fractureBrush(fractureColor);  // White fractures
    Pen wavePen(waveColor, 1.0f);  // Deep sea blue waves

    // Riverbed pebbles (8x8 staggered grid)
    int pebbleSpacing = 8;
    for (int y = 0; y < height; y += pebbleSpacing) {
        int xOffset = (y / pebbleSpacing) % 2 == 0 ? 0 : pebbleSpacing / 2;
        for (int x = xOffset; x < width; x += pebbleSpacing) {
            int size = (x + y) % 2 == 0 ? 4 : 6;  // Deterministic size variation
            graphics.FillEllipse(&pebbleBrush, x - size / 2, y - size / 2, size, size);
        }
    }

    // Icy fractures (4x4 grid)
    int fractureSize = 4;
    for (int y = 0; y < height; y += fractureSize) {
        for (int x = 0; x < width; x += fractureSize) {
            if (((x / fractureSize) + (y / fractureSize)) % 2 == 0) {
                Point points[4] = {
                    Point(x, y),
                    Point(x + 2, y + 2),
                    Point(x + 4, y),
                    Point(x, y)
                };
                graphics.FillPolygon(&fractureBrush, points, 4);
            }
        }
    }

    // Icy waves (8-pixel period)
    int period = 8;
    float amplitude = 2.0f;
    for (int y = 0; y < height; y += period) {
        for (int x = 0; x < width; x++) {
            int offset = static_cast<int>(amplitude * sin(2.0f * 3.14159265f * x / period));
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + offset)),
                PointF(static_cast<Gdiplus::REAL>(x + 1), static_cast<Gdiplus::REAL>(y + offset))
            };
            graphics.DrawLines(&wavePen, points, 2);
        }
    }

    return bitmap;
}
Bitmap* CreateShadyGrovePattern(int width, int height, const Color& bgColor, const Color& lightColor, const Color& shadowColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Forest green background
    SolidBrush lightBrush(lightColor);  // Lime green light patches
    Pen shadowPen(shadowColor, 1.0f);  // Black shadows

    // Sunlight patches (4x4 checkerboard)
    int patchSize = 4;
    for (int y = 0; y < height; y += patchSize) {
        for (int x = 0; x < width; x += patchSize) {
            if (((x / patchSize) + (y / patchSize)) % 2 == 0) {
                graphics.FillRectangle(&lightBrush, x, y, patchSize, patchSize);
            }
        }
    }

    // Shadow veins (4x4 diagonal grid)
    int veinSpacing = 4;
    for (int y = 0; y < height; y += veinSpacing) {
        for (int x = (y / veinSpacing) % 2 == 0 ? 0 : veinSpacing / 2; x < width; x += veinSpacing) {
            if ((x / veinSpacing + y / veinSpacing) % 2 == 0) {
                graphics.DrawLine(&shadowPen, x, y, x + veinSpacing, y + veinSpacing);
            }
            else {
                graphics.DrawLine(&shadowPen, x + veinSpacing, y, x, y + veinSpacing);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateMistyHighlandPattern(int width, int height, const Color& bgColor, const Color& tuftColor, const Color& mistColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Light gray background
    SolidBrush tuftBrush(tuftColor);  // Bright green tufts
    Color denseMistColor(96, mistColor.GetR(), mistColor.GetG(), mistColor.GetB());  // Semi-transparent gray mist
    SolidBrush mistBrush(denseMistColor);  // Mist layer

    // Grass tufts (8x8 staggered grid)
    int tuftSpacing = 8;
    for (int y = 0; y < height; y += tuftSpacing) {
        int xOffset = (y / tuftSpacing) % 2 == 0 ? 0 : tuftSpacing / 2;
        for (int x = xOffset; x < width; x += tuftSpacing) {
            graphics.FillEllipse(&tuftBrush, x - 2, y - 2, 4, 4);
        }
    }

    // Misty layer (4x4 grid)
    int mistSize = 4;
    for (int y = 0; y < height; y += mistSize) {
        for (int x = 0; x < width; x += mistSize) {
            if (((x / mistSize) + (y / mistSize)) % 2 == 0) {
                graphics.FillEllipse(&mistBrush, x - 2, y - 2, mistSize, mistSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreatePebbledStreambedPattern(int width, int height, const Color& pebbleColor, const Color& waterColor, const Color& diamondColor, const Color& bgColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Sky blue background
    SolidBrush pebbleBrush(pebbleColor);  // Gray pebbles
    Pen waterPen(waterColor, 1.0f);  // Deep sea blue water
    SolidBrush diamondBrush(diamondColor);  // Light gray diamonds

    // Pebbles (8x8 staggered grid)
    int pebbleSpacing = 8;
    for (int y = 0; y < height; y += pebbleSpacing) {
        int xOffset = (y / pebbleSpacing) % 2 == 0 ? 0 : pebbleSpacing / 2;
        for (int x = xOffset; x < width; x += pebbleSpacing) {
            int size = (x + y) % 2 == 0 ? 4 : 6;  // Deterministic size variation
            graphics.FillEllipse(&pebbleBrush, x - size / 2, y - size / 2, size, size);
        }
    }

    // Water waves (8-pixel period)
    int period = 8;
    float amplitude = 2.0f;
    for (int y = 0; y < height; y += period) {
        for (int x = 0; x < width; x++) {
            int offset = static_cast<int>(amplitude * sin(2.0f * 3.14159265f * x / period));
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + offset)),
                PointF(static_cast<Gdiplus::REAL>(x + 1), static_cast<Gdiplus::REAL>(y + offset))
            };
            graphics.DrawLines(&waterPen, points, 2);
        }
    }

    // Diamond stones (4x4 grid)
    int diamondSize = 4;
    for (int y = 0; y < height; y += diamondSize) {
        for (int x = 0; x < width; x += diamondSize) {
            if (((x / diamondSize) + (y / diamondSize)) % 2 == 1) {
                PointF points[4] = {
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize), static_cast<Gdiplus::REAL>(y + diamondSize / 2)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y + diamondSize)),
                    PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + diamondSize / 2))
                };
                graphics.FillPolygon(&diamondBrush, points, 4);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateAutumnalLeafFallPattern(int width, int height, const Color& bgColor, const Color& color1, const Color& color2, const Color& color3, const Color& veinColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Brown background
    SolidBrush brush1(color1);  // Red leaves
    SolidBrush brush2(color2);  // Orange leaves
    SolidBrush brush3(color3);  // Gold leaves
    Pen veinPen(veinColor, 1.0f);  // Black veins

    // Autumn leaves (8x8 staggered grid)
    int leafSpacing = 8;
    for (int y = 0; y < height; y += leafSpacing) {
        int xOffset = (y / leafSpacing) % 2 == 0 ? 0 : leafSpacing / 2;
        for (int x = xOffset; x < width; x += leafSpacing) {
            int colorIndex = (x / leafSpacing + y / leafSpacing) % 3;
            SolidBrush* brush = &brush1;
            if (colorIndex == 1) brush = &brush2;
            else if (colorIndex == 2) brush = &brush3;
            graphics.FillEllipse(brush, x - 2, y - 2, 4, 4);
        }
    }

    // Leaf veins (4x4 diagonal grid)
    int veinSpacing = 4;
    for (int y = 0; y < height; y += veinSpacing) {
        for (int x = (y / veinSpacing) % 2 == 0 ? 0 : veinSpacing / 2; x < width; x += veinSpacing) {
            if ((x / veinSpacing + y / veinSpacing) % 2 == 0) {
                graphics.DrawLine(&veinPen, x, y, x + veinSpacing, y + veinSpacing);
            }
            else {
                graphics.DrawLine(&veinPen, x + veinSpacing, y, x, y + veinSpacing);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateVolcanicLavaFlowPattern(int width, int height, const Color& ashColor, const Color& crackColor1, const Color& crackColor2, const Color& lavaColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(ashColor);  // Charcoal gray background
    Pen crackPen1(crackColor1, 1.0f);  // Vermilion cracks
    Pen crackPen2(crackColor2, 1.0f);  // Orange cracks
    SolidBrush lavaBrush(lavaColor);  // Saddle brown lava

    // Lava cracks (crosshatch pattern)
    for (int x = 8; x < width; x += 16) {
        graphics.DrawLine(&crackPen1, x, 0, x, height - 1);  // Vertical vermilion
    }
    for (int y = 8; y < height; y += 16) {
        graphics.DrawLine(&crackPen2, 0, y, width - 1, y);  // Horizontal orange
    }

    // Cooled lava (8x8 herringbone)
    int brickWidth = 8;
    int brickHeight = 8;
    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 2; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? lavaColor : ashColor);
            graphics.FillRectangle(&brush, x, y, brickWidth / 2, brickHeight);
            graphics.FillRectangle(&brush, x + brickWidth / 2, y + brickHeight / 2, brickWidth / 2, brickHeight);
        }
    }

    return bitmap;
}
Bitmap* CreateLuminescentFernGrovePattern(int width, int height, const Color& bgColor, const Color& frondColor, const Color& glowColor, const Color& gridColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Forest green background
    Pen frondPen(frondColor, 1.0f);  // Bright green fronds
    Color lightGlowColor(48, glowColor.GetR(), glowColor.GetG(), glowColor.GetB());  // Semi-transparent sky blue glow
    SolidBrush glowBrush(lightGlowColor);  // Bioluminescent glow
    Pen gridPen(gridColor, 1.0f);  // White grid lines

    // Fern tendrils (8x8 grid)
    int frondSpacing = 8;
    for (int y = 0; y < height; y += frondSpacing) {
        for (int x = (y / frondSpacing) % 2 == 0 ? 0 : frondSpacing / 2; x < width; x += frondSpacing) {
            graphics.DrawBezier(&frondPen,
                Point(x, y),
                Point(x + 2, y + 4),
                Point(x - 2, y + 6),
                Point(x, y + 8));
        }
    }

    // Bioluminescent glow (4x4 grid)
    int glowSize = 4;
    for (int y = 0; y < height; y += glowSize) {
        for (int x = 0; x < width; x += glowSize) {
            if (((x / glowSize) + (y / glowSize)) % 2 == 0) {
                graphics.FillEllipse(&glowBrush, x - 2, y - 2, glowSize, glowSize);
            }
        }
    }

    // Subtle grid glow (16x16 grid)
    int gridSpacing = 16;
    for (int i = 0; i <= width; i += gridSpacing) {
        graphics.DrawLine(&gridPen, i, 0, i, height - 1);  // Vertical
        graphics.DrawLine(&gridPen, 0, i, width - 1, i);  // Horizontal
    }

    return bitmap;
}
Bitmap* CreateCrystalineFrostMeadowPattern(int width, int height, const Color& bgColor, const Color& crystalColor, const Color& tuftColor, const Color& fractureColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Light gray background
    SolidBrush crystalBrush(crystalColor);  // White crystals
    SolidBrush tuftBrush(tuftColor);  // Bright green tufts
    Pen fracturePen(fractureColor, 1.0f);  // Sky blue fractures

    // Frost crystals (8x8 staggered grid)
    int crystalSize = 8;
    for (int y = 0; y < height; y += crystalSize) {
        int xOffset = (y / crystalSize) % 2 == 0 ? 0 : crystalSize / 2;
        for (int x = xOffset; x < width; x += crystalSize) {
            Point points[6] = {
                Point(x, y + crystalSize / 2),
                Point(x + crystalSize / 2, y),
                Point(x + crystalSize, y + crystalSize / 2),
                Point(x + crystalSize, y + crystalSize),
                Point(x + crystalSize / 2, y + crystalSize * 3 / 2),
                Point(x, y + crystalSize)
            };
            graphics.FillPolygon(&crystalBrush, points, 6);
        }
    }

    // Grass tufts (4x4 grid)
    int tuftSize = 4;
    for (int y = 0; y < height; y += tuftSize) {
        for (int x = 0; x < width; x += tuftSize) {
            if (((x / tuftSize) + (y / tuftSize)) % 2 == 0) {
                graphics.FillEllipse(&tuftBrush, x - 2, y - 2, tuftSize, tuftSize);
            }
        }
    }

    // Frost fractures (4x4 grid)
    for (int y = 0; y < height; y += tuftSize) {
        for (int x = 0; x < width; x += tuftSize) {
            if (((x / tuftSize) + (y / tuftSize)) % 2 == 1) {
                graphics.DrawLine(&fracturePen, x, y, x + tuftSize, y + tuftSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateSunlitPrairieWavePattern(int width, int height, const Color& bgColor, const Color& waveColor, const Color& flowerColor, const Color& lightColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Bright green background
    SolidBrush waveBrush(waveColor);  // Gold grass waves
    SolidBrush flowerBrush(flowerColor);  // Yellow wildflowers
    Pen lightPen(lightColor, 1.0f);  // White sunlight

    // Grass waves (8x8 staggered grid)
    int waveWidth = 8;
    int waveHeight = 8;
    for (int y = 0; y < height; y += waveHeight) {
        int xOffset = (y / waveHeight) % 2 == 0 ? 0 : waveWidth / 2;
        for (int x = xOffset; x < width; x += waveWidth) {
            SolidBrush brush((x / waveWidth + y / waveHeight) % 2 == 0 ? waveColor : bgColor);
            Point points[3] = {
                Point(x, y),
                Point(x + waveWidth / 2, y + waveHeight),
                Point(x + waveWidth, y)
            };
            graphics.FillPolygon(&brush, points, 3);
        }
    }

    // Wildflowers (4x4 grid)
    int flowerSize = 4;
    for (int y = 0; y < height; y += flowerSize) {
        for (int x = 0; x < width; x += flowerSize) {
            if (((x / flowerSize) + (y / flowerSize)) % 2 == 0) {
                graphics.FillEllipse(&flowerBrush, x - 2, y - 2, flowerSize, flowerSize);
            }
        }
    }

    // Sunlight plaid (8x8 grid)
    int lightSpacing = 8;
    for (int i = 0; i <= width; i += lightSpacing) {
        graphics.DrawLine(&lightPen, i, 0, i, height - 1);  // Vertical
        graphics.DrawLine(&lightPen, 0, i, width - 1, i);  // Horizontal
    }

    return bitmap;
}
Bitmap* CreateEchoingCanyonEchoPattern(int width, int height, const Color& bgColor, const Color& stoneColor, const Color& echoColor, const Color& rockColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Slate gray background
    SolidBrush stoneBrush(stoneColor);  // Gray stones
    Pen echoPen(echoColor, 1.0f);  // Sky blue echoes
    SolidBrush rockBrush(rockColor);  // Saddle brown rock

    // Stone diamonds (4x4 grid)
    int diamondSize = 4;
    for (int y = 0; y < height; y += diamondSize) {
        for (int x = 0; x < width; x += diamondSize) {
            if (((x / diamondSize) + (y / diamondSize)) % 2 == 0) {
                PointF points[4] = {
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize), static_cast<Gdiplus::REAL>(y + diamondSize / 2)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y + diamondSize)),
                    PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + diamondSize / 2))
                };
                graphics.FillPolygon(&stoneBrush, points, 4);
            }
        }
    }

    // Echo waves (8-pixel period)
    int period = 8;
    float amplitude = 2.0f;
    for (int y = 0; y < height; y += period) {
        for (int x = 0; x < width; x++) {
            int offset = static_cast<int>(amplitude * sin(2.0f * 3.14159265f * x / period));
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + offset)),
                PointF(static_cast<Gdiplus::REAL>(x + 1), static_cast<Gdiplus::REAL>(y + offset))
            };
            graphics.DrawLines(&echoPen, points, 2);
        }
    }

    // Rock herringbone (8x8 staggered grid)
    int brickWidth = 8;
    int brickHeight = 8;
    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 2; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? rockColor : bgColor);
            graphics.FillRectangle(&brush, x, y, brickWidth / 2, brickHeight);
            graphics.FillRectangle(&brush, x + brickWidth / 2, y + brickHeight / 2, brickWidth / 2, brickHeight);
        }
    }

    return bitmap;
}
Bitmap* CreateVerdantSwampMistPattern(int width, int height, const Color& bgColor, const Color& algaeColor, const Color& mistColor, const Color& meshColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Forest green background
    Pen algaePen(algaeColor, 1.0f);  // Moss green algae
    Color lightMistColor(48, mistColor.GetR(), mistColor.GetG(), mistColor.GetB());  // Semi-transparent lime green mist
    SolidBrush mistBrush(lightMistColor);  // Misty layer
    SolidBrush meshBrush(meshColor);  // Bright green mesh

    // Algae lines (4x4 diagonal grid)
    int algaeSpacing = 4;
    for (int y = 0; y < height; y += algaeSpacing) {
        for (int x = 0; x < width; x += algaeSpacing) {
            if ((x / algaeSpacing + y / algaeSpacing) % 2 == 0) {
                graphics.DrawLine(&algaePen, x, y, x + algaeSpacing, y + algaeSpacing);
            }
            else {
                graphics.DrawLine(&algaePen, x + algaeSpacing, y, x, y + algaeSpacing);
            }
        }
    }

    // Misty layer (4x4 grid)
    int mistSize = 4;
    for (int y = 0; y < height; y += mistSize) {
        for (int x = 0; x < width; x += mistSize) {
            if (((x / mistSize) + (y / mistSize)) % 2 == 0) {
                graphics.FillEllipse(&mistBrush, x - 2, y - 2, mistSize, mistSize);
            }
        }
    }

    // Vegetation mesh (8x8 staggered grid)
    int meshSpacing = 8;
    for (int y = 0; y < height; y += meshSpacing) {
        int xOffset = (y / meshSpacing) % 2 == 0 ? 0 : meshSpacing / 2;
        for (int x = xOffset; x < width; x += meshSpacing) {
            graphics.FillEllipse(&meshBrush, x - 2, y - 2, 4, 4);
        }
    }

    return bitmap;
}
Bitmap* CreateTwilightWoodlandPatchPattern(int width, int height, const Color& bgColor, const Color& mossColor, const Color& leafColor1, const Color& leafColor2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Brown background
    SolidBrush mossBrush(mossColor);  // Moss green patches
    SolidBrush leafBrush1(leafColor1);  // Red leaves
    SolidBrush leafBrush2(leafColor2);  // Orange leaves

    // Moss patches (8x8 staggered grid)
    int mossSpacing = 8;
    for (int y = 0; y < height; y += mossSpacing) {
        int xOffset = (y / mossSpacing) % 2 == 0 ? 0 : mossSpacing / 2;
        for (int x = xOffset; x < width; x += mossSpacing) {
            graphics.FillEllipse(&mossBrush, x - 2, y - 2, 4, 4);
        }
    }

    // Leaf litter (4x4 checkerboard)
    int leafSize = 4;
    for (int y = 0; y < height; y += leafSize) {
        for (int x = 0; x < width; x += leafSize) {
            if (((x / leafSize) + (y / leafSize)) % 2 == 0) {
                graphics.FillEllipse(&leafBrush1, x - 2, y - 2, leafSize, leafSize);
            }
            else {
                graphics.FillEllipse(&leafBrush2, x - 2, y - 2, leafSize, leafSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateLushRainforestUndergrowthPattern(int width, int height, const Color& bgColor, const Color& vineColor, const Color& flowerColor, const Color& frondColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Forest green background
    SolidBrush vineBrush(vineColor);  // Moss green vines
    SolidBrush flowerBrush(flowerColor);  // Yellow flowers
    Pen frondPen(frondColor, 1.0f);  // Bright green fronds

    // Vines (8x8 staggered grid)
    int vineSpacing = 8;
    for (int y = 0; y < height; y += vineSpacing) {
        int xOffset = (y / vineSpacing) % 2 == 0 ? 0 : vineSpacing / 2;
        for (int x = xOffset; x < width; x += vineSpacing) {
            graphics.FillEllipse(&vineBrush, x - 2, y - 2, 4, 4);
        }
    }

    // Flower blooms (4x4 grid, concentric)
    int flowerSize = 4;
    for (int y = 0; y < height; y += flowerSize) {
        for (int x = 0; x < width; x += flowerSize) {
            if (((x / flowerSize) + (y / flowerSize)) % 2 == 0) {
                int centerX = x + flowerSize / 2;
                int centerY = y + flowerSize / 2;
                for (int r = 1; r < flowerSize / 2; r += 2) {
                    if (abs((x - centerX) / r) == abs((y - centerY) / r)) {
                        graphics.FillEllipse(&flowerBrush, x - 1, y - 1, 2, 2);
                    }
                }
            }
        }
    }

    // Fern fronds (16x16 grid)
    int frondSpacing = 16;
    for (int y = 0; y < height; y += frondSpacing) {
        for (int x = (y / frondSpacing) % 2 == 0 ? 0 : frondSpacing / 2; x < width; x += frondSpacing) {
            graphics.DrawBezier(&frondPen,
                Point(x, y),
                Point(x + 4, y + 8),
                Point(x - 4, y + 12),
                Point(x, y + 16));
        }
    }

    return bitmap;
}
Bitmap* CreateFrozenTundraCrackPattern(int width, int height, const Color& bgColor, const Color& fractureColor, const Color& snowColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Light gray background
    SolidBrush fractureBrush(fractureColor);  // White fractures
    SolidBrush snowBrush(snowColor);  // Gray snow

    // Ice fractures (4x4 grid)
    int fractureSize = 4;
    for (int y = 0; y < height; y += fractureSize) {
        for (int x = 0; x < width; x += fractureSize) {
            if (((x / fractureSize) + (y / fractureSize)) % 2 == 0) {
                Point points[4] = {
                    Point(x, y),
                    Point(x + 2, y + 2),
                    Point(x + 4, y),
                    Point(x, y)
                };
                graphics.FillPolygon(&fractureBrush, points, 4);
            }
        }
    }

    // Snow patches (8x8 staggered grid)
    int snowSpacing = 8;
    for (int y = 0; y < height; y += snowSpacing) {
        int xOffset = (y / snowSpacing) % 2 == 0 ? 0 : snowSpacing / 2;
        for (int x = xOffset; x < width; x += snowSpacing) {
            int size = (x + y) % 2 == 0 ? 4 : 6;  // Deterministic size variation
            graphics.FillEllipse(&snowBrush, x - size / 2, y - size / 2, size, size);
        }
    }

    return bitmap;
}
Bitmap* CreateWhisperingWindGrassPattern(int width, int height, const Color& bgColor, const Color& grassColor, const Color& flowerColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Bright green background
    SolidBrush grassBrush(grassColor);  // Lime green grass
    SolidBrush flowerBrush(flowerColor);  // Yellow wildflowers

    // Wind-blown grass (8x8 staggered grid)
    int brickWidth = 8;
    int brickHeight = 8;
    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 2; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? grassColor : bgColor);
            graphics.FillRectangle(&brush, x, y, brickWidth / 2, brickHeight / 2);
            graphics.FillRectangle(&brush, x + brickWidth / 2, y + brickHeight / 2, brickWidth / 2, brickHeight / 2);
        }
    }

    // Wildflowers (4x4 grid)
    int flowerSize = 4;
    for (int y = 0; y < height; y += flowerSize) {
        for (int x = 0; x < width; x += flowerSize) {
            if (((x / flowerSize) + (y / flowerSize)) % 2 == 0) {
                graphics.FillEllipse(&flowerBrush, x - 2, y - 2, flowerSize, flowerSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateSunbakedDuneCrestPattern(int width, int height, const Color& bgColor, const Color& rippleColor, const Color& hazeColor, const Color& lightColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);  // Tan background
    Pen ripplePen(rippleColor, 1.0f);  // Gold ripples
    SolidBrush hazeBrush(hazeColor);  // Light gray heat haze
    Pen lightPen(lightColor, 1.0f);  // White sunlight

    // Sand ripples (8-pixel period)
    int period = 8;
    float amplitude = 2.0f;
    for (int y = 0; y < height; y += period) {
        for (int x = 0; x < width; x++) {
            int offset = static_cast<int>(amplitude * sin(2.0f * 3.14159265f * x / period));
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + offset)),
                PointF(static_cast<Gdiplus::REAL>(x + 1), static_cast<Gdiplus::REAL>(y + offset))
            };
            graphics.DrawLines(&ripplePen, points, 2);
        }
    }

    // Heat haze diamonds (4x4 grid)
    int diamondSize = 4;
    for (int y = 0; y < height; y += diamondSize) {
        for (int x = 0; x < width; x += diamondSize) {
            if (((x / diamondSize) + (y / diamondSize)) % 2 == 0) {
                PointF points[4] = {
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize), static_cast<Gdiplus::REAL>(y + diamondSize / 2)),
                    PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y + diamondSize)),
                    PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + diamondSize / 2))
                };
                graphics.FillPolygon(&hazeBrush, points, 4);
            }
        }
    }

    // Sunlight plaid (8x8 grid)
    int lightSpacing = 8;
    for (int i = 0; i <= width; i += lightSpacing) {
        graphics.DrawLine(&lightPen, i, 0, i, height - 1);  // Vertical
        graphics.DrawLine(&lightPen, 0, i, width - 1, i);  // Horizontal
    }

    return bitmap;
}
Bitmap* CreateLegionsNumberMosaicTexture(int width, int height, const Color& bgColor, const Color& tileColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Deep black background

    SolidBrush tileBrush(tileColor); // White hexagons
    Gdiplus::REAL tileSize = 8.0f; // Ensure tileSize is floating point

    // Create a hexagonal mosaic pattern inspired by Legion’s number digit distribution
    for (int y = 0; y < height; y += static_cast<int>(tileSize)) {
        for (int x = 0; x < width; x += static_cast<int>(tileSize)) {
            // Draw hexagons with offset for staggered pattern
            PointF hexagon[6] = {
                PointF(static_cast<Gdiplus::REAL>(x) + tileSize / 2, static_cast<Gdiplus::REAL>(y)),                  // Top
                PointF(static_cast<Gdiplus::REAL>(x) + tileSize, static_cast<Gdiplus::REAL>(y) + tileSize / 4),       // Top-right
                PointF(static_cast<Gdiplus::REAL>(x) + tileSize, static_cast<Gdiplus::REAL>(y) + 3 * tileSize / 4),   // Bottom-right
                PointF(static_cast<Gdiplus::REAL>(x) + tileSize / 2, static_cast<Gdiplus::REAL>(y) + tileSize),       // Bottom
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y) + 3 * tileSize / 4),              // Bottom-left
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y) + tileSize / 4)                   // Top-left
            };
            if ((x / static_cast<int>(tileSize) + y / static_cast<int>(tileSize)) % 2 == 0) { // Staggered pattern
                graphics.FillPolygon(&tileBrush, hexagon, 6);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateBatrachionSpiralTexture(int width, int height, const Color& bgColor, const Color& spiralColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Forest green background

    Pen spiralPen(spiralColor, 1.5f); // Turquoise spirals
    int centerX = width / 2;
    int centerY = height / 2;
    float radius = 2.0f;
    float angle = 0.0f;

    // Simulate batrachion spiral with recursive growth (simplified logarithmic spiral)
    for (int i = 0; i < 15 && radius < width / 2; i++) {
        float nextRadius = radius * 1.2f; // Growth factor for organic feel
        float startX = centerX + radius * cos(angle);
        float startY = centerY + radius * sin(angle);
        float endX = centerX + nextRadius * cos(angle + 3.14159f / 3);
        float endY = centerY + nextRadius * sin(angle + 3.14159f / 3);
        graphics.DrawLine(&spiralPen, startX, startY, endX, endY);
        radius = nextRadius;
        angle += 3.14159f / 3; // 60-degree steps for spiral
    }

    return bitmap;
}
Bitmap* CreateFactorionGridTexture(int width, int height, const Color& bgColor, const Color& digitColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Midnight blue background

    SolidBrush digitBrush(digitColor); // Silver squares
    int gridSize = 8; // 4x4 grid of 8x8 blocks in 32x32

    // Simulate factorion digits (e.g., 145) with a grid pattern
    int factorions[] = { 145, 405, 1 }; // Example factorions
    srand(12345); // Fixed seed for repeatability
    for (int y = 0; y < height; y += gridSize) {
        for (int x = 0; x < width; x += gridSize) {
            int digit = factorions[rand() % 3] / 100; // Use first digit for simplicity
            if (digit > 0) {
                graphics.FillRectangle(&digitBrush, x + 2, y + 2, 4, 4); // Small square for digit presence
            }
        }
    }

    return bitmap;
}
Bitmap* CreateJugglerSpiralTexture(int width, int height, const Color& bgColor, const Color& spiralColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Dark brown background

    Pen spiralPen(spiralColor, 1.0f); // Amber spirals
    int centerX = width / 2;
    int centerY = height / 2;
    float radius = 1.0f;
    float angle = 0.0f;

    // Simulate Juggler sequence: n → n² if even, n^(1/3) if odd, used to scale spiral growth
    long long n = 1;
    for (int step = 0; step < 10 && radius < width / 2; step++) {
        float growth = (n % 2 == 0) ? static_cast<float>(sqrt(n)) : static_cast<float>(pow(n, 1.0 / 3.0));
        float nextRadius = radius * growth / 10.0f; // Scale for 32x32
        float startX = centerX + radius * cos(angle);
        float startY = centerY + radius * sin(angle);
        float endX = centerX + nextRadius * cos(angle + 3.14159f / 4);
        float endY = centerY + nextRadius * sin(angle + 3.14159f / 4);
        graphics.DrawLine(&spiralPen, startX, startY, endX, endY);
        radius = nextRadius;
        angle += 3.14159f / 4; // 45-degree steps for spiral
        n = (n % 2 == 0) ? n * n : static_cast<long long>(pow(n, 1.0 / 3.0));
    }

    return bitmap;
}
Bitmap* CreateCarotidKundaliniTexture(int width, int height, const Color& bgColor, const Color& fractalColor1, const Color& fractalColor2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Navy blue background

    SolidBrush fractalBrush1(fractalColor1); // Red for outer edges
    SolidBrush fractalBrush2(fractalColor2); // Purple for inner swirls
    int centerX = width / 2;
    int centerY = height / 2;

    // Simplified Carotid-Kundalini approximation: draw concentric spirals based on distance from center
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dx = x - centerX;
            int dy = y - centerY;
            double distance = sqrt(dx * dx + dy * dy);
            double angle = atan2(dy, dx) * 180 / 3.14159;

            // Use distance and angle to create swirling fractal pattern
            if (distance < 16 && (int)(distance + angle / 10) % 2 == 0) {
                graphics.FillRectangle(&fractalBrush1, x, y, 1, 1); // Outer red swirls
            }
            else if (distance < 8 && (int)(distance * angle / 20) % 2 == 0) {
                graphics.FillRectangle(&fractalBrush2, x, y, 1, 1); // Inner purple swirls
            }
        }
    }

    return bitmap;
}
Bitmap* CreateLeviathanNumberTexture(int width, int height, const Color& bgColor, const Color& digitColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Charcoal gray background

    SolidBrush digitBrush(digitColor); // Gold dots
    int gridSize = 4; // 8x8 blocks in 32x32

    // Simulate a Leviathan number pattern: place dots at positions determined by a pseudo-random sequence
    srand(12345); // Fixed seed for repeatability
    for (int y = 0; y < height; y += gridSize) {
        for (int x = 0; x < width; x += gridSize) {
            if (rand() % 5 == 0) { // 20% chance for a digit (simulating sparsity of large numbers)
                graphics.FillEllipse(&digitBrush, x + 2, y + 2, 4, 4);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateGoldenRatioSpiralTexture(int width, int height, const Color& bgColor, const Color& spiralColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Dark green background

    Pen spiralPen(spiralColor, 2.0f); // Yellow spiral
    const float phi = 1.6180339887f;
    float centerX = width / 2.0f;
    float centerY = height / 2.0f;
    float radius = 1.0f;
    float angle = 0.0f;

    for (int i = 0; i < 20; i++) { // Draw 20 segments for spiral
        float nextRadius = radius * phi;
        float startX = centerX + radius * cos(angle);
        float startY = centerY + radius * sin(angle);
        float endX = centerX + nextRadius * cos(angle + 3.14159f / 2);
        float endY = centerY + nextRadius * sin(angle + 3.14159f / 2);
        graphics.DrawArc(&spiralPen, centerX - nextRadius, centerY - nextRadius, nextRadius * 2, nextRadius * 2,
            angle * 180 / 3.14159f, 90);
        radius = nextRadius;
        angle += 3.14159f / 2; // 90 degrees per step
        if (radius > width / 2) break; // Stop when spiral exceeds bounds
    }

    return bitmap;
}
Bitmap* CreateCellularAutomatonTexture(int width, int height, const Color& bgColor, const Color& cellColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Black background

    SolidBrush cellBrush(cellColor); // Orange cells
    std::vector<int> row(width, 0);
    row[width / 2] = 1; // Start with a single cell in the middle

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (row[x] == 1) {
                graphics.FillRectangle(&cellBrush, x, y, 1, 1);
            }
        }
        // Apply Rule 30: 111→0, 110→0, 101→0, 100→1, 011→1, 010→1, 001→1, 000→0
        std::vector<int> nextRow(width, 0);
        for (int x = 0; x < width; x++) {
            int left = (x > 0) ? row[x - 1] : 0;
            int center = row[x];
            int right = (x < width - 1) ? row[x + 1] : 0;
            int pattern = (left << 2) | (center << 1) | right;
            nextRow[x] = (pattern == 4 || pattern == 3 || pattern == 2 || pattern == 1) ? 1 : 0;
        }
        row = nextRow;
    }

    return bitmap;
}
Bitmap* CreateSierpinskiTriangleTexture(int width, int height, const Color& bgColor, const Color& triColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Dark blue background

    SolidBrush triBrush(triColor); // Cyan triangles
    SolidBrush bgBrush(bgColor);   // Background brush for hole removal
    Gdiplus::REAL baseSize = static_cast<Gdiplus::REAL>(width); // Ensure floating-point division

    // Define initial triangle points (top, left, right)
    PointF points[3] = {
        PointF(static_cast<Gdiplus::REAL>(width) / 2.0f, 0.0f),                  // Top
        PointF(0.0f, static_cast<Gdiplus::REAL>(height) - 1.0f),                 // Bottom-left
        PointF(static_cast<Gdiplus::REAL>(width) - 1.0f, static_cast<Gdiplus::REAL>(height) - 1.0f) // Bottom-right
    };
    graphics.FillPolygon(&triBrush, points, 3);

    // Recursive removal (simplified to 2 levels for 32x32)
    for (int level = 0; level < 2; level++) {
        Gdiplus::REAL offset = baseSize / static_cast<Gdiplus::REAL>(2 << level); // Shift by 2^level
        PointF midPoints[3][3] = {
            {PointF(points[0].X, points[0].Y + offset),
             PointF(points[1].X + offset / 2.0f, points[1].Y - offset / 2.0f),
             PointF(points[2].X - offset / 2.0f, points[2].Y - offset / 2.0f)},

            {PointF(points[0].X - offset / 2.0f, points[0].Y + offset / 2.0f),
             PointF(points[1].X + offset, points[1].Y - offset),
             PointF(points[2].X - offset / 2.0f, points[2].Y - offset / 2.0f)},

            {PointF(points[0].X + offset / 2.0f, points[0].Y + offset / 2.0f),
             PointF(points[1].X + offset / 2.0f, points[1].Y - offset / 2.0f),
             PointF(points[2].X - offset, points[2].Y - offset)}
        };
        for (int i = 0; i < 3; i++) {
            graphics.FillPolygon(&bgBrush, midPoints[i], 3); // Remove middle triangles
        }
    }

    return bitmap;
}
Bitmap* CreateTruchetArcTexture(int width, int height, const Color& bgColor, const Color& arcColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Dark gray background

    SolidBrush arcBrush(arcColor); // Green arcs
    int tileSize = 8;
    srand(static_cast<unsigned int>(time(nullptr))); // Proper casting to avoid warning

    for (int y = 0; y < height; y += tileSize) {
        for (int x = 0; x < width; x += tileSize) {
            int arcType = rand() % 4; // 0=TL, 1=TR, 2=BL, 3=BR
            graphics.SetClip(Rect(x, y, tileSize, tileSize)); // Clip to tile bounds

            if (arcType == 0) { // Top-left arc
                graphics.FillPie(&arcBrush, x - tileSize, y - tileSize, tileSize * 2, tileSize * 2, 0, 90);
            }
            else if (arcType == 1) { // Top-right arc
                graphics.FillPie(&arcBrush, x, y - tileSize, tileSize * 2, tileSize * 2, 270, 90);
            }
            else if (arcType == 2) { // Bottom-left arc
                graphics.FillPie(&arcBrush, x - tileSize, y, tileSize * 2, tileSize * 2, 90, 90);
            }
            else { // Bottom-right arc
                graphics.FillPie(&arcBrush, x, y, tileSize * 2, tileSize * 2, 180, 90);
            }

            graphics.ResetClip();
        }
    }

    return bitmap;
}
Bitmap* CreateVampireNumberTexture(int width, int height, const Color& bgColor, const Color& digitColor1, const Color& digitColor2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Brown background

    SolidBrush digitBrush1(digitColor1); // Red for digits 1 and 2
    SolidBrush digitBrush2(digitColor2); // Orange for digits 6 and 0
    SolidBrush hollowBrush(bgColor); // Brush for hollow space
    int blockSize = 8;

    // Digit patterns (stylized shapes for 1, 2, 6, 0 in 8x8 blocks)
    for (int y = 0; y < height; y += blockSize) {
        for (int x = 0; x < width; x += blockSize) {
            int digitIndex = (y / blockSize) * 2 + (x / blockSize); // Arrange 1, 2, 6, 0
            SolidBrush* brush = (digitIndex % 2 == 0) ? &digitBrush1 : &digitBrush2;

            if (digitIndex == 0) { // Digit 1: Vertical bar
                graphics.FillRectangle(brush, x + 3, y, 2, blockSize);
            }
            else if (digitIndex == 1) { // Digit 2: Diagonal curve
                Point points[3] = { Point(x, y), Point(x + blockSize / 2, y + blockSize), Point(x + blockSize, y) };
                graphics.FillPolygon(brush, points, 3);
            }
            else if (digitIndex == 2) { // Digit 6: Loop
                graphics.FillEllipse(brush, x, y, blockSize, blockSize);
                graphics.FillRectangle(&hollowBrush, x + 2, y + 2, blockSize - 4, blockSize - 4); // Hollow center
            }
            else if (digitIndex == 3) { // Digit 0: Circle
                graphics.FillEllipse(brush, x, y, blockSize, blockSize);
            }
        }
    }

    return bitmap;
}

Bitmap* CreateMandelbrotFragmentTexture(int width, int height, const Color& bgColor, const Color& fractalColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor); // Dark purple background

    SolidBrush fractalBrush(fractalColor); // White for fractal edge
    const int MAX_ITER = 50;

    // Map 32x32 to a region of the Mandelbrot set (-0.75 to -0.25 real, -0.25 to 0.25 imag)
    double minReal = -0.75, maxReal = -0.25;
    double minImag = -0.25, maxImag = 0.25;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            double c_real = minReal + (maxReal - minReal) * x / (width - 1);
            double c_imag = minImag + (maxImag - minImag) * (height - 1 - y) / (height - 1);
            double z_real = 0, z_imag = 0;
            int iter = 0;

            while (iter < MAX_ITER && (z_real * z_real + z_imag * z_imag) <= 4.0) {
                double temp = z_real * z_real - z_imag * z_imag + c_real;
                z_imag = 2 * z_real * z_imag + c_imag;
                z_real = temp;
                iter++;
            }

            // Fill bounded points with fractal color
            if (iter == MAX_ITER) {
                graphics.FillRectangle(&fractalBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}

inline float DegToRad(float deg) {
    return deg * 3.14159f / 180.0f;
}

Bitmap* CreateDigitalOccultationTexture(int width, int height, const Color& bgColor, const Color& neonColor, const Color& circuitColor) {
    // Optionally, seed randomness once at program startup rather than here:
    srand(static_cast<unsigned int>(time(nullptr)));

    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    int centerX = width / 2;
    int centerY = height / 2;
    int radius = min(width, height) / 2 - 20;

    // Draw a neon circle.
    Pen neonPen(neonColor, 2.0f);
    neonPen.SetLineJoin(LineJoinRound);
    graphics.DrawEllipse(&neonPen, centerX - radius, centerY - radius, 2 * radius, 2 * radius);

    // Draw digital circuit-like lines across the circle.
    Pen circuitPen(circuitColor, 1.0f);
    const int numCircuits = 8;
    for (int i = 0; i < numCircuits; i++) {
        float angle = 360.0f * i / numCircuits;
        float rad = DegToRad(angle);
        float x1 = centerX + radius * cos(rad);
        float y1 = centerY + radius * sin(rad);
        float x2 = centerX + (radius * 0.5f) * cos(rad + DegToRad(360.0f / (2 * numCircuits)));
        float y2 = centerY + (radius * 0.5f) * sin(rad + DegToRad(360.0f / (2 * numCircuits)));
        graphics.DrawLine(&circuitPen, x1, y1, x2, y2);
    }

    // Overlay random digital "glitches" (small rectangles) for a high-tech feel.
    SolidBrush circuitBrush(circuitColor);
    for (int i = 0; i < 30; i++) {
        int x = rand() % width;
        int y = rand() % height;
        graphics.FillRectangle(&circuitBrush, x, y, 2, 2);
    }

    return bitmap;
}
Bitmap* CreateRunicPantacleTexture(int width, int height, const Color& bgColor, const Color& pantacleColor, const Color& runeColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    int centerX = width / 2;
    int centerY = height / 2;
    int radius = min(width, height) / 2 - 20;

    // Draw the outer circle.
    Pen pantaclePen(pantacleColor, 2.0f);
    graphics.DrawEllipse(&pantaclePen, centerX - radius, centerY - radius, 2 * radius, 2 * radius);

    // Calculate pentagon vertices for the pentacle (inner star).
    PointF pentagon[5];
    for (int i = 0; i < 5; i++) {
        float angle = DegToRad(90.0f + i * 72.0f); // Start at 90° (top) and space by 72°
        pentagon[i] = PointF(centerX + radius * 0.8f * cos(angle),
            centerY - radius * 0.8f * sin(angle));
    }
    // Draw the pentagram (connect every second vertex).
    for (int i = 0; i < 5; i++) {
        int j = (i + 2) % 5;
        graphics.DrawLine(&pantaclePen, pentagon[i], pentagon[j]);
    }

    // Draw runic inscriptions along the circle.
    FontFamily fontFamily(L"Arial");
    Font font(&fontFamily, 12, FontStyleBold, UnitPixel);
    SolidBrush runeBrush(runeColor);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    const int numRunes = 10;
    for (int i = 0; i < numRunes; i++) {
        float angle = 360.0f * i / numRunes;
        float rad = DegToRad(angle);
        float runeX = centerX + radius * cos(rad);
        float runeY = centerY + radius * sin(rad);
        RectF runeRect(runeX - 8, runeY - 8, 16, 16);
        // For demo purposes, we simply draw the letter "R" as a rune.
        graphics.DrawString(L"R", -1, &font, runeRect, &format, &runeBrush);
    }

    return bitmap;
}
Bitmap* CreateAstrologicalTransmutationCircleTexture(int width, int height, const Color& bgColor, const Color& circleColor, const Color& symbolColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    int diameter = min(width, height) - 40;
    int centerX = width / 2;
    int centerY = height / 2;
    Pen circlePen(circleColor, 2.0f);
    graphics.DrawEllipse(&circlePen, centerX - diameter / 2, centerY - diameter / 2, diameter, diameter);

    // Divide the circle into 12 segments and mark each with a simple symbol.
    const int segments = 12;
    FontFamily fontFamily(L"Arial");
    Font font(&fontFamily, 14, FontStyleBold, UnitPixel);
    SolidBrush symbolBrush(symbolColor);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    for (int i = 0; i < segments; i++) {
        float angle = 360.0f * i / segments;
        float rad = DegToRad(angle);
        // Position symbols just outside the circle.
        float symbolX = centerX + (diameter / 2 + 15) * cos(rad);
        float symbolY = centerY + (diameter / 2 + 15) * sin(rad);
        RectF rect(symbolX - 10, symbolY - 10, 20, 20);
        // For demonstration, label each segment with its number.
        wchar_t buffer[3];
        swprintf(buffer, 3, L"%d", i + 1);
        graphics.DrawString(buffer, -1, &font, rect, &format, &symbolBrush);
    }

    return bitmap;
}

Bitmap* CreateAlchemicalMandalaTexture(int width, int height, const Color& bgColor, const Color& circleColor, const Color& glyphColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    // Draw concentric circles.
    const int numCircles = 5;
    Pen circlePen(circleColor, 2.0f);
    for (int i = 0; i < numCircles; i++) {
        int margin = i * (width / (2 * numCircles));
        int diameter = width - 2 * margin;
        graphics.DrawEllipse(&circlePen, margin, margin, diameter, diameter);
    }

    // Draw interlaced radial lines.
    Pen linePen(glyphColor, 1.0f);
    const int numLines = 12;
    float centerX = width / 2.0f;
    float centerY = height / 2.0f;
    float maxRadius = width / 2.0f;
    for (int i = 0; i < numLines; i++) {
        float angle = 2 * 3.14159f * i / numLines;
        float x = centerX + maxRadius * cos(angle);
        float y = centerY + maxRadius * sin(angle);
        graphics.DrawLine(&linePen, centerX, centerY, x, y);
    }

    // Draw alchemical glyphs as text (example: "Hg" for Mercury).
    FontFamily fontFamily(L"Arial");
    Font font(&fontFamily, 16, FontStyleBold, UnitPixel);
    SolidBrush glyphBrush(glyphColor);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    // Place a glyph in the top quarter.
    RectF glyphRect(0, 0, static_cast<REAL>(width), static_cast<REAL>(height) / 4);
    graphics.DrawString(L"Hg", -1, &font, glyphRect, &format, &glyphBrush);

    return bitmap;
}
Bitmap* CreateMysticSigilCascadeTexture(int width, int height, const Color& bgColor, const Color& sigilColor, const Color& accentColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    // Draw a cascade of arcs radiating from the center.
    Pen arcPen(accentColor, 2.0f);
    arcPen.SetLineJoin(LineJoinRound);
    const int numArcs = 6;
    float centerX = width / 2.0f;
    float centerY = height / 2.0f;
    float maxRadius = min(width, height) / 2.0f - 20;

    for (int i = 0; i < numArcs; i++) {
        // Calculate increasing radius for each arc.
        float radius = (i + 1) * (maxRadius / numArcs);
        // Define the bounding rectangle for the arc.
        RectF arcRect(centerX - radius, centerY - radius, 2 * radius, 2 * radius);
        // Vary the start angle slightly for a cascading effect.
        float startAngle = i * 15.0f;
        float sweepAngle = 180.0f;
        graphics.DrawArc(&arcPen, arcRect, startAngle, sweepAngle);
    }

    // Draw a sigil along each arc.
    FontFamily fontFamily(L"Times New Roman");
    Font font(&fontFamily, 18, FontStyleBold, UnitPixel);
    SolidBrush sigilBrush(sigilColor);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    for (int i = 0; i < numArcs; i++) {
        float radius = (i + 1) * (maxRadius / numArcs);
        // Place each sigil at an offset angle along the arc.
        float angle = DegToRad(60.0f + i * 10.0f);
        float x = centerX + radius * cos(angle);
        float y = centerY + radius * sin(angle);
        RectF sigilRect(x - 10, y - 10, 20, 20);
        graphics.DrawString(L"Σ", -1, &font, sigilRect, &format, &sigilBrush);
    }

    return bitmap;
}
Bitmap* CreateMysticRuneFieldTexture(int width, int height, const Color& bgColor, const Color& runeColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    // Create a semi-transparent rune brush.
    // Here we assume runeColor has full opacity and we adjust it to 50%:
    Color semiTransparentRune(runeColor.GetA() / 2, runeColor.GetR(), runeColor.GetG(), runeColor.GetB());
    SolidBrush runeBrush(semiTransparentRune);

    FontFamily fontFamily(L"Courier New");
    Font font(&fontFamily, 16, FontStyleBold, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    const int numRunes = 50;
    // Seed randomness if needed.
    srand(static_cast<unsigned int>(time(nullptr)));

    for (int i = 0; i < numRunes; i++) {
        int x = rand() % width;
        int y = rand() % height;
        // Save the current transformation.
        graphics.TranslateTransform(static_cast<REAL>(x), static_cast<REAL>(y));
        // Rotate by a random angle.
        float angle = static_cast<float>(rand() % 360);
        graphics.RotateTransform(angle);
        RectF runeRect(-10, -10, 20, 20);
        // Draw the rune (using “ᚠ” as an example).
        graphics.DrawString(L"ᚠ", -1, &font, runeRect, &format, &runeBrush);
        // Reset transform to avoid affecting subsequent drawing.
        graphics.ResetTransform();
    }

    return bitmap;
}

Bitmap* CreateTreeOfLifeTexture(int width, int height, const Color& bgColor,
    const Color& branchColor, const Color& nodeColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    int centerX = width / 2;
    int bottomY = height - 20;
    int topY = 20;
    Pen branchPen(branchColor, 3.0f);
    // Draw central trunk.
    graphics.DrawLine(&branchPen, centerX, bottomY, centerX, topY);

    // Draw branches.
    const int numBranches = 5;
    for (int i = 1; i <= numBranches; i++) {
        int y = bottomY - i * ((bottomY - topY) / (numBranches + 1));
        int branchLength = 50;
        // Left branch.
        graphics.DrawLine(&branchPen, centerX, y, centerX - branchLength, y - branchLength / 2);
        // Right branch.
        graphics.DrawLine(&branchPen, centerX, y, centerX + branchLength, y - branchLength / 2);
    }

    // Draw nodes as small circles.
    SolidBrush nodeBrush(nodeColor);
    const int nodeRadius = 5;
    // Node on trunk (top).
    graphics.FillEllipse(&nodeBrush, centerX - nodeRadius, topY - nodeRadius, nodeRadius * 2, nodeRadius * 2);
    for (int i = 1; i <= numBranches; i++) {
        int y = bottomY - i * ((bottomY - topY) / (numBranches + 1));
        // Node on trunk.
        graphics.FillEllipse(&nodeBrush, centerX - nodeRadius, y - nodeRadius, nodeRadius * 2, nodeRadius * 2);
        // Nodes on branches.
        graphics.FillEllipse(&nodeBrush, centerX - 50 - nodeRadius, y - 25 - nodeRadius, nodeRadius * 2, nodeRadius * 2);
        graphics.FillEllipse(&nodeBrush, centerX + 50 - nodeRadius, y - 25 - nodeRadius, nodeRadius * 2, nodeRadius * 2);
    }

    return bitmap;
}


Bitmap* CreateVesicaPiscisTexture(int width, int height, const Color& bgColor,
    const Color& circleColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    int radius = min(width, height) / 4;
    int centerY = height / 2;
    // Place two circle centers offset horizontally.
    int centerX1 = width / 2 - radius / 2;
    int centerX2 = width / 2 + radius / 2;
    Pen circlePen(circleColor, 2.0f);

    graphics.DrawEllipse(&circlePen, centerX1 - radius, centerY - radius, radius * 2, radius * 2);
    graphics.DrawEllipse(&circlePen, centerX2 - radius, centerY - radius, radius * 2, radius * 2);

    return bitmap;
}


Bitmap* CreateFlowerOfLifeTexture(int width, int height, const Color& bgColor,
    const Color& circleColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    // Parameters for a grid of circles.
    const int numRows = 5;
    const int numCols = 5;
    int circleRadius = min(width, height) / (numCols * 2);
    Pen circlePen(circleColor, 1.5f);

    // Draw circles with alternate row offsets.
    for (int row = 0; row < numRows; row++) {
        for (int col = 0; col < numCols; col++) {
            int offsetX = (row % 2) * circleRadius;
            int x = col * 2 * circleRadius + offsetX + circleRadius;
            int y = row * static_cast<int>(1.7 * circleRadius) + circleRadius;
            graphics.DrawEllipse(&circlePen, x - circleRadius, y - circleRadius, circleRadius * 2, circleRadius * 2);
        }
    }

    return bitmap;
}
Bitmap* CreateMerkabaTexture(int width, int height, const Color& bgColor,
    const Color& lineColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    Gdiplus::REAL centerX = static_cast<Gdiplus::REAL>(width) / 2.0f;
    Gdiplus::REAL centerY = static_cast<Gdiplus::REAL>(height) / 2.0f;
    Gdiplus::REAL size = static_cast<Gdiplus::REAL>(min(width, height)) / 3.0f;

    // Upward-pointing triangle.
    PointF triangle1[3] = {
        PointF(centerX, centerY - size),
        PointF(centerX - size, centerY + size),
        PointF(centerX + size, centerY + size)
    };
    // Downward-pointing triangle.
    PointF triangle2[3] = {
        PointF(centerX, centerY + size),
        PointF(centerX - size, centerY - size),
        PointF(centerX + size, centerY - size)
    };

    Pen pen(lineColor, 2.0f);
    graphics.DrawPolygon(&pen, triangle1, 3);
    graphics.DrawPolygon(&pen, triangle2, 3);

    return bitmap;
}
Bitmap* CreateTriskeleTexture(int width, int height, const Color& bgColor,
    const Color& spiralColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    Gdiplus::REAL centerX = static_cast<Gdiplus::REAL>(width) / 2.0f;
    Gdiplus::REAL centerY = static_cast<Gdiplus::REAL>(height) / 2.0f;
    Pen spiralPen(spiralColor, 2.0f);

    const int numSpirals = 3;
    const float a = 0.0f;  // Starting radius.
    const float b = 4.0f;  // Growth factor.
    const float thetaMax = 6 * 3.14159f; // About 3 full turns.
    const int steps = 100;

    for (int s = 0; s < numSpirals; s++) {
        float offset = (2 * 3.14159f / numSpirals) * s;
        PointF prevPoint(centerX, centerY);
        for (int i = 1; i <= steps; i++) {
            float theta = thetaMax * i / steps;
            float r = a + b * theta;
            float x = centerX + r * cos(theta + offset);
            float y = centerY + r * sin(theta + offset);
            PointF currPoint(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y));
            graphics.DrawLine(&spiralPen, prevPoint, currPoint);
            prevPoint = currPoint;
        }
    }

    return bitmap;
}


Bitmap* CreateTetractysTexture(int width, int height, const Color& bgColor,
    const Color& dotColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    SolidBrush dotBrush(dotColor);
    const int dotRadius = 4;
    const int numRows = 4;

    for (int row = 0; row < numRows; row++) {
        int dotsInRow = row + 1;
        // Center the row horizontally.
        int startX = (width - dotsInRow * dotRadius * 2 - (dotsInRow - 1) * dotRadius) / 2;
        int y = 50 + row * (dotRadius * 3);
        for (int col = 0; col < dotsInRow; col++) {
            int x = startX + col * (dotRadius * 3);
            graphics.FillEllipse(&dotBrush, x, y, dotRadius * 2, dotRadius * 2);
        }
    }

    return bitmap;
}

Bitmap* CreateCosmicEggTexture(int width, int height, const Color& bgColor,
    const Color& eggColor, const Color& swirlColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    int centerX = width / 2;
    int centerY = height / 2;
    int eggWidth = min(width, height) - 40;
    int eggHeight = static_cast<int>(eggWidth * 1.3); // Typical egg ratio.

    // Draw the egg outline.
    Pen eggPen(eggColor, 3.0f);
    graphics.DrawEllipse(&eggPen, centerX - eggWidth / 2, centerY - eggHeight / 2, eggWidth, eggHeight);

    // Draw swirling arcs inside the egg.
    Pen swirlPen(swirlColor, 2.0f);
    const int numSwirls = 5;
    for (int i = 0; i < numSwirls; i++) {
        float startAngle = 360.0f * i / numSwirls;
        float sweepAngle = 180.0f;
        graphics.DrawArc(&swirlPen, centerX - eggWidth / 4, centerY - eggHeight / 4, eggWidth / 2, eggHeight / 2, startAngle, sweepAngle);
    }

    return bitmap;
}

Bitmap* CreateSealOfSolomonTexture(int width, int height, const Color& bgColor,
    const Color& lineColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.Clear(bgColor);

    Gdiplus::REAL centerX = static_cast<Gdiplus::REAL>(width) / 2.0f;
    Gdiplus::REAL centerY = static_cast<Gdiplus::REAL>(height) / 2.0f;
    Gdiplus::REAL size = static_cast<Gdiplus::REAL>(min(width, height)) / 3.0f;

    // Upward triangle.
    PointF upTriangle[3] = {
        PointF(centerX, centerY - size),
        PointF(centerX - size, centerY + size),
        PointF(centerX + size, centerY + size)
    };
    // Downward triangle.
    PointF downTriangle[3] = {
        PointF(centerX, centerY + size),
        PointF(centerX - size, centerY - size),
        PointF(centerX + size, centerY - size)
    };

    Pen pen(lineColor, 2.0f);
    graphics.DrawPolygon(&pen, upTriangle, 3);
    graphics.DrawPolygon(&pen, downTriangle, 3);

    return bitmap;
}

//

Bitmap* CreateSolidColorBlock(int width, int height, const Color& color) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    // Fill the entire bitmap with the given color
    SolidBrush brush(color);
    graphics.FillRectangle(&brush, 0, 0, width, height);

    return bitmap;
}

Bitmap* CreateTextBitmap(char character, const Color& color) {
    int width = 32, height = 32;
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    // Fill background with transparency or a solid color if needed
    graphics.Clear(Color(0, 0, 0, 0)); // Transparent background

    // Convert char to a wide character string
    std::wstring wstr(1, character);

    FontFamily fontFamily(L"Consolas");
    Font font(L"Consolas", 32, FontStyleBold, UnitPixel);

    // Draw the character
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    // Ensure RectF uses floating-point values
    RectF rect(0.0f, 0.0f, static_cast<Gdiplus::REAL>(width), static_cast<Gdiplus::REAL>(height));
    graphics.DrawString(wstr.c_str(), -1, &font, rect, &format, &brush);

    return bitmap;
}

// bitmap operations

Bitmap* FlipBitmap(Bitmap* original, bool horizontal, bool vertical) {
    int width = original->GetWidth();
    int height = original->GetHeight();

    Bitmap* flippedBitmap = new Bitmap(width, height, original->GetPixelFormat());
    Graphics graphics(flippedBitmap);

    // Convert `UINT` to `Gdiplus::REAL` (float) to avoid C4244 warnings
    Gdiplus::REAL realWidth = static_cast<Gdiplus::REAL>(width);
    Gdiplus::REAL realHeight = static_cast<Gdiplus::REAL>(height);

    int flipX = horizontal ? -1 : 1;
    int flipY = vertical ? -1 : 1;

    // Apply transformations correctly
    graphics.TranslateTransform(horizontal ? realWidth : 0, vertical ? realHeight : 0);
    graphics.ScaleTransform(static_cast<Gdiplus::REAL>(flipX), static_cast<Gdiplus::REAL>(flipY));

    // Draw the flipped image using the correct DrawImage overload
    graphics.DrawImage(original, Rect(0, 0, width, height));

    return flippedBitmap;
}

Bitmap* RotateBitmap(Bitmap* original, float angle) {
    int width = original->GetWidth();
    int height = original->GetHeight();

    // Compute the new bounding box size after rotation
    float radians = angle * 3.14159265f / 180.0f;
    float cosine = fabs(cos(radians));
    float sine = fabs(sin(radians));

    int newWidth = static_cast<int>(width * cosine + height * sine);
    int newHeight = static_cast<int>(width * sine + height * cosine);

    // Create a new bitmap with enough space to hold the rotated image
    Bitmap* rotatedBitmap = new Bitmap(newWidth, newHeight, original->GetPixelFormat());
    Graphics graphics(rotatedBitmap);

    // Set high-quality transformations
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

    // Move to the center, rotate, then move back
    graphics.TranslateTransform(newWidth / 2.0f, newHeight / 2.0f);
    graphics.RotateTransform(angle);
    graphics.TranslateTransform(-width / 2.0f, -height / 2.0f);

    // Draw the rotated image centered
    graphics.DrawImage(original, 0, 0, width, height);

    return rotatedBitmap;
}


Bitmap* ApplyMask(Bitmap* base, Bitmap* mask) {
    int width = base->GetWidth();
    int height = base->GetHeight();

    Bitmap* result = new Bitmap(width, height, base->GetPixelFormat());
    Graphics graphics(result);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Color maskPixel;
            mask->GetPixel(x, y, &maskPixel);

            if (maskPixel.GetAlpha() > 128) {  // If mask is mostly opaque, draw
                Color basePixel;
                base->GetPixel(x, y, &basePixel);
                result->SetPixel(x, y, basePixel);
            }
        }
    }

    return result;
}

Bitmap* CompositeBitmaps(Bitmap* base, Bitmap* overlay, float opacity) {
    if (!base || !overlay) return nullptr; // Safety check

    int width = base->GetWidth();
    int height = base->GetHeight();

    // Ensure overlay is the same size
    if (overlay->GetWidth() != width || overlay->GetHeight() != height) return nullptr;

    // Create output bitmap
    Bitmap* result = new Bitmap(width, height, base->GetPixelFormat());
    Graphics graphics(result);

    // Draw the base image
    graphics.DrawImage(base, 0, 0, width, height);

    // Set up opacity (alpha blending)
    ColorMatrix colorMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, opacity, 0.0f,  // Corrected alpha blending
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };

    ImageAttributes imgAttributes;
    imgAttributes.SetColorMatrix(&colorMatrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    // Draw the overlay image with transparency
    graphics.DrawImage(overlay, Rect(0, 0, width, height), 0, 0, width, height, UnitPixel, &imgAttributes);

    return result;
}
//

Bitmap* CreateChevronPattern(int width, int height, const Color& primaryColor, const Color& secondaryColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    // Size adjustments to fit within 32x32
    int chevronWidth = width / 4; // Adjusting to fit more evenly within the grid
    int chevronHeight = height / 4;

    // Iterate over the grid to fill with chevrons
    for (int y = 0; y < height; y += chevronHeight) {
        for (int x = 0; x < width; x += chevronWidth) {
            SolidBrush brush((x / chevronWidth + y / chevronHeight) % 2 == 0 ? primaryColor : secondaryColor);

            // Adjust chevron points to ensure they align well and tessellate
            Point points[3] = {
                Point(x, y),
                Point(x + chevronWidth / 2, y + chevronHeight),
                Point(x + chevronWidth, y)
            };

            graphics.FillPolygon(&brush, points, 3);
        }
    }

    return bitmap;
}
Bitmap* CreateDiamondPattern(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int diamondSize = 4;
    for (int y = 0; y < height; y += diamondSize) {
        for (int x = 0; x < width; x += diamondSize) {
            SolidBrush brush(((x / diamondSize) + (y / diamondSize)) % 2 == 0 ? color1 : color2);

            PointF points[4] = {
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y)),          // Top
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize), static_cast<Gdiplus::REAL>(y + diamondSize / 2)), // Right
                PointF(static_cast<Gdiplus::REAL>(x + diamondSize / 2), static_cast<Gdiplus::REAL>(y + diamondSize)), // Bottom
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + diamondSize / 2))          // Left
            };
            graphics.FillPolygon(&brush, points, 4);
        }
    }

    return bitmap;
}
Bitmap* CreateOffsetHerringbone(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int brickWidth = GRID_SIZE / 2;
    int brickHeight = GRID_SIZE / 6;

    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 3; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? color1 : color2);
            graphics.FillRectangle(&brush, x, y, brickWidth / 3, brickHeight);
            graphics.FillRectangle(&brush, x + brickWidth / 3, y + brickHeight / 3, brickWidth / 3, brickHeight);
        }
    }

    return bitmap;
}
Bitmap* CreateZigZagHerringbone(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int brickWidth = GRID_SIZE / 4;
    int brickHeight = GRID_SIZE / 6;

    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 2; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? color1 : color2);
            graphics.FillRectangle(&brush, x, y, brickWidth, brickHeight / 2);
            graphics.FillRectangle(&brush, x + brickWidth / 2, y + brickHeight / 2, brickWidth, brickHeight / 2);
        }
    }

    return bitmap;
}
Bitmap* CreateSmoothZigzagPattern(int width, int height, const Color& lineColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    Pen pen(lineColor, 2);
    int zigzagHeight = GRID_SIZE / 6;

    // Create a smooth zigzag pattern across the bitmap
    for (int y = 0; y < height; y += zigzagHeight) {
        for (int x = 0; x < width; x++) {
            int zigzagOffset = (x / GRID_SIZE) % 2 == 0 ? zigzagHeight : -zigzagHeight;
            PointF points[2] = {
                PointF(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y + zigzagOffset)),
                PointF(static_cast<Gdiplus::REAL>(x + 1), static_cast<Gdiplus::REAL>(y + zigzagOffset))
            };
            graphics.DrawLines(&pen, points, 2);
        }
    }

    return bitmap;
}
Bitmap* CreateVStripePattern(int width, int height, const Color& primaryColor, const Color& secondaryColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int stripeWidth = width / 10;

    for (int x = 0; x < width; x += stripeWidth) {
        SolidBrush brush((x / stripeWidth) % 2 == 0 ? primaryColor : secondaryColor);
        graphics.FillRectangle(&brush, x, 0, stripeWidth, height);
    }
    return bitmap;
}
Bitmap* CreateHerringbonePattern(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int brickWidth = GRID_SIZE / 3;
    int brickHeight = GRID_SIZE / 6;

    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 2; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? color1 : color2);
            graphics.FillRectangle(&brush, x, y, brickWidth / 2, brickHeight);
            graphics.FillRectangle(&brush, x + brickWidth / 2, y + brickHeight / 2, brickWidth / 2, brickHeight);
        }
    }

    return bitmap;
}
Bitmap* CreateDensePlaid(int width, int height, const Color& stripeColor1, const Color& stripeColor2, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    int stripeWidth = GRID_SIZE / 8; // Thinner stripes for a denser look

    SolidBrush brush1(stripeColor1);
    SolidBrush brush2(stripeColor2);

    for (int x = 0; x < width; x += stripeWidth * 2) {
        graphics.FillRectangle(&brush1, x, 0, stripeWidth, height);
    }

    for (int y = 0; y < height; y += stripeWidth * 2) {
        graphics.FillRectangle(&brush2, 0, y, width, stripeWidth);
    }

    return bitmap;
}
Bitmap* CreateDiagonalHerringbone(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int brickWidth = GRID_SIZE / 3;
    int brickHeight = GRID_SIZE / 5;

    for (int y = 0; y < height; y += brickHeight) {
        for (int x = (y / brickHeight) % 2 == 0 ? 0 : -brickWidth / 2; x < width + brickWidth; x += brickWidth) {
            SolidBrush brush((x / brickWidth + y / brickHeight) % 2 == 0 ? color1 : color2);
            graphics.FillRectangle(&brush, x, y, brickWidth / 2, brickHeight / 2);
            graphics.FillRectangle(&brush, x + brickWidth / 2, y + brickHeight / 2, brickWidth / 2, brickHeight / 2);
        }
    }

    return bitmap;
}
Bitmap* CreateFlowerVariationConcentric(int width, int height, const Color& flowerColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    SolidBrush backgroundBrush(backgroundColor);
    graphics.FillRectangle(&backgroundBrush, 0, 0, width, height);

    SolidBrush flowerBrush(flowerColor);

    // Adjust the pattern size to fit within the 32x32 grid for tessellation
    int patternSize = 8;
    int centerX = patternSize / 2;
    int centerY = patternSize / 2;

    // Loop through the bitmap's pixel space
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dx = x % patternSize;
            int dy = y % patternSize;

            // Define concentric circle pattern
            if (abs(dx - centerX) == abs(dy - centerY)) {
                graphics.FillRectangle(&flowerBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateFractalHalftone(int width, int height, const Color& dotColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush dotBrush(dotColor);

    const int gridSize = 8;

    // Recursive function to simulate fractal behavior
    auto drawFractal = [&](int x, int y, int size, auto&& drawFractal) -> void {
        if (size <= 1) return; // Base case for recursion
        graphics.FillEllipse(&dotBrush, x, y, size, size);
        drawFractal(x + size / 2, y + size / 2, size / 2, drawFractal); // Draw smaller fractal
        drawFractal(x - size / 2, y - size / 2, size / 2, drawFractal); // Draw another smaller fractal
    };

    for (int y = 0; y < height; y += gridSize) {
        for (int x = 0; x < width; x += gridSize) {
            int size = (x + y) % 4 + 1; // Vary the dot size
            drawFractal(x, y, size * gridSize, drawFractal); // Call the recursive fractal drawing
        }
    }

    return bitmap;
}
Bitmap* CreateHerringboneChevronMix(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int chevronWidth = width / 5;
    int chevronHeight = height / 5;

    for (int y = 0; y < height; y += chevronHeight) {
        for (int x = (y / chevronHeight) % 2 == 0 ? 0 : -chevronWidth / 2; x < width + chevronWidth; x += chevronWidth) {
            SolidBrush brush((x / chevronWidth + y / chevronHeight) % 2 == 0 ? color1 : color2);

            Point points[3] = {
                Point(x, y),
                Point(x + chevronWidth, y + chevronHeight / 2),
                Point(x, y + chevronHeight)
            };

            graphics.FillPolygon(&brush, points, 3);

            Point inversePoints[3] = {
                Point(x + chevronWidth, y),
                Point(x, y + chevronHeight / 2),
                Point(x + chevronWidth, y + chevronHeight)
            };

            graphics.FillPolygon(&brush, inversePoints, 3);
        }
    }

    return bitmap;
}
Bitmap* CreateDiagonalChevronWeave(int width, int height, const Color& color1, const Color& color2) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    int chevronWidth = width / 6;
    int chevronHeight = height / 6;

    for (int y = 0; y < height; y += chevronHeight) {
        for (int x = (y / chevronHeight) % 2 == 0 ? 0 : chevronWidth / 2; x < width; x += chevronWidth) {
            SolidBrush brush((x / chevronWidth + y / chevronHeight) % 2 == 0 ? color1 : color2);

            // Create left diagonal
            Point leftPoints[3] = {
                Point(x, y),
                Point(x + chevronWidth / 2, y + chevronHeight),
                Point(x, y + chevronHeight)
            };
            graphics.FillPolygon(&brush, leftPoints, 3);

            // Create right diagonal
            Point rightPoints[3] = {
                Point(x + chevronWidth, y),
                Point(x + chevronWidth / 2, y + chevronHeight),
                Point(x + chevronWidth, y + chevronHeight)
            };
            graphics.FillPolygon(&brush, rightPoints, 3);
        }
    }

    return bitmap;
}
Bitmap* CreateFeatherTexture(int width, int height, const Color& featherColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    Pen featherPen(featherColor, 1);
    for (int x = 0; x < width; x += width / 5) {
        for (int y = 0; y < height; y += height / 3) {
            graphics.DrawBezier(&featherPen, Point(x, y), Point(x + 5, y + 10), Point(x - 5, y + 20), Point(x, y + 30));
        }
    }

    return bitmap;
}
Bitmap* CreateCircuitLEDMatrix(int width, int height, const Color& ledColor, const Color& backgroundColor) {
    const int GRID_SIZE = 32; // Enforce 32x32 tile size
    Bitmap* bitmap = new Bitmap(GRID_SIZE, GRID_SIZE, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeNone); // Pixel art precision

    // Fill background with subtle circuit texture
    SolidBrush backgroundBrush(backgroundColor);
    graphics.FillRectangle(&backgroundBrush, 0, 0, GRID_SIZE, GRID_SIZE);
    Pen tracePen(Color(50, ledColor.GetR(), ledColor.GetG(), ledColor.GetB()), 1); // Faint traces
    graphics.DrawLine(&tracePen, 0, GRID_SIZE / 2, GRID_SIZE, GRID_SIZE / 2); // Horizontal
    graphics.DrawLine(&tracePen, GRID_SIZE / 2, 0, GRID_SIZE / 2, GRID_SIZE); // Vertical

    // Draw LED grid with connectors
    SolidBrush ledBrush(ledColor);
    int spacing = 8; // Adjusted for 32x32 (4x4 grid)
    int ledSize = 4; // Smaller LEDs for detail

    for (int y = spacing / 2; y < GRID_SIZE; y += spacing) {
        for (int x = spacing / 2; x < GRID_SIZE; x += spacing) {
            graphics.FillEllipse(&ledBrush, x - ledSize / 2, y - ledSize / 2, ledSize, ledSize);
            // Add faint "wire" connections
            if (x > spacing / 2) graphics.DrawLine(&tracePen, x - spacing, y, x, y); // Horizontal wire
            if (y > spacing / 2) graphics.DrawLine(&tracePen, x, y - spacing, x, y); // Vertical wire
        }
    }

    // Add corner accents for cybernetic flair
    SolidBrush accentBrush(Color(255, ledColor.GetR() + 50, ledColor.GetG() + 50, ledColor.GetB()));
    graphics.FillRectangle(&accentBrush, 0, 0, 4, 4); // Top-left
    graphics.FillRectangle(&accentBrush, GRID_SIZE - 4, GRID_SIZE - 4, 4, 4); // Bottom-right

    return bitmap;
}
Bitmap* CreateHexagonalCircuitGrid(int width, int height, const Color& traceColor, const Color& backgroundColor) {
    const int GRID_SIZE = 32; // Enforce 32x32 tile size
    Bitmap* bitmap = new Bitmap(GRID_SIZE, GRID_SIZE, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeNone); // Pixel art precision

    // Fill background with subtle texture
    SolidBrush backgroundBrush(backgroundColor);
    graphics.FillRectangle(&backgroundBrush, 0, 0, GRID_SIZE, GRID_SIZE);
    Pen faintPen(Color(50, traceColor.GetR(), traceColor.GetG(), traceColor.GetB()), 1);
    graphics.DrawRectangle(&faintPen, 0, 0, GRID_SIZE - 1, GRID_SIZE - 1); // Border

    // Draw hexagonal grid
    Pen tracePen(traceColor, 1); // Thinner for pixel art
    const int size = 8;  // Smaller hexagons for 32x32 (roughly 3 fit diagonally)
    float dx = size * 1.5f;  // Horizontal offset
    float dy = size * sqrt(3.0f);  // Vertical offset

    for (float y = -dy / 2; y < GRID_SIZE + dy; y += dy) { // Start off-screen for coverage
        bool offset = fmod(y / dy + 10, 2) < 1; // Adjusted for consistent staggering
        for (float x = offset ? -dx / 2 : dx / 2; x < GRID_SIZE + dx; x += dx) {
            PointF hex[6];
            for (int i = 0; i < 6; ++i) {
                float angle = i * 60.0f * 3.14159265f / 180.0f;
                hex[i] = PointF(x + size * cos(angle), y + size * sin(angle));
            }
            graphics.DrawPolygon(&tracePen, hex, 6);

            // Add central "node" dot (fixed casting issue)
            SolidBrush nodeBrush(traceColor);
            graphics.FillEllipse(&nodeBrush, static_cast<int>(x - 1), static_cast<int>(y - 1), 2, 2);
        }
    }

    return bitmap;
}
Bitmap* CreateArcaneCircuitGlyphs(int width, int height, const Color& traceColor, const Color& glyphColor, const Color& backgroundColor) {
    const int GRID_SIZE = 32; // Enforce 32x32 tile size
    Bitmap* bitmap = new Bitmap(GRID_SIZE, GRID_SIZE, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeNone); // Pixel art precision

    // Fill background with subtle arcane texture
    SolidBrush backgroundBrush(backgroundColor);
    graphics.FillRectangle(&backgroundBrush, 0, 0, GRID_SIZE, GRID_SIZE);
    Pen faintPen(Color(50, traceColor.GetR(), traceColor.GetG(), traceColor.GetB()), 1);
    graphics.DrawRectangle(&faintPen, 0, 0, GRID_SIZE - 1, GRID_SIZE - 1); // Faint border

    // Draw circuit-like energy paths
    Pen tracePen(traceColor, 1); // Thinner for pixel art
    graphics.DrawLine(&tracePen, GRID_SIZE / 2, 0, GRID_SIZE / 2, GRID_SIZE); // Vertical
    graphics.DrawLine(&tracePen, 0, GRID_SIZE / 2, GRID_SIZE, GRID_SIZE / 2); // Horizontal
    // Diagonal accents for arcane energy flow
    graphics.DrawLine(&tracePen, 0, 0, 8, 8);
    graphics.DrawLine(&tracePen, GRID_SIZE - 1, 0, GRID_SIZE - 9, 8);
    graphics.DrawLine(&tracePen, 0, GRID_SIZE - 1, 8, GRID_SIZE - 9);
    graphics.DrawLine(&tracePen, GRID_SIZE - 1, GRID_SIZE - 1, GRID_SIZE - 9, GRID_SIZE - 9);

    // Draw arcane glyphs at key points
    SolidBrush glyphBrush(glyphColor);
    graphics.FillEllipse(&glyphBrush, GRID_SIZE / 2 - 3, GRID_SIZE / 4 - 3, 6, 6); // Top center
    graphics.FillEllipse(&glyphBrush, GRID_SIZE / 2 - 3, 3 * GRID_SIZE / 4 - 3, 6, 6); // Bottom center
    graphics.FillEllipse(&glyphBrush, GRID_SIZE / 4 - 3, GRID_SIZE / 2 - 3, 6, 6); // Left center
    graphics.FillEllipse(&glyphBrush, 3 * GRID_SIZE / 4 - 3, GRID_SIZE / 2 - 3, 6, 6); // Right center
    // Add smaller corner glyphs
    graphics.FillRectangle(&glyphBrush, 2, 2, 3, 3); // Top-left
    graphics.FillRectangle(&glyphBrush, GRID_SIZE - 5, GRID_SIZE - 5, 3, 3); // Bottom-right

    return bitmap;
}
Bitmap* CreateAlchemicalPowerCore(int width, int height, const Color& ringColor, const Color& sigilColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(32, 32, PixelFormat32bppARGB);  // Force 32x32 for sprite sheet
    Graphics graphics(bitmap);

    // Fill background
    SolidBrush backgroundBrush(Color(255, backgroundColor.GetR(), backgroundColor.GetG(), backgroundColor.GetB()));
    graphics.FillRectangle(&backgroundBrush, 0, 0, 32, 32);

    // Draw concentric magic-tech rings
    Pen ringPen(Color(255, ringColor.GetR(), ringColor.GetG(), ringColor.GetB()), 1);  // Thin pen for pixel art
    int centerX = 16;  // 32 / 2
    int centerY = 16;
    int radii[] = { 6, 12 };  // Fixed radii for simplicity and alignment

    for (int i = 0; i < 2; i++) {
        int r = radii[i];
        graphics.DrawEllipse(&ringPen, centerX - r, centerY - r, r * 2, r * 2);
    }

    // Add alchemical sigil points (simplified to 4 cardinal points)
    SolidBrush sigilBrush(Color(255, sigilColor.GetR(), sigilColor.GetG(), sigilColor.GetB()));
    int sigilSize = 4;  // Small for pixel art clarity
    graphics.FillEllipse(&sigilBrush, 14, 2, sigilSize, sigilSize);   // Top
    graphics.FillEllipse(&sigilBrush, 14, 26, sigilSize, sigilSize);  // Bottom
    graphics.FillEllipse(&sigilBrush, 2, 14, sigilSize, sigilSize);   // Left
    graphics.FillEllipse(&sigilBrush, 26, 14, sigilSize, sigilSize);  // Right

    return bitmap;
}
Bitmap* CreateCelestialDataRings(int width, int height, const Color& ringColor, const Color& starColor, const Color& backgroundColor) {
    const int GRID_SIZE = 32; // Enforce 32x32 tile size
    Bitmap* bitmap = new Bitmap(GRID_SIZE, GRID_SIZE, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.SetSmoothingMode(SmoothingModeNone); // Pixel art precision

    // Fill background with subtle cosmic texture
    SolidBrush backgroundBrush(backgroundColor);
    graphics.FillRectangle(&backgroundBrush, 0, 0, GRID_SIZE, GRID_SIZE);
    Pen faintPen(Color(50, starColor.GetR(), starColor.GetG(), starColor.GetB()), 1);
    graphics.DrawEllipse(&faintPen, 2, 2, GRID_SIZE - 5, GRID_SIZE - 5); // Faint outer orbit

    // Draw celestial rings
    Pen ringPen(ringColor, 1); // Thinner for pixel art
    int centerX = GRID_SIZE / 2;
    int centerY = GRID_SIZE / 2;
    int maxRadius = GRID_SIZE / 2; // Adjusted for 32x32
    int step = 6; // Tighter spacing

    for (int r = step; r <= maxRadius; r += step) {
        // Broken arcs for cosmic motion
        graphics.DrawArc(&ringPen, centerX - r, centerY - r, r * 2, r * 2, 45, 90);
        graphics.DrawArc(&ringPen, centerX - r, centerY - r, r * 2, r * 2, 225, 90);
    }

    // Draw small glowing data stars
    SolidBrush starBrush(starColor);
    for (int angle = 0; angle < 360; angle += 60) { // Fewer stars for balance
        float rad = angle * 3.14159265f / 180.0f;
        int x = centerX + static_cast<int>(maxRadius * 0.7f * cos(rad));
        int y = centerY + static_cast<int>(maxRadius * 0.7f * sin(rad));
        graphics.FillEllipse(&starBrush, x - 2, y - 2, 4, 4); // Smaller stars
    }

    // Add central "data core"
    graphics.FillEllipse(&starBrush, centerX - 3, centerY - 3, 6, 6);

    return bitmap;
}
Bitmap* CreateFishScalePattern(int width, int height, const Color& scaleColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush brush(scaleColor);
    int radius = width / 8;
    int spacingX = radius * 2;
    int spacingY = radius;

    for (int y = 0; y < height + spacingY; y += spacingY) {
        for (int x = (y / spacingY) % 2 == 0 ? 0 : spacingX / 2; x < width + spacingX; x += spacingX) {
            graphics.FillPie(&brush, x - radius, y - radius, radius * 2, radius * 2, 0, 180); // Half-circle
        }
    }

    return bitmap;
}
Bitmap* CreateInterlockingCirclesPattern(int width, int height, const Color& ringColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    Pen ringPen(ringColor, 2);
    int ringRadius = width / 6;
    int spacing = ringRadius * 2;

    for (int y = 0; y < height + spacing; y += spacing) {
        for (int x = (y / spacing) % 2 == 0 ? 0 : spacing / 2; x < width + spacing; x += spacing) {
            graphics.DrawEllipse(&ringPen, x - ringRadius, y - ringRadius, ringRadius * 2, ringRadius * 2);
            graphics.DrawEllipse(&ringPen, x - ringRadius / 2, y - ringRadius / 2, ringRadius, ringRadius);
        }
    }

    return bitmap;
}
Bitmap* CreatePixelatedCamouflage(int width, int height, const Color& color1, const Color& color2, const Color& color3, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    Color colors[] = { color1, color2, color3 };
    SolidBrush* brushes[] = { new SolidBrush(color1), new SolidBrush(color2), new SolidBrush(color3) };

    int blockSize = width / 8;

    for (int y = 0; y < height; y += blockSize) {
        for (int x = 0; x < width; x += blockSize) {
            int patternType = (x / blockSize + y / blockSize) % 3;
            graphics.FillRectangle(brushes[patternType], x, y, blockSize, blockSize);
        }
    }

    // Clean up dynamically allocated brushes
    for (int i = 0; i < 3; ++i) {
        delete brushes[i];
    }

    return bitmap;
}
Bitmap* CreateTormentedSoilPattern(int width, int height, const Color& ashColor, const Color& flameColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush ashBrush(ashColor);
    graphics.FillRectangle(&ashBrush, 0, 0, width, height);

    SolidBrush flameBrush(flameColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Random cracks and burning edges in the damned terrain
            if ((modX % 3 == 0 && modY % 5 == 0) || (modX * modY) % 7 == 0) {
                graphics.FillRectangle(&flameBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateOrganicFractalPattern(int width, int height, const Color& growthColor, const Color& alienSoilColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush soilBrush(alienSoilColor);
    graphics.FillRectangle(&soilBrush, 0, 0, width, height);

    SolidBrush growthBrush(growthColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Alien fractal-like structures
            if ((modX * modY) % 7 == 0 || (modX + modY) % 5 == 0) {
                graphics.FillRectangle(&growthBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateClockworkPattern(int width, int height, const Color& gearColor, const Color& steelColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush steelBrush(steelColor);
    graphics.FillRectangle(&steelBrush, 0, 0, width, height);

    SolidBrush gearBrush(gearColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Gears appearing in a repeating pattern, ensuring interlocking
            if ((modX == modY) || (modX % 4 == 0 && modY % 4 == 0)) {
                graphics.FillRectangle(&gearBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateNeonCircuitPattern(int width, int height, const Color& circuitColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush backgroundBrush(backgroundColor);
    graphics.FillRectangle(&backgroundBrush, 0, 0, width, height);

    SolidBrush circuitBrush(circuitColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Create circuit traces that form a glowing, tessellating structure
            if ((modX == 2 || modY == 2) || ((modX % 4 == 0) && (modY % 4 == 0))) {
                graphics.FillRectangle(&circuitBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateHyperspacePattern(int width, int height, const Color& energyColor, const Color& voidColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush voidBrush(voidColor);
    graphics.FillRectangle(&voidBrush, 0, 0, width, height);

    SolidBrush energyBrush(energyColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Create warped grid distortions
            if ((modX == modY) || (modX == 7 - modY) || (modX % 4 == 0 && modY % 3 == 0)) {
                graphics.FillRectangle(&energyBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateNightmarePattern(int width, int height, const Color& veinColor, const Color& darkColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush darkBrush(darkColor);
    graphics.FillRectangle(&darkBrush, 0, 0, width, height);

    SolidBrush veinBrush(veinColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Create chaotic vein-like streaks
            if ((modX == modY) || (modX % 3 == 0 && modY % 2 == 0)) {
                graphics.FillRectangle(&veinBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateSpiralLatticePattern(int width, int height, const Color& stoneColor, const Color& voidColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush stoneBrush(stoneColor);
    graphics.FillRectangle(&stoneBrush, 0, 0, width, height);

    SolidBrush voidBrush(voidColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Staircases and archways forming recursive curves
            if ((modX * modY) % 5 == 0 || ((modX + modY) % 7 == 0)) {
                graphics.FillRectangle(&voidBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateLightFracturePattern(int width, int height, const Color& lightColor, const Color& darkColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush darkBrush(darkColor);
    graphics.FillRectangle(&darkBrush, 0, 0, width, height);

    SolidBrush lightBrush(lightColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Light bursting from fractured space
            if ((modX + modY) % 6 == 0 || (modX * modY) % 7 == 0) {
                graphics.FillRectangle(&lightBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateNegativeGeometryPattern(int width, int height, const Color& voidColor, const Color& gridColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush voidBrush(voidColor);
    graphics.FillRectangle(&voidBrush, 0, 0, width, height);

    SolidBrush gridBrush(gridColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Inverted grid where absence forms the structure
            if ((modX + modY) % 4 == 0 || (modX * modY) % 9 == 0) {
                graphics.FillRectangle(&gridBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateUnrealityTidePattern(int width, int height, const Color& waveColor, const Color& voidColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush voidBrush(voidColor);
    graphics.FillRectangle(&voidBrush, 0, 0, width, height);

    SolidBrush waveBrush(waveColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Tidal motion based on sine wave patterns
            if ((int)(sin(modX * 0.4) * 4) == modY % 4) {
                graphics.FillRectangle(&waveBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateRecursiveWellPattern(int width, int height, const Color& abyssColor, const Color& echoColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);

    SolidBrush abyssBrush(abyssColor);
    graphics.FillRectangle(&abyssBrush, 0, 0, width, height);

    SolidBrush echoBrush(echoColor);

    int patternSize = 8;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int modX = x % patternSize;
            int modY = y % patternSize;

            // Nested, spiraling voids, implying infinite descent
            if ((modX * modY) % 6 == 0 || (modX + modY) % 4 == 0) {
                graphics.FillRectangle(&echoBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateNeonGrid(int size, const Color& neonColor, const Color& bgColor) {
    // Force 32x32 size for consistency
    Bitmap* bitmap = new Bitmap(32, 32, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(bgColor);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias); // Smooth edges for cleaner tiling

    Pen neonPen(neonColor, 1.0f); // Consistent thin lines

    // Draw a grid with 8-pixel spacing, adjusted for tessellation
    for (int i = 0; i <= 32; i += 8) {
        // Alternate thickness for neon effect without overcomplicating
        if (i % 16 == 0) {
            neonPen.SetWidth(2.0f); // Thicker every 16 pixels
        }
        else {
            neonPen.SetWidth(1.0f); // Thinner for contrast
        }
        // Vertical lines
        graphics.DrawLine(&neonPen, i, 0, i, 31);
        // Horizontal lines
        graphics.DrawLine(&neonPen, 0, i, 31, i);
    }

    return bitmap;
}
Bitmap* CreateCrystallineStructure(int width, int height, const Color& crystalColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(32, 32, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias); // Smooth edges for cleaner tiling

    SolidBrush brush(crystalColor);
    int crystalSize = 8; // Fixed to fit 32x32 grid (4 crystals wide)

    for (int y = 0; y < 32; y += crystalSize) {
        // Offset every other row by half a crystal for hexagonal packing
        int xOffset = (y / crystalSize) % 2 == 0 ? 0 : crystalSize / 2;
        for (int x = xOffset; x < 32; x += crystalSize) {
            Point points[6] = {
                Point(x, y + crystalSize / 2),           // Left-middle
                Point(x + crystalSize / 2, y),           // Top
                Point(x + crystalSize, y + crystalSize / 2), // Right-middle
                Point(x + crystalSize, y + crystalSize),     // Bottom-right
                Point(x + crystalSize / 2, y + crystalSize * 3 / 2), // Bottom
                Point(x, y + crystalSize)                // Bottom-left
            };
            graphics.FillPolygon(&brush, points, 6);
        }
    }

    return bitmap;
}
Bitmap* CreateBarkTexture(int width, int height, const Color& barkColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush barkBrush(barkColor);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if ((x + y) % 4 == 0 || (x * y) % 16 == 0) {
                graphics.FillRectangle(&barkBrush, x, y, 1, 1);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateStoneMosaicPattern(int width, int height, const Color& stoneColor, const Color& mortarColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(mortarColor);

    SolidBrush stoneBrush(stoneColor);
    int stoneSize = width / 6;

    for (int y = 0; y < height; y += stoneSize) {
        for (int x = (y / stoneSize) % 2 == 0 ? 0 : stoneSize / 2; x < width; x += stoneSize) {
            graphics.FillEllipse(&stoneBrush, x - stoneSize / 2, y - stoneSize / 2, stoneSize, stoneSize);
        }
    }

    return bitmap;
}
Bitmap* CreateOrganicMeshPattern(int width, int height, const Color& meshColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush meshBrush(meshColor);
    Pen meshPen(meshColor, 2);  // Use a Pen for drawing lines
    int meshSize = width / 8;

    for (int y = 0; y < height; y += meshSize) {
        for (int x = (y / meshSize) % 2 == 0 ? 0 : meshSize / 2; x < width; x += meshSize) {
            graphics.FillEllipse(&meshBrush, x, y, meshSize / 2, meshSize / 2);
            if (rand() % 2 == 0) { // Randomly connecting
                graphics.DrawLine(&meshPen, x, y + meshSize / 2, x + meshSize, y + meshSize / 2); // Use meshPen
            }
        }
    }

    return bitmap;
}
Bitmap* CreateTimeWarpPattern(int width, int height, const Color& color1, const Color& color2, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush brush1(color1);
    SolidBrush brush2(color2);
    int segmentWidth = width / 8;

    for (int y = 0; y < height; y += segmentWidth) {
        for (int x = 0; x < width; x += segmentWidth) {
            if ((x / segmentWidth + y / segmentWidth) % 2 == 0) {
                graphics.FillPie(&brush1, x, y, segmentWidth, segmentWidth, 0, 180);
                graphics.FillPie(&brush2, x, y, segmentWidth, segmentWidth, 180, 180);
            }
            else {
                graphics.FillPie(&brush2, x, y, segmentWidth, segmentWidth, 0, 180);
                graphics.FillPie(&brush1, x, y, segmentWidth, segmentWidth, 180, 180);
            }
        }
    }

    return bitmap;
}

Bitmap* CreateQuantumMatrixPattern(int width, int height, const Color& matrixColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush matrixBrush(matrixColor);
    int cellSize = width / 16;

    for (int y = 0; y < height; y += cellSize) {
        for (int x = 0; x < width; x += cellSize) {
            if (rand() % 100 < 30) { // 30% chance to draw a cell
                graphics.FillRectangle(&matrixBrush, x, y, cellSize, cellSize);
            }
            // Add some vertical lines for a matrix effect
            if (x % (cellSize * 2) == 0) {
                graphics.FillRectangle(&matrixBrush, x, y, 1, cellSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateAlienFloraPattern(int width, int height, const Color& floraColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush floraBrush(floraColor);
    Pen floraPen(floraColor, 2.0f); // Use a Pen for drawing tendrils
    int floraSize = width / 8;

    for (int y = 0; y < height; y += floraSize) {
        for (int x = (y / floraSize) % 2 == 0 ? 0 : floraSize / 2; x < width; x += floraSize) {
            graphics.FillEllipse(&floraBrush, x, y, floraSize / 2, floraSize / 2);
            // Add tendrils for an alien look
            for (int i = 0; i < 4; ++i) {
                float angle = i * 90.0f * 3.14159f / 180.0f;
                int endX = x + floraSize / 2 + static_cast<int>(floraSize / 4 * cosf(angle));
                int endY = y + floraSize / 2 + static_cast<int>(floraSize / 4 * sinf(angle));
                graphics.DrawLine(&floraPen, x + floraSize / 2, y + floraSize / 2, endX, endY);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateLeafDecay(int width, int height, const Color& leafColor, const Color& decayColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush leafBrush(leafColor);
    SolidBrush decayBrush(decayColor);
    Pen leafPen(leafColor, 2.0f); // Create a Pen for drawing outlines

    // Simulate leaves in various stages of decay
    for (int y = 0; y < height; y += 16) {
        for (int x = 0; x < width; x += 16) {
            if ((x * y) % 32 == 0) {
                graphics.FillEllipse(&leafBrush, x, y, 16, 16); // Full leaf
                graphics.FillEllipse(&decayBrush, x + 4, y + 4, 8, 8); // Decay effect
            }
            else {
                // Skeletonized leaf
                graphics.DrawEllipse(&leafPen, x, y, 16, 16);
                graphics.DrawLine(&leafPen, x, y + 8, x + 16, y + 8);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateNaturesCamouflage(int width, int height, const Color& primaryColor, const Color& secondaryColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(32, 32, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush primaryBrush(primaryColor);
    SolidBrush secondaryBrush(secondaryColor);

    // Checkerboard pattern with 4x4 blocks for camouflage
    for (int y = 0; y < 32; y += 4) {
        for (int x = 0; x < 32; x += 4) {
            if (((x / 4) + (y / 4)) % 2 == 0) { // Simplified checkerboard logic
                graphics.FillRectangle(&primaryBrush, x, y, 4, 4);
            }
            else {
                graphics.FillRectangle(&secondaryBrush, x, y, 4, 4);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateIceFracture(int width, int height, const Color& iceColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(32, 32, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias); // Smooth edges for cleaner tiling

    SolidBrush iceBrush(iceColor);

    // Simplified ice fracture pattern on an 8x8 staggered grid
    for (int y = 0; y < 32; y += 8) {
        int xOffset = (y / 8) % 2 == 0 ? 0 : 8; // Stagger for even distribution
        for (int x = xOffset; x < 32; x += 16) {
            Point points[] = {
                Point(x, y),           // Top-left
                Point(x + 8, y + 8),   // Bottom-middle
                Point(x + 16, y),      // Top-right
                Point(x, y)            // Back to start
            };
            graphics.FillPolygon(&iceBrush, points, 4);
        }
    }

    return bitmap;
}
Bitmap* CreateNanoBotSwarm(int width, int height, const Color& botColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush botBrush(botColor);
    Pen botTrailPen(botColor, 0.5f);

    int botSize = 2;
    int swarmDensity = 3;

    // Generate bots with movement trails
    for (int y = 0; y < height; y += swarmDensity) {
        for (int x = 0; x < width; x += swarmDensity) {
            if ((x * y) % 7 < 3) { // Introduce slight randomness
                int offsetX = (rand() % 3) - 1;
                int offsetY = (rand() % 3) - 1;

                // Draw nano-bot
                graphics.FillRectangle(&botBrush, x + offsetX, y + offsetY, botSize, botSize);

                // Simulate a motion trail
                if (rand() % 3 == 0) {
                    graphics.DrawLine(&botTrailPen, x + offsetX, y + offsetY, x + offsetX - 2, y + offsetY + 2);
                }
            }
        }
    }

    return bitmap;
}
Bitmap* CreateSolarPanelArray(int width, int height, const Color& panelColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush panelBrush(panelColor);

    // Solar panel size and gap
    int panelWidth = width / 15;
    int panelHeight = height / 5;
    int gap = 3;  // Increase gap for spacing

    // Add subtle detail by drawing a darker border for the panels to simulate depth
    Pen borderPen(Color(0, 100, 100, 100), 1); // Dark border color

    for (int y = 0; y < height; y += panelHeight + gap) {
        for (int x = 0; x < width; x += panelWidth + gap) {
            // Draw panel with shading effect (a simple gradient or darker panel color at the top)
            SolidBrush topShadeBrush(Color(60, 60, 60));  // Slight darker shade for the top of the panel
            graphics.FillRectangle(&topShadeBrush, x, y, panelWidth, panelHeight / 2); // Top half shading

            // Draw the bottom half of the panel
            graphics.FillRectangle(&panelBrush, x, y + panelHeight / 2, panelWidth, panelHeight / 2);

            // Draw panel borders for a 3D look
            graphics.DrawRectangle(&borderPen, x, y, panelWidth, panelHeight);
        }
    }

    return bitmap;
}
Bitmap* CreateFactorySilhouettePattern(int width, int height, const Color& silhouetteColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush silhouetteBrush(silhouetteColor);

    // Machinery and structure sizes
    int shapeWidth = width / 12;
    int shapeHeight = height / 6;
    int smokestackWidth = shapeWidth / 4;
    int smokestackHeight = height / 4;

    // Add buildings (rectangular shapes) to represent factory structures
    for (int y = 0; y < height; y += shapeHeight * 2) {
        for (int x = 0; x < width; x += shapeWidth * 2) {
            // Draw main building (rectangular blocks)
            graphics.FillRectangle(&silhouetteBrush, x, y + shapeHeight / 2, shapeWidth, shapeHeight);

            // Add smaller rounded features to simulate silos or tanks
            graphics.FillEllipse(&silhouetteBrush, x + shapeWidth / 2, y + shapeHeight / 2, shapeWidth, shapeHeight);

            // Add smokestacks (tall narrow rectangles)
            graphics.FillRectangle(&silhouetteBrush, x + shapeWidth / 4, y - smokestackHeight / 2, smokestackWidth, smokestackHeight);
        }
    }

    return bitmap;
}
Bitmap* CreateNanoTechGrid(int width, int height, const Color& gridColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    Pen gridPen(gridColor);
    gridPen.SetDashStyle(DashStyleDot); // Subtle dotted lines for a nano-tech feel

    // Draw very fine grid lines
    int gridSpacing = 3; // Small spacing for nano-tech look
    for (int y = 0; y < height; y += gridSpacing) {
        graphics.DrawLine(&gridPen, 0, y, width, y);
    }
    for (int x = 0; x < width; x += gridSpacing) {
        graphics.DrawLine(&gridPen, x, 0, x, height);
    }

    // Add nodes (small circles) at grid intersections
    SolidBrush nodeBrush(gridColor);
    for (int y = gridSpacing; y < height; y += gridSpacing) {
        for (int x = gridSpacing; x < width; x += gridSpacing) {
            graphics.FillEllipse(&nodeBrush, x - 1, y - 1, 3, 3); // Small circles to represent nodes
        }
    }

    return bitmap;
}
Bitmap* CreateCircuitOverloadPattern(int width, int height, const Color& overloadColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);

    SolidBrush overloadBrush(overloadColor);

    // Create chaotic overload pattern with varying block sizes
    int maxBlockSize = 8;
    for (int y = 0; y < height; y += 4) {
        for (int x = 0; x < width; x += 4) {
            // Create some randomness with the size and position of blocks
            int blockSize = (rand() % maxBlockSize) + 2; // Random block size between 2 and maxBlockSize
            if ((x + y) % 8 == 0 || (rand() % 10 < 2)) { // Add randomness to placement
                // Slight offsets in x, y for variation
                int offsetX = rand() % 3 - 1; // -1, 0, or 1
                int offsetY = rand() % 3 - 1; // -1, 0, or 1

                graphics.FillRectangle(&overloadBrush, x + offsetX, y + offsetY, blockSize, blockSize);
            }
        }
    }

    return bitmap;
}
Bitmap* CreateMysticalFog(int width, int height, const Color& fogColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(32, 32, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias); // Smooth edges for cleaner tiling

    // Define semi-transparent fog layers
    Color denseFogColor(96, fogColor.GetR(), fogColor.GetG(), fogColor.GetB());   // Denser fog (alpha 96)
    Color lightFogColor(48, fogColor.GetR(), fogColor.GetG(), fogColor.GetB());   // Lighter fog (alpha 48)
    SolidBrush denseFogBrush(denseFogColor);
    SolidBrush lightFogBrush(lightFogColor);
    Pen fogPen(fogColor, 1.0f); // Thin pen for biomechanical wisps

    // Overlapping fog ellipses on a staggered grid
    for (int y = 0; y < 32; y += 8) {
        int xOffset = (y / 8) % 2 == 0 ? 0 : 8; // Stagger for organic flow
        for (int x = xOffset; x < 32; x += 16) {
            // Dense fog core
            graphics.FillEllipse(&denseFogBrush, x - 4, y - 4, 16, 16);
            // Lighter fog halo
            graphics.FillEllipse(&lightFogBrush, x - 8, y - 8, 24, 24);
            // Biomechanical wisp
            int wispX = x + static_cast<int>(4.0f * sinf(y * 0.2f)); // Subtle wave for wisp
            int wispY = y + 4;
            graphics.DrawLine(&fogPen, x, y, wispX, wispY); // Short, wavy tendril
        }
    }

    return bitmap;
}
Bitmap* CreateClassicMaze(int width, int height, const Color& wallColor, const Color& pathColor, const Color& backgroundColor) {
    Bitmap* bitmap = new Bitmap(32, 32, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    graphics.Clear(backgroundColor);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias); // Smooth edges for cleaner tiling

    SolidBrush wallBrush(wallColor);
    SolidBrush pathBrush(pathColor);
    Pen wallPen(wallColor, 1.0f); // Thin pen for biomechanical details

    // Create a maze-like pattern with 4x4 blocks
    for (int y = 0; y < 32; y += 4) {
        for (int x = 0; x < 32; x += 4) {
            // Define wall pattern with a more intricate layout
            bool isWall = ((x / 4 + y / 4) % 3 == 0) || (x % 8 == 0 && y % 16 == 0) || (y % 8 == 0 && x % 16 == 0);
            if (isWall) {
                graphics.FillRectangle(&wallBrush, x, y, 4, 4); // Wall blocks
                // Add biomechanical detail to walls
                if ((x / 4 + y / 4) % 3 == 0) {
                    graphics.DrawLine(&wallPen, x + 1, y + 1, x + 3, y + 3); // Diagonal circuit trace
                }
            }
            else {
                graphics.FillRectangle(&pathBrush, x, y, 4, 4); // Path blocks
            }
        }
    }

    return bitmap;
}



//
void LoadSprites() {

    // **Whites & Light Neutrals**  
    Color white_color(255, 255, 255);            // Pure White  
    Color ivory_color(255, 255, 240);            // Soft Ivory  
    Color beige_color(245, 245, 220);            // Neutral Beige  
    Color wheat_color(245, 222, 179);            // Warm Wheat  
    Color tan_color(210, 180, 140);              // Sandy Brown  

    // **Yellows & Oranges**  
    Color lemon_color(255, 247, 0);              // Bright Lemon Yellow  
    Color amber_color(255, 191, 0);              // Vivid Amber  
    Color gold_color(255, 215, 0);               // Classic Gold  
    Color saffron_color(244, 196, 48);           // Deep Saffron Yellow  
    Color orange_color(255, 165, 0);             // Pure Orange  
    Color burntOrange_color(204, 85, 0);         // Deep Burnt Orange  
    Color rust_color(183, 65, 14);               // Rusty Orange  

    // **Reds & Pinks**  
    Color lightSalmon_color(255, 160, 122);      // Soft Light Salmon  
    Color vermilion_color(227, 66, 52);          // Bright Vermilion  
    Color coral_color(255, 50, 71);              // Warm Coral  
    Color crimson_color(220, 20, 60);            // Deep Crimson  
    Color red_color(255, 0, 0);                  // True Red  
    Color maroon_color(128, 0, 0);               // Dark Maroon  
    Color hotPink_color(255, 105, 180);          // Vibrant Hot Pink  
    Color magenta_color(255, 0, 255);            // Electric Magenta  

    // **Earth Tones & Browns**  
    Color saddleBrown_color(139, 69, 19);        // Deep Saddle Brown  
    Color sepia_color(112, 66, 20);              // Warm Sepia  
    Color sienna_color(160, 82, 45);             // Rich Sienna  
    Color reddishBrown_color(210, 100, 10);      // Rusty Reddish Brown  
    Color brown_color(165, 42, 42);              // Standard Brown  
    Color mahogany_color(192, 64, 0);            // Deep Mahogany  
    Color chestnut_color(205, 92, 92);           // Muted Chestnut  

    // **Greens - Grass & Foliage**  
    Color chartreuse_color(127, 255, 0);         // Bright Chartreuse  
    Color brightGreen_color(76, 175, 80);        // Lush Bright Green  
    Color springGreen_color(0, 255, 127);        // Vivid Spring Green  
    Color lightGreen_color(144, 238, 144);       // Soft Light Green  
    Color yellowGreen_color(154, 205, 50);       // Yellow-Green  
    Color limeGreen_color(50, 205, 50);          // Vivid Lime Green  
    Color olive_color(107, 142, 35);             // True Olive Green  
    Color mossGreen_color(173, 223, 173);        // Soft Moss Green  
    Color darkOliveGreen_color(85, 107, 47);     // Deep Dark Olive Green  
    Color forestGreen_color(34, 139, 34);        // Rich Forest Green  
    Color deepGreen_color(0, 100, 0);            // Dark Forest Green  
    Color seaGreen_color(46, 139, 87);           // Balanced Sea Green  

    // **Blues - Water & Sky**  
    Color skyBlue_color(135, 206, 235);          // Clear Sky Blue  
    Color freshWaterBlue_color(173, 216, 230);   // Fresh Water Blue  
    Color deepFreshWaterBlue_color(135, 206, 250); // Deep Fresh Water Blue  
    Color cornflowerBlue_color(100, 149, 237);   // Soft Cornflower Blue  
    Color steelBlue_color(70, 130, 180);         // Sea Water Blue  
    Color dodgerBlue_color(30, 144, 255);        // Bright Dodger Blue  
    Color royalBlue_color(65, 105, 225);         // Deep Royal Blue  
    Color pureBlue_color(0, 0, 255);             // True Blue  
    Color midnightBlue_color(25, 25, 112);       // Dark Midnight Blue  
    Color deepSeaBlue_color(0, 0, 139);          // Deep Sea Blue  

    // **Purples & Violets**  
    Color lavender_color(230, 230, 250);         // Soft Lavender  
    Color thistle_color(216, 191, 216);          // Muted Thistle  
    Color orchid_color(218, 112, 214);           // Rich Orchid  
    Color violet_color(238, 130, 238);           // Pure Violet  
    Color darkOrchid_color(153, 50, 204);        // Deep Dark Orchid  
    Color indigo_color(75, 0, 130);              // Intense Indigo  

    // **Neutrals - Rocks, Shadows, & Structures**  
    Color lightGray_color(211, 211, 211);        // Soft Light Gray  
    Color gray_color(128, 128, 128);             // Neutral Gray  
    Color lightSlateGray_color(160, 150, 150);   // Muted Light Slate Gray  
    Color ashGray_color(178, 190, 181);          // Soft Ash Gray  
    Color slateGray_color(112, 128, 144);        // Balanced Slate Gray  
    Color darkGray_color(169, 169, 169);         // Dark Gray  
    Color dimGray_color(105, 105, 105);          // Standard Dim Gray  
    Color charcoal_color(50, 50, 50);            // Charcoal Gray  
    Color gunmetalGray_color(42, 52, 57);        // Cool Gunmetal Gray  
    Color veryDarkGray_color(30, 30, 30);        // Very Dark Gray  
    Color jetBlack_color(20, 20, 20);            // Deep Jet Black  

        //pattern creation via method and color

    //checkerboard_pattern_1 = CreateCheckerboardPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    red_solid_pattern = CreateSolidColorBlock(GRID_SIZE, GRID_SIZE, red_color);
    a_text_pattern = CreateTextBitmap('A', red_color);
    b_text_pattern = CreateTextBitmap('B', red_color);
    c_text_pattern = CreateTextBitmap('C', red_color);
    d_text_pattern = CreateTextBitmap('D', red_color);
    e_text_pattern = CreateTextBitmap('E', red_color);
    f_text_pattern = CreateTextBitmap('F', red_color);
    g_text_pattern = CreateTextBitmap('G', red_color);
    h_text_pattern = CreateTextBitmap('H', red_color);
    i_text_pattern = CreateTextBitmap('I', red_color);
    j_text_pattern = CreateTextBitmap('J', red_color);
    k_text_pattern = CreateTextBitmap('K', red_color);
    l_text_pattern = CreateTextBitmap('L', red_color);
    m_text_pattern = CreateTextBitmap('M', red_color);
    n_text_pattern = CreateTextBitmap('N', red_color);
    o_text_pattern = CreateTextBitmap('O', red_color);
    p_text_pattern = CreateTextBitmap('P', red_color);
    q_text_pattern = CreateTextBitmap('Q', red_color);
    r_text_pattern = CreateTextBitmap('R', red_color);
    s_text_pattern = CreateTextBitmap('S', red_color);
    t_text_pattern = CreateTextBitmap('T', red_color);
    u_text_pattern = CreateTextBitmap('U', red_color);
    v_text_pattern = CreateTextBitmap('V', red_color);
    w_text_pattern = CreateTextBitmap('W', red_color);
    x_text_pattern = CreateTextBitmap('X', red_color);
    y_text_pattern = CreateTextBitmap('Y', red_color);
    z_text_pattern = CreateTextBitmap('Z', red_color);
    zero_text_pattern = CreateTextBitmap('0', red_color);
    one_text_pattern = CreateTextBitmap('1', red_color);
    two_text_pattern = CreateTextBitmap('2', red_color);
    three_text_pattern = CreateTextBitmap('3', red_color);
    four_text_pattern = CreateTextBitmap('4', red_color);
    five_text_pattern = CreateTextBitmap('5', red_color);
    six_text_pattern = CreateTextBitmap('6', red_color);
    seven_text_pattern = CreateTextBitmap('7', red_color);
    eight_text_pattern = CreateTextBitmap('8', red_color);
    nine_text_pattern = CreateTextBitmap('9', red_color);
    question_text_pattern = CreateTextBitmap('?', red_color);
    exclaim_text_pattern = CreateTextBitmap('!', red_color);
    plus_text_pattern = CreateTextBitmap('+', red_color);
    equal_text_pattern = CreateTextBitmap('=', red_color);
    diamond_pattern_1 = CreateDiamondPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    chevron_pattern_1 = CreateChevronPattern(GRID_SIZE, GRID_SIZE, gray_color, white_color);
    offsetherringbone_pattern_1 = CreateOffsetHerringbone(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    zigzagherringbone_pattern_1 = CreateZigZagHerringbone(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    smoothzigzag_pattern_1 = CreateSmoothZigzagPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    vstripe_pattern_1 = CreateVStripePattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    herringbone_pattern_1 = CreateHerringbonePattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    denseplaid_pattern_1 = CreateDensePlaid(GRID_SIZE, GRID_SIZE, jetBlack_color, gray_color, white_color);
    diagonalherringbone_pattern_1 = CreateDiagonalHerringbone(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    concentricflowers_pattern_1 = CreateFlowerVariationConcentric(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    fractalhalftone_pattern_1 = CreateFractalHalftone(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    herringbonechevron_pattern = CreateHerringboneChevronMix(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    diagonalchevron_pattern = CreateDiagonalChevronWeave(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    feathertexture_pattern_1 = CreateFeatherTexture(GRID_SIZE, GRID_SIZE, gray_color, white_color);
    circuitledmatrix_pattern_1 = CreateCircuitLEDMatrix(GRID_SIZE, GRID_SIZE, red_color, jetBlack_color);
    hexagonalcircuitgrid_pattern_1 = CreateHexagonalCircuitGrid(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    arcanecircuitglyphs_pattern_1 = CreateArcaneCircuitGlyphs(GRID_SIZE, GRID_SIZE, white_color, gray_color, jetBlack_color);
    alchemicalpowercore_pattern_1 = CreateAlchemicalPowerCore(GRID_SIZE, GRID_SIZE, white_color, gold_color, jetBlack_color);
    celestialdatarings_pattern_1 = CreateCelestialDataRings(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color);
    fishscalepattern_pattern_1 = CreateFishScalePattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    interlockingcirclespattern_pattern_1 = CreateInterlockingCirclesPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    pixelatedcamouflage_pattern_1 = CreatePixelatedCamouflage(GRID_SIZE, GRID_SIZE, white_color, brown_color, gray_color, brown_color);
    tormentedsoilpattern_pattern_1 = CreateTormentedSoilPattern(GRID_SIZE, GRID_SIZE, brown_color, jetBlack_color);
    organicfractalpattern_pattern_1 = CreateOrganicFractalPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    clockworkpattern_pattern_1 = CreateClockworkPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    neoncircuitpattern_pattern_1 = CreateNeonCircuitPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    hyperspacepattern_pattern_1 = CreateHyperspacePattern(GRID_SIZE, GRID_SIZE, gray_color, white_color);
    nightmarepattern_pattern_1 = CreateNightmarePattern(GRID_SIZE, GRID_SIZE, jetBlack_color, red_color);
    spirallatticepattern_pattern_1 = CreateSpiralLatticePattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    lightfracturepattern_pattern_1 = CreateLightFracturePattern(GRID_SIZE, GRID_SIZE, white_color, jetBlack_color);
    negativegeometrypattern_pattern_1 = CreateNegativeGeometryPattern(GRID_SIZE, GRID_SIZE, white_color, jetBlack_color);
    unrealitytidepattern_pattern_1 = CreateUnrealityTidePattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    recursivewellpattern_pattern_1 = CreateRecursiveWellPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    neongrid_pattern_1 = CreateNeonGrid(1, white_color, jetBlack_color);
    crystallinestructure_pattern_1 = CreateCrystallineStructure(GRID_SIZE, GRID_SIZE, gray_color, white_color);
    barktexture_pattern_1 = CreateBarkTexture(GRID_SIZE, GRID_SIZE, brown_color, jetBlack_color);
    stonemosaicpattern_pattern_1 = CreateStoneMosaicPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    organicmeshpattern_pattern_1 = CreateOrganicMeshPattern(GRID_SIZE, GRID_SIZE, white_color, jetBlack_color);
    timewarppattern_pattern_1 = CreateTimeWarpPattern(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color, white_color);
    quantummatrixpattern_pattern_1 = CreateQuantumMatrixPattern(GRID_SIZE, GRID_SIZE, white_color, jetBlack_color);
    alienflorapattern_pattern_1 = CreateAlienFloraPattern(GRID_SIZE, GRID_SIZE, gray_color, white_color);
    leafdecay_pattern_1 = CreateLeafDecay(GRID_SIZE, GRID_SIZE, brown_color, jetBlack_color, gray_color);
    naturescamouflage_pattern_1 = CreateNaturesCamouflage(GRID_SIZE, GRID_SIZE, brown_color, jetBlack_color, white_color);
    icefracture_pattern_1 = CreateIceFracture(GRID_SIZE, GRID_SIZE, gray_color, white_color);
    nanobotswarm_pattern_1 = CreateNanoBotSwarm(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color);
    solarpanelarray_pattern_1 = CreateSolarPanelArray(GRID_SIZE, GRID_SIZE, white_color, jetBlack_color);
    factorysilhouettepattern_pattern_1 = CreateFactorySilhouettePattern(GRID_SIZE, GRID_SIZE, jetBlack_color, white_color);
    nanotechgrid_pattern_1 = CreateNanoTechGrid(GRID_SIZE, GRID_SIZE, gray_color, white_color);
    circuitoverloadpattern_pattern_1 = CreateCircuitOverloadPattern(GRID_SIZE, GRID_SIZE, red_color, jetBlack_color);
    mysticalfog_pattern_1 = CreateMysticalFog(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    classicmaze_pattern_1 = CreateClassicMaze(GRID_SIZE, GRID_SIZE, gray_color, jetBlack_color, white_color);
    canopyLeaves_pattern = CreateCanopyLeavesTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    desertSand_pattern = CreateDesertSandTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    lilyPads_pattern = CreateLilyPadsTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    brickPath_pattern = CreateBrickPathTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    algae_pattern = CreateAlgaeTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    flowers_pattern = CreateFlowersTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    holeInGround_pattern = CreateHoleInGroundTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    trash_pattern = CreateTrashTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    bush_pattern = CreateBushTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    gasolinePuddle_pattern = CreateGasolinePuddleTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    tNT_pattern = CreateTNTTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    rock_pattern = CreateRockTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    tallGrass_pattern = CreateTallGrassTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    shortGrass_pattern = CreateShortGrassTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    burntTree_pattern = CreateBurntTreeTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    seawater_pattern = CreateSeawaterTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    freshwater_pattern = CreateFreshwaterTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    fire_pattern = CreateFireTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    grasslands_pattern = CreateGrasslandsPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    boulderField_pattern = CreateBoulderFieldPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    sandDunes_pattern = CreateSandDunesTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    charredStump_pattern = CreateCharredStumpTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    autumnTree_pattern = CreateAutumnTreeTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    wavyGrassPatch_pattern = CreateWavyGrassPatchTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    largeBoulder_pattern = CreateLargeBoulderTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    thornyShrub_pattern = CreateThornyShrubTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    wildflowerPatch_pattern = CreateWildflowerPatchTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    forestCanopy_pattern = CreateForestCanopyTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    riverbedPebbles_pattern = CreateRiverbedPebblesPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    grassTuft_pattern = CreateGrassTuftPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    rockyOutcrop_pattern = CreateRockyOutcropPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    sandyRipple_pattern = CreateSandyRipplePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    leafVein_pattern = CreateLeafVeinPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    mossyStone_pattern = CreateMossyStonePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    wetMud_pattern = CreateWetMudPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    fernFrond_pattern = CreateFernFrondPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    snowDrift_pattern = CreateSnowDriftPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    volcanicAsh_pattern = CreateVolcanicAshPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    grassChevron_pattern = CreateGrassChevronPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    rockyDiamond_pattern = CreateRockyDiamondPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    dirtHerringbone_pattern = CreateDirtHerringbonePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    barkPatch_pattern = CreateBarkPatchPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    leafLitter_pattern = CreateLeafLitterPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    snowPatch_pattern = CreateSnowPatchPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    volcanicCrater_pattern = CreateVolcanicCraterPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    fernPatch_pattern = CreateFernPatchPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    enchantedForestGlade_pattern = CreateEnchantedForestGladePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, white_color);
    desertMirageRipple_pattern = CreateDesertMirageRipplePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    craggyMossyCliff_pattern = CreateCraggyMossyCliffPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    twilightMeadowBloom_pattern = CreateTwilightMeadowBloomPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, white_color);
    icyRiverbedFusion_pattern = CreateIcyRiverbedFusionPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, white_color);
    shadyGrove_pattern = CreateShadyGrovePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    mistyHighland_pattern = CreateMistyHighlandPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color);
    pebbledStreambed_pattern = CreatePebbledStreambedPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, white_color);
    autumnalLeafFall_pattern = CreateAutumnalLeafFallPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, white_color, jetBlack_color);
    volcanicLavaFlow_pattern = CreateVolcanicLavaFlowPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, white_color);
    luminescentFernGrove_pattern = CreateLuminescentFernGrovePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, jetBlack_color);
    crystalineFrostMeadow_pattern = CreateCrystalineFrostMeadowPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, red_color, white_color);
    sunlitPrairieWave_pattern = CreateSunlitPrairieWavePattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color, jetBlack_color);
    echoingCanyonEcho_pattern = CreateEchoingCanyonEchoPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color, jetBlack_color);
    verdantSwampMist_pattern = CreateVerdantSwampMistPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color, jetBlack_color);
    twilightWoodlandPatch_pattern = CreateTwilightWoodlandPatchPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color, jetBlack_color);
    lushRainforestUndergrowth_pattern = CreateLushRainforestUndergrowthPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color, jetBlack_color);
    frozenTundraCrack_pattern = CreateFrozenTundraCrackPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color);
    whisperingWindGrass_pattern = CreateWhisperingWindGrassPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color);
    sunbakedDuneCrest_pattern = CreateSunbakedDuneCrestPattern(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color, jetBlack_color);
    legionsNumberMosaic_pattern = CreateLegionsNumberMosaicTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    batrachionSpiral_pattern = CreateBatrachionSpiralTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    factorionGrid_pattern = CreateFactorionGridTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    jugglerSpiral_pattern = CreateJugglerSpiralTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    carotidKundalini_pattern = CreateCarotidKundaliniTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color);
    leviathanNumber_pattern = CreateLeviathanNumberTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    goldenRatioSpiral_pattern = CreateGoldenRatioSpiralTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    cellularAutomaton_pattern = CreateCellularAutomatonTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    sierpinskiTriangle_pattern = CreateSierpinskiTriangleTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    truchetArc_pattern = CreateTruchetArcTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color);
    vampireNumber_pattern = CreateVampireNumberTexture(GRID_SIZE, GRID_SIZE, white_color, gray_color, white_color);


    //    alienflorapattern_pattern_1 = CreateAlienFloraPattern(GRID_SIZE, GRID_SIZE, gray_color, white_color);
}

const int CHUNK_ROWS = 4; // Number of chunks in rows
const int CHUNK_COLS = 4; // Number of chunks in columns
const int CHUNK_WIDTH = WIDTH / CHUNK_COLS;  // Width of each chunk
const int CHUNK_HEIGHT = HEIGHT / CHUNK_ROWS; // Height of each chunk

// Define the block rendering map globally or statically
std::unordered_map<int, std::function<void(Graphics&, const Block&)>> blockRenderers;

///create block type for every pattern example with color configuration that can show adequet color contrast

void InitializeBlockRenderers() {
    blockRenderers[1] = [](Graphics& g, const Block& b) { g.DrawImage(canopyLeaves_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[2] = [](Graphics& g, const Block& b) { g.DrawImage(desertSand_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[3] = [](Graphics& g, const Block& b) { g.DrawImage(lilyPads_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[4] = [](Graphics& g, const Block& b) { g.DrawImage(brickPath_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[5] = [](Graphics& g, const Block& b) { g.DrawImage(algae_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[6] = [](Graphics& g, const Block& b) { g.DrawImage(flowers_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[7] = [](Graphics& g, const Block& b) { g.DrawImage(holeInGround_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[8] = [](Graphics& g, const Block& b) { g.DrawImage(trash_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[9] = [](Graphics& g, const Block& b) { g.DrawImage(bush_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[10] = [](Graphics& g, const Block& b) { g.DrawImage(gasolinePuddle_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[11] = [](Graphics& g, const Block& b) { g.DrawImage(tNT_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[12] = [](Graphics& g, const Block& b) { g.DrawImage(rock_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[13] = [](Graphics& g, const Block& b) { g.DrawImage(tallGrass_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[14] = [](Graphics& g, const Block& b) { g.DrawImage(shortGrass_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[15] = [](Graphics& g, const Block& b) { g.DrawImage(burntTree_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[16] = [](Graphics& g, const Block& b) { g.DrawImage(seawater_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[17] = [](Graphics& g, const Block& b) { g.DrawImage(freshwater_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[18] = [](Graphics& g, const Block& b) { g.DrawImage(fire_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[19] = [](Graphics& g, const Block& b) { g.DrawImage(grasslands_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[20] = [](Graphics& g, const Block& b) { g.DrawImage(boulderField_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[21] = [](Graphics& g, const Block& b) { g.DrawImage(sandDunes_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[22] = [](Graphics& g, const Block& b) { g.DrawImage(charredStump_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[23] = [](Graphics& g, const Block& b) { g.DrawImage(autumnTree_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[24] = [](Graphics& g, const Block& b) { g.DrawImage(wavyGrassPatch_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[25] = [](Graphics& g, const Block& b) { g.DrawImage(largeBoulder_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[26] = [](Graphics& g, const Block& b) { g.DrawImage(thornyShrub_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[27] = [](Graphics& g, const Block& b) { g.DrawImage(wildflowerPatch_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[28] = [](Graphics& g, const Block& b) { g.DrawImage(forestCanopy_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[29] = [](Graphics& g, const Block& b) { g.DrawImage(riverbedPebbles_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[30] = [](Graphics& g, const Block& b) { g.DrawImage(grassTuft_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[31] = [](Graphics& g, const Block& b) { g.DrawImage(rockyOutcrop_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[32] = [](Graphics& g, const Block& b) { g.DrawImage(sandyRipple_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[33] = [](Graphics& g, const Block& b) { g.DrawImage(leafVein_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[34] = [](Graphics& g, const Block& b) { g.DrawImage(mossyStone_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[35] = [](Graphics& g, const Block& b) { g.DrawImage(wetMud_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[36] = [](Graphics& g, const Block& b) { g.DrawImage(fernFrond_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[37] = [](Graphics& g, const Block& b) { g.DrawImage(snowDrift_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[38] = [](Graphics& g, const Block& b) { g.DrawImage(volcanicAsh_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[39] = [](Graphics& g, const Block& b) { g.DrawImage(grassChevron_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[40] = [](Graphics& g, const Block& b) { g.DrawImage(rockyDiamond_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[41] = [](Graphics& g, const Block& b) { g.DrawImage(dirtHerringbone_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };

    blockRenderers[42] = [](Graphics& g, const Block& b) { g.DrawImage(diamond_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[43] = [](Graphics& g, const Block& b) { g.DrawImage(chevron_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[44] = [](Graphics& g, const Block& b) { g.DrawImage(offsetherringbone_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[45] = [](Graphics& g, const Block& b) { g.DrawImage(zigzagherringbone_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[46] = [](Graphics& g, const Block& b) { g.DrawImage(smoothzigzag_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[47] = [](Graphics& g, const Block& b) { g.DrawImage(vstripe_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[48] = [](Graphics& g, const Block& b) { g.DrawImage(herringbone_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[49] = [](Graphics& g, const Block& b) { g.DrawImage(denseplaid_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[50] = [](Graphics& g, const Block& b) { g.DrawImage(diagonalherringbone_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[51] = [](Graphics& g, const Block& b) { g.DrawImage(concentricflowers_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[52] = [](Graphics& g, const Block& b) { g.DrawImage(fractalhalftone_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[53] = [](Graphics& g, const Block& b) { g.DrawImage(herringbonechevron_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[54] = [](Graphics& g, const Block& b) { g.DrawImage(diagonalchevron_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[55] = [](Graphics& g, const Block& b) { g.DrawImage(feathertexture_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[56] = [](Graphics& g, const Block& b) { g.DrawImage(circuitledmatrix_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[57] = [](Graphics& g, const Block& b) { g.DrawImage(hexagonalcircuitgrid_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[58] = [](Graphics& g, const Block& b) { g.DrawImage(arcanecircuitglyphs_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[59] = [](Graphics& g, const Block& b) { g.DrawImage(alchemicalpowercore_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[60] = [](Graphics& g, const Block& b) { g.DrawImage(celestialdatarings_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[61] = [](Graphics& g, const Block& b) { g.DrawImage(fishscalepattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[62] = [](Graphics& g, const Block& b) { g.DrawImage(interlockingcirclespattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[63] = [](Graphics& g, const Block& b) { g.DrawImage(pixelatedcamouflage_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[64] = [](Graphics& g, const Block& b) { g.DrawImage(tormentedsoilpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[65] = [](Graphics& g, const Block& b) { g.DrawImage(organicfractalpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[66] = [](Graphics& g, const Block& b) { g.DrawImage(clockworkpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[67] = [](Graphics& g, const Block& b) { g.DrawImage(neoncircuitpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[68] = [](Graphics& g, const Block& b) { g.DrawImage(hyperspacepattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[69] = [](Graphics& g, const Block& b) { g.DrawImage(nightmarepattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[70] = [](Graphics& g, const Block& b) { g.DrawImage(spirallatticepattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[71] = [](Graphics& g, const Block& b) { g.DrawImage(lightfracturepattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[72] = [](Graphics& g, const Block& b) { g.DrawImage(negativegeometrypattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[73] = [](Graphics& g, const Block& b) { g.DrawImage(unrealitytidepattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[74] = [](Graphics& g, const Block& b) { g.DrawImage(recursivewellpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[75] = [](Graphics& g, const Block& b) { g.DrawImage(neongrid_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[76] = [](Graphics& g, const Block& b) { g.DrawImage(crystallinestructure_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[77] = [](Graphics& g, const Block& b) { g.DrawImage(barktexture_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[78] = [](Graphics& g, const Block& b) { g.DrawImage(stonemosaicpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[79] = [](Graphics& g, const Block& b) { g.DrawImage(organicmeshpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[80] = [](Graphics& g, const Block& b) { g.DrawImage(timewarppattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[81] = [](Graphics& g, const Block& b) { g.DrawImage(quantummatrixpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[82] = [](Graphics& g, const Block& b) { g.DrawImage(alienflorapattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[83] = [](Graphics& g, const Block& b) { g.DrawImage(leafdecay_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[84] = [](Graphics& g, const Block& b) { g.DrawImage(naturescamouflage_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[85] = [](Graphics& g, const Block& b) { g.DrawImage(icefracture_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[86] = [](Graphics& g, const Block& b) { g.DrawImage(nanobotswarm_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[87] = [](Graphics& g, const Block& b) { g.DrawImage(solarpanelarray_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[88] = [](Graphics& g, const Block& b) { g.DrawImage(factorysilhouettepattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[89] = [](Graphics& g, const Block& b) { g.DrawImage(nanotechgrid_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[90] = [](Graphics& g, const Block& b) { g.DrawImage(circuitoverloadpattern_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[91] = [](Graphics& g, const Block& b) { g.DrawImage(mysticalfog_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[92] = [](Graphics& g, const Block& b) { g.DrawImage(classicmaze_pattern_1, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    
    blockRenderers[93] = [](Graphics& g, const Block& b) { g.DrawImage(barkPatch_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[94] = [](Graphics& g, const Block& b) { g.DrawImage(leafLitter_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[95] = [](Graphics& g, const Block& b) { g.DrawImage(snowPatch_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[96] = [](Graphics& g, const Block& b) { g.DrawImage(volcanicCrater_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[97] = [](Graphics& g, const Block& b) { g.DrawImage(fernPatch_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[98] = [](Graphics& g, const Block& b) { g.DrawImage(enchantedForestGlade_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[99] = [](Graphics& g, const Block& b) { g.DrawImage(desertMirageRipple_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[100] = [](Graphics& g, const Block& b) { g.DrawImage(craggyMossyCliff_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[101] = [](Graphics& g, const Block& b) { g.DrawImage(twilightMeadowBloom_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[102] = [](Graphics& g, const Block& b) { g.DrawImage(icyRiverbedFusion_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[103] = [](Graphics& g, const Block& b) { g.DrawImage(shadyGrove_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[104] = [](Graphics& g, const Block& b) { g.DrawImage(mistyHighland_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[105] = [](Graphics& g, const Block& b) { g.DrawImage(pebbledStreambed_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[106] = [](Graphics& g, const Block& b) { g.DrawImage(autumnalLeafFall_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[107] = [](Graphics& g, const Block& b) { g.DrawImage(volcanicLavaFlow_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[108] = [](Graphics& g, const Block& b) { g.DrawImage(luminescentFernGrove_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[109] = [](Graphics& g, const Block& b) { g.DrawImage(crystalineFrostMeadow_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[110] = [](Graphics& g, const Block& b) { g.DrawImage(sunlitPrairieWave_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[111] = [](Graphics& g, const Block& b) { g.DrawImage(echoingCanyonEcho_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[112] = [](Graphics& g, const Block& b) { g.DrawImage(verdantSwampMist_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[113] = [](Graphics& g, const Block& b) { g.DrawImage(twilightWoodlandPatch_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[114] = [](Graphics& g, const Block& b) { g.DrawImage(lushRainforestUndergrowth_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[115] = [](Graphics& g, const Block& b) { g.DrawImage(frozenTundraCrack_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[116] = [](Graphics& g, const Block& b) { g.DrawImage(whisperingWindGrass_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[117] = [](Graphics& g, const Block& b) { g.DrawImage(sunbakedDuneCrest_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[118] = [](Graphics& g, const Block& b) { g.DrawImage(legionsNumberMosaic_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[119] = [](Graphics& g, const Block& b) { g.DrawImage(batrachionSpiral_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[120] = [](Graphics& g, const Block& b) { g.DrawImage(factorionGrid_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[121] = [](Graphics& g, const Block& b) { g.DrawImage(jugglerSpiral_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[122] = [](Graphics& g, const Block& b) { g.DrawImage(carotidKundalini_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[123] = [](Graphics& g, const Block& b) { g.DrawImage(leviathanNumber_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[124] = [](Graphics& g, const Block& b) { g.DrawImage(goldenRatioSpiral_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[125] = [](Graphics& g, const Block& b) { g.DrawImage(cellularAutomaton_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[126] = [](Graphics& g, const Block& b) { g.DrawImage(sierpinskiTriangle_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[127] = [](Graphics& g, const Block& b) { g.DrawImage(truchetArc_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[128] = [](Graphics& g, const Block& b) { g.DrawImage(vampireNumber_pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };



}

void DrawScene(HDC hdc) {
    // Initialize hdcMem if it hasn't been done yet
    if (hdcMem == NULL) {
        hdcMem = CreateCompatibleDC(hdc);
        hbmMem = CreateCompatibleBitmap(hdc, WIDTH, HEIGHT);
        hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
    }

    // Fill background
    RECT backgroundRect = { 0, 0, WIDTH, HEIGHT };
    FillRect(hdcMem, &backgroundRect, backgroundBrush);

    // Create a GDI+ Graphics object from the memory device context
    Graphics graphics(hdcMem);

    // Initialize block renderers if not already done
    static bool initialized = false;
    if (!initialized) {
        InitializeBlockRenderers();
        initialized = true;
    }

    // Loop through each chunk and each block in the chunk
    for (int chunkRow = 0; chunkRow < CHUNK_ROWS; ++chunkRow) {
        for (int chunkCol = 0; chunkCol < CHUNK_COLS; ++chunkCol) {
            int chunkXStart = chunkCol * CHUNK_WIDTH;
            int chunkYStart = chunkRow * CHUNK_HEIGHT;
            int chunkXEnd = chunkXStart + CHUNK_WIDTH;
            int chunkYEnd = chunkYStart + CHUNK_HEIGHT;

            for (const Block& block : blocks) {
                if (block.type != -1) { // Ignore empty blocks
                    if (block.x >= chunkXStart && block.x < chunkXEnd &&
                        block.y >= chunkYStart && block.y < chunkYEnd) {

                        RECT blockRect = { block.x, block.y, block.x + GRID_SIZE, block.y + GRID_SIZE };

                        // Check if block type has a renderer
                        auto it = blockRenderers.find(block.type);
                        if (it != blockRenderers.end()) {
                            it->second(graphics, block); // Call the corresponding rendering function
                        }
                    }
                }
            }
        }
    }


    // Copy the memory device context to the actual device context
    BitBlt(hdc, 0, 0, WIDTH, HEIGHT, hdcMem, 0, 0, SRCCOPY);
}

std::wstring GetSaveFilePath() {
    wchar_t path[MAX_PATH]; // Buffer for path
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, path))) { // Get the "Documents" folder
        std::wstring savePath = std::wstring(path) + L"\\colorcreepsave.txt"; // Append file name
        return savePath; // Return the full save path
    }
    return L"colorcreepsave.txt"; // Fallback path if SHGetFolderPath fails
}
void CleanupSprites() {
    delete red_solid_pattern;
    delete a_text_pattern;
    delete b_text_pattern;
    delete c_text_pattern;
    delete d_text_pattern;
    delete e_text_pattern;
    delete f_text_pattern;
    delete g_text_pattern;
    delete h_text_pattern;
    delete i_text_pattern;
    delete j_text_pattern;
    delete k_text_pattern;
    delete l_text_pattern;
    delete m_text_pattern;
    delete n_text_pattern;
    delete o_text_pattern;
    delete p_text_pattern;
    delete q_text_pattern;
    delete r_text_pattern;
    delete s_text_pattern;
    delete t_text_pattern;
    delete u_text_pattern;
    delete v_text_pattern;
    delete w_text_pattern;
    delete x_text_pattern;
    delete y_text_pattern;
    delete z_text_pattern;
    delete zero_text_pattern;
    delete one_text_pattern;
    delete two_text_pattern;
    delete three_text_pattern;
    delete four_text_pattern;
    delete five_text_pattern;
    delete six_text_pattern;
    delete seven_text_pattern;
    delete eight_text_pattern;
    delete nine_text_pattern;
    delete question_text_pattern;
    delete exclaim_text_pattern;
    delete plus_text_pattern;
    delete equal_text_pattern;
    delete diamond_pattern_1;
    delete chevron_pattern_1;
    delete offsetherringbone_pattern_1;
    delete zigzagherringbone_pattern_1;
    delete smoothzigzag_pattern_1;
    delete vstripe_pattern_1;
    delete herringbone_pattern_1;
    delete denseplaid_pattern_1;
    delete diagonalherringbone_pattern_1;
    delete concentricflowers_pattern_1;
    delete fractalhalftone_pattern_1;
    delete herringbonechevron_pattern;
    delete diagonalchevron_pattern;
    delete feathertexture_pattern_1;
    delete circuitledmatrix_pattern_1;
    delete hexagonalcircuitgrid_pattern_1;
    delete arcanecircuitglyphs_pattern_1;
    delete alchemicalpowercore_pattern_1;
    delete celestialdatarings_pattern_1;
    delete fishscalepattern_pattern_1;
    delete interlockingcirclespattern_pattern_1;
    delete pixelatedcamouflage_pattern_1;
    delete tormentedsoilpattern_pattern_1;
    delete organicfractalpattern_pattern_1;
    delete clockworkpattern_pattern_1;
    delete neoncircuitpattern_pattern_1;
    delete hyperspacepattern_pattern_1;
    delete nightmarepattern_pattern_1;
    delete spirallatticepattern_pattern_1;
    delete lightfracturepattern_pattern_1;
    delete negativegeometrypattern_pattern_1;
    delete unrealitytidepattern_pattern_1;
    delete recursivewellpattern_pattern_1;
    delete neongrid_pattern_1;
    delete crystallinestructure_pattern_1;
    delete barktexture_pattern_1;
    delete stonemosaicpattern_pattern_1;
    delete organicmeshpattern_pattern_1;
    delete timewarppattern_pattern_1;
    delete quantummatrixpattern_pattern_1;
    delete alienflorapattern_pattern_1;
    delete leafdecay_pattern_1;
    delete naturescamouflage_pattern_1;
    delete icefracture_pattern_1;
    delete nanobotswarm_pattern_1;
    delete solarpanelarray_pattern_1;
    delete factorysilhouettepattern_pattern_1;
    delete nanotechgrid_pattern_1;
    delete circuitoverloadpattern_pattern_1;
    delete mysticalfog_pattern_1;
    delete classicmaze_pattern_1;

    delete canopyLeaves_pattern;
    delete desertSand_pattern;
    delete lilyPads_pattern;
    delete brickPath_pattern;
    delete algae_pattern;
    delete flowers_pattern;
    delete holeInGround_pattern;
    delete trash_pattern;
    delete bush_pattern;
    delete gasolinePuddle_pattern;
    delete tNT_pattern;
    delete rock_pattern;
    delete tallGrass_pattern;
    delete shortGrass_pattern;
    delete burntTree_pattern;
    delete seawater_pattern;
    delete freshwater_pattern;
    delete fire_pattern;
    delete grasslands_pattern;
    delete boulderField_pattern;
    delete sandDunes_pattern;
    delete charredStump_pattern;
    delete autumnTree_pattern;
    delete wavyGrassPatch_pattern;
    delete largeBoulder_pattern;
    delete thornyShrub_pattern;
    delete wildflowerPatch_pattern;
    delete forestCanopy_pattern;
    delete riverbedPebbles_pattern;
    delete grassTuft_pattern;
    delete rockyOutcrop_pattern;
    delete sandyRipple_pattern;
    delete leafVein_pattern;
    delete mossyStone_pattern;
    delete wetMud_pattern;
    delete fernFrond_pattern;
    delete snowDrift_pattern;
    delete volcanicAsh_pattern;
    delete grassChevron_pattern;
    delete rockyDiamond_pattern;
    delete dirtHerringbone_pattern;
    delete barkPatch_pattern;
    delete snowPatch_pattern;
    delete volcanicCrater_pattern;
    delete fernPatch_pattern;
    delete enchantedForestGlade_pattern;
    delete desertMirageRipple_pattern;
    delete craggyMossyCliff_pattern;
    delete twilightMeadowBloom_pattern;
    delete icyRiverbedFusion_pattern;
    delete shadyGrove_pattern;
    delete mistyHighland_pattern;
    delete pebbledStreambed_pattern;
    delete autumnalLeafFall_pattern;
    delete volcanicLavaFlow_pattern;
    delete luminescentFernGrove_pattern;
    delete crystalineFrostMeadow_pattern;
    delete sunlitPrairieWave_pattern;
    delete echoingCanyonEcho_pattern;
    delete verdantSwampMist_pattern;
    delete twilightWoodlandPatch_pattern;
    delete lushRainforestUndergrowth_pattern;
    delete frozenTundraCrack_pattern;
    delete whisperingWindGrass_pattern;
    delete sunbakedDuneCrest_pattern;
    delete legionsNumberMosaic_pattern;
    delete batrachionSpiral_pattern;
    delete factorionGrid_pattern;
    delete jugglerSpiral_pattern;
    delete carotidKundalini_pattern;
    delete leviathanNumber_pattern;
    delete goldenRatioSpiral_pattern;
    delete cellularAutomaton_pattern;
    delete sierpinskiTriangle_pattern;
    delete truchetArc_pattern;
    delete vampireNumber_pattern;
    delete leafLitter_pattern;



    if (hbmMem) {
        DeleteObject(hbmMem); // Delete the compatible bitmap
    }
    if (hdcMem) {
        SelectObject(hdcMem, hbmOld); // Restore the old bitmap
        DeleteDC(hdcMem); // Delete the memory device context
    }

    // Optionally reset global pointers to avoid dangling references
    hdcMem = nullptr;
    hbmMem = nullptr;
    hbmOld = nullptr;
    // Reset any GDI+ bitmaps if you have them
}

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow) {//
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    LoadSprites();
    WNDCLASS wc = {}; // Window class structure
    wc.lpfnWndProc = WindowProc; // Set the window procedure
    wc.hInstance = hInstance;    // Handle to the instance
    wc.lpszClassName = CLASS_NAME; // Name of the window class
    // Set background color -- possibly redundant with other background color that is drawn in
    RegisterClass(&wc); // Register the window class
    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(
        0,                           // Optional styles
        CLASS_NAME,                   // Window class name
        L"Color Creep",                 // Window title
        WS_OVERLAPPEDWINDOW,          // Window style
        CW_USEDEFAULT, CW_USEDEFAULT, // Position and size
        rect.right - rect.left,       // Adjusted width
        rect.bottom - rect.top,       // Adjusted height
        NULL, NULL, hInstance, NULL
    );
    if (hwnd == NULL) { // If window creation failed
        return 0;
    }

    InitializeBrushes();

    // Initialize memory DC once
    InitializeMemoryDC(GetDC(NULL));
    ShowWindow(hwnd, nCmdShow); // Show the window
    UpdateWindow(hwnd);         // Update the window
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { // Message loop
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    GdiplusShutdown(gdiplusToken);
    return 0; // Exit the application
}