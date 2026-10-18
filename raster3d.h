// =============================================================================
//  raster3d.h  -  Our own 3D triangle rasterizer (no GPU, no QPainter)
//
//  Used for the first-person gun and for soldiers standing in the world.
//    back-face culling -> Sutherland-Hodgman near-plane clipping ->
//    perspective projection -> flat Lambert + Blinn specular + rim light ->
//    edge-function triangle fill with a per-pixel depth test
//  Two depth modes:
//    view model: zb holds 1/z, 0 = empty (its own buffer, always in front)
//    world:      zb is the world z-buffer holding view depth, so models are
//                hidden correctly by terrain and walls
// =============================================================================
#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "raster.h"
#include "game.h"

// ── Minimal 3D maths (camera space: x right, y down, z forward) ─────────────
struct V3 { float x, y, z; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 norm(V3 a) { float l = std::sqrt(dot(a, a)) + 1e-9f; return a * (1.f / l); }
// Rodrigues rotation of v about unit axis k by angle a
inline V3 rotate(V3 v, V3 k, float a) {
    float c = std::cos(a), s = std::sin(a);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
}

// ── Procedural materials (solid textures) ──────────────────────────────────────
// A 32^3 tile of value noise, sampled with trilinear filtering. Surfaces are
// textured from the model's own local coordinates, so a pattern stays glued to
// the body however it moves (a classic solid-texturing technique: no UV maps).
struct NoiseTile {
    uint8_t v[32 * 32 * 32];
    NoiseTile() {
        for (int i = 0; i < 32 * 32 * 32; i++) {
            uint32_t h = uint32_t(i) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
            v[i] = uint8_t(h & 255);
        }
    }
    float at(int x, int y, int z) const { return v[((z & 31) << 10) | ((y & 31) << 5) | (x & 31)] * (1.f / 255.f); }
    float sample(float x, float y, float z) const {
        int ix = int(x + 1024.f) - 1024, iy = int(y + 1024.f) - 1024, iz = int(z + 1024.f) - 1024;   // fast floor
        float fx = x - ix, fy = y - iy, fz = z - iz;
        fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
        float a = at(ix, iy, iz) + (at(ix + 1, iy, iz) - at(ix, iy, iz)) * fx;
        float b = at(ix, iy + 1, iz) + (at(ix + 1, iy + 1, iz) - at(ix, iy + 1, iz)) * fx;
        float c = at(ix, iy, iz + 1) + (at(ix + 1, iy, iz + 1) - at(ix, iy, iz + 1)) * fx;
        float d = at(ix, iy + 1, iz + 1) + (at(ix + 1, iy + 1, iz + 1) - at(ix, iy + 1, iz + 1)) * fx;
        float ab = a + (b - a) * fy, cd = c + (d - c) * fy;
        return ab + (cd - ab) * fz;
    }
};
inline const NoiseTile &noiseTile() { static const NoiseTile t; return t; }

// Pre-baked 64^3 lookup tiles (4 samples per noise cell, repeating every 16
// cells): per pixel a material then costs one or two array reads instead of
// trilinear noise. `smooth` holds filtered noise; `camo` the tone index of a
// two-octave camouflage pattern (0 base, 1 dark, 2 light).
struct MatTiles {
    std::vector<uint8_t> smooth, camo;       // 64^3 (4 samples / cell) and 128^3 (8 samples / cell, crisper blotch edges)
    MatTiles() : smooth(size_t(64) * 64 * 64), camo(size_t(128) * 128 * 128) {
        const NoiseTile &N = noiseTile();
        // wrap the 16-cell period so the tiles repeat seamlessly
        auto per = [&](float a, float b, float c) { return N.sample(std::fmod(a, 16.f), std::fmod(b, 16.f), std::fmod(c, 16.f)); };
        for (int z = 0; z < 64; z++) for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++)
            smooth[size_t((z << 12) | (y << 6) | x)] = uint8_t(per(x * 0.25f, y * 0.25f, z * 0.25f) * 255.f);
        for (int z = 0; z < 128; z++) for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
            float fx = x * 0.125f, fy = y * 0.125f, fz = z * 0.125f;
            float n = per(fx, fy, fz) * 0.65f + per(fx * 2.f + 3.f, fy * 2.f + 5.f, fz * 2.f + 7.f) * 0.35f;
            camo[size_t((z << 14) | (y << 7) | x)] = uint8_t(n < 0.4f ? 1 : n > 0.6f ? 2 : 0);
        }
    }
    static int idx(float u, float v, float w) {
        return ((int(w * 4.f + 4096.f) & 63) << 12) | ((int(v * 4.f + 4096.f) & 63) << 6) | (int(u * 4.f + 4096.f) & 63);
    }
    static int idxC(float u, float v, float w) {
        return ((int(w * 8.f + 8192.f) & 127) << 14) | ((int(v * 8.f + 8192.f) & 127) << 7) | (int(u * 8.f + 8192.f) & 127);
    }
};
inline const MatTiles &matTiles() { static const MatTiles t; return t; }

enum MatKind { MAT_NONE = 0, MAT_CLOTH, MAT_CAMO, MAT_METAL, MAT_SKIN, MAT_RUBBER, MAT_PLASTIC };
struct Mat {
    int kind = MAT_NONE;
    uint32_t c0 = 0, c1 = 0, c2 = 0;   // base colour (camo: base, dark, light blotches)
    float scale = 8.f;                 // pattern frequency (per metre)
    float shine = 0.1f;                // specular strength
};

