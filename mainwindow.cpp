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
#include "missions.h"
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
static inline float frandB2(float a, float b) { return frand(a, b); }

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor — bake every texture procedurally
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), m_buf(W, H, QImage::Format_ARGB32)
{
    initDefaultKeyBinds();
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(W / 2, H / 2);
    setWindowTitle("RASTER  -  Tactical Shooter");
    resize(W, H);
    m_snd = new SoundEngine(this);
    m_pixels = reinterpret_cast<uint32_t *>(m_buf.bits());
    m_cv = rs::Canvas{m_pixels, W, H};
    m_depth.assign(size_t(W) * H, 1e30f);
    loadLayoutRows(false);
    for (int m = 0; m < MAP_COUNT; m++) parseLayout(ALL_LAYOUTS[m], LAYOUT_ROWS[m], m_parsed[m]);

    std::vector<uint32_t> tmp;
    for (int t = 0; t < THEME_COUNT; t++) {
        bakeCanopy(m_canopyTex[t], t);
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
    m_typeAtlas[ET_RIFLEMAN] = m_enemyAtlas; m_typeMask[ET_RIFLEMAN] = m_enemyMask;
    m_typeAtlas[ET_SCOUT] = m_scoutAtlas; m_typeMask[ET_SCOUT] = m_scoutMask;
    m_typeAtlas[ET_SHOTGUNNER] = m_shotgunAtlas; m_typeMask[ET_SHOTGUNNER] = m_shotgunMask;
    m_typeAtlas[ET_SNIPER] = m_sniperAtlas; m_typeMask[ET_SNIPER] = m_sniperMask;
    bakeEnemyAtlas(m_typeAtlas[ET_GRENADIER], m_typeMask[ET_GRENADIER], GRENADIER_LOOK, GEAR_GRENADIER);
    bakeEnemyAtlas(m_typeAtlas[ET_SHIELD], m_typeMask[ET_SHIELD], SHIELD_LOOK, GEAR_SHIELD);
    bakeEnemyAtlas(m_typeAtlas[ET_MEDIC], m_typeMask[ET_MEDIC], MEDIC_LOOK, GEAR_MEDIC);
    bakeEnemyAtlas(m_typeAtlas[ET_HEAVY], m_typeMask[ET_HEAVY], HEAVY_LOOK, GEAR_HEAVY, true);
    bakeEnemyAtlas(m_typeAtlas[ET_STALKER], m_typeMask[ET_STALKER], STALKER_LOOK, GEAR_STALKER);
    bakeMachineAtlas(m_typeAtlas[ET_DRONE], m_typeMask[ET_DRONE], 0);
    bakeMachineAtlas(m_typeAtlas[ET_TURRET], m_typeMask[ET_TURRET], 1);
    bakeBossAtlases();
    bakePortraits();
    bakePickups(m_pickupTex);
    for (int c = 0; c < 3; c++) {
        m_portrait[c].assign(PORTRAIT * PORTRAIT, 0);
        paintSoldier(m_portrait[c].data(), nullptr, PORTRAIT, lookForClass(c), POSE_WALK, 0.f, c == 2);
    }
    // Bake every map's light map up front so menus never hitch
    for (int m = 0; m < MAP_COUNT; m++) loadMapGeometry(m);
    loadMapGeometry(m_mapChoice);

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);
    m_clock.start();

    m_net = new NetworkManager(this);
    connect(m_net, &NetworkManager::matchStarted, this, [this]() {
        startMultiplayerMatch();
    });
    connect(m_net, &NetworkManager::lobbyUpdated, this, [this]() {
        QWidget::update();
    });
    connect(m_net, &NetworkManager::connectionError, this, [this](const QString &err) {
        pushFeed(err, 4.0f);
    });
    m_curHazardName = THEMES[0].hazName;
    m_curHazardColor = THEMES[0].hazLight;
}

bool MainWindow::isActionPressed(KeyAction act) const
{
    if (act < 0 || act >= ACT_COUNT) return false;
    int k = m_keyBinds[act];
    return k > 0 && m_keys.count(k) > 0;
}

bool MainWindow::isPlayerMoving() const
{
    return isActionPressed(ACT_MOVE_FWD) || isActionPressed(ACT_MOVE_BACK) ||
           isActionPressed(ACT_MOVE_LEFT) || isActionPressed(ACT_MOVE_RIGHT);
}

const char* MainWindow::hazardNameAt(int cx, int cy) const
{
    if (cx >= 0 && cy >= 0 && cx < m_mapW && cy < m_mapH) {
        bool isQueenAcid = (!m_baseAcid.empty() && size_t(cy * m_mapW + cx) < m_baseAcid.size() && !m_baseAcid[cy * m_mapW + cx]);
        if (isQueenAcid) return "ACID";
    }
    return (m_theme >= 0 && m_theme < THEME_COUNT && THEMES[m_theme].hazName) ? THEMES[m_theme].hazName : "HAZARD";
}

uint32_t MainWindow::hazardColorAt(int cx, int cy) const
{
    if (cx >= 0 && cy >= 0 && cx < m_mapW && cy < m_mapH) {
        bool isQueenAcid = (!m_baseAcid.empty() && size_t(cy * m_mapW + cx) < m_baseAcid.size() && !m_baseAcid[cy * m_mapW + cx]);
        if (isQueenAcid) return rs::rgb(150, 210, 60);
    }
    return (m_theme >= 0 && m_theme < THEME_COUNT) ? THEMES[m_theme].hazLight : 0xFF96E63C;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Multiplayer (Sector War) Session Management
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::openMultiplayerLobby()
{
    m_gameMode = MODE_MULTIPLAYER;
    m_campaignRun = false;
    m_screen = SCR_MULTIPLAYER_LOBBY;
    m_paused = false;
    m_mpTypingAddr = false;
    m_mpTypingName = false;
    sfx(SND_UI_SELECT, 0.6f);
    if (!m_net->isConnected()) {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, 7777);
        if (probe.waitForConnected(80)) {
            probe.disconnectFromHost();
            m_mpInputAddr = "127.0.0.1:7777";
            pushFeed("LOCAL HOST FOUND ON 7777 - CLICK [J] JOIN", 4.0f);
        } else {
            m_net->startHost(7777, m_mpPlayerName);
        }
    }
}

void MainWindow::hostMultiplayer()
{
    m_net->startHost(7777, m_mpPlayerName);
    pushFeed("HOSTING SECTOR WAR ON PORT 7777", 3.5f);
    sfx(SND_UI_SELECT, 0.6f);
}

void MainWindow::joinMultiplayer()
{
    QString target = m_mpInputAddr.trimmed();
    QString host = target;
    quint16 port = 7777;
    int col = target.indexOf(':');
    if (col >= 0) {
        host = target.left(col);
        port = quint16(target.mid(col + 1).toUShort());
        if (port == 0) port = 7777;
    }
    m_net->connectToHost(host, port, m_mpPlayerName);
    pushFeed("CONNECTING TO " + target, 3.5f);
    sfx(SND_UI_SELECT, 0.6f);
}

void MainWindow::startMultiplayerMatch()
{
    if (m_screen == SCR_PLAYING) return;

    m_mapChoice = m_net ? m_net->settings().mapChoice : 6;
    loadMapGeometry(m_mapChoice);
    m_screen = SCR_PLAYING;
    m_paused = false;
    m_campaignRun = false;
    m_gameMode = MODE_MULTIPLAYER;
    m_gs = GameState{};
    m_gs.mapChoice = m_mapChoice;
    m_gs.level = 1;
    m_gs.enemies.clear();
    m_gs.actors.clear();
    m_loadedMap = m_mapChoice;

    if (m_net->isHost()) {
        m_net->startMatch();
    }
    m_net->resetRound(m_gs, *this, m_parsed[m_mapChoice]);

    Player &pl = m_gs.player;
    pl.footZ = floorH(pl.posX, pl.posY);
    pl.velZ = 0.f; pl.velX = 0.f; pl.velY = 0.f;
    pl.onGround = true;
    pl.planeX = -pl.dirY * m_planeLen;
    pl.planeY = pl.dirX * m_planeLen;
    computeReachable(pl.posX, pl.posY);

    for (auto &a : m_gs.actors) {
        a.footZ = floorH(a.posX, a.posY);
        a.velZ = 0.f; a.velX = 0.f; a.velY = 0.f;
        a.onGround = true;
        a.planeX = -a.dirY * m_planeLen;
        a.planeY = a.dirX * m_planeLen;
    }

    CharClass myCls = CLS_ASSAULT;
    for (const auto &s : m_net->playerSlots()) {
        if (s.id == m_net->localPlayerId()) {
            myCls = s.cls;
            break;
        }
    }
    m_gs.charClass = myCls;
    initClassLoadout(m_gs.player, myCls);

    m_keys.clear();
    m_lmb = false;
    m_jumpQueued = false;
    m_tpp = false;
    m_boomT = 0.f;
    m_boomCur = 0.f;
    m_shoulderT = 1.f;
    m_scopeMag = 4.f;
    m_adsT = 0.f;
    m_adsFOVMult = 1.f;
    m_prevAdsT = 0.f;
    m_pitch = 0.f;
    m_recoilPitch = 0.f;
    m_aimX = W / 2.f;
    m_aimY = H / 2.f;
    pl.reloadTimer = 0.f;
    pl.switchTimer = 0.f;
    pl.attackCooldown = 0.f;
    pl.adsMode = false;
    pl.sprinting = false;

    m_combo = 0; m_comboTimer = 0; m_comboDisplay = 0;
    m_hitMarker = m_headMarker = m_killMarker = m_headshotBanner = 0; m_damageDirTimer = 0; m_damageMarks.clear();
    m_killerIdx = -1; m_kcCount = 0; m_kcHead = 0; m_killcamActive = false;
    m_killFeed.clear(); m_casings.clear();
    m_emergencyAmmoCooldown = 0.f; m_emergencyHealthCooldown = 0.f;

    setFocus();
    QCursor::setPos(mapToGlobal(QPoint(width() / 2, height() / 2)));
    setCursor(Qt::BlankCursor);

    updateAmbience();
    pushFeed("SECTOR WAR INITIATED - ELIMINATE ENEMY OPERATIVES!", 4.5f, true);
    sfx(SND_DEPLOY, 0.9f);
}


