// =============================================================================
//  multiplayer.cpp  -  Tactical Team Multiplayer Implementation
// =============================================================================
#include "multiplayer.h"
#include <QHostAddress>
#include <QTimer>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>

namespace {
static const char *const BOT_NAMES[8] = {
    "TITAN", "STRIKER", "SPECTRE", "PHANTOM",
    "GOLIATH", "VALKYRIE", "RAZOR", "APEX"
};
}

// Network data is untrusted: keep ids, weapons, teams and positions in range
static inline int clampGun(int g) { return (g >= -1 && g < WPN_COUNT) ? g : -1; }
static inline bool finitePos(float x, float y, float z, const World *w)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    if (w && (x < 0.f || y < 0.f || x >= float(w->m_mapW) || y >= float(w->m_mapH))) return false;
    return std::fabs(z) < 50.f;
}
static inline bool doorCell(const World &w, int y, int x) { return w.inside(x, y); }

int NetworkManager::idForSocket(QTcpSocket *s) const
{
    for (const auto &p : m_sockIds) if (p.first == s) return p.second;
    return -1;
}

QTcpSocket *NetworkManager::socketForId(int id) const
{
    for (const auto &p : m_sockIds) if (p.second == id) return p.first;
    return nullptr;
}

// Team spawn: the Citadel has dedicated spawn rooms; on other maps the first
// open floor cell scanning in from the west (Alpha) or east (Bravo) edge
void NetworkManager::teamSpawn(int team, const World &world, float &x, float &y) const
{
    if (isCitadel() && world.m_mapW >= 48 && world.m_mapH >= 48) { x = team == 0 ? 2.5f : 44.5f; y = 22.5f; return; }
    // the campus: Alpha holds the arts side (north), Bravo the engineering side by Aurobindo Bhavan (south)
    if (m_settings.mapChoice == MAP_JU && world.inside(int(JU_SPAWN[1][0]), int(JU_SPAWN[1][1]))) { x = JU_SPAWN[team & 1][0]; y = JU_SPAWN[team & 1][1]; return; }
    int W = world.m_mapW, H = world.m_mapH;
    for (int r = 0; r < H / 2; r++) {
        for (int k = 0; k < W; k++) {
            int cx = team == 0 ? 1 + k : W - 2 - k;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                int cy = H / 2 + sgn * r;
                if (!world.inside(cx, cy)) continue;
                float fx = cx + 0.5f, fy = cy + 0.5f;
                bool reachOk = world.m_reach.size() != size_t(W) * size_t(H) || world.reachable(cx, cy);   // reach map may be from the last map
                if (world.posValid(fx, fy, 0.3f) && reachOk) { x = fx; y = fy; return; }
            }
            if (k > W / 3) break;
        }
    }
    x = W * 0.5f; y = H * 0.5f;
}

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
{
}

NetworkManager::~NetworkManager()
{
    disconnectSession();
}

void NetworkManager::disconnectSession()
{
    m_state = MP_OFFLINE;
    m_connected = false;

    if (m_clientSocket) {
        m_clientSocket->disconnect();
        m_clientSocket->abort();
        delete m_clientSocket;
        m_clientSocket = nullptr;
    }

    for (QTcpSocket *sock : m_clientSockets) {
        if (sock) {
            sock->disconnect();
            sock->abort();
            delete sock;
        }
    }
    m_clientSockets.clear();
    m_sockIds.clear();
    m_nextClientId = 1;

    if (m_tcpServer) {
        m_tcpServer->disconnect();
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    if (m_udpSocket) {
        m_udpSocket->disconnect();
        m_udpSocket->close();
        delete m_udpSocket;
        m_udpSocket = nullptr;
    }

    m_slots.clear();
}

bool NetworkManager::startHost(quint16 port, const QString &hostName)
{
    disconnectSession();
    m_isHost = true;
    m_port = port;

    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(QHostAddress::Any, m_port)) {
        emit connectionError("Failed to bind TCP server on port " + QString::number(m_port));
        disconnectSession();
        return false;
    }
    connect(m_tcpServer, &QTcpServer::newConnection, this, &NetworkManager::onNewConnection);

    m_udpSocket = new QUdpSocket(this);
    m_udpSocket->bind(QHostAddress::Any, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &NetworkManager::onUdpData);

    m_localPlayerId = 0;
    m_connected = true;
    m_state = MP_LOBBY;

    // Slot 0 is Host
    m_slots.clear();
    MpPlayerSlot hostSlot;
    hostSlot.id = 0;
    hostSlot.name = hostName.isEmpty() ? "Host Operative" : hostName;
    hostSlot.team = 0; // Alpha
    hostSlot.cls = CLS_ASSAULT;
    hostSlot.ready = true;
    hostSlot.isHost = true;
    hostSlot.isBot = false;
    hostSlot.alive = true;
    m_slots.push_back(hostSlot);

    balanceTeamsAndFillBots();
    emit lobbyUpdated();
    return true;
}

bool NetworkManager::connectToHost(const QString &hostAddress, quint16 port, const QString &playerName)
{
    disconnectSession();
    m_isHost = false;
    m_port = port;

    m_clientSocket = new QTcpSocket(this);
    m_clientSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_clientSocket, &QTcpSocket::readyRead, this, &NetworkManager::onServerData);
    connect(m_clientSocket, &QTcpSocket::disconnected, this, &NetworkManager::onServerDisconnected);

    m_udpSocket = new QUdpSocket(this);
    m_udpSocket->bind(QHostAddress::Any, 0); // Ephemeral port
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &NetworkManager::onUdpData);

    QString host = hostAddress.trimmed();
    quint16 p = port;
    if (host.contains(':')) {
        QStringList parts = host.split(':');
        host = parts[0];
        if (parts.size() > 1) p = quint16(parts[1].toUShort());
    }

    m_serverAddr = QHostAddress(host);
    m_clientSocket->connectToHost(host, p);
    if (!m_clientSocket->waitForConnected(5000)) {
        emit connectionError("Connection timed out to " + hostAddress);
        disconnectSession();
        return false;
    }
    m_serverAddr = m_clientSocket->peerAddress();

    m_connected = true;
    m_state = MP_LOBBY;

    // Send Join Request
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << (quint8)NET_JOIN_REQ << playerName << (quint8)0 << (quint8)CLS_ASSAULT;
    sendTcpPacket(m_clientSocket, NET_JOIN_REQ, payload);

    return true;
}

void NetworkManager::onNewConnection()
{
    if (!m_tcpServer) return;
    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket *sock = m_tcpServer->nextPendingConnection();
        if (int(m_clientSockets.size()) >= MP_MAX_PLAYERS - 1 || m_nextClientId >= 100) { sock->abort(); sock->deleteLater(); continue; }   // room full
        sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_clientSockets.push_back(sock);
        m_sockIds.push_back({sock, m_nextClientId++});
        connect(sock, &QTcpSocket::readyRead, this, &NetworkManager::onClientData);
        connect(sock, &QTcpSocket::disconnected, this, &NetworkManager::onClientDisconnected);
    }
}

void NetworkManager::onClientDisconnected()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    auto it = std::find(m_clientSockets.begin(), m_clientSockets.end(), sock);
    if (it != m_clientSockets.end()) {
        m_clientSockets.erase(it);
    }
    // drop the player's lobby slot and, mid-match, their operative
    int gone = idForSocket(sock);
    m_sockIds.erase(std::remove_if(m_sockIds.begin(), m_sockIds.end(), [sock](const std::pair<QTcpSocket*, int> &p) { return p.first == sock; }), m_sockIds.end());
    if (gone >= 0) {
        m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(), [gone](const MpPlayerSlot &s) { return s.id == gone; }), m_slots.end());
        if (m_activeGs) m_activeGs->actors.erase(std::remove_if(m_activeGs->actors.begin(), m_activeGs->actors.end(), [gone](const Actor &a) { return a.id == gone; }), m_activeGs->actors.end());
    }
    sock->deleteLater();

    balanceTeamsAndFillBots();
    broadcastLobbyState();
    emit lobbyUpdated();
}

