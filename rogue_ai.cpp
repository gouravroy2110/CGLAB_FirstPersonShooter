// =============================================================================
//  rogue_ai.cpp - Autonomous Adaptive Infiltrator (Rogue Operative AI)
// =============================================================================
#include "mainwindow.h"
#include <cmath>

static inline float frandR() { return float(rand()) / float(RAND_MAX); }
static inline float frandR(float a, float b) { return a + (b - a) * frandR(); }

void MainWindow::spawnRogueOperative()
{
    // Don't spawn if a Rogue is already active and alive
    for (const Enemy &e : m_gs.enemies) {
        if (e.alive && e.isRogue) return;
    }
    const Player &pl = m_gs.player;

    // Find a valid floor tile 16..40 units away from player
    float bestX = -1.f, bestY = -1.f;
    for (int tries = 0; tries < 80; tries++) {
        int cx = 2 + rand() % (m_mapW - 4);
        int cy = 2 + rand() % (m_mapH - 4);
        if (m_map[cy][cx] != CELL_FLOOR || m_acid[cy][cx] || !reachable(cx, cy) || m_plat[cy][cx] < -0.1f) continue;
        float x = cx + 0.5f, y = cy + 0.5f;
        float d2 = (x - pl.posX) * (x - pl.posX) + (y - pl.posY) * (y - pl.posY);
        if (d2 < 256.f || d2 > 1600.f) continue;
        if (!posValid(x, y, 0.35f)) continue;
        bestX = x; bestY = y;
        break;
    }
    if (bestX < 0.f) {
        // Fallback: any reachable floor tile
        for (int y = 2; y < m_mapH - 2 && bestX < 0.f; y++) {
            for (int x = 2; x < m_mapW - 2 && bestX < 0.f; x++) {
                if (m_map[y][x] == CELL_FLOOR && reachable(x, y) && posValid(x + 0.5f, y + 0.5f, 0.35f)) {
                    bestX = x + 0.5f; bestY = y + 0.5f;
                }
            }
        }
    }
    if (bestX < 0.f) return;

    // Spawn Rogue Operative with upgraded adaptive attributes
    Enemy r;
    r.x = bestX; r.y = bestY;
    r.hp = r.maxHp = 135 + (m_rogueBrain.generation - 1) * 20;
    r.isRogue = true;
    r.elite = true;
    r.scale = 1.05f;
    r.state = ES_ALERT;
    r.type = (m_rogueBrain.generation % 3 == 0) ? ET_HEAVY : (m_rogueBrain.generation % 2 == 0) ? ET_SNIPER : ET_RIFLEMAN;
    r.fireCooldown = 0.8f;
    r.aggroRange = 16.f;
    r.rogueTarget = -1;
    r.roguePolicyT = 0.f;
    r.rogueAction = ROGUE_ACT_FLANK;
    m_gs.enemies.push_back(r);

    m_rogueStats.encounters++;
    pushFeed(QString("ALERT: ROGUE INFILTRATOR ACTIVE  [GEN %1]").arg(m_rogueBrain.generation), 4.5f, true);
    sfx(SND_RADIO, 0.9f);
}

