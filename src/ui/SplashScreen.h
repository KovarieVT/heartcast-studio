#pragma once
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace bc {

// Animated startup screen. Progress comes from real loading steps
// (Engine::startup callback). A watchdog makes sure it can never hang:
// if loading stalls for kStallMs it reports the stall and lets the app continue
// (or quit) instead of staying on screen forever.
class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(bool animated, QWidget *parent = nullptr);
    void setProgress(int pct, const QString &message);
    void finish(QWidget *mainWindow);   // fades out, then shows main window

    static constexpr int kStallMs = 25000;

Q_SIGNALS:
    void stalled();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct Bat { QPointF pos; QPointF vel; double span, phase, speed; };
    struct Ember { QPointF pos; QPointF vel; double size, phase; };
    void tick();
    void spawnBat(Bat &b, bool anywhere);
    void spawnEmber(Ember &e, bool anywhere);

    bool m_animated;
    QTimer m_timer, m_watchdog;
    QElapsedTimer m_clock;
    QList<Bat> m_bats;
    QList<Ember> m_embers;
    double m_shownPct = 0;
    int m_targetPct = 0;
    QString m_message;
    int m_quipIdx = 0;
    qint64 m_lastQuipMs = 0;
    double m_opacity = 1.0;
    bool m_finishing = false;
    QWidget *m_next = nullptr;
};

} // namespace bc
