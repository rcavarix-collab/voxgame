# Voxistics — Complete Design Reference

## Part I — Vision and Constraints

### 1.1 What this is
A first-person 3D voxel game. Grid-locked terrain manipulation (Minecraft-lineage) is the solid foundation; automated item logistics (BuildCraft-lineage) was the original second pillar, but it's currently an open question rather than a committed direction — the pipe blocks that would carry it were pulled back out of the prototype (Part IV §4.4, Part VI §6.5) while what kind of game this actually becomes gets figured out on top of the world/render/save foundation Milestone 1 already proved. If logistics is the answer, Part VI's design (interface, network model, algorithm choice) is ready to pick back up largely as-is.

### 1.2 Non-negotiable constraints, and why each exists
- **Multi-file project, mechanically decomposed by subsystem** (`common.h`; `world.h`/`world.cpp`; `render.h`/`render.cpp`; `audio.h`/`audio.cpp`; `persist.h`/`persist.cpp`; `game.h`/`game.cpp`; `textures.cpp`; `music_synth.cpp`; `main.cpp` as the thin entry point) plus save files on disk. The original two-source-file rule (chosen to sidestep multi-file Visual Studio project configuration, historically a recurring blocker) was explicitly lifted once the single-file monolith's own size started costing more — in build/edit friction and in chunk-load-time frame stutter traced partly to it — than the file-count risk it was written to avoid. The split is a mechanical decomposition, not a redesign: every global stays a global (`extern` in its owning header, defined once in exactly one `.cpp`), split along existing subsystem boundaries the design doc already named as Parts.
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
- Newly-visible columns are *enqueued*, not generated immediately — actual terrain generation drains a capped number of columns per tick, the same bounded-work-queue pattern Part V's falling-block system established (Section 5.1). Generating the whole load radius synchronously (unavoidable at least once, for the initial spawn) would otherwise stall the first frame while every chunk in range generates and meshes at once. Columns are queued **ring by ring outward from the player's own column**, so the ground underfoot always generates first (a corner-to-corner raster order once put the spawn column dozens of ticks down the queue).
- **Player physics never runs on ground that doesn't exist yet.** Ungenerated space reads as air, so until the player's own column is resident they're held exactly in place (no gravity, no movement), and walking into a not-yet-resident column is refused like walking into a wall. A new game starts the player standing on the surface (terrain height is a pure function of x/z, so no chunk is needed to know it). As a backstop, a player whose box overlaps solid blocks — terrain appearing around an edge, a block falling onto them — is lifted one block per tick until free, and a placement that would overlap the player's own box is refused.
- Chunks leaving the radius (past a small hysteresis margin, so a player oscillating right at the boundary doesn't thrash) are evicted: their raw block data moves from `World::chunks` into a separate `g_evictedChunks` map (releasing GPU buffers) rather than being dropped, and restored byte-for-byte if the player returns rather than being regenerated (which would silently overwrite edits). This is in-memory only, not disk-backed — a deliberate scope decision, since building a persistent on-disk streaming cache before the game's fundamental scale (voxel size itself is still an open question) is settled would risk being infrastructure for the wrong shape of world. It still bounds the two things that actually mattered: `RebuildDirtyChunks`'s per-frame dirty-scan and the world draw loop's per-frame iteration, both of which previously scaled with total lifetime-explored area rather than the currently-loaded area (Part 1.3). `SaveGame` walks both `World::chunks` and `g_evictedChunks` to capture the complete world regardless of what's currently resident.

---

## Part III — Block Model

### 3.1 Identity: name-based, not position-based
`BlockID` (an enum) is a *runtime* convenience only. The actual identity written to disk is the block's **string name** (`g_blockNames[]`). This single decision is what allows the block list to grow indefinitely during development — adding, removing, or reordering enum entries never corrupts an existing save, because loading remaps saved names onto whatever the current build's names are, substituting a safe "unknown" (air) for anything genuinely removed, with a logged warning rather than a silent misread.

### 3.2 Metadata table
One flat array, `BlockInfo g_info[BLOCK_COUNT]`, is the single source of truth per block:
```
foundational : bool   — never falls, always supports (Section V)
solid        : bool   — participates in collision and raycast hits
tex          : int    — atlas slot
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

---

## Part IV — Rendering

### 4.1 Visual target
Blocky, saturated, clear silhouettes. Procedurally generated textures (patterns drawn in code at startup), not imported art — consistent with the "no external asset files" constraint and directly reusing the *idea* (not the code) behind cc_2_2_2.cpp's 145 generator functions.

### 4.2 The central performance decision: per-chunk merged meshing
Every exposed face of every cube-shaped block in a chunk is combined into one vertex/index buffer pair, drawn with a single `DrawIndexed` call per chunk. This converts draw cost from **O(blocks)** to **O(loaded chunks)** — the single highest-leverage decision in the whole render design, and the direct fix for the original Prismative.cpp flaw (one `UpdateSubresource` + `DrawIndexed` pair *per solid cell*, ~26,000 draw calls/frame at modest fill).

**Face culling:** a face is only emitted if the neighboring cell (in world space, across chunk boundaries where relevant) is not solid. This requires the mesher to query `World::Solid()`, not just the local chunk array, at chunk edges.

**Mesh rebuild policy:** only when a chunk is marked `dirty` (an edit occurred inside it, or a neighbor's edit could have exposed/hidden one of its boundary faces). Never rebuilt speculatively, never rebuilt every frame. `World::dirtyChunks` holds exactly the resident chunks whose flag is set (chunks enter and leave `World::chunks` only through `GetOrCreateChunk`/`AdoptChunk`/`TakeChunk`/`ClearChunks`, which keep the two in step), so the rebuilder looks only at chunks that need work — nothing at all while the world is static — and picks the ones **nearest the camera** first. New ground, a load or a render-distance change fills in outward from the player rather than in hash-map order.

**Per-frame cost controls, added after real play surfaced measurable stutter:**
- **Neighbor-solidity fast path** (`NeighborSolid`, render.cpp) — the ~67% of a chunk's cells whose neighbor is in the same chunk read straight out of `blocks[]` instead of paying `World::Solid`'s floor-divide-plus-hash-lookup cost for every one of a chunk's up to 24,576 neighbor checks; only checks that actually cross a chunk boundary fall back to the general path.
- **Capped mesh rebuilds per frame** (`MAX_CHUNK_REBUILDS_PER_FRAME = 6`, render.cpp) — entering unexplored terrain can mark several newly-generated columns dirty in the same tick; rebuilding all of them (each a full mesh pass plus two synchronous GPU `CreateBuffer` calls) in one frame is exactly the kind of single-frame spike this cap smooths across several frames instead, mirroring Part V's per-tick work-queue philosophy.
- **View-frustum culling** (`ExtractFrustum`/`FrustumIntersectsAABB`, render.cpp) — the six view-frustum planes are extracted directly from the combined view-projection matrix each frame; a chunk whose AABB doesn't intersect it is skipped in the draw loop entirely. Bounds per-frame draw cost by what the camera can actually see rather than by how much of the world happens to be currently loaded.
- **Chunk eviction** (Section 2.4) — the companion fix to the above: without it, "currently loaded" itself only ever grows, so even a perfectly culled draw loop and a capped rebuild budget would still be iterating (if not drawing or meshing) an ever-larger set every frame. Together, loaded-set size and per-frame draw/mesh work both now track the player's current position rather than their lifetime path through the world.

### 4.3 Texture atlas — fixing the single hardcoded-texture bug
The originally reviewed prototype's `RebuildMesh` merged cube geometry correctly but rendered the *entire merged mesh* with one hardcoded texture slot regardless of the actual block type at each face — meaning dirt, wood, and every other cube-shaped block visually rendered as stone. The fix: one shared atlas texture built once at startup (currently a 3-column × 2-row grid of tiles), with each emitted face's UV computed from *that voxel's own* atlas slot (`AtlasRect(slot, ...)`) rather than a single fixed rectangle. One draw call per chunk is preserved; per-voxel texture correctness is restored. The atlas layout constants in `common.h` (`ATLAS_COLS`/`ATLAS_ROWS`) and the tile order baked in `textures.cpp`'s `GenerateGameTextures()` must stay in agreement — documented explicitly in both files' comments so a future edit to one doesn't silently desync from the other.

Two seam rules, both from a visible 1px line on every block: (1) tiles are plotted pixel-exact straight into the atlas buffer, each clipped to its own square — GDI+ pens (a 3px border centred on x+1, half-pixel offset mode) used to paint a strip into whichever neighbouring tile had been drawn first; (2) `AtlasRect` insets UVs by only 1/64 texel. A half-texel inset under point sampling maps a face onto texel centres 0.5–63.5, which draws the first and last texel column of every tile at half width. With no MSAA, pixel centres never extrapolate past a face, so a tiny guard against float error is all that's needed.

### 4.4 Non-cube shapes — removed pending direction
Pipe blocks (straight/corner/junction) and their per-instance rendering were implemented in an earlier pass — drawn individually outside the merged chunk mesh, since their silhouette isn't a full cube face — but pulled back out of the prototype entirely (block types, meshes, texture, hotbar icons) while the game's actual direction is still being decided. The scaling problem that implementation ran into is still worth remembering if any non-cube geometry returns: per-instance draw calls reproduce the exact per-object cost problem the chunk-mesh fix (4.2) solved for cubes, so it doesn't scale past prototype density without either (a) baking oriented geometry into the chunk mesh with a per-instance transform at mesh-build time, or (b) instanced rendering (one draw call per shape across all loaded chunks via a transform buffer). Neither is implemented, and there's currently nothing in the block roster that needs either — every current block is a plain cube.

### 4.5 Block picking — GPU-exact, not CPU-approximate
Two options were compared for "what block is the player looking at":
- **CPU raycast with fixed-step marching** (the original Prismative.cpp approach, `t += 0.1f`): rejected — can skip thin geometry at shallow angles, and face normals are inferred after the fact by comparing consecutive sampled cells, which is wrong at cell-corner crossings.
- **Amanatides–Woo exact voxel DDA traversal** (1987): the algorithm actually implemented. Steps exactly one voxel boundary at a time using only comparisons and one addition per step; the crossed face's normal falls directly out of which axis was stepped, not inferred. This is precise, well-understood, and cheap.
- **GPU ID-buffer readback** (discussed as a theoretically superior alternative once shapes get complex): render block-ID+face-index as color into a tiny offscreen target and read back the pixel under the crosshair. Correct for arbitrary non-cube geometry (a raycast against a ramp's *actual surface*, not its bounding cube) since it reuses the exact geometry already rasterized. **Not implemented in the prototype** — flagged as the eventual right answer once non-cube shapes exist again, but Amanatides–Woo is sufficient and simpler while every solid shape in the current roster is a full cube.

### 4.6 2D/3D split
One D3D11 device, three passes, never two graphics APIs at runtime:
1. **Sky pass** — a large untextured, vertex-colored box centered on the camera each frame, drawn first with depth test/write both off (reusing the UI pass's depth-disabled state) so the opaque world pass always overdraws it regardless of the box's actual size. Its view matrix drops the eye position entirely (rotation only), the standard skybox trick for making it rotate with the camera's look direction but never translate with the player's movement. Top/bottom faces are a flat color; the 4 side faces interpolate per-vertex from a zenith color at the top edge to a horizon color at the bottom edge, which is what actually reads as sky since play happens near-horizontal. No cubemap texture, no sun/time-of-day — "basic and functional" is the target here, not atmospheric.
2. **World pass** — perspective projection, depth test on, chunk meshes plus individually-drawn special shapes.
3. **UI pass** — its own shader/input-layout/cbuffer/blend-state/depth-state, drawn last each frame. Vertex positions are supplied already in pixel space and mapped straight to NDC in the vertex shader (`x/screenW*2-1`, `1-y/screenH*2`) — an orthographic projection in substance, without needing a matrix for it. Depth test/write off, alpha blending on (standard src-alpha/inv-src-alpha), so panels and text composite correctly over the 3D scene. Every UI vertex carries a color tint alongside its UV, so the same textured-quad pipeline draws plain glyphs, tinted panels/borders, and full-color icons.

A small font-glyph atlas (ASCII 32–126 in monospace grids, plus a solid-white strip for untextured tinted rectangles) is generated by GDI+ at load time the same way the block atlas is. Text is drawn **1:1** — one texel per screen pixel, point-sampled, snapped to whole pixels — so it stays crisp instead of being resampled from one master size; to still offer several sizes, the glyph set is baked once per size ("band", cell heights 14/18/22/28/34/44 px) and a requested scale picks the nearest band. Glyphs step by the font's own monospace advance rather than a full padded cell, so letters sit at normal text spacing and every menu label fits its row. Rectangles sample the centre of the white strip, so panels and the flat menu dim are uniform to their edges (stretching a glyph cell with a transparent rim once gave every panel a wide faded border and the dim a vignette). Hotbar icons are 32px — exactly half a tile — so point sampling stays even. The crosshair, hotbar, every menu and its sliders are all built from it. Hotbar item icons are drawn by sampling the *same* block atlas the world pass uses, rather than generating separate icon art.

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
Fully designed, **not yet coded**, and now provisional rather than committed: the pipe blocks that would have been this system's visible surface were pulled back out of the prototype (4.4) while the game's actual direction is still being decided. The design here is kept as a record of the thinking, not a queued-up Milestone 2 task list — whether item logistics is what this game becomes is an open question again, not a foregone conclusion the prototype is just waiting to catch up to.

---

## Part VII — Save/Load System

### 7.1 Why the legacy approach was unacceptable
All four reference files persisted state via `file.write(reinterpret_cast<const char*>(&block), sizeof(Block))` — a raw struct dump. This fails three ways: (1) any struct field change silently corrupts every old save with no error; (2) no corruption detection — an interrupted write loads however far it got with no signal anything's wrong; (3) block identity is positional (enum/array order *is* the format), so adding a new block type during ongoing development reinterprets every existing save's blocks as the wrong type. A concrete bug was also found in LG2.cpp: `LoadGame` clears the quadtree and never rebuilds it, and separately, the quadtree holds pointers invalidated by `blocks.insert`/`erase` elsewhere — save/load interacting with a raw-pointer spatial index made the whole system fragile in a way that would have been very hard to diagnose from symptoms alone.

### 7.2 Format actually implemented
```
magic (u32 "VXLG") | version (u32, currently 3)
player: pos.x,y,z (f32×3)  yaw,pitch (f32×2)  hotbarSelection (i32)
blockNameCount (u32) | [ nameLen(u16) nameBytes ] × count
blockCount (u32) | [ x,y,z (i32×3)  nameTableIndex(u8) ] × count
checksum (u32)  — FNV-1a over every byte above
```
Block identity is written and read via the **name table**, not the enum — this is the direct structural fix for the positional-ID corruption failure mode, and it's the reason `g_blockNames[]` exists as a parallel source of truth to `BlockID`. Version bumped from 1 to 2 when a settings block was first added inline here; bumped again to 3 when that block was pulled back out into the separate global settings file described in 7.2.2 below. A v2 file is still loaded rather than rejected — its world/player data is byte-identical to v3's, just followed by a settings block v3 no longer has — specifically so the migration described in 7.2.2 can run. Only something older than v2, or newer than the running build understands, is rejected cleanly by the version check (Section 7.4).

### 7.2.1 Save location
`Documents\My Games\Voxistics\` — the conventional PC-game save location (Skyrim and most Bethesda/Paradox titles use the same pattern), chosen over a hidden `%LOCALAPPDATA%` folder specifically because it's visible and easy for players to find, back up, or copy between machines. The directory is resolved fresh on every save/load (`SHGetKnownFolderPath(FOLDERID_Documents, ...)` plus the `My Games\Voxistics` subfolder, created if missing) rather than cached once, so a transient failure doesn't permanently strand the game on a fallback it no longer needs.

Two things can go wrong with a known-folder lookup in the real world, and both are handled by falling back to the current working directory (this prototype's original behavior) rather than failing the save outright: the `SHGetKnownFolderPath` call itself failing (rare, but has no reason to be fatal when a working fallback exists), and something unexpected already occupying part of the intended path — concretely, a plain file sitting where a folder needs to be. The code checks `exists() && !is_directory()` before calling `create_directories()` specifically to catch that second case rather than letting a failed directory creation surface as a mysterious save failure. `EnsureDirectoryBulletproof()` factors this check out of `GetSaveDirectory()` so `GetSavesDirectory()` (7.2.4) can reuse the identical logic for its own subfolder rather than duplicating it.

### 7.2.4 Multi-slot saves
Individual save files live in `Documents\My Games\Voxistics\Saves\slot1.sav` .. `slot5.sav` (`MAX_SAVE_SLOTS = 5`) rather than the single fixed `voxelproto.sav` this prototype originally had — the title screen's New Game / Load Game (Part XII) needs more than one world to choose between. The per-slot file format is completely unchanged (7.2's v3 format, byte-for-byte) — only *which path* `SaveGame`/`LoadGame` read and write moved, so this required no version bump.

A slot's occupied/empty status for the picker list (12.3) is read straight off the filesystem (`std::filesystem::exists`) rather than a stored index or catalog file, so it can never drift out of sync with what's actually on disk. Slots are auto-named "World N" by position rather than player-chosen names — building a full text-entry keyboard widget for renaming was scoped out of this pass (documented as real future work, not forgotten) in favor of shipping multiple slots that work correctly first.

**Migration:** a save from before multi-slot support existed lived directly at `Documents\My Games\Voxistics\voxelproto.sav`. `MigrateLegacySingleSaveIfPresent()`, called once at startup, moves that file into `Saves\slot1.sav` if it exists and slot 1 doesn't already have its own save — the same "migrate once, on first encounter, never overwrite something newer" philosophy as the v2-settings migration (7.2.2), so a save from before this change isn't silently orphaned.

### 7.2.5 Day clock field (v4)
`SaveGame`/`LoadGame` gained one field, `g_dayTimeSeconds` (Part XIII), appended right after `hotbarIndex` — save version bumped to v4. It's world state, not a settings.cfg preference, since different saves can legitimately be at different points in their day. v2 and v3 saves (predating the day clock) still load; they default to 0.0 (dawn) rather than needing a value that was never meaningful for them.

### 7.2.2 Global settings file
Gameplay/UI preferences (mouse sensitivity, inversion, render distance, the FPS counter toggle, master/music volume, and every keybinding) originally lived inside the save file itself (v2, above). They now live in `Documents\My Games\Voxistics\settings.cfg` — the same directory as the save file, resolved through the same bulletproofed `GetSaveDirectory()` — independent of any world save. Two reasons drove the move: a title screen's Options needs to read/write these before any save is loaded or even exists, and preferences arguably belong to the *player*, not to any one world, so they should carry over between saves rather than reset per-world.

The format is plain `key=value` lines (`sensitivityX=1.000000`, `keybind.forward=87`, etc.) rather than the save file's versioned binary encoding — a handful of human-meaningful scalars a player might reasonably want to inspect or hand-edit, where "unknown keys are ignored, a missing key keeps its compiled-in default" gives forward/backward compatibility for free, with no version field needed. It's written via the same temp-file-then-rename pattern as the save file (never a direct in-place write), and re-read once at startup before anything else consults these values.

It's saved incrementally rather than only at one moment: every toggle click, every Reset to Default, every completed slider drag (once when the drag ends, not on every pixel of motion), and every committed keybind rebind writes it immediately, so a preference change survives even if the process is later killed without a clean exit.

**Migration from v2 saves:** loading a v2 save (7.2 above) parses its embedded settings block as before and applies it to the running session, and — only if `settings.cfg` doesn't exist yet — writes it out once via the same `SaveSettings()` used everywhere else. Once that file exists it's the sole source of truth from then on; this path only ever fires for the first v2 save loaded on a machine that hasn't run the new format yet.

### 7.2.3 Write sequence (crash safety) — settings file
Same shape as the save file's (7.3): the whole file is one `ostringstream`-built buffer, written to `settings.cfg.tmp`, then renamed into place — never edited in-place, so a crash mid-write leaves the previous version of the file intact rather than truncated.

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
**Kept (as concept only, not code):** GDI+ procedural texture generation baked once at load time into GPU-resident textures — directly informs `textures.cpp`'s role and structure, though every generator function in `textures.cpp` is newly written.
**Discarded:** the ~700-line copy-pasted `if(shift)/else if(ctrl)/else` key-handling block (noted as a pattern to actively avoid when hotbar/keybinding code is written — a small `{key,modifier}→action` table is the correct shape); holding 145 live `Bitmap*` objects at once rather than baking into a shared atlas.

### 8.5 Open-source/academic material reviewed for concepts (nothing copied)
- **fogleman/Craft** (MIT-licensed) — read for chunk/mesh/persistence architecture as a size-appropriate reference; not used as a source of copied code.
- **Minetest, BuildCraft** (LGPL) — read for world/mapgen split and pipe-routing *logic*, deliberately not copied from due to copyleft; re-implemented from understanding only.
- **Amanatides & Woo, 1987** — implemented directly from the published algorithm (Part 4.5); algorithms from papers carry no licensing restriction regardless of the paper's own copyright, since copyright doesn't cover the underlying method.
- **Tarjan, 1975** (union-find analysis) — informs the designed-but-unbuilt network connectivity system (6.3).
- **Laine & Karras, 2010** (sparse voxel octrees) — evaluated and explicitly rejected as a loose fit (6.4); documented so this isn't re-evaluated needlessly later.

---

## Part IX — Milestones

**Milestone 1 (current prototype target — implemented):** world storage, chunked meshing with correct per-block atlas texturing, gravity/falling blocks, exact-DDA block picking, place/break, crash-safe versioned save/load across five independent slots (7.2.4), a separate global settings file (7.2.2) for gameplay/UI preferences, basic FPS movement and collision, a title screen (New Game / Load Game / Options / Quit, Part XII) fronting gameplay rather than dropping straight into it, a rudimentary dual-pass UI (crosshair, hotbar with selection highlight, an in-game Pause menu with Resume/Options/Save/Load/Quit to Title/Quit, an Options hub shared by Pause and the title screen alike branching into six settings submenus: Look Settings with independent X/Y sensitivity sliders and X/Y inversion, Graphics with a render-distance slider, Display with an FPS-counter toggle, Audio with working Master/Music volume sliders, Accessibility with a field-of-view slider, toggle-to-move, and a high-contrast UI palette (Part XI), and fully remappable Keybindings covering every keyboard-or-mouse-bound action — each submenu with its own Reset to Default), a basic camera-following skybox, and a looping procedural ambient music track (Part X). No items, no crafting, no machines beyond a placeholder block.

**Milestone 2 (provisional — see Part VI §6.5):** `IItemHandler` interface implemented for chests and player inventory; basic UI (2D ortho pass) for inventory/hotbar; pipe placement forms visible networks (union-find connectivity, no item flow yet); per-instance oriented, connection-aware pipe geometry. Contingent on item logistics actually being the direction this game takes — not committed the way Milestone 1 was.

**Milestone 3:** item flow through networks (graph-based transfer, not entity-based); machine processing state (input buffer → timed transform → output buffer) using the same interface chests use.

**Milestone 4:** recipe data and tiered unlock gating; the point at which "leveled crafting" becomes real rather than conceptual.

Each milestone is a strict superset of the previous — nothing in Milestone 1's architecture needs to be revisited to support Milestones 2–4, by design (this was checked explicitly for each: the `IItemHandler` interface, the union-find network design, and the chunk/save format were all shaped up front specifically so later milestones are additive, not restructuring work).

---

## Part X — Audio

### 10.1 Playback backend
XAudio2 (`xaudio2.h`/`xaudio2.lib`), initialized once at startup (`InitAudio()`) after `CoInitializeEx` — the one thing in this codebase that actually requires COM initialized on the calling thread (`SHGetKnownFolderPath` manages its own COM state internally, so nothing earlier needed this). One `IXAudio2SourceVoice` plays the day-cycle music as a stream of small generated chunks (Part XIV). There is only a Music channel so far — no sound effects — but Master and Music are already separate settings/sliders specifically so a future SFX channel is just another source voice under the same mastering voice, not a remix of the existing volume model. `InitAudio()` failing (no usable audio device, missing driver, etc.) is non-fatal: every audio entry point is guarded by a null check, so the game is fully playable, just silently, rather than refusing to start.

### 10.2 Procedural, not an asset
All music is synthesized in code at runtime — no audio file exists anywhere in the repo, and there's nothing to license — the same "bake it in code, never load an external asset" philosophy the block/UI textures use (8.4). An earlier 20-second looping ambient pad and a later single-curve arpeggio track both preceded the current composition; Part XIV describes what ships now.

### 10.3 Why mono
The track is generated as a single channel throughout, not stereo collapsed down. There is no stereo field to speak of yet (no positional audio), so there was nothing to lose, and it sidesteps needing a "mono audio" accessibility toggle for this track specifically — there's no stereo image for such a toggle to collapse.

---

## Part XI — Accessibility

A deliberately-scoped real slice rather than every idea discussed, plus one hard rule that applies regardless of what's implemented yet.

### 11.1 What's implemented
- **Field of view slider** (Motion/Comfort) — 45–100°, replacing what was a fixed constant. Neither a wider nor a narrower FOV is universally more comfortable for motion/vestibular sensitivity (wider can worsen edge distortion for some, narrower can worsen tunnel-vision for others), so this is a slider a player tunes in whichever direction helps them, not a binary toggle guessing a direction for them.
- **Toggle-to-move** (Input flexibility) — an Accessibility setting that changes what a WASD press *means*: instead of the action reading as "down" only while the key is physically held, a press flips a per-action latch that stays on until pressed again. Movement no longer requires holding a key down for the whole duration of walking. Implemented as a genuine input-semantics change in `IsActionDown()`, not a visual/cosmetic toggle: `WM_KEYDOWN`'s key-repeat bit (bit 30 of `lParam`) is checked so holding the key doesn't rapidly flip the latch, and the latch is cleared on focus loss (`WM_KILLFOCUS`) and on switching the mode off, so a stale toggle can never leave the player walking without input. Latching a direction releases its opposite (forward/back, left/right) — with both latched they cancelled out and the next press un-latched the wrong one, which read as inverted controls — while perpendicular latches still combine for diagonals. Mouse-button bindings toggle the same way as keys. While the mode is on, a small arrow cross in the bottom-left corner lights each latched direction, so an active latch is never invisible.
- **High-contrast UI palette** (Vision) — pushes every panel/button/slider-track fill toward the luminance extremes (near-black backgrounds, strongly saturated hover/handle colors) instead of the subtle gray-shade steps used otherwise. Text was already white-on-dark in both modes, so only fill colors branch.

All three persist in the global settings file (7.2.2) like every other preference, with their own Reset to Default, in a dedicated Accessibility submenu reachable from Pause (and, once a title screen exists, from its Options too).

### 11.2 What's deliberately not implemented yet, and why
- **A "reduce flashing" toggle.** Nothing in this prototype flashes or strobes today (no particles, no damage vignette, no lightning) — a toggle controlling zero real effects would be exactly the kind of dead control this project has already pushed back on once (the old Audio submenu's "no backend yet" placeholder, Part X). The actual commitment is the hard rule in 11.3 below, which binds *future* work whether or not a toggle exists yet. The toggle gets built alongside whatever first effect would actually need one.
- **A colorblind-safe palette.** Nothing in the current UI conveys meaning through hue alone (no red/green status indicators, no color-only warnings) — there's nothing to remap yet. Worth building the moment a UI element starts encoding meaning in color rather than only decoration.
- **A UI scale slider.** Unlike the above, this is real, wanted future work — just architecturally bigger than it looks: every hit-rect (`PointInRect` calls throughout the menu click handlers, slider drag math) would need to move in lockstep with every visual size, or clicks would misalign the moment the slider left 100%. Deferred rather than shipped half-consistent.

### 11.3 Hard rule: no uncontrolled flashing or strobing, ever
Independent of whether a toggle exists to control it: no effect added to this game — weather, damage feedback, particles, screen shake, UI transitions, anything — may flash, strobe, or rapidly alternate brightness/color in a way the player doesn't control the timing of. This is a photosensitive-seizure-trigger concern, not a taste preference, and it binds every future milestone the same way the resource-discipline requirement (1.2) binds the build. Any effect that has a legitimate reason to pulse or flicker gets a player-facing toggle to disable it *before* it ships, not after.

---

## Part XII — Title Screen and Menu Structure

### 12.1 GameState vs. MenuScreen
Two separate pieces of state, not one conflated enum: `GameState` (`Title`/`InGame`) tracks whether a real game is running at all, while `MenuScreen` tracks which panel is on screen. The game starts in `GameState::Title` with an empty, unloaded `World` — no chunks generate and no physics tick (the existing "freeze simulation while any menu is open" rule, 5.3, already covers this for free, since every title-tree screen is a non-`None` `MenuScreen`) until New Game or Load Game actually hands control to the player via `EnterGameplay()`.

### 12.2 One options hub, two entry points
The six settings submenus (Look/Graphics/Display/Audio/Accessibility/Keybindings) are pure global-preference state (settings.cfg, 7.2.2) with no dependency on a loaded world, so they're reachable identically from Pause (mid-game) and from the title screen's own Options button, through one shared `MenuScreen::OptionsHub` rather than duplicating six menu entries in both places. `g_optionsReturnScreen` records which of the two opened it, so OptionsHub's Back (and Escape) return to the right place. Pause itself shrank accordingly, down to Resume / Options / Save / Load / Quit to Title / Quit.

### 12.3 New Game / Load Game slot picker
Both New Game and Load Game lead to the same slot-picker screen (`MenuScreen::SlotPicker`), distinguished only by `g_slotPickerMode`. Load Game on an empty slot is a no-op with a toast; New Game on an occupied slot needs a second click within a few seconds to confirm the overwrite (`g_confirmOverwriteSlot`/`g_confirmOverwriteTimer`) rather than silently destroying a world, without the larger scope of a full modal dialog system. A fresh New Game is saved to disk immediately (an empty-world save) so the slot stops reading as "empty" from that point on, rather than only existing once the player happens to trigger a save later.

### 12.4 Escape's hierarchy
Escape backs out exactly one level, and which level depends on which tree it's in: `SlotPicker → TitleMain`, `OptionsHub → g_optionsReturnScreen`, any settings submenu `→ OptionsHub`, and — only in the in-game tree — `Pause → (resume)` and gameplay `→ Pause`. The slot picker is shared by the title screen's Load/New Game and Pause's Load Game; `g_slotPickerReturnScreen` sends its Back row to whichever opened it (Escape from it in-game returns to Pause). `IsSettingsSubmenu()` is the single predicate both the click handlers' Back buttons and this Escape logic agree on, so the two can never disagree about which screens count as "a settings submenu" for routing purposes.

---

## Part XIII — Day Clock

One authoritative value, `g_dayTimeSeconds` (0 to `DAY_LENGTH_SECONDS = 3600`, one in-game day = one real hour, locked in), advancing only inside the exact same gate that already freezes physics and chunk generation while any menu is open (5.3) — so it is structurally impossible for the clock to run while paused, without needing a separate check. A fresh New Game starts it at 0 (dawn); Load Game restores whatever was saved (7.2.5); it is never derived from the real-world wall clock.

This is deliberately the *only* clock anything in this system reads. Part XIV's music is built entirely around not needing a second one — see 14.4.

---

## Part XIV — Day-Cycle Music

### 14.1 Why chunked and clock-anchored, not baked once
The music has to stay in agreement with the day clock (and eventually lighting) for as long as the game runs, and two independently-running clocks — a baked buffer's playback position vs. the game's simulated time — can only be kept aligned approximately, by periodic correction. So the track is generated in small chunks (`MUSIC_CHUNK_SAMPLES` = 0.25 s), continuously, and every chunk is computed starting from whatever `g_dayTimeSeconds` actually is. There is only ever one clock.

### 14.2 Form: one hour, six sections, looping into itself
| Section | Day time | Tempo | Chords | Melodic material | Shared lowpass |
|---|---|---|---|---|---|
| Dawn | 0:00–8:00 | none → soft 120 BPM pulse fading in over 3 min | Dm9 → G7sus4 → Em7 → A7sus4, 120 s each, 16 s crossfades | from 4:00: D4–E4–F4–A4 as 8ths, each note blooming (2.5 s attack / 4 s release), every 8 bars (every 6 in the last 90 s) | 380 → 720 Hz over 6 min |
| Morning | 8:00–20:00 | 122 BPM | same cycle, 4 bars each | main motif D4–F4–A4–C5–A4–F4–E4–G4 on a soft pulse wave (32 % width), fading in over 20 s | 750 → 1.6 kHz over 7 min |
| Midday | 20:00–35:00 | 124 BPM | same cycle, 6 bars each | main motif (28 % width) + countermelody A4–C5–E5–G5–E5–C5–A4–G4 | 1.5 → 3.1 kHz, hold, → 2.2 kHz |
| Afternoon | 35:00–47:00 | 122 BPM | same cycle, 4 bars each | transformed motif C5–A4–F4–D4–E4–G4–A4–F4, 8th/dotted-8th, every 3 bars (6 in the last 100 s) | 2.2 → 1.0 kHz |
| Dusk | 47:00–55:00 | pulse fades out over 3.5 min | same cycle, 2 min each | 2–3-note cells of the Afternoon motif every 16–24 s | 1.0 kHz → 480 Hz |
| Night | 55:00–60:00 | none | Dm9, Em7, A7sus4 (75 s each), open D3+A3 drone, then a 50 s bloom back into Dawn's Dm9 | single pure sines (D5/A4/F4), 7 s envelopes, 25–30 s apart | ~470 Hz, opens toward 620 Hz |

Every automation curve (layer levels, cutoff, resonance, duck depth) is a keyframe table whose value at 3600 s equals its value at 0, and the final Night segment crossfades into Dawn's opening chord, so the hour closes on itself.

### 14.3 Synthesis: additive and subtractive in tandem
- **Additive** — tones built as explicit sums of sine partials: pure sine (mid/high pads, night tones, air), a *soft triangle* from the triangle series' odd partials 1/3/5 at 1/n² (low pad, bass, Dawn motif, Dusk fragments), and a sine + soft-triangle blend (countermelody). Band-limited by construction. The 3rd and 5th partials are computed from the fundamental through the exact multiple-angle identities (sin 3x = 3s − 4s³, sin 5x = 5s − 20s³ + 16s⁵), so a three-partial tone costs one sine evaluation.
- **Subtractive** — spectrally rich sources shaped by filters: PolyBLEP band-limited saw (low pad, the bass's "soft saw") and pulse (the motifs), and white noise (the textured bed and air layer). One shared resonant lowpass (RBJ biquad) carries the day-long brightness arc over the whole tonal mix; the two saw layers also get their own fixed one-pole tone filters (900 Hz pad, 400 Hz bass) so they stay soft when the shared cutoff opens past 1.5 kHz; the noise bed has its own slowly moving lowpass and the air layer a bandpass, both bypassing the shared filter.
- **Voicing** — chord tones are folded into three pad registers (low 80–160 Hz, mid 175–350 Hz, high 390–720 Hz) plus a bass tone per chord, each register a fixed bank of one oscillator per distinct frequency. A tone shared by two chords is one oscillator whose gain is the sum of both chords' weights, so common tones sustain through a change and a crossfade never adds voices.
- **Pulse** — a pre-rendered soft thump (sine gliding 104 → 52 Hz, 6 ms raised-cosine attack, windowed to exactly zero within 0.45 s, shorter than any beat) on every beat, accented on 1 and 3. From Morning on, the bass ducks sidechain-style on each beat (raised-cosine dip and recovery), with slowly varying depth.

### 14.4 Music Intensity (Accessibility, 11.1)
A linear scalar (0–1, `g_musicIntensity`) applied to the rhythmic and melodic layers only — pulse, motifs, countermelody, Dusk fragments, night tones, and duck depth. At 1.0 the arrangement plays exactly as designed, already the maximum energy this track reaches; at 0.0 only the bed (pads, bass, noise, air) remains. It is a floor control, never a ceiling-breaker: resonance is capped (Q ≤ 1.2, `kMaxQ`) and every transition is smooth at every setting.

### 14.5 Determinism: what's stateless and what isn't
Chord weights, beat position, every note event (onset, pitch, micro-timing and length variation via a hash of the event's identity), every envelope and every oscillator phase is a pure function of wrapped day time. Pad and bass phases are computed from absolute time, and every such frequency × 3600 is an integer, so phases wrap from 3600 to 0 with no jump; notes take their phase from their own onset. The only state carried between chunks (`MusicState`) is the filter histories and a resume fade-in counter. `ResetMusicState` runs on every discontinuity (new game, load, resume), and a fresh state fades in over 1.5 s, so resuming never clicks. Verified offline: rendering 10 s as one call vs. forty quarter-second chunks differs by at most 1 LSB, and the 3600 → 0 seam is smoother than the signal around it.

### 14.6 Playback: queue, pause, and the title screen
`StartMusicPlayback()` (New Game, Load, Resume, quick-load during play) re-anchors to the current `g_dayTimeSeconds`, resets state, primes 4 chunks (1 s) and starts the voice; `StopMusicPlayback()` (any menu opening, focus loss, Quit to Title) stops and flushes. Pausing goes fully silent — silence reads as "in-game time stopped." The title screen is silent because nothing starts playback until a game begins. PCM lives in a fixed pool of buffers reused cyclically (one more slot than the 16-chunk lookahead, so the slot being rewritten is always older than anything XAudio2 still has queued); after a flush, no slot is rewritten until `BuffersQueued` actually reaches zero. `RefillMusicQueueIfNeeded()` adds at most one chunk per frame, and only while no menu is open. Quick-loading from a menu leaves the music stopped; Resume restarts it at the loaded time.

### 14.7 Cost and output level
Generation runs on the main thread, so it is budgeted like everything else: median ~1.3 ms per 0.25 s chunk (about 190× realtime), versus ~6.8 ms average and ~30 ms worst for the previous generator, which also primed 16 chunks at once on every resume. No libm calls in the sample loop (polynomial sine, truncation-based floor, PolyBLEP); slow parameters are evaluated once per 64-sample control block and interpolated; silent layers and inactive oscillators are skipped entirely. Output scale (`kOutputScale` = 0.78) comes from a full-hour offline render at intensity 1.0: peak ~0.85 FS, loudness close to the previous track; the final clamp is only a backstop and never engages.

### 14.8 Where the spec was interpreted or overruled
- *"Continuous 8th notes, one full cycle every 2 bars"* (8 pitches) — each pitch sounds as two 8ths, which satisfies both clauses.
- *Countermelody "one cycle every 4 bars, legato 8th notes"* — one pitch per half note, legato, with a soft 8th-note re-articulation.
- *Night opens the filter to 620 Hz in its last 2 minutes, but Dawn starts at 380 Hz* — the seam wins (no sudden events): it opens to 620 Hz, then glides back to 380 Hz through the final 50 s crossfade.
- *Small gaps between sections' filter ranges* (Dawn ends at 720, Morning starts at 750; Morning holds 1.6 kHz, Midday starts at 1.5 kHz) — bridged smoothly; Midday's sweep starts from 1.6 kHz.
- *Midday "occasional C♮ for Mixolydian color"* — C♮ is already in D Dorian (Mixolydian's characteristic tone would be F♯, which would clash with the pads' F), so nothing was added. Open question for the composer.
- *Night pulse "none (or –28 dB residual)"* — none.
- *Bass register ranges* are approximate: the Em7 bass is E1 (41.2 Hz), just under Dawn's stated 45 Hz floor.
- *Resonance values* (0.20–0.33) map to Q = 0.707 + 1.2·r, capped at 1.2.
- *Tempo changes* step at section boundaries (≤ 1.6 %); every section begins on a whole bar, so the chord grid stays aligned.
- *dB values in the spec* are relative; the mix was set by measurement — melodic layers sit roughly 3–8 dB under the bed in the pulsed sections and further under it in the sparse ones.
- *Motif hand-offs* — the Morning motif fades in over 20 s at 8:00, and at 35:00 Midday's motif fades out over 15 s while Afternoon's fades in, so no line appears or vanishes at full level.

---

## Part XV — Build

Eight source files (`main.cpp world.cpp render.cpp audio.cpp persist.cpp game.cpp textures.cpp music_synth.cpp`), one compiler invocation, no project file strictly needed (the checked-in `.vcxproj`/`.vcxproj.filters` list them all for Visual Studio):

```
cl main.cpp world.cpp render.cpp audio.cpp persist.cpp game.cpp textures.cpp music_synth.cpp /link d3d11.lib dxgi.lib d3dcompiler.lib gdiplus.lib gdi32.lib user32.lib shell32.lib ole32.lib uuid.lib xaudio2.lib /SUBSYSTEM:WINDOWS
```

or with MinGW-w64 (used during development to compile-check this prototype on a non-Windows host, since it ships full D3D11/DXGI/D3DCompiler/GDI+/XAudio2 headers and import libraries):

```
x86_64-w64-mingw32-g++ -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE \
  main.cpp world.cpp render.cpp audio.cpp persist.cpp game.cpp textures.cpp music_synth.cpp -o voxistics.exe \
  -ld3d11 -ldxgi -ld3dcompiler -lgdiplus -lgdi32 -luser32 -lole32 -lshell32 -luuid -lxaudio2_8 -static-libgcc -static-libstdc++
```

Each `.cpp` above owns one subsystem and includes only the headers it needs (`common.h` for shared math/block-table types; `world.h` for the simulation model; `render.h` for D3D11 state and chunk meshing; `audio.h` for XAudio2 playback; `persist.h` for settings/save-load; `game.h` for the menu state machine, input dispatch, and the UI render pass). `textures.cpp` and `music_synth.cpp` stay dependency-free of the rest of the project by design. `textures.cpp` exposes `extern "C"` entry points to `render.cpp` by convention; `music_synth.cpp` has its own header (`music_synth.h`) holding `MusicState` and the generator's entry points, and `audio.cpp` `static_assert`s that the music's day length matches `DAY_LENGTH_SECONDS`.

(`-lxaudio2_8` is MinGW's import-lib name for the same XAudio2 2.8 API that the Windows SDK's `xaudio2.lib` provides — a MinGW-only naming difference, same idea as `-municode` above it.)

Default controls (all fully remappable to any keyboard key or the left/right/middle mouse button via Pause → Keybindings — click a row, then press the new input; Esc cancels a rebind in progress, except on the Pause Menu row, where Esc binds Escape): WASD to move, mouse to look (click once to capture the cursor), Space to jump, left-click to break the targeted block, right-click to place the selected hotbar block, number keys 1–9 to select a hotbar block (fixed, not remappable in this pass), F5 to save, F9 to load, Esc to open/close the Pause menu or back out one level from any of its submenus (Look Settings, Graphics, Display, Audio, Keybindings, each with its own Reset to Default), all clickable with the freed cursor.
