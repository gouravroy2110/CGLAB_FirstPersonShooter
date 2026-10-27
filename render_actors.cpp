// =============================================================================
//  render_actors.cpp  -  3D soldiers standing in the world
//
//  Your own body in third person and every squad bot are 3D
//  soldiers: smooth-shaded tubes and ellipsoids (lit per vertex) for the
//  limbs, torso, plate carrier, pack, head and helmet, plus the same
//  extruded gun profiles as the first-person weapon. They are transformed
//  into the camera frame used by the terrain marcher and drawn by the shared
//  triangle rasterizer (raster3d.h) against the world z-buffer, so hills and
//  walls hide them pixel by pixel. Lit by the sun with the light-map shadow.
// =============================================================================
#include "mainwindow.h"
#include "gunmodels.h"
#include "raster3d.h"
#include "gunbuild.h"
#include "soldier3d.h"
#include <cmath>
#include <algorithm>

// A squadmate / multiplayer operative as a soldier pose: drawn with it, and shot against it.
// In multiplayer the colours are the teams': Alpha red, Bravo blue (as the scoreboard).
bool MainWindow::actorPose(const Actor &a, Soldier3D &sp) const
{
    if (!a.alive && a.respawnT <= 0.f) return false;
    sp = Soldier3D{};
    sp.x = a.posX; sp.y = a.posY; sp.z = a.footZ;
    sp.yaw = std::atan2(a.dirY, a.dirX);
    sp.walk = a.walkPhase;
    sp.move = std::min(1.f, std::sqrt(a.velX * a.velX + a.velY * a.velY) / 3.f);
    sp.fall = a.alive ? 0.f : 1.f;
    sp.weapon = a.curWeapon;
    if (m_gameMode == MODE_MULTIPLAYER) {
        sp.look = a.team == 0 ? ENEMY_LOOK : SQUAD_LOOK;
        sp.band = a.team == 0 ? rs::rgb(230, 60, 50) : rs::rgb(60, 140, 255);
    } else {
        sp.look = a.team == 0 ? SQUAD_LOOK : ENEMY_LOOK;
        sp.band = a.escort ? rs::rgb(240, 240, 240) : a.team == 0 ? rs::rgb(60, 140, 255) : rs::rgb(230, 60, 50);
    }
    sp.sprint = a.sprinting;
    sp.recoil = std::min(1.f, a.shootAnim / 0.12f);
    sp.seed = float((a.id * 29) % 100) / 100.f;
    sp.t = m_time;
    return true;
}

using namespace rs;

namespace {
const float PI = 3.14159265f;

} // namespace

// World <-> camera frame of the terrain renderer (x right, y down, z forward)
struct WorldCam {
    float cx, cy, cz, dX, dY;
    V3 point(float wx, float wy, float wz) const {
        float sx = wx - cx, sy = wy - cy;
        return V3{sx * -dY + sy * dX, cz - wz, sx * dX + sy * dY};
    }
    V3 dir(float wx, float wy, float wz) const { return V3{wx * -dY + wy * dX, -wz, wx * dX + wy * dY}; }
};

// Planar shadow projection: a world point slides along the sun direction onto
// the ground plane under the soldier's feet
struct ShadowProj { float gz, lx, ly, lz; const World *w = nullptr; };

// The soldier's shadow, built directly on the ground: the planar projection of a
// round limb is a quad as wide as the limb, and of a joint a disc, so each part
// becomes one flat polygon (projected along the sun, stencilled so overlaps
// darken once). Far cheaper than flattening every face of the rounded body.
static void drawSoldierShadow(Raster3D &r, const WorldCam &cam, const Soldier3D &p, const ShadowProj &sh)
{
    SoldierJoints J; soldierJoints(p, J);
    const SoldierXf X = soldierXf(p);
    auto G = [&](const float *j, float &gx, float &gy) {          // joint -> its spot on the ground
        float wx, wy, wz; X.world(j[0], j[1], j[2], wx, wy, wz);
        float k = (wz - sh.gz) / sh.lz;
        gx = wx - sh.lx * k; gy = wy - sh.ly * k;
    };
    const float S = p.scale;
    // each shadow corner sits on the real terrain under it (rolling ground, steps)
    auto GP = [&](float x, float y) { return cam.point(x, y, (sh.w ? sh.w->floorH(x, y) : sh.gz) + 0.012f); };
    auto quad = [&](const float *a, const float *b, float ra, float rb) {
        float ax, ay, bx, by; G(a, ax, ay); G(b, bx, by);
        float dx = bx - ax, dy = by - ay, l = std::sqrt(dx * dx + dy * dy);
        float nx = l > 1e-4f ? -dy / l : 1.f, ny = l > 1e-4f ? dx / l : 0.f;
        ra *= S; rb *= S;
        V3 q[4] = { GP(ax + nx * ra, ay + ny * ra), GP(bx + nx * rb, by + ny * rb), GP(bx - nx * rb, by - ny * rb), GP(ax - nx * ra, ay - ny * ra) };
        r.shadowPoly(q, 4);
    };
    auto disc = [&](const float *c, float rad) {
        float cx, cy; G(c, cx, cy);
        V3 q[8];
        for (int k = 0; k < 8; k++) { float t = k * 0.785398f; q[k] = GP(cx + std::cos(t) * rad * S, cy + std::sin(t) * rad * S); }
        r.shadowPoly(q, 8);
    };
    const float pelvis[3] = { 0.f, 0.f, 0.43f - J.drop }, chest[3] = { J.lean, J.head[1] * 0.6f, 0.72f - J.drop };
    const float pack0[3] = { J.lean * 0.4f - 0.115f, 0.f, 0.53f - J.drop }, pack1[3] = { J.lean - 0.115f, 0.f, 0.705f - J.drop };
    const float K = SOLDIER_BULK;
    quad(pelvis, chest, 0.09f * K, 0.105f * K);
    quad(pack0, pack1, 0.07f * K, 0.066f * K);
    disc(J.head, 0.06f * soldierHeadK(p.gear));
    for (int s = 0; s < 2; s++) {
        quad(J.hip[s], J.knee[s], 0.048f * K, 0.038f * K);
        quad(J.knee[s], J.ankle[s], 0.036f * K, 0.03f * K);
        const float toe[3] = { J.ankle[s][0] + 0.075f, J.ankle[s][1], std::max(0.f, J.ankle[s][2] - 0.02f) };
        quad(J.ankle[s], toe, 0.03f, 0.028f);
        quad(J.sh[s], J.elbow[s], 0.036f * K, 0.03f * K);
        quad(J.elbow[s], J.hand[s], 0.029f * K, 0.024f * K);
        disc(J.knee[s], 0.036f * K); disc(J.elbow[s], 0.03f * K);
    }
    if (p.weapon >= 0 && p.weapon < WPN_COUNT) {                     // the gun: a thin bar along the aim
        float len = WEAPONS[p.weapon].gunLen;
        const float *h = J.hand[1];
        float pc = std::cos(J.aimP), ps = std::sin(J.aimP);
        const float tip[3] = { h[0] + pc * len * 0.75f, h[1], h[2] + ps * len * 0.75f }, butt[3] = { h[0] - pc * len * 0.25f, h[1], h[2] - ps * len * 0.25f };
        quad(butt, tip, 0.02f, 0.014f);
    }
    if (p.gear == SG_SHIELD && p.fall <= 0.f) {
        const float s0[3] = { 0.2f, -0.06f, 0.22f - J.drop }, s1[3] = { 0.2f, -0.06f, 0.8f - J.drop };
        quad(s0, s1, 0.13f, 0.13f);
    }
}

