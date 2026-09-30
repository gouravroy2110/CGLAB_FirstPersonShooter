// =============================================================================
//  mainwindow.cpp  —  FPS Raycaster  |  Week 1+
//
//  ★ Pure raster graphics: every pixel is written via our own px()/fillRect()/
//    fillCircle()/drawLine() helpers into a raw QImage buffer.
//    QPainter is used ONLY for font/text rendering in the HUD overlay — which
//    itself runs Qt's QRasterPaintEngine (no GPU / no OpenGL).
//
//  Render order per frame:
//    1  renderFloorCeiling()   perspective-correct floor tiles + ceiling
//    2  renderWalls()          DDA raycaster  → fills m_zBuf[]
//    3  renderSprites()        enemy + bullet billboards (Z-tested, with trail)
//    4  renderWeaponAndArms()  two arms + detailed pistol + muzzle flash
//    5  renderDamageVignette() red screen-edge flash when player is hit
//    6  renderHUD()            crosshair · HP · stamina · ammo · minimap · FPS
//    7  renderLevelBanner()    "LEVEL COMPLETE" / "LEVEL X" transitions
//    8  renderPauseMenu()      dim overlay + resume / exit options
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
    m_pixels = reinterpret_cast<uint32_t*>(m_buf.bits());
    std::fill(m_zBuf, m_zBuf + W, 1e30f);

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);
    m_clock.start();
    initGame();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game initialisation
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::initGame()
{
    m_gs         = GameState{};
    m_gs.player.posX  = 2.5f;  m_gs.player.posY   = 2.5f;
    m_gs.player.dirX  = 1.0f;  m_gs.player.dirY   = 0.0f;
    m_gs.player.planeX= 0.0f;  m_gs.player.planeY = 0.66f;
    m_gs.player.hp    = 100;   m_gs.player.ammo   = 30;
    m_gs.player.stamina = 100.f;
    loadLevel(1);
}

