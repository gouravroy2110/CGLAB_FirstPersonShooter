// =============================================================================
//  mainwindow.h
// =============================================================================
#pragma once
#include <QWidget>
#include <QHash>
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
#include "multiplayer.h"

struct Raster3D;
struct WorldCam;
class CvInput;

class MainWindow : public QWidget, public World {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    // Dev aid: render a scripted set of scenes to PNG files and return.
    bool snapshot(const QString &dir);
    // Open the sound device (skipped in snapshot/test mode)
    void enableAudio();
    // Player options (QSettings); not loaded in snapshot mode so tests use defaults
    void loadSettings();

    // Multiplayer public access
    QString m_mpInputAddr = "127.0.0.1:7777";
    QString m_mpPlayerName = "Operative";
    void openMultiplayerLobby();
    void hostMultiplayer();
    void joinMultiplayer();
    void startMultiplayerMatch();

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
    std::vector<uint32_t> m_sky[THEME_COUNT];
    MipTex                m_floorTex[THEME_COUNT];
    MipTex                m_wallTex[THEME_COUNT][6];
    MipTex                m_facadeTex[FACADE_STYLES];   // campus building storeys
    MipTex                m_facadeGround[FACADE_STYLES];   // ...and their ground floors
    std::vector<uint32_t> m_canopyTex[THEME_COUNT];
    std::vector<uint32_t> m_campusTreeTex[TREE_VARIANTS];
    std::vector<uint32_t> m_enemyAtlas, m_eliteAtlas, m_scoutAtlas, m_shotgunAtlas, m_sniperAtlas;
    std::vector<uint8_t>  m_enemyMask, m_eliteMask, m_scoutMask, m_shotgunMask, m_sniperMask;
    std::vector<uint32_t> m_pickupTex;
    std::vector<uint32_t> m_portrait[3];
    static const int PORTRAIT = 160;

