#pragma once
#include "engine/WidgetSource.h"
#include "platforms/Platform.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QTimer>
#include <obs.hpp>

namespace bc {

struct OverlayPack {
    QString id, name, emoji, font;
    QColor bg1, bg2, accent, text;
};

struct AlertRule {
    bool enabled = true;
    double minAmount = 0;
    QString templ;      // "{user} just followed!"
};

// Paints every bc_widget source (alerts, chat box, goals, labels, timers…)
// and pushes frames to libobs only when content changes. Alerts are queued,
// animated at 30 fps only while visible, and play their sound through a
// private media source mixed into the stream on audio channel 6.
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
    QImage renderPreview(const WidgetConfig &cfg, const QSize &size);   // for the Widgets page

private:
    WidgetManager() = default;
    void onEvent(const StreamEvent &e);
    void nextAlert();
    void animateTick();
    void renderWidget(obs_source_t *src);
    QImage render(obs_source_t *src, const WidgetConfig &cfg);
    void playSound(const QString &file);

    QVector<OverlayPack> m_packs;
    QJsonObject m_defaultTemplates;
    QTimer m_secondTimer, m_animTimer;
    QQueue<StreamEvent> m_alertQueue;
    StreamEvent m_current;
    bool m_alertActive = false;
    QElapsedTimer m_alertClock;
    QHash<obs_source_t *, qint64> m_timerStart;
    qint64 m_streamStartMs = 0;
    OBSSourceAutoRelease m_sound;
};

} // namespace bc
