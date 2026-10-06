// =============================================================================
//  assets.cpp  -  Procedural texture & sprite baking
//
//  Every texture in the game is generated here at start-up, texel by texel,
//  from value-noise, Voronoi cells and simple shaded primitives. Nothing is
//  loaded from disk.
// =============================================================================
#include "assets.h"
#include "gunmodels.h"
#include <cmath>
#include <cstring>

using namespace rs;

const Theme THEMES[3] = {
    // DUNGEON — overcast, wet stone
    { rgb(52,60,74),  rgb(128,134,140), rgb(118,124,130), rgb(235,235,225), rgb(178,182,190), rgb(84,88,98),
      0.55f, 0.55f, 0.075f, 0.95f, 0.78f },
    // ARENA — bright midday
    { rgb(34,86,168), rgb(176,200,222), rgb(172,192,210), rgb(255,244,214), rgb(250,250,252), rgb(150,160,178),
      1.00f, 0.42f, 0.040f, 0.75f, 0.42f },
    // CRYPT — dusk
    { rgb(26,20,50),  rgb(220,126,82),  rgb(96,66,74),    rgb(255,156,88),  rgb(255,178,140), rgb(70,48,76),
      0.75f, 0.36f, 0.085f, 0.30f, 0.50f },
};

static inline float smooth(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}

void MipTex::build(const std::vector<uint32_t> &base, int sz)
{
    size = sz;
    lv[0] = base;
    int s = sz;
    for (int l = 1; l < MIP_LEVELS; l++) {
        int ns = s / 2;
        lv[l].assign(size_t(ns) * ns, 0);
        const std::vector<uint32_t> &src = lv[l - 1];
        for (int y = 0; y < ns; y++) for (int x = 0; x < ns; x++) {
            uint32_t a = src[(2*y)*s + 2*x], b = src[(2*y)*s + 2*x+1], c = src[(2*y+1)*s + 2*x], d = src[(2*y+1)*s + 2*x+1];
            lv[l][y*ns + x] = rgb((R(a)+R(b)+R(c)+R(d)) >> 2, (G(a)+G(b)+G(c)+G(d)) >> 2, (B(a)+B(b)+B(c)+B(d)) >> 2);
        }
        s = ns;
    }
}

// -----------------------------------------------------------------------------
//  Sky: gradient + fBm clouds that thin toward the horizon
// -----------------------------------------------------------------------------
void bakeSky(std::vector<uint32_t> &sky, int theme)
{
    const Theme &T = THEMES[theme];
    sky.assign(size_t(SKY_W) * SKY_H, 0);
    for (int y = 0; y < SKY_H; y++) {
        float v = float(y) / float(SKY_H - 1);                 // 0 = zenith, 1 = horizon
        uint32_t base = lerpCol(T.zenith, T.horizon, std::pow(v, 1.7f));
        float persp = 1.f / (1.08f - v);                       // clouds squash toward horizon
        for (int x = 0; x < SKY_W; x++) {
            float cy = persp * 30.f;
            float n  = fbm(float(x), cy, SKY_W, 6, 5, 11 + theme);
            float n2 = fbm(float(x) + 9.f, cy - 6.f, SKY_W, 6, 4, 11 + theme);
            float thr = 1.f - T.cloudCover;
            float c = smooth(thr - 0.08f, thr + 0.22f, n);
            c *= smooth(1.0f, 0.72f, v) ;                      // dissolve into haze
            float lit = clampf(0.5f + (n - n2) * 4.f, 0.f, 1.f);
            uint32_t cc = lerpCol(T.cloudDark, T.cloudLit, lit);
            uint32_t col = lerpCol(base, cc, c * 0.92f);
            // faint high-altitude streaks
            float st = vnoise(float(x) * 64.f / SKY_W, persp * 0.6f, 64, 77 + theme);
            col = lerpCol(col, T.cloudLit, smooth(0.78f, 0.95f, st) * 0.18f * (1.f - v));
            sky[size_t(y) * SKY_W + x] = col;
        }
    }
}

// -----------------------------------------------------------------------------
//  Floor textures (256×256 = 2×2 world units, tileable)
// -----------------------------------------------------------------------------
// Tileable Voronoi: distance to nearest and second-nearest feature points
static void voronoi(float x, float y, int cells, int size, int seed, float &d1, float &d2, int &id)
{
    float cs = float(size) / cells;
    int cx = int(x / cs), cy = int(y / cs);
    d1 = d2 = 1e9f; id = 0;
    for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
        int gx = cx + ox, gy = cy + oy;
        int wx = ((gx % cells) + cells) % cells, wy = ((gy % cells) + cells) % cells;
        float fx = (gx + 0.15f + 0.7f * hash2(wx, wy, seed)) * cs;
        float fy = (gy + 0.15f + 0.7f * hash2(wx, wy, seed + 1)) * cs;
        float d = std::sqrt((x - fx) * (x - fx) + (y - fy) * (y - fy));
        if (d < d1) { d2 = d1; d1 = d; id = wy * cells + wx; }
        else if (d < d2) d2 = d;
    }
}

