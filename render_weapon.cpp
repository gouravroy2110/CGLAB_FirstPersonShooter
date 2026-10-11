// =============================================================================
//  render_weapon.cpp  -  First-person weapon, hands, optics
//
//  The gun is a real 3D model: every side-profile polygon in gunmodels.h is
//  extruded to its thickness (GUN_GEOM3D) into a solid block, placed in camera
//  space by a forward/up/right frame aimed at the crosshair, then:
//    back-face culling -> Sutherland-Hodgman near-plane clipping ->
//    perspective projection -> flat Lambert + Blinn specular shading ->
//    edge-function triangle fill with a per-pixel z-buffer (1/z).
//  Forearms are tapered 3D boxes.
//
//  Animations: walk bob, mouse-lag sway, recoil kick + muzzle climb, slide /
//  pump cycling, magazine-out reload, weapon-swap drop, sprint carry pose.
// =============================================================================
#include "mainwindow.h"
#include "gunmodels.h"
#include "raster3d.h"
#include "gunbuild.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static inline float smoothstep01(float x) { x = clampf(x, 0.f, 1.f); return x * x * (3 - 2 * x); }

namespace {
// A tapered limb: quad from (ax,ay) width wa to (bx,by) width wb, Gouraud
void limb(Canvas &cv, float ax, float ay, float wa, float bx, float by, float wb, uint32_t ca, uint32_t cb)
{
    float dx = bx - ax, dy = by - ay, L = std::sqrt(dx * dx + dy * dy);
    if (L < 1) return;
    float nx = -dy / L, ny = dx / L;
    float xy[8] = { ax + nx * wa, ay + ny * wa, bx + nx * wb, by + ny * wb, bx - nx * wb, by - ny * wb, ax - nx * wa, ay - ny * wa };
    uint32_t cols[4] = { scaleCol(ca, 1.25f), scaleCol(cb, 1.25f), scaleCol(cb, 0.7f), scaleCol(ca, 0.7f) };
    cv.poly(xy, cols, 4);
}

using ViewModelRaster = Raster3D;

bool isWoodOrCloth(uint32_t c) { return R(c) > G(c) + 20; }
} // namespace

