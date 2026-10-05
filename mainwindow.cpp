// =============================================================================
//  mainwindow.cpp  -  Construction, game loop, game logic, input, hitscan
//
//  RASTER PROOF: every visible pixel is written by the CPU into m_buf
//  (a QImage used purely as a uint32_t framebuffer). QPainter is used for
//  one thing only: copying the finished image to the window. Text uses our
//  bitmap pixel font (pixelfont.h); window scaling and the zoom magnifier are
//  our own nearest-neighbour loop (paintEvent).
//
//  World / terrain logic lives in world.cpp; rendering lives in:
//    render_world.cpp   heightfield terrain + walls, sky, sprites, particles, tracers
//    render_weapon.cpp  first-person weapon, hands, scope, reflex sight
//    render_hud.cpp     HUD, menus, overlays
// =============================================================================
#include "mainwindow.h"
#include "pixelfont.h"
#include <QPainter>
#include <QDir>
#include <QFile>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

static inline float frand() { return float(rand()) / float(RAND_MAX); }
static inline float frand(float a, float b) { return a + (b - a) * frand(); }

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor — bake every texture procedurally
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), m_buf(W, H, QImage::Format_ARGB32)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(W / 2, H / 2);
    setWindowTitle("RASTER  -  Tactical Shooter");
    resize(W, H);
    m_pixels = reinterpret_cast<uint32_t *>(m_buf.bits());
    m_cv = rs::Canvas{m_pixels, W, H};
    m_depth.assign(size_t(W) * H, 1e30f);
    std::memset(m_map, 0, sizeof(m_map));
    for (int m = 0; m < 3; m++) parseLayout(ALL_LAYOUTS[m], m_parsed[m]);

    std::vector<uint32_t> tmp;
    for (int t = 0; t < 3; t++) {
        bakeSky(m_sky[t], t);
        bakeFloor(tmp, t);
        m_floorTex[t].build(tmp, FLOOR_TS);
        for (int type = 1; type <= 5; type++) {
            bakeWall(tmp, type, t);
            m_wallTex[t][type].build(tmp, WALL_TS);
        }
    }
    bakeEnemyAtlas(m_enemyAtlas, m_enemyMask, ENEMY_LOOK);
    bakeEnemyAtlas(m_eliteAtlas, m_eliteMask, ELITE_LOOK);
    bakePickups(m_pickupTex);
    for (int c = 0; c < 3; c++) {
        m_portrait[c].assign(PORTRAIT * PORTRAIT, 0);
        paintSoldier(m_portrait[c].data(), nullptr, PORTRAIT, lookForClass(c), POSE_WALK, 0.f, c == 2);
    }
    // Bake every map's light map up front so menus never hitch
    for (int m = 0; m < 3; m++) loadMapGeometry(m);
    loadMapGeometry(m_mapChoice);

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);
    m_clock.start();
}

// ─────────────────────────────────────────────────────────────────────────────
//  initGame / loadLevel
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::initGame()
{
    m_gs = GameState{};
    m_gs.mapChoice  = m_mapChoice;
    m_gs.charClass  = CharClass(m_charChoice);
    m_gs.difficulty = Difficulty(m_difficulty);

    const CharProfile &cp = CHAR_PROFILES[m_charChoice];
    const DiffConfig  &dc = DIFF_CFG[m_difficulty];
    Player &pl = m_gs.player;
    pl.hp              = cp.hp;
    pl.stamina         = cp.stamina;
    pl.speedMult       = cp.speedMult + dc.speedBonus;
    pl.damageReduction = cp.damageReduction;
    pl.curWeapon = WPN_PISTOL;
    pl.mag[WPN_PISTOL] = WEAPONS[WPN_PISTOL].magSize;
    pl.reserve[WPN_PISTOL] = cp.ammo * 2;

    m_combo = 0; m_comboTimer = 0; m_comboDisplay = 0;
    m_pitch = 0; m_recoilPitch = 0; m_aimX = W / 2.f; m_aimY = H / 2.f; m_adsT = 0; m_adsFOVMult = 1;
    m_hitMarker = m_headMarker = m_killMarker = m_headshotBanner = 0; m_damageDirTimer = 0;
    m_velZ = 0; m_onGround = true; m_acidAcc = 0; m_kick = m_actionT = 0; m_pendingWeapon = -1;
    m_killFeed.clear(); m_casings.clear();
    std::memset(m_enemyHPDisplay, 0, sizeof(m_enemyHPDisplay));
    loadLevel(1);
    m_paused = false;
}

int MainWindow::enemyMaxHP() const
{
    int idx = std::min(m_gs.level, 5);
    return std::max(1, int(float(LEVEL_CFG[idx].enemyHP + (m_gs.level > 5 ? (m_gs.level - 5) * 25 : 0)) *
                           DIFF_CFG[m_difficulty].enemyHPMult));
}

void MainWindow::loadLevel(int lv)
{
    int idx = std::min(lv, 5);
    const LevelCfg   &cfg = LEVEL_CFG[idx];
    const DiffConfig &dc  = DIFF_CFG[m_difficulty];

    m_gs.level = lv; m_gs.killsThisLevel = 0; m_gs.killsNeeded = cfg.killsNeeded;
    m_gs.levelComplete = false; m_gs.levelTransTimer = 0;
    m_gs.bullets.clear(); m_gs.tracers.clear(); m_gs.particles.clear(); m_gs.healthKits.clear();
    m_gs.weaponPickups.clear(); m_gs.damageNums.clear();
    m_gs.enemyBulletSpeed = cfg.bulletSpeed  * dc.bulletSpeedMult;
    m_gs.enemyFireRate    = cfg.fireCooldown * dc.fireCoolMult;

    Player &pl = m_gs.player;
    if (lv > 1) {
        pl.hp = std::min(pl.hp + 30, CHAR_PROFILES[m_charChoice].hp);
        int w = pl.curWeapon;
        pl.reserve[w] = std::min(pl.reserve[w] + WEAPONS[w].magSize, WEAPONS[w].maxReserve);
        pl.damageFlash = 0;
    }

    loadMapGeometry(m_gs.mapChoice);

    const ParsedMap &pm = m_parsed[m_gs.mapChoice];
    pl.posX = pm.spawnX;
    pl.posY = pm.spawnY;
    m_footZ = floorH(pl.posX, pl.posY); m_velZ = 0; m_onGround = true;
    pl.dirX = 1.f;  pl.dirY = 0.f;
    pl.planeX = 0.f; pl.planeY = 0.66f;
    pl.shakeY = 0;  pl.adsMode = false;
    pl.reloadTimer = 0; pl.switchTimer = 0;
    m_pitch = 0; m_aimX = W / 2.f; m_aimY = H / 2.f;

    // Enemies: a random subset of the map's patrol spawns, plus every vault guard
    m_gs.enemies.clear();
    std::vector<Marker> spawns, kits;
    for (const Marker &mk : pm.markers) { if (mk.kind == 'e') spawns.push_back(mk); if (mk.kind == 'k') kits.push_back(mk); }
    for (size_t i = spawns.size(); i > 1; i--) std::swap(spawns[i - 1], spawns[size_t(rand()) % i]);
    for (size_t i = kits.size(); i > 1; i--) std::swap(kits[i - 1], kits[size_t(rand()) % i]);
    int hp = enemyMaxHP();
    auto addEnemy = [&](float x, float y, bool elite) {
        Enemy e; e.x = x; e.y = y; e.elite = elite;
        e.hp = e.maxHp = elite ? hp * 2 : hp;
        float baseRange = elite ? cfg.aggroRange + 3.f : cfg.aggroRange;
        e.aggroRange = baseRange * dc.aggroRangeMult;
        e.fireCooldown = frand(1.f, 3.f) * dc.fireCoolMult;
        e.wanderTimer = frand(0.f, 3.f);
        e.walkPhase = frand();
        m_gs.enemies.push_back(e);
    };
    int nEnemies = std::min(cfg.numEnemies + (lv > 5 ? 2 : 0), int(spawns.size()));
    for (int i = 0; i < nEnemies; i++) addEnemy(spawns[i].x, spawns[i].y, false);
    for (const Marker &mk : pm.markers) if (mk.kind == 'x') addEnemy(mk.x, mk.y, true);
    m_gs.killsNeeded = std::min(m_gs.killsNeeded, int(m_gs.enemies.size()));

    // Pickups placed by the level designer (medkit count scales with difficulty)
    for (int i = 0; i < std::min(dc.numKits, int(kits.size())); i++) {
        HealthKit k; k.x = kits[i].x; k.y = kits[i].y; k.active = true; k.bobPhase = float(i) * 1.2f;
        m_gs.healthKits.push_back(k);
    }
    m_gs.secretsTotal = 0;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) if (m_map[y][x] == CELL_SECRET) m_gs.secretsTotal++;
    for (const Marker &mk : pm.markers) {
        WeaponPickup wp; wp.x = mk.x; wp.y = mk.y; wp.bobPhase = frand(0, 6.28f);
        if (mk.kind == 'a') wp.type = PICKUP_AMMO;
        else if (mk.kind == 'r') wp.type = PICKUP_RIFLE;
        else if (mk.kind == 's') wp.type = PICKUP_SHOTGUN;
        else if (mk.kind == 't') wp.type = PICKUP_INTEL;
        else continue;
        m_gs.weaponPickups.push_back(wp);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main loop
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::tick()
{
    double now = m_clock.elapsed();
    float dt = float((now - m_lastMs) / 1000.0);
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.001f) dt = 0.001f;
    m_fps = 0.92 * m_fps + 0.08 / double(dt);
    m_lastMs = now;

    advance(dt);

    bool captured = m_screen == SCR_PLAYING && !m_paused && !m_gs.gameOver && hasFocus();
    Qt::CursorShape want = captured ? Qt::BlankCursor : Qt::ArrowCursor;
    if (cursor().shape() != want) setCursor(want);

    render();
    QWidget::update();
}

