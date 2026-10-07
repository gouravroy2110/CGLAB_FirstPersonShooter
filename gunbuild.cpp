// =============================================================================
//  gunbuild.cpp  -  Kit-bashed side profiles for the extended arsenal
//
//  u runs along the barrel (0 = butt), h is height above the bore axis, in
//  the same "gun units" as gunmodels.h. Each part is a convex polygon with a
//  thickness, extruded into a solid by the 3D view-model rasterizer.
// =============================================================================
#include "gunbuild.h"
#include <vector>
#include <algorithm>
#include <initializer_list>

namespace {
// palette
const uint32_t BLK = 0xFF1A1B1E, DRK = 0xFF2B2C2F, MID = 0xFF3C3E43, LGT = 0xFF505359, STL = 0xFF8A909C;
const uint32_t WOOD = 0xFF6B4226, WOOD2 = 0xFF7A4B2A, TAN = 0xFF8E7B57, OLV = 0xFF55663F, OLV2 = 0xFF44532F;
const uint32_t WHT = 0xFFD8DCE2, CYAN = 0xFF46D2FF, ORNG = 0xFFFF8A2A, RED = 0xFFB0302A, COPPER = 0xFFB87333;

struct Built {
    std::vector<GunPart> parts, hands;
    std::vector<float> widths;
    GunModel model{};
    GunGeom3D geom{};
    GunMounts mounts{};
};
std::vector<Built> g_built;             // index = weapon id (originals left empty)

struct GB {
    Built &b;
    void poly(std::initializer_list<float> pts, uint32_t col, float width, uint8_t flags = 0) {
        GunPart p{}; p.n = int(pts.size() / 2); p.col = col; p.flags = flags;
        int k = 0; for (float v : pts) p.pts[k++] = v;
        b.parts.push_back(p); b.widths.push_back(width);
    }
    void box(float u0, float h0, float u1, float h1, uint32_t col, float width, uint8_t flags = 0) {
        poly({u0, h1, u1, h1, u1, h0, u0, h0}, col, width, flags);
    }
};

// Hands from an archetype, shifted onto this gun's grip / fore-grip
void addHands(Built &b, ViewArch arch, float gU, float gH, float fU, float fH, bool pump = false)
{
    GunModel &m = b.model;
    auto push = [&](const GunPart &src, float du, float dh, bool keepAction) {
        GunPart p = src;
        for (int k = 0; k < p.n; k++) { p.pts[2 * k] += du; p.pts[2 * k + 1] += dh; }
        if (!keepAction) p.flags &= uint8_t(~GP_ACTION);
        b.hands.push_back(p);
    };
    switch (arch) {
    case VA_RIFLE: {
        float tdu = gU - 32, tdh = gH + 12, sdu = fU - 64, sdh = fH + 3;
        for (int i = 0; i < 4; i++) push(RIFLE_HANDS[i], sdu, sdh, false);
        for (int i = 4; i < 7; i++) push(RIFLE_HANDS[i], tdu, tdh, false);
        m.rWristU = 29 + tdu; m.rWristH = -20 + tdh; m.lWristU = 62 + sdu; m.lWristH = -14 + sdh;
        break; }
    case VA_SHOTGUN: {
        float tdu = gU - 25, tdh = gH + 8, sdu = fU - 69, sdh = fH + 5;
        for (int i = 0; i < 4; i++) push(SHOTGUN_HANDS[i], sdu, sdh, pump);
        for (int i = 4; i < 6; i++) push(SHOTGUN_HANDS[i], tdu, tdh, false);
        m.rWristU = 22 + tdu; m.rWristH = -14 + tdh; m.lWristU = 68 + sdu; m.lWristH = -15 + sdh;
        break; }
    case VA_PISTOL: {
        float du = gU - 5, dh = gH + 16;
        for (int i = 0; i < 4; i++) push(PISTOL_HANDS[i], du, dh, false);
        m.rWristU = 3 + du; m.rWristH = -27 + dh; m.lWristU = -1 + du; m.lWristH = -31 + dh;
        break; }
    case VA_KNIFE: {
        float du = gU - 10, dh = gH + 5;
        for (int i = 0; i < 5; i++) push(KNIFE_HANDS[i], du, dh, false);
        m.rWristU = 8 + du; m.rWristH = -10 + dh; m.lWristU = 0; m.lWristH = 0;
        break; }
    }
}

// Fill the GunModel header once parts and hands are in place
void finish(Built &b, ViewArch arch, float length, float muzzleU, float muzzleH, float ejectU, float ejectH,
            float gU, float gH, float fU, float fH, float handW, float scale, GunMounts mounts, bool pump = false)
{
    GunModel &m = b.model;
    m.length = length; m.muzzleU = muzzleU; m.muzzleH = muzzleH; m.ejectU = ejectU; m.ejectH = ejectH;
    m.gripU = gU; m.gripH = gH; m.foreU = fU; m.foreH = fH;
    float lo = 1e9f, hi = -1e9f;
    for (const GunPart &p : b.parts) for (int k = 0; k < p.n; k++) { lo = std::min(lo, p.pts[2 * k + 1]); hi = std::max(hi, p.pts[2 * k + 1]); }
    m.hMin = lo; m.hMax = hi;
    addHands(b, arch, gU, gH, fU, fH, pump);
    b.geom.handW = handW; b.geom.scale = scale;
    b.mounts = mounts;
}

void link(Built &b)
{
    b.model.parts = b.parts.data(); b.model.count = int(b.parts.size());
    b.model.hands = b.hands.data(); b.model.handCount = int(b.hands.size());
    b.geom.partW = b.widths.data(); b.geom.count = int(b.widths.size());
}

// ── The models ───────────────────────────────────────────────────────────────
void revolver(Built &b) {
    GB g{b};
    g.poly({2, -2, 14, -2, 10, -28, -2, -26}, WOOD, 5.0f);                    // grip
    g.box(2, -3, 30, 6, DRK, 4.8f);                                         // frame
    g.box(16, -3, 30, 10, MID, 7.6f, GP_ACTION);                            // cylinder
    g.box(18, 6, 28, 7, BLK, 7.9f, GP_NORIM | GP_ACTION);                   // cylinder flutes
    g.box(18, 0, 28, 1, BLK, 7.9f, GP_NORIM | GP_ACTION);
    g.box(30, 2, 58, 8, DRK, 3.4f);                                         // barrel
    g.box(30, -1, 52, 2, DRK, 3.8f);                                        // under-lug
    g.poly({10, 8, 16, 8, 14, 13, 9, 13}, BLK, 2.0f, GP_HAMMER);            // hammer
    g.box(14, -10, 24, -3, BLK, 4.0f, GP_NORIM);                            // trigger guard
    g.box(54, 8, 57, 11, BLK, 1.5f);                                        // front sight
    finish(b, VA_PISTOL, 58, 58, 5, 24, 4, 6, -14, 0, 0, 8.f, 0.0045f, {14, 30, 10, 40, -2});
}
void machinePistol(Built &b) {
    GB g{b};
    g.poly({-2, -28, 11, -30, 9, -46, -3, -44}, BLK, 4.2f, GP_MAG);          // extended mag
    g.poly({3, -3, 15, -3, 11, -30, -3, -28}, DRK, 5.0f);                    // grip
    g.box(4, -4, 44, 1, MID, 4.8f);                                         // frame
    g.box(15, -11, 26, -4, BLK, 5.1f, GP_NORIM);                            // trigger guard
    g.box(0, 1, 46, 9, DRK, 5.2f, GP_ACTION);                               // slide
    g.box(26, 5, 36, 8, BLK, 5.5f, GP_ACTION | GP_NORIM);                   // ejection port
    g.box(46, 1, 54, 8, BLK, 4.6f);                                         // compensator
    g.box(48, 6, 52, 8, LGT, 4.8f, GP_NORIM);                               // comp ports
    g.box(8, -2, 12, 0, ORNG, 5.0f, GP_NORIM);                              // selector
    g.box(1, 9, 6, 11, BLK, 4.4f, GP_ACTION);                               // rear sight
    g.box(42, 9, 45, 11, BLK, 1.6f, GP_ACTION);                             // front sight
    finish(b, VA_PISTOL, 54, 54, 4.5f, 31, 7, 5, -16, 0, 0, 8.f, 0.0042f, {12, 30, 9, 40, -4});
}
void smg(Built &b) {
    GB g{b};
    g.poly({0, 4, 14, 4, 14, -2, 0, -10}, DRK, 4.5f);                         // folding stock
    g.box(14, 0, 22, 4, MID, 3.0f);                                         // stock tube
    g.poly({22, 8, 52, 8, 56, -4, 22, -6}, BLK, 5.6f);                       // upper receiver
    g.poly({30, -6, 50, -6, 54, -14, 34, -14}, DRK, 5.2f);                   // angular lower
    g.poly({28, -6, 35, -6, 32, -20, 25, -19}, DRK, 4.4f);                   // pistol grip
    g.box(38, -34, 44, -14, MID, 3.6f, GP_MAG);                              // magazine
    g.box(24, 8, 52, 10, DRK, 4.0f);                                        // top rail
    g.box(52, 0, 66, 6, MID, 4.2f);                                         // barrel shroud
    g.box(66, 1, 72, 5, BLK, 2.6f);                                         // barrel
    g.box(36, 2, 44, 6, BLK, 5.9f, GP_NORIM);                               // ejection port
    finish(b, VA_RIFLE, 72, 72, 3, 40, 4, 31, -12, 58, -1, 9.5f, 0.0056f, {26, 50, 10, 60, 0});
}
void pdw(Built &b) {
    GB g{b};
    g.poly({0, 6, 10, 10, 54, 10, 60, 4, 60, -8, 0, -12}, BLK, 6.2f);       // bullpup shell
    g.box(14, 10, 50, 14, 0xFF5A6A70, 5.0f, GP_MAG);                        // top-mounted magazine
    g.box(20, -12, 30, -4, 0xFF0E0E10, 6.5f, GP_NORIM);                     // thumbhole
    g.poly({28, -8, 36, -8, 34, -22, 26, -22}, DRK, 5.4f);                   // grip
    g.poly({44, -8, 52, -8, 52, -16, 44, -16}, DRK, 5.4f);                   // fore grip
    g.box(60, -1, 68, 3, MID, 3.0f);                                        // barrel
    g.box(24, 14, 34, 18, DRK, 3.4f);                                       // sight housing
    finish(b, VA_RIFLE, 68, 68, 1, 30, -10, 31, -14, 48, -10, 9.5f, 0.0055f, {22, 40, 18, 52, -8});
}
void burstRifle(Built &b) {
    GB g{b};
    g.poly({0, 3, 22, 5, 22, -8, 0, -15}, BLK, 5.0f);                         // fixed stock
    g.box(27, -6, 48, 4, MID, 5.6f);                                        // lower
    g.box(26, 3, 50, 10, LGT, 5.6f);                                        // upper
    g.poly({30, 10, 46, 10, 46, 16, 30, 16}, MID, 3.0f);                     // carry handle
    g.box(33, 11, 43, 14, 0xFF0E0E10, 3.4f, GP_NORIM);                      // handle hole
    g.poly({31, -5, 38, -5, 35, -20, 28, -19}, DRK, 4.4f);                   // grip
    g.box(36, -10, 42, -6, BLK, 5.9f, GP_NORIM);                            // trigger guard
    g.poly({41, -6, 48, -6, 52, -26, 45, -27}, MID, 3.6f, GP_MAG);           // curved mag
    g.box(50, -3, 82, 8, DRK, 6.4f);                                        // round handguard
    g.poly({78, 8, 81, 8, 81, 17, 78, 15}, DRK, 2.2f);                       // front sight post
    g.box(82, 1, 100, 4, DRK, 2.6f);                                        // barrel
    g.box(100, 0, 106, 5, BLK, 3.4f);                                       // flash hider
    finish(b, VA_RIFLE, 106, 106, 2.5f, 40, 6.5f, 32, -12, 66, -3, 9.5f, 0.0062f, {30, 46, 16, 66, -3});
}
void battleRifle(Built &b) {
    GB g{b};
    g.poly({0, 6, 18, 6, 18, -6, 0, -14}, TAN, 5.0f);                         // stock
    g.box(18, 0, 26, 5, DRK, 3.4f);                                         // stock hinge
    g.box(26, -4, 76, 10, TAN, 5.8f);                                       // receiver
    g.box(34, 4, 44, 8, BLK, 6.1f, GP_NORIM);                               // ejection port
    g.poly({31, -5, 38, -5, 35, -20, 28, -19}, DRK, 4.4f);                   // grip
    g.box(36, -10, 42, -4, BLK, 5.9f, GP_NORIM);                            // trigger guard
    g.poly({42, -4, 50, -4, 52, -24, 44, -24}, DRK, 3.8f, GP_MAG);           // 20-rnd box mag
    g.box(26, 10, 76, 12, DRK, 4.0f);                                       // top rail
    g.box(56, 1, 74, 3, DRK, 6.1f, GP_NORIM);                               // side rail
    g.box(76, 1, 98, 5, DRK, 3.0f);                                         // barrel
    g.box(98, 0, 104, 6, BLK, 4.0f);                                        // muzzle brake
    finish(b, VA_RIFLE, 104, 104, 3, 40, 6, 32, -12, 64, -2, 9.5f, 0.0062f, {30, 70, 12, 66, -4});
}
void bullpup(Built &b) {
    GB g{b};
    g.poly({0, 8, 8, 10, 8, -12, 0, -10}, OLV, 5.6f);                         // butt
    g.poly({8, 10, 52, 10, 56, 0, 56, -10, 8, -12}, OLV, 6.0f);              // body
    g.poly({16, -12, 24, -12, 26, -30, 18, -30}, 0xFF3E4A44, 4.0f, GP_MAG);  // mag behind the grip
    g.poly({40, -10, 47, -10, 45, -24, 38, -24}, OLV2, 4.6f);                // grip
    g.poly({58, -8, 64, -8, 64, -24, 58, -24}, OLV2, 4.6f);                  // vertical fore grip
    g.box(46, -14, 58, -12, BLK, 5.0f, GP_NORIM);                           // guard
    g.box(56, 0, 84, 5, DRK, 3.0f);                                         // barrel
    g.box(84, 0, 90, 5, BLK, 3.6f);                                         // flash hider
    g.box(26, 10, 44, 16, OLV, 3.6f);                                       // optic mount
    g.box(20, 16, 50, 21, OLV2, 4.2f);                                      // integrated 1.5x tube
    g.poly({50, 15, 56, 13, 56, 23, 50, 21}, OLV2, 5.0f);                    // objective bell
    g.box(55, 14, 57, 22, 0xFF6FB0DA, 4.4f, GP_GLASS);                      // lens
    finish(b, VA_RIFLE, 90, 90, 2.5f, 28, 4, 43, -12, 61, -12, 9.5f, 0.0058f, {20, 50, 21, 61, -8});
}
void autoShotgun(Built &b) {
    GB g{b};
    g.poly({0, 2, 18, 6, 26, 5, 26, -6, 0, -14}, BLK, 5.0f);                  // stock
    g.box(26, -6, 60, 8, DRK, 6.6f);                                        // boxy receiver
    g.poly({36, -6, 54, -6, 56, -14, 52, -22, 40, -22, 34, -14}, MID, 8.2f, GP_MAG);   // drum
    g.poly({28, -6, 35, -6, 32, -20, 25, -19}, BLK, 4.6f);                   // grip
    g.box(30, 8, 52, 12, DRK, 3.2f);                                        // carry handle
    g.box(34, 9, 48, 11, 0xFF0E0E10, 3.6f, GP_NORIM);
    g.box(60, -1, 92, 6, MID, 5.2f);                                        // barrel shroud
    g.box(64, 1, 88, 3, BLK, 5.5f, GP_NORIM);                               // shroud vents
    g.box(92, 0, 98, 5, BLK, 3.4f);                                         // muzzle
    finish(b, VA_SHOTGUN, 98, 98, 2.5f, 44, 4, 25, -8, 69, -5, 9.5f, 0.0068f, {30, 52, 12, 76, -1});
}
void doubleBarrel(Built &b) {
    GB g{b};
    g.poly({0, 0, 14, 2, 24, 4, 24, -6, 12, -12, 2, -16}, WOOD, 4.4f);       // pistol-grip stock
    g.box(24, -5, 36, 6, MID, 5.4f);                                        // action block
    g.box(36, 1, 70, 6, DRK, 3.0f);                                         // top barrel
    g.box(36, -4, 70, 1, MID, 3.0f);                                        // bottom barrel
    g.box(36, -7, 56, -3, WOOD2, 4.6f);                                     // fore-end
    g.box(33, -1, 38, 2, LGT, 5.8f);                                        // hinge pin
    g.box(26, 6, 30, 8, BLK, 2.0f);                                         // top lever
    finish(b, VA_SHOTGUN, 70, 70, 1, 34, 2, 12, -7, 48, -6, 9.5f, 0.0065f, {30, 36, 6, 50, -7});
}
void lmg(Built &b) {
    GB g{b};
    g.poly({0, 4, 20, 6, 20, -6, 0, -14}, DRK, 5.2f);                         // stock
    g.box(20, -6, 58, 8, DRK, 6.4f);                                        // receiver
    g.box(30, 8, 56, 13, MID, 6.2f);                                        // feed cover
    g.poly({34, -6, 52, -6, 52, -28, 34, -28}, OLV2, 8.0f, GP_MAG);          // ammo box
    g.box(36, -6, 44, -2, 0xFFB08A3A, 8.4f, GP_NORIM);                      // belt links
    g.poly({22, -6, 29, -6, 26, -20, 19, -19}, BLK, 4.6f);                   // grip
    g.box(58, 10, 68, 14, DRK, 3.0f);                                       // carry handle
    g.box(58, -3, 84, 7, MID, 6.0f);                                        // heat shield
    g.box(84, 1, 104, 5, DRK, 3.0f);                                        // barrel
    g.box(104, 0, 110, 6, BLK, 3.6f);                                       // flash hider
    g.box(76, -5, 100, -3, BLK, 5.2f);                                      // folded bipod
    finish(b, VA_RIFLE, 110, 110, 3, 46, 4, 24, -12, 70, -3, 9.5f, 0.0062f, {30, 56, 13, 70, -3});
}
void sniper(Built &b) {
    GB g{b};
    g.poly({0, 6, 26, 6, 30, 0, 30, -12, 8, -14, 0, -10}, OLV, 5.0f);        // stock
    g.box(6, 6, 24, 10, OLV2, 4.4f);                                        // cheek rest
    g.poly({30, -6, 38, -6, 36, -22, 29, -21}, OLV2, 4.6f);                  // grip
    g.box(30, -6, 62, 7, DRK, 5.6f);                                        // receiver
    g.poly({44, 5, 48, 5, 52, -2, 48, -3}, LGT, 8.4f, GP_ACTION);            // bolt handle
    g.box(46, -16, 54, -6, BLK, 4.0f, GP_MAG);                              // magazine
    g.box(62, -6, 90, 6, OLV, 5.4f);                                        // fore-end
    g.box(90, 0, 122, 4, DRK, 2.6f);                                        // fluted barrel
    g.box(122, -1, 130, 5, BLK, 3.6f);                                      // muzzle brake
    g.box(40, 7, 44, 14, BLK, 3.2f);                                        // scope rings
    g.box(70, 7, 74, 14, BLK, 3.2f);
    g.box(34, 14, 80, 20, BLK, 4.2f);                                       // scope tube
    g.poly({30, 21, 36, 20, 36, 13, 30, 12}, BLK, 5.2f);                     // eyepiece
    g.box(54, 20, 60, 24, DRK, 3.2f);                                       // elevation turret
    g.poly({80, 20, 88, 24, 88, 10, 80, 14}, BLK, 6.4f);                     // objective bell
    g.box(87, 11, 89, 23, 0xFF6FB0DA, 5.8f, GP_GLASS);                      // lens
    finish(b, VA_RIFLE, 130, 130, 2, 46, 4, 33, -12, 76, -4, 9.5f, 0.0056f, {34, 80, 7, 80, -6});
}
void dmr(Built &b) {
    GB g{b};
    g.poly({0, 4, 22, 6, 22, -6, 0, -14}, TAN, 5.0f);                         // stock
    g.box(22, -6, 80, 4, TAN, 5.8f);                                        // chassis
    g.box(30, 4, 62, 10, DRK, 5.6f);                                        // receiver
    g.poly({31, -5, 38, -5, 35, -20, 28, -19}, DRK, 4.4f);                   // grip
    g.poly({44, -6, 52, -6, 53, -22, 45, -22}, DRK, 3.8f, GP_MAG);           // 15-rnd mag
    g.box(26, 10, 80, 12, DRK, 4.0f);                                       // rail
    g.box(80, 1, 104, 5, DRK, 2.8f);                                        // barrel
    g.box(104, 0, 112, 6, BLK, 3.8f);                                       // brake
    g.box(32, 14, 66, 19, BLK, 4.0f);                                       // 4x scope
    g.poly({28, 20, 32, 19, 32, 14, 28, 13}, BLK, 4.8f);
    g.poly({66, 19, 72, 22, 72, 11, 66, 14}, BLK, 5.6f);
    g.box(71, 12, 73, 21, 0xFF6FB0DA, 5.0f, GP_GLASS);
    g.box(40, 12, 44, 14, BLK, 3.0f); g.box(56, 12, 60, 14, BLK, 3.0f);
    finish(b, VA_RIFLE, 112, 112, 3, 42, 6, 32, -12, 68, -3, 9.5f, 0.006f, {32, 72, 12, 68, -6});
}
void rocket(Built &b) {
    GB g{b};
    g.box(0, -2, 90, 6, 0xFF3E4A30, 7.2f);                                  // launch tube
    g.poly({0, -5, 8, -2, 8, 6, 0, 9}, DRK, 8.4f);                           // rear flare
    g.poly({90, -6, 100, -6, 116, 0, 116, 4, 100, 10, 90, 10}, 0xFF5E6A3E, 10.f, GP_MAG);   // warhead
    g.box(116, 0, 122, 4, DRK, 3.0f, GP_MAG);                               // fuse tip
    g.box(38, 6, 62, 9, WOOD, 8.0f);                                        // heat shields
    g.box(38, -5, 62, -2, WOOD, 8.0f);
    g.poly({44, -2, 50, -2, 48, -16, 42, -16}, BLK, 4.6f);                   // trigger grip
    g.poly({28, -2, 34, -2, 34, -14, 28, -14}, BLK, 4.6f);                   // rear grip
    g.box(56, 6, 60, 14, BLK, 2.0f);                                        // iron sight
    finish(b, VA_RIFLE, 122, 122, 2, 0, 0, 46, -10, 31, -8, 9.5f, 0.0055f, {50, 60, 9, 40, -6});
}
void grenadeLauncher(Built &b) {
    GB g{b};
    g.poly({0, 2, 18, 4, 18, -6, 0, -12}, DRK, 5.0f);                         // stock
    g.box(18, -4, 40, 6, MID, 5.5f);                                        // frame
    g.poly({40, -10, 64, -10, 66, -4, 66, 8, 64, 12, 40, 12}, DRK, 12.f, GP_ACTION);   // revolving cylinder
    g.box(42, 4, 62, 6, BLK, 12.4f, GP_NORIM | GP_ACTION);                  // chamber flutes
    g.box(42, -6, 62, -4, BLK, 12.4f, GP_NORIM | GP_ACTION);
    g.box(66, -2, 92, 8, OLV2, 7.0f);                                       // wide barrel
    g.poly({70, -2, 76, -2, 76, -16, 70, -16}, BLK, 4.6f);                   // fore grip
    g.poly({24, -4, 31, -4, 28, -18, 21, -17}, BLK, 4.6f);                   // grip
    g.box(70, 8, 74, 14, BLK, 2.4f);                                        // sight
    finish(b, VA_SHOTGUN, 92, 92, 3, 50, 4, 25, -8, 73, -10, 9.5f, 0.0062f, {66, 80, 8, 73, -2});
}
void plasma(Built &b) {
    GB g{b};
    g.poly({0, 6, 16, 8, 16, -8, 0, -14}, WHT, 5.0f);                         // stock
    g.poly({16, 10, 60, 10, 68, 4, 68, -6, 16, -8}, WHT, 6.6f);              // body
    g.box(20, 2, 60, 4, 0xFF9AA2AE, 6.9f, GP_NORIM);                        // seam
    g.box(36, -18, 50, -8, CYAN, 5.0f, GP_MAG | GP_GLOW);                   // energy cell
    g.box(52, -2, 80, 6, DRK, 5.0f);                                        // coil housing
    g.box(56, -3, 58, 7, CYAN, 5.4f, GP_GLOW);                              // glowing coil rings
    g.box(64, -3, 66, 7, CYAN, 5.4f, GP_GLOW);
    g.box(72, -3, 74, 7, CYAN, 5.4f, GP_GLOW);
    g.box(80, -1, 90, 7, WHT, 6.0f);                                        // emitter
    g.box(90, 1, 92, 5, CYAN, 4.0f, GP_GLOW);                               // muzzle glow
    g.poly({24, -8, 31, -8, 29, -22, 22, -21}, DRK, 4.6f);                   // grip
    g.box(30, 10, 46, 15, DRK, 3.4f);                                       // sight
    g.box(42, 11, 44, 14, CYAN, 3.6f, GP_GLOW);
    finish(b, VA_RIFLE, 92, 92, 3, 40, 6, 27, -14, 62, -4, 9.5f, 0.0058f, {26, 50, 15, 62, -6});
}
void arcGun(Built &b) {
    GB g{b};
    g.poly({0, 4, 14, 6, 14, -6, 0, -12}, DRK, 5.0f);                         // stock
    g.box(14, -6, 50, 8, MID, 6.0f);                                        // body
    g.box(28, -20, 40, -6, 0xFFE0B030, 5.0f, GP_MAG);                       // battery
    g.box(30, -14, 38, -12, BLK, 5.3f, GP_NORIM | GP_MAG);
    g.box(50, -4, 74, 8, COPPER, 7.0f);                                     // copper coil
    for (int k = 0; k < 6; k++) g.box(52 + k * 4, -5, 53.5f + k * 4, 9, 0xFF6A3A18, 7.4f, GP_NORIM);
    g.poly({74, 6, 88, 11, 88, 9, 74, 3}, STL, 2.4f);                        // tesla prongs
    g.poly({74, -1, 88, -6, 88, -4, 74, 2}, STL, 2.4f);
    g.box(74, 1, 84, 3, 0xFF9AD8FF, 1.6f, GP_GLOW);                         // arc core
    g.poly({18, -6, 25, -6, 22, -20, 15, -19}, BLK, 4.6f);                   // grip
    finish(b, VA_RIFLE, 88, 88, 2, 30, 6, 20, -12, 60, -6, 9.5f, 0.0058f, {18, 48, 8, 60, -6});
}
void railgun(Built &b) {
    GB g{b};
    g.poly({0, 6, 20, 8, 20, -8, 0, -14}, DRK, 5.0f);                         // stock
    g.poly({20, 10, 56, 10, 60, 2, 60, -8, 20, -8}, MID, 6.6f);              // body
    g.box(30, -16, 48, -8, DRK, 5.4f, GP_MAG);                              // capacitor bank
    g.box(32, -13, 46, -11, CYAN, 5.7f, GP_GLOW | GP_MAG);
    g.box(56, 6, 118, 9, LGT, 2.4f);                                        // upper rail
    g.box(56, -5, 118, -2, LGT, 2.4f);                                      // lower rail
    g.box(60, 1, 114, 3, 0xFF60E0FF, 1.2f, GP_GLOW);                        // field between the rails
    for (int k = 0; k < 5; k++) g.box(64 + k * 11, -5, 66 + k * 11, 9, DRK, 4.0f);   // rail clamps
    g.box(28, 14, 52, 19, BLK, 3.8f);                                       // scope
    g.box(51, 14, 53, 19, 0xFF6FB0DA, 4.0f, GP_GLASS);
    g.box(34, 10, 38, 14, BLK, 3.0f);
    g.poly({24, -8, 31, -8, 29, -22, 22, -21}, BLK, 4.6f);                   // grip
    finish(b, VA_RIFLE, 118, 118, 2, 40, 6, 27, -14, 66, -6, 9.5f, 0.0055f, {28, 52, 10, 66, -6});
}
void flamethrower(Built &b) {
    GB g{b};
    g.poly({0, 4, 16, 6, 16, -6, 0, -12}, DRK, 5.0f);                         // stock
    g.box(16, -5, 56, 6, MID, 6.0f);                                        // body
    g.poly({24, -6, 46, -6, 48, -10, 46, -24, 24, -24, 22, -10}, RED, 9.0f, GP_MAG);   // fuel tank
    g.box(26, -18, 44, -16, 0xFF7A1A14, 9.3f, GP_NORIM | GP_MAG);
    g.box(56, -1, 86, 5, STL, 3.4f);                                        // nozzle tube
    g.box(86, -2, 92, 6, DRK, 4.6f);                                        // pilot holder
    g.box(92, 0, 94, 2, ORNG, 2.0f, GP_GLOW);                               // pilot light
    g.poly({62, -1, 68, -1, 68, -16, 62, -16}, BLK, 4.6f);                   // front grip
    g.poly({20, -5, 27, -5, 24, -19, 17, -18}, BLK, 4.6f);                   // grip
    finish(b, VA_RIFLE, 94, 94, 2, 0, 0, 22, -12, 65, -10, 9.5f, 0.006f, {20, 50, 6, 65, -2});
}
void machete(Built &b) {
    GB g{b};
    g.box(-6, -5, 0, 2, 0xFF282A2E, 3.6f);                                   // pommel
    g.poly({0, 3, 22, 3, 22, -6, 0, -6}, 0xFF2A1F14, 3.6f);                  // grip
    g.box(22, -6, 25, 6, 0xFF353840, 4.4f);                                  // guard
    g.poly({25, 6, 80, 8, 92, 4, 92, -2, 80, -6, 25, -5}, 0xFF9CA3AE, 1.0f); // blade
    g.poly({25, -5, 80, -6, 92, -2, 90, -4, 80, -8, 25, -7}, 0xFFE4EAF0, 0.6f);   // edge
    finish(b, VA_KNIFE, 92, 92, 2, 0, 0, 10, -5, 0, 0, 6.f, 0.0042f, {0, 0, 0, 0, 0});
}
void baton(Built &b) {
    GB g{b};
    g.box(0, -4, 24, 3, BLK, 4.0f);                                         // rubber handle
    g.box(24, -5, 27, 4, DRK, 4.6f);                                        // guard
    g.box(27, -2.5f, 68, 1.5f, MID, 3.0f);                                  // shaft
    g.box(68, -3.5f, 72, 2.5f, DRK, 4.0f);                                  // tip
    g.box(72, -2, 74, -1, CYAN, 1.6f, GP_GLOW);                             // electrodes
    g.box(72, 0, 74, 1, CYAN, 1.6f, GP_GLOW);
    finish(b, VA_KNIFE, 74, 74, -0.5f, 0, 0, 10, -5, 0, 0, 6.f, 0.0042f, {0, 0, 0, 0, 0});
}
void axe(Built &b) {
    GB g{b};
    g.box(0, -3.5f, 84, 2.5f, WOOD, 4.0f);                                  // handle
    g.poly({70, 4, 84, 6, 92, 18, 92, -22, 84, -10, 70, -6}, RED, 2.6f);     // head
    g.box(90, -22, 94, 18, 0xFFC8CED6, 1.4f);                               // edge
    g.poly({72, 4, 58, 14, 56, 12, 70, -2}, DRK, 2.2f);                      // back spike
    finish(b, VA_KNIFE, 94, 94, -2, 0, 0, 10, -5, 0, 0, 6.f, 0.0045f, {0, 0, 0, 0, 0});
}

// Originals: their geometry lives in gunmodels.h
const GunMounts ORIGINAL_MOUNTS[4] = {
    {0, 0, 0, 0, 0},          // knife
    {12, 30, 9, 30, -4},      // pistol: slide top, dust cover
    {30, 46, 6, 69, -8},      // shotgun: receiver top, pump
    {26, 49, 11, 64, -3},     // rifle: flat-top rail, handguard
};
} // namespace