struct Raster3D {
    rs::Canvas &cv;
    float *zb;
    static constexpr float F = 560.f;            // view-model focal length (pixels)
    float fx = F, fy = F, cx = W / 2.f, cy = H / 2.f;
    float nearZ = 0.03f;
    bool  worldZ = false;                        // false: zb = 1/z (view model); true: zb = view depth
    V3    light = norm(V3{-0.35f, -0.85f, -0.45f});
    // World models: light level, fog colour and density (0 = no fog)
    float lightMul = 1.f;
    uint32_t fogCol = 0;
    float fogDensity = 0.f;
    float alpha = 1.f;                           // < 1: ordered-dither fade (camera too close)
    int   clipX0 = 0, clipY0 = 0, clipX1 = W - 1, clipY1 = H - 1;   // pixels outside this box are never touched
    bool  emissive = false;                      // glowing parts: unlit, full brightness
    // High-quality path (near models): material per pixel, sky / ground ambient,
    // body-local texture frame, and a mask of model pixels for edge smoothing
    Mat   mat;
    V3    upV{0, -1, 0};                         // world up in camera space
    uint32_t skyTint = 0xFFB4C8E6, gndTint = 0xFF8C7860;
    bool  texFrame = false;
    V3    tO{0, 0, 0}, tInv[3] = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
    uint8_t *mask = nullptr;                     // set: model pixels are marked 1
    int   mx0 = W, my0 = H, mx1 = -1, my1 = -1;  // bounding box of marked pixels
    // Camera-space images of the local axes (scale included) and the origin
    void setLocalFrame(V3 O, V3 A, V3 B, V3 C) {
        float det = dot(A, cross(B, C));
        if (std::fabs(det) < 1e-12f) { texFrame = false; return; }
        float id = 1.f / det;
        tO = O; tInv[0] = cross(B, C) * id; tInv[1] = cross(C, A) * id; tInv[2] = cross(A, B) * id;
        texFrame = true;
    }
    void markPx(int x, int y) {
        if (!mask) return;
        mask[y * W + x] = 1;
        mx0 = std::min(mx0, x); my0 = std::min(my0, y); mx1 = std::max(mx1, x); my1 = std::max(my1, y);
    }
    // Material colour at a local position (u, v, w): channels 0..255 as floats
    void matColor(float u, float v, float w, float &r, float &g, float &b) const {
        const NoiseTile &N = noiseTile();
        const MatTiles &T = matTiles();
        const float inv255 = 1.f / 255.f;
        float s = mat.scale;
        float k = 1.f;
        uint32_t c = mat.c0;
        switch (mat.kind) {
        case MAT_CAMO: {
            // two octaves of blotches pick one of three tones; a fine weave on top
            int t = T.camo[size_t(MatTiles::idxC(u * s, v * s, w * s))];
            c = t == 1 ? mat.c1 : t == 2 ? mat.c2 : mat.c0;
            k = 0.93f + 0.1f * N.at(int(u * 160.f), int(v * 160.f), int(w * 160.f));
            break;
        }
        case MAT_CLOTH:
            k = 0.86f + 0.2f * inv255 * T.smooth[size_t(MatTiles::idx(u * s, v * s, w * s))] + 0.06f * N.at(int(u * 150.f), int(v * 150.f), int(w * 150.f));
            break;
        case MAT_METAL:
            k = 0.88f + 0.14f * inv255 * T.smooth[size_t(MatTiles::idx(u * 30.f, v * 30.f, w * 30.f))] + 0.05f * N.at(int(u * 220.f), int(v * 60.f), int(w * 220.f));
            break;
        case MAT_SKIN:
            k = 0.94f + 0.1f * inv255 * T.smooth[size_t(MatTiles::idx(u * 26.f, v * 26.f, w * 26.f))];
            break;
        case MAT_RUBBER:
            k = 0.85f + 0.2f * N.at(int(u * 120.f), int(v * 120.f), int(w * 120.f));
            break;
        default:
            k = 0.95f + 0.08f * inv255 * T.smooth[size_t(MatTiles::idx(u * 20.f, v * 20.f, w * 20.f))];
            break;
        }
        r = rs::R(c) * k; g = rs::G(c) * k; b = rs::B(c) * k;
    }
    // Per-vertex lighting for the HQ path: sun Lambert, sky/ground hemisphere, rim; specular separately
    void lightAt(V3 p, V3 n, float &lr, float &lg, float &lb, float &spec, float &fog) const {
        float diff = std::max(0.f, dot(n, light));
        float hemi = 0.5f + 0.5f * dot(n, upV);
        V3 view = norm(p * -1.f), half = norm(light + view);
        float h = std::max(0.f, dot(n, half)), h4 = h * h * h * h, h8 = h4 * h4, h16 = h8 * h8;
        spec = (h16 * h8 * h4 * mat.shine + h4 * 0.04f * mat.shine) * lightMul * 255.f;     // a sharp highlight and a soft sheen
        float v = 1.f - std::max(0.f, dot(n, view));
        float rim = v * v * v * 0.22f;
        auto ch = [&](int sky, int gnd) { return (1.05f * diff + (0.16f + 0.2f * hemi) * (sky * hemi + gnd * (1.f - hemi)) / 255.f * 1.4f + rim) * lightMul * 1.3f; };
        lr = ch(rs::R(skyTint), rs::R(gndTint)); lg = ch(rs::G(skyTint), rs::G(gndTint)); lb = ch(rs::B(skyTint), rs::B(gndTint));
        fog = fogDensity > 0.f ? std::exp(-fogDensity * std::max(0.f, p.z)) : 1.f;
    }
    struct HQV { float sx, sy, iz, lr, lg, lb, sp, fg, u, v, w; };
    void triHQ(HQV a, HQV b, HQV c) {
        static const uint8_t BAYER[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
        float area = (b.sx - a.sx) * (c.sy - a.sy) - (b.sy - a.sy) * (c.sx - a.sx);
        if (std::fabs(area) < 1e-4f) return;
        if (area < 0) { std::swap(b, c); area = -area; }
        int minX = std::max(clipX0, int(std::floor(std::min({a.sx, b.sx, c.sx})))), maxX = std::min(clipX1, int(std::ceil(std::max({a.sx, b.sx, c.sx}))));
        int minY = std::max(clipY0, int(std::floor(std::min({a.sy, b.sy, c.sy})))), maxY = std::min(clipY1, int(std::ceil(std::max({a.sy, b.sy, c.sy}))));
        if (minX > maxX || minY > maxY) return;
        float inv = 1.f / area;
        int ditherCut = int(alpha * 16.f);
        const float fr = rs::R(fogCol), fgc = rs::G(fogCol), fb = rs::B(fogCol);
        const float dw0 = -(c.sy - b.sy), dw1 = -(a.sy - c.sy), dw2 = -(b.sy - a.sy);
        // attributes are linear on the screen: per-pixel steps along x instead of re-blending
        enum { IZ, LR, LG, LB, SP, FG, TU, TV, TW, NA };
        const float A0[NA] = { a.iz, a.lr, a.lg, a.lb, a.sp, a.fg, a.u, a.v, a.w };
        const float A1[NA] = { b.iz, b.lr, b.lg, b.lb, b.sp, b.fg, b.u, b.v, b.w };
        const float A2[NA] = { c.iz, c.lr, c.lg, c.lb, c.sp, c.fg, c.u, c.v, c.w };
        float dA[NA];
        for (int k = 0; k < NA; k++) dA[k] = (dw0 * A0[k] + dw1 * A1[k] + dw2 * A2[k]) * inv;
        for (int y = minY; y <= maxY; y++) {
            float py = y + 0.5f, px = minX + 0.5f;
            float w0 = (c.sx - b.sx) * (py - b.sy) - (c.sy - b.sy) * (px - b.sx);
            float w1 = (a.sx - c.sx) * (py - c.sy) - (a.sy - c.sy) * (px - c.sx);
            float w2 = (b.sx - a.sx) * (py - a.sy) - (b.sy - a.sy) * (px - a.sx);
            float At[NA];
            for (int k = 0; k < NA; k++) At[k] = (w0 * A0[k] + w1 * A1[k] + w2 * A2[k]) * inv;
            bool was = false;
            for (int x = minX; x <= maxX; x++, w0 += dw0, w1 += dw1, w2 += dw2) {
                if (x > minX) for (int k = 0; k < NA; k++) At[k] += dA[k];
                if (w0 < 0 || w1 < 0 || w2 < 0) { if (was) break; continue; }
                was = true;
                if (ditherCut < 16 && BAYER[y & 3][x & 3] >= ditherCut) continue;
                float &d = zb[y * W + x];
                if (worldZ) { float z = 1.f / At[IZ]; if (z >= d) continue; d = z; }
                else { if (At[IZ] <= d) continue; d = At[IZ]; }
                float mr, mg, mb;
                matColor(At[TU], At[TV], At[TW], mr, mg, mb);
                float sp = At[SP], fg = At[FG];
                float r = mr * At[LR] + sp, g = mg * At[LG] + sp, bl = mb * At[LB] + sp;
                r = fr + (r - fr) * fg; g = fgc + (g - fgc) * fg; bl = fb + (bl - fb) * fg;
                int ri = int(r), gi = int(g), bi = int(bl);
                ri = ri < 0 ? 0 : ri > 255 ? 255 : ri; gi = gi < 0 ? 0 : gi > 255 ? 255 : gi; bi = bi < 0 ? 0 : bi > 255 ? 255 : bi;
                cv.p[y * W + x] = 0xFF000000u | (uint32_t(ri) << 16) | (uint32_t(gi) << 8) | uint32_t(bi);
                markPx(x, y);
            }
        }
    }
    // A smooth polygon through the HQ path: lit per vertex, material per pixel
    void faceHQ(const V3 *pts, const V3 *nrm, int n, V3 faceN) {
        V3 c{0, 0, 0};
        for (int i = 0; i < n; i++) c = c + pts[i];
        c = c * (1.f / n);
        if (dot(faceN, c) >= 0.f) return;
        HQV vv[16]; V3 cp[16]; int m = 0;
        auto make = [&](V3 p, V3 nn) {
            HQV q;
            lightAt(p, nn, q.lr, q.lg, q.lb, q.sp, q.fg);
            V3 d = p - tO;
            q.u = texFrame ? dot(tInv[0], d) : p.x; q.v = texFrame ? dot(tInv[1], d) : p.y; q.w = texFrame ? dot(tInv[2], d) : p.z;
            q.iz = 0; q.sx = q.sy = 0;
            return q;
        };
        for (int i = 0; i < n && m < 14; i++) {
            int j = (i + 1) % n;
            V3 a = pts[i], b = pts[j];
            bool ain = a.z >= nearZ, bin = b.z >= nearZ;
            if (ain) { cp[m] = a; vv[m++] = make(a, nrm[i]); }
            if (ain != bin) { float t = (nearZ - a.z) / (b.z - a.z); V3 p = a + (b - a) * t; cp[m] = p; vv[m++] = make(p, norm(nrm[i] + (nrm[j] - nrm[i]) * t)); }
        }
        if (m < 3) return;
        for (int i = 0; i < m; i++) { project(cp[i], vv[i].sx, vv[i].sy); vv[i].iz = 1.f / cp[i].z; }
        for (int i = 1; i + 1 < m; i++) triHQ(vv[0], vv[i], vv[i + 1]);
    }
    // Shadow pass: the geometry arrives already flattened onto the ground; every
    // covered pixel that lies on the visible surface is darkened exactly once
    // (a stamp per pixel acts as the stencil, so overlapping parts don't stack)
    bool  shadowMode = false;
    uint16_t *stamp = nullptr;
    uint16_t stampId = 1;
    float shadowK = 0.45f;                       // fraction of light the shadow removes
    void triShadow(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2) {
        float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (std::fabs(area) < 1e-4f) return;
        if (area < 0) { std::swap(x1, x2); std::swap(y1, y2); std::swap(z1, z2); area = -area; }
        int minX = std::max(clipX0, int(std::floor(std::min({x0, x1, x2})))), maxX = std::min(clipX1, int(std::ceil(std::max({x0, x1, x2}))));
        int minY = std::max(clipY0, int(std::floor(std::min({y0, y1, y2})))), maxY = std::min(clipY1, int(std::ceil(std::max({y0, y1, y2}))));
        float inv = 1.f / area;
        int keep = int((1.f - shadowK) * 256.f);
        for (int y = minY; y <= maxY; y++) {
            float py = y + 0.5f;
            for (int x = minX; x <= maxX; x++) {
                float px = x + 0.5f;
                float w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
                float w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
                float w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                int idx = y * W + x;
                if (stamp[idx] == stampId) continue;
                float z = 1.f / ((w0 * z0 + w1 * z1 + w2 * z2) * inv);
                float d = zb[idx];
                float tol = 0.12f + 0.04f * z;                            // grazing views stretch depth errors
                if (z > d + tol || z < d - tol) continue;                // not on the visible ground here
                stamp[idx] = stampId;
                uint32_t c = cv.p[idx];
                cv.p[idx] = 0xFF000000u | (uint32_t((rs::R(c) * keep) >> 8) << 16) | (uint32_t((rs::G(c) * keep) >> 8) << 8) | uint32_t((rs::B(c) * keep) >> 8);
            }
        }
    }
    void shadowPoly(const V3 *pts, int n) {
        V3 clip[16]; int m = 0;
        for (int i = 0; i < n && m < 14; i++) {
            V3 a = pts[i], b = pts[(i + 1) % n];
            bool ain = a.z >= nearZ, bin = b.z >= nearZ;
            if (ain) clip[m++] = a;
            if (ain != bin) { float t = (nearZ - a.z) / (b.z - a.z); clip[m++] = a + (b - a) * t; }
        }
        if (m < 3) return;
        float sx[16], sy[16], iz[16];
        for (int i = 0; i < m; i++) { project(clip[i], sx[i], sy[i]); iz[i] = 1.f / clip[i].z; }
        for (int i = 1; i + 1 < m; i++) triShadow(sx[0], sy[0], iz[0], sx[i], sy[i], iz[i], sx[i + 1], sy[i + 1], iz[i + 1]);
    }

    bool project(V3 p, float &sx, float &sy) const {
        if (p.z < nearZ) return false;
        sx = cx + fx * p.x / p.z; sy = cy + fy * p.y / p.z;
        return true;
    }
    // Depth-tested, flat-coloured triangle (edge functions, 1/z interpolated)
    void tri(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint32_t col, bool blend) {
        static const uint8_t BAYER[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
        float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (std::fabs(area) < 1e-4f) return;
        if (area < 0) { std::swap(x1, x2); std::swap(y1, y2); std::swap(z1, z2); area = -area; }
        int minX = std::max(clipX0, int(std::floor(std::min({x0, x1, x2})))), maxX = std::min(clipX1, int(std::ceil(std::max({x0, x1, x2}))));
        int minY = std::max(clipY0, int(std::floor(std::min({y0, y1, y2})))), maxY = std::min(clipY1, int(std::ceil(std::max({y0, y1, y2}))));
        float inv = 1.f / area;
        int ditherCut = int(alpha * 16.f);
        for (int y = minY; y <= maxY; y++) {
            float py = y + 0.5f;
            for (int x = minX; x <= maxX; x++) {
                float px = x + 0.5f;
                float w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
                float w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
                float w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                if (ditherCut < 16 && BAYER[y & 3][x & 3] >= ditherCut) continue;
                float iz = (w0 * z0 + w1 * z1 + w2 * z2) * inv;          // interpolated 1/z
                float &d = zb[y * W + x];
                if (worldZ) {
                    float z = 1.f / iz;
                    if (z >= d) continue;
                    if (blend) cv.blend(x, y, col);
                    else { cv.p[y * W + x] = col; d = z; markPx(x, y); }
                } else {
                    if (iz <= d) continue;
                    if (blend) cv.blend(x, y, col);
                    else { cv.p[y * W + x] = col; d = iz; }
                }
            }
        }
    }
    // One planar face: cull, light, clip to the near plane, project, fill
    void face(const V3 *pts, int n, V3 normal, uint32_t base, float specK, bool glass = false) {
        if (shadowMode) { shadowPoly(pts, n); return; }
        if (mat.kind != MAT_NONE && !emissive && !glass && n <= 14) { V3 nn[14]; for (int i = 0; i < n; i++) nn[i] = normal; faceHQ(pts, nn, n, normal); return; }
        V3 c{0, 0, 0};
        for (int i = 0; i < n; i++) c = c + pts[i];
        c = c * (1.f / n);
        if (dot(normal, c) >= 0.f) return;                            // back-face culling
        if (emissive) {                                               // self-lit: no shading, just a facing tint
            float k = 0.85f + 0.15f * std::max(0.f, -dot(normal, norm(c)));
            uint32_t col = rs::scaleCol(base, k * 1.25f);
            V3 clip[16]; int m = 0;
            for (int i = 0; i < n && m < 14; i++) {
                V3 a = pts[i], b = pts[(i + 1) % n];
                bool ain = a.z >= nearZ, bin = b.z >= nearZ;
                if (ain) clip[m++] = a;
                if (ain != bin) { float t = (nearZ - a.z) / (b.z - a.z); clip[m++] = a + (b - a) * t; }
            }
            if (m < 3) return;
            float sx[16], sy[16], iz[16];
            for (int i = 0; i < m; i++) { project(clip[i], sx[i], sy[i]); iz[i] = 1.f / clip[i].z; }
            for (int i = 1; i + 1 < m; i++) tri(sx[0], sy[0], iz[0], sx[i], sy[i], iz[i], sx[i + 1], sy[i + 1], iz[i + 1], col, false);
            return;
        }
        float diff = std::max(0.f, dot(normal, light));
        V3 view = norm(c * -1.f), half = norm(light + view);
        float spec = std::pow(std::max(0.f, dot(normal, half)), 28.f) * specK;
        float rim = std::pow(1.f - std::max(0.f, dot(normal, view)), 3.f) * 0.18f;
        float shade = (0.30f + 0.95f * diff + rim) * lightMul;
        spec *= lightMul;
        uint32_t col;
        if (glass) col = rs::withAlpha(rs::lerpCol(base, rs::rgb(235, 245, 255), 0.25f + 0.5f * diff + spec), 215);
        else col = rs::rgb(int(rs::R(base) * shade * 1.35f + 255 * spec), int(rs::G(base) * shade * 1.35f + 255 * spec), int(rs::B(base) * shade * 1.35f + 255 * spec));
        if (fogDensity > 0.f) col = (col & 0xFF000000) | (rs::lerpCol(fogCol, col, std::exp(-fogDensity * std::max(0.f, c.z))) & 0xFFFFFF);
        // Sutherland–Hodgman against z = near
        V3 clip[16]; int m = 0;
        for (int i = 0; i < n && m < 14; i++) {
            V3 a = pts[i], b = pts[(i + 1) % n];
            bool ain = a.z >= nearZ, bin = b.z >= nearZ;
            if (ain) clip[m++] = a;
            if (ain != bin) { float t = (nearZ - a.z) / (b.z - a.z); clip[m++] = a + (b - a) * t; }
        }
        if (m < 3) return;
        float sx[16], sy[16], iz[16];
        for (int i = 0; i < m; i++) { project(clip[i], sx[i], sy[i]); iz[i] = 1.f / clip[i].z; }
        for (int i = 1; i + 1 < m; i++) tri(sx[0], sy[0], iz[0], sx[i], sy[i], iz[i], sx[i + 1], sy[i + 1], iz[i + 1], col, glass);
    }
    // Extruded convex profile: 2 caps + one quad per edge
    template <class Map>
    void extrude(const float *uh, int n, float width, Map map, V3 Fd, V3 Ud, V3 Rd, uint32_t col, float specK, bool glass = false) {
        V3 front[6], back[6];
        float area = 0;
        for (int k = 0; k < n; k++) {
            front[k] = map(uh[2 * k], uh[2 * k + 1], width * 0.5f);
            back[k]  = map(uh[2 * k], uh[2 * k + 1], -width * 0.5f);
            int j = (k + 1) % n;
            area += uh[2 * k] * uh[2 * j + 1] - uh[2 * j] * uh[2 * k + 1];
        }
        face(front, n, Rd, col, specK, glass);
        face(back, n, Rd * -1.f, col, specK, glass);
        if (glass) return;
        for (int k = 0; k < n; k++) {
            int j = (k + 1) % n;
            float eu = uh[2 * j] - uh[2 * k], eh = uh[2 * j + 1] - uh[2 * k + 1];
            float nu = area > 0 ? eh : -eh, nh = area > 0 ? -eu : eu;
            float l = std::sqrt(nu * nu + nh * nh);
            if (l < 1e-5f) continue;
            V3 q[4] = { front[k], front[j], back[j], back[k] };
            face(q, 4, norm(Fd * (nu / l) + Ud * (nh / l)), col, specK);
        }
    }
    // Tapered box from a (half-size ra) to b (half-size rb): limbs, sleeves
    void box(V3 a, V3 b, float ra, float rb, uint32_t col, float specK) {
        V3 ax = norm(b - a), ref = std::fabs(ax.y) > 0.9f ? V3{1, 0, 0} : V3{0, -1, 0};
        V3 s1 = norm(cross(ax, ref)), s2 = cross(s1, ax);
        boxAxes(a, b, s1, s2, ra, ra, rb, rb, col, specK);
    }
    // ── Smooth (Gouraud) shading: lighting per vertex, colours interpolated ───────
    // Same light model as face(): Lambert + Blinn specular + rim, then fog
    uint32_t shadeAt(V3 p, V3 n, uint32_t base, float specK) const {
        if (emissive) return rs::scaleCol(base, 1.1f);
        float diff = std::max(0.f, dot(n, light));
        V3 view = norm(p * -1.f), half = norm(light + view);
        float h = std::max(0.f, dot(n, half)), h4 = h * h * h * h, h8 = h4 * h4;
        float spec = h8 * h8 * h8 * h4 * specK * lightMul;                  // h^28 without pow()
        float v = 1.f - std::max(0.f, dot(n, view));
        float rim = v * v * v * 0.18f;
        float shade = (0.30f + 0.95f * diff + rim) * lightMul;
        uint32_t col = rs::rgb(int(rs::R(base) * shade * 1.35f + 255 * spec), int(rs::G(base) * shade * 1.35f + 255 * spec),
                               int(rs::B(base) * shade * 1.35f + 255 * spec));
        if (fogDensity > 0.f) col = rs::lerpCol(fogCol, col, std::exp(-fogDensity * std::max(0.f, p.z)));
        return col;
    }
    // Triangle with a colour per vertex (barycentric interpolation) and the depth test
    void triG(float x0, float y0, float z0, uint32_t c0, float x1, float y1, float z1, uint32_t c1, float x2, float y2, float z2, uint32_t c2) {
        static const uint8_t BAYER[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
        float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (std::fabs(area) < 1e-4f) return;
        if (area < 0) { std::swap(x1, x2); std::swap(y1, y2); std::swap(z1, z2); std::swap(c1, c2); area = -area; }
        int minX = std::max(clipX0, int(std::floor(std::min({x0, x1, x2})))), maxX = std::min(clipX1, int(std::ceil(std::max({x0, x1, x2}))));
        int minY = std::max(clipY0, int(std::floor(std::min({y0, y1, y2})))), maxY = std::min(clipY1, int(std::ceil(std::max({y0, y1, y2}))));
        float inv = 1.f / area;
        int ditherCut = int(alpha * 16.f);
        const float r0 = rs::R(c0), g0 = rs::G(c0), b0 = rs::B(c0), r1 = rs::R(c1), g1 = rs::G(c1), b1 = rs::B(c1), r2 = rs::R(c2), g2 = rs::G(c2), b2 = rs::B(c2);
        // edge functions are linear in x: step them across the row instead of re-evaluating
        const float dw0 = -(y2 - y1), dw1 = -(y0 - y2), dw2 = -(y1 - y0);
        for (int y = minY; y <= maxY; y++) {
            float py = y + 0.5f, px = minX + 0.5f;
            float w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
            float w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
            float w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
            bool was = false;
            for (int x = minX; x <= maxX; x++, w0 += dw0, w1 += dw1, w2 += dw2) {
                if (w0 < 0 || w1 < 0 || w2 < 0) { if (was) break; continue; }     // left the (convex) span
                was = true;
                if (ditherCut < 16 && BAYER[y & 3][x & 3] >= ditherCut) continue;
                float b0w = w0 * inv, b1w = w1 * inv, b2w = w2 * inv;
                float iz = b0w * z0 + b1w * z1 + b2w * z2;
                float &d = zb[y * W + x];
                if (worldZ) { float z = 1.f / iz; if (z >= d) continue; d = z; }
                else { if (iz <= d) continue; d = iz; }
                cv.p[y * W + x] = 0xFF000000u | (uint32_t(b0w * r0 + b1w * r1 + b2w * r2) << 16) |
                                  (uint32_t(b0w * g0 + b1w * g1 + b2w * g2) << 8) | uint32_t(b0w * b0 + b1w * b1 + b2w * b2);
                markPx(x, y);
            }
        }
    }
    // Planar polygon with per-vertex normals: culled by its face normal, lit per vertex,
    // clipped to the near plane (colours interpolated along clipped edges), fan-filled
    void faceSmooth(const V3 *pts, const V3 *nrm, int n, V3 faceN, uint32_t base, float specK) {
        if (shadowMode) { shadowPoly(pts, n); return; }
        if (mat.kind != MAT_NONE && !emissive) { faceHQ(pts, nrm, n, faceN); return; }
        V3 c{0, 0, 0};
        for (int i = 0; i < n; i++) c = c + pts[i];
        c = c * (1.f / n);
        if (dot(faceN, c) >= 0.f) return;
        uint32_t vc[8];
        for (int i = 0; i < n; i++) vc[i] = shadeAt(pts[i], nrm[i], base, specK);
        V3 clip[16]; uint32_t cc[16]; int m = 0;
        for (int i = 0; i < n && m < 14; i++) {
            int j = (i + 1) % n;
            V3 a = pts[i], b = pts[j];
            bool ain = a.z >= nearZ, bin = b.z >= nearZ;
            if (ain) { clip[m] = a; cc[m++] = vc[i]; }
            if (ain != bin) { float t = (nearZ - a.z) / (b.z - a.z); clip[m] = a + (b - a) * t; cc[m++] = rs::lerpCol(vc[i], vc[j], t); }
        }
        if (m < 3) return;
        float sx[16], sy[16], iz[16];
        for (int i = 0; i < m; i++) { project(clip[i], sx[i], sy[i]); iz[i] = 1.f / clip[i].z; }
        for (int i = 1; i + 1 < m; i++) triG(sx[0], sy[0], iz[0], cc[0], sx[i], sy[i], iz[i], cc[i], sx[i + 1], sy[i + 1], iz[i + 1], cc[i + 1]);
    }
    // A grid of quads that share their vertices (rows x cols, row-major; `wrap` joins
    // the last column to the first). Every vertex is projected and lit ONCE (HQ:
    // lighting + texture coordinates; otherwise a Gouraud colour), then each
    // visible quad is filled straight from that cache. Quads that cross the near
    // plane fall back to the clipping path.
    void grid(const V3 *P, const V3 *N, int rows, int cols, bool wrap, uint32_t col, float specK) {
        static std::vector<HQV> hv;
        static std::vector<uint32_t> gc;
        static std::vector<uint8_t> ok;
        int nv = rows * cols;
        int qc = wrap ? cols : cols - 1;
        if (shadowMode) {
            for (int r = 0; r + 1 < rows; r++) for (int c = 0; c < qc; c++) {
                int c1 = (c + 1) % cols;
                V3 q[4] = { P[r * cols + c], P[r * cols + c1], P[(r + 1) * cols + c1], P[(r + 1) * cols + c] };
                shadowPoly(q, 4);
            }
            return;
        }
        bool hqm = mat.kind != MAT_NONE && !emissive;
        if (int(hv.size()) < nv) { hv.resize(size_t(nv)); gc.resize(size_t(nv)); ok.resize(size_t(nv)); }
        bool done[512] = {};
        auto prep = [&](int i) {
            if (i < 512 && done[i]) return;
            if (i < 512) done[i] = true;
            ok[size_t(i)] = P[i].z >= nearZ;
            if (!ok[size_t(i)]) return;
            HQV &h = hv[size_t(i)];
            project(P[i], h.sx, h.sy); h.iz = 1.f / P[i].z;
            if (hqm) {
                lightAt(P[i], N[i], h.lr, h.lg, h.lb, h.sp, h.fg);
                V3 d = P[i] - tO;
                h.u = texFrame ? dot(tInv[0], d) : P[i].x; h.v = texFrame ? dot(tInv[1], d) : P[i].y; h.w = texFrame ? dot(tInv[2], d) : P[i].z;
            } else gc[size_t(i)] = shadeAt(P[i], N[i], col, specK);
        };
        for (int r = 0; r + 1 < rows; r++) for (int c = 0; c < qc; c++) {
            int c1 = (c + 1) % cols;
            int i0 = r * cols + c, i1 = r * cols + c1, i2 = (r + 1) * cols + c1, i3 = (r + 1) * cols + c;
            V3 fn = norm(N[i0] + N[i1] + N[i2] + N[i3]);
            V3 ctr = (P[i0] + P[i1] + P[i2] + P[i3]) * 0.25f;
            if (dot(fn, ctr) >= 0.f) continue;                               // back-face
            prep(i0); prep(i1); prep(i2); prep(i3);
            if (!(ok[size_t(i0)] && ok[size_t(i1)] && ok[size_t(i2)] && ok[size_t(i3)])) {
                V3 q[4] = { P[i0], P[i1], P[i2], P[i3] }, qn[4] = { N[i0], N[i1], N[i2], N[i3] };
                faceSmooth(q, qn, 4, fn, col, specK);
                continue;
            }
            if (hqm) { triHQ(hv[size_t(i0)], hv[size_t(i1)], hv[size_t(i2)]); triHQ(hv[size_t(i0)], hv[size_t(i2)], hv[size_t(i3)]); }
            else {
                const HQV &a = hv[size_t(i0)], &b = hv[size_t(i1)], &c2 = hv[size_t(i2)], &d = hv[size_t(i3)];
                triG(a.sx, a.sy, a.iz, gc[size_t(i0)], b.sx, b.sy, b.iz, gc[size_t(i1)], c2.sx, c2.sy, c2.iz, gc[size_t(i2)]);
                triG(a.sx, a.sy, a.iz, gc[size_t(i0)], c2.sx, c2.sy, c2.iz, gc[size_t(i2)], d.sx, d.sy, d.iz, gc[size_t(i3)]);
            }
        }
    }
    // Rounded tube from a to b with an elliptical cross-section (r1 along s1, r2 along
    // s2), smooth normals around it and flat end caps: limbs, torso, boots
    // `squareness` > 2 turns the ellipse into a superellipse |x|^n + |y|^n = 1: a box with rounded edges
    void tube(V3 a, V3 b, V3 s1, V3 s2, float ra1, float ra2, float rb1, float rb2, uint32_t col, float specK, int sides = 10, bool caps = true, float squareness = 2.f) {
        V3 ax = norm(b - a);
        // keep the side axes perpendicular to the tube
        s1 = norm(s1 - ax * dot(s1, ax)); s2 = norm(cross(ax, s1));
        if (dot(s2, s2) < 0.5f) s2 = norm(cross(ax, V3{0, 1, 0}));
        sides = std::max(3, std::min(sides, 16));
        V3 A[16], B[16], N[16];
        for (int k = 0; k < sides; k++) {
            float t = 6.2831853f * (k + (squareness > 2.f ? 0.5f : 0.f)) / sides, cs = std::cos(t), sn = std::sin(t);
            float ex = 2.f / squareness;
            float qc = cs < 0 ? -std::pow(-cs, ex) : std::pow(cs, ex), qs = sn < 0 ? -std::pow(-sn, ex) : std::pow(sn, ex);
            A[k] = a + s1 * (qc * ra1) + s2 * (qs * ra2);
            B[k] = b + s1 * (qc * rb1) + s2 * (qs * rb2);
            // the gradient of the superellipse gives the surface normal
            float n1 = squareness > 2.f ? (qc < 0 ? -1.f : 1.f) * std::pow(std::fabs(qc), squareness - 1.f) : qc;
            float n2 = squareness > 2.f ? (qs < 0 ? -1.f : 1.f) * std::pow(std::fabs(qs), squareness - 1.f) : qs;
            N[k] = norm(s1 * (n1 / std::max(1e-3f, (ra1 + rb1) * 0.5f)) + s2 * (n2 / std::max(1e-3f, (ra2 + rb2) * 0.5f)));
        }
        {
            V3 G[32], GN[32];
            for (int k = 0; k < sides; k++) { G[k] = A[k]; G[sides + k] = B[k]; GN[k] = GN[sides + k] = N[k]; }
            grid(G, GN, 2, sides, true, col, specK);
        }
        if (caps) {
            V3 ca[16], cb[16];
            for (int k = 0; k < sides; k++) { ca[sides - 1 - k] = A[k]; cb[k] = B[k]; }
            face(ca, std::min(sides, 14), ax * -1.f, col, specK);
            face(cb, std::min(sides, 14), ax, col, specK);
        }
    }
    void tube(V3 a, V3 b, float ra, float rb, uint32_t col, float specK, int sides = 10) {
        V3 ax = norm(b - a), ref = std::fabs(ax.y) > 0.9f ? V3{1, 0, 0} : V3{0, -1, 0};
        V3 s1 = norm(cross(ax, ref));
        tube(a, b, s1, cross(s1, ax), ra, ra, rb, rb, col, specK, sides);
    }
    // Smooth ellipsoid: centre c, unit axes e1 e2 e3 with radii r1 r2 r3. `upper`
    // keeps only the half along +e3 (helmet domes); seg = rings around e3
    void ellipsoid(V3 c, V3 e1, V3 e2, V3 e3, float r1, float r2, float r3, uint32_t col, float specK, int seg = 10, bool upper = false) {
        seg = std::max(4, std::min(seg, 16));
        int rings = seg / 2 + 1;
        float lat0 = upper ? 0.f : -1.5707963f;
        auto P = [&](float lat, float lon, V3 &p, V3 &n) {
            float cl = std::cos(lat), sl = std::sin(lat), co = std::cos(lon), so = std::sin(lon);
            p = c + e1 * (cl * co * r1) + e2 * (cl * so * r2) + e3 * (sl * r3);
            n = norm(e1 * (cl * co / r1) + e2 * (cl * so / r2) + e3 * (sl / r3));
        };
        {
            // (rings + 1) x seg vertices; trig once per row and per column
            V3 G[17 * 16], GN[17 * 16];
            float co[16], so[16];
            for (int k = 0; k < seg; k++) { float o = 6.2831853f * k / seg; co[k] = std::cos(o); so[k] = std::sin(o); }
            for (int i = 0; i <= rings; i++) {
                float la = lat0 + (1.5707963f - lat0) * i / rings, cl = std::cos(la), sl = std::sin(la);
                if (i == rings) { cl = 0.f; sl = 1.f; }
                for (int k = 0; k < seg; k++) {
                    G[i * seg + k] = c + e1 * (cl * co[k] * r1) + e2 * (cl * so[k] * r2) + e3 * (sl * r3);
                    GN[i * seg + k] = norm(e1 * (cl * co[k] / r1) + e2 * (cl * so[k] / r2) + e3 * (sl / r3));
                }
            }
            grid(G, GN, rings + 1, seg, true, col, specK);
        }
        if (upper) {                                              // close the rim with a flat disc
            V3 disc[16];
            for (int k = 0; k < seg && k < 14; k++) { V3 n; P(0.f, -6.2831853f * k / seg, disc[k], n); }
            face(disc, std::min(seg, 14), e3 * -1.f, col, specK);
        }
    }
    // Box from a to b with explicit side axes and per-end half sizes (torso, head)
    void boxAxes(V3 a, V3 b, V3 s1, V3 s2, float ra1, float ra2, float rb1, float rb2, uint32_t col, float specK) {
        V3 ax = norm(b - a);
        V3 A[4] = { a + s1 * ra1 + s2 * ra2, a - s1 * ra1 + s2 * ra2, a - s1 * ra1 - s2 * ra2, a + s1 * ra1 - s2 * ra2 };
        V3 B[4] = { b + s1 * rb1 + s2 * rb2, b - s1 * rb1 + s2 * rb2, b - s1 * rb1 - s2 * rb2, b + s1 * rb1 - s2 * rb2 };
        V3 nrm[4] = { s2, s1 * -1.f, s2 * -1.f, s1 };
        for (int k = 0; k < 4; k++) {
            int j = (k + 1) % 4;
            V3 q[4] = { A[k], A[j], B[j], B[k] };
            face(q, 4, nrm[k], col, specK);
        }
        face(A, 4, ax * -1.f, col, specK);
        face(B, 4, ax, col, specK);
    }
};
