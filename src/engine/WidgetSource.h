#pragma once
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include "engine/Engine.h"

namespace bc {

// "bc_widget" is a native libobs async-video source type registered by the app.
// Widgets (alerts, chat box, goals, labels, timers…) are painted with QPainter
// by overlays/WidgetManager and pushed as BGRA frames ONLY when they change, so
// an idle widget costs nothing. No browser/Chromium is involved.
void registerWidgetSource();
void unregisterWidgetSources();

struct WidgetConfig {
    QString kind;      // alerts | chat | events | label | goal | countdown | stopwatch | social | sponsor | viewers | text_panel
    QString pack;      // overlay pack id (cotton-candy …)
    int width = 600, height = 200;
    QString title;
    QString labelType; // recent_follower | recent_sub | recent_donation | viewer_count
    QString goalMetric;// followers | subs | donations
    int goalTarget = 100, goalStart = 0;
    int countdownMinutes = 10;
    int fontSize = 28;
    QString socials;   // "twitch:name;tiktok:name;…"
    QString soundFile; // alert sound
    int alertSeconds = 6;
};

class WidgetRegistry : public QObject {
    Q_OBJECT
public:
    static WidgetRegistry &instance();
    QList<obs_source_t *> sources() const;
    WidgetConfig config(obs_source_t *src) const;
    void pushFrame(obs_source_t *src, const QImage &img);

    // called from libobs callbacks (any thread)
    void add(obs_source_t *src, const WidgetConfig &cfg);
    void update(obs_source_t *src, const WidgetConfig &cfg);
    void remove(obs_source_t *src);

Q_SIGNALS:
    void widgetsChanged();
    void widgetConfigChanged(obs_source_t *src);

private:
    mutable QMutex m_mutex;
    QHash<obs_source_t *, WidgetConfig> m_widgets;
};

WidgetConfig widgetConfigFromData(obs_data_t *settings);

} // namespace bc
