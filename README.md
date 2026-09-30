# CGLAB First Person Shooter
### A Doom-style FPS built entirely in Qt using pure software rasterization

> **No OpenGL. No GPU. Every pixel is computed by the CPU and written into a `QImage` buffer.**
> This is a Computer Graphics lab group project demonstrating semester-long raster algorithms in an interactive game.

---

## Screenshots / What it looks like

```
┌────────────────────────────────────────────────────────────────────────────┐
│  AMMO  30  │                   [ceiling]                     │  KILLS  0   │
│  KILLS  0  │─────────────────────────────────────────────────│  FPS   60   │
│  FPS   60  │         3D raycasted walls (stone/brick)        │             │
│            │         with procedural textures + fog          │  [minimap]  │
│ [HP bar]   │   ──────────────────────────────────────────    │             │
│            │             [ + ]  ← crosshair                  └─────────────┘
│            │   ──────────────────────────────────────────                  │
│            │         perspective-correct floor tiles                       │
│            │                   [floor]                                     │
│            │                              ┌─────────────────────┐         │
│            │                              │  [pistol + hand]    │         │
└────────────┴──────────────────────────────┴─────────────────────┴─────────┘
```

---

## Features Implemented

### Week 1 — Core Engine

#### 🔭 Raycasting Engine (Doom / Wolfenstein style)
- **DDA (Digital Differential Analyzer)** ray marching for wall detection
- One ray per screen column (800 rays/frame at 800×600)
- **Fisheye correction** via perpendicular distance instead of Euclidean distance
- **Z-buffer** (`float zBuf[W]`) stores per-column wall depth for sprite occlusion

#### 🎨 Procedural Wall Textures (no image files)
- **Stone** — large horizontal blocks with mortar lines + per-pixel noise
- **Brick** — running-bond courses with alternating offset mortar joints
- **Concrete pillar** — smooth noise pattern with blue-grey tint
- Y-side faces (north/south walls) rendered 50% darker to simulate side lighting

#### 🌅 Perspective-Correct Floor & Ceiling
- Full floor-casting algorithm: each pixel gets the correct world-space tile coordinate
- **Checkerboard floor tiles** with distance-based shading
- Ceiling is mirrored with a cooler blue-grey tint
- Distance fog: both floor and ceiling fade to black at ~14 map units

#### 🌫️ Distance Fog
- `fogBlend()` linearly fades all colours toward black over 14 map units
- Applied to walls, floor, ceiling, and all sprites
- Creates natural depth perception without a depth buffer

