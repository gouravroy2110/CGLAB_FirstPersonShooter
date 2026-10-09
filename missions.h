// =============================================================================
//  missions.h  -  "Operation Ashfall": the story campaign as data
//
//  A private military contractor, Helix Dynamics, has taken over a mountain
//  research region and is turning its power grid into a weapon. Its founder,
//  Aurelio Helix, built a battlefield AI called VECTOR to run it. You are a
//  lone operator, call sign RASTER, guided over the radio by Major Kade and
//  later by Dr. Ortiz, the scientist who wrote VECTOR's safety lock.
//
//  Each mission = one map (with its biome, time of day and weather) + an
//  ordered list of objectives + radio lines + a boss. Missions reuse the
//  markers in the map layouts: digits are objective points, b/c/f/g/y are
//  boss spots, n is the escort, z the extraction.
// =============================================================================
#pragma once
#include "game.h"
#include "assets.h"

enum ObjType {
    OBJ_KILL_COUNT,   // eliminate `count` hostiles
    OBJ_KILL_ALL,     // clear every hostile on the map
    OBJ_REACH,        // get to a marker (radius 2)
    OBJ_COLLECT,      // pick up items placed at markers (key card, intel)
    OBJ_INTERACT,     // hold E at each marker for `time` seconds
    OBJ_HOLD,         // stand in a zone at each marker for `time` seconds
    OBJ_SURVIVE,      // survive `time` seconds of waves near a marker
    OBJ_ESCORT,       // bring the scientist (starts at 'n') to the marker
    OBJ_BOSS,         // defeat the mission boss
};

// What happens when an objective begins
enum ObjEvent { EV_NONE, EV_SPAWN_BOSS, EV_WAVES, EV_OPEN_GATE };

struct ObjectiveDef {
    ObjType type;
    const char *markers;      // marker characters this objective uses ("123", "t", "z")
    int   count;
    float time;               // seconds (interact / hold / survive)
    ObjEvent event;
    const char *text;         // tracker line
    const char *radio;        // radio line said when it starts (speaker: text), or ""
};

struct RadioLine { int speaker; const char *text; };
enum Speaker { SPK_KADE = 0, SPK_ORTIZ, SPK_VECTOR, SPK_HELIX, SPK_RASTER, SPK_COUNT };

struct BriefPanel { int speaker; const char *title; const char *text; int view; };   // view: 0 portrait, 1 map flyover, 2 tactical map

struct MissionDef {
    const char *name, *codename;
    int   act;                // 1..3, 4 = finale
    int   map;
    float hour;
    int   weather;
    int   boss;               // BossKind (second twin / guard spawn automatically)
    const char *bossMarkers;  // where the boss (and partner) appear
    int   enemies;            // regular hostiles on the map at the start
    EnemyType mix[4];         // what they are (weighted equally)
    bool  stealth;            // alarm meter instead of an open fight
    float parTime;
    int   reward;             // coins for a clear (x rank bonus)
    BriefPanel brief[3];
    ObjectiveDef obj[6];
    int   objCount;
    RadioLine outro;
};

static const char *const SPEAKER_NAMES[SPK_COUNT] = { "MAJOR KADE", "DR. ORTIZ", "VECTOR", "AURELIO HELIX", "RASTER" };

