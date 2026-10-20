// =============================================================================
//  audio.cpp  -  Sound synthesis (start-up) and the real-time stereo mixer
// =============================================================================
#include "audio.h"
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {
const float SR = float(SoundEngine::RATE);
const float TWO_PI = 6.2831853f;

using Buf = std::vector<float>;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float next() { s = s * 1664525u + 1013904223u; return float((s >> 8) & 0xFFFF) / 32768.f - 1.f; }
};

Buf make(float sec) { return Buf(size_t(sec * SR), 0.f); }
inline float tsec(size_t i) { return float(i) / SR; }

// One-pole low-pass (in place)
void lowpass(Buf &b, float fc) {
    float a = 1.f - std::exp(-TWO_PI * fc / SR), y = 0;
    for (float &x : b) { y += a * (x - y); x = y; }
}
void highpass(Buf &b, float fc) {
    float a = 1.f - std::exp(-TWO_PI * fc / SR), y = 0;
    for (float &x : b) { y += a * (x - y); x = x - y; }
}
// RBJ band-pass biquad (constant 0 dB peak gain)
void bandpass(Buf &b, float fc, float q) {
    float w0 = TWO_PI * fc / SR, al = std::sin(w0) / (2 * q), c = std::cos(w0);
    float b0 = al, b2 = -al, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (float &x : b) {
        float y = (b0 * x + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = x; y2 = y1; y1 = y; x = y;
    }
}
Buf noise(float sec, uint32_t seed) { Buf b = make(sec); Rng r(seed); for (float &x : b) x = r.next(); return b; }
// Brown-ish noise for rumbles and wind
Buf brown(float sec, uint32_t seed) { Buf b = make(sec); Rng r(seed); float y = 0; for (float &x : b) { y = (y + r.next() * 0.04f) * 0.996f; x = y * 8.f; } return b; }

void envExp(Buf &b, float tau, float start = 0.f) {
    for (size_t i = 0; i < b.size(); i++) { float t = tsec(i) - start; b[i] *= t < 0 ? 0.f : std::exp(-t / tau); }
}
void envAR(Buf &b, float attack, float release) {
    float T = tsec(b.size());
    for (size_t i = 0; i < b.size(); i++) {
        float t = tsec(i);
        float a = attack > 0 ? std::min(1.f, t / attack) : 1.f;
        float r = std::min(1.f, (T - t) / release);
        b[i] *= a * std::max(0.f, r);
    }
}
// Add `src * gain` into dst starting at `at` seconds
void mix(Buf &dst, const Buf &src, float gain = 1.f, float at = 0.f) {
    size_t o = size_t(at * SR);
    for (size_t i = 0; i < src.size() && o + i < dst.size(); i++) dst[o + i] += src[i] * gain;
}
// Sine with exponential frequency sweep f0 -> f1 and exponential decay
Buf sweep(float sec, float f0, float f1, float tau) {
    Buf b = make(sec); float ph = 0;
    for (size_t i = 0; i < b.size(); i++) {
        float t = tsec(i), k = t / sec;
        float f = f0 * std::pow(f1 / f0, k);
        ph += TWO_PI * f / SR;
        b[i] = std::sin(ph) * std::exp(-t / tau);
    }
    return b;
}
Buf tone(float sec, float f, float tau, float attack = 0.002f) {
    Buf b = make(sec); float ph = 0;
    for (size_t i = 0; i < b.size(); i++) {
        float t = tsec(i); ph += TWO_PI * f / SR;
        b[i] = std::sin(ph) * std::exp(-t / tau) * std::min(1.f, t / attack);
    }
    return b;
}
// Metallic click: a few inharmonic partials with fast decays + a transient
Buf clink(std::initializer_list<float> freqs, float tau, uint32_t seed) {
    Buf b = make(tau * 6.f + 0.02f);
    float k = 1.f;
    for (float f : freqs) { mix(b, tone(tsec(b.size()), f, tau * k, 0.0005f), 0.5f / k); k *= 1.3f; }
    Buf n = noise(0.01f, seed); highpass(n, 2000); envExp(n, 0.002f); mix(b, n, 0.6f);
    return b;
}
// Sawtooth voice through two formant band-passes (grunts, shouts)
Buf voice(float sec, float f0, float f1, float fA, float fB, float attack, uint32_t seed) {
    Buf s = make(sec); float ph = 0; Rng r(seed);
    for (size_t i = 0; i < s.size(); i++) {
        float t = tsec(i), f = f0 + (f1 - f0) * t / sec + r.next() * 4.f;
        ph += f / SR; ph -= std::floor(ph);
        s[i] = ph * 2.f - 1.f;
    }
    Buf a = s, c = s;
    bandpass(a, fA, 4.f); bandpass(c, fB, 5.f);
    Buf out = make(sec);
    for (size_t i = 0; i < out.size(); i++) out[i] = a[i] + c[i] * 0.7f;
    envAR(out, attack, sec * 0.6f);
    return out;
}
// Small room: two feedback combs blended in
void room(Buf &b, float wet, float d1 = 0.031f, float d2 = 0.047f, float fb = 0.45f) {
    size_t n1 = size_t(d1 * SR), n2 = size_t(d2 * SR);
    Buf out = b;
    for (size_t i = 0; i < b.size(); i++) {
        float e = 0;
        if (i >= n1) e += out[i - n1] * fb;
        if (i >= n2) e += out[i - n2] * fb * 0.8f;
        out[i] = b[i] + e * 0.5f;
    }
    for (size_t i = 0; i < b.size(); i++) b[i] = b[i] * (1 - wet) + out[i] * wet;
}
void normalize(Buf &b, float peak) {
    float m = 1e-6f; for (float x : b) m = std::max(m, std::fabs(x));
    for (float &x : b) x *= peak / m;
}
// Gun report: crack + body + low thump + tail
Buf gunshot(float len, float crackTau, float bodyFc, float bodyTau, float thumpF0, float thumpF1, float thumpTau,
            float tailFc, float tailTau, float tailGain, uint32_t seed) {
    Buf out = make(len);
    Buf crack = noise(len, seed); highpass(crack, 900); envExp(crack, crackTau); mix(out, crack, 0.9f);
    Buf body = noise(len, seed + 1); lowpass(body, bodyFc); envExp(body, bodyTau); mix(out, body, 1.4f);
    mix(out, sweep(len, thumpF0, thumpF1, thumpTau), 0.9f);
    Buf tail = noise(len, seed + 2); lowpass(tail, tailFc); envExp(tail, tailTau, 0.01f); mix(out, tail, tailGain);
    room(out, 0.35f);
    return out;
}
// Seamless loop: cross-fade the end into the start
void makeLoop(Buf &b, float xfade) {
    size_t n = size_t(xfade * SR);
    if (b.size() <= 2 * n) return;
    Buf out(b.begin(), b.end() - long(n));
    for (size_t i = 0; i < n; i++) {
        float k = float(i) / n;
        out[i] = b[b.size() - n + i] * (1 - k) + b[i] * k;
    }
    b.swap(out);
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Sound bank
// ─────────────────────────────────────────────────────────────────────────────
void SoundEngine::synthesize()
{
    Buf b;
    // --- Player weapons -------------------------------------------------------
    b = gunshot(0.45f, 0.010f, 2600, 0.045f, 190, 60, 0.05f, 1300, 0.14f, 0.35f, 11); normalize(b, 0.85f); m_bank[SND_PISTOL] = b;
    b = gunshot(0.95f, 0.018f, 1700, 0.11f, 115, 38, 0.12f, 800, 0.32f, 0.55f, 21); normalize(b, 1.0f); m_bank[SND_SHOTGUN] = b;
    b = gunshot(0.50f, 0.008f, 3200, 0.035f, 150, 52, 0.045f, 1500, 0.16f, 0.32f, 31); normalize(b, 0.8f); m_bank[SND_RIFLE] = b;

    b = make(0.26f);
    { Buf n = noise(0.26f, 41); Buf hi = n; bandpass(n, 900, 1.2f); bandpass(hi, 2600, 2.f);
      for (size_t i = 0; i < b.size(); i++) { float t = tsec(i) / 0.26f, e = std::sin(3.14159f * t); b[i] = (n[i] * (1 - t) + hi[i] * t) * e * e; } }
    normalize(b, 0.55f); m_bank[SND_KNIFE_SWING] = b;

    b = make(0.25f);
    { Buf n = noise(0.25f, 43); lowpass(n, 900); envExp(n, 0.045f); mix(b, n, 1.2f);
      mix(b, sweep(0.25f, 160, 75, 0.05f), 0.9f);
      Buf s = noise(0.05f, 44); highpass(s, 2500); envExp(s, 0.012f); mix(b, s, 0.35f); }
    normalize(b, 0.8f); m_bank[SND_KNIFE_HIT] = b;

    // --- Handling -------------------------------------------------------------
    b = clink({3100, 1850}, 0.006f, 51); normalize(b, 0.5f); m_bank[SND_DRY_FIRE] = b;
    b = make(0.16f); mix(b, clink({1700, 2650, 4100}, 0.018f, 52), 1.f);
    { Buf s = noise(0.08f, 53); bandpass(s, 2000, 1.5f); envExp(s, 0.03f); mix(b, s, 0.5f, 0.01f); }
    normalize(b, 0.6f); m_bank[SND_MAG_OUT] = b;
    b = make(0.22f); mix(b, clink({920, 1530, 2900}, 0.028f, 54), 1.f); mix(b, tone(0.1f, 220, 0.02f), 0.5f);
    mix(b, clink({1400, 2300}, 0.015f, 55), 0.6f, 0.07f);
    normalize(b, 0.7f); m_bank[SND_MAG_IN] = b;
    b = make(0.28f);
    { Buf s = noise(0.1f, 56); bandpass(s, 1500, 1.2f); envAR(s, 0.06f, 0.03f); mix(b, s, 0.6f, 0.02f); }
    mix(b, clink({1250, 2100, 3300}, 0.02f, 57), 0.9f); mix(b, clink({980, 1900, 2800}, 0.025f, 58), 1.f, 0.12f);
    normalize(b, 0.7f); m_bank[SND_BOLT] = b;
    b = make(0.4f);
    for (int k = 0; k < 2; k++) {
        Buf s = noise(0.08f, 59 + k); bandpass(s, 1100, 1.0f); envExp(s, 0.03f); mix(b, s, 0.7f, k * 0.17f);
        mix(b, clink({700, 1320, 2050}, 0.025f, 61 + k), 0.9f, k * 0.17f + 0.01f);
        mix(b, tone(0.08f, 160, 0.02f), 0.4f, k * 0.17f);
    }
    normalize(b, 0.75f); m_bank[SND_PUMP] = b;
    b = make(0.14f); mix(b, clink({1100, 2350}, 0.02f, 63), 1.f);
    { Buf s = noise(0.06f, 64); bandpass(s, 1700, 2.f); envExp(s, 0.02f); mix(b, s, 0.4f); }
    normalize(b, 0.55f); m_bank[SND_SHELL_IN] = b;
    b = make(0.22f);
    { Buf s = noise(0.14f, 65); bandpass(s, 850, 0.9f); envAR(s, 0.04f, 0.08f); mix(b, s, 0.6f); }
    mix(b, clink({1500, 2450}, 0.015f, 66), 0.7f, 0.12f);
    normalize(b, 0.5f); m_bank[SND_SWITCH] = b;
    b = make(0.3f); mix(b, clink({4200, 6100, 8300}, 0.03f, 67), 1.f); mix(b, clink({4400, 6600}, 0.02f, 68), 0.45f, 0.11f);
    mix(b, clink({4300, 6400}, 0.015f, 69), 0.2f, 0.19f);
    normalize(b, 0.3f); m_bank[SND_CASING] = b;
    b = make(0.3f); mix(b, clink({720, 1230}, 0.025f, 70), 1.f); mix(b, clink({760, 1300}, 0.02f, 71), 0.5f, 0.12f);
    normalize(b, 0.35f); m_bank[SND_SHELL_DROP] = b;

    // --- Movement -------------------------------------------------------------
    b = make(0.13f);
    { Buf n = noise(0.13f, 72); lowpass(n, 520); envExp(n, 0.025f); mix(b, n, 1.f);
      mix(b, tone(0.13f, 85, 0.03f), 0.5f);
      Buf c = noise(0.05f, 73); highpass(c, 2500); envExp(c, 0.01f); mix(b, c, 0.18f, 0.005f); }
    normalize(b, 0.4f); m_bank[SND_STEP] = b;
    b = make(0.2f);
    { Buf s = noise(0.18f, 74); bandpass(s, 650, 0.8f); envAR(s, 0.03f, 0.12f); mix(b, s, 1.f); }
    normalize(b, 0.35f); m_bank[SND_JUMP] = b;
    b = make(0.3f); mix(b, sweep(0.3f, 90, 50, 0.07f), 1.f);
    { Buf n = noise(0.3f, 75); lowpass(n, 420); envExp(n, 0.05f); mix(b, n, 1.f); }
    normalize(b, 0.6f); m_bank[SND_LAND] = b;

    // --- Damage & combat feedback -----------------------------------------------
    b = make(0.4f); mix(b, sweep(0.2f, 130, 70, 0.05f), 0.9f);
    { Buf n = noise(0.15f, 76); lowpass(n, 600); envExp(n, 0.03f); mix(b, n, 0.8f); }
    mix(b, voice(0.3f, 140, 100, 700, 1150, 0.015f, 77), 0.9f, 0.02f);
    normalize(b, 0.75f); m_bank[SND_PLAYER_HURT] = b;
    b = make(0.2f);
    { Buf s = sweep(0.2f, 2700, 1250, 1.f); Buf n = noise(0.2f, 78); bandpass(n, 3000, 3.f);
      for (size_t i = 0; i < b.size(); i++) { float t = tsec(i) / 0.2f, e = t < 0.45f ? t / 0.45f : (1 - t) / 0.55f; b[i] = (s[i] * 0.6f + n[i]) * e * e; } }
    normalize(b, 0.45f); m_bank[SND_BULLET_WHIZ] = b;

    b = gunshot(0.6f, 0.01f, 1800, 0.05f, 150, 55, 0.05f, 900, 0.22f, 0.5f, 81); lowpass(b, 3200); normalize(b, 0.8f); m_bank[SND_ENEMY_RIFLE] = b;
    b = gunshot(0.45f, 0.01f, 2200, 0.04f, 200, 70, 0.04f, 1100, 0.15f, 0.35f, 82); lowpass(b, 3800); normalize(b, 0.7f); m_bank[SND_ENEMY_PISTOL] = b;
    b = gunshot(0.9f, 0.02f, 1300, 0.11f, 105, 38, 0.12f, 650, 0.3f, 0.6f, 83); lowpass(b, 2400); normalize(b, 0.9f); m_bank[SND_ENEMY_SHOTGUN] = b;
    b = gunshot(1.4f, 0.006f, 3600, 0.05f, 130, 45, 0.07f, 1200, 0.2f, 0.4f, 84);
    { Buf echo = b; lowpass(echo, 1100); mix(b, echo, 0.35f, 0.21f); mix(b, echo, 0.18f, 0.45f); }
    normalize(b, 0.95f); m_bank[SND_ENEMY_SNIPER] = b;

    b = make(0.16f);
    { Buf n = noise(0.16f, 85); lowpass(n, 750); envExp(n, 0.035f); mix(b, n, 1.f); mix(b, tone(0.16f, 140, 0.045f), 0.6f); }
    normalize(b, 0.6f); m_bank[SND_HIT_FLESH] = b;
    b = make(0.06f); mix(b, tone(0.06f, 1800, 0.014f, 0.0005f), 1.f); mix(b, tone(0.06f, 3600, 0.006f, 0.0005f), 0.5f);
    normalize(b, 0.35f); m_bank[SND_HITMARK] = b;
    b = make(0.4f); mix(b, tone(0.4f, 2400, 0.12f, 0.0005f), 1.f); mix(b, tone(0.4f, 3630, 0.08f, 0.0005f), 0.6f);
    mix(b, tone(0.4f, 5150, 0.05f, 0.0005f), 0.4f); mix(b, clink({6000}, 0.004f, 86), 0.6f);
    normalize(b, 0.55f); m_bank[SND_HEADSHOT] = b;
    b = make(0.3f); mix(b, tone(0.12f, 880, 0.04f), 0.8f); mix(b, tone(0.2f, 1320, 0.07f), 0.9f, 0.06f); mix(b, tone(0.15f, 90, 0.04f), 0.6f);
    normalize(b, 0.45f); m_bank[SND_KILL] = b;
    b = make(0.9f); mix(b, voice(0.45f, 170, 85, 620, 1000, 0.01f, 87), 1.f);
    { Buf t = sweep(0.25f, 70, 45, 0.08f); Buf n = noise(0.25f, 88); lowpass(n, 380); envExp(n, 0.07f); mix(b, t, 0.8f, 0.5f); mix(b, n, 0.9f, 0.5f); }
    normalize(b, 0.7f); m_bank[SND_ENEMY_DEATH] = b;
    b = make(0.35f); mix(b, voice(0.2f, 190, 210, 820, 1250, 0.01f, 89), 1.f);
    { Buf n = noise(0.35f, 90); bandpass(n, 1900, 1.5f); envAR(n, 0.005f, 0.05f); mix(b, n, 0.25f); }
    bandpass(b, 1200, 0.7f); normalize(b, 0.5f); m_bank[SND_ENEMY_ALERT] = b;

    b = make(0.16f);
    { Buf h = noise(0.16f, 91); highpass(h, 1500); envExp(h, 0.012f); mix(b, h, 1.f);
      Buf l = noise(0.16f, 92); lowpass(l, 900); envExp(l, 0.03f); mix(b, l, 0.5f); mix(b, tone(0.05f, 420, 0.01f), 0.4f); }
    normalize(b, 0.55f); m_bank[SND_IMPACT] = b;
    b = m_bank[SND_IMPACT]; b.resize(size_t(0.45f * SR), 0.f);
    { Buf w = make(0.4f); float ph = 0;
      for (size_t i = 0; i < w.size(); i++) { float t = tsec(i); float f = 3300 * std::pow(1400.f / 3300.f, t / 0.4f) * (1 + 0.02f * std::sin(t * 60)); ph += TWO_PI * f / SR; w[i] = std::sin(ph) * std::exp(-t / 0.13f); }
      mix(b, w, 0.35f, 0.01f); }
    normalize(b, 0.55f); m_bank[SND_RICOCHET] = b;

    // --- Pickups, world, UI -------------------------------------------------------
    b = make(0.28f); mix(b, clink({1500, 2400}, 0.02f, 93), 1.f); mix(b, clink({1300, 2700}, 0.02f, 94), 0.8f, 0.05f); mix(b, clink({1700, 2900}, 0.02f, 95), 0.7f, 0.1f);
    normalize(b, 0.5f); m_bank[SND_PICKUP_AMMO] = b;
    b = make(0.5f); mix(b, tone(0.3f, 660, 0.1f, 0.01f), 0.8f); mix(b, tone(0.4f, 990, 0.14f, 0.01f), 0.9f, 0.08f); mix(b, tone(0.4f, 1980, 0.08f, 0.01f), 0.25f, 0.08f);
    normalize(b, 0.5f); m_bank[SND_PICKUP_HEALTH] = b;
    b = make(0.55f); mix(b, m_bank[SND_MAG_IN], 1.f); mix(b, m_bank[SND_BOLT], 0.8f, 0.12f); mix(b, tone(0.3f, 784, 0.1f, 0.01f), 0.4f, 0.2f);
    normalize(b, 0.7f); m_bank[SND_PICKUP_WEAPON] = b;
    b = make(1.1f);
    { const float notes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
      for (int i = 0; i < 4; i++) { mix(b, tone(0.6f, notes[i], 0.25f, 0.005f), 0.5f, i * 0.09f); mix(b, tone(0.6f, notes[i] * 2, 0.12f, 0.005f), 0.15f, i * 0.09f); }
      room(b, 0.4f, 0.07f, 0.11f, 0.5f); }
    normalize(b, 0.6f); m_bank[SND_INTEL] = b;
    b = brown(1.5f, 96);
    { lowpass(b, 320); float wob = 0; Rng r(97);
      for (size_t i = 0; i < b.size(); i++) { float t = tsec(i); if (i % 2000 == 0) wob = 0.6f + 0.4f * std::fabs(r.next()); b[i] *= wob * std::min(1.f, t / 0.15f) * std::min(1.f, (1.4f - t) / 0.3f); }
      Buf rum = tone(1.5f, 46, 10.f, 0.1f); envAR(rum, 0.1f, 0.4f); mix(b, rum, 0.4f);
      Buf thud = sweep(0.3f, 80, 45, 0.08f); mix(b, thud, 1.2f, 1.18f); }
    normalize(b, 0.7f); m_bank[SND_DOOR] = b;
    b = make(2.0f);
    { const float notes[5] = {392.f, 523.25f, 659.25f, 783.99f, 1046.5f};
      for (int i = 0; i < 5; i++) mix(b, tone(1.6f, notes[i], 0.6f, 0.01f), 0.35f, i * 0.11f);
      Buf sh = make(1.6f); float ph = 0;
      for (size_t i = 0; i < sh.size(); i++) { float t = tsec(i); ph += TWO_PI * 2093.f / SR; sh[i] = std::sin(ph) * (0.5f + 0.5f * std::sin(t * 40)) * std::exp(-t / 0.5f); }
      mix(b, sh, 0.12f, 0.5f); room(b, 0.5f, 0.09f, 0.13f, 0.55f); }
    normalize(b, 0.6f); m_bank[SND_SECRET] = b;
    b = noise(0.5f, 98);
    { highpass(b, 2800); Rng r(99); float g = 0;
      for (size_t i = 0; i < b.size(); i++) { if (i % 300 == 0) g = r.next() > 0.3f ? 1.f : 0.25f; b[i] *= g; }
      envAR(b, 0.02f, 0.15f); }
    normalize(b, 0.35f); m_bank[SND_ACID] = b;
    b = make(0.5f);
    { Buf l = sweep(0.15f, 60, 45, 0.06f); Buf n = noise(0.1f, 100); lowpass(n, 150); envExp(n, 0.04f);
      mix(b, l, 1.f); mix(b, n, 0.6f); mix(b, l, 0.75f, 0.22f); mix(b, n, 0.4f, 0.22f); }
    normalize(b, 0.7f); m_bank[SND_HEARTBEAT] = b;
    b = make(2.0f);
    { auto tri = [](float sec, float f, float tau) { Buf o = make(sec); for (size_t i = 0; i < o.size(); i++) { float t = tsec(i), p = std::fmod(t * f, 1.f); o[i] = (4 * std::fabs(p - 0.5f) - 1) * std::exp(-t / tau) * std::min(1.f, t / 0.01f); } return o; };
      mix(b, tri(0.3f, 523.25f, 0.2f), 0.4f); mix(b, tri(0.3f, 659.25f, 0.2f), 0.4f, 0.14f); mix(b, tri(0.3f, 783.99f, 0.2f), 0.4f, 0.28f);
      mix(b, tri(1.5f, 523.25f, 0.7f), 0.3f, 0.45f); mix(b, tri(1.5f, 659.25f, 0.7f), 0.3f, 0.45f); mix(b, tri(1.5f, 783.99f, 0.7f), 0.3f, 0.45f); mix(b, tri(1.5f, 1046.5f, 0.7f), 0.25f, 0.45f);
      room(b, 0.4f, 0.08f, 0.12f, 0.5f); }
    normalize(b, 0.6f); m_bank[SND_LEVEL_CLEAR] = b;
    b = make(0.55f);
    { Buf s = sweep(0.45f, 300, 1300, 0.3f); envAR(s, 0.02f, 0.15f); mix(b, s, 0.7f);
      for (int i = 0; i < 4; i++) mix(b, tone(0.2f, 1500.f + i * 400.f, 0.05f), 0.2f, 0.15f + i * 0.06f); }
    normalize(b, 0.5f); m_bank[SND_UPGRADE] = b;
    b = make(0.7f);
    { Buf w = noise(0.5f, 101); bandpass(w, 500, 0.7f); envAR(w, 0.2f, 0.25f); mix(b, w, 0.6f);
      mix(b, clink({1800}, 0.01f, 102), 0.6f); mix(b, tone(0.25f, 880, 0.08f), 0.5f, 0.35f); mix(b, tone(0.25f, 1175, 0.08f), 0.5f, 0.47f); }
    normalize(b, 0.55f); m_bank[SND_DEPLOY] = b;
    b = make(2.2f);
    { const float n[3] = {392.f, 311.13f, 261.63f};
      for (int i = 0; i < 3; i++) { Buf t = tone(1.0f, n[i], 0.45f, 0.02f); Buf t2 = tone(1.0f, n[i] * 0.5f, 0.5f, 0.02f); mix(b, t, 0.5f, i * 0.42f); mix(b, t2, 0.35f, i * 0.42f); }
      room(b, 0.5f, 0.09f, 0.14f, 0.55f); }
    normalize(b, 0.6f); m_bank[SND_GAME_OVER] = b;
    b = make(0.7f);
    { Buf n = noise(0.7f, 103); bandpass(n, 1800, 1.2f); Rng r(104); float g = 1;
      for (size_t i = 0; i < n.size(); i++) { if (i % 1800 == 0) g = r.next() > -0.2f ? 1.f : 0.2f; n[i] *= g; }
      envAR(n, 0.01f, 0.1f); mix(b, n, 0.6f); mix(b, tone(0.12f, 1250, 0.04f), 0.5f); mix(b, tone(0.12f, 1250, 0.04f), 0.4f, 0.55f); }
    normalize(b, 0.4f); m_bank[SND_RADIO] = b;
    auto blip = [](float f, float sec) { Buf o = make(sec); for (size_t i = 0; i < o.size(); i++) { float t = tsec(i); o[i] = (std::fmod(t * f, 1.f) < 0.5f ? 0.6f : -0.6f) * std::exp(-t / (sec * 0.35f)); } lowpass(o, 4000); return o; };
    b = blip(660, 0.05f); normalize(b, 0.25f); m_bank[SND_UI_MOVE] = b;
    b = make(0.16f); mix(b, blip(880, 0.06f), 1.f); mix(b, blip(1320, 0.08f), 1.f, 0.06f); normalize(b, 0.3f); m_bank[SND_UI_SELECT] = b;
    b = blip(440, 0.08f); normalize(b, 0.25f); m_bank[SND_UI_BACK] = b;
    b = make(0.2f);
    { Buf s = noise(0.12f, 105); bandpass(s, 2500, 2.f); envAR(s, 0.08f, 0.03f); mix(b, s, 0.6f); mix(b, clink({2200, 3500}, 0.01f, 106), 0.6f, 0.11f); }
    normalize(b, 0.4f); m_bank[SND_SCOPE_IN] = b;
    b = clink({2300}, 0.004f, 107); normalize(b, 0.3f); m_bank[SND_ZOOM_TICK] = b;

    // --- Ambience loops (6 s, cross-faded) --------------------------------------------
    {
        Buf w = brown(6.6f, 108); lowpass(w, 420);
        for (size_t i = 0; i < w.size(); i++) { float t = tsec(i); w[i] *= 0.55f + 0.3f * std::sin(t * 0.9f) + 0.15f * std::sin(t * 2.3f + 1.f); }
        makeLoop(w, 0.6f); normalize(w, 0.5f); m_bank[SND_AMB_WIND] = w;
        Buf d = brown(6.6f, 109); lowpass(d, 220);
        Rng r(110);
        for (int k = 0; k < 7; k++) mix(d, tone(0.4f, 1400.f + 900.f * std::fabs(r.next()), 0.06f, 0.001f), 0.25f, 0.3f + std::fabs(r.next()) * 5.6f);
        makeLoop(d, 0.6f); normalize(d, 0.45f); m_bank[SND_AMB_DUNGEON] = d;
        Buf c = make(6.6f);
        { float p1 = 0, p2 = 0; for (size_t i = 0; i < c.size(); i++) { float t = tsec(i); p1 += TWO_PI * 55.f / SR; p2 += TWO_PI * 82.6f / SR; c[i] = (std::sin(p1) + 0.6f * std::sin(p2)) * (0.6f + 0.4f * std::sin(t * 0.7f)); } }
        Buf cw = brown(6.6f, 111); lowpass(cw, 300); mix(c, cw, 0.6f);
        makeLoop(c, 0.6f); normalize(c, 0.4f); m_bank[SND_AMB_CRYPT] = c;
    }

    // --- Arsenal ----------------------------------------------------------------------
    // Revolver: big low crack with a long ring
    b = gunshot(0.9f, 0.012f, 1900, 0.07f, 120, 40, 0.09f, 900, 0.3f, 0.5f, 201);
    mix(b, tone(0.6f, 1650, 0.12f, 0.001f), 0.06f); normalize(b, 0.95f); m_bank[SND_REVOLVER] = b;
    // SMG: short, bright, light
    b = gunshot(0.32f, 0.006f, 3800, 0.022f, 210, 80, 0.03f, 1900, 0.09f, 0.25f, 202); normalize(b, 0.62f); m_bank[SND_SMG] = b;
    // LMG: heavier rifle with more body
    b = gunshot(0.55f, 0.009f, 2500, 0.045f, 120, 45, 0.06f, 1200, 0.18f, 0.4f, 203); normalize(b, 0.82f); m_bank[SND_LMG] = b;
    // Sniper: very loud crack + long rolling echo + bolt
    b = gunshot(1.8f, 0.006f, 3600, 0.06f, 95, 34, 0.1f, 1100, 0.3f, 0.55f, 204);
    { Buf echo = b; lowpass(echo, 900); mix(b, echo, 0.4f, 0.24f); mix(b, echo, 0.22f, 0.55f); mix(b, m_bank[SND_BOLT], 0.5f, 0.7f); }
    normalize(b, 1.0f); m_bank[SND_SNIPER] = b;
    // DMR / battle rifle: crisp heavy single shot
    b = gunshot(0.8f, 0.008f, 3000, 0.05f, 110, 40, 0.07f, 1100, 0.24f, 0.45f, 205); normalize(b, 0.9f); m_bank[SND_DMR] = b;
    // Double barrel: huge boom
    b = gunshot(1.2f, 0.022f, 1300, 0.14f, 90, 30, 0.16f, 600, 0.4f, 0.7f, 206); normalize(b, 1.0f); m_bank[SND_DOUBLE_BARREL] = b;
    // Suppressed: a muffled thump and a little mechanical clack
    b = make(0.22f);
    { Buf n = noise(0.22f, 207); lowpass(n, 700); envExp(n, 0.025f); mix(b, n, 1.f); mix(b, sweep(0.2f, 180, 80, 0.03f), 0.6f);
      mix(b, clink({2600, 3900}, 0.008f, 208), 0.35f, 0.01f); }
    normalize(b, 0.45f); m_bank[SND_SUPPRESSED] = b;
    // Rocket launch: whoosh rising + thump
    b = make(1.2f);
    { Buf n = noise(1.2f, 209); bandpass(n, 700, 0.6f); envAR(n, 0.05f, 0.8f); mix(b, n, 0.9f);
      mix(b, sweep(0.4f, 70, 40, 0.15f), 1.f); mix(b, gunshot(0.5f, 0.02f, 1200, 0.08f, 80, 35, 0.1f, 500, 0.2f, 0.4f, 210), 0.7f); }
    normalize(b, 0.95f); m_bank[SND_ROCKET_FIRE] = b;
    // Grenade launcher: hollow "thoonk"
    b = make(0.45f);
    { mix(b, sweep(0.3f, 240, 70, 0.07f), 1.f); Buf n = noise(0.3f, 211); lowpass(n, 600); envExp(n, 0.05f); mix(b, n, 0.7f); room(b, 0.3f); }
    normalize(b, 0.8f); m_bank[SND_GL_THUMP] = b;
    // Explosion: blast + low rumble + debris
    b = make(2.4f);
    { Buf n = noise(2.4f, 212); lowpass(n, 900); envExp(n, 0.25f); mix(b, n, 1.2f);
      mix(b, sweep(1.6f, 70, 28, 0.5f), 1.2f);
      Buf r2 = brown(2.4f, 213); lowpass(r2, 200); envExp(r2, 0.7f); mix(b, r2, 0.8f);
      Rng r(214); for (int k = 0; k < 10; k++) mix(b, clink({900.f + 900.f * std::fabs(r.next()), 2100.f}, 0.01f, 215 + k), 0.12f, 0.25f + std::fabs(r.next()) * 1.2f);
      room(b, 0.3f, 0.06f, 0.09f, 0.5f); }
    normalize(b, 1.0f); m_bank[SND_EXPLOSION] = b;
    // Plasma bolt: falling zap with a buzz
    b = make(0.4f);
    { Buf z = sweep(0.35f, 1800, 220, 0.12f); Buf q = make(0.35f);
      for (size_t i = 0; i < q.size(); i++) { float t = tsec(i); q[i] = (std::fmod(t * 120.f, 1.f) < 0.5f ? 0.4f : -0.4f) * std::exp(-t / 0.1f); }
      mix(b, z, 0.9f); mix(b, q, 0.5f); lowpass(b, 6000); }
    normalize(b, 0.65f); m_bank[SND_PLASMA] = b;
    b = make(1.0f);
    { float ph = 0; for (size_t i = 0; i < b.size(); i++) { float t = tsec(i), f = 200.f + 900.f * t; ph += TWO_PI * f / SR; b[i] = std::sin(ph) * std::min(1.f, t / 0.3f) * 0.6f + std::sin(ph * 2.01f) * 0.2f; } envAR(b, 0.1f, 0.1f); }
    normalize(b, 0.35f); m_bank[SND_PLASMA_CHARGE] = b;
    // Arc: crackling electric burst
    b = noise(0.3f, 216);
    { highpass(b, 1500); Rng r(217); float g = 0; for (size_t i = 0; i < b.size(); i++) { if (i % 160 == 0) g = r.next() > 0.1f ? 1.f : 0.1f; b[i] *= g; }
      envAR(b, 0.005f, 0.12f); Buf hum = tone(0.3f, 110, 0.1f); mix(b, hum, 0.4f); }
    normalize(b, 0.55f); m_bank[SND_ARC] = b;
    // Railgun: rising whine, then a hard metallic crack
    b = make(0.9f);
    { float ph = 0; for (size_t i = 0; i < b.size(); i++) { float t = tsec(i), f = 600.f * std::pow(6.f, t / 0.9f); ph += TWO_PI * f / SR; b[i] = std::sin(ph) * (t / 0.9f) * 0.5f; } }
    normalize(b, 0.35f); m_bank[SND_RAIL_CHARGE] = b;
    b = make(1.2f);
    { mix(b, clink({3200, 5100, 7300}, 0.05f, 218), 1.f); Buf n = noise(1.2f, 219); highpass(n, 1200); envExp(n, 0.06f); mix(b, n, 0.9f);
      mix(b, sweep(0.6f, 140, 40, 0.2f), 0.9f); room(b, 0.35f, 0.07f, 0.1f, 0.5f); }
    normalize(b, 0.95f); m_bank[SND_RAIL_FIRE] = b;
    // Flamethrower burst: roaring band-limited noise
    b = noise(0.3f, 220);
    { lowpass(b, 1400); Buf lo = brown(0.3f, 221); lowpass(lo, 200); mix(b, lo, 0.6f); envAR(b, 0.03f, 0.1f); }
    normalize(b, 0.45f); m_bank[SND_FLAME] = b;
    b = make(0.6f);
    { Buf s = noise(0.6f, 222); bandpass(s, 3000, 2.f); envExp(s, 0.25f); mix(b, s, 0.7f); mix(b, tone(0.3f, 330, 0.1f), 0.4f); mix(b, tone(0.3f, 247, 0.1f), 0.4f, 0.15f); }
    normalize(b, 0.5f); m_bank[SND_OVERHEAT] = b;
    // Heavy melee: whoosh + meaty chop
    b = make(0.45f);
    { Buf w = noise(0.25f, 223); bandpass(w, 500, 0.8f); envAR(w, 0.12f, 0.08f); mix(b, w, 0.8f);
      Buf c = noise(0.2f, 224); lowpass(c, 800); envExp(c, 0.05f); mix(b, c, 1.2f, 0.2f); mix(b, sweep(0.2f, 120, 60, 0.05f), 0.9f, 0.2f); }
    normalize(b, 0.8f); m_bank[SND_MELEE_HEAVY] = b;
    // Baton: electric snap
    b = make(0.3f); mix(b, m_bank[SND_ARC], 0.7f); mix(b, m_bank[SND_KNIFE_HIT], 0.7f); normalize(b, 0.7f); m_bank[SND_BATON_HIT] = b;
    // Grenade pin + spoon
    b = make(0.3f); mix(b, clink({2900, 4300}, 0.01f, 225), 1.f); mix(b, clink({1900, 3100, 4700}, 0.02f, 226), 0.8f, 0.12f); normalize(b, 0.5f); m_bank[SND_GRENADE_PIN] = b;
    b = make(0.2f); mix(b, clink({700, 1450}, 0.02f, 227), 1.f); { Buf n = noise(0.08f, 228); lowpass(n, 700); envExp(n, 0.02f); mix(b, n, 0.6f); }
    normalize(b, 0.5f); m_bank[SND_GRENADE_BOUNCE] = b;
    // Flashbang: sharp bang, then a high ear ring
    b = gunshot(0.8f, 0.004f, 5000, 0.03f, 300, 90, 0.04f, 2500, 0.2f, 0.5f, 229); normalize(b, 1.0f); m_bank[SND_FLASHBANG] = b;
    b = tone(3.2f, 3150, 1.2f, 0.05f); { Buf t2 = tone(3.2f, 3175, 1.0f, 0.05f); mix(b, t2, 0.8f); } normalize(b, 0.28f); m_bank[SND_EAR_RING] = b;
    b = noise(1.4f, 230); { highpass(b, 1800); lowpass(b, 6000); envAR(b, 0.05f, 1.0f); } normalize(b, 0.35f); m_bank[SND_SMOKE_HISS] = b;
    b = noise(0.6f, 231); { lowpass(b, 900); Rng r(232); float g = 0; for (size_t i = 0; i < b.size(); i++) { if (i % 900 == 0) g = 0.4f + 0.6f * std::fabs(r.next()); b[i] *= g; } envAR(b, 0.05f, 0.2f); }
    normalize(b, 0.4f); m_bank[SND_FIRE_BURN] = b;
    b = gunshot(0.3f, 0.005f, 4200, 0.02f, 240, 90, 0.02f, 2000, 0.08f, 0.2f, 233); bandpass(b, 2200, 0.8f); normalize(b, 0.5f); m_bank[SND_TURRET_FIRE] = b;
    b = make(1.0f); { mix(b, tone(0.9f, 1760, 0.3f, 0.002f), 0.7f); mix(b, tone(0.9f, 1760, 0.2f, 0.002f), 0.35f, 0.25f); room(b, 0.5f, 0.11f, 0.17f, 0.6f); }
    normalize(b, 0.45f); m_bank[SND_RADAR_PING] = b;
    b = noise(0.8f, 234); { lowpass(b, 1100); Buf lo = brown(0.8f, 235); lowpass(lo, 160); mix(b, lo, 0.8f); envAR(b, 0.04f, 0.5f); }
    normalize(b, 0.6f); m_bank[SND_JETPACK] = b;
    b = make(0.5f); { mix(b, clink({1300, 2600}, 0.015f, 236), 1.f); Buf z = noise(0.35f, 237); bandpass(z, 2400, 3.f); envAR(z, 0.01f, 0.3f); mix(b, z, 0.5f, 0.05f); mix(b, clink({900, 1700}, 0.03f, 238), 0.9f, 0.4f); }
    normalize(b, 0.55f); m_bank[SND_GRAPPLE] = b;
    b = make(0.5f); mix(b, m_bank[SND_KNIFE_HIT], 1.f); mix(b, voice(0.3f, 150, 90, 650, 1050, 0.01f, 239), 0.5f, 0.06f); normalize(b, 0.75f); m_bank[SND_TAKEDOWN] = b;
    b = make(0.6f); mix(b, clink({1500, 2400}, 0.02f, 240), 0.6f); mix(b, m_bank[SND_BOLT], 0.5f, 0.25f); normalize(b, 0.45f); m_bank[SND_INSPECT] = b;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mixer
// ─────────────────────────────────────────────────────────────────────────────
SoundEngine::SoundEngine(QObject *parent) : QIODevice(parent)
{
    synthesize();
}

SoundEngine::~SoundEngine()
{
    if (m_sink) m_sink->stop();
}

bool SoundEngine::start()
{
    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return false;
    QAudioFormat fmt;
    fmt.setSampleRate(RATE);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(fmt)) fmt = dev.preferredFormat();
    if (fmt.sampleFormat() != QAudioFormat::Int16 && fmt.sampleFormat() != QAudioFormat::Float) return false;
    if (fmt.channelCount() < 1) return false;
    m_fmt = fmt;
    open(QIODevice::ReadOnly);
    m_sink = new QAudioSink(dev, fmt, this);
    m_sink->setBufferSize(fmt.bytesForDuration(60000));     // ~60 ms latency
    m_sink->start(this);
    return true;
}

void SoundEngine::play(SoundId id, float gain, float pan, float pitch, float delaySec)
{
    if (!m_sink || id < 0 || id >= SND_COUNT || gain <= 0.001f) return;
    float a = (std::clamp(pan, -1.f, 1.f) + 1.f) * 0.7853982f;          // equal-power pan
    float rateScale = float(RATE) / float(std::max(1, m_fmt.sampleRate()));
    Voice v{&m_bank[id], 0.0, pitch * rateScale, gain * std::cos(a) * 1.414f, gain * std::sin(a) * 1.414f,
            int(delaySec * m_fmt.sampleRate()), false, false};
    std::lock_guard<std::mutex> g(m_lock);
    if (m_voices.size() > 48) {                                      // voice stealing: drop the oldest one-shot
        for (size_t i = 0; i < m_voices.size(); i++) if (!m_voices[i].ambient && !m_voices[i].clip) { m_voices.erase(m_voices.begin() + long(i)); break; }
    }
    m_voices.push_back(v);
}

void SoundEngine::setAmbience(int id, float gain)
{
    if (!m_sink) return;
    std::lock_guard<std::mutex> g(m_lock);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice &v) { return v.ambient; }), m_voices.end());
    if (id < 0 || id >= SND_COUNT) return;
    float rateScale = float(RATE) / float(std::max(1, m_fmt.sampleRate()));
    m_voices.push_back(Voice{&m_bank[id], 0.0, rateScale, gain, gain, 0, true, true});
}