#### 👾 Billboard Sprite System
- World-space sprites projected to screen using **inverse camera matrix transform**
- **Z-buffer occlusion**: sprites behind walls are correctly hidden column-by-column
- Back-to-front sorting (painter's algorithm) for correct sprite-on-sprite overlap
- **16×16 pixel-art Imp sprite** hand-authored in `game.h` as a `uint32_t[16][16]` array
- White flash effect on enemy hit (0.18s)
- Enemy HP bar above each sprite (green → depletes per hit)

#### 💥 Bullet Projectile System
- Bullets are **world-space objects** (not hitscan) — they travel through the 3D world
- Rendered as **glowing gold orbs** via a circular sprite sampled procedurally
- Wall collision: tile-map lookup per bullet position
- Enemy collision: radius test (0.4 map units)
- Bullets are z-buffer-tested against walls — they can be seen flying through corridors

#### 🔫 First-Person Weapon (Pistol)
- Drawn entirely with `fillRect()` calls — no sprite assets needed
- **Weapon bob**: sinusoidal vertical + horizontal movement while walking
- **Muzzle flash**: multi-layer circle burst (outer glow → orange → gold → white core)
- **Screen flash**: entire framebuffer brightened for 1–2 frames on fire
- Fire rate limited (0.45s cooldown between shots)

#### 🗺️ Minimap
- 10×10 tile view centred on player, top-left corner of screen
- Wall types colour-coded (grey = stone, brown = brick, blue = concrete)
- Player shown as teal square; live enemies as red squares

#### 🎮 Player & Controls
- **WASD**: walk forward/backward, strafe left/right
- **← → / Q / E**: turn left/right (keyboard)
- **Mouse move**: mouse-look (re-centres cursor each frame)
- **Space / LMB**: fire pistol
- **R**: restart (on game over)
- AABB sliding collision: player glides smoothly along walls (X and Y resolved separately)

#### 📊 HUD
- HP bar (green → red below 40%)
- Ammo counter with LOW AMMO / NO AMMO warning
- Kill counter
- Live FPS display (exponential moving average)
- Control hints overlay (first 5 seconds)
- "AREA CLEARED!" banner when all enemies are dead

---

## Project Structure

```
fps_game/
├── fps_game.pro        — Qt project file
├── main.cpp            — Entry point (app + window creation)
├── game.h              — All game data: map, sprite art, structs (no Qt)
│   ├── MAP[24][24]     — Tile-based world map (0=empty, 1=stone, 2=brick, 3=concrete)
│   ├── IMP_SPRITE[][]  — 16×16 pixel-art enemy sprite
│   ├── Player          — pos, dir vector, camera plane, hp, ammo, timers
│   ├── Enemy           — map position, hp, hit flash, alive flag
│   ├── Bullet          — position, velocity (map units/sec), alive flag
│   └── GameState       — owns player, enemy list, bullet list, kills, frame count
├── mainwindow.h        — QWidget class declaration
└── mainwindow.cpp      — All logic + rendering (~550 lines)
    ├── initLevel()     — Player spawn, enemy placement
    ├── gameStep()      — Movement, rotation, bullet update, collision
    ├── fireBullet()    — Spawns projectile in player facing direction
    ├── posValid()      — AABB collision with wall tiles
    ├── renderFloorCeiling() — Perspective-correct floor cast
    ├── renderWalls()   — DDA raycaster → fills z-buffer
    ├── renderSprites() — Billboard sprites with Z-test
    ├── renderWeapon()  — First-person pistol + muzzle flash
    └── renderHUD()     — Crosshair, bars, minimap, text
```

---

## How to Build

1. Open **Qt Creator**
2. **File → Open File or Project** → select `fps_game.pro`
3. Configure with **MinGW** kit (Qt 5.x or 6.x)
4. Press **▶ Run**

**Requirements:** Qt 5.9+ (Widgets module). No external libraries.

---

## Map Layout (24 × 24)

```
111111111111111111111111
1......................1
1......................1
1...2222....2222.......1   ← brick alcoves
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
1...2222....2222.......1
1......................1
1......................1
1.....111111111111.....1   ← wall barrier
1......................1
111111111111111111111111
```
Player spawns at (2.5, 2.5) facing East. Six imp enemies spread around the arena.

---

## Algorithms Used (CG Lab reference)

| Game System | Algorithm |
|---|---|
| Wall rendering | DDA ray marching (same as Assignment 2) |
| Bullet movement | DDA step-based traversal |
| Wall drawing | Bresenham line (pixel helpers) |
| Sprite circles | Midpoint circle algorithm |
| Floor perspective | Inverse camera plane projection |
| Sprite transform | Inverse 2×2 camera matrix |
| Collision | AABB overlap test |
| Sprite sort | Std sort by squared distance |
| Shading | Dot-product lighting (Y-side faces) |
| Fog | Linear distance blend |

---

## Planned — Week 2+
- [ ] Enemy patrol AI (waypoint system)
- [ ] Line-of-sight detection (DDA ray to player)
- [ ] Enemy shoots back
- [ ] Camera scroll (larger maps)
- [ ] Multiple weapon types
- [ ] Shotgun spread (multi-ray fire)
- [ ] Wave system

---

## Team
Group project — Computer Graphics Lab, Sem 5
