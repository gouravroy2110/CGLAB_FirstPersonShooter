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

const Theme THEMES[THEME_COUNT] = {
    // VOLCANIC: ember sky, basalt, lava pools
    { rgb(44,14,12),  rgb(196,74,34),  rgb(112,52,38),  rgb(255,150,80),  rgb(170,80,56),  rgb(44,22,22),
      0.62f, 0.44f, 0.060f, 0.42f, 0.86f,
      rgb(170,30,0), rgb(255,196,70), true, 45.f, 0.f, 4, "VOLCANIC", "LAVA" },
    // DESERT: bright sun, sandstone, tar pits
    { rgb(52,118,204), rgb(244,218,172), rgb(228,206,164), rgb(255,242,206), rgb(255,255,255), rgb(206,196,184),
      1.15f, 0.46f, 0.026f, 0.95f, 0.14f,
      rgb(26,22,18), rgb(96,84,62), false, 10.f, 0.55f, 0, "DESERT", "TAR" },
    // SWAMP FOREST: green overcast, mud, bog water, trees
    { rgb(62,82,74),  rgb(156,174,146), rgb(112,132,106), rgb(232,242,204), rgb(196,206,186), rgb(92,102,92),
      0.55f, 0.50f, 0.085f, 0.80f, 0.72f,
      rgb(36,54,18), rgb(112,140,52), false, 18.f, 0.45f, 1, "SWAMP", "BOG" },
    // SNOW: pale overcast, snow, frozen water, pines
    { rgb(112,142,184), rgb(222,232,242), rgb(214,222,232), rgb(255,250,240), rgb(255,255,255), rgb(172,182,198),
      0.80f, 0.64f, 0.050f, 0.50f, 0.62f,
      rgb(24,56,90), rgb(150,206,236), false, 14.f, 0.5f, 2, "SNOW", "ICE" },
    // CITY RUINS: smoggy dusk, asphalt, chemical spills
    { rgb(66,58,60),  rgb(206,152,110), rgb(142,120,104), rgb(255,182,122), rgb(222,172,142), rgb(72,62,66),
      0.75f, 0.40f, 0.060f, 0.33f, 0.56f,
      rgb(40,70,20), rgb(150,210,60), true, 28.f, 0.f, 0, "CITY RUINS", "ACID" },
    // FACILITY: night, floodlit steel, coolant
    { rgb(8,12,28),   rgb(46,66,98),   rgb(38,50,68),   rgb(206,222,255), rgb(96,116,148), rgb(18,22,34),
      0.50f, 0.52f, 0.050f, 1.05f, 0.42f,
      rgb(6,50,84), rgb(90,226,255), true, 30.f, 0.f, 0, "FACILITY", "COOLANT" },
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
            if (theme == 0) col = lerpCol(col, rgb(255, 120, 40), smooth(0.55f, 1.f, v) * 0.35f);      // volcano glow on the horizon
            if (theme == 5 && hash2(x, y, 91) > 0.9975f && v < 0.7f) col = lerpCol(col, rgb(240, 245, 255), 0.8f);   // stars
            sky[size_t(y) * SKY_W + x] = col;
        }
    }
}

// -----------------------------------------------------------------------------
//  Floor textures (256×256 = 2×2 world units, tileable), one per biome
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
    const float TAU = 6.2831853f;
    tex.assign(size_t(S) * S, 0);
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float fx = float(x), fy = float(y);
        uint32_t c = 0;
        switch (theme) {
        case 0: { // Basalt plates split by glowing magma cracks, with ash drifts
            float d1, d2; int id;
            voronoi(fx, fy, 6, S, 117, d1, d2, id);
            float edge = d2 - d1;
            float n = fbm(fx, fy, S, 32, 3, 119);
            int base = int(46 + 18 * hash2(id, 1, 3));
            uint32_t rock = rgb(base + int(14 * n), base + int(10 * n) - 4, base + int(10 * n) - 6);
            rock = scaleCol(rock, 0.7f + 0.45f * smooth(0.f, 10.f, edge));
            float heat = smooth(2.2f, 0.f, edge) * (0.6f + 0.4f * fbm(fx, fy, S, 8, 3, 121));
            c = lerpCol(rock, lerpCol(rgb(200, 50, 10), rgb(255, 190, 70), heat), heat * 0.95f);
            float ash = smooth(0.55f, 0.75f, fbm(fx, fy, S, 4, 4, 123));
            c = lerpCol(c, rgb(110, 104, 100), ash * 0.55f);
            break;
        }
        case 1: { // Sand: wind ripples warped by noise, a few pebbles
            float warp = fbm(fx, fy, S, 4, 3, 131) * 40.f;
            float rip = 0.5f + 0.5f * std::sin((fy * 0.9f + fx * 0.25f + warp) * TAU / 18.f);
            float n = fbm(fx, fy, S, 32, 3, 133);
            c = rgb(int(206 + 26 * n + 16 * rip), int(176 + 22 * n + 14 * rip), int(122 + 18 * n + 10 * rip));
            c = scaleCol(c, 0.86f + 0.2f * fbm(fx, fy, S, 2, 2, 135));
            float d1, d2; int id;
            voronoi(fx, fy, 20, S, 137, d1, d2, id);
            if (hash2(id, 3, 7) > 0.8f && d1 < 2.f + 1.5f * hash2(id, 4, 7)) { int v = int(120 + 40 * hash2(id, 5, 7)); c = rgb(v, v - 12, v - 26); }
            break;
        }
        case 2: { // Swamp: dark mud, moss mats, grass tufts, fallen leaves
            float patch = fbm(fx, fy, S, 3, 5, 141);
            float fine = fbm(fx, fy, S, 32, 3, 143);
            float blade = hash2(x, y, 9);
            uint32_t moss = rgb(int(46 + 30 * fine + 22 * blade), int(70 + 36 * fine + 30 * blade), int(26 + 12 * fine));
            float dn = fbm(fx, fy, S, 16, 4, 145);
            uint32_t mud = rgb(int(64 + 26 * dn), int(52 + 20 * dn), int(34 + 14 * dn));
            c = lerpCol(mud, moss, smooth(0.4f, 0.58f, patch));
            float d1, d2; int id;
            voronoi(fx, fy, 22, S, 147, d1, d2, id);
            if (hash2(id, 3, 7) > 0.7f && d1 < 2.6f) c = lerpCol(rgb(150, 96, 40), rgb(120, 60, 30), hash2(id, 8, 7));   // leaves
            float wet = smooth(0.62f, 0.75f, fbm(fx, fy, S, 4, 3, 149));
            c = lerpCol(c, rgb(40, 48, 40), wet * 0.4f);
            break;
        }
        case 3: { // Snow with soft drifts, blue shadows, sparkles and rock showing through
            float n = fbm(fx, fy, S, 8, 4, 151), fine = fbm(fx, fy, S, 32, 3, 153);
            int v = int(214 + 26 * n + 10 * fine);
            c = rgb(v - 8, v - 2, std::min(255, v + 10));
            c = lerpCol(c, rgb(170, 190, 220), smooth(0.6f, 0.3f, n) * 0.35f);
            if (hash2(x, y, 155) > 0.996f) c = rgb(255, 255, 255);
            float rock = smooth(0.72f, 0.8f, fbm(fx, fy, S, 4, 4, 157));
            c = lerpCol(c, rgb(90, 94, 102), rock * 0.85f);
            break;
        }
        case 4: { // City: cracked asphalt, faded lane paint, broken concrete slabs, rubble
            float n = fbm(fx, fy, S, 32, 3, 161);
            int v = int(58 + 22 * n);
            c = rgb(v, v, v + 3);
            if (hash2(x, y, 163) > 0.93f) c = scaleCol(c, 1.25f);                 // aggregate
            if ((x % 128) >= 60 && (x % 128) < 66 && (y % 64) < 36) c = lerpCol(c, rgb(210, 190, 110), 0.55f);   // faded lane dashes
            float cr = std::fabs(fbm(fx, fy, S, 8, 4, 165) - 0.5f);
            if (cr < 0.01f) c = scaleCol(c, 0.4f);
            float slab = smooth(0.62f, 0.66f, fbm(fx, fy, S, 3, 3, 167));
            int sv = int(120 + 30 * n);
            c = lerpCol(c, rgb(sv, sv - 4, sv - 10), slab);
            float d1, d2; int id;
            voronoi(fx, fy, 26, S, 169, d1, d2, id);
            if (hash2(id, 3, 7) > 0.78f && d1 < 2.4f) { int r = int(90 + 50 * hash2(id, 5, 7)); c = rgb(r, r - 6, r - 12); }
            break;
        }
        default: { // Facility: diamond-plate steel panels, seams, hazard stripes
            int px = x % 64, py = y % 64;
            float n = fbm(fx, fy, S, 16, 3, 171);
            int v = int(92 + 20 * n);
            c = rgb(v, v + 4, v + 10);
            int dx = (x + (y / 6) * 3) % 12, dy = y % 6;                           // raised diamonds
            if ((dx == 3 || dx == 4) && (dy == 2 || dy == 3)) c = scaleCol(c, 1.28f);
            if (px < 2 || py < 2) c = rgb(36, 40, 46);
            if (px == 2 || py == 2) c = scaleCol(c, 1.2f);
            if ((px == 8 || px == 56) && (py == 8 || py == 56)) c = rgb(150, 156, 166);   // bolts
            if (y % 128 > 118) c = ((x + y) / 8) % 2 ? rgb(220, 180, 30) : rgb(30, 30, 34);   // hazard stripe
            break;
        }
        }
        tex[size_t(y) * S + x] = c;
    }
}

