# Voxistics block-texture brief

Paste everything below the line into the art conversation. It is also the
format spec the engine's texture loader reads, so the two stay in step:
anything written to this spec drops into `assets/textures/` and loads with
no conversion.

---

I'm building a voxel game (Minecraft-scale cubes, first person, point-sampled
pixel textures, no smoothing). I'd like you to turn my art style into block
textures for it. The engine reads a plain-text texture format, described
below. Please follow it exactly, since a malformed texture is rejected and
the engine falls back to a placeholder.

## Output format (`.vtex`)

Give each file as its own fenced code block, with the file name on the line
above it (for example `stone.vtex`). A file may hold any number of `texture`
and `block` entries, and every `.vtex` file in the folder is loaded.

```
# Lines starting with # are comments. Indentation is optional.

texture stone                 # name: lowercase a-z, 0-9, _
size 16                       # square, 16 or 32 pixels per side
palette
  a 7c7c82                    # one key character, then 6-digit hex RGB
                              # (or 8 digits, RGB + alpha, for see-through blocks)
  b 6a6a70
  c 8f8f95
pixels                        # exactly `size` rows of exactly `size` keys
  aabacaabaaacabaa
  ...                         # (16 rows in total for size 16)
end

block stone                   # must be one of the block names listed below
  all stone                   # texture used on every face
end

block chest
  all   chest_side            # fallback for any face not named below
  top   chest_top
  front chest_front           # the face that looks toward the player on placement
end
```

Rules:
- Faces you can name in a `block` entry are `all`, `top`, `bottom`, `side`
  (all four sides) and `front`. The more specific name wins: `front` beats
  `side`, and `side` beats `all`.
- A palette key is any single printable character except space and `#`.
  Keys only need to be unique within their own texture. Around 16 colours
  or fewer per texture reads best.
- Every pixel row must be exactly `size` characters with no spaces, there
  must be exactly `size` rows, and every character must be in that
  texture's palette. Please re-check row counts and lengths before sending.
- Keep all textures in one set the same size. 16 is recommended; if you use
  32, use it everywhere.
- Transparency is only for see-through blocks (`glass`, `crystal`): give
  those palette entries 8 hex digits, `rrggbbaa`, where `aa` is how solid
  the pixel is (`00` invisible, `ff` solid). Glass reads best mostly clear
  (`aa` around `20`–`40`) with a firmer frame; everything else stays opaque
  (6 digits), and alpha on an opaque block is ignored.

## Art guidance for this engine

- **Nothing anyone owns.** No logos, brand marks, trademarks, real
  currencies or crypto symbols, company or product names, or recognisable
  characters from other works. Original motifs, natural materials and
  traditional public-domain patterns (knotwork, florals, geometric
  ornament) are all fine.
- **No regular dither.** Don't fill a background with a repeating cycle
  of keys (`abca` / `cabc`, a checkerboard). Up close it reads as woven
  fabric; at a distance it turns into shimmering diagonal lines across the
  landscape. Build backgrounds from irregular clumps of 2–4 pixels in 2–4
  close tones.
- **No details in fixed spots.** Every tile repeats on every neighbouring
  block, so a flower, crystal, knot or hole in the same place on each tile
  becomes a perfect grid across a wall. Scatter details unevenly, vary
  their size, and think of the tile as a random patch of a larger surface.
- **Lines wrap.** Cracks, veins, strata and grain must leave one edge and
  re-enter at the matching point of the opposite edge, so they continue
  unbroken across blocks. A line that stops short of the edge becomes a
  row of loose dashes when tiled. Strata and bands: one set per tile, in
  uneven thicknesses (a band that repeats every 8 pixels reads as ruled
  paper on a cliff).

- **Tile seamlessly.** Each texture repeats on every neighbouring block, so
  its left edge must continue into its right edge and its top edge into its
  bottom edge. Only use a drawn border where a block should visibly read as
  a separate piece (the foundation and machine are framed, for example);
  on natural blocks a border becomes a grid across the whole landscape.
- **No baked light direction.** The engine shades each face by direction and
  darkens corners where blocks meet (ambient occlusion). A highlight painted
  on one edge will look wrong once tiled and shaded, so keep any shading
  even and texture-level only.
- **Leave headroom.** Faces get darkened by up to about 40%, so avoid pure
  black and pure white. A range of roughly `141414` to `f0f0f0` works well.
- **Readable at a distance.** Favour mid-sized clusters of 2–4 pixels over
  single-pixel noise everywhere. Busy one-pixel noise shimmers on distant
  blocks.
- **Orientation.** For side faces, row 0 is the top (toward the sky). Top and
  bottom faces may appear rotated, so avoid designs there that only work one
  way round.

## Blocks the engine has today

| name         | what it is                                   | faces worth drawing        |
|--------------|----------------------------------------------|----------------------------|
| `foundation` | structural block: never falls and always holds up what's above it; also the world's bottom layer | `all` (a framed, engineered look fits) |
| `stone`      | natural rock, the bulk of the underground      | `all`                      |
| `dirt`       | the top soil layers                          | `all`; optionally a `top`  |
| `wood`       | building material (planks or log, your call) | `all`, or `side` + `top` for a log's end grain |
| `chest`      | storage container                            | `side`, `top`, `front` (latch or lock) |
| `machine`    | generic machine placeholder                  | `side`, `top`, `front` (control panel) |
| `tube`       | a thin bar block (pipes, rails, posts)       | `all`                      |
| `music_block` | glows in time with the music playing        | `all` (it lights up on its own; mid tones glow best) |
| `timestream_block` | lights up while the time line passes through it | `all` (pale, calm) |
| `essence_attractor` | draws essence from the land around it  | `all` (a core in a frame)  |
| `glass`      | clear, see-through glass                     | `all` (mostly transparent, see alpha above) |
| `crystal`    | tinted, cloudier see-through crystal         | `all` (semi-transparent colour) |
| `snow`       | snow                                         | `all` |
| `sand`       | loose sand                                   | `all` |
| `sandstone`  | layered sedimentary rock                     | `side` (strata), `top` / `bottom` (smooth) |
| `cracked_earth` | sun-baked ground split into plates        | `all` (the cracks must wrap) |
| `clay`       | smooth reddish clay                          | `all` |
| `basalt`     | dark volcanic stone                          | `all` |
| `magma_rock` | dark stone split by glowing lava veins       | `all` (veins must wrap) |
| `log`        | a tree trunk                                 | `side` (bark), `top` / `bottom` (rings; these needn't tile) |
| `moss`       | a soft moss cushion                          | `all` |

The current art for the natural blocks is `natural.vtex`, generated by
`tools/natural_textures.py` from palettes of an earlier batch; anything
authored for the same block names replaces it.

Feel free to propose additional blocks that suit my art, using the same
format with a new name plus a one-line description of what each is. The
engine side adds new block types easily.

Please start with `stone`, `dirt` and `wood` so I can see the style in-game
before we do the rest.
