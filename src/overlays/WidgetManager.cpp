#include "overlays/WidgetManager.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "platforms/PlatformHub.h"
#include "app/Theme.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <complex>
#include <media-io/audio-io.h>

namespace bc {

static void audioCapture(void *param, obs_source_t *, const audio_data *audio, bool muted);

WidgetManager &WidgetManager::instance() {
    static WidgetManager w;
    return w;
}

QStringList WidgetManager::alertTypes() {
    return {"follow", "subscribe", "gift", "raid", "cheer", "donation", "membership", "redemption"};
}

bool WidgetManager::isAnimated(const WidgetConfig &cfg) {
    static const QSet<QString> animated = {"ticker", "chat_marquee", "banner", "ambient", "candles", "voice_sigil", "audio_visualizer",
                                           "lower_third", "slideshow", "on_air", "combo", "reminder"};
    return animated.contains(cfg.kind);
}

void WidgetManager::init() {
    QFile f(":/overlays/packs.json");
    f.open(QIODevice::ReadOnly);
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto v : root.value("packs").toArray()) {
        const QJsonObject o = v.toObject();
        const QJsonArray bg = o.value("bg").toArray();
        m_packs << OverlayPack{o.value("id").toString(), o.value("name").toString(), o.value("glyph").toString(),
                               o.value("font").toString(), o.value("particles").toString("bats"),
                               QColor(bg.at(0).toString()), QColor(bg.at(1).toString()), QColor(o.value("accent").toString()),
                               QColor(o.value("metal").toString("#B8935A")), QColor(o.value("text").toString())};
    }
    m_defaultTemplates = root.value("alertTemplates").toObject();

    auto &reg = WidgetRegistry::instance();
    connect(&reg, &WidgetRegistry::widgetsChanged, this, [this] {
        const auto srcs = WidgetRegistry::instance().sources();
        for (obs_source_t *s : srcs) {
            if (!m_timerStart.contains(s)) m_timerStart.insert(s, QDateTime::currentMSecsSinceEpoch());
            renderWidget(s);
        }
        // forget state of widgets that were removed
        for (auto it = m_chatState.begin(); it != m_chatState.end();) it = srcs.contains(it.key()) ? std::next(it) : m_chatState.erase(it);
        for (auto it = m_slides.begin(); it != m_slides.end();) it = srcs.contains(it.key()) ? std::next(it) : m_slides.erase(it);
        for (auto it = m_timerStart.begin(); it != m_timerStart.end();) it = srcs.contains(it.key()) ? std::next(it) : m_timerStart.erase(it);
        syncAudioTaps();
    });
    connect(&reg, &WidgetRegistry::widgetConfigChanged, this, [this](obs_source_t *s) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (c.kind == "subathon" && c.subathonEnd == 0) restartWidget(s);   // start a new subathon clock
        renderWidget(s);
        syncAudioTaps();
    });

    auto &hub = PlatformHub::instance();
    connect(&hub, &PlatformHub::streamEvent, this, &WidgetManager::onEvent);
    connect(&hub, &PlatformHub::chatMessage, this, &WidgetManager::onChat);
    auto &e = Engine::instance();
    connect(&e, &Engine::streamingStateChanged, this, [this] {
        if (Engine::instance().isStreaming() && m_streamStartMs == 0) { m_streamStartMs = QDateTime::currentMSecsSinceEpoch(); resetSession(); }
        if (!Engine::instance().isStreaming()) m_streamStartMs = 0;
        refreshAll();
    });
    connect(&e, &Engine::recordingStateChanged, this, [this] {
        if (Engine::instance().isRecording() && m_recStartMs == 0) m_recStartMs = QDateTime::currentMSecsSinceEpoch();
        if (!Engine::instance().isRecording()) m_recStartMs = 0;
        refreshAll();
    });
    connect(&e, &Engine::programSceneChanged, this, [this] { forEachOfKind("scene_name", [this](obs_source_t *s) { renderWidget(s); }); });

    connect(&m_secondTimer, &QTimer::timeout, this, &WidgetManager::secondTick);
    m_secondTimer.start(1000);
    m_animTimer.setInterval(33);
    connect(&m_animTimer, &QTimer::timeout, this, &WidgetManager::animateTick);
}

void WidgetManager::shutdown() {
    m_secondTimer.stop();
    m_animTimer.stop();
    for (auto &tap : m_taps) {
        OBSSourceAutoRelease s = obs_weak_source_get_source(tap->weak);
        if (s) obs_source_remove_audio_capture_callback(s, audioCapture, tap.get());
    }
    m_taps.clear();
    if (m_sound) obs_set_output_source(6, nullptr);
    m_sound = nullptr;
}

