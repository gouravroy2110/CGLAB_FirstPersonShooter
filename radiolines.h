// =============================================================================
//  radiolines.h  -  The radio commander's script
//
//  A fixed set of spoken lines for situations in a fight (no AI writing them):
//  each situation has a priority, a cooldown and a few variants that rotate so
//  the same line never plays twice in a row. {boss} {hazard} {name} are filled
//  in from the game when the line is chosen. Speakers are the campaign cast
//  (missions.h): 0 Major Kade, 1 Dr. Ortiz, 2 VECTOR, 3 Aurelio Helix, 4 Raster.
// =============================================================================
#pragma once

enum RadioEvent {
    RE_LOW_HEALTH, RE_HAZARD, RE_LOW_AMMO, RE_HIT_BEHIND, RE_FLANKED, RE_GRENADE, RE_SNIPER,
    RE_STREAK, RE_LONG_SHOT, RE_SQUAD_DOWN, RE_TWO_LEFT, RE_LAST_ONE, RE_BOSS_APPEAR, RE_BOSS_PHASE,
    RE_SECRET, RE_IDLE, RE_STEALTH, RE_ZONE_START, RE_ZONE_CLEAR, RE_RESPAWN,
    RE_COUNT
};

// priority: 3 interrupts anything, 2 waits for the channel, 1 is chatter (needs a quiet gap)
struct RadioEventDef { int priority; float cooldown; };
static const RadioEventDef RADIO_EVENTS[RE_COUNT] = {
    {3, 30.f},   // RE_LOW_HEALTH
    {3, 25.f},   // RE_HAZARD
    {2, 40.f},   // RE_LOW_AMMO
    {3, 20.f},   // RE_HIT_BEHIND
    {2, 35.f},   // RE_FLANKED
    {3, 8.f},    // RE_GRENADE
    {2, 45.f},   // RE_SNIPER
    {1, 35.f},   // RE_STREAK
    {1, 30.f},   // RE_LONG_SHOT
    {2, 10.f},   // RE_SQUAD_DOWN
    {2, 60.f},   // RE_TWO_LEFT
    {2, 60.f},   // RE_LAST_ONE
    {2, 30.f},   // RE_BOSS_APPEAR
    {2, 12.f},   // RE_BOSS_PHASE
    {1, 90.f},   // RE_SECRET
    {1, 60.f},   // RE_IDLE
    {2, 30.f},   // RE_STEALTH
    {1, 20.f},   // RE_ZONE_START
    {1, 20.f},   // RE_ZONE_CLEAR
    {1, 10.f},   // RE_RESPAWN
};