void bakeFloor(std::vector<uint32_t> &tex, int theme)
{
    const int S = FLOOR_TS;
    tex.assign(size_t(S) * S, 0);
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float fx = float(x), fy = float(y);
        uint32_t c = 0;
        if (theme == 1) {
            // Grass & dirt patches with pebbles
            float patch = fbm(fx, fy, S, 3, 5, 3);
            float g = smooth(0.44f, 0.56f, patch);
            float fine = fbm(fx, fy, S, 32, 3, 5);
            float blade = hash2(x, y, 9);
            uint32_t grass = rgb(int(62 + 34 * fine + 26 * blade), int(88 + 40 * fine + 34 * blade), int(34 + 14 * fine));
            float dn = fbm(fx, fy, S, 16, 4, 21);
            uint32_t dirt = rgb(int(112 + 40 * dn), int(92 + 30 * dn), int(64 + 22 * dn));
            // pebbles in the dirt
            float d1, d2; int id;
            voronoi(fx, fy, 24, S, 31, d1, d2, id);
            if (hash2(id, 3, 7) > 0.55f && d1 < 2.2f + 1.5f * hash2(id, 4, 7)) {
                int v = int(140 + 50 * hash2(id, 5, 7));
                dirt = rgb(v, v - 6, v - 14);
            }
            c = lerpCol(dirt, grass, g);
            // large-scale darkening for variation
            c = scaleCol(c, 0.82f + 0.3f * fbm(fx, fy, S, 2, 2, 41));
        } else if (theme == 0) {
            // Cobblestones with moss in the joints
            float d1, d2; int id;
            voronoi(fx, fy, 8, S, 17, d1, d2, id);
            float edge = d2 - d1;
            float n = fbm(fx, fy, S, 32, 3, 19);
            int base = int(92 + 34 * hash2(id, 1, 3));
            uint32_t stone = rgb(base + int(18 * n), base + int(16 * n) - 2, base + int(12 * n) - 6);
            float bevel = smooth(0.f, 9.f, edge);                 // rounded tops
            stone = scaleCol(stone, 0.55f + 0.5f * bevel);
            float moss = fbm(fx, fy, S, 8, 3, 23);
            uint32_t joint = lerpCol(rgb(38, 36, 32), rgb(52, 74, 36), smooth(0.45f, 0.65f, moss));
            c = edge < 2.6f ? joint : lerpCol(joint, stone, smooth(2.6f, 4.0f, edge));
            // wet sheen patches
            c = lerpCol(c, rgb(150, 160, 170), smooth(0.68f, 0.8f, fbm(fx, fy, S, 4, 3, 27)) * 0.18f);
        } else {
            // Large flagstones with cracks
            int row = y / 64;
            int off = (row & 1) ? 48 : 0;
            int sx = ((x + off) % 96), sy = y % 64;
            int slabId = ((x + off) / 96) * 7 + row * 13;
            float n = fbm(fx, fy, S, 16, 4, 51);
            int base = int(62 + 26 * hash2(slabId, 2, 5));
            uint32_t slab = rgb(base + int(26 * n), base + int(24 * n), base + int(30 * n) + 6);
            float ex = std::min(float(sx), float(95 - sx)), ey = std::min(float(sy), float(63 - sy));
            float e = std::min(ex, ey);
            slab = scaleCol(slab, 0.6f + 0.4f * smooth(0.f, 6.f, e));
            // crack: thin noise ridge
            float cr = std::fabs(fbm(fx, fy, S, 8, 4, 57) - 0.5f);
            if (cr < 0.012f) slab = scaleCol(slab, 0.45f);
            float moss = fbm(fx, fy, S, 8, 3, 59);
            uint32_t joint = lerpCol(rgb(26, 24, 26), rgb(44, 60, 34), smooth(0.5f, 0.7f, moss));
            c = e < 1.5f ? joint : slab;
        }
        tex[size_t(y) * S + x] = c;
    }
}

