# Voxel Logistics Game — Complete Design Reference

## Part I — Vision and Constraints

### 1.1 What this is
A first-person 3D voxel game combining grid-locked terrain manipulation (Minecraft-lineage) with automated item logistics (BuildCraft-lineage). The player places blocks by hand early on and, as the game progresses, builds pipe/machine networks that move and transform items without further manual handling. Progression is tiered: later recipes and machines require materials or unlocks gated behind earlier ones.

### 1.2 Non-negotiable constraints, and why each exists
- **Two source files total** (`main.cpp`, `supplement.cpp`) plus save files on disk. Chosen specifically because multi-file Visual Studio project configuration has been a recurring, unresolved blocker — two files handed to one compiler invocation removes the failure surface entirely (no "forgot to add file to project," no linker path misconfiguration).
- **No copied code.** The four legacy files (Prismative.cpp, drillder.cpp, LG2.cpp, cc_2_2_2.cpp) are reference material for concepts only. Every line in the new project is written fresh. This matters for the earlier licensing discussion too: because nothing is copied, there is no attribution obligation of any kind, from any source, ever — clean-room implementation sidesteps the entire question.
- **No wrapping/torus world.** Deliberately rejected after consideration — interesting conceptually, but it complicates pipe network topology (a network shouldn't need special-case logic for crossing a world seam) and complicates save-chunk addressing (modular coordinates instead of plain integers). World is unbounded-feeling in X/Z, bounded in Y.
- **Resource discipline as a first-class requirement**, not a secondary optimization pass. Every system description below states its cost model explicitly so this can be checked at design time rather than discovered at profiling time.

### 1.3 What "optimal" means for this project specifically
Not "fastest possible" or "most feature-rich." It means: **cost scales with what's actually on screen or actually changing, never with total world size or total elapsed content.** Every subsystem below is graded against that one test.

---

## Part II — World Representation

### 2.1 Chunking
- 16×16×16 blocks per chunk (4096 cells), stored as flat `uint8_t[4096]` in the prototype.
- Chunks exist only when they contain at least one non-air block that was ever set — pure-air regions of the world allocate nothing. This is what makes "seemingly infinite" cheap: a `std::unordered_map<ChunkCoord, unique_ptr<Chunk>>` only grows with actual player activity, not with world size.
- **Compression upgrade path, deliberately deferred:** a palette-per-chunk scheme (distinct-block list + bit-packed indices sized to `ceil(log2(paletteSize))`) was designed and costed — a nearly-air chunk costs bytes, a 12-block-type terrain chunk costs ~4 bits/voxel (~16KB), versus 64KB for the flat array. Not implemented in the prototype because content variety (10 block types total) doesn't yet justify the complexity; the flat array is simpler and correct, and this is the documented point where compression pays for itself (once per-chunk distinct block counts start regularly exceeding ~16-20).

### 2.2 Coordinate systems
Three coordinate spaces exist and must never be silently mixed:
- **World space** — plain `int x, y, z`, the address of any block anywhere.
- **Chunk space** — `ChunkCoord{x,y,z}`, one chunk's identity in the sparse map.
- **Local space** — `[0,16)` per axis, a block's position inside its chunk.

`ToChunk()` must floor-divide correctly for negative coordinates (`v >= 0 ? v/16 : (v-15)/16`), not truncate — a naive `/` in C++ rounds toward zero and silently misassigns blocks near the origin on the negative side. This was an actual bug caught in the reviewed prototype (`UpdateLoaded` originally reimplemented this incorrectly instead of reusing `ToChunk`).

### 2.3 Height bound
Y is bounded to a fixed range (0–255 suggested). Rationale: true unbounded vertical buys nothing for this game's content (no infinite mining depth is planned) and costs real complexity in generation and eventual lighting; a generous fixed range is functionally infinite to a player while keeping Y a simple `uint8_t` if ever useful for compact storage.

### 2.4 Chunk loading
- A radius (`LOAD_RADIUS`, chunks) around the player's current chunk defines the loaded set.
- The loaded set is recomputed **only when the player's chunk coordinate actually changes**, not every frame — recomputing several hundred hash-set entries every single tick regardless of movement was an identified inefficiency in the reviewed prototype and is explicitly avoided.
- Chunks leaving the radius are eligible for serialization-and-drop (not implemented in the prototype's fixed small world, but the architecture — sparse map keyed by chunk coordinate — supports it without restructuring).

---

## Part III — Block Model

### 3.1 Identity: name-based, not position-based
`BlockID` (an enum) is a *runtime* convenience only. The actual identity written to disk is the block's **string name** (`g_blockNames[]`). This single decision is what allows the block list to grow indefinitely during development — adding, removing, or reordering enum entries never corrupts an existing save, because loading remaps saved names onto whatever the current build's names are, substituting a safe "unknown" (air) for anything genuinely removed, with a logged warning rather than a silent misread.

### 3.2 Metadata table
One flat array, `BlockInfo g_info[BLOCK_COUNT]`, is the single source of truth per block:
```
foundational : bool   — never falls, always supports (Section V)
solid        : bool   — participates in collision and raycast hits
shape        : int    — 0 cube, 1 straight pipe, 2 corner, 3 junction
tex          : int    — atlas slot (cubes) or unused (pipes share one texture)
```
No virtual dispatch, no per-block class hierarchy — a block's behavior is entirely data-driven from this table plus the systems that read it. This is deliberate: virtual calls inside the meshing and simulation inner loops would violate the "no virtual dispatch in hot paths" resource rule.

### 3.3 Prototype block roster
| Block | Foundational | Shape | Purpose |
|---|---|---|---|
| Air | — | — | Absence of a block |
| Foundation | yes | cube | Never falls; the base layer of any build |
| Stone | no | cube | Terrain, subject to gravity |
| Dirt | no | cube | Terrain, subject to gravity |
| Wood | no | cube | Building material |
| Chest | yes | cube | Storage container (item-handler interface, Part VI) |
| Machine | yes | cube | Placeholder processing block |
| Pipe straight/corner/junction | yes | non-cube | Item transport geometry |

---

## Part IV — Rendering

### 4.1 Visual target
Blocky, saturated, clear silhouettes. Procedurally generated textures (patterns drawn in code at startup), not imported art — consistent with the "no external asset files" constraint and directly reusing the *idea* (not the code) behind cc_2_2_2.cpp's 145 generator functions.

### 4.2 The central performance decision: per-chunk merged meshing
Every exposed face of every cube-shaped block in a chunk is combined into one vertex/index buffer pair, drawn with a single `DrawIndexed` call per chunk. This converts draw cost from **O(blocks)** to **O(loaded chunks)** — the single highest-leverage decision in the whole render design, and the direct fix for the original Prismative.cpp flaw (one `UpdateSubresource` + `DrawIndexed` pair *per solid cell*, ~26,000 draw calls/frame at modest fill).

**Face culling:** a face is only emitted if the neighboring cell (in world space, across chunk boundaries where relevant) is not solid. This requires the mesher to query `World::Solid()`, not just the local chunk array, at chunk edges.

**Mesh rebuild policy:** only when a chunk is marked `dirty` (an edit occurred inside it, or a neighbor's edit could have exposed/hidden one of its boundary faces). Never rebuilt speculatively, never rebuilt every frame.

### 4.3 Texture atlas — fixing the single hardcoded-texture bug
The originally reviewed prototype's `RebuildMesh` merged cube geometry correctly but rendered the *entire merged mesh* with one hardcoded texture slot regardless of the actual block type at each face — meaning dirt, wood, and every other cube-shaped block visually rendered as stone. The fix: one shared atlas texture built once at startup (currently a 3-column × 2-row grid of tiles), with each emitted face's UV computed from *that voxel's own* atlas slot (`AtlasRect(slot, ...)`) rather than a single fixed rectangle. One draw call per chunk is preserved; per-voxel texture correctness is restored. The atlas layout constants in `main.cpp` (`ATLAS_COLS`/`ATLAS_ROWS`) and the tile order baked in `supplement.cpp`'s `GenerateGameTextures()` must stay in agreement — documented explicitly in both files' comments so a future edit to one doesn't silently desync from the other.

### 4.4 Non-cube shapes — pipes
Straight/corner/junction pipe geometry is excluded from the merged chunk mesh (its silhouette isn't a full cube face) and drawn individually per instance, sharing one pipe texture. **This is a known, explicitly flagged scaling limit**, not an oversight: fine at prototype density, but once pipe networks become the majority of placed blocks (the expected end-state of a BuildCraft-style base), per-segment draw calls reproduce the exact per-object cost problem the chunk-mesh fix solved for cubes. Two documented remedies for later, neither implemented now: (a) bake oriented pipe geometry into the chunk mesh too, with a per-instance transform baked at mesh-build time, or (b) instanced rendering — one draw call per pipe *shape* across all loaded chunks, using a per-instance transform buffer. Left as a deliberate, visible technical debt marker rather than solved prematurely. The prototype additionally simplifies all three pipe shapes to one placeholder cube mesh; distinct L/plus geometry per shape is deferred alongside real network connectivity to Milestone 2.

### 4.5 Block picking — GPU-exact, not CPU-approximate
Two options were compared for "what block is the player looking at":
- **CPU raycast with fixed-step marching** (the original Prismative.cpp approach, `t += 0.1f`): rejected — can skip thin geometry at shallow angles, and face normals are inferred after the fact by comparing consecutive sampled cells, which is wrong at cell-corner crossings.
- **Amanatides–Woo exact voxel DDA traversal** (1987): the algorithm actually implemented. Steps exactly one voxel boundary at a time using only comparisons and one addition per step; the crossed face's normal falls directly out of which axis was stepped, not inferred. This is precise, well-understood, and cheap.
- **GPU ID-buffer readback** (discussed as a theoretically superior alternative once shapes get complex): render block-ID+face-index as color into a tiny offscreen target and read back the pixel under the crosshair. Correct for arbitrary non-cube geometry (a raycast against a ramp's *actual surface*, not its bounding cube) since it reuses the exact geometry already rasterized. **Not implemented in the prototype** — flagged as the eventual right answer once shapes beyond simple pipes exist, but Amanatides–Woo is sufficient and simpler while every solid shape is either a full cube or well-approximated by one for picking purposes.

### 4.6 2D/3D split
One D3D11 device, two passes, never two graphics APIs at runtime:
1. **World pass** — perspective projection, depth test on, chunk meshes plus individually-drawn special shapes.
2. **UI pass** — its own shader/input-layout/cbuffer/blend-state/depth-state, drawn immediately after the world pass each frame. Vertex positions are supplied already in pixel space and mapped straight to NDC in the vertex shader (`x/screenW*2-1`, `1-y/screenH*2`) — an orthographic projection in substance, without needing a matrix for it. Depth test/write off, alpha blending on (standard src-alpha/inv-src-alpha), so panels and text composite correctly over the 3D scene. Every UI vertex carries a color tint alongside its UV, so the same textured-quad pipeline draws plain glyphs, tinted panels/borders, and full-color icons.

A small font-glyph atlas (ASCII 32–126, monospace grid, one reserved solid-white cell for untextured tinted rectangles) is generated by GDI+ at load time the same way the block atlas is — this is what the crosshair, hotbar, and pause menu (Resume/Save/Load/Quit) are built from. Hotbar item icons are drawn by sampling the *same* block atlas / pipe texture the world pass uses, rather than generating separate icon art.

GDI+ is used exclusively at load time to *generate* textures into bitmaps that get uploaded once to GPU textures; it never touches the frame loop. This was an explicit decision against mixing GDI+ and Direct3D rendering live, which would fight over the swap chain surface.

### 4.7 Camera and player view
Standard FPS mouse-look: yaw from horizontal delta, pitch from vertical delta, pitch clamped to avoid gimbal flip (±~1.55 rad). Perspective projection, near/far planes wide enough for the load radius in use.

---

## Part V — Simulation: the falling-block system as the reusable pattern

### 5.1 Why gravity is designed this way
This is the *first* simulation system, and it establishes the template every later system (item transfer, machine ticking) should copy.

**Mechanism:**
1. An edit that removes support beneath a non-foundational block pushes that block's position onto a `fallQ` queue — it is not processed synchronously.
2. Each tick, at most `MAX_FALLS` queue entries are drained — a hard constant cap, independent of queue length.
3. Draining an entry either moves the block down one cell (re-queuing if still unsupported) or lands it permanently.

**Why this matters beyond gravity itself:** it guarantees **O(1)-bounded per-tick simulation cost regardless of how catastrophic a single edit was** — removing the foundation under an enormous structure cannot spike frame time, because the resulting cascade is smoothed across many ticks instead of resolved in one pass. Every future per-tick system (item movement through a pipe network, machine progress updates) is expected to follow this same shape: a capped-per-tick work queue, never an unbounded scan of "everything that might need attention this frame."

### 5.2 Correctness details worth preserving
- **Deferred mutation, not in-place iteration mutation.** The queue pattern avoids the exact bug found in LG2.cpp, where `SpreadGrass`/`HandleLifespans` mutated (inserted into/erased from) the same `std::vector<Block>` being iterated, and where a `Quadtree` held raw pointers into that vector that were invalidated by any subsequent insert or erase.
- **Bulk-load must bypass live gravity checks entirely** — this was an identified bug in the reviewed prototype: calling the live `World::Set()` during save-file loading runs gravity-trigger logic against a world that's only partially reconstructed, and because `unordered_map` iteration order is arbitrary, a block can appear to "lose support" simply because the block that would have supported it hasn't been placed yet. Fix: a separate `SetRaw()` path used only during load, which writes the block into its chunk directly and performs no support checks at all. Gravity re-evaluates naturally from then on as the player interacts with the loaded world.

### 5.3 Tick model
Fixed-timestep logic tick, decoupled from render/present rate via an accumulator loop (`FIXED_DT = 1/60`), generalized into the main loop rather than scattered per-feature.

---

## Part VI — Item Logistics (designed, not yet implemented in the prototype)

### 6.1 The load-bearing interface
Every object capable of holding items — chests, machine input/output buffers, the player's own inventory, and pipe endpoints — implements one shared interface:
```cpp
class IItemHandler {
public:
    virtual int slotCount() const = 0;
    virtual ItemStack get(int slot) const = 0;
    virtual ItemStack insert(int slot, ItemStack stack, bool simulate) = 0; // returns remainder
    virtual ItemStack extract(int slot, int count, bool simulate) = 0;
};
```
The `simulate` flag is the key idea: a router can ask several candidate destinations "would you accept this?" before committing to one, with zero rollback machinery required, because a simulated call is guaranteed to have no side effects. This is exactly why routing code never needs to know what concrete type sits on the other end of a connection — a chest, a furnace's input slot, and a pipe junction all answer the same two questions the same way.

### 6.2 Networks as graphs, not simulated physical entities
Two architectural lineages were compared for how items exist while in transit through pipes:
- **Items as entities** (classic BuildCraft) — each stack is a real object with a position sliding through space. Visually rich, but costs thousands of ticking entities at scale, awkward serialization, and per-item per-junction pathing decisions.
- **Items as abstract transfers** (the chosen approach, closer to later logistics-mod designs) — the pipe network is a graph; each tick resolves "what quantity moves from where to where" as a graph computation, with visuals as non-authoritative interpolated ghosts layered on top for presentation only. This scales to large factories because the simulation cost is bounded by network topology complexity, not by total item count in transit.

### 6.3 Network connectivity — Union-Find, the one place a specific published algorithm is an exact fit
Placing a pipe segment adjacent to existing network segments is a `union(newSegment, neighbors)` operation. Two segments belong to the same network exactly when `find(a) == find(b)`. Tarjan's 1975 analysis of union-by-rank with path compression establishes that this runs in near-constant amortized time per operation — and placement (the common case during play) is exactly a union call.

**Removal is the one operation this structure doesn't support cheaply** — union-find has no efficient split. The designed approach: on removing a pipe segment, don't try to incrementally split the disjoint-set structure; instead flood-fill-rebuild only the network(s) that touched the removed segment, using union-find during that rebuild to re-merge as it proceeds. This keeps the common operation (placing pipe, which happens constantly while building) cheap, and pays a bounded, local cost only for the rare operation (breaking pipe).

### 6.4 Why not a graphics-paper-style structure here
Two literature detours were explicitly rejected as loose fits for this specific problem, worth recording so the reasoning isn't re-litigated later:
- **Sparse voxel octrees** (Laine & Karras, 2010) — designed for dense, near-fractal geometric detail rendered via GPU ray casting. This game's content is the opposite: large uniform runs of few block types with sharp axis-aligned boundaries. The flat/palette-compressed chunk array beats a tree structure here on both traversal cost and compression, because the content is already extremely repetitive.
- **Generic ray-tracing acceleration structures** in general — solving a harder, more general problem (arbitrary continuous-space geometry) than "which axis-aligned voxel does this ray enter next," which Amanatides–Woo already answers exactly and cheaply.

The disjoint-set structure, by contrast, is an exact match: the problem it was proven optimal for (maintaining connected components under incremental merges) is *literally* the problem pipe-network connectivity poses.

### 6.5 Status
Fully designed, **not yet coded**. This is explicitly Milestone 2+ work (see Part IX) — the prototype's job is to prove the world/render/save foundation first.

---

## Part VII — Save/Load System

### 7.1 Why the legacy approach was unacceptable
All four reference files persisted state via `file.write(reinterpret_cast<const char*>(&block), sizeof(Block))` — a raw struct dump. This fails three ways: (1) any struct field change silently corrupts every old save with no error; (2) no corruption detection — an interrupted write loads however far it got with no signal anything's wrong; (3) block identity is positional (enum/array order *is* the format), so adding a new block type during ongoing development reinterprets every existing save's blocks as the wrong type. A concrete bug was also found in LG2.cpp: `LoadGame` clears the quadtree and never rebuilds it, and separately, the quadtree holds pointers invalidated by `blocks.insert`/`erase` elsewhere — save/load interacting with a raw-pointer spatial index made the whole system fragile in a way that would have been very hard to diagnose from symptoms alone.

### 7.2 Format actually implemented
```
magic (u32 "VXLG") | version (u32)
player: pos.x,y,z (f32×3)  yaw,pitch (f32×2)  hotbarSelection (i32)
blockNameCount (u32) | [ nameLen(u16) nameBytes ] × count
blockCount (u32) | [ x,y,z (i32×3)  nameTableIndex(u8) ] × count
checksum (u32)  — FNV-1a over every byte above
```
Block identity is written and read via the **name table**, not the enum — this is the direct structural fix for the positional-ID corruption failure mode, and it's the reason `g_blockNames[]` exists as a parallel source of truth to `BlockID`.

### 7.3 Write sequence (crash safety)
1. Serialize the entire save into an in-memory buffer.
2. Write that buffer to `voxelproto.sav.tmp`.
3. Only if the write completes without error: rotate the existing `voxelproto.sav` to `voxelproto.sav.bak`, then rename `.tmp` into place as `voxelproto.sav`.

A crash or power loss at any point before step 3 completes leaves the previously-good save completely untouched — there is no window where the live save file is partially overwritten.

### 7.4 Load sequence (corruption safety)
1. Read the whole file into memory.
2. Compute FNV-1a over everything except the trailing checksum field; compare. Mismatch → abort before touching any live game state, log the reason.
3. Verify magic number and version — wrong magic or unsupported version aborts cleanly rather than attempting to interpret garbage as a world.
4. Build the saved-name → current-`BlockID` remap table. A name no longer present in the current build maps to `AIR` (with a logged warning) rather than silently reinterpreting as whatever ID happens to occupy that slot today.
5. Populate the world via `World::SetRaw()` — the gravity-bypassing bulk setter described in 5.2 — never the live `Set()` path, precisely because iteration order through the loaded records is not guaranteed to match spatial support order.
6. Restore player position, facing, and hotbar selection last.

### 7.5 Explicitly out of scope for the prototype, designed for later
Per-region multi-chunk files (grouping a 16×16 column of chunks behind one small offset-table header, so entering a new area is one file open instead of hundreds) — noted as the natural extension once a single monolithic save file becomes slow to write at scale, requiring no format redesign since chunk records are already self-contained with their own bounds and would simply move into region-scoped files.

---

## Part VIII — Reference Material and How Each Was Used

### 8.1 Prismative.cpp (D3D11, closest in spirit to the target)
**Kept (as concept):** manager-class decomposition (ResourceManager/InputManager/EntityManager/GameStateManager/Camera/Game); the multi-shape block idea (cube/tube/ramp/slab/pyramid/funnel, each its own vertex buffer) — directly informed the cube/straight/corner/junction shape system; player AABB collision at multiple height samples; raycasting for block picking (approach superseded by exact DDA, per 4.5).
**Discarded:** the render loop's per-solid-cell draw call (the ~26,000-draw-call/frame problem, fixed by chunk meshing); fixed-step raycast marching (0.1 units/step, replaced by DDA); per-frame (not per-second) physics constants.

### 8.2 drillder.cpp (GDI+ orthogonal slice viewer)
**Kept (as concept):** integer axis-aligned player facing (`dirX,dirY,dirZ`) with 90°-rotation via integer swizzle instead of float yaw/pitch for block-facing purposes — directly informs how pipe/machine orientation should be represented, though not yet implemented in the prototype since no oriented blocks exist yet; durability-on-voxel (`Voxel{type, isObstacle, durability}`) as the germ of mining/machine-progress mechanics; the fixed-tick movement accumulator pattern (`moveTimerAccumMs`/`MOVE_DELAY_MS`), generalized into the main tick loop.
**Discarded:** world wrapping (rejected per 1.2); the three-orthogonal-slice rendering itself is not part of the game, though it was flagged as worth keeping as an optional debug overlay (not yet built).

### 8.3 LG2.cpp ("Lawn Gone," 2D cellular automaton)
**Kept (as concept):** budgeted-work-per-tick (`MAX_SPREAD_PER_CYCLE`) as the direct ancestor of `MAX_FALLS`; deferred mutation (collect into a side buffer, apply after the iteration completes) as the direct ancestor of the falling-block queue's structure; cheap subsystem presence guards (skip a whole update pass when nothing needs it) as a principle, though its actual implementation (`any_of` scans over the full block vector every tick) was identified as the wrong way to implement that principle — a maintained running count per type is the correct version.
**Discarded/warned about:** the quadtree-over-uniform-grid mismatch (a uniform grid wants O(1) index lookup, not a spatial tree); the dangling-pointer bug from mixing a pointer-based spatial index with a mutating vector; positional-enum save corruption (Part VII).

### 8.4 cc_2_2_2.cpp (2D GDI+ sandbox, 145 texture generators)
**Kept (as concept only, not code):** GDI+ procedural texture generation baked once at load time into GPU-resident textures — directly informs `supplement.cpp`'s role and structure, though every generator function in `supplement.cpp` is newly written.
**Discarded:** the ~700-line copy-pasted `if(shift)/else if(ctrl)/else` key-handling block (noted as a pattern to actively avoid when hotbar/keybinding code is written — a small `{key,modifier}→action` table is the correct shape); holding 145 live `Bitmap*` objects at once rather than baking into a shared atlas.

### 8.5 Open-source/academic material reviewed for concepts (nothing copied)
- **fogleman/Craft** (MIT-licensed) — read for chunk/mesh/persistence architecture as a size-appropriate reference; not used as a source of copied code.
- **Minetest, BuildCraft** (LGPL) — read for world/mapgen split and pipe-routing *logic*, deliberately not copied from due to copyleft; re-implemented from understanding only.
- **Amanatides & Woo, 1987** — implemented directly from the published algorithm (Part 4.5); algorithms from papers carry no licensing restriction regardless of the paper's own copyright, since copyright doesn't cover the underlying method.
- **Tarjan, 1975** (union-find analysis) — informs the designed-but-unbuilt network connectivity system (6.3).
- **Laine & Karras, 2010** (sparse voxel octrees) — evaluated and explicitly rejected as a loose fit (6.4); documented so this isn't re-evaluated needlessly later.

---

## Part IX — Milestones

**Milestone 1 (current prototype target — implemented):** world storage, chunked meshing with correct per-block atlas texturing, gravity/falling blocks, exact-DDA block picking, place/break, crash-safe versioned save/load, basic FPS movement and collision, and a rudimentary dual-pass UI (crosshair, hotbar with selection highlight, pause menu with Resume/Save/Load/Quit). No items, no crafting, no machines beyond a placeholder block.

**Milestone 2:** `IItemHandler` interface implemented for chests and player inventory; basic UI (2D ortho pass) for inventory/hotbar; pipe placement forms visible networks (union-find connectivity, no item flow yet); distinct per-shape pipe geometry (straight/corner/junction) replacing the current placeholder cube.

**Milestone 3:** item flow through networks (graph-based transfer, not entity-based); machine processing state (input buffer → timed transform → output buffer) using the same interface chests use.

**Milestone 4:** recipe data and tiered unlock gating; the point at which "leveled crafting" becomes real rather than conceptual.

Each milestone is a strict superset of the previous — nothing in Milestone 1's architecture needs to be revisited to support Milestones 2–4, by design (this was checked explicitly for each: the `IItemHandler` interface, the union-find network design, and the chunk/save format were all shaped up front specifically so later milestones are additive, not restructuring work).

---

## Part X — Build

Two source files, one compiler invocation, no project file needed:

```
cl main.cpp supplement.cpp /link d3d11.lib dxgi.lib d3dcompiler.lib gdiplus.lib gdi32.lib user32.lib /SUBSYSTEM:WINDOWS
```

or with MinGW-w64 (used during development to compile-check this prototype on a non-Windows host, since it ships full D3D11/DXGI/D3DCompiler/GDI+ headers and import libraries):

```
x86_64-w64-mingw32-g++ -std=c++17 -O2 -mwindows main.cpp supplement.cpp -o voxelgame.exe \
  -ld3d11 -ldxgi -ld3dcompiler -lgdiplus -lgdi32 -luser32 -lole32 -static-libgcc -static-libstdc++
```

Controls: WASD to move, mouse to look (click once to capture the cursor), Space to jump, left-click to break the targeted block, right-click to place the selected hotbar block, number keys 1–9 to select a hotbar block, F5 to save, F9 to load, Esc to open/close the pause menu (Resume / Save Game / Load Game / Quit, click with the freed cursor).
