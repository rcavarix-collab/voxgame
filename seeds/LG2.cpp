#include <windows.h>
#include <vector>
#include <fstream>
#include <string>
#include <shlobj.h>
#include <locale>
#include <codecvt>
#include <set>
#include <utility>
#include <gdiplus.h>
#include <cmath>
#include <chrono>
#include <unordered_set>
#include <random>
#include <functional>
#include <memory>
#include <algorithm>

using namespace Gdiplus;

// Constants and enums
const int GRID_SIZE = 10;
const int GRID_COLS_INITIAL = 100;
const int GRID_ROWS_INITIAL = 100;
int GRID_COLS = GRID_COLS_INITIAL;
int GRID_ROWS = GRID_ROWS_INITIAL;
int WIDTH = GRID_COLS * GRID_SIZE;
int HEIGHT = GRID_ROWS * GRID_SIZE;
const int MAX_FIRE_BLOCKS = 4000;
const int MAX_FIRE_SPREAD_PER_CYCLE = 200;
const int MAX_TREE_BLOCKS = 200;
const int MAX_SPREAD_PER_CYCLE = 20;
const int GROWTH_INTERVAL = 100;
const COLORREF BACKGROUND_COLOR = RGB(101, 67, 33);
const COLORREF PLAYER_COLOR = RGB(255, 50, 71);
const int CHUNK_ROWS = 10;
const int CHUNK_COLS = 10;
const int CHUNK_WIDTH = WIDTH / CHUNK_COLS;
const int CHUNK_HEIGHT = HEIGHT / CHUNK_ROWS;
const float FRAME_DURATION = 0.25f;

enum BlockType {
    ROCK = 0, HOLE = 1, TREE = 2, GRASS_LIGHT = 4, GRASS_DARK = 5, PATH = 6,
    GASOLINE = 7, TNT = 8, FIRE = 9, SEAWATER = 10, ALGAE = 11, FLOWER = 12,
    BUSH = 13, FRESH_WATER = 14, DESERT = 15, PAD = 16, TRASH = 17, DEEP_SEA = 18,
    LEAVES = 19, BURNT_TREE = 20, NONE = -1
};

// Block struct
struct Block {
    int x, y, type;
    bool isObstacle;
    int lifeSpan;
    Block(int x = 0, int y = 0, int type = NONE, bool isObstacle = false, int lifeSpan = -1)
        : x(x), y(y), type(type), isObstacle(isObstacle), lifeSpan(lifeSpan) {}
};

