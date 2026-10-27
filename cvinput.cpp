// =============================================================================
//  cvinput.cpp  -  Webcam controls: the helper process and its datagrams
// =============================================================================
#include "cvinput.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#if __has_include("raster_srcdir.h")
#include "raster_srcdir.h"                // qmake writes the source folder here (fps_game.pro)
#endif

static const quint16 CV_PORT = 47888;

CvInput::CvInput(QObject *parent) : QObject(parent) {}

CvInput::~CvInput() { stop(); }

// tools/cv_control.py: next to the game, one or two folders up, or in the source tree
QString CvInput::findScript() const
{
    QString env = qEnvironmentVariable("RASTER_CV_SCRIPT");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;
    QDir d(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 4; up++) {
        QString p = d.filePath("tools/cv_control.py");
        if (QFileInfo::exists(p)) return p;
        if (!d.cdUp()) break;
    }
#ifdef RASTER_SRC_DIR
    QString src = QString(RASTER_SRC_DIR) + "/tools/cv_control.py";
    if (QFileInfo::exists(src)) return src;
#endif
    return QString();
}

bool CvInput::start()
{
    if (m_sock) return true;
    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(QHostAddress::LocalHost, CV_PORT)) {
        m_status = QString("PORT %1 BUSY - IS ANOTHER COPY RUNNING?").arg(CV_PORT);
        delete m_sock; m_sock = nullptr;
        return false;
    }
    connect(m_sock, &QUdpSocket::readyRead, this, &CvInput::onDatagram);
    m_age = 99.f; m_count = 0; m_upT = 0.f; m_noCamera = false;

    QString script = findScript();
    if (script.isEmpty()) { m_status = "tools/cv_control.py NOT FOUND - RUN IT BY HAND"; return true; }
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_proc->setWorkingDirectory(QFileInfo(script).absolutePath());
    QStringList args{script, "--parent", QString::number(QCoreApplication::applicationPid())};   // it quits when we do
    m_proc->start("python", args);
    if (!m_proc->waitForStarted(3000)) {
        m_proc->start("py", args);                      // the Windows launcher
        if (!m_proc->waitForStarted(3000)) { m_status = "PYTHON NOT FOUND - RUN: python tools/cv_control.py"; return true; }
    }
    m_status.clear();
    return true;
}

QString CvInput::status() const
{
    if (!m_status.isEmpty()) return m_status;
    if (m_proc && m_proc->state() == QProcess::NotRunning)
        return "CAMERA HELPER STOPPED - RUN  python tools/cv_control.py --show  TO SEE WHY";
    if (m_noCamera) return "THE CAMERA GIVES NO IMAGES - CLOSE OTHER APPS USING IT";
    if (m_count == 0) return QString("STARTING THE CAMERA... %1 S - TAKES ABOUT 10 S").arg(int(m_upT));
    return "WAITING FOR THE CAMERA...";
}

void CvInput::stop()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(1500);
        delete m_proc; m_proc = nullptr;
    }
    if (m_sock) { m_sock->close(); delete m_sock; m_sock = nullptr; }
    m_frame = CvFrame{};
    m_age = 99.f;
}

void CvInput::onDatagram()
{
    while (m_sock && m_sock->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_sock->receiveDatagram();
        QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
        if (o.isEmpty()) continue;
        if (o.value("status").toString() == "nocam") { m_noCamera = true; continue; }   // a heartbeat: no images
        m_noCamera = false;
        CvFrame f;
        f.face = o.value("face").toInt() != 0;
        f.hand = o.value("hand").toInt() != 0;
        f.x = float(o.value("x").toDouble(0.5)); f.y = float(o.value("y").toDouble(0.5));
        f.yaw = float(o.value("yaw").toDouble()); f.pitch = float(o.value("pitch").toDouble());
        f.roll = float(o.value("roll").toDouble()); f.size = float(o.value("size").toDouble());
        f.jaw = float(o.value("jaw").toDouble());
        QJsonArray b = o.value("box").toArray();
        for (int i = 0; i < 4 && i < b.size(); i++) f.box[i] = float(b[i].toDouble());
        f.hx = float(o.value("hx").toDouble(0.5)); f.hy = float(o.value("hy").toDouble(0.5));
        f.gesture = o.value("gesture").toString("None");
        QString fb = o.value("fbits").toString();
        f.fingers = 0;
        for (int i = 0; i < 4 && i < fb.size(); i++) { f.up[i] = fb[i] == '1'; f.fingers += f.up[i]; }
        f.thumbOut = o.value("thumbout").toBool();
        f.gscore = float(o.value("gscore").toDouble());
        QByteArray t = QByteArray::fromBase64(o.value("thumb").toString().toLatin1());
        if (t.size() == THUMB_W * THUMB_H) f.thumb.assign(t.begin(), t.end());
        m_frame = std::move(f);
        m_age = 0.f;
        m_count++;
        m_status.clear();
    }
}
