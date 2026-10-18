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
#include "soldier3d.h"
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
    float vh = 4.f + dist * 0.15f, t = dist / vh;                    // a high lob that clears ledges and low walls
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
    // lost sight of the target: walk the grid path towards it instead of pushing into a wall
    if (!los && e.state == ES_ALERT && e.hover <= 0.f && e.type != ET_TURRET && spd > 0.f && d < 24.f) {
        e.navT -= dt;
        if (e.navT <= 0.f) { e.navT = 0.3f; if (!gridStep(e.x, e.y, tgX, tgY, e.navX, e.navY)) { e.navX = tgX; e.navY = tgY; } }
        vx = e.navX - e.x; vy = e.navY - e.y;
    }
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

// ─────────────────────────────────────────────────────────────────────────────
//  3D bodies: regular hostiles are drawn as rounded 3D soldiers (soldier3d.h);
//  bosses, drones and turrets keep their own painted sprites
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::soldierEnemy(const Enemy &e) const
{
    return !e.bossKind && e.type != ET_DRONE && e.type != ET_TURRET && e.hover <= 0.f;
}

bool MainWindow::enemyPose(int idx, Soldier3D &sp) const
{
    if (idx < 0 || idx >= int(m_gs.enemies.size())) return false;
    const Enemy &e = m_gs.enemies[size_t(idx)];
    if (!soldierEnemy(e)) return false;
    static const int GUN[ET_COUNT] = { WPN_RIFLE, WPN_MACHINE_PISTOL, WPN_SHOTGUN, WPN_SNIPER, WPN_GRENADE_LAUNCHER, WPN_PISTOL,
                                       WPN_SMG, WPN_LMG, WPN_MACHETE, WPN_SMG, WPN_LMG };
    static const int GEAR[ET_COUNT] = { SG_NONE, SG_NONE, SG_NONE, SG_SNIPER, SG_GRENADIER, SG_SHIELD, SG_MEDIC, SG_HEAVY, SG_STALKER, SG_NONE, SG_NONE };
    const SoldierLook *looks[ET_COUNT] = { &ENEMY_LOOK, &SCOUT_LOOK, &SHOTGUN_LOOK, &SNIPER_LOOK, &GRENADIER_LOOK, &SHIELD_LOOK,
                                           &MEDIC_LOOK, &HEAVY_LOOK, &STALKER_LOOK, &ENEMY_LOOK, &ENEMY_LOOK };
    int t = clampi(int(e.type), 0, ET_COUNT - 1);
    sp = Soldier3D{};
    sp.x = e.x; sp.y = e.y; sp.z = floorH(e.x, e.y);
    sp.yaw = e.yaw;
    sp.scale = e.scale;
    sp.weapon = GUN[t];
    sp.gear = GEAR[t];
    sp.look = e.elite ? ELITE_LOOK : *looks[t];
    sp.band = e.type == ET_MEDIC ? rgb(240, 240, 240) : rgb(230, 60, 50);
    sp.walk = e.walkPhase * 7.f;
    sp.move = e.moving && e.alive ? (e.type == ET_SCOUT || e.type == ET_STALKER ? 1.f : 0.8f) : 0.f;
    sp.sprint = e.type == ET_STALKER && e.moving;
    sp.recoil = e.shootAnim > 0.f ? 1.f : 0.f;
    sp.helmet = !e.helmetOff;
    sp.flashPart = e.flashPart;
    sp.flashK = e.alive ? clampf(e.partFlashT / 0.14f, 0.f, 1.f) : 0.f;
    sp.seed = float((idx * 37) % 100) / 100.f;
    sp.crouch = e.alive && e.hasCover && e.peekT <= 0.f && e.state == ES_ALERT ? 0.75f : 0.f;   // ducking behind cover
    if (e.alive && e.state == ES_ALERT) {
        const Player &pl = m_gs.player;
        float dx = pl.posX - e.x, dy = pl.posY - e.y, d = std::sqrt(dx * dx + dy * dy) + 1e-3f;
        sp.aimPitch = clampf(std::atan2(pl.footZ + 0.5f - (sp.z + 0.62f), d), -0.8f, 0.8f);
    }
    if (!e.alive) {
        sp.fall = std::min(1.f, e.deathTime / 0.75f);
        sp.fallRel = e.fallYaw - e.yaw;
        sp.alpha = e.corpseFade;
    } else if (e.type == ET_STALKER) sp.alpha = 1.f - 0.85f * e.cloak;
    return true;
}

