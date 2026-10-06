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
    bakeEnemyAtlas(m_scoutAtlas, m_scoutMask, SCOUT_LOOK);
    bakeEnemyAtlas(m_shotgunAtlas, m_shotgunMask, SHOTGUN_LOOK);
    bakeEnemyAtlas(m_sniperAtlas, m_sniperMask, SNIPER_LOOK);
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
    pl.owned[WPN_KNIFE] = true;
    pl.owned[WPN_PISTOL] = true;
    for (int w = 0; w < WPN_COUNT; w++) {
        pl.upgradeExtMag[w] = false;
        pl.upgradeCompensator[w] = false;
        pl.upgradeAmmo[w] = false;
        pl.upgradeSpecial[w] = false;
    }
    m_upgradeCards.clear();
    m_selectedUpgradeCard = -1;
    m_upgradePicked = false;
    m_showArmory = false;
    pl.owned[WPN_KNIFE] = true;
    pl.owned[WPN_PISTOL] = true;
    pl.owned[WPN_SHOTGUN] = false;
    pl.owned[WPN_RIFLE] = false;
    pl.curWeapon = WPN_PISTOL;
    pl.mag[WPN_KNIFE] = 0;
    pl.reserve[WPN_KNIFE] = 0;
    pl.mag[WPN_PISTOL] = effectiveMagSize(WPN_PISTOL);
    pl.reserve[WPN_PISTOL] = cp.ammo * 2;
    m_knifeSwing = 0;

    m_combo = 0; m_comboTimer = 0; m_comboDisplay = 0;
    m_pitch = 0; m_recoilPitch = 0; m_aimX = W / 2.f; m_aimY = H / 2.f; m_adsT = 0; m_adsFOVMult = 1;
    m_hitMarker = m_headMarker = m_killMarker = m_headshotBanner = 0; m_damageDirTimer = 0;
    
    pl.velX = 0.f;
    pl.velY = 0.f;
    m_velZ = 0;
    m_onGround = true;
    m_acidAcc = 0;
    m_kick = m_actionT = 0;
    m_pendingWeapon = -1;
    
    m_killFeed.clear(); m_casings.clear();
    m_emergencyAmmoCooldown = 0.f; m_emergencyHealthCooldown = 0.f;
    std::memset(m_enemyHPDisplay, 0, sizeof(m_enemyHPDisplay));
    loadLevel(1);
    m_paused = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Weapon Upgrade / Attachment Helpers
// ─────────────────────────────────────────────────────────────────────────────
int MainWindow::effectiveMagSize(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0;
    const Player &pl = m_gs.player;
    int base = WEAPONS[w].magSize;
    if (w == WPN_KNIFE) return 0;
    if (pl.upgradeExtMag[w]) {
        if (w == WPN_PISTOL)  return 20; // 12 -> 20 (+66%)
        if (w == WPN_SHOTGUN) return 9;  // 6 -> 9 (+50%)
        if (w == WPN_RIFLE)   return 45; // 30 -> 45 (+50%)
    }
    return base;
}

int MainWindow::effectiveDamage(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0;
    const Player &pl = m_gs.player;
    int base = WEAPONS[w].damage;
    if (pl.upgradeAmmo[w]) {
        if (w == WPN_KNIFE)   return 80; // 55 -> 80 (+45%)
        if (w == WPN_PISTOL)  return 40; // 28 -> 40 (+42%)
        if (w == WPN_SHOTGUN) return 34; // 24 -> 34 (+42% per pellet)
        if (w == WPN_RIFLE)   return 32; // 22 -> 32 (+45%)
    }
    return base;
}

float MainWindow::effectiveRecoil(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 1.f;
    const Player &pl = m_gs.player;
    float base = WEAPONS[w].recoil;
    if (pl.upgradeCompensator[w]) return base * 0.52f; // -48% recoil kick
    return base;
}

float MainWindow::effectiveSpread(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0.02f;
    const Player &pl = m_gs.player;
    float base = WEAPONS[w].spread;
    if (pl.upgradeCompensator[w]) return base * 0.58f; // -42% spread
    return base;
}

float MainWindow::effectiveFireDelay(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0.2f;
    const Player &pl = m_gs.player;
    float base = WEAPONS[w].fireDelay;
    if (w == WPN_SHOTGUN && pl.upgradeSpecial[w]) return 0.40f; // Rapid pump
    if (w == WPN_PISTOL && pl.upgradeSpecial[w])  return 0.15f; // Match hair trigger
    if (w == WPN_KNIFE && pl.upgradeCompensator[w]) return 0.22f; // Lightweight flurry
    return base;
}

bool MainWindow::effectiveScope(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return false;
    const Player &pl = m_gs.player;
    // M4 Carbine starts with reflex sight; unlocks high-magnification scope with ACOG upgrade!
    if (w == WPN_RIFLE) return pl.upgradeSpecial[w];
    return WEAPONS[w].scope;
}

float MainWindow::effectiveAdsFov(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 1.f;
    const Player &pl = m_gs.player;
    if (w == WPN_RIFLE) return pl.upgradeSpecial[w] ? 0.25f : 0.65f;
    return WEAPONS[w].adsFov;
}

void MainWindow::applyUpgrade(int weapon, int slot)
{
    Player &pl = m_gs.player;
    if (weapon >= 0 && weapon < WPN_COUNT) {
        if (slot == 0) {
            pl.upgradeExtMag[weapon] = true;
            pl.mag[weapon] = effectiveMagSize(weapon);
        } else if (slot == 1) {
            pl.upgradeCompensator[weapon] = true;
        } else if (slot == 2) {
            pl.upgradeAmmo[weapon] = true;
        } else if (slot == 3) {
            pl.upgradeSpecial[weapon] = true;
        }
    } else {
        pl.hp = std::min(CHAR_PROFILES[m_charChoice].hp, pl.hp + 50);
        for (int w = WPN_PISTOL; w < WPN_COUNT; w++) {
            if (pl.owned[w]) pl.reserve[w] = WEAPONS[w].maxReserve;
        }
    }
}