// -----------------------------------------------------------------------------
//  Wall textures (128×128 per world unit)
// -----------------------------------------------------------------------------
void bakeWall(std::vector<uint32_t> &tex, int type, int theme)
{
    const int S = WALL_TS;
    if (type == 5) {
        // Secret wall: the stone texture plus a faint doorway seam, cracks, and an engraved ancient sigil
        bakeWall(tex, 1, theme);
        for (int y = 14; y < S; y++) for (int x = 22; x < S - 22; x++) {
            bool seam = (x == 22 || x == S - 23 || y == 14);
            if (seam && hash2(x, y, 401) > 0.25f) tex[size_t(y) * S + x] = scaleCol(tex[size_t(y) * S + x], 0.72f);
        }
        for (int c = 0; c < 4; c++) {
            float x = 30.f + 68.f * hash2(c, 1, 403), y = 20.f + 90.f * hash2(c, 2, 403);
            for (int k = 0; k < 40; k++) {
                int ix = int(x), iy = int(y);
                if (ix >= 0 && iy >= 0 && ix < S && iy < S) tex[size_t(iy) * S + ix] = scaleCol(tex[size_t(iy) * S + ix], 0.55f);
                x += hash2(c, k, 405) * 2.f - 1.f; y += 1.f;
            }
        }
        // Engraved mystic diamond sigil at the center of the secret wall
        const int cx = S / 2, cy = S / 2;
        for (int dy = -18; dy <= 18; dy++) for (int dx = -18; dx <= 18; dx++) {
            int d = std::abs(dx) + std::abs(dy);
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= S || py < 0 || py >= S) continue;
            // Outer diamond ring and inner eye/dot
            if (d == 16 || d == 15 || d == 8 || d == 7 || (d <= 2)) {
                uint32_t orig = tex[size_t(py) * S + px];
                // Deep carved groove with subtle amber/gold tint
                uint32_t carved = scaleCol(orig, 0.40f);
                carved = lerpCol(carved, rgb(215, 170, 75), 0.35f);
                tex[size_t(py) * S + px] = carved;
            } else if (d == 17) {
                // Highlighted chisel edge on the outside
                tex[size_t(py) * S + px] = scaleCol(tex[size_t(py) * S + px], 1.25f);
            }
        }
        return;
    }
    tex.assign(size_t(S) * S, 0);
    uint32_t stoneCol[3] = { rgb(112,114,104), rgb(180,152,112), rgb(84,82,92) };
    uint32_t brickCol[3] = { rgb(130,62,46),  rgb(176,120,82),  rgb(98,54,50) };
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float fx = float(x), fy = float(y);
        uint32_t c = 0;
        switch (type) {
        case 1: { // Big irregular stone blocks
            float wob = (fbm(fx, fy, S, 8, 2, 61) - 0.5f) * 6.f;
            int rowH = 32;
            int row = int((fy + wob) / rowH);
            int off = (row & 1) ? 22 : 0;
            int bw = 48 + int(16 * hash2(row, 0, 63));
            float lx = std::fmod(fx + off + wob + S, float(bw));
            float ly = std::fmod(fy + wob + S, float(rowH));
            int bid = int((fx + off + wob + S) / bw) * 17 + row * 31;
            float e = std::min(std::min(lx, bw - lx), std::min(ly, rowH - ly));
            float n = fbm(fx, fy, S, 16, 4, 67 + bid % 5);
            uint32_t st = scaleCol(stoneCol[theme], 0.78f + 0.3f * hash2(bid, 1, 65) + 0.35f * (n - 0.5f));
            // bevel: lit top edge, shadowed bottom edge
            float shade = 1.f;
            if (ly < 4) shade = 1.18f; else if (rowH - ly < 4) shade = 0.7f;
            st = scaleCol(st, shade);
            c = e < 1.6f ? rgb(46, 44, 40) : st;
            if (theme != 1) {
                float moss = fbm(fx, fy, S, 8, 4, 69) + fy / S * 0.45f;
                c = lerpCol(c, rgb(58, 82, 40), smooth(0.78f, 0.95f, moss) * 0.85f);
            }
            break;
        }
        case 2: { // Brick
            int bh = 16, bw = 32;
            int row = y / bh;
            int off = (row & 1) ? bw / 2 : 0;
            int lx = (x + off) % bw, ly = y % bh;
            int bid = ((x + off) / bw) * 7 + row * 11;
            bool mortar = (ly < 2) || (lx < 2);
            float n = fbm(fx, fy, S, 32, 3, 71);
            uint32_t br = scaleCol(brickCol[theme], 0.78f + 0.35f * hash2(bid, 2, 73) + 0.3f * (n - 0.5f));
            if (ly == 2) br = scaleCol(br, 1.15f);
            if (ly == bh - 1) br = scaleCol(br, 0.78f);
            c = mortar ? rgb(int(150 + 30 * n), int(144 + 30 * n), int(132 + 26 * n)) : br;
            // soot / grime from the ground up
            float grime = smooth(0.4f, 1.0f, fy / S) * (0.5f + 0.5f * fbm(fx, fy, S, 4, 3, 75));
            c = scaleCol(c, 1.f - 0.35f * grime);
            break;
        }
        case 3: { // Concrete pillar with stains and form-tie holes
            float n = fbm(fx, fy, S, 16, 5, 81);
            float streak = fbm(fx, fy * 0.08f, S, 16, 3, 83);
            int v = int(128 + 50 * (n - 0.5f));
            c = rgb(v, v, v + 4);
            c = scaleCol(c, 1.f - 0.3f * smooth(0.5f, 0.75f, streak));
            if (x % 64 < 1 || y % 64 < 1) c = scaleCol(c, 0.65f);
            int hx = x % 32 - 16, hy = y % 32 - 16;
            if (hx * hx + hy * hy < 5) c = rgb(40, 40, 44);
            if (theme == 0) c = lerpCol(c, rgb(70, 90, 70), smooth(0.7f, 0.9f, n + fy / S * 0.3f) * 0.6f);
            break;
        }
        default: { // Door: vertical planks, iron bands, rivets, ring handle
            int plank = x / 21;
            float grain = fbm(fx * 0.25f + plank * 40.f, fy * 2.f, S, 16, 4, 91 + plank);
            uint32_t wood = rgb(int(104 + 50 * grain), int(64 + 30 * grain), int(34 + 16 * grain));
            if (x % 21 < 1) wood = rgb(34, 22, 14);
            c = wood;
            bool band = (y > 18 && y < 30) || (y > 98 && y < 110);
            if (band) {
                float m = fbm(fx, fy, S, 32, 3, 95);
                c = rgb(int(58 + 30 * m), int(56 + 28 * m), int(58 + 26 * m));
                if (y == 19 || y == 99) c = rgb(110, 108, 104);
                int rx = x % 16 - 8, ry = (y < 64 ? y - 24 : y - 104);
                if (rx * rx + ry * ry < 6) c = rx + ry < 0 ? rgb(150, 146, 140) : rgb(70, 68, 66);
            }
            int hx = x - 96, hy = y - 66;
            float hd = std::sqrt(float(hx * hx + hy * hy));
            if (hd > 6.f && hd < 9.f) c = hy < 0 ? rgb(160, 150, 120) : rgb(96, 88, 70);
            if (hd < 3.f) c = rgb(40, 38, 36);
            break;
        }
        }
        tex[size_t(y) * S + x] = c;
    }
}

