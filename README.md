# CGLAB First Person Shooter
### A Doom-style FPS built entirely in Qt using pure software rasterization

> **No OpenGL. No GPU. Every single pixel is computed by the CPU and written into a `QImage` framebuffer.**
> This is a Computer Graphics lab group project demonstrating real-time raster algorithms inside an interactive game.

---

## Controls

| Key | Action |
|---|---|
| **W / S** | Walk forward / backward |
| **A / D** | Strafe left / right |
| **Shift + WASD** | **Sprint** (1.65× speed, drains stamina) |
| **← → or Q / E** | Turn left / right |
| **Mouse move** | Mouse-look (cursor auto-recentres) |
| **Space / LMB** | Fire |
| **ESC** | Pause game |
| **R** | Resume (when paused) / Restart (on game-over) |
| **Q** | Quit to desktop (when paused or game-over) |
| **ESC × 2** | Exit immediately |

---

## Features Implemented

### ─── Core Engine ───────────────────────────────────────

#### 🔭 DDA Raycasting (Doom/Wolfenstein style)
- One ray cast per screen column (800 rays at 800×600)
- **Fisheye correction** — perpendicular wall distance eliminates the barrel-distortion artifact
- **Z-buffer** (`float zBuf[W]`) stores per-column depth; used to correctly occlude sprites behind walls
- Camera plane vectors `(planeX, planeY)` give a 66° horizontal FOV

#### 🌅 Perspective-Correct Floor & Ceiling Casting
- Every floor pixel gets the true world-space tile coordinate via inverse camera-plane projection
- **Checkerboard floor tiles** with per-row distance-based shading
- Ceiling mirrored from floor with a cooler blue-grey tint
- Both fade smoothly with distance fog

#### 🌫️ Distance Fog
- `fogBlend(color, dist)` linearly multiplies RGB toward black over 14 map units
- Applied to walls, floor, ceiling, and all sprites → natural depth perception, no explicit depth buffer needed

#### 🧱 Procedural Wall Textures (zero image assets)
- **Stone** — large horizontal blocks with mortar lines and per-pixel noise
- **Brick** — running-bond courses with alternating-row mortar joints, red-brown palette
- **Concrete pillar** — smooth hash noise with a blue-grey tint
- Y-side faces (north/south) rendered at 50% brightness to simulate side lighting

---

### ─── Gameplay ───────────────────────────────────────────

#### 🏃 Player Movement & Sprint System
- **WASD** movement with wall-sliding AABB collision (X and Y axes resolved independently)
- **Shift + WASD → Sprint**: 1.65× movement speed
- **Stamina bar** (0–100): drains in ~2 s, recharges in ~4 s when not sprinting
- Sprint is automatically cut when stamina hits 0
- Stamina bar turns **gold** while sprinting, **blue** while recharging

#### 🎯 Mouse Look
- Mouse delta rotates camera; cursor is re-centred each frame so it never leaves the window
- Keyboard turn (Q/E/← →) also available at 2.2 rad/s

#### 👾 Enemy AI — Line-of-Sight + Fire
- 6 → 12 static Imp enemies placed at strategic positions around the 24×24 map
- **Aggro range check**: each enemy measures Euclidean distance to player every frame
- **LOS (Line-of-Sight) DDA ray**: before firing, a step-march is cast from enemy to player through the tile map — enemies **cannot shoot through walls**
- On aggro + clear LOS: enemy fires a projectile bullet toward the player's current position
- Fire rate and bullet speed **scale with level difficulty**

#### 💊 Player Damage & Health
- Enemy bullets deal `10 + 4×(level−1)` HP per hit
- HP drops to 0 → **GAME OVER** screen
- **Damage vignette**: red pixels bleed in from all four screen edges scaled by hit intensity
- Partial HP heal (+30) and ammo refill (+20) awarded between levels

#### 🔫 Bullet Projectile System
- Bullets are **world-space objects** (not hitscan) — they travel through the 3D world
- Wall collision: tile-map lookup each tick; bullet destroyed on impact
- **Player bullets** (gold) hit enemies, **enemy bullets** (red/magenta) hit the player
- Hit radius: 0.42 map-units for enemies, 0.39 for player
- Fire rate cooldown: 0.45 s between player shots

#### 🏆 Level Progression System
- Killing a required number of enemies triggers **LEVEL COMPLETE**
- Auto-advances to next level after a 3.5-second banner

| Level | Enemies | Kills needed | Enemy HP | Fire range | Fire cooldown | Bullet speed |
|---|---|---|---|---|---|---|
| 1 | 6 | 4 | 3 | 5.0 tiles | 3.0 s | 4.5 t/s |
| 2 | 8 | 6 | 4 | 6.0 tiles | 2.5 s | 5.5 t/s |
| 3 | 10 | 8 | 5 | 7.0 tiles | 2.0 s | 6.5 t/s |
| 4 | 12 | 10 | 7 | 8.0 tiles | 1.5 s | 7.5 t/s |
| 5+ | 12 | 12 | 10+ | 9.0 tiles | 1.0 s | 8.5 t/s |

