// =============================================================================
//  bosses.cpp  -  Story bosses: one framework, nine fights
//
//  Every boss is an Enemy with a bossKind. They share: phases at 66% / 33%
//  health (pattern changes, speed-up, reinforcements), telegraphed attacks
//  (a warning shape first, then the hit, then a recovery window), weak
//  points (armour, shields, flanks), a health bar with phase notches and a
//  guaranteed loot drop.
//
//    Warden          armoured charger: telegraphed charge (stunned if it hits
//                    a wall), ground-slam shockwave you jump over, shotgun
//    Bresenham Twins two snipers on opposite perches; each aim is a laser line
//                    drawn with Bresenham, locks, fires, then they relocate
//    Seed Queen      plants egg sacs whose acid grows across the floor cell by
//                    cell (breadth-first seed fill); kill the eggs to undo it
//    Gunship         aerial: strafing runs along a marked line, homing missiles
//    Twin Guards     a heavy and a medic; the medic heals the heavy
//    Mirage          cloaked: a shimmer; blinks behind you, spawns decoys
//    Engineer        shield pylons make him invulnerable; deploys turrets
//    Juggernaut      walking tank: minigun spin-up, mortar barrages; armoured
//                    front, weak back
//    Helix Founder   finale (VECTOR): shielded mech -> laser walls + dashes ->
//                    overload floor + decoys (only the real one casts a shadow)
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static inline float frandB() { return float(rand()) / float(RAND_MAX); }
static inline float frandB(float a, float b) { return a + (b - a) * frandB(); }
static const float PI_B = 3.14159265f;

struct BossDef { const char *name, *title; int atlas; float hpMult, scale, armor; bool minor; };
static const BossDef BOSSES[BK_COUNT] = {
    {"", "", 0, 1, 1, 0, true},
    {"THE WARDEN",       "ARMOURED CHARGER",        0, 9.0f, 1.55f, 0.5f, false},
    {"TWIN: BRES",       "SNIPER",                  1, 3.0f, 1.2f,  0.0f, false},
    {"TWIN: ENHAM",      "SNIPER",                  2, 3.0f, 1.2f,  0.0f, false},
    {"THE SEED QUEEN",   "BROOD MOTHER",            3, 8.0f, 1.6f,  0.2f, false},
    {"EGG SAC",          "",                        4, 0.9f, 0.7f,  0.0f, true},
    {"GUNSHIP",          "AERIAL DRONE",            5, 7.0f, 2.4f,  0.2f, false},
    {"GUARD: HAMMER",    "HEAVY",                   6, 6.0f, 1.4f,  0.3f, false},
    {"GUARD: MERCY",     "MEDIC",                   7, 3.5f, 1.25f, 0.0f, false},
    {"MIRAGE",           "THE GHOST",               8, 6.5f, 1.3f,  0.0f, false},
    {"THE ENGINEER",     "SHIELD SPECIALIST",       9, 6.0f, 1.3f,  0.0f, false},
    {"SHIELD PYLON",     "",                        10, 1.6f, 1.0f, 0.0f, true},
    {"JUGGERNAUT",       "WALKING TANK",            11, 13.0f, 1.9f, 0.5f, false},
    {"VECTOR",           "THE HELIX FOUNDER",       12, 18.0f, 1.8f, 0.3f, false},
};

const char *MainWindow::bossName(int kind) const { return BOSSES[clampi(kind, 0, BK_COUNT - 1)].name; }
const char *MainWindow::bossTitle(int kind) const { return BOSSES[clampi(kind, 0, BK_COUNT - 1)].title; }
bool MainWindow::bossIsMinor(int kind) const { return BOSSES[clampi(kind, 0, BK_COUNT - 1)].minor; }

// Boss looks (painted with the soldier painter at start-up)
static const SoldierLook WARDEN_LOOK   = { rgb(70,52,40), rgb(46,32,24), rgb(120,60,30), rgb(90,70,50), rgb(60,44,30), rgb(110,40,24), rgb(196,150,116), rgb(30,24,20), rgb(255,120,30), rgb(34,26,20), rgb(40,30,24) };
static const SoldierLook TWIN_A_LOOK   = { rgb(60,74,52), rgb(40,50,34), rgb(84,100,70), rgb(46,58,40), rgb(30,38,26), rgb(52,64,44), rgb(180,138,104), rgb(28,34,26), rgb(255,40,40), rgb(26,30,22), rgb(24,28,20) };
static const SoldierLook TWIN_B_LOOK   = { rgb(74,60,52), rgb(50,40,34), rgb(100,84,70), rgb(58,46,40), rgb(38,30,26), rgb(64,52,44), rgb(180,138,104), rgb(34,28,26), rgb(40,140,255), rgb(30,26,22), rgb(28,24,20) };
static const SoldierLook QUEEN_LOOK    = { rgb(60,90,50), rgb(36,60,30), rgb(110,160,60), rgb(70,110,50), rgb(40,70,30), rgb(90,140,60), rgb(160,190,120), rgb(30,50,24), rgb(190,255,80), rgb(30,44,22), rgb(40,60,30) };
static const SoldierLook HAMMER_LOOK   = { rgb(80,40,40), rgb(54,26,26), rgb(110,60,50), rgb(70,36,32), rgb(46,22,20), rgb(90,40,36), rgb(180,138,106), rgb(30,20,20), rgb(255,90,40), rgb(28,20,18), rgb(26,18,16) };
static const SoldierLook MERCY_LOOK    = { rgb(220,222,226), rgb(160,164,170), rgb(240,240,244), rgb(60,140,200), rgb(40,100,150), rgb(236,238,242), rgb(196,150,116), rgb(70,72,76), rgb(80,255,140), rgb(70,72,76), rgb(60,62,66) };
static const SoldierLook MIRAGE_LOOK   = { rgb(170,178,190), rgb(120,128,140), rgb(200,206,216), rgb(140,148,160), rgb(100,106,118), rgb(190,196,206), rgb(170,140,120), rgb(60,64,72), rgb(140,220,255), rgb(80,84,92), rgb(90,94,102) };
static const SoldierLook ENGINEER_LOOK = { rgb(210,120,30), rgb(150,80,20), rgb(230,150,50), rgb(60,60,64), rgb(40,40,44), rgb(230,190,40), rgb(196,150,116), rgb(40,40,44), rgb(80,220,255), rgb(40,36,30), rgb(50,46,40) };
static const SoldierLook JUGGER_LOOK   = { rgb(64,66,52), rgb(42,44,34), rgb(90,92,74), rgb(70,72,58), rgb(48,50,40), rgb(60,62,50), rgb(170,130,100), rgb(26,26,24), rgb(255,60,30), rgb(24,24,22), rgb(30,30,28) };
static const SoldierLook FOUNDER_LOOK  = { rgb(30,30,36), rgb(18,18,22), rgb(200,160,60), rgb(40,40,48), rgb(24,24,30), rgb(210,170,70), rgb(180,140,110), rgb(16,16,20), rgb(255,60,200), rgb(20,20,24), rgb(200,160,60) };