void MainWindow::loadLevel(int lv)
{
    int idx = std::min(lv, 5);
    const LevelCfg &cfg = LEVEL_CFG[idx];

    m_gs.level              = lv;
    m_gs.killsThisLevel     = 0;
    m_gs.killsNeeded        = cfg.killsNeeded;
    m_gs.levelComplete      = false;
    m_gs.levelTransTimer    = 0.f;
    m_gs.enemyBulletSpeed   = cfg.bulletSpeed;
    m_gs.enemyFireRate      = cfg.fireCooldown;
    m_gs.bullets.clear();

    // Partial heal + ammo refill between levels
    if (lv > 1) {
        m_gs.player.hp   = std::min(100, m_gs.player.hp + 30);
        m_gs.player.ammo = std::min(30,  m_gs.player.ammo + 20);
        m_gs.player.damageFlash = 0.f;
    }

    int numE = std::min(cfg.numEnemies, 12);
    m_gs.enemies.clear();
    for (int i = 0; i < numE; i++) {
        Enemy e;
        e.x           = SPAWN_PTS[i][0];
        e.y           = SPAWN_PTS[i][1];
        e.hp          = cfg.enemyHP + (lv > 5 ? (lv-5)*2 : 0);
        e.aggroRange  = cfg.aggroRange;
        e.fireCooldown= cfg.fireCooldown;
        e.alive       = true;
        m_gs.enemies.push_back(e);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game loop
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::tick()
{
    double now = m_clock.elapsed();
    m_dt  = float((now - m_lastMs) / 1000.0);
    if (m_dt > 0.05f) m_dt = 0.05f;
    m_fps    = 0.9 * m_fps + 0.1 / double(m_dt);
    m_lastMs = now;

    if (!m_paused && !m_gs.gameOver) gameStep(m_dt);
    render();
    QWidget::update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game logic
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::gameStep(float dt)
{
    Player &pl = m_gs.player;

    // ── Level transition countdown ────────────────────────────────────────────
    if (m_gs.levelComplete) {
        m_gs.levelTransTimer -= dt;
        if (m_gs.levelTransTimer <= 0.f) loadLevel(m_gs.level + 1);
        return;   // freeze enemies during banner
    }

    // ── Timers ───────────────────────────────────────────────────────────────
    if (pl.attackCooldown > 0) pl.attackCooldown -= dt;
    if (pl.flashTimer     > 0) pl.flashTimer     -= dt;
    if (pl.damageFlash    > 0) pl.damageFlash    -= dt;

    // ── Rotation  (arrow keys / Q·E) ─────────────────────────────────────────
    const float ROT = 2.2f * dt;
    auto rotate = [&](float a){
        float ox=pl.dirX, op=pl.planeX;
        pl.dirX  = pl.dirX  *cosf(a)+pl.dirY  *sinf(a);
        pl.dirY  =-ox       *sinf(a)+pl.dirY  *cosf(a);
        pl.planeX= pl.planeX*cosf(a)+pl.planeY*sinf(a);
        pl.planeY=-op       *sinf(a)+pl.planeY*cosf(a);
    };
    if (m_keys.count(Qt::Key_Left) || m_keys.count(Qt::Key_Q))  rotate( ROT);
    if (m_keys.count(Qt::Key_Right)|| m_keys.count(Qt::Key_E))  rotate(-ROT);

    // ── Sprint + stamina ──────────────────────────────────────────────────────
    bool moving = m_keys.count(Qt::Key_W)||m_keys.count(Qt::Key_S)||
                  m_keys.count(Qt::Key_A)||m_keys.count(Qt::Key_D);
    bool wantSprint = moving && m_keys.count(Qt::Key_Shift) && pl.stamina > 0;
    pl.sprinting = wantSprint;
    if (wantSprint)
        pl.stamina = std::max(0.f, pl.stamina - 45.f * dt);
    else
        pl.stamina = std::min(100.f, pl.stamina + 28.f * dt);

    // ── Movement  (WASD + strafe) ─────────────────────────────────────────────
    const float SPD = (pl.sprinting ? 5.8f : 3.5f) * dt;
    auto tryMove = [&](float nx, float ny){
        if (posValid(nx, pl.posY)) pl.posX = nx;
        if (posValid(pl.posX, ny)) pl.posY = ny;
    };
    if (m_keys.count(Qt::Key_W)) tryMove(pl.posX+pl.dirX*SPD, pl.posY+pl.dirY*SPD);
    if (m_keys.count(Qt::Key_S)) tryMove(pl.posX-pl.dirX*SPD, pl.posY-pl.dirY*SPD);
    if (m_keys.count(Qt::Key_A)) tryMove(pl.posX-pl.dirY*SPD, pl.posY+pl.dirX*SPD);
    if (m_keys.count(Qt::Key_D)) tryMove(pl.posX+pl.dirY*SPD, pl.posY-pl.dirX*SPD);

    // Weapon bob
    if (moving) pl.bobPhase += dt * (pl.sprinting ? 8.f : 5.f);
    else        pl.bobPhase *= 0.88f;

    // ── Enemy AI: aggro + fire ────────────────────────────────────────────────
    for (auto &e : m_gs.enemies) {
        if (!e.alive) continue;
        if (e.fireCooldown > 0) { e.fireCooldown -= dt; continue; }

        float dx = pl.posX - e.x, dy = pl.posY - e.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < e.aggroRange && hasLOS(e.x, e.y, pl.posX, pl.posY)) {
            enemyFire(e);
            e.fireCooldown = m_gs.enemyFireRate;
        }
    }

    // ── Bullet updates ────────────────────────────────────────────────────────
    for (auto &b : m_gs.bullets) {
        if (!b.alive) continue;
        b.x += b.dx * dt;
        b.y += b.dy * dt;

        int mx = int(b.x), my = int(b.y);
        if (mx<0||mx>=MAP_W||my<0||my>=MAP_H||MAP[my][mx]>0){
            b.alive=false; continue;
        }

        if (b.owner == PLAYER_SHOT) {
            for (auto &e : m_gs.enemies) {
                if (!e.alive) continue;
                float dx=b.x-e.x, dy=b.y-e.y;
                if (dx*dx+dy*dy < 0.18f) {
                    b.alive=false; e.hp--; e.hitFlash=0.18f;
                    if (e.hp<=0){ e.alive=false; m_gs.kills++; m_gs.killsThisLevel++;
                                  m_gs.score += 100*m_gs.level; }
                    break;
                }
            }
        } else { // ENEMY_SHOT
            float dx=b.x-pl.posX, dy=b.y-pl.posY;
            if (dx*dx+dy*dy < 0.15f) {
                b.alive=false;
                pl.hp -= 10 + 4*(m_gs.level-1);
                pl.damageFlash = 0.45f;
                if (pl.hp <= 0) { pl.hp=0; m_gs.gameOver=true; }
            }
        }
    }

    // Purge dead bullets
    m_gs.bullets.erase(
        std::remove_if(m_gs.bullets.begin(), m_gs.bullets.end(),
                       [](const Bullet &b){ return !b.alive; }),
        m_gs.bullets.end());

    for (auto &e : m_gs.enemies)
        if (e.hitFlash > 0) e.hitFlash -= dt;

    // ── Level complete check ───────────────────────────────────────────────────
    if (!m_gs.levelComplete && m_gs.killsThisLevel >= m_gs.killsNeeded) {
        m_gs.levelComplete   = true;
        m_gs.levelTransTimer = 3.5f;
        m_gs.score          += 500 * m_gs.level;
    }

    m_gs.frameNum++;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::posValid(float x, float y) const
{
    const float R = 0.28f;
    auto ok=[](float tx,float ty){
        int mx=int(tx),my=int(ty);
        return mx>=0&&mx<MAP_W&&my>=0&&my<MAP_H&&MAP[my][mx]==0;
    };
    return ok(x-R,y-R)&&ok(x+R,y-R)&&ok(x-R,y+R)&&ok(x+R,y+R);
}

bool MainWindow::hasLOS(float x0,float y0,float x1,float y1) const
{
    float dx=x1-x0, dy=y1-y0;
    float dist=sqrtf(dx*dx+dy*dy);
    if (dist<0.1f) return true;
    float nx=dx/dist*0.12f, ny=dy/dist*0.12f;
    float cx=x0+nx, cy=y0+ny;
    int steps=int(dist/0.12f);
    for(int i=0;i<steps;i++){
        int mx=int(cx),my=int(cy);
        if(mx<0||mx>=MAP_W||my<0||my>=MAP_H) return false;
        if(MAP[my][mx]>0) return false;
        cx+=nx; cy+=ny;
    }
    return true;
}

void MainWindow::fireBullet()
{
    Player &pl=m_gs.player;
    if(pl.ammo<=0||pl.attackCooldown>0) return;
    pl.ammo--; pl.attackCooldown=0.45f; pl.flashTimer=0.14f;
    Bullet b;
    b.x=pl.posX+pl.dirX*0.4f; b.y=pl.posY+pl.dirY*0.4f;
    b.dx=pl.dirX*9.f; b.dy=pl.dirY*9.f;
    b.owner=PLAYER_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}

void MainWindow::enemyFire(Enemy &e)
{
    float dx=m_gs.player.posX-e.x, dy=m_gs.player.posY-e.y;
    float dist=sqrtf(dx*dx+dy*dy);
    if(dist<0.1f) return;
    Bullet b;
    b.x=e.x; b.y=e.y;
    float spd=m_gs.enemyBulletSpeed;
    b.dx=(dx/dist)*spd; b.dy=(dy/dist)*spd;
    b.owner=ENEMY_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::keyPressEvent(QKeyEvent *e)
{
    m_keys.insert(e->key());
    if (e->key()==Qt::Key_Space) fireBullet();

    if (e->key()==Qt::Key_Escape) {
        if (m_paused)
            QApplication::quit();       // second ESC → exit
        else
            m_paused = true;            // first ESC → pause
    }
    if (e->key()==Qt::Key_R) {
        if (m_paused)   m_paused=false;
        if (m_gs.gameOver){ initGame(); m_paused=false; }
    }
    if (e->key()==Qt::Key_Q && m_paused) QApplication::quit();
    if (e->key()==Qt::Key_Q && m_gs.gameOver) QApplication::quit();
}
void MainWindow::keyReleaseEvent(QKeyEvent *e){ m_keys.erase(e->key()); }
void MainWindow::mousePressEvent(QMouseEvent *e){
    if(e->button()==Qt::LeftButton) fireBullet();
}
void MainWindow::mouseMoveEvent(QMouseEvent *e)
{
    if(m_paused||m_gs.gameOver) return;
    int cx=width()/2;
    float dx=float(e->x()-cx);
    if(std::fabs(dx)<1.f) return;
    float rot=dx*-0.0015f;
    Player &pl=m_gs.player;
    float ox=pl.dirX, op=pl.planeX;
    pl.dirX  = pl.dirX *cosf(rot)+pl.dirY *sinf(rot);
    pl.dirY  =-ox      *sinf(rot)+pl.dirY *cosf(rot);
    pl.planeX= pl.planeX*cosf(rot)+pl.planeY*sinf(rot);
    pl.planeY=-op       *sinf(rot)+pl.planeY*cosf(rot);
    QCursor::setPos(mapToGlobal(QPoint(cx,height()/2)));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Color helpers
// ─────────────────────────────────────────────────────────────────────────────
uint32_t MainWindow::fogBlend(uint32_t c,float dist)
{
    float f=std::max(0.f,1.f-dist/14.f);
    return (0xFF<<24)|(uint8_t(((c>>16)&0xFF)*f)<<16)
                     |(uint8_t(((c>>8 )&0xFF)*f)<<8)
                     | uint8_t(( c     &0xFF)*f);
}
uint32_t MainWindow::dimColor(uint32_t c,float b)
{
    if(b>=1.f) return c;
    uint8_t a=c>>24;
    return (a<<24)|(uint8_t(((c>>16)&0xFF)*b)<<16)
                  |(uint8_t(((c>>8 )&0xFF)*b)<<8)
                  | uint8_t(( c     &0xFF)*b);
}
uint32_t MainWindow::wallPixel(int type,float wx,float wy,int side,float dist)
{
    int tx=int(wx*64)&63, ty=int(wy*64)&63;
    uint32_t c=0xFF888888;
    switch(type){
    case 1:{
        bool m=(ty%16<2)||((ty/16)%2==0?(tx%32<2):((tx+16)%32<2));
        if(m){c=0xFF555555;break;}
        int n=((tx*7+ty*13)^tx)&7;
        uint8_t v=uint8_t(125+n); c=(0xFF<<24)|(v<<16)|(v<<8)|v; break;}
    case 2:{
        bool m=(ty%8<1)||((ty/8)%2==0?(tx%20<2):((tx+10)%20<2));
        if(m){c=0xFF777766;break;}
        int n=((tx*5+ty*11)^ty)&15;
        c=(0xFF<<24)|(uint8_t(155+n)<<16)|(uint8_t(72+n/3)<<8)|uint8_t(55+n/3); break;}
    case 3:{
        int n=((tx*3+ty*7)^(tx+ty))&31; uint8_t v=uint8_t(90+n);
        c=(0xFF<<24)|(uint8_t(v*.75f)<<16)|(uint8_t(v*.85f)<<8)|v; break;}
    }
    if(side==1){uint8_t r=(c>>16&0xFF)/2,g=(c>>8&0xFF)/2,b=(c&0xFF)/2;
                c=(0xFF<<24)|(r<<16)|(g<<8)|b;}
    return fogBlend(c,dist);
}
uint32_t MainWindow::sampleImp(float u,float v,bool flash)
{
    int tx=std::min(15,std::max(0,int(u*16)));
    int ty=std::min(15,std::max(0,int(v*16)));
    uint32_t c=IMP_SPRITE[ty][tx];
    if((c>>24)==0) return 0;
    return flash?0xFFFFFFFF:c;
}
uint32_t MainWindow::samplePlayerBullet(float u,float v)
{
    float du=u-.5f,dv=v-.5f, d=du*du+dv*dv;
    if(d>.22f) return 0;
    float t=1.f-d*4.5f;
    return (0xFF<<24)|(0xFF<<16)|(uint8_t(180*t+75)<<8)|uint8_t(10*t);
}
uint32_t MainWindow::sampleEnemyBullet(float u,float v)
{
    float du=u-.5f,dv=v-.5f, d=du*du+dv*dv;
    if(d>.22f) return 0;
    float t=1.f-d*4.5f;
    // Red-hot core → dark red glow
    return (0xFF<<24)|(uint8_t(220*t+35)<<16)|(uint8_t(30*t)<<8)|uint8_t(180*t);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pixel helpers (pure raster — direct writes to m_pixels)
// ─────────────────────────────────────────────────────────────────────────────
inline void MainWindow::px(int x,int y,uint32_t c)
{ if((unsigned)x<(unsigned)W&&(unsigned)y<(unsigned)H) m_pixels[y*W+x]=c; }

void MainWindow::fillRect(int x,int y,int w,int h,uint32_t c)
{
    int x0=std::max(x,0),x1=std::min(x+w,W);
    int y0=std::max(y,0),y1=std::min(y+h,H);
    for(int r=y0;r<y1;++r) for(int col=x0;col<x1;++col) m_pixels[r*W+col]=c;
}
void MainWindow::fillCircle(int cx,int cy,int r,uint32_t c)
{ for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx) if(dx*dx+dy*dy<=r*r) px(cx+dx,cy+dy,c); }
void MainWindow::drawLine(int x0,int y0,int x1,int y1,uint32_t c)
{
    int dx=std::abs(x1-x0),sx=x0<x1?1:-1;
    int dy=std::abs(y1-y0),sy=y0<y1?1:-1;
    int err=(dx>dy?dx:-dy)/2;
    for(;;){ px(x0,y0,c); if(x0==x1&&y0==y1) break;
        int e=err; if(e>-dx){err-=dy;x0+=sx;} if(e<dy){err+=dx;y0+=sy;} }
}

// ─────────────────────────────────────────────────────────────────────────────
//  1. Floor & Ceiling — perspective-correct floor casting
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderFloorCeiling()
{
    const Player &pl=m_gs.player;
    float rdX0=pl.dirX-pl.planeX, rdY0=pl.dirY-pl.planeY;
    float rdX1=pl.dirX+pl.planeX, rdY1=pl.dirY+pl.planeY;

    for(int y=H/2+1;y<H;y++){
        int   p=y-H/2;
        float rowDist=float(H/2)/float(p);
        float sX=rowDist*(rdX1-rdX0)/float(W), sY=rowDist*(rdY1-rdY0)/float(W);
        float fX=pl.posX+rowDist*rdX0, fY=pl.posY+rowDist*rdY0;
        float fog=std::max(0.f,1.f-rowDist/14.f);

        for(int x=0;x<W;x++){
            bool check=(int(fX)+int(fY))&1;
            uint8_t fv=uint8_t((check?42:26)*fog+6);
            m_pixels[y*W+x]=(0xFF<<24)|(fv<<16)|(uint8_t(fv*.88f)<<8)|uint8_t(fv*.65f);
            uint8_t cv=uint8_t(16*fog+3);
            m_pixels[(H-1-y)*W+x]=(0xFF<<24)|(uint8_t(cv*.65f)<<16)|(uint8_t(cv*.75f)<<8)|cv;
            fX+=sX; fY+=sY;
        }
    }
    for(int x=0;x<W;x++) m_pixels[(H/2)*W+x]=0xFF060608;
}

// ─────────────────────────────────────────────────────────────────────────────
//  2. Walls — DDA raycaster, fills m_zBuf[]
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWalls()
{
    const Player &pl=m_gs.player;
    for(int x=0;x<W;x++){
        float camX=2.f*x/float(W)-1.f;
        float rdX=pl.dirX+pl.planeX*camX, rdY=pl.dirY+pl.planeY*camX;
        int mapX=int(pl.posX), mapY=int(pl.posY);
        float ddX=std::fabs(rdX)<1e-20f?1e30f:std::fabs(1.f/rdX);
        float ddY=std::fabs(rdY)<1e-20f?1e30f:std::fabs(1.f/rdY);
        float sdX,sdY; int stX,stY;
        if(rdX<0){stX=-1;sdX=(pl.posX-mapX)*ddX;}else{stX=1;sdX=(mapX+1.f-pl.posX)*ddX;}
        if(rdY<0){stY=-1;sdY=(pl.posY-mapY)*ddY;}else{stY=1;sdY=(mapY+1.f-pl.posY)*ddY;}
        bool hit=false; int side=0;
        for(int g=0;g<64&&!hit;g++){
            if(sdX<sdY){sdX+=ddX;mapX+=stX;side=0;}else{sdY+=ddY;mapY+=stY;side=1;}
            if(mapX<0||mapX>=MAP_W||mapY<0||mapY>=MAP_H) break;
            if(MAP[mapY][mapX]>0) hit=true;
        }
        if(!hit){m_zBuf[x]=1e30f;continue;}
        float dist=(side==0)?(sdX-ddX):(sdY-ddY);
        m_zBuf[x]=dist;
        float wh; if(side==0) wh=pl.posY+dist*rdY; else wh=pl.posX+dist*rdX;
        wh-=std::floor(wh);
        int lh=std::max(1,int(float(H)/dist));
        int ds=std::max(0,H/2-lh/2), de=std::min(H-1,H/2+lh/2);
        int wt=MAP[mapY][mapX];
        for(int y=ds;y<=de;y++){
            float wy=float(y-(H/2-lh/2))/float(lh);
            m_pixels[y*W+x]=wallPixel(wt,wh,wy,side,dist);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  3. Sprites — enemies + bullets with motion trail (Z-tested billboard)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderSprites()
{
    const Player &pl=m_gs.player;
    enum SpType{SPR_ENEMY,SPR_PBULLET,SPR_EBULLET};
    struct Sp{float x,y,dx,dy,dist;SpType t;int idx;};
    std::vector<Sp> sprites;

    for(int i=0;i<int(m_gs.enemies.size());i++){
        const Enemy &e=m_gs.enemies[i]; if(!e.alive) continue;
        float dx=e.x-pl.posX,dy=e.y-pl.posY;
        sprites.push_back({e.x,e.y,0,0,dx*dx+dy*dy,SPR_ENEMY,i});
    }
    for(int i=0;i<int(m_gs.bullets.size());i++){
        const Bullet &b=m_gs.bullets[i]; if(!b.alive) continue;
        float dx=b.x-pl.posX,dy=b.y-pl.posY;
        SpType st=b.owner==PLAYER_SHOT?SPR_PBULLET:SPR_EBULLET;
        sprites.push_back({b.x,b.y,b.dx,b.dy,dx*dx+dy*dy,st,i});
    }
    std::sort(sprites.begin(),sprites.end(),[](const Sp&a,const Sp&b){return a.dist>b.dist;});

    float invDet=1.f/(pl.planeX*pl.dirY-pl.dirX*pl.planeY);

    // Helper lambda to draw one sprite instance (possibly offset for trail)
    auto drawAt=[&](float wx,float wy,SpType type,int idx,float bright,float wScale){
        float spX=wx-pl.posX, spY=wy-pl.posY;
        float txX= invDet*( pl.dirY*spX-pl.dirX*spY);
        float txY= invDet*(-pl.planeY*spX+pl.planeX*spY);
        if(txY<0.05f) return;

        int scrX=int(float(W/2)*(1.f+txX/txY));
        int sprH=std::abs(int(float(H)/txY));
        int sprW=int(sprH*wScale);   // <1 for bullets = narrow elongated shape
        int sy0=H/2-sprH/2, sy1=H/2+sprH/2;
        int sx0=scrX-sprW/2, sx1=scrX+sprW/2;

        for(int sx=sx0;sx<sx1;sx++){
            if(sx<0||sx>=W) continue;
            if(txY>=m_zBuf[sx]) continue;
            float u=float(sx-sx0)/float(sprW);
            for(int sy=std::max(0,sy0);sy<std::min(H,sy1);sy++){
                float v=float(sy-sy0)/float(sprH);
                uint32_t c=0;
                if(type==SPR_ENEMY)   c=sampleImp(u,v,m_gs.enemies[idx].hitFlash>0);
                else if(type==SPR_PBULLET) c=samplePlayerBullet(u,v);
                else                       c=sampleEnemyBullet(u,v);
                if((c>>24)==0) continue;
                c=dimColor(c,bright);
                m_pixels[sy*W+sx]=fogBlend(c,txY);
            }
        }
    };

    for(const Sp &sp:sprites){
        if(sp.t==SPR_ENEMY){
            drawAt(sp.x,sp.y,sp.t,sp.idx,1.f,1.f);
        } else {
            // 3-pass trail: current pos (full) + 2 ghost positions (faded, narrower)
            float spd=sqrtf(sp.dx*sp.dx+sp.dy*sp.dy);
            for(int pass=0;pass<3;pass++){
                float trailT=pass*0.055f;           // seconds back in time
                float gx=sp.x - sp.dx*trailT;
                float gy=sp.y - sp.dy*trailT;
                float bright=1.f-pass*0.35f;
                float wScale=0.45f-pass*0.1f;       // thin elongated bullet
                (void)spd;
                drawAt(gx,gy,sp.t,sp.idx,bright,wScale);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  4. Weapon & Arms — detailed pistol + both forearms (partial character)
//     ALL drawing uses fillRect / fillCircle / drawLine  (raster only)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWeaponAndArms()
{
    Player &pl=m_gs.player;
    float bobX=sinf(pl.bobPhase)*(pl.sprinting?9.f:5.f);
    float bobY=fabsf(cosf(pl.bobPhase*.5f))*(pl.sprinting?12.f:7.f);
    float breathe=sinf(m_gs.frameNum*.018f)*1.8f;

    // ── Gun root (bottom-right quadrant) ─────────────────────────────────────
    int rx=W/2+72+int(bobX);
    int ry=H-8+int(bobY+breathe);  // ry is the bottom-most reference

    // ─── LEFT ARM (support / guide hand) ─────────────────────────────────────
    int lx=W/2-100+int(bobX*.4f);
    int ly=ry-90;   // roughly at barrel height

    // Sleeve (tactical dark olive)
    fillRect(lx-22,ly+28,44,H-(ly+28),0xFF2C3020);
    fillRect(lx-24,ly+24,48,H-(ly+24),0xFF252818);
    fillRect(lx-22,ly+28,5,H-(ly+28), 0xFF3A4028);  // left-edge highlight
    // Forearm skin
    fillRect(lx-18,ly+8,36,22,0xFF7A4C2A);
    // Palm (support hand, facing up under barrel)
    fillRect(lx-20,ly-4,40,14,0xFF7A4C2A);
    fillRect(lx-22,ly-4,6,10,0xFF6B3D1A);   // thumb
    // Fingers visible curling over barrel
    for(int f=0;f<4;f++){
        fillRect(lx-14+f*9,ly-13,7,11,0xFF7A4C2A);
        fillRect(lx-14+f*9,ly-15,7, 3,0xFF5A3010);  // fingernail
    }

    // ─── GUN ─────────────────────────────────────────────────────────────────

    // Muzzle compensator / brake (outermost tip)
    fillRect(rx-6,ry-138,12,7,0xFF2A2A32);
    fillRect(rx-4,ry-136,8, 4,0xFF101012);   // port holes

    // Barrel (long thin column behind brake)
    fillRect(rx-5,ry-131,10,67,0xFF3C3C42);
    fillRect(rx-3,ry-131, 6,67,0xFF525260);  // top-face highlight
    // Underbarrel tactical rail (three notches)
    for(int n=0;n<3;n++) fillRect(rx-5,ry-115+n*14,10,4,0xFF1E1E26);

    // Front sight post
    fillRect(rx-2,ry-140,4,9,0xFF252530);
    fillRect(rx-1,ry-138,2,4,0xFFCCCCDD);   // white dot insert

    // Slide (upper receiver)
    fillRect(rx-18,ry-65,36,30,0xFF35353E); // body
    fillRect(rx-18,ry-65,36, 2,0xFF4A4A58); // top highlight
    fillRect(rx-18,ry-65, 2,30,0xFF4A4A58); // left highlight
    fillRect(rx+14,ry-65, 4,30,0xFF1C1C22); // right shadow
    // Rear serrations on slide
    for(int s=0;s<6;s++) fillRect(rx+2+s*2,ry-62,1,20,0xFF272732);
    // Ejection port cut-out (right side)
    fillRect(rx+5,ry-59,9,15,0xFF18181E);
    // Rear sight (U-notch)
    fillRect(rx-5,ry-67,12,5,0xFF252530);
    fillRect(rx-4,ry-70, 3,5,0xFF484858);   // left blade
    fillRect(rx+3,ry-70, 3,5,0xFF484858);   // right blade
    fillRect(rx-1,ry-70, 2,2,0xFFCCCCDD);   // tritium dot
    // Hammer exposed at rear
    fillRect(rx+14,ry-67,5,8,0xFF2C2C38);
    fillRect(rx+13,ry-69,7,4,0xFF3C3C4A);   // hammer spur

    // Frame / lower receiver
    fillRect(rx-18,ry-36,36,16,0xFF2E2E38);
    fillRect(rx-18,ry-36,36, 2,0xFF3E3E4A);
    fillRect(rx-18,ry-36, 2,16,0xFF3E3E4A);
    // Mag release button (left)
    fillRect(rx-18,ry-28, 3, 7,0xFF404052);
    // Trigger guard (3-sided)
    fillRect(rx-17,ry-21, 3,18,0xFF262630);  // left
    fillRect(rx+13,ry-21, 4,18,0xFF262630);  // right
    fillRect(rx-17,ry- 4,34, 3,0xFF262630);  // bottom
    // Trigger
    fillRect(rx- 3,ry-18, 6,11,0xFF3A3A48);
    fillRect(rx- 2,ry-16, 4, 7,0xFF4E4E60);
    // Grip body
    fillRect(rx-16,ry- 2,30,40,0xFF22222E);
    fillRect(rx-16,ry- 2, 3,40,0xFF2C2C3A);  // left edge
    fillRect(rx+12,ry- 2, 2,40,0xFF181820);  // right shadow
    // Grip texture lines
    for(int t=0;t<6;t++) fillRect(rx-14,ry+1+t*6,26,4,0xFF28283A);
    // Magazine base plate
    fillRect(rx-15,ry+37,29,5,0xFF1A1A24);

    // ─── RIGHT ARM (trigger hand) ─────────────────────────────────────────────
    // Wrist / hand wrapping grip (skin)
    fillRect(rx-20,ry+41,40,28,0xFF7A4C2A);
    fillRect(rx-22,ry+44, 8,20,0xFF7A4C2A);  // thumb visible left
    // Knuckles
    for(int k=0;k<4;k++) fillRect(rx-15+k*8,ry+39,6,5,0xFF6B3D1A);
    // Sleeve
    fillRect(rx-20,ry+68,40,H-(ry+68),0xFF2C3020);
    fillRect(rx-22,ry+70,44,H-(ry+70),0xFF252818);
    fillRect(rx-20,ry+68, 5,H-(ry+68),0xFF3A4028);

    // ─── MUZZLE FLASH (raster only — fillCircle + drawLine) ──────────────────
    if(pl.flashTimer>0.f){
        float t=pl.flashTimer/0.14f;
        int fr=int((10+rand()%5)*t);
        if(fr>0){
            int fx=rx, fy=ry-141;
            // Radial rays (6 directions)
            for(int ray=0;ray<6;ray++){
                float a=ray*3.14159f/3.f;
                drawLine(fx,fy,fx+int(cosf(a)*(fr+10)),fy+int(sinf(a)*(fr+10)),0x99FF6600);
            }
            fillCircle(fx,fy,fr+5,0x55FF7700);
            fillCircle(fx,fy,fr+2,0xCCFFAA00);
            fillCircle(fx,fy,fr,  0xFFFFEE00);
            fillCircle(fx,fy,fr/2,0xFFFFFFFF);
            // Screen-wide brightness flash
            int addR=int(32*t),addG=int(16*t);
            for(int i=0;i<W*H;i++){
                uint32_t p=m_pixels[i];
                uint8_t r=uint8_t(std::min(255,int((p>>16&0xFF)+addR)));
                uint8_t g=uint8_t(std::min(255,int((p>>8 &0xFF)+addG)));
                m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|(p&0xFF);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  5. Damage vignette — red screen edges when player is hit
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderDamageVignette()
{
    if(m_gs.player.damageFlash<=0.f) return;
    float t=m_gs.player.damageFlash/0.45f;
    int depth=int(90*t);
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        int d=std::min({x,y,W-1-x,H-1-y});
        if(d<depth){
            float s=(1.f-float(d)/float(depth))*t;
            uint32_t &p=m_pixels[y*W+x];
            uint8_t r=uint8_t(std::min(255,int((p>>16&0xFF)+int(190*s))));
            p=(0xFF<<24)|(r<<16)|(p&0x00FFFF);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  6. HUD — crosshair, HP/stamina/ammo bars, minimap, FPS
//     QPainter is used ONLY for drawText (font rasterization).
//     All bars / crosshair are drawn with our own raster helpers above.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderHUD()
{
    // ── Crosshair (raster) ────────────────────────────────────────────────────
    int cx=W/2,cy=H/2,gap=6,arm=11;
    for(int i=0;i<2;i++){
        drawLine(cx-arm-gap,cy-i+1,cx-gap-1,  cy-i+1, 0xBBFFFFFF);
        drawLine(cx+gap,    cy-i+1,cx+arm+gap, cy-i+1, 0xBBFFFFFF);
        drawLine(cx-i+1,cy-arm-gap,cx-i+1,cy-gap-1, 0xBBFFFFFF);
        drawLine(cx-i+1,cy+gap,    cx-i+1,cy+arm+gap,0xBBFFFFFF);
    }
    px(cx,cy,0xFFFF3333);

    // ── Bars (raster) ─────────────────────────────────────────────────────────
    // HP
    int hpFill=180*m_gs.player.hp/100;
    fillRect(8,8,180,16,0x99000000);
    fillRect(8,8,hpFill,16,m_gs.player.hp>40?0xFF22CC44:0xFFFF3311);
    // Stamina (sprint fuel)
    int stFill=int(120*m_gs.player.stamina/100.f);
    fillRect(8,27,120,8,0x99000000);
    fillRect(8,27,stFill,8,m_gs.player.sprinting?0xFFFFCC00:0xFF4488FF);

    // ── Text (QPainter — software font rasterizer, no GPU) ───────────────────
    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",11,QFont::Bold));
    p.setPen(Qt::white);
    p.drawText(12,22,QString("HP  %1").arg(m_gs.player.hp));
    p.setPen(m_gs.player.sprinting?QColor(255,204,0):QColor(68,136,255));
    p.drawText(12,40,QString("STM %1%").arg(int(m_gs.player.stamina)));

    // Top-right panel
    p.fillRect(W-160,0,160,86,QColor(0,0,0,130));
    p.setFont(QFont("Courier New",12,QFont::Bold));
    p.setPen(QColor(255,210,0));
    p.drawText(W-150,20,QString("AMMO   %1").arg(m_gs.player.ammo));
    p.setPen(QColor(200,200,200));
    p.drawText(W-150,40,QString("KILLS  %1").arg(m_gs.kills));
    p.setPen(QColor(80,255,80));
    p.drawText(W-150,60,QString("FPS    %1").arg(int(m_fps)));
    p.setPen(QColor(120,200,255));
    p.drawText(W-150,80,QString("SCORE  %1").arg(m_gs.score));

    // Level indicator (top-centre)
    p.setFont(QFont("Courier New",11,QFont::Bold));
    p.setPen(QColor(0,220,180));
    p.drawText(W/2-60,18,QString("LEVEL %1  |  KILL %2/%3")
               .arg(m_gs.level).arg(m_gs.killsThisLevel).arg(m_gs.killsNeeded));

    // Minimap (top-left, below bars)
    const int MM=5,TILE=8,OX=8,OY=46;
    p.fillRect(OX,OY,MM*2*TILE,MM*2*TILE,QColor(0,0,0,160));
    for(int dy=-MM;dy<MM;dy++) for(int dx=-MM;dx<MM;dx++){
        int mx=int(m_gs.player.posX)+dx, my=int(m_gs.player.posY)+dy;
        if(mx<0||mx>=MAP_W||my<0||my>=MAP_H) continue;
        if(MAP[my][mx]){
            QColor wc=MAP[my][mx]==1?QColor(80,80,80):MAP[my][mx]==2?QColor(120,60,40):QColor(50,80,100);
            p.fillRect(OX+(dx+MM)*TILE,OY+(dy+MM)*TILE,TILE-1,TILE-1,wc);
        }
    }
    p.fillRect(OX+MM*TILE,OY+MM*TILE,TILE-1,TILE-1,QColor(0,220,180));
    for(auto &e:m_gs.enemies){ if(!e.alive) continue;
        int ex=int(e.x-m_gs.player.posX+MM),ey=int(e.y-m_gs.player.posY+MM);
        if(ex>=0&&ex<MM*2&&ey>=0&&ey<MM*2)
            p.fillRect(OX+ex*TILE,OY+ey*TILE,TILE-1,TILE-1,QColor(220,40,40));
    }

    // Ammo warning
    if(m_gs.player.ammo<=5&&m_gs.player.ammo>0){
        p.setFont(QFont("Courier New",14,QFont::Bold));
        p.setPen(QColor(255,80,0));
        p.drawText(W/2-60,H-52,"LOW AMMO!");
    } else if(m_gs.player.ammo==0){
        p.setPen(QColor(255,30,30));
        p.setFont(QFont("Courier New",14,QFont::Bold));
        p.drawText(W/2-58,H-52,"NO AMMO!");
    }

    // Controls hint (first 5 sec)
    if(m_gs.frameNum<300&&m_gs.level==1){
        p.fillRect(W/2-260,H-30,520,24,QColor(0,0,0,155));
        p.setFont(QFont("Courier New",10));
        p.setPen(QColor(170,170,170));
        p.drawText(W/2-255,H-12,"WASD:move  SHIFT+WASD:sprint  A/D:strafe  Q/E:turn  Space/LMB:fire  ESC:pause");
    }

    // Area cleared
    int alive=0; for(auto &e:m_gs.enemies) if(e.alive) alive++;
    if(alive==0&&!m_gs.levelComplete){
        p.setFont(QFont("Courier New",24,QFont::Bold));
        p.setPen(QColor(50,255,100));
        p.drawText(QRect(0,H/2-30,W,60),Qt::AlignCenter,"ENEMIES ELIMINATED!");
    }

    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  7. Level banner — "LEVEL COMPLETE" + "LOADING LEVEL X"
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderLevelBanner()
{
    if(!m_gs.levelComplete) return;
    float t=m_gs.levelTransTimer/3.5f;

    // Darken entire screen proportionally
    float dark=0.45f*(1.f-t)+0.15f*t;  // gets lighter as it fades out
    for(int i=0;i<W*H;i++){
        uint32_t c=m_pixels[i];
        uint8_t r=uint8_t(((c>>16)&0xFF)*dark);
        uint8_t g=uint8_t(((c>> 8)&0xFF)*dark);
        uint8_t b=uint8_t(( c     &0xFF)*dark);
        m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }

    QPainter p(&m_buf);
    p.fillRect(W/2-260,H/2-80,520,160,QColor(0,10,20,210));

    p.setFont(QFont("Courier New",34,QFont::Bold));
    p.setPen(QColor(50,255,120));
    p.drawText(QRect(W/2-260,H/2-75,520,70),Qt::AlignCenter,"LEVEL COMPLETE!");

    p.setFont(QFont("Courier New",14));
    p.setPen(QColor(255,210,0));
    p.drawText(QRect(W/2-260,H/2+2,520,36),Qt::AlignCenter,
               QString("BONUS +%1 pts    TOTAL: %2").arg(500*m_gs.level).arg(m_gs.score));

    p.setPen(QColor(150,200,255));
    p.drawText(QRect(W/2-260,H/2+40,520,30),Qt::AlignCenter,
               QString("Loading Level %1 ...").arg(m_gs.level+1));
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  8. Pause menu — semi-transparent overlay with Resume / Exit options
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderPauseMenu()
{
    if(!m_paused) return;
    // Half-brightness darken
    for(int i=0;i<W*H;i++){
        uint32_t c=m_pixels[i];
        uint8_t r=(c>>16&0xFF)/2,g=(c>>8&0xFF)/2,b=(c&0xFF)/2;
        m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    QPainter p(&m_buf);
    // Panel
    p.fillRect(W/2-180,H/2-120,360,240,QColor(5,8,18,230));
    // Border
    p.setPen(QPen(QColor(0,180,140),2));
    p.drawRect(W/2-180,H/2-120,360,240);

    p.setFont(QFont("Courier New",28,QFont::Bold));
    p.setPen(QColor(0,220,180));
    p.drawText(QRect(W/2-180,H/2-110,360,60),Qt::AlignCenter,"PAUSED");

    // Divider
    p.setPen(QColor(0,120,100));
    p.drawLine(W/2-140,H/2-45,W/2+140,H/2-45);

    p.setFont(QFont("Courier New",15,QFont::Bold));
    p.setPen(QColor(200,230,200));
    p.drawText(QRect(W/2-180,H/2-30,360,50),Qt::AlignCenter,"[R]  RESUME");

    p.setPen(QColor(255,255,150));
    p.drawText(QRect(W/2-180,H/2+30,360,50),Qt::AlignCenter,"[Q]  QUIT GAME");

    p.setFont(QFont("Courier New",10));
    p.setPen(QColor(100,120,100));
    p.drawText(QRect(W/2-180,H/2+90,360,30),Qt::AlignCenter,"Press ESC again to also exit");
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game-over screen
// ─────────────────────────────────────────────────────────────────────────────
static void drawGameOver(QImage &buf, int kills, int score, int level)
{
    QPainter p(&buf);
    // Dark overlay
    p.fillRect(0,0,W,H,QColor(0,0,0,180));
    // Panel
    p.fillRect(W/2-220,H/2-120,440,260,QColor(10,5,8,220));
    p.setPen(QPen(QColor(180,30,30),2));
    p.drawRect(W/2-220,H/2-120,440,260);

    p.setFont(QFont("Courier New",40,QFont::Bold));
    p.setPen(QColor(255,40,40));
    p.drawText(QRect(W/2-220,H/2-110,440,80),Qt::AlignCenter,"GAME OVER");

    p.setFont(QFont("Courier New",14));
    p.setPen(QColor(200,180,180));
    p.drawText(QRect(W/2-220,H/2-10,440,36),Qt::AlignCenter,
               QString("Reached Level %1   Kills: %2   Score: %3").arg(level).arg(kills).arg(score));

    p.setFont(QFont("Courier New",13,QFont::Bold));
    p.setPen(QColor(100,220,100));
    p.drawText(QRect(W/2-220,H/2+40,440,36),Qt::AlignCenter,"[R]  RESTART");
    p.setPen(QColor(255,180,80));
    p.drawText(QRect(W/2-220,H/2+80,440,36),Qt::AlignCenter,"[Q]  QUIT TO DESKTOP");
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Master render
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::render()
{
    renderFloorCeiling();
    renderWalls();
    renderSprites();
    renderWeaponAndArms();
    renderDamageVignette();
    renderHUD();
    renderLevelBanner();
    renderPauseMenu();

    if(m_gs.gameOver)
        drawGameOver(m_buf, m_gs.kills, m_gs.score, m_gs.level);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Qt paint event — single blit of the completed QImage
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawImage(0,0,m_buf);
}
