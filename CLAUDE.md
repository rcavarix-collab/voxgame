# Voxistics — working notes for Claude

The design of record is `DESIGN.md`; read the relevant part before changing a system.

## What the game is reaching for
The owner's touchstones: Minecraft (the block world, building), Satisfactory and FactoryTown (production chains, logistics), BuildCraft and IndustrialCraft (machines, pipes, power), Equivalent Exchange (transmutation by value), Sandustry (simulated materials that fall, flow and react). The shape: a factory game grounded in a living, simulated world, with The Line and the essence network as the strange layer on top — essence is the natural candidate for a transmutation economy. Theme: industry isn't the monster — its byproducts are real, and running it well keeps the land healthy (DESIGN.md Part XX). The threat is abstract, not combat: neglected land opens rifts of creeping ooze with real loss, pushed back by management and light. Take their *mechanics* (genre ground); never their names, terms, items or art.

## Standing priorities
- **Lagless efficiency.** Cost scales with what's on screen or what changed, never with world size. Budget per-tick work; rebuild only on change; measure with the F3 profiler.
- **Fake it convincingly, cheaply.** Visual effects are per-pixel tricks driven by small per-frame constants, not extra passes or per-block data (DESIGN.md 4.8–4.12).
- **Photosensitivity.** Nothing flashes faster than 3 times a second (musiclevel.h shows how).
- **Nothing anyone owns — no stepping on toes.** Everything in the game is original or genuinely free to use:
  - no brands, logos, trademarks, product or company names, real currencies or crypto symbols, official insignia or emblems;
  - no copyrighted art, music, melodies, text or characters, and nothing recreated from another game (its textures, creatures, item names, distinctive look or UI art) — shared genre mechanics are fine, their specific expression is not;
  - traditional public-domain motifs (knotwork, florals, geometric and sacred-geometry figures) and natural materials are fine;
  - code: the seed files are the owner's own; outside code is read for ideas only (DESIGN.md Part VIII — nothing copied, no copyleft); fonts are the player's installed system fonts, rendered at load, never shipped.
  When unsure, make it more original rather than less.
- **No numbers in player-facing displays** where a band or feel will do; keep debug UI minimal (F3, F7, F8).

## The seed files — consult them first
Prismative.cpp, drillder.cpp, LG2.cpp and cc_2_2_2.cpp (repo root) are the owner's hand-tested prototypes. Before designing a feature, texture, block behaviour or view, check them for an idea that already works — take the idea, never copy the code. DESIGN.md Part VIII indexes what each still offers: cc_2_2_2.cpp's 145 texture generators are the technique library for new procedural textures (see `tools/natural_textures.py` for how a set is made); LG2.cpp's cellular-automaton behaviours (spreading grass, fire, water, trees) map onto the scheduled-update queue.

## Building and checking
- The owner builds with Visual Studio (x64, C++17, SDL checks on). Debug builds optimise the hot loops per file (Voxistics.vcxproj); judge performance in Release.
- Native tests: `bash tests/run.sh` (no Windows needed; stubs in tests/stub).
- Shaders: `python3 tools/check_shaders.py` (glslangValidator HLSL front end, every variant).
- New .cpp files go into Voxistics.vcxproj (and .filters) — the project file is the source list.
- Sound: the world sound palette (docs/SOUND_PALETTE.md) is harmony-locked to the music; any new sound goes through `sfx_synth.cpp` with pitches from the safe sets, and `bash tools/sound_demo.sh analyze` must stay clean (demo WAVs: `tools/sound_demo.sh demo DIR`).
- Art: `.vtex` files in assets/textures (spec: assets/textures/TEXTURE_BRIEF.md). Natural set: regenerate with `python3 tools/natural_textures.py`.
