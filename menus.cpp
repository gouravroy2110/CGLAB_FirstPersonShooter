// =============================================================================
//  menus.cpp  -  Settings and the Arsenal browser
//
//  Settings are saved with QSettings (sensitivity, field of view, volume,
//  camera mode, free aim, squad bots, crosshair colour, FPS counter).
//  The Arsenal shows every weapon as a slowly turning 3D model drawn by the
//  shared triangle rasterizer into its own depth buffer, beside stat bars.
// =============================================================================
#include "mainwindow.h"
#include "gunbuild.h"
#include "raster3d.h"
#include <QSettings>
#include <QKeySequence>
#include <cmath>
#include <algorithm>

using namespace rs;

static const uint32_t UI_AMBER = 0xFFFFB43A, UI_BG = 0xFF0E1216;
static inline QColor qc(uint32_t c, int a = 255) { return QColor(R(c), G(c), B(c), a); }

// ─────────────────────────────────────────────────────────────────────────────
//  Settings
// ─────────────────────────────────────────────────────────────────────────────
static const char *const CROSS_NAMES[4] = { "WHITE", "GREEN", "CYAN", "YELLOW" };
static const uint32_t CROSS_COLORS[4] = { 0xFFFFFFFF, 0xFF5CFF7A, 0xFF4AE0FF, 0xFFFFE04A };

uint32_t MainWindow::crosshairColor() const { return CROSS_COLORS[clampi(m_crossColor, 0, 3)]; }

void MainWindow::initDefaultKeyBinds()
{
    m_keyBinds[ACT_MOVE_FWD]    = Qt::Key_W;
    m_keyBinds[ACT_MOVE_BACK]   = Qt::Key_S;
    m_keyBinds[ACT_MOVE_LEFT]   = Qt::Key_A;
    m_keyBinds[ACT_MOVE_RIGHT]  = Qt::Key_D;
    m_keyBinds[ACT_JUMP]        = Qt::Key_Space;
    m_keyBinds[ACT_SPRINT]      = Qt::Key_Shift;
    m_keyBinds[ACT_FIRE]        = Qt::Key_F;
    m_keyBinds[ACT_ADS]         = Qt::Key_Tab;
    m_keyBinds[ACT_RELOAD]      = Qt::Key_R;
    m_keyBinds[ACT_MELEE]       = Qt::Key_Q;
    m_keyBinds[ACT_INSPECT]     = Qt::Key_H;
    m_keyBinds[ACT_WEAPON_STATS]= Qt::Key_I;
    m_keyBinds[ACT_INTERACT]    = Qt::Key_E;
    m_keyBinds[ACT_THROW]       = Qt::Key_G;
    m_keyBinds[ACT_CYCLE_THROW] = Qt::Key_T;
    m_keyBinds[ACT_GADGET]      = Qt::Key_Z;
    m_keyBinds[ACT_FLASHLIGHT]  = Qt::Key_L;
    m_keyBinds[ACT_CAMERA]      = Qt::Key_V;
    m_keyBinds[ACT_SHOULDER]    = Qt::Key_X;
    m_keyBinds[ACT_ARMORY]      = Qt::Key_U;
    m_keyBinds[ACT_SLOT1]       = Qt::Key_1;
    m_keyBinds[ACT_SLOT2]       = Qt::Key_2;
    m_keyBinds[ACT_SLOT3]       = Qt::Key_3;
    m_keyBinds[ACT_SLOT4]       = Qt::Key_4;
}

