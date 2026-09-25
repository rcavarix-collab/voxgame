# Cacophony: design (working)

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
- **The mech is large: its view is above the treeline** (owner). Trees (8–12 m) are below eye level, so the land reads as a landscape to stride across.
  - (proposed) Eye height about 15 m, walking about 10 m/s, boosting to about 30 m/s, and a jump of about 8 m.
  - A higher view sees farther, so view distance and far detail (coarser facets far away) matter from the start.
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
- **Craters** have a raised lip of thrown-up ground, with soil scattered over the grass around them and a scorched ring.
- **Ground variety (owner).** The starting map has clumps of several grass and dirt types, like the archive's patchwork plain, so the look can be judged side by side. They lie in organic meadows (large irregular regions with interpenetrating borders), smaller islands of another grass, and scattered bare patches of loam, clay and gravel. Each voxel sample carries its ground type; each type has its own texture and its own footstep sound.
- **Props:**
  - Trees, rocks and plants are built from our primitive shapes (owner): low-poly objects, not voxels.
  - When destroyed, they break into their own facets: shards, splinters, a stump.

## 3. Weapons and destruction
- **Rockets** can eventually destroy almost anything: terrain (craters, holes through the ground), trees, rocks, targets.
- **Machine guns** mow down trees and plants and harm enemies, but barely scratch terrain.
- Other weapons and abilities will come along the way.
- **Weapon locking** can be toggled.
- **Damage to the mech:** blasts too close and flying debris hurt it, unless it's shielded.
- **Fire** (owner):
  - Rocket blasts and explosions (the wanderer's, or the mech's own when it's destroyed) start weak fires.
  - Certain ground types and props are flammable and are destroyed as fire spreads: dry grass and trees burn, and trees leave charred trunks; soil, gravel and rock don't burn.
  - It spreads by a budgeted rule on a sparse set of burning cells, LG2's idea re-implemented safely (docs/SEED_REVIEW.md §2).
  - Fire glows and flickers slowly, never strobing.
- **Coal veins (owner):** coal seams in the ground that can accidentally be set aflame. A burning seam smoulders underground and destroys the coal, hindering the player's ability to obtain it, so digging near coal takes care.
- **Items and projectiles (owner):** item entities to collect weren't wanted, but may be needed for the finer points of the game; projectiles too.
  - Both are governed so they can never become too numerous or expensive.
  - Each comes from a fixed-size pool with a hard cap. Nearby items merge into one, old ones fade, and the oldest is recycled when the pool is full.
  - Cost stays bounded however wild things get.
- **Photosensitivity:** muzzle flash, explosions and warning lights never flash faster than 3 times a second. Sustained fire reads as a steady glow with a slow pulse, not a strobe.

## 4. The mech
- **Controls:** fire, switch weapons, toggle weapon lock, move, strafe, jump, boost.
- **Energy: solar, for now.**
  - Jumping and boosting draw on energy.
  - Energy recharges only in **direct sunlight on the mech**. Shadow from trees, cliffs, tunnels or your own craters blocks charging.
  - (proposed) The test is a few samples of the sun's shadow at the mech's panels, taken each tick from the same shadow data that lights the scene, so it costs almost nothing.
  - Play shows it: step into the open to charge; a tunnel is shelter that starves you.
- **Quartz crusher (owner's idea, to decide).** The mech may run on a crusher that gets energy from crystalline quartz it has to unearth. Squeezed quartz really does produce a voltage (the piezoelectric effect), so it's plausible old tech.
  - It ties power to digging: blast the ground open, expose quartz veins, scoop crystals into the crusher, and energy flows into the mech's power.
  - The crusher is visible from the cockpit: jaws grinding, a warm glow as it crushes (steady, never flashing).
  - Two ways to keep the sun in it:
    - **(a) Sun-charged quartz:** exposed crystals soak up sunlight through the day. Buried veins are weak and sunlit ones grow bright, so where and when you dig matters.
    - **(b) Both sources:** the panels trickle-charge in sun, and crushing quartz gives big bursts.
  - **Spent quartz is kept (owner):** the crushed grit maintains the potential of the mech's other devices (shield coils, weapons, sensors), so one dig feeds two needs.
  - The code keeps energy sources general, so either fits.
- **The shield is powered** (owner). While up, it draws energy, competing with jump and boost.
- **The sun moves: a one-hour day** (owner, like Voxistics). Charge follows the sun: strong at noon, long shadows and weak charge at dusk, none at night. Nights are for conserving energy.

## 5. Weather (owner)
- **Two kinds of fog:**
  - **cheap volumetric fog:** low ground fog, computed per pixel from height and distance, with no extra passes;
  - **wind fog:** mist and dust drifting with the wind.
- **Precipitation** in varying degrees.
- **Cloud cover that casts shadows** on the ground, drawn as a scrolling cloud-shadow pattern.
  - (proposed) Passing clouds also cut solar charge (the same shadow read by SunExposure), so weather matters to energy.
- All flash-free: no lightning strobes.

- **Sky and stars (owner):** prettier and more realistic, as long as it isn't expensive. Planned: per-pixel scattering-style sky colour and hash-placed stars in the single sky pass.

## 6. Sound and music
- Different ground makes different sounds underfoot (§2).
- **Music to a beat, heavy-metal intensity (owner).** Like Voxistics' techno track, but in the idiom of death metal. First we study how the genre is composed and organized, as we did before building the techno track: riff-based form, tempo and feel changes, blast beats and double-bass drumming, low tunings and dark modes. Then we build from that understanding.
- **Actions play the music (owner).** What the player does adds to the music in close sync:
  - each action keeps its own sound;
  - that sound also means something in the composition and in the flow of combat.
  - (proposed) For example, the music can place machine-gun fire on the drums' subdivision, land a rocket's impact on a downbeat accent, roll the double bass while boosting, and cue a riff change on a lock-on. The sound is quantized; the action never waits.
  - Voxistics' synth and harmony locking (sounds quantized to the beat and drawn from safe pitch sets) are in the archive to build on.
- **Not a rhythm game (owner).** There's no bonus for shooting or moving on the beat, and nothing roguelike about it. The beat is there to turn the chaos of destruction and victory into something with melody and meaning. The music follows the player; the player never has to follow the music.
- Nothing from existing songs: no riffs, melodies or recognisable patterns.

## 7. Displays
- **No words or symbols in play (owner).** Only the menus use text. The cockpit and HUD speak through needles, bands, lamps, shapes, motion and sound, so the mech is visual and intuitive to control. No letters, digits, icons-as-glyphs or labels on dials. (The F3 debug overlay is the one exception: it appears only when pressed, for tuning.)
- **Cockpit** (foreground, physical): the mech's own state on live dials, needles and lamps. Energy, charge (sun on the panels), heat and damage all go here. No numbers where a needle or band will do.
- **Holographic HUD** (projected): the world and targets, such as lock-on brackets and the aim point.
- The two stay distinct.

## 8. Setting (parked, owner: "a distracting tangent" for now)
**For now (owner):** the mech's interior is like the inside of an old tractor, so dieselpunk by default. The look and time period of the player and the mech are decided later; the terrain comes first. The notes below are kept for when the question returns.

The cockpit's look depends on the game's time and place. The owner is weighing diesel-punk, steam-punk, near future, forgotten past, or something like "made of carbon and powered by positrons".
- **Constraints already chosen:** solar power, a holographic HUD, faceted land, heavy-metal music.
- **(proposed) A forgotten future:** a machine built by a lost civilisation, recovered and piloted.
  - Carbon and ceramic shell, and a solar skin.
  - Instruments that feel physical but are made of cast metal and light.
  - Antimatter could be the rockets' payload rather than the power source (it would compete with solar).
- **Tone, whatever the era (owner): clunky and analogue.** Going too advanced takes away the clunky analogue feel people like about mechs.
  - Heavy switches and levers.
  - Needles that swing, overshoot and settle; lamps that warm up and cool down (slowly: never a flash).
  - A cockpit that rattles and shakes, and steps you feel.
  - The HUD is a crude projection onto the glass, like an old gunsight reflector, not a sleek hologram.
- **The setting's idea (owner): old tech attempting new-age tech.** It achieves the same effects with less advanced materials. For example:
  - the HUD is a lamp and lens projecting onto the glass;
  - solar comes from ranks of crude cells or mirrors;
  - the shield is a humming field thrown by heavy coils;
  - lock-on is a mechanical tracker that clunks onto its target;
  - sensor readouts are sweeps on a glowing tube.
  - Everything visibly works hard: it hums, ticks, warms up and strains.
- **Where the power goes (owner: not steam; plausible by design, never explained).** Still open. The owner didn't care for a flywheel; the quartz crusher (§4) is the current idea.
- Either way: original, with no existing franchise's mech or cockpit style.

## 9. First playable test
1. The look settled: facet size and textures.
2. The terrain, with digging, on a **flat test world** (owner). The world is flat for now, with **2 m hand-cut facets** (owner: the best performer).
3. The mech: movement, strafe, jump, boost; solar energy.
4. A cockpit with its dials.
5. A target that doesn't fire back:
   - The target is **a slow walking wanderer** (owner).
   - lock on and fire missiles;
   - machine-gun it;
   - blow holes through the ground around it;
   - eventually destroy it.
   - Switch weapons throughout.

No enemies that fire back yet.
