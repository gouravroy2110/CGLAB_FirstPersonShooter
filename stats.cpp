// =============================================================================
//  stats.cpp  -  Lifetime service record
//
//  Counts what you do across every mode and session: kills, headshots, deaths,
//  accuracy (overall and per weapon), time played, missions, bosses, secrets,
//  best streak, longest kill. Shots and hits are read from the game's own
//  counters each frame and credited to the gun in your hands. Saved with
//  QSettings (never from the test suite) and shown on the STATS screen with
//  your favourite gun on the turntable.
// =============================================================================
#include "mainwindow.h"
#include "missions.h"
#include <QSettings>
#include <cmath>
#include <algorithm>

using namespace rs;

static const uint32_t S_AMBER = 0xFFFFB43A, S_BG = 0xFF0E1216;
static inline QColor qcS(uint32_t c) { return QColor(R(c), G(c), B(c)); }

void MainWindow::loadStats()
{
    m_stats = LifetimeStats{};
    if (m_testMode) return;
    QSettings s("RasterFPS", "RasterFPS");
    s.beginGroup("stats");
    m_stats.kills = s.value("kills", 0).toInt();
    m_stats.headshots = s.value("headshots", 0).toInt();
    m_stats.deaths = s.value("deaths", 0).toInt();
    m_stats.shots = s.value("shots", 0).toInt();
    m_stats.hits = s.value("hits", 0).toInt();
    m_stats.missionsWon = s.value("missions", 0).toInt();
    m_stats.bosses = s.value("bosses", 0).toInt();
    m_stats.secrets = s.value("secrets", 0).toInt();
    m_stats.zones = s.value("zones", 0).toInt();
    m_stats.bestStreak = s.value("streak", 0).toInt();
    m_stats.bestScore = s.value("bestScore", 0).toInt();
    m_stats.longestKill = s.value("longestKill", 0.f).toFloat();
    m_stats.seconds = s.value("seconds", 0.0).toDouble();
    for (int w = 0; w < WPN_COUNT; w++) {
        m_stats.wKills[w] = s.value(QString("wk%1").arg(w), 0).toInt();
        m_stats.wShots[w] = s.value(QString("ws%1").arg(w), 0).toInt();
        m_stats.wHits[w] = s.value(QString("wh%1").arg(w), 0).toInt();
    }
    s.endGroup();
}

void MainWindow::saveStats() const
{
    if (m_testMode) return;
    QSettings s("RasterFPS", "RasterFPS");
    s.beginGroup("stats");
    s.setValue("kills", m_stats.kills);
    s.setValue("headshots", m_stats.headshots);
    s.setValue("deaths", m_stats.deaths);
    s.setValue("shots", m_stats.shots);
    s.setValue("hits", m_stats.hits);
    s.setValue("missions", m_stats.missionsWon);
    s.setValue("bosses", m_stats.bosses);
    s.setValue("secrets", m_stats.secrets);
    s.setValue("zones", m_stats.zones);
    s.setValue("streak", m_stats.bestStreak);
    s.setValue("bestScore", m_stats.bestScore);
    s.setValue("longestKill", m_stats.longestKill);
    s.setValue("seconds", m_stats.seconds);
    for (int w = 0; w < WPN_COUNT; w++) {
        s.setValue(QString("wk%1").arg(w), m_stats.wKills[w]);
        s.setValue(QString("ws%1").arg(w), m_stats.wShots[w]);
        s.setValue(QString("wh%1").arg(w), m_stats.wHits[w]);
    }
    s.endGroup();
}