void MainWindow::advance(float dt)
{
    m_dt = dt;
    m_time += dt;
    m_menuFrame++;
    Player &pl = m_gs.player;
    bool playing = m_screen == SCR_PLAYING;

    if (!playing) {
        m_menuCamAng += dt * 0.10f;
        if (m_loadedMap != m_mapChoice) loadMapGeometry(m_mapChoice);
    }
    if (playing && !m_paused && !m_gs.gameOver) gameStep(dt);

    // ADS blend + FOV
    const WeaponDef &wd = WEAPONS[pl.curWeapon];
    bool canAim = playing && pl.adsMode && pl.reloadTimer <= 0 && pl.switchTimer <= 0 && !pl.sprinting && !m_gs.gameOver;
    m_adsT += ((canAim ? 1.f : 0.f) - m_adsT) * std::min(1.f, dt * 12.f);
    float adsFov = wd.scope ? 1.f / m_scopeMag : wd.adsFov;
    m_adsFOVMult = 1.f + (adsFov - 1.f) * m_adsT;

    // Aiming down sights pulls the free crosshair back to the centre (the
    // camera absorbs the offset, so the aimed-at point stays put)
    if (playing && canAim) {
        float f = std::min(1.f, dt * 14.f);
        float ox = (m_aimX - W / 2.f) * f, oy = (m_aimY - H / 2.f) * f;
        turn(std::atan(2.f * ox / W * 0.66f * m_adsFOVMult));
        m_pitch = rs::clampf(m_pitch - oy / m_K, -0.75f, 0.75f);
        m_aimX -= ox; m_aimY -= oy;
    }

    m_sprintT += ((pl.sprinting ? 1.f : 0.f) - m_sprintT) * std::min(1.f, dt * 8.f);

    // Weapon sway lags behind mouse motion
    m_swayX += (-m_mouseDX * 0.9f - m_swayX) * std::min(1.f, dt * 9.f);
    m_swayY += (-m_mouseDY * 0.9f - m_swayY) * std::min(1.f, dt * 9.f);
    m_swayX = rs::clampf(m_swayX, -40.f, 40.f); m_swayY = rs::clampf(m_swayY, -30.f, 30.f);
    m_mouseDX = m_mouseDY = 0;

    // Scope breathing sway (figure-8). Hold SHIFT while scoped to steady.
    bool movingKeys = m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D);
    m_breath += dt * (m_holdBreath ? 0.35f : 1.25f);
    float amp = m_holdBreath ? 0.8f : (movingKeys ? 9.f : 4.5f);
    m_scopeSwayX += (std::sin(m_breath * 0.9f) * amp - m_scopeSwayX) * std::min(1.f, dt * 4.f);
    m_scopeSwayY += (std::sin(m_breath * 1.8f) * amp * 0.6f - m_scopeSwayY) * std::min(1.f, dt * 4.f);

    // Recoil / animation decay
    m_kick *= std::exp(-dt * 9.f);
    m_camKick *= std::exp(-dt * 14.f);
    m_actionT = std::max(0.f, m_actionT - dt * (pl.curWeapon == WPN_SHOTGUN ? 1.4f : 9.f));
    m_flashLight = std::max(0.f, m_flashLight - dt * 16.f);
    float rec = m_recoilPitch * std::min(1.f, dt * 5.f);
    m_pitch -= rec; m_recoilPitch -= rec;
    pl.shakeY *= std::exp(-dt * 18.f); if (std::fabs(pl.shakeY) < 0.2f) pl.shakeY = 0;

    m_hitMarker      = std::max(0.f, m_hitMarker - dt);
    m_headMarker     = std::max(0.f, m_headMarker - dt);
    m_killMarker     = std::max(0.f, m_killMarker - dt);
    m_headshotBanner = std::max(0.f, m_headshotBanner - dt);
    m_secretBanner   = std::max(0.f, m_secretBanner - dt);
    m_acidFlash      = std::max(0.f, m_acidFlash - dt * 2.f);
    m_damageDirTimer = std::max(0.f, m_damageDirTimer - dt);
    m_comboDisplay   = std::max(0.f, m_comboDisplay - dt);
    if (m_comboTimer > 0) { m_comboTimer -= dt; if (m_comboTimer <= 0) m_combo = 0; }
    for (auto &k : m_killFeed) k.life -= dt;
    m_killFeed.erase(std::remove_if(m_killFeed.begin(), m_killFeed.end(), [](const KFEntry &k) { return k.life <= 0; }), m_killFeed.end());
    for (int i = 0; i < 12; i++) if (m_enemyHPDisplay[i] > 0) m_enemyHPDisplay[i] -= dt;

    // Ejected casings (screen space, with gravity and spin)
    for (auto &c : m_casings) {
        c.vy += 1500.f * dt; c.x += c.vx * dt; c.y += c.vy * dt; c.a += c.va * dt; c.life -= dt;
    }
    m_casings.erase(std::remove_if(m_casings.begin(), m_casings.end(), [](const Casing &c) { return c.life <= 0 || c.y > H + 20; }), m_casings.end());

    // What is under the crosshair? (drives crosshair colour + target panel)
    m_probeEnemy = -1; m_probePart = 0;
    if (playing && !m_gs.gameOver) {
        computeView();
        float ax, ay; aimPoint(ax, ay);
        HitResult h = hitscan(ax, ay);
        m_probeEnemy = h.enemy; m_probePart = h.part;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game logic
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::pushFeed(const QString &t, float life, bool head)
{
    m_killFeed.insert(m_killFeed.begin(), KFEntry{t, life, head});
    if (int(m_killFeed.size()) > 5) m_killFeed.pop_back();
}

void MainWindow::gameStep(float dt)
{
    Player &pl = m_gs.player;
    m_gs.frameNum++;
    if (m_gs.levelComplete) { m_gs.levelTransTimer -= dt; if (m_gs.levelTransTimer <= 0) loadLevel(m_gs.level + 1); return; }
    if (pl.attackCooldown > 0) pl.attackCooldown -= dt;
    if (pl.flashTimer > 0)     pl.flashTimer -= dt;
    if (pl.damageFlash > 0)    pl.damageFlash -= dt;

    // Reload / weapon switch timers
    if (pl.reloadTimer > 0) {
        pl.reloadTimer -= dt;
        if (pl.reloadTimer <= 0) {
            int w = pl.curWeapon;
            int take = std::min(WEAPONS[w].magSize - pl.mag[w], pl.reserve[w]);
            pl.mag[w] += take; pl.reserve[w] -= take; pl.reloadTimer = 0;
        }
    }
    if (pl.switchTimer > 0) {
        float before = pl.switchTimer;
        pl.switchTimer -= dt;
        if (before > 0.2f && pl.switchTimer <= 0.2f && m_pendingWeapon >= 0) { pl.curWeapon = m_pendingWeapon; m_pendingWeapon = -1; }
        if (pl.switchTimer < 0) pl.switchTimer = 0;
    }

    updateDoors(dt);

    // Keyboard turning
    float rotSpd = 2.2f * dt * (pl.adsMode ? 0.5f : 1.f);
    if (m_keys.count(Qt::Key_Left))  turn(-rotSpd);
    if (m_keys.count(Qt::Key_Right)) turn(rotSpd);

    // Sprint, stamina, breath-holding
    bool moving = m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D);
    bool shift = m_keys.count(Qt::Key_Shift) > 0;
    bool scoped = WEAPONS[pl.curWeapon].scope && m_adsT > 0.85f;
    bool wantSprint = moving && shift && pl.stamina > 0 && !pl.adsMode && pl.reloadTimer <= 0;
    m_holdBreath = scoped && shift && pl.stamina > 4.f;
    pl.sprinting = wantSprint;
    pl.stamina += wantSprint ? -45.f * dt : m_holdBreath ? -22.f * dt : 28.f * dt;
    pl.stamina = std::max(0.f, std::min(CHAR_PROFILES[m_charChoice].stamina, pl.stamina));

    float SPD = pl.speedMult * (pl.sprinting ? 5.8f : pl.adsMode ? 2.0f : 3.5f) * dt;
    float mx = 0, my = 0;
    if (m_keys.count(Qt::Key_W)) { mx += pl.dirX; my += pl.dirY; }
    if (m_keys.count(Qt::Key_S)) { mx -= pl.dirX; my -= pl.dirY; }
    if (m_keys.count(Qt::Key_A)) { mx += pl.dirY; my -= pl.dirX; }
    if (m_keys.count(Qt::Key_D)) { mx -= pl.dirY; my += pl.dirX; }
    float len = std::sqrt(mx * mx + my * my); if (len > 0.001f) { mx /= len; my /= len; }
    float nx = pl.posX + mx * SPD, ny = pl.posY + my * SPD;
    // Ground under the feet may rise at most a step (0.32) unless we're mid-jump above it
    bool canMoveX = canStep(nx, pl.posY, m_footZ, 0.32f);
    bool canMoveY = canStep(pl.posX, ny, m_footZ, 0.32f);

    // If movement directly into a wall is blocked, suppress sliding along the wall surface
    // when motion is perpendicular to the wall (i.e. no intentional motion parallel to the wall face)
    if (!canMoveX && canMoveY && std::fabs(my) <= 0.40f * std::fabs(mx)) canMoveY = false;
    if (!canMoveY && canMoveX && std::fabs(mx) <= 0.40f * std::fabs(my)) canMoveX = false;

    if (canMoveX) pl.posX = nx;
    if (canMoveY) pl.posY = ny;

    // Vertical: gravity, landing, sticking to slopes when walking downhill
    float g = floorH(pl.posX, pl.posY);
    m_velZ -= 20.f * dt;
    m_footZ += m_velZ * dt;
    if (m_onGround && m_velZ <= 0 && m_footZ - g < 0.12f) { m_footZ = g; m_velZ = 0; }
    if (m_footZ <= g) {
        if (!m_onGround && m_velZ < -4.f) pl.shakeY += std::min(10.f, -m_velZ * 1.2f);
        m_footZ = g; m_velZ = 0; m_onGround = true;
    } else if (m_footZ > g + 0.12f) m_onGround = false;

    // Acid burns while standing in it
    int pcx = int(pl.posX), pcy = int(pl.posY);
    if (m_acid[pcy][pcx] && m_footZ <= g + 0.05f) {
        m_acidAcc += 28.f * dt * (1.f - pl.damageReduction * 0.5f);
        m_acidFlash = 1.f;
        if (m_acidAcc >= 1.f) { int d = int(m_acidAcc); m_acidAcc -= d; pl.hp -= d; pl.damageFlash = std::max(pl.damageFlash, 0.2f); }
        if (pl.hp <= 0) { pl.hp = 0; m_gs.gameOver = true; pl.adsMode = false; }
    }
    if (moving) pl.bobPhase += dt * (pl.sprinting ? 9.f : 5.5f); else pl.bobPhase *= 0.9f;

    // Automatic fire while the trigger is held
    if (m_lmb && WEAPONS[pl.curWeapon].automatic) tryFire();

    // Health kits
    for (auto &k : m_gs.healthKits) {
        if (!k.active) continue;
        k.bobPhase += dt * 2.2f;
        float dx = pl.posX - k.x, dy = pl.posY - k.y;
        int maxHP = CHAR_PROFILES[m_charChoice].hp;
        if (dx * dx + dy * dy < 0.35f && pl.hp < maxHP && std::fabs(m_footZ - floorH(k.x, k.y)) < 0.6f) {
            k.active = false;
            pl.hp = std::min(pl.hp + 30, maxHP);
            pushFeed("+30 HEALTH", 3.f);
        }
    }
    // Weapon / ammo pickups
    for (auto &wp : m_gs.weaponPickups) {
        if (!wp.active) continue;
        wp.bobPhase += dt * 2.8f;
        float dx = pl.posX - wp.x, dy = pl.posY - wp.y;
        if (dx * dx + dy * dy >= 0.35f || std::fabs(m_footZ - floorH(wp.x, wp.y)) > 0.6f) continue;
        wp.active = false;
        if (wp.type == PICKUP_INTEL) {
            for (int w = 0; w < WPN_COUNT; w++) if (pl.owned[w]) pl.reserve[w] = WEAPONS[w].maxReserve;
            pl.hp = CHAR_PROFILES[m_charChoice].hp;
            m_gs.score += 1000;
            pushFeed("INTEL RECOVERED  +1000", 4.5f, true);
        } else if (wp.type == PICKUP_AMMO) {
            for (int w = 0; w < WPN_COUNT; w++) if (pl.owned[w])
                pl.reserve[w] = std::min(pl.reserve[w] + WEAPONS[w].magSize, WEAPONS[w].maxReserve);
            pushFeed("+ AMMO", 3.f);
        } else {
            int w = wp.type == PICKUP_SHOTGUN ? WPN_SHOTGUN : WPN_RIFLE;
            bool had = pl.owned[w];
            pl.owned[w] = true;
            if (!had) pl.mag[w] = WEAPONS[w].magSize;
            pl.reserve[w] = std::min(pl.reserve[w] + WEAPONS[w].magSize * 2, WEAPONS[w].maxReserve);
            pushFeed(QString(had ? "+ AMMO  %1" : "NEW WEAPON  %1").arg(WEAPONS[w].name), 4.f);
            if (!had) switchWeapon(w);
        }
    }

    updateEnemies(dt);

    // Enemy projectiles
    const DiffConfig &dc = DIFF_CFG[m_difficulty];
    for (auto &b : m_gs.bullets) {
        if (!b.alive) continue;
        b.px = b.x; b.py = b.y; b.pz = b.z;
        b.x += b.dx * dt; b.y += b.dy * dt; b.z += b.dz * dt;
        int bx = int(std::floor(b.x)), by = int(std::floor(b.y));
        if (bx < 0 || bx >= MAP_W || by < 0 || by >= MAP_H || b.z < groundAt(b.x, b.y)) {
            b.alive = false; spawnImpact(b.px, b.py, b.pz, !solidCell(bx, by)); continue;
        }
        float dx = b.x - pl.posX, dy = b.y - pl.posY;
        if (dx * dx + dy * dy < 0.15f && b.z > m_footZ - 0.1f && b.z < m_footZ + 0.95f) {
            b.alive = false;
            int dmg = int((10 + 4 * (m_gs.level - 1)) * (1.f - pl.damageReduction) * dc.dmgReceivedMult);
            pl.hp -= dmg; pl.damageFlash = 0.45f; pl.shakeY = -9.f;
            m_damageAngle = std::atan2(-b.dy, -b.dx); m_damageDirTimer = 2.0f;
            if (pl.hp <= 0) { pl.hp = 0; m_gs.gameOver = true; pl.adsMode = false; }
        }
    }
    m_gs.bullets.erase(std::remove_if(m_gs.bullets.begin(), m_gs.bullets.end(), [](const Bullet &b) { return !b.alive; }), m_gs.bullets.end());

    // Tracers
    for (auto &t : m_gs.tracers) t.t += 95.f * dt;
    m_gs.tracers.erase(std::remove_if(m_gs.tracers.begin(), m_gs.tracers.end(), [](const Tracer &t) { return t.t - 2.f > t.len; }), m_gs.tracers.end());

    // Particles
    for (auto &pt : m_gs.particles) {
        if (pt.life <= 0) continue;
        float grav = pt.type == PT_DUST ? -0.15f : pt.type == PT_SMOKE ? -0.35f : pt.type == PT_SPARK ? 11.f : 9.f;
        pt.vz -= grav * dt;
        float drag = (pt.type == PT_DUST || pt.type == PT_SMOKE) ? std::exp(-dt * 3.f) : 1.f;
        pt.vx *= drag; pt.vy *= drag; pt.vz *= drag;
        pt.x += pt.vx * dt; pt.y += pt.vy * dt; pt.z += pt.vz * dt;
        if (pt.type == PT_DUST || pt.type == PT_SMOKE) pt.size += dt * 0.12f;
        float gz = groundAt(pt.x, pt.y);
        if (pt.z < gz) {
            int cx = int(std::floor(pt.x)), cy = int(std::floor(pt.y));
            if (solidCell(cx, cy) && pt.z < gz - 0.05f) {           // hit a wall side: bounce back
                pt.vx = -pt.vx * 0.3f; pt.vy = -pt.vy * 0.3f; pt.x += pt.vx * dt * 2; pt.y += pt.vy * dt * 2;
            } else {
                if (pt.type == PT_BLOOD && pt.vz < -0.5f) addDecal(m_blood, pt.x, pt.y, 0.05f, 90);
                pt.z = gz; pt.vz *= -0.25f; pt.vx *= 0.5f; pt.vy *= 0.5f;
            }
        }
        if (false) { pt.vx = -pt.vx * 0.3f; pt.vy = -pt.vy * 0.3f; pt.x += pt.vx * dt * 2; pt.y += pt.vy * dt * 2; }
        pt.life -= dt;
    }
    m_gs.particles.erase(std::remove_if(m_gs.particles.begin(), m_gs.particles.end(), [](const Particle &p) { return p.life <= 0; }), m_gs.particles.end());

    for (auto &d : m_gs.damageNums) { d.life -= dt; d.z += dt * 0.5f; }
    m_gs.damageNums.erase(std::remove_if(m_gs.damageNums.begin(), m_gs.damageNums.end(), [](const DamageNum &d) { return d.life <= 0; }), m_gs.damageNums.end());

    if (!m_gs.levelComplete && m_gs.killsThisLevel >= m_gs.killsNeeded) {
        m_gs.levelComplete = true; m_gs.levelTransTimer = 4.f; m_gs.score += 500 * m_gs.level;
    }
}

