# CGLAB First Person Shooter
### A tactical FPS built entirely in Qt using pure software rasterization

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
| **RMB / Tab** | Aim Down Sights (ADS) — zooms FOV |
| **Space / LMB** | Fire |
| **Scroll Up/Down** | Zoom in/out (raster pixel-block proof for professor) |
| **[ / ]** | Decrease / increase pixel block size |
| **ESC** | Pause game |
| **R** | Resume (when paused) / Restart (on game-over) |
| **Q** | Quit to desktop (when paused or game-over) |

---

## Pre-Game Flow

```
Main Menu  →  Map Select  →  Difficulty Select  →  Character Select  →  Game
```

| Screen | Options |
|---|---|
| **Main Menu** | NEW GAME / EXIT |
| **Map Select** | Urban Ruins / Military Base / Dark Dungeon |
| **Difficulty Select** | EASY / MEDIUM / HARD — each scales enemy HP, fire rate, bullet speed, medkit count |
| **Character Select** | GHOST / FALCON / PHANTOM — each with different HP, Speed, Damage stats |

---

## Features Implemented

### ─── Core Rendering Engine ───────────────────────────────────────

#### 🔭 DDA Raycasting (Software Only)
- One ray cast per screen column (800 rays at 800×600)
- **Fisheye correction** — perpendicular wall distance, no barrel distortion
- **Z-buffer** (`float zBuf[W]`) per-column depth for correct sprite occlusion behind walls
- Camera plane vectors `(planeX, planeY)` give a 66° horizontal FOV
- **Pixel Block Mode (`[`/`]` keys)** — renders in 1×1, 2×2, 4×4, or 8×8 super-pixels to visually demonstrate the raster framebuffer at any zoom level

#### 🌅 Perspective-Correct Floor & Ceiling Casting
- Every floor pixel gets the true world-space tile coordinate via inverse camera-plane projection
- Checkerboard floor tiles with per-row distance-based shading
- Ceiling mirrored with a sky-blue tint
- Both fade smoothly with distance fog

#### 🌅 Procedural Skybox & Clouds
- Full 1024×300 panoramic skybox pre-generated at startup using **5-octave Fractal Brownian Motion (FBM) Value Noise**
- Beautiful gradient sky (zenith = deep navy → horizon = light blue) with soft cloud layer
- **Parallax scrolling** — sky rotates with player facing direction
- **Dynamic sun disc** — appears in the east, fades in/out as player rotates
- Horizon haze line at ceiling/floor boundary
- **Zero per-frame cost** — skybox is fully precomputed, only a table lookup per pixel during rendering

#### 🌫️ Distance Fog
- Linear per-pixel fog blends RGB toward black over 14 map units
- Applied to walls, floor, ceiling, and all sprites

#### 🧱 Procedural Wall Textures (zero image assets, all precomputed)
- **Stone** — large horizontal blocks with mortar lines and per-pixel hash noise
- **Brick** — running-bond courses, red-brown palette
- **Concrete** — smooth hash noise with blue-grey tint
- Y-side faces at 50% brightness to simulate side lighting
- All 3 textures precomputed into `uint32_t m_textures[4][64*64]` at startup for maximum throughput

---

### ─── Gameplay ───────────────────────────────────────────

#### 🏃 Player Movement & Sprint System
- **WASD** movement with wall-sliding AABB collision (X and Y axes resolved independently)
- **Diagonal movement normalized** — no speed boost when pressing two keys
- **Shift + WASD → Sprint**: 1.65× movement speed
- **Stamina bar** (0–100): drains in ~2 s, recharges in ~4 s
- Sprint cut automatically when stamina hits 0

#### 🔫 Shooting & ADS
- Space or LMB fires; configurable fire cooldown per character
- **Aim Down Sights (RMB/Tab)** — smoothly narrows FOV to simulate zoom
- **Muzzle flash** — bright white/yellow raster disc drawn for 2 frames
- **Camera shake** — brief Y-offset on fire and on player hit
- **Recoil** — `recoilOffset` animates upward then decays on each shot

#### 💊 Health & Damage System
- Player HP displayed as raster block-segments in bottom-left panel
- **Damage vignette** — per-pixel red screen-edge fade on hit (precomputed lookup table)
- **Damage direction indicator** — arrow points toward incoming damage source
- **Health Kits (Medkits)** — spawned on map, walk over to restore +30 HP; shown as bobbing raster green-cross billboards with a pulsing glow ring; appear on radar

#### 👾 Enemy AI
- Enemies have **line-of-sight check** (DDA march through MAP[][]) before shooting
- Enemies open fire when within aggro range **and** have clear LOS to player
- Enemies shoot projectiles toward the player's position
- On hit: enemy **flashes red** for 0.2 s; enemy **HP bar** appears above their sprite for 2.2 s (fades out)
- **Target HP bar** (bottom centre) shows focused enemy stats — only when you have LOS to them

