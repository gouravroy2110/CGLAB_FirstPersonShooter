// =============================================================================
//  game.h  —  FPS Raycaster  |  Week 1+  (fully software-rasterized)
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// ─── Framebuffer ─────────────────────────────────────────────────────────────
static const int W     = 800;
static const int H     = 600;

// ─── Map ─────────────────────────────────────────────────────────────────────
static const int MAP_W = 24;
static const int MAP_H = 24;
// 0=empty  1=stone  2=brick  3=concrete pillar
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

// Enemy spawn grid — picked from here in order, more per level
static const float SPAWN_PTS[12][2] = {
    {5.5f,5.5f}, {19.5f,5.5f}, {5.5f,18.5f}, {19.5f,18.5f},
    {12.0f,7.5f},{12.0f,16.5f},{8.5f,11.0f}, {15.5f,11.0f},
    {3.5f,5.5f}, {20.5f,18.5f},{12.0f,3.5f}, {12.0f,20.5f},
};

// ─── Imp sprite  16×16 pixel-art  (0xAARRGGBB, A=0→transparent) ─────────────
#define _X 0x00000000
#define SK 0xFF9B6B3D
#define RE 0xFFFF3300
#define MT 0xFF660000
#define BD 0xFF5E3A1A
#define LG 0xFF3B2010
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

// ─── Bullet ownership ─────────────────────────────────────────────────────────
enum BulletOwner { PLAYER_SHOT, ENEMY_SHOT };

// ─── Player ───────────────────────────────────────────────────────────────────
struct Player {
    float posX = 2.5f, posY = 2.5f;
    float dirX = 1.0f, dirY = 0.0f;
    float planeX = 0.0f, planeY = 0.66f;
    int   hp       = 100;
    int   ammo     = 30;
    float stamina  = 100.f;    // sprint fuel 0–100
    bool  sprinting= false;
    float bobPhase = 0.f;
    float attackCooldown = 0.f;
    float flashTimer     = 0.f;  // muzzle flash
    float damageFlash    = 0.f;  // red vignette on hit
};

// ─── Enemy ────────────────────────────────────────────────────────────────────
struct Enemy {
    float x, y;
    int   hp          = 3;
    float hitFlash    = 0.f;
    bool  alive       = true;
    float fireCooldown= 3.0f;  // seconds until next shot
    float aggroRange  = 5.0f;  // max distance to detect+fire
};

// ─── Bullet ───────────────────────────────────────────────────────────────────
struct Bullet {
    float x, y, dx, dy;
    BulletOwner owner = PLAYER_SHOT;
    bool  alive       = true;
};

// ─── Per-level config ─────────────────────────────────────────────────────────
struct LevelCfg {
    int   numEnemies, killsNeeded, enemyHP;
    float aggroRange, fireCooldown, bulletSpeed;
};
static const LevelCfg LEVEL_CFG[] = {
    {0,  0,  0, 0.f, 0.f,  0.f},   // 0: unused
    {6,  4,  3, 5.f, 3.0f, 4.5f},  // 1: easy
    {8,  6,  4, 6.f, 2.5f, 5.5f},  // 2
    {10, 8,  5, 7.f, 2.0f, 6.5f},  // 3
    {12, 10, 7, 8.f, 1.5f, 7.5f},  // 4
    {12, 12,10, 9.f, 1.0f, 8.5f},  // 5+
};

// ─── Full game state ──────────────────────────────────────────────────────────
struct GameState {
    Player             player;
    std::vector<Enemy> enemies;
    std::vector<Bullet>bullets;
    int   kills            = 0;
    int   killsThisLevel   = 0;
    int   killsNeeded      = 4;
    int   level            = 1;
    int   score            = 0;
    float levelTransTimer  = 0.f; // >0 = "level complete" screen showing
    bool  levelComplete    = false;
    bool  gameOver         = false;
    int   frameNum         = 0;
    float enemyBulletSpeed = 4.5f;
    float enemyFireRate    = 3.0f;
};
