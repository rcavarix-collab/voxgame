# Architecture and first-prototype plan

September 2026. This is written before any engine code, so we build in an orderly way.
1. How the new game is organized.
2. What's worth carrying from the Voxistics archive and from the owner's seed files.
3. What the first prototype needs to be a meaningful test.
4. The build order.
5. The questions for the owner.

The game's design lives in `DESIGN.md`; this document is about how to build it.

---

## 1. Architecture

### 1.1 Principles
These come from CLAUDE.md, applied to this game.
- **Simulation is pure C++; presentation is Windows/D3D.** Terrain, mech, weapons, props and the target have no Windows or graphics code in them. They're tested natively, and they can't stall the renderer.
- **Actions become events.** The simulation doesn't play sounds or spawn sparks. It records what happened (fired, hit, blast, tree felled, lock acquired, footstep on clay, damage taken). The sound effects, the music, the visual effects and the HUD each read that list and respond in their own way. This is what lets actions "add to the music in close sync" without tangling combat code with audio code, and it keeps each response cheap and separate.
- **Cost follows activity, never world size.**
  - Terrain stores only what changed and meshes only what a change touched.
  - Effects and debris come from fixed pools.
  - The shadow map is redrawn only when something changed.
- **One system per file**, each with a header comment: what it is, what it costs, where its design lives.

### 1.2 Modules

| Layer | File | What it does |
|---|---|---|
| Platform | `main.cpp` | Window, DPI, fixed-step loop (60 ticks/s, at most 5 per frame), frame cap |
| | `input.h/.cpp` | Actions ↔ keys/mouse (a remappable table), mouse look |
| | `persist.h/.cpp` | Settings file, local paths (Documents\My Games\\<game>\\) |
| Core | `common.h` | Maths, hash |
| | `noise.h` | Value noise, fbm (generator, placement, textures) |
| | `profiler.h/.cpp` | F3 timings, boot timeline, Ctrl+F3 report, GPU timings |
| **Simulation** (pure, tested) | `terrain.h/.cpp` | Density field; 2 m hand-cut facets; blasts; ground queries; raycasts. A first draft is in `prototypes/terrain_draft/` |
| | `props.h/.cpp` | Trees and rocks: placement from the generator, health, felling, shattering |
| | `debris.h/.cpp` | Fixed pool of shards, splinters and clods: fly, bounce, settle, fade; hurt the mech |
| | `sun.h/.cpp` | Sun direction; "is this point in direct sun?" (rays through terrain and props) for solar charging |
| | `mech.h/.cpp` | Movement (walk, strafe, jump, boost), ground contact, energy, heat, damage, shield |
| | `weapons.h/.cpp` | Weapon definitions, switching, lock-on, rockets (projectiles), machine gun (hitscan with tracers) |
| | `target.h/.cpp` | The practice target: parts, health, destroyed state |
| | `events.h` | The per-tick list of what happened, read by everything below |
| Glue | `game.h/.cpp` | Owns the world, runs the tick in order, hands events on, pause/reset |
| **Presentation** | `render.h/.cpp` | D3D11 device, shader cache, passes, frustum culling |
| | `shaders.h` | All HLSL, checked by `tools/check_shaders.py` |
| | `terrain_render.cpp` | Chunk buffers (upload on version change), terrain pass, shadow pass |
| | `effects.h/.cpp` | Muzzle glow, tracers, explosions, dust, scorch (flash-safe) |
| | `cockpit.h/.cpp` | Cockpit frame and live dials, drawn in camera space in front of the world |
| | `hud.h/.cpp` | Holographic overlay: aim point, lock brackets, target state |
| | `ui.h/.cpp` | Text atlas (system font, rendered at load), F3 overlay, pause screen |
| **Audio** | `audio.h/.cpp` | XAudio2, voices, the music worker thread |
| | `sfx.h/.cpp` | Weapon, impact, footstep (per ground type) and mech sounds, positional |
| | `music.h/.cpp` | Later: the metal engine and how events feed it (after the composition study) |

### 1.3 The tick and the frame

**Each tick (1/60 s), in order:**
1. input to mech intent;
2. mech (move, ground, energy from sun);
3. weapons: fire, spawn rockets, trace bullets;
4. rockets fly and hit (terrain blast, props, target);
5. debris;
6. target;
7. events are handed on.

**Each frame:**
1. The tick loop.
2. Terrain meshing within a budget: at most N chunks, nearest first. A blast touches a few chunks, so it's rebuilt within a frame or two.
3. The shadow map, only if something changed or the mech moved far.
4. Render: sky, terrain, props (instanced), debris (instanced), effects, cockpit, HUD, UI.
5. Audio: the event-driven sounds; the music queue is filled on its own thread.

### 1.4 Budgets for the modest machine