#### 📈 Level Progression
- Kill threshold increases each level; once met → level-complete banner → next level loads
- Each level: more enemies spawned, higher HP, faster fire rate
- Maps feature 3 different layouts (Urban Ruins / Military Base / Dark Dungeon)

#### ⚙️ Difficulty System
| Difficulty | Enemy HP | Fire Rate | Bullet Speed | Medkit Count |
|---|---|---|---|---|
| EASY | 0.7× | 1.4× cooldown | 0.8× | ×3 |
| MEDIUM | 1.0× | 1.0× cooldown | 1.0× | ×2 |
| HARD | 1.6× | 0.65× cooldown | 1.35× | ×1 |

---

### ─── Visuals & HUD ───────────────────────────────────────────

#### 🎮 High-Fidelity Weapon & Arms
- **Tactical pistol** — drawn entirely in raster: ejector port, barrel bore, rear serrations, stippled grip, trigger + guard, picatinny rail, front/rear dot sights
- **Gloved arms** — detailed forearm muscle definition, knuckle highlights, tactical wrist watch on firing hand
- Bobbing idle animation (sin-wave position offset each frame)
- Muzzle flash attached to barrel tip

#### 🧭 HUD Elements
- **Health panel** (bottom-left): HP text + 10-block raster health bar + character name
- **Ammo panel** (bottom-right): ammo counter, magazine segments
- **Stamina bar**: colour shifts green → yellow → red as it depletes
- **Mini-map / Radar**: circular radar with walls, player dot, facing vector, enemy blips (blink red), medkit blips (green) — **all dots clamped to radar edge** (no overflow)
- **Compass**: top-centre bearing strip showing N/E/S/W
- **Kill feed**: last 3 kills scrolled in top-right (auto-fades)
- **Hit marker**: white "X" crosshair flash on enemy damage
- **Combo display**: kill-streak text centred on screen

#### 📷 Zoom / Pixel-Block Mode
- **Scroll wheel** zooms the rendered framebuffer — professor can zoom in to see individual raster pixels
- `[` / `]` keys change the render block size (1–8), coarsening the raster visibly

---

### ─── Menu System ───────────────────────────────────────────

- **Animated data-stream background** — 55 falling-light columns drawn pixel-by-pixel (raster proof)
- **Pre-generated vignette** — edge-darkening computed once at startup into `uint8_t m_vignette[H][W]`
- **Map Select** — 3 zone cards with terrain description and difficulty star ratings
- **Difficulty Select** — 3 options with per-stat bar charts drawn as raster rectangles
- **Character Select** — 3 operative cards with raster silhouette + stat bars; full header never truncated (uses full `W` width)
- **Pause Menu** — ESC overlay with Resume / Quit
- **Game Over** — panel with Restart / Quit buttons

---

## Raster Compliance Table

All game rendering is **pure software rasterization** — pixels written directly to `m_pixels[y*W+x]`:

| System | Method | Raster proof |
|---|---|---|
| Sky / Clouds | FBM Value Noise precomputed to `m_skybox[1024*300]`, lookup per pixel | `m_pixels[y*W+x]` |
| Floor / Ceiling | Per-pixel inverse-projection floor cast | `m_pixels[y*W+x]` |
| Walls | DDA march + `m_textures[t][ty*64+tx]` lookup | `m_pixels[y*W+x]` |
| Sprites | Per-column, per-row billboard with Z-test | `m_pixels[sy*W+sx]` |
| Weapon & Arms | `fillRect()` + `drawLine()` primitives | `m_pixels[y*W+x]` |
| Muzzle flash | `fillCircle()` + ray lines | `m_pixels[y*W+x]` |
| Damage vignette | Precomputed `m_vignette[H][W]` table lookup | `m_pixels[y*W+x]` |
| Medkit billboards | Raster cross + glow ring (midpoint circle) | `m_pixels[sy*W+sx]` |
| HUD bars | `fillRect()` | `m_pixels[y*W+x]` |
| Crosshair | `drawLine()` + `px()` | `m_pixels[y*W+x]` |
| Radar | `fillCircle()` + pixel columns | `m_pixels[y*W+x]` |
| Menu background | 55 per-pixel falling-light streams | `m_pixels[y*W+x]` |
| **Text / fonts** | `QPainter::drawText()` on `QImage` | Qt **QRasterPaintEngine** (software, no GPU) |

Final blit: `QPainter::drawImage(0,0,m_buf)` — one pass per frame.

---

## Algorithms Used (CG Reference)

