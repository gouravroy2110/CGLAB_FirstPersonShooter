// =============================================================================
//  render_hud.cpp  -  Frame composition, post-processing, HUD, menus, overlays
//
//  All panels, bars, icons, the compass, minimap, crosshair and markers are
//  rasterized with the primitives in raster.h; text uses the bitmap pixel
//  font in pixelfont.h. No vector drawing (QPainter paths/fonts) is used.
// =============================================================================
#include "mainwindow.h"
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
    if (m_screen != SCR_PLAYING) {
        m_menuRects.clear();
        renderMenuBackdrop();
        switch (m_screen) {
        case SCR_MENU:        renderMenu();       break;
        case SCR_MAP_SELECT:  renderMapSelect();  break;
        case SCR_DIFF_SELECT: renderDiffSelect(); break;
        case SCR_CHAR_SELECT: renderCharSelect(); break;
        default: break;
        }
        flushText();
        return;
    }
    computeView();
    renderTerrain();
    renderSky();
    renderSprites();
    renderParticles();
    renderTracers();
    renderLensFlare();
    pixelate();
    renderDamageNumbers();
    renderViewModel();
    renderCasings();
    renderReflexSight();
    renderScope();
    renderPostFX();
    renderHUD();
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
    int maxHP = CHAR_PROFILES[m_charChoice].hp;
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
            r -= (r * k) >> 9; g += ((200 - g) * k) >> 9; b -= (b * k) >> 9;
            if (g < 0) g = 0;
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
    const Player &pl = m_gs.player;
    bool scoped = effectiveScope(pl.curWeapon) && m_adsT > 0.6f;
    if (!scoped) { renderCompass(); renderMinimap(); renderKillFeed(); }
    renderHealthPanel();
    renderWeaponPanel();
    if (!scoped) renderTargetInfo();
    renderDamageDirection();
    renderCrosshair();
    renderHitMarkers();

    float ax, ay; aimPoint(ax, ay);
    int mag = pl.mag[pl.curWeapon], res = pl.reserve[pl.curWeapon];
    int maxMag = effectiveMagSize(pl.curWeapon);
    if (pl.curWeapon != WPN_KNIFE && pl.reloadTimer <= 0 && pl.switchTimer <= 0 && !m_gs.gameOver) {
        if (mag == 0 && res == 0) text(ax - 100, ay + 34, 200, 20, Qt::AlignCenter, "NO AMMO", QColor(255, 70, 60), 14, 800);
        else if (mag <= maxMag / 4 && res > 0) text(ax - 100, ay + 34, 200, 20, Qt::AlignCenter, "[R]  RELOAD", QColor(255, 190, 80, int(180 + 75 * std::sin(m_time * 8))), 13, 700);
        else if (m_nearSecret) text(ax - 150, ay + 34, 300, 20, Qt::AlignCenter, "[E]  INSPECT WALL", QColor(255, 215, 100, int(210 + 45 * std::sin(m_time * 9))), 14, 800);
        else if (m_nearDoor) text(ax - 150, ay + 34, 300, 20, Qt::AlignCenter, "[E]  OPEN DOOR", QColor(220, 220, 220, int(190 + 50 * std::sin(m_time * 6))), 13, 700);
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
    if (m_acidFlash > 0.3f && !m_gs.gameOver)
        text(0, H / 2 + 60, W, 22, Qt::AlignCenter, "ACID!  JUMP OUT", QColor(170, 255, 90, int(255 * std::min(1.f, m_acidFlash))), 18, 900);
    if (m_combo >= 2 && m_comboDisplay > 0) {
        float t = std::min(1.f, m_comboDisplay / 0.5f);
        text(0, 96, W, 30, Qt::AlignCenter, QString("x%1  KILL STREAK").arg(m_combo), QColor(255, 200, 60, int(255 * t)), 22, 800);
    }
    if (m_pixelScale > 1)
        text(0, H - 16, W, 14, Qt::AlignCenter, QString("PIXEL BLOCK %1x%1  ( [ ] to change )").arg(m_pixelScale), QColor(255, 200, 100, 200), 11, 600);
    if (m_gs.frameNum < 420 && m_gs.level == 1 && !scoped) {
        m_cv.blendRect(0, H - 20, W, 20, withAlpha(0, 150));
        text(0, H - 19, W, 18, Qt::AlignCenter, "WASD move  MOUSE aim  LMB fire/strike  RMB sights  R reload  1-4 weapons  SPACE jump  E door  ESC pause",
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
        if (!e.alive || e.state != ES_ALERT) continue;
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
    for (int w = WPN_PISTOL; w < WPN_COUNT; ++w) if (pl.owned[w]) curTotalAmmo += pl.mag[w] + pl.reserve[w];
    for (const WeaponPickup &wp : m_gs.weaponPickups) {
        if (!wp.active) continue;
        if (!wp.emergency && (curTotalAmmo > 0 || wp.type != PICKUP_AMMO)) continue;
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
    text(cx - 160, y0 + hgt + 20, 320, 16, Qt::AlignCenter,
         QString("ELIMINATE HOSTILES   %1 / %2      SECRETS   %3 / %4").arg(m_gs.killsThisLevel).arg(m_gs.killsNeeded)
             .arg(m_gs.secretsFound).arg(m_gs.secretsTotal), QColor(255, 210, 140), 12, 700);
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
        if (cx < 0 || cy < 0 || cx >= MAP_W || cy >= MAP_H) c = withAlpha(0xFF05070A, 200);
        else if (m_map[cy][cx] == CELL_DOOR && m_doorT[cy][cx] < 0.9f) c = withAlpha(rgb(170, 120, 60), 235);
        else if (solidCell(cx, cy)) c = withAlpha(rgb(150, 156, 164), 235);
        else if (m_acid[cy][cx]) c = withAlpha(rgb(90, 190, 50), 220);
        else {
            int lift = int(m_plat[cy][cx] * 40.f);           // ledges lighter, pits darker
            c = withAlpha(rgb(14 + lift, 18 + lift, 22 + lift), 180);
        }
        // view cone
        if (oy < 0 && std::fabs(float(ox)) < -oy * 0.66f) c = lerpCol(c, withAlpha(0xFFFFFFFF, R(c) > 100 ? 235 : 190), 0.09f);
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
        bool alert = e.state == ES_ALERT;
        if (toMap(e.x, e.y, sx, sy, alert)) m_cv.fillCircle(int(sx), int(sy), alert && blink ? 4 : 3, alert ? RED : rgb(220, 120, 110));
    }
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
    const WeaponDef &wd = WEAPONS[pl.curWeapon];
    const int w = 262, h = 54, x = W - w - 16, y = H - 74;
    plate(m_cv, x, y, w, h, 10, withAlpha(UI_BG, 175));
    m_cv.blendRect(x + 10, y + h, w - 10, 2, withAlpha(AMBER, 200));
    // weapon silhouette icon, rasterized from the same polygons as the view model
    const GunModel &M = *GUN_MODELS[pl.curWeapon];
    float sc = 92.f / M.length;
    if (pl.curWeapon == WPN_PISTOL) sc = 1.0f;
    else if (pl.curWeapon == WPN_KNIFE) sc = 1.05f;
    drawGunFlat(m_cv, M, x + 18, y + 30 + (M.hMax + M.hMin) * 0.5f * sc, sc, true, withAlpha(0xFFFFFFFF, 210));
    if (pl.curWeapon == WPN_KNIFE) {
        text(x + 118, y + 4, 134, 30, Qt::AlignRight | Qt::AlignVCenter, "MELEE", QColor(255, 215, 80), 18, 800);
        text(x + 118, y + 37, 134, 13, Qt::AlignRight, wd.name, QColor(170, 170, 170), 10, 600, false);
    } else {
        int mag = pl.mag[pl.curWeapon];
        int maxMag = effectiveMagSize(pl.curWeapon);
        uint32_t mc = mag == 0 ? RED : mag <= maxMag / 4 ? AMBER : 0xFFFFFFFF;
        text(x + 118, y + 2, 70, 36, Qt::AlignRight | Qt::AlignVCenter, QString::number(mag), qc(mc), 26, 800);
        text(x + 192, y + 14, 60, 20, Qt::AlignLeft | Qt::AlignVCenter, QString("/ %1").arg(pl.reserve[pl.curWeapon]), QColor(190, 190, 190), 15, 600);
        text(x + 118, y + 37, 134, 13, Qt::AlignRight, wd.name, QColor(170, 170, 170), 10, 600, false);
        // rounds in the magazine
        int pips = maxMag, pw = std::max(2, std::min(5, 100 / pips));
        for (int i = 0; i < pips; i++) m_cv.fillRect(x + 18 + i * (pw + 1), y + h - 8, pw, 4, i < mag ? withAlpha(AMBER, 230) : withAlpha(0xFFFFFFFF, 35));
    }

    // Attachment badges for active weapon
    int bx = x + 10;
    auto badge = [&](const char *label, bool active, uint32_t col) {
        m_cv.blendRect(bx, y - 10, 27, 9, withAlpha(active ? col : 0xFF1E2228, active ? 220 : 100));
        text(bx, y - 11, 27, 9, Qt::AlignCenter, label, active ? QColor(255, 255, 255) : QColor(95, 100, 105), 8, 700, false);
        bx += 30;
    };
    if (pl.curWeapon != WPN_KNIFE) {
        badge("MAG", pl.upgradeExtMag[pl.curWeapon], GREEN);
        badge("COMP", pl.upgradeCompensator[pl.curWeapon], 0xFF3AB4E8);
        badge("AP", pl.upgradeAmmo[pl.curWeapon], AMBER);
        const char *spName = (pl.curWeapon == WPN_RIFLE ? "ACOG" : pl.curWeapon == WPN_SHOTGUN ? "PUMP" : "HAIR");
        badge(spName, pl.upgradeSpecial[pl.curWeapon], 0xFFFF7040);
    } else {
        badge("HILT", pl.upgradeCompensator[WPN_KNIFE], 0xFF3AB4E8);
        badge("SERR", pl.upgradeAmmo[WPN_KNIFE], AMBER);
        badge("STIL", pl.upgradeSpecial[WPN_KNIFE], 0xFFFF7040);
    }
    // reload progress
    if (pl.reloadTimer > 0) {
        float p = 1.f - pl.reloadTimer / std::max(0.01f, m_reloadTotal);
        m_cv.blendRect(x + 10, y - 8, w - 10, 3, withAlpha(0xFFFFFFFF, 40));
        m_cv.blendRect(x + 10, y - 8, int((w - 10) * p), 3, AMBER);
        text(x + 10, y - 24, w - 10, 14, Qt::AlignRight, "RELOADING", QColor(255, 200, 100), 11, 800);
    }
    // weapon slots
    for (int i = 0; i < WPN_COUNT; i++) {
        int sx = x + 10 + i * 30, sy = y - 24;
        if (pl.reloadTimer > 0) break;
        bool cur = i == pl.curWeapon, own = pl.owned[i];
        m_cv.blendRect(sx, sy, 24, 14, withAlpha(cur ? AMBER : UI_BG, cur ? 230 : own ? 170 : 80));
        text(sx, sy, 24, 14, Qt::AlignCenter, QString::number(i + 1), cur ? QColor(20, 20, 20) : own ? QColor(220, 220, 220) : QColor(90, 90, 90), 10, 800, false);
    }
}

void MainWindow::renderCrosshair()
{
    const Player &pl = m_gs.player;
    if (m_adsT > 0.35f || m_gs.gameOver) return;
    int cx = int(m_aimX), cy = int(m_aimY);
    const WeaponDef &wd = WEAPONS[pl.curWeapon];

    if (pl.curWeapon == WPN_KNIFE) {
        bool inRange = (m_probeEnemy >= 0);
        int gap = inRange ? 7 : 13;
        uint32_t col = inRange ? rgb(255, 50, 40) : 0xFFFFFFFF;
        m_cv.brackets(cx - gap, cy - gap, gap * 2 + 1, gap * 2 + 1, 4, withAlpha(col, inRange ? 255 : 180), 2);
        m_cv.fillRect(cx - 1, cy - 1, 2, 2, col);
        if (inRange) {
            text(cx - 30, cy + gap + 4, 60, 12, Qt::AlignCenter, "READY", QColor(255, 60, 50), 9, 800, false);
        }
        return;
    }

    bool moving = m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D);
    float spread = wd.spread * (moving ? 1.8f : 1.f) * (inAir() ? 2.5f : 1.f) * W;
    int gap = int(5 + spread * 0.9f + m_kick * 10 + (pl.sprinting ? 10 : 0));
    const int len = 9;
    bool enemy = m_probeEnemy >= 0;
    uint32_t col = enemy ? (m_probePart == PART_HEAD ? rgb(255, 60, 50) : rgb(255, 120, 90)) : 0xFFFFFFFF;
    auto bar = [&](int x, int y, int w, int h) { m_cv.blendRect(x - 1, y - 1, w + 2, h + 2, withAlpha(0, 150)); m_cv.fillRect(x, y, w, h, col); };
    if (pl.curWeapon == WPN_SHOTGUN) {
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

void MainWindow::renderDamageDirection()
{
    if (m_damageDirTimer <= 0) return;
    const Player &pl = m_gs.player;
    float t = std::min(1.f, m_damageDirTimer / 1.2f);
    float rel = m_damageAngle - std::atan2(pl.dirY, pl.dirX);
    float sa = rel - PI / 2;
    m_cv.arc(W / 2, H / 2, 118, 128, sa - 0.32f, sa + 0.32f, withAlpha(RED, int(200 * t)));
    m_cv.arc(W / 2, H / 2, 112, 117, sa - 0.18f, sa + 0.18f, withAlpha(rgb(255, 150, 120), int(160 * t)));
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
    const char *typeStr = e.elite ? "ELITE GUARD" :
                          e.type == ET_SCOUT ? "SCOUT" :
                          e.type == ET_SHOTGUNNER ? "SHOTGUNNER" :
                          e.type == ET_SNIPER ? "SNIPER" : "INFANTRY";
    text(x + 8, y + 2, w - 16, 14, Qt::AlignLeft, typeStr, e.elite ? QColor(255, 60, 50) : e.type == ET_SNIPER ? QColor(255, 90, 70) : QColor(255, 160, 90), 11, 800, false);
    text(x + 8, y + 2, w - 16, 14, Qt::AlignRight, m_probePart == PART_HEAD ? QString("HEAD  x%1").arg(WEAPONS[m_gs.player.curWeapon].headMult, 0, 'f', 1)
                                                   : m_probePart == PART_LIMB ? "LIMB" : "BODY",
         m_probePart == PART_HEAD ? QColor(255, 80, 60) : QColor(200, 200, 200), 11, 700, false);
}

void MainWindow::renderKillFeed()
{
    text(W - 220, 8, 204, 18, Qt::AlignRight, QString("SCORE  %1").arg(m_gs.score), QColor(255, 210, 140), 15, 800);
    text(W - 220, 26, 204, 14, Qt::AlignRight, QString("%1 FPS").arg(int(m_fps)), QColor(140, 140, 140), 10, 500, false);
    int y = 46;
    for (const auto &kf : m_killFeed) {
        float t = std::min(1.f, kf.life / 0.5f);
        int a = int(255 * t);
        m_cv.blendRect(W - 226, y, 210, 22, withAlpha(UI_BG, int(170 * t)));
        m_cv.blendRect(W - 226, y, 3, 22, withAlpha(kf.head ? RED : AMBER, a));
        text(W - 218, y, 200, 22, Qt::AlignLeft | Qt::AlignVCenter, kf.text, kf.head ? QColor(255, 120, 100, a) : QColor(240, 240, 240, a), 12, 700, false);
        y += 25;
    }
}

void MainWindow::renderLevelBanner()
{
    if (!m_gs.levelComplete) return;

    // Dark tactical overlay covering entire view
    m_cv.blendRect(0, 0, W, H, withAlpha(0x06090D, 225));

    // Top Header Banner
    m_cv.blendRect(0, 14, W, 74, withAlpha(UI_BG, 200));
    m_cv.blendRect(0, 14, W, 2, withAlpha(AMBER, 255));
    m_cv.blendRect(0, 88, W, 2, withAlpha(AMBER, 255));

    text(0, 18, W, 34, Qt::AlignCenter, QString("ZONE %1 CLEARED").arg(m_gs.level), QColor(255, 215, 80), 30, 900);
    text(0, 54, W, 20, Qt::AlignCenter,
         QString("ZONE CLEAR BONUS +%1      SCORE %2      HEADSHOTS %3")
             .arg(500 * m_gs.level).arg(m_gs.score).arg(m_gs.headshots),
         QColor(220, 225, 230), 12, 700);

    if (!m_upgradePicked) {
        text(0, 94, W, 22, Qt::AlignCenter,
             QString("FIELD SUPPLY DROP  -  CHOOSE 1 WEAPON UPGRADE  [ DECISION TIME: %1s ]")
                 .arg(std::max(1, int(m_upgradeDecisionTimer) + 1)),
             QColor(255, 220, 90), 13, 800);
    } else {
        text(0, 94, W, 22, Qt::AlignCenter,
             QString("FIELD SUPPLY DROP  -  ATTACHMENT CONFIGURED  [ DEPLOYING IN %1s ]")
                 .arg(QString::number(std::max(0.1f, m_deployTimer), 'f', 1)),
             QColor(100, 255, 140), 13, 800);
    }

    // 3 Upgrade Cards
    int cw = 232, ch = 280, cy = 122;
    int spacing = 18;
    int totalW = 3 * cw + 2 * spacing;
    int startX = (W - totalW) / 2;

    for (int i = 0; i < int(m_upgradeCards.size()) && i < 3; i++) {
        const UpgradeCard &card = m_upgradeCards[i];
        int cx = startX + i * (cw + spacing);
        bool selected = (m_selectedUpgradeCard == i);

        // Card backdrop panel
        panel(cx, cy, cw, ch, selected, selected ? GREEN : 0xFF354050);

        // Header key button: [ 1 ], [ 2 ], [ 3 ]
        uint32_t keyBtnCol = selected ? GREEN : AMBER;
        m_cv.fillRect(cx + 12, cy + 12, 42, 22, keyBtnCol);
        text(cx + 12, cy + 12, 42, 22, Qt::AlignCenter, QString("[%1]").arg(i + 1), QColor(15, 15, 15), 12, 900, false);

        if (selected) {
            m_cv.blendRect(cx + cw - 95, cy + 12, 85, 22, withAlpha(GREEN, 220));
            text(cx + cw - 95, cy + 12, 85, 22, Qt::AlignCenter, "EQUIPPED", QColor(10, 10, 10), 11, 800, false);
        }

        // Weapon icon if weapon >= 0
        if (card.weapon >= 0 && card.weapon < WPN_COUNT) {
            const GunModel &M = *GUN_MODELS[card.weapon];
            float sc = 70.f / M.length;
            if (card.weapon == WPN_PISTOL) sc = 0.85f;
            else if (card.weapon == WPN_KNIFE) sc = 0.90f;
            drawGunFlat(m_cv, M, cx + cw / 2 - 30, cy + 62 + (M.hMax + M.hMin) * 0.5f * sc, sc, true, withAlpha(0xFFFFFFFF, selected ? 240 : 160));
        }

        // Title
        text(cx + 10, cy + 98, cw - 20, 26, Qt::AlignCenter, card.title, selected ? QColor(255, 225, 90) : QColor(240, 240, 240), 13, 800);

        // Description (Word wrapped)
        m_cv.blendRect(cx + 10, cy + 130, cw - 20, 78, withAlpha(0, 130));
        text(cx + 14, cy + 134, cw - 28, 70, Qt::AlignLeft | Qt::TextWordWrap, card.desc, QColor(185, 195, 205), 11, 500, false);

        // Stat bonus plate at bottom
        m_cv.blendRect(cx + 8, cy + ch - 54, cw - 16, 42, withAlpha(selected ? 0xFF0E3020 : 0xFF141F2C, 220));
        m_cv.blendRect(cx + 8, cy + ch - 54, cw - 16, 2, withAlpha(selected ? GREEN : 0xFF3AB4E8, 220));
        text(cx + 10, cy + ch - 50, cw - 20, 36, Qt::AlignCenter | Qt::TextWordWrap, card.bonus, selected ? QColor(100, 255, 140) : QColor(80, 210, 255), 11, 700, false);
    }

    // Bottom Action Prompt & Continue Button
    int btnW = 240, btnH = 36;
    int btnX = (W - btnW) / 2, btnY = 432;

    if (!m_upgradePicked) {
        text(0, 408, W, 18, Qt::AlignCenter,
             QString("CLICK A CARD OR PRESS [1, 2, 3] TO EQUIP  -  AUTO-SELECT IN %1s")
                 .arg(std::max(1, int(m_upgradeDecisionTimer) + 1)),
             QColor(230, 235, 240), 11, 600);

        m_cv.blendRect(btnX, btnY, btnW, btnH, withAlpha(0x1B2635, 230));
        m_cv.blendRect(btnX, btnY, btnW, 2, withAlpha(AMBER, 220));
        m_cv.blendRect(btnX, btnY + btnH - 2, btnW, 2, withAlpha(AMBER, 220));
        m_cv.blendRect(btnX, btnY, 2, btnH, withAlpha(AMBER, 220));
        m_cv.blendRect(btnX + btnW - 2, btnY, 2, btnH, withAlpha(AMBER, 220));
        text(btnX, btnY + 7, btnW, 22, Qt::AlignCenter, "CONTINUE >>  [SPACE]", QColor(255, 220, 100), 13, 800);
    } else {
        text(0, 408, W, 18, Qt::AlignCenter,
             QString("UPGRADE SECURED!  DEPLOYING TO ZONE %1 IN %2s  (CLICK TO SKIP)")
                 .arg(m_gs.level + 1).arg(QString::number(std::max(0.1f, m_deployTimer), 'f', 1)),
             QColor(100, 255, 140), 11, 700);

        bool pulse = (int(m_time * 4.f) & 1) == 0;
        uint32_t bgCol = pulse ? 0xFF1D6638 : 0xFF144D29;
        uint32_t bdrCol = 0xFF45F07E;
        m_cv.blendRect(btnX, btnY, btnW, btnH, withAlpha(bgCol, 240));
        m_cv.blendRect(btnX, btnY, btnW, 2, withAlpha(bdrCol, 255));
        m_cv.blendRect(btnX, btnY + btnH - 2, btnW, 2, withAlpha(bdrCol, 255));
        m_cv.blendRect(btnX, btnY, 2, btnH, withAlpha(bdrCol, 255));
        m_cv.blendRect(btnX + btnW - 2, btnY, 2, btnH, withAlpha(bdrCol, 255));
        text(btnX, btnY + 7, btnW, 22, Qt::AlignCenter, "DEPLOY NOW >>  [SPACE]", QColor(255, 255, 255), 13, 900);
    }
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
    text(x, y + 44, w, 16, Qt::AlignCenter, QString("ZONE %1 SURVIVAL LOADOUT  -  ALL UPGRADES ACTIVE").arg(m_gs.level), QColor(180, 190, 200), 11, 600);

    for (int i = 0; i < WPN_COUNT; i++) {
        int ry = y + 70 + i * 76;
        bool owned = pl.owned[i];
        m_cv.blendRect(x + 20, ry, w - 40, 68, withAlpha(0xFFFFFFFF, owned ? 16 : 6));
        m_cv.blendRect(x + 20, ry, 4, 68, withAlpha(i == pl.curWeapon ? AMBER : owned ? 0xFF3AB4E8 : 0xFF555555, 240));

        // Weapon name & stats
        text(x + 32, ry + 6, 160, 20, Qt::AlignLeft, WEAPONS[i].name, owned ? QColor(255, 255, 255) : QColor(120, 120, 120), 13, 800);
        text(x + 32, ry + 26, 160, 16, Qt::AlignLeft,
             owned ? QString("DMG: %1  MAG: %2").arg(effectiveDamage(i)).arg(effectiveMagSize(i)) : "LOCKED / UNACQUIRED",
             owned ? QColor(200, 210, 220) : QColor(100, 100, 100), 10, 600, false);

        // Attachment slot badges
        int ax = x + 195;
        auto drawSlot = [&](const char *label, bool active, uint32_t col) {
            m_cv.blendRect(ax, ry + 12, 78, 44, withAlpha(active ? col : 0xFF141920, active ? 210 : 130));
            m_cv.blendRect(ax, ry + 12, 78, 2, withAlpha(active ? col : 0xFF2A3542, 255));
            text(ax, ry + 16, 78, 14, Qt::AlignCenter, label, active ? QColor(255, 255, 255) : QColor(90, 95, 100), 9, 800, false);
            text(ax, ry + 32, 78, 16, Qt::AlignCenter, active ? "ACTIVE" : "NONE", active ? QColor(100, 255, 140) : QColor(80, 85, 90), 8, 700, false);
            ax += 84;
        };

        if (i == WPN_KNIFE) {
            drawSlot("BALANCED HILT", pl.upgradeCompensator[i], 0xFF3AB4E8);
            drawSlot("SERRATED EDGE", pl.upgradeAmmo[i], AMBER);
            drawSlot("STILETTO TIP", pl.upgradeSpecial[i], 0xFFFF7040);
        } else {
            drawSlot("EXTENDED MAG", pl.upgradeExtMag[i], GREEN);
            drawSlot(i == WPN_SHOTGUN ? "CHOKE" : "COMPENSATOR", pl.upgradeCompensator[i], 0xFF3AB4E8);
            drawSlot("AP / +P AMMO", pl.upgradeAmmo[i], AMBER);
            const char *sp = (i == WPN_RIFLE ? "ACOG 3X" : i == WPN_SHOTGUN ? "RAPID PUMP" : "HAIR TRIG");
            drawSlot(sp, pl.upgradeSpecial[i], 0xFFFF7040);
        }
    }

    text(x, y + h - 30, w, 18, Qt::AlignCenter, "[U] OR [ESC] RESUME COMBAT      [SPACE] CLOSE", QColor(255, 200, 100), 12, 700);
}

void MainWindow::renderPauseMenu()
{
    if (!m_paused) return;
    for (int i = 0; i < W * H; i++) { uint32_t c = m_pixels[i]; int l = (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 9; m_pixels[i] = rgb(l, l, l + 4); }
    const int w = 380, h = 300, x = W / 2 - w / 2, y = H / 2 - h / 2;
    panel(x, y, w, h, true, AMBER);
    text(x, y + 18, w, 44, Qt::AlignCenter, "PAUSED", QColor(255, 255, 255), 36, 900);
    struct Opt { const char *key, *label; uint32_t c; };
    const Opt opts[4] = { {"R", "RESUME", AMBER}, {"M", "MAIN MENU", 0xFFDDDDDD}, {"Q", "QUIT TO DESKTOP", 0xFFDDDDDD}, {"ESC", "FORCE QUIT", RED} };
    for (int i = 0; i < 4; i++) {
        int oy = y + 82 + i * 46;
        m_cv.blendRect(x + 40, oy, w - 80, 38, withAlpha(0xFFFFFFFF, i == 0 ? 22 : 10));
        m_cv.fillRect(x + 50, oy + 8, 44, 22, opts[i].c);
        text(x + 50, oy + 8, 44, 22, Qt::AlignCenter, opts[i].key, QColor(15, 15, 15), 12, 900, false);
        text(x + 108, oy, w - 150, 38, Qt::AlignLeft | Qt::AlignVCenter, opts[i].label, QColor(235, 235, 235), 16, 700);
    }
    text(x, y + h - 30, w, 18, Qt::AlignCenter, "SCROLL raster zoom   [ ] pixel blocks   V free-aim toggle", QColor(150, 150, 150), 11, 500);
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
                         {"SCORE", QString::number(m_gs.score)}, {"ZONE", QString::number(m_gs.level)}, {"SECRETS", QString("%1 / %2").arg(m_gs.secretsFound).arg(m_gs.secretsTotal)} };
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
    float savedPitch = m_pitch, savedFov = m_adsFOVMult, savedFoot = m_footZ;
    m_gs = GameState{};
    Player &pl = m_gs.player;
    pl.posX = m_parsed[m_mapChoice].camX; pl.posY = m_parsed[m_mapChoice].camY;
    pl.dirX = std::cos(m_menuCamAng); pl.dirY = std::sin(m_menuCamAng);
    pl.planeX = -pl.dirY * 0.66f; pl.planeY = pl.dirX * 0.66f;
    m_pitch = 0.04f; m_adsFOVMult = 1.f; m_footZ = floorH(pl.posX, pl.posY) + 0.15f;
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
    m_gs = saved; m_pitch = savedPitch; m_adsFOVMult = savedFov; m_footZ = savedFoot;

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
    m_cv.blendRect(16, H - 28, 88, 22, withAlpha(0xFFFFFFFF, 18));
    text(16, H - 28, 88, 22, Qt::AlignCenter, "< BACK", QColor(220, 220, 220), 12, 700, false);
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

    const char *opts[3] = { "NEW GAME", "DEBUG RANGE", "EXIT" };
    for (int i = 0; i < 3; i++) {
        bool sel = m_menuCursor == i;
        int x = 48, y = 270 + i * 54, w = 300, h = 46;
        m_menuRects.push_back({x, y, w, h, i});
        if (sel) {
            m_cv.hGrad(x, y, w, h, withAlpha(AMBER, 70), withAlpha(AMBER, 0));
            m_cv.fillRect(x, y, 4, h, AMBER);
        } else m_cv.hGrad(x, y, w, h, withAlpha(0xFFFFFFFF, 14), withAlpha(0xFFFFFFFF, 0));
        text(x + 22, y, w, h, Qt::AlignLeft | Qt::AlignVCenter, opts[i], sel ? QColor(255, 255, 255) : QColor(175, 175, 175), 20, sel ? 800 : 600);
        if (sel) text(x + w - 60, y, 50, h, Qt::AlignRight | Qt::AlignVCenter, ">", qc(AMBER), 20, 800, false);
    }

    // Controls card
    const int cx = W - 300, cy = 250, cw = 270, ch = 270;
    panel(cx, cy, cw, ch, false, AMBER);
    text(cx + 16, cy + 10, cw, 18, Qt::AlignLeft, "FIELD MANUAL", qc(AMBER), 13, 800, false);
    const char *rows[][2] = { {"WASD", "Move"}, {"MOUSE", "Free-aim crosshair"}, {"LMB / F", "Fire"}, {"RMB / TAB", "Aim / scope"},
                              {"WHEEL", "Scope zoom (rifle)"}, {"R", "Reload"}, {"1  2  3", "Switch weapon"}, {"SHIFT", "Sprint / hold breath"},
                              {"SPACE", "Jump (ledges, acid)"}, {"E", "Door / search walls"}, {"V  /  C", "Aim mode / recentre"} };
    for (int i = 0; i < 11; i++) {
        int ry = cy + 36 + i * 20;
        m_cv.blendRect(cx + 16, ry + 2, 80, 16, withAlpha(0xFFFFFFFF, 20));
        text(cx + 16, ry + 2, 80, 16, Qt::AlignCenter, rows[i][0], QColor(235, 235, 235), 10, 800, false);
        text(cx + 106, ry, cw - 116, 20, Qt::AlignLeft | Qt::AlignVCenter, rows[i][1], QColor(190, 190, 190), 12, 500, false);
    }
    text(cx, cy + ch + 6, cw, 16, Qt::AlignCenter, "Headshots deal 2-3x damage", qc(AMBER), 12, 700);

    m_cv.blendRect(0, H - 30, W, 30, withAlpha(0, 160));
    text(16, H - 28, W - 32, 26, Qt::AlignLeft | Qt::AlignVCenter, "UP/DOWN select   ENTER confirm   mouse supported", QColor(160, 160, 160), 12, 500, false);
    text(16, H - 28, W - 32, 26, Qt::AlignRight | Qt::AlignVCenter, "SCROLL raster zoom   [ ] pixel blocks", QColor(160, 160, 160), 12, 500, false);
}

void MainWindow::renderMapSelect()
{
    menuHeader("STEP 1 / 3", "SELECT DEPLOYMENT ZONE");
    static const char *TAGS[3] = { "OVERCAST  |  2 SECRETS", "MIDDAY HILLS  |  1 SECRET", "DUSK  |  2 SECRETS" };
    for (int i = 0; i < 3; i++) {
        bool sel = m_mapChoice == i;
        int w = 232, h = 262, x = 30 + i * (w + 22), y = H - 34 - h - 22 - (sel ? 8 : 0);
        m_menuRects.push_back({x, y, w, h, i});
        panel(x, y, w, h, sel, AMBER);
        // top-down layout preview, tinted by theme
        const ParsedMap &pm = m_parsed[i];
        const int ts = 3, px0 = x + (w - MAP_W * ts) / 2, py0 = y + 12;
        uint32_t wallC = i == 0 ? rgb(140, 146, 140) : i == 1 ? rgb(200, 176, 130) : rgb(120, 110, 130);
        uint32_t floorC = i == 0 ? rgb(40, 44, 42) : i == 1 ? rgb(52, 66, 36) : rgb(36, 30, 40);
        for (int my = 0; my < MAP_H; my++) for (int mx = 0; mx < MAP_W; mx++) {
            int t = pm.type[my][mx];
            uint32_t c = t == CELL_DOOR ? rgb(170, 110, 50) : t == CELL_SECRET ? wallC : t ? wallC
                       : pm.acid[my][mx] ? rgb(90, 190, 50) : scaleCol(floorC, 1.f + pm.plat[my][mx] * 0.9f);
            m_cv.fillRect(px0 + mx * ts, py0 + my * ts, ts, ts, c);
        }
        m_cv.fillCircle(px0 + int(pm.spawnX * ts), py0 + int(pm.spawnY * ts), 2, AMBER);
        if (sel) {
            int scan = py0 + int(std::fmod(m_time * 40.f, float(MAP_H * ts)));
            m_cv.blendRect(px0, scan, MAP_W * ts, 2, withAlpha(AMBER, 120));
        }
        text(x, y + 136, w, 30, Qt::AlignCenter, MAP_NAMES[i], sel ? QColor(255, 255, 255) : QColor(200, 200, 200), 22, 900);
        text(x, y + 164, w, 16, Qt::AlignCenter, TAGS[i], qc(sel ? AMBER : AMBER_DIM), 11, 800, false);
        text(x + 14, y + 184, w - 28, 50, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, MAP_DESCS[i], QColor(180, 180, 180), 12, 500, false);
        text(x, y + h - 22, w, 16, Qt::AlignCenter, QString("[%1]").arg(i + 1), QColor(130, 130, 130), 11, 700, false);
    }
    menuFooter("LEFT/RIGHT browse   ENTER or 1-3 select   ESC back");
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
