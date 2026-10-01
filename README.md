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
| **Maps** | 24×24, doors only for show | Three hand-authored **40×40** maps; doors sink into the floor and lead to rooms; **secret walls** open onto hidden vaults guarded by **elite soldiers** with intel rewards; acid-crossing and ledge-climbing challenge areas |
| **Bullet impacts** | Nothing on walls | **Bullet holes** stamped on wall and cliff faces (hole, burnt rim, chipped ring, cracks); shotgun leaves a spread pattern; scorch marks and blood on the ground |
| **Aiming** | Crosshair fixed at the centre | **Free-aim crosshair** moving inside a dead-zone; pushing past the edge turns or tilts the camera (V toggles classic) |
| **Damage** | Every hit = 1 HP | **Pixel-perfect hitscan** against head / body / limb masks; **headshots ×2–3**, limbs reduced; damage numbers, headshot banner and markers |
| **Weapons** | Single flat pistol drawing | Pistol, pump shotgun and automatic carbine with magazines, reload, recoil, spread; perspective, Gouraud-shaded view models with gloved hands; slide/pump/reload/sprint animations, muzzle flash, ejected brass |
| **Optics** | FOV squeeze | **4× rifle scope** (2–8× on the wheel, breathing sway, hold breath) plus reflex / holographic sights |
| **Enemies** | Static 24×24 sprite | Procedurally painted soldiers with an 11-frame animation (walk, shoot, flinch, death fall, dithered corpse fade); patrol / chase / strafe AI; elite variant |
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
| **LMB / F** | Fire (the carbine is fully automatic) |
| **RMB / Tab** | Aim down sights — reflex sight (pistol), holographic sight (shotgun), **4× scope** (carbine) |
| **Mouse wheel** | **Raster magnifier** 1×→32× around the cursor (menus) or crosshair (in game): every framebuffer pixel becomes a box, with a pixel grid and a coordinate/colour readout. While scoped with the carbine it changes scope power instead |
| **R** | Reload · resume (paused) · redeploy (game over) |
| **1 / 2 / 3** | Pistol / shotgun / carbine (once picked up) |
| **Space** | Jump — onto ledges, out of pits, across acid |
| **E** | Open a door · search a suspicious wall for a secret passage |
| **V** | Toggle free-aim ↔ classic centred crosshair |
| **C** | Re-centre crosshair and view |
| **← / →** | Turn |
| **[ / ]** | Pixel-block size 1–8 (raster proof) |
| **ESC** | Pause (ESC again quits) · **M** main menu · **Q** quit |

Menus work with keyboard (arrows / 1-3 / Enter / Esc) **and mouse** (hover + click, right-click = back).

---

## What's in the game

### Combat
- **Pixel-perfect hitscan.** A shot resolves exactly what is drawn under the crosshair pixel: walls (with their real heights — you can shoot over a wall top), the floor, and enemy sprites tested **texel by texel** against a per-texel hit mask.
- **Headshots.** The enemy sprite atlas carries a mask of `HEAD / BODY / LIMB` per texel. Damage multipliers: head **×3.0** pistol, **×2.0** shotgun, **×2.5** carbine; limbs ×0.7–0.75. A pistol headshot one-taps a level-1 hostile on Easy/Medium.
- Feedback: crosshair turns red (with a ring when over a head), target panel shows `HEAD ×3.0 / BODY / LIMB`, floating damage numbers (red for headshots), hit / headshot / kill markers, a `HEADSHOT` banner with a raster skull, kill-feed entries and bonus score.
- **Three weapons** with magazines, reserve ammo, reload, spread, recoil, automatic fire and shotgun pellets with damage fall-off. Enemies drop ammo crates and weapon pickups.
- **Tracers** fly along every resolved shot; impacts throw sparks, debris and dust; floor hits leave scorch marks; blood soaks into the floor.