// PosHash defined globally
struct PosHash {
    size_t operator()(const std::pair<int, int>& p) const {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

// Quadtree structure with renamed QuadRect
struct QuadRect {
    int x, y, w, h;
    QuadRect(int x = 0, int y = 0, int w = 0, int h = 0) : x(x), y(y), w(w), h(h) {}
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    bool intersects(const QuadRect& other) const {
        return !(other.x >= x + w || other.x + other.w <= x ||
            other.y >= y + h || other.y + other.h <= y);
    }
};

class Quadtree {
    static const int MAX_OBJECTS = 4;
    static const int MAX_LEVELS = 5;
    int level;
    QuadRect bounds;
    std::vector<Block*> objects;
    std::unique_ptr<Quadtree> nodes[4];

public:
    Quadtree(int level, QuadRect bounds) : level(level), bounds(bounds) {}

    void clear() {
        objects.clear();
        for (auto& node : nodes) node.reset();
    }

    void split() {
        int subWidth = bounds.w / 2;
        int subHeight = bounds.h / 2;
        int x = bounds.x, y = bounds.y;
        nodes[0] = std::make_unique<Quadtree>(level + 1, QuadRect(x, y, subWidth, subHeight));
        nodes[1] = std::make_unique<Quadtree>(level + 1, QuadRect(x + subWidth, y, subWidth, subHeight));
        nodes[2] = std::make_unique<Quadtree>(level + 1, QuadRect(x, y + subHeight, subWidth, subHeight));
        nodes[3] = std::make_unique<Quadtree>(level + 1, QuadRect(x + subWidth, y + subHeight, subWidth, subHeight));
    }

    int getIndex(const Block* block) const {
        int midX = bounds.x + bounds.w / 2;
        int midY = bounds.y + bounds.h / 2;
        bool top = block->y < midY;
        bool bottom = block->y >= midY;
        bool left = block->x < midX;
        bool right = block->x >= midX;
        if (top && left) return 0;
        if (top && right) return 1;
        if (bottom && left) return 2;
        if (bottom && right) return 3;
        return -1;
    }

    void insert(Block* block) {
        if (nodes[0]) {
            int index = getIndex(block);
            if (index != -1) {
                nodes[index]->insert(block);
                return;
            }
        }
        objects.push_back(block);
        if (objects.size() > MAX_OBJECTS && level < MAX_LEVELS) {
            if (!nodes[0]) split();
            auto it = objects.begin();
            while (it != objects.end()) {
                int index = getIndex(*it);
                if (index != -1) {
                    nodes[index]->insert(*it);
                    it = objects.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    }

    void retrieve(std::vector<Block*>& result, const QuadRect& area) const {
        if (!bounds.intersects(area)) return;
        result.insert(result.end(), objects.begin(), objects.end());
        if (nodes[0]) {
            for (const auto& node : nodes) node->retrieve(result, area);
        }
    }
};

// Global variables
std::vector<Block> blocks;
Quadtree quadtree(0, QuadRect(0, 0, WIDTH, HEIGHT));
RECT player = { GRID_COLS / 2 * GRID_SIZE, GRID_ROWS / 2 * GRID_SIZE,
                GRID_COLS / 2 * GRID_SIZE + GRID_SIZE, GRID_ROWS / 2 * GRID_SIZE + GRID_SIZE };
int SPEED_X = GRID_SIZE, SPEED_Y = GRID_SIZE, currentBlockType = 0;
HDC hdcMem = nullptr;
HBITMAP hbmMem = nullptr, hbmOld = nullptr;
HBRUSH backgroundBrush, playerBrush;
ULONG_PTR gdiplusToken;
int frameCount = 0, currentFreshWaterFrame = 0, currentSeaWaterFrame = 0,
currentDeepSeaWaterFrame = 0, currentFireFrame = 0;
float freshWaterAnimationTime = 0.0f, seaWaterAnimationTime = 0.0f,
deepSeaWaterAnimationTime = 0.0f, fireAnimationTime = 0.0f;
Bitmap* freshWaterFrames[4], * seaWaterFrames[4], * deepSeaWaterFrames[4], * fireFrames[4],
* desertCheckerboard, * treeRingCheckerboard, * treeRingPattern, * padRingPattern,
* pathBrickPattern, * algaePattern, * flowerPattern, * holePattern, * trashPattern,
* bushPattern, * gasolinePattern, * tntPattern, * rockPattern, * grassType1Pattern,
* grassType2Pattern, * playerNormalFrame, * playerBrightFrame, * burntTreeSprite;

// Random number generator
static std::random_device rd;
static std::mt19937 gen(rd());
std::uniform_int_distribution<> percentDist(0, 99);
std::uniform_real_distribution<float> floatDist(0.0f, 1.0f);

// Directions
const int directions[8][2] = {
    {0, -GRID_SIZE}, {0, GRID_SIZE}, {-GRID_SIZE, 0}, {GRID_SIZE, 0},
    {-GRID_SIZE, -GRID_SIZE}, {GRID_SIZE, GRID_SIZE}, {-GRID_SIZE, GRID_SIZE}, {GRID_SIZE, -GRID_SIZE}
};

// Time management
std::chrono::high_resolution_clock::time_point lastFrameTime;
void InitializeTime() { lastFrameTime = std::chrono::high_resolution_clock::now(); }
float GetDeltaTime() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> elapsed = currentTime - lastFrameTime;
    lastFrameTime = currentTime;
    return elapsed.count();
}

// Forward declarations
void ExplodeTNT(int x, int y, std::vector<Block>& blocks);
void AddTreeRings();
bool IsTreeNearby(int x, int y);
bool IsBlockOfTypeAround(int x, int y, int type);
void CleanupSprites();
Bitmap* CreateAlgaePattern(int width, int height, const Color& baseColor, const Color& lineColor);

// Utility functions
std::wstring GetSaveFilePath() {
    wchar_t path[MAX_PATH];
    return SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, path))
        ? std::wstring(path) + L"\\lawngonesave.txt" : L"lawngonesave.txt";
}

void SaveGame() {
    std::ofstream file(GetSaveFilePath(), std::ios::binary);
    if (!file.is_open()) return;
    file.write(reinterpret_cast<const char*>(&player.left), sizeof(player.left));
    file.write(reinterpret_cast<const char*>(&player.top), sizeof(player.top));
    size_t blockCount = blocks.size();
    file.write(reinterpret_cast<const char*>(&blockCount), sizeof(blockCount));
    for (const auto& block : blocks) {
        file.write(reinterpret_cast<const char*>(&block), sizeof(Block));
    }
}

void LoadGame() {
    std::ifstream file(GetSaveFilePath(), std::ios::binary);
    if (!file.is_open()) return;
    blocks.clear();
    quadtree.clear();
    int playerLeft, playerTop;
    file.read(reinterpret_cast<char*>(&playerLeft), sizeof(playerLeft));
    file.read(reinterpret_cast<char*>(&playerTop), sizeof(playerTop));
    player = { playerLeft, playerTop, playerLeft + GRID_SIZE, playerTop + GRID_SIZE };
    size_t blockCount;
    file.read(reinterpret_cast<char*>(&blockCount), sizeof(blockCount));
    blocks.reserve(blockCount);  // Pre-allocate to avoid reallocation
    std::vector<Block> tempBlocks;
    tempBlocks.reserve(blockCount);
    for (size_t i = 0; i < blockCount; ++i) {
        Block block;
        file.read(reinterpret_cast<char*>(&block), sizeof(Block));
        tempBlocks.push_back(block);
    }
    blocks.insert(blocks.end(), tempBlocks.begin(), tempBlocks.end());
}

void RemoveBlockAtPosition(int x, int y) {
    auto it = std::find_if(blocks.begin(), blocks.end(), [x, y](const Block& b) {
        return b.x == x && b.y == y;
        });
    if (it != blocks.end()) {
        blocks.erase(it);
    }
}

bool IsBlockAtPosition(int x, int y) {
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x, y, GRID_SIZE, GRID_SIZE));
    return std::any_of(nearby.begin(), nearby.end(), [x, y](const Block* b) {
        return b->x == x && b->y == y;
        });
}

bool IsBlockObstacle(int x, int y) {
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x, y, GRID_SIZE, GRID_SIZE));
    auto it = std::find_if(nearby.begin(), nearby.end(), [x, y](const Block* b) {
        return b->x == x && b->y == y;
        });
    return it != nearby.end() && (*it)->isObstacle;
}

