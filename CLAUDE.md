# Voxistics — working notes for Claude

The design of record is `DESIGN.md`; read the relevant part before changing a system.

## Standing priorities
- **Lagless efficiency.** Cost scales with what's on screen or what changed, never with world size. Budget per-tick work; rebuild only on change; measure with the F3 profiler.
- **Fake it convincingly, cheaply.** Visual effects are per-pixel tricks driven by small per-frame constants, not extra passes or per-block data (DESIGN.md 4.8–4.12).
- **Photosensitivity.** Nothing flashes faster than 3 times a second (musiclevel.h shows how).
- **Nothing anyone owns.** No brands, logos, trademarks, real currencies, or others' characters in art or names.
- **No numbers in player-facing displays** where a band or feel will do; keep debug UI minimal (F3, F7, F8).

## The seed files — consult them first
Prismative.cpp, drillder.cpp, LG2.cpp and cc_2_2_2.cpp (repo root) are the owner's hand-tested prototypes. Before designing a feature, texture, block behaviour or view, check them for an idea that already works — take the idea, never copy the code. DESIGN.md Part VIII indexes what each still offers: cc_2_2_2.cpp's 145 texture generators are the technique library for new procedural textures (see `tools/natural_textures.py` for how a set is made); LG2.cpp's cellular-automaton behaviours (spreading grass, fire, water, trees) map onto the scheduled-update queue.

## Building and checking
- The owner builds with Visual Studio (x64, C++17, SDL checks on). Debug builds optimise the hot loops per file (Voxistics.vcxproj); judge performance in Release.
- Native tests: `bash tests/run.sh` (no Windows needed; stubs in tests/stub).
- Shaders: `python3 tools/check_shaders.py` (glslangValidator HLSL front end, every variant).
- New .cpp files go into Voxistics.vcxproj (and .filters) — the project file is the source list.
- Art: `.vtex` files in assets/textures (spec: assets/textures/TEXTURE_BRIEF.md). Natural set: regenerate with `python3 tools/natural_textures.py`.
