// =============================================================================
//  cvcontrols.cpp  -  CV MODE: playing with your head and hands (webcam)
//
//  tools/cv_control.py (MediaPipe) measures the face and one hand every camera
//  frame; this file turns those measurements into the game's own actions:
//
//    head sideways / tilted     ->  peek (lean) left / right
//    head turned / nodded       ->  the view turns with it (on top of the mouse)
//    head dropped               ->  crouch
//    face closer to the screen  ->  aim down sights
//    1 / 2 / 3 / 4 fingers up   ->  weapon slot 1 / 2 / 3 / 4 (thumb tucked in)
//    finger gun (thumb up,      ->  fire (held: automatic fire)
//      index pointing)
//    fist (hold, then open)     ->  cook a grenade, throw it
//    open palm (hold)           ->  reload
//    the rock-on sign           ->  use: pick up / swap a gun, open a door
//    mouth wide open            ->  quick melee
//
//  Everything is relative to your neutral pose, measured during the first
//  second with a face in view ([K] measures it again). Gestures must be seen
//  for a few frames before they count, so a flicker doesn't fire anything.
// =============================================================================
#include "mainwindow.h"
#include "cvinput.h"
#include <cmath>
#include <algorithm>

using namespace rs;

static const uint32_t AMBER = 0xFFFFB43C, UI_BG = 0xFF0E1216, GREEN = 0xFF5AE68C;   // the HUD's palette
static inline QColor qc(uint32_t c, int a = 255) { return QColor(R(c), G(c), B(c), a); }

static const int   CV_MAP = 4;              // ASHEN CITY: city blocks to peek around
static const float CV_CAL_TIME = 1.0f;      // seconds of a still face for the neutral pose
static const float CV_GESTURE_HOLD = 0.12f; // a gesture must be seen this long to count
static const float CV_GESTURE_SCORE = 0.55f;

void MainWindow::startCvMode()
{
    m_gameMode = MODE_FREEPLAY; m_campaignRun = false;
    if (m_net) m_net->disconnectSession();
    m_mapChoice = CV_MAP; m_charChoice = 0; m_difficulty = 0;   // easy: the body is a new controller
    initGame();
    m_screen = SCR_PLAYING;                                      // initGame leaves the screen to its caller
    if (!m_cvIn) m_cvIn = new CvInput(this);
    m_cvIn->start();
    m_cvMode = true;
    resetCvState();
    pushFeed("CV MODE  -  FACE THE CAMERA AND HOLD STILL", 4.f, true);
}

void MainWindow::stopCvMode()
{
    if (m_cvIn) m_cvIn->stop();
    if (m_cvMode) { m_lmb = false; m_crouch = false; }
    m_cvMode = false;
    resetCvState();
}

void MainWindow::resetCvState()
{
    m_cvCal = CvCalib{};
    m_cvLean = 0.f; m_cvYawApplied = 0.f; m_cvPitchApplied = 0.f;
    m_cvCrouch = false; m_cvAds = false; m_cvFiring = false; m_cvJaw = false;
    m_cvGesture = "None"; m_cvCandidate = "None"; m_cvCandT = 0.f; m_cvGestT = 0.f; m_cvGestDone = false;
}

// The hand sign the camera sees, from MediaPipe's gesture and our own finger count
static QString cvSign(const CvFrame &f)
{
    if (!f.hand) return "None";
    if (f.gesture == "Closed_Fist" && f.gscore >= CV_GESTURE_SCORE) return "Closed_Fist";
    if (f.gesture == "ILoveYou" && f.gscore >= CV_GESTURE_SCORE) return "ILoveYou";
    if ((f.gesture == "Open_Palm" && f.gscore >= CV_GESTURE_SCORE) || (f.fingers == 4 && f.thumbOut)) return "Open_Palm";
    if (f.thumbOut && f.up[0] && !f.up[2] && !f.up[3]) return "Gun";          // the finger gun: thumb up, index (and middle) out
    if (!f.thumbOut && f.fingers >= 1 && f.fingers <= 4) return QString("Finger_%1").arg(f.fingers);
    return "None";
}