void NetworkManager::onServerDisconnected()
{
    emit connectionError("Disconnected from host server.");
    disconnectSession();
}

void NetworkManager::sendTcpPacket(QTcpSocket *sock, NetMsgType type, const QByteArray &payload)
{
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) return;
    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);
    out << (quint8)type << (quint32)payload.size();
    block.append(payload);
    sock->write(block);
}

void NetworkManager::broadcastTcpPacket(NetMsgType type, const QByteArray &payload)
{
    for (QTcpSocket *sock : m_clientSockets) {
        sendTcpPacket(sock, type, payload);
    }
}

void NetworkManager::broadcastExcept(NetMsgType type, const QByteArray &payload, QTcpSocket *skip)
{
    for (QTcpSocket *sock : m_clientSockets) if (sock != skip) sendTcpPacket(sock, type, payload);
}

void NetworkManager::sendUdpPacket(const QByteArray &data, const QHostAddress &addr, quint16 port)
{
    if (m_udpSocket) {
        m_udpSocket->writeDatagram(data, addr, port);
    }
}

void NetworkManager::onClientData()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    while (sock->bytesAvailable() >= 5) {
        QByteArray header = sock->peek(5);
        quint32 len = 0;
        QDataStream in(header);
        quint8 dummy; in >> dummy >> len;

        if (sock->bytesAvailable() < 5 + len) break; // Wait for full packet
        sock->read(5); // Consume header
        QByteArray payload = sock->read(len);
        processTcpPacket(payload, sock);
    }
}

void NetworkManager::onServerData()
{
    if (!m_clientSocket) return;
    while (m_clientSocket->bytesAvailable() >= 5) {
        QByteArray header = m_clientSocket->peek(5);
        quint32 len = 0;
        QDataStream in(header);
        quint8 dummy; in >> dummy >> len;

        if (m_clientSocket->bytesAvailable() < 5 + len) break;
        m_clientSocket->read(5);
        QByteArray payload = m_clientSocket->read(len);
        processTcpPacket(payload, nullptr);
    }
}