// -----------------------------------------------------------------------------
//  Soldier painter — shaded primitives (sphere/capsule/box normals + Lambert)
// -----------------------------------------------------------------------------
const SoldierLook ENEMY_LOOK = {
    rgb(104,108,74), rgb(70,74,50), rgb(122,104,74), rgb(86,90,60), rgb(62,66,44),
    rgb(84,90,62), rgb(196,150,116), rgb(46,46,48), rgb(120,200,240), rgb(40,34,30), rgb(36,36,38)
};

const SoldierLook ELITE_LOOK = {
    rgb(46,48,54), rgb(26,27,32), rgb(70,72,80), rgb(30,32,36), rgb(20,21,24),
    rgb(34,36,40), rgb(180,136,104), rgb(20,20,22), rgb(255,60,40), rgb(22,20,20), rgb(18,18,20)
};

// Fast Scout: light urban grey-blue kit with cyan goggles
const SoldierLook SCOUT_LOOK = {
    rgb(72,78,92), rgb(44,48,60), rgb(96,102,118), rgb(46,50,62), rgb(32,34,44),
    rgb(52,56,68), rgb(205,160,126), rgb(36,38,44), rgb(60,220,255), rgb(30,32,38), rgb(28,30,34)
};

// Heavy Shotgunner: rust / heavy brown armor with dark yellow goggles
const SoldierLook SHOTGUN_LOOK = {
    rgb(118,82,54), rgb(80,52,32), rgb(140,102,68), rgb(92,62,40), rgb(64,42,26),
    rgb(102,70,44), rgb(188,142,108), rgb(48,40,34), rgb(255,190,50), rgb(38,32,26), rgb(34,30,26)
};

// Deadly Sniper: hooded woodland ghillie / dark green with crimson lens
const SoldierLook SNIPER_LOOK = {
    rgb(54,68,48), rgb(34,44,30), rgb(74,90,64), rgb(40,52,36), rgb(26,34,22),
    rgb(44,56,38), rgb(175,135,102), rgb(30,36,28), rgb(240,40,60), rgb(24,28,22), rgb(22,26,20)
};

