// =============================================================================
//  render_weapon.cpp  -  First-person weapon, hands, optics
//
//  The gun is a set of 2D side-profile polygons (gunmodels.h) pushed through a
//  one-point perspective mapping: points along the barrel converge toward a
//  vanishing point placed on the crosshair, and heights shrink with distance.
//  Every polygon is filled by the edge-function triangle rasterizer with
//  Gouraud shading (lighter toward the top), plus a Bresenham specular rim.
//
//  Animations: walk bob, mouse-lag sway, recoil kick + muzzle climb, slide /
//  pump cycling, magazine-out reload, weapon-swap drop, sprint carry pose.
// =============================================================================
#include "mainwindow.h"
#include "gunmodels.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static inline float smoothstep01(float x) { x = clampf(x, 0.f, 1.f); return x * x * (3 - 2 * x); }

namespace {
struct GunProj {
    float Rx, Ry, Vx, Vy, k, s0, L, upX, upY;
    void map(float u, float h, float &x, float &y, float &s) const {
        float f = 1.f / (1.f + k * u / L);
        s = s0 * f;
        x = Vx + (Rx - Vx) * f + upX * h * s;
        y = Vy + (Ry - Vy) * f + upY * h * s;
    }
};

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
} // namespace

void MainWindow::renderViewModel()
{
    const Player &pl = m_gs.player;
    const int w = pl.curWeapon;
    const WeaponDef &wd = WEAPONS[w];
    if (wd.scope && m_adsT > 0.6f) return;                     // scope overlay replaces the gun
    const GunModel &M = *GUN_MODELS[w];
    const CharProfile &cp = CHAR_PROFILES[m_charChoice];

    // --- Animation state --------------------------------------------------------
    bool moving = m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D);
    float bobAmp = moving ? (pl.sprinting ? 1.6f : 1.f) : 0.f;
    float bx = std::sin(pl.bobPhase) * 9.f * bobAmp, by = std::fabs(std::cos(pl.bobPhase)) * 7.f * bobAmp;
    float breathe = std::sin(m_time * 1.7f) * 2.2f;
    float rl = 0, rp = 0;
    if (pl.reloadTimer > 0) { rp = 1.f - pl.reloadTimer / std::max(0.01f, m_reloadTotal); rl = smoothstep01(rp / 0.18f) * (1.f - smoothstep01((rp - 0.82f) / 0.18f)); }
    float drop = 0;
    if (pl.switchTimer > 0) { float s = pl.switchTimer / 0.4f; drop = s > 0.5f ? (1.f - s) * 2.f : s * 2.f; }
    float spr = m_sprintT;
    float aimHide = wd.scope ? 0.f : m_adsT;                    // reflex weapons lower as the sight rises

    // --- Anchors: rear of the gun (R, off-screen bottom-right) and the
    //     vanishing point (V, on the crosshair) -------------------------------------
    // --- Anchors: rear of the gun (R, off-screen bottom-right) and the
    //     vanishing point (V, on the crosshair) -------------------------------------
    struct Base { float rx, ry, k, s0; };
    const Base bases[WPN_COUNT] = {
        {W * 0.74f, H * 0.82f, 0.8f, 5.2f}, // WPN_KNIFE
        {W * 0.78f, H * 0.82f, 0.7f, 5.0f}, // WPN_PISTOL
        {W * 1.00f, H * 0.98f, 1.5f, 5.8f}, // WPN_SHOTGUN
        {W * 1.00f, H * 0.98f, 1.7f, 5.6f}  // WPN_RIFLE
    };
    const Base &b = bases[w];
    GunProj gp;
    gp.k = b.k; gp.s0 = b.s0; gp.L = M.length;
    gp.Vx = m_aimX + m_swayX * 0.5f; gp.Vy = m_aimY + m_swayY * 0.5f + 25.f;
    gp.Rx = b.rx + bx + m_swayX; gp.Ry = b.ry + by + breathe + m_swayY;
    // Rifle ADS: swing toward the centre line before the scope takes over
    if (wd.scope && m_adsT > 0) { gp.Rx += (W * 0.62f - gp.Rx) * m_adsT; gp.Ry += 30 * m_adsT; }
    // recoil: gun slams back toward the camera, muzzle climbs
    gp.Rx += (gp.Rx - gp.Vx) * 0.08f * m_kick; gp.Ry += (gp.Ry - gp.Vy) * 0.08f * m_kick;
    gp.Vy -= 50.f * m_kick;
    float cant = -0.04f + 0.05f * m_kick;

    // Knife unique stabbing and slashing animations
    if (w == WPN_KNIFE && m_actionT > 0.f) {
        float t = 1.0f - m_actionT;
        if (m_knifeSwing == 1) {
            // STAB ANIMATION: Forward piercing lunge directly into the crosshair
            float thrust = std::sin(t * 3.14159f);
            gp.Rx += (gp.Vx - gp.Rx) * 0.48f * thrust;
            gp.Ry += (gp.Vy - gp.Ry) * 0.48f * thrust;
            gp.s0 *= (1.f + 0.35f * thrust);
            cant += -0.25f * thrust;
        } else {
            // SLASH ANIMATION: Sweeping diagonal cut across the screen with blade tilt
            float slash = std::sin(t * 3.14159f);
            gp.Rx += std::cos(t * 3.14159f) * 120.f * slash;
            gp.Ry -= std::sin(t * 3.14159f) * 75.f * slash;
            cant += (t - 0.5f) * 1.4f * slash;
        }
    }

    // reload: tilt the gun in and up so the magazine well faces the camera
    gp.Ry += rl * 30; gp.Vy += rl * 120; gp.Vx -= rl * 70; cant += rl * 0.45f;
    // sprint carry
    gp.Rx += 20 * spr; gp.Ry += 40 * spr; gp.Vx -= 200 * spr; gp.Vy += 200 * spr; cant += 0.35f * spr;
    // swap / reflex-ADS drop
    float dropPx = (drop + aimHide) * 340.f;
    gp.Ry += dropPx; gp.Vy += dropPx;
    // Gun "up": mostly screen-up, a little perpendicular to the barrel, rotated by cant
    {
        float ax = gp.Vx - gp.Rx, ay = gp.Vy - gp.Ry, al = std::sqrt(ax * ax + ay * ay) + 1e-3f;
        float px = ay / al, py = -ax / al; if (py > 0) { px = -px; py = -py; }
        float ux = px * 0.18f, uy = -0.82f + py * 0.18f, ul = std::sqrt(ux * ux + uy * uy);
        ux /= ul; uy /= ul;
        float c = std::cos(cant), s = std::sin(cant);
        gp.upX = ux * c - uy * s; gp.upY = ux * s + uy * c;
    }

    // --- Moving parts -----------------------------------------------------------------
    float actionU = 0;
    if (w == WPN_PISTOL) actionU = -7.f * m_actionT;
    if (w == WPN_SHOTGUN) { float t = 1.f - m_actionT; actionU = t > 0.15f && t < 0.85f ? -11.f * std::sin((t - 0.15f) / 0.7f * 3.14159f) : 0.f; }
    float magH = 0; bool magHidden = false;
    if (pl.reloadTimer > 0 && w != WPN_SHOTGUN && w != WPN_KNIFE) {
        if (rp < 0.2f) magH = 0;
        else if (rp < 0.42f) magH = -60.f * (rp - 0.2f) / 0.22f;
        else if (rp < 0.58f) magHidden = true;
        else if (rp < 0.8f) magH = -45.f * (1.f - (rp - 0.58f) / 0.22f);
    }

    // Rasterize one convex part through the perspective mapping (Gouraud + rim)
    auto drawPart = [&](const GunPart &part, float du, float dh) {
        float xy[12]; uint32_t cols[6];
        float hmin = 1e9f, hmax = -1e9f;
        for (int k = 0; k < part.n; k++) { hmin = std::min(hmin, part.pts[2 * k + 1]); hmax = std::max(hmax, part.pts[2 * k + 1]); }
        for (int k = 0; k < part.n; k++) {
            float u = part.pts[2 * k] + du, h = part.pts[2 * k + 1], s;
            gp.map(u, h + dh, xy[2 * k], xy[2 * k + 1], s);
            float local = hmax > hmin ? (h - hmin) / (hmax - hmin) : 0.5f;
            float global = (h - M.hMin) / (M.hMax - M.hMin);
            float lt = (0.62f + 0.55f * local + 0.25f * global) * (1.f + 0.25f * u / M.length);
            if (part.flags & GP_GLASS) cols[k] = lerpCol(part.col, rgb(220, 240, 255), local * 0.6f);
            else cols[k] = scaleCol(part.col, lt * 1.2f);
        }
        m_cv.poly(xy, cols, part.n);
        if (part.flags & GP_NORIM) return;
        float area = 0;
        for (int k = 0; k < part.n; k++) { int j = (k + 1) % part.n; area += part.pts[2 * k] * part.pts[2 * j + 1] - part.pts[2 * j] * part.pts[2 * k + 1]; }
        for (int k = 0; k < part.n; k++) {
            int j = (k + 1) % part.n;
            float eu = part.pts[2 * j] - part.pts[2 * k], eh = part.pts[2 * j + 1] - part.pts[2 * k + 1];
            float nh = area > 0 ? -eu : eu;             // outward normal's height component
            float el = std::sqrt(eu * eu + eh * eh);
            if (el < 0.01f || nh / el < 0.6f) continue;
            m_cv.line(int(xy[2 * k]), int(xy[2 * k + 1]), int(xy[2 * j]), int(xy[2 * j + 1]), withAlpha(scaleCol(part.col, 3.4f), 160));
        }
    };

    // --- Sleeves (behind everything) ---------------------------------------------------
    float rwx, rwy, rws, lwx, lwy, lws;
    gp.map(M.rWristU, M.rWristH, rwx, rwy, rws);
    gp.map(M.lWristU + (w == WPN_SHOTGUN ? actionU : 0), M.lWristH, lwx, lwy, lws);
    float lsx = (w == WPN_PISTOL ? W * 0.55f : W * 0.30f) + bx * 0.5f + m_swayX, lsy = H + 150.f + dropPx * 0.5f;
    float rsx = W + 90.f + bx + m_swayX, rsy = H + 110.f + dropPx * 0.6f;

    if (w == WPN_KNIFE) {
        // Right arm drives the combat knife
        limb(m_cv, rsx, rsy, 60, rwx, rwy, 9.f * rws, cp.sleeve, cp.sleeveHi);
        limb(m_cv, rwx + (rsx - rwx) * 0.12f, rwy + (rsy - rwy) * 0.12f, 10.f * rws, rwx, rwy, 8.5f * rws, scaleCol(cp.sleeveHi, 1.2f), cp.sleeveHi);
        // Left hand in tactical ready guard
        float glx = W * 0.20f + bx * 0.3f, gly = H + 80.f + dropPx * 0.4f;
        float gwx = W * 0.28f + bx * 0.4f, gwy = H * 0.88f + dropPx * 0.4f;
        limb(m_cv, glx, gly, 46, gwx, gwy, 28, cp.sleeve, cp.sleeveHi);
        m_cv.fillCircle(int(gwx), int(gwy), 14, GLOVE);
    } else {
        limb(m_cv, lsx, lsy, 54, lwx, lwy, 8.5f * lws, cp.sleeve, cp.sleeveHi);
        limb(m_cv, lwx + (lsx - lwx) * 0.12f, lwy + (lsy - lwy) * 0.12f, 9.5f * lws, lwx, lwy, 8.f * lws, scaleCol(cp.sleeveHi, 1.2f), cp.sleeveHi);
        limb(m_cv, rsx, rsy, 60, rwx, rwy, 9.f * rws, cp.sleeve, cp.sleeveHi);
        limb(m_cv, rwx + (rsx - rwx) * 0.12f, rwy + (rsy - rwy) * 0.12f, 10.f * rws, rwx, rwy, 8.5f * rws, scaleCol(cp.sleeveHi, 1.2f), cp.sleeveHi);
    }

    // --- Gun / Knife, then gloves wrapped around it ---------------------------------------------
    for (int i = 0; i < M.count; i++) {
        const GunPart &part = M.parts[i];
        if ((part.flags & GP_MAG) && magHidden) continue;
        drawPart(part, (part.flags & GP_ACTION) ? actionU : 0.f, (part.flags & GP_MAG) ? magH : 0.f);
    }
    for (int i = 0; i < M.handCount; i++) {
        const GunPart &part = M.hands[i];
        bool support = w == WPN_PISTOL ? i == 0 : i < 4;
        float du = (part.flags & GP_ACTION) ? actionU : 0.f, dh = 0;
        if (support && w == WPN_RIFLE) dh = -10.f * rl;          // support hand reaches for a fresh mag
        drawPart(part, du, dh);
    }

    // Slashing arc / thrust streak effect for knife
    if (w == WPN_KNIFE && m_actionT > 0.08f && m_actionT < 0.92f) {
        float act = m_actionT;
        float t = 1.0f - act;
        float s0x, s0y, ss0, s1x, s1y, ss1;
        gp.map(M.length * 0.4f, 0.f, s0x, s0y, ss0);
        gp.map(M.length + 8.f, 2.f, s1x, s1y, ss1);
        int alpha = int(220 * std::sin(t * 3.14159f));
        uint32_t streakCol = m_knifeSwing == 1 ? withAlpha(0xFFFFFFFF, alpha) : withAlpha(0xFFDDE8F8, alpha);
        m_cv.thickLine(int(s0x), int(s0y), int(s1x), int(s1y), streakCol, 3);
    }

    // Fresh magazine in the support hand mid-reload
    if (pl.reloadTimer > 0 && w != WPN_SHOTGUN && w != WPN_KNIFE && rp > 0.38f && rp < 0.62f) {
        float mx, my, ms; gp.map(M.lWristU - 6, M.lWristH - 22, mx, my, ms);
        m_cv.quad(mx - 4 * ms, my - 12 * ms, mx + 4 * ms, my - 12 * ms, mx + 6 * ms, my + 10 * ms, mx - 3 * ms, my + 10 * ms, rgb(56, 58, 62));
    }

    // --- Muzzle / ejection points for effects -------------------------------------------------
    float s;
    float ex, ey; gp.map(M.ejectU, M.ejectH, ex, ey, s); m_ejectX = ex; m_ejectY = ey;
    gp.map(M.muzzleU, M.muzzleH, m_muzzleX, m_muzzleY, s);

    // --- Muzzle flash ---------------------------------------------------------------------------
    if (pl.flashTimer > 0 && w != WPN_KNIFE) {
        float t = pl.flashTimer / (w == WPN_SHOTGUN ? 0.09f : 0.06f);
        float ax2, ay2, s2; gp.map(M.muzzleU + 12, M.muzzleH, ax2, ay2, s2);
        float dx = ax2 - m_muzzleX, dy = ay2 - m_muzzleY, dl = std::sqrt(dx * dx + dy * dy) + 1e-3f;
        dx /= dl; dy /= dl;
        float sc = s * (w == WPN_SHOTGUN ? 1.6f : 1.f) * (0.7f + 0.5f * t);
        unsigned seed = unsigned(m_flashSeed);
        auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return float((seed >> 16) & 0x7FFF) / 32767.f; };
        m_cv.glow(int(m_muzzleX), int(m_muzzleY), int(60 * sc), rgb(255, 170, 60), 0.9f * t);
        // forward flame cone + side petals (additive Gouraud triangles)
        for (int p = 0; p < 7; p++) {
            float ang = (p == 0) ? 0.f : (p - 3.5f) * 0.42f + (rnd() - 0.5f) * 0.3f;
            float len = (p == 0 ? 42.f : 14.f + 16.f * rnd()) * sc;
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
    if (effectiveScope(pl.curWeapon) || m_adsT < 0.02f) return;
    float rise = 1.f - smoothstep01(m_adsT);
    float cx = W / 2.f + m_swayX * 0.4f, cy = H / 2.f + m_swayY * 0.4f + rise * H * 0.8f;
    bool holo = pl.curWeapon == WPN_SHOTGUN;
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
