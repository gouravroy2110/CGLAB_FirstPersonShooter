// =============================================================================
//  commander.cpp  -  Radio commander: scripted voice call-outs during a fight
//
//  Watches the game for situations (low health, hazard, grenade at your feet,
//  fire from behind, flankers, snipers, streaks, a squadmate down, the last
//  hostiles, bosses, secrets, idling, stealth, zone start / clear) and queues a
//  line from the fixed script in radiolines.h. Priorities decide who may cut
//  in, cooldowns stop the radio from nagging, variants rotate.
//
//  Voices: every line ships with the game, compiled in from voices/ (made with
//  the Windows speech engine, see tools/bundle_voices.py). When a line plays,
//  its file runs through a radio filter written here (band-pass, overdrive,
//  hiss, squelch bursts; a ring modulator for VECTOR) and into the mixer.
//  A line missing from the bundle (say, one just added) is spoken by a
//  background PowerShell job into a local cache on Windows; until then, or
//  off Windows, that line shows as a subtitle only.
// =============================================================================
#include "mainwindow.h"
#include "missions.h"
#include "radiolines.h"
#include "audio.h"
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTextStream>
#include <cmath>
#include <algorithm>

using namespace rs;

namespace {
// Speech voice per speaker (missions.h order: Kade, Ortiz, VECTOR, Helix, Raster)
struct VoiceProfile { const char *voice; int rate; const char *lang; const char *pitch; bool robot; };
const VoiceProfile VOICES[SPK_COUNT] = {
    { "Microsoft David Desktop",  1, "en-US", "default", false },   // Major Kade
    { "Microsoft Hazel Desktop",  0, "en-GB", "default", false },   // Dr. Ortiz
    { "Microsoft David Desktop", -2, "en-US", "x-low",   true  },   // VECTOR
    { "Microsoft David Desktop", -1, "en-US", "low",     false },   // Aurelio Helix
    { "Microsoft Zira Desktop",   1, "en-US", "default", false },   // Raster
};

// RBJ biquad filters for the radio treatment
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float run(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    static Biquad make(bool high, float f, float sr) {
        float w = 2.f * 3.14159265f * f / sr, c = std::cos(w), al = std::sin(w) / (2.f * 0.7071f);
        float a0 = 1 + al;
        Biquad q;
        if (high) { q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; q.b2 = (1 + c) / 2 / a0; }
        else      { q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0;  q.b2 = (1 - c) / 2 / a0; }
        q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0;
        return q;
    }
};

// A 16-bit mono/stereo PCM WAV -> float samples (first channel) and its rate
bool readWav(const QString &path, std::vector<float> &out, int &rate)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray b = f.readAll();
    if (b.size() < 44 || !b.startsWith("RIFF") || b.mid(8, 4) != "WAVE") return false;
    int channels = 1, bits = 16;
    rate = 22050;
    int pos = 12;
    auto u16 = [&](int p) { return int(uint8_t(b[p])) | (int(uint8_t(b[p + 1])) << 8); };
    auto u32 = [&](int p) { return u16(p) | (u16(p + 2) << 16); };
    while (pos + 8 <= b.size()) {
        QByteArray id = b.mid(pos, 4);
        int len = u32(pos + 4);
        int body = pos + 8;
        if (id == "fmt ") { channels = std::max(1, u16(body + 2)); rate = u32(body + 4); bits = u16(body + 14); }
        else if (id == "data") {
            if (bits != 16) return false;
            int n = std::min(len, int(b.size()) - body) / (2 * channels);
            out.resize(size_t(n));
            for (int i = 0; i < n; i++) out[size_t(i)] = float(int16_t(u16(body + i * 2 * channels))) / 32768.f;
            return n > 0;
        }
        pos = body + len + (len & 1);
    }
    return false;
}

