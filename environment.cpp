// =============================================================================
//  environment.cpp  -  Time of day, weather, flashlight, trees
//
//  Time of day blends the biome's palette toward dawn / dusk / night: sky and
//  haze are tinted per pixel, the sun's strength and colour change, and the
//  ground light is re-mixed from the two baked light-map layers (ambient
//  occlusion and sun x shadow) so shadows stay correct without a re-bake.
//  Weather is a box of world-space particles that follows the camera: rain
//  streaks (projected lines), snow, blizzard, sand and ash, plus thicker fog
//  and lightning. At night a flashlight cone lights the frame using the
//  z-buffer (brighter close up, falling off with depth).
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static inline float frandE() { return float(rand()) / float(RAND_MAX); }
static inline float frandE(float a, float b) { return a + (b - a) * frandE(); }

static const char *const WEATHER_NAMES[WX_COUNT] = { "CLEAR", "RAIN", "SNOW", "SANDSTORM", "ASHFALL", "BLIZZARD" };
const char *weatherName(int w) { return WEATHER_NAMES[clampi(w, 0, WX_COUNT - 1)]; }

// 0 = full night .. 1 = full day for an hour of the day
static float dayAmount(float hour)
{
    float s = std::sin((hour - 6.f) / 12.f * 3.14159265f);     // 0 at 6h/18h, 1 at noon
    return clampf(s * 2.2f + 0.35f, 0.f, 1.f);
}
// 0..1 how strongly dawn/dusk colours show
static float twilightAmount(float hour)
{
    float a = std::exp(-std::pow((hour - 6.5f) / 1.3f, 2.f)), b = std::exp(-std::pow((hour - 18.3f) / 1.3f, 2.f));
    return clampf(a + b, 0.f, 1.f);
}

void MainWindow::setEnvironment(float hour, int weather)
{
    m_hour = hour;
    m_weather = weather;
    m_wx.clear();
    m_envDirty = true;
    computeEnv();
    m_lightAmb = m_env.ambient; m_lightSun = m_env.sunStrength;
    recomposeLight();
}

// Build this frame's palette from the biome + hour + weather
void MainWindow::computeEnv()
{
    Theme BT = THEMES[m_theme];
    if (m_water) {                                    // the campus: the swamp's trees and grass under a clear Kolkata sky
        BT.zenith = rgb(70, 125, 200); BT.horizon = rgb(190, 212, 230); BT.haze = rgb(178, 196, 210);
        BT.fogDensity *= 0.35f; BT.sunStrength = std::max(BT.sunStrength, 0.95f); BT.ambient = std::max(BT.ambient, 0.55f);
        BT.cloudLit = rgb(245, 245, 248); BT.cloudDark = rgb(190, 196, 206);
    }
    m_env = BT;
    float day = dayAmount(m_hour), tw = twilightAmount(m_hour);
    m_dayLevel = day;
    // sky tint applied to every sky pixel (and the haze the fog fades into)
    uint32_t nightTint = rgb(40, 52, 96), duskTint = rgb(255, 150, 96);
    float tr = 1.f, tg = 1.f, tb = 1.f;
    auto mixTint = [&](uint32_t t, float k) { tr += (R(t) / 255.f - tr) * k; tg += (G(t) / 255.f - tg) * k; tb += (B(t) / 255.f - tb) * k; };
    mixTint(nightTint, (1.f - day) * 0.85f);
    mixTint(duskTint, tw * 0.55f * day);
    float bright = 0.26f + 0.74f * day;
    m_skyTint[0] = tr * bright; m_skyTint[1] = tg * bright; m_skyTint[2] = tb * bright;
    if (m_water) { m_skyTint[0] *= 0.82f; m_skyTint[1] *= 0.97f; m_skyTint[2] *= 1.3f; }   // the campus: a blue sky over the swamp's
    auto tint = [&](uint32_t c) { return rgb(clampi(int(R(c) * m_skyTint[0]), 0, 255), clampi(int(G(c) * m_skyTint[1]), 0, 255), clampi(int(B(c) * m_skyTint[2]), 0, 255)); };
    m_env.haze = tint(BT.haze);
    m_env.sunColor = lerpCol(lerpCol(BT.sunColor, rgb(255, 140, 70), tw * 0.7f), rgb(150, 170, 230), 1.f - day);   // moonlight at night
    m_env.sunStrength = BT.sunStrength * (0.15f + 0.85f * day) * (1.f - 0.3f * tw);
    // night keeps some ambient; the city and the facility have artificial light
    float nightAmb = (m_theme == BIO_FACILITY || m_theme == BIO_RUINS) ? 0.72f : 0.5f;
    m_env.ambient = BT.ambient * (nightAmb + (1.f - nightAmb) * day);
    // weather: thicker fog, less sun
    static const float FOG_MUL[WX_COUNT] = { 1.f, 1.35f, 1.5f, 2.8f, 1.6f, 3.4f };
    static const float SUN_MUL[WX_COUNT] = { 1.f, 0.55f, 0.65f, 0.6f, 0.7f, 0.4f };
    m_env.fogDensity = BT.fogDensity * FOG_MUL[m_weather] * (day < 0.3f ? 1.25f : 1.f);
    m_env.sunStrength *= SUN_MUL[m_weather];
    if (m_weather == WX_SAND) m_env.haze = lerpCol(m_env.haze, rgb(196, 156, 100), 0.6f);
    if (m_weather == WX_BLIZZARD || m_weather == WX_SNOW) m_env.haze = lerpCol(m_env.haze, scaleCol(rgb(226, 232, 240), bright), 0.5f);
    if (m_weather == WX_ASH) m_env.haze = lerpCol(m_env.haze, scaleCol(rgb(90, 80, 76), bright), 0.4f);
    if (m_lightning > 0.f) { m_env.ambient += 0.8f * m_lightning; m_env.haze = lerpCol(m_env.haze, rgb(220, 225, 255), 0.5f * m_lightning); }
}