// Build and rasterize one soldier on its joints (soldier3d.h). Local frame:
// a = forward, b = right, c = up. Rounded smooth-shaded parts; detail drops
// with distance. With `sh` set it draws the soldier's shadow instead: every
// vertex is projected onto the ground along the sun and the silhouette is
// darkened once (stencil), the classic planar-shadow technique.
static void drawSoldier(Raster3D &r, const WorldCam &cam, const Soldier3D &p, const ShadowProj *sh = nullptr)
{
    SoldierJoints J; soldierJoints(p, J);
    const SoldierXf X = soldierXf(p);
    auto P = [&](float a, float b, float c) {
        float wx, wy, wz; X.world(a, b, c, wx, wy, wz);
        if (sh) { float k = (wz - sh->gz) / sh->lz; wx -= sh->lx * k; wy -= sh->ly * k; wz = sh->gz; }
        return cam.point(wx, wy, wz);
    };
    auto PJ = [&](const float *j) { return P(j[0], j[1], j[2]); };
    auto PL = [&](const float *j, float da, float db, float dc) { return P(j[0] + da, j[1] + db, j[2] + dc); };
    auto D = [&](float a, float b, float c) { float wx, wy, wz; X.dir(a, b, c, wx, wy, wz); return norm(cam.dir(wx, wy, wz)); };
    const SoldierLook &L = p.look;
    V3 fwd = D(1, 0, 0), right = D(0, 1, 0), up = D(0, 0, 1);
    // level of detail from the distance to the camera (shadows are always coarse)
    float dist = std::max(0.1f, P(0, 0, 0.5f).z);
    const bool farAway = dist > 18.f;                                      // a few pixels tall: the coarsest shapes
    const bool hq = !sh && dist < 9.f;                                     // close: materials, anatomy and kit detail
    const int SEG = sh ? 6 : dist < 4.f ? 12 : dist < 10.f ? 9 : farAway ? 4 : 6;
    const int SIDES = sh ? 6 : dist < 4.f ? 10 : dist < 10.f ? 8 : farAway ? 4 : 6;
    const bool fine = !sh && dist < 9.f;
    if (hq) { V3 O = P(0, 0, 0); r.setLocalFrame(O, P(1, 0, 0) - O, P(0, 1, 0) - O, P(0, 0, 1) - O); }
    // materials (only up close; further away the plain colours below are used)
    auto useMat = [&](int kind, uint32_t c0, uint32_t c1 = 0, uint32_t c2 = 0, float scale = 8.f, float shine = 0.1f) {
        if (!hq) { r.mat.kind = MAT_NONE; return; }
        r.mat.kind = kind; r.mat.c0 = c0; r.mat.c1 = c1 ? c1 : c0; r.mat.c2 = c2 ? c2 : c0; r.mat.scale = scale; r.mat.shine = shine;
    };
    // the part that was just hit glows red for a moment
    // (a brief warm highlight, not a repaint: the kit keeps its colours)
    auto tint = [&](uint32_t c, int part) { return (p.flashK > 0.f && p.flashPart == part) ? lerpCol(c, rgb(255, 120, 90), 0.3f * p.flashK) : c; };
    const uint32_t U = tint(L.uniform, PART_LIMB), UD = tint(L.uniformDark, PART_LIMB);
    const uint32_t TU = tint(L.uniform, PART_BODY), TUD = tint(L.uniformDark, PART_BODY), VEST = tint(L.vest, PART_BODY), VD = tint(L.vestDark, PART_BODY);
    const uint32_t HEADC = tint(L.mask, PART_HEAD), HELM = tint(L.helmet, PART_HEAD), SKIN = tint(L.skin, PART_HEAD);
    const uint32_t CAMO_L = lerpCol(L.uniform, rgb(214, 204, 176), 0.3f);   // the light blotch of the camo
    auto camo = [&](uint32_t base, int part) { useMat(MAT_CAMO, tint(base, part), tint(L.uniformDark, part), tint(CAMO_L, part), 9.f, 0.06f); };
    float S = p.scale;                                                     // part thickness scale (the head block raises it)
    // tessellation follows the size on screen: a part a few pixels across needs few facets
    auto segFor = [&](float radius, int lo, int hi) {
        float px = radius * S * r.fx / dist;
        return std::max(lo, std::min(hi, (int(px * 0.8f) + 1) & ~1));
    };
    auto ball = [&](V3 c, float ra, float rb, float rc, uint32_t col, float spec) {
        r.ellipsoid(c, fwd, right, up, ra * S, rb * S, rc * S, col, spec, std::min(SEG, segFor(std::max({ra, rb, rc}), 6, 14)));
    };
    auto joint = [&](V3 c, float ra, float rb, float rc, uint32_t col, float spec) {
        r.ellipsoid(c, fwd, right, up, ra * S, rb * S, rc * S, col, spec, std::min(std::max(6, SEG - 4), segFor(std::max({ra, rb, rc}), 6, 10)));
    };
    auto tube = [&](V3 a, V3 b, float ra1, float ra2, float rb1, float rb2, uint32_t col, float spec, int sides) {
        r.tube(a, b, right, fwd, ra1 * S, ra2 * S, rb1 * S, rb2 * S, col, spec, sides);
    };
    auto limb = [&](V3 a, V3 b, float ra, float rb, uint32_t col, float spec) { r.tube(a, b, ra * S, rb * S, col, spec, std::min(SIDES, segFor(std::max(ra, rb), 5, 12))); };
    // a box with rounded edges (plates, pouches, boots): superellipse cross-section
    auto rbox = [&](V3 a, V3 b, V3 s1, V3 s2, float r1, float r2, float q1, float q2, uint32_t col, float spec) {
        if (farAway || sh) r.boxAxes(a, b, s1, s2, r1 * S, r2 * S, q1 * S, q2 * S, col, spec);
        else r.tube(a, b, s1, s2, r1 * S, r2 * S, q1 * S, q2 * S, col, spec, std::min(hq ? 12 : 8, std::max(8, segFor(std::max(r1, r2), 8, 12))), true, 5.f);
    };
    auto slab = [&](V3 a, V3 b, V3 s1, V3 s2, float r1, float r2, float q1, float q2, uint32_t col, float spec) {
        r.boxAxes(a, b, s1, s2, r1 * S, r2 * S, q1 * S, q2 * S, col, spec);
    };
    const float lean = J.lean, dz = J.drop, hz = J.breathe;
    const bool heavy = p.gear == SG_HEAVY;
    const float bulk = (heavy ? 1.08f : 1.f) * SOLDIER_BULK;           // heavies: a touch broader (they are also taller)
    const float hb2 = J.head[1] * 0.6f;                                // lean carries the chest sideways

    // Outer layers are drawn first (gun, arms, kit, plates, then the body under them):
    // hidden pixels then fail the depth test before any texturing work is done
    if (!sh && p.weapon >= 0 && p.weapon < WPN_COUNT) {
        // Gun: extruded profile, grip in the right hand, barrel along the aim direction
        float pc = std::cos(J.aimP), ps = std::sin(J.aimP);
        V3 rHand = PJ(J.hand[1]);
        const GunModel &M = gunModel(p.weapon);
        const GunGeom3D &G3 = gunGeom(p.weapon);
        float GS = WEAPONS[p.weapon].gunLen / M.length * p.scale;
        V3 Fd = D(pc, 0, ps), Ud = D(-ps, 0, pc), Rd = right;
        V3 O = rHand - Fd * (M.gripU * GS) - Ud * (M.gripH * GS);
        auto map = [&](float u, float h, float wz) { return O + Fd * (u * GS) + Ud * (h * GS) + Rd * (wz * GS); };
        for (int i = 0; i < M.count; i++) {
            const GunPart &part = M.parts[i];
            if (part.flags & GP_NORIM) continue;                  // tiny recessed details vanish at this size
            float width = i < G3.count ? G3.partW[i] : 4.f;
            bool glass = (part.flags & GP_GLASS) != 0;
            r.emissive = (part.flags & GP_GLOW) != 0;
            if (hq && !glass && !r.emissive) useMat(MAT_METAL, part.col, 0, 0, 8.f, 0.55f);                 // gunmetal up close
            r.extrude(part.pts, part.n, width, map, Fd, Ud, Rd, part.col, glass ? 0.8f : 0.35f, glass);
            r.mat.kind = MAT_NONE;
            r.emissive = false;
        }
    }

    // riot shield on the left forearm: a slab with a view slit
    if (p.gear == SG_SHIELD && p.fall <= 0.f) {
        useMat(MAT_PLASTIC, tint(rgb(52, 58, 68), PART_SHIELD), 0, 0, 8.f, 0.45f);
        V3 c0 = P(0.2f, -0.06f, 0.22f - dz), c1 = P(0.2f, -0.06f, 0.8f - dz);
        r.tube(c0, c1, right, fwd, 0.13f * S, 0.013f * S, 0.13f * S, 0.013f * S, tint(rgb(52, 58, 68), PART_SHIELD), 0.35f, hq ? 12 : 4, true, hq ? 8.f : 2.f);
        if (!sh) {
            useMat(MAT_NONE, 0);
            slab(P(0.215f, -0.06f, 0.66f - dz), P(0.215f, -0.06f, 0.71f - dz), right, fwd, 0.09f, 0.003f, 0.09f, 0.003f, rgb(150, 190, 220), 0.9f);   // visor slit
            slab(P(0.215f, -0.06f, 0.36f - dz), P(0.215f, -0.06f, 0.38f - dz), right, fwd, 0.11f, 0.003f, 0.11f, 0.003f, rgb(220, 220, 220), 0.3f);   // POLICE stripe
        }
    }

    // --- Arms -----------------------------------------------------------------------------
    for (int s = 0; s < 2; s++) {
        V3 shp = PJ(J.sh[s]), el = PJ(J.elbow[s]), hd = PJ(J.hand[s]);
        camo(L.uniform, PART_LIMB);
        joint(shp, 0.044f * bulk, 0.046f * bulk, 0.042f * bulk, U, 0.06f);                               // deltoid
        V3 bi = shp + (el - shp) * 0.45f;
        limb(shp, bi, 0.038f * bulk, 0.036f * bulk, U, 0.05f);
        limb(bi, el, 0.036f * bulk, 0.03f * bulk, U, 0.05f);
        if (fine) { useMat(MAT_CLOTH, p.band, 0, 0, 20.f, 0.1f); limb(shp + (el - shp) * 0.28f, shp + (el - shp) * 0.42f, 0.0395f * bulk, 0.037f * bulk, p.band, 0.2f); }   // team armband
        V3 ed = norm(hd - el);
        if (hq) { useMat(MAT_RUBBER, scaleCol(VD, 0.85f), 0, 0, 8.f, 0.25f); joint(el - ed * (0.008f * S), 0.03f * bulk, 0.032f * bulk, 0.03f * bulk, VD, 0.2f); }   // elbow pad
        camo(L.uniformAlt, PART_LIMB);
        V3 wrist = hd - ed * (0.03f * S);
        limb(el, wrist, 0.03f * bulk, 0.025f * bulk, tint(L.uniformAlt, PART_LIMB), 0.05f);
        // glove: a palm and fingers curled round the grip
        uint32_t gl = tint(L.gloves, PART_LIMB);
        useMat(MAT_RUBBER, gl, 0, 0, 8.f, 0.15f);
        if (!sh) {
            limb(wrist, wrist + ed * (0.008f * S), 0.03f, 0.03f, gl, 0.1f);                                // cuff
            r.tube(wrist, hd + ed * (0.012f * S), right, up, 0.026f * S, 0.014f * S, 0.028f * S, 0.013f * S, gl, 0.15f, SIDES, true, hq ? 3.f : 2.f);   // palm
            if (hq) {
                V3 knuckle = hd + ed * (0.012f * S);
                V3 curl = knuckle + ed * (0.012f * S) - up * (0.02f * S);
                r.tube(knuckle, curl, right, up, 0.021f * S, 0.009f * S, 0.019f * S, 0.008f * S, gl, 0.15f, 6, true, 3.f);   // fingers
                limb(hd - up * (0.004f * S) + right * (s ? -0.016f : 0.016f) * S, hd + ed * (0.02f * S) + right * (s ? -0.018f : 0.018f) * S, 0.008f, 0.007f, gl, 0.15f);   // thumb
            }
        }
    }

    // --- Head: goggles and headgear first, the face under them ---------------------------
    const float HK = soldierHeadK(p.gear);
    const float hx = J.head[0], hy = J.head[1], hc = J.head[2] + SOLDIER_HEAD_LIFT * HK / SOLDIER_HEAD;
    // the head is drawn a size up (it reads too small on the broad body): every point
    // is scaled about the head centre and every thickness by the same factor
    auto PH = [&](float a, float b, float c) { return P(hx + (a - hx) * HK, hy + (b - hy) * HK, hc + (c - hc) * HK); };
    S = p.scale * HK;
    if (!sh) {
        // goggles: a rubber frame and a lit lens across the face
        useMat(MAT_RUBBER, scaleCol(L.boots, 0.6f), 0, 0, 8.f, 0.3f);
        rbox(PH(hx + 0.04f, hy, hc - 0.005f), PH(hx + 0.04f, hy, hc + 0.017f), right, fwd, 0.037f, 0.009f, 0.038f, 0.009f, scaleCol(L.boots, 0.6f), 0.2f);
        useMat(MAT_NONE, 0);
        r.emissive = true;
        slab(PH(hx + 0.049f, hy, hc - 0.002f), PH(hx + 0.049f, hy, hc + 0.013f), right, fwd, 0.032f, 0.003f, 0.033f, 0.003f, L.lens, 0.9f);
        r.emissive = false;
        if (hq) { useMat(MAT_NONE, 0); r.emissive = true; slab(PH(hx + 0.0515f, hy - 0.016f, hc + 0.008f), PH(hx + 0.0515f, hy - 0.016f, hc + 0.012f), right, fwd, 0.006f, 0.002f, 0.006f, 0.002f, rgb(255, 255, 255), 0.f); r.emissive = false; }
    }

    if (p.gear == SG_STALKER) {
        useMat(MAT_CLOTH, tint(rgb(24, 26, 30), PART_HEAD), 0, 0, 18.f, 0.04f);
        r.ellipsoid(PH(hx - 0.01f, hy, hc + 0.005f), fwd, right, up, 0.058f * S, 0.052f * S, 0.064f * S, tint(rgb(24, 26, 30), PART_HEAD), 0.05f, SEG, true);
    } else if (p.gear == SG_SNIPER && p.helmet) {
        // boonie hat: a crown and a wide floppy brim
        useMat(MAT_CAMO, HELM, scaleCol(HELM, 0.75f), lerpCol(HELM, rgb(214, 204, 176), 0.3f), 12.f, 0.04f);
        r.ellipsoid(PH(hx - 0.004f, hy, hc + 0.012f), fwd, right, up, 0.05f * S, 0.047f * S, 0.05f * S, HELM, 0.1f, SEG, true);
        tube(PH(hx - 0.004f, hy, hc + 0.006f), PH(hx - 0.004f, hy, hc + 0.016f), 0.088f, 0.082f, 0.086f, 0.08f, scaleCol(HELM, 0.85f), 0.05f, SIDES);
    } else if (p.helmet) {
        // helmet with a camo cover, a rim, side rails, a night-vision mount and a chin strap
        useMat(MAT_CAMO, HELM, scaleCol(HELM, 0.72f), lerpCol(HELM, rgb(214, 204, 176), 0.25f), 12.f, 0.12f);
        r.ellipsoid(PH(hx - 0.004f, hy, hc + 0.01f), fwd, right, up, 0.062f * S, 0.057f * S, 0.068f * S, HELM, 0.3f, SEG, true);
        useMat(MAT_RUBBER, scaleCol(HELM, 0.7f), 0, 0, 8.f, 0.2f);
        tube(PH(hx - 0.004f, hy, hc + 0.006f), PH(hx - 0.004f, hy, hc + 0.016f), 0.065f, 0.06f, 0.065f, 0.06f, scaleCol(HELM, 0.7f), 0.2f, SIDES);
        if (hq) {
            useMat(MAT_PLASTIC, rgb(44, 46, 48), 0, 0, 8.f, 0.35f);
            for (int sd = -1; sd <= 1; sd += 2) {
                rbox(PH(hx - 0.03f, hy + sd * 0.058f, hc + 0.03f), PH(hx + 0.028f, hy + sd * 0.058f, hc + 0.03f), up, right, 0.009f, 0.005f, 0.009f, 0.005f, rgb(44, 46, 48), 0.3f);   // rails
                joint(PH(hx - 0.004f, hy + sd * 0.05f, hc - 0.008f), 0.024f, 0.013f, 0.026f, scaleCol(L.vestDark, 0.8f), 0.2f);                                             // ear cups
                useMat(MAT_NONE, 0);
                r.box(PH(hx + 0.0f, hy + sd * 0.048f, hc + 0.0f), PH(hx + 0.026f, hy + sd * 0.026f, hc - 0.046f), 0.003f * S, 0.003f * S, rgb(40, 40, 38), 0.1f);       // chin strap
                useMat(MAT_PLASTIC, rgb(44, 46, 48), 0, 0, 8.f, 0.35f);
            }
            useMat(MAT_METAL, rgb(52, 54, 56), 0, 0, 8.f, 0.6f);
            rbox(PH(hx + 0.058f, hy, hc + 0.038f), PH(hx + 0.058f, hy, hc + 0.064f), right, fwd, 0.013f, 0.007f, 0.011f, 0.007f, rgb(52, 54, 56), 0.4f);           // NVG mount
            useMat(MAT_CLOTH, scaleCol(L.vestDark, 0.9f), 0, 0, 20.f, 0.05f);
            rbox(PH(hx - 0.066f, hy, hc + 0.025f), PH(hx - 0.066f, hy, hc + 0.05f), right, fwd, 0.02f, 0.008f, 0.018f, 0.008f, L.vestDark, 0.05f);                   // counterweight pouch
        }
    }
    // --- Neck, head, face, headgear -----------------------------------------------------------
    useMat(MAT_CLOTH, HEADC, 0, 0, 24.f, 0.04f);
    limb(PH(hx - 0.014f, hy * 0.8f, 0.715f - dz + hz), PH(hx - 0.008f, hy, hc - 0.03f), 0.027f, 0.025f, HEADC, 0.05f);
    uint32_t hoodC = p.gear == SG_STALKER ? tint(rgb(30, 32, 36), PART_HEAD) : HEADC;
    useMat(MAT_CLOTH, hoodC, 0, 0, 24.f, 0.04f);
    ball(PH(hx, hy, hc), 0.047f, 0.041f, 0.052f, hoodC, 0.1f);                                        // skull (balaclava)
    if (hq) {
        ball(PH(hx + 0.02f, hy, hc - 0.03f), 0.03f, 0.032f, 0.022f, hoodC, 0.05f);                    // jaw
        ball(PH(hx + 0.045f, hy, hc - 0.008f), 0.012f, 0.008f, 0.014f, hoodC, 0.05f);                 // nose
        useMat(MAT_SKIN, SKIN, 0, 0, 8.f, 0.12f);
        ball(PH(hx + 0.034f, hy, hc + 0.004f), 0.016f, 0.032f, 0.013f, SKIN, 0.12f);                   // skin round the eyes
    }

    S = p.scale;

    if (hq) {
        // shoulder straps over to the back plate
        useMat(MAT_CLOTH, VEST, 0, 0, 14.f, 0.08f);
        for (int sd = -1; sd <= 1; sd += 2) {
            V3 a = P(lean + 0.06f, hb2 + sd * 0.062f, 0.705f - dz), b = P(lean + 0.004f, hb2 + sd * 0.07f, 0.745f - dz), c = P(lean - 0.06f, hb2 + sd * 0.062f, 0.705f - dz);
            r.tube(a, b, right, up, 0.018f * S, 0.007f * S, 0.018f * S, 0.007f * S, VEST, 0.08f, 6, true, 4.f);
            r.tube(b, c, right, up, 0.018f * S, 0.007f * S, 0.018f * S, 0.007f * S, VEST, 0.08f, 6, true, 4.f);
        }
        // magazine pouches with lighter flaps and the mags' baseplates peeking out
        for (int k = -1; k <= 1; k++) {
            float a0 = lean * 0.3f + 0.092f * bulk, b0 = k * 0.05f + hb2 * 0.4f;
            useMat(MAT_CLOTH, VD, 0, 0, 20.f, 0.05f);
            rbox(P(a0, b0, 0.55f - dz), P(a0, b0, 0.612f - dz), right, fwd, 0.02f, 0.013f, 0.02f, 0.013f, VD, 0.05f);
            useMat(MAT_CLOTH, lerpCol(VD, VEST, 0.5f), 0, 0, 20.f, 0.05f);
            rbox(P(a0 + 0.002f, b0, 0.6f - dz), P(a0 + 0.002f, b0, 0.618f - dz), right, fwd, 0.022f, 0.015f, 0.022f, 0.015f, VD, 0.05f);
        }
        useMat(MAT_CLOTH, scaleCol(VD, 0.9f), 0, 0, 20.f, 0.05f);
        rbox(P(lean + 0.09f * bulk, 0.045f + hb2, 0.64f - dz), P(lean + 0.09f * bulk, 0.045f + hb2, 0.685f - dz), right, fwd, 0.026f, 0.01f, 0.026f, 0.01f, VD, 0.06f);   // admin pouch
        if (p.gear == SG_GRENADIER) {                                                             // bandolier of grenades across the chest
            useMat(MAT_METAL, rgb(70, 84, 46), 0, 0, 8.f, 0.4f);
            for (int k = 0; k < 5; k++) {
                float t = k / 4.f;
                ball(P(lean * 0.8f + 0.095f, -0.07f + 0.14f * t + hb2, 0.56f + 0.13f * t - dz), 0.018f, 0.018f, 0.022f, rgb(70, 84, 46), 0.3f);
            }
        }
        if (p.gear == SG_MEDIC) {                                                                 // white cross on the chest plate
            useMat(MAT_CLOTH, rgb(240, 240, 240), 0, 0, 20.f, 0.05f);
            slab(P(lean + 0.09f, hb2, 0.6f - dz), P(lean + 0.09f, hb2, 0.68f - dz), right, fwd, 0.012f, 0.004f, 0.012f, 0.004f, rgb(240, 240, 240), 0.2f);
            slab(P(lean + 0.09f, hb2, 0.633f - dz), P(lean + 0.09f, hb2, 0.647f - dz), right, fwd, 0.04f, 0.004f, 0.04f, 0.004f, rgb(240, 240, 240), 0.2f);
        }
        if (heavy) {                                                                              // shoulder armour and a groin flap
            useMat(MAT_METAL, VEST, 0, 0, 8.f, 0.35f);
            for (int sd = -1; sd <= 1; sd += 2) joint(P(lean, sd * 0.14f + hb2, 0.708f - dz), 0.05f, 0.045f, 0.028f, VEST, 0.25f);
            rbox(P(0.075f, 0, 0.47f - dz), P(0.08f, 0, 0.37f - dz), right, fwd, 0.05f, 0.01f, 0.04f, 0.01f, VEST, 0.15f);
        }
    }
    // plate carrier: front and back plates, a cummerbund round the sides
    useMat(MAT_CLOTH, VEST, 0, 0, 14.f, 0.08f);
    float vf = 0.066f * bulk;
    rbox(P(lean * 0.3f + vf, hb2 * 0.4f, 0.54f - dz), P(lean + vf + 0.006f, hb2, 0.708f - dz + hz), right, fwd, 0.084f * bulk, 0.02f, 0.088f * bulk, 0.02f, VEST, 0.1f);
    rbox(P(lean * 0.3f - vf - 0.008f, hb2 * 0.4f, 0.54f - dz), P(lean - vf - 0.006f, hb2, 0.705f - dz), right, fwd, 0.082f * bulk, 0.018f, 0.086f * bulk, 0.018f, VD, 0.08f);
    r.tube(P(lean * 0.25f, hb2 * 0.3f, 0.54f - dz), P(lean * 0.5f, hb2 * 0.5f, 0.615f - dz), right, fwd, 0.1f * S * bulk, 0.074f * S * bulk, 0.104f * S * bulk, 0.074f * S * bulk, VEST, 0.08f, SIDES, true, hq ? 3.5f : 2.f);
    // --- Torso: abdomen, a rounded chest, shoulders ------------------------------------------
    camo(L.uniform, PART_BODY);
    tube(P(0, 0, 0.5f - dz), P(lean * 0.6f, hb2 * 0.6f, 0.62f - dz), 0.078f * bulk, 0.054f * bulk, 0.092f * bulk, 0.058f * bulk, TU, 0.05f, SIDES);
    ball(P(lean * 0.85f, hb2 * 0.85f, 0.655f - dz + hz), 0.066f * bulk, 0.104f * bulk, 0.088f, TU, 0.05f);
    ball(P(lean, hb2, 0.712f - dz + hz), 0.048f * bulk, 0.088f * bulk, 0.032f, TU, 0.05f);           // trapezius into the neck

    // --- Pelvis, belt, gear -----------------------------------------------------------------
    if (hq) {
        useMat(MAT_METAL, rgb(150, 140, 110), 0, 0, 8.f, 0.8f);
        slab(P(0.066f * bulk, 0, 0.49f - dz), P(0.066f * bulk, 0, 0.51f - dz), right, fwd, 0.017f, 0.004f, 0.017f, 0.004f, rgb(150, 140, 110), 0.7f);   // buckle
        useMat(MAT_CLOTH, VD, 0, 0, 20.f, 0.05f);
        for (int sd = -1; sd <= 1; sd += 2)                                                       // hip pouches
            rbox(P(-0.02f, sd * 0.086f * bulk, 0.43f - dz), P(-0.02f, sd * 0.086f * bulk, 0.495f - dz), fwd, right, 0.026f, 0.014f, 0.026f, 0.014f, VD, 0.05f);
        // drop-leg holster on the right thigh with a pistol grip
        const float *hp = J.hip[1], *kn = J.knee[1];
        float h0[3] = { hp[0] + (kn[0] - hp[0]) * 0.15f, hp[1] + 0.05f, hp[2] + (kn[2] - hp[2]) * 0.15f };
        float h1[3] = { hp[0] + (kn[0] - hp[0]) * 0.55f, hp[1] + 0.05f, hp[2] + (kn[2] - hp[2]) * 0.55f };
        rbox(PJ(h0), PJ(h1), fwd, right, 0.022f, 0.012f, 0.02f, 0.012f, scaleCol(VD, 0.8f), 0.08f);
        useMat(MAT_PLASTIC, rgb(34, 34, 36), 0, 0, 8.f, 0.3f);
        rbox(PL(h0, -0.01f, 0.f, 0.f), PL(h0, -0.012f, 0.f, 0.035f), fwd, right, 0.012f, 0.009f, 0.012f, 0.009f, rgb(34, 34, 36), 0.3f);
    }

    useMat(MAT_RUBBER, scaleCol(L.boots, 0.9f), 0, 0, 8.f, 0.2f);
    r.tube(P(0, 0, 0.483f - dz), P(0, 0, 0.517f - dz), right, fwd, 0.095f * S * bulk, 0.067f * S * bulk, 0.095f * S * bulk, 0.067f * S * bulk, scaleCol(L.boots, 0.9f), 0.2f, SIDES, true, hq ? 3.f : 2.f);   // belt
    camo(L.uniformDark, PART_BODY);
    r.tube(P(0, 0, 0.415f - dz), P(0, 0, 0.51f - dz), right, fwd, 0.086f * S * bulk, 0.06f * S * bulk, 0.09f * S * bulk, 0.062f * S * bulk, TUD, 0.05f, SIDES, true, hq ? 3.f : 2.f);

    // --- Legs -----------------------------------------------------------------------------
    for (int s = 0; s < 2; s++) {
        int side = s ? 1 : -1;
        const float *hp = J.hip[s], *kn = J.knee[s], *an = J.ankle[s];
        V3 hip = PJ(hp), knee = PJ(kn), ankle = PJ(an);
        if (hq) {
            // cargo pocket on the outside of the thigh, with a flap
            float pc0[3] = { hp[0] + (kn[0] - hp[0]) * 0.55f, hp[1] + (kn[1] - hp[1]) * 0.55f + side * 0.046f, hp[2] + (kn[2] - hp[2]) * 0.55f };
            V3 td = norm(knee - hip);
            V3 pc = PJ(pc0);
            useMat(MAT_CLOTH, scaleCol(L.uniform, 0.92f), 0, 0, 22.f, 0.04f);
            rbox(pc - td * (0.042f * S), pc + td * (0.042f * S), right, fwd, 0.012f, 0.036f, 0.013f, 0.037f, U, 0.04f);
            rbox(pc - td * (0.048f * S), pc - td * (0.03f * S), right, fwd, 0.015f, 0.039f, 0.015f, 0.039f, UD, 0.04f);
        }
        // knee pad
        useMat(MAT_RUBBER, scaleCol(VD, 0.85f), 0, 0, 8.f, 0.25f);
        V3 sd = norm(ankle - knee);
        if (!sh) rbox(knee + fwd * (0.036f * S * bulk) - sd * (0.025f * S), knee + fwd * (0.036f * S * bulk) + sd * (0.04f * S), right, fwd, 0.034f * bulk, 0.012f, 0.03f * bulk, 0.011f, VD, 0.25f);
        // boot: a shaft round the ankle, the foot with a rounded toe cap, a dark sole
        float fa = an[0], hb = an[1], bz = std::max(0.f, an[2] - 0.045f);
        uint32_t bootC = tint(L.boots, PART_LIMB);
        useMat(MAT_RUBBER, bootC, 0, 0, 8.f, 0.18f);
        limb(ankle + up * (0.035f * S), P(fa, hb, bz + 0.03f), 0.035f, 0.037f, bootC, 0.15f);
        rbox(P(fa - 0.04f, hb, bz + 0.03f), P(fa + 0.06f, hb, bz + 0.026f), right, up, 0.037f, 0.032f, 0.035f, 0.024f, bootC, 0.15f);
        if (!sh) {
            ball(P(fa + 0.062f, hb, bz + 0.024f), 0.022f, 0.03f, 0.02f, bootC, 0.2f);
            useMat(MAT_RUBBER, scaleCol(L.boots, 0.45f), 0, 0, 8.f, 0.05f);
            slab(P(fa - 0.044f, hb, bz + 0.004f), P(fa + 0.086f, hb, bz + 0.004f), right, up, 0.034f, 0.006f, 0.033f, 0.006f, scaleCol(L.boots, 0.45f), 0.05f);
        }
        // thigh: wide at the hip, a bulge in front, narrowing to the knee
        float mt[3] = { hp[0] + (kn[0] - hp[0]) * 0.45f + 0.008f, hp[1] + (kn[1] - hp[1]) * 0.45f, hp[2] + (kn[2] - hp[2]) * 0.45f };
        V3 mid = PJ(mt);
        camo(L.uniform, PART_LIMB);
        r.tube(hip, mid, right, fwd, 0.05f * S * bulk, 0.055f * S * bulk, 0.047f * S * bulk, 0.051f * S * bulk, U, 0.05f, SIDES);
        r.tube(mid, knee, right, fwd, 0.047f * S * bulk, 0.051f * S * bulk, 0.037f * S * bulk, 0.039f * S * bulk, U, 0.05f, SIDES);
        // shin with a calf
        float mc[3] = { kn[0] + (an[0] - kn[0]) * 0.35f - 0.006f, kn[1] + (an[1] - kn[1]) * 0.35f, kn[2] + (an[2] - kn[2]) * 0.35f };
        V3 calf = PJ(mc);
        r.tube(knee, calf, right, fwd, 0.036f * S * bulk, 0.038f * S * bulk, 0.036f * S * bulk, 0.04f * S * bulk, UD, 0.05f, SIDES);
        r.tube(calf, ankle, right, fwd, 0.036f * S * bulk, 0.04f * S * bulk, 0.027f * S * bulk, 0.029f * S * bulk, UD, 0.05f, SIDES);
    }

    // pack (medics carry a white med bag) with a radio and an antenna
    uint32_t packC = p.gear == SG_MEDIC ? rgb(225, 225, 220) : VD;
    useMat(MAT_CLOTH, packC, 0, 0, 14.f, 0.05f);
    rbox(P(lean * 0.4f - 0.125f * bulk, hb2 * 0.4f, 0.53f - dz), P(lean - 0.125f * bulk, hb2, 0.7f - dz), right, fwd, 0.07f, 0.036f, 0.064f, 0.032f, packC, 0.06f);
    if (fine) {
        if (p.gear == SG_MEDIC) {
            useMat(MAT_CLOTH, rgb(210, 40, 40), 0, 0, 20.f, 0.05f);
            slab(P(lean - 0.165f, hb2, 0.57f - dz), P(lean - 0.165f, hb2, 0.66f - dz), right, fwd, 0.014f, 0.003f, 0.014f, 0.003f, rgb(210, 40, 40), 0.2f);
            slab(P(lean - 0.165f, hb2, 0.608f - dz), P(lean - 0.165f, hb2, 0.622f - dz), right, fwd, 0.045f, 0.003f, 0.045f, 0.003f, rgb(210, 40, 40), 0.2f);
        } else {
            useMat(MAT_PLASTIC, rgb(46, 48, 44), 0, 0, 8.f, 0.3f);
            rbox(P(lean - 0.128f, -0.045f + hb2, 0.66f - dz), P(lean - 0.128f, -0.045f + hb2, 0.745f - dz), right, fwd, 0.017f, 0.014f, 0.017f, 0.014f, rgb(46, 48, 44), 0.3f);
            useMat(MAT_NONE, 0);
            r.box(P(lean - 0.128f, -0.05f + hb2, 0.745f - dz), P(lean - 0.138f, -0.056f + hb2, 0.92f - dz), 0.003f * S, 0.0022f * S, rgb(30, 30, 30), 0.3f);
        }
    }

    useMat(MAT_NONE, 0);
    r.emissive = false;
    r.texFrame = false;
}