// -----------------------------------------------------------------------------
//  Wall textures (128×128 per world unit), per biome
// -----------------------------------------------------------------------------
// One storey of a campus building, one world unit (4 m) wide, after photos of the JU buildings:
//   0 cream concrete with a horizontal band (Arts, engineering blocks)
//   1 white plaster, JU-blue pilasters and window frames (Central Library, Engineering Science)
//   2 white with red spandrel panels (TEQIP)
//   3 salmon brick with white slab bands (Meghnad Saha Bhavan, Triguna Sen Auditorium)
//   4 a canteen hut: timber posts, open front, a low brick wall
//   5 Central Library: white storeys between JU-blue pilasters, blue window frames; the ground floor blue
//   6 Aurobindo Bhavan: warm cream, maroon pilasters at every bay, grilled windows
//   7 Triguna Sen Auditorium / Alumni block: dark red brick, grey concrete bands, grey window frames
//   8 TEQIP Building: cream with deep red bands and horizontal red louvres
//   9 the compound wall: cream plaster, a red-brick coping
//  10 a gate: cream iron grille with the round JU emblem plate, the street dark behind it
//  11 a gate column: terracotta red, round (shaded across), a cream capital
//  12 Subarna Jayanti Bhavan (CSE / IT): light beige brick, a dark glass ribbon window
//  13 Prayukti Bhavan: grey-cream concrete panels, terracotta accent panels
//  14 Technology Bhavan: cream stucco under wide concrete sunshades
//  15 Electrical Engineering: red brick masonry, white floor slabs
//  16 Chemical / Mechanical workshops: industrial brick, external pipe gantries
//  17 Gandhi Bhavan: red brick between white stone columns
//  18 Pharmacy: off-white plaster, light green window trim
//  19 UG Science: deep cream stucco, a band of ventilation grilles
// `ground` asks for the building's ground floor where it differs (the library's blue base)
void bakeFacade(std::vector<uint32_t> &tex, int style, bool ground)
{
    const int S = WALL_TS;
    tex.assign(size_t(S) * S, 0);
    auto h = [](int x, int y) { uint32_t n = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u; n = (n ^ (n >> 13)) * 1274126177u; return int(n >> 24); };
    struct P { int wall[3], trim[3], frame[3]; };
    static const P PAL[5] = {
        {{230, 225, 210}, {176, 172, 160}, {96, 92, 84}},
        {{236, 236, 230}, {44, 78, 190}, {44, 78, 190}},
        {{236, 234, 228}, {196, 48, 38}, {110, 106, 100}},
        {{196, 112, 82}, {232, 228, 216}, {236, 232, 220}},
        {{150, 96, 58}, {176, 122, 72}, {92, 58, 34}},
    };
    auto clamp8 = [](int v) { return std::max(0, std::min(255, v)); };
    if (style >= 12) {
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
            int n = (h(x, y) - 128) / 24 + (h(x / 4, y / 4 + 99) - 128) / 36;
            int r, g, b;
            auto c3 = [&](int R_, int G_, int B_) { r = R_ + n; g = G_ + n; b = B_ + n; };
            auto brick = [&](int R_, int G_, int B_, int mr, int mg, int mb) {
                int row = y / 8, off = (row & 1) * 8;
                if (y % 8 == 0 || (x + off) % 16 == 0) c3(mr, mg, mb);
                else { int k = (h((x + off) / 16, row + 31) - 128) / 10; c3(R_ + k, G_ + k / 2, B_ + k / 2); }
            };
            auto glassAt = [&](int y0, int y1) {
                float kk = 1.f - float(y - y0) / float(y1 - y0);
                c3(int(40 + 36 * kk), int(52 + 40 * kk), int(64 + 50 * kk));
            };
            auto windows = [&](int fr, int fg, int fb, bool bars) {          // two framed windows per bay
                for (int w = 0; w < 2; w++) {
                    int x0 = 18 + w * 56, x1 = x0 + 36, y0 = 28, y1 = 92;
                    if (x >= x0 - 3 && x < x1 + 3 && y >= y0 - 3 && y < y1 + 3) c3(fr, fg, fb);
                    if (x >= x0 && x < x1 && y >= y0 && y < y1) {
                        glassAt(y0, y1);
                        if (bars && (std::abs(x - (x0 + x1) / 2) < 1 || std::abs(y - (y0 + 30)) < 1)) c3(fr, fg, fb);
                    }
                }
            };
            switch (style) {
            case 12:                                                        // Subarna Jayanti: beige brick + ribbon glazing
                brick(220, 210, 190, 236, 230, 216);
                if (y >= 34 && y < 84) { glassAt(34, 84); if (x % 32 < 2) c3(70, 72, 76); }
                if (y >= 84 && y < 88) c3(236, 232, 222);
                break;
            case 13:                                                        // Prayukti: concrete panels, terracotta accents
                c3(210, 206, 196);
                if (x % 64 == 0 || y % 64 == 0) c3(176, 172, 164);
                if (x < 14 || (x >= 64 && x < 78)) c3(172, 82, 60);
                windows(120, 118, 112, true);
                break;
            case 14:                                                        // Technology Bhavan: stucco, sunshades
                c3(232, 222, 196);
                windows(150, 144, 130, true);
                if (y >= 16 && y < 24) c3(214, 210, 200);                   // the projecting sunshade
                if (y >= 24 && y < 40) { r -= 46; g -= 46; b -= 44; }       // its shadow over the window head
                break;
            case 15:                                                        // Electrical: red brick, white slabs
                brick(160, 60, 48, 196, 176, 160);
                windows(236, 234, 226, true);
                if (y >= S - 14) c3(236, 234, 226);
                break;
            case 16:                                                        // workshops: industrial brick, pipes
                brick(160, 75, 60, 190, 170, 150);
                windows(96, 100, 104, false);
                if (x >= 18 && x < 54 && y >= 28 && y < 92 && ((x - 18) % 9 == 0 || (y - 28) % 9 == 0)) c3(96, 100, 104);   // steel sash
                if (x >= 72 && x < 92 && y >= 28 && y < 92 && ((x - 72) % 9 == 0 || (y - 28) % 9 == 0)) c3(96, 100, 104);
                if (x >= 6 && x < 12) { float u = (x - 9) / 3.f; c3(int(150 - 40 * u * u), int(152 - 40 * u * u), int(156 - 40 * u * u)); }   // a down pipe
                if (y >= 98 && y < 104) { float u = (y - 101) / 3.f; c3(int(170 - 50 * u * u), int(120 - 40 * u * u), int(40)); }          // a gas line
                break;
            case 17:                                                        // Gandhi Bhavan: red brick, white columns
                brick(175, 65, 50, 200, 176, 160);
                windows(232, 228, 214, true);
                if (x < 12 || (x >= 60 && x < 72)) {                        // white stone columns
                    float u = x < 12 ? (x - 6) / 6.f : (x - 66) / 6.f;
                    int k = int(30 * u * u);
                    c3(234 - k, 230 - k, 216 - k);
                }
                if (y >= S - 10) c3(226, 220, 204);
                break;
            case 18:                                                        // Pharmacy: off-white, green trim
                c3(238, 236, 226);
                windows(150, 200, 160, true);
                if (y >= S - 10) c3(222, 220, 210);
                break;
            default:                                                        // UG Science: deep cream, vent grilles
                c3(226, 200, 150);
                windows(200, 178, 132, true);
                if (y >= 98 && y < 112 && (x % 6 < 3) && (y % 4 < 2)) c3(90, 76, 56);
                if (y >= S - 10) c3(206, 180, 132);
                break;
            }
            tex[size_t(y) * S + x] = rgb(clamp8(r), clamp8(g), clamp8(b));
        }
        return;
    }
    if (style == 5 || style == 6 || style >= 9) {
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
            int n = (h(x, y) - 128) / 24 + (h(x / 4, y / 4 + 99) - 128) / 36;
            int r, g, b;
            auto c3 = [&](int R_, int G_, int B_) { r = R_ + n; g = G_ + n; b = B_ + n; };
            auto glass = [&](int y0, int y1) {
                float kk = 1.f - float(y - y0) / float(y1 - y0);
                c3(int(44 + 38 * kk), int(56 + 42 * kk), int(70 + 52 * kk));
            };
            if (style == 5) {                                               // the Central Library
                const int BL[3] = {98, 118, 214}, WH[3] = {240, 240, 236};
                bool pil = x < 14 || x >= S - 14;
                if (ground) c3(BL[0], BL[1], BL[2]); else c3(WH[0], WH[1], WH[2]);
                if (pil) c3(BL[0] - 8, BL[1] - 8, BL[2] - 6);
                for (int w = 0; w < 2; w++) {
                    int x0 = 24 + w * 46, x1 = x0 + 34, y0 = 26, y1 = 96;
                    if (x >= x0 - 4 && x < x1 + 4 && y >= y0 - 4 && y < y1 + 4) { if (ground) c3(236, 236, 230); else c3(BL[0], BL[1], BL[2]); }
                    if (x >= x0 && x < x1 && y >= y0 && y < y1) {
                        glass(y0, y1);
                        if ((x - x0) % 11 == 0 || (y - y0) % 14 == 0) c3(ground ? 236 : BL[0], ground ? 236 : BL[1], ground ? 230 : BL[2]);   // glazing bars
                    }
                }
                if (y >= S - 8) c3(ground ? 70 : 222, ground ? 86 : 222, ground ? 170 : 216);
            } else if (style == 6) {                                        // Aurobindo Bhavan
                c3(236, 210, 156);
                if (x < 12 || (x >= 60 && x < 70)) c3(134, 44, 56);         // maroon pilasters
                for (int w = 0; w < 2; w++) {
                    int x0 = 20 + w * 56, x1 = x0 + 34, y0 = 28, y1 = 94;
                    if (x >= x0 - 3 && x < x1 + 3 && y >= y0 - 3 && y < y1 + 3) c3(214, 190, 140);
                    if (x >= x0 && x < x1 && y >= y0 && y < y1) {
                        glass(y0, y1);
                        if ((x - x0) % 6 == 0 || (y - y0) % 8 == 0) c3(200, 196, 186);   // the window grille
                    }
                }
                if (y >= S - 10) c3(214, 186, 132);
            } else if (style == 9) {                                        // compound wall
                c3(226, 214, 186);
                int stain = std::max(0, 30 - (S - y)) / 3;                  // splash at the foot
                r -= stain; g -= stain; b -= stain;
                if (y < 18) {                                               // red-brick coping
                    int row = y / 6, off = (row & 1) * 8;
                    bool mortar = y % 6 == 0 || (x + off) % 16 == 0;
                    if (mortar) c3(190, 176, 160); else c3(170, 74, 56);
                }
                if (y >= 18 && y < 21) c3(120, 110, 96);
                if (y < 12 && x % 8 < 2) c3(46, 48, 50);                    // iron spikes standing on it
                if (y < 3 && x % 8 < 2) c3(70, 72, 76);
            } else if (style == 10) {                                       // iron gate with the emblem
                c3(36, 40, 38);                                             // the street beyond
                if (y >= 100) c3(70, 68, 64);
                bool bar = (x % 10) < 3, rail = (y % 30) < 3 || y < 4;
                float ex = x - 64.f, ey = y - 58.f, er = std::sqrt(ex * ex + ey * ey);
                if (bar || rail) c3(226, 218, 196);
                if (er < 26.f) {                                            // the round brass plate
                    int k = int((1.f - er / 26.f) * 40.f);
                    c3(196 + k / 2, 160 + k / 2, 82);
                    if (er > 22.f || (er > 14.f && er < 16.f)) c3(150, 112, 50);
                    if (er < 8.f && std::fabs(ex) < 4.f) c3(40, 60, 150);  // the gate number
                }
            } else {                                                        // gate column
                float u = (x + 0.5f) / S * 2.f - 1.f;
                float sh = 0.62f + 0.38f * std::sqrt(std::max(0.f, 1.f - u * u)) - 0.12f * u;
                c3(int(182 * sh), int(86 * sh), int(68 * sh));
                if (y < 16) c3(int(232 * sh), int(220 * sh), int(196 * sh));    // the capital
            }
            tex[size_t(y) * S + x] = rgb(clamp8(r), clamp8(g), clamp8(b));
        }
        return;
    }
    if (style == 7 || style == 8) {
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
            int n = (h(x, y) - 128) / 22 + (h(x / 4, y / 4 + 99) - 128) / 34;
            int r, g, b;
            auto c3 = [&](int R_, int G_, int B_) { r = R_ + n; g = G_ + n; b = B_ + n; };
            bool win = x >= 34 && x < 94 && y >= 30 && y < 92;
            if (style == 7) {
                int row = y / 7, off = (row & 1) * 9;
                bool mortar = y % 7 == 0 || (x + off) % 18 == 0;
                int k = (h((x + off) / 18, row + 7) - 128) / 9;
                if (mortar) c3(176, 166, 154); else c3(160 + k, 50 + k / 2, 40 + k / 2);
                if (y >= S - 16) { c3(150, 150, 144); if (h(x, y + 7) > 200) c3(110, 108, 100); }   // exposed-aggregate band at each floor
                if (x >= 28 && x < 100 && y >= 24 && y < 98) c3(150, 150, 144);   // the window's concrete surround
                if (win) {
                    float kk = 1.f - float(y - 30) / 62.f;
                    c3(int(40 + 36 * kk), int(46 + 38 * kk), int(52 + 44 * kk));
                    if ((x - 34) % 12 == 0 || (y - 30) % 12 == 0) c3(120, 118, 110);   // the grille
                }
            } else {
                c3(240, 238, 225);
                if (y >= 72 && y < S - 10) {                                  // the red band, with louvres where there's a window
                    c3(165, 32, 42);
                    if (x >= 20 && x < 108 && y >= 80 && (y - 80) % 6 < 2) c3(128, 22, 30);
                }
                if (x >= 30 && x < 98 && y >= 22 && y < 66) {                 // a window set in the cream
                    float kk = 1.f - float(y - 22) / 44.f;
                    c3(int(46 + 36 * kk), int(56 + 40 * kk), int(64 + 46 * kk));
                    if (std::abs(x - 64) < 1 || (y - 22) % 11 == 0) c3(70, 66, 60);
                }
                if (y >= S - 10) c3(206, 196, 170);
            }
            tex[size_t(y) * S + x] = rgb(std::max(0, std::min(255, r)), std::max(0, std::min(255, g)), std::max(0, std::min(255, b)));
        }
        return;
    }
    const P &p = PAL[std::max(0, std::min(4, style))];
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        int n = (h(x, y) - 128) / 22 + (h(x / 4, y / 4 + 99) - 128) / 34;
        int r = p.wall[0] + n, g = p.wall[1] + n, b = p.wall[2] + n;
        auto set = [&](const int *c, int k = 0) { r = c[0] + n + k; g = c[1] + n + k; b = c[2] + n + k; };
        if (style == 3) {                                                   // brick courses + mortar
            int row = y / 8, off = (row & 1) * 8;
            if (y % 8 == 0 || (x + off) % 16 == 0) { r = 186 + n; g = 176 + n; b = 160 + n; }
            else { int k = (h((x + off) / 16, row) - 128) / 10; r += k; g += k / 2; b += k / 2; }
        }
        if (style == 4) {                                                   // a canteen (Ahar): perforated red-brick jali walls, a wide glazed front
            int row = y / 8, off = (row & 1) * 8;
            bool mortar = y % 8 == 0 || (x + off) % 16 == 0;
            bool hole = ((x + off) % 16 >= 6 && (x + off) % 16 < 10) && (y % 8 >= 3 && y % 8 < 6) && (row % 2 == 0);
            r = 176 + n; g = 86 + n; b = 62 + n;
            if (mortar) { r = 196 + n; g = 150 + n; b = 124 + n; }
            if (hole) { r = 50 + n; g = 34 + n; b = 28 + n; }
            if (x >= 30 && x < 98 && y >= 30 && y < 100) {                  // the counter window
                float kk = 1.f - float(y - 30) / 70.f;
                r = int(52 + 30 * kk); g = int(46 + 26 * kk); b = int(40 + 26 * kk);
                if (std::abs(x - 64) < 2) { r = 120; g = 84; b = 52; }
            }
            if (y >= 100 && y < 106) { r = 214 + n; g = 196 + n; b = 170 + n; }   // the sill band
            tex[size_t(y) * S + x] = rgb(std::max(0, std::min(255, r)), std::max(0, std::min(255, g)), std::max(0, std::min(255, b)));
            continue;
        }
        // the slab at each floor
        if (y >= S - 12) { if (style == 3) set(p.trim); else if (style == 0) set(p.trim, -10); else { r -= 34; g -= 34; b -= 32; } }
        if (y < 3) { r -= 20; g -= 20; b -= 18; }
        if (style == 1 && (x < 10 || x >= S - 10)) set(p.trim);               // blue pilasters at the bays
        if (style == 0 && y >= 100 && y < S - 12) set(p.trim, 12);           // the band under the sills
        for (int w = 0; w < 2; w++) {
            int x0 = 16 + w * 56, x1 = x0 + 40, y0 = 26, y1 = 92;
            if (style == 2 && x >= x0 - 6 && x < x1 + 6 && y >= y1 + 4 && y < S - 12) set(p.trim);   // red spandrel
            if (x >= x0 - 3 && x < x1 + 3 && y >= y0 - 3 && y < y1) set(p.frame);
            if (x >= x0 && x < x1 && y >= y0 && y < y1 - 3) {
                float k = 1.f - float(y - y0) / float(y1 - y0);
                bool glint = (x - x0) + (y - y0) / 2 < 16 && (x - x0) + (y - y0) / 2 > 9;
                r = int(46 + 40 * k) + (glint ? 50 : 0); g = int(62 + 44 * k) + (glint ? 50 : 0); b = int(80 + 50 * k) + (glint ? 45 : 0);
                if (std::abs(x - (x0 + x1) / 2) < 1 || std::abs(y - (y0 + 30)) < 1) set(p.frame);
            }
            if (style != 2 && x >= x0 - 5 && x < x1 + 5 && y >= y1 && y < y1 + 4) { r = 238; g = 234; b = 222; }
        }
        tex[size_t(y) * S + x] = rgb(std::max(0, std::min(255, r)), std::max(0, std::min(255, g)), std::max(0, std::min(255, b)));
    }
}

