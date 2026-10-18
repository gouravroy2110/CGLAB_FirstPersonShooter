// =============================================================================
//  soldier3d.h  -  One soldier skeleton shared by the renderer and the hit test
//
//  A soldier is described by a pose (Soldier3D). soldierJoints() places its
//  joints in the soldier's own frame (a = forward, b = right, c = up): legs and
//  arms are solved with two-bone IK, crouching lowers the hips, leaning tilts
//  the upper body. SoldierXf turns that frame into world space, including the
//  death fall, which tips the whole body about its feet in the direction the
//  killing shot pushed it. render_actors.cpp draws rounded parts on these
//  joints; raySoldier() casts a ray against capsules on the same joints, so a
//  hit lands exactly on what you see.
// =============================================================================
#pragma once
#include <cmath>
#include <algorithm>
#include "raster3d.h"
#include "weapons.h"
#include "assets.h"

// Build of every soldier: widths of torso, limbs and kit (and the hitboxes / shadows that
// follow them). 1.0 is anatomically lean; a little broader reads better at game size.
constexpr float SOLDIER_BULK = 1.25f;
constexpr float SOLDIER_HEAD = 1.4f;        // head and headgear size, in step with the broad build
constexpr float SOLDIER_HEAD_LIFT = 0.016f; // the bigger head sits a little higher on the neck

enum SoldierGearKind { SG_NONE, SG_SHIELD, SG_GRENADIER, SG_MEDIC, SG_HEAVY, SG_STALKER, SG_SNIPER };

// every soldier wears the same head size (heavies included)
inline float soldierHeadK(int gear) { (void)gear; return SOLDIER_HEAD; }

struct Soldier3D {
    float x = 0, y = 0, z = 0;  // feet (world)
    float yaw = 0;              // facing angle
    float aimPitch = 0.f;       // radians, + = aiming up
    float walk = 0.f;           // walk-cycle phase (radians)
    float move = 0.f;           // 0..1 how fast it is moving
    float fall = 0.f;           // 0 standing .. 1 lying on the ground
    float fallRel = 3.14159f;   // direction of the fall relative to facing (pi = backwards)
    float crouch = 0.f;         // 0 standing .. 1 crouched
    float lean = 0.f;           // -1 left .. 1 right
    float scale = 1.f;
    int   weapon = WPN_RIFLE;
    SoldierLook look{};
    float recoil = 0.f;         // 0..1 kick after a shot
    bool  sprint = false;       // gun held low while running
    uint32_t band = 0xFF3C8CFF; // team armband
    int   gear = SG_NONE;
    bool  helmet = true;        // false: knocked off by a headshot
    int   flashPart = 0;        // HitPart tinted red for a moment after a hit
    float flashK = 0.f;
    float alpha = 1.f;          // < 1: dithered (cloak, corpse fade)
    float seed = 0.f;           // per-soldier variation
    float t = 0.f;              // clock for idle motion (breathing, weight shift)
    float footZ[2] = {0.f, 0.f};// ground under each foot relative to z (planted on slopes / steps)
};

// Joint positions in the local frame (before scale / fall)
struct SoldierJoints {
    float lean, drop, breathe, aimP, shZ;
    float hip[2][3], knee[2][3], ankle[2][3];
    float sh[2][3], elbow[2][3], hand[2][3];
    float head[3];
};

// Two-bone IK in 3D (plain floats): the middle joint where both bones reach,
// bent towards `pole`
inline void ik2(const float *a, const float *c, float L1, float L2, const float *pole, float *mid)
{
    float d[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-5f;
    for (float &v : d) v /= len;
    float reach = std::min(len, L1 + L2 - 0.002f);
    float along = (L1 * L1 - L2 * L2 + reach * reach) / (2.f * reach);
    float h = std::sqrt(std::max(0.f, L1 * L1 - along * along));
    float pd = pole[0] * d[0] + pole[1] * d[1] + pole[2] * d[2];
    float p[3] = { pole[0] - d[0] * pd, pole[1] - d[1] * pd, pole[2] - d[2] * pd };
    float pl = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) + 1e-6f;
    for (int i = 0; i < 3; i++) mid[i] = a[i] + d[i] * along + p[i] / pl * h;
}