    std::vector<uint8_t> m_sunVisCache[MAP_COUNT], m_lightCache[MAP_COUNT], m_sunCache[MAP_COUNT];   // pristine light layers, baked once
    // -- Environment (environment.cpp): time of day, weather, flashlight, trees ------------
    Theme m_env = THEMES[0];          // this frame's palette (biome + hour + weather)
    float m_hour = 12.f;              // time of day, 0..24
    bool  m_dayCycle = false;
    int   m_weather = 0;              // Weather
    float m_dayLevel = 1.f;           // 0 night .. 1 day
    float m_skyTint[3] = {1.f, 1.f, 1.f};
    float m_envTimer = 0.f, m_lightning = 0.f;
    bool  m_envDirty = true, m_envLocked = false;   // locked: a mission set the environment
    bool  m_flashlightOn = true;
    int   m_todSetting = 0, m_weatherSetting = 0;  // Settings: 0 = map default
    struct WxDrop { float x, y, z, vx, vy, vz; int seed; };
    std::vector<WxDrop> m_wx;
    std::vector<int> m_treeCells;
    void  setEnvironment(float hour, int weather);
    void  computeEnv();
    void  updateEnvironment(float dt);
    void  renderWeather();
    void  renderFlashlight();

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
    // -- Extended hostile roster (enemies.cpp) -------------------------------------------
    std::vector<uint32_t> m_typeAtlas[ET_COUNT];
    std::vector<uint8_t>  m_typeMask[ET_COUNT];
    const std::vector<uint32_t> &atlasFor(const Enemy &e) const;
    const std::vector<uint8_t>  &maskFor(const Enemy &e) const;
    const char *enemyTypeName(const Enemy &e) const;
    void  moveEnemy(Enemy &e, int idx, float vx, float vy, float spd, float dt);
    void  enemyGrenade(Enemy &e, float tx, float ty);
    void  updateSpecialEnemy(int idx, float dt, float tgX, float tgY, float tgZ, float d, bool los, float detectRange);
    EnemyType rollEnemyType(int lv, bool elevated) const;
    // -- Story bosses (bosses.cpp) --------------------------------------------------------
    const char *bossName(int kind) const;
    const char *bossTitle(int kind) const;
    bool  bossIsMinor(int kind) const;
    std::vector<uint32_t> m_bossAtlas[13];
    std::vector<uint8_t>  m_bossMask[13];
    void  bakeBossAtlases();
    const std::vector<uint32_t> *bossAtlas(const Enemy &e) const;
    const std::vector<uint8_t>  *bossMask(const Enemy &e) const;
    int   spawnBoss(int kind, float x, float y, float hpScale);
    void  spawnBossPair(int kindA, float ax, float ay, int kindB, float bx, float by);
    void  hurtPlayer(int dmg, float fromX, float fromY, int killer);
    void  bossShot(Enemy &e, float tx, float ty, float tz, float speed, int dmg, float spread);
    void  bossReinforce(Enemy &e, int count, EnemyType t);
    void  bossDamaged(int idx, int dmg);
    void  spawnDecoy(int idx);
    void  updateBoss(int idx, float dt, float tgX, float tgY, float tgZ, float d, bool los);
    void  updateBossWorld(float dt);
    void  renderBossFX();
    void  renderBossBar();
    void  spawnZoneBoss(int kind, float x, float y, float farX, float farY);
    float m_bossBanner = 0.f;
    float m_bossRunAX = 0, m_bossRunAY = 0, m_bossRunBX = 0, m_bossRunBY = 0;   // gunship strafing line
    float m_laserAngle = 0.f, m_laserHitT = 0.f;                              // VECTOR's laser walls
    float m_overloadT = 0.f, m_overloadPhase = 9.f;
    std::vector<int> m_overloadCells;
    // acid that grows from an egg sac (or a glob that dries up after a while)
    struct SeedSpread { int egg; std::vector<int> cells, frontier; float t; float life = -1.f; };
    std::vector<SeedSpread> m_seeds;
    std::vector<uint8_t> m_baseAcid;   // the map's own hazard cells (never receded)
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
    bool m_staminaExhausted = false;

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
    GameScreen m_settingsReturnScreen = SCR_MENU;
    bool  m_settingsWasPaused = false;
    float m_mouseSens = 1.f;
    int   m_fovDeg = 67;
    int   m_volume = 80;
    int   m_crossColor = 0;
    bool  m_showFps = true;
    float m_planeLen = 0.66f;           // camera plane length = tan(FOV / 2)
    int   m_settingsRow = 0;
    int   m_arsenalSel = WPN_RIFLE;
    enum KeyAction {
        ACT_MOVE_FWD = 0,
        ACT_MOVE_BACK,
        ACT_MOVE_LEFT,
        ACT_MOVE_RIGHT,
        ACT_JUMP,
        ACT_SPRINT,
        ACT_FIRE,
        ACT_ADS,
        ACT_RELOAD,
        ACT_MELEE,
        ACT_INSPECT,
        ACT_WEAPON_STATS,
        ACT_INTERACT,
        ACT_THROW,
        ACT_CYCLE_THROW,
        ACT_GADGET,
        ACT_FLASHLIGHT,
        ACT_CAMERA,
        ACT_SHOULDER,
        ACT_ARMORY,
        ACT_SLOT1,
        ACT_SLOT2,
        ACT_SLOT3,
        ACT_SLOT4,
        ACT_CROUCH,
        ACT_LEAN,
        ACT_COUNT
    };
    int   m_keyBinds[ACT_COUNT];
    int   m_settingsTab = 0;          // 0 = General, 1 = Controls
    int   m_controlsRow = 0;          // 0..ACT_COUNT (ACT_COUNT is reset button)
    int   m_rebindingAction = -1;     // -1 if none, or 0..ACT_COUNT-1
    bool  m_showWeaponStats = false;
    void  initDefaultKeyBinds();
    bool  isActionPressed(KeyAction act) const;
    bool  isPlayerMoving() const;
    static QString keyName(int k);
    void  openSettings(GameScreen fromScreen);
    void  saveSettings() const;
    void  applySettings();
    int   settingsRows() const;
    void  changeSetting(int row, int dir);
    uint32_t crosshairColor() const;
    void  renderSettings();
    void  renderArsenal();
    // -- Map editor (editor.cpp) ---------------------------------------------------------
    std::vector<std::string> m_layoutRows[MAP_COUNT];   // the rows each map is parsed from
    std::vector<std::string> m_edRows;
    int   m_edMap = 0, m_edBrush = 1, m_edZoom = 12, m_edPanX = 0, m_edPanY = 0;
    bool  m_edFill = false, m_edDirty = false, m_edPlaytest = false;
    char  m_edDigit = '1';
    QString m_edMsg; float m_edMsgT = 0.f;
    void  loadLayoutRows(bool overrides);
    void  applyLayoutRows(int map);
    QString validateRows(const std::vector<std::string> &rows) const;
    void  openEditor(int map);
    bool  editorCellAt(float fx, float fy, int &cx, int &cy) const;
    void  editorPaint(int cx, int cy, bool erase);
    void  editorSave();
    void  editorPlaytest();
    void  editorKey(QKeyEvent *e);
    void  editorMouse(const QPointF &f, Qt::MouseButtons buttons);
    void  renderEditor();
    // -- Campaign (campaign.cpp) ---------------------------------------------------------
    GameMode m_gameMode = MODE_FREEPLAY;   // current game mode: Free Play, Campaign, or Multiplayer
    bool  m_campaignRun = false;        // playing a story mission (not free play)
    void  openCampaign();
    int   maxHP() const;
    float maxStamina() const;
    float reloadMul() const;
    // save
    int   m_cUnlocked = 1, m_cCoins = 0, m_cSel = 0, m_cRank[16] = {}, m_cBest[16] = {}, m_upg[4] = {};
    QString m_cInventory;
    void  loadCampaign();
    void  saveCampaign() const;
    QString packInventory(const Player &pl) const;
    void  unpackInventory(const QString &s, Player &pl);
    // armoury: weapons and attachment options unlocked by missions, chosen in the loadout
    uint32_t m_cWeapUnlock = 0;
    uint8_t  m_cAttUnlock[AT_COUNT] = {};
    uint8_t  m_cWeapRarity[WPN_COUNT] = {};
    QStringList m_debriefUnlocks;
    Player   m_loKit;                                     // the kit being edited in the loadout screen
    int      m_loSlot = 2, m_loFocus = 0, m_loAtt = 0;   // loadout cursor
    bool  weaponUnlocked(int w) const { return w >= 0 && w < WPN_COUNT && (m_cWeapUnlock >> w) & 1u; }
    bool  attUnlocked(int slot, int opt) const { return opt == 0 || (slot >= 0 && slot < AT_COUNT && ((m_cAttUnlock[slot] >> opt) & 1u)); }
    void  grantMissionUnlocks(int mission, int rank);
    void  defaultKit(Player &pl) const;
    void  openLoadout();
    void  closeLoadout();
    void  renderLoadout();
    void  loadoutKey(int k);
    void  loadoutClick(int id);
    void  cycleSlotWeapon(int slot, int dir);
    void  cycleAttachment(int att, int dir);
    void  drawGunStage(int w, int tx, int ty, int tw, int th, float ang);
    // lifetime stats (stats.cpp)
    struct LifetimeStats {
        int kills = 0, headshots = 0, deaths = 0, shots = 0, hits = 0, missionsWon = 0, bosses = 0, secrets = 0, zones = 0;
        int bestStreak = 0, bestScore = 0;
        float longestKill = 0.f;
        double seconds = 0.0;
        int wKills[WPN_COUNT] = {}, wShots[WPN_COUNT] = {}, wHits[WPN_COUNT] = {};
    };
    LifetimeStats m_stats;
    int   m_statsShotsSeen = 0, m_statsHitsSeen = 0, m_statsSecretsSeen = 0;
    bool  m_statsPrevOver = false, m_statsPrevZone = false;
    float m_statsSaveT = 0.f;
    void  loadStats();
    void  saveStats() const;
    void  trackStats(float dt);                           // per frame: shots / hits / secrets / deaths / time
    void  statKill(bool head, float dist, bool boss);
    void  renderStats();
    void  openStats();

