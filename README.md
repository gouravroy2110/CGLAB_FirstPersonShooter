# CGLAB First Person Shooter
### A tactical FPS built entirely in Qt using pure software rasterization

> **No OpenGL. No GPU. Every single pixel is computed by the CPU and written into a `QImage` framebuffer.**
> No image files are loaded either — every texture, sprite, gun and portrait is generated procedurally at start-up.
> This is a Computer Graphics lab group project demonstrating real-time raster algorithms inside an interactive game.

---

## Latest update: 3D enemies, shadows, crouch & lean, smarter squads, armoury, stats

| Area | What changed |
|---|---|
| **3D enemies** | Regular hostiles are now real 3D soldiers built from smooth-shaded (Gouraud) tubes and ellipsoids on a shared skeleton (`soldier3d.h`): IK arms and legs, per-type kit (riot shield, grenade bandolier, medic cross and bag, heavy armour plates, sniper boonie hat, stalker hood). Hits are a ray test against capsules on the same skeleton, so you hit exactly what you see |
| **Character detail** | Close soldiers use a high-quality path: per-pixel procedural materials (camouflage, fabric weave, gunmetal, skin, rubber) solid-textured from body-local coordinates so they stay glued to the moving body; sky/ground hemispheric ambient + sun + rim + per-material specular; rounded-box (superellipse) plates, pouches and boots; sculpted chest, hands with fingers on the grip, a face under the balaclava, helmet cover / rails / ear cups / NVG mount / chin strap; feet planted on slopes by IK, breathing and weight shift; edge anti-aliasing on model silhouettes. Tessellation follows each part's size on screen |
| **Hit reactions** | The body part you hit flashes red; a headshot kill knocks the helmet off, which tumbles and bounces; bodies buckle at the knees and fall away from the shot with arms flung out |
| **Shadows** | Planar projected shadows for every soldier (including your own body, even in first person): each limb's shadow is a ground quad following the terrain, stencilled so overlaps darken once, strength from the sun and shade |
| **Crouch & lean** | Hold **Ctrl** to crouch (half speed, steadier aim, smaller target, hides behind low walls); hold **Alt + A / D** to lean around corners. Both rebindable |
| **Squad tactics** | Enemies take cover (low walls: duck and pop up; full walls: step out to a peek spot), flank to your side, and route around walls with a grid search |
| **Armoury** | Each campaign mission unlocks weapons and attachment options; an S rank makes that mission's guns RARE; guns you carry out of a mission join the armoury. New **LOADOUT** screen ([L] in the campaign menu) to pick guns per slot and fit attachments |
| **Stats** | New **STATS** screen: kills, headshots, accuracy, K/D, streaks, longest kill, bosses, secrets, time played, campaign ranks, per-weapon kills and accuracy, favourite gun on a turntable |

## Story campaign, six distinct worlds, bosses, map editor

| Area | What changed |
|---|---|
| **Campaign: Operation Ashfall** | 9 story missions in 3 acts + finale. Each has a **comic-panel briefing** (live 3D flyover of the map, pixel portrait, tactical map, typed-out text), an **intro flyby** along a Catmull-Rom spline, scripted objectives (kill, reach, collect key cards / intel, **hold E** to interact, capture zones, survive waves, **escort Dr. Ortiz**, **stealth with an alarm meter**, boss), **radio messages** with portraits, and a **debrief** with an S/A/B/C rank and coins. Progress, ranks, coins and your kit are saved between runs |
| **Permanent upgrades** | Coins buy body armour, max health, stamina and faster reloads in the campaign menu |
| **Bosses** | 9 story bosses (Warden, Bresenham Twins, Seed Queen, Gunship, Black Site guards, Mirage, Engineer, Juggernaut, **VECTOR**): telegraphed attacks, phases at 66 % / 33 %, shield pylons, weak points, decoys, laser walls, overload floors, spreading acid. Free play uses them as zone commanders |
| **New hostiles** | Grenadier, riot Shield (blocks rounds), Medic (revives the fallen), Heavy, cloaked Stalker (refraction shimmer), flying Drone, Turret |
| **Six different worlds** | The three old maps are re-themed and two new ones are added: **Emberforge** (volcanic, lava), **Dune Outpost** (desert), **Mire Hollow** (swamp, trees, bog), **Frostline** (snow, pines), **Ashen City** (ruins 64×64), **Helix Facility** (labs 56×56, coolant). Each has its own textures, sky, hazard and default weather |
| **Weather & time of day** | Rain, snow, sandstorm, ash and blizzard particles, lightning; sun and ambient light baked separately and re-mixed for any hour, stars at night, **flashlight (L)** |
| **Map editor** | MAP EDITOR in the main menu: paint walls / floor / hazards / markers with a pencil or **seed fill**, flood-fill validation (start present, every marker reachable), save over a map, **F5 to play-test** |

