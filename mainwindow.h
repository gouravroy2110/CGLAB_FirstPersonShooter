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
#include <QPainter>
#include <QApplication>
#include <set>
#include <vector>
#include <QString>
#include "game.h"

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *)      override;
    void keyPressEvent(QKeyEvent *)     override;
    void keyReleaseEvent(QKeyEvent *)   override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent *)  override;
    void wheelEvent(QWheelEvent *)      override;

private slots:
    void tick();

private:
    // ── Framebuffer ──────────────────────────────────────────────────────────
    QImage    m_buf;
    uint32_t *m_pixels;
    float     m_zBuf[W];
    int       m_map[MAP_H][MAP_W];

    // Precomputed assets for high FPS
    uint32_t  m_skybox[1024 * 300];
    uint32_t  m_textures[5][64*64];
    uint8_t   m_vignette[H][W];

    // ── Zoom / pixel-scale (raster proof for professor) ───────────────────────
    float m_zoomFactor = 1.f;
    int   m_pixelScale = 1;

    // ── ADS smooth lerp ───────────────────────────────────────────────────────
    float m_adsFOVMult = 1.f;
    // Jump state
    float m_jumpZ    = 0.f;
    float m_jumpVelZ = 0.f;
    // Casing ejection
    float m_casingTimer = 0.f;
    float m_casingX = 0.f, m_casingY = 0.f;
    float m_casingVX = 0.f, m_casingVY = 0.f;

    // ── Loop ─────────────────────────────────────────────────────────────────
    QTimer        m_timer;
    QElapsedTimer m_clock;
    double        m_lastMs = 0;
    double        m_fps    = 60;
    float         m_dt     = 0.016f;
    int           m_menuFrame = 0;

    // ── Input ────────────────────────────────────────────────────────────────
    std::set<int> m_keys;

    // ── UI state ─────────────────────────────────────────────────────────────
    GameScreen m_screen     = SCR_MENU;
    bool       m_paused     = false;
    int        m_mapChoice  = 0;
    int        m_charChoice = 0;
    int        m_difficulty = 1;   // 0=Easy 1=Medium 2=Hard
    int        m_menuCursor = 0;

    // ── Visual feedback ───────────────────────────────────────────────────────
    float m_hitMarker      = 0.f;
    float m_damageAngle    = 0.f;
    float m_damageDirTimer = 0.f;
    int   m_combo          = 0;
    float m_comboTimer     = 0.f;
    float m_comboDisplay   = 0.f;

    // Enemy HP display timers (one per slot, max 12 enemies)
    float m_enemyHPDisplay[12] = {};

    struct KFEntry { QString text; float life; };
    std::vector<KFEntry> m_killFeed;

    // ── Game ─────────────────────────────────────────────────────────────────
    GameState m_gs;

    // ── Logic ────────────────────────────────────────────────────────────────
    void initGame();
    void loadLevel(int lv);
    void gameStep(float dt);
    void fireBullet();
    void enemyFire(Enemy &e);
    void spawnBlood(float wx, float wy, int count);
    bool posValid(float x, float y) const;
    bool hasLOS(float x0, float y0, float x1, float y1) const;
    void tryOpenDoor();
    void spawnPickup(float x, float y);
    void renderPickupSprites();

    // ── Screens ───────────────────────────────────────────────────────────────
    void drawMenuBg();
    void renderMenu();
    void renderMapSelect();
    void renderDiffSelect();
    void renderCharSelect();

    // ── Game pipeline ─────────────────────────────────────────────────────────
    void render();
    void renderFloorCeiling();   // sky+clouds ceiling, perspective floor
    void renderWalls();
    void renderSprites();        // enemies + bullets + medkits + enemy HP bars
    void renderParticles();
    void renderWeaponAndArms();  // high-quality gun + tactical arms
    void renderDamageVignette();
    void renderHUD();
    void renderLevelBanner();
    void renderPauseMenu();

    // ── HUD sub-renderers ─────────────────────────────────────────────────────
    void renderTopBar();
    void renderRadar();
    void renderHealthPanel();
    void renderAmmoPanel();
    void renderCrosshair();
    void renderDamageDirection();
    void renderTargetBar();
    void renderComboDisplay();
    void renderKillFeed();

    // ── Pixel helpers (pure raster — writes directly to m_pixels) ────────────
    inline void  px(int x, int y, uint32_t c);
    void fillRect(int x, int y, int w, int h, uint32_t c);
    void fillCircle(int cx, int cy, int r, uint32_t c);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t c);

    // ── Color helpers ─────────────────────────────────────────────────────────
    static uint32_t fogBlend(uint32_t col, float dist);
    static uint32_t dimColor(uint32_t col, float bright);
    static uint32_t wallPixel(int type, float wx, float wy, int side, float dist);
    static uint32_t sampleImp(float u, float v, bool flash);
    static uint32_t samplePlayerBullet(float u, float v);
    static uint32_t sampleEnemyBullet(float u, float v);
    static uint32_t sampleMedkit(float u, float v);
};
