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
#include "world.h"
#include "sim.h"

struct Raster3D;
struct WorldCam;

class MainWindow : public QWidget, public World {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    // Dev aid: render a scripted set of scenes to PNG files and return.
    bool snapshot(const QString &dir);
    // Open the sound device (skipped in snapshot/test mode)
    void enableAudio();
    // Player options (QSettings); not loaded in snapshot mode so tests use defaults
    void loadSettings();

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
    // (grid, heightfield, light map and decals live in World)
    ParsedMap  m_parsed[MAP_COUNT];

    // -- Baked assets ------------------------------------------------------------
    std::vector<uint32_t> m_sky[3];
    MipTex                m_floorTex[3];
    MipTex                m_wallTex[3][6];
    std::vector<uint32_t> m_enemyAtlas, m_eliteAtlas, m_scoutAtlas, m_shotgunAtlas, m_sniperAtlas;
    std::vector<uint8_t>  m_enemyMask, m_eliteMask, m_scoutMask, m_shotgunMask, m_sniperMask;
    std::vector<uint32_t> m_pickupTex;
    std::vector<uint32_t> m_portrait[3];
    static const int PORTRAIT = 160;

    std::vector<uint8_t> m_sunVisCache[MAP_COUNT], m_lightCache[MAP_COUNT];   // pristine maps, baked once

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
    float m_acidAcc = 0.f;
    bool  inAir() const { return !m_gs.player.onGround; }
    float m_K = H;               // vertical projection scale this frame
    int   m_hor = H / 2;         // horizon row this frame
    float m_eye = 0.5f;          // eye height this frame
    void  computeView();
    // -- Third-person camera (V toggles, X swaps the shoulder) ----------------------
    float m_camX = 2.5f, m_camY = 2.5f;    // camera ground position this frame (= player in first person)
    bool  m_tpp = false;
    bool  m_shoulderLeft = false;
    float m_boomT = 0.f;                   // 0 = first person .. 1 = third person (animated)
    float m_boomCur = 0.f;                 // boom length after wall collision (smoothed)
    float m_shoulderT = 1.f;               // -1 left shoulder .. +1 right shoulder (animated)
    float m_headZ = 0.5f;                  // player eye height this frame
    void  updateCamera(float dt);
    float boomWanted() const;              // boom length before collision
    void  boomOffset(float len, float &dx, float &dy, float &dz) const;
    bool  thirdPerson() const { return m_boomT > 0.5f; }
    void  playerMuzzle(float &x, float &y, float &z) const;   // world-space gun muzzle (third person)
    void  renderActors3D();
    void  renderProps3D(struct Raster3D &r, const struct WorldCam &cam);
    void  renderWorldFX();             // plasma, beams, laser, radar marks, smoke shading
    void  renderScreenFlash();         // flashbang white-out + explosion light
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
    bool m_jumpQueued = false;

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
    // -- Settings + Arsenal (menus.cpp) ------------------------------------------------
    bool  m_testMode = false;           // --snapshot run
    float m_mouseSens = 1.f;
    int   m_fovDeg = 67;
    int   m_volume = 80;
    int   m_crossColor = 0;
    bool  m_showFps = true;
    float m_planeLen = 0.66f;           // camera plane length = tan(FOV / 2)
    int   m_settingsRow = 0;
    int   m_arsenalSel = WPN_RIFLE;
    void  saveSettings() const;
    void  applySettings();
    int   settingsRows() const;
    void  changeSetting(int row, int dir);
    uint32_t crosshairColor() const;
    void  renderSettings();
    void  renderArsenal();

