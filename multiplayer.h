// =============================================================================
//  multiplayer.h  -  Tactical Team Multiplayer: Lobby, Networking & Rules
//
//  Sector War Game Mode:
//    - 1v1, 2v2, 3v3, or 4v4 Symmetrical Team Battles (Team Alpha vs Bravo)
//    - Class Starter Kits (Assault, Scout, Tank)
//    - High-Stakes Death Drops (dropped weapon retains ammo & attachments)
//    - Timed Battlefield Power Weapons (Catwalks: Snipers, Courtyard: Heavy,
//      Sub-level Vault: Railgun / RPG / Plasma unlocked via Golden Key)
//    - Timed Supply Stations (Medkits & Ammo crates)
//    - Network Protocol: TCP (Lobby & Events) + UDP (High-speed Snapshot Sync)
//      Compatible with LAN, Direct IP, and Playit.gg tunnels
// =============================================================================
#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostAddress>
#include <QByteArray>
#include <QDataStream>
#include <QVariantMap>
#include <vector>
#include <QString>
#include "game.h"
#include "weapons.h"
#include "world.h"

static const quint16 MP_DEFAULT_PORT = 7777;
static const int MP_MAX_PLAYERS = 8;

enum MpState {
    MP_OFFLINE = 0,
    MP_LOBBY,
    MP_WARMUP,
    MP_PLAYING,
    MP_ROUND_END,
    MP_MATCH_END
};

// --- Network Packet Types ----------------------------------------------------
enum NetMsgType : quint8 {
    NET_JOIN_REQ        = 1,
    NET_JOIN_ACK        = 2,
    NET_LOBBY_UPDATE    = 3,
    NET_SET_TEAM        = 4,
    NET_SET_CLASS       = 5,
    NET_SET_READY       = 6,
    NET_UPDATE_SETTINGS = 7,
    NET_START_MATCH     = 8,
    NET_INPUT           = 9,
    NET_SNAPSHOT        = 10,
    NET_EVENT           = 11,
    NET_PING            = 12,
    NET_PONG            = 13,
    NET_SET_NAME        = 14
};

// --- Game Events -------------------------------------------------------------
enum NetEventType : quint8 {
    EV_KILL           = 1,
    EV_DEATH_DROP     = 2,
    EV_PICKUP         = 3,
    EV_RESPAWN        = 4,
    EV_ROUND_START    = 5,
    EV_ROUND_END      = 6,
    EV_MATCH_END      = 7,
    EV_KEY_SPAWNED    = 8,
    EV_KEY_PICKED     = 9,
    EV_KEY_DELIVERED  = 10,
    EV_CHAT           = 11,
    EV_DAMAGE         = 12
};

