#pragma once
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>

class QLabel;
class QPushButton;
class QDockWidget;
class QMenu;
class QDialog;
class QAction;

namespace bc {

class PreviewWidget;
class ScenesPanel;
class SourcesPanel;
class MixerPanel;
class StreamManagerPage;
class SettingsPage;
class BrandHeader;
class StatsPanel;
class TransitionsPanel;

// OBS / Streamlabs-style workspace: the preview fills the window, the working panels
// are docks along the bottom (Scenes · Sources · Audio Mixer · Scene Transitions ·
// Controls) with Chat on the right, and everything else opens as its own window
// from the menus. Settings is a separate window with categories down the left.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void runFirstTimeWizard();
    void showCrashRecoveryNotice(const QString &details);

public Q_SLOTS:
    void goLive();
    void endLive();
    void toggleRecording();
    void toggleReplayBuffer();
    void toggleVirtualCam();
    void openSettings(const QString &category = QString());
    // "widgets[:Category]", "media", "effects", "transitions", "recordings", "analytics", "plugins", "chat", "activity"
    void openWindow(const QString &which);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void buildDocks();
    void buildStatusBar();
    void registerHotkeys();
    void refreshControls();
    void refreshTitle();
    void tick();
    void notify(const QString &text, bool error);
    void openFullscreenProjector();
    void setStudioMode(bool on);
    void saveLayoutAs();
    void loadLayout(const QString &name);
    void fillDocksMenu(QMenu *menu);
    void fillCollectionsMenu(QMenu *menu);
    void setPanelsLocked(bool locked);
    void resetPanels();
    QList<QDockWidget *> allDocks() const;

    PreviewWidget *m_preview, *m_program;
    QWidget *m_transitionCol;
    ScenesPanel *m_scenes;
    SourcesPanel *m_sources;
    MixerPanel *m_mixer;
    StreamManagerPage *m_streamPage;
    SettingsPage *m_settings;
    QDialog *m_settingsDlg = nullptr;
    QHash<QString, QDialog *> m_windows;
    BrandHeader *m_header;
    StatsPanel *m_stats;
    TransitionsPanel *m_transPanel;
    QDockWidget *m_scenesDock, *m_sourcesDock, *m_mixerDock, *m_controlsDock,
                *m_transitionsDock, *m_chatDock, *m_activityDock, *m_statsDock;
    QByteArray m_defaultState;
    QPushButton *m_liveBtn, *m_recBtn, *m_pauseBtn, *m_replayBtn, *m_saveReplayBtn, *m_vcamBtn, *m_studioBtn;
    QAction *m_studioAction = nullptr;
    QLabel *m_statDrop, *m_statLive, *m_statRec, *m_statCpu, *m_statKbps, *m_statMsg;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_quitting = false;
};

} // namespace bc
