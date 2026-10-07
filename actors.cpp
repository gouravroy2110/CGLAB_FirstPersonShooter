// =============================================================================
//  actors.cpp  -  Squad bots: AI teammates that are full actors
//
//  A bot is an Actor (the same Player struct you are) moved by the shared
//  stepMovement() from an InputCmd its brain fills in each tick: follow the
//  leader, pick the nearest visible hostile, strafe and fire. Hostiles pick
//  the nearest visible target among you and your squad.
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static inline float frandA() { return float(rand()) / float(RAND_MAX); }
static inline float frandA(float a, float b) { return a + (b - a) * frandA(); }

static const char *const SQUAD_NAMES[] = { "HAWK", "VIPER", "ECHO", "GHOST" };

// Spawn the chosen number of squad bots in a ring around the player
void MainWindow::spawnSquad()
{
    m_gs.actors.clear();
    const Player &pl = m_gs.player;
    for (int i = 0; i < m_squadSize; i++) {
        Actor a;
        a.id = i + 1; a.team = 0; a.ctl = CTL_BOT;
        std::snprintf(a.name, sizeof(a.name), "%s", SQUAD_NAMES[i % 4]);
        a.hp = 120; a.curWeapon = i % 2 ? WPN_SHOTGUN : WPN_RIFLE;
        a.speedMult = 1.f;
        a.posX = pl.posX; a.posY = pl.posY;
        for (int t = 0; t < 24; t++) {                     // a free spot near the leader
            float ang = (i + t * 0.37f) * 2.1f, r = 1.2f + 0.15f * t;
            float x = pl.posX + std::cos(ang) * r, y = pl.posY + std::sin(ang) * r;
            if (posValid(x, y, 0.3f) && reachable(int(x), int(y)) && std::fabs(floorH(x, y) - floorH(pl.posX, pl.posY)) < 0.3f) { a.posX = x; a.posY = y; break; }
        }
        a.footZ = floorH(a.posX, a.posY);
        a.dirX = pl.dirX; a.dirY = pl.dirY;
        a.thinkT = frandA(0.f, 0.3f);
        m_gs.actors.push_back(a);
    }
}

// Nearest visible target for a hostile at (x, y, z): the player or a squad bot.
// Returns -1 for the player, the actor index otherwise.
int MainWindow::hostileTarget(float x, float y, float z, float &tx, float &ty, float &tz, float &dist) const
{
    const Player &pl = m_gs.player;
    int best = -1;
    tx = pl.posX; ty = pl.posY; tz = pl.footZ + 0.5f;
    dist = std::sqrt((tx - x) * (tx - x) + (ty - y) * (ty - y));
    bool bestVis = hasLOS(x, y, z, tx, ty, tz);
    for (int i = 0; i < int(m_gs.actors.size()); i++) {
        const Actor &a = m_gs.actors[i];
        if (!a.alive) continue;
        float ax = a.posX, ay = a.posY, az = a.footZ + 0.5f;
        float d = std::sqrt((ax - x) * (ax - x) + (ay - y) * (ay - y));
        if (d > 22.f) continue;
        bool vis = hasLOS(x, y, z, ax, ay, az);
        // prefer visible targets, then the closer one
        if ((vis && !bestVis) || (vis == bestVis && d < dist)) { best = i; tx = ax; ty = ay; tz = az; dist = d; bestVis = vis; }
    }
    return best;
}

void MainWindow::damageActor(int i, int dmg, float fromX, float fromY)
{
    Actor &a = m_gs.actors[size_t(i)];
    if (!a.alive) return;
    a.hp -= dmg; a.hitFlash = 0.15f;
    sfxAt(SND_HIT_FLESH, a.posX, a.posY, 0.6f, frandA(0.9f, 1.1f));
    if (a.target < 0) {                                    // turn towards the shooter
        float dx = fromX - a.posX, dy = fromY - a.posY, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
        a.dirX = dx / d; a.dirY = dy / d;
    }
    if (a.hp <= 0) {
        a.hp = 0; a.alive = false; a.respawnT = 12.f;
        a.velX = a.velY = 0.f;
        spawnBlood(a.posX, a.posY, a.footZ + 0.5f, 14, 1.f);
        addDecal(m_blood, a.posX, a.posY, 0.35f, 150);
        pushFeed(QString("SQUAD  %1 IS DOWN - BACK IN 12 S").arg(a.name), 3.5f);
        sfx(SND_RADIO, 0.6f);
    }
}

