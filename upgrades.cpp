// =============================================================================
//  upgrades.cpp  -  Zone-clear upgrade cards
//
//  Cards: fit an attachment to a gun you carry, unlock a new gun, refine a
//  gun's rarity, restock throwables, or swap your gadget.
// =============================================================================
#include "mainwindow.h"
#include <algorithm>

static QString attBonus(int slot, int opt)
{
    switch (slot) {
    case AT_OPTIC:  { static const char *b[6] = { "", "1x reflex dot", "1x holographic ring", "2x magnified optic", "4x magnified optic", "8x long-range optic" }; return b[opt]; }
    case AT_MUZZLE: return opt == 1 ? "Quiet shots: distant hostiles aren't alerted" : "-45% recoil, -30% spread";
    case AT_MAG:    return "+50% magazine capacity";
    case AT_UNDER:  return opt == 1 ? "-25% recoil" : "-40% hip-fire spread + visible laser";
    case AT_STOCK:  return "-12% recoil, steadier aim";
    case AT_AMMO:   { static const char *b[4] = { "", "+10% damage, punches through doors", "Sets hostiles on fire", "+25% body damage" }; return b[opt]; }
    }
    return "";
}

void MainWindow::applyUpgrade(const UpgradeCard &c)
{
    Player &pl = m_gs.player;
    int w = c.weapon;
    if (c.slot == UC_UNLOCK && w >= 0) {
        Loadout L; L.rarity = uint8_t(c.option);
        giveWeapon(w, L, -1, -1, false);
        pl.mag[w] = effectiveMagSize(w);
        pl.reserve[w] = std::min(WEAPONS[w].maxReserve, std::max(pl.reserve[w], WEAPONS[w].magSize * 2));
    } else if (c.slot >= 0 && c.slot < AT_COUNT && w >= 0 && pl.owned[w]) {
        pl.load[w].att[c.slot] = uint8_t(c.option);
        if (c.slot == AT_MAG) pl.mag[w] = effectiveMagSize(w);
    } else if (c.slot == UC_RARITY && w >= 0 && pl.owned[w]) {
        pl.load[w].rarity = uint8_t(std::min(RARITY_COUNT - 1, int(pl.load[w].rarity) + 1));
    } else if (c.slot == UC_THROWS) {
        for (int k = 0; k < TH_COUNT; k++) pl.throwCount[k] = std::min(THROWS[k].maxCarry, pl.throwCount[k] + (k == TH_FRAG ? 2 : 1));
    } else if (c.slot == UC_GADGET) {
        pl.gadget = c.option; pl.gadgetCd = 0.f;
    } else {
        pl.hp = std::min(CHAR_PROFILES[m_charChoice].hp, pl.hp + 50);
        for (int g = 0; g < WPN_COUNT; g++) if (pl.owned[g] && usesAmmo(g)) pl.reserve[g] = WEAPONS[g].maxReserve;
    }
}

void MainWindow::generateUpgradeChoices()
{
    m_upgradeCards.clear();
    m_selectedUpgradeCard = -1;
    m_upgradePicked = false;
    const Player &pl = m_gs.player;
    std::vector<UpgradeCard> pool;

    // A new gun for the next zone: always offered first
    int nw = randomWeaponForLevel(m_gs.level + 1, false);
    if (nw >= 0 && !pl.owned[nw] && !isMelee(nw)) {
        Loadout L = rollLoadout(nw, m_gs.level, false);
        const WeaponDef &d = WEAPONS[nw];
        QString stat = d.proj == PJ_HITSCAN ? QString("%1 dmg x%2  %3 rnd").arg(d.damage).arg(d.pellets).arg(d.magSize)
                                            : QString("%1  %2").arg(d.splashDmg > 0 ? "explosive" : "special").arg(d.magSize ? QString("%1 rnd").arg(d.magSize) : QString("energy"));
        pool.push_back(UpgradeCard{nw, UC_UNLOCK, L.rarity, QString("%1: FIELD ISSUE").arg(d.name),
                                   QString("%1 %2. It will be in your hands when you deploy.").arg(RARITY_NAMES[L.rarity]).arg(d.name),
                                   "NEW WEAPON  " + stat});
    }

    // Attachments for the guns you carry
    for (int s = SLOT_SIDEARM; s < SLOT_COUNT; s++) {
        int w = pl.slot[s];
        if (w < 0) continue;
        const Loadout &L = pl.load[w];
        for (int slot = 0; slot < AT_COUNT; slot++) {
            int cur = L.att[slot];
            std::vector<int> opts;
            for (int o = 1; o < ATT_OPTIONS[slot]; o++)
                if (o != cur && attachmentFits(w, slot, o) && (slot != AT_OPTIC || o > cur)) opts.push_back(o);
            if (opts.empty()) continue;
            int o = opts[size_t(rand()) % opts.size()];
            pool.push_back(UpgradeCard{w, slot, o, QString("%1: %2").arg(WEAPONS[w].shortName).arg(ATT_NAMES[slot][o]),
                                       QString("Fit a %1 to your %2.").arg(QString(ATT_NAMES[slot][o]).toLower()).arg(WEAPONS[w].name),
                                       attBonus(slot, o)});
        }
        if (L.rarity < RARITY_COUNT - 1)
            pool.push_back(UpgradeCard{w, UC_RARITY, L.rarity + 1, QString("%1: REFINED").arg(WEAPONS[w].shortName),
                                       QString("Gunsmith work raises it to %1.").arg(RARITY_NAMES[L.rarity + 1]),
                                       QString("+%1% damage, -%2% recoil").arg(int((RARITY_DMG[L.rarity + 1] - 1) * 100)).arg(int((1 - RARITY_RECOIL[L.rarity + 1]) * 100))});
    }
    pool.push_back(UpgradeCard{-1, UC_THROWS, 0, "MUNITIONS CRATE", "Restock frags, smoke, flashbangs, molotovs and a mine.", "+2 FRAG  +1 OF EVERY OTHER"});
    int g = (pl.gadget + 1 + rand() % (GD_COUNT - 1)) % GD_COUNT;
    pool.push_back(UpgradeCard{-1, UC_GADGET, g, QString("GADGET: %1").arg(GADGETS[g].name), GADGETS[g].desc,
                               QString("[Z] TO USE   %1 S COOLDOWN").arg(int(GADGETS[g].cooldown))});

    // shuffle everything after the unlock card
    size_t first = (!pool.empty() && pool[0].slot == UC_UNLOCK) ? 1 : 0;
    for (size_t i = pool.size(); i > first + 1; i--) std::swap(pool[i - 1], pool[first + size_t(rand()) % (i - first)]);
    for (size_t i = 0; i < std::min(size_t(3), pool.size()); i++) m_upgradeCards.push_back(pool[i]);
    while (m_upgradeCards.size() < 3)
        m_upgradeCards.push_back(UpgradeCard{-1, UC_RESTOCK, 0, "FIELD SUPPLY RESTOCK", "Combat supply drop replenishing all ammo reserves and field health.", "+100% Reserve Ammo & +50 HP"});
}