SoldierLook lookForClass(int cls)
{
    SoldierLook l = ENEMY_LOOK;
    if (cls == 0) { l.uniform = rgb(88,98,64); l.uniformDark = rgb(56,64,40); l.uniformAlt = rgb(110,96,66);
                    l.vest = rgb(70,78,50); l.vestDark = rgb(50,56,36); l.helmet = rgb(72,80,54); l.lens = rgb(255,170,60); }
    if (cls == 1) { l.uniform = rgb(62,64,74); l.uniformDark = rgb(36,38,46); l.uniformAlt = rgb(88,90,100);
                    l.vest = rgb(34,36,42); l.vestDark = rgb(24,26,30); l.helmet = rgb(40,42,48); l.lens = rgb(90,220,255); }
    if (cls == 2) { l.uniform = rgb(140,112,78); l.uniformDark = rgb(104,80,52); l.uniformAlt = rgb(160,136,100);
                    l.vest = rgb(96,78,52); l.vestDark = rgb(70,56,36); l.helmet = rgb(112,92,62); l.lens = rgb(255,90,70); }
    return l;
}

namespace {
struct SP {
    uint32_t *px; uint8_t *mask; int size; float s; const SoldierLook *L;

    void plot(int x, int y, uint32_t c, uint8_t part) {
        if (unsigned(x) >= unsigned(size) || unsigned(y) >= unsigned(size)) return;
        px[y * size + x] = c | 0xFF000000;
        if (mask) mask[y * size + x] = part;
    }
    uint32_t camo(float fx, float fy, uint32_t base) const {
        float n  = vnoise(fx * 0.24f, fy * 0.24f, 64, 7);
        float n2 = vnoise(fx * 0.32f + 5.f, fy * 0.32f, 64, 13);
        if (n > 0.63f) return L->uniformDark;
        if (n2 > 0.66f) return L->uniformAlt;
        return base;
    }
    static uint32_t light(uint32_t c, float nx, float ny, float nz) {
        const float lx = -0.45f, ly = -0.58f, lz = 0.68f;
        float d = std::max(0.f, nx * lx + ny * ly + nz * lz);
        float spec = std::pow(std::max(0.f, d), 24.f) * 0.25f;
        float f = 0.40f + 0.75f * d;
        return rgb(int(R(c) * f + 255 * spec), int(G(c) * f + 255 * spec), int(B(c) * f + 255 * spec));
    }
    template <class F> void bbox(float x0, float y0, float x1, float y1, F fn) {
        int a = std::max(0, int(std::floor(x0 * s))), b = std::min(size - 1, int(std::ceil(x1 * s)));
        int c = std::max(0, int(std::floor(y0 * s))), d = std::min(size - 1, int(std::ceil(y1 * s)));
        for (int y = c; y <= d; y++) for (int x = a; x <= b; x++) fn(x, y, (x + 0.5f) / s, (y + 0.5f) / s);
    }
    void ellipse(float cx, float cy, float rx, float ry, uint32_t col, uint8_t part, bool cam = false, float clipY = 1e9f) {
        bbox(cx - rx, cy - ry, cx + rx, cy + ry, [&](int x, int y, float fx, float fy) {
            float dx = (fx - cx) / rx, dy = (fy - cy) / ry, d = dx * dx + dy * dy;
            if (d > 1.f || fy > clipY) return;
            plot(x, y, light(cam ? camo(fx, fy, col) : col, dx, dy, std::sqrt(1.f - d)), part);
        });
    }
    void capsule(float x0, float y0, float x1, float y1, float r, uint32_t col, uint8_t part, bool cam = false) {
        float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
        bbox(std::min(x0, x1) - r, std::min(y0, y1) - r, std::max(x0, x1) + r, std::max(y0, y1) + r,
             [&](int x, int y, float fx, float fy) {
            float t = L2 > 0 ? clampf(((fx - x0) * vx + (fy - y0) * vy) / L2, 0.f, 1.f) : 0.f;
            float dx = fx - (x0 + vx * t), dy = fy - (y0 + vy * t), d = std::sqrt(dx * dx + dy * dy);
            if (d > r) return;
            float nx = dx / r, ny = dy / r;
            plot(x, y, light(cam ? camo(fx, fy, col) : col, nx, ny, std::sqrt(std::max(0.f, 1.f - nx * nx - ny * ny))), part);
        });
    }
    void box(float x0, float y0, float x1, float y1, uint32_t col, uint8_t part, bool cam = false) {
        bbox(x0, y0, x1, y1, [&](int x, int y, float fx, float fy) {
            if (fx < x0 || fx > x1 || fy < y0 || fy > y1) return;
            float u = (fx - x0) / (x1 - x0) * 2 - 1, v = (fy - y0) / (y1 - y0) * 2 - 1;
            float nx = (u < 0 ? -1 : 1) * std::pow(std::fabs(u), 4.f) * 0.85f;
            float ny = (v < 0 ? -1 : 1) * std::pow(std::fabs(v), 4.f) * 0.85f;
            plot(x, y, light(cam ? camo(fx, fy, col) : col, nx, ny, std::sqrt(std::max(0.05f, 1.f - nx * nx - ny * ny))), part);
        });
    }
    // Muzzle flash: additive, also writes into transparent texels with partial alpha
    void flash(float cx, float cy, float r) {
        bbox(cx - r, cy - r, cx + r, cy + r, [&](int x, int y, float fx, float fy) {
            float d = ((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) / (r * r);
            float ang = std::atan2(fy - cy, fx - cx);
            float rays = 0.55f + 0.45f * std::cos(ang * 6.f);
            float f = std::max(0.f, 1.f - d / rays);
            if (f <= 0) return;
            f = f * f;
            uint32_t &p = px[y * size + x];
            uint32_t fc = rgb(255, int(200 + 55 * f), int(90 + 150 * f));
            if ((p >> 24) == 0) p = withAlpha(fc, int(255 * std::min(1.f, f * 1.6f)));
            else p = rgb(std::min(255, R(p) + int(255 * f)), std::min(255, G(p) + int(210 * f)), std::min(255, B(p) + int(120 * f)));
        });
    }
};
}

void paintSoldier(uint32_t *px, uint8_t *mask, int size, const SoldierLook &look, SoldierPose pose, float walk, bool bulky)
{
    std::fill(px, px + size * size, 0u);
    if (mask) std::fill(mask, mask + size * size, uint8_t(0));
    SP p{px, mask, size, float(size) / 64.f, &look};
    const uint8_t BODY = 1, HEAD = 2, LIMB = 3;

    float sw = (pose == POSE_WALK) ? std::sin(walk * 6.2831853f) : 0.f;
    float by = (pose == POSE_WALK) ? -std::fabs(sw) * 0.9f : 0.f;   // body bob
    float bw = bulky ? 1.8f : 0.f;

    // Legs + boots
    float fLx = 27.f + sw * 4.5f, fLy = 58.f - std::max(0.f, sw) * 2.5f;
    float fRx = 37.f - sw * 4.5f, fRy = 58.f - std::max(0.f, -sw) * 2.5f;
    p.capsule(28, 37 + by, fLx, fLy, 4.1f, look.uniform, LIMB, true);
    p.capsule(36, 37 + by, fRx, fRy, 4.1f, look.uniform, LIMB, true);
    p.ellipse((28 + fLx) / 2, (37 + by + fLy) / 2 + 1, 2.8f, 2.4f, look.vestDark, LIMB);
    p.ellipse((36 + fRx) / 2, (37 + by + fRy) / 2 + 1, 2.8f, 2.4f, look.vestDark, LIMB);
    p.box(fLx - 4.5f, fLy - 1.5f, fLx + 4.2f, std::min(63.9f, fLy + 5.5f), look.boots, LIMB);
    p.box(fRx - 4.2f, fRy - 1.5f, fRx + 4.5f, std::min(63.9f, fRy + 5.5f), look.boots, LIMB);

    // Torso, vest, pouches, belt
    p.box(22 - bw, 16 + by, 42 + bw, 36 + by, look.uniform, BODY, true);
    p.box(23 - bw, 18 + by, 41 + bw, 34 + by, look.vest, BODY);
    p.box(24 - bw, 27 + by, 29, 33 + by, look.vestDark, BODY);
    p.box(30, 27 + by, 34, 33 + by, look.vestDark, BODY);
    p.box(35, 27 + by, 40 + bw, 33 + by, look.vestDark, BODY);
    p.box(25, 18 + by, 27.5f, 26 + by, look.vestDark, BODY);
    p.box(36.5f, 18 + by, 39, 26 + by, look.vestDark, BODY);
    p.box(23, 34 + by, 41, 38 + by, rgb(42, 36, 28), BODY);
    p.box(30.5f, 34.5f + by, 33.5f, 37.5f + by, rgb(140, 132, 112), BODY);
    // Shoulders
    p.ellipse(22 - bw, 19.5f + by, 5.f, 4.5f, look.uniform, BODY, true);
    p.ellipse(42 + bw, 19.5f + by, 5.f, 4.5f, look.uniform, BODY, true);

    const uint32_t gunCol = rgb(34, 35, 38), gunDark = rgb(22, 22, 24);
    if (pose == POSE_WALK) {
        // Low-ready: rifle diagonal across the body
        p.capsule(42 + bw, 20 + by, 44 + bw, 28 + by, 3.6f, look.uniform, LIMB, true);
        p.capsule(44, 25 + by, 16, 36 + by, 1.9f, gunCol, BODY);
        p.box(31, 30 + by, 34.5f, 36.5f + by, gunDark, BODY);
        p.box(34, 25.5f + by, 40, 28.5f + by, gunDark, BODY);
        p.capsule(44 + bw, 28 + by, 37, 30 + by, 3.1f, look.uniform, LIMB, true);
        p.ellipse(37, 30 + by, 2.6f, 2.4f, look.gloves, LIMB);
        p.capsule(22 - bw, 20 + by, 20 - bw, 28 + by, 3.6f, look.uniform, LIMB, true);
        p.capsule(20 - bw, 28 + by, 24, 34 + by, 3.1f, look.uniform, LIMB, true);
        p.ellipse(24, 34 + by, 2.6f, 2.4f, look.gloves, LIMB);
    } else {
        // Shouldered: rifle pointing at the viewer (foreshortened)
        p.capsule(42 + bw, 20, 45 + bw, 27, 3.6f, look.uniform, LIMB, true);
        p.box(29, 19, 41, 26, gunCol, BODY);
        p.box(35, 25, 38.5f, 31.5f, gunDark, BODY);
        p.box(33, 16.5f, 40, 19.5f, gunDark, BODY);
        p.ellipse(34.5f, 18, 1.4f, 1.4f, look.lens, BODY);
        p.ellipse(31, 22, 2.9f, 2.9f, rgb(44, 45, 48), BODY);
        p.ellipse(31, 22, 1.3f, 1.3f, rgb(6, 6, 6), BODY);
        p.capsule(45 + bw, 27, 38, 25.5f, 3.1f, look.uniform, LIMB, true);
        p.ellipse(38, 25.5f, 2.6f, 2.4f, look.gloves, LIMB);
        p.capsule(22 - bw, 20, 20 - bw, 27, 3.6f, look.uniform, LIMB, true);
        p.capsule(20 - bw, 27, 29, 23.5f, 3.1f, look.uniform, LIMB, true);
        p.ellipse(29, 23.5f, 2.6f, 2.4f, look.gloves, LIMB);
    }

    // Neck, face, goggles, scarf, helmet
    float hy = by;
    p.box(29, 13 + hy, 35, 18 + hy, look.mask, BODY);
    p.ellipse(32, 10.5f + hy, 6.f, 6.6f, look.mask, HEAD);
    p.ellipse(32, 11.f + hy, 4.6f, 4.4f, look.skin, HEAD);
    p.box(26.6f, 12.6f + hy, 37.4f, 17.2f + hy, look.mask, HEAD);
    p.box(25.6f, 8.f + hy, 38.4f, 11.4f + hy, rgb(24, 24, 26), HEAD);
    p.ellipse(29.f, 9.7f + hy, 2.6f, 1.6f, look.lens, HEAD);
    p.ellipse(35.f, 9.7f + hy, 2.6f, 1.6f, look.lens, HEAD);
    p.ellipse(32, 6.8f + hy, 7.8f, 5.9f, look.helmet, HEAD, false, 8.6f + hy);
    p.box(24.2f, 7.4f + hy, 39.8f, 9.0f + hy, scaleCol(look.helmet, 0.8f), HEAD);
    p.box(24.8f, 5.0f + hy, 39.2f, 6.0f + hy, scaleCol(look.helmet, 0.7f), HEAD);
    p.box(30.6f, 2.2f + hy, 33.4f, 4.8f + hy, rgb(26, 26, 26), HEAD);

    if (pose == POSE_SHOOT) p.flash(31, 22, 9.f);

    // Rim darkening: texels bordering transparency get darker (reads as an outline)
    std::vector<uint32_t> copy(px, px + size * size);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        uint32_t c = copy[y * size + x];
        if ((c >> 24) != 255) continue;
        bool edge = false;
        for (int k = 0; k < 4 && !edge; k++) {
            int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
            if (nx < 0 || ny < 0 || nx >= size || ny >= size || (copy[ny * size + nx] >> 24) == 0) edge = true;
        }
        if (edge) px[y * size + x] = scaleCol(c, 0.55f);
    }
}