// --- Operative Starter Loadouts ----------------------------------------------
inline void initClassLoadout(Player &pl, CharClass cls) {
    for (int w = 0; w < WPN_COUNT; w++) { pl.owned[w] = false; pl.mag[w] = 0; pl.reserve[w] = 0; pl.load[w] = Loadout{}; }
    pl.slot[SLOT_MELEE] = WPN_KNIFE; pl.owned[WPN_KNIFE] = true;
    pl.slot[SLOT_PRIMARY2] = -1; // Empty Slot 4 for power weapons and battlefield drops

    if (cls == CLS_SCOUT) {
        pl.hp = 75; pl.stamina = 150.f; pl.speedMult = 1.4f; pl.damageReduction = 0.0f;
        pl.slot[SLOT_SIDEARM] = WPN_MACHINE_PISTOL; pl.owned[WPN_MACHINE_PISTOL] = true;
        pl.mag[WPN_MACHINE_PISTOL] = 20; pl.reserve[WPN_MACHINE_PISTOL] = 80;
        pl.slot[SLOT_PRIMARY] = WPN_SMG; pl.owned[WPN_SMG] = true; // Vector SMG
        pl.mag[WPN_SMG] = 30; pl.reserve[WPN_SMG] = 120;
        pl.throwSel = TH_SMOKE; pl.throwCount[TH_SMOKE] = 2;
        pl.curWeapon = WPN_SMG;
    } else if (cls == CLS_TANK) {
        pl.hp = 200; pl.stamina = 80.f; pl.speedMult = 0.7f; pl.damageReduction = 0.5f;
        pl.slot[SLOT_SIDEARM] = WPN_REVOLVER; pl.owned[WPN_REVOLVER] = true; // .44 Magnum
        pl.mag[WPN_REVOLVER] = 6; pl.reserve[WPN_REVOLVER] = 36;
        pl.slot[SLOT_PRIMARY] = WPN_SHOTGUN; pl.owned[WPN_SHOTGUN] = true; // M870
        pl.mag[WPN_SHOTGUN] = 8; pl.reserve[WPN_SHOTGUN] = 32;
        pl.throwSel = TH_MOLOTOV; pl.throwCount[TH_MOLOTOV] = 1;
        pl.curWeapon = WPN_SHOTGUN;
    } else { // CLS_ASSAULT (Default)
        pl.hp = 100; pl.stamina = 100.f; pl.speedMult = 1.0f; pl.damageReduction = 0.0f;
        pl.slot[SLOT_SIDEARM] = WPN_PISTOL; pl.owned[WPN_PISTOL] = true; // M9
        pl.mag[WPN_PISTOL] = 15; pl.reserve[WPN_PISTOL] = 60;
        pl.slot[SLOT_PRIMARY] = WPN_RIFLE; pl.owned[WPN_RIFLE] = true; // M4 Carbine
        pl.mag[WPN_RIFLE] = 30; pl.reserve[WPN_RIFLE] = 120;
        pl.throwSel = TH_FRAG; pl.throwCount[TH_FRAG] = 2;
        pl.curWeapon = WPN_RIFLE;
    }
}

// --- Player Slot in Lobby & Match --------------------------------------------
struct MpPlayerSlot {
    int       id = -1;
    QString   name = "Operative";
    int       team = 0;           // 0: Alpha (Red), 1: Bravo (Blue)
    CharClass cls = CLS_ASSAULT;
    bool      ready = false;
    bool      isHost = false;
    bool      isBot = false;
    int       ping = 0;
    int       kills = 0;
    int       deaths = 0;
    int       score = 0;
    int       livesLeft = 5;
    float     respawnTimer = 0.f;
    bool      alive = true;
};

// --- Room Settings -----------------------------------------------------------
struct MpRoomSettings {
    int     teamSize = 3;         // 1..4 (1v1, 2v2, 3v3, 4v4)
    int     roundDuration = 300;  // 180..600 seconds (3 to 10 minutes)
    int     roundsToWin = 2;      // Best of 3
    int     respawnLimit = 5;     // 3, 4, 5, or -1 (Infinite)
    int     mapChoice = 6;        // Map index (6 = The Citadel)
    bool    botFill = true;       // Automatically populate empty slots with bots
    QString roomCode = "ALPHA-7";
};

// --- Match Live State --------------------------------------------------------
struct MpMatchState {
    int   currentRound = 1;
    float roundTimer = 300.f;
    int   teamScore[2] = {0, 0};
    int   roundsWon[2] = {0, 0};
    bool  inRound = false;
    bool  roundOver = false;
    bool  matchOver = false;
    int   winningTeam = -1;
    int   roundWinner = -1;       // 0: Alpha, 1: Bravo, -1: Draw
    float intermissionTimer = 0.f;

    // Encryption Key Objective
    bool  keyActive = false;
    float keySpawnTimer = 120.f;  // Spawns 2 minutes into round
    float keyX = 23.5f, keyY = 22.5f, keyZ = -1.8f;
    int   keyCarrier = -1;        // Actor ID holding the key, -1 if on ground
    bool  vaultUnlocked = false;

    // Timed Power Weapon Pads
    float catwalkWeaponTimer = 60.f;
    float courtyardWeaponTimer = 60.f;

    // Timed Supply Stations
    float medkitTimer1 = 35.f, medkitTimer2 = 35.f;
    float ammoTimer1 = 25.f, ammoTimer2 = 25.f;
};

