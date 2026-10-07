// =============================================================================
//  render_actors.cpp  -  3D soldiers standing in the world
//
//  Your own body in third person and every squad bot are low-poly 3D
//  soldiers: boxes for the limbs, torso, vest, pack and helmet, plus the same
//  extruded gun profiles as the first-person weapon. They are transformed
//  into the camera frame used by the terrain marcher and drawn by the shared
//  triangle rasterizer (raster3d.h) against the world z-buffer, so hills and
//  walls hide them pixel by pixel. Lit by the sun with the light-map shadow.
// =============================================================================
#include "mainwindow.h"
#include "gunmodels.h"
#include "raster3d.h"
#include "gunbuild.h"
#include <cmath>
#include <algorithm>

using namespace rs;

namespace {
const float PI = 3.14159265f;

// Pose of one soldier for this frame
struct Soldier3D {
    float x, y, z;          // feet (world)
    float yaw;              // facing angle
    float aimPitch;         // radians, + = aiming up
    float walk;             // walk-cycle phase (radians)
    float move;             // 0..1 how fast it is moving
    float fall = 0.f;       // 0 standing .. 1 lying on its back
    int   weapon = WPN_RIFLE;
    SoldierLook look;
    float recoil = 0.f;     // 0..1 kick after a shot
};

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

// Build and rasterize one soldier. Local frame: a = forward, b = right, c = up.
static void drawSoldier(Raster3D &r, const WorldCam &cam, const Soldier3D &p)
{
    const float fx = std::cos(p.yaw), fy = std::sin(p.yaw);   // forward
    const float sx = -fy, sy = fx;                            // right
    const float th = p.fall * PI * 0.5f, ct = std::cos(th), st = std::sin(th);
    // local (a, b, c) -> camera space; falling rotates the body backwards about the feet
    auto P = [&](float a, float b, float c) {
        float a2 = a * ct - c * st, c2 = a * st + c * ct + 0.04f * p.fall;
        return cam.point(p.x + fx * a2 + sx * b, p.y + fy * a2 + sy * b, p.z + c2);
    };
    auto D = [&](float a, float b, float c) {
        float a2 = a * ct - c * st, c2 = a * st + c * ct;
        return norm(cam.dir(fx * a2 + sx * b, fy * a2 + sy * b, c2));
    };
    const SoldierLook &L = p.look;
    V3 fwd = D(1, 0, 0), right = D(0, 1, 0), up = D(0, 0, 1);

    // --- Legs: thigh swings with the walk cycle, shin bends on the back swing ------
    for (int side = -1; side <= 1; side += 2) {
        float ph = p.walk + (side > 0 ? PI : 0.f);
        float swing = std::sin(ph) * 0.55f * p.move;
        float bend = (0.5f - 0.5f * std::cos(ph)) * 0.75f * p.move;
        float hb = 0.05f * side;
        V3 hip = P(0.f, hb, 0.47f);
        float ka = std::sin(swing) * 0.22f, kc = 0.47f - std::cos(swing) * 0.22f;
        V3 knee = P(ka, hb, kc);
        float sa = swing - bend;
        float fa = ka + std::sin(sa) * 0.22f, fc = std::max(0.035f, kc - std::cos(sa) * 0.22f);
        V3 foot = P(fa, hb, fc);
        r.box(hip, knee, 0.042f, 0.036f, L.uniform, 0.05f);
        r.box(knee, foot, 0.034f, 0.03f, L.uniformDark, 0.05f);
        r.box(P(fa - 0.03f, hb, fc - 0.01f), P(fa + 0.07f, hb, fc - 0.01f), 0.032f, 0.03f, L.boots, 0.1f);   // boot
    }

    // --- Pelvis, torso, vest, pack ----------------------------------------------------
    float lean = 0.03f + 0.02f * p.move;
    r.boxAxes(P(0, 0, 0.43f), P(0, 0, 0.51f), right, fwd, 0.08f, 0.05f, 0.08f, 0.05f, L.uniformDark, 0.05f);
    r.boxAxes(P(0, 0, 0.49f), P(lean, 0, 0.73f), right, fwd, 0.08f, 0.05f, 0.095f, 0.055f, L.uniform, 0.05f);
    r.boxAxes(P(0.004f, 0, 0.53f), P(lean + 0.004f, 0, 0.71f), right, fwd, 0.088f, 0.062f, 0.1f, 0.066f, L.vest, 0.12f);
    r.boxAxes(P(-0.07f, 0, 0.52f), P(lean - 0.07f, 0, 0.70f), right, fwd, 0.065f, 0.04f, 0.07f, 0.045f, L.vestDark, 0.08f);
    // --- Neck, head, helmet, goggles --------------------------------------------------
    float hx = lean + 0.01f;
    r.box(P(hx, 0, 0.72f), P(hx, 0, 0.77f), 0.025f, 0.025f, L.skin, 0.05f);
    r.boxAxes(P(hx, 0, 0.765f), P(hx, 0, 0.855f), right, fwd, 0.042f, 0.045f, 0.042f, 0.045f, L.mask, 0.1f);
    r.boxAxes(P(hx - 0.005f, 0, 0.82f), P(hx - 0.005f, 0, 0.885f), right, fwd, 0.054f, 0.058f, 0.046f, 0.05f, L.helmet, 0.25f);
    r.boxAxes(P(hx + 0.046f, 0, 0.805f), P(hx + 0.046f, 0, 0.83f), right, fwd, 0.034f, 0.006f, 0.034f, 0.006f, L.lens, 0.9f);

    // --- Arms + gun: everything above the shoulders pivots with the aim pitch ---------
    const float shZ = 0.69f;
    float pc = std::cos(p.aimPitch), ps = std::sin(p.aimPitch);
    auto aimRot = [&](float a, float b, float c) {            // rotate about the shoulder line
        float da = a, dc = c - shZ;
        return P(lean + da * pc - dc * ps, b, shZ + da * ps + dc * pc);
    };
    bool oneHand = isMelee(p.weapon);
    bool pistol = WEAPONS[p.weapon].arch == VA_PISTOL;
    float kick = -0.03f * p.recoil;
    // hands (local, before the pitch rotation)
    float rA = pistol ? 0.27f : 0.13f, rB = pistol ? 0.03f : 0.06f, rC = pistol ? 0.71f : 0.63f;
    float lA = pistol ? 0.27f : 0.29f, lB = pistol ? 0.0f : -0.005f, lC = pistol ? 0.70f : 0.655f;
    if (oneHand) { rA = 0.2f; rB = 0.1f; rC = 0.56f; }
    V3 rHand = aimRot(rA + kick, rB, rC), lHand = aimRot(lA + kick, lB, lC);
    V3 rSh = P(lean, 0.1f, shZ), lSh = P(lean, -0.1f, shZ);
    auto arm = [&](V3 sh, V3 hand, float outward) {
        V3 mid = (sh + hand) * 0.5f;
        V3 elbow = mid - up * 0.07f + right * outward;
        r.box(sh, elbow, 0.033f, 0.029f, L.uniform, 0.05f);
        r.box(elbow, hand, 0.028f, 0.024f, L.uniformAlt, 0.05f);
        r.box(hand - fwd * 0.012f, hand + fwd * 0.025f, 0.024f, 0.022f, L.gloves, 0.1f);
    };
    arm(rSh, rHand, 0.04f);
    if (oneHand) arm(lSh, P(0.05f, -0.12f, 0.5f), -0.03f);
    else arm(lSh, lHand, -0.05f);

    // Gun: extruded profile, grip in the right hand, barrel along the aim direction
    const GunModel &M = gunModel(p.weapon);
    const GunGeom3D &G3 = gunGeom(p.weapon);
    float S = WEAPONS[p.weapon].gunLen / M.length;
    V3 Fd = norm(fwd * pc + up * ps), Ud = norm(up * pc - fwd * ps), Rd = right;
    V3 O = rHand - Fd * (M.gripU * S) - Ud * (M.gripH * S);
    auto map = [&](float u, float h, float wz) { return O + Fd * (u * S) + Ud * (h * S) + Rd * (wz * S); };
    for (int i = 0; i < M.count; i++) {
        const GunPart &part = M.parts[i];
        if (part.flags & GP_NORIM) continue;                  // tiny recessed details vanish at this size
        float width = i < G3.count ? G3.partW[i] : 4.f;
        bool glass = (part.flags & GP_GLASS) != 0;
        r.emissive = (part.flags & GP_GLOW) != 0;
        r.extrude(part.pts, part.n, width, map, Fd, Ud, Rd, part.col, glass ? 0.8f : 0.35f, glass);
        r.emissive = false;
    }
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
    const Theme &T = THEMES[m_theme];
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
    const Theme &T = THEMES[m_theme];
    WorldCam cam{m_camX, m_camY, m_eye, pl.dirX, pl.dirY};
    Raster3D r{m_cv, m_depth.data()};
    r.worldZ = true;
    r.fx = (W / 2.f) / (m_planeLen * m_adsFOVMult);
    r.fy = m_K;
    r.cx = W / 2.f; r.cy = float(m_hor);
    r.nearZ = 0.05f;
    r.fogCol = T.haze; r.fogDensity = T.fogDensity;
    float ce = std::cos(T.sunElev);
    r.light = norm(cam.dir(std::cos(SUN_AZIMUTH) * ce, std::sin(SUN_AZIMUTH) * ce, std::sin(T.sunElev)));
    auto litAt = [&](float x, float y) { return 0.12f + (T.ambient + T.sunStrength * 0.55f * sunVisAt(x, y)) * 1.0f; };

    // Squad bots and other players
    for (const Actor &a : m_gs.actors) {
        if (!a.alive && a.respawnT <= 0.f) continue;
        Soldier3D sp;
        sp.x = a.posX; sp.y = a.posY; sp.z = a.footZ;
        sp.yaw = std::atan2(a.dirY, a.dirX);
        sp.aimPitch = 0.f;
        sp.walk = a.walkPhase;
        sp.move = std::min(1.f, std::sqrt(a.velX * a.velX + a.velY * a.velY) / 3.f);
        sp.fall = a.alive ? 0.f : 1.f;
        sp.weapon = a.curWeapon;
        sp.look = a.team == 0 ? SQUAD_LOOK : ENEMY_LOOK;
        sp.recoil = std::min(1.f, a.shootAnim / 0.12f);
        r.lightMul = litAt(a.posX, a.posY) * (a.hitFlash > 0 ? 1.6f : 1.f);
        r.alpha = 1.f;
        drawSoldier(r, cam, sp);
        // muzzle flash
        if (a.shootAnim > 0.06f) {
            float mx = a.posX + a.dirX * 0.5f - a.dirY * 0.06f, my = a.posY + a.dirY * 0.5f + a.dirX * 0.06f;
            float px, py, d;
            if (project(mx, my, a.footZ + 0.64f, px, py, d) && d > 0.1f && d < m_depth[size_t(clampi(int(py), 0, H - 1)) * W + clampi(int(px), 0, W - 1)] + 0.3f)
                m_cv.glow(int(px), int(py), std::max(4, int(0.14f * m_K / d)), rgb(255, 190, 90), 1.2f);
        }
    }

    renderProps3D(r, cam);

    // Your own body in third person; it fades out when the camera is pushed in close
    if (m_boomT > 0.02f || m_kcBody) {
        float closeFade = clampf((m_boomCur - 0.35f) / 0.5f, 0.f, 1.f);
        float alpha = m_kcBody ? 1.f : std::min(m_boomT * 1.5f, 1.f) * closeFade;
        if (alpha > 0.05f) {
            float ax, ay; aimPoint(ax, ay);
            Soldier3D sp;
            sp.x = pl.posX; sp.y = pl.posY; sp.z = pl.footZ;
            float camOff = std::atan((2.f * ax / W - 1.f) * m_planeLen * m_adsFOVMult);
            sp.yaw = m_kcBody ? m_kcBodyYaw : std::atan2(pl.dirY, pl.dirX) + camOff * 0.8f;
            sp.aimPitch = m_kcBody ? 0.f : clampf(std::atan((m_hor - ay) / m_K), -1.f, 1.f);
            sp.walk = pl.bobPhase * 0.5f;
            sp.move = std::min(1.f, std::sqrt(pl.velX * pl.velX + pl.velY * pl.velY) / 3.5f);
            sp.weapon = pl.curWeapon;
            sp.look = lookForClass(m_charChoice);
            sp.recoil = std::min(1.f, m_kick);
            r.lightMul = litAt(pl.posX, pl.posY) * (pl.damageFlash > 0.3f ? 1.5f : 1.f);
            r.alpha = alpha;
            drawSoldier(r, cam, sp);
        }
    }
}
