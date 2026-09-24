# Voxistics — World Sound Palette

The short environmental and interaction sounds, synthesized by the same engine as the day-cycle music (DESIGN.md Part XIV) and written as parts of that composition — punctuation and colour inside the track, never effects laid on top of it. Implementation: `sfx_synth.h/.cpp` (the palette, pure and natively tested), `soundscape.h/.cpp` (the three axes from the world), `audio.cpp` (the second voice). DESIGN.md Part X.4 summarises; this file is the full specification.

---

## 1. The contract: why these can't sound like garbage

Every rule below is enforced in code, not left to taste.

**1.1 One clock, one harmony.** The music's chord, beat, section, shared filter cutoff and master level are all pure functions of day time (Part XIV.5), so the palette asks the score directly — `MusicHarmonyAt(t)` — for the moment the sound will be *heard* (`AudibleMusicTime()`, read from the chunk actually playing). A sound never guesses the key; it reads it.

**1.2 Pitch comes only from safe sets.** D Dorian is D E F G A B C. Against each chord of the progression the *safe set* is every scale tone that is not a semitone from a sounding chord tone:

| Chord (bass) | Chord tones | Safe set | Excluded (semitone clash) |
|---|---|---|---|
| Dm9 (D2) | D F A C E | D E F G A C | B (against C) |
| G7sus4 (G1) | G C D F A | G A C D F | E (against F), B (against C) |
| Em7 (E1) | E G B D | E G A B D | F (against E), C (against B) |
| A7sus4 (A1) | A D E G C | A C D E G | F (against E), B (against C) |
| Drone (D2) | D A | D E F G A C | — (B kept out for colour consistency) |

The **anchor set** — safe under every chord — is **D, G, A** (root, 4th, 5th: the open, suspended sound of the whole track). A sound that can't be sure of the chord (it lands inside a chord crossfade, where both chords' weights exceed 0.2) uses the anchor set. Nothing ever sounds a semitone or a tritone against the pads. There are no pitch glides that end off the set: glides only ever *arrive* on a safe tone.

**1.3 Shared timbre.** The palette uses the track's own oscillators — pure sine, the additive soft triangle (partials 1/3/5 at 1/n²), the sine+triangle blend, the PolyBLEP pulse (DC-removed, 0.55 scale) and saw (one-pole softened), hash white noise through RBJ biquads, and the track's pitch-gliding thump — plus two voices built from the same parts: **VOX** (a softened saw/triangle through two formant bandpasses) and **BELL** (a sine with harmonic partials, and — only toward Mechanical — two quiet, fast-dying bar-mode partials).

**1.4 Shared filter character.** Every sound passes a lowpass that tracks the music's own day-long cutoff arc: `fc = clamp(1.4 × musicCutoff × 2^(0.5·P) × (0.85 + 0.3·M) × 2^offset, 500, 4200 Hz)`, Q = 0.707 + 0.3·M (never above the track's 1.2 ceiling). At night, when the track closes to ~470 Hz, the palette darkens with it; at midday it opens with it. A sound's `offset` (its octave allowance above the track) is part of its spec and never exceeds +1.

**1.5 Deep-bass relationship.** Any sound with sub weight (Land, Set, Sealed, Heartbeat…) glides its thump onto the *current chord's bass note* (D2 73.4, G1 49.0, E1 41.2, A1 55.0 Hz) — the same way the track's own thump glides 104 → 52 Hz. If the pulse layer is audible and the onset falls within 60 ms of a beat, the sub is dropped (the kick already carries it) and only the upper body plays.

**1.6 No aggressive transients, no sudden brightness.** Every envelope corner is the track's raised cosine (zero slope, no clicks). Minimum attack: 6 ms tonal, 3 ms noise. Noise is never unfiltered, and the whole palette passes a two-stage 4.2 kHz lowpass on its way out. Metallic partials are at most −20 dB re the body and die within ~60 ms, so they colour an attack and never sustain as a pitch.

**1.7 Level: punctuation, not competition.** Levels are in the same dB scale as Part XIV's layer tables (the motif sits at −15 to −19, the pads −19 to −31). No palette sound exceeds **−21 dB**; interactions sit at −23 to −27, textures −28 to −36. The whole bus follows the track's master curve (`kMasterGain`), so it breathes with the day, and it has its own **World sounds** volume slider under Master.

**1.8 Randomness only inside musical ranges.** All variation is the track's own deterministic hash of the event's identity: onset jitter (bounded by the M axis, §3), a choice among safe-set pitches, ±0.5–2.5 dB level, one of three timbre variants. Nothing is free-running noise.

**1.9 Clusters form gestures.** (§4) Sounds that land close together share a gesture: a ladder of pitches, one contour, one echo, a voice ceiling and a merge rule. A burst of breaking becomes a falling arpeggio, not a pile.

**1.10 Pause is silence.** The pause menu keeps its meaning — time stopped. Opening it fades any palette tails over 0.3 s; the ambient scheduler only runs during play. The library and map (which also stop the music) allow only their own soft UI sounds.

**1.11 Nothing anyone owns.** Every sound here is synthesized from the recipes below; no samples exist. No sound is modelled on another game's signature cues (pickup chimes, level-up fanfares, UI blips) — the vocal ad-libs are abstract formant syllables, not words or anyone's voice.

---

## 2. Notation

- **Voices:** `SINE`, `STRI` (soft triangle), `BLEND` (0.65 sine + 0.45 soft triangle), `PULSE(w)`, `SAW→lp(f)` (saw through a one-pole lowpass), `NOISE→bp(f,Q)` / `NOISE→lp(f)`, `THUMP(from→to, τ)`, `VOX(vowel)`, `BELL(ratios)`.
- **Body morph:** a voice marked *morph* crossfades STRI (M = 0) → BLEND (M = 0.5) → PULSE 34 % (M = 1).
- **Envelope:** `A/H/R` in ms, raised-cosine corners; `A/exp τ` = raised-cosine attack then exponential decay with time constant τ, windowed to exact zero.
- **Pitch names:** scientific (A4 = 440 Hz). **Ladder** `L(chord)`: the chord's tones ascending from D5 — Dm9 D5 E5 F5 A5 C6 D6 E6; G7sus4 D5 F5 G5 A5 C6 D6 F6; Em7 D5 E5 G5 B5 D6 E6 G6; A7sus4 D5 E5 G5 A5 C6 D6 E6. A gesture holds a *ladder index*, not a pitch, so a streak keeps climbing smoothly through a chord change (voice leading for free).
- **Axes:** **P** ∈ [−1, +1] (negative → positive), **A** ∈ [0, 1] (calm → active), **M** ∈ [0, 1] (organic → mechanical). A sound's default position is where it sits in its natural context; the *axis response* lines add what's specific to that sound on top of the global rules in §3.
- **Tier:** 1 interaction (always plays, immediately) · 2 discovery/progress (next beat) · 3 rhythmic accent · 4 texture · 5 rare colour.

