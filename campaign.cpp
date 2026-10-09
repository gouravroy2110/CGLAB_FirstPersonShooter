// =============================================================================
//  campaign.cpp  -  "Operation Ashfall": story missions
//
//    campaign menu   missions by act, best ranks, permanent upgrades (coins)
//    briefing        three comic panels: a live 3D flyover of the map rendered
//                    into a panel, a speaker portrait, a tactical map; text
//                    typed out letter by letter
//    intro flyby     the camera flies a Catmull-Rom spline through the mission
//                    points before handing control to you
//    objectives      kill / reach / collect / interact (hold E) / hold zone /
//                    survive waves / escort / boss, with a tracker, waypoint
//                    diamond, compass pip and progress rings
//    radio           subtitles with a pixel portrait and radio crackle
//    stealth         an alarm meter; a full meter brings reinforcements
//    debrief         rank S/A/B/C from time, accuracy, headshots, damage
//                    taken, secrets and alarms; coins; next mission unlocks
//    save            QSettings: unlocks, best ranks, coins, upgrades, the guns
//                    you carry between missions
// =============================================================================
#include "mainwindow.h"
#include "missions.h"
#include "gunbuild.h"
#include <QSettings>
#include <cmath>
#include <algorithm>

using namespace rs;

static const uint32_t C_AMBER = 0xFFFFB43A, C_BG = 0xFF0E1216, C_GREEN = 0xFF5AE68C, C_RED = 0xFFF04A3C, C_BLUE = 0xFF4AB4FF;
static inline QColor qcC(uint32_t c, int a = 255) { return QColor(R(c), G(c), B(c), a); }
static inline float smoothstep01(float x) { x = clampf(x, 0.f, 1.f); return x * x * (3 - 2 * x); }
static inline float frandM() { return float(rand()) / float(RAND_MAX); }
static inline float frandM(float a, float b) { return a + (b - a) * frandM(); }

static const char *const UPGRADE_NAMES[4] = { "BODY ARMOUR", "FIELD CONDITIONING", "ENDURANCE", "QUICK HANDS" };
static const char *const UPGRADE_DESCS[4] = { "-8% damage taken per tier", "+20 max health per tier", "+25% stamina per tier", "-10% reload time per tier" };
static const int UPGRADE_COST[4][3] = { {120, 220, 360}, {100, 200, 320}, {80, 160, 260}, {120, 240, 0} };
static const int UPGRADE_MAX[4] = { 3, 3, 3, 2 };

// ─────────────────────────────────────────────────────────────────────────────
//  Persistent player stats (campaign upgrades)
// ─────────────────────────────────────────────────────────────────────────────
int MainWindow::maxHP() const { return CHAR_PROFILES[m_charChoice].hp + (m_campaignRun ? 20 * m_upg[1] : 0); }
float MainWindow::maxStamina() const { return CHAR_PROFILES[m_charChoice].stamina * (1.f + (m_campaignRun ? 0.25f * m_upg[2] : 0.f)); }
float MainWindow::reloadMul() const { return 1.f - (m_campaignRun ? 0.1f * m_upg[3] : 0.f); }

// ─────────────────────────────────────────────────────────────────────────────
//  Save / load
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::loadCampaign()
{
    m_cUnlocked = 1; m_cCoins = 0;
    for (int i = 0; i < MISSION_COUNT; i++) { m_cRank[i] = 0; m_cBest[i] = 0; }
    for (int i = 0; i < 4; i++) m_upg[i] = 0;
    m_cInventory.clear();
    if (m_testMode) return;
    QSettings s("RasterFPS", "RasterFPS");
    s.beginGroup("campaign");
    m_cUnlocked = clampi(s.value("unlocked", 1).toInt(), 1, MISSION_COUNT);
    m_cCoins = std::max(0, s.value("coins", 0).toInt());
    for (int i = 0; i < MISSION_COUNT; i++) { m_cRank[i] = s.value(QString("rank%1").arg(i), 0).toInt(); m_cBest[i] = s.value(QString("best%1").arg(i), 0).toInt(); }
    for (int i = 0; i < 4; i++) m_upg[i] = clampi(s.value(QString("upg%1").arg(i), 0).toInt(), 0, UPGRADE_MAX[i]);
    m_cInventory = s.value("inventory", "").toString();
    s.endGroup();
}

void MainWindow::saveCampaign() const
{
    if (m_testMode) return;
    QSettings s("RasterFPS", "RasterFPS");
    s.beginGroup("campaign");
    s.setValue("unlocked", m_cUnlocked);
    s.setValue("coins", m_cCoins);
    for (int i = 0; i < MISSION_COUNT; i++) { s.setValue(QString("rank%1").arg(i), m_cRank[i]); s.setValue(QString("best%1").arg(i), m_cBest[i]); }
    for (int i = 0; i < 4; i++) s.setValue(QString("upg%1").arg(i), m_upg[i]);
    s.setValue("inventory", m_cInventory);
    s.endGroup();
}

// The guns (with their rarity and attachments) and throwables you carry over
QString MainWindow::packInventory() const
{
    const Player &pl = m_gs.player;
    QStringList parts;
    for (int s = 0; s < SLOT_COUNT; s++) {
        int w = pl.slot[s];
        if (w < 0) { parts << "-"; continue; }
        QString a = QString::number(w) + ":" + QString::number(pl.load[w].rarity);
        for (int k = 0; k < AT_COUNT; k++) a += "," + QString::number(pl.load[w].att[k]);
        parts << a;
    }
    QString th;
    for (int k = 0; k < TH_COUNT; k++) th += QString::number(pl.throwCount[k]) + (k + 1 < TH_COUNT ? "," : "");
    parts << th << QString::number(pl.gadget);
    return parts.join(";");
}

