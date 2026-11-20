#include "ui/MainWindow.h"
#include "BuildInfo.h"
#include "app/Theme.h"
#include "core/HotkeyManager.h"
#include "core/Logger.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "engine/Engine.h"
#include "platforms/PlatformHub.h"
#include "plugins/PluginManager.h"
#include "ui/GothicPainter.h"
#include "overlays/WidgetManager.h"
#include "ui/DeveloperCredentialsDialog.h"
#include "ui/Icons.h"
#include "ui/Pages.h"
#include "ui/PreviewWidget.h"
#include "ui/PropertiesView.h"
#include "ui/SettingsPage.h"
#include "ui/SetupWizard.h"
#include "ui/StudioDocks.h"
#include "updater/Updater.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QDialog>
#include <QDirIterator>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <iterator>

namespace bc {

// Brand mark in the menu bar's corner (like Streamlabs' title area). The window's glow
// flickers like candlelight; it stops while live/recording if "reduce effects while
// live" is on, so decoration never competes with encoding.
class BrandHeader : public QWidget {
public:
    explicit BrandHeader(QWidget *p = nullptr) : QWidget(p) {
        setFixedSize(150, 24);
        connect(&m_timer, &QTimer::timeout, this, [this] { m_t += 0.1; update(); });
        m_timer.start(100);
    }
    void setAnimating(bool on) { on ? m_timer.start(100) : m_timer.stop(); }
    QSize sizeHint() const override { return QSize(150, 24); }   // QMenuBar lays corner widgets out by size hint
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double flicker = m_timer.isActive() ? 0.82 + 0.18 * std::sin(m_t * 7.1) * std::sin(m_t * 2.3) : 1.0;
        gothic::drawEmblem(p, QPointF(18, 12.5), 26, theme::Crimson, flicker);
        QFont f(theme::decorativeFamily()); f.setPointSize(10); f.setWeight(QFont::Bold);
        p.setFont(f); p.setPen(theme::Parchment);
        p.drawText(QRectF(40, 0, width() - 42, height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(BC_APP_NAME));
    }
private:
    QTimer m_timer; double m_t = 0;
};

class ProjectorWindow : public QWidget {
public:
    ProjectorWindow() {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr("KovaLive — Fullscreen program"));
        auto *l = new QVBoxLayout(this); l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new PreviewWidget(PreviewWidget::Program, this));
    }
protected:
    void keyPressEvent(QKeyEvent *e) override { if (e->key() == Qt::Key_Escape) close(); }
};

static QDockWidget *makeDock(QMainWindow *w, const QString &title, const QString &obj, QWidget *content) {
    auto *d = new QDockWidget(title, w);
    d->setObjectName(obj);
    d->setWidget(content);
    d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    return d;
}