void bakeWall(std::vector<uint32_t> &tex, int type, int theme)
{
    const int S = WALL_TS;
    if (type == 5) {
        // Secret wall: the stone texture plus a faint doorway seam, cracks, and an engraved sigil
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
        const int cx = S / 2, cy = S / 2;
        for (int dy = -18; dy <= 18; dy++) for (int dx = -18; dx <= 18; dx++) {
            int d = std::abs(dx) + std::abs(dy);
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= S || py < 0 || py >= S) continue;
            if (d == 16 || d == 15 || d == 8 || d == 7 || (d <= 2)) {
                uint32_t carved = scaleCol(tex[size_t(py) * S + px], 0.40f);
                tex[size_t(py) * S + px] = lerpCol(carved, rgb(215, 170, 75), 0.35f);
            } else if (d == 17) tex[size_t(py) * S + px] = scaleCol(tex[size_t(py) * S + px], 1.25f);
        }
        return;
    }
    tex.assign(size_t(S) * S, 0);
    const uint32_t stoneCol[THEME_COUNT] = { rgb(64,58,56), rgb(196,160,112), rgb(96,104,90), rgb(118,124,136), rgb(140,136,128), rgb(120,128,140) };
    const uint32_t brickCol[THEME_COUNT] = { rgb(96,40,30), rgb(186,138,92), rgb(120,64,48), rgb(126,70,60), rgb(140,66,48), rgb(176,180,186) };
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float fx = float(x), fy = float(y);
        uint32_t c = 0;
        switch (type) {
        case 1: { // Main wall material
            if (theme == 1) {                        // desert: layered sandstone strata
                float wob = (fbm(fx, fy, S, 4, 3, 211) - 0.5f) * 14.f;
                float band = fy + wob;
                float layer = 0.5f + 0.5f * std::sin(band * 0.25f) * 0.6f + 0.4f * fbm(fx * 0.3f, band, S, 8, 2, 213);
                c = scaleCol(stoneCol[1], 0.72f + 0.38f * layer);
                if (std::fabs(std::fmod(band + 200.f, 22.f)) < 1.3f) c = scaleCol(c, 0.7f);
                break;
            }
            if (theme == 4) {                        // ruins: cast concrete, form lines, holes, rebar
                float n = fbm(fx, fy, S, 16, 5, 221);
                int v = int(124 + 46 * (n - 0.5f));
                c = rgb(v, v - 2, v - 6);
                if (y % 42 < 1) c = scaleCol(c, 0.7f);
                float hole = fbm(fx, fy, S, 4, 4, 223);
                if (hole > 0.7f) { c = rgb(30, 28, 26); if (hash2(x, y, 225) > 0.93f) c = rgb(130, 60, 30); }
                float soot = smooth(0.4f, 1.0f, fy / S) * fbm(fx, fy, S, 4, 3, 227);
                c = scaleCol(c, 1.f - 0.4f * soot);
                break;
            }
            if (theme == 5) {                        // facility: riveted steel panels with a stencil band
                int px = x % 64, py = y % 64;
                float n = fbm(fx, fy, S, 16, 3, 231);
                int v = int(104 + 26 * n);
                c = rgb(v, v + 6, v + 14);
                if (px < 2 || py < 2) c = rgb(40, 44, 52);
                if ((px == 6 || px == 58) && (py % 12 == 6)) c = rgb(170, 176, 186);
                if (y > 52 && y < 64) c = ((x + y) / 6) % 2 ? rgb(210, 170, 30) : rgb(36, 36, 40);
                float streak = fbm(fx * 0.2f, fy, S, 16, 3, 233);
                c = scaleCol(c, 1.f - 0.25f * smooth(0.6f, 0.8f, streak));
                break;
            }
            // stone blocks (volcanic basalt with glowing seams, swamp mossy, snow icy)
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
            float shade = ly < 4 ? 1.18f : rowH - ly < 4 ? 0.7f : 1.f;
            st = scaleCol(st, shade);
            uint32_t joint = theme == 0 ? lerpCol(rgb(150, 40, 10), rgb(255, 150, 40), hash2(x, y, 241) * 0.5f) : rgb(46, 44, 40);
            c = e < 1.6f ? joint : st;
            if (theme == 2) c = lerpCol(c, rgb(58, 86, 40), smooth(0.7f, 0.92f, fbm(fx, fy, S, 8, 4, 69) + fy / S * 0.45f) * 0.9f);
            if (theme == 3) {
                c = lerpCol(c, rgb(176, 200, 226), smooth(0.65f, 0.85f, fbm(fx, fy, S, 8, 3, 243)) * 0.5f);   // frost
                if (y < 10 + int(6 * fbm(fx, 0.f, S, 8, 2, 245))) c = rgb(236, 242, 250);                 // snow cap
            }
            break;
        }
        case 2: { // Brick (adobe in the desert, glazed tiles in the facility)
            int bh = theme == 5 ? 32 : 16, bw = theme == 5 ? 32 : 32;
            int row = y / bh;
            int off = (row & 1) && theme != 5 ? bw / 2 : 0;
            int lx = (x + off) % bw, ly = y % bh;
            int bid = ((x + off) / bw) * 7 + row * 11;
            bool mortar = (ly < 2) || (lx < 2);
            float n = fbm(fx, fy, S, 32, 3, 71);
            uint32_t br = scaleCol(brickCol[theme], 0.78f + 0.35f * hash2(bid, 2, 73) + 0.3f * (n - 0.5f));
            if (ly == 2) br = scaleCol(br, 1.15f);
            if (ly == bh - 1) br = scaleCol(br, 0.78f);
            c = mortar ? rgb(int(150 + 30 * n), int(144 + 30 * n), int(132 + 26 * n)) : br;
            float grime = smooth(0.4f, 1.0f, fy / S) * (0.5f + 0.5f * fbm(fx, fy, S, 4, 3, 75));
            c = scaleCol(c, 1.f - 0.35f * grime);
            if (theme == 4 && fbm(fx, fy, S, 3, 2, 251) > 0.66f)                      // graffiti blobs
                c = lerpCol(c, hash2(int(fx / 40), int(fy / 40), 253) > 0.5f ? rgb(220, 60, 120) : rgb(60, 200, 220), 0.55f);
            if (theme == 3 && y < 8) c = rgb(236, 242, 250);
            break;
        }
        case 3: { // Pillar: bark in the swamp / snow (tree trunks), obsidian, columns, beams
            if (theme == 2 || theme == 3) {
                float grain = fbm(fx * 0.15f, fy * 1.5f, S, 16, 4, 261);
                int v = int(60 + 50 * grain);
                c = theme == 2 ? rgb(v, int(v * 0.72f), int(v * 0.48f)) : rgb(int(v * 0.85f), int(v * 0.7f), int(v * 0.55f));
                if (std::fabs(std::sin(fx * 0.35f + grain * 6.f)) < 0.12f) c = scaleCol(c, 0.55f);   // bark ridges
                if (theme == 2) c = lerpCol(c, rgb(70, 100, 46), smooth(0.65f, 0.85f, fbm(fx, fy, S, 8, 3, 263)) * 0.7f);
                break;
            }
            float n = fbm(fx, fy, S, 16, 5, 81);
            float streak = fbm(fx, fy * 0.08f, S, 16, 3, 83);
            int v = int(128 + 50 * (n - 0.5f));
            c = rgb(v, v, v + 4);
            if (theme == 0) c = rgb(int(30 + 20 * n), int(26 + 16 * n), int(34 + 22 * n));        // obsidian
            if (theme == 1) c = scaleCol(stoneCol[1], 0.8f + 0.3f * n);
            if (theme == 5) { c = rgb(70, 76, 86); if (x % 32 < 4) c = rgb(110, 118, 130); }
            c = scaleCol(c, 1.f - 0.3f * smooth(0.5f, 0.75f, streak));
            if (x % 64 < 1 || y % 64 < 1) c = scaleCol(c, 0.65f);
            break;
        }
        default: { // Door: wood planks; steel blast door in the facility and ruins
            if (theme >= 4) {
                float n = fbm(fx, fy, S, 16, 3, 271);
                int v = int(90 + 30 * n);
                c = rgb(v, v + 4, v + 8);
                if (theme == 4 && (y % 12) < 2) c = scaleCol(c, 0.7f);                // corrugation
                if (x > 60 && x < 68) c = rgb(30, 32, 36);                             // split line
                if (y > 100 && y < 116) c = ((x + y) / 8) % 2 ? rgb(220, 180, 30) : rgb(30, 30, 34);
                if (std::abs(x - 64) > 50 && y > 40 && y < 70) c = rgb(60, 200, 90);      // status light
                break;
            }
            int plank = x / 21;
            float grain = fbm(fx * 0.25f + plank * 40.f, fy * 2.f, S, 16, 4, 91 + plank);
            uint32_t wood = rgb(int(104 + 50 * grain), int(64 + 30 * grain), int(34 + 16 * grain));
            if (theme == 0) wood = scaleCol(wood, 0.6f);
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

// Tree canopy billboards for pillar cells in the swamp (broadleaf) and snow (pine)
// A broadleaf campus tree, the size of a real one (~13 m): a slim trunk, branches
// reaching into the crown, and the crown as a cluster of leaf clumps, each lit as a
// sphere from the upper left, with gaps where the sky shows through
void bakeCampusTree(std::vector<uint32_t> &tex, int variant)
{
    const int S = TREE_TS;
    tex.assign(size_t(S) * S, 0);
    auto rnd = [&](int i) { return hash2(i, variant, 4111); };
    struct Blob { float x, y, r, z; };
    std::vector<Blob> blobs;
    int nb = 12 + variant * 2;
    for (int i = 0; i < nb; i++) {
        float a = rnd(i * 3) * 6.2832f, rr = std::sqrt(rnd(i * 3 + 1));
        blobs.push_back({0.5f + std::cos(a) * rr * (0.27f + 0.04f * variant), 0.34f + std::sin(a) * rr * 0.17f,
                         0.11f + 0.08f * rnd(i * 3 + 2), rnd(i * 7 + 5)});
    }
    blobs.push_back({0.5f, 0.32f, 0.2f, 0.5f});
    auto put = [&](int x, int y, uint32_t c) { if (x >= 0 && y >= 0 && x < S && y < S) tex[size_t(y) * S + x] = c; };
    // trunk: tapers from the roots up into the crown
    float lean = (rnd(91) - 0.5f) * 0.05f;
    for (int y = int(S * 0.42f); y < S; y++) {
        float v = float(y) / S, half = S * (0.022f + 0.03f * std::max(0.f, (v - 0.75f) / 0.25f) + 0.012f * (v - 0.42f));
        float cx = S * (0.5f + lean * (1.f - v));
        for (int x = int(cx - half); x <= int(cx + half); x++) {
            float u = (x - cx) / std::max(1.f, half);
            int k = int(80 * (0.75f - 0.45f * u)) + int(18 * (hash2(x, y / 3, 4123) - 0.5f));
            put(x, y, rgb(clampi(34 + k / 2, 0, 255), clampi(28 + k / 3, 0, 255), clampi(24 + k / 4, 0, 255)));
        }
    }
    // branches: from the top of the trunk out to the clumps
    for (int i = 0; i < 5; i++) {
        const Blob &b = blobs[size_t(i * 2 % nb)];
        float x0 = S * 0.5f, y0 = S * 0.6f, x1 = b.x * S, y1 = (b.y + 0.04f) * S;
        for (int t = 0; t <= 60; t++) {
            float k = t / 60.f, x = x0 + (x1 - x0) * k, y = y0 + (y1 - y0) * k - std::sin(k * 3.14159f) * S * 0.03f;
            int w = k < 0.5f ? 2 : 1;
            for (int o = -w / 2; o <= w / 2; o++) put(int(x) + o, int(y), rgb(78, 62, 48));
        }
    }
    // crown
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float u = (x + 0.5f) / S, v = (y + 0.5f) / S;
        float best = -1.f, nx = 0.f, ny = 0.f, nz = 0.f;
        for (const Blob &b : blobs) {
            float dx = (u - b.x) / b.r, dy = (v - b.y) / b.r, q = dx * dx + dy * dy;
            float edge = 1.f + 0.22f * (hash2(x / 2, y / 2, 4127 + variant) - 0.5f);     // ragged leafy rim
            if (q >= edge * edge) continue;
            float z = std::sqrt(std::max(0.f, 1.f - q)) * b.r + b.z * 0.05f;
            if (z > best) { best = z; nx = dx; ny = dy; nz = std::sqrt(std::max(0.f, 1.f - q)); }
        }
        if (best < 0.f) continue;
        if (hash2(x / 2, y / 2, 4133 + variant) > 0.965f && nz < 0.6f) continue;           // sky through the leaves
        float lit = clampf(0.38f + 0.75f * std::max(0.f, -0.45f * nx - 0.55f * ny + 0.7f * nz), 0.3f, 1.25f);
        float leaf = hash2(x, y, 4139 + variant);
        float n = fbm(float(x), float(y), S, 16, 3, 4141 + variant);
        int r = int((40 + 30 * n + (leaf > 0.85f ? 22 : 0)) * lit), g = int((84 + 48 * n + (leaf > 0.85f ? 30 : 0)) * lit), b = int((30 + 18 * n) * lit);
        put(x, y, rgb(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255)));
    }
}