QString MainWindow::keyName(int k)
{
    switch (k) {
    case Qt::Key_Space:      return "SPACE";
    case Qt::Key_Shift:      return "SHIFT";
    case Qt::Key_Control:    return "CTRL";
    case Qt::Key_Alt:        return "ALT";
    case Qt::Key_Tab:        return "TAB";
    case Qt::Key_Return:
    case Qt::Key_Enter:      return "ENTER";
    case Qt::Key_Backspace:  return "BKSP";
    case Qt::Key_CapsLock:   return "CAPS";
    case Qt::Key_Escape:     return "ESC";
    case Qt::Key_Up:         return "UP";
    case Qt::Key_Down:       return "DOWN";
    case Qt::Key_Left:       return "LEFT";
    case Qt::Key_Right:      return "RIGHT";
    case Qt::Key_PageUp:     return "PGUP";
    case Qt::Key_PageDown:   return "PGDN";
    case Qt::Key_Home:       return "HOME";
    case Qt::Key_End:        return "END";
    case Qt::Key_Insert:     return "INS";
    case Qt::Key_Delete:     return "DEL";
    default: {
        if (k >= Qt::Key_A && k <= Qt::Key_Z) return QString(QChar('A' + (k - Qt::Key_A)));
        if (k >= Qt::Key_0 && k <= Qt::Key_9) return QString(QChar('0' + (k - Qt::Key_0)));
        QString s = QKeySequence(k).toString().toUpper();
        return s.isEmpty() ? QString("KEY_%1").arg(k) : s;
    }
    }
}

void MainWindow::openSettings(GameScreen fromScreen)
{
    m_settingsReturnScreen = fromScreen;
    m_settingsWasPaused = m_paused;
    m_paused = true;
    m_screen = SCR_SETTINGS;
    m_settingsTab = 0;
    m_settingsRow = 0;
    m_controlsRow = 0;
    m_rebindingAction = -1;
    m_keys.clear();
    m_lmb = false;
    m_gs.player.adsMode = false;
    sfx(SND_UI_SELECT, 0.6f);
}

void MainWindow::loadSettings()
{
    initDefaultKeyBinds();
    loadLayoutRows(true);                                   // saved editor maps replace the built-in ones
    QSettings s("RasterFPS", "RasterFPS");
    m_mouseSens  = clampf(s.value("mouseSens", 1.0).toFloat(), 0.2f, 6.0f);
    m_fovDeg     = clampi(s.value("fov", 67).toInt(), 55, 100);
    m_volume     = clampi(s.value("volume", 80).toInt(), 0, 100);
    m_tpp        = s.value("thirdPerson", false).toBool();
    m_freeAim    = s.value("freeAim", false).toBool();
    m_squadSize  = clampi(s.value("squad", 0).toInt(), 0, 3);
    m_crossColor = clampi(s.value("crosshair", 0).toInt(), 0, 3);
    m_showFps    = s.value("showFps", true).toBool();

    static const char *const ACTION_SLUGS[ACT_COUNT] = {
        "move_fwd", "move_back", "move_left", "move_right", "jump", "sprint",
        "fire", "ads", "reload", "melee", "inspect", "weapon_stats", "interact",
        "throw", "cycle_throw", "gadget", "flashlight", "camera", "shoulder",
        "armory", "slot1", "slot2", "slot3", "slot4"
    };

    if (s.contains("keybind_v2")) {
        for (int i = 0; i < ACT_COUNT; i++) {
            QVariant v = s.value(QString("keybind_%1").arg(ACTION_SLUGS[i]));
            if (v.isValid()) m_keyBinds[i] = v.toInt();
        }
    } else {
        // Migrate legacy numeric bindings from 22-action layout (pre-stats/armory)
        for (int i = 0; i <= ACT_INSPECT; i++) {
            QVariant v = s.value(QString("keybind_%1").arg(i));
            if (v.isValid()) m_keyBinds[i] = v.toInt();
        }
        m_keyBinds[ACT_WEAPON_STATS] = Qt::Key_I;
        m_keyBinds[ACT_ARMORY]       = Qt::Key_U;
        // Old indices 11..17 were interact, throw, cycle_throw, gadget, flashlight, camera, shoulder
        int oldIndices[7] = { ACT_INTERACT, ACT_THROW, ACT_CYCLE_THROW, ACT_GADGET, ACT_FLASHLIGHT, ACT_CAMERA, ACT_SHOULDER };
        for (int j = 0; j < 7; j++) {
            QVariant v = s.value(QString("keybind_%1").arg(11 + j));
            if (v.isValid()) m_keyBinds[oldIndices[j]] = v.toInt();
        }
        // Old indices 18..21 were slots 1..4
        for (int j = 0; j < 4; j++) {
            QVariant v = s.value(QString("keybind_%1").arg(18 + j));
            if (v.isValid()) m_keyBinds[ACT_SLOT1 + j] = v.toInt();
        }
        saveSettings();
    }
    applySettings();
}

