// =============================================================================
//  audio.h  -  Procedural sound effects + real-time software mixer
//
//  Like the graphics, no asset files are loaded: every sound is synthesised
//  at start-up from oscillators, filtered noise and envelopes. A small mixer
//  (a QIODevice pulled by QAudioSink) plays any number of overlapping voices
//  with per-voice gain, stereo pan, pitch and start delay.
// =============================================================================
#pragma once
#include <QIODevice>
#include <QAudioFormat>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

class QAudioSink;

enum SoundId {
    SND_PISTOL, SND_SHOTGUN, SND_RIFLE, SND_KNIFE_SWING, SND_KNIFE_HIT,
    SND_DRY_FIRE, SND_MAG_OUT, SND_MAG_IN, SND_BOLT, SND_PUMP, SND_SHELL_IN, SND_SWITCH,
    SND_CASING, SND_SHELL_DROP,
    SND_STEP, SND_JUMP, SND_LAND,
    SND_PLAYER_HURT, SND_BULLET_WHIZ,
    SND_ENEMY_RIFLE, SND_ENEMY_PISTOL, SND_ENEMY_SHOTGUN, SND_ENEMY_SNIPER,
    SND_HIT_FLESH, SND_HITMARK, SND_HEADSHOT, SND_KILL, SND_ENEMY_DEATH, SND_ENEMY_ALERT,
    SND_IMPACT, SND_RICOCHET,
    SND_PICKUP_AMMO, SND_PICKUP_HEALTH, SND_PICKUP_WEAPON, SND_INTEL,
    SND_DOOR, SND_SECRET, SND_ACID, SND_HEARTBEAT,
    SND_LEVEL_CLEAR, SND_UPGRADE, SND_DEPLOY, SND_GAME_OVER, SND_RADIO,
    SND_UI_MOVE, SND_UI_SELECT, SND_UI_BACK, SND_SCOPE_IN, SND_ZOOM_TICK,
    SND_AMB_WIND, SND_AMB_DUNGEON, SND_AMB_CRYPT,
    // arsenal
    SND_REVOLVER, SND_SMG, SND_LMG, SND_SNIPER, SND_DMR, SND_DOUBLE_BARREL, SND_SUPPRESSED,
    SND_ROCKET_FIRE, SND_GL_THUMP, SND_EXPLOSION, SND_PLASMA, SND_PLASMA_CHARGE, SND_ARC,
    SND_RAIL_CHARGE, SND_RAIL_FIRE, SND_FLAME, SND_OVERHEAT, SND_MELEE_HEAVY, SND_BATON_HIT,
    SND_GRENADE_PIN, SND_GRENADE_BOUNCE, SND_FLASHBANG, SND_EAR_RING, SND_SMOKE_HISS, SND_FIRE_BURN,
    SND_TURRET_FIRE, SND_RADAR_PING, SND_JETPACK, SND_GRAPPLE, SND_TAKEDOWN, SND_INSPECT,
    SND_COUNT
};

class SoundEngine : public QIODevice {
public:
    explicit SoundEngine(QObject *parent = nullptr);
    ~SoundEngine() override;

    bool start();                 // open the default output; false if no device
    bool available() const { return m_sink != nullptr; }

    // Fire-and-forget. pan -1 (left) .. +1 (right); pitch 1 = original speed.
    void play(SoundId id, float gain = 1.f, float pan = 0.f, float pitch = 1.f, float delaySec = 0.f);
    // Looping ambience (one at a time); id < 0 stops it
    void setAmbience(int id, float gain);
    // Sounds made at run time (radio voices): mono at RATE. addClip returns a handle.
    int  addClip(std::vector<float> samples);
    void playClip(int handle, float gain);
    void stopClips();                          // cut whatever clip is playing (a more urgent call)
    float clipSeconds(int handle) const;
    void setMuted(bool m) { m_muted = m; }
    bool muted() const { return m_muted; }
    void setMasterVolume(float v) { m_master = v; }
    long long bytesServed() const { return m_served; }

    const std::vector<float> &buffer(SoundId id) const { return m_bank[id]; }
    static constexpr int RATE = 44100;

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return 0; }
    qint64 bytesAvailable() const override { return 1 << 20; }
    bool   isSequential() const override { return true; }

private:
    struct Voice { const std::vector<float> *buf; double pos; float step, gainL, gainR; int delay; bool loop; bool ambient; bool clip = false; };
    std::vector<float> m_bank[SND_COUNT];
    std::vector<std::unique_ptr<std::vector<float>>> m_clips;   // stable addresses: voices point into them
    std::vector<Voice> m_voices;
    std::mutex m_lock;
    QAudioSink *m_sink = nullptr;
    QAudioFormat m_fmt;
    float m_master = 0.8f;
    bool  m_muted = false;
    float m_ambGain = 0.f;
    std::atomic<long long> m_served{0};
    void synthesize();
};
