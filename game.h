// =============================================================================
//  game.h  -  Pure game data: maps, configs, weapons, entity structs
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

static const int W = 800, H = 600;
static const int MAP_W = 40, MAP_H = 40;

#include "maps.h"

// --- Map types & parsing -------------------------------------------------------
enum CellType { CELL_FLOOR=0, CELL_STONE=1, CELL_BRICK=2, CELL_PILLAR=3, CELL_DOOR=4, CELL_SECRET=5 };

static const char* const MAP_NAMES[3] = { "DUNGEON", "ARENA", "CRYPT" };
static const char* const MAP_DESCS[3] = {
    "Rooms behind doors, an acid vault and two hidden chambers.",
    "Rolling hills, a fortified mesa and a hidden bunker.",
    "Sealed crypts, an altar ledge and an ossuary across the acid.",
};
static const char* const* const ALL_LAYOUTS[3] = { DUNGEON_LAYOUT, ARENA_LAYOUT, CRYPT_LAYOUT };
static const float TERRAIN_AMP[3] = { 0.25f, 0.75f, 0.35f };   // rolling-hill amplitude

// World height of each wall type above its base
static const float WALL_HEIGHT[6] = { 0.f, 1.15f, 1.0f, 1.5f, 1.0f, 1.15f };

struct Marker { char kind; float x, y; };
struct ParsedMap {
    int   type[MAP_H][MAP_W];
    float plat[MAP_H][MAP_W];     // ledge / pit offset added to the rolling terrain
    bool  acid[MAP_H][MAP_W];
    std::vector<Marker> markers;
    float spawnX = 1.5f, spawnY = 1.5f, camX = 2.5f, camY = 2.5f;
};

inline bool  isGroundChar(char c) { return c == '.' || c == '-' || c == '^' || c == '=' || c == 'v' || c == '~'; }
inline float platForChar(char c) {
    switch (c) { case '-': return 0.25f; case '^': return 0.5f; case '=': return 1.0f; case 'v': return -0.6f; case '~': return -0.5f; default: return 0.f; }
}

inline void parseLayout(const char *const *rows, ParsedMap &m)
{
    m.markers.clear();
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
        char c = rows[y][x];
        int t = CELL_FLOOR;
        switch (c) { case '#': t = CELL_STONE; break; case 'B': t = CELL_BRICK; break; case 'P': t = CELL_PILLAR; break;
                     case 'D': t = CELL_DOOR; break; case 'S': t = CELL_SECRET; break; default: break; }
        m.type[y][x] = t;
        m.plat[y][x] = platForChar(c);
        m.acid[y][x] = c == '~';
        if (t == CELL_FLOOR && !isGroundChar(c)) {
            // Marker: sit at the most common ground height among the 8 neighbours
            char best = '.'; int bestN = -1;
            const char kinds[6] = { '.', '-', '^', '=', 'v', '~' };
            for (char k : kinds) {
                int n = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
                    if ((dx || dy) && rows[y + dy][x + dx] == k) n++;
                if (n > bestN || (n == bestN && platForChar(k) > platForChar(best))) { best = k; bestN = n; }
            }
            m.plat[y][x] = platForChar(best);
            m.acid[y][x] = best == '~';
            if (c == '@') { m.spawnX = x + 0.5f; m.spawnY = y + 0.5f; }
            else if (c == 'm') { m.camX = x + 0.5f; m.camY = y + 0.5f; }
            else m.markers.push_back(Marker{c, x + 0.5f, y + 0.5f});
        }
    }
}

// --- Enums -------------------------------------------------------------------
enum BulletOwner  { PLAYER_SHOT, ENEMY_SHOT };
enum GameScreen   { SCR_MENU, SCR_MAP_SELECT, SCR_DIFF_SELECT, SCR_CHAR_SELECT, SCR_PLAYING };
enum CharClass    { CLS_ASSAULT=0, CLS_SCOUT=1, CLS_TANK=2 };
enum Difficulty   { DIFF_EASY=0, DIFF_MEDIUM=1, DIFF_HARD=2 };
enum HitPart      { PART_NONE=0, PART_BODY=1, PART_HEAD=2, PART_LIMB=3 };

