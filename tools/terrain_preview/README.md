# Terrain preview

Renders Cacophony's real terrain (terrain.cpp, sun.cpp) offline, with the same shading as the game's shaders (shaders.h), from the mech's eye height. Use it to judge the ground's look without a Windows run. When the terrain shader changes, keep `preview.cpp` in step with it.

```
g++ -O2 -std=c++17 -I../.. preview.cpp ../../terrain.cpp ../../sun.cpp -o preview && ./preview OUTDIR
```
