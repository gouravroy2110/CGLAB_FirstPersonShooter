// =============================================================================
//  enemies.cpp  -  The extended hostile roster
//
//    Grenadier  lobs grenades; a red ring on the ground shows where they land
//    Shield     riot shield blocks rounds from the front (aim for head / legs,
//               or use explosives); advances while firing a pistol
//    Medic      revives fallen allies (not after a headshot) and heals the hurt
//    Heavy      slow, very tough, suppressive rotary-cannon bursts
//    Stalker    cloaked (a shimmer in the image) until close or hit; blade rush
//    Drone      hovers above the ground, circles you, quick laser bursts
//    Turret     fixed sentry gun with long bursts
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static inline float frandX() { return float(rand()) / float(RAND_MAX); }
static inline float frandX(float a, float b) { return a + (b - a) * frandX(); }

const std::vector<uint32_t> &MainWindow::atlasFor(const Enemy &e) const
{
    if (e.bossKind) return *bossAtlas(e);
    if (e.elite && e.type <= ET_SNIPER) return m_eliteAtlas;
    return m_typeAtlas[e.type];
}
const std::vector<uint8_t> &MainWindow::maskFor(const Enemy &e) const
{
    if (e.bossKind) return *bossMask(e);
    if (e.elite && e.type <= ET_SNIPER) return m_eliteMask;
    return m_typeMask[e.type];
}

const char *MainWindow::enemyTypeName(const Enemy &e) const
{
    static const char *N[ET_COUNT] = { "RIFLEMAN", "SCOUT", "SHOTGUNNER", "SNIPER", "GRENADIER", "SHIELD TROOPER", "MEDIC", "HEAVY", "STALKER", "DRONE", "SENTRY GUN" };
    if (e.bossKind) return bossName(e.bossKind);
    if (e.elite && e.type <= ET_SNIPER) return "ELITE GUARD";
    return N[clampi(e.type, 0, ET_COUNT - 1)];
}

// Move with the same footing rules as the original troopers (no ledges, no hazards)
void MainWindow::moveEnemy(Enemy &e, int idx, float vx, float vy, float spd, float dt)
{
    for (size_t j = 0; j < m_gs.enemies.size(); j++) {           // separation
        if (int(j) == idx || !m_gs.enemies[j].alive) continue;
        float ox = e.x - m_gs.enemies[j].x, oy = e.y - m_gs.enemies[j].y, o2 = ox * ox + oy * oy;
        if (o2 < 0.5f && o2 > 1e-4f) { float o = std::sqrt(o2); vx += ox / o * 0.8f; vy += oy / o * 0.8f; spd = std::max(spd, 0.6f); }
    }
    float vl = std::sqrt(vx * vx + vy * vy);
    float ox = e.x, oy = e.y;
    if (vl > 0.01f && spd > 0) {
        if (e.shockT > 0) spd *= 0.4f;
        float stepX = vx / vl * spd * dt, stepY = vy / vl * spd * dt;
        if (e.type == ET_DRONE) {                               // flying: only walls stop it
            if (posValid(e.x + stepX, e.y, 0.3f)) e.x += stepX;
            if (posValid(e.x, e.y + stepY, 0.3f)) e.y += stepY;
        } else {
            float hz = floorH(e.x, e.y);
            auto ok = [&](float x, float y) {
                int cx = int(x), cy = int(y);
                return canStep(x, y, hz, 0.3f, 0.25f) && floorH(x, y) > hz - 0.3f && !m_acid[cy][cx];
            };
            if (ok(e.x + stepX, e.y)) e.x += stepX; else e.wanderTimer = 0;
            if (ok(e.x, e.y + stepY)) e.y += stepY; else e.wanderTimer = 0;
        }
    }
    float moved = std::sqrt((e.x - ox) * (e.x - ox) + (e.y - oy) * (e.y - oy));
    e.moving = moved > 0.0005f;
    if (e.moving) e.walkPhase += moved * (e.type == ET_STALKER ? 2.f : 1.3f);
}

