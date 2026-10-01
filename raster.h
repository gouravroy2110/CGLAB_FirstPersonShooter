// =============================================================================
//  raster.h  —  Pure software raster primitives
//
//  Everything the game draws goes through these functions: they write 32-bit
//  ARGB pixels straight into a uint32_t framebuffer. No GPU, no vector paths.
//    • Bresenham lines (with thickness)
//    • Midpoint-style filled circles, soft radial glows, rings/arcs
//    • Edge-function triangle rasterizer with Gouraud (per-vertex colour) shading
//    • Convex polygon fill (triangle fan)
//    • Alpha blending + additive blending per pixel
// =============================================================================
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace rs {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline uint32_t argb(int a, int r, int g, int b) {
    return (uint32_t(clampi(a,0,255)) << 24) | (uint32_t(clampi(r,0,255)) << 16) |
           (uint32_t(clampi(g,0,255)) << 8)  |  uint32_t(clampi(b,0,255));
}
inline uint32_t rgb(int r, int g, int b) { return argb(255, r, g, b); }
inline int A(uint32_t c) { return int(c >> 24); }
inline int R(uint32_t c) { return int((c >> 16) & 255); }
inline int G(uint32_t c) { return int((c >> 8) & 255); }
inline int B(uint32_t c) { return int(c & 255); }
inline uint32_t withAlpha(uint32_t c, int a) { return (uint32_t(clampi(a,0,255)) << 24) | (c & 0xFFFFFF); }

inline uint32_t lerpCol(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0.f, 1.f);
    return argb(int(A(a) + (A(b) - A(a)) * t), int(R(a) + (R(b) - R(a)) * t),
                int(G(a) + (G(b) - G(a)) * t), int(B(a) + (B(b) - B(a)) * t));
}
// Multiply RGB by s (alpha kept)
inline uint32_t scaleCol(uint32_t c, float s) {
    return argb(A(c), int(R(c) * s), int(G(c) * s), int(B(c) * s));
}

// ── Canvas: a view onto a 32-bit pixel buffer ───────────────────────────────
struct Canvas {
    uint32_t *p = nullptr;
    int w = 0, h = 0;

    inline bool in(int x, int y) const { return unsigned(x) < unsigned(w) && unsigned(y) < unsigned(h); }

    inline void set(int x, int y, uint32_t c) { if (in(x, y)) p[y * w + x] = c; }

    // Source-over alpha blend using c's alpha channel
    inline void blend(int x, int y, uint32_t c) {
        if (!in(x, y)) return;
        uint32_t a = c >> 24;
        if (a == 0) return;
        uint32_t &d = p[y * w + x];
        if (a == 255) { d = c; return; }
        uint32_t ia = 255 - a;
        uint32_t rb = (((c & 0xFF00FF) * a + (d & 0xFF00FF) * ia) >> 8) & 0xFF00FF;
        uint32_t g  = (((c & 0x00FF00) * a + (d & 0x00FF00) * ia) >> 8) & 0x00FF00;
        d = 0xFF000000 | rb | g;
    }
    // Additive (light) blend — c's alpha scales the contribution
    inline void add(int x, int y, uint32_t c) {
        if (!in(x, y)) return;
        int a = int(c >> 24);
        uint32_t &d = p[y * w + x];
        int r = R(d) + R(c) * a / 255, g = G(d) + G(c) * a / 255, b = B(d) + B(c) * a / 255;
        d = 0xFF000000 | (uint32_t(std::min(r,255)) << 16) | (uint32_t(std::min(g,255)) << 8) | uint32_t(std::min(b,255));
    }