// ─────────────────────────────────────────────────────────────────────────────
//  initGame / loadLevel
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::initGame()
{
    m_gameMode = m_campaignRun ? MODE_CAMPAIGN : MODE_FREEPLAY;
    if (m_net) m_net->disconnectSession();
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
    for (int w = 0; w < WPN_COUNT; w++) { pl.owned[w] = false; pl.mag[w] = 0; pl.reserve[w] = 0; pl.load[w] = Loadout{}; }
    pl.slot[SLOT_MELEE] = WPN_KNIFE; pl.slot[SLOT_SIDEARM] = WPN_PISTOL; pl.slot[SLOT_PRIMARY] = -1; pl.slot[SLOT_PRIMARY2] = -1;
    pl.throwCount[TH_FRAG] = 2; pl.throwCount[TH_SMOKE] = 1; pl.throwCount[TH_FLASH] = 1; pl.throwCount[TH_MOLOTOV] = 0; pl.throwCount[TH_MINE] = 0;
    pl.throwSel = TH_FRAG; pl.gadget = GD_RADAR; pl.gadgetCd = 0.f; pl.heat = 0.f; pl.overheatT = 0.f; pl.charge = 0.f; pl.burstLeft = 0;
    m_charging = m_cooking = m_shellReload = m_grappling = m_staminaExhausted = false; m_bloom = 0.f; m_radarT = m_flashT = 0.f; m_inspectT = 0.f;
    m_upgradeCards.clear();
    m_selectedUpgradeCard = -1;
    m_upgradePicked = false;
    m_showArmory = false;
    pl.owned[WPN_KNIFE] = true;
    pl.owned[WPN_PISTOL] = true;
    pl.curWeapon = WPN_PISTOL;
    pl.mag[WPN_KNIFE] = 0;
    pl.reserve[WPN_KNIFE] = 0;
    pl.mag[WPN_PISTOL] = effectiveMagSize(WPN_PISTOL);
    pl.reserve[WPN_PISTOL] = cp.ammo * 2;
    m_knifeSwing = 0;

    m_combo = 0; m_comboTimer = 0; m_comboDisplay = 0;
    m_pitch = 0; m_recoilPitch = 0; m_aimX = W / 2.f; m_aimY = H / 2.f; m_adsT = 0; m_adsFOVMult = 1;
    m_hitMarker = m_headMarker = m_killMarker = m_headshotBanner = 0; m_damageDirTimer = 0; m_damageMarks.clear();
    m_killerIdx = -1; m_kcCount = 0; m_kcHead = 0; m_killcamActive = false;
    
    pl.velX = 0.f;
    pl.velY = 0.f;
    m_gs.player.velZ = 0;
    m_gs.player.onGround = true;
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
void MainWindow::startDebugMode()
{
    m_mapChoice = 0; // Dungeon map (has doors, secret walls, acid vault)
    m_charChoice = 0;
    m_difficulty = 1;
    initGame();
    Player &pl = m_gs.player;
    pl.hp = 9999;
    Loadout good; good.rarity = 3;
    giveWeapon(WPN_RIFLE, good, -1, WEAPONS[WPN_RIFLE].maxReserve, false);
    giveWeapon(WPN_SHOTGUN, good, -1, WEAPONS[WPN_SHOTGUN].maxReserve, false);
    for (int k = 0; k < TH_COUNT; k++) pl.throwCount[k] = THROWS[k].maxCarry;
    // every other gun laid out on open floor near the spawn (breadth-first over
    // reachable cells): walk over one to take it, [E] to swap when the slots are full
    {
        std::vector<std::pair<float, float>> spots;
        std::vector<uint8_t> seen(size_t(m_mapW) * m_mapH, 0);
        std::vector<int> q{int(pl.posY) * m_mapW + int(pl.posX)};
        seen[size_t(q[0])] = 1;
        for (size_t qi = 0; qi < q.size() && spots.size() < WPN_COUNT; qi++) {
            int cx = q[qi] % m_mapW, cy = q[qi] / m_mapW;
            float x = cx + 0.5f, y = cy + 0.5f;
            float d2 = (x - pl.posX) * (x - pl.posX) + (y - pl.posY) * (y - pl.posY);
            if (d2 > 1.2f && !m_acid[cy][cx] && std::fabs(floorH(x, y) - pl.footZ) < 0.3f && posValid(x, y, 0.3f)) spots.push_back({x, y});
            const int dd[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto &o : dd) {
                int nx = cx + o[0], ny = cy + o[1];
                if (!inside(nx, ny) || seen[size_t(ny) * m_mapW + nx] || solidCell(nx, ny)) continue;
                seen[size_t(ny) * m_mapW + nx] = 1; q.push_back(ny * m_mapW + nx);
            }
        }
        size_t n = 0;
        for (int w = 1; w < WPN_COUNT && n < spots.size(); w++) {
            if (pl.owned[w]) continue;
            Loadout L; L.rarity = uint8_t(n % RARITY_COUNT);
            spawnWeaponPickup(spots[n].first, spots[n].second, w, L, false);
            n++;
        }
        for (int k = 0; k < TH_COUNT && n < spots.size(); k++, n++) {
            WeaponPickup th; th.x = spots[n].first; th.y = spots[n].second; th.type = PICKUP_THROW; th.throwKind = k;
            m_gs.weaponPickups.push_back(th);
        }
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
    pushFeed("EVERY GUN IS ON THE FLOOR - WALK OVER IT, [E] TO SWAP", 6.0f);
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
    m_gs.zoneKills = m_gs.zoneHeadshots = m_gs.zoneShots = m_gs.zoneHits = m_gs.zoneDamage = m_gs.zoneSecrets = 0;
    m_gs.zoneTime = 0; m_gs.zoneHadBoss = m_gs.zoneBossKilled = false;
    m_ixPage = 0;
    int nEnemies = cfg.numEnemies + (lv > 1 ? (lv - 1) * 3 : 0);
    m_gs.killsNeeded = std::min(nEnemies, 4 + (lv - 1) * 2);
    if (m_campaignRun) { static const float DM[3] = { 0.75f, 1.f, 1.3f }; nEnemies = std::max(1, int(MISSIONS[m_mission].enemies * DM[rs::clampi(m_difficulty, 0, 2)] + 0.5f)); }

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
        pl.hp = std::min(pl.hp + 35, maxHP());
        for (int w = 0; w < WPN_COUNT; w++) {
            if (pl.owned[w] && usesAmmo(w)) {
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

    m_gs.player.footZ = floorH(pl.posX, pl.posY);
    m_gs.player.velZ = 0;
    m_gs.player.onGround = true;

    pl.dirX = 1.f;
    pl.dirY = 0.f;
    
    pl.planeX = 0.f; pl.planeY = m_planeLen;
    pl.shakeY = 0;  pl.adsMode = false; m_staminaExhausted = false;
    computeReachable(pl.posX, pl.posY);
    spawnSquad();
    pl.reloadTimer = 0; pl.switchTimer = 0;
    m_pitch = 0; m_aimX = W / 2.f; m_aimY = H / 2.f;

    // Enemies: Hybrid Tactical Spawning
    m_gs.enemies.clear();
    std::vector<Marker> spawns, kits;
    for (const Marker &mk : pm.markers) { if (mk.kind == 'e') spawns.push_back(mk); if (mk.kind == 'k') kits.push_back(mk); }
    for (size_t i = spawns.size(); i > 1; i--) std::swap(spawns[i - 1], spawns[size_t(rand()) % i]);
    for (size_t i = kits.size(); i > 1; i--) std::swap(kits[i - 1], kits[size_t(rand()) % i]);

    // Gather valid walkable floor tiles away from player spawn (> 8 units), reachable without
    // opening secrets and not inside pits (enemies spawned there could never be reached / escape)
    computeReachable(pl.posX, pl.posY);
    std::vector<std::pair<float, float>> validFloorTiles;
    for (int y = 1; y < m_mapH - 1; y++) {
        for (int x = 1; x < m_mapW - 1; x++) {
            if (m_map[y][x] == CELL_FLOOR && !m_acid[y][x] && reachable(x, y) && m_plat[y][x] > -0.1f) {
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

    // markers behind secret walls (or cut off in an edited map) aren't used for regular troops
    spawns.erase(std::remove_if(spawns.begin(), spawns.end(), [&](const Marker &mk) { return !reachable(int(mk.x), int(mk.y)); }), spawns.end());
    // 1) Fixed tactical choke/ambush markers (up to 50% of spawns)
    int fixedCount = std::min(int(spawns.size()), nEnemies / 2);
    for (int i = 0; i < fixedCount; i++) {
        EnemyType et = m_campaignRun ? missionEnemyType() : rollEnemyType(lv, false);
        spawnEnemy(spawns[i].x, spawns[i].y, false, et);
    }

    // 2) Dynamic tactical positions for remaining enemies
    int dynamicCount = nEnemies - fixedCount;
    int floorIdx = 0;
    for (int i = 0; i < dynamicCount && floorIdx < int(validFloorTiles.size()); i++, floorIdx++) {
        float fx = validFloorTiles[floorIdx].first;
        float fy = validFloorTiles[floorIdx].second;
        bool elevated = (floorH(fx, fy) > 0.2f);
        EnemyType et = m_campaignRun ? missionEnemyType() : rollEnemyType(lv, elevated);
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
    // Zone commander: a boss on the far side of the map
    {
        float bestD = -1, bx = 0, by = 0;
        for (const auto &t : validFloorTiles) {
            float dsq = (t.first - pl.posX) * (t.first - pl.posX) + (t.second - pl.posY) * (t.second - pl.posY);
            if (dsq > bestD && posValid(t.first, t.second, 0.4f)) { bestD = dsq; bx = t.first; by = t.second; }
        }
        if (bestD > 0) {
            // the zone commander is one of the story bosses, in campaign order
            static const int ROTATION[9] = { BK_WARDEN, BK_TWIN_A, BK_SEED_QUEEN, BK_GUNSHIP, BK_GUARD_HEAVY, BK_MIRAGE, BK_ENGINEER, BK_JUGGERNAUT, BK_FOUNDER };
            int kind = ROTATION[(lv - 1) % 9];
            // a second far tile (the other twin), well away from the first
            float fx2 = bx, fy2 = by, best2 = -1;
            for (const auto &t : validFloorTiles) {
                float d1 = (t.first - bx) * (t.first - bx) + (t.second - by) * (t.second - by);
                float d2 = (t.first - pl.posX) * (t.first - pl.posX) + (t.second - pl.posY) * (t.second - pl.posY);
                if (d2 > 64.f && d1 > best2 && d1 < 26.f * 26.f && posValid(t.first, t.second, 0.4f)) { best2 = d1; fx2 = t.first; fy2 = t.second; }
            }
            if (!m_campaignRun) spawnZoneBoss(kind, bx, by, fx2, fy2);
            m_gs.zoneHadBoss = true;
        }
    }
    m_seeds.clear(); m_overloadCells.clear(); m_overloadPhase = 9.f;
    m_baseAcid = m_acid.v;
    m_curHazardName = hazardNameAt(-1, -1);
    m_curHazardColor = hazardColorAt(-1, -1);
    // The zone is cleared only when every enemy is dead (vault guards and the commander included)
    m_gs.killsNeeded = int(m_gs.enemies.size());
    m_reinforceTimer = frand(14.f, 22.f);
    m_reinforceLeft = lv >= 2 ? std::min(1 + lv, 5) : 0;

    // Pickups placed by the level designer (medkit count scales with difficulty)
    for (int i = 0; i < std::min(dc.numKits, int(kits.size())); i++) {
        HealthKit k; k.x = kits[i].x; k.y = kits[i].y; k.active = true; k.bobPhase = float(i) * 1.2f;
        m_gs.healthKits.push_back(k);
    }
    m_gs.secretsTotal = 0;
    m_secretHinted.clear();
    for (int y = 0; y < m_mapH; y++) for (int x = 0; x < m_mapW; x++) if (m_map[y][x] == CELL_SECRET) m_gs.secretsTotal++;
    m_gs.zoneSecretsTotal = m_gs.secretsTotal;
    for (const Marker &mk : pm.markers) {
        WeaponPickup wp; wp.x = mk.x; wp.y = mk.y; wp.bobPhase = frand(0, 6.28f);
        if (mk.kind == 'a') wp.type = PICKUP_AMMO;
        else if (mk.kind == 'r') { wp.type = PICKUP_WEAPON; wp.weapon = WPN_RIFLE; }
        else if (mk.kind == 's') { wp.type = PICKUP_WEAPON; wp.weapon = WPN_SHOTGUN; }
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

    bool captured = m_screen == SCR_PLAYING && !m_paused && !m_gs.gameOver && !m_gs.levelComplete && !m_showArmory && hasFocus();
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
        if (m_screen != SCR_SETTINGS || m_settingsReturnScreen != SCR_PLAYING) {
            m_menuCamAng += dt * 0.10f;
            if (m_loadedMap != m_mapChoice) loadMapGeometry(m_mapChoice);
        }
    }
    if (playing && m_flyActive) {
        // intro flyby: the world holds still while the camera travels
        m_flyT += dt;
        if (m_flyT >= m_flyDur) m_flyActive = false;
        updateEnvironment(dt);
        return;
    }
    if (playing && !m_paused && !m_gs.gameOver) gameStep(dt);
    if (playing && m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
        if (m_net->isHost()) {
            m_net->tickServer(dt, m_gs, *this, m_parsed[m_gs.mapChoice]);
        } else {
            m_net->tickClient(dt, m_gs, *this);
        }
        m_net->checkObjectiveInteractions(m_gs);
    }
    if (playing && m_campaignRun && m_gs.gameOver) updateRadio(dt);


    // ADS blend + FOV
    bool canAim = playing && pl.adsMode && !isMelee(pl.curWeapon) && pl.curWeapon >= 0 && pl.reloadTimer <= 0 && pl.switchTimer <= 0 && !pl.sprinting && !m_gs.gameOver && !m_gs.levelComplete;
    float adsRate = 12.f;
    if (pl.curWeapon >= 0) {
        WeaponClass c = WEAPONS[pl.curWeapon].cls;
        adsRate = c == WC_SMG || c == WC_PISTOL ? 18.f : c == WC_LMG || c == WC_SNIPER || c == WC_LAUNCHER ? 8.f : 12.f;
        if (pl.load[pl.curWeapon].att[AT_STOCK] == 1) adsRate *= 1.4f;
    }
    m_adsT += ((canAim ? 1.f : 0.f) - m_adsT) * std::min(1.f, dt * adsRate);
    float adsFov = effectiveScope(pl.curWeapon) ? 1.f / m_scopeMag : effectiveAdsFov(pl.curWeapon);
    m_adsFOVMult = 1.f + (adsFov - 1.f) * m_adsT;
    if (m_prevAdsT < 0.5f && m_adsT >= 0.5f) sfx(SND_SCOPE_IN, effectiveScope(pl.curWeapon) ? 0.5f : 0.25f);
    m_prevAdsT = m_adsT;
    if (playing) {
        float dirLen = std::sqrt(pl.dirX * pl.dirX + pl.dirY * pl.dirY);
        if (dirLen > 1e-4f) { pl.dirX /= dirLen; pl.dirY /= dirLen; }
        else { pl.dirX = 1.f; pl.dirY = 0.f; }
        pl.planeX = -pl.dirY * m_planeLen;
        pl.planeY = pl.dirX * m_planeLen;
        updateCamera(dt);
    }
    updateEnvironment(dt);
    m_edMsgT = std::max(0.f, m_edMsgT - dt);

    // Sound timers, heartbeat at low health, game-over sting
    m_hurtSndT -= dt; m_alertSndT -= dt;
    if (playing && !m_paused && !m_gs.gameOver && !m_gs.levelComplete &&
        pl.hp <= std::max(25, maxHP() / 4)) {
        m_heartbeatT -= dt;
        if (m_heartbeatT <= 0) { sfx(SND_HEARTBEAT, 0.8f); m_heartbeatT = 0.85f; }
    }
    if (playing && m_gs.gameOver && !m_prevGameOver) { sfx(SND_GAME_OVER, 0.8f); startKillcam(); }
    updateKillcam(dt);
    m_prevGameOver = playing && m_gs.gameOver;

    // Aiming down sights pulls the free crosshair back to the centre (the
    // camera absorbs the offset, so the aimed-at point stays put)
    if (playing && canAim) {
        float f = std::min(1.f, dt * 14.f);
        float ox = (m_aimX - W / 2.f) * f, oy = (m_aimY - H / 2.f) * f;
        turn(std::atan(2.f * ox / W * m_planeLen * m_adsFOVMult));
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
    bool movingKeys = isPlayerMoving();
    m_breath += dt * (m_holdBreath ? 0.35f : 1.25f);
    float amp = m_holdBreath ? 0.8f : (movingKeys ? 9.f : 4.5f);
    m_scopeSwayX += (std::sin(m_breath * 0.9f) * amp - m_scopeSwayX) * std::min(1.f, dt * 4.f);
    m_scopeSwayY += (std::sin(m_breath * 1.8f) * amp * 0.6f - m_scopeSwayY) * std::min(1.f, dt * 4.f);

    // Recoil / animation decay
    m_kick *= std::exp(-dt * 9.f);
    m_camKick *= std::exp(-dt * 14.f);
    m_actionT = std::max(0.f, m_actionT - dt * (pl.curWeapon == WPN_SHOTGUN ? 1.4f : WEAPONS[std::max(0, pl.curWeapon)].mode == FM_BOLT ? 1.2f : pl.curWeapon == WPN_AXE ? 2.6f : (isMelee(pl.curWeapon) ? 4.8f : 9.f)));
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
    for (DamageMark &dm : m_damageMarks) dm.t -= dt;
    m_damageMarks.erase(std::remove_if(m_damageMarks.begin(), m_damageMarks.end(), [](const DamageMark &d) { return d.t <= 0; }), m_damageMarks.end());
    m_comboDisplay   = std::max(0.f, m_comboDisplay - dt);
    if (m_comboTimer > 0) { m_comboTimer -= dt; if (m_comboTimer <= 0) m_combo = 0; }
    for (auto &k : m_killFeed) k.life -= dt;
    m_killFeed.erase(std::remove_if(m_killFeed.begin(), m_killFeed.end(), [](const KFEntry &k) { return k.life <= 0; }), m_killFeed.end());
    for (auto &en : m_gs.enemies) if (en.hpShow > 0) en.hpShow -= dt;

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
        // melee reach is measured from the player, not the (possibly third-person) camera
        m_probeDepth = h.sky ? 1e9f : std::sqrt((h.x - pl.posX) * (h.x - pl.posX) + (h.y - pl.posY) * (h.y - pl.posY));

        // Proximity detection for secret walls and doors in front of the player
        int cand[12][2] = {
            {int(pl.posX + pl.dirX * 0.7f), int(pl.posY + pl.dirY * 0.7f)},
            {int(pl.posX + pl.dirX * 1.3f), int(pl.posY + pl.dirY * 1.3f)},
            {int(pl.posX + pl.dirX * 1.9f), int(pl.posY + pl.dirY * 1.9f)},
            {int(pl.posX + pl.dirX * 2.5f), int(pl.posY + pl.dirY * 2.5f)},
            {int(pl.posX + pl.dirX * 3.1f), int(pl.posY + pl.dirY * 3.1f)},
            {int(pl.posX) + 1, int(pl.posY)}, {int(pl.posX) - 1, int(pl.posY)},
            {int(pl.posX), int(pl.posY) + 1}, {int(pl.posX), int(pl.posY) - 1},
            {int(pl.posX) + 2, int(pl.posY)}, {int(pl.posX) - 2, int(pl.posY)},
            {int(pl.posX), int(pl.posY) + 2}
        };
        for (auto &c : cand) {
            int nx = c[0], ny = c[1];
            if (nx > 0 && ny > 0 && nx < m_mapW - 1 && ny < m_mapH - 1) {
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

// ─────────────────────────────────────────────────────────────────────────────
//  Sound helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::enableAudio()
{
    if (m_snd && !m_snd->available()) m_snd->start();
    m_ambId = -1;
    updateAmbience();
}

void MainWindow::sfx(SoundId id, float gain, float pitch, float delay)
{
    if (m_snd) m_snd->play(id, gain, 0.f, pitch, delay);
}

// Positional sound: stereo pan from the direction relative to the view,
// distance fall-off, and muffling when a wall blocks the line of sight.
void MainWindow::sfxAt(SoundId id, float wx, float wy, float gain, float pitch, float delay)
{
    if (!m_snd || !m_snd->available()) return;
    const Player &pl = m_gs.player;
    float dx = wx - pl.posX, dy = wy - pl.posY, d = std::sqrt(dx * dx + dy * dy);
    float fl = std::sqrt(pl.dirX * pl.dirX + pl.dirY * pl.dirY) + 1e-6f;
    float rx = -pl.dirY / fl, ry = pl.dirX / fl;                 // screen-right in world space
    float pan = d > 0.05f ? rs::clampf((dx * rx + dy * ry) / d, -1.f, 1.f) * 0.85f : 0.f;
    float att = 1.f / (1.f + 0.035f * std::pow(d, 1.6f));
    if (d > 1.5f && !hasLOS(wx, wy, floorH(wx, wy) + 0.6f, pl.posX, pl.posY, m_gs.player.footZ + 0.5f)) att *= 0.5f;
    m_snd->play(id, gain * att, pan, pitch, delay);
}

void MainWindow::updateAmbience()
{
    if (!m_snd) return;
    // drone underground / in the facility, wind outdoors, low drone in the swamp and ruins
    int want = (m_theme == BIO_VOLCANIC || m_theme == BIO_FACILITY) ? SND_AMB_DUNGEON : (m_theme == BIO_DESERT || m_theme == BIO_SNOW) ? SND_AMB_WIND : SND_AMB_CRYPT;
    if (want == m_ambId) return;
    m_snd->setAmbience(want, 0.32f);
    m_ambId = want;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Intermission between zones: pick one upgrade + the weapon to deploy with
// ─────────────────────────────────────────────────────────────────────────────
std::vector<int> MainWindow::loadoutChoices() const
{
    std::vector<int> v;
    int unlock = -1;
    if (m_selectedUpgradeCard >= 0 && m_selectedUpgradeCard < int(m_upgradeCards.size()) && m_upgradeCards[m_selectedUpgradeCard].slot == UC_UNLOCK)
        unlock = m_upgradeCards[m_selectedUpgradeCard].weapon;
    for (int s = 0; s < SLOT_COUNT; s++) { int w = m_gs.player.slot[s]; if (w >= 0) v.push_back(w); }
    if (unlock >= 0 && std::find(v.begin(), v.end(), unlock) == v.end()) v.push_back(unlock);
    return v;
}
QRect MainWindow::ixCardRect(int i) const
{
    const int cw = 232, ch = 248, sp = 18, x0 = (W - (3 * cw + 2 * sp)) / 2;
    return QRect(x0 + i * (cw + sp), 118, cw, ch);
}
QRect MainWindow::ixChipRect(int slot, int n) const
{
    const int cw = 150, sp = 10, tot = n * cw + (n - 1) * sp, x0 = (W - tot) / 2;
    return QRect(x0 + slot * (cw + sp), 392, cw, 48);
}
QRect MainWindow::ixDeployRect() const { return QRect((W - 270) / 2, 456, 270, 38); }

void MainWindow::updateIntermissionHover(const QPointF &widgetPos)
{
    QPointF f = widgetToFrame(widgetPos);
    QPoint p(int(f.x()), int(f.y()));
    int oldCard = m_hoverCard, oldChip = m_hoverChip;
    m_hoverCard = -1; m_hoverChip = -1;
    for (int i = 0; i < int(m_upgradeCards.size()) && i < 3; i++) if (ixCardRect(i).contains(p)) m_hoverCard = i;
    std::vector<int> ch = loadoutChoices();
    for (int s = 0; s < int(ch.size()); s++) if (ixChipRect(s, int(ch.size())).contains(p)) m_hoverChip = s;
    m_hoverDeploy = ixDeployRect().contains(p);
    if ((m_hoverCard != oldCard && m_hoverCard >= 0) || (m_hoverChip != oldChip && m_hoverChip >= 0)) sfx(SND_UI_MOVE, 0.5f);
}

void MainWindow::deployNextZone()
{
    Player &pl = m_gs.player;
    int idx = m_selectedUpgradeCard >= 0 ? m_selectedUpgradeCard : 0;
    QString title;
    if (idx < int(m_upgradeCards.size())) {
        applyUpgrade(m_upgradeCards[idx]);
        title = m_upgradeCards[idx].title;
    }
    int lw = m_loadoutWeapon;
    loadLevel(m_gs.level + 1);
    if (lw >= 0 && lw < WPN_COUNT && pl.owned[lw]) {
        pl.curWeapon = lw; m_pendingWeapon = -1; pl.switchTimer = 0.4f; pl.reloadTimer = 0;
        if (effectiveScope(lw)) m_scopeMag = rs::clampf(1.f / effectiveAdsFov(lw), 2.f, 8.f);
        if (usesAmmo(lw) && pl.mag[lw] == 0) {                     // arrive with a loaded weapon
            int take = std::min(effectiveMagSize(lw), pl.reserve[lw]);
            pl.mag[lw] += take; pl.reserve[lw] -= take;
        }
    }
    if (!title.isEmpty()) pushFeed("UPGRADE EQUIPPED: " + title, 4.f, true);
    pushFeed(QString("ZONE %1 - DEPLOYED WITH %2").arg(m_gs.level).arg(WEAPONS[pl.curWeapon].shortName), 4.f);
    sfx(SND_DEPLOY, 0.7f);
}

void MainWindow::addDamageMark(float srcX, float srcY, float strength)
{
    // merge with a mark from nearly the same direction, else add (oldest drops off)
    const Player &pl = m_gs.player;
    float a = std::atan2(srcY - pl.posY, srcX - pl.posX);
    for (DamageMark &d : m_damageMarks) {
        float b = std::atan2(d.y - pl.posY, d.x - pl.posX);
        if (std::fabs(std::remainder(a - b, 6.2831853f)) < 0.25f) { d.x = srcX; d.y = srcY; d.t = 2.2f; d.strength = std::min(1.f, d.strength + strength * 0.5f); return; }
    }
    if (m_damageMarks.size() >= 6) m_damageMarks.erase(m_damageMarks.begin());
    m_damageMarks.push_back(DamageMark{srcX, srcY, 2.2f, std::max(0.35f, strength)});
}

int MainWindow::aliveEnemies() const
{
    int n = 0;
    for (const Enemy &e : m_gs.enemies) if (e.alive) n++;
    return n;
}

// Picking a card also picks the gun it is for, so that gun is in your hands on deploy
void MainWindow::selectUpgradeCard(int i)
{
    if (i < 0 || i >= int(m_upgradeCards.size())) return;
    m_selectedUpgradeCard = i; m_upgradePicked = true;
    const UpgradeCard &c = m_upgradeCards[i];
    if (c.weapon >= 0 && c.weapon < WPN_COUNT && !isMelee(c.weapon)) m_loadoutWeapon = c.weapon;
    else {
        std::vector<int> ch = loadoutChoices();
        if (std::find(ch.begin(), ch.end(), m_loadoutWeapon) == ch.end()) m_loadoutWeapon = m_gs.player.curWeapon;
    }
    sfx(SND_UPGRADE, 0.6f);
}

QRect MainWindow::ixShopRect(int i) const { return QRect(W / 2 - 312 + i * 212, 552, 200, 24); }

void MainWindow::buyShopItem(int i)
{
    static const int cost[3] = { 40, 35, 25 };
    if (i < 0 || i > 2) return;
    if (m_gs.coins < cost[i]) { sfx(SND_UI_BACK, 0.6f); m_ixMsg = "NOT ENOUGH COINS"; m_ixMsgT = 2.f; return; }
    Player &pl = m_gs.player;
    m_gs.coins -= cost[i];
    if (i == 0) {
        for (int w = 0; w < WPN_COUNT; w++) if (pl.owned[w] && usesAmmo(w))
            pl.reserve[w] = std::min(pl.reserve[w] + WEAPONS[w].magSize * 2, WEAPONS[w].maxReserve);
        pl.throwCount[TH_FRAG] = std::min(THROWS[TH_FRAG].maxCarry, pl.throwCount[TH_FRAG] + 1);
        sfx(SND_PICKUP_AMMO, 0.7f); m_ixMsg = "AMMO RESUPPLIED  +2 MAGAZINES PER GUN";
    } else if (i == 1) {
        pl.hp = std::min(pl.hp + 50, maxHP());
        sfx(SND_PICKUP_HEALTH, 0.7f); m_ixMsg = "MEDKIT USED  +50 HP";
    } else {
        generateUpgradeChoices();
        m_selectedUpgradeCard = -1;
        sfx(SND_UPGRADE, 0.6f); m_ixMsg = "NEW UPGRADE CARDS";
    }
    m_ixMsgT = 2.f;
}

// Zone report: kills, headshots and bonus objectives pay out coins and score
void MainWindow::buildZoneReport()
{
    const GameState &g = m_gs;
    m_report.clear();
    const int lv = g.level;
    auto add = [&](const QString &label, const QString &detail, bool done, int coins, int score) {
        m_report.push_back(ReportItem{label, detail, done, done ? coins : 0, done ? score : 0});
    };
    auto mmss = [](float s) { int t = int(s); return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0')); };
    add("HOSTILES ELIMINATED", QString("%1 kills  x 5 coins").arg(g.zoneKills), g.zoneKills > 0, g.zoneKills * 5, 0);
    add("HEADSHOTS", QString("%1 headshots  x 10 coins").arg(g.zoneHeadshots), g.zoneHeadshots > 0, g.zoneHeadshots * 10, 0);
    float hsRate = g.zoneKills ? float(g.zoneHeadshots) / g.zoneKills : 0.f;
    add("HEADHUNTER", QString("%1% of kills were headshots  (need 40%)").arg(int(hsRate * 100)), hsRate >= 0.4f, 40, 400 * lv);
    float acc = g.zoneShots ? float(g.zoneHits) / g.zoneShots : 0.f;
    add("MARKSMAN", QString("accuracy %1%  (need 35%)").arg(int(acc * 100)), acc >= 0.35f && g.zoneShots >= 5, 30, 300 * lv);
    if (g.zoneSecretsTotal > 0)
        add("EXPLORER", QString("%1 / %2 secret areas found").arg(g.zoneSecrets).arg(g.zoneSecretsTotal), g.zoneSecrets >= g.zoneSecretsTotal, 50, 500 * lv);
    add("UNTOUCHABLE", QString("%1 damage taken  (need under 40)").arg(g.zoneDamage), g.zoneDamage < 40, 40, 400 * lv);
    float par = 150.f + 30.f * lv;
    add("BLITZ", QString("cleared in %1  (par %2)").arg(mmss(g.zoneTime)).arg(mmss(par)), g.zoneTime <= par, 30, 300 * lv);
    if (g.zoneHadBoss) add("COMMANDER DOWN", "zone commander eliminated", g.zoneBossKilled, 60, 600 * lv);
    m_reportCoins = m_reportScore = 0;
    for (const ReportItem &it : m_report) { m_reportCoins += it.coins; m_reportScore += it.score; }
    m_gs.coins += m_reportCoins;
    m_gs.score += m_reportScore;
}

void MainWindow::gameStep(float dt)
{
    Player &pl = m_gs.player;
    m_gs.frameNum++;
    if (m_gs.levelComplete) {
        // The player decides at their own pace; the long fallback timer only
        // exists so an idle game still moves on.
        m_intermissionT += dt;
        m_ixMsgT = std::max(0.f, m_ixMsgT - dt);
        if (m_ixPage == 0) {
            if (m_intermissionT > 30.f) { m_ixPage = 1; m_ixPageStart = m_intermissionT; }   // idle: move on
        } else {
            m_upgradeDecisionTimer -= dt;
            if (m_upgradeDecisionTimer <= 0.0f) deployNextZone();
        }
        return;
    }
    m_gs.zoneTime += dt;
    recordKillcamFrame();
    if (pl.attackCooldown > 0) pl.attackCooldown -= dt;
    if (pl.flashTimer > 0)     pl.flashTimer -= dt;
    if (pl.damageFlash > 0)    pl.damageFlash -= dt;

    // Reload / weapon switch timers
    updateReload(dt);
    if (pl.switchTimer > 0) {
        float before = pl.switchTimer;
        pl.switchTimer -= dt;
        if (before > 0.2f && pl.switchTimer <= 0.2f && m_pendingWeapon >= 0) {
            pl.curWeapon = m_pendingWeapon; m_pendingWeapon = -1;
            if (effectiveScope(pl.curWeapon)) m_scopeMag = rs::clampf(1.f / effectiveAdsFov(pl.curWeapon), 2.f, 8.f);
        }
        if (pl.switchTimer < 0) pl.switchTimer = 0;
    }

    updateDoors(dt);

    // Keyboard turning
    float rotSpd = 2.2f * dt * (pl.adsMode ? 0.5f : 1.f);
    if (m_keys.count(Qt::Key_Left))  turn(-rotSpd);
    if (m_keys.count(Qt::Key_Right)) turn(rotSpd);

    // Sprint, stamina, breath-holding
    bool moving = isPlayerMoving();
    bool shift = isActionPressed(ACT_SPRINT);
    bool scoped = effectiveScope(pl.curWeapon) && m_adsT > 0.85f;

    // Player must release Shift to recover from stamina exhaustion
    if (!shift) {
        m_staminaExhausted = false;
    }

    bool canStartSprint = pl.sprinting ? (pl.stamina > 0.f) : (pl.stamina > 5.f);
    bool wantSprint = moving && shift && !m_staminaExhausted && canStartSprint && !pl.adsMode && !m_charging && !m_cooking;
    m_holdBreath = scoped && shift && !m_staminaExhausted && pl.stamina > 4.f;

    if (wantSprint) {
        pl.stamina -= 45.f * dt;
        if (pl.stamina <= 0.f) {
            pl.stamina = 0.f;
            m_staminaExhausted = true;
            wantSprint = false;
        }
    } else if (m_holdBreath) {
        pl.stamina -= 22.f * dt;
        if (pl.stamina <= 0.f) {
            pl.stamina = 0.f;
            m_staminaExhausted = true;
            m_holdBreath = false;
        }
    } else if (!shift) {
        pl.stamina += 28.f * dt;
    }
    pl.stamina = std::max(0.f, std::min(maxStamina(), pl.stamina));
    pl.sprinting = wantSprint;

    bool mpActive = (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive());
    if (mpActive && pl.hp <= 0) {
        if (m_mpRespawnCountdown < 0.f) {
            m_mpRespawnCountdown = 5.0f;
        }
        m_mpRespawnCountdown -= dt;
        pl.velX = pl.velY = pl.velZ = 0.f;
        pl.adsMode = false;
        if (m_mpRespawnCountdown <= 0.f) {
            m_mpRespawnCountdown = -1.f;
            m_net->handleRespawn(m_net->localPlayerId(), m_gs, *this, m_parsed[m_gs.mapChoice]);
            pushFeed("DEPLOYED TO BATTLEFIELD", 2.0f);
            QVariantMap rev; rev["id"] = m_net->localPlayerId();
            m_net->broadcastEvent(EV_RESPAWN, rev);
        }
    }

    // Movement: the keyboard becomes an InputCmd, moved by the same code as every actor
    InputCmd cmd;
    if (mpActive && pl.hp <= 0) {
        cmd.forward = 0.f;
        cmd.strafe  = 0.f;
        cmd.sprint  = false;
        cmd.jump    = false;
        cmd.fire    = false;
        cmd.ads     = false;
    } else {
        cmd.forward = float(isActionPressed(ACT_MOVE_FWD)) - float(isActionPressed(ACT_MOVE_BACK));
        cmd.strafe  = float(isActionPressed(ACT_MOVE_RIGHT)) - float(isActionPressed(ACT_MOVE_LEFT));
        cmd.sprint  = wantSprint;
        cmd.jump    = m_jumpQueued;
        cmd.ads     = pl.adsMode;
    }
    cmd.yaw     = std::atan2(pl.dirY, pl.dirX);
    cmd.seq     = m_gs.frameNum;
    m_jumpQueued = false;
    MoveEvents mv = stepMovement(*this, pl, cmd, dt);
    if (mv.jumped) sfx(SND_JUMP, 0.5f, frand(0.9f, 1.1f));
    if (mv.landed) {
        if (mv.landVel < -4.f) pl.shakeY += std::min(10.f, -mv.landVel * 1.2f);
        if (mv.landVel < -2.5f) sfx(SND_LAND, std::min(1.f, -mv.landVel * 0.12f), frand(0.9f, 1.1f));
    }
    float g = floorH(pl.posX, pl.posY);

    // Hazard burns/damages while standing in it
    int pcx = int(pl.posX), pcy = int(pl.posY);
    if (pcx >= 0 && pcx < m_mapW && pcy >= 0 && pcy < m_mapH && m_acid[pcy][pcx] && m_gs.player.footZ <= g + 0.05f) {
        m_curHazardName = hazardNameAt(pcx, pcy);
        m_curHazardColor = hazardColorAt(pcx, pcy);
        m_acidAcc += THEMES[m_theme].hazDps * dt * (1.f - pl.damageReduction * 0.5f);
        m_acidFlash = 1.f;
        m_acidSndT -= dt;
        if (m_acidSndT <= 0) { sfx(SND_ACID, 0.6f, frand(0.9f, 1.1f)); m_acidSndT = 0.35f; }
        if (m_hurtSndT <= 0) { sfx(SND_PLAYER_HURT, 0.45f, frand(0.95f, 1.05f)); m_hurtSndT = 1.0f; }
        if (m_acidAcc >= 1.f) { int d = int(m_acidAcc); m_acidAcc -= d; pl.hp -= d; m_gs.zoneDamage += d; pl.damageFlash = std::max(pl.damageFlash, 0.2f); }
        if (pl.hp <= 0) {
            pl.hp = 0; pl.adsMode = false;
            m_killerIdx = -3;
            if (mpActive) {
                m_net->handleDeathDrop(m_net->localPlayerId(), -1, false, m_gs);
            } else {
                m_gs.gameOver = true;
            }
        }
    }
    if (moving) pl.bobPhase += dt * (pl.sprinting ? 9.f : 5.5f); else pl.bobPhase *= 0.9f;
    // Footsteps: one per half bob cycle while walking on the ground
    if (moving && m_gs.player.onGround && int(pl.bobPhase / 3.14159f) != int(m_footstepPrev / 3.14159f))
        sfx(SND_STEP, pl.sprinting ? 0.55f : pl.adsMode ? 0.2f : 0.36f, frand(0.86f, 1.14f));
    m_footstepPrev = pl.bobPhase;

    // Trigger (auto / burst / charge / beam), throwables, pickups, area effects
    updateTrigger(dt);
    updateThrow(dt);
    updatePickups(dt);
    updateProjectiles(dt);
    updateAreaEffects(dt);
    updateStatusEffects(dt);

    checkEmergencyDrops(dt);

    updateActors(dt);
    updateEnemies(dt);
    updateBossWorld(dt);
    updateSecretHints(dt);
    checkReinforcements(dt);

    // Enemy projectiles
    const DiffConfig &dc = DIFF_CFG[m_difficulty];
    for (auto &b : m_gs.bullets) {
        if (!b.alive) continue;
        b.px = b.x; b.py = b.y; b.pz = b.z;
        b.x += b.dx * dt; b.y += b.dy * dt; b.z += b.dz * dt;
        int bx = int(std::floor(b.x)), by = int(std::floor(b.y));
        if (bx < 0 || bx >= m_mapW || by < 0 || by >= m_mapH || b.z < groundAt(b.x, b.y)) {
            b.alive = false; spawnImpact(b.px, b.py, b.pz, !solidCell(bx, by));
            sfxAt(SND_IMPACT, b.px, b.py, 0.5f, frand(0.9f, 1.2f));
            continue;
        }
        bool hitActor = false;
        for (int ai = 0; ai < int(m_gs.actors.size()) && !hitActor; ai++) {
            const Actor &a = m_gs.actors[size_t(ai)];
            if (!a.alive) continue;
            float ax = b.x - a.posX, ay = b.y - a.posY;
            if (ax * ax + ay * ay < 0.12f && b.z > a.footZ - 0.1f && b.z < a.footZ + 0.95f) {
                damageActor(ai, b.damage > 0 ? b.damage : 10, b.x - b.dx, b.y - b.dy);
                hitActor = true;
            }
        }
        if (hitActor) { b.alive = false; continue; }
        // closest point of this frame's path (prev -> now) to the player: fast rounds can't skip through
        auto closest = [&](float cx, float cy, float &ox, float &oy, float &oz) {
            float sx = b.x - b.px, sy = b.y - b.py, l2 = sx * sx + sy * sy;
            float t = l2 > 1e-6f ? rs::clampf(((cx - b.px) * sx + (cy - b.py) * sy) / l2, 0.f, 1.f) : 1.f;
            ox = b.px + sx * t; oy = b.py + sy * t; oz = b.pz + (b.z - b.pz) * t;
        };
        float cbx, cby, cbz;
        closest(pl.posX, pl.posY, cbx, cby, cbz);
        float dx = cbx - pl.posX, dy = cby - pl.posY;
        if (!b.whizzed && dx * dx + dy * dy < 1.6f) { b.whizzed = true; sfxAt(SND_BULLET_WHIZ, b.x, b.y, 0.9f, frand(0.9f, 1.15f)); }
        if (dx * dx + dy * dy < 0.15f && cbz > m_gs.player.footZ - 0.1f && cbz < m_gs.player.footZ + 0.95f) {
            b.alive = false;
            int dmg = b.damage > 0 ? b.damage : int((10 + 4 * (m_gs.level - 1)) * (1.f - pl.damageReduction) * dc.dmgReceivedMult);
            pl.hp -= dmg; m_gs.zoneDamage += dmg; pl.damageFlash = 0.45f; pl.shakeY = -9.f;
            sfx(SND_HIT_FLESH, 0.6f, frand(0.9f, 1.1f));
            if (m_hurtSndT <= 0) { sfx(SND_PLAYER_HURT, 0.85f, frand(0.93f, 1.07f)); m_hurtSndT = 0.35f; }
            m_damageAngle = std::atan2(-b.dy, -b.dx); m_damageDirTimer = 2.0f;
            if (pl.hp <= 0) {
                pl.hp = 0; pl.adsMode = false; m_killerIdx = b.shooter;
                if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
                    m_net->handleDeathDrop(m_net->localPlayerId(), b.shooter, false, m_gs);
                } else {
                    m_gs.gameOver = true;
                }
            }
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

    updateMission(dt);
    if (!m_campaignRun && !m_gs.levelComplete && !m_gs.enemies.empty() && aliveEnemies() == 0) {
        m_gs.levelComplete = true;
        m_upgradeDecisionTimer = 60.0f;
        m_intermissionT = 0.f;
        m_deployTimer = 0.0f;
        m_upgradePicked = false;
        m_selectedUpgradeCard = -1;
        m_hoverCard = m_hoverChip = -1;
        m_gs.score += 500 * m_gs.level;
        generateUpgradeChoices();
        buildZoneReport();
        m_ixPage = 0;
        // Stop combat input carrying over into the menu (held trigger, aim)
        m_lmb = false; pl.adsMode = false; m_showArmory = false;
        m_loadoutWeapon = pl.curWeapon;
        if (isMelee(m_loadoutWeapon))
            for (int s = SLOT_PRIMARY2; s >= SLOT_SIDEARM; s--) if (pl.slot[s] >= 0) { m_loadoutWeapon = pl.slot[s]; break; }
        sfx(SND_LEVEL_CLEAR, 0.8f);
        pushFeed("ZONE CLEARED - CHOOSE AN UPGRADE AND YOUR WEAPON", 5.0f, true);
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

        float ez = floorH(e.x, e.y) + 0.62f;
        float tgX, tgY, tgZ, d;
        if (m_gs.actors.empty()) { tgX = pl.posX; tgY = pl.posY; tgZ = pl.footZ + 0.5f; d = std::sqrt((tgX - e.x) * (tgX - e.x) + (tgY - e.y) * (tgY - e.y)); }
        else hostileTarget(e.x, e.y, ez, tgX, tgY, tgZ, d);
        float dx = tgX - e.x, dy = tgY - e.y;
        float detectRange = e.aggroRange * 1.5f;
        bool los = d < std::max(22.f, detectRange) && hasLOS(e.x, e.y, ez, tgX, tgY, tgZ) && !smokeBlocks(e.x, e.y, ez, tgX, tgY, tgZ);
        if (m_radarT > 0.f) e.hpShow = std::max(e.hpShow, 0.1f);
        if (e.stunT > 0.f) { e.moving = false; continue; }     // flashed / stunned: frozen
        // a loud (unsuppressed) shot nearby gets their attention
        if (e.state == ES_IDLE && m_noiseT > 0.f && d < detectRange * 1.6f) { e.state = ES_ALERT; e.alertDist = 0.f; }
        if (los && d < detectRange) {
            if (e.state != ES_ALERT) {
                // Initial target acquisition: alert bot with snappy reaction time
                e.state = ES_ALERT;
                if (m_alertSndT <= 0) { sfxAt(SND_ENEMY_ALERT, e.x, e.y, 0.9f, frand(0.88f, 1.12f)); m_alertSndT = 1.5f; }
                e.fireCooldown = frand(0.35f, 0.65f) * dc.fireCoolMult;
                e.strafeTimer = frand(2.0f, 3.8f);
                e.moveX = frand() < 0.5f ? -1.f : 1.f;
            }
            e.alertDist = 0.f; // Player detected! Pursue & engage
        }
        if (e.bossKind) { updateBoss(int(i), dt, tgX, tgY, tgZ, d, los); continue; }
        if (e.type >= ET_GRENADIER) { updateSpecialEnemy(int(i), dt, tgX, tgY, tgZ, d, los, detectRange); continue; }

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
            if (e.shockT > 0) spd *= 0.4f;    // shocked: sluggish
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
            enemyFire(e, tgX, tgY, tgZ);
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
    else if (type == ET_GRENADIER) hp = int(baseHp * 0.9f);
    else if (type == ET_SHIELD) hp = int(baseHp * 1.2f);
    else if (type == ET_MEDIC) hp = int(baseHp * 0.8f);
    else if (type == ET_HEAVY) hp = int(baseHp * 3.0f);
    else if (type == ET_STALKER) hp = int(baseHp * 0.9f);
    else if (type == ET_DRONE) hp = int(baseHp * 0.5f);
    else if (type == ET_TURRET) hp = int(baseHp * 1.6f);
    e.hp = e.maxHp = elite ? hp * 2 : hp;
    if (type == ET_DRONE) e.hover = 1.35f;
    if (type == ET_HEAVY) e.scale = 1.12f;

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
    if (m_gs.level < 2 || m_gs.levelComplete || m_gs.gameOver || m_reinforceLeft <= 0) return;
    m_reinforceTimer -= dt;
    if (m_reinforceTimer > 0) return;

    // Count alive enemies
    int aliveCount = 0;
    for (const auto &e : m_gs.enemies) if (e.alive) aliveCount++;
    if (aliveCount >= 8) { m_reinforceTimer = 8.f; return; }

    const Player &pl = m_gs.player;
    // Find a valid floor tile out of player LOS and at distance 10..22 units
    for (int tries = 0; tries < 40; tries++) {
        int rx = 2 + rand() % (m_mapW - 4), ry = 2 + rand() % (m_mapH - 4);
        if (m_map[ry][rx] != CELL_FLOOR || m_acid[ry][rx] || !reachable(rx, ry) || m_plat[ry][rx] < -0.1f) continue;
        float fx = rx + 0.5f, fy = ry + 0.5f;
        float dsq = (fx - pl.posX) * (fx - pl.posX) + (fy - pl.posY) * (fy - pl.posY);
        if (dsq < 100.f || dsq > 500.f) continue;
        float fz = floorH(fx, fy) + 0.5f;
        if (hasLOS(fx, fy, fz, pl.posX, pl.posY, m_gs.player.footZ + 0.5f)) continue; // Must be out of sight!
        if (!posValid(fx, fy, 0.28f)) continue;

        EnemyType rt = (rand() % 100 < 40) ? ET_SCOUT : (rand() % 100 < 70) ? ET_RIFLEMAN : ET_SHOTGUNNER;
        spawnEnemy(fx, fy, false, rt);
        m_reinforceLeft--; m_gs.killsNeeded++;
        pushFeed("HOSTILE REINFORCEMENTS INBOUND", 3.0f);
        sfx(SND_RADIO, 0.6f);
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

// Third person: the camera sees past a corner that the gun barrel does not.
// If the line from the muzzle to the aimed point is blocked, the bullet hits
// that obstacle instead (sparks + scorch, no damage behind it).
void MainWindow::muzzleObstruction(HitResult &h, float mx, float my, float mz) const
{
    float tx = h.x, ty = h.y, tz = h.z;
    if (h.sky) { tx = mx + (h.x - mx) * 0.6f; ty = my + (h.y - my) * 0.6f; tz = mz + (h.z - mz) * 0.6f; }
    // stop just short of the target surface so the surface itself doesn't count
    float dx = tx - mx, dy = ty - my, dz = tz - mz, len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 0.2f) return;
    float k = (len - 0.12f) / len;
    float bx, by, bz;
    if (!traceSegment(mx, my, mz, mx + dx * k, my + dy * k, mz + dz * k, bx, by, bz, 0.04f)) return;
    h.enemy = -1; h.part = PART_NONE; h.sky = false; h.wall = false; h.floor = true;
    h.x = bx; h.y = by; h.z = bz;
    h.depth = std::sqrt((bx - m_camX) * (bx - m_camX) + (by - m_camY) * (by - m_camY));
}

void MainWindow::damageEnemy(int idx, int dmg, HitPart part, float hx, float hy, float hz, int byActor)
{
    Enemy &e = m_gs.enemies[idx];
    if (!e.alive) return;
    bool head = part == PART_HEAD;
    bool mine = byActor < 0;
    if (e.shielded) { spawnImpact(hx, hy, hz, false); sfxAt(SND_RICOCHET, hx, hy, 0.6f); return; }
    dmg = std::max(1, int(dmg * (1.f - e.armor)));
    e.hp -= dmg; e.hitFlash = 0.16f;
    e.hpShow = 2.2f;
    if (e.type == ET_STALKER) e.revealT = 1.5f;
    if (e.bossKind) bossDamaged(idx, dmg);
    if (e.hp <= 0 && (e.type == ET_DRONE || e.type == ET_TURRET)) { e.hover = 0.f; explode(e.x, e.y, floorH(e.x, e.y) + 0.4f, 1.2f, 0.f, PO_ENEMY, true); }
    sfxAt(SND_HIT_FLESH, hx, hy, 0.8f, frand(0.9f, 1.1f));
    if (mine) sfx(head ? SND_HEADSHOT : SND_HITMARK, head ? 0.8f : 0.55f);

    // the hostile reacts to whoever shot it
    const Player &src = mine ? m_gs.player : m_gs.actors[size_t(byActor)];
    float dx = src.posX - e.x, dy = src.posY - e.y, d = std::sqrt(dx * dx + dy * dy);
    float ez = floorH(e.x, e.y) + 0.62f;
    float detectRange = e.aggroRange * 1.5f;
    bool los = d < std::max(18.f, detectRange) && hasLOS(e.x, e.y, ez, src.posX, src.posY, src.footZ + 0.5f);

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

    if (mine) {
        m_hitMarker = 0.25f;
        if (head) m_headMarker = 0.35f;
        m_gs.damageNums.push_back(DamageNum{hx, hy, hz + 0.05f, dmg, head, 0.9f});
    }
    spawnBlood(hx, hy, hz, head ? 22 : 9, head ? 1.7f : 1.f);

    if (e.hp <= 0 && e.decoy) {                              // a decoy pops: no loot, no score
        e.hp = 0; e.alive = false; e.state = ES_DEAD; e.deathTime = 0; e.corpseFade = 0.3f;
        for (int k = 0; k < 10; k++) spawnImpact(e.x, e.y, floorH(e.x, e.y) + frandB2(0.2f, 1.f), false);
        sfxAt(SND_ARC, e.x, e.y, 0.7f);
        if (mine) pushFeed("A DECOY - THE REAL ONE CASTS A SHADOW", 2.f);
        return;
    }
    if (e.hp <= 0 && !mine) {
        // squad kill: counts toward clearing the zone, not toward your score or report
        e.hp = 0; e.alive = false; e.state = ES_DEAD; e.deathTime = 0; e.killedByHeadshot = head;
        sfxAt(SND_ENEMY_DEATH, e.x, e.y, 0.9f, frand(0.85f, 1.15f));
        m_gs.killsThisLevel++;
        Actor &a = m_gs.actors[size_t(byActor)];
        a.kills++;
        pushFeed(QString("%1  %2").arg(a.name).arg(head ? "HEADSHOT - HOSTILE DOWN" : "HOSTILE DOWN"), 3.5f);
        addDecal(m_blood, e.x, e.y, 0.42f, 170);
        if (e.boss || e.elite) dropReward(e.x, e.y, e.boss);
        return;
    }
    if (e.hp <= 0) {
        e.hp = 0; e.alive = false; e.state = ES_DEAD; e.deathTime = 0; e.killedByHeadshot = head;
        sfx(SND_KILL, 0.7f, 1.f, 0.05f);
        sfxAt(SND_ENEMY_DEATH, e.x, e.y, 0.9f, frand(0.85f, 1.15f));
        m_gs.kills++; m_gs.killsThisLevel++; m_gs.zoneKills++;
        if (head) m_gs.zoneHeadshots++;
        m_gs.score += 100 * m_gs.level + (head ? 50 * m_gs.level : 0);
        if (head) { m_gs.headshots++; m_headshotBanner = 1.3f; }
        m_killMarker = 0.5f;
        m_combo++; m_comboTimer = 3.5f; m_comboDisplay = 1.8f;
        pushFeed((m_combo > 1 ? QString("x%1  ").arg(m_combo) : QString()) + (head ? "HEADSHOT KILL" : "ENEMY DOWN"), 4.5f, head);
        addDecal(m_blood, e.x, e.y, 0.42f, 170);
        if (e.boss) { m_gs.zoneBossKilled = true; m_gs.score += 1000 * m_gs.level; }
        if (e.boss || e.elite) dropReward(e.x, e.y, e.boss);      // bosses and elites always drop a gun
        else if (rand() % 3 == 0) spawnPickup(e.x, e.y);
    }
}

void MainWindow::enemyFire(Enemy &e, float tx, float ty, float tz)
{
    const Player &pl = m_gs.player;
    float dx = tx - e.x, dy = ty - e.y, d = std::sqrt(dx * dx + dy * dy);
    if (d < 0.1f) return;
    SoundId shot = e.type == ET_SNIPER ? SND_ENEMY_SNIPER : e.type == ET_SHOTGUNNER ? SND_ENEMY_SHOTGUN :
                   (e.type == ET_SCOUT || e.type == ET_SHIELD || e.type == ET_MEDIC) ? SND_ENEMY_PISTOL :
                   e.type == ET_DRONE ? SND_PLASMA : e.type == ET_TURRET ? SND_TURRET_FIRE : e.type == ET_HEAVY ? SND_LMG : SND_ENEMY_RIFLE;
    sfxAt(shot, e.x, e.y, 1.f, frand(0.94f, 1.06f));
    float acc = DIFF_CFG[m_difficulty].enemyAccuracy;
    if (e.type == ET_SNIPER) acc = std::min(0.98f, acc + 0.18f);
    else if (e.type == ET_SCOUT) acc = std::max(0.40f, acc - 0.12f);
    if (e.elite) acc = std::min(0.98f, acc + 0.1f);

    float z0 = floorH(e.x, e.y) + 0.62f * e.scale + e.hover, z1 = tz - 0.05f;
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
            b.owner = ENEMY_SHOT; b.alive = true; b.damage = pelletDmg; b.shooter = int(&e - m_gs.enemies.data());
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
        b.owner = ENEMY_SHOT; b.alive = true; b.damage = dmg; b.shooter = int(&e - m_gs.enemies.data());
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
            if (cx < 1 || cx >= m_mapW - 1 || cy < 1 || cy >= m_mapH - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR || !reachable(cx, cy)) continue;
            if (!posValid(tx, ty, 0.28f)) continue;
            float fh = floorH(tx, ty);
            if (std::fabs(fh - m_gs.player.footZ) > 0.75f) continue;

            bool los = hasLOS(pl.posX, pl.posY, m_gs.player.footZ + 0.5f, tx, ty, fh + 0.3f);
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
            if (cx < 1 || cx >= m_mapW - 1 || cy < 1 || cy >= m_mapH - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR || !reachable(cx, cy)) continue;
            if (!posValid(tx, ty, 0.28f)) continue;
            float fh = floorH(tx, ty);
            if (std::fabs(fh - m_gs.player.footZ) > 0.75f) continue;

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
        if (cx >= 1 && cx < m_mapW - 1 && cy >= 1 && cy < m_mapH - 1 && !solidCell(cx, cy) && !m_acid[cy][cx] && posValid(tx, ty, 0.2f)) {
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
                if (cx < 1 || cx >= m_mapW - 1 || cy < 1 || cy >= m_mapH - 1) continue;
                if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR || !reachable(cx, cy)) continue;
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
            if (cx < 1 || cx >= m_mapW - 1 || cy < 1 || cy >= m_mapH - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR || !reachable(cx, cy)) continue;
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
            if (cx < 1 || cx >= m_mapW - 1 || cy < 1 || cy >= m_mapH - 1) continue;
            if (solidCell(cx, cy) || m_acid[cy][cx] || m_map[cy][cx] == CELL_DOOR || !reachable(cx, cy)) continue;
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
    if (m_net && m_net->isMultiplayerActive()) return;
    Player &pl = m_gs.player;
    if (pl.hp <= 0) return;

    if (m_emergencyAmmoCooldown > 0.f)   m_emergencyAmmoCooldown -= dt;
    if (m_emergencyHealthCooldown > 0.f) m_emergencyHealthCooldown -= dt;

    // 1. Ammo drop check: when the player runs out of firearms ammo
    int totalAmmo = 0;
    for (int w = 0; w < WPN_COUNT; ++w) {
        if (pl.owned[w] && usesAmmo(w)) {
            totalAmmo += pl.mag[w] + pl.reserve[w];
        }
    }
    for (int w = 0; w < WPN_COUNT; ++w) if (pl.owned[w] && !isMelee(w) && !usesAmmo(w)) totalAmmo += 1;   // energy guns never run dry

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
                sfx(SND_RADIO, 0.6f);
            }
        }
    }

    // 2. Health drop check: when the player is in critical health condition
    const int maxHP = this->maxHP();
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
                sfx(SND_RADIO, 0.6f);
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
    bool moving = m_screen == SCR_PLAYING && isPlayerMoving();
    float bobZ = moving ? std::sin(pl.bobPhase * 2.f) * (pl.sprinting ? 0.018f : 0.01f) * (1.f - m_adsT * 0.8f) : 0.f;
    m_headZ = m_gs.player.footZ + 0.5f + bobZ;
    m_eye = m_headZ;
    m_camX = pl.posX; m_camY = pl.posY;
    if (m_boomT > 0.001f && m_boomCur > 0.001f) {
        float dx, dy, dz;
        boomOffset(m_boomCur, dx, dy, dz);
        m_camX += dx; m_camY += dy; m_eye += dz;
    }
    m_hor = int(H / 2.f + m_pitch * m_K + pl.shakeY + m_camKick);
}

// Over-the-shoulder boom: back along the view, out to one shoulder, up a little.
// Looking up swings the camera down and vice versa (orbit around the head).
float MainWindow::boomWanted() const
{
    const Player &pl = m_gs.player;
    float len = 1.9f - 0.65f * m_adsT;                    // aiming pulls the camera in
    if (pl.sprinting) len += 0.2f;
    return len * m_boomT;
}

void MainWindow::boomOffset(float len, float &dx, float &dy, float &dz) const
{
    const Player &pl = m_gs.player;
    float rx = -pl.dirY, ry = pl.dirX;                    // camera right
    float full = std::max(0.01f, boomWanted());
    float k = std::min(1.f, len / full);                  // 0..1 of the wanted boom
    float side = 0.56f * m_shoulderT * k * m_boomT;
    dx = -pl.dirX * len + rx * side;
    dy = -pl.dirY * len + ry * side;
    dz = (0.46f * k - 0.35f * m_pitch * len) * m_boomT;
}

void MainWindow::updateCamera(float dt)
{
    const Player &pl = m_gs.player;
    // Scoped weapons always aim through the first-person optic
    bool wantTpp = m_tpp && !(effectiveScope(pl.curWeapon) && pl.adsMode);
    m_boomT += ((wantTpp ? 1.f : 0.f) - m_boomT) * std::min(1.f, dt * 7.f);
    if (!wantTpp && m_boomT < 0.002f) m_boomT = 0.f;
    m_shoulderT += ((m_shoulderLeft ? -1.f : 1.f) - m_shoulderT) * std::min(1.f, dt * 8.f);
    if (m_boomT <= 0.f) { m_boomCur = 0.f; return; }

    // Collision: march from the head to the wanted camera spot; stop short of
    // the first wall or hill so the camera never ends up inside geometry
    float want = boomWanted();
    float dx, dy, dz;
    boomOffset(want, dx, dy, dz);
    float hx = pl.posX, hy = pl.posY, hz = pl.footZ + 0.5f;
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    float allowed = want;
    float bx, by, bz;
    // extend the ray so the camera keeps ~0.22 units of clearance from a wall
    float pad = len > 1e-3f ? (len + 0.22f) / len : 1.f;
    if (len > 1e-3f && traceSegment(hx, hy, hz, hx + dx * pad, hy + dy * pad, hz + dz * pad, bx, by, bz, 0.03f)) {
        float hit = std::sqrt((bx - hx) * (bx - hx) + (by - hy) * (by - hy) + (bz - hz) * (bz - hz));
        allowed = std::max(0.f, (hit - 0.22f) / len * want);
    }
    // Snap in quickly when blocked, ease back out
    if (allowed < m_boomCur) m_boomCur = allowed;
    else m_boomCur += (allowed - m_boomCur) * std::min(1.f, dt * 5.f);
}

// The gun muzzle in world space for the third-person body (right hand, chest high)
void MainWindow::playerMuzzle(float &x, float &y, float &z) const
{
    const Player &pl = m_gs.player;
    float rx = -pl.dirY, ry = pl.dirX;
    x = pl.posX + pl.dirX * 0.42f + rx * 0.07f;
    y = pl.posY + pl.dirY * 0.42f + ry * 0.07f;
    z = pl.footZ + 0.62f;
}

bool MainWindow::project(float wx, float wy, float wz, float &sx, float &sy, float &depth) const
{
    const Player &pl = m_gs.player;
    float eplX = pl.planeX * m_adsFOVMult, eplY = pl.planeY * m_adsFOVMult;
    float invDet = 1.f / (eplX * pl.dirY - pl.dirX * eplY);
    float spX = wx - m_camX, spY = wy - m_camY;
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
    float sens = std::max(0.25f, m_adsFOVMult) * m_mouseSens * 2.2f;
    dx *= sens; dy *= sens;
    m_mouseDX += dx; m_mouseDY += dy;
    auto yaw = [&](float px) { turn(std::atan(2.f * px / W * m_planeLen * m_adsFOVMult)); };
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
    sfx(SND_UI_SELECT, 0.6f);
    switch (m_screen) {
    case SCR_MENU:
        if (m_menuCursor == 0) { m_gameMode = MODE_CAMPAIGN; openCampaign(); }
        else if (m_menuCursor == 1) { m_gameMode = MODE_FREEPLAY; m_campaignRun = false; if (m_net) m_net->disconnectSession(); m_screen = SCR_MAP_SELECT; }
        else if (m_menuCursor == 2) { m_gameMode = MODE_MULTIPLAYER; openMultiplayerLobby(); }
        else if (m_menuCursor == 3) m_screen = SCR_ARSENAL;
        else if (m_menuCursor == 4) openEditor(m_mapChoice);
        else if (m_menuCursor == 5) openSettings(SCR_MENU);
        else if (m_menuCursor == 6) startDebugMode();
        else QApplication::quit();
        break;
    case SCR_SETTINGS: changeSetting(m_settingsRow, 1); break;
    case SCR_ARSENAL: break;
    case SCR_MAP_SELECT:  m_screen = SCR_DIFF_SELECT; break;
    case SCR_DIFF_SELECT: m_screen = SCR_CHAR_SELECT; break;
    case SCR_CHAR_SELECT: initGame(); m_screen = SCR_PLAYING; break;
    case SCR_MULTIPLAYER_LOBBY:
        if (m_net && m_net->isHost()) startMultiplayerMatch();
        break;
    default: break;
    }
}
void MainWindow::menuBack()
{
    sfx(SND_UI_BACK, 0.6f);
    switch (m_screen) {
    case SCR_MENU:        QApplication::quit(); break;
    case SCR_CAMPAIGN:    m_screen = SCR_MENU; m_menuCursor = 0; break;
    case SCR_BRIEFING:    m_screen = SCR_CAMPAIGN; break;
    case SCR_DEBRIEF:     m_campaignRun = false; openCampaign(); break;
    case SCR_EDITOR:      m_screen = SCR_MENU; m_menuCursor = 4; break;
    case SCR_MAP_SELECT:  m_gameMode = MODE_FREEPLAY; m_screen = SCR_MENU; m_menuCursor = 1; break;
    case SCR_SETTINGS:
        saveSettings();
        if (m_settingsReturnScreen == SCR_PLAYING) {
            m_screen = SCR_PLAYING;
            m_paused = m_settingsWasPaused;
        } else {
            m_screen = SCR_MENU;
            m_menuCursor = 5;
        }
        break;
    case SCR_ARSENAL:     m_screen = SCR_MENU; m_menuCursor = 3; break;
    case SCR_DIFF_SELECT: m_screen = SCR_MAP_SELECT; break;
    case SCR_CHAR_SELECT: m_screen = SCR_DIFF_SELECT; break;
    case SCR_MULTIPLAYER_LOBBY:
        m_gameMode = MODE_FREEPLAY;
        if (m_net) m_net->disconnectSession();
        m_screen = SCR_MENU;
        m_menuCursor = 2;
        break;
    default: break;
    }
}


void MainWindow::keyPressEvent(QKeyEvent *e)
{
    int k = e->key();
    m_keys.insert(k);
    if (m_screen == SCR_EDITOR) { editorKey(e); return; }
    if (m_screen == SCR_CAMPAIGN || m_screen == SCR_BRIEFING || m_screen == SCR_DEBRIEF) { if (!e->isAutoRepeat()) campaignKey(k); return; }
    if (m_screen == SCR_PLAYING && m_flyActive) { if (!e->isAutoRepeat()) { m_flyActive = false; m_keys.erase(k); } return; }
    if (m_screen == SCR_PLAYING && k == Qt::Key_F5 && m_edPlaytest) { m_edPlaytest = false; openEditor(m_edMap); return; }

    // Global settings key: [O] or [P] opens/toggles settings
    if (k == Qt::Key_O || k == Qt::Key_P) {
        if (m_screen == SCR_SETTINGS) {
            menuBack();
            return;
        } else if (m_screen == SCR_PLAYING) {
            openSettings(SCR_PLAYING);
            return;
        } else if (m_screen == SCR_MENU) {
            openSettings(SCR_MENU);
            return;
        }
    }

    if (m_screen == SCR_MULTIPLAYER_LOBBY) {
        if (m_mpTypingName) {
            if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Escape) {
                m_mpTypingName = false;
                if (m_mpPlayerName.trimmed().isEmpty()) m_mpPlayerName = "Operative";
                if (m_net) m_net->setPlayerName(m_mpPlayerName);
                sfx(SND_UI_SELECT, 0.5f);
                return;
            }
            if (k == Qt::Key_Backspace) {
                if (!m_mpPlayerName.isEmpty()) m_mpPlayerName.chop(1);
                return;
            }
            QString txt = e->text();
            for (QChar c : txt) {
                if ((c.isLetterOrNumber() || c == ' ' || c == '_' || c == '-') && m_mpPlayerName.length() < 16) {
                    m_mpPlayerName.append(c);
                }
            }
            return;
        }

        if (m_mpTypingAddr) {
            if (k == Qt::Key_Return || k == Qt::Key_Enter) {
                m_mpTypingAddr = false;
                joinMultiplayer();
                return;
            }
            if (k == Qt::Key_Escape) {
                m_mpTypingAddr = false;
                return;
            }
            if (k == Qt::Key_Backspace) {
                if (!m_mpInputAddr.isEmpty()) m_mpInputAddr.chop(1);
                return;
            }
            QString txt = e->text();
            for (QChar c : txt) {
                if (c.isLetterOrNumber() || c == '.' || c == ':' || c == '-' || c == '_') {
                    m_mpInputAddr.append(c);
                }
            }
            return;
        }

        if (k == Qt::Key_N) {
            m_mpTypingName = true;
            m_mpTypingAddr = false;
            sfx(SND_UI_SELECT, 0.5f);
            return;
        }
        if (k == Qt::Key_1) { m_net->setTeam(0); sfx(SND_UI_MOVE, 0.4f); return; }
        if (k == Qt::Key_2) { m_net->setTeam(1); sfx(SND_UI_MOVE, 0.4f); return; }
        if (k == Qt::Key_C) {
            CharClass nextCls = CLS_ASSAULT;
            for (const auto &s : m_net->playerSlots()) {
                if (s.id == m_net->localPlayerId()) nextCls = CharClass((s.cls + 1) % 3);
            }
            m_net->setClass(nextCls);
            sfx(SND_UI_MOVE, 0.4f);
            return;
        }
        if (k == Qt::Key_R) {
            bool rdy = false;
            for (const auto &s : m_net->playerSlots()) {
                if (s.id == m_net->localPlayerId()) rdy = s.ready;
            }
            m_net->setReady(!rdy);
            sfx(SND_UI_SELECT, 0.5f);
            return;
        }
        if (k == Qt::Key_H) { hostMultiplayer(); return; }
        if (k == Qt::Key_J) { joinMultiplayer(); return; }
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
            if (m_net->isHost()) {
                startMultiplayerMatch();
            }
            return;
        }
        if (k == Qt::Key_Escape || k == Qt::Key_Backspace) {
            menuBack();
            return;
        }
        return;
    }

    if (m_screen != SCR_PLAYING) {
        int *sel = m_screen == SCR_MAP_SELECT ? &m_mapChoice : m_screen == SCR_DIFF_SELECT ? &m_difficulty :
                   m_screen == SCR_CHAR_SELECT ? &m_charChoice : nullptr;
        if (k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_Left || k == Qt::Key_Right) sfx(SND_UI_MOVE, 0.5f);
        if (m_screen == SCR_MENU) {
            if (k == Qt::Key_Up)   m_menuCursor = std::max(0, m_menuCursor - 1);
            if (k == Qt::Key_Down) m_menuCursor = std::min(7, m_menuCursor + 1);
            if (k >= Qt::Key_1 && k <= Qt::Key_8) { m_menuCursor = k - Qt::Key_1; menuConfirm(); return; }
        } else if (m_screen == SCR_SETTINGS) {
            if (m_rebindingAction >= 0) {
                if (k == Qt::Key_Escape) {
                    m_rebindingAction = -1;
                    sfx(SND_UI_BACK, 0.5f);
                    return;
                }
                m_keyBinds[m_rebindingAction] = k;
                m_rebindingAction = -1;
                saveSettings();
                sfx(SND_UI_SELECT, 0.6f);
                return;
            }

            if (k == Qt::Key_Tab) {
                m_settingsTab = 1 - m_settingsTab;
                sfx(SND_UI_MOVE, 0.4f);
                return;
            }
            if (k == Qt::Key_1 && m_settingsTab != 0) {
                m_settingsTab = 0;
                sfx(SND_UI_MOVE, 0.4f);
                return;
            }
            if (k == Qt::Key_2 && m_settingsTab != 1) {
                m_settingsTab = 1;
                sfx(SND_UI_MOVE, 0.4f);
                return;
            }

            if (m_settingsTab == 0) {
                if (k == Qt::Key_Up)    m_settingsRow = (m_settingsRow + settingsRows() - 1) % settingsRows();
                if (k == Qt::Key_Down)  m_settingsRow = (m_settingsRow + 1) % settingsRows();
                if (k == Qt::Key_Left)  changeSetting(m_settingsRow, -1);
                if (k == Qt::Key_Right) changeSetting(m_settingsRow, 1);
                if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
                    changeSetting(m_settingsRow, 1);
                    return;
                }
                if (k == Qt::Key_Escape || k == Qt::Key_Backspace) {
                    menuBack();
                    return;
                }
                return;
            } else {
                if (k == Qt::Key_Up) {
                    if (m_controlsRow == ACT_COUNT) m_controlsRow = 10;
                    else if (m_controlsRow % 11 == 0) m_controlsRow = m_controlsRow + 10;
                    else m_controlsRow--;
                    sfx(SND_UI_MOVE, 0.4f);
                } else if (k == Qt::Key_Down) {
                    if (m_controlsRow == ACT_COUNT) m_controlsRow = 0;
                    else if (m_controlsRow % 11 == 10) m_controlsRow = ACT_COUNT;
                    else m_controlsRow++;
                    sfx(SND_UI_MOVE, 0.4f);
                } else if (k == Qt::Key_Left) {
                    if (m_controlsRow >= 11 && m_controlsRow < ACT_COUNT) m_controlsRow -= 11;
                    sfx(SND_UI_MOVE, 0.4f);
                } else if (k == Qt::Key_Right) {
                    if (m_controlsRow < 11) m_controlsRow += 11;
                    sfx(SND_UI_MOVE, 0.4f);
                } else if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
                    if (m_controlsRow == ACT_COUNT) {
                        initDefaultKeyBinds();
                        saveSettings();
                        sfx(SND_UPGRADE, 0.6f);
                        return;
                    }
                    m_rebindingAction = m_controlsRow;
                    sfx(SND_UI_SELECT, 0.5f);
                    return;
                } else if (k == Qt::Key_Escape || k == Qt::Key_Backspace) {
                    menuBack();
                    return;
                }
                return;
            }
        } else if (m_screen == SCR_ARSENAL) {
            if (k == Qt::Key_Up)   m_arsenalSel = (m_arsenalSel + WPN_COUNT - 1) % WPN_COUNT;
            if (k == Qt::Key_Down) m_arsenalSel = (m_arsenalSel + 1) % WPN_COUNT;
            if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) return;
        } else if (sel) {
            if (k == Qt::Key_Left)  *sel = std::max(0, *sel - 1);
            int nOpt = m_screen == SCR_MAP_SELECT ? MAP_COUNT : 3;
            if (m_screen == SCR_MAP_SELECT && k == Qt::Key_Down) *sel = std::min(nOpt - 1, *sel + 3);
            if (m_screen == SCR_MAP_SELECT && k == Qt::Key_Up) *sel = std::max(0, *sel - 3);
            if (k == Qt::Key_Right) *sel = std::min(nOpt - 1, *sel + 1);
            if (k >= Qt::Key_1 && k < Qt::Key_1 + nOpt) { *sel = k - Qt::Key_1; menuConfirm(); return; }
        }
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) menuConfirm();
        else if (k == Qt::Key_Escape || (k == Qt::Key_Q && m_screen == SCR_MENU)) menuBack();
        return;
    }
    if (e->isAutoRepeat()) return;
    if (m_killcamActive) { m_killcamActive = false; return; }
    Player &pl = m_gs.player;

    // Zone cleared: choose an upgrade [1-3], a weapon [TAB / arrows], then deploy [ENTER].
    // Keys pressed in the first moments are ignored so combat input can't skip the screen.
    if (m_gs.levelComplete && !m_paused && !m_gs.gameOver && k != Qt::Key_Escape) {
        if (m_intermissionT < 0.9f) return;
        if (m_ixPage == 0) {                                     // zone report: ENTER skips the tally, then continues
            if (k == Qt::Key_Return || k == Qt::Key_Enter) {
                float full = 0.6f + 0.3f * float(m_report.size()) + 0.6f;
                if (m_intermissionT < full) m_intermissionT = full;
                else { m_ixPage = 1; m_ixPageStart = m_intermissionT; sfx(SND_UI_SELECT, 0.6f); }
            }
            return;
        }
        if (m_intermissionT - m_ixPageStart < 0.25f) return;
        if (k >= Qt::Key_1 && k <= Qt::Key_3) { selectUpgradeCard(k - Qt::Key_1); return; }
        if (k >= Qt::Key_7 && k <= Qt::Key_9) { buyShopItem(k - Qt::Key_7); return; }
        if (k == Qt::Key_Tab || k == Qt::Key_Right || k == Qt::Key_Left) {
            std::vector<int> ch = loadoutChoices();
            if (!ch.empty()) {
                int cur = 0;
                for (int s = 0; s < int(ch.size()); s++) if (ch[s] == m_loadoutWeapon) cur = s;
                cur = (cur + (k == Qt::Key_Left ? int(ch.size()) - 1 : 1)) % int(ch.size());
                m_loadoutWeapon = ch[cur];
                sfx(SND_SWITCH, 0.5f);
            }
            return;
        }
        if (k == Qt::Key_Return || k == Qt::Key_Enter) {
            if (m_selectedUpgradeCard >= 0) deployNextZone();
            else { sfx(SND_UI_BACK, 0.6f); pushFeed("PICK AN UPGRADE FIRST  [1] [2] [3]", 2.f); }
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

    bool live = !m_paused && !m_gs.gameOver;
    if (live) {
        if (k == Qt::Key_O || k == Qt::Key_P) { openSettings(SCR_PLAYING); return; }
        if (k == m_keyBinds[ACT_JUMP] && m_gs.player.onGround) m_jumpQueued = true;
        if (k == Qt::Key_N && m_snd) { m_snd->setMuted(!m_snd->muted()); pushFeed(m_snd->muted() ? "SOUND: OFF" : "SOUND: ON", 2.f); }
        if (k == m_keyBinds[ACT_FIRE]) tryFire();
        if (k == m_keyBinds[ACT_INTERACT] && !e->isAutoRepeat()) { if (!trySwapPickup()) tryOpenDoor(); }
        if (k == m_keyBinds[ACT_THROW] && !e->isAutoRepeat()) startThrow();
        if (k == m_keyBinds[ACT_CYCLE_THROW]) cycleThrowable();
        if (k == m_keyBinds[ACT_GADGET]) useGadget();
        if (k == m_keyBinds[ACT_INSPECT] && !isMelee(pl.curWeapon) && pl.reloadTimer <= 0 && pl.switchTimer <= 0) { m_inspectT = 2.2f; sfx(SND_INSPECT, 0.6f); }
        if (k == Qt::Key_C) { m_pitch = 0.f; m_aimX = W / 2.f; m_aimY = H / 2.f; }
        if (k == Qt::Key_B) { m_freeAim = !m_freeAim; m_aimX = W / 2.f; m_aimY = H / 2.f; pushFeed(m_freeAim ? "FREE AIM: ON" : "FREE AIM: OFF", 2.f); }
        if (k == m_keyBinds[ACT_CAMERA]) { m_tpp = !m_tpp; pushFeed(m_tpp ? "THIRD PERSON" : "FIRST PERSON", 1.5f); sfx(SND_UI_MOVE, 0.4f); }
        if (k == m_keyBinds[ACT_SHOULDER] && m_tpp) m_shoulderLeft = !m_shoulderLeft;
        if (k == m_keyBinds[ACT_FLASHLIGHT]) { m_flashlightOn = !m_flashlightOn; sfx(SND_ZOOM_TICK, 0.6f); pushFeed(m_flashlightOn ? "FLASHLIGHT ON" : "FLASHLIGHT OFF", 1.2f); }
        if (k == m_keyBinds[ACT_ADS]) pl.adsMode = !pl.adsMode;
        if (k == m_keyBinds[ACT_SLOT1]) selectSlot(0);
        if (k == m_keyBinds[ACT_SLOT2]) selectSlot(1);
        if (k == m_keyBinds[ACT_SLOT3]) selectSlot(2);
        if (k == m_keyBinds[ACT_SLOT4]) selectSlot(3);
        if (k == m_keyBinds[ACT_MELEE]) quickMelee();
        if (k == m_keyBinds[ACT_RELOAD]) startReload();
    }
    if (m_screen == SCR_PLAYING && m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive() && m_net->matchState().matchOver) {
        if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Escape || k == Qt::Key_Space) {
            m_screen = SCR_MULTIPLAYER_LOBBY;
            m_net->matchStateMutable().matchOver = false;
            m_net->matchStateMutable().roundOver = false;
            m_net->matchStateMutable().inRound = false;
            return;
        }
    }
    if (m_paused) {
        if (k == Qt::Key_R) { m_paused = false; return; }
        if (k == Qt::Key_O || k == Qt::Key_P) { openSettings(SCR_PLAYING); return; }
        if (k == Qt::Key_M) { m_screen = SCR_MENU; m_paused = false; m_menuCursor = 0; m_campaignRun = false; m_envLocked = false; m_gameMode = MODE_FREEPLAY; if (m_net) m_net->disconnectSession(); return; }
        if (k == Qt::Key_Q || k == Qt::Key_Escape) { QApplication::quit(); return; }
    }
    if (k == Qt::Key_Escape && !m_gs.gameOver) { m_paused = true; pl.adsMode = false; m_lmb = false; }
    if (k == Qt::Key_R && m_gs.gameOver) { m_paused = false; if (m_campaignRun) startMission(m_mission); else initGame(); }
    if (k == Qt::Key_Escape && m_gs.gameOver && m_campaignRun) { finishMission(false); return; }
    if (k == Qt::Key_Q && m_gs.gameOver) QApplication::quit();
    if (k == Qt::Key_M && m_gs.gameOver) { m_screen = SCR_MENU; m_paused = false; m_menuCursor = 0; m_campaignRun = false; m_envLocked = false; m_gameMode = MODE_FREEPLAY; if (m_net) m_net->disconnectSession(); }
    if (k == Qt::Key_BracketLeft)  m_pixelScale = std::min(m_pixelScale * 2, 8);
    if (k == Qt::Key_BracketRight) m_pixelScale = std::max(m_pixelScale / 2, 1);
}
void MainWindow::keyReleaseEvent(QKeyEvent *e) { if (!e->isAutoRepeat()) m_keys.erase(e->key()); }
void MainWindow::focusOutEvent(QFocusEvent *) { m_keys.clear(); m_lmb = false; }

void MainWindow::mousePressEvent(QMouseEvent *e)
{
    if (m_screen == SCR_EDITOR) {
        QPointF f = widgetToFrame(e->position());
        for (const HitRect &r : m_menuRects) {
            if (f.x() >= r.x && f.y() >= r.y && f.x() < r.x + r.w && f.y() < r.y + r.h) {
                if (r.id == 10) { menuBack(); return; }
            }
        }
        editorMouse(f, e->buttons());
        return;
    }
    if (m_screen == SCR_PLAYING && m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive() && m_net->matchState().matchOver) {
        m_screen = SCR_MULTIPLAYER_LOBBY;
        m_net->matchStateMutable().matchOver = false;
        m_net->matchStateMutable().roundOver = false;
        m_net->matchStateMutable().inRound = false;
        return;
    }
    if (m_screen != SCR_PLAYING) {
        if (e->button() == Qt::RightButton) { menuBack(); return; }
        QPointF f = widgetToFrame(e->position());
        for (const HitRect &r : m_menuRects) {
            if (f.x() < r.x || f.y() < r.y || f.x() >= r.x + r.w || f.y() >= r.y + r.h) continue;
            if (r.id == 10) { menuBack(); return; }
            if (m_screen == SCR_CAMPAIGN) { campaignClick(r.id); return; }
            if (m_screen == SCR_SETTINGS) {
                if (m_rebindingAction >= 0) {
                    m_rebindingAction = -1;
                    sfx(SND_UI_BACK, 0.5f);
                    return;
                }
                if (r.id == 50) {
                    m_settingsTab = 0;
                    sfx(SND_UI_MOVE, 0.4f);
                    return;
                }
                if (r.id == 51) {
                    m_settingsTab = 1;
                    sfx(SND_UI_MOVE, 0.4f);
                    return;
                }
                if (m_settingsTab == 0) {
                    m_settingsRow = r.id;
                    if (r.id == 8) {
                        m_settingsTab = 1;
                        m_controlsRow = 0;
                        sfx(SND_UI_SELECT, 0.5f);
                        return;
                    }
                    if (r.id == 9) {
                        changeSetting(9, 1);
                        return;
                    }
                    int vx = 60 + (W - 120) - 300;
                    if (f.x() >= vx && f.x() <= vx + 170) {
                        float frac = rs::clampf((f.x() - vx) / 170.f, 0.f, 1.f);
                        if (r.id == 0) m_mouseSens = rs::clampf(std::round((0.2f + frac * 5.8f) * 10.f) / 10.f, 0.2f, 6.0f);
                        else if (r.id == 1) m_fovDeg = rs::clampi(int(55 + frac * 45.f), 55, 100);
                        else if (r.id == 2) m_volume = rs::clampi(int(frac * 100.f), 0, 100);
                        else if (r.id == 5) m_squadSize = rs::clampi(int(std::round(frac * 3.f)), 0, 3);
                        applySettings();
                        saveSettings();
                        if (r.id == 2) sfx(SND_UI_SELECT, 0.6f);
                        else sfx(SND_UI_MOVE, 0.4f);
                    } else if (f.x() < vx) {
                        changeSetting(r.id, -1);
                    } else {
                        changeSetting(r.id, 1);
                    }
                    return;
                } else {
                    if (r.id >= 100 && r.id < 100 + ACT_COUNT) {
                        int act = r.id - 100;
                        m_controlsRow = act;
                        m_rebindingAction = act;
                        sfx(SND_UI_SELECT, 0.5f);
                        return;
                    }
                    if (r.id == 200) {
                        initDefaultKeyBinds();
                        saveSettings();
                        sfx(SND_UPGRADE, 0.6f);
                        return;
                    }
                    return;
                }
            }
            if (m_screen == SCR_MULTIPLAYER_LOBBY) {
                if (r.id != 9) m_mpTypingAddr = false;
                if (r.id != 8) {
                    if (m_mpTypingName) {
                        if (m_mpPlayerName.trimmed().isEmpty()) m_mpPlayerName = "Operative";
                        if (m_net) m_net->setPlayerName(m_mpPlayerName);
                    }
                    m_mpTypingName = false;
                }
                if (r.id == 1) { m_net->setTeam(0); sfx(SND_UI_MOVE, 0.4f); }
                else if (r.id == 2) { m_net->setTeam(1); sfx(SND_UI_MOVE, 0.4f); }
                else if (r.id == 3) {
                    CharClass nextCls = CLS_ASSAULT;
                    for (const auto &s : m_net->playerSlots()) {
                        if (s.id == m_net->localPlayerId()) nextCls = CharClass((s.cls + 1) % 3);
                    }
                    m_net->setClass(nextCls);
                    sfx(SND_UI_MOVE, 0.4f);
                }
                else if (r.id == 4) {
                    bool rdy = false;
                    for (const auto &s : m_net->playerSlots()) {
                        if (s.id == m_net->localPlayerId()) rdy = s.ready;
                    }
                    m_net->setReady(!rdy);
                    sfx(SND_UI_SELECT, 0.5f);
                }
                else if (r.id == 5) {
                    if (m_net->isHost()) startMultiplayerMatch();
                }
                else if (r.id == 6) { hostMultiplayer(); }
                else if (r.id == 7) { joinMultiplayer(); }
                else if (r.id == 8) { m_mpTypingName = true; m_mpTypingAddr = false; sfx(SND_UI_SELECT, 0.5f); }
                else if (r.id == 9) { m_mpTypingAddr = true; m_mpTypingName = false; sfx(SND_UI_SELECT, 0.5f); }
                else if (r.id == 20) { m_net->cycleTeamSize(); sfx(SND_UI_SELECT, 0.5f); }
                else if (r.id == 21) { m_net->toggleBotFill(); sfx(SND_UI_SELECT, 0.5f); }
                else if (r.id == 22) { m_net->cycleDuration(); sfx(SND_UI_SELECT, 0.5f); }
                else if (r.id == 23) { m_net->cycleRoundsToWin(); sfx(SND_UI_SELECT, 0.5f); }
                else if (r.id == 24) { m_net->cycleMap(); sfx(SND_UI_SELECT, 0.5f); }
                else if (r.id >= 100) { m_net->kickSlot(r.id - 100); sfx(SND_UI_BACK, 0.6f); }
                return;
            }
            if (m_screen == SCR_ARSENAL) { m_arsenalSel = r.id; sfx(SND_UI_MOVE, 0.5f); return; }

            if (m_screen == SCR_MENU) m_menuCursor = r.id;
            if (m_screen == SCR_MAP_SELECT) m_mapChoice = r.id;
            if (m_screen == SCR_DIFF_SELECT) m_difficulty = r.id;
            if (m_screen == SCR_CHAR_SELECT) m_charChoice = r.id;
            menuConfirm();
            return;
        }
        return;
    }
    if (m_screen == SCR_PLAYING && m_paused) {
        QPointF f = widgetToFrame(e->position());
        const int w = 380, h = 340, x = W / 2 - w / 2, y = H / 2 - h / 2;
        for (int i = 0; i < 5; i++) {
            int oy = y + 68 + i * 44;
            if (f.x() >= x + 40 && f.x() < x + w - 40 && f.y() >= oy && f.y() < oy + 36) {
                if (i == 0) { m_paused = false; sfx(SND_UI_SELECT, 0.6f); }
                else if (i == 1) { openSettings(SCR_PLAYING); }
                else if (i == 2) { m_screen = SCR_MENU; m_paused = false; m_menuCursor = 0; sfx(SND_UI_SELECT, 0.6f); }
                else if (i == 3 || i == 4) { QApplication::quit(); }
                return;
            }
        }
        return;
    }
    if (m_gs.levelComplete && !m_paused && !m_gs.gameOver) {
        if (m_intermissionT < 0.9f) return;                     // ignore the shot that cleared the zone
        QPointF f = widgetToFrame(e->position());
        QPoint pt(int(f.x()), int(f.y()));
        if (m_ixPage == 0) {
            float full = 0.6f + 0.3f * float(m_report.size()) + 0.6f;
            if (m_intermissionT < full) m_intermissionT = full;
            else if (ixDeployRect().contains(pt)) { m_ixPage = 1; m_ixPageStart = m_intermissionT; sfx(SND_UI_SELECT, 0.6f); }
            return;
        }
        if (m_intermissionT - m_ixPageStart < 0.25f) return;
        for (int i = 0; i < int(m_upgradeCards.size()) && i < 3; i++) {
            if (!ixCardRect(i).contains(pt)) continue;
            selectUpgradeCard(i);
            return;
        }
        for (int i = 0; i < 3; i++) if (ixShopRect(i).contains(pt)) { buyShopItem(i); return; }
        std::vector<int> ch = loadoutChoices();
        for (int s = 0; s < int(ch.size()); s++) {
            if (!ixChipRect(s, int(ch.size())).contains(pt)) continue;
            m_loadoutWeapon = ch[s]; sfx(SND_SWITCH, 0.5f);
            return;
        }
        if (ixDeployRect().contains(pt)) {
            if (m_selectedUpgradeCard >= 0) deployNextZone();
            else { sfx(SND_UI_BACK, 0.6f); pushFeed("PICK AN UPGRADE FIRST  [1] [2] [3]", 2.f); }
        }
        return;
    }
    if (m_showArmory) {
        m_showArmory = false;
        return;
    }
    if (m_killcamActive) { m_killcamActive = false; return; }
    if (m_paused || m_gs.gameOver) return;
    if (e->button() == Qt::LeftButton) { m_lmb = true; tryFire(); }
    if (e->button() == Qt::RightButton) m_gs.player.adsMode = true;
}
void MainWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) { m_lmb = false; releaseTrigger(); }
    if (e->button() == Qt::RightButton) m_gs.player.adsMode = false;
}
void MainWindow::mouseMoveEvent(QMouseEvent *e)
{
    m_mouseWidget = e->position();
    if (m_screen == SCR_EDITOR) { if (e->buttons() & (Qt::LeftButton | Qt::RightButton)) editorMouse(widgetToFrame(e->position()), e->buttons()); return; }
    if (m_screen != SCR_PLAYING) {
        QPointF f = widgetToFrame(e->position());
        int before = m_menuCursor * 100 + m_mapChoice * 10 + m_difficulty + m_charChoice * 1000 + m_settingsRow * 10000 + m_cSel * 100000 + m_controlsRow * 1000000;
        for (const HitRect &r : m_menuRects) {
            if (r.id == 10 || f.x() < r.x || f.y() < r.y || f.x() >= r.x + r.w || f.y() >= r.y + r.h) continue;
            if (m_screen == SCR_SETTINGS) {
                if (m_settingsTab == 0 && r.id >= 0 && r.id < settingsRows()) m_settingsRow = r.id;
                else if (m_settingsTab == 1 && r.id >= 100 && r.id < 100 + ACT_COUNT) m_controlsRow = r.id - 100;
                else if (m_settingsTab == 1 && r.id == 200) m_controlsRow = ACT_COUNT;
            }
            if (m_screen == SCR_MENU) m_menuCursor = r.id;
            if (m_screen == SCR_MAP_SELECT) m_mapChoice = r.id;
            if (m_screen == SCR_DIFF_SELECT) m_difficulty = r.id;
            if (m_screen == SCR_CHAR_SELECT) m_charChoice = r.id;
            if (m_screen == SCR_CAMPAIGN && r.id >= 0 && r.id < MISSION_COUNT) m_cSel = r.id;
        }
        int after = m_menuCursor * 100 + m_mapChoice * 10 + m_difficulty + m_charChoice * 1000 + m_settingsRow * 10000 + m_cSel * 100000 + m_controlsRow * 1000000;
        if (after != before) sfx(SND_UI_MOVE, 0.5f);
        return;
    }
    if (m_gs.levelComplete || m_showArmory) { if (m_gs.levelComplete) updateIntermissionHover(e->position()); return; }
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
    if (m_screen == SCR_SETTINGS) {
        changeSetting(m_settingsRow, steps > 0 ? 1 : -1);
        return;
    }
    bool live = m_screen == SCR_PLAYING && !m_paused && !m_gs.gameOver;
    if (live) {
        if (effectiveScope(m_gs.player.curWeapon) && m_adsT > 0.5f) {
            m_scopeMag = rs::clampf(m_scopeMag + steps, 2.f, 8.f);
            sfx(SND_ZOOM_TICK, 0.5f);
            return;
        }
        // Scroll wheel cycles weapons instead of zooming into the canvas
        const Player &pl = m_gs.player;
        int activeW = m_pendingWeapon >= 0 ? m_pendingWeapon : pl.curWeapon;
        int curS = slotOf(activeW);
        if (curS < 0) curS = 0;
        int dir = steps > 0 ? 1 : -1;
        for (int i = 1; i <= SLOT_COUNT; i++) {
            int targetS = (curS + dir * i % SLOT_COUNT + SLOT_COUNT) % SLOT_COUNT;
            int w = pl.slot[targetS];
            if (w >= 0 && pl.owned[w] && w != activeW) {
                selectSlot(targetS);
                return;
            }
        }
        return;
    }
    // Raster magnifier: only outside live gameplay
    float ax = float(m_mouseWidget.x()) * W / std::max(1, width());
    float ay = float(m_mouseWidget.y()) * H / std::max(1, height());
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
    m_testMode = true;
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
        pl.planeX = -pl.dirY * m_planeLen; pl.planeY = pl.dirX * m_planeLen;
        m_gs.player.footZ = floorH(x, y); m_gs.player.velZ = 0; m_gs.player.onGround = true;
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
    m_screen = SCR_ARSENAL; m_arsenalSel = WPN_SNIPER; run(10); save("03_arsenal_sniper");
    m_arsenalSel = WPN_PLASMA; run(30); save("03_arsenal_plasma");
    m_screen = SCR_SETTINGS; m_settingsRow = 1; run(5); save("04_settings");
    m_settingsTab = 1; m_controlsRow = 0; run(5); save("04_settings_controls");
    m_settingsTab = 0;
    { int fov0 = m_fovDeg; changeSetting(1, 1); log(QString("settings: fov %1 -> %2, plane %3").arg(fov0).arg(m_fovDeg).arg(m_planeLen, 0, 'f', 3)); changeSetting(1, -1); }
    m_screen = SCR_MAP_SELECT;
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
        // keep one enemy alive (killing them all would now clear the zone)
        for (size_t i = 1; i < m_gs.enemies.size(); i++) { m_gs.enemies[i].alive = false; m_gs.enemies[i].corpseFade = 0; }
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
        float yWalk = m_gs.player.posY, zWalk = m_gs.player.footZ;
        m_keys.clear();
        place(16.5f, 20.5f, -1.5708f);                          // standing on the 0.5 ledge ring
        m_gs.player.velZ = 5.5f; m_gs.player.onGround = false; m_keys.insert(Qt::Key_W);
        for (int i = 0; i < 40; i++) advance(1.f / 60.f);
        m_keys.clear();
        for (int i = 0; i < 30; i++) advance(1.f / 60.f);
        log(QString("walk up the steps: stopped at y=%1 z=%2 (high ledge starts at y<20)").arg(yWalk).arg(zWalk));
        log(QString("jump from ledge: y=%1 z=%2 plat=%3 (1.0 = on the high ledge)").arg(m_gs.player.posY).arg(m_gs.player.footZ).arg(m_plat[int(m_gs.player.posY)][16]));
        place(13.5f, 34.5f, 3.1416f);                           // arena: walk west into the pit (sunken -0.6)
        m_keys.insert(Qt::Key_W); for (int i = 0; i < 120; i++) advance(1.f / 60.f); m_keys.clear();
        float pitX = m_gs.player.posX;
        m_keys.insert(Qt::Key_D); for (int i = 0; i < 60; i++) advance(1.f / 60.f); m_keys.clear();   // try to walk out north
        log(QString("pit: fell to z=%1 at x=%2; walking out without jumping -> y=%3 (pit spans y 33..36)").arg(m_gs.player.footZ).arg(pitX).arg(m_gs.player.posY));
    }

    // --- Dungeon: secret wall opens onto a guarded vault ------------------------------
    start(0);
    {
        // the hint: dust drifting out of the seam and a one-time whisper
        place(13.0f, 6.5f, 0.f);
        for (int i = 0; i < 120; i++) { m_gs.player.hp = 9999; advance(1.f / 60.f); }
        int motes = 0; for (const Particle &p : m_gs.particles) if (p.type == PT_DUST && p.maxLife > 1.3f && p.maxLife < 2.1f) motes++;
        render(); save("20_secret_hint");
        bool whisper = false; for (const auto &f : m_killFeed) if (f.text.contains("COLD DRAFT")) whisper = true;
        log(QString("secret hint: dust motes=%1 whisper=%2 hinted=%3").arg(motes).arg(whisper).arg(m_secretHinted.size()));
    }
    place(15.5f, 6.5f, 0.f); run(5); save("20_dungeon_secret_wall");
    tryOpenDoor(); run(70); save("21_dungeon_vault_open");
    log(QString("secrets found: %1 / %2, cell(16,6)=%3").arg(m_gs.secretsFound).arg(m_gs.secretsTotal).arg(m_map[6][16]));
    // Door into the acid vault
    place(20.5f, 17.3f, 1.5708f); run(3); save("22_dungeon_door");
    tryOpenDoor(); run(80);
    place(20.5f, 18.4f, 1.5708f); m_pitch = -0.15f; run(10); save("23_dungeon_acid_vault");
    place(25.5f, 16.5f, 0.f); run(10); save("24_emberforge_lava_hazard");

    // --- Crypt: altar ledge and dusk shadows -----------------------------------------
    start(2);
    place(m_parsed[2].camX, m_parsed[2].camY, 1.2f); run(10); save("30_crypt");
    place(7.5f, 26.5f, -0.6f); run(10); save("31_crypt_altar");

    // --- Frostline: ice hazard test --------------------------------------------------
    start(3);
    place(10.5f, 50.5f, 0.f); run(10); save("35_frostline_ice_hazard");

    // --- Ashen City: acid hazard test ------------------------------------------------
    start(4);
    place(16.5f, 29.5f, 0.f); run(10); save("36_ashen_city_acid_hazard");

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

    // --- Third person: boom, wall collision, body + squad soldier -----------------
    start(1);
    place(20.5f, 20.5f, 0.4f);
    m_tpp = true;
    for (int i = 0; i < 40; i++) advance(1.f / 60.f);
    {
        Actor a; a.id = 1; a.team = 0; a.posX = 22.5f; a.posY = 19.6f; a.footZ = floorH(22.5f, 19.6f);
        a.dirX = std::cos(0.9f); a.dirY = std::sin(0.9f); a.curWeapon = WPN_RIFLE; a.walkPhase = 1.2f; a.velX = 2.f;
        m_gs.actors.push_back(a);
        Actor b = a; b.id = 2; b.posX = 23.6f; b.posY = 21.3f; b.footZ = floorH(23.6f, 21.3f); b.curWeapon = WPN_PISTOL;
        b.dirX = std::cos(2.6f); b.dirY = std::sin(2.6f); b.velX = 0.f; b.walkPhase = 0.f;
        m_gs.actors.push_back(b);
    }
    run(2); save("60_tpp");
    log(QString("tpp: boomT=%1 boom=%2 cam=(%3,%4) player=(%5,%6)").arg(m_boomT, 0, 'f', 2).arg(m_boomCur, 0, 'f', 2)
        .arg(m_camX, 0, 'f', 2).arg(m_camY, 0, 'f', 2).arg(m_gs.player.posX, 0, 'f', 2).arg(m_gs.player.posY, 0, 'f', 2));
    m_shoulderLeft = true; m_gs.player.adsMode = true;
    for (int i = 0; i < 30; i++) advance(1.f / 60.f);
    run(1); save("61_tpp_ads_left");
    m_gs.player.adsMode = false; m_shoulderLeft = false;
    // Back to a wall: the boom must pull in so the camera stays out of the wall
    start(0);
    place(1.6f, 1.6f, 0.0f);
    m_tpp = true;
    for (int i = 0; i < 40; i++) advance(1.f / 60.f);
    run(1); save("62_tpp_wall");
    log(QString("tpp against wall: boom=%1 (wanted %2) camera cell solid=%3").arg(m_boomCur, 0, 'f', 2).arg(boomWanted(), 0, 'f', 2)
        .arg(solidCell(int(std::floor(m_camX)), int(std::floor(m_camY)))));
    m_tpp = false;
    for (int i = 0; i < 40; i++) advance(1.f / 60.f);
    log(QString("back to first person: boomT=%1").arg(m_boomT, 0, 'f', 3));
    m_gs.actors.clear();

    // --- Squad bots: 3 AI teammates fight on the arena for 40 s -----------------------
    for (int map : {1, 3}) {
        m_squadSize = 3;
        start(map);
        for (auto &e : m_gs.enemies) e.fireCooldown = 1.f;      // let them shoot back
        int nan = 0, inWall = 0, maxApart = 0;
        for (int f = 0; f < 60 * 40; f++) {
            m_gs.player.hp = 9999;
            // the leader walks a slow circle so the squad has to follow
            float ang = f * 0.004f;
            m_gs.player.dirX = std::cos(ang); m_gs.player.dirY = std::sin(ang);
            m_gs.player.planeX = -m_gs.player.dirY * m_planeLen; m_gs.player.planeY = m_gs.player.dirX * m_planeLen;
            if (f % 240 < 120) m_keys.insert(Qt::Key_W); else m_keys.erase(Qt::Key_W);
            advance(1.f / 60.f);
            for (const Actor &a : m_gs.actors) {
                if (std::isnan(a.posX) || std::isnan(a.posY) || std::isnan(a.footZ)) nan++;
                if (a.alive && !posValid(a.posX, a.posY, 0.2f)) inWall++;
                float d = std::sqrt((a.posX - m_gs.player.posX) * (a.posX - m_gs.player.posX) + (a.posY - m_gs.player.posY) * (a.posY - m_gs.player.posY));
                maxApart = std::max(maxApart, int(d));
            }
            if (m_gs.levelComplete) break;
        }
        m_keys.clear();
        int squadKills = 0, alive = 0;
        for (const Actor &a : m_gs.actors) { squadKills += a.kills; alive += a.alive; }
        log(QString("squad %1: bots=%2 alive=%3 squadKills=%4 playerKills=%5 nan=%6 inWall=%7 maxApart=%8")
            .arg(MAP_NAMES[map]).arg(m_gs.actors.size()).arg(alive).arg(squadKills).arg(m_gs.kills).arg(nan).arg(inWall).arg(maxApart));
        if (map == 1) { m_tpp = true; for (int i = 0; i < 40; i++) advance(1.f / 60.f); run(1); save("63_squad"); m_tpp = false; }
    }
    m_squadSize = 0;

    // --- Arsenal: every weapon fires and hurts a target; throwables and gadgets work ---
    {
        start(1);
        Player &pl = m_gs.player;
        auto setupTarget = [&](float dist) {
            place(28.5f, 6.5f, 3.1416f);
            for (auto &e : m_gs.enemies) { e.alive = false; e.corpseFade = 0.f; }
            Enemy &t = m_gs.enemies[0];
            t.alive = true; t.state = ES_ALERT; t.hp = t.maxHp = 100000; t.fireCooldown = 999.f; t.burnT = t.shockT = t.stunT = 0.f;
            t.x = 28.5f - dist; t.y = 6.5f; t.corpseFade = 1.f; t.scale = 1.f;
            m_gs.enemies[1].alive = true; m_gs.enemies[1].hp = 100000; m_gs.enemies[1].fireCooldown = 999.f;     // keeps the zone open
            m_gs.enemies[1].x = 2.5f; m_gs.enemies[1].y = 37.5f; m_gs.enemies[1].corpseFade = 1.f;
            computeView();
            float sx, sy, d;
            if (project(t.x, t.y, floorH(t.x, t.y) + 0.55f, sx, sy, d)) { m_aimX = sx; m_aimY = sy; }
        };
        int working = 0;
        QString list;
        for (int w = 1; w < WPN_COUNT; w++) {
            const WeaponDef &d = WEAPONS[w];
            setupTarget(isMelee(w) ? 1.1f : d.proj == PJ_FLAME ? 3.0f : d.proj == PJ_ARC ? 5.f : 6.f);
            for (int k = 0; k < WPN_COUNT; k++) pl.owned[k] = false;
            pl.slot[SLOT_PRIMARY] = pl.slot[SLOT_PRIMARY2] = -1;
            pl.owned[w] = true; pl.slot[d.slot == SLOT_PRIMARY ? SLOT_PRIMARY : d.slot] = w;
            pl.curWeapon = w; pl.switchTimer = 0; pl.reloadTimer = 0; m_pendingWeapon = -1; pl.attackCooldown = 0;
            pl.mag[w] = effectiveMagSize(w); pl.reserve[w] = d.maxReserve; pl.heat = 0; pl.overheatT = 0; pl.charge = 0;
            m_charging = false; m_shellReload = false; pl.hp = 9999;
            Enemy &t = m_gs.enemies[0];
            int hp0 = t.hp;
            if (d.mode == FM_BOLT) {                            // scope in and centre the target, like a player would
                m_adsT = 1.f; m_scopeSwayX = m_scopeSwayY = 0.f;
                float dist = std::sqrt((t.x - pl.posX) * (t.x - pl.posX) + (t.y - pl.posY) * (t.y - pl.posY));
                m_pitch = (floorH(t.x, t.y) + 0.55f - (pl.footZ + 0.5f)) / dist;
            }
            m_lmb = true; tryFire();
            int hold = d.mode == FM_CHARGE ? 70 : d.mode == FM_SEMI || d.mode == FM_BOLT ? 1 : 40;
            for (int f = 0; f < hold; f++) { int keep = t.hp; setupTarget(isMelee(w) ? 1.1f : d.proj == PJ_FLAME ? 3.0f : d.proj == PJ_ARC ? 5.f : 6.f); t.hp = keep; advance(1.f / 60.f); }
            m_lmb = false; releaseTrigger();
            if (w == WPN_BATTLE_RIFLE || w == WPN_LMG || w == WPN_PLASMA || w == WPN_ARC || w == WPN_FLAMETHROWER || w == WPN_RAILGUN || w == WPN_SNIPER || w == WPN_ROCKET) { m_lmb = true; tryFire(); }
            if (w == WPN_SMG || w == WPN_ROCKET || w == WPN_ARC || w == WPN_FLAMETHROWER || w == WPN_REVOLVER || w == WPN_RAILGUN || w == WPN_PLASMA || w == WPN_AXE) {
                render(); save(QString("7%1_%2").arg(char('a' + w % 26)).arg(d.shortName).replace('.', '_').replace('-', '_'));
            }
            for (int f = 0; f < 90; f++) advance(1.f / 60.f);   // let projectiles land, burns tick
            m_lmb = false; releaseTrigger();
            int dmg = hp0 - m_gs.enemies[0].hp;
            if (dmg > 0) working++;
            list += QString(" %1=%2").arg(d.shortName).arg(dmg);
        }
        log(QString("arsenal: %1 / %2 weapons damaged the target").arg(working).arg(WPN_COUNT - 1));
        log("  damage per weapon:" + list);

        // attachments change the stats that matter
        Loadout L; L.att[AT_OPTIC] = 4; L.att[AT_MUZZLE] = 2; L.att[AT_MAG] = 1; L.rarity = 4;
        pl.load[WPN_RIFLE] = Loadout{};
        float rec0 = effectiveRecoil(WPN_RIFLE); int mag0 = effectiveMagSize(WPN_RIFLE); int dmg0 = effectiveDamage(WPN_RIFLE);
        pl.load[WPN_RIFLE] = L;
        log(QString("rifle attachments: scope=%1 adsFov=%2 recoil %3->%4 mag %5->%6 dmg %7->%8 (legendary, 4x, comp, ext mag)")
            .arg(effectiveScope(WPN_RIFLE)).arg(effectiveAdsFov(WPN_RIFLE)).arg(rec0, 0, 'f', 2).arg(effectiveRecoil(WPN_RIFLE), 0, 'f', 2)
            .arg(mag0).arg(effectiveMagSize(WPN_RIFLE)).arg(dmg0).arg(effectiveDamage(WPN_RIFLE)));
        pl.load[WPN_RIFLE] = Loadout{};

        // throwables
        auto throwAt = [&](int k, float dist) {
            setupTarget(dist);
            pl.throwCount[k] = 3; pl.throwSel = k;
            int hp0 = m_gs.enemies[0].hp;
            startThrow(); m_keys.insert(Qt::Key_G); advance(1.f / 60.f); m_keys.erase(Qt::Key_G); advance(1.f / 60.f);
            if (k == TH_MINE) { m_gs.enemies[0].x = 30.f; m_gs.enemies[0].y = 6.5f; }
            for (int f = 0; f < 60 * 4; f++) {
                m_gs.enemies[0].stunT = 1.f;                         // hold the target still
                if (k == TH_MINE && f == 120) { m_gs.enemies[0].x = pl.posX + pl.dirX * 0.5f; m_gs.enemies[0].y = pl.posY + pl.dirY * 0.5f; pl.posX += 3.f; }
                advance(1.f / 60.f);
                if (k == TH_SMOKE && f == 150) { render(); save("78_smoke"); }
                if (k == TH_MOLOTOV && f == 40) { render(); save("79_molotov"); }
                if (k == TH_FRAG && !m_gs.projectiles.empty() && f % 5 == 0 && m_blastLight > 0.5f) { render(); save("77_frag"); }
            }
            return hp0 - m_gs.enemies[0].hp;
        };
        int frag = throwAt(TH_FRAG, 5.f);
        bool smokeSeen = false;
        { setupTarget(6.f); pl.throwCount[TH_SMOKE] = 2; pl.throwSel = TH_SMOKE; startThrow(); m_keys.insert(Qt::Key_G); advance(1.f / 60.f); m_keys.clear(); advance(1.f / 60.f);
          for (int f = 0; f < 60 * 4; f++) { advance(1.f / 60.f); if (f == 180) { render(); save("78_smoke"); } }
          for (const SmokeCloud &c : m_gs.smokes) smokeSeen |= c.life > 0;
        }
        bool smokeBlocked = !m_gs.smokes.empty() && smokeBlocks(pl.posX, pl.posY, pl.footZ + 0.5f, m_gs.smokes[0].x - 3.f, m_gs.smokes[0].y, m_gs.smokes[0].z + 0.5f);
        m_gs.smokes.clear();
        setupTarget(5.f); pl.throwCount[TH_FLASH] = 1; pl.throwSel = TH_FLASH; startThrow(); m_keys.insert(Qt::Key_G); advance(1.f / 60.f); m_keys.clear();
        float stun = 0.f; for (int f = 0; f < 60 * 3; f++) { advance(1.f / 60.f); stun = std::max(stun, m_gs.enemies[0].stunT); }
        int molotov = throwAt(TH_MOLOTOV, 4.f);
        int mine = throwAt(TH_MINE, 4.f);
        log(QString("throwables: frag dmg=%1  smoke cloud=%2 blocks sight=%3  flash stun=%4 s  molotov dmg=%5  mine dmg=%6")
            .arg(frag).arg(smokeSeen).arg(smokeBlocked).arg(stun, 0, 'f', 1).arg(molotov).arg(mine));

        // gadgets
        setupTarget(7.f);
        m_gs.enemies[0].hp = 120;
        pl.gadget = GD_TURRET; pl.gadgetCd = 0; useGadget();
        for (int f = 0; f < 60 * 8; f++) { advance(1.f / 60.f); m_gs.player.hp = 9999; if (f == 30) { render(); save("80_turret"); } }
        bool turretKill = !m_gs.enemies[0].alive;
        pl.gadget = GD_RADAR; pl.gadgetCd = 0; useGadget(); advance(1.f / 60.f);
        float radar = m_radarT;
        pl.gadget = GD_JETPACK; pl.gadgetCd = 0; float z0 = pl.footZ; useGadget();
        float zMax = z0; for (int f = 0; f < 40; f++) { advance(1.f / 60.f); zMax = std::max(zMax, pl.footZ); }
        pl.gadget = GD_MEDSTATION; pl.gadgetCd = 0; pl.hp = 50; useGadget();
        for (int f = 0; f < 120; f++) advance(1.f / 60.f);
        int healed = pl.hp - 50;
        log(QString("gadgets: turret killed target=%1  radar=%2 s  jetpack rise=%3  medstation healed=%4  turrets=%5")
            .arg(turretKill).arg(radar, 0, 'f', 1).arg(zMax - z0, 0, 'f', 2).arg(healed).arg(m_gs.turrets.size()));

        // inventory: a primary pickup with both primary slots full swaps on E
        start(1);
        Loadout r4; r4.rarity = 4;
        giveWeapon(WPN_RIFLE, Loadout{}, -1, -1, true);
        giveWeapon(WPN_SMG, Loadout{}, -1, -1, true);
        spawnWeaponPickup(pl.posX, pl.posY, WPN_LMG, r4, false);
        m_pickupCooldown = 0.f;
        for (int f = 0; f < 30; f++) advance(1.f / 60.f);
        int near = m_nearPickup;
        bool swapped = trySwapPickup();
        for (int f = 0; f < 40; f++) advance(1.f / 60.f);
        int dropped = 0; for (const WeaponPickup &wp : m_gs.weaponPickups) if (wp.active && wp.type == PICKUP_WEAPON && wp.weapon == WPN_SMG) dropped++;
        log(QString("swap: prompt=%1 swapped=%2 slots=[%3 %4 %5 %6] smg dropped=%7 cur=%8 rarity=%9")
            .arg(near >= 0).arg(swapped).arg(pl.slot[0]).arg(pl.slot[1]).arg(pl.slot[2]).arg(pl.slot[3]).arg(dropped).arg(WEAPONS[pl.curWeapon].shortName).arg(pl.load[WPN_LMG].rarity));
        // view-model gallery
        int gallery[] = { WPN_SNIPER, WPN_PLASMA, WPN_RAILGUN, WPN_AUTO_SHOTGUN, WPN_BULLPUP, WPN_FLAMETHROWER, WPN_MACHETE, WPN_ARC };
        place(m_parsed[1].camX, m_parsed[1].camY, 0.5f);
        for (int g : gallery) {
            pl.owned[g] = true; pl.curWeapon = g; pl.switchTimer = 0; m_pendingWeapon = -1; pl.mag[g] = effectiveMagSize(g);
            run(3); save(QString("90_vm_%1").arg(WEAPONS[g].shortName).replace('.', '_').replace('-', '_'));
        }
        // attachments visible on the gun + laser
        pl.owned[WPN_RIFLE] = true; pl.curWeapon = WPN_RIFLE;
        pl.load[WPN_RIFLE].att[AT_OPTIC] = 2; pl.load[WPN_RIFLE].att[AT_MUZZLE] = 1; pl.load[WPN_RIFLE].att[AT_UNDER] = 2; pl.load[WPN_RIFLE].rarity = 3;
        run(3); save("91_vm_rifle_attachments");
        m_inspectT = 1.1f; run(1); save("92_inspect");
        m_inspectT = 0.f;
        m_tpp = true; for (int i = 0; i < 40; i++) advance(1.f / 60.f); run(1); save("93_tpp_rifle"); m_tpp = false;
        for (int i = 0; i < 40; i++) advance(1.f / 60.f);
        // debug range: every gun on the floor
        startDebugMode(); run(3); save("94_debug_range");
        int onFloor = 0; for (const WeaponPickup &wp : m_gs.weaponPickups) if (wp.type == PICKUP_WEAPON) onFloor++;
        log(QString("debug range: %1 guns on the floor").arg(onFloor));
    }

    // --- Biomes: every map at its default time + weather, then variations -------------
    for (int m = 0; m < MAP_COUNT; m++) {
        start(m);
        place(m_parsed[m].camX, m_parsed[m].camY, 0.7f);
        m_pitch = 0.05f;
        for (int i = 0; i < 20; i++) advance(1.f / 60.f);
        run(1); save(QString("95_biome_%1_%2").arg(m).arg(QString(MAP_NAMES[m]).replace(' ', '_')));
        log(QString("biome %1: %2 hour=%3 weather=%4 day=%5 trees=%6 size=%7x%8").arg(MAP_NAMES[m]).arg(THEMES[MAP_THEME[m]].name)
            .arg(m_hour, 0, 'f', 1).arg(m_weather).arg(m_dayLevel, 0, 'f', 2).arg(m_treeCells.size()).arg(m_mapW).arg(m_mapH));
    }
    start(3); place(m_parsed[3].camX, m_parsed[3].camY, 0.7f);
    setEnvironment(23.f, WX_BLIZZARD); for (int i = 0; i < 30; i++) advance(1.f / 60.f); run(1); save("96_snow_night_blizzard");
    start(2); place(m_parsed[2].camX, m_parsed[2].camY, 1.2f);
    setEnvironment(18.4f, WX_RAIN); for (int i = 0; i < 30; i++) advance(1.f / 60.f); run(1); save("96_swamp_dusk_rain");
    start(1); place(m_parsed[1].camX, m_parsed[1].camY, 0.7f);
    setEnvironment(15.f, WX_SAND); for (int i = 0; i < 30; i++) advance(1.f / 60.f); run(1); save("96_desert_sandstorm");
    m_dayCycle = false;
    // --- Map editor: flood fill + validation ------------------------------------------
    {
        openEditor(1);
        int before = 0; for (const std::string &r : m_edRows) for (char c : r) before += c == '~';
        m_edFill = true; m_edBrush = 0;                    // fill the acid lake with ground
        int fx = -1, fy = -1;
        for (int y = 0; y < int(m_edRows.size()) && fx < 0; y++) for (int x = 0; x < int(m_edRows[0].size()); x++) if (m_edRows[size_t(y)][size_t(x)] == '~') { fx = x; fy = y; break; }
        editorPaint(fx, fy, false);
        int after = 0; for (const std::string &r : m_edRows) for (char c : r) after += c == '~';
        QString okMsg = validateRows(m_edRows);
        m_edRows[5][5] = '#'; m_edRows[1][1] = '#';        // bury the player start
        QString badMsg = validateRows(m_edRows);
        log(QString("editor: fill removed %1 hazard cells (left %2), valid='%3', without start='%4'").arg(before - after).arg(after).arg(okMsg).arg(badMsg));
        openEditor(4); run(2); save("97_editor");
        editorMouse(QPointF(50, H - 20), Qt::LeftButton);
        log(QString("editor back button: screen=%1 cursor=%2").arg(m_screen).arg(m_menuCursor));
        if (m_screen != SCR_MENU || m_menuCursor != 4) log("ERROR: editor back button did not return to main menu");
        openEditor(4);
        QKeyEvent escKey(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        editorKey(&escKey);
        log(QString("editor esc key: screen=%1 cursor=%2").arg(m_screen).arg(m_menuCursor));
        if (m_screen != SCR_MENU || m_menuCursor != 4) log("ERROR: editor ESC key did not return to main menu");
        m_screen = SCR_MENU;
    }
    m_screen = SCR_MAP_SELECT; run(3); save("02_maps_biomes");

    // --- Extended roster: each new archetype fights; shield / medic / cloak rules ------
    {
        start(1);
        Player &pl = m_gs.player;
        auto arena = [&]() {
            place(20.5f, 24.5f, 0.f);
            for (auto &e : m_gs.enemies) { e.alive = false; e.corpseFade = 0.f; }
            m_gs.enemies.clear();
            spawnEnemy(2.5f, 37.5f, false, ET_RIFLEMAN);                 // far away: keeps the zone open
            m_gs.enemies.back().fireCooldown = 999.f;
            m_gs.bullets.clear(); m_gs.projectiles.clear();
        };
        static const char *TN[ET_COUNT] = { "RIFLE", "SCOUT", "SHOTGUN", "SNIPER", "GRENADIER", "SHIELD", "MEDIC", "HEAVY", "STALKER", "DRONE", "TURRET" };
        QString line;
        for (int t = ET_GRENADIER; t < ET_COUNT; t++) {
            arena();
            spawnEnemy(27.5f, 24.5f, false, EnemyType(t));
            Enemy &e = m_gs.enemies.back(); e.state = ES_ALERT; e.fireCooldown = 0.5f;
            int dmg0 = m_gs.zoneDamage, nan = 0, wall = 0;
            float x0 = e.x, y0 = e.y;
            for (int f = 0; f < 60 * 9; f++) {
                pl.hp = 9999; advance(1.f / 60.f);
                const Enemy &q = m_gs.enemies.back();
                if (std::isnan(q.x) || std::isnan(q.y)) nan++;
                if (q.type != ET_DRONE && !posValid(q.x, q.y, 0.15f)) wall++;
                if (f == 200 && t == ET_GRENADIER) { render(); save("98_grenadier_ring"); }
            }
            const Enemy &q = m_gs.enemies.back();
            float moved = std::sqrt((q.x - x0) * (q.x - x0) + (q.y - y0) * (q.y - y0));
            line += QString(" %1:dmg=%2,moved=%3,nan=%4,wall=%5").arg(TN[t]).arg(m_gs.zoneDamage - dmg0).arg(moved, 0, 'f', 1).arg(nan).arg(wall);
        }
        log("new hostiles:" + line);
        // riot shield: a round into the shield is stopped
        arena();
        spawnEnemy(25.5f, 24.5f, false, ET_SHIELD); m_gs.enemies.back().fireCooldown = 99.f;
        computeView();
        int shieldIdx = int(m_gs.enemies.size()) - 1;
        float sx, sy, sd;
        project(m_gs.enemies[size_t(shieldIdx)].x, m_gs.enemies[size_t(shieldIdx)].y, floorH(25.5f, 24.5f) + 0.45f, sx, sy, sd);
        HitResult hs = hitscan(sx - 0.12f * m_K / sd, sy);
        int hp0 = m_gs.enemies[size_t(shieldIdx)].hp;
        pl.curWeapon = WPN_PISTOL; pl.mag[WPN_PISTOL] = 12; pl.attackCooldown = 0; m_aimX = sx - 0.12f * m_K / sd; m_aimY = sy; tryFire();
        log(QString("riot shield: part=%1 (4 = shield) hp %2 -> %3").arg(int(hs.part)).arg(hp0).arg(m_gs.enemies[size_t(shieldIdx)].hp));
        // medic: brings a fallen rifleman back
        arena();
        spawnEnemy(26.5f, 24.5f, false, ET_RIFLEMAN); int ri = int(m_gs.enemies.size()) - 1;
        spawnEnemy(29.5f, 24.5f, false, ET_MEDIC); m_gs.enemies.back().state = ES_ALERT; m_gs.enemies.back().fireCooldown = 99.f;
        m_gs.enemies[size_t(ri)].fireCooldown = 99.f;
        damageEnemy(ri, 99999, PART_BODY, 26.5f, 24.5f, 0.5f);
        for (int f = 0; f < 60 * 6; f++) { pl.hp = 9999; advance(1.f / 60.f); }
        log(QString("medic: fallen rifleman revived=%1").arg(m_gs.enemies[size_t(ri)].alive));
        // stalker: cloaked at range, revealed when hit
        arena();
        spawnEnemy(34.5f, 24.5f, false, ET_STALKER); int si = int(m_gs.enemies.size()) - 1;
        for (int f = 0; f < 40; f++) { pl.hp = 9999; advance(1.f / 60.f); }
        float cloakFar = m_gs.enemies[size_t(si)].cloak;
        m_gs.enemies[size_t(si)].state = ES_ALERT;
        damageEnemy(si, 5, PART_BODY, 34.5f, 24.5f, 0.5f);
        for (int f = 0; f < 20; f++) { pl.hp = 9999; advance(1.f / 60.f); }
        render(); save("98_stalker_shimmer");
        log(QString("stalker: cloak at range=%1 after a hit=%2").arg(cloakFar, 0, 'f', 2).arg(m_gs.enemies[size_t(si)].cloak, 0, 'f', 2));
        // line-up screenshot of every new archetype
        arena();
        for (int t = ET_GRENADIER; t < ET_COUNT; t++) {
            spawnEnemy(26.5f, 20.5f + (t - ET_GRENADIER) * 1.15f, false, EnemyType(t));
            m_gs.enemies.back().fireCooldown = 99.f; m_gs.enemies.back().cloak = 0.f; m_gs.enemies.back().revealT = 99.f;
        }
        place(20.5f, 24.5f, 0.f); run(2); save("98_roster");
    }

    // --- Story bosses: each fight runs, attacks, changes phase, dies, drops loot -------
    {
        static const int KINDS[9] = { BK_WARDEN, BK_TWIN_A, BK_SEED_QUEEN, BK_GUNSHIP, BK_GUARD_HEAVY, BK_MIRAGE, BK_ENGINEER, BK_JUGGERNAUT, BK_FOUNDER };
        for (int kk = 0; kk < 9; kk++) {
            int kind = KINDS[kk];
            start(1);
            Player &pl = m_gs.player;
            place(18.5f, 24.5f, 0.f);
            m_gs.enemies.clear(); m_gs.bullets.clear(); m_gs.projectiles.clear(); m_seeds.clear();
            spawnEnemy(2.5f, 37.5f, false, ET_RIFLEMAN); m_gs.enemies.back().fireCooldown = 999.f;
            size_t first = m_gs.enemies.size();
            spawnZoneBoss(kind, 28.5f, 24.5f, 30.5f, 12.5f);
            int bi = int(first);
            for (size_t i = first; i < m_gs.enemies.size(); i++) m_gs.enemies[i].state = ES_ALERT;
            bool shieldedAtStart = m_gs.enemies[size_t(bi)].shielded;
            int dmg0 = m_gs.zoneDamage, attacks = 0, nan = 0, wall = 0, lastAtk = -1;
            int acidPeak = 0, acid0 = 0; for (uint8_t a : m_acid.v) acid0 += a;
            bool shot = false;
            auto step = [&](int frames) {
                for (int f = 0; f < frames; f++) {
                    pl.hp = 9999; advance(1.f / 60.f);
                    for (size_t i = first; i < m_gs.enemies.size(); i++) {
                        const Enemy &q = m_gs.enemies[i];
                        if (!q.alive) continue;
                        if (std::isnan(q.x) || std::isnan(q.y)) nan++;
                        if (q.hover <= 0.f && !posValid(q.x, q.y, 0.1f)) wall++;
                    }
                    const Enemy &b = m_gs.enemies[size_t(bi)];
                    if (b.atk != lastAtk) { if (b.atk >= 0) attacks++; lastAtk = b.atk; }
                    int ac = 0; for (uint8_t a : m_acid.v) ac += a; acidPeak = std::max(acidPeak, ac);
                    if (!shot && b.atk == 0 && b.atkPhaseT > 0.3f) { render(); save(QString("99_boss_%1_telegraph").arg(QString(bossName(kind)).replace(' ', '_').replace(':', '_'))); shot = true; }
                    if (m_gs.gameOver) { m_gs.gameOver = false; pl.hp = 9999; }
                }
            };
            step(60 * 14);
            if (!shot) { render(); save(QString("99_boss_%1").arg(QString(bossName(kind)).replace(' ', '_').replace(':', '_'))); }
            // break shields: destroy pylons, then work the boss down through its phases
            for (size_t i = first; i < m_gs.enemies.size(); i++) if (m_gs.enemies[i].bossKind == BK_PYLON || m_gs.enemies[i].bossKind == BK_EGG) damageEnemy(int(i), 99999, PART_BODY, m_gs.enemies[i].x, m_gs.enemies[i].y, 0.5f);
            step(30);
            bool shieldedAfterPylons = m_gs.enemies[size_t(bi)].shielded;
            int acidAfterEggs = 0; for (uint8_t a : m_acid.v) acidAfterEggs += a;
            int phases = 0;
            for (int chunk = 0; chunk < 40 && m_gs.enemies[size_t(bi)].alive; chunk++) {
                Enemy &b = m_gs.enemies[size_t(bi)];
                damageEnemy(bi, std::max(1, b.maxHp / 9), PART_BODY, b.x, b.y, floorH(b.x, b.y) + 0.5f);
                phases = std::max(phases, m_gs.enemies[size_t(bi)].phase);
                step(50);
            }
            // a pair: finish the partner too
            for (size_t i = first; i < m_gs.enemies.size(); i++) if (m_gs.enemies[i].alive && m_gs.enemies[i].bossKind) damageEnemy(int(i), 99999, PART_BODY, m_gs.enemies[i].x, m_gs.enemies[i].y, 0.5f);
            int loot = 0; for (const WeaponPickup &wp : m_gs.weaponPickups) if (wp.active && wp.reward) loot++;
            log(QString("boss %1: attacks=%2 dmgToPlayer=%3 shield %4->%5 phaseReached=%6 killed=%7 loot=%8 nan=%9 wall=%10%11")
                .arg(bossName(kind)).arg(attacks).arg(m_gs.zoneDamage - dmg0).arg(shieldedAtStart).arg(shieldedAfterPylons).arg(phases)
                .arg(!m_gs.enemies[size_t(bi)].alive).arg(loot).arg(nan).arg(wall)
                .arg(kind == BK_SEED_QUEEN ? QString(" acid %1 -> peak %2 -> after eggs %3").arg(acid0).arg(acidPeak).arg(acidAfterEggs) : QString()));
        }
    }

    // --- Campaign: menu, briefing, flyby, every mission played through by a script ----
    {
        loadCampaign();
        m_cUnlocked = 3; m_cRank[0] = 4; m_cRank[1] = 2; m_cCoins = 420;
        m_screen = SCR_CAMPAIGN; m_cSel = 2; run(2); save("97_campaign_menu");
        m_cSel = 0; m_mapChoice = MISSIONS[0].map; m_screen = SCR_BRIEFING; m_briefT = 7.f; run(2); save("97_briefing");
        for (int mi = 0; mi < MISSION_COUNT; mi++) {
            loadCampaign();
            m_charChoice = 0; m_difficulty = 1; m_cSel = mi;
            m_flyForTest = mi == 2;
            startMission(mi);
            m_flyForTest = false;
            if (m_flyActive) {
                QString pts; for (const FlyPt &q : m_flyPts) pts += QString(" (%1,%2,%3)").arg(q.x, 0, 'f', 1).arg(q.y, 0, 'f', 1).arg(q.z, 0, 'f', 1);
                float fx, fy, fz; flybyPoint(0.35f, fx, fy, fz);
                log(QString("flyby: %1 pts:%2 at35%%=(%3,%4,%5) ground=%6 map=%7x%8").arg(m_flyPts.size()).arg(pts).arg(fx).arg(fy).arg(fz).arg(groundAt(fx, fy)).arg(m_mapW).arg(m_mapH));
                m_flyT = m_flyDur * 0.35f; render(); save("97_flyby"); m_flyT = m_flyDur * 0.1f; render(); save("97_flyby_a"); m_flyT = m_flyDur * 0.7f; render(); save("97_flyby_b"); m_flyActive = false;
            }
            Player &pl = m_gs.player;
            int frames = 0, nan = 0, objSeen = 0, shot = 0;
            QString trail;
            int lastObj = -1;
            while (m_screen == SCR_PLAYING && frames < 60 * 400) {
                pl.hp = 9999;
                if (m_gs.gameOver) { m_gs.gameOver = false; }
                const MissionDef &M = MISSIONS[mi];
                if (m_obj != lastObj) { lastObj = m_obj; objSeen++; trail += QString(" %1@%2s").arg(m_obj).arg(int(m_missionTime)); }
                m_keys.erase(Qt::Key_E);
                if (m_obj >= 0 && m_obj < M.objCount && !m_missionDone) {
                    const ObjectiveDef &O = M.obj[m_obj];
                    float tx, ty;
                    switch (O.type) {
                    case OBJ_KILL_COUNT: case OBJ_KILL_ALL: case OBJ_SURVIVE:
                        if (frames % 20 == 0) for (int i = 0; i < int(m_gs.enemies.size()); i++) {
                            const Enemy &e = m_gs.enemies[size_t(i)];
                            if (e.alive && !e.bossKind) { damageEnemy(i, 99999, PART_HEAD, e.x, e.y, floorH(e.x, e.y) + 0.8f); break; }
                        }
                        break;
                    case OBJ_REACH: case OBJ_COLLECT: case OBJ_INTERACT: case OBJ_HOLD:
                        if (objectiveTarget(tx, ty) && frames % 10 == 0) place(tx, ty, 0.f);
                        if (O.type == OBJ_INTERACT) m_keys.insert(Qt::Key_E);
                        if (O.type == OBJ_HOLD && frames % 40 == 0) for (int i = 0; i < int(m_gs.enemies.size()); i++)
                            if (m_gs.enemies[size_t(i)].alive && !m_gs.enemies[size_t(i)].bossKind) { damageEnemy(i, 99999, PART_BODY, pl.posX, pl.posY, 0.5f); break; }
                        break;
                    case OBJ_ESCORT:
                        if (m_escort >= 0) {
                            Actor &a = m_gs.actors[size_t(m_escort)];
                            a.hp = 160;
                            if (!a.escortMet) place(a.posX + 1.f, a.posY, 3.14159f);
                            else if (frames % 30 == 0 && missionMarker(O.markers[0], tx, ty)) { place(tx + 0.8f, ty, 0.f); a.posX = tx; a.posY = ty; a.footZ = floorH(tx, ty); }
                        }
                        break;
                    case OBJ_BOSS:
                        if (frames % 30 == 0) {
                            for (int i = 0; i < int(m_gs.enemies.size()); i++) {
                                Enemy &e = m_gs.enemies[size_t(i)];
                                if (!e.alive || !e.bossKind) continue;
                                damageEnemy(i, std::max(1, e.maxHp / 6), PART_BODY, e.x, e.y, floorH(e.x, e.y) + 0.5f);
                            }
                            if (!shot && m_bossIntroT > 1.f) { render(); save(QString("97_mission%1_boss").arg(mi + 1)); shot = 1; }
                        }
                        break;
                    }
                }
                advance(1.f / 60.f);
                for (const Enemy &e : m_gs.enemies) if (e.alive && (std::isnan(e.x) || std::isnan(e.y))) nan++;
                if (frames == 240) { render(); save(QString("97_mission%1").arg(mi + 1)); }
                frames++;
            }
            m_keys.erase(Qt::Key_E);
            log(QString("mission %1 %2: debrief=%3 won=%4 rank=%5 coins=%6 objectives=%7/%8 time=%9s nan=%10 trail:%11")
                .arg(mi + 1).arg(MISSIONS[mi].name).arg(m_screen == SCR_DEBRIEF).arg(m_debriefWon).arg(m_debriefRank).arg(m_debriefCoins)
                .arg(m_obj).arg(MISSIONS[mi].objCount).arg(int(m_missionTime)).arg(nan).arg(trail));
            if (mi == 0) { m_debriefT = 3.f; render(); save("97_debrief"); }
        }
        // a failed escort fails the mission
        loadCampaign(); startMission(2); m_flyActive = false;
        for (int f = 0; f < 600 && m_escort < 0; f++) { m_gs.player.hp = 9999; float tx, ty; if (objectiveTarget(tx, ty)) place(tx, ty, 0.f); advance(1.f / 60.f); }
        if (m_escort >= 0) m_gs.actors[size_t(m_escort)].alive = false;
        for (int f = 0; f < 60 * 5 && m_screen == SCR_PLAYING; f++) { m_gs.player.hp = 9999; advance(1.f / 60.f); }
        log(QString("escort killed: failed=%1 screen=debrief:%2 won=%3").arg(m_missionFailed).arg(m_screen == SCR_DEBRIEF).arg(m_debriefWon));
        m_campaignRun = false; m_envLocked = false;
    }

    // --- FRONTIER: 64x64 map, runtime map size --------------------------------------
    start(3);
    log(QString("frontier size: %1x%2 (expect 64x64)").arg(m_mapW).arg(m_mapH));
    place(m_parsed[3].camX, m_parsed[3].camY, 0.6f); run(10); save("35_frontier");
    place(31.5f, 31.5f, 2.4f); run(10); save("36_frontier_tower");
    tm.restart();
    for (int i = 0; i < 120; i++) { advance(1.f / 60.f); render(); }
    log(QString("%1 ms/frame (frontier)").arg(tm.elapsed() / 120.0));
    {
        // A World built on its own (no window) must agree with the game's copy
        World w;
        w.loadLayout(m_parsed[3], TERRAIN_AMP[3], 3, MAP_THEME[3]);
        int diff = 0;
        for (int y = 0; y < 64; y += 3) for (int x = 0; x < 64; x += 3)
            if (std::fabs(w.floorH(x + 0.3f, y + 0.7f) - floorH(x + 0.3f, y + 0.7f)) > 1e-5f) diff++;
        float hx, hy, hz;
        bool blocked = w.traceSegment(2.5f, 2.5f, 0.6f, 31.5f, 31.5f, 0.6f, hx, hy, hz);
        log(QString("standalone World: %1x%2 height mismatches=%3 trace blocked=%4 at (%5,%6)")
            .arg(w.m_mapW).arg(w.m_mapH).arg(diff).arg(blocked).arg(hx, 0, 'f', 1).arg(hy, 0, 'f', 1));
    }
    // --- Kill cam: get shot by a rifleman, replay from its eyes -------------------------
    {
        start(1);
        place(28.5f, 6.5f, 3.1416f);
        for (auto &e : m_gs.enemies) { e.alive = false; e.corpseFade = 0.f; }
        Enemy &k = m_gs.enemies[0];
        k.alive = true; k.corpseFade = 1.f; k.state = ES_ALERT; k.x = 22.f; k.y = 7.5f; k.hp = 9999; k.fireCooldown = 0.f;
        m_gs.enemies[1].alive = true; m_gs.enemies[1].x = 2.5f; m_gs.enemies[1].y = 37.5f;
        m_gs.player.hp = 25;
        for (int f = 0; f < 60 * 12 && !m_gs.gameOver; f++) { k.fireCooldown = std::min(k.fireCooldown, 0.3f); advance(1.f / 60.f); }
        log(QString("killcam: dead=%1 killer=%2 active=%3 frames=%4").arg(m_gs.gameOver).arg(m_killerIdx).arg(m_killcamActive).arg(m_kcLen));
        for (int f = 0; f < 90; f++) advance(1.f / 60.f);
        render(); save("51_killcam");
        for (int f = 0; f < 60 * 6; f++) advance(1.f / 60.f);
        log(QString("killcam finished: active=%1 (expect 0)").arg(m_killcamActive));
    }
    m_gs.player.hp = 0; m_gs.gameOver = true; run(2); save("50_gameover");
    m_gs.gameOver = false;

    // ── Sound bank sanity: every effect non-empty, finite, not silent, not clipping ──
    {
        int bad = 0;
        for (int i = 0; i < SND_COUNT; i++) {
            const std::vector<float> &b = m_snd->buffer(SoundId(i));
            float peak = 0; double rms = 0; bool finite = true;
            for (float x : b) { if (!std::isfinite(x)) finite = false; peak = std::max(peak, std::fabs(x)); rms += double(x) * x; }
            rms = b.empty() ? 0 : std::sqrt(rms / b.size());
            bool ok = !b.empty() && finite && peak > 0.05f && peak <= 1.0f && rms > 0.002;
            if (!ok) { bad++; log(QString("  BAD SOUND %1: len=%2 peak=%3 rms=%4 finite=%5").arg(i).arg(b.size()).arg(peak).arg(rms).arg(finite)); }
        }
        log(QString("sound bank: %1 sounds, %2 bad").arg(int(SND_COUNT)).arg(bad));
        // Real output device, silent: does the sink start and pull mixed audio?
        m_snd->setMasterVolume(0.f);
        bool started = m_snd->start();
        m_snd->play(SND_PISTOL);
        QElapsedTimer at; at.start();
        while (at.elapsed() < 600) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        log(QString("audio device: started=%1 bytesMixed=%2").arg(started).arg(m_snd->bytesServed()));
    }

    // ── Intermission: combat input must not skip it; selection + loadout + deploy work ──
    {
        start(1);
        Player &pl = m_gs.player;
        pl.owned[WPN_SHOTGUN] = true; pl.mag[WPN_SHOTGUN] = 6; pl.reserve[WPN_SHOTGUN] = 12;
        m_gs.zoneKills = 9; m_gs.zoneHeadshots = 5; m_gs.zoneShots = 20; m_gs.zoneHits = 11; m_gs.zoneTime = 140.f; m_gs.zoneDamage = 25;
        for (auto &en : m_gs.enemies) { en.alive = false; en.hp = 0; en.corpseFade = 0; }
        m_gs.zoneBossKilled = true;
        advance(1.f / 60.f);                                     // zone clears this frame
        // The player is still shooting / jumping / pressing 1 at that moment
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(400, 300), QPointF(400, 300), QPointF(400, 300), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        mousePressEvent(&click);
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier), one(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier);
        keyPressEvent(&space); keyPressEvent(&one);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        keyPressEvent(&enter);
        m_keys.clear();
        log(QString("intermission after stray input: level=%1 complete=%2 selected=%3 (expect 1, 1, -1)")
                .arg(m_gs.level).arg(m_gs.levelComplete).arg(m_selectedUpgradeCard));
        for (int i = 0; i < 60; i++) advance(1.f / 60.f);
        render(); save("59_zone_report_tally");
        for (int i = 0; i < 180; i++) advance(1.f / 60.f);
        render(); save("60_zone_report");
        log(QString("zone report: %1 rows, +%2 coins, +%3 score, coins now %4").arg(m_report.size()).arg(m_reportCoins).arg(m_reportScore).arg(m_gs.coins));
        keyPressEvent(&enter); m_keys.clear();                    // -> upgrade page
        for (int i = 0; i < 30; i++) advance(1.f / 60.f);
        render(); save("61_intermission");
        log(QString("page after ENTER on report: %1 (expect 1)").arg(m_ixPage));
        keyPressEvent(&enter); m_keys.clear();
        log(QString("enter without a pick: level=%1 (expect 1)").arg(m_gs.level));
        QKeyEvent two(QEvent::KeyPress, Qt::Key_2, Qt::NoModifier), tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        // pick the rifle unlock card if offered, otherwise card 2
        int unlockIdx = -1;
        for (int i = 0; i < int(m_upgradeCards.size()); i++) if (m_upgradeCards[i].slot == UC_UNLOCK) unlockIdx = i;
        if (unlockIdx >= 0) { QKeyEvent kk(QEvent::KeyPress, Qt::Key(Qt::Key_1 + unlockIdx), Qt::NoModifier); keyPressEvent(&kk); }
        else keyPressEvent(&two);
        m_keys.clear();
        log(QString("picked card %1 (unlock card index %2) -> loadout weapon %3").arg(m_selectedUpgradeCard).arg(unlockIdx).arg(m_loadoutWeapon));
        QKeyEvent seven(QEvent::KeyPress, Qt::Key_7, Qt::NoModifier); keyPressEvent(&seven); m_keys.clear();
        log(QString("shop ammo: coins now %1").arg(m_gs.coins));
        int chosen = m_loadoutWeapon;
        (void)tab;
        m_mouseWidget = QPointF(ixCardRect(1).center()); updateIntermissionHover(m_mouseWidget);
        render(); save("62_intermission_selected");
        float t0 = m_upgradeDecisionTimer;
        for (int i = 0; i < 600; i++) advance(1.f / 60.f);      // 10 s of thinking: must not auto-deploy
        log(QString("after 10 s idle with a pick: level=%1 timer %2 -> %3 (expect level 1)").arg(m_gs.level).arg(t0).arg(m_upgradeDecisionTimer));
        keyPressEvent(&enter); m_keys.clear();
        log(QString("deploy: level=%1 weapon=%2 chosen=%3 owned=%4 mag=%5 complete=%6 (expect 2, chosen, 1, >0, 0)")
                .arg(m_gs.level).arg(pl.curWeapon).arg(chosen).arg(pl.owned[pl.curWeapon]).arg(pl.mag[pl.curWeapon]).arg(m_gs.levelComplete));
        for (int i = 0; i < 40; i++) advance(1.f / 60.f);
        render(); save("63_deployed_with_choice");
        // Boss: look at the commander, then kill it and see the loot beam
        int bi = -1;
        for (int i = 0; i < int(m_gs.enemies.size()); i++) if (m_gs.enemies[i].boss) bi = i;
        log(QString("boss present: %1").arg(bi >= 0));
        if (bi >= 0) {
            Enemy &b = m_gs.enemies[bi];
            b.fireCooldown = 999; b.state = ES_ALERT;
            for (auto &en : m_gs.enemies) en.fireCooldown = 999;
            float ang = frand(0, 6.28f);
            for (int t = 0; t < 32; t++) {
                float a2 = ang + t * 0.196f, px = b.x - std::cos(a2) * 4.5f, py = b.y - std::sin(a2) * 4.5f;
                if (posValid(px, py, 0.3f) && hasLOS(px, py, floorH(px, py) + 0.5f, b.x, b.y, floorH(b.x, b.y) + 0.8f)) { place(px, py, a2); break; }
            }
            m_aimX = W / 2.f; m_aimY = H / 2.f;
            run(5); save("64_boss");
            damageEnemy(bi, 99999, PART_BODY, b.x, b.y, floorH(b.x, b.y) + 0.8f);
            for (int i = 0; i < 90; i++) advance(1.f / 60.f);
            render(); save("65_boss_loot");
            int rewards = 0; for (const auto &wp2 : m_gs.weaponPickups) if (wp2.reward && wp2.active) rewards++;
            log(QString("boss loot: reward guns on the ground = %1").arg(rewards));
        }
        // 3D weapon views
        for (int wv = 0; wv < WPN_COUNT; wv++) {
            pl.owned[wv] = true; pl.curWeapon = wv; pl.switchTimer = 0; pl.reloadTimer = 0; m_pendingWeapon = -1;
            if (usesAmmo(wv) && pl.mag[wv] == 0) pl.mag[wv] = effectiveMagSize(wv);
            for (size_t i = 1; i < m_gs.enemies.size(); i++) m_gs.enemies[i].alive = false;
            run(10); save(QString("8%1_gun3d_%2").arg(wv).arg(WEAPONS[wv].shortName));
        }
        pl.curWeapon = WPN_KNIFE; tryFire(); for (int i = 0; i < 6; i++) advance(1.f / 60.f); render(); save("84_knife_slash");
        pl.curWeapon = WPN_RIFLE; pl.attackCooldown = 0; tryFire(); for (int i = 0; i < 2; i++) advance(1.f / 60.f); render(); save("85_rifle_fire");
        pl.curWeapon = WPN_RIFLE; pl.mag[WPN_RIFLE] = 3; pl.reserve[WPN_RIFLE] = 60; startReload(); for (int i = 0; i < 50; i++) advance(1.f / 60.f); render(); save("86_rifle_reload");
    }

    // ── Bot playtest: random movement + aiming at enemies, invariants checked every frame ──
    for (int map = 0; map < MAP_COUNT; map++) {
        m_mapChoice = map; m_charChoice = 0; m_difficulty = 2;
        initGame(); m_screen = SCR_PLAYING;
        Player &pl = m_gs.player;
        pl.hp = 100000;
        int nan = 0, inWall = 0, underGround = 0, enemyInWall = 0, enemyUnreach = 0, stuck = 0, hardStuck = 0, levels = 0, kills0 = 0;
        for (const Enemy &e : m_gs.enemies)
            if (!e.elite && !reachable(int(e.x), int(e.y))) enemyUnreach++;
        float stuckT = 0, lastX = pl.posX, lastY = pl.posY;
        unsigned rng = 1234u + unsigned(map);
        auto rnd = [&rng]() { rng = rng * 1103515245u + 12345u; return int((rng >> 16) & 0x7FFF); };
        float keyT = 0;
        for (int f = 0; f < 60 * 75; f++) {
            if (keyT <= 0) {
                m_keys.clear();
                int r = rnd() % 6;
                if (r != 5) m_keys.insert(Qt::Key_W);
                if (r == 1) m_keys.insert(Qt::Key_A);
                if (r == 2) m_keys.insert(Qt::Key_D);
                if (r == 3) m_keys.insert(Qt::Key_Shift);
                if (rnd() % 4 == 0 && m_gs.player.onGround) { m_gs.player.velZ = 5.5f; m_gs.player.onGround = false; }
                turn(float(rnd() % 200 - 100) / 100.f * 1.2f);
                keyT = 0.6f + float(rnd() % 100) / 80.f;
            }
            keyT -= 1.f / 60.f;
            // aim toward the nearest visible enemy and fire
            float best = 1e9f; int bi = -1;
            for (int i = 0; i < int(m_gs.enemies.size()); i++) {
                const Enemy &e = m_gs.enemies[i];
                if (!e.alive) continue;
                float dx = e.x - pl.posX, dy = e.y - pl.posY, d = dx * dx + dy * dy;
                if (d < best && hasLOS(pl.posX, pl.posY, m_gs.player.footZ + 0.5f, e.x, e.y, floorH(e.x, e.y) + 0.6f)) { best = d; bi = i; }
            }
            if (bi >= 0) {
                const Enemy &e = m_gs.enemies[bi];
                float want = std::atan2(e.y - pl.posY, e.x - pl.posX), have = std::atan2(pl.dirY, pl.dirX);
                float dd = want - have; while (dd > 3.14159f) dd -= 6.28318f; while (dd < -3.14159f) dd += 6.28318f;
                turn(rs::clampf(dd, -0.08f, 0.08f));
                m_aimX = W / 2.f; m_aimY = H / 2.f;
                if (std::fabs(dd) < 0.1f) { if (pl.mag[pl.curWeapon] == 0 && pl.reserve[pl.curWeapon] == 0) pl.reserve[pl.curWeapon] = 60; tryFire(); }
            }
            advance(1.f / 60.f);
            if (m_gs.levelComplete && m_intermissionT > 1.f) {           // clear the zone like a player would
                levels++;
                m_selectedUpgradeCard = 0;
                deployNextZone();
                for (const Enemy &e : m_gs.enemies)
                    if (!e.elite && !reachable(int(e.x), int(e.y))) enemyUnreach++;
            }
            if (f % 6 == 0) render();
            // invariants
            if (!std::isfinite(pl.posX) || !std::isfinite(pl.posY) || !std::isfinite(m_gs.player.footZ) || !std::isfinite(m_pitch)) nan++;
            if (!posValid(pl.posX, pl.posY, 0.2f)) inWall++;
            if (m_gs.player.footZ < floorH(pl.posX, pl.posY) - 0.05f) underGround++;
            for (const Enemy &e : m_gs.enemies) if (e.alive && !posValid(e.x, e.y, 0.15f)) enemyInWall++;
            bool pushing = m_keys.count(Qt::Key_W) > 0;
            float mv = std::hypot(pl.posX - lastX, pl.posY - lastY);
            lastX = pl.posX; lastY = pl.posY;
            if (pushing && mv < 0.002f) stuckT += 1.f / 60.f; else stuckT = 0;
            if (stuckT > 2.f) {
                stuck++; stuckT = 0;
                int free = 0;                                    // can the player move in ANY direction?
                for (int k = 0; k < 16; k++) {
                    float an = k * 0.3927f;
                    if (canStep(pl.posX + std::cos(an) * 0.06f, pl.posY + std::sin(an) * 0.06f, m_gs.player.footZ, 0.76f, 0.28f, pl.posX, pl.posY)) free++;   // a jump clears 0.76
                }
                if (free == 0) { hardStuck++; log(QString("  HARD STUCK at (%1,%2) z=%3").arg(pl.posX).arg(pl.posY).arg(m_gs.player.footZ)); }
                turn(2.5f);
            }
        }
        kills0 = m_gs.kills;
        m_keys.clear();
        save(QString("7%1_bot_%2").arg(map).arg(MAP_NAMES[map]));
        log(QString("bot %1: kills=%2 zones=%3  nan=%4 inWall=%5 underGround=%6 enemyInWall=%7 unreachableSpawns=%8 stuckEvents=%9 hardStuck=%10")
                .arg(MAP_NAMES[map]).arg(kills0).arg(levels).arg(nan).arg(inWall).arg(underGround).arg(enemyInWall).arg(enemyUnreach).arg(stuck).arg(hardStuck));
    }
    return true;
}