OverlayPack WidgetManager::pack(const QString &id) const {
    for (const auto &p : m_packs) if (p.id == id) return p;
    // Unknown ids (e.g. packs from older versions) fall back to the first pack.
    return m_packs.isEmpty() ? OverlayPack{"nocturne", "Nocturne", QStringLiteral("☾"), "Cinzel", "bats", theme::Obsidian, theme::Void,
                                           theme::Crimson, theme::OldGold, theme::Bone}
                             : m_packs.first();
}

AlertRule WidgetManager::rule(const QString &type) const {
    auto &s = Settings::instance();
    AlertRule r;
    r.enabled = s.get("alerts." + type + ".enabled", true).toBool();
    r.minAmount = s.get("alerts." + type + ".min", 0).toDouble();
    r.templ = s.get("alerts." + type + ".template", m_defaultTemplates.value(type).toString("{user}")).toString();
    return r;
}

void WidgetManager::setRule(const QString &type, const AlertRule &r) {
    auto &s = Settings::instance();
    s.set("alerts." + type + ".enabled", r.enabled);
    s.set("alerts." + type + ".min", r.minAmount);
    s.set("alerts." + type + ".template", r.templ);
    s.save();
}

void WidgetManager::restartTimers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_timerStart.begin(); it != m_timerStart.end(); ++it) it.value() = now;
    refreshAll();
}

void WidgetManager::refreshAll() { for (obs_source_t *s : WidgetRegistry::instance().sources()) renderWidget(s); }

void WidgetManager::forEachOfKind(const QString &kind, const std::function<void(obs_source_t *)> &fn) {
    for (obs_source_t *s : WidgetRegistry::instance().sources())
        if (WidgetRegistry::instance().config(s).kind == kind) fn(s);
}

qint64 WidgetManager::widgetStartMs(obs_source_t *src) const {
    return src ? m_timerStart.value(src, QDateTime::currentMSecsSinceEpoch()) : QDateTime::currentMSecsSinceEpoch() - 37000;
}

void WidgetManager::resetSession() {
    m_session = Session{};
    m_chatState.clear();
    refreshAll();
}

// ---------------------------------------------------------------- persistence of widget values
void WidgetManager::writeSettings(obs_source_t *src, const std::function<void(obs_data_t *)> &fn) {
    if (!src || !WidgetRegistry::instance().sources().contains(src)) return;
    OBSDataAutoRelease st = obs_source_get_settings(src);
    fn(st);
    obs_source_update(src, nullptr);   // libobs re-reads its (now modified) settings → registry → re-render
}

void WidgetManager::adjustCounter(obs_source_t *src, int dv, int dv2) {
    writeSettings(src, [&](obs_data_t *st) {
        obs_data_set_int(st, "value", obs_data_get_int(st, "value") + dv);
        obs_data_set_int(st, "value2", qMax<long long>(0, obs_data_get_int(st, "value2") + dv2));
    });
}

void WidgetManager::resetCounter(obs_source_t *src) {
    writeSettings(src, [](obs_data_t *st) { obs_data_set_int(st, "value", 0); obs_data_set_int(st, "value2", 0); });
}

void WidgetManager::restartWidget(obs_source_t *src) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_timerStart[src] = now;
    const WidgetConfig c = WidgetRegistry::instance().config(src);
    if (c.kind == "subathon")
        writeSettings(src, [&](obs_data_t *st) { obs_data_set_int(st, "subathon_end", now + qint64(qMax(1, c.countdownMinutes)) * 60000); });
    renderWidget(src);
}

void WidgetManager::addSubathonSeconds(obs_source_t *src, int seconds) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const WidgetConfig c = WidgetRegistry::instance().config(src);
    qint64 end = c.subathonEnd > 0 ? c.subathonEnd : now + qint64(qMax(1, c.countdownMinutes)) * 60000;
    if (end < now) end = now;   // a finished subathon restarts from zero
    writeSettings(src, [&](obs_data_t *st) { obs_data_set_int(st, "subathon_end", end + qint64(seconds) * 1000); });
}