// A lobbed grenade aimed at the target's feet (lead for its movement)
void MainWindow::enemyGrenade(Enemy &e, float tx, float ty)
{
    Projectile p;
    p.x = e.x; p.y = e.y; p.z = floorH(e.x, e.y) + 0.9f;
    p.kind = PJ_GRENADE; p.owner = PO_ENEMY; p.throwKind = -1;
    float acc = DIFF_CFG[m_difficulty].enemyAccuracy;
    tx += frandX(-1.f, 1.f) * (1.2f - acc); ty += frandX(-1.f, 1.f) * (1.2f - acc);
    float dx = tx - p.x, dy = ty - p.y, dist = std::sqrt(dx * dx + dy * dy) + 1e-3f;
    float vh = 6.f + dist * 0.3f, t = dist / vh;
    p.vx = dx / dist * vh; p.vy = dy / dist * vh;
    p.vz = (floorH(tx, ty) - p.z + 0.5f * 9.8f * t * t) / t;
    p.gravity = 9.8f; p.fuse = t + 0.9f; p.life = 8.f;
    p.splashR = 2.4f; p.splashDmg = float(18 + 6 * m_gs.level) * DIFF_CFG[m_difficulty].dmgReceivedMult;
    p.landX = tx; p.landY = ty;
    m_gs.projectiles.push_back(p);
    sfxAt(SND_GL_THUMP, e.x, e.y, 0.8f, frandX(0.95f, 1.1f));
}

