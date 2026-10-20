// =============================================================================
//  render_hud.cpp  -  Frame composition, post-processing, HUD, menus, overlays
//
//  All panels, bars, icons, the compass, minimap, crosshair and markers are
//  rasterized with the primitives in raster.h; text uses the bitmap pixel
//  font in pixelfont.h. No vector drawing (QPainter paths/fonts) is used.
// =============================================================================
#include "mainwindow.h"
#include "gunbuild.h"
#include "gunmodels.h"
#include "pixelfont.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static const float PI = 3.14159265f;
static const uint32_t AMBER = 0xFFFFB43C, AMBER_DIM = 0xFF8A6420, UI_BG = 0xFF0E1216, RED = 0xFFF04A3C, GREEN = 0xFF5AE68C;
static inline QColor qc(uint32_t c, int a = 255) { return QColor(R(c), G(c), B(c), a); }

// ─────────────────────────────────────────────────────────────────────────────
//  Text queue
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::text(float x, float y, float w, float h, int flags, const QString &s, QColor c, int px, int weight, bool shadow)
{
    m_text.push_back(TextCmd{QRectF(x, y, w, h), flags, s, c, px, weight, shadow});
}

// Text is rasterized with the bitmap pixel font (pixelfont.h) — no vector glyphs
static std::string toFontString(const QString &q)
{
    std::string out;
    out.reserve(size_t(q.size()));
    for (QChar ch : q) {
        ushort u = ch.unicode();
        out += u == 0x00B0 ? char(127) : (u >= 32 && u < 127) ? char(u) : '?';
    }
    return out;
}

int MainWindow::fontScale(int px) { return std::max(1, (px + 4) / 9); }

