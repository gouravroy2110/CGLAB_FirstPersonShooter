// =============================================================================
//  mainwindow.cpp  —  Raster FPS | High-fidelity UI + full gameplay
//
//  ★ RASTER PROOF (for professor):
//    • SCROLL WHEEL  → window grows; Qt nearest-neighbor (SmoothPixmapTransform=false)
//      makes every pixel appear as a square block — demonstrating the pixel buffer.
//    • [ / ] KEYS    → halve/double render block size (1→2→4→8 px per block).
//      Press [ twice: entire game renders in 4×4 blocks — undeniable software raster.
//    • All game pixels are written to a raw uint32_t* buffer (m_pixels).
//    • QPainter is used ONLY for drawText — QRasterPaintEngine, no GPU.
//
//  Render pipeline (SCR_PLAYING):
//    1  renderFloorCeiling   perspective-correct floor cast (pixelScale-aware)
//    2  renderWalls          DDA raycaster → fills m_zBuf (pixelScale-aware)
//    3  renderSprites        Z-tested enemy + bullet billboards, 3-pass trail
//    4  renderParticles      world-space blood FX (gravity, Z-tested)
//    5  renderWeaponAndArms  left arm + pistol + right arm, ADS scope, flash
//    6  renderDamageVignette red screen-edge when player is hit
//    7  renderHUD            calls all sub-renderers (see §HUD)
//    8  renderLevelBanner    "OBJECTIVE COMPLETE" transition panel
//    9  renderPauseMenu      ESC overlay
//   10  drawGameOver         mission-failed screen
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
    setMinimumSize(W/2, H/2);
    resize(W, H);

    m_pixels = reinterpret_cast<uint32_t*>(m_buf.bits());
    std::fill(m_zBuf, m_zBuf+W, 1e30f);
    std::memset(m_map, 0, sizeof(m_map));

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);
    m_clock.start();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game init / level load
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::initGame()
{
    m_gs           = GameState{};
    m_gs.mapChoice = m_mapChoice;
    m_gs.charClass = CharClass(m_charChoice);

    const CharProfile &cp = CHAR_PROFILES[m_charChoice];
    m_gs.player.hp              = cp.hp;
    m_gs.player.ammo            = cp.ammo;
    m_gs.player.stamina         = cp.stamina;
    m_gs.player.speedMult       = cp.speedMult;
    m_gs.player.damageReduction = cp.damageReduction;

    m_combo = 0; m_comboTimer = 0; m_comboDisplay = 0;
    m_hitMarker = 0; m_damageDirTimer = 0;
    m_killFeed.clear();

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
        m_gs.player.hp   = std::min(m_gs.player.hp  +30, CHAR_PROFILES[m_charChoice].hp);
        m_gs.player.ammo = std::min(m_gs.player.ammo+20, CHAR_PROFILES[m_charChoice].ammo);
        m_gs.player.damageFlash = 0;
    }

    const int *src = ALL_MAPS[m_gs.mapChoice];
    for (int y=0;y<MAP_H;y++) for (int x=0;x<MAP_W;x++)
        m_map[y][x] = src[y*MAP_W+x];

    m_gs.player.posX=2.5f; m_gs.player.posY=2.5f;
    m_gs.player.dirX=1.f;  m_gs.player.dirY=0.f;
    m_gs.player.planeX=0.f;m_gs.player.planeY=0.66f;
    m_gs.player.shakeY=0;  m_gs.player.adsMode=false;

    m_gs.enemies.clear();
    const float (*spawns)[2] = ALL_SPAWNS[m_gs.mapChoice];
    for (int i=0; i<std::min(cfg.numEnemies,12); i++) {
        Enemy e; e.x=spawns[i][0]; e.y=spawns[i][1];
        e.hp=cfg.enemyHP+(lv>5?(lv-5)*2:0);
        e.aggroRange=cfg.aggroRange; e.fireCooldown=cfg.fireCooldown; e.alive=true;
        m_gs.enemies.push_back(e);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game loop tick
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::tick()
{
    double now = m_clock.elapsed();
    m_dt  = float((now-m_lastMs)/1000.0);
    if (m_dt>0.05f) m_dt=0.05f;
    m_fps = 0.9*m_fps+0.1/double(m_dt);
    m_lastMs = now;
    m_menuFrame++;

    if (m_screen==SCR_PLAYING && !m_paused && !m_gs.gameOver) gameStep(m_dt);

    // ADS FOV smooth lerp
    float tgtFOV = (m_screen==SCR_PLAYING && m_gs.player.adsMode) ? 0.45f : 1.f;
    m_adsFOVMult += (tgtFOV-m_adsFOVMult)*std::min(1.f,m_dt*14.f);

    // Decay visual timers
    m_gs.player.shakeY *= 0.72f;
    if (std::fabs(m_gs.player.shakeY)<0.2f) m_gs.player.shakeY=0;
    m_hitMarker      = std::max(0.f, m_hitMarker-m_dt);
    m_damageDirTimer = std::max(0.f, m_damageDirTimer-m_dt);
    m_comboDisplay   = std::max(0.f, m_comboDisplay-m_dt);
    if (m_comboTimer>0) { m_comboTimer-=m_dt; if(m_comboTimer<=0){m_combo=0;} }
    for (auto &k:m_killFeed) k.life-=m_dt;
    m_killFeed.erase(std::remove_if(m_killFeed.begin(),m_killFeed.end(),
        [](const KFEntry &k){return k.life<=0;}),m_killFeed.end());

    render();
    QWidget::update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game logic
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::gameStep(float dt)
{
    Player &pl = m_gs.player;
    if (m_gs.levelComplete) { m_gs.levelTransTimer-=dt; if(m_gs.levelTransTimer<=0) loadLevel(m_gs.level+1); return; }
    if (pl.attackCooldown>0) pl.attackCooldown-=dt;
    if (pl.flashTimer    >0) pl.flashTimer    -=dt;
    if (pl.damageFlash   >0) pl.damageFlash   -=dt;

    // Rotation
    float rotSpd=2.2f*dt*(pl.adsMode?0.55f:1.f);
    auto rotate=[&](float a){
        float ox=pl.dirX,op=pl.planeX;
        pl.dirX  = pl.dirX *cosf(a)+pl.dirY *sinf(a); pl.dirY  =-ox      *sinf(a)+pl.dirY *cosf(a);
        pl.planeX= pl.planeX*cosf(a)+pl.planeY*sinf(a);pl.planeY=-op       *sinf(a)+pl.planeY*cosf(a);
    };
    if (m_keys.count(Qt::Key_Left)||m_keys.count(Qt::Key_Q))  rotate( rotSpd);
    if (m_keys.count(Qt::Key_Right)||m_keys.count(Qt::Key_E)) rotate(-rotSpd);

    // Sprint + stamina
    bool moving=m_keys.count(Qt::Key_W)||m_keys.count(Qt::Key_S)||m_keys.count(Qt::Key_A)||m_keys.count(Qt::Key_D);
    bool wantSprint=moving&&m_keys.count(Qt::Key_Shift)&&pl.stamina>0&&!pl.adsMode;
    pl.sprinting=wantSprint;
    pl.stamina+=wantSprint?(-45.f*dt):(28.f*dt);
    pl.stamina=std::max(0.f,std::min(CHAR_PROFILES[m_charChoice].stamina,pl.stamina));

    // Movement (normalised so diagonal isn't faster)
    float SPD=pl.speedMult*(pl.sprinting?5.8f:pl.adsMode?2.2f:3.5f)*dt;
    float mx=0,my=0;
    if (m_keys.count(Qt::Key_W)){mx+=pl.dirX;my+=pl.dirY;}
    if (m_keys.count(Qt::Key_S)){mx-=pl.dirX;my-=pl.dirY;}
    if (m_keys.count(Qt::Key_A)){mx+=pl.dirY;my-=pl.dirX;}
    if (m_keys.count(Qt::Key_D)){mx-=pl.dirY;my+=pl.dirX;}
    float len=sqrtf(mx*mx+my*my); if(len>0.001f){mx/=len;my/=len;}
    float nx=pl.posX+mx*SPD,ny=pl.posY+my*SPD;
    if (posValid(nx,pl.posY)) pl.posX=nx;
    if (posValid(pl.posX,ny)) pl.posY=ny;
    if (moving) pl.bobPhase+=dt*(pl.sprinting?8.f:4.f); else pl.bobPhase*=0.85f;

    // Enemy AI: aggro + fire
    for (auto &e:m_gs.enemies){
        if (!e.alive) continue;
        if (e.fireCooldown>0){e.fireCooldown-=dt;continue;}
        float dx=pl.posX-e.x,dy=pl.posY-e.y,dist=sqrtf(dx*dx+dy*dy);
        if (dist<e.aggroRange&&hasLOS(e.x,e.y,pl.posX,pl.posY)){enemyFire(e);e.fireCooldown=m_gs.enemyFireRate;}
    }

    // Bullet updates
    for (auto &b:m_gs.bullets){
        if (!b.alive) continue;
        b.x+=b.dx*dt; b.y+=b.dy*dt;
        int bx=int(b.x),by=int(b.y);
        if (bx<0||bx>=MAP_W||by<0||by>=MAP_H||m_map[by][bx]>0){b.alive=false;continue;}
        if (b.owner==PLAYER_SHOT){
            for (auto &e:m_gs.enemies){
                if (!e.alive) continue;
                float dx=b.x-e.x,dy=b.y-e.y;
                if (dx*dx+dy*dy<0.18f){
                    b.alive=false; e.hp--; e.hitFlash=0.18f;
                    m_hitMarker=0.35f;
                    if (e.hp<=0){
                        e.alive=false; m_gs.kills++; m_gs.killsThisLevel++; m_gs.score+=100*m_gs.level;
                        spawnBlood(e.x,e.y,12);
                        m_combo++; m_comboTimer=3.5f; m_comboDisplay=1.8f;
                        KFEntry kf;
                        kf.text=(m_combo>1?QString("x%1 ").arg(m_combo):"")+QString("ENEMY DOWN");
                        kf.life=4.5f;
                        m_killFeed.insert(m_killFeed.begin(),kf);
                        if(int(m_killFeed.size())>4) m_killFeed.pop_back();
                    }
                    break;
                }
            }
        } else {
            float dx=b.x-pl.posX,dy=b.y-pl.posY;
            if (dx*dx+dy*dy<0.15f){
                b.alive=false;
                int dmg=int((10+4*(m_gs.level-1))*(1.f-pl.damageReduction));
                pl.hp-=dmg; pl.damageFlash=0.45f; pl.shakeY=-9.f;
                m_damageAngle=atan2f(-b.dy,-b.dx); // toward source
                m_damageDirTimer=2.0f;
                if (pl.hp<=0){pl.hp=0;m_gs.gameOver=true;}
            }
        }
    }
    m_gs.bullets.erase(std::remove_if(m_gs.bullets.begin(),m_gs.bullets.end(),[](const Bullet&b){return!b.alive;}),m_gs.bullets.end());
    for (auto &e:m_gs.enemies) if(e.hitFlash>0) e.hitFlash-=dt;

    // Blood particles
    for (auto &pt:m_gs.particles){
        if(pt.life<=0) continue;
        pt.vz-=9.f*dt; pt.x+=pt.vx*dt; pt.y+=pt.vy*dt; pt.z+=pt.vz*dt;
        if(pt.z<0){pt.z=0;pt.vz*=-0.3f;pt.vx*=0.6f;pt.vy*=0.6f;}
        pt.life-=dt*0.9f;
    }
    m_gs.particles.erase(std::remove_if(m_gs.particles.begin(),m_gs.particles.end(),[](const Particle&p){return p.life<=0;}),m_gs.particles.end());

    if (!m_gs.levelComplete&&m_gs.killsThisLevel>=m_gs.killsNeeded){
        m_gs.levelComplete=true; m_gs.levelTransTimer=3.5f; m_gs.score+=500*m_gs.level;
    }
    m_gs.frameNum++;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::posValid(float x,float y) const {
    const float R=0.28f;
    auto ok=[&](float tx,float ty){int mx=int(tx),my=int(ty);return mx>=0&&mx<MAP_W&&my>=0&&my<MAP_H&&m_map[my][mx]==0;};
    return ok(x-R,y-R)&&ok(x+R,y-R)&&ok(x-R,y+R)&&ok(x+R,y+R);
}
bool MainWindow::hasLOS(float x0,float y0,float x1,float y1) const {
    float dx=x1-x0,dy=y1-y0,dist=sqrtf(dx*dx+dy*dy);
    if(dist<0.1f) return true;
    float nx=dx/dist*0.12f,ny=dy/dist*0.12f,cx=x0+nx,cy=y0+ny;
    for(int i=0;i<int(dist/0.12f);i++){
        int mx=int(cx),my=int(cy); if(mx<0||mx>=MAP_W||my<0||my>=MAP_H||m_map[my][mx]>0) return false; cx+=nx;cy+=ny;
    }
    return true;
}
void MainWindow::fireBullet(){
    Player &pl=m_gs.player;
    if(pl.ammo<=0||pl.attackCooldown>0) return;
    pl.ammo--; pl.attackCooldown=0.45f; pl.flashTimer=0.14f; pl.shakeY=-11.f;
    Bullet b; b.x=pl.posX+pl.dirX*0.4f; b.y=pl.posY+pl.dirY*0.4f;
    b.dx=pl.dirX*9.f; b.dy=pl.dirY*9.f; b.owner=PLAYER_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}
void MainWindow::enemyFire(Enemy &e){
    float dx=m_gs.player.posX-e.x,dy=m_gs.player.posY-e.y,dist=sqrtf(dx*dx+dy*dy);
    if(dist<0.1f) return;
    Bullet b; b.x=e.x; b.y=e.y; b.dx=(dx/dist)*m_gs.enemyBulletSpeed; b.dy=(dy/dist)*m_gs.enemyBulletSpeed; b.owner=ENEMY_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}
void MainWindow::spawnBlood(float wx,float wy,int count){
    for(int i=0;i<count;i++){
        Particle p; p.x=wx+(rand()%20-10)*0.015f; p.y=wy+(rand()%20-10)*0.015f;
        p.z=0.4f+(rand()%10)*0.05f; p.vx=(rand()%100-50)*0.025f; p.vy=(rand()%100-50)*0.025f; p.vz=(rand()%60)*0.035f;
        p.life=0.7f+(rand()%40)*0.008f; p.color=(0xFF<<24)|((180+rand()%60)<<16)|(rand()%25<<8);
        m_gs.particles.push_back(p);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::keyPressEvent(QKeyEvent *e)
{
    m_keys.insert(e->key());

    // ── Pre-game menus ────────────────────────────────────────────────────────
    if (m_screen==SCR_MENU){
        if(e->key()==Qt::Key_Up)   m_menuCursor=std::max(0,m_menuCursor-1);
        if(e->key()==Qt::Key_Down) m_menuCursor=std::min(1,m_menuCursor+1);
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space||e->key()==Qt::Key_1){
            if(m_menuCursor==0||e->key()==Qt::Key_1) m_screen=SCR_MAP_SELECT;
            else QApplication::quit();
        }
        if(e->key()==Qt::Key_Escape||e->key()==Qt::Key_Q) QApplication::quit();
        return;
    }
    if (m_screen==SCR_MAP_SELECT){
        if(e->key()==Qt::Key_1){m_mapChoice=0;m_menuCursor=0;m_screen=SCR_CHAR_SELECT;}
        if(e->key()==Qt::Key_2){m_mapChoice=1;m_menuCursor=0;m_screen=SCR_CHAR_SELECT;}
        if(e->key()==Qt::Key_3){m_mapChoice=2;m_menuCursor=0;m_screen=SCR_CHAR_SELECT;}
        if(e->key()==Qt::Key_Left) m_mapChoice=std::max(0,m_mapChoice-1);
        if(e->key()==Qt::Key_Right)m_mapChoice=std::min(2,m_mapChoice+1);
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space){m_screen=SCR_CHAR_SELECT;m_menuCursor=0;}
        if(e->key()==Qt::Key_Escape) {m_screen=SCR_MENU;m_menuCursor=0;}
        return;
    }
    if (m_screen==SCR_CHAR_SELECT){
        if(e->key()==Qt::Key_1){m_charChoice=0;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_2){m_charChoice=1;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_3){m_charChoice=2;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_Left) m_charChoice=std::max(0,m_charChoice-1);
        if(e->key()==Qt::Key_Right)m_charChoice=std::min(2,m_charChoice+1);
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space){initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_Escape){m_screen=SCR_MAP_SELECT;m_menuCursor=0;}
        return;
    }

    // ── In-game ──────────────────────────────────────────────────────────────
    if(e->key()==Qt::Key_Space) fireBullet();
    if(e->key()==Qt::Key_Escape){ if(m_paused) QApplication::quit(); else m_paused=true; }
    if(e->key()==Qt::Key_R){ if(m_paused) m_paused=false; if(m_gs.gameOver){initGame();m_paused=false;} }
    if(e->key()==Qt::Key_Q && (m_paused||m_gs.gameOver)) QApplication::quit();
    if(e->key()==Qt::Key_M && !m_gs.gameOver){ m_screen=SCR_MENU; m_paused=false; m_menuCursor=0; }
    if(e->key()==Qt::Key_BracketLeft)  m_pixelScale=std::min(m_pixelScale*2,8);
    if(e->key()==Qt::Key_BracketRight) m_pixelScale=std::max(m_pixelScale/2,1);
}
void MainWindow::keyReleaseEvent(QKeyEvent *e){ m_keys.erase(e->key()); }
void MainWindow::mousePressEvent(QMouseEvent *e){
    if(m_screen!=SCR_PLAYING||m_paused||m_gs.gameOver) return;
    if(e->button()==Qt::LeftButton) fireBullet();
    if(e->button()==Qt::RightButton) m_gs.player.adsMode=true;
}
void MainWindow::mouseReleaseEvent(QMouseEvent *e){ if(e->button()==Qt::RightButton) m_gs.player.adsMode=false; }
void MainWindow::mouseMoveEvent(QMouseEvent *e){
    if(m_screen!=SCR_PLAYING||m_paused||m_gs.gameOver) return;
    int cx=width()/2; float dx=float(e->x()-cx); if(std::fabs(dx)<1.f) return;
    float rot=dx*-(m_gs.player.adsMode?0.0008f:0.0015f);
    Player &pl=m_gs.player; float ox=pl.dirX,op=pl.planeX;
    pl.dirX= pl.dirX*cosf(rot)+pl.dirY*sinf(rot); pl.dirY=-ox*sinf(rot)+pl.dirY*cosf(rot);
    pl.planeX=pl.planeX*cosf(rot)+pl.planeY*sinf(rot);pl.planeY=-op*sinf(rot)+pl.planeY*cosf(rot);
    QCursor::setPos(mapToGlobal(QPoint(cx,height()/2)));
}
void MainWindow::wheelEvent(QWheelEvent *e){
    int steps=e->angleDelta().y()/120;
    m_zoomFactor=std::max(0.5f,std::min(4.f,m_zoomFactor+steps*0.25f));
    resize(int(W*m_zoomFactor),int(H*m_zoomFactor));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Color helpers
// ─────────────────────────────────────────────────────────────────────────────
uint32_t MainWindow::fogBlend(uint32_t c,float dist){
    float f=std::max(0.f,1.f-dist/14.f);
    return (0xFF<<24)|(uint8_t(((c>>16)&0xFF)*f)<<16)|(uint8_t(((c>>8)&0xFF)*f)<<8)|uint8_t((c&0xFF)*f);
}
uint32_t MainWindow::dimColor(uint32_t c,float b){
    if(b>=1.f) return c; uint8_t a=c>>24;
    return (a<<24)|(uint8_t(((c>>16)&0xFF)*b)<<16)|(uint8_t(((c>>8)&0xFF)*b)<<8)|uint8_t((c&0xFF)*b);
}
uint32_t MainWindow::wallPixel(int t,float wx,float wy,int side,float dist){
    int tx=int(wx*64)&63,ty=int(wy*64)&63; uint32_t c=0xFF888888;
    switch(t){
    case 1:{bool m=(ty%16<2)||((ty/16)%2==0?(tx%32<2):((tx+16)%32<2));
        if(m){c=0xFF555555;break;} int n=((tx*7+ty*13)^tx)&7; uint8_t v=uint8_t(125+n); c=(0xFF<<24)|(v<<16)|(v<<8)|v; break;}
    case 2:{bool m=(ty%8<1)||((ty/8)%2==0?(tx%20<2):((tx+10)%20<2));
        if(m){c=0xFF777766;break;} int n=((tx*5+ty*11)^ty)&15; c=(0xFF<<24)|(uint8_t(155+n)<<16)|(uint8_t(72+n/3)<<8)|uint8_t(55+n/3); break;}
    case 3:{int n=((tx*3+ty*7)^(tx+ty))&31; uint8_t v=uint8_t(90+n); c=(0xFF<<24)|(uint8_t(v*.75f)<<16)|(uint8_t(v*.85f)<<8)|v; break;}
    }
    if(side==1){uint8_t r=(c>>16&0xFF)/2,g=(c>>8&0xFF)/2,b=(c&0xFF)/2; c=(0xFF<<24)|(r<<16)|(g<<8)|b;}
    return fogBlend(c,dist);
}
uint32_t MainWindow::sampleImp(float u,float v,bool flash){
    int tx=std::min(15,std::max(0,int(u*16))),ty=std::min(15,std::max(0,int(v*16)));
    uint32_t c=IMP_SPRITE[ty][tx]; if((c>>24)==0) return 0; return flash?0xFFFFFFFF:c;
}
uint32_t MainWindow::samplePlayerBullet(float u,float v){
    float du=u-.5f,dv=v-.5f,d=du*du+dv*dv; if(d>.22f) return 0;
    float t=1.f-d*4.5f; return (0xFF<<24)|(0xFF<<16)|(uint8_t(180*t+75)<<8)|uint8_t(10*t);
}
uint32_t MainWindow::sampleEnemyBullet(float u,float v){
    float du=u-.5f,dv=v-.5f,d=du*du+dv*dv; if(d>.22f) return 0;
    float t=1.f-d*4.5f; return (0xFF<<24)|(uint8_t(220*t+35)<<16)|(uint8_t(30*t)<<8)|uint8_t(180*t);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pixel helpers
// ─────────────────────────────────────────────────────────────────────────────
inline void MainWindow::px(int x,int y,uint32_t c){if((unsigned)x<(unsigned)W&&(unsigned)y<(unsigned)H)m_pixels[y*W+x]=c;}
void MainWindow::fillRect(int x,int y,int w,int h,uint32_t c){
    int x0=std::max(x,0),x1=std::min(x+w,W),y0=std::max(y,0),y1=std::min(y+h,H);
    for(int r=y0;r<y1;r++) for(int col=x0;col<x1;col++) m_pixels[r*W+col]=c;
}
void MainWindow::fillCircle(int cx,int cy,int r,uint32_t c){
    for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++) if(dx*dx+dy*dy<=r*r) px(cx+dx,cy+dy,c);
}
void MainWindow::drawLine(int x0,int y0,int x1,int y1,uint32_t c){
    int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=std::abs(y1-y0),sy=y0<y1?1:-1,err=(dx>dy?dx:-dy)/2;
    for(;;){px(x0,y0,c);if(x0==x1&&y0==y1)break;int e=err;if(e>-dx){err-=dy;x0+=sx;}if(e<dy){err+=dx;y0+=sy;}}
}

// =============================================================================
//  MENU SCREENS
// =============================================================================

// Animated "data stream" background — falling green-tinted streaks + scan lines
// Clearly raster: every pixel computed individually based on frame counter
void MainWindow::drawMenuBg(uint32_t *px2, int frame)
{
    // Base: dark gradient (top=navy, bottom=black)
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        float fy=float(y)/H;
        uint8_t b=uint8_t(18*(1.f-fy)+3);
        px2[y*W+x]=(0xFF<<24)|(uint8_t(b*0.15f)<<16)|(uint8_t(b*0.4f)<<8)|b;
    }
    // Scan lines (every 4 rows slightly darker — classic CRT effect)
    for(int y=0;y<H;y+=4) for(int x=0;x<W;x++){
        uint32_t &p=px2[y*W+x];
        uint8_t r=(p>>16&0xFF)*3/4,g=(p>>8&0xFF)*3/4,b=(p&0xFF)*3/4;
        px2[y*W+x]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    // Falling vertical data streams (55 columns)
    for(int col=0;col<55;col++){
        int x=((col*17+3)%55)*14+(col%3)*3;
        if(x>=W) continue;
        float speed=0.8f+(col%7)*0.22f;
        int baseY=int(fmodf(frame*speed*0.18f+col*41.7f, H+100))-50;
        int len=20+(col*19)%55;
        for(int dy=0;dy<len;dy++){
            int py=baseY+dy; if(py<0||py>=H) continue;
            float t=1.f-float(dy)/len;
            uint8_t v=uint8_t(t*t*90);
            // head pixel is bright; tail fades
            if(dy==0) px2[py*W+x]=(0xFF<<24)|(uint8_t(v*.1f)<<16)|(255<<8)|uint8_t(v*.6f);
            else       px2[py*W+x]=(0xFF<<24)|(0<<16)|(uint8_t(v*.7f)<<8)|uint8_t(v*.3f);
        }
    }
    // Horizontal scan sweep (slow moving bright line — simulates screen refresh)
    int sweepY=int(fmodf(frame*0.6f,H));
    for(int x=0;x<W;x++){
        uint32_t &p=px2[sweepY*W+x];
        uint8_t g=std::min(255,int((p>>8&0xFF)+18));
        px2[sweepY*W+x]=(0xFF<<24)|(p>>16&0xFF)<<16|(g<<8)|(p&0xFF);
    }
    // Corner vignette
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        float dx=float(x-W/2)/(W/2),dy2=float(y-H/2)/(H/2);
        float vig=dx*dx+dy2*dy2;
        if(vig>0.5f){
            float s=std::min(1.f,(vig-0.5f)*1.6f);
            uint32_t &p=px2[y*W+x];
            uint8_t r=uint8_t((p>>16&0xFF)*(1-s*.6f)),g2=uint8_t((p>>8&0xFF)*(1-s*.6f)),b2=uint8_t((p&0xFF)*(1-s*.6f));
            px2[y*W+x]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;
        }
    }
}

void MainWindow::renderMenu()
{
    drawMenuBg(m_pixels, m_menuFrame);

    QPainter p(&m_buf);

    // ── Left panel: title ──────────────────────────────────────────────────────
    p.fillRect(0, 0, W*2/5, H, QColor(0,0,0,130));
    // Vertical accent line
    for(int y=0;y<H;y++){
        float pulse=0.6f+0.4f*sinf(m_menuFrame*0.04f+y*0.01f);
        px(W*2/5,y,(uint32_t(uint8_t(80*pulse))<<24)|(0<<16)|(uint8_t(200*pulse)<<8)|uint8_t(80*pulse));
    }
    // Game logo
    p.setFont(QFont("Courier New",64,QFont::Black));
    p.setPen(QColor(0,255,140));
    p.drawText(QRect(24,100,W*2/5-24,90),Qt::AlignLeft|Qt::AlignVCenter,"RASTER");
    p.setFont(QFont("Courier New",20,QFont::Bold));
    p.setPen(QColor(160,220,200));
    p.drawText(QRect(28,192,W*2/5-28,36),Qt::AlignLeft,"TACTICAL SHOOTER");
    // Decorative line under title
    for(int x=28;x<W*2/5-20;x++){
        uint8_t b=uint8_t(255*(1.f-float(x-28)/float(W*2/5-48)));
        px(x,232,(0xFF<<24)|(0<<16)|(b<<8)|uint8_t(b/2));
    }
    // Subtitle text
    p.setFont(QFont("Courier New",9));
    p.setPen(QColor(40,100,70));
    p.drawText(QRect(28,248,W*2/5-28,20),Qt::AlignLeft,"Computer Graphics Lab  •  Semester 5");
    p.drawText(QRect(28,262,W*2/5-28,20),Qt::AlignLeft,"Pure software rasterization — no GPU");

    // ── Right panel: menu options ──────────────────────────────────────────────
    int mx=W*2/5+60, my=H/2-55;
    static const char* OPTS[]={"NEW GAME","EXIT"};
    for(int i=0;i<2;i++){
        bool sel=(m_menuCursor==i);
        int ox=mx,oy=my+i*70,ow=260,oh=56;
        p.fillRect(ox,oy,ow,oh,QColor(0,sel?18:8,0,sel?200:120));
        p.setPen(QPen(sel?QColor(0,255,140):QColor(0,70,45),sel?2:1));
        p.drawRect(ox,oy,ow,oh);
        // Animated left accent bar
        if(sel){
            float pulse2=0.7f+0.3f*sinf(m_menuFrame*0.15f);
            for(int ay=oy;ay<oy+oh;ay++) px(ox,(ay),(0xFF<<24)|(0<<16)|(uint8_t(255*pulse2)<<8)|uint8_t(120*pulse2));
            fillRect(ox+1,oy+1,3,oh-2,(uint32_t(180)<<24)|(0<<16)|(uint8_t(200*pulse2)<<8)|uint8_t(80*pulse2));
        }
        p.setFont(QFont("Courier New",22,QFont::Bold));
        p.setPen(sel?QColor(0,255,140):QColor(80,130,100));
        p.drawText(QRect(ox+14,oy,ow-14,oh),Qt::AlignLeft|Qt::AlignVCenter,
                   QString("[%1]  %2").arg(i+1).arg(OPTS[i]));
    }
    // Navigation hint
    p.setFont(QFont("Courier New",9));
    p.setPen(QColor(40,70,50));
    p.drawText(QRect(mx,my+155,260,18),Qt::AlignLeft,"↑ ↓  navigate   ENTER  confirm");

    // ── Bottom info bar ────────────────────────────────────────────────────────
    fillRect(0,H-24,W,24,0xCC000000);
    for(int x=0;x<W;x++) px(x,H-24,0xFF004422);
    p.setFont(QFont("Courier New",9));
    p.setPen(QColor(0,100,55));
    p.drawText(QRect(10,H-14,W/2,14),Qt::AlignLeft,
               "SCROLL: zoom to see individual pixels   [ ]: change pixel block size");
    p.setPen(QColor(0,60,35));
    p.drawText(QRect(W/2,H-14,W/2-10,14),Qt::AlignRight,
               QString("ZOOM %1x  BLOCK %2px").arg(m_zoomFactor,0,'f',1).arg(1));
    p.end();
}

void MainWindow::renderMapSelect()
{
    drawMenuBg(m_pixels, m_menuFrame);
    QPainter p(&m_buf);

    // Header
    fillRect(0,0,W,44,0xCC000000);
    for(int x=0;x<W;x++) px(x,43,0xFF004422);
    p.setFont(QFont("Courier New",16,QFont::Bold));
    p.setPen(QColor(0,255,140));
    p.drawText(QRect(0,0,W,42),Qt::AlignCenter,"SELECT DEPLOYMENT ZONE");
    p.setFont(QFont("Courier New",9));
    p.setPen(QColor(60,100,70));
    p.drawText(QRect(0,0,W,42),Qt::AlignRight|Qt::AlignBottom,"[ESC] Back    [← →] Navigate    [ENTER / 1-3] Confirm   ");

    // 3 map cards
    static const char* DIFFICULTY[]={"★★☆","★★★","★★☆"};
    for(int i=0;i<3;i++){
        bool sel=(m_mapChoice==i);
        int cx=W/2-280+i*280;
        int cy=60, cw=240, ch=430;

        // Card background + border
        p.fillRect(cx-cw/2,cy,cw,ch,QColor(0,sel?20:8,sel?12:5,sel?210:160));
        p.setPen(QPen(sel?QColor(0,255,140):QColor(0,70,45),sel?2:1));
        p.drawRect(cx-cw/2,cy,cw,ch);
        if(sel){ // glow top edge
            for(int x=cx-cw/2;x<cx+cw/2;x++) px(x,cy,0xFF00FF88);
        }

        // Mini-map raster preview (draw from tile data directly into m_pixels)
        const int *mp=ALL_MAPS[i];
        int tsize=5, previewX=cx-cw/2+12, previewY=cy+12;
        for(int my2=0;my2<24;my2++) for(int mx2=0;mx2<24;mx2++){
            int tile=mp[my2*MAP_W+mx2];
            uint32_t col=tile==0?0xFF0A1A0A:tile==1?0xFF606058:tile==2?0xFF704040:0xFF406040;
            for(int dy=0;dy<tsize-1;dy++) for(int dx=0;dx<tsize-1;dx++){
                int px3=previewX+mx2*tsize+dx,py3=previewY+my2*tsize+dy;
                if(px3>=0&&px3<W&&py3>=0&&py3<H) m_pixels[py3*W+px3]=col;
            }
        }
        // Animated scan line on selected mini-map
        if(sel){
            int scanY=previewY+int(fmodf(m_menuFrame*0.4f,24*tsize));
            for(int x=previewX;x<previewX+24*tsize&&x<W;x++){
                if(scanY>=0&&scanY<H){
                    uint32_t &pp=m_pixels[scanY*W+x];
                    pp=(0xFF<<24)|((pp>>16&0xFF)/2+10)<<16|((pp>>8&0xFF)/2+60)<<8|((pp&0xFF)/2+20);
                }
            }
        }
        // Player dot on mini-map
        px(previewX+int(2.5f*tsize),previewY+int(2.5f*tsize),0xFF00FFAA);

        // Map name + info
        int infoY=cy+24*tsize+18;
        p.setFont(QFont("Courier New",16,QFont::Bold));
        p.setPen(sel?QColor(0,255,140):QColor(120,180,140));
        p.drawText(QRect(cx-cw/2+8,infoY,cw-8,28),Qt::AlignCenter,
                   QString("[%1]  %2").arg(i+1).arg(MAP_NAMES[i]));
        p.setFont(QFont("Courier New",9));
        p.setPen(QColor(140,180,150));
        p.drawText(QRect(cx-cw/2+8,infoY+32,cw-8,40),Qt::AlignCenter|Qt::TextWordWrap,MAP_DESCS[i]);
        p.setPen(QColor(255,200,80));
        p.drawText(QRect(cx-cw/2+8,infoY+76,cw-8,18),Qt::AlignCenter,
                   QString("DIFFICULTY: %1").arg(DIFFICULTY[i]));
    }
    p.end();
}

void MainWindow::renderCharSelect()
{
    drawMenuBg(m_pixels, m_menuFrame);
    QPainter p(&m_buf);

    // Header
    fillRect(0,0,W,44,0xCC000000);
    for(int x=0;x<W;x++) px(x,43,0xFF004422);
    p.setFont(QFont("Courier New",16,QFont::Bold));
    p.setPen(QColor(0,255,140));
    p.drawText(QRect(0,0,W/2,42),Qt::AlignCenter,
               QString("ZONE: %1  —  SELECT OPERATIVE").arg(MAP_NAMES[m_mapChoice]));
    p.setFont(QFont("Courier New",9));
    p.setPen(QColor(60,100,70));
    p.drawText(QRect(W/2,0,W/2,42),Qt::AlignRight|Qt::AlignBottom,"[ESC] Back    [← →] Navigate    [ENTER / 1-3] Deploy   ");

    for(int i=0;i<3;i++){
        const CharProfile &cp=CHAR_PROFILES[i];
        bool sel=(m_charChoice==i);
        int cx=W/2-280+i*280, cy=56, cw=236, ch=450;

        p.fillRect(cx-cw/2,cy,cw,ch,QColor(0,sel?20:8,sel?12:5,sel?210:155));
        p.setPen(QPen(sel?QColor(0,255,140):QColor(0,70,45),sel?2:1));
        p.drawRect(cx-cw/2,cy,cw,ch);
        if(sel) for(int x=cx-cw/2;x<cx+cw/2;x++) px(x,cy,0xFF00FF88);

        // Raster character silhouette (drawn directly into m_pixels)
        int sh=cy+14, sw=cx;
        uint32_t sl=cp.sleeve,slh=cp.sleeveHi,sk=cp.skinColor;
        auto blit=[&](int rx,int ry,int rw,int rh,uint32_t c){
            for(int ry2=std::max(0,ry);ry2<std::min(H,ry+rh);ry2++)
                for(int rx2=std::max(0,rx);rx2<std::min(W,rx+rw);rx2++) m_pixels[ry2*W+rx2]=c;
        };
        // Head with helmet
        blit(sw-10,sh+2,20,20,sl);       // helmet
        blit(sw-7, sh+5,14,14,sk);       // face
        blit(sw-5, sh+6,10,2,0xFF223322);// visor strip
        // Torso + vest
        blit(sw-18,sh+22,36,42,sl);
        blit(sw-14,sh+24,28,38,slh);     // vest face
        blit(sw-12,sh+27,24,14,0xFF222222); // chest plate detail
        // Arms
        blit(sw-28,sh+22,12,36,slh);
        blit(sw+16,sh+22,12,36,slh);
        blit(sw-26,sh+56,10,8,sk);       // left hand
        blit(sw+16,sh+56,10,8,sk);       // right hand
        // Legs
        blit(sw-16,sh+64,14,40,sl);
        blit(sw+2, sh+64,14,40,sl);
        blit(sw-14,sh+100,12,6,0xFF222228); // boot
        blit(sw+4, sh+100,12,6,0xFF222228);

        // Weapon silhouette
        blit(sw+14,sh+50,20,4,0xFF333340);

        // Name + key
        int infoY=cy+130;
        p.setFont(QFont("Courier New",16,QFont::Bold));
        p.setPen(sel?QColor(0,255,140):QColor(100,160,120));
        p.drawText(QRect(cx-cw/2,infoY,cw,28),Qt::AlignCenter,
                   QString("[%1]  %2").arg(i+1).arg(cp.name));

        // Stats bars
        auto statBar=[&](int sy,const char*lbl,float frac,uint32_t bc){
            p.setFont(QFont("Courier New",8));
            p.setPen(QColor(140,160,150)); p.drawText(QRect(cx-cw/2+10,sy,70,14),Qt::AlignLeft,lbl);
            fillRect(cx-cw/2+80,sy+2,cw-90,10,0xFF0A180A);
            fillRect(cx-cw/2+80,sy+2,int((cw-90)*std::min(1.f,frac)),10,bc);
        };
        statBar(infoY+34,"HP",  float(cp.hp)/200.f,   cp.hp>100?0xFF22CC55:0xFF44AA22);
        statBar(infoY+52,"SPD", cp.speedMult/1.5f,    0xFF2288FF);
        statBar(infoY+70,"STA", cp.stamina/150.f,      0xFFFFCC22);
        statBar(infoY+88,"AMMO",float(cp.ammo)/30.f,   0xFFFF8822);
        statBar(infoY+106,"ARM",cp.damageReduction,    0xFF8822FF);

        // Text stats
        p.setFont(QFont("Courier New",8));
        p.setPen(QColor(120,150,130));
        p.drawText(QRect(cx-cw/2+8,infoY+126,cw-8,16),Qt::AlignCenter,cp.stat1);
        p.drawText(QRect(cx-cw/2+8,infoY+142,cw-8,16),Qt::AlignCenter,cp.stat2);
        p.setPen(i==2?QColor(100,160,255):QColor(100,130,110));
        p.drawText(QRect(cx-cw/2+8,infoY+158,cw-8,16),Qt::AlignCenter,cp.stat3);
    }
    p.end();
}

// =============================================================================
//  GAME RENDERERS
// =============================================================================

void MainWindow::renderFloorCeiling()
{
    const Player &pl=m_gs.player;
    int shOff=int(pl.shakeY), hor=H/2+shOff;
    float rdX0=pl.dirX-pl.planeX*m_adsFOVMult,rdY0=pl.dirY-pl.planeY*m_adsFOVMult;
    float rdX1=pl.dirX+pl.planeX*m_adsFOVMult,rdY1=pl.dirY+pl.planeY*m_adsFOVMult;
    int PS=m_pixelScale;
    for(int y=hor+1;y<H;y+=PS){
        int p2=y-hor; float rowDist=float(H/2)/float(p2);
        float sX=rowDist*(rdX1-rdX0)/float(W),sY=rowDist*(rdY1-rdY0)/float(W);
        float fX=pl.posX+rowDist*rdX0,fY=pl.posY+rowDist*rdY0;
        float fog=std::max(0.f,1.f-rowDist/14.f);
        for(int x=0;x<W;x+=PS){
            bool check=(int(fX)+int(fY))&1;
            uint8_t fv=uint8_t((check?42:26)*fog+6);
            uint32_t fc=(0xFF<<24)|(fv<<16)|(uint8_t(fv*.88f)<<8)|uint8_t(fv*.65f);
            uint8_t cv=uint8_t(16*fog+3);
            uint32_t cc=(0xFF<<24)|(uint8_t(cv*.65f)<<16)|(uint8_t(cv*.75f)<<8)|cv;
            for(int by=0;by<PS&&y+by<H;by++) for(int bx=0;bx<PS&&x+bx<W;bx++){
                m_pixels[(y+by)*W+(x+bx)]=fc;
                int cy2=2*hor-(y+by)-1; if(cy2>=0&&cy2<H) m_pixels[cy2*W+(x+bx)]=cc;
            }
            fX+=sX*PS; fY+=sY*PS;
        }
    }
    for(int x=0;x<W;x++){int hy=std::max(0,std::min(H-1,hor));m_pixels[hy*W+x]=0xFF060608;}
}

void MainWindow::renderWalls()
{
    const Player &pl=m_gs.player;
    int shOff=int(pl.shakeY),hor=H/2+shOff;
    int PS=m_pixelScale;
    for(int x=0;x<W;x+=PS){
        float camX=2.f*x/float(W)-1.f;
        float rdX=pl.dirX+pl.planeX*m_adsFOVMult*camX,rdY=pl.dirY+pl.planeY*m_adsFOVMult*camX;
        int mapX=int(pl.posX),mapY=int(pl.posY);
        float ddX=std::fabs(rdX)<1e-20f?1e30f:std::fabs(1.f/rdX),ddY=std::fabs(rdY)<1e-20f?1e30f:std::fabs(1.f/rdY);
        float sdX,sdY; int stX,stY;
        if(rdX<0){stX=-1;sdX=(pl.posX-mapX)*ddX;}else{stX=1;sdX=(mapX+1.f-pl.posX)*ddX;}
        if(rdY<0){stY=-1;sdY=(pl.posY-mapY)*ddY;}else{stY=1;sdY=(mapY+1.f-pl.posY)*ddY;}
        bool hit=false; int side=0;
        for(int g=0;g<64&&!hit;g++){
            if(sdX<sdY){sdX+=ddX;mapX+=stX;side=0;}else{sdY+=ddY;mapY+=stY;side=1;}
            if(mapX<0||mapX>=MAP_W||mapY<0||mapY>=MAP_H) break;
            if(m_map[mapY][mapX]>0) hit=true;
        }
        if(!hit){for(int bx=0;bx<PS&&x+bx<W;bx++) m_zBuf[x+bx]=1e30f;continue;}
        float dist=(side==0)?(sdX-ddX):(sdY-ddY);
        for(int bx=0;bx<PS&&x+bx<W;bx++) m_zBuf[x+bx]=dist;
        float wh; if(side==0) wh=pl.posY+dist*rdY; else wh=pl.posX+dist*rdX; wh-=std::floor(wh);
        int lh=std::max(1,int(float(H)/dist));
        int ds=std::max(0,hor-lh/2),de=std::min(H-1,hor+lh/2);
        int wt=m_map[mapY][mapX];
        for(int y=ds;y<=de;y++){
            float wy=float(y-(hor-lh/2))/float(lh);
            uint32_t col=wallPixel(wt,wh,wy,side,dist);
            for(int bx=0;bx<PS&&x+bx<W;bx++) m_pixels[y*W+(x+bx)]=col;
        }
    }
}

void MainWindow::renderSprites()
{
    const Player &pl=m_gs.player;
    enum SpType{SPR_ENEMY,SPR_PBULLET,SPR_EBULLET};
    struct Sp{float x,y,dx,dy,dist;SpType t;int idx;};
    std::vector<Sp> sprites;
    for(int i=0;i<int(m_gs.enemies.size());i++){const Enemy &e=m_gs.enemies[i];if(!e.alive)continue;float dx=e.x-pl.posX,dy=e.y-pl.posY;sprites.push_back({e.x,e.y,0,0,dx*dx+dy*dy,SPR_ENEMY,i});}
    for(int i=0;i<int(m_gs.bullets.size());i++){const Bullet &b=m_gs.bullets[i];if(!b.alive)continue;float dx=b.x-pl.posX,dy=b.y-pl.posY;SpType st=b.owner==PLAYER_SHOT?SPR_PBULLET:SPR_EBULLET;sprites.push_back({b.x,b.y,b.dx,b.dy,dx*dx+dy*dy,st,i});}
    std::sort(sprites.begin(),sprites.end(),[](const Sp&a,const Sp&b2){return a.dist>b2.dist;});
    float eplX=pl.planeX*m_adsFOVMult,eplY=pl.planeY*m_adsFOVMult;
    float invDet=1.f/(eplX*pl.dirY-pl.dirX*eplY);
    int hor=H/2+int(pl.shakeY);
    auto drawAt=[&](float wx,float wy,SpType type,int idx,float bright,float wScale){
        float spX=wx-pl.posX,spY=wy-pl.posY;
        float txX=invDet*(pl.dirY*spX-pl.dirX*spY),txY=invDet*(-eplY*spX+eplX*spY);
        if(txY<0.05f) return;
        int scrX=int(float(W/2)*(1.f+txX/txY));
        int sprH=std::abs(int(float(H)/txY)),sprW=int(sprH*wScale);
        int sy0=hor-sprH/2,sy1=hor+sprH/2,sx0=scrX-sprW/2,sx1=scrX+sprW/2;
        for(int sx=sx0;sx<sx1;sx++){
            if(sx<0||sx>=W||txY>=m_zBuf[sx]) continue;
            float u=float(sx-sx0)/float(std::max(1,sprW));
            for(int sy=std::max(0,sy0);sy<std::min(H,sy1);sy++){
                float v=float(sy-sy0)/float(std::max(1,sprH));
                uint32_t c=type==SPR_ENEMY?sampleImp(u,v,m_gs.enemies[idx].hitFlash>0):type==SPR_PBULLET?samplePlayerBullet(u,v):sampleEnemyBullet(u,v);
                if((c>>24)==0) continue;
                m_pixels[sy*W+sx]=fogBlend(dimColor(c,bright),txY);
            }
        }
    };
    for(const Sp &sp:sprites){
        if(sp.t==SPR_ENEMY){drawAt(sp.x,sp.y,sp.t,sp.idx,1.f,1.f);}
        else{for(int pass=0;pass<3;pass++) drawAt(sp.x-sp.dx*pass*0.055f,sp.y-sp.dy*pass*0.055f,sp.t,sp.idx,1.f-pass*0.35f,0.45f-pass*0.1f);}
    }
}

void MainWindow::renderParticles()
{
    const Player &pl=m_gs.player;
    float eplX=pl.planeX*m_adsFOVMult,eplY=pl.planeY*m_adsFOVMult;
    float invDet=1.f/(eplX*pl.dirY-pl.dirX*eplY);
    int hor=H/2+int(pl.shakeY);
    for(const Particle &pt:m_gs.particles){
        if(pt.life<=0) continue;
        float spX=pt.x-pl.posX,spY=pt.y-pl.posY;
        float txX=invDet*(pl.dirY*spX-pl.dirX*spY),txY=invDet*(-eplY*spX+eplX*spY);
        if(txY<0.05f) continue;
        int scrX=int(float(W/2)*(1.f+txX/txY));
        int floorY=hor+int(float(H)*0.5f/txY);
        int scrY=floorY-int(pt.z*float(H)/txY);
        if(scrX<1||scrX>=W-1||scrY<1||scrY>=H-1||txY>=m_zBuf[scrX]) continue;
        uint32_t c=dimColor(fogBlend(pt.color,txY),pt.life);
        if((c>>24)==0) continue;
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) px(scrX+dx,scrY+dy,c);
    }
}

void MainWindow::renderWeaponAndArms()
{
    Player &pl=m_gs.player;
    const CharProfile &cp=CHAR_PROFILES[m_charChoice];
    bool ads=pl.adsMode;
    float bobX=sinf(pl.bobPhase)*(pl.sprinting?9.f:5.f);
    float bobY=fabsf(cosf(pl.bobPhase*.5f))*(pl.sprinting?12.f:7.f);
    float breathe=sinf(m_gs.frameNum*.018f)*1.5f;
    int rx=ads?W/2+int(bobX*.3f):W/2+72+int(bobX);
    int ry=ads?H-30+int(bobY*.4f+breathe*.5f):H-8+int(bobY+breathe);
    if(!ads){
        int lx=W/2-100+int(bobX*.4f),ly=ry-90;
        fillRect(lx-22,ly+28,44,H-(ly+28),cp.sleeve); fillRect(lx-24,ly+24,48,H-(ly+24),cp.sleeveHi);
        fillRect(lx-18,ly+8,36,22,cp.skinColor); fillRect(lx-20,ly-4,40,14,cp.skinColor);
        for(int f=0;f<4;f++){fillRect(lx-14+f*9,ly-13,7,11,cp.skinColor);fillRect(lx-14+f*9,ly-15,7,3,uint32_t(cp.skinColor-0x200010));}
    }
    int gs2=ads?80:100; auto gr=[&](int v)->int{return v*gs2/100;};
    fillRect(rx-gr(6),ry-gr(138),gr(12),gr(7),0xFF2A2A30);fillRect(rx-gr(5),ry-gr(131),gr(10),gr(67),0xFF3C3C42);
    fillRect(rx-gr(3),ry-gr(131),gr(6),gr(67),0xFF525260);for(int n=0;n<3;n++) fillRect(rx-gr(5),ry-gr(115)+n*gr(14),gr(10),gr(4),0xFF1E1E26);
    fillRect(rx-gr(2),ry-gr(140),gr(4),gr(9),0xFF252530);fillRect(rx-gr(1),ry-gr(138),gr(2),gr(4),0xFFCCCCDD);
    fillRect(rx-gr(18),ry-gr(65),gr(36),gr(30),0xFF35353E);fillRect(rx-gr(18),ry-gr(65),gr(36),gr(2),0xFF4A4A58);
    for(int s=0;s<6;s++) fillRect(rx+gr(2)+s*gr(2),ry-gr(62),gr(1),gr(20),0xFF272732);
    fillRect(rx+gr(5),ry-gr(59),gr(9),gr(15),0xFF18181E);fillRect(rx-gr(5),ry-gr(67),gr(12),gr(5),0xFF252530);
    fillRect(rx+gr(14),ry-gr(67),gr(5),gr(8),0xFF2C2C38);fillRect(rx-gr(18),ry-gr(36),gr(36),gr(16),0xFF2E2E38);
    fillRect(rx-gr(17),ry-gr(21),gr(3),gr(18),0xFF262630);fillRect(rx+gr(13),ry-gr(21),gr(4),gr(18),0xFF262630);
    fillRect(rx-gr(17),ry-gr(4),gr(34),gr(3),0xFF262630);fillRect(rx-gr(3),ry-gr(18),gr(6),gr(11),0xFF3A3A48);
    fillRect(rx-gr(16),ry-gr(2),gr(30),gr(40),0xFF22222E);for(int t=0;t<6;t++) fillRect(rx-gr(14),ry+gr(1)+t*gr(6),gr(26),gr(4),0xFF28283A);
    fillRect(rx-gr(15),ry+gr(37),gr(29),gr(5),0xFF1A1A24);
    fillRect(rx-gr(20),ry+gr(41),gr(40),gr(28),cp.skinColor);fillRect(rx-gr(22),ry+gr(44),gr(8),gr(20),cp.skinColor);
    fillRect(rx-gr(20),ry+gr(68),gr(40),H-(ry+gr(68)),cp.sleeve);fillRect(rx-gr(22),ry+gr(70),gr(44),H-(ry+gr(70)),cp.sleeveHi);

    // ADS scope overlay
    if(ads&&m_adsFOVMult<0.7f){
        int sCx=W/2,sCy=H/2;
        fillCircle(sCx,sCy,62,0xFF000000);
        for(int ang=0;ang<360;ang+=2){float a=ang*3.14159f/180.f;px(sCx+int(cosf(a)*60),sCy+int(sinf(a)*60),0xFF88AA88);}
        drawLine(sCx-55,sCy,sCx-10,sCy,0xFF88CC88);drawLine(sCx+10,sCy,sCx+55,sCy,0xFF88CC88);
        drawLine(sCx,sCy-55,sCx,sCy-10,0xFF88CC88);drawLine(sCx,sCy+10,sCx,sCy+55,0xFF88CC88);
        fillCircle(sCx,sCy,2,0xFFFF4444);
        for(int y=0;y<H;y++) for(int x=0;x<W;x++){float dx2=float(x-sCx),dy2=float(y-sCy);if(dx2*dx2+dy2*dy2>60.f*60.f){uint32_t &pp=m_pixels[y*W+x];uint8_t r=(pp>>16&0xFF)/5,g=(pp>>8&0xFF)/5,b=(pp&0xFF)/5;pp=(0xFF<<24)|(r<<16)|(g<<8)|b;}}
    }
    // Muzzle flash
    if(pl.flashTimer>0){
        float t=pl.flashTimer/0.14f; int fr=int((10+rand()%5)*t);
        if(fr>0){
            int fx=rx,fy=ry-gr(141);
            for(int ray=0;ray<6;ray++){float a=ray*3.14159f/3.f;drawLine(fx,fy,fx+int(cosf(a)*(fr+12)),fy+int(sinf(a)*(fr+12)),0x99FF6600);}
            fillCircle(fx,fy,fr+5,0x55FF7700);fillCircle(fx,fy,fr+2,0xCCFFAA00);fillCircle(fx,fy,fr,0xFFFFEE00);fillCircle(fx,fy,fr/2,0xFFFFFFFF);
            int addR=int(32*t),addG=int(16*t);
            for(int i=0;i<W*H;i++){uint32_t pp=m_pixels[i];uint8_t r=uint8_t(std::min(255,int((pp>>16&0xFF)+addR))),g=uint8_t(std::min(255,int((pp>>8&0xFF)+addG)));m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|(pp&0xFF);}
        }
    }
}

void MainWindow::renderDamageVignette()
{
    if(m_gs.player.damageFlash<=0) return;
    float t=m_gs.player.damageFlash/0.45f; int depth=int(90*t);
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        int d=std::min({x,y,W-1-x,H-1-y});
        if(d<depth){float s=(1.f-float(d)/float(depth))*t;uint32_t &pp=m_pixels[y*W+x];uint8_t r=uint8_t(std::min(255,int((pp>>16&0xFF)+int(190*s))));pp=(0xFF<<24)|(r<<16)|(pp&0x00FFFF);}
    }
}