#### 🏅 Score System
- **+100 × level** per enemy kill
- **+500 × level** bonus on level complete
- Total score persists across levels and is shown on game-over

---

### ─── Rendering ──────────────────────────────────────────

#### 🎭 Billboard Sprite System (Z-tested)
- Enemy and bullet sprites are world-space billboards projected to screen via **inverse 2×2 camera matrix**
- Z-buffer test per screen column: sprites behind walls are correctly hidden
- **Back-to-front sort** (painter's algorithm) prevents far sprites overdrawing near ones
- Sprites scale linearly with inverse distance (correct perspective foreshortening)

#### 💥 Bullet Trail Animation (3-pass ghost system)
- Each bullet is drawn **three times**: current position (full brightness) + two ghost positions behind it
- Ghost positions computed as `pos − velocity × trailTime` (0.055 s steps) — no history storage needed
- Each ghost: **35% dimmer** and **narrower** (wScale 0.45→0.25) → elongated streak/tracer effect
- **Player bullet**: gold/orange core, white hotspot
- **Enemy bullet**: red/magenta core, dark red glow — visually distinct at a glance

#### 👤 16×16 Pixel-Art Imp Sprite
- Hand-authored as `uint32_t IMP_SPRITE[16][16]` directly in `game.h` (no image files)
- **White flash** effect when hit (`hitFlash` timer 0.18 s)
- Transparent pixels (alpha=0) correctly skipped in the sprite renderer

#### 🔫 First-Person Weapon + Arms (Partial Character)
All drawn with `fillRect()` / `fillCircle()` / `drawLine()` — zero sprite assets:

**Left arm (support / guide hand):**
- Dark tactical sleeve with left-edge highlight
- Forearm skin transitioning to hand
- Palm facing up (cupped under barrel)
- Four visible fingers with fingernail shading
- Thumb on the left side

**Detailed pistol (Glock-style):**
- Muzzle compensator / brake (with port-hole cut-outs)
- Long barrel with top-face highlight
- Three underbarrel tactical rail notches
- Front sight post with white dot insert
- Slide body with serrations (rear), ejection port cut-out (right side)
- Rear U-notch sight with tritium dot
- Hammer spur exposed at slide rear
- Frame with mag-release button
- Trigger guard (3-sided) + trigger with face highlight
- Textured grip with horizontal pattern lines
- Magazine base plate at grip bottom

**Right arm (trigger hand):**
- Wrist / palm gripping grip (skin tones)
- Knuckle detail across four fingers
- Dark tactical sleeve continuing off-screen

**Weapon bob**: sinusoidal X+Y offset; frequency 5 Hz walking, 8 Hz sprinting for faster feel

**Muzzle flash** (on fire):
- 6 radial rays (`drawLine`) fanning out from barrel tip
- 4-layer concentric circles: outer glow → orange → gold → white core
- Full-screen brightness flash (every pixel R+G boosted by flash intensity)

#### 🔴 Damage Vignette
- On player hit: red channel boosted at all four screen edges up to 90 px deep
- Intensity proportional to `damageFlash` timer (0→0.45 s fade)

---

### ─── HUD & UI ───────────────────────────────────────────

#### 📊 Heads-Up Display
- **HP bar** (180 px wide): green → red when HP < 40%
- **Stamina bar** (120 px wide): gold (sprinting) / blue (recharging)
- **Ammo counter** — LOW AMMO / NO AMMO warning text
- **Kill progress** at top-centre: `LEVEL X | KILL Y/Z`
- **Score** displayed top-right
- **FPS** (exponential moving average, top-right)
- **Minimap** (10×10 tile view centred on player): colour-coded wall types, teal player dot, red enemy dots
- Control hints overlay (first 5 seconds of Level 1 only)

#### ⊕ Crosshair
- Two-pixel-thick horizontal + vertical lines with a 6-pixel gap
- Red centre dot

#### ⏸ Pause Menu (ESC)
- First ESC: pause — game frozen, enemies frozen
- Semi-transparent overlay panel with border
- `[R]` Resume, `[Q]` Quit to Desktop
- Second ESC: immediate exit (`QApplication::quit()`)

#### 💀 Game-Over Screen
- Full-screen dark overlay with red-bordered panel
- Shows **level reached**, **total kills**, **final score**
- `[R]` Restart from Level 1, `[Q]` Quit to Desktop

#### 🏁 Level Complete Banner
- Entire screen darkened
- Centred panel: "LEVEL COMPLETE!", bonus points earned, total score, "Loading Level X…"
- Auto-advances to next level after 3.5 seconds

---

### ─── Raster Graphics Compliance ────────────────────────

**All game rendering is pure software rasterization:**

| System | Method | Pixels written |
|---|---|---|
| Floor / Ceiling | Per-pixel loop, floor-casting math | `m_pixels[y*W+x]` directly |
| Walls | DDA march + `wallPixel()` | `m_pixels[y*W+x]` directly |
| Sprites | Per-column, per-row billboard | `m_pixels[sy*W+sx]` directly |
| Weapon & Arms | `fillRect()` calls | `m_pixels[y*W+x]` directly |
| Muzzle flash | `fillCircle()` + `drawLine()` | `m_pixels[y*W+x]` directly |
| Damage vignette | Per-pixel loop, edge distance | `m_pixels[y*W+x]` directly |
| Bullet trail | 3× sprite pass, `dimColor()` | `m_pixels[y*W+x]` directly |
| HUD bars | `fillRect()` | `m_pixels[y*W+x]` directly |
| Crosshair | `drawLine()` + `px()` | `m_pixels[y*W+x]` directly |
| **Text / fonts** | `QPainter::drawText()` on `QImage` | Qt **QRasterPaintEngine** (software, no GPU) |

The final blit: `QPainter::drawImage(0,0,m_buf)` — one pass per frame.

---

## Project Structure

```
fps_game/
├── fps_game.pro         Qt project file
├── main.cpp             Entry point — creates 800×600 fixed window
├── game.h               All pure data (no Qt): map, sprite art, structs, level configs
│   ├── MAP[24][24]      Tile world (0=air, 1=stone, 2=brick, 3=concrete)
│   ├── SPAWN_PTS[12]    Enemy spawn grid (more used per level)
│   ├── IMP_SPRITE[16][16] Hand-authored pixel-art demon
│   ├── BulletOwner      Enum: PLAYER_SHOT / ENEMY_SHOT
│   ├── Player           pos, dir, camera plane, hp, ammo, stamina, timers
│   ├── Enemy            pos, hp, hitFlash, fireCooldown, aggroRange
│   ├── Bullet           pos, velocity, owner, alive
│   ├── LevelCfg         Per-level parameters table (LEVEL_CFG[])
│   └── GameState        Owns player, enemies, bullets, score, level, flags
├── mainwindow.h         QWidget subclass declaration
└── mainwindow.cpp       All logic + rendering (~700 lines)
    ├── initGame()            Full reset, calls loadLevel(1)
    ├── loadLevel(int)        Spawns enemies from LEVEL_CFG, refills player
    ├── gameStep()            Movement, sprint, enemy AI, bullets, level check
    ├── fireBullet()          Player shoots; cooldown + muzzle flash timers
    ├── enemyFire(Enemy&)     Enemy shoots toward player position
    ├── hasLOS()              DDA ray through MAP to check wall obstruction
    ├── posValid()            4-corner AABB vs tile map (wall sliding)
    ├── renderFloorCeiling()  Perspective-correct floor cast
    ├── renderWalls()         DDA raycaster → fills m_zBuf[]
    ├── renderSprites()       Z-tested billboards + 3-pass bullet trail
    ├── renderWeaponAndArms() Two arms + detailed pistol + muzzle flash
    ├── renderDamageVignette() Red screen-edge effect on player hit
    ├── renderHUD()           All HUD bars + minimap (QPainter text only)
    ├── renderLevelBanner()   Level complete transition screen
    ├── renderPauseMenu()     ESC pause overlay with resume/exit options
    └── drawGameOver()        Game-over panel with restart/quit options
```

---

## Map Layout (24 × 24)

```
111111111111111111111111
1......................1
1......................1
1...2222....2222.......1   ← brick alcoves (top)
1...2..........2.......1
1...2..........2.......1
1...222.....222........1
1......................1
1......3........3......1   ← concrete pillars
1..........1111........1
1..........1..1........1   ← stone room (centre)
1..........1..1........1
1..........1111........1
1......................1
1......3........3......1
1......................1
1...2..........2.......1
1...2..........2.......1
1...2222....2222.......1   ← brick alcoves (bottom)
1......................1
1......................1
1.....111111111111.....1   ← stone wall barrier
1......................1
111111111111111111111111
```

Player spawns at **(2.5, 2.5)** facing East.
Level 1 enemies at corners + centre. More spawn slots filled each level.

---

## Algorithms Used (CG Reference)

| System | Algorithm |
|---|---|
| Wall rendering | DDA ray marching (same family as Assignment 2) |
| Bullet movement | Linear Euler integration (pos += vel × dt) |
| Enemy LOS | DDA step-march through MAP[][] |
| Sprite projection | Inverse 2×2 camera-matrix transform |
| Sprite occlusion | Per-column Z-buffer test |
| Sprite sort | std::sort by squared distance (painter's algorithm) |
| Floor/ceiling | Perspective-correct inverse projection |
| Wall texture | Procedural hash noise + modular brick/mortar pattern |
| Fisheye fix | Perpendicular distance (not Euclidean) |
| Fog | Linear distance blend toward black |
| Side lighting | Y-side faces at 50% brightness |
| Collision | AABB 4-corner test, axes resolved independently |
| Bullet trail | Ghost sprites at pos − vel×t, dimColor() falloff |
| Pixel helpers | Bresenham line, midpoint circle, axis-aligned rect fill |

---

## How to Build

1. Open **Qt Creator**
2. **File → Open File or Project** → select `fps_game.pro`
3. Configure with **MinGW** kit (Qt 5.9+)
4. Press **▶ Run**

No external libraries. Only Qt Widgets module required.

---
## Changes & New Features

### High-Fidelity Tactical Updates
- **Procedural Sky & Clouds:** Beautiful 5-octave FBM Value Noise computed per-pixel for sky and clouds, complete with parallax scrolling and dynamic sun. Pure software rendering!
- **Difficulty Select:** New pre-game screen to choose Easy, Medium, or Hard. Scales enemy HP, damage, firing speed, and health kit spawns.
- **Health Kits:** Added medkits to maps. Walk over to heal. Rendered as bobbing raster billboards with a pulsing green glow ring.
- **Enemy HP Display:** Hitting an enemy displays an HP bar above their sprite for 2.2 seconds.
- **Improved Weapon Visuals:** High-detail tactical pistol featuring an ejector port, barrel bore, rear serrations, stippled grip, trigger guard, and dot sights.
- **Improved Arms:** Tactical gloved arms with forearm muscle definition, knuckle highlights, and a wrist watch detail on the firing hand.

### UI & Performance Fixes
- **Massive FPS Boost:** Skybox, wall textures, and menu vignette are now pre-generated, eliminating millions of per-pixel floating-point math calls per frame. FPS increased significantly!
- **UI Layout Fixes:** Adjusted heading alignment on the Character Select screen to prevent text clipping.

---
## Movement Fixes and Improvements

### Corrected A/D Strafing

The original strafing calculations were reversed. Pressing **A** moved the player right, while pressing **D** moved the player left.

The movement vectors were corrected so that:

- **A** strafes left relative to the player's viewing direction.
- **D** strafes right relative to the player's viewing direction.

### Fixed Diagonal Movement

Previously, each movement key applied full speed independently. Pressing two keys together, such as **W + D**, made diagonal movement approximately `√2` times faster than straight movement.

The movement inputs are now combined into a single vector and normalized before applying speed:

```cpp
float moveX = 0.f;
float moveY = 0.f;

if (m_keys.count(Qt::Key_W)) {
    moveX += pl.dirX;
    moveY += pl.dirY;
}

if (m_keys.count(Qt::Key_S)) {
    moveX -= pl.dirX;
    moveY -= pl.dirY;
}

// Strafe left
if (m_keys.count(Qt::Key_A)) {
    moveX += pl.dirY;
    moveY -= pl.dirX;
}

// Strafe right
if (m_keys.count(Qt::Key_D)) {
    moveX -= pl.dirY;
    moveY += pl.dirX;
}

// Normalize the movement vector
float moveLength = std::sqrt(moveX * moveX + moveY * moveY);

if (moveLength > 0.f) {
    moveX /= moveLength;
    moveY /= moveLength;

    tryMove(
        pl.posX + moveX * SPD,
        pl.posY + moveY * SPD
    );
}
```

### Why This Change Was Made

This update provides:

- Correct left and right strafing
- Working diagonal movement
- Equal speed in every direction
- Controls that behave like a standard first-person shooter
- Existing wall-collision handling for diagonal movement

## How to Extend (Week 2+ Roadmap)

```
Enemy movement    → add patrol waypoints[] to Enemy struct, lerp pos each frame
Enemy rotation    → store dirX/dirY on Enemy, rotate toward player when aggroed
Enemy animation   → cycle through IMP_SPRITE variants (add IMP_WALK[4][16][16])
Shotgun           → fireBullet() shoots 5 bullets at ±10/20/30° offsets
Health packs      → add Item struct, pick up when posValid() radius overlap
Destructible walls→ MAP[my][mx]=0 on bullet hit + add rubble sprite
Sound (beep)      → QSound::play("fire.wav") on fireBullet() — still raster render
Save system       → write GameState fields to QSettings on level complete
```

---

## Team
Group project — Computer Graphics Lab, Semester 5