## Engine groundwork, third person, full arsenal, new UI

| Area | What changed |
|---|---|
| **Performance** | The terrain ray-march runs on **all CPU cores**: screen columns are independent, so they are split into chunks and marched in parallel (`parallel.h`, QtConcurrent). Terrain 8.9 ms → 1.6 ms, whole frame 15.7 ms → ~6.5 ms |
| **Engine structure** | The map model is now a standalone **`World`** class (no Qt) — grid, heightfield, doors, light map, decals, collision, line of sight. One **`stepMovement()`** moves every body from an **`InputCmd`**, so the player, squad bots and (later) network players share the same physics |
| **Bigger maps** | Map size is set at load time (no more fixed 40×40). New **FRONTIER** map, **64×64**: village, canyon, acid marsh with stepping stones, hill plateau, central fort with a watchtower, two secret vaults |
| **Third person** | **V** switches first ↔ third person. Over-the-shoulder camera on a boom that **pulls in when a wall is behind you** (ray-marched against the heightfield), **X** swaps shoulder, aiming pulls the camera closer. Shots leave the **gun muzzle** and converge on the crosshair; a wall between gun and target stops the round. Your body is a **3D soldier model** |
| **Squad bots** | 0–3 AI teammates (Settings) that follow you in formation, pick targets, strafe and fire; hostiles target whoever is nearest and visible. Name tags, health bars and blue minimap dots |
| **Arsenal** | **25 weapons** in one data table, **5 throwables**, **5 gadgets**, **6 attachment slots**, **5 rarity tiers** — see *Arsenal* below |
| **UI** | Large text smoothed with **Scale2x / Scale3x** pixel-art upscaling; new main menu (New Game · Arsenal · Settings · Firing Range · Exit); **Arsenal** screen with a rotating 3D model of every gun and stat bars; **Settings** (sensitivity, field of view, volume, camera, free aim, squad, crosshair colour, FPS) saved between runs; damage arcs that keep pointing at the attacker while you turn; **kill cam** replaying your death from the killer's eyes |

## Previous version