// Facing, hit flashes and loose helmets
void MainWindow::updateEnemyAnim(float dt)
{
    const Player &pl = m_gs.player;
    for (Enemy &e : m_gs.enemies) {
        e.partFlashT = std::max(0.f, e.partFlashT - dt);
        if (!e.alive) continue;
        float want = e.yaw;
        if (e.state == ES_ALERT) want = std::atan2(pl.posY - e.y, pl.posX - e.x);
        else if (e.moving && (e.moveX * e.moveX + e.moveY * e.moveY) > 1e-4f) want = std::atan2(e.moveY, e.moveX);
        float diff = std::remainder(want - e.yaw, 6.2831853f);
        e.yaw += clampf(diff, -7.f * dt, 7.f * dt);
    }
    for (LooseHelmet &h : m_gs.helmets) {
        h.life -= dt;
        h.vz -= 9.f * dt;
        float nx = h.x + h.vx * dt, ny = h.y + h.vy * dt;
        if (!solidCell(int(nx), int(ny))) { h.x = nx; h.y = ny; } else { h.vx *= -0.4f; h.vy *= -0.4f; }
        h.z += h.vz * dt;
        float g = floorH(h.x, h.y) + 0.03f;
        if (h.z < g) {
            h.z = g;
            if (h.vz < -1.2f) { h.vz = -h.vz * 0.35f; sfxAt(SND_RICOCHET, h.x, h.y, 0.25f, 1.6f); }
            else h.vz = 0.f;
            h.vx *= 0.6f; h.vy *= 0.6f; h.spin *= 0.6f;
        }
        h.ang += h.spin * dt;
    }
    m_gs.helmets.erase(std::remove_if(m_gs.helmets.begin(), m_gs.helmets.end(), [](const LooseHelmet &h) { return h.life <= 0.f; }), m_gs.helmets.end());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Squad tactics: cover and flanking
//
//    cover   riflemen and snipers look for LOW cover: a floor cell where a
//            crouched soldier is hidden from the target but standing up gives a
//            clear line. They duck (can't see or shoot, smaller target), then
//            pop up to fire, then duck again.
//    flank   scouts, shotgunners and every other rifleman circle to a spot
//            60-110 degrees around the target with a line of sight, so the
//            player gets fire from two directions.
//  Paths around walls come from a small breadth-first search on the grid.
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::gridStep(float x, float y, float gx, float gy, float &wx, float &wy) const
{
    int sx = int(x), sy = int(y), tx = int(gx), ty = int(gy);
    if (sx == tx && sy == ty) { wx = gx; wy = gy; return true; }
    const int R = 12, N = 2 * R + 1;                        // search window around the start
    if (std::abs(tx - sx) > R || std::abs(ty - sy) > R) { wx = gx; wy = gy; return false; }
    static std::vector<int16_t> from;
    from.assign(size_t(N) * N, -1);
    auto idx = [&](int cx, int cy) { return (cy - sy + R) * N + (cx - sx + R); };
    std::vector<int> q; q.reserve(N * N);
    from[size_t(idx(sx, sy))] = int16_t(idx(sx, sy));
    q.push_back(idx(sx, sy));
    static const int D[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    bool found = false;
    for (size_t h = 0; h < q.size() && !found; h++) {
        int c = q[h], cx = c % N - R + sx, cy = c / N - R + sy;
        for (const auto &d : D) {
            int nx = cx + d[0], ny = cy + d[1];
            if (std::abs(nx - sx) > R || std::abs(ny - sy) > R || !inside(nx, ny)) continue;
            int ni = idx(nx, ny);
            if (from[size_t(ni)] >= 0 || solidCell(nx, ny) || m_acid[ny][nx]) continue;
            if (std::fabs(floorH(nx + 0.5f, ny + 0.5f) - floorH(cx + 0.5f, cy + 0.5f)) > 0.3f) continue;   // ledges need a jump
            from[size_t(ni)] = int16_t(c);
            if (nx == tx && ny == ty) { found = true; break; }
            q.push_back(ni);
        }
    }
    if (!found) { wx = gx; wy = gy; return false; }
    // walk back from the goal to the cell right after the start
    int c = idx(tx, ty);
    while (from[size_t(c)] != idx(sx, sy)) c = from[size_t(c)];
    wx = c % N - R + sx + 0.5f; wy = c / N - R + sy + 0.5f;
    return true;
}

bool MainWindow::findCover(int i, float tgX, float tgY, float tgZ, float &cx, float &cy)
{
    Enemy &e = m_gs.enemies[size_t(i)];
    float best = 1e9f; bool ok = false;
    int ex = int(e.x), ey = int(e.y);
    for (int y = ey - 6; y <= ey + 6; y++) for (int x = ex - 6; x <= ex + 6; x++) {
        if (!inside(x, y) || solidCell(x, y) || m_acid[y][x] || !reachable(x, y)) continue;
        float px = x + 0.5f, py = y + 0.5f;
        float dt2 = (px - tgX) * (px - tgX) + (py - tgY) * (py - tgY);
        if (dt2 < 16.f || dt2 > 196.f) continue;                        // 4-14 units from the target
        // someone else already uses this spot
        bool taken = false;
        for (size_t j = 0; j < m_gs.enemies.size() && !taken; j++)
            if (int(j) != i && m_gs.enemies[j].alive && m_gs.enemies[j].hasCover && int(m_gs.enemies[j].coverX) == x && int(m_gs.enemies[j].coverY) == y) taken = true;
        if (taken) continue;
        float g = floorH(px, py);
        bool hiddenLow = !hasLOS(tgX, tgY, tgZ, px, py, g + 0.35f);
        if (!hiddenLow) continue;
        bool seenHigh = hasLOS(tgX, tgY, tgZ, px, py, g + 0.72f);
        float peekX = px, peekY = py;
        if (!seenHigh) {
            // full cover: needs a free spot right beside it with a clear shot
            bool found = false;
            static const int N4[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
            for (const auto &n : N4) {
                int qx = x + n[0], qy = y + n[1];
                if (!inside(qx, qy) || solidCell(qx, qy) || m_acid[qy][qx]) continue;
                float qpx = qx + 0.5f, qpy = qy + 0.5f;
                if (std::fabs(floorH(qpx, qpy) - g) > 0.3f) continue;
                if (hasLOS(tgX, tgY, tgZ, qpx, qpy, floorH(qpx, qpy) + 0.62f)) { peekX = qpx; peekY = qpy; found = true; break; }
            }
            if (!found) continue;
        }
        float de = (px - e.x) * (px - e.x) + (py - e.y) * (py - e.y);
        float score = de + std::fabs(std::sqrt(dt2) - 8.f) * 2.f + (seenHigh ? 0.f : 3.f);   // low cover preferred
        if (score < best) { best = score; cx = px; cy = py; e.peekX = peekX; e.peekY = peekY; e.lowCover = seenHigh; ok = true; }
    }
    return ok;
}

bool MainWindow::findFlank(int i, float tgX, float tgY, float tgZ, float &fx, float &fy) const
{
    const Enemy &e = m_gs.enemies[size_t(i)];
    float base = std::atan2(e.y - tgY, e.x - tgX);
    float side = (i % 2) ? 1.f : -1.f;
    for (int k = 0; k < 10; k++) {
        float a = base + side * (1.05f + 0.1f * k) * (k % 2 ? -1.f : 1.f);
        float r = (e.type == ET_SHOTGUNNER ? 3.5f : 5.5f) + (k % 3);
        float px = tgX + std::cos(a) * r, py = tgY + std::sin(a) * r;
        int x = int(px), y = int(py);
        if (!inside(x, y) || solidCell(x, y) || m_acid[y][x] || !reachable(x, y) || !posValid(px, py, 0.25f)) continue;
        if (!hasLOS(px, py, floorH(px, py) + 0.62f, tgX, tgY, tgZ)) continue;
        fx = x + 0.5f; fy = y + 0.5f;
        return true;
    }
    return false;
}

// Returns true when the tactic owns this frame's movement. mayFire: allowed to shoot now.
bool MainWindow::updateEnemyTactics(int i, float dt, float tgX, float tgY, float tgZ, float d, bool los, float &vx, float &vy, float &spd, bool &mayFire)
{
    Enemy &e = m_gs.enemies[size_t(i)];
    mayFire = true;
    if (e.type != ET_RIFLEMAN && e.type != ET_SNIPER && e.type != ET_SCOUT && e.type != ET_SHOTGUNNER) return false;
    e.planT -= dt;
    if (e.planT <= 0.f) {
        e.planT = frandX(2.5f, 4.f);
        // roles: snipers hold cover, scouts / shotgunners flank, riflemen alternate when there are several
        int alertPeers = 0;
        for (const Enemy &o : m_gs.enemies) if (o.alive && o.state == ES_ALERT && !o.bossKind) alertPeers++;
        if (e.type == ET_SNIPER) e.role = 1;
        else if (e.type == ET_SCOUT || e.type == ET_SHOTGUNNER) e.role = alertPeers >= 2 ? 2 : 0;
        else e.role = alertPeers >= 2 ? (i % 2 ? 2 : 1) : 1;
        if (float(e.hp) < 0.5f * e.maxHp && e.role == 2) e.role = 1;      // hurt flankers fall back to cover
        // (re)validate the spot
        bool keep = e.hasCover && e.role == 1 && !hasLOS(tgX, tgY, tgZ, e.coverX, e.coverY, floorH(e.coverX, e.coverY) + 0.35f);
        if (!keep) {
            e.hasCover = false;
            float cx, cy;
            if (e.role == 1 && findCover(i, tgX, tgY, tgZ, cx, cy)) { e.coverX = cx; e.coverY = cy; }
            else if (e.role == 2 && findFlank(i, tgX, tgY, tgZ, cx, cy)) { e.coverX = cx; e.coverY = cy; }
            else e.role = 0;
        }
    }
    if (e.role == 0) { e.hasCover = false; return false; }
    float gx = e.coverX - e.x, gy = e.coverY - e.y, gd = std::sqrt(gx * gx + gy * gy);
    if (gd > (e.hasCover ? 1.6f : 0.35f)) {
        // travel along the grid path; shoot on the move only when already in the open
        e.hasCover = false;
        e.navT -= dt;
        if (e.navT <= 0.f) { e.navT = 0.3f; if (!gridStep(e.x, e.y, e.coverX, e.coverY, e.navX, e.navY)) { e.role = 0; return false; } }
        vx = e.navX - e.x; vy = e.navY - e.y;
        spd = e.role == 2 ? (e.type == ET_SCOUT ? 1.35f : 1.15f) : 1.1f;
        mayFire = e.role == 2 && los && d < 6.f;
        return true;
    }
    if (e.role == 2) { e.role = 0; return false; }                      // flank reached: fight from here
    // in cover: hide, then pop up (or step out) to shoot, then hide again
    e.hasCover = true;
    vx = vy = 0.f; spd = 0.f;
    if (!e.lowCover) {
        // full cover: the body moves between the cover cell and the peek spot
        float px = e.peekT > 0.f ? e.peekX : e.coverX, py = e.peekT > 0.f ? e.peekY : e.coverY;
        float qx = px - e.x, qy = py - e.y;
        if (qx * qx + qy * qy > 0.01f) { vx = qx; vy = qy; spd = 1.1f; }
    }
    if (e.peekT > 0.f) {
        e.peekT -= dt;
        mayFire = true;
        if (e.peekT <= 0.f) e.hideT = frandX(1.0f, 2.0f);
    } else {
        e.hideT -= dt;
        mayFire = false;
        if (e.hideT <= 0.f) { e.peekT = frandX(1.2f, 2.0f); e.fireCooldown = std::min(e.fireCooldown, 0.35f); }
    }
    return true;
}
