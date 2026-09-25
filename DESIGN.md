# Design (working, no title yet)

This is the design of record for the new game, built up from the owner's decisions as they're made. Proposals are marked **(proposed)** until the owner confirms them.

---

## 1. The shape of it
- The player pilots a **mech suit** across a large, **fully destructible** world.
- **Destruction is fun, and it has a cost.**
  - Trees and rocks get pulverized.
  - Blasts, debris and flying ground can hurt the mech if it isn't shielded.
- Digging into the earth matters. That is why the world is voxels.
- **Scale is felt from the cockpit.** The mech is perceived as roughly person-sized-and-up. How big it feels comes from:
  - its eye height above the ground;
  - the viewport area;
  - the weapons and effects in the foreground.

  The terrain stays large and dynamic around it. Voxel size relative to eye height is one tuning setting.
- Movement is fast, like combat. Rendering must hold up at that speed.

## 2. The world's look
Chosen: **faceted smooth.** The ground is stored as voxels, and its surface is drawn as flat-shaded facets that follow the true shape of the land, not cubes. The study is in `prototypes/voxel_shapes/` (three rounds).

- **Only the surface is ever built.** What's underneath is stored but not meshed until something exposes it, such as a dig or a missed rocket. A blast removes a sphere of density, and only the chunks it touched re-mesh. Cost follows damage, not world size.
- **Method (proposed):**
  - one vertex per cell (surface nets), each quad split on its shorter diagonal;
  - vertices slid along the ground by a fixed hash for a hand-cut look. That keeps the slopes, and so the materials, intact.
  - **Facet size: 2 m** (owner: "pick the one that performs the best"). That is about 0.2x the triangles of cubes for the same ground. Craters from rockets are wider than a cell anyway.
  - Larger facets far away (level of detail).
- **Materials depend on facet orientation and depth** (owner).
  - Up-facing facets near the surface are grass. Steep or freshly exposed facets are dirt, and deeper ones rock.
  - So grass sits on top of dirt realistically, and a crater's rim keeps its grass while its bowl shows soil and stone.
  - In the engine, voxels store a material. Each facet's texture is chosen by the material, the facet's normal and its depth.
- **Textures (proposed; owner: "this is where we can really mess with the textures").** A flat texture on a flat facet reads as flat and repeats visibly, so:
  - Project textures from world position (triplanar), never per facet: no seams, no stretching.
  - Randomly rotate and offset each texture tile by a hash, so repeats don't line up.
  - Add broad, slow variation across the land on top of the fine detail.
  - Show detail up close and fade it with distance.
  - Keep a per-facet tint (small hash variation) so neighbouring facets read apart.
  - All of this is per-pixel arithmetic: no extra passes, no per-voxel data.
- **Light:**
  - sun shadows, which also feed solar charging (§4);
  - ambient occlusion read from the density field;
  - no outlines.
- **Ground variety (owner).** The starting map has clumps of several grass and dirt types, like the archive's patchwork plain, so the look can be judged side by side. Each voxel sample carries its ground type; each type has its own texture and its own footstep sound.
- **Props:**
  - Trees and rocks are low-poly objects, not voxels.
  - When destroyed, they break into their own facets: shards, splinters, a stump.

## 3. Weapons and destruction
- **Rockets** can eventually destroy almost anything: terrain (craters, holes through the ground), trees, rocks, targets.
- **Machine guns** mow down trees and plants and harm enemies, but barely scratch terrain.
- Other weapons and abilities will come along the way.
- **Weapon locking** can be toggled.
- **Damage to the mech:** blasts too close and flying debris hurt it, unless it's shielded.
- **Photosensitivity:** muzzle flash, explosions and warning lights never flash faster than 3 times a second. Sustained fire reads as a steady glow with a slow pulse, not a strobe.

## 4. The mech
- **Controls:** fire, switch weapons, toggle weapon lock, move, strafe, jump, boost.
- **Energy: solar, for now.**
  - Jumping and boosting draw on energy.
  - Energy recharges only in **direct sunlight on the mech**. Shadow from trees, cliffs, tunnels or your own craters blocks charging.
  - (proposed) The test is a few samples of the sun's shadow at the mech's panels, taken each tick from the same shadow data that lights the scene, so it costs almost nothing.
  - Play shows it: step into the open to charge; a tunnel is shelter that starves you.

## 5. Sound and music
- Different ground makes different sounds underfoot (§2).
- **Music to a beat, heavy-metal intensity (owner).** Like Voxistics' techno track, but in the idiom of death metal. First we study how the genre is composed and organized, as we did before building the techno track: riff-based form, tempo and feel changes, blast beats and double-bass drumming, low tunings and dark modes. Then we build from that understanding.
- **Actions play the music (owner).** What the player does adds to the music in close sync:
  - each action keeps its own sound;
  - that sound also means something in the composition and in the flow of combat.
  - (proposed) For example, the music can place machine-gun fire on the drums' subdivision, land a rocket's impact on a downbeat accent, roll the double bass while boosting, and cue a riff change on a lock-on. The sound is quantized; the action never waits.
  - Voxistics' synth and harmony locking (sounds quantized to the beat and drawn from safe pitch sets) are in the archive to build on.
- **Not a rhythm game (owner).** There's no bonus for shooting or moving on the beat, and nothing roguelike about it. The beat is there to turn the chaos of destruction and victory into something with melody and meaning. The music follows the player; the player never has to follow the music.
- Nothing from existing songs: no riffs, melodies or recognisable patterns.

## 6. Displays
- **Cockpit** (foreground, physical): the mech's own state on live dials, needles and lamps. Energy, charge (sun on the panels), heat and damage all go here. No numbers where a needle or band will do.
- **Holographic HUD** (projected): the world and targets, such as lock-on brackets and the aim point.
- The two stay distinct.

## 7. First playable test
1. The look settled: facet size and textures.
2. The terrain, with digging, on a **flat test world** (owner). The world is flat for now, with **2 m hand-cut facets** (owner: the best performer).
3. The mech: movement, strafe, jump, boost; solar energy.
4. A cockpit with its dials.
5. A target that doesn't fire back:
   - lock on and fire missiles;
   - machine-gun it;
   - blow holes through the ground around it;
   - eventually destroy it.
   - Switch weapons throughout.

No enemies that fire back yet.
