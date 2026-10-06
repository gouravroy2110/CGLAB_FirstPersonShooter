// =============================================================================
//  mainwindow.h
// =============================================================================
#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <set>
#include <unordered_map>
#include <vector>
#include <QString>
#include "game.h"
#include "raster.h"
#include "assets.h"
#include "audio.h"

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    // Dev aid: render a scripted set of scenes to PNG files and return.
    bool snapshot(const QString &dir);
    // Open the sound device (skipped in snapshot/test mode)
    void enableAudio();

protected:
    void paintEvent(QPaintEvent *)      override;
    void keyPressEvent(QKeyEvent *)     override;
    void keyReleaseEvent(QKeyEvent *)   override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent *)  override;
    void wheelEvent(QWheelEvent *)      override;
    void focusOutEvent(QFocusEvent *)   override;
    bool focusNextPrevChild(bool)       override { return false; }   // keep TAB for aiming

private slots:
    void tick();

private:
    // -- Framebuffer -------------------------------------------------------------
    QImage     m_buf;
    uint32_t  *m_pixels;
    rs::Canvas m_cv;
    std::vector<float> m_depth;          // per-pixel z-buffer (view depth), 1e30 = sky
    int        m_colTop[W];              // first terrain row per column; sky above it

    // -- World ---------------------------------------------------------------------
    ParsedMap  m_parsed[3];
    int        m_map[MAP_H][MAP_W];      // CellType
    float      m_plat[MAP_H][MAP_W];     // ledge / pit offsets
    bool       m_acid[MAP_H][MAP_W];
    float      m_cellBase[MAP_H][MAP_W]; // ground level a wall stands on
    float      m_doorT[MAP_H][MAP_W];    // 0 closed .. 1 fully sunk into the floor
    bool       m_doorMoving[MAP_H][MAP_W];
    std::vector<float> m_vh;             // rolling-terrain heights at cell corners
    float smoothH(float x, float y) const;        // bilinear rolling terrain
    float floorH(float x, float y) const;         // terrain + ledge/pit of that cell
    float cellTop(int cx, int cy) const;          // top of a solid cell
    bool  solidCell(int cx, int cy) const;
    float groundAt(float x, float y) const;       // standing height (wall tops for solid cells)

    // -- Baked assets ------------------------------------------------------------
    std::vector<uint32_t> m_sky[3];
    MipTex                m_floorTex[3];
    MipTex                m_wallTex[3][6];
    std::vector<uint32_t> m_enemyAtlas, m_eliteAtlas, m_scoutAtlas, m_shotgunAtlas, m_sniperAtlas;
    std::vector<uint8_t>  m_enemyMask, m_eliteMask, m_scoutMask, m_shotgunMask, m_sniperMask;
    std::vector<uint32_t> m_pickupTex;
    std::vector<uint32_t> m_portrait[3];
    static const int PORTRAIT = 160;
    int m_theme = 0;

    // Light maps (12 texels per tile): sun visibility + final ground light
    // (Lambert on the terrain slope x shadow x ambient occlusion)
    static const int LM = 12;
    std::vector<uint8_t> m_sunVis, m_light;
    std::vector<uint8_t> m_sunVisCache[3], m_lightCache[3];   // pristine maps, baked once
    // Bullet holes: one small decal texture per wall face that has been hit
    static const int WD_U = 96, WD_V = 256;                  // 96 texels per world unit (~1 cm)
    std::unordered_map<int, std::vector<uint8_t>> m_wallDecals;
    void stampWallDecal(int cx, int cy, int face, float u, float v, bool pellet);
    // Floor decals (32 texels per tile): blood + scorch marks
    static const int DM = 32;
    std::vector<uint8_t> m_blood, m_scorch;
    void buildLightMap(int x0 = 0, int y0 = 0, int x1 = MAP_W, int y1 = MAP_H);
    float sunVisAt(float x, float y) const;
    void  addDecal(std::vector<uint8_t> &d, float x, float y, float radius, int amount);

    // -- Raster proof: magnifier (mouse wheel) + pixel-block mode ([ / ]) --------
    int     m_rasterZoom = 1;                     // 1,2,4,...,32: framebuffer pixels -> boxes
    float   m_zoomCX = W / 2.f, m_zoomCY = H / 2.f; // magnifier centre (framebuffer coords)
    QPointF m_mouseWidget{W / 2.0, H / 2.0};
    QImage  m_display;                            // window-sized output, filled by our own upscale
    int     m_pixelScale = 1;
    void    zoomOrigin(float &x0, float &y0) const;
    QPointF widgetToFrame(const QPointF &p) const; // undoes window scaling + magnifier
    static int fontScale(int px);
    const QImage &composeDisplay(int ww, int wh);   // framebuffer -> window pixels

    // -- Camera ------------------------------------------------------------------
    float m_adsFOVMult = 1.f;
    float m_adsT       = 0.f;    // 0 = hip, 1 = fully aimed
    float m_pitch      = 0.f;    // vertical look as a slope (tan of angle)
    float m_footZ = 0.f, m_velZ = 0.f;   // player feet height + vertical speed
    bool  m_onGround = true;
    float m_acidAcc = 0.f;
    bool  inAir() const { return !m_onGround; }
    float m_K = H;               // vertical projection scale this frame
    int   m_hor = H / 2;         // horizon row this frame
    float m_eye = 0.5f;          // eye height this frame
    void  computeView();
    bool  project(float wx, float wy, float wz, float &sx, float &sy, float &depth) const;
    void  turn(float ang);

    // -- Free-aim crosshair --------------------------------------------------------
    float m_aimX = W / 2.f, m_aimY = H / 2.f;
    bool  m_freeAim = true;
    float m_mouseDX = 0, m_mouseDY = 0;     // movement this frame (for weapon sway)
    float m_swayX = 0, m_swayY = 0;
    float m_breath = 0, m_scopeSwayX = 0, m_scopeSwayY = 0;
    float m_scopeMag = 4.f;
    bool  m_holdBreath = false;
    void  applyAimDelta(float dx, float dy);
    void  aimPoint(float &ax, float &ay) const;

    // -- Weapon animation ----------------------------------------------------------
    float m_kick = 0, m_actionT = 0, m_camKick = 0, m_recoilPitch = 0;
    float m_muzzleX = W * 0.6f, m_muzzleY = H * 0.6f;
    float m_ejectX = W * 0.7f, m_ejectY = H * 0.75f;
    int   m_pendingWeapon = -1;
    int   m_knifeSwing = 0;
    float m_reloadTotal = 1.f, m_sprintT = 0.f;
    float m_flashLight = 0;
    int   m_flashSeed = 0;
    struct Casing { float x, y, vx, vy, a, va, life; bool shell; };
    std::vector<Casing> m_casings;

    // -- Loop ----------------------------------------------------------------------
    QTimer        m_timer;
    QElapsedTimer m_clock;
    double        m_lastMs = 0;
    double        m_fps    = 60;
    float         m_dt     = 0.016f;
    int           m_menuFrame = 0;
    float         m_time = 0;
    void advance(float dt);

    // -- Input ---------------------------------------------------------------------
    std::set<int> m_keys;
    bool m_lmb = false;

    // -- UI state ------------------------------------------------------------------
    GameScreen m_screen     = SCR_MENU;
    bool       m_paused     = false;
    int        m_mapChoice  = 1;
    int        m_charChoice = 0;
    int        m_difficulty = 1;
    int        m_menuCursor = 0;
    float      m_menuCamAng = 0.f;
    int        m_loadedMap  = -1;
    struct HitRect { int x, y, w, h, id; };
    std::vector<HitRect> m_menuRects;
    void loadMapGeometry(int map);
    void menuConfirm();
    void menuBack();

    // -- Feedback ------------------------------------------------------------------
    float m_hitMarker = 0, m_headMarker = 0, m_killMarker = 0, m_headshotBanner = 0, m_secretBanner = 0, m_acidFlash = 0;
    float m_damageAngle = 0, m_damageDirTimer = 0;
    int   m_combo = 0;
    float m_comboTimer = 0, m_comboDisplay = 0;
    int   m_probeEnemy = -1, m_probePart = 0;
    bool  m_nearSecret = false, m_nearDoor = false;
    float m_enemyHPDisplay[12] = {};
    struct KFEntry { QString text; float life; bool head; };
    std::vector<KFEntry> m_killFeed;
    void pushFeed(const QString &t, float life, bool head = false);
    float m_emergencyAmmoCooldown = 0.f;
    float m_emergencyHealthCooldown = 0.f;

    GameState m_gs;

    // -- Text (bitmap pixel font, rasterized into the framebuffer) ---------------------
    struct TextCmd { QRectF r; int flags; QString s; QColor c; int px; int weight; bool shadow; };
    std::vector<TextCmd> m_text;
    void text(float x, float y, float w, float h, int flags, const QString &s, QColor c,
              int px, int weight = 600, bool shadow = true);
    void flushText();

    // -- Logic ---------------------------------------------------------------------
    void initGame();
    void startDebugMode();
    void loadLevel(int lv);
    void gameStep(float dt);
    void updateEnemies(float dt);
    void tryFire();
    void fireWeapon();
    void startReload();
    void switchWeapon(int w);
    void enemyFire(Enemy &e);
    void damageEnemy(int idx, int dmg, HitPart part, float hx, float hy, float hz);
    void spawnBlood(float wx, float wy, float wz, int count, float force);
    void spawnImpact(float wx, float wy, float wz, bool floor);
    bool posValid(float x, float y, float r = 0.28f) const;
    bool canStep(float x, float y, float fromZ, float maxUp, float r = 0.28f) const;
    bool hasLOS(float x0, float y0, float z0, float x1, float y1, float z1) const;
    void tryOpenDoor();
    void updateDoors(float dt);
    void spawnPickup(float x, float y);
    bool findEmergencyDropPos(float &outX, float &outY) const;
    bool findHotZoneDropPos(float &outX, float &outY) const;
    void checkEmergencyDrops(float dt);
    int  enemyFrame(const Enemy &e) const;
    int  enemyMaxHP() const;
    void spawnEnemy(float x, float y, bool elite, EnemyType type);
    void checkReinforcements(float dt);
    float m_reinforceTimer = 0.f;

    // -- Sound -------------------------------------------------------------------
    SoundEngine *m_snd = nullptr;
    int   m_ambId = -1;
    float m_footstepPrev = 0, m_heartbeatT = 0, m_acidSndT = 0, m_hurtSndT = 0, m_alertSndT = 0, m_prevAdsT = 0;
    bool  m_prevGameOver = false;
    void  sfx(SoundId id, float gain = 1.f, float pitch = 1.f, float delay = 0.f);
    void  sfxAt(SoundId id, float wx, float wy, float gain = 1.f, float pitch = 1.f, float delay = 0.f);
    void  updateAmbience();

    // -- Between-zone intermission (upgrade + loadout) -------------------------------
    float m_intermissionT = 0.f;     // seconds since the zone was cleared
    int   m_hoverCard = -1, m_hoverChip = -1;
    bool  m_hoverDeploy = false;
    int   m_loadoutWeapon = WPN_PISTOL;
    std::vector<int> loadoutChoices() const;
    QRect ixCardRect(int i) const;
    QRect ixChipRect(int slot, int n) const;
    QRect ixDeployRect() const;
    void  updateIntermissionHover(const QPointF &widgetPos);
    void  deployNextZone();

    // -- Reachability (cells the player can walk to without secrets) ----------------
    std::vector<uint8_t> m_reach;
    void  computeReachable(float sx, float sy);
    bool  reachable(int cx, int cy) const { return cx >= 0 && cy >= 0 && cx < MAP_W && cy < MAP_H && !m_reach.empty() && m_reach[size_t(cy) * MAP_W + cx]; }
    float m_probeDepth = 1e9f;

    // -- Weapon Upgrade / Attachment System ----------------------------------------
    struct UpgradeCard {
        int weapon = -1; // 0..3 (WPN_KNIFE..WPN_RIFLE) or -1
        int slot = 0;    // 0: Ext Mag, 1: Compensator/Choke, 2: High Caliber AP Ammo, 3: Special/Optic
        QString title;
        QString desc;
        QString bonus;
    };
    std::vector<UpgradeCard> m_upgradeCards;
    int  m_selectedUpgradeCard = -1;
    bool m_upgradePicked = false;
    float m_upgradeDecisionTimer = 30.0f;
    float m_deployTimer = 0.0f;
    bool m_showArmory = false;
    int  effectiveMagSize(int w) const;
    int  effectiveDamage(int w) const;
    float effectiveRecoil(int w) const;
    float effectiveSpread(int w) const;
    float effectiveFireDelay(int w) const;
    bool  effectiveScope(int w) const;
    float effectiveAdsFov(int w) const;
    void applyUpgrade(int weapon, int slot);
    void generateUpgradeChoices();
    void renderArmoryOverlay();

    struct HitResult {
        int enemy = -1; HitPart part = PART_NONE;
        float x = 0, y = 0, z = 0, depth = 1e9f;
        bool wall = false, floor = false, sky = false;
        int cx = 0, cy = 0, face = 0; float u = 0, v = 0;   // wall-face hit coordinates
    };
    HitResult hitscan(float ax, float ay) const;

    // -- Screens -------------------------------------------------------------------
    void renderMenuBackdrop();
    void renderMenu();
    void renderMapSelect();
    void renderDiffSelect();
    void renderCharSelect();
    void menuHeader(const QString &step, const QString &title);
    void menuFooter(const QString &hint);
    void panel(int x, int y, int w, int h, bool selected, uint32_t accent);

    // -- World pipeline ----------------------------------------------------------------
    void render();
    void renderSky();
    void renderTerrain();
    void pixelate();
    void renderLensFlare();
    void renderSprites();
    void renderTracers();
    void renderParticles();
    void renderDamageNumbers();

    // -- First-person weapon -----------------------------------------------------------
    void renderViewModel();
    void renderReflexSight();
    void renderScope();
    void renderCasings();

    // -- Post / HUD --------------------------------------------------------------------
    void renderPostFX();
    void renderHUD();
    void renderCompass();
    void renderMinimap();
    void renderHealthPanel();
    void renderWeaponPanel();
    void renderCrosshair();
    void renderHitMarkers();
    void renderDamageDirection();
    void renderTargetInfo();
    void renderKillFeed();
    void renderLevelBanner();
    void renderPauseMenu();
    void renderGameOver();

    // -- Colour helpers ------------------------------------------------------------------
    uint32_t fog(uint32_t c, float dist) const;
};