bool IsBlockOfType(int x, int y, int type) {
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x, y, GRID_SIZE, GRID_SIZE));
    auto it = std::find_if(nearby.begin(), nearby.end(), [x, y](const Block* b) {
        return b->x == x && b->y == y;
        });
    return it != nearby.end() && (*it)->type == type;
}

bool IsBlockOfFlammableType(int x, int y) {
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x, y, GRID_SIZE, GRID_SIZE));
    auto it = std::find_if(nearby.begin(), nearby.end(), [x, y](const Block* b) {
        return b->x == x && b->y == y;
        });
    if (it == nearby.end()) return false;
    int type = (*it)->type;
    return type == GRASS_LIGHT || type == GRASS_DARK || type == TREE || type == FLOWER ||
        type == BUSH || type == ALGAE || type == LEAVES;
}

void SetObstacleFlag(Block& block) {
    block.isObstacle = block.type == ROCK || block.type == TREE || block.type == TNT ||
        block.type == SEAWATER || block.type == FRESH_WATER || block.type == DEEP_SEA ||
        block.type == HOLE;
}

// Presence checks
bool AreLifespansPresent() {
    return std::any_of(blocks.begin(), blocks.end(), [](const Block& b) {
        return b.lifeSpan > 0 && b.type != FIRE;
        });
}

bool IsFirePresent() { return std::any_of(blocks.begin(), blocks.end(), [](const Block& b) { return b.type == FIRE; }); }
bool IsSeawaterPresent() { return std::any_of(blocks.begin(), blocks.end(), [](const Block& b) { return b.type == SEAWATER || b.type == DEEP_SEA; }); }
bool IsTNTPresent() { return std::any_of(blocks.begin(), blocks.end(), [](const Block& b) { return b.type == TNT; }); }
bool IsRockPresent() { return std::any_of(blocks.begin(), blocks.end(), [](const Block& b) { return b.type == ROCK; }); }
bool IsFreshWaterOrHolePresent() { return std::any_of(blocks.begin(), blocks.end(), [](const Block& b) { return b.type == HOLE || b.type == FRESH_WATER; }); }
bool IsTreePresent() { return std::any_of(blocks.begin(), blocks.end(), [](const Block& b) { return b.type == TREE; }); }
int CountTrees() { return std::count_if(blocks.begin(), blocks.end(), [](const Block& b) { return b.type == TREE; }); }

// Core game logic
void HandleLifespans() {
    if (!AreLifespansPresent()) return;
    std::unordered_set<std::pair<int, int>, PosHash> leavesToRemove;
    for (auto it = blocks.begin(); it != blocks.end(); ) {
        Block& block = *it;
        if (block.lifeSpan > 0) {
            block.lifeSpan--;
            if (block.lifeSpan == 0 && block.type == TREE) {
                block.type = NONE;
                block.isObstacle = false;
                for (const auto& dir : directions) {
                    int newX = block.x + dir[0], newY = block.y + dir[1];
                    if (IsBlockOfType(newX, newY, LEAVES)) leavesToRemove.emplace(newX, newY);
                }
            }
        }
        if (block.type == NONE || block.lifeSpan == 0) {
            it = blocks.erase(it);
        }
        else {
            ++it;
        }
    }
    for (const auto& pos : leavesToRemove) RemoveBlockAtPosition(pos.first, pos.second);
}

void SpreadGrass(HWND hwnd) {
    std::vector<Block> newBlocks;
    newBlocks.reserve(MAX_SPREAD_PER_CYCLE);
    int spreadCount = 0;
    for (const Block& block : blocks) {
        if (spreadCount >= MAX_SPREAD_PER_CYCLE) break;
        if (block.type != GRASS_LIGHT && block.type != GRASS_DARK && block.type != ALGAE) continue;
        for (const auto& dir : directions) {
            if (spreadCount >= MAX_SPREAD_PER_CYCLE) break;
            int newX = block.x + dir[0], newY = block.y + dir[1];
            if (newX >= 0 && newX < WIDTH && newY >= 0 && newY < HEIGHT &&
                !IsBlockAtPosition(newX, newY) && !IsBlockObstacle(newX, newY) && floatDist(gen) < 0.2f) {
                newBlocks.push_back({ newX, newY, block.type, false });
                spreadCount++;
            }
        }
    }
    blocks.insert(blocks.end(), newBlocks.begin(), newBlocks.end());
}

bool IsDensityTooHigh(int x, int y, int type, int maxDensity) {
    const int radius = 5 * GRID_SIZE;
    int count = 0;
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x - radius, y - radius, radius * 2, radius * 2));
    for (const Block* block : nearby) {
        if (block->type == type &&
            std::abs(block->x - x) <= radius && std::abs(block->y - y) <= radius) {
            if (++count >= maxDensity) return true;
        }
    }
    return false;
}

