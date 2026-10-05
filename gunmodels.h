// =============================================================================
//  gunmodels.h  -  Weapon geometry as 2D side-profile polygons
//
//  Each weapon is a list of convex polygons in "gun space": u runs along the
//  barrel (0 = butt, length = muzzle) and h is height above the bore axis.
//  The same data is rasterized three ways:
//    • first-person view model  (perspective-projected, Gouraud shaded)
//    • HUD weapon icon          (flat, silhouette)
//    • floor pickup sprites     (flat, baked into a texture)
// =============================================================================
#pragma once
#include <cstdint>
#include "raster.h"

enum GunPartFlag : uint8_t {
    GP_ACTION = 1,   // moves with slide / pump cycling
    GP_MAG    = 2,   // magazine: drops out during reload
    GP_GLASS  = 4,   // optic lens: glossy, tinted
    GP_NORIM  = 8,   // recessed detail: no specular rim
};

struct GunPart { int n; float pts[12]; uint32_t col; uint8_t flags; };

struct GunModel {
    const GunPart *parts; int count;
    float length;
    float muzzleU, muzzleH, ejectU, ejectH;
    float gripU, gripH, foreU, foreH;
    float hMin, hMax;
    const GunPart *hands; int handCount;     // glove polygons, drawn over the gun
    float rWristU, rWristH, lWristU, lWristH; // where the sleeves attach
};

static const uint32_t GLOVE = 0xFF2A2A2F;

// --- M4 carbine with 4x scope -------------------------------------------------
static const GunPart RIFLE_PARTS[] = {
    {4,{0,3, 17,5, 17,-9, 0,-15},            0xFF2B2C2F, 0},          // stock
    {4,{3,-3, 14,-2, 14,-6, 3,-9},           0xFF1F2023, GP_NORIM},   // stock cut-out
    {4,{17,3, 28,3, 28,-2, 17,-2},           0xFF242528, 0},          // buffer tube
    {4,{41,-6, 48,-6, 51,-24, 44,-25},       0xFF34363A, GP_MAG},     // magazine
    {4,{31,-5, 38,-5, 35,-20, 28,-19},       0xFF2A2B2E, 0},          // pistol grip
    {4,{27,4, 48,4, 48,-6, 27,-6},           0xFF3C3E43, 0},          // lower receiver
    {4,{36,-6, 42,-6, 42,-10, 36,-10},       0xFF1A1B1E, GP_NORIM},   // trigger guard
    {4,{26,10, 49,10, 49,3, 26,3},           0xFF474A4F, 0},          // upper receiver
    {4,{35,8, 44,8, 44,5, 35,5},             0xFF101012, GP_NORIM},   // ejection port
    {4,{49,9, 78,9, 78,-3, 49,-3},           0xFF393B3F, 0},          // handguard
    {4,{49,11, 78,11, 78,9, 49,9},           0xFF2A2B2E, 0},          // top rail
    {4,{52,2, 76,2, 76,0, 52,0},             0xFF2B2C30, GP_NORIM},   // handguard vents
    {4,{78,3, 93,3, 93,0, 78,0},             0xFF2B2C2F, 0},          // barrel
    {4,{73,11, 76,11, 76,16, 73,15},         0xFF2A2B2E, 0},          // front sight post
    {4,{93,4, 100,4, 100,-1, 93,-1},         0xFF1D1E20, 0},          // muzzle brake
    {4,{33,11, 37,11, 37,14, 33,14},         0xFF1A1B1D, 0},          // scope mount
    {4,{52,11, 56,11, 56,14, 52,14},         0xFF1A1B1D, 0},          // scope mount
    {4,{26,21, 33,20, 33,13, 26,12},         0xFF1E1F22, 0},          // eyepiece
    {4,{33,19, 60,19, 60,14, 33,14},         0xFF25262A, 0},          // scope tube
    {4,{43,22, 48,22, 48,19, 43,19},         0xFF2A2B2E, 0},          // elevation turret
    {4,{60,19, 66,22, 66,11, 60,14},         0xFF202124, 0},          // objective bell
    {4,{65,21, 67,21, 67,12, 65,12},         0xFF6FB0DA, GP_GLASS},   // objective lens
};
static const GunPart RIFLE_HANDS[] = {
    {6,{55,-2, 70,-2, 72,-8, 68,-15, 58,-15, 54,-8}, GLOVE, 0},         // support palm
    {4,{58,5, 61,5, 61,-3, 58,-3},                   GLOVE, 0},         // fingers over the handguard
    {4,{62,5.5f, 65,5.5f, 65,-3, 62,-3},             GLOVE, 0},
    {4,{66,5, 69,5, 69,-3, 66,-3},                   GLOVE, 0},
    {6,{26,-3, 39,-3, 40,-10, 37,-21, 29,-22, 25,-12}, GLOVE, 0},       // trigger-hand fist
    {4,{32,-1, 43,0, 43,-4, 32,-5},                  GLOVE, 0},         // thumb
    {4,{38,-7, 45,-7, 45,-9.5f, 38,-9.5f},           GLOVE, 0},         // index finger
};
static const GunModel RIFLE_MODEL = { RIFLE_PARTS, int(sizeof(RIFLE_PARTS)/sizeof(GunPart)),
    100, 100,1.5f, 40,6.5f, 32,-12, 64,-3, -25, 22,
    RIFLE_HANDS, int(sizeof(RIFLE_HANDS)/sizeof(GunPart)), 29,-20, 62,-14 };