void NetworkManager::processTcpPacket(const QByteArray &data, QTcpSocket *fromSocket)
{
    QDataStream in(data);

    if (m_isHost) {
        // --- HOST HANDLERS ---
        // Which player sent this (ids are fixed per connection)
        int playerId = idForSocket(fromSocket);
        if (playerId < 0) return;

        if (data.size() < 1) return;
        quint8 type = 0;
        in >> type;

        if (type == NET_JOIN_REQ) {
            QString name; quint8 reqTeam, reqClass;
            in >> name >> reqTeam >> reqClass;

            int humanCount[2] = {0, 0};
            for (const auto &s : m_slots) {
                if (!s.isBot && s.id != playerId) humanCount[s.team]++;
            }
            int assignedTeam = reqTeam % 2;
            if (humanCount[assignedTeam] >= m_settings.teamSize && humanCount[1 - assignedTeam] < m_settings.teamSize) {
                assignedTeam = 1 - assignedTeam;
            }

            MpPlayerSlot slot;
            slot.id = playerId;
            slot.name = name.trimmed().isEmpty() ? QString("Operative %1").arg(playerId + 1) : name.trimmed().left(15);
            slot.team = assignedTeam;
            slot.cls = CharClass(reqClass % 3);
            slot.ready = false;
            slot.isHost = false;
            slot.isBot = false;
            slot.alive = true;

            // Add or update slot
            bool found = false;
            for (auto &s : m_slots) {
                if (s.id == playerId) { s = slot; found = true; break; }
            }
            if (!found) m_slots.push_back(slot);

            balanceTeamsAndFillBots();

            // Send Join Ack back
            QByteArray ack; QDataStream aout(&ack, QIODevice::WriteOnly);
            aout << (quint8)NET_JOIN_ACK << (qint32)playerId;
            sendTcpPacket(fromSocket, NET_JOIN_ACK, ack);

            broadcastLobbyState();
            emit lobbyUpdated();
        }
        else if (type == NET_SET_TEAM) {
            quint8 t; in >> t;
            int wantedTeam = t % 2;
            int humanCount = 0;
            for (const auto &s : m_slots) {
                if (!s.isBot && s.id != playerId && s.team == wantedTeam) humanCount++;
            }
            if (humanCount < m_settings.teamSize) {
                for (auto &s : m_slots) { if (s.id == playerId) { s.team = wantedTeam; break; } }
                balanceTeamsAndFillBots();
                broadcastLobbyState();
                emit lobbyUpdated();
            }
        }
        else if (type == NET_SET_CLASS) {
            quint8 c; in >> c;
            for (auto &s : m_slots) { if (s.id == playerId) { s.cls = CharClass(c % 3); break; } }
            balanceTeamsAndFillBots();
            broadcastLobbyState();
            emit lobbyUpdated();
        }
        else if (type == NET_SET_READY) {
            quint8 r; in >> r;
            for (auto &s : m_slots) { if (s.id == playerId) { s.ready = (r != 0); break; } }
            broadcastLobbyState();
            emit lobbyUpdated();
        }
        else if (type == NET_SET_NAME) {
            QString newName; in >> newName;
            if (!newName.trimmed().isEmpty()) {
                for (auto &s : m_slots) { if (s.id == playerId) { s.name = newName.trimmed().left(15); break; } }
                broadcastLobbyState();
                emit lobbyUpdated();
            }
        }
        else if (type == NET_INPUT) {
            qint32 pid; float px, py, pz, dx, dy, vx, vy;
            qint32 gun, hp, shots = 0; quint8 alive, shooting;
            in >> pid >> px >> py >> pz >> dx >> dy >> vx >> vy >> gun >> hp >> alive >> shooting >> shots;
            pid = playerId;                                   // a client can only move its own operative
            if (in.status() != QDataStream::Ok || !finitePos(px, py, pz, m_activeWorld) || !std::isfinite(dx) || !std::isfinite(dy)) return;
            gun = clampGun(gun); hp = std::max(0, std::min(int(hp), 500));

            if (m_activeGs) {
                for (auto &act : m_activeGs->actors) {
                    if (act.id == pid && act.ctl == CTL_REMOTE) {
                        act.posX = px; act.posY = py; act.footZ = pz;
                        act.dirX = dx; act.dirY = dy;
                        act.planeX = -dy * 0.66f; act.planeY = dx * 0.66f;
                        act.velX = vx; act.velY = vy;
                        act.curWeapon = gun;
                        act.hp = hp;
                        act.alive = (alive != 0);
                        if (shooting) act.shootAnim = 0.15f;
                        if (shots > act.netShots) act.shotFx = std::min(3, act.shotFx + int(shots - act.netShots));
                        act.netShots = shots;
                        break;
                    }
                }
            }
        }
        else if (type == NET_EVENT) {
            quint8 evType; in >> evType;
            QVariantMap data; in >> data;
            auto forwardAll = [&](QTcpSocket *skip) {
                QByteArray fwd; QDataStream fout(&fwd, QIODevice::WriteOnly);
                fout << (quint8)NET_EVENT << evType << data;
                broadcastExcept(NET_EVENT, fwd, skip);
            };
            if (evType == EV_DAMAGE) {
                // a client hit someone: route it to whoever owns that health
                int dmg = std::max(0, std::min(data.value("dmg").toInt(), 400));
                dealDamage(playerId, data.value("target").toInt(), dmg, data.value("head").toBool());
                return;
            }
            if (evType == EV_DIED) {                                 // a client was killed: the host keeps the score
                creditKill(playerId, data.value("killer").toInt(), data.value("head").toBool());
                return;
            }
            if (evType == EV_SPAWN_PICKUP || evType == EV_PICKUP) {  // a client dropped / took loot: tell the rest
                forwardAll(fromSocket);
                if (evType == EV_SPAWN_PICKUP) spawnFromEvent(data); else takeFromEvent(data);
                return;
            }
            if (evType == EV_KEY_PICKED) {
                m_match.keyCarrier = playerId;
                data["carrier"] = playerId;
                forwardAll(nullptr);
            }
            else if (evType == EV_KEY_DELIVERED) {
                if (!m_match.keyActive) return;                      // already delivered
                int team = std::max(0, teamOf(playerId));
                m_match.keyActive = false; m_match.keyCarrier = -1; m_match.vaultUnlocked = true;
                m_match.teamScore[team] += 500;
                data["team"] = team; data["points"] = 500;
                forwardAll(nullptr);
            }
            else if (evType == EV_RESPAWN) {
                int rid = playerId;
                data["id"] = rid;
                QByteArray fwd; QDataStream fout(&fwd, QIODevice::WriteOnly);
                fout << (quint8)NET_EVENT << (quint8)EV_RESPAWN << data;
                broadcastTcpPacket(NET_EVENT, fwd);

                for (auto &sl : m_slots) if (sl.id == rid) sl.alive = true;
                if (m_activeGs && rid != m_localPlayerId) {
                    CharClass cls = CLS_ASSAULT;
                    for (const auto &sl : m_slots) if (sl.id == rid) cls = sl.cls;
                    for (auto &act : m_activeGs->actors) {
                        if (act.id == rid) {
                            // back to full health and the class kit, or the next snapshot would
                            // report 0 HP and kill the client again the moment it respawned
                            initClassLoadout(act, cls);
                            act.alive = true;
                            act.respawnT = 5.f;
                            break;
                        }
                    }
                }
            }
            emit matchEvent(NetEventType(evType), data);
        }
    } else {
        // --- CLIENT HANDLERS ---
        quint8 type = 0;
        in >> type;

        if (type == NET_JOIN_ACK) {
            qint32 pid; in >> pid;
            m_localPlayerId = pid;
        }
        else if (type == NET_LOBBY_UPDATE) {
            qint32 tSize, rDur, rLimit, mChoice; quint8 bFill; QString rCode; qint32 numSlots;
            in >> tSize >> rDur >> rLimit >> mChoice >> bFill >> rCode >> numSlots;
            m_settings.teamSize = tSize;
            m_settings.roundDuration = rDur;
            m_settings.respawnLimit = rLimit;
            m_settings.mapChoice = mChoice;
            m_settings.botFill = (bFill != 0);
            m_settings.roomCode = rCode;

            m_slots.clear();
            numSlots = std::max(0, std::min(int(numSlots), MP_MAX_PLAYERS * 2));
            for (int i = 0; i < numSlots && in.status() == QDataStream::Ok; i++) {
                qint32 id, team, cls; QString name; quint8 ready, bot;
                in >> id >> name >> team >> cls >> ready >> bot;
                MpPlayerSlot s;
                s.id = id; s.name = name.left(15); s.team = team == 1 ? 1 : 0; s.cls = CharClass(((cls % 3) + 3) % 3);
                s.ready = (ready != 0); s.isBot = (bot != 0);
                s.isHost = (id == 0);
                m_slots.push_back(s);
            }
            emit lobbyUpdated();
        }
        else if (type == NET_START_MATCH) {
            m_state = MP_PLAYING;
            emit matchStarted();
        }
        else if (type == NET_SNAPSHOT) {
            float rTimer; quint8 inR, rOver, mOver;
            qint32 rWin, cRound, s0, s1, rw0, rw1, wTeam;
            quint8 kAct; qint32 kCar; float kx, ky, kz; quint8 vUn;
            in >> rTimer >> inR >> rOver >> mOver >> rWin >> cRound >> s0 >> s1 >> rw0 >> rw1 >> wTeam
               >> kAct >> kCar >> kx >> ky >> kz >> vUn;

            m_match.roundTimer = rTimer;
            m_match.inRound = (inR != 0);
            m_match.roundOver = (rOver != 0);
            m_match.matchOver = (mOver != 0);
            m_match.roundWinner = rWin;
            m_match.currentRound = cRound;
            m_match.teamScore[0] = s0; m_match.teamScore[1] = s1;
            m_match.roundsWon[0] = rw0; m_match.roundsWon[1] = rw1;
            m_match.winningTeam = wTeam;
            m_match.keyActive = (kAct != 0); m_match.keyCarrier = kCar;
            m_match.keyX = kx; m_match.keyY = ky; m_match.keyZ = kz;
            m_match.vaultUnlocked = (vUn != 0);

            // Blast doors
            float d5, d42; quint8 dm5, dm42;
            in >> d5 >> dm5 >> d42 >> dm42;
            if (m_activeWorld && isCitadel() && doorCell(*m_activeWorld, 22, 42)) {
                m_activeWorld->m_doorT[22][5] = d5;
                m_activeWorld->m_doorMoving[22][5] = (dm5 != 0);
                m_activeWorld->m_doorT[22][42] = d42;
                m_activeWorld->m_doorMoving[22][42] = (dm42 != 0);
            }

            // Host player (id 0)
            float hx, hy, hz, hdx, hdy, hvx, hvy;
            qint32 hgun, hhp, hshots; quint8 halive, hshooting;
            in >> hx >> hy >> hz >> hdx >> hdy >> hvx >> hvy >> hgun >> hhp >> halive >> hshooting >> hshots;

            hgun = clampGun(hgun);
            if (m_activeGs && finitePos(hx, hy, hz, m_activeWorld)) {
                for (auto &act : m_activeGs->actors) {
                    if (act.id == 0) {
                        act.posX = hx; act.posY = hy; act.footZ = hz;
                        act.dirX = hdx; act.dirY = hdy;
                        act.planeX = -hdy * 0.66f; act.planeY = hdx * 0.66f;
                        act.velX = hvx; act.velY = hvy;
                        act.curWeapon = hgun;
                        act.hp = hhp;
                        if (act.alive && !halive) act.respawnT = 5.f;      // keep the body on the ground
                        act.alive = (halive != 0);
                        if (hshooting) act.shootAnim = 0.15f;
                        if (hshots > act.netShots) act.shotFx = std::min(3, act.shotFx + int(hshots - act.netShots));
                        act.netShots = hshots;
                        break;
                    }
                }

            }
            if (m_activeGs) {
                qint32 actCount; in >> actCount;
                actCount = std::max(0, std::min(int(actCount), 64));
                for (int i = 0; i < actCount && in.status() == QDataStream::Ok; i++) {
                    qint32 aid, ateam, actl;
                    float ax, ay, az, adx, ady, avx, avy;
                    qint32 agun, ahp, ashots; quint8 aalive, ashooting;
                    in >> aid >> ateam >> actl >> ax >> ay >> az >> adx >> ady >> avx >> avy >> agun >> ahp >> aalive >> ashooting >> ashots;
                    agun = clampGun(agun);
                    if (!finitePos(ax, ay, az, m_activeWorld)) continue;

                    if (aid == m_localPlayerId) continue;            // our own health is ours: hits arrive as events

                    for (auto &act : m_activeGs->actors) {
                        if (act.id == aid && act.id != 0) {
                            act.posX = ax; act.posY = ay; act.footZ = az;
                            act.dirX = adx; act.dirY = ady;
                            act.planeX = -ady * 0.66f; act.planeY = adx * 0.66f;
                            act.velX = avx; act.velY = avy;
                            act.curWeapon = agun;
                            act.hp = ahp;
                            if (act.alive && !aalive) act.respawnT = 5.f;
                            act.alive = (aalive != 0);
                            if (ashooting) act.shootAnim = 0.15f;
                            if (ashots > act.netShots) act.shotFx = std::min(3, act.shotFx + int(ashots - act.netShots));
                            act.netShots = ashots;
                            break;
                        }
                    }
                }
                // everyone's kills, deaths and score (the host keeps them)
                qint32 nStats = 0; in >> nStats;
                nStats = std::max(0, std::min(int(nStats), MP_MAX_PLAYERS * 2));
                for (int i = 0; i < nStats && in.status() == QDataStream::Ok; i++) {
                    qint32 sid, k, d, sc; in >> sid >> k >> d >> sc;
                    for (auto &sl : m_slots) if (sl.id == sid) { sl.kills = k; sl.deaths = d; sl.score = sc; }
                }
            }
        }
        else if (type == NET_EVENT) {
            quint8 evType; in >> evType;
            QVariantMap data;
            in >> data;
            if (evType == EV_DAMAGE) {
                if (data.value("target").toInt() == m_localPlayerId)
                    applyLocalHit(data.value("dmg").toInt(), data.value("shooter").toInt(), data.value("head").toBool());
                return;
            }
            if (evType == EV_SPAWN_PICKUP) { spawnFromEvent(data); return; }
            if (evType == EV_PICKUP) { takeFromEvent(data); return; }
            if (evType == EV_KEY_PICKED) m_match.keyCarrier = data.value("carrier").toInt();
            else if (evType == EV_RESPAWN) {
                int rid = data.value("id").toInt();
                if (m_activeGs && rid != m_localPlayerId) {
                    for (auto &act : m_activeGs->actors) {
                        if (act.id == rid) {
                            act.alive = true;
                            act.respawnT = 5.f;
                            break;
                        }
                    }
                }
            }
            emit matchEvent(NetEventType(evType), data);
        }
    }
}

