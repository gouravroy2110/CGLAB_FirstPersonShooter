// =============================================================================
//  weapons.cpp  -  The arsenal tables
// =============================================================================
#include "weapons.h"
#include "audio.h"

#define ROW(...) WeaponDef{__VA_ARGS__}
// name, short, class, slot,
//   dmg, head, limb, delay, pellets, spread, mag, reload, recoil, auto, adsFov, scope, reserve,
//   mode, burst, reload mode, recoil pattern, falloff start/end/min,
//   projectile, speed, splash r, splash dmg, penetration, status, heat, charge,
//   move mult, reach, sound, view arch, 3D length, tier
const WeaponDef WEAPONS[WPN_COUNT] = {
    ROW("COMBAT KNIFE", "KNIFE", WC_MELEE, SLOT_MELEE,
        45, 2.5f, 0.80f, 0.35f, 1, 0.000f, 0, 0.00f, 0.1f, true, 1.00f, false, 0,
        FM_MELEE, 1, RM_NONE, RP_RANDOM, 99, 99, 1.f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 1.05f, 2.2f, SND_KNIFE_SWING, VA_KNIFE, 0.13f, 0),
    ROW("M9 SIDEARM", "PISTOL", WC_PISTOL, SLOT_SIDEARM,
        22, 2.5f, 0.75f, 0.24f, 1, 0.012f, 12, 1.25f, 1.0f, false, 0.70f, false, 96,
        FM_SEMI, 1, RM_MAG, RP_VERTICAL, 10, 26, 0.6f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 1.0f, 0, SND_PISTOL, VA_PISTOL, 0.14f, 0),
    ROW("M870 SHOTGUN", "SHOTGUN", WC_SHOTGUN, SLOT_PRIMARY,
        15, 2.0f, 0.75f, 0.58f, 8, 0.045f, 6, 0.42f, 2.0f, false, 0.80f, false, 48,
        FM_SEMI, 1, RM_SHELL, RP_HEAVY, 2, 14, 0.4f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 0.95f, 0, SND_SHOTGUN, VA_SHOTGUN, 0.44f, 1),
    ROW("M4 CARBINE", "RIFLE", WC_RIFLE, SLOT_PRIMARY,
        18, 2.5f, 0.70f, 0.10f, 1, 0.016f, 30, 1.90f, 0.8f, true, 0.65f, false, 180,
        FM_AUTO, 1, RM_MAG, RP_RIGHT, 18, 40, 0.75f,
        PJ_HITSCAN, 0, 0, 0, 0.25f, ST_NONE, 0, 0, 0.95f, 0, SND_RIFLE, VA_RIFLE, 0.44f, 1),
    ROW(".44 MAGNUM", "REVOLVER", WC_PISTOL, SLOT_SIDEARM,
        48, 2.6f, 0.75f, 0.42f, 1, 0.008f, 6, 2.20f, 2.4f, false, 0.65f, false, 48,
        FM_SEMI, 1, RM_MAG, RP_HEAVY, 14, 32, 0.65f,
        PJ_HITSCAN, 0, 0, 0, 0.35f, ST_NONE, 0, 0, 1.0f, 0, SND_REVOLVER, VA_PISTOL, 0.16f, 1),
    ROW("MP-9 AUTO", "M.PISTOL", WC_PISTOL, SLOT_SIDEARM,
        12, 2.5f, 0.75f, 0.06f, 1, 0.022f, 20, 1.40f, 0.55f, true, 0.72f, false, 160,
        FM_AUTO, 1, RM_MAG, RP_RANDOM, 8, 20, 0.5f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 1.0f, 0, SND_SMG, VA_PISTOL, 0.16f, 1),
    ROW("VECTOR SMG", "SMG", WC_SMG, SLOT_PRIMARY,
        15, 2.4f, 0.75f, 0.07f, 1, 0.018f, 30, 1.60f, 0.45f, true, 0.70f, false, 210,
        FM_AUTO, 1, RM_MAG, RP_SCURVE, 10, 24, 0.55f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 1.05f, 0, SND_SMG, VA_RIFLE, 0.32f, 1),
    ROW("P90 PDW", "PDW", WC_SMG, SLOT_PRIMARY,
        13, 2.4f, 0.75f, 0.065f, 1, 0.020f, 50, 2.30f, 0.40f, true, 0.70f, false, 250,
        FM_AUTO, 1, RM_MAG, RP_SCURVE, 10, 24, 0.55f,
        PJ_HITSCAN, 0, 0, 0, 0.15f, ST_NONE, 0, 0, 1.05f, 0, SND_SMG, VA_RIFLE, 0.30f, 2),
    ROW("M16 BURST", "BURST", WC_RIFLE, SLOT_PRIMARY,
        21, 2.5f, 0.70f, 0.34f, 1, 0.012f, 30, 1.90f, 0.75f, false, 0.62f, false, 180,
        FM_BURST, 3, RM_MAG, RP_VERTICAL, 20, 42, 0.75f,
        PJ_HITSCAN, 0, 0, 0, 0.25f, ST_NONE, 0, 0, 0.95f, 0, SND_RIFLE, VA_RIFLE, 0.48f, 2),
    ROW("SCAR-H BATTLE RIFLE", "BATTLE", WC_RIFLE, SLOT_PRIMARY,
        34, 2.4f, 0.70f, 0.20f, 1, 0.012f, 20, 2.10f, 1.6f, false, 0.60f, false, 120,
        FM_SEMI, 1, RM_MAG, RP_HEAVY, 22, 48, 0.8f,
        PJ_HITSCAN, 0, 0, 0, 0.5f, ST_NONE, 0, 0, 0.92f, 0, SND_DMR, VA_RIFLE, 0.48f, 2),
    ROW("AUG BULLPUP", "BULLPUP", WC_RIFLE, SLOT_PRIMARY,
        19, 2.5f, 0.70f, 0.085f, 1, 0.013f, 30, 2.40f, 0.65f, true, 0.55f, false, 180,
        FM_AUTO, 1, RM_MAG, RP_VERTICAL, 20, 42, 0.75f,
        PJ_HITSCAN, 0, 0, 0, 0.25f, ST_NONE, 0, 0, 0.97f, 0, SND_RIFLE, VA_RIFLE, 0.40f, 3),
    ROW("AA-12 AUTO SHOTGUN", "AUTO-SG", WC_SHOTGUN, SLOT_PRIMARY,
        10, 2.0f, 0.75f, 0.25f, 7, 0.050f, 8, 2.60f, 1.6f, true, 0.80f, false, 64,
        FM_AUTO, 1, RM_MAG, RP_HEAVY, 2, 12, 0.35f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 0.92f, 0, SND_SHOTGUN, VA_SHOTGUN, 0.44f, 3),
    ROW("SAWN-OFF DOUBLE BARREL", "DOUBLE", WC_SHOTGUN, SLOT_PRIMARY,
        16, 2.0f, 0.75f, 0.25f, 10, 0.070f, 2, 1.0f, 3.0f, false, 0.85f, false, 40,
        FM_SEMI, 1, RM_SHELL, RP_HEAVY, 1.5f, 8, 0.25f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 1.0f, 0, SND_DOUBLE_BARREL, VA_SHOTGUN, 0.30f, 1),
    ROW("M249 SAW", "LMG", WC_LMG, SLOT_PRIMARY,
        20, 2.3f, 0.70f, 0.075f, 1, 0.030f, 100, 4.50f, 1.1f, true, 0.70f, false, 300,
        FM_AUTO, 1, RM_MAG, RP_RANDOM, 20, 44, 0.75f,
        PJ_HITSCAN, 0, 0, 0, 0.4f, ST_NONE, 0, 0, 0.82f, 0, SND_LMG, VA_RIFLE, 0.52f, 3),
    ROW("AWM BOLT SNIPER", "SNIPER", WC_SNIPER, SLOT_PRIMARY,
        95, 2.6f, 0.75f, 1.30f, 1, 0.060f, 5, 3.00f, 3.5f, false, 0.125f, true, 30,
        FM_BOLT, 1, RM_MAG, RP_HEAVY, 99, 99, 1.f,
        PJ_BULLET, 140.f, 0, 0, 0.8f, ST_NONE, 0, 0, 0.88f, 0, SND_SNIPER, VA_RIFLE, 0.56f, 3),
    ROW("MK14 DMR", "DMR", WC_SNIPER, SLOT_PRIMARY,
        42, 2.4f, 0.75f, 0.22f, 1, 0.020f, 15, 2.40f, 1.6f, false, 0.25f, true, 90,
        FM_SEMI, 1, RM_MAG, RP_VERTICAL, 40, 70, 0.85f,
        PJ_HITSCAN, 0, 0, 0, 0.5f, ST_NONE, 0, 0, 0.92f, 0, SND_DMR, VA_RIFLE, 0.52f, 2),
    ROW("RPG-7 LAUNCHER", "ROCKET", WC_LAUNCHER, SLOT_PRIMARY,
        40, 1.0f, 1.0f, 0.80f, 1, 0.010f, 1, 2.80f, 3.0f, false, 0.75f, false, 6,
        FM_SEMI, 1, RM_MAG, RP_HEAVY, 99, 99, 1.f,
        PJ_ROCKET, 18.f, 2.6f, 230, 0, ST_NONE, 0, 0, 0.82f, 0, SND_ROCKET_FIRE, VA_RIFLE, 0.6f, 4),
    ROW("M32 GRENADE LAUNCHER", "GL", WC_LAUNCHER, SLOT_PRIMARY,
        30, 1.0f, 1.0f, 0.55f, 1, 0.012f, 6, 0.60f, 2.0f, false, 0.80f, false, 18,
        FM_SEMI, 1, RM_SHELL, RP_HEAVY, 99, 99, 1.f,
        PJ_GRENADE, 13.f, 2.2f, 150, 0, ST_NONE, 0, 0, 0.88f, 0, SND_GL_THUMP, VA_SHOTGUN, 0.44f, 3),
    ROW("PLASMA RIFLE", "PLASMA", WC_ENERGY, SLOT_PRIMARY,
        34, 1.8f, 0.80f, 0.16f, 1, 0.010f, 0, 0.0f, 0.6f, true, 0.70f, false, 0,
        FM_CHARGE, 1, RM_NONE, RP_VERTICAL, 99, 99, 1.f,
        PJ_PLASMA, 30.f, 0.7f, 20, 0, ST_BURN, 0.09f, 1.0f, 0.95f, 0, SND_PLASMA, VA_RIFLE, 0.46f, 4),
    ROW("ARC GUN", "ARC", WC_ENERGY, SLOT_PRIMARY,
        18, 1.0f, 1.0f, 0.12f, 1, 0.000f, 40, 2.50f, 0.3f, true, 0.80f, false, 200,
        FM_BEAM, 1, RM_MAG, RP_RANDOM, 99, 99, 1.f,
        PJ_ARC, 0, 0, 0, 0, ST_SHOCK, 0, 0, 0.95f, 12.f, SND_ARC, VA_RIFLE, 0.42f, 4),
    ROW("RAILGUN", "RAIL", WC_ENERGY, SLOT_PRIMARY,
        160, 2.0f, 0.8f, 1.10f, 1, 0.030f, 4, 3.20f, 4.0f, false, 0.33f, true, 16,
        FM_CHARGE, 1, RM_MAG, RP_HEAVY, 99, 99, 1.f,
        PJ_RAIL, 0, 0, 0, 1.0f, ST_NONE, 0, 0.75f, 0.85f, 0, SND_RAIL_FIRE, VA_RIFLE, 0.58f, 4),
    ROW("FLAMETHROWER", "FLAMER", WC_ENERGY, SLOT_PRIMARY,
        7, 1.0f, 1.0f, 0.05f, 1, 0.000f, 100, 3.00f, 0.15f, true, 0.90f, false, 300,
        FM_AUTO, 1, RM_MAG, RP_RANDOM, 99, 99, 1.f,
        PJ_FLAME, 9.f, 0, 0, 0, ST_BURN, 0, 0, 0.9f, 6.5f, SND_FLAME, VA_RIFLE, 0.48f, 3),
    ROW("MACHETE", "MACHETE", WC_MELEE, SLOT_MELEE,
        65, 2.2f, 0.85f, 0.55f, 1, 0.000f, 0, 0.00f, 0.1f, true, 1.00f, false, 0,
        FM_MELEE, 1, RM_NONE, RP_RANDOM, 99, 99, 1.f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 1.03f, 2.5f, SND_KNIFE_SWING, VA_KNIFE, 0.2f, 1),
    ROW("STUN BATON", "BATON", WC_MELEE, SLOT_MELEE,
        35, 2.0f, 0.9f, 0.40f, 1, 0.000f, 0, 0.00f, 0.1f, true, 1.00f, false, 0,
        FM_MELEE, 1, RM_NONE, RP_RANDOM, 99, 99, 1.f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_STUN, 0, 0, 1.05f, 2.2f, SND_BATON_HIT, VA_KNIFE, 0.18f, 1),
    ROW("FIRE AXE", "AXE", WC_MELEE, SLOT_MELEE,
        95, 2.0f, 0.9f, 0.90f, 1, 0.000f, 0, 0.00f, 0.1f, true, 1.00f, false, 0,
        FM_MELEE, 1, RM_NONE, RP_RANDOM, 99, 99, 1.f,
        PJ_HITSCAN, 0, 0, 0, 0, ST_NONE, 0, 0, 0.97f, 2.4f, SND_MELEE_HEAVY, VA_KNIFE, 0.24f, 2),
};