void MainWindow::updateEnvironment(float dt)
{
    if (m_dayCycle) m_hour = std::fmod(m_hour + dt * (24.f / 300.f), 24.f);     // a full day in 5 minutes
    m_lightning = std::max(0.f, m_lightning - dt * 3.f);
    if ((m_weather == WX_RAIN) && frandE() < dt * 0.05f) {                      // a storm now and then
        m_lightning = 1.f;
        sfx(SND_EXPLOSION, 0.35f, 0.45f, frandE(0.3f, 1.4f));
    }
    computeEnv();
    // re-mix the ground light when it has drifted (cheap: two lookups per texel)
    m_envTimer -= dt;
    if ((std::fabs(m_env.ambient - m_lightAmb) > 0.02f || std::fabs(m_env.sunStrength - m_lightSun) > 0.02f) && m_envTimer <= 0.f) {
        m_lightAmb = m_env.ambient; m_lightSun = m_env.sunStrength;
        recomposeLight();
        m_envTimer = 0.4f;
    }

    // Weather particles: a box around the camera, recycled as they leave it
    static const int COUNT[WX_COUNT] = { 0, 900, 700, 500, 450, 1400 };
    int want = COUNT[m_weather];
    const float BOX = 11.f;
    float windX = 0.f, windY = 0.f;
    if (m_weather == WX_SAND) { windX = 9.f; windY = 3.f; }
    if (m_weather == WX_BLIZZARD) { windX = 6.f; windY = 2.f; }
    if (m_weather == WX_SNOW) { windX = 0.6f; }
    auto spawn = [&](WxDrop &p, bool top) {
        p.x = m_camX + frandE(-BOX, BOX); p.y = m_camY + frandE(-BOX, BOX);
        float g = groundAt(p.x, p.y);
        p.z = top ? m_eye + frandE(1.5f, 5.f) : g + frandE(0.f, 5.f);
        if (m_weather == WX_SAND) p.z = g + frandE(0.f, 1.6f);
        p.vx = windX + frandE(-0.5f, 0.5f); p.vy = windY + frandE(-0.5f, 0.5f);
        p.vz = m_weather == WX_RAIN ? -14.f : m_weather == WX_SAND ? frandE(-0.3f, 0.3f) : m_weather == WX_ASH ? -0.5f : -1.4f;
        p.seed = rand();
    };
    if (int(m_wx.size()) != want) { m_wx.resize(size_t(want)); for (WxDrop &p : m_wx) spawn(p, false); }
    for (WxDrop &p : m_wx) {
        float sway = m_weather == WX_SNOW || m_weather == WX_ASH ? std::sin(m_time * 1.3f + p.seed) * 0.4f : 0.f;
        p.x += (p.vx + sway) * dt; p.y += p.vy * dt; p.z += p.vz * dt;
        if (p.z < groundAt(p.x, p.y) || std::fabs(p.x - m_camX) > BOX || std::fabs(p.y - m_camY) > BOX) spawn(p, true);
    }
}