void NetworkManager::onUdpData()
{
    while (m_udpSocket && m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        QHostAddress sender; quint16 port;
        m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &port);

        // Client and server movement snapshot handling
    }
}

void NetworkManager::balanceTeamsAndFillBots()
{
    if (!m_isHost) return;

    // Remove old bots
    m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(), [](const MpPlayerSlot &s) { return s.isBot; }), m_slots.end());

    if (!m_settings.botFill) return;

    int teamCount[2] = {0, 0};
    int classCount[2][3] = {{0,0,0}, {0,0,0}};
    for (const auto &s : m_slots) {
        teamCount[s.team]++;
        classCount[s.team][s.cls]++;
    }

    int neededPerTeam = m_settings.teamSize;
    int botId = 100;

    for (int t = 0; t < 2; t++) {
        while (teamCount[t] < neededPerTeam && int(m_slots.size()) < MP_MAX_PLAYERS) {
            // Find most under-represented class for symmetrical balance
            int bestCls = 0;
            int minVal = 999;
            for (int c = 0; c < 3; c++) {
                if (classCount[t][c] < minVal) { minVal = classCount[t][c]; bestCls = c; }
            }

            MpPlayerSlot bot;
            bot.id = botId++;
            bot.name = BOT_NAMES[(bot.id + t * 4) % 8];
            bot.team = t;
            bot.cls = CharClass(bestCls);
            bot.ready = true;
            bot.isHost = false;
            bot.isBot = true;
            bot.alive = true;
            m_slots.push_back(bot);

            teamCount[t]++;
            classCount[t][bestCls]++;
        }
    }
}

void NetworkManager::broadcastLobbyState()
{
    if (!m_isHost) return;
    QByteArray pl; QDataStream sout(&pl, QIODevice::WriteOnly);
    sout << (quint8)NET_LOBBY_UPDATE << (qint32)m_settings.teamSize << (qint32)m_settings.roundDuration
         << (qint32)m_settings.respawnLimit << (qint32)m_settings.mapChoice << (quint8)(m_settings.botFill ? 1 : 0)
         << m_settings.roomCode << (qint32)m_slots.size();
    for (const auto &slot : m_slots) {
        sout << (qint32)slot.id << slot.name << (qint32)slot.team << (qint32)slot.cls << (quint8)(slot.ready ? 1 : 0) << (quint8)(slot.isBot ? 1 : 0);
    }
    broadcastTcpPacket(NET_LOBBY_UPDATE, pl);
}

void NetworkManager::setPlayerName(const QString &name)
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return;
    for (auto &s : m_slots) {
        if (s.id == m_localPlayerId) { s.name = trimmed; break; }
    }
    if (m_isHost) {
        broadcastLobbyState();
        emit lobbyUpdated();
    } else if (m_clientSocket) {
        QByteArray pl; QDataStream out(&pl, QIODevice::WriteOnly);
        out << (quint8)NET_SET_NAME << trimmed;
        sendTcpPacket(m_clientSocket, NET_SET_NAME, pl);
        emit lobbyUpdated();
    }
}

void NetworkManager::setTeam(int team)
{
    int wantedTeam = team % 2;
    if (m_isHost) {
        int humanCount = 0;
        for (const auto &s : m_slots) {
            if (!s.isBot && s.id != m_localPlayerId && s.team == wantedTeam) humanCount++;
        }
        if (humanCount < m_settings.teamSize) {
            for (auto &s : m_slots) { if (s.id == m_localPlayerId) { s.team = wantedTeam; break; } }
            balanceTeamsAndFillBots();
            broadcastLobbyState();
            emit lobbyUpdated();
        }
    } else if (m_clientSocket) {
        QByteArray pl; QDataStream out(&pl, QIODevice::WriteOnly);
        out << (quint8)NET_SET_TEAM << (quint8)wantedTeam;
        sendTcpPacket(m_clientSocket, NET_SET_TEAM, pl);
    }
}

void NetworkManager::setClass(CharClass cls)
{
    if (m_isHost) {
        for (auto &s : m_slots) { if (s.id == m_localPlayerId) { s.cls = cls; break; } }
        balanceTeamsAndFillBots();
        broadcastLobbyState();
        emit lobbyUpdated();
    } else if (m_clientSocket) {
        QByteArray pl; QDataStream out(&pl, QIODevice::WriteOnly);
        out << (quint8)NET_SET_CLASS << (quint8)cls;
        sendTcpPacket(m_clientSocket, NET_SET_CLASS, pl);
    }
}

void NetworkManager::setReady(bool ready)
{
    if (m_isHost) {
        for (auto &s : m_slots) { if (s.id == m_localPlayerId) { s.ready = ready; break; } }
        broadcastLobbyState();
        emit lobbyUpdated();
    } else if (m_clientSocket) {
        QByteArray pl; QDataStream out(&pl, QIODevice::WriteOnly);
        out << (quint8)NET_SET_READY << (quint8)(ready ? 1 : 0);
        sendTcpPacket(m_clientSocket, NET_SET_READY, pl);
    }
}

void NetworkManager::kickSlot(int slotId)
{
    if (!m_isHost || slotId == m_localPlayerId) return;
    if (QTcpSocket *sock = socketForId(slotId)) sock->disconnectFromHost();
    m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(), [slotId](const MpPlayerSlot &s) { return s.id == slotId; }), m_slots.end());
    balanceTeamsAndFillBots();
    broadcastLobbyState();
    emit lobbyUpdated();
}

void NetworkManager::cycleTeamSize()
{
    if (!m_isHost) return;
    m_settings.teamSize = (m_settings.teamSize % 4) + 1;
    updateSettings(m_settings);
}

void NetworkManager::toggleBotFill()
{
    if (!m_isHost) return;
    m_settings.botFill = !m_settings.botFill;
    updateSettings(m_settings);
}

