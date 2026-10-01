// =============================================================================
//  assets.h  -  Procedurally baked raster assets (no image files are loaded)
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>
#include "raster.h"

// Visual theme per map: sky palette, sun, haze, fog
struct Theme {
    uint32_t zenith, horizon, haze, sunColor, cloudLit, cloudDark;
    float sunStrength;   // direct light strength
    float ambient;       // ambient light
    float fogDensity;    // exponential fog per world unit
    float sunElev;       // sun elevation (radians) — sets shadow length
    float cloudCover;    // 0..1
};
extern const Theme THEMES[3];
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

// --- Soldier sprites -----------------------------------------------------------
struct SoldierLook {
    uint32_t uniform, uniformDark, uniformAlt, vest, vestDark, helmet, skin, mask, lens, boots, gloves;
};
extern const SoldierLook ENEMY_LOOK, ELITE_LOOK;
SoldierLook lookForClass(int cls);

enum SoldierPose { POSE_AIM, POSE_WALK, POSE_SHOOT, POSE_HIT };
// Paint a soldier into a size×size ARGB buffer (+ optional per-texel hit mask)
void paintSoldier(uint32_t *px, uint8_t *mask, int size, const SoldierLook &look,
                  SoldierPose pose, float walk, bool bulky = false);

// Enemy animation atlas
static const int ENEMY_TS = 64;
enum EnemyFrame { EF_AIM=0, EF_WALK0=1, EF_SHOOT=5, EF_HIT=6, EF_DEATH0=7, EF_COUNT=11 };
void bakeEnemyAtlas(std::vector<uint32_t> &atlas, std::vector<uint8_t> &mask, const SoldierLook &look);

// Pickup sprites (48x48): 0=medkit 1=ammo 2=shotgun 3=rifle 4=intel
static const int PICKUP_TS = 48;
void bakePickups(std::vector<uint32_t> &tex);