void MainWindow::updateEnemies(float dt)
{
    Player &pl = m_gs.player;
    const DiffConfig &dc = DIFF_CFG[m_difficulty];
    for (size_t i = 0; i < m_gs.enemies.size(); i++) {
        Enemy &e = m_gs.enemies[i];
        if (!e.alive) {
            e.deathTime += dt;
            if (e.deathTime > 7.f) e.corpseFade = std::max(0.f, e.corpseFade - dt * 0.5f);
            continue;
        }
        if (e.hitFlash > 0) e.hitFlash -= dt;
        if (e.shootAnim > 0) e.shootAnim -= dt;
        e.fireCooldown -= dt;

        float dx = pl.posX - e.x, dy = pl.posY - e.y, d = std::sqrt(dx * dx + dy * dy);
        float ez = floorH(e.x, e.y) + 0.62f;
        float detectRange = e.aggroRange * 1.5f;
        bool los = d < std::max(18.f, detectRange) && hasLOS(e.x, e.y, ez, pl.posX, pl.posY, m_footZ + 0.5f);
        if (los && d < detectRange) e.state = ES_ALERT;

        // Pick a movement vector
        float vx = 0, vy = 0, spd = 0;
        if (e.state == ES_ALERT) {
            float ux = d > 0.01f ? dx / d : 0, uy = d > 0.01f ? dy / d : 0;
            e.strafeTimer -= dt;
            if (e.strafeTimer <= 0) { e.strafeTimer = frand(0.8f, 2.2f); e.moveX = frand() < 0.5f ? -1.f : 1.f; }
            if (!los || d > 5.5f)      { vx = ux; vy = uy; spd = 1.35f; }
            else if (d < 2.2f)         { vx = -ux; vy = -uy; spd = 1.0f; }
            else                       { vx = -uy * e.moveX; vy = ux * e.moveX; spd = 0.9f; }
            if (e.hitFlash > 0) spd *= 0.3f;            // staggered
            if (e.shootAnim > 0) spd = 0;
        } else {
            e.wanderTimer -= dt;
            if (e.wanderTimer <= 0) {
                e.wanderTimer = frand(1.5f, 3.5f);
                if (frand() < 0.45f) { e.moveX = 0; e.moveY = 0; }
                else { float a = frand(0, 6.2831853f); e.moveX = std::cos(a); e.moveY = std::sin(a); }
            }
            vx = e.moveX; vy = e.moveY; spd = (vx != 0 || vy != 0) ? 0.55f : 0.f;
        }
        // Separation from other enemies
        for (size_t j = 0; j < m_gs.enemies.size(); j++) {
            if (j == i || !m_gs.enemies[j].alive) continue;
            float ox = e.x - m_gs.enemies[j].x, oy = e.y - m_gs.enemies[j].y, o2 = ox * ox + oy * oy;
            if (o2 < 0.5f && o2 > 1e-4f) { float o = std::sqrt(o2); vx += ox / o * 0.8f; vy += oy / o * 0.8f; spd = std::max(spd, 0.6f); }
        }
        float vl = std::sqrt(vx * vx + vy * vy);
        float ox = e.x, oy = e.y;
        if (vl > 0.01f && spd > 0) {
            float stepX = vx / vl * spd * dt, stepY = vy / vl * spd * dt;
            float hz = floorH(e.x, e.y);
            auto ok = [&](float x, float y) {
                int cx = int(x), cy = int(y);
                return canStep(x, y, hz, 0.3f, 0.25f) && floorH(x, y) > hz - 0.3f && !m_acid[cy][cx];
            };
            if (ok(e.x + stepX, e.y)) e.x += stepX; else if (e.state == ES_IDLE) e.wanderTimer = 0;
            if (ok(e.x, e.y + stepY)) e.y += stepY; else if (e.state == ES_IDLE) e.wanderTimer = 0;
        }
        float moved = std::sqrt((e.x - ox) * (e.x - ox) + (e.y - oy) * (e.y - oy));
        e.moving = moved > 0.0005f;
        if (e.moving) e.walkPhase += moved * 1.3f;

        // Fire
        if (e.state == ES_ALERT && los && d < e.aggroRange && e.fireCooldown <= 0 && e.hitFlash <= 0) {
            enemyFire(e);
            e.shootAnim = 0.2f;
            e.fireCooldown = m_gs.enemyFireRate * frand(0.7f, 1.3f) * (e.elite ? 0.6f : 1.f);
        }
        (void)dc;
    }
}

