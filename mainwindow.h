// =============================================================================
//  mainwindow.h  —  FPS Raycaster  |  Week 1+
// =============================================================================
#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
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
    void mouseMoveEvent(QMouseEvent *) override;

private slots:
    void tick();

private:
    // ── Framebuffer ──────────────────────────────────────────────────────────
    QImage    m_buf;
    uint32_t *m_pixels;
    float     m_zBuf[W];

    // ── Loop ─────────────────────────────────────────────────────────────────
    QTimer        m_timer;
    QElapsedTimer m_clock;
    double        m_lastMs = 0;
    double        m_fps    = 60;
    float         m_dt     = 0.016f;  // last frame delta (for bullet trails)

    // ── Input ────────────────────────────────────────────────────────────────
    std::set<int> m_keys;

    // ── State ────────────────────────────────────────────────────────────────
    GameState m_gs;
    bool      m_paused = false;

    // ── Logic ────────────────────────────────────────────────────────────────
    void initGame();
    void loadLevel(int lv);
    void gameStep(float dt);
    void fireBullet();
    void enemyFire(Enemy &e);
    bool posValid(float x, float y) const;
    bool hasLOS(float x0, float y0, float x1, float y1) const;

    // ── Renderer ─────────────────────────────────────────────────────────────
    void render();
    void renderFloorCeiling();
    void renderWalls();
    void renderSprites();
    void renderWeaponAndArms();
    void renderDamageVignette();
    void renderHUD();
    void renderPauseMenu();
    void renderLevelBanner();

    // Pixel helpers (pure raster — all write directly to m_pixels)
    inline void  px(int x, int y, uint32_t c);
    void fillRect(int x, int y, int w, int h, uint32_t c);
    void fillCircle(int cx, int cy, int r, uint32_t c);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t c);

    // Color helpers
    static uint32_t fogBlend(uint32_t col, float dist);
    static uint32_t dimColor(uint32_t col, float bright);
    static uint32_t wallPixel(int type, float wx, float wy, int side, float dist);
    static uint32_t sampleImp(float u, float v, bool flash);
    static uint32_t samplePlayerBullet(float u, float v);
    static uint32_t sampleEnemyBullet(float u, float v);
};