static const int MISSION_COUNT = 9;
static const MissionDef MISSIONS[MISSION_COUNT] = {
    // ── ACT I ──────────────────────────────────────────────────────────────────────
    {"BREACH", "MISSION 1", 1, 1, 9.f, WX_NONE, BK_WARDEN, "b", 12, {ET_RIFLEMAN, ET_SCOUT, ET_SHOTGUNNER, ET_RIFLEMAN}, false, 300.f, 120,
     {{SPK_KADE, "OPERATION ASHFALL", "Helix Dynamics took the Dune Outpost three days ago. Since then the whole valley's grid has gone dark. Get in, find out why.", 1},
      {SPK_KADE, "THE OUTPOST", "Their warden runs the gate. Big, armoured, likes to charge. Make him hit a wall, then hit him back.", 0},
      {SPK_KADE, "OBJECTIVES", "Clear the compound. Find the key card in the west bunker. Deal with the Warden.", 2}},
     {{OBJ_KILL_COUNT, "", 6, 0, EV_NONE, "Clear the outpost compound", "KADE: Contacts in the compound. Thin them out."},
      {OBJ_COLLECT, "1", 1, 0, EV_NONE, "Find the key card (west bunker)", "KADE: Key card should be in the west bunker."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Defeat the Warden", "KADE: That's him. Watch the floor when he lines up a charge."}},
     3, {SPK_KADE, "Good work, Raster. The card points to a radio net in the swamp. Helix is talking to someone."}},
    {"DEAD WIRE", "MISSION 2", 1, 2, 16.f, WX_RAIN, BK_TWIN_A, "bc", 14, {ET_RIFLEMAN, ET_SNIPER, ET_GRENADIER, ET_SCOUT}, false, 360.f, 140,
     {{SPK_KADE, "MIRE HOLLOW", "Three radio masts relay Helix traffic through the swamp. Cut them and we can listen in.", 1},
      {SPK_KADE, "THE TWINS", "Two snipers work this marsh. You'll see their aim before they shoot - a red line. Step off it.", 0},
      {SPK_KADE, "OBJECTIVES", "Hold E at each mast for five seconds to cut it. Then survive the twins.", 2}},
     {{OBJ_INTERACT, "123", 3, 5.f, EV_NONE, "Cut the radio masts (hold E)", "KADE: Three masts. Cut the wire on each."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Defeat the Bresenham Twins", "KADE: Laser sights on the ridge! Keep moving!"}},
     2, {SPK_ORTIZ, "...this is Dr. Ortiz. I'm held under the forge. Helix built VECTOR - an AI to run his war. I wrote its safety lock. Get me out."}},
    {"UNDERGROUND", "MISSION 3", 1, 0, 15.f, WX_ASH, BK_SEED_QUEEN, "b", 14, {ET_SHOTGUNNER, ET_MEDIC, ET_RIFLEMAN, ET_SCOUT}, false, 420.f, 160,
     {{SPK_ORTIZ, "EMBERFORGE", "The forge powers the whole grid. I'm at the power junction. They're breeding something down here.", 1},
      {SPK_KADE, "ESCORT", "Reach the junction, then bring Ortiz to the safe room. Keep her alive.", 0},
      {SPK_ORTIZ, "THE QUEEN", "The brood mother plants egg sacs - acid spreads from them. Kill the sacs and it dries up.", 2}},
     {{OBJ_REACH, "1", 1, 0, EV_NONE, "Reach the power junction", "ORTIZ: The junction is east of the lava. Hurry."},
      {OBJ_ESCORT, "2", 1, 0, EV_NONE, "Escort Dr. Ortiz to the safe room", "ORTIZ: I'm right behind you. Don't outrun me."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Destroy the Seed Queen", "ORTIZ: It's coming out of the vents! Shoot the egg sacs!"}},
     3, {SPK_ORTIZ, "VECTOR runs from three outposts in the snow. Take them and we can see where it really lives."}},
    // ── ACT II ─────────────────────────────────────────────────────────────────────
    {"HIGH GROUND", "MISSION 4", 2, 3, 11.f, WX_SNOW, BK_GUNSHIP, "g", 16, {ET_RIFLEMAN, ET_SNIPER, ET_DRONE, ET_SHIELD}, false, 480.f, 180,
     {{SPK_KADE, "FROSTLINE", "Three VECTOR relay outposts. Stand in each until it's ours, then hold the fort.", 1},
      {SPK_ORTIZ, "THE GUNSHIP", "VECTOR will send its gunship. It marks a line before a strafing run - get off it. Its missiles turn slowly.", 0},
      {SPK_KADE, "OBJECTIVES", "Capture three outposts. Defend the fort for sixty seconds. Bring down the gunship.", 2}},
     {{OBJ_HOLD, "123", 3, 8.f, EV_NONE, "Capture the relay outposts", "KADE: Stand in the ring until it's ours."},
      {OBJ_SURVIVE, "4", 1, 60.f, EV_WAVES, "Defend the fort", "KADE: They know. Hold the fort - sixty seconds."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Bring down the gunship", "ORTIZ: Gunship inbound!"}},
     3, {SPK_KADE, "The relays lead to a black site in the mountain. Helix's records will be there."}},
    {"BLACK SITE", "MISSION 5", 2, 5, 23.f, WX_NONE, BK_GUARD_HEAVY, "bc", 15, {ET_RIFLEMAN, ET_SHIELD, ET_MEDIC, ET_TURRET}, false, 480.f, 200,
     {{SPK_ORTIZ, "HELIX FACILITY", "The black site. Intel is sealed in hidden vaults - look for wall panels that don't match.", 1},
      {SPK_KADE, "THE GUARDS", "Two guards hold the armoury. The medic keeps the heavy standing. Drop the medic first.", 0},
      {SPK_KADE, "OBJECTIVES", "Recover both intel cases. Disable the two alarm panels. Defeat the guards.", 2}},
     {{OBJ_COLLECT, "t", 2, 0, EV_NONE, "Recover the intel (secret vaults)", "ORTIZ: The vaults are behind false panels. Press E on suspicious walls."},
      {OBJ_INTERACT, "12", 2, 4.f, EV_NONE, "Disable the alarm panels (hold E)", "KADE: Kill the alarms before they lock the site down."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Defeat the Twin Guards", "KADE: Armoury's waking up. Medic first!"}},
     3, {SPK_ORTIZ, "These files... VECTOR is learning. Helix means to sell the grid as a weapon. We have to reach the core."}},
    {"WHITEOUT", "MISSION 6", 2, 3, 2.f, WX_BLIZZARD, BK_MIRAGE, "y", 12, {ET_RIFLEMAN, ET_STALKER, ET_SNIPER, ET_SCOUT}, true, 420.f, 220,
     {{SPK_KADE, "FROSTLINE, NIGHT", "Blizzard cover. Cross the valley to extraction without raising the alarm. Use your flashlight [L] sparingly.", 1},
      {SPK_ORTIZ, "MIRAGE", "Something is out there. Cloaked. It shows as a shimmer - smoke, a flashbang or radar will light it up.", 0},
      {SPK_KADE, "OBJECTIVES", "Stay quiet. Find what's hunting you and kill it. Reach extraction.", 2}},
     {{OBJ_REACH, "y", 1, 0, EV_NONE, "Cross the valley quietly", "KADE: Stay low. Every alarm brings more of them."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Kill the Mirage", "ORTIZ: It's here. It's right next to you!"},
      {OBJ_REACH, "z", 1, 0, EV_NONE, "Reach extraction", "KADE: Extraction is in the south-east. Go."}},
     3, {SPK_HELIX, "You are persistent, Raster. Come down to the core, then. VECTOR would like to meet you."}},
    // ── ACT III ────────────────────────────────────────────────────────────────────
    {"CORE DESCENT", "MISSION 7", 3, 0, 21.f, WX_ASH, BK_ENGINEER, "c", 16, {ET_HEAVY, ET_GRENADIER, ET_SHIELD, ET_DRONE}, false, 540.f, 240,
     {{SPK_ORTIZ, "EMBERFORGE, NIGHT", "The core is under the forge. Fight up through the three levels to the control floor.", 1},
      {SPK_KADE, "THE ENGINEER", "He hides behind a shield fed by pylons. Kill the pylons and the shield drops.", 0},
      {SPK_KADE, "OBJECTIVES", "Reach the three checkpoints. Defeat the Engineer.", 2}},
     {{OBJ_REACH, "5", 1, 0, EV_WAVES, "Reach the lower checkpoint", "KADE: First level. Push through."},
      {OBJ_REACH, "6", 1, 0, EV_WAVES, "Climb to the mound", "KADE: Second level. They're dug in."},
      {OBJ_REACH, "7", 1, 0, EV_WAVES, "Reach the control floor", "KADE: Last climb."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Defeat the Engineer", "ORTIZ: Shield pylons around him - take them out!"}},
     4, {SPK_ORTIZ, "The core's route is open... but the founder has moved VECTOR to the city. He's dug in at the plaza."}},
    {"SIEGE", "MISSION 8", 3, 4, 17.f, WX_NONE, BK_JUGGERNAUT, "b", 18, {ET_RIFLEMAN, ET_HEAVY, ET_GRENADIER, ET_MEDIC}, false, 600.f, 260,
     {{SPK_KADE, "ASHEN CITY", "The plaza is the last ground between us and the facility. They'll come in waves.", 1},
      {SPK_KADE, "THE GATE", "Hold the plaza, then plant a charge on the main gate. What's behind it walks on legs.", 0},
      {SPK_ORTIZ, "JUGGERNAUT", "It's armoured in front. Get behind it. Its minigun spins up first - you'll see the glow.", 2}},
     {{OBJ_SURVIVE, "1", 1, 75.f, EV_WAVES, "Hold the plaza", "KADE: Here they come. Hold the plaza!"},
      {OBJ_INTERACT, "2", 1, 4.f, EV_OPEN_GATE, "Plant a charge on the main gate (hold E)", "KADE: Gate's yours. Plant the charge."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Destroy the Juggernaut", "KADE: Gate's down - something big is coming through!"}},
     3, {SPK_HELIX, "Enough. Come to the reactor hall, Raster. Let's end this properly."}},
    // ── FINALE ─────────────────────────────────────────────────────────────────────
    {"ASHFALL", "FINALE", 4, 5, 0.5f, WX_NONE, BK_FOUNDER, "f", 14, {ET_RIFLEMAN, ET_SHIELD, ET_HEAVY, ET_STALKER}, false, 600.f, 400,
     {{SPK_HELIX, "THE REACTOR HALL", "VECTOR and I are one now. A mind that never sleeps, wired into every light in the valley.", 1},
      {SPK_ORTIZ, "HOW TO WIN", "First the mech: break its shield pylons. Then jump the laser walls. At the end the floor overloads - watch the tiles, and remember only the real one casts a shadow.", 0},
      {SPK_KADE, "LAST ORDERS", "Reach the reactor hall. Shut him down. Then come home, Raster.", 2}},
     {{OBJ_REACH, "f", 1, 0, EV_NONE, "Enter the reactor hall", "KADE: Reactor hall's in the centre. Go."},
      {OBJ_BOSS, "", 0, 0, EV_SPAWN_BOSS, "Defeat VECTOR", "HELIX: Welcome, Raster. Let me show you what VECTOR can do."}},
     2, {SPK_ORTIZ, "VECTOR is offline. The lights are coming back on across the valley... Raster, you did it. Come home."}},
};