void NetworkManager::cycleDuration()
{
    if (!m_isHost) return;
    if (m_settings.roundDuration < 300) m_settings.roundDuration = 300;
    else if (m_settings.roundDuration < 420) m_settings.roundDuration = 420;
    else if (m_settings.roundDuration < 600) m_settings.roundDuration = 600;
    else m_settings.roundDuration = 180;
    updateSettings(m_settings);
}

void NetworkManager::cycleRoundsToWin()
{
    if (!m_isHost) return;
    if (m_settings.roundsToWin == 1) m_settings.roundsToWin = 2;
    else if (m_settings.roundsToWin == 2) m_settings.roundsToWin = 3;
    else m_settings.roundsToWin = 1;
    updateSettings(m_settings);
}

void NetworkManager::cycleMap()
{
    if (!m_isHost) return;
    if (m_settings.mapChoice == 6) m_settings.mapChoice = MAP_JU;
    else if (m_settings.mapChoice == MAP_JU) m_settings.mapChoice = 5;
    else if (m_settings.mapChoice == 5) m_settings.mapChoice = 4;
    else if (m_settings.mapChoice == 4) m_settings.mapChoice = 1;
    else m_settings.mapChoice = 6;
    updateSettings(m_settings);
}

void NetworkManager::updateSettings(const MpRoomSettings &s)
{
    if (!m_isHost) return;
    m_settings = s;
    balanceTeamsAndFillBots();
    broadcastLobbyState();
    emit lobbyUpdated();
}

void NetworkManager::startMatch()
{
    if (!m_isHost) return;
    m_state = MP_PLAYING;

    m_match = MpMatchState{};
    m_match.roundTimer = float(m_settings.roundDuration);
    m_match.inRound = true;
    for (auto &sl : m_slots) { sl.kills = sl.deaths = sl.score = 0; sl.alive = true; }

    QByteArray pl; QDataStream out(&pl, QIODevice::WriteOnly);
    out << (quint8)NET_START_MATCH << (qint32)m_settings.mapChoice;
    broadcastTcpPacket(NET_START_MATCH, pl);
}

void NetworkManager::resetRound(GameState &gs, World &world, const ParsedMap &pm)
{
    m_match.roundTimer = float(m_settings.roundDuration);
    m_match.inRound = true;
    m_match.roundOver = false;
    m_match.roundWinner = -1;
    m_match.intermissionTimer = 0.f;
    m_match.teamScore[0] = 0; // RESET ROUND SCORES!
    m_match.teamScore[1] = 0; // RESET ROUND SCORES!
    m_match.keyActive = false;
    m_match.keyCarrier = -1;
    m_match.keySpawnTimer = 120.f;
    m_match.vaultUnlocked = false;
    m_match.catwalkWeaponTimer = 60.f;
    m_match.courtyardWeaponTimer = 60.f;

    m_match.medkitTimer1 = m_match.medkitTimer2 = 35.f;
    m_match.ammoTimer1 = m_match.ammoTimer2 = 25.f;
    for (auto &sl : m_slots) sl.alive = true;
    m_localDeadReported = false;

    // Clear pickups & bullets
    gs.weaponPickups.clear();
    gs.healthKits.clear();
    gs.bullets.clear();
    gs.tracers.clear();

    // Close all blast doors at the start of every round so both teams start in spawn rooms!
    for (int y = 0; y < world.m_mapH; y++) {
        for (int x = 0; x < world.m_mapW; x++) {
            if (pm.type[y][x] == CELL_DOOR) {
                world.m_map[y][x] = CELL_DOOR;
                world.m_doorT[y][x] = 0.f;
                world.m_doorMoving[y][x] = false;
            }
        }
    }

    // Dedicated Spawn Rooms:
    // Team Alpha: West Spawn Room (center: X: 2.5, Y: 22.5)
    // Team Bravo: East Spawn Room (center: X: 44.5, Y: 22.5)
    float baseA_X, baseA_Y, baseB_X, baseB_Y;
    teamSpawn(0, world, baseA_X, baseA_Y);
    teamSpawn(1, world, baseB_X, baseB_Y);

    gs.actors.clear();

    for (size_t i = 0; i < m_slots.size(); i++) {
        const auto &s = m_slots[i];
        float baseX = (s.team == 0) ? baseA_X : baseB_X;
        float baseY = (s.team == 0) ? baseA_Y : baseB_Y;

        float ox = (i % 2 == 0) ? -0.30f : 0.30f;
        float oy = ((i / 2) % 2 == 0) ? -0.30f : 0.30f;
        float sx = baseX, sy = baseY;
        if (world.posValid(baseX + ox, baseY + oy, 0.28f)) {
            sx = baseX + ox;
            sy = baseY + oy;
        }

        if (s.id == m_localPlayerId) {
            gs.player.posX = sx; gs.player.posY = sy;
            gs.player.dirX = (s.team == 0) ? 1.f : -1.f;
            gs.player.dirY = 0.f;
            gs.player.planeX = -gs.player.dirY * 0.66f;
            gs.player.planeY = gs.player.dirX * 0.66f;
            gs.player.velX = gs.player.velY = gs.player.velZ = 0.f;
            gs.player.footZ = world.floorH(sx, sy);
            gs.player.onGround = true;
            initClassLoadout(gs.player, s.cls);
        } else {
            Actor a;
            a.id = s.id;
            a.team = s.team;
            a.ctl = s.isBot ? CTL_BOT : CTL_REMOTE;
            std::snprintf(a.name, sizeof(a.name), "%s", s.name.toUtf8().constData());
            a.posX = sx; a.posY = sy;
            a.dirX = (s.team == 0) ? 1.f : -1.f;
            a.dirY = 0.f;
            a.planeX = -a.dirY * 0.66f;
            a.planeY = a.dirX * 0.66f;
            a.velX = a.velY = a.velZ = 0.f;
            a.footZ = world.floorH(sx, sy);
            a.onGround = true;
            a.alive = true;
            initClassLoadout(a, s.cls);
            gs.actors.push_back(a);
        }
    }
}