void MainWindow::bakeBossAtlases()
{
    bakeEnemyAtlas(m_bossAtlas[0], m_bossMask[0], WARDEN_LOOK, GEAR_NONE, true);
    bakeEnemyAtlas(m_bossAtlas[1], m_bossMask[1], TWIN_A_LOOK);
    bakeEnemyAtlas(m_bossAtlas[2], m_bossMask[2], TWIN_B_LOOK);
    bakeEnemyAtlas(m_bossAtlas[3], m_bossMask[3], QUEEN_LOOK, GEAR_GRENADIER, true);
    bakeMachineAtlas(m_bossAtlas[4], m_bossMask[4], 2);                // egg sac
    bakeMachineAtlas(m_bossAtlas[5], m_bossMask[5], 0);                // gunship (big drone)
    bakeEnemyAtlas(m_bossAtlas[6], m_bossMask[6], HAMMER_LOOK, GEAR_HEAVY, true);
    bakeEnemyAtlas(m_bossAtlas[7], m_bossMask[7], MERCY_LOOK, GEAR_MEDIC);
    bakeEnemyAtlas(m_bossAtlas[8], m_bossMask[8], MIRAGE_LOOK, GEAR_STALKER);
    bakeEnemyAtlas(m_bossAtlas[9], m_bossMask[9], ENGINEER_LOOK, GEAR_GRENADIER);
    bakeMachineAtlas(m_bossAtlas[10], m_bossMask[10], 3);              // shield pylon
    bakeEnemyAtlas(m_bossAtlas[11], m_bossMask[11], JUGGER_LOOK, GEAR_HEAVY, true);
    bakeEnemyAtlas(m_bossAtlas[12], m_bossMask[12], FOUNDER_LOOK, GEAR_HEAVY, true);
}

const std::vector<uint32_t> *MainWindow::bossAtlas(const Enemy &e) const { return e.bossKind ? &m_bossAtlas[BOSSES[e.bossKind].atlas] : nullptr; }
const std::vector<uint8_t> *MainWindow::bossMask(const Enemy &e) const { return e.bossKind ? &m_bossMask[BOSSES[e.bossKind].atlas] : nullptr; }

// ─────────────────────────────────────────────────────────────────────────────
//  Spawning
// ─────────────────────────────────────────────────────────────────────────────
int MainWindow::spawnBoss(int kind, float x, float y, float hpScale)
{
    const BossDef &B = BOSSES[kind];
    EnemyType base = kind == BK_GUNSHIP ? ET_DRONE : kind == BK_EGG || kind == BK_PYLON ? ET_TURRET : ET_RIFLEMAN;
    spawnEnemy(x, y, !B.minor, base);
    int idx = int(m_gs.enemies.size()) - 1;
    Enemy &e = m_gs.enemies.back();
    e.bossKind = kind;
    e.boss = !B.minor;
    e.scale = B.scale;
    e.armor = B.armor;
    e.hp = e.maxHp = std::max(1, int(enemyMaxHP() * B.hpMult * hpScale));
    e.aggroRange += 8.f;
    e.state = B.minor ? ES_ALERT : ES_IDLE;
    e.hover = kind == BK_GUNSHIP ? 2.4f : 0.f;
    e.atkT = 1.5f;
    e.cloak = kind == BK_MIRAGE ? 1.f : 0.f;
    e.alertDirX = 1.f; e.alertDirY = 0.f;           // facing (Juggernaut)
    if (!B.minor) m_gs.zoneHadBoss = true;
    // companions set up with the boss
    if (kind == BK_ENGINEER || kind == BK_FOUNDER) {
        int n = kind == BK_FOUNDER ? 4 : 3;
        for (int k = 0; k < n; k++) {
            float a = k * 2.f * PI_B / n + 0.4f, r = kind == BK_FOUNDER ? 6.f : 5.f;
            float px = x + std::cos(a) * r, py = y + std::sin(a) * r;
            for (int t = 0; t < 10 && !posValid(px, py, 0.35f); t++) { r *= 0.8f; px = x + std::cos(a) * r; py = y + std::sin(a) * r; }
            if (posValid(px, py, 0.3f)) { int pi = spawnBoss(BK_PYLON, px, py, 1.f); m_gs.enemies[size_t(pi)].partner = idx; }
        }
        m_gs.enemies[size_t(idx)].shielded = true;
    }
    return idx;
}

// Twins and guards come as pairs that know each other
void MainWindow::spawnBossPair(int kindA, float ax, float ay, int kindB, float bx, float by)
{
    int a = spawnBoss(kindA, ax, ay, 1.f), b = spawnBoss(kindB, bx, by, 1.f);
    m_gs.enemies[size_t(a)].partner = b;
    m_gs.enemies[size_t(b)].partner = a;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shared helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::hurtPlayer(int dmg, float fromX, float fromY, int killer)
{
    Player &pl = m_gs.player;
    if (m_gs.gameOver) return;
    dmg = std::max(1, int(dmg * (1.f - pl.damageReduction) * DIFF_CFG[m_difficulty].dmgReceivedMult));
    pl.hp -= dmg; m_gs.zoneDamage += dmg; pl.damageFlash = 0.5f; pl.shakeY -= 6.f;
    addDamageMark(fromX, fromY, std::min(1.f, dmg / 25.f));
    if (pl.hp <= 0) {
        pl.hp = 0; pl.adsMode = false; m_killerIdx = killer;
        if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
            m_net->handleDeathDrop(m_net->localPlayerId(), killer, false, m_gs);
            QTimer::singleShot(5000, [this]() {
                if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
                    m_net->handleRespawn(m_net->localPlayerId(), m_gs, *this, m_parsed[m_gs.mapChoice]);
                }
            });
        } else {
            m_gs.gameOver = true;
        }
    }
}


static bool onGroundNear(float footZ, float ground) { return footZ < ground + 0.25f; }

// A high-velocity enemy round straight along (dx, dy)
void MainWindow::bossShot(Enemy &e, float tx, float ty, float tz, float speed, int dmg, float spread)
{
    float z0 = floorH(e.x, e.y) + 0.65f * e.scale + e.hover;
    float dx = tx - e.x, dy = ty - e.y, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
    float a = std::atan2(dy, dx) + frandB(-spread, spread);
    Bullet b; b.x = e.x + std::cos(a) * 0.4f; b.y = e.y + std::sin(a) * 0.4f; b.z = z0;
    b.px = b.x; b.py = b.y; b.pz = b.z;
    b.dx = std::cos(a) * speed; b.dy = std::sin(a) * speed; b.dz = (tz - z0) / (d / speed);
    b.owner = ENEMY_SHOT; b.damage = dmg; b.shooter = int(&e - m_gs.enemies.data());
    m_gs.bullets.push_back(b);
}

void MainWindow::bossReinforce(Enemy &e, int count, EnemyType t)
{
    for (int k = 0; k < count; k++) {
        for (int tries = 0; tries < 30; tries++) {
            float a = frandB(0, 2 * PI_B), r = frandB(4.f, 8.f);
            float x = e.x + std::cos(a) * r, y = e.y + std::sin(a) * r;
            int cx = int(x), cy = int(y);
            if (!inside(cx, cy) || solidCell(cx, cy) || m_acid[cy][cx] || !posValid(x, y, 0.3f) || !reachable(cx, cy)) continue;
            spawnEnemy(x, y, t == ET_RIFLEMAN, t);
            m_gs.enemies.back().state = ES_ALERT;
            break;
        }
    }
}

