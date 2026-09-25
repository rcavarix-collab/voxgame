# Seed file review

September 2026. All four of the owner's seed files (now in `seeds/`) were read in full, every line, for the new mech game. For each file:
- what it is;
- the ideas worth taking (ideas only; the code stays the owner's);
- the flaws found.

**Standing rule:** anything we take gets re-implemented, tested and bug-checked. The flaws are listed so they aren't carried over with the idea.

---

## 1. drillder.cpp: "Drillder", a voxel slice viewer (882 lines, GDI+)

**What it is.**
- A 64×64×32 voxel world seen as three orthogonal slices (XY, XZ, YZ) around the player, plus a debug quadrant.
- The player moves one cell at a time, facing along an axis.
- **Moving forward into an obstacle digs it.** Each press takes one point of durability, and the block breaks at zero. Dug blocks go to an inventory.
- Gravity pulls one cell every 160 ms, in a direction the player can re-aim (V sets gravity to the facing direction).
- Block type 5 is a permanent, walk-through decoration.

**Ideas worth taking:**
1. **Ground that resists.** Durability per voxel, with digging that wears it down over time. For us, ground types get hardness: rockets bite less into rock, and a future drill or ram weapon grinds through soil fast and rock slowly.
2. **Digging yields material.** Dug ground goes somewhere (an inventory). A possible later hook: what the mech digs or blasts could feed it (repairs, ammunition). Not for the first test.
3. **Re-aimable gravity.** Probably not for a mech, but noted.
4. **The three-slice view as a debug tool:** seeing inside the ground under a crater, or checking what a blast removed.
5. **Indestructible markers** (type 5): useful for test-course boundaries and spawn pads.
6. Strafing as its own move, and continuous movement while held.

**Flaws found:**
- **Movement follows the keyboard's repeat rate.**
  - Every WM_KEYDOWN, including auto-repeats (~30 a second), calls MoveForward immediately and resets the move timer.
  - Holding W therefore moves and digs at the repeat rate, not every 160 ms.
  - Fix: ignore repeats (lParam bit 30) and move only on the timer.
- **Clicks in the vertical slices are upside down.** Drawing maps screen-up to +Z (`offsetZ = half - cy`), but the click handler uses `wz = z + (cy - half)`. Clicking above the player places or removes the block below.
- **Pitching loses the heading.** Pitch up from any horizontal direction goes to straight up; pitching down again always returns to ±X. Facing +Y, up then down leaves you facing +X.
- **Yaw does nothing while facing up or down**, because rotating (0, 0, ±1) about Z leaves it unchanged. Strafe is disabled too, so the player is stuck until they pitch.
- **Compass labels don't match the axes.** In the XY slice, +Y is drawn downward, but "N (+Y)" is written at the top.
- A block can be placed in the player's own cell (the 3×3 placement area includes the centre), embedding the player in an obstacle.
- The world wraps in X and Y; the old design rejected wrapping.
- Timers assume exactly 16 ms per WM_TIMER, which Windows doesn't guarantee, so speeds drift.
- Repaints about 675 text strings per frame with GDI+. Slow, but fine for a slice view.

---

## 2. LG2.cpp: "Lawn Gone", a 2D cellular-automaton sandbox (1,003 lines, GDI+)

**What it is.** A 100×100 grid of blocks with living rules, run 10 times a second:
- **grass** spreads (up to 20 new cells a cycle, with 20% odds per neighbour);
- **flowers and bushes** sprout on grass, capped by local density, and wither after a lifespan;
- **trees** seed new trees 3–5 cells away, up to 200 of them with density caps, grow a ring of leaves, and die after 2,000 cycles;
- **fire** spreads to flammables (10%), always catches gasoline, burns out after a lifespan, and turns about 30% of burning trees into **burnt trees** that stand for 5,000 cycles;
- **gasoline** makes fire jump two cells;
- **explosives** set off neighbouring explosives in a chain and clear a radius-6 area;
- **seawater** kills grass and floods holes, and becomes deep sea at the edges;
- **fresh water** forms in holes surrounded by holes and spreads through connected holes (unless there's desert next to them);
- **desert** creeps out from rock, except near fresh water.

The view scrolls when the player walks off the edge.

**Ideas worth taking:**
1. **Fire as the second layer of destruction.**
   - A rocket in dry grass starts a fire that spreads on its own.
   - Fuel spills (like its gasoline) make fire leap.
   - Trees burn to charred trunks that stay standing.
   - It gives destruction consequences over time, and it's cheap: a budgeted rule on a sparse set of burning cells.
2. **Chain reactions.** Explosive props that set each other off: fuel barrels or depots as test-course targets.
3. **Craters fill with water.** Its holes turning into fresh water that spreads through connected holes means our blast craters can become ponds over time.
4. **The land heals.** Grass creeps back over bare and scorched ground, and trees reseed with density caps. Destruction isn't permanent, and a battlefield softens if you leave it.
5. **Lifespans with density caps** keep a living world bounded, with no runaway growth.
6. Its lesson, already learned in Voxistics: every rule gets a work budget per tick.

**Flaws found (crash-level first):**
- **Dangling pointers.**
  - The quadtree stores `Block*` pointers into the `blocks` vector, rebuilt only when painting.
  - Every tick function inserts into `blocks` (which reallocates) and erases from it (which shifts elements), then queries the stale quadtree, and even writes through its pointers (`adj->type = FIRE`).
  - That's undefined behaviour: wrong cells change, or it crashes.
- **Erasing inside a range-for.** `SpreadSeawater` calls `RemoveBlockAtPosition` (a vector erase) while looping over `blocks` with a range-for, which invalidates the iterator. `ExplodeTNT` and `AddTreeRings` do the same through quadtree pointers.
- **Duplicates.** Spread checks use the stale index, and new cells made in the same cycle aren't checked against each other, so several blocks pile onto one cell.
- **Sprites never made.** `LoadSprites` creates only the two grass patterns ("... rest as per original"). Rock, water, fire, trees and the rest draw from null bitmaps, so they're invisible.
- **Unsafe save file.**
  - Raw `Block` structs are written with padding, `size_t` counts and positional enum values.
  - Load trusts the count, so a damaged file can make it reserve gigabytes.
  - The save sits loose in the Documents root.
- **Scans for presence.** Every "is there any fire / seawater / TNT…" check scans all blocks every tick. Counts should be kept running.
- **Unbounded growth.** Scrolling shifts every block's coordinates and adds blank blocks at the edges. Blocks that scroll off screen are kept forever and still iterated.
- Gasoline catches with `percentDist(gen) < 100`: always, so that roll is pointless.
- Random numbers are seeded from the machine (`random_device`), so worlds aren't reproducible.
- `GetDC(hwnd)` in WM_CREATE is never released (a leak).
- `InvalidateRect(..., TRUE)` every tick erases the background (flicker).
- `ShiftGrid` redraws `GetConsoleWindow()`, which doesn't exist here.
- **Blinking player.** The player sprite alternates frames at 1 Hz. That's within the photosensitivity limit, but it's still a blink; we'd use a steady marker.

---

## 3. Prismative.cpp: a D3D11 multi-shape block viewer (1,196 lines)

**What it is.**
- A 64³ grid drawn in D3D11 with nine shapes (cube, tube, ramp, top and bottom slabs, pyramid, half pyramid, funnel, upturned funnel), each with its own vertex buffer.
- Walk around with mouse-look; place and remove blocks by raycast; select the type with the number keys.
- Split into manager classes: Resource, Input, Audio, UI, GameState, Entity, Camera, Game.
- **A tool (a stick) is drawn in the foreground, attached to the camera** and following its yaw and pitch.
- Esc pauses and frees the cursor.

**Ideas worth taking:**
1. **Foreground equipment attached to the camera.** This is the direct ancestor of the mech's weapons in the cockpit view: offset right, down and forward, turning with the view. For us: the arms and guns, with recoil and sway.
2. The **manager split** (resources, input, state, entities, camera), already reflected in our module plan.
3. Pause frees the cursor; resume captures it. Pausing freezes the world.
4. Collision sampled at several heights (feet and head); for the mech, at several points up its body.
5. Its shape set: ramps, pyramids and funnels as prop and debris shapes.

**Flaws found:**
- **One draw call per block, with a constant-buffer update each** (`UpdateSubresource` per cell). 64³ cells gives thousands of draws per frame, the classic fatal flaw that chunk meshing and instancing fix.
- **The camera sits at the feet.** `Camera::UpdateFromPlayer` copies the player's position (the feet) and never adds eye height. Raycasts start from the feet too.
- **The tool is held on the wrong side.** `right = Cross(forward, up)` points left in this left-handed setup. (The movement code uses `Cross(up, forward)`, which is correct.)
- **Refuses to start if any texture is missing.**
  - All 11 PNGs must exist in `Documents\primative_sprites\` at exactly 32×32.
  - Otherwise Init fails, WM_CREATE returns -1, and the window silently never appears.
  - The shader blobs leak on that path too.
- **Player width ignored.** `PLAYER_WIDTH` is never used; collision tests one column at the centre, so the player clips into walls.
- **Speed tied to the frame rate.** Speed and gravity are per frame (`MOVE_SPEED` 0.1 per tick) on a 16 ms WM_TIMER that Windows doesn't guarantee.
- **Mouse centring hard-coded.** Centring uses `WINDOW_WIDTH/2` constants (wrong after a resize). It also runs even when the window isn't focused, pulling the cursor away from other programs.
- The raycast marches in fixed 0.1 steps, so it can miss thin corners and read normals poorly. (Superseded by exact DDA in Voxistics; for us, the density raycast with refinement.)
- Truncation (`int`) instead of floor for grid coordinates. It happens to work because of the +GRID/2 offset, but breaks for negative positions.

---

## 4. cc_2_2_2.cpp: "Color Creep", a pattern studio (5,759 lines, GDI+)

**What it is.** A 60×33 grid of 32 px cells that you paint with patterns from a keyboard layout: every key, plain, with Shift or with Ctrl, picks one of 135 types. Esc opens a sampler of every pattern in 3×3 clusters. F11 toggles fullscreen, and the grid saves to Documents.

Its heart is the generator library. **128 patterns are wired to cells**, plus **about 20 more generators that are written but never called.** By family:
- **Ground and growth (about 60):**
  - grasses: tufts, short, tall, wavy, chevron, windblown, prairie wave;
  - sand, dunes, ripples, mirage;
  - pebbles, boulders, outcrops, mossy stone, craggy cliff;
  - mud, snow, ice, tundra cracks, frost meadow;
  - volcanic ash, crater and lava;
  - bark, leaf veins, litter, leaf decay, canopy, ferns, swamp mist, glades;
  - fire, water, burnt tree, charred stump.
- **Weaves and tilings (about 20):**
  - chevrons, diamonds, five herringbones, plaid, V-stripes;
  - fish scale, interlocking circles, camouflage, stone mosaic, brick path;
  - Truchet arcs, maze.
- **Tech (about 12):**
  - circuit LED matrix, hex circuit grid, neon grid, nanotech grid;
  - solar panels, clockwork, circuit overload, factory silhouettes.
- **Mathematics:**
  - Rule 30 automaton, Sierpinski triangle, Mandelbrot fragment;
  - golden-ratio, logarithmic and sequence spirals;
  - number-themed mosaics.
- **Geometry and ornament (the unused set):**
  - flower of life, vesica piscis, merkaba, seal of Solomon, triskele;
  - tetractys, tree of life, cosmic egg;
  - mandala, sigil cascade, rune field, pentacle circle, astrological circle.
- **Letters and digits:** A–Z, 0–9 and ?!+= drawn from the system font (Consolas) at load.

**Beyond the generators, three things worth taking:**
1. **A texture-composition toolkit:** flip, rotate, mask, and composite with opacity. This is exactly the owner's version of the anti-repetition plan (random tile turns and flips), and a way to build new textures by layering existing ones.
2. **Layered composition as the house technique.** Most natural patterns are built as base, then scatter, then overlay: for example, tufts, then flowers, then a semi-transparent glow or mist. Our ground-type textures should be made the same way, with different layers per grass and dirt type.
3. **A curated palette.** About 70 named colours grouped by family (whites, yellows and oranges, reds and pinks, earth tones, grass greens, water blues, purples, stone greys). A starting palette for the ground types and the cockpit.

**And some patterns fit the shader directly.** Many generators are pure per-pixel arithmetic on the cell position, like `(x mod 8) * (y mod 8) % 7 == 0`. These need no texture memory at all and suit "fake it cheaply": cockpit screen backgrounds, HUD hologram scanlines, shield ripple patterns. Truchet tiles (random quarter-arcs per tile) never visibly repeat, which is ideal for large surfaces.

**Flaws found:**
- **Colours never configured.** `LoadSprites` defines the whole palette, then calls almost every natural pattern with white, grey and red placeholders. As shipped, canopy leaves, grass and water are all grey-and-white. The generators are shape techniques; their colours were never set (its own comment says so: "create block type for every pattern example with color configuration…").
- **About 20 generators never run, and most would break at 32 px.** They assume a big canvas: `radius = min(w, h)/2 - 20` goes negative at 32 px; the tetractys starts at y = 50; tree-of-life branches are 50 px long.
- **Different every run.** Truchet, digital occultation and rune field seed `rand()` from the clock. Organic mesh, quantum matrix, nanobot swarm and circuit overload use unseeded `rand()`, so they depend on call order.
- **Colour overflow.** The LED matrix accent is `Color(255, r+50, g+50, b)`. For red (255, 0, 0) the byte wraps (305 becomes 49), so the "brighter" accent comes out dark.
- **Invisible borders.** The solar panel border pen has alpha 0.
- **Visible seams and repetition.** Many patterns don't divide 32 evenly (stripe width 32/10 = 3, herringbone thirds), so they show seams at tile edges. Nearly all are regular grids, which repeat visibly across a surface: exactly the owner's concern. Any we take must be scattered by hash, not laid on a grid.
- **Mismatched sampler.** The Esc sampler lays out 200 types, but only 128 have renderers, so the rest are blank.
- **Handle and shutdown errors.** `GetDC(NULL)` is never released. `hbmMem` is deleted twice (CleanUp then CleanupSprites). GdiplusShutdown runs twice (WM_DESTROY and the end of WinMain).
- **Slow drawing.** DrawScene loops 16 chunks × every block, and every lookup is a linear scan.
- **The 700-line key table.** Plain, Shift and Ctrl for every key in if/else: the pattern to avoid. A small `{key, modifier} → action` table is the right shape.
- **Unsafe save file** (`size_t` counts, no validation). Same issue as LG2.
- **Motifs to handle with care** under "nothing anyone owns": runes, the pentacle, "Hg", "Σ", astrological numbering. Public-domain geometry is fine; lettered glyphs and sigils read as text or symbols, so keep them out of player-facing art unless the owner wants them.

---

## 5. What this changes in the first prototype plan
- **Ground-type textures** (DESIGN §2): build each grass and dirt type by cc_2_2_2's layered method (base, scatter, overlay), with colours from its palette. Scatter by hash, never on a grid. Vary tiles with its flip and rotate idea, applied per tile in the shader.
- **Hardness per ground type** (drillder): rock resists rockets. A drill weapon later.
- **Fire and regrowth** (LG2): a strong candidate for right after the first test. Chain-reacting fuel props are a candidate for the test course. Re-implemented as budgeted rules on a sparse set, no pointers into containers, running counts.
- **Foreground weapons attached to the camera** (Prismative): the cockpit view's arms and guns, on the correct side.
- **The three-slice debug view** (drillder), for looking inside the ground.
- **Shader-only patterns** (cc_2_2_2's per-pixel formulas, Truchet) for cockpit screens, HUD and shield effects.