inline void soldierJoints(const Soldier3D &p, SoldierJoints &J)
{
    const float PI = 3.14159265f;
    // a dying soldier's knees buckle before the body tips over
    float buckle = p.fall > 0.f ? std::sin(std::min(1.f, p.fall * 1.6f) * PI) * 0.7f : 0.f;
    float crouch = std::max(p.crouch, buckle);
    J.drop = 0.17f * crouch;
    J.lean = 0.03f + 0.05f * p.move * (p.sprint ? 1.6f : 1.f) + 0.07f * crouch - 0.025f * p.recoil;   // a shot rocks the chest back
    float idle = (1.f - p.move) * (p.fall > 0.f ? 0.f : 1.f);
    J.breathe = 0.005f * std::sin(p.t * 1.7f + p.seed * 6.f) * idle;
    float sway = 0.012f * std::sin(p.t * 0.55f + p.seed * 9.f) * idle;   // shifting weight from foot to foot
    float lowFoot = std::min(0.f, std::min(p.footZ[0], p.footZ[1]));
    J.drop += -lowFoot;                                                   // the hips sink to reach the lower foot
    J.aimP = (p.sprint && p.move > 0.5f) ? -0.65f : p.aimPitch;
    if (p.fall > 0.f) J.aimP = -0.9f * std::min(1.f, p.fall * 2.f);   // the gun drops
    // legs: hip over a foot target on the ground, knee solved with IK (bends forward)
    for (int s = 0; s < 2; s++) {
        int side = s ? 1 : -1;
        float ph = p.walk + (side > 0 ? PI : 0.f);
        float hb = 0.066f * side;                                  // broad stance
        float stride = 0.17f * p.move * (p.sprint ? 1.3f : 1.f) * (1.f - 0.5f * crouch);
        float lift = 0.07f * p.move * std::max(0.f, std::cos(ph));
        J.hip[s][0] = 0.f; J.hip[s][1] = hb + sway; J.hip[s][2] = 0.47f - J.drop + (side > 0 ? 1.f : -1.f) * sway * 0.4f;
        J.ankle[s][0] = std::sin(ph) * stride - 0.03f * crouch + (side > 0 ? 0.05f : -0.05f) * crouch;
        J.ankle[s][1] = hb * (1.f + 0.5f * crouch);
        J.ankle[s][2] = 0.045f + lift + p.footZ[s];
        float pole[3] = { 1.f, 0.f, 0.f };
        ik2(J.hip[s], J.ankle[s], 0.225f, 0.215f, pole, J.knee[s]);
    }
    // upper body
    J.shZ = 0.695f - J.drop;
    float lb = 0.06f * p.lean + sway * 0.6f;                     // leaning / weight shift moves the shoulders sideways
    J.head[0] = J.lean + 0.016f; J.head[1] = lb * 1.6f; J.head[2] = 0.805f - J.drop + J.breathe;
    float pc = std::cos(J.aimP), ps = std::sin(J.aimP);
    auto aimRot = [&](float a, float b, float c, float *o) {    // rotate about the shoulder line
        float dc = c - 0.695f;
        o[0] = J.lean + a * pc - dc * ps; o[1] = b + lb; o[2] = J.shZ + a * ps + dc * pc;
    };
    bool oneHand = isMelee(p.weapon);
    bool pistol = p.weapon >= 0 && WEAPONS[p.weapon].arch == VA_PISTOL;
    float kick = -0.03f * p.recoil;
    float rA = pistol ? 0.27f : 0.13f, rB = pistol ? 0.03f : 0.06f, rC = pistol ? 0.71f : 0.63f;
    float lA = pistol ? 0.27f : 0.29f, lB = pistol ? 0.0f : -0.005f, lC = pistol ? 0.70f : 0.655f;
    if (oneHand) { rA = 0.2f; rB = 0.1f; rC = 0.56f; }
    if (p.gear == SG_SHIELD) { lA = 0.16f; lB = -0.07f; lC = 0.6f; }  // left arm holds the shield
    for (int s = 0; s < 2; s++) {
        int side = s ? 1 : -1;
        J.sh[s][0] = J.lean; J.sh[s][1] = 0.128f * side + lb; J.sh[s][2] = J.shZ;   // broad shoulders
    }
    aimRot(rA + kick, rB, rC, J.hand[1]);
    if (oneHand && p.gear != SG_SHIELD) {
        J.hand[0][0] = 0.05f + 0.06f * std::sin(p.walk) * p.move; J.hand[0][1] = -0.13f; J.hand[0][2] = 0.5f - J.drop;
    } else aimRot(lA + kick, lB, lC, J.hand[0]);
    if (p.fall > 0.f) {                                         // arms flung out as the body goes down
        float k = std::min(1.f, p.fall * 1.5f);
        for (int s = 0; s < 2; s++) {
            int side = s ? 1 : -1;
            float flung[3] = { J.sh[s][0] - 0.03f, J.sh[s][1] + side * 0.22f, J.sh[s][2] + 0.03f };   // flat on the ground
            for (int i = 0; i < 3; i++) J.hand[s][i] += (flung[i] - J.hand[s][i]) * k;
        }
    }
    for (int s = 0; s < 2; s++) {
        float pole[3] = { 0.f, (s ? 0.04f : -0.05f) * 6.f, -1.f };
        ik2(J.sh[s], J.hand[s], 0.165f, 0.165f, pole, J.elbow[s]);
    }
}