// The sign that just started (or stopped) being held
void MainWindow::cvGestureChanged(const QString &from, const QString &to)
{
    Player &pl = m_gs.player;
    if (from == "Closed_Fist" && m_cooking) releaseThrow();               // open the fist: throw
    if (from == "Gun" && m_cvFiring) { m_cvFiring = false; m_lmb = false; releaseTrigger(); }
    if (pl.hp <= 0) return;
    if (to == "Closed_Fist") startThrow();                                 // close it: pull the pin
    else if (to.startsWith("Finger_")) {                                   // count the slot on your fingers
        int s = to.mid(7).toInt() - 1;
        if (s >= 0 && s < SLOT_COUNT) {
            if (pl.slot[s] >= 0) selectSlot(s);
            else pushFeed(QString("SLOT %1 IS EMPTY").arg(s + 1), 1.2f);
        }
    } else if (to == "ILoveYou") { if (!trySwapPickup()) tryOpenDoor(); }
    else if (to == "Gun") { m_cvFiring = true; m_lmb = true; tryFire(); }      // bang
}

void MainWindow::updateCvControls(float dt)
{
    if (!m_cvIn) return;
    m_cvIn->tick(dt);
    const CvFrame &f = m_cvIn->frame();
    bool live = m_cvIn->age() < 0.6f && f.face;
    Player &pl = m_gs.player;

    // --- neutral pose: the first second with a face in view --------------------------------
    if (!m_cvCal.ok) {
        if (live) {
            m_cvCal.t += dt; m_cvCal.n++;
            m_cvCal.x += f.x; m_cvCal.y += f.y; m_cvCal.yaw += f.yaw; m_cvCal.pitch += f.pitch;
            m_cvCal.roll += f.roll; m_cvCal.size += f.size;
            if (m_cvCal.t >= CV_CAL_TIME) {
                float n = float(m_cvCal.n);
                m_cvCal.x /= n; m_cvCal.y /= n; m_cvCal.yaw /= n; m_cvCal.pitch /= n; m_cvCal.roll /= n; m_cvCal.size /= n;
                m_cvCal.ok = true;
                pushFeed("CV CALIBRATED  -  PEEK, LOOK, DUCK, LEAN IN; HANDS FOR GRENADE / RELOAD / WEAPON", 4.f);
                sfx(SND_UI_SELECT, 0.6f);
            }
        }
        return;
    }

    auto dead = [](float v, float dz, float range) {                       // dead zone, then 0..1 over `range`
        float a = std::fabs(v);
        return a <= dz ? 0.f : std::copysign(std::min(1.f, (a - dz) / range), v);
    };
    float k = std::min(1.f, dt * 10.f);

    // --- peek: move the head sideways or tilt it ---------------------------------------------
    float leanRaw = live ? (f.x - m_cvCal.x) * 5.f + (f.roll - m_cvCal.roll) / 22.f : 0.f;
    m_cvLean += (dead(leanRaw, 0.25f, 0.45f) - m_cvLean) * k;

    // --- look: the view follows the turned / nodded head (an offset that springs back) -----------
    float yawWant = live ? dead(f.yaw - m_cvCal.yaw, 0.06f, 0.35f) * 0.75f : 0.f;          // up to ~43 degrees
    float pitchWant = live ? -dead(f.pitch - m_cvCal.pitch, 0.06f, 0.3f) * 0.35f : 0.f;    // slope
    float yawStep = (yawWant - m_cvYawApplied) * k, pitchStep = (pitchWant - m_cvPitchApplied) * k;
    turn(yawStep); m_cvYawApplied += yawStep;
    m_pitch = clampf(m_pitch + pitchStep, -0.75f, 0.75f); m_cvPitchApplied += pitchStep;

    // --- crouch: drop the head; aim: lean in towards the screen ------------------------------------
    float drop = live ? f.y - m_cvCal.y : 0.f;
    m_cvCrouch = m_cvCrouch ? drop > 0.05f : drop > 0.08f;
    float closer = (live && m_cvCal.size > 0.f) ? f.size / m_cvCal.size : 1.f;
    bool adsWant = m_cvAds ? closer > 1.08f : closer > 1.16f;
    if (adsWant != m_cvAds) { m_cvAds = adsWant; pl.adsMode = adsWant; }

    // --- mouth open: quick melee ------------------------------------------------------------------
    bool jaw = live && f.jaw > 0.5f;
    if (jaw && !m_cvJaw && pl.hp > 0) quickMelee();
    m_cvJaw = jaw;

    // --- hand gestures: held for a few frames before they count -------------------------------------
    QString seen = m_cvIn->age() < 0.6f ? cvSign(f) : QString("None");
    if (seen != m_cvCandidate) { m_cvCandidate = seen; m_cvCandT = 0.f; }
    else m_cvCandT += dt;
    if (m_cvCandT >= CV_GESTURE_HOLD && m_cvCandidate != m_cvGesture) {
        QString from = m_cvGesture;
        m_cvGesture = m_cvCandidate; m_cvGestT = 0.f; m_cvGestDone = false;
        cvGestureChanged(from, m_cvGesture);
    }
    m_cvGestT += dt;
    if (m_cvGesture == "Open_Palm" && !m_cvGestDone && m_cvGestT > 0.35f && pl.hp > 0) { startReload(); m_cvGestDone = true; }
    if (m_cvFiring) m_lmb = true;                                          // pointing keeps the trigger down
}

