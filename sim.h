// =============================================================================
//  sim.h  -  Actors driven by input commands
//
//  Every body that walks the map (the local player, squad bots, and later
//  network players) is moved by the same function from the same input
//  struct. A keyboard, a bot brain or a network packet only has to produce an
//  InputCmd; the physics never needs to know which one it was.
// =============================================================================
#pragma once
#include "game.h"
#include "world.h"

// What happened during a movement step (so the caller can play sounds, shake)
struct MoveEvents {
    bool  jumped  = false;
    bool  landed  = false;
    float landVel = 0.f;     // vertical speed at touchdown (negative = falling)
};

// Shared movement: acceleration on the ground, limited air control, step-up of
// 0.32 units, gravity, jumping and sliding along walls
MoveEvents stepMovement(const World &w, Player &p, const InputCmd &cmd, float dt);