// Rotate a frame about a pivot (nearest-neighbour resampling)
static void rotateFrame(const uint32_t *src, const uint8_t *sm, uint32_t *dst, uint8_t *dm, int S,
                        float deg, float px, float py, float dx, float dy)
{
    float a = deg * 3.14159265f / 180.f, ca = std::cos(a), sa = std::sin(a);
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float rx = x + 0.5f - dx, ry = y + 0.5f - dy;
        int sx = int(std::floor(px + rx * ca + ry * sa)), sy = int(std::floor(py - rx * sa + ry * ca));
        if (sx < 0 || sy < 0 || sx >= S || sy >= S) { dst[y * S + x] = 0; dm[y * S + x] = 0; continue; }
        dst[y * S + x] = src[sy * S + sx]; dm[y * S + x] = sm[sy * S + sx];
    }
}

void bakeEnemyAtlas(std::vector<uint32_t> &atlas, std::vector<uint8_t> &mask, const SoldierLook &look)
{
    const int S = ENEMY_TS, F = S * S;
    atlas.assign(size_t(EF_COUNT) * F, 0);
    mask.assign(size_t(EF_COUNT) * F, 0);
    bool bulky = &look == &ELITE_LOOK;
    paintSoldier(&atlas[EF_AIM * F], &mask[EF_AIM * F], S, look, POSE_AIM, 0, bulky);
    for (int i = 0; i < 4; i++)
        paintSoldier(&atlas[(EF_WALK0 + i) * F], &mask[(EF_WALK0 + i) * F], S, look, POSE_WALK, i * 0.25f, bulky);
    paintSoldier(&atlas[EF_SHOOT * F], &mask[EF_SHOOT * F], S, look, POSE_SHOOT, 0, bulky);
    // Hit flinch: lean back a few degrees around the feet
    rotateFrame(&atlas[EF_AIM * F], &mask[EF_AIM * F], &atlas[EF_HIT * F], &mask[EF_HIT * F], S, -8.f, 32, 62, 32, 62);
    // Death: topple sideways in four steps
    const float ang[4] = { 24.f, 50.f, 74.f, 90.f };
    for (int i = 0; i < 4; i++) {
        float k = ang[i] / 90.f;
        rotateFrame(&atlas[EF_AIM * F], &mask[EF_AIM * F], &atlas[(EF_DEATH0 + i) * F], &mask[(EF_DEATH0 + i) * F],
                    S, ang[i], 32, 62, 32 - 27 * k, 62 - 9 * k);
    }
}