// --- M870 pump shotgun ---------------------------------------------------------
static const GunPart SHOTGUN_PARTS[] = {
    {4,{-2,2, 0,2, 0,-16, -2,-16},           0xFF151515, 0},          // butt pad
    {5,{0,1, 20,5, 26,4, 26,-6, 0,-15},      0xFF6B4226, 0},          // wooden stock
    {4,{26,6, 46,6, 46,-6, 26,-6},           0xFF2E2F33, 0},          // receiver
    {4,{32,4, 42,4, 42,1, 32,1},             0xFF111113, GP_NORIM},   // ejection port
    {4,{31,-6, 40,-6, 39,-10, 31,-10},       0xFF1C1D20, GP_NORIM},   // trigger guard
    {4,{46,5, 100,5, 100,1, 46,1},           0xFF2A2B2E, 0},          // barrel
    {4,{46,0, 90,0, 90,-4, 46,-4},           0xFF232427, 0},          // magazine tube
    {4,{90,0.5f, 93,0.5f, 93,-4.5f, 90,-4.5f},0xFF1A1A1C, 0},         // tube cap
    {4,{58,1, 80,1, 80,-8, 58,-8},           0xFF7A4B2A, GP_ACTION},  // pump fore-end
    {4,{60,-2, 78,-2, 78,-3, 60,-3},         0xFF55331C, GP_ACTION|GP_NORIM}, // grooves
    {4,{60,-5, 78,-5, 78,-6, 60,-6},         0xFF55331C, GP_ACTION|GP_NORIM},
    {4,{97,6.5f, 99,6.5f, 99,5, 97,5},       0xFFD8D0B0, 0},          // bead sight
};
static const GunPart SHOTGUN_HANDS[] = {
    {6,{59,-4, 76,-4, 78,-9, 74,-15, 63,-15, 58,-9}, GLOVE, GP_ACTION}, // hand on the pump
    {4,{62,4, 65,4, 65,-5, 62,-5},                   GLOVE, GP_ACTION},
    {4,{66,4.5f, 69,4.5f, 69,-5, 66,-5},             GLOVE, GP_ACTION},
    {4,{70,4, 73,4, 73,-5, 70,-5},                   GLOVE, GP_ACTION},
    {6,{18,1, 31,0, 32,-6, 29,-14, 21,-15, 17,-6},   GLOVE, 0},         // stock wrist grip
    {4,{31,-6, 38,-6, 38,-8.5f, 31,-8.5f},           GLOVE, 0},
};
static const GunModel SHOTGUN_MODEL = { SHOTGUN_PARTS, int(sizeof(SHOTGUN_PARTS)/sizeof(GunPart)),
    100, 100,3, 37,3, 25,-8, 69,-5, -16, 7,
    SHOTGUN_HANDS, int(sizeof(SHOTGUN_HANDS)/sizeof(GunPart)), 22,-14, 68,-15 };

// --- M9 pistol with mini reflex sight ------------------------------------------
static const GunPart PISTOL_PARTS[] = {
    {4,{-3,-28, 11,-30, 11,-33, -3,-31},     0xFF151618, GP_MAG},     // magazine base
    {4,{3,-3, 15,-3, 11,-30, -3,-28},        0xFF1F2024, 0},          // grip
    {4,{4,1, 44,1, 44,-4, 4,-4},             0xFF25262B, 0},          // frame
    {4,{15,-4, 26,-4, 24,-11, 15,-11},       0xFF1A1B1E, GP_NORIM},   // trigger guard
    {4,{48,6, 51,6, 51,2, 48,2},             0xFF3A3B40, 0},          // barrel tip
    {4,{0,9, 48,9, 48,1, 0,1},               0xFF2D2E33, GP_ACTION},  // slide
    {4,{3,8, 10,8, 10,2, 3,2},               0xFF222327, GP_ACTION|GP_NORIM}, // serrations
    {4,{26,8, 36,8, 36,5, 26,5},             0xFF0E0E10, GP_ACTION|GP_NORIM}, // ejection port
    {4,{1,11, 6,11, 6,9, 1,9},               0xFF18191C, GP_ACTION},  // rear sight
    {4,{43,11, 46,11, 46,9, 43,9},           0xFF18191C, GP_ACTION},  // front sight
    {4,{14,17, 30,17, 30,9, 14,9},           0xFF1A1B1E, GP_ACTION},  // reflex sight housing
    {4,{27,16, 29,16, 29,10, 27,10},         0xFF5FA0C8, GP_ACTION|GP_GLASS}, // reflex lens
};
static const GunPart PISTOL_HANDS[] = {
    {6,{-7,-10, 8,-12, 12,-20, 9,-31, -5,-32, -9,-20}, 0xFF242428, 0},  // support hand cupping
    {6,{-4,-3, 13,-3, 15,-10, 11,-27, -2,-28, -5,-12}, GLOVE, 0},       // firing hand
    {4,{3,-0.5f, 16,0.5f, 16,-3, 3,-4},              GLOVE, 0},         // thumb along the frame
    {4,{14,-5, 22,-5, 22,-7.5f, 14,-7.5f},           GLOVE, 0},         // trigger finger
};
static const GunModel PISTOL_MODEL = { PISTOL_PARTS, int(sizeof(PISTOL_PARTS)/sizeof(GunPart)),
    51, 51,4, 31,7, 5,-16, 3,-20, -33, 17,
    PISTOL_HANDS, int(sizeof(PISTOL_HANDS)/sizeof(GunPart)), 3,-27, -1,-31 };

