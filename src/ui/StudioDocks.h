#pragma once
#include <QDialog>
#include <QListWidget>
#include <QTreeWidget>
#include <QWidget>
#include <atomic>
#include <obs.hpp>

class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;
class QComboBox;
class QSpinBox;
class QTableWidget;

namespace bc {

class PreviewWidget;

// ---------------------------------------------------------------- scenes
class ScenesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScenesPanel(QWidget *parent = nullptr);
    void reload();
Q_SIGNALS:
    void sceneActivated(const QString &name);
private:
    void contextMenu(const QPoint &pt);
    QListWidget *m_list;
    bool m_reloading = false;
};

// ---------------------------------------------------------------- sources
class SourcesPanel : public QWidget {
    Q_OBJECT
public:
    explicit SourcesPanel(PreviewWidget *preview, QWidget *parent = nullptr);
    void reload();
    void addSourceMenu(QWidget *anchor);
    void addWidgetOfKind(const QString &catalogId, const QString &pack = "nocturne");
Q_SIGNALS:
    void galleryRequested();
public:
private:
    obs_sceneitem_t *currentItem() const;
    void onItemChanged(QTreeWidgetItem *it, int col);
    void contextMenu(const QPoint &pt);
    void removeSelected();
    void moveSelected(obs_order_movement m);
    void duplicateSelected();
    void groupSelected();
    QTreeWidget *m_tree;
    PreviewWidget *m_preview;
    bool m_reloading = false;
};

// ---------------------------------------------------------------- audio mixer
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);
    void setLevels(float magDb, float peakDb);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    float m_mag = -60, m_peak = -60, m_hold = -60;
    qint64 m_holdMs = 0, m_clipMs = 0;
};

class AudioStrip : public QWidget {
    Q_OBJECT
public:
    explicit AudioStrip(obs_source_t *src, QWidget *parent = nullptr);
    ~AudioStrip() override;
    obs_source_t *source() const { return m_src; }
    void setSoloMuted(bool m);
    void pollMeter();
Q_SIGNALS:
    void soloToggled(AudioStrip *strip, bool on);
private:
    static void volmeterCb(void *data, const float mag[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
                           const float inputPeak[MAX_AUDIO_CHANNELS]);
    void syncFromSource();
    OBSSource m_src;
    obs_volmeter_t *m_meter = nullptr;
    obs_fader_t *m_fader = nullptr;
    std::atomic<float> m_mag{-60.f}, m_peak{-60.f};
    LevelMeter *m_levels;
    QSlider *m_slider;
    QLabel *m_db;
    QToolButton *m_mute, *m_solo, *m_monitor;
    std::vector<OBSSignal> m_sigs;
    bool m_soloMuted = false, m_userMuted = false;
};

class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(QWidget *parent = nullptr);
    void rebuild();
private:
    QVBoxLayout *m_lay;
    QList<AudioStrip *> m_strips;
    QTimer *m_poll;
};

// ---------------------------------------------------------------- transitions (dock)
class TransitionsPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransitionsPanel(QWidget *parent = nullptr);
    void sync();
private:
    QComboBox *m_combo;
    QSpinBox *m_duration;
};

// ---------------------------------------------------------------- stats (dock)
class StatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatsPanel(QWidget *parent = nullptr);
    void refresh();   // called once per second by the main window
private:
    QLabel *m_engine;
    QTableWidget *m_outputs;
};

// ---------------------------------------------------------------- dialogs
class FiltersDialog : public QDialog {
    Q_OBJECT
public:
    explicit FiltersDialog(obs_source_t *src, QWidget *parent = nullptr);
private:
    void reload();
    void showFilter(obs_source_t *f);
    void savePreset();
    void loadPreset();
    OBSSource m_src;
    QListWidget *m_list;
    QWidget *m_propsHost;
};

class TransformDialog : public QDialog {
    Q_OBJECT
public:
    TransformDialog(obs_sceneitem_t *item, PreviewWidget *preview, QWidget *parent = nullptr);
};

void openAudioAdvanced(obs_source_t *src, QWidget *parent);

} // namespace bc