void bakeCanopy(std::vector<uint32_t> &tex, int theme)
{
    const int S = CANOPY_TS;
    tex.assign(size_t(S) * S, 0);
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float u = (x + 0.5f) / S * 2.f - 1.f, v = (y + 0.5f) / S;           // u -1..1, v 0 top..1 bottom
        float n = fbm(float(x), float(y), S, 8, 4, 281 + theme);
        bool in;
        float shade;
        if (theme == 3) {                                                    // pine: stacked cones
            float tier = std::fmod(v * 4.f, 1.f);
            float w = (0.25f + 0.75f * v) * (0.55f + 0.45f * tier);
            in = std::fabs(u) < w * (0.85f + 0.3f * n) && v < 0.92f;
            shade = 0.6f + 0.6f * tier - 0.3f * u;
        } else {                                                             // broadleaf: lumpy round crown
            float cx = u, cy = (v - 0.45f) * 2.f;
            float r = std::sqrt(cx * cx + cy * cy);
            in = r < 0.78f + 0.3f * (n - 0.5f) && v < 0.88f;
            shade = 1.1f - 0.5f * cy - 0.3f * u + 0.4f * (n - 0.5f);
        }
        if (!in) continue;
        uint32_t base = theme == 3 ? rgb(34, 70, 52) : rgb(58, 96, 40);
        uint32_t c = scaleCol(base, clampf(shade, 0.35f, 1.4f));
        if (theme == 3 && hash2(x, y, 291) > 0.7f && std::fmod(v * 4.f, 1.f) < 0.25f) c = rgb(232, 240, 248);   // snow on the boughs
        if (hash2(x, y, 293) > 0.92f) c = scaleCol(c, 1.3f);
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

// Friendly squad: sand-and-blue kit so allies never read as hostiles
const SoldierLook SQUAD_LOOK = {
    rgb(150,138,104), rgb(110,100,74), rgb(170,160,126), rgb(58,92,140), rgb(40,66,104),
    rgb(64,100,150), rgb(196,150,116), rgb(40,40,44), rgb(110,230,255), rgb(52,44,36), rgb(46,44,42)
};

const SoldierLook GRENADIER_LOOK = {
    rgb(112,104,70), rgb(78,72,46), rgb(150,120,60), rgb(96,86,52), rgb(66,58,36),
    rgb(90,84,56), rgb(196,150,116), rgb(56,48,40), rgb(255,150,40), rgb(48,40,32), rgb(40,36,30)
};
const SoldierLook SHIELD_LOOK = {
    rgb(40,52,78), rgb(26,34,52), rgb(58,72,104), rgb(30,40,62), rgb(20,26,42),
    rgb(30,38,58), rgb(190,146,112), rgb(24,28,36), rgb(120,200,255), rgb(22,24,30), rgb(20,22,28)
};
const SoldierLook MEDIC_LOOK = {
    rgb(206,208,204), rgb(150,152,150), rgb(226,226,222), rgb(184,186,182), rgb(120,122,120),
    rgb(232,232,228), rgb(196,150,116), rgb(70,72,74), rgb(255,90,80), rgb(70,70,72), rgb(60,60,62)
};
const SoldierLook HEAVY_LOOK = {
    rgb(54,56,60), rgb(36,38,42), rgb(80,82,88), rgb(66,62,40), rgb(46,42,26),
    rgb(52,54,58), rgb(180,138,106), rgb(30,30,32), rgb(255,200,40), rgb(28,28,30), rgb(26,26,28)
};
const SoldierLook STALKER_LOOK = {
    rgb(26,28,34), rgb(14,15,20), rgb(40,44,56), rgb(20,22,28), rgb(12,13,16),
    rgb(18,20,26), rgb(150,120,100), rgb(10,10,12), rgb(80,255,230), rgb(12,12,14), rgb(16,16,18)
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

void paintSoldier(uint32_t *px, uint8_t *mask, int size, const SoldierLook &look, SoldierPose pose, float walk, bool bulky, int gear)
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

    // --- Extra kit -------------------------------------------------------------------
    if (gear == GEAR_GRENADIER) {
        // bandolier of grenades across the chest + a fat launcher tube instead of the rifle
        p.capsule(24, 18 + by, 40, 34 + by, 1.6f, rgb(60, 50, 34), BODY);
        for (int g = 0; g < 4; g++) p.ellipse(26.5f + g * 3.8f, 20.5f + g * 3.8f + by, 1.7f, 1.9f, rgb(70, 90, 50), BODY);
        if (pose != POSE_WALK) { p.ellipse(31, 22, 4.4f, 4.4f, rgb(54, 60, 40), BODY); p.ellipse(31, 22, 2.6f, 2.6f, rgb(10, 10, 10), BODY); }
        else p.capsule(44, 24 + by, 18, 34 + by, 3.2f, rgb(60, 66, 44), BODY);
    }
    if (gear == GEAR_SHIELD) {
        // riot shield held on the left: blocks rounds (its own hit-mask value), slit window near the top
        p.box(12, 15 + by, 33, 52 + by, rgb(46, 52, 60), MASK_SHIELD);
        p.box(13, 16 + by, 32, 51 + by, rgb(64, 72, 84), MASK_SHIELD);
        p.box(15, 19 + by, 30, 22 + by, rgb(150, 190, 220), MASK_SHIELD);
        p.box(14, 40 + by, 31, 42 + by, rgb(220, 220, 220), MASK_SHIELD);
    }
    if (gear == GEAR_MEDIC) {
        p.box(29, 19 + by, 35, 31 + by, rgb(240, 240, 238), BODY);
        p.box(30.5f, 20.5f + by, 33.5f, 29.5f + by, rgb(210, 36, 36), BODY);
        p.box(27.5f, 23.5f + by, 36.5f, 26.5f + by, rgb(210, 36, 36), BODY);
        p.box(20 - bw, 21 + by, 24 - bw, 24 + by, rgb(210, 36, 36), LIMB);               // arm band
    }
    if (gear == GEAR_HEAVY) {
        // rotary cannon held at the hip: barrel cluster facing the viewer
        float gy = pose == POSE_WALK ? 32.f + by : 27.f;
        p.box(25, gy - 6, 41, gy + 6, rgb(30, 30, 34), BODY);
        for (int k = 0; k < 6; k++) {
            float a = k * 1.047f + (pose == POSE_SHOOT ? 0.5f : 0.f);
            p.ellipse(33 + std::cos(a) * 3.2f, gy + std::sin(a) * 3.2f, 1.4f, 1.4f, rgb(8, 8, 8), BODY);
        }
        p.box(42 + bw, 22 + by, 47 + bw, 34 + by, rgb(140, 110, 40), BODY);              // ammo drum
    }
    if (gear == GEAR_STALKER) {
        p.capsule(44, 26 + by, 50, 42 + by, 1.3f, rgb(200, 210, 220), LIMB);            // arm blade
        p.ellipse(32, 9.7f + hy, 6.f, 1.6f, rgb(80, 255, 230), HEAD);                    // visor
    }

    if (pose == POSE_SHOOT) p.flash(31, 22, gear == GEAR_HEAVY ? 12.f : 9.f);

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

void bakeEnemyAtlas(std::vector<uint32_t> &atlas, std::vector<uint8_t> &mask, const SoldierLook &look, int gear, bool bulkyIn)
{
    const int S = ENEMY_TS, F = S * S;
    atlas.assign(size_t(EF_COUNT) * F, 0);
    mask.assign(size_t(EF_COUNT) * F, 0);
    bool bulky = &look == &ELITE_LOOK || bulkyIn;
    paintSoldier(&atlas[EF_AIM * F], &mask[EF_AIM * F], S, look, POSE_AIM, 0, bulky, gear);
    for (int i = 0; i < 4; i++)
        paintSoldier(&atlas[(EF_WALK0 + i) * F], &mask[(EF_WALK0 + i) * F], S, look, POSE_WALK, i * 0.25f, bulky, gear);
    paintSoldier(&atlas[EF_SHOOT * F], &mask[EF_SHOOT * F], S, look, POSE_SHOOT, 0, bulky, gear);
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

// Hover drone and sentry gun, painted with the same shaded primitives
void bakeMachineAtlas(std::vector<uint32_t> &atlas, std::vector<uint8_t> &mask, int kind)
{
    const int S = ENEMY_TS, F = S * S;
    atlas.assign(size_t(EF_COUNT) * F, 0);
    mask.assign(size_t(EF_COUNT) * F, 0);
    SoldierLook dummy = ENEMY_LOOK;
    for (int f = 0; f < EF_DEATH0; f++) {
        SP p{&atlas[size_t(f) * F], &mask[size_t(f) * F], S, float(S) / 64.f, &dummy};
        const uint8_t BODY = 1, HEAD = 2, LIMB = 3;
        if (kind == 0) {                                     // drone: body, four rotors, red eye
            float spin = f * 0.9f;
            for (int k = 0; k < 4; k++) {
                float rx = k % 2 ? 50.f : 14.f, ry = k < 2 ? 22.f : 34.f;
                p.capsule(32, 28, rx, ry, 1.6f, rgb(50, 52, 58), LIMB);
                // blurred rotor disc: a thin bright ellipse that wobbles per frame
                p.ellipse(rx, ry - 3.f, 9.f, 1.6f + 0.6f * std::sin(spin + k), rgb(150, 156, 166), LIMB);
            }
            p.ellipse(32, 29, 12.f, 8.f, rgb(70, 74, 82), BODY);
            p.box(24, 33, 40, 38, rgb(44, 46, 52), BODY);
            p.ellipse(32, 31, 4.f, 4.f, rgb(30, 30, 34), HEAD);
            p.ellipse(32, 31, 2.4f, 2.4f, f == EF_SHOOT ? rgb(255, 230, 160) : rgb(255, 40, 40), HEAD);
            p.capsule(26, 38, 22, 44, 1.2f, rgb(40, 40, 44), LIMB);
            p.capsule(38, 38, 42, 44, 1.2f, rgb(40, 40, 44), LIMB);
            if (f == EF_SHOOT) p.flash(32, 40, 8.f);
        } else if (kind == 2) {                              // egg sac: a glowing pod on roots
            float beat = 1.f + 0.06f * std::sin(f * 1.6f);
            for (int k = 0; k < 5; k++) p.capsule(32, 56, 14.f + k * 9.f, 63, 1.6f, rgb(70, 60, 40), LIMB);
            p.ellipse(32, 42, 14.f * beat, 18.f * beat, rgb(110, 170, 60), BODY);
            p.ellipse(32, 40, 9.f * beat, 12.f * beat, rgb(170, 230, 90), BODY);
            p.ellipse(32, 36, 4.f, 5.f, rgb(230, 255, 160), HEAD);                  // the core is the weak spot
            for (int k = 0; k < 6; k++) p.capsule(32, 30, 20.f + k * 5.f, 50.f - (k % 2) * 6.f, 0.7f, rgb(60, 110, 40), BODY);
        } else if (kind == 3) {                              // shield pylon: post + glowing core
            p.box(26, 20, 38, 63, rgb(70, 76, 86), BODY);
            p.box(24, 56, 40, 63, rgb(50, 54, 62), BODY);
            p.box(28, 24, 36, 30, rgb(90, 220, 255), HEAD);
            p.box(29, 34, 35, 52, rgb(40, 140, 200), BODY);
            p.ellipse(32, 16, 6.f, 6.f, rgb(140, 240, 255), HEAD);
        } else {                                             // sentry gun on a tripod
            p.capsule(32, 40, 18, 62, 2.f, rgb(46, 48, 52), LIMB);
            p.capsule(32, 40, 46, 62, 2.f, rgb(46, 48, 52), LIMB);
            p.capsule(32, 40, 32, 63, 2.f, rgb(40, 42, 46), LIMB);
            p.box(20, 22, 44, 40, rgb(110, 46, 40), BODY);
            p.box(21, 24, 43, 30, rgb(140, 60, 50), BODY);
            p.ellipse(32, 33, 5.f, 5.f, rgb(30, 30, 34), BODY);
            p.ellipse(32, 33, 2.6f, 2.6f, rgb(6, 6, 6), BODY);
            p.box(27, 16, 37, 22, rgb(36, 38, 42), HEAD);
            p.ellipse(32, 19, 2.f, 2.f, rgb(255, 50, 40), HEAD);
            if (f == EF_SHOOT) p.flash(32, 33, 10.f);
        }
    }
    // hit flinch and destruction: tip over like the soldiers
    rotateFrame(&atlas[EF_AIM * F], &mask[EF_AIM * F], &atlas[EF_HIT * F], &mask[EF_HIT * F], S, -6.f, 32, 62, 32, 62);
    const float ang[4] = { 20.f, 45.f, 70.f, 90.f };
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