void WidgetManager::drawGiveawayWinner(obs_source_t *src) {
    ChatWidgetState &st = m_chatState[src];
    QStringList pool = st.entrants;
    pool.removeAll(st.winner);
    if (pool.isEmpty()) pool = st.entrants;
    if (!pool.isEmpty()) st.winner = pool.at(int(QRandomGenerator::system()->bounded(quint32(pool.size()))));
    renderWidget(src);
}

void WidgetManager::nextQuestion(obs_source_t *src) {
    ChatWidgetState &st = m_chatState[src];
    if (st.questionIndex < st.questions.size()) ++st.questionIndex;
    renderWidget(src);
}

void WidgetManager::clearChatState(obs_source_t *src) {
    m_chatState.remove(src);
    renderWidget(src);
}

// ---------------------------------------------------------------- events & chat
double WidgetManager::hypeScore() const {
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 5 * 60000;
    double s = 0;
    for (const auto &[ms, pts] : m_session.hype) if (ms >= cutoff) s += pts;
    return s;
}

void WidgetManager::onEvent(const StreamEvent &e) {
    const AlertRule r = rule(e.type);
    if (r.enabled && (e.amount >= r.minAmount || e.test)) {
        m_alertQueue.enqueue(e);
        if (!m_alertActive) nextAlert();
    }
    // Tallies, leaderboards, hype and subathon time only count real events (tests just alert).
    if (!e.test) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        double pts = 0;
        if (e.type == "follow") pts = 1;
        else if (e.type == "subscribe" || e.type == "membership") pts = 5;
        else if (e.type == "gift") { pts = 5 * qMax(1.0, e.amount); m_session.giftTotals[e.user] += qMax(1.0, e.amount); }
        else if (e.type == "cheer") { pts = e.amount / 100.0; m_session.tipTotals[e.user] += e.amount / 100.0; }
        else if (e.type == "donation") { pts = e.amount; m_session.tipTotals[e.user] += e.amount; }
        else if (e.type == "raid") pts = qMax(5.0, e.amount / 5.0);
        else pts = 1;
        m_session.hype.append({now, pts});
        if (e.type == "subscribe" || e.type == "gift" || e.type == "membership") {
            const int add = e.type == "gift" ? int(qMax(1.0, e.amount)) : 1;
            m_session.combo = now - m_session.comboLastMs < 60000 ? m_session.combo + add : add;
            m_session.comboLastMs = now;
        }
        forEachOfKind("subathon", [&](obs_source_t *s) {
            const WidgetConfig c = WidgetRegistry::instance().config(s);
            int secs = 0;
            if (e.type == "follow") secs = c.addFollow;
            else if (e.type == "subscribe" || e.type == "membership") secs = c.addSub;
            else if (e.type == "gift") secs = int(c.addSub * qMax(1.0, e.amount));
            else if (e.type == "cheer") secs = int(c.addTip * e.amount / 100.0);
            else if (e.type == "donation") secs = int(c.addTip * e.amount);
            if (secs > 0) addSubathonSeconds(s, secs);
        });
    }
    static const QSet<QString> eventKinds = {"events", "label", "goal", "goal_ring", "goal_candle", "recent_list", "raid_banner",
                                             "hype_meter", "combo", "leaderboard"};
    for (obs_source_t *s : WidgetRegistry::instance().sources())
        if (eventKinds.contains(WidgetRegistry::instance().config(s).kind)) renderWidget(s);
}

