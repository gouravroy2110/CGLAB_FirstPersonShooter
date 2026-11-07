// =============================================================================
//  rogue_brain.h - Local Lightweight Reinforcement Learning Policy Engine
//  for the Adaptive Rogue Operative / Infiltrator
// =============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <QString>

enum RogueAction {
    ROGUE_ACT_KITE = 0,          // Backpedal while firing, keep 12-16m distance (counters shotguns)
    ROGUE_ACT_FLANK = 1,         // Circle around cover to attack from the blind side (counters snipers)
    ROGUE_ACT_PUNISH_RUSH = 2,   // Sprint rush when player is vulnerable (reloading / low HP)
    ROGUE_ACT_PUPPETEER = 3,     // Draw / bait standard enemies into player's crossfire
    ROGUE_ACT_RETREAT = 4,       // Break line of sight to regain tactical posture
    ROGUE_ACT_COUNT = 5
};

// Tactical states (discrete state space: 3 * 4 * 3 * 2 = 72 states)
// Dim 0: Distance Band (0: Close < 7m, 1: Mid 7-15m, 2: Far > 15m)
// Dim 1: Player Weapon (0: Shotgun/CQC, 1: Automatic/Rifle, 2: Sniper/DMR, 3: Heavy/Other)
// Dim 2: Player Posture (0: Aggressive Rush, 1: In Cover / Stationary, 2: Vulnerable [Reloading / Low HP])
// Dim 3: Nearby Threat (0: Player isolated, 1: Regular guards nearby [Bait opportunity])

struct PlayerCombatTracker {
    float avgCombatDist = 12.f;
    int   samples = 0;
    int   shotgunShots = 0;
    int   sniperShots = 0;
    int   autoShots = 0;
    int   otherShots = 0;
    float timeSprinting = 0.f;
    float timeCover = 0.f;
    float totalTime = 0.f;
    int   exposedReloads = 0;

    void reset() {
        avgCombatDist = 12.f;
        samples = 0;
        shotgunShots = sniperShots = autoShots = otherShots = 0;
        timeSprinting = timeCover = totalTime = 0.f;
        exposedReloads = 0;
    }

    void recordDistance(float d) {
        if (d > 0.1f) {
            avgCombatDist = (avgCombatDist * samples + d) / float(samples + 1);
            samples++;
        }
    }

    void recordShot(int weaponClass) {
        if (weaponClass == 4) shotgunShots++;      // WC_SHOTGUN
        else if (weaponClass == 6) sniperShots++;  // WC_SNIPER
        else if (weaponClass == 2 || weaponClass == 3 || weaponClass == 5) autoShots++; // SMG, Rifle, LMG
        else otherShots++;
    }

    void updateTime(float dt, bool sprinting, bool crouched, bool moving) {
        totalTime += dt;
        if (sprinting && moving) timeSprinting += dt;
        if (crouched && !moving) timeCover += dt;
    }

    QString diagnosedArchetype() const {
        int totalShots = shotgunShots + sniperShots + autoShots + otherShots;
        if (totalShots >= 3 && (shotgunShots * 100 / totalShots) >= 35)
            return "CQB Brawler (Shotgun Rusher)";
        if (totalShots >= 3 && (sniperShots * 100 / totalShots) >= 30)
            return "Long-Range Marksman (Camper)";
        if (totalTime > 8.f && (timeCover / totalTime) > 0.30f)
            return "Tactical Pointman (Cover Hugger)";
        if (totalTime > 8.f && (timeSprinting / totalTime) > 0.30f)
            return "Run & Gun Skirmisher (Aggressive)";
        return "Versatile Combatant";
    }

    QString diagnoseCombatStyle() const { return diagnosedArchetype(); }

    QString favoriteWeaponName() const {
        if (shotgunShots >= sniperShots && shotgunShots >= autoShots && shotgunShots > 0) return "Shotgun";
        if (sniperShots >= autoShots && sniperShots > 0) return "Sniper";
        if (autoShots > 0) return "Assault / SMG";
        return "Rifle";
    }

    float movementPcnt() const {
        return totalTime > 0.1f ? std::min(1.f, (timeSprinting + (totalTime - timeCover)) / (2.f * totalTime)) : 0.5f;
    }
};

struct RogueThreatReport {
    int   generation = 1;
    int   encounters = 0;
    int   playerWon = 0;
    int   rogueWon = 0;
    int   infightingKills = 0;     // Regular guards killed by Rogue
    int   kitesExecuted = 0;
    int   flanksExecuted = 0;
    int   punishesExecuted = 0;
    int   puppeteerLures = 0;
    float adaptationScore = 20.f;  // % adaptation to player

