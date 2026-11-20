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
    QString kind;      // renderer — see overlays/WidgetCatalog.cpp for every kind
    QString pack;      // overlay pack id (nocturne …)
    int width = 600, height = 200;
    QString title;
    QString text;      // multi-line content: messages, schedule lines, poll options, subtitle…
    QString labelType; // label: recent_follower | recent_sub | … | session_chatters
    QString goalMetric;// goals: followers | subs | donations | bits | messages
    QString metric;    // ticker/recent/leaderboard/ambient selector
    QString keyword;   // chat command or counted word (!vote, !join, !q, !death, LUL…)
    QString target;    // countdown-to-date "YYYY-MM-DD HH:MM" or world-clock time zone
    QString file;      // text file (file_text) or image folder (slideshow)
    QString audioSource; // voice sigil / visualizer source name (empty = first microphone)
    int goalTarget = 100, goalStart = 0;
    int countdownMinutes = 10;
    int count = 5;     // items to show / candles / bars
    int interval = 10; // seconds (rotator, slideshow) or minutes (reminder, pomodoro break)
    int value = 0, value2 = 0;              // counter / win-loss values (persisted)
    int addFollow = 30, addSub = 120, addTip = 60;   // subathon seconds per event (tip: per 1 unit / 100 bits)
    qint64 subathonEnd = 0;                 // ms since epoch (persisted)
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
// Catalog defaults (JSON) on top of the source's registered defaults — what a freshly
// added widget would get. Used for gallery previews.
WidgetConfig widgetConfigFromJson(const QJsonObject &values);

} // namespace bc