void WidgetManager::onChat(const ChatMessage &m) {
    if (m.hidden || m.deleted) return;
    m_session.chatCounts[m.user] += 1;
    m_session.messages += 1;
    if (m_session.firstChatter.isEmpty()) m_session.firstChatter = m.user;
    m_session.lastChatUser = m.user;
    m_session.lastChatText = m.text;
    m_session.lastChatPlatform = m.platform;
    m_session.hype.append({QDateTime::currentMSecsSinceEpoch(), 0.1});

    const QString text = m.text.trimmed();
    const bool mod = m.fromMod || m.badges.contains("broadcaster") || m.badges.contains("moderator");
    auto startsWithCmd = [&](const QString &cmd, QString *rest = nullptr) {
        if (cmd.isEmpty() || !text.startsWith(cmd, Qt::CaseInsensitive)) return false;
        const QString r = text.mid(cmd.size());
        if (!r.isEmpty() && !r.at(0).isSpace() && r != "+" && r != "-") return false;
        if (rest) *rest = r.trimmed();
        return true;
    };

    static const QSet<QString> chatKinds = {"chat", "chat_bubble", "poll", "giveaway", "qna", "word_counter", "first_chatter",
                                            "leaderboard", "label", "goal", "goal_ring", "goal_candle", "hype_meter", "counter", "winloss"};
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (!chatKinds.contains(c.kind)) continue;
        QString rest;
        if (c.kind == "poll") {
            const int options = int(c.text.split('\n', Qt::SkipEmptyParts).size());
            int choice = 0;
            if (startsWithCmd(c.keyword.isEmpty() ? "!vote" : c.keyword, &rest)) choice = rest.section(' ', 0, 0).toInt();
            else if (text.size() == 1 && text.at(0).isDigit()) choice = text.toInt();
            if (choice >= 1 && choice <= options) m_chatState[s].votes[m.user] = choice;
        } else if (c.kind == "giveaway") {
            if (text.compare(c.keyword.isEmpty() ? "!join" : c.keyword, Qt::CaseInsensitive) == 0 && !m_chatState[s].entrants.contains(m.user))
                m_chatState[s].entrants << m.user;
        } else if (c.kind == "qna") {
            if (startsWithCmd(c.keyword.isEmpty() ? "!q" : c.keyword, &rest) && !rest.isEmpty()) {
                auto &st = m_chatState[s];
                st.questions.append({m.user, rest.left(240)});
                if (st.questionIndex < 0) st.questionIndex = 0;
            }
        } else if (c.kind == "word_counter" && !c.keyword.isEmpty()) {
            const QRegularExpression re("(?<![\\w])" + QRegularExpression::escape(c.keyword) + "(?![\\w])", QRegularExpression::CaseInsensitiveOption);
            auto it = re.globalMatch(text);
            while (it.hasNext()) { it.next(); m_chatState[s].words += 1; }
        } else if (c.kind == "counter" && mod && !c.keyword.isEmpty()) {
            if (text.compare(c.keyword, Qt::CaseInsensitive) == 0 || text.compare(c.keyword + "+", Qt::CaseInsensitive) == 0) adjustCounter(s, +1, 0);
            else if (text.compare(c.keyword + "-", Qt::CaseInsensitive) == 0) adjustCounter(s, -1, 0);
        } else if (c.kind == "winloss" && mod) {
            if (text.compare("!win", Qt::CaseInsensitive) == 0) adjustCounter(s, +1, 0);
            else if (text.compare("!loss", Qt::CaseInsensitive) == 0) adjustCounter(s, 0, +1);
        }
        renderWidget(s);
    }
}

void WidgetManager::nextAlert() {
    if (m_alertQueue.isEmpty()) {
        m_alertActive = false;
        forEachOfKind("alerts", [this](obs_source_t *s) { renderWidget(s); });   // clear
        return;
    }
    m_current = m_alertQueue.dequeue();
    m_alertActive = true;
    m_alertClock.restart();
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (c.kind == "alerts" && !c.soundFile.isEmpty()) { playSound(c.soundFile); break; }
    }
    m_animTimer.start();
}

// On air (program, projectors, virtual camera) or in the scene being edited. Animated widgets
// in scenes nobody is looking at are not repainted, so a collection full of ambience costs
// nothing until its scene is shown.
static bool onScreen(obs_source_t *s) {
    if (obs_source_showing(s)) return true;
    auto &e = Engine::instance();
    obs_scene_t *sc = e.scene(e.previewScene());
    return sc && obs_scene_find_source_recursive(sc, obs_source_get_name(s));
}

void WidgetManager::animateTick() {
    int alertMs = 6000;
    bool any = m_alertActive;
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (c.kind == "alerts" && m_alertActive) { alertMs = qMax(2, c.alertSeconds) * 1000; renderWidget(s); }
        else if (isAnimated(c)) { any = true; if (onScreen(s)) renderWidget(s); }
    }
    if (m_alertActive && m_alertClock.elapsed() > alertMs) nextAlert();
    if (!any) m_animTimer.stop();
}

void WidgetManager::secondTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_cpu = m_usage.cpuPercent();
    double kbps = 0;
    auto &e = Engine::instance();
    for (const QString &id : e.activeDestinations()) kbps += e.destinationStatus(id).kbps;
    m_kbps.append(kbps);
    while (m_kbps.size() > 60) m_kbps.removeFirst();
    while (!m_session.hype.isEmpty() && m_session.hype.first().first < now - 5 * 60000) m_session.hype.removeFirst();
    if (m_session.combo > 0 && now - m_session.comboLastMs > 60000) m_session.combo = 0;

    bool anyAnimated = false;
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (isAnimated(c)) { anyAnimated = true; continue; }   // the 30 fps timer handles these
        if (c.kind == "alerts") continue;
        renderWidget(s);
    }
    if ((anyAnimated || m_alertActive) && !m_animTimer.isActive()) m_animTimer.start();
}