MainWindow::MainWindow() {
    setWindowIcon(QIcon(":/icons/logo_256.png"));
    resize(1500, 920);
    // Every dock can be dragged to any edge, stacked, tabbed together (drop onto another
    // dock's title), floated as its own window and docked again — like OBS.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    // ---- central: preview (and program + transition button in Studio Mode)
    auto *central = new QWidget;
    auto *cl = new QHBoxLayout(central);
    cl->setContentsMargins(4, 4, 4, 0);
    cl->setSpacing(4);
    m_preview = new PreviewWidget(PreviewWidget::Edit);
    m_program = new PreviewWidget(PreviewWidget::Program);
    m_transitionCol = new QWidget;
    auto *tc = new QVBoxLayout(m_transitionCol);
    tc->addStretch();
    auto *tbtn = new QPushButton(tr("Transition")); tbtn->setProperty("accent", "primary"); tbtn->setMinimumHeight(54);
    tc->addWidget(tbtn);
    tc->addStretch();
    cl->addWidget(m_preview, 1); cl->addWidget(m_transitionCol); cl->addWidget(m_program, 1);
    m_program->hide(); m_transitionCol->hide();
    setCentralWidget(central);
    connect(tbtn, &QPushButton::clicked, this, [] { auto &e = Engine::instance(); e.transitionTo(e.previewScene()); });
    connect(m_preview, &PreviewWidget::itemDoubleClicked, this, [this](obs_sceneitem_t *i) { (new PropertiesDialog(obs_sceneitem_get_source(i), this))->show(); });

    m_scenes = new ScenesPanel;
    m_sources = new SourcesPanel(m_preview);
    m_mixer = new MixerPanel;
    connect(m_scenes, &ScenesPanel::sceneActivated, this, [](const QString &n) { Engine::instance().setPreviewScene(n); });
    connect(m_sources, &SourcesPanel::galleryRequested, this, [this] { openWindow("widgets"); });
    auto syncPreview = [this] { m_preview->setSceneName(Engine::instance().previewScene()); m_sources->reload(); };
    connect(&Engine::instance(), &Engine::previewSceneChanged, this, syncPreview);
    connect(&Engine::instance(), &Engine::programSceneChanged, this, syncPreview);
    syncPreview();

    // ---- Settings window (OBS-style categories); the Stream category edits destinations & accounts
    m_streamPage = new StreamManagerPage;
    m_settings = new SettingsPage;
    m_settings->insertCategory(1, m_streamPage, tr("Stream"), QStringLiteral("➤"));
    m_settings->orderCategories({tr("General"), tr("Stream"), tr("Output"), tr("Recording"), tr("Audio"), tr("Video"),
                                 tr("Hotkeys"), tr("Appearance"), tr("Advanced")});
    connect(m_settings, &SettingsPage::themeChanged, this, [this] { refreshControls(); update(); });
    connect(m_settings, &SettingsPage::rerunWizard, this, &MainWindow::runFirstTimeWizard);
    m_settingsDlg = new QDialog(this, Qt::Window);
    m_settingsDlg->setWindowTitle(tr("Settings"));
    auto *sl = new QVBoxLayout(m_settingsDlg);
    sl->setContentsMargins(10, 10, 10, 10);
    sl->addWidget(m_settings);
    m_settingsDlg->resize(1020, 720);
    connect(m_settings, &SettingsPage::closeRequested, m_settingsDlg, &QDialog::close);

    m_header = new BrandHeader;
    buildDocks();
    buildMenus();
    menuBar()->setCornerWidget(m_header, Qt::TopRightCorner);
    m_header->show();
    buildStatusBar();
    registerHotkeys();

    // tray
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(QIcon(":/icons/logo_256.png"), this);
        auto *tm = new QMenu(this);
        tm->addAction(tr("Show KovaLive"), this, [this] { showNormal(); raise(); activateWindow(); });
        tm->addAction(tr("Start / Stop Streaming"), this, [this] { Engine::instance().isStreaming() ? endLive() : goLive(); });
        tm->addAction(tr("Start / Stop Recording"), this, &MainWindow::toggleRecording);
        tm->addSeparator();
        tm->addAction(tr("Exit"), this, &QWidget::close);
        m_tray->setContextMenu(tm);
        m_tray->setToolTip(QStringLiteral(BC_APP_NAME));
        m_tray->show();
    }

    auto &e = Engine::instance();
    connect(&e, &Engine::streamingStateChanged, this, &MainWindow::refreshControls);
    connect(&e, &Engine::recordingStateChanged, this, &MainWindow::refreshControls);
    connect(&e, &Engine::replayBufferStateChanged, this, &MainWindow::refreshControls);
    connect(&e, &Engine::virtualCamStateChanged, this, &MainWindow::refreshControls);
    connect(&e, &Engine::engineError, this, [this](const QString &m) { notify(m, true); });
    connect(&e, &Engine::replaySaved, this, [this](const QString &p) { notify(tr("Replay saved: %1").arg(QFileInfo(p).fileName()), false); });
    connect(&e, &Engine::scenesChanged, this, &MainWindow::registerHotkeys);
    connect(&e, &Engine::scenesChanged, this, &MainWindow::refreshTitle);
    connect(&PlatformHub::instance(), &PlatformHub::notice, this, &MainWindow::notify);
    connect(&PluginManager::instance(), &PluginManager::startStreamingRequested, this, &MainWindow::goLive);
    connect(&Updater::instance(), &Updater::updateAvailable, this, [this](const UpdateInfo &u) {
        if (QMessageBox::question(this, tr("Update available"), tr("KovaLive %1 is available.\n\n%2\n\nDownload and install now?").arg(u.version.toString(), u.notes)) == QMessageBox::Yes)
            Updater::instance().downloadAndInstall(u);
    });
    connect(&Updater::instance(), &Updater::upToDate, this, [this] { notify(tr("You're on the latest version"), false); });
    connect(&Updater::instance(), &Updater::checkFailed, this, [this](const QString &r) { notify(tr("Update check: %1").arg(r), true); });
    connect(&Updater::instance(), &Updater::installFailed, this, [this](const QString &r) { QMessageBox::warning(this, tr("Update"), r); });
    connect(&Updater::instance(), &Updater::readyToInstall, this, [this](const QString &path) {
        if (Engine::instance().isStreaming() || Engine::instance().isRecording()) {
            notify(tr("Update downloaded — it will install when you're not live."), false);
            return;
        }
        if (QProcess::startDetached(path, {"/SP-", "/SILENT", "/CLOSEAPPLICATIONS", "/RESTARTAPPLICATIONS"})) { m_quitting = true; close(); }
    });

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::tick);
    timer->start(1000);
    auto *autosave = new QTimer(this);
    connect(autosave, &QTimer::timeout, this, [this] {
        auto &en = Engine::instance();
        if (!en.currentCollectionPath().isEmpty()) en.saveCollection(en.currentCollectionPath());
        Settings::instance().set("layout.main", QString::fromLatin1(saveState().toBase64()));   // panels survive a crash
        Settings::instance().save();
    });
    autosave->start(qMax(1, Settings::instance().i("general.autosaveMinutes")) * 60000);

    const QByteArray geo = QByteArray::fromBase64(Settings::instance().s("layout.geometry").toLatin1());
    if (!geo.isEmpty()) restoreGeometry(geo);
    refreshControls();
    refreshTitle();
    PluginManager::instance().setMainWindow(this);
    PluginManager::instance().loadEnabled();
    if (Settings::instance().b("general.checkUpdates")) QTimer::singleShot(8000, [] { Updater::instance().check(false); });
}

QList<QDockWidget *> MainWindow::allDocks() const { return findChildren<QDockWidget *>(); }

void MainWindow::refreshTitle() {
    const QString coll = QFileInfo(Engine::instance().currentCollectionPath()).completeBaseName();
    // Qt appends " — KovaLive!" (the application display name) to every window title.
    setWindowTitle(QStringLiteral("v%1").arg(BC_APP_VERSION) + (coll.isEmpty() ? QString() : tr(" · Scenes: %1").arg(coll)));
}