int MainWindow::enemyFrame(const Enemy &e) const
{
    if (!e.alive) return EF_DEATH0 + std::min(3, int(e.deathTime / 0.11f));
    if (e.hitFlash > 0.06f) return EF_HIT;
    if (e.shootAnim > 0) return EF_SHOOT;
    if (e.moving) return EF_WALK0 + (int(e.walkPhase * 4.f) & 3);
    return e.state == ES_ALERT ? EF_AIM : EF_WALK0;
}

void MainWindow::tryFire()
{
    Player &pl = m_gs.player;
    if (pl.reloadTimer > 0 || pl.switchTimer > 0 || pl.attackCooldown > 0 || pl.sprinting) return;
    if (pl.mag[pl.curWeapon] <= 0) {
        if (pl.reserve[pl.curWeapon] > 0) startReload();
        else { pl.attackCooldown = 0.3f; pushFeed("OUT OF AMMO", 1.5f); }
        return;
    }
    fireWeapon();
}

void MainWindow::fireWeapon()
{
    Player &pl = m_gs.player;
    const WeaponDef &wd = WEAPONS[pl.curWeapon];
    pl.mag[pl.curWeapon]--;
    pl.attackCooldown = wd.fireDelay;
    pl.flashTimer = pl.curWeapon == WPN_SHOTGUN ? 0.09f : 0.06f;
    m_flashLight = 1.f; m_flashSeed = rand();
    m_kick = std::min(1.8f, m_kick + 0.55f + 0.25f * wd.recoil);
    m_actionT = 1.f;
    m_camKick += 2.5f * wd.recoil;
    float climb = 0.012f * wd.recoil * (m_adsT > 0.5f ? 0.6f : 1.f);
    m_pitch = rs::clampf(m_pitch + climb, -0.75f, 0.75f);
    m_recoilPitch += climb * 0.85f;
    turn(frand(-1.f, 1.f) * 0.004f * wd.recoil);
    m_gs.shotsFired++;

    computeView();
    float ax, ay; aimPoint(ax, ay);
    bool moving = m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D);
    float spread = wd.spread * (moving ? 1.8f : 1.f) * (inAir() ? 2.5f : 1.f);
    spread *= 1.f - 0.8f * m_adsT;
    if (wd.pellets > 1) spread = std::max(spread, wd.spread * 0.6f);
    float spreadPx = spread * W;

    // Tracer origin: the on-screen muzzle unprojected 0.6 units into the world
    const float startD = 0.6f;
    float mcx = 2.f * m_muzzleX / W - 1.f;
    float sX = pl.posX + (pl.dirX + pl.planeX * m_adsFOVMult * mcx) * startD;
    float sY = pl.posY + (pl.dirY + pl.planeY * m_adsFOVMult * mcx) * startD;
    float sZ = m_eye + (m_hor - m_muzzleY) / m_K * startD;
    if (WEAPONS[pl.curWeapon].scope && m_adsT > 0.85f) { sX = pl.posX + pl.dirX * startD; sY = pl.posY + pl.dirY * startD; sZ = m_eye - 0.08f; }

    bool anyHit = false;
    for (int p = 0; p < wd.pellets; p++) {
        float r = spreadPx * std::sqrt(frand()), a = frand(0, 6.2831853f);
        float px = ax + std::cos(a) * r, py = ay + std::sin(a) * r;
        HitResult h = hitscan(px, py);
        float ex = h.x, ey = h.y, ez = h.z;
        if (h.sky) { ex = pl.posX + (ex - pl.posX) * 0.5f; ey = pl.posY + (ey - pl.posY) * 0.5f; ez = sZ + (ez - sZ) * 0.5f; }
        Tracer t; t.x0 = sX; t.y0 = sY; t.z0 = sZ; t.x1 = ex; t.y1 = ey; t.z1 = ez;
        t.len = std::sqrt((ex - sX) * (ex - sX) + (ey - sY) * (ey - sY) + (ez - sZ) * (ez - sZ));
        t.t = 0.f;
        if (p < 3) m_gs.tracers.push_back(t);
        if (h.enemy >= 0) {
            anyHit = true;
            float mult = h.part == PART_HEAD ? wd.headMult : h.part == PART_LIMB ? wd.limbMult : 1.f;
            float falloff = wd.pellets > 1 ? std::max(0.35f, 1.f - h.depth / 12.f) : 1.f;
            int dmg = std::max(1, int(wd.damage * mult * falloff * frand(0.9f, 1.1f) + 0.5f));
            damageEnemy(h.enemy, dmg, h.part, h.x, h.y, h.z);
        } else if (!h.sky) {
            float bx = h.x - (h.x - pl.posX) / std::max(0.1f, h.depth) * 0.04f;
            float by = h.y - (h.y - pl.posY) / std::max(0.1f, h.depth) * 0.04f;
            spawnImpact(bx, by, h.z, h.floor);
            if (h.floor) addDecal(m_scorch, h.x, h.y, 0.03f, 160);
            if (h.wall) stampWallDecal(h.cx, h.cy, h.face, h.u, h.v, wd.pellets > 1);
        }
    }
    if (anyHit) m_gs.shotsHit++;

    // Brass
    Casing c; c.x = m_ejectX; c.y = m_ejectY; c.vx = frand(260, 380); c.vy = frand(-420, -300);
    c.a = frand(0, 6.28f); c.va = frand(14, 24); c.life = 1.0f; c.shell = pl.curWeapon == WPN_SHOTGUN;
    if (!(WEAPONS[pl.curWeapon].scope && m_adsT > 0.85f)) m_casings.push_back(c);

    if (pl.mag[pl.curWeapon] == 0 && pl.reserve[pl.curWeapon] > 0) startReload();
}

