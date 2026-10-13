// =============================================================================
//  killcam.cpp  -  Replay of your death from the killer's eyes
//
//  Every game tick copies the state that matters for a picture (you, the
//  hostiles, their rounds, tracers, the squad) into a 4-second ring buffer.
//  When you die, the last ~2.5 s are played back at 60% speed: the camera
//  sits at the shooter's head looking at you, your body is the 3D soldier,
//  and the world is rendered by the same raster pipeline as the game.
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static const int KC_CAP = 240;               // 4 s at 60 ticks
static const float KC_REPLAY = 2.5f;         // seconds of history replayed
static const float KC_SPEED = 0.6f;          // slow motion

void MainWindow::recordKillcamFrame()
{
    if (m_kcFrames.size() < size_t(KC_CAP)) m_kcFrames.resize(size_t(KC_CAP));
    KillcamFrame &f = m_kcFrames[size_t(m_kcHead)];
    f.player = m_gs.player;
    f.enemies = m_gs.enemies;
    f.bullets = m_gs.bullets;
    f.tracers = m_gs.tracers;
    f.actors = m_gs.actors;
    f.headZ = m_gs.player.footZ + 0.5f;
    m_kcHead = (m_kcHead + 1) % KC_CAP;
    m_kcCount = std::min(KC_CAP, m_kcCount + 1);
}

void MainWindow::startKillcam()
{
    if (m_kcCount < 20) { m_killcamActive = false; return; }
    m_killcamActive = true;
    m_killcamT = 0.f;
    int frames = std::min(m_kcCount, int(KC_REPLAY * 60.f));
    m_kcStart = (m_kcHead - frames + KC_CAP) % KC_CAP;
    m_kcLen = frames;
}

bool MainWindow::killcamActive() const { return m_killcamActive; }

void MainWindow::updateKillcam(float dt)
{
    if (!m_killcamActive) return;
    m_killcamT += dt;
    if (m_killcamT * KC_SPEED * 60.f >= m_kcLen + 30) m_killcamActive = false;     // hold the last frame a moment
}