// A gun lying in the world (pickups): the same extruded profile, any orientation
static void drawGunAt(Raster3D &r, const WorldCam &cam, int w, float x, float y, float z, float yaw, float roll, float len)
{
    const GunModel &M = gunModel(w);
    const GunGeom3D &G3 = gunGeom(w);
    float S = len / M.length;
    V3 Fd = norm(cam.dir(std::cos(yaw), std::sin(yaw), 0.f));
    V3 side = norm(cam.dir(-std::sin(yaw), std::cos(yaw), 0.f)), up = norm(cam.dir(0, 0, 1));
    V3 Ud = norm(up * std::cos(roll) + side * std::sin(roll)), Rd = norm(cross(Fd, Ud));
    V3 C = cam.point(x, y, z);
    float hMid = (M.hMax + M.hMin) * 0.5f;
    V3 O = C - Fd * (M.length * 0.5f * S) - Ud * (hMid * S);
    auto map = [&](float u, float h, float wz) { return O + Fd * (u * S) + Ud * (h * S) + Rd * (wz * S); };
    for (int i = 0; i < M.count; i++) {
        const GunPart &part = M.parts[i];
        if (part.flags & GP_NORIM) continue;
        float width = i < G3.count ? G3.partW[i] : 4.f;
        bool glass = (part.flags & GP_GLASS) != 0;
        r.emissive = (part.flags & GP_GLOW) != 0;
        r.extrude(part.pts, part.n, width, map, Fd, Ud, Rd, part.col, glass ? 0.8f : 0.4f, glass);
        r.emissive = false;
    }
}