void MainWindow::damageEnemy(int idx, int dmg, HitPart part, float hx, float hy, float hz)
{
    Enemy &e = m_gs.enemies[idx];
    if (!e.alive) return;
    bool head = part == PART_HEAD;
    e.hp -= dmg; e.hitFlash = 0.16f; e.state = ES_ALERT;
    m_hitMarker = 0.25f;
    if (head) m_headMarker = 0.35f;
    if (idx < 12) m_enemyHPDisplay[idx] = 2.2f;
    m_gs.damageNums.push_back(DamageNum{hx, hy, hz + 0.05f, dmg, head, 0.9f});
    spawnBlood(hx, hy, hz, head ? 22 : 9, head ? 1.7f : 1.f);

    if (e.hp <= 0) {
        e.hp = 0; e.alive = false; e.state = ES_DEAD; e.deathTime = 0; e.killedByHeadshot = head;
        m_gs.kills++; m_gs.killsThisLevel++;
        m_gs.score += 100 * m_gs.level + (head ? 50 * m_gs.level : 0);
        if (head) { m_gs.headshots++; m_headshotBanner = 1.3f; }
        m_killMarker = 0.5f;
        m_combo++; m_comboTimer = 3.5f; m_comboDisplay = 1.8f;
        pushFeed((m_combo > 1 ? QString("x%1  ").arg(m_combo) : QString()) + (head ? "HEADSHOT KILL" : "ENEMY DOWN"), 4.5f, head);
        addDecal(m_blood, e.x, e.y, 0.42f, 170);
        if (rand() % 3 == 0) spawnPickup(e.x, e.y);
    }
}

void MainWindow::startReload()
{
    Player &pl = m_gs.player;
    int w = pl.curWeapon;
    if (pl.reloadTimer > 0 || pl.switchTimer > 0 || pl.mag[w] >= WEAPONS[w].magSize || pl.reserve[w] <= 0) return;
    float t = WEAPONS[w].reloadTime;
    if (w == WPN_SHOTGUN) t = 0.5f + 0.32f * std::min(WEAPONS[w].magSize - pl.mag[w], pl.reserve[w]);
    pl.reloadTimer = t;
    m_reloadTotal = t;
    pl.adsMode = false;
}

void MainWindow::switchWeapon(int w)
{
    Player &pl = m_gs.player;
    if (w < 0 || w >= WPN_COUNT || !pl.owned[w] || (w == pl.curWeapon && m_pendingWeapon < 0)) return;
    pl.reloadTimer = 0;
    pl.switchTimer = 0.4f;
    m_pendingWeapon = w;
}

void MainWindow::enemyFire(Enemy &e)
{
    const Player &pl = m_gs.player;
    float dx = pl.posX - e.x, dy = pl.posY - e.y, d = std::sqrt(dx * dx + dy * dy);
    if (d < 0.1f) return;
    float acc = DIFF_CFG[m_difficulty].enemyAccuracy;
    float a = std::atan2(dy, dx) + frand(-1.f, 1.f) * (1.f - acc) * 0.22f;
    if (e.elite) acc = std::min(0.97f, acc + 0.1f);
    float z0 = floorH(e.x, e.y) + 0.62f, z1 = m_footZ + 0.45f;
    float spd = m_gs.enemyBulletSpeed * (e.elite ? 1.25f : 1.f);
    Bullet b; b.x = e.x + std::cos(a) * 0.3f; b.y = e.y + std::sin(a) * 0.3f; b.z = z0;
    b.px = b.x; b.py = b.y; b.pz = b.z;
    b.dx = std::cos(a) * spd; b.dy = std::sin(a) * spd;
    b.dz = (z1 - z0) / (d / spd) + frand(-1.f, 1.f) * (1.f - acc) * 0.6f;
    b.owner = ENEMY_SHOT; b.alive = true;
    m_gs.bullets.push_back(b);
}

void MainWindow::spawnBlood(float wx, float wy, float wz, int count, float force)
{
    const Player &pl = m_gs.player;
    float dx = wx - pl.posX, dy = wy - pl.posY, d = std::max(0.1f, std::sqrt(dx * dx + dy * dy));
    dx /= d; dy /= d;
    for (int i = 0; i < count; i++) {
        Particle p;
        p.type = PT_BLOOD;
        p.x = wx; p.y = wy; p.z = wz;
        p.vx = dx * frand(0.4f, 1.8f) * force + frand(-0.8f, 0.8f);
        p.vy = dy * frand(0.4f, 1.8f) * force + frand(-0.8f, 0.8f);
        p.vz = frand(-0.2f, 1.6f) * force;
        p.life = p.maxLife = frand(0.5f, 1.1f);
        p.size = frand(0.012f, 0.03f);
        int r = 120 + rand() % 70;
        p.color = rs::rgb(r, rand() % 12, rand() % 14);
        m_gs.particles.push_back(p);
    }
    // a short-lived mist puff
    Particle m; m.type = PT_SMOKE; m.x = wx; m.y = wy; m.z = wz; m.vx = dx * 0.3f; m.vy = dy * 0.3f; m.vz = 0;
    m.life = m.maxLife = 0.3f; m.size = 0.07f * force; m.color = rs::rgb(140, 12, 10);
    m_gs.particles.push_back(m);
}

void MainWindow::spawnImpact(float wx, float wy, float wz, bool floor)
{
    for (int i = 0; i < 7; i++) {
        Particle p; p.type = PT_SPARK; p.x = wx; p.y = wy; p.z = std::max(0.01f, wz);
        p.vx = frand(-2.5f, 2.5f); p.vy = frand(-2.5f, 2.5f); p.vz = frand(0.5f, 3.f);
        p.life = p.maxLife = frand(0.12f, 0.3f); p.size = 0.012f;
        p.color = rs::rgb(255, 200 + rand() % 55, 90 + rand() % 80);
        m_gs.particles.push_back(p);
    }
    for (int i = 0; i < 4; i++) {
        Particle p; p.type = PT_DEBRIS; p.x = wx; p.y = wy; p.z = std::max(0.01f, wz);
        p.vx = frand(-1.2f, 1.2f); p.vy = frand(-1.2f, 1.2f); p.vz = frand(0.5f, 2.f);
        p.life = p.maxLife = frand(0.4f, 0.8f); p.size = 0.015f;
        int v = 50 + rand() % 50; p.color = rs::rgb(v, v - 4, v - 8);
        m_gs.particles.push_back(p);
    }
    for (int i = 0; i < 3; i++) {
        Particle p; p.type = PT_DUST; p.x = wx; p.y = wy; p.z = std::max(0.02f, wz);
        p.vx = frand(-0.4f, 0.4f); p.vy = frand(-0.4f, 0.4f); p.vz = frand(0.05f, 0.4f);
        p.life = p.maxLife = frand(0.6f, 1.1f); p.size = floor ? 0.07f : 0.05f;
        p.color = floor ? rs::rgb(140, 120, 96) : rs::rgb(150, 146, 140);
        m_gs.particles.push_back(p);
    }
}