void MainWindow::buildDocks() {
    m_scenesDock = makeDock(this, tr("Scenes"), "dock-scenes", m_scenes);
    m_sourcesDock = makeDock(this, tr("Sources"), "dock-sources", m_sources);
    m_mixerDock = makeDock(this, tr("Audio Mixer"), "dock-mixer", m_mixer);
    m_transPanel = new TransitionsPanel;
    m_transitionsDock = makeDock(this, tr("Scene Transitions"), "dock-transitions", m_transPanel);
    m_chatDock = makeDock(this, tr("Chat"), "dock-chat", new ChatPage(true));
    m_activityDock = makeDock(this, tr("Activity Feed"), "dock-activity", new ActivityPage(true));
    m_stats = new StatsPanel;
    m_statsDock = makeDock(this, tr("Stats"), "dock-stats", m_stats);

    // Controls, in OBS's order
    auto *ctl = new QWidget; auto *cl = new QVBoxLayout(ctl);
    cl->setSpacing(6);
    m_liveBtn = new QPushButton(tr("Start Streaming")); m_liveBtn->setProperty("accent", "primary"); m_liveBtn->setMinimumHeight(36);
    m_recBtn = new QPushButton(tr("Start Recording"));
    m_pauseBtn = new QPushButton; m_pauseBtn->setIcon(icons::get("pause")); m_pauseBtn->setProperty("iconOnly", true); m_pauseBtn->setToolTip(tr("Pause recording")); m_pauseBtn->setFixedWidth(42);
    m_replayBtn = new QPushButton(tr("Start Replay Buffer"));
    m_saveReplayBtn = new QPushButton; m_saveReplayBtn->setIcon(icons::get("save")); m_saveReplayBtn->setProperty("iconOnly", true); m_saveReplayBtn->setToolTip(tr("Save replay")); m_saveReplayBtn->setFixedWidth(42);
    m_vcamBtn = new QPushButton(tr("Start Virtual Camera"));
    m_studioBtn = new QPushButton(tr("Studio Mode")); m_studioBtn->setCheckable(true);
    auto *settingsBtn = new QPushButton(tr("Settings"));
    auto *exitBtn = new QPushButton(tr("Exit"));
    auto row = [cl](QWidget *a, QWidget *b) { auto *h = new QHBoxLayout; h->setSpacing(4); h->addWidget(a, 1); h->addWidget(b); cl->addLayout(h); };
    cl->addWidget(m_liveBtn);
    row(m_recBtn, m_pauseBtn);
    row(m_replayBtn, m_saveReplayBtn);
    cl->addWidget(m_vcamBtn);
    cl->addWidget(m_studioBtn);
    cl->addWidget(settingsBtn);
    cl->addWidget(exitBtn);
    cl->addStretch();
    m_controlsDock = makeDock(this, tr("Controls"), "dock-controls", ctl);
    connect(m_liveBtn, &QPushButton::clicked, this, [this] { Engine::instance().isStreaming() ? endLive() : goLive(); });
    connect(m_recBtn, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(m_pauseBtn, &QPushButton::clicked, this, [] { auto &e = Engine::instance(); e.pauseRecording(!e.isRecordingPaused()); });
    connect(m_replayBtn, &QPushButton::clicked, this, &MainWindow::toggleReplayBuffer);
    connect(m_saveReplayBtn, &QPushButton::clicked, this, [this] { if (!Engine::instance().saveReplay()) notify(tr("Start the replay buffer first."), true); });
    connect(m_vcamBtn, &QPushButton::clicked, this, &MainWindow::toggleVirtualCam);
    connect(m_studioBtn, &QPushButton::toggled, this, &MainWindow::setStudioMode);
    connect(settingsBtn, &QPushButton::clicked, this, [this] { openSettings(); });
    connect(exitBtn, &QPushButton::clicked, this, &QWidget::close);

    // Default arrangement: OBS's bottom row (Scenes | Sources | Audio Mixer | Scene
    // Transitions | Controls) with Chat on the right like Streamlabs; Activity Feed is
    // tabbed behind Chat and Stats is under them (hidden until you turn it on).
    addDockWidget(Qt::BottomDockWidgetArea, m_scenesDock);
    splitDockWidget(m_scenesDock, m_sourcesDock, Qt::Horizontal);
    splitDockWidget(m_sourcesDock, m_mixerDock, Qt::Horizontal);
    splitDockWidget(m_mixerDock, m_transitionsDock, Qt::Horizontal);
    splitDockWidget(m_transitionsDock, m_controlsDock, Qt::Horizontal);
    addDockWidget(Qt::RightDockWidgetArea, m_chatDock);
    tabifyDockWidget(m_chatDock, m_activityDock);
    splitDockWidget(m_chatDock, m_statsDock, Qt::Vertical);
    m_chatDock->raise();
    resizeDocks({m_scenesDock, m_sourcesDock, m_mixerDock, m_transitionsDock, m_controlsDock}, {220, 300, 440, 220, 220}, Qt::Horizontal);
    resizeDocks({m_scenesDock}, {290}, Qt::Vertical);
    resizeDocks({m_chatDock}, {330}, Qt::Horizontal);
    m_statsDock->hide();
    m_defaultState = saveState();

    const QByteArray st = QByteArray::fromBase64(Settings::instance().s("layout.main").toLatin1());
    // Layouts saved by the old sidebar version had no right-hand chat; start fresh once.
    if (!st.isEmpty() && Settings::instance().i("layout.version") >= 2) restoreState(st);
    Settings::instance().set("layout.version", 2);
    setPanelsLocked(Settings::instance().b("layout.locked"));
}

void MainWindow::fillDocksMenu(QMenu *menu) {
    menu->clear();
    QList<QDockWidget *> docks = allDocks();
    std::sort(docks.begin(), docks.end(), [](QDockWidget *a, QDockWidget *b) { return a->windowTitle().localeAwareCompare(b->windowTitle()) < 0; });
    for (QDockWidget *d : docks) {
        QAction *a = menu->addAction(d->windowTitle());
        a->setCheckable(true);
        a->setChecked(d->isVisible());
        connect(a, &QAction::triggered, this, [d](bool on) { d->setVisible(on); if (on) d->raise(); });
    }
    menu->addSeparator();
    QAction *lock = menu->addAction(tr("Lock Docks"));
    lock->setCheckable(true);
    lock->setChecked(Settings::instance().b("layout.locked"));
    connect(lock, &QAction::toggled, this, &MainWindow::setPanelsLocked);
    menu->addAction(tr("Reset Docks"), this, &MainWindow::resetPanels);
    menu->addSeparator();
    menu->addAction(tr("Save Layout As…"), this, &MainWindow::saveLayoutAs);
    const QVariantMap saved = Settings::instance().get("layout.saved").toMap();
    if (!saved.isEmpty()) {
        QMenu *load = menu->addMenu(tr("Load Layout"));
        for (const QString &n : saved.keys()) load->addAction(n, this, [this, n] { loadLayout(n); });
        QMenu *del = menu->addMenu(tr("Delete Layout"));
        for (const QString &n : saved.keys()) del->addAction(n, this, [n] {
            QVariantMap m = Settings::instance().get("layout.saved").toMap();
            m.remove(n);
            Settings::instance().set("layout.saved", m);
            Settings::instance().save();
        });
    }
}

void MainWindow::setPanelsLocked(bool locked) {
    // Locked docks can still be shown/hidden, but not dragged, undocked or re-tabbed.
    for (QDockWidget *d : allDocks())
        d->setFeatures(locked ? QDockWidget::DockWidgetClosable
                              : QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    Settings::instance().set("layout.locked", locked);
    Settings::instance().save();
}

void MainWindow::resetPanels() {
    for (QDockWidget *d : allDocks()) d->setFloating(false);
    restoreState(m_defaultState);
    setPanelsLocked(false);
    notify(tr("Docks reset to the default layout"), false);
}

void MainWindow::fillCollectionsMenu(QMenu *menu) {
    menu->clear();
    auto &e = Engine::instance();
    menu->addAction(tr("New…"), this, [this] {
        auto &en = Engine::instance();
        const QString n = QInputDialog::getText(this, tr("New scene collection"), tr("Name:"), QLineEdit::Normal, tr("My scenes"));
        if (n.trimmed().isEmpty()) return;
        en.saveCollection(en.currentCollectionPath());
        en.loadStarterCollection();
        const QString path = Paths::sceneCollections() + "/" + QString(n).replace(QRegularExpression("[^\\w -]"), "_") + ".json";
        en.saveCollection(path);
        en.loadCollection(path);
        refreshTitle();
    });
    menu->addAction(tr("Duplicate…"), this, [this] {
        auto &en = Engine::instance();
        const QString n = QInputDialog::getText(this, tr("Duplicate scene collection"), tr("Name:"), QLineEdit::Normal,
                                                QFileInfo(en.currentCollectionPath()).completeBaseName() + tr(" copy"));
        if (n.trimmed().isEmpty()) return;
        const QString path = Paths::sceneCollections() + "/" + QString(n).replace(QRegularExpression("[^\\w -]"), "_") + ".json";
        en.saveCollection(path);
        en.loadCollection(path);
        refreshTitle();
    });
    menu->addAction(tr("Import…"), this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Import scene collection"), QString(), "*.json");
        if (f.isEmpty()) return;
        const QString dest = Paths::sceneCollections() + "/" + QFileInfo(f).fileName();
        QFile::remove(dest); QFile::copy(f, dest);
        auto &en = Engine::instance();
        en.saveCollection(en.currentCollectionPath());
        if (!en.loadCollection(dest)) QMessageBox::warning(this, tr("Import"), tr("That file isn't a valid scene collection."));
        refreshTitle();
    });
    menu->addAction(tr("Export…"), this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Export scene collection"), "scenes.json", "*.json");
        if (!f.isEmpty() && !Engine::instance().saveCollection(f)) QMessageBox::warning(this, tr("Export"), tr("Could not write the file."));
    });
    menu->addSeparator();
    QDirIterator it(Paths::sceneCollections(), {"*.json"}, QDir::Files);
    QStringList files;
    while (it.hasNext()) files << it.next();
    files.sort(Qt::CaseInsensitive);
    for (const QString &f : files) {
        QAction *a = menu->addAction(QFileInfo(f).completeBaseName());
        a->setCheckable(true);
        a->setChecked(QFileInfo(f) == QFileInfo(e.currentCollectionPath()));
        connect(a, &QAction::triggered, this, [this, f] {
            auto &en = Engine::instance();
            if (QFileInfo(f) == QFileInfo(en.currentCollectionPath())) return;
            en.saveCollection(en.currentCollectionPath());
            if (!en.loadCollection(f)) QMessageBox::warning(this, tr("Scene collection"), tr("That file isn't a valid scene collection."));
            refreshTitle();
        });
    }
}