| Item | Budget |
|---|---|
| View distance | about 320 m to start; far chunks at coarser facets later |
| Terrain triangles | about 150–250k in view on flat ground |
| Draw calls | chunks in view (about 300–450) plus a handful of instanced batches |
| Terrain meshing | ~4 chunks per frame; a chunk meshes in well under a millisecond |
| Debris | pool of 2,048 pieces |
| Effects | pool of 512 |
| Props | instanced; a few thousand trees and rocks in view |
| Sun test | 4 rays per tick at the mech's panels, ~60 steps each |

---

## 2. What to carry from the Voxistics archive
All of this is our own code. Each piece comes over deliberately, adapted, with its tests.

**Carry (engine, proven):**
- **Window and loop:** per-monitor DPI, resize, borderless fullscreen (F11), fixed step with the 5-tick cap, frame limiter. (main.cpp)
- **D3D11 set-up and the shader cache.** Hashed bytecode files, 4 compile threads, stale files removed. This keeps boot fast. (render.cpp)
- **Profiler:** F3 overlay, boot timeline, Ctrl+F3 performance report (local file, path shown), GPU timestamp timings. (profiler.*, render.cpp)
- **Settings and key remapping:** the `{key → action}` table, mouse sensitivity and inversion, FOV, volumes. (persist.*, game.cpp input parts)
- **UI basics:** system-font text atlas rendered at load, batched quads, pressable buttons, pause/options screens. (render.h UI, game.cpp)
- **Sky and light:** atmosphere, fog, tonemap, sRGB; the shadow map redrawn only when stale; bloom (for explosions and muzzle glow).
- **Audio engine:** XAudio2 set-up, the music worker thread, synth kit, harmony-locked sound palette (`sfx_synth`), stereo positional sounds, the photosensitivity-safe envelope (`musiclevel.h`).
- **Tools:** `tests/run.sh` with Windows stubs, `check_shaders.py`, `check_msvc.sh`, the offline preview renderers.

**Adapt:**
- **Texture pipeline:** `.vtex` files, generator scripts (`natural_textures.py`), surface maps (height to normal). For this game, textures are projected from the world (triplanar), and tiles are randomly turned and shifted (DESIGN §2).
- **Faceted-prop hull code** (Voxistics 4.15): becomes trees, rocks and their fracture pieces.
- **Music composition engine** (`music_synth`): its structure (sections locked to a clock, layers entering and leaving, harmony-locked sounds) is the scaffold for the metal engine. The sounds and the composition are new.
- **Save format** (versioned, crash-safe, modified chunks only): later. The first test doesn't need saving.

**Leave in the archive:** the block registry and cube mesher, The Line, pulse logistics, fliers, essence, the block library and hotbar, the old sky clock and day song.

---

## 3. What the seed files offer this game
All four were read in full. The detailed review, with every flaw found, is in `docs/SEED_REVIEW.md`. In short:
- **drillder.cpp:**
  - ground that resists (hardness per ground type; a drill weapon later);
  - digging yields material;
  - a three-slice debug view;
  - indestructible markers.
  - Flaws: moves at the keyboard's repeat rate; vertical-slice clicks are upside down; pitching loses the heading.
- **LG2.cpp:**
  - fire that spreads and leaves burnt trees;
  - fuel that makes fire leap;
  - chain-reacting explosives;
  - craters filling with water;
  - regrowth with density caps.
  - Flaws: dangling pointers into a vector that reallocates, erasing inside a range-for, and duplicate cells. All crash-level.