void MainWindow::renderKillcam()
{
    int idx = std::min(m_kcLen - 1, int(m_killcamT * KC_SPEED * 60.f));
    const KillcamFrame &f = m_kcFrames[size_t((m_kcStart + idx) % KC_CAP)];

    // swap the recorded moment in
    Player savedPl = m_gs.player;
    std::vector<Enemy> savedEn; savedEn.swap(m_gs.enemies);
    std::vector<Bullet> savedBu; savedBu.swap(m_gs.bullets);
    std::vector<Tracer> savedTr; savedTr.swap(m_gs.tracers);
    std::vector<Actor> savedAc; savedAc.swap(m_gs.actors);
    std::vector<Particle> savedPa; savedPa.swap(m_gs.particles);
    float sCamX = m_camX, sCamY = m_camY, sEye = m_eye, sK = m_K, sFov = m_adsFOVMult, sBoom = m_boomT, sBoomCur = m_boomCur;
    int sHor = m_hor;

    m_gs.player = f.player;
    m_gs.enemies = f.enemies;
    m_gs.bullets = f.bullets;
    m_gs.tracers = f.tracers;
    m_gs.actors = f.actors;
    Player &pl = m_gs.player;
    float px = pl.posX, py = pl.posY, pz = f.headZ;

    // camera: the killer's head, or a slow orbit if nothing in particular killed you
    float cx, cy, cz;
    QString who;
    bool fromKiller = m_killerIdx >= 0 && m_killerIdx < int(m_gs.enemies.size());
    if (fromKiller) {
        Enemy &k = m_gs.enemies[size_t(m_killerIdx)];
        float dx = px - k.x, dy = py - k.y, dl = std::sqrt(dx * dx + dy * dy) + 1e-4f;
        cx = k.x - dx / dl * 0.25f; cy = k.y - dy / dl * 0.25f;
        cz = floorH(k.x, k.y) + 0.78f * k.scale;
        k.alive = false; k.corpseFade = 0.f;                // don't draw the shooter in its own face
        who = k.boss ? "COMMANDER" : k.elite ? "ELITE GUARD" : k.type == ET_SNIPER ? "SNIPER" : k.type == ET_SHOTGUNNER ? "SHOTGUNNER" : k.type == ET_SCOUT ? "SCOUT" : "RIFLEMAN";
        who += QString("  -  %1 U AWAY").arg(dl, 0, 'f', 1);
    } else {
        float a = m_killcamT * 0.5f + 1.f;
        cx = px + std::cos(a) * 2.6f; cy = py + std::sin(a) * 2.6f; cz = pz + 0.6f;
        if (!posValid(cx, cy, 0.1f)) { cx = px - pl.dirX * 1.5f; cy = py - pl.dirY * 1.5f; }
        cz = std::max(cz, groundAt(cx, cy) + 0.3f);
        if (m_killerIdx == -2) who = "YOUR OWN EXPLOSIVE";
        else if (m_killerIdx == -3) who = QString(m_curHazardName ? m_curHazardName : THEMES[m_theme].hazName);
        else who = "THE ENVIRONMENT";
    }
    float lx = px - cx, ly = py - cy, ll = std::sqrt(lx * lx + ly * ly) + 1e-4f;
    m_kcBodyYaw = std::atan2(f.player.dirY, f.player.dirX);
    pl.dirX = lx / ll; pl.dirY = ly / ll;
    pl.planeX = -pl.dirY * m_planeLen; pl.planeY = pl.dirX * m_planeLen;
    m_adsFOVMult = fromKiller ? std::max(0.45f, std::min(1.f, 6.f / ll)) : 1.f;   // a sniper's view is zoomed
    m_K = float(H) / m_adsFOVMult;
    m_camX = cx; m_camY = cy; m_eye = cz;
    float slope = (pz - cz) / ll;
    m_hor = int(H / 2.f - slope * m_K);
    m_boomT = 0.f; m_boomCur = 0.f;
    m_kcBody = true;

    renderTerrain();
    renderSky();
    renderActors3D();
    renderSprites();
    renderTracers();

    m_kcBody = false;
    // restore the present
    m_gs.player = savedPl;
    m_gs.enemies.swap(savedEn); m_gs.bullets.swap(savedBu); m_gs.tracers.swap(savedTr);
    m_gs.actors.swap(savedAc); m_gs.particles.swap(savedPa);
    m_camX = sCamX; m_camY = sCamY; m_eye = sEye; m_K = sK; m_adsFOVMult = sFov; m_hor = sHor; m_boomT = sBoom; m_boomCur = sBoomCur;

    // film look: desaturate + red tint, letterbox, label
    for (int i = 0; i < W * H; i++) {
        uint32_t c = m_pixels[i];
        int l = (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 8;
        m_pixels[i] = rgb(std::min(255, (l * 3 + R(c)) / 4 + 18), (l * 3 + G(c)) / 4, (l * 3 + B(c)) / 4);
    }
    m_cv.fillRect(0, 0, W, 54, 0xFF000000);
    m_cv.fillRect(0, H - 54, W, 54, 0xFF000000);
    bool blink = int(m_killcamT * 3) % 2 == 0;
    m_cv.fillCircle(30, 27, 6, blink ? rgb(240, 50, 40) : rgb(90, 20, 20));
    text(44, 12, 300, 30, Qt::AlignLeft | Qt::AlignVCenter, "KILLCAM", QColor(255, 255, 255), 22, 900);
    text(W - 420, 12, 400, 30, Qt::AlignRight | Qt::AlignVCenter, QString("KILLED BY  %1").arg(who), QColor(255, 120, 100), 12, 800, false);
    float p = std::min(1.f, idx / float(std::max(1, m_kcLen - 1)));
    m_cv.blendRect(20, H - 34, W - 40, 4, withAlpha(0xFFFFFFFF, 40));
    m_cv.fillRect(20, H - 34, int((W - 40) * p), 4, rgb(240, 70, 60));
    text(0, H - 26, W, 16, Qt::AlignCenter, "REPLAY  60% SPEED      ANY KEY TO SKIP", QColor(170, 170, 170), 10, 700, false);
    flushText();
}