int SoundEngine::addClip(std::vector<float> samples)
{
    std::lock_guard<std::mutex> g(m_lock);
    m_clips.push_back(std::make_unique<std::vector<float>>(std::move(samples)));
    return int(m_clips.size()) - 1;
}

float SoundEngine::clipSeconds(int handle) const
{
    if (handle < 0 || handle >= int(m_clips.size())) return 0.f;
    return float(m_clips[size_t(handle)]->size()) / float(RATE);
}

void SoundEngine::playClip(int handle, float gain)
{
    if (!m_sink || handle < 0 || gain <= 0.001f) return;
    std::lock_guard<std::mutex> g(m_lock);
    if (handle >= int(m_clips.size())) return;
    float rateScale = float(RATE) / float(std::max(1, m_fmt.sampleRate()));
    Voice v{m_clips[size_t(handle)].get(), 0.0, rateScale, gain, gain, 0, false, false, true};
    m_voices.push_back(v);
}

void SoundEngine::stopClips()
{
    std::lock_guard<std::mutex> g(m_lock);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice &v) { return v.clip; }), m_voices.end());
}

qint64 SoundEngine::readData(char *data, qint64 maxSize)
{
    const int ch = std::max(1, m_fmt.channelCount());
    const bool isFloat = m_fmt.sampleFormat() == QAudioFormat::Float;
    const int bps = isFloat ? 4 : 2;
    const qint64 frames = maxSize / (ch * bps);
    if (frames <= 0) return 0;
    std::vector<float> L(size_t(frames), 0.f), Rr(size_t(frames), 0.f);
    {
        std::lock_guard<std::mutex> g(m_lock);
        for (Voice &v : m_voices) {
            qint64 f = 0;
            if (v.delay > 0) { qint64 skip = std::min<qint64>(v.delay, frames); v.delay -= int(skip); f = skip; }
            const std::vector<float> &b = *v.buf;
            const size_t n = b.size();
            if (n < 2) { v.pos = 1e18; continue; }
            for (; f < frames; f++) {
                size_t i = size_t(v.pos);
                if (i + 1 >= n) {
                    if (!v.loop) { v.pos = 1e18; break; }
                    v.pos -= double(n - 1); i = size_t(v.pos);
                }
                float fr = float(v.pos - double(i));
                float s = b[i] + (b[i + 1] - b[i]) * fr;
                L[size_t(f)] += s * v.gainL; Rr[size_t(f)] += s * v.gainR;
                v.pos += v.step;
            }
        }
        m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice &v) { return v.pos >= 1e17; }), m_voices.end());
    }
    const float m = m_muted ? 0.f : m_master;
    auto shape = [](float x) {                                       // soft clipper (tanh approximation)
        x = std::clamp(x, -3.f, 3.f);
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    };
    for (qint64 f = 0; f < frames; f++) {
        float l = shape(L[size_t(f)] * m), r = shape(Rr[size_t(f)] * m);
        for (int c = 0; c < ch; c++) {
            float s = ch == 1 ? (l + r) * 0.5f : (c == 0 ? l : c == 1 ? r : 0.f);
            if (isFloat) std::memcpy(data + (f * ch + c) * 4, &s, 4);
            else { int16_t v = int16_t(std::clamp(s, -1.f, 1.f) * 32767.f); std::memcpy(data + (f * ch + c) * 2, &v, 2); }
        }
    }
    m_served += frames * ch * bps;
    return frames * ch * bps;
}
