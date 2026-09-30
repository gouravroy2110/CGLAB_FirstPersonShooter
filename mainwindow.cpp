// =============================================================================
//  mainwindow.cpp  —  FPS Raycaster  |  Full feature set
//
//  ★ RASTER PROOF:
//    • Scroll wheel  → window grows; Qt nearest-neighbor scaling makes pixels
//      visibly chunky at any zoom level.
//    • [ ] keys      → halve/double the render block size (1→2→4→8 px blocks).
//      Press [ twice to see the game as 4×4 pixel blocks in real time.
//    • All game geometry: fillRect / fillCircle / drawLine → m_pixels (raw buf).
//    • QPainter used ONLY for drawText (QRasterPaintEngine, no GPU/OpenGL).
//
//  Render order (SCR_PLAYING):
//    1. renderFloorCeiling   perspective-correct floor cast
//    2. renderWalls          DDA raycaster → fills m_zBuf
//    3. renderSprites        enemies + bullet trails (Z-tested billboards)
//    4. renderParticles      blood FX (projected world-space particles)
//    5. renderWeaponAndArms  left arm + pistol + right arm + ADS + flash
//    6. renderDamageVignette red screen-edge on hit
//    7. renderHUD            crosshair · HP · stamina · ammo · minimap · FPS
//    8. renderLevelBanner    "LEVEL COMPLETE" transition
//    9. renderPauseMenu      ESC overlay
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
    // Resizable window — paintEvent scales m_buf to whatever size the window is,
    // using nearest-neighbor. Scroll wheel changes window size → chunky pixels.
    setMinimumSize(W/2, H/2);
    resize(W, H);

    m_pixels = reinterpret_cast<uint32_t*>(m_buf.bits());
    std::fill(m_zBuf, m_zBuf + W, 1e30f);
    std::memset(m_map, 0, sizeof(m_map));

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);
    m_clock.start();
    // Start on main menu — initGame() is called when player picks char+map
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game init / level load
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::initGame()
{
    m_gs = GameState{};
    m_gs.mapChoice  = m_mapChoice;
    m_gs.charClass  = CharClass(m_charChoice);

    // Apply character stats
    const CharProfile &cp = CHAR_PROFILES[m_charChoice];
    m_gs.player.hp             = cp.hp;
    m_gs.player.ammo           = cp.ammo;
    m_gs.player.stamina        = cp.stamina;
    m_gs.player.speedMult      = cp.speedMult;
    m_gs.player.damageReduction= cp.damageReduction;

    loadLevel(1);
    m_paused = false;
}

