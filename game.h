// =============================================================================
//  game.h  —  All pure game data: maps, structs, enums, sprites, configs
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

static const int W = 800, H = 600;
static const int MAP_W = 24, MAP_H = 24;

// ─── Maps (0=empty 1=stone 2=brick 3=concrete pillar) ─────────────────────────
static const int DUNGEON_MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},{1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},{1,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,1},
    {1,0,0,2,2,2,2,0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};
static const int ARENA_MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,3,0,0,0,0,0,3,0,0,0,0,3,0,0,0,0,0,3,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};
static const int CRYPT_MAP[MAP_H][MAP_W] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,2,2,2,2,2,2,0,0,0,0,2,2,2,2,2,2,2,2,0,1},{1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},{1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},{1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},{1,0,2,2,2,2,0,2,2,2,0,0,0,0,2,2,2,0,2,2,2,2,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,2,2,2,0,2,2,2,0,0,0,0,2,2,2,0,2,2,2,2,0,1},{1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},{1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},{1,0,2,0,3,0,0,3,0,2,0,0,0,0,2,0,3,0,0,3,0,2,0,1},
    {1,0,2,0,0,0,0,0,0,2,0,0,0,0,2,0,0,0,0,0,0,2,0,1},{1,0,2,2,2,2,2,2,2,2,0,0,0,0,2,2,2,2,2,2,2,2,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};
static const int* const ALL_MAPS[3] = { &DUNGEON_MAP[0][0], &ARENA_MAP[0][0], &CRYPT_MAP[0][0] };
static const char* const MAP_NAMES[3] = { "DUNGEON", "ARENA", "CRYPT" };
static const char* const MAP_DESCS[3] = {
    "Rooms & corridors. Enemies lurk around every corner.",
    "Open arena. Enemies have clear lines of sight.",
    "Sealed crypts. Close-quarters combat.",
};

// Spawn points
static const float DUNGEON_SPAWNS[12][2] = {{5.5f,5.5f},{19.5f,5.5f},{5.5f,18.5f},{19.5f,18.5f},{12.f,7.5f},{12.f,16.5f},{8.5f,11.f},{15.5f,11.f},{3.5f,5.5f},{20.5f,18.5f},{12.f,3.5f},{12.f,20.5f}};
static const float ARENA_SPAWNS[12][2]   = {{5.5f,5.5f},{17.5f,5.5f},{5.5f,17.5f},{17.5f,17.5f},{11.5f,5.5f},{5.5f,11.5f},{17.5f,11.5f},{11.5f,17.5f},{8.f,8.f},{14.f,8.f},{8.f,14.f},{14.f,14.f}};
static const float CRYPT_SPAWNS[12][2]   = {{5.f,5.5f},{16.f,5.5f},{5.f,17.5f},{16.f,17.5f},{11.5f,1.5f},{11.5f,22.f},{1.5f,11.5f},{22.f,11.5f},{5.f,11.5f},{16.f,11.5f},{11.5f,5.5f},{11.5f,17.5f}};
static const float (*ALL_SPAWNS[3])[2] = { DUNGEON_SPAWNS, ARENA_SPAWNS, CRYPT_SPAWNS };

// Health kit positions
static const float DUNGEON_KITS[6][2] = {{11.5f,11.5f},{4.f,4.5f},{19.5f,4.f},{4.f,19.f},{19.f,19.5f},{12.f,21.5f}};
static const float ARENA_KITS[6][2]   = {{5.5f,5.5f},{17.5f,5.5f},{5.5f,17.5f},{17.5f,17.5f},{11.5f,11.5f},{11.5f,3.5f}};
static const float CRYPT_KITS[6][2]   = {{5.5f,5.5f},{5.5f,17.5f},{16.5f,5.5f},{16.5f,17.5f},{11.5f,1.5f},{11.5f,22.f}};
static const float (*ALL_KITS[3])[2]  = { DUNGEON_KITS, ARENA_KITS, CRYPT_KITS };
static const int MAX_KITS = 6;

