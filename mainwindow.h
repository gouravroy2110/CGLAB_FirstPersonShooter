// =============================================================================
//  mainwindow.h  —  FPS Raycaster  |  Full feature set
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
#include "game.h"

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *)     override;
    void keyPressEvent(QKeyEvent *)    override;
    void keyReleaseEvent(QKeyEvent *)  override;
    void mousePressEvent(QMouseEvent *)override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *)     override;

private slots:
    void tick();

private:
    // ── Framebuffer ──────────────────────────────────────────────────────────
    QImage    m_buf;
    uint32_t *m_pixels;
    float     m_zBuf[W];

    // ── Active map (mutable copy, selected per game start) ───────────────────
    int m_map[MAP_H][MAP_W];

    // ── Zoom & pixel-scale (professor raster proof) ───────────────────────────
    float m_zoomFactor = 1.f;  // window display scale  (scroll wheel)
    int   m_pixelScale = 1;    // render block size 1/2/4/8  ([ ] keys)

    // ── ADS smooth transition ─────────────────────────────────────────────────
    float m_adsFOVMult = 1.f;  // 1.0=normal, 0.5=zoomed in (lerps)

    // ── Loop ─────────────────────────────────────────────────────────────────
    QTimer        m_timer;
    QElapsedTimer m_clock;
    double        m_lastMs = 0;
    double        m_fps    = 60;
    float         m_dt     = 0.016f;
    int           m_menuFrame = 0; // animates menus even when not playing

    // ── Input ────────────────────────────────────────────────────────────────
    std::set<int> m_keys;

    // ── UI state ─────────────────────────────────────────────────────────────
    GameScreen m_screen     = SCR_MENU;
    bool       m_paused     = false;
    int        m_mapChoice  = 0;     // pending map selection
    int        m_charChoice = 0;     // pending character selection

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

    // ── Screens ───────────────────────────────────────────────────────────────
    void renderMenu();
    void renderMapSelect();
    void renderCharSelect();

    // ── Game renderers ────────────────────────────────────────────────────────
    void render();
    void renderFloorCeiling();
    void renderWalls();
    void renderSprites();        // enemies + bullets with trail
    void renderParticles();      // blood FX
    void renderWeaponAndArms();  // arms + ADS + muzzle flash
    void renderDamageVignette();
    void renderHUD();
    void renderPauseMenu();
    void renderLevelBanner();

    // ── Pixel helpers (pure raster — write directly to m_pixels) ─────────────
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
};