void MainWindow::buildMenus() {
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Show Recordings"), this, [this] { openWindow("recordings"); });
    file->addAction(tr("Open Recordings Folder"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::instance().s("recording.path"))); });
    file->addSeparator();
    file->addAction(tr("Settings"), this, [this] { openSettings(); }, QKeySequence::Preferences);
    file->addAction(tr("Show Settings Folder"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::dataRoot())); });
    file->addSeparator();
    file->addAction(tr("Exit"), this, &QWidget::close, QKeySequence::Quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *u = m_preview->undoStack()->createUndoAction(this, tr("Undo"));
    u->setShortcut(QKeySequence::Undo);
    QAction *r = m_preview->undoStack()->createRedoAction(this, tr("Redo"));
    r->setShortcut(QKeySequence::Redo);
    edit->addAction(u); edit->addAction(r);
    edit->addSeparator();
    edit->addAction(tr("Edit Transform…"), this, [this] { if (auto *i = m_preview->selectedItem()) (new TransformDialog(i, m_preview, this))->show(); }, QKeySequence(Qt::CTRL | Qt::Key_E));
    edit->addAction(tr("Fit to Screen"), this, [this] {
        if (auto *i = m_preview->selectedItem()) { const auto b = ItemTransform::read(i); Engine::instance().fitItemToCanvas(i); m_preview->pushTransformUndo(i, b, tr("Fit to screen")); }
    }, QKeySequence(Qt::CTRL | Qt::Key_F));
    edit->addSeparator();
    QAction *snap = edit->addAction(tr("Snapping"));
    snap->setCheckable(true); snap->setChecked(true);
    connect(snap, &QAction::toggled, m_preview, &PreviewWidget::setSnapping);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *fs = view->addAction(tr("Fullscreen Interface"));
    fs->setShortcut(QKeySequence(Qt::Key_F11));
    connect(fs, &QAction::triggered, this, [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    m_studioAction = view->addAction(tr("Studio Mode"));
    m_studioAction->setCheckable(true);
    connect(m_studioAction, &QAction::toggled, this, &MainWindow::setStudioMode);
    QAction *safe = view->addAction(tr("Safe Areas"));
    safe->setCheckable(true);
    connect(safe, &QAction::toggled, m_preview, &PreviewWidget::setSafeAreas);
    QAction *sb = view->addAction(tr("Status Bar"));
    sb->setCheckable(true); sb->setChecked(true);
    connect(sb, &QAction::toggled, statusBar(), &QWidget::setVisible);
    view->addSeparator();
    view->addAction(tr("Fullscreen Projector (Program)"), this, &MainWindow::openFullscreenProjector, QKeySequence(Qt::CTRL | Qt::Key_F11));
    view->addAction(tr("Stream Analytics"), this, [this] { openWindow("analytics"); });

    QMenu *docks = menuBar()->addMenu(tr("&Docks"));
    connect(docks, &QMenu::aboutToShow, this, [this, docks] { fillDocksMenu(docks); });

    QMenu *coll = menuBar()->addMenu(tr("Scene &Collection"));
    connect(coll, &QMenu::aboutToShow, this, [this, coll] { fillCollectionsMenu(coll); });

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("Widget Gallery…"), this, [this] { openWindow("widgets"); });
    tools->addAction(tr("Media Library…"), this, [this] { openWindow("media"); });
    tools->addAction(tr("Effects…"), this, [this] { openWindow("effects"); });
    tools->addAction(tr("Transitions…"), this, [this] { openWindow("transitions"); });
    tools->addAction(tr("Chat && Moderation…"), this, [this] { openWindow("chat"); });
    tools->addAction(tr("Activity Feed…"), this, [this] { openWindow("activity"); });
    tools->addAction(tr("Plugins…"), this, [this] { openWindow("plugins"); });
    tools->addSeparator();
    tools->addAction(tr("Auto-Configuration Wizard"), this, &MainWindow::runFirstTimeWizard);
    tools->addAction(tr("Microphone Test"), this, [this] { openMicTest(this); });
    tools->addAction(tr("Developer Credentials (Twitch / YouTube)…"), this, [this] { DeveloperCredentialsDialog(QString(), this).exec(); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("User Guide"), this, [] {
        const QString dir = QCoreApplication::applicationDirPath() + "/../../docs/";
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo::exists(dir + "USER-GUIDE.html") ? dir + "USER-GUIDE.html" : dir + "USER-GUIDE.md"));
    });
    QMenu *logs = help->addMenu(tr("Log Files"));
    logs->addAction(tr("Show Log Files"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::logs())); });
    logs->addAction(tr("View Current Log"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Logger::currentLogFile())); });
    help->addAction(tr("Crash Reports"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::crashes())); });
    help->addSeparator();
    help->addAction(tr("Check for Updates"), this, [] { Updater::instance().check(true); });
    help->addAction(tr("About KovaLive!"), this, [this] {
        QMessageBox::about(this, tr("About"), tr("<h2>%1 %2</h2><p>A gothic, native live-streaming studio for Windows.</p>"
            "<p>Built with Qt 6 and libobs (OBS Studio's engine). VST3 support by obs-vst3 (GPL-3.0-or-later) using the "
            "Steinberg VST3 SDK (MIT). This program is free software under the GNU GPL.</p>"
            "<p>Logs: %3</p>").arg(BC_APP_NAME, BC_APP_VERSION, QDir::toNativeSeparators(Paths::logs())));
    });
}