const ThrowDef THROWS[TH_COUNT] = {
    {"FRAG GRENADE",  "FRAG",    2.6f, 3.0f, 170.f, 4, 0xFF5C6A3C},
    {"SMOKE GRENADE", "SMOKE",   1.6f, 3.2f,   0.f, 3, 0xFFB8BCC0},
    {"FLASHBANG",     "FLASH",   1.4f, 7.0f,   0.f, 3, 0xFFE8E8E0},
    {"MOLOTOV",       "MOLOTOV", 0.0f, 1.9f,  20.f, 3, 0xFFFF8A2A},
    {"CLAYMORE MINE", "MINE",    1.2f, 2.6f, 190.f, 2, 0xFF4A5A3A},
};

const GadgetDef GADGETS[GD_COUNT] = {
    {"SENTRY TURRET",   "TURRET",  "Auto-fires at hostiles for 25 s",        25.f, 45.f, 0xFF60C0FF},
    {"MEDKIT STATION",  "MED",     "Heals 8 HP/s nearby for 12 s",           12.f, 50.f, 0xFF60FF90},
    {"RADAR PING",      "RADAR",   "Reveals every hostile for 8 s",           8.f, 30.f, 0xFFFFD050},
    {"GRAPPLING HOOK",  "GRAPPLE", "Pull yourself up to a ledge (8 units)",   0.f,  6.f, 0xFFC0A080},
    {"JETPACK BURST",   "JETPACK", "A strong vertical boost",                 0.f,  5.f, 0xFFFF9050},
};