void MainWindow::updateSpecialEnemy(int idx, float dt, float tgX, float tgY, float tgZ, float d, bool los, float detectRange)
{
    Enemy &e = m_gs.enemies[size_t(idx)];
    const DiffConfig &dc = DIFF_CFG[m_difficulty];
    float dx = tgX - e.x, dy = tgY - e.y;
    float ux = d > 0.01f ? dx / d : 0.f, uy = d > 0.01f ? dy / d : 0.f;
    float perpX = -uy * (e.moveX >= 0 ? 1.f : -1.f), perpY = ux * (e.moveX >= 0 ? 1.f : -1.f);
    e.strafeTimer -= dt;
    if (e.strafeTimer <= 0) { e.strafeTimer = frandX(2.f, 3.6f); e.moveX = frandX() < 0.5f ? -1.f : 1.f; }
    bool alert = e.state == ES_ALERT;
    float vx = 0, vy = 0, spd = 0;

    // idle: wander slowly (machines hold still)
    if (!alert) {
        if (e.type != ET_TURRET) {
            e.wanderTimer -= dt;
            if (e.wanderTimer <= 0) {
                e.wanderTimer = frandX(2.f, 4.5f);
                if (frandX() < 0.5f) { e.moveX = 0; e.moveY = 0; }
                else { float a = frandX(0, 6.2831853f); e.moveX = std::cos(a); e.moveY = std::sin(a); }
            }
            vx = e.moveX; vy = e.moveY; spd = (vx != 0 || vy != 0) ? 0.45f : 0.f;
        }
        if (e.type == ET_STALKER) e.cloak = std::min(1.f, e.cloak + dt);
        moveEnemy(e, idx, vx, vy, spd, dt);
        return;
    }

    switch (e.type) {
    case ET_GRENADIER: {
        // keep 7-12 units away; lob a grenade every few seconds at your feet
        if (d < 6.f) { vx = -ux + perpX * 0.4f; vy = -uy + perpY * 0.4f; spd = 0.85f; }
        else if (d > 12.f || !los) { vx = ux; vy = uy; spd = 0.8f; }
        else { vx = perpX; vy = perpY; spd = 0.5f; }
        e.grenT -= dt;
        if (los && d < 16.f && e.grenT <= 0.f && e.hitFlash <= 0.f) {
            const Player &pl = m_gs.player;
            float lead = d / 9.f;
            bool atPlayer = std::fabs(tgX - pl.posX) < 0.01f && std::fabs(tgY - pl.posY) < 0.01f;
            enemyGrenade(e, tgX + (atPlayer ? pl.velX * lead : 0.f), tgY + (atPlayer ? pl.velY * lead : 0.f));
            e.grenT = frandX(3.2f, 4.5f) * dc.fireCoolMult;
            e.shootAnim = 0.3f;
        }
        break;
    }
    case ET_SHIELD: {
        // steady advance behind the shield, pistol shots
        if (d > 3.f || !los) { vx = ux * 0.9f + perpX * 0.2f; vy = uy * 0.9f + perpY * 0.2f; spd = 0.7f; }
        else { vx = perpX; vy = perpY; spd = 0.4f; }
        if (los && d < 12.f && e.fireCooldown <= 0 && e.hitFlash <= 0) {
            enemyFire(e, tgX, tgY, tgZ); e.shootAnim = 0.2f;
            e.fireCooldown = frandX(0.9f, 1.4f) * dc.fireCoolMult;
        }
        break;
    }
    case ET_MEDIC: {
        // find a fallen ally to revive (not a headshot kill, not a commander)
        int best = -1; float bestD = 9.f;
        for (int j = 0; j < int(m_gs.enemies.size()); j++) {
            const Enemy &o = m_gs.enemies[size_t(j)];
            if (o.alive || o.killedByHeadshot || o.bossKind || o.deathTime > 8.f || o.type == ET_DRONE || o.type == ET_TURRET || j == idx) continue;
            float od = std::sqrt((o.x - e.x) * (o.x - e.x) + (o.y - e.y) * (o.y - e.y));
            if (od < bestD) { bestD = od; best = j; }
        }
        if (best >= 0) {
            Enemy &o = m_gs.enemies[size_t(best)];
            if (bestD > 0.7f) { vx = o.x - e.x; vy = o.y - e.y; spd = 1.1f; e.reviveT = 0.f; }
            else {
                e.reviveT += dt;
                if (int(e.reviveT * 4) != int((e.reviveT - dt) * 4)) {
                    Particle p; p.type = PT_SPARK; p.x = o.x; p.y = o.y; p.z = floorH(o.x, o.y) + 0.3f;
                    p.vx = frandX(-0.5f, 0.5f); p.vy = frandX(-0.5f, 0.5f); p.vz = 1.5f; p.life = p.maxLife = 0.4f; p.size = 0.015f; p.color = rgb(90, 255, 140);
                    m_gs.particles.push_back(p);
                }
                if (e.reviveT >= 1.8f) {
                    o.alive = true; o.state = ES_ALERT; o.hp = std::max(1, o.maxHp / 2); o.deathTime = 0.f; o.corpseFade = 1.f;
                    o.hitFlash = 0.3f; o.fireCooldown = 1.f; o.burnT = 0.f;
                    e.reviveT = 0.f;
                    sfxAt(SND_PICKUP_HEALTH, o.x, o.y, 0.8f, 0.8f);
                    pushFeed(QString("MEDIC REVIVED A %1").arg(enemyTypeName(o)), 2.5f);
                }
            }
        } else {
            e.reviveT = 0.f;
            if (d > 7.f || !los) { vx = ux * 0.7f + perpX * 0.4f; vy = uy * 0.7f + perpY * 0.4f; spd = 0.9f; }
            else { vx = -ux * 0.3f + perpX; vy = -uy * 0.3f + perpY; spd = 0.8f; }
            if (los && d < 11.f && e.fireCooldown <= 0 && e.hitFlash <= 0) {
                enemyFire(e, tgX, tgY, tgZ); e.shootAnim = 0.2f;
                e.fireCooldown = frandX(1.0f, 1.5f) * dc.fireCoolMult;
            }
        }
        // heal wounded allies close by
        for (Enemy &o : m_gs.enemies) {
            if (!o.alive || &o == &e || o.hp >= o.maxHp) continue;
            if ((o.x - e.x) * (o.x - e.x) + (o.y - e.y) * (o.y - e.y) > 9.f) continue;
            e.healAcc += 6.f * dt;
            if (e.healAcc >= 1.f) { int h = int(e.healAcc); e.healAcc -= h; o.hp = std::min(o.maxHp, o.hp + h); o.hpShow = std::max(o.hpShow, 0.5f); }
        }
        break;
    }
    case ET_HEAVY: {
        // slow push; long suppressive bursts
        if (d > 6.f || !los) { vx = ux; vy = uy; spd = 0.5f; }
        else { vx = perpX; vy = perpY; spd = 0.25f; }
        if (e.burstLeft > 0) {
            e.burstT -= dt; spd = 0.f;
            if (e.burstT <= 0.f && los) { enemyFire(e, tgX + frandX(-0.4f, 0.4f), tgY + frandX(-0.4f, 0.4f), tgZ); e.burstLeft--; e.burstT = 0.09f; e.shootAnim = 0.12f; }
            if (!los) e.burstLeft = 0;
        } else if (los && d < 15.f && e.fireCooldown <= 0 && e.hitFlash <= 0) {
            e.burstLeft = 5; e.burstT = 0.f;
            e.fireCooldown = frandX(2.4f, 3.2f) * dc.fireCoolMult;
        }
        break;
    }
    case ET_STALKER: {
        // cloaked rush; decloaks close up or when hurt; slashes in melee range
        e.revealT = std::max(0.f, e.revealT - dt);
        bool reveal = d < 3.f || e.revealT > 0.f || m_radarT > 0.f;
        e.cloak += ((reveal ? 0.f : 1.f) - e.cloak) * std::min(1.f, dt * 4.f);
        vx = ux + perpX * (d > 4.f ? 0.5f : 0.f); vy = uy + perpY * (d > 4.f ? 0.5f : 0.f); spd = d > 1.0f ? 1.7f : 0.f;
        e.fireCooldown = std::max(0.f, e.fireCooldown);
        if (d < 1.15f && e.fireCooldown <= 0.f && e.hitFlash <= 0.f) {
            e.fireCooldown = 0.9f; e.shootAnim = 0.25f; e.revealT = 1.f;
            sfxAt(SND_KNIFE_SWING, e.x, e.y, 0.9f, 0.85f);
            int dmg = int((14 + 5 * m_gs.level) * dc.dmgReceivedMult);
            bool atPlayer = std::fabs(tgX - m_gs.player.posX) < 0.01f && std::fabs(tgY - m_gs.player.posY) < 0.01f;
            if (atPlayer) {
                Player &pl = m_gs.player;
                dmg = int(dmg * (1.f - pl.damageReduction));
                pl.hp -= dmg; m_gs.zoneDamage += dmg; pl.damageFlash = 0.5f; pl.shakeY = -8.f;
                addDamageMark(e.x, e.y, 1.f);
                sfx(SND_KNIFE_HIT, 0.8f); sfx(SND_PLAYER_HURT, 0.8f);
                if (pl.hp <= 0) { pl.hp = 0; m_gs.gameOver = true; pl.adsMode = false; m_killerIdx = idx; }
            } else for (int ai = 0; ai < int(m_gs.actors.size()); ai++)
                if (std::fabs(m_gs.actors[size_t(ai)].posX - tgX) < 0.01f) damageActor(ai, dmg, e.x, e.y);
        }
        break;
    }
    case ET_DRONE: {
        // circle the target at ~5 units, quick bursts
        float want = 5.f;
        vx = perpX * 1.f + ux * (d - want) * 0.4f; vy = perpY * 1.f + uy * (d - want) * 0.4f; spd = 1.6f;
        e.hover += (1.35f + 0.12f * std::sin(m_time * 2.f + idx) - e.hover) * std::min(1.f, dt * 3.f);
        if (e.burstLeft > 0) {
            e.burstT -= dt;
            if (e.burstT <= 0.f && los) { enemyFire(e, tgX, tgY, tgZ); e.burstLeft--; e.burstT = 0.11f; e.shootAnim = 0.1f; }
        } else if (los && d < 13.f && e.fireCooldown <= 0 && e.hitFlash <= 0) {
            e.burstLeft = 3; e.burstT = 0.f;
            e.fireCooldown = frandX(1.4f, 2.0f) * dc.fireCoolMult;
        }
        break;
    }
    case ET_TURRET: {
        if (e.burstLeft > 0) {
            e.burstT -= dt;
            if (e.burstT <= 0.f && los) { enemyFire(e, tgX, tgY, tgZ); e.burstLeft--; e.burstT = 0.12f; e.shootAnim = 0.1f; }
            if (!los) e.burstLeft = 0;
        } else if (los && d < 17.f && e.fireCooldown <= 0 && e.hitFlash <= 0) {
            e.burstLeft = 5; e.burstT = 0.f;
            e.fireCooldown = frandX(1.6f, 2.4f) * dc.fireCoolMult;
        }
        break;
    }
    default: break;
    }
    (void)detectRange;
    if (e.hitFlash > 0) spd *= 0.3f;
    if (e.type == ET_TURRET) spd = 0.f;
    moveEnemy(e, idx, vx, vy, spd, dt);
}

// What each archetype adds to a zone's mix as the levels go up
EnemyType MainWindow::rollEnemyType(int lv, bool elevated) const
{
    if (elevated && lv >= 2 && rand() % 2) return ET_SNIPER;
    int r = rand() % 100;
    if (lv >= 4 && r < 6) return ET_STALKER;
    if (lv >= 3 && r < 14) return ET_HEAVY;
    if (lv >= 3 && r < 22) return ET_DRONE;
    if (lv >= 2 && r < 32) return ET_SHIELD;
    if (lv >= 2 && r < 42) return ET_GRENADIER;
    if (lv >= 2 && r < 50) return ET_MEDIC;
    if (r < 62) return ET_SCOUT;
    if (r < 80) return ET_SHOTGUNNER;
    return ET_RIFLEMAN;
}
