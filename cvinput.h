// =============================================================================
//  cvinput.h  -  Webcam controls: receives what tools/cv_control.py sees
//
//  The helper (Python: OpenCV + MediaPipe face landmarker + gesture recognizer)
//  sends one JSON datagram per camera frame to UDP 127.0.0.1:47888. CvInput
//  starts / stops the helper and keeps the latest measurement; turning it into
//  game actions (calibration, dead zones, gestures) is cvcontrols.cpp.
// =============================================================================
#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QUdpSocket>
#include <vector>
#include <cstdint>

struct CvFrame {
    bool    face = false, hand = false;
    float   x = 0.5f, y = 0.5f;          // nose in the (mirrored) image, 0..1
    float   yaw = 0.f, pitch = 0.f;      // head turn / nod, about -1..1 (+ = right / down)
    float   roll = 0.f;                  // head tilt, degrees (+ = right ear to right shoulder)
    float   size = 0.f;                  // face width / image width: bigger = closer
    float   jaw = 0.f;                   // mouth open 0..1
    float   box[4] = {0, 0, 0, 0};       // face box, 0..1
    float   hx = 0.5f, hy = 0.5f;        // hand centre
    QString gesture = "None";            // MediaPipe gesture name
    bool    up[4] = {false, false, false, false};   // index, middle, ring, little finger raised
    int     fingers = 0;                 // how many of them
    bool    thumbOut = false;
    float   gscore = 0.f;
    std::vector<uint8_t> thumb;          // 96 x 72 grey camera image
};

class CvInput : public QObject {
    Q_OBJECT
public:
    static const int THUMB_W = 96, THUMB_H = 72;
    explicit CvInput(QObject *parent = nullptr);
    ~CvInput() override;

    bool start();                        // start listening + the helper script
    void stop();
    bool running() const { return m_sock != nullptr; }
    bool helperAlive() const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    QString status() const;                  // what to show while there are no camera frames

    const CvFrame &frame() const { return m_frame; }
    float age() const { return m_age; }  // seconds since the last datagram
    void  tick(float dt) { m_age += dt; m_upT += dt; }
    int   frames() const { return m_count; }
    void  inject(const CvFrame &f) { m_frame = f; m_age = 0.f; m_count++; }   // tests: a frame as if the helper sent it

private slots:
    void onDatagram();

private:
    QUdpSocket *m_sock = nullptr;
    QProcess   *m_proc = nullptr;
    CvFrame     m_frame;
    float       m_age = 99.f;
    float       m_upT = 0.f;                 // seconds since start()
    int         m_count = 0;
    bool        m_noCamera = false;          // the helper runs but the camera gives no images
    QString     m_status;
    QString     findScript() const;
};