void MainWindow::saveSettings() const
{
    if (m_testMode) return;                               // the snapshot tests never touch saved options
    QSettings s("RasterFPS", "RasterFPS");
    s.setValue("mouseSens", m_mouseSens);
    s.setValue("fov", m_fovDeg);
    s.setValue("volume", m_volume);
    s.setValue("thirdPerson", m_tpp);
    s.setValue("freeAim", m_freeAim);
    s.setValue("squad", m_squadSize);
    s.setValue("crosshair", m_crossColor);
    s.setValue("showFps", m_showFps);
    s.setValue("keybind_v2", true);

    static const char *const ACTION_SLUGS[ACT_COUNT] = {
        "move_fwd", "move_back", "move_left", "move_right", "jump", "sprint",
        "fire", "ads", "reload", "melee", "inspect", "weapon_stats", "interact",
        "throw", "cycle_throw", "gadget", "flashlight", "camera", "shoulder",
        "armory", "slot1", "slot2", "slot3", "slot4"
    };

    for (int i = 0; i < ACT_COUNT; i++) {
        s.setValue(QString("keybind_%1").arg(ACTION_SLUGS[i]), m_keyBinds[i]);
        s.remove(QString("keybind_%1").arg(i));
    }
}

// Field of view -> length of the camera plane (the renderer's FOV), volume -> mixer
void MainWindow::applySettings()
{
    m_planeLen = std::tan(float(m_fovDeg) * 3.14159265f / 360.f);
    Player &pl = m_gs.player;
    float l = std::sqrt(pl.planeX * pl.planeX + pl.planeY * pl.planeY);
    if (l > 1e-4f) { pl.planeX *= m_planeLen / l; pl.planeY *= m_planeLen / l; }
    if (m_snd) m_snd->setMasterVolume(m_volume / 100.f);
}

int MainWindow::settingsRows() const { return 10; }

void MainWindow::changeSetting(int row, int dir)
{
    switch (row) {
    case 0: m_mouseSens = clampf(std::round((m_mouseSens + 0.1f * dir) * 10.f) / 10.f, 0.2f, 6.0f); break;
    case 1: m_fovDeg = clampi(m_fovDeg + 3 * dir, 55, 100); break;
    case 2: m_volume = clampi(m_volume + 5 * dir, 0, 100); break;
    case 3: m_tpp = !m_tpp; break;
    case 4: m_freeAim = !m_freeAim; break;
    case 5: m_squadSize = (m_squadSize + dir + 4) % 4; break;
    case 6: m_crossColor = (m_crossColor + dir + 4) % 4; break;
    case 7: m_showFps = !m_showFps; break;
    case 8:
        m_settingsTab = 1;
        m_controlsRow = 0;
        sfx(SND_UI_SELECT, 0.5f);
        return;
    case 9:
        if (dir != 0) {
            m_mouseSens = 1.f; m_fovDeg = 67; m_volume = 80; m_tpp = false;
            m_freeAim = false; m_squadSize = 0; m_crossColor = 0; m_showFps = true;
            sfx(SND_UPGRADE, 0.6f);
        }
        break;
    }
    applySettings();
    saveSettings();
    if (row == 2) sfx(SND_UI_SELECT, 0.6f);
    else sfx(SND_UI_MOVE, 0.4f);
}