void MainWindow::renderProps3D(Raster3D &r, const WorldCam &cam)
{
    const Theme &T = m_env;
    auto litAt = [&](float x, float y) { return 0.12f + (T.ambient + T.sunStrength * 0.55f * sunVisAt(x, y)) * 1.0f; };
    r.alpha = 1.f;
    // Weapon pickups: the real model, slowly spinning above the ground
    for (const WeaponPickup &wp : m_gs.weaponPickups) {
        if (!wp.active || wp.type != PICKUP_WEAPON || wp.weapon < 0) continue;
        float d2 = (wp.x - m_camX) * (wp.x - m_camX) + (wp.y - m_camY) * (wp.y - m_camY);
        if (d2 > 30.f * 30.f) continue;
        float base = floorH(wp.x, wp.y);
        r.lightMul = litAt(wp.x, wp.y) * 1.15f;
        drawGunAt(r, cam, wp.weapon, wp.x, wp.y, base + 0.18f + std::sin(wp.bobPhase) * 0.03f, wp.bobPhase * 0.6f, 0.f,
                  std::max(0.22f, WEAPONS[wp.weapon].gunLen * 1.1f));
    }
    // Throwable pickups: a grenade-sized can in its colour
    for (const WeaponPickup &wp : m_gs.weaponPickups) {
        if (!wp.active || wp.type != PICKUP_THROW) continue;
        float base = floorH(wp.x, wp.y) + 0.08f + std::sin(wp.bobPhase) * 0.03f;
        r.lightMul = litAt(wp.x, wp.y) * 1.2f;
        r.box(cam.point(wp.x, wp.y, base), cam.point(wp.x, wp.y, base + 0.12f), 0.045f, 0.04f, THROWS[wp.throwKind].color, 0.4f);
        r.box(cam.point(wp.x, wp.y, base + 0.12f), cam.point(wp.x, wp.y, base + 0.15f), 0.02f, 0.02f, rgb(60, 62, 66), 0.6f);
    }
    // Sentry turrets: tripod, body, barrel
    for (const Turret &t : m_gs.turrets) {
        r.lightMul = litAt(t.x, t.y);
        V3 top = cam.point(t.x, t.y, t.z + 0.36f);
        for (int k = 0; k < 3; k++) {
            float a = t.yaw * 0.f + k * 2.094f;
            r.box(top, cam.point(t.x + std::cos(a) * 0.22f, t.y + std::sin(a) * 0.22f, t.z), 0.018f, 0.014f, rgb(50, 52, 56), 0.2f);
        }
        float cy = std::cos(t.yaw), sy = std::sin(t.yaw);
        V3 f = norm(cam.dir(cy, sy, 0)), s = norm(cam.dir(-sy, cy, 0));
        r.boxAxes(cam.point(t.x, t.y, t.z + 0.34f), cam.point(t.x, t.y, t.z + 0.52f), s, f, 0.07f, 0.09f, 0.06f, 0.08f, rgb(70, 110, 140), 0.4f);
        r.box(cam.point(t.x + cy * 0.05f, t.y + sy * 0.05f, t.z + 0.45f), cam.point(t.x + cy * 0.32f, t.y + sy * 0.32f, t.z + 0.45f), 0.016f, 0.014f, rgb(30, 30, 34), 0.5f);
        r.emissive = true;
        r.box(cam.point(t.x - cy * 0.05f, t.y - sy * 0.05f, t.z + 0.53f), cam.point(t.x - cy * 0.03f, t.y - sy * 0.03f, t.z + 0.55f), 0.012f, 0.012f,
              t.target >= 0 ? rgb(255, 60, 40) : rgb(80, 220, 255), 0.f);
        r.emissive = false;
        if (t.flash > 0.f) {
            float px, py, d;
            if (project(t.x + cy * 0.36f, t.y + sy * 0.36f, t.z + 0.45f, px, py, d) && d > 0.1f)
                m_cv.glow(int(px), int(py), std::max(3, int(0.12f * m_K / d)), rgb(255, 200, 100), 1.2f);
        }
    }
    // Medkit stations: a case with a pulsing green cross
    for (const MedStation &m : m_gs.medStations) {
        r.lightMul = litAt(m.x, m.y);
        V3 f = norm(cam.dir(1, 0, 0)), s = norm(cam.dir(0, 1, 0));
        r.boxAxes(cam.point(m.x, m.y, m.z), cam.point(m.x, m.y, m.z + 0.16f), s, f, 0.14f, 0.1f, 0.14f, 0.1f, rgb(220, 224, 228), 0.3f);
        r.emissive = true;
        uint32_t g = lerpCol(rgb(40, 160, 80), rgb(120, 255, 160), 0.5f + 0.5f * std::sin(m_time * 4.f));
        r.boxAxes(cam.point(m.x, m.y, m.z + 0.16f), cam.point(m.x, m.y, m.z + 0.175f), s, f, 0.1f, 0.03f, 0.1f, 0.03f, g, 0.f);
        r.boxAxes(cam.point(m.x, m.y, m.z + 0.16f), cam.point(m.x, m.y, m.z + 0.175f), s, f, 0.03f, 0.1f, 0.03f, 0.1f, g, 0.f);
        r.emissive = false;
        float px, py, d;
        if (project(m.x, m.y, m.z + 0.3f, px, py, d) && d > 0.1f) m_cv.glow(int(px), int(py), std::max(6, int(0.9f * m_K / d)), rgb(80, 255, 140), 0.25f);
    }
    // Projectiles with a body: rockets, grenades, thrown items, mines
    for (const Projectile &p : m_gs.projectiles) {
        if (p.kind == PJ_PLASMA || p.kind == PJ_BULLET) continue;
        r.lightMul = litAt(p.x, p.y);
        if (p.kind == PJ_ROCKET && p.throwKind < 0) {
            float l = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) + 1e-4f;
            float tx = p.x - p.vx / l * 0.18f, ty = p.y - p.vy / l * 0.18f, tz = p.z - p.vz / l * 0.18f;
            r.box(cam.point(tx, ty, tz), cam.point(p.x, p.y, p.z), 0.022f, 0.04f, rgb(80, 96, 60), 0.4f);
            float px, py, d;
            if (project(tx, ty, tz, px, py, d) && d > 0.1f) m_cv.glow(int(px), int(py), std::max(4, int(0.15f * m_K / d)), rgb(255, 170, 60), 1.3f);
            continue;
        }
        uint32_t c = p.throwKind >= 0 ? THROWS[p.throwKind].color : rgb(70, 80, 50);
        float rr = p.throwKind == TH_MINE ? 0.06f : 0.03f;
        V3 a = cam.point(p.x, p.y, p.z), b = cam.point(p.x, p.y, p.z + (p.throwKind == TH_MINE ? 0.04f : 0.06f));
        r.box(a, b, rr, rr, c, 0.3f);
        if (p.throwKind == TH_MINE || p.throwKind == TH_FRAG) {        // blinking arming light
            bool on = p.throwKind == TH_MINE ? (p.armed && int(m_time * 3) % 2 == 0) : int(m_time * 8) % 2 == 0;
            float px, py, d;
            if (on && project(p.x, p.y, p.z + 0.07f, px, py, d) && d > 0.1f && d < m_depth[size_t(clampi(int(py), 0, H - 1)) * W + clampi(int(px), 0, W - 1)] + 0.2f)
                m_cv.glow(int(px), int(py), std::max(2, int(0.05f * m_K / d)), rgb(255, 40, 30), 1.f);
        }
    }
}