void NaturalGrowth(HWND hwnd) {
    std::vector<Block> newBlocks;
    newBlocks.reserve(blocks.size() / 10);
    for (const Block& block : blocks) {
        if (block.type == GRASS_LIGHT || block.type == GRASS_DARK) {
            if (percentDist(gen) < 3 && !IsDensityTooHigh(block.x, block.y, FLOWER, 5)) {
                newBlocks.push_back({ block.x, block.y, FLOWER, false, 15 });
            }
            int bushX = block.x + GRID_SIZE, bushY = block.y + GRID_SIZE;
            if (percentDist(gen) < 2 && bushX < WIDTH && bushY < HEIGHT &&
                !IsDensityTooHigh(bushX, bushY, BUSH, 3) &&
                (IsBlockOfType(bushX, bushY, GRASS_LIGHT) || IsBlockOfType(bushX, bushY, GRASS_DARK))) {
                newBlocks.push_back({ bushX, bushY, BUSH, false, 20 });
            }
        }
        else if (block.type == SEAWATER || block.type == FRESH_WATER) {
            if (percentDist(gen) < 3 && !IsDensityTooHigh(block.x, block.y, PAD, 3)) {
                newBlocks.push_back({ block.x, block.y, PAD, false, 30 });
            }
            int trashX = block.x + GRID_SIZE, trashY = block.y + GRID_SIZE;
            if (percentDist(gen) < 2 && trashX < WIDTH && trashY < HEIGHT &&
                !IsDensityTooHigh(trashX, trashY, TRASH, 2) &&
                (IsBlockOfType(trashX, trashY, SEAWATER) || IsBlockOfType(trashX, trashY, FRESH_WATER))) {
                newBlocks.push_back({ trashX, trashY, TRASH, false, 10 });
            }
        }
    }
    blocks.insert(blocks.end(), newBlocks.begin(), newBlocks.end());
}

void SpreadFire(HWND hwnd) {
    if (!IsFirePresent()) return;
    std::vector<Block> newFire, tntToExplode;
    newFire.reserve(MAX_FIRE_SPREAD_PER_CYCLE);
    tntToExplode.reserve(10);
    int fireCount = 0, spreadCount = 0;
    for (auto it = blocks.begin(); it != blocks.end(); ) {
        Block& block = *it;
        if (fireCount >= MAX_FIRE_BLOCKS || spreadCount >= MAX_FIRE_SPREAD_PER_CYCLE) break;
        if (block.type != FIRE) { ++it; continue; }
        fireCount++;
        if (block.lifeSpan <= 0) {
            it = blocks.erase(it);
            continue;
        }
        block.lifeSpan--;
        for (const auto& dir : directions) {
            if (spreadCount >= MAX_FIRE_SPREAD_PER_CYCLE || fireCount >= MAX_FIRE_BLOCKS) break;
            int newX = block.x + dir[0], newY = block.y + dir[1];
            if (newX >= 0 && newX < WIDTH && newY >= 0 && newY < HEIGHT && IsBlockAtPosition(newX, newY)) {
                std::vector<Block*> nearby;
                quadtree.retrieve(nearby, QuadRect(newX, newY, GRID_SIZE, GRID_SIZE));
                for (Block* adj : nearby) {
                    if (adj->x == newX && adj->y == newY) {
                        if (adj->type == GASOLINE && percentDist(gen) < 100) {
                            adj->type = FIRE;
                            adj->lifeSpan = 4;
                            newFire.push_back(*adj);
                            fireCount++; spreadCount++;
                        }
                        else if (adj->type == TREE && percentDist(gen) < 10) {
                            adj->type = FIRE;
                            adj->lifeSpan = 8;
                            newFire.push_back(*adj);
                            fireCount++; spreadCount++;
                            if (percentDist(gen) < 30) {
                                adj->type = BURNT_TREE;
                                adj->isObstacle = true;
                                adj->lifeSpan = 5000;
                            }
                        }
                        else if (IsBlockOfFlammableType(newX, newY) && percentDist(gen) < 10) {
                            adj->type = FIRE;
                            adj->lifeSpan = 8;
                            newFire.push_back(*adj);
                            fireCount++; spreadCount++;
                        }
                        else if (adj->type == TNT) {
                            tntToExplode.push_back(*adj);
                        }
                    }
                }
            }
        }
        ++it;
    }
    for (const auto& tnt : tntToExplode) ExplodeTNT(tnt.x, tnt.y, blocks);
    blocks.insert(blocks.end(), newFire.begin(), newFire.end());
}

void SpreadSeawater() {
    if (!IsSeawaterPresent()) return;
    std::vector<Block> newAlgae;
    newAlgae.reserve(blocks.size() / 10);
    for (auto& block : blocks) {
        if (block.type != SEAWATER && block.type != DEEP_SEA) continue;
        for (const auto& dir : directions) {
            int newX = block.x + dir[0], newY = block.y + dir[1];
            if (newX >= 0 && newX < WIDTH && newY >= 0 && newY < HEIGHT) {
                if (IsBlockAtPosition(newX, newY)) {
                    std::vector<Block*> nearby;
                    quadtree.retrieve(nearby, QuadRect(newX, newY, GRID_SIZE, GRID_SIZE));
                    for (Block* adj : nearby) {
                        if (adj->x == newX && adj->y == newY) {
                            if (adj->type == GRASS_LIGHT || adj->type == GRASS_DARK || adj->type == NONE) {
                                RemoveBlockAtPosition(newX, newY);
                            }
                            else if (adj->type == HOLE) {
                                adj->type = SEAWATER;
                                adj->isObstacle = true;
                                SetObstacleFlag(*adj);
                            }
                        }
                    }
                }
                else {
                    newAlgae.push_back({ newX, newY, ALGAE, false });
                }
            }
        }
    }
    blocks.insert(blocks.end(), newAlgae.begin(), newAlgae.end());
}