    void reset() {
        generation = 1;
        encounters = playerWon = rogueWon = infightingKills = 0;
        kitesExecuted = flanksExecuted = punishesExecuted = puppeteerLures = 0;
        adaptationScore = 20.f;
    }
};

class RoguePolicyEngine {
public:
    static const int NUM_STATES = 72;
    float Q[NUM_STATES][ROGUE_ACT_COUNT];
    int   lastState = 0;
    int   lastAction = 0;
    int   generation = 1;
    bool  hasPendingUpdate = false;

    RoguePolicyEngine() {
        reset();
    }

    void reset() {
        generation = 1;
        lastState = 0;
        lastAction = 0;
        hasPendingUpdate = false;
        initBaselinePolicy();
    }

    void initBaselinePolicy() {
        // Pre-initialize Q-values with strong tactical heuristics
        for (int s = 0; s < NUM_STATES; s++) {
            int dBand = s / 24;
            int wClass = (s / 6) % 4;
            int posture = (s / 2) % 3;
            int guards = s % 2;

            for (int a = 0; a < ROGUE_ACT_COUNT; a++) Q[s][a] = 10.f;

            // If player has shotgun/CQC and is close: kiting is heavily rewarded
            if (wClass == 0 && dBand == 0) Q[s][ROGUE_ACT_KITE] += 26.f;
            // If player is sniper/camping far: flanking with cover is rewarded
            if (wClass == 2 || posture == 1) Q[s][ROGUE_ACT_FLANK] += 24.f;
            // If player is vulnerable (reloading or low HP): punish rush is rewarded
            if (posture == 2) Q[s][ROGUE_ACT_PUNISH_RUSH] += 32.f;
            // If guards are nearby: puppeteer crossfire bait is rewarded
            if (guards == 1) Q[s][ROGUE_ACT_PUPPETEER] += 22.f;
            // Far distance: flank or close distance
            if (dBand == 2) Q[s][ROGUE_ACT_FLANK] += 16.f;
        }
    }

    int encodeState(float dist, int playerWpnClass, bool reloadingOrLowHp, bool playerStationary, bool guardsNearby) {
        int dBand = dist < 7.f ? 0 : dist < 16.f ? 1 : 2;
        int wClass = (playerWpnClass == 4) ? 0 : (playerWpnClass == 6) ? 2 : (playerWpnClass == 2 || playerWpnClass == 3 || playerWpnClass == 5) ? 1 : 3;
        int posture = reloadingOrLowHp ? 2 : playerStationary ? 1 : 0;
        int guards = guardsNearby ? 1 : 0;
        return std::clamp(dBand * 24 + wClass * 6 + posture * 2 + guards, 0, NUM_STATES - 1);
    }

    int selectAction(int state, bool explore = true) {
        lastState = state;
        // Epsilon-greedy (90% exploit optimal learned policy, 10% explore)
        if (explore && (rand() % 100 < 10)) {
            lastAction = rand() % ROGUE_ACT_COUNT;
            hasPendingUpdate = true;
            return lastAction;
        }
        int bestA = 0;
        float bestVal = Q[state][0];
        for (int a = 1; a < ROGUE_ACT_COUNT; a++) {
            if (Q[state][a] > bestVal) {
                bestVal = Q[state][a];
                bestA = a;
            }
        }
        lastAction = bestA;
        hasPendingUpdate = true;
        return bestA;
    }

    void updateReward(float reward, int nextState) {
        if (!hasPendingUpdate) return;
        nextState = std::clamp(nextState, 0, NUM_STATES - 1);
        float maxNext = Q[nextState][0];
        for (int a = 1; a < ROGUE_ACT_COUNT; a++) {
            if (Q[nextState][a] > maxNext) maxNext = Q[nextState][a];
        }
        const float alpha = 0.28f;
        const float gamma = 0.85f;
        Q[lastState][lastAction] += alpha * (reward + gamma * maxNext - Q[lastState][lastAction]);
        hasPendingUpdate = false;
    }

    void onGenerationAdvance() {
        generation++;
        // Retain and slightly sharpen high-performing action weights
        for (int s = 0; s < NUM_STATES; s++) {
            for (int a = 0; a < ROGUE_ACT_COUNT; a++) {
                if (Q[s][a] > 15.f) Q[s][a] = std::min(90.f, Q[s][a] * 1.05f);
            }
        }
    }
};