const char *const ATT_SLOT_NAMES[AT_COUNT] = { "OPTIC", "MUZZLE", "MAGAZINE", "UNDERBARREL", "STOCK", "AMMO" };
const char *const ATT_NAMES[AT_COUNT][6] = {
    { "IRON SIGHTS", "RED DOT", "HOLO SIGHT", "2X SCOPE", "4X SCOPE", "8X SCOPE" },
    { "NONE", "SUPPRESSOR", "COMPENSATOR", "", "", "" },
    { "STANDARD MAG", "EXTENDED MAG", "", "", "", "" },
    { "NONE", "FOREGRIP", "LASER SIGHT", "", "", "" },
    { "STANDARD STOCK", "TACTICAL STOCK", "", "", "", "" },
    { "FMJ", "ARMOUR-PIERCING", "INCENDIARY", "HOLLOW-POINT", "", "" },
};

const char *const RARITY_NAMES[RARITY_COUNT] = { "COMMON", "UNCOMMON", "RARE", "EPIC", "LEGENDARY" };
const uint32_t RARITY_COLORS[RARITY_COUNT] = { 0xFFD8DCE0, 0xFF5CE07A, 0xFF4AA8FF, 0xFFB26CFF, 0xFFFFC23A };

bool attachmentFits(int w, int slot, int opt)
{
    if (w < 0 || w >= WPN_COUNT || opt < 0 || opt >= ATT_OPTIONS[slot]) return false;
    if (opt == 0) return true;                             // the default is always "fitted"
    const WeaponDef &d = WEAPONS[w];
    bool bullets = d.proj == PJ_HITSCAN || d.proj == PJ_BULLET;
    switch (slot) {
    case AT_OPTIC:
        if (d.cls == WC_MELEE || d.cls == WC_LAUNCHER || d.proj == PJ_FLAME || d.proj == PJ_ARC) return false;
        if (d.cls == WC_PISTOL || d.cls == WC_SHOTGUN) return opt <= 2;
        if (d.cls == WC_SMG) return opt <= 3;
        if (d.cls == WC_SNIPER || d.proj == PJ_RAIL) return opt >= 3;
        return opt <= 4;
    case AT_MUZZLE:  return bullets && d.cls != WC_MELEE;
    case AT_MAG:     return usesAmmo(w) && d.magSize > 2 && d.cls != WC_LAUNCHER;
    case AT_UNDER:
        if (d.cls == WC_PISTOL) return opt == 2;
        return d.cls == WC_SMG || d.cls == WC_RIFLE || d.cls == WC_LMG || d.cls == WC_SHOTGUN || d.cls == WC_SNIPER;
    case AT_STOCK:   return d.cls == WC_SMG || d.cls == WC_RIFLE || d.cls == WC_LMG || d.cls == WC_SNIPER || d.cls == WC_SHOTGUN;
    case AT_AMMO:    return bullets && d.cls != WC_MELEE && (opt != 1 || d.cls != WC_SHOTGUN);
    }
    return false;
}
