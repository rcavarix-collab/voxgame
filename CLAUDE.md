# Working notes for Claude

A new game, started September 2026. No title yet (the owner will name it). The design of record is `DESIGN.md`; read the relevant part before changing a system.

## The game so far
- The player drives a **mech suit** across a **fully destructible voxel world**. Digging into the earth matters to the owner; that is why the world is voxels.
- **Two layers of display:**
  - the **cockpit**: physical controls and live dials, in the foreground;
  - the **holographic HUD**, projected in front of it.
  Keep them distinct: the cockpit shows the mech's own state; the HUD shows the world and targets.
- **First playable milestone:**
  - a simple cockpit with mech controls and active dials;
  - one target that doesn't fire back: lock on and fire missiles, test machine-gun fire, destroy it.
  - No enemies that shoot back yet.
- **Before that milestone:** lock in the look. The owner chose faceted smooth terrain (`prototypes/voxel_shapes/`). Facet size and textures are next.
- Mech movement is fast, like combat. Rendering must hold up at speed, so cost per chunk and mesh rebuild time are design inputs, not afterthoughts.
- Not this game: time distortion, The Line, pulse logistics. Those were Voxistics.

## Layout
- `archive/voxistics/`: the previous game, frozen (its last live commit is d081e19). It still builds and its tests still pass.
  - Read it for proven pieces worth bringing over deliberately: shader cache, F3 profiler, boot timeline, GPU timing, sfx and music synths, save format.
  - Bring a piece over on purpose, with tests. Never copy the whole thing.
- `prototypes/`: offline experiments (renders, studies). Nothing in the game depends on them.
- Seed files at the root: Prismative.cpp, drillder.cpp, LG2.cpp, cc_2_2_2.cpp. These are the owner's hand-tested prototypes; consult them first.
  - Take ideas, never code.
  - cc_2_2_2.cpp's texture generators are the technique library for procedural textures.
  - drillder.cpp is a digging prototype.

## Standing priorities (carried over from Voxistics; they're the owner's, not the old game's)
- **Target machine: an outdated, modest Windows PC** (the owner's own). Design every feature to run well there. Never read or report the player's hardware (no GPU/CPU/spec queries); assume the modest machine instead.
- **Privacy.** No telemetry, analytics, crash reporting, update checks or any network use. Nothing about the player or their machine is collected or leaves it. Debug aids act only when pressed and write only local files.
- **Lagless efficiency.** Cost scales with what's on screen or what changed, never with world size. Budget per-tick work, rebuild only on change, and measure with a profiler.
- **Fast boot.** Nothing slow happens at launch that could be cached, deferred or done once.
- **Organisation and careful annotation.** One system per file, and the project file is the source list. Every system opens with a header comment saying what it is, what it costs and where its design lives. Non-obvious lines say what they guard against.
- **Fake it convincingly, cheaply.** Visual effects are per-pixel tricks driven by small per-frame constants, not extra passes or per-block data.
- **Photosensitivity.** Nothing flashes faster than 3 times a second. This covers muzzle flash, explosions, warning lights and HUD blinks.
- **Nothing anyone owns: no stepping on toes.** Everything is original or genuinely free to use.
  - No brands, logos, trademarks, product or company names, real currencies or crypto symbols, or official or military insignia.
  - No copyrighted art, music, text or characters, and nothing recreated from another game or show: no mech designs, cockpit layouts, HUD art or names from existing franchises. Shared genre mechanics are fine; their specific expression is not.
  - Traditional public-domain motifs and natural materials are fine.
  - Outside code is read for ideas only (nothing copied, no copyleft).
  - Fonts are the player's installed system fonts, rendered at load, never shipped.
  - When unsure, make it more original.
- **Minimal text.** Show, don't tell. Keep words few and plain. English only for now.
- **No numbers in player-facing displays** where a band, needle or feel will do. Dials suit this.

## Scope
Voxistics' MoSCoW sheet is archived with it. A new sheet starts once the concept is settled. Until then, new ideas are proposals and the owner decides.

## Building and checking
- The owner builds with Visual Studio (x64, C++17, SDL checks on). Judge performance in Release.
- The archive's checks still run: `bash archive/voxistics/tests/run.sh` and `python3 archive/voxistics/tools/check_shaders.py`.