    // menu / briefing / debrief
    void  renderCampaign();
    void  buyCampaignUpgrade(int u);
    void  campaignKey(int k);
    void  campaignClick(int id);
    void  renderBriefing();
    void  renderDebrief();
    float m_briefT = 0.f, m_debriefT = 0.f;
    std::vector<uint32_t> m_briefView;
    struct DebriefRow { QString label, detail; bool ok; };
    std::vector<DebriefRow> m_debriefRows;
    bool  m_debriefWon = false; int m_debriefRank = 0, m_debriefCoins = 0, m_debriefScore = 0;
    // radio
    static const int PORT = 64;
    std::vector<uint32_t> m_speakerPortrait[5];
    struct RadioMsg { int speaker; QString text; float dur; float t = -1.f; int prio = 2; bool started = false; RadioMsg(int s, const QString &x, float d) : speaker(s), text(x), dur(d), t(d) {} };
    std::vector<RadioMsg> m_radio;
    void  bakePortraits();
    void  say(int speaker, const QString &line, float seconds);
    void  sayTagged(const char *tagged);
    void  updateRadio(float dt);
    void  renderRadio();
    // radio commander + spoken voices (commander.cpp, radiolines.h)
    bool  m_radioVoice = true;                         // speak radio lines (Settings)
    bool  m_voicesInit = false;
    float m_cmdCool[32] = {};
    int   m_cmdLast[32] = {};
    float m_cmdGap = 99.f, m_cmdZoneStartT = 0.f, m_cmdIdleT = 0.f, m_cmdPrevAlarm = 0.f;
    bool  m_cmdWasLowHp = false, m_cmdHitBehind = false, m_cmdLongShot = false, m_cmdSecret = false;
    bool  m_cmdZoneStart = false, m_cmdRespawn = false, m_cmdPrevZoneDone = false;
    int   m_cmdPrevAlive = -1, m_cmdPrevCombo = 0, m_cmdAmmoWarnW = -1, m_cmdIdleKills = 0, m_cmdFired = 0;
    std::vector<int> m_cmdBossPhase;
    std::vector<uint8_t> m_cmdActorAlive;
    struct VoiceJob { int speaker; QString text; QString key; };
    QString m_voiceDir;
    std::vector<VoiceJob> m_voicePending;
    QHash<QString, int> m_voiceClips;
    class QProcess *m_voiceProc = nullptr;
    void  radioEvent(int ev, const QString &arg = QString());
    void  updateCommander(float dt);
    void  initVoices();
    void  queueVoice(int speaker, const QString &text);
    void  startVoiceJob();
    int   voiceClip(int speaker, const QString &text);
    QString voiceKey(int speaker, const QString &text) const;
    QString radioName(const QString &s) const;
    // mission runtime
    int   m_mission = 0, m_obj = -1, m_objCount = 0, m_objMask = 0, m_objZone = -1, m_waveCount = 0, m_alarms = 0;
    int   m_intelCount = 0, m_keyCount = 0, m_escort = -1, m_objKillBase = 0, m_squadSizeSaved = 0;
    float m_objTimer = 0.f, m_waveT = 0.f, m_missionTime = 0.f, m_alarm = 0.f, m_alarmCooldown = 0.f, m_missionEndT = 0.f, m_bossIntroT = 0.f, m_finaleT = 0.f;
    bool  m_missionDone = false, m_missionFailed = false, m_bossSpawned = false;
    QString m_failReason;
    bool  missionMarker(char c, float &x, float &y, int nth = 0) const;
    void  startMission(int i);
    void  nextObjective();
    EnemyType missionEnemyType() const;
    void  spawnMissionWave(int n);
    void  updateMission(float dt);
    bool  objectiveTarget(float &x, float &y) const;
    QString objectiveLine() const;
    void  renderMissionHUD();
    void  renderMissionWorld();
    void  finishMission(bool won);
    // camera: flyby along a spline
    struct FlyPt { float x, y, z; };
    std::vector<FlyPt> m_flyPts;
    float m_flyT = 0.f, m_flyDur = 1.f;
    bool  m_flyActive = false, m_flyForTest = false;
    void  startFlyby();
    void  flybyPoint(float u, float &x, float &y, float &z) const;
    void  renderFlyby();
    void  renderWorldFrom(float cx, float cy, float cz, float dx, float dy, float slope);

