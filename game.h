// =============================================================================
//  game.h  -  Pure game data: maps, configs, weapons, entity structs
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

static const int W = 800, H = 600;

// Row-major 2D array sized at run time; g[y][x] indexing like a C array
template <class T>
struct Grid {
    int w = 0, h = 0;
    std::vector<T> v;
    void resize(int nw, int nh, T init = T()) { w = nw; h = nh; v.assign(size_t(nw) * nh, init); }
    T       *operator[](int y)       { return v.data() + size_t(y) * w; }
    const T *operator[](int y) const { return v.data() + size_t(y) * w; }
};

#include "maps.h"

// --- Map types & parsing -------------------------------------------------------
enum CellType { CELL_FLOOR=0, CELL_STONE=1, CELL_BRICK=2, CELL_PILLAR=3, CELL_DOOR=4, CELL_SECRET=5 };

static const int MAP_COUNT = 6;
// Each map has its own biome (textures, sky, hazard, weather), terrain shape and layout
static const char* const MAP_NAMES[MAP_COUNT] = { "EMBERFORGE", "DUNE OUTPOST", "MIRE HOLLOW", "FROSTLINE", "ASHEN CITY", "HELIX FACILITY" };
static const char* const MAP_DESCS[MAP_COUNT] = {
    "Volcanic tunnels: lava pools, sealed forges and two hidden chambers.",
    "Desert dunes, a fortified mesa and a buried bunker.",
    "Swamp forest: bog pools, trees and drowned tombs.",
    "64x64 snowfield: village, frozen canyon, pine forest, central fort.",
    "64x64 ruined city: gutted blocks, a walled plaza and its main gate.",
    "56x56 research complex: labs, vaults and the reactor hall.",
};
static const char* const* const ALL_LAYOUTS[MAP_COUNT] = { DUNGEON_LAYOUT, ARENA_LAYOUT, CRYPT_LAYOUT, FRONTIER_LAYOUT, RUINS_LAYOUT, FACILITY_LAYOUT };
#define LAYOUT_N(L) int(sizeof(L) / sizeof(L[0]))
static const int LAYOUT_ROWS[MAP_COUNT] = { LAYOUT_N(DUNGEON_LAYOUT), LAYOUT_N(ARENA_LAYOUT), LAYOUT_N(CRYPT_LAYOUT),
                                            LAYOUT_N(FRONTIER_LAYOUT), LAYOUT_N(RUINS_LAYOUT), LAYOUT_N(FACILITY_LAYOUT) };
static const float TERRAIN_AMP[MAP_COUNT] = { 0.3f, 0.85f, 0.3f, 0.9f, 0.1f, 0.02f };   // rolling-hill amplitude
static const int   MAP_THEME[MAP_COUNT]   = { 0, 1, 2, 3, 4, 5 };                       // biome per map (assets.h Biome)

// World height of each wall type above its base
static const float WALL_HEIGHT[6] = { 0.f, 1.15f, 1.0f, 1.5f, 1.0f, 1.15f };

struct Marker { char kind; float x, y; };
struct ParsedMap {
    int   w = 0, h = 0;
    Grid<int>     type;
    Grid<float>   plat;           // ledge / pit offset added to the rolling terrain
    Grid<uint8_t> acid;
    std::vector<Marker> markers;
    float spawnX = 1.5f, spawnY = 1.5f, camX = 2.5f, camY = 2.5f;
};

inline bool  isGroundChar(char c) { return c == '.' || c == '-' || c == '^' || c == '=' || c == 'v' || c == '~'; }
inline float platForChar(char c) {
    switch (c) { case '-': return 0.25f; case '^': return 0.5f; case '=': return 1.0f; case 'v': return -0.6f; case '~': return -0.5f; default: return 0.f; }
}