void MainWindow::updateRogueAI(int idx, float dt, float tgX, float tgY, float tgZ, float d, bool los)
{
    Enemy &e = m_gs.enemies[size_t(idx)];
    const Player &pl = m_gs.player;
    const DiffConfig &dc = DIFF_CFG[m_difficulty];

    // Re-evaluate target: check for nearby regular hostiles vs player (3-way infighting)
    e.rogueTarget = -1; // default: target player
    float bestHostileDist = 18.f;
    int bestHostileIdx = -1;
    for (int j = 0; j < int(m_gs.enemies.size()); j++) {
        if (j == idx) continue;
        const Enemy &o = m_gs.enemies[size_t(j)];
        if (!o.alive || o.isRogue) continue;
        float od = std::hypot(o.x - e.x, o.y - e.y);
        if (od < bestHostileDist && hasLOS(e.x, e.y, floorH(e.x, e.y) + 0.6f, o.x, o.y, floorH(o.x, o.y) + 0.6f)) {
            bestHostileDist = od;
            bestHostileIdx = j;
        }
    }
    // If a regular hostile is closer than the player or player is completely out of sight, engage guard
    if (bestHostileIdx >= 0 && (bestHostileDist < d * 0.75f || !los)) {
        e.rogueTarget = bestHostileIdx;
    }

    float vx = 0.f, vy = 0.f, spd = 0.f;

    // --- Engaging Standard Guard (3-Way Infighting) ---
    if (e.rogueTarget >= 0) {
        Enemy &targetGuard = m_gs.enemies[size_t(e.rogueTarget)];
        float gx = targetGuard.x, gy = targetGuard.y;
        float gdx = gx - e.x, gdy = gy - e.y, gd = std::hypot(gdx, gdy);
        float gux = gd > 0.01f ? gdx / gd : 0.f, guy = gd > 0.01f ? gdy / gd : 0.f;

        if (gd > 6.f) { vx = gux; vy = guy; spd = 1.15f; }
        else { vx = -guy; vy = gux; spd = 0.8f; }

        if (gd < 16.f && e.fireCooldown <= 0.f && e.hitFlash <= 0.f) {
            enemyFire(e, gx, gy, floorH(gx, gy) + 0.6f);
            e.shootAnim = 0.22f;
            e.fireCooldown = frandR(0.45f, 0.85f) * dc.fireCoolMult;
        }
        moveEnemy(e, idx, vx, vy, spd, dt);
        return;
    }

    // --- Engaging Player: Execute Reinforcement Learning Policy ---
    float dx = tgX - e.x, dy = tgY - e.y;
    float ux = d > 0.01f ? dx / d : 0.f, uy = d > 0.01f ? dy / d : 0.f;
    float perpX = -uy * (e.moveX >= 0.f ? 1.f : -1.f), perpY = ux * (e.moveX >= 0.f ? 1.f : -1.f);

    // Record combat tracking info
    m_combatTracker.recordDistance(d);

    // Policy re-evaluation timer
    e.roguePolicyT -= dt;
    if (e.roguePolicyT <= 0.f) {
        e.roguePolicyT = frandR(1.2f, 2.0f);

        // Detect player state
        bool reloadingOrLowHp = (pl.reloadTimer > 0.1f) || (pl.hp < maxHP() * 0.35f);
        bool playerStationary = !isPlayerMoving();
        int guardsNearPlayer = 0;
        for (const Enemy &o : m_gs.enemies) {
            if (o.alive && !o.isRogue && std::hypot(o.x - pl.posX, o.y - pl.posY) < 14.f) guardsNearPlayer++;
        }

        int curWeaponClass = (pl.curWeapon >= 0 && pl.curWeapon < WPN_COUNT) ? WEAPONS[pl.curWeapon].cls : WC_RIFLE;
        int state = m_rogueBrain.encodeState(d, curWeaponClass, reloadingOrLowHp, playerStationary, guardsNearPlayer >= 2);
        e.rogueAction = m_rogueBrain.selectAction(state);

        // Update adaptation score
        m_rogueStats.adaptationScore = std::min(98.f, 20.f + (m_rogueBrain.generation - 1) * 15.f + (m_rogueStats.encounters * 3.f));
    }

    // Execute selected action
    switch (e.rogueAction) {
    case ROGUE_ACT_KITE: {
        // Backpedal while firing, keep 12-16m distance (counters shotguns)
        m_rogueStats.kitesExecuted++;
        if (d < 13.f) {
            vx = -ux * 0.9f + perpX * 0.4f;
            vy = -uy * 0.9f + perpY * 0.4f;
            spd = 1.35f;
        } else if (d > 18.f) {
            vx = ux * 0.7f; vy = uy * 0.7f; spd = 0.9f;
        } else {
            vx = perpX; vy = perpY; spd = 0.95f;
        }
        break;
    }
    case ROGUE_ACT_FLANK: {
        // Circle around cover to attack from the blind side (counters snipers)
        m_rogueStats.flanksExecuted++;
        vx = perpX * 0.9f + ux * 0.2f;
        vy = perpY * 0.9f + uy * 0.2f;
        spd = 1.25f;
        break;
    }
    case ROGUE_ACT_PUNISH_RUSH: {
        // Sprint rush when player is vulnerable
        m_rogueStats.punishesExecuted++;
        vx = ux; vy = uy; spd = 1.60f;
        break;
    }
    case ROGUE_ACT_PUPPETEER: {
        // Draw standard enemies into player's crossfire via loud alert
        m_rogueStats.puppeteerLures++;
        m_noiseT = std::max(m_noiseT, 1.2f);
        // Alert all guards in 28 units towards player
        for (Enemy &o : m_gs.enemies) {
            if (o.alive && !o.isRogue && o.state == ES_IDLE && std::hypot(o.x - e.x, o.y - e.y) < 28.f) {
                o.state = ES_ALERT;
                o.alertDirX = ux; o.alertDirY = uy;
                o.alertDist = d;
            }
        }
        // Rogue pulls back slightly while guards swarm in
        vx = -ux * 0.5f + perpX * 0.8f;
        vy = -uy * 0.5f + perpY * 0.8f;
        spd = 1.1f;
        break;
    }
    case ROGUE_ACT_RETREAT: {
        // Retreat to cover
        vx = -ux; vy = -uy; spd = 1.4f;
        break;
    }
    default:
        vx = perpX; vy = perpY; spd = 1.0f;
        break;
    }

    // Rogue firing cadence
    if (los && d <= 22.f && e.fireCooldown <= 0.f && e.hitFlash <= 0.f) {
        enemyFire(e, tgX, tgY, tgZ);
        e.shootAnim = 0.22f;
        float cool = frandR(0.55f, 0.95f) * dc.fireCoolMult;
        if (e.rogueAction == ROGUE_ACT_PUNISH_RUSH) cool *= 0.65f; // Rapid punishment fire
        e.fireCooldown = cool;
        // Reward RL engine for dealing fire
        m_rogueBrain.updateReward(14.f, m_rogueBrain.lastState);
    }

    moveEnemy(e, idx, vx, vy, spd, dt);
}