// Local frame -> world: scale, an optional twist, the fall about the feet, yaw
struct SoldierXf {
    float x, y, z, fx, fy, sx, sy, s, th, lift;
    float kx, ky;               // fall axis (local a-b plane)
    float tw;                   // twist about the vertical while falling
    void localRot(float &a, float &b, float &c) const {
        if (th != 0.f) {
            // twist first (the body turns a little as it falls), then Rodrigues about (kx, ky, 0)
            float ca = std::cos(tw), sa = std::sin(tw);
            float a1 = a * ca - b * sa, b1 = a * sa + b * ca;
            a = a1; b = b1;
            float cs = std::cos(th), sn = std::sin(th);
            float kd = kx * a + ky * b;                                   // k . v
            float cxv = ky * c, cyv = -kx * c, czv = kx * b - ky * a;     // k x v
            float na = a * cs + cxv * sn + kx * kd * (1 - cs);
            float nb = b * cs + cyv * sn + ky * kd * (1 - cs);
            float nc = c * cs + czv * sn;
            a = na; b = nb; c = nc;
        }
    }
    void world(float a, float b, float c, float &wx, float &wy, float &wz) const {
        a *= s; b *= s; c *= s;
        localRot(a, b, c);
        wx = x + fx * a + sx * b; wy = y + fy * a + sy * b; wz = z + c + lift;
    }
    void dir(float a, float b, float c, float &wx, float &wy, float &wz) const {
        localRot(a, b, c);
        wx = fx * a + sx * b; wy = fy * a + sy * b; wz = c;
    }
};

inline SoldierXf soldierXf(const Soldier3D &p)
{
    SoldierXf X;
    X.x = p.x; X.y = p.y; X.z = p.z;
    X.fx = std::cos(p.yaw); X.fy = std::sin(p.yaw);
    X.sx = -X.fy; X.sy = X.fx;
    X.s = p.scale;
    // ease out with a small bounce when the body lands
    float f = std::min(1.f, p.fall);
    float e = f < 0.85f ? 1.f - (1.f - f / 0.85f) * (1.f - f / 0.85f) : 1.f + 0.05f * std::sin((f - 0.85f) / 0.15f * 3.14159f);
    X.th = p.fall > 0.f ? e * 1.5707963f : 0.f;
    X.kx = -std::sin(p.fallRel); X.ky = std::cos(p.fallRel);
    X.tw = p.fall > 0.f ? (p.seed - 0.5f) * 0.9f * std::min(1.f, f) : 0.f;
    X.lift = 0.05f * std::min(1.f, p.fall) * p.scale;
    return X;
}