void MainWindow::renderActors3D()
{
    const Player &pl = m_gs.player;
    const Theme &T = m_env;
    WorldCam cam{m_camX, m_camY, m_eye, pl.dirX, pl.dirY};
    Raster3D r{m_cv, m_depth.data()};
    r.worldZ = true;
    r.fx = (W / 2.f) / (m_planeLen * m_adsFOVMult);
    r.fy = m_K;
    r.cx = W / 2.f; r.cy = float(m_hor);
    r.nearZ = 0.2f;                                                         // bodies right at the lens are cut, never blown up
    r.fogCol = T.haze; r.fogDensity = T.fogDensity;
    float ce = std::cos(T.sunElev);
    float sunX = std::cos(SUN_AZIMUTH) * ce, sunY = std::sin(SUN_AZIMUTH) * ce, sunZ = std::sin(T.sunElev);
    r.light = norm(cam.dir(sunX, sunY, sunZ));
    r.upV = norm(cam.dir(0.f, 0.f, 1.f));
    r.skyTint = lerpCol(T.zenith, T.horizon, 0.5f);
    r.gndTint = lerpCol(T.haze, rgb(70, 60, 50), 0.6f);
    if (m_modelMask.size() != size_t(W) * H) m_modelMask.assign(size_t(W) * H, 0);
    else std::fill(m_modelMask.begin(), m_modelMask.end(), uint8_t(0));
    auto litAt = [&](float x, float y) { return 0.12f + (T.ambient + T.sunStrength * 0.55f * sunVisAt(x, y)) * 1.0f; };

    // --- Everyone with a soldier body this frame --------------------------------------
    struct Body { Soldier3D sp; float light; bool shadow; };
    std::vector<Body> bodies;
    bodies.reserve(m_gs.actors.size() + m_gs.enemies.size() + 1);
    for (const Actor &a : m_gs.actors) {
        Body b;
        if (!actorPose(a, b.sp)) continue;
        b.light = litAt(a.posX, a.posY) * (a.hitFlash > 0 ? 1.6f : 1.f);
        b.shadow = true;
        bodies.push_back(b);
    }
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive && e.corpseFade <= 0.f) continue;
        Body b;
        if (!enemyPose(i, b.sp)) continue;
        if ((e.x - m_camX) * (e.x - m_camX) + (e.y - m_camY) * (e.y - m_camY) > 45.f * 45.f) continue;
        b.light = litAt(e.x, e.y) * 1.08f * (e.alive && e.hitFlash > 0.f ? 1.25f : 1.f);
        b.shadow = b.sp.alpha > 0.5f;
        bodies.push_back(b);
    }
    // your own body: drawn in third person (and during the kill cam); its shadow always
    bool ownBody = false;
    Soldier3D own;
    float ownAlpha = 0.f;
    {
        float ax, ay; aimPoint(ax, ay);
        own.x = pl.posX; own.y = pl.posY; own.z = pl.footZ;
        float camOff = std::atan((2.f * ax / W - 1.f) * m_planeLen * m_adsFOVMult);
        own.yaw = m_kcBody ? m_kcBodyYaw : std::atan2(pl.dirY, pl.dirX) + camOff * 0.8f;
        own.aimPitch = m_kcBody ? 0.f : clampf(std::atan((m_hor - ay) / m_K), -1.f, 1.f);
        own.walk = pl.bobPhase * 0.5f;
        own.move = std::min(1.f, std::sqrt(pl.velX * pl.velX + pl.velY * pl.velY) / 3.5f);
        own.weapon = pl.curWeapon;
        own.look = lookForClass(m_charChoice);
        own.sprint = pl.sprinting;
        own.band = rgb(60, 140, 255);
        if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->teamOf(m_net->localPlayerId()) == 0) own.band = rgb(230, 60, 50);
        own.recoil = std::min(1.f, m_kick);
        own.crouch = m_crouchT;
        own.lean = m_leanT;
        float closeFade = clampf((m_boomCur - 0.35f) / 0.5f, 0.f, 1.f);
        ownAlpha = m_kcBody ? 1.f : std::min(m_boomT * 1.5f, 1.f) * closeFade;
        ownBody = (m_boomT > 0.02f || m_kcBody) && ownAlpha > 0.05f;
    }

    // feet on the terrain (steps, slopes) for every standing body near enough to notice
    auto ground = [&](float x, float y) { return floorH(x, y); };
    for (Body &b : bodies) {
        b.sp.t = m_time;
        if (b.sp.fall <= 0.f && (b.sp.x - m_camX) * (b.sp.x - m_camX) + (b.sp.y - m_camY) * (b.sp.y - m_camY) < 20.f * 20.f) plantFeet(b.sp, ground);
    }
    own.t = m_time;
    plantFeet(own, ground);

    // --- View culling: bodies behind the camera or far off the sides are skipped -------------
    auto inView = [&](const Soldier3D &sp, float margin) {
        V3 c = cam.point(sp.x, sp.y, sp.z + 0.5f);
        if (c.z < -1.2f) return false;                                     // behind (a fallen body may still reach forward)
        if (c.z < 0.3f) return true;
        float sx = r.cx + r.fx * c.x / c.z;
        return sx > -margin * r.fx / c.z - 40.f && sx < W + margin * r.fx / c.z + 40.f;
    };
    for (Body &b : bodies) {
        // fade out a body the camera is standing over or pressed against
        float cxw = b.sp.x, cyw = b.sp.y;
        if (b.sp.fall > 0.f) {                                            // a corpse: its middle lies half a body from the feet
            float fy = b.sp.yaw + b.sp.fallRel;
            cxw += std::cos(fy) * 0.45f * b.sp.fall; cyw += std::sin(fy) * 0.45f * b.sp.fall;
        }
        float dc = std::sqrt((cxw - m_camX) * (cxw - m_camX) + (cyw - m_camY) * (cyw - m_camY));
        float nearK = clampf((dc - (b.sp.fall > 0.f ? 0.32f : 0.3f)) / 0.4f, 0.f, 1.f);
        if (nearK < 1.f) b.sp.alpha = std::min(b.sp.alpha, nearK);
        if (b.sp.alpha <= 0.02f) { b.sp.alpha = -1.f; b.shadow = false; continue; }
        b.shadow = b.shadow && inView(b.sp, 1.6f);                       // a shadow can reach in from just off-screen
        if (b.sp.alpha >= 0.f && !inView(b.sp, 0.9f)) b.sp.alpha = -1.f;   // marks: don't draw the body
    }

    // --- Shadow pass: every body flattened onto the ground along the sun -----------------
    if (m_shadowStamp.size() != size_t(W) * H) { m_shadowStamp.assign(size_t(W) * H, 0); m_shadowId = 0; }
    r.shadowMode = true;
    r.stamp = m_shadowStamp.data();
    float lz = std::max(0.35f, sunZ);
    auto castShadow = [&](const Soldier3D &sp) {
        if (++m_shadowId == 0) { std::fill(m_shadowStamp.begin(), m_shadowStamp.end(), 0); m_shadowId = 1; }
        r.stampId = m_shadowId;
        float sunVis = sunVisAt(sp.x, sp.y);
        r.shadowK = 0.14f + 0.36f * clampf(T.sunStrength * sunVis, 0.f, 1.f);
        ShadowProj sh{sp.z + 0.01f, sunX, sunY, lz, this};
        drawSoldierShadow(r, cam, sp, sh);
    };
    for (const Body &b : bodies) if (b.shadow) castShadow(b.sp);
    if (!m_killcamActive || m_kcBody) castShadow(own);                        // your shadow, even in first person
    r.shadowMode = false;

    // --- Bodies -------------------------------------------------------------------------
    r.mask = m_modelMask.data();
    for (const Body &b : bodies) {
        if (b.sp.alpha < 0.f) continue;                                    // culled
        r.lightMul = b.light;
        r.alpha = b.sp.alpha;
        drawSoldier(r, cam, b.sp);
    }
    r.alpha = 1.f;
    // muzzle flashes
    for (const Actor &a : m_gs.actors) {
        if (!a.alive || a.shootAnim <= 0.06f) continue;
        float mx = a.posX + a.dirX * 0.5f - a.dirY * 0.06f, my = a.posY + a.dirY * 0.5f + a.dirX * 0.06f;
        float px, py, d;
        if (project(mx, my, a.footZ + 0.64f, px, py, d) && d > 0.1f && d < m_depth[size_t(clampi(int(py), 0, H - 1)) * W + clampi(int(px), 0, W - 1)] + 0.3f)
            m_cv.glow(int(px), int(py), std::max(4, int(0.14f * m_K / d)), rgb(255, 190, 90), 1.2f);
    }
    for (const Enemy &e : m_gs.enemies) {
        if (!e.alive || e.shootAnim <= 0.f || !soldierEnemy(e) || e.type == ET_STALKER) continue;
        float c = std::cos(e.yaw), s = std::sin(e.yaw);
        float mx = e.x + c * 0.55f * e.scale - s * 0.06f, my = e.y + s * 0.55f * e.scale + c * 0.06f;
        float px, py, d;
        if (project(mx, my, floorH(e.x, e.y) + 0.64f * e.scale, px, py, d) && d > 0.1f && d < m_depth[size_t(clampi(int(py), 0, H - 1)) * W + clampi(int(px), 0, W - 1)] + 0.3f)
            m_cv.glow(int(px), int(py), std::max(4, int(0.15f * m_K / d)), rgb(255, 190, 90), 1.3f);
    }
    // helmets knocked off by headshots
    for (const LooseHelmet &h : m_gs.helmets) {
        r.lightMul = litAt(h.x, h.y);
        r.alpha = std::min(1.f, h.life / 1.f);
        float ca = std::cos(h.ang), sa = std::sin(h.ang);
        V3 e1 = norm(cam.dir(ca, 0.f, sa)), e2 = norm(cam.dir(0.f, 1.f, 0.f)), e3 = norm(cam.dir(-sa, 0.f, ca));
        V3 c = cam.point(h.x, h.y, h.z);
        r.ellipsoid(c, e1, e2, e3, h.hat ? 0.05f : 0.06f, h.hat ? 0.047f : 0.055f, h.hat ? 0.05f : 0.066f, h.color, 0.3f, 10, true);
    }
    r.alpha = 1.f;

    if (ownBody) {
        r.lightMul = litAt(pl.posX, pl.posY) * (pl.damageFlash > 0.3f ? 1.5f : 1.f);
        r.alpha = ownAlpha;
        drawSoldier(r, cam, own);
        r.alpha = 1.f;
    }
    smoothModelEdges(r.mx0, r.my0, r.mx1, r.my1);
    r.mask = nullptr;

    renderProps3D(r, cam);
}