void MainWindow::spawnPickup(float x, float y)
{
    WeaponPickup wp;
    wp.x = x + (rand() % 10 - 5) * 0.06f;
    wp.y = y + (rand() % 10 - 5) * 0.06f;
    if (!posValid(wp.x, wp.y, 0.1f)) { wp.x = x; wp.y = y; }
    wp.bobPhase = frand(0, 6.28f);
    int r = rand() % 10;
    wp.type = r < 5 ? PICKUP_AMMO : r < 8 ? PICKUP_SHOTGUN : PICKUP_RIFLE;
    m_gs.weaponPickups.push_back(wp);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Camera helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::turn(float a)
{
    Player &pl = m_gs.player;
    float c = std::cos(a), s = std::sin(a);
    float dx = pl.dirX, px = pl.planeX;
    pl.dirX = dx * c - pl.dirY * s;   pl.dirY = dx * s + pl.dirY * c;
    pl.planeX = px * c - pl.planeY * s; pl.planeY = px * s + pl.planeY * c;
}

void MainWindow::computeView()
{
    const Player &pl = m_gs.player;
    m_K = float(H) / m_adsFOVMult;
    bool moving = m_screen == SCR_PLAYING && (m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D));
    float bobZ = moving ? std::sin(pl.bobPhase * 2.f) * (pl.sprinting ? 0.018f : 0.01f) * (1.f - m_adsT * 0.8f) : 0.f;
    m_eye = m_footZ + 0.5f + bobZ;
    m_hor = int(H / 2.f + m_pitch * m_K + pl.shakeY + m_camKick);
}

bool MainWindow::project(float wx, float wy, float wz, float &sx, float &sy, float &depth) const
{
    const Player &pl = m_gs.player;
    float eplX = pl.planeX * m_adsFOVMult, eplY = pl.planeY * m_adsFOVMult;
    float invDet = 1.f / (eplX * pl.dirY - pl.dirX * eplY);
    float spX = wx - pl.posX, spY = wy - pl.posY;
    float txX = invDet * (pl.dirY * spX - pl.dirX * spY), txY = invDet * (-eplY * spX + eplX * spY);
    depth = txY;
    if (txY < 0.05f) return false;
    sx = W / 2.f * (1.f + txX / txY);
    sy = m_hor + (m_eye - wz) * m_K / txY;
    return true;
}

void MainWindow::aimPoint(float &ax, float &ay) const
{
    if (WEAPONS[m_gs.player.curWeapon].scope && m_adsT > 0.85f) { ax = W / 2.f + m_scopeSwayX; ay = H / 2.f + m_scopeSwayY; }
    else { ax = m_aimX; ay = m_aimY; }
}

// Free aim: the crosshair moves inside a dead-zone box; pushing past its edge
// turns the camera (yaw) or tilts it (pitch). With free aim off — or while
// aiming down sights — mouse motion drives the camera directly.
void MainWindow::applyAimDelta(float dx, float dy)
{
    float sens = std::max(0.25f, m_adsFOVMult);
    dx *= sens; dy *= sens;
    m_mouseDX += dx; m_mouseDY += dy;
    auto yaw = [&](float px) { turn(std::atan(2.f * px / W * 0.66f * m_adsFOVMult)); };
    if (!m_freeAim || m_gs.player.adsMode) {
        yaw(dx);
        m_pitch = rs::clampf(m_pitch - dy / m_K, -0.75f, 0.75f);
        return;
    }
    const float DZX = 170.f, DZY = 120.f;
    m_aimX += dx; m_aimY += dy;
    float ex = m_aimX - W / 2.f;
    if (ex > DZX)  { yaw(ex - DZX); m_aimX = W / 2.f + DZX; }
    if (ex < -DZX) { yaw(ex + DZX); m_aimX = W / 2.f - DZX; }
    float ey = m_aimY - H / 2.f;
    if (std::fabs(ey) > DZY) {
        float over = ey > 0 ? ey - DZY : ey + DZY;
        float np = rs::clampf(m_pitch - over / m_K, -0.75f, 0.75f);
        float used = (m_pitch - np) * m_K;           // pixels absorbed by pitch
        m_pitch = np;
        m_aimY -= used;
        m_aimY = rs::clampf(m_aimY, 24.f, H - 24.f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::menuConfirm()
{
    switch (m_screen) {
    case SCR_MENU:        if (m_menuCursor == 0) m_screen = SCR_MAP_SELECT; else QApplication::quit(); break;
    case SCR_MAP_SELECT:  m_screen = SCR_DIFF_SELECT; break;
    case SCR_DIFF_SELECT: m_screen = SCR_CHAR_SELECT; break;
    case SCR_CHAR_SELECT: initGame(); m_screen = SCR_PLAYING; break;
    default: break;
    }
}
void MainWindow::menuBack()
{
    switch (m_screen) {
    case SCR_MENU:        QApplication::quit(); break;
    case SCR_MAP_SELECT:  m_screen = SCR_MENU; m_menuCursor = 0; break;
    case SCR_DIFF_SELECT: m_screen = SCR_MAP_SELECT; break;
    case SCR_CHAR_SELECT: m_screen = SCR_DIFF_SELECT; break;
    default: break;
    }
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    int k = e->key();
    m_keys.insert(k);
    if (m_screen != SCR_PLAYING) {
        int *sel = m_screen == SCR_MAP_SELECT ? &m_mapChoice : m_screen == SCR_DIFF_SELECT ? &m_difficulty :
                   m_screen == SCR_CHAR_SELECT ? &m_charChoice : nullptr;
        if (m_screen == SCR_MENU) {
            if (k == Qt::Key_Up)   m_menuCursor = 0;
            if (k == Qt::Key_Down) m_menuCursor = 1;
            if (k == Qt::Key_1) { m_menuCursor = 0; menuConfirm(); return; }
        } else if (sel) {
            if (k == Qt::Key_Left)  *sel = std::max(0, *sel - 1);
            if (k == Qt::Key_Right) *sel = std::min(2, *sel + 1);
            if (k >= Qt::Key_1 && k <= Qt::Key_3) { *sel = k - Qt::Key_1; menuConfirm(); return; }
        }
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) menuConfirm();
        else if (k == Qt::Key_Escape || (k == Qt::Key_Q && m_screen == SCR_MENU)) menuBack();
        return;
    }
    if (e->isAutoRepeat()) return;
    Player &pl = m_gs.player;
    bool live = !m_paused && !m_gs.gameOver;
    if (live) {
        if (k == Qt::Key_Space && m_onGround) { m_velZ = 5.5f; m_onGround = false; }
        if (k == Qt::Key_F) tryFire();
        if (k == Qt::Key_E) tryOpenDoor();
        if (k == Qt::Key_C) { m_pitch = 0.f; m_aimX = W / 2.f; m_aimY = H / 2.f; }
        if (k == Qt::Key_V) { m_freeAim = !m_freeAim; m_aimX = W / 2.f; m_aimY = H / 2.f; pushFeed(m_freeAim ? "FREE AIM: ON" : "FREE AIM: OFF", 2.f); }
        if (k == Qt::Key_Tab) pl.adsMode = !pl.adsMode;
        if (k >= Qt::Key_1 && k <= Qt::Key_3) switchWeapon(k - Qt::Key_1);
        if (k == Qt::Key_R) startReload();
    }
    if (k == Qt::Key_Escape) { if (m_paused) QApplication::quit(); else if (!m_gs.gameOver) { m_paused = true; pl.adsMode = false; m_lmb = false; } }
    if (k == Qt::Key_R) { if (m_paused) m_paused = false; else if (m_gs.gameOver) { initGame(); m_paused = false; } }
    if (k == Qt::Key_Q && (m_paused || m_gs.gameOver)) QApplication::quit();
    if (k == Qt::Key_M && (m_paused || m_gs.gameOver)) { m_screen = SCR_MENU; m_paused = false; m_menuCursor = 0; }
    if (k == Qt::Key_BracketLeft)  m_pixelScale = std::min(m_pixelScale * 2, 8);
    if (k == Qt::Key_BracketRight) m_pixelScale = std::max(m_pixelScale / 2, 1);
}
void MainWindow::keyReleaseEvent(QKeyEvent *e) { if (!e->isAutoRepeat()) m_keys.erase(e->key()); }
void MainWindow::focusOutEvent(QFocusEvent *) { m_keys.clear(); m_lmb = false; }

void MainWindow::mousePressEvent(QMouseEvent *e)
{
    if (m_screen != SCR_PLAYING) {
        if (e->button() == Qt::RightButton) { menuBack(); return; }
        QPointF f = widgetToFrame(e->position());
        for (const HitRect &r : m_menuRects) {
            if (f.x() < r.x || f.y() < r.y || f.x() >= r.x + r.w || f.y() >= r.y + r.h) continue;
            if (r.id == 10) { menuBack(); return; }
            if (m_screen == SCR_MENU) m_menuCursor = r.id;
            if (m_screen == SCR_MAP_SELECT) m_mapChoice = r.id;
            if (m_screen == SCR_DIFF_SELECT) m_difficulty = r.id;
            if (m_screen == SCR_CHAR_SELECT) m_charChoice = r.id;
            menuConfirm();
            return;
        }
        return;
    }
    if (m_paused || m_gs.gameOver) return;
    if (e->button() == Qt::LeftButton) { m_lmb = true; tryFire(); }
    if (e->button() == Qt::RightButton) m_gs.player.adsMode = true;
}
void MainWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) m_lmb = false;
    if (e->button() == Qt::RightButton) m_gs.player.adsMode = false;
}
void MainWindow::mouseMoveEvent(QMouseEvent *e)
{
    m_mouseWidget = e->position();
    if (m_screen != SCR_PLAYING) {
        QPointF f = widgetToFrame(e->position());
        for (const HitRect &r : m_menuRects) {
            if (r.id == 10 || f.x() < r.x || f.y() < r.y || f.x() >= r.x + r.w || f.y() >= r.y + r.h) continue;
            if (m_screen == SCR_MENU) m_menuCursor = r.id;
            if (m_screen == SCR_MAP_SELECT) m_mapChoice = r.id;
            if (m_screen == SCR_DIFF_SELECT) m_difficulty = r.id;
            if (m_screen == SCR_CHAR_SELECT) m_charChoice = r.id;
        }
        return;
    }
    if (m_paused || m_gs.gameOver || !hasFocus()) return;
    QPoint c(width() / 2, height() / 2);
    float dx = float(e->position().x() - c.x()), dy = float(e->position().y() - c.y());
    if (std::fabs(dx) < 0.5f && std::fabs(dy) < 0.5f) return;
    float toFb = float(W) / std::max(1, width());
    applyAimDelta(dx * toFb, dy * toFb);
    QCursor::setPos(mapToGlobal(c));
}
void MainWindow::wheelEvent(QWheelEvent *e)
{
    int steps = e->angleDelta().y() / 120;
    if (!steps) return;
    bool live = m_screen == SCR_PLAYING && !m_paused && !m_gs.gameOver;
    if (live && WEAPONS[m_gs.player.curWeapon].scope && m_adsT > 0.5f) {
        m_scopeMag = rs::clampf(m_scopeMag + steps, 2.f, 8.f);
        return;
    }
    // Raster magnifier: keep the framebuffer pixel under the anchor fixed on screen.
    // Anchor = mouse cursor in menus, crosshair while playing.
    float ax, ay;
    if (live) aimPoint(ax, ay);
    else { ax = float(m_mouseWidget.x()) * W / std::max(1, width()); ay = float(m_mouseWidget.y()) * H / std::max(1, height()); }
    float x0, y0; zoomOrigin(x0, y0);
    float srcX = x0 + ax / m_rasterZoom, srcY = y0 + ay / m_rasterZoom;
    m_rasterZoom = steps > 0 ? std::min(32, m_rasterZoom * 2) : std::max(1, m_rasterZoom / 2);
    m_zoomCX = srcX - ax / m_rasterZoom + W / (2.f * m_rasterZoom);
    m_zoomCY = srcY - ay / m_rasterZoom + H / (2.f * m_rasterZoom);
    update();
}