void MainWindow::buildStatusBar() {
    m_statMsg = new QLabel; m_statMsg->setObjectName("Muted");
    m_statDrop = new QLabel; m_statLive = new QLabel; m_statRec = new QLabel; m_statCpu = new QLabel; m_statKbps = new QLabel;
    statusBar()->addWidget(m_statMsg, 1);
    for (QLabel *l : {m_statDrop, m_statLive, m_statRec, m_statCpu, m_statKbps}) { l->setMinimumWidth(40); statusBar()->addPermanentWidget(l); }
}

void MainWindow::registerHotkeys() {
    auto &hk = HotkeyManager::instance();
    hk.setHostWindow(this);
    hk.registerAction("stream.toggle", tr("Start / stop streaming"), tr("Streaming"), [this] { Engine::instance().isStreaming() ? endLive() : goLive(); });
    hk.registerAction("record.toggle", tr("Start / stop recording"), tr("Recording"), [this] { toggleRecording(); });
    hk.registerAction("record.pause", tr("Pause / resume recording"), tr("Recording"), [] { auto &e = Engine::instance(); e.pauseRecording(!e.isRecordingPaused()); });
    hk.registerAction("replay.save", tr("Save replay"), tr("Recording"), [] { Engine::instance().saveReplay(); });
    hk.registerAction("replay.toggle", tr("Start / stop replay buffer"), tr("Recording"), [this] { toggleReplayBuffer(); });
    hk.registerAction("vcam.toggle", tr("Start / stop virtual camera"), tr("Virtual camera"), [this] { toggleVirtualCam(); });
    hk.registerAction("mic.mute", tr("Mute microphone"), tr("Audio"), [] { if (auto *m = Engine::instance().micAudio()) obs_source_set_muted(m, true); });
    hk.registerAction("mic.unmute", tr("Unmute microphone"), tr("Audio"), [] { if (auto *m = Engine::instance().micAudio()) obs_source_set_muted(m, false); });
    hk.registerAction("mic.toggle", tr("Toggle microphone mute"), tr("Audio"), [] { if (auto *m = Engine::instance().micAudio()) obs_source_set_muted(m, !obs_source_muted(m)); });
    hk.registerAction("source.toggle", tr("Show / hide selected source"), tr("Sources"), [this] { if (auto *i = m_preview->selectedItem()) obs_sceneitem_set_visible(i, !obs_sceneitem_visible(i)); });
    hk.registerAction("media.playpause", tr("Play / pause selected media"), tr("Sources"), [this] { if (auto *i = m_preview->selectedItem()) obs_source_media_play_pause(obs_sceneitem_get_source(i), obs_source_media_get_state(obs_sceneitem_get_source(i)) == OBS_MEDIA_STATE_PLAYING); });
    hk.registerAction("alert.test", tr("Trigger test alert"), tr("Alerts"), [] { PlatformHub::instance().emitTestEvent("follow"); });
    // widgets (apply to every widget of that kind)
    auto &wm = WidgetManager::instance();
    hk.registerAction("widget.counter.inc", tr("Counters +1"), tr("Widgets"), [&wm] { wm.forEachOfKind("counter", [&wm](obs_source_t *s) { wm.adjustCounter(s, +1, 0); }); });
    hk.registerAction("widget.counter.dec", tr("Counters −1"), tr("Widgets"), [&wm] { wm.forEachOfKind("counter", [&wm](obs_source_t *s) { wm.adjustCounter(s, -1, 0); }); });
    hk.registerAction("widget.counter.reset", tr("Counters reset"), tr("Widgets"), [&wm] { wm.forEachOfKind("counter", [&wm](obs_source_t *s) { wm.resetCounter(s); }); });
    hk.registerAction("widget.win", tr("Win +1"), tr("Widgets"), [&wm] { wm.forEachOfKind("winloss", [&wm](obs_source_t *s) { wm.adjustCounter(s, +1, 0); }); });
    hk.registerAction("widget.loss", tr("Loss +1"), tr("Widgets"), [&wm] { wm.forEachOfKind("winloss", [&wm](obs_source_t *s) { wm.adjustCounter(s, 0, +1); }); });
    hk.registerAction("widget.giveaway.draw", tr("Giveaway: draw a winner"), tr("Widgets"), [&wm] { wm.forEachOfKind("giveaway", [&wm](obs_source_t *s) { wm.drawGiveawayWinner(s); }); });
    hk.registerAction("widget.qna.next", tr("Q&A: next question"), tr("Widgets"), [&wm] { wm.forEachOfKind("qna", [&wm](obs_source_t *s) { wm.nextQuestion(s); }); });
    hk.registerAction("widget.subathon.add", tr("Subathon: add 5 minutes"), tr("Widgets"), [&wm] { wm.forEachOfKind("subathon", [&wm](obs_source_t *s) { wm.addSubathonSeconds(s, 300); }); });
    for (const QString &n : Engine::instance().sceneOrder())
        hk.registerAction("scene." + n, tr("Switch to \"%1\"").arg(n), tr("Scenes"), [n] { Engine::instance().transitionTo(n); });
    hk.applyAll();
}

