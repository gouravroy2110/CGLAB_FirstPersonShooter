// =============================================================================
//  render_world.cpp  -  Software 3D world renderer
//
//    • Terrain: one front-to-back ray-march per screen column over the
//      heightfield (voxel-space style with a y-buffer). Rolling ground,
//      ledge cliffs, wall faces and wall tops all come out of the same loop.
//      Each ground span is solved exactly per pixel row, so the floor texture
//      stays perspective-correct on slopes. Writes a per-pixel z-buffer.
//    • Ground shading: mip-mapped texture, baked light map (slope Lambert x
//      terrain/wall shadows x AO), blood & scorch decals, animated acid.
//    • Walls: mip-mapped textures, Lambert per face, contact AO, bullet holes.
//    • Sky: angular panorama lookup above the terrain silhouette.
//    • Sprites / tracers / particles: projected and depth-tested per pixel.
// =============================================================================
#include "mainwindow.h"
#include "parallel.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static const float PI = 3.14159265f;
static const float MAXD = 60.f;

uint32_t MainWindow::fog(uint32_t c, float dist) const
{
    const Theme &T = m_env;
    float f = std::exp(-T.fogDensity * dist);
    int hr = R(T.haze), hg = G(T.haze), hb = B(T.haze);
    return (c & 0xFF000000) | (uint32_t(hr + int((R(c) - hr) * f)) << 16) |
           (uint32_t(hg + int((G(c) - hg) * f)) << 8) | uint32_t(hb + int((B(c) - hb) * f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Terrain + walls
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderTerrain()
{
    const Player &pl = m_gs.player;
    const Theme &T = m_env;
    const MipTex &ft = m_floorTex[m_theme];
    const int LW = m_mapW * LM, DW = m_mapW * DM;
    const int hr = R(T.haze), hg = G(T.haze), hb = B(T.haze);
    const float sunX = std::cos(SUN_AZIMUTH), sunY = std::sin(SUN_AZIMUTH);

    // Fog look-up table: 8 entries per world unit
    static int fogLut[8 * 64 + 1];
    static float lutDensity = -1.f;
    if (lutDensity != T.fogDensity) {
        for (int i = 0; i <= 8 * 64; i++) fogLut[i] = int(256.f * std::exp(-T.fogDensity * i / 8.f));
        lutDensity = T.fogDensity;
    }
    auto fogI = [&](float d) { return fogLut[std::min(8 * 64, int(d * 8.f))]; };
    auto shadeFog = [&](int r, int g, int b, int f) -> uint32_t {
        r = std::min(255, r); g = std::min(255, g); b = std::min(255, b);
        r = hr + (((r - hr) * f) >> 8); g = hg + (((g - hg) * f) >> 8); b = hb + (((b - hb) * f) >> 8);
        return 0xFF000000 | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    };
    // Mip thresholds (depth at which the next smaller level kicks in)
    float floorFoot = (2.f * m_planeLen * m_adsFOVMult / W) * 128.f * 1.4f;
    float wallFoot = 128.f * 1.3f / m_K;
    auto mipFor = [](float d, float foot) {
        float f = d * foot;
        return f < 2.f ? 0 : f < 4.f ? 1 : f < 8.f ? 2 : 3;
    };
    const float roofLight = (T.ambient + T.sunStrength * 0.8f) * 0.85f;
    static int lightLut[256];
    if (lightLut[100] != 256) for (int i = 0; i < 256; i++) lightLut[i] = i * 256 / 100;

    // Every column is independent, so the screen is split into bands that are
    // marched in parallel on the CPU cores (each thread writes only its own columns)
    auto column = [&](int x) {
        float camX = 2.f * x / float(W) - 1.f;
        float rdX = pl.dirX + pl.planeX * m_adsFOVMult * camX, rdY = pl.dirY + pl.planeY * m_adsFOVMult * camX;
        int mapX = int(m_camX), mapY = int(m_camY);
        float ddX = std::fabs(rdX) < 1e-20f ? 1e30f : std::fabs(1.f / rdX);
        float ddY = std::fabs(rdY) < 1e-20f ? 1e30f : std::fabs(1.f / rdY);
        float sdX, sdY; int stX, stY;
        if (rdX < 0) { stX = -1; sdX = (m_camX - mapX) * ddX; } else { stX = 1; sdX = (mapX + 1.f - m_camX) * ddX; }
        if (rdY < 0) { stY = -1; sdY = (m_camY - mapY) * ddY; } else { stY = 1; sdY = (mapY + 1.f - m_camY) * ddY; }

        int ytop = H;                       // rows [ytop, H) of this column are done
        uint32_t *colPix = m_pixels + x;
        float *colDep = m_depth.data() + x;

        // Upward-facing surface between (d1,h1) and (d2,h2) along the ray.
        // kind 0 = ground of cell (cx,cy), 1 = top of a wall
        auto span = [&](float d1, float h1, float d2, float h2, int cx, int cy, int kind) {
            if (ytop <= 0 || d2 <= d1) return;
            float yN = clampf(m_hor + (m_eye - h1) * m_K / d1, -1e6f, 1e6f);
            float yF = clampf(m_hor + (m_eye - h2) * m_K / d2, -1e6f, 1e6f);
            if (yF >= yN) return;                                   // facing away
            int r0 = std::max(0, int(std::ceil(yF - 0.5f)));
            int r1 = std::min(ytop - 1, int(std::ceil(yN - 0.5f)) - 1);
            if (r1 < r0) return;
            float Dd = d2 - d1, Dh = h2 - h1, A = m_K * (m_eye - h1);
            bool acid = kind == 0 && m_acid[cy][cx];
            int wt = kind == 1 ? clampi(m_map[cy][cx], 1, 5) : 0;
            // ground texture mip for this span (from its nearest depth)
            int glvl = mipFor(d1, floorFoot), gsz = FLOOR_TS >> glvl, gmsk = gsz - 1;
            float gts = float(128 >> glvl);
            const uint32_t *gtex = ft.lv[glvl].data();
            uint32_t *pp = colPix + r1 * W;
            float *dp = colDep + r1 * W;
            for (int y = r1; y >= r0; y--, pp -= W, dp -= W) {
                // exact row → depth solve for a linear segment
                float Y = y + 0.5f - m_hor;
                float den = Y * Dd + m_K * Dh;
                float t = std::fabs(den) > 1e-6f ? clampf((A - Y * d1) / den, 0.f, 1.f) : 0.f;
                float d = d1 + Dd * t;
                float wx = m_camX + rdX * d, wy = m_camY + rdY * d;
                uint32_t out;
                if (acid) {
                    // hazard pool in the biome's colours: crossing ripple bands + bubbles;
                    // lava / coolant are self-lit and slowly flow, others take the light map
                    bool isQueenAcid = (!m_baseAcid.empty() && size_t(cy * m_mapW + cx) < m_baseAcid.size() && !m_baseAcid[cy * m_mapW + cx]);
                    uint32_t deepCol = isQueenAcid ? rgb(40, 70, 20) : m_water ? rgb(22, 44, 36) : T.hazDeep;      // the jheel: algae green
                    uint32_t lightCol = isQueenAcid ? rgb(150, 210, 60) : m_water ? rgb(62, 98, 76) : T.hazLight;
                    bool glow = isQueenAcid ? true : m_water ? false : T.hazGlow;
                    float flow = glow ? m_time * 0.25f : 0.f;
                    float w1 = std::sin(m_time * 2.1f + (wx + flow) * 5.f + wy * 3.f), w2 = std::sin(m_time * 1.3f - wx * 3.f + (wy + flow) * 6.f);
                    float w = 0.5f + 0.25f * (w1 + w2);
                    float hl = w > 0.85f ? (w - 0.85f) * 6.f : 0.f;
                    float bub = hash2(int(wx * 20.f), int(wy * 20.f), int(m_time * 3.f)) > 0.975f ? 1.f : 0.f;
                    uint32_t hc = lerpCol(deepCol, lightCol, clampf(w * 0.8f + hl * 0.5f + bub * 0.6f, 0.f, 1.f));
                    int L = glow ? 256 : lightLut[m_light[size_t(clampi(int(wy * LM), 0, m_mapH * LM - 1)) * LW + clampi(int(wx * LM), 0, LW - 1)]];
                    out = shadeFog((R(hc) * L) >> 8, (G(hc) * L) >> 8, (B(hc) * L) >> 8, glow ? std::min(256, fogI(d) + 60) : fogI(d));
                } else if (kind == 1) {
                    int lvl = mipFor(d, wallFoot);
                    int sz = WALL_TS >> lvl, msk = sz - 1;
                    uint32_t c = m_wallTex[m_theme][wt].lv[lvl][(int(wy * sz) & msk) * sz + (int(wx * sz) & msk)];
                    if (m_campus && wt == CELL_BRICK) {
                        int v = 150 + (R(c) - 128) / 8 + int(hash2(int(wx * 8.f), int(wy * 8.f), 7) * 14.f);
                        float ex = wx - std::floor(wx), ey = wy - std::floor(wy);
                        bool rim = (ex < 0.06f && !(cx > 0 && m_map[cy][cx - 1] == CELL_BRICK)) || (ex > 0.94f && !(cx + 1 < m_mapW && m_map[cy][cx + 1] == CELL_BRICK))
                                || (ey < 0.06f && !(cy > 0 && m_map[cy - 1][cx] == CELL_BRICK)) || (ey > 0.94f && !(cy + 1 < m_mapH && m_map[cy + 1][cx] == CELL_BRICK));
                        if (rim) v = 205;                                      // the parapet
                        c = rgb(v, v - 2, v - 6);
                        if (m_styles.w == m_mapW && m_styles[cy][cx] == 4)      // a canteen: clay tiles
                            c = rgb(150 + (v - 150) / 2, 70 + (v - 150) / 3, 46);
                    }
                    int L = int(roofLight * 256);
                    out = shadeFog((R(c) * L) >> 8, (G(c) * L) >> 8, (B(c) * L) >> 8, fogI(d));
                } else {
                    uint32_t c = gtex[(int(wy * gts) & gmsk) * gsz + (int(wx * gts) & gmsk)];
                    if (m_campus && !m_road[cy][cx]) {                  // the campus lawn: the ground's grain in green
                        int lum = (R(c) * 3 + G(c) * 5 + B(c) * 2) / 10;
                        int k = int(hash2(int(wx * 3.f), int(wy * 3.f), 61) * 18.f) - 9;   // patchy, sun-worn grass
                        c = 0xFF000000 | (uint32_t(clampi(46 + lum * 2 / 5 + k, 0, 255)) << 16) | (uint32_t(clampi(66 + lum / 2 + k, 0, 255)) << 8) | uint32_t(clampi(34 + lum / 4 + k / 2, 0, 255));
                    }
                    if (m_road[cy][cx]) {                               // asphalt: the ground's grain in grey
                        int lum = (R(c) * 3 + G(c) * 5 + B(c) * 2) / 10;
                        int v = 46 + lum * 2 / 5;
                        c = 0xFF000000 | (uint32_t(v) << 16) | (uint32_t(v + 1) << 8) | uint32_t(v + 4);
                    }
                    int li = clampi(int(wx * LM), 0, LW - 1), lj = clampi(int(wy * LM), 0, m_mapH * LM - 1);
                    int L = lightLut[m_light[size_t(lj) * LW + li]];
                    int r = (R(c) * L) >> 8, g = (G(c) * L) >> 8, b = (B(c) * L) >> 8;
                    int di = clampi(int(wx * DM), 0, DW - 1), dj = clampi(int(wy * DM), 0, m_mapH * DM - 1);
                    size_t didx = size_t(dj) * DW + di;
                    if (int bl = m_blood[didx]) {
                        int k = bl * 220 >> 8;
                        r = (r * (256 - k) + ((70 * L) >> 8) * k) >> 8;
                        g = (g * (256 - k) + ((4 * L) >> 8) * k) >> 8;
                        b = (b * (256 - k) + ((4 * L) >> 8) * k) >> 8;
                    }
                    if (int sc = m_scorch[didx]) { int k = 256 - (sc * 180 >> 8); r = r * k >> 8; g = g * k >> 8; b = b * k >> 8; }
                    out = shadeFog(r, g, b, fogI(d));
                }
                *pp = out;
                *dp = d;
            }
            ytop = r0;
        };

        // Vertical face at depth d from hLow up to hHigh (wall side or ledge cliff)
        auto face = [&](float d, float hLow, float hHigh, int cx, int cy, int side) {
            if (ytop <= 0) return;
            float yHigh = clampf(m_hor + (m_eye - hHigh) * m_K / d, -1e6f, 1e6f), yLow = clampf(m_hor + (m_eye - hLow) * m_K / d, -1e6f, 1e6f);
            int r0 = std::max(0, int(std::ceil(yHigh - 0.5f)));
            int r1 = std::min(ytop - 1, int(std::ceil(yLow - 0.5f)) - 1);
            if (r1 < r0) return;
            bool solid = solidCell(cx, cy);
            int wt = solid ? clampi(m_map[cy][cx], 1, 5) : 1;
            int lvl = mipFor(d, wallFoot);
            int sz = WALL_TS >> lvl, msk = sz - 1;
            bool facade = m_campus && solid && (wt == CELL_BRICK || wt == CELL_STONE);
            const int fst = facade && m_styles.w == m_mapW ? m_styles[cy][cx] : 0;
            const uint32_t *tex = facade ? m_facadeTex[fst].lv[lvl].data() : m_wallTex[m_theme][wt].lv[lvl].data();
            const uint32_t *groundTex = facade ? m_facadeGround[fst].lv[lvl].data() : tex;
            const float vScale = facade ? (fst == 11 ? 1.f / 1.6f : 1.f / STOREY_H) : 1.f;   // a gate column is one tall piece
            float bx = m_camX + rdX * d, by = m_camY + rdY * d;
            float wallX = side == 0 ? by : bx; wallX -= std::floor(wallX);
            if ((side == 0 && rdX > 0) || (side == 1 && rdY < 0)) wallX = 1.f - wallX;
            int tx = int(wallX * sz) & msk;
            int faceIdx = side == 0 ? (stX > 0 ? 0 : 1) : (stY > 0 ? 2 : 3);
            float nx = side == 0 ? -float(stX) : 0.f, ny = side == 1 ? -float(stY) : 0.f;
            float ndl = std::max(0.f, nx * sunX + ny * sunY) * std::cos(T.sunElev);
            float vis = sunVisAt(bx + nx * 0.08f, by + ny * 0.08f);
            float lit = T.ambient * (side == 1 ? 0.9f : 1.f) + T.sunStrength * 0.95f * ndl * (0.25f + 0.75f * vis);
            int f = fogI(d);
            // the texture slides down with a sinking door
            float vOff = (solid && (wt == CELL_DOOR || wt == CELL_SECRET)) ? m_doorT[cy][cx] * WALL_HEIGHT[wt] : 0.f;
            const uint8_t *dec = nullptr;
            auto it = m_wallDecals.find((cy * m_mapW + cx) * 4 + faceIdx);
            if (it != m_wallDecals.end()) dec = it->second.data();
            int du = int(wallX * WD_U);
            float zStep = d / m_K;
            float z = m_eye - (r0 + 0.5f - m_hor) * zStep;
            for (int y = r0; y <= r1; y++, z -= zStep) {
                float v = hHigh - z;
                float above = z - hLow;
                const uint32_t *tt = above < STOREY_H ? groundTex : tex;   // a building's ground floor has its own look
                uint32_t c = tt[(int((v + vOff) * vScale * sz) & msk) * sz + tx];
                if (facade && above < 0.1f && above >= 0.f) c = rgb(R(c) * 5 / 8, G(c) * 5 / 8, B(c) * 5 / 8);   // the plinth
                float ao = above < 0.35f ? 0.55f + 0.45f * std::max(0.f, above) / 0.35f : 1.f;
                int L = int(lit * ao * 256);
                int r = (R(c) * L) >> 8, g = (G(c) * L) >> 8, b = (B(c) * L) >> 8;
                if (dec) {
                    int dv = int(v * WD_U);
                    if (dv >= 0 && dv < WD_V && du >= 0 && du < WD_U) {
                        int t = dec[dv * WD_U + du];
                        if (t >= 128) { int k = 256 - ((t - 128) * 236 >> 7); r = r * k >> 8; g = g * k >> 8; b = b * k >> 8; }
                        else if (t) { r += (230 - r) * t >> 8; g += (225 - g) * t >> 8; b += (215 - b) * t >> 8; }
                    }
                }
                colPix[y * W] = shadeFog(r, g, b, f);
                colDep[y * W] = d;
            }
            ytop = r0;
        };

        float dPrev = 0.05f, hPrev = floorH(m_camX, m_camY);
        for (int g = 0; g < 256 && ytop > 0; g++) {
            float dExit = std::max(dPrev, std::min(std::min(sdX, sdY), MAXD));
            if (!solidCell(mapX, mapY)) {
                // subdivide by projected size so slopes stay smooth
                float px = m_K * 0.6f * (1.f / dPrev - 1.f / std::max(dExit, dPrev + 1e-3f));
                int n = clampi(int(px / 10.f) + 1, 1, 16);
                for (int i = 1; i <= n; i++) {
                    float dN = dPrev + (dExit - dPrev) / float(n - i + 1);
                    float sx = clampf(m_camX + rdX * dN, mapX + 1e-3f, mapX + 0.999f);
                    float sy = clampf(m_camY + rdY * dN, mapY + 1e-3f, mapY + 0.999f);
                    float hN = smoothH(sx, sy) + m_plat[mapY][mapX];
                    span(dPrev, hPrev, dN, hN, mapX, mapY, 0);
                    dPrev = dN; hPrev = hN;
                }
            } else {
                float top = cellTop(mapX, mapY);
                span(dPrev, top, dExit, top, mapX, mapY, 1);
                dPrev = dExit; hPrev = top;
            }
            if (dExit >= MAXD) break;
            int side;
            if (sdX < sdY) { sdX += ddX; mapX += stX; side = 0; } else { sdY += ddY; mapY += stY; side = 1; }
            if (mapX < 0 || mapY < 0 || mapX >= m_mapW || mapY >= m_mapH) break;
            float bx = m_camX + rdX * dExit, by = m_camY + rdY * dExit;
            float hNew = solidCell(mapX, mapY) ? cellTop(mapX, mapY) : smoothH(bx, by) + m_plat[mapY][mapX];
            if (hNew > hPrev + 1e-4f) face(std::max(dExit, 0.05f), hPrev, hNew, mapX, mapY, side);
            hPrev = hNew; dPrev = std::max(dExit, 0.05f);
        }
        m_colTop[x] = ytop;
    };
    parallelColumns(W, column);
}

// Sky above the terrain silhouette of each column
void MainWindow::renderSky()
{
    const Player &pl = m_gs.player;
    const Theme &T = m_env;
    const std::vector<uint32_t> &sky = m_sky[m_theme];
    float facing = std::atan2(pl.dirY, pl.dirX);
    static int colIdx[W], rowIdx[H];
    for (int x = 0; x < W; x++) {
        float camX = 2.f * x / W - 1.f;
        float ang = facing + std::atan(camX * m_planeLen * m_adsFOVMult);
        float u = ang / (2 * PI); u -= std::floor(u);
        colIdx[x] = std::min(SKY_W - 1, int(u * SKY_W));
    }
    for (int y = 0; y < H; y++) {
        float slope = float(m_hor - y) / m_K;
        float v = 1.f - std::atan(slope) / (PI / 2);
        rowIdx[y] = clampi(int(v * (SKY_H - 1)), 0, SKY_H - 1) * SKY_W;
    }
    const int tr = int(m_skyTint[0] * 256), tg = int(m_skyTint[1] * 256), tb = int(m_skyTint[2] * 256);
    const bool tinted = tr != 256 || tg != 256 || tb != 256;
    const float night = clampf(1.f - m_dayLevel * 1.6f, 0.f, 1.f);
    for (int x = 0; x < W; x++) {
        int top = m_colTop[x];
        for (int y = 0; y < top; y++) {
            uint32_t c = sky[rowIdx[y] + colIdx[x]];
            if (tinted) c = rgb(std::min(255, (R(c) * tr) >> 8), std::min(255, (G(c) * tg) >> 8), std::min(255, (B(c) * tb) >> 8));
            if (night > 0.f && m_weather == WX_NONE && hash2(colIdx[x], rowIdx[y] / SKY_W, 17) > 0.9965f && y < m_hor) c = lerpCol(c, rgb(235, 240, 255), night);
            m_pixels[y * W + x] = c;
            m_depth[y * W + x] = 1e30f;
        }
    }
    // Sun disc + bloom, only where sky is visible
    float d = SUN_AZIMUTH - facing;
    while (d < -PI) d += 2 * PI;
    while (d > PI) d -= 2 * PI;
    if (std::fabs(d) < 1.3f) {
        float sx = W / 2.f * (1.f + std::tan(d) / (m_planeLen * m_adsFOVMult));
        float sy = m_hor - std::tan(T.sunElev) * m_K;
        int rad = int(9.f / m_adsFOVMult), bloom = int(170 / std::sqrt(m_adsFOVMult));
        int x0 = std::max(0, int(sx) - bloom), x1 = std::min(W - 1, int(sx) + bloom);
        int y0 = std::max(0, int(sy) - bloom), y1 = std::min(H - 1, int(sy) + bloom);
        uint32_t core = lerpCol(T.sunColor, rgb(255, 255, 250), 0.7f);
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            if (y >= m_colTop[x]) continue;
            float dx = x - sx, dy = y - sy, r2 = dx * dx + dy * dy;
            if (r2 <= rad * rad) { m_pixels[y * W + x] = core; continue; }
            float b = 1.f - r2 / float(bloom * bloom);
            float c = 1.f - r2 / float(9 * rad * rad);
            float k = (b > 0 ? b * b * 0.55f : 0.f) + (c > 0 ? c * c * 0.9f : 0.f);
            if (k > 0) m_cv.add(x, y, withAlpha(T.sunColor, int(255 * clampf(k * T.sunStrength, 0.f, 1.f))));
        }
    }
}

// Pixel-block mode (raster proof): replicate the top-left pixel of each block
void MainWindow::pixelate()
{
    const int PS = m_pixelScale;
    if (PS <= 1) return;
    for (int y = 0; y < H; y += PS) for (int x = 0; x < W; x += PS) {
        uint32_t c = m_pixels[y * W + x];
        for (int by = 0; by < PS && y + by < H; by++)
            for (int bx = 0; bx < PS && x + bx < W; bx++) m_pixels[(y + by) * W + x + bx] = c;
    }
}

// Lens flare when the sun is visible (not hidden by terrain or walls)
void MainWindow::renderLensFlare()
{
    const Player &pl = m_gs.player;
    const Theme &T = m_env;
    float facing = std::atan2(pl.dirY, pl.dirX);
    float d = SUN_AZIMUTH - facing;
    while (d < -PI) d += 2 * PI;
    while (d > PI) d -= 2 * PI;
    if (std::fabs(d) > 1.2f) return;
    float sx = W / 2.f * (1.f + std::tan(d) / (m_planeLen * m_adsFOVMult));
    float sy = m_hor - std::tan(T.sunElev) * m_K;
    if (sx < 0 || sx >= W || sy < 0 || sy >= H) return;
    if (int(sy) >= m_colTop[clampi(int(sx), 0, W - 1)]) return;
    float k = T.sunStrength * (1.f - std::fabs(d) / 1.2f);
    float cx = W / 2.f, cy = H / 2.f;
    const float ts[5] = { 0.35f, 0.7f, 1.15f, 1.5f, 1.9f };
    const uint32_t tint[5] = { rgb(255, 200, 120), rgb(120, 200, 255), rgb(180, 255, 160), rgb(255, 140, 200), rgb(140, 160, 255) };
    const int rad[5] = { 22, 12, 36, 16, 58 };
    for (int i = 0; i < 5; i++) {
        int gx = int(sx + (cx - sx) * ts[i]), gy = int(sy + (cy - sy) * ts[i]);
        m_cv.glow(gx, gy, rad[i], tint[i], 0.22f * k);
    }
    for (int x = 0; x < W; x++) {
        float f = 1.f - std::fabs(x - sx) / (W * 0.6f);
        if (f <= 0) continue;
        uint32_t c = withAlpha(T.sunColor, int(70 * f * f * k));
        m_cv.add(x, int(sy), c); m_cv.add(x, int(sy) + 1, withAlpha(c, int(35 * f * f * k)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sprites — billboards standing on the terrain, z-tested per pixel
// ─────────────────────────────────────────────────────────────────────────────
static const uint8_t BAYER4[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };

void MainWindow::renderSprites()
{
    const Player &pl = m_gs.player;
    const Theme &T = m_env;
    enum SpType { SP_ENEMY, SP_KIT, SP_PICKUP, SP_EBULLET, SP_TREE };
    struct Sp { float depth; SpType t; int idx; };
    std::vector<Sp> list;
    float eplX = pl.planeX * m_adsFOVMult, eplY = pl.planeY * m_adsFOVMult;
    float invDet = 1.f / (eplX * pl.dirY - pl.dirX * eplY);
    auto depthOf = [&](float x, float y) { float sx = x - m_camX, sy = y - m_camY; return invDet * (-eplY * sx + eplX * sy); };
    auto depthAt = [&](int x, int y) { return m_depth[size_t(clampi(y, 0, H - 1)) * W + clampi(x, 0, W - 1)]; };

    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[i];
        if (!e.alive && e.corpseFade <= 0) continue;
        list.push_back({depthOf(e.x, e.y), SP_ENEMY, i});
    }
    for (int i = 0; i < int(m_gs.healthKits.size()); i++) if (m_gs.healthKits[i].active) list.push_back({depthOf(m_gs.healthKits[i].x, m_gs.healthKits[i].y), SP_KIT, i});
    for (int i = 0; i < int(m_gs.weaponPickups.size()); i++) if (m_gs.weaponPickups[i].active) list.push_back({depthOf(m_gs.weaponPickups[i].x, m_gs.weaponPickups[i].y), SP_PICKUP, i});
    for (int i = 0; i < int(m_gs.bullets.size()); i++) list.push_back({depthOf(m_gs.bullets[i].x, m_gs.bullets[i].y), SP_EBULLET, i});
    for (int i = 0; i < int(m_treeCells.size()); i++) {
        float tx = m_treeCells[i] % m_mapW + 0.5f, ty = m_treeCells[i] / m_mapW + 0.5f;
        float dd = depthOf(tx, ty);
        if (dd > 0.2f && dd < 40.f) list.push_back({dd, SP_TREE, i});
    }
    std::sort(list.begin(), list.end(), [](const Sp &a, const Sp &b) { return a.depth > b.depth; });

    float shimmer = 0.f;   // > 0: draw as a refraction shimmer (cloaked stalker / Mirage)
    // Billboard blitter: tex (ts×ts), world size, bottom at world z0
    auto blit = [&](float wx, float wy, float z0, float wsize, const uint32_t *tex, int ts, float light,
                    float flash, float fade, bool shadow) {
        float sx, sy, d;
        if (!project(wx, wy, z0, sx, sy, d) || d < 0.12f) return;
        float sz = wsize * m_K / d;
        int x0 = int(sx - sz / 2), x1 = int(sx + sz / 2), y1 = int(sy), y0 = int(sy - sz);
        int fogI = int(256 * std::exp(-T.fogDensity * d));
        const int hr = R(T.haze), hg = G(T.haze), hb = B(T.haze);
        int L = int(light * 256);
        if (shadow) {
            float fsx, fsy, fd;
            if (project(wx, wy, floorH(wx, wy), fsx, fsy, fd)) {
                float rx = sz * 0.34f, ry = std::max(2.f, sz * 0.07f);
                for (int yy = int(fsy - ry); yy <= int(fsy + ry); yy++)
                    for (int xx = int(fsx - rx); xx <= int(fsx + rx); xx++) {
                        if (xx < 0 || xx >= W || yy < 0 || yy >= H) continue;
                        if (std::fabs(m_depth[yy * W + xx] - d) > 0.6f) continue;     // only on the ground near the feet
                        float ex = (xx - fsx) / rx, ey = (yy - fsy) / ry, q = ex * ex + ey * ey;
                        if (q < 1.f) m_cv.blend(xx, yy, withAlpha(0, int(110 * (1.f - q) * fade)));
                    }
            }
        }
        for (int x = std::max(0, x0); x < std::min(W, x1); x++) {
            int tx = clampi(int((x - (sx - sz / 2)) / sz * ts), 0, ts - 1);
            for (int y = std::max(0, y0); y < std::min(H, y1); y++) {
                if (d >= m_depth[y * W + x]) continue;
                int ty = clampi(int((y - (sy - sz)) / sz * ts), 0, ts - 1);
                uint32_t c = tex[ty * ts + tx];
                uint32_t a = c >> 24;
                if (!a) continue;
                if (fade < 1.f && BAYER4[y & 3][x & 3] >= int(fade * 16)) continue;
                if (shimmer > 0.f) {
                    // cloaked: bend the pixels behind the silhouette sideways, with a faint edge
                    int off = int(std::sin(y * 0.35f + m_time * 7.f) * 4.f * shimmer);
                    uint32_t bg = m_pixels[y * W + clampi(x + off, 0, W - 1)];
                    bool edge = tx > 0 && tx < ts - 1 && ((tex[ty * ts + tx - 1] >> 24) == 0 || (tex[ty * ts + tx + 1] >> 24) == 0);
                    uint32_t sc = scaleCol(c, light);
                    uint32_t outc = lerpCol(sc, scaleCol(bg, 1.08f), shimmer);
                    if (edge) outc = lerpCol(outc, rgb(200, 230, 255), 0.35f * shimmer);
                    m_pixels[y * W + x] = outc | 0xFF000000;
                    m_depth[y * W + x] = d;
                    continue;
                }
                if (a < 255) { m_cv.add(x, y, c); continue; }          // muzzle-flash texels glow
                int r = std::min(255, (R(c) * L) >> 8), g = std::min(255, (G(c) * L) >> 8), b = std::min(255, (B(c) * L) >> 8);
                if (flash > 0) { r += int((255 - r) * flash); g += int((235 - g) * flash * 0.8f); b += int((220 - b) * flash * 0.8f); }
                r = hr + (((r - hr) * fogI) >> 8); g = hg + (((g - hg) * fogI) >> 8); b = hb + (((b - hb) * fogI) >> 8);
                m_pixels[y * W + x] = 0xFF000000 | (uint32_t(clampi(r, 0, 255)) << 16) | (uint32_t(clampi(g, 0, 255)) << 8) | uint32_t(clampi(b, 0, 255));
                m_depth[y * W + x] = d;
            }
        }
    };

    for (const Sp &s : list) {
        switch (s.t) {
        case SP_ENEMY: {
            const Enemy &e = m_gs.enemies[s.idx];
            int f = enemyFrame(e);
            float base = floorH(e.x, e.y) + e.hover;
            float light = (T.ambient + T.sunStrength * 0.8f * sunVisAt(e.x, e.y)) * 1.12f;
            float flash = e.alive && e.hitFlash > 0 ? std::min(1.f, e.hitFlash / 0.16f) * 0.35f : 0.f;
            const std::vector<uint32_t> &atlas = atlasFor(e);
            if (e.bossKind) { light *= 1.1f; if (e.shielded) flash = 0.25f + 0.15f * std::sin(m_time * 8.f); }
            shimmer = (e.alive && e.type == ET_STALKER) ? e.cloak : (e.bossKind && e.decoy == false && e.cloak < 1.f && e.cloak > 0.f) ? e.cloak : 0.f;
            if (!soldierEnemy(e))                                  // soldiers are 3D bodies (renderActors3D)
                blit(e.x, e.y, base, e.scale, &atlas[size_t(f) * ENEMY_TS * ENEMY_TS], ENEMY_TS, light, flash, e.alive ? 1.f : e.corpseFade, e.alive && e.hover <= 0.f);
            shimmer = 0.f;
            if (e.hover > 0.f && e.alive) {                       // drone: a soft shadow on the ground below
                float gx, gy, gd;
                if (project(e.x, e.y, floorH(e.x, e.y), gx, gy, gd) && gd > 0.3f) {
                    int rx = std::max(2, int(0.3f * m_K / gd)), ry = std::max(1, rx / 4);
                    for (int yy = int(gy) - ry; yy <= int(gy) + ry; yy++) for (int xx = int(gx) - rx; xx <= int(gx) + rx; xx++) {
                        if (xx < 0 || yy < 0 || xx >= W || yy >= H || std::fabs(m_depth[yy * W + xx] - gd) > 0.6f) continue;
                        float q = float((xx - gx) * (xx - gx)) / (rx * rx) + float((yy - gy) * (yy - gy)) / (ry * ry);
                        if (q < 1.f) m_cv.blend(xx, yy, withAlpha(0, int(90 * (1.f - q))));
                    }
                }
            }
            if (e.alive && e.hpShow > 0) {
                float sx, sy, d;
                if (project(e.x, e.y, base + 1.08f * e.scale, sx, sy, d) && d < depthAt(int(sx), int(sy)) + 0.5f) {
                    float a = std::min(1.f, e.hpShow / 0.4f);
                    int bw = clampi(int(70 / std::max(1.f, d * 0.4f)), 28, 70), bx = int(sx) - bw / 2, by = int(sy) - 6;
                    float frac = float(e.hp) / float(std::max(1, e.maxHp));
                    m_cv.blendRect(bx - 1, by - 1, bw + 2, 6, withAlpha(0, int(170 * a)));
                    m_cv.blendRect(bx, by, int(bw * frac), 4, withAlpha(frac > 0.5f ? rgb(90, 230, 120) : frac > 0.25f ? rgb(250, 190, 60) : rgb(240, 70, 60), int(255 * a)));
                }
            }
            break;
        }
        case SP_KIT: {
            const HealthKit &k = m_gs.healthKits[s.idx];
            float base = floorH(k.x, k.y);
            float bob = 0.06f + std::sin(k.bobPhase) * 0.04f;
            float sx, sy, d;
            if (project(k.x, k.y, base, sx, sy, d) && d > 0.1f && d < depthAt(int(sx), int(sy)) + 0.4f)
                m_cv.glow(int(sx), int(sy), int(0.35f * m_K / d), rgb(80, 255, 120), 0.35f + 0.15f * std::sin(k.bobPhase * 2));
            blit(k.x, k.y, base + bob, 0.42f, &m_pickupTex[0], PICKUP_TS, 1.15f, 0, 1.f, false);
            break;
        }
        case SP_PICKUP: {
            const WeaponPickup &wp = m_gs.weaponPickups[s.idx];
            int ti = wp.type == PICKUP_AMMO ? 1 : wp.type == PICKUP_SHOTGUN ? 2 : wp.type == PICKUP_RIFLE ? 3 : 4;
            bool model3d = wp.type == PICKUP_WEAPON || wp.type == PICKUP_THROW;   // drawn as real 3D models instead
            float base = floorH(wp.x, wp.y);
            float bob = 0.06f + std::sin(wp.bobPhase) * 0.04f;
            float sx, sy, d;
            uint32_t gc = ti == 1 ? rgb(255, 200, 80) : ti == 4 ? rgb(255, 230, 60) : rgb(90, 170, 255);
            if (wp.reward) gc = rgb(255, 205, 60);
            if (wp.type == PICKUP_WEAPON && wp.load.rarity > 0) gc = RARITY_COLORS[wp.load.rarity];
            if (wp.type == PICKUP_THROW) gc = THROWS[wp.throwKind].color;
            if (wp.type == PICKUP_KEY) gc = rgb(80, 230, 255);
            if (project(wp.x, wp.y, base, sx, sy, d) && d > 0.1f && d < depthAt(int(sx), int(sy)) + 0.4f) {
                m_cv.glow(int(sx), int(sy), int((ti == 4 || wp.reward ? 0.55f : 0.38f) * m_K / d), gc, 0.35f + 0.15f * std::sin(wp.bobPhase * 2));
                // Loot beam for boss / elite drops: an additive light column, depth-tested per pixel
                float tx, ty, td;
                if ((wp.reward || wp.type == PICKUP_KEY || (wp.type == PICKUP_WEAPON && wp.load.rarity >= 3)) && project(wp.x, wp.y, base + 2.6f, tx, ty, td)) {
                    int x0 = int(sx), y0 = int(sy), y1 = std::max(0, int(ty));
                    int half = std::max(1, int(0.05f * m_K / d));
                    for (int yy = std::min(H - 1, y0); yy >= y1; yy--) {
                        float k = float(y0 - yy) / std::max(1, y0 - y1);
                        int xx = x0 + int((tx - sx) * k);
                        for (int ox = -half * 2; ox <= half * 2; ox++) {
                            int px = xx + ox;
                            if (px < 0 || px >= W || d >= m_depth[yy * W + px]) continue;
                            float fall = 1.f - std::fabs(float(ox)) / (half * 2 + 1);
                            m_cv.add(px, yy, withAlpha(gc, int(150 * fall * fall * (1.f - k))));
                        }
                    }
                }
            }
            if (!model3d) blit(wp.x, wp.y, base + bob, ti == 1 || ti == 4 ? 0.42f : 0.62f, &m_pickupTex[size_t(ti) * PICKUP_TS * PICKUP_TS], PICKUP_TS, 1.2f, 0, 1.f, false);
            break;
        }
        case SP_TREE: {
            int cx = m_treeCells[s.idx] % m_mapW, cy = m_treeCells[s.idx] / m_mapW;
            float top = cellTop(cx, cy);
            float light = (T.ambient + T.sunStrength * 0.7f) * 1.05f;
            // a big crown that hides most of the trunk block (pines start low, broadleaf higher)
            float base = m_theme == BIO_SNOW ? floorH(cx + 0.5f, cy + 0.5f) + 0.35f : top - 0.75f;
            float crown = m_theme == BIO_SNOW ? 2.5f : 2.2f;
            if (m_campus) {                                       // a whole tree on one sprite, standing on the ground
                int var = (cx * 7 + cy * 13) % TREE_VARIANTS;
                float size = 3.0f + 0.25f * float((cx * 5 + cy * 3) % 4);
                blit(cx + 0.5f, cy + 0.5f, floorH(cx + 0.5f, cy + 0.5f), size, m_campusTreeTex[var].data(), TREE_TS, light, 0, 1.f, true);
                break;
            }
            blit(cx + 0.5f, cy + 0.5f, base, crown, m_canopyTex[m_theme].data(), CANOPY_TS, light, 0, 1.f, false);
            break;
        }
        case SP_EBULLET: {
            const Bullet &b = m_gs.bullets[s.idx];
            float hx, hy, hd, tx, ty, td;
            if (!project(b.x, b.y, b.z, hx, hy, hd) || hd < 0.15f) break;
            if (hd >= depthAt(int(hx), int(hy))) break;
            float tlx = b.x - b.dx * 0.07f, tly = b.y - b.dy * 0.07f, tlz = b.z - b.dz * 0.07f;
            if (!project(tlx, tly, tlz, tx, ty, td) || td < 0.1f) { tx = hx; ty = hy; }
            int wpx = std::max(1, int(0.03f * m_K / hd));
            m_cv.thickLine(int(tx), int(ty), int(hx), int(hy), withAlpha(rgb(255, 90, 40), 140), wpx + 2);
            m_cv.thickLine(int(tx), int(ty), int(hx), int(hy), rgb(255, 220, 160), wpx);
            m_cv.glow(int(hx), int(hy), std::max(4, int(0.12f * m_K / hd)), rgb(255, 80, 30), 0.9f);
            break;
        }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tracers: bright streaks travelling along each hitscan path
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderTracers()
{
    for (const Tracer &t : m_gs.tracers) {
        float sHead = std::min(t.t, t.len), sTail = std::max(0.f, t.t - 2.2f);
        if (sTail >= t.len || sHead <= sTail) continue;
        auto at = [&](float s, float &x, float &y, float &z) {
            float k = t.len > 0 ? s / t.len : 0; x = t.x0 + (t.x1 - t.x0) * k; y = t.y0 + (t.y1 - t.y0) * k; z = t.z0 + (t.z1 - t.z0) * k;
        };
        float ax, ay, az, bx, by, bz, ha, va, da, hb, vb, db;
        at(sTail, ax, ay, az); at(sHead, bx, by, bz);
        for (int it = 0; it < 12 && !project(ax, ay, az, ha, va, da); it++) { sTail += (sHead - sTail) * 0.3f; at(sTail, ax, ay, az); }
        if (!project(ax, ay, az, ha, va, da) || !project(bx, by, bz, hb, vb, db)) continue;
        int steps = std::max(1, int(std::max(std::fabs(hb - ha), std::fabs(vb - va))));
        if (steps > 2000) continue;
        for (int i = 0; i <= steps; i++) {
            float k = float(i) / steps;
            float invd = (1.f / da) * (1 - k) + (1.f / db) * k;        // perspective-correct depth
            float dd = 1.f / invd;
            int x = int(ha + (hb - ha) * k), y = int(va + (vb - va) * k);
            if (x < 0 || x >= W || y < 0 || y >= H || dd >= m_depth[y * W + x]) continue;
            int a = int(60 + 195 * k);
            m_cv.add(x, y, withAlpha(rgb(255, 230, 150), a));
            m_cv.add(x + 1, y, withAlpha(rgb(255, 170, 60), a / 3));
            m_cv.add(x, y + 1, withAlpha(rgb(255, 170, 60), a / 3));
        }
    }
}

void MainWindow::renderParticles()
{
    for (const Particle &pt : m_gs.particles) {
        float sx, sy, d;
        if (!project(pt.x, pt.y, pt.z, sx, sy, d) || d < 0.12f) continue;
        int x = int(sx), y = int(sy);
        if (x < 0 || x >= W || y < 0 || y >= H || d >= m_depth[y * W + x] + 0.05f) continue;
        float lf = pt.life / std::max(0.01f, pt.maxLife);
        int r = std::max(1, int(pt.size * m_K / d));
        switch (pt.type) {
        case PT_SPARK: {
            float px, py, pd;
            if (project(pt.x - pt.vx * 0.025f, pt.y - pt.vy * 0.025f, pt.z - pt.vz * 0.025f, px, py, pd))
                m_cv.line(int(px), int(py), x, y, withAlpha(pt.color, int(255 * lf)));
            m_cv.glow(x, y, r * 3 + 2, pt.color, lf);
            break;
        }
        case PT_DUST: case PT_SMOKE: {
            // smoke-grenade puffs are big and dense; fade in, then out
            float fin = pt.size > 0.3f ? std::min(1.f, (1.f - lf) * 4.f) : 1.f;
            int alpha = pt.size > 0.3f ? int(190 * lf * fin) : int((pt.type == PT_SMOKE ? 120 : 110) * lf);
            m_cv.softCircle(x, y, std::min(r, pt.size > 0.3f ? 140 : 60), withAlpha(fog(pt.color, d), alpha));
            break;
        }
            break;
        case PT_FLAME: {
            // additive fire: yellow core cooling to red, then sooty
            uint32_t c = lf > 0.5f ? lerpCol(rgb(255, 120, 30), rgb(255, 230, 140), (lf - 0.5f) * 2.f) : lerpCol(rgb(90, 30, 10), rgb(255, 120, 30), lf * 2.f);
            int rr = std::min(std::max(2, int(r * 1.5f)), 42);
            m_cv.glow(x, y, rr, c, 0.5f * std::min(1.f, lf * 1.6f));
            break;
        }
        default:
            m_cv.fillCircle(x, y, std::min(r, 6), withAlpha(fog(pt.color, d), int(255 * std::min(1.f, lf * 2.f))));
            break;
        }
    }
}

void MainWindow::renderDamageNumbers()
{
    for (const DamageNum &dn : m_gs.damageNums) {
        float sx, sy, d;
        if (!project(dn.x, dn.y, dn.z, sx, sy, d)) continue;
        float a = std::min(1.f, dn.life / 0.4f);
        float pop = 1.f + std::max(0.f, dn.life - 0.75f) * 3.f;
        int px = int((dn.head ? 22 : 16) * pop);
        QColor c = dn.head ? QColor(255, 70, 50, int(255 * a)) : QColor(255, 235, 170, int(255 * a));
        text(sx - 60, sy - px - 4, 120, px + 6, Qt::AlignCenter, QString::number(dn.value), c, px, 800);
    }
}
