// =============================================================================
//  render_fx.cpp  -  Weapon effects drawn into the framebuffer
//
//  Plasma bolts (additive glow), chain lightning (jagged Bresenham polylines
//  re-randomised every frame), the railgun slug trail, grapple rope, the laser
//  sight, radar markers seen through walls, being inside a smoke cloud, the
//  flashbang white-out and explosion light. All per-pixel, all raster.
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

void MainWindow::renderWorldFX()
{
    auto depthAt = [&](int x, int y) { return m_depth[size_t(clampi(y, 0, H - 1)) * W + clampi(x, 0, W - 1)]; };

    // --- Plasma bolts -----------------------------------------------------------------
    for (const Projectile &p : m_gs.projectiles) {
        if (p.kind != PJ_PLASMA) continue;
        float sx, sy, d;
        if (!project(p.x, p.y, p.z, sx, sy, d) || d < 0.1f || d > depthAt(int(sx), int(sy)) + 0.3f) continue;
        int r = clampi(int((0.05f + 0.03f * p.splashR) * m_K / d), 2, 14);
        m_cv.glow(int(sx), int(sy), r * 3, rgb(70, 200, 255), 0.85f);
        m_cv.fillCircle(int(sx), int(sy), r, rgb(200, 245, 255));
        // short streak behind it
        float tx, ty, td;
        if (project(p.x - p.vx * 0.03f, p.y - p.vy * 0.03f, p.z - p.vz * 0.03f, tx, ty, td))
            m_cv.thickLine(int(tx), int(ty), int(sx), int(sy), withAlpha(rgb(120, 220, 255), 160), std::max(1, r));
    }

    // --- Beams ------------------------------------------------------------------------
    unsigned seed = unsigned(m_gs.frameNum * 2654435761u);
    auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return float((seed >> 16) & 0x7FFF) / 32767.f; };
    for (const Beam &b : m_gs.beams) {
        float a = clampf(b.life / std::max(0.01f, b.maxLife), 0.f, 1.f);
        float x0, y0, d0, x1, y1, d1;
        if (!project(b.x0, b.y0, b.z0, x0, y0, d0) || !project(b.x1, b.y1, b.z1, x1, y1, d1)) continue;
        if (b.kind == 0) {
            // chain lightning: jagged path, a fresh zig-zag every frame
            const int N = 9;
            float px = x0, py = y0;
            float dx = x1 - x0, dy = y1 - y0, l = std::sqrt(dx * dx + dy * dy) + 1e-3f;
            float nx = -dy / l, ny = dx / l;
            for (int i = 1; i <= N; i++) {
                float t = float(i) / N;
                float off = i == N ? 0.f : (rnd() - 0.5f) * l * 0.18f;
                float qx = x0 + dx * t + nx * off, qy = y0 + dy * t + ny * off;
                m_cv.thickLine(int(px), int(py), int(qx), int(qy), withAlpha(b.color, int(150 * a)), 4);
                m_cv.line(int(px), int(py), int(qx), int(qy), withAlpha(rgb(240, 250, 255), int(255 * a)));
                px = qx; py = qy;
            }
            m_cv.glow(int(x1), int(y1), 18, b.color, 0.9f * a);
        } else if (b.kind == 1) {
            // railgun: a straight bright core with a soft, slowly fading halo
            int th = std::max(2, int(7 * a));
            m_cv.thickLine(int(x0), int(y0), int(x1), int(y1), withAlpha(b.color, int(110 * a)), th + 4);
            m_cv.thickLine(int(x0), int(y0), int(x1), int(y1), withAlpha(rgb(230, 250, 255), int(230 * a)), std::max(1, th / 2));
        } else {
            m_cv.line(int(x0), int(y0), int(x1), int(y1), withAlpha(b.color, int(230 * a)));      // grapple rope
        }
    }

    // --- Radar ping: hostiles marked through walls -------------------------------------
    if (m_radarT > 0.f) {
        float a = std::min(1.f, m_radarT / 1.f);
        for (const Enemy &e : m_gs.enemies) {
            if (!e.alive) continue;
            float sx, sy, d;
            if (!project(e.x, e.y, floorH(e.x, e.y) + 0.6f * e.scale, sx, sy, d) || d < 0.2f) continue;
            if (sx < -20 || sx > W + 20 || sy < -20 || sy > H + 20) continue;
            int r = clampi(int(0.25f * m_K / d), 5, 30);
            bool hidden = d > depthAt(int(sx), int(sy)) + 0.2f;
            uint32_t c = withAlpha(rgb(255, 70, 50), int((hidden ? 200 : 120) * a));
            m_cv.line(int(sx), int(sy - r), int(sx + r), int(sy), c); m_cv.line(int(sx + r), int(sy), int(sx), int(sy + r), c);
            m_cv.line(int(sx), int(sy + r), int(sx - r), int(sy), c); m_cv.line(int(sx - r), int(sy), int(sx), int(sy - r), c);
            if (hidden) m_cv.fillRect(int(sx) - 1, int(sy) - 1, 3, 3, c);
        }
    }

    // --- Inside a smoke cloud: the whole view goes grey ---------------------------------
    float smoke = 0.f;
    for (const SmokeCloud &s : m_gs.smokes) {
        float grow = std::min(1.f, (s.maxLife - s.life) / 1.2f) * std::min(1.f, s.life / 2.f);
        float d = std::sqrt((m_camX - s.x) * (m_camX - s.x) + (m_camY - s.y) * (m_camY - s.y));
        smoke = std::max(smoke, clampf(1.f - d / std::max(0.1f, s.r * grow), 0.f, 1.f));
    }
    if (smoke > 0.01f) m_cv.blendRect(0, 0, W, H, withAlpha(rgb(190, 194, 198), int(230 * smoke)));
}

// Flashbang white-out and explosion flash: full-screen raster blends
void MainWindow::renderScreenFlash()
{
    if (m_blastLight > 0.f) {
        int a = int(45 * m_blastLight);
        for (int i = 0; i < W * H; i++) {
            uint32_t c = m_pixels[i];
            m_pixels[i] = 0xFF000000 | (uint32_t(std::min(255, int(R(c)) + a)) << 16) | (uint32_t(std::min(255, int(G(c)) + a * 3 / 4)) << 8) | uint32_t(std::min(255, int(B(c)) + a / 3));
        }
    }
    if (m_flashT > 0.f) m_cv.blendRect(0, 0, W, H, withAlpha(rgb(255, 255, 250), int(255 * clampf(m_flashT / 1.4f, 0.f, 1.f))));
}
