// props.h
//
// Trees, bushes and rocks (DESIGN.md §2, §3), built from primitive shapes
// (prims.h) and placed by the ground they stand on: forests on moss,
// scattered trees in meadows, bushes in dry grass, rocks on gravel.
//
// Props live in 32 m tiles, generated from the seed around the mech and
// forgotten again when far away, unless something happened to them (a
// felled tree stays felled). Each tile keeps a baked mesh, rebuilt only
// when one of its props changes (or, while a tree is falling, each frame
// for that one tile). They can be shot, blasted, knocked over by the mech
// walking into them, and burned.
//
// Cost: residency and meshing scale with tiles changed; per tick, only
// falling and burning props do work. Pure C++, tested natively.

#pragma once

#include "common.h"
#include "events.h"
#include "prims.h"
#include <unordered_map>
#include <vector>

class Terrain;

enum PropKind : uint8_t { PROP_PINE, PROP_BROADLEAF, PROP_BUSH, PROP_ROCK, PROP_KIND_COUNT };
enum PropState : uint8_t { PS_STANDING, PS_FALLING, PS_FALLEN, PS_STUMP, PS_GONE };

struct Prop {
    Vec3 pos;              // base, on the ground
    float yaw, scale;
    uint8_t kind, state;
    bool burning = false, charred = false;
    float health;
    float fallYaw = 0, fallAngle = 0, fallSpeed = 0; // falling trees: lean direction, angle (0..pi/2), angular speed
    float burnTime = 0;
    uint32_t seed;
    bool IsTree() const { return kind == PROP_PINE || kind == PROP_BROADLEAF; }
    float Height() const;  // standing height
    float Radius() const;  // for hits: crown or body radius
};

struct PropTileKey {
    int x, z;
    bool operator==(const PropTileKey& o) const { return x == o.x && z == o.z; }
};
struct PropTileKeyHash { size_t operator()(const PropTileKey& k) const { return (size_t)Hash3(k.x, 0, k.z, 31); } };

struct PropTile {
    std::vector<Prop> props;
    std::vector<MeshVertex> mesh;
    uint32_t version = 0;
    bool meshed = false;
    bool modified = false;  // something happened here: keep it when far away
    bool active = false;    // a prop is falling or burning: tick it
    bool distant = false;   // beyond view: mesh dropped, not rebuilt until it's near again
};

struct PropRef { PropTileKey tile; int index = -1; };

class Props {
public:
    static constexpr float TILE = 32.0f;

    void Reset(uint32_t seed);
    void Update(const Terrain& t, Vec3 focus, float radius);   // residency around the mech
    void Tick(float dt, EventList& ev);                       // falling and burning
    int BuildMeshes(int budget);

    // Nearest prop hit by the segment o + dir * [0, maxDist]; false if none.
    bool Raycast(Vec3 o, Vec3 dir, float maxDist, float& tHit, PropRef& hit) const;
    Prop* Get(const PropRef& r);
    // `from`: where the damage came from (trees fall away from it).
    void Damage(const PropRef& r, float amount, Vec3 from, EventList& ev);
    // A blast: props in the core are shattered (trees to stumps), those
    // further out take falling-off damage and fall away from the centre.
    void Blast(Vec3 centre, float radius, EventList& ev);
    // The mech striding into trees knocks them over; bushes are crushed.
    void Trample(Vec3 feet, float radius, Vec3 velocity, EventList& ev);
    // Set flammable props within `radius` burning. Returns how many caught.
    int Ignite(Vec3 centre, float radius);
    // Burning props, for spreading fire to the ground and drawing flames.
    void BurningPositions(std::vector<Vec3>& out) const;

    const std::unordered_map<PropTileKey, PropTile, PropTileKeyHash>& Tiles() const { return m_tiles; }
    int Count() const;

private:
    void Generate(const Terrain& t, const PropTileKey& k, PropTile& tile) const;
    void MeshTile(PropTile& tile) const;
    void Changed(PropTile& tile) { tile.meshed = false; tile.modified = true; }

    uint32_t m_seed = 1;
    std::unordered_map<PropTileKey, PropTile, PropTileKeyHash> m_tiles;
};
