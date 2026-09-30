// =============================================================================
//  mainwindow.cpp  —  FPS Raycaster  |  Week 1
//
//  Render order each frame:
//    1. renderFloorCeiling()  — shaded floor + ceiling  (full-screen pass)
//    2. renderWalls()         — DDA raycaster, fills z-buffer
//    3. renderSprites()       — enemy + bullet billboards (z-tested)
//    4. renderWeapon()        — pistol sprite + muzzle flash
//    5. renderHUD()           — crosshair, HP, ammo, FPS  (QPainter text)
// =============================================================================
#include "mainwindow.h"
#include <QPainter>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , m_buf(W, H, QImage::Format_ARGB32)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // Direct pointer into pixel buffer — avoids QImage overhead per pixel
    m_pixels = reinterpret_cast<uint32_t*>(m_buf.bits());
    std::fill(m_zBuf, m_zBuf + W, 1e30f);

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);   // ~60 Hz
    m_clock.start();

    initLevel();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Level setup
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::initLevel()
{
    m_gs = GameState{};

    m_gs.player.posX   = 2.5f;   m_gs.player.posY   = 2.5f;
    m_gs.player.dirX   = 1.0f;   m_gs.player.dirY   = 0.0f;
    m_gs.player.planeX = 0.0f;   m_gs.player.planeY = 0.66f;
    m_gs.player.hp     = 100;    m_gs.player.ammo   = 30;

    // Static enemies placed around the map
    m_gs.enemies = {
        { 5.5f,  5.5f },
        { 19.5f, 5.5f },
        { 5.5f,  18.5f },
        { 19.5f, 18.5f },
        { 12.0f, 7.5f },
        { 12.0f, 16.5f },
    };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game loop tick
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::tick()
{
    double now = m_clock.elapsed();
    float  dt  = float((now - m_lastMs) / 1000.0);
    if (dt > 0.05f) dt = 0.05f;   // cap on focus loss / lag spike
    m_fps    = 0.9 * m_fps + 0.1 * (1.0 / double(dt));
    m_lastMs = now;

    if (!m_gs.gameOver) gameStep(dt);
    render();
    QWidget::update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game logic
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::gameStep(float dt)
{
    Player &pl = m_gs.player;

    // ── Timers ───────────────────────────────────────────────────────────────
    if (pl.attackCooldown > 0) pl.attackCooldown -= dt;
    if (pl.flashTimer     > 0) pl.flashTimer     -= dt;

    // ── Rotation  (← → arrows) ───────────────────────────────────────────────
    const float ROT = 2.2f * dt;
    if (m_keys.count(Qt::Key_Left) || m_keys.count(Qt::Key_Q)) {
        float ox = pl.dirX;
        pl.dirX  =  pl.dirX * cosf(ROT) + pl.dirY * sinf(ROT);
        pl.dirY  = -ox      * sinf(ROT) + pl.dirY * cosf(ROT);
        float op = pl.planeX;
        pl.planeX=  pl.planeX * cosf(ROT) + pl.planeY * sinf(ROT);
        pl.planeY= -op        * sinf(ROT) + pl.planeY * cosf(ROT);
    }
    if (m_keys.count(Qt::Key_Right) || m_keys.count(Qt::Key_E)) {
        float ox = pl.dirX;
        pl.dirX  = pl.dirX * cosf(-ROT) + pl.dirY * sinf(-ROT);
        pl.dirY  = -ox     * sinf(-ROT) + pl.dirY * cosf(-ROT);
        float op = pl.planeX;
        pl.planeX= pl.planeX * cosf(-ROT) + pl.planeY * sinf(-ROT);
        pl.planeY= -op       * sinf(-ROT) + pl.planeY * cosf(-ROT);
    }

    // ── Movement  (WASD) ─────────────────────────────────────────────────────
    const float SPD = 3.5f * dt;
    bool moving = false;

    auto tryMove = [&](float nx, float ny) {
        // Slide: test X and Y separately so you glide along walls
        if (posValid(nx, pl.posY)) pl.posX = nx;
        if (posValid(pl.posX, ny)) pl.posY = ny;
    };

    if (m_keys.count(Qt::Key_W)) {
        tryMove(pl.posX + pl.dirX * SPD, pl.posY + pl.dirY * SPD);
        moving = true;
    }
    if (m_keys.count(Qt::Key_S)) {
        tryMove(pl.posX - pl.dirX * SPD, pl.posY - pl.dirY * SPD);
        moving = true;
    }
    // Strafe: perpendicular = (-dirY, dirX)
    if (m_keys.count(Qt::Key_A)) {
        tryMove(pl.posX - pl.dirY * SPD, pl.posY + pl.dirX * SPD);
        moving = true;
    }
    if (m_keys.count(Qt::Key_D)) {
        tryMove(pl.posX + pl.dirY * SPD, pl.posY - pl.dirX * SPD);
        moving = true;
    }

    // Weapon bob
    if (moving) pl.bobPhase += dt * 5.0f;
    else        pl.bobPhase *= 0.85f;

    // ── Bullet updates ────────────────────────────────────────────────────────
    for (auto &b : m_gs.bullets) {
        if (!b.alive) continue;
        b.x += b.dx * dt;
        b.y += b.dy * dt;

        // Wall hit
        int mx = int(b.x), my = int(b.y);
        if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H || MAP[my][mx] > 0) {
            b.alive = false; continue;
        }

        // Enemy hit (radius check: 0.4 map units)
        for (auto &e : m_gs.enemies) {
            if (!e.alive) continue;
            float dx = b.x - e.x, dy = b.y - e.y;
            if (dx*dx + dy*dy < 0.16f) {   // 0.4² = 0.16
                b.alive    = false;
                e.hp      -= 1;
                e.hitFlash = 0.18f;
                if (e.hp <= 0) { e.alive = false; m_gs.kills++; }
                break;
            }
        }
    }

    // Purge dead bullets
    m_gs.bullets.erase(
        std::remove_if(m_gs.bullets.begin(), m_gs.bullets.end(),
                       [](const Bullet &b){ return !b.alive; }),
        m_gs.bullets.end());

    // Tick enemy flash timers
    for (auto &e : m_gs.enemies)
        if (e.hitFlash > 0) e.hitFlash -= dt;

    // Restart on R
    m_gs.frameNum++;
}

bool MainWindow::posValid(float x, float y) const
{
    const float R = 0.28f;
    auto clear = [](float tx, float ty) {
        int mx = int(tx), my = int(ty);
        if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H) return false;
        return MAP[my][mx] == 0;
    };
    return clear(x-R, y-R) && clear(x+R, y-R) &&
           clear(x-R, y+R) && clear(x+R, y+R);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::keyPressEvent(QKeyEvent *e)
{
    m_keys.insert(e->key());
    if (e->key() == Qt::Key_Space) fireBullet();
    if (e->key() == Qt::Key_R && m_gs.gameOver) initLevel();
}

void MainWindow::keyReleaseEvent(QKeyEvent *e) { m_keys.erase(e->key()); }

void MainWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) fireBullet();
}