void NetworkManager::tickServer(float dt, GameState &gs, World &world, const ParsedMap &pm)
{
    if (!m_isHost) return;
    m_activeGs = &gs;
    m_activeWorld = &world;

    if (m_match.inRound) {
        m_match.roundTimer -= dt;

        // Safety blast door release: 3s into the round, ensure spawn blast doors are opening
        if (m_match.roundTimer <= (float(m_settings.roundDuration) - 3.0f)) {
            if (isCitadel() && doorCell(world, 22, 42)) {
                if (world.m_map[22][5] == CELL_DOOR && !world.m_doorMoving[22][5] && world.m_doorT[22][5] < 0.9f) world.m_doorMoving[22][5] = true;
                if (world.m_map[22][42] == CELL_DOOR && !world.m_doorMoving[22][42] && world.m_doorT[22][42] < 0.9f) world.m_doorMoving[22][42] = true;
            }
        }

        // Check encryption key objective spawn (spawns 2 minutes into round)
        if (isCitadel() && !m_match.keyActive && !m_match.vaultUnlocked && m_match.roundTimer <= (float(m_settings.roundDuration) - 120.f)) {
            m_match.keyActive = true;
            m_match.keyCarrier = -1;
            m_match.keyX = 23.5f; m_match.keyY = 22.5f; m_match.keyZ = -1.8f;

            WeaponPickup kp;
            kp.x = m_match.keyX; kp.y = m_match.keyY;
            kp.type = PICKUP_KEY;
            kp.reward = true;
            gs.weaponPickups.push_back(kp);

            QVariantMap ev;
            ev["x"] = m_match.keyX; ev["y"] = m_match.keyY;
            broadcastEvent(EV_KEY_SPAWNED, ev);
        }

        // Pads: a new item only once the last one there was taken
        auto padFree = [&](float x, float y) {
            if (!world.posValid(x, y, 0.2f)) return false;
            for (const auto &wp : gs.weaponPickups) if (wp.active && std::fabs(wp.x - x) < 0.5f && std::fabs(wp.y - y) < 0.5f) return false;
            for (const auto &k : gs.healthKits) if (k.active && std::fabs(k.x - x) < 0.5f && std::fabs(k.y - y) < 0.5f) return false;
            return true;
        };
        auto gunPad = [&](float &timer, float x, float y, int w1, int w2) {
            timer -= dt;
            if (timer > 0.f) return;
            timer = 60.f;
            if (!padFree(x, y)) return;
            WeaponPickup wp; wp.x = x; wp.y = y; wp.type = PICKUP_WEAPON;
            wp.weapon = (rand() % 2 == 0) ? w1 : w2;
            wp.load.rarity = 2;
            wp.reward = true;
            gs.weaponPickups.push_back(wp);
        };
        // Power weapon pads (Catwalk sniper / DMR, Courtyard LMG / auto shotgun, every 60 s)
        gunPad(m_match.catwalkWeaponTimer, 23.5f, 10.5f, WPN_SNIPER, WPN_DMR);
        gunPad(m_match.courtyardWeaponTimer, 23.5f, 33.5f, WPN_LMG, WPN_AUTO_SHOTGUN);

        // Supply pads: a medkit in two opposite corners, ammo crates in the other two
        auto supplyPad = [&](float &timer, float period, float x, float y, bool kit) {
            timer -= dt;
            if (timer > 0.f) return;
            timer = period;
            if (!padFree(x, y)) return;
            if (kit) { HealthKit hk; hk.x = x; hk.y = y; gs.healthKits.push_back(hk); }
            else { WeaponPickup wp; wp.x = x; wp.y = y; wp.type = PICKUP_AMMO; gs.weaponPickups.push_back(wp); }
        };
        supplyPad(m_match.medkitTimer1, 35.f, 8.5f, 7.5f, true);
        supplyPad(m_match.medkitTimer2, 35.f, 38.5f, 37.5f, true);
        supplyPad(m_match.ammoTimer1, 25.f, 8.5f, 37.5f, false);
        supplyPad(m_match.ammoTimer2, 25.f, 38.5f, 7.5f, false);

        // Round Win check by timer expiration
        if (m_match.roundTimer <= 0.f) {
            m_match.inRound = false;
            m_match.roundOver = true;
            int winTeam = -1;
            if (m_match.teamScore[0] > m_match.teamScore[1]) winTeam = 0;
            else if (m_match.teamScore[1] > m_match.teamScore[0]) winTeam = 1;
            m_match.roundWinner = winTeam;
            if (winTeam >= 0) m_match.roundsWon[winTeam]++;
            m_match.intermissionTimer = 5.0f; // 5-second round intermission

            QVariantMap rev;
            rev["winner"] = winTeam;
            rev["score0"] = m_match.teamScore[0];
            rev["score1"] = m_match.teamScore[1];
            rev["round"] = m_match.currentRound;
            broadcastEvent(EV_ROUND_END, rev);

            if (winTeam >= 0 && m_match.roundsWon[winTeam] >= m_settings.roundsToWin) {
                m_match.matchOver = true;
                m_match.winningTeam = winTeam;
                QVariantMap mev;
                mev["winner"] = winTeam;
                broadcastEvent(EV_MATCH_END, mev);
            }
        }
    } else if (m_match.roundOver && !m_match.matchOver) {
        m_match.intermissionTimer -= dt;
        if (m_match.intermissionTimer <= 0.f) {
            m_match.currentRound++;
            resetRound(gs, world, pm);
            QVariantMap rsev;
            rsev["round"] = m_match.currentRound;
            broadcastEvent(EV_ROUND_START, rsev);
        }
    }

    // Broadcast authoritative snapshot to all clients
    m_snapshotTimer += dt;
    if (m_snapshotTimer >= 0.033f && !m_clientSockets.empty()) {
        m_snapshotTimer = 0.f;
        QByteArray snapData;
        QDataStream out(&snapData, QIODevice::WriteOnly);
        out << (quint8)NET_SNAPSHOT;
        // Match state
        out << (float)m_match.roundTimer
            << (quint8)(m_match.inRound ? 1 : 0)
            << (quint8)(m_match.roundOver ? 1 : 0)
            << (quint8)(m_match.matchOver ? 1 : 0)
            << (qint32)m_match.roundWinner
            << (qint32)m_match.currentRound
            << (qint32)m_match.teamScore[0] << (qint32)m_match.teamScore[1]
            << (qint32)m_match.roundsWon[0] << (qint32)m_match.roundsWon[1]
            << (qint32)m_match.winningTeam
            << (quint8)(m_match.keyActive ? 1 : 0) << (qint32)m_match.keyCarrier
            << (float)m_match.keyX << (float)m_match.keyY << (float)m_match.keyZ
            << (quint8)(m_match.vaultUnlocked ? 1 : 0);

        // Blast door states (Citadel)
        bool doors = isCitadel() && doorCell(world, 22, 42);
        float d5 = doors ? world.m_doorT[22][5] : 0.f;
        float d42 = doors ? world.m_doorT[22][42] : 0.f;
        quint8 dm5 = doors ? (world.m_doorMoving[22][5] ? 1 : 0) : 0;
        quint8 dm42 = doors ? (world.m_doorMoving[22][42] ? 1 : 0) : 0;
        out << d5 << dm5 << d42 << dm42;

        // Host player (id 0)
        const Player &hpl = gs.player;
        out << (float)hpl.posX << (float)hpl.posY << (float)hpl.footZ
            << (float)hpl.dirX << (float)hpl.dirY
            << (float)hpl.velX << (float)hpl.velY
            << (qint32)hpl.curWeapon << (qint32)hpl.hp
            << (quint8)(hpl.hp > 0 ? 1 : 0)
            << (quint8)(hpl.attackCooldown > 0.05f ? 1 : 0)
            << (qint32)gs.shotsFired;

        // Actors (bots and remote operatives)
        qint32 actCount = qint32(gs.actors.size());
        out << actCount;
        for (const auto &act : gs.actors) {
            out << (qint32)act.id << (qint32)act.team << (qint32)act.ctl
                << (float)act.posX << (float)act.posY << (float)act.footZ
                << (float)act.dirX << (float)act.dirY
                << (float)act.velX << (float)act.velY
                << (qint32)act.curWeapon << (qint32)act.hp
                << (quint8)(act.alive ? 1 : 0)
                << (quint8)(act.shootAnim > 0.01f ? 1 : 0)
                << (qint32)act.netShots;
        }
        out << (qint32)m_slots.size();
        for (const auto &sl : m_slots) out << (qint32)sl.id << (qint32)sl.kills << (qint32)sl.deaths << (qint32)sl.score;

        broadcastTcpPacket(NET_SNAPSHOT, snapData);
    }
}

void NetworkManager::tickClient(float dt, GameState &gs, World &world)
{
    (void)world;
    if (m_isHost || !m_clientSocket || m_clientSocket->state() != QAbstractSocket::ConnectedState) return;
    if (m_state != MP_PLAYING) return;

    m_activeGs = &gs;
    m_activeWorld = &world;

    m_snapshotTimer += dt;
    if (m_snapshotTimer < 0.033f) return;
    m_snapshotTimer = 0.f;

    const Player &pl = gs.player;
    QByteArray plData;
    QDataStream out(&plData, QIODevice::WriteOnly);
    out << (quint8)NET_INPUT
        << (qint32)m_localPlayerId
        << (float)pl.posX << (float)pl.posY << (float)pl.footZ
        << (float)pl.dirX << (float)pl.dirY
        << (float)pl.velX << (float)pl.velY
        << (qint32)pl.curWeapon
        << (qint32)pl.hp
        << (quint8)(pl.hp > 0 ? 1 : 0)
        << (quint8)(pl.attackCooldown > 0.05f ? 1 : 0)
        << (qint32)gs.shotsFired;
    sendTcpPacket(m_clientSocket, NET_INPUT, plData);
}

