// =============================================================================
//  combat.cpp  -  The arsenal in play
//
//    effective stats   rarity x attachments x weapon row
//    inventory         4 slots (melee, sidearm, 2 primaries), drop / swap
//    firing            semi, auto, burst, bolt, charge, beam, revolver fan-fire,
//                      per-gun recoil patterns, bloom, sprint-to-fire delay
//    hit types         hitscan (+ wall penetration), ballistic bullets with
//                      drop, rockets, bouncing grenades, plasma, chain arc,
//                      wall-piercing rail, flame cone
//    explosions        radial damage with line of sight + falloff, craters
//                      (the heightfield is lowered and the light map re-baked)
//    melee             knife / machete arcs / baton stun / axe door-chop,
//                      silent takedowns, quick melee
//    throwables        frag (cook), smoke (blocks sight), flashbang, molotov, mine
//    gadgets           sentry turret, medkit station, radar, grapple, jetpack
//    status effects    burn, shock (slow), stun
// =============================================================================
#include "mainwindow.h"
#include "gunbuild.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static inline float frandC() { return float(rand()) / float(RAND_MAX); }
static inline float frandC(float a, float b) { return a + (b - a) * frandC(); }
static const float PI_C = 3.14159265f;

// ─────────────────────────────────────────────────────────────────────────────
//  Effective stats
// ─────────────────────────────────────────────────────────────────────────────
int MainWindow::effectiveMagSize(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0;
    const WeaponDef &d = WEAPONS[w];
    if (d.magSize <= 0) return 0;
    const Loadout &L = m_gs.player.load[w];
    float m = d.magSize * RARITY_MAG[L.rarity];
    if (L.att[AT_MAG] == 1) m *= 1.5f;
    return std::max(1, int(m + 0.5f));
}

int MainWindow::effectiveDamage(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0;
    const Loadout &L = m_gs.player.load[w];
    float d = WEAPONS[w].damage * RARITY_DMG[L.rarity];
    if (L.att[AT_AMMO] == 1) d *= 1.1f;
    if (L.att[AT_MUZZLE] == 1) d *= 0.95f;
    return std::max(1, int(d + 0.5f));
}

float MainWindow::effectiveRecoil(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 1.f;
    const Loadout &L = m_gs.player.load[w];
    float r = WEAPONS[w].recoil * RARITY_RECOIL[L.rarity];
    if (L.att[AT_MUZZLE] == 2) r *= 0.55f;
    if (L.att[AT_MUZZLE] == 1) r *= 0.9f;
    if (L.att[AT_UNDER] == 1) r *= 0.75f;
    if (L.att[AT_STOCK] == 1) r *= 0.88f;
    if (m_bipod) r *= 0.35f;
    return r;
}

float MainWindow::effectiveSpread(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0.02f;
    const Loadout &L = m_gs.player.load[w];
    float s = WEAPONS[w].spread;
    if (L.att[AT_MUZZLE] == 2) s *= 0.7f;
    if (L.att[AT_UNDER] == 2) s *= 0.6f;          // laser: tighter hip fire
    if (L.att[AT_UNDER] == 1) s *= 0.9f;
    if (m_bipod) s *= 0.4f;
    return s;
}

float MainWindow::effectiveFireDelay(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0.2f;
    const WeaponDef &d = WEAPONS[w];
    // Revolver fan-fire: hold the trigger from the hip to slap the hammer
    if (w == WPN_REVOLVER && m_lmb && m_adsT < 0.3f && m_fanFire) return 0.17f;
    return d.fireDelay;
}

bool MainWindow::effectiveScope(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return false;
    int o = m_gs.player.load[w].att[AT_OPTIC];
    if (o >= 3) return true;
    if (o >= 1) return false;
    return WEAPONS[w].scope;
}

float MainWindow::effectiveAdsFov(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 1.f;
    static const float OPTIC_FOV[6] = { 0.f, 0.72f, 0.68f, 0.5f, 0.25f, 0.125f };
    int o = m_gs.player.load[w].att[AT_OPTIC];
    return o > 0 ? OPTIC_FOV[o] : WEAPONS[w].adsFov;
}

float MainWindow::effectivePenetration(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return 0.f;
    return WEAPONS[w].penetration + (m_gs.player.load[w].att[AT_AMMO] == 1 ? 0.5f : 0.f);
}