// Walkie-talkie treatment: resample to the mixer rate, band-pass, overdrive,
// sample-and-hold grit, hiss, squelch bursts at both ends
std::vector<float> radioTreat(const std::vector<float> &in, int rate, bool robot, uint32_t seed)
{
    const float SR = float(SoundEngine::RATE);
    size_t n = size_t(double(in.size()) * SR / rate);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; i++) {
        double p = double(i) * rate / SR;
        size_t k = size_t(p);
        float fr = float(p - double(k));
        float a = in[std::min(k, in.size() - 1)], c = in[std::min(k + 1, in.size() - 1)];
        x[i] = a + (c - a) * fr;
    }
    Biquad h1 = Biquad::make(true, 320.f, SR), h2 = h1, l1 = Biquad::make(false, 3000.f, SR), l2 = l1;
    Biquad nh = Biquad::make(true, 800.f, SR), nl = Biquad::make(false, 5000.f, SR);
    uint32_t r = seed | 1u;
    auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return float(r & 0xFFFF) / 32768.f - 1.f; };
    float hold = 0.f;
    for (size_t i = 0; i < n; i++) {
        float v = l2.run(l1.run(h2.run(h1.run(x[i]))));
        if (robot) v *= 0.55f + 0.45f * std::sin(2.f * 3.14159265f * 55.f * float(i) / SR);   // VECTOR's metallic ring
        v = std::tanh(v * 3.2f) * 0.75f;
        if ((i & 3) == 0) hold = v;                                  // ~11 kHz grit
        x[i] = hold + nl.run(nh.run(rnd() * 0.05f));
    }
    auto burst = [&](size_t len, bool click) {
        std::vector<float> b(len);
        Biquad bh = Biquad::make(true, 600.f, SR), bl = Biquad::make(false, 4500.f, SR);
        for (size_t i = 0; i < len; i++) {
            float env = 1.f - 0.8f * float(i) / float(len);
            b[i] = bl.run(bh.run(rnd() * 0.9f)) * env;
            if (click && i < 40) b[i] += 0.5f;
        }
        return b;
    };
    std::vector<float> out = burst(size_t(0.14f * SR), true);
    out.resize(out.size() + size_t(0.04f * SR), 0.f);
    out.insert(out.end(), x.begin(), x.end());
    out.resize(out.size() + size_t(0.04f * SR), 0.f);
    std::vector<float> tail = burst(size_t(0.11f * SR), false);
    out.insert(out.end(), tail.begin(), tail.end());
    float peak = 1e-4f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    for (float &v : out) v *= 0.85f / peak;
    return out;
}

QString xmlEscape(QString s)
{
    s.replace('&', "&amp;"); s.replace('<', "&lt;"); s.replace('>', "&gt;"); s.replace('"', "&quot;");
    return s;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Voice cache
// ─────────────────────────────────────────────────────────────────────────────
QString MainWindow::voiceKey(int speaker, const QString &text) const
{
    const VoiceProfile &v = VOICES[clampi(speaker, 0, SPK_COUNT - 1)];
    QByteArray k = QString("%1|%2|%3|%4").arg(v.voice).arg(v.rate).arg(v.pitch).arg(text).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(k, QCryptographicHash::Md5).toHex()) + ".wav";
}

// The lines worth having ready: the whole commander script (with every boss,
// hazard and squad name filled in) and the campaign's radio lines
void MainWindow::initVoices()
{
    if (m_testMode) return;
#ifndef Q_OS_WIN
    return;                                                     // the bundled voices still play; nothing to generate
#endif
    m_voiceDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/voice";
    QDir().mkpath(m_voiceDir);
    static const char *const SQUAD[] = { "HAWK", "VIPER", "ECHO", "GHOST", "TITAN", "STRIKER", "SPECTRE", "PHANTOM", "GOLIATH", "VALKYRIE", "RAZOR", "APEX" };
    for (int i = 0; i < RADIO_SCRIPT_COUNT; i++) {
        const RadioScriptLine &L = RADIO_SCRIPT[i];
        QString t(L.text);
        QStringList expanded;
        if (t.contains("{boss}")) { for (int k = 1; k < BK_COUNT; k++) if (!bossIsMinor(k)) expanded << QString(t).replace("{boss}", radioName(bossName(k))); }
        else if (t.contains("{hazard}")) { for (int k = 0; k < THEME_COUNT; k++) expanded << QString(t).replace("{hazard}", QString(THEMES[k].hazName).toLower()); }
        else if (t.contains("{name}")) { for (const char *n : SQUAD) expanded << QString(t).replace("{name}", radioName(n)); }
        else expanded << t;
        for (const QString &e : expanded) queueVoice(L.speaker, e);
    }
    for (int m = 0; m < MISSION_COUNT; m++) {
        for (int o = 0; o < MISSIONS[m].objCount; o++) {
            QString s(MISSIONS[m].obj[o].radio);
            if (s.isEmpty()) continue;
            int sp = SPK_KADE, colon = s.indexOf(':');
            if (colon > 0) {
                QString who = s.left(colon).trimmed();
                sp = who == "ORTIZ" ? SPK_ORTIZ : who == "VECTOR" ? SPK_VECTOR : who == "HELIX" ? SPK_HELIX : SPK_KADE;
                s = s.mid(colon + 1).trimmed();
            }
            queueVoice(sp, s);
        }
        queueVoice(MISSIONS[m].outro.speaker, MISSIONS[m].outro.text);
    }
    startVoiceJob();
}

