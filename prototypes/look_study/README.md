# Look study (parked until the prototype runs)

The owner felt the first faceted terrain looked like other smooth-voxel games (7 Days to Die, Astroneer). The terrain technology stays; the look should become Cacophony's own. This folder is for trying looks offline, with the game's real terrain code (see tools/terrain_preview), before anything reaches the shaders.

## Directions to try
- **Engraving:** hatched ink on warm paper, like an old field manual (fits the tractor cab).
- **Cracked plates:** ground split into irregular slabs with dark seams (dried mud, basalt).
- **Halftone print:** ink dots on cream paper, like an old printed map.
- **Banded light:** a few hard light bands, warm sun and cool shadow.

## Weathering, as model painters and CGI artists do it (owner)
Convincing materials come from weathering, and most of the standard techniques are cheap per-pixel shading from data we already have:
- **Washes:** dark grime settling into crevices and hollows, driven by the openness (AO) value the mesher stores.
- **Dry-brushing:** raised edges and up-facing surfaces catch a lighter tone.
- **Dust and deposits** on top faces, from the facet normal (n.y).
- **Streaks** running down walls, trunks and armour: world-space vertical noise below edges.
- **Edge chipping and wear** on hard materials (rock, metal): brighter, rougher edges.
- **Filters:** subtle colour variation across a surface, the painter's glaze.
- **Rust, soot and scorch:** already started with the scorched ground around blasts.

These apply across the terrain, the props, the mech and the cockpit, so one set of shader functions can serve all of them.