QString MainWindow::weaponLabel(int w) const
{
    if (w < 0 || w >= WPN_COUNT) return QString();
    return QString(WEAPONS[w].name);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory
// ─────────────────────────────────────────────────────────────────────────────
int MainWindow::slotOf(int w) const
{
    for (int s = 0; s < SLOT_COUNT; s++) if (m_gs.player.slot[s] == w) return s;
    return -1;
}

// Where would weapon w go? A free primary slot first, else the one in hand
int MainWindow::targetSlotFor(int w) const
{
    const Player &pl = m_gs.player;
    InvSlot s = WEAPONS[w].slot;
    if (s != SLOT_PRIMARY) return s;
    if (pl.slot[SLOT_PRIMARY] < 0) return SLOT_PRIMARY;
    if (pl.slot[SLOT_PRIMARY2] < 0) return SLOT_PRIMARY2;
    int cur = slotOf(pl.curWeapon);
    return cur == SLOT_PRIMARY2 ? SLOT_PRIMARY2 : SLOT_PRIMARY;
}

// Put a weapon in the inventory. The gun it replaces (if any) is dropped.
void MainWindow::giveWeapon(int w, const Loadout &ld, int mag, int reserve, bool equip)
{
    Player &pl = m_gs.player;
    if (w < 0 || w >= WPN_COUNT) return;
    if (pl.owned[w]) {                                     // already carried: ammo, and keep the better copy
        pl.reserve[w] = std::min(WEAPONS[w].maxReserve, pl.reserve[w] + (reserve >= 0 ? reserve + std::max(0, mag) : WEAPONS[w].magSize * 2));
        if (ld.rarity > pl.load[w].rarity) pl.load[w].rarity = ld.rarity;
        for (int a = 0; a < AT_COUNT; a++) if (pl.load[w].att[a] == 0 && ld.att[a]) pl.load[w].att[a] = ld.att[a];
        return;
    }
    int s = targetSlotFor(w);
    int old = pl.slot[s];
    if (old >= 0 && old != w) dropWeapon(old);
    pl.slot[s] = w;
    pl.owned[w] = true;
    pl.load[w] = ld;
    pl.mag[w] = mag >= 0 ? std::min(mag, effectiveMagSize(w)) : effectiveMagSize(w);
    pl.reserve[w] = reserve >= 0 ? reserve : std::min(WEAPONS[w].maxReserve, WEAPONS[w].magSize * 2);
    if (equip || old == pl.curWeapon) { pl.curWeapon = -1; switchWeapon(w); if (pl.curWeapon < 0) pl.curWeapon = w; }
}

// Throw a carried weapon on the ground in front of the player (keeps its ammo)
void MainWindow::dropWeapon(int w)
{
    Player &pl = m_gs.player;
    if (w < 0 || w >= WPN_COUNT || !pl.owned[w]) return;
    WeaponPickup wp;
    wp.x = pl.posX + pl.dirX * 0.7f; wp.y = pl.posY + pl.dirY * 0.7f;
    if (!posValid(wp.x, wp.y, 0.1f)) { wp.x = pl.posX; wp.y = pl.posY; }
    wp.type = PICKUP_WEAPON; wp.weapon = w; wp.load = pl.load[w];
    wp.mag = pl.mag[w]; wp.reserve = pl.reserve[w];
    wp.bobPhase = 0.f;
    m_gs.weaponPickups.push_back(wp);
    m_pickupCooldown = 1.2f;                              // don't instantly pick it back up
    pl.owned[w] = false; pl.mag[w] = 0; pl.reserve[w] = 0;
    for (int s = 0; s < SLOT_COUNT; s++) if (pl.slot[s] == w) pl.slot[s] = -1;
}

void MainWindow::selectSlot(int s)
{
    if (s < 0 || s >= SLOT_COUNT) return;
    int w = m_gs.player.slot[s];
    if (w >= 0) switchWeapon(w);
}

void MainWindow::switchWeapon(int w)
{
    Player &pl = m_gs.player;
    if (w < 0 || w >= WPN_COUNT || !pl.owned[w] || (w == pl.curWeapon && m_pendingWeapon < 0)) return;
    pl.reloadTimer = 0; m_shellReload = false;            // swapping cancels a reload
    pl.switchTimer = 0.4f;
    pl.burstLeft = 0; pl.charge = 0.f; m_inspectT = 0.f;
    m_pendingWeapon = w;
    sfx(SND_SWITCH, 0.6f, frandC(0.95f, 1.05f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Reload: whole magazine, or one shell at a time (cancel by firing)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::startReload()
{
    Player &pl = m_gs.player;
    int w = pl.curWeapon;
    if (w < 0 || !usesAmmo(w)) return;
    int maxMag = effectiveMagSize(w);
    if (pl.reloadTimer > 0 || pl.switchTimer > 0 || pl.mag[w] >= maxMag || pl.reserve[w] <= 0) return;
    const WeaponDef &d = WEAPONS[w];
    m_inspectT = 0.f; pl.burstLeft = 0;
    if (d.reload == RM_SHELL) {
        m_shellReload = true;
        float t = d.reloadTime + 0.25f;                   // first shell includes bringing the gun up
        pl.reloadTimer = t; m_reloadTotal = t;
        sfx(SND_SHELL_IN, 0.6f, frandC(0.95f, 1.05f), t * 0.7f);
    } else {
        float t = d.reloadTime * (pl.load[w].att[AT_MAG] == 1 ? 1.1f : 1.f) * reloadMul();
        pl.reloadTimer = t; m_reloadTotal = t;
        sfx(SND_MAG_OUT, 0.7f, 1.f, t * 0.15f);
        sfx(SND_MAG_IN, 0.75f, 1.f, t * 0.6f);
        sfx(SND_BOLT, 0.7f, d.cls == WC_PISTOL ? 1.25f : 1.f, t * 0.82f);
    }
    pl.adsMode = false;
}

void MainWindow::updateReload(float dt)
{
    Player &pl = m_gs.player;
    if (pl.reloadTimer <= 0) return;
    if (pl.sprinting) { pl.reloadTimer = 0; m_shellReload = false; return; }   // sprinting cancels
    pl.reloadTimer -= dt;
    if (pl.reloadTimer > 0) return;
    int w = pl.curWeapon;
    if (w < 0) { pl.reloadTimer = 0; return; }
    int maxMag = effectiveMagSize(w);
    if (m_shellReload) {
        if (pl.reserve[w] > 0 && pl.mag[w] < maxMag) { pl.mag[w]++; pl.reserve[w]--; }
        if (pl.mag[w] < maxMag && pl.reserve[w] > 0) {
            float t = WEAPONS[w].reloadTime;
            pl.reloadTimer = t; m_reloadTotal = t;
            sfx(SND_SHELL_IN, 0.6f, frandC(0.95f, 1.05f), t * 0.7f);
        } else {
            pl.reloadTimer = 0; m_shellReload = false;
            if (w == WPN_SHOTGUN) sfx(SND_PUMP, 0.6f);
        }
        return;
    }
    int take = std::min(maxMag - pl.mag[w], pl.reserve[w]);
    pl.mag[w] += take; pl.reserve[w] -= take; pl.reloadTimer = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Trigger: called on press and every frame while held
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::tryFire()
{
    Player &pl = m_gs.player;
    int w = pl.curWeapon;
    if (w < 0) return;
    const WeaponDef &d = WEAPONS[w];
    // shell-by-shell reload: firing with a round loaded cancels it
    if (pl.reloadTimer > 0 && m_shellReload && pl.mag[w] > 0) { pl.reloadTimer = 0; m_shellReload = false; }
    if (pl.reloadTimer > 0 || pl.switchTimer > 0 || pl.attackCooldown > 0 || pl.sprinting || m_sprintFireT > 0.f) return;
    if (pl.burstLeft > 0 || pl.overheatT > 0.f) return;
    m_inspectT = 0.f;
    if (d.mode == FM_MELEE) { meleeAttack(w, false); return; }
    if (usesAmmo(w) && pl.mag[w] <= 0) {
        if (pl.reserve[w] > 0) startReload();
        else { pl.attackCooldown = 0.3f; sfx(SND_DRY_FIRE, 0.7f); pushFeed("OUT OF AMMO - PRESS 1 FOR MELEE", 1.5f); }
        return;
    }
    if (d.mode == FM_CHARGE) {                             // charge starts; release (or full) fires
        if (!m_charging) { m_charging = true; pl.charge = 0.f; sfx(w == WPN_RAILGUN ? SND_RAIL_CHARGE : SND_PLASMA_CHARGE, 0.55f); }
        return;
    }
    if (d.mode == FM_BURST) { pl.burstLeft = d.burst - 1; pl.burstT = 0.065f; }
    fireOnce();
    if (d.mode == FM_BURST) pl.attackCooldown = d.fireDelay + 0.065f * (d.burst - 1);
}

void MainWindow::releaseTrigger()
{
    Player &pl = m_gs.player;
    if (m_charging && pl.curWeapon >= 0 && WEAPONS[pl.curWeapon].mode == FM_CHARGE) {
        // plasma fires on release at any charge; the railgun needs at least half
        if (pl.curWeapon == WPN_PLASMA || pl.charge >= 0.5f) fireOnce();
        m_charging = false; pl.charge = 0.f;
    }
    m_fanFire = false;
}

void MainWindow::updateTrigger(float dt)
{
    Player &pl = m_gs.player;
    int w = pl.curWeapon;
    if (w < 0) return;
    const WeaponDef &d = WEAPONS[w];
    // heat bleeds off; an overheated gun is locked until it vents
    if (pl.overheatT > 0.f) {
        pl.overheatT -= dt; pl.heat = std::max(0.f, pl.heat - dt * 0.5f);
        if (int(m_time * 20) % 3 == 0) {
            Particle p; p.type = PT_SMOKE; p.x = pl.posX + pl.dirX * 0.4f; p.y = pl.posY + pl.dirY * 0.4f; p.z = pl.footZ + 0.45f;
            p.vx = frandC(-0.2f, 0.2f); p.vy = frandC(-0.2f, 0.2f); p.vz = 0.6f; p.life = p.maxLife = 0.8f; p.size = 0.05f; p.color = rgb(220, 225, 235);
            m_gs.particles.push_back(p);
        }
        if (pl.overheatT <= 0.f) { pl.overheatT = 0.f; pushFeed("WEAPON COOLED", 1.2f); }
    } else if (m_time - m_lastShotT > 0.3f) pl.heat = std::max(0.f, pl.heat - dt * 0.45f);
    m_bloom = std::max(0.f, m_bloom - dt * 3.f);
    if (m_time - m_lastShotT > 0.35f) m_sprayIndex = 0;
    m_sprintFireT = pl.sprinting ? 0.18f : std::max(0.f, m_sprintFireT - dt);

    // burst in flight
    if (pl.burstLeft > 0) {
        pl.burstT -= dt;
        if (pl.burstT <= 0.f) {
            if (pl.mag[w] > 0) { fireOnce(); pl.burstLeft--; pl.burstT = 0.065f; }
            else pl.burstLeft = 0;
        }
    }
    // charge weapons
    if (m_charging) {
        if (!m_lmb || pl.switchTimer > 0) { if (!m_lmb) releaseTrigger(); else { m_charging = false; pl.charge = 0.f; } }
        else {
            pl.charge = std::min(1.f, pl.charge + dt / std::max(0.1f, d.chargeTime));
            if (w == WPN_RAILGUN && pl.charge >= 1.f) { fireOnce(); m_charging = false; pl.charge = 0.f; pl.attackCooldown = d.fireDelay; }
        }
    }
    // held trigger: automatics, beams and revolver fan-fire
    if (m_lmb && !m_charging) {
        bool held = d.automatic || d.mode == FM_BEAM;
        if (w == WPN_REVOLVER && m_adsT < 0.3f) { m_fanFire = true; held = true; }
        if (held && d.mode != FM_MELEE) tryFire();
        else if (held && d.mode == FM_MELEE && pl.attackCooldown <= 0) tryFire();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  One shot of the weapon in hand
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::applyRecoil(int w)
{
    const WeaponDef &d = WEAPONS[w];
    float rec = effectiveRecoil(w);
    float up = 1.f, side = 0.f;
    int i = m_sprayIndex++;
    switch (d.pattern) {
    case RP_VERTICAL: up = 1.f; side = frandC(-0.15f, 0.15f); break;
    case RP_SCURVE:   up = 0.8f; side = std::sin(i * 0.55f) * 0.6f; break;
    case RP_RIGHT:    up = 1.f; side = 0.25f + frandC(-0.1f, 0.1f); break;
    case RP_HEAVY:    up = 1.6f; side = frandC(-0.3f, 0.3f); break;
    case RP_RANDOM:   up = 0.6f; side = frandC(-0.6f, 0.6f); break;
    }
    if (m_fanFire) { up *= 1.3f; side *= 2.f; }
    m_kick = std::min(1.8f, m_kick + 0.55f + 0.25f * rec);
    m_camKick += 2.5f * rec * (m_bipod ? 0.4f : 1.f);
    float climb = 0.012f * rec * up * (m_adsT > 0.5f ? 0.6f : 1.f);
    m_pitch = clampf(m_pitch + climb, -0.75f, 0.75f);
    m_recoilPitch += climb * 0.85f;
    turn(side * 0.006f * rec);
    m_bloom = std::min(2.f, m_bloom + 0.25f * rec);
}

// World-space origin of a shot: the gun muzzle (third person) or just in front of the eye
void MainWindow::shotOrigin(float &sX, float &sY, float &sZ) const
{
    const Player &pl = m_gs.player;
    const float startD = 0.6f;
    float mcx = 2.f * m_muzzleX / W - 1.f;
    sX = pl.posX + (pl.dirX + pl.planeX * m_adsFOVMult * mcx) * startD;
    sY = pl.posY + (pl.dirY + pl.planeY * m_adsFOVMult * mcx) * startD;
    sZ = m_eye + (m_hor - m_muzzleY) / m_K * startD;
    if (effectiveScope(pl.curWeapon) && m_adsT > 0.85f) { sX = pl.posX + pl.dirX * startD; sY = pl.posY + pl.dirY * startD; sZ = m_eye - 0.08f; }
    if (thirdPerson()) playerMuzzle(sX, sY, sZ);
}

// Unit 3D direction of the ray through screen point (ax, ay)
void MainWindow::aimDirection(float ax, float ay, float &dx, float &dy, float &dz) const
{
    const Player &pl = m_gs.player;
    float camX = 2.f * ax / W - 1.f;
    float rx = pl.dirX + pl.planeX * m_adsFOVMult * camX, ry = pl.dirY + pl.planeY * m_adsFOVMult * camX;
    float slope = (m_hor - ay) / m_K;                     // z rises by slope per unit of view depth
    float l = std::sqrt(rx * rx + ry * ry + slope * slope);
    dx = rx / l; dy = ry / l; dz = slope / l;
}

void MainWindow::fireOnce()
{
    Player &pl = m_gs.player;
    int w = pl.curWeapon;
    const WeaponDef &d = WEAPONS[w];
    const Loadout &L = pl.load[w];
    if (usesAmmo(w)) { if (pl.mag[w] <= 0) return; pl.mag[w]--; }
    float chargeK = pl.charge;
    if (d.heatPerShot > 0.f) {
        pl.heat += d.heatPerShot + 0.3f * chargeK;
        if (pl.heat >= 1.f) { pl.heat = 1.f; pl.overheatT = 2.2f; sfx(SND_OVERHEAT, 0.7f); pushFeed("OVERHEATED", 1.5f); }
    }
    pl.attackCooldown = effectiveFireDelay(w);
    m_lastShotT = m_time;
    m_inspectT = 0.f;
    // report: suppressed guns are quiet and don't alert distant hostiles
    bool suppressed = L.att[AT_MUZZLE] == 1;
    SoundId snd = suppressed ? SND_SUPPRESSED : SoundId(d.sound);
    sfx(snd, suppressed ? 0.9f : 1.f, frandC(0.95f, 1.05f));
    if (!suppressed) m_noiseT = 0.6f;
    if (d.mode == FM_BOLT) sfx(SND_BOLT, 0.6f, 0.9f, 0.45f);
    if (w == WPN_SHOTGUN) { sfx(SND_PUMP, 0.7f, 1.f, 0.12f); sfx(SND_SHELL_DROP, 0.45f, frandC(0.9f, 1.1f), 0.7f); }
    else if (d.proj == PJ_HITSCAN || d.proj == PJ_BULLET) sfx(SND_CASING, 0.4f, frandC(0.85f, 1.2f), frandC(0.35f, 0.5f));
    if (d.proj != PJ_FLAME && d.proj != PJ_ARC) {
        pl.flashTimer = d.cls == WC_SHOTGUN ? 0.09f : suppressed ? 0.02f : 0.06f;
        m_flashLight = suppressed ? 0.3f : 1.f; m_flashSeed = rand();
    }
    m_actionT = 1.f;
    m_gs.shotsFired++; m_gs.zoneShots++;

    computeView();
    float ax, ay; aimPoint(ax, ay);
    bool moving = m_keys.count(Qt::Key_W) || m_keys.count(Qt::Key_S) || m_keys.count(Qt::Key_A) || m_keys.count(Qt::Key_D);
    float baseSpread = effectiveSpread(w);
    float spread = baseSpread * (moving ? 1.8f : 1.f) * (inAir() ? 2.5f : 1.f) * (1.f + m_bloom * 0.5f);
    if (m_fanFire) spread *= 3.f;
    spread *= 1.f - 0.8f * m_adsT;
    if (d.mode == FM_BOLT && m_adsT > 0.85f) spread = 0.f;                // scoped bolt-action: dead on
    if (d.pellets > 1) spread = std::max(spread, baseSpread * 0.6f);
    float spreadPx = spread * W;
    float sX, sY, sZ; shotOrigin(sX, sY, sZ);

    switch (d.proj) {
    case PJ_HITSCAN: fireHitscan(w, ax, ay, spreadPx, sX, sY, sZ); break;
    case PJ_BULLET: case PJ_ROCKET: case PJ_GRENADE: case PJ_PLASMA: {
        float r = spreadPx * std::sqrt(frandC()), a = frandC(0, 6.2831853f);
        float dx, dy, dz; aimDirection(ax + std::cos(a) * r, ay + std::sin(a) * r, dx, dy, dz);
        // aim the projectile at what the crosshair is on (so the muzzle offset and third
        // person still converge), keeping the spread as an offset from that line
        HitResult h = hitscan(ax, ay);
        if (!h.sky) {
            float tx = h.x - sX, ty = h.y - sY, tz = h.z - sZ, tl = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (tl > 0.5f) {
                float cx, cy, cz; aimDirection(ax, ay, cx, cy, cz);
                dx = tx / tl + (dx - cx); dy = ty / tl + (dy - cy); dz = tz / tl + (dz - cz);
                float l = std::sqrt(dx * dx + dy * dy + dz * dz); dx /= l; dy /= l; dz /= l;
            }
        }
        Projectile p; p.x = sX; p.y = sY; p.z = sZ;
        p.kind = d.proj; p.owner = PO_PLAYER; p.weapon = w;
        float spd = d.projSpeed;
        if (d.proj == PJ_GRENADE) { dz += 0.12f; float l = std::sqrt(dx * dx + dy * dy + dz * dz); dx /= l; dy /= l; dz /= l; }
        p.vx = dx * spd; p.vy = dy * spd; p.vz = dz * spd;
        p.damage = float(effectiveDamage(w)) * (d.proj == PJ_PLASMA ? 1.f + 2.5f * chargeK : 1.f);
        p.splashR = d.splashR * (d.proj == PJ_PLASMA ? 1.f + chargeK : 1.f);
        p.splashDmg = d.splashDmg * (d.proj == PJ_PLASMA ? 1.f + 2.f * chargeK : 1.f) * RARITY_DMG[L.rarity];
        p.gravity = d.proj == PJ_BULLET ? 3.5f : d.proj == PJ_ROCKET ? 1.0f : d.proj == PJ_GRENADE ? 9.f : 0.f;
        p.fuse = d.proj == PJ_GRENADE ? 1.6f : 0.f;
        p.life = d.proj == PJ_BULLET ? 2.0f : 6.f;
        p.incendiary = d.status == ST_BURN || L.att[AT_AMMO] == 2;
        m_gs.projectiles.push_back(p);
        if (d.proj == PJ_BULLET) {
            // a long tracer helps read the bullet drop
            Tracer t; t.x0 = sX; t.y0 = sY; t.z0 = sZ; t.x1 = sX + dx * 60.f; t.y1 = sY + dy * 60.f; t.z1 = sZ + dz * 60.f - 0.5f * 3.5f * 0.18f;
            t.len = 60.f; t.t = 0.f; m_gs.tracers.push_back(t);
        }
        break; }
    case PJ_ARC:   fireArc(w, ax, ay, sX, sY, sZ); break;
    case PJ_RAIL:  fireRail(w, ax, ay, sX, sY, sZ, std::max(0.5f, chargeK)); break;
    case PJ_FLAME: fireFlame(w, sX, sY, sZ); break;
    }

    // the kick lands after the round has left: it moves the next shot, not this one
    applyRecoil(w);

    // brass / shells
    if (d.proj == PJ_HITSCAN || d.proj == PJ_BULLET) {
        Casing c; c.x = m_ejectX; c.y = m_ejectY; c.vx = frandC(260, 380); c.vy = frandC(-420, -300);
        c.a = frandC(0, 6.28f); c.va = frandC(14, 24); c.life = 1.0f; c.shell = d.cls == WC_SHOTGUN;
        if (!(effectiveScope(w) && m_adsT > 0.85f) && !thirdPerson()) m_casings.push_back(c);
    }
    if (usesAmmo(w) && pl.mag[w] == 0 && pl.reserve[w] > 0 && pl.burstLeft == 0) startReload();
}

// Damage multiplier for distance (falloff start / end / min from the weapon row)
static float falloffFor(const WeaponDef &d, float dist)
{
    if (dist <= d.falloffStart) return 1.f;
    if (dist >= d.falloffEnd) return d.falloffMin;
    float t = (dist - d.falloffStart) / std::max(0.01f, d.falloffEnd - d.falloffStart);
    return 1.f + (d.falloffMin - 1.f) * t;
}

void MainWindow::fireHitscan(int w, float ax, float ay, float spreadPx, float sX, float sY, float sZ)
{
    Player &pl = m_gs.player;
    const WeaponDef &d = WEAPONS[w];
    const Loadout &L = pl.load[w];
    float pen = effectivePenetration(w);
    bool anyHit = false;
    for (int p = 0; p < d.pellets; p++) {
        float r = spreadPx * std::sqrt(frandC()), a = frandC(0, 6.2831853f);
        float px = ax + std::cos(a) * r, py = ay + std::sin(a) * r;
        float dmgScale = 1.f;
        float startD = 0.f;
        // a penetrating round continues through doors / thin walls with reduced damage
        for (int pass = 0; pass < 2; pass++) {
            HitResult h = hitscan(px, py, startD);
            if (thirdPerson() && pass == 0) muzzleObstruction(h, sX, sY, sZ);
            float ex = h.x, ey = h.y, ez = h.z;
            if (h.sky) { ex = pl.posX + (ex - pl.posX) * 0.5f; ey = pl.posY + (ey - pl.posY) * 0.5f; ez = sZ + (ez - sZ) * 0.5f; }
            if (pass == 0 && p < 3) {
                Tracer t; t.x0 = sX; t.y0 = sY; t.z0 = sZ; t.x1 = ex; t.y1 = ey; t.z1 = ez;
                t.len = std::sqrt((ex - sX) * (ex - sX) + (ey - sY) * (ey - sY) + (ez - sZ) * (ez - sZ)); t.t = 0.f;
                m_gs.tracers.push_back(t);
            }
            if (h.enemy >= 0 && h.part == PART_SHIELD && pen < 0.5f) {
                spawnImpact(h.x, h.y, h.z, false);
                if (p < 2) sfxAt(SND_RICOCHET, h.x, h.y, 0.7f, frandC(0.9f, 1.2f));
                break;
            }
            if (h.enemy >= 0) {
                anyHit = true;
                float mult = h.part == PART_HEAD ? d.headMult : h.part == PART_LIMB ? d.limbMult : h.part == PART_SHIELD ? 0.4f : 1.f;
                if (L.att[AT_AMMO] == 3) mult *= h.part == PART_HEAD ? 0.9f : 1.25f;    // hollow-point
                float dist = std::sqrt((h.x - pl.posX) * (h.x - pl.posX) + (h.y - pl.posY) * (h.y - pl.posY));
                int dmg = std::max(1, int(effectiveDamage(w) * mult * falloffFor(d, dist) * dmgScale * frandC(0.9f, 1.1f) + 0.5f));
                damageEnemy(h.enemy, dmg, h.part, h.x, h.y, h.z);
                if (L.att[AT_AMMO] == 2) igniteEnemy(h.enemy, 2.5f);
                break;
            }
            if (h.actor >= 0 && m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
                anyHit = true;
                const Actor &act = m_gs.actors[h.actor];
                int myTeam = 0;
                for (const auto &s : m_net->playerSlots()) if (s.id == m_net->localPlayerId()) myTeam = s.team;
                bool isHostile = (act.team != myTeam);
                if (isHostile) {
                    float mult = h.part == PART_HEAD ? d.headMult : 1.f;
                    if (L.att[AT_AMMO] == 3) mult *= h.part == PART_HEAD ? 0.9f : 1.25f;
                    float dist = std::sqrt((h.x - pl.posX) * (h.x - pl.posX) + (h.y - pl.posY) * (h.y - pl.posY));
                    int dmg = std::max(1, int(effectiveDamage(w) * mult * falloffFor(d, dist) * dmgScale * frandC(0.9f, 1.1f) + 0.5f));
                    damageActor(h.actor, dmg, pl.posX, pl.posY);
                    if (h.part == PART_HEAD) { m_headMarker = 0.22f; m_headshotBanner = 1.4f; sfx(SND_HEADSHOT, 0.9f); }
                    else { m_hitMarker = 0.18f; sfx(SND_HITMARK, 0.7f); }
                    QVariantMap dev;
                    dev["target"] = act.id;
                    dev["dmg"] = dmg;
                    dev["head"] = (h.part == PART_HEAD);
                    dev["shooter"] = m_net->localPlayerId();
                    m_net->broadcastEvent(EV_DAMAGE, dev);
                }
                break;
            }

            if (h.sky) break;
            float bx = h.x - (h.x - m_camX) / std::max(0.1f, h.depth) * 0.04f;
            float by = h.y - (h.y - m_camY) / std::max(0.1f, h.depth) * 0.04f;
            spawnImpact(bx, by, h.z, h.floor);
            if (p < 2) sfxAt(h.wall && rand() % 4 == 0 ? SND_RICOCHET : SND_IMPACT, h.x, h.y, 0.7f, frandC(0.85f, 1.2f));
            if (h.floor) addDecal(m_scorch, h.x, h.y, 0.03f, 160);
            if (h.wall) stampWallDecal(h.cx, h.cy, h.face, h.u, h.v, d.pellets > 1);
            // through a door or secret wall (one cell thick) if the round has the punch
            bool thin = h.wall && inside(h.cx, h.cy) && (m_map[h.cy][h.cx] == CELL_DOOR || m_map[h.cy][h.cx] == CELL_SECRET);
            if (!(h.wall && pen >= 0.25f && (thin || pen >= 0.75f))) break;
            dmgScale *= thin ? 0.6f : 0.45f;
            startD = h.depth + 1.05f;
        }
    }
    if (anyHit) { m_gs.shotsHit++; m_gs.zoneHits++; }
}

// Chain lightning: the target under the crosshair (or the nearest one in a
// narrow cone) takes the bolt, then it jumps to up to three neighbours
void MainWindow::fireArc(int w, float ax, float ay, float sX, float sY, float sZ)
{
    Player &pl = m_gs.player;
    const WeaponDef &d = WEAPONS[w];
    HitResult h = hitscan(ax, ay);
    int first = (h.enemy >= 0 && h.depth <= d.reach + m_boomCur) ? h.enemy : -1;
    float dx, dy, dz; aimDirection(ax, ay, dx, dy, dz);
    if (first < 0) {
        float best = 0.97f;
        for (int i = 0; i < int(m_gs.enemies.size()); i++) {
            const Enemy &e = m_gs.enemies[size_t(i)];
            if (!e.alive) continue;
            float ex = e.x - pl.posX, ey = e.y - pl.posY, el = std::sqrt(ex * ex + ey * ey);
            if (el > d.reach || el < 0.1f) continue;
            float c = (ex * dx + ey * dy) / (el * std::sqrt(dx * dx + dy * dy) + 1e-6f);
            if (c > best && hasLOS(sX, sY, sZ, e.x, e.y, floorH(e.x, e.y) + 0.6f)) { best = c; first = i; }
        }
    }
    float px = sX, py = sY, pz = sZ;
    if (first < 0) {                                        // a crackle into the air
        float ex = sX + dx * d.reach * 0.6f, ey = sY + dy * d.reach * 0.6f, ez = sZ + dz * d.reach * 0.6f;
        m_gs.beams.push_back(Beam{px, py, pz, ex, ey, ez, 0.12f, 0.12f, rgb(150, 210, 255), 0});
        return;
    }
    std::vector<int> hit;
    int cur = first;
    float dmg = float(effectiveDamage(w));
    for (int jump = 0; jump < 4 && cur >= 0; jump++) {
        Enemy &e = m_gs.enemies[size_t(cur)];
        float ez = floorH(e.x, e.y) + 0.55f * e.scale;
        m_gs.beams.push_back(Beam{px, py, pz, e.x, e.y, ez, 0.14f, 0.14f, rgb(150, 210, 255), 0});
        hit.push_back(cur);
        e.shockT = std::max(e.shockT, 1.5f);
        damageEnemy(cur, int(dmg), PART_BODY, e.x, e.y, ez);
        if (jump == 0) { m_gs.shotsHit++; m_gs.zoneHits++; }
        px = e.x; py = e.y; pz = ez;
        dmg *= 0.7f;
        int next = -1; float bestD = 3.5f;
        for (int i = 0; i < int(m_gs.enemies.size()); i++) {
            const Enemy &o = m_gs.enemies[size_t(i)];
            if (!o.alive || std::find(hit.begin(), hit.end(), i) != hit.end()) continue;
            float dd = std::sqrt((o.x - px) * (o.x - px) + (o.y - py) * (o.y - py));
            if (dd < bestD && hasLOS(px, py, pz, o.x, o.y, floorH(o.x, o.y) + 0.55f)) { bestD = dd; next = i; }
        }
        cur = next;
    }
}

// Railgun: passes through every hostile and every wall along the line
void MainWindow::fireRail(int w, float ax, float ay, float sX, float sY, float sZ, float charge)
{
    Player &pl = m_gs.player;
    float dx, dy, dz; aimDirection(ax, ay, dx, dy, dz);
    float len = 60.f;
    // enemies whose body the line passes within reach of
    int hits = 0;
    float dmg = effectiveDamage(w) * charge;
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive) continue;
        float base = floorH(e.x, e.y) + e.hover;
        // closest approach between the ray and the enemy's vertical axis
        float ex = e.x - sX, ey = e.y - sY;
        float hl2 = dx * dx + dy * dy;
        float t = (ex * dx + ey * dy) / std::max(1e-6f, hl2);
        if (t < 0.2f || t > len) continue;
        float cx = sX + dx * t - e.x, cy = sY + dy * t - e.y;
        if (cx * cx + cy * cy > 0.3f * 0.3f * e.scale * e.scale) continue;
        float z = sZ + dz * t - base;
        if (z < 0.f || z > 0.95f * e.scale) continue;
        HitPart part = z > 0.74f * e.scale ? PART_HEAD : z < 0.35f * e.scale ? PART_LIMB : PART_BODY;
        float mult = part == PART_HEAD ? WEAPONS[w].headMult : part == PART_LIMB ? WEAPONS[w].limbMult : 1.f;
        damageEnemy(i, int(dmg * mult), part, e.x, e.y, base + z);
        hits++;
    }
    if (hits) { m_gs.shotsHit++; m_gs.zoneHits++; }
    // scorch every wall face the slug goes through
    float px = sX, py = sY, pz = sZ;
    for (float t = 0.5f; t < len; t += 0.25f) {
        float x = sX + dx * t, y = sY + dy * t, z = sZ + dz * t;
        int cx = int(std::floor(x)), cy = int(std::floor(y));
        if (!inside(cx, cy)) { len = t; break; }
        if (z < groundAt(x, y)) {
            int ox = int(std::floor(px)), oy = int(std::floor(py));
            if (!solidCell(ox, oy) && solidCell(cx, cy)) {
                spawnImpact(px, py, pz, false);
                int face = cx != ox ? (cx > ox ? 0 : 1) : (cy > oy ? 2 : 3);
                float u = cx != ox ? y - std::floor(y) : x - std::floor(x);
                stampWallDecal(cx, cy, face, u, cellTop(cx, cy) - z, false);
            }
            if (!solidCell(cx, cy)) { spawnImpact(x, y, z, true); addDecal(m_scorch, x, y, 0.12f, 220); len = t; break; }
        }
        px = x; py = y; pz = z;
    }
    m_gs.beams.push_back(Beam{sX, sY, sZ, sX + dx * len, sY + dy * len, sZ + dz * len, 0.6f, 0.6f, rgb(110, 230, 255), 1});
    pl.shakeY -= 4.f;
}

// Flamethrower: a cone of fire, damage + burning on every hostile inside it
void MainWindow::fireFlame(int w, float sX, float sY, float sZ)
{
    Player &pl = m_gs.player;
    const WeaponDef &d = WEAPONS[w];
    float ax, ay; aimPoint(ax, ay);
    float dx, dy, dz; aimDirection(ax, ay, dx, dy, dz);
    float hl = std::sqrt(dx * dx + dy * dy) + 1e-6f;
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive) continue;
        float ex = e.x - pl.posX, ey = e.y - pl.posY, el = std::sqrt(ex * ex + ey * ey);
        if (el > d.reach || el < 0.05f) continue;
        float c = (ex * dx + ey * dy) / (el * hl);
        if (c < std::cos(0.32f)) continue;
        if (!hasLOS(sX, sY, sZ, e.x, e.y, floorH(e.x, e.y) + 0.5f)) continue;
        damageEnemy(i, effectiveDamage(w), PART_BODY, e.x, e.y, floorH(e.x, e.y) + 0.5f);
        igniteEnemy(i, 3.f);
    }
    // the flame itself: fast additive particles that slow and rise
    for (int k = 0; k < 4; k++) {
        Particle p; p.type = PT_FLAME; p.x = sX; p.y = sY; p.z = sZ;
        float spd = d.projSpeed * frandC(0.75f, 1.1f);
        p.vx = (dx + frandC(-0.12f, 0.12f)) * spd; p.vy = (dy + frandC(-0.12f, 0.12f)) * spd; p.vz = (dz + frandC(-0.05f, 0.1f)) * spd;
        p.life = p.maxLife = frandC(0.45f, 0.7f); p.size = 0.05f;
        p.color = rgb(255, 150 + rand() % 60, 40);
        m_gs.particles.push_back(p);
    }
    // where the stream lands, the ground keeps burning for a moment
    if (frandC() < 0.12f) {
        HitResult h = hitscan(ax, ay);
        float hd = std::sqrt((h.x - pl.posX) * (h.x - pl.posX) + (h.y - pl.posY) * (h.y - pl.posY));
        if (!h.sky && hd < d.reach && h.floor) m_gs.fires.push_back(FireZone{h.x, h.y, 0.7f, 2.5f});
    }
    if (int(m_time * 10) != int((m_time - m_dt) * 10)) sfx(SND_FLAME, 0.4f, frandC(0.9f, 1.1f));
}

void MainWindow::igniteEnemy(int i, float sec)
{
    Enemy &e = m_gs.enemies[size_t(i)];
    if (!e.alive) return;
    e.burnT = std::max(e.burnT, sec);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Melee: knife stab/slash, machete arc, baton stun, axe (chops doors)
//  Hitting an unaware hostile is a silent takedown.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::meleeAttack(int w, bool quick)
{
    Player &pl = m_gs.player;
    const WeaponDef &d = WEAPONS[w];
    pl.attackCooldown = quick ? 0.45f : effectiveFireDelay(w);
    m_actionT = 1.0f;
    m_knifeSwing = 1 - m_knifeSwing;
    if (quick) m_quickMeleeT = 0.45f;
    sfx(w == WPN_AXE ? SND_MELEE_HEAVY : SND_KNIFE_SWING, 0.8f, frandC(0.9f, 1.15f));
    m_camKick += (m_knifeSwing ? 1.0f : -0.8f) * (w == WPN_AXE ? 1.8f : 1.f);
    m_kick = std::min(1.2f, m_kick + 0.35f);
    m_gs.shotsFired++; m_gs.zoneShots++;

    computeView();
    float ax, ay; aimPoint(ax, ay);
    float dx, dy, dz; aimDirection(ax, ay, dx, dy, dz);
    float hl = std::sqrt(dx * dx + dy * dy) + 1e-6f;
    float reach = d.reach;
    float arcCos = w == WPN_MACHETE ? std::cos(0.6f) : w == WPN_AXE ? std::cos(0.35f) : std::cos(0.28f);
    int maxTargets = w == WPN_MACHETE ? 3 : 1;
    // targets: hostiles in front, closest first
    std::vector<std::pair<float, int>> cand;
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive) continue;
        float ex = e.x - pl.posX, ey = e.y - pl.posY, el = std::sqrt(ex * ex + ey * ey);
        if (el > reach * 0.8f || el < 0.05f) continue;
        float c = (ex * dx + ey * dy) / (el * hl);
        if (c < arcCos) continue;
        if (std::fabs(floorH(e.x, e.y) - pl.footZ) > 0.8f) continue;
        cand.push_back({el, i});
    }
    std::sort(cand.begin(), cand.end());
    int hits = 0;
    for (auto &c : cand) {
        if (hits >= maxTargets) break;
        Enemy &e = m_gs.enemies[size_t(c.second)];
        float hz = floorH(e.x, e.y) + 0.6f * e.scale;
        bool unaware = e.state == ES_IDLE && !e.boss;
        HitPart part = PART_BODY;
        HitResult h = hitscan(ax, ay);
        if (h.enemy == c.second) part = h.part;
        float mult = part == PART_HEAD ? d.headMult : part == PART_LIMB ? d.limbMult : 1.f;
        int dmg = int(effectiveDamage(quick ? pl.slot[SLOT_MELEE] : w) * mult * frandC(0.95f, 1.15f));
        if (unaware) {                                     // silent takedown
            dmg = e.hp + 1;
            sfx(SND_TAKEDOWN, 0.9f);
            m_gs.score += 200 * m_gs.level;
            pushFeed("SILENT TAKEDOWN  +" + QString::number(200 * m_gs.level), 3.f, true);
        } else sfx(w == WPN_BATON ? SND_BATON_HIT : SND_KNIFE_HIT, 0.9f, frandC(0.9f, 1.1f), 0.05f);
        damageEnemy(c.second, dmg, part, e.x, e.y, hz);
        if (c.second < int(m_gs.enemies.size())) e.hitFlash = 0.35f;
        if (w == WPN_BATON) { e.stunT = std::max(e.stunT, 2.2f); e.shockT = std::max(e.shockT, 2.2f); }
        spawnBlood(e.x, e.y, hz, part == PART_HEAD ? 24 : 14, 1.5f);
        hits++;
    }
    if (hits) { m_gs.shotsHit++; m_gs.zoneHits++; m_hitMarker = 0.3f; return; }
    // the axe chops through a door in front
    if (w == WPN_AXE) {
        for (float t = 0.5f; t <= 1.6f; t += 0.3f) {
            int cx = int(pl.posX + pl.dirX * t), cy = int(pl.posY + pl.dirY * t);
            if (!inside(cx, cy)) break;
            if (m_map[cy][cx] == CELL_DOOR && !m_doorMoving[cy][cx]) {
                m_doorMoving[cy][cx] = true; m_doorT[cy][cx] = std::max(m_doorT[cy][cx], 0.6f);
                sfxAt(SND_DOOR, cx + 0.5f, cy + 0.5f, 0.9f);
                for (int k = 0; k < 10; k++) spawnImpact(cx + 0.5f - pl.dirX * 0.5f, cy + 0.5f - pl.dirY * 0.5f, pl.footZ + frandC(0.2f, 0.9f), false);
                pushFeed("DOOR CHOPPED", 2.f);
                return;
            }
            if (solidCell(cx, cy)) break;
        }
    }
    HitResult h = hitscan(ax, ay);
    float rd = std::sqrt((h.x - pl.posX) * (h.x - pl.posX) + (h.y - pl.posY) * (h.y - pl.posY));
    if (!h.sky && rd <= reach * 0.9f) {
        spawnImpact(h.x, h.y, h.z, h.floor);
        sfx(SND_IMPACT, 0.6f, frandC(0.9f, 1.1f), 0.05f);
    }
}

void MainWindow::quickMelee()
{
    Player &pl = m_gs.player;
    int mw = pl.slot[SLOT_MELEE];
    if (mw < 0 || pl.attackCooldown > 0 || pl.switchTimer > 0 || isMelee(pl.curWeapon)) return;
    pl.reloadTimer = 0; m_shellReload = false;
    meleeAttack(mw, true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Projectiles: ballistic bullets, rockets, grenades, plasma, thrown items
// ─────────────────────────────────────────────────────────────────────────────
// Enemy whose body (a vertical cylinder) contains the point, or -1
int MainWindow::enemyAtPoint(float x, float y, float z, float pad, HitPart *part) const
{
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive) continue;
        float r = 0.22f * e.scale + pad;
        float dx = x - e.x, dy = y - e.y;
        if (dx * dx + dy * dy > r * r) continue;
        float base = floorH(e.x, e.y) + e.hover, hz = z - base;
        if (hz < -pad || hz > 0.95f * e.scale + pad) continue;
        if (part) *part = hz > 0.74f * e.scale ? PART_HEAD : hz < 0.35f * e.scale ? PART_LIMB : PART_BODY;
        return i;
    }
    return -1;
}

void MainWindow::updateProjectiles(float dt)
{
    for (size_t pi = 0; pi < m_gs.projectiles.size(); pi++) {
        Projectile p = m_gs.projectiles[pi];               // copy: explode() may push new ones
        if (!p.alive) continue;
        p.life -= dt;
        // mines sit still until a hostile steps close
        if (p.throwKind == TH_MINE && p.armed) {
            for (const Enemy &e : m_gs.enemies) {
                if (!e.alive) continue;
                if ((e.x - p.x) * (e.x - p.x) + (e.y - p.y) * (e.y - p.y) < 1.1f * 1.1f) { p.alive = false; break; }
            }
            if (!p.alive) explode(p.x, p.y, p.z + 0.1f, THROWS[TH_MINE].radius, THROWS[TH_MINE].damage, p.owner, false);
            m_gs.projectiles[pi] = p;
            continue;
        }
        if (p.throwKind == TH_MINE) {
            p.fuse -= dt;
            if (p.fuse <= 0.f) { p.armed = true; sfxAt(SND_HITMARK, p.x, p.y, 0.5f, 0.6f); }
            m_gs.projectiles[pi] = p;
            continue;
        }
        // integrate in small steps so fast rounds can't tunnel through walls
        float speed = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
        int steps = std::max(1, int(speed * dt / 0.12f) + 1);
        float h = dt / steps;
        bool done = false;
        for (int s = 0; s < steps && !done; s++) {
            float ox = p.x, oy = p.y, oz = p.z;
            p.vz -= p.gravity * h;
            p.x += p.vx * h; p.y += p.vy * h; p.z += p.vz * h;
            int cx = int(std::floor(p.x)), cy = int(std::floor(p.y));
            if (!inside(cx, cy)) { p.alive = false; done = true; break; }
            if (p.owner == PO_ENEMY) {
                const Player &tp = m_gs.player;
                if (p.kind == PJ_ROCKET) {                       // slow homing missiles you can outrun / dodge
                    float hx = tp.posX - p.x, hy = tp.posY - p.y, hz = tp.footZ + 0.5f - p.z;
                    float hl = std::sqrt(hx * hx + hy * hy + hz * hz) + 1e-4f;
                    float sp = std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
                    float k = std::min(1.f, 1.1f * h);
                    p.vx += (hx / hl * sp - p.vx) * k; p.vy += (hy / hl * sp - p.vy) * k; p.vz += (hz / hl * sp - p.vz) * k;
                }
                float ddx = p.x - tp.posX, ddy = p.y - tp.posY;
                if (ddx * ddx + ddy * ddy < 0.16f && p.z > tp.footZ - 0.1f && p.z < tp.footZ + 0.95f && p.kind == PJ_ROCKET) {
                    explode(p.x, p.y, p.z, p.splashR, p.splashDmg, PO_ENEMY, false);
                    p.alive = false; done = true; break;
                }
            }
            // hostiles
            HitPart part = PART_BODY;
            int ei = p.owner == PO_ENEMY ? -1 : enemyAtPoint(p.x, p.y, p.z, p.kind == PJ_BULLET ? 0.02f : 0.08f, &part);
            if (ei >= 0 && p.throwKind < 0) {
                if (p.kind == PJ_BULLET) {
                    const WeaponDef &d = WEAPONS[p.weapon];
                    float mult = part == PART_HEAD ? d.headMult : part == PART_LIMB ? d.limbMult : 1.f;
                    damageEnemy(ei, int(p.damage * mult), part, p.x, p.y, p.z);
                    if (p.incendiary) igniteEnemy(ei, 2.5f);
                    m_gs.shotsHit++; m_gs.zoneHits++;
                    spawnBlood(p.x, p.y, p.z, 14, 1.4f);
                } else if (p.kind == PJ_GRENADE || p.kind == PJ_ROCKET || p.kind == PJ_PLASMA) {
                    damageEnemy(ei, int(p.damage), part, p.x, p.y, p.z);
                    if (p.incendiary) igniteEnemy(ei, 3.f);
                    explode(p.x, p.y, p.z, p.splashR, p.splashDmg, p.owner, p.incendiary && p.kind == PJ_PLASMA);
                    m_gs.shotsHit++; m_gs.zoneHits++;
                }
                p.alive = false; done = true; break;
            }
            float g = groundAt(p.x, p.y);
            if (p.z <= g) {
                bool wallHit = solidCell(cx, cy) && oz > groundAt(ox, oy) - 0.02f && g > oz;
                if ((p.kind == PJ_GRENADE && p.throwKind < 0) || (p.throwKind >= 0 && p.throwKind != TH_MOLOTOV)) {
                    // bounce: reflect off the wall face or the ground
                    p.x = ox; p.y = oy; p.z = std::max(oz, groundAt(ox, oy) + 0.02f);
                    if (wallHit) {
                        int ocx = int(std::floor(ox)), ocy = int(std::floor(oy));
                        if (ocx != cx) p.vx = -p.vx * 0.45f;
                        if (ocy != cy) p.vy = -p.vy * 0.45f;
                    } else { p.vz = -p.vz * 0.35f; p.vx *= 0.45f; p.vy *= 0.45f; }
                    if (++p.bounces < 12 && std::fabs(p.vz) > 0.6f) sfxAt(SND_GRENADE_BOUNCE, p.x, p.y, 0.6f, frandC(0.9f, 1.1f));
                    if (std::fabs(p.vz) < 0.5f && !wallHit) { p.vz = 0.f; p.z = groundAt(p.x, p.y) + 0.02f; }
                    continue;
                }
                // everything else stops on impact
                p.x = ox; p.y = oy; p.z = oz;
                if (p.kind == PJ_BULLET) {
                    spawnImpact(p.x, p.y, p.z, !wallHit);
                    sfxAt(SND_IMPACT, p.x, p.y, 0.7f);
                    if (!wallHit) addDecal(m_scorch, p.x, p.y, 0.03f, 160);
                    else {
                        int ocx = int(std::floor(ox)), ocy = int(std::floor(oy));
                        int face = cx != ocx ? (cx > ocx ? 0 : 1) : (cy > ocy ? 2 : 3);
                        float u = cx != ocx ? p.y - std::floor(p.y) : p.x - std::floor(p.x);
                        stampWallDecal(cx, cy, face, u, cellTop(cx, cy) - p.z, false);
                    }
                } else if (p.throwKind == TH_MOLOTOV) {
                    sfxAt(SND_FIRE_BURN, p.x, p.y, 0.9f);
                    sfxAt(SND_GRENADE_BOUNCE, p.x, p.y, 0.8f, 1.6f);
                    m_gs.fires.push_back(FireZone{p.x, p.y, THROWS[TH_MOLOTOV].radius, 6.f});
                    addDecal(m_scorch, p.x, p.y, THROWS[TH_MOLOTOV].radius * 0.8f, 150);
                } else explode(p.x, p.y, p.z, p.splashR, p.splashDmg, p.owner, false);
                p.alive = false; done = true;
            }
        }
        if (p.alive && (p.throwKind >= 0 || p.kind == PJ_GRENADE) && p.z <= groundAt(p.x, p.y) + 0.05f) {
            float k = std::exp(-6.f * dt);                     // rolling on the ground slows it quickly
            p.vx *= k; p.vy *= k;
        }
        if (p.alive && p.kind == PJ_ROCKET && p.throwKind < 0) {   // smoke trail
            Particle s; s.type = PT_SMOKE; s.x = p.x; s.y = p.y; s.z = p.z; s.vx = frandC(-0.2f, 0.2f); s.vy = frandC(-0.2f, 0.2f); s.vz = 0.2f;
            s.life = s.maxLife = 1.4f; s.size = 0.06f; s.color = rgb(170, 170, 165); m_gs.particles.push_back(s);
        }
        // fuses: grenades, smokes, flashbangs
        if (p.alive && p.fuse > 0.f) {
            p.fuse -= dt;
            if (p.fuse <= 0.f) {
                p.alive = false;
                if (p.throwKind == TH_SMOKE) {
                    m_gs.smokes.push_back(SmokeCloud{p.x, p.y, groundAt(p.x, p.y), THROWS[TH_SMOKE].radius, 14.f, 14.f});
                    sfxAt(SND_SMOKE_HISS, p.x, p.y, 0.9f);
                } else if (p.throwKind == TH_FLASH) flashbang(p.x, p.y, p.z);
                else if (p.owner == PO_ENEMY && p.incendiary) {    // acid glob: a pool that dries up after a while
                    SeedSpread s; s.egg = -1; s.t = 0.f; s.life = 9.f;
                    int cx0 = int(p.x), cy0 = int(p.y);
                    for (int yy = cy0 - 1; yy <= cy0 + 1; yy++) for (int xx = cx0 - 1; xx <= cx0 + 1; xx++)
                        if (inside(xx, yy) && !solidCell(xx, yy) && !m_acid[yy][xx] && (std::abs(xx - cx0) + std::abs(yy - cy0)) < 2) { m_acid[yy][xx] = 1; s.cells.push_back(yy * m_mapW + xx); }
                    m_seeds.push_back(s);
                    sfxAt(SND_ACID, p.x, p.y, 0.9f);
                    explode(p.x, p.y, p.z + 0.05f, 1.2f, p.splashDmg, PO_ENEMY, true);
                }
                else explode(p.x, p.y, p.z + 0.05f, p.throwKind == TH_FRAG ? THROWS[TH_FRAG].radius : p.splashR,
                             p.throwKind == TH_FRAG ? THROWS[TH_FRAG].damage : p.splashDmg, p.owner, false);
            }
        }
        if (p.life <= 0.f && p.alive) {
            p.alive = false;
            if (p.kind == PJ_ROCKET || p.kind == PJ_PLASMA) explode(p.x, p.y, p.z, p.splashR, p.splashDmg, p.owner, false);
        }
        m_gs.projectiles[pi] = p;
    }
    m_gs.projectiles.erase(std::remove_if(m_gs.projectiles.begin(), m_gs.projectiles.end(), [](const Projectile &p) { return !p.alive; }), m_gs.projectiles.end());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Explosions: line-of-sight radial damage, fireball, crater
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::explode(float x, float y, float z, float radius, float damage, int owner, bool small)
{
    Player &pl = m_gs.player;
    sfxAt(SND_EXPLOSION, x, y, small ? 0.6f : 1.f, frandC(0.9f, 1.05f) * (small ? 1.4f : 1.f));
    // fireball, sparks, smoke, debris
    int n = small ? 10 : 26;
    for (int i = 0; i < n; i++) {
        Particle p; p.type = i % 3 == 0 ? PT_SPARK : PT_FLAME;
        p.x = x; p.y = y; p.z = z + 0.05f;
        float a = frandC(0, 6.2831853f), e = frandC(0.1f, 1.2f), s = frandC(1.5f, 5.f) * (small ? 0.5f : 1.f);
        p.vx = std::cos(a) * std::cos(e) * s; p.vy = std::sin(a) * std::cos(e) * s; p.vz = std::sin(e) * s;
        p.life = p.maxLife = frandC(0.25f, 0.6f); p.size = p.type == PT_SPARK ? 0.015f : frandC(0.08f, 0.16f);
        p.color = rgb(255, 120 + rand() % 120, 40 + rand() % 40);
        m_gs.particles.push_back(p);
    }
    for (int i = 0; i < (small ? 4 : 14); i++) {
        Particle p; p.type = PT_SMOKE; p.x = x + frandC(-0.4f, 0.4f) * radius * 0.3f; p.y = y + frandC(-0.4f, 0.4f) * radius * 0.3f; p.z = z + 0.1f;
        p.vx = frandC(-0.5f, 0.5f); p.vy = frandC(-0.5f, 0.5f); p.vz = frandC(0.4f, 1.2f);
        p.life = p.maxLife = frandC(1.5f, 3.f); p.size = frandC(0.12f, 0.25f); p.color = rgb(70, 66, 62);
        m_gs.particles.push_back(p);
    }
    for (int i = 0; i < (small ? 3 : 10); i++) spawnImpact(x + frandC(-0.2f, 0.2f), y + frandC(-0.2f, 0.2f), z, true);
    addDecal(m_scorch, x, y, radius * 0.45f, 230);
    m_blastLight = std::max(m_blastLight, (small ? 0.35f : 0.8f) * clampf(1.2f - std::sqrt((x - m_camX) * (x - m_camX) + (y - m_camY) * (y - m_camY)) / 25.f, 0.f, 1.f));

    // damage: hostiles, squad, the player (all need line of sight to the blast)
    for (int i = 0; i < int(m_gs.enemies.size()) && owner != PO_ENEMY; i++) {
        Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive) continue;
        float ez = floorH(e.x, e.y) + 0.5f;
        float d = std::sqrt((e.x - x) * (e.x - x) + (e.y - y) * (e.y - y) + (ez - z) * (ez - z) * 0.5f);
        if (d > radius || !hasLOS(x, y, z + 0.15f, e.x, e.y, ez)) continue;
        float k = 1.f - d / radius;
        int dmg = int(damage * (0.25f + 0.75f * k));
        if (owner == PO_ACTOR && !m_gs.actors.empty()) damageEnemy(i, dmg, PART_BODY, e.x, e.y, ez, 0);
        else damageEnemy(i, dmg, PART_BODY, e.x, e.y, ez);
        e.stunT = std::max(e.stunT, 0.6f * k);
    }
    for (int i = 0; i < int(m_gs.actors.size()); i++) {
        Actor &a = m_gs.actors[size_t(i)];
        if (!a.alive) continue;
        float d = std::sqrt((a.posX - x) * (a.posX - x) + (a.posY - y) * (a.posY - y));
        if (d < radius * 0.8f && hasLOS(x, y, z + 0.15f, a.posX, a.posY, a.footZ + 0.5f))
            damageActor(i, int(damage * 0.3f * (1.f - d / radius)), x, y);
    }
    float pd = std::sqrt((pl.posX - x) * (pl.posX - x) + (pl.posY - y) * (pl.posY - y) + (pl.footZ + 0.4f - z) * (pl.footZ + 0.4f - z));
    if (pd < radius && hasLOS(x, y, z + 0.15f, pl.posX, pl.posY, pl.footZ + 0.5f)) {
        int dmg = int(damage * (owner == PO_ENEMY ? 1.f : 0.45f) * (1.f - pd / radius) * (1.f - pl.damageReduction));
        pl.hp -= dmg; m_gs.zoneDamage += dmg; pl.damageFlash = 0.5f;
        m_damageAngle = std::atan2(y - pl.posY, x - pl.posX); m_damageDirTimer = 2.f;
        addDamageMark(x, y, 1.f);
        if (pl.hp <= 0) {
            pl.hp = 0; pl.adsMode = false; m_killerX = x; m_killerY = y; m_killerZ = z + 0.5f; m_killerIdx = owner == PO_PLAYER ? -2 : -1;
            if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
                m_net->handleDeathDrop(m_net->localPlayerId(), -1, false, m_gs);
            } else {
                m_gs.gameOver = true;
            }
        }
    }
    pl.shakeY += 14.f * std::max(0.f, 1.f - pd / (radius * 4.f));

    // crater: big blasts dent the heightfield (only open ground; light map re-baked locally)
    if (!small && radius >= 2.2f) {
        int cx = int(x), cy = int(y);
        if (inside(cx, cy) && !solidCell(cx, cy) && !m_acid[cy][cx]) {
            const int VW = m_mapW + 1;
            bool changed = false;
            for (int j = cy - 1; j <= cy + 2; j++) for (int i = cx - 1; i <= cx + 2; i++) {
                if (i <= 1 || j <= 1 || i >= m_mapW - 1 || j >= m_mapH - 1) continue;
                float d = std::sqrt((i - x) * (i - x) + (j - y) * (j - y));
                if (d > 1.6f) continue;
                float &v = m_vh[size_t(j) * VW + i];
                float dent = 0.09f * (1.f - d / 1.6f);
                if (v > m_vhBase[size_t(j) * VW + i] - 0.22f) { v -= dent; changed = true; }
            }
            if (changed) {
                buildLightMap(cx - 3, cy - 3, cx + 4, cy + 4);
                // the player and actors stand on the new ground
                if (pl.footZ < floorH(pl.posX, pl.posY)) pl.footZ = floorH(pl.posX, pl.posY);
            }
        }
    }
}

void MainWindow::flashbang(float x, float y, float z)
{
    Player &pl = m_gs.player;
    sfxAt(SND_FLASHBANG, x, y, 1.f);
    m_blastLight = 1.f;
    for (Enemy &e : m_gs.enemies) {
        if (!e.alive) continue;
        float d = std::sqrt((e.x - x) * (e.x - x) + (e.y - y) * (e.y - y));
        if (d < THROWS[TH_FLASH].radius && hasLOS(x, y, z + 0.1f, e.x, e.y, floorH(e.x, e.y) + 0.7f))
            e.stunT = std::max(e.stunT, 1.2f + 3.2f * (1.f - d / THROWS[TH_FLASH].radius));
    }
    // the player is blinded if they can see it and are looking roughly at it
    float dx = x - pl.posX, dy = y - pl.posY, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
    float facing = (dx * pl.dirX + dy * pl.dirY) / d;
    if (d < 11.f && hasLOS(pl.posX, pl.posY, pl.footZ + 0.5f, x, y, z + 0.1f)) {
        float k = clampf((facing + 0.3f) / 1.3f, 0.f, 1.f) * (1.f - d / 11.f);
        if (k > 0.05f) { m_flashT = std::max(m_flashT, 0.6f + 3.f * k); sfx(SND_EAR_RING, 0.7f * k + 0.2f); }
    }
}

// Does a smoke cloud block the line between two points?
bool MainWindow::smokeBlocks(float x0, float y0, float z0, float x1, float y1, float z1) const
{
    for (const SmokeCloud &s : m_gs.smokes) {
        float grow = std::min(1.f, (s.maxLife - s.life) / 1.2f) * std::min(1.f, s.life / 2.f);
        float r = s.r * grow;
        if (r < 0.3f) continue;
        float dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
        float l2 = dx * dx + dy * dy + dz * dz;
        float t = clampf(((s.x - x0) * dx + (s.y - y0) * dy + (s.z + 0.6f - z0) * dz) / std::max(1e-6f, l2), 0.f, 1.f);
        float px = x0 + dx * t - s.x, py = y0 + dy * t - s.y, pz = z0 + dz * t - (s.z + 0.6f);
        if (px * px + py * py + pz * pz * 2.f < r * r) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Throwables: G (hold to cook), T cycles; mines are placed at your feet
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::startThrow()
{
    Player &pl = m_gs.player;
    if (m_cooking || pl.throwCount[pl.throwSel] <= 0) {
        if (pl.throwCount[pl.throwSel] <= 0) pushFeed(QString("NO %1 LEFT  [T] CYCLE").arg(THROWS[pl.throwSel].shortName), 1.5f);
        return;
    }
    m_cooking = true; m_cookT = 0.f;
    sfx(SND_GRENADE_PIN, 0.7f);
}

void MainWindow::releaseThrow()
{
    Player &pl = m_gs.player;
    if (!m_cooking) return;
    m_cooking = false;
    int k = pl.throwSel;
    if (pl.throwCount[k] <= 0) return;
    pl.throwCount[k]--;
    Projectile p;
    p.throwKind = k; p.owner = PO_PLAYER; p.kind = PJ_GRENADE;
    if (k == TH_MINE) {                                      // placed, not thrown
        p.x = pl.posX + pl.dirX * 0.5f; p.y = pl.posY + pl.dirY * 0.5f;
        if (!posValid(p.x, p.y, 0.1f)) { p.x = pl.posX; p.y = pl.posY; }
        p.z = floorH(p.x, p.y); p.vx = p.vy = p.vz = 0.f; p.fuse = THROWS[TH_MINE].fuse; p.life = 1e9f;
        m_gs.projectiles.push_back(p);
        sfx(SND_GRENADE_PIN, 0.5f, 0.7f);
        pushFeed("CLAYMORE PLACED", 1.5f);
        return;
    }
    float vx, vy, vz;
    throwVelocity(vx, vy, vz);
    p.x = pl.posX + pl.dirX * 0.3f; p.y = pl.posY + pl.dirY * 0.3f; p.z = pl.footZ + 0.6f;
    p.vx = vx; p.vy = vy; p.vz = vz;
    p.gravity = 9.8f; p.life = 12.f;
    p.fuse = k == TH_MOLOTOV ? 0.f : std::max(0.05f, THROWS[k].fuse - (k == TH_FRAG ? m_cookT : 0.f));
    m_gs.projectiles.push_back(p);
    m_kick = std::min(1.2f, m_kick + 0.4f);
    sfx(SND_KNIFE_SWING, 0.5f, 0.8f);
}

// Throws land where the crosshair is (up to 16 units): solve the lob for that
// spot with a fixed horizontal speed. Aiming at the sky gives a long arc.
void MainWindow::throwVelocity(float &vx, float &vy, float &vz)
{
    const Player &pl = m_gs.player;
    computeView();
    float ax, ay; aimPoint(ax, ay);
    float dx, dy, dz; aimDirection(ax, ay, dx, dy, dz);
    float sx = pl.posX + pl.dirX * 0.3f, sy = pl.posY + pl.dirY * 0.3f, sz = pl.footZ + 0.6f;
    HitResult h = hitscan(ax, ay);
    float hl = std::sqrt(dx * dx + dy * dy) + 1e-6f;
    float tx, ty, tz;
    if (!h.sky) { tx = h.x; ty = h.y; tz = h.enemy >= 0 ? floorH(h.x, h.y) : h.z; }   // at a hostile: land at its feet
    else { tx = sx + dx / hl * 16.f; ty = sy + dy / hl * 16.f; tz = sz + 1.f; }
    float hx = tx - sx, hy = ty - sy, hd = std::sqrt(hx * hx + hy * hy);
    if (hd > 16.f) { hx *= 16.f / hd; hy *= 16.f / hd; tz = sz + (tz - sz) * 16.f / hd; hd = 16.f; }
    float vh = 7.5f + hd * 0.25f;                          // longer throws are a bit faster
    float t = std::max(0.15f, hd / vh);
    vx = hd > 1e-3f ? hx / hd * vh : pl.dirX * vh * 0.2f;
    vy = hd > 1e-3f ? hy / hd * vh : pl.dirY * vh * 0.2f;
    vz = (tz - sz + 0.5f * 9.8f * t * t) / t;
    vx += pl.velX * 0.3f; vy += pl.velY * 0.3f;
}

void MainWindow::updateThrow(float dt)
{
    Player &pl = m_gs.player;
    if (!m_cooking) return;
    m_cookT += dt;
    if (!m_keys.count(Qt::Key_G)) { releaseThrow(); return; }
    // a frag cooked past its fuse goes off in your hand
    if (pl.throwSel == TH_FRAG && m_cookT >= THROWS[TH_FRAG].fuse) {
        m_cooking = false; pl.throwCount[TH_FRAG]--;
        pushFeed("COOKED TOO LONG!", 2.f);
        explode(pl.posX + pl.dirX * 0.3f, pl.posY + pl.dirY * 0.3f, pl.footZ + 0.5f, THROWS[TH_FRAG].radius, THROWS[TH_FRAG].damage, PO_PLAYER, false);
    }
}

void MainWindow::cycleThrowable()
{
    Player &pl = m_gs.player;
    for (int i = 1; i <= TH_COUNT; i++) {
        int k = (pl.throwSel + i) % TH_COUNT;
        if (pl.throwCount[k] > 0 || i == TH_COUNT) { pl.throwSel = k; break; }
    }
    sfx(SND_UI_MOVE, 0.4f);
    pushFeed(QString("THROWABLE: %1  x%2").arg(THROWS[pl.throwSel].name).arg(pl.throwCount[pl.throwSel]), 1.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Gadgets (Z)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::useGadget()
{
    Player &pl = m_gs.player;
    const GadgetDef &g = GADGETS[pl.gadget];
    if (pl.gadgetCd > 0.f) { pushFeed(QString("%1 RECHARGING  %2 S").arg(g.shortName).arg(int(std::ceil(pl.gadgetCd))), 1.2f); sfx(SND_DRY_FIRE, 0.5f); return; }
    float fx = pl.posX + pl.dirX * 0.9f, fy = pl.posY + pl.dirY * 0.9f;
    switch (pl.gadget) {
    case GD_TURRET:
        if (!posValid(fx, fy, 0.25f) || std::fabs(floorH(fx, fy) - pl.footZ) > 0.35f) { pushFeed("NO ROOM FOR THE TURRET", 1.5f); return; }
        m_gs.turrets.push_back(Turret{fx, fy, floorH(fx, fy), std::atan2(pl.dirY, pl.dirX), g.duration, 0.5f, -1, 0.f});
        sfx(SND_DEPLOY, 0.6f); pushFeed("SENTRY TURRET ONLINE", 2.f);
        break;
    case GD_MEDSTATION:
        m_gs.medStations.push_back(MedStation{pl.posX, pl.posY, pl.footZ, g.duration});
        sfx(SND_PICKUP_HEALTH, 0.6f); pushFeed("MEDKIT STATION DEPLOYED", 2.f);
        break;
    case GD_RADAR:
        m_radarT = g.duration; sfx(SND_RADAR_PING, 0.8f); pushFeed("RADAR PING - HOSTILES MARKED", 2.f);
        break;
    case GD_GRAPPLE: {
        // find a ledge (a higher floor cell) along the aim, up to 8 units away
        computeView();
        float ax, ay; aimPoint(ax, ay);
        HitResult h = hitscan(ax, ay);
        float hd = std::sqrt((h.x - pl.posX) * (h.x - pl.posX) + (h.y - pl.posY) * (h.y - pl.posY));
        if (h.sky || hd > 8.f || !h.wall) { pushFeed("GRAPPLE: NO LEDGE IN RANGE", 1.5f); sfx(SND_DRY_FIRE, 0.5f); return; }
        // the landing cell is the face's cell; it must be open ground (a ledge), not a wall
        int cx = h.cx, cy = h.cy;
        if (solidCell(cx, cy)) { pushFeed("GRAPPLE: CAN'T GRIP A WALL - AIM AT A LEDGE", 1.8f); sfx(SND_DRY_FIRE, 0.5f); return; }
        float tx = clampf(h.x + (pl.posX < h.x ? 0.35f : -0.35f) * (h.face <= 1 ? 1.f : 0.f), cx + 0.3f, cx + 0.7f);
        float ty = clampf(h.y + (pl.posY < h.y ? 0.35f : -0.35f) * (h.face >= 2 ? 1.f : 0.f), cy + 0.3f, cy + 0.7f);
        if (!posValid(tx, ty, 0.28f)) { tx = cx + 0.5f; ty = cy + 0.5f; }
        m_grappleT = 0.f; m_grappleDur = 0.25f + hd * 0.05f;
        m_grappleFromX = pl.posX; m_grappleFromY = pl.posY; m_grappleFromZ = pl.footZ;
        m_grappleX = tx; m_grappleY = ty; m_grappleZ = floorH(tx, ty);
        m_grappling = true;
        m_gs.beams.push_back(Beam{pl.posX, pl.posY, pl.footZ + 0.6f, h.x, h.y, h.z, m_grappleDur, m_grappleDur, rgb(200, 190, 170), 2});
        sfx(SND_GRAPPLE, 0.8f);
        break; }
    case GD_JETPACK:
        pl.velZ = 7.2f; pl.onGround = false;
        sfx(SND_JETPACK, 0.8f);
        for (int i = 0; i < 16; i++) {
            Particle p; p.type = PT_FLAME; p.x = pl.posX - pl.dirX * 0.2f; p.y = pl.posY - pl.dirY * 0.2f; p.z = pl.footZ + 0.3f;
            p.vx = frandC(-0.6f, 0.6f); p.vy = frandC(-0.6f, 0.6f); p.vz = frandC(-3.f, -1.f);
            p.life = p.maxLife = frandC(0.2f, 0.45f); p.size = 0.06f; p.color = rgb(255, 160, 60);
            m_gs.particles.push_back(p);
        }
        break;
    }
    pl.gadgetCd = g.cooldown;
}

// Turrets, medkit stations, smoke clouds, fire zones, beams, grapple pull
void MainWindow::updateAreaEffects(float dt)
{
    Player &pl = m_gs.player;
    pl.gadgetCd = std::max(0.f, pl.gadgetCd - dt);
    m_radarT = std::max(0.f, m_radarT - dt);
    m_flashT = std::max(0.f, m_flashT - dt);
    m_blastLight = std::max(0.f, m_blastLight - dt * 3.f);
    m_noiseT = std::max(0.f, m_noiseT - dt);
    m_pickupCooldown = std::max(0.f, m_pickupCooldown - dt);
    m_quickMeleeT = std::max(0.f, m_quickMeleeT - dt);
    if (m_inspectT > 0.f) m_inspectT = std::max(0.f, m_inspectT - dt);

    // grapple: arc the player up onto the ledge
    if (m_grappling) {
        m_grappleT += dt;
        float t = clampf(m_grappleT / m_grappleDur, 0.f, 1.f);
        float e = t * t * (3 - 2 * t);
        pl.posX = m_grappleFromX + (m_grappleX - m_grappleFromX) * e;
        pl.posY = m_grappleFromY + (m_grappleY - m_grappleFromY) * e;
        pl.footZ = m_grappleFromZ + (m_grappleZ - m_grappleFromZ) * e + std::sin(t * PI_C) * 0.45f;
        pl.velX = pl.velY = pl.velZ = 0.f; pl.onGround = false;
        if (t >= 1.f) { m_grappling = false; pl.footZ = m_grappleZ; pl.onGround = true; }
    }

    // smoke clouds keep billowing
    for (SmokeCloud &s : m_gs.smokes) {
        s.life -= dt;
        float grow = std::min(1.f, (s.maxLife - s.life) / 1.2f);
        for (int puff = 0; puff < 2 && s.life > 1.f; puff++) {
            Particle p; p.type = PT_SMOKE;
            float a = frandC(0, 6.2831853f), r = frandC(0.f, s.r * grow * 0.8f);
            p.x = s.x + std::cos(a) * r; p.y = s.y + std::sin(a) * r; p.z = s.z + frandC(0.f, 0.9f);
            p.vx = frandC(-0.15f, 0.15f); p.vy = frandC(-0.15f, 0.15f); p.vz = frandC(0.02f, 0.12f);
            p.life = p.maxLife = frandC(2.f, 3.5f); p.size = frandC(0.35f, 0.6f); p.color = rgb(196, 200, 204);
            m_gs.particles.push_back(p);
        }
    }
    m_gs.smokes.erase(std::remove_if(m_gs.smokes.begin(), m_gs.smokes.end(), [](const SmokeCloud &s) { return s.life <= 0; }), m_gs.smokes.end());

    // fire zones burn whatever stands in them
    for (FireZone &f : m_gs.fires) {
        f.life -= dt; f.tick -= dt;
        if (rand() % 2 == 0) {
            Particle p; p.type = PT_FLAME;
            float a = frandC(0, 6.2831853f), r = frandC(0.f, f.r);
            p.x = f.x + std::cos(a) * r; p.y = f.y + std::sin(a) * r; p.z = floorH(p.x, p.y) + 0.02f;
            p.vx = frandC(-0.1f, 0.1f); p.vy = frandC(-0.1f, 0.1f); p.vz = frandC(0.6f, 1.4f);
            p.life = p.maxLife = frandC(0.3f, 0.6f); p.size = frandC(0.05f, 0.1f); p.color = rgb(255, 140 + rand() % 80, 40);
            m_gs.particles.push_back(p);
        }
        if (f.tick > 0.f) continue;
        f.tick = 0.25f;
        for (int i = 0; i < int(m_gs.enemies.size()); i++) {
            Enemy &e = m_gs.enemies[size_t(i)];
            if (!e.alive) continue;
            if ((e.x - f.x) * (e.x - f.x) + (e.y - f.y) * (e.y - f.y) < f.r * f.r) igniteEnemy(i, 2.f);
        }
        float pd2 = (pl.posX - f.x) * (pl.posX - f.x) + (pl.posY - f.y) * (pl.posY - f.y);
        if (pd2 < f.r * f.r && pl.footZ < floorH(pl.posX, pl.posY) + 0.3f) {
            int dmg = int(4 * (1.f - pl.damageReduction * 0.5f));
            pl.hp -= dmg; m_gs.zoneDamage += dmg; pl.damageFlash = std::max(pl.damageFlash, 0.25f);
            if (m_hurtSndT <= 0) { sfx(SND_PLAYER_HURT, 0.5f); m_hurtSndT = 0.8f; }
            if (pl.hp <= 0) {
                pl.hp = 0; pl.adsMode = false;
                if (m_gameMode == MODE_MULTIPLAYER && m_net && m_net->isMultiplayerActive()) {
                    m_net->handleDeathDrop(m_net->localPlayerId(), -1, false, m_gs);
                } else {
                    m_gs.gameOver = true;
                }
            }
        }
    }
    m_gs.fires.erase(std::remove_if(m_gs.fires.begin(), m_gs.fires.end(), [](const FireZone &f) { return f.life <= 0; }), m_gs.fires.end());
    if (!m_gs.fires.empty() && int(m_time * 2) != int((m_time - dt) * 2)) sfxAt(SND_FIRE_BURN, m_gs.fires[0].x, m_gs.fires[0].y, 0.5f);

    // sentry turrets
    for (Turret &t : m_gs.turrets) {
        t.life -= dt; t.fireT -= dt; t.flash = std::max(0.f, t.flash - dt);
        float tz = t.z + 0.45f;
        int best = -1; float bestD = 14.f;
        for (int i = 0; i < int(m_gs.enemies.size()); i++) {
            const Enemy &e = m_gs.enemies[size_t(i)];
            if (!e.alive) continue;
            float d = std::sqrt((e.x - t.x) * (e.x - t.x) + (e.y - t.y) * (e.y - t.y));
            if (d < bestD && hasLOS(t.x, t.y, tz, e.x, e.y, floorH(e.x, e.y) + 0.55f) && !smokeBlocks(t.x, t.y, tz, e.x, e.y, floorH(e.x, e.y) + 0.55f)) { bestD = d; best = i; }
        }
        t.target = best;
        if (best < 0) { t.yaw += dt * 0.6f; continue; }
        const Enemy &e = m_gs.enemies[size_t(best)];
        float want = std::atan2(e.y - t.y, e.x - t.x);
        float diff = std::remainder(want - t.yaw, 6.2831853f);
        t.yaw += clampf(diff, -4.f * dt, 4.f * dt);
        if (std::fabs(diff) < 0.12f && t.fireT <= 0.f) {
            t.fireT = 0.18f; t.flash = 0.06f;
            sfxAt(SND_TURRET_FIRE, t.x, t.y, 0.6f, frandC(0.95f, 1.05f));
            float ez = floorH(e.x, e.y) + 0.55f;
            Tracer tr; tr.x0 = t.x + std::cos(t.yaw) * 0.3f; tr.y0 = t.y + std::sin(t.yaw) * 0.3f; tr.z0 = tz;
            if (frandC() < clampf(1.1f - bestD / 16.f, 0.3f, 0.9f)) {
                tr.x1 = e.x; tr.y1 = e.y; tr.z1 = ez;
                int ei = best;
                Enemy &te = m_gs.enemies[size_t(ei)];
                te.hp -= 14; te.hitFlash = 0.12f; te.hpShow = 2.f; te.state = ES_ALERT;
                spawnBlood(e.x, e.y, ez, 6, 0.8f);
                if (te.hp <= 0) {
                    te.hp = 0; te.alive = false; te.state = ES_DEAD; te.deathTime = 0;
                    m_gs.killsThisLevel++;
                    sfxAt(SND_ENEMY_DEATH, te.x, te.y, 0.8f);
                    pushFeed("TURRET  HOSTILE DOWN", 3.f);
                    addDecal(m_blood, te.x, te.y, 0.42f, 170);
                    if (te.boss || te.elite) dropReward(te.x, te.y, te.boss);
                }
            } else { tr.x1 = e.x + frandC(-0.7f, 0.7f); tr.y1 = e.y + frandC(-0.7f, 0.7f); tr.z1 = ez + frandC(-0.3f, 0.5f); }
            tr.len = std::sqrt((tr.x1 - tr.x0) * (tr.x1 - tr.x0) + (tr.y1 - tr.y0) * (tr.y1 - tr.y0) + (tr.z1 - tr.z0) * (tr.z1 - tr.z0));
            m_gs.tracers.push_back(tr);
        }
    }
    m_gs.turrets.erase(std::remove_if(m_gs.turrets.begin(), m_gs.turrets.end(), [](const Turret &t) { return t.life <= 0; }), m_gs.turrets.end());

    // medkit stations
    const int maxHP = this->maxHP();
    for (MedStation &m : m_gs.medStations) {
        m.life -= dt;
        if ((pl.posX - m.x) * (pl.posX - m.x) + (pl.posY - m.y) * (pl.posY - m.y) < 4.f && pl.hp < maxHP) {
            m_medAcc += 8.f * dt;
            if (m_medAcc >= 1.f) { int d = int(m_medAcc); m_medAcc -= d; pl.hp = std::min(maxHP, pl.hp + d); }
        }
        for (Actor &a : m_gs.actors)
            if (a.alive && (a.posX - m.x) * (a.posX - m.x) + (a.posY - m.y) * (a.posY - m.y) < 4.f) a.hp = std::min(120, a.hp + int(8.f * dt + frandC()));
    }
    m_gs.medStations.erase(std::remove_if(m_gs.medStations.begin(), m_gs.medStations.end(), [](const MedStation &m) { return m.life <= 0; }), m_gs.medStations.end());

    for (Beam &b : m_gs.beams) b.life -= dt;
    m_gs.beams.erase(std::remove_if(m_gs.beams.begin(), m_gs.beams.end(), [](const Beam &b) { return b.life <= 0; }), m_gs.beams.end());

    // LMG bipod: aiming, standing still, with cover (wall or ledge) right in front
    m_bipod = false;
    if (pl.curWeapon == WPN_LMG && pl.adsMode && pl.velX * pl.velX + pl.velY * pl.velY < 0.05f && pl.onGround) {
        float cx = pl.posX + pl.dirX * 0.6f, cy = pl.posY + pl.dirY * 0.6f;
        int ix = int(cx), iy = int(cy);
        if (inside(ix, iy) && (solidCell(ix, iy) || floorH(cx, cy) > pl.footZ + 0.2f)) m_bipod = true;
    }
}

// Burning, shock and stun tick on hostiles
void MainWindow::updateStatusEffects(float dt)
{
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive) { e.burnT = 0.f; continue; }
        e.shockT = std::max(0.f, e.shockT - dt);
        e.stunT = std::max(0.f, e.stunT - dt);
        if (e.burnT > 0.f) {
            e.burnT -= dt;
            e.burnAcc += 9.f * dt;
            if (rand() % 3 == 0) {
                Particle p; p.type = PT_FLAME; p.x = e.x + frandC(-0.15f, 0.15f); p.y = e.y + frandC(-0.15f, 0.15f);
                p.z = floorH(e.x, e.y) + frandC(0.2f, 0.9f) * e.scale; p.vx = 0; p.vy = 0; p.vz = frandC(0.5f, 1.2f);
                p.life = p.maxLife = 0.35f; p.size = 0.05f; p.color = rgb(255, 150, 50);
                m_gs.particles.push_back(p);
            }
            if (e.burnAcc >= 1.f) {
                int d = int(e.burnAcc); e.burnAcc -= d;
                e.hp -= d; e.hpShow = 1.5f;
                if (e.hp <= 0) { e.hp = 1; damageEnemy(i, 5, PART_BODY, e.x, e.y, floorH(e.x, e.y) + 0.5f); }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pickups: ammo, intel, throwables, guns (free slot = automatic, else [E] swap)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::updatePickups(float dt)
{
    Player &pl = m_gs.player;
    m_nearPickup = -1;
    const int maxHP = this->maxHP();
    for (auto &k : m_gs.healthKits) {
        if (!k.active) continue;
        k.bobPhase += dt * 2.2f;
        float dx = pl.posX - k.x, dy = pl.posY - k.y;
        if (dx * dx + dy * dy < 0.35f && pl.hp < maxHP && std::fabs(pl.footZ - floorH(k.x, k.y)) < 0.6f) {
            k.active = false;
            pl.hp = std::min(pl.hp + 30, maxHP);
            pushFeed("+30 HEALTH", 3.f);
            sfx(SND_PICKUP_HEALTH, 0.7f);
        }
    }
    for (int i = 0; i < int(m_gs.weaponPickups.size()); i++) {
        WeaponPickup &wp = m_gs.weaponPickups[size_t(i)];
        if (!wp.active) continue;
        wp.bobPhase += dt * 2.8f;
        // legacy markers become real weapon pickups
        if (wp.type == PICKUP_SHOTGUN || wp.type == PICKUP_RIFLE) { wp.weapon = wp.type == PICKUP_SHOTGUN ? WPN_SHOTGUN : WPN_RIFLE; wp.type = PICKUP_WEAPON; }
        float dx = pl.posX - wp.x, dy = pl.posY - wp.y;
        if (dx * dx + dy * dy >= 0.35f || std::fabs(pl.footZ - floorH(wp.x, wp.y)) > 0.6f) continue;
        if (wp.type == PICKUP_KEY) {
            wp.active = false; m_keyCount++;
            pushFeed("KEY CARD ACQUIRED", 3.f, true);
            sfx(SND_INTEL, 0.8f);
        } else if (wp.type == PICKUP_INTEL) {
            wp.active = false; m_intelCount++;
            for (int w = 0; w < WPN_COUNT; w++) if (pl.owned[w] && usesAmmo(w)) pl.reserve[w] = WEAPONS[w].maxReserve;
            pl.hp = maxHP;
            m_gs.score += 1000;
            pushFeed("INTEL RECOVERED  +1000", 4.5f, true);
            sfx(SND_INTEL, 0.8f);
        } else if (wp.type == PICKUP_AMMO) {
            wp.active = false;
            bool scav = wp.emergency;
            for (int w = 0; w < WPN_COUNT; w++) if (pl.owned[w] && usesAmmo(w)) {
                int add = scav ? WEAPONS[w].magSize : WEAPONS[w].magSize * 2;
                pl.reserve[w] = std::min(pl.reserve[w] + add, WEAPONS[w].maxReserve);
            }
            pushFeed(scav ? "+ SCAVENGER RATIONS (1 MAG)" : "+ AMMO", 3.f);
            sfx(SND_PICKUP_AMMO, 0.7f);
            if (pl.curWeapon >= 0 && usesAmmo(pl.curWeapon) && pl.mag[pl.curWeapon] == 0 && pl.reserve[pl.curWeapon] > 0 && pl.reloadTimer <= 0) startReload();
        } else if (wp.type == PICKUP_THROW) {
            int k = wp.throwKind;
            if (pl.throwCount[k] >= THROWS[k].maxCarry) continue;
            wp.active = false;
            pl.throwCount[k] = std::min(THROWS[k].maxCarry, pl.throwCount[k] + (k == TH_MINE ? 1 : 2));
            pushFeed(QString("+ %1").arg(THROWS[k].name), 2.5f);
            sfx(SND_PICKUP_AMMO, 0.6f, 1.2f);
        } else if (wp.type == PICKUP_WEAPON && wp.weapon >= 0) {
            int w = wp.weapon;
            if (pl.owned[w]) {                              // same gun: take its ammo (and a better copy)
                if (wp.load.rarity > pl.load[w].rarity) { m_nearPickup = i; continue; }
                if (pl.reserve[w] >= WEAPONS[w].maxReserve) continue;
                wp.active = false;
                giveWeapon(w, wp.load, wp.mag, wp.reserve, false);
                pushFeed(QString("+ AMMO  %1").arg(WEAPONS[w].name), 3.f);
                sfx(SND_PICKUP_AMMO, 0.75f);
                continue;
            }
            int s = targetSlotFor(w);
            if (pl.slot[s] < 0 && m_pickupCooldown <= 0.f) {   // a free slot: just take it
                wp.active = false;
                giveWeapon(w, wp.load, wp.mag, wp.reserve, true);
                pushFeed(QString("NEW WEAPON  %1 %2").arg(RARITY_NAMES[wp.load.rarity]).arg(WEAPONS[w].name), 4.f, wp.load.rarity >= 3);
                sfx(SND_PICKUP_WEAPON, 0.75f);
            } else m_nearPickup = i;                        // full: offer a swap on [E]
        }
    }
}

bool MainWindow::trySwapPickup()
{
    if (m_nearPickup < 0 || m_nearPickup >= int(m_gs.weaponPickups.size())) return false;
    WeaponPickup wp = m_gs.weaponPickups[size_t(m_nearPickup)];
    if (!wp.active || wp.type != PICKUP_WEAPON) return false;
    Player &pl = m_gs.player;
    m_gs.weaponPickups[size_t(m_nearPickup)].active = false;
    if (pl.owned[wp.weapon]) {                              // upgrade to the better copy, keep ammo
        int mag = pl.mag[wp.weapon], res = pl.reserve[wp.weapon];
        Loadout old = pl.load[wp.weapon];
        pl.load[wp.weapon] = wp.load;
        for (int a = 0; a < AT_COUNT; a++) if (pl.load[wp.weapon].att[a] == 0) pl.load[wp.weapon].att[a] = old.att[a];
        pl.mag[wp.weapon] = std::min(mag, effectiveMagSize(wp.weapon)); pl.reserve[wp.weapon] = res;
    } else giveWeapon(wp.weapon, wp.load, wp.mag, wp.reserve, true);
    m_pickupCooldown = 1.f;
    m_nearPickup = -1;
    pushFeed(QString("PICKED UP  %1 %2").arg(RARITY_NAMES[wp.load.rarity]).arg(WEAPONS[wp.weapon].name), 3.5f, wp.load.rarity >= 3);
    sfx(SND_PICKUP_WEAPON, 0.75f);
    return true;
}

// A random gun suited to the current zone. Rarity rolls get better with level (and bosses).
int MainWindow::randomWeaponForLevel(int lv, bool boss) const
{
    std::vector<int> pool;
    int maxTier = std::min(4, 1 + lv / 2 + (boss ? 1 : 0));
    for (int w = 1; w < WPN_COUNT; w++) {
        if (isMelee(w) && !boss) continue;
        if (WEAPONS[w].tier <= maxTier && WEAPONS[w].tier >= 1) pool.push_back(w);
    }
    // prefer guns the player doesn't have
    std::vector<int> fresh;
    for (int w : pool) if (!m_gs.player.owned[w]) fresh.push_back(w);
    const std::vector<int> &src = fresh.empty() ? pool : fresh;
    return src.empty() ? WPN_RIFLE : src[size_t(rand()) % src.size()];
}

Loadout MainWindow::rollLoadout(int w, int lv, bool boss) const
{
    Loadout L;
    float r = frandC() + lv * 0.06f + (boss ? 0.55f : 0.f);
    L.rarity = r > 1.35f ? 4 : r > 1.05f ? 3 : r > 0.8f ? 2 : r > 0.55f ? 1 : 0;
    // better guns come with a couple of attachments
    for (int k = 0; k < L.rarity; k++) {
        int slot = rand() % AT_COUNT, opt = 1 + rand() % std::max(1, ATT_OPTIONS[slot] - 1);
        if (attachmentFits(w, slot, opt)) L.att[slot] = uint8_t(opt);
    }
    return L;
}

void MainWindow::spawnWeaponPickup(float x, float y, int w, const Loadout &L, bool reward)
{
    WeaponPickup wp; wp.x = x; wp.y = y; wp.bobPhase = frandC(0, 6.28f);
    wp.type = PICKUP_WEAPON; wp.weapon = w; wp.load = L; wp.reward = reward;
    m_gs.weaponPickups.push_back(wp);
}

void MainWindow::spawnPickup(float x, float y)
{
    int r = rand() % 10;
    if (r < 5) { WeaponPickup wp; wp.x = x; wp.y = y; wp.bobPhase = frandC(0, 6.28f); wp.type = PICKUP_AMMO; m_gs.weaponPickups.push_back(wp); }
    else if (r < 7) { WeaponPickup wp; wp.x = x; wp.y = y; wp.type = PICKUP_THROW; wp.throwKind = rand() % TH_COUNT; m_gs.weaponPickups.push_back(wp); }
    else { int w = randomWeaponForLevel(m_gs.level, false); spawnWeaponPickup(x, y, w, rollLoadout(w, m_gs.level, false), false); }
}

// Bosses and elites always drop a gun (one you don't carry if possible)
void MainWindow::dropReward(float x, float y, bool boss)
{
    int w = randomWeaponForLevel(m_gs.level + (boss ? 2 : 1), boss);
    Loadout L = rollLoadout(w, m_gs.level + 2, boss);
    if (boss && L.rarity < 3) L.rarity = 3;
    spawnWeaponPickup(x, y, w, L, true);
    if (boss) {
        HealthKit k; k.x = x + 0.45f; k.y = y; k.bobPhase = 0.f;
        if (!posValid(k.x, k.y, 0.1f)) k.x = x - 0.45f;
        if (posValid(k.x, k.y, 0.1f)) m_gs.healthKits.push_back(k);
        WeaponPickup am; am.x = x; am.y = y + 0.45f; am.type = PICKUP_AMMO;
        if (posValid(am.x, am.y, 0.1f)) m_gs.weaponPickups.push_back(am);
        WeaponPickup th; th.x = x - 0.4f; th.y = y - 0.4f; th.type = PICKUP_THROW; th.throwKind = TH_FRAG;
        if (posValid(th.x, th.y, 0.1f)) m_gs.weaponPickups.push_back(th);
    }
    pushFeed(QString("%1 DROPPED  %2 %3").arg(boss ? "COMMANDER" : "ELITE").arg(RARITY_NAMES[L.rarity]).arg(WEAPONS[w].name), 4.5f, true);
    sfx(SND_RADIO, 0.5f);
}