void MainWindow::renderViewModel()
{
    const Player &pl = m_gs.player;
    const int w = pl.curWeapon;
    if (w < 0) return;
    if (effectiveScope(w) && m_adsT > 0.6f) return;                     // scope overlay replaces the gun
    const GunModel &M = gunModel(w);
    const GunGeom3D &G3 = gunGeom(w);
    const GunMounts &MT = gunMounts(w);
    const WeaponDef &WD = WEAPONS[w];
    const Loadout &LD = pl.load[w];
    const ViewArch arch = WD.arch;
    const bool melee = isMelee(w);
    const CharProfile &cp = CHAR_PROFILES[m_charChoice];
    const float S = G3.scale;                                           // metres per gun unit

    // --- Animation state --------------------------------------------------------
    bool moving = isPlayerMoving();
    float bobAmp = moving ? (pl.sprinting ? 1.6f : 1.f) : 0.f;
    float bx = std::sin(pl.bobPhase) * 9.f * bobAmp, by = std::fabs(std::cos(pl.bobPhase)) * 7.f * bobAmp;
    float breathe = std::sin(m_time * 1.7f) * 2.2f;
    float rl = 0, rp = 0;
    if (pl.reloadTimer > 0) { rp = 1.f - pl.reloadTimer / std::max(0.01f, m_reloadTotal); rl = smoothstep01(rp / 0.18f) * (1.f - smoothstep01((rp - 0.82f) / 0.18f)); }
    float drop = 0;
    if (pl.switchTimer > 0) { float s = pl.switchTimer / 0.4f; drop = s > 0.5f ? (1.f - s) * 2.f : s * 2.f; }
    float spr = m_sprintT;
    float aimHide = effectiveScope(w) ? 0.f : m_adsT;                  // reflex weapons lower as the sight rises

    // --- Hand position (camera space, metres) and orientation ---------------------
    const V3 gripPos[4] = { {0.15f, 0.20f, 0.31f}, {0.13f, 0.19f, 0.33f}, {0.175f, 0.27f, 0.38f}, {0.17f, 0.27f, 0.38f} };
    V3 Hg = gripPos[arch];
    if (w == WPN_ROCKET) { Hg.x += 0.02f; Hg.y -= 0.05f; }               // launcher rides on the shoulder
    Hg.x += (bx + m_swayX * 0.7f) * 0.0007f;
    Hg.y += (by + breathe + m_swayY * 0.7f) * 0.0007f;
    Hg.y += (drop + aimHide) * 0.32f;
    Hg.x -= 0.02f * spr; Hg.y += 0.05f * spr;
    Hg.y += 0.03f * rl;
    Hg.z -= 0.035f * m_kick; Hg.y += 0.008f * m_kick;
    if (effectiveScope(w) && m_adsT > 0) Hg = Hg + (V3{0.0f, 0.11f, 0.27f} - Hg) * m_adsT;
    float yaw = 0.10f + 0.55f * spr + 0.35f * rl;                       // + = muzzle swings left
    float pitch = 0.03f + 0.11f * m_kick - 0.35f * spr + 0.15f * rl;
    float roll = -0.06f + 0.05f * m_kick + 0.40f * spr + 0.60f * rl;

    // Inspect (H): turn the gun to show its side, then back
    if (m_inspectT > 0.f) {
        float t = 1.f - m_inspectT / 2.2f, k = std::sin(t * 3.14159f);
        yaw += 1.15f * k; roll += 0.35f * std::sin(t * 6.2832f); pitch += 0.12f * k;
        Hg.x -= 0.07f * k; Hg.z += 0.03f * k; Hg.y -= 0.02f * k;
    }
    // Quick melee: bash forward with the gun
    if (m_quickMeleeT > 0.f && !melee) {
        float k = std::sin((1.f - m_quickMeleeT / 0.45f) * 3.14159f);
        Hg.z += 0.10f * k; Hg.x -= 0.05f * k; roll -= 0.5f * k; pitch -= 0.2f * k;
    }
    // Charging weapons tremble as they build up
    if (pl.charge > 0.f) { Hg.x += (std::sin(m_time * 71.f) * 0.002f) * pl.charge; Hg.y += (std::cos(m_time * 63.f) * 0.002f) * pl.charge; }

    float thrust = 0, slash = 0, kt = 0;
    if (melee && m_actionT > 0.f) {
        kt = 1.0f - m_actionT;
        if (m_knifeSwing == 1 && w != WPN_AXE && w != WPN_MACHETE) { thrust = std::sin(kt * 3.14159f); roll -= 0.25f * thrust; pitch += 0.08f * thrust; }
        else {
            slash = std::sin(kt * 3.14159f);
            Hg.x += std::cos(kt * 3.14159f) * 0.10f * slash - 0.04f * slash;
            Hg.y -= std::sin(kt * 3.14159f) * 0.06f * slash;
            roll += (kt - 0.5f) * 1.6f * slash; yaw += (0.5f - kt) * 0.8f * slash;
        }
    }
    // Barrel points at the crosshair (converging a few metres ahead)
    V3 aim = norm(V3{(m_aimX + m_swayX * 0.5f - W / 2.f) / ViewModelRaster::F, (m_aimY + m_swayY * 0.5f - H / 2.f) / ViewModelRaster::F, 1.f});
    V3 Fd = norm(aim * 3.0f - Hg);
    V3 Ud = norm(V3{0, -1, 0} - Fd * dot(V3{0, -1, 0}, Fd));
    V3 Rd = norm(cross(Fd, Ud));
    Fd = rotate(Fd, Ud, yaw);   Rd = rotate(Rd, Ud, yaw);              // yaw about up
    Fd = rotate(Fd, Rd, pitch); Ud = rotate(Ud, Rd, pitch);           // pitch about right (positive = muzzle up)
    Ud = rotate(Ud, Fd, roll);  Rd = rotate(Rd, Fd, roll);             // roll about the barrel
    if (thrust > 0) Hg = Hg + Fd * (0.16f * thrust);
    const V3 O = Hg - Fd * (M.gripU * S) - Ud * (M.gripH * S);         // gun-space origin
    auto map = [&](float u, float h, float wz) { return O + Fd * (u * S) + Ud * (h * S) + Rd * (wz * S); };

    // --- Moving parts -----------------------------------------------------------------
    float actionU = 0;
    if (arch == VA_PISTOL && w != WPN_REVOLVER) actionU = -7.f * m_actionT;
    if (w == WPN_SHOTGUN) { float t = 1.f - m_actionT; actionU = t > 0.15f && t < 0.85f ? -11.f * std::sin((t - 0.15f) / 0.7f * 3.14159f) : 0.f; }
    if (WD.mode == FM_BOLT) { float t = 1.f - m_actionT; actionU = t > 0.3f && t < 0.9f ? -10.f * std::sin((t - 0.3f) / 0.6f * 3.14159f) : 0.f; }
    float magH = 0; bool magHidden = WD.cls == WC_LAUNCHER && w == WPN_ROCKET && pl.mag[w] == 0;
    if (pl.reloadTimer > 0 && WD.reload == RM_MAG) {
        if (rp < 0.2f) magH = 0;
        else if (rp < 0.42f) magH = -60.f * (rp - 0.2f) / 0.22f;
        else if (rp < 0.58f) magHidden = true;
        else if (rp < 0.8f) magH = -45.f * (1.f - (rp - 0.58f) / 0.22f);
    }

    m_vmDepth.assign(size_t(W) * H, 0.f);
    ViewModelRaster vr{m_cv, m_vmDepth.data()};

    auto drawPart = [&](const GunPart &part, float width, float du, float dh, bool hand) {
        float uh[12];
        for (int k = 0; k < part.n; k++) { uh[2 * k] = part.pts[2 * k] + du; uh[2 * k + 1] = part.pts[2 * k + 1] + dh; }
        bool glass = (part.flags & GP_GLASS) != 0;
        float specK = hand ? 0.12f : glass ? 0.8f : isWoodOrCloth(part.col) ? 0.12f : 0.45f;
        uint32_t col = part.col;
        if (part.flags & GP_GLOW) {                                     // energy parts pulse; hotter as heat / charge rises
            float pulse = 0.8f + 0.2f * std::sin(m_time * 6.f) + 0.6f * pl.charge;
            col = pl.overheatT > 0.f ? rgb(255, 90, 40) : lerpCol(part.col, rgb(255, 255, 255), clampf(pulse - 0.8f, 0.f, 0.6f));
            vr.emissive = true;
        }
        vr.extrude(uh, part.n, width, map, Fd, Ud, Rd, col, specK, glass);
        vr.emissive = false;
    };
    // Attachments are real geometry on the rail, muzzle and under-barrel
    auto attBox = [&](float u0, float h0, float u1, float h1, uint32_t col, float width, uint8_t flags) {
        GunPart p{}; p.n = 4; p.col = col; p.flags = flags;
        float pts[8] = { u0, h1, u1, h1, u1, h0, u0, h0 };
        for (int k = 0; k < 8; k++) p.pts[k] = pts[k];
        drawPart(p, width, 0.f, 0.f, false);
    };

    // --- Arms (3D boxes): elbows sit below / behind the camera -----------------------
    V3 wr = map(M.rWristU, M.rWristH, 0.f);
    V3 er = wr + V3{0.13f, 0.24f, -0.20f};
    vr.box(er, wr, 0.050f, 0.030f, cp.sleeve, 0.08f);
    vr.box(wr + (er - wr) * 0.16f, wr + (er - wr) * 0.02f, 0.034f, 0.031f, cp.sleeveHi, 0.08f);
    if (arch == VA_KNIFE) {
        V3 lw{-0.14f + bx * 0.0004f, 0.23f + (drop + aimHide) * 0.3f, 0.36f};
        V3 le = lw + V3{-0.08f, 0.24f, -0.16f};
        vr.box(le, lw, 0.05f, 0.032f, cp.sleeve, 0.08f);
        vr.box(lw, lw + V3{0.02f, -0.03f, 0.05f}, 0.034f, 0.03f, GLOVE, 0.1f);
    } else {
        V3 wl = map(M.lWristU + (w == WPN_SHOTGUN ? actionU : 0.f), M.lWristH, 0.f);
        V3 el = wl + (arch == VA_PISTOL ? V3{-0.06f, 0.24f, -0.16f} : V3{-0.12f, 0.26f, -0.26f});
        vr.box(el, wl, 0.050f, 0.029f, cp.sleeve, 0.08f);
        vr.box(wl + (el - wl) * 0.16f, wl + (el - wl) * 0.02f, 0.033f, 0.030f, cp.sleeveHi, 0.08f);
    }

    // --- Gun body ---------------------------------------------------------------------
    for (int i = 0; i < M.count; i++) {
        const GunPart &part = M.parts[i];
        if ((part.flags & GP_MAG) && magHidden) continue;
        float width = i < G3.count ? G3.partW[i] : 4.f;
        float hammer = (part.flags & GP_HAMMER) ? -3.5f * std::sin(m_actionT * 3.14159f) : 0.f;   // drop, then cock
        drawPart(part, width, (part.flags & GP_ACTION) ? actionU : hammer * 0.4f, (part.flags & GP_MAG) ? magH : hammer, false);
    }
    if (!melee) {
        float rc = (MT.railU0 + MT.railU1) * 0.5f, rh = MT.railH;
        int optic = LD.att[AT_OPTIC];
        if (optic == 1) {                                               // red dot
            attBox(rc - 6, rh, rc + 6, rh + 2.5f, 0xFF1A1B1E, 4.2f, 0);
            attBox(rc - 3, rh + 2.5f, rc + 5, rh + 9, 0xFF25262A, 4.6f, 0);
            attBox(rc + 3, rh + 3.5f, rc + 4.5f, rh + 8, 0xFFFF4030, 3.8f, GP_GLASS);
        } else if (optic == 2) {                                        // holographic
            attBox(rc - 7, rh, rc + 8, rh + 3, 0xFF1A1B1E, 5.0f, 0);
            attBox(rc - 6, rh + 3, rc - 3, rh + 11, 0xFF2B2C30, 6.0f, 0);
            attBox(rc + 5, rh + 3, rc + 8, rh + 11, 0xFF2B2C30, 6.0f, 0);
            attBox(rc - 6, rh + 10, rc + 8, rh + 12, 0xFF2B2C30, 6.0f, 0);
            attBox(rc - 3, rh + 4, rc + 5, rh + 10, 0xFF6FB0DA, 5.4f, GP_GLASS);
        } else if (optic >= 3 && w != WPN_SNIPER && w != WPN_DMR && w != WPN_RAILGUN && w != WPN_BULLPUP) {   // magnified scope
            float len = optic == 3 ? 26.f : optic == 4 ? 34.f : 42.f;
            attBox(rc - 4, rh, rc - 1, rh + 5, 0xFF1A1B1E, 3.2f, 0);
            attBox(rc + 6, rh, rc + 9, rh + 5, 0xFF1A1B1E, 3.2f, 0);
            attBox(rc - len * 0.45f, rh + 5, rc + len * 0.55f, rh + 10, 0xFF1C1D20, 4.2f, 0);
            attBox(rc - len * 0.45f - 4, rh + 4, rc - len * 0.45f, rh + 11, 0xFF141517, 5.0f, 0);
            attBox(rc + len * 0.55f, rh + 3.5f, rc + len * 0.55f + 5, rh + 11.5f, 0xFF141517, 6.0f, 0);
            attBox(rc + len * 0.55f + 4, rh + 4.5f, rc + len * 0.55f + 5.5f, rh + 10.5f, 0xFF6FB0DA, 5.4f, GP_GLASS);
        }
        int mz = LD.att[AT_MUZZLE];
        if (mz == 1) attBox(M.muzzleU - 1, M.muzzleH - 3.6f, M.muzzleU + 22, M.muzzleH + 3.6f, 0xFF1E1F22, 5.2f, 0);   // suppressor
        if (mz == 2) {                                                  // compensator with ports
            attBox(M.muzzleU - 1, M.muzzleH - 3, M.muzzleU + 8, M.muzzleH + 3, 0xFF2A2B2E, 4.4f, 0);
            attBox(M.muzzleU + 2, M.muzzleH + 1.5f, M.muzzleU + 6, M.muzzleH + 3.2f, 0xFF0E0E10, 4.6f, GP_NORIM);
        }
        int ub = LD.att[AT_UNDER];
        if (ub == 1) attBox(MT.underU - 3, MT.underH - 15, MT.underU + 3, MT.underH, 0xFF25262A, 4.4f, 0);   // foregrip
        if (ub == 2) {                                                  // laser module
            attBox(MT.underU - 2, MT.underH - 6, MT.underU + 12, MT.underH, 0xFF2A2B2E, 4.2f, 0);
            attBox(MT.underU + 12, MT.underH - 4.5f, MT.underU + 13.5f, MT.underH - 1.5f, 0xFFFF3020, 2.8f, GP_GLOW);
            float lx, ly;
            if (vr.project(map(MT.underU + 13.5f, MT.underH - 3, 0.f), lx, ly)) { m_laserX = lx; m_laserY = ly; m_laserOn = true; }
        }
    }
    // --- Gloved hands wrapped around it -------------------------------------------------
    for (int i = 0; i < M.handCount; i++) {
        const GunPart &part = M.hands[i];
        bool support = arch == VA_PISTOL ? i == 0 : i < 4;
        float du = (part.flags & GP_ACTION) ? actionU : 0.f, dh = 0;
        if (support && arch == VA_RIFLE && WD.reload == RM_MAG) dh = -10.f * rl;   // support hand reaches for a fresh mag
        drawPart(part, G3.handW, du, dh, true);
    }
    // Fresh magazine in the support hand mid-reload
    if (pl.reloadTimer > 0 && WD.reload == RM_MAG && rp > 0.38f && rp < 0.62f) {
        V3 a = map(M.lWristU - 6, M.lWristH - 10, 0.f), b = map(M.lWristU - 4, M.lWristH - 34, 0.f);
        vr.box(a, b, 3.f * S, 3.f * S, rgb(56, 58, 62), 0.4f);
    }

    // Slashing arc / thrust streak effect for knife
    if (melee && m_actionT > 0.08f && m_actionT < 0.92f) {
        float s0x, s0y, s1x, s1y;
        if (vr.project(map(M.length * 0.4f, 0.f, 0.f), s0x, s0y) && vr.project(map(M.length + 8.f, 2.f, 0.f), s1x, s1y)) {
            int alpha = int(220 * std::sin(kt * 3.14159f));
            uint32_t streakCol = m_knifeSwing == 1 ? withAlpha(0xFFFFFFFF, alpha) : withAlpha(0xFFDDE8F8, alpha);
            m_cv.thickLine(int(s0x), int(s0y), int(s1x), int(s1y), streakCol, 3);
        }
    }

    // --- Muzzle / ejection points for effects -------------------------------------------------
    float ex, ey;
    if (vr.project(map(M.ejectU, M.ejectH, -G3.partW[0] * 0.5f), ex, ey)) { m_ejectX = ex; m_ejectY = ey; }
    V3 muzzle3 = map(M.muzzleU, M.muzzleH, 0.f);
    vr.project(muzzle3, m_muzzleX, m_muzzleY);

    // --- Muzzle flash ---------------------------------------------------------------------------
    if (pl.flashTimer > 0 && !melee) {
        float t = pl.flashTimer / (WD.cls == WC_SHOTGUN ? 0.09f : 0.06f);
        float ax2 = m_muzzleX, ay2 = m_muzzleY;
        vr.project(map(M.muzzleU + 12, M.muzzleH, 0.f), ax2, ay2);
        float dx = ax2 - m_muzzleX, dy = ay2 - m_muzzleY, dl = std::sqrt(dx * dx + dy * dy) + 1e-3f;
        dx /= dl; dy /= dl;
        float s = std::min(3.2f, ViewModelRaster::F * S / std::max(0.05f, muzzle3.z));   // pixels per gun unit at the muzzle
        float sc = s * (WD.cls == WC_SHOTGUN || WD.cls == WC_LAUNCHER ? 1.6f : 1.f) * (0.7f + 0.5f * t) * (LD.att[AT_MUZZLE] == 1 ? 0.35f : 1.f);
        unsigned seed = unsigned(m_flashSeed);
        auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return float((seed >> 16) & 0x7FFF) / 32767.f; };
        m_cv.glow(int(m_muzzleX), int(m_muzzleY), int(60 * sc), rgb(255, 170, 60), 0.9f * t);
        // forward flame cone + side petals (additive Gouraud triangles)
        for (int p = 0; p < 7; p++) {
            float ang = (p == 0) ? 0.f : (p - 3.5f) * 0.42f + (rnd() - 0.5f) * 0.3f;
            float len = (p == 0 ? (WD.automatic ? 26.f : 42.f) : 14.f + 16.f * rnd()) * sc;
            float wdt = (p == 0 ? 7.f : 4.f) * sc;
            float c = std::cos(ang), sn = std::sin(ang);
            float ddx = dx * c - dy * sn, ddy = dx * sn + dy * c;
            float pnx = -ddy, pny = ddx;
            m_cv.tri(m_muzzleX + pnx * wdt, m_muzzleY + pny * wdt, m_muzzleX - pnx * wdt, m_muzzleY - pny * wdt,
                     m_muzzleX + ddx * len, m_muzzleY + ddy * len,
                     argb(255, 255, 250, 220), argb(255, 255, 250, 220), argb(0, 255, 120, 20), true);
        }
        m_cv.fillCircle(int(m_muzzleX), int(m_muzzleY), int(6 * sc), withAlpha(rgb(255, 255, 235), 230));
    }
}