void bakePickups(std::vector<uint32_t> &tex)
{
    const int S = PICKUP_TS, F = S * S;
    tex.assign(size_t(5) * F, 0);
    SoldierLook dummy = ENEMY_LOOK;
    // Medkit
    {
        SP p{&tex[0], nullptr, S, float(S) / 64.f, &dummy};
        p.box(24, 16, 40, 21, rgb(40, 40, 44), 0);
        p.box(23, 16, 27, 28, rgb(40, 40, 44), 0);
        p.box(37, 16, 41, 28, rgb(40, 40, 44), 0);
        p.box(10, 24, 54, 58, rgb(236, 236, 230), 0);
        p.box(10, 32, 54, 34, rgb(170, 170, 166), 0);
        p.box(28, 37, 36, 55, rgb(210, 36, 36), 0);
        p.box(23, 42, 41, 50, rgb(210, 36, 36), 0);
    }
    // Ammo box
    {
        SP p{&tex[F], nullptr, S, float(S) / 64.f, &dummy};
        p.box(25, 20, 39, 24, rgb(50, 50, 52), 0);
        p.box(8, 30, 56, 58, rgb(78, 86, 48), 0);
        p.box(7, 25, 57, 32, rgb(64, 72, 40), 0);
        p.box(12, 40, 52, 46, rgb(214, 172, 52), 0);
        p.box(29, 29, 35, 36, rgb(130, 130, 128), 0);
    }
    // Intel case: black hard-case with brass latches and a yellow hazard band
    {
        SP p{&tex[4 * F], nullptr, S, float(S) / 64.f, &dummy};
        p.box(26, 22, 38, 26, rgb(60, 60, 64), 0);
        p.box(8, 26, 56, 56, rgb(34, 36, 40), 0);
        p.box(8, 36, 56, 40, rgb(230, 190, 40), 0);
        p.box(16, 26, 21, 31, rgb(200, 170, 90), 0);
        p.box(43, 26, 48, 31, rgb(200, 170, 90), 0);
    }
    // Weapon pickups: flat-rasterised from the same gun geometry as the view model
    for (int i = 0; i < 2; i++) {
        Canvas cv{&tex[(2 + i) * F], S, S};
        const GunModel &m = i == 0 ? SHOTGUN_MODEL : RIFLE_MODEL;
        float sc = float(S - 4) / (m.length + 4);
        drawGunFlat(cv, m, 3, S * 0.5f + (m.hMax + m.hMin) * 0.5f * sc, sc);
    }
}