    // -- Feedback ------------------------------------------------------------------
    float m_hitMarker = 0, m_headMarker = 0, m_killMarker = 0, m_headshotBanner = 0, m_secretBanner = 0, m_acidFlash = 0;
    const char *m_curHazardName = nullptr;
    uint32_t    m_curHazardColor = 0;
    const char *hazardNameAt(int cx, int cy) const;
    uint32_t    hazardColorAt(int cx, int cy) const;
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
    float m_secretDustT = 0.f;
    std::vector<int> m_secretHinted;     // secret cells that already whispered this zone
    void  updateSecretHints(float dt);
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

    // -- CV MODE: webcam controls (cvcontrols.cpp, cvinput.h) ------------------------------
    struct CvCalib { bool ok = false; float t = 0.f; int n = 0; float x = 0, y = 0, yaw = 0, pitch = 0, roll = 0, size = 0; };
    CvInput *m_cvIn = nullptr;
    bool     m_cvMode = false;
    CvCalib  m_cvCal;
    float    m_cvLean = 0.f, m_cvYawApplied = 0.f, m_cvPitchApplied = 0.f;
    bool     m_cvCrouch = false, m_cvAds = false, m_cvFiring = false, m_cvJaw = false, m_cvGestDone = false;
    QString  m_cvGesture = "None", m_cvCandidate = "None";
    float    m_cvCandT = 0.f, m_cvGestT = 0.f;
    void startCvMode();
    void stopCvMode();
    void resetCvState();
    void updateCvControls(float dt);
    void cvGestureChanged(const QString &from, const QString &to);
    void renderCvPanel();
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
    void  damageActor(int i, int dmg, float fromX, float fromY, int shooterId = -1, bool head = false);
    void spawnBlood(float wx, float wy, float wz, int count, float force);
    void spawnImpact(float wx, float wy, float wz, bool floor);
    void tryOpenDoor();
    void updateDoors(float dt);
    void spawnPickup(float x, float y);
    bool findEmergencyDropPos(float &outX, float &outY) const;
    bool findHotZoneDropPos(float &outX, float &outY) const;
    void checkEmergencyDrops(float dt);
    int  enemyFrame(const Enemy &e) const;
    // 3D soldier bodies for regular hostiles (enemies.cpp / soldier3d.h)
    bool soldierEnemy(const Enemy &e) const;              // drawn as a 3D soldier (not a boss or a machine)
    bool enemyPose(int idx, struct Soldier3D &sp) const;
    void updateEnemyAnim(float dt);                       // facing, hit flashes, loose helmets
    bool updateEnemyTactics(int idx, float dt, float tgX, float tgY, float tgZ, float d, bool los, float &vx, float &vy, float &spd, bool &mayFire);
    bool gridStep(float x, float y, float gx, float gy, float &wx, float &wy) const;   // BFS waypoint towards a goal
    bool findCover(int idx, float tgX, float tgY, float tgZ, float &cx, float &cy);   // also records the peek spot
    bool findFlank(int idx, float tgX, float tgY, float tgZ, float &fx, float &fy) const;
    std::vector<uint16_t> m_shadowStamp;                  // stencil for planar shadows
    std::vector<uint8_t>  m_modelMask;                    // pixels covered by 3D bodies this frame (edge smoothing)
    void smoothModelEdges(int x0, int y0, int x1, int y1);
    // Crouch (Ctrl) and lean (hold Alt + A / D)
    bool  m_crouch = false;
    float m_crouchT = 0.f;                                // 0 standing .. 1 crouched (animated)
    float m_leanT = 0.f;                                  // -1 left .. 1 right (animated)
    static constexpr int CTRL_ROWS = (ACT_COUNT + 1) / 2;  // controls list: rows per column
    float playerAimZ() const { return m_gs.player.footZ + 0.5f - 0.18f * m_crouchT; }   // where hostiles aim / look
    uint16_t m_shadowId = 0;
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
        int enemy = -1; int actor = -1; HitPart part = PART_NONE;
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
    void renderWeaponStatsBox();
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

    // -- Multiplayer (Sector War) --------------------------------------------------------
    NetworkManager *m_net = nullptr;
    bool    m_mpTypingAddr = false;
    bool    m_mpTypingName = false;
    bool    m_showScoreboard = false;
    float   m_mpRespawnCountdown = -1.f;
    int     m_mpCoins = 0;              // kill coins: spent in the respawn armoury
    int     m_mpBuy = 0;                // armoury pick for the next life (MP_ARMOURY index)
    QString m_mpKilledBy;               // who got us last
    QString m_mpBonusText;              // "+100 KILL  +50 COINS"
    float   m_mpBonusT = 0.f;
    void onMatchEvent(NetEventType type, const QVariantMap &d);
    void applyMpArmoury();
    bool actorPose(const Actor &a, struct Soldier3D &sp) const;
    void renderMultiplayerLobby();
    void renderMultiplayerHUD();
    void renderScoreboard();
};
