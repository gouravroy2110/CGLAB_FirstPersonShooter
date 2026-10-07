// =============================================================================
//  weapons.h  -  Data-driven arsenal
//
//  Every gun is one row of WEAPONS: stats, fire mode, reload style, recoil
//  pattern, damage falloff, projectile type, status effect, sound and which
//  3D model to draw. Adding a gun = one row here + one profile in gunbuild.cpp
//  + (optionally) a sound recipe. Throwables, gadgets, attachments and rarity
//  tiers are tables too.
// =============================================================================
#pragma once
#include <cstdint>

enum WeaponId {
    WPN_KNIFE = 0, WPN_PISTOL, WPN_SHOTGUN, WPN_RIFLE,          // the original four keep their ids
    WPN_REVOLVER, WPN_MACHINE_PISTOL,
    WPN_SMG, WPN_PDW,
    WPN_BURST_RIFLE, WPN_BATTLE_RIFLE, WPN_BULLPUP,
    WPN_AUTO_SHOTGUN, WPN_DOUBLE_BARREL,
    WPN_LMG,
    WPN_SNIPER, WPN_DMR,
    WPN_ROCKET, WPN_GRENADE_LAUNCHER,
    WPN_PLASMA, WPN_ARC, WPN_RAILGUN, WPN_FLAMETHROWER,
    WPN_MACHETE, WPN_BATON, WPN_AXE,
    WPN_COUNT
};

enum WeaponClass { WC_MELEE, WC_PISTOL, WC_SMG, WC_RIFLE, WC_SHOTGUN, WC_LMG, WC_SNIPER, WC_LAUNCHER, WC_ENERGY };
enum InvSlot     { SLOT_MELEE = 0, SLOT_SIDEARM = 1, SLOT_PRIMARY = 2, SLOT_PRIMARY2 = 3, SLOT_COUNT = 4 };
enum FireMode    { FM_SEMI, FM_AUTO, FM_BURST, FM_BOLT, FM_CHARGE, FM_BEAM, FM_MELEE };
enum ReloadMode  { RM_MAG, RM_SHELL, RM_NONE };              // RM_SHELL = one round at a time (cancellable)
enum RecoilPat   { RP_VERTICAL, RP_SCURVE, RP_RIGHT, RP_HEAVY, RP_RANDOM };
enum ProjKind    { PJ_HITSCAN, PJ_BULLET, PJ_ROCKET, PJ_GRENADE, PJ_PLASMA, PJ_ARC, PJ_RAIL, PJ_FLAME };
enum StatusKind  { ST_NONE, ST_BURN, ST_SHOCK, ST_STUN };
enum ViewArch    { VA_KNIFE, VA_PISTOL, VA_SHOTGUN, VA_RIFLE };   // which hands / grip pose to use

struct WeaponDef {
    const char *name, *shortName;
    WeaponClass cls;
    InvSlot  slot;
    int   damage;        // per pellet / strike, body hit
    float headMult;      // headshot multiplier
    float limbMult;      // arm / leg multiplier
    float fireDelay;     // seconds between shots (or bursts)
    int   pellets;
    float spread;        // hip-fire spread, in screen-fraction units
    int   magSize;       // 0 = melee / energy (heat instead of ammo)
    float reloadTime;    // full reload, or per shell for RM_SHELL
    float recoil;        // view-kick strength
    bool  automatic;
    float adsFov;        // FOV multiplier when aiming (smaller = more zoom)
    bool  scope;         // full-screen magnified optic
    int   maxReserve;
    // --- feel and mechanics ---
    FireMode   mode;
    int        burst;          // rounds per burst
    ReloadMode reload;
    RecoilPat  pattern;
    float falloffStart, falloffEnd, falloffMin;   // damage multiplier vs distance
    ProjKind   proj;
    float projSpeed;           // units / s for projectile weapons
    float splashR, splashDmg;  // explosions
    float penetration;         // 0 none .. 1 railgun (passes through walls)
    StatusKind status;
    float heatPerShot;         // energy weapons: 1.0 = overheated
    float chargeTime;          // FM_CHARGE: hold time for a full charge
    float moveMult;            // movement speed while holding it
    float reach;               // melee reach
    int   sound;               // SoundId of the report
    ViewArch arch;
    float gunLen;              // world length of the 3D model (third person / pickups)
    int   tier;                // 1..4: how late in the run it starts dropping
};

extern const WeaponDef WEAPONS[WPN_COUNT];

inline bool isMelee(int w)  { return w >= 0 && w < WPN_COUNT && WEAPONS[w].cls == WC_MELEE; }
inline bool usesAmmo(int w) { return w >= 0 && w < WPN_COUNT && WEAPONS[w].magSize > 0 && WEAPONS[w].reload != RM_NONE; }

// --- Throwables (G throws, hold G to cook, T cycles) ---------------------------------
enum ThrowKind { TH_FRAG, TH_SMOKE, TH_FLASH, TH_MOLOTOV, TH_MINE, TH_COUNT };
struct ThrowDef { const char *name, *shortName; float fuse; float radius; float damage; int maxCarry; uint32_t color; };
extern const ThrowDef THROWS[TH_COUNT];

// --- Gadgets (Z uses the equipped one; cooldowns) ------------------------------------
enum GadgetKind { GD_TURRET, GD_MEDSTATION, GD_RADAR, GD_GRAPPLE, GD_JETPACK, GD_COUNT };
struct GadgetDef { const char *name, *shortName, *desc; float duration; float cooldown; uint32_t color; };
extern const GadgetDef GADGETS[GD_COUNT];

// --- Attachments ---------------------------------------------------------------------
enum AttSlot { AT_OPTIC, AT_MUZZLE, AT_MAG, AT_UNDER, AT_STOCK, AT_AMMO, AT_COUNT };
// AT_OPTIC:  0 iron, 1 red dot, 2 holo, 3 2x, 4 4x, 5 8x
// AT_MUZZLE: 0 none, 1 suppressor, 2 compensator
// AT_MAG:    0 standard, 1 extended
// AT_UNDER:  0 none, 1 foregrip, 2 laser sight
// AT_STOCK:  0 standard, 1 tactical stock
// AT_AMMO:   0 FMJ, 1 armour-piercing, 2 incendiary, 3 hollow-point
static const int ATT_OPTIONS[AT_COUNT] = { 6, 3, 2, 3, 2, 4 };
extern const char *const ATT_SLOT_NAMES[AT_COUNT];
extern const char *const ATT_NAMES[AT_COUNT][6];
bool attachmentFits(int weapon, int slot, int option);

struct Loadout {
    uint8_t att[AT_COUNT] = {0, 0, 0, 0, 0, 0};
    uint8_t rarity = 0;          // 0 common .. 4 legendary
};

// --- Rarity: colour + stat variance --------------------------------------------------
static const int RARITY_COUNT = 5;
extern const char *const RARITY_NAMES[RARITY_COUNT];
extern const uint32_t RARITY_COLORS[RARITY_COUNT];
static const float RARITY_DMG[RARITY_COUNT]    = { 1.00f, 1.05f, 1.10f, 1.16f, 1.24f };
static const float RARITY_RECOIL[RARITY_COUNT] = { 1.00f, 0.95f, 0.90f, 0.84f, 0.76f };
static const float RARITY_MAG[RARITY_COUNT]    = { 1.00f, 1.00f, 1.10f, 1.20f, 1.30f };