// ─── Imp sprite 16×16 ────────────────────────────────────────────────────────
#define _X 0x00000000
#define SK 0xFF9B6B3D
#define RE 0xFFFF3300
#define MT 0xFF660000
#define BD 0xFF5E3A1A
#define LG 0xFF3B2010
static const uint32_t IMP_SPRITE[16][16] = {
    {_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X},{_X,_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X,_X},
    {_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X},{_X,_X,_X,_X,SK,RE,SK,SK,SK,SK,RE,SK,_X,_X,_X,_X},
    {_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X},{_X,_X,_X,_X,SK,SK,MT,SK,SK,MT,SK,SK,_X,_X,_X,_X},
    {_X,_X,_X,_X,_X,SK,SK,SK,SK,SK,SK,_X,_X,_X,_X,_X},{_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X},
    {_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X},{_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X},
    {_X,_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X,_X},{_X,_X,_X,_X,BD,BD,BD,BD,BD,BD,BD,BD,_X,_X,_X,_X},
    {_X,_X,_X,_X,LG,LG,LG,_X,_X,LG,LG,LG,_X,_X,_X,_X},{_X,_X,_X,_X,LG,LG,_X,_X,_X,_X,LG,LG,_X,_X,_X,_X},
    {_X,_X,_X,_X,LG,LG,_X,_X,_X,_X,LG,LG,_X,_X,_X,_X},{_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X,_X},
};
#undef _X
#undef SK
#undef RE
#undef MT
#undef BD
#undef LG

// ─── Med-kit sprite 16×16 ────────────────────────────────────────────────────
#define _T 0x00000000
#define _B 0xFF116611
#define _G 0xFF22AA44
#define _W 0xFFEEFFEE
#define _R 0xFFDD3333
static const uint32_t MEDKIT_SPRITE[16][16] = {
    {_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T},
    {_T,_T,_T,_B,_B,_B,_B,_B,_B,_B,_B,_B,_B,_T,_T,_T},
    {_T,_T,_T,_B,_G,_G,_G,_G,_G,_G,_G,_G,_B,_T,_T,_T},
    {_T,_T,_T,_B,_G,_W,_W,_R,_R,_W,_W,_G,_B,_T,_T,_T},
    {_T,_B,_B,_B,_G,_W,_W,_R,_R,_W,_W,_G,_B,_B,_B,_T},
    {_T,_B,_R,_R,_R,_R,_R,_R,_R,_R,_R,_R,_R,_R,_B,_T},
    {_T,_B,_R,_R,_R,_R,_R,_W,_W,_R,_R,_R,_R,_R,_B,_T},
    {_T,_B,_R,_R,_R,_R,_R,_W,_W,_R,_R,_R,_R,_R,_B,_T},
    {_T,_B,_R,_R,_R,_R,_R,_R,_R,_R,_R,_R,_R,_R,_B,_T},
    {_T,_B,_B,_B,_G,_W,_W,_R,_R,_W,_W,_G,_B,_B,_B,_T},
    {_T,_T,_T,_B,_G,_W,_W,_R,_R,_W,_W,_G,_B,_T,_T,_T},
    {_T,_T,_T,_B,_G,_G,_G,_G,_G,_G,_G,_G,_B,_T,_T,_T},
    {_T,_T,_T,_B,_B,_B,_B,_B,_B,_B,_B,_B,_B,_T,_T,_T},
    {_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T},
    {_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T},
    {_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T},
};
#undef _T
#undef _B
#undef _G
#undef _W
#undef _R

// ─── Enums ────────────────────────────────────────────────────────────────────
enum BulletOwner  { PLAYER_SHOT, ENEMY_SHOT };
enum GameScreen   { SCR_MENU, SCR_MAP_SELECT, SCR_DIFF_SELECT, SCR_CHAR_SELECT, SCR_PLAYING };
enum CharClass    { CLS_ASSAULT=0, CLS_SCOUT=1, CLS_TANK=2 };
enum Difficulty   { DIFF_EASY=0, DIFF_MEDIUM=1, DIFF_HARD=2 };

