# CGLAB First Person Shooter
### A tactical FPS built entirely in Qt using pure software rasterization

> **No OpenGL. No GPU. Every single pixel is computed by the CPU and written into a `QImage` framebuffer.**
> No image files are loaded either — every texture, sprite, gun and portrait is generated procedurally at start-up.
> This is a Computer Graphics lab group project demonstrating real-time raster algorithms inside an interactive game.

---

## What's new in this version

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
| **Mouse** | **Free-aim crosshair** — moves freely inside a dead-zone; pushing past its edge turns / tilts the camera |
| **LMB / F** | Fire / Slash (the carbine is fully automatic) |
| **RMB / Tab** | Aim down sights — reflex sight (pistol/carbine), holographic sight (shotgun), **ACOG 3× optical scope** (carbine with optics upgrade) |
| **Mouse wheel** | **Raster magnifier** 1×→32× around the cursor (menus) or crosshair (in game): every framebuffer pixel becomes a box, with a pixel grid and a coordinate/colour readout. |
| **R** | Reload · resume (paused) · redeploy (game over) |
| **1 / 2 / 3 / 4** | Knife / Pistol / Shotgun / Carbine (once picked up) |
| **U** | **Field Armory** — inspect weapon stats, attachment slots, and upgrade status |
| **9** | **Debug Weapon Range** — toggle all attachments across all weapons |
| **Space** | Jump — onto ledges, out of pits, across acid |
| **N** | Sound on / off |
| **E** | Open a door · **inspect a suspicious wall** for a secret passage |
| **V** | Toggle free-aim ↔ classic centred crosshair |
| **C** | Re-centre crosshair and view |
| **← / →** | Turn |
| **[ / ]** | Pixel-block size 1–8 (raster proof) |
| **ESC** | Pause (ESC again quits) · **M** main menu · **Q** quit |

Menus work with keyboard (arrows / 1-4 / Enter / Esc / Space) **and mouse** (hover + click, right-click = back).

---

## What's in the game

### Combat & Arsenal
- **Pixel-perfect hitscan.** A shot resolves exactly what is drawn under the crosshair pixel: walls (with their real heights — you can shoot over a wall top), the floor, and enemy sprites tested **texel by texel** against a per-texel hit mask.
- **Headshots & Criticals.** The enemy sprite atlas carries a mask of `HEAD / BODY / LIMB` per texel. Damage multipliers: head **×3.0** pistol, **×2.0** shotgun, **×2.5** carbine, **×3.5** knife criticals; limbs ×0.7–0.75. A pistol headshot one-taps a level-1 hostile on Easy/Medium.
- **Four weapons in arsenal**:
  - **Slot 1 — Combat Knife**: Fast tactical melee slashes and lunges (90 base damage, alternate slash/stab animations, stagger, 3.5× headshot multiplier).
  - **Slot 2 — M9 Tactical Pistol**: Accurate semi-automatic sidearm with fast handling and reflex red dot sight.
  - **Slot 3 — M870 Tactical Shotgun**: Overhauled heavy pump shotgun firing 8 high-velocity pellets (24 damage per pellet, up to 192 point-blank damage, snappy 0.58s pump cycle delay, extended 18m range falloff, and tight choke).
  - **Slot 4 — M4 Automatic Carbine**: Fully automatic assault rifle equipped standard with a Reflex Red Dot Sight, upgradable into a designated marksman rifle via the ACOG 3× Optical Scope attachment.
- **Full Inventory Ammo Replenishment**: Upon clearing any combat zone and advancing to the next tier, reserve ammunition is replenished for **all owned weapons** in the operative's loadout.
- Feedback: crosshair turns red (with a ring when over a head), target panel shows `HEAD ×3.0 / BODY / LIMB`, floating damage numbers (red for headshots), hit / headshot / kill markers, a `HEADSHOT` banner with a raster skull, kill-feed entries and bonus score.
- **Tracers** fly along every resolved shot; impacts throw sparks, debris and dust; floor hits leave scorch marks; blood soaks into the floor.

### Modular Weapon Upgrades & Field Armory
- **4 Modular Upgrade Slots per Weapon**:
  - **Extended Magazines**: Increases magazine capacity by +50% to +66% (M9: 15 → 22 rounds, M870: 8 → 12 shells, M4: 30 → 45 rounds).
  - **Recoil Compensator & Tactical Choke**: Reduces vertical recoil climb by 48% and narrows hipfire/ADS pellet spread by 42%.
  - **High-Caliber Armor Piercing (AP) Ammo**: Boosts bullet and pellet damage output by +42% to +45% across all firearms.
  - **Special Modules / Optics**:
    - **M4 Carbine**: Unlockable **ACOG 3× Optical Scope** transforming the weapon from red dot into a scoped rifle.
    - **M870 Shotgun**: **Rapid Cycling Pump Module** reducing cycling delay by 35%.
    - **M9 Pistol**: **Hair Trigger Assembly** reducing semi-automatic delay by 35%.
    - **Combat Knife**: **Serrated Stiletto Blade** granting lethal 3.5× headshot critical damage.
