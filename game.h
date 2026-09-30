// =============================================================================
//  game.h  —  FPS Raycaster  |  Week 1
//  All game data: map, sprite art, structs.  No Qt dependency.
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// ─── Framebuffer ─────────────────────────────────────────────────────────────
static const int W     = 800;
static const int H     = 600;

// ─── World map ───────────────────────────────────────────────────────────────
static const int MAP_W = 24;
static const int MAP_H = 24;
//  0 = empty   1 = stone   2 = brick   3 = concrete pillar
static const int MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

// ─── Enemy sprite: 16×16 pixel-art demon ─────────────────────────────────────
//  Format: 0xAARRGGBB  (A=0 → transparent)
#define _X 0x00000000
#define SK 0xFF9B6B3D  // skin
#define RE 0xFFFF3300  // red eyes
#define MT 0xFF660000  // mouth
#define BD 0xFF5E3A1A  // body
#define LG 0xFF3B2010  // legs
static const uint32_t IMP_SPRITE[16][16] = {
    {_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X},
    {_X,_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X,_X},
    {_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X},
    {_X,_X,_X,_X,SK,RE,SK,SK,SK,SK,RE,SK,_X,_X,_X,_X},
    {_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X},
    {_X,_X,_X,_X,SK,SK,MT,SK,SK,MT,SK,SK,_X,_X,_X,_X},
    {_X,_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X,_X},
    {_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X},
    {_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X},
    {_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X},
    {_X,_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X,_X},
    {_X,_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X,_X},
    {_X,_X,_X,_X,LG,LG,LG,_X,_X,LG,LG,LG,_X,_X,_X,_X},
    {_X,_X,_X,_X,LG,LG,_X,_X,_X,_X,LG,LG,_X,_X,_X,_X},
    {_X,_X,_X,_X,LG,LG,_X,_X,_X,_X,LG,LG,_X,_X,_X,_X},
    {_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X},
};
#undef _X
#undef SK
#undef RE
#undef MT
#undef BD
#undef LG

// ─── Player ───────────────────────────────────────────────────────────────────
struct Player {
    float posX  = 2.5f, posY  = 2.5f;   // world position (map units)
    float dirX  = 1.0f, dirY  = 0.0f;   // unit direction vector
    float planeX= 0.0f, planeY= 0.66f;  // camera plane (⊥ to dir, |0.66|=~66° FOV)
    int   hp    = 100;
    int   ammo  = 30;
    float bobPhase      = 0.f;   // weapon bob phase
    float attackCooldown= 0.f;   // fire rate limiter (seconds until next shot)
    float flashTimer    = 0.f;   // muzzle flash remaining time
};

// ─── Enemy ────────────────────────────────────────────────────────────────────
struct Enemy {
    float x, y;
    int   hp       = 3;
    float hitFlash = 0.f;  // seconds of white flash remaining
    bool  alive    = true;
};

// ─── Bullet (world-space projectile) ─────────────────────────────────────────
struct Bullet {
    float x, y;    // map-space position
    float dx, dy;  // map units / second
    bool  alive = true;
};

// ─── Full game state ──────────────────────────────────────────────────────────
struct GameState {
    Player              player;
    std::vector<Enemy>  enemies;
    std::vector<Bullet> bullets;
    int    kills    = 0;
    int    frameNum = 0;
    bool   gameOver = false;
};