| Area | Before | Now |
|---|---|---|
| **Terrain** | Flat checkerboard floor | Heightfield terrain: rolling hills, steps, ledges, pits and acid pools, rendered by a per-column ray-march with a y-buffer and a full per-pixel z-buffer |
| **Movement** | Flat walking, cosmetic jump | Gravity, real jumping and landing, 0.32-unit step-up limit (ledges need a jump), falling into pits, acid damage |
| **Maps & Spawning** | 24×24, static spawns | Three hand-authored **40×40** maps; **50/50 Hybrid Spawning** (fixed tactical designer positions + dynamic safe floor spawns >8 units away); doors sink into the floor; **secret walls** with procedurally engraved **runic sigils** and proximity HUD prompt (`[E] INSPECT WALL`); diverse vault guards |
| **Bullet impacts** | Nothing on walls | **Bullet holes** stamped on wall and cliff faces (hole, burnt rim, chipped ring, cracks); shotgun leaves a spread pattern; scorch marks and blood on the ground |
| **Aiming** | Crosshair fixed at the centre | **Free-aim crosshair** moving inside a dead-zone; pushing past the edge turns or tilts the camera (V toggles classic) |
| **Damage** | Every hit = 1 HP | **Pixel-perfect hitscan** against head / body / limb masks; **headshots ×2–3**, limbs reduced; damage numbers, headshot banner and markers |
| **Weapons & Melee** | Single flat pistol | **Combat Knife** (slot 1, tactical slash/stab, high headshot crit); **M870 Shotgun overhauled** (pellet damage 15→24, pump delay 0.85s→0.58s, tighter spread, 18m range falloff); M4 Carbine starts with Reflex Sight and upgrades to ACOG 3× Optical Scope |
| **Attachments & Upgrades** | Fixed static weapon stats | **Modular Attachment System**: Extended Mags (+50–66%), Recoil Compensators (-48% kick, -42% spread), High-Caliber AP Ammo (+42–45% dmg), Hair Trigger / Rapid Pump / Stiletto Edge; **Field Armory modal (`[U]`)** and active HUD badges |
| **Zone Clear Supply Drops** | 3s instant transition | **Tactical Supply Drop 3-Card Draft**: 30-second decision timer to inspect and pick cards, prominent clickable **[ CONTINUE / DEPLOY ]** button, and 3-second deployment countdown with instant deploy shortcuts (`Space`). Reserve ammo refilled for **all owned weapons** |
| **Tactical Enemy AI** | Oscillating sideways jitter | **Smart Stop-and-Shoot AI**: hostiles halt to stabilize aim when ready to fire (`fireCooldown ≤ 0.35s`), engage targets up to realistic weapon ranges (up to 22 units for Snipers) |
| **Enemy Archetypes** | Uniform troopers | **4 distinct hostile archetypes**: Scouts (rapid pistols, high agility), Riflemen (burst fire), Shotgunners (heavy armor/HP, 5-pellet blasts), Snipers (elevated perches, extreme range) |
| **Optics** | FOV squeeze | **ACOG 3× Optical Scope** (figure-8 breathing sway, hold breath) plus Reflex Red Dot and Holographic sights |
| **Lighting & sky** | Linear fog to black | Terrain-aware sun shadows, slope lighting, ambient occlusion, per-face wall lighting, haze fog, angular sky with sun bloom and lens flare; three themes (overcast, midday, dusk) |
| **Textures** | 64² procedural | 128²/256² procedural textures with mip-maps (cobblestone, grass/dirt, flagstones, stone, brick, concrete, doors, secret walls) |
| **HUD & menus** | Basic panels | Compass with enemy pips, rotating minimap, slanted health/ammo panels, kill feed, target panel, secrets counter; menus show the map live in 3D with mouse support |
| **Raster purity** | Text drawn with QPainter vector fonts | **Bitmap pixel font** for all text; own nearest-neighbour upscaling; QPainter only copies the finished image 1:1 |
| **Raster proof** | Mouse wheel resized the window | **Mouse-wheel magnifier** up to 32× around the cursor/crosshair: each framebuffer pixel becomes a box with a pixel grid and a `PIXEL (x,y) = #RRGGBB` readout; `[` `]` pixel-block mode kept |

---

## Controls

| Key | Action |
|---|---|
| **W / S / A / D** | Move / strafe |
| **Shift** | Sprint (drains stamina) — or **hold breath** while scoped |
| **Mouse** | **Free-aim crosshair** — moves inside a dead-zone; pushing past its edge turns / tilts the camera |
| **LMB / F** | Fire · hold for automatics, bursts, beams and **charge weapons** (release to fire) · hold the revolver from the hip to **fan-fire** |
| **RMB / Tab** | Aim down sights / scope (aim speed depends on the gun) |
| **Mouse wheel** | Scope magnification while scoped · otherwise the **raster magnifier** 1×→32× |
| **1 / 2 / 3 / 4** | Melee · sidearm · primary · second primary |
| **Q** | **Quick melee** without switching |
| **R** | Reload (shotguns load **one shell at a time** — fire to stop) · resume (paused) · redeploy (game over) |
| **G** | Throw — **hold to cook** a frag, a dotted arc shows where it lands · **T** cycles throwable |
| **Z** | Use gadget (turret, medkit station, radar, grapple, jetpack) |
| **E** | Open door · search walls · **pick up / swap** the gun under you |
| **H** | **Inspect** the weapon (rarity and stats) |
| **I** | **Weapon stats** — toggle tactical stat box above weapon panel |
| **V / X** | First ↔ third person · swap shoulder |
| **U** | **Field Armory** — your 4 slots, rarity, attachments, throwables, gadget |
| **B** | Free-aim ↔ classic centred crosshair |
| **C** | Re-centre crosshair and view |
| **Space** | Jump — onto ledges, out of pits, across acid |
| **N** | Sound on / off |
| **Ctrl** | Crouch (hold) |
| **Alt + A / D** | Lean left / right (hold) |
| **L** | Flashlight |
| **← / →** | Turn |
| **[ / ]** | Pixel-block size 1–8 (raster proof) |
| **ESC** | Pause (ESC again quits) · **M** main menu · **Q** quit |

Menus work with keyboard (arrows / 1-5 / Enter / Esc / Space) **and mouse** (hover + click, right-click = back).

---

## What's in the game

### Arsenal
Every gun is one row of a data table (`weapons.cpp`): damage, fire rate, magazine, reload style, recoil pattern, damage falloff, projectile type, splash, penetration, status effect, heat, charge time, mobility, sound and which 3D model to draw. You carry **4 slots** (melee, sidearm, two primaries). A full slot means **[E]** swaps — the old gun is dropped with its ammo and attachments.

