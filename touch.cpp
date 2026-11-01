// =============================================================================
//  touch.cpp  -  Phones and tablets: on-screen controls
//
//  On Android / iOS (or on the desktop with --touch) the game is played with
//  fingers. Every touch is given a role when it lands:
//    a button          ->  that action (fire also turns the view while held)
//    the left side     ->  a floating joystick: walk, push to the rim to sprint
//    anywhere else     ->  drag to look
//  Movement and stance go through isActionPressed() and the movement command,
//  so touch drives exactly the same code as the keyboard. Menus are tapped:
//  a touch that isn't taken here becomes a mouse click (Qt synthesises it).
//  The controls are drawn into the framebuffer like the rest of the HUD.
// =============================================================================
#include "mainwindow.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static const uint32_t T_AMBER = 0xFFFFB43C;

enum TouchButton { TB_FIRE, TB_ADS, TB_RELOAD, TB_JUMP, TB_CROUCH, TB_GRENADE, TB_WEAPON, TB_USE, TB_PAUSE, TB_COUNT };
// x is measured from the frame's right edge, so the cluster sits under the right thumb at any width;
// everything stays above y = 480, clear of the weapon strip, ammo and throwable panels
struct TouchButtonDef { float fromRight, y, r; const char *label; };
static const TouchButtonDef TBUT[TB_COUNT] = {
    {92, 404, 54, "FIRE"},
    {196, 452, 30, "AIM"},
    {196, 372, 27, "RELOAD"},
    {50, 286, 29, "JUMP"},
    {128, 286, 27, "CROUCH"},
    {272, 444, 27, "GRENADE"},
    {50, 206, 25, "WEAPON"},
    {272, 370, 25, "USE"},
    {28, 74, 18, "II"},
};
static inline float tbX(int b) { return float(W) - TBUT[b].fromRight; }
static const float STICK_R = 62.f;                   // joystick throw, frame pixels
static const float LOOK_GAIN = 1.5f;                 // a finger's drag -> mouse units

enum TouchRole { TR_NONE, TR_STICK, TR_LOOK, TR_BUTTON };

bool MainWindow::touchLive() const
{
    return m_screen == SCR_PLAYING && !m_paused && !m_gs.gameOver && !m_gs.levelComplete && !m_showArmory && !m_killcamActive;
}

// Next weapon that's carried (the [1]-[4] keys, as one button)
static int nextCarriedSlot(const Player &pl)
{
    int cur = 0;
    for (int s = 0; s < SLOT_COUNT; s++) if (pl.slot[s] == pl.curWeapon) cur = s;
    for (int k = 1; k <= SLOT_COUNT; k++) { int s = (cur + k) % SLOT_COUNT; if (pl.slot[s] >= 0) return s; }
    return cur;
}

void MainWindow::touchDown(int id, QPointF f)
{
    Player &pl = m_gs.player;
    TouchPt t; t.start = t.last = f;
    for (int b = 0; b < TB_COUNT; b++) {
        float dx = float(f.x()) - tbX(b), dy = float(f.y()) - TBUT[b].y;
        if (dx * dx + dy * dy > TBUT[b].r * TBUT[b].r * 1.3f) continue;
        t.role = TR_BUTTON; t.btn = b;
        switch (b) {
        case TB_FIRE:    m_lmb = true; tryFire(); break;
        case TB_ADS:     pl.adsMode = !pl.adsMode; break;
        case TB_RELOAD:  startReload(); break;
        case TB_JUMP:    if (pl.onGround) m_jumpQueued = true; break;
        case TB_CROUCH:  m_touchCrouch = !m_touchCrouch; break;
        case TB_GRENADE: m_touchThrow = true; startThrow(); break;
        case TB_WEAPON:  selectSlot(nextCarriedSlot(pl)); break;
        case TB_USE:     if (!trySwapPickup()) tryOpenDoor(); break;
        case TB_PAUSE:   m_paused = true; pl.adsMode = false; m_lmb = false; sfx(SND_UI_SELECT, 0.5f); break;
        }
        m_touches[id] = t;
        return;
    }
    bool stickTaken = false;
    for (const auto &kv : m_touches) if (kv.second.role == TR_STICK) stickTaken = true;
    if (!stickTaken && f.x() < W * 0.45f && f.y() > 150.f) {
        t.role = TR_STICK;
        m_stickBase = f; m_stickOn = true;
    } else t.role = TR_LOOK;
    m_touches[id] = t;
}

void MainWindow::touchMove(int id, QPointF f)
{
    auto it = m_touches.find(id);
    if (it == m_touches.end()) return;
    TouchPt &t = it->second;
    if (t.role == TR_STICK) {
        float dx = float(f.x() - m_stickBase.x()), dy = float(f.y() - m_stickBase.y());
        float d = std::sqrt(dx * dx + dy * dy);
        if (d > STICK_R * 1.35f) {                                   // the base follows a thumb that runs away
            float k = (d - STICK_R * 1.35f) / d;
            m_stickBase += QPointF(dx * k, dy * k);
            dx -= dx * k; dy -= dy * k; d = STICK_R * 1.35f;
        }
        float nx = dx / STICK_R, ny = dy / STICK_R, n = std::min(1.f, d / STICK_R);
        if (d > 1e-3f) { nx = dx / d * n; ny = dy / d * n; }
        m_touchMoveX = nx; m_touchMoveY = ny;
        m_touchSprint = d > STICK_R * 1.1f && ny < -0.7f;              // pushed past the rim, forwards: sprint
    } else if (t.role == TR_LOOK || (t.role == TR_BUTTON && t.btn == TB_FIRE)) {
        float dx = float(f.x() - t.last.x()), dy = float(f.y() - t.last.y());
        applyAimDelta(dx * LOOK_GAIN, dy * LOOK_GAIN);
    }
    t.last = f;
}