void ExplodeTNT(int x, int y, std::vector<Block>& blocks) {
    if (!IsTNTPresent()) return;
    std::unordered_set<std::pair<int, int>, PosHash> tntToExplode;
    RemoveBlockAtPosition(x, y);
    const int radius = 6;
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x - radius * GRID_SIZE, y - radius * GRID_SIZE, radius * 2 * GRID_SIZE, radius * 2 * GRID_SIZE));
    for (Block* block : nearby) {
        int dx = (block->x - x) / GRID_SIZE, dy = (block->y - y) / GRID_SIZE;
        if (std::abs(dx) <= radius && std::abs(dy) <= radius) {
            if (block->type == TNT) tntToExplode.emplace(block->x, block->y);
            else RemoveBlockAtPosition(block->x, block->y);
        }
    }
    for (const auto& pos : tntToExplode) {
        int tx = pos.first, ty = pos.second;
        RemoveBlockAtPosition(tx, ty);
        std::vector<Block*> explosionArea;
        quadtree.retrieve(explosionArea, QuadRect(tx - radius * GRID_SIZE, ty - radius * GRID_SIZE, radius * 2 * GRID_SIZE, radius * 2 * GRID_SIZE));
        for (Block* block : explosionArea) {
            int dx = (block->x - tx) / GRID_SIZE, dy = (block->y - ty) / GRID_SIZE;
            if (std::abs(dx) <= radius && std::abs(dy) <= radius) RemoveBlockAtPosition(block->x, block->y);
        }
    }
}

void SpreadTrees(HWND hwnd) {
    if (!IsTreePresent()) return;
    int treeCount = CountTrees();
    if (treeCount >= MAX_TREE_BLOCKS) return;
    std::vector<Block> newTrees;
    newTrees.reserve(MAX_TREE_BLOCKS - treeCount);
    for (Block& block : blocks) {
        if (block.type != TREE || treeCount >= MAX_TREE_BLOCKS) continue;
        for (int distance = 3; distance <= 5 && treeCount < MAX_TREE_BLOCKS; ++distance) {
            for (const auto& dir : directions) {
                int newX = block.x + dir[0] * distance, newY = block.y + dir[1] * distance;
                if (newX >= 0 && newX < WIDTH && newY >= 0 && newY < HEIGHT &&
                    !IsBlockAtPosition(newX, newY) && !IsBlockObstacle(newX, newY) &&
                    !IsDensityTooHigh(newX, newY, TREE, 3) && percentDist(gen) < 3) {
                    newTrees.push_back({ newX, newY, TREE, true, 2000 });
                    treeCount++;
                }
            }
        }
    }
    blocks.insert(blocks.end(), newTrees.begin(), newTrees.end());
    AddTreeRings();
}

void AddTreeRings() {
    if (!IsTreePresent()) return;
    std::vector<Block> newRings;
    newRings.reserve(blocks.size() / 10);
    for (auto it = blocks.begin(); it != blocks.end(); ) {
        if (it->type == LEAVES && !IsTreeNearby(it->x, it->y)) {
            it = blocks.erase(it);
        }
        else if (it->type == TREE) {
            for (const auto& dir : directions) {
                int newX = it->x + dir[0], newY = it->y + dir[1];
                if (!IsBlockAtPosition(newX, newY) && !IsBlockOfType(newX, newY, LEAVES)) {
                    newRings.push_back({ newX, newY, LEAVES, false });
                }
            }
            ++it;
        }
        else {
            ++it;
        }
    }
    blocks.insert(blocks.end(), newRings.begin(), newRings.end());
}

void AccelerateFireSpread() {
    if (!IsFirePresent()) return;
    std::vector<Block> newFireBlocks;
    newFireBlocks.reserve(128);
    std::unordered_set<std::pair<int, int>, PosHash> newFirePositions;
    for (const Block& block : blocks) {
        if (block.type != FIRE) continue;
        for (const auto& dir : directions) {
            int newX = block.x + dir[0], newY = block.y + dir[1];
            if (newX >= 0 && newX < WIDTH && newY >= 0 && newY < HEIGHT &&
                IsBlockOfType(newX, newY, GASOLINE)) {
                for (int distance = 1; distance <= 2; ++distance) {
                    for (const auto& spreadDir : directions) {
                        int spreadX = newX + spreadDir[0] * distance, spreadY = newY + spreadDir[1] * distance;
                        if (spreadX >= 0 && spreadX < WIDTH && spreadY >= 0 && spreadY < HEIGHT &&
                            IsBlockAtPosition(spreadX, spreadY) && !IsBlockOfType(spreadX, spreadY, FIRE) &&
                            IsBlockOfFlammableType(spreadX, spreadY) && newFirePositions.insert({ spreadX, spreadY }).second) {
                            newFireBlocks.push_back({ spreadX, spreadY, FIRE, true, 8 });
                        }
                    }
                }
            }
        }
    }
    blocks.insert(blocks.end(), newFireBlocks.begin(), newFireBlocks.end());
}

void SpreadDesert() {
    if (!IsRockPresent()) return;
    std::vector<Block> newDesert;
    newDesert.reserve(blocks.size() / 10);
    for (const Block& block : blocks) {
        if (block.type != ROCK) continue;
        for (const auto& dir : directions) {
            int newX = block.x + dir[0], newY = block.y + dir[1];
            if (newX >= 0 && newX < WIDTH && newY >= 0 && newY < HEIGHT &&
                !IsBlockAtPosition(newX, newY) && !IsBlockOfTypeAround(newX, newY, FRESH_WATER)) {
                newDesert.push_back({ newX, newY, DESERT, false });
            }
        }
    }
    blocks.insert(blocks.end(), newDesert.begin(), newDesert.end());
}