// --- Difficulty config -------------------------------------------------------
struct DiffConfig {
    const char *name, *desc1, *desc2, *desc3;
    float enemyHPMult, fireCoolMult, bulletSpeedMult, dmgReceivedMult, speedBonus;
    int   numKits;
    float enemyAccuracy;   // 0..1, scales enemy aim spread
    float aggroRangeMult;  // multiplier for enemy aggro & detection range
};
static const DiffConfig DIFF_CFG[3] = {
    {"EASY",   "Enemy HP:    60%", "Fire Rate:   slow",  "Kits: 6  DMG: -45%",
     0.60f, 2.0f,  0.70f, 0.55f, 0.2f, 6, 0.55f, 0.85f},
    {"MEDIUM", "Enemy HP:   100%", "Fire Rate: normal", "Kits: 4  DMG: 100%",
     1.00f, 1.0f,  1.00f, 1.00f, 0.0f, 4, 0.75f, 1.00f},
    {"HARD",   "Enemy HP:   160%", "Fire Rate:  fast",  "Kits: 2  DMG: +50%",
     1.60f, 0.55f, 1.30f, 1.50f, 0.0f, 2, 0.90f, 1.35f},
};

// --- Character profiles ------------------------------------------------------
struct CharProfile {
    const char *name, *stat1, *stat2, *stat3;
    int   hp, ammo;             // ammo = starting pistol reserve / 2
    float speedMult, stamina, damageReduction;
    uint32_t sleeve, sleeveHi, skinColor;
};
static const CharProfile CHAR_PROFILES[3] = {
    {"ASSAULT","HP: 100    Speed: 1.0x","Ammo: 60   Stamina: 100","Damage reduction: 0%",   100,30,1.0f,100.f,0.0f,0xFF3B4229,0xFF4D5636,0xFFB98A64},
    {"SCOUT",  "HP:  75    Speed: 1.4x","Ammo: 50   Stamina: 150","Damage reduction: 0%",    75,25,1.4f,150.f,0.0f,0xFF23252E,0xFF33364A,0xFF9C6E4E},
    {"TANK",   "HP: 200    Speed: 0.7x","Ammo: 40   Stamina:  80","Damage reduction: 50%",  200,20,0.7f, 80.f,0.5f,0xFF5A4526,0xFF6E5530,0xFFC49A74},
};

// --- Weapons -----------------------------------------------------------------
enum WeaponId { WPN_KNIFE=0, WPN_PISTOL=1, WPN_SHOTGUN=2, WPN_RIFLE=3, WPN_COUNT=4 };
struct WeaponDef {
    const char *name, *shortName;
    int   damage;        // per pellet / strike, body hit
    float headMult;      // headshot multiplier
    float limbMult;      // arm / leg multiplier
    float fireDelay;     // seconds between shots
    int   pellets;
    float spread;        // hip-fire spread, in screen-fraction units
    int   magSize;
    float reloadTime;
    float recoil;        // view-kick strength
    bool  automatic;
    float adsFov;        // FOV multiplier when aiming (smaller = more zoom)
    bool  scope;         // true = full-screen magnified optic, false = reflex sight
    int   maxReserve;
};
static const WeaponDef WEAPONS[WPN_COUNT] = {
    {"COMBAT KNIFE",  "KNIFE",   55, 2.5f, 0.80f, 0.35f, 1, 0.000f,  0, 0.00f, 0.1f, true,  1.00f, false,   0},
    {"M9 SIDEARM",    "PISTOL",  28, 3.0f, 0.75f, 0.24f, 1, 0.012f, 12, 1.25f, 1.0f, false, 0.70f, false,  96},
    {"M870 SHOTGUN",  "SHOTGUN", 24, 2.0f, 0.75f, 0.58f, 8, 0.045f,  6, 2.00f, 2.0f, false, 0.80f, false,  48},
    {"M4 CARBINE",    "RIFLE",   22, 2.5f, 0.70f, 0.10f, 1, 0.016f, 30, 1.90f, 0.8f, true,  0.65f, false,  180},
};

// --- Particles ---------------------------------------------------------------
enum ParticleType { PT_BLOOD, PT_SPARK, PT_DUST, PT_SMOKE, PT_DEBRIS };
struct Particle {
    float x,y,z,vx,vy,vz,life,maxLife,size;
    uint32_t color;
    ParticleType type = PT_BLOOD;
};

// --- Pickups -----------------------------------------------------------------
struct HealthKit { float x, y, bobPhase=0; bool active=true; bool emergency=false; };
enum PickupType { PICKUP_AMMO, PICKUP_SHOTGUN, PICKUP_RIFLE, PICKUP_INTEL };
struct WeaponPickup { float x, y, bobPhase=0; PickupType type=PICKUP_AMMO; bool active=true; bool emergency=false; bool reward=false; };

// --- Player ------------------------------------------------------------------
struct Player {
    float posX=2.5f,posY=2.5f,dirX=1.f,dirY=0.f,planeX=0.f,planeY=0.66f;
    int   hp=100;
    float stamina=100.f,speedMult=1.f,damageReduction=0.f;