- **Prismative.cpp:**
  - foreground equipment attached to the camera (the ancestor of the cockpit's weapons);
  - the manager split;
  - pause frees the cursor.
  - Flaws: one draw call per block; the camera at the feet; the tool on the wrong side; refuses to start if a texture is missing.
- **cc_2_2_2.cpp ("Color Creep"):**
  - 128 wired pattern generators plus about 20 unused ones;
  - a flip, rotate, mask and composite toolkit (the owner's version of random tile turns);
  - layered composition (base, scatter, overlay);
  - a curated palette of about 70 colours;
  - per-pixel formula patterns and Truchet tiles that suit shaders directly.
  - Flaws: colours were never configured (placeholders); regular grids that repeat visibly; clock-seeded randomness; a colour overflow; the unused generators break at 32 px.

Anything taken is re-implemented from the idea and bug-checked; nothing is copied.

## 4. The first prototype: a meaningful test

### 4.1 What it must answer
1. **Does piloting feel good?** Weight, speed, jump and boost, looking around from the cockpit.
2. **Is destruction fun?** Rockets cratering the ground and blowing holes through it, machine guns felling trees, rocks shattering, debris that can hurt you.
3. **Does solar energy make you think?** Choosing sun over shade, and tunnels that starve you.
4. **Does the look work at speed** on the modest machine? Faceted ground, ground-type clumps, textures, frame times in F3.

### 4.2 What the owner asked for
- A flat test world with clumps of different grasses and dirts.
- The mech can move, strafe, jump and boost, fire, switch weapons, and toggle weapon lock.
- Rockets destroy most things eventually. Machine guns mow down trees and plants and harm enemies.
- Destruction can hurt the mech if it's unshielded.
- Solar energy for jump and boost, charged only in direct sun.
- A cockpit with live dials, distinct from the holographic HUD.
- A target to lock on to, fire missiles at, machine-gun, and eventually destroy. It doesn't fire back.
- Different ground makes different sounds.

### 4.3 Proposed additions (so the test answers its questions)
- **Things to wreck and measure scale by:** trees (8–12 m) and rocks scattered through the clumps. Without them, a flat world gives no sense of size or speed, and machine guns have nothing to mow.
- **Weight and feel:**
  - small cockpit sway from footsteps;
  - a shake from nearby blasts;
  - the weapons visible in the foreground, recoiling when they fire.
  - Slow and smooth only, nothing that flashes.
- **A shield** so "unshielded" means something. A toggle that draws solar energy while up. That puts energy under a second demand besides jump and boost, which is a real choice.
- **Ground hardness** from drillder: soils crater easily; rock resists (smaller craters).
- **Simple sound effects for everything** (owner: from the systems of the previous project): fire, impact, blast, footsteps per ground, boost, lock tone. All come from Voxistics' sound palette (`sfx_synth`, harmony-locked), so they already sit together. The music engine waits for the composition study.
- **Weapon sprites** (owner: from the same systems): the foreground guns and launcher, and muzzle glow, drawn by the procedural texture pipeline (generator scripts, `.vtex`, surface maps for light and shine), layered in cc_2_2_2's manner. Attached to the view like Prismative's tool, on the correct side, with recoil and sway.
- **Debug:**
  - F3 profiler;
  - a key to reset the field;
  - a key to toggle an outside view of the mech (for tuning only);
  - an infinite-energy toggle.
- **Deferred:** regrowth (LG2), saving, menus beyond pause, enemies that fire back, the music engine, hills.

### 4.4 Test checklist (for the owner's playtest)
1. Walk, strafe, jump and boost across the clumps. Does it feel heavy but quick? Is the scale right from the cockpit?
2. Look at the ground clumps at speed. Do they read? Any visible repetition?
3. Machine-gun a line of trees. Do they fall satisfyingly? Does grass get shredded?
4. Rocket the ground: craters, then a hole through into a pit. Grass stays on the rim, with soil and rock inside?
5. Rocket a rock close by. Does the debris hurt you unshielded, and not with the shield up?
6. Stand in a tree's shadow, then in a crater, then in the open. Does the charge dial respond, and does boost run out when you're starved?
7. Lock on to the target, fire rockets and switch to guns. Does it break apart in stages and end destroyed?
8. F3 during heavy destruction: frame times, meshing, debris count. Any hitches?

---

## 5. Build order
Each step is committed, tested and runnable before the next.
1. **Foundation:** window, loop, input, settings, profiler, D3D11 and shader cache, sky, debug text. (From the archive, adapted.)
2. **Terrain:** density, 2 m facets, clumps, blasts, chunk rendering, shadows. The draft exists; native tests.
3. **Mech:** movement, cockpit camera, ground contact, sun test, energy, shield. Native tests.
4. **Props and debris:** trees and rocks, felling and shattering, the debris pool, damage to the mech.
5. **Weapons and target:** rockets, machine gun, switching, lock-on, the target and its stages.
6. **Cockpit and HUD:** dials (energy, charge, heat, damage, shield) and the holographic overlay.
7. **Sound:** placeholder palette through the event list, footsteps per ground.
8. **Textures:** world-projected, per ground type, with random tile turns and shifts.
9. **Playtest build** for the owner, with the checklist above.

---

## 6. Owner's answers (September 2026)
- **Props:** trees, rocks and plants built from our primitives.
- **Mech:** large, with its view above the treeline.
- **Shield:** powered; it draws energy.
- **Sun:** the one-hour day, moving.
- **Target:** a slow walking wanderer.
- **Title:** "Cacophony".
- **Fire:** in the first test. Rockets and explosions start weak fires, and flammable ground and props burn.

## 7. The questions as asked
1. **Mech scale and speed.** Eye height about 4 m (person-sized-and-up)? Walking at about 8 m/s, boosting to about 25 m/s?
2. **Shield.** Is a toggle that draws solar energy right, or should the shield be passive and recharge on its own?
3. **The sun.** Fixed for the test (simplest), or a slow day cycle, so charging changes over time?
4. **The target.** A static structure, like a tower or turret with parts that break off in stages, or a slow walker that just wanders?
5. **Title.** A working name for the window and the save folder? (It can stay "untitled" for now.)
6. **Fire and regrowth** from LG2: in the first test, or next?