void NetworkManager::handleDeathDrop(int victimId, int killerId, bool headshot, GameState &gs)
{
    bool local = (victimId == m_localPlayerId);
    if (local) {
        if (m_localDeadReported) return;                    // two hits in the same frame still kill once
        m_localDeadReported = true;
    }
    // The loot comes from the machine that knows the real inventory: our own, or a bot on the host.
    // It is announced to everyone by syncPickups().
    Player *p = nullptr;
    if (local) p = &gs.player;
    else if (m_isHost) { for (auto &a : gs.actors) if (a.id == victimId && a.ctl == CTL_BOT) { p = &a; break; } }
    if (p) {
        int dropGun = p->curWeapon;
        if (dropGun < 0 || dropGun == WPN_KNIFE || !p->owned[dropGun])
            dropGun = p->slot[SLOT_PRIMARY2] >= 0 ? p->slot[SLOT_PRIMARY2] : p->slot[SLOT_PRIMARY];
        if (dropGun >= 0 && dropGun < WPN_COUNT && p->owned[dropGun]) {
            WeaponPickup wp;
            wp.x = p->posX; wp.y = p->posY;
            wp.bobPhase = float(rand() % 100) * 0.1f;
            wp.type = PICKUP_WEAPON;
            wp.weapon = dropGun;
            wp.load = p->load[dropGun];
            wp.mag = p->mag[dropGun];
            wp.reserve = p->reserve[dropGun];
            wp.reward = true;
            gs.weaponPickups.push_back(wp);
        }
        // ...and a medkit beside the body
        float kx = p->posX - p->dirY * 0.45f, ky = p->posY + p->dirX * 0.45f;
        if (m_activeWorld && !m_activeWorld->posValid(kx, ky, 0.1f)) { kx = p->posX; ky = p->posY; }
        HealthKit hk; hk.x = kx; hk.y = ky;
        gs.healthKits.push_back(hk);
    }
    if (m_isHost) creditKill(victimId, killerId, headshot);
    else if (local) {
        QVariantMap ev; ev["killer"] = killerId; ev["head"] = headshot;
        broadcastEvent(EV_DIED, ev);
    }
}

// Host: one death is scored once. Kill +100 (+150 headshot) to the killer and their team, -50 to the victim's team.
void NetworkManager::creditKill(int victimId, int killerId, bool head)
{
    if (!m_isHost) return;
    for (auto &sl : m_slots) if (sl.id == victimId) { if (!sl.alive) return; sl.alive = false; sl.deaths++; }
    int vt = std::max(0, teamOf(victimId));
    int kt = (killerId >= 0 && killerId != victimId) ? teamOf(killerId) : -1;
    int pts = head ? 150 : 100;
    if (kt >= 0 && kt != vt) {
        m_match.teamScore[kt] += pts;
        for (auto &sl : m_slots) if (sl.id == killerId) { sl.kills++; sl.score += pts; }
    } else killerId = -1;                                    // the map (acid, fire, a fall) or their own grenade
    m_match.teamScore[vt] = std::max(0, m_match.teamScore[vt] - 50);

    int weapon = -1;
    if (m_activeGs && killerId >= 0) {
        if (killerId == m_localPlayerId) weapon = m_activeGs->player.curWeapon;
        else for (const auto &a : m_activeGs->actors) if (a.id == killerId) weapon = a.curWeapon;
    }
    QVariantMap kev;
    kev["victim"] = victimId;
    kev["killer"] = killerId;
    kev["head"] = head;
    kev["weapon"] = weapon;
    broadcastEvent(EV_KILL, kev);
}

void NetworkManager::dealDamage(int shooterId, int targetId, int dmg, bool head)
{
    if (!m_isHost) {                                         // the host routes it
        QVariantMap ev; ev["target"] = targetId; ev["dmg"] = dmg; ev["head"] = head;
        broadcastEvent(EV_DAMAGE, ev);
        return;
    }
    if (shooterId != targetId && shooterId >= 0 && teamOf(shooterId) == teamOf(targetId)) return;   // no friendly fire
    if (targetId == m_localPlayerId) { applyLocalHit(dmg, shooterId, head); return; }
    if (!m_activeGs) return;
    for (auto &act : m_activeGs->actors) {
        if (act.id != targetId) continue;
        if (!act.alive) return;
        if (act.ctl == CTL_REMOTE) {                         // a human: their own machine applies it
            QVariantMap ev; ev["target"] = targetId; ev["dmg"] = dmg; ev["head"] = head; ev["shooter"] = shooterId;
            QByteArray pl; QDataStream out(&pl, QIODevice::WriteOnly);
            out << (quint8)NET_EVENT << (quint8)EV_DAMAGE << ev;
            sendTcpPacket(socketForId(targetId), NET_EVENT, pl);
            act.hitFlash = 0.2f;
            return;
        }
        int d = std::max(1, int(dmg * (1.f - act.damageReduction)));
        act.hp -= d; act.hitFlash = 0.2f;
        if (act.hp <= 0) {
            act.hp = 0; act.alive = false; act.respawnT = 5.f;
            act.velX = act.velY = 0.f;
            handleDeathDrop(act.id, shooterId, head, *m_activeGs);
        }
        return;
    }
}

void NetworkManager::applyLocalHit(int dmg, int shooterId, bool head)
{
    if (!m_activeGs) return;
    Player &pl = m_activeGs->player;
    if (pl.hp <= 0) return;
    int d = std::max(1, int(dmg * (1.f - pl.damageReduction)));
    pl.hp = std::max(0, pl.hp - d);
    pl.damageFlash = 0.45f;
    QVariantMap ev; ev["target"] = m_localPlayerId; ev["dmg"] = d; ev["shooter"] = shooterId; ev["head"] = head;
    emit matchEvent(EV_DAMAGE, ev);                          // the game plays the hurt sound / direction marker
    if (pl.hp <= 0) handleDeathDrop(m_localPlayerId, shooterId, head, *m_activeGs);
}

void NetworkManager::syncPickups(GameState &gs)
{
    if (!isMultiplayerActive()) return;
    auto newId = [&]() { return (m_localPlayerId + 1) * 100000 + (++m_pickupSeq % 100000); };
    for (auto &wp : gs.weaponPickups) {
        if (wp.active && wp.netId == 0) {
            wp.netId = newId();
            QVariantMap ev;
            ev["nid"] = wp.netId; ev["kit"] = false; ev["x"] = wp.x; ev["y"] = wp.y;
            ev["type"] = int(wp.type); ev["weapon"] = wp.weapon; ev["mag"] = wp.mag; ev["reserve"] = wp.reserve;
            ev["throw"] = wp.throwKind; ev["reward"] = wp.reward; ev["rarity"] = int(wp.load.rarity);
            QVariantList att; for (int a = 0; a < AT_COUNT; a++) att << int(wp.load.att[a]);
            ev["att"] = att;
            broadcastEvent(EV_SPAWN_PICKUP, ev);
        } else if (!wp.active && wp.netId > 0) {
            QVariantMap ev; ev["nid"] = wp.netId;
            broadcastEvent(EV_PICKUP, ev);
            wp.netId = -1;
        }
    }
    for (auto &k : gs.healthKits) {
        if (k.active && k.netId == 0) {
            k.netId = newId();
            QVariantMap ev; ev["nid"] = k.netId; ev["kit"] = true; ev["x"] = k.x; ev["y"] = k.y;
            broadcastEvent(EV_SPAWN_PICKUP, ev);
        } else if (!k.active && k.netId > 0) {
            QVariantMap ev; ev["nid"] = k.netId;
            broadcastEvent(EV_PICKUP, ev);
            k.netId = -1;
        }
    }
}

