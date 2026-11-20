#pragma once
#include "core/HardwareInfo.h"
#include "engine/WidgetSource.h"
#include "platforms/Platform.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <memory>
#include <obs.hpp>
#include <vector>

namespace bc {

struct OverlayPack {
    QString id, name, glyph, font, particles;   // particles: bats | embers | petals | ash | feathers
    QColor bg1, bg2, accent, metal, text;
};

struct AlertRule {
    bool enabled = true;
    double minAmount = 0;
    QString templ;      // "{user} just followed!"
};

// Per-widget chat interaction state (polls, giveaways, Q&A, word counts).
struct ChatWidgetState {
    QHash<QString, int> votes;                       // user → option (1-based)
    QStringList entrants;
    QString winner;
    QList<QPair<QString, QString>> questions;        // (user, question)
    int questionIndex = -1;
    int words = 0;
};

// Live audio from one source, for the voice sigil and the visualizer.
struct AudioTap {
    QString name;
    OBSWeakSource weak;
    QMutex mutex;
    std::vector<float> ring = std::vector<float>(2048, 0.f);
    size_t pos = 0;
    float rms = 0;
    uint32_t sampleRate = 48000;
};

// Paints every bc_widget source and pushes frames to libobs only when content changes.
// Animated widgets (alerts while showing, tickers, ambience, audio-reactive) re-render at
// 30 fps; everything else re-renders when its data changes or once a second for clocks.
class WidgetManager : public QObject {
    Q_OBJECT
public:
    static WidgetManager &instance();
    void init();
    void shutdown();

    QVector<OverlayPack> packs() const { return m_packs; }
    OverlayPack pack(const QString &id) const;
    AlertRule rule(const QString &type) const;
    void setRule(const QString &type, const AlertRule &r);
    static QStringList alertTypes();

    void restartTimers();
    void refreshAll();
    QImage renderPreview(const WidgetConfig &cfg, const QSize &size);   // gallery previews (sample data)

    // ---- actions (widget property buttons, hotkeys, chat commands)
    void adjustCounter(obs_source_t *src, int deltaValue, int deltaValue2);
    void resetCounter(obs_source_t *src);
    void restartWidget(obs_source_t *src);
    void addSubathonSeconds(obs_source_t *src, int seconds);
    void drawGiveawayWinner(obs_source_t *src);
    void nextQuestion(obs_source_t *src);
    void clearChatState(obs_source_t *src);
    void forEachOfKind(const QString &kind, const std::function<void(obs_source_t *)> &fn);
    void resetSession();   // new stream: clears leaderboards, chat stats, hype

    // ---- data the renderers read (main thread)
    struct Session {
        QHash<QString, int> chatCounts;      // user → messages
        QString firstChatter;
        int messages = 0;
        QHash<QString, double> giftTotals;   // user → gifted subs
        QHash<QString, double> tipTotals;    // user → tips + bits/100
        QList<QPair<qint64, double>> hype;   // (ms, points)
        int combo = 0;
        qint64 comboLastMs = 0;
        QString lastChatUser, lastChatText, lastChatPlatform;
    };
    const Session &session() const { return m_session; }
    const ChatWidgetState chatState(obs_source_t *src) const { return m_chatState.value(src); }
    double hypeScore() const;
    QList<double> kbpsHistory() const { return m_kbps; }
    double cpuPercent() const { return m_cpu; }
    qint64 streamStartMs() const { return m_streamStartMs; }
    qint64 recordStartMs() const { return m_recStartMs; }
    qint64 widgetStartMs(obs_source_t *src) const;
    float audioLevel(const QString &sourceName) const;                 // 0..1 RMS
    QVector<float> audioSpectrum(const QString &sourceName, int bands) const;   // 0..1 per band
    QImage slideshowFrame(const WidgetConfig &cfg, obs_source_t *src, qint64 now, const QSize &size);
    QString fileText(const QString &path);

private:
    WidgetManager() = default;
    void onEvent(const StreamEvent &e);
    void onChat(const ChatMessage &m);
    void nextAlert();
    void animateTick();
    void secondTick();
    void renderWidget(obs_source_t *src);
    QImage render(obs_source_t *src, const WidgetConfig &cfg);
    void playSound(const QString &file);
    void writeSettings(obs_source_t *src, const std::function<void(obs_data_t *)> &fn);
    void syncAudioTaps();
    static bool isAnimated(const WidgetConfig &cfg);

    friend struct WidgetPainter;
    QVector<OverlayPack> m_packs;
    QJsonObject m_defaultTemplates;
    QTimer m_secondTimer, m_animTimer;
    QQueue<StreamEvent> m_alertQueue;
    StreamEvent m_current;
    bool m_alertActive = false;
    QElapsedTimer m_alertClock;
    QHash<obs_source_t *, qint64> m_timerStart;
    qint64 m_streamStartMs = 0, m_recStartMs = 0;
    OBSSourceAutoRelease m_sound;

    Session m_session;
    QHash<obs_source_t *, ChatWidgetState> m_chatState;
    QList<double> m_kbps;
    double m_cpu = 0;
    UsageSampler m_usage;
    QHash<QString, std::shared_ptr<AudioTap>> m_taps;
    struct FileCache { qint64 mtime = -1; QString text; };
    QHash<QString, FileCache> m_files;
    struct Slides { QString folder; QStringList files; QImage current, previous; int index = -1; qint64 switchedMs = 0; };
    QHash<obs_source_t *, Slides> m_slides;
};

} // namespace bc