void WidgetManager::playSound(const QString &file) {
    if (!QFile::exists(file)) return;
    if (!m_sound) {
        OBSDataAutoRelease st = obs_data_create();
        obs_data_set_bool(st, "is_local_file", true);
        obs_data_set_bool(st, "restart_on_activate", false);
        obs_data_set_bool(st, "close_when_inactive", false);
        m_sound = obs_source_create_private("ffmpeg_source", "KovaLive Alert Sound", st);
        if (!m_sound) return;
        obs_source_set_monitoring_type(m_sound, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
        obs_set_output_source(6, m_sound);   // mixed into stream + recordings
    }
    OBSDataAutoRelease st = obs_data_create();
    obs_data_set_string(st, "local_file", file.toUtf8().constData());
    obs_data_set_bool(st, "is_local_file", true);
    obs_source_update(m_sound, st);
    obs_source_media_restart(m_sound);
}

// ---------------------------------------------------------------- rendering hand-off
void WidgetManager::renderWidget(obs_source_t *src) {
    const WidgetConfig cfg = WidgetRegistry::instance().config(src);
    if (cfg.kind.isEmpty()) return;
    WidgetRegistry::instance().pushFrame(src, render(src, cfg));
}

QImage WidgetManager::renderPreview(const WidgetConfig &cfg, const QSize &size) {
    WidgetConfig c = cfg; c.width = size.width(); c.height = size.height();
    return render(nullptr, c);
}

// ---------------------------------------------------------------- audio taps
static void audioCapture(void *param, obs_source_t *, const audio_data *audio, bool muted) {
    auto *tap = static_cast<AudioTap *>(param);
    if (!audio || !audio->data[0] || audio->frames == 0) return;
    const float *in = reinterpret_cast<const float *>(audio->data[0]);   // the mix is planar float
    QMutexLocker l(&tap->mutex);
    double sum = 0;
    for (uint32_t i = 0; i < audio->frames; ++i) {
        const float v = muted ? 0.f : in[i];
        tap->ring[tap->pos] = v;
        tap->pos = (tap->pos + 1) % tap->ring.size();
        sum += double(v) * v;
    }
    const float rms = float(std::sqrt(sum / audio->frames));
    tap->rms = rms > tap->rms ? rms : tap->rms * 0.85f + rms * 0.15f;   // fast attack, slow release
}

void WidgetManager::syncAudioTaps() {
    QSet<QString> wanted;
    for (obs_source_t *s : WidgetRegistry::instance().sources()) {
        const WidgetConfig c = WidgetRegistry::instance().config(s);
        if (c.kind != "voice_sigil" && c.kind != "audio_visualizer") continue;
        QString name = c.audioSource;
        if (name.isEmpty() && Engine::instance().micAudio()) name = QString::fromUtf8(obs_source_get_name(Engine::instance().micAudio()));
        if (!name.isEmpty()) wanted << name;
    }
    for (auto it = m_taps.begin(); it != m_taps.end();) {
        if (wanted.contains(it.key())) { ++it; continue; }
        OBSSourceAutoRelease s = obs_weak_source_get_source(it.value()->weak);
        if (s) obs_source_remove_audio_capture_callback(s, audioCapture, it.value().get());
        it = m_taps.erase(it);
    }
    for (const QString &name : wanted) {
        if (m_taps.contains(name)) continue;
        OBSSourceAutoRelease s = obs_get_source_by_name(name.toUtf8().constData());
        if (!s) continue;
        auto tap = std::make_shared<AudioTap>();
        tap->name = name;
        tap->weak = OBSGetWeakRef(s);
        obs_audio_info oai{};
        if (obs_get_audio_info(&oai)) tap->sampleRate = oai.samples_per_sec;
        obs_source_add_audio_capture_callback(s, audioCapture, tap.get());
        m_taps.insert(name, tap);
    }
}

static QString tapName(const QString &sourceName) {
    if (!sourceName.isEmpty()) return sourceName;
    obs_source_t *mic = Engine::instance().micAudio();
    return mic ? QString::fromUtf8(obs_source_get_name(mic)) : QString();
}

float WidgetManager::audioLevel(const QString &sourceName) const {
    const auto tap = m_taps.value(tapName(sourceName));
    if (!tap) return 0;
    QMutexLocker l(&tap->mutex);
    return qBound(0.f, tap->rms * 4.f, 1.f);
}

QVector<float> WidgetManager::audioSpectrum(const QString &sourceName, int bands) const {
    QVector<float> out(qMax(1, bands), 0.f);
    const auto tap = m_taps.value(tapName(sourceName));
    if (!tap) return out;
    constexpr int N = 1024;
    std::vector<std::complex<float>> x(N);
    uint32_t rate;
    {
        QMutexLocker l(&tap->mutex);
        rate = tap->sampleRate;
        const size_t sz = tap->ring.size();
        for (int i = 0; i < N; ++i) {
            const float w = 0.5f - 0.5f * std::cos(2.f * float(M_PI) * i / (N - 1));   // Hann window
            x[size_t(i)] = tap->ring[(tap->pos + sz - N + size_t(i)) % sz] * w;
        }
    }
    // iterative radix-2 FFT
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[size_t(i)], x[size_t(j)]);
    }
    for (int len = 2; len <= N; len <<= 1) {
        const std::complex<float> wl = std::polar(1.f, -2.f * float(M_PI) / len);
        for (int i = 0; i < N; i += len) {
            std::complex<float> w(1.f);
            for (int k = 0; k < len / 2; ++k) {
                const auto u = x[size_t(i + k)], v = x[size_t(i + k + len / 2)] * w;
                x[size_t(i + k)] = u + v; x[size_t(i + k + len / 2)] = u - v;
                w *= wl;
            }
        }
    }
    // log-spaced bands 40 Hz – 12 kHz, in dB mapped to 0..1
    const double lo = std::log(40.0), hi = std::log(12000.0);
    for (int b = 0; b < out.size(); ++b) {
        const double f0 = std::exp(lo + (hi - lo) * b / out.size()), f1 = std::exp(lo + (hi - lo) * (b + 1) / out.size());
        int k0 = qMax(1, int(f0 * N / rate)), k1 = qMax(k0 + 1, int(f1 * N / rate));
        k1 = qMin(k1, N / 2);
        float mag = 0;
        for (int k = k0; k < k1; ++k) mag = std::max(mag, std::abs(x[size_t(k)]));
        const float db = 20.f * std::log10(mag / (N / 4.f) + 1e-6f);
        out[b] = qBound(0.f, (db + 70.f) / 70.f, 1.f);
    }
    return out;
}