    void fillRect(int x, int y, int rw, int rh, uint32_t c) {
        int x0 = std::max(x, 0), x1 = std::min(x + rw, w), y0 = std::max(y, 0), y1 = std::min(y + rh, h);
        for (int yy = y0; yy < y1; yy++) { uint32_t *row = p + yy * w; for (int xx = x0; xx < x1; xx++) row[xx] = c; }
    }
    void blendRect(int x, int y, int rw, int rh, uint32_t c) {
        if ((c >> 24) == 255) { fillRect(x, y, rw, rh, c); return; }
        int x0 = std::max(x, 0), x1 = std::min(x + rw, w), y0 = std::max(y, 0), y1 = std::min(y + rh, h);
        for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) blend(xx, yy, c);
    }
    // Vertical gradient rectangle (top colour → bottom colour), alpha-blended
    void vGrad(int x, int y, int rw, int rh, uint32_t top, uint32_t bot) {
        for (int yy = 0; yy < rh; yy++) {
            uint32_t c = lerpCol(top, bot, rh > 1 ? float(yy) / float(rh - 1) : 0.f);
            for (int xx = 0; xx < rw; xx++) blend(x + xx, y + yy, c);
        }
    }
    void hGrad(int x, int y, int rw, int rh, uint32_t left, uint32_t right) {
        for (int xx = 0; xx < rw; xx++) {
            uint32_t c = lerpCol(left, right, rw > 1 ? float(xx) / float(rw - 1) : 0.f);
            for (int yy = 0; yy < rh; yy++) blend(x + xx, y + yy, c);
        }
    }
    void rectOutline(int x, int y, int rw, int rh, uint32_t c, int t = 1) {
        blendRect(x, y, rw, t, c); blendRect(x, y + rh - t, rw, t, c);
        blendRect(x, y + t, t, rh - 2 * t, c); blendRect(x + rw - t, y + t, t, rh - 2 * t, c);
    }
    // Corner brackets — the "tactical" frame look
    void brackets(int x, int y, int rw, int rh, int len, uint32_t c, int t = 2) {
        blendRect(x, y, len, t, c);            blendRect(x, y, t, len, c);
        blendRect(x + rw - len, y, len, t, c); blendRect(x + rw - t, y, t, len, c);
        blendRect(x, y + rh - t, len, t, c);   blendRect(x, y + rh - len, t, len, c);
        blendRect(x + rw - len, y + rh - t, len, t, c); blendRect(x + rw - t, y + rh - len, t, len, c);
    }

    void fillCircle(int cx, int cy, int r, uint32_t c) {
        if (r <= 0) { blend(cx, cy, c); return; }
        for (int dy = -r; dy <= r; dy++) {
            int span = int(std::sqrt(float(r * r - dy * dy)) + 0.5f);
            for (int dx = -span; dx <= span; dx++) blend(cx + dx, cy + dy, c);
        }
    }
    // Soft disc: alpha-blended with a smooth radial falloff (smoke, dust, mist)
    void softCircle(int cx, int cy, int r, uint32_t c) {
        if (r <= 0) { blend(cx, cy, c); return; }
        int a0 = int(c >> 24);
        float inv = 1.f / float(r * r);
        for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
            float d = float(dx * dx + dy * dy) * inv;
            if (d >= 1.f) continue;
            float f = 1.f - d;
            blend(cx + dx, cy + dy, withAlpha(c, int(a0 * f * f)));
        }
    }
    // Radial glow: alpha falls off quadratically from centre (additive light)
    void glow(int cx, int cy, int r, uint32_t c, float strength = 1.f) {
        if (r <= 0) return;
        int x0 = std::max(cx - r, 0), x1 = std::min(cx + r, w - 1), y0 = std::max(cy - r, 0), y1 = std::min(cy + r, h - 1);
        float inv = 1.f / float(r * r);
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float d = float((x - cx) * (x - cx) + (y - cy) * (y - cy)) * inv;
            if (d >= 1.f) continue;
            float f = (1.f - d); f *= f * strength;
            add(x, y, withAlpha(c, int(255 * clampf(f, 0.f, 1.f))));
        }
    }
    // Ring between radii r0..r1 over angle range [a0,a1] (radians, 0 = +x, CW in screen space)
    void arc(int cx, int cy, float r0, float r1, float a0, float a1, uint32_t c) {
        int R1 = int(r1) + 1;
        for (int dy = -R1; dy <= R1; dy++) for (int dx = -R1; dx <= R1; dx++) {
            float d = std::sqrt(float(dx * dx + dy * dy));
            if (d < r0 || d > r1) continue;
            float a = std::atan2(float(dy), float(dx));
            float t = a - a0; while (t < 0) t += 6.2831853f; while (t >= 6.2831853f) t -= 6.2831853f;
            float span = a1 - a0; while (span < 0) span += 6.2831853f;
            if (t <= span) blend(cx + dx, cy + dy, c);
        }
    }
    void ring(int cx, int cy, float r0, float r1, uint32_t c) {
        int R1 = int(r1) + 1;
        float a2 = r0 * r0, b2 = r1 * r1;
        for (int dy = -R1; dy <= R1; dy++) for (int dx = -R1; dx <= R1; dx++) {
            float d = float(dx * dx + dy * dy);
            if (d >= a2 && d <= b2) blend(cx + dx, cy + dy, c);
        }
    }

    // Bresenham line, alpha-blended
    void line(int x0, int y0, int x1, int y1, uint32_t c) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
        int guard = 0;
        for (;;) {
            blend(x0, y0, c);
            if ((x0 == x1 && y0 == y1) || ++guard > 4096) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    void thickLine(int x0, int y0, int x1, int y1, uint32_t c, int t) {
        if (t <= 1) { line(x0, y0, x1, y1, c); return; }
        float dx = float(x1 - x0), dy = float(y1 - y0), L = std::sqrt(dx * dx + dy * dy);
        if (L < 0.5f) { fillCircle(x0, y0, t / 2, c); return; }
        float nx = -dy / L * t * 0.5f, ny = dx / L * t * 0.5f;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }

    // ── Triangle rasterizer (edge functions, Gouraud colour interpolation) ──
    void tri(float x0, float y0, float x1, float y1, float x2, float y2,
             uint32_t c0, uint32_t c1, uint32_t c2, bool additive = false) {
        float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (std::fabs(area) < 1e-3f) return;
        if (area < 0) { std::swap(x1, x2); std::swap(y1, y2); std::swap(c1, c2); area = -area; }
        int minX = std::max(0, int(std::floor(std::min({x0, x1, x2}))));
        int maxX = std::min(w - 1, int(std::ceil(std::max({x0, x1, x2}))));
        int minY = std::max(0, int(std::floor(std::min({y0, y1, y2}))));
        int maxY = std::min(h - 1, int(std::ceil(std::max({y0, y1, y2}))));
        if (minX > maxX || minY > maxY) return;
        bool flat = (c0 == c1 && c1 == c2);
        float inv = 1.f / area;
        for (int y = minY; y <= maxY; y++) {
            float py = y + 0.5f;
            for (int x = minX; x <= maxX; x++) {
                float px = x + 0.5f;
                float w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
                float w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
                float w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                uint32_t c;
                if (flat) c = c0;
                else {
                    float b0 = w0 * inv, b1 = w1 * inv, b2 = w2 * inv;
                    c = argb(int(A(c0) * b0 + A(c1) * b1 + A(c2) * b2), int(R(c0) * b0 + R(c1) * b1 + R(c2) * b2),
                             int(G(c0) * b0 + G(c1) * b1 + G(c2) * b2), int(B(c0) * b0 + B(c1) * b1 + B(c2) * b2));
                }
                if (additive) add(x, y, c); else blend(x, y, c);
            }
        }
    }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) { tri(x0, y0, x1, y1, x2, y2, c, c, c); }
    void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c) {
        tri(x0, y0, x1, y1, x2, y2, c); tri(x0, y0, x2, y2, x3, y3, c);
    }
    // Convex polygon as a triangle fan with per-vertex colours
    void poly(const float *xy, const uint32_t *cols, int n, bool additive = false) {
        for (int i = 1; i + 1 < n; i++)
            tri(xy[0], xy[1], xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], cols[0], cols[i], cols[i + 1], additive);
    }
};

// ── Procedural noise (value noise + fBm), used to bake textures ─────────────
inline float hash2(int x, int y, int seed = 0) {
    uint32_t n = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(seed) * 2246822519u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= n >> 16;
    return float(n & 0xFFFFFF) / float(0xFFFFFF);
}
// Tileable value noise (period in cells)
inline float vnoise(float x, float y, int period, int seed = 0) {
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    auto wrap = [period](int i) { int m = i % period; return m < 0 ? m + period : m; };
    float a = hash2(wrap(ix), wrap(iy), seed), b = hash2(wrap(ix + 1), wrap(iy), seed);
    float c = hash2(wrap(ix), wrap(iy + 1), seed), d = hash2(wrap(ix + 1), wrap(iy + 1), seed);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}
// fBm over a tile of size `size` texels, base frequency `freq` cells per tile
inline float fbm(float x, float y, int size, int freq, int octaves, int seed = 0) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < octaves; o++) {
        sum += vnoise(x * freq / size, y * freq / size, freq, seed + o * 31) * amp;
        norm += amp; amp *= 0.5f; freq *= 2;
    }
    return sum / norm;
}

} // namespace rs