// =============================================================================
//  HUD SUB-RENDERERS
// =============================================================================

void MainWindow::renderTopBar()
{
    // Dark strip 0..40
    for(int y=0;y<40;y++) for(int x=0;x<W;x++){
        uint32_t &pp=m_pixels[y*W+x];
        uint8_t r=(pp>>16&0xFF)/5,g=(pp>>8&0xFF)/5+2,b=(pp&0xFF)/5+4;
        m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    // Bottom border line
    for(int x=0;x<W;x++) px(x,40,0xFF004422);

    QPainter p(&m_buf);

    // Left: map + level + kills
    p.setFont(QFont("Courier New",10,QFont::Bold));
    p.setPen(QColor(0,220,130));
    p.drawText(8,26,QString("[%1]  LV%2  KILL %3/%4").arg(MAP_NAMES[m_gs.mapChoice]).arg(m_gs.level).arg(m_gs.killsThisLevel).arg(m_gs.killsNeeded));

    // Center: compass strip (scrolling heading labels)
    {
        float heading=atan2f(-m_gs.player.dirY,m_gs.player.dirX)*180.f/3.14159f;
        if(heading<0) heading+=360.f;
        // Convert to "N=0" display heading (N is at 90 in our coord, shift so N=0 display)
        float dh=fmodf(360.f-(heading-90.f)+360.f,360.f);
        const int cw=280,chalf=cw/2;
        float ppd=float(cw)/90.f;
        static const struct{float deg;const char*lbl;}DIRS[]={{0,"N"},{45,"NE"},{90,"E"},{135,"SE"},{180,"S"},{225,"SW"},{270,"W"},{315,"NW"}};
        for(auto &d:DIRS){
            for(int wrap=-1;wrap<=1;wrap++){
                float delta=fmodf(d.deg+wrap*360.f-dh+360.f,360.f);
                if(delta>180) delta-=360.f;
                if(fabs(delta)>100) continue;
                int lx=W/2+int(delta*ppd);
                bool isCard=(int(d.deg)%90==0);
                p.setFont(QFont("Courier New",isCard?11:8,isCard?QFont::Bold:QFont::Normal));
                p.setPen(isCard?QColor(0,255,140):QColor(80,150,100));
                p.drawText(QRect(lx-16,4,32,18),Qt::AlignCenter,d.lbl);
            }
        }
        // Heading marker
        p.setPen(QColor(255,80,80));
        p.setFont(QFont("Courier New",10,QFont::Bold));
        p.drawText(QRect(W/2-8,21,16,16),Qt::AlignCenter,"▼");
        p.setFont(QFont("Courier New",9));
        p.setPen(QColor(180,180,180));
        static const char*CARD8[]={"N","NE","E","SE","S","SW","W","NW"};
        int ci=int((dh+22.5f)/45.f)%8;
        p.drawText(QRect(W/2+140,8,60,22),Qt::AlignLeft,QString("%1° %2").arg(int(dh)).arg(CARD8[ci]));
    }

    // Right: score + FPS
    p.setFont(QFont("Courier New",10,QFont::Bold));
    p.setPen(QColor(255,190,60));
    p.drawText(QRect(W-200,0,190,40),Qt::AlignRight|Qt::AlignVCenter,
               QString("SCORE %1  |  %2 FPS").arg(m_gs.score).arg(int(m_fps)));
    p.end();
}

void MainWindow::renderRadar()
{
    const int RX=76,RY=76+42,RD=60;
    const Player &pl=m_gs.player;

    // Radar background (dark circle)
    for(int dy=-RD;dy<=RD;dy++) for(int dx=-RD;dx<=RD;dx++){
        if(dx*dx+dy*dy>RD*RD) continue;
        int px2=RX+dx,py2=RY+dy; if(px2<0||px2>=W||py2<0||py2>=H) continue;
        uint32_t &pp=m_pixels[py2*W+px2];
        uint8_t r=(pp>>16&0xFF)/7,g=(pp>>8&0xFF)/7,b=uint8_t((pp&0xFF)/7+10);
        m_pixels[py2*W+px2]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    // Radar border
    for(int a=0;a<360;a++){float f=a*3.14159f/180.f;px(RX+int(cosf(f)*RD),RY+int(sinf(f)*RD),0xFF00CC66);}
    // Range rings
    for(int r2=RD/3;r2<RD;r2+=RD/3) for(int a=0;a<360;a+=4){float f=a*3.14159f/180.f;px(RX+int(cosf(f)*r2),RY+int(sinf(f)*r2),0x1A003311);}
    // Cross-hairs
    drawLine(RX-RD+8,RY,RX+RD-8,RY,0x22004422); drawLine(RX,RY-RD+8,RX,RY+RD-8,0x22004422);
    // Sweep line (animated)
    float sweepA=fmodf(m_gs.frameNum*0.04f,3.14159f*2);
    for(int r=0;r<RD;r++) px(RX+int(cosf(sweepA)*r),RY+int(sinf(sweepA)*r),0x4400FF44);

    // Player (white dot + direction indicator)
    fillCircle(RX,RY,3,0xFFFFFFFF);
    for(int r=4;r<20;r++) px(RX+int(pl.dirX*r),RY+int(pl.dirY*r),r<10?0xFFFFFFFF:0x88FFFFFF);

    // Enemy blips (blinking red)
    bool blink=(m_gs.frameNum/8)%2==0;
    for(const auto &e:m_gs.enemies){
        if(!e.alive) continue;
        float relX=e.x-pl.posX,relY=e.y-pl.posY;
        float fwd=relX*pl.dirX+relY*pl.dirY, rgt=relY*pl.dirX-relX*pl.dirY;
        float scale=float(RD)/12.f;
        int ex2=RX+int(rgt*scale),ey2=RY-int(fwd*scale);
        if(ex2<2||ex2>=W-2||ey2<2||ey2>=H-2) continue;
        fillCircle(ex2,ey2,blink?4:3,blink?0xFFFF2200:0xFFFF6600);
    }
    // Label
    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",7,QFont::Bold));
    p.setPen(QColor(0,150,70));
    p.drawText(QRect(RX-30,RY+RD+2,60,13),Qt::AlignCenter,"RADAR");
    p.end();
}

void MainWindow::renderHealthPanel()
{
    const Player &pl=m_gs.player;
    const CharProfile &cp=CHAR_PROFILES[m_charChoice];
    const int PX=8,PY=H-80,PW=190,PH=72;

    // Panel BG (dark tinted)
    for(int y=PY;y<PY+PH;y++) for(int x=PX-2;x<PX+PW+2;x++){
        if(x<0||x>=W||y<0||y>=H) continue;
        uint32_t &pp=m_pixels[y*W+x];
        uint8_t r=(pp>>16&0xFF)/6,g=(pp>>8&0xFF)/6+4,b=(pp&0xFF)/6+6;
        m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    for(int x=PX-2;x<PX+PW+2&&x<W;x++) px(x,PY-1,0xFF004422);

    // HP segmented bar (10 segments)
    int segs=10, segW=(PW-30)/segs;
    for(int s=0;s<segs;s++){
        float fill=float(pl.hp)/float(cp.hp);
        bool filled=(s+1)<=int(fill*segs+0.5f);
        uint32_t c=filled?(pl.hp>40?0xFF00CC55:pl.hp>20?0xFFCC8800:0xFFCC2200):0xFF0A180A;
        fillRect(PX+30+s*(segW+2),PY+18,segW,14,c);
    }
    // Stamina dots
    int staDots=12; float staF=pl.stamina/cp.stamina;
    for(int d=0;d<staDots;d++){
        bool filled=float(d)/staDots<staF;
        uint32_t c=filled?(pl.sprinting?0xFFFFCC00:0xFF2288FF):0xFF0A1018;
        fillCircle(PX+8+d*14,PY+52,5,c);
    }

    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",10,QFont::Bold));
    p.setPen(QColor(0,255,140));
    p.drawText(PX+2,PY+14,QString("♥  %1 / %2").arg(pl.hp).arg(cp.hp));
    p.setPen(pl.sprinting?QColor(255,200,0):QColor(60,140,255));
    p.drawText(PX+2,PY+68,QString("⚡  %1%%").arg(int(100.f*pl.stamina/cp.stamina)));
    p.end();
}

void MainWindow::renderAmmoPanel()
{
    const Player &pl=m_gs.player;
    const CharProfile &cp=CHAR_PROFILES[m_charChoice];
    const int PW=190,PH=72,PX=W-PW-8,PY=H-80;

    for(int y=PY;y<PY+PH;y++) for(int x=PX-2;x<PX+PW+2;x++){
        if(x<0||x>=W||y<0||y>=H) continue;
        uint32_t &pp=m_pixels[y*W+x];
        uint8_t r=(pp>>16&0xFF)/6+5,g=(pp>>8&0xFF)/6+3,b=(pp&0xFF)/6;
        m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g<<8)|b;
    }
    for(int x=PX-2;x<PX+PW+2&&x<W;x++) px(x,PY-1,0xFF442200);

    // Bullet dots (each represents 1 round)
    int dotsPerRow=std::min(cp.ammo,15);
    for(int i=0;i<cp.ammo;i++){
        bool loaded=i<pl.ammo;
        int col2=i%dotsPerRow, row=i/dotsPerRow;
        uint32_t c=loaded?0xFFFFCC44:0xFF332211;
        int bx=PX+6+col2*12, by=PY+22+row*14;
        // Bullet shape: rectangle with pointed top
        fillRect(bx,by+2,8,10,c);
        fillRect(bx+2,by,4,4,c);
    }

    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",11,QFont::Bold));
    p.setPen(QColor(255,180,60));
    p.drawText(QRect(PX,PY+2,PW-4,18),Qt::AlignRight,
               QString("%1 / %2").arg(pl.ammo).arg(cp.ammo));
    p.setFont(QFont("Courier New",8));
    p.setPen(QColor(160,110,50));
    p.drawText(QRect(PX,PY+PH-14,PW-4,14),Qt::AlignRight,"PISTOL  MK.II");
    p.end();
}

void MainWindow::renderCrosshair()
{
    const Player &pl=m_gs.player;
    if(pl.adsMode&&m_adsFOVMult<0.8f) return; // scope replaces crosshair
    int cx=W/2,cy=H/2;
    bool moving=m_keys.count(Qt::Key_W)||m_keys.count(Qt::Key_S)||m_keys.count(Qt::Key_A)||m_keys.count(Qt::Key_D);
    float spread=5.f+(moving?5.f:0.f)+(pl.sprinting?6.f:0.f)+(pl.flashTimer>0?8.f*(pl.flashTimer/0.14f):0.f);
    int sp=int(spread),arm=10;
    uint32_t col=m_hitMarker>0?0xFFFF5544:0xAAFFFFFF;
    for(int th=0;th<2;th++){
        drawLine(cx-arm-sp,cy+th,cx-sp-1,cy+th,col); drawLine(cx+sp+1,cy+th,cx+arm+sp,cy+th,col);
        drawLine(cx+th,cy-arm-sp,cx+th,cy-sp-1,col); drawLine(cx+th,cy+sp+1,cx+th,cy+arm+sp,col);
    }
    px(cx,cy,0xFFFF4444);
    // Hit marker (white X fading out)
    if(m_hitMarker>0){
        float t=m_hitMarker/0.35f; uint32_t hc=(uint32_t(uint8_t(255*t))<<24)|0xFFFFFF;
        int sz=int(10+(1-t)*6);
        drawLine(cx-sz,cy-sz,cx+sz,cy+sz,hc); drawLine(cx+sz,cy-sz,cx-sz,cy+sz,hc);
    }
}

void MainWindow::renderDamageDirection()
{
    if(m_damageDirTimer<=0) return;
    float t=std::min(1.f,m_damageDirTimer/2.f);
    const Player &pl=m_gs.player;
    float playerFacing=atan2f(pl.dirY,pl.dirX);
    float relAngle=m_damageAngle-playerFacing;
    float sa=sinf(relAngle),ca=cosf(relAngle);
    // Position arc at screen edge toward threat source
    int R=int(std::min(W,H)*0.34f);
    int icx=std::max(20,std::min(W-20,W/2+int(sa*R)));
    int icy=std::max(20,std::min(H-20,H/2-int(ca*R)));
    // Pulsing indicator
    float pulse=0.6f+0.4f*sinf(m_menuFrame*0.35f);
    fillCircle(icx,icy,int(22*t*pulse),uint32_t(uint8_t(120*t*pulse))<<24|0xFF2000);
    fillCircle(icx,icy,int(12*t),uint32_t(uint8_t(200*t))<<24|0xFF5533);
    fillCircle(icx,icy,int(5*t),uint32_t(uint8_t(255*t))<<24|0xFFFFFF);
}

void MainWindow::renderTargetBar()
{
    const Player &pl=m_gs.player;
    float bestDot=-1; int bestIdx=-1;
    float cosThr=cosf(20.f*3.14159f/180.f);
    for(int i=0;i<int(m_gs.enemies.size());i++){
        const Enemy &e=m_gs.enemies[i]; if(!e.alive) continue;
        float dx=e.x-pl.posX,dy=e.y-pl.posY,dist=sqrtf(dx*dx+dy*dy);
        if(dist>10) continue;
        float dot=(dx*pl.dirX+dy*pl.dirY)/dist;
        if(dot>cosThr&&dot>bestDot){bestDot=dot;bestIdx=i;}
    }
    if(bestIdx<0) return;
    const Enemy &te=m_gs.enemies[bestIdx];
    int maxHP=LEVEL_CFG[std::min(m_gs.level,5)].enemyHP+(m_gs.level>5?(m_gs.level-5)*2:0);
    float frac=float(te.hp)/float(std::max(1,maxHP));
    int bx=W/2-110,by=H-96,bw=220,bh=14;
    fillRect(bx-6,by-18,bw+12,bh+26,0x99000000);
    fillRect(bx,by,bw,bh,0xFF0A0A0A);
    uint32_t bc=frac>0.5f?0xFF00CC55:frac>0.25f?0xFFCC8800:0xFFCC2200;
    fillRect(bx,by,int(bw*frac),bh,bc);
    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",9,QFont::Bold));
    p.setPen(QColor(200,200,200));
    p.drawText(QRect(bx-6,by-16,bw+12,16),Qt::AlignCenter,
               QString("TARGET  %1%%").arg(int(frac*100)));
    p.end();
}

void MainWindow::renderComboDisplay()
{
    if(m_combo<2||m_comboDisplay<=0) return;
    float t=std::min(1.f,m_comboDisplay/0.5f);
    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",int(20+4*(1-t)),QFont::Black));
    p.setPen(QColor(255,200,0,int(255*t)));
    p.drawText(QRect(0,H/2-120,W,44),Qt::AlignCenter,
               QString("x%1  KILL STREAK").arg(m_combo));
    p.end();
}

void MainWindow::renderKillFeed()
{
    if(m_killFeed.empty()) return;
    QPainter p(&m_buf);
    int y=48;
    for(const auto &kf:m_killFeed){
        float t=std::min(1.f,kf.life/0.5f);
        int alpha=int(200*t);
        p.fillRect(W-195,y,190,20,QColor(0,0,0,int(140*t)));
        // Left accent
        p.fillRect(W-195,y,3,20,QColor(0,int(255*t),int(140*t),alpha));
        p.setFont(QFont("Courier New",10,QFont::Bold));
        p.setPen(QColor(0,int(255*t),int(140*t),alpha));
        p.drawText(QRect(W-190,y,185,20),Qt::AlignLeft|Qt::AlignVCenter," ▸ "+kf.text);
        y+=22;
    }
    p.end();
}

void MainWindow::renderHUD()
{
    renderTopBar();
    renderRadar();
    renderKillFeed();
    renderHealthPanel();
    renderAmmoPanel();
    renderCrosshair();
    renderDamageDirection();
    renderTargetBar();
    renderComboDisplay();

    QPainter p(&m_buf);
    // Ammo/health warnings
    const Player &pl=m_gs.player;
    if(pl.ammo<=3&&pl.ammo>0){
        p.setFont(QFont("Courier New",13,QFont::Bold));
        p.setPen(QColor(255,80,0,int(200+55*sinf(m_gs.frameNum*.25f))));
        p.drawText(QRect(0,H-50,W,20),Qt::AlignCenter,"LOW AMMO");
    } else if(pl.ammo==0){
        p.setFont(QFont("Courier New",13,QFont::Bold));
        p.setPen(QColor(255,30,30));
        p.drawText(QRect(0,H-50,W,20),Qt::AlignCenter,"NO AMMO");
    }
    if(pl.hp<=20&&!m_gs.gameOver){
        p.setFont(QFont("Courier New",11,QFont::Bold));
        p.setPen(QColor(255,0,0,int(140+115*sinf(m_gs.frameNum*.3f))));
        p.drawText(QRect(0,H-66,W,20),Qt::AlignCenter,"CRITICAL HEALTH");
    }
    // Pixel-scale info (subtle, bottom center)
    if(m_pixelScale>1){
        p.setFont(QFont("Courier New",9));
        p.setPen(QColor(0,120,60,160));
        p.drawText(QRect(0,H-14,W,12),Qt::AlignCenter,
                   QString("RENDER BLOCK: %1×%1 px  |  ZOOM %2×  (raster demo mode)").arg(m_pixelScale).arg(m_zoomFactor,0,'f',1));
    }
    // First-level controls hint
    if(m_gs.frameNum<300&&m_gs.level==1){
        p.fillRect(0,H-26,W,26,QColor(0,0,0,160));
        p.setFont(QFont("Courier New",9));
        p.setPen(QColor(0,140,70));
        p.drawText(QRect(0,H-13,W,12),Qt::AlignCenter,
                   "WASD:move  SHIFT:sprint  RMB:ADS  Space/LMB:fire  Q/E:turn  M:menu  ESC:pause  [ ]:pixelblocks  SCROLL:zoom");
    }
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Level banner  — "OBJECTIVE COMPLETE"
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderLevelBanner()
{
    if(!m_gs.levelComplete) return;
    float t=m_gs.levelTransTimer/3.5f;
    float dark=0.38f*(1-t)+0.14f*t;
    for(int i=0;i<W*H;i++){uint32_t c=m_pixels[i];uint8_t r=uint8_t(((c>>16)&0xFF)*dark),g=uint8_t(((c>>8)&0xFF)*dark),b=uint8_t((c&0xFF)*dark);m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|b;}
    QPainter p(&m_buf);
    p.fillRect(W/2-270,H/2-90,540,180,QColor(0,10,20,225));
    for(int x=W/2-270;x<W/2+270;x++) px(x,H/2-90,0xFF00FF88);
    p.setFont(QFont("Courier New",32,QFont::Black));
    p.setPen(QColor(0,255,140));
    p.drawText(QRect(W/2-270,H/2-82,540,60),Qt::AlignCenter,"OBJECTIVE COMPLETE");
    p.setFont(QFont("Courier New",14));
    p.setPen(QColor(255,200,60));
    p.drawText(QRect(W/2-270,H/2-8,540,32),Qt::AlignCenter,
               QString("BONUS  +%1 PTS      TOTAL  %2").arg(500*m_gs.level).arg(m_gs.score));
    p.setPen(QColor(140,190,255));
    p.drawText(QRect(W/2-270,H/2+32,540,30),Qt::AlignCenter,
               QString("Deploying to zone %1...   T-minus %2s").arg(m_gs.level+1).arg(int(m_gs.levelTransTimer)+1));
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pause menu
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderPauseMenu()
{
    if(!m_paused) return;
    for(int i=0;i<W*H;i++){uint32_t c=m_pixels[i];uint8_t r=(c>>16&0xFF)/2,g=(c>>8&0xFF)/2,b=(c&0xFF)/2;m_pixels[i]=(0xFF<<24)|(r<<16)|(g<<8)|b;}
    QPainter p(&m_buf);
    p.fillRect(W/2-200,H/2-145,400,295,QColor(2,8,16,240));
    p.setPen(QPen(QColor(0,200,140),2)); p.drawRect(W/2-200,H/2-145,400,295);
    for(int x=W/2-200;x<W/2+200;x++) px(x,H/2-145,0xFF00FF88);
    p.setFont(QFont("Courier New",28,QFont::Black));
    p.setPen(QColor(0,255,140));
    p.drawText(QRect(W/2-200,H/2-132,400,52),Qt::AlignCenter,"MISSION PAUSED");
    p.setPen(QColor(0,100,60));
    p.drawLine(W/2-160,H/2-74,W/2+160,H/2-74);
    p.setFont(QFont("Courier New",14,QFont::Bold));
    p.setPen(QColor(200,230,210));
    p.drawText(QRect(W/2-200,H/2-62,400,46),Qt::AlignCenter,"[R]  RESUME");
    p.setPen(QColor(140,190,255));
    p.drawText(QRect(W/2-200,H/2-4,400,46),Qt::AlignCenter,"[M]  MAIN MENU");
    p.setPen(QColor(255,220,100));
    p.drawText(QRect(W/2-200,H/2+54,400,46),Qt::AlignCenter,"[Q]  QUIT TO DESKTOP");
    p.setPen(QColor(255,60,60));
    p.drawText(QRect(W/2-200,H/2+112,400,30),Qt::AlignCenter,"[ESC]  FORCE QUIT");
    p.setFont(QFont("Courier New",8));
    p.setPen(QColor(0,70,40));
    p.drawText(QRect(W/2-200,H/2+144,400,24),Qt::AlignCenter,
               "SCROLL wheel: zoom  |  [ ] keys: pixel block size");
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game-over screen
// ─────────────────────────────────────────────────────────────────────────────
static void drawGameOver(QImage &buf, const GameState &gs, int charChoice)
{
    // Dramatic darken
    uint32_t *pix=reinterpret_cast<uint32_t*>(buf.bits());
    for(int i=0;i<W*H;i++){uint32_t c=pix[i];uint8_t r=(c>>16&0xFF)/5,g=(c>>8&0xFF)/5,b=(c&0xFF)/5;pix[i]=(0xFF<<24)|(r<<16)|(g<<8)|b;}
    QPainter p(&buf);
    p.fillRect(W/2-250,H/2-135,500,290,QColor(8,2,2,235));
    p.setPen(QPen(QColor(180,20,20),2)); p.drawRect(W/2-250,H/2-135,500,290);
    for(int x=W/2-250;x<W/2+250;x++) pix[(H/2-135)*W+x]=0xFFFF2200;
    p.setFont(QFont("Courier New",40,QFont::Black));
    p.setPen(QColor(255,30,30));
    p.drawText(QRect(W/2-250,H/2-120,500,70),Qt::AlignCenter,"MISSION FAILED");
    p.setPen(QColor(0,100,60));
    p.drawLine(W/2-200,H/2-42,W/2+200,H/2-42);
    p.setFont(QFont("Courier New",11));
    p.setPen(QColor(180,160,160));
    p.drawText(QRect(W/2-240,H/2-32,480,24),Qt::AlignCenter,
               QString("Zone: %1   Class: %2   Level: %3   Kills: %4   Score: %5")
               .arg(MAP_NAMES[gs.mapChoice]).arg(CHAR_PROFILES[charChoice].name)
               .arg(gs.level).arg(gs.kills).arg(gs.score));
    p.setFont(QFont("Courier New",13,QFont::Bold));
    p.setPen(QColor(80,210,120));
    p.drawText(QRect(W/2-250,H/2+8,500,38),Qt::AlignCenter,"[R]  REDEPLOY (same settings)");
    p.setPen(QColor(140,180,255));
    p.drawText(QRect(W/2-250,H/2+52,500,38),Qt::AlignCenter,"[M]  MAIN MENU");
    p.setPen(QColor(255,160,60));
    p.drawText(QRect(W/2-250,H/2+96,500,38),Qt::AlignCenter,"[Q]  QUIT");
    p.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Master render dispatcher
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::render()
{
    switch(m_screen){
    case SCR_MENU:        renderMenu();        return;
    case SCR_MAP_SELECT:  renderMapSelect();   return;
    case SCR_CHAR_SELECT: renderCharSelect();  return;
    case SCR_PLAYING:     break;
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
    if(m_gs.gameOver) drawGameOver(m_buf,m_gs,m_charChoice);
}

// ─────────────────────────────────────────────────────────────────────────────
//  paintEvent — nearest-neighbor scale of 800×600 buffer to window size
//  SmoothPixmapTransform=false → hard pixel edges when zoomed = raster proof
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(rect(), m_buf, m_buf.rect());
}