// ─────────────────────────────────────────────────────────────────────────────
//  HUD: what the camera sees, and what it is doing to the game
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::renderCvPanel()
{
    if (!m_cvIn) return;
    const CvFrame &f = m_cvIn->frame();
    const int S = 3, TW = CvInput::THUMB_W, TH = CvInput::THUMB_H;          // thumbnail at 1.5x (S/2)
    const int pw = TW * S / 2 + 12, px = W - pw - 10, py = 64;
    const int ph = TH * S / 2 + 98;
    m_cv.blendRect(px, py, pw, ph, withAlpha(UI_BG, 215));
    m_cv.rectOutline(px, py, pw, ph, withAlpha(AMBER, 150), 1);
    const int ix = px + 6, iy = py + 18, iw = TW * S / 2, ih = TH * S / 2;
    text(px + 6, py + 2, pw - 12, 14, Qt::AlignLeft, "CV CONTROLS", qc(AMBER), 9, 900, false);
    bool live = m_cvIn->age() < 0.6f;
    text(px + 6, py + 2, pw - 12, 14, Qt::AlignRight, live ? QString("LIVE") : QString("NO SIGNAL"),
         live ? QColor(110, 220, 140) : QColor(255, 110, 90), 8, 800, false);
    // the camera, grey, mirrored like a mirror
    if (live && f.thumb.size() == size_t(TW * TH)) {
        for (int y = 0; y < ih; y++) for (int x = 0; x < iw; x++) {
            int g = f.thumb[size_t(y * 2 / S) * TW + size_t(x * 2 / S)];
            m_cv.fillRect(ix + x, iy + y, 1, 1, rgb(g, g, g));
        }
        if (f.face) {
            int bx0 = ix + int(f.box[0] * iw), by0 = iy + int(f.box[1] * ih), bx1 = ix + int(f.box[2] * iw), by1 = iy + int(f.box[3] * ih);
            m_cv.rectOutline(std::max(ix, bx0), std::max(iy, by0), std::min(ix + iw, bx1) - std::max(ix, bx0), std::min(iy + ih, by1) - std::max(iy, by0), GREEN, 1);
            m_cv.fillCircle(ix + int(f.x * iw), iy + int(f.y * ih), 2, GREEN);
            if (m_cvCal.ok) m_cv.fillCircle(ix + int(m_cvCal.x * iw), iy + int(m_cvCal.y * ih), 2, withAlpha(AMBER, 200));   // neutral
        }
        if (f.hand) m_cv.fillCircle(ix + int(f.hx * iw), iy + int(f.hy * ih), 4, withAlpha(rgb(80, 180, 255), 220));
    } else {
        m_cv.fillRect(ix, iy, iw, ih, rgb(16, 18, 22));
        QString why = !m_cvIn->status().isEmpty() ? m_cvIn->status() : QString("WAITING FOR THE CAMERA...");
        text(ix + 4, iy, iw - 8, ih, Qt::AlignCenter | Qt::TextWordWrap, why, QColor(220, 200, 160), 8, 700, false);
    }
    int ty = iy + ih + 4;
    if (!m_cvCal.ok) {
        text(px + 6, ty, pw - 12, 30, Qt::AlignCenter | Qt::TextWordWrap, f.face ? "HOLD STILL - CALIBRATING" : "FACE THE CAMERA",
             QColor(255, 220, 90), 9, 900, false);
        return;
    }
    // peek meter
    int mx = px + 6, mw = pw - 12;
    m_cv.fillRect(mx, ty + 2, mw, 6, rgb(40, 44, 50));
    int c = mx + mw / 2, l = int(m_cvLean * mw / 2);
    m_cv.fillRect(std::min(c, c + l), ty + 2, std::abs(l) + 1, 6, AMBER);
    m_cv.fillRect(c, ty, 1, 10, withAlpha(0xFFFFFFFF, 160));
    text(mx, ty + 9, mw, 12, Qt::AlignCenter, std::fabs(m_cvLean) > 0.05f ? (m_cvLean < 0 ? "PEEK LEFT" : "PEEK RIGHT") : "PEEK", QColor(200, 200, 200), 8, 700, false);
    // state chips
    auto chip = [&](int x, int y, const char *label, bool on) {
        m_cv.blendRect(x, y, 44, 14, on ? withAlpha(AMBER, 200) : withAlpha(0xFFFFFFFF, 20));
        text(x, y, 44, 14, Qt::AlignCenter, label, on ? QColor(20, 20, 20) : QColor(160, 160, 160), 8, 800, false);
    };
    int cy2 = ty + 24;
    chip(mx, cy2, "LOOK", std::fabs(m_cvYawApplied) > 0.03f || std::fabs(m_cvPitchApplied) > 0.02f);
    chip(mx + 48, cy2, "DUCK", m_cvCrouch);
    chip(mx + 96, cy2, "AIM", m_cvAds);
    // the hand
    static const struct { const char *g, *label; } GL[] = {
        {"Closed_Fist", "FIST - GRENADE"}, {"Open_Palm", "PALM - RELOAD"}, {"Gun", "FINGER GUN - FIRE"},
        {"ILoveYou", "ROCK ON - USE"}, {"Finger_1", "1 - KNIFE"}, {"Finger_2", "2 - SIDEARM"},
        {"Finger_3", "3 - PRIMARY"}, {"Finger_4", "4 - PRIMARY 2"}};
    QString g = "NO HAND";
    for (const auto &e : GL) if (m_cvGesture == e.g) g = e.label;
    if (m_cvGesture == "None" && f.hand) g = "HAND";
    text(mx, cy2 + 18, mw, 14, Qt::AlignCenter, g, m_cvGesture != "None" ? QColor(120, 200, 255) : QColor(150, 150, 150), 9, 900, false);
    text(mx, cy2 + 34, mw, 12, Qt::AlignCenter, "1-4 FINGERS: WEAPON   FINGER GUN: FIRE", QColor(130, 130, 130), 7, 600, false);
}