void NetworkManager::spawnFromEvent(const QVariantMap &d)
{
    if (!m_activeGs) return;
    int nid = d.value("nid").toInt();
    if (nid <= 0) return;
    for (const auto &wp : m_activeGs->weaponPickups) if (wp.netId == nid) return;
    for (const auto &k : m_activeGs->healthKits) if (k.netId == nid) return;
    float x = d.value("x").toFloat(), y = d.value("y").toFloat();
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    if (d.value("kit").toBool()) {
        HealthKit hk; hk.x = x; hk.y = y; hk.netId = nid;
        m_activeGs->healthKits.push_back(hk);
        return;
    }
    WeaponPickup wp;
    wp.x = x; wp.y = y; wp.netId = nid;
    wp.type = PickupType(std::max(0, std::min(d.value("type").toInt(), int(PICKUP_KEY))));
    int w = d.value("weapon").toInt();
    wp.weapon = (w >= 0 && w < WPN_COUNT) ? w : -1;
    if (wp.type == PICKUP_WEAPON && wp.weapon < 0) return;
    wp.mag = d.value("mag").toInt(); wp.reserve = d.value("reserve").toInt();
    wp.throwKind = std::max(0, std::min(d.value("throw").toInt(), TH_COUNT - 1));
    wp.reward = d.value("reward").toBool();
    wp.load.rarity = uint8_t(std::max(0, std::min(d.value("rarity").toInt(), RARITY_COUNT - 1)));
    QVariantList att = d.value("att").toList();
    for (int a = 0; a < AT_COUNT && a < att.size(); a++) wp.load.att[a] = uint8_t(std::max(0, std::min(att[a].toInt(), 7)));
    m_activeGs->weaponPickups.push_back(wp);
}

void NetworkManager::takeFromEvent(const QVariantMap &d)
{
    if (!m_activeGs) return;
    int nid = d.value("nid").toInt();
    if (nid <= 0) return;
    for (auto &wp : m_activeGs->weaponPickups) if (wp.netId == nid) { wp.active = false; wp.netId = -1; }
    for (auto &k : m_activeGs->healthKits) if (k.netId == nid) { k.active = false; k.netId = -1; }
}

int NetworkManager::teamOf(int id) const
{
    for (const auto &s : m_slots) if (s.id == id) return s.team == 1 ? 1 : 0;
    return -1;
}

QString NetworkManager::nameOf(int id) const
{
    for (const auto &s : m_slots) if (s.id == id) return s.name;
    return QString();
}

void NetworkManager::handleRespawn(int actorId, GameState &gs, const World &world, const ParsedMap &pm)
{
    (void)pm;
    int team = 0;
    CharClass cls = CLS_ASSAULT;
    for (const auto &s : m_slots) {
        if (s.id == actorId) { team = s.team; cls = s.cls; break; }
    }
    float baseX, baseY;
    teamSpawn(team, world, baseX, baseY);
    float sx = baseX, sy = baseY;
    float rx = baseX + float(rand() % 5 - 2) * 0.25f;
    float ry = baseY + float(rand() % 5 - 2) * 0.25f;
    if (world.posValid(rx, ry, 0.28f)) {
        sx = rx; sy = ry;
    }

    for (auto &sl : m_slots) if (sl.id == actorId) sl.alive = true;
    if (actorId == m_localPlayerId) {
        m_localDeadReported = false;
        gs.player.posX = sx; gs.player.posY = sy;
        gs.player.dirX = (team == 0) ? 1.f : -1.f;
        gs.player.dirY = 0.f;
        gs.player.planeX = -gs.player.dirY * 0.66f;
        gs.player.planeY = gs.player.dirX * 0.66f;
        gs.player.velX = gs.player.velY = gs.player.velZ = 0.f;
        gs.player.footZ = world.floorH(sx, sy);
        gs.player.onGround = true;
        gs.gameOver = false;
        initClassLoadout(gs.player, cls);
    } else {
        for (auto &a : gs.actors) {
            if (a.id == actorId) {
                a.posX = sx; a.posY = sy;
                a.dirX = (team == 0) ? 1.f : -1.f;
                a.dirY = 0.f;
                a.planeX = -a.dirY * 0.66f;
                a.planeY = a.dirX * 0.66f;
                a.velX = a.velY = a.velZ = 0.f;
                a.footZ = world.floorH(sx, sy);
                a.onGround = true;
                a.alive = true;
                a.respawnT = 5.f;
                initClassLoadout(a, cls);
                break;
            }
        }
    }
}

void NetworkManager::checkObjectiveInteractions(GameState &gs)
{
    if (!m_match.keyActive) return;

    // Key pickup: player walks over key
    if (m_match.keyCarrier < 0) {
        float dx = gs.player.posX - m_match.keyX, dy = gs.player.posY - m_match.keyY;
        if (std::sqrt(dx * dx + dy * dy) < 1.2f) {
            m_match.keyCarrier = m_localPlayerId;
            QVariantMap ev; ev["carrier"] = m_localPlayerId;
            broadcastEvent(EV_KEY_PICKED, ev);
        }
    }

    // Key delivery: carrier reaches Upper Terminal at North catwalk (~X: 23.5, Y: 5.5)
    if (m_match.keyCarrier == m_localPlayerId) {
        float tx = 23.5f, ty = 5.5f;
        float dx = gs.player.posX - tx, dy = gs.player.posY - ty;
        if (std::sqrt(dx * dx + dy * dy) < 1.8f) {
            m_match.keyActive = false;
            m_match.keyCarrier = -1;
            m_match.vaultUnlocked = true;

            int team = std::max(0, teamOf(m_localPlayerId));
            if (m_isHost) m_match.teamScore[team] += 500;   // +500 points for team (a client's is added by the host)

            // Spawn Vault power weapons
            WeaponPickup w1; w1.x = 22.5f; w1.y = 23.5f; w1.type = PICKUP_WEAPON; w1.weapon = WPN_RAILGUN; w1.reward = true;
            WeaponPickup w2; w2.x = 24.5f; w2.y = 23.5f; w2.type = PICKUP_WEAPON; w2.weapon = WPN_ROCKET;  w2.reward = true;
            gs.weaponPickups.push_back(w1);
            gs.weaponPickups.push_back(w2);

            QVariantMap ev; ev["team"] = team; ev["points"] = 500;
            broadcastEvent(EV_KEY_DELIVERED, ev);
        }
    }
}

void NetworkManager::broadcastEvent(NetEventType type, const QVariantMap &data)
{
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out << (quint8)NET_EVENT << (quint8)type << data;

    if (m_isHost) {
        broadcastTcpPacket(NET_EVENT, payload);
    } else if (m_clientSocket) {
        sendTcpPacket(m_clientSocket, NET_EVENT, payload);
    }
    emit matchEvent(type, data);
}

void NetworkManager::sendInput(const InputCmd &cmd, int curWeapon, float yaw, float pitch)
{
    (void)cmd; (void)curWeapon; (void)yaw; (void)pitch;
    // Broadcast via UDP or fast TCP
}

bool NetworkManager::runNetworkTest()
{
    // Automated in-process loopback test: Host on 17777, Client connects
    NetworkManager host;
    NetworkManager client;

    if (!host.startHost(17777, "TestHost")) return false;
    if (!client.connectToHost("127.0.0.1", 17777, "TestClient")) return false;

    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 1000 && client.playerSlots().size() < 2) {
        QCoreApplication::processEvents();
    }

    bool pass = (client.playerSlots().size() >= 2);
    host.disconnectSession();
    client.disconnectSession();
    QCoreApplication::processEvents();
    return pass;
}