---

## 3. The three axes

### 3.1 Where they come from (soundscape.cpp)
A fixed-cost census: each frame reads one horizontal slab of a 32 × 24 × 32 box around the player (1,024 lookups, top-down so "open to the sky" costs one extra read), so the whole box refreshes every 24 frames whatever the world's size. Each block has a **sound material** (earth, stone, wood, plant, glass, crystal, metal, flesh, genesis, water, ice, sand, snow, ember) which rolls up into classes: *natural* (earth, wood, plant, moss, water…), *mechanical* (machine, tube, chest, foundation, lattice, archivist wall), *dark* (the flesh set, void static) and *genesis* (the genesis set).

Counts become saturating *presences* (1 − e^(−count/scale)), so a handful of machines registers and a hundred doesn't dominate: mechanical (scale 20), natural *exposed* surface (150), plants (20), genesis (15), dark (15).

- **M** target = 0.35 + 0.6 · mechanical − 0.3 · natural · (1 − mechanical): open land ≈ 0.05, bare rock underground 0.35, a works yard of 60 machines ≈ 0.9. Slew τ 5 s.
- **P** target = 0.1 + 0.2 · natural + 0.2 · plants + 0.5 · genesis − 1.3 · dark: bare dirt ≈ 0.3, a meadow ≈ 0.5, fifteen blocks of flesh drag it to about −0.5. Later: unmanaged byproducts and sour essence (Part XX) join the negative side. Slew τ 10 s.
- **A** target = 0.12 + 0.35 · movement (walk 0.4, sprint 0.8, slide 1.0) + 0.3 · recent interactions (decaying over ~8 s, saturating at 12) + 0.12 · the section's own energy (Midday 1, Morning/Afternoon 0.6, Dawn/Dusk 0.25, Night 0) + 0.15 · mechanical. Rises with τ 1.5 s, falls with τ 6 s — busy fast, calm slowly, like breath.

The same census fills the ambient scene (plants, water, ember, glow, machines, dark, the nearest three music blocks), a roof probe (a solid block within 12 above = enclosed; 12+ of the 40 above = deep), and notices **discoveries**, one per sweep at most: dark ground first seen (or again after 10 minutes) → Omen; ore coming into range (2-minute cooldown) → Vein; three or more block types new this session in one sweep → Horizon; the first of each glowing block type → Glint. The first 20 seconds of a session only take stock (nothing is "new" on arrival).

The F3 profiler shows the three as bars.

### 3.2 Global responses (every sound, unless its entry says otherwise)

| | Organic (M = 0) | Mechanical (M = 1) |
|---|---|---|
| Body | STRI, warm | PULSE 34 %, clean edge |
| Onset jitter | ±18 ms (interactions: 0 to +9 ms, never early) | ±1 ms |
| Detune between a sound's layers | 4 cents | 0 |
| Breath (NOISE→bp at 2f, Q 2) | −26 dB re body | off |
| Metallic partials (×2.76, ×5.40, τ 40 ms) | −34 dB | −20 dB |
| Attack / release scale | ×1.25 / ×1.15 | ×0.75 / ×0.85 |
| Filter | cutoff ×0.85, Q 0.707 | cutoff ×1.15, Q 1.0 |
| Level variation | ±2.5 dB | ±0.5 dB |

| | Negative (P = −1) | Positive (P = +1) |
|---|---|---|
| Pitch pool | root, 4th, 5th, ♭7 only (open, hollow — no 3rds, no 9ths) | full safe set; 9ths and Dorian's ♮6 (B, under Em7) allowed |
| Register | tonal bodies drop an octave below P = −0.5 | as written |
| Contour | gestures fall | gestures rise |
| Wear | slow wow, 0.55 Hz, ±6 cents × (−P) | none |
| Completion | a multi-note figure's last note is left out, hash chance up to 50 % at P = −1 | always completes, may add a grace note |
| Filter | cutoff ×0.71 (−½ octave) | ×1.41 |
| Echo loop tone | 1.3 kHz | 2.5 kHz |
| Level | −1.5 dB | 0 |

| | Calm (A = 0) | High activity (A = 1) |
|---|---|---|
| Ambient budget | 1 event per 4 bars | 6 per 4 bars |
| Grid for scheduled events | half-bar | 8th (accents 16th) |
| Release scale | ×1.25 | ×0.75 |
| Echo | send ×1.0, feedback 0.38 | send ×0.5, feedback 0.24 |
| Grace notes | none | A > 0.6: a ladder step above, a 32nd early, −9 dB |
| Accent level | −0.75 dB | +0.75 dB |

Axis values change continuously, but anything that would be a *discrete* change (pitch pool, register octave, grid) is latched at the start of each gesture or scheduler bar, so it never flips mid-phrase.

---

## 4. The gesture system — how clusters stay musical