void MainWindow::renderSettings()
{
    menuHeader("OPTIONS", "SETTINGS");

    // Top Tabs: [ 1  GENERAL ] (id 50) and [ 2  CONTROLS ] (id 51)
    const int tabY = 64, tabH = 26;
    int tab0X = 220, tab0W = 170;
    int tab1X = 410, tab1W = 170;

    m_menuRects.push_back({tab0X, tabY, tab0W, tabH, 50});
    m_menuRects.push_back({tab1X, tabY, tab1W, tabH, 51});

    QPointF mf = widgetToFrame(m_mouseWidget);
    bool hoverTab0 = (mf.x() >= tab0X && mf.x() < tab0X + tab0W && mf.y() >= tabY && mf.y() < tabY + tabH);
    bool hoverTab1 = (mf.x() >= tab1X && mf.x() < tab1X + tab1W && mf.y() >= tabY && mf.y() < tabY + tabH);

    bool t0Sel = (m_settingsTab == 0);
    m_cv.blendRect(tab0X, tabY, tab0W, tabH, withAlpha(t0Sel ? UI_AMBER : 0xFFFFFFFF, t0Sel ? 70 : (hoverTab0 ? 30 : 15)));
    if (t0Sel) m_cv.fillRect(tab0X, tabY + tabH - 3, tab0W, 3, UI_AMBER);
    m_cv.rectOutline(tab0X, tabY, tab0W, tabH, withAlpha(t0Sel ? UI_AMBER : 0xFFFFFFFF, t0Sel ? 180 : 40));
    text(tab0X, tabY, tab0W, tabH, Qt::AlignCenter, "1  GENERAL", t0Sel ? QColor(255, 255, 255) : QColor(170, 175, 180), 12, 800, false);

    bool t1Sel = (m_settingsTab == 1);
    m_cv.blendRect(tab1X, tabY, tab1W, tabH, withAlpha(t1Sel ? UI_AMBER : 0xFFFFFFFF, t1Sel ? 70 : (hoverTab1 ? 30 : 15)));
    if (t1Sel) m_cv.fillRect(tab1X, tabY + tabH - 3, tab1W, 3, UI_AMBER);
    m_cv.rectOutline(tab1X, tabY, tab1W, tabH, withAlpha(t1Sel ? UI_AMBER : 0xFFFFFFFF, t1Sel ? 180 : 40));
    text(tab1X, tabY, tab1W, tabH, Qt::AlignCenter, "2  CONTROLS", t1Sel ? QColor(255, 255, 255) : QColor(170, 175, 180), 12, 800, false);

    if (m_settingsTab == 0) {
        const int x = 60, w = W - 120, y0 = 98, rh = 38, sp = 42;
        struct Row { const char *name; QString value; float bar; const char *help; };
        Row rows[10] = {
            {"MOUSE SENSITIVITY", QString::number(m_mouseSens, 'f', 2) + "x", (m_mouseSens - 0.2f) / 5.8f, "Adjust aiming speed (higher = faster, lower = precision)"},
            {"FIELD OF VIEW", QString("%1 DEG").arg(m_fovDeg), (m_fovDeg - 55) / 45.f, "Horizontal view angle of the raster camera"},
            {"MASTER VOLUME", QString("%1%").arg(m_volume), m_volume / 100.f, "Adjust all sound effects and synthesizer volume"},
            {"CAMERA", m_tpp ? "THIRD PERSON" : "FIRST PERSON", -1.f, "V switches in game; X swaps shoulder"},
            {"FREE AIM", m_freeAim ? "ON (DEAD ZONE)" : "OFF (DIRECT AIM)", -1.f, m_freeAim ? "Crosshair moves inside dead zone before camera turns" : "Camera turns immediately with mouse (recommended)"},
            {"SQUAD BOTS", m_squadSize ? QString::number(m_squadSize) : QString("OFF"), m_squadSize / 3.f, "AI teammates that follow you and fight"},
            {"CROSSHAIR COLOUR", CROSS_NAMES[m_crossColor], -1.f, "Pick one that stands out for your eyes"},
            {"SHOW FPS", m_showFps ? "ON" : "OFF", -1.f, "Frame rate under the score"},
            {"CONFIGURE CONTROLS", "ENTER / CLICK ->", -1.f, "Customize keyboard bindings for movement, combat, and actions"},
            {"RESET GENERAL DEFAULTS", "ENTER / CLICK", -1.f, "Restore default sensitivity, volume, and options"},
        };
        for (int i = 0; i < 10; i++) {
            int y = y0 + i * sp;
            bool sel = m_settingsRow == i;
            m_menuRects.push_back({x, y, w, rh, i});
            m_cv.blendRect(x, y, w, rh, withAlpha(UI_BG, sel ? 225 : 170));
            if (sel) { m_cv.fillRect(x, y, 4, rh, UI_AMBER); m_cv.rectOutline(x, y, w, rh, withAlpha(UI_AMBER, 160)); }
            text(x + 18, y + 3, 300, 18, Qt::AlignLeft, rows[i].name, sel ? QColor(255, 255, 255) : QColor(200, 200, 200), 13, 800, false);
            text(x + 18, y + 21, 420, 14, Qt::AlignLeft, rows[i].help, QColor(130, 140, 150), 9, 500, false);
            int vx = x + w - 300;
            if (rows[i].bar >= 0.f) {
                m_cv.blendRect(vx, y + 15, 170, 8, withAlpha(0xFFFFFFFF, 30));
                m_cv.fillRect(vx, y + 15, int(170 * clampf(rows[i].bar, 0.f, 1.f)), 8, sel ? UI_AMBER : 0xFF9AA4AE);
            }
            if (i == 6) m_cv.fillRect(vx + 150, y + 11, 16, 16, CROSS_COLORS[m_crossColor]);
            text(vx + 156, y, 120, rh, Qt::AlignRight | Qt::AlignVCenter, rows[i].value, sel ? qc(UI_AMBER) : QColor(220, 220, 220), 13, 800, false);
            if (sel && i < 8) { text(vx - 26, y, 20, rh, Qt::AlignCenter, "<", qc(UI_AMBER), 14, 900, false); text(x + w - 18, y, 16, rh, Qt::AlignCenter, ">", qc(UI_AMBER), 14, 900, false); }
        }
        menuFooter("UP/DOWN choose   LEFT/RIGHT change   [2] or TAB controls   [O] / ESC back");
    } else {
        static const char *const ACTION_NAMES[ACT_COUNT] = {
            "MOVE FORWARD",
            "MOVE BACKWARD",
            "STRAFE LEFT",
            "STRAFE RIGHT",
            "JUMP",
            "SPRINT / BREATH",
            "FIRE WEAPON",
            "AIM DOWN SIGHTS",
            "RELOAD",
            "QUICK MELEE",
            "INSPECT WEAPON",
            "WEAPON STATS",
            "INTERACT / USE",
            "THROW GRENADE",
            "CYCLE GRENADE",
            "DEPLOY GADGET",
            "FLASHLIGHT",
            "CAMERA 1ST/3RD",
            "SWAP SHOULDER",
            "ARMORY / UPGRADES",
            "PRIMARY WEAPON",
            "SIDEARM",
            "HEAVY / MELEE",
            "SPECIAL WEAPON"
        };

        const int col1X = 40, col2X = 415, colW = 345, y0 = 96, rh = 30, sp = 34;

        for (int i = 0; i < ACT_COUNT; i++) {
            int cx = (i < 12) ? col1X : col2X;
            int cy = y0 + (i % 12) * sp;
            bool sel = (m_controlsRow == i);
            bool rebinding = (m_rebindingAction == i);

            m_menuRects.push_back({cx, cy, colW, rh, 100 + i});

            m_cv.blendRect(cx, cy, colW, rh, withAlpha(UI_BG, sel ? 220 : 160));
            if (sel) {
                m_cv.fillRect(cx, cy, 3, rh, UI_AMBER);
                m_cv.rectOutline(cx, cy, colW, rh, withAlpha(UI_AMBER, 140));
            }

            text(cx + 12, cy, colW - 110, rh, Qt::AlignLeft | Qt::AlignVCenter, ACTION_NAMES[i],
                 sel ? QColor(255, 255, 255) : QColor(200, 205, 210), 10, 700, false);

            int bx = cx + colW - 95, by = cy + 4, bw = 85, bh = 22;
            if (rebinding) {
                m_cv.fillRect(bx, by, bw, bh, UI_AMBER);
                text(bx, by, bw, bh, Qt::AlignCenter, "PRESS KEY...", QColor(10, 10, 10), 9, 900, false);
            } else {
                m_cv.blendRect(bx, by, bw, bh, withAlpha(0xFFFFFFFF, sel ? 35 : 18));
                m_cv.rectOutline(bx, by, bw, bh, withAlpha(sel ? UI_AMBER : 0xFFFFFFFF, sel ? 170 : 45));
                text(bx, by, bw, bh, Qt::AlignCenter, keyName(m_keyBinds[i]),
                     sel ? qc(UI_AMBER) : QColor(220, 225, 230), 10, 800, false);
            }
        }

        // Reset Defaults Button
        int rbx = 240, rby = y0 + 12 * sp + 4, rbw = 320, rbh = 28;
        bool resetSel = (m_controlsRow == ACT_COUNT);
        m_menuRects.push_back({rbx, rby, rbw, rbh, 200});
        m_cv.blendRect(rbx, rby, rbw, rbh, withAlpha(UI_BG, resetSel ? 230 : 170));
        m_cv.rectOutline(rbx, rby, rbw, rbh, withAlpha(resetSel ? UI_AMBER : 0xFFFFFFFF, resetSel ? 200 : 50));
        if (resetSel) m_cv.fillRect(rbx, rby, 4, rbh, UI_AMBER);
        text(rbx, rby, rbw, rbh, Qt::AlignCenter, "RESET ALL CONTROLS TO DEFAULT",
             resetSel ? qc(UI_AMBER) : QColor(200, 205, 210), 11, 800, false);

        if (m_rebindingAction >= 0) {
            menuFooter("PRESS ANY KEY TO BIND   ESC to cancel");
        } else {
            menuFooter("CLICK or ENTER to rebind   [1] or TAB general   ESC back");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Arsenal: turntable + stats for every weapon
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderArsenal()
{
    menuHeader("ARSENAL", QString("%1 WEAPONS").arg(WPN_COUNT));
    const int w = clampi(m_arsenalSel, 0, WPN_COUNT - 1);
    const WeaponDef &d = WEAPONS[w];

    // --- list (scrolls to keep the selection visible) ---------------------------------
    const int lx = 20, ly = 80, lw = 210, rowH = 19, visible = 23;
    int first = clampi(w - visible / 2, 0, std::max(0, int(WPN_COUNT) - visible));
    m_cv.blendRect(lx, ly, lw, visible * rowH + 6, withAlpha(UI_BG, 190));
    static const char *CLS[] = { "MELEE", "PISTOL", "SMG", "RIFLE", "SHOTGUN", "LMG", "SNIPER", "LAUNCHER", "ENERGY" };
    for (int i = first; i < std::min(int(WPN_COUNT), first + visible); i++) {
        int y = ly + 3 + (i - first) * rowH;
        bool sel = i == w;
        m_menuRects.push_back({lx, y, lw, rowH, i});
        if (sel) { m_cv.blendRect(lx + 2, y, lw - 4, rowH - 1, withAlpha(UI_AMBER, 60)); m_cv.fillRect(lx + 2, y, 3, rowH - 1, UI_AMBER); }
        text(lx + 10, y, 140, rowH, Qt::AlignLeft | Qt::AlignVCenter, WEAPONS[i].shortName, sel ? QColor(255, 255, 255) : QColor(185, 185, 185), 11, sel ? 800 : 600, false);
        text(lx + 118, y, 86, rowH, Qt::AlignRight | Qt::AlignVCenter, CLS[WEAPONS[i].cls], QColor(120, 130, 140), 8, 700, false);
    }

    // --- turntable -----------------------------------------------------------------------
    const int tx = 245, ty = 80, tw = 535, th = 250;
    m_cv.vGrad(tx, ty, tw, th, withAlpha(0xFF101820, 230), withAlpha(0xFF05080C, 230));
    m_cv.rectOutline(tx, ty, tw, th, withAlpha(0xFFFFFFFF, 30));
    for (int gx = tx; gx < tx + tw; gx += 24) m_cv.blendRect(gx, ty + th - 50, 1, 50, withAlpha(0xFF3AB4E8, 26));   // floor grid
    for (int gy = ty + th - 50; gy < ty + th; gy += 12) m_cv.blendRect(tx, gy, tw, 1, withAlpha(0xFF3AB4E8, 26));
    {
        const GunModel &M = gunModel(w);
        const GunGeom3D &G3 = gunGeom(w);
        std::vector<float> zb(size_t(W) * H, 0.f);
        Raster3D r{m_cv, zb.data()};
        r.fx = r.fy = 560.f; r.cx = tx + tw / 2.f; r.cy = ty + th / 2.f - 6.f;
        r.light = norm(V3{-0.4f, -0.8f, -0.45f});
        float ang = m_time * 0.6f;
        float wantLen = 0.75f;                               // every gun fills the stage the same way
        float S = wantLen / std::max(20.f, M.length);
        V3 Fd = norm(V3{std::cos(ang), 0.f, std::sin(ang)}), Ud = V3{0, -1, 0}, Rd = norm(cross(Fd, Ud));
        float tilt = 0.18f;
        Fd = rotate(Fd, Rd, tilt); Ud = rotate(Ud, Rd, tilt);
        V3 C{0.f, 0.f, 1.25f};
        float hMid = (M.hMax + M.hMin) * 0.5f;
        V3 O = C - Fd * (M.length * 0.5f * S) - Ud * (hMid * S);
        auto map = [&](float u, float h, float wz) { return O + Fd * (u * S) + Ud * (h * S) + Rd * (wz * S); };
        for (int i = 0; i < M.count; i++) {
            const GunPart &part = M.parts[i];
            float width = i < G3.count ? G3.partW[i] : 4.f;
            bool glass = (part.flags & GP_GLASS) != 0;
            r.emissive = (part.flags & GP_GLOW) != 0;
            r.extrude(part.pts, part.n, width, map, Fd, Ud, Rd, part.col, glass ? 0.8f : 0.45f, glass);
            r.emissive = false;
        }
    }
    text(tx + 14, ty + 8, tw - 28, 26, Qt::AlignLeft, d.name, QColor(255, 255, 255), 20, 900);
    text(tx + 14, ty + 34, tw - 28, 14, Qt::AlignLeft,
         QString("%1   |   SLOT %2   |   %3").arg(CLS[d.cls]).arg(d.slot + 1).arg(d.tier ? QString("DROPS FROM ZONE %1+").arg(std::max(1, (d.tier - 1) * 2)) : QString("STARTING KIT")),
         qc(UI_AMBER), 10, 800, false);
    text(tx + 14, ty + th - 22, tw - 28, 14, Qt::AlignLeft, "Real 3D: extruded side profile, back-face culled, Sutherland-Hodgman clipped, z-buffered",
         QColor(120, 140, 160), 9, 500, false);

    // --- stats ---------------------------------------------------------------------------
    const int sx = 245, sy = 342, sw = 535;
    m_cv.blendRect(sx, sy, sw, 214, withAlpha(UI_BG, 190));
    static const char *MODE[] = { "SEMI-AUTO", "FULL AUTO", "3-ROUND BURST", "BOLT ACTION", "CHARGE", "BEAM", "MELEE" };
    static const char *PROJ[] = { "HITSCAN", "BALLISTIC (DROP)", "ROCKET", "BOUNCING GRENADE", "PLASMA BOLT", "CHAIN LIGHTNING", "PIERCING SLUG", "FLAME CONE" };
    float dps = d.mode == FM_MELEE ? d.damage / std::max(0.1f, d.fireDelay) : d.damage * d.pellets / std::max(0.05f, d.fireDelay);
    float range = d.mode == FM_MELEE ? d.reach : d.proj == PJ_FLAME ? d.reach : d.proj == PJ_ARC ? d.reach : std::min(60.f, d.falloffEnd);
    auto bar = [&](int row, const char *label, float v, const QString &val) {
        int y = sy + 12 + row * 26;
        text(sx + 14, y, 130, 16, Qt::AlignLeft, label, QColor(170, 175, 180), 10, 800, false);
        m_cv.blendRect(sx + 140, y + 5, 260, 7, withAlpha(0xFFFFFFFF, 26));
        m_cv.fillRect(sx + 140, y + 5, int(260 * clampf(v, 0.f, 1.f)), 7, UI_AMBER);
        text(sx + 410, y, 115, 16, Qt::AlignRight, val, QColor(230, 230, 230), 10, 800, false);
    };
    bar(0, "DAMAGE", d.damage * d.pellets / 240.f, d.pellets > 1 ? QString("%1 x %2").arg(d.damage).arg(d.pellets) : QString::number(d.damage));
    bar(1, "FIRE RATE", std::min(1.f, 60.f / std::max(0.01f, d.fireDelay) / 900.f), QString("%1 RPM").arg(int(60.f / std::max(0.01f, d.fireDelay))));
    bar(2, "DPS", dps / 400.f, QString::number(int(dps)));
    bar(3, "RANGE", range / 60.f, QString("%1 U").arg(range, 0, 'f', 1));
    bar(4, "RECOIL", d.recoil / 4.f, QString::number(d.recoil, 'f', 1));
    bar(5, "MOBILITY", (d.moveMult - 0.8f) / 0.25f, QString("%1%").arg(int(d.moveMult * 100)));
    bar(6, "CAPACITY", d.magSize / 100.f, d.magSize ? QString::number(d.magSize) : QString(d.mode == FM_MELEE ? "--" : "HEAT"));
    QString mech = QString("%1  |  %2").arg(MODE[d.mode]).arg(d.mode == FM_MELEE ? QString("REACH %1").arg(d.reach, 0, 'f', 1) : QString(PROJ[d.proj]));
    if (d.splashR > 0) mech += QString("  |  SPLASH %1 U").arg(d.splashR, 0, 'f', 1);
    if (d.penetration > 0) mech += QString("  |  PENETRATION %1").arg(int(d.penetration * 100));
    if (d.status == ST_BURN) mech += "  |  BURNS";
    if (d.status == ST_SHOCK) mech += "  |  SHOCKS";
    if (d.status == ST_STUN) mech += "  |  STUNS";
    if (d.reload == RM_SHELL) mech += "  |  SHELL-BY-SHELL";
    text(sx + 14, sy + 196, sw - 28, 14, Qt::AlignLeft, mech, QColor(120, 210, 255), 9, 700, false);
    menuFooter("UP/DOWN browse   ESC back");
}