void MainWindow::renderLaser()
{
    const Player &pl = m_gs.player;
    if (pl.curWeapon < 0 || pl.load[pl.curWeapon].att[AT_UNDER] != 2 || isMelee(pl.curWeapon)) { m_laserOn = false; return; }
    if (effectiveScope(pl.curWeapon) && m_adsT > 0.6f) { m_laserOn = false; return; }
    float ax, ay; aimPoint(ax, ay);
    HitResult h = hitscan(ax, ay);
    float ex = ax, ey = ay, sx = m_laserX, sy = m_laserY;
    bool ok = m_laserOn && !thirdPerson();
    if (thirdPerson()) {
        float mx, my, mz; playerMuzzle(mx, my, mz);
        float d; ok = project(mx, my, mz, sx, sy, d);
        if (!h.sky) { float d2; project(h.x, h.y, h.z, ex, ey, d2); }
    }
    if (ok) {
        // the beam is faint in the air and brightest at the dot
        int steps = std::max(1, int(std::max(std::fabs(ex - sx), std::fabs(ey - sy))));
        for (int i = 0; i <= steps; i++) {
            float k = float(i) / steps;
            m_cv.add(int(sx + (ex - sx) * k), int(sy + (ey - sy) * k), withAlpha(rgb(255, 40, 30), int(40 + 70 * k)));
        }
        if (!h.sky) { m_cv.glow(int(ex), int(ey), 7, rgb(255, 40, 30), 1.1f); m_cv.fillCircle(int(ex), int(ey), 1, rgb(255, 170, 160)); }
    }
    m_laserOn = false;
}