// "THE WARDEN" -> "The Warden", "HAWK" -> "Hawk": speech engines read capitals as letters
QString MainWindow::radioName(const QString &s) const
{
    QString out = s.toLower();
    bool up = true;
    for (QChar &c : out) { if (up && c.isLetter()) { c = c.toUpper(); up = false; } if (c == ' ' || c == '-') up = true; }
    out.replace(':', ' ');
    return out.simplified();
}

void MainWindow::queueVoice(int speaker, const QString &text)
{
    if (m_voiceDir.isEmpty() || text.isEmpty()) return;
    QString key = voiceKey(speaker, text);
    if (QFile::exists(":/voices/" + key) || QFile::exists(m_voiceDir + "/" + key)) return;   // bundled or already made
    for (const VoiceJob &j : m_voicePending) if (j.key == key) return;
    m_voicePending.push_back(VoiceJob{speaker, text, key});
}

// One PowerShell process speaks a batch of lines into WAV files (written as .tmp and
// renamed, so the game never reads a half-written file)
void MainWindow::startVoiceJob()
{
    if (m_voiceProc || m_voicePending.empty() || m_voiceDir.isEmpty()) return;
    QString script = m_voiceDir + "/make_voices.ps1";
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << "Add-Type -AssemblyName System.Speech\n"
          "$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(22050, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)\n"
          "function Say($voice, $rate, $lang, $pitch, $text, $out) {\n"
          "  try {\n"
          "    $s = New-Object System.Speech.Synthesis.SpeechSynthesizer\n"
          "    try { $s.SelectVoice($voice) } catch {}\n"
          "    $s.Rate = $rate\n"
          "    $s.SetOutputToWaveFile($out + '.tmp', $fmt)\n"
          "    $s.SpeakSsml(\"<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='$lang'><prosody pitch='$pitch'>$text</prosody></speak>\")\n"
          "    $s.Dispose()\n"
          "    Move-Item -Force ($out + '.tmp') $out\n"
          "  } catch {}\n"
          "}\n";
    int n = 0;
    while (!m_voicePending.empty() && n < 120) {
        VoiceJob j = m_voicePending.front();
        m_voicePending.erase(m_voicePending.begin());
        const VoiceProfile &v = VOICES[clampi(j.speaker, 0, SPK_COUNT - 1)];
        QString text = xmlEscape(j.text);
        text.replace('\'', "''");                                   // PowerShell single-quoted string
        QString out = QDir::toNativeSeparators(m_voiceDir + "/" + j.key);
        out.replace('\'', "''");
        ts << "Say '" << v.voice << "' " << v.rate << " '" << v.lang << "' '" << v.pitch << "' '" << text << "' '" << out << "'\n";
        n++;
    }
    f.close();
    m_voiceProc = new QProcess(this);
    connect(m_voiceProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
        m_voiceProc->deleteLater();
        m_voiceProc = nullptr;
        startVoiceJob();                                            // anything asked for meanwhile
    });
    m_voiceProc->start("powershell", { "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", script });
}