// ---------------------------------------------------------------- files & slideshows
QString WidgetManager::fileText(const QString &path) {
    if (path.isEmpty()) return {};
    const QFileInfo fi(path);
    if (!fi.exists()) return {};
    FileCache &c = m_files[path];
    const qint64 mt = fi.lastModified().toMSecsSinceEpoch();
    if (mt != c.mtime) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) c.text = QString::fromUtf8(f.read(4096)).trimmed().left(300);
        c.mtime = mt;
    }
    return c.text;
}

QImage WidgetManager::slideshowFrame(const WidgetConfig &cfg, obs_source_t *src, qint64 now, const QSize &size) {
    Slides &s = m_slides[src];
    if (s.folder != cfg.file || (now - s.switchedMs > 30000 && s.files.isEmpty())) {
        s = Slides{};
        s.folder = cfg.file;
        const QStringList filters = {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.gif"};
        for (const QString &f : QDir(cfg.file).entryList(filters, QDir::Files, QDir::Name)) s.files << QDir(cfg.file).filePath(f);
    }
    if (s.files.isEmpty()) return {};
    const qint64 period = qint64(qMax(1, cfg.interval)) * 1000;
    if (s.index < 0 || now - s.switchedMs >= period) {
        s.previous = s.current;
        s.index = (s.index + 1) % int(s.files.size());
        QImage img(s.files.at(s.index));
        s.current = img.isNull() ? QImage() : img.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        s.switchedMs = now;
    }
    QImage frame(size, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    QPainter p(&frame);
    auto draw = [&](const QImage &img, double op) {
        if (img.isNull() || op <= 0) return;
        p.setOpacity(op);
        p.drawImage(QPointF((size.width() - img.width()) / 2.0, (size.height() - img.height()) / 2.0), img);
    };
    const double fade = qBound(0.0, (now - s.switchedMs) / 1000.0, 1.0);
    draw(s.previous, 1.0);
    draw(s.current, fade);
    p.end();
    return frame;
}

} // namespace bc