| Class | Weapons | What makes them different |
|---|---|---|
| Pistols | M9, **.44 Magnum revolver**, **MP-9 machine pistol** | Revolver hammer cocks between shots, **fan-fire** from the hip; machine pistol full auto |
| SMG | **Vector SMG**, **P90 PDW** | High fire rate, low recoil, **fast aim-down-sights** |
| Rifles | M4, **M16 burst**, **SCAR-H battle rifle** (semi, heavy), **AUG bullpup** (built-in optic) | Own **recoil pattern** each (vertical, S-curve, right drift, heavy); rounds **punch through doors** |
| Shotguns | M870 pump, **AA-12 auto**, **sawn-off double barrel** | Pellets, range falloff, **shell-by-shell reload** you can interrupt |
| LMG | **M249 SAW** | 100-round belt, long reload, **bipod**: aim while still against cover → recoil ×0.35 |
| Sniper / DMR | **AWM bolt-action**, **MK14 DMR** | Full-screen scope, hold breath, **ballistic bullet with drop** (sniper), bolt animation |
| Launchers | **RPG-7**, **M32 grenade launcher** | Rocket with smoke trail; **bouncing timed grenades**; explosions with radial damage, line of sight and **craters** (the heightfield is dented and the light map re-baked) |
| Energy | **Plasma rifle**, **Arc gun**, **Railgun**, **Flamethrower** | Charged plasma bolts + **heat / overheat**; **chain lightning** jumping between hostiles (shock = slow); railgun **pierces every wall and enemy** on the line; flame cone that **sets hostiles on fire** |
| Melee | Knife, **machete**, **stun baton**, **fire axe** | Machete hits several in an arc, baton **stuns**, axe **chops doors open**; hitting an unaware hostile is a **silent takedown** |