void SpreadFreshWater() {
    if (!IsFreshWaterOrHolePresent()) return;
    std::vector<Block> newWater;
    newWater.reserve(blocks.size() / 10);
    auto isSurroundedByHoles = [&](int x, int y) {
        for (const auto& dir : directions) {
            int nx = x + dir[0], ny = y + dir[1];
            if (!IsBlockAtPosition(nx, ny) || !IsBlockOfType(nx, ny, HOLE)) return false;
        }
        return true;
    };
    for (auto& block : blocks) {
        if (block.type == HOLE && isSurroundedByHoles(block.x, block.y)) {
            block.type = FRESH_WATER;
            block.isObstacle = true;
            SetObstacleFlag(block);
        }
    }
    for (auto& block : blocks) {
        if (block.type != FRESH_WATER) continue;
        for (const auto& dir : directions) {
            int newX = block.x + dir[0], newY = block.y + dir[1];
            if (!IsBlockOfTypeAround(newX, newY, DESERT) && IsBlockAtPosition(newX, newY) &&
                IsBlockOfType(newX, newY, HOLE)) {
                std::vector<Block*> nearby;
                quadtree.retrieve(nearby, QuadRect(newX, newY, GRID_SIZE, GRID_SIZE));
                for (Block* adj : nearby) {
                    if (adj->x == newX && adj->y == newY) {
                        adj->type = FRESH_WATER;
                        SetObstacleFlag(*adj);
                        newWater.push_back(*adj);
                    }
                }
            }
        }
    }
    blocks.insert(blocks.end(), newWater.begin(), newWater.end());
}

void UpdateSeawaterToDeepSea() {
    for (auto& block : blocks) {
        if (block.type == SEAWATER && (block.x == 0 || block.x >= WIDTH - GRID_SIZE ||
            block.y == 0 || block.y >= HEIGHT - GRID_SIZE)) {
            block.type = DEEP_SEA;
        }
    }
}

bool IsTreeNearby(int x, int y) {
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x - GRID_SIZE, y - GRID_SIZE, GRID_SIZE * 3, GRID_SIZE * 3));
    return std::any_of(nearby.begin(), nearby.end(), [x, y](const Block* b) {
        return std::abs(b->x - x) <= GRID_SIZE && std::abs(b->y - y) <= GRID_SIZE && b->type == TREE;
        });
}

bool IsBlockOfTypeAround(int x, int y, int type) {
    std::vector<Block*> nearby;
    quadtree.retrieve(nearby, QuadRect(x - GRID_SIZE, y - GRID_SIZE, GRID_SIZE * 3, GRID_SIZE * 3));
    return std::any_of(nearby.begin(), nearby.end(), [x, y, type](const Block* b) {
        return std::abs(b->x - x) <= GRID_SIZE && std::abs(b->y - y) <= GRID_SIZE && b->type == type;
        });
}

// Graphics and rendering
void InitializeBrushes() {
    backgroundBrush = CreateSolidBrush(BACKGROUND_COLOR);
    playerBrush = CreateSolidBrush(PLAYER_COLOR);
}

void InitializeMemoryDC(HDC hdc) {
    if (!hdcMem) {
        hdcMem = CreateCompatibleDC(hdc);
        hbmMem = CreateCompatibleBitmap(hdc, WIDTH, HEIGHT);
        hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
    }
}

void CleanUp() {
    DeleteObject(backgroundBrush);
    DeleteObject(playerBrush);
    if (hdcMem) {
        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        hdcMem = nullptr;
    }
    CleanupSprites();
}

void LoadSprites() {
    grassType1Pattern = CreateAlgaePattern(GRID_SIZE, GRID_SIZE, Color(76, 175, 80), Color(144, 238, 144));
    grassType2Pattern = CreateAlgaePattern(GRID_SIZE, GRID_SIZE, Color(107, 142, 35), Color(0, 100, 0));
    // ... (rest of sprite loading as per original)
}

void UpdateAnimations(float deltaTime) {
    freshWaterAnimationTime += deltaTime;
    if (freshWaterAnimationTime >= FRAME_DURATION) {
        currentFreshWaterFrame = (currentFreshWaterFrame + 1) % 4;
        freshWaterAnimationTime = 0.0f;
    }
    seaWaterAnimationTime += deltaTime;
    if (seaWaterAnimationTime >= FRAME_DURATION) {
        currentSeaWaterFrame = (currentSeaWaterFrame + 1) % 4;
        seaWaterAnimationTime = 0.0f;
    }
    deepSeaWaterAnimationTime += deltaTime;
    if (deepSeaWaterAnimationTime >= FRAME_DURATION) {
        currentDeepSeaWaterFrame = (currentDeepSeaWaterFrame + 1) % 4;
        deepSeaWaterAnimationTime = 0.0f;
    }
    fireAnimationTime += deltaTime;
    if (fireAnimationTime >= FRAME_DURATION) {
        currentFireFrame = (currentFireFrame + 1) % 4;
        fireAnimationTime = 0.0f;
    }
    frameCount = (frameCount + 1) % 10;
}

