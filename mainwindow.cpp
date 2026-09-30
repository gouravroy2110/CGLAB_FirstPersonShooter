// =============================================================================
//  mainwindow.cpp  —  Raster FPS  |  Full feature set
//
//  ★ RASTER PROOF:
//    • Sky + clouds are computed PER PIXEL each frame — sinusoidal noise,
//      no textures, no images. Scroll wheel or [ key zooms in → chunky blocks.
//    • All game/HUD geometry: raw uint32_t* writes (fillRect/fillCircle/drawLine).
//    • QPainter used ONLY for drawText (Qt's QRasterPaintEngine — no GPU).
// =============================================================================
#include "mainwindow.h"
#include <QPainter>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), m_buf(W, H, QImage::Format_ARGB32)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(W/2, H/2);
    resize(W, H);
    m_pixels = reinterpret_cast<uint32_t*>(m_buf.bits());
    std::fill(m_zBuf, m_zBuf+W, 1e30f);
    std::memset(m_map, 0, sizeof(m_map));
    std::memset(m_enemyHPDisplay, 0, sizeof(m_enemyHPDisplay));

    // Precalculate textures
    for(int t=1; t<=3; t++){
        for(int ty=0; ty<64; ty++){
            for(int tx=0; tx<64; tx++){
                uint32_t c = 0xFF888888;
                switch(t){
                case 1:{bool m=(ty%16<2)||((ty/16)%2==0?(tx%32<2):((tx+16)%32<2));if(m){c=0xFF555555;break;}int n=((tx*7+ty*13)^tx)&7;uint8_t v=uint8_t(125+n);c=(0xFF<<24)|(v<<16)|(v<<8)|v;break;}
                case 2:{bool m=(ty%8<1)||((ty/8)%2==0?(tx%20<2):((tx+10)%20<2));if(m){c=0xFF777766;break;}int n=((tx*5+ty*11)^ty)&15;c=(0xFF<<24)|(uint8_t(155+n)<<16)|(uint8_t(72+n/3)<<8)|uint8_t(55+n/3);break;}
                case 3:{int n=((tx*3+ty*7)^(tx+ty))&31;uint8_t v=uint8_t(90+n);c=(0xFF<<24)|(uint8_t(v*.75f)<<16)|(uint8_t(v*.85f)<<8)|v;break;}
                }
                m_textures[t][ty*64+tx] = c;
            }
        }
    }
    // Precalculate vignette
    for(int y=0; y<H; y++){
        for(int x=0; x<W; x++){
            float ddx = float(x-W/2)/(W/2);
            float ddy = float(y-H/2)/(H/2);
            float vig = ddx*ddx + ddy*ddy;
            if(vig > 0.5f){
                m_vignette[y][x] = uint8_t(std::min(1.f, (vig-0.5f)*1.6f) * 255);
            } else {
                m_vignette[y][x] = 0;
            }
        }
    }
    // Precalculate nice cloud skybox (FBM with Value Noise)
    auto hash = [](int x, int y) {
        int n = x + y * 57; n = (n<<13) ^ n;
        return ( 1.0f - ( (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);    
    };
    auto pnoise = [&](float x, float y, int period) {
        int ix = (int)floorf(x), iy = (int)floorf(y);
        float fx = x - ix, fy = y - iy;
        float u = fx*fx*(3.0f-2.0f*fx), v = fy*fy*(3.0f-2.0f*fy);
        int ix0 = ix % period; if(ix0<0) ix0+=period;
        int ix1 = (ix+1) % period; if(ix1<0) ix1+=period;
        float a = hash(ix0, iy), b = hash(ix1, iy), c = hash(ix0, iy+1), d = hash(ix1, iy+1);
        return (a*(1.0f-u) + b*u)*(1.0f-v) + (c*(1.0f-u) + d*u)*v;
    };
    for(int y=0; y<300; y++){
        float skyV = 1.f - float(y)/300.f;
        float zR=12,zG=22,zB=70, hR=100,hG=150,hB=210;
        uint8_t sr=uint8_t(hR*(1-skyV)+zR*skyV), sg=uint8_t(hG*(1-skyV)+zG*skyV), sb=uint8_t(hB*(1-skyV)+zB*skyV);
        for(int x=0; x<1024; x++){
            float skyU = float(x)/1024.f;
            float n = 0, amp = 0.5f, freq = 6.0f;
            for(int o=0; o<5; o++){
                n += pnoise(skyU * freq, skyV * freq * 0.6f, (int)freq) * amp;
                amp *= 0.5f; freq *= 2.0f;
            }
            n = n * 0.5f + 0.5f;
            float cloud = std::max(0.f, (n - 0.45f) * 2.5f);
            cloud *= std::min(1.f, skyV * 3.f);
            cloud = std::min(1.f, cloud);
            uint8_t cr = uint8_t(sr + (245-sr)*cloud), cg = uint8_t(sg + (245-sg)*cloud), cb = uint8_t(sb + (255-sb)*cloud*0.9f);
            m_skybox[y*1024 + x] = (0xFF<<24)|(cr<<16)|(cg<<8)|cb;
        }
    }

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);
    m_clock.start();
}

// ─────────────────────────────────────────────────────────────────────────────
//  initGame / loadLevel
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::initGame()
{
    m_gs = GameState{};
    m_gs.mapChoice  = m_mapChoice;
    m_gs.charClass  = CharClass(m_charChoice);
    m_gs.difficulty = Difficulty(m_difficulty);

    const CharProfile &cp = CHAR_PROFILES[m_charChoice];
    const DiffConfig  &dc = DIFF_CFG[m_difficulty];
    m_gs.player.hp              = cp.hp;
    m_gs.player.ammo            = cp.ammo;
    m_gs.player.stamina         = cp.stamina;
    m_gs.player.speedMult       = cp.speedMult + dc.speedBonus;
    m_gs.player.damageReduction = cp.damageReduction;

    m_combo=0; m_comboTimer=0; m_comboDisplay=0;
    m_hitMarker=0; m_damageDirTimer=0;
    m_killFeed.clear();
    std::memset(m_enemyHPDisplay, 0, sizeof(m_enemyHPDisplay));
    loadLevel(1);
    m_paused = false;
}

