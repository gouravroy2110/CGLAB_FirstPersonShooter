// =============================================================================
//  world.cpp  -  Terrain, light maps, doors & secrets, decals, hitscan, LOS
//
//  The world is a W x H grid (size comes from the layout) of cells over a continuous heightfield:
//    ground(x,y) = rolling terrain (bilinear over cell-corner heights)
//                + per-cell ledge / pit offset (vertical cliffs at cell edges)
//  Solid cells (walls, doors, secret walls) stand on that ground and rise
//  WALL_HEIGHT above it. Rendering, hitscan, line of sight, movement and the
//  baked sun-shadow map all sample this one description.
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

// ─────────────────────────────────────────────────────────────────────────────
//  Ground sampling
// ─────────────────────────────────────────────────────────────────────────────
float World::smoothH(float x, float y) const
{
    x = clampf(x, 0.f, float(m_mapW) - 0.001f); y = clampf(y, 0.f, float(m_mapH) - 0.001f);
    int ix = int(x), iy = int(y);
    float fx = x - ix, fy = y - iy;
    const int VW = m_mapW + 1;
    float a = m_vh[iy * VW + ix], b = m_vh[iy * VW + ix + 1], c = m_vh[(iy + 1) * VW + ix], d = m_vh[(iy + 1) * VW + ix + 1];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

float World::floorH(float x, float y) const
{
    int cx = clampi(int(x), 0, m_mapW - 1), cy = clampi(int(y), 0, m_mapH - 1);
    return smoothH(x, y) + m_plat[cy][cx];
}

bool World::solidCell(int cx, int cy) const
{
    if (cx < 0 || cy < 0 || cx >= m_mapW || cy >= m_mapH) return true;
    int t = m_map[cy][cx];
    if (t == CELL_FLOOR) return false;
    if ((t == CELL_DOOR || t == CELL_SECRET) && m_doorT[cy][cx] >= 0.9f) return false;
    return true;
}

float World::cellTop(int cx, int cy) const
{
    int t = m_map[cy][cx];
    float h = WALL_HEIGHT[clampi(t, 0, 5)];
    if (cx == 0 || cy == 0 || cx == m_mapW - 1 || cy == m_mapH - 1) h += 1.0f;   // tall perimeter
    if (t == CELL_DOOR || t == CELL_SECRET) h *= 1.f - m_doorT[cy][cx];
    return m_cellBase[cy][cx] + h;
}

float World::groundAt(float x, float y) const
{
    int cx = int(std::floor(x)), cy = int(std::floor(y));
    if (cx >= 0 && cy >= 0 && cx < m_mapW && cy < m_mapH && solidCell(cx, cy)) return cellTop(cx, cy);
    return floorH(x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Map loading
// ─────────────────────────────────────────────────────────────────────────────
void World::loadLayout(const ParsedMap &pm, float terrainAmp, int seed, int theme)
{
    m_theme = theme;
    m_mapW = pm.w; m_mapH = pm.h;
    m_map.resize(m_mapW, m_mapH); m_plat.resize(m_mapW, m_mapH); m_acid.resize(m_mapW, m_mapH);
    m_cellBase.resize(m_mapW, m_mapH); m_doorT.resize(m_mapW, m_mapH); m_doorMoving.resize(m_mapW, m_mapH);
    for (int y = 0; y < m_mapH; y++) for (int x = 0; x < m_mapW; x++) {
        m_map[y][x] = pm.type[y][x];
        m_plat[y][x] = pm.plat[y][x];
        m_acid[y][x] = pm.acid[y][x];
        m_doorT[y][x] = 0.f;
        m_doorMoving[y][x] = false;
    }
    // Rolling terrain: fBm noise at the cell corners
    const int VW = m_mapW + 1, VH = m_mapH + 1;
    m_vh.assign(size_t(VW) * VH, 0.f);
    for (int j = 0; j < VH; j++) for (int i = 0; i < VW; i++) {
        float n = fbm(float(i), float(j), m_mapW, 4, 3, 7 + seed * 13);
        m_vh[j * VW + i] = terrainAmp * (n - 0.5f) * 2.4f;
    }
    // Wall bases: lowest corner of the cell, raised to sit on any adjacent ledge
    for (int y = 0; y < m_mapH; y++) for (int x = 0; x < m_mapW; x++) {
        float lo = std::min({m_vh[y * VW + x], m_vh[y * VW + x + 1], m_vh[(y + 1) * VW + x], m_vh[(y + 1) * VW + x + 1]});
        float plat = 0.f;
        if (m_map[y][x] != CELL_FLOOR) {
            const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto &o : d) {
                int nx = x + o[0], ny = y + o[1];
                if (nx >= 0 && ny >= 0 && nx < m_mapW && ny < m_mapH && m_map[ny][nx] == CELL_FLOOR) plat = std::max(plat, m_plat[ny][nx]);
            }
        }
        m_cellBase[y][x] = lo + plat;
    }
    m_vhBase = m_vh;
    clearDecals();
    m_sunVis.clear(); m_light.clear();
    m_reach.clear();
}

void World::clearDecals()
{
    m_blood.assign(size_t(m_mapW * DM) * (m_mapH * DM), 0);
    m_scorch.assign(size_t(m_mapW * DM) * (m_mapH * DM), 0);
    m_wallDecals.clear();
}

void MainWindow::loadMapGeometry(int map)
{
    loadLayout(m_parsed[map], TERRAIN_AMP[map], map, MAP_THEME[map]);
    m_loadedMap = map;
    if (m_sunVisCache[map].empty()) { buildLightMap(); m_sunVisCache[map] = m_sunVis; m_lightCache[map] = m_lmAo; m_sunCache[map] = m_lmSun; }
    else { m_sunVis = m_sunVisCache[map]; m_lmAo = m_lightCache[map]; m_lmSun = m_sunCache[map]; m_light.assign(m_lmAo.size(), 100); }
    // trees: pillar cells in the swamp and the snow carry a canopy
    m_treeCells.clear();
    if (m_theme == BIO_SWAMP || m_theme == BIO_SNOW)
        for (int y = 1; y < m_mapH - 1; y++) for (int x = 1; x < m_mapW - 1; x++) if (m_map[y][x] == CELL_PILLAR) m_treeCells.push_back(y * m_mapW + x);
    if (!m_envLocked) {
        static const float HOUR[THEME_COUNT] = { 15.5f, 13.f, 16.f, 11.f, 17.2f, 23.f };
        float hour = m_todSetting == 0 ? HOUR[m_theme] : m_todSetting == 1 ? 12.f : m_todSetting == 2 ? 18.4f : 0.5f;
        m_dayCycle = m_todSetting == 4;
        int wx = m_weatherSetting == 0 ? THEMES[m_theme].weather : m_weatherSetting - 1;
        setEnvironment(hour, wx);
    } else recomposeLight();
    updateAmbience();
}

// Flood fill from (sx,sy) over floor cells and doors, but not through secret
// walls: cells reached here are places the player can get to by normal play.
void World::computeReachable(float sx, float sy)
{
    m_reach.assign(size_t(m_mapW) * m_mapH, 0);
    std::vector<int> stack;
    int x0 = clampi(int(sx), 0, m_mapW - 1), y0 = clampi(int(sy), 0, m_mapH - 1);
    stack.push_back(y0 * m_mapW + x0);
    while (!stack.empty()) {
        int id = stack.back(); stack.pop_back();
        if (m_reach[size_t(id)]) continue;
        int x = id % m_mapW, y = id / m_mapW;
        int t = m_map[y][x];
        if (t != CELL_FLOOR && t != CELL_DOOR) continue;
        m_reach[size_t(id)] = 1;
        const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto &o : d) {
            int nx = x + o[0], ny = y + o[1];
            if (nx >= 0 && ny >= 0 && nx < m_mapW && ny < m_mapH && !m_reach[size_t(ny * m_mapW + nx)]) stack.push_back(ny * m_mapW + nx);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Light map: per ground texel
//    sun visibility  — 3D ray-march toward the sun against terrain and walls
//    slope lighting  — Lambert of the terrain normal vs the sun direction
//    occlusion       — darker at wall bases, cliff bases and inside pits
//  Region-limited so opening a door only re-bakes the area around it.
// ─────────────────────────────────────────────────────────────────────────────
void World::buildLightMap(int x0, int y0, int x1, int y1)
{
    const Theme &T = THEMES[m_theme];
    const int LW = m_mapW * LM, LH = m_mapH * LM;
    if (m_sunVis.size() != size_t(LW) * LH) {
        m_sunVis.assign(size_t(LW) * LH, 255); m_light.assign(size_t(LW) * LH, 100);
        m_lmAo.assign(size_t(LW) * LH, 100); m_lmSun.assign(size_t(LW) * LH, 0);
    }
    if (x1 < 0) x1 = m_mapW;
    if (y1 < 0) y1 = m_mapH;
    x0 = clampi(x0, 0, m_mapW); x1 = clampi(x1, 0, m_mapW); y0 = clampi(y0, 0, m_mapH); y1 = clampi(y1, 0, m_mapH);
    const int pad = 2;
    int i0 = std::max(0, x0 * LM - pad), i1 = std::min(LW, x1 * LM + pad);
    int j0 = std::max(0, y0 * LM - pad), j1 = std::min(LH, y1 * LM + pad);
    int RW = i1 - i0, RH = j1 - j0;

    float ce = std::cos(T.sunElev), se = std::sin(T.sunElev), tanE = std::tan(T.sunElev);
    float sx = std::cos(SUN_AZIMUTH), sy = std::sin(SUN_AZIMUTH);
    float maxLen = std::min(12.f, 3.0f / tanE);
    std::vector<float> vis(size_t(RW) * RH, 1.f), ao(size_t(RW) * RH, 1.f), lam(size_t(RW) * RH, 1.f);

    for (int j = j0; j < j1; j++) for (int i = i0; i < i1; i++) {
        size_t r = size_t(j - j0) * RW + (i - i0);
        float wx = (i + 0.5f) / LM, wy = (j + 0.5f) / LM;
        int cx = int(wx), cy = int(wy);
        if (solidCell(cx, cy)) { vis[r] = 0; ao[r] = 0; continue; }
        float h0 = floorH(wx, wy);
        for (float t = 0.05f; t < maxLen; t += 0.06f) {
            float px = wx + sx * t, py = wy + sy * t;
            int qx = int(px), qy = int(py);
            if (qx < 0 || qy < 0 || qx >= m_mapW || qy >= m_mapH) break;
            float z = h0 + 0.02f + t * tanE;
            float occ = solidCell(qx, qy) ? cellTop(qx, qy) : floorH(px, py);
            if (occ > z) { vis[r] = 0; break; }
        }
        // terrain normal from the rolling component
        float gx = (smoothH(wx + 0.05f, wy) - smoothH(wx - 0.05f, wy)) / 0.1f;
        float gy = (smoothH(wx, wy + 0.05f) - smoothH(wx, wy - 0.05f)) / 0.1f;
        float nl = 1.f / std::sqrt(gx * gx + gy * gy + 1.f);
        float ndl = std::max(0.f, (-gx * sx * ce - gy * sy * ce + se) * nl);
        lam[r] = clampf(ndl / se, 0.f, 1.6f);
        // occlusion from nearby walls and taller neighbouring ground
        float minD = 1.f, myPlat = m_plat[cy][cx];
        for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
            int nx = cx + ox, ny = cy + oy;
            if (nx < 0 || ny < 0 || nx >= m_mapW || ny >= m_mapH) continue;
            bool occluder = solidCell(nx, ny) || m_plat[ny][nx] > myPlat + 0.2f;
            if (!occluder) continue;
            float dx = std::max({float(nx) - wx, 0.f, wx - float(nx + 1)});
            float dy = std::max({float(ny) - wy, 0.f, wy - float(ny + 1)});
            minD = std::min(minD, std::sqrt(dx * dx + dy * dy));
        }
        float a = clampf(minD / 0.45f, 0.f, 1.f);
        ao[r] = (0.45f + 0.55f * a * a * (3 - 2 * a)) * (myPlat < -0.1f ? 0.72f : 1.f);
    }
    // Soft penumbra: two 3x3 box blurs of the visibility
    for (int pass = 0; pass < 2; pass++) {
        std::vector<float> out(vis.size());
        for (int j = 0; j < RH; j++) for (int i = 0; i < RW; i++) {
            float s = 0; int n = 0;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int x = i + dx, y = j + dy;
                if (x < 0 || y < 0 || x >= RW || y >= RH) continue;
                s += vis[size_t(y) * RW + x]; n++;
            }
            out[size_t(j) * RW + i] = s / n;
        }
        vis.swap(out);
    }
    // Write back the interior of the region
    int wi0 = x0 * LM, wi1 = x1 * LM, wj0 = y0 * LM, wj1 = y1 * LM;
    for (int j = wj0; j < wj1; j++) for (int i = wi0; i < wi1; i++) {
        size_t r = size_t(j - j0) * RW + (i - i0), g = size_t(j) * LW + i;
        m_sunVis[g] = uint8_t(255 * clampf(vis[r], 0.f, 1.f));
        m_lmAo[g]  = uint8_t(clampi(int(ao[r] * 1.12f * 100.f), 0, 255));
        m_lmSun[g] = uint8_t(clampi(int(0.85f * vis[r] * lam[r] * ao[r] * 1.12f * 100.f), 0, 255));
    }
    recomposeLight(x0, y0, x1, y1);
}

// ground light = ambient x occlusion + sun strength x (sun x shadow x slope x occlusion)
void World::recomposeLight(int x0, int y0, int x1, int y1)
{
    const int LW = m_mapW * LM;
    if (m_lmAo.size() != m_light.size() || m_light.empty()) return;
    if (x1 < 0) x1 = m_mapW;
    if (y1 < 0) y1 = m_mapH;
    int a = int(m_lightAmb * 256.f), s = int(m_lightSun * 256.f);
    for (int j = y0 * LM; j < y1 * LM; j++) {
        size_t g = size_t(j) * LW + size_t(x0 * LM);
        for (int i = x0 * LM; i < x1 * LM; i++, g++)
            m_light[g] = uint8_t(std::min(255, (m_lmAo[g] * a + m_lmSun[g] * s) >> 8));
    }
}

float World::sunVisAt(float x, float y) const
{
    int i = clampi(int(x * LM), 0, m_mapW * LM - 1), j = clampi(int(y * LM), 0, m_mapH * LM - 1);
    return m_sunVis[size_t(j) * m_mapW * LM + i] / 255.f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Decals
// ─────────────────────────────────────────────────────────────────────────────
void World::addDecal(std::vector<uint8_t> &d, float x, float y, float radius, int amount)
{
    const int DW = m_mapW * DM, DH = m_mapH * DM;
    int r = int(radius * DM) + 1, cx = int(x * DM), cy = int(y * DM);
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) {
        int px = cx + i, py = cy + j;
        if (px < 0 || py < 0 || px >= DW || py >= DH) continue;
        float dist = std::sqrt(float(i * i + j * j)) / (radius * DM);
        if (dist > 1.f) continue;
        float n = hash2(px, py, 5);                      // ragged edge
        if (dist > 0.55f && n < (dist - 0.55f) * 2.2f) continue;
        uint8_t &v = d[size_t(py) * DW + px];
        v = uint8_t(std::min(255, int(v) + int(amount * (1.f - dist * 0.6f))));
    }
}

// Bullet hole on a wall face. Texel values: 128..255 = darken (hole, burn,
// cracks), 1..127 = lighten (chipped surface / dust ring).
void World::stampWallDecal(int cx, int cy, int face, float u, float v, bool pellet)
{
    std::vector<uint8_t> &d = m_wallDecals[(cy * m_mapW + cx) * 4 + face];
    if (d.empty()) d.assign(size_t(WD_U) * WD_V, 0);
    float cu = u * WD_U, cv = v * WD_U;                // same texel density both ways
    auto dark = [&](int x, int y, int val) {
        if (x < 0 || y < 0 || x >= WD_U || y >= WD_V) return;
        uint8_t &t = d[size_t(y) * WD_U + x];
        if (t < 128 || val > t) t = uint8_t(val);
    };
    auto light = [&](int x, int y, int val) {
        if (x < 0 || y < 0 || x >= WD_U || y >= WD_V) return;
        uint8_t &t = d[size_t(y) * WD_U + x];
        if (t == 0 || (t < 128 && val > t)) t = uint8_t(val);
    };
    unsigned seed = unsigned(rand());
    auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return float((seed >> 16) & 0x7FFF) / 32767.f; };
    float rh = pellet ? 1.0f : 1.5f;
    int R = 7;
    for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) {
        float dd = std::sqrt(float(dx * dx + dy * dy));
        int x = int(cu) + dx, y = int(cv) + dy;
        if (dd <= rh) dark(x, y, 255);                                   // the hole
        else if (dd <= rh + 1.2f) dark(x, y, 205);                       // burnt rim
        else if (dd <= rh + 3.5f && rnd() < 0.55f) light(x, y, int(40 + 50 * rnd()));   // chipped ring
        else if (dd <= rh + 5.5f && rnd() < 0.12f) dark(x, y, 150);      // powder speckle
    }
    // Radial cracks (Bresenham walks outward)
    int cracks = pellet ? 2 : 4;
    for (int k = 0; k < cracks; k++) {
        float a = rnd() * 6.2831853f, len = (pellet ? 3.f : 5.f) + rnd() * 6.f;
        int x0 = int(cu), y0 = int(cv), x1 = int(cu + std::cos(a) * len), y1 = int(cv + std::sin(a) * len);
        int ddx = std::abs(x1 - x0), sxs = x0 < x1 ? 1 : -1, ddy = -std::abs(y1 - y0), sys = y0 < y1 ? 1 : -1, err = ddx + ddy;
        for (int guard = 0; guard < 32; guard++) {
            dark(x0, y0, 175);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= ddy) { err += ddy; x0 += sxs; }
            if (e2 <= ddx) { err += ddx; y0 += sys; }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Movement & visibility
// ─────────────────────────────────────────────────────────────────────────────
bool World::posValid(float x, float y, float r) const
{
    auto ok = [&](float tx, float ty) { return !solidCell(int(std::floor(tx)), int(std::floor(ty))); };
    return ok(x - r, y - r) && ok(x + r, y - r) && ok(x - r, y + r) && ok(x + r, y + r);
}

// Walkable if no wall is touched and no ground under the body rises more than maxUp
bool World::canStep(float x, float y, float fromZ, float maxUp, float r, float ox, float oy) const
{
    if (!posValid(x, y, r)) return false;
    auto footMax = [&](float px, float py) {
        return std::max({floorH(px, py), floorH(px - r, py - r), floorH(px + r, py - r), floorH(px - r, py + r), floorH(px + r, py + r)});
    };
    float h = footMax(x, y);
    if (h <= fromZ + maxUp) return true;
    // Already overlapping a ledge (e.g. after dropping down beside it): allow moves that
    // keep the centre off the ledge and don't push further into higher ground
    if (ox < 0) return false;
    return floorH(x, y) <= fromZ + maxUp && h <= footMax(ox, oy) + 1e-4f;
}

bool World::traceSegment(float x0, float y0, float z0, float x1, float y1, float z1,
                         float &hx, float &hy, float &hz, float step) const
{
    float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    int n = std::max(1, int(len / step));
    for (int i = 1; i <= n; i++) {
        float t = float(i) / n, x = x0 + dx * t, y = y0 + dy * t, z = z0 + dz * t;
        if (z < groundAt(x, y)) {
            // back off to the last free sample
            float tb = float(i - 1) / n;
            hx = x0 + dx * tb; hy = y0 + dy * tb; hz = z0 + dz * tb;
            return true;
        }
    }
    return false;
}

bool World::hasLOS(float x0, float y0, float z0, float x1, float y1, float z1) const
{
    float dx = x1 - x0, dy = y1 - y0, dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.1f) return true;
    int n = int(dist / 0.15f);
    for (int i = 1; i < n; i++) {
        float t = float(i) / n, x = x0 + dx * t, y = y0 + dy * t, z = z0 + (z1 - z0) * t;
        int cx = int(std::floor(x)), cy = int(std::floor(y));
        if (solidCell(cx, cy)) { if (z < cellTop(cx, cy)) return false; }
        else if (floorH(x, y) > z) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Doors and secret walls: E starts them sinking into the ground
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::tryOpenDoor()
{
    Player &pl = m_gs.player;
    int cand[15][2] = {
        {int(pl.posX + pl.dirX * 0.7f), int(pl.posY + pl.dirY * 0.7f)},
        {int(pl.posX + pl.dirX * 1.3f), int(pl.posY + pl.dirY * 1.3f)},
        {int(pl.posX + pl.dirX * 1.9f), int(pl.posY + pl.dirY * 1.9f)},
        {int(pl.posX + pl.dirX * 2.5f), int(pl.posY + pl.dirY * 2.5f)},
        {int(pl.posX + pl.dirX * 3.1f), int(pl.posY + pl.dirY * 3.1f)},
        {int(pl.posX) + 1, int(pl.posY)}, {int(pl.posX) - 1, int(pl.posY)},
        {int(pl.posX), int(pl.posY) + 1}, {int(pl.posX), int(pl.posY) - 1},
        {int(pl.posX) + 2, int(pl.posY)}, {int(pl.posX) - 2, int(pl.posY)},
        {int(pl.posX), int(pl.posY) + 2}, {int(pl.posX), int(pl.posY) - 2},
        {int(pl.posX) + 1, int(pl.posY) + 1}, {int(pl.posX) - 1, int(pl.posY) - 1}
    };
    for (auto &c : cand) {
        int nx = c[0], ny = c[1];
        if (nx <= 0 || ny <= 0 || nx >= m_mapW - 1 || ny >= m_mapH - 1) continue;
        int t = m_map[ny][nx];
        if ((t != CELL_DOOR && t != CELL_SECRET) || m_doorMoving[ny][nx]) continue;
        m_doorMoving[ny][nx] = true;
        sfxAt(SND_DOOR, nx + 0.5f, ny + 0.5f, 0.9f);
        if (t == CELL_SECRET) sfx(SND_SECRET, 0.8f, 1.f, 0.3f);
        if (t == CELL_SECRET) {
            m_gs.secretsFound++; m_gs.zoneSecrets++;
            m_gs.score += 500;
            pushFeed("SECRET AREA FOUND  +500", 4.5f, true);
            m_secretBanner = 2.5f;
        } else pushFeed("DOOR OPENING", 2.f);
        return;
    }
}

void MainWindow::updateDoors(float dt)
{
    for (int y = 0; y < m_mapH; y++) for (int x = 0; x < m_mapW; x++) {
        if (!m_doorMoving[y][x]) continue;
        m_doorT[y][x] = std::min(1.f, m_doorT[y][x] + dt * 1.3f);
        if (m_doorT[y][x] >= 1.f) {
            m_doorMoving[y][x] = false;
            m_map[y][x] = CELL_FLOOR;
            buildLightMap(x - 8, y - 8, x + 9, y + 9);
            if (m_screen == SCR_PLAYING) computeReachable(m_parsed[m_gs.mapChoice].spawnX, m_parsed[m_gs.mapChoice].spawnY);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Hitscan — marches the exact same heightfield the renderer draws:
//  terrain slopes, ledge cliffs, wall faces, wall tops, then enemy sprites
//  tested texel-by-texel against their head / body / limb mask.
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::HitResult MainWindow::hitscan(float ax, float ay, float startDist) const
{
    HitResult res;
    const Player &pl = m_gs.player;
    float camX = 2.f * ax / W - 1.f;
    float rdX = pl.dirX + pl.planeX * m_adsFOVMult * camX, rdY = pl.dirY + pl.planeY * m_adsFOVMult * camX;
    float slope = (m_hor - ay) / m_K;                  // z(depth) = eye + slope * depth
    auto zAt = [&](float d) { return m_eye + slope * d; };
    const float MAXD = 80.f;

    int mapX = int(m_camX), mapY = int(m_camY);
    float ddX = std::fabs(rdX) < 1e-20f ? 1e30f : std::fabs(1.f / rdX);
    float ddY = std::fabs(rdY) < 1e-20f ? 1e30f : std::fabs(1.f / rdY);
    float sdX, sdY; int stX, stY;
    if (rdX < 0) { stX = -1; sdX = (m_camX - mapX) * ddX; } else { stX = 1; sdX = (mapX + 1.f - m_camX) * ddX; }
    if (rdY < 0) { stY = -1; sdY = (m_camY - mapY) * ddY; } else { stY = 1; sdY = (mapY + 1.f - m_camY) * ddY; }

    float dPrev = 0.02f, hPrev = floorH(m_camX, m_camY);
    bool found = false;
    for (int g = 0; g < 200 && !found; g++) {
        float dExit = std::min(sdX, sdY);
        if (!solidCell(mapX, mapY)) {
            int n = std::max(1, int((dExit - dPrev) / 0.2f) + 1);
            float d0 = dPrev, h0 = hPrev;
            for (int i = 1; i <= n; i++) {
                float d1 = dPrev + (dExit - dPrev) * i / n;
                float px = clampf(m_camX + rdX * d1, mapX + 1e-3f, mapX + 0.999f);
                float py = clampf(m_camY + rdY * d1, mapY + 1e-3f, mapY + 0.999f);
                float h1 = smoothH(px, py) + m_plat[mapY][mapX];
                if (zAt(d1) < h1 && d1 > startDist) {
                    float f0 = zAt(d0) - h0, f1 = zAt(d1) - h1;
                    float t = f0 - f1 != 0 ? clampf(f0 / (f0 - f1), 0.f, 1.f) : 0.f;
                    float dh = d0 + (d1 - d0) * t;
                    res.floor = true; res.depth = dh;
                    res.x = m_camX + rdX * dh; res.y = m_camY + rdY * dh; res.z = zAt(dh);
                    found = true; break;
                }
                d0 = d1; h0 = h1;
            }
            hPrev = h0;
        } else {
            float top = cellTop(mapX, mapY);
            if (zAt(dExit) < top && dExit > startDist) {  // came down onto the wall top
                float dh = slope < 0 ? std::max(dPrev, (top - m_eye) / slope) : dPrev;
                res.wall = false; res.floor = false; res.depth = dh;
                res.x = m_camX + rdX * dh; res.y = m_camY + rdY * dh; res.z = top;
                found = true;
            }
            hPrev = top;
        }
        if (found) break;
        int side;
        if (sdX < sdY) { sdX += ddX; mapX += stX; side = 0; } else { sdY += ddY; mapY += stY; side = 1; }
        if (mapX < 0 || mapY < 0 || mapX >= m_mapW || mapY >= m_mapH || dExit > MAXD) break;
        float dB = dExit, bx = m_camX + rdX * dB, by = m_camY + rdY * dB;
        float hNew = solidCell(mapX, mapY) ? cellTop(mapX, mapY) : smoothH(bx, by) + m_plat[mapY][mapX];
        float zB = zAt(dB);
        if (hNew > hPrev && zB < hNew && dB > startDist) {
            // Vertical face (wall or ledge cliff)
            res.wall = true; res.depth = dB; res.x = bx; res.y = by; res.z = zB;
            res.cx = mapX; res.cy = mapY;
            res.face = side == 0 ? (stX > 0 ? 0 : 1) : (stY > 0 ? 2 : 3);
            float wx = side == 0 ? by : bx; wx -= std::floor(wx);
            if ((side == 0 && rdX > 0) || (side == 1 && rdY < 0)) wx = 1.f - wx;
            res.u = wx; res.v = hNew - zB;
            found = true; break;
        }
        hPrev = hNew; dPrev = dB;
    }
    if (!found) {
        res.sky = true; res.depth = 1e9f;
        res.x = m_camX + rdX * 40.f; res.y = m_camY + rdY * 40.f; res.z = zAt(40.f);
    }

    // Enemies — same projection as the sprite renderer (sprite = 1x1 unit standing on the ground)
    float eplX = pl.planeX * m_adsFOVMult, eplY = pl.planeY * m_adsFOVMult;
    float invDet = 1.f / (eplX * pl.dirY - pl.dirX * eplY);
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[i];
        if (!e.alive) continue;
        float spX = e.x - m_camX, spY = e.y - m_camY;
        float txX = invDet * (pl.dirY * spX - pl.dirX * spY), txY = invDet * (-eplY * spX + eplX * spY);
        if (txY < 0.1f + m_boomCur * 0.85f || txY < startDist || txY >= res.depth) continue;
        float base = floorH(e.x, e.y) + e.hover;
        float scrX = W / 2.f * (1.f + txX / txY);
        float sz = m_K * e.scale / txY;
        float top = m_hor + (m_eye - base - e.scale) * m_K / txY;
        float u = (ax - (scrX - sz / 2)) / sz, v = (ay - top) / sz;
        if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
        int f = enemyFrame(e);
        int tx = int(u * ENEMY_TS), ty = int(v * ENEMY_TS);
        const std::vector<uint8_t> &mask = maskFor(e);
        uint8_t part = mask[size_t(f) * ENEMY_TS * ENEMY_TS + ty * ENEMY_TS + tx];
        if (!part) continue;
        res.enemy = i; res.part = HitPart(part); res.depth = txY;
        res.x = m_camX + rdX * txY; res.y = m_camY + rdY * txY; res.z = m_eye + slope * txY;
        res.wall = res.floor = res.sky = false;
    }

    // Actors (multiplayer operatives only)
    if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
        for (int i = 0; i < int(m_gs.actors.size()); i++) {
            const Actor &a = m_gs.actors[i];
            if (!a.alive) continue;
            float spX = a.posX - m_camX, spY = a.posY - m_camY;
            float txX = invDet * (pl.dirY * spX - pl.dirX * spY), txY = invDet * (-eplY * spX + eplX * spY);
            if (txY < 0.1f + m_boomCur * 0.85f || txY < startDist || txY >= res.depth) continue;
            float base = a.footZ;
            float scrX = W / 2.f * (1.f + txX / txY);
            float sz = m_K * 1.0f / txY;
            float top = m_hor + (m_eye - base - 1.0f) * m_K / txY;
            float u = (ax - (scrX - sz / 2)) / sz, v = (ay - top) / sz;
            if (u < 0.18f || u > 0.82f || v < 0.f || v > 1.0f) continue;
            HitPart part = v < 0.26f ? PART_HEAD : PART_BODY;
            res.actor = i; res.enemy = -1; res.part = part; res.depth = txY;
            res.x = m_camX + rdX * txY; res.y = m_camY + rdY * txY; res.z = m_eye + slope * txY;
            res.wall = res.floor = res.sky = false;
        }
    }
    return res;
}


// ─────────────────────────────────────────────────────────────────────────────
//  Secret walls give themselves away a little: a draft blows dust out of the
//  crack at their base, you hear a whisper the first time you come close, and
//  the cell shimmers on the minimap when you are nearby
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::updateSecretHints(float dt)
{
    const Player &pl = m_gs.player;
    m_secretDustT -= dt;
    bool puff = m_secretDustT <= 0.f;
    if (puff) m_secretDustT = 0.22f;
    const int RAD = 9;
    int px = int(pl.posX), py = int(pl.posY);
    static const int N4[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    for (int y = py - RAD; y <= py + RAD; y++) for (int x = px - RAD; x <= px + RAD; x++) {
        if (x < 1 || y < 1 || x >= m_mapW - 1 || y >= m_mapH - 1) continue;
        if (m_map[y][x] != CELL_SECRET || m_doorMoving[y][x] || m_doorT[y][x] > 0.05f) continue;
        float cx = x + 0.5f, cy = y + 0.5f;
        float d2 = (cx - pl.posX) * (cx - pl.posX) + (cy - pl.posY) * (cy - pl.posY);
        if (d2 > float(RAD * RAD)) continue;
        int key = y * m_mapW + x;
        if (d2 < 5.5f * 5.5f && std::find(m_secretHinted.begin(), m_secretHinted.end(), key) == m_secretHinted.end()) {
            m_secretHinted.push_back(key);
            pushFeed("YOU FEEL A COLD DRAFT...", 3.5f);
            sfxAt(SND_SECRET, cx, cy, 0.18f, 0.55f);
        }
        if (!puff || rand() % 3) continue;
        for (const auto &n : N4) {
            int nx = x + n[0], ny = y + n[1];
            if (solidCell(nx, ny)) continue;
            // a mote leaves the seam along this face and drifts out into the room
            float along = float(rand()) / RAND_MAX - 0.5f;
            Particle p; p.type = PT_DUST;
            p.x = cx + n[0] * 0.52f + (n[0] ? 0.f : along * 0.9f);
            p.y = cy + n[1] * 0.52f + (n[1] ? 0.f : along * 0.9f);
            p.z = groundAt(p.x, p.y) + 0.03f + 0.5f * float(rand()) / RAND_MAX;
            float sp = 0.35f + 0.35f * float(rand()) / RAND_MAX;
            p.vx = n[0] * sp + (n[0] ? 0.f : along * 0.2f); p.vy = n[1] * sp + (n[1] ? 0.f : along * 0.2f); p.vz = 0.05f;
            p.life = p.maxLife = 1.4f + 0.6f * float(rand()) / RAND_MAX;
            p.size = 0.045f;
            p.color = lerpCol(m_env.haze, rgb(225, 220, 205), 0.6f);
            m_gs.particles.push_back(p);
        }
    }
}