// ─── Difficulty config ────────────────────────────────────────────────────────
struct DiffConfig {
    const char *name, *desc1, *desc2, *desc3;
    float enemyHPMult, fireCoolMult, bulletSpeedMult, dmgReceivedMult, speedBonus;
    int   numKits;
};
static const DiffConfig DIFF_CFG[3] = {
    {"EASY",   "Enemy HP:    60%", "Fire Rate:   slow",  "Kits: 6  DMG: -45%",
     0.60f, 2.0f,  0.70f, 0.55f, 0.2f, 6},
    {"MEDIUM", "Enemy HP:   100%", "Fire Rate: normal", "Kits: 4  DMG: 100%",
     1.00f, 1.0f,  1.00f, 1.00f, 0.0f, 4},
    {"HARD",   "Enemy HP:   160%", "Fire Rate:  fast",  "Kits: 2  DMG: +50%",
     1.60f, 0.55f, 1.30f, 1.50f, 0.0f, 2},
};

// ─── Character profiles ───────────────────────────────────────────────────────
struct CharProfile {
    const char *name, *stat1, *stat2, *stat3;
    int   hp, ammo;
    float speedMult, stamina, damageReduction;
    uint32_t sleeve, sleeveHi, skinColor;
};
static const CharProfile CHAR_PROFILES[3] = {
    {"ASSAULT","HP: 100    Speed: 1.0x","Ammo: 30   Stamina: 100","Damage reduction: 0%",   100,30,1.0f,100.f,0.0f,0xFF2C3020,0xFF3A4028,0xFF7A4C2A},
    {"SCOUT",  "HP:  75    Speed: 1.4x","Ammo: 25   Stamina: 150","Damage reduction: 0%",    75,25,1.4f,150.f,0.0f,0xFF181820,0xFF242430,0xFF7A4C2A},
    {"TANK",   "HP: 200    Speed: 0.7x","Ammo: 20   Stamina:  80","Damage reduction: 50%",  200,20,0.7f, 80.f,0.5f,0xFF3D2B12,0xFF4A3518,0xFF8A5C32},
};

// ─── Blood particle ───────────────────────────────────────────────────────────
struct Particle { float x,y,z,vx,vy,vz,life; uint32_t color; };

// ─── Health kit ───────────────────────────────────────────────────────────────
struct HealthKit { float x, y, bobPhase=0; bool active=true; };

// ─── Player ───────────────────────────────────────────────────────────────────
struct Player {
    float posX=2.5f,posY=2.5f,dirX=1.f,dirY=0.f,planeX=0.f,planeY=0.66f;
    int   hp=100,ammo=30;
    float stamina=100.f,speedMult=1.f,damageReduction=0.f;
    float bobPhase=0,attackCooldown=0,flashTimer=0,damageFlash=0;
    bool  adsMode=false,sprinting=false;
    float shakeY=0;
};

// ─── Enemy ────────────────────────────────────────────────────────────────────
struct Enemy { float x,y; int hp=3; float hitFlash=0,fireCooldown=3.f,aggroRange=5.f; bool alive=true; };

// ─── Bullet ──────────────────────────────────────────────────────────────────
struct Bullet { float x,y,dx,dy; BulletOwner owner=PLAYER_SHOT; bool alive=true; };

// ─── Level config ─────────────────────────────────────────────────────────────
struct LevelCfg { int numEnemies,killsNeeded,enemyHP; float aggroRange,fireCooldown,bulletSpeed; };
static const LevelCfg LEVEL_CFG[] = {
    {0,0,0,0,0,0},{6,4,3,5.f,3.0f,4.5f},{8,6,4,6.f,2.5f,5.5f},
    {10,8,5,7.f,2.0f,6.5f},{12,10,7,8.f,1.5f,7.5f},{12,12,10,9.f,1.0f,8.5f},
};

// ─── GameState ────────────────────────────────────────────────────────────────
struct GameState {
    Player player;
    std::vector<Enemy>     enemies;
    std::vector<Bullet>    bullets;
    std::vector<Particle>  particles;
    std::vector<HealthKit> healthKits;
    int   kills=0,killsThisLevel=0,killsNeeded=4,level=1,score=0,frameNum=0;
    float levelTransTimer=0,enemyBulletSpeed=4.5f,enemyFireRate=3.f;
    bool  levelComplete=false,gameOver=false;
    int   mapChoice=0;
    CharClass charClass=CLS_ASSAULT;
    Difficulty difficulty=DIFF_MEDIUM;
};