void MainWindow::renderCasings()
{
    for (const Casing &c : m_casings) {
        float L = c.shell ? 9.f : 6.f, Wd = c.shell ? 3.5f : 2.f;
        float ca = std::cos(c.a), sa = std::sin(c.a);
        float xy[8] = { c.x - ca * L + sa * Wd, c.y - sa * L - ca * Wd, c.x + ca * L + sa * Wd, c.y + sa * L - ca * Wd,
                        c.x + ca * L - sa * Wd, c.y + sa * L + ca * Wd, c.x - ca * L - sa * Wd, c.y - sa * L + ca * Wd };
        uint32_t body = c.shell ? rgb(190, 40, 30) : rgb(220, 170, 60), hi = c.shell ? rgb(240, 90, 70) : rgb(255, 230, 140);
        uint32_t cols[4] = { hi, hi, body, body };
        m_cv.poly(xy, cols, 4);
        if (c.shell) m_cv.fillCircle(int(c.x + ca * L), int(c.y + sa * L), 2, rgb(210, 170, 70));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Reflex / holographic sight (pistol + shotgun ADS)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderReflexSight()
{
    const Player &pl = m_gs.player;
    if (pl.curWeapon < 0 || effectiveScope(pl.curWeapon) || isMelee(pl.curWeapon) || m_adsT < 0.02f) return;
    float rise = 1.f - smoothstep01(m_adsT);
    float cx = W / 2.f + m_swayX * 0.4f, cy = H / 2.f + m_swayY * 0.4f + rise * H * 0.8f;
    int optic = pl.load[pl.curWeapon].att[AT_OPTIC];
    bool holo = optic == 2 || (optic == 0 && WEAPONS[pl.curWeapon].cls == WC_SHOTGUN);
    const CharProfile &cp = CHAR_PROFILES[m_charChoice];
    const float gw = holo ? 58.f : 36.f, gh = holo ? 38.f : 25.f, ft = holo ? 9.f : 7.f;
    const uint32_t metal = rgb(30, 31, 35), metalHi = rgb(78, 80, 88), metalMid = rgb(46, 47, 52);

    // Weapon body seen from behind: slide / receiver widening toward the camera
    float top = cy + gh + ft + 2;
    float w0 = holo ? 46 : 26, w1 = holo ? 84 : 46;
    m_cv.tri(cx - w0, top, cx + w0, top, cx + w1, H + 4, metalHi, metalHi, metal);
    m_cv.tri(cx - w0, top, cx + w1, H + 4, cx - w1, H + 4, metalHi, metal, metal);
    m_cv.line(int(cx - w0), int(top), int(cx + w0), int(top), withAlpha(rgb(150, 152, 160), 200));
    for (int k = 0; k < 7; k++) {                                  // rear serrations
        float yy = top + 30 + k * 8, f = (yy - top) / (H - top);
        float hw = w0 + (w1 - w0) * f;
        m_cv.blendRect(int(cx - hw * 0.7f), int(yy), int(hw * 1.4f), 2, withAlpha(0, 110));
    }
    // Two-handed grip: gloves and thumbs climbing the sides
    const uint32_t glove = rgb(40, 40, 46), gloveHi = rgb(70, 70, 78);
    float gy = top + (H - top) * 0.45f;
    limb(m_cv, cx + w1 + 160, H + 120, 70, cx + w1 * 0.9f, H - 6, 34, cp.sleeve, cp.sleeveHi);
    limb(m_cv, cx - w1 - 160, H + 120, 70, cx - w1 * 0.9f, H - 6, 34, cp.sleeve, cp.sleeveHi);
    m_cv.tri(cx + w1 * 0.75f, gy, cx + w1 + 120, H + 10, cx + w1 * 0.5f, H + 10, gloveHi, glove, glove);
    m_cv.tri(cx - w1 * 0.75f, gy, cx - w1 * 0.5f, H + 10, cx - w1 - 120, H + 10, gloveHi, glove, glove);
    m_cv.thickLine(int(cx + w1 * 0.8f), int(H - 10), int(cx + w0 + 6), int(gy - 6), gloveHi, 12);
    m_cv.thickLine(int(cx - w1 * 0.8f), int(H - 10), int(cx - w0 - 6), int(gy - 6), gloveHi, 12);

    // Sight mount + glass + housing (thick hood on top)
    m_cv.quad(cx - gw * 0.6f, top - 4, cx + gw * 0.6f, top - 4, cx + gw * 0.8f, top + 6, cx - gw * 0.8f, top + 6, metalMid);
    int L = int(cx - gw), T = int(cy - gh), Wd = int(2 * gw), Hd = int(2 * gh);
    for (int y = T; y < T + Hd; y++) for (int x = L; x < L + Wd; x++) {
        float d = float((x - L) + (y - T)) / float(Wd + Hd);
        int a = int(22 + 34 * std::max(0.f, 1.f - std::fabs(d - 0.28f) * 7.f));
        m_cv.blend(x, y, withAlpha(rgb(150, 215, 235), a));
    }
    int F = int(ft);
    m_cv.vGrad(L - F, T - F - 6, Wd + 2 * F, F + 6, metalHi, metal);
    m_cv.fillRect(L - F, T, F, Hd, metal);
    m_cv.fillRect(L + Wd, T, F, Hd, metal);
    m_cv.fillRect(L - F, T + Hd, Wd + 2 * F, F, metalMid);
    m_cv.blendRect(L - F, T - F - 6, 2, Hd + 2 * F + 6, withAlpha(metalHi, 220));
    m_cv.rectOutline(L - 1, T - 1, Wd + 2, Hd + 2, withAlpha(0, 170));
    if (holo) { m_cv.fillRect(L + Wd + F, T + 8, 7, 16, metalHi); m_cv.fillRect(L - F - 7, T + 8, 7, 16, metalHi); }

    // Illuminated reticle
    if (m_adsT > 0.55f) {
        float a = smoothstep01((m_adsT - 0.55f) / 0.45f);
        int ax = int(cx), ay = int(cy);
        if (holo) {
            m_cv.ring(ax, ay, 20.f, 22.f, withAlpha(rgb(255, 50, 40), int(230 * a)));
            for (int k = 0; k < 4; k++) {
                float an = k * 1.5707963f;
                m_cv.thickLine(ax + int(std::cos(an) * 25), ay + int(std::sin(an) * 25), ax + int(std::cos(an) * 31), ay + int(std::sin(an) * 31),
                               withAlpha(rgb(255, 50, 40), int(230 * a)), 2);
            }
        }
        m_cv.glow(ax, ay, 14, rgb(255, 30, 20), 1.1f * a);
        m_cv.fillCircle(ax, ay, 2, withAlpha(rgb(255, 70, 55), int(255 * a)));
        m_cv.set(ax, ay, rgb(255, 210, 200));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Magnified rifle scope (full-screen optic)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderScope()
{
    const Player &pl = m_gs.player;
    if (!effectiveScope(pl.curWeapon) || m_adsT <= 0.6f) return;
    float open = smoothstep01((m_adsT - 0.6f) / 0.4f);
    float cx = W / 2.f + m_scopeSwayX, cy = H / 2.f + m_scopeSwayY;
    float Rr = H * 0.47f * (0.6f + 0.4f * open);
    float edge = 22.f;
    float R2 = Rr * Rr, Ri = Rr - edge;

    // Outside the eyepiece: black; inner edge: lens vignette + chromatic fringe
    for (int y = 0; y < H; y++) {
        float dy = y + 0.5f - cy;
        uint32_t *row = m_pixels + y * W;
        for (int x = 0; x < W; x++) {
            float dx = x + 0.5f - cx, d2 = dx * dx + dy * dy;
            if (d2 > R2) { row[x] = 0xFF050505; continue; }
            if (d2 < Ri * Ri) continue;
            float t = (std::sqrt(d2) - Ri) / edge;     // 0 inner → 1 rim
            uint32_t c = row[x];
            float k = 1.f - t * t * 0.95f;
            int r = int(R(c) * k), g = int(G(c) * k), b = int(B(c) * k);
            if (t > 0.55f && t < 0.8f) { b += int(60 * (1 - std::fabs(t - 0.67f) / 0.13f)); r -= 10; }
            row[x] = rgb(r, g, b);
        }
    }
    // Scope tube rim
    m_cv.ring(int(cx), int(cy), Rr, Rr + 3, rgb(38, 40, 44));

    // Duplex reticle: thick outer posts, thin centre crosshair, mil-dots
    const uint32_t ink = rgb(8, 8, 8);
    int icx = int(cx), icy = int(cy);
    int inner = int(Rr * 0.28f);
    m_cv.fillRect(int(cx - Rr), icy - 2, int(Rr - inner), 4, ink);
    m_cv.fillRect(icx + inner, icy - 2, int(Rr - inner), 4, ink);
    m_cv.fillRect(icx - 2, int(cy - Rr), 4, int(Rr - inner), ink);
    m_cv.fillRect(icx - 2, icy + inner, 4, int(Rr - inner), ink);
    m_cv.fillRect(icx - inner, icy, inner * 2, 1, ink);
    m_cv.fillRect(icx, icy - inner, 1, inner * 2, ink);
    float mil = inner / 4.5f;
    for (int i = 1; i <= 4; i++) {
        int o = int(i * mil);
        m_cv.fillCircle(icx + o, icy, 2, ink); m_cv.fillCircle(icx - o, icy, 2, ink);
        m_cv.fillCircle(icx, icy - o, 2, ink);
        m_cv.fillRect(icx - 6 + i, icy + o, 13 - 2 * i, 1, ink);          // bullet-drop ladder
    }
    // Illuminated centre dot
    m_cv.glow(icx, icy, 7, rgb(255, 40, 30), 0.8f);
    m_cv.fillCircle(icx, icy, 1, rgb(255, 90, 70));

    // Lens glint (upper-left arc)
    m_cv.arc(icx, icy, Rr - 34, Rr - 30, 3.6f, 4.4f, withAlpha(rgb(200, 230, 255), 26));
    m_cv.arc(icx, icy, Rr - 28, Rr - 26, 3.7f, 4.2f, withAlpha(rgb(220, 240, 255), 18));

    // Magnification + breath indicator
    text(cx + Rr * 0.45f, cy + Rr * 0.62f, 90, 18, Qt::AlignLeft, QString("%1.0x").arg(int(m_scopeMag)), QColor(210, 220, 200, 200), 13, 700, false);
    text(0, 14, W, 18, Qt::AlignCenter, m_holdBreath ? "HOLDING BREATH" : "SHIFT  hold breath   WHEEL  zoom",
         m_holdBreath ? QColor(120, 220, 255) : QColor(170, 170, 170), 11, 600);
}