void buildGunModels()
{
    if (!g_built.empty()) return;
    g_built.resize(WPN_COUNT);
    using Fn = void (*)(Built &);
    struct Job { int w; Fn fn; };
    const Job jobs[] = {
        {WPN_REVOLVER, revolver}, {WPN_MACHINE_PISTOL, machinePistol}, {WPN_SMG, smg}, {WPN_PDW, pdw},
        {WPN_BURST_RIFLE, burstRifle}, {WPN_BATTLE_RIFLE, battleRifle}, {WPN_BULLPUP, bullpup},
        {WPN_AUTO_SHOTGUN, autoShotgun}, {WPN_DOUBLE_BARREL, doubleBarrel}, {WPN_LMG, lmg},
        {WPN_SNIPER, sniper}, {WPN_DMR, dmr}, {WPN_ROCKET, rocket}, {WPN_GRENADE_LAUNCHER, grenadeLauncher},
        {WPN_PLASMA, plasma}, {WPN_ARC, arcGun}, {WPN_RAILGUN, railgun}, {WPN_FLAMETHROWER, flamethrower},
        {WPN_MACHETE, machete}, {WPN_BATON, baton}, {WPN_AXE, axe},
    };
    for (const Job &j : jobs) { j.fn(g_built[size_t(j.w)]); link(g_built[size_t(j.w)]); }
}

const GunModel &gunModel(int w)
{
    if (w >= 0 && w < 4) return *GUN_MODELS[w];
    if (g_built.empty()) buildGunModels();
    return g_built[size_t(w)].model;
}
const GunGeom3D &gunGeom(int w)
{
    if (w >= 0 && w < 4) return GUN_GEOM3D[w];
    if (g_built.empty()) buildGunModels();
    return g_built[size_t(w)].geom;
}
const GunMounts &gunMounts(int w)
{
    if (w >= 0 && w < 4) return ORIGINAL_MOUNTS[w];
    if (g_built.empty()) buildGunModels();
    return g_built[size_t(w)].mounts;
}