inline void parseLayout(const char *const *rows, int nRows, ParsedMap &m)
{
    m.markers.clear();
    m.h = nRows;
    m.w = 0;
    while (rows[0][m.w]) m.w++;
    m.type.resize(m.w, m.h); m.plat.resize(m.w, m.h); m.acid.resize(m.w, m.h);
    for (int y = 0; y < m.h; y++) for (int x = 0; x < m.w; x++) {
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
enum GameScreen   { SCR_MENU, SCR_MAP_SELECT, SCR_DIFF_SELECT, SCR_CHAR_SELECT, SCR_PLAYING, SCR_SETTINGS, SCR_ARSENAL, SCR_EDITOR, SCR_CAMPAIGN, SCR_BRIEFING, SCR_DEBRIEF };
enum CharClass    { CLS_ASSAULT=0, CLS_SCOUT=1, CLS_TANK=2 };
enum Difficulty   { DIFF_EASY=0, DIFF_MEDIUM=1, DIFF_HARD=2 };
enum HitPart      { PART_NONE=0, PART_BODY=1, PART_HEAD=2, PART_LIMB=3, PART_SHIELD=4 };

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

// --- Weapons: the arsenal table lives in weapons.h ----------------------------
#include "weapons.h"

// --- Particles ---------------------------------------------------------------
enum ParticleType { PT_BLOOD, PT_SPARK, PT_DUST, PT_SMOKE, PT_DEBRIS, PT_FLAME };
struct Particle {
    float x,y,z,vx,vy,vz,life,maxLife,size;
    uint32_t color;
    ParticleType type = PT_BLOOD;
};

// --- Pickups -----------------------------------------------------------------
struct HealthKit { float x, y, bobPhase=0; bool active=true; bool emergency=false; };
enum PickupType { PICKUP_AMMO, PICKUP_SHOTGUN, PICKUP_RIFLE, PICKUP_INTEL, PICKUP_WEAPON, PICKUP_THROW, PICKUP_KEY };
struct WeaponPickup {
    float x, y, bobPhase=0; PickupType type=PICKUP_AMMO; bool active=true; bool emergency=false; bool reward=false;
    int   weapon=-1;          // PICKUP_WEAPON: which gun
    Loadout load;             // its rarity + attachments
    int   mag=-1, reserve=-1; // a dropped gun keeps its ammo (-1 = fresh)
    int   throwKind=0;        // PICKUP_THROW
};

// --- Player ------------------------------------------------------------------
struct Player {
    float posX=2.5f,posY=2.5f,dirX=1.f,dirY=0.f,planeX=0.f,planeY=0.66f;
    int   hp=100;
    float stamina=100.f,speedMult=1.f,damageReduction=0.f;

    // Horizontal movement velocity.
    // These values allow acceleration, friction and airborne momentum.
    float velX=0.f,velY=0.f;
    // Vertical state: feet height, vertical speed, standing on the ground
    float footZ=0.f,velZ=0.f;
    bool  onGround=true;

    float bobPhase=0,attackCooldown=0,flashTimer=0,damageFlash=0;
    bool  adsMode=false,sprinting=false;
    float shakeY=0;
    // Weapons: four inventory slots (melee, sidearm, two primaries)
    int   curWeapon=WPN_PISTOL;
    int   slot[SLOT_COUNT]={WPN_KNIFE, WPN_PISTOL, -1, -1};
    bool  owned[WPN_COUNT]={true,true};
    int   mag[WPN_COUNT]={0,12};
    int   reserve[WPN_COUNT]={0,60};
    Loadout load[WPN_COUNT];   // attachments + rarity per gun
    float reloadTimer=0;     // >0 while reloading
    float switchTimer=0;     // >0 while swapping weapons
    bool  triggerHeld=false;
    // Energy weapons, charge, bursts
    float heat=0.f;          // 0..1; reaching 1 overheats
    float overheatT=0.f;     // >0 while locked out
    float charge=0.f;        // 0..1 for charge weapons
    int   burstLeft=0;       // rounds still to fire in a burst
    float burstT=0.f;
    // Throwables and gadget
    int   throwCount[TH_COUNT]={2,1,0,0,0};
    int   throwSel=TH_FRAG;
    int   gadget=GD_RADAR;
    float gadgetCd=0.f;
};

// One tick of intent for one actor
struct InputCmd {
    float forward = 0.f;     // -1 back .. +1 forward
    float strafe  = 0.f;     // -1 left .. +1 right
    bool  sprint  = false;
    bool  jump    = false;
    bool  fire    = false;
    bool  ads     = false;
    float yaw     = 0.f;     // facing angle (radians)
    int   seq     = 0;       // sequence number (for network replay later)
};

// Other actors in the match: squad bots now, remote players later
enum ActorCtl { CTL_BOT = 0, CTL_REMOTE = 1 };
struct Actor : Player {
    int      id = 0;
    int      team = 0;               // 0 = the local player's team
    ActorCtl ctl = CTL_BOT;
    char     name[16] = "";
    bool     escort = false;         // a story NPC you protect: follows the player, never respawns
    bool     escortMet = false;      // the escort waits until you reach them
    bool     alive = true;
    float    respawnT = 0.f;
    float    hitFlash = 0.f, shootAnim = 0.f, walkPhase = 0.f;
    int      kills = 0;
    InputCmd cmd;
    // bot brain
    int      target = -1;            // enemy index being engaged
    float    thinkT = 0.f, fireT = 0.f, aimErr = 0.f;
};

// --- Story bosses (bosses.cpp) ------------------------------------------------------
enum BossKind { BK_NONE = 0, BK_WARDEN, BK_TWIN_A, BK_TWIN_B, BK_SEED_QUEEN, BK_EGG, BK_GUNSHIP,
                BK_GUARD_HEAVY, BK_GUARD_MEDIC, BK_MIRAGE, BK_ENGINEER, BK_PYLON, BK_JUGGERNAUT, BK_FOUNDER, BK_COUNT };

// --- Enemy -------------------------------------------------------------------
enum EnemyState { ES_IDLE, ES_ALERT, ES_DEAD };
enum EnemyType  { ET_RIFLEMAN=0, ET_SCOUT=1, ET_SHOTGUNNER=2, ET_SNIPER=3,
                  ET_GRENADIER=4, ET_SHIELD=5, ET_MEDIC=6, ET_HEAVY=7, ET_STALKER=8, ET_DRONE=9, ET_TURRET=10, ET_COUNT=11 };
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
    // Status effects
    float burnT=0.f, burnAcc=0.f; // burning: damage over time
    float shockT=0.f;             // shocked: slowed
    float stunT=0.f;              // stunned / flashed: can't move or shoot
    // Extended roster
    float hover=0.f;              // drones fly this high above the ground
    float grenT=2.f;              // grenadier: time to the next lob
    int   burstLeft=0; float burstT=0.f;   // heavy / drone / turret bursts
    float reviveT=0.f, healAcc=0.f;        // medic
    float cloak=1.f, revealT=0.f;          // stalker: 1 = fully cloaked
    // Bosses (bosses.cpp)
    int   bossKind=0;             // 0 = not a story boss
    int   phase=0;                // boss phase 0..2
    float atkT=1.f, atkPhaseT=0.f; int atk=-1;   // attack timer / telegraph / active attack
    float tx=0.f, ty=0.f;         // attack target point
    float armor=0.f;              // 0..1 damage reduction (front plates)
    bool  shielded=false;         // invulnerable until the shield is broken
    int   partner=-1;             // twin bosses: the other one
    bool  decoy=false;            // a boss copy that dies in one hit
};

// --- Projectiles, throwables, area effects, deployables ------------------------
enum ProjOwner { PO_PLAYER, PO_ACTOR, PO_TURRET, PO_ENEMY };
struct Projectile {
    float x, y, z, vx, vy, vz;
    ProjKind kind = PJ_ROCKET;
    int   throwKind = -1;        // >= 0: a thrown grenade of this ThrowKind
    ProjOwner owner = PO_PLAYER;
    int   weapon = -1;
    float life = 6.f, fuse = 0.f;
    float damage = 0.f, splashR = 0.f, splashDmg = 0.f;
    float gravity = 0.f;
    int   bounces = 0;
    bool  armed = false, alive = true;
    bool  incendiary = false;
    float landX = 0.f, landY = 0.f;   // enemy grenades: where they will land (warning ring)
};
struct SmokeCloud { float x, y, z, r, life, maxLife; };
struct FireZone   { float x, y, r, life; float tick = 0.f; };
struct Turret     { float x, y, z, yaw = 0.f, life = 25.f, fireT = 0.f; int target = -1; float flash = 0.f; };
struct MedStation { float x, y, z, life = 12.f; };
// Short-lived beams drawn in the world: arc lightning, rail trail, laser sight
struct Beam { float x0, y0, z0, x1, y1, z1, life, maxLife; uint32_t color; int kind; };

// --- Projectiles -------------------------------------------------------------
// Enemy rounds are slow glowing projectiles you can dodge.
struct Bullet { float x,y,z,dx,dy,dz=0; float px=0,py=0,pz=0; BulletOwner owner=ENEMY_SHOT; bool alive=true; int damage=0; bool whizzed=false; int shooter=-1; };
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
    std::vector<Actor>     actors;     // squad bots / other players (the local player is `player`)
    std::vector<Bullet>    bullets;
    std::vector<Tracer>    tracers;
    std::vector<Particle>  particles;
    std::vector<HealthKit> healthKits;
    std::vector<WeaponPickup> weaponPickups;
    std::vector<DamageNum> damageNums;
    std::vector<Projectile> projectiles;
    std::vector<SmokeCloud> smokes;
    std::vector<FireZone>   fires;
    std::vector<Turret>     turrets;
    std::vector<MedStation> medStations;
    std::vector<Beam>       beams;
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