struct RadioScriptLine { int event; int speaker; const char *text; };
static const RadioScriptLine RADIO_SCRIPT[] = {
    { RE_LOW_HEALTH, 0, "You're bleeding out. Find cover and patch up!" },
    { RE_LOW_HEALTH, 0, "You're hit bad, soldier. Get your head down!" },
    { RE_LOW_HEALTH, 1, "Your vitals are crashing. Pull back and heal!" },
    { RE_LOW_HEALTH, 0, "Don't be a hero. Break contact and recover!" },

    { RE_HAZARD, 1, "Get out of the {hazard}, now!" },
    { RE_HAZARD, 0, "You're standing in {hazard}! Move!" },
    { RE_HAZARD, 1, "That {hazard} will eat through your boots. Jump clear!" },

    { RE_LOW_AMMO, 0, "You're running dry. Grab ammo off the bodies." },
    { RE_LOW_AMMO, 0, "Last mag. Make every round count." },
    { RE_LOW_AMMO, 1, "Ammo's critical. Swap to your sidearm if you have to." },

    { RE_HIT_BEHIND, 0, "Contact rear! Check your six!" },
    { RE_HIT_BEHIND, 0, "They're behind you! Turn around!" },
    { RE_HIT_BEHIND, 1, "Fire from your back, soldier!" },

    { RE_FLANKED, 0, "They're moving around your flank!" },
    { RE_FLANKED, 0, "Watch your sides, they're circling you." },
    { RE_FLANKED, 1, "Movement on your flank. They're trying to box you in." },

    { RE_GRENADE, 0, "Grenade! Move!" },
    { RE_GRENADE, 0, "Frag out at your feet! Get clear!" },
    { RE_GRENADE, 1, "Incoming grenade!" },

    { RE_SNIPER, 0, "Sniper! Break line of sight!" },
    { RE_SNIPER, 1, "I'm seeing a scope glint. Stay off the open ground." },
    { RE_SNIPER, 0, "Marksman on the high ground. Use cover!" },

    { RE_STREAK, 0, "Good work. Keep the pressure on." },
    { RE_STREAK, 0, "That's how it's done, soldier." },
    { RE_STREAK, 1, "They're falling apart out there. Keep going!" },

    { RE_LONG_SHOT, 0, "Hell of a shot." },
    { RE_LONG_SHOT, 1, "Clean hit at that range? Impressive." },
    { RE_LONG_SHOT, 0, "Long range headshot. Textbook." },

    { RE_SQUAD_DOWN, 0, "We've lost {name}! Hold together!" },
    { RE_SQUAD_DOWN, 1, "{name} is down! Cover the squad!" },
    { RE_SQUAD_DOWN, 0, "{name}'s hit! Don't let them push!" },

    { RE_TWO_LEFT, 0, "Two hostiles left. Finish it." },
    { RE_TWO_LEFT, 1, "Only a couple of signals left on my screen." },
    { RE_LAST_ONE, 0, "One more. Hunt them down." },
    { RE_LAST_ONE, 1, "Last contact. I'm marking him for you." },

    { RE_BOSS_APPEAR, 0, "Heads up, {boss} is on the field!" },
    { RE_BOSS_APPEAR, 1, "That's {boss}. Watch for the wind up and dodge." },
    { RE_BOSS_APPEAR, 2, "Meet {boss}. My finest work. Try to keep up." },

    { RE_BOSS_PHASE, 0, "{boss} is changing tactics. Stay sharp!" },
    { RE_BOSS_PHASE, 1, "{boss} is adapting. Watch the attack pattern." },
    { RE_BOSS_PHASE, 2, "You think that hurt? {boss} is only getting started." },
    { RE_BOSS_PHASE, 0, "It's weakening! Keep pouring it on!" },

    { RE_SECRET, 1, "Hear that wind? Something is behind those walls." },
    { RE_SECRET, 1, "My scanner shows a hollow in the wall near you." },
    { RE_SECRET, 0, "There's a draft in there. Check the walls." },

    { RE_IDLE, 0, "Clock's ticking, soldier. Head for the marker." },
    { RE_IDLE, 0, "We can't hold this position forever. Move." },
    { RE_IDLE, 1, "The objective is still waiting. Follow the marker." },

    { RE_STEALTH, 0, "Easy. They're getting suspicious." },
    { RE_STEALTH, 1, "Their alert level is rising. Find a shadow." },
    { RE_STEALTH, 0, "Slow down. One more mistake and they sound the alarm." },

    { RE_ZONE_START, 0, "New zone. Clear it out and stay alive." },
    { RE_ZONE_START, 0, "Boots on the ground. Sweep the area." },
    { RE_ZONE_START, 1, "Hostiles everywhere in this sector. Careful." },

    { RE_ZONE_CLEAR, 0, "Zone clear. Good work." },
    { RE_ZONE_CLEAR, 0, "Area secure. Resupply and get ready." },
    { RE_ZONE_CLEAR, 1, "That's the last of them. Breathe for a moment." },

    { RE_RESPAWN, 0, "Back in the fight. Your team needs you." },
    { RE_RESPAWN, 0, "Redeployed. Push with your squad." },
};
static const int RADIO_SCRIPT_COUNT = int(sizeof(RADIO_SCRIPT) / sizeof(RADIO_SCRIPT[0]));