void MainWindow::unpackInventory(const QString &s)
{
    if (s.isEmpty()) return;
    QStringList parts = s.split(';');
    if (parts.size() < SLOT_COUNT + 2) return;
    Player &pl = m_gs.player;
    for (int w = 0; w < WPN_COUNT; w++) { pl.owned[w] = false; pl.mag[w] = 0; pl.reserve[w] = 0; }
    for (int sl = 0; sl < SLOT_COUNT; sl++) {
        pl.slot[sl] = -1;
        if (parts[sl] == "-") continue;
        QStringList kv = parts[sl].split(':');
        int w = kv[0].toInt();
        if (w < 0 || w >= WPN_COUNT || kv.size() < 2) continue;
        QStringList ra = kv[1].split(',');
        Loadout L; L.rarity = uint8_t(clampi(ra[0].toInt(), 0, RARITY_COUNT - 1));
        for (int k = 0; k < AT_COUNT && k + 1 < ra.size(); k++) L.att[k] = uint8_t(ra[k + 1].toInt());
        pl.slot[sl] = w; pl.owned[w] = true; pl.load[w] = L;
        pl.mag[w] = effectiveMagSize(w); pl.reserve[w] = WEAPONS[w].magSize * 3;
    }
    QStringList th = parts[SLOT_COUNT].split(',');
    for (int k = 0; k < TH_COUNT && k < th.size(); k++) pl.throwCount[k] = clampi(th[k].toInt(), 0, THROWS[k].maxCarry);
    pl.gadget = clampi(parts[SLOT_COUNT + 1].toInt(), 0, GD_COUNT - 1);
    if (pl.slot[SLOT_MELEE] < 0) { pl.slot[SLOT_MELEE] = WPN_KNIFE; pl.owned[WPN_KNIFE] = true; }
    if (pl.slot[SLOT_SIDEARM] < 0) { pl.slot[SLOT_SIDEARM] = WPN_PISTOL; pl.owned[WPN_PISTOL] = true; pl.mag[WPN_PISTOL] = 12; pl.reserve[WPN_PISTOL] = 60; }
    for (int sl = SLOT_PRIMARY2; sl >= 0; sl--) if (pl.slot[sl] >= 0 && !isMelee(pl.slot[sl])) { pl.curWeapon = pl.slot[sl]; break; }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Campaign menu
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::openCampaign()
{
    loadCampaign();
    m_campaignRun = false;
    m_cSel = std::min(m_cUnlocked - 1, MISSION_COUNT - 1);
    m_screen = SCR_CAMPAIGN;
}

static const char *rankLetter(int r) { static const char *L[5] = { "-", "C", "B", "A", "S" }; return L[clampi(r, 0, 4)]; }
static uint32_t rankColor(int r) { static const uint32_t C[5] = { 0xFF606870, 0xFFB0B4B8, 0xFF5AE68C, 0xFF4AB4FF, 0xFFFFC23A }; return C[clampi(r, 0, 4)]; }

void MainWindow::renderCampaign()
{
    menuHeader(QString("OPERATION ASHFALL   -   %1 COINS").arg(m_cCoins), "CAMPAIGN");
    // mission list grouped by act
    static const char *ACTS[5] = { "", "ACT I - THE VALLEY", "ACT II - THE MOUNTAIN", "ACT III - THE CORE", "FINALE" };
    int y = 80, lastAct = 0;
    for (int i = 0; i < MISSION_COUNT; i++) {
        const MissionDef &M = MISSIONS[i];
        if (M.act != lastAct) { if (lastAct) y += 4; text(24, y, 300, 14, Qt::AlignLeft, ACTS[M.act], qcC(C_AMBER), 9, 800, false); y += 18; lastAct = M.act; }
        bool locked = i >= m_cUnlocked, sel = i == m_cSel;
        m_menuRects.push_back({20, y, 330, 30, i});
        m_cv.blendRect(20, y, 330, 30, withAlpha(sel ? 0xFF2A2210 : C_BG, sel ? 235 : 190));
        if (sel) { m_cv.fillRect(20, y, 4, 30, C_AMBER); m_cv.rectOutline(20, y, 330, 30, withAlpha(C_AMBER, 160)); }
        text(32, y, 60, 30, Qt::AlignLeft | Qt::AlignVCenter, QString::number(i + 1), locked ? QColor(90, 90, 90) : QColor(200, 200, 200), 12, 900, false);
        text(56, y, 200, 30, Qt::AlignLeft | Qt::AlignVCenter, M.name, locked ? QColor(90, 90, 90) : sel ? QColor(255, 255, 255) : QColor(210, 210, 210), 13, 800, false);
        text(190, y, 110, 30, Qt::AlignLeft | Qt::AlignVCenter, locked ? "LOCKED" : MAP_NAMES[M.map], QColor(120, 130, 140), 8, 700, false);
        m_cv.fillRect(312, y + 5, 30, 20, withAlpha(rankColor(m_cRank[i]), locked ? 60 : 220));
        text(312, y + 5, 30, 20, Qt::AlignCenter, rankLetter(m_cRank[i]), QColor(10, 10, 10), 12, 900, false);
        y += 33;
    }
    // selected mission card
    const MissionDef &M = MISSIONS[m_cSel];
    int cx = 366, cy = 80, cw = 414;
    panel(cx, cy, cw, 236, true, C_AMBER);
    text(cx + 14, cy + 10, cw - 28, 14, Qt::AlignLeft, M.codename, qcC(C_AMBER), 10, 800, false);
    text(cx + 14, cy + 24, cw - 28, 30, Qt::AlignLeft, M.name, QColor(255, 255, 255), 24, 900);
    static const char *WXN[WX_COUNT] = { "CLEAR", "RAIN", "SNOW", "SANDSTORM", "ASH", "BLIZZARD" };
    QString when = M.hour >= 20.f || M.hour < 5.f ? "NIGHT" : M.hour >= 17.f ? "DUSK" : M.hour < 9.5f ? "MORNING" : "DAY";
    text(cx + 14, cy + 58, cw - 28, 14, Qt::AlignLeft, QString("%1  |  %2  |  %3  |  %4").arg(MAP_NAMES[M.map]).arg(THEMES[MAP_THEME[M.map]].name).arg(when).arg(WXN[M.weather]),
         QColor(170, 180, 190), 9, 700, false);
    text(cx + 14, cy + 76, cw - 28, 60, Qt::AlignLeft | Qt::TextWordWrap, M.brief[0].text, QColor(200, 205, 210), 10, 500, false);
    for (int o = 0; o < M.objCount; o++)
        text(cx + 14, cy + 140 + o * 15, cw - 28, 14, Qt::AlignLeft, QString("-  %1").arg(M.obj[o].text), QColor(150, 200, 255), 9, 700, false);
    text(cx + 14, cy + 208, cw - 28, 14, Qt::AlignLeft, QString("BOSS: %1   PAR %2:%3   BEST %4")
         .arg(bossName(M.boss)).arg(int(M.parTime) / 60).arg(int(M.parTime) % 60, 2, 10, QChar('0')).arg(m_cBest[m_cSel]), QColor(255, 130, 110), 9, 800, false);
    // difficulty / class / play
    int by = cy + 248;
    m_cv.blendRect(cx, by, cw, 30, withAlpha(C_BG, 210));
    text(cx + 12, by, cw / 2, 30, Qt::AlignLeft | Qt::AlignVCenter, QString("[D] DIFFICULTY: %1").arg(DIFF_CFG[m_difficulty].name), QColor(220, 220, 220), 10, 800, false);
    text(cx + cw / 2, by, cw / 2 - 12, 30, Qt::AlignRight | Qt::AlignVCenter, QString("[C] CLASS: %1").arg(CHAR_PROFILES[m_charChoice].name), QColor(220, 220, 220), 10, 800, false);
    bool locked = m_cSel >= m_cUnlocked;
    m_menuRects.push_back({cx, by + 38, cw, 34, 50});
    m_cv.blendRect(cx, by + 38, cw, 34, withAlpha(locked ? 0xFF2A2F36 : 0xFF1D6638, 235));
    m_cv.rectOutline(cx, by + 38, cw, 34, locked ? withAlpha(0xFFFFFFFF, 40) : 0xFF45F07E, locked ? 1 : 2);
    text(cx, by + 38, cw, 34, Qt::AlignCenter, locked ? "FINISH THE PREVIOUS MISSION TO UNLOCK" : "BRIEFING  >>   [ENTER]", QColor(255, 255, 255), 13, 900);
    // permanent upgrades
    int uy = by + 84;
    text(cx, uy, cw, 14, Qt::AlignLeft, "PERMANENT UPGRADES  (KEYS 5-8)", qcC(C_AMBER), 10, 800, false);
    for (int u = 0; u < 4; u++) {
        int ry = uy + 16 + u * 24;
        int lvl = m_upg[u], cost = lvl < UPGRADE_MAX[u] ? UPGRADE_COST[u][lvl] : 0;
        m_menuRects.push_back({cx, ry, cw, 22, 60 + u});
        m_cv.blendRect(cx, ry, cw, 22, withAlpha(C_BG, 200));
        text(cx + 8, ry, 170, 22, Qt::AlignLeft | Qt::AlignVCenter, QString("[%1] %2").arg(u + 5).arg(UPGRADE_NAMES[u]), QColor(230, 230, 230), 9, 800, false);
        text(cx + 170, ry, 160, 22, Qt::AlignLeft | Qt::AlignVCenter, UPGRADE_DESCS[u], QColor(140, 150, 160), 8, 600, false);
        for (int k = 0; k < UPGRADE_MAX[u]; k++) m_cv.fillRect(cx + 330 + k * 9, ry + 7, 7, 8, k < lvl ? C_GREEN : withAlpha(0xFFFFFFFF, 40));
        text(cx + 360, ry, 48, 22, Qt::AlignRight | Qt::AlignVCenter, lvl < UPGRADE_MAX[u] ? QString::number(cost) : QString("MAX"),
             lvl < UPGRADE_MAX[u] && m_cCoins >= cost ? qcC(C_AMBER) : QColor(110, 110, 110), 9, 800, false);
    }
    if (m_ixMsgT > 0.f) text(366, 548, 414, 14, Qt::AlignCenter, m_ixMsg, QColor(140, 255, 160), 10, 800, false);
    menuFooter("UP/DOWN mission   ENTER briefing   D difficulty   C class   5-8 upgrades   ESC back");
}

void MainWindow::buyCampaignUpgrade(int u)
{
    if (u < 0 || u > 3) return;
    int lvl = m_upg[u];
    if (lvl >= UPGRADE_MAX[u]) { m_ixMsg = "ALREADY AT MAXIMUM"; m_ixMsgT = 2.f; sfx(SND_UI_BACK, 0.5f); return; }
    int cost = UPGRADE_COST[u][lvl];
    if (m_cCoins < cost) { m_ixMsg = "NOT ENOUGH COINS - EARN THEM IN MISSIONS"; m_ixMsgT = 2.f; sfx(SND_UI_BACK, 0.5f); return; }
    m_cCoins -= cost; m_upg[u]++;
    m_ixMsg = QString("%1 TIER %2").arg(UPGRADE_NAMES[u]).arg(m_upg[u]); m_ixMsgT = 2.f;
    sfx(SND_UPGRADE, 0.7f);
    saveCampaign();
}

void MainWindow::campaignKey(int k)
{
    if (m_screen == SCR_CAMPAIGN) {
        if (k == Qt::Key_Up) { m_cSel = (m_cSel + MISSION_COUNT - 1) % MISSION_COUNT; sfx(SND_UI_MOVE, 0.5f); }
        if (k == Qt::Key_Down) { m_cSel = (m_cSel + 1) % MISSION_COUNT; sfx(SND_UI_MOVE, 0.5f); }
        if (k == Qt::Key_D) { m_difficulty = (m_difficulty + 1) % 3; sfx(SND_UI_MOVE, 0.5f); }
        if (k == Qt::Key_C) { m_charChoice = (m_charChoice + 1) % 3; sfx(SND_UI_MOVE, 0.5f); }
        if (k >= Qt::Key_5 && k <= Qt::Key_8) buyCampaignUpgrade(k - Qt::Key_5);
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            if (m_cSel < m_cUnlocked) {
                // the briefing flies over the mission map: load it, empty of the last game's people
                m_mapChoice = MISSIONS[m_cSel].map;
                m_gs.enemies.clear(); m_gs.actors.clear(); m_gs.weaponPickups.clear(); m_gs.projectiles.clear(); m_gs.bullets.clear();
                m_screen = SCR_BRIEFING; m_briefT = 0.f; sfx(SND_UI_SELECT, 0.6f);
            }
            else sfx(SND_UI_BACK, 0.6f);
        }
        if (k == Qt::Key_Escape) { m_screen = SCR_MENU; m_menuCursor = 0; sfx(SND_UI_BACK, 0.6f); }
        return;
    }
    if (m_screen == SCR_BRIEFING) {
        if (k == Qt::Key_Escape) { m_screen = SCR_CAMPAIGN; sfx(SND_UI_BACK, 0.6f); return; }
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            if (m_briefT < 6.f) m_briefT = 6.f;                 // finish the typing first
            else startMission(m_cSel);
        }
        return;
    }
    if (m_screen == SCR_DEBRIEF) {
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space || k == Qt::Key_Escape) {
            if (m_debriefT < 2.5f) m_debriefT = 2.5f;
            else { m_campaignRun = false; openCampaign(); if (m_debriefWon && m_mission + 1 < m_cUnlocked) m_cSel = m_mission + 1; }
        }
    }
}