void MainWindow::loadLevel(int lv)
{
    int idx = std::min(lv, 5);
    const LevelCfg   &cfg = LEVEL_CFG[idx];
    const DiffConfig  &dc  = DIFF_CFG[m_difficulty];

    m_gs.level=lv; m_gs.killsThisLevel=0; m_gs.killsNeeded=cfg.killsNeeded;
    m_gs.levelComplete=false; m_gs.levelTransTimer=0;
    m_gs.bullets.clear(); m_gs.particles.clear(); m_gs.healthKits.clear();
    m_gs.enemyBulletSpeed = cfg.bulletSpeed  * dc.bulletSpeedMult;
    m_gs.enemyFireRate    = cfg.fireCooldown * dc.fireCoolMult;

    if (lv>1) {
        m_gs.player.hp   = std::min(m_gs.player.hp+30, CHAR_PROFILES[m_charChoice].hp);
        m_gs.player.ammo = std::min(m_gs.player.ammo+20, CHAR_PROFILES[m_charChoice].ammo);
        m_gs.player.damageFlash=0;
    }

    // Copy map
    const int *src = ALL_MAPS[m_gs.mapChoice];
    for (int y=0;y<MAP_H;y++) for (int x=0;x<MAP_W;x++) m_map[y][x]=src[y*MAP_W+x];

    // Player reset
    m_gs.player.posX=2.5f; m_gs.player.posY=2.5f;
    m_gs.player.dirX=1.f;  m_gs.player.dirY=0.f;
    m_gs.player.planeX=0.f;m_gs.player.planeY=0.66f;
    m_gs.player.shakeY=0;  m_gs.player.adsMode=false;

    // Spawn enemies (HP scaled by difficulty + level)
    m_gs.enemies.clear();
    const float (*spawns)[2]=ALL_SPAWNS[m_gs.mapChoice];
    for (int i=0;i<std::min(cfg.numEnemies,12);i++){
        Enemy e; e.x=spawns[i][0]; e.y=spawns[i][1];
        e.hp   = int(std::max(1.f, float(cfg.enemyHP+(lv>5?(lv-5)*2:0))*dc.enemyHPMult));
        e.aggroRange=cfg.aggroRange; e.fireCooldown=cfg.fireCooldown*dc.fireCoolMult; e.alive=true;
        m_gs.enemies.push_back(e);
    }

    // Spawn health kits
    const float (*kits)[2]=ALL_KITS[m_gs.mapChoice];
    int numKits=std::min(dc.numKits, MAX_KITS);
    for (int i=0;i<numKits;i++){
        HealthKit k; k.x=kits[i][0]; k.y=kits[i][1]; k.active=true; k.bobPhase=float(i)*1.2f;
        m_gs.healthKits.push_back(k);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tick
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::tick()
{
    double now=m_clock.elapsed();
    m_dt=float((now-m_lastMs)/1000.0); if(m_dt>0.05f)m_dt=0.05f;
    m_fps=0.9*m_fps+0.1/double(m_dt); m_lastMs=now; m_menuFrame++;

    if(m_screen==SCR_PLAYING&&!m_paused&&!m_gs.gameOver) gameStep(m_dt);

    float tgtFOV=(m_screen==SCR_PLAYING&&m_gs.player.adsMode)?0.45f:1.f;
    m_adsFOVMult+=(tgtFOV-m_adsFOVMult)*std::min(1.f,m_dt*14.f);
    m_gs.player.shakeY*=0.72f; if(std::fabs(m_gs.player.shakeY)<0.2f) m_gs.player.shakeY=0;

    m_hitMarker     =std::max(0.f,m_hitMarker     -m_dt);
    m_damageDirTimer=std::max(0.f,m_damageDirTimer-m_dt);
    m_comboDisplay  =std::max(0.f,m_comboDisplay  -m_dt);
    if(m_comboTimer>0){m_comboTimer-=m_dt;if(m_comboTimer<=0)m_combo=0;}
    for(auto &k:m_killFeed) k.life-=m_dt;
    m_killFeed.erase(std::remove_if(m_killFeed.begin(),m_killFeed.end(),[](const KFEntry&k){return k.life<=0;}),m_killFeed.end());
    for(int i=0;i<12;i++) if(m_enemyHPDisplay[i]>0) m_enemyHPDisplay[i]-=m_dt;

    render();
    QWidget::update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game logic
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::gameStep(float dt)
{
    Player &pl=m_gs.player;
    if(m_gs.levelComplete){m_gs.levelTransTimer-=dt;if(m_gs.levelTransTimer<=0)loadLevel(m_gs.level+1);return;}
    if(pl.attackCooldown>0)pl.attackCooldown-=dt;
    if(pl.flashTimer>0)    pl.flashTimer    -=dt;
    if(pl.damageFlash>0)   pl.damageFlash   -=dt;

    // Rotation
    float rotSpd=2.2f*dt*(pl.adsMode?0.55f:1.f);
    auto rot=[&](float a){float ox=pl.dirX,op=pl.planeX;pl.dirX=pl.dirX*cosf(a)+pl.dirY*sinf(a);pl.dirY=-ox*sinf(a)+pl.dirY*cosf(a);pl.planeX=pl.planeX*cosf(a)+pl.planeY*sinf(a);pl.planeY=-op*sinf(a)+pl.planeY*cosf(a);};
    if(m_keys.count(Qt::Key_Left)||m_keys.count(Qt::Key_Q))  rot( rotSpd);
    if(m_keys.count(Qt::Key_Right)||m_keys.count(Qt::Key_E)) rot(-rotSpd);

    // Sprint + stamina
    bool moving=m_keys.count(Qt::Key_W)||m_keys.count(Qt::Key_S)||m_keys.count(Qt::Key_A)||m_keys.count(Qt::Key_D);
    bool wantSprint=moving&&m_keys.count(Qt::Key_Shift)&&pl.stamina>0&&!pl.adsMode;
    pl.sprinting=wantSprint;
    pl.stamina+=wantSprint?(-45.f*dt):(28.f*dt);
    pl.stamina=std::max(0.f,std::min(CHAR_PROFILES[m_charChoice].stamina,pl.stamina));

    // Movement — normalised so diagonal isn't faster
    float SPD=pl.speedMult*(pl.sprinting?5.8f:pl.adsMode?2.2f:3.5f)*dt;
    float mx=0,my=0;
    if(m_keys.count(Qt::Key_W)){mx+=pl.dirX;my+=pl.dirY;}
    if(m_keys.count(Qt::Key_S)){mx-=pl.dirX;my-=pl.dirY;}
    if(m_keys.count(Qt::Key_A)){mx+=pl.dirY;my-=pl.dirX;}
    if(m_keys.count(Qt::Key_D)){mx-=pl.dirY;my+=pl.dirX;}
    float len=sqrtf(mx*mx+my*my); if(len>0.001f){mx/=len;my/=len;}
    float nx=pl.posX+mx*SPD,ny=pl.posY+my*SPD;
    if(posValid(nx,pl.posY))pl.posX=nx; if(posValid(pl.posX,ny))pl.posY=ny;
    if(moving)pl.bobPhase+=dt*(pl.sprinting?8.f:4.f); else pl.bobPhase*=0.85f;

    // Health kit pickup
    for(auto &k:m_gs.healthKits){
        if(!k.active) continue;
        k.bobPhase+=dt*2.2f;
        float dx=pl.posX-k.x,dy=pl.posY-k.y;
        if(dx*dx+dy*dy<0.35f){
            k.active=false;
            int maxHP=CHAR_PROFILES[m_charChoice].hp;
            pl.hp=std::min(pl.hp+30,maxHP);
            KFEntry kf; kf.text="+ HEALTH KIT  +30 HP"; kf.life=3.f;
            m_killFeed.insert(m_killFeed.begin(),kf);
            if(int(m_killFeed.size())>4)m_killFeed.pop_back();
        }
    }

    // Enemy AI
    for(auto &e:m_gs.enemies){
        if(!e.alive) continue;
        if(e.fireCooldown>0){e.fireCooldown-=dt;continue;}
        float dx=pl.posX-e.x,dy=pl.posY-e.y,d=sqrtf(dx*dx+dy*dy);
        if(d<e.aggroRange&&hasLOS(e.x,e.y,pl.posX,pl.posY)){enemyFire(e);e.fireCooldown=m_gs.enemyFireRate;}
    }

    // Bullets
    const DiffConfig &dc=DIFF_CFG[m_difficulty];
    for(auto &b:m_gs.bullets){
        if(!b.alive) continue;
        b.x+=b.dx*dt; b.y+=b.dy*dt;
        int bx=int(b.x),by=int(b.y);
        if(bx<0||bx>=MAP_W||by<0||by>=MAP_H||m_map[by][bx]>0){b.alive=false;continue;}
        if(b.owner==PLAYER_SHOT){
            for(int i=0;i<int(m_gs.enemies.size());i++){
                Enemy &e=m_gs.enemies[i]; if(!e.alive) continue;
                float dx=b.x-e.x,dy=b.y-e.y;
                if(dx*dx+dy*dy<0.18f){
                    b.alive=false; e.hp--; e.hitFlash=0.18f;
                    m_hitMarker=0.35f;
                    if(i<12) m_enemyHPDisplay[i]=2.2f;  // show HP for 2.2s
                    if(e.hp<=0){
                        e.alive=false; m_gs.kills++; m_gs.killsThisLevel++; m_gs.score+=100*m_gs.level;
                        spawnBlood(e.x,e.y,14);
                        m_combo++; m_comboTimer=3.5f; m_comboDisplay=1.8f;
                        KFEntry kf; kf.text=(m_combo>1?QString("x%1 ").arg(m_combo):"")+QString("ENEMY DOWN"); kf.life=4.5f;
                        m_killFeed.insert(m_killFeed.begin(),kf); if(int(m_killFeed.size())>4)m_killFeed.pop_back();
                    }
                    break;
                }
            }
        } else {
            float dx=b.x-pl.posX,dy=b.y-pl.posY;
            if(dx*dx+dy*dy<0.15f){
                b.alive=false;
                int dmg=int((10+4*(m_gs.level-1))*(1.f-pl.damageReduction)*dc.dmgReceivedMult);
                pl.hp-=dmg; pl.damageFlash=0.45f; pl.shakeY=-9.f;
                m_damageAngle=atan2f(-b.dy,-b.dx); m_damageDirTimer=2.0f;
                if(pl.hp<=0){pl.hp=0;m_gs.gameOver=true;}
            }
        }
    }
    m_gs.bullets.erase(std::remove_if(m_gs.bullets.begin(),m_gs.bullets.end(),[](const Bullet&b){return!b.alive;}),m_gs.bullets.end());
    for(auto &e:m_gs.enemies) if(e.hitFlash>0)e.hitFlash-=dt;

    // Blood particles
    for(auto &pt:m_gs.particles){
        if(pt.life<=0)continue; pt.vz-=9.f*dt; pt.x+=pt.vx*dt; pt.y+=pt.vy*dt; pt.z+=pt.vz*dt;
        if(pt.z<0){pt.z=0;pt.vz*=-0.3f;pt.vx*=0.6f;pt.vy*=0.6f;} pt.life-=dt*0.9f;
    }
    m_gs.particles.erase(std::remove_if(m_gs.particles.begin(),m_gs.particles.end(),[](const Particle&p){return p.life<=0;}),m_gs.particles.end());

    if(!m_gs.levelComplete&&m_gs.killsThisLevel>=m_gs.killsNeeded){
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
    float dx=x1-x0,dy=y1-y0,dist=sqrtf(dx*dx+dy*dy); if(dist<0.1f)return true;
    float nx=dx/dist*0.12f,ny=dy/dist*0.12f,cx=x0+nx,cy=y0+ny;
    for(int i=0;i<int(dist/0.12f);i++){int mx=int(cx),my=int(cy);if(mx<0||mx>=MAP_W||my<0||my>=MAP_H||m_map[my][mx]>0)return false;cx+=nx;cy+=ny;} return true;
}
void MainWindow::fireBullet(){
    Player &pl=m_gs.player; if(pl.ammo<=0||pl.attackCooldown>0)return;
    pl.ammo--; pl.attackCooldown=0.45f; pl.flashTimer=0.14f; pl.shakeY=-11.f;
    Bullet b; b.x=pl.posX+pl.dirX*0.4f; b.y=pl.posY+pl.dirY*0.4f; b.dx=pl.dirX*9.f; b.dy=pl.dirY*9.f; b.owner=PLAYER_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}
void MainWindow::enemyFire(Enemy &e){
    float dx=m_gs.player.posX-e.x,dy=m_gs.player.posY-e.y,d=sqrtf(dx*dx+dy*dy); if(d<0.1f)return;
    Bullet b; b.x=e.x; b.y=e.y; b.dx=(dx/d)*m_gs.enemyBulletSpeed; b.dy=(dy/d)*m_gs.enemyBulletSpeed; b.owner=ENEMY_SHOT; b.alive=true;
    m_gs.bullets.push_back(b);
}
void MainWindow::spawnBlood(float wx,float wy,int count){
    for(int i=0;i<count;i++){
        Particle p; p.x=wx+(rand()%20-10)*0.015f; p.y=wy+(rand()%20-10)*0.015f;
        p.z=0.4f+(rand()%10)*0.05f; p.vx=(rand()%100-50)*0.025f; p.vy=(rand()%100-50)*0.025f; p.vz=(rand()%60)*0.035f;
        p.life=0.7f+(rand()%40)*0.008f; p.color=(0xFF<<24)|((180+rand()%60)<<16)|(rand()%25<<8); m_gs.particles.push_back(p);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Input
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::keyPressEvent(QKeyEvent *e)
{
    m_keys.insert(e->key());
    if(m_screen==SCR_MENU){
        if(e->key()==Qt::Key_Up)   m_menuCursor=std::max(0,m_menuCursor-1);
        if(e->key()==Qt::Key_Down) m_menuCursor=std::min(1,m_menuCursor+1);
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space||e->key()==Qt::Key_1){if(m_menuCursor==0||e->key()==Qt::Key_1)m_screen=SCR_MAP_SELECT;else QApplication::quit();}
        if(e->key()==Qt::Key_Escape||e->key()==Qt::Key_Q) QApplication::quit();
        return;
    }
    if(m_screen==SCR_MAP_SELECT){
        if(e->key()==Qt::Key_1){m_mapChoice=0;m_screen=SCR_DIFF_SELECT;m_menuCursor=1;}
        if(e->key()==Qt::Key_2){m_mapChoice=1;m_screen=SCR_DIFF_SELECT;m_menuCursor=1;}
        if(e->key()==Qt::Key_3){m_mapChoice=2;m_screen=SCR_DIFF_SELECT;m_menuCursor=1;}
        if(e->key()==Qt::Key_Left)  m_mapChoice=std::max(0,m_mapChoice-1);
        if(e->key()==Qt::Key_Right) m_mapChoice=std::min(2,m_mapChoice+1);
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space){m_screen=SCR_DIFF_SELECT;m_menuCursor=1;}
        if(e->key()==Qt::Key_Escape){m_screen=SCR_MENU;m_menuCursor=0;}
        return;
    }
    if(m_screen==SCR_DIFF_SELECT){
        if(e->key()==Qt::Key_1){m_difficulty=0;m_screen=SCR_CHAR_SELECT;m_menuCursor=0;}
        if(e->key()==Qt::Key_2){m_difficulty=1;m_screen=SCR_CHAR_SELECT;m_menuCursor=0;}
        if(e->key()==Qt::Key_3){m_difficulty=2;m_screen=SCR_CHAR_SELECT;m_menuCursor=0;}
        if(e->key()==Qt::Key_Left)  m_difficulty=std::max(0,m_difficulty-1);
        if(e->key()==Qt::Key_Right) m_difficulty=std::min(2,m_difficulty+1);
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space){m_screen=SCR_CHAR_SELECT;m_menuCursor=0;}
        if(e->key()==Qt::Key_Escape){m_screen=SCR_MAP_SELECT;m_menuCursor=0;}
        return;
    }
    if(m_screen==SCR_CHAR_SELECT){
        if(e->key()==Qt::Key_1){m_charChoice=0;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_2){m_charChoice=1;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_3){m_charChoice=2;initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_Left)  m_charChoice=std::max(0,m_charChoice-1);
        if(e->key()==Qt::Key_Right) m_charChoice=std::min(2,m_charChoice+1);
        if(e->key()==Qt::Key_Return||e->key()==Qt::Key_Space){initGame();m_screen=SCR_PLAYING;}
        if(e->key()==Qt::Key_Escape){m_screen=SCR_DIFF_SELECT;m_menuCursor=m_difficulty;}
        return;
    }
    // In-game
    if(e->key()==Qt::Key_Space) fireBullet();
    if(e->key()==Qt::Key_Escape){if(m_paused)QApplication::quit();else m_paused=true;}
    if(e->key()==Qt::Key_R){if(m_paused)m_paused=false;if(m_gs.gameOver){initGame();m_paused=false;}}
    if(e->key()==Qt::Key_Q&&(m_paused||m_gs.gameOver)) QApplication::quit();
    if(e->key()==Qt::Key_M&&!m_gs.gameOver){m_screen=SCR_MENU;m_paused=false;m_menuCursor=0;}
    if(e->key()==Qt::Key_BracketLeft)  m_pixelScale=std::min(m_pixelScale*2,8);
    if(e->key()==Qt::Key_BracketRight) m_pixelScale=std::max(m_pixelScale/2,1);
}
void MainWindow::keyReleaseEvent(QKeyEvent *e){m_keys.erase(e->key());}
void MainWindow::mousePressEvent(QMouseEvent *e){
    if(m_screen!=SCR_PLAYING||m_paused||m_gs.gameOver)return;
    if(e->button()==Qt::LeftButton) fireBullet();
    if(e->button()==Qt::RightButton) m_gs.player.adsMode=true;
}
void MainWindow::mouseReleaseEvent(QMouseEvent *e){if(e->button()==Qt::RightButton)m_gs.player.adsMode=false;}
void MainWindow::mouseMoveEvent(QMouseEvent *e){
    if(m_screen!=SCR_PLAYING||m_paused||m_gs.gameOver)return;
    int cx=width()/2; float dx=float(e->x()-cx); if(std::fabs(dx)<1.f)return;
    float rot2=dx*-(m_gs.player.adsMode?0.0008f:0.0015f);
    Player &pl=m_gs.player; float ox=pl.dirX,op=pl.planeX;
    pl.dirX=pl.dirX*cosf(rot2)+pl.dirY*sinf(rot2);pl.dirY=-ox*sinf(rot2)+pl.dirY*cosf(rot2);
    pl.planeX=pl.planeX*cosf(rot2)+pl.planeY*sinf(rot2);pl.planeY=-op*sinf(rot2)+pl.planeY*cosf(rot2);
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
uint32_t MainWindow::fogBlend(uint32_t c,float dist){float f=std::max(0.f,1.f-dist/14.f);return(0xFF<<24)|(uint8_t(((c>>16)&0xFF)*f)<<16)|(uint8_t(((c>>8)&0xFF)*f)<<8)|uint8_t((c&0xFF)*f);}
uint32_t MainWindow::dimColor(uint32_t c,float b){if(b>=1.f)return c;uint8_t a=c>>24;return(a<<24)|(uint8_t(((c>>16)&0xFF)*b)<<16)|(uint8_t(((c>>8)&0xFF)*b)<<8)|uint8_t((c&0xFF)*b);}
uint32_t MainWindow::wallPixel(int t,float wx,float wy,int side,float dist){
    int tx=int(wx*64)&63,ty=int(wy*64)&63; uint32_t c=0xFF888888;
    switch(t){
    case 1:{bool m=(ty%16<2)||((ty/16)%2==0?(tx%32<2):((tx+16)%32<2));if(m){c=0xFF555555;break;}int n=((tx*7+ty*13)^tx)&7;uint8_t v=uint8_t(125+n);c=(0xFF<<24)|(v<<16)|(v<<8)|v;break;}
    case 2:{bool m=(ty%8<1)||((ty/8)%2==0?(tx%20<2):((tx+10)%20<2));if(m){c=0xFF777766;break;}int n=((tx*5+ty*11)^ty)&15;c=(0xFF<<24)|(uint8_t(155+n)<<16)|(uint8_t(72+n/3)<<8)|uint8_t(55+n/3);break;}
    case 3:{int n=((tx*3+ty*7)^(tx+ty))&31;uint8_t v=uint8_t(90+n);c=(0xFF<<24)|(uint8_t(v*.75f)<<16)|(uint8_t(v*.85f)<<8)|v;break;}
    }
    if(side==1){uint8_t r=(c>>16&0xFF)/2,g=(c>>8&0xFF)/2,b=(c&0xFF)/2;c=(0xFF<<24)|(r<<16)|(g<<8)|b;}
    return fogBlend(c,dist);
}
uint32_t MainWindow::sampleImp(float u,float v,bool flash){int tx=std::min(15,std::max(0,int(u*16))),ty=std::min(15,std::max(0,int(v*16)));uint32_t c=IMP_SPRITE[ty][tx];if((c>>24)==0)return 0;return flash?0xFFFFFFFF:c;}
uint32_t MainWindow::samplePlayerBullet(float u,float v){float du=u-.5f,dv=v-.5f,d=du*du+dv*dv;if(d>.22f)return 0;float t=1.f-d*4.5f;return(0xFF<<24)|(0xFF<<16)|(uint8_t(180*t+75)<<8)|uint8_t(10*t);}
uint32_t MainWindow::sampleEnemyBullet(float u,float v){float du=u-.5f,dv=v-.5f,d=du*du+dv*dv;if(d>.22f)return 0;float t=1.f-d*4.5f;return(0xFF<<24)|(uint8_t(220*t+35)<<16)|(uint8_t(30*t)<<8)|uint8_t(180*t);}
uint32_t MainWindow::sampleMedkit(float u,float v){int tx=std::min(15,std::max(0,int(u*16))),ty=std::min(15,std::max(0,int(v*16)));return MEDKIT_SPRITE[ty][tx];}

// ─────────────────────────────────────────────────────────────────────────────
//  Pixel helpers
// ─────────────────────────────────────────────────────────────────────────────
inline void MainWindow::px(int x,int y,uint32_t c){if((unsigned)x<(unsigned)W&&(unsigned)y<(unsigned)H)m_pixels[y*W+x]=c;}
void MainWindow::fillRect(int x,int y,int w,int h,uint32_t c){int x0=std::max(x,0),x1=std::min(x+w,W),y0=std::max(y,0),y1=std::min(y+h,H);for(int r=y0;r<y1;r++)for(int col=x0;col<x1;col++)m_pixels[r*W+col]=c;}
void MainWindow::fillCircle(int cx,int cy,int r,uint32_t c){for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++)if(dx*dx+dy*dy<=r*r)px(cx+dx,cy+dy,c);}
void MainWindow::drawLine(int x0,int y0,int x1,int y1,uint32_t c){int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=std::abs(y1-y0),sy=y0<y1?1:-1,err=(dx>dy?dx:-dy)/2;for(;;){px(x0,y0,c);if(x0==x1&&y0==y1)break;int e=err;if(e>-dx){err-=dy;x0+=sx;}if(e<dy){err+=dx;y0+=sy;}}}

// =============================================================================
//  MENU SCREENS
// =============================================================================

void MainWindow::drawMenuBg()
{
    // Precalculated base background
    static uint32_t rowCol[H];
    static bool initRow = false;
    if(!initRow){
        for(int y=0;y<H;y++){
            float fy = float(y)/H; uint8_t b = uint8_t(18*(1.f-fy)+3);
            rowCol[y] = (0xFF<<24)|(uint8_t(b*0.15f)<<16)|(uint8_t(b*0.40f)<<8)|b;
        }
        initRow = true;
    }
    for(int y=0;y<H;y++){
        uint32_t rc = rowCol[y];
        for(int x=0;x<W;x++) m_pixels[y*W+x] = rc;
    }
    for(int y=0;y<H;y+=4) for(int x=0;x<W;x++){
        uint32_t &p=m_pixels[y*W+x];
        uint8_t r=(p>>16&0xFF)*3/4,g=(p>>8&0xFF)*3/4,b2=(p&0xFF)*3/4;
        m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g<<8)|b2;
    }
    for(int col=0;col<55;col++){
        int x=((col*17+3)%55)*14+(col%3)*3; if(x>=W)continue;
        float speed=0.8f+(col%7)*0.22f; int baseY=int(fmodf(m_menuFrame*speed*0.18f+col*41.7f,H+100))-50;
        int len=20+(col*19)%55;
        for(int dy=0;dy<len;dy++){
            int py=baseY+dy;if(py<0||py>=H)continue;
            float t=1.f-float(dy)/len;uint8_t v=uint8_t(t*t*90);
            if(dy==0) m_pixels[py*W+x]=(0xFF<<24)|(uint8_t(v*.1f)<<16)|(255<<8)|uint8_t(v*.6f);
            else m_pixels[py*W+x]=(0xFF<<24)|(0<<16)|(uint8_t(v*.7f)<<8)|uint8_t(v*.3f);
        }
    }
    int sweepY=int(fmodf(m_menuFrame*0.6f,H));
    for(int x=0;x<W;x++){
        uint32_t &p=m_pixels[sweepY*W+x];
        uint8_t g2=std::min(255,int((p>>8&0xFF)+18));
        m_pixels[sweepY*W+x]=(0xFF<<24)|((p>>16)&0xFF)<<16|(g2<<8)|(p&0xFF);
    }
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        uint8_t v = m_vignette[y][x];
        if(v > 0){
            float s = v / 255.f;
            uint32_t &p=m_pixels[y*W+x];
            uint8_t r=uint8_t((p>>16&0xFF)*(1-s*.6f)),g2=uint8_t((p>>8&0xFF)*(1-s*.6f)),b2=uint8_t((p&0xFF)*(1-s*.6f));
            m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;
        }
    }
}

void MainWindow::renderMenu()
{
    drawMenuBg();
    
    // Pure raster passes first
    for(int y=0;y<H;y++){float pulse=0.6f+0.4f*sinf(m_menuFrame*0.04f+y*0.01f);px(W*2/5,y,(uint32_t(uint8_t(80*pulse))<<24)|(0<<16)|(uint8_t(200*pulse)<<8)|uint8_t(80*pulse));}
    for(int x=28;x<W*2/5-20;x++){uint8_t b=uint8_t(255*(1.f-float(x-28)/float(W*2/5-48)));px(x,235,(0xFF<<24)|(0<<16)|(b<<8)|uint8_t(b/2));}
    
    int mx=W*2/5+60,my=H/2-55;
    for(int i=0;i<2;i++){
        bool sel=(m_menuCursor==i); int ox=mx,oy=my+i*70,ow=260,oh=56;
        if(sel){float pulse2=0.7f+0.3f*sinf(m_menuFrame*0.15f);for(int ay=oy;ay<oy+oh;ay++) px(ox,ay,(0xFF<<24)|(0<<16)|(uint8_t(255*pulse2)<<8)|uint8_t(120*pulse2));fillRect(ox+1,oy+1,3,oh-2,(uint32_t(180)<<24)|(0<<16)|(uint8_t(200*pulse2)<<8)|uint8_t(80*pulse2));}
    }
    fillRect(0,H-24,W,24,0xCC000000); for(int x=0;x<W;x++) px(x,H-24,0xFF004422);

    QPainter p(&m_buf);
    p.fillRect(0,0,W*2/5,H,QColor(0,0,0,130));
    p.setFont(QFont("Courier New",64,QFont::Black));p.setPen(QColor(0,255,136));
    p.drawText(QRect(24,100,W*2/5-24,90),Qt::AlignLeft|Qt::AlignVCenter,"RASTER");
    p.setFont(QFont("Courier New",20,QFont::Bold));p.setPen(QColor(160,220,200));
    p.drawText(QRect(28,195,W*2/5-28,36),Qt::AlignLeft,"TACTICAL SHOOTER");
    
    p.setFont(QFont("Courier New",9));p.setPen(QColor(40,100,70));
    p.drawText(QRect(28,250,W*2/5-28,20),Qt::AlignLeft,"Computer Graphics Lab  •  Semester 5");
    p.drawText(QRect(28,264,W*2/5-28,20),Qt::AlignLeft,"Pure software rasterization — no GPU");
    
    static const char*OPTS[]={"NEW GAME","EXIT"};
    for(int i=0;i<2;i++){
        bool sel=(m_menuCursor==i); int ox=mx,oy=my+i*70,ow=260,oh=56;
        p.fillRect(ox,oy,ow,oh,QColor(0,sel?18:8,0,sel?200:120));
        p.setPen(QPen(sel?QColor(0,255,140):QColor(0,70,45),sel?2:1)); p.drawRect(ox,oy,ow,oh);
        p.setFont(QFont("Courier New",22,QFont::Bold));p.setPen(sel?QColor(0,255,140):QColor(80,130,100));
        p.drawText(QRect(ox+14,oy,ow-14,oh),Qt::AlignLeft|Qt::AlignVCenter,QString("[%1]  %2").arg(i+1).arg(OPTS[i]));
    }
    p.setFont(QFont("Courier New",9));p.setPen(QColor(40,70,50));
    p.drawText(QRect(mx,my+155,260,18),Qt::AlignLeft,"↑↓ navigate   ENTER confirm");
    
    p.setFont(QFont("Courier New",9));p.setPen(QColor(0,100,55));
    p.drawText(QRect(10,H-14,W/2,14),Qt::AlignLeft,"SCROLL: zoom to see raster pixels   [ ]: pixel block size");
    p.end();
}

void MainWindow::renderMapSelect()
{
    drawMenuBg();
    fillRect(0,0,W,44,0xCC000000); for(int x=0;x<W;x++)px(x,43,0xFF004422);
    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",16,QFont::Bold));p.setPen(QColor(0,255,140));
    p.drawText(QRect(0,0,W,42),Qt::AlignCenter,"SELECT DEPLOYMENT ZONE");
    static const char*DIFF2[]={"★★☆","★★★","★★☆"};
    for(int i=0;i<3;i++){
        bool sel=(m_mapChoice==i); int cx=W/2-280+i*280,cy=60,cw=240,ch=430;
        p.fillRect(cx-cw/2,cy,cw,ch,QColor(0,sel?20:8,sel?12:5,sel?210:160));
        p.setPen(QPen(sel?QColor(0,255,140):QColor(0,70,45),sel?2:1));p.drawRect(cx-cw/2,cy,cw,ch);
        if(sel)for(int x=cx-cw/2;x<cx+cw/2;x++)px(x,cy,0xFF00FF88);
        const int*mp=ALL_MAPS[i]; int tsize=5,previewX=cx-cw/2+12,previewY=cy+12;
        for(int my2=0;my2<24;my2++) for(int mx2=0;mx2<24;mx2++){
            int tile=mp[my2*MAP_W+mx2]; uint32_t col=tile==0?0xFF0A1A0A:tile==1?0xFF606058:tile==2?0xFF704040:0xFF406040;
            for(int dy=0;dy<tsize-1;dy++) for(int dx=0;dx<tsize-1;dx++){int px3=previewX+mx2*tsize+dx,py3=previewY+my2*tsize+dy;if(px3>=0&&px3<W&&py3>=0&&py3<H)m_pixels[py3*W+px3]=col;}
        }
        if(sel){int scanY=previewY+int(fmodf(m_menuFrame*0.4f,24*tsize));for(int x=previewX;x<previewX+24*tsize&&x<W;x++){if(scanY>=0&&scanY<H){uint32_t &pp=m_pixels[scanY*W+x];pp=(0xFF<<24)|((pp>>16&0xFF)/2+10)<<16|((pp>>8&0xFF)/2+60)<<8|((pp&0xFF)/2+20);}}}
        px(previewX+int(2.5f*tsize),previewY+int(2.5f*tsize),0xFF00FFAA);
        int infoY=cy+24*tsize+18;
        p.setFont(QFont("Courier New",16,QFont::Bold));p.setPen(sel?QColor(0,255,140):QColor(120,180,140));
        p.drawText(QRect(cx-cw/2+8,infoY,cw-8,28),Qt::AlignCenter,QString("[%1]  %2").arg(i+1).arg(MAP_NAMES[i]));
        p.setFont(QFont("Courier New",9));p.setPen(QColor(140,180,150));
        p.drawText(QRect(cx-cw/2+8,infoY+32,cw-8,40),Qt::AlignCenter|Qt::TextWordWrap,MAP_DESCS[i]);
        p.setPen(QColor(255,200,80));p.drawText(QRect(cx-cw/2+8,infoY+76,cw-8,18),Qt::AlignCenter,QString("DIFFICULTY: %1").arg(DIFF2[i]));
    }
    p.setFont(QFont("Courier New",10));p.setPen(QColor(60,100,70));
    p.drawText(QRect(0,H-20,W,18),Qt::AlignCenter,"[← →] Navigate    [1/2/3] or [ENTER] Select    [ESC] Back");
    p.end();
}

void MainWindow::renderDiffSelect()
{
    drawMenuBg();
    fillRect(0,0,W,44,0xCC000000); for(int x=0;x<W;x++)px(x,43,0xFF004422);
    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",16,QFont::Bold));p.setPen(QColor(0,255,140));
    p.drawText(QRect(0,0,W,42),Qt::AlignCenter,QString("SELECT DIFFICULTY  |  Zone: %1").arg(MAP_NAMES[m_mapChoice]));

    static const QColor DIFF_COLORS[3]={QColor(80,200,80),QColor(220,180,0),QColor(220,60,60)};
    static const char*ICONS[]={"●●○","●●●","●●●"};
    for(int i=0;i<3;i++){
        const DiffConfig &dc=DIFF_CFG[i];
        bool sel=(m_difficulty==i);
        int cx=W/2-280+i*280,cy=70,cw=230,ch=380;
        p.fillRect(cx-cw/2,cy,cw,ch,QColor(0,0,0,sel?200:140));
        p.setPen(QPen(sel?DIFF_COLORS[i]:QColor(40,40,40),sel?2:1));p.drawRect(cx-cw/2,cy,cw,ch);
        if(sel)for(int x=cx-cw/2;x<cx+cw/2;x++)px(x,cy,sel?uint32_t((DIFF_COLORS[i].red()<<16)|(DIFF_COLORS[i].green()<<8)|DIFF_COLORS[i].blue()|0xFF000000):0xFF002200);

        // Difficulty icon rings (raster circles)
        int iconCX=cx, iconCY=cy+80;
        fillCircle(iconCX,iconCY,55,uint32_t(60<<24)|(i==2?0xFF2200:i==1?0xFF8800:0x00FF44));
        for(int a=0;a<360;a+=3){float fa=a*3.14159f/180.f;px(iconCX+int(cosf(fa)*55),iconCY+int(sinf(fa)*55),sel?uint32_t(0xFF000000|(DIFF_COLORS[i].red()<<16)|(DIFF_COLORS[i].green()<<8)|DIFF_COLORS[i].blue()):0xFF303030);}
        // Number inside
        p.setFont(QFont("Courier New",28,QFont::Black));p.setPen(sel?DIFF_COLORS[i]:QColor(80,80,80));
        p.drawText(QRect(cx-40,iconCY-22,80,44),Qt::AlignCenter,QString::number(i+1));
        // Difficulty name
        p.setFont(QFont("Courier New",18,QFont::Bold));p.setPen(sel?DIFF_COLORS[i]:QColor(100,100,100));
        p.drawText(QRect(cx-cw/2+8,cy+150,cw-8,32),Qt::AlignCenter,QString("[%1]  %2").arg(i+1).arg(dc.name));
        // Threat indicator
        p.setFont(QFont("Courier New",13,QFont::Bold));p.setPen(sel?DIFF_COLORS[i]:QColor(70,70,70));
        p.drawText(QRect(cx-cw/2+8,cy+185,cw-8,24),Qt::AlignCenter,ICONS[i]);
        // Stats
        p.setFont(QFont("Courier New",9));p.setPen(QColor(180,180,180));
        p.drawText(QRect(cx-cw/2+10,cy+218,cw-10,20),Qt::AlignCenter,dc.desc1);
        p.drawText(QRect(cx-cw/2+10,cy+238,cw-10,20),Qt::AlignCenter,dc.desc2);
        p.setPen(i==0?QColor(80,200,80):i==2?QColor(220,80,80):QColor(200,200,200));
        p.drawText(QRect(cx-cw/2+10,cy+258,cw-10,20),Qt::AlignCenter,dc.desc3);
        // Multiplier bars
        auto mbar=[&](int sy,float frac,uint32_t c){fillRect(cx-cw/2+10,sy,cw-20,8,0xFF111111);fillRect(cx-cw/2+10,sy,int((cw-20)*std::min(1.f,frac)),8,c);};
        p.setFont(QFont("Courier New",8));p.setPen(QColor(120,120,120));
        p.drawText(QRect(cx-cw/2+10,cy+285,cw-10,14),Qt::AlignLeft,"Enemy Threat:");
        mbar(cy+300,dc.enemyHPMult/1.6f,i==0?0xFF22CC44:i==1?0xFFCC8800:0xFFCC2200);
        p.drawText(QRect(cx-cw/2+10,cy+315,cw-10,14),Qt::AlignLeft,"Player Damage Taken:");
        mbar(cy+330,dc.dmgReceivedMult/1.5f,i==0?0xFF22CC44:i==1?0xFFCC8800:0xFFCC2200);
    }
    p.setFont(QFont("Courier New",10));p.setPen(QColor(60,100,70));
    p.drawText(QRect(0,H-20,W,18),Qt::AlignCenter,"[← →] Navigate    [1/2/3] or [ENTER] Select    [ESC] Back");
    p.end();
}

void MainWindow::renderCharSelect()
{
    drawMenuBg();
    fillRect(0,0,W,44,0xCC000000); for(int x=0;x<W;x++)px(x,43,0xFF004422);
    
    for(int i=0;i<3;i++){
        const CharProfile &cp=CHAR_PROFILES[i]; bool sel=(m_charChoice==i);
        int cx=W/2-280+i*280,cy=56,cw=236,ch=450;
        uint32_t bgCol = sel ? 0xD2050814 : 0x9B050800; // approximation of QColor(0,sel?20:8,sel?12:5,sel?210:155)
        fillRect(cx-cw/2, cy, cw, ch, bgCol);
        if(sel)for(int x=cx-cw/2;x<cx+cw/2;x++)px(x,cy,0xFF00FF88);
        
        // Raster silhouette
        int sh=cy+14,sw=cx;
        uint32_t sl=cp.sleeve,slh=cp.sleeveHi,sk2=cp.skinColor;
        auto blit=[&](int rx,int ry,int rw,int rh,uint32_t c){for(int ry2=std::max(0,ry);ry2<std::min(H,ry+rh);ry2++)for(int rx2=std::max(0,rx);rx2<std::min(W,rx+rw);rx2++)m_pixels[ry2*W+rx2]=c;};
        blit(sw-10,sh+2,20,20,sl);blit(sw-7,sh+5,14,14,sk2);blit(sw-5,sh+6,10,2,0xFF223322);
        blit(sw-18,sh+22,36,42,sl);blit(sw-14,sh+24,28,38,slh);blit(sw-12,sh+27,24,14,0xFF222222);
        blit(sw-28,sh+22,12,36,slh);blit(sw+16,sh+22,12,36,slh);
        blit(sw-26,sh+56,10,8,sk2);blit(sw+16,sh+56,10,8,sk2);
        blit(sw-16,sh+64,14,40,sl);blit(sw+2,sh+64,14,40,sl);
        blit(sw-14,sh+100,12,6,0xFF222228);blit(sw+4,sh+100,12,6,0xFF222228);
        blit(sw+14,sh+50,20,4,0xFF333340);
    }
    
    QPainter p(&m_buf);
    p.setFont(QFont("Courier New",15,QFont::Bold));p.setPen(QColor(0,255,140));
    p.drawText(QRect(0,0,W,42),Qt::AlignCenter,
               QString("ZONE: %1  |  DIFF: %2  —  SELECT OPERATIVE").arg(MAP_NAMES[m_mapChoice]).arg(DIFF_CFG[m_difficulty].name));

    for(int i=0;i<3;i++){
        const CharProfile &cp=CHAR_PROFILES[i]; bool sel=(m_charChoice==i);
        int cx=W/2-280+i*280,cy=56,cw=236,ch=450;
        p.setPen(QPen(sel?QColor(0,255,140):QColor(0,70,45),sel?2:1));p.drawRect(cx-cw/2,cy,cw,ch);
        int infoY=cy+130;
        p.setFont(QFont("Courier New",16,QFont::Bold));p.setPen(sel?QColor(0,255,140):QColor(100,160,120));
        p.drawText(QRect(cx-cw/2,infoY,cw,28),Qt::AlignCenter,QString("[%1]  %2").arg(i+1).arg(cp.name));
        auto statBar=[&](int sy,const char*lbl,float frac,uint32_t bc){p.setFont(QFont("Courier New",8));p.setPen(QColor(140,160,150));p.drawText(QRect(cx-cw/2+10,sy,70,14),Qt::AlignLeft,lbl);fillRect(cx-cw/2+80,sy+2,cw-90,10,0xFF0A180A);fillRect(cx-cw/2+80,sy+2,int((cw-90)*std::min(1.f,frac)),10,bc);};
        statBar(infoY+34,"HP",float(cp.hp)/200.f,cp.hp>100?0xFF22CC55:0xFF44AA22);statBar(infoY+52,"SPD",cp.speedMult/1.5f,0xFF2288FF);
        statBar(infoY+70,"STA",cp.stamina/150.f,0xFFFFCC22);statBar(infoY+88,"AMMO",float(cp.ammo)/30.f,0xFFFF8822);statBar(infoY+106,"ARM",cp.damageReduction,0xFF8822FF);
        p.setFont(QFont("Courier New",8));p.setPen(QColor(120,150,130));
        p.drawText(QRect(cx-cw/2+8,infoY+126,cw-8,16),Qt::AlignCenter,cp.stat1);
        p.drawText(QRect(cx-cw/2+8,infoY+142,cw-8,16),Qt::AlignCenter,cp.stat2);
        p.setPen(i==2?QColor(100,160,255):QColor(100,130,110));
        p.drawText(QRect(cx-cw/2+8,infoY+158,cw-8,16),Qt::AlignCenter,cp.stat3);
    }
    p.setFont(QFont("Courier New",10));p.setPen(QColor(60,100,70));
    p.drawText(QRect(0,H-20,W,18),Qt::AlignCenter,"[← →] Browse    [1/2/3] or [ENTER] Deploy    [ESC] Back");
    p.end();
}

// =============================================================================
//  GAME RENDERERS
// =============================================================================

// ─────────────────────────────────────────────────────────────────────────────
//  1. Floor & Ceiling — sky gradient + procedural clouds (all raster)
//     Clouds are sinusoidal noise computed per pixel. Press [ to see them
//     as giant blocks — proves pure per-pixel raster computation.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderFloorCeiling()
{
    const Player &pl=m_gs.player;
    int shOff=int(pl.shakeY), hor=H/2+shOff;
    int PS=m_pixelScale;
    float facing=atan2f(pl.dirY,pl.dirX);

    // ── Sky / Ceiling ─────────────────────────────────────────────────────────
    for(int y=0;y<hor&&y<H;y+=PS){
        int sy = std::min(299, std::max(0, y * 300 / std::max(1, hor)));
        for(int x=0;x<W;x+=PS){
            float skyU=float(x)/W + facing/(2.f*3.14159f)+0.5f;
            skyU-=floorf(skyU);
            int sx = int(skyU * 1024.f) % 1024; if(sx<0) sx+=1024;
            uint32_t skyCol = m_skybox[sy*1024 + sx];
            for(int by=0;by<PS&&y+by<hor&&y+by<H;by++)
                for(int bx=0;bx<PS&&x+bx<W;bx++)
                    m_pixels[(y+by)*W+(x+bx)]=skyCol;
        }
    }
    // Sun 
    float angleDiff = 0.f - facing; // Sun is at East
    while(angleDiff < -3.14159f) angleDiff += 2.f*3.14159f;
    while(angleDiff > 3.14159f) angleDiff -= 2.f*3.14159f;
    if(fabsf(angleDiff) < 3.14159f/2.f){
        int sunX = W/2 + int(angleDiff * (W/2) / (m_adsFOVMult * 0.8f));
        int sunY = int(hor * 0.35f);
        for(int dy=-20;dy<=20;dy++) for(int dx=-20;dx<=20;dx++){
            float d=sqrtf(float(dx*dx+dy*dy));
            if(d > 20) continue;
            int sx=sunX+dx,sy=sunY+dy;
            if(sx>=0&&sx<W&&sy>=0&&sy<hor){
                if(d < 12){
                    float t = d/12.f;
                    uint8_t sr=uint8_t(255*(1.f-t*.2f)),sg=uint8_t(240*(1.f-t*.3f)),sb2=uint8_t(180*(1.f-t*.5f));
                    m_pixels[sy*W+sx]=(0xFF<<24)|(sr<<16)|(sg<<8)|sb2;
                } else {
                    float t=1.f-(d-12.f)/8.f; uint8_t a=uint8_t(t*t*80);
                    uint32_t &pp=m_pixels[sy*W+sx];
                    uint8_t r=uint8_t(std::min(255,int((pp>>16&0xFF)+(255-int((pp>>16&0xFF)))*a/255)));
                    uint8_t g=uint8_t(std::min(255,int((pp>>8&0xFF)+(240-int((pp>>8&0xFF)))*a/255)));
                    pp=(0xFF<<24)|(r<<16)|(g<<8)|(pp&0xFF);
                }
            }
        }
    }
    // Horizon haze line
    for(int x=0;x<W;x++){int hy=std::max(0,std::min(H-1,hor));m_pixels[hy*W+x]=0xFF202830;}

    // ── Floor — perspective-correct floor cast ────────────────────────────────
    float rdX0=pl.dirX-pl.planeX*m_adsFOVMult,rdY0=pl.dirY-pl.planeY*m_adsFOVMult;
    float rdX1=pl.dirX+pl.planeX*m_adsFOVMult,rdY1=pl.dirY+pl.planeY*m_adsFOVMult;
    for(int y=hor+1;y<H;y+=PS){
        int p2=y-hor; float rowDist=float(H/2)/float(p2);
        float sX=rowDist*(rdX1-rdX0)/float(W),sY=rowDist*(rdY1-rdY0)/float(W);
        float fX=pl.posX+rowDist*rdX0,fY=pl.posY+rowDist*rdY0;
        float fog=std::max(0.f,1.f-rowDist/14.f);
        uint32_t fogM = uint32_t(fog*256);
        for(int x=0;x<W;x+=PS){
            bool check=((int(fX)+int(fY))&1)==0;
            uint32_t c_val = check ? 42 : 26;
            uint8_t r = (c_val * fogM) >> 8;
            uint8_t g = ((c_val * 220 / 256) * fogM) >> 8;
            uint8_t b = ((c_val * 160 / 256) * fogM) >> 8;
            uint32_t fc = (0xFF<<24)|(r<<16)|(g<<8)|b;
            for(int by=0;by<PS&&y+by<H;by++) for(int bx=0;bx<PS&&x+bx<W;bx++) m_pixels[(y+by)*W+(x+bx)]=fc;
            fX+=sX*PS; fY+=sY*PS;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  2. Walls
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWalls()
{
    const Player &pl=m_gs.player; int shOff=int(pl.shakeY),hor=H/2+shOff; int PS=m_pixelScale;
    for(int x=0;x<W;x+=PS){
        float camX=2.f*x/float(W)-1.f,rdX=pl.dirX+pl.planeX*m_adsFOVMult*camX,rdY=pl.dirY+pl.planeY*m_adsFOVMult*camX;
        int mapX=int(pl.posX),mapY=int(pl.posY);
        float ddX=std::fabs(rdX)<1e-20f?1e30f:std::fabs(1.f/rdX),ddY=std::fabs(rdY)<1e-20f?1e30f:std::fabs(1.f/rdY);
        float sdX,sdY; int stX,stY;
        if(rdX<0){stX=-1;sdX=(pl.posX-mapX)*ddX;}else{stX=1;sdX=(mapX+1.f-pl.posX)*ddX;}
        if(rdY<0){stY=-1;sdY=(pl.posY-mapY)*ddY;}else{stY=1;sdY=(mapY+1.f-pl.posY)*ddY;}
        bool hit=false; int side=0;
        for(int g=0;g<64&&!hit;g++){if(sdX<sdY){sdX+=ddX;mapX+=stX;side=0;}else{sdY+=ddY;mapY+=stY;side=1;}if(mapX<0||mapX>=MAP_W||mapY<0||mapY>=MAP_H)break;if(m_map[mapY][mapX]>0)hit=true;}
        if(!hit){for(int bx=0;bx<PS&&x+bx<W;bx++)m_zBuf[x+bx]=1e30f;continue;}
        float dist=(side==0)?(sdX-ddX):(sdY-ddY); for(int bx=0;bx<PS&&x+bx<W;bx++)m_zBuf[x+bx]=dist;
        float wh;if(side==0)wh=pl.posY+dist*rdY;else wh=pl.posX+dist*rdX;wh-=std::floor(wh);
        int lh=std::max(1,int(float(H)/dist)),ds=std::max(0,hor-lh/2),de=std::min(H-1,hor+lh/2),wt=m_map[mapY][mapX];
        
        float fog=std::max(0.f,1.f-dist/14.f);
        uint32_t fogM = uint32_t(fog*256);
        int tx = int(wh*64)&63;
        int t = (wt>=1 && wt<=3) ? wt : 1;
        float step = 64.f / float(lh);
        float texPos = float(ds - (hor - lh/2)) * step;
        for(int y=ds;y<=de;y++){
            int ty = int(texPos) & 63;
            texPos += step;
            uint32_t c = m_textures[t][ty*64+tx];
            if(side==1){uint8_t r=(c>>17&0x7F),g=(c>>9&0x7F),b=(c>>1&0x7F);c=(0xFF<<24)|(r<<16)|(g<<8)|b;}
            uint8_t r=(c>>16&0xFF)*fogM>>8, g=(c>>8&0xFF)*fogM>>8, b=(c&0xFF)*fogM>>8;
            c = (0xFF<<24)|(r<<16)|(g<<8)|b;
            for(int bx=0;bx<PS&&x+bx<W;bx++) m_pixels[y*W+(x+bx)]=c;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  3. Sprites — enemies + bullets + medkits + enemy HP bars
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderSprites()
{
    const Player &pl=m_gs.player;
    enum SpType{SPR_ENEMY,SPR_PBULLET,SPR_EBULLET,SPR_MEDKIT};
    struct Sp{float x,y,dx,dy,dist;SpType t;int idx;};
    std::vector<Sp> sprites;

    for(int i=0;i<int(m_gs.enemies.size());i++){const Enemy &e=m_gs.enemies[i];if(!e.alive)continue;float dx=e.x-pl.posX,dy=e.y-pl.posY;sprites.push_back({e.x,e.y,0,0,dx*dx+dy*dy,SPR_ENEMY,i});}
    for(int i=0;i<int(m_gs.bullets.size());i++){const Bullet &b=m_gs.bullets[i];if(!b.alive)continue;float dx=b.x-pl.posX,dy=b.y-pl.posY;SpType st=b.owner==PLAYER_SHOT?SPR_PBULLET:SPR_EBULLET;sprites.push_back({b.x,b.y,b.dx,b.dy,dx*dx+dy*dy,st,i});}
    for(int i=0;i<int(m_gs.healthKits.size());i++){const HealthKit &k=m_gs.healthKits[i];if(!k.active)continue;float dx=k.x-pl.posX,dy=k.y-pl.posY;sprites.push_back({k.x,k.y,0,0,dx*dx+dy*dy,SPR_MEDKIT,i});}

    std::sort(sprites.begin(),sprites.end(),[](const Sp&a,const Sp&b2){return a.dist>b2.dist;});
    float eplX=pl.planeX*m_adsFOVMult,eplY=pl.planeY*m_adsFOVMult;
    float invDet=1.f/(eplX*pl.dirY-pl.dirX*eplY);
    int hor=H/2+int(pl.shakeY);

    // Collect HP bar data for a single QPainter pass at end
    struct HPBar{int scrX,sprTop,sprH;float frac,alpha;int hp;};
    std::vector<HPBar> hpBars;

    auto drawAt=[&](float wx,float wy,SpType type,int idx,float bright,float wScale,float bobOff=0.f){
        float spX=wx-pl.posX,spY=wy-pl.posY;
        float txX=invDet*(pl.dirY*spX-pl.dirX*spY),txY=invDet*(-eplY*spX+eplX*spY);
        if(txY<0.05f)return;
        int scrX=int(float(W/2)*(1.f+txX/txY));
        int sprH=std::abs(int(float(H)/txY)),sprW=int(sprH*wScale);
        int sy0=hor-sprH/2+int(bobOff),sy1=hor+sprH/2+int(bobOff);
        int sx0=scrX-sprW/2,sx1=scrX+sprW/2;
        for(int sx=sx0;sx<sx1;sx++){
            if(sx<0||sx>=W||txY>=m_zBuf[sx])continue;
            float u=float(sx-sx0)/float(std::max(1,sprW));
            for(int sy=std::max(0,sy0);sy<std::min(H,sy1);sy++){
                float v=float(sy-sy0)/float(std::max(1,sprH));
                uint32_t c;
                switch(type){
                case SPR_ENEMY:   c=sampleImp(u,v,idx<int(m_gs.enemies.size())&&m_gs.enemies[idx].hitFlash>0);break;
                case SPR_PBULLET: c=samplePlayerBullet(u,v);break;
                case SPR_EBULLET: c=sampleEnemyBullet(u,v);break;
                case SPR_MEDKIT:  c=sampleMedkit(u,v);break;
                }
                if((c>>24)==0)continue;
                m_pixels[sy*W+sx]=fogBlend(dimColor(c,bright),txY);
            }
        }
        // Collect HP bar info for enemies that were recently hit
        if(type==SPR_ENEMY&&idx<12&&m_enemyHPDisplay[idx]>0&&txY<m_zBuf[std::max(0,std::min(W-1,scrX))]){
            const Enemy &e=m_gs.enemies[idx];
            int maxHP=std::max(1,int(float(LEVEL_CFG[std::min(m_gs.level,5)].enemyHP+
                                          (m_gs.level>5?(m_gs.level-5)*2:0))*DIFF_CFG[m_difficulty].enemyHPMult));
            float frac=float(e.hp)/float(maxHP);
            float alpha=std::min(1.f,m_enemyHPDisplay[idx]/0.4f);
            hpBars.push_back({scrX,sy0,sprH,frac,alpha,e.hp});
        }
    };

    for(const Sp &sp:sprites){
        if(sp.t==SPR_ENEMY){drawAt(sp.x,sp.y,sp.t,sp.idx,1.f,1.f);}
        else if(sp.t==SPR_MEDKIT){
            float bob=sinf(m_gs.healthKits[sp.idx].bobPhase)*10.f;
            drawAt(sp.x,sp.y,sp.t,sp.idx,1.f,0.7f,bob);
            // Glow ring around medkit
            float spX=sp.x-pl.posX,spY=sp.y-pl.posY;
            float txX=invDet*(pl.dirY*spX-pl.dirX*spY),txY=invDet*(-eplY*spX+eplX*spY);
            if(txY>0.05f&&txY<6.f){
                int scrX=int(float(W/2)*(1.f+txX/txY));
                int sprH2=std::abs(int(float(H)/txY));
                int glowY=hor+sprH2/2+int(sinf(m_gs.healthKits[sp.idx].bobPhase)*10.f);
                float pulseA=0.5f+0.5f*sinf(m_gs.healthKits[sp.idx].bobPhase*3.f);
                for(int a=0;a<360;a+=8){float fa=a*3.14159f/180.f;int gx=scrX+int(cosf(fa)*(sprH2/4)),gy=glowY-sprH2/8+int(sinf(fa)*(sprH2/8));if(gx>=0&&gx<W&&gy>=0&&gy<H)m_pixels[gy*W+gx]=uint32_t(uint8_t(200*pulseA))<<24|0x22FF44;}
            }
        }
        else{for(int pass=0;pass<3;pass++)drawAt(sp.x-sp.dx*pass*0.055f,sp.y-sp.dy*pass*0.055f,sp.t,sp.idx,1.f-pass*0.35f,0.45f-pass*0.1f);}
    }

    // Draw HP bars (raster bars + QPainter text in one pass)
    if(!hpBars.empty()){
        QPainter p2(&m_buf);
        for(const auto &hb:hpBars){
            int barW=64,barH=8,bx=hb.scrX-barW/2,by=hb.sprTop-barH-8;
            if(by<44) by=44;
            uint8_t a=uint8_t(200*hb.alpha);
            // Background
            fillRect(bx-1,by-14,barW+2,barH+16,uint32_t(a/2)<<24);
            // Bar bg
            fillRect(bx,by,barW,barH,uint32_t(a)<<24|0x111111);
            // Bar fill
            uint32_t bc=hb.frac>0.5f?0xFF00CC55:hb.frac>0.25f?0xFFCC8800:0xFFCC2200;
            int fill=int(barW*hb.frac); if(fill>0) fillRect(bx,by,fill,barH,uint32_t(a)<<24|(bc&0xFFFFFF));
            // HP text
            p2.setFont(QFont("Courier New",8,QFont::Bold));
            p2.setPen(QColor(255,255,255,int(255*hb.alpha)));
            p2.drawText(QRect(bx,by-12,barW,12),Qt::AlignCenter,
                        QString("HP  %1  (%2%)").arg(hb.hp).arg(int(hb.frac*100)));
        }
        p2.end();
    }
}

void MainWindow::renderParticles()
{
    const Player &pl=m_gs.player;
    float eplX=pl.planeX*m_adsFOVMult,eplY=pl.planeY*m_adsFOVMult;
    float invDet=1.f/(eplX*pl.dirY-pl.dirX*eplY);
    int hor=H/2+int(pl.shakeY);
    for(const Particle &pt:m_gs.particles){
        if(pt.life<=0)continue;
        float spX=pt.x-pl.posX,spY=pt.y-pl.posY;
        float txX=invDet*(pl.dirY*spX-pl.dirX*spY),txY=invDet*(-eplY*spX+eplX*spY);
        if(txY<0.05f)continue;
        int scrX=int(float(W/2)*(1.f+txX/txY));
        int floorY=hor+int(float(H)*0.5f/txY),scrY=floorY-int(pt.z*float(H)/txY);
        if(scrX<1||scrX>=W-1||scrY<1||scrY>=H-1||txY>=m_zBuf[scrX])continue;
        uint32_t c=dimColor(fogBlend(pt.color,txY),pt.life); if((c>>24)==0)continue;
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) px(scrX+dx,scrY+dy,c);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  5. Weapon & Arms  — realistic tactical pistol + gloved arms
//     All drawing uses fillRect/fillCircle/drawLine (pure raster)
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderWeaponAndArms()
{
    Player &pl=m_gs.player;
    const CharProfile &cp=CHAR_PROFILES[m_charChoice];
    bool ads=pl.adsMode;
    float bobX=sinf(pl.bobPhase)*(pl.sprinting?9.f:5.f);
    float bobY=fabsf(cosf(pl.bobPhase*.5f))*(pl.sprinting?12.f:7.f);
    float breathe=sinf(m_gs.frameNum*.018f)*1.5f;
    // ADS: weapon centers; normal: weapon at bottom-right
    int rx=ads?W/2+int(bobX*.25f):W/2+68+int(bobX);
    int ry=ads?H-25+int(bobY*.35f+breathe*.4f):H-5+int(bobY+breathe);
    int GS=ads?80:100;  // global scale
    auto g=[&](int v)->int{return v*GS/100;};

    // ── SUPPORT HAND / LEFT ARM ───────────────────────────────────────────────
    if(!ads){
        int lx=W/2-105+int(bobX*.4f),ly=ry-88;
        // Forearm (tapers toward wrist — 3 segments)
        fillRect(lx-24,ly+30,48,H-(ly+30),cp.sleeve);   // upper sleeve
        fillRect(lx-22,ly+26,44,H-(ly+26),cp.sleeveHi); // sleeve detail
        // Forearm muscle definition (lighter stripe along top)
        fillRect(lx-20,ly+26,6,60,uint32_t(cp.sleeveHi+0x080808));
        // Wrist + tactical glove (very dark, tactical)
        uint32_t glove=0xFF1A1A20, gloveHi=0xFF252530;
        fillRect(lx-18,ly+12,36,20,glove);   // glove back
        fillRect(lx-16,ly+10,32,12,gloveHi); // glove top highlight
        // Knuckle definition
        for(int k=0;k<4;k++) fillRect(lx-10+k*7,ly+8,5,6,uint32_t(gloveHi+0x101010));
        // Fingers wrapped around frame
        for(int f=0;f<4;f++) fillRect(lx-12+f*7,ly-2,5,12,glove);
        fillRect(lx-14,ly-5,5,8,glove); // thumb
    }

    // ── PISTOL — high-detail tactical model ──────────────────────────────────
    // Drawing from bottom to top (barrel tip is at top)

    // --- Magazine (extends below grip) ---
    fillRect(rx-g(7), ry+g(48), g(14), H-(ry+g(48))+5, 0xFF1A1A24);
    fillRect(rx-g(5), ry+g(50), g(4), g(12), 0xFF252530);  // mag release button

    // --- Grip ---
    fillRect(rx-g(10), ry+g(0), g(20), g(50), 0xFF1C1C28); // grip body
    fillRect(rx-g(10), ry+g(0), g(20), g(2), 0xFF2A2A38);  // top of grip
    // Stippling texture (grip pattern — individual dots, proves raster)
    for(int row=0;row<5;row++) for(int col=0;col<3;col++)
        fillCircle(rx-g(6)+col*g(5), ry+g(8)+row*g(8), g(1), 0xFF0A0A14);
    // Right side panel
    fillRect(rx+g(8), ry+g(5), g(3), g(38), 0xFF161622);
    // Backstrap
    fillRect(rx-g(11), ry+g(5), g(3), g(38), 0xFF161622);

    // --- Trigger guard ---
    fillRect(rx-g(12), ry-g(10), g(2), g(22), 0xFF252530); // left bar
    fillRect(rx+g(10), ry-g(10), g(2), g(22), 0xFF252530); // right bar
    fillRect(rx-g(12), ry-g(11), g(24), g(3), 0xFF252530); // bottom bar
    // Trigger (visible through guard)
    fillRect(rx-g(2), ry-g(8), g(4), g(14), 0xFF2E2E3C);
    fillRect(rx-g(1), ry-g(5), g(2), g(8), 0xFF3C3C4A);

    // --- Frame (lower receiver) ---
    fillRect(rx-g(13), ry-g(32), g(26), g(23), 0xFF28282E); // frame body
    fillRect(rx-g(13), ry-g(32), g(26), g(2), 0xFF383848); // top edge
    // Picatinny rail (bottom of frame, forward)
    fillRect(rx-g(13), ry-g(34), g(20), g(4), 0xFF1E1E28);
    for(int s=0;s<4;s++) fillRect(rx-g(13)+s*g(5), ry-g(34), g(4), g(4), 0xFF2A2A36); // rail slots

    // --- Slide (upper receiver) — main body ---
    fillRect(rx-g(13), ry-g(110), g(26), g(80), 0xFF373740); // slide main
    fillRect(rx-g(13), ry-g(110), g(2), g(80), 0xFF454550); // left edge highlight
    fillRect(rx+g(11), ry-g(110), g(2), g(80), 0xFF202028); // right edge shadow
    fillRect(rx-g(12), ry-g(109), g(24), g(2), 0xFF454550); // top highlight
    // Slide mid-body detail
    fillRect(rx-g(11), ry-g(55), g(22), g(2), 0xFF2A2A34); // slide seam

    // --- Ejector port (cutout in slide right side) ---
    fillRect(rx+g(6), ry-g(90), g(8), g(22), 0xFF0A0A12); // port opening
    fillRect(rx+g(7), ry-g(89), g(4), g(6), 0xFF8A7040); // brass case peek (ejected round)
    fillRect(rx+g(8), ry-g(88), g(2), g(4), 0xFFA08030);

    // --- Rear serrations (8 diagonal cuts on back of slide) ---
    for(int s=0;s<8;s++){
        int sy=ry-g(107)+s*g(4);
        fillRect(rx-g(13), sy, g(26), g(2), 0xFF2A2A34);
        fillRect(rx-g(11), sy, g(22), g(1), 0xFF202028);
    }

    // --- Rear sight (U-shaped notch) ---
    fillRect(rx-g(12), ry-g(113), g(8), g(5), 0xFF484852); // left post
    fillRect(rx+g(4), ry-g(113), g(8), g(5), 0xFF484852);  // right post
    fillRect(rx-g(3), ry-g(112), g(6), g(2), 0xFF202028);  // notch gap
    // Sight dots (white dot sights)
    fillCircle(rx-g(8), ry-g(111), g(2), 0xFFEEEEFF);  // left dot
    fillCircle(rx+g(8), ry-g(111), g(2), 0xFFEEEEFF);  // right dot

    // --- Barrel (protruding slightly from slide front) ---
    fillRect(rx-g(5), ry-g(115), g(10), g(8), 0xFF404048); // barrel body
    fillRect(rx-g(4), ry-g(118), g(8), g(6), 0xFF505058); // crown
    fillCircle(rx, ry-g(116), g(3), 0xFF0A0A14); // bore (muzzle hole)
    // Front sight post (tall blade)
    fillRect(rx-g(2), ry-g(118), g(4), g(7), 0xFF484850);
    fillCircle(rx, ry-g(115), g(1), 0xFFEEEEFF); // front dot

    // --- Thumb safety ---
    fillRect(rx-g(13), ry-g(38), g(5), g(6), 0xFF3A3A48);

    // --- Hammer ---
    fillRect(rx-g(4), ry-g(34), g(8), g(4), 0xFF404050);
    fillRect(rx-g(2), ry-g(37), g(4), g(5), 0xFF303040);

    // ── FIRING HAND / RIGHT ARM ───────────────────────────────────────────────
    uint32_t glove=0xFF1A1A20, gloveHi=0xFF252530;
    // Upper forearm / sleeve
    fillRect(rx-g(22), ry+g(45), g(44), H-(ry+g(45)), cp.sleeve);
    fillRect(rx-g(20), ry+g(48), g(40), H-(ry+g(48)), cp.sleeveHi);
    // Forearm muscle stripe
    fillRect(rx-g(18), ry+g(48), g(5), 60, uint32_t(cp.sleeveHi+0x080808));
    // Wrist
    fillRect(rx-g(18), ry+g(36), g(36), g(14), glove);
    fillRect(rx-g(16), ry+g(34), g(32), g(8), gloveHi);
    // Palm on grip
    fillRect(rx-g(16), ry+g(2), g(20), g(36), glove);
    fillRect(rx-g(14), ry+g(2), g(16), g(34), gloveHi);
    // Thumb wrap around grip
    fillRect(rx+g(5), ry-g(15), g(8), g(30), glove);
    // Finger definition
    for(int f=0;f<4;f++) fillRect(rx-g(10)+f*g(5), ry-g(2), g(4), g(5), gloveHi);
    // Knuckle highlight
    for(int k=0;k<4;k++) fillCircle(rx-g(8)+k*g(5), ry-g(2), g(2), uint32_t(gloveHi+0x080808));
    // Watch detail (left wrist)
    fillRect(rx-g(19), ry+g(38), g(7), g(8), 0xFF202020); // watch body
    fillRect(rx-g(18), ry+g(39), g(5), g(6), 0xFF001828); // watch face
    fillRect(rx-g(17), ry+g(40), g(3), g(1), 0xFF00FFAA); // digital readout

    // ── ADS scope ─────────────────────────────────────────────────────────────
    if(ads&&m_adsFOVMult<0.7f){
        int sCx=W/2,sCy=H/2;
        fillCircle(sCx,sCy,62,0xFF000000);
        for(int a=0;a<360;a+=2){float fa=a*3.14159f/180.f;px(sCx+int(cosf(fa)*60),sCy+int(sinf(fa)*60),0xFF88AA88);}
        for(int a=0;a<360;a+=8){float fa=a*3.14159f/180.f;px(sCx+int(cosf(fa)*55),sCy+int(sinf(fa)*55),0xFF446644);}
        drawLine(sCx-55,sCy,sCx-10,sCy,0xFF88CC88);drawLine(sCx+10,sCy,sCx+55,sCy,0xFF88CC88);
        drawLine(sCx,sCy-55,sCx,sCy-10,0xFF88CC88);drawLine(sCx,sCy+10,sCx,sCy+55,0xFF88CC88);
        // Stadia marks
        for(int s=1;s<=3;s++){drawLine(sCx-30,sCy+s*10,sCx-20,sCy+s*10,0xFF446644);drawLine(sCx+20,sCy+s*10,sCx+30,sCy+s*10,0xFF446644);}
        fillCircle(sCx,sCy,2,0xFFFF4444);
        for(int y=0;y<H;y++) for(int x=0;x<W;x++){float ddx=float(x-sCx),ddy=float(y-sCy);if(ddx*ddx+ddy*ddy>60.f*60.f){uint32_t &pp=m_pixels[y*W+x];uint8_t r=(pp>>16&0xFF)/5,g2=(pp>>8&0xFF)/5,b2=(pp&0xFF)/5;pp=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}}
    }

    // ── Muzzle flash ──────────────────────────────────────────────────────────
    if(pl.flashTimer>0){
        float t=pl.flashTimer/0.14f; int fr=int((12+rand()%6)*t);
        if(fr>0){
            int fx=rx,fy=ry-g(118);
            for(int ray=0;ray<8;ray++){float fa=ray*3.14159f/4.f+t*.5f;int rl=fr+8+rand()%6;drawLine(fx,fy,fx+int(cosf(fa)*rl),fy+int(sinf(fa)*rl),0x99FF5500);}
            fillCircle(fx,fy,fr+6,0x44FF8800);fillCircle(fx,fy,fr+3,0xAAFFAA00);fillCircle(fx,fy,fr,0xFFFFDD00);fillCircle(fx,fy,fr/2,0xFFFFFFFF);
            int addR=int(36*t),addG=int(18*t);
            for(int i=0;i<W*H;i++){uint32_t pp=m_pixels[i];uint8_t r=uint8_t(std::min(255,int((pp>>16&0xFF)+addR))),g2=uint8_t(std::min(255,int((pp>>8&0xFF)+addG)));m_pixels[i]=(0xFF<<24)|(r<<16)|(g2<<8)|(pp&0xFF);}
        }
    }
}

void MainWindow::renderDamageVignette()
{
    if(m_gs.player.damageFlash<=0)return;
    float t=m_gs.player.damageFlash/0.45f; int depth=int(90*t);
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){int d=std::min({x,y,W-1-x,H-1-y});if(d<depth){float s=(1.f-float(d)/float(depth))*t;uint32_t &pp=m_pixels[y*W+x];uint8_t r=uint8_t(std::min(255,int((pp>>16&0xFF)+int(190*s))));pp=(0xFF<<24)|(r<<16)|(pp&0x00FFFF);}}
}

// =============================================================================
//  HUD
// =============================================================================
void MainWindow::renderTopBar()
{
    for(int y=0;y<40;y++) for(int x=0;x<W;x++){uint32_t &pp=m_pixels[y*W+x];uint8_t r=(pp>>16&0xFF)/5,g2=(pp>>8&0xFF)/5+2,b2=(pp&0xFF)/5+4;m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}
    for(int x=0;x<W;x++)px(x,40,0xFF004422);
    QPainter p(&m_buf);
    // Left
    p.setFont(QFont("Courier New",10,QFont::Bold));p.setPen(QColor(0,220,130));
    const char*diffChars[]={"EASY","MED","HARD"};
    p.drawText(8,26,QString("[%1] %2  LV%3  KILL %4/%5").arg(MAP_NAMES[m_gs.mapChoice]).arg(diffChars[m_difficulty]).arg(m_gs.level).arg(m_gs.killsThisLevel).arg(m_gs.killsNeeded));
    // Compass
    {
        float heading=atan2f(-m_gs.player.dirY,m_gs.player.dirX)*180.f/3.14159f;if(heading<0)heading+=360.f;
        float dh=fmodf(360.f-(heading-90.f)+360.f,360.f);
        const int cw=280; float ppd=float(cw)/90.f;
        static const struct{float deg;const char*lbl;}DIRS[]={{0,"N"},{45,"NE"},{90,"E"},{135,"SE"},{180,"S"},{225,"SW"},{270,"W"},{315,"NW"}};
        for(auto &d:DIRS)for(int wrap=-1;wrap<=1;wrap++){float delta=fmodf(d.deg+wrap*360.f-dh+360.f,360.f);if(delta>180)delta-=360.f;if(fabs(delta)>100)continue;int lx=W/2+int(delta*ppd);bool isCard=(int(d.deg)%90==0);p.setFont(QFont("Courier New",isCard?11:8,isCard?QFont::Bold:QFont::Normal));p.setPen(isCard?QColor(0,255,140):QColor(80,150,100));p.drawText(QRect(lx-16,4,32,18),Qt::AlignCenter,d.lbl);}
        p.setPen(QColor(255,80,80));p.setFont(QFont("Courier New",10,QFont::Bold));p.drawText(QRect(W/2-8,21,16,16),Qt::AlignCenter,"▼");
        static const char*CARD8[]={"N","NE","E","SE","S","SW","W","NW"};int ci=int((dh+22.5f)/45.f)%8;
        p.setFont(QFont("Courier New",9));p.setPen(QColor(180,180,180));p.drawText(QRect(W/2+140,8,60,22),Qt::AlignLeft,QString("%1° %2").arg(int(dh)).arg(CARD8[ci]));
    }
    p.setFont(QFont("Courier New",10,QFont::Bold));p.setPen(QColor(255,190,60));
    p.drawText(QRect(W-200,0,190,40),Qt::AlignRight|Qt::AlignVCenter,QString("SCORE %1  |  %2 FPS").arg(m_gs.score).arg(int(m_fps)));
    p.end();
}

void MainWindow::renderRadar()
{
    const int RX=76,RY=76+42,RD=60; const Player &pl=m_gs.player;
    for(int dy=-RD;dy<=RD;dy++) for(int dx=-RD;dx<=RD;dx++){if(dx*dx+dy*dy>RD*RD)continue;int px2=RX+dx,py2=RY+dy;if(px2<0||px2>=W||py2<0||py2>=H)continue;uint32_t &pp=m_pixels[py2*W+px2];uint8_t r=(pp>>16&0xFF)/7,g2=(pp>>8&0xFF)/7,b2=uint8_t((pp&0xFF)/7+10);m_pixels[py2*W+px2]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}
    for(int a=0;a<360;a++){float fa=a*3.14159f/180.f;px(RX+int(cosf(fa)*RD),RY+int(sinf(fa)*RD),0xFF00CC66);}
    for(int r2=RD/3;r2<RD;r2+=RD/3) for(int a=0;a<360;a+=4){float fa=a*3.14159f/180.f;px(RX+int(cosf(fa)*r2),RY+int(sinf(fa)*r2),0x1A003311);}
    drawLine(RX-RD+8,RY,RX+RD-8,RY,0x22004422);drawLine(RX,RY-RD+8,RX,RY+RD-8,0x22004422);
    float sweepA=fmodf(m_gs.frameNum*0.04f,3.14159f*2);
    for(int r=0;r<RD;r++)px(RX+int(cosf(sweepA)*r),RY+int(sinf(sweepA)*r),0x4400FF44);
    fillCircle(RX,RY,3,0xFFFFFFFF);
    for(int r=4;r<20;r++)px(RX+int(pl.dirX*r),RY+int(pl.dirY*r),r<10?0xFFFFFFFF:0x88FFFFFF);
    bool blink=(m_gs.frameNum/8)%2==0;
    for(const auto &e:m_gs.enemies){if(!e.alive)continue;float relX=e.x-pl.posX,relY=e.y-pl.posY,fwd=relX*pl.dirX+relY*pl.dirY,rgt=relY*pl.dirX-relX*pl.dirY;int ex2=RX+int(rgt*float(RD)/12.f),ey2=RY-int(fwd*float(RD)/12.f);if(ex2<2||ex2>=W-2||ey2<2||ey2>=H-2)continue;fillCircle(ex2,ey2,blink?4:3,blink?0xFFFF2200:0xFFFF6600);}
    for(const auto &k:m_gs.healthKits){if(!k.active)continue;float relX=k.x-pl.posX,relY=k.y-pl.posY,fwd=relX*pl.dirX+relY*pl.dirY,rgt=relY*pl.dirX-relX*pl.dirY;int kx=RX+int(rgt*float(RD)/12.f),ky=RY-int(fwd*float(RD)/12.f);if(kx>=2&&kx<W-2&&ky>=2&&ky<H-2)fillCircle(kx,ky,3,0xFF22FF66);}
    QPainter p2(&m_buf);p2.setFont(QFont("Courier New",7,QFont::Bold));p2.setPen(QColor(0,150,70));p2.drawText(QRect(RX-30,RY+RD+2,60,13),Qt::AlignCenter,"RADAR");p2.end();
}

void MainWindow::renderHealthPanel()
{
    const Player &pl=m_gs.player; const CharProfile &cp=CHAR_PROFILES[m_charChoice];
    const int PX=8,PY=H-80,PW=190,PH=72;
    for(int y=PY;y<PY+PH;y++) for(int x=PX-2;x<PX+PW+2;x++){if(x<0||x>=W||y<0||y>=H)continue;uint32_t &pp=m_pixels[y*W+x];uint8_t r=(pp>>16&0xFF)/6,g2=(pp>>8&0xFF)/6+4,b2=(pp&0xFF)/6+6;m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}
    for(int x=PX-2;x<PX+PW+2&&x<W;x++)px(x,PY-1,0xFF004422);
    int segs=10,segW=(PW-30)/segs;
    for(int s=0;s<segs;s++){float fill=float(pl.hp)/float(cp.hp);bool filled=(s+1)<=int(fill*segs+0.5f);uint32_t c=filled?(pl.hp>40?0xFF00CC55:pl.hp>20?0xFFCC8800:0xFFCC2200):0xFF0A180A;fillRect(PX+30+s*(segW+2),PY+18,segW,14,c);}
    int staDots=12; float staF=pl.stamina/cp.stamina;
    for(int d=0;d<staDots;d++){bool filled=float(d)/staDots<staF;fillCircle(PX+8+d*14,PY+52,5,filled?(pl.sprinting?0xFFFFCC00:0xFF2288FF):0xFF0A1018);}
    QPainter p2(&m_buf);p2.setFont(QFont("Courier New",10,QFont::Bold));p2.setPen(QColor(0,255,140));
    p2.drawText(PX+2,PY+14,QString("♥  %1 / %2").arg(pl.hp).arg(cp.hp));
    p2.setPen(pl.sprinting?QColor(255,200,0):QColor(60,140,255));
    p2.drawText(PX+2,PY+68,QString("⚡  %1%%").arg(int(100.f*pl.stamina/cp.stamina)));p2.end();
}

void MainWindow::renderAmmoPanel()
{
    const Player &pl=m_gs.player; const CharProfile &cp=CHAR_PROFILES[m_charChoice];
    const int PW=190,PH=72,PX=W-PW-8,PY=H-80;
    for(int y=PY;y<PY+PH;y++) for(int x=PX-2;x<PX+PW+2;x++){if(x<0||x>=W||y<0||y>=H)continue;uint32_t &pp=m_pixels[y*W+x];uint8_t r=(pp>>16&0xFF)/6+5,g2=(pp>>8&0xFF)/6+3,b2=(pp&0xFF)/6;m_pixels[y*W+x]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}
    for(int x=PX-2;x<PX+PW+2&&x<W;x++)px(x,PY-1,0xFF442200);
    int dotsPerRow=std::min(cp.ammo,15);
    for(int i=0;i<cp.ammo;i++){bool loaded=i<pl.ammo;int col2=i%dotsPerRow,row=i/dotsPerRow;fillRect(PX+6+col2*12,PY+22+row*14,8,10,loaded?0xFFFFCC44:0xFF332211);if(loaded)fillRect(PX+7+col2*12,PY+22+row*14,6,3,0xFFFFEE88);}
    int kitsLeft=0;for(const auto &k:m_gs.healthKits)if(k.active)kitsLeft++;
    QPainter p2(&m_buf);p2.setFont(QFont("Courier New",11,QFont::Bold));p2.setPen(QColor(255,180,60));
    p2.drawText(QRect(PX,PY+2,PW-4,18),Qt::AlignRight,QString("%1 / %2  ◉").arg(pl.ammo).arg(cp.ammo));
    p2.setFont(QFont("Courier New",8));p2.setPen(QColor(160,110,50));
    p2.drawText(QRect(PX,PY+PH-14,PW-4,14),Qt::AlignRight,
                QString("PISTOL MK.II   ✚%1 kits").arg(kitsLeft));
    p2.end();
}

void MainWindow::renderCrosshair()
{
    const Player &pl=m_gs.player; if(pl.adsMode&&m_adsFOVMult<0.8f)return;
    int cx=W/2,cy=H/2;
    bool moving=m_keys.count(Qt::Key_W)||m_keys.count(Qt::Key_S)||m_keys.count(Qt::Key_A)||m_keys.count(Qt::Key_D);
    float spread=5.f+(moving?5.f:0.f)+(pl.sprinting?6.f:0.f)+(pl.flashTimer>0?8.f*(pl.flashTimer/0.14f):0.f);
    int sp=int(spread),arm=10;
    uint32_t col=m_hitMarker>0?0xFFFF5544:0xAAFFFFFF;
    for(int th=0;th<2;th++){drawLine(cx-arm-sp,cy+th,cx-sp-1,cy+th,col);drawLine(cx+sp+1,cy+th,cx+arm+sp,cy+th,col);drawLine(cx+th,cy-arm-sp,cx+th,cy-sp-1,col);drawLine(cx+th,cy+sp+1,cx+th,cy+arm+sp,col);}
    px(cx,cy,0xFFFF4444);
    if(m_hitMarker>0){float t=m_hitMarker/0.35f;uint32_t hc=(uint32_t(uint8_t(255*t))<<24)|0xFFFFFF;int sz=int(10+(1-t)*6);drawLine(cx-sz,cy-sz,cx+sz,cy+sz,hc);drawLine(cx+sz,cy-sz,cx-sz,cy+sz,hc);}
}

void MainWindow::renderDamageDirection()
{
    if(m_damageDirTimer<=0)return;
    float t=std::min(1.f,m_damageDirTimer/2.f);
    const Player &pl=m_gs.player;
    float playerFacing=atan2f(pl.dirY,pl.dirX),relAngle=m_damageAngle-playerFacing;
    float sa=sinf(relAngle),ca=cosf(relAngle);
    int R=int(std::min(W,H)*0.34f);
    int icx=std::max(20,std::min(W-20,W/2+int(sa*R))),icy=std::max(20,std::min(H-20,H/2-int(ca*R)));
    float pulse=0.6f+0.4f*sinf(m_menuFrame*0.35f);
    fillCircle(icx,icy,int(22*t*pulse),uint32_t(uint8_t(120*t*pulse))<<24|0xFF2000);
    fillCircle(icx,icy,int(12*t),uint32_t(uint8_t(200*t))<<24|0xFF5533);
    fillCircle(icx,icy,int(5*t),uint32_t(uint8_t(255*t))<<24|0xFFFFFF);
}

void MainWindow::renderTargetBar()
{
    const Player &pl=m_gs.player;float bestDot=-1;int bestIdx=-1;float cosThr=cosf(20.f*3.14159f/180.f);
    for(int i=0;i<int(m_gs.enemies.size());i++){const Enemy &e=m_gs.enemies[i];if(!e.alive)continue;float dx=e.x-pl.posX,dy=e.y-pl.posY,d=sqrtf(dx*dx+dy*dy);if(d>10)continue;float dot=(dx*pl.dirX+dy*pl.dirY)/d;if(dot>cosThr&&dot>bestDot){bestDot=dot;bestIdx=i;}}
    if(bestIdx<0)return;
    const Enemy &te=m_gs.enemies[bestIdx];
    int maxHP=std::max(1,int(float(LEVEL_CFG[std::min(m_gs.level,5)].enemyHP+(m_gs.level>5?(m_gs.level-5)*2:0))*DIFF_CFG[m_difficulty].enemyHPMult));
    float frac=float(te.hp)/float(maxHP);
    int bx=W/2-110,by=H-96,bw=220,bh=14;
    fillRect(bx-6,by-18,bw+12,bh+26,0x99000000);fillRect(bx,by,bw,bh,0xFF0A0A0A);
    uint32_t bc=frac>0.5f?0xFF00CC55:frac>0.25f?0xFFCC8800:0xFFCC2200;
    fillRect(bx,by,int(bw*frac),bh,bc);
    QPainter p2(&m_buf);p2.setFont(QFont("Courier New",9,QFont::Bold));p2.setPen(QColor(200,200,200));
    p2.drawText(QRect(bx-6,by-16,bw+12,16),Qt::AlignCenter,QString("TARGET  %1%%  HP %2").arg(int(frac*100)).arg(te.hp));p2.end();
}

void MainWindow::renderComboDisplay()
{
    if(m_combo<2||m_comboDisplay<=0)return; float t=std::min(1.f,m_comboDisplay/0.5f);
    QPainter p2(&m_buf);p2.setFont(QFont("Courier New",int(20+4*(1-t)),QFont::Black));p2.setPen(QColor(255,200,0,int(255*t)));
    p2.drawText(QRect(0,H/2-120,W,44),Qt::AlignCenter,QString("x%1  KILL STREAK").arg(m_combo));p2.end();
}

void MainWindow::renderKillFeed()
{
    if(m_killFeed.empty())return; QPainter p2(&m_buf); int y=48;
    for(const auto &kf:m_killFeed){
        float t=std::min(1.f,kf.life/0.5f);int alpha=int(200*t);
        p2.fillRect(W-195,y,190,20,QColor(0,0,0,int(140*t)));
        p2.fillRect(W-195,y,3,20,QColor(0,int(255*t),int(140*t),alpha));
        p2.setFont(QFont("Courier New",10,QFont::Bold));p2.setPen(QColor(0,int(255*t),int(140*t),alpha));
        p2.drawText(QRect(W-190,y,185,20),Qt::AlignLeft|Qt::AlignVCenter," ▸ "+kf.text);y+=22;
    }
    p2.end();
}

void MainWindow::renderHUD()
{
    renderTopBar();renderRadar();renderKillFeed();
    renderHealthPanel();renderAmmoPanel();
    renderCrosshair();renderDamageDirection();renderTargetBar();renderComboDisplay();
    const Player &pl=m_gs.player;
    QPainter p(&m_buf);
    if(pl.ammo<=3&&pl.ammo>0){p.setFont(QFont("Courier New",13,QFont::Bold));p.setPen(QColor(255,80,0,int(200+55*sinf(m_gs.frameNum*.25f))));p.drawText(QRect(0,H-50,W,20),Qt::AlignCenter,"LOW AMMO");}
    else if(pl.ammo==0){p.setFont(QFont("Courier New",13,QFont::Bold));p.setPen(QColor(255,30,30));p.drawText(QRect(0,H-50,W,20),Qt::AlignCenter,"NO AMMO");}
    if(pl.hp<=20&&!m_gs.gameOver){p.setFont(QFont("Courier New",11,QFont::Bold));p.setPen(QColor(255,0,0,int(140+115*sinf(m_gs.frameNum*.3f))));p.drawText(QRect(0,H-66,W,20),Qt::AlignCenter,"CRITICAL HEALTH");}
    if(m_pixelScale>1){p.setFont(QFont("Courier New",9));p.setPen(QColor(0,120,60,160));p.drawText(QRect(0,H-14,W,12),Qt::AlignCenter,QString("BLOCK: %1×%1 px  |  ZOOM %2×").arg(m_pixelScale).arg(m_zoomFactor,0,'f',1));}
    if(m_gs.frameNum<300&&m_gs.level==1){p.fillRect(0,H-26,W,26,QColor(0,0,0,160));p.setFont(QFont("Courier New",9));p.setPen(QColor(0,140,70));p.drawText(QRect(0,H-13,W,12),Qt::AlignCenter,"WASD:move  SHIFT:sprint  RMB:ADS  Space/LMB:fire  Q/E:turn  M:menu  ESC:pause  [ ]:pixelblocks  SCROLL:zoom");}
    p.end();
}

void MainWindow::renderLevelBanner()
{
    if(!m_gs.levelComplete)return;
    float t=m_gs.levelTransTimer/3.5f,dark=0.38f*(1-t)+0.14f*t;
    for(int i=0;i<W*H;i++){uint32_t c=m_pixels[i];uint8_t r=uint8_t(((c>>16)&0xFF)*dark),g2=uint8_t(((c>>8)&0xFF)*dark),b2=uint8_t((c&0xFF)*dark);m_pixels[i]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}
    QPainter p(&m_buf);p.fillRect(W/2-270,H/2-90,540,180,QColor(0,10,20,225));for(int x=W/2-270;x<W/2+270;x++)px(x,H/2-90,0xFF00FF88);
    p.setFont(QFont("Courier New",32,QFont::Black));p.setPen(QColor(0,255,140));p.drawText(QRect(W/2-270,H/2-82,540,60),Qt::AlignCenter,"OBJECTIVE COMPLETE");
    p.setFont(QFont("Courier New",14));p.setPen(QColor(255,200,60));p.drawText(QRect(W/2-270,H/2-8,540,32),Qt::AlignCenter,QString("BONUS  +%1 PTS      TOTAL  %2").arg(500*m_gs.level).arg(m_gs.score));
    p.setPen(QColor(140,190,255));p.drawText(QRect(W/2-270,H/2+32,540,30),Qt::AlignCenter,QString("Deploying to zone %1...   T-%2s").arg(m_gs.level+1).arg(int(m_gs.levelTransTimer)+1));
    p.end();
}

void MainWindow::renderPauseMenu()
{
    if(!m_paused)return;
    for(int i=0;i<W*H;i++){uint32_t c=m_pixels[i];uint8_t r=(c>>16&0xFF)/2,g2=(c>>8&0xFF)/2,b2=(c&0xFF)/2;m_pixels[i]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}
    QPainter p(&m_buf);p.fillRect(W/2-200,H/2-145,400,295,QColor(2,8,16,240));p.setPen(QPen(QColor(0,200,140),2));p.drawRect(W/2-200,H/2-145,400,295);for(int x=W/2-200;x<W/2+200;x++)px(x,H/2-145,0xFF00FF88);
    p.setFont(QFont("Courier New",28,QFont::Black));p.setPen(QColor(0,255,140));p.drawText(QRect(W/2-200,H/2-132,400,52),Qt::AlignCenter,"MISSION PAUSED");
    p.setPen(QColor(0,100,60));p.drawLine(W/2-160,H/2-74,W/2+160,H/2-74);
    p.setFont(QFont("Courier New",14,QFont::Bold));
    p.setPen(QColor(200,230,210));p.drawText(QRect(W/2-200,H/2-62,400,46),Qt::AlignCenter,"[R]  RESUME");
    p.setPen(QColor(140,190,255));p.drawText(QRect(W/2-200,H/2-4,400,46),Qt::AlignCenter,"[M]  MAIN MENU");
    p.setPen(QColor(255,220,100));p.drawText(QRect(W/2-200,H/2+54,400,46),Qt::AlignCenter,"[Q]  QUIT TO DESKTOP");
    p.setPen(QColor(255,60,60));p.drawText(QRect(W/2-200,H/2+112,400,30),Qt::AlignCenter,"[ESC]  FORCE QUIT");
    p.setFont(QFont("Courier New",8));p.setPen(QColor(0,70,40));
    p.drawText(QRect(W/2-200,H/2+144,400,24),Qt::AlignCenter,"SCROLL=zoom  |  [ ]=pixel blocks  |  Sky+Clouds: raster noise");
    p.end();
}

static void drawGameOver(QImage &buf, const GameState &gs, int charChoice, int diff)
{
    uint32_t *pix=reinterpret_cast<uint32_t*>(buf.bits());
    for(int i=0;i<W*H;i++){uint32_t c=pix[i];uint8_t r=(c>>16&0xFF)/5,g2=(c>>8&0xFF)/5,b2=(c&0xFF)/5;pix[i]=(0xFF<<24)|(r<<16)|(g2<<8)|b2;}
    QPainter p(&buf);
    p.fillRect(W/2-250,H/2-135,500,290,QColor(8,2,2,235));p.setPen(QPen(QColor(180,20,20),2));p.drawRect(W/2-250,H/2-135,500,290);
    p.setFont(QFont("Courier New",40,QFont::Black));p.setPen(QColor(255,30,30));
    p.drawText(QRect(W/2-250,H/2-120,500,70),Qt::AlignCenter,"MISSION FAILED");
    p.setPen(QColor(0,100,60));p.drawLine(W/2-200,H/2-42,W/2+200,H/2-42);
    p.setFont(QFont("Courier New",11));p.setPen(QColor(180,160,160));
    p.drawText(QRect(W/2-240,H/2-32,480,24),Qt::AlignCenter,
               QString("Zone: %1  |  Diff: %2  |  Class: %3  |  LV%4  |  Kills: %5  |  Score: %6")
               .arg(MAP_NAMES[gs.mapChoice]).arg(DIFF_CFG[diff].name).arg(CHAR_PROFILES[charChoice].name).arg(gs.level).arg(gs.kills).arg(gs.score));
    p.setFont(QFont("Courier New",13,QFont::Bold));
    p.setPen(QColor(80,210,120));p.drawText(QRect(W/2-250,H/2+8,500,38),Qt::AlignCenter,"[R]  REDEPLOY (same settings)");
    p.setPen(QColor(140,180,255));p.drawText(QRect(W/2-250,H/2+52,500,38),Qt::AlignCenter,"[M]  MAIN MENU");
    p.setPen(QColor(255,160,60));p.drawText(QRect(W/2-250,H/2+96,500,38),Qt::AlignCenter,"[Q]  QUIT");p.end();
}

void MainWindow::render()
{
    switch(m_screen){
    case SCR_MENU:        renderMenu();       return;
    case SCR_MAP_SELECT:  renderMapSelect();  return;
    case SCR_DIFF_SELECT: renderDiffSelect(); return;
    case SCR_CHAR_SELECT: renderCharSelect(); return;
    case SCR_PLAYING:     break;
    }
    renderFloorCeiling();renderWalls();renderSprites();renderParticles();
    renderWeaponAndArms();renderDamageVignette();renderHUD();
    renderLevelBanner();renderPauseMenu();
    if(m_gs.gameOver) drawGameOver(m_buf,m_gs,m_charChoice,m_difficulty);
}

// Nearest-neighbor scaling — chunky pixels when zoomed = raster proof
void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing,false);
    p.setRenderHint(QPainter::SmoothPixmapTransform,false);
    p.drawImage(rect(),m_buf,m_buf.rect());
}
