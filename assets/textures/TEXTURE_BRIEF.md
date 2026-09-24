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
- Opaque only for now: no transparency.

## Art guidance for this engine

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

Feel free to propose additional blocks that suit my art, using the same
format with a new name plus a one-line description of what each is. The
engine side adds new block types easily.

Please start with `stone`, `dirt` and `wood` so I can see the style in-game
before we do the rest.