    // -- Feedback ------------------------------------------------------------------
    float m_hitMarker = 0, m_headMarker = 0, m_killMarker = 0, m_headshotBanner = 0, m_secretBanner = 0, m_acidFlash = 0;
    float m_damageAngle = 0, m_damageDirTimer = 0;
    // Damage indicators remember where the hit came from (world space), so the
    // arc keeps pointing at the attacker while you turn. Up to 6 at once.
    struct DamageMark { float x, y, t, strength; };
    std::vector<DamageMark> m_damageMarks;
    void addDamageMark(float srcX, float srcY, float strength);
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
    void startReload();
    void switchWeapon(int w);
    void enemyFire(Enemy &e, float tx, float ty, float tz);
    // byActor >= 0: a squad bot made the hit (no player score, markers or combo)
    void damageEnemy(int idx, int dmg, HitPart part, float hx, float hy, float hz, int byActor = -1);
    // -- Squad bots (actors.cpp) ------------------------------------------------------
    int   m_squadSize = 0;               // 0..3 AI teammates (Settings)
    void  spawnSquad();
    void  updateActors(float dt);
    int   hostileTarget(float x, float y, float z, float &tx, float &ty, float &tz, float &dist) const;
    void  damageActor(int i, int dmg, float fromX, float fromY);
    void spawnBlood(float wx, float wy, float wz, int count, float force);
    void spawnImpact(float wx, float wy, float wz, bool floor);
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
    float m_probeDepth = 1e9f;
    std::vector<float> m_vmDepth;      // z-buffer for the 3D first-person weapon

    // -- Zone objectives, report & coins ---------------------------------------------
    int   m_reinforceLeft = 0;         // reinforcements still to come this zone
    int   aliveEnemies() const;
    int   m_ixPage = 0;                // intermission: 0 = zone report, 1 = upgrades & loadout
    float m_ixPageStart = 0.f;
    struct ReportItem { QString label, detail; bool done; int coins, score; };
    std::vector<ReportItem> m_report;
    int   m_reportCoins = 0, m_reportScore = 0;
    QString m_ixMsg; float m_ixMsgT = 0.f;
    void  buildZoneReport();
    void  renderZoneReport();
    void  selectUpgradeCard(int i);
    QRect ixShopRect(int i) const;
    void  buyShopItem(int i);
    void  dropReward(float x, float y, bool boss);

    // -- Weapon Upgrade / Attachment System ----------------------------------------
    // slot: 0..5 = attachment slot (AT_*), or one of the UC_* card kinds below
    enum { UC_UNLOCK = 10, UC_RARITY = 11, UC_THROWS = 12, UC_GADGET = 13, UC_RESTOCK = 14 };
    struct UpgradeCard {
        int weapon = -1;  // gun the card is for, -1 = none
        int slot = 0;
        int option = 0;   // attachment option / rarity / gadget id
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
    float effectivePenetration(int w) const;
    QString weaponLabel(int w) const;
    void applyUpgrade(const UpgradeCard &c);
    void generateUpgradeChoices();