void MainWindow::loadLevel(int lv)
{
    int idx = std::min(lv, 5);
    const LevelCfg &cfg = LEVEL_CFG[idx];

    m_gs.level           = lv;
    m_gs.killsThisLevel  = 0;
    m_gs.killsNeeded     = cfg.killsNeeded;
    m_gs.levelComplete   = false;
    m_gs.levelTransTimer = 0.f;
    m_gs.bullets.clear();
    m_gs.particles.clear();
    m_gs.enemyBulletSpeed= cfg.bulletSpeed;
    m_gs.enemyFireRate   = cfg.fireCooldown;

    if (lv > 1) {
        m_gs.player.hp   = std::min(m_gs.player.hp + 30,
                                    CHAR_PROFILES[m_charChoice].hp);
        m_gs.player.ammo = std::min(m_gs.player.ammo + 20,
                                    CHAR_PROFILES[m_charChoice].ammo);
        m_gs.player.damageFlash = 0.f;
    }

    // Copy selected map into mutable m_map
    const int *src = ALL_MAPS[m_gs.mapChoice];
    for (int y=0;y<MAP_H;y++) for (int x=0;x<MAP_W;x++)
        m_map[y][x] = src[y*MAP_W+x];

    // Player spawn
    m_gs.player.posX=2.5f; m_gs.player.posY=2.5f;
    m_gs.player.dirX=1.f;  m_gs.player.dirY=0.f;
    m_gs.player.planeX=0.f;m_gs.player.planeY=0.66f;
    m_gs.player.shakeY=0.f;
    m_gs.player.adsMode=false;

    // Spawn enemies
    m_gs.enemies.clear();
    const float (*spawns)[2] = ALL_SPAWNS[m_gs.mapChoice];
    int numE = std::min(cfg.numEnemies, 12);
    for (int i=0; i<numE; i++) {
        Enemy e;
        e.x=spawns[i][0]; e.y=spawns[i][1];
        e.hp=cfg.enemyHP+(lv>5?(lv-5)*2:0);
        e.aggroRange=cfg.aggroRange;
        e.fireCooldown=cfg.fireCooldown;
        e.alive=true;
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
    m_fps    = 0.9*m_fps + 0.1/double(m_dt);
    m_lastMs = now;
    m_menuFrame++;

    if (m_screen == SCR_PLAYING && !m_paused && !m_gs.gameOver)
        gameStep(m_dt);

    // Smooth ADS FOV transition
    float targetFOV = (m_screen==SCR_PLAYING && m_gs.player.adsMode) ? 0.45f : 1.f;
    m_adsFOVMult += (targetFOV - m_adsFOVMult) * std::min(1.f, m_dt * 14.f);

    // Screen shake decay
    m_gs.player.shakeY *= 0.72f;
    if (std::fabs(m_gs.player.shakeY) < 0.2f) m_gs.player.shakeY = 0.f;

    render();
    QWidget::update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game logic
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::gameStep(float dt)
{
    Player &pl = m_gs.player;

    if (m_gs.levelComplete) {
        m_gs.levelTransTimer -= dt;
        if (m_gs.levelTransTimer <= 0.f) loadLevel(m_gs.level+1);
        return;
    }

    if (pl.attackCooldown>0) pl.attackCooldown-=dt;
    if (pl.flashTimer    >0) pl.flashTimer    -=dt;
    if (pl.damageFlash   >0) pl.damageFlash   -=dt;

    // ── Rotation ──────────────────────────────────────────────────────────────
    float rotSpd = 2.2f * dt * (pl.adsMode ? 0.55f : 1.f);
    auto rotate=[&](float a){
        float ox=pl.dirX, op=pl.planeX;
        pl.dirX  = pl.dirX  *cosf(a)+pl.dirY  *sinf(a);
        pl.dirY  =-ox       *sinf(a)+pl.dirY  *cosf(a);
        pl.planeX= pl.planeX*cosf(a)+pl.planeY*sinf(a);
        pl.planeY=-op       *sinf(a)+pl.planeY*cosf(a);
    };
    if (m_keys.count(Qt::Key_Left) ||m_keys.count(Qt::Key_Q)) rotate( rotSpd);
    if (m_keys.count(Qt::Key_Right)||m_keys.count(Qt::Key_E)) rotate(-rotSpd);

    // ── Sprint + stamina ──────────────────────────────────────────────────────
    bool moving = m_keys.count(Qt::Key_W)||m_keys.count(Qt::Key_S)||
                  m_keys.count(Qt::Key_A)||m_keys.count(Qt::Key_D);
    bool wantSprint = moving && m_keys.count(Qt::Key_Shift) && pl.stamina>0 && !pl.adsMode;
    pl.sprinting = wantSprint;
    pl.stamina += wantSprint ? (-45.f*dt) : (28.f*dt);
    pl.stamina = std::max(0.f, std::min(CHAR_PROFILES[m_charChoice].stamina, pl.stamina));

    // ── Movement (corrected strafe so diagonal isn't faster) ─────────────────
    float SPD = pl.speedMult * (pl.sprinting ? 5.8f : pl.adsMode ? 2.2f : 3.5f) * dt;
    float moveX=0,moveY=0;
    if (m_keys.count(Qt::Key_W)){moveX+=pl.dirX; moveY+=pl.dirY;}
    if (m_keys.count(Qt::Key_S)){moveX-=pl.dirX; moveY-=pl.dirY;}
    if (m_keys.count(Qt::Key_A)){moveX+=pl.dirY; moveY-=pl.dirX;}
    if (m_keys.count(Qt::Key_D)){moveX-=pl.dirY; moveY+=pl.dirX;}
    float len=sqrtf(moveX*moveX+moveY*moveY);
    if (len>0.001f){moveX/=len; moveY/=len;}
    float nx=pl.posX+moveX*SPD, ny=pl.posY+moveY*SPD;
    if (posValid(nx, pl.posY)) pl.posX=nx;
    if (posValid(pl.posX, ny)) pl.posY=ny;

    if (moving) pl.bobPhase += dt*(pl.sprinting?8.f:4.f);
    else        pl.bobPhase *= 0.85f;

    // ── Enemy AI ──────────────────────────────────────────────────────────────
    for (auto &e:m_gs.enemies) {
        if (!e.alive) continue;
        if (e.fireCooldown>0){e.fireCooldown-=dt; continue;}
        float dx=pl.posX-e.x, dy=pl.posY-e.y;
        float dist=sqrtf(dx*dx+dy*dy);
        if (dist<e.aggroRange && hasLOS(e.x,e.y,pl.posX,pl.posY)){
            enemyFire(e);
            e.fireCooldown=m_gs.enemyFireRate;
        }
    }

    // ── Bullet updates ────────────────────────────────────────────────────────
    for (auto &b:m_gs.bullets) {
        if (!b.alive) continue;
        b.x+=b.dx*dt; b.y+=b.dy*dt;
        int mx=int(b.x),my=int(b.y);
        if (mx<0||mx>=MAP_W||my<0||my>=MAP_H||m_map[my][mx]>0){b.alive=false;continue;}

        if (b.owner==PLAYER_SHOT){
            for (auto &e:m_gs.enemies){
                if (!e.alive) continue;
                float dx=b.x-e.x, dy=b.y-e.y;
                if (dx*dx+dy*dy<0.18f){
                    b.alive=false; e.hp--; e.hitFlash=0.18f;
                    if (e.hp<=0){
                        e.alive=false;
                        m_gs.kills++;m_gs.killsThisLevel++;
                        m_gs.score+=100*m_gs.level;
                        spawnBlood(e.x, e.y, 12);
                    }
                    break;
                }
            }
        } else {
            float dx=b.x-pl.posX, dy=b.y-pl.posY;
            if (dx*dx+dy*dy<0.15f){
                b.alive=false;
                int dmg = int((10+4*(m_gs.level-1))*(1.f-pl.damageReduction));
                pl.hp -= dmg;
                pl.damageFlash=0.45f;
                pl.shakeY = -8.f;  // camera jolts up on hit
                if (pl.hp<=0){pl.hp=0;m_gs.gameOver=true;}
            }
        }
    }
    m_gs.bullets.erase(
        std::remove_if(m_gs.bullets.begin(),m_gs.bullets.end(),[](const Bullet&b){return!b.alive;}),
        m_gs.bullets.end());

    for (auto &e:m_gs.enemies) if(e.hitFlash>0) e.hitFlash-=dt;

    // ── Blood particles ───────────────────────────────────────────────────────
    for (auto &pt:m_gs.particles){
        if(pt.life<=0) continue;
        pt.vz -= 9.0f*dt;
        pt.x  += pt.vx*dt; pt.y+=pt.vy*dt; pt.z+=pt.vz*dt;
        if(pt.z<0){pt.z=0; pt.vz*=-0.3f; pt.vx*=0.6f; pt.vy*=0.6f;}
        pt.life -= dt*0.9f;
    }
    m_gs.particles.erase(
        std::remove_if(m_gs.particles.begin(),m_gs.particles.end(),[](const Particle&p){return p.life<=0;}),
        m_gs.particles.end());

    if (!m_gs.levelComplete && m_gs.killsThisLevel>=m_gs.killsNeeded){
        m_gs.levelComplete=true;
        m_gs.levelTransTimer=3.5f;
        m_gs.score+=500*m_gs.level;
    }
    m_gs.frameNum++;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::posValid(float x,float y) const {
    const float R=0.28f;
    auto ok=[&](float tx,float ty){
        int mx=int(tx),my=int(ty);
        return mx>=0&&mx<MAP_W&&my>=0&&my<MAP_H&&m_map[my][mx]==0;
    };
    return ok(x-R,y-R)&&ok(x+R,y-R)&&ok(x-R,y+R)&&ok(x+R,y+R);
}
bool MainWindow::hasLOS(float x0,float y0,float x1,float y1) const {
    float dx=x1-x0, dy=y1-y0, dist=sqrtf(dx*dx+dy*dy);
    if(dist<0.1f) return true;
    float nx=dx/dist*0.12f, ny=dy/dist*0.12f;
    float cx=x0+nx, cy=y0+ny;
    for(int i=0;i<int(dist/0.12f);i++){
        int mx=int(cx),my=int(cy);
        if(mx<0||mx>=MAP_W||my<0||my>=MAP_H||m_map[my][mx]>0) return false;
        cx+=nx; cy+=ny;
    }
    return true;
}
void MainWindow::fireBullet() {
    Player &pl=m_gs.player;
    if(pl.ammo<=0||pl.attackCooldown>0) return;
    pl.ammo--; pl.attackCooldown=0.45f; pl.flashTimer=0.14f;
    pl.shakeY = -10.f;  // recoil kick
    Bullet b;
    b.x=pl.posX+pl.dirX*0.4f; b.y=pl.posY+pl.dirY*0.4f;
    b.dx=pl.dirX*9.f; b.dy=pl.dirY*9.f;
    b.owner=PLAYER_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}
void MainWindow::enemyFire(Enemy &e) {
    float dx=m_gs.player.posX-e.x, dy=m_gs.player.posY-e.y;
    float dist=sqrtf(dx*dx+dy*dy);
    if(dist<0.1f) return;
    Bullet b;
    b.x=e.x; b.y=e.y;
    b.dx=(dx/dist)*m_gs.enemyBulletSpeed; b.dy=(dy/dist)*m_gs.enemyBulletSpeed;
    b.owner=ENEMY_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}
void MainWindow::spawnBlood(float wx,float wy,int count) {
    for(int i=0;i<count;i++){
        Particle p;
        p.x=wx+(rand()%20-10)*0.015f; p.y=wy+(rand()%20-10)*0.015f;
        p.z=0.4f+(rand()%10)*0.05f;
        p.vx=(rand()%100-50)*0.025f; p.vy=(rand()%100-50)*0.025f;
        p.vz=(rand()%60)*0.035f;
        p.life=0.7f+(rand()%40)*0.008f;
        int r=180+rand()%60, g=rand()%25;
        p.color=(0xFF<<24)|(r<<16)|(g<<8)|0;
        m_gs.particles.push_back(p);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::keyPressEvent(QKeyEvent *e)
{
    m_keys.insert(e->key());

    // ── Menus ────────────────────────────────────────────────────────────────
    if(m_screen==SCR_MENU){
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_1) m_screen=SCR_MAP_SELECT;
        if(e->key()==Qt::Key_Q||e->key()==Qt::Key_Escape)  QApplication::quit();
        return;
    }
    if(m_screen==SCR_MAP_SELECT){
        if(e->key()==Qt::Key_1){m_mapChoice=0;m_screen=SCR_CHAR_SELECT;}
        if(e->key()==Qt::Key_2){m_mapChoice=1;m_screen=SCR_CHAR_SELECT;}
        if(e->key()==Qt::Key_3){m_mapChoice=2;m_screen=SCR_CHAR_SELECT;}
        if(e->key()==Qt::Key_Escape)m_screen=SCR_MENU;
        return;
    }
    if(m_screen==SCR_CHAR_SELECT){
        if(e->key()==Qt::Key_1){m_charChoice=0;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_2){m_charChoice=1;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_3){m_charChoice=2;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_Escape)m_screen=SCR_MAP_SELECT;
        return;
    }

    // ── In-game ──────────────────────────────────────────────────────────────
    if(e->key()==Qt::Key_Space) fireBullet();

    if(e->key()==Qt::Key_Escape){
        if(m_paused) QApplication::quit();
        else         m_paused=true;
    }
    if(e->key()==Qt::Key_R){
        if(m_paused)       m_paused=false;
        if(m_gs.gameOver){ initGame(); m_paused=false; }
    }
    if(e->key()==Qt::Key_Q){
        if(m_paused||m_gs.gameOver) QApplication::quit();
    }
    if(e->key()==Qt::Key_M && !m_gs.gameOver){
        // Return to main menu
        m_screen=SCR_MENU; m_paused=false;
    }

    // ── Raster proof: pixel block size toggle ─────────────────────────────────
    if(e->key()==Qt::Key_BracketLeft)  m_pixelScale=std::min(m_pixelScale*2,8);
    if(e->key()==Qt::Key_BracketRight) m_pixelScale=std::max(m_pixelScale/2,1);
}
void MainWindow::keyReleaseEvent(QKeyEvent *e){ m_keys.erase(e->key()); }
void MainWindow::mousePressEvent(QMouseEvent *e){
    if(m_screen!=SCR_PLAYING||m_paused||m_gs.gameOver) return;
    if(e->button()==Qt::LeftButton)  fireBullet();
    if(e->button()==Qt::RightButton) m_gs.player.adsMode=true;
}
void MainWindow::mouseReleaseEvent(QMouseEvent *e){
    if(e->button()==Qt::RightButton) m_gs.player.adsMode=false;
}
void MainWindow::mouseMoveEvent(QMouseEvent *e){
    if(m_screen!=SCR_PLAYING||m_paused||m_gs.gameOver) return;
    int cx=width()/2;
    float dx=float(e->x()-cx);
    if(std::fabs(dx)<1.f) return;
    float sens=m_gs.player.adsMode?0.0008f:0.0015f;
    float rot=dx*-sens;
    Player &pl=m_gs.player;
    float ox=pl.dirX,op=pl.planeX;
    pl.dirX  = pl.dirX *cosf(rot)+pl.dirY *sinf(rot);
    pl.dirY  =-ox      *sinf(rot)+pl.dirY *cosf(rot);
    pl.planeX= pl.planeX*cosf(rot)+pl.planeY*sinf(rot);
    pl.planeY=-op       *sinf(rot)+pl.planeY*cosf(rot);
    QCursor::setPos(mapToGlobal(QPoint(cx,height()/2)));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Scroll wheel — zoom window (nearest-neighbor scaling proves raster pixels)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::wheelEvent(QWheelEvent *e)
{
    int steps = e->angleDelta().y() / 120;
    m_zoomFactor = std::max(0.5f, std::min(4.f, m_zoomFactor + steps*0.25f));
    resize(int(W*m_zoomFactor), int(H*m_zoomFactor));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Color helpers
// ─────────────────────────────────────────────────────────────────────────────
uint32_t MainWindow::fogBlend(uint32_t c,float dist){
    float f=std::max(0.f,1.f-dist/14.f);
    return (0xFF<<24)|(uint8_t(((c>>16)&0xFF)*f)<<16)|(uint8_t(((c>>8)&0xFF)*f)<<8)|uint8_t((c&0xFF)*f);
}
uint32_t MainWindow::dimColor(uint32_t c,float b){
    if(b>=1.f) return c;
    uint8_t a=c>>24;
    return (a<<24)|(uint8_t(((c>>16)&0xFF)*b)<<16)|(uint8_t(((c>>8)&0xFF)*b)<<8)|uint8_t((c&0xFF)*b);
}
uint32_t MainWindow::wallPixel(int t,float wx,float wy,int side,float dist){
    int tx=int(wx*64)&63,ty=int(wy*64)&63; uint32_t c=0xFF888888;
    switch(t){
    case 1:{bool m=(ty%16<2)||((ty/16)%2==0?(tx%32<2):((tx+16)%32<2));
        if(m){c=0xFF555555;break;} int n=((tx*7+ty*13)^tx)&7; uint8_t v=uint8_t(125+n);
        c=(0xFF<<24)|(v<<16)|(v<<8)|v; break;}
    case 2:{bool m=(ty%8<1)||((ty/8)%2==0?(tx%20<2):((tx+10)%20<2));
        if(m){c=0xFF777766;break;} int n=((tx*5+ty*11)^ty)&15;
        c=(0xFF<<24)|(uint8_t(155+n)<<16)|(uint8_t(72+n/3)<<8)|uint8_t(55+n/3); break;}
    case 3:{int n=((tx*3+ty*7)^(tx+ty))&31; uint8_t v=uint8_t(90+n);
        c=(0xFF<<24)|(uint8_t(v*.75f)<<16)|(uint8_t(v*.85f)<<8)|v; break;}
    }
    if(side==1){uint8_t r=(c>>16&0xFF)/2,g=(c>>8&0xFF)/2,b=(c&0xFF)/2; c=(0xFF<<24)|(r<<16)|(g<<8)|b;}
    return fogBlend(c,dist);
}
uint32_t MainWindow::sampleImp(float u,float v,bool flash){
    int tx=std::min(15,std::max(0,int(u*16))),ty=std::min(15,std::max(0,int(v*16)));
    uint32_t c=IMP_SPRITE[ty][tx]; if((c>>24)==0) return 0;
    return flash?0xFFFFFFFF:c;
}
uint32_t MainWindow::samplePlayerBullet(float u,float v){
    float du=u-.5f,dv=v-.5f,d=du*du+dv*dv;
    if(d>.22f) return 0; float t=1.f-d*4.5f;
    return (0xFF<<24)|(0xFF<<16)|(uint8_t(180*t+75)<<8)|uint8_t(10*t);
}
uint32_t MainWindow::sampleEnemyBullet(float u,float v){
    float du=u-.5f,dv=v-.5f,d=du*du+dv*dv;
    if(d>.22f) return 0; float t=1.f-d*4.5f;
    return (0xFF<<24)|(uint8_t(220*t+35)<<16)|(uint8_t(30*t)<<8)|uint8_t(180*t);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pixel helpers (pure raster — direct writes to m_pixels uint32_t buffer)
// ─────────────────────────────────────────────────────────────────────────────
inline void MainWindow::px(int x,int y,uint32_t c){
    if((unsigned)x<(unsigned)W&&(unsigned)y<(unsigned)H) m_pixels[y*W+x]=c;
}
void MainWindow::fillRect(int x,int y,int w,int h,uint32_t c){
    int x0=std::max(x,0),x1=std::min(x+w,W),y0=std::max(y,0),y1=std::min(y+h,H);
    for(int r=y0;r<y1;r++) for(int col=x0;col<x1;col++) m_pixels[r*W+col]=c;
}
void MainWindow::fillCircle(int cx,int cy,int r,uint32_t c){
    for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++) if(dx*dx+dy*dy<=r*r) px(cx+dx,cy+dy,c);
}
void MainWindow::drawLine(int x0,int y0,int x1,int y1,uint32_t c){
    int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=std::abs(y1-y0),sy=y0<y1?1:-1,err=(dx>dy?dx:-dy)/2;
    for(;;){px(x0,y0,c); if(x0==x1&&y0==y1)break; int e=err; if(e>-dx){err-=dy;x0+=sx;} if(e<dy){err+=dx;y0+=sy;}}
}

// ─────────────────────────────────────────────────────────────────────────────
//  MENU SCREENS (raster background + QPainter text overlay)
// ─────────────────────────────────────────────────────────────────────────────

// Shared: draw a simulated dungeon corridor (raster) as atmospheric background
static void drawCorridorBg(uint32_t *px, int frame)
{
    // Ceiling + floor gradient
    for(int y=0;y<H;y++){
        float fy = float(y)/H;
        // Ceiling top half (very dark blue-grey fading darker toward horizon)
        // Floor bottom half (near-black with slight brown)
        uint8_t base = y<H/2 ? uint8_t(8*(1-fy*2)+2) : uint8_t(10*(fy*2-1)+2);
        for(int x=0;x<W;x++)
            px[y*W+x]=(0xFF<<24)|(uint8_t(base*.5f)<<16)|(uint8_t(base*.4f)<<8)|uint8_t(base*.7f);
    }
    // Stone corridor walls — trapezoidal shapes using fillRect layers
    for(int y=0;y<H;y++){
        float t = std::fabs(float(y-H/2))/float(H/2);  // 0=center, 1=edge
        int ww = int(W * 0.38f * t * t);
        uint8_t wv = uint8_t(55+30*t);
        // Left wall
        for(int x=0;x<ww&&x<W;x++){
            float shade=float(x)/float(std::max(1,ww));
            uint8_t v=uint8_t(wv*(0.5f+shade*0.5f));
            px[y*W+x]=(0xFF<<24)|(v<<16)|(v<<8)|uint8_t(v*0.9f);
        }
        // Right wall
        for(int x=W-ww;x<W;x++){
            float shade=float(W-1-x)/float(std::max(1,ww));
            uint8_t v=uint8_t(wv*(0.5f+shade*0.5f));
            px[y*W+x]=(0xFF<<24)|(v<<16)|(v<<8)|uint8_t(v*0.9f);
        }
    }
    // Animated blood-red glow at vanishing point (proves it's computed per frame)
    float pulse = 0.6f + 0.4f * sinf(frame * 0.04f);
    int gr = int(30*pulse);
    for(int dy=-gr;dy<=gr;dy++) for(int dx=-gr*2;dx<=gr*2;dx++){
        float d=float(dx*dx/(gr*gr*4.f)+dy*dy/float(gr*gr));
        if(d>1.f) continue;
        int sx=W/2+dx, sy=H/2+dy;
        if(sx<0||sx>=W||sy<0||sy>=H) continue;
        float a=(1.f-d)*0.35f*pulse;
        uint32_t &p=px[sy*W+sx];
        uint8_t r=uint8_t(std::min(255,int((p>>16&0xFF)+int(120*a))));
        uint8_t g=uint8_t(std::min(255,int((p>>8 &0xFF)+int(10*a))));
        p=(0xFF<<24)|(r<<16)|(g<<8)|(p&0xFF);
    }
}

void MainWindow::renderMenu()
{
    drawCorridorBg(m_pixels, m_menuFrame);

    QPainter p(&m_buf);
    // Title
    p.setFont(QFont("Courier New",72,QFont::Black));
    p.setPen(QColor(180,20,20));
    p.drawText(QRect(0,60,W,100),Qt::AlignCenter,"DOOM");
    p.setFont(QFont("Courier New",28,QFont::Bold));
    p.setPen(QColor(210,210,210));
    p.drawText(QRect(0,148,W,40),Qt::AlignCenter,"RASTER EDITION");

    // Subtitle
    p.setFont(QFont("Courier New",11));
    p.setPen(QColor(100,150,100));
    p.drawText(QRect(0,196,W,24),Qt::AlignCenter,"Software-rasterized FPS | Computer Graphics Lab | Sem 5");

    // Divider line (raster)
    for(int x=W/2-220;x<W/2+220;x++) m_pixels[(H/2-60)*W+x]=0xFF444444;

    // Menu options
    p.fillRect(W/2-160,H/2-50,320,130,QColor(0,0,0,180));
    p.setPen(QPen(QColor(0,160,120),1));
    p.drawRect(W/2-160,H/2-50,320,130);

    p.setFont(QFont("Courier New",18,QFont::Bold));
    p.setPen(QColor(50,230,120));
    p.drawText(QRect(W/2-160,H/2-38,320,50),Qt::AlignCenter,"[1 / ENTER]  NEW GAME");
    p.setPen(QColor(255,80,80));
    p.drawText(QRect(W/2-160,H/2+30,320,50),Qt::AlignCenter,"[Q / ESC]    EXIT");

    // Zoom info hint
    p.setFont(QFont("Courier New",9));
    p.setPen(QColor(80,80,80));
    p.drawText(QRect(0,H-22,W,20),Qt::AlignCenter,
               "SCROLL to zoom (shows raster pixels)  |  [ ] keys to change pixel block size in-game");
    p.end();
}

void MainWindow::renderMapSelect()
{
    drawCorridorBg(m_pixels, m_menuFrame);
    QPainter p(&m_buf);

    p.setFont(QFont("Courier New",28,QFont::Bold));
    p.setPen(QColor(0,220,180));
    p.drawText(QRect(0,30,W,50),Qt::AlignCenter,"SELECT MAP");
    p.setFont(QFont("Courier New",10));
    p.setPen(QColor(100,100,100));
    p.drawText(QRect(0,82,W,20),Qt::AlignCenter,"[ESC] Back");

    // 3 map cards
    static const uint32_t MAP_COLORS[3] = {0xFF555544,0xFF223344,0xFF442233};
    for(int i=0;i<3;i++){
        int cx=W/2 - 280 + i*280;
        int cy=H/2-90;
        p.fillRect(cx-110,cy,220,210,QColor(5,8,18,220));
        p.setPen(QPen(QColor(0,160,120),2));
        p.drawRect(cx-110,cy,220,210);

        // Mini map preview (raster grid)
        const int *mp = ALL_MAPS[i];
        int tsize=4,mox=cx-88,moy=cy+12;
        for(int my=0;my<24;my++) for(int mx=0;mx<24;mx++){
            int tile=mp[my*MAP_W+mx];
            uint32_t col = tile==0 ? 0xFF111111 :
                           tile==1 ? 0xFF888880 :
                           tile==2 ? 0xFF885555 : 0xFF558855;
            for(int dy=0;dy<tsize-1;dy++) for(int dx=0;dx<tsize-1;dx++){
                int px2=mox+mx*tsize+dx, py2=moy+my*tsize+dy;
                if(px2>=0&&px2<W&&py2>=0&&py2<H) m_pixels[py2*W+px2]=col;
            }
        }

        // Map name + description
        p.setFont(QFont("Courier New",16,QFont::Bold));
        p.setPen(QColor(255,220,100));
        p.drawText(QRect(cx-108,cy+112,216,28),Qt::AlignCenter,
                   QString("[%1] %2").arg(i+1).arg(MAP_NAMES[i]));
        p.setFont(QFont("Courier New",9));
        p.setPen(QColor(180,180,180));
        p.drawText(QRect(cx-108,cy+142,216,50),Qt::AlignCenter|Qt::TextWordWrap,
                   MAP_DESCS[i]);
    }
    p.end();
}

void MainWindow::renderCharSelect()
{
    drawCorridorBg(m_pixels, m_menuFrame);
    QPainter p(&m_buf);

    p.setFont(QFont("Courier New",26,QFont::Bold));
    p.setPen(QColor(0,220,180));
    p.drawText(QRect(0,25,W,46),Qt::AlignCenter,"SELECT CHARACTER");
    p.setFont(QFont("Courier New",10));
    p.setPen(QColor(100,100,100));
    p.drawText(QRect(0,74,W,20),Qt::AlignCenter,
               QString("Map: %1   |   [ESC] Back").arg(MAP_NAMES[m_mapChoice]));

    for(int i=0;i<3;i++){
        const CharProfile &cp=CHAR_PROFILES[i];
        int cx=W/2-280+i*280, cy=H/2-90;
        p.fillRect(cx-115,cy,230,240,QColor(5,8,18,220));
        p.setPen(QPen(QColor(0,140,100),2));
        p.drawRect(cx-115,cy,230,240);

        // Character silhouette (raster — draw with fillRect blocks)
        // Torso using the character's sleeve color
        uint32_t sl=cp.sleeve, slh=cp.sleeveHi, sk=cp.skinColor;
        // Head
        for(int dy=0;dy<20;dy++) for(int dx=0;dx<16;dx++){
            m_pixels[(cy+12+dy)*W+(cx-8+dx)]=sk;
        }
        // Torso
        for(int dy=0;dy<32;dy++) for(int dx=0;dx<28;dx++){
            m_pixels[(cy+34+dy)*W+(cx-14+dx)]=sl;
        }
        // Arms
        for(int dy=0;dy<28;dy++) for(int dx=0;dx<10;dx++){
            m_pixels[(cy+34+dy)*W+(cx-24+dx)]=slh;
            m_pixels[(cy+34+dy)*W+(cx+14+dx)]=slh;
        }
        // Legs
        for(int dy=0;dy<28;dy++){
            for(int dx=0;dx<12;dx++) m_pixels[(cy+68+dy)*W+(cx-12+dx)]=sl;
            for(int dx=0;dx<12;dx++) m_pixels[(cy+68+dy)*W+(cx+0+dx)]=slh;
        }

        // Name
        p.setFont(QFont("Courier New",16,QFont::Bold));
        p.setPen(QColor(255,220,100));
        p.drawText(QRect(cx-115,cy+100,230,30),Qt::AlignCenter,
                   QString("[%1] %2").arg(i+1).arg(cp.name));

        // Stats
        p.setFont(QFont("Courier New",9));
        p.setPen(QColor(200,200,200));
        p.drawText(QRect(cx-110,cy+132,220,22),Qt::AlignCenter,cp.stat1);
        p.drawText(QRect(cx-110,cy+152,220,22),Qt::AlignCenter,cp.stat2);
        p.setPen(i==2?QColor(80,180,255):QColor(150,150,150));
        p.drawText(QRect(cx-110,cy+172,220,22),Qt::AlignCenter,cp.stat3);
    }
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  1. Floor & Ceiling — perspective-correct floor casting
//     m_pixelScale controls block size for raster proof ([ ] keys)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderFloorCeiling()
{
    const Player &pl=m_gs.player;
    int shakeOff = int(pl.shakeY);
    int horizon  = H/2 + shakeOff;

    float rdX0=pl.dirX-pl.planeX*m_adsFOVMult, rdY0=pl.dirY-pl.planeY*m_adsFOVMult;
    float rdX1=pl.dirX+pl.planeX*m_adsFOVMult, rdY1=pl.dirY+pl.planeY*m_adsFOVMult;

    int PS = m_pixelScale;
    for(int y=horizon+1; y<H; y+=PS){
        int p2 = y - horizon;
        float rowDist = float(H/2)/float(p2);
        float sX=rowDist*(rdX1-rdX0)/float(W), sY=rowDist*(rdY1-rdY0)/float(W);
        float fX=pl.posX+rowDist*rdX0, fY=pl.posY+rowDist*rdY0;
        float fog=std::max(0.f,1.f-rowDist/14.f);

        for(int x=0;x<W;x+=PS){
            bool check=(int(fX)+int(fY))&1;
            uint8_t fv=uint8_t((check?42:26)*fog+6);
            uint32_t floorC=(0xFF<<24)|(fv<<16)|(uint8_t(fv*.88f)<<8)|uint8_t(fv*.65f);
            uint8_t cv=uint8_t(16*fog+3);
            uint32_t ceilC=(0xFF<<24)|(uint8_t(cv*.65f)<<16)|(uint8_t(cv*.75f)<<8)|cv;

            // Fill PS×PS block for floor and mirrored ceiling
            for(int by=0;by<PS&&y+by<H;by++){
                for(int bx=0;bx<PS&&x+bx<W;bx++){
                    m_pixels[(y+by)*W+(x+bx)]=floorC;
                    int cy2=2*horizon-(y+by)-1;
                    if(cy2>=0&&cy2<H) m_pixels[cy2*W+(x+bx)]=ceilC;
                }
            }
            fX+=sX*PS; fY+=sY*PS;
        }
    }
    // Horizon line
    for(int x=0;x<W;x++){
        int hy=std::max(0,std::min(H-1,horizon));
        m_pixels[hy*W+x]=0xFF060608;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  2. Walls — DDA raycaster, fills m_zBuf, respects m_pixelScale
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWalls()
{
    const Player &pl=m_gs.player;
    int shakeOff=int(pl.shakeY), horizon=H/2+shakeOff;
    int PS=m_pixelScale;

    for(int x=0;x<W;x+=PS){
        float camX=2.f*x/float(W)-1.f;
        float rdX=pl.dirX+pl.planeX*m_adsFOVMult*camX;
        float rdY=pl.dirY+pl.planeY*m_adsFOVMult*camX;
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
            if(m_map[mapY][mapX]>0) hit=true;
        }
        if(!hit){for(int bx=0;bx<PS&&x+bx<W;bx++) m_zBuf[x+bx]=1e30f; continue;}

        float dist=(side==0)?(sdX-ddX):(sdY-ddY);
        for(int bx=0;bx<PS&&x+bx<W;bx++) m_zBuf[x+bx]=dist;

        float wh; if(side==0) wh=pl.posY+dist*rdY; else wh=pl.posX+dist*rdX;
        wh-=std::floor(wh);
        int lh=std::max(1,int(float(H)/dist));
        int ds=std::max(0,horizon-lh/2), de=std::min(H-1,horizon+lh/2);
        int wt=m_map[mapY][mapX];
        for(int y=ds;y<=de;y++){
            float wy=float(y-(horizon-lh/2))/float(lh);
            uint32_t col=wallPixel(wt,wh,wy,side,dist);
            for(int bx=0;bx<PS&&x+bx<W;bx++) m_pixels[y*W+(x+bx)]=col;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  3. Sprites — enemies + bullets (Z-tested, bullet trail, ADS FOV)
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

    float effPlX=pl.planeX*m_adsFOVMult, effPlY=pl.planeY*m_adsFOVMult;
    float invDet=1.f/(effPlX*pl.dirY-pl.dirX*effPlY);
    int shakeOff=int(pl.shakeY), horizon=H/2+shakeOff;

    auto drawAt=[&](float wx,float wy,SpType type,int idx,float bright,float wScale){
        float spX=wx-pl.posX, spY=wy-pl.posY;
        float txX= invDet*( pl.dirY*spX-pl.dirX*spY);
        float txY= invDet*(-effPlY*spX+effPlX*spY);
        if(txY<0.05f) return;
        int scrX=int(float(W/2)*(1.f+txX/txY));
        int sprH=std::abs(int(float(H)/txY));
        int sprW=int(sprH*wScale);
        int sy0=horizon-sprH/2, sy1=horizon+sprH/2;
        int sx0=scrX-sprW/2, sx1=scrX+sprW/2;
        for(int sx=sx0;sx<sx1;sx++){
            if(sx<0||sx>=W||txY>=m_zBuf[sx]) continue;
            float u=float(sx-sx0)/float(std::max(1,sprW));
            for(int sy=std::max(0,sy0);sy<std::min(H,sy1);sy++){
                float v=float(sy-sy0)/float(std::max(1,sprH));
                uint32_t c=type==SPR_ENEMY?sampleImp(u,v,m_gs.enemies[idx].hitFlash>0)
                          :type==SPR_PBULLET?samplePlayerBullet(u,v):sampleEnemyBullet(u,v);
                if((c>>24)==0) continue;
                m_pixels[sy*W+sx]=fogBlend(dimColor(c,bright),txY);
            }
        }
    };

    for(const Sp &sp:sprites){
        if(sp.t==SPR_ENEMY){ drawAt(sp.x,sp.y,sp.t,sp.idx,1.f,1.f); }
        else {
            for(int pass=0;pass<3;pass++){
                float tr=pass*0.055f;
                drawAt(sp.x-sp.dx*tr, sp.y-sp.dy*tr, sp.t, sp.idx,
                       1.f-pass*0.35f, 0.45f-pass*0.10f);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  4. Blood particles — projected world-space splatter
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderParticles()
{
    const Player &pl=m_gs.player;
    float effPlX=pl.planeX*m_adsFOVMult, effPlY=pl.planeY*m_adsFOVMult;
    float invDet=1.f/(effPlX*pl.dirY-pl.dirX*effPlY);
    int shakeOff=int(pl.shakeY), horizon=H/2+shakeOff;

    for(const Particle &pt:m_gs.particles){
        if(pt.life<=0.f) continue;
        float spX=pt.x-pl.posX, spY=pt.y-pl.posY;
        float txX= invDet*( pl.dirY*spX-pl.dirX*spY);
        float txY= invDet*(-effPlY*spX+effPlX*spY);
        if(txY<0.05f) continue;
        int scrX=int(float(W/2)*(1.f+txX/txY));
        // Height: floor is at screen y = horizon + H/(2*txY)
        int floorY=horizon+int(float(H)*0.5f/txY);
        int scrY  =floorY-int(pt.z*float(H)/txY);
        if(scrX<1||scrX>=W-1||scrY<1||scrY>=H-1) continue;
        if(txY>=m_zBuf[scrX]) continue;
        uint32_t c=dimColor(fogBlend(pt.color,txY), pt.life);
        if((c>>24)==0) continue;
        // 3×3 splatter pixel
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) px(scrX+dx,scrY+dy,c);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  5. Weapon & Arms — detailed pistol + both forearms + ADS + muzzle flash
//     ALL drawing: fillRect / fillCircle / drawLine (pure raster)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWeaponAndArms()
{
    Player &pl=m_gs.player;
    const CharProfile &cp=CHAR_PROFILES[m_charChoice];

    bool ads = pl.adsMode;
    float bobX=sinf(pl.bobPhase)*(pl.sprinting?9.f:5.f);
    float bobY=fabsf(cosf(pl.bobPhase*.5f))*(pl.sprinting?12.f:7.f);
    float breathe=sinf(m_gs.frameNum*.018f)*1.5f;

    // In ADS: weapon moves toward screen centre; otherwise bottom-right
    int rx,ry;
    if(ads){
        rx=W/2     + int(bobX*.3f);
        ry=H - 30  + int(bobY*.4f+breathe*.5f);
    } else {
        rx=W/2+72  + int(bobX);
        ry=H - 8   + int(bobY+breathe);
    }

    // ── Left arm (support hand) ───────────────────────────────────────────────
    if(!ads){
        int lx=W/2-100+int(bobX*.4f), ly=ry-90;
        fillRect(lx-22,ly+28,44,H-(ly+28),cp.sleeve);
        fillRect(lx-24,ly+24,48,H-(ly+24),cp.sleeveHi);
        fillRect(lx-22,ly+28,5,H-(ly+28),uint32_t(cp.sleeveHi+0x050505));
        fillRect(lx-18,ly+8,36,22,cp.skinColor);
        fillRect(lx-20,ly-4,40,14,cp.skinColor);
        fillRect(lx-22,ly-4,6,10,uint32_t(cp.skinColor-0x100808));
        for(int f=0;f<4;f++){
            fillRect(lx-14+f*9,ly-13,7,11,cp.skinColor);
            fillRect(lx-14+f*9,ly-15,7,3,uint32_t(cp.skinColor-0x200010));
        }
    }

    // ── Pistol (improved) ─────────────────────────────────────────────────────
    int gs = ads ? 80 : 100;   // gun scale factor (smaller in ADS)
    auto gr=[&](int v)->int{ return v*gs/100; };

    fillRect(rx-gr(6),ry-gr(138),gr(12),gr(7),0xFF2A2A30);
    fillRect(rx-gr(4),ry-gr(136),gr(8),gr(3),0xFF101012);
    fillRect(rx-gr(5),ry-gr(131),gr(10),gr(67),0xFF3C3C42);
    fillRect(rx-gr(3),ry-gr(131),gr(6),gr(67),0xFF525260);
    for(int n=0;n<3;n++) fillRect(rx-gr(5),ry-gr(115)+n*gr(14),gr(10),gr(4),0xFF1E1E26);
    fillRect(rx-gr(2),ry-gr(140),gr(4),gr(9),0xFF252530);
    fillRect(rx-gr(1),ry-gr(138),gr(2),gr(4),0xFFCCCCDD);
    fillRect(rx-gr(18),ry-gr(65),gr(36),gr(30),0xFF35353E);
    fillRect(rx-gr(18),ry-gr(65),gr(36),gr(2),0xFF4A4A58);
    fillRect(rx-gr(18),ry-gr(65),gr(2),gr(30),0xFF4A4A58);
    fillRect(rx+gr(14),ry-gr(65),gr(4),gr(30),0xFF1C1C22);
    for(int s=0;s<6;s++) fillRect(rx+gr(2)+s*gr(2),ry-gr(62),gr(1),gr(20),0xFF272732);
    fillRect(rx+gr(5),ry-gr(59),gr(9),gr(15),0xFF18181E);
    fillRect(rx-gr(5),ry-gr(67),gr(12),gr(5),0xFF252530);
    fillRect(rx-gr(4),ry-gr(70),gr(3),gr(5),0xFF484858);
    fillRect(rx+gr(3),ry-gr(70),gr(3),gr(5),0xFF484858);
    fillRect(rx-gr(1),ry-gr(70),gr(2),gr(2),0xFFCCCCDD);
    fillRect(rx+gr(14),ry-gr(67),gr(5),gr(8),0xFF2C2C38);
    fillRect(rx+gr(13),ry-gr(69),gr(7),gr(4),0xFF3C3C4A);
    fillRect(rx-gr(18),ry-gr(36),gr(36),gr(16),0xFF2E2E38);
    fillRect(rx-gr(18),ry-gr(36),gr(36),gr(2),0xFF3E3E4A);
    fillRect(rx-gr(18),ry-gr(15),gr(3),gr(7),0xFF404052);
    fillRect(rx-gr(17),ry-gr(21),gr(3),gr(18),0xFF262630);
    fillRect(rx+gr(13),ry-gr(21),gr(4),gr(18),0xFF262630);
    fillRect(rx-gr(17),ry-gr(4),gr(34),gr(3),0xFF262630);
    fillRect(rx-gr(3),ry-gr(18),gr(6),gr(11),0xFF3A3A48);
    fillRect(rx-gr(2),ry-gr(16),gr(4),gr(7),0xFF4E4E60);
    fillRect(rx-gr(16),ry-gr(2),gr(30),gr(40),0xFF22222E);
    fillRect(rx-gr(16),ry-gr(2),gr(3),gr(40),0xFF2C2C3A);
    fillRect(rx+gr(12),ry-gr(2),gr(2),gr(40),0xFF181820);
    for(int t=0;t<6;t++) fillRect(rx-gr(14),ry+gr(1)+t*gr(6),gr(26),gr(4),0xFF28283A);
    fillRect(rx-gr(15),ry+gr(37),gr(29),gr(5),0xFF1A1A24);

    // ── Right arm (trigger hand) ───────────────────────────────────────────────
    fillRect(rx-gr(20),ry+gr(41),gr(40),gr(28),cp.skinColor);
    fillRect(rx-gr(22),ry+gr(44),gr(8),gr(20),cp.skinColor);
    for(int k=0;k<4;k++) fillRect(rx-gr(15)+k*gr(8),ry+gr(39),gr(6),gr(5),uint32_t(cp.skinColor-0x100808));
    fillRect(rx-gr(20),ry+gr(68),gr(40),H-(ry+gr(68)),cp.sleeve);
    fillRect(rx-gr(22),ry+gr(70),gr(44),H-(ry+gr(70)),cp.sleeveHi);
    fillRect(rx-gr(20),ry+gr(68),gr(5),H-(ry+gr(68)),uint32_t(cp.sleeveHi+0x050505));

    // ── ADS scope overlay (raster circles) ────────────────────────────────────
    if(ads && m_adsFOVMult < 0.7f){
        // Scope ring
        int sCx=W/2, sCy=H/2;
        fillCircle(sCx,sCy,62,0xFF000000);
        fillCircle(sCx,sCy,60,0x00000000);
        // Draw scope circle outline using drawLine arcs (raster)
        for(int ang=0;ang<360;ang+=2){
            float a=ang*3.14159f/180.f;
            px(sCx+int(cosf(a)*60),sCy+int(sinf(a)*60),0xFF88AA88);
        }
        // Crosshair reticle lines
        drawLine(sCx-55,sCy,sCx-8,sCy,0xFF88CC88);
        drawLine(sCx+8,sCy,sCx+55,sCy,0xFF88CC88);
        drawLine(sCx,sCy-55,sCx,sCy-8,0xFF88CC88);
        drawLine(sCx,sCy+8,sCx,sCy+55,0xFF88CC88);
        // Centre dot
        fillCircle(sCx,sCy,2,0xFFFF4444);
        // Vignette darkening outside scope
        for(int y=0;y<H;y++) for(int x=0;x<W;x++){
            float dx=float(x-sCx),dy=float(y-sCy);
            if(dx*dx+dy*dy>60.f*60.f){
                uint32_t &p=m_pixels[y*W+x];
                uint8_t r=(p>>16&0xFF)/4, g=(p>>8&0xFF)/4, b=(p&0xFF)/4;
                p=(0xFF<<24)|(r<<16)|(g<<8)|b;
            }
        }
    }

    // ── Muzzle flash ─────────────────────────────────────────────────────────
    if(pl.flashTimer>0.f){
        float t=pl.flashTimer/0.14f;
        int fr=int((10+rand()%5)*t);
        if(fr>0){
            int fx=rx, fy=ry-gr(141);
            for(int ray=0;ray<6;ray++){
                float a=ray*3.14159f/3.f;
                drawLine(fx,fy,fx+int(cosf(a)*(fr+10)),fy+int(sinf(a)*(fr+10)),0x99FF6600);
            }
            fillCircle(fx,fy,fr+5,0x55FF7700);
            fillCircle(fx,fy,fr+2,0xCCFFAA00);
            fillCircle(fx,fy,fr,  0xFFFFEE00);
            fillCircle(fx,fy,fr/2,0xFFFFFFFF);
            int addR=int(32*t),addG=int(16*t);
            for(int i=0;i<W*H;i++){
                uint32_t p2=m_pixels[i];
                uint8_t r=uint8_t(std::min(255,int((p2>>16&0xFF)+addR)));
                uint8_t g=uint8_t(std::min(255,int((p2>>8 &0xFF)+addG)));
                m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|(p2&0xFF);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  6. Damage vignette
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderDamageVignette()
{
    if(m_gs.player.damageFlash<=0.f) return;
    float t=m_gs.player.damageFlash/0.45f; int depth=int(90*t);
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
//  7. HUD
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderHUD()
{
    const Player &pl=m_gs.player;
    bool ads=pl.adsMode && m_adsFOVMult<0.8f;

    // Crosshair (raster) — dot only in ADS, lines in normal
    int cx=W/2, cy=H/2;
    if(ads){
        fillCircle(cx,cy,2,0xFFFF4444);
    } else {
        int gap=6,arm=11;
        for(int i=0;i<2;i++){
            drawLine(cx-arm-gap,cy-i+1,cx-gap-1,   cy-i+1,0xBBFFFFFF);
            drawLine(cx+gap,    cy-i+1,cx+arm+gap,  cy-i+1,0xBBFFFFFF);
            drawLine(cx-i+1,cy-arm-gap,cx-i+1,cy-gap-1,0xBBFFFFFF);
            drawLine(cx-i+1,cy+gap,    cx-i+1,cy+arm+gap,0xBBFFFFFF);
        }
        px(cx,cy,0xFFFF3333);
    }

    // HP bar
    int hpFill=180*pl.hp/100;
    fillRect(8,8,180,16,0x99000000);
    fillRect(8,8,hpFill,16,pl.hp>40?0xFF22CC44:0xFFFF3311);
    // Stamina bar
    int stFill=int(120*pl.stamina/CHAR_PROFILES[m_charChoice].stamina);
    fillRect(8,27,120,8,0x99000000);
    fillRect(8,27,stFill,8,pl.sprinting?0xFFFFCC00:0xFF4488FF);

    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",11,QFont::Bold));
    p.setPen(Qt::white);
    p.drawText(12,22,QString("HP  %1").arg(pl.hp));
    p.setPen(pl.sprinting?QColor(255,204,0):QColor(68,136,255));
    p.drawText(12,40,QString("STM %1%%").arg(int(pl.stamina)));

    p.fillRect(W-162,0,162,100,QColor(0,0,0,130));
    p.setFont(QFont("Courier New",11,QFont::Bold));
    p.setPen(QColor(255,210,0)); p.drawText(W-152,18,QString("AMMO   %1").arg(pl.ammo));
    p.setPen(QColor(200,200,200));p.drawText(W-152,36,QString("KILLS  %1").arg(m_gs.kills));
    p.setPen(QColor(80,255,80)); p.drawText(W-152,54,QString("FPS    %1").arg(int(m_fps)));
    p.setPen(QColor(120,200,255));p.drawText(W-152,72,QString("SCORE  %1").arg(m_gs.score));
    // Raster proof indicators
    p.setPen(QColor(80,80,80));
    p.setFont(QFont("Courier New",8));
    p.drawText(W-152,90,QString("ZOOM %1x  BLK %2px").arg(m_zoomFactor,0,'f',1).arg(m_pixelScale));

    // Level indicator
    p.setFont(QFont("Courier New",10,QFont::Bold));
    p.setPen(QColor(0,220,180));
    p.drawText(QRect(0,0,W,18),Qt::AlignCenter,
               QString("LEVEL %1 [%2]  KILL %3/%4")
               .arg(m_gs.level).arg(MAP_NAMES[m_gs.mapChoice])
               .arg(m_gs.killsThisLevel).arg(m_gs.killsNeeded));

    // Minimap
    const int MM=5,TILE=8,OX=8,OY=46;
    p.fillRect(OX,OY,MM*2*TILE,MM*2*TILE,QColor(0,0,0,160));
    for(int dy=-MM;dy<MM;dy++) for(int dx=-MM;dx<MM;dx++){
        int mx=int(pl.posX)+dx,my=int(pl.posY)+dy;
        if(mx<0||mx>=MAP_W||my<0||my>=MAP_H) continue;
        if(m_map[my][mx]){
            QColor wc=m_map[my][mx]==1?QColor(80,80,80):m_map[my][mx]==2?QColor(120,60,40):QColor(50,80,100);
            p.fillRect(OX+(dx+MM)*TILE,OY+(dy+MM)*TILE,TILE-1,TILE-1,wc);
        }
    }
    p.fillRect(OX+MM*TILE,OY+MM*TILE,TILE-1,TILE-1,QColor(0,220,180));
    for(auto &e:m_gs.enemies){ if(!e.alive) continue;
        int ex=int(e.x-pl.posX+MM),ey=int(e.y-pl.posY+MM);
        if(ex>=0&&ex<MM*2&&ey>=0&&ey<MM*2)
            p.fillRect(OX+ex*TILE,OY+ey*TILE,TILE-1,TILE-1,QColor(220,40,40));
    }

    // ADS label
    if(ads){ p.setFont(QFont("Courier New",10,QFont::Bold));
             p.setPen(QColor(80,200,80)); p.drawText(W/2-25,H-45,"[ ADS ]"); }

    // Low ammo
    if(pl.ammo<=5&&pl.ammo>0){ p.setFont(QFont("Courier New",14,QFont::Bold));
        p.setPen(QColor(255,80,0)); p.drawText(W/2-60,H-50,"LOW AMMO!"); }
    if(pl.ammo==0){ p.setFont(QFont("Courier New",14,QFont::Bold));
        p.setPen(QColor(255,30,30)); p.drawText(W/2-58,H-50,"NO AMMO!"); }

    // Controls hint
    if(m_gs.frameNum<300&&m_gs.level==1){
        p.fillRect(W/2-290,H-28,580,22,QColor(0,0,0,155));
        p.setFont(QFont("Courier New",9));
        p.setPen(QColor(140,140,140));
        p.drawText(W/2-285,H-12,
                   "WASD:move  SHIFT:sprint  RMB:ADS  Space/LMB:fire  M:menu  []:pixelsize  SCROLL:zoom  ESC:pause");
    }

    int alive=0; for(auto &e:m_gs.enemies) if(e.alive) alive++;
    if(alive==0&&!m_gs.levelComplete){
        p.setFont(QFont("Courier New",24,QFont::Bold));
        p.setPen(QColor(50,255,100));
        p.drawText(QRect(0,H/2-30,W,60),Qt::AlignCenter,"ENEMIES ELIMINATED!");
    }
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  8. Level banner
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderLevelBanner()
{
    if(!m_gs.levelComplete) return;
    float t=m_gs.levelTransTimer/3.5f;
    float dark=0.40f*(1.f-t)+0.15f*t;
    for(int i=0;i<W*H;i++){
        uint32_t c=m_pixels[i];
        uint8_t r=uint8_t(((c>>16)&0xFF)*dark),g=uint8_t(((c>>8)&0xFF)*dark),b=uint8_t((c&0xFF)*dark);
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
               QString("Loading Level %1 in %2s...").arg(m_gs.level+1).arg(int(m_gs.levelTransTimer)+1));
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  9. Pause menu
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderPauseMenu()
{
    if(!m_paused) return;
    for(int i=0;i<W*H;i++){
        uint32_t c=m_pixels[i];
        uint8_t r=(c>>16&0xFF)/2,g=(c>>8&0xFF)/2,b=(c&0xFF)/2;
        m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    QPainter p(&m_buf);
    p.fillRect(W/2-185,H/2-130,370,270,QColor(5,8,18,235));
    p.setPen(QPen(QColor(0,180,140),2)); p.drawRect(W/2-185,H/2-130,370,270);
    p.setFont(QFont("Courier New",28,QFont::Bold));
    p.setPen(QColor(0,220,180));
    p.drawText(QRect(W/2-185,H/2-118,370,60),Qt::AlignCenter,"PAUSED");
    p.setPen(QColor(0,120,100));
    p.drawLine(W/2-145,H/2-52,W/2+145,H/2-52);
    p.setFont(QFont("Courier New",14,QFont::Bold));
    p.setPen(QColor(200,230,200));
    p.drawText(QRect(W/2-185,H/2-38,370,46),Qt::AlignCenter,"[R]  RESUME");
    p.setPen(QColor(150,200,255));
    p.drawText(QRect(W/2-185,H/2+18,370,46),Qt::AlignCenter,"[M]  MAIN MENU");
    p.setPen(QColor(255,255,150));
    p.drawText(QRect(W/2-185,H/2+74,370,46),Qt::AlignCenter,"[Q / ESC]  QUIT");
    p.setFont(QFont("Courier New",9));
    p.setPen(QColor(60,80,60));
    p.drawText(QRect(W/2-185,H/2+128,370,26),Qt::AlignCenter,
               "SCROLL=zoom in/out  [=bigger pixels  ]=finer pixels");
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game-over screen
// ─────────────────────────────────────────────────────────────────────────────
static void drawGameOver(QImage &buf, int kills, int score, int level, const char *mapName)
{
    QPainter p(&buf);
    p.fillRect(0,0,W,H,QColor(0,0,0,185));
    p.fillRect(W/2-230,H/2-120,460,280,QColor(10,5,8,225));
    p.setPen(QPen(QColor(180,30,30),2)); p.drawRect(W/2-230,H/2-120,460,280);
    p.setFont(QFont("Courier New",40,QFont::Bold));
    p.setPen(QColor(255,40,40));
    p.drawText(QRect(W/2-230,H/2-108,460,80),Qt::AlignCenter,"GAME OVER");
    p.setFont(QFont("Courier New",13));
    p.setPen(QColor(200,180,180));
    p.drawText(QRect(W/2-230,H/2-10,460,30),Qt::AlignCenter,
               QString("Level %1 [%2]   Kills: %3   Score: %4").arg(level).arg(mapName).arg(kills).arg(score));
    p.setFont(QFont("Courier New",13,QFont::Bold));
    p.setPen(QColor(100,220,100));
    p.drawText(QRect(W/2-230,H/2+36,460,36),Qt::AlignCenter,"[R]  RESTART SAME SETTINGS");
    p.setPen(QColor(150,200,255));
    p.drawText(QRect(W/2-230,H/2+76,460,36),Qt::AlignCenter,"[M]  MAIN MENU");
    p.setPen(QColor(255,180,80));
    p.drawText(QRect(W/2-230,H/2+116,460,36),Qt::AlignCenter,"[Q]  QUIT TO DESKTOP");
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Master render dispatcher
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::render()
{
    switch(m_screen){
    case SCR_MENU:        renderMenu();       return;
    case SCR_MAP_SELECT:  renderMapSelect();  return;
    case SCR_CHAR_SELECT: renderCharSelect(); return;
    case SCR_PLAYING: break;
    }
    renderFloorCeiling();
    renderWalls();
    renderSprites();
    renderParticles();
    renderWeaponAndArms();
    renderDamageVignette();
    renderHUD();
    renderLevelBanner();
    renderPauseMenu();
    if(m_gs.gameOver)
        drawGameOver(m_buf, m_gs.kills, m_gs.score, m_gs.level, MAP_NAMES[m_gs.mapChoice]);
}

// ─────────────────────────────────────────────────────────────────────────────
//  paintEvent — scales m_buf to window size with NEAREST-NEIGHBOR
//  (SmoothPixmapTransform=false → chunky pixels when zoomed in = raster proof)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    // Stretch internal 800×600 buffer to whatever the window size is — pixel-perfect
    p.drawImage(rect(), m_buf, m_buf.rect());
}