std::unordered_map<int, std::function<void(Graphics&, const Block&)>> blockRenderers;
void InitializeBlockRenderers() {
    blockRenderers[ROCK] = [](Graphics& g, const Block& b) { g.DrawImage(rockPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[HOLE] = [](Graphics& g, const Block& b) { g.DrawImage(holePattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[TREE] = [](Graphics& g, const Block& b) { g.DrawImage(treeRingPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[GRASS_LIGHT] = [](Graphics& g, const Block& b) { g.DrawImage(grassType1Pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[GRASS_DARK] = [](Graphics& g, const Block& b) { g.DrawImage(grassType2Pattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[PATH] = [](Graphics& g, const Block& b) { g.DrawImage(pathBrickPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[GASOLINE] = [](Graphics& g, const Block& b) { g.DrawImage(gasolinePattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[TNT] = [](Graphics& g, const Block& b) { g.DrawImage(tntPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[FIRE] = [](Graphics& g, const Block& b) { g.DrawImage(fireFrames[currentFireFrame], b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[SEAWATER] = [](Graphics& g, const Block& b) { g.DrawImage(seaWaterFrames[currentSeaWaterFrame], b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[ALGAE] = [](Graphics& g, const Block& b) { g.DrawImage(algaePattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[FLOWER] = [](Graphics& g, const Block& b) { g.DrawImage(flowerPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[BUSH] = [](Graphics& g, const Block& b) { g.DrawImage(bushPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[FRESH_WATER] = [](Graphics& g, const Block& b) { g.DrawImage(freshWaterFrames[currentFreshWaterFrame], b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[DESERT] = [](Graphics& g, const Block& b) { g.DrawImage(desertCheckerboard, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[PAD] = [](Graphics& g, const Block& b) { g.DrawImage(padRingPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[TRASH] = [](Graphics& g, const Block& b) { g.DrawImage(trashPattern, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[DEEP_SEA] = [](Graphics& g, const Block& b) { g.DrawImage(deepSeaWaterFrames[currentDeepSeaWaterFrame], b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[LEAVES] = [](Graphics& g, const Block& b) { g.DrawImage(treeRingCheckerboard, b.x, b.y, GRID_SIZE, GRID_SIZE); };
    blockRenderers[BURNT_TREE] = [](Graphics& g, const Block& b) { g.DrawImage(burntTreeSprite, b.x, b.y, GRID_SIZE, GRID_SIZE); };
}

void DrawScene(HDC hdc) {
    InitializeMemoryDC(hdc);

    RECT rect = { 0, 0, WIDTH, HEIGHT };
    FillRect(hdcMem, &rect, backgroundBrush);

    Graphics graphics(hdcMem);
    static bool initialized = false;
    if (!initialized) {
        InitializeBlockRenderers();
        initialized = true;
    }

    // Rebuild quadtree each frame
    quadtree.clear();
    for (auto& block : blocks) quadtree.insert(&block);

    // Draw blocks per chunk using quadtree
    for (int chunkRow = 0; chunkRow < CHUNK_ROWS; ++chunkRow) {
        for (int chunkCol = 0; chunkCol < CHUNK_COLS; ++chunkCol) {
            int chunkXStart = chunkCol * CHUNK_WIDTH;
            int chunkYStart = chunkRow * CHUNK_HEIGHT;
            QuadRect chunkArea(chunkXStart, chunkYStart, CHUNK_WIDTH, CHUNK_HEIGHT);
            std::vector<Block*> chunkBlocks;
            quadtree.retrieve(chunkBlocks, chunkArea);

            for (const Block* block : chunkBlocks) {
                if (block->type != NONE && blockRenderers.count(block->type)) {
                    blockRenderers[block->type](graphics, *block);
                }
            }
        }
    }

    // Draw player
    graphics.DrawImage((frameCount / 5) % 2 ? playerBrightFrame : playerNormalFrame,
        player.left, player.top, GRID_SIZE, GRID_SIZE);
    BitBlt(hdc, 0, 0, WIDTH, HEIGHT, hdcMem, 0, 0, SRCCOPY);
}

void ShiftGrid(int shiftX, int shiftY) {
    // First, shift all existing blocks
    for (auto& block : blocks) {
        block.x += shiftX;
        block.y += shiftY;
    }

    // Check for new areas revealed after the shift and add blank blocks only where needed

    // Case 1: Moving left, add new blocks on the right
    if (shiftX > 0) {
        for (int y = 0; y < HEIGHT; y += GRID_SIZE) {
            if (!IsBlockAtPosition(WIDTH - GRID_SIZE, y)) {
                Block newBlock(WIDTH - GRID_SIZE, y, -1, false);  // New blank block
                blocks.push_back(newBlock);
            }
        }
    }
    // Case 2: Moving right, add new blocks on the left
    else if (shiftX < 0) {
        for (int y = 0; y < HEIGHT; y += GRID_SIZE) {
            if (!IsBlockAtPosition(0, y)) {
                Block newBlock(0, y, -1, false);  // New blank block
                blocks.push_back(newBlock);
            }
        }
    }

    // Case 3: Moving up, add new blocks at the bottom
    if (shiftY > 0) {
        for (int x = 0; x < WIDTH; x += GRID_SIZE) {
            if (!IsBlockAtPosition(x, HEIGHT - GRID_SIZE)) {
                Block newBlock(x, HEIGHT - GRID_SIZE, -1, false);  // New blank block
                blocks.push_back(newBlock);
            }
        }
    }
    // Case 4: Moving down, add new blocks at the top
    else if (shiftY < 0) {
        for (int x = 0; x < WIDTH; x += GRID_SIZE) {
            if (!IsBlockAtPosition(x, 0)) {
                Block newBlock(x, 0, -1, false);  // New blank block
                blocks.push_back(newBlock);
            }
        }
    }

    // Finally, redraw the screen to reflect the updated grid
    InvalidateRect(GetConsoleWindow(), NULL, TRUE);  // Force screen redraw
}

void ClearPlayerTrail(HDC hdc, int prevLeft, int prevTop, int prevRight, int prevBottom) {
    RECT rect = { prevLeft, prevTop, prevRight, prevBottom };
    FillRect(hdc, &rect, backgroundBrush);
}


void SetGrowthTimer(HWND hwnd) { SetTimer(hwnd, 1, GROWTH_INTERVAL, NULL); }

Bitmap* CreateAlgaePattern(int width, int height, const Color& baseColor, const Color& lineColor) {
    Bitmap* bitmap = new Bitmap(width, height, PixelFormat32bppARGB);
    Graphics graphics(bitmap);
    SolidBrush baseBrush(baseColor);
    graphics.FillRectangle(&baseBrush, 0, 0, width, height);
    Pen linePen(lineColor, 2);
    for (int i = -height; i < width; i += 5) {
        graphics.DrawLine(&linePen, i, 0, i + height, height);
    }
    return bitmap;
}

void CleanupSprites() {
    for (int i = 0; i < 4; ++i) {
        delete freshWaterFrames[i];
        delete seaWaterFrames[i];
        delete deepSeaWaterFrames[i];
        delete fireFrames[i];
    }
    delete desertCheckerboard; delete treeRingCheckerboard; delete treeRingPattern;
    delete padRingPattern; delete pathBrickPattern; delete algaePattern;
    delete flowerPattern; delete holePattern; delete trashPattern; delete bushPattern;
    delete gasolinePattern; delete tntPattern; delete rockPattern;
    delete grassType1Pattern; delete grassType2Pattern; delete playerNormalFrame;
    delete playerBrightFrame; delete burntTreeSprite;
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        InitializeMemoryDC(GetDC(hwnd));
        return 0;
    case WM_DESTROY:
        SaveGame();
        KillTimer(hwnd, 1);
        CleanUp();
        GdiplusShutdown(gdiplusToken);
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        DrawScene(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_RBUTTONDOWN:
        RemoveBlockAtPosition(LOWORD(lParam) / GRID_SIZE * GRID_SIZE, HIWORD(lParam) / GRID_SIZE * GRID_SIZE);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_KEYDOWN: {
        int prevLeft = player.left, prevTop = player.top, prevRight = player.right, prevBottom = player.bottom;
        switch (wParam) {
        case VK_LEFT: case 'A':
            if (player.left - SPEED_X >= 0 && !IsBlockObstacle(player.left - SPEED_X, player.top)) {
                player.left -= SPEED_X; player.right -= SPEED_X;
                ClearPlayerTrail(hdcMem, prevLeft, prevTop, prevRight, prevBottom);
            }
            else ShiftGrid(GRID_SIZE, 0);
            break;
        case VK_RIGHT: case 'D':
            if (player.left + SPEED_X < WIDTH && !IsBlockObstacle(player.left + SPEED_X, player.top)) {
                player.left += SPEED_X; player.right += SPEED_X;
                ClearPlayerTrail(hdcMem, prevLeft, prevTop, prevRight, prevBottom);
            }
            else ShiftGrid(-GRID_SIZE, 0);
            break;
        case VK_UP: case 'W':
            if (player.top - SPEED_Y >= 0 && !IsBlockObstacle(player.left, player.top - SPEED_Y)) {
                player.top -= SPEED_Y; player.bottom -= SPEED_Y;
                ClearPlayerTrail(hdcMem, prevLeft, prevTop, prevRight, prevBottom);
            }
            else ShiftGrid(0, GRID_SIZE);
            break;
        case VK_DOWN: case 'S':
            if (player.top + SPEED_Y < HEIGHT && !IsBlockObstacle(player.left, player.top + SPEED_Y)) {
                player.top += SPEED_Y; player.bottom += SPEED_Y;
                ClearPlayerTrail(hdcMem, prevLeft, prevTop, prevRight, prevBottom);
            }
            else ShiftGrid(0, -GRID_SIZE);
            break;
        case '0': currentBlockType = SEAWATER; break;
        case '1': currentBlockType = ROCK; break;
        case '2': currentBlockType = HOLE; break;
        case '3': currentBlockType = TREE; break;
        case '4': currentBlockType = GRASS_LIGHT; break;
        case '5': currentBlockType = GRASS_DARK; break;
        case '6': currentBlockType = PATH; break;
        case '7': currentBlockType = GASOLINE; break;
        case '8': currentBlockType = TNT; break;
        case '9': currentBlockType = FIRE; break;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam) / GRID_SIZE * GRID_SIZE, y = HIWORD(lParam) / GRID_SIZE * GRID_SIZE;
        RemoveBlockAtPosition(x, y);
        std::vector<Block> newBlocks;
        newBlocks.push_back(Block(x, y, currentBlockType));
        if (currentBlockType == FIRE) newBlocks.back().lifeSpan = 4;
        SetObstacleFlag(newBlocks.back());
        blocks.insert(blocks.end(), newBlocks.begin(), newBlocks.end());
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_TIMER:
        UpdateSeawaterToDeepSea();
        SpreadGrass(hwnd);
        SpreadFire(hwnd);
        AccelerateFireSpread();
        SpreadSeawater();
        SpreadTrees(hwnd);
        NaturalGrowth(hwnd);
        SpreadFreshWater();
        SpreadDesert();
        HandleLifespans();
        UpdateAnimations(GetDeltaTime());
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    InitializeTime();
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    LoadSprites();
    WNDCLASS wc = { 0, WindowProc, 0, 0, hInstance, 0, 0, 0, 0, L"LawnGone" };
    RegisterClass(&wc);
    RECT rect = { 0, 0, WIDTH, HEIGHT };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowEx(0, L"LawnGone", L"Lawn Gone", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    InitializeBrushes();
    LoadGame();
    SetGrowthTimer(hwnd);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}