    // -- Arsenal (combat.cpp) -----------------------------------------------------------
    int   slotOf(int w) const;
    int   targetSlotFor(int w) const;
    void  giveWeapon(int w, const Loadout &ld, int mag = -1, int reserve = -1, bool equip = true);
    void  dropWeapon(int w);
    void  selectSlot(int s);
    void  updateReload(float dt);
    void  releaseTrigger();
    void  updateTrigger(float dt);
    void  applyRecoil(int w);
    void  shotOrigin(float &sX, float &sY, float &sZ) const;
    void  aimDirection(float ax, float ay, float &dx, float &dy, float &dz) const;
    void  fireOnce();
    void  fireHitscan(int w, float ax, float ay, float spreadPx, float sX, float sY, float sZ);
    void  fireArc(int w, float ax, float ay, float sX, float sY, float sZ);
    void  fireRail(int w, float ax, float ay, float sX, float sY, float sZ, float charge);
    void  fireFlame(int w, float sX, float sY, float sZ);
    void  igniteEnemy(int i, float sec);
    void  meleeAttack(int w, bool quick);
    void  quickMelee();
    int   enemyAtPoint(float x, float y, float z, float pad, HitPart *part) const;
    void  updateProjectiles(float dt);
    void  explode(float x, float y, float z, float radius, float damage, int owner, bool small);
    void  flashbang(float x, float y, float z);
    bool  smokeBlocks(float x0, float y0, float z0, float x1, float y1, float z1) const;
    void  startThrow();
    void  releaseThrow();
    void  throwVelocity(float &vx, float &vy, float &vz);
    void  updateThrow(float dt);
    void  cycleThrowable();
    void  useGadget();
    void  updateAreaEffects(float dt);
    void  updateStatusEffects(float dt);
    void  updatePickups(float dt);
    bool  trySwapPickup();
    int   randomWeaponForLevel(int lv, bool boss) const;
    Loadout rollLoadout(int w, int lv, bool boss) const;
    void  spawnWeaponPickup(float x, float y, int w, const Loadout &L, bool reward);
    bool  m_bipod = false, m_fanFire = false, m_shellReload = false, m_charging = false, m_cooking = false;
    float m_lastShotT = -10.f, m_bloom = 0.f, m_sprintFireT = 0.f, m_inspectT = 0.f, m_quickMeleeT = 0.f;
    int   m_sprayIndex = 0;
    float m_noiseT = 0.f;              // a loud shot was just fired (alerts nearby hostiles)
    float m_blastLight = 0.f;          // explosion flash on screen
    float m_flashT = 0.f;              // flashbang white-out
    float m_radarT = 0.f;              // radar ping: hostiles marked
    float m_pickupCooldown = 0.f;
    int   m_nearPickup = -1;           // weapon pickup under the player that needs [E] to swap
    float m_cookT = 0.f;
    bool  m_grappling = false;
    float m_grappleT = 0.f, m_grappleDur = 0.3f, m_grappleFromX = 0, m_grappleFromY = 0, m_grappleFromZ = 0, m_grappleX = 0, m_grappleY = 0, m_grappleZ = 0;
    float m_medAcc = 0.f;
    float m_killerX = 0.f, m_killerY = 0.f, m_killerZ = 0.f;   // where the fatal shot came from (kill cam)
    // -- Kill cam (killcam.cpp) ---------------------------------------------------------
    struct KillcamFrame {
        Player player; float headZ = 0.f;
        std::vector<Enemy> enemies; std::vector<Bullet> bullets; std::vector<Tracer> tracers; std::vector<Actor> actors;
    };
    std::vector<KillcamFrame> m_kcFrames;
    int   m_kcHead = 0, m_kcCount = 0, m_kcStart = 0, m_kcLen = 0;
    bool  m_killcamActive = false, m_kcBody = false;
    float m_killcamT = 0.f, m_kcBodyYaw = 0.f;
    int   m_killerIdx = -1;            // enemy index that fired the fatal round; -1 world, -2 own explosive
    void  recordKillcamFrame();
    void  startKillcam();
    bool  killcamActive() const;
    void  updateKillcam(float dt);
    void  renderKillcam();
    void renderArmoryOverlay();

    struct HitResult {
        int enemy = -1; HitPart part = PART_NONE;
        float x = 0, y = 0, z = 0, depth = 1e9f;
        bool wall = false, floor = false, sky = false;
        int cx = 0, cy = 0, face = 0; float u = 0, v = 0;   // wall-face hit coordinates
    };
    HitResult hitscan(float ax, float ay, float startDist = 0.f) const;
    void muzzleObstruction(HitResult &h, float mx, float my, float mz) const;

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
    void renderLaser();                 // laser-sight attachment: beam + dot
    float m_laserX = 0, m_laserY = 0;   // laser emitter on screen (first person)
    bool  m_laserOn = false;

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
    void renderSquadTags();
    void renderTargetInfo();
    void renderKillFeed();
    void renderLevelBanner();
    void renderPauseMenu();
    void renderGameOver();

    // -- Colour helpers ------------------------------------------------------------------
    uint32_t fog(uint32_t c, float dist) const;
};
