// =============================================================================
//  game.h  —  All pure game data (no Qt). Three maps, three character classes,
//             blood particles, enums for menus.
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

static const int W = 800, H = 600;
static const int MAP_W = 24, MAP_H = 24;

// ─── Maps ─────────────────────────────────────────────────────────────────────
// 0=empty  1=stone  2=brick  3=concrete pillar

// MAP 1: Dungeon (rooms + corridors)
static const int DUNGEON_MAP[MAP_H][MAP_W] = {
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

// MAP 2: Arena (wide open, grid of concrete pillars)
static const int ARENA_MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

// MAP 3: Crypt (4 enclosed brick chambers + outer stone corridors)
static const int CRYPT_MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,2,2,2,2,2,2,0,0,0,0,2,2,2,2,2,2,2,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,2,2,2,0,2,2,2,0,0,0,0,2,2,2,0,2,2,2,2,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,2,2,0,2,2,2,0,0,0,0,2,2,2,0,2,2,2,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,2,2,2,2,2,2,2,0,0,0,0,2,2,2,2,2,2,2,2,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

// Active map is copied into MainWindow::m_map at level load.
// Index 0=dungeon, 1=arena, 2=crypt
static const int* const ALL_MAPS[3] = {
    &DUNGEON_MAP[0][0], &ARENA_MAP[0][0], &CRYPT_MAP[0][0]
};
static const char* const MAP_NAMES[3] = { "DUNGEON", "ARENA", "CRYPT" };
static const char* const MAP_DESCS[3] = {
    "Rooms & corridors. Enemies lurk around every corner.",
    "Open arena. Enemies have clear lines of sight.",
    "Sealed crypts. Close-quarters combat.",
};

// Spawn points per map (ensure positions are open tiles)
static const float DUNGEON_SPAWNS[12][2] = {
    {5.5f,5.5f},{19.5f,5.5f},{5.5f,18.5f},{19.5f,18.5f},
    {12.f,7.5f},{12.f,16.5f},{8.5f,11.f},{15.5f,11.f},
    {3.5f,5.5f},{20.5f,18.5f},{12.f,3.5f},{12.f,20.5f},
};
static const float ARENA_SPAWNS[12][2] = {
    {5.5f,5.5f},{17.5f,5.5f},{5.5f,17.5f},{17.5f,17.5f},
    {11.5f,5.5f},{5.5f,11.5f},{17.5f,11.5f},{11.5f,17.5f},
    {8.f,8.f},{14.f,8.f},{8.f,14.f},{14.f,14.f},
};
static const float CRYPT_SPAWNS[12][2] = {
    {5.f,5.5f},{16.f,5.5f},{5.f,17.5f},{16.f,17.5f},
    {11.5f,1.5f},{11.5f,22.f},{1.5f,11.5f},{22.f,11.5f},
    {5.f,11.5f},{16.f,11.5f},{11.5f,5.5f},{11.5f,17.5f},
};
static const float (*ALL_SPAWNS[3])[2] = {
    DUNGEON_SPAWNS, ARENA_SPAWNS, CRYPT_SPAWNS
};

// ─── Imp sprite  16×16  (A=0 → transparent) ──────────────────────────────────
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

// ─── Enums ────────────────────────────────────────────────────────────────────
enum BulletOwner  { PLAYER_SHOT, ENEMY_SHOT };
enum GameScreen   { SCR_MENU, SCR_MAP_SELECT, SCR_CHAR_SELECT, SCR_PLAYING };
enum CharClass    { CLS_ASSAULT = 0, CLS_SCOUT = 1, CLS_TANK = 2 };

// ─── Character profiles ───────────────────────────────────────────────────────
struct CharProfile {
    const char *name, *stat1, *stat2, *stat3;
    int   hp, ammo;
    float speedMult, stamina, damageReduction;
    uint32_t sleeve, sleeveHi, skinColor;
};
static const CharProfile CHAR_PROFILES[3] = {
    {"ASSAULT",
     "HP: 100    Speed: 1.0x",
     "Ammo: 30   Stamina: 100",
     "Damage reduction: 0%",
     100, 30, 1.0f, 100.f, 0.0f,
     0xFF2C3020, 0xFF3A4028, 0xFF7A4C2A},
    {"SCOUT",
     "HP:  75    Speed: 1.4x",
     "Ammo: 25   Stamina: 150  (long sprint)",
     "Damage reduction: 0%",
     75, 25, 1.4f, 150.f, 0.0f,
     0xFF181820, 0xFF242430, 0xFF7A4C2A},
    {"TANK",
     "HP: 200    Speed: 0.7x",
     "Ammo: 20   Stamina:  80",
     "Damage reduction: 50%",
     200, 20, 0.7f, 80.f, 0.5f,
     0xFF3D2B12, 0xFF4A3518, 0xFF8A5C32},
};

// ─── Blood particle ───────────────────────────────────────────────────────────
struct Particle {
    float x, y;    // world XY
    float z;       // height above floor (0=floor, ~0.5=waist)
    float vx, vy, vz;
    float life;    // 1→0
    uint32_t color;
};

// ─── Player ───────────────────────────────────────────────────────────────────
struct Player {
    float posX=2.5f, posY=2.5f;
    float dirX=1.f,  dirY=0.f;
    float planeX=0.f, planeY=0.66f;
    int   hp=100, ammo=30;
    float stamina=100.f;
    bool  sprinting=false;
    float speedMult=1.f, damageReduction=0.f;
    float bobPhase=0;
    float attackCooldown=0, flashTimer=0, damageFlash=0;
    bool  adsMode=false;
    float shakeY=0;   // vertical camera shake in pixels (decays per frame)
};

// ─── Enemy ───────────────────────────────────────────────────────────────────
struct Enemy {
    float x,y;
    int   hp=3;
    float hitFlash=0, fireCooldown=3.f, aggroRange=5.f;
    bool  alive=true;
};

// ─── Bullet ──────────────────────────────────────────────────────────────────
struct Bullet {
    float x,y,dx,dy;
    BulletOwner owner=PLAYER_SHOT;
    bool alive=true;
};

// ─── Level config ─────────────────────────────────────────────────────────────
struct LevelCfg { int numEnemies,killsNeeded,enemyHP; float aggroRange,fireCooldown,bulletSpeed; };
static const LevelCfg LEVEL_CFG[] = {
    {0,0,0,0,0,0},
    {6,4,3,5.f,3.0f,4.5f},
    {8,6,4,6.f,2.5f,5.5f},
    {10,8,5,7.f,2.0f,6.5f},
    {12,10,7,8.f,1.5f,7.5f},
    {12,12,10,9.f,1.0f,8.5f},
};

// ─── GameState ────────────────────────────────────────────────────────────────
struct GameState {
    Player player;
    std::vector<Enemy>    enemies;
    std::vector<Bullet>   bullets;
    std::vector<Particle> particles;
    int   kills=0, killsThisLevel=0, killsNeeded=4;
    int   level=1, score=0;
    float levelTransTimer=0;
    bool  levelComplete=false, gameOver=false;
    int   frameNum=0;
    float enemyBulletSpeed=4.5f, enemyFireRate=3.f;
    // Pre-game selections (set before calling loadLevel)
    int   mapChoice=0;
    CharClass charClass=CLS_ASSAULT;
};
