// =============================================================================
//  mainwindow.h  —  FPS Raycaster  |  Week 1
// =============================================================================
#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
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
    uint32_t *m_pixels;   // raw pointer into m_buf.bits()
    float     m_zBuf[W];  // per-column wall depth (for sprite occlusion)

    // ── Loop ─────────────────────────────────────────────────────────────────
    QTimer       m_timer;
    QElapsedTimer m_clock;
    double       m_lastMs = 0;
    double       m_fps    = 60;

    // ── Input ────────────────────────────────────────────────────────────────
    std::set<int> m_keys;

    // ── Game ─────────────────────────────────────────────────────────────────
    GameState m_gs;

    // ── Logic ────────────────────────────────────────────────────────────────
    void initLevel();
    void gameStep(float dt);
    void fireBullet();
    bool posValid(float x, float y) const;

    // ── Renderer ─────────────────────────────────────────────────────────────
    void render();
    void renderFloorCeiling();          // full-screen floor/ceiling pass
    void renderWalls();                 // DDA raycaster → fills m_zBuf
    void renderSprites();               // billboard sprites (enemies + bullets)
    void renderWeapon();                // first-person gun at screen bottom
    void renderHUD();                   // QPainter text overlay

    // Pixel-level helpers
    inline void  px(int x, int y, uint32_t c);
    void fillRect(int x, int y, int w, int h, uint32_t c);
    void fillCircle(int cx, int cy, int r, uint32_t c);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t c);

    // Color helpers
    static uint32_t fogBlend(uint32_t col, float dist);
    static uint32_t wallPixel(int type, float wx, float wy, int side, float dist);
    static uint32_t sampleImp(float u, float v, bool flash);
    static uint32_t sampleBullet(float u, float v);
};
