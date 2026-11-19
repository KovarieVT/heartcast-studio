#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>

class QListWidget;
class QStackedWidget;
class QLabel;
class QPushButton;
class QDockWidget;
class QSplitter;
class QMenu;

namespace bc {

class PreviewWidget;
class ScenesPanel;
class SourcesPanel;
class MixerPanel;
class HomePage;
class StreamManagerPage;
class RecordingPage;
class AnalyticsPage;
class SettingsPage;
class PluginsPage;
class BrandHeader;
class StatsPanel;
class TransitionsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void runFirstTimeWizard();
    void showCrashRecoveryNotice(const QString &details);

    enum NavIndex { NavHome, NavStudio, NavScenes, NavSources, NavMixer, NavOverlays, NavWidgets, NavStream,
                    NavRecording, NavMedia, NavEffects, NavTransitions, NavChat, NavActivity, NavAnalytics,
                    NavPlugins, NavSettings };

public Q_SLOTS:
    void goLive();
    void endLive();
    void toggleRecording();
    void toggleReplayBuffer();
    void toggleVirtualCam();
    void navigate(int navIndex);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildMenus();
    void buildDocks();
    void buildStatusBar();
    void registerHotkeys();
    void refreshControls();
    void tick();
    void notify(const QString &text, bool error);
    void openFullscreenProjector();
    void saveLayoutAs();
    void loadLayout(const QString &name);
    void fillPanelsMenu(QMenu *menu);
    void setPanelsLocked(bool locked);
    void resetPanels();
    QList<QDockWidget *> allDocks() const;

    QListWidget *m_nav;
    QStackedWidget *m_stack;
    PreviewWidget *m_preview, *m_program;
    QWidget *m_transitionCol;
    ScenesPanel *m_scenes;
    SourcesPanel *m_sources;
    MixerPanel *m_mixer;
    HomePage *m_home;
    StreamManagerPage *m_streamPage;
    RecordingPage *m_recordPage;
    AnalyticsPage *m_analytics;
    SettingsPage *m_settings;
    PluginsPage *m_plugins;
    BrandHeader *m_header;
    StatsPanel *m_stats;
    TransitionsPanel *m_transPanel;
    QDockWidget *m_scenesDock, *m_sourcesDock, *m_mixerDock, *m_controlsDock,
                *m_transitionsDock, *m_chatDock, *m_activityDock, *m_statsDock;
    QByteArray m_defaultState;
    QPushButton *m_liveBtn, *m_recBtn, *m_pauseBtn, *m_replayBtn, *m_vcamBtn, *m_studioBtn;
    QLabel *m_livePill, *m_recPill, *m_statCpu, *m_statFps, *m_statDrop, *m_statKbps, *m_statDur, *m_statMsg;
    QSystemTrayIcon *m_tray = nullptr;
    QByteArray m_studioDockState;
    int m_currentPage = 0;
    bool m_quitting = false;
};

} // namespace bc