void MainWindow::touchUp(int id)
{
    auto it = m_touches.find(id);
    if (it == m_touches.end()) return;
    const TouchPt &t = it->second;
    if (t.role == TR_STICK) { m_stickOn = false; m_touchMoveX = m_touchMoveY = 0.f; m_touchSprint = false; }
    if (t.role == TR_BUTTON && t.btn == TB_FIRE) { m_lmb = false; releaseTrigger(); }
    if (t.role == TR_BUTTON && t.btn == TB_GRENADE) m_touchThrow = false;   // updateThrow lets it fly
    m_touches.erase(it);
}

void MainWindow::touchReset()
{
    std::vector<int> ids;
    for (const auto &kv : m_touches) ids.push_back(kv.first);
    for (int id : ids) touchUp(id);
    m_stickOn = false; m_touchMoveX = m_touchMoveY = 0.f; m_touchSprint = false; m_touchThrow = false;
}

// Is this action held by a finger? (movement digitally; the analog stick goes into the move command)
bool MainWindow::touchAction(KeyAction act) const
{
    switch (act) {
    case ACT_MOVE_FWD:   return m_touchMoveY < -0.3f;
    case ACT_MOVE_BACK:  return m_touchMoveY > 0.3f;
    case ACT_MOVE_LEFT:  return m_touchMoveX < -0.3f;
    case ACT_MOVE_RIGHT: return m_touchMoveX > 0.3f;
    case ACT_SPRINT:     return m_touchSprint;
    case ACT_CROUCH:     return m_touchCrouch;
    default:             return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing: translucent buttons and the joystick, over the HUD
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderTouchControls()
{
    const Player &pl = m_gs.player;
    bool held[TB_COUNT] = {};
    for (const auto &kv : m_touches) if (kv.second.role == TR_BUTTON) held[kv.second.btn] = true;
    for (int b = 0; b < TB_COUNT; b++) {
        const TouchButtonDef &d = TBUT[b];
        bool on = held[b] || (b == TB_ADS && pl.adsMode) || (b == TB_CROUCH && m_touchCrouch) || (b == TB_GRENADE && m_cooking);
        int cx = int(tbX(b)), cy = int(d.y), r = int(d.r);
        m_cv.fillCircle(cx, cy, r, withAlpha(on ? T_AMBER : 0xFF101418, on ? 120 : 90));
        m_cv.ring(cx, cy, d.r - 1.5f, d.r, withAlpha(on ? T_AMBER : 0xFFFFFFFF, on ? 230 : 120));
        int fs = b == TB_FIRE ? 14 : b == TB_PAUSE ? 10 : 8;
        text(cx - r, cy - 8, 2 * r, 16, Qt::AlignCenter, d.label, on ? QColor(255, 240, 200) : QColor(235, 235, 235, 220), fs, 900, false);
    }
    if (int left = pl.throwCount[pl.throwSel]) {                         // grenades left
        const TouchButtonDef &g = TBUT[TB_GRENADE];
        text(int(tbX(TB_GRENADE)) - 20, int(g.y) + int(g.r) - 2, 40, 12, Qt::AlignCenter, QString("x%1").arg(left), QColor(200, 200, 200), 8, 700, false);
    }
    // the joystick: a ring where the thumb came down, the knob where it is
    if (m_stickOn) {
        int bx = int(m_stickBase.x()), by = int(m_stickBase.y());
        m_cv.fillCircle(bx, by, int(STICK_R), withAlpha(0xFF101418, 70));
        m_cv.ring(bx, by, STICK_R - 2.f, STICK_R, withAlpha(0xFFFFFFFF, 120));
        int kx = bx + int(m_touchMoveX * STICK_R), ky = by + int(m_touchMoveY * STICK_R);
        m_cv.fillCircle(kx, ky, 24, withAlpha(m_touchSprint ? T_AMBER : 0xFFFFFFFF, 150));
    } else {
        text(24, H - 150, 200, 14, Qt::AlignLeft, "LEFT THUMB: MOVE", QColor(200, 200, 200, 120), 9, 700, false);
    }
}

// The 4:3 frame letterboxed in a window of any shape (phones are ~20:9)
QRect MainWindow::frameViewport(int ww, int wh) const
{
    if (!m_touchUI) return QRect(0, 0, ww, wh);                     // the desktop window stretches, as it always has
    // a phone's rounded corners and its camera cut-out sit at the short edges in landscape:
    // keep 3% clear on each side so no HUD text lands under them
    float s = std::min(float(ww) * (1.f - 2.f * PHONE_SIDE_MARGIN) / W, float(wh) / H);
    int vw = std::max(1, int(W * s)), vh = std::max(1, int(H * s));
    return QRect((ww - vw) / 2, (wh - vh) / 2, vw, vh);
}