void MainWindow::zoomOrigin(float &x0, float &y0) const
{
    float vw = float(W) / m_rasterZoom, vh = float(H) / m_rasterZoom;
    x0 = rs::clampf(m_zoomCX - vw / 2, 0.f, W - vw);
    y0 = rs::clampf(m_zoomCY - vh / 2, 0.f, H - vh);
}

QPointF MainWindow::widgetToFrame(const QPointF &p) const
{
    float x0, y0; zoomOrigin(x0, y0);
    float fx = float(p.x()) * W / std::max(1, width()), fy = float(p.y()) * H / std::max(1, height());
    return QPointF(x0 + fx / m_rasterZoom, y0 + fy / m_rasterZoom);
}

// Output stage - pure raster. The framebuffer is upscaled to the window with
// our own nearest-neighbour loop: every framebuffer pixel becomes a solid box.
// With the magnifier at 6+ screen pixels per box, a 1-pixel grid is drawn
// between boxes and the pixel under the cursor/crosshair is outlined with its
// coordinates and colour. QPainter only copies the finished image 1:1.
void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_rasterZoom == 1 && width() == W && height() == H) { p.drawImage(0, 0, m_buf); return; }
    p.drawImage(0, 0, composeDisplay(width(), height()));
}

const QImage &MainWindow::composeDisplay(int ww, int wh)
{
    if (m_display.width() != ww || m_display.height() != wh) m_display = QImage(ww, wh, QImage::Format_ARGB32);
    uint32_t *dst = reinterpret_cast<uint32_t *>(m_display.bits());
    float x0, y0; zoomOrigin(x0, y0);
    const float pxX = float(ww) / W * m_rasterZoom, pxY = float(wh) / H * m_rasterZoom;   // screen px per fb px
    static std::vector<int> srcX, srcY;
    static std::vector<uint8_t> edgeX, edgeY;
    srcX.resize(ww); edgeX.resize(ww); srcY.resize(wh); edgeY.resize(wh);
    for (int x = 0; x < ww; x++) { srcX[x] = rs::clampi(int(x0 + (x + 0.5f) / pxX), 0, W - 1); edgeX[x] = x > 0 && srcX[x] != srcX[x - 1]; }
    for (int y = 0; y < wh; y++) { srcY[y] = rs::clampi(int(y0 + (y + 0.5f) / pxY), 0, H - 1); edgeY[y] = y > 0 && srcY[y] != srcY[y - 1]; }
    const bool grid = std::min(pxX, pxY) >= 6.f;
    for (int y = 0; y < wh; y++) {
        const uint32_t *srow = m_pixels + srcY[y] * W;
        uint32_t *drow = dst + y * ww;
        if (grid && edgeY[y]) { for (int x = 0; x < ww; x++) drow[x] = 0xFF000000 | ((srow[srcX[x]] >> 1) & 0x7F7F7F); continue; }
        for (int x = 0; x < ww; x++) {
            uint32_t c = srow[srcX[x]];
            drow[x] = (grid && edgeX[x]) ? (0xFF000000 | ((c >> 1) & 0x7F7F7F)) : c;
        }
    }
    if (m_rasterZoom > 1) {
        rs::Canvas dc{dst, ww, wh};
        // Probe pixel: under the crosshair while playing, under the mouse otherwise
        float fx, fy;
        if (m_screen == SCR_PLAYING && !m_paused && !m_gs.gameOver) aimPoint(fx, fy);
        else { QPointF f = widgetToFrame(m_mouseWidget); fx = float(f.x()); fy = float(f.y()); }
        int px = rs::clampi(int(fx), 0, W - 1), py = rs::clampi(int(fy), 0, H - 1);
        int bx = int((px - x0) * pxX), by = int((py - y0) * pxY);
        if (pxX >= 4.f) dc.rectOutline(bx - 1, by - 1, int(pxX) + 3, int(pxY) + 3, 0xFFFFD040, 2);
        uint32_t c = m_pixels[py * W + px];
        char buf[200];
        std::snprintf(buf, sizeof(buf), "RASTER ZOOM %dX   1 FRAMEBUFFER PIXEL = %dx%d SCREEN PX   PIXEL (%d,%d) = #%02X%02X%02X",
                      m_rasterZoom, int(pxX + 0.5f), int(pxY + 0.5f), px, py, rs::R(c), rs::G(c), rs::B(c));
        std::string line(buf);
        int tw = pf::width(line, 1) + 52, th = 24;
        dc.blendRect(8, wh - th - 8, tw, th, rs::argb(200, 10, 12, 16));
        dc.fillRect(8, wh - th - 8, 3, th, 0xFFFFB43C);
        dc.fillRect(18, wh - th, 14, 9, 0xFF000000 | c);
        dc.rectOutline(17, wh - th - 1, 16, 11, 0xFFFFFFFF);
        pf::drawLine(dc, 40, wh - th, line, 0xFFFFFFFF, 1);
        pf::drawLine(dc, 8, 8, "SCROLL: ZOOM IN / OUT", 0xC0FFFFFF, 1);
    }
    return m_display;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Snapshot mode (developer aid): fps_game --snapshot <dir>
//  Renders scripted scenes to PNG and writes a small test log.
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::snapshot(const QString &dir)
{
    m_timer.stop();
    QDir().mkpath(dir);
    QFile logf(dir + "/log.txt");
    if (!logf.open(QIODevice::WriteOnly)) return false;
    auto log = [&](const QString &s) { logf.write((s + "\n").toUtf8()); };
    auto run = [&](int frames) { for (int i = 0; i < frames; i++) advance(1.f / 60.f); render(); };
    auto save = [&](const QString &name) { m_buf.save(dir + "/" + name + ".png"); };
    auto place = [&](float x, float y, float ang) {
        Player &pl = m_gs.player;
        pl.posX = x; pl.posY = y;
        pl.dirX = std::cos(ang); pl.dirY = std::sin(ang);
        pl.planeX = -pl.dirY * 0.66f; pl.planeY = pl.dirX * 0.66f;
        m_footZ = floorH(x, y); m_velZ = 0; m_onGround = true;
        m_pitch = 0; m_aimX = W / 2.f; m_aimY = H / 2.f;
    };
    auto start = [&](int map) {
        m_mapChoice = map; m_charChoice = 0; m_difficulty = 1;
        initGame(); m_screen = SCR_PLAYING;
        for (auto &e : m_gs.enemies) e.fireCooldown = 999.f;     // keep the photographer alive
        m_gs.player.hp = 9999;
    };

    m_screen = SCR_MENU; run(30); save("01_menu");
    // Raster magnifier on the menu title: 8x and 32x around the "R" of RASTER
    m_mouseWidget = QPointF(110, 140);
    for (int z = 0; z < 3; z++) { QWheelEvent we(m_mouseWidget, m_mouseWidget, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false); wheelEvent(&we); }
    run(1); composeDisplay(W, H).save(dir + "/05_zoom8_menu.png");
    for (int z = 0; z < 2; z++) { QWheelEvent we(m_mouseWidget, m_mouseWidget, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false); wheelEvent(&we); }
    composeDisplay(W, H).save(dir + "/06_zoom32_menu.png");
    log(QString("menu zoom level: %1").arg(m_rasterZoom));
    m_rasterZoom = 1;
    m_screen = SCR_MAP_SELECT; m_mapChoice = 1; run(10); save("02_maps");
    m_screen = SCR_CHAR_SELECT; run(5); save("04_char");

    // --- Arena: rolling hills + the mesa -------------------------------------------
    start(1);
    place(m_parsed[1].camX, m_parsed[1].camY, std::atan2(18.5f - m_parsed[1].camY, 16.5f - m_parsed[1].camX));
    run(10); save("10_arena_hills");
    place(16.5f, 25.f, -1.5708f); m_pitch = 0.05f; run(10); save("11_arena_mesa");

    // --- Bullet holes: rifle + shotgun into a wall --------------------------------
    {
        Player &pl = m_gs.player;
        place(10.5f, 13.6f, -1.5708f);                          // ~2 units from the brick bunker's south wall
        for (auto &e : m_gs.enemies) { e.alive = false; e.corpseFade = 0; }
        pl.owned[WPN_RIFLE] = true; pl.mag[WPN_RIFLE] = 30; pl.reserve[WPN_RIFLE] = 90;
        pl.curWeapon = WPN_RIFLE; run(2);
        for (int i = 0; i < 9; i++) {
            m_aimX = W / 2.f - 60 + (i % 3) * 60; m_aimY = H / 2.f - 30 + (i / 3) * 30;
            pl.attackCooldown = 0; m_recoilPitch = 0; m_pitch = 0; tryFire(); run(6);
        }
        pl.owned[WPN_SHOTGUN] = true; pl.mag[WPN_SHOTGUN] = 6; pl.curWeapon = WPN_SHOTGUN;
        m_aimX = W / 2.f + 120; m_aimY = H / 2.f; pl.attackCooldown = 0; m_pitch = 0; tryFire();
        run(30); m_aimX = W / 2.f; m_aimY = H / 2.f; m_pitch = 0; run(1);
        save("12_bullet_holes");
        log(QString("wall decal faces: %1").arg(m_wallDecals.size()));
    }

    // --- Ledge test: a 0.5 ledge blocks walking, jumping gets you up --------------
    {
        // arena mesa: '-' ring at row 21 (0.25), '^' ring at row 20 (0.5)
        place(16.5f, 22.5f, -1.5708f);
        m_keys.insert(Qt::Key_W);
        for (int i = 0; i < 90; i++) advance(1.f / 60.f);
        float yWalk = m_gs.player.posY, zWalk = m_footZ;
        m_keys.clear();
        place(16.5f, 20.5f, -1.5708f);                          // standing on the 0.5 ledge ring
        m_velZ = 5.5f; m_onGround = false; m_keys.insert(Qt::Key_W);
        for (int i = 0; i < 40; i++) advance(1.f / 60.f);
        m_keys.clear();
        for (int i = 0; i < 30; i++) advance(1.f / 60.f);
        log(QString("walk up the steps: stopped at y=%1 z=%2 (high ledge starts at y<20)").arg(yWalk).arg(zWalk));
        log(QString("jump from ledge: y=%1 z=%2 plat=%3 (1.0 = on the high ledge)").arg(m_gs.player.posY).arg(m_footZ).arg(m_plat[int(m_gs.player.posY)][16]));
        place(13.5f, 34.5f, 3.1416f);                           // arena: walk west into the pit (sunken -0.6)
        m_keys.insert(Qt::Key_W); for (int i = 0; i < 120; i++) advance(1.f / 60.f); m_keys.clear();
        float pitX = m_gs.player.posX;
        m_keys.insert(Qt::Key_D); for (int i = 0; i < 60; i++) advance(1.f / 60.f); m_keys.clear();   // try to walk out north
        log(QString("pit: fell to z=%1 at x=%2; walking out without jumping -> y=%3 (pit spans y 33..36)").arg(m_footZ).arg(pitX).arg(m_gs.player.posY));
    }

    // --- Dungeon: secret wall opens onto a guarded vault ------------------------------
    start(0);
    place(15.5f, 6.5f, 0.f); run(5); save("20_dungeon_secret_wall");
    tryOpenDoor(); run(70); save("21_dungeon_vault_open");
    log(QString("secrets found: %1 / %2, cell(16,6)=%3").arg(m_gs.secretsFound).arg(m_gs.secretsTotal).arg(m_map[6][16]));
    // Door into the acid vault
    place(20.5f, 17.3f, 1.5708f); run(3); save("22_dungeon_door");
    tryOpenDoor(); run(80);
    place(20.5f, 18.4f, 1.5708f); m_pitch = -0.15f; run(10); save("23_dungeon_acid_vault");

    // --- Crypt: altar ledge and dusk shadows -----------------------------------------
    start(2);
    place(m_parsed[2].camX, m_parsed[2].camY, 1.2f); run(10); save("30_crypt");
    place(7.5f, 26.5f, -0.6f); run(10); save("31_crypt_altar");

    // --- Combat frame on hills + perf ------------------------------------------------
    start(1);
    place(28.5f, 6.5f, 3.1416f);
    m_gs.enemies[0].x = 25.0f; m_gs.enemies[0].y = 6.3f; m_gs.enemies[0].alive = true; m_gs.enemies[0].state = ES_ALERT;
    run(5);
    {
        float sx, sy, d; computeView();
        if (project(m_gs.enemies[0].x, m_gs.enemies[0].y, floorH(m_gs.enemies[0].x, m_gs.enemies[0].y) + 0.86f, sx, sy, d)) { m_aimX = sx; m_aimY = sy; }
        run(1); save("40_aim_head");
        for (int z = 0; z < 4; z++) { QWheelEvent we(QPointF(400, 300), QPointF(400, 300), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false); wheelEvent(&we); }
        composeDisplay(W, H).save(dir + "/42_zoom16_head.png");
        m_rasterZoom = 1;
        HitResult h = hitscan(m_aimX, m_aimY);
        log(QString("probe at head: enemy=%1 part=%2").arg(h.enemy).arg(int(h.part)));
        tryFire(); for (int i = 0; i < 3; i++) advance(1.f / 60.f); render(); save("41_fire");
    }
    QElapsedTimer tm; tm.start();
    for (int i = 0; i < 120; i++) { advance(1.f / 60.f); render(); }
    log(QString("%1 ms/frame (arena)").arg(tm.elapsed() / 120.0));
    {
        auto timeIt = [&](const char *name, auto fn) {
            QElapsedTimer t2; t2.start();
            for (int i = 0; i < 60; i++) fn();
            log(QString("  %1: %2 ms").arg(name).arg(t2.nsecsElapsed() / 60.0 / 1e6, 0, 'f', 2));
        };
        computeView();
        timeIt("terrain", [&] { renderTerrain(); });
        timeIt("sky", [&] { renderSky(); });
        timeIt("sprites", [&] { renderSprites(); });
        timeIt("viewmodel", [&] { renderViewModel(); });
        timeIt("postfx", [&] { renderPostFX(); });
        timeIt("hud", [&] { renderHUD(); flushText(); });
        timeIt("advance", [&] { advance(1.f / 60.f); });
    }
    start(0); place(5.5f, 30.5f, 0.f);
    tm.restart();
    for (int i = 0; i < 120; i++) { advance(1.f / 60.f); render(); }
    log(QString("%1 ms/frame (dungeon)").arg(tm.elapsed() / 120.0));
    m_gs.player.hp = 0; m_gs.gameOver = true; run(2); save("50_gameover");
    return true;
}
