# Voxel shape study

The same hillside (a tunnel cut in, a pit dug on top), built from different cell shapes and rendered offline. It's used to choose the world's look before any engine work.

- `shapes.cpp` (round one): cubes, hex prisms (full and half height), triangle prisms, rhombic dodecahedra and truncated octahedra. Each is a solid/empty cell; a face is drawn where a solid cell meets an empty one.
- `shapes2.cpp` (round two): cubes split into square pyramids; cubes split into tetrahedra; the octahedra-and-tetrahedra honeycomb; then three ways of drawing a stored voxel world with slopes:
  - sloped blocks: marching tetrahedra on block corners;
  - faceted smooth: marching tetrahedra on a stored density;
  - smooth: surface nets on the same density.
- `shapes3.cpp` (round three): faceted terrain done well. Six ways to cut the same density into facets, with per-facet colour by slope and depth, a sun shadow map, ambient occlusion from the density, and destruction: craters (one through the tunnel roof), trees, rocks, a pulverized tree and a shattered rock. The owner chose faceted smooth.
- `compare_*.jpg`, `round2_*.jpg` and `round3_*.jpg` are the sheets. `cockpit_height.jpg` shows the lead candidates from about 4 m up.
- Each panel's label gives the triangle count relative to cubes, for the same ground.

Only the surface is ever drawn. The ground beneath is known but not meshed until something (a dig, a missed rocket) exposes it.

Build: `g++ -O2 -std=c++17 shapes2.cpp -o shapes2 && ./shapes2 OUTDIR` (and the same for shapes.cpp). Then `comp.py` makes the round-one sheets, and `comp3.py OUTDIR` makes the round-three sheets.