void MainWindow::updateActors(float dt)
{
    Player &pl = m_gs.player;
    for (int ai = 0; ai < int(m_gs.actors.size()); ai++) {
        Actor &a = m_gs.actors[size_t(ai)];
        a.hitFlash = std::max(0.f, a.hitFlash - dt);
        a.shootAnim = std::max(0.f, a.shootAnim - dt);
        if (!a.alive) {
            a.respawnT -= dt;
            if (a.respawnT <= 0.f) {                       // rejoin beside the leader
                a.alive = true; a.hp = 120; a.target = -1;
                a.posX = pl.posX - pl.dirX * 0.9f; a.posY = pl.posY - pl.dirY * 0.9f;
                if (!posValid(a.posX, a.posY, 0.3f)) { a.posX = pl.posX; a.posY = pl.posY; }
                a.footZ = floorH(a.posX, a.posY); a.velZ = 0.f; a.onGround = true;
                pushFeed(QString("SQUAD  %1 REJOINED").arg(a.name), 2.5f);
            }
            continue;
        }
        float az = a.footZ + 0.55f;

        // --- Think: pick a target a few times per second -------------------------------
        a.thinkT -= dt;
        if (a.thinkT <= 0.f) {
            a.thinkT = frandA(0.22f, 0.35f);
            int best = -1; float bestD = 18.f;
            for (int i = 0; i < int(m_gs.enemies.size()); i++) {
                const Enemy &e = m_gs.enemies[size_t(i)];
                if (!e.alive) continue;
                float d = std::sqrt((e.x - a.posX) * (e.x - a.posX) + (e.y - a.posY) * (e.y - a.posY));
                if (d >= bestD) continue;
                if (!hasLOS(a.posX, a.posY, az, e.x, e.y, floorH(e.x, e.y) + 0.6f)) continue;
                best = i; bestD = d;
            }
            if (best != a.target) a.aimErr = frandA(0.25f, 0.6f);   // re-acquiring takes a moment
            a.target = best;
            // strafe direction changes now and then
            if (frandA() < 0.25f) a.cmd.strafe = frandA() < 0.5f ? -0.7f : 0.7f;
        }

        InputCmd cmd;
        cmd.strafe = 0.f;
        float leadDX = pl.posX - a.posX, leadDY = pl.posY - a.posY;
        float leadD = std::sqrt(leadDX * leadDX + leadDY * leadDY);
        if (a.target >= 0 && m_gs.enemies[size_t(a.target)].alive) {
            const Enemy &e = m_gs.enemies[size_t(a.target)];
            float dx = e.x - a.posX, dy = e.y - a.posY, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
            // turn towards the target (limited turn rate)
            float want = std::atan2(dy, dx), cur = std::atan2(a.dirY, a.dirX);
            float diff = std::remainder(want - cur, 6.2831853f);
            cur += clampf(diff, -5.f * dt, 5.f * dt);
            a.dirX = std::cos(cur); a.dirY = std::sin(cur);
            // keep a fighting distance, strafe, don't stray far from the leader
            float ideal = a.curWeapon == WPN_SHOTGUN ? 3.5f : 7.f;
            cmd.forward = d > ideal + 1.5f ? 0.8f : d < ideal - 1.5f ? -0.6f : 0.f;
            if (leadD > 10.f) cmd.forward = 0.f;
            cmd.strafe = a.cmd.strafe;
            // Fire when roughly on target
            a.aimErr = std::max(0.f, a.aimErr - dt);
            a.fireT -= dt;
            if (std::fabs(diff) < 0.15f && a.aimErr <= 0.f && a.fireT <= 0.f) {
                bool shotgun = a.curWeapon == WPN_SHOTGUN;
                a.fireT = shotgun ? frandA(0.9f, 1.3f) : frandA(0.18f, 0.35f);
                a.shootAnim = 0.12f;
                sfxAt(shotgun ? SND_SHOTGUN : SND_RIFLE, a.posX, a.posY, 0.55f, frandA(0.95f, 1.05f));
                float hitChance = clampf(1.05f - d / 18.f, 0.25f, 0.9f) * (shotgun ? (d < 6.f ? 1.f : 0.4f) : 1.f);
                float ez = floorH(e.x, e.y);
                Tracer t; t.x0 = a.posX + a.dirX * 0.5f; t.y0 = a.posY + a.dirY * 0.5f; t.z0 = a.footZ + 0.64f;
                if (frandA() < hitChance) {
                    bool head = frandA() < 0.12f;
                    int dmg = shotgun ? int(frandA(45.f, 70.f)) : int(frandA(16.f, 24.f));
                    if (head) dmg *= 2;
                    t.x1 = e.x; t.y1 = e.y; t.z1 = ez + (head ? 0.85f : 0.55f) * e.scale;
                    damageEnemy(a.target, dmg, head ? PART_HEAD : PART_BODY, t.x1, t.y1, t.z1, ai);
                } else {
                    t.x1 = e.x + frandA(-0.6f, 0.6f); t.y1 = e.y + frandA(-0.6f, 0.6f); t.z1 = ez + frandA(0.2f, 1.0f);
                    spawnImpact(t.x1, t.y1, std::max(t.z1, groundAt(t.x1, t.y1)), true);
                }
                t.len = std::sqrt((t.x1 - t.x0) * (t.x1 - t.x0) + (t.y1 - t.y0) * (t.y1 - t.y0) + (t.z1 - t.z0) * (t.z1 - t.z0));
                m_gs.tracers.push_back(t);
            }
        } else {
            a.target = -1;
            // Follow: hold a slot behind and to the side of the leader
            float side = (a.id % 2) ? 1.f : -1.f;
            float slotX = pl.posX - pl.dirX * 1.4f - pl.dirY * side * (0.9f + 0.4f * (a.id / 2));
            float slotY = pl.posY - pl.dirY * 1.4f + pl.dirX * side * (0.9f + 0.4f * (a.id / 2));
            float dx = slotX - a.posX, dy = slotY - a.posY, d = std::sqrt(dx * dx + dy * dy);
            if (d > 0.5f) {
                a.dirX = dx / d; a.dirY = dy / d;
                cmd.forward = d > 4.f ? 1.f : std::min(1.f, d / 1.5f);
                cmd.sprint = d > 6.f;
            } else {
                // idle: look where the leader looks
                a.dirX += (pl.dirX - a.dirX) * std::min(1.f, dt * 3.f);
                a.dirY += (pl.dirY - a.dirY) * std::min(1.f, dt * 3.f);
                float l = std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY) + 1e-6f;
                a.dirX /= l; a.dirY /= l;
            }
        }
        a.sprinting = cmd.sprint;
        cmd.yaw = std::atan2(a.dirY, a.dirX);

        // Stuck against a ledge? hop. Far behind and out of sight? catch up.
        float ox = a.posX, oy = a.posY;
        MoveEvents mv = stepMovement(*this, a, cmd, dt);
        (void)mv;
        float moved = std::sqrt((a.posX - ox) * (a.posX - ox) + (a.posY - oy) * (a.posY - oy));
        if ((std::fabs(cmd.forward) > 0.3f) && moved < 0.3f * dt && a.onGround) { InputCmd j; j.jump = true; stepMovement(*this, a, j, 0.f); }
        if (leadD > 16.f && !hasLOS(pl.posX, pl.posY, pl.footZ + 0.5f, a.posX, a.posY, az)) {
            float bx = pl.posX - pl.dirX * 1.2f, by = pl.posY - pl.dirY * 1.2f;
            if (posValid(bx, by, 0.3f)) { a.posX = bx; a.posY = by; a.footZ = floorH(bx, by); a.velX = a.velY = a.velZ = 0.f; }
        }
        if (moved > 0.0005f) a.walkPhase += moved * 9.f;
        // acid hurts bots too
        int cx = int(a.posX), cy = int(a.posY);
        if (inside(cx, cy) && m_acid[cy][cx] && a.footZ <= floorH(a.posX, a.posY) + 0.05f) {
            a.cmd.seq += 1;
            if (a.cmd.seq % 20 == 0) damageActor(ai, 4, a.posX, a.posY);
        }
    }
}
