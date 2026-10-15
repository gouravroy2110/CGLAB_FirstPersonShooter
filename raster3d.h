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
    bool  emissive = false;                      // glowing parts: unlit, full brightness

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
        int minX = std::max(0, int(std::floor(std::min({x0, x1, x2})))), maxX = std::min(W - 1, int(std::ceil(std::max({x0, x1, x2}))));
        int minY = std::max(0, int(std::floor(std::min({y0, y1, y2})))), maxY = std::min(H - 1, int(std::ceil(std::max({y0, y1, y2}))));
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
                    else { cv.p[y * W + x] = col; d = z; }
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
        int minX = std::max(0, int(std::floor(std::min({x0, x1, x2})))), maxX = std::min(W - 1, int(std::ceil(std::max({x0, x1, x2}))));
        int minY = std::max(0, int(std::floor(std::min({y0, y1, y2})))), maxY = std::min(H - 1, int(std::ceil(std::max({y0, y1, y2}))));
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
            }
        }
    }
    // Planar polygon with per-vertex normals: culled by its face normal, lit per vertex,
    // clipped to the near plane (colours interpolated along clipped edges), fan-filled
    void faceSmooth(const V3 *pts, const V3 *nrm, int n, V3 faceN, uint32_t base, float specK) {
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
    // Rounded tube from a to b with an elliptical cross-section (r1 along s1, r2 along
    // s2), smooth normals around it and flat end caps: limbs, torso, boots
    void tube(V3 a, V3 b, V3 s1, V3 s2, float ra1, float ra2, float rb1, float rb2, uint32_t col, float specK, int sides = 10, bool caps = true) {
        V3 ax = norm(b - a);
        // keep the side axes perpendicular to the tube
        s1 = norm(s1 - ax * dot(s1, ax)); s2 = norm(cross(ax, s1));
        if (dot(s2, s2) < 0.5f) s2 = norm(cross(ax, V3{0, 1, 0}));
        sides = std::max(3, std::min(sides, 16));
        V3 A[16], B[16], N[16];
        for (int k = 0; k < sides; k++) {
            float t = 6.2831853f * k / sides, cs = std::cos(t), sn = std::sin(t);
            A[k] = a + s1 * (cs * ra1) + s2 * (sn * ra2);
            B[k] = b + s1 * (cs * rb1) + s2 * (sn * rb2);
            N[k] = norm(s1 * (cs / std::max(1e-3f, (ra1 + rb1) * 0.5f)) + s2 * (sn / std::max(1e-3f, (ra2 + rb2) * 0.5f)));
        }
        for (int k = 0; k < sides; k++) {
            int j = (k + 1) % sides;
            V3 q[4] = { A[k], A[j], B[j], B[k] }, qn[4] = { N[k], N[j], N[j], N[k] };
            faceSmooth(q, qn, 4, norm(N[k] + N[j]), col, specK);
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
        for (int i = 0; i < rings; i++) {
            float la = lat0 + (1.5707963f - lat0) * i / rings, lb = lat0 + (1.5707963f - lat0) * (i + 1) / rings;
            for (int k = 0; k < seg; k++) {
                float oa = 6.2831853f * k / seg, ob = 6.2831853f * (k + 1) / seg;
                V3 q[4], qn[4];
                P(la, oa, q[0], qn[0]); P(la, ob, q[1], qn[1]); P(lb, ob, q[2], qn[2]); P(lb, oa, q[3], qn[3]);
                V3 fn = norm(qn[0] + qn[1] + qn[2] + qn[3]);
                if (i == rings - 1) { V3 t[3] = { q[0], q[1], q[2] }, tn[3] = { qn[0], qn[1], qn[2] }; faceSmooth(t, tn, 3, fn, col, specK); }
                else faceSmooth(q, qn, 4, fn, col, specK);
            }
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