    // Horizontal movement velocity.
    // These values allow acceleration, friction and airborne momentum.
    float velX=0.f,velY=0.f;

    float bobPhase=0,attackCooldown=0,flashTimer=0,damageFlash=0;
    bool  adsMode=false,sprinting=false;
    float shakeY=0;
    // Weapons
    int   curWeapon=WPN_PISTOL;
    bool  owned[WPN_COUNT]={true,true,false,false};
    int   mag[WPN_COUNT]={0,12,0,0};
    int   reserve[WPN_COUNT]={0,60,0,0};
    float reloadTimer=0;     // >0 while reloading
    float switchTimer=0;     // >0 while swapping weapons
    bool  triggerHeld=false;
    // Weapon Upgrades & Attachments:
    bool  upgradeExtMag[WPN_COUNT]      = {false, false, false, false};
    bool  upgradeCompensator[WPN_COUNT] = {false, false, false, false};
    bool  upgradeAmmo[WPN_COUNT]        = {false, false, false, false};
    bool  upgradeSpecial[WPN_COUNT]     = {false, false, false, false};
};

// --- Enemy -------------------------------------------------------------------
enum EnemyState { ES_IDLE, ES_ALERT, ES_DEAD };
enum EnemyType  { ET_RIFLEMAN=0, ET_SCOUT=1, ET_SHOTGUNNER=2, ET_SNIPER=3 };
struct Enemy {
    float x,y;
    int   hp=75, maxHp=75;
    float hitFlash=0, fireCooldown=3.f, aggroRange=5.f;
    bool  alive=true;
    EnemyState state=ES_IDLE;
    EnemyType  type=ET_RIFLEMAN;
    float walkPhase=0, shootAnim=0, deathTime=0, corpseFade=1.f;
    float moveX=0, moveY=0, strafeTimer=0, wanderTimer=0;
    bool  moving=false, killedByHeadshot=false;
    bool  elite=false;           // vault guard: tougher, faster, darker kit
    float alertDirX=0.f, alertDirY=0.f, alertDist=0.f;
    float hpShow=0.f;             // seconds left to show the floating health bar
    bool  boss=false;             // zone commander: big, tough, drops a weapon
    float scale=1.f;              // sprite / hit-box size in world units
};

// --- Projectiles -------------------------------------------------------------
// Enemy rounds are slow glowing projectiles you can dodge.
struct Bullet { float x,y,z,dx,dy,dz=0; float px=0,py=0,pz=0; BulletOwner owner=ENEMY_SHOT; bool alive=true; int damage=0; bool whizzed=false; };
// Player rounds are hitscan; a tracer streak flies along the resolved path.
struct Tracer { float x0,y0,z0, x1,y1,z1, t=0, len=1; };
// Floating damage number
struct DamageNum { float x,y,z; int value; bool head; float life; };

// --- Level config ------------------------------------------------------------
struct LevelCfg { int numEnemies,killsNeeded,enemyHP; float aggroRange,fireCooldown,bulletSpeed; };
static const LevelCfg LEVEL_CFG[] = {
    {0,0,0,0,0,0},{6,4,75,6.f,3.0f,4.5f},{8,6,100,7.f,2.5f,5.5f},
    {10,8,125,8.f,2.0f,6.5f},{12,10,175,9.f,1.5f,7.5f},{12,12,250,10.f,1.0f,8.5f},
};

// --- GameState ---------------------------------------------------------------
struct GameState {
    Player player;
    std::vector<Enemy>     enemies;
    std::vector<Bullet>    bullets;
    std::vector<Tracer>    tracers;
    std::vector<Particle>  particles;
    std::vector<HealthKit> healthKits;
    std::vector<WeaponPickup> weaponPickups;
    std::vector<DamageNum> damageNums;
    int   kills=0,killsThisLevel=0,killsNeeded=4,level=1,score=0,frameNum=0;
    int   headshots=0, shotsFired=0, shotsHit=0, secretsFound=0, secretsTotal=0;
    float levelTransTimer=0,enemyBulletSpeed=4.5f,enemyFireRate=3.f;
    // Per-zone tallies for the zone report / bonus checklist
    int   coins=0;
    int   zoneKills=0, zoneHeadshots=0, zoneShots=0, zoneHits=0, zoneDamage=0, zoneSecrets=0, zoneSecretsTotal=0;
    float zoneTime=0;
    bool  zoneHadBoss=false, zoneBossKilled=false;
    bool  levelComplete=false,gameOver=false;
    int   mapChoice=0;
    CharClass charClass=CLS_ASSAULT;
    Difficulty difficulty=DIFF_MEDIUM;
};