- **Field Armory Overlay (`[U]`)**: Press `[U]` at any time during gameplay to view detailed operative weapon stats, installed attachment modules, and upgrade status across all slots.
- **HUD Attachment Badges**: The bottom weapon HUD badge displays active attachment abbreviations (`[MAG]`, `[COMP]`, `[AP]`, `[ACOG]`, `[PUMP]`, `[TRIG]`).
- **Debug Weapon Range (`[9]`)**: Press `[9]` in-game to instantly toggle all attachments across all weapons for testing and tuning.

### Zone objectives, Commander boss & loot
- **A zone is cleared only when every enemy on the map is dead** — patrols, vault guards behind secret walls, reinforcements and the Commander. The HUD shows *HOSTILES REMAINING*; when 3 or fewer are left they are marked on the minimap and compass, and if the last ones are hidden in a vault the HUD says to search the walls.
- Reinforcements are limited per zone (none in zone 1, up to 5 later), so a zone can always be finished.
- Every zone has a **Commander** boss: a larger (1.35×) elite with ~3.6× health, its own health bar and a +1000 score bounty.
- **Bosses and elite guards always drop a gun** (one you don't own yet when possible) marked by a golden light beam and shown on the compass; the Commander also drops a medkit and ammo.

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

- Coins are shown in the HUD and spent in the **field shop** on the upgrade screen: `[7]` ammo resupply (40), `[8]` medkit +50 HP (35), `[9]` reroll the upgrade cards (25).

### Zone Clear: upgrade + loadout screen
- After the report: **1 choose one of 3 upgrade cards → 2 choose the weapon to deploy with → 3 deploy**.
- A gun you don't own yet is offered as an **unlock card** (e.g. *M4 CARBINE: FIELD ISSUE*). Picking any card also selects that gun in the *Deploy with* row, and **you start the next zone holding the chosen gun, loaded**.
- Pick a card with `1` `2` `3` or a click (you can change your mind), choose the weapon with `TAB` / arrows or a click, then press `ENTER` or click **DEPLOY**. A 60-second fallback timer only moves an idle game on.
- The mouse is released on this screen, and input from the fight that cleared the zone is ignored for the first second, so a stray shot, jump or number key can't pick a card or skip the screen.

### 3D first-person weapons
- Guns, the knife and the gloved hands are **real 3D models**: each side-profile polygon is extruded to its own thickness (barrel, receiver, scope, pump, grips…) and rendered with perspective, **back-face culling**, **Sutherland–Hodgman near-plane clipping**, flat **Lambert + Blinn specular** lighting and a **per-pixel z-buffer**. Forearms are 3D boxes.
- Recoil, reload, sprint, swap, slide/pump and knife stab/slash animations are now 3D rotations (yaw / pitch / roll about the gun's own axes).

### Sound
- **Every sound is synthesised in code at start-up** — no audio files, the same way no image files are used. Oscillators, filtered noise, envelopes, formant filters and a small room reverb build 52 effects (`audio.cpp`).
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
| Scope / sights | Per-pixel radial masks, rectangles, rings, Bresenham lines |
| HUD / menus | Rect / gradient / triangle / ring / arc fills with alpha blending |
| Textures & sprites | Baked texel-by-texel at start-up (noise, Voronoi, shaded primitives) |
| Text | Own **bitmap pixel font** (`pixelfont.h`, hand-drawn 5×7 glyphs) blitted as pixel blocks — no vector fonts |
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

---

## Project structure

```
fps_game/
├── fps_game.pro        Qt project file
├── main.cpp            Entry point (also: --snapshot <dir> renders test scenes to PNG)
├── game.h              Maps, configs, weapons, entity structs
├── pixelfont.h         Bitmap 5x7 pixel font (glyph table + blitter + word wrap)
├── raster.h            Canvas: pixel/blend/add, rects, gradients, circles, glows,
│                       rings/arcs, Bresenham, triangle rasterizer, polygons, noise
├── gunmodels.h         Weapon + glove polygons (view model, HUD icon, pickups)
├── assets.h/.cpp       Procedural sky, floors, walls (+mips), soldier painter,
│                       enemy animation atlas, pickup sprites, theme table
├── maps.h              The three 40x40 ASCII layouts (legend in the file header)
├── mainwindow.h/.cpp   Game loop, logic, AI, physics, input, free aim
├── world.cpp           Heightfield, light-map baking, doors & secrets, bullet-hole
│                       decals, 3D hitscan, line of sight, movement checks
├── render_world.cpp    Terrain + walls ray-march, sky, lens flare, sprites, tracers, particles
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

Only the Qt Widgets module is required.

---

## Team
Group project — Computer Graphics Lab, Semester 5