// --- Combat knife -------------------------------------------------------------
static const GunPart KNIFE_PARTS[] = {
    // Steel pommel
    {4, {-6, 2,  0, 2,  0, -5, -6, -5},          0xFF282A2E, 0},
    // Tactical ribbed grip
    {4, {0, 3,  24, 3,  22, -6,  0, -6},         0xFF1C1D1F, 0},
    // Grip ribs
    {4, {4, 2,  8, 2,  8, -5,  4, -5},           0xFF141517, GP_NORIM},
    {4, {10, 2, 14, 2, 14, -5, 10, -5},          0xFF141517, GP_NORIM},
    {4, {16, 2, 20, 2, 20, -5, 16, -5},          0xFF141517, GP_NORIM},
    // Steel crossguard
    {4, {23, 9, 27, 9, 27, -10, 23, -10},        0xFF353840, 0},
    // Blade ricasso
    {4, {27, 6, 34, 6, 34, -4, 27, -4},          0xFF686C75, 0},
    // Main blade spine & flat
    {4, {34, 6, 52, 6, 50, -4, 34, -4},          0xFF8A909C, 0},
    // Blood groove / fuller
    {4, {36, 3, 48, 3, 48, 0, 36, 0},            0xFF4A4E57, GP_NORIM},
    // Swedge / clip point
    {3, {52, 6, 65, 2, 52, 2},                   0xFF727885, GP_NORIM},
    // Blade tip & belly
    {4, {52, 2, 65, 2, 60, -4, 50, -4},          0xFF9EA5B3, 0},
    // Razor edge bevel (high-polish silver edge)
    {4, {27, -4, 50, -4, 60, -4, 27, -6},        0xFFDDE4ED, 0},
    {3, {50, -4, 65, 2, 60, -4},                 0xFFEEF3FA, 0},
};

static const GunPart KNIFE_HANDS[] = {
    // Gloved hand wrapping around the knife handle
    {6, {2, 4, 20, 4, 22, -2, 19, -9, 4, -9, 1, -2}, GLOVE, 0},
    {4, {5, 5.5f, 8, 5.5f, 8, 2, 5, 2},             GLOVE, 0},
    {4, {10, 5.5f, 13, 5.5f, 13, 2, 10, 2},         GLOVE, 0},
    {4, {15, 5.5f, 18, 5.5f, 18, 2, 15, 2},         GLOVE, 0},
    // Thumb locked along the guard
    {4, {16, 6.5f, 23, 6.5f, 23, 2, 16, 2},         GLOVE, 0},
};

static const GunModel KNIFE_MODEL = {
    KNIFE_PARTS, int(sizeof(KNIFE_PARTS)/sizeof(GunPart)),
    65, 65, 2, 0, 0,
    10, -5, 0, 0,
    -12, 10,
    KNIFE_HANDS, int(sizeof(KNIFE_HANDS)/sizeof(GunPart)),
    8, -10, 0, 0
};

static const GunModel *const GUN_MODELS[4] = { &KNIFE_MODEL, &PISTOL_MODEL, &SHOTGUN_MODEL, &RIFLE_MODEL };

// Flat (orthographic) rasterization of a gun profile — used for HUD icons and
// pickup textures. (ox,oy) = screen position of u=0,h=0; s = pixels per unit.
inline void drawGunFlat(rs::Canvas &cv, const GunModel &m, float ox, float oy, float s,
                        bool silhouette = false, uint32_t tint = 0xFFFFFFFF)
{
    for (int i = 0; i < m.count; i++) {
        const GunPart &gp = m.parts[i];
        float xy[12]; uint32_t cols[6];
        for (int k = 0; k < gp.n; k++) {
            float u = gp.pts[2*k], hh = gp.pts[2*k+1];
            xy[2*k] = ox + u * s; xy[2*k+1] = oy - hh * s;
            if (silhouette) cols[k] = tint;
            else {
                float lt = 0.75f + 0.6f * (hh - m.hMin) / (m.hMax - m.hMin);
                cols[k] = rs::scaleCol(gp.col, lt * 1.6f);
            }
        }
        cv.poly(xy, cols, gp.n);
    }
}