// Called every simulated frame while playing
void MainWindow::trackStats(float dt)
{
    const GameState &g = m_gs;
    int w = g.player.curWeapon;
    // a new game resets the game's counters: re-sync instead of counting backwards
    if (g.shotsFired < m_statsShotsSeen) m_statsShotsSeen = g.shotsFired;
    if (g.shotsHit < m_statsHitsSeen) m_statsHitsSeen = g.shotsHit;
    if (g.secretsFound < m_statsSecretsSeen) m_statsSecretsSeen = g.secretsFound;
    int ds = g.shotsFired - m_statsShotsSeen, dh = g.shotsHit - m_statsHitsSeen;
    m_stats.shots += ds; m_stats.hits += dh;
    if (w >= 0 && w < WPN_COUNT) { m_stats.wShots[w] += ds; m_stats.wHits[w] += dh; }
    m_statsShotsSeen = g.shotsFired; m_statsHitsSeen = g.shotsHit;
    m_stats.secrets += g.secretsFound - m_statsSecretsSeen;
    m_statsSecretsSeen = g.secretsFound;
    if (g.gameOver && !m_statsPrevOver) m_stats.deaths++;
    m_statsPrevOver = g.gameOver;
    if (g.levelComplete && !m_statsPrevZone && !m_campaignRun) m_stats.zones++;
    m_statsPrevZone = g.levelComplete;
    m_stats.bestStreak = std::max(m_stats.bestStreak, m_combo);
    m_stats.bestScore = std::max(m_stats.bestScore, g.score);
    if (!m_paused) m_stats.seconds += dt;
    m_statsSaveT += dt;
    if (m_statsSaveT > 30.f) { m_statsSaveT = 0.f; saveStats(); }
}

// A kill by the player
void MainWindow::statKill(bool head, float dist, bool boss)
{
    int w = m_gs.player.curWeapon;
    m_stats.kills++;
    if (head) m_stats.headshots++;
    if (boss) m_stats.bosses++;
    if (w >= 0 && w < WPN_COUNT) m_stats.wKills[w]++;
    m_stats.longestKill = std::max(m_stats.longestKill, dist);
    if (head && dist > 18.f) m_cmdLongShot = true;              // the radio compliments a long headshot
}

