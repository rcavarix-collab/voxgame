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
- **Player physics never runs on ground that doesn't exist yet.** Ungenerated space reads as air, so until the player's own column is resident they're held exactly in place (no gravity, no movement), and walking into a not-yet-resident column is refused like walking into a wall. A new game starts the player standing on the surface (terrain height is a pure function of x/z, so no chunk is needed to know it). The world's bottom layer (y = 0) can't be broken — there is nothing beneath it — and should a player ever end up below the world anyway, they're put back on the highest solid block of their column. As a backstop, a player whose box overlaps solid blocks — terrain appearing around an edge, a block falling onto them — is lifted one block per tick until free, and a placement that would overlap the player's own box is refused.
- Terrain is generated **one ring beyond the view radius**. A chunk's mesh reads all 8 neighbouring columns (face culling across shared faces, ambient occlusion across edges and corners — 4.2), so a chunk is only meshed once its whole 3×3 neighbourhood is resident; the extra ring is what lets the outermost visible ring be meshed, and waiting means each chunk is built once rather than once per neighbour arrival.
- Every chunk carries a **`modified`** flag: set by any edit, fall or load, i.e. whenever it stops matching what the generator produces. Columns leaving the radius (past a small hysteresis margin, so a player oscillating at the boundary doesn't thrash) are evicted: **unmodified chunks are simply freed** — the generator rebuilds them bit-for-bit on return (2.5) — and only modified chunks move to `g_evictedChunks` (minus GPU buffers). When a column becomes resident again, `GenerateColumn` generates its terrain and overlays any modified chunks held for it. So memory spent on places the player has left scales with what they *changed* there, not with distance walked, and the draw loop and rebuild queue stay bounded by the loaded area (Part 1.3). This is in-memory only, not disk-backed — a deliberate scope decision until the world's scale is settled. `SaveGame` walks both maps for modified chunks (7.2).

### 2.5 World generators
Because unmodified terrain is regenerated rather than stored — on eviction and on load — a world is only reproducible with the exact generator that made it. Each world therefore records its generator (`WorldGenParams`: type, version, seed) in its save, and a generator's output must be a pure function of (params, coordinates). Changing a generator's output means adding a new *version* alongside the old one, which existing worlds keep using; a save naming a generator or version this build doesn't have is refused with a clear reason rather than loaded onto the wrong terrain. Two exist: `hills` v1 (the original sin/cos terrain, which every pre-v5 save is tagged with) and `flat` v1 (surface at y = 12). New worlds currently use `flat` for testing (`DefaultNewWorldGen`, world.cpp). Every world gets a seed now even though neither generator reads one yet, so a seeded noise generator needs no format change.

---

## Part III — Block Model

### 3.1 Identity: name-based, not position-based
`BlockID` (an enum) is a *runtime* convenience only. The actual identity written to disk is the block's **string name** (`g_blocks[id].name`). This single decision is what allows the block list to grow indefinitely during development — adding, removing, or reordering enum entries never corrupts an existing save, because loading remaps saved names onto whatever the current build's names are, substituting a safe "unknown" (air) for anything genuinely removed, with a logged warning rather than a silent misread.

### 3.2 The block registry
`blocks.h` holds one row per block type in `g_blocks[BLOCK_COUNT]`, the single source of truth every system reads — meshing, gravity, picking, the hotbar (`g_placeableList` is derived from the `placeable` flag), the save format's name table and the texture builder:
```
name         — identity on disk (3.1)
solid        — collision, raycast hits, hides neighbouring faces
foundational — never falls, always supports (Part V)
placeable    — appears on the hotbar
orientable   — stores a facing; its `front` texture goes on that side
hasData      — may carry a per-block data record
texAll / texTop / texBottom / texSide / texFront — texture names (4.3)
```
Adding a block is one enum entry plus one row. No virtual dispatch, no per-block class hierarchy — virtual calls inside the meshing and simulation inner loops would violate the "no virtual dispatch in hot paths" rule.

**Per-block state.** Every chunk stores a state byte per cell alongside the block ID (and saves it). The low 3 bits are a facing (`BlockFace`) for orientable blocks — set on placement so the front faces the player; the other 5 bits are reserved (a machine's on/off, a slab's half...) so they can be claimed without a storage or format change. It travels with a block when it falls.

**Per-block data.** A sparse map per chunk (`Chunk::data`, keyed by cell, null for the vast majority of chunks) holds variable-length records for blocks that need more than a byte — a chest's contents, a machine's buffers (the item-handler interface of Part VI will live here). A record is dropped automatically when its cell's block changes, and is saved with its chunk. Nothing writes one yet; the storage and save path exist so machines don't need a format change.

### 3.3 Prototype block roster
| Block | Foundational | Orientable | Purpose |
|---|---|---|---|
| Air | — | — | Absence of a block |
| Foundation | yes | no | Never falls; the base layer of any build |
| Stone | no | no | Terrain, subject to gravity |
| Dirt | no | no | Terrain, subject to gravity |
| Wood | no | no | Building material |
| Chest | yes | yes | Storage container (item-handler interface, Part VI) |
| Machine | yes | yes | Placeholder processing block |
| Stone slab, wood ramp, tube, stone pyramid / half pyramid / funnel / half funnel | yes (for testing) | per shape | The Prismative.cpp primitives (4.4) |
| Music block, timestream block | no | no | Light up with the music / where The Line passes (18.1) |
| Essence attractor | yes | no | Placeholder player-built node on the essence map (Part XIX) |

---

## Part IV — Rendering

### 4.1 Visual target
Blocky, saturated, clear silhouettes. Procedurally generated textures (patterns drawn in code at startup), not imported art — consistent with the "no external asset files" constraint and directly reusing the *idea* (not the code) behind cc_2_2_2.cpp's 145 generator functions.

### 4.2 The central performance decision: per-chunk merged meshing
Every exposed face of every cube-shaped block in a chunk is combined into one vertex/index buffer pair, drawn with a single `DrawIndexed` call per chunk. This converts draw cost from **O(blocks)** to **O(loaded chunks)** — the single highest-leverage decision in the whole render design, and the direct fix for the original Prismative.cpp flaw (one `UpdateSubresource` + `DrawIndexed` pair *per solid cell*, ~26,000 draw calls/frame at modest fill).

**Face culling:** a face is only emitted if the neighboring cell (in world space, across chunk boundaries where relevant) is not solid. This requires the mesher to query `World::Solid()`, not just the local chunk array, at chunk edges.

**Mesher** (`mesher.cpp`, no D3D, tested natively): each rebuild first copies the solidity of the chunk plus a one-cell shell of its 26 neighbours into a padded 18³ grid, so every culling and ambient-occlusion test is a plain array read — no per-face hash lookups across chunk borders. Vertices are packed to **8 bytes** (chunk-local position as bytes; a texture-array layer; 5-bit u/v, 2-bit AO and 3-bit face in one `uint16`), with the chunk's world origin supplied per draw from a small constant buffer; indices are **16-bit** (the worst case, a 3D checkerboard, is 49,152 vertices). That's a 2.5× smaller vertex buffer and half the index buffer compared with the previous float format. u/v reach 16 so merged (greedy) quads can later tile a texture across several blocks with no format change.

(Positions and u/v are stored in 1/8-block fixed point, so shaped blocks (4.4) fit the same format.)

**Lighting inputs, baked at mesh time:** each vertex carries its face's shade class (which gives the shader its normal and a light per-direction bias) and a per-corner **ambient occlusion** level (the classic voxel AO: the two edge neighbours and the diagonal of the open cell beside each corner); quads are split along their brighter diagonal to avoid the AO anisotropy seam. The lighting itself is a handful of per-pixel multiplies (4.9).

**Mesh rebuild policy:** only when a chunk is marked `dirty` (an edit occurred inside it, or a neighbor's edit could have exposed/hidden one of its boundary faces). Never rebuilt speculatively, never rebuilt every frame. `World::dirtyChunks` holds exactly the resident chunks whose flag is set (chunks enter and leave `World::chunks` only through `GetOrCreateChunk`/`AdoptChunk`/`TakeChunk`/`ClearChunks`, which keep the two in step), so the rebuilder looks only at chunks that need work — nothing at all while the world is static — and picks the ones **nearest the camera** first. New ground, a load or a render-distance change fills in outward from the player rather than in hash-map order.

**Per-frame cost controls, added after real play surfaced measurable stutter:**
- **Padded neighbour grid** (above) — superseded the older per-check fast path: no neighbour check pays a hash lookup at all.
- **Capped mesh rebuilds per frame** (`MAX_CHUNK_REBUILDS_PER_FRAME = 6`, render.cpp), nearest the camera first, and only for chunks whose 8 neighbouring columns are resident (2.4) — entering unexplored terrain can mark several newly-generated columns dirty in the same tick; rebuilding all of them (each a full mesh pass plus two synchronous GPU `CreateBuffer` calls) in one frame is exactly the kind of single-frame spike this cap smooths across several frames instead, mirroring Part V's per-tick work-queue philosophy.
- **View-frustum culling** (`ExtractFrustum`/`FrustumIntersectsAABB`, render.cpp) — the six view-frustum planes are extracted directly from the combined view-projection matrix each frame; a chunk whose AABB doesn't intersect it is skipped in the draw loop entirely. Bounds per-frame draw cost by what the camera can actually see rather than by how much of the world happens to be currently loaded.
- **Chunk eviction** (Section 2.4) — the companion fix to the above: without it, "currently loaded" itself only ever grows, so even a perfectly culled draw loop and a capped rebuild budget would still be iterating (if not drawing or meshing) an ever-larger set every frame. Together, loaded-set size and per-frame draw/mesh work both now track the player's current position rather than their lifetime path through the world.

### 4.3 Block textures — a texture array, authored or procedural
Every distinct block face texture is one layer of a `Texture2DArray` (64×64, full mip chain), and the mesher stamps each face with its layer from `g_blockFaceLayer[block][facing][face]` — resolved once at load, never per vertex. (This replaced a single atlas image, which is what made per-face textures, orientation and mipmaps practical: with one texture per layer there's no neighbouring tile to bleed into, so addressing can wrap and mips can't smear tiles together.) The sampler is point for magnification — crisp pixel art up close — with linear blending between mip levels so distant blocks don't shimmer. The array is an **sRGB** texture, so the shader reads linear colour and the hardware does the conversion for free; mips are averaged in linear light too (a gamma-space average darkens any contrasty texture with distance — a black/white checker would fade to 128 instead of the correct 188).

`blocktex.cpp` builds the set (pure C++, tested natively): for each (block, facing, face) it picks a texture name from the registry (3.2: front > side > all; top/bottom > all), or — if an authored `block` entry exists for that block — from that entry, which replaces the registry's mapping outright so an authored block never mixes in a placeholder face. A name resolves to authored `.vtex` art if present (scaled up by whole pixels, so it stays exactly as drawn), else a procedural placeholder of that name (`foundation`, `stone`, `dirt`, `wood`, `chest`, `chest_front`, `machine`, `machine_front`, `tube`, `music_block`, `timestream_block`, `essence_attractor`, `glass`, `crystal`), else the block's own placeholder, else a magenta checker. Hotbar icons come from the same set (the face a placed block shows the player).

**Authored art:** plain-text `.vtex` files in `assets/textures/` (a palette plus a character grid per texture, plus `block` entries mapping textures onto `all`/`top`/`bottom`/`side`/`front`), specified in `assets/textures/TEXTURE_BRIEF.md` — which doubles as the prompt handed to the art conversation, so the format the art is written in and the format the loader reads are the same document. The folder is found beside the working directory or up to three folders above the exe. A malformed entry is skipped (everything else still loads) and every problem — parse errors with file:line, unknown blocks, missing or unused textures — is written to `assets/textures/_errors.txt`, with a toast at startup saying how many; the file is deleted again once there are none.

The natural placeholders (stone, dirt, wood) are drawn as 16×16 pixel art — the chunky scale the art brief asks for — from the engine's own integer hash, never `rand()`: MSVC's `rand()` is a weak generator whose consecutive values correlate, which lined "random" specks up into diagonal streaks across the landscape on Windows builds (and never on the Linux previews). Every pattern wraps at 16, so tiles meet without a seam (tested: no step across a tile's edge larger than the steps already inside it); dirt is warm browns only, wood is staggered planks with grain running along them.

**Block library and hotbar.** The hotbar is ten slots (keys 1–9 and 0, the wheel cycles them), filled by the player from the **block library** (E, rebindable): a grid of every placeable block's icon, above the hotbar. A click on a block puts it in the selected slot and closes the library; pressing a block and dragging it onto any hotbar slot puts it there and keeps the library open for more; clicking a hotbar slot while the library is open picks which slot a click fills. The hotbar's contents are saved in settings by block name, so registry changes can't scramble them, and an unknown or no-longer-placeable name keeps that slot's default. Layout and the click-or-drag gesture are plain functions (`library.h`), tested natively; the grid scrolls by row with the wheel once blocks outgrow the window.

**Natural materials** (snow, sand, sandstone, cracked earth, clay, basalt, magma rock, log, moss) have authored art in `assets/textures/natural.vtex`, generated by `tools/natural_textures.py` from the palettes of the first art batch: tileable value noise and crack networks computed on a 16×16 torus, details scattered by a seeded hash — the same three rules the brief now asks of hand-made art (no regular dither, no details in fixed spots, lines wrap). Tested: the file loads with no errors or warnings, covers every natural block's faces, and every texture but the log's cut end wraps without a seam.

Seam rule, from a visible 1px line on every block: procedural tiles are plotted pixel-exact, each clipped to its own square (GDI+ pens once painted a strip into neighbouring tiles), and face UVs span exactly 0..1 of a layer (a half-texel inset under point sampling once drew every tile's edge texels at half width).

### 4.4 Non-cube shapes — baked into the chunk mesh
The primitives from the original Prismative.cpp prototype are back as registry shapes (`BlockShape`, `shapes.cpp`): **slab** (upper or lower half, by where on a face you click), **ramp** (rises away from the player who places it), **tube** (a quarter-block bar running out from the face you click, along any axis), **pyramid**, **half pyramid**, **funnel** (an upside-down pyramid) and **half funnel**. Each is authored once in a canonical orientation, in 1/8-block units, and rotated by the block's state byte (3.2). The mesher bakes their polygons straight into the chunk mesh like cube faces — no per-instance draw calls, which is what sank the earlier per-instance pipe blocks (one draw call per shape reproduced the exact per-object cost problem that 4.2 solved for cubes).

To make that possible the packed vertex (4.2) stores positions and texture coordinates in 1/8-block fixed point; it's still 8 bytes. A polygon lying flat on the cell boundary (a slab's bottom, a ramp's back wall, a tube's end) is hidden by a full-cube neighbour; everything else is always drawn, and only full cubes hide neighbouring faces or darken AO — a slab beside a cube leaves the cube's side visible. Sloped faces get their own shade classes (up-facing slopes slightly darker than tops, down-facing ones darker still); shapes take no AO.

**Collision** uses per-shape boxes rather than the whole cell, so a slab is half height, a tube is only as thick as it looks, and a ramp is two half steps; the player now steps up anything up to half a block (never a full block), which is what makes slabs and ramps walkable. Block picking still treats a shaped block as its full cell. The test blocks are foundational for now, so a test build doesn't collapse while you look at it, and the hotbar scrolls with the mouse wheel since the roster outgrew keys 1–9.

**Hotbar icons** (`icons.cpp`) are rendered at load from each block's real mesh by a small software rasterizer — a three-quarter view with the world shader's shading — so shapes, fronts and textures all read in the hotbar; the whole roster costs a fraction of a millisecond at startup.

### 4.5 Block picking — GPU-exact, not CPU-approximate
Two options were compared for "what block is the player looking at":
- **CPU raycast with fixed-step marching** (the original Prismative.cpp approach, `t += 0.1f`): rejected — can skip thin geometry at shallow angles, and face normals are inferred after the fact by comparing consecutive sampled cells, which is wrong at cell-corner crossings.
- **Amanatides–Woo exact voxel DDA traversal** (1987): the algorithm actually implemented. Steps exactly one voxel boundary at a time using only comparisons and one addition per step; the crossed face's normal falls directly out of which axis was stepped, not inferred. This is precise, well-understood, and cheap.
- **GPU ID-buffer readback** (discussed as a theoretically superior alternative once shapes get complex): render block-ID+face-index as color into a tiny offscreen target and read back the pixel under the crosshair. Correct for arbitrary non-cube geometry (a raycast against a ramp's *actual surface*, not its bounding cube) since it reuses the exact geometry already rasterized. **Not implemented in the prototype** — flagged as the eventual right answer once non-cube shapes exist again, but Amanatides–Woo is sufficient and simpler while every solid shape in the current roster is a full cube.

### 4.6 2D/3D split
One D3D11 device, three passes, never two graphics APIs at runtime:
1. **Sky pass** — a large box centered on the camera each frame, drawn first with depth test/write both off (reusing the UI pass's depth-disabled state) so the opaque world pass always overdraws it regardless of the box's actual size. Its view matrix drops the eye position entirely (rotation only), the standard skybox trick for making it rotate with the camera's look direction but never translate with the player's movement. Everything in it — sky gradient, sunset band, sun, moon, stars, clouds — is computed per pixel from the view direction (4.9), so it has no textures and its cost is fixed by the screen, not the scene.
2. **World pass** — perspective projection, depth test on, chunk meshes plus individually-drawn special shapes.
3. **UI pass** — its own shader/input-layout/cbuffer/blend-state/depth-state, drawn last each frame. Vertex positions are supplied already in pixel space and mapped straight to NDC in the vertex shader (`x/screenW*2-1`, `1-y/screenH*2`) — an orthographic projection in substance, without needing a matrix for it. Depth test/write off, alpha blending on (standard src-alpha/inv-src-alpha), so panels and text composite correctly over the 3D scene. Every UI vertex carries a color tint alongside its UV, so the same textured-quad pipeline draws plain glyphs, tinted panels/borders, and full-color icons.

A small font-glyph atlas (ASCII 32–126 in monospace grids, plus a solid-white strip for untextured tinted rectangles) is generated by GDI+ at load time the same way the block atlas is. Text is drawn **1:1** — one texel per screen pixel, point-sampled, snapped to whole pixels — so it stays crisp instead of being resampled from one master size; to still offer several sizes, the glyph set is baked once per size ("band", cell heights 14/18/22/28/34/44 px) and a requested scale picks the nearest band. Glyphs step by the font's own monospace advance rather than a full padded cell, so letters sit at normal text spacing and every menu label fits its row. Rectangles sample the centre of the white strip, so panels and the flat menu dim are uniform to their edges (stretching a glyph cell with a transparent rim once gave every panel a wide faded border and the dim a vignette). Hotbar icons are 32px — exactly half a tile — so point sampling stays even. The crosshair, hotbar, every menu and its sliders are all built from it. Hotbar item icons are drawn by sampling the *same* block atlas the world pass uses, rather than generating separate icon art.

GDI+ is used exclusively at load time to *generate* textures into bitmaps that get uploaded once to GPU textures; it never touches the frame loop. This was an explicit decision against mixing GDI+ and Direct3D rendering live, which would fight over the swap chain surface.

### 4.6.1 Window size, fullscreen and DPI
The backbuffer follows the window: the window is resizable and maximisable, `WM_SIZE` resizes the swap chain and depth buffer (`ResizeRenderTargets`), and the projection's aspect ratio and every UI layout read the live `g_screenW`/`g_screenH` (a minimised 0×0 window keeps the old size). The window can't be made smaller than 960×680 (below that the taller menus and the hotbar would run off-screen). F11 or Display Settings toggles **borderless fullscreen** on the window's current monitor (no exclusive mode, so alt-tab and other monitors behave normally; DXGI's own Alt+Enter exclusive fullscreen is disabled so the two can't fight); the choice is saved. The process declares per-monitor DPI awareness, so Windows never bitmap-stretches the window on a scaled display — the UI stays pixel-exact at its native size (a UI-scale option is the planned follow-up for high-DPI screens).

### 4.8 Lighting effects: sun shadows, outlines, screen-space AO
Three independently toggleable effects (Graphics Settings, saved; outlines and SSAO off by default, shadows on — since 4.9 they carry the lighting: without them the sun reaches under every overhang. Bloom, 4.10, is the fourth and is on), each built so it costs nothing while off and, while on, scales with screen pixels or with what changed — never with world size:
- **Sun shadows.** An orthographic 2048² depth map from the sun (`ShadowLightViewProj`, sky.h) over ±48–112 blocks around the player (wider with render distance), with the centre snapped to whole texels so the shadow edges don't crawl as the player moves. The sun crosses the sky in 50 minutes, so the map is re-rendered only when it's stale — the sun moved ~¼°, the player moved a quarter of the covered area, or chunk meshes changed within the area it covers (far-off chunks streaming in don't count) — and most frames just reuse it (the profiler counts re-renders). The world pixel shader tests each pixel against it with 4 taps of hardware PCF (comparison sampler, border = lit); a normal offset of just over one texel plus a slope-scaled raster bias keep surfaces from shadowing themselves. Shadowed surfaces lose the direct sun and keep the sky's ambient light (4.9), so a shadow is blue-grey at noon and fades out with the sunlight at dusk; the shadow also fades out toward the map's edge rather than ending at a line.
- **Edge outlines** and **screen-space AO** share one post pass over an off-screen copy of the scene and its depth buffer: outlines darken where the depth Laplacian spikes (zero across any flat surface however steeply it's seen, large at silhouettes and block edges); SSAO takes 12 depth samples around each pixel, comparing each to the depth the local surface plane predicts there, so flat ground at a grazing angle doesn't occlude itself. It complements the baked per-vertex AO (4.2), which already darkens block corners. With both off the scene draws straight to the backbuffer — the post path doesn't run at all.

Every shader is syntax- and type-checked off-Windows with `tools/check_shaders.py` (glslangValidator's HLSL front end). At startup a shadow or post shader that fails to compile on a given driver only disables that effect, and the world shader falls back to its pre-shadow variant (`NO_SHADOWS`) rather than failing to start — but never silently: the compiler's output goes to `shader_errors.txt` in the working directory (removed again once everything compiles), a startup toast points to it, and the Graphics menu shows the effect as UNAVAILABLE instead of a toggle that does nothing.

### 4.9 Light and atmosphere — faked, but in linear light
The goal is a convincing day without paying for real light transport: no light propagation through the world, no per-block light data, no extra passes. Everything is a few per-pixel multiplies driven by one small per-frame constant buffer (`FrameCB`, filled from `ComputeAtmosphere` in sky.h, which is pure and tested), shared by the sky and world shaders through a common HLSL prelude (`g_atmosphereSrc`).
- **Linear light, filmic finish.** Textures are read as linear (sRGB view), lighting adds up in linear light, and the result goes through an ACES-style tonemap and display gamma. That's what lets the sun be genuinely bright (a sunlit face is ~3× its shaded side) without blowing out to flat white, and what keeps colours rich in shadow.
- **Sun and moon.** Direct light by the face's facing, white-gold high in the sky and orange near the horizon; moonlight is faint and blue and only exists while the moon is up at night. The sun's falloff is softened (√(N·L), a faked wrap) so a low sun still lights flat ground enough for its long shadows to read, and direct sun arrives within a minute or two of sunrise and ends exactly at sunset. Its path peaks ~60° up, tilted south, so noon shadows stretch out from under things instead of hiding beneath them.
- **Hemisphere ambient.** Instead of a fixed brightness per face, faces are lit by the sky from above (blue by day, deep blue at night) and a dim warm bounce from below, blended by how much the face points up, times the baked AO. Shadows (4.8) remove only the sun, so shaded areas take on the sky's colour the way real shade does.
- **Sky.** A horizon-to-zenith gradient, a sunrise/sunset band that wraps the horizon and is strongest toward the sun, a haze around the sun, a sun disc and the moon, the star field (Part XVIII), and **clouds** — thin, wispy high-altitude streaks (cirrus) rather than heavy puffs: value noise on a high, far-off plane, stretched along the wind, domain-warped into wisps and drifting with the game clock. They're translucent — at most ~55 % opaque by day and ~25 % at night, so the stars and moon still show through — and lit right through like ice cloud: bright, glowing toward the sun, catching the sunset's colour.
- **Fog into the sky.** Distant terrain fades into exactly the sky colour behind it (the shared `SkyColor` function), from half the load radius out to its edge — so the limit of the loaded world disappears into the horizon instead of ending at a wall, and render distance sets the fog automatically.
- **Exposure.** A fixed, time-of-day exposure stands in for eye adaptation: it lifts only real night (so nights stay playable and moonlit) and leaves twilight's colour alone.
- **Glow.** Reactive blocks (blocks.h `BlockGlow`) add emitted light on top of all of this, and the world and sky shaders write "how much this pixel glows" into the scene's alpha channel for the bloom pass.

All tuning was checked with an offline CPU mirror of these shaders over a real generated world (dawn, noon, sunset, night) before it went in.

### 4.10 Glow (bloom)
Things that give off light should look like they do, so glowing blocks and the sun get a soft halo (and glowing blocks also really light their surroundings, 4.12). The world and sky shaders already know exactly what glows — a reactive block's glow level, the sun's disc and a softer ring around it, with cloud in front subtracted — and write it into the scene target's alpha. Bloom uses that mask rather than a brightness threshold, so only real light sources bloom and a sunlit white wall never smears.

The pass rides on the post pass (4.8): the scene's `rgb × alpha` is shrunk to **quarter resolution** (four bilinear taps, a 4×4 box), then blurred with a separable 9-tap Gaussian (five bilinear fetches per pass) three times, each pass twice as wide as the last — a bright core with a long, soft tail. The targets are small float textures (R11G11B10), so dim halos don't band. The post pass screen-blends the result over the image, which brightens without ever clipping to flat white. Seven quarter-resolution draws and one full-screen composite: a fixed cost set by the window size, whatever the scene holds. It's on by default (Graphics Settings → Glow) and, like the other effects, a failed shader compile only switches it off; with every effect off, the post path doesn't run at all.

### 4.11 See-through blocks (glass, crystal)
Registry blocks can be **translucent** (blocks.h). They're full cubes for collision and picking, but for rendering:
- **Culling.** The mesher's padded grid (4.2) now records what fills each cell: an opaque cube hides any face beside it and darkens AO; a translucent cube hides only faces of its *own kind* (a wall of glass has no inner walls) and never darkens AO; so stone behind glass keeps its face, and glass touching stone loses the hidden one.
- **One buffer, two ranges.** The mesher writes all opaque triangles first and the see-through ones after, and each chunk remembers where the split is. The opaque pass and the shadow map draw only the first range (so glass casts no shadow); a chunk with no glass costs nothing extra.
- **The blended pass.** After everything opaque, chunks holding glass are sorted far to near (a handful, not the world) and their second range is drawn with alpha blending, depth tested but not written, back faces culled (every cube face is wound clockwise from outside — tested — so a glass cube shows one layer, not two). The blend leaves the destination's alpha alone, so a glowing block seen through glass still blooms (4.10).
- **Faked optics.** The same world shader, told it's drawing glass, lets the world behind show through by the texture's alpha, turns toward a mirror of the sky at grazing angles (Schlick's Fresnel, reusing the sky colour function of 4.9 — the "reflection" is the sky model, not a second render of the scene) and becomes more opaque there, and adds a hard sun glint unless the glass is in shadow. No refraction, no extra render targets.
- **Art.** `.vtex` palettes take an optional alpha (`rrggbbaa`, see the texture brief); the placeholders are a mostly clear pale glass with a firmer frame, and a cloudier violet crystal with facet lines. Hotbar icons show them see-through (but never invisible).

### 4.12 Light from glowing blocks, with shadows
Bloom (4.10) makes a glowing block *look* bright, but on its own it's a screen-space halo that spills over walls — light with no shadows. So glowing blocks also light the world around them, faked cheaply with a **light grid**: a 64×64×64 cube of cells around the player (one per block, two channels), built on the CPU by `glowlight.cpp` (pure, tested natively) and uploaded as a small 3D texture.
- **Shadows by line of sight.** Each glowing block lights every open cell within 8 blocks with a smooth (1 − d/r)² falloff, but only cells it can see: a line traced from the block to the cell (the same exact voxel stepping as block picking, 4.5) stops at the first opaque cube. So walls, pillars and floors throw real shadows from it. Overlapping lights add. Glass lets light through.
- **Soft edges for free.** The world shader samples the grid once per pixel, at the centre of the open cell the face looks into, with hardware trilinear filtering — which turns one-block steps into soft light and soft shadow edges.
- **Two channels, animated for free.** Music blocks light the red channel and timestream blocks the green one; the shader scales them each frame by the music playing now and by how near The Line passes (its glow comes and goes as the line sweeps), so pulsing light costs nothing on the CPU.
- **Cost.** Rebuilt only when the player crosses into another chunk (the grid is chunk-aligned, the player always at least a chunk from its faces) or a mesh rebuild touches a chunk holding a light or within a light's reach — never per frame, and a field of hundreds of lights keeps the 128 nearest. A rebuild is a scan of the 64 chunks it covers plus a few thousand short traces per light, then a 512 KB upload. With no lights nearby the shader skips the lookup entirely.

### 4.13 Surface detail: 32-pixel materials with normal, shine and glow maps
The look the game settles on, chosen by eye from a side-by-side of 16 flat, 16, 32 and 64 pixels with maps: **32 texels per block, lit per pixel** — its own thing rather than "Minecraft with better textures", while still reading as a cube world (at 64 the surfaces started to look like photographs glued onto boxes). The texture array stores 64×64 layers, so 32-px art is scaled up by exactly 2× and stays crisp, and the density can be dialled later with no engine change. The materials are *generated* (`tools/natural_textures.py`: each material a set of continuous fields in tile units, sampled at any resolution — colours quantised to short ramps from the art batches' palettes so they stay painterly).
- **Three maps per texture.** A `.vtex` texture may follow its pixels with `height` (0–9 then a–z), `shine` and `glow` (0–9) grids. The builder turns height into a tangent-space normal map by wrapping central differences (tiles repeat, so their slopes do), softening heights drawn at 32/64 px first so gentle slopes don't light up as contour terraces (chunky 16-px art keeps crisp pixel bevels), and stores normal-xy/shine/glow in a second texture array beside the colours, with mips that average normals as vectors.
- **Normal mapping without tangents.** Every face and shape gets its surface frame in the pixel shader from screen-space derivatives of position and texture coordinates — no tangent data in the 8-byte vertex. Sun, moon, sky ambient and the glow grid's light all see the bumped normal; the face's own normal still decides whether the sun reaches it at all, so bumps never light a side turned away.
- **Shine** adds a sun glint (shadowed like the sun) and a faint sheen of sky where the map says a material is glossy — ice, water, wet flesh, pebbles, clay.
- **Glow** lights texels by themselves (lava veins, star-forge sparks, golden motes, the seedling's bud) and feeds the bloom mask; blocks whose glow should also light the world are `GLOW_EMBER` (a steady warm source in a third glow-grid channel); `GLOW_PULSE` breathes its glow map at 0.4 Hz, never below a third, far inside the flash limit.
- **Density is one number:** `tools/natural_textures.py --size` regenerates every material at 16, 32 or 64; all art in a set shares one size.
- **Cost:** one more texture read per pixel, a handful of multiplies, and a few derivative instructions; nothing on the CPU per frame.

The material set is organised in three families from the art batches: the **natural** set (stone, dirt, wood, snow, sand, sandstone, cracked earth, clay, basalt, magma, logs, moss, mossy cobble, meadow grass, water, ice, ash, coral, jungle leaves, leaf litter, peat, salt flat, pebbles, coastal sand), the **dark** set (veined flesh, flesh wound, pulsing membrane, weeping sore, corrupted flesh) and its light counterpart, the **genesis** set (genesis soil, seedling shrine, dawn light, star forge, new log). Water and ice are provisional solid see-through blocks until fluids exist.

### 4.14 Plants: cards that face you
Wildflowers, glow mushrooms, ferns, brambles and reeds are `SHAPE_CARD` blocks: one upright, see-through picture that turns about the vertical to face the viewer — the old sprite trick, which gives the impression of a plant from any side for one quad. The mesher emits four vertices all at the cell's base centre with the corners in u/v and a flag in the layer's top bit; the world vertex shader spreads them into a one-block quad sideways to the eye, so every plant turns on its own with no CPU work per frame and no new vertex data. The pixel shader cuts out see-through texels, so cards draw in the opaque pass with no sorting. Plants are walk-through (not solid) but targetable: the block-picking ray hits any non-air block past the cell the eye is in, so standing in reeds doesn't make them the target of every click. They cast no shadow (a card collapses to a point in the shadow pass). Their art is generated at 32 px on transparent backgrounds; the glow mushrooms' caps glow and the block lights the world warmly (`GLOW_EMBER`). Their library and hotbar icon is simply their picture.

### 4.7 Camera and player view
Standard FPS mouse-look: yaw from horizontal delta, pitch from vertical delta, pitch clamped to avoid gimbal flip (±~1.55 rad). Perspective projection, near/far planes wide enough for the load radius in use.

**Moving:** walk 4.5 blocks/s; **sprint** (hold Shift, forward only) 6.5; **crouch** (hold Ctrl) 1.8, with the player's box dropping from 1.8 to 0.9 blocks tall and the eye from 1.6 to 0.75 — so the player fits a 1-block-high, 1-block-wide gap. Letting go of crouch stands up only where there's room to; under a low roof the player stays crouched, and a player who finds themselves with no room to stand (a load, a block placed over them) crouches rather than being pushed up out of the space. **Power slide:** pressing crouch while sprinting on the ground drops into a slide — a burst to 9 blocks/s along the way the player was running, bleeding off with ground friction (none in the air) over at most ~1.4 s, at crouch height (a slide goes straight under a 1-block gap), ending in a crouch; a jump ends it early, a wall stops that axis. During the slide the eye drops further and the view **leans into the slide**, relative to where the player is looking: sideways motion rolls the camera toward that side (up to ~12°), straight ahead dips it forward, backwards tips it back, scaled by the slide's speed and eased in and out. The lean is view-only — movement always uses the level right vector. Movement state isn't saved (it re-derives in a tick). All of it is in `UpdatePlayerPhysics` (world.cpp), tested natively.

---

### 4.15 Faceted props and building pieces
Set-dressing that breaks up the cube grid cheaply: small faceted shapes baked into the chunk mesh like any other shape (4.4), one mesh reused across many materials, so the terrain stops reading as uniform cubes without new geometry per biome. Faceted on purpose (octagons, not circles) to match the pixel-sampled look, and authored on the same 1/8-block grid as every shape.

- **One mesh, three placements.** A prop is authored once, anchored on its bottom face and growing up, leaning a little toward +Z. Placing it against a face (the clicked face, `PLACE_CLICKED_AXIS`) makes that face its anchor: on a floor it's a **Swell** (a mound, bulb or knob); under a ceiling the same mesh hangs as a **Drape**; on a wall it juts out as a **Root snag** or **Ledge**, its lean turned to droop. Props on floors and ceilings are also turned a quarter-turn at a time by a hash of the cell, so a scatter never lines up.
- **The family.** Swell presets: *mound* (low, flat-topped: clumps, tussocks, clods), *bulb* (pinched base, leaning well off-centre: coral nodes, flesh bulbs, membrane sacs), *knob* (tall, pinched: root knuckles), *boulder* (wide and low, worn smooth: pebbles, drifts) and *breaker* (barely above a water surface: a rock tip, a leaf pad). **Shard** is the angular counterpart, a single broken chunk (scree; a ledge on a wall). **Ripple lip** is a thin lapping ridge along one edge of a water surface, for shorelines.
- **Dwelling and industry pieces** use the same machinery: a **beam** (a diagonal rafter; cells placed stair-wise chain into one continuous beam), **corbel** (stepped bracket), **shutter** (louvered panel), **chimney cap** (tiered), **awning** (sloped plate off a wall), **pipe** (octagonal conduit along the clicked axis), **gear** (notched disc in relief), **vent** (a stack flaring at the top), **hopper** (an open inverted frustum, for ore) and **strut** (an X-brace facing the player).
- **How they're built** (`shapes.cpp`): each shape is a few convex parts given by their corner points; an exact integer convex hull turns each into outward-wound faces, once, at startup. Placement applies an integer rotation about the cell centre (the anchor, then the quarter-turn); faces lying flat on a cell boundary are hidden by a full neighbour like any shape's; collision is one box, the bounds over every turn (a mound is low enough to step onto). The prop's own top wears the block's top texture; everything is textured by projection along its facet's dominant axis.
- **Lighting slanted facets.** Faces that aren't axis-aligned carry a slope shade class, and the pixel shader lights them by their exact flat normal (the cross product of the position's screen derivatives, turned toward the viewer) — free, exactly faceted, and it also corrected the older ramps and pyramids, which had used one fixed normal whatever their orientation.
- **Budget.** A prop is 10–30 polygons. A chunk's mesh keeps 16-bit indices; a chunk packed solid with the most detailed props could exceed 65,536 vertices, so any past that limit are left out of that chunk's mesh instead of overflowing. See-through props (ice, water) go in the blended pass.
- **In the library now, not in world generation yet.** The first set (40 blocks) pairs the shapes with existing materials: moss, meadow, earth, peat, genesis and salt clumps; coral, flesh and membrane bulbs; snow, pebble and coastal boulders; stone, basalt, magma, sandstone, mossy, ore and ice shards; a water ripple, a pebble and a leaf breaker; log and wood beams, wood and stone corbels, a shutter, an awning, stone and clay chimney caps; tube and lattice pipes, a machine gear, a foundation vent, an ore hopper, wood and lattice struts. Scattering them through terrain generation comes later.

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

### 5.4 Scheduled block updates — gravity generalized
The falling-block queue is now one case of a general **scheduled update queue** (`ScheduleUpdate(x, y, z, kind, delayTicks)`, world.cpp): a priority queue ordered by due tick then insertion order, with a handler per `UpdateKind`. Gravity (`UPD_GRAVITY`) is the first kind; machines and anything else whose blocks change over time add a kind and a handler rather than their own queue. At most `MAX_UPDATES_PER_TICK` (64) run per tick however many are due, oldest first — Section 5.1's smoothing, now shared. An idle world has an empty queue and costs nothing: simulation cost scales with what's actually changing (Part 1.3). The profiler shows the time spent and the queue length (Part XVI).

A column with updates pending isn't evicted (a cascade must finish in its own column), and pending updates are **saved** with their remaining delays (save v6), so a save taken mid-collapse finishes collapsing after loading instead of leaving blocks floating. Long-delay updates — machine timers — will want to travel with their chunk (stored per chunk, resumed when the column returns) rather than pinning a column in memory; that's the planned extension when machines arrive.

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

### 7.2 Format actually implemented (v8)
The byte format lives in `worldfile.cpp` (pure C++, no OS calls — tested natively); `persist.cpp` does the disk side and applies a decoded save to live state.
```
magic (u32 "VXLG") | version (u32, currently 8)
player: pos.x,y,z (f32×3)  yaw,pitch (f32×2)  hotbarSelection (i32)  dayTime (f32)
generator: name (str)  version (u32)  seed (u64)                      (2.5)
blockNameCount (u32) | [ nameLen(u16) nameBytes ] × count             (3.1)
chunkCount (u32) | per chunk:
    cx, cy, cz (i32×3)  flags (u8: 1 = has state, 2 = has data)
    blocks: runs of (length u16, nameIndex u16) covering all 4096 cells
    state (if flag 1): runs of (length u16, value u8)
    data  (if flag 2): count (u16), then (cell u16, length u32, bytes)
updateCount (u32) | [ x,y,z (i32×3)  kind (u8)  delay (u32, ticks from now) ] × count   (v6, 5.4)
lineCells (u32) | [ cell (i64)  seconds (f32) ] × count; angMom (f64); angle (f32)   (v7, Part XVIII)
zoneCount (u32) | zone id (u64) × count; attractorCount (u32) | [ x,y,z (i32×3) ] × count   (v8, Part XIX)
checksum (u32)  — FNV-1a over every byte above
```
**Only modified chunks are written** (2.4); everything else regenerates from the recorded generator. Cells run in `LocalIndex` order (x fastest, then z, then y), so the horizontal layers typical of terrain and buildings collapse into a handful of runs. The effect on size is large: the old format spent 13 bytes on every non-air block (x, y, z as i32 plus an ID), so a radius-8 hills world was tens of megabytes; now an untouched world is a few hundred bytes of header, and a modest build costs a few hundred bytes to a few KB per chunk it touched (the native test's two-chunk edit encodes to 209 bytes).

**Older versions still load:** v2 (preferences embedded, 7.2.2), v3 (preferences moved out), v4 (added the day clock), v5 (generator + per-chunk storage, no pending updates), v6 (no Line state), v7 (no essence discoveries). Their block lists are all hills v1 terrain, so they load as hills worlds with every stored chunk flagged modified; because those formats stored only non-air blocks, a hills chunk the player had dug out entirely would be absent — so each stored column's chunk rows 0–3 (hills v1 tops out at y = 60) are filled in as explicit empty chunks rather than letting regeneration refill them. Saving such a world again writes the current version.

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

**Autosave:** every 5 minutes of actual play (paused time doesn't count, so a game left on the pause menu isn't rewritten), on Quit to Title, on Quit, and when the window is closed mid-game. With delta saves (7.2) a save is small enough to write on the main thread without a hitch; the previous file is always kept as `.bak` by the sequence above.

### 7.4 Load sequence (corruption safety)
1. Read the whole file into memory.
2. Compute FNV-1a over everything except the trailing checksum field; compare. Mismatch → abort before touching any live game state, log the reason.
3. Verify magic number and version — wrong magic or unsupported version aborts cleanly rather than attempting to interpret garbage as a world.
4. Build the saved-name → current-`BlockID` remap table. A name no longer present in the current build maps to `AIR` (with a logged warning) rather than silently reinterpreting as whatever ID happens to occupy that slot today.
5. Decode every chunk into a scratch map (never through the live `Set()` path — no gravity checks against a half-built world, 5.2); refuse the load on any structural error (a run overflowing its chunk, a bad cell index, an unknown generator).
6. Only then apply: player, day clock and generator, and hand every saved chunk to the modified-chunk store. Streaming generates the columns around the player and overlays those chunks (2.4), so a load costs the decode, however large the world.

### 7.5 Designed for later
Per-region multi-chunk files (grouping a 16×16 column of chunks behind one small offset-table header) — the natural extension once one save file becomes slow to write at scale, requiring no format redesign since v5 chunk records are already self-contained and would simply move into region-scoped files. Palette compression of chunks in memory, and writing saves on a background thread, are the other two noted extensions.

---

## Part VIII — Reference Material and How Each Was Used

**Standing rule: nothing anyone owns.** Everything in the game — art, music, names, symbols, text, code — is original or genuinely free to use. No brands, logos, trademarks, product names, currencies or crypto symbols, official insignia; no copyrighted art, music, characters or text; nothing recreated from another game (textures, creatures, item names, distinctive look). Genre conventions and mechanics are shared ground; another work's specific expression isn't. Public-domain motifs (knotwork, florals, geometric and sacred-geometry figures) and natural materials are fine. When in doubt, make it more original.

**Standing rule: consult these first.** The seed files in the repo root (Prismative.cpp, drillder.cpp, LG2.cpp, cc_2_2_2.cpp) — and totality.cpp, solarsystem.cpp and themer.cpp when they're pasted in — were each tested by hand and hold worked-out ideas. Before designing a new feature, texture, block behaviour or view, check whether one of them already solved it, and take the idea (never the code: see the licensing note at the top). The "Still to mine" notes below are the open leads.

### 8.1 Prismative.cpp (D3D11, closest in spirit to the target)
**Kept (as concept):** manager-class decomposition (ResourceManager/InputManager/EntityManager/GameStateManager/Camera/Game); the multi-shape block idea (cube/tube/ramp/slab/pyramid/funnel, each its own vertex buffer) — directly informed the cube/straight/corner/junction shape system; player AABB collision at multiple height samples; raycasting for block picking (approach superseded by exact DDA, per 4.5).
**Discarded:** the render loop's per-solid-cell draw call (the ~26,000-draw-call/frame problem, fixed by chunk meshing); fixed-step raycast marching (0.1 units/step, replaced by DDA); per-frame (not per-second) physics constants.

### 8.2 drillder.cpp (GDI+ orthogonal slice viewer)
**Kept (as concept):** integer axis-aligned player facing (`dirX,dirY,dirZ`) with 90°-rotation via integer swizzle instead of float yaw/pitch for block-facing purposes — directly informs how pipe/machine orientation should be represented, though not yet implemented in the prototype since no oriented blocks exist yet; durability-on-voxel (`Voxel{type, isObstacle, durability}`) as the germ of mining/machine-progress mechanics; the fixed-tick movement accumulator pattern (`moveTimerAccumMs`/`MOVE_DELAY_MS`), generalized into the main tick loop.
**Discarded:** world wrapping (rejected per 1.2); the three-orthogonal-slice rendering itself is not part of the game, though it was flagged as worth keeping as an optional debug overlay (not yet built).
**Since used:** oriented blocks now exist (chests, machines, ramps, tubes) and store their facing in the block's state byte (3.2).
**Still to mine:** per-voxel durability for mining and machine progress; the three-slice view as a debug overlay (looking inside terrain, testing crawlspaces).

### 8.3 LG2.cpp ("Lawn Gone," 2D cellular automaton)
**Kept (as concept):** budgeted-work-per-tick (`MAX_SPREAD_PER_CYCLE`) as the direct ancestor of `MAX_FALLS`; deferred mutation (collect into a side buffer, apply after the iteration completes) as the direct ancestor of the falling-block queue's structure; cheap subsystem presence guards (skip a whole update pass when nothing needs it) as a principle, though its actual implementation (`any_of` scans over the full block vector every tick) was identified as the wrong way to implement that principle — a maintained running count per type is the correct version.
**Discarded/warned about:** the quadtree-over-uniform-grid mismatch (a uniform grid wants O(1) index lookup, not a spatial tree); the dangling-pointer bug from mixing a pointer-based spatial index with a mutating vector; positional-enum save corruption (Part VII).
**Still to mine:** its block *behaviours* — grass spreading, tree growth and rings, fire spreading and burning trees to charred ones, seawater and fresh water spreading, desert creep, TNT, lifespans — each maps directly onto the scheduled block-update queue (Part V) as a budgeted per-tick rule, and would give the natural materials (4.3) something to *do*.

### 8.4 cc_2_2_2.cpp (2D GDI+ sandbox, 145 texture generators)
**Kept (as concept only, not code):** GDI+ procedural texture generation baked once at load time into GPU-resident textures — directly informs `textures.cpp`'s role and structure, though every generator function in `textures.cpp` is newly written.
**Discarded:** the ~700-line copy-pasted `if(shift)/else if(ctrl)/else` key-handling block (noted as a pattern to actively avoid when hotbar/keybinding code is written — a small `{key,modifier}→action` table is the correct shape); holding 145 live `Bitmap*` objects at once rather than baking into a shared atlas.
**Still to mine — the texture technique library.** Its 145 generators, which the player selected and tested, are the go-to source of *techniques* whenever a new procedural texture is made (the way `tools/natural_textures.py` made the natural set): not a list to port wholesale, but proven ways to draw a look. Roughly, by family:
- *Natural ground and growth:* grass (tufts, chevrons, wavy patches, tall/short), sand dunes and sandy ripples, desert mirage, snow drifts and patches, frost meadow, frozen tundra cracks, ice fracture, mossy stone and craggy mossy cliff, wet mud, riverbed pebbles and pebbled streambed, boulder fields and rocky outcrops, volcanic ash, craters and lava flow, bark and bark patches, leaf veins, leaf litter, leaf decay, fern fronds, canopy leaves, forest undergrowth, swamp mist, lily pads, algae, seawater and fresh water.
- *Weaves and tilings:* chevron, diamond, herringbone (offset, zigzag, diagonal, chevron mix), plaid, V-stripes, fish scale, interlocking circles, Truchet arcs, maze, stone mosaic, brick path, pixel camouflage.
- *Geometry and ornament:* golden-ratio and other spirals, Sierpinski triangle, cellular automaton, Mandelbrot fragment, flower of life, vesica piscis, Merkaba, triskele, tetractys, seal of Solomon, tree of life, mandalas and sigils, runic fields.
- *Tech and industrial:* circuit boards, LED matrices, hexagonal circuit grids, neon grids, clockwork, solar panels, nanotech grids, factory silhouettes — candidates for machine and pipe faces.
When porting a technique, apply the texture brief's rules (16×16, lines wrap, no regular dither, details scattered, no brands) and never its `rand()` seeding — several of its generators seed from the clock, which makes them different every run.

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
XAudio2 (`xaudio2.h`/`xaudio2.lib`), initialized once at startup (`InitAudio()`) after `CoInitializeEx` — the one thing in this codebase that actually requires COM initialized on the calling thread (`SHGetKnownFolderPath` manages its own COM state internally, so nothing earlier needed this). One `IXAudio2SourceVoice` plays the day-cycle music as a stream of small generated chunks (Part XIV); a second plays the world sound palette (10.4) on small buffers. Master, Music and World Sounds are separate sliders: each channel is its own source voice under the one mastering voice. `InitAudio()` failing (no usable audio device, missing driver, etc.) is non-fatal: every audio entry point is guarded by a null check, so the game is fully playable, just silently, rather than refusing to start.

### 10.2 Procedural, not an asset
All music is synthesized in code at runtime — no audio file exists anywhere in the repo, and there's nothing to license — the same "bake it in code, never load an external asset" philosophy the block/UI textures use (8.4). An earlier 20-second looping ambient pad and a later single-curve arpeggio track both preceded the current composition; Part XIV describes what ships now.

### 10.3 Mono music, stereo world
The music is generated as a single channel: it's the room the game happens in, not a place. The world sound palette (10.4) is stereo: a block you place or break sounds from its side, softening with distance, music blocks play their clave part from where they stand, ambience spreads gently across the field, and the echo ping-pongs side to side. Placement is equal-power and held within ±60 % of full pan (nothing sits hard in one ear), with the centre at exactly the mono level. **Mono audio** (Accessibility) centres everything for players who hear on one side only.

### 10.4 The world sound palette
Short interaction, discovery, accent, texture and rare-colour sounds — 46 of them in seven families — synthesized by the music's own engine and written as parts of the composition, not effects laid over it. The full specification is **docs/SOUND_PALETTE.md**; the short version:

- **One clock, one harmony.** The score's chord, beat, section, filter cutoff and master level are pure functions of day time, so `MusicHarmonyAt(t)` (music_synth.h) tells the palette exactly what is playing at the moment a sound will be heard (`AudibleMusicTime`, read from the music chunk actually playing). Every pitch comes from the current chord's *safe set* (scale tones not a semitone from a sounding chord tone; D–G–A during a chord crossfade); every scheduled onset lands on the beat grid; thumps glide onto the current bass note.
- **Shared DNA.** The oscillators, envelopes, noise and filters are literally the track's (`synth_kit.h`, extracted from music_synth.cpp and shared), plus a formant voice for wordless vocal ad-libs and a bell. Every sound passes a lowpass that follows the track's day-long cutoff arc, and the palette as a whole a 4.2 kHz ceiling. No sound passes −21 dB on the track's own layer scale (the motif sits at −15 to −19), checked per sound, not per voice.
- **Three axes** from the ground around the player (`soundscape.h`: a fixed 1,024-block census slab per frame over a 32 × 24 × 32 box, plus movement and recent actions, all eased over seconds): **positive ↔ negative** (health vs neglect: pitch pool, contour, register, wear), **calm ↔ active** (ambient budget from 1 to 6 events per 4 bars, grid, release, echo), **organic ↔ mechanical** (soft triangle → clean pulse, timing jitter, metallic partials, filter edge). The main track leans the same way, gently (cutoff, pad and air balance, wow, pump — ±2–3 dB at most, bit-identical at the neutral point).
- **Gestures, not piles.** A tempo-synced dotted-8th echo; Sets climb the current chord's ladder and Takes descend it, so a building session plays arpeggios and a streak to the octave resolves into a cadence; onsets within 30 ms merge; repeats soften; a new note avoids a 2nd against anything ringing; event sounds suppress ambience for their length; rare colour waits for calm.
- **Where it plays.** `worldsound.cpp` wires it to the game: place, break, refused placement, hotbar, the library (open, pick, drop, close), save, footsteps on the beat (crouch every other beat, walk every beat, sprint on 8ths; soft ground scuffs and crunches, hard ground clicks and knocks quietly in key), landings, slides, sprint "hey"s, The Line passing through the player, discoveries from the census, and the per-bar ambient scheduler (wind in the grass, chirps, night shimmer, drips, ember crackle, machine rhythms and hum, music blocks as a clave part, the dark set's heartbeat and worn drone, caves, far bells, falling stars, the day's seam). Pausing fades it all in 0.3 s (pause is silence); the library and map keep their own UI sounds.
- **Cost.** A 512-sample buffer per ~11.6 ms, rendered only when something sounds or play is live; typically 2–6 of 48 voices; the census is constant per frame. Measured in the F3 profiler as WORLD SOUND, which also shows the three axes.

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

**The sky and world light follow it** (`ComputeSky`, sky.h — pure functions of the clock, tested natively): the sun rises in the east as the music's Dawn begins (0:00), peaks tilted toward the south, and sets in the west at 50:00 inside Dusk, leaving ten minutes of real night. The moon trails it by ~140°, up through dusk and night into the early morning. The sky shader blends day and night gradients, warms the sky around a low sun, and draws the sun's disc and glow; the world's brightness runs from 30 % at night (still playable) to full in the day, and the direct-sun amount drives the shadows (4.8). The star field turns about the celestial pole with the clock (normal east-to-west streaming), and The Line (Part XVIII) composites its own precession on top.

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

Eighteen source files (`main.cpp world.cpp render.cpp audio.cpp persist.cpp game.cpp textures.cpp music_synth.cpp profiler.cpp worldfile.cpp vtex.cpp blocktex.cpp mesher.cpp shapes.cpp icons.cpp theline.cpp essence.cpp essencemap.cpp`), one compiler invocation, no project file strictly needed (the checked-in `.vcxproj`/`.vcxproj.filters` list them all for Visual Studio):

```
cl main.cpp world.cpp render.cpp audio.cpp persist.cpp game.cpp textures.cpp music_synth.cpp profiler.cpp worldfile.cpp vtex.cpp blocktex.cpp mesher.cpp shapes.cpp icons.cpp theline.cpp essence.cpp essencemap.cpp /link d3d11.lib dxgi.lib d3dcompiler.lib gdiplus.lib gdi32.lib user32.lib shell32.lib ole32.lib uuid.lib xaudio2.lib /SUBSYSTEM:WINDOWS
```

or with MinGW-w64 (used during development to compile-check this prototype on a non-Windows host, since it ships full D3D11/DXGI/D3DCompiler/GDI+/XAudio2 headers and import libraries):

```
x86_64-w64-mingw32-g++ -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE \
  main.cpp world.cpp render.cpp audio.cpp persist.cpp game.cpp textures.cpp music_synth.cpp profiler.cpp worldfile.cpp vtex.cpp blocktex.cpp mesher.cpp shapes.cpp icons.cpp theline.cpp essence.cpp essencemap.cpp -o voxistics.exe \
  -ld3d11 -ldxgi -ld3dcompiler -lgdiplus -lgdi32 -luser32 -lole32 -lshell32 -luuid -lxaudio2_8 -static-libgcc -static-libstdc++
```

Each `.cpp` above owns one subsystem and includes only the headers it needs (`common.h` for shared math/block-table types; `world.h` for the simulation model; `render.h` for D3D11 state and chunk meshing; `audio.h` for XAudio2 playback; `persist.h` for settings/save-load; `game.h` for the menu state machine, input dispatch, and the UI render pass). `blocks.h` is the block registry (Part III). `worldfile.cpp` (save format), `vtex.cpp` (texture parser), `blocktex.cpp` (block texture set), `mesher.cpp` (chunk meshing), `shapes.cpp` (block shapes) and `icons.cpp` (hotbar icons) are deliberately free of Windows and D3D so the native tests (Part XVII) can build them; `textures.cpp` (GDI+ UI font atlas) and `music_synth.cpp` stay dependency-free of the rest of the project by design. `textures.cpp` exposes `extern "C"` entry points to `render.cpp` by convention; `music_synth.cpp` has its own header (`music_synth.h`) holding `MusicState` and the generator's entry points, and `audio.cpp` `static_assert`s that the music's day length matches `DAY_LENGTH_SECONDS`.

(`-lxaudio2_8` is MinGW's import-lib name for the same XAudio2 2.8 API that the Windows SDK's `xaudio2.lib` provides — a MinGW-only naming difference, same idea as `-municode` above it.)

Default controls (all fully remappable to any keyboard key or the left/right/middle mouse button via Pause → Keybindings — click a row, then press the new input; Esc cancels a rebind in progress, except on the Pause Menu row, where Esc binds Escape): WASD to move, mouse to look (click once to capture the cursor), Space to jump, Shift to sprint, Ctrl to crouch (Ctrl while sprinting: power slide, 4.7), left-click to break the targeted block, right-click to place the selected hotbar block, number keys 1–9 and 0 or the mouse wheel to select a hotbar slot, E for the block library (click a block to use it, drag it onto a slot to keep it), M for the essence map, F3 for the profiler overlay (Ctrl+F3 records a 30-second performance report to `perf_report.txt` beside the saves), F7 for The Line's debug marker, F8 to jump to the next time of day and ] / [ (or Page Up / Page Down) held to run the clock forward / backward — a whole day in 15 s, with a time readout; the one day clock drives the sky, sun, shadows, light and music, so all of them follow, and the music re-anchors to the new time on release (debug aids, like F3 and F7), F11 for fullscreen, F5 to save, F9 to load, Esc to open/close the Pause menu or back out one level from any of its submenus (Look Settings, Graphics, Display, Audio, Keybindings, each with its own Reset to Default), all clickable with the freed cursor.

## Part XVI — Frame Profiler

Part 1.3's rule (cost scales with what's on screen or changing, never with total world size) is only a rule if it can be checked, so the engine measures itself. `profiler.h/.cpp` times each system every frame with `QueryPerformanceCounter` (`ProfScope` RAII timers around terrain generation, eviction, physics, falls, music synthesis, mesh rebuilds, world draw submission, the UI pass and `Present`) and records load counters (resident chunks, chunks and triangles drawn, meshes built, dirty chunks / columns / falls waiting). A 128-frame ring buffer (~2 s) is summarised twice a second into average and worst milliseconds per system, plus frame time and **work time** (frame minus `Present`, which under vsync is mostly waiting rather than work). Worst-frame numbers matter as much as averages: a hitch is a single bad frame that an average hides.

Collection is always on (a few dozen timer reads per frame); the overlay is toggled with F3 (unless F3 is bound to an action) or Display Settings → Profiler, and persisted in settings.cfg. New systems should get a `ProfScope` and, where they have a queue, a counter — that is how a design-rule regression shows up the day it's introduced instead of in a playtest.

**Performance report (Ctrl+F3).** The overlay shows ~2 s at a time; judging a build needs longer and needs it written down. Ctrl+F3 records every frame for 30 seconds of normal play, then writes `perf_report.txt` next to the saves: the build (Debug/Release), window size and graphics settings (nothing about the machine itself is read); median, 95th, 99th percentile and worst frame and work time; the same for every system; hitch counts (work over 33, 50 and 100 ms); the five worst frames with the systems that took their time; and peak load counters. A quiet countdown shows while it records. It's how performance gets judged on the owner's machine rather than guessed in the cloud.

## Part XVII — Tests

`tests/run.sh` builds and runs `tests/tests.cpp` with the host compiler — no Windows needed — against the platform-free modules (`world.cpp`, `worldfile.cpp`, `vtex.cpp`, `blocktex.cpp`, `mesher.cpp`, `shapes.cpp`, `icons.cpp`), with `tests/stub/` standing in for the two Windows/D3D headers they touch. It covers the `.vtex` parser (valid input and each class of error), texture assembly (placeholders, authored overrides and upscaling, orientation, warnings), the v5 save round trip (including state, data, corruption detection), legacy v4 loading (name remapping, dug-out chunks), streaming (one-ring-past-view residency, eviction keeping only modified chunks, bit-exact regeneration on return), player spawn and unstick, the mesher (culling, AO, cross-chunk faces, orientation, the 16-bit worst case), shapes (orientation, boundary culling, collision, stepping onto a slab but not a full block), the rendered icons, and the sound system (the music's harmony query and colour, every palette sound's pitch safety and level ceiling under every chord, gestures, merge, determinism, the ambient budget, pause fades, and the soundscape census; 10.4). Every change to those modules should keep it at zero failures, and new systems should add their checks here — the harnesses that used to be written and thrown away during development now live in the repo instead.

## Part XVIII — The Line

A thin, one-dimensional distortion in local time, personal to the player (`theline.h/.cpp`, pure C++, tested natively). It is not a mapped place and not a drawn effect: its intended expression is only in how things that already move behave. A debug marker exists for testing and is meant to be hidden once the sky expression is validated on its own.

**The pivot** is the place the player spends the most time. Time spent is accumulated per 32-block cell, fading with a 4-hour half-life of play, so where the player spends time these days outweighs where they once did (every cell fades at once for free: new time is added inflated by a growing scale factor instead, so the favourite cell can only ever change to the one being added to — O(1) per tick). The pivot's target is that favourite cell, refined toward whichever of its eight neighbours also hold a lot of time (weights squared, relative to the favourite) — the middle of the place actually lived in, never the empty ground between two separate haunts, which an average over everywhere would give. The pivot **travels** toward its target rather than jumping: most of the way in about a minute, never faster than 2 blocks a second, so a move to a new base drags the line across the land over several minutes. Pivot sources are a list — today only the player's own dwelling — and whichever dominates supplies both pivot and spin; player-built structures are meant to become further sources through the same mechanism (not built yet, not architected out).

**Spin** is **clockwise** seen from above by default. It reverses only when the player's net angular momentum about the pivot (top-down, +X right, +Z up; counterclockwise positive: Σ (r × v) over actual movement, with a 3-hour half-life of play so it follows current habits) builds up a good deal of counterclockwise circling — about twenty seconds of deliberately walking around the pivot that way — and returns to clockwise as that fades. Teleport-sized jumps (loads) don't count as movement.

**The line** runs horizontally through the pivot at the player's own level — half a block above their feet, through the middle of the blocks they stand among (following the player's height over ~5 s) and sweeps around the pivot in the spin's direction, one turn per in-game day. It is evaluated only within loaded space and never constrains building — it has no blocks, no collision, no geometry.

**Intensity** falls off exponentially in whole orders of magnitude of the player's horizontal distance to the line: I = 10^−steps(d / L), where `steps` is a smooth staircase (flat treads, short risers — legible decades rather than a continuous slope). L, the blocks per decade, is asymmetric with motion: stretched to 12 when moving with the line's sweep (strong effect with little distance closed), shrunk to 3 against it, 6 standing still. All of these are open parameters in `LineTuning`, meant to be prototyped and tuned, not a finished curve.

**Sky expression.** The star field (procedural, in the sky shader) keeps its normal east-to-west streaming — turning about the celestial pole, which lies perpendicular to the sun's path — and The Line composites a small **precession** on top: a tilt of up to ~6° about a horizontal axis that starts along the line and circles at a rate rising with intensity, in the spin's direction. The stars still do their normal thing, but subtly wrong, and more so the closer the player is to the line. The **moon** gets a ghost image from the same precession at a quarter of the amplitude, drawn at under a quarter of the moon's brightness — a soft double exposure, always subtler than the star effect. Nothing here needs UI; wind, when a wind system exists, is meant to take the same intensity value.

**Persistence:** the pivot history (a float per cell visited), the spin accumulator and the line's angle are saved (v7); everything else is re-derived each tick. **Debug:** F7 toggles the marker (the line across the loaded area — cyan for counterclockwise, orange for clockwise — with strokes showing which way it's sweeping and a white pole at the pivot) and a top-right readout of distance, blocks per decade, intensity, spin, alignment and pivot.

### 18.1 Reactive blocks
Two plain blocks light up on their own, at no CPU cost per block: the registry gives them a glow kind (`BlockGlow`), the mesher writes it into spare vertex bits, and the world shader reads one per-frame value. The **music block** flashes on the *notes* of the music actually audible, not its loudness: the day's music is mostly sustained pads, so loudness barely dips and a loudness-driven block sat lit 100 % of the daytime. Instead (`musiclevel.h`, pure, tested) each queued chunk is measured as it's synthesized in 1/64 s steps: the energy of the sample-to-sample change (which favours a note's bright attack over a pad's smooth body) relative to its own average over the last ~⅓ s, so a quiet passage flickers with its notes as much as a loud one, with an absolute gate keeping near-silence dark. The block never shows those onsets directly — at 1.5–6 a second that is a strobe, and a seizure risk (photosensitivity guidance caps flashes at 3 a second). What it shows is a slow **swell** that follows how busy the notes are: rising over ~¼ s as they cluster, fading over ~1 s, spread over the full brightness range, with a hard slew limit on top (full range takes at least half a second either way, whatever the input). Measured over the game's own music it breathes between roughly 20 % and 80 % brightness with at most ~0.6 noticeable swings a second; fed notes at 8 a second it simply holds steady (tested). The level is read back at the chunk XAudio2 is playing now (submitted minus still queued), so it follows what's heard rather than the four seconds generated ahead, and it behaves the same at any frame rate. The **timestream block** lights while The Line passes through its cell: the pixel shader finds the block's cell from the world position and face normal and measures its distance to the line (across it, and vertically against the line's height). It is the one deliberate exception to The Line's invisibility — a detector the player chooses to place.

## Part XIX — The Essence Network Map

A top-down, pannable, zoomable view of the player's own discovered essence network (M, rebindable; a menu screen, so the world is frozen and the cursor free while it's open). It covers the screen with a fully opaque backdrop and the world isn't rendered at all while it's up — no trace of the scene behind it, and no GPU spent on it. Nodes sit at their real world coordinates; the map is a legible abstraction for decision-making, not a dump of the simulation — when in doubt it shows less.

**The data (essence.h/.cpp, provisional).** No essence simulation exists yet, so the map reads a small `EssenceNetwork` model that the real simulation is meant to replace behind the same interface: *convergence zones* placed deterministically from the world seed (0–3 per 128-block region, magnitudes spread over four orders of magnitude, mostly faint or minor, a few great) and *attractors*, a placeholder `essence_attractor` block standing in for player-built structures. Routes are derived: an attractor draws from zones within 64 blocks (active, or intermittent when the flow is small) and could from major zones out to 160 (planned); major zones within 160 blocks exchange essence; and 1 in 20 pairs of major zones up to 600 blocks apart are *bound* (entangled — linked with no physical path). Hierarchy is metadata only: each node rolls up into the strongest stronger node within reach, which groups belts and labels without ever moving a node. **Discovery:** only nodes the player has come within 40 blocks of exist on the map (attractors are known because the player built them); zones are generated only for the regions around the player, and the discovered set is saved (v8).

**One intensity scale.** Everything is read through order-of-magnitude bands (`EssenceBand`, one per decade — the same decades The Line's falloff steps through), named qualitatively (faint, minor, moderate, strong, great, vast). No numbers appear anywhere on the map.

**The view (essencemap.h/.cpp, pure, tested).**
- **Nodes:** size by decade of magnitude (`MapNodeRadius`: a fixed step per order of magnitude, mildly zoom-aware), rounds for natural zones, squares for built attractors, each with a soft halo.
- **Belts:** minor sources (band ≤ 1) aren't drawn as dots; they merge into one soft translucent belt per group (the node they roll up into, or their region), with individual dots appearing only when zoomed well in.
- **Routes:** solid for active, dashed for intermittent, dotted for planned; width and the speed of the pulses travelling along them rise with the flow's band. Only bound routes curve — a quadratic Bézier bowed to one side — so a straight line always means a physical path.
- **Labels:** the six most significant nodes and three most significant routes on screen, ranked by magnitude and named by band ("STRONG CONVERGENCE", "MODERATE FLOW"); a label that would overlap one already placed is dropped rather than stacked. Zoomed in past 1.5 px/block, every visible node is labelled.
- **Camera:** it opens centred on The Line's pivot (the place the player spends the most time), marked with a faint ring (The Line itself stays unseen) — with the player shown as an arrow. The wheel zooms about the cursor, and dragging pans.

It extends the existing 2D UI pass rather than adding a renderer: the view emits coloured triangles and label requests, which game.cpp batches through the same white-texel quads and crisp text the menus use. The routines that echo older prototypes (`ScaleRadius`, `DrawDottedBezier`, belt rendering and top-N ranking from totality.cpp, solarsystem.cpp and themer.cpp) were rewritten here because those files weren't available; each is isolated in one function so the originals can replace them.

---

## Part XX — Of Interest: the Development Pile
Wanted, not scheduled. Each entry notes what it would build on so it can be picked up cold.

- **Core direction — industry that is safe when it's well run.** Not "industry is the monster": every process has real byproducts (ash, slag, heat, spent essence, runoff), and the skill is managing them. Unmanaged, they spread through the simulation (budgeted update rules, Part V; fluids and gases below): soil drifts toward the dark set, plants wither, essence flows sour on the map. Managed — contained, filtered, reprocessed into something useful (ash to fertiliser, slag to stone), buffered with planting — a site stays healthy to its fence line. Damage recovers once its source is fixed, faster beside genesis blocks. Health is shown, not counted: colour, plants, light and the essence map's bands (qualitative, per the no-numbers rule). The dark and genesis sets are the two ends of that scale.

- **The threat: rifts of dimensional ooze** — an abstract enemy instead of combat (the spreading-corruption idea as a mechanic, not anyone's names or art). Land left badly managed long enough (unmanaged byproducts, sour essence, sustained dark-set drift) tears open into a rift; the land darkens first, so it's foreseeable. The ooze creeps block by block along a front on the budgeted update queue (Part V), turning terrain toward the dark set and consuming what was built — real loss. Pushed back by management, not weapons: genesis blocks and restored land, light (glow and lighting become defences — light as a resource), cutting off the neglected source, perhaps essence to seal a rift. The Line might slow or hasten it where the player lives. Cost stays bounded: only fronts near loaded ground simulate per block within a tick budget; distant rifts advance coarsely per region (like the essence network) and catch up on return. Shown on the essence map as a creeping band.
- **Fluids** (low priority; heavy). Water, lava and the like that flow and settle. Would build on the scheduled-update queue (Part V) as a budgeted per-tick spread rule, LG2.cpp's seawater/fresh-water spreading as the reference behaviour (Part VIII), the see-through pass (4.11) for rendering, and the per-block state byte for fill level. Today's shallow water and glacier ice are solid see-through placeholders.
- **Vapours and gases** (low priority; heavy). Steam, mist, spores, smoke drifting and dispersing through open space. Could share the fluid machinery with a lighter-than-air rule, and render cheaply as billboarded cards (4.14) or a fog tint sampled from a small grid like the glow light (4.12), so cost scales with what's near the player.
- **Soft texture filtering, colour bleed and large-scale variation** (4.3/4.13): a crisp/smooth texture mix, neighbour-colour bleed between blocks from a small colour grid, and a gentle world-space tint that breaks up tiling. Designed and agreed; to be judged from a side-by-side render before building.
- **Plants that drop off** when the block under them is removed (cards currently stay floating).
- **Hand-authored decorative set** from the first art batch (porcelain, rune shrine, stained glass as a see-through block) — parked.