- **Throwables** (G, hold to cook, T cycles): **frag**, **smoke** (blocks hostile sight lines and greys out your view inside it), **flashbang** (stuns hostiles, white-out + ear ringing for you), **molotov** (burning ground), **claymore mine**. Throws land where you aim (the lob is solved for the crosshair point) and bounce on the heightfield.
- **Gadgets** (Z, with cooldowns): **sentry turret**, **medkit station**, **radar ping** (hostiles marked through walls), **grappling hook** (pulls you onto a ledge), **jetpack burst**.
- **Attachments** — optics (red dot, holo, 2×, 4×, 8×), muzzle (suppressor: quiet and doesn't alert distant hostiles / compensator), extended magazine, underbarrel (foregrip / **laser sight** with a visible beam), tactical stock, ammo (armour-piercing, incendiary, hollow-point). They are real 3D parts on the gun.
- **Rarity** — common, uncommon, rare, epic, legendary: more damage, less recoil, bigger magazines, coloured name and loot beam.
- **Feel** — per-gun recoil patterns and camera kick, **bloom** (spread grows with sustained fire), sprint-to-fire delay, dry-fire click, reload cancel, tracers, brass and shells.
- **Firing range** (main menu): every gun and throwable laid out on the floor to try.

### Combat feedback
- **Pixel-perfect hitscan.** A shot resolves exactly what is drawn under the crosshair pixel: walls (with their real heights), the floor, and enemy sprites tested **texel by texel** against a `HEAD / BODY / LIMB` mask. Headshots ×2–3, limbs reduced.
- Crosshair turns red over a hostile (ring over a head), target panel, floating damage numbers, hit / headshot / kill markers, kill feed, tracers, sparks, scorch marks and blood decals.
- **Damage arcs** remember where each hit came from and keep pointing at the attacker while you turn (up to six at once).
- **Kill cam**: when you die, the last 2.5 seconds replay at 60% speed from the shooter's eyes, with your body drawn as the 3D soldier. Any key skips it.

### Zone objectives, Commander boss & loot
- **A zone is cleared only when every enemy on the map is dead** — patrols, vault guards behind secret walls, reinforcements and the Commander. The HUD shows *HOSTILES REMAINING*; when 3 or fewer are left they are marked on the minimap and compass, and if the last ones are hidden in a vault the HUD says to search the walls.
- Reinforcements are limited per zone (none in zone 1, up to 5 later), so a zone can always be finished.
- Every zone has a **Commander** boss: a larger (1.35×) elite with ~3.6× health, its own health bar and a +1000 score bounty.
- **Bosses and elite guards always drop a gun** (one you don't carry yet when possible), rolled at a higher tier and rarity — the Commander's drop is at least **epic** — marked by a coloured light beam; the Commander also drops a medkit, ammo and a frag. Ordinary hostiles sometimes drop ammo, throwables or a gun suited to the zone.

### Zone report, coins & field shop
- Clearing a zone first shows a **ZONE REPORT** checklist that ticks in row by row and pays **coins** (and bonus score):

| Row | Reward |
|---|---|
| Hostiles eliminated | 5 coins per kill |
| Headshots | 10 coins per headshot |
| Headhunter — at least 40% of kills were headshots | 40 coins, 400 × zone pts |
| Marksman — accuracy at least 35% | 30 coins, 300 × zone pts |
| Explorer — every secret area in the zone found | 50 coins, 500 × zone pts |
| Untouchable — took under 40 damage | 40 coins, 400 × zone pts |
| Blitz — cleared within par time | 30 coins, 300 × zone pts |
| Commander down | 60 coins, 600 × zone pts |

- Coins are shown in the HUD and spent in the **field shop** on the upgrade screen: `[7]` ammo resupply + a frag (40), `[8]` medkit +50 HP (35), `[9]` reroll the upgrade cards (25).

### Zone Clear: upgrade + loadout screen
- After the report: **1 choose one of 3 upgrade cards → 2 choose the weapon to deploy with → 3 deploy**.
- Cards: a **new gun** for your zone (with a rarity roll), an **attachment** for a gun you carry, a **rarity refinement**, a **munitions crate** (throwables) or a **new gadget**. Picking a gun card also selects that gun in the *Deploy with* row, and **you start the next zone holding the chosen gun, loaded**.
- Pick a card with `1` `2` `3` or a click (you can change your mind), choose the weapon with `TAB` / arrows or a click, then press `ENTER` or click **DEPLOY**. A 60-second fallback timer only moves an idle game on.
- The mouse is released on this screen, and input from the fight that cleared the zone is ignored for the first second, so a stray shot, jump or number key can't pick a card or skip the screen.

### 3D weapons, soldiers and props
- **Third person** shows your soldier as a 3D model (boxes for limbs, torso, vest, pack, helmet + the real gun model), walking and aiming with your crosshair; squad bots, weapon pickups, turrets, medkit stations, rockets, grenades and mines are 3D too — all drawn by the same triangle rasterizer (`raster3d.h`) against the world z-buffer, lit by the sun with the light-map shadow and fogged like the terrain.
- Guns, the knife and the gloved hands are **real 3D models**: each side-profile polygon is extruded to its own thickness (barrel, receiver, scope, pump, grips…) and rendered with perspective, **back-face culling**, **Sutherland–Hodgman near-plane clipping**, flat **Lambert + Blinn specular** lighting and a **per-pixel z-buffer**. Forearms are 3D boxes.
- Recoil, reload, sprint, swap, slide/pump and knife stab/slash animations are now 3D rotations (yaw / pitch / roll about the gun's own axes).

### Sound
- **Every sound is synthesised in code at start-up** — no audio files, the same way no image files are used. Oscillators, filtered noise, envelopes, formant filters and a small room reverb build **83 effects** (`audio.cpp`) — each new weapon has its own report (revolver, SMG, LMG, sniper with echo, DMR, double barrel, suppressed shots, rocket launch, grenade thump, explosion, plasma, arc crackle, railgun charge + crack, flamethrower), plus grenade pin and bounce, flashbang ear-ring, smoke hiss, fire, turret, radar ping, jetpack, grapple, takedown and inspect.
- A real-time software mixer (`SoundEngine`, pulled by `QAudioSink`) plays overlapping voices with per-voice volume, stereo pan, pitch and delay; a soft clipper prevents distortion.
- **Positional 3D audio**: enemy gunfire, impacts, doors and death cries are panned left/right by direction, fade with distance and are muffled behind walls.
- **Distinct sounds for each feature**:

| Feature | Sounds |
|---|---|
| Your weapons | pistol / shotgun / carbine reports, knife swing + hit, dry-fire click, brass and shell tinkles |
| Handling | magazine out / in, slide & bolt rack, shotgun shell loading and pump, weapon switch, scope-in, scope zoom ticks |
| Enemies | rifleman, scout pistol, shotgunner and sniper (with echo) gunfire, alert call-out, death cry, near-miss bullet whiz |
| Hits | flesh hit, hit-marker tick, helmet "ping" for headshots, kill confirm, wall impacts and ricochets |
| Player | footsteps (faster when sprinting, quiet when aiming), jump, landing thud, pain grunts, acid sizzle, low-health heartbeat |
| World | stone door grinding, secret-area chime, pickups (ammo, health, weapon, intel fanfare), radio chatter for reinforcements and supply drops |
| Flow | zone-clear fanfare, upgrade select, deploy, game-over sting, menu move / select / back blips |
| Ambience | looping wind (Arena), cave drone with water drips (Dungeon), low dusk drone (Crypt) |

### Tactical Enemy AI & Hostile Archetypes
- **Smart Stop-and-Shoot AI**: Hostiles halt movement when ready to fire (`fireCooldown ≤ 0.35s`), take aim, and engage the operative up to true weapon ranges (up to 22 units), completely replacing erratic sideways jitter with grounded tactical gunplay.
- **Four Distinct Hostile Archetypes**:
  - **Scouts**: Fast, light skirmishers with high mobility and rapid pistol fire.
  - **Riflemen**: Versatile mid-range troopers firing automatic bursts.
  - **Shotgunners**: Heavy close-quarters breachers (+35% HP) firing devastating 5-pellet shotgun cones.
  - **Snipers**: Stationed on elevated ledges and towers with extreme range (22 units) and pinpoint accuracy.
- **Infinite Survival Scaling**: Enemy health, armor, count, and kill quotas scale progressively each zone; out-of-sight reinforcement waves deploy in later zones.

### Tactical Spawning & Secret Vaults
- **50/50 Hybrid Spawning**: 50% fixed spawns on hand-authored chokepoints and elevated nests (`'e'`), plus 50% dynamic spawns placed on safe floor tiles (>8 units away from player spawn).
- **Procedural Secret Sigils**: Secret walls feature an engraved, weathered **runic sigil** carved into the rock face.
- **Interactive Proximity HUD Prompt**: Walking up to a secret wall displays `[E] INSPECT WALL` on the HUD (and `[E] OPEN DOOR` for doors).
- **Varied Vault Guardians**: Secret vaults are guarded by diverse hostile combinations (Shotgunners, Riflemen, Snipers) protecting the intel cache.

### Realistic terrain & lighting
- Three themed maps: **Dungeon** (overcast, mossy stone, cobblestones), **Arena** (midday, grass/dirt with pebbles), **Crypt** (dusk, flagstones with cracks).
- **Sun shadows**: the ground light map ray-marches toward the sun in 3D against terrain and wall heights (long shadows at dusk), blurred into soft penumbras; opening a door re-bakes only the area around it. **Ambient occlusion** darkens ground near walls, at cliff bases and inside pits.
- Walls: per-face **Lambert lighting** vs the sun, true per-type heights (pillars are taller), mip-mapped 128² textures.
- Floor: perspective-correct floor casting with **mip-mapped** 256² textures (no far-distance shimmer), blood and scorch **decals**.
- **Exponential fog toward the theme's haze colour**, an **angular sky panorama** (stable under pitch and zoom), sun disc + bloom, **lens flare** when the sun is visible, cinematic vignette.

### HUD & menus
- Compass strip (alerted enemies show as red pips), objective counter, rotating **minimap** (walls, doors, enemies, pickups, view cone), slanted health / stamina panel, weapon panel with a rasterized gun icon, magazine pips, weapon slots and reload bar, kill feed, damage-direction arcs, low-health desaturation + pulsing red edges.
- Menus render the selected map **live in 3D** behind them, with soldiers standing around. Map cards show a top-down layout; difficulty and operative cards have stat bars.
- Pause and game-over screens (with kills, headshots, accuracy).

---

## Acceleration-Based Player Movement

Replaced instant movement with velocity-based acceleration and deceleration.

### Changes

- Added horizontal player velocity.
- Applied smooth ground acceleration and friction.
- Preserved movement momentum while airborne.
- Reduced air control, backward speed, and strafe speed.
- Prevented diagonal movement speed boosts.
- Stopped velocity on blocked collision axes.
- Reset player velocity when starting or loading a level.

### Result

Player movement now feels smoother, more natural, and more responsive while preserving the existing terrain, step-height, and collision systems.

## Raster compliance

Everything visible is written into `m_pixels[y*W + x]` by our own code:

| System | Method |
|---|---|
| Sky | Angular lookup into a baked fBm-noise panorama, above each column's terrain silhouette |
| Terrain + walls | Per-column heightfield ray-march with a y-buffer; exact per-row depth solve on each segment; mip-mapped textures, light-map + decal lookup |
| Bullet holes | Per-face decal textures stamped with discs + Bresenham cracks, sampled in the wall loop |
| Depth | Full per-pixel z-buffer written by the terrain pass |
| Sprites | Billboards standing on the terrain, per-pixel z-test, Bayer dither fade, contact shadows |
| Tracers | Projected 3D line, perspective-correct depth test per pixel |
| Weapon / hands | Edge-function triangle rasterizer with Gouraud shading |
| 3D soldiers, pickups, turrets | Same triangle rasterizer, world camera frame, depth-tested against the terrain z-buffer |
| Beams, plasma, flames | Jagged Bresenham polylines, additive glows, projected per frame |
| Scope / sights | Per-pixel radial masks, rectangles, rings, Bresenham lines |
| HUD / menus | Rect / gradient / triangle / ring / arc fills with alpha blending |
| Textures & sprites | Baked texel-by-texel at start-up (noise, Voronoi, shaded primitives) |
| Text | Own **bitmap pixel font** (`pixelfont.h`, hand-drawn 5×7 glyphs) blitted as pixel blocks — large text uses **Scale2x / Scale3x** upscaled copies; no vector fonts |
| Window scaling / zoom | Own nearest-neighbour upscale + pixel-grid overlay (`composeDisplay`) |

**No vector graphics are used anywhere.** No QPainter shapes, paths, pens, brushes or fonts, no OpenGL. Polygons (guns, hands, HUD shapes) are vertex lists filled by our own triangle rasterizer. Qt's only role is copying the finished image to the window **1:1**.

**Showing the raster grid:** scroll the mouse wheel. The magnifier zooms 2×, 4×, 8×, 16× or 32× around the cursor or crosshair; from 8× up each framebuffer pixel is drawn as a box with grid lines, and the readout shows `PIXEL (x,y) = #RRGGBB`. `[` / `]` re-render the scene in 2×2…8×8 pixel blocks.

---

## Algorithms used (CG reference)

| System | Algorithm |
|---|---|
| Terrain | Voxel-space style column ray-march over a heightfield (DDA across cells + sub-steps), y-buffer occlusion |
| Slopes | Row → depth solved exactly for each linear segment: t = (K(e−h₁) − Y·d₁) / (Y·Δd + K·Δh) |
| Visibility | Per-pixel z-buffer |
| Walls | DDA cell stepping, perpendicular distance (fisheye fix), faces where the height jumps |
| Texture filtering | Box-filtered mip-maps, level from screen-space footprint |
| Lighting | Lambert (N·L) per wall face; 2D shadow ray-march vs wall height; distance-field AO; box-blur penumbra |
| Fog | Exponential `exp(-density·d)` toward haze colour |
| Sprites | Inverse camera-matrix projection, painter's sort, per-column z-buffer |
| Hit detection | Screen-space hitscan with per-texel hit mask |
| Polygons | Edge-function triangle rasterization, Gouraud colour interpolation, convex fan |
| 3D weapon | Extrusion of 2D profiles, camera-space basis + Rodrigues rotations, back-face culling, Sutherland–Hodgman clipping at the near plane, perspective projection, 1/z depth buffer, Lambert + Blinn shading |
| Lines | Bresenham; thick lines as quads |
| Circles / glows | Scan-span discs, radial falloff glows, ring / arc masks |
| Gun projection | One-point perspective (vanishing point on the crosshair) |
| Procedural textures | Value noise, fBm, tileable Voronoi (cobbles, pebbles) |
| Sprite painting | Analytic normals for spheres / capsules / boxes + Lambert |
| Sprite rotation | Inverse-mapped nearest-neighbour rotation (death frames) |
| Dissolve | 4×4 Bayer ordered dither |
| Blending | Source-over alpha and additive light |
| Parallel rendering | Screen columns of the terrain march split across CPU cores (QtConcurrent) |
| Large text | **Scale2x / EPX** and **Scale3x / AdvMAME3x** pixel-art upscaling of the glyph bitmaps |
| Third-person camera | Boom collision by marching the segment head → camera against the heightfield |
| Shot convergence | Crosshair ray from the camera finds the target; the round is traced muzzle → target point |
| Ballistics | Explicit Euler integration with gravity, sub-stepped against terrain and enemy cylinders |
| Grenade bounce | Reflection off wall faces / ground with restitution and rolling friction |
| Throw solve | Projectile motion solved for a landing point: v_z = (Δz + ½gt²)/t |
| Explosions / smoke | Radial falloff with line of sight; smoke = sphere-segment intersection on sight lines |
| Craters | Heightfield vertices lowered locally, light map re-baked for the region |
| Chain lightning | Nearest-neighbour hops with line-of-sight checks |
| Kill cam | Ring buffer of game-state snapshots replayed through the renderer from another camera |
| Campaign flyby | Catmull-Rom spline through the mission points, camera kept above the heightfield |
| Map editor fill | Stack-based 4-connected seed fill; BFS flood fill to validate reachability |
| Acid spread | BFS growth front over the grid, receding when its source dies |
| Time of day | Two light-map layers (ambient occlusion, sun) re-mixed per hour |

---

## Project structure

```
fps_game/
├── fps_game.pro        Qt project file
├── main.cpp            Entry point (also: --snapshot <dir> renders test scenes to PNG)
├── game.h              Maps, configs, entity structs (player, actors, enemies, projectiles)
├── weapons.h/.cpp      The arsenal tables: 25 weapons, throwables, gadgets, attachments, rarity
├── world.h             World class: grid, heightfield, doors, light map, decals, collision, LOS
├── sim.h/.cpp          InputCmd + stepMovement(): shared movement for every actor
├── parallel.h          Column-parallel helper for the terrain pass
├── raster3d.h          Shared 3D triangle rasterizer (view model + world models)
├── gunbuild.h/.cpp     Kit-bashed 3D profiles for the extended arsenal + attachment mounts
├── pixelfont.h         Bitmap 5x7 pixel font (glyph table, Scale2x/3x copies, blitter, word wrap)
├── raster.h            Canvas: pixel/blend/add, rects, gradients, circles, glows,
│                       rings/arcs, Bresenham, triangle rasterizer, polygons, noise
├── gunmodels.h         Weapon + glove polygons (view model, HUD icon, pickups)
├── assets.h/.cpp       Procedural sky, floors, walls (+mips), soldier painter,
│                       enemy animation atlas, pickup sprites, theme table
├── maps.h              ASCII layouts of the six maps (legend in the file header)
├── mainwindow.h/.cpp   Game loop, enemy AI, zones, input, free aim, third-person camera, tests
├── combat.cpp          Firing, projectiles, explosions, melee, throwables, gadgets, pickups
├── upgrades.cpp        Zone-clear upgrade cards
├── actors.cpp          Squad bots (AI teammates driven by InputCmd)
├── killcam.cpp         Death replay from the killer's view
├── missions.h          Campaign data: 9 missions, objectives, briefings, radio lines
├── campaign.cpp        Campaign menu, briefing, objectives, radio, flyby, debrief, save
├── bosses.cpp          Boss table and fights
├── enemies.cpp         New hostile types
├── environment.cpp     Weather, time of day, flashlight
├── editor.cpp          In-game map editor
├── stats.cpp           Lifetime stats and the STATS screen
├── soldier3d.h         Shared soldier skeleton: joints, IK, death fall, ray hit test
├── menus.cpp           Settings (saved) and the Arsenal browser
├── world.cpp           World methods + heightfield, light-map baking, doors & secrets, bullet-hole
│                       decals, 3D hitscan, line of sight, movement checks
├── render_world.cpp    Terrain + walls ray-march, sky, lens flare, sprites, tracers, particles
├── render_actors.cpp   3D soldiers, weapon pickups, turrets, medkit stations, projectiles
├── render_fx.cpp       Plasma, chain lightning, rail trail, radar marks, smoke, flash / blast light
├── render_weapon.cpp   First-person gun + hands, muzzle flash, casings, sights, scope
├── render_hud.cpp      Frame composition, post FX, HUD, menus, intermission, pause / game over
└── audio.h/.cpp        Procedural sound synthesis + real-time stereo mixer
```

---

## How to build

1. Open **Qt Creator** → **File → Open File or Project** → `fps_game.pro`
2. Configure with the **MinGW** kit (Qt 6+) → **▶ Run**

Or via CLI:
```powershell
$env:PATH = "C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
qmake fps_game.pro -spec win32-g++
mingw32-make -j4
```

Qt modules used: Widgets, Multimedia (sound) and Concurrent (multi-core terrain pass).

Run the automated checks (renders test scenes to PNG and writes `log.txt`):
```powershell
.\release\fps_game.exe --snapshot out_dir
```

---

## Team
Group project — Computer Graphics Lab, Semester 5