void MainWindow::renderStats()
{
    const LifetimeStats &S = m_stats;
    int hours = int(S.seconds / 3600.0), mins = int(std::fmod(S.seconds, 3600.0) / 60.0);
    menuHeader(QString("SERVICE RECORD   -   %1 H %2 MIN IN THE FIELD").arg(hours).arg(mins), "STATS");
    // career tiles
    auto pct = [](int a, int b) { return b > 0 ? QString("%1%").arg(int(100.0 * a / b + 0.5)) : QString("--"); };
    struct Tile { const char *label; QString value; };
    Tile tiles[12] = {
        { "KILLS", QString::number(S.kills) },
        { "HEADSHOTS", QString("%1  (%2)").arg(S.headshots).arg(pct(S.headshots, S.kills)) },
        { "ACCURACY", pct(S.hits, S.shots) },
        { "DEATHS", QString::number(S.deaths) },
        { "K / D", S.deaths ? QString::number(double(S.kills) / S.deaths, 'f', 2) : QString::number(S.kills) },
        { "BEST STREAK", QString("x%1").arg(S.bestStreak) },
        { "MISSIONS WON", QString::number(S.missionsWon) },
        { "BOSSES DOWN", QString::number(S.bosses) },
        { "SECRETS FOUND", QString::number(S.secrets) },
        { "ZONES CLEARED", QString::number(S.zones) },
        { "LONGEST KILL", QString("%1 U").arg(S.longestKill, 0, 'f', 1) },
        { "BEST SCORE", QString::number(S.bestScore) },
    };
    for (int i = 0; i < 12; i++) {
        int x = 20 + (i % 3) * 124, y = 82 + (i / 3) * 62;
        m_cv.blendRect(x, y, 118, 56, withAlpha(S_BG, 200));
        m_cv.fillRect(x, y, 118, 2, withAlpha(S_AMBER, 160));
        text(x + 8, y + 7, 106, 12, Qt::AlignLeft, tiles[i].label, QColor(140, 150, 160), 8, 800, false);
        text(x + 8, y + 22, 106, 28, Qt::AlignLeft | Qt::AlignVCenter, tiles[i].value, QColor(255, 255, 255), 15, 900);
    }
    // campaign ranks (loaded when the screen opens)
    int ry = 82 + 4 * 62 + 6;
    text(20, ry, 360, 14, Qt::AlignLeft, "CAMPAIGN RANKS", qcS(S_AMBER), 10, 800, false);
    static const char *RL[5] = { "-", "C", "B", "A", "S" };
    static const uint32_t RC[5] = { 0xFF505860, 0xFFB0B4B8, 0xFF5AE68C, 0xFF4AB4FF, 0xFFFFC23A };
    for (int m = 0; m < MISSION_COUNT; m++) {
        int x = 20 + m * 40, y = ry + 18;
        int rk = clampi(m_cRank[m], 0, 4);
        m_cv.blendRect(x, y, 36, 36, withAlpha(RC[rk], rk ? 220 : 90));
        text(x, y, 36, 24, Qt::AlignCenter, RL[rk], QColor(12, 12, 12), 16, 900, false);
        text(x, y + 22, 36, 12, Qt::AlignCenter, QString::number(m + 1), QColor(20, 20, 20), 8, 800, false);
    }
    // favourite gun on the turntable + per-weapon table
    int order[WPN_COUNT];
    for (int w = 0; w < WPN_COUNT; w++) order[w] = w;
    std::sort(order, order + WPN_COUNT, [&](int a, int b) { return S.wKills[a] != S.wKills[b] ? S.wKills[a] > S.wKills[b] : S.wShots[a] > S.wShots[b]; });
    int fav = order[0];
    const int tx = 400, ty = 82, tw = 380, th = 150;
    drawGunStage(fav, tx, ty, tw, th, m_time * 0.5f);
    text(tx + 12, ty + 8, tw - 24, 14, Qt::AlignLeft, S.wKills[fav] ? "FAVOURITE WEAPON" : "NO KILLS YET", qcS(S_AMBER), 9, 800, false);
    text(tx + 12, ty + 22, tw - 24, 22, Qt::AlignLeft, WEAPONS[fav].name, QColor(255, 255, 255), 14, 900);
    int ty2 = ty + th + 8;
    m_cv.blendRect(tx, ty2, tw, 18, withAlpha(S_BG, 220));
    text(tx + 8, ty2, 150, 18, Qt::AlignLeft | Qt::AlignVCenter, "WEAPON", QColor(140, 150, 160), 8, 800, false);
    text(tx + 160, ty2, 60, 18, Qt::AlignRight | Qt::AlignVCenter, "KILLS", QColor(140, 150, 160), 8, 800, false);
    text(tx + 225, ty2, 60, 18, Qt::AlignRight | Qt::AlignVCenter, "ACC", QColor(140, 150, 160), 8, 800, false);
    int maxK = std::max(1, S.wKills[fav]);
    for (int r = 0; r < 12; r++) {
        int w = order[r];
        if (S.wKills[w] == 0 && S.wShots[w] == 0) break;
        int y = ty2 + 20 + r * 21;
        m_cv.blendRect(tx, y, tw, 19, withAlpha(S_BG, 170));
        text(tx + 8, y, 150, 19, Qt::AlignLeft | Qt::AlignVCenter, WEAPONS[w].shortName, QColor(230, 230, 230), 9, 800, false);
        text(tx + 160, y, 60, 19, Qt::AlignRight | Qt::AlignVCenter, QString::number(S.wKills[w]), QColor(255, 255, 255), 9, 900, false);
        text(tx + 225, y, 60, 19, Qt::AlignRight | Qt::AlignVCenter, pct(S.wHits[w], S.wShots[w]), QColor(140, 220, 255), 9, 800, false);
        m_cv.blendRect(tx + 295, y + 7, 76, 5, withAlpha(0xFFFFFFFF, 25));
        m_cv.fillRect(tx + 295, y + 7, int(76.f * S.wKills[w] / maxK), 5, S_AMBER);
    }
    menuFooter("ESC back");
}

void MainWindow::openStats()
{
    loadCampaign();
    m_screen = SCR_STATS;
}

MainWindow::~MainWindow()
{
    saveStats();                                          // whatever happened since the last autosave
}