void MainWindow::mouseMoveEvent(QMouseEvent *e)
{
    // Mouse look: rotate by how far the cursor drifted from centre
    int cx = width() / 2;
    float dx = float(e->x() - cx);
    if (std::fabs(dx) < 1.0f) return;

    float rot = dx * -0.0015f;   // sensitivity
    Player &pl = m_gs.player;
    float ox = pl.dirX;
    pl.dirX  = pl.dirX * cosf(rot) + pl.dirY * sinf(rot);
    pl.dirY  = -ox     * sinf(rot) + pl.dirY * cosf(rot);
    float op = pl.planeX;
    pl.planeX= pl.planeX * cosf(rot) + pl.planeY * sinf(rot);
    pl.planeY= -op       * sinf(rot) + pl.planeY * cosf(rot);

    // Re-centre cursor so delta resets each frame
    QCursor::setPos(mapToGlobal(QPoint(cx, height() / 2)));
}

void MainWindow::fireBullet()
{
    Player &pl = m_gs.player;
    if (pl.ammo <= 0 || pl.attackCooldown > 0) return;
    pl.ammo--;
    pl.attackCooldown = 0.45f;
    pl.flashTimer     = 0.14f;

    const float SPD = 9.0f;
    Bullet b;
    // Spawn slightly ahead of player centre to avoid self-collision
    b.x  = pl.posX + pl.dirX * 0.4f;
    b.y  = pl.posY + pl.dirY * 0.4f;
    b.dx = pl.dirX * SPD;
    b.dy = pl.dirY * SPD;
    b.alive = true;
    m_gs.bullets.push_back(b);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pixel helpers
// ─────────────────────────────────────────────────────────────────────────────
inline void MainWindow::px(int x, int y, uint32_t c)
{
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
        m_pixels[y * W + x] = c;
}

void MainWindow::fillRect(int x, int y, int w, int h, uint32_t c)
{
    int x0=std::max(x,0), x1=std::min(x+w,W);
    int y0=std::max(y,0), y1=std::min(y+h,H);
    for (int row=y0; row<y1; ++row)
        for (int col=x0; col<x1; ++col)
            m_pixels[row*W+col] = c;
}

void MainWindow::fillCircle(int cx, int cy, int r, uint32_t c)
{
    for (int dy=-r; dy<=r; ++dy)
        for (int dx=-r; dx<=r; ++dx)
            if (dx*dx+dy*dy <= r*r) px(cx+dx, cy+dy, c);
}

void MainWindow::drawLine(int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx=std::abs(x1-x0), sx=x0<x1?1:-1;
    int dy=std::abs(y1-y0), sy=y0<y1?1:-1;
    int err=(dx>dy?dx:-dy)/2;
    for(;;){ px(x0,y0,c); if(x0==x1&&y0==y1) break;
        int e=err; if(e>-dx){err-=dy;x0+=sx;} if(e<dy){err+=dx;y0+=sy;} }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Color helpers
// ─────────────────────────────────────────────────────────────────────────────

// Blend colour toward black based on distance (fog)
uint32_t MainWindow::fogBlend(uint32_t col, float dist)
{
    float f = std::max(0.0f, 1.0f - dist / 14.0f);
    uint8_t r = uint8_t(((col>>16)&0xFF) * f);
    uint8_t g = uint8_t(((col>> 8)&0xFF) * f);
    uint8_t b = uint8_t(( col     &0xFF) * f);
    return (0xFF<<24)|(r<<16)|(g<<8)|b;
}

// Procedural wall pixel at texture coordinates (wx, wy) ∈ [0,1]
uint32_t MainWindow::wallPixel(int type, float wx, float wy, int side, float dist)
{
    int tx = int(wx * 64) & 63;
    int ty = int(wy * 64) & 63;
    uint32_t c = 0xFF888888;

    switch (type) {
    case 1: { // Stone — large horizontal blocks
        bool mortar = (ty % 16 < 2) ||
                      ((ty / 16) % 2 == 0 ? (tx % 32 < 2) : ((tx + 16) % 32 < 2));
        if (mortar) { c = 0xFF555555; break; }
        int n = ((tx*7 + ty*13) ^ tx) & 7;
        uint8_t v = uint8_t(125 + n);
        c = (0xFF<<24)|(v<<16)|(v<<8)|v;
        break;
    }
    case 2: { // Brick — running-bond courses
        bool mortar = (ty % 8 < 1) ||
                      ((ty/8)%2==0 ? (tx % 20 < 2) : ((tx+10) % 20 < 2));
        if (mortar) { c = 0xFF777766; break; }
        int n = ((tx*5 + ty*11) ^ ty) & 15;
        uint8_t br=uint8_t(155+n), bg=uint8_t(72+n/3), bb=uint8_t(55+n/3);
        c = (0xFF<<24)|(br<<16)|(bg<<8)|bb;
        break;
    }
    case 3: { // Concrete pillar — subtle noise
        int n = ((tx*3+ty*7)^(tx+ty)) & 31;
        uint8_t v = uint8_t(90 + n);
        c = (0xFF<<24)|(uint8_t(v*0.75f)<<16)|(uint8_t(v*0.85f)<<8)|v;
        break;
    }}

    // Y-side faces (north/south walls) are ~50% darker → simulates side lighting
    if (side == 1) {
        uint8_t r=(c>>16&0xFF)/2, g=(c>>8&0xFF)/2, b=(c&0xFF)/2;
        c = (0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    return fogBlend(c, dist);
}

// Sample the 16×16 imp sprite at normalised (u,v) ∈ [0,1]
uint32_t MainWindow::sampleImp(float u, float v, bool flash)
{
    int tx = std::min(15, std::max(0, int(u * 16)));
    int ty = std::min(15, std::max(0, int(v * 16)));
    uint32_t c = IMP_SPRITE[ty][tx];
    if ((c >> 24) == 0) return 0;           // transparent
    return flash ? 0xFFFFFFFF : c;           // white on hit
}

// Bullet: orange/gold glowing sphere
uint32_t MainWindow::sampleBullet(float u, float v)
{
    float du = u - 0.5f, dv = v - 0.5f;
    float d  = du*du + dv*dv;
    if (d > 0.25f) return 0;
    float t  = 1.0f - d * 4.0f;
    return (0xFF<<24)|(0xFF<<16)|(uint8_t(170*t+85)<<8)|uint8_t(20*t);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — floor & ceiling
//  Uses perspective-correct floor casting so tiles have correct size at all
//  distances.  The ceiling row is mirrored symmetrically from the floor row.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderFloorCeiling()
{
    const Player &pl = m_gs.player;

    // Left-most and right-most ray directions at the horizon
    float rdX0 = pl.dirX - pl.planeX, rdY0 = pl.dirY - pl.planeY; // leftmost
    float rdX1 = pl.dirX + pl.planeX, rdY1 = pl.dirY + pl.planeY; // rightmost

    for (int y = H/2 + 1; y < H; y++) {
        int   p       = y - H/2;
        float rowDist = float(H / 2) / float(p);  // world-space distance of this floor row

        float stepX = rowDist * (rdX1 - rdX0) / float(W);
        float stepY = rowDist * (rdY1 - rdY0) / float(W);

        float flX = pl.posX + rowDist * rdX0;
        float flY = pl.posY + rowDist * rdY0;

        float fog = std::max(0.0f, 1.0f - rowDist / 14.0f);

        for (int x = 0; x < W; x++) {
            // Checkerboard floor tiles
            bool check = (int(flX) + int(flY)) & 1;
            uint8_t fv = uint8_t((check ? 40 : 26) * fog + 6);
            m_pixels[y * W + x] =
                (0xFF<<24)|(fv<<16)|(uint8_t(fv*0.88f)<<8)|uint8_t(fv*0.65f);

            // Ceiling: mirror row, cooler blue-grey tint
            uint8_t cv = uint8_t(18 * fog + 3);
            m_pixels[(H - 1 - y) * W + x] =
                (0xFF<<24)|(uint8_t(cv*0.65f)<<16)|(uint8_t(cv*0.75f)<<8)|cv;

            flX += stepX;
            flY += stepY;
        }
    }
    // Horizon: pure dark line prevents colour bleeding from floor/ceiling seam
    for (int x = 0; x < W; x++) m_pixels[(H/2)*W+x] = 0xFF060608;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — DDA raycaster (walls)
//  Reference: Lode's Computer Graphics Tutorial (lodev.org)
//  Fills m_zBuf[x] with perpendicular wall distance for sprite Z-testing.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWalls()
{
    const Player &pl = m_gs.player;

    for (int x = 0; x < W; x++) {
        // Camera-space X: -1 (left edge) .. +1 (right edge)
        float camX  = 2.0f * x / float(W) - 1.0f;
        float rdX   = pl.dirX + pl.planeX * camX;
        float rdY   = pl.dirY + pl.planeY * camX;

        int   mapX  = int(pl.posX);
        int   mapY  = int(pl.posY);

        // Stepping distances along the ray per unit map step
        float ddX   = std::fabs(rdX) < 1e-20f ? 1e30f : std::fabs(1.0f / rdX);
        float ddY   = std::fabs(rdY) < 1e-20f ? 1e30f : std::fabs(1.0f / rdY);

        // Initial side distances + step directions
        float sdX, sdY;
        int   stX, stY;
        if (rdX < 0) { stX=-1; sdX=(pl.posX - mapX)          * ddX; }
        else         { stX= 1; sdX=(mapX + 1.0f - pl.posX)   * ddX; }
        if (rdY < 0) { stY=-1; sdY=(pl.posY - mapY)          * ddY; }
        else         { stY= 1; sdY=(mapY + 1.0f - pl.posY)   * ddY; }

        // DDA march
        bool hit  = false;
        int  side = 0;
        for (int step = 0; step < 64 && !hit; step++) {
            if (sdX < sdY) { sdX += ddX; mapX += stX; side = 0; }
            else           { sdY += ddY; mapY += stY; side = 1; }
            if (mapX < 0 || mapX >= MAP_W || mapY < 0 || mapY >= MAP_H) break;
            if (MAP[mapY][mapX] > 0) hit = true;
        }
        if (!hit) { m_zBuf[x] = 1e30f; continue; }

        // Perpendicular distance (eliminates fisheye distortion)
        float dist = (side == 0) ? (sdX - ddX) : (sdY - ddY);
        m_zBuf[x]  = dist;

        // Texture coordinate along wall surface (0..1)
        float wallHit;
        if (side == 0) wallHit = pl.posY + dist * rdY;
        else           wallHit = pl.posX + dist * rdX;
        wallHit -= std::floor(wallHit);

        int lineH   = std::max(1, int(float(H) / dist));
        int drawSt  = std::max(0,   H/2 - lineH/2);
        int drawEnd = std::min(H-1, H/2 + lineH/2);

        int wallType = MAP[mapY][mapX];
        for (int y = drawSt; y <= drawEnd; y++) {
            float texY = float(y - (H/2 - lineH/2)) / float(lineH);
            m_pixels[y * W + x] = wallPixel(wallType, wallHit, texY, side, dist);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — billboard sprites (enemies + bullets)
//  Classic sprite transform from Lode's tutorial, with Z-buffer occlusion.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderSprites()
{
    const Player &pl = m_gs.player;

    struct Sp { float x, y, dist; int type, idx; };
    std::vector<Sp> sprites;

    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[i];
        if (!e.alive) continue;
        float dx = e.x - pl.posX, dy = e.y - pl.posY;
        sprites.push_back({e.x, e.y, dx*dx+dy*dy, 0, i});
    }
    for (int i = 0; i < int(m_gs.bullets.size()); i++) {
        const Bullet &b = m_gs.bullets[i];
        if (!b.alive) continue;
        float dx = b.x - pl.posX, dy = b.y - pl.posY;
        sprites.push_back({b.x, b.y, dx*dx+dy*dy, 1, i});
    }

    // Back-to-front so far sprites don't overdraw close ones
    std::sort(sprites.begin(), sprites.end(),
              [](const Sp &a, const Sp &b){ return a.dist > b.dist; });

    // Camera-space transform matrix (inverse of camera matrix)
    float invDet = 1.0f / (pl.planeX * pl.dirY - pl.dirX * pl.planeY);

    for (const Sp &sp : sprites) {
        float spX = sp.x - pl.posX;
        float spY = sp.y - pl.posY;

        // Transform sprite position into camera space
        float txX =  invDet * ( pl.dirY   * spX - pl.dirX   * spY);
        float txY =  invDet * (-pl.planeY * spX + pl.planeX * spY); // depth

        if (txY < 0.1f) continue;  // behind or too close to camera

        int screenX = int(float(W/2) * (1.0f + txX / txY));

        // Projected height (= width, square sprite)
        int sprH  = std::abs(int(float(H) / txY));
        int sprW  = sprH;
        int sy0   = H/2 - sprH/2;
        int sy1   = H/2 + sprH/2;
        int sx0   = screenX - sprW/2;
        int sx1   = screenX + sprW/2;

        for (int sx = sx0; sx < sx1; sx++) {
            if (sx < 0 || sx >= W) continue;
            if (txY >= m_zBuf[sx]) continue;   // wall is closer → skip column

            float u = float(sx - sx0) / float(sprW);

            for (int sy = std::max(0, sy0); sy < std::min(H, sy1); sy++) {
                float v = float(sy - sy0) / float(sprH);

                uint32_t c;
                if (sp.type == 0) {
                    c = sampleImp(u, v, m_gs.enemies[sp.idx].hitFlash > 0);
                } else {
                    c = sampleBullet(u, v);
                }
                if ((c >> 24) == 0) continue;  // transparent pixel

                m_pixels[sy * W + sx] = fogBlend(c, txY);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — first-person pistol
//  Drawn in screen space (no Z involvement).  Weapon bob via player.bobPhase.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWeapon()
{
    Player &pl = m_gs.player;

    int bobX = int(sinf(pl.bobPhase) * 6.0f);
    int bobY = int(std::fabs(cosf(pl.bobPhase * 0.5f)) * 8.0f);

    // Gun root: bottom-centre of screen, slightly right (dominant hand)
    int rx = W/2 + 80 + bobX;
    int ry = H  - 20 + bobY;   // from bottom

    // ── Barrel ───────────────────────────────────────────────────────────────
    fillRect(rx-5,  ry-115, 10, 60, 0xFF3C3C3C);
    fillRect(rx-3,  ry-115, 6,  60, 0xFF525252);  // highlight
    fillRect(rx-6,  ry-118, 12,  6, 0xFF2A2A2A);  // muzzle block
    fillRect(rx-3,  ry-118, 6,   4, 0xFF111111);  // bore

    // ── Slide ────────────────────────────────────────────────────────────────
    fillRect(rx-18, ry-56, 36, 20, 0xFF303030);
    fillRect(rx-16, ry-54, 32,  4, 0xFF484848);
    fillRect(rx-16, ry-47, 32,  4, 0xFF484848);

    // ── Frame ────────────────────────────────────────────────────────────────
    fillRect(rx-18, ry-37, 36, 15, 0xFF282828);

    // ── Trigger guard ────────────────────────────────────────────────────────
    fillRect(rx-17, ry-23,  3, 16, 0xFF252525);
    fillRect(rx+14, ry-23,  3, 16, 0xFF252525);
    fillRect(rx-17, ry- 8, 34,  3, 0xFF252525);
    fillRect(rx- 4, ry-20,  8,  9, 0xFF3A3A3A);  // trigger

    // ── Grip ─────────────────────────────────────────────────────────────────
    fillRect(rx-15, ry-8, 30, ry+20, 0xFF1E1E1E);
    for (int gr = 0; gr < 5; gr++)
        fillRect(rx-13, ry-4+gr*9, 26, 5, 0xFF272727);

    // ── Hand / arm ────────────────────────────────────────────────────────────
    fillRect(rx-20, ry+35, 40, H-(ry+35), 0xFF6B4226);
    fillRect(rx-24, ry+65, 48, H-(ry+65), 0xFF5A3820);

    // ── Muzzle flash ─────────────────────────────────────────────────────────
    if (pl.flashTimer > 0.0f) {
        float t  = pl.flashTimer / 0.14f;          // 1→0 as it fades
        int   fr = int((10 + rand()%4) * t);
        if (fr > 0) {
            int fx = rx, fy = ry - 118;            // tip of barrel
            fillCircle(fx, fy, fr + 7, 0x55FF6600);
            fillCircle(fx, fy, fr + 3, 0xAAFF9900);
            fillCircle(fx, fy, fr,     0xFFFFDD00);
            fillCircle(fx, fy, fr / 2, 0xFFFFFFFF);

            // Slight screen-wide brightness flash
            int addR = int(30 * t), addG = int(15 * t);
            for (int i = 0; i < W * H; i++) {
                uint32_t p = m_pixels[i];
                uint8_t  r = uint8_t(std::min(255, int((p>>16&0xFF) + addR)));
                uint8_t  g = uint8_t(std::min(255, int((p>> 8&0xFF) + addG)));
                m_pixels[i] = (0xFF<<24)|(r<<16)|(g<<8)|(p&0xFF);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — HUD overlay
//  QPainter is used only for text/font rendering; all geometry is raster.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderHUD()
{
    // ── Crosshair (raster) ────────────────────────────────────────────────────
    const int cx = W/2, cy = H/2;
    const int gap = 6, arm = 11;
    for (int i = 1; i <= 2; i++) {  // two-pixel-thick lines
        drawLine(cx-arm-gap, cy-i+1, cx-gap-1, cy-i+1, 0xBBFFFFFF);
        drawLine(cx+gap,     cy-i+1, cx+arm+gap, cy-i+1,0xBBFFFFFF);
        drawLine(cx-i+1, cy-arm-gap, cx-i+1, cy-gap-1, 0xBBFFFFFF);
        drawLine(cx-i+1, cy+gap,     cx-i+1, cy+arm+gap,0xBBFFFFFF);
    }
    px(cx, cy, 0xFFFF3333);

    // ── Text (QPainter) ───────────────────────────────────────────────────────
    QPainter p(&m_buf);

    // HP bar — top-left
    int hpFill = 180 * m_gs.player.hp / 100;
    fillRect(8, 8, 180, 18, 0x99000000);
    fillRect(8, 8, hpFill, 18,
             m_gs.player.hp > 40 ? 0xFF22CC44 : 0xFFFF3311);
    p.setFont(QFont("Courier New", 11, QFont::Bold));
    p.setPen(Qt::white);
    p.drawText(12, 23, QString("HP %1").arg(m_gs.player.hp));

    // Stats — top-right
    p.fillRect(W-155, 0, 155, 72, QColor(0,0,0,130));
    p.setFont(QFont("Courier New", 12, QFont::Bold));
    p.setPen(QColor(255, 210, 0));
    p.drawText(W-145, 20, QString("AMMO   %1").arg(m_gs.player.ammo));
    p.setPen(QColor(200,200,200));
    p.drawText(W-145, 40, QString("KILLS  %1").arg(m_gs.kills));
    p.setPen(QColor(80, 255, 80));
    p.drawText(W-145, 60, QString("FPS    %1").arg(int(m_fps)));

    // Minimap (top-left corner, 20px per tile, 5×5 around player)
    const int MM = 5, TILE = 8, OX = 8, OY = 34;
    p.fillRect(OX, OY, MM*2*TILE, MM*2*TILE, QColor(0,0,0,160));
    for (int dy = -MM; dy < MM; dy++) {
        for (int dx = -MM; dx < MM; dx++) {
            int mx = int(m_gs.player.posX) + dx;
            int my = int(m_gs.player.posY) + dy;
            if (mx<0||mx>=MAP_W||my<0||my>=MAP_H) continue;
            if (MAP[my][mx]) {
                QColor wc = (MAP[my][mx]==1)?QColor(80,80,80)
                           :(MAP[my][mx]==2)?QColor(120,60,40)
                                            :QColor(50,80,100);
                p.fillRect(OX+(dx+MM)*TILE, OY+(dy+MM)*TILE, TILE-1, TILE-1, wc);
            }
        }
    }
    // Player dot on minimap
    p.fillRect(OX+MM*TILE, OY+MM*TILE, TILE-1, TILE-1, QColor(0,220,180));
    // Enemy dots on minimap
    for (auto &e : m_gs.enemies) {
        if (!e.alive) continue;
        int ex = int(e.x - m_gs.player.posX + MM);
        int ey = int(e.y - m_gs.player.posY + MM);
        if (ex>=0&&ex<MM*2&&ey>=0&&ey<MM*2)
            p.fillRect(OX+ex*TILE, OY+ey*TILE, TILE-1, TILE-1, QColor(220,40,40));
    }

    // Controls hint (first 5 seconds)
    if (m_gs.frameNum < 300) {
        p.fillRect(W/2-230, H-32, 460, 26, QColor(0,0,0,155));
        p.setFont(QFont("Courier New", 10));
        p.setPen(QColor(170,170,170));
        p.drawText(W/2-225, H-12,
                   "WASD: move   A/D: strafe   Q/E or ←→: turn   Space/LMB: fire");
    }

    // "Area cleared" overlay
    int alive = 0;
    for (auto &e : m_gs.enemies) if (e.alive) alive++;
    if (alive == 0) {
        p.fillRect(W/2-200, H/2-50, 400, 80, QColor(0,0,0,190));
        p.setFont(QFont("Courier New", 32, QFont::Bold));
        p.setPen(QColor(50,255,100));
        p.drawText(QRect(W/2-200,H/2-50,400,80), Qt::AlignCenter, "AREA CLEARED!");
    }

    // Low ammo warning
    if (m_gs.player.ammo <= 5 && m_gs.player.ammo > 0) {
        p.setFont(QFont("Courier New", 14, QFont::Bold));
        p.setPen(QColor(255,80,0));
        p.drawText(W/2 - 60, H - 55, "LOW AMMO!");
    }
    if (m_gs.player.ammo == 0) {
        p.setFont(QFont("Courier New", 14, QFont::Bold));
        p.setPen(QColor(255,30,30));
        p.drawText(W/2 - 55, H - 55, "NO AMMO!");
    }

    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Master render (calls all passes in order)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::render()
{
    renderFloorCeiling();   // 1. full-screen floor + ceiling
    renderWalls();          // 2. raycasted walls  + fill z-buffer
    renderSprites();        // 3. billboard enemies + bullets
    renderWeapon();         // 4. first-person gun
    renderHUD();            // 5. crosshair, text, minimap
}

// ─────────────────────────────────────────────────────────────────────────────
//  Qt paint event — single blit of the completed QImage
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawImage(0, 0, m_buf);
}
