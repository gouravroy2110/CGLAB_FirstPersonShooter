// =============================================================================
//  assets.h  -  Procedurally baked raster assets (no image files are loaded)
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>
#include "raster.h"

// Biome per map: sky palette, sun, haze, fog, hazard pools and default weather
enum Biome { BIO_VOLCANIC = 0, BIO_DESERT, BIO_SWAMP, BIO_SNOW, BIO_RUINS, BIO_FACILITY, THEME_COUNT };
enum Weather { WX_NONE = 0, WX_RAIN, WX_SNOW, WX_SAND, WX_ASH, WX_BLIZZARD, WX_COUNT };
struct Theme {
    uint32_t zenith, horizon, haze, sunColor, cloudLit, cloudDark;
    float sunStrength;   // direct light strength
    float ambient;       // ambient light
    float fogDensity;    // exponential fog per world unit
    float sunElev;       // sun elevation (radians) — sets shadow length
    float cloudCover;    // 0..1
    uint32_t hazDeep, hazLight;   // hazard pool colours ('~' cells): lava, tar, bog, ice water, chemicals, coolant
    bool  hazGlow;       // self-lit hazard (lava, coolant)
    float hazDps;        // damage per second standing in it
    float hazSlow;       // 0..1 movement slow while wading
    int   weather;       // default Weather
    const char *name;
};
extern const Theme THEMES[THEME_COUNT];
static const float SUN_AZIMUTH = -0.6f;   // world-space angle of the sun

// Texture sizes
static const int SKY_W = 1024, SKY_H = 300;
static const int FLOOR_TS = 256;          // floor texture: 256 texels = 2 world units
static const int WALL_TS  = 128;          // wall texture: 128 texels = 1 world unit
static const int MIP_LEVELS = 4;

// A square texture with a box-filtered mip chain
struct MipTex {
    int size = 0;
    std::vector<uint32_t> lv[MIP_LEVELS];
    void build(const std::vector<uint32_t> &base, int sz);
};

void bakeSky(std::vector<uint32_t> &sky, int theme);
void bakeFloor(std::vector<uint32_t> &tex, int theme);
void bakeWall(std::vector<uint32_t> &tex, int type, int theme);
// Tree canopy billboard (broadleaf for the swamp, pine for snow)
static const int CANOPY_TS = 96;
void bakeCanopy(std::vector<uint32_t> &tex, int theme);

// --- Soldier sprites -----------------------------------------------------------
struct SoldierLook {
    uint32_t uniform, uniformDark, uniformAlt, vest, vestDark, helmet, skin, mask, lens, boots, gloves;
};
extern const SoldierLook ENEMY_LOOK, ELITE_LOOK, SCOUT_LOOK, SHOTGUN_LOOK, SNIPER_LOOK, SQUAD_LOOK;
extern const SoldierLook GRENADIER_LOOK, SHIELD_LOOK, MEDIC_LOOK, HEAVY_LOOK, STALKER_LOOK;
// Extra kit painted over a soldier
enum SoldierGear { GEAR_NONE = 0, GEAR_GRENADIER, GEAR_SHIELD, GEAR_MEDIC, GEAR_HEAVY, GEAR_STALKER };
SoldierLook lookForClass(int cls);

enum SoldierPose { POSE_AIM, POSE_WALK, POSE_SHOOT, POSE_HIT };
// Paint a soldier into a size×size ARGB buffer (+ optional per-texel hit mask)
void paintSoldier(uint32_t *px, uint8_t *mask, int size, const SoldierLook &look,
                  SoldierPose pose, float walk, bool bulky = false, int gear = GEAR_NONE);

// Enemy animation atlas
static const int ENEMY_TS = 64;
enum EnemyFrame { EF_AIM=0, EF_WALK0=1, EF_SHOOT=5, EF_HIT=6, EF_DEATH0=7, EF_COUNT=11 };
void bakeEnemyAtlas(std::vector<uint32_t> &atlas, std::vector<uint8_t> &mask, const SoldierLook &look, int gear = GEAR_NONE, bool bulky = false);
// Machines: 0 = hover drone, 1 = sentry gun (same frame layout as soldiers)
void bakeMachineAtlas(std::vector<uint32_t> &atlas, std::vector<uint8_t> &mask, int kind);
// Hit-mask values: 1 body, 2 head, 3 limb, 4 shield (blocks rounds)
static const uint8_t MASK_SHIELD = 4;

// Pickup sprites (48x48): 0=medkit 1=ammo 2=shotgun 3=rifle 4=intel
static const int PICKUP_TS = 48;
void bakePickups(std::vector<uint32_t> &tex);