// The playable clip for a line, or -1 (not generated yet: it is queued for next time)
int MainWindow::voiceClip(int speaker, const QString &text)
{
    if (!m_radioVoice || m_testMode || !m_snd || !m_snd->available()) return -1;
    QString key = voiceKey(speaker, text);
    auto it = m_voiceClips.find(key);
    if (it != m_voiceClips.end()) return it.value();
    std::vector<float> pcm; int rate = 22050;
    bool ok = readWav(":/voices/" + key, pcm, rate);                    // shipped with the game
    if (!ok && !m_voiceDir.isEmpty()) ok = readWav(m_voiceDir + "/" + key, pcm, rate);   // made on this PC
    if (!ok) { queueVoice(speaker, text); startVoiceJob(); return -1; }
    int h = m_snd->addClip(radioTreat(pcm, rate, VOICES[clampi(speaker, 0, SPK_COUNT - 1)].robot, uint32_t(qHash(key))));
    m_voiceClips.insert(key, h);
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
//  The commander
// ─────────────────────────────────────────────────────────────────────────────
// Queue a line for a situation if its cooldown and the channel allow it
void MainWindow::radioEvent(int ev, const QString &arg)
{
    if (ev < 0 || ev >= RE_COUNT || m_cmdCool[ev] > 0.f) return;
    const RadioEventDef &D = RADIO_EVENTS[ev];
    if (D.priority <= 1 && (m_cmdGap < 7.f || !m_radio.empty())) return;      // chatter needs a quiet channel
    if (D.priority == 2 && m_radio.size() > 1) return;
    // pick a variant (never the one used last time)
    int idx[16], n = 0;
    for (int i = 0; i < RADIO_SCRIPT_COUNT && n < 16; i++) if (RADIO_SCRIPT[i].event == ev) idx[n++] = i;
    if (!n) return;
    int pick = idx[rand() % n];
    if (n > 1 && pick == m_cmdLast[ev]) pick = idx[(std::find(idx, idx + n, pick) - idx + 1 + rand() % (n - 1)) % n];
    m_cmdLast[ev] = pick;
    const RadioScriptLine &L = RADIO_SCRIPT[pick];
    QString text(L.text);
    text.replace("{boss}", radioName(arg)).replace("{hazard}", arg.toLower()).replace("{name}", radioName(arg));
    m_cmdCool[ev] = D.cooldown;
    m_cmdGap = 0.f;
    m_cmdFired++;
    RadioMsg msg(L.speaker, text, std::max(2.6f, 0.9f + text.size() * 0.055f));
    msg.prio = D.priority;
    if (D.priority >= 3 && !m_radio.empty() && m_radio.front().prio < 3) {
        // urgent: cut the current line
        if (m_radio.front().started && m_snd) m_snd->stopClips();
        m_radio.erase(m_radio.begin());
        m_radio.insert(m_radio.begin(), msg);
        return;
    }
    if (D.priority >= 3) m_radio.insert(m_radio.begin(), msg); else m_radio.push_back(msg);
}

void MainWindow::updateCommander(float dt)
{
    const Player &pl = m_gs.player;
    for (float &c : m_cmdCool) c = std::max(0.f, c - dt);
    m_cmdGap += dt;
    bool mp = m_gameMode == MODE_MULTIPLAYER;
    if (!m_campaignRun) updateRadio(dt);                                // the campaign drives its own radio clock
    if (m_gs.gameOver || m_gs.levelComplete) {
        if (m_gs.levelComplete && !m_cmdPrevZoneDone && !m_campaignRun && !mp) radioEvent(RE_ZONE_CLEAR);
        m_cmdPrevZoneDone = m_gs.levelComplete;
        return;
    }
    m_cmdPrevZoneDone = false;
    // a new zone (free play): a line once the player has had a second to look around
    if (m_cmdZoneStart) { m_cmdZoneStartT += dt; if (m_cmdZoneStartT > 1.5f) { m_cmdZoneStart = false; if (!m_campaignRun && !mp) radioEvent(RE_ZONE_START); } }
    // health
    int mhp = maxHP();
    if (pl.hp > 0 && pl.hp < mhp * 3 / 10 && !m_cmdWasLowHp) { m_cmdWasLowHp = true; radioEvent(RE_LOW_HEALTH); }
    if (pl.hp > mhp / 2) m_cmdWasLowHp = false;
    // hazard underfoot
    if (m_acidFlash > 0.9f) radioEvent(RE_HAZARD, QString(m_curHazardName ? m_curHazardName : "acid"));
    // ammo
    int w = pl.curWeapon;
    if (w >= 0 && usesAmmo(w) && pl.reserve[w] == 0 && pl.mag[w] <= std::max(1, WEAPONS[w].magSize / 4)) {
        if (m_cmdAmmoWarnW != w) { m_cmdAmmoWarnW = w; radioEvent(RE_LOW_AMMO); }
    } else if (w >= 0 && usesAmmo(w) && pl.reserve[w] > 0) m_cmdAmmoWarnW = -1;
    // flags raised elsewhere
    if (m_cmdHitBehind) { m_cmdHitBehind = false; radioEvent(RE_HIT_BEHIND); }
    if (m_cmdLongShot) { m_cmdLongShot = false; radioEvent(RE_LONG_SHOT); }
    if (m_cmdSecret) { m_cmdSecret = false; radioEvent(RE_SECRET); }
    if (m_cmdRespawn) { m_cmdRespawn = false; radioEvent(RE_RESPAWN); }
    // a grenade landing at your feet
    for (const Projectile &p : m_gs.projectiles)
        if (p.alive && p.owner == PO_ENEMY && p.kind == PJ_GRENADE && p.fuse > 0.3f &&
            (p.landX - pl.posX) * (p.landX - pl.posX) + (p.landY - pl.posY) * (p.landY - pl.posY) < 9.f) { radioEvent(RE_GRENADE); break; }
    // flankers, snipers, bosses
    float vx = pl.dirX, vy = pl.dirY;
    int alive = 0;
    if (m_cmdBossPhase.size() != m_gs.enemies.size()) m_cmdBossPhase.assign(m_gs.enemies.size(), -1);
    for (int i = 0; i < int(m_gs.enemies.size()); i++) {
        const Enemy &e = m_gs.enemies[size_t(i)];
        if (!e.alive) continue;
        alive++;
        float dx = e.x - pl.posX, dy = e.y - pl.posY, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
        float facing = (dx * vx + dy * vy) / d;                          // 1 ahead, -1 behind
        if (e.state == ES_ALERT && e.role == 2 && d < 10.f && facing < 0.3f) radioEvent(RE_FLANKED);
        if (e.type == ET_SNIPER && e.state == ES_ALERT && d > 12.f && e.shootAnim > 0.f) radioEvent(RE_SNIPER);
        if (e.bossKind && !bossIsMinor(e.bossKind) && !e.decoy) {
            int &seen = m_cmdBossPhase[size_t(i)];
            if (seen < 0 && !m_campaignRun) radioEvent(RE_BOSS_APPEAR, bossName(e.bossKind));
            else if (seen >= 0 && e.phase != seen) radioEvent(RE_BOSS_PHASE, bossName(e.bossKind));
            seen = e.phase;
        }
    }
    // the last hostiles (not in multiplayer, where there are none)
    if (!mp && !m_gs.enemies.empty()) {
        if (alive == 2 && m_cmdPrevAlive > 2) radioEvent(RE_TWO_LEFT);
        if (alive == 1 && m_cmdPrevAlive > 1) radioEvent(RE_LAST_ONE);
    }
    m_cmdPrevAlive = alive;
    // streaks
    if (m_combo >= 5 && m_cmdPrevCombo < 5) radioEvent(RE_STREAK);
    m_cmdPrevCombo = m_combo;
    // a squadmate falls
    if (m_cmdActorAlive.size() != m_gs.actors.size()) { m_cmdActorAlive.assign(m_gs.actors.size(), 1); }
    for (size_t i = 0; i < m_gs.actors.size(); i++) {
        const Actor &a = m_gs.actors[i];
        if (m_cmdActorAlive[i] && !a.alive && a.team == 0 && !a.escort && !mp) radioEvent(RE_SQUAD_DOWN, QString(a.name));
        m_cmdActorAlive[i] = a.alive ? 1 : 0;
    }
    // campaign: stealth alarm rising, idling away from the objective
    if (m_campaignRun) {
        if (m_alarm > 0.5f && m_cmdPrevAlarm <= 0.5f) radioEvent(RE_STEALTH);
        m_cmdPrevAlarm = m_alarm;
        bool busy = isPlayerMoving() || m_lmb || m_gs.kills != m_cmdIdleKills;
        m_cmdIdleKills = m_gs.kills;
        m_cmdIdleT = busy ? 0.f : m_cmdIdleT + dt;
        if (m_cmdIdleT > 45.f) { m_cmdIdleT = 0.f; radioEvent(RE_IDLE); }
    }
}