void MainWindow::campaignClick(int id)
{
    if (m_screen != SCR_CAMPAIGN) return;
    if (id >= 0 && id < MISSION_COUNT) { m_cSel = id; sfx(SND_UI_MOVE, 0.5f); }
    if (id == 50) campaignKey(Qt::Key_Return);
    if (id >= 60 && id < 64) buyCampaignUpgrade(id - 60);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Portraits (procedural pixel heads) and the radio
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::bakePortraits()
{
    static const SoldierLook KADE = { rgb(80,90,64), rgb(56,64,44), rgb(100,96,70), rgb(70,78,52), rgb(50,56,36), rgb(90,100,70), rgb(170,124,96), rgb(70,72,60), rgb(255,200,60), rgb(40,36,30), rgb(40,40,40) };
    static const SoldierLook ORTIZ = { rgb(220,222,226), rgb(170,172,176), rgb(236,236,240), rgb(220,222,226), rgb(170,172,176), rgb(120,80,60), rgb(196,148,112), rgb(196,148,112), rgb(120,200,255), rgb(60,60,64), rgb(200,200,204) };
    static const SoldierLook HELIX = { rgb(30,30,36), rgb(18,18,22), rgb(200,160,60), rgb(40,40,48), rgb(24,24,30), rgb(160,160,166), rgb(200,160,130), rgb(200,160,130), rgb(255,60,200), rgb(20,20,24), rgb(200,160,60) };
    const SoldierLook *L[SPK_COUNT] = { &KADE, &ORTIZ, nullptr, &HELIX, &SQUAD_LOOK };
    const int S = 160;
    std::vector<uint32_t> tmp(size_t(S) * S);
    for (int sp = 0; sp < SPK_COUNT; sp++) {
        m_speakerPortrait[sp].assign(size_t(PORT) * PORT, 0xFF101418);
        if (!L[sp]) {
            // VECTOR: a wireframe face of lines on a dark grid (drawn with Bresenham)
            Canvas cv{m_speakerPortrait[sp].data(), PORT, PORT};
            for (int g = 0; g < PORT; g += 6) { cv.line(g, 0, g, PORT - 1, rgb(40, 14, 40)); cv.line(0, g, PORT - 1, g, rgb(40, 14, 40)); }
            const int pts[][2] = { {32, 6}, {50, 16}, {54, 34}, {44, 54}, {32, 58}, {20, 54}, {10, 34}, {14, 16} };
            for (int i = 0; i < 8; i++) cv.line(pts[i][0], pts[i][1], pts[(i + 1) % 8][0], pts[(i + 1) % 8][1], rgb(255, 60, 200));
            for (int i = 0; i < 8; i++) cv.line(pts[i][0], pts[i][1], 32, 32, rgb(140, 30, 120));
            cv.fillCircle(22, 28, 4, rgb(255, 120, 230)); cv.fillCircle(42, 28, 4, rgb(255, 120, 230));
            cv.line(22, 44, 42, 44, rgb(255, 60, 200));
            continue;
        }
        paintSoldier(tmp.data(), nullptr, S, *L[sp], POSE_AIM, 0.f, false);
        // crop the head + shoulders and box-filter it down to the portrait
        const int x0 = 44, y0 = 0, cw = 72;
        for (int y = 0; y < PORT; y++) for (int x = 0; x < PORT; x++) {
            int sx = x0 + x * cw / PORT, sy = y0 + y * cw / PORT;
            uint32_t c = tmp[size_t(sy) * S + sx];
            m_speakerPortrait[sp][size_t(y) * PORT + x] = (c >> 24) ? c : lerpCol(rgb(20, 28, 36), rgb(40, 56, 70), float(y) / PORT);
        }
    }
}

void MainWindow::say(int speaker, const QString &line, float seconds)
{
    m_radio.push_back(RadioMsg{speaker, line, seconds});
}

// "SPEAKER: text" lines from the mission data
void MainWindow::sayTagged(const char *tagged)
{
    QString s(tagged);
    if (s.isEmpty()) return;
    int sp = SPK_KADE;
    int colon = s.indexOf(':');
    if (colon > 0) {
        QString who = s.left(colon).trimmed();
        if (who == "ORTIZ") sp = SPK_ORTIZ; else if (who == "VECTOR") sp = SPK_VECTOR; else if (who == "HELIX") sp = SPK_HELIX;
        s = s.mid(colon + 1).trimmed();
    }
    say(sp, s, 4.5f);
}

void MainWindow::updateRadio(float dt)
{
    if (m_radio.empty()) return;
    RadioMsg &m = m_radio.front();
    if (m.t == m.dur) sfx(SND_RADIO, 0.55f);
    m.t -= dt;
    if (m.t <= 0.f) m_radio.erase(m_radio.begin());
}

void MainWindow::renderRadio()
{
    if (m_radio.empty()) return;
    const RadioMsg &m = m_radio.front();
    float shown = m.dur - m.t;
    int chars = std::min(int(m.text.size()), int(shown * 55.f));
    const int x = 16, y = H - 186, w = 400, h = 84;
    m_cv.blendRect(x, y, w, h, withAlpha(C_BG, 210));
    m_cv.fillRect(x, y, 3, h, m.speaker == SPK_VECTOR || m.speaker == SPK_HELIX ? 0xFFFF3CC8 : C_BLUE);
    // portrait with scanlines + a little static
    const std::vector<uint32_t> &P = m_speakerPortrait[clampi(m.speaker, 0, SPK_COUNT - 1)];
    for (int py = 0; py < PORT; py++) for (int px = 0; px < PORT; px++) {
        uint32_t c = P[size_t(py) * PORT + px];
        if (py % 3 == 0) c = scaleCol(c, 0.75f);
        if (hash2(px, py, int(m_time * 20)) > 0.985f) c = rgb(220, 230, 240);
        m_cv.set(x + 6 + px * 56 / PORT, y + 5 + py * 56 / PORT, c);
    }
    m_cv.rectOutline(x + 5, y + 4, 58, 58, withAlpha(0xFFFFFFFF, 50));
    text(x + 70, y + 5, w - 76, 16, Qt::AlignLeft, SPEAKER_NAMES[clampi(m.speaker, 0, SPK_COUNT - 1)], qcC(m.speaker == SPK_VECTOR || m.speaker == SPK_HELIX ? 0xFFFF6CD8 : C_BLUE), 14, 900, false);
    text(x + 70, y + 26, w - 78, 56, Qt::AlignLeft | Qt::TextWordWrap, m.text.left(chars), QColor(230, 232, 236), 14, 600, false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Missions
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::missionMarker(char c, float &x, float &y, int nth) const
{
    int n = 0;
    for (const Marker &mk : m_parsed[MISSIONS[m_mission].map].markers)
        if (mk.kind == c) { if (n == nth) { x = mk.x; y = mk.y; return true; } n++; }
    return false;
}

void MainWindow::startMission(int i)
{
    m_mission = clampi(i, 0, MISSION_COUNT - 1);
    const MissionDef &M = MISSIONS[m_mission];
    m_campaignRun = true;
    m_mapChoice = M.map;
    m_envLocked = true;
    m_squadSizeSaved = m_squadSize; m_squadSize = 0;           // the story is a solo operation
    initGame();
    m_squadSize = m_squadSizeSaved;
    setEnvironment(M.hour, M.weather);
    // carry the campaign kit + permanent upgrades
    Player &pl = m_gs.player;
    unpackInventory(m_cInventory);
    pl.hp = maxHP();
    pl.stamina = maxStamina();
    pl.damageReduction = CHAR_PROFILES[m_charChoice].damageReduction + 0.08f * m_upg[0];
    if (effectiveScope(pl.curWeapon)) m_scopeMag = clampf(1.f / effectiveAdsFov(pl.curWeapon), 2.f, 8.f);
    // mission items: the key card at '1' (mission 1); intel cases come from the map's 't' markers
    if (m_mission == 0) { float kx, ky; if (missionMarker('1', kx, ky)) { WeaponPickup wp; wp.x = kx; wp.y = ky; wp.type = PICKUP_KEY; m_gs.weaponPickups.push_back(wp); } }
    if (M.stealth) for (Enemy &e : m_gs.enemies) { e.aggroRange *= 0.55f; e.state = ES_IDLE; }
    m_obj = -1; m_objCount = 0; m_objTimer = 0.f; m_objMask = 0; m_waveT = 0.f; m_waveCount = 0;
    m_missionTime = 0.f; m_alarm = 0.f; m_alarms = 0; m_intelCount = 0; m_keyCount = 0; m_missionDone = false; m_missionFailed = false;
    m_missionEndT = 0.f; m_bossSpawned = false; m_escort = -1; m_objKillBase = 0;
    m_radio.clear();
    m_screen = SCR_PLAYING;
    nextObjective();
    // intro flyby along the mission's points
    startFlyby();
}

void MainWindow::nextObjective()
{
    const MissionDef &M = MISSIONS[m_mission];
    m_obj++;
    m_objCount = 0; m_objTimer = 0.f; m_objMask = 0; m_waveT = 0.f; m_waveCount = 0;
    if (m_obj >= M.objCount) {
        m_missionDone = true; m_missionEndT = 0.f;
        sfx(SND_LEVEL_CLEAR, 0.8f);
        pushFeed("MISSION COMPLETE", 4.f, true);
        say(M.outro.speaker, M.outro.text, 6.f);
        return;
    }
    const ObjectiveDef &O = M.obj[m_obj];
    m_objKillBase = m_gs.kills;
    sayTagged(O.radio);
    if (m_obj > 0) sfx(SND_UPGRADE, 0.5f);
    if (O.event == EV_SPAWN_BOSS && !m_bossSpawned) {
        m_bossSpawned = true;
        float bx = 0, by = 0, cx = 0, cy = 0;
        bool hasB = missionMarker(M.bossMarkers[0], bx, by);
        bool hasC = M.bossMarkers[1] && missionMarker(M.bossMarkers[1], cx, cy);
        if (!hasB) { bx = m_gs.player.posX + 8.f; by = m_gs.player.posY; }
        size_t first = m_gs.enemies.size();
        if (M.boss == BK_TWIN_A) spawnBossPair(BK_TWIN_A, bx, by, BK_TWIN_B, hasC ? cx : bx + 4.f, hasC ? cy : by);
        else if (M.boss == BK_GUARD_HEAVY) spawnBossPair(BK_GUARD_HEAVY, bx, by, BK_GUARD_MEDIC, hasC ? cx : bx + 1.5f, hasC ? cy : by);
        else spawnBoss(M.boss, bx, by, 1.f + 0.12f * m_mission);
        for (size_t k = first; k < m_gs.enemies.size(); k++) m_gs.enemies[k].state = ES_ALERT;
        m_bossIntroT = 2.8f;
        sfx(SND_ENEMY_ALERT, 1.f, 0.6f);
    }
    if (O.type == OBJ_ESCORT) {
        float nx, ny;
        if (missionMarker('n', nx, ny)) {
            Actor a; a.id = 99; a.team = 0; a.escort = true; a.hp = 160;
            std::snprintf(a.name, sizeof(a.name), "DR. ORTIZ");
            a.posX = nx; a.posY = ny; a.footZ = floorH(nx, ny); a.curWeapon = WPN_PISTOL; a.speedMult = 0.95f;
            m_gs.actors.push_back(a);
            m_escort = int(m_gs.actors.size()) - 1;
        }
    }
    if (O.type == OBJ_SURVIVE || O.event == EV_WAVES) spawnMissionWave(O.type == OBJ_SURVIVE ? 4 : 3);
}

EnemyType MainWindow::missionEnemyType() const
{
    const MissionDef &M = MISSIONS[m_mission];
    return M.mix[rand() % 4];
}

// A wave from the mission's mix, out of sight, 10-20 units from the objective
void MainWindow::spawnMissionWave(int n)
{
    const MissionDef &M = MISSIONS[m_mission];
    float ox = m_gs.player.posX, oy = m_gs.player.posY;
    if (m_obj >= 0 && m_obj < M.objCount && M.obj[m_obj].markers[0]) missionMarker(M.obj[m_obj].markers[0], ox, oy);
    int made = 0;
    for (int tries = 0; tries < 200 && made < n; tries++) {
        int cx = 2 + rand() % (m_mapW - 4), cy = 2 + rand() % (m_mapH - 4);
        float x = cx + 0.5f, y = cy + 0.5f;
        float d2 = (x - ox) * (x - ox) + (y - oy) * (y - oy);
        if (d2 < 100.f || d2 > 400.f) continue;
        if (m_map[cy][cx] != CELL_FLOOR || m_acid[cy][cx] || !reachable(cx, cy) || m_plat[cy][cx] < -0.1f || !posValid(x, y, 0.3f)) continue;
        float pd2 = (x - m_gs.player.posX) * (x - m_gs.player.posX) + (y - m_gs.player.posY) * (y - m_gs.player.posY);
        if (pd2 < 64.f) continue;
        spawnEnemy(x, y, false, missionEnemyType());
        m_gs.enemies.back().state = ES_ALERT;
        made++;
    }
    m_waveCount++;
    if (made) pushFeed(QString("HOSTILE WAVE  x%1").arg(made), 2.5f);
}

// Hold-E interaction, zones, escort, timers: one objective at a time
void MainWindow::updateMission(float dt)
{
    if (!m_campaignRun || m_gs.gameOver) return;
    const MissionDef &M = MISSIONS[m_mission];
    Player &pl = m_gs.player;
    m_missionTime += dt;
    m_bossIntroT = std::max(0.f, m_bossIntroT - dt);
    updateRadio(dt);
    if (m_missionDone || m_missionFailed) {
        m_missionEndT += dt;
        if (m_missionEndT > (m_missionFailed ? 3.5f : 4.f)) finishMission(!m_missionFailed);
        return;
    }
    // stealth: hostiles that spot you fill the alarm; a full alarm calls reinforcements
    if (M.stealth) {
        int alert = 0;
        for (const Enemy &e : m_gs.enemies) if (e.alive && e.state == ES_ALERT && !e.bossKind) alert++;
        m_alarm += alert ? 0.12f * alert * dt : -0.05f * dt;
        m_alarm = clampf(m_alarm, 0.f, 1.f);
        if (m_alarm >= 1.f) {
            m_alarm = 0.f; m_alarms++;
            say(SPK_KADE, "They've raised the alarm! Reinforcements incoming!", 3.f);
            sfx(SND_RADIO, 0.8f);
            int save = m_obj; spawnMissionWave(5); m_obj = save;
        }
    }
    // escort death fails the mission
    if (m_escort >= 0 && m_escort < int(m_gs.actors.size()) && !m_gs.actors[size_t(m_escort)].alive) {
        m_missionFailed = true; m_failReason = "DR. ORTIZ WAS KILLED";
        sfx(SND_GAME_OVER, 0.8f);
        return;
    }
    if (m_obj < 0 || m_obj >= M.objCount) return;
    const ObjectiveDef &O = M.obj[m_obj];
    auto nearMarker = [&](char c, int nth, float r, float &mx, float &my) {
        if (!missionMarker(c, mx, my, nth)) return false;
        return (pl.posX - mx) * (pl.posX - mx) + (pl.posY - my) * (pl.posY - my) < r * r;
    };
    float mx, my;
    switch (O.type) {
    case OBJ_KILL_COUNT:
        m_objCount = m_gs.kills - m_objKillBase;
        if (m_objCount >= O.count) nextObjective();
        break;
    case OBJ_KILL_ALL:
        if (aliveEnemies() == 0) nextObjective();
        break;
    case OBJ_REACH:
        if (nearMarker(O.markers[0], 0, 2.2f, mx, my)) nextObjective();
        break;
    case OBJ_COLLECT:
        m_objCount = O.markers[0] == 't' ? m_intelCount : m_keyCount;
        if (m_objCount >= O.count) nextObjective();
        break;
    case OBJ_INTERACT: case OBJ_HOLD: {
        // each marker of the objective, in any order
        int total = int(std::strlen(O.markers)), inZone = -1;
        for (int k = 0; k < total; k++) {
            if (m_objMask & (1 << k)) continue;
            if (nearMarker(O.markers[k], 0, O.type == OBJ_HOLD ? 2.6f : 1.6f, mx, my)) { inZone = k; break; }
        }
        bool working = inZone >= 0 && (O.type == OBJ_HOLD || m_keys.count(Qt::Key_E));
        if (inZone != m_objZone) { m_objTimer = 0.f; m_objZone = inZone; }
        if (working) {
            m_objTimer += dt;
            if (int(m_objTimer * 3) != int((m_objTimer - dt) * 3)) sfx(SND_ZOOM_TICK, 0.4f, 0.8f + 0.2f * (m_objTimer / O.time));
            if (m_objTimer >= O.time) {
                m_objMask |= 1 << inZone; m_objCount++; m_objTimer = 0.f; m_objZone = -1;
                sfx(SND_PICKUP_WEAPON, 0.7f);
                pushFeed(O.type == OBJ_HOLD ? "OUTPOST CAPTURED" : "DONE", 2.f);
                if (O.type == OBJ_HOLD) spawnMissionWave(2);
                if (O.event == EV_OPEN_GATE) {
                    // blow the gate: every door near the marker drops
                    for (int y = int(my) - 3; y <= int(my) + 3; y++) for (int x = int(mx) - 3; x <= int(mx) + 3; x++)
                        if (inside(x, y) && m_map[y][x] == CELL_DOOR) { m_doorMoving[y][x] = true; m_doorT[y][x] = std::max(m_doorT[y][x], 0.7f); }
                    explode(mx, my - 0.6f, floorH(mx, my) + 0.6f, 1.5f, 0.f, PO_ENEMY, false);
                }
                if (m_objCount >= total) nextObjective();
            }
        } else if (O.type == OBJ_INTERACT) m_objTimer = std::max(0.f, m_objTimer - dt * 2.f);
        else m_objTimer = std::max(0.f, m_objTimer - dt * 0.5f);   // a contested zone slips back slowly
        break;
    }
    case OBJ_SURVIVE:
        m_objTimer += dt;
        m_waveT += dt;
        if (m_waveT > 14.f) { m_waveT = 0.f; spawnMissionWave(3 + m_waveCount / 2); }
        if (m_objTimer >= O.time) { pushFeed("HELD!", 2.5f, true); nextObjective(); }
        break;
    case OBJ_ESCORT:
        if (m_escort >= 0 && m_escort < int(m_gs.actors.size())) {
            const Actor &a = m_gs.actors[size_t(m_escort)];
            if (missionMarker(O.markers[0], mx, my) && (a.posX - mx) * (a.posX - mx) + (a.posY - my) * (a.posY - my) < 4.5f) {
                say(SPK_ORTIZ, "I'm safe. Go - I'll patch into their network from here.", 4.f);
                m_gs.actors[size_t(m_escort)].alive = false; m_gs.actors[size_t(m_escort)].respawnT = 1e9f;
                m_escort = -1;
                nextObjective();
            }
        }
        break;
    case OBJ_BOSS: {
        bool anyBoss = false;
        for (const Enemy &e : m_gs.enemies) if (e.alive && e.bossKind && !bossIsMinor(e.bossKind) && !e.decoy) anyBoss = true;
        if (m_bossSpawned && !anyBoss) {
            pushFeed(QString("%1 DEFEATED").arg(bossName(M.boss)), 3.5f, true);
            if (M.boss == BK_FOUNDER) { m_blastLight = 1.f; m_finaleT = 4.f; }
            for (Enemy &e : m_gs.enemies) if (e.alive && e.decoy) { e.alive = false; e.corpseFade = 0.f; }
            nextObjective();
        }
        break;
    }
    }
}

// The marker the current objective points at (for the waypoint), if any
bool MainWindow::objectiveTarget(float &x, float &y) const
{
    if (!m_campaignRun || m_obj < 0) return false;
    const MissionDef &M = MISSIONS[m_mission];
    if (m_obj >= M.objCount) return false;
    const ObjectiveDef &O = M.obj[m_obj];
    if (O.type == OBJ_BOSS) {
        for (const Enemy &e : m_gs.enemies) if (e.alive && e.boss && !e.decoy) { x = e.x; y = e.y; return true; }
        return false;
    }
    if (O.type == OBJ_ESCORT) return missionMarker(O.markers[0], x, y);
    if (O.type == OBJ_COLLECT && O.markers[0] == 't') {
        // nearest intel case still lying around
        float best = 1e9f; bool ok = false;
        for (const WeaponPickup &wp : m_gs.weaponPickups) if (wp.active && wp.type == PICKUP_INTEL) {
            float d = (wp.x - m_gs.player.posX) * (wp.x - m_gs.player.posX) + (wp.y - m_gs.player.posY) * (wp.y - m_gs.player.posY);
            if (d < best) { best = d; x = wp.x; y = wp.y; ok = true; }
        }
        return ok;
    }
    if (O.type == OBJ_INTERACT || O.type == OBJ_HOLD) {
        int total = int(std::strlen(O.markers));
        float best = 1e9f; bool ok = false;
        for (int k = 0; k < total; k++) {
            if (m_objMask & (1 << k)) continue;
            float mx, my;
            if (!missionMarker(O.markers[k], mx, my)) continue;
            float d = (mx - m_gs.player.posX) * (mx - m_gs.player.posX) + (my - m_gs.player.posY) * (my - m_gs.player.posY);
            if (d < best) { best = d; x = mx; y = my; ok = true; }
        }
        return ok;
    }
    if (O.markers[0]) return missionMarker(O.markers[0], x, y);
    return false;
}

QString MainWindow::objectiveLine() const
{
    const MissionDef &M = MISSIONS[m_mission];
    if (m_obj < 0 || m_obj >= M.objCount) return m_missionDone ? "MISSION COMPLETE" : "";
    const ObjectiveDef &O = M.obj[m_obj];
    QString s = O.text;
    switch (O.type) {
    case OBJ_KILL_COUNT: s += QString("  %1/%2").arg(std::min(m_objCount, O.count)).arg(O.count); break;
    case OBJ_COLLECT: s += QString("  %1/%2").arg(m_objCount).arg(O.count); break;
    case OBJ_INTERACT: case OBJ_HOLD: s += QString("  %1/%2").arg(m_objCount).arg(int(std::strlen(O.markers))); break;
    case OBJ_SURVIVE: s += QString("  %1 S").arg(std::max(0, int(O.time - m_objTimer))); break;
    case OBJ_KILL_ALL: s += QString("  %1 LEFT").arg(aliveEnemies()); break;
    default: break;
    }
    return s;
}

// Tracker, waypoint, progress ring, alarm meter, radio, boss intro card
void MainWindow::renderMissionHUD()
{
    if (!m_campaignRun) return;
    const MissionDef &M = MISSIONS[m_mission];
    // tracker
    const int tw = 260, tx = 16, ty = 190;              // under the minimap, clear of the feed and boss bar
    m_cv.blendRect(tx, ty, tw, 44, withAlpha(C_BG, 190));
    m_cv.fillRect(tx, ty, 3, 44, C_AMBER);
    text(tx + 10, ty + 4, tw - 20, 12, Qt::AlignLeft, QString("%1  -  %2").arg(M.codename).arg(M.name), qcC(C_AMBER), 9, 800, false);
    text(tx + 10, ty + 20, tw - 20, 20, Qt::AlignLeft | Qt::TextWordWrap, objectiveLine(), QColor(240, 240, 240), 10, 800, false);
    int mm = int(m_missionTime);
    text(tx + 10, ty + 4, tw - 20, 12, Qt::AlignRight, QString("%1:%2").arg(mm / 60).arg(mm % 60, 2, 10, QChar('0')), QColor(170, 175, 180), 9, 700, false);
    // waypoint diamond on screen (clamped to the edge when off-screen)
    float wx, wy;
    if (objectiveTarget(wx, wy)) {
        float sx, sy, d;
        bool front = project(wx, wy, floorH(wx, wy) + 1.2f, sx, sy, d);
        float dist = std::sqrt((wx - m_gs.player.posX) * (wx - m_gs.player.posX) + (wy - m_gs.player.posY) * (wy - m_gs.player.posY));
        if (!front) { sx = W - sx; sy = H - 40; }
        bool clamped = sx < 30 || sx > W - 30 || sy < 70 || sy > H - 110 || !front;
        sx = clampf(sx, 30.f, W - 30.f); sy = clampf(sy, 70.f, H - 110.f);
        uint32_t c = withAlpha(C_AMBER, 230);
        int ix = int(sx), iy = int(sy);
        m_cv.line(ix, iy - 9, ix + 9, iy, c); m_cv.line(ix + 9, iy, ix, iy + 9, c); m_cv.line(ix, iy + 9, ix - 9, iy, c); m_cv.line(ix - 9, iy, ix, iy - 9, c);
        m_cv.fillRect(ix - 1, iy - 1, 3, 3, c);
        text(sx - 40, sy + 11, 80, 12, Qt::AlignCenter, clamped ? QString("<  %1 U  >").arg(int(dist)) : QString("%1 U").arg(int(dist)), qcC(C_AMBER), 8, 800);
    }
    // interact / hold progress
    if (m_obj >= 0 && m_obj < M.objCount) {
        const ObjectiveDef &O = M.obj[m_obj];
        if ((O.type == OBJ_INTERACT || O.type == OBJ_HOLD) && m_objZone >= 0) {
            float k = m_objTimer / O.time;
            m_cv.ring(W / 2, H / 2 + 60, 20.f, 23.f, withAlpha(0xFFFFFFFF, 50));
            m_cv.arc(W / 2, H / 2 + 60, 20.f, 23.f, -1.5708f, -1.5708f + 6.2832f * k, C_GREEN);
            text(W / 2 - 100, H / 2 + 86, 200, 14, Qt::AlignCenter, O.type == OBJ_INTERACT ? (m_keys.count(Qt::Key_E) ? "WORKING..." : "HOLD [E]") : "CAPTURING...", QColor(140, 255, 160), 10, 800);
        }
        if (O.type == OBJ_SURVIVE) {
            float k = clampf(m_objTimer / O.time, 0.f, 1.f);
            m_cv.blendRect(W / 2 - 120, 108, 240, 6, withAlpha(0xFFFFFFFF, 30));
            m_cv.fillRect(W / 2 - 120, 108, int(240 * k), 6, C_GREEN);
        }
    }
    // stealth alarm meter
    if (M.stealth) {
        m_cv.blendRect(W / 2 - 90, 92, 180, 8, withAlpha(0xFFFFFFFF, 30));
        m_cv.fillRect(W / 2 - 90, 92, int(180 * m_alarm), 8, m_alarm > 0.7f ? C_RED : C_AMBER);
        text(W / 2 - 90, 78, 180, 12, Qt::AlignCenter, m_alarms ? QString("ALARM  x%1").arg(m_alarms) : QString("UNDETECTED"), m_alarm > 0.5f ? QColor(255, 120, 100) : QColor(170, 210, 255), 9, 800, false);
    }
    // escort health
    if (m_escort >= 0 && m_escort < int(m_gs.actors.size()) && m_gs.actors[size_t(m_escort)].alive) {
        float k = m_gs.actors[size_t(m_escort)].hp / 160.f;
        text(tx, ty + 48, tw, 12, Qt::AlignLeft, "DR. ORTIZ", QColor(150, 210, 255), 9, 800, false);
        m_cv.blendRect(tx + 70, ty + 51, tw - 70, 5, withAlpha(0xFFFFFFFF, 30));
        m_cv.fillRect(tx + 70, ty + 51, int((tw - 70) * clampf(k, 0.f, 1.f)), 5, k > 0.4f ? C_BLUE : C_RED);
    }
    // boss intro card
    if (m_bossIntroT > 0.f) {
        float a = std::min(1.f, m_bossIntroT / 0.5f) * std::min(1.f, (2.8f - m_bossIntroT) / 0.3f);
        m_cv.blendRect(0, 228, W, 64, withAlpha(0xFF000000, int(170 * a)));
        m_cv.blendRect(0, 228, W, 2, withAlpha(C_RED, int(255 * a)));
        m_cv.blendRect(0, 290, W, 2, withAlpha(C_RED, int(255 * a)));
        text(0, 232, W, 34, Qt::AlignCenter, bossName(M.boss), QColor(255, 255, 255, int(255 * a)), 26, 900);
        text(0, 268, W, 16, Qt::AlignCenter, bossTitle(M.boss), QColor(255, 110, 90, int(255 * a)), 11, 800, false);
    }
    renderRadio();
    if (m_missionFailed) {
        m_cv.blendRect(0, 250, W, 80, withAlpha(0xFF000000, 200));
        text(0, 258, W, 36, Qt::AlignCenter, "MISSION FAILED", QColor(255, 80, 70), 28, 900);
        text(0, 296, W, 20, Qt::AlignCenter, m_failReason, QColor(220, 220, 220), 12, 700);
    }
}

// Mission props in the world: masts, alarm panels, capture rings, the key card
void MainWindow::renderMissionWorld()
{
    if (!m_campaignRun || m_obj < 0) return;
    const MissionDef &M = MISSIONS[m_mission];
    if (m_obj >= M.objCount) return;
    const ObjectiveDef &O = M.obj[m_obj];
    if (O.type != OBJ_INTERACT && O.type != OBJ_HOLD && O.type != OBJ_SURVIVE && O.type != OBJ_REACH && O.type != OBJ_ESCORT) return;
    int total = int(std::strlen(O.markers));
    for (int k = 0; k < total; k++) {
        float mx, my;
        if (!missionMarker(O.markers[k], mx, my)) continue;
        bool done = (m_objMask & (1 << k)) != 0;
        float r = O.type == OBJ_HOLD ? 2.6f : O.type == OBJ_SURVIVE ? 3.f : O.type == OBJ_INTERACT ? 1.6f : 2.2f;
        uint32_t col = done ? C_GREEN : O.type == OBJ_HOLD || O.type == OBJ_SURVIVE ? C_BLUE : C_AMBER;
        // ring on the ground, plus a progress arc while working on it
        float px0 = 0, py0 = 0; bool have = false;
        float prog = (!done && k == m_objZone) ? m_objTimer / std::max(0.1f, O.time) : 0.f;
        for (int s = 0; s <= 48; s++) {
            float a = s * 6.2831853f / 48.f, x = mx + std::cos(a) * r, y = my + std::sin(a) * r;
            float sx, sy, d;
            bool ok = project(x, y, floorH(x, y) + 0.04f, sx, sy, d) && d > 0.2f && d < m_depth[size_t(clampi(int(sy), 0, H - 1)) * W + clampi(int(sx), 0, W - 1)] + 0.5f;
            uint32_t c = (float(s) / 48.f < prog) ? C_GREEN : col;
            if (ok && have) m_cv.thickLine(int(px0), int(py0), int(sx), int(sy), withAlpha(c, 200), 2);
            px0 = sx; py0 = sy; have = ok;
        }
        // a beacon column so the point reads from far away
        float bx, by, bd, tx2, ty2, td;
        if (project(mx, my, floorH(mx, my), bx, by, bd) && project(mx, my, floorH(mx, my) + 2.5f, tx2, ty2, td) && bd > 0.5f) {
            int half = std::max(1, int(0.04f * m_K / bd));
            for (int yy = std::max(0, int(ty2)); yy <= std::min(H - 1, int(by)); yy++) {
                float kk = float(by - yy) / std::max(1.f, by - ty2);
                for (int ox = -half; ox <= half; ox++) {
                    int xx = int(bx) + ox;
                    if (xx < 0 || xx >= W || bd >= m_depth[size_t(yy) * W + xx]) continue;
                    m_cv.add(xx, yy, withAlpha(col, int(110 * (1.f - kk))));
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  End of a mission: rank, coins, unlocks, save
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::finishMission(bool won)
{
    const MissionDef &M = MISSIONS[m_mission];
    const GameState &g = m_gs;
    m_debriefWon = won;
    m_debriefT = 0.f;
    m_debriefRows.clear();
    int points = 0;
    auto row = [&](const QString &label, const QString &detail, bool ok) { m_debriefRows.push_back(DebriefRow{label, detail, ok}); if (ok) points++; };
    float acc = g.shotsFired ? float(g.shotsHit) / g.shotsFired : 0.f;
    float hs = g.kills ? float(g.headshots) / g.kills : 0.f;
    auto mmss = [](float s) { int t = int(s); return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0')); };
    row("SPEED", QString("%1  (par %2)").arg(mmss(m_missionTime)).arg(mmss(M.parTime)), m_missionTime <= M.parTime);
    row("ACCURACY", QString("%1%  (need 35%)").arg(int(acc * 100)), acc >= 0.35f && g.shotsFired >= 5);
    row("HEADSHOTS", QString("%1% of kills  (need 30%)").arg(int(hs * 100)), hs >= 0.3f && g.kills >= 3);
    row("CAREFUL", QString("%1 damage taken  (need under 150)").arg(g.zoneDamage), g.zoneDamage < 150);
    row("EXPLORER", QString("%1 secret areas found").arg(g.secretsFound), g.secretsFound > 0);
    if (M.stealth) row("GHOST", QString("%1 alarms raised").arg(m_alarms), m_alarms == 0);
    else row("THOROUGH", QString("%1 hostiles eliminated").arg(g.kills), g.kills >= M.enemies);
    int rank = !won ? 0 : points >= 5 ? 4 : points >= 4 ? 3 : points >= 2 ? 2 : 1;
    m_debriefRank = rank;
    static const float MUL[5] = { 0.f, 1.f, 1.2f, 1.5f, 2.f };
    m_debriefCoins = won ? int(M.reward * MUL[rank]) + g.coins : g.coins / 2;
    m_debriefScore = g.score;
    if (!m_testMode) loadCampaign();                        // re-read in case another window changed it
    if (won) {
        m_cRank[m_mission] = std::max(m_cRank[m_mission], rank);
        m_cBest[m_mission] = std::max(m_cBest[m_mission], g.score);
        m_cUnlocked = std::max(m_cUnlocked, std::min(MISSION_COUNT, m_mission + 2));
        m_cInventory = packInventory();
    }
    m_cCoins += m_debriefCoins;
    saveCampaign();
    m_envLocked = false;
    m_screen = SCR_DEBRIEF;
}

void MainWindow::renderDebrief()
{
    const MissionDef &M = MISSIONS[m_mission];
    float t = m_debriefT;
    menuHeader(QString("%1  -  %2").arg(M.codename).arg(M.name), m_debriefWon ? "MISSION COMPLETE" : "MISSION FAILED");
    const int x = 60, w = 440;
    for (int i = 0; i < int(m_debriefRows.size()); i++) {
        if (t < 0.4f + i * 0.25f) break;
        const DebriefRow &r = m_debriefRows[size_t(i)];
        int y = 100 + i * 40;
        m_cv.blendRect(x, y, w, 34, withAlpha(C_BG, 200));
        m_cv.fillRect(x + 10, y + 8, 18, 18, r.ok ? 0xFF1D6638 : 0xFF2A2F36);
        if (r.ok) { m_cv.thickLine(x + 13, y + 17, x + 17, y + 21, 0xFF7CFFA8, 3); m_cv.thickLine(x + 17, y + 21, x + 25, y + 11, 0xFF7CFFA8, 3); }
        text(x + 40, y + 2, 200, 16, Qt::AlignLeft, r.label, r.ok ? QColor(240, 240, 240) : QColor(150, 150, 150), 12, 800, false);
        text(x + 40, y + 18, 380, 14, Qt::AlignLeft, r.detail, QColor(150, 160, 170), 9, 600, false);
    }
    // the rank letter, stamped in
    if (t > 1.9f) {
        float k = std::min(1.f, (t - 1.9f) / 0.3f);
        int size = int(120 - 40 * k);
        uint32_t rc = rankColor(m_debriefRank);
        m_cv.blendRect(560, 110, 180, 180, withAlpha(C_BG, 220));
        m_cv.rectOutline(560, 110, 180, 180, rc, 3);
        text(560, 110, 180, 150, Qt::AlignCenter, rankLetter(m_debriefRank), qcC(rc), size, 900);
        text(560, 250, 180, 20, Qt::AlignCenter, "RANK", QColor(200, 200, 200), 12, 800, false);
    }
    if (t > 2.2f) {
        text(520, 310, 260, 20, Qt::AlignCenter, QString("+%1 COINS").arg(m_debriefCoins), QColor(255, 215, 110), 16, 900);
        text(520, 334, 260, 16, Qt::AlignCenter, QString("SCORE %1").arg(m_debriefScore), QColor(140, 220, 255), 11, 800, false);
        if (m_debriefWon && m_mission + 1 < MISSION_COUNT) text(520, 356, 260, 16, Qt::AlignCenter, QString("UNLOCKED: %1").arg(MISSIONS[m_mission + 1].name), QColor(140, 255, 160), 11, 800, false);
        if (m_debriefWon && m_mission + 1 == MISSION_COUNT) text(380, 400, 400, 40, Qt::AlignCenter | Qt::TextWordWrap, "OPERATION ASHFALL COMPLETE. THE VALLEY'S LIGHTS ARE BACK ON.", QColor(255, 225, 120), 13, 900);
    }
    menuFooter(t < 2.5f ? "ENTER skip" : "ENTER continue");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Briefing: three comic panels
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderBriefing()
{
    const MissionDef &M = MISSIONS[m_cSel];
    m_briefT += m_dt;
    // panel 1 needs a live view of the map: render it first, then shrink it into a buffer
    {
        int saveMap = m_loadedMap;
        if (m_loadedMap != M.map) { loadMapGeometry(M.map); }
        float a = m_time * 0.15f;
        const ParsedMap &pm = m_parsed[M.map];
        float cx = pm.w * 0.5f, cy = pm.h * 0.5f;
        float rr = std::min(pm.w, pm.h) * 0.42f;
        float ex = cx + std::cos(a) * rr, ey = cy + std::sin(a) * rr;
        if (!posValid(ex, ey, 0.1f)) { ex = pm.camX; ey = pm.camY; }
        renderWorldFrom(ex, ey, groundAt(ex, ey) + 2.2f, cx - ex, cy - ey, -0.18f);
        m_briefView.assign(size_t(240) * 150, 0);
        for (int y = 0; y < 150; y++) for (int x = 0; x < 240; x++) {
            int sx = x * W / 240, sy = 60 + y * (H - 120) / 150;
            m_briefView[size_t(y) * 240 + x] = m_pixels[size_t(sy) * W + sx];
        }
        (void)saveMap;
    }
    m_cv.fillRect(0, 0, W, H, 0xFF0B0E12);
    // halftone dots in the background, like a printed comic
    for (int y = 0; y < H; y += 6) for (int x = (y / 6) % 2 * 3; x < W; x += 6) m_cv.set(x, y, rgb(22, 28, 36));
    menuHeader(QString("%1  -  BRIEFING").arg(M.codename), M.name);
    for (int p = 0; p < 3; p++) {
        const BriefPanel &B = M.brief[p];
        int px = 18 + p * 258, py = 78 + (p == 1 ? 14 : 0), pw = 246, ph = 412;
        float appear = clampf((m_briefT - p * 0.5f) / 0.35f, 0.f, 1.f);
        if (appear <= 0.f) continue;
        m_cv.fillRect(px - 3, py - 3, pw + 6, ph + 6, 0xFF000000);
        m_cv.fillRect(px, py, pw, ph, rgb(232, 226, 210));                       // paper
        m_cv.fillRect(px + 6, py + 6, pw - 12, 22, rgb(30, 30, 34));
        text(px + 10, py + 6, pw - 20, 22, Qt::AlignLeft | Qt::AlignVCenter, B.title, QColor(255, 210, 90), 14, 900, false);
        int vx = px + 3, vy = py + 34, vw = 240, vh = 150;
        if (B.view == 1) {                                                         // live map flyover
            for (int y = 0; y < vh; y++) for (int x = 0; x < vw; x++) m_cv.set(vx + x, vy + y, m_briefView[size_t(y) * 240 + x]);
        } else if (B.view == 0) {                                                  // speaker portrait
            m_cv.fillRect(vx, vy, vw, vh, rgb(34, 44, 56));
            const std::vector<uint32_t> &P = m_speakerPortrait[clampi(B.speaker, 0, SPK_COUNT - 1)];
            for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) m_cv.set(vx + 56 + x, vy + 11 + y, P[size_t(y / 2) * PORT + x / 2]);
        } else {                                                                   // tactical map with the objective points
            const ParsedMap &pm = m_parsed[M.map];
            m_cv.fillRect(vx, vy, vw, vh, rgb(18, 34, 30));
            int ts = std::max(1, std::min(vw / pm.w, vh / pm.h)), ox = vx + (vw - pm.w * ts) / 2, oy = vy + (vh - pm.h * ts) / 2;
            for (int y = 0; y < pm.h; y++) for (int x = 0; x < pm.w; x++) if (pm.type[y][x]) m_cv.fillRect(ox + x * ts, oy + y * ts, ts, ts, rgb(70, 140, 110));
            for (int o = 0; o < M.objCount; o++)
                for (const char *c = M.obj[o].markers; *c; c++)
                    for (const Marker &mk : pm.markers) if (mk.kind == *c) {
                        m_cv.fillCircle(ox + int(mk.x * ts), oy + int(mk.y * ts), 3, rgb(255, 200, 60));
                        text(ox + mk.x * ts + 4, oy + mk.y * ts - 6, 14, 12, Qt::AlignLeft, QString::number(o + 1), QColor(255, 220, 120), 8, 900, false);
                    }
            m_cv.fillCircle(ox + int(pm.spawnX * ts), oy + int(pm.spawnY * ts), 3, rgb(90, 200, 255));
        }
        m_cv.rectOutline(vx, vy, vw, vh, 0xFF000000, 2);
        // speech: who + typed-out text in a bubble
        int sy = vy + vh + 10;
        m_cv.fillRect(px + 8, sy, pw - 16, ph - (sy - py) - 12, rgb(250, 248, 240));
        m_cv.rectOutline(px + 8, sy, pw - 16, ph - (sy - py) - 12, 0xFF000000, 2);
        m_cv.tri(px + 30, sy, px + 44, sy, px + 34, sy - 10, rgb(250, 248, 240), rgb(250, 248, 240), rgb(250, 248, 240));
        text(px + 16, sy + 8, pw - 32, 18, Qt::AlignLeft, SPEAKER_NAMES[clampi(B.speaker, 0, SPK_COUNT - 1)], QColor(160, 40, 40), 14, 900, false);
        int chars = std::max(0, int((m_briefT - p * 0.5f - 0.3f) * 60.f));
        text(px + 16, sy + 34, pw - 32, ph - (sy - py) - 52, Qt::AlignLeft | Qt::TextWordWrap, QString(B.text).left(chars), QColor(30, 30, 34), 14, 700, false);
    }
    menuFooter(m_briefT < 6.f ? "ENTER skip   ESC back" : "ENTER deploy   ESC back");
}

// Render the world from an arbitrary camera (briefing views, flybys)
void MainWindow::renderWorldFrom(float cx, float cy, float cz, float dx, float dy, float slope)
{
    Player savedPl = m_gs.player;
    float sCamX = m_camX, sCamY = m_camY, sEye = m_eye, sK = m_K, sFov = m_adsFOVMult, sBoom = m_boomT;
    int sHor = m_hor;
    float l = std::sqrt(dx * dx + dy * dy) + 1e-6f;
    Player &pl = m_gs.player;
    pl.dirX = dx / l; pl.dirY = dy / l;
    pl.planeX = -pl.dirY * m_planeLen; pl.planeY = pl.dirX * m_planeLen;
    m_adsFOVMult = 1.f; m_K = float(H);
    m_camX = cx; m_camY = cy; m_eye = cz;
    m_hor = int(H / 2.f - slope * m_K);
    m_boomT = 0.f;
    renderTerrain();
    renderSky();
    renderActors3D();
    renderSprites();
    renderParticles();
    renderWeather();
    m_gs.player = savedPl;
    m_camX = sCamX; m_camY = sCamY; m_eye = sEye; m_K = sK; m_adsFOVMult = sFov; m_hor = sHor; m_boomT = sBoom;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Intro flyby: a Catmull-Rom spline through the mission's points
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::startFlyby()
{
    const MissionDef &M = MISSIONS[m_mission];
    m_flyPts.clear();
    const ParsedMap &pm = m_parsed[M.map];
    const Player &pl = m_gs.player;
    m_flyPts.push_back({pm.w * 0.5f, pm.h * 0.5f, groundAt(pm.w * 0.5f, pm.h * 0.5f) + 9.f});   // high over the middle
    for (int o = 0; o < M.objCount && m_flyPts.size() < 5; o++) {
        float x, y;
        char c = M.obj[o].markers[0] ? M.obj[o].markers[0] : M.bossMarkers[0];
        if (missionMarker(c, x, y)) m_flyPts.push_back({x, y, groundAt(x, y) + 4.5f});
    }
    float bx = pl.posX - pl.dirX * 3.f, by = pl.posY - pl.dirY * 3.f;
    if (bx < 1.f || by < 1.f || bx > m_mapW - 1.f || by > m_mapH - 1.f) { bx = pl.posX; by = pl.posY; }
    m_flyPts.push_back({bx, by, pl.footZ + 1.6f});
    m_flyPts.push_back({pl.posX, pl.posY, pl.footZ + 0.5f});
    m_flyT = 0.f;
    m_flyDur = 2.0f * float(m_flyPts.size());
    m_flyActive = !m_testMode || m_flyForTest;
}

// Catmull-Rom: passes through every control point with smooth tangents
static float catmull(float p0, float p1, float p2, float p3, float t)
{
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

void MainWindow::flybyPoint(float u, float &x, float &y, float &z) const
{
    int n = int(m_flyPts.size());
    float f = clampf(u, 0.f, 0.9999f) * (n - 1);
    int i = int(f);
    float t = f - i;
    auto P = [&](int k) { return m_flyPts[size_t(clampi(k, 0, n - 1))]; };
    x = catmull(P(i - 1).x, P(i).x, P(i + 1).x, P(i + 2).x, t);
    y = catmull(P(i - 1).y, P(i).y, P(i + 1).y, P(i + 2).y, t);
    z = catmull(P(i - 1).z, P(i).z, P(i + 1).z, P(i + 2).z, t);
}

void MainWindow::renderFlyby()
{
    const MissionDef &M = MISSIONS[m_mission];
    float u = m_flyT / m_flyDur;
    float x, y, z;
    flybyPoint(u, x, y, z);
    // look across the middle of the map while flying, then settle into the player's own view
    float x2 = m_mapW * 0.5f, y2 = m_mapH * 0.5f, z2 = groundAt(x2, y2);
    float settle = smoothstep01((u - 0.72f) / 0.28f);
    const Player &pl = m_gs.player;
    x2 += (pl.posX + pl.dirX * 8.f - x2) * settle; y2 += (pl.posY + pl.dirY * 8.f - y2) * settle; z2 += (pl.footZ + 0.5f - z2) * settle;
    if ((x2 - x) * (x2 - x) + (y2 - y) * (y2 - y) < 4.f) { x2 = x + pl.dirX * 4.f; y2 = y + pl.dirY * 4.f; }
    // never dip into the ground or a wall top on the way (the last point is the player's eye)
    if (u < 0.85f) {
        float hi = 0.f;
        for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) hi = std::max(hi, groundAt(x + ox * 0.7f, y + oy * 0.7f));
        z = std::max(z, hi + 1.6f);
    }
    float dx = x2 - x, dy = y2 - y, dz = z2 - z, hl = std::sqrt(dx * dx + dy * dy) + 1e-4f;
    if (hl < 0.05f) { dx = m_gs.player.dirX; dy = m_gs.player.dirY; hl = 1.f; }
    renderWorldFrom(x, y, z, dx, dy, clampf(-dz / hl, -0.2f, 0.6f));   // positive = look down
    // letterbox + title card
    m_cv.fillRect(0, 0, W, 60, 0xFF000000);
    m_cv.fillRect(0, H - 60, W, 60, 0xFF000000);
    float a = std::min(1.f, m_flyT / 0.6f) * std::min(1.f, (m_flyDur - m_flyT) / 0.6f);
    text(0, H - 56, W, 30, Qt::AlignCenter, QString("%1:  %2").arg(M.codename).arg(M.name), QColor(255, 255, 255, int(255 * a)), 20, 900);
    text(0, H - 26, W, 16, Qt::AlignCenter, QString("%1   |   ANY KEY TO SKIP").arg(MAP_NAMES[M.map]), QColor(170, 170, 170, int(220 * a)), 10, 700, false);
    flushText();
}