// Anti-aliasing for the 3D bodies: pixels on a model's silhouette are blended
// with their background neighbours, and inside the model strong colour steps
// (where one part meets another) are softened. Only the box the models covered
// this frame is scanned.
void MainWindow::smoothModelEdges(int x0, int y0, int x1, int y1)
{
    if (x1 < x0 || y1 < y0) return;
    x0 = std::max(1, x0 - 1); y0 = std::max(1, y0 - 1); x1 = std::min(W - 2, x1 + 1); y1 = std::min(H - 2, y1 + 1);
    const uint8_t *mk = m_modelMask.data();
    uint32_t *px = m_pixels;
    auto luma = [](uint32_t c) { return (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 8; };
    // filter from a copy so every pixel reads unmodified neighbours
    std::vector<uint32_t> src(size_t(x1 - x0 + 3) * size_t(y1 - y0 + 3));
    int sw = x1 - x0 + 3;
    for (int y = y0 - 1; y <= y1 + 1; y++) std::copy(px + size_t(y) * W + (x0 - 1), px + size_t(y) * W + (x1 + 2), src.begin() + size_t(y - y0 + 1) * sw);
    auto S = [&](int x, int y) { return src[size_t(y - y0 + 1) * sw + (x - x0 + 1)]; };
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int i = y * W + x;
            bool m = mk[i] != 0;
            bool edge = m != (mk[i - 1] != 0) || m != (mk[i + 1] != 0) || m != (mk[i - W] != 0) || m != (mk[i + W] != 0);
            uint32_t c = S(x, y), l = S(x - 1, y), rr = S(x + 1, y), u = S(x, y - 1), d = S(x, y + 1);
            if (!edge) {
                if (!m) continue;
                int lc = luma(c), dmax = std::max({std::abs(luma(l) - lc), std::abs(luma(rr) - lc), std::abs(luma(u) - lc), std::abs(luma(d) - lc)});
                if (dmax < 40) continue;                                  // flat area: keep it sharp
            }
            // 4-neighbour tent filter: half the pixel, half its neighbours
            int r2 = (R(c) * 4 + R(l) + R(rr) + R(u) + R(d)) >> 3, g2 = (G(c) * 4 + G(l) + G(rr) + G(u) + G(d)) >> 3, b2 = (B(c) * 4 + B(l) + B(rr) + B(u) + B(d)) >> 3;
            px[i] = 0xFF000000u | (uint32_t(r2) << 16) | (uint32_t(g2) << 8) | uint32_t(b2);
        }
    }
}
