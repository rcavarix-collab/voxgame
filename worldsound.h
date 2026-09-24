// worldsound.h
//
// The glue between the game and the world sound palette (DESIGN.md Part
// X.4, docs/SOUND_PALETTE.md): the soundscape census and axes, movement
// sounds read off the player each tick (footfalls, landings, slides), The
// Line passing, discoveries, and the calls game.cpp makes when the player
// acts (place, break, hotbar, library, save).

#pragma once

#include "blocks.h"
#include "sfx_synth.h"

void WorldSoundReset();                 // a new game or a load: a fresh session
void WorldSoundTick(float dt);          // after each physics tick
void WorldSoundFrame(float dt, bool playing); // once per frame: census, axes, the palette's queue

void WorldSoundPlace(BlockID id);       // a block was placed (Set)
void WorldSoundBreak(BlockID id);       // a block was taken
void WorldSoundCue(SoundId id, int slot = 0, float strength = 1.0f);

// For the F3 overlay.
SoundAxes WorldSoundAxes();