- **The echo bus.** One tempo-synced feedback delay at a dotted 8th (0.75 beat: 369 ms at 122 BPM), loop filtered (one-pole lowpass per P, one-pole highpass 180 Hz), feedback per A. Every tonal sound sends to it. Echoes land on the grid, so any cluster reads as syncopation inside the groove — the progressive-house delay the genre already uses to glue parts.
- **Gesture window.** Tier-1 and tier-2 sounds within one bar of each other form one gesture: they share a ladder index that steps per event — **up** for making (Set, Pick), **down** for taking (Take), toward the anchor for neutral. After a bar with no events the gesture ends and the next one starts from the anchor (index 0 = D5).
- **Merge.** A second onset within 30 ms of the first doesn't start a new note: it adds up to +1.5 dB to the first. No flams, no doubled attacks.
- **Repeat softening.** The same sound retriggered within 150 ms plays −3 dB (down to −9) and rotates its timbre variant, so rapid clicking doesn't machine-gun.
- **Interval rule.** A new tonal onset may not sit a 2nd from any palette pitch still sounding above −20 dB; it moves to the next ladder tone.
- **Voice ceiling.** 48 partial-voices. When full, ambience (tiers 3–5) simply yields; an interaction or event takes the slot of the quietest, lowest-priority voice (almost always a tail near silence).
- **Level ceiling, per sound.** After a sound is built, the voices sounding at each of its peaks are summed at their envelope level there; if the sum could pass −22.5 dB (the −21 ceiling less a margin for the echo's return) the whole sound comes down together. Bells are level-normalised by their partials.
- **Hierarchy.** Tier 2 suppresses tiers 3–5 for its duration (footfalls excepted: they're physical, and exempt from the budget too). Tiers 3–5 are also scaled by the Music Intensity setting (14.4): at 0 only interactions and events remain. Tier 5 plays only when A < 0.4 and no tier 2 has played in the last 8 bars, each rare sound with its own cooldown. Tier 3–4 events are spent from the ambient budget (§3.2).
- **Motif respect.** Tonal palette bodies live mostly in D5–A6, above the motif (D4–C5) and the countermelody (A4–G5), alongside the air layer's own A5/E6 — a register the track already treats as "sparkle". Vocal pads and thumps sit below. Ambient events avoid onsets that collide with a motif note in the same octave.
- **Breathing with the track.** Tonal tails get the bass's sidechain dip at half depth (the duck curve, 14.3), so the palette pumps with the groove instead of against it.

---

## 5. The palette

### 5.1 Vocal-style ad-libs and hype punctuation
A soft, wordless "choir of one": formant syllables pitched from the chord, used sparingly — the human lift progressive house uses between phrases.

**VOX voice:** source = STRI (organic) → PULSE 40 % (mechanical) at f0, plus SAW→lp(3·f0) at −6 dB; through two parallel constant-0 dB-peak bandpasses at the vowel's formants (F1 Q 5, F2 Q 4, F2 at −4 dB); breath NOISE→bp(F2, 2) at −20 dB re body × (1 − 0.7M). Vibrato 5.2 Hz, ±10 cents organic → ±2 cents mechanical, faded in after 150 ms over 200 ms. Vowels: **oo** 300/870, **oh** 450/800, **ah** 700/1150, **eh** 530/1850 (only when P > 0.3), **mm** 250 (F1 only, output lowpassed at 450 Hz). Vowel changes glide over 60 ms (organic) or step on a 16th with a 20 ms glide (mechanical, the gentle "vocoder" end). Formant bandpasses have 0 dB peak gain: they carve, they never boost.

**V1 · Rise ("ooh–ah")** — *tier 2*
- When: a milestone — a streak completes an octave (P2), a machine comes online the first time (P4), a mending run finishes. At most once per 16 bars.
- Synthesis: two VOX notes, **oo→ah** over the first; pitches ladder index 4 → 5 (A5 → C6 over Dm9), sung an octave down (A4 → C5) in the space the motif leaves between phrases. Note 1: 60/180/220 ms; note 2 starts an 8th later: 80/300/500 ms. Echo send 0.5. Level −24.
- Axes default: P +0.6 · A 0.6 · M 0.3.
- Axis response: P > 0.5 adds a third note (the octave above note 1, a 16th later, −6 dB); P < 0 turns it into **oh→oo**, falling ladder 4 → 2, and it can lose its second note (1.2 completion rule). A shifts the entry from the 8th before the downbeat (calm) to a 16th pickup (active). M steps the vowel change on the 16th and trims vibrato — a cleaner, "processed" lift near the works.
- Why it sits: it's the classic house vocal lift, landing on the downbeat in the space between motif phrases, sung on chord tones through a filter that tracks the track's own.

**V2 · Breath hey** — *tier 3*
- When: activity peaks — entering a sprint or slide while A > 0.6; at most once per 2 bars.
- Synthesis: one short VOX **eh** (P ≥ 0) or **oh** (P < 0), mostly breath: body −10 dB under its breath noise; pitch = ladder index 2 an octave down; 6/40/120 ms; placed on the *and* of the next beat (off-beat, never on the kick). Echo send 0.35. Level −28.
- Axes default: P +0.3 · A 0.8 · M 0.3.
- Axis response: A below 0.6 → it doesn't fire at all (hype belongs to activity). P shifts the vowel and pitch (ladder 2 → 0 as P falls). M raises the body over the breath (−4 dB instead of −10), so it reads as a clipped, precise syllable.
- Why it sits: off-beat, breathy and short, it fills the gap between kicks the way a shaken "hey" does in house, without a word or a recognisable voice.

**V3 · Hum ("mm")** — *tier 4*
- When: calm contentment — standing still for 8 s or more in healthy land (P > 0.3, A < 0.3).
- Synthesis: VOX **mm**, three notes quoting the Dusk cells of the Afternoon motif down an octave (A4 → G4 → E4 under Em7/A7sus4; A4 → F4 → D4 under Dm9/G7sus4), each 250/400/900 ms, 0.55 s apart (Dusk's own cell spacing). Level −31.
- Axes default: P +0.5 · A 0.1 · M 0.1.
- Axis response: P under 0.3 → silent (nobody hums in neglect). M above 0.6 → the hum becomes a closed **oo** with stepped pitch, a machine "idling in tune". A only moves where it lands (on the next half-bar at A 0; never above A 0.3).
- Why it sits: it literally hums the track's own motif fragment, in the pads' register, under the shared lowpass — it sounds like the world humming along.

**V4 · Choir bloom ("aah")** — *tier 2*
- When: arrival and wonder — the Horizon discovery (D2), dawn's first light (A2), a rift sealed (P6).
- Synthesis: three VOX **ah** voices on the chord's root, 5th and 9th in the mid-pad register (D4 A4 E5 over Dm9; G3 D4 A4 over G7sus4; E4 B4 D5 over Em7 (root, 5th, 7th); A3 E4 D5 over A7sus4 (root, 5th, 11th), detuned ±4 cents organic; 900 ms attack, 600 ms hold, 1.8 s release. Starts on the next downbeat. Level −25 (sum).
- Axes default: P +0.5 · A 0.3 · M 0.2.
- Axis response: P < 0 drops the 9th and uses **oo** (open 5th, sombre); P > 0.6 adds the octave above the root at −8 dB. A shortens the swell (attack 900 → 450 ms) so it fits a busier bar. M removes the detune and steps the vowel onset (**oo→ah** on a 16th) — a cleaner, "synth choir".
- Why it sits: it's the pad chord itself, sung — the same voicing logic as the pad registers, entering on the downbeat and leaving before the next chord.

**V5 · Answer ("oh–yeah")** — *tier 3*
- When: in the Afternoon section, in the rests of the motif's 3-bar phrase, while A > 0.5 and P > 0 (a call-and-response with the motif); at most once per phrase.
- Synthesis: two VOX notes quoting the phrase's last two pitches (A4 → F4) an octave up (A5 → F5 under Dm9/G7sus4; the anchor fallback A5 → G5 elsewhere), **oh→eh** then **ah**, 8th-note spacing, 40/120/300 ms each, placed on beat 7 of the 12-beat phrase. Echo send 0.6. Level −27.
- Axes default: P +0.4 · A 0.6 · M 0.3.
- Axis response: P decides whether it answers at all and whether it ends up (eh) or settles (ah). A > 0.8 adds a third, grace-note syllable; A < 0.5 silences it. M steps the vowels, trims vibrato.
- Why it sits: it's an echo of the motif in the motif's own gaps — the track's melody answered, not overdubbed.

**V6 · Sigh ("oh", falling)** — *tier 4*
- When: neglect noticed — P drops below −0.3 and stays there for 10 s (once per 2 minutes).
- Synthesis: one VOX **oh→oo**, pitch ladder index 3 an octave down, glide *down* a whole step onto a safe tone (e.g. A4 → G4) over its release; 300/200/1200 ms. Level −30.
- Axes default: P −0.5 · A 0.2 · M 0.1.
- Axis response: deeper P → longer release (1.2 → 1.8 s) and more wow. A only moves it off the downbeat to the next half-bar. M above 0.6 → the glide becomes two stepped notes (a mechanical "wind-down").
- Why it sits: a falling whole step on scale tones is Dorian's own melancholy — sadness without dissonance.

### 5.2 Interaction and confirmation
Immediate (≤ ~25 ms), tier 1, and always harmonically current. **Material tint** (from the block's sound material) sets a small timbre shift on Set/Take/Footfall/Land:

| Material | Tint |
|---|---|
| earth, sand, snow, moss | body filter ×0.7, noise grain lp 900 Hz, no metallic |
| stone, basalt, ice | body τ ×0.7 (tighter), noise lp 1.6 kHz |
| wood, logs | body through bp 900 Hz Q 1.2 (a hollow knock), τ ×1.2 |
| plant, card | no thump, body → SINE, half level, +1 octave |
| glass, crystal | body → BELL(1, 2, 3), τ ×1.6, offset +0.5 octave |
| metal (machine, tube, lattice) | +10 dB metallic partials, PULSE body |
| flesh | body filter ×0.5, level −3 dB, slow 40 ms attack (soft, wet) |
| genesis | body → BLEND, + a ladder-octave SINE shimmer at −12 dB |

**I1 · Set (place a block)** — *tier 1*
- Synthesis: *body* morph voice at the gesture's current ladder pitch (step **up** each Set in the gesture), 6/exp τ 120 ms, through a pluck filter — lowpass starting at 6·f0 and falling to 1.5·f0 with τ 60 ms (the track's subtractive family, played as a pluck); *sub* THUMP(2·bass → bass, τ 70 ms) at −6 dB re body, 3/exp 90 ms; *grain* NOISE→lp(material) 3/exp 15 ms at −14 dB. Echo send 0.25. Level −24.
- Axes default: P +0.2 · A 0.4 · M 0.3.
- Axis response: M morphs the body (warm pluck → clean pulse blip) and tightens jitter to zero. P picks the pool (P < −0.25: ladder restricted to root/4th/5th/♭7; below −0.5 an octave down) and brightens the pluck's start (6·f0 → 8·f0 at P = 1). A above 0.6 shortens τ to 80 ms — a quick build becomes a crisp, rising run. (No grace note here: an interaction never waits.)
- Why it sits: every placement is a chord tone, and a building session plays an arpeggio up the current chord; the sub lands on the bass note, so it never fights the low end.

**I2 · Take (break a block)** — *tier 1*
- Synthesis: two voices a 32nd apart: ladder pitch then the next ladder tone *down* (the gesture steps down each Take), body morph 6/exp 90 ms with pluck filter 4·f0 → 1.2·f0; *crumble* 3 noise grains (material filter, 3/exp 12 ms, 25 ms apart ± jitter, −12 dB, falling level); sub THUMP at −10 dB, only for stone/earth/wood. Echo send 0.2. Level −25.
- Axes default: P 0 · A 0.4 · M 0.3.
- Axis response: M turns the crumble into two even ticks (±1 ms) with metallic edges — dismantling, not digging. P < 0 → the second note falls a 4th instead of a step and the grains darken; P > 0.4 → the second note is the octave-down (a satisfying "done"). A shortens everything by ×0.8.
- Why it sits: a falling two-note figure is the inverse of Set, so mining plays descending arpeggios that resolve toward the root.

**I3 · Slot (hotbar select)** — *tier 1*
- Synthesis: one BLEND tone, 6/20/60 ms, pitch fixed per slot on the anchor set — slots 1–10 = A3 D4 G4 A4 D5 G5 A5 D6 G6 A6 — so each slot has its own note and scrolling plays a rising or falling D–G–A figure that fits every chord. Lowpass offset +0.5. No echo. Level −30, −1.5 dB per slot above 5 (equal loudness in the brighter end).
- Axes default: P 0 · A 0.3 · M 0.5.
- Axis response: M morphs body and tightens attack to 4 ms. P < 0 → releases shorten and the level drops 1.5 dB. A: repeat softening applies (scroll bursts get quieter, not busier).
- Why it sits: the anchor set is consonant with all five chords, so no scroll position ever clashes.

**I4 · Library open / close** — *tier 1*
- Open: two STRI notes, anchor D5 + A5 together, attack 30 ms, lowpass sweeping 500 → 1400 Hz over 180 ms (a filter-opening "breath"), release 350 ms. Level −27.
- Close: the mirror — A5 + D5 with the filter closing 1400 → 500 Hz, 20/60/250 ms. Level −28.
- Axes default: P 0 · A 0.3 · M 0.4. Axis response: M → PULSE body, sharper sweep (120 ms). P < 0 → open 5th only an octave lower. A has no effect (UI stays constant).
- Why it sits: it's a filter sweep on an open 5th — the track's own gesture (its day-long cutoff arc) in miniature, so opening a menu sounds like the music drawing a breath as it stops.

**I5 · Pick and drop (library drag to hotbar)** — *tier 1*
- Pick: SINE gliding up a 5th from D5 and *arriving* on A5 over 40 ms (anchor tones at both ends), 6/30/120 ms. Level −30.
- Drop: the target slot's note (I3 table) as a BLEND pluck 6/exp 150 ms plus a soft THUMP on D2 at −8 dB; if the slot changed, a second grace note a 16th later on the slot's octave. Level −26.
- Axes default: P 0.2 · A 0.3 · M 0.4. Axis response: as I3; P > 0.4 adds the octave grace.
- Why it sits: the slot keeps the note it plays when selected, so the drop "names" the slot musically — the hotbar is a little scale.

**I6 · Can't (placement refused)** — *tier 1*
- Synthesis: *the same pitch twice*, not a clash — ladder index 0 an octave down (D4), STRI, 6/30/80 ms then again a 16th later at −6 dB; lowpass fixed at 500 Hz. No echo. Level −28.
- Axes default: P −0.1 · A 0.4 · M 0.3. Axis response: M → PULSE, shorter (60 ms). P barely matters (it's already neutral). A: repeat softening only.
- Why it sits: "no" is said in rhythm (uh-uh), not with a sour interval; on the root under a closed filter it's almost a bass-drum ghost note.

**I7 · Land (touch down after a fall)** — *tier 1*
- Synthesis: THUMP(2·bass → bass, τ 90 ms), level scaling with fall speed (−30 at a hop → −23 at a big drop); *body* NOISE→lp(material) 3/exp 30 ms at −12 dB. Big drops (> 6 blocks) add a BLEND root an octave above the bass, 10/exp 250 ms. Level −23 max.
- Axes default: P 0 · A 0.5 · M 0.3. Axis response: M sharpens the noise (lp ×1.5, 2 ms metallic tick). P < 0 → the root is left out. A → nothing (it's physical).
- Why it sits: the thump is pitched to the chord's bass note, and when it falls near a beat the kick swallows its sub (1.5) — a landing on the beat simply *is* the beat.

**I8 · Slide (power slide)** — *tier 1*
- Synthesis: NOISE→bp, centre gliding from the chord's 5th ×4 down to its root ×4 over the slide's length (e.g. 1760 → 1174 Hz under Dm9), Q 1.6 (the air layer's own filter); 60 ms attack, holds while sliding, 250 ms release; plus a BLEND 5th an octave above the bass, 80 ms attack, −14 dB (the "tonal" floor under the rush). Level −28.
- Axes default: P 0 · A 0.9 · M 0.3. Axis response: M narrows Q to 2.4 and adds a faint PULSE edge. P > 0.4 → the glide lands on the 9th instead of the root (brighter finish). A: none extra.
- Why it sits: it's the air layer, swept — a textured whoosh already in the track's vocabulary, with its centre arriving on a chord tone.

### 5.3 Discovery — "something new"
Tier 2: quantised to the next beat (next half-bar when calm), never more than one per 2 bars.

**D1 · Unveil** — *not triggered: the owner dropped the first-placement cue. The recipe stays in the palette for a future discovery that earns it.*
- Synthesis: three BELL(1, 2, 3; partial gains 1, 0.3, 0.12; partial τ ×0.5) notes, ladder root → 5th → 9th (e.g. D5 A5 E6 under Dm9), 8th notes, each 6/exp τ 600 ms; echo send 0.5. Level −25.
- Axes default: P +0.4 · A 0.4 · M 0.3.
- Axis response: P > 0.5 adds the octave on top as a 4th note; P < 0 plays root → 4th → root (a question, not an answer). A → 16ths instead of 8ths at A > 0.6. M adds the bar-mode partials (a clean glass/metal chime) and drops jitter.
- Why it sits: it's an arpeggio of the chord in the air layer's register, ringing into the delay — a gentle "ooh" rather than a fanfare.

**D2 · Horizon (entering ground made of materials not yet met this session)**
- Synthesis: V4's choir bloom (root/5th/9th) + a two-note BELL quote of the main motif's opening (D5 → F5 under Dm9/G7sus4; D5 → E5 under Em7/A7sus4) on beats 3 and 4. Level −24 (sum).
- Axes default: P +0.5 · A 0.3 · M 0.2. Axis response: as V4 and D1 combined; M → the quote on PULSE instead of BELL.
- Why it sits: it opens with the motif's own first interval.

**D3 · Glint (first emissive block seen nearby: glow mushrooms, magma, star forge, dawn light)**
- Synthesis: two SINEs a 5th apart in the air-tone register (A5 + E6 under Dm9/A7sus4, D6 + A6 elsewhere), 40/200/1400 ms, with 16th-note amplitude shimmer (depth 0.3, raised-cosine), offset +1 octave. Level −31.
- Axes default: P +0.3 · A 0.2 · M 0.1.
- Axis response: M → the shimmer becomes a square 16th gate with 5 ms ramps (a clean pulse), metallic partials on. P < 0 → a 4th instead of a 5th (D6 + G6) and half-speed shimmer. A → shimmer rate from 8ths (calm) to 16ths (active).
- Why it sits: it doubles the air layer's own A5/E6 tones — the same sparkle, momentarily in focus.

**D4 · Vein (rare ore found: raw fragment ore)**
- Synthesis: four BELL notes on the main motif's first four pitches up an octave (D5 F5 A5 C6 — the Dm9 arpeggio the track is built on), 16th spacing, 6/exp 500 ms; under chords where F or C are unsafe the anchor substitutes (D5 G5 A5 D6). Echo send 0.6. Level −24.
- Axes default: P +0.5 · A 0.5 · M 0.4. Axis response: P < 0 → three notes, descending. A < 0.3 → 8ths. M → metallic partials up, (a struck bar).
- Why it sits: it *is* the motif's opening, quoted — finding treasure plays the theme.

**D5 · Timeslip (The Line passes through the player's cell)**
- Synthesis: a *reverse-envelope* chord tone — BLEND at ladder 3, 900 ms raised-cosine swell ending in a 60 ms release (a swell that pulls away, like tape played backwards, but with no hard edge); plus its echo reversed in order (taps at −1.5, −0.75 beat *before* it, arriving ghosts at −18 and −12 dB). Glides *into* pitch from +50 cents over the swell (arrival on a safe tone). Level −27.
- Axes default: P 0 · A 0.3 · M 0.5.
- Axis response: M stepped arrival (pitch quantised in 25-cent steps), cleaner; P picks 5th (P ≥ 0) or 4th (P < 0) for the pitch; A shortens the swell to 450 ms.
- Why it sits: it's the one effect the palette allows to feel "time-bent" — still a chord tone, arriving exactly on the beat.

**D6 · Omen (first sight of dark ground / a rift)**
- Synthesis: VOX **oo** on the open 5th, two octaves down from the ladder (D3 + A3 / G2 + D3…), 1.2 s attack, 3 s release, slow 0.5 Hz tremolo (depth 0.25); then one falling 4th in STRI, D4 → A3 (both anchor tones). Level −27.
- Axes default: P −0.7 · A 0.2 · M 0.2. Axis response: M → the drone becomes SAW→lp(400) at the bass's octave (a transformer hum in key); A → attack 1.2 → 0.6 s. P more negative → longer, lower, more wow.
- Why it sits: hollow 5ths and the drone chord (Part XIV's Night) carry the unease without a single dissonant interval.

### 5.4 Subtle rhythmic world accents
Tier 3, spent from the ambient budget, always on the grid. These make the world *play along*.

**R1 · Footfall**
- Synthesis: NOISE→lp(material) 3/exp 10 ms, and a sub SINE at the bass note, 8/exp 40 ms at −16 dB. Level −36 (organic ground), −33 (hard ground). Walking cadence (~2 steps/s) is close to the beat (2.03/s at 122 BPM), so steps phase-lock: a footfall sound is delayed up to 40 ms to land on the nearest 8th, never advanced; outside the window it plays as is, just softer.
- Axes default: P 0 · A 0.4 · M 0.3. Axis response: M → cleaner tick with a metallic edge (walking on foundation/lattice). A → sprint steps hit 8ths with every other step −3 dB (a shuffle). P < 0 → darker (lp ×0.7).
- Why it sits: steps land on the grid as the quietest hi-hat in the mix.

**R2 · The works (machine ticks)**
- Synthesis: each running machine type contributes one Euclidean pattern slot — E(3, 8), E(5, 16), E(2, 5) (on 16ths) — and nearby machines of a type share one pattern (more machines = +1 dB, not more hits). Tick: NOISE→bp(2.8 kHz × 2^(0.5P), Q 1.2) 3/exp 8 ms + metallic partial pair at the chord's root ×4, −18 dB. Level −34.
- Axes default: P 0 · A 0.6 · M 0.9. Axis response: M is its home; toward organic it softens into wood-block STRI clicks. P < 0 → a missing hit in each pattern (the ♭ of wear: an irregular machine), darker bp. A → pattern density: E(2, 8) at calm to E(5, 16) at high activity.
- Why it sits: factories literally become the hi-hat section, locked to the tempo.
- Note: triggers on running machines (Part VI, future); placeholder machines count while the census sees them.

**R3 · Drip**
- Synthesis: SINE, *arrives* on a safe tone from a 5th below over 25 ms (the "bloop" of water is an upward glide), 3/exp 70 ms; pitch from the ladder top octave (D6–A6). 8th grid, one per 2 bars at calm. Level −33.
- Where: enclosed spaces with water/ice/moss nearby (census), and caves.
- Axes default: P +0.1 · A 0.2 · M 0.1. Axis response: M → no glide, BELL body (a metronomic drip in a pipe). P < 0 → lower octave, slower. A → rate.
- Why it sits: a pitched drop on a chord tone is a delay-soaked pluck; three of them make a melody.

**R4 · Ember crackle**
- Synthesis: NOISE→bp(1.2 kHz, Q 1) grains 2/exp 6 ms, placed on a 16th grid with hash probability (0.35 at calm, 0.6 active), velocities ±3 dB; one grain per bar lands on the *and* of 4 for lift. Level −35.
- Where: near magma rock, star forge (ember glow).
- Axes default: P 0 · A 0.5 · M 0.2. Axis response: M → the grid tightens (no jitter), grains shorten to 4 ms. P < 0 → lower bp (800 Hz) and sparser. A → probability.
- Why it sits: a fire's crackle, quantised, becomes a shaker part.

**R5 · Clave (the music block as an instrument)**
- Synthesis: BELL(1, 2.0) through bp 1.2 kHz, 2/exp 45 ms, on a fixed rhythmic slot per music block (its position hashes to one of E(3, 8) rotations), pitch = anchor set by height (y mod 3 → D, G, A in octave 5). Only while the pulse layer is audible. Nearest 3 blocks play; more blocks raise level, not hits. Level −32.
- Axes default: P +0.2 · A 0.5 · M 0.5. Axis response: M organic → wood-block (STRI, bp 900 Hz); mechanical → cleaner clave with metallic partials. A → how many of the 3 play. P < 0 → the last hit of each pattern drops.
- Why it sits: a placed music block becomes a percussion part the player composes — on the grid, in the anchor set.

**R6 · Heartbeat (near a rift / dark ground)**
- Synthesis: THUMP on the bass note ×2 → bass (lub) then ×1.5 → bass a 16th later at −4 dB (dub), on beats 1 and 3 at half-time. Level −31 (grows to −27 as P → −1).
- Axes default: P −0.6 · A 0.3 · M 0.1. Axis response: P is its trigger (only below −0.3) and level. M → the second hit becomes a clean pulse click (a machine's heartbeat). A → quarter-time → half-time.
- Why it sits: it's the track's own kick, doubled and slowed — unease made of the groove itself.

### 5.5 Living-world texture moments
Tier 4: long, soft, spent from the ambient budget; chord-aware for their whole length (they re-read the harmony every control block and crossfade tones through chord changes exactly as the pads do).

**L1 · Singing wind (grass, leaves, plants)**
- Synthesis: NOISE through two bandpasses at chord tones (the chord's 5th and 9th in octave 5, Q 6), their centres crossfading with the chord weights; swell over 2 bars (raised-cosine up 1 bar, down 1 bar). Level −33.
- Axes default: P +0.3 · A 0.2 · M 0.0. Axis response: M suppresses it (wind in a factory yard is just air: Q 1.6, −6 dB). P < 0 → root and 4th, Q 4 (a thinner, colder whistle). A → swells every 4 bars (calm) to every bar (active), shorter.
- Why it sits: the noise is tuned to the pad chord — wind that sings the harmony.

**L2 · Chirps (bird-like figures, healthy nature, Dawn/Morning)**
- Synthesis: 2–4 SINE chirps, each arriving on a ladder tone in octave 6 from +3 semitones above over 40 ms (a downward sweep that *lands*), 3/20/40 ms, 16th-note spaced, figure contour from P. Level −33.
- Axes default: P +0.6 · A 0.3 · M 0.0. Axis response: only when P > 0.2 and M < 0.5. P higher → 4 notes, rising; lower → 2 notes. A → 16ths vs 8ths. Not modelled on any real species' song — the figures are ladder arpeggios.
- Why it sits: a morning motif doubled high and fast; every chirp ends on a chord tone.

**L3 · Night shimmer (insect-like, Night and late Dusk)**
- Synthesis: SINE pair at E6 + A6 (or D6 + G6 when E/A are unsafe), amplitude gated in 16th-note triplets with 5 ms ramps (a trill rhythm), 4–8 s swells. Level −36.
- Axes default: P +0.3 · A 0.1 · M 0.0. Axis response: P < 0 → silent (a neglected night is quiet). M → steady tone instead of trill. A → gate rate 8ths → 16th triplets.
- Why it sits: it's the air layer's pair of tones, re-rhythmed — the night's own sparkle.

**L4 · Mushroom breath (near glow-mushroom clusters, pulsing membrane)**
- Synthesis: a SINE 5th (root + 5th in octave 4) swelling in time with the block's visible 0.4 Hz glow pulse (DESIGN 4.12 PULSE), 1 s up, 1.5 s down; plus a breath NOISE→bp(600 Hz, Q 2) at −12 dB re the tones. Level −34.
- Axes default: P +0.2 · A 0.1 · M 0.0. Axis response: P < 0 (pulsing membrane, flesh) → 4th instead of 5th, an octave down, more breath (−6 dB). M → clean sine only. A → none (it follows the light).
- Why it sits: sound and light share one pulse; the tones are the pad's root and 5th.

**L5 · Hum of the works (machine drone in key)**
- Synthesis: PULSE 45 % at the bass note ×2 through SAW-style one-pole lp 400 Hz (the track's "soft saw" filter), plus a SINE at bass ×4 at −10 dB; follows chord changes with a 2-beat glide (the new bass, in tune). Level scales with mechanical presence, −36 → −30.
- Axes default: P 0 · A 0.5 · M 0.9. Axis response: M is its gain. P < 0 → a slow 0.55 Hz wow (±6 cents) and −2 dB (a tired machine); P > 0.4 → a clean 5th added above (a machine running well sings). A → a 16th-note amplitude pump at depth 0.15·A (sidechain feel).
- Why it sits: the factory hums the bass line — industry in key.

**L6 · Worn ground (dark land drone)**
- Synthesis: SAW→lp(300 Hz) detuned pair at the bass ×2 (±7 cents), wow 0.55 Hz ±6 cents, swell over 4 bars, 8 bars long. Level −33.
- Axes default: P −0.7 · A 0.2 · M 0.2. Axis response: plays only for P < −0.3; deeper P → longer and louder (to −29). M → less detune. A → shorter swell.
- Why it sits: unease through instability (slow detune and wow on a consonant bass note), never a dissonant pitch.

**L7 · Enclosure bloom (stepping into a cave or roofed space)**
- Synthesis: SINE root + 5th an octave above the bass, 1.5 s attack / 2.5 s release, while the palette's lowpass drops ×0.7 for as long as the player stays enclosed. Level −33.
- Axes default: P 0 · A 0.2 · M 0.3. Axis response: P picks 5th (≥ 0) or 4th (< 0). M → PULSE body at −6 dB. A → attack 1.5 → 0.8 s.
- Why it sits: it is the pad's low register, briefly swelling — the room's resonance tuned to the chord.

### 5.6 Progress and positive feedback

**P1 · Streak (successive Sets in one gesture)**
- Not a separate sound: I1 walking up the ladder. When a streak reaches the octave above where it started, it also fires P2 and the next Set starts a new gesture.
- Axes: P decides the direction (P < 0: the streak never climbs above index 4 — building in neglected land feels capped).

**P2 · Cadence (streak octave / a task completed)** — *tier 2*
- Synthesis: two notes resolving to the root: the 9th (E6) → the octave root (D6) under Dm9, the 5th → root elsewhere; BELL + VOX **ah** doubled an octave down at −8 dB; on beats 3 and 1 (lands on the downbeat). Level −24.
- Axes default: P +0.7 · A 0.5 · M 0.3. Axis response: P < 0 → no cadence (it doesn't fire); A → beat 4 → 1 instead of 3 → 1 when busy. M → BELL gets bar-mode partials; VOX off above M 0.7.
- Why it sits: a stepwise resolution to D on the downbeat — the strongest consonance in the key.

**P3 · Mending (land restoring, genesis spreading)** — *tier 3*
- Synthesis: each restored block plays the *next note of the main motif* (D F A C A F E G, looping), up an octave, as a BLEND pluck 6/exp 300 ms, on the next 8th; restoration events closer than an 8th queue. Level −29.
- Axes default: P +0.7 · A 0.3 · M 0.2. Axis response: M → PULSE pluck (the motif's own voice). P raises level (the healthier it gets, the clearer the melody). A → 16th grid when busy.
- Why it sits: healing land plays the theme, one note at a time — progress audible as melody.

**P4 · Online (a machine starts running)** — *tier 2*
- Synthesis: PULSE 30 % (the motif voice) root → 5th → octave in 16ths, 6/60/120 ms each; the last note sustains 600 ms into L5's hum. Level −25.
- Axes default: P +0.3 · A 0.6 · M 0.9. Axis response: P < 0 → root → 4th → root (it starts, but grudgingly). M toward organic → STRI. A → 32nd spacing when very busy.
- Why it sits: the motif voice, playing the chord's frame; it hands off into a drone that's in key.

**P5 · Sealed (manual save)** — *tier 1*
- Synthesis: SINE root + octave (D5 + D6 via the anchor), 20/100/700 ms, and THUMP on D2 at −8 dB. Level −27.
- Axes: fixed timbre (a system sound): only the global lowpass applies.
- Why it sits: the plainest consonance on the tonic. (Autosave stays silent.)

**P6 · Rift closed** — *tier 2, the biggest moment the palette has*
- Synthesis: V4 choir bloom + D4-style four-note BELL motif quote rising + THUMP on the next downbeat; the palette lowpass opens ×1.4 for 2 bars and relaxes. Level −21 (the ceiling).
- Axes default: P +0.5 (it's a turn toward health) · A 0.5 · M 0.3. Axis response: as its parts.
- Why it sits: chord, motif and bass together on the downbeat — a cadence, not an explosion.

### 5.7 Ambient punctuation and rare colour
Tier 5: only when calm (A < 0.4), each with a cooldown, never within 8 bars of a tier-2 sound.

**A1 · Far bell** (cooldown 6 min)
- Synthesis: BELL(1, 2, 3, 4.2 at −24 dB) on the chord root in octave 4, 20/exp τ 2.5 s, lowpass fixed ×0.6 (far away), echo send 0.9. Level −32.
- Axes: P < 0 → struck an octave lower, partial 4.2 off. M → bar-mode partials (a far-off factory siren would be wrong; this is a clean struck plate). A → only below 0.4.
- Why it sits: the tonic, rung in the distance, its echo on the grid.

**A2 · Horizon glimmer** (sunrise and sunset, once each)
- Synthesis: four SINEs on the chord's 5th, 9th, octave and 12th in octaves 5–6, entering one per beat, 1.5 s attack, 4 s release; with V4's bloom at −6 dB at sunrise only. Level −30.
- Axes: P picks full chord (P ≥ 0) or the open 5ths (P < 0). M → PULSE edge on the top voice. A → faster entries.
- Why it sits: the air layer opening into a chord as the sky does.

**A3 · Falling stars** (night, looking up at the sky ≥ 4 s; cooldown 90 s)
- Synthesis: four SINE notes descending the ladder from its top (E6 D6 C6 A5 under Dm9), 8th spacing, each 10/exp 400 ms, falling level (0, −3, −6, −9 dB), echo send 0.7. Level −32.
- Axes: P < 0 → three notes, anchor set. M → BELL. A → none (calm only).
- Why it sits: a descending arpeggio of the chord in the register of the night tones.

**A4 · Deep murmur** (well underground, below y 16; cooldown 3 min)
- Synthesis: THUMP-like slow glide on the bass note, 1.5 s attack, 3 s release, plus NOISE→lp(180 Hz) at −10 dB. Level −30.
- Axes: P < 0 → wow. M → steadier (no noise). A → none.
- Why it sits: the bass line, felt from below.

**A5 · Far call** (Night, healthy land; cooldown 5 min)
- Synthesis: VOX **oo**, gliding up from the root to the 5th (D4 → A4) over 2 s — both ends anchor tones — then held 1 s, 3 s release; echo send 0.9. Level −33.
- Axes: P < 0 → none. M → stepped (two notes). A → none.
- Why it sits: the Night section's single sine tones, sung — a distant voice in the drone's 5th.

**A6 · Seam (the day turns: 3600 → 0)**
- Synthesis: BELL on D5 + A5 at the exact loop point, 30/exp τ 4 s, echo send 0.9. Level −29.
- Axes: fixed (it marks the clock, not the land); the lowpass applies.
- Why it sits: the moment Night blooms back into Dawn's Dm9 — the tonic 5th under a tonic chord.

---

## 6. How the main track answers the axes
Small, bounded, slewed (≥ 8 s), so the composition keeps its identity. Applied inside `GenerateMusicChunk` from the axis values carried in `MusicState` (interpolated within each chunk, so a change never steps). With the axes at their neutral point (P 0.2, A 0.3, M 0.35) the track renders exactly as Part XIV specifies.

| Axis | Track response |
|---|---|
| M (from 0.35) | shared cutoff ×(1 + 0.15·ΔM); low-pad saw and air tones +3 dB·ΔM (machinery: more edge, glassier); noise bed −3 dB·ΔM (nature: breathier) |
| P (from 0.2) | high pad +2 dB·ΔP; shared cutoff ×2^(0.15·ΔP) |
| P below 0 | mid pad wow ±3 cents × (−P) at 0.4 Hz; master −1 dB × (−P) |
| A (from 0.3) | duck depth ×(1 + 0.3·ΔA); pulse thump +1.5 dB·ΔA |

Verified: at the neutral point the rendered track is bit-identical to the composition without the palette.

---

## 7. Cost
- Palette voice: rendered in 512-sample buffers (11.6 ms) with 3 queued (~35 ms latency) on its own XAudio2 source voice. A played sound renders immediately (not next frame). Outside play (title screen, pause) nothing renders once the palette is silent.
- 48 partial-voices maximum; typical play is 2–6. An idle palette costs ~0.1 % of a core (the bus and scheduler only). Per-sample work per voice is an oscillator (polynomial sine / PolyBLEP), an envelope and one filter — the same primitives as the track.
- Harmony queries: one `MusicHarmonyAt` per trigger and per control block while voices are active.
- Census: 768 block reads per frame, constant.

## 8. Checks
- **tests/tests.cpp** (every build): every sound × every chord × the axis corners — every pitch in the chord's safe set, never above the −21 dB ceiling; Set climbs, Take falls; merge within 30 ms; determinism; the ambient budget (calm ≤ ~1 per 4 bars, busy several times more); pausing fades to exact silence; the neutral colour leaves the track bit-identical; the census reads a meadow organic and positive, a works yard mechanical, flesh negative with an Omen.
- **tools/sound_demo.sh analyze**: the same matrix at all 8 axis corners, reporting each sound's mean and worst level, its worst-frame spectral share above 4.2 kHz and above 10 kHz (clicks are broadband), and pitch safety.
- **tools/sound_demo.sh demo DIR**: WAV renders of each category over the day's music, at contrasting axes, with solo versions.