void MainWindow::generateUpgradeChoices()
{
    m_upgradeCards.clear();
    m_selectedUpgradeCard = -1;
    m_upgradePicked = false;
    Player &pl = m_gs.player;

    struct Possible { int weapon, slot; QString title, desc, bonus; };
    std::vector<Possible> pool;

    // Rifle
    if (!pl.upgradeSpecial[WPN_RIFLE])
        pool.push_back({WPN_RIFLE, 3, "M4: ACOG 3X OPTIC", "Equips precision magnified 3x optic. Hold RMB to engage sniper scope.", "3.0x Magnification & Mil-Dot Reticle"});
    if (!pl.upgradeExtMag[WPN_RIFLE])
        pool.push_back({WPN_RIFLE, 0, "M4: EXTENDED 45-RND MAG", "High-capacity magazine for sustained fire superiority.", "Magazine: 30 -> 45 Rounds (+50%)"});
    if (!pl.upgradeCompensator[WPN_RIFLE])
        pool.push_back({WPN_RIFLE, 1, "M4: QUAD COMPENSATOR", "Vented muzzle brake reduces vertical kick and spread.", "-48% Recoil & -42% Spread"});
    if (!pl.upgradeAmmo[WPN_RIFLE])
        pool.push_back({WPN_RIFLE, 2, "M4: 5.56 AP MATCH AMMO", "High-penetration tungsten-core rounds counter enemy armor.", "+45% Bullet Damage (22 -> 32)"});

    // Shotgun
    if (!pl.upgradeCompensator[WPN_SHOTGUN])
        pool.push_back({WPN_SHOTGUN, 1, "M870: COMPETITION CHOKE", "Constricts pellet spread for devastating mid-range precision.", "-45% Pellet Spread & Tight Grouping"});
    if (!pl.upgradeExtMag[WPN_SHOTGUN])
        pool.push_back({WPN_SHOTGUN, 0, "M870: EXTENDED TUBE", "Extended underbarrel tube allows continuous shotgun fire.", "Capacity: 6 -> 9 Shells (+50%)"});
    if (!pl.upgradeAmmo[WPN_SHOTGUN])
        pool.push_back({WPN_SHOTGUN, 2, "M870: MAGNUM 00-BUCK", "High-brass heavy charge inflicts massive trauma.", "+42% Pellet Damage (24 -> 34)"});
    if (!pl.upgradeSpecial[WPN_SHOTGUN])
        pool.push_back({WPN_SHOTGUN, 3, "M870: RAPID CYCLE SLIDE", "Polished receiver rails and lightened pump action slide.", "+45% Fire Rate (0.58s -> 0.40s Delay)"});

    // Pistol
    if (!pl.upgradeAmmo[WPN_PISTOL])
        pool.push_back({WPN_PISTOL, 2, "M9: +P HOLLOW POINT", "Overpressured high-expansion sidearm rounds.", "+42% Damage (28 -> 40)"});
    if (!pl.upgradeExtMag[WPN_PISTOL])
        pool.push_back({WPN_PISTOL, 0, "M9: EXTENDED 20-RND MAG", "High-capacity double-stack magazine for sustained defense.", "Magazine: 12 -> 20 Rounds (+66%)"});
    if (!pl.upgradeCompensator[WPN_PISTOL])
        pool.push_back({WPN_PISTOL, 1, "M9: TACTICAL COMPENSATOR", "Threaded muzzle brake stabilizes single-action rapid fire.", "-45% Recoil & -40% Spread"});
    if (!pl.upgradeSpecial[WPN_PISTOL])
        pool.push_back({WPN_PISTOL, 3, "M9: MATCH HAIR TRIGGER", "Competition tuned sear for lightning-fast semi-auto tapping.", "+60% Fire Rate (0.24s -> 0.15s Delay)"});

    // Knife
    if (!pl.upgradeAmmo[WPN_KNIFE])
        pool.push_back({WPN_KNIFE, 2, "KNIFE: SERRATED BLADE", "Razor-sharp teeth slice deep into flesh and armor.", "+45% Melee Damage (55 -> 80)"});
    if (!pl.upgradeCompensator[WPN_KNIFE])
        pool.push_back({WPN_KNIFE, 1, "KNIFE: LIGHTWEIGHT HILT", "Ergonomic balanced hilt allows rapid flurry slashing.", "+60% Attack Speed (0.35s -> 0.22s)"});
    if (!pl.upgradeSpecial[WPN_KNIFE])
        pool.push_back({WPN_KNIFE, 3, "KNIFE: STILETTO TIP", "Reinforced armor-piercing tanto tip for deadly criticals.", "+40% Headshot/Crit Multiplier"});

    for (size_t i = pool.size(); i > 1; i--)
        std::swap(pool[i - 1], pool[size_t(rand()) % i]);

    for (size_t i = 0; i < std::min(size_t(3), pool.size()); i++) {
        m_upgradeCards.push_back(UpgradeCard{pool[i].weapon, pool[i].slot, pool[i].title, pool[i].desc, pool[i].bonus});
    }

    while (m_upgradeCards.size() < 3) {
        m_upgradeCards.push_back(UpgradeCard{-1, 0, "FIELD SUPPLY RESTOCK", "Combat supply drop replenishing all ammo reserves and field health.", "+100% Reserve Ammo & +50 HP"});
    }
}