void MainWindow::flushText()
{
    for (const TextCmd &t : m_text) {
        int sc = fontScale(t.px);
        bool bold = t.weight >= 700 && sc >= 2;
        uint32_t col = argb(t.c.alpha(), t.c.red(), t.c.green(), t.c.blue());
        uint32_t shadow = argb(int(t.c.alpha() * 0.75f), 0, 0, 0);
        std::string str = toFontString(t.s);
        int rx = int(t.r.x()), ry = int(t.r.y()), rw = int(t.r.width()), rh = int(t.r.height());
        std::vector<std::string> lines = (t.flags & Qt::TextWordWrap) ? pf::wrap(str, rw, sc) : std::vector<std::string>{str};
        int lineH = pf::LINE * sc, blockH = int(lines.size()) * lineH - (pf::LINE - pf::CAP) * sc;
        int y0 = ry;
        if (t.flags & Qt::AlignVCenter) y0 = ry + (rh - blockH) / 2;
        else if (t.flags & Qt::AlignBottom) y0 = ry + rh - blockH;
        for (size_t i = 0; i < lines.size(); i++) {
            int w = pf::width(lines[i], sc, bold);
            int x0 = rx;
            if (t.flags & Qt::AlignHCenter) x0 = rx + (rw - w) / 2;
            else if (t.flags & Qt::AlignRight) x0 = rx + rw - w;
            int y = y0 + int(i) * lineH;
            if (t.shadow) pf::drawLine(m_cv, x0 + std::max(1, sc / 2), y + std::max(1, sc / 2), lines[i], shadow, sc, bold);
            pf::drawLine(m_cv, x0, y, lines[i], col, sc, bold);
        }
    }
    m_text.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Frame
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::render()
{
    m_text.clear();
    if (m_screen == SCR_EDITOR) { m_menuRects.clear(); renderEditor(); flushText(); return; }
    if (m_screen != SCR_PLAYING) {
        m_menuRects.clear();
        if (m_screen == SCR_SETTINGS && m_settingsReturnScreen == SCR_PLAYING) {
            computeView();
            renderTerrain();
            renderSky();
            renderActors3D();
            renderSprites();
            renderParticles();
            renderWorldFX();
            renderTracers();
            renderLensFlare();
            pixelate();
            if (!thirdPerson()) {
                renderViewModel();
                renderCasings();
                renderReflexSight();
            }
            renderLaser();
            renderScope();
            renderPostFX();
            for (int i = 0; i < W * H; i++) {
                uint32_t c = m_pixels[i];
                int l = (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 9;
                m_pixels[i] = rgb(l, l + 2, l + 6);
            }
        } else {
            renderMenuBackdrop();
        }
        switch (m_screen) {
        case SCR_MENU:        renderMenu();       break;
        case SCR_MAP_SELECT:  renderMapSelect();  break;
        case SCR_DIFF_SELECT: renderDiffSelect(); break;
        case SCR_CHAR_SELECT: renderCharSelect(); break;
        case SCR_SETTINGS:    renderSettings();   break;
        case SCR_ARSENAL:     renderArsenal();    break;
        case SCR_LOADOUT:     renderLoadout();    break;
        case SCR_STATS:       renderStats();      break;
        case SCR_CAMPAIGN:    renderCampaign();   break;
        case SCR_BRIEFING:    renderBriefing();   break;
        case SCR_DEBRIEF:     m_debriefT += m_dt; renderDebrief(); break;
        case SCR_MULTIPLAYER_LOBBY: renderMultiplayerLobby(); break;
        default: break;
        }

        flushText();
        return;
    }
    if (m_killcamActive) { renderKillcam(); return; }
    if (m_flyActive) { renderFlyby(); return; }
    computeView();
    renderTerrain();
    renderSky();
    renderActors3D();
    renderSprites();
    renderParticles();
    renderWorldFX();
    renderBossFX();
    renderMissionWorld();
    renderTracers();
    renderWeather();
    renderFlashlight();
    renderLensFlare();
    pixelate();
    renderDamageNumbers();
    if (!thirdPerson()) {
        renderViewModel();
        renderCasings();
        renderReflexSight();
    }
    renderLaser();
    renderScope();
    renderPostFX();
    renderScreenFlash();
    renderHUD();
    renderMissionHUD();
    if (!m_campaignRun) renderRadio();                          // the radio commander outside the campaign
    flushText();
    renderLevelBanner();
    if (m_showArmory) renderArmoryOverlay();
    renderPauseMenu();
    if (m_gs.gameOver) renderGameOver();
    flushText();
}

// Screen-space post: muzzle light, cinematic vignette, damage + low-health
void MainWindow::renderPostFX()
{
    // vig: radial distance term (0..1024); vigDark: cinematic darkening (256 = none)
    static std::vector<uint16_t> vig, vigDark;
    static int clearL[H], clearR[H];                       // per row: untouched centre span
    if (vig.empty()) {
        vig.resize(W * H); vigDark.resize(W * H);
        for (int y = 0; y < H; y++) {
            clearL[y] = W; clearR[y] = 0;
            for (int x = 0; x < W; x++) {
                float dx = (x - W / 2.f) / (W / 2.f), dy = (y - H / 2.f) / (H / 2.f);
                float d2 = std::min(1.f, (dx * dx + dy * dy) * 0.5f);
                vig[y * W + x] = uint16_t(d2 * 1024);
                float k = std::max(0.f, (d2 - 0.12f) / 0.88f);
                int dk = int(256 * (1.f - 0.34f * k));
                vigDark[y * W + x] = uint16_t(dk);
                if (dk >= 256) { clearL[y] = std::min(clearL[y], x); clearR[y] = std::max(clearR[y], x + 1); }
            }
            if (clearL[y] >= clearR[y]) { clearL[y] = W; clearR[y] = W; }
        }
    }
    const Player &pl = m_gs.player;
    const int maxHP = this->maxHP();
    float hpFrac = float(pl.hp) / maxHP;
    float lowHP = hpFrac < 0.3f && !m_gs.gameOver ? (0.3f - hpFrac) / 0.3f : 0.f;
    float pulse = 0.65f + 0.35f * std::sin(m_time * 6.f);
    int dmg = int(std::max(0.f, pl.damageFlash / 0.45f) * 256);
    int low = int(lowHP * pulse * 256);
    int desat = int(lowHP * 180);
    int fl = int(m_flashLight * 256);
    int acid = int(m_acidFlash * (0.6f + 0.4f * std::sin(m_time * 14.f)) * 256);
    if (!fl && !desat && !acid && !dmg && !low) {
        // Fast path: vignette only, skipping the untouched centre of each row
        for (int y = 0; y < H; y++) {
            uint32_t *row = m_pixels + y * W;
            const uint16_t *vd = &vigDark[y * W];
            for (int pass = 0; pass < 2; pass++) {
                int xa = pass == 0 ? 0 : clearR[y], xb = pass == 0 ? clearL[y] : W;
                for (int x = xa; x < xb; x++) {
                    uint32_t c = row[x], k = vd[x];
                    row[x] = 0xFF000000 | ((((c & 0xFF00FF) * k) >> 8) & 0xFF00FF) | ((((c & 0x00FF00) * k) >> 8) & 0x00FF00);
                }
            }
        }
        return;
    }
    uint32_t hc = m_curHazardColor ? m_curHazardColor : THEMES[m_theme].hazLight;
    int tr = R(hc), tg = G(hc), tb = B(hc);
    if (m_theme == BIO_DESERT && m_curHazardName && strcmp(m_curHazardName, "TAR") == 0) {
        tr = 180; tg = 120; tb = 40;
    }
    for (int i = 0; i < W * H; i++) {
        uint32_t c = m_pixels[i];
        int r = R(c), g = G(c), b = B(c);
        if (fl) { r += (46 * fl) >> 8; g += (30 * fl) >> 8; b += (10 * fl) >> 8; }
        int v = vig[i];                                    // 0..1024
        int dark = vigDark[i];
        r = (r * dark) >> 8; g = (g * dark) >> 8; b = (b * dark) >> 8;
        if (desat) { int l = (r * 77 + g * 150 + b * 29) >> 8; r += ((l - r) * desat) >> 8; g += ((l - g) * desat) >> 8; b += ((l - b) * desat) >> 8; }
        if (acid) {
            int e = 256 - ((1024 - v) >> 2);              // stronger toward the edges
            int k = (acid * std::max(64, e)) >> 9;
            r = (r * (256 - k) + tr * k) >> 8;
            g = (g * (256 - k) + tg * k) >> 8;
            b = (b * (256 - k) + tb * k) >> 8;
        }
        if (dmg || low) {
            int e = (v * v) >> 10;                         // edges only
            int k = ((dmg + low / 2) * e) >> 10;
            r += ((200 - r) * std::min(256, k)) >> 8; g = (g * (256 - std::min(256, k))) >> 8; b = (b * (256 - std::min(256, k))) >> 8;
        }
        m_pixels[i] = 0xFF000000 | (uint32_t(std::min(255, r)) << 16) | (uint32_t(std::min(255, g)) << 8) | uint32_t(std::min(255, b));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HUD
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderHUD()
{
    if (m_gs.levelComplete) return;                 // the intermission screen replaces the HUD
    const Player &pl = m_gs.player;
    bool scoped = effectiveScope(pl.curWeapon) && m_adsT > 0.6f;
    if (!scoped) { renderCompass(); renderMinimap(); renderKillFeed(); }
    renderHealthPanel();
    renderWeaponPanel();
    if (!scoped) renderWeaponStatsBox();
    if (!scoped) renderTargetInfo();
    renderBossBar();
    renderDamageDirection();
    renderSquadTags();
    renderCrosshair();
    renderHitMarkers();
    if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
        renderMultiplayerHUD();
        if (m_showScoreboard) renderScoreboard();
    }


    float ax, ay; aimPoint(ax, ay);
    int cwp = std::max(0, pl.curWeapon);
    int mag = pl.mag[cwp], res = pl.reserve[cwp];
    int maxMag = effectiveMagSize(cwp);
    if (pl.curWeapon >= 0 && usesAmmo(pl.curWeapon) && pl.reloadTimer <= 0 && pl.switchTimer <= 0 && !m_gs.gameOver) {
        if (mag == 0 && res == 0) text(ax - 100, ay + 34, 200, 20, Qt::AlignCenter, "NO AMMO", QColor(255, 70, 60), 14, 800);
        else if (mag <= maxMag / 4 && res > 0) text(ax - 100, ay + 34, 200, 20, Qt::AlignCenter, QString("[%1]  RELOAD").arg(keyName(m_keyBinds[ACT_RELOAD])), QColor(255, 190, 80, int(180 + 75 * std::sin(m_time * 8))), 13, 700);
        else if (m_nearSecret) text(ax - 150, ay + 34, 300, 20, Qt::AlignCenter, QString("[%1]  INSPECT WALL").arg(keyName(m_keyBinds[ACT_INTERACT])), QColor(255, 215, 100, int(210 + 45 * std::sin(m_time * 9))), 14, 800);
        else if (m_nearDoor) {
            QString prompt = QString("[%1]  OPEN %2").arg(keyName(m_keyBinds[ACT_INTERACT])).arg((m_gameMode == MODE_MULTIPLAYER) ? "BLAST DOOR" : "DOOR");
            text(ax - 160, ay + 34, 320, 20, Qt::AlignCenter, prompt, (m_gameMode == MODE_MULTIPLAYER) ? QColor(255, 215, 60, int(200 + 55 * std::sin(m_time * 7))) : QColor(220, 220, 220, int(190 + 50 * std::sin(m_time * 6))), 13, 800);
        }
    }
    if (m_headshotBanner > 0) {
        float t = m_headshotBanner / 1.3f;
        float pop = 1.f + std::max(0.f, t - 0.8f) * 2.5f;
        int a = int(255 * std::min(1.f, t * 3.f));
        int sx = int(ax) - 70, sy = int(ay) - 70;
        // little raster skull
        m_cv.fillCircle(sx, sy, 8, withAlpha(rgb(255, 255, 255), a));
        m_cv.fillRect(sx - 5, sy + 5, 10, 6, withAlpha(rgb(255, 255, 255), a));
        m_cv.fillCircle(sx - 3, sy, 2, withAlpha(rgb(200, 30, 30), a));
        m_cv.fillCircle(sx + 3, sy, 2, withAlpha(rgb(200, 30, 30), a));
        text(sx + 12, sy - 14, 200, 28, Qt::AlignLeft | Qt::AlignVCenter, "HEADSHOT", QColor(255, 80, 60, a), int(20 * pop), 900);
    }
    if (m_secretBanner > 0) {
        float t = std::min(1.f, m_secretBanner / 0.5f);
        m_cv.blendRect(0, 136, W, 44, withAlpha(UI_BG, int(170 * t)));
        m_cv.blendRect(0, 136, W, 2, withAlpha(AMBER, int(255 * t)));
        text(0, 138, W, 26, Qt::AlignCenter, "SECRET AREA DISCOVERED", QColor(255, 210, 120, int(255 * t)), 22, 900);
        text(0, 162, W, 16, Qt::AlignCenter, QString("Secrets %1 / %2   -   expect heavy resistance").arg(m_gs.secretsFound).arg(m_gs.secretsTotal),
             QColor(220, 220, 220, int(230 * t)), 12, 600);
    }
    if (m_acidFlash > 0.3f && !m_gs.gameOver) {
        const char *hname = m_curHazardName ? m_curHazardName : THEMES[m_theme].hazName;
        uint32_t hcol = m_curHazardColor ? m_curHazardColor : THEMES[m_theme].hazLight;
        QColor tc = qc(hcol);
        if (m_theme == BIO_DESERT && hname && strcmp(hname, "TAR") == 0) tc = QColor(240, 180, 80);
        tc.setAlpha(int(255 * std::min(1.f, m_acidFlash)));
        text(0, H / 2 + 60, W, 22, Qt::AlignCenter, QString("%1!  JUMP OUT").arg(hname ? hname : "HAZARD"), tc, 18, 900);
    }
    if (m_combo >= 2 && m_comboDisplay > 0) {
        float t = std::min(1.f, m_comboDisplay / 0.5f);
        text(0, 96, W, 30, Qt::AlignCenter, QString("x%1  KILL STREAK").arg(m_combo), QColor(255, 200, 60, int(255 * t)), 22, 800);
    }
    if (m_pixelScale > 1)
        text(0, H - 16, W, 14, Qt::AlignCenter, QString("PIXEL BLOCK %1x%1  ( [ ] to change )").arg(m_pixelScale), QColor(255, 200, 100, 200), 11, 600);
    if (m_gs.frameNum < 420 && m_gs.level == 1 && !scoped) {
        m_cv.blendRect(0, H - 20, W, 20, withAlpha(0, 150));
        text(0, H - 19, W, 18, Qt::AlignCenter, "WASD move  LMB fire  RMB sights  R reload  1-4 weapons  SPACE jump  CTRL crouch  ALT+A/D lean  E use  ESC pause",
             QColor(220, 220, 220, 220), 11, 500, false);
    }
}

void MainWindow::renderCompass()
{
    const Player &pl = m_gs.player;
    const int cx = W / 2, y0 = 8, wdt = 360, hgt = 26;
    (void)wdt;
    m_cv.hGrad(cx - wdt / 2, y0, wdt / 2, hgt, withAlpha(UI_BG, 0), withAlpha(UI_BG, 170));
    m_cv.hGrad(cx, y0, wdt / 2, hgt, withAlpha(UI_BG, 170), withAlpha(UI_BG, 0));
    float a = std::atan2(pl.dirY, pl.dirX) * 180.f / PI;
    float head = std::fmod(a + 90.f + 720.f, 360.f);
    const float ppd = 2.f;
    static const char *CARD[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    for (int deg = 0; deg < 360; deg += 5) {
        float dlt = std::fmod(deg - head + 540.f, 360.f) - 180.f;
        if (std::fabs(dlt) > 88) continue;
        int x = cx + int(dlt * ppd);
        int fade = int(255 * (1.f - std::fabs(dlt) / 90.f));
        bool major = deg % 45 == 0, mid = deg % 15 == 0;
        int th = major ? 9 : mid ? 6 : 3;
        m_cv.blendRect(x, y0 + hgt - th - 2, 1, th, withAlpha(major ? AMBER : 0xFFDDDDDD, fade));
        if (major) text(x - 15, y0 + 1, 30, 14, Qt::AlignCenter, CARD[deg / 45], deg == 0 ? QColor(255, 90, 70, fade) : QColor(255, 255, 255, fade), deg % 90 == 0 ? 13 : 11, 700, false);
        else if (mid) text(x - 15, y0 + 2, 30, 12, Qt::AlignCenter, QString::number(deg), QColor(200, 200, 200, fade * 2 / 3), 9, 500, false);
    }
    // Alerted enemies show as red pips on the compass
    for (const Enemy &e : m_gs.enemies) {
        if (!e.alive || (e.state != ES_ALERT && aliveEnemies() > 3)) continue;
        float ea = std::atan2(e.y - pl.posY, e.x - pl.posX) * 180.f / PI;
        float dlt = std::fmod(ea - a + 540.f, 360.f) - 180.f;
        if (std::fabs(dlt) > 88) continue;
        int x = cx + int(dlt * ppd);
        m_cv.tri(x - 4, y0 + hgt + 2, x + 4, y0 + hgt + 2, x, y0 + hgt + 8, RED);
    }

    // Health drops on the compass
    for (const HealthKit &k : m_gs.healthKits) {
        if (!k.active) continue;
        if (!k.emergency && pl.hp > 25) continue;
        float dx = k.x - pl.posX, dy = k.y - pl.posY;
        float dist = std::sqrt(dx * dx + dy * dy);
        float ka = std::atan2(dy, dx) * 180.f / PI;
        float dlt = std::fmod(ka - a + 540.f, 360.f) - 180.f;
        bool pulse = (int(m_time * 6.f) & 1) == 0;
        uint32_t col = (k.emergency && pulse) ? 0xFF88FFAA : GREEN;
        int cy = y0 + 13;
        if (std::fabs(dlt) <= 88.f) {
            int x = cx + int(dlt * ppd);
            m_cv.blendRect(x - 5, cy - 5, 11, 11, withAlpha(0x000000, 200));
            m_cv.fillRect(x - 4, cy - 1, 9, 3, col);
            m_cv.fillRect(x - 1, cy - 4, 3, 9, col);
            m_cv.tri(x, y0 + 2, x - 3, y0 - 2, x + 3, y0 - 2, col);
            text(x - 20, y0 - 8, 40, 9, Qt::AlignCenter, QString("%1m").arg(std::max(1, int(dist))), QColor(100, 255, 140), 9, 700, false);
        } else {
            int x = dlt < 0 ? cx - 176 : cx + 176;
            if (dlt < 0) {
                m_cv.tri(x, cy, x + 6, cy - 4, x + 6, cy + 4, col);
                m_cv.fillRect(x + 8, cy - 1, 5, 2, col);
                m_cv.fillRect(x + 9, cy - 3, 3, 6, col);
                text(x, y0 - 8, 36, 9, Qt::AlignLeft, QString("%1m").arg(std::max(1, int(dist))), QColor(100, 255, 140), 9, 700, false);
            } else {
                m_cv.tri(x, cy, x - 6, cy - 4, x - 6, cy + 4, col);
                m_cv.fillRect(x - 13, cy - 1, 5, 2, col);
                m_cv.fillRect(x - 12, cy - 3, 3, 6, col);
                text(x - 36, y0 - 8, 36, 9, Qt::AlignRight, QString("%1m").arg(std::max(1, int(dist))), QColor(100, 255, 140), 9, 700, false);
            }
        }
    }

    // Ammo drops on the compass
    int curTotalAmmo = 0;
    for (int w = 0; w < WPN_COUNT; ++w) if (pl.owned[w] && usesAmmo(w)) curTotalAmmo += pl.mag[w] + pl.reserve[w];
    for (const WeaponPickup &wp : m_gs.weaponPickups) {
        if (!wp.active) continue;
        if (!wp.emergency && !wp.reward && (curTotalAmmo > 0 || wp.type != PICKUP_AMMO)) continue;
        float dx = wp.x - pl.posX, dy = wp.y - pl.posY;
        float dist = std::sqrt(dx * dx + dy * dy);
        float wa = std::atan2(dy, dx) * 180.f / PI;
        float dlt = std::fmod(wa - a + 540.f, 360.f) - 180.f;
        bool pulse = (int(m_time * 6.f) & 1) == 0;
        uint32_t col = (wp.emergency && pulse) ? 0xFFFFDD33 : AMBER;
        int cy = y0 + 13;
        if (std::fabs(dlt) <= 88.f) {
            int x = cx + int(dlt * ppd);
            m_cv.blendRect(x - 6, cy - 5, 13, 11, withAlpha(0x000000, 200));
            m_cv.fillRect(x - 5, cy - 4, 11, 9, col);
            m_cv.fillRect(x - 4, cy - 3, 9, 7, 0xFF332000);
            m_cv.fillRect(x - 2, cy - 2, 4, 5, col);
            m_cv.tri(x, y0 + 2, x - 3, y0 - 2, x + 3, y0 - 2, col);
            text(x - 20, y0 - 8, 40, 9, Qt::AlignCenter, QString("%1m").arg(std::max(1, int(dist))), QColor(255, 215, 80), 9, 700, false);
        } else {
            int x = dlt < 0 ? cx - 176 : cx + 176;
            if (dlt < 0) {
                m_cv.tri(x, cy, x + 6, cy - 4, x + 6, cy + 4, col);
                m_cv.fillRect(x + 8, cy - 3, 6, 6, col);
                m_cv.fillRect(x + 9, cy - 2, 4, 4, 0xFF332000);
                text(x, y0 - 8, 36, 9, Qt::AlignLeft, QString("%1m").arg(std::max(1, int(dist))), QColor(255, 215, 80), 9, 700, false);
            } else {
                m_cv.tri(x, cy, x - 6, cy - 4, x - 6, cy + 4, col);
                m_cv.fillRect(x - 14, cy - 3, 6, 6, col);
                m_cv.fillRect(x - 13, cy - 2, 4, 4, 0xFF332000);
                text(x - 36, y0 - 8, 36, 9, Qt::AlignRight, QString("%1m").arg(std::max(1, int(dist))), QColor(255, 215, 80), 9, 700, false);
            }
        }
    }

    m_cv.tri(cx - 6, y0 - 1, cx + 6, y0 - 1, cx, y0 + 6, AMBER);
    text(cx - 40, y0 + hgt + 4, 80, 14, Qt::AlignCenter, QString::number(int(head)) + QChar(0x00B0), QColor(230, 230, 230), 11, 600);
    if (m_gameMode != MODE_MULTIPLAYER) {
        text(cx - 160, y0 + hgt + 20, 320, 16, Qt::AlignCenter,
             QString("HOSTILES REMAINING   %1      SECRETS   %2 / %3").arg(aliveEnemies())
                 .arg(m_gs.secretsFound).arg(m_gs.secretsTotal), QColor(255, 210, 140), 12, 700);
        // Every remaining enemy hidden behind an unopened secret wall: tell the player where to look
        int alive = 0, hidden = 0;
        for (const Enemy &e : m_gs.enemies) if (e.alive) { alive++; if (!reachable(int(e.x), int(e.y))) hidden++; }
        if (alive > 0 && hidden == alive)
            text(cx - 220, y0 + hgt + 36, 440, 14, Qt::AlignCenter, "LAST HOSTILES ARE IN A HIDDEN AREA  -  SEARCH WALLS WITH [E]",
                 QColor(255, 120, 90, int(190 + 60 * std::sin(m_time * 5))), 11, 800);
        else if (alive > 0 && alive <= 3)
            text(cx - 220, y0 + hgt + 36, 440, 14, Qt::AlignCenter, "LAST HOSTILES MARKED ON RADAR", QColor(255, 160, 120), 11, 700);
    }
}

void MainWindow::renderMinimap()
{
    const Player &pl = m_gs.player;
    const int MX = 86, MY = 90, MR = 66;
    const float scale = 9.f;                       // pixels per world unit
    float fx = pl.dirX, fy = pl.dirY, fl = std::sqrt(fx * fx + fy * fy); fx /= fl; fy /= fl;
    float rx = -fy, ry = fx;                       // screen-right in world space
    for (int oy = -MR; oy <= MR; oy++) for (int ox = -MR; ox <= MR; ox++) {
        int d2 = ox * ox + oy * oy;
        if (d2 > MR * MR) continue;
        float wx = pl.posX + (rx * ox - fx * oy) / scale, wy = pl.posY + (ry * ox - fy * oy) / scale;
        int cx = int(std::floor(wx)), cy = int(std::floor(wy));
        uint32_t c;
        if (cx < 0 || cy < 0 || cx >= m_mapW || cy >= m_mapH) c = withAlpha(0xFF05070A, 200);
        else if (m_map[cy][cx] == CELL_DOOR && m_doorT[cy][cx] < 0.9f) c = withAlpha(rgb(170, 120, 60), 235);
        else if (m_map[cy][cx] == CELL_SECRET && m_doorT[cy][cx] < 0.9f &&
                 (cx + 0.5f - pl.posX) * (cx + 0.5f - pl.posX) + (cy + 0.5f - pl.posY) * (cy + 0.5f - pl.posY) < 49.f)
            c = withAlpha(lerpCol(rgb(150, 156, 164), rgb(235, 200, 120), 0.18f + 0.14f * std::sin(m_time * 3.f)), 235);   // a faint shimmer
        else if (solidCell(cx, cy)) c = withAlpha(rgb(150, 156, 164), 235);
        else if (m_acid[cy][cx]) c = withAlpha(hazardColorAt(cx, cy), 220);
        else {
            int lift = int(m_plat[cy][cx] * 40.f);           // ledges lighter, pits darker
            c = withAlpha(rgb(14 + lift, 18 + lift, 22 + lift), 180);
        }
        // view cone
        if (oy < 0 && std::fabs(float(ox)) < -oy * m_planeLen) c = lerpCol(c, withAlpha(0xFFFFFFFF, R(c) > 100 ? 235 : 190), 0.09f);
        m_cv.blend(MX + ox, MY + oy, c);
    }
    m_cv.ring(MX, MY, MR, MR + 2.f, withAlpha(0xFFFFFFFF, 90));
    m_cv.ring(MX, MY, MR + 2.f, MR + 3.5f, withAlpha(0, 140));
    auto toMap = [&](float wx, float wy, float &sx, float &sy, bool clampEdge) {
        float dx = wx - pl.posX, dy = wy - pl.posY;
        sx = (dx * rx + dy * ry) * scale; sy = -(dx * fx + dy * fy) * scale;
        float d = std::sqrt(sx * sx + sy * sy);
        if (d > MR - 5) { if (!clampEdge) return false; sx *= (MR - 5) / d; sy *= (MR - 5) / d; }
        sx += MX; sy += MY; return true;
    };
    float sx, sy;
    for (const auto &k : m_gs.healthKits) if (k.active && toMap(k.x, k.y, sx, sy, k.emergency)) {
        bool pulse = (int(m_time * 5) & 1) == 0;
        uint32_t c = (k.emergency && pulse) ? 0xFF88FFAA : GREEN;
        m_cv.fillRect(int(sx) - 3, int(sy) - 1, 7, 3, c);
        m_cv.fillRect(int(sx) - 1, int(sy) - 3, 3, 7, c);
        if (k.emergency) m_cv.ring(int(sx), int(sy), 4.5f, 6.f, c);
    }
    for (const auto &p : m_gs.weaponPickups) if (p.active && toMap(p.x, p.y, sx, sy, p.emergency)) {
        bool pulse = (int(m_time * 5) & 1) == 0;
        uint32_t c = (p.emergency && pulse) ? 0xFFFFDD33 : AMBER;
        m_cv.fillCircle(int(sx), int(sy), p.emergency ? 3 : 2, c);
        if (p.emergency) m_cv.ring(int(sx), int(sy), 4.5f, 6.f, c);
    }
    bool blink = int(m_time * 4) % 2 == 0;
    for (const auto &e : m_gs.enemies) {
        if (!e.alive) continue;
        bool alert = e.state == ES_ALERT || aliveEnemies() <= 3;   // stragglers are revealed
        if (toMap(e.x, e.y, sx, sy, alert)) m_cv.fillCircle(int(sx), int(sy), alert && blink ? 4 : 3, alert ? RED : rgb(220, 120, 110));
    }
    int myTeam = 0;
    bool isMp = (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive());
    if (isMp) {
        for (const auto &s : m_net->playerSlots()) if (s.id == m_net->localPlayerId()) myTeam = s.team;
    }
    for (const Actor &a : m_gs.actors) {
        if (!a.alive) continue;
        if (toMap(a.posX, a.posY, sx, sy, true)) {
            if (isMp) {
                bool isEnemy = (a.team != myTeam);
                uint32_t dotCol = isEnemy ? RED : rgb(80, 170, 255);
                m_cv.fillCircle(int(sx), int(sy), isEnemy ? 4 : 3, dotCol);
                m_cv.ring(int(sx), int(sy), isEnemy ? 4.5f : 3.5f, isEnemy ? 6.0f : 4.5f, isEnemy ? withAlpha(RED, 220) : withAlpha(0xFFFFFFFF, 160));
            } else {
                m_cv.fillCircle(int(sx), int(sy), 3, rgb(80, 170, 255));
                m_cv.ring(int(sx), int(sy), 3.5f, 4.5f, withAlpha(0xFFFFFFFF, 160));
            }
        }
    }
    for (const Turret &t : m_gs.turrets) if (toMap(t.x, t.y, sx, sy, true)) m_cv.fillRect(int(sx) - 2, int(sy) - 2, 5, 5, rgb(96, 192, 255));
    // player arrow
    m_cv.tri(MX, MY - 7, MX - 5, MY + 5, MX + 5, MY + 5, 0xFFFFFFFF);
    m_cv.tri(MX, MY + 2, MX - 5, MY + 5, MX + 5, MY + 5, withAlpha(0, 120));
    // north marker
    float nx = -ry * MR, ny = fy * MR;   // world north (0,-1) in map space
    {
        float dx = 0, dy = -1;
        nx = (dx * rx + dy * ry) * (MR + 10); ny = -(dx * fx + dy * fy) * (MR + 10);
        text(MX + nx - 8, MY + ny - 8, 16, 16, Qt::AlignCenter, "N", QColor(255, 90, 70), 11, 800);
    }
    text(MX - 70, MY + MR + 8, 140, 14, Qt::AlignCenter, QString("%1   -   ZONE %2").arg(MAP_NAMES[m_gs.mapChoice]).arg(m_gs.level), QColor(230, 230, 230), 11, 700);
}

// Slanted HUD backing plate
static void plate(Canvas &cv, int x, int y, int w, int h, int skew, uint32_t c)
{
    cv.quad(x + skew, y, x + w, y, x + w - skew, y + h, x, y + h, c);
}

void MainWindow::renderHealthPanel()
{
    const Player &pl = m_gs.player;
    const CharProfile &cp = CHAR_PROFILES[m_charChoice];
    const int x = 16, y = H - 74, w = 262, h = 54;
    plate(m_cv, x, y, w, h, 10, withAlpha(UI_BG, 175));
    m_cv.blendRect(x, y + h, w - 10, 2, withAlpha(AMBER, 200));
    float frac = rs::clampf(float(pl.hp) / cp.hp, 0.f, 1.f);
    uint32_t col = frac > 0.5f ? GREEN : frac > 0.25f ? AMBER : RED;
    text(x + 10, y + 4, 70, 34, Qt::AlignLeft | Qt::AlignVCenter, QString::number(pl.hp), qc(col == GREEN ? 0xFFFFFFFF : col), 26, 800);
    text(x + 12, y + 36, 70, 14, Qt::AlignLeft, "HEALTH", QColor(170, 170, 170), 10, 600, false);
    // segmented, slanted bar
    const int bx = x + 82, by = y + 12, segs = 20, sw = 7, gap = 2, bh = 14;
    int lit = int(frac * segs + 0.999f);
    for (int s = 0; s < segs; s++) {
        int sx = bx + s * (sw + gap);
        uint32_t c = s < lit ? col : withAlpha(0xFFFFFFFF, 30);
        m_cv.quad(sx + 3, by, sx + sw + 3, by, sx + sw, by + bh, sx, by + bh, c);
        if (s < lit) m_cv.quad(sx + 3, by, sx + sw + 3, by, sx + sw + 2, by + 4, sx + 2, by + 4, withAlpha(0xFFFFFFFF, 70));
    }
    // stamina
    float st = pl.stamina / cp.stamina;
    uint32_t sc = m_holdBreath ? rgb(110, 220, 255) : pl.sprinting ? AMBER : rgb(80, 150, 255);
    m_cv.blendRect(bx, by + bh + 8, segs * (sw + gap), 4, withAlpha(0xFFFFFFFF, 25));
    m_cv.blendRect(bx, by + bh + 8, int(segs * (sw + gap) * st), 4, sc);
    text(bx, by + bh + 14, 200, 12, Qt::AlignLeft, QString("%1   %2").arg(cp.name).arg(DIFF_CFG[m_difficulty].name), QColor(150, 150, 150), 9, 600, false);
}

void MainWindow::renderWeaponPanel()
{
    const Player &pl = m_gs.player;
    if (pl.curWeapon < 0) return;
    const int cw = pl.curWeapon;
    const WeaponDef &wd = WEAPONS[cw];
    const Loadout &L = pl.load[cw];
    const int w = 262, h = 54, x = W - w - 16, y = H - 74;
    plate(m_cv, x, y, w, h, 10, withAlpha(UI_BG, 175));
    uint32_t rc = RARITY_COLORS[L.rarity];
    m_cv.blendRect(x + 10, y + h, w - 10, 2, withAlpha(L.rarity ? rc : AMBER, 220));
    // weapon silhouette icon, rasterized from the same polygons as the view model
    const GunModel &M = gunModel(cw);
    float sc = std::min(92.f / M.length, 36.f / std::max(1.f, M.hMax - M.hMin));
    drawGunFlat(m_cv, M, x + 18, y + 28 + (M.hMax + M.hMin) * 0.5f * sc, sc, true, withAlpha(L.rarity ? rc : 0xFFFFFFFF, 210));
    static const char *MODE[] = { "SEMI", "AUTO", "BURST", "BOLT", "CHARGE", "BEAM", "MELEE" };
    QString mode = MODE[wd.mode];
    if (m_bipod) mode = "BIPOD";
    if (isMelee(cw)) {
        text(x + 118, y + 4, 134, 30, Qt::AlignRight | Qt::AlignVCenter, "MELEE", QColor(255, 215, 80), 18, 800);
    } else if (!usesAmmo(cw)) {
        // energy weapon: heat gauge instead of rounds
        bool hot = pl.overheatT > 0.f;
        text(x + 118, y + 4, 134, 16, Qt::AlignRight, hot ? "OVERHEATED" : "HEAT", hot ? QColor(255, 90, 60) : QColor(180, 220, 255), 12, 800);
        int gw = 120, gx = x + w - 10 - gw, gy = y + 24;
        m_cv.blendRect(gx, gy, gw, 8, withAlpha(0xFFFFFFFF, 30));
        uint32_t hc = lerpCol(rgb(80, 200, 255), rgb(255, 80, 40), pl.heat);
        m_cv.fillRect(gx, gy, int(gw * pl.heat), 8, hot && int(m_time * 8) % 2 ? rgb(255, 255, 255) : hc);
    } else {
        int mag = pl.mag[cw];
        int maxMag = effectiveMagSize(cw);
        uint32_t mc = mag == 0 ? RED : mag <= maxMag / 4 ? AMBER : 0xFFFFFFFF;
        text(x + 118, y + 2, 70, 36, Qt::AlignRight | Qt::AlignVCenter, QString::number(mag), qc(mc), 26, 800);
        text(x + 192, y + 14, 60, 20, Qt::AlignLeft | Qt::AlignVCenter, QString("/ %1").arg(pl.reserve[cw]), QColor(190, 190, 190), 15, 600);
        int pips = std::min(maxMag, 50), pw = std::max(1, std::min(5, 100 / std::max(1, pips)));
        for (int i = 0; i < pips; i++) m_cv.fillRect(x + 18 + i * (pw + 1), y + h - 8, pw, 4, i < mag ? withAlpha(AMBER, 230) : withAlpha(0xFFFFFFFF, 35));
    }
    text(x + 100, y + 37, 152, 13, Qt::AlignRight, QString("%1  %2").arg(mode).arg(wd.name), L.rarity ? qc(rc) : QColor(170, 170, 170), 10, 700, false);
    // charge bar
    if (pl.charge > 0.f) {
        m_cv.blendRect(x + 10, y - 8, w - 10, 4, withAlpha(0xFFFFFFFF, 40));
        m_cv.fillRect(x + 10, y - 8, int((w - 10) * pl.charge), 4, pl.charge >= 1.f ? rgb(255, 255, 255) : rgb(90, 210, 255));
        text(x + 10, y - 24, w - 10, 14, Qt::AlignRight, pl.charge >= 1.f ? "FULL CHARGE" : "CHARGING", QColor(140, 220, 255), 11, 800);
    }
    // reload progress
    if (pl.reloadTimer > 0) {
        float p = 1.f - pl.reloadTimer / std::max(0.01f, m_reloadTotal);
        m_cv.blendRect(x + 10, y - 8, w - 10, 3, withAlpha(0xFFFFFFFF, 40));
        m_cv.blendRect(x + 10, y - 8, int((w - 10) * p), 3, AMBER);
        text(x + 10, y - 24, w - 10, 14, Qt::AlignRight, m_shellReload ? "LOADING  (FIRE TO STOP)" : "RELOADING", QColor(255, 200, 100), 11, 800);
    }
    // inventory slots 1..4
    if (pl.reloadTimer <= 0 && pl.charge <= 0.f) {
        for (int s = 0; s < SLOT_COUNT; s++) {
            int sx = x + 10 + s * 62, sy = y - 24;
            int sw = pl.slot[s];
            bool cur = sw >= 0 && sw == cw;
            m_cv.blendRect(sx, sy, 58, 14, withAlpha(cur ? AMBER : UI_BG, cur ? 230 : sw >= 0 ? 170 : 70));
            if (sw >= 0 && pl.load[sw].rarity) m_cv.fillRect(sx, sy + 12, 58, 2, RARITY_COLORS[pl.load[sw].rarity]);
            text(sx, sy, 58, 14, Qt::AlignCenter, sw >= 0 ? QString("%1 %2").arg(s + 1).arg(WEAPONS[sw].shortName) : QString("%1  -").arg(s + 1),
                 cur ? QColor(20, 20, 20) : sw >= 0 ? QColor(220, 220, 220) : QColor(90, 90, 90), 8, 800, false);
        }
    }
    // attachments on the gun in hand
    int bx = x + 10;
    for (int a = 0; a < AT_COUNT; a++) {
        int o = L.att[a];
        if (!o) continue;
        static const char *ABBR[AT_COUNT][6] = { {"", "DOT", "HOLO", "2X", "4X", "8X"}, {"", "SUPP", "COMP", "", "", ""}, {"", "XMAG", "", "", "", ""},
                                                 {"", "GRIP", "LASR", "", "", ""}, {"", "STCK", "", "", "", ""}, {"", "AP", "INC", "HP", "", ""} };
        m_cv.blendRect(bx, y + 4, 30, 9, withAlpha(0xFF3AB4E8, 200));
        text(bx, y + 3, 30, 9, Qt::AlignCenter, ABBR[a][o], QColor(255, 255, 255), 8, 700, false);
        bx += 32;
        if (bx > x + 108) break;
    }

    // Throwables + gadget, left of the weapon panel
    int tx = x - 112, ty = y + 4;
    plate(m_cv, tx, ty, 104, 46, 6, withAlpha(UI_BG, 165));
    const ThrowDef &th = THROWS[pl.throwSel];
    m_cv.fillCircle(tx + 14, ty + 12, 6, th.color);
    m_cv.ring(tx + 14, ty + 12, 5.f, 6.5f, withAlpha(0, 160));
    text(tx + 24, ty + 4, 78, 14, Qt::AlignLeft, QString("%1 x%2").arg(th.shortName).arg(pl.throwCount[pl.throwSel]),
         pl.throwCount[pl.throwSel] ? QColor(230, 230, 230) : QColor(110, 110, 110), 10, 800, false);
    text(tx + 24, ty + 15, 78, 10, Qt::AlignLeft, "G THROW  T NEXT", QColor(130, 140, 150), 7, 600, false);
    const GadgetDef &gd = GADGETS[pl.gadget];
    float cdk = pl.gadgetCd > 0.f ? pl.gadgetCd / gd.cooldown : 0.f;
    m_cv.fillCircle(tx + 14, ty + 34, 6, cdk > 0.f ? rgb(70, 74, 80) : gd.color);
    if (cdk > 0.f) m_cv.arc(tx + 14, ty + 34, 6.f, 8.f, -1.5708f, -1.5708f + 6.2832f * (1.f - cdk), gd.color);
    text(tx + 24, ty + 26, 78, 14, Qt::AlignLeft, cdk > 0.f ? QString("%1 %2s").arg(gd.shortName).arg(int(std::ceil(pl.gadgetCd))) : QString("%1 READY").arg(gd.shortName),
         cdk > 0.f ? QColor(140, 140, 140) : qc(gd.color), 9, 800, false);
    text(tx + 24, ty + 37, 78, 10, Qt::AlignLeft, "Z USE", QColor(130, 140, 150), 7, 600, false);
    // cooking: the predicted arc as projected dots (simulated with the same gravity)
    if (m_cooking && pl.throwSel != TH_MINE) {
        float vx, vy, vz; const_cast<MainWindow *>(this)->throwVelocity(vx, vy, vz);
        float x = pl.posX + pl.dirX * 0.3f, y = pl.posY + pl.dirY * 0.3f, z = pl.footZ + 0.6f;
        for (int i = 0; i < 60; i++) {
            float h = 0.04f;
            vz -= 9.8f * h; x += vx * h; y += vy * h; z += vz * h;
            if (z < groundAt(x, y)) {
                float sx, sy, d;
                if (project(x, y, groundAt(x, y) + 0.02f, sx, sy, d)) { m_cv.ring(int(sx), int(sy), 5.f, 7.f, withAlpha(AMBER, 220)); m_cv.fillCircle(int(sx), int(sy), 2, AMBER); }
                break;
            }
            float sx, sy, d;
            if (i % 2 == 0 && project(x, y, z, sx, sy, d) && d > 0.3f) m_cv.fillCircle(int(sx), int(sy), std::max(1, int(3.f / std::max(1.f, d * 0.3f))), withAlpha(0xFFFFFFFF, 200 - i * 3));
        }
    }
    // cooking a frag: fuse ring at the crosshair
    if (m_cooking) {
        float left = pl.throwSel == TH_FRAG ? 1.f - m_cookT / THROWS[TH_FRAG].fuse : 1.f;
        float ax, ay; aimPoint(ax, ay);
        m_cv.arc(int(ax), int(ay), 22.f, 25.f, -1.5708f, -1.5708f + 6.2832f * left, left < 0.3f ? RED : AMBER);
        QString throwPrompt = pl.throwSel == TH_FRAG ? QString("COOKING - RELEASE %1").arg(keyName(m_keyBinds[ACT_THROW]))
                                                     : QString("RELEASE %1 TO THROW").arg(keyName(m_keyBinds[ACT_THROW]));
        text(ax - 60, ay + 28, 120, 12, Qt::AlignCenter, throwPrompt, QColor(255, 210, 120), 9, 800, false);
    }
    // a better / different gun underfoot
    if (m_nearPickup >= 0 && m_nearPickup < int(m_gs.weaponPickups.size())) {
        const WeaponPickup &wp = m_gs.weaponPickups[size_t(m_nearPickup)];
        if (wp.active && wp.weapon >= 0) {
            int s = targetSlotFor(wp.weapon);
            int old = pl.owned[wp.weapon] ? wp.weapon : pl.slot[s];
            QString keyStr = keyName(m_keyBinds[ACT_INTERACT]);
            QString msg = old == wp.weapon ? QString("[%1]  UPGRADE TO %2 %3").arg(keyStr).arg(RARITY_NAMES[wp.load.rarity]).arg(WEAPONS[wp.weapon].name)
                                           : QString("[%1]  SWAP %2 FOR %3 %4").arg(keyStr).arg(old >= 0 ? WEAPONS[old].shortName : "").arg(RARITY_NAMES[wp.load.rarity]).arg(WEAPONS[wp.weapon].name);
            int mw = 420, mx = W / 2 - mw / 2, my = H - 150;
            plate(m_cv, mx, my, mw, 26, 6, withAlpha(UI_BG, 200));
            m_cv.fillRect(mx, my + 24, mw, 2, RARITY_COLORS[wp.load.rarity]);
            text(mx, my, mw, 26, Qt::AlignCenter, msg, qc(RARITY_COLORS[wp.load.rarity]), 11, 800);
        }
    }
    if (m_inspectT > 0.f) {
        int mw = 300, mx = W / 2 - mw / 2, my = 130;
        text(mx, my, mw, 16, Qt::AlignCenter, QString("%1  %2").arg(RARITY_NAMES[L.rarity]).arg(wd.name), qc(rc), 13, 900);
        QString st = isMelee(cw) ? QString("DMG %1   REACH %2").arg(effectiveDamage(cw)).arg(wd.reach, 0, 'f', 1)
                   : QString("DMG %1%2   RPM %3   MAG %4").arg(effectiveDamage(cw)).arg(wd.pellets > 1 ? QString(" x%1").arg(wd.pellets) : QString())
                     .arg(int(60.f / std::max(0.01f, wd.fireDelay))).arg(usesAmmo(cw) ? QString::number(effectiveMagSize(cw)) : QString("ENERGY"));
        text(mx, my + 16, mw, 14, Qt::AlignCenter, st, QColor(200, 205, 210), 10, 700, false);
    }
}

void MainWindow::renderWeaponStatsBox()
{
    if (!m_showWeaponStats) return;
    const Player &pl = m_gs.player;
    if (pl.curWeapon < 0) return;
    const int cw = pl.curWeapon;
    const WeaponDef &wd = WEAPONS[cw];
    const Loadout &L = pl.load[cw];

    const int bw = 262, bh = 172;
    const int bx = W - bw - 16;
    const int by = H - 74 - 28 - bh; // directly above inventory slot pills (sy = 502)

    uint32_t rc = L.rarity ? RARITY_COLORS[L.rarity] : AMBER;

    // Tactical card backing with rarity accent
    m_cv.blendRect(bx, by, bw, bh, withAlpha(UI_BG, 225));
    m_cv.rectOutline(bx, by, bw, bh, withAlpha(0xFFFFFFFF, 35));
    m_cv.fillRect(bx, by, bw, 2, rc);
    m_cv.fillRect(bx, by, 3, bh, rc);

    // Header: Rarity + Weapon Name, and key toggle prompt
    int ty = by + 6;
    QString title = QString("%1 %2").arg(L.rarity ? RARITY_NAMES[L.rarity] : "STANDARD").arg(wd.name).toUpper();
    text(bx + 8, ty, bw - 66, 14, Qt::AlignLeft | Qt::AlignVCenter, title, qc(rc), 11, 800, false);
    text(bx + bw - 58, ty, 50, 14, Qt::AlignRight | Qt::AlignVCenter, QString("[%1]").arg(keyName(m_keyBinds[ACT_WEAPON_STATS])), QColor(160, 175, 190), 9, 700, false);

    // Subheader: Class & Fire Mode
    static const char *CLS_NAMES[] = { "MELEE", "PISTOL", "SMG", "RIFLE", "SHOTGUN", "LMG", "SNIPER", "LAUNCHER", "ENERGY" };
    static const char *FMODE_NAMES[] = { "SEMI", "AUTO", "BURST", "BOLT", "CHARGE", "BEAM", "MELEE" };
    const char *clsName = (wd.cls >= 0 && wd.cls < 9) ? CLS_NAMES[wd.cls] : "WEAPON";
    const char *modeName = m_bipod ? "BIPOD" : (wd.mode >= 0 && wd.mode < 7) ? FMODE_NAMES[wd.mode] : "STANDARD";
    QString sub = QString("%1  |  %2").arg(clsName).arg(modeName);
    if (wd.slot == SLOT_SIDEARM) sub += "  |  SIDEARM";
    else if (wd.slot == SLOT_MELEE) sub += "  |  MELEE";
    text(bx + 8, by + 21, bw - 16, 12, Qt::AlignLeft, sub, QColor(135, 145, 155), 8, 600, false);

    // Separator line
    m_cv.blendRect(bx + 6, by + 34, bw - 12, 1, withAlpha(0xFFFFFFFF, 25));

    // Stat bars
    auto drawStatRow = [&](int ry, const char *label, float fraction, const QString &valStr, uint32_t col) {
        text(bx + 8, ry, 40, 12, Qt::AlignLeft | Qt::AlignVCenter, label, QColor(170, 180, 190), 8, 700, false);
        const int barX = bx + 52, barW = 110;
        m_cv.blendRect(barX, ry + 3, barW, 6, withAlpha(0xFFFFFFFF, 25));
        m_cv.fillRect(barX, ry + 3, std::max(2, int(barW * std::clamp(fraction, 0.f, 1.f))), 6, col);
        text(bx + 166, ry, 88, 12, Qt::AlignRight | Qt::AlignVCenter, valStr, QColor(230, 235, 240), 8, 800, false);
    };

    // 1. DAMAGE
    int dmg = effectiveDamage(cw);
    QString dmgStr;
    float dmgFrac = 0.f;
    if (wd.pellets > 1) {
        dmgStr = QString("%1x%2 (%3)").arg(dmg).arg(wd.pellets).arg(dmg * wd.pellets);
        dmgFrac = std::clamp(float(dmg * wd.pellets) / 160.f, 0.05f, 1.f);
    } else {
        dmgStr = QString::number(dmg);
        dmgFrac = std::clamp(float(dmg) / 120.f, 0.05f, 1.f);
    }
    drawStatRow(by + 38, "DMG", dmgFrac, dmgStr, rgb(255, 85, 75));

    // 2. FIRE RATE
    QString rofStr;
    float rofFrac = 0.f;
    if (isMelee(cw)) {
        float swings = 1.f / std::max(0.01f, effectiveFireDelay(cw));
        rofStr = QString("%1/s").arg(swings, 0, 'f', 1);
        rofFrac = std::clamp(swings / 3.5f, 0.05f, 1.f);
    } else {
        int rpm = int(60.f / std::max(0.01f, effectiveFireDelay(cw)));
        rofStr = QString("%1 RPM").arg(rpm);
        rofFrac = std::clamp(float(rpm) / 950.f, 0.05f, 1.f);
    }
    drawStatRow(by + 51, "RATE", rofFrac, rofStr, rgb(255, 180, 50));

    // 3. ACCURACY
    float sp = effectiveSpread(cw);
    int acc = std::clamp(int((1.0f - sp / 0.075f) * 100.f), 10, 100);
    drawStatRow(by + 64, "ACC", float(acc) / 100.f, QString("%1%").arg(acc), rgb(75, 215, 125));

    // 4. RECOIL
    float rec = effectiveRecoil(cw);
    int ctrl = std::clamp(int((1.0f - rec / 10.f) * 100.f), 10, 100);
    drawStatRow(by + 77, "RECL", float(ctrl) / 100.f, QString("%1%").arg(ctrl), rgb(65, 180, 245));

    // 5. CAPACITY
    QString capStr;
    float capFrac = 0.f;
    if (usesAmmo(cw)) {
        int mag = effectiveMagSize(cw);
        capStr = QString("%1 RDS").arg(mag);
        capFrac = std::clamp(float(mag) / 80.f, 0.05f, 1.f);
    } else if (isMelee(cw)) {
        capStr = "MELEE";
        capFrac = 1.f;
    } else {
        capStr = QString("%1% HEAT").arg(int(wd.heatPerShot * 100.f));
        capFrac = std::clamp(1.f - wd.heatPerShot, 0.05f, 1.f);
    }
    drawStatRow(by + 90, "CAP", capFrac, capStr, rgb(175, 135, 245));

    // Separator line
    m_cv.blendRect(bx + 6, by + 105, bw - 12, 1, withAlpha(0xFFFFFFFF, 25));

    // Combat Perks / Details
    float pen = effectivePenetration(cw);
    QString perks = QString("CRIT x%1").arg(wd.headMult, 0, 'f', 1);
    if (usesAmmo(cw)) perks += QString("  |  RLD %1s").arg(wd.reloadTime, 0, 'f', 1);
    if (pen > 0.05f) perks += QString("  |  PEN %1%").arg(int(pen * 100.f));
    if (effectiveScope(cw)) perks += "  |  SCOPED";
    if (wd.status == ST_BURN) perks += "  |  INCENDIARY";
    else if (wd.status == ST_SHOCK) perks += "  |  SHOCK";
    else if (wd.status == ST_STUN) perks += "  |  STUN";
    if (isMelee(cw)) perks += QString("  |  REACH %1m").arg(wd.reach, 0, 'f', 1);
    text(bx + 8, by + 109, bw - 16, 12, Qt::AlignLeft, perks, QColor(255, 210, 110), 8, 700, false);

    // Attachments
    QString mods;
    for (int a = 0; a < AT_COUNT; a++) {
        int o = L.att[a];
        if (o > 0 && o < ATT_OPTIONS[a]) {
            if (!mods.isEmpty()) mods += ", ";
            mods += ATT_NAMES[a][o];
        }
    }
    if (mods.isEmpty()) mods = "NONE (ARMORY [U])";
    text(bx + 8, by + 123, bw - 16, 12, Qt::AlignLeft, QString("MODS: %1").arg(mods), QColor(120, 205, 245), 7, 700, false);

    // Ammo / Reserve
    QString ammoInfo;
    if (usesAmmo(cw)) {
        ammoInfo = QString("RESERVE: %1 / %2 MAX").arg(pl.reserve[cw]).arg(wd.maxReserve);
    } else if (!isMelee(cw)) {
        ammoInfo = QString("HEAT: %1% (RATE %2/SHOT)").arg(int(pl.heat * 100.f)).arg(int(wd.heatPerShot * 100.f));
    } else {
        ammoInfo = "STRIKE WEAPON - NO AMMO REQUIRED";
    }
    text(bx + 8, by + 137, bw - 16, 12, Qt::AlignLeft, ammoInfo, QColor(165, 175, 185), 7, 600, false);

    // Footer
    m_cv.blendRect(bx + 6, by + 151, bw - 12, 1, withAlpha(0xFFFFFFFF, 25));
    text(bx + 8, by + 154, bw - 16, 12, Qt::AlignCenter,
         QString("[%1] TOGGLE STATS DISPLAY").arg(keyName(m_keyBinds[ACT_WEAPON_STATS])),
         QColor(150, 160, 170), 8, 700, false);
}

void MainWindow::renderCrosshair()
{
    const Player &pl = m_gs.player;
    if (m_adsT > 0.35f || m_gs.gameOver) return;
    int cx = int(m_aimX), cy = int(m_aimY);
    if (pl.curWeapon < 0) return;
    const WeaponDef &wd = WEAPONS[pl.curWeapon];

    if (isMelee(pl.curWeapon)) {
        bool inRange = (m_probeEnemy >= 0 && m_probeDepth <= wd.reach);
        int gap = inRange ? 7 : 13;
        uint32_t col = inRange ? rgb(255, 50, 40) : 0xFFFFFFFF;
        m_cv.brackets(cx - gap, cy - gap, gap * 2 + 1, gap * 2 + 1, 4, withAlpha(col, inRange ? 255 : 180), 2);
        m_cv.fillRect(cx - 1, cy - 1, 2, 2, col);
        if (inRange) {
            text(cx - 30, cy + gap + 4, 60, 12, Qt::AlignCenter, "READY", QColor(255, 60, 50), 9, 800, false);
        }
        return;
    }

    bool moving = isPlayerMoving();
    float spread = effectiveSpread(pl.curWeapon) * (moving ? 1.8f : 1.f) * (inAir() ? 2.5f : 1.f) * (1.f + m_bloom * 0.5f) * W;
    int gap = int(5 + spread * 0.9f + m_kick * 10 + (pl.sprinting ? 10 : 0));
    const int len = 9;
    bool enemy = m_probeEnemy >= 0;
    uint32_t col = enemy ? (m_probePart == PART_HEAD ? rgb(255, 60, 50) : rgb(255, 120, 90)) : crosshairColor();
    auto bar = [&](int x, int y, int w, int h) { m_cv.blendRect(x - 1, y - 1, w + 2, h + 2, withAlpha(0, 150)); m_cv.fillRect(x, y, w, h, col); };
    if (wd.cls == WC_SHOTGUN || wd.proj == PJ_FLAME) {
        m_cv.ring(cx, cy, gap + 3.f, gap + 5.f, withAlpha(0, 120));
        m_cv.ring(cx, cy, gap + 3.5f, gap + 4.5f, col);
    } else {
        bar(cx - gap - len, cy - 1, len, 2); bar(cx + gap + 1, cy - 1, len, 2);
        bar(cx - 1, cy - gap - len, 2, len); bar(cx - 1, cy + gap + 1, 2, len);
    }
    bar(cx - 1, cy - 1, 2, 2);
    if (enemy && m_probePart == PART_HEAD) m_cv.ring(cx, cy, 4.f, 5.f, withAlpha(col, 200));
    // Free-aim: show the dead-zone corners as the crosshair approaches them
    if (m_freeAim) {
        float ex = std::fabs(m_aimX - W / 2.f) / 170.f, ey = std::fabs(m_aimY - H / 2.f) / 120.f;
        float k = std::max(ex, ey);
        if (k > 0.6f) {
            int a = int(120 * std::min(1.f, (k - 0.6f) / 0.4f));
            m_cv.brackets(W / 2 - 170 - 6, H / 2 - 120 - 6, 340 + 12, 240 + 12, 14, withAlpha(0xFFFFFFFF, a), 1);
        }
    }
}

void MainWindow::renderHitMarkers()
{
    float ax, ay; aimPoint(ax, ay);
    int cx = int(ax), cy = int(ay);
    auto marker = [&](float t, int r0, int r1, uint32_t c, int th) {
        uint32_t cc = withAlpha(c, int(255 * std::min(1.f, t)));
        for (int s = 0; s < 4; s++) {
            int sx = (s & 1) ? 1 : -1, sy = (s & 2) ? 1 : -1;
            m_cv.thickLine(cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1, cc, th);
        }
    };
    if (m_killMarker > 0) { float t = m_killMarker / 0.5f; marker(t * 2, int(8 + (1 - t) * 6), int(18 + (1 - t) * 6), RED, 3); }
    else if (m_headMarker > 0) marker(m_headMarker / 0.35f * 2, 7, 16, rgb(255, 70, 50), 3);
    else if (m_hitMarker > 0) marker(m_hitMarker / 0.25f * 2, 6, 13, 0xFFFFFFFF, 2);
}

// Squad name tags + health over their heads (depth-tested against the world)
void MainWindow::renderSquadTags()
{
    bool isMp = (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive());
    int myTeam = 0;
    if (isMp) {
        for (const auto &s : m_net->playerSlots()) if (s.id == m_net->localPlayerId()) myTeam = s.team;
    }

    for (const Actor &a : m_gs.actors) {
        if (!a.alive) continue;
        float sx, sy, d;
        if (!project(a.posX, a.posY, a.footZ + 1.05f, sx, sy, d) || d < 0.5f || d > 40.f) continue;
        bool visible = d < m_depth[size_t(clampi(int(sy), 0, H - 1)) * W + clampi(int(sx), 0, W - 1)] + 0.5f;
        int alpha = visible ? 230 : 110;

        if (isMp) {
            bool isEnemy = (a.team != myTeam);
            QColor tagCol = isEnemy ? QColor(255, 75, 75, alpha) : QColor(110, 190, 255, alpha);
            QString label = isEnemy ? QString("%1 [ENEMY]").arg(a.name) : QString("%1 [ALLY]").arg(a.name);
            text(sx - 50, sy - 16, 100, 12, Qt::AlignCenter, label, tagCol, 9, 800);

            float frac = a.hp / 120.f;
            m_cv.blendRect(int(sx) - 16, int(sy) - 3, 32, 4, withAlpha(0, alpha / 2));
            uint32_t barCol = isEnemy ? RED : rgb(90, 180, 255);
            m_cv.blendRect(int(sx) - 16, int(sy) - 3, int(32 * clampf(frac, 0.f, 1.f)), 4, withAlpha(barCol, alpha));
        } else {
            text(sx - 40, sy - 14, 80, 12, Qt::AlignCenter, a.name, QColor(110, 190, 255, alpha), 9, 800);
            float frac = a.hp / 120.f;
            m_cv.blendRect(int(sx) - 16, int(sy) - 3, 32, 4, withAlpha(0, alpha / 2));
            m_cv.blendRect(int(sx) - 16, int(sy) - 3, int(32 * clampf(frac, 0.f, 1.f)), 4, withAlpha(rgb(90, 180, 255), alpha));
        }
    }
}

void MainWindow::renderDamageDirection()
{
    const Player &pl = m_gs.player;
    float facing = std::atan2(pl.dirY, pl.dirX);
    for (const DamageMark &dm : m_damageMarks) {
        float t = std::min(1.f, dm.t / 1.2f);
        // angle to the attacker recomputed every frame: the marker swings as you turn
        float rel = std::atan2(dm.y - pl.posY, dm.x - pl.posX) - facing;
        float sa = rel - PI / 2;
        float wdt = 0.22f + 0.18f * dm.strength;
        m_cv.arc(W / 2, H / 2, 116, 128, sa - wdt, sa + wdt, withAlpha(RED, int(210 * t)));
        m_cv.arc(W / 2, H / 2, 110, 115, sa - wdt * 0.55f, sa + wdt * 0.55f, withAlpha(rgb(255, 150, 120), int(170 * t)));
        // chevron pointing outward
        float cx = W / 2 + std::cos(sa) * 136, cy = H / 2 + std::sin(sa) * 136;
        float px = -std::sin(sa), py = std::cos(sa);
        m_cv.tri(cx + std::cos(sa) * 9, cy + std::sin(sa) * 9, cx + px * 7, cy + py * 7, cx - px * 7, cy - py * 7, withAlpha(RED, int(220 * t)));
    }
}

void MainWindow::renderTargetInfo()
{
    if (m_probeEnemy < 0) return;
    const Enemy &e = m_gs.enemies[m_probeEnemy];
    if (!e.alive) return;
    const int w = 200, x = W / 2 - w / 2, y = 84;
    float frac = float(e.hp) / float(std::max(1, e.maxHp));
    m_cv.blendRect(x, y, w, 26, withAlpha(UI_BG, 160));
    m_cv.blendRect(x + 8, y + 17, w - 16, 4, withAlpha(0xFFFFFFFF, 30));
    m_cv.blendRect(x + 8, y + 17, int((w - 16) * frac), 4, frac > 0.5f ? GREEN : frac > 0.25f ? AMBER : RED);
    const char *typeStr = enemyTypeName(e);
    text(x + 8, y + 2, w - 16, 14, Qt::AlignLeft, typeStr, e.elite ? QColor(255, 60, 50) : e.type == ET_SNIPER ? QColor(255, 90, 70) : QColor(255, 160, 90), 11, 800, false);
    text(x + 8, y + 2, w - 16, 14, Qt::AlignRight, m_probePart == PART_HEAD ? QString("HEAD  x%1").arg(WEAPONS[m_gs.player.curWeapon].headMult, 0, 'f', 1)
                                                   : m_probePart == PART_LIMB ? "LIMB" : "BODY",
         m_probePart == PART_HEAD ? QColor(255, 80, 60) : QColor(200, 200, 200), 11, 700, false);
}

void MainWindow::renderKillFeed()
{
    text(W - 220, 8, 204, 18, Qt::AlignRight, QString("SCORE  %1").arg(m_gs.score), QColor(255, 210, 140), 15, 800);
    if (m_showFps) text(W - 220, 26, 204, 14, Qt::AlignRight, QString("%1 FPS").arg(int(m_fps)), QColor(140, 140, 140), 10, 500, false);
    // coins
    m_cv.fillCircle(W - 150, 33, 5, rgb(255, 200, 60)); m_cv.ring(W - 150, 33, 3.f, 4.f, rgb(200, 140, 30));
    text(W - 142, 26, 70, 14, Qt::AlignLeft, QString::number(m_gs.coins), QColor(255, 215, 110), 10, 800, false);
    int y = 46;
    for (const auto &kf : m_killFeed) {
        float t = std::min(1.f, kf.life / 0.5f);
        int a = int(255 * t);
        m_cv.blendRect(W - 226, y, 210, 22, withAlpha(UI_BG, int(170 * t)));
        m_cv.blendRect(W - 226, y, 3, 22, withAlpha(kf.head ? RED : AMBER, a));
        text(W - 218, y, 200, 22, Qt::AlignLeft | Qt::AlignVCenter, kf.text.size() > 33 ? kf.text.left(31) + ".." : kf.text, kf.head ? QColor(255, 120, 100, a) : QColor(240, 240, 240, a), 12, 700, false);
        y += 25;
    }
}

// Zone report: checklist rows tick in one by one, coins and score count up
void MainWindow::renderZoneReport()
{
    float fade = clampf(m_intermissionT / 0.6f, 0.f, 1.f);
    float ease = fade * fade * (3 - 2 * fade);
    int ta = int(255 * ease);
    m_cv.blendRect(0, 0, W, H, withAlpha(0x06090D, int(228 * ease)));
    m_cv.blendRect(0, 14, W, 74, withAlpha(UI_BG, int(200 * ease)));
    m_cv.blendRect(0, 14, W, 2, withAlpha(AMBER, ta));
    m_cv.blendRect(0, 88, W, 2, withAlpha(AMBER, ta));
    text(0, 18, W, 34, Qt::AlignCenter, QString("ZONE %1 CLEARED").arg(m_gs.level), QColor(255, 215, 80, ta), 30, 900);
    text(0, 54, W, 20, Qt::AlignCenter, QString("ALL HOSTILES ELIMINATED      ZONE BONUS +%1      SCORE %2").arg(500 * m_gs.level).arg(m_gs.score),
         QColor(220, 225, 230, ta), 12, 700);
    text(0, 94, W, 18, Qt::AlignCenter, "ZONE REPORT  -  BONUS CHECKLIST", QColor(255, 220, 90, ta), 12, 800, false);

    const int px = 86, pw = W - 172, rowH = 34, y0 = 120;
    int n = int(m_report.size());
    m_cv.blendRect(px, y0 - 4, pw, n * rowH + 8, withAlpha(UI_BG, int(200 * ease)));
    m_cv.rectOutline(px, y0 - 4, pw, n * rowH + 8, withAlpha(0xFFFFFFFF, int(30 * ease)));
    float lastReveal = 0.6f + 0.3f * n;
    for (int i = 0; i < n; i++) {
        const ReportItem &it = m_report[i];
        float rt = 0.6f + 0.3f * i;
        if (m_intermissionT < rt) break;
        int a = int(255 * clampf((m_intermissionT - rt) / 0.25f, 0.f, 1.f));
        int y = y0 + i * rowH;
        if (i % 2) m_cv.blendRect(px + 2, y, pw - 4, rowH - 2, withAlpha(0xFFFFFFFF, a / 24));
        // checkbox: green tick or grey cross, drawn with thick Bresenham strokes
        int bx = px + 14, by = y + 7;
        if (it.done) {
            m_cv.blendRect(bx, by, 20, 20, withAlpha(0xFF1D6638, a));
            m_cv.thickLine(bx + 4, by + 10, bx + 8, by + 15, withAlpha(0xFF7CFFA8, a), 3);
            m_cv.thickLine(bx + 8, by + 15, bx + 16, by + 5, withAlpha(0xFF7CFFA8, a), 3);
        } else {
            m_cv.blendRect(bx, by, 20, 20, withAlpha(0xFF2A2F36, a));
            m_cv.thickLine(bx + 5, by + 5, bx + 15, by + 15, withAlpha(0xFF8A8F96, a), 2);
            m_cv.thickLine(bx + 15, by + 5, bx + 5, by + 15, withAlpha(0xFF8A8F96, a), 2);
        }
        text(px + 46, y + 3, 300, 16, Qt::AlignLeft, it.label, it.done ? QColor(240, 240, 240, a) : QColor(150, 155, 160, a), 13, 800, false);
        text(px + 46, y + 19, 380, 12, Qt::AlignLeft, it.detail, QColor(150, 160, 170, a), 10, 600, false);
        int cxr = px + pw - 190;
        if (it.coins > 0) {
            m_cv.fillCircle(cxr + 8, y + 16, 6, withAlpha(rgb(255, 200, 60), a));
            m_cv.ring(cxr + 8, y + 16, 3.5f, 5.f, withAlpha(rgb(200, 140, 30), a));
        }
        text(cxr + 20, y + 8, 70, 16, Qt::AlignLeft, it.coins > 0 ? QString("+%1").arg(it.coins) : QString("--"),
             it.coins > 0 ? QColor(255, 215, 110, a) : QColor(110, 115, 120, a), 13, 800, false);
        text(cxr + 90, y + 8, 90, 16, Qt::AlignRight, it.score > 0 ? QString("+%1 PTS").arg(it.score) : QString(""),
             QColor(140, 220, 255, a), 11, 700, false);
    }
    // Totals count up once every row is in
    float k = clampf((m_intermissionT - lastReveal) / 0.6f, 0.f, 1.f);
    int ty = y0 + n * rowH + 12;
    int shownCoins = int(m_reportCoins * k), shownScore = int(m_reportScore * k);
    m_cv.fillCircle(W / 2 - 150, ty + 10, 7, withAlpha(rgb(255, 200, 60), ta));
    m_cv.ring(W / 2 - 150, ty + 10, 4.f, 6.f, withAlpha(rgb(200, 140, 30), ta));
    text(W / 2 - 138, ty, 300, 20, Qt::AlignLeft | Qt::AlignVCenter,
         QString("+%1 COINS    (WALLET %2)").arg(shownCoins).arg(m_gs.coins - m_reportCoins + shownCoins), QColor(255, 215, 110, ta), 15, 900);
    text(0, ty + 22, W, 16, Qt::AlignCenter, QString("BONUS SCORE  +%1").arg(shownScore), QColor(140, 220, 255, ta), 12, 800, false);

    QRect dr = ixDeployRect();
    bool done = m_intermissionT >= lastReveal + 0.6f;
    m_cv.blendRect(dr.x(), dr.y(), dr.width(), dr.height(), withAlpha(done ? 0xFF1D6638 : 0xFF1B2635, 235));
    m_cv.rectOutline(dr.x(), dr.y(), dr.width(), dr.height(), done ? 0xFF45F07E : withAlpha(0xFFFFFFFF, 60), done ? 2 : 1);
    text(dr.x(), dr.y(), dr.width(), dr.height(), Qt::AlignCenter, done ? "CONTINUE  >>   [ENTER]" : "[ENTER]  SKIP",
         done ? QColor(255, 255, 255) : QColor(170, 175, 180), 13, 900);
    text(0, 502, W, 16, Qt::AlignCenter, "Next: choose an upgrade, your weapon and spend coins in the field shop",
         QColor(170, 175, 180, ta), 11, 600, false);
}

void MainWindow::renderLevelBanner()
{
    if (!m_gs.levelComplete) return;
    if (m_ixPage == 0) { renderZoneReport(); return; }
    const Player &pl = m_gs.player;
    // Fade in over 0.6 s; the cards slide up into place
    float fade = clampf(m_intermissionT / 0.6f, 0.f, 1.f);
    float ease = fade * fade * (3 - 2 * fade);
    int slide = int((1.f - ease) * 26.f);
    bool ready = m_intermissionT >= 0.9f;                     // input accepted from here

    m_cv.blendRect(0, 0, W, H, withAlpha(0x06090D, int(228 * ease)));

    // Header
    m_cv.blendRect(0, 14, W, 74, withAlpha(UI_BG, int(200 * ease)));
    m_cv.blendRect(0, 14, W, 2, withAlpha(AMBER, int(255 * ease)));
    m_cv.blendRect(0, 88, W, 2, withAlpha(AMBER, int(255 * ease)));
    int ta = int(255 * ease);
    text(0, 18, W, 34, Qt::AlignCenter, QString("ZONE %1 CLEARED").arg(m_gs.level), QColor(255, 215, 80, ta), 30, 900);
    text(0, 54, W, 20, Qt::AlignCenter,
         QString("ZONE CLEAR BONUS +%1      SCORE %2      HEADSHOTS %3").arg(500 * m_gs.level).arg(m_gs.score).arg(m_gs.headshots),
         QColor(220, 225, 230, ta), 12, 700);

    // Step indicator: 1 upgrade  ->  2 weapon  ->  3 deploy
    {
        bool s1 = m_selectedUpgradeCard >= 0;
        const char *steps[3] = { "1  CHOOSE UPGRADE", "2  CHOOSE WEAPON", "3  DEPLOY" };
        for (int i = 0; i < 3; i++) {
            bool done = (i == 0 && s1) || (i == 1 && s1);
            bool cur = (i == 0 && !s1) || (i == 2 && s1);
            QColor c = done ? QColor(100, 255, 140, ta) : cur ? QColor(255, 220, 90, ta) : QColor(150, 155, 160, ta);
            text(W / 2 - 300 + i * 200, 94, 200, 18, Qt::AlignCenter, steps[i], c, 12, 800, false);
        }
    }

    // Upgrade cards
    for (int i = 0; i < int(m_upgradeCards.size()) && i < 3; i++) {
        const UpgradeCard &card = m_upgradeCards[i];
        QRect rc = ixCardRect(i);
        int cx = rc.x(), cy = rc.y() + slide, cw = rc.width(), ch = rc.height();
        bool selected = (m_selectedUpgradeCard == i);
        bool hover = ready && m_hoverCard == i && !selected;

        panel(cx, cy, cw, ch, selected, selected ? GREEN : 0xFF354050);
        if (hover) m_cv.rectOutline(cx - 2, cy - 2, cw + 4, ch + 4, withAlpha(AMBER, 200), 2);

        uint32_t keyBtnCol = selected ? GREEN : AMBER;
        m_cv.fillRect(cx + 12, cy + 12, 42, 22, keyBtnCol);
        text(cx + 12, cy + 12, 42, 22, Qt::AlignCenter, QString("[%1]").arg(i + 1), QColor(15, 15, 15), 12, 900, false);
        if (selected) {
            m_cv.blendRect(cx + cw - 95, cy + 12, 85, 22, withAlpha(GREEN, 220));
            text(cx + cw - 95, cy + 12, 85, 22, Qt::AlignCenter, "SELECTED", QColor(10, 10, 10), 11, 800, false);
        }
        if (card.weapon >= 0 && card.weapon < WPN_COUNT) {
            const GunModel &M = gunModel(card.weapon);
            float sc = std::min(90.f / M.length, 40.f / std::max(1.f, M.hMax - M.hMin));
            uint32_t tint = card.slot == UC_UNLOCK && card.option > 0 ? RARITY_COLORS[card.option] : 0xFFFFFFFF;
            drawGunFlat(m_cv, M, cx + cw / 2 - M.length * sc * 0.5f, cy + 62 + (M.hMax + M.hMin) * 0.5f * sc, sc, true, withAlpha(tint, selected || hover ? 240 : 160));
        }
        text(cx + 10, cy + 92, cw - 20, 26, Qt::AlignCenter, card.title, selected ? QColor(255, 225, 90) : QColor(240, 240, 240), 13, 800);
        m_cv.blendRect(cx + 10, cy + 122, cw - 20, 70, withAlpha(0, 130));
        text(cx + 14, cy + 126, cw - 28, 62, Qt::AlignLeft | Qt::TextWordWrap, card.desc, QColor(185, 195, 205), 11, 500, false);
        m_cv.blendRect(cx + 8, cy + ch - 50, cw - 16, 40, withAlpha(selected ? 0xFF0E3020 : 0xFF141F2C, 220));
        m_cv.blendRect(cx + 8, cy + ch - 50, cw - 16, 2, withAlpha(selected ? GREEN : 0xFF3AB4E8, 220));
        text(cx + 10, cy + ch - 46, cw - 20, 34, Qt::AlignCenter | Qt::TextWordWrap, card.bonus, selected ? QColor(100, 255, 140) : QColor(80, 210, 255), 11, 700, false);
    }

    // Loadout: which weapon to deploy with
    std::vector<int> choices = loadoutChoices();
    text(0, 374, W, 16, Qt::AlignCenter, "DEPLOY WITH   ( TAB / ARROWS OR CLICK )", QColor(200, 205, 210, ta), 11, 800, false);
    for (int s = 0; s < int(choices.size()); s++) {
        int wpn = choices[s];
        QRect rc = ixChipRect(s, int(choices.size()));
        int x = rc.x(), y = rc.y() + slide, w = rc.width(), h = rc.height();
        bool sel = wpn == m_loadoutWeapon, hover = ready && m_hoverChip == s && !sel;
        m_cv.blendRect(x, y, w, h, withAlpha(sel ? 0xFF2A2210 : UI_BG, sel ? 235 : 200));
        m_cv.rectOutline(x, y, w, h, sel ? AMBER : hover ? withAlpha(AMBER, 160) : withAlpha(0xFFFFFFFF, 40), sel ? 2 : 1);
        const GunModel &M = gunModel(wpn);
        float sc = std::min(44.f / M.length, 30.f / (M.hMax - M.hMin));   // fit both width and height
        drawGunFlat(m_cv, M, x + 8 + (44.f - M.length * sc) * 0.5f, y + h * 0.5f + (M.hMax + M.hMin) * 0.5f * sc, sc, true, withAlpha(0xFFFFFFFF, sel ? 235 : 150));
        text(x + 56, y + 6, w - 60, 16, Qt::AlignLeft, pl.owned[wpn] ? QString(WEAPONS[wpn].shortName) : QString("%1 (NEW)").arg(WEAPONS[wpn].shortName),
             sel ? QColor(255, 215, 80) : QColor(225, 225, 225), 12, 800, false);
        QString stat = isMelee(wpn) ? QString("DMG %1  MELEE").arg(effectiveDamage(wpn)) : !usesAmmo(wpn) ? QString("DMG %1  ENERGY").arg(effectiveDamage(wpn))
                                        : QString("DMG %1  MAG %2").arg(effectiveDamage(wpn)).arg(effectiveMagSize(wpn));
        text(x + 56, y + 24, w - 60, 14, Qt::AlignLeft, stat, QColor(160, 170, 180), 9, 600, false);
        if (usesAmmo(wpn))
            text(x + 56, y + 35, w - 60, 12, Qt::AlignLeft, QString("RESERVE %1").arg(pl.reserve[wpn]), QColor(130, 140, 150), 9, 600, false);
    }

    // Deploy button (enabled once an upgrade is chosen)
    QRect dr = ixDeployRect();
    bool canDeploy = m_selectedUpgradeCard >= 0;
    int bx = dr.x(), by = dr.y() + slide, bw = dr.width(), bh = dr.height();
    if (canDeploy) {
        bool pulse = (int(m_time * 3.f) & 1) == 0;
        m_cv.blendRect(bx, by, bw, bh, withAlpha(pulse || m_hoverDeploy ? 0xFF1D6638 : 0xFF144D29, 240));
        m_cv.rectOutline(bx, by, bw, bh, 0xFF45F07E, 2);
        text(bx, by, bw, bh, Qt::AlignCenter, "DEPLOY  >>   [ENTER]", QColor(255, 255, 255), 14, 900);
    } else {
        m_cv.blendRect(bx, by, bw, bh, withAlpha(0xFF1B2635, 220));
        m_cv.rectOutline(bx, by, bw, bh, withAlpha(0xFFFFFFFF, 50), 1);
        text(bx, by, bw, bh, Qt::AlignCenter, "PICK AN UPGRADE FIRST", QColor(150, 155, 160), 12, 800);
    }
    text(0, 500, W, 16, Qt::AlignCenter, "[1] [2] [3] or click a card      TAB weapon      ENTER deploy",
         QColor(200, 205, 210, ta), 11, 600, false);
    text(0, 516, W, 14, Qt::AlignCenter, QString("Take your time  -  auto-deploy in %1 s").arg(std::max(1, int(m_upgradeDecisionTimer) + 1)),
         QColor(130, 140, 150, ta), 10, 600, false);

    // Field shop: spend coins earned from the zone report
    m_cv.fillCircle(W / 2 + 12, 539, 5, rgb(255, 200, 60)); m_cv.ring(W / 2 + 12, 539, 3.f, 4.f, rgb(200, 140, 30));
    text(W / 2 - 160, 532, 320, 16, Qt::AlignCenter, QString("FIELD SHOP     %1 COINS").arg(m_gs.coins), QColor(255, 215, 110, ta), 11, 800, false);
    static const char *items[3] = { "[7] AMMO + FRAG  40", "[8] FIELD MEDKIT  35", "[9] REROLL CARDS  25" };
    static const int cost[3] = { 40, 35, 25 };
    for (int i = 0; i < 3; i++) {
        QRect sr = ixShopRect(i);
        bool afford = m_gs.coins >= cost[i];
        m_cv.blendRect(sr.x(), sr.y(), sr.width(), sr.height(), withAlpha(afford ? 0xFF1E2A1A : 0xFF1B1F25, 230));
        m_cv.rectOutline(sr.x(), sr.y(), sr.width(), sr.height(), afford ? withAlpha(AMBER, 200) : withAlpha(0xFFFFFFFF, 40), 1);
        text(sr.x(), sr.y(), sr.width(), sr.height(), Qt::AlignCenter, items[i], afford ? QColor(255, 225, 150) : QColor(120, 125, 130), 10, 800, false);
    }
    if (m_ixMsgT > 0)
        text(0, 582, W, 14, Qt::AlignCenter, m_ixMsg, QColor(140, 255, 160, int(255 * std::min(1.f, m_ixMsgT))), 11, 800, false);
}

void MainWindow::renderArmoryOverlay()
{
    const Player &pl = m_gs.player;
    // Dim background
    for (int i = 0; i < W * H; i++) {
        uint32_t c = m_pixels[i];
        int l = (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 9;
        m_pixels[i] = rgb(l, l + 2, l + 5);
    }

    const int w = 560, h = 420, x = W / 2 - w / 2, y = H / 2 - h / 2;
    panel(x, y, w, h, true, AMBER);

    text(x, y + 14, w, 32, Qt::AlignCenter, "FIELD ARMORY & ATTACHMENTS", QColor(255, 215, 80), 22, 900);
    text(x, y + 44, w, 16, Qt::AlignCenter, QString("ZONE %1 LOADOUT  -  THROWABLES %2/%3/%4/%5/%6  -  GADGET %7").arg(m_gs.level)
         .arg(pl.throwCount[0]).arg(pl.throwCount[1]).arg(pl.throwCount[2]).arg(pl.throwCount[3]).arg(pl.throwCount[4]).arg(GADGETS[pl.gadget].name), QColor(180, 190, 200), 10, 600);

    static const char *SLOT_NAMES[SLOT_COUNT] = { "MELEE", "SIDEARM", "PRIMARY", "PRIMARY 2" };
    for (int si = 0; si < SLOT_COUNT; si++) {
        int ry = y + 70 + si * 76;
        int wi = pl.slot[si];
        bool has = wi >= 0;
        uint32_t rcol = has ? RARITY_COLORS[pl.load[wi].rarity] : 0xFF555555;
        m_cv.blendRect(x + 20, ry, w - 40, 68, withAlpha(0xFFFFFFFF, has ? 16 : 6));
        m_cv.blendRect(x + 20, ry, 4, 68, withAlpha(has && wi == pl.curWeapon ? AMBER : rcol, 240));
        text(x + 32, ry + 4, 160, 12, Qt::AlignLeft, QString("[%1] %2").arg(si + 1).arg(SLOT_NAMES[si]), QColor(140, 150, 160), 9, 800, false);
        if (!has) { text(x + 32, ry + 20, 300, 18, Qt::AlignLeft, "EMPTY - pick up a gun to fill it", QColor(110, 110, 110), 11, 600, false); continue; }
        const WeaponDef &d = WEAPONS[wi];
        text(x + 32, ry + 16, 170, 18, Qt::AlignLeft, d.name, qc(rcol), 12, 800);
        text(x + 32, ry + 34, 170, 14, Qt::AlignLeft, RARITY_NAMES[pl.load[wi].rarity], qc(rcol), 9, 700, false);
        text(x + 32, ry + 48, 170, 14, Qt::AlignLeft,
             isMelee(wi) ? QString("DMG %1  REACH %2").arg(effectiveDamage(wi)).arg(d.reach, 0, 'f', 1)
                         : QString("DMG %1  MAG %2  RCL %3").arg(effectiveDamage(wi)).arg(usesAmmo(wi) ? QString::number(effectiveMagSize(wi)) : QString("--")).arg(effectiveRecoil(wi), 0, 'f', 2),
             QColor(200, 210, 220), 9, 600, false);
        // attachment slots: what's fitted
        int ax = x + 206;
        for (int a = 0; a < AT_COUNT; a++) {
            int bx = ax + (a % 3) * 106, by = ry + 6 + (a / 3) * 31;
            bool fits = false;
            for (int o = 1; o < ATT_OPTIONS[a]; o++) fits |= attachmentFits(wi, a, o);
            int o = pl.load[wi].att[a];
            m_cv.blendRect(bx, by, 100, 27, withAlpha(o ? 0xFF1C3A4A : 0xFF141920, fits ? 210 : 90));
            text(bx, by + 1, 100, 11, Qt::AlignCenter, ATT_SLOT_NAMES[a], QColor(120, 130, 140), 7, 800, false);
            text(bx, by + 12, 100, 13, Qt::AlignCenter, fits ? QString(ATT_NAMES[a][o]) : QString("N/A"),
                 o ? QColor(120, 220, 255) : fits ? QColor(170, 175, 180) : QColor(70, 75, 80), 8, 700, false);
        }
    }

    text(x, y + h - 30, w, 18, Qt::AlignCenter, "[U] OR [ESC] RESUME COMBAT      [SPACE] CLOSE", QColor(255, 200, 100), 12, 700);
}

void MainWindow::renderPauseMenu()
{
    if (!m_paused) return;
    for (int i = 0; i < W * H; i++) { uint32_t c = m_pixels[i]; int l = (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 9; m_pixels[i] = rgb(l, l, l + 4); }
    const int w = 380, h = 340, x = W / 2 - w / 2, y = H / 2 - h / 2;
    panel(x, y, w, h, true, AMBER);
    text(x, y + 16, w, 40, Qt::AlignCenter, "PAUSED", QColor(255, 255, 255), 34, 900);
    struct Opt { const char *key, *label; uint32_t c; };
    const Opt opts[5] = {
        {"R", "RESUME", AMBER},
        {"O", "SETTINGS", AMBER},
        {"M", "MAIN MENU", 0xFFDDDDDD},
        {"Q", "QUIT TO DESKTOP", 0xFFDDDDDD},
        {"ESC", "FORCE QUIT", RED}
    };
    for (int i = 0; i < 5; i++) {
        int oy = y + 68 + i * 44;
        m_cv.blendRect(x + 40, oy, w - 80, 36, withAlpha(0xFFFFFFFF, i == 0 ? 22 : 10));
        m_cv.fillRect(x + 50, oy + 7, 44, 22, opts[i].c);
        text(x + 50, oy + 7, 44, 22, Qt::AlignCenter, opts[i].key, QColor(15, 15, 15), 12, 900, false);
        text(x + 108, oy, w - 150, 36, Qt::AlignLeft | Qt::AlignVCenter, opts[i].label, QColor(235, 235, 235), 15, 700);
    }
    text(x, y + h - 28, w, 18, Qt::AlignCenter, "[O] Settings   [B] Free-aim   [V] Third-person", QColor(150, 150, 150), 11, 500);
}

void MainWindow::renderGameOver()
{
    for (int i = 0; i < W * H; i++) { uint32_t c = m_pixels[i]; int l = (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 8; m_pixels[i] = rgb(l * 2 / 5 + 30, l / 4, l / 4); }
    const int w = 460, h = 320, x = W / 2 - w / 2, y = H / 2 - h / 2;
    panel(x, y, w, h, true, RED);
    text(x, y + 16, w, 52, Qt::AlignCenter, "MISSION FAILED", QColor(255, 80, 66), 42, 900);
    int acc = m_gs.shotsFired ? int(100.f * m_gs.shotsHit / m_gs.shotsFired) : 0;
    struct Stat { const char *k; QString v; };
    const Stat st[6] = { {"KILLS", QString::number(m_gs.kills)}, {"HEADSHOTS", QString::number(m_gs.headshots)}, {"ACCURACY", QString("%1%").arg(acc)},
                         {"SCORE", QString::number(m_gs.score)}, {"ZONE", QString::number(m_gs.level)}, {"COINS", QString::number(m_gs.coins)} };
    for (int i = 0; i < 6; i++) {
        int cx = x + 30 + (i % 3) * 136, cy = y + 84 + (i / 3) * 62;
        m_cv.blendRect(cx, cy, 128, 54, withAlpha(0xFFFFFFFF, 12));
        text(cx, cy + 6, 128, 14, Qt::AlignCenter, st[i].k, QColor(160, 160, 160), 10, 700, false);
        text(cx, cy + 20, 128, 30, Qt::AlignCenter, st[i].v, QColor(255, 255, 255), 22, 800);
    }
    text(x, y + 220, w, 24, Qt::AlignCenter, "[R]  REDEPLOY", QColor(255, 200, 100), 17, 800);
    text(x, y + 250, w, 22, Qt::AlignCenter, "[M]  MAIN MENU          [Q]  QUIT", QColor(210, 210, 210), 14, 600);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Menus — the selected map renders live behind every menu screen
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::panel(int x, int y, int w, int h, bool selected, uint32_t accent)
{
    m_cv.blendRect(x, y, w, h, withAlpha(UI_BG, selected ? 222 : 185));
    m_cv.rectOutline(x, y, w, h, withAlpha(0xFFFFFFFF, selected ? 40 : 22));
    if (selected) {
        m_cv.fillRect(x, y, w, 3, accent);
        m_cv.brackets(x - 4, y - 4, w + 8, h + 8, 16, accent, 2);
    }
}

void MainWindow::renderMenuBackdrop()
{
    GameState saved = m_gs;
    float savedPitch = m_pitch, savedFov = m_adsFOVMult, savedFoot = m_gs.player.footZ;
    m_gs = GameState{};
    Player &pl = m_gs.player;
    pl.posX = m_parsed[m_mapChoice].camX; pl.posY = m_parsed[m_mapChoice].camY;
    pl.dirX = std::cos(m_menuCamAng); pl.dirY = std::sin(m_menuCamAng);
    pl.planeX = -pl.dirY * m_planeLen; pl.planeY = pl.dirX * m_planeLen;
    m_pitch = 0.04f; m_adsFOVMult = 1.f; m_gs.player.footZ = floorH(pl.posX, pl.posY) + 0.15f;
    // A few soldiers standing around for atmosphere
    const float ring[4][2] = { {3.2f, 0.4f}, {-2.6f, 2.4f}, {1.6f, -3.0f}, {-3.4f, -1.5f} };
    for (int i = 0; i < 4; i++) {
        Enemy e; e.x = pl.posX + ring[i][0]; e.y = pl.posY + ring[i][1];
        if (!posValid(e.x, e.y, 0.2f)) continue;
        e.state = i % 2 ? ES_ALERT : ES_IDLE;
        m_gs.enemies.push_back(e);
    }
    computeView();
    renderTerrain(); renderSky(); renderSprites(); renderLensFlare();
    m_gs = saved; m_pitch = savedPitch; m_adsFOVMult = savedFov; m_gs.player.footZ = savedFoot;

    // Darken: left-to-right gradient + bottom fade + vignette
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float l = 0.42f + 0.58f * std::min(1.f, float(x) / (W * 0.7f));
        float b = 1.f - 0.55f * std::max(0.f, float(y - H * 0.55f) / (H * 0.45f));
        float dx = (x - W / 2.f) / (W / 2.f), dy = (y - H / 2.f) / (H / 2.f);
        float v = 1.f - 0.35f * std::min(1.f, (dx * dx + dy * dy) * 0.5f);
        float k = l * b * v * 0.8f;
        uint32_t c = m_pixels[y * W + x];
        m_pixels[y * W + x] = rgb(int(R(c) * k), int(G(c) * k), int(B(c) * k + 3));
    }
}

void MainWindow::menuHeader(const QString &step, const QString &title)
{
    m_cv.vGrad(0, 0, W, 70, withAlpha(0, 200), withAlpha(0, 0));
    text(32, 16, 300, 16, Qt::AlignLeft, step, qc(AMBER), 12, 800, false);
    text(32, 30, W - 64, 34, Qt::AlignLeft | Qt::AlignVCenter, title, QColor(255, 255, 255), 28, 900);
}

void MainWindow::menuFooter(const QString &hint)
{
    m_cv.blendRect(0, H - 34, W, 34, withAlpha(0, 170));
    m_cv.blendRect(0, H - 34, W, 1, withAlpha(0xFFFFFFFF, 30));
    QPointF mf = widgetToFrame(m_mouseWidget);
    bool hover = mf.x() >= 16 && mf.x() < 16 + 88 && mf.y() >= H - 28 && mf.y() < H - 28 + 22;
    m_cv.blendRect(16, H - 28, 88, 22, withAlpha(0xFFFFFFFF, hover ? 45 : 18));
    if (hover) m_cv.rectOutline(16, H - 28, 88, 22, withAlpha(0xFFFFFFFF, 80));
    text(16, H - 28, 88, 22, Qt::AlignCenter, "< BACK", hover ? QColor(255, 255, 255) : QColor(220, 220, 220), 12, 700, false);
    m_menuRects.push_back({16, H - 28, 88, 22, 10});
    text(120, H - 30, W - 136, 26, Qt::AlignRight | Qt::AlignVCenter, hint, QColor(170, 170, 170), 12, 500, false);
}

void MainWindow::renderMenu()
{
    // Title block
    m_cv.fillRect(48, 108, 6, 104, AMBER);
    text(66, 96, 520, 92, Qt::AlignLeft | Qt::AlignVCenter, "RASTER", QColor(255, 255, 255), 88, 900);
    text(70, 182, 520, 30, Qt::AlignLeft | Qt::AlignVCenter, "T A C T I C A L   S H O O T E R", qc(AMBER), 18, 700);
    text(70, 212, 520, 18, Qt::AlignLeft, "Every pixel is computed on the CPU  -  pure software rasterization", QColor(170, 175, 180), 12, 500);

    const char *opts[9] = { "CAMPAIGN", "FREE PLAY", "MULTIPLAYER", "ARSENAL", "MAP EDITOR", "SETTINGS", "FIRING RANGE", "STATS", "EXIT" };
    for (int i = 0; i < 9; i++) {
        bool sel = m_menuCursor == i;
        int x = 48, y = 230 + i * 36, w = 300, h = 33;
        m_menuRects.push_back({x, y, w, h, i});
        if (sel) {
            m_cv.hGrad(x, y, w, h, withAlpha(AMBER, 70), withAlpha(AMBER, 0));
            m_cv.fillRect(x, y, 4, h, AMBER);
        } else m_cv.hGrad(x, y, w, h, withAlpha(0xFFFFFFFF, 14), withAlpha(0xFFFFFFFF, 0));
        text(x + 22, y, w, h, Qt::AlignLeft | Qt::AlignVCenter, opts[i], sel ? QColor(255, 255, 255) : QColor(175, 175, 175), 18, sel ? 800 : 600);
        if (sel) text(x + w - 60, y, 50, h, Qt::AlignRight | Qt::AlignVCenter, ">", qc(AMBER), 18, 800, false);
    }


    // Controls card
    const int cx = W - 300, cy = 226, cw = 270, ch = 312;
    panel(cx, cy, cw, ch, false, AMBER);
    text(cx + 16, cy + 8, cw, 18, Qt::AlignLeft, "FIELD MANUAL", qc(AMBER), 13, 800, false);
    QString statKey = keyName(m_keyBinds[ACT_WEAPON_STATS]);
    QByteArray statKeyBa = statKey.toLatin1();
    const char *statKeyC = statKeyBa.constData();
    const char *rows[][2] = { {"WASD", "Move"}, {"LMB / F", "Fire (hold: charge)"}, {"RMB / TAB", "Aim / scope"},
                              {"1 2 3 4", "Melee / sidearm / primaries"}, {"R", "Reload"}, {"Q", "Quick melee"}, {"G  /  T", "Throw (hold to cook) / cycle"},
                              {"Z", "Use gadget"}, {"E", "Door / pick up / swap gun"}, {"V  /  X", "1st-3rd person / shoulder"},
                              {"H", "Inspect weapon"}, {statKeyC, "Weapon stats (toggle)"}, {"SHIFT", "Sprint / hold breath"},
                              {"CTRL", "Crouch (hold)"}, {"ALT + A / D", "Lean left / right"} };
    for (int i = 0; i < 15; i++) {
        int ry = cy + 28 + i * 18;
        m_cv.blendRect(cx + 16, ry + 1, 80, 16, withAlpha(0xFFFFFFFF, 20));
        text(cx + 16, ry + 1, 80, 16, Qt::AlignCenter, rows[i][0], QColor(235, 235, 235), 10, 800, false);
        text(cx + 106, ry, cw - 116, 18, Qt::AlignLeft | Qt::AlignVCenter, rows[i][1], QColor(190, 190, 190), 10, 500, false);
    }
    text(cx, cy + ch + 4, cw, 16, Qt::AlignCenter, "Headshots deal 2-3x damage", qc(AMBER), 12, 700);

    m_cv.blendRect(0, H - 30, W, 30, withAlpha(0, 160));
    text(16, H - 28, W - 32, 26, Qt::AlignLeft | Qt::AlignVCenter, "UP/DOWN select   ENTER confirm   mouse supported", QColor(160, 160, 160), 12, 500, false);
    text(16, H - 28, W - 32, 26, Qt::AlignRight | Qt::AlignVCenter, "SCROLL raster zoom   [ ] pixel blocks", QColor(160, 160, 160), 12, 500, false);
}

void MainWindow::renderMapSelect()
{
    menuHeader("STEP 1 / 3", "SELECT DEPLOYMENT ZONE");
    static const char *TAGS[MAP_COUNT] = { "VOLCANIC  |  LAVA", "DESERT  |  TAR PITS", "SWAMP  |  BOG + TREES", "SNOW  |  ICE + PINES", "CITY RUINS  |  ACID", "FACILITY  |  COOLANT", "CITADEL  |  ACID VAULTS" };
    int scrollRow = (m_mapChoice >= 6 ? 1 : 0);
    for (int i = 0; i < MAP_COUNT; i++) {
        int row = i / 3 - scrollRow;
        if (row < 0 || row > 1) continue;
        bool sel = m_mapChoice == i;
        int w = 244, h = 228, col = i % 3;
        int x = 22 + col * (w + 12), y = 78 + row * (h + 12) - (sel ? 4 : 0);
        m_menuRects.push_back({x, y, w, h, i});
        panel(x, y, w, h, sel, AMBER);
        // top-down layout preview in the biome's own colours
        const ParsedMap &pm = m_parsed[i];
        const Theme &T = THEMES[MAP_THEME[i]];
        const int box = 128, ts = std::max(1, box / pm.w), pw = pm.w * ts, ph = pm.h * ts;
        const int px0 = x + (w - pw) / 2, py0 = y + 10 + (box - ph) / 2;
        uint32_t floorC = m_floorTex[MAP_THEME[i]].lv[MIP_LEVELS - 1][0];
        uint32_t wallC = m_wallTex[MAP_THEME[i]][1].lv[MIP_LEVELS - 1][0];
        for (int my = 0; my < pm.h; my++) for (int mx = 0; mx < pm.w; mx++) {
            int t = pm.type[my][mx];
            uint32_t c = t == CELL_DOOR ? rgb(170, 110, 50) : t == CELL_PILLAR ? scaleCol(wallC, 0.7f) : t ? wallC
                       : pm.acid[my][mx] ? T.hazLight : scaleCol(floorC, 1.f + pm.plat[my][mx] * 0.9f);
            m_cv.fillRect(px0 + mx * ts, py0 + my * ts, ts, ts, c);
        }
        m_cv.fillCircle(px0 + int(pm.spawnX * ts), py0 + int(pm.spawnY * ts), 2, AMBER);
        if (sel) {
            int scan = py0 + int(std::fmod(m_time * 40.f, float(ph)));
            m_cv.blendRect(px0, scan, pw, 2, withAlpha(AMBER, 120));
        }
        text(x, y + 142, w, 24, Qt::AlignCenter, MAP_NAMES[i], sel ? QColor(255, 255, 255) : QColor(200, 200, 200), 15, 900);
        text(x, y + 166, w, 14, Qt::AlignCenter, TAGS[i], qc(sel ? AMBER : AMBER_DIM), 10, 800, false);
        text(x + 12, y + 182, w - 24, 40, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, MAP_DESCS[i], QColor(180, 180, 180), 10, 500, false);
        text(x + w - 30, y + 6, 24, 14, Qt::AlignCenter, QString("[%1]").arg(i + 1), QColor(130, 130, 130), 10, 700, false);
    }
    menuFooter("ARROWS browse   ENTER or 1-7 select   ESC back");
}

void MainWindow::renderDiffSelect()
{
    menuHeader(QString("STEP 2 / 3   -   %1").arg(MAP_NAMES[m_mapChoice]), "SELECT DIFFICULTY");
    const uint32_t cols[3] = { GREEN, AMBER, RED };
    for (int i = 0; i < 3; i++) {
        const DiffConfig &dc = DIFF_CFG[i];
        bool sel = m_difficulty == i;
        int w = 232, h = 300, x = 30 + i * (w + 22), y = H - 34 - h - 22 - (sel ? 8 : 0);
        m_menuRects.push_back({x, y, w, h, i});
        panel(x, y, w, h, sel, cols[i]);
        // threat chevrons
        for (int k = 0; k < 3; k++) {
            int cx = x + w / 2 - 36 + k * 36, cy = y + 52;
            uint32_t c = k <= i ? cols[i] : withAlpha(0xFFFFFFFF, 30);
            m_cv.quad(cx - 14, cy - 12, cx, cy - 26, cx + 14, cy - 12, cx, cy + 2, c);
        }
        text(x, y + 80, w, 36, Qt::AlignCenter, dc.name, sel ? QColor(255, 255, 255) : QColor(200, 200, 200), 28, 900);
        text(x, y + 124, w, 16, Qt::AlignCenter, dc.desc1, QColor(190, 190, 190), 12, 500, false);
        text(x, y + 142, w, 16, Qt::AlignCenter, dc.desc2, QColor(190, 190, 190), 12, 500, false);
        text(x, y + 160, w, 16, Qt::AlignCenter, dc.desc3, qc(cols[i]), 12, 700, false);
        auto bar = [&](int by, const char *lbl, float f) {
            text(x + 16, by, w - 32, 14, Qt::AlignLeft, lbl, QColor(150, 150, 150), 10, 700, false);
            m_cv.blendRect(x + 16, by + 16, w - 32, 6, withAlpha(0xFFFFFFFF, 25));
            m_cv.fillRect(x + 16, by + 16, int((w - 32) * std::min(1.f, f)), 6, cols[i]);
        };
        bar(y + 192, "ENEMY TOUGHNESS", dc.enemyHPMult / 1.6f);
        bar(y + 222, "DAMAGE TAKEN", dc.dmgReceivedMult / 1.5f);
        bar(y + 252, "ENEMY ACCURACY", dc.enemyAccuracy);
    }
    menuFooter("LEFT/RIGHT browse   ENTER or 1-3 select   ESC back");
}

void MainWindow::renderCharSelect()
{
    menuHeader(QString("STEP 3 / 3   -   %1   -   %2").arg(MAP_NAMES[m_mapChoice]).arg(DIFF_CFG[m_difficulty].name), "SELECT OPERATIVE");
    for (int i = 0; i < 3; i++) {
        const CharProfile &cp = CHAR_PROFILES[i];
        bool sel = m_charChoice == i;
        int w = 232, h = 420, x = 30 + i * (w + 22), y = H - 34 - h - 16 - (sel ? 6 : 0);
        m_menuRects.push_back({x, y, w, h, i});
        panel(x, y, w, h, sel, AMBER);
        // Portrait plate + baked soldier sprite
        int px0 = x + (w - PORTRAIT) / 2, py0 = y + 14;
        m_cv.vGrad(px0, py0, PORTRAIT, PORTRAIT, withAlpha(sel ? AMBER : 0xFF5A6470, sel ? 70 : 40), withAlpha(UI_BG, 0));
        const std::vector<uint32_t> &pt = m_portrait[i];
        for (int yy = 0; yy < PORTRAIT; yy++) for (int xx = 0; xx < PORTRAIT; xx++) {
            uint32_t c = pt[yy * PORTRAIT + xx];
            if (c >> 24) m_cv.set(px0 + xx, py0 + yy, sel ? c : scaleCol(c, 0.7f));
        }
        m_cv.blendRect(px0 + 20, py0 + PORTRAIT - 4, PORTRAIT - 40, 3, withAlpha(0, 120));
        text(x, y + 184, w, 32, Qt::AlignCenter, cp.name, sel ? QColor(255, 255, 255) : QColor(200, 200, 200), 26, 900);
        auto stat = [&](int sy, const char *lbl, float f, uint32_t c) {
            text(x + 16, sy, 60, 14, Qt::AlignLeft, lbl, QColor(160, 160, 160), 10, 800, false);
            for (int s = 0; s < 10; s++) {
                int sx = x + 70 + s * 15;
                m_cv.quad(sx + 3, sy + 2, sx + 15, sy + 2, sx + 12, sy + 12, sx, sy + 12, s < int(f * 10 + 0.5f) ? c : withAlpha(0xFFFFFFFF, 25));
            }
        };
        stat(y + 226, "HEALTH", cp.hp / 200.f, GREEN);
        stat(y + 248, "SPEED", cp.speedMult / 1.4f, rgb(90, 170, 255));
        stat(y + 270, "STAMINA", cp.stamina / 150.f, AMBER);
        stat(y + 292, "AMMO", cp.ammo / 30.f, rgb(255, 140, 70));
        stat(y + 314, "ARMOR", cp.damageReduction * 2.f, rgb(190, 120, 255));
        text(x, y + 342, w, 16, Qt::AlignCenter, cp.stat1, QColor(170, 170, 170), 11, 500, false);
        text(x, y + 360, w, 16, Qt::AlignCenter, cp.stat2, QColor(170, 170, 170), 11, 500, false);
        text(x, y + 378, w, 16, Qt::AlignCenter, cp.stat3, i == 2 ? QColor(190, 150, 255) : QColor(170, 170, 170), 11, 600, false);
        text(x, y + h - 22, w, 16, Qt::AlignCenter, QString("[%1]").arg(i + 1), QColor(130, 130, 130), 11, 700, false);
    }
    menuFooter("LEFT/RIGHT browse   ENTER or 1-3 deploy   ESC back");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Multiplayer Lobby (Sector War)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderMultiplayerLobby()
{
    menuHeader("TACTICAL MULTIPLAYER", "SECTOR WAR  |  LOBBY");

    // Network Status Banner
    const int bx = 32, by = 64, bw = W - 64, bh = 40;
    panel(bx, by, bw, bh, false, AMBER);

    QString statusText = m_net->isHost() ? QString("HOSTING ON PORT %1").arg(MP_DEFAULT_PORT)
                                         : (m_net->isConnected() ? "CLIENT CONNECTED" : "OFFLINE / DISCONNECTED");
    text(bx + 12, by + 4, 155, 16, Qt::AlignLeft, statusText, m_net->isConnected() ? QColor(90, 230, 140) : QColor(255, 120, 90), 10, 800, false);
    text(bx + 12, by + 20, 155, 14, Qt::AlignLeft, QString("ROOM: %1").arg(m_net->settings().roomCode), QColor(200, 200, 200), 10, 600, false);

    // Callsign (Username) entry box (id 8)
    int nameX = bx + 172, nameY = by + 6, nameW = 180, nameH = 28;
    m_cv.blendRect(nameX, nameY, nameW, nameH, withAlpha(UI_BG, 240));
    m_cv.rectOutline(nameX, nameY, nameW, nameH, m_mpTypingName ? AMBER : withAlpha(0xFFFFFFFF, 60), m_mpTypingName ? 2 : 1);
    m_menuRects.push_back({nameX, nameY, nameW, nameH, 8}); // id 8 = edit callsign
    QString nameDisplay = m_mpTypingName ? QString("NAME: %1_").arg(m_mpPlayerName)
                                         : QString("NAME: %1").arg(m_mpPlayerName);
    text(nameX + 8, nameY + 2, nameW - 16, nameH - 4, Qt::AlignLeft | Qt::AlignVCenter, nameDisplay,
         m_mpTypingName ? QColor(255, 230, 100) : QColor(220, 220, 220), 10, 700, false);

    // IP / Tunnel entry box (id 9)
    int ipX = nameX + nameW + 8, ipY = by + 6, ipW = bw - (ipX - bx) - 180, ipH = 28;
    m_cv.blendRect(ipX, ipY, ipW, ipH, withAlpha(UI_BG, 240));
    m_cv.rectOutline(ipX, ipY, ipW, ipH, m_mpTypingAddr ? AMBER : withAlpha(0xFFFFFFFF, 60), m_mpTypingAddr ? 2 : 1);
    m_menuRects.push_back({ipX, ipY, ipW, ipH, 9}); // id 9 = edit IP
    QString addrDisplay = m_mpTypingAddr ? QString("TARGET: %1_").arg(m_mpInputAddr)
                                         : QString("TARGET: %1").arg(m_mpInputAddr);
    text(ipX + 8, ipY + 2, ipW - 16, ipH - 4, Qt::AlignLeft | Qt::AlignVCenter, addrDisplay,
         m_mpTypingAddr ? QColor(255, 230, 100) : QColor(220, 220, 220), 10, 700, false);

    // Host & Join Buttons
    int btnW = 82, btnH = 28, btnY = by + 6;
    int hX = bx + bw - 174, jX = bx + bw - 88;
    m_cv.blendRect(hX, btnY, btnW, btnH, withAlpha(m_net->isHost() ? AMBER : UI_BG, 200));
    m_cv.rectOutline(hX, btnY, btnW, btnH, m_net->isHost() ? 0xFFFFFFFF : withAlpha(AMBER, 180), 1);
    m_menuRects.push_back({hX, btnY, btnW, btnH, 6}); // id 6 = host
    text(hX, btnY, btnW, btnH, Qt::AlignCenter, "[H] HOST", m_net->isHost() ? QColor(20, 20, 20) : QColor(255, 255, 255), 11, 800, false);

    m_cv.blendRect(jX, btnY, btnW, btnH, withAlpha(!m_net->isHost() && m_net->isConnected() ? GREEN : UI_BG, 200));
    m_cv.rectOutline(jX, btnY, btnW, btnH, withAlpha(0xFFFFFFFF, 160), 1);
    m_menuRects.push_back({jX, btnY, btnW, btnH, 7}); // id 7 = join
    text(jX, btnY, btnW, btnH, Qt::AlignCenter, "[J] JOIN", QColor(255, 255, 255), 11, 800, false);

    // Host Match Settings Bar (Team Size, Bot Toggle, Round Time, Series Length, Map)
    const int sy = by + bh + 6, sh = 30;
    const MpRoomSettings &st = m_net->settings();
    bool isHost = m_net->isHost();

    // 1. Team Size Button (id 20)
    int s1X = bx, s1W = 120;
    m_cv.blendRect(s1X, sy, s1W, sh, withAlpha(UI_BG, 230));
    m_cv.rectOutline(s1X, sy, s1W, sh, isHost ? AMBER : withAlpha(0xFFFFFFFF, 40), 1);
    if (isHost) m_menuRects.push_back({s1X, sy, s1W, sh, 20});
    text(s1X, sy, s1W, sh, Qt::AlignCenter, QString("MODE: %1v%1").arg(st.teamSize), isHost ? QColor(255, 215, 60) : QColor(200, 200, 200), 10, 800, false);

    // 2. Bot Toggle Button (id 21)
    int s2X = s1X + s1W + 8, s2W = 120;
    m_cv.blendRect(s2X, sy, s2W, sh, withAlpha(UI_BG, 230));
    m_cv.rectOutline(s2X, sy, s2W, sh, isHost ? (st.botFill ? GREEN : RED) : withAlpha(0xFFFFFFFF, 40), 1);
    if (isHost) m_menuRects.push_back({s2X, sy, s2W, sh, 21});
    text(s2X, sy, s2W, sh, Qt::AlignCenter, st.botFill ? "BOTS: ON" : "BOTS: OFF (PvP)", st.botFill ? QColor(90, 240, 140) : QColor(255, 90, 80), 10, 800, false);

    // 3. Round Duration Button (id 22)
    int s3X = s2X + s2W + 8, s3W = 130;
    m_cv.blendRect(s3X, sy, s3W, sh, withAlpha(UI_BG, 230));
    m_cv.rectOutline(s3X, sy, s3W, sh, isHost ? AMBER : withAlpha(0xFFFFFFFF, 40), 1);
    if (isHost) m_menuRects.push_back({s3X, sy, s3W, sh, 22});
    text(s3X, sy, s3W, sh, Qt::AlignCenter, QString("TIME: %1 MIN").arg(st.roundDuration / 60), isHost ? QColor(255, 215, 60) : QColor(200, 200, 200), 10, 800, false);

    // 4. Series Length Button (id 23)
    int s4X = s3X + s3W + 8, s4W = 150;
    m_cv.blendRect(s4X, sy, s4W, sh, withAlpha(UI_BG, 230));
    m_cv.rectOutline(s4X, sy, s4W, sh, isHost ? AMBER : withAlpha(0xFFFFFFFF, 40), 1);
    if (isHost) m_menuRects.push_back({s4X, sy, s4W, sh, 23});
    QString seriesName = (st.roundsToWin == 1) ? "BEST OF 1" : (st.roundsToWin == 2) ? "BEST OF 3" : "BEST OF 5";
    text(s4X, sy, s4W, sh, Qt::AlignCenter, seriesName, isHost ? QColor(255, 215, 60) : QColor(200, 200, 200), 10, 800, false);

    // 5. Map Choice Button (id 24)
    int s5X = s4X + s4W + 8, s5W = bw - (s5X - bx);
    m_cv.blendRect(s5X, sy, s5W, sh, withAlpha(UI_BG, 230));
    m_cv.rectOutline(s5X, sy, s5W, sh, isHost ? AMBER : withAlpha(0xFFFFFFFF, 40), 1);
    if (isHost) m_menuRects.push_back({s5X, sy, s5W, sh, 24});
    QString mapName = (st.mapChoice == 6) ? "CITADEL & BUNKER" : (st.mapChoice == 5) ? "HELIX FACILITY" : (st.mapChoice == 4) ? "ASHEN CITY" : "DUNE OUTPOST";
    text(s5X, sy, s5W, sh, Qt::AlignCenter, QString("MAP: %1").arg(mapName), isHost ? QColor(90, 210, 255) : QColor(200, 200, 200), 10, 800, false);

    // Two Team Columns: Team Alpha (Red) & Team Bravo (Blue)
    const int colW = (W - 80) / 2, colH = 295, colY = sy + sh + 8;
    int aX = 32, bX = 48 + colW;

    // Team Alpha Box
    panel(aX, colY, colW, colH, false, RED);
    m_cv.fillRect(aX, colY, colW, 26, withAlpha(RED, 220));
    text(aX + 16, colY + 3, colW - 32, 20, Qt::AlignLeft | Qt::AlignVCenter, "TEAM ALPHA  (RED)", QColor(255, 255, 255), 13, 900, false);

    // Team Bravo Box
    panel(bX, colY, colW, colH, false, 0xFF3AB4E8);
    m_cv.fillRect(bX, colY, colW, 26, withAlpha(0xFF2878D0, 220));
    text(bX + 16, colY + 3, colW - 32, 20, Qt::AlignLeft | Qt::AlignVCenter, "TEAM BRAVO  (BLUE)", QColor(255, 255, 255), 13, 900, false);

    // Populate player slots in Alpha and Bravo
    int alphaRow = 0, bravoRow = 0;
    for (const auto &slot : m_net->playerSlots()) {
        bool isAlpha = (slot.team == 0);
        int curRow = isAlpha ? alphaRow++ : bravoRow++;
        if (curRow >= st.teamSize && curRow >= 4) continue;

        int sx = isAlpha ? aX + 8 : bX + 8;
        int sySlot = colY + 32 + curRow * 48;
        int sw = colW - 16, shSlot = 42;

        bool isMe = (slot.id == m_net->localPlayerId());
        m_cv.blendRect(sx, sySlot, sw, shSlot, withAlpha(UI_BG, 220));
        m_cv.rectOutline(sx, sySlot, sw, shSlot, isMe ? AMBER : withAlpha(0xFFFFFFFF, 40), isMe ? 2 : 1);

        // Operative Name
        QString dispName = slot.name;
        if (slot.isHost) dispName += " [HOST]";
        if (slot.isBot) dispName += " [BOT]";
        if (isMe) dispName += " (YOU)";
        text(sx + 8, sySlot + 3, 165, 18, Qt::AlignLeft, dispName, isMe ? QColor(255, 230, 100) : QColor(240, 240, 240), 11, 800, false);

        // Class badge
        const char *clsNames[3] = { "ASSAULT", "SCOUT", "TANK" };
        uint32_t clsCols[3] = { 0xFFE89A3A, 0xFF5AE68C, 0xFF4AE0FF };
        int ci = clampi(int(slot.cls), 0, 2);
        m_cv.blendRect(sx + 8, sySlot + 23, 70, 14, withAlpha(clsCols[ci], 180));
        text(sx + 8, sySlot + 23, 70, 14, Qt::AlignCenter, clsNames[ci], QColor(15, 15, 15), 9, 900, false);

        // Ready badge
        int readyW = 60, readyX = sx + sw - (isHost && !slot.isHost ? 90 : 66);
        if (slot.ready) {
            m_cv.blendRect(readyX, sySlot + 11, readyW, 20, withAlpha(GREEN, 200));
            text(readyX, sySlot + 11, readyW, 20, Qt::AlignCenter, "READY", QColor(10, 10, 10), 9, 900, false);
        } else {
            m_cv.blendRect(readyX, sySlot + 11, readyW, 20, withAlpha(0xFF4A5058, 160));
            text(readyX, sySlot + 11, readyW, 20, Qt::AlignCenter, "WAITING", QColor(200, 200, 200), 9, 700, false);
        }

        // Host Kick Button [X]
        if (isHost && !slot.isHost) {
            int kX = sx + sw - 24, kY = sySlot + 11, kW = 20, kH = 20;
            m_cv.blendRect(kX, kY, kW, kH, withAlpha(RED, 210));
            m_cv.rectOutline(kX, kY, kW, kH, 0xFFFFFFFF, 1);
            m_menuRects.push_back({kX, kY, kW, kH, 100 + slot.id}); // id 100+slot.id = Kick
            text(kX, kY, kW, kH, Qt::AlignCenter, "X", QColor(255, 255, 255), 10, 900, false);
        }
    }

    // Bottom Action Buttons:
    int byRow = H - 54;
    int b1W = 115, b1H = 34;

    m_cv.blendRect(32, byRow, b1W, b1H, withAlpha(RED, 160));
    m_cv.rectOutline(32, byRow, b1W, b1H, withAlpha(RED, 240), 1);
    m_menuRects.push_back({32, byRow, b1W, b1H, 1}); // id 1 = Alpha
    text(32, byRow, b1W, b1H, Qt::AlignCenter, "[1] TEAM ALPHA", QColor(255, 255, 255), 10, 800, false);

    m_cv.blendRect(155, byRow, b1W, b1H, withAlpha(0xFF2878D0, 160));
    m_cv.rectOutline(155, byRow, b1W, b1H, withAlpha(0xFF2878D0, 240), 1);
    m_menuRects.push_back({155, byRow, b1W, b1H, 2}); // id 2 = Bravo
    text(155, byRow, b1W, b1H, Qt::AlignCenter, "[2] TEAM BRAVO", QColor(255, 255, 255), 10, 800, false);

    m_cv.blendRect(278, byRow, 125, b1H, withAlpha(AMBER, 160));
    m_cv.rectOutline(278, byRow, 125, b1H, withAlpha(AMBER, 240), 1);
    m_menuRects.push_back({278, byRow, 125, b1H, 3}); // id 3 = Class
    text(278, byRow, 125, b1H, Qt::AlignCenter, "[C] CYCLE CLASS", QColor(255, 255, 255), 10, 800, false);

    m_cv.blendRect(411, byRow, 110, b1H, withAlpha(GREEN, 160));
    m_cv.rectOutline(411, byRow, 110, b1H, withAlpha(GREEN, 240), 1);
    m_menuRects.push_back({411, byRow, 110, b1H, 4}); // id 4 = Ready
    text(411, byRow, 110, b1H, Qt::AlignCenter, "[R] TOGGLE READY", QColor(255, 255, 255), 10, 800, false);

    if (m_net->isHost()) {
        m_cv.blendRect(529, byRow, W - 32 - 529, b1H, withAlpha(GREEN, 230));
        m_cv.rectOutline(529, byRow, W - 32 - 529, b1H, 0xFFFFFFFF, 2);
        m_menuRects.push_back({529, byRow, W - 32 - 529, b1H, 5}); // id 5 = Start
        text(529, byRow, W - 32 - 529, b1H, Qt::AlignCenter, "[ENTER] START MATCH", QColor(10, 10, 10), 12, 900, false);
    } else {
        m_cv.blendRect(529, byRow, W - 32 - 529, b1H, withAlpha(UI_BG, 200));
        m_cv.rectOutline(529, byRow, W - 32 - 529, b1H, withAlpha(0xFFFFFFFF, 40), 1);
        QString waitText = m_net->isConnected() ? "WAITING FOR HOST TO START" : "OFFLINE - CLICK [H] OR [J]";
        text(529, byRow, W - 32 - 529, b1H, Qt::AlignCenter, waitText, m_net->isConnected() ? QColor(90, 230, 140) : QColor(255, 120, 90), 10, 800, false);
    }

    menuFooter("[ESC] Menu   [H] Host   [J] Join   [N] Callsign   [1]/[2] Team   [C] Class   [R] Ready   [Click to edit/select]");
}

// ─────────────────────────────────────────────────────────────────────────────
//  In-Game Multiplayer HUD
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderMultiplayerHUD()
{
    const MpMatchState &ms = m_net->matchState();

    // Top Match Scoreboard Banner
    const int bw = 460, bh = 34, bx = W / 2 - bw / 2, by = 10;
    m_cv.blendRect(bx, by, bw, bh, withAlpha(UI_BG, 220));
    m_cv.rectOutline(bx, by, bw, bh, withAlpha(0xFFFFFFFF, 50), 1);

    // Alpha Score
    m_cv.fillRect(bx, by, 140, bh, withAlpha(RED, 210));
    text(bx + 10, by, 120, bh, Qt::AlignCenter, QString("ALPHA: %1").arg(ms.teamScore[0]), QColor(255, 255, 255), 14, 900);

    // Center Round & Time
    int mins = int(ms.roundTimer) / 60, secs = int(ms.roundTimer) % 60;
    QString timeStr = QString("R%1  %2:%3").arg(ms.currentRound).arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    text(bx + 140, by, bw - 280, bh, Qt::AlignCenter, timeStr, QColor(255, 220, 80), 13, 800);

    // Bravo Score
    m_cv.fillRect(bx + bw - 140, by, 140, bh, withAlpha(0xFF2878D0, 210));
    text(bx + bw - 130, by, 120, bh, Qt::AlignCenter, QString("BRAVO: %1").arg(ms.teamScore[1]), QColor(255, 255, 255), 14, 900);

    // Objective Banner
    int obW = 420, obH = 22, obX = W / 2 - obW / 2, obY = by + bh + 4;
    m_cv.blendRect(obX, obY, obW, obH, withAlpha(UI_BG, 200));

    if (!ms.keyActive && !ms.vaultUnlocked) {
        int left = std::max(0, int(ms.roundTimer - (float(m_net->settings().roundDuration) - 120.f)));
        text(obX, obY, obW, obH, Qt::AlignCenter, QString("VAULT KEY ARRIVING IN %1s").arg(left), QColor(190, 190, 190), 10, 700, false);
    } else if (ms.keyActive && ms.keyCarrier < 0) {
        m_cv.rectOutline(obX, obY, obW, obH, 0xFFFFD700, 1);
        text(obX, obY, obW, obH, Qt::AlignCenter, "[!] ENCRYPTION KEY IS IN SUB-LEVEL VAULT (Z: -1.8)!", QColor(255, 225, 60), 10, 800, false);
    } else if (ms.keyActive && ms.keyCarrier >= 0) {
        m_cv.rectOutline(obX, obY, obW, obH, 0xFFFFD700, 1);
        text(obX, obY, obW, obH, Qt::AlignCenter, "[!] KEY CARRIED -> DELIVER TO NORTH CATWALK TERMINAL!", QColor(255, 215, 40), 10, 800, false);
    } else if (ms.vaultUnlocked) {
        m_cv.rectOutline(obX, obY, obW, obH, GREEN, 1);
        text(obX, obY, obW, obH, Qt::AlignCenter, "[VAULT UNLOCKED] POWER WEAPONS ACCESSIBLE IN BASEMENT", qc(GREEN), 10, 800, false);
    }

    // Dead / Respawning Screen Overlay
    if (m_gs.player.hp <= 0 && ms.inRound) {
        m_cv.blendRect(0, 0, W, H, withAlpha(RED, 60));
        int dy = H / 2 - 40;
        text(0, dy, W, 36, Qt::AlignCenter, "ELIMINATED IN ACTION", QColor(255, 80, 70), 28, 900);
        text(0, dy + 40, W, 22, Qt::AlignCenter, "HOLDING WEAPON DROPPED AS WORLD LOOT", QColor(240, 240, 240), 13, 700);
        int secLeft = std::max(1, int(m_mpRespawnCountdown + 0.99f));
        text(0, dy + 68, W, 22, Qt::AlignCenter, QString("RESPAWNING WITH STANDARD CLASS LOADOUT IN %1s...").arg(secLeft), QColor(255, 220, 90), 13, 700);
    }

    // Round Intermission Overlay (Displays after every round)
    if (ms.roundOver && !ms.matchOver) {
        int rw = 540, rh = 180, rx = W / 2 - rw / 2, ry = H / 2 - rh / 2;
        m_cv.blendRect(rx, ry, rw, rh, withAlpha(UI_BG, 245));
        m_cv.rectOutline(rx, ry, rw, rh, AMBER, 2);

        text(rx, ry + 12, rw, 28, Qt::AlignCenter, QString("ROUND %1 FINISHED").arg(ms.currentRound), QColor(255, 215, 60), 20, 900);

        QString winStr = (ms.roundWinner == 0) ? "★ TEAM ALPHA WINS THE ROUND ★" :
                         (ms.roundWinner == 1) ? "★ TEAM BRAVO WINS THE ROUND ★" : "ROUND TIED — NO WINNER";
        uint32_t winCol = (ms.roundWinner == 0) ? RED : (ms.roundWinner == 1) ? 0xFF2878D0 : AMBER;
        text(rx, ry + 48, rw, 32, Qt::AlignCenter, winStr, qc(winCol), 20, 900);

        QString seriesStr = QString("SERIES STANDINGS:  ALPHA [ %1 ]  —  [ %2 ] BRAVO    (FIRST TO %3)")
                                .arg(ms.roundsWon[0]).arg(ms.roundsWon[1]).arg(m_net->settings().roundsToWin);
        text(rx, ry + 88, rw, 22, Qt::AlignCenter, seriesStr, QColor(230, 230, 230), 13, 800);

        int nextSec = std::max(1, int(ms.intermissionTimer + 0.99f));
        text(rx, ry + 128, rw, 24, Qt::AlignCenter, QString("NEXT ROUND BEGINS IN %1s...").arg(nextSec), QColor(90, 230, 140), 14, 800);
    }

    // Match Over / Victory / Defeat Overlay
    if (ms.matchOver) {
        int mw = 600, mh = 220, mx = W / 2 - mw / 2, my = H / 2 - mh / 2;
        m_cv.blendRect(mx, my, mw, mh, withAlpha(UI_BG, 250));
        uint32_t champCol = (ms.winningTeam == 0) ? RED : 0xFF2878D0;
        m_cv.rectOutline(mx, my, mw, mh, champCol, 3);

        int myTeam = 0;
        for (const auto &s : m_net->playerSlots()) if (s.id == m_net->localPlayerId()) myTeam = s.team;
        bool victory = (ms.winningTeam == myTeam);

        text(mx, my + 14, mw, 36, Qt::AlignCenter, victory ? "★ TOURNAMENT VICTORY ★" : "MATCH DEFEAT",
             victory ? QColor(90, 240, 140) : QColor(255, 70, 60), 28, 900);

        QString champStr = (ms.winningTeam == 0) ? "TEAM ALPHA HAS SECURED THE SECTOR" : "TEAM BRAVO HAS SECURED THE SECTOR";
        text(mx, my + 58, mw, 24, Qt::AlignCenter, champStr, qc(champCol), 15, 800);

        QString finalStr = QString("FINAL SERIES:  ALPHA %1  —  %2 BRAVO").arg(ms.roundsWon[0]).arg(ms.roundsWon[1]);
        text(mx, my + 94, mw, 24, Qt::AlignCenter, finalStr, QColor(240, 240, 240), 14, 800);

        text(mx, my + 148, mw, 28, Qt::AlignCenter, "[ENTER] OR [ESC] RETURN TO LOBBY", QColor(255, 215, 60), 14, 900);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Multiplayer Scoreboard (Tab / F1)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderScoreboard()
{
    const int sw = 620, sh = 360, sx = W / 2 - sw / 2, sy = H / 2 - sh / 2;
    panel(sx, sy, sw, sh, true, AMBER);

    text(sx, sy + 10, sw, 24, Qt::AlignCenter, "SECTOR WAR  -  TACTICAL SCOREBOARD", qc(AMBER), 16, 900);

    auto renderTeamTable = [this](int team, int tx, int ty, int tw, const QString &tName, uint32_t tCol) {
        m_cv.fillRect(tx, ty, tw, 22, withAlpha(tCol, 220));
        text(tx + 8, ty + 2, 160, 18, Qt::AlignLeft, tName, QColor(255, 255, 255), 12, 900, false);
        text(tx + tw - 80, ty + 2, 70, 18, Qt::AlignRight, "SCORE", QColor(255, 255, 255), 10, 800, false);

        int rowY = ty + 24;
        for (const auto &slot : m_net->playerSlots()) {
            if (slot.team != team) continue;
            m_cv.blendRect(tx, rowY, tw, 20, withAlpha(0xFFFFFFFF, (slot.id == m_net->localPlayerId()) ? 30 : 10));
            text(tx + 8, rowY + 2, 160, 16, Qt::AlignLeft, slot.name + (slot.isBot ? " [BOT]" : ""),
                 (slot.id == m_net->localPlayerId()) ? QColor(255, 220, 80) : QColor(220, 220, 220), 10, 700, false);
            const char *cName = (slot.cls == CLS_SCOUT) ? "SCOUT" : (slot.cls == CLS_TANK) ? "TANK" : "ASSAULT";
            text(tx + 180, rowY + 2, 70, 16, Qt::AlignCenter, cName, QColor(170, 175, 180), 9, 600, false);
            text(tx + tw - 70, rowY + 2, 60, 16, Qt::AlignRight, QString("%1 PTS").arg(slot.score), QColor(255, 255, 255), 10, 800, false);
            rowY += 22;
        }
    };

    renderTeamTable(0, sx + 20, sy + 44, sw - 40, "TEAM ALPHA (RED SQUAD)", RED);
    renderTeamTable(1, sx + 20, sy + 184, sw - 40, "TEAM BRAVO (BLUE SQUAD)", 0xFF2878D0);

    text(sx, sy + sh - 26, sw, 18, Qt::AlignCenter, "[TAB] or [F1] to close scoreboard", QColor(160, 160, 160), 10, 600);
}