void MainWindow::openSettings(const QString &category) {
    m_settings->load();
    m_streamPage->reloadDestinations();
    m_streamPage->reloadAccounts();
    if (!category.isEmpty()) m_settings->showCategory(category);
    m_settingsDlg->show();
    m_settingsDlg->raise();
    m_settingsDlg->activateWindow();
}

void MainWindow::openWindow(const QString &which) {
    const QString id = which.section(':', 0, 0), arg = which.section(':', 1);   // e.g. "widgets:Time"
    QDialog *dlg = m_windows.value(id);
    if (!dlg) {
        QWidget *page = nullptr;
        QString title;
        QSize size(1100, 760);
        if (id == "widgets") { page = new WidgetsPage(m_sources); title = tr("Widget Gallery"); size = QSize(1180, 820); }
        else if (id == "media") { page = new MediaLibraryPage; title = tr("Media Library"); }
        else if (id == "effects") { page = new EffectsPage; title = tr("Effects"); }
        else if (id == "transitions") { page = new TransitionsPage; title = tr("Transitions"); size = QSize(900, 700); }
        else if (id == "recordings") { page = new RecordingPage; title = tr("Recordings"); }
        else if (id == "analytics") { page = new AnalyticsPage; title = tr("Stream Analytics"); }
        else if (id == "plugins") { page = new PluginsPage; title = tr("Plugins"); size = QSize(900, 640); }
        else if (id == "chat") { page = new ChatPage(false); title = tr("Chat & Moderation"); size = QSize(900, 720); }
        else if (id == "activity") { page = new ActivityPage(false); title = tr("Activity Feed"); size = QSize(760, 720); }
        if (!page) return;
        dlg = new QDialog(this, Qt::Window);
        dlg->setWindowTitle(title);
        auto *l = new QVBoxLayout(dlg);
        l->setContentsMargins(12, 10, 12, 10);
        l->addWidget(page);
        dlg->resize(size);
        m_windows.insert(id, dlg);
    }
    // refresh content that may have changed while the window was closed
    QWidget *page = dlg->layout()->itemAt(0)->widget();
    if (auto *r = qobject_cast<RecordingPage *>(page)) r->reloadLibrary();
    if (auto *a = qobject_cast<AnalyticsPage *>(page)) a->refresh();
    if (auto *pl = qobject_cast<PluginsPage *>(page)) pl->reload();
    if (auto *m = qobject_cast<MediaLibraryPage *>(page)) m->reload();
    if (auto *ef = qobject_cast<EffectsPage *>(page)) ef->reloadSources();
    if (auto *w = qobject_cast<WidgetsPage *>(page); w && !arg.isEmpty()) w->showCategory(arg);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::setStudioMode(bool on) {
    if (Engine::instance().studioMode() != on) Engine::instance().setStudioMode(on);
    m_program->setVisible(on);
    m_transitionCol->setVisible(on);
    { QSignalBlocker b(m_studioBtn); m_studioBtn->setChecked(on); }
    if (m_studioAction) { QSignalBlocker b(m_studioAction); m_studioAction->setChecked(on); }
}

void MainWindow::refreshControls() {
    auto &e = Engine::instance();
    const bool live = e.isStreaming();
    m_liveBtn->setText(live ? tr("Stop Streaming") : tr("Start Streaming"));
    m_liveBtn->setProperty("live", live);
    m_liveBtn->style()->unpolish(m_liveBtn); m_liveBtn->style()->polish(m_liveBtn);
    m_recBtn->setText(e.isRecording() ? tr("Stop Recording") : tr("Start Recording"));
    m_pauseBtn->setEnabled(e.isRecording());
    m_pauseBtn->setIcon(icons::get(e.isRecordingPaused() ? "play" : "pause"));
    m_pauseBtn->setToolTip(e.isRecordingPaused() ? tr("Resume recording") : tr("Pause recording"));
    m_replayBtn->setText(e.isReplayBufferActive() ? tr("Stop Replay Buffer") : tr("Start Replay Buffer"));
    m_saveReplayBtn->setEnabled(e.isReplayBufferActive());
    m_vcamBtn->setText(e.isVirtualCamActive() ? tr("Stop Virtual Camera") : tr("Start Virtual Camera"));
    m_vcamBtn->setToolTip(e.virtualCamAvailable() ? QString() : tr("Virtual camera component not installed"));
    const bool busy = live || e.isRecording();
    m_header->setAnimating(Settings::instance().b("appearance.ornaments") &&
                           !(busy && Settings::instance().b("general.reduceEffectsWhileLive")));
    if (m_tray) m_tray->setToolTip(live ? tr("KovaLive — LIVE") : QStringLiteral(BC_APP_NAME));
    m_streamPage->refreshStatus();
    tick();
}

void MainWindow::tick() {
    auto &h = SessionHistory::instance();
    h.sample();
    auto &e = Engine::instance();
    const auto st = e.stats();
    double kb = 0; int dropped = 0, total = 0;
    for (const QString &id : e.activeDestinations()) { const auto s = e.destinationStatus(id); kb += s.kbps; dropped += s.droppedFrames; total += s.totalFrames; }
    auto hms = [](qint64 s) { return QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0')); };
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_statDrop->setText(tr("Dropped Frames %1 (%2%)").arg(dropped).arg(total ? 100.0 * dropped / total : 0.0, 0, 'f', 1));
    m_statLive->setText(QStringLiteral("● LIVE: ") + (h.liveStartMs ? hms((now - h.liveStartMs) / 1000) : QStringLiteral("00:00:00")));
    m_statLive->setStyleSheet(e.isStreaming() ? "color:#E0606F;" : "");
    const qint64 recStart = WidgetManager::instance().recordStartMs();
    m_statRec->setText(QStringLiteral("● REC: ") + (e.isRecording() && recStart ? hms((now - recStart) / 1000) : QStringLiteral("00:00:00")));
    m_statRec->setStyleSheet(e.isRecording() ? "color:#E0606F;" : "");
    m_statCpu->setText(tr("CPU: %1%, %2 fps").arg(h.lastCpu, 0, 'f', 1).arg(st.activeFps, 0, 'f', 2));
    const double dropPct = total ? 100.0 * dropped / total : 0;
    const QString dot = !e.isStreaming() ? "#4A4050" : dropPct > 2 ? "#C8283F" : dropPct > 0.5 ? "#C68E2F" : "#3F8F7A";
    m_statKbps->setText(QStringLiteral("<span style='color:%1'>■</span> %2 kb/s").arg(dot).arg(int(kb)));
    m_stats->refresh();
    if (!m_transPanel->isAncestorOf(QApplication::focusWidget())) m_transPanel->sync();
    if (m_settingsDlg && m_settingsDlg->isVisible()) m_streamPage->refreshStatus();
    if (QDialog *a = m_windows.value("analytics"); a && a->isVisible())
        if (auto *ap = qobject_cast<AnalyticsPage *>(a->layout()->itemAt(0)->widget())) ap->refresh();
}

void MainWindow::notify(const QString &text, bool error) {
    m_statMsg->setText((error ? "⚠ " : "☾ ") + text);
    QTimer::singleShot(9000, m_statMsg, [this, text] { if (m_statMsg->text().endsWith(text)) m_statMsg->clear(); });
    if (Settings::instance().b("general.notifications") && m_tray && (error || !isActiveWindow()))
        m_tray->showMessage(QStringLiteral(BC_APP_NAME), text, error ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 6000);
    if (error) qWarning("%s", qPrintable(text));
}

void MainWindow::goLive() {
    auto &hub = PlatformHub::instance();
    if (Engine::instance().isStreaming()) return;
    bool any = false;
    for (const auto &d : hub.destinations()) any |= d.enabled;
    if (!any) {
        QMessageBox::information(this, tr("Start streaming"), tr("Add a streaming destination first (Settings → Stream)."));
        openSettings(tr("Stream"));
        return;
    }
    m_liveBtn->setEnabled(false);
    m_liveBtn->setText(tr("Connecting…"));
    hub.goLive(m_streamPage->streamTitle(), [this](bool ok, const QString &msg) {
        m_liveBtn->setEnabled(true);
        refreshControls();
        if (!ok) QMessageBox::warning(this, tr("Couldn't go live"), msg.isEmpty() ? tr("No destination could start.") : msg);
        else if (!msg.isEmpty()) notify(tr("Live, with warnings: %1").arg(msg), true);
        else notify(tr("You're live. Have a great stream."), false);
    });
}

void MainWindow::endLive() {
    if (Settings::instance().b("general.confirmStopStream") &&
        QMessageBox::question(this, tr("Stop streaming"), tr("End your stream on all platforms?")) != QMessageBox::Yes) return;
    PlatformHub::instance().endLive();
}

void MainWindow::toggleRecording() {
    auto &e = Engine::instance();
    QString err;
    if (e.isRecording()) e.stopRecording();
    else if (!e.startRecording(&err)) QMessageBox::warning(this, tr("Recording"), err);
}

void MainWindow::toggleReplayBuffer() {
    auto &e = Engine::instance();
    QString err;
    if (e.isReplayBufferActive()) e.stopReplayBuffer();
    else if (!e.startReplayBuffer(&err)) QMessageBox::warning(this, tr("Replay buffer"), err);
}

void MainWindow::toggleVirtualCam() {
    auto &e = Engine::instance();
    QString err;
    if (e.isVirtualCamActive()) e.stopVirtualCam();
    else if (!e.startVirtualCam(&err)) QMessageBox::warning(this, tr("Virtual camera"), err);
}

void MainWindow::openFullscreenProjector() {
    auto *w = new ProjectorWindow;
    const auto screens = QGuiApplication::screens();
    QScreen *target = screens.size() > 1 ? screens.last() : screens.first();
    w->setGeometry(target->geometry());
    w->showFullScreen();
}

void MainWindow::saveLayoutAs() {
    const QString n = QInputDialog::getText(this, tr("Save layout"), tr("Layout name:"));
    if (n.trimmed().isEmpty()) return;
    QVariantMap m = Settings::instance().get("layout.saved").toMap();
    m.insert(n.trimmed(), QString::fromLatin1(saveState().toBase64()));
    Settings::instance().set("layout.saved", m);
    Settings::instance().save();
}

void MainWindow::loadLayout(const QString &name) {
    const QByteArray st = QByteArray::fromBase64(Settings::instance().get("layout.saved").toMap().value(name).toString().toLatin1());
    if (!st.isEmpty()) restoreState(st);
}

void MainWindow::runFirstTimeWizard() {
    SetupWizard wiz(this);
    wiz.exec();
    m_streamPage->reloadDestinations();
    m_settings->load();
    theme::apply(*qApp, Settings::instance().s("appearance.accent"), Settings::instance().i("appearance.fontPt"));
}

void MainWindow::showCrashRecoveryNotice(const QString &details) {
    QMessageBox box(this);
    box.setWindowTitle(tr("Welcome back"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("KovaLive didn't close normally last time. Your scenes were auto-saved and have been restored. "
                   "MKV recordings made before the crash are still playable."));
    box.setDetailedText(details);
    auto *logs = box.addButton(tr("Open crash reports"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton *>(logs)) QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::crashes()));
}

void MainWindow::closeEvent(QCloseEvent *ev) {
    auto &e = Engine::instance();
    if (!m_quitting && (e.isStreaming() || e.isRecording())) {
        if (QMessageBox::question(this, tr("Exit"), tr("You're still live or recording. Stop everything and exit?")) != QMessageBox::Yes) { ev->ignore(); return; }
    }
    auto &s = Settings::instance();
    s.set("layout.main", QString::fromLatin1(saveState().toBase64()));
    s.set("layout.geometry", QString::fromLatin1(saveGeometry().toBase64()));
    s.save();
    for (QDialog *d : std::as_const(m_windows)) d->close();
    if (m_settingsDlg) m_settingsDlg->close();
    if (m_tray) m_tray->hide();
    ev->accept();
}

} // namespace bc