// Damage notifications: phase changes, Twin Guard heals, VECTOR's guard calls
void MainWindow::bossDamaged(int idx, int dmg)
{
    Enemy &e = m_gs.enemies[size_t(idx)];
    if (bossIsMinor(e.bossKind)) return;
    e.state = ES_ALERT;
    float f = float(std::max(0, e.hp)) / float(std::max(1, e.maxHp));
    int ph = f > 0.66f ? 0 : f > 0.33f ? 1 : 2;
    if (e.decoy) return;
    if (e.bossKind == BK_FOUNDER) {
        e.burstT += float(dmg);                               // damage taken since the last guard call
        if (e.phase == 1 && e.burstT > 220.f) { e.burstT = 0.f; bossReinforce(e, 2, ET_RIFLEMAN); pushFeed("VECTOR: \"GUARDS!\"", 2.5f); }
    }
    if (ph > e.phase && e.hp > 0) {
        e.phase = ph;
        e.atk = -1; e.atkT = 1.2f;
        sfx(SND_RADIO, 0.8f); sfx(SND_ENEMY_ALERT, 0.9f, 0.7f);
        m_bossBanner = 2.5f;
        static const char *P[3] = { "", "PHASE 2", "PHASE 3" };
        pushFeed(QString("%1 - %2").arg(bossName(e.bossKind)).arg(P[ph]), 3.f, true);
        switch (e.bossKind) {
        case BK_WARDEN: bossReinforce(e, ph, ET_SHOTGUNNER); break;
        case BK_SEED_QUEEN: bossReinforce(e, 2, ET_DRONE); break;
        case BK_GUNSHIP: bossReinforce(e, 2, ET_DRONE); break;
        case BK_MIRAGE: for (int k = 0; k < 1 + ph; k++) spawnDecoy(idx); break;
        case BK_JUGGERNAUT: bossReinforce(e, 2, ET_HEAVY); break;
        case BK_FOUNDER:
            if (ph == 1) { e.scale = 1.3f; e.armor = 0.f; pushFeed("VECTOR LEAVES THE MECH", 3.f, true); explode(e.x, e.y, floorH(e.x, e.y) + 0.6f, 1.6f, 0.f, PO_ENEMY, false); }
            if (ph == 2) { for (int k = 0; k < 3; k++) spawnDecoy(idx); pushFeed("VECTOR OVERLOADS THE CORE", 3.f, true); }
            break;
        default: break;
        }
    }
}