void MainWindow::startDebugMode()
{
    m_mapChoice = 0; // Dungeon map (has doors, secret walls, acid vault)
    m_charChoice = 0;
    m_difficulty = 1;
    initGame();
    Player &pl = m_gs.player;
    pl.hp = 9999;
    for (int w = 0; w < WPN_COUNT; w++) {
        pl.owned[w] = true;
        pl.upgradeExtMag[w] = true;
        pl.upgradeCompensator[w] = true;
        pl.upgradeAmmo[w] = true;
        pl.upgradeSpecial[w] = true;
        pl.mag[w] = effectiveMagSize(w);
        pl.reserve[w] = WEAPONS[w].maxReserve;
    }
    // Clear normal enemies and spawn one of each archetype in front of player for testing
    m_gs.enemies.clear();
    float px = pl.posX, py = pl.posY;
    spawnEnemy(px + 3.0f, py - 1.0f, false, ET_RIFLEMAN);
    spawnEnemy(px + 4.5f, py + 0.5f, false, ET_SCOUT);
    spawnEnemy(px + 2.5f, py + 2.0f, false, ET_SHOTGUNNER);
    spawnEnemy(px + 6.0f, py - 2.5f, false, ET_SNIPER);
    spawnEnemy(px + 5.0f, py + 2.0f, true,  ET_SHOTGUNNER); // Elite
    // Lower initial aggression cooldown for test dummies
    for (auto &e : m_gs.enemies) { e.fireCooldown = 999.f; e.state = ES_IDLE; }
    m_screen = SCR_PLAYING;
    pushFeed("DEBUG / TEST FIRING RANGE ACTIVE", 5.0f, true);
    pushFeed("ALL WEAPONS & UPGRADES LOADED  -  [U] ARMORY", 5.0f);
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

    m_gs.level = lv; m_gs.killsThisLevel = 0;
    int nEnemies = cfg.numEnemies + (lv > 1 ? (lv - 1) * 3 : 0);
    m_gs.killsNeeded = std::min(nEnemies, 4 + (lv - 1) * 2);

    m_gs.levelComplete = false; m_gs.levelTransTimer = 0;
    m_upgradeCards.clear(); m_selectedUpgradeCard = -1; m_upgradePicked = false; m_showArmory = false;
    m_upgradeDecisionTimer = 30.0f; m_deployTimer = 0.0f;
    m_gs.bullets.clear(); m_gs.tracers.clear(); m_gs.particles.clear(); m_gs.healthKits.clear();
    m_gs.weaponPickups.clear(); m_gs.damageNums.clear();
    m_emergencyAmmoCooldown = 0.f; m_emergencyHealthCooldown = 0.f;
    m_gs.enemyBulletSpeed = cfg.bulletSpeed  * dc.bulletSpeedMult;
    m_gs.enemyFireRate    = cfg.fireCooldown * dc.fireCoolMult;

    Player &pl = m_gs.player;
    if (lv > 1) {
        pl.hp = std::min(pl.hp + 35, CHAR_PROFILES[m_charChoice].hp);
        for (int w = 0; w < WPN_COUNT; w++) {
            if (pl.owned[w] && w != WPN_KNIFE) {
                int addMag = effectiveMagSize(w);
                pl.reserve[w] = std::min(pl.reserve[w] + addMag, WEAPONS[w].maxReserve);
            }
        }
        pl.damageFlash = 0;
    }

    loadMapGeometry(m_gs.mapChoice);

    const ParsedMap &pm = m_parsed[m_gs.mapChoice];
    pl.posX = pm.spawnX;
    pl.posY = pm.spawnY;

    // Do not preserve movement momentum between zones.
    pl.velX = 0.f;
    pl.velY = 0.f;

    m_footZ = floorH(pl.posX, pl.posY);
    m_velZ = 0;
    m_onGround = true;

    pl.dirX = 1.f;
    pl.dirY = 0.f;
    
    pl.planeX = 0.f; pl.planeY = 0.66f;
    pl.shakeY = 0;  pl.adsMode = false;
    pl.reloadTimer = 0; pl.switchTimer = 0;
    m_pitch = 0; m_aimX = W / 2.f; m_aimY = H / 2.f;

    // Enemies: Hybrid Tactical Spawning
    m_gs.enemies.clear();
    std::vector<Marker> spawns, kits;
    for (const Marker &mk : pm.markers) { if (mk.kind == 'e') spawns.push_back(mk); if (mk.kind == 'k') kits.push_back(mk); }
    for (size_t i = spawns.size(); i > 1; i--) std::swap(spawns[i - 1], spawns[size_t(rand()) % i]);
    for (size_t i = kits.size(); i > 1; i--) std::swap(kits[i - 1], kits[size_t(rand()) % i]);

    // Gather valid walkable floor tiles away from player spawn (> 8 units)
    std::vector<std::pair<float, float>> validFloorTiles;
    for (int y = 1; y < MAP_H - 1; y++) {
        for (int x = 1; x < MAP_W - 1; x++) {
            if (m_map[y][x] == CELL_FLOOR && !m_acid[y][x]) {
                float fx = x + 0.5f, fy = y + 0.5f;
                float dsq = (fx - pl.posX) * (fx - pl.posX) + (fy - pl.posY) * (fy - pl.posY);
                if (dsq > 64.f && posValid(fx, fy, 0.28f)) {
                    validFloorTiles.push_back({fx, fy});
                }
            }
        }
    }
    for (size_t i = validFloorTiles.size(); i > 1; i--)
        std::swap(validFloorTiles[i - 1], validFloorTiles[size_t(rand()) % i]);

    // 1) Fixed tactical choke/ambush markers (up to 50% of spawns)
    int fixedCount = std::min(int(spawns.size()), nEnemies / 2);
    for (int i = 0; i < fixedCount; i++) {
        EnemyType et = ET_RIFLEMAN;
        int roll = rand() % 100;
        if (roll < 35) et = ET_SHOTGUNNER;
        else if (roll < 60) et = ET_RIFLEMAN;
        else if (roll < 80) et = ET_SCOUT;
        else if (lv >= 2)   et = ET_SNIPER;
        spawnEnemy(spawns[i].x, spawns[i].y, false, et);
    }

    // 2) Dynamic tactical positions for remaining enemies
    int dynamicCount = nEnemies - fixedCount;
    int floorIdx = 0;
    for (int i = 0; i < dynamicCount && floorIdx < int(validFloorTiles.size()); i++, floorIdx++) {
        float fx = validFloorTiles[floorIdx].first;
        float fy = validFloorTiles[floorIdx].second;
        bool elevated = (floorH(fx, fy) > 0.2f);
        EnemyType et = ET_RIFLEMAN;
        if (elevated && lv >= 2) {
            et = ET_SNIPER;
        } else {
            int roll = rand() % 100;
            if (roll < 30) et = ET_SCOUT;
            else if (roll < 60) et = ET_SHOTGUNNER;
            else if (roll < 80 && lv >= 2) et = ET_SNIPER;
            else et = ET_RIFLEMAN;
        }
        spawnEnemy(fx, fy, false, et);
    }

    // Vault guards at secret markers 'x':
    int secretIdx = 0;
    for (const Marker &mk : pm.markers) {
        if (mk.kind == 'x') {
            EnemyType vt = (secretIdx % 3 == 0) ? ET_SHOTGUNNER : (secretIdx % 3 == 1) ? ET_RIFLEMAN : ET_SNIPER;
            spawnEnemy(mk.x, mk.y, true, vt);
            if (lv >= 2) {
                float ox = mk.x + (rand() % 2 == 0 ? 0.6f : -0.6f);
                float oy = mk.y + (rand() % 2 == 0 ? 0.6f : -0.6f);
                if (posValid(ox, oy, 0.25f)) spawnEnemy(ox, oy, true, ET_SCOUT);
            }
            secretIdx++;
        }
    }
    m_gs.killsNeeded = std::min(m_gs.killsNeeded, int(m_gs.enemies.size()));
    m_reinforceTimer = frand(14.f, 22.f);

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
    bool canAim = playing && pl.adsMode && pl.reloadTimer <= 0 && pl.switchTimer <= 0 && !pl.sprinting && !m_gs.gameOver;
    m_adsT += ((canAim ? 1.f : 0.f) - m_adsT) * std::min(1.f, dt * 12.f);
    float adsFov = effectiveScope(pl.curWeapon) ? 1.f / m_scopeMag : effectiveAdsFov(pl.curWeapon);
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
    m_actionT = std::max(0.f, m_actionT - dt * (pl.curWeapon == WPN_SHOTGUN ? 1.4f : (pl.curWeapon == WPN_KNIFE ? 4.8f : 9.f)));
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
    m_nearSecret = false; m_nearDoor = false;
    if (playing && !m_gs.gameOver) {
        computeView();
        float ax, ay; aimPoint(ax, ay);
        HitResult h = hitscan(ax, ay);
        m_probeEnemy = h.enemy; m_probePart = h.part;

        // Proximity detection for secret walls and doors in front of the player
        int cand[3][2] = {
            {int(pl.posX + pl.dirX * 0.7f), int(pl.posY + pl.dirY * 0.7f)},
            {int(pl.posX + pl.dirX * 1.2f), int(pl.posY + pl.dirY * 1.2f)},
            {int(pl.posX + pl.dirX * 1.6f), int(pl.posY + pl.dirY * 1.6f)}
        };
        for (auto &c : cand) {
            int nx = c[0], ny = c[1];
            if (nx > 0 && ny > 0 && nx < MAP_W - 1 && ny < MAP_H - 1) {
                int t = m_map[ny][nx];
                if (t == CELL_SECRET && !m_doorMoving[ny][nx] && m_doorT[ny][nx] < 0.9f) { m_nearSecret = true; break; }
                if (t == CELL_DOOR && !m_doorMoving[ny][nx] && m_doorT[ny][nx] < 0.9f) { m_nearDoor = true; break; }
            }
        }
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
    if (m_gs.levelComplete) {
        if (!m_upgradePicked) {
            m_upgradeDecisionTimer -= dt;
            if (m_upgradeDecisionTimer <= 0.0f) {
                if (!m_upgradeCards.empty()) {
                    applyUpgrade(m_upgradeCards[0].weapon, m_upgradeCards[0].slot);
                    m_selectedUpgradeCard = 0;
                }
                m_upgradePicked = true;
                m_deployTimer = 3.0f;
                pushFeed("AUTO-EQUIPPED: " + (m_upgradeCards.empty() ? "" : m_upgradeCards[0].title), 3.0f, true);
            }
        } else {
            m_deployTimer -= dt;
            if (m_deployTimer <= 0.0f) {
                loadLevel(m_gs.level + 1);
            }
        }
        return;
    }
    if (pl.attackCooldown > 0) pl.attackCooldown -= dt;
    if (pl.flashTimer > 0)     pl.flashTimer -= dt;
    if (pl.damageFlash > 0)    pl.damageFlash -= dt;

    // Reload / weapon switch timers
    if (pl.reloadTimer > 0) {
        pl.reloadTimer -= dt;
        if (pl.reloadTimer <= 0) {
            int w = pl.curWeapon;
            int maxMag = effectiveMagSize(w);
            int take = std::min(maxMag - pl.mag[w], pl.reserve[w]);
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
    bool scoped = effectiveScope(pl.curWeapon) && m_adsT > 0.85f;
    bool wantSprint = moving && shift && pl.stamina > 0 && !pl.adsMode && pl.reloadTimer <= 0;
    m_holdBreath = scoped && shift && pl.stamina > 4.f;
    pl.sprinting = wantSprint;
    pl.stamina += wantSprint ? -45.f * dt : m_holdBreath ? -22.f * dt : 28.f * dt;
    pl.stamina = std::max(0.f, std::min(CHAR_PROFILES[m_charChoice].stamina, pl.stamina));

    // -------------------------------------------------------------------------
// Acceleration-based horizontal movement
// -------------------------------------------------------------------------

// Read keyboard input as two separate axes.
//
// forwardInput:
//   W =  1
//   S = -1
//
// strafeInput:
//   D =  1
//   A = -1
float forwardInput =
    float(m_keys.count(Qt::Key_W)) -
    float(m_keys.count(Qt::Key_S));

float strafeInput =
    float(m_keys.count(Qt::Key_D)) -
    float(m_keys.count(Qt::Key_A));

// Backward movement is slower than forward movement.
// Strafing is also slightly slower.
//
// These multipliers combine during diagonal backward/strafe movement.
float directionMult = 1.f;

if (forwardInput < 0.f)
    directionMult *= 0.78f;

if (std::fabs(strafeInput) > 0.01f)
    directionMult *= 0.88f;

// Convert local input into world-space movement.
//
// Forward direction:
//   (dirX, dirY)
//
// Right direction:
//   (-dirY, dirX)
float moveX =
    pl.dirX * forwardInput -
    pl.dirY * strafeInput;

float moveY =
    pl.dirY * forwardInput +
    pl.dirX * strafeInput;

// Normalize diagonal input.
//
// Without this, pressing W+D would move the player approximately
// 1.414 times faster than pressing only W.
float inputLen = std::sqrt(moveX * moveX + moveY * moveY);

if (inputLen > 1.f) {
    moveX /= inputLen;
    moveY /= inputLen;
}

// Select the player's maximum movement speed.
//
// Sprinting: 5.8
// Normal:    3.5
// ADS:       2.0
float moveSpeed =
    pl.speedMult *
    (pl.sprinting ? 5.8f :
     pl.adsMode    ? 2.0f :
                     3.5f);

// The input direction determines the desired velocity.
// We do not apply this velocity instantly.
float targetVelX = moveX * moveSpeed * directionMult;
float targetVelY = moveY * moveSpeed * directionMult;

bool hasMoveInput = inputLen > 0.001f;

// Choose how quickly current velocity moves toward target velocity.
//
// On ground with input:
//   Responsive acceleration.
//
// On ground without input:
//   Strong friction and quick deceleration.
//
// In air with input:
//   Limited air control.
//
// In air without input:
//   Preserve most existing momentum.
float response;

if (m_onGround) {
    response = hasMoveInput ? 13.f : 18.f;
} else {
    response = hasMoveInput ? 3.5f : 0.45f;
}

// Frame-rate-independent smoothing.
//
// At a higher response value, the current velocity reaches the
// target velocity more quickly.
float velocityBlend = 1.f - std::exp(-response * dt);

pl.velX += (targetVelX - pl.velX) * velocityBlend;
pl.velY += (targetVelY - pl.velY) * velocityBlend;

// Remove tiny velocity values after ground friction has almost
// completely stopped the player. This prevents slow drifting.
float velocitySquared =
    pl.velX * pl.velX +
    pl.velY * pl.velY;

if (m_onGround &&
    !hasMoveInput &&
    velocitySquared < 0.0004f) {
    pl.velX = 0.f;
    pl.velY = 0.f;
}

// Convert velocity into this frame's movement distance.
float nx = pl.posX + pl.velX * dt;
float ny = pl.posY + pl.velY * dt;

// Preserve the existing terrain and step-height checks.
bool canMoveX =
    canStep(nx, pl.posY, m_footZ, 0.32f);

bool canMoveY =
    canStep(pl.posX, ny, m_footZ, 0.32f);

// Preserve the existing wall-sliding suppression.
//
// This now compares velocity instead of the old temporary
// input direction.
if (!canMoveX &&
    canMoveY &&
    std::fabs(pl.velY) <= 0.40f * std::fabs(pl.velX)) {
    canMoveY = false;
}

if (!canMoveY &&
    canMoveX &&
    std::fabs(pl.velX) <= 0.40f * std::fabs(pl.velY)) {
    canMoveX = false;
}

// Move along each valid axis.
//
// If an axis is blocked, cancel velocity on that axis.
// Otherwise, momentum would continuously push the player into the wall.
if (canMoveX) {
    pl.posX = nx;
} else {
    pl.velX = 0.f;
}

if (canMoveY) {
    pl.posY = ny;
} else {
    pl.velY = 0.f;
}
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
            for (int w = WPN_PISTOL; w < WPN_COUNT; w++) if (pl.owned[w]) pl.reserve[w] = WEAPONS[w].maxReserve;
            pl.hp = CHAR_PROFILES[m_charChoice].hp;
            m_gs.score += 1000;
            pushFeed("INTEL RECOVERED  +1000", 4.5f, true);
        } else if (wp.type == PICKUP_AMMO) {
            bool isScavenger = wp.emergency;
            for (int w = WPN_PISTOL; w < WPN_COUNT; w++) if (pl.owned[w]) {
                int add = isScavenger ? WEAPONS[w].magSize : (WEAPONS[w].magSize * 2);
                pl.reserve[w] = std::min(pl.reserve[w] + add, WEAPONS[w].maxReserve);
            }
            if (isScavenger) pushFeed("+ SCAVENGER RATIONS (1 MAG)", 3.5f);
            else pushFeed("+ AMMO", 3.f);
            if (pl.mag[pl.curWeapon] == 0 && pl.reserve[pl.curWeapon] > 0 && pl.reloadTimer <= 0) {
                startReload();
            }
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

    checkEmergencyDrops(dt);

    updateEnemies(dt);
    checkReinforcements(dt);

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
            int dmg = b.damage > 0 ? b.damage : int((10 + 4 * (m_gs.level - 1)) * (1.f - pl.damageReduction) * dc.dmgReceivedMult);
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
        m_gs.levelComplete = true;
        m_upgradeDecisionTimer = 30.0f;
        m_deployTimer = 0.0f;
        m_upgradePicked = false;
        m_selectedUpgradeCard = -1;
        m_gs.score += 500 * m_gs.level;
        generateUpgradeChoices();
        pushFeed("ZONE CLEARED! SELECT TACTICAL UPGRADE [1, 2, 3]", 5.0f, true);
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
        bool los = d < std::max(22.f, detectRange) && hasLOS(e.x, e.y, ez, pl.posX, pl.posY, m_footZ + 0.5f);
        if (los && d < detectRange) {
            if (e.state != ES_ALERT) {
                // Initial target acquisition: alert bot with snappy reaction time
                e.state = ES_ALERT;
                e.fireCooldown = frand(0.35f, 0.65f) * dc.fireCoolMult;
                e.strafeTimer = frand(2.0f, 3.8f);
                e.moveX = frand() < 0.5f ? -1.f : 1.f;
            }
            e.alertDist = 0.f; // Player detected! Pursue & engage
        }

        // Pick movement vector based on archetype and tactical posture
        float vx = 0, vy = 0, spd = 0;
        if (e.state == ES_ALERT) {
            if (e.alertDist > 0.f) {
                // Moving towards bullet alert position
                vx = e.alertDirX;
                vy = e.alertDirY;
                spd = (e.type == ET_SCOUT ? 1.25f : 0.95f);
            } else {
                float ux = d > 0.01f ? dx / d : 0, uy = d > 0.01f ? dy / d : 0;
                float perpX = -uy * e.moveX, perpY = ux * e.moveX;

                e.strafeTimer -= dt;
                if (e.strafeTimer <= 0) {
                    // Commit to tactical path for 2.0 to 4.0 seconds (eliminates side-to-side jitter!)
                    e.strafeTimer = frand(2.2f, 4.0f);
                    e.moveX = frand() < 0.5f ? -1.f : 1.f;
                }

                // TACTICAL STOP-AND-SHOOT:
                // When aiming / preparing to shoot within effective range, stop to steady weapon
                float maxFiringRange = (e.type == ET_SNIPER ? 22.f : e.type == ET_RIFLEMAN ? 15.f : e.type == ET_SCOUT ? 10.f : 7.5f);
                bool aimingToFire = (los && d <= maxFiringRange && e.fireCooldown <= 0.35f);

                if (aimingToFire) {
                    vx = 0.f; vy = 0.f; spd = 0.f;
                } else {
                    // Tactical movement & repositioning:
                    if (e.type == ET_SCOUT) {
                        // Scout: agile flanking rusher
                        if (!los || d > 5.5f) {
                            vx = ux * 0.7f + perpX * 0.5f; vy = uy * 0.7f + perpY * 0.5f; spd = 1.30f;
                        } else if (d < 2.0f) {
                            vx = -ux * 0.6f + perpX * 0.6f; vy = -uy * 0.6f + perpY * 0.6f; spd = 1.20f;
                        } else {
                            vx = ux * 0.25f + perpX * 0.8f; vy = uy * 0.25f + perpY * 0.8f; spd = 1.15f;
                        }
                    } else if (e.type == ET_SHOTGUNNER) {
                        // Shotgunner: steady direct advance into close quarters
                        if (!los || d > 2.8f) {
                            vx = ux * 0.85f + perpX * 0.25f; vy = uy * 0.85f + perpY * 0.25f; spd = 1.05f;
                        } else if (d < 1.4f) {
                            vx = -ux * 0.6f + perpX * 0.5f; vy = -uy * 0.6f + perpY * 0.5f; spd = 0.85f;
                        } else {
                            vx = perpX; vy = perpY; spd = 0.80f;
                        }
                    } else if (e.type == ET_SNIPER) {
                        // Sniper: holds long-range vantage points. Backpedals if player approaches!
                        if (d < 7.5f) {
                            vx = -ux * 0.9f + perpX * 0.2f; vy = -uy * 0.9f + perpY * 0.2f; spd = 0.85f;
                        } else if (!los) {
                            vx = ux; vy = uy; spd = 0.75f;
                        } else {
                            // Holds angle or tiny lateral micro-adjustment
                            vx = perpX * 0.5f; vy = perpY * 0.5f; spd = 0.35f;
                        }
                    } else {
                        // Rifleman: methodic medium-range tactics
                        if (!los || d > 7.5f) {
                            vx = ux * 0.8f + perpX * 0.3f; vy = uy * 0.8f + perpY * 0.3f; spd = 0.90f;
                        } else if (d < 2.8f) {
                            vx = -ux * 0.8f + perpX * 0.3f; vy = -uy * 0.8f + perpY * 0.3f; spd = 0.80f;
                        } else {
                            vx = ux * 0.2f + perpX * 0.8f; vy = uy * 0.2f + perpY * 0.8f; spd = 0.75f;
                        }
                    }
                }
            }
            if (e.elite) spd *= 1.15f;
            if (e.hitFlash > 0) spd *= 0.25f; // staggered
            if (e.shootAnim > 0) spd = 0.f;
        } else {
            // Idle wander
            e.wanderTimer -= dt;
            if (e.wanderTimer <= 0) {
                e.wanderTimer = frand(2.0f, 4.5f);
                if (frand() < 0.5f) { e.moveX = 0; e.moveY = 0; }
                else { float a = frand(0, 6.2831853f); e.moveX = std::cos(a); e.moveY = std::sin(a); }
            }
            vx = e.moveX; vy = e.moveY;
            spd = (vx != 0 || vy != 0) ? (e.type == ET_SCOUT ? 0.75f : 0.45f) : 0.f;
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
        if (e.moving) e.walkPhase += moved * (e.type == ET_SCOUT ? 1.8f : 1.3f);

        // Progress distance travelled in bullet direction
        if (e.state == ES_ALERT && e.alertDist > 0.f) {
            if (moved > 0.0005f) {
                e.alertDist -= moved;
            } else {
                e.alertDist -= dt * 3.5f;
            }
            if (e.alertDist <= 0.f) {
                e.alertDist = 0.f;
                if (!(los && d < detectRange)) {
                    e.state = ES_IDLE;
                    e.wanderTimer = frand(1.0f, 2.5f);
                }
            }
        }

        // Fire: engage target with realistic tactical cadence within effective weapon range
        float firingRange = (e.type == ET_SNIPER ? 22.f : e.type == ET_RIFLEMAN ? 15.f : e.type == ET_SCOUT ? 10.f : 7.5f);
        if (e.state == ES_ALERT && e.alertDist <= 0.f && los && d <= firingRange && e.fireCooldown <= 0 && e.hitFlash <= 0) {
            enemyFire(e);
            e.shootAnim = 0.22f;
            float cool = 0.f;
            if (e.type == ET_SCOUT)           cool = frand(0.70f, 1.10f); // Rapid pistol fire
            else if (e.type == ET_SHOTGUNNER) cool = frand(1.60f, 2.20f); // Heavy pump blast
            else if (e.type == ET_SNIPER)     cool = frand(2.10f, 2.70f); // Precise sniper cadence
            else                              cool = frand(1.10f, 1.65f); // Rifleman bursts
            cool *= dc.fireCoolMult;
            if (e.elite) cool *= 0.75f;
            e.fireCooldown = cool;
        }
        (void)dc;
    }
}

void MainWindow::spawnEnemy(float x, float y, bool elite, EnemyType type)
{
    int baseHp = enemyMaxHP();
    Enemy e; e.x = x; e.y = y; e.elite = elite; e.type = type;
    int hp = baseHp;
    if (type == ET_SCOUT) hp = int(baseHp * 0.70f);
    else if (type == ET_SHOTGUNNER) hp = int(baseHp * 1.35f);
    else if (type == ET_SNIPER) hp = int(baseHp * 0.85f);
    e.hp = e.maxHp = elite ? hp * 2 : hp;

    const LevelCfg &cfg = LEVEL_CFG[std::min(m_gs.level, 5)];
    const DiffConfig &dc = DIFF_CFG[m_difficulty];
    float baseRange = cfg.aggroRange;
    if (type == ET_SNIPER) baseRange += 7.0f;
    else if (type == ET_SCOUT) baseRange += 1.5f;
    if (elite) baseRange += 3.0f;

    e.aggroRange = baseRange * dc.aggroRangeMult;
    e.fireCooldown = frand(0.4f, 1.2f) * dc.fireCoolMult;
    e.strafeTimer = frand(2.0f, 3.8f);
    e.wanderTimer = frand(0.f, 3.f);
    e.walkPhase = frand();
    m_gs.enemies.push_back(e);
}

void MainWindow::checkReinforcements(float dt)
{
    // In higher procedural zones (level >= 2), periodically spawn out-of-sight reinforcements
    if (m_gs.level < 2 || m_gs.levelComplete || m_gs.gameOver) return;
    m_reinforceTimer -= dt;
    if (m_reinforceTimer > 0) return;

    // Count alive enemies
    int aliveCount = 0;
    for (const auto &e : m_gs.enemies) if (e.alive) aliveCount++;
    if (aliveCount >= 8) { m_reinforceTimer = 8.f; return; }

    const Player &pl = m_gs.player;
    // Find a valid floor tile out of player LOS and at distance 10..22 units
    for (int tries = 0; tries < 40; tries++) {
        int rx = 2 + rand() % (MAP_W - 4), ry = 2 + rand() % (MAP_H - 4);
        if (m_map[ry][rx] != CELL_FLOOR || m_acid[ry][rx]) continue;
        float fx = rx + 0.5f, fy = ry + 0.5f;
        float dsq = (fx - pl.posX) * (fx - pl.posX) + (fy - pl.posY) * (fy - pl.posY);
        if (dsq < 100.f || dsq > 500.f) continue;
        float fz = floorH(fx, fy) + 0.5f;
        if (hasLOS(fx, fy, fz, pl.posX, pl.posY, m_footZ + 0.5f)) continue; // Must be out of sight!
        if (!posValid(fx, fy, 0.28f)) continue;

        EnemyType rt = (rand() % 100 < 40) ? ET_SCOUT : (rand() % 100 < 70) ? ET_RIFLEMAN : ET_SHOTGUNNER;
        spawnEnemy(fx, fy, false, rt);
        pushFeed("HOSTILE REINFORCEMENTS INBOUND", 3.0f);
        m_reinforceTimer = frand(20.f, 32.f);
        break;
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
    if (pl.curWeapon == WPN_KNIFE) {
        fireWeapon();
        return;
    }
    if (pl.mag[pl.curWeapon] <= 0) {
        if (pl.reserve[pl.curWeapon] > 0) startReload();
        else { pl.attackCooldown = 0.3f; pushFeed("OUT OF AMMO - PRESS 1 FOR KNIFE", 1.5f); }
        return;
    }
    fireWeapon();
}

void MainWindow::fireWeapon()
{
    Player &pl = m_gs.player;
    const WeaponDef &wd = WEAPONS[pl.curWeapon];

    if (pl.curWeapon == WPN_KNIFE) {
        pl.attackCooldown = effectiveFireDelay(WPN_KNIFE);
        m_actionT = 1.0f;
        m_knifeSwing = 1 - m_knifeSwing; // alternate slash / stab
        m_camKick += (m_knifeSwing ? 1.0f : -0.8f);
        m_kick = std::min(1.2f, m_kick + 0.35f);
        m_gs.shotsFired++;

        computeView();
        float ax, ay; aimPoint(ax, ay);
        HitResult h = hitscan(ax, ay);

        if (h.enemy >= 0 && h.depth <= 2.2f) {
            m_gs.shotsHit++;
            float headMult = (pl.upgradeSpecial[WPN_KNIFE] ? 3.5f : wd.headMult);
            float mult = h.part == PART_HEAD ? headMult : (h.part == PART_LIMB ? wd.limbMult : 1.0f);
            int dmg = int(effectiveDamage(WPN_KNIFE) * mult * frand(0.95f, 1.15f));
            damageEnemy(h.enemy, dmg, h.part, h.x, h.y, h.z);
            m_hitMarker = 0.3f;
            if (h.part == PART_HEAD) m_headMarker = 0.45f;
            if (h.enemy < int(m_gs.enemies.size())) {
                m_gs.enemies[h.enemy].hitFlash = 0.35f; // enemy stagger
            }
            spawnBlood(h.x, h.y, h.z, h.part == PART_HEAD ? 24 : 14, 1.5f);
        } else if (!h.sky && h.depth <= 2.0f) {
            spawnImpact(h.x, h.y, h.z, h.floor);
        }
        return;
    }

    pl.mag[pl.curWeapon]--;
    pl.attackCooldown = effectiveFireDelay(pl.curWeapon);
    pl.flashTimer = pl.curWeapon == WPN_SHOTGUN ? 0.09f : 0.06f;
    m_flashLight = 1.f; m_flashSeed = rand();
    float recoil = effectiveRecoil(pl.curWeapon);
    m_kick = std::min(1.8f, m_kick + 0.55f + 0.25f * recoil);
    m_actionT = 1.f;
    m_camKick += 2.5f * recoil;
    float climb = 0.012f * recoil * (m_adsT > 0.5f ? 0.6f : 1.f);
    m_pitch = rs::clampf(m_pitch + climb, -0.75f, 0.75f);
    m_recoilPitch += climb * 0.85f;
    turn(frand(-1.f, 1.f) * 0.004f * recoil);
    m_gs.shotsFired++;

    computeView();
    float ax, ay; aimPoint(ax, ay);
    bool moving = m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D);
    float baseSpread = effectiveSpread(pl.curWeapon);
    float spread = baseSpread * (moving ? 1.8f : 1.f) * (inAir() ? 2.5f : 1.f);
    spread *= 1.f - 0.8f * m_adsT;
    if (wd.pellets > 1) spread = std::max(spread, baseSpread * 0.6f);
    float spreadPx = spread * W;

    // Tracer origin: the on-screen muzzle unprojected 0.6 units into the world
    const float startD = 0.6f;
    float mcx = 2.f * m_muzzleX / W - 1.f;
    float sX = pl.posX + (pl.dirX + pl.planeX * m_adsFOVMult * mcx) * startD;
    float sY = pl.posY + (pl.dirY + pl.planeY * m_adsFOVMult * mcx) * startD;
    float sZ = m_eye + (m_hor - m_muzzleY) / m_K * startD;
    if (effectiveScope(pl.curWeapon) && m_adsT > 0.85f) { sX = pl.posX + pl.dirX * startD; sY = pl.posY + pl.dirY * startD; sZ = m_eye - 0.08f; }

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
            float falloff = wd.pellets > 1 ? std::max(0.40f, 1.f - h.depth / 18.f) : 1.f;
            int baseDmg = effectiveDamage(pl.curWeapon);
            int dmg = std::max(1, int(baseDmg * mult * falloff * frand(0.9f, 1.1f) + 0.5f));
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
    if (!(effectiveScope(pl.curWeapon) && m_adsT > 0.85f)) m_casings.push_back(c);

    if (pl.mag[pl.curWeapon] == 0 && pl.reserve[pl.curWeapon] > 0) startReload();
}

void MainWindow::damageEnemy(int idx, int dmg, HitPart part, float hx, float hy, float hz)
{
    Enemy &e = m_gs.enemies[idx];
    if (!e.alive) return;
    bool head = part == PART_HEAD;
    e.hp -= dmg; e.hitFlash = 0.16f;

    Player &pl = m_gs.player;
    float dx = pl.posX - e.x, dy = pl.posY - e.y, d = std::sqrt(dx * dx + dy * dy);
    float ez = floorH(e.x, e.y) + 0.62f;
    float detectRange = e.aggroRange * 1.5f;
    bool los = d < std::max(18.f, detectRange) && hasLOS(e.x, e.y, ez, pl.posX, pl.posY, m_footZ + 0.5f);

    e.state = ES_ALERT;
    if (los && d < detectRange) {
        // Player is within detection range: alert and pursue normally
        e.alertDist = 0.f;
    } else {
        // Player is out of detection range: run in the direction the bullet came from
        if (d > 0.01f) {
            e.alertDirX = dx / d;
            e.alertDirY = dy / d;
        } else {
            e.alertDirX = 0.f;
            e.alertDirY = 0.f;
        }
        // Run in the bullet direction for a certain distance (up to 12 units)
        e.alertDist = std::max(6.0f, std::min(d, 12.0f));
    }

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
    int maxMag = effectiveMagSize(w);
    if (w == WPN_KNIFE || pl.reloadTimer > 0 || pl.switchTimer > 0 || pl.mag[w] >= maxMag || pl.reserve[w] <= 0) return;
    float t = WEAPONS[w].reloadTime;
    if (w == WPN_SHOTGUN) t = 0.5f + 0.32f * std::min(maxMag - pl.mag[w], pl.reserve[w]);
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
    if (e.type == ET_SNIPER) acc = std::min(0.98f, acc + 0.18f);
    else if (e.type == ET_SCOUT) acc = std::max(0.40f, acc - 0.12f);
    if (e.elite) acc = std::min(0.98f, acc + 0.1f);

    float z0 = floorH(e.x, e.y) + 0.62f, z1 = m_footZ + 0.45f;
    float baseSpd = m_gs.enemyBulletSpeed * (e.elite ? 1.25f : 1.f);

    // Calculate base damage scaling
    int baseDmg = int((10 + 4 * (m_gs.level - 1)) * (1.f - pl.damageReduction) * DIFF_CFG[m_difficulty].dmgReceivedMult);

    if (e.type == ET_SHOTGUNNER) {
        // Shotgunner fires a 5-pellet spread cone
        int pellets = 5;
        int pelletDmg = std::max(3, baseDmg / 3);
        float shotSpd = baseSpd * 1.1f;
        for (int p = 0; p < pellets; p++) {
            float a = std::atan2(dy, dx) + frand(-1.f, 1.f) * (1.f - acc) * 0.35f;
            Bullet b; b.x = e.x + std::cos(a) * 0.3f; b.y = e.y + std::sin(a) * 0.3f; b.z = z0;
            b.px = b.x; b.py = b.y; b.pz = b.z;
            b.dx = std::cos(a) * shotSpd; b.dy = std::sin(a) * shotSpd;
            b.dz = (z1 - z0) / (d / shotSpd) + frand(-1.f, 1.f) * 0.35f;
            b.owner = ENEMY_SHOT; b.alive = true; b.damage = pelletDmg;
            m_gs.bullets.push_back(b);
        }
    } else {
        float a = std::atan2(dy, dx) + frand(-1.f, 1.f) * (1.f - acc) * 0.22f;
        float spd = baseSpd;
        int dmg = baseDmg;
        if (e.type == ET_SNIPER) {
            spd *= 1.85f; // High-velocity sniper round
            dmg = int(baseDmg * 1.75f); // High-caliber damage
        } else if (e.type == ET_SCOUT) {
            spd *= 0.95f;
            dmg = int(baseDmg * 0.65f); // Light sidearm round
        }
        Bullet b; b.x = e.x + std::cos(a) * 0.3f; b.y = e.y + std::sin(a) * 0.3f; b.z = z0;
        b.px = b.x; b.py = b.y; b.pz = b.z;
        b.dx = std::cos(a) * spd; b.dy = std::sin(a) * spd;
        b.dz = (z1 - z0) / (d / spd) + frand(-1.f, 1.f) * (1.f - acc) * 0.5f;
        b.owner = ENEMY_SHOT; b.alive = true; b.damage = dmg;
        m_gs.bullets.push_back(b);
    }
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

bool MainWindow::findEmergencyDropPos(float &outX, float &outY) const
{
    const Player &pl = m_gs.player;
    float bestX = 0, bestY = 0;
    bool foundAny = false;

    // Search around the player for a reachable, walkable floor cell
    // Preferred distance: 5.0 to 8.5 units
    float baseAngle = frand(0.f, 6.2831853f);
    for (int i = 0; i < 32; i++) {
        float ang = baseAngle + i * (6.2831853f / 32.f);
        for (float d = 7.5f; d >= 4.5f; d -= 0.75f) {
            float tx = pl.posX + std::cos(ang) * d;
            float ty = pl.posY + std::sin(ang) * d;
            int cx = int(std::floor(tx)), cy = int(std::floor(ty));
            if (cx < 1 || cx >= MAP_W - 1 || cy < 1 || cy >= MAP_H - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR) continue;
            if (!posValid(tx, ty, 0.28f)) continue;
            float fh = floorH(tx, ty);
            if (std::fabs(fh - m_footZ) > 0.75f) continue;

            bool los = hasLOS(pl.posX, pl.posY, m_footZ + 0.5f, tx, ty, fh + 0.3f);
            if (los) {
                outX = tx;
                outY = ty;
                return true;
            }
            if (!foundAny) {
                bestX = tx;
                bestY = ty;
                foundAny = true;
            }
        }
    }

    if (foundAny) {
        outX = bestX;
        outY = bestY;
        return true;
    }

    // Fallback: try closer distances (2.0 to 4.5 units) if in a compact room
    for (int i = 0; i < 24; i++) {
        float ang = baseAngle + i * (6.2831853f / 24.f);
        for (float d = 4.0f; d >= 2.0f; d -= 0.5f) {
            float tx = pl.posX + std::cos(ang) * d;
            float ty = pl.posY + std::sin(ang) * d;
            int cx = int(std::floor(tx)), cy = int(std::floor(ty));
            if (cx < 1 || cx >= MAP_W - 1 || cy < 1 || cy >= MAP_H - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR) continue;
            if (!posValid(tx, ty, 0.28f)) continue;
            float fh = floorH(tx, ty);
            if (std::fabs(fh - m_footZ) > 0.75f) continue;

            outX = tx;
            outY = ty;
            return true;
        }
    }

    // Extreme fallback: slight offset in reverse direction
    for (float d = 1.8f; d >= 1.0f; d -= 0.4f) {
        float tx = pl.posX - pl.dirX * d;
        float ty = pl.posY - pl.dirY * d;
        int cx = int(std::floor(tx)), cy = int(std::floor(ty));
        if (cx >= 1 && cx < MAP_W - 1 && cy >= 1 && cy < MAP_H - 1 && !solidCell(cx, cy) && !m_acid[cy][cx] && posValid(tx, ty, 0.2f)) {
            outX = tx;
            outY = ty;
            return true;
        }
    }
    return false;
}

bool MainWindow::findHotZoneDropPos(float &outX, float &outY) const
{
    const Player &pl = m_gs.player;

    // Collect living enemies
    struct CandEnemy { int idx; float dist; };
    std::vector<CandEnemy> candidates;
    for (int i = 0; i < int(m_gs.enemies.size()); ++i) {
        const auto &e = m_gs.enemies[i];
        if (!e.alive) continue;
        float dx = e.x - pl.posX, dy = e.y - pl.posY;
        float d = std::sqrt(dx * dx + dy * dy);
        candidates.push_back({ i, d });
    }

    // Sort by distance descending to prioritize distant hostiles (10 to 24 units away)
    std::sort(candidates.begin(), candidates.end(), [](const CandEnemy &a, const CandEnemy &b) {
        return a.dist > b.dist;
    });

    // Try finding a walkable floor position near one of the distant enemies (Hot Zone!)
    for (const auto &c : candidates) {
        if (c.dist < 8.0f) continue; // too close, not a distant hot zone
        const auto &e = m_gs.enemies[c.idx];
        float baseAng = frand(0.f, 6.2831853f);
        for (int step = 0; step < 16; ++step) {
            float ang = baseAng + step * (6.2831853f / 16.f);
            for (float r = 2.5f; r <= 4.5f; r += 0.7f) {
                float tx = e.x + std::cos(ang) * r;
                float ty = e.y + std::sin(ang) * r;
                int cx = int(std::floor(tx)), cy = int(std::floor(ty));
                if (cx < 1 || cx >= MAP_W - 1 || cy < 1 || cy >= MAP_H - 1) continue;
                if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR) continue;
                if (!posValid(tx, ty, 0.28f)) continue;
                float fh = floorH(tx, ty);
                if (std::fabs(fh - floorH(e.x, e.y)) > 0.8f) continue;
                outX = tx;
                outY = ty;
                return true;
            }
        }
    }

    // Fallback: If no distant enemy, find a distant floor cell 14 to 22 units from player
    float baseAng = frand(0.f, 6.2831853f);
    for (int step = 0; step < 36; ++step) {
        float ang = baseAng + step * (6.2831853f / 36.f);
        for (float d = 20.0f; d >= 12.0f; d -= 1.0f) {
            float tx = pl.posX + std::cos(ang) * d;
            float ty = pl.posY + std::sin(ang) * d;
            int cx = int(std::floor(tx)), cy = int(std::floor(ty));
            if (cx < 1 || cx >= MAP_W - 1 || cy < 1 || cy >= MAP_H - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR) continue;
            if (!posValid(tx, ty, 0.28f)) continue;
            outX = tx;
            outY = ty;
            return true;
        }
    }

    // Closer fallback: 8 to 12 units
    for (int step = 0; step < 24; ++step) {
        float ang = baseAng + step * (6.2831853f / 24.f);
        for (float d = 12.0f; d >= 7.0f; d -= 1.0f) {
            float tx = pl.posX + std::cos(ang) * d;
            float ty = pl.posY + std::sin(ang) * d;
            int cx = int(std::floor(tx)), cy = int(std::floor(ty));
            if (cx < 1 || cx >= MAP_W - 1 || cy < 1 || cy >= MAP_H - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR) continue;
            if (!posValid(tx, ty, 0.28f)) continue;
            outX = tx;
            outY = ty;
            return true;
        }
    }

    return findEmergencyDropPos(outX, outY);
}

void MainWindow::checkEmergencyDrops(float dt)
{
    if (m_gs.gameOver || m_screen != SCR_PLAYING) return;
    Player &pl = m_gs.player;
    if (pl.hp <= 0) return;

    if (m_emergencyAmmoCooldown > 0.f)   m_emergencyAmmoCooldown -= dt;
    if (m_emergencyHealthCooldown > 0.f) m_emergencyHealthCooldown -= dt;

    // 1. Ammo drop check: when the player runs out of firearms ammo
    int totalAmmo = 0;
    for (int w = WPN_PISTOL; w < WPN_COUNT; ++w) {
        if (pl.owned[w]) {
            totalAmmo += pl.mag[w] + pl.reserve[w];
        }
    }

    if (totalAmmo == 0) {
        bool hasActiveEmergencyAmmo = false;
        for (const auto &wp : m_gs.weaponPickups) {
            if (wp.active && wp.emergency && wp.type == PICKUP_AMMO) {
                hasActiveEmergencyAmmo = true;
                break;
            }
        }
        if (!hasActiveEmergencyAmmo && m_emergencyAmmoCooldown <= 0.f) {
            float dropX = 0, dropY = 0;
            if (findHotZoneDropPos(dropX, dropY)) {
                WeaponPickup wp;
                wp.x = dropX;
                wp.y = dropY;
                wp.bobPhase = frand(0.f, 6.28f);
                wp.type = PICKUP_AMMO;
                wp.active = true;
                wp.emergency = true;
                m_gs.weaponPickups.push_back(wp);
                m_emergencyAmmoCooldown = 25.0f;
                pushFeed("AMMO DROPPED IN HOSTILE ZONE - CHECK COMPASS", 5.0f, true);
            }
        }
    }

    // 2. Health drop check: when the player is in critical health condition
    int maxHP = CHAR_PROFILES[m_charChoice].hp;
    bool criticalHealth = (pl.hp <= std::max(25, int(maxHP * 0.25f)));

    if (criticalHealth) {
        bool hasActiveEmergencyHealth = false;
        for (const auto &k : m_gs.healthKits) {
            if (k.active && k.emergency) {
                hasActiveEmergencyHealth = true;
                break;
            }
        }
        if (!hasActiveEmergencyHealth && m_emergencyHealthCooldown <= 0.f) {
            float dropX = 0, dropY = 0;
            if (findEmergencyDropPos(dropX, dropY)) {
                HealthKit k;
                k.x = dropX;
                k.y = dropY;
                k.bobPhase = frand(0.f, 6.28f);
                k.active = true;
                k.emergency = true;
                m_gs.healthKits.push_back(k);
                m_emergencyHealthCooldown = 25.0f;
                pushFeed("HEALTH DROPPED NEAR YOUR LOCATION", 4.5f, true);
            }
        }
    }
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
    if (effectiveScope(m_gs.player.curWeapon) && m_adsT > 0.85f) { ax = W / 2.f + m_scopeSwayX; ay = H / 2.f + m_scopeSwayY; }
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
    case SCR_MENU:
        if (m_menuCursor == 0) m_screen = SCR_MAP_SELECT;
        else if (m_menuCursor == 1) startDebugMode();
        else QApplication::quit();
        break;
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
            if (k == Qt::Key_Up)   m_menuCursor = std::max(0, m_menuCursor - 1);
            if (k == Qt::Key_Down) m_menuCursor = std::min(2, m_menuCursor + 1);
            if (k == Qt::Key_1) { m_menuCursor = 0; menuConfirm(); return; }
            if (k == Qt::Key_2) { m_menuCursor = 1; menuConfirm(); return; }
            if (k == Qt::Key_3) { m_menuCursor = 2; menuConfirm(); return; }
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

    // Level completion upgrade selection
    if (m_gs.levelComplete) {
        if (k >= Qt::Key_1 && k <= Qt::Key_3) {
            int idx = k - Qt::Key_1;
            if (idx < int(m_upgradeCards.size())) {
                applyUpgrade(m_upgradeCards[idx].weapon, m_upgradeCards[idx].slot);
                m_selectedUpgradeCard = idx;
                m_upgradePicked = true;
                m_deployTimer = 3.0f;
                pushFeed("UPGRADE EQUIPPED: " + m_upgradeCards[idx].title, 3.5f, true);
            }
            return;
        }
        if (k == Qt::Key_Space || k == Qt::Key_Return || k == Qt::Key_Enter) {
            if (!m_upgradePicked && !m_upgradeCards.empty()) {
                applyUpgrade(m_upgradeCards[0].weapon, m_upgradeCards[0].slot);
                m_selectedUpgradeCard = 0;
                m_upgradePicked = true;
            }
            loadLevel(m_gs.level + 1);
            return;
        }
        return;
    }

    // Toggle armory modal
    if (k == Qt::Key_U) {
        m_showArmory = !m_showArmory;
        return;
    }
    if (m_showArmory) {
        if (k == Qt::Key_Escape || k == Qt::Key_Space) {
            m_showArmory = false;
            return;
        }
    }

    // Toggle upgrades with 9 key
    if (k == Qt::Key_9) {
        bool any = pl.upgradeExtMag[WPN_RIFLE];
        for (int w = 0; w < WPN_COUNT; w++) {
            pl.upgradeExtMag[w] = !any;
            pl.upgradeCompensator[w] = !any;
            pl.upgradeAmmo[w] = !any;
            pl.upgradeSpecial[w] = !any;
            if (!any) pl.mag[w] = effectiveMagSize(w);
        }
        pushFeed(any ? "ALL UPGRADES REMOVED" : "ALL UPGRADES EQUIPPED", 2.5f);
        return;
    }

    bool live = !m_paused && !m_gs.gameOver;
    if (live) {
        if (k == Qt::Key_Space && m_onGround) { m_velZ = 5.5f; m_onGround = false; }
        if (k == Qt::Key_F) tryFire();
        if (k == Qt::Key_E) tryOpenDoor();
        if (k == Qt::Key_C) { m_pitch = 0.f; m_aimX = W / 2.f; m_aimY = H / 2.f; }
        if (k == Qt::Key_V) { m_freeAim = !m_freeAim; m_aimX = W / 2.f; m_aimY = H / 2.f; pushFeed(m_freeAim ? "FREE AIM: ON" : "FREE AIM: OFF", 2.f); }
        if (k == Qt::Key_Tab) pl.adsMode = !pl.adsMode;
        if (k >= Qt::Key_1 && k <= Qt::Key_4) switchWeapon(k - Qt::Key_1);
        if (k == Qt::Key_Q) {
            if (pl.curWeapon == WPN_KNIFE) {
                for (int w = WPN_PISTOL; w < WPN_COUNT; ++w) {
                    if (pl.owned[w]) { switchWeapon(w); break; }
                }
            } else {
                switchWeapon(WPN_KNIFE);
            }
        }
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
    if (m_gs.levelComplete) {
        QPointF f = widgetToFrame(e->position());
        int cw = 232, ch = 280, cy = 122;
        int spacing = 18;
        int totalW = 3 * cw + 2 * spacing;
        int startX = (W - totalW) / 2;
        for (int i = 0; i < int(m_upgradeCards.size()) && i < 3; i++) {
            int cx = startX + i * (cw + spacing);
            if (f.x() >= cx && f.x() < cx + cw && f.y() >= cy && f.y() < cy + ch) {
                applyUpgrade(m_upgradeCards[i].weapon, m_upgradeCards[i].slot);
                m_selectedUpgradeCard = i;
                m_upgradePicked = true;
                m_deployTimer = 3.0f;
                pushFeed("UPGRADE EQUIPPED: " + m_upgradeCards[i].title, 3.5f, true);
                return;
            }
        }
        // Continue / Deploy button
        int btnW = 240, btnH = 36;
        int btnX = (W - btnW) / 2, btnY = 432;
        if (f.x() >= btnX && f.x() < btnX + btnW && f.y() >= btnY && f.y() < btnY + btnH) {
            if (!m_upgradePicked && !m_upgradeCards.empty()) {
                applyUpgrade(m_upgradeCards[0].weapon, m_upgradeCards[0].slot);
                m_selectedUpgradeCard = 0;
                m_upgradePicked = true;
            }
            loadLevel(m_gs.level + 1);
            return;
        }
        return;
    }
    if (m_showArmory) {
        m_showArmory = false;
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
    if (live && effectiveScope(m_gs.player.curWeapon) && m_adsT > 0.5f) {
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