### Free-aim crosshair
The crosshair moves freely with the mouse inside a dead-zone box (shown faintly as you near it). Pushing past the box edge yaws the camera (sideways) or pitches it (up/down), like Arma-style free aim. The gun physically swings to point at the crosshair. Aiming down sights smoothly folds the crosshair offset into the camera so the aimed point stays put. **V** switches to a classic centred crosshair.

### Maps, doors & secrets
- Three hand-authored **40×40** maps (`maps.h`, readable ASCII layouts) — almost 3× the old area.
- **Doors** (`E`) sink into the floor and lead to rooms with enemies and supplies.
- **Secret walls** look like ordinary stone (a faint seam and cracks are the only hint). Searching one with `E` opens a **hidden vault** — `SECRET AREA FOUND +500` — guarded by **elite soldiers** (black kit, red goggles, 2× HP, faster and more accurate fire) and holding an **intel case** (+1000, full ammo and health).
- Challenge areas: an **acid vault** you cross by jumping between stepping stones to reach the carbine, a fortified **mesa** you climb by steps and a jump, an altar ledge, and **pits** you can only leave by jumping. Acid burns while you stand in it.
- HUD tracks secrets found; the game-over screen reports them.

### Terrain that goes up and down
- The ground is a continuous **heightfield**: rolling hills (fBm noise at the cell corners, bilinear in between) plus per-cell **ledges, steps and pits** with vertical rock faces.
- Player physics: gravity, jumping, landing shake, sticking to slopes; you can step up 0.32 units — anything taller needs a jump. Enemies, pickups, blood and bullets all follow the ground height.
- Line of sight, enemy fire and the sun shadows are fully 3D: hills and ledges hide you and cast shadows; slopes facing the sun are brighter.

### Bullet holes
- Every shot that lands on a wall or rock face stamps a **bullet-hole decal** onto that face: dark hole, burnt rim, chipped plaster ring, powder speckle and Bresenham-drawn radial cracks (shotgun pellets leave smaller holes, so a blast leaves a spray pattern). Stored as sparse 96-texels-per-unit textures per wall face, rendered with perspective like the wall itself.

### Optics
- **4× rifle scope** — the scene is re-rendered at the magnified FOV; outside the eyepiece is black with a lens vignette, chromatic fringe and glint; duplex reticle with mil-dots, bullet-drop ladder and illuminated centre. **Breathing sway** (figure-8) — hold Shift to steady it. Wheel changes magnification 2×–8×.
- **Reflex sight** (pistol) and **holographic sight** (shotgun) rise into view with tinted glass and an illuminated dot / ring.

### Weapon & animation
- Guns are side-profile polygons pushed through a **one-point perspective mapping** (the barrel converges toward the crosshair) and filled with a **Gouraud-shaded triangle rasterizer** plus a Bresenham specular rim.
- Gloved hands are polygons in gun-space, so they move with the gun; sleeves use the operative's colours.
- Animations: walk bob, mouse-lag sway, breathing idle, recoil kick + muzzle climb (camera recovers), pistol slide cycling, shotgun pump, **magazine-out reload**, weapon-swap drop, sprint carry pose.
- Muzzle flash: additive flame petals + bloom, plus a screen-space muzzle light; ejected brass / red shotgun shells tumble with gravity.

### Characters & enemies
- Soldiers are **painted procedurally** from shaded primitives (spheres, capsules, rounded boxes with Lambert lighting, camouflage noise, rim darkening): helmet, goggles, scarf, vest + pouches, rifle.
- **11-frame animation atlas**: aim, 4-frame walk cycle, shooting (with muzzle flash), hit flinch, 4-frame death fall. Corpses dissolve away with an **ordered-dither (Bayer)** fade.
- Enemy AI: idle wander → alert on sight / when shot → chase, keep their distance, strafe, and fire with difficulty-scaled accuracy; separation so they don't stack. Ground contact shadows; lit by the baked sun map.
- Operative portraits on the character screen are rendered by the same soldier painter.

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
└── render_hud.cpp      Frame composition, post FX, HUD, menus, pause / game over
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