// Ray (o + d*t) against the soldier's capsules. Returns the HitPart (0 = miss)
// and the ray parameter of the hit. d need not be normalised.
inline int raySoldier(const Soldier3D &p, float ox, float oy, float oz, float dx, float dy, float dz, float &tHit)
{
    SoldierJoints J; soldierJoints(p, J);
    SoldierXf X = soldierXf(p);
    float dd = dx * dx + dy * dy + dz * dz;
    if (dd < 1e-12f) return 0;
    int best = 0; float bestT = 1e9f;
    // closest approach between the ray and segment a-b; hit when within radius r
    auto capsule = [&](const float *A, const float *B, float r, int part) {
        float ax, ay, az, bx, by, bz;
        X.world(A[0], A[1], A[2], ax, ay, az);
        X.world(B[0], B[1], B[2], bx, by, bz);
        r *= p.scale;
        float ux = bx - ax, uy = by - ay, uz = bz - az;
        float wx = ox - ax, wy = oy - ay, wz = oz - az;
        float a = dd, b = dx * ux + dy * uy + dz * uz, c = ux * ux + uy * uy + uz * uz;
        float d = dx * wx + dy * wy + dz * wz, e = ux * wx + uy * wy + uz * wz;
        float den = a * c - b * b, t, s;
        if (c < 1e-9f) { s = 0.f; t = -d / a; }
        else if (den < 1e-9f) { s = 0.f; t = -d / a; }
        else { s = std::clamp((a * e - b * d) / den, 0.f, 1.f); t = (b * s - d) / a; }
        // re-solve t for the clamped s
        float px = ax + ux * s, py = ay + uy * s, pz = az + uz * s;
        t = ((px - ox) * dx + (py - oy) * dy + (pz - oz) * dz) / a;
        if (t <= 0.f) return;
        float qx = ox + dx * t - px, qy = oy + dy * t - py, qz = oz + dz * t - pz;
        float dist2 = qx * qx + qy * qy + qz * qz;
        if (dist2 > r * r) return;
        float back = std::sqrt((r * r - dist2) / a);              // step back to the surface
        float th = std::max(0.f, t - back);
        if (th < bestT) { bestT = th; best = part; }
    };
    const float pelvis[3] = { 0.f, 0.f, 0.44f - J.drop }, chest[3] = { J.lean, J.head[1] * 0.6f, 0.71f - J.drop };
    // hitboxes are a little more generous than the drawn surface, as in most shooters
    const float K = SOLDIER_BULK;
    capsule(pelvis, chest, 0.115f * K, PART_BODY);
    const float HK = soldierHeadK(p.gear);
    float hc[3] = { J.head[0], J.head[1], J.head[2] + SOLDIER_HEAD_LIFT * HK / SOLDIER_HEAD + 0.005f * HK };
    capsule(hc, hc, 0.072f * HK, PART_HEAD);
    for (int s = 0; s < 2; s++) {
        capsule(J.hip[s], J.knee[s], 0.058f * K, PART_LIMB);
        capsule(J.knee[s], J.ankle[s], 0.045f * K, PART_LIMB);
        capsule(J.sh[s], J.elbow[s], 0.046f * K, PART_LIMB);
        capsule(J.elbow[s], J.hand[s], 0.04f * K, PART_LIMB);
    }
    if (p.gear == SG_SHIELD && p.fall <= 0.f) {
        // the riot shield: a thick slab in front of the left side, knees to eyes
        const float s0[3] = { 0.2f, -0.06f, 0.22f - J.drop }, s1[3] = { 0.2f, -0.06f, 0.8f - J.drop };
        capsule(s0, s1, 0.13f, PART_SHIELD);                     // nearest surface wins, like every part
    }
    tHit = bestT;
    return best;
}

// Feet on uneven ground: measure the terrain under each ankle and store it in
// footZ, so the IK plants the foot there and the hips sink to the lower one
template <class GroundFn>
inline void plantFeet(Soldier3D &p, GroundFn groundAt)
{
    p.footZ[0] = p.footZ[1] = 0.f;
    if (p.fall > 0.f) return;
    SoldierJoints J; soldierJoints(p, J);
    SoldierXf X = soldierXf(p);
    for (int s = 0; s < 2; s++) {
        float wx, wy, wz;
        X.world(J.ankle[s][0], J.ankle[s][1], 0.f, wx, wy, wz);
        p.footZ[s] = std::max(-0.25f, std::min(0.25f, groundAt(wx, wy) - p.z));
    }
}