// --- Network Manager Class ---------------------------------------------------
class NetworkManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager() override;

    // Session Management
    bool startHost(quint16 port = MP_DEFAULT_PORT, const QString &hostName = "Host");
    bool connectToHost(const QString &hostAddress, quint16 port = MP_DEFAULT_PORT, const QString &playerName = "Player");
    void disconnectSession();

    bool isHost() const { return m_isHost; }
    bool isConnected() const { return m_connected; }
    bool isMultiplayerActive() const { return m_state == MP_PLAYING || m_state == MP_ROUND_END; }
    MpState state() const { return m_state; }
    int  localPlayerId() const { return m_localPlayerId; }

    const MpRoomSettings &settings() const { return m_settings; }
    MpRoomSettings &settingsMutable() { return m_settings; }
    const std::vector<MpPlayerSlot> &playerSlots() const { return m_slots; }
    const MpMatchState &matchState() const { return m_match; }
    MpMatchState &matchStateMutable() { return m_match; }

    // Lobby Actions
    void setTeam(int team);
    void setClass(CharClass cls);
    void setReady(bool ready);
    void setPlayerName(const QString &name);
    void broadcastLobbyState();
    void updateSettings(const MpRoomSettings &s);
    void startMatch();
    void kickSlot(int slotId);
    void cycleTeamSize();
    void toggleBotFill();
    void cycleDuration();
    void cycleRoundsToWin();
    void cycleMap();

    // In-Game Transmission
    void sendInput(const InputCmd &cmd, int curWeapon, float yaw, float pitch);
    void broadcastEvent(NetEventType type, const QVariantMap &data);

    // Simulation Ticks
    void tickServer(float dt, GameState &gs, World &world, const ParsedMap &pm);
    void tickClient(float dt, GameState &gs, World &world);

    // Game Mode Event Helpers
    void handleDeathDrop(int victimId, int killerId, bool headshot, GameState &gs);
    void handleRespawn(int actorId, GameState &gs, const World &world, const ParsedMap &pm);
    void checkObjectiveInteractions(GameState &gs);
    void resetRound(GameState &gs, World &world, const ParsedMap &pm);

    // Automated loopback test
    static bool runNetworkTest();

signals:
    void lobbyUpdated();
    void matchStarted();
    void matchEvent(NetEventType type, const QVariantMap &data);
    void connectionError(const QString &err);

private slots:
    void onNewConnection();
    void onClientData();
    void onClientDisconnected();
    void onServerData();
    void onServerDisconnected();
    void onUdpData();

private:
    bool             m_isHost = false;
    bool             m_connected = false;
    MpState          m_state = MP_OFFLINE;
    int              m_localPlayerId = 0;
    MpRoomSettings   m_settings;
    std::vector<MpPlayerSlot> m_slots;
    MpMatchState     m_match;

    GameState       *m_activeGs = nullptr;
    World           *m_activeWorld = nullptr;

    // Networking handles
    QTcpServer      *m_tcpServer = nullptr;
    QTcpSocket      *m_clientSocket = nullptr;     // For client mode
    std::vector<QTcpSocket*> m_clientSockets;      // For host mode
    QUdpSocket      *m_udpSocket = nullptr;
    quint16          m_port = MP_DEFAULT_PORT;
    QHostAddress     m_serverAddr;
    // Each client socket keeps the player id it was given on connect (ids never shift)
    std::vector<std::pair<QTcpSocket*, int>> m_sockIds;
    int              m_nextClientId = 1;
    int              idForSocket(QTcpSocket *s) const;
    QTcpSocket      *socketForId(int id) const;
    bool             isCitadel() const { return m_settings.mapChoice == 6; }
    void             teamSpawn(int team, const World &world, float &x, float &y) const;

    float            m_snapshotTimer = 0.f;
    float            m_pingTimer = 0.f;

    void processTcpPacket(const QByteArray &data, QTcpSocket *fromSocket = nullptr);
    void sendTcpPacket(QTcpSocket *sock, NetMsgType type, const QByteArray &payload);
    void broadcastTcpPacket(NetMsgType type, const QByteArray &payload);
    void sendUdpPacket(const QByteArray &data, const QHostAddress &addr, quint16 port);

    void balanceTeamsAndFillBots();
};