// Weather drawn into the frame: depth-tested against the z-buffer
void MainWindow::renderWeather()
{
    if (m_weather == WX_NONE) return;
    float bright = 0.35f + 0.65f * m_dayLevel;
    for (const WxDrop &p : m_wx) {
        float sx, sy, d;
        if (!project(p.x, p.y, p.z, sx, sy, d) || d < 0.3f) continue;
        int x = int(sx), y = int(sy);
        if (x < 0 || x >= W || y < 0 || y >= H || d >= m_depth[size_t(y) * W + x]) continue;
        switch (m_weather) {
        case WX_RAIN: {
            float tx, ty, td;
            if (project(p.x - p.vx * 0.03f, p.y - p.vy * 0.03f, p.z + 0.22f, tx, ty, td))
                m_cv.line(int(tx), int(ty), x, y, withAlpha(scaleCol(rgb(190, 205, 230), bright), d < 4.f ? 110 : 70));
            break; }
        case WX_SAND: {
            int len = std::max(2, int(18.f / d));
            m_cv.line(x - len, y, x, y, withAlpha(rgb(214, 180, 126), 90));
            break; }
        case WX_ASH: {
            bool ember = (p.seed & 15) == 0;
            int r = std::max(1, int(1.6f / d));
            if (ember) m_cv.glow(x, y, r + 2, rgb(255, 120, 40), 0.9f);
            else m_cv.fillCircle(x, y, r, withAlpha(rgb(120, 112, 108), 200));
            break; }
        default: {                                         // snow / blizzard
            int r = std::max(0, int(2.2f / d));
            uint32_t c = withAlpha(scaleCol(rgb(248, 250, 255), bright), 230);
            if (r == 0) m_cv.blend(x, y, c); else m_cv.fillCircle(x, y, r, c);
            break; }
        }
    }
    // the air itself
    if (m_weather == WX_SAND) m_cv.blendRect(0, 0, W, H, withAlpha(rgb(190, 150, 96), 40));
    if (m_weather == WX_BLIZZARD) m_cv.blendRect(0, 0, W, H, withAlpha(scaleCol(rgb(230, 236, 244), bright), 60));
    if (m_lightning > 0.f) m_cv.blendRect(0, 0, W, H, withAlpha(rgb(230, 235, 255), int(110 * m_lightning)));
}

// Flashlight at night: a cone around the aim point, scaled by the z-buffer depth
void MainWindow::renderFlashlight()
{
    if (!m_flashlightOn || m_dayLevel > 0.6f) return;
    float strength = (0.6f - m_dayLevel) / 0.6f;
    float ax, ay; aimPoint(ax, ay);
    if (thirdPerson()) { ax = W / 2.f; ay = H / 2.f; }
    const float RAD = 210.f, R2 = RAD * RAD;
    int y0 = std::max(0, int(ay - RAD)), y1 = std::min(H - 1, int(ay + RAD));
    int x0 = std::max(0, int(ax - RAD)), x1 = std::min(W - 1, int(ax + RAD));
    for (int y = y0; y <= y1; y++) {
        float dy = y - ay;
        uint32_t *row = m_pixels + size_t(y) * W;
        const float *dep = m_depth.data() + size_t(y) * W;
        for (int x = x0; x <= x1; x++) {
            float dx = x - ax, r2 = dx * dx + dy * dy;
            if (r2 >= R2) continue;
            float d = dep[x];
            if (d > 25.f) continue;                        // sky / far away: nothing to light
            float cone = 1.f - r2 / R2;
            cone *= cone;
            float k = strength * cone * std::min(1.4f, 3.f / std::max(0.6f, d));
            uint32_t c = row[x];
            int r = std::min(255, int(R(c) * (1.f + 1.6f * k) + 26 * k));
            int g = std::min(255, int(G(c) * (1.f + 1.5f * k) + 24 * k));
            int b = std::min(255, int(B(c) * (1.f + 1.3f * k) + 18 * k));
            row[x] = rgb(r, g, b);
        }
    }
}