| System | Algorithm |
|---|---|
| Wall rendering | DDA ray marching |
| Floor/ceiling | Perspective-correct inverse projection |
| Sprite projection | Inverse 2×2 camera-matrix transform |
| Sprite occlusion | Per-column Z-buffer test |
| Sprite sort | `std::sort` by squared distance (painter's algorithm) |
| Sky/clouds | Fractal Brownian Motion (FBM) Value Noise — 5 octaves |
| Wall texture | Hash noise + modular brick/mortar pattern (precomputed) |
| Fog | Linear distance blend toward black |
| Side lighting | Y-side faces at 50% brightness |
| Fisheye fix | Perpendicular distance (not Euclidean) |
| Collision | AABB 4-corner test, axes resolved independently |
| Enemy LOS | DDA step-march through `MAP[][]` |
| Bullet movement | Linear Euler integration (`pos += vel * dt`) |
| Bullet trail | Ghost sprites at `pos - vel*t`, `dimColor()` falloff |
| Radar clamping | Normalize offset vector to radar radius before drawing |
| Pixel helpers | Bresenham line, midpoint circle, axis-aligned rect fill |

---

## Project Structure

```
fps_game/
├── fps_game.pro         Qt project file
├── main.cpp             Entry point — creates 800×600 window
├── game.h               All pure data (no Qt): map, sprite art, structs, level configs
│   ├── MAP[24][24]      Tile world (0=air, 1=stone, 2=brick, 3=concrete)
│   ├── SPAWN_PTS[12]    Enemy spawn grid
│   ├── CHAR_PROFILES[3] Ghost / Falcon / Phantom stat tables
│   ├── DIFF_CFG[3]      Easy / Medium / Hard multiplier tables
│   ├── IMP_SPRITE[16][16] Hand-authored pixel-art enemy
│   ├── MEDKIT_SPRITE    Hand-authored pixel-art medkit cross
│   ├── BulletOwner      Enum: PLAYER_SHOT / ENEMY_SHOT
│   ├── Player           pos, dir, camera plane, hp, ammo, stamina, timers
│   ├── Enemy            pos, hp, hitFlash, fireCooldown, aggroRange
│   ├── Bullet           pos, velocity, owner, alive
│   ├── HealthKit        pos, active, bobOffset
│   ├── LevelCfg         Per-level parameters table (LEVEL_CFG[])
│   └── GameState        Owns player, enemies, bullets, healthKits, score, level
├── mainwindow.h         QWidget subclass declaration
│   ├── m_pixels         Raw ARGB framebuffer pointer
│   ├── m_skybox[1024*300] Precomputed panoramic cloud skybox
│   ├── m_textures[4][64*64] Precomputed wall textures
│   └── m_vignette[H][W] Precomputed edge-darkening table
└── mainwindow.cpp       All logic + rendering
    ├── initGame()            Full reset, precomputes assets, calls loadLevel(1)
    ├── loadLevel(int)        Spawns enemies + medkits from LEVEL_CFG
    ├── gameStep()            Movement, sprint, enemy AI, bullets, level check
    ├── fireBullet()          Player shoots; cooldown + muzzle flash timers
    ├── enemyFire(Enemy&)     Enemy shoots toward player position
    ├── hasLOS()              DDA ray through MAP to check wall obstruction
    ├── posValid()            4-corner AABB vs tile map (wall sliding)
    ├── renderFloorCeiling()  Precomputed skybox lookup + perspective floor cast
    ├── renderWalls()         DDA raycaster + precomputed texture lookup
    ├── renderSprites()       Z-tested billboards + medkits + enemy HP bars
    ├── renderWeaponAndArms() Two arms + detailed pistol + muzzle flash
    ├── renderDamageVignette() Red screen-edge effect on player hit
    ├── renderHUD()           All HUD bars + radar + compass + kill feed
    ├── renderTargetBar()     Enemy focus HP bar (LOS-gated)
    ├── renderLevelBanner()   Level complete transition screen
    ├── renderPauseMenu()     ESC pause overlay
    ├── drawGameOver()        Game-over panel
    ├── renderMenu()          Main menu
    ├── renderMapSelect()     Zone selection screen
    ├── renderDiffSelect()    Difficulty selection screen
    └── renderCharSelect()    Operative selection screen
```

---

## How to Build

1. Open **Qt Creator**
2. **File → Open File or Project** → select `fps_game.pro`
3. Configure with **MinGW** kit (Qt 6+)
4. Press **▶ Run**

Or via CLI:
```powershell
$env:PATH = "C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;" + $env:PATH
qmake fps_game.pro -spec win32-g++
mingw32-make -j4
```

No external libraries. Only Qt Widgets module required.

---

## How to Extend (Roadmap)

```
Enemy patrol     → add waypoints[] to Enemy struct, lerp pos each frame
Enemy animation  → cycle IMP_SPRITE variants (add IMP_WALK[4][16][16])
Shotgun          → fireBullet() shoots 5 bullets at ±10/20° offsets
Destructible walls → MAP[my][mx]=0 on bullet hit, spawn rubble sprite
Save system      → write GameState fields to QSettings on level complete
Networked co-op  → QTcpSocket, sync player positions each tick
Sound            → QSoundEffect::play() on fireBullet() — still raster render
```

---

## Team
Group project — Computer Graphics Lab, Semester 5