// A copy that looks the same but dies in one hit (and casts no shadow)
void MainWindow::spawnDecoy(int idx)
{
    Enemy src = m_gs.enemies[size_t(idx)];
    for (int tries = 0; tries < 20; tries++) {
        float a = frandB(0, 2 * PI_B), r = frandB(2.5f, 5.f);
        float x = src.x + std::cos(a) * r, y = src.y + std::sin(a) * r;
        if (!posValid(x, y, 0.35f) || m_acid[int(y)][int(x)] || !reachable(int(x), int(y))) continue;
        Enemy d = src;
        d.x = x; d.y = y; d.decoy = true; d.hp = d.maxHp = 1; d.boss = false; d.elite = false; d.shielded = false; d.partner = -1;
        d.atkT = frandB(0.5f, 2.f); d.atk = -1;
        m_gs.enemies.push_back(d);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-boss behaviour (called from updateEnemies once the target is known)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::updateBoss(int idx, float dt, float tgX, float tgY, float tgZ, float d, bool los)
{
    Enemy &e = m_gs.enemies[size_t(idx)];
    Player &pl = m_gs.player;
    const float px = pl.posX, py = pl.posY;
    float dx = tgX - e.x, dy = tgY - e.y;
    float ux = d > 0.01f ? dx / d : 0.f, uy = d > 0.01f ? dy / d : 0.f;
    float speedUp = 1.f + 0.25f * e.phase;
    if (e.decoy) speedUp *= 0.8f;
    e.atkT -= dt;
    float vx = 0, vy = 0, spd = 0;
    bool alert = e.state == ES_ALERT || bossIsMinor(e.bossKind);
    if (!alert) { moveEnemy(e, idx, 0, 0, 0, dt); return; }
    float myGround = floorH(e.x, e.y);
    (void)myGround;

    switch (e.bossKind) {
    // ── Minor parts: eggs spread acid, pylons hold a shield up ─────────────────────
    case BK_EGG: case BK_PYLON:
        break;

    // ── THE WARDEN ───────────────────────────────────────────────────────────────────
    case BK_WARDEN: {
        e.armor = e.stunT > 0.f ? 0.f : 0.5f;                    // plates only cover it while it's on its feet
        if (e.atk == 0) {                                        // CHARGE
            e.atkPhaseT += dt;
            if (e.atkPhaseT < 0.9f / speedUp) { spd = 0; }       // telegraph: a red stripe on the floor
            else if (e.atkPhaseT < 2.6f) {
                float ox = e.x, oy = e.y;
                float step = 8.5f * speedUp * dt;
                float nx = e.x + e.tx * step, ny = e.y + e.ty * step;
                bool blocked = !canStep(nx, ny, floorH(e.x, e.y), 0.35f, 0.4f);
                if (blocked) {                                   // slammed into a wall: dazed, plates open
                    e.stunT = 2.4f; e.atk = -1; e.atkT = 2.6f;
                    sfxAt(SND_EXPLOSION, e.x, e.y, 0.5f, 1.6f);
                    for (int k = 0; k < 8; k++) spawnImpact(e.x + e.tx * 0.6f, e.y + e.ty * 0.6f, floorH(e.x, e.y) + frandB(0.2f, 1.2f), false);
                    pl.shakeY += 6.f;
                    pushFeed("THE WARDEN IS DAZED - HIT IT NOW", 2.f);
                } else { e.x = nx; e.y = ny; e.walkPhase += step * 1.5f; e.moving = true; }
                if ((px - e.x) * (px - e.x) + (py - e.y) * (py - e.y) < 1.0f) {
                    hurtPlayer(30 + 6 * m_gs.level, e.x, e.y, idx);
                    pl.velX += e.tx * 9.f; pl.velY += e.ty * 9.f; pl.velZ = 3.f; pl.onGround = false;
                    e.atk = -1; e.atkT = 1.8f;
                }
                (void)ox; (void)oy;
            } else { e.atk = -1; e.atkT = 1.5f; }
            return;
        }
        if (e.atk == 1) {                                        // GROUND SLAM: jump the shockwave
            e.atkPhaseT += dt;
            if (e.atkPhaseT >= 0.8f) {
                float r = (e.atkPhaseT - 0.8f) * 6.5f;
                float pd = std::sqrt((px - e.x) * (px - e.x) + (py - e.y) * (py - e.y));
                if (!e.decoy && e.burstLeft == 0 && std::fabs(pd - r) < 0.45f && onGroundNear(pl.footZ, floorH(px, py))) {
                    hurtPlayer(22 + 5 * m_gs.level, e.x, e.y, idx); e.burstLeft = 1;
                }
                if (r > 6.f) { e.atk = -1; e.atkT = 1.4f / speedUp; e.burstLeft = 0; }
            } else if (e.atkPhaseT < dt * 1.5f) { sfxAt(SND_MELEE_HEAVY, e.x, e.y, 1.f, 0.6f); }
            if (e.atkPhaseT >= 0.8f && e.atkPhaseT < 0.8f + dt * 1.5f) { sfxAt(SND_EXPLOSION, e.x, e.y, 0.8f, 0.7f); pl.shakeY += 5.f; }
            return;
        }
        if (e.atkT <= 0.f && los && e.stunT <= 0.f) {
            if (d < 4.f) { e.atk = 1; e.atkPhaseT = 0.f; e.burstLeft = 0; }
            else if (d < 14.f) { e.atk = 0; e.atkPhaseT = 0.f; e.tx = ux; e.ty = uy; sfxAt(SND_ENEMY_ALERT, e.x, e.y, 1.f, 0.6f); }
            break;
        }
        if (los && d < 9.f && e.fireCooldown <= 0.f) { e.type = ET_SHOTGUNNER; enemyFire(e, tgX, tgY, tgZ); e.type = ET_RIFLEMAN; e.shootAnim = 0.25f; e.fireCooldown = 1.6f / speedUp; }
        vx = ux; vy = uy; spd = d > 2.5f ? 0.8f * speedUp : 0.f;
        break;
    }

    // ── THE BRESENHAM TWINS ──────────────────────────────────────────────────────────
    case BK_TWIN_A: case BK_TWIN_B: {
        bool enraged = e.partner >= 0 && !m_gs.enemies[size_t(e.partner)].alive;
        float lockT = enraged ? 0.8f : 1.4f;
        if (e.atk == 0) {                                        // AIM: laser line, locks, fires
            e.atkPhaseT += dt;
            if (e.atkPhaseT < lockT - 0.3f) { e.tx = tgX; e.ty = tgY; }   // tracking; the last 0.3 s it holds still
            if (!los && e.atkPhaseT < lockT - 0.3f) { e.atk = -1; e.atkT = 0.6f; }
            if (e.atkPhaseT >= lockT) {
                bossShot(e, e.tx, e.ty, tgZ, 55.f, int((24 + 6 * m_gs.level)), 0.f);
                sfxAt(SND_SNIPER, e.x, e.y, 1.f, 0.95f);
                e.shootAnim = 0.3f;
                e.atk = 1; e.atkPhaseT = 0.f;
            }
            return;
        }
        if (e.atk == 1) {                                        // RELOCATE to another perch
            e.atkPhaseT += dt;
            if (e.atkPhaseT > 0.7f) {
                float bestX = e.x, bestY = e.y, best = -1.f;
                for (int t = 0; t < 40; t++) {
                    int cx = 2 + rand() % (m_mapW - 4), cy = 2 + rand() % (m_mapH - 4);
                    float fx = cx + 0.5f, fy = cy + 0.5f;
                    if (solidCell(cx, cy) || m_acid[cy][cx] || !posValid(fx, fy, 0.3f) || !reachable(cx, cy)) continue;
                    float h = m_plat[cy][cx];
                    float dp = std::sqrt((fx - px) * (fx - px) + (fy - py) * (fy - py));
                    if (dp < 9.f || dp > 24.f) continue;
                    float score = h * 3.f + frandB();
                    if (e.partner >= 0) {                        // keep the twins apart
                        const Enemy &o = m_gs.enemies[size_t(e.partner)];
                        score += std::min(6.f, std::sqrt((fx - o.x) * (fx - o.x) + (fy - o.y) * (fy - o.y))) * 0.3f;
                    }
                    if (score > best) { best = score; bestX = fx; bestY = fy; }
                }
                for (int k = 0; k < 6; k++) {
                    Particle p; p.type = PT_SMOKE; p.x = e.x; p.y = e.y; p.z = floorH(e.x, e.y) + 0.5f;
                    p.vx = frandB(-0.4f, 0.4f); p.vy = frandB(-0.4f, 0.4f); p.vz = 0.4f; p.life = p.maxLife = 1.2f; p.size = 0.2f; p.color = rgb(150, 150, 150);
                    m_gs.particles.push_back(p);
                }
                e.x = bestX; e.y = bestY;
                e.atk = -1; e.atkT = enraged ? 0.5f : 1.2f;
            }
            return;
        }
        if (e.atkT <= 0.f && los && d < 30.f) { e.atk = 0; e.atkPhaseT = 0.f; sfxAt(SND_SCOPE_IN, e.x, e.y, 0.8f); }
        break;
    }

    // ── THE SEED QUEEN ───────────────────────────────────────────────────────────────
    case BK_SEED_QUEEN: {
        int eggs = 0;
        for (const Enemy &o : m_gs.enemies) if (o.alive && o.bossKind == BK_EGG) eggs++;
        if (e.atkT <= 0.f) {
            if (eggs < 2 + e.phase && frandB() < 0.6f) {          // PLANT an egg sac nearby
                for (int t = 0; t < 30; t++) {
                    float a = frandB(0, 2 * PI_B), r = frandB(2.5f, 8.f);
                    float x = e.x + std::cos(a) * r, y = e.y + std::sin(a) * r;
                    int cx = int(x), cy = int(y);
                    if (!inside(cx, cy) || solidCell(cx, cy) || m_acid[cy][cx] || !posValid(x, y, 0.3f) || !reachable(cx, cy)) continue;
                    if ((x - px) * (x - px) + (y - py) * (y - py) < 4.f) continue;
                    int ei = spawnBoss(BK_EGG, cx + 0.5f, cy + 0.5f, 1.f + 0.3f * e.phase);
                    m_seeds.push_back(SeedSpread{ei, {}, {cy * m_mapW + cx}, 0.f});
                    sfxAt(SND_GRENADE_BOUNCE, x, y, 0.8f, 0.5f);
                    pushFeed("AN EGG SAC TAKES ROOT - SHOOT IT", 2.f);
                    break;
                }
            } else if (los) {                                    // SPIT an acid glob (lands as a pool)
                Enemy tmp = e;
                enemyGrenade(e, tgX, tgY);
                m_gs.projectiles.back().incendiary = true;       // marks an acid glob
                m_gs.projectiles.back().splashDmg *= 0.6f;
                e.shootAnim = 0.3f;
                (void)tmp;
            }
            e.atkT = (2.2f - 0.4f * e.phase);
        }
        if (d > 6.f || !los) { vx = ux; vy = uy; spd = 0.6f; }
        else { vx = -uy; vy = ux; spd = 0.5f; }
        break;
    }

    // ── GUNSHIP ──────────────────────────────────────────────────────────────────────
    case BK_GUNSHIP: {
        e.hover += (2.6f + 0.25f * std::sin(m_time) - e.hover) * std::min(1.f, dt * 2.f);
        if (e.atk == 0) {                                        // STRAFING RUN along a marked line
            e.atkPhaseT += dt;
            if (e.atkPhaseT > 1.0f) {
                float k = (e.atkPhaseT - 1.0f) / 1.6f;
                float sx = e.tx - e.ty * 0.f, sy = e.ty;
                (void)sx; (void)sy;
                // fly from the run start toward the run end, raking the ground below
                float ax = m_bossRunAX + (m_bossRunBX - m_bossRunAX) * k, ay = m_bossRunAY + (m_bossRunBY - m_bossRunAY) * k;
                e.x += (ax - e.x) * std::min(1.f, dt * 6.f); e.y += (ay - e.y) * std::min(1.f, dt * 6.f);
                e.burstT -= dt;
                if (e.burstT <= 0.f) {
                    e.burstT = 0.07f;
                    float gx = e.x + frandB(-0.5f, 0.5f), gy = e.y + frandB(-0.5f, 0.5f);
                    spawnImpact(gx, gy, floorH(gx, gy), true);
                    addDecal(m_scorch, gx, gy, 0.05f, 140);
                    sfxAt(SND_TURRET_FIRE, e.x, e.y, 0.5f, 0.8f);
                    if ((px - gx) * (px - gx) + (py - gy) * (py - gy) < 0.9f) hurtPlayer(9 + 2 * m_gs.level, e.x, e.y, idx);
                }
                if (k >= 1.f) { e.atk = -1; e.atkT = 2.5f / speedUp; }
            }
            return;
        }
        if (e.atkT <= 0.f) {
            if (frandB() < 0.5f) {
                e.atk = 0; e.atkPhaseT = 0.f;
                // the run passes over the player, starting 8 units to one side
                float a = frandB(0, 2 * PI_B);
                m_bossRunAX = px - std::cos(a) * 8.f; m_bossRunAY = py - std::sin(a) * 8.f;
                m_bossRunBX = px + std::cos(a) * 8.f; m_bossRunBY = py + std::sin(a) * 8.f;
                sfxAt(SND_JETPACK, e.x, e.y, 0.9f, 0.6f);
                pushFeed("STRAFING RUN - GET OFF THE LINE", 1.5f);
            } else {                                             // MISSILES: slow, homing, dodgeable
                for (int k = 0; k < 2 + e.phase; k++) {
                    Projectile p; p.x = e.x; p.y = e.y; p.z = floorH(e.x, e.y) + e.hover;
                    float a = std::atan2(py - e.y, px - e.x) + (k - 0.5f) * 0.6f;
                    p.vx = std::cos(a) * 7.f; p.vy = std::sin(a) * 7.f; p.vz = 1.f;
                    p.kind = PJ_ROCKET; p.owner = PO_ENEMY; p.gravity = 0.f; p.life = 6.f;
                    p.splashR = 2.2f; p.splashDmg = float(20 + 6 * m_gs.level);
                    m_gs.projectiles.push_back(p);
                }
                sfxAt(SND_ROCKET_FIRE, e.x, e.y, 0.9f);
                e.atkT = 3.f / speedUp;
            }
            return;
        }
        // circle the player at range
        float want = 9.f;
        vx = -uy + ux * (d - want) * 0.3f; vy = ux + uy * (d - want) * 0.3f; spd = 1.8f * speedUp;
        break;
    }

    // ── TWIN GUARDS ──────────────────────────────────────────────────────────────────
    case BK_GUARD_HEAVY: {
        if (e.burstLeft > 0) {
            e.burstT -= dt;
            if (e.burstT <= 0.f && los) { bossShot(e, tgX + frandB(-0.6f, 0.6f), tgY + frandB(-0.6f, 0.6f), tgZ, m_gs.enemyBulletSpeed * 1.2f, 5 + m_gs.level, 0.07f); sfxAt(SND_LMG, e.x, e.y, 0.8f); e.burstLeft--; e.burstT = 0.1f; e.shootAnim = 0.1f; }
        } else if (los && e.atkT <= 0.f) { e.burstLeft = 6; e.atkT = 2.8f / speedUp; }
        if (e.grenT <= 0.f && los && d > 4.f) { enemyGrenade(e, tgX, tgY); e.grenT = 6.f; }
        e.grenT -= dt;
        vx = ux; vy = uy; spd = d > 5.f ? 0.55f : 0.f;
        break;
    }
    case BK_GUARD_MEDIC: {
        // keep the heavy alive: heal beam while in reach, stay away from you
        if (e.partner >= 0) {
            Enemy &o = m_gs.enemies[size_t(e.partner)];
            float od = std::sqrt((o.x - e.x) * (o.x - e.x) + (o.y - e.y) * (o.y - e.y));
            if (o.alive && od < 9.f && hasLOS(e.x, e.y, floorH(e.x, e.y) + 0.7f, o.x, o.y, floorH(o.x, o.y) + 0.7f) && o.hp < o.maxHp) {
                e.healAcc += (14.f + 4.f * e.phase) * dt;
                if (e.healAcc >= 1.f) { int h = int(e.healAcc); e.healAcc -= h; o.hp = std::min(o.maxHp, o.hp + h); o.hpShow = 1.f; }
                e.burstT = 0.15f;                                 // beam visible
                m_gs.beams.push_back(Beam{e.x, e.y, floorH(e.x, e.y) + 0.8f, o.x, o.y, floorH(o.x, o.y) + 0.8f, 0.05f, 0.05f, rgb(90, 255, 140), 4});
            }
            if (od > 7.f) { vx = o.x - e.x; vy = o.y - e.y; spd = 1.f; }
        }
        if (d < 7.f) { vx = -ux; vy = -uy; spd = 1.1f; }
        if (los && d < 14.f && e.fireCooldown <= 0.f) { bossShot(e, tgX, tgY, tgZ, m_gs.enemyBulletSpeed, 8 + 2 * m_gs.level, 0.06f); sfxAt(SND_ENEMY_PISTOL, e.x, e.y, 0.8f); e.fireCooldown = 0.9f; e.shootAnim = 0.2f; }
        break;
    }

    // ── MIRAGE ───────────────────────────────────────────────────────────────────────
    case BK_MIRAGE: {
        e.revealT = std::max(0.f, e.revealT - dt);
        bool reveal = e.revealT > 0.f || m_radarT > 0.f || m_flashT > 0.f || (d < 2.f && e.atk != 0);
        for (const SmokeCloud &s : m_gs.smokes) if ((s.x - e.x) * (s.x - e.x) + (s.y - e.y) * (s.y - e.y) < s.r * s.r) reveal = true;
        if (e.stunT > 0.f) reveal = true;
        e.cloak += ((reveal ? 0.15f : 0.92f) - e.cloak) * std::min(1.f, dt * 5.f);
        if (e.atk == 0) {                                        // BLINK STRIKE: appears behind you, then slashes
            e.atkPhaseT += dt;
            if (e.atkPhaseT >= 0.5f && e.burstLeft == 0) {
                float bx = px - pl.dirX * 1.3f, by = py - pl.dirY * 1.3f;
                if (posValid(bx, by, 0.3f)) { e.x = bx; e.y = by; }
                e.burstLeft = 1;
                sfxAt(SND_GRAPPLE, e.x, e.y, 0.7f, 1.4f);
            }
            if (e.atkPhaseT >= 0.95f) {
                float pd = std::sqrt((px - e.x) * (px - e.x) + (py - e.y) * (py - e.y));
                if (pd < 1.5f && !e.decoy) hurtPlayer(26 + 6 * m_gs.level, e.x, e.y, idx);
                else if (pd < 1.5f) hurtPlayer(6, e.x, e.y, idx);
                sfxAt(SND_KNIFE_SWING, e.x, e.y, 1.f, 0.8f);
                e.shootAnim = 0.25f; e.atk = -1; e.atkT = (3.2f - 0.6f * e.phase) * (e.decoy ? 1.5f : 1.f); e.burstLeft = 0;
                e.revealT = 0.6f;
            }
            return;
        }
        if (e.atkT <= 0.f && d < 18.f) { e.atk = 0; e.atkPhaseT = 0.f; e.burstLeft = 0; break; }
        // throwing knives from the shimmer
        if (los && d < 12.f && e.fireCooldown <= 0.f) { bossShot(e, tgX, tgY, tgZ, 16.f, 10 + 2 * m_gs.level, 0.05f); sfxAt(SND_KNIFE_SWING, e.x, e.y, 0.6f, 1.3f); e.fireCooldown = 1.4f; }
        vx = -uy + ux * 0.3f; vy = ux + uy * 0.3f; spd = 1.6f * speedUp;
        break;
    }

    // ── THE ENGINEER ─────────────────────────────────────────────────────────────────
    case BK_ENGINEER: {
        int pylons = 0;
        for (const Enemy &o : m_gs.enemies) if (o.alive && o.bossKind == BK_PYLON) pylons++;
        if (e.shielded && pylons == 0) { e.shielded = false; pushFeed("THE ENGINEER'S SHIELD IS DOWN", 3.f, true); sfx(SND_OVERHEAT, 0.9f); }
        int turrets = 0;
        for (const Enemy &o : m_gs.enemies) if (o.alive && o.type == ET_TURRET && !o.bossKind) turrets++;
        if (e.atkT <= 0.f && turrets < 2 + e.phase) {            // DEPLOY a sentry gun
            for (int t = 0; t < 20; t++) {
                float a = frandB(0, 2 * PI_B), r = frandB(1.5f, 3.5f);
                float x = e.x + std::cos(a) * r, y = e.y + std::sin(a) * r;
                if (!posValid(x, y, 0.3f) || m_acid[int(y)][int(x)] || !reachable(int(x), int(y))) continue;
                spawnEnemy(x, y, false, ET_TURRET); m_gs.enemies.back().state = ES_ALERT;
                sfxAt(SND_DEPLOY, x, y, 0.8f);
                break;
            }
            e.atkT = 9.f - 2.f * e.phase;
        }
        if (los && d < 15.f && e.fireCooldown <= 0.f) { enemyFire(e, tgX, tgY, tgZ); e.fireCooldown = 0.7f; e.shootAnim = 0.2f; }
        if (d < 6.f) { vx = -ux; vy = -uy; spd = 0.9f; } else if (!los) { vx = ux; vy = uy; spd = 0.7f; }
        break;
    }

    // ── JUGGERNAUT ───────────────────────────────────────────────────────────────────
    case BK_JUGGERNAUT: {
        // turns slowly: flank it to hit the exposed back
        float want = std::atan2(dy, dx), cur = std::atan2(e.alertDirY, e.alertDirX);
        cur += clampf(std::remainder(want - cur, 2 * PI_B), -0.9f * dt, 0.9f * dt);
        e.alertDirX = std::cos(cur); e.alertDirY = std::sin(cur);
        float rel = std::fabs(std::remainder(std::atan2(py - e.y, px - e.x) - cur, 2 * PI_B));
        e.armor = rel > 2.2f ? 0.f : 0.5f;                        // behind it: no plates
        if (e.atk == 0) {                                        // MINIGUN: spin-up, then a long sweep
            e.atkPhaseT += dt;
            if (e.atkPhaseT >= 1.0f) {
                e.burstT -= dt;
                if (e.burstT <= 0.f) {
                    e.burstT = 0.1f;
                    float a = cur + frandB(-0.16f, 0.16f);
                    bossShot(e, e.x + std::cos(a) * 10.f, e.y + std::sin(a) * 10.f, tgZ, m_gs.enemyBulletSpeed * 1.4f, 4 + m_gs.level, 0.f);
                    sfxAt(SND_LMG, e.x, e.y, 0.7f, 0.9f);
                    e.shootAnim = 0.1f;
                }
                if (e.atkPhaseT > 3.6f) { e.atk = -1; e.atkT = 2.f / speedUp; }
            }
            return;
        }
        if (e.atkT <= 0.f && los) {
            if (frandB() < 0.55f) { e.atk = 0; e.atkPhaseT = 0.f; sfxAt(SND_RAIL_CHARGE, e.x, e.y, 0.8f, 0.5f); }
            else {                                               // MORTAR barrage at and around you
                for (int k = 0; k < 3 + e.phase; k++) {
                    float ox = k == 0 ? 0.f : frandB(-3.f, 3.f), oy = k == 0 ? 0.f : frandB(-3.f, 3.f);
                    enemyGrenade(e, tgX + ox, tgY + oy);
                    Projectile &p = m_gs.projectiles.back();
                    p.splashR = 2.8f; p.splashDmg *= 1.3f; p.fuse += 0.3f * k;
                }
                e.atkT = 3.4f / speedUp;
            }
        }
        vx = ux; vy = uy; spd = d > 5.f ? 0.45f : 0.f;
        break;
    }

    // ── VECTOR / THE HELIX FOUNDER ───────────────────────────────────────────────────
    case BK_FOUNDER: {
        int pylons = 0;
        for (const Enemy &o : m_gs.enemies) if (o.alive && o.bossKind == BK_PYLON) pylons++;
        if (e.phase == 0 && e.shielded && pylons == 0) { e.shielded = false; pushFeed("THE MECH'S SHIELD IS DOWN - SHOOT THE VENTS", 3.f, true); sfx(SND_OVERHEAT, 1.f); }
        if (e.phase == 0) {
            // mech: homing rockets and minigun sweeps from behind its shield
            if (e.atkT <= 0.f && los) {
                if (frandB() < 0.5f) {
                    for (int k = 0; k < 3; k++) {
                        Projectile p; p.x = e.x; p.y = e.y; p.z = floorH(e.x, e.y) + 1.4f;
                        float a = std::atan2(py - e.y, px - e.x) + (k - 1) * 0.5f;
                        p.vx = std::cos(a) * 7.5f; p.vy = std::sin(a) * 7.5f; p.vz = 1.5f;
                        p.kind = PJ_ROCKET; p.owner = PO_ENEMY; p.life = 6.f; p.splashR = 2.2f; p.splashDmg = float(22 + 6 * m_gs.level);
                        m_gs.projectiles.push_back(p);
                    }
                    sfxAt(SND_ROCKET_FIRE, e.x, e.y, 1.f);
                } else { e.burstLeft = 18; }
                e.atkT = 3.f;
            }
            if (e.burstLeft > 0) {
                e.burstT -= dt;
                if (e.burstT <= 0.f) { e.burstT = 0.09f; e.burstLeft--; bossShot(e, tgX, tgY, tgZ, m_gs.enemyBulletSpeed * 1.4f, 4 + m_gs.level, 0.14f); sfxAt(SND_LMG, e.x, e.y, 0.7f); e.shootAnim = 0.08f; }
            }
            vx = ux; vy = uy; spd = d > 7.f ? 0.4f : 0.f;
            break;
        }
        if (e.phase == 1) {
            // on foot: rotating laser walls (jump them) and dash strikes
            m_laserAngle += dt * 0.75f * speedUp;
            float footG = floorH(px, py);
            bool airborne = pl.footZ > footG + 0.3f;
            for (int k = 0; k < 2; k++) {
                float a = m_laserAngle + k * PI_B;
                float lx = std::cos(a), ly = std::sin(a);
                float rx = px - e.x, ry = py - e.y;
                float along = rx * lx + ry * ly, perp = std::fabs(-rx * ly + ry * lx);
                if (along > 0.f && along < 11.f && perp < 0.32f && !airborne && m_laserHitT <= 0.f && !e.decoy) { hurtPlayer(16 + 4 * m_gs.level, e.x, e.y, idx); m_laserHitT = 0.5f; }
            }
            if (e.atk == 0) {
                e.atkPhaseT += dt;
                if (e.atkPhaseT > 0.6f) {
                    float bx = px - pl.dirX * 1.2f, by = py - pl.dirY * 1.2f;
                    if (posValid(bx, by, 0.35f)) { e.x = bx; e.y = by; }
                    float pd = std::sqrt((px - e.x) * (px - e.x) + (py - e.y) * (py - e.y));
                    if (pd < 1.6f) hurtPlayer(20 + 5 * m_gs.level, e.x, e.y, idx);
                    sfxAt(SND_MELEE_HEAVY, e.x, e.y, 1.f, 0.8f);
                    e.atk = -1; e.atkT = 3.5f;
                }
                return;
            }
            if (e.atkT <= 0.f) { e.atk = 0; e.atkPhaseT = 0.f; sfxAt(SND_JETPACK, e.x, e.y, 0.8f, 1.4f); }
            if (los && e.fireCooldown <= 0.f) { bossShot(e, tgX, tgY, tgZ, 22.f, 10 + 3 * m_gs.level, 0.03f); sfxAt(SND_PLASMA, e.x, e.y, 0.7f); e.fireCooldown = 0.6f; e.shootAnim = 0.15f; }
            vx = -uy; vy = ux; spd = 1.2f;
            break;
        }
        // phase 2: overload — the floor electrifies in patterns; charged bolts
        if (!e.decoy) {
            m_overloadT -= dt;
            if (m_overloadT <= 0.f) {
                m_overloadT = 3.2f;
                m_overloadCells.clear();
                int pat = rand() % 3;
                int cx0 = int(e.x), cy0 = int(e.y);
                for (int y = cy0 - 9; y <= cy0 + 9; y++) for (int x = cx0 - 9; x <= cx0 + 9; x++) {
                    if (!inside(x, y) || solidCell(x, y)) continue;
                    bool on = pat == 0 ? ((x + y) & 1) == 0 : pat == 1 ? ((x / 2 + y / 2) & 1) == 0 : (std::abs(x - cx0) + std::abs(y - cy0)) % 4 < 2;
                    if (on) m_overloadCells.push_back(y * m_mapW + x);
                }
                m_overloadPhase = 0.f;
                sfx(SND_RAIL_CHARGE, 0.6f, 0.7f);
            }
            m_overloadPhase += dt;
            if (m_overloadPhase > 1.3f && m_overloadPhase < 2.1f) {
                int pc = int(py) * m_mapW + int(px);
                bool onTile = std::find(m_overloadCells.begin(), m_overloadCells.end(), pc) != m_overloadCells.end();
                if (onTile && pl.footZ < floorH(px, py) + 0.2f && m_laserHitT <= 0.f) { hurtPlayer(14 + 3 * m_gs.level, e.x, e.y, idx); m_laserHitT = 0.5f; sfx(SND_ARC, 0.8f); }
            }
        }
        if (los && e.fireCooldown <= 0.f) { bossShot(e, tgX, tgY, tgZ, 18.f, 12 + 3 * m_gs.level, 0.04f); sfxAt(SND_PLASMA, e.x, e.y, 0.7f, 0.8f); e.fireCooldown = e.decoy ? 1.6f : 0.8f; e.shootAnim = 0.15f; }
        vx = -uy + ux * (d - 6.f) * 0.2f; vy = ux + uy * (d - 6.f) * 0.2f; spd = 1.3f;
        break;
    }
    default: break;
    }
    if (e.hitFlash > 0) spd *= 0.4f;
    moveEnemy(e, idx, vx, vy, spd, dt);
}

// Per-frame: acid spreading from egg sacs (breadth-first seed fill), shields, timers
void MainWindow::updateBossWorld(float dt)
{
    m_bossBanner = std::max(0.f, m_bossBanner - dt);
    m_laserHitT = std::max(0.f, m_laserHitT - dt);
    for (SeedSpread &s : m_seeds) {
        bool eggAlive = s.egg >= 0 && s.egg < int(m_gs.enemies.size()) && m_gs.enemies[size_t(s.egg)].alive;
        if (s.egg < 0) { s.life -= dt; eggAlive = s.life > 0.f; if (eggAlive) continue; }
        if (!eggAlive) {
            // the egg is dead: its acid recedes
            for (int c : s.cells) { int x = c % m_mapW, y = c / m_mapW; if (!m_baseAcid[size_t(c)]) m_acid[y][x] = 0; }
            if (!s.cells.empty() && s.egg >= 0) pushFeed("THE ACID RECEDES", 1.5f);
            s.cells.clear(); s.frontier.clear();
            continue;
        }
        s.t += dt;
        if (s.t < 0.55f || s.cells.size() > 34) continue;
        s.t = 0.f;
        // grow by one ring step: take the oldest frontier cells (queue order = breadth first)
        std::vector<int> next;
        int grown = 0;
        for (size_t i = 0; i < s.frontier.size() && grown < 3; i++) {
            int c = s.frontier[i], x = c % m_mapW, y = c / m_mapW;
            if (!inside(x, y) || solidCell(x, y) || m_acid[y][x]) continue;
            m_acid[y][x] = 1; s.cells.push_back(c); grown++;
            const int dd[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto &o : dd) next.push_back((y + o[1]) * m_mapW + (x + o[0]));
        }
        s.frontier.erase(s.frontier.begin(), s.frontier.begin() + std::min(s.frontier.size(), size_t(std::max(1, grown))));
        s.frontier.insert(s.frontier.end(), next.begin(), next.end());
    }
    m_seeds.erase(std::remove_if(m_seeds.begin(), m_seeds.end(), [](const SeedSpread &s) { return s.cells.empty() && s.frontier.empty(); }), m_seeds.end());
}

// Telegraphs and boss effects drawn into the world
void MainWindow::renderBossFX()
{
    auto groundCell = [&](int cx, int cy, uint32_t col, int alpha) {
        // project the cell's four corners and fill the quad, depth-tested against the z-buffer
        float xs[4], ys[4], ds[4];
        const float cxs[4] = {0, 1, 1, 0}, cys[4] = {0, 0, 1, 1};
        float avg = 0;
        for (int k = 0; k < 4; k++) {
            float wx = cx + cxs[k], wy = cy + cys[k];
            if (!project(wx, wy, floorH(wx, wy) + 0.02f, xs[k], ys[k], ds[k]) || ds[k] < 0.3f) return;
            avg += ds[k] * 0.25f;
        }
        int x0 = std::max(0, int(std::min({xs[0], xs[1], xs[2], xs[3]}))), x1 = std::min(W - 1, int(std::max({xs[0], xs[1], xs[2], xs[3]})));
        int y0 = std::max(0, int(std::min({ys[0], ys[1], ys[2], ys[3]}))), y1 = std::min(H - 1, int(std::max({ys[0], ys[1], ys[2], ys[3]})));
        if (x1 - x0 > 600 || y1 - y0 > 400) return;
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            bool in = true, pos = false, neg = false;
            for (int k = 0; k < 4; k++) {
                int j = (k + 1) % 4;
                float c = (xs[j] - xs[k]) * (y + 0.5f - ys[k]) - (ys[j] - ys[k]) * (x + 0.5f - xs[k]);
                if (c > 0) pos = true; else neg = true;
            }
            in = !(pos && neg);
            if (!in || avg > m_depth[size_t(y) * W + x] + 0.4f) continue;
            m_cv.add(x, y, withAlpha(col, alpha));
        }
    };
    auto groundRing = [&](float cx, float cy, float r, uint32_t col, int alpha, int th) {
        float px0 = 0, py0 = 0; bool have = false;
        for (int k = 0; k <= 40; k++) {
            float a = k * 2.f * PI_B / 40.f, x = cx + std::cos(a) * r, y = cy + std::sin(a) * r;
            float sx, sy, d;
            bool ok = project(x, y, floorH(x, y) + 0.04f, sx, sy, d) && d > 0.2f && d < m_depth[size_t(clampi(int(sy), 0, H - 1)) * W + clampi(int(sx), 0, W - 1)] + 0.5f;
            if (ok && have) m_cv.thickLine(int(px0), int(py0), int(sx), int(sy), withAlpha(col, alpha), th);
            px0 = sx; py0 = sy; have = ok;
        }
    };
    float pulse = 0.55f + 0.45f * std::sin(m_time * 14.f);
    for (const Enemy &e : m_gs.enemies) {
        if (!e.alive || !e.bossKind) continue;
        switch (e.bossKind) {
        case BK_WARDEN:
            if (e.atk == 0 && e.atkPhaseT < 0.9f) {            // charge stripe
                for (float t = 0.5f; t < 12.f; t += 0.5f) {
                    int cx = int(e.x + e.tx * t), cy = int(e.y + e.ty * t);
                    if (!inside(cx, cy) || solidCell(cx, cy)) break;
                    groundCell(cx, cy, rgb(255, 40, 20), int(120 * pulse));
                }
            }
            if (e.atk == 1) {                                  // slam: warning ring, then the shockwave
                if (e.atkPhaseT < 0.8f) groundRing(e.x, e.y, 6.f * (e.atkPhaseT / 0.8f), rgb(255, 60, 30), int(200 * pulse), 2);
                else groundRing(e.x, e.y, (e.atkPhaseT - 0.8f) * 6.5f, rgb(255, 200, 120), 230, 4);
            }
            if (e.stunT > 0.f) {
                float sx, sy, d;
                if (project(e.x, e.y, floorH(e.x, e.y) + 1.9f * e.scale * 0.6f, sx, sy, d) && d > 0.3f)
                    for (int k = 0; k < 3; k++) m_cv.glow(int(sx + std::cos(m_time * 6.f + k * 2.1f) * 14.f), int(sy + std::sin(m_time * 6.f + k * 2.1f) * 5.f), 5, rgb(255, 230, 90), 1.f);
            }
            break;
        case BK_TWIN_A: case BK_TWIN_B:
            if (e.atk == 0) {                                  // the Bresenham laser: thin, then thick before the shot
                float lock = (e.partner >= 0 && !m_gs.enemies[size_t(e.partner)].alive) ? 0.8f : 1.4f;
                float k = e.atkPhaseT / lock;
                float sx, sy, sd, tx, ty, td;
                float z0 = floorH(e.x, e.y) + 0.7f * e.scale;
                if (project(e.x, e.y, z0, sx, sy, sd) && project(e.tx, e.ty, m_gs.player.footZ + 0.45f, tx, ty, td)) {
                    uint32_t c = e.bossKind == BK_TWIN_A ? rgb(255, 40, 40) : rgb(60, 150, 255);
                    if (k > 0.75f) m_cv.thickLine(int(sx), int(sy), int(tx), int(ty), withAlpha(c, int(200 * pulse)), 3);
                    else m_cv.line(int(sx), int(sy), int(tx), int(ty), withAlpha(c, int(90 + 120 * k)));
                    m_cv.glow(int(tx), int(ty), 6 + int(10 * k), c, 0.9f);
                }
            }
            break;
        case BK_EGG: {                                         // pulsing glow
            float sx, sy, d;
            if (project(e.x, e.y, floorH(e.x, e.y) + 0.3f, sx, sy, d) && d > 0.3f) m_cv.glow(int(sx), int(sy), std::max(6, int(0.5f * m_K / d)), rgb(150, 255, 80), 0.35f * pulse);
            break;
        }
        case BK_GUNSHIP:
            if (e.atk == 0 && e.atkPhaseT < 1.6f) {            // the strafing line
                float len = std::sqrt((m_bossRunBX - m_bossRunAX) * (m_bossRunBX - m_bossRunAX) + (m_bossRunBY - m_bossRunAY) * (m_bossRunBY - m_bossRunAY));
                for (float t = 0.f; t < len; t += 0.6f) {
                    float k = t / len;
                    int cx = int(m_bossRunAX + (m_bossRunBX - m_bossRunAX) * k), cy = int(m_bossRunAY + (m_bossRunBY - m_bossRunAY) * k);
                    if (inside(cx, cy) && !solidCell(cx, cy)) groundCell(cx, cy, rgb(255, 60, 30), int(90 * pulse));
                }
            }
            break;
        case BK_PYLON: case BK_ENGINEER: case BK_FOUNDER: {
            // shield beams from each pylon to its boss, and a bubble ring around a shielded boss
            if (e.bossKind == BK_PYLON && e.partner >= 0) {
                const Enemy &b = m_gs.enemies[size_t(e.partner)];
                if (b.alive && b.shielded) {
                    float ax, ay, ad, bx, by, bd;
                    if (project(e.x, e.y, floorH(e.x, e.y) + 0.9f, ax, ay, ad) && project(b.x, b.y, floorH(b.x, b.y) + 0.8f * b.scale, bx, by, bd))
                        m_cv.thickLine(int(ax), int(ay), int(bx), int(by), withAlpha(rgb(90, 220, 255), int(120 * pulse)), 2);
                }
            }
            if (e.shielded) groundRing(e.x, e.y, 1.2f * e.scale, rgb(90, 220, 255), int(160 * pulse), 2);
            if (e.bossKind == BK_FOUNDER && e.phase == 1) {    // laser walls
                for (int k = 0; k < 2; k++) {
                    float a = m_laserAngle + k * PI_B;
                    float x0 = e.x, y0 = e.y, x1 = e.x + std::cos(a) * 11.f, y1 = e.y + std::sin(a) * 11.f;
                    float ax, ay, ad, bx, by, bd, cx2, cy2, cd, dx2, dy2, dd;
                    if (project(x0, y0, floorH(x0, y0) + 0.05f, ax, ay, ad) && project(x1, y1, floorH(x1, y1) + 0.05f, bx, by, bd) &&
                        project(x0, y0, floorH(x0, y0) + 0.32f, cx2, cy2, cd) && project(x1, y1, floorH(x1, y1) + 0.32f, dx2, dy2, dd)) {
                        m_cv.thickLine(int(ax), int(ay), int(bx), int(by), withAlpha(rgb(255, 40, 160), 220), 3);
                        m_cv.thickLine(int(cx2), int(cy2), int(dx2), int(dy2), withAlpha(rgb(255, 120, 220), 200), 2);
                        m_cv.tri(ax, ay, bx, by, dx2, dy2, withAlpha(rgb(255, 40, 160), 60), withAlpha(rgb(255, 40, 160), 60), withAlpha(rgb(255, 40, 160), 30));
                    }
                }
            }
            break;
        }
        case BK_JUGGERNAUT:
            if (e.atk == 0 && e.atkPhaseT < 1.0f) {            // minigun spin-up glow
                float sx, sy, d;
                if (project(e.x, e.y, floorH(e.x, e.y) + 0.55f * e.scale, sx, sy, d) && d > 0.3f) m_cv.glow(int(sx), int(sy), std::max(6, int(0.45f * m_K / d)), rgb(255, 120, 40), 1.2f * e.atkPhaseT);
            }
            break;
        default: break;
        }
    }
    // VECTOR's overload: tiles glow amber, then flash white while live
    if (!m_overloadCells.empty()) {
        bool live = m_overloadPhase > 1.3f && m_overloadPhase < 2.1f;
        if (m_overloadPhase < 2.1f)
            for (int c : m_overloadCells) groundCell(c % m_mapW, c / m_mapW, live ? rgb(200, 220, 255) : rgb(255, 160, 40), live ? 150 : int(40 + 50 * pulse));
    }
}

// The boss health bar: name, phase notches, shield state; twins share the top
void MainWindow::renderBossBar()
{
    const Player &pl = m_gs.player;
    int shown = 0;
    for (const Enemy &e : m_gs.enemies) {
        if (!e.alive || !e.boss || e.decoy || shown >= 2) continue;
        float dx = e.x - pl.posX, dy = e.y - pl.posY;
        if (e.state != ES_ALERT && dx * dx + dy * dy > 18.f * 18.f) continue;
        const int bw = shown == 0 ? 360 : 260, bx = W / 2 - bw / 2, by = 134 + shown * 34;
        float frac = float(e.hp) / float(std::max(1, e.maxHp));
        m_cv.blendRect(bx - 6, by - 16, bw + 12, 30, withAlpha(0xFF0E1216, 200));
        m_cv.blendRect(bx, by, bw, 8, withAlpha(0xFFFFFFFF, 30));
        uint32_t col = e.shielded ? rgb(90, 200, 255) : rgb(240, 74, 60);
        m_cv.fillRect(bx, by, int(bw * frac), 8, col);
        m_cv.blendRect(bx, by, int(bw * frac), 2, withAlpha(0xFFFFFFFF, 90));
        if (e.bossKind) for (float n : {0.66f, 0.33f}) m_cv.fillRect(bx + int(bw * n), by - 2, 2, 12, rgb(255, 230, 160));   // phase notches
        QString nm = e.bossKind ? QString(bossName(e.bossKind)) : QString("COMMANDER");
        if (e.bossKind && e.shielded) nm += "   [SHIELDED]";
        text(bx, by - 15, bw, 14, Qt::AlignLeft, nm, e.shielded ? QColor(120, 210, 255) : QColor(255, 120, 100), 11, 900, false);
        text(bx, by - 15, bw, 14, Qt::AlignRight, e.bossKind ? QString("PHASE %1").arg(e.phase + 1) : QString("%1 / %2").arg(e.hp).arg(e.maxHp), QColor(220, 220, 220), 10, 700, false);
        shown++;
    }
    if (m_bossBanner > 0.f) {
        int a = int(255 * std::min(1.f, m_bossBanner));
        text(0, 200, W, 30, Qt::AlignCenter, "THE FIGHT CHANGES", QColor(255, 200, 120, a), 22, 900);
    }
}

// Free play: each zone's commander is one of the story bosses, in order
void MainWindow::spawnZoneBoss(int kind, float x, float y, float farX, float farY)
{
    if (kind == BK_TWIN_A) spawnBossPair(BK_TWIN_A, x, y, BK_TWIN_B, farX, farY);
    else if (kind == BK_GUARD_HEAVY) spawnBossPair(BK_GUARD_HEAVY, x, y, BK_GUARD_MEDIC, x + 1.5f, y);
    else spawnBoss(kind, x, y, 1.f);
}
