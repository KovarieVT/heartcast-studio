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
#include "ui/KawaiiPainter.h"
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
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <iterator>

namespace bc {

// Brand mark in the nav rail. The twinkle pauses while live/recording if
// "reduce effects while live" is on, so decoration never competes with encoding.
class BrandHeader : public QWidget {
public:
    explicit BrandHeader(QWidget *p = nullptr) : QWidget(p), m_logo(QPixmap(":/icons/logo_256.png").scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation)) {
        setFixedHeight(84);
        connect(&m_timer, &QTimer::timeout, this, [this] { m_t += 0.1; update(); });
        m_timer.start(100);
    }
    void setAnimating(bool on) { on ? m_timer.start(100) : m_timer.stop(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(14, 14, m_logo);
        QFont f("Nunito"); f.setPointSize(13); f.setWeight(QFont::ExtraBold);
        p.setFont(f); p.setPen(theme::DeepRose);
        p.drawText(QRectF(78, 20, width() - 80, 26), Qt::AlignLeft | Qt::AlignVCenter, "HeartCast");
        QFont s("Nunito"); s.setPointSize(9); p.setFont(s); p.setPen(theme::MintAccent.darker(130));
        p.drawText(QRectF(78, 44, width() - 80, 20), Qt::AlignLeft | Qt::AlignVCenter, tr("studio ♡"));
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255, int(150 + 100 * std::sin(m_t * 4))));
        p.drawPath(kawaii::sparkle(QPointF(66, 18), 6));
    }
private:
    QPixmap m_logo; QTimer m_timer; double m_t = 0;
};

class ProjectorWindow : public QWidget {
public:
    ProjectorWindow() {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr("HeartCast — Fullscreen program"));
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
    setWindowTitle(QStringLiteral(BC_APP_NAME));
    setWindowIcon(QIcon(":/icons/logo_256.png"));
    resize(1500, 920);
    // OBS/PRISM-style workspace: every panel can be dragged to any edge, stacked,
    // tabbed together (drop onto another panel's title), dragged out as its own
    // window, and dragged back. Tab groups move together.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    // ---- left nav rail
    auto *central = new QWidget;
    auto *h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
    auto *rail = new QWidget; rail->setFixedWidth(206);
    auto *rl = new QVBoxLayout(rail); rl->setContentsMargins(0, 0, 0, 0); rl->setSpacing(0);
    m_header = new BrandHeader;
    rl->addWidget(m_header);
    m_nav = new QListWidget; m_nav->setObjectName("NavRail");
    const QStringList items = {tr("♡  Home"), tr("✦  Studio"), tr("▦  Scenes"), tr("◈  Sources"), tr("♪  Audio Mixer"),
                               tr("✿  Overlays"), tr("❀  Widgets"), tr("➤  Stream Manager"), tr("●  Recording"),
                               tr("▣  Media Library"), tr("✧  Effects"), tr("⇄  Transitions"), tr("✉  Chat"),
                               tr("☆  Activity Feed"), tr("▤  Analytics"), tr("⧉  Plugins"), tr("⚙  Settings")};
    m_nav->addItems(items);
    rl->addWidget(m_nav, 1);
    h->addWidget(rail);

    m_stack = new QStackedWidget;
    h->addWidget(m_stack, 1);
    setCentralWidget(central);

    // ---- studio page (preview / program)
    auto *studio = new QWidget;
    auto *sl = new QVBoxLayout(studio);
    auto *bar = new QHBoxLayout;
    auto *undo = new QToolButton; undo->setText(tr("↶ Undo"));
    auto *redo = new QToolButton; redo->setText(tr("↷ Redo"));
    auto *safe = new QToolButton; safe->setText(tr("Safe areas")); safe->setCheckable(true);
    auto *snap = new QToolButton; snap->setText(tr("Snapping")); snap->setCheckable(true); snap->setChecked(true);
    m_studioBtn = new QPushButton(tr("Studio mode")); m_studioBtn->setCheckable(true);
    auto *proj = new QToolButton; proj->setText(tr("⛶ Fullscreen"));
    auto *panelsBtn = new QToolButton; panelsBtn->setText(tr("▦ Panels")); panelsBtn->setPopupMode(QToolButton::InstantPopup);
    auto *panelsMenu = new QMenu(panelsBtn);
    connect(panelsMenu, &QMenu::aboutToShow, this, [this, panelsMenu] { fillPanelsMenu(panelsMenu); });
    panelsBtn->setMenu(panelsMenu);
    for (QWidget *w : {static_cast<QWidget *>(undo), static_cast<QWidget *>(redo), static_cast<QWidget *>(safe), static_cast<QWidget *>(snap)}) bar->addWidget(w);
    bar->addStretch();
    bar->addWidget(panelsBtn); bar->addWidget(m_studioBtn); bar->addWidget(proj);
    sl->addLayout(bar);
    auto *split = new QSplitter;
    m_preview = new PreviewWidget(PreviewWidget::Edit);
    m_program = new PreviewWidget(PreviewWidget::Program);
    m_transitionCol = new QWidget;
    auto *tc = new QVBoxLayout(m_transitionCol);
    tc->addStretch();
    auto *tbtn = new QPushButton(tr("Transition\n➜")); tbtn->setProperty("accent", "pink"); tbtn->setMinimumHeight(70);
    tc->addWidget(tbtn);
    tc->addStretch();
    split->addWidget(m_preview); split->addWidget(m_transitionCol); split->addWidget(m_program);
    m_program->hide(); m_transitionCol->hide();
    sl->addWidget(split, 1);
    auto *legend = new QLabel(tr("Drag to move · drag a corner to resize (Shift = free aspect) · Alt = no snapping · arrows nudge · double-click for properties  ·  Panels: drag a title bar to rearrange, drop onto another panel to tab, double-click to pop out"));
    legend->setObjectName("Muted");
    legend->setWordWrap(true);
    sl->addWidget(legend);

    connect(undo, &QToolButton::clicked, m_preview->undoStack(), &QUndoStack::undo);
    connect(redo, &QToolButton::clicked, m_preview->undoStack(), &QUndoStack::redo);
    connect(safe, &QToolButton::toggled, m_preview, &PreviewWidget::setSafeAreas);
    connect(snap, &QToolButton::toggled, m_preview, &PreviewWidget::setSnapping);
    connect(proj, &QToolButton::clicked, this, &MainWindow::openFullscreenProjector);
    connect(tbtn, &QPushButton::clicked, this, [] { auto &e = Engine::instance(); e.transitionTo(e.previewScene()); });
    connect(m_studioBtn, &QPushButton::toggled, this, [this](bool on) {
        Engine::instance().setStudioMode(on);
        m_program->setVisible(on); m_transitionCol->setVisible(on);
    });
    connect(m_preview, &PreviewWidget::itemDoubleClicked, this, [this](obs_sceneitem_t *i) { (new PropertiesDialog(obs_sceneitem_get_source(i), this))->show(); });

    m_scenes = new ScenesPanel;
    m_sources = new SourcesPanel(m_preview);
    m_mixer = new MixerPanel;
    connect(m_scenes, &ScenesPanel::sceneActivated, this, [](const QString &n) { Engine::instance().setPreviewScene(n); });
    auto syncPreview = [this] { m_preview->setSceneName(Engine::instance().previewScene()); m_sources->reload(); };
    connect(&Engine::instance(), &Engine::previewSceneChanged, this, syncPreview);
    connect(&Engine::instance(), &Engine::programSceneChanged, this, syncPreview);
    syncPreview();

    // ---- pages
    m_home = new HomePage;
    m_streamPage = new StreamManagerPage;
    m_recordPage = new RecordingPage;
    m_analytics = new AnalyticsPage;
    m_settings = new SettingsPage;
    m_plugins = new PluginsPage;
    auto wrap = [](QWidget *w) { w->setContentsMargins(14, 10, 14, 10); return w; };
    m_stack->addWidget(wrap(m_home));                                  // 0
    m_stack->addWidget(studio);                                        // 1
    m_stack->addWidget(wrap(new WidgetsPage(m_sources, true)));        // 2 overlays
    m_stack->addWidget(wrap(new WidgetsPage(m_sources, false)));       // 3 widgets
    m_stack->addWidget(wrap(m_streamPage));                            // 4
    m_stack->addWidget(wrap(m_recordPage));                            // 5
    m_stack->addWidget(wrap(new MediaLibraryPage));                    // 6
    m_stack->addWidget(wrap(new EffectsPage));                         // 7
    m_stack->addWidget(wrap(new TransitionsPage));                     // 8
    m_stack->addWidget(wrap(new ChatPage));                            // 9
    m_stack->addWidget(wrap(new ActivityPage));                        // 10
    m_stack->addWidget(wrap(m_analytics));                             // 11
    m_stack->addWidget(wrap(m_plugins));                               // 12
    m_stack->addWidget(wrap(m_settings));                              // 13

    connect(m_home, &HomePage::goLiveRequested, this, &MainWindow::goLive);
    connect(m_streamPage, &StreamManagerPage::goLiveRequested, this, &MainWindow::goLive);
    connect(m_settings, &SettingsPage::rerunWizard, this, &MainWindow::runFirstTimeWizard);
    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::navigate);

    buildDocks();
    buildMenus();
    buildStatusBar();
    registerHotkeys();

    // tray
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(QIcon(":/icons/logo_256.png"), this);
        auto *tm = new QMenu(this);
        tm->addAction(tr("Show HeartCast"), this, [this] { showNormal(); raise(); activateWindow(); });
        tm->addAction(tr("Go Live / End Stream"), this, [this] { Engine::instance().isStreaming() ? endLive() : goLive(); });
        tm->addAction(tr("Start / Stop Recording"), this, &MainWindow::toggleRecording);
        tm->addSeparator();
        tm->addAction(tr("Quit"), this, &QWidget::close);
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
    connect(&e, &Engine::replaySaved, this, [this](const QString &p) { notify(tr("Replay saved ✦ %1").arg(QFileInfo(p).fileName()), false); });
    connect(&e, &Engine::scenesChanged, this, &MainWindow::registerHotkeys);
    connect(&PlatformHub::instance(), &PlatformHub::notice, this, &MainWindow::notify);
    connect(&PluginManager::instance(), &PluginManager::startStreamingRequested, this, &MainWindow::goLive);
    connect(&Updater::instance(), &Updater::updateAvailable, this, [this](const UpdateInfo &u) {
        if (QMessageBox::question(this, tr("Update available ♡"), tr("HeartCast %1 is available.\n\n%2\n\nDownload and install now?").arg(u.version.toString(), u.notes)) == QMessageBox::Yes)
            Updater::instance().downloadAndInstall(u);
    });
    connect(&Updater::instance(), &Updater::upToDate, this, [this] { notify(tr("You're on the latest version ♡"), false); });
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
    connect(autosave, &QTimer::timeout, this, [] {
        auto &en = Engine::instance();
        if (!en.currentCollectionPath().isEmpty()) en.saveCollection(en.currentCollectionPath());
        Settings::instance().save();
    });
    connect(autosave, &QTimer::timeout, this, [this] {   // keep the panel layout safe even after a crash
        if (m_currentPage == 1) {
            Settings::instance().set("layout.main", QString::fromLatin1(saveState().toBase64()));
            Settings::instance().save();
        }
    });
    autosave->start(qMax(1, Settings::instance().i("general.autosaveMinutes")) * 60000);

    const QByteArray geo = QByteArray::fromBase64(Settings::instance().s("layout.geometry").toLatin1());
    if (!geo.isEmpty()) restoreGeometry(geo);
    m_nav->setCurrentRow(NavStudio);
    refreshControls();
    PluginManager::instance().setMainWindow(this);
    PluginManager::instance().loadEnabled();
    if (Settings::instance().b("general.checkUpdates")) QTimer::singleShot(8000, [] { Updater::instance().check(false); });
}

QList<QDockWidget *> MainWindow::allDocks() const { return findChildren<QDockWidget *>(); }

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
    // controls
    auto *ctl = new QWidget; auto *cl = new QVBoxLayout(ctl);
    m_liveBtn = new QPushButton(tr("✦ Go Live")); m_liveBtn->setProperty("accent", "pink"); m_liveBtn->setMinimumHeight(42);
    m_recBtn = new QPushButton(tr("● Start Recording"));
    m_pauseBtn = new QPushButton(tr("❚❚ Pause Recording"));
    m_replayBtn = new QPushButton(tr("Start Replay Buffer"));
    auto *saveReplay = new QPushButton(tr("Save Replay ✦")); saveReplay->setProperty("accent", "mint");
    m_vcamBtn = new QPushButton(tr("Start Virtual Camera"));
    auto *mic = new QPushButton(tr("Mic test ♪"));
    for (QWidget *w : {static_cast<QWidget *>(m_liveBtn), static_cast<QWidget *>(m_recBtn), static_cast<QWidget *>(m_pauseBtn),
                       static_cast<QWidget *>(m_replayBtn), static_cast<QWidget *>(saveReplay), static_cast<QWidget *>(m_vcamBtn),
                       static_cast<QWidget *>(mic)}) cl->addWidget(w);
    cl->addStretch();
    m_controlsDock = makeDock(this, tr("Controls"), "dock-controls", ctl);
    connect(m_liveBtn, &QPushButton::clicked, this, [this] { Engine::instance().isStreaming() ? endLive() : goLive(); });
    connect(m_recBtn, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(m_pauseBtn, &QPushButton::clicked, this, [] { auto &e = Engine::instance(); e.pauseRecording(!e.isRecordingPaused()); });
    connect(m_replayBtn, &QPushButton::clicked, this, &MainWindow::toggleReplayBuffer);
    connect(saveReplay, &QPushButton::clicked, this, [this] { if (!Engine::instance().saveReplay()) notify(tr("Start the replay buffer first ♡"), true); });
    connect(m_vcamBtn, &QPushButton::clicked, this, &MainWindow::toggleVirtualCam);
    connect(mic, &QPushButton::clicked, this, [this] { openMicTest(this); });

    // Default arrangement (OBS-like): bottom row Scenes | Sources | Mixer | Transitions | Controls,
    // Chat + Activity tabbed on the right with Stats under them (hidden until you turn them on).
    addDockWidget(Qt::BottomDockWidgetArea, m_scenesDock);
    splitDockWidget(m_scenesDock, m_sourcesDock, Qt::Horizontal);
    splitDockWidget(m_sourcesDock, m_mixerDock, Qt::Horizontal);
    splitDockWidget(m_mixerDock, m_transitionsDock, Qt::Horizontal);
    splitDockWidget(m_transitionsDock, m_controlsDock, Qt::Horizontal);
    addDockWidget(Qt::RightDockWidgetArea, m_chatDock);
    tabifyDockWidget(m_chatDock, m_activityDock);
    splitDockWidget(m_chatDock, m_statsDock, Qt::Vertical);
    m_chatDock->raise();
    resizeDocks({m_scenesDock, m_sourcesDock, m_mixerDock, m_transitionsDock, m_controlsDock}, {240, 320, 420, 200, 220}, Qt::Horizontal);
    resizeDocks({m_scenesDock}, {300}, Qt::Vertical);
    resizeDocks({m_chatDock}, {360}, Qt::Horizontal);
    for (QDockWidget *d : {m_chatDock, m_activityDock, m_statsDock}) d->hide();
    m_defaultState = saveState();

    const QByteArray st = QByteArray::fromBase64(Settings::instance().s("layout.main").toLatin1());
    if (!st.isEmpty()) restoreState(st);
    setPanelsLocked(Settings::instance().b("layout.locked"));
}

void MainWindow::fillPanelsMenu(QMenu *menu) {
    menu->clear();
    menu->addSection(tr("Show panels"));
    QList<QDockWidget *> docks = allDocks();
    std::sort(docks.begin(), docks.end(), [](QDockWidget *a, QDockWidget *b) { return a->windowTitle().localeAwareCompare(b->windowTitle()) < 0; });
    for (QDockWidget *d : docks) {
        QAction *a = menu->addAction(d->windowTitle());
        a->setCheckable(true);
        a->setChecked(d->isVisible());
        connect(a, &QAction::triggered, this, [this, d](bool on) {
            if (m_currentPage != 1) navigate(NavStudio);
            d->setVisible(on);
            if (on) d->raise();
        });
    }
    menu->addSection(tr("Arrange"));
    QAction *lock = menu->addAction(tr("Lock panels"));
    lock->setCheckable(true);
    lock->setChecked(Settings::instance().b("layout.locked"));
    connect(lock, &QAction::toggled, this, &MainWindow::setPanelsLocked);
    menu->addAction(tr("Reset panels to default"), this, &MainWindow::resetPanels);
    menu->addSection(tr("Saved layouts"));
    menu->addAction(tr("Save current layout as…"), this, &MainWindow::saveLayoutAs);
    const QVariantMap saved = Settings::instance().get("layout.saved").toMap();
    for (const QString &n : saved.keys()) menu->addAction(tr("Load \"%1\"").arg(n), this, [this, n] { loadLayout(n); });
    if (!saved.isEmpty()) {
        QMenu *del = menu->addMenu(tr("Delete saved layout"));
        for (const QString &n : saved.keys()) del->addAction(n, this, [n] {
            QVariantMap m = Settings::instance().get("layout.saved").toMap();
            m.remove(n);
            Settings::instance().set("layout.saved", m);
            Settings::instance().save();
        });
    }
}

void MainWindow::setPanelsLocked(bool locked) {
    // Locked panels can still be shown/hidden, but not dragged, undocked or re-tabbed.
    for (QDockWidget *d : allDocks())
        d->setFeatures(locked ? QDockWidget::DockWidgetClosable
                              : QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    Settings::instance().set("layout.locked", locked);
    Settings::instance().save();
}

void MainWindow::resetPanels() {
    if (m_currentPage != 1) navigate(NavStudio);
    for (QDockWidget *d : allDocks()) d->setFloating(false);
    restoreState(m_defaultState);
    setPanelsLocked(false);
    notify(tr("Panels reset to the default layout"), false);
}

void MainWindow::buildMenus() {
    auto &e = Engine::instance();
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QMenu *coll = file->addMenu(tr("Scene collection"));
    coll->addAction(tr("New (starter scenes)"), this, [] {
        auto &e = Engine::instance();
        const QString n = QInputDialog::getText(nullptr, tr("New collection"), tr("Name:"), QLineEdit::Normal, tr("My collection"));
        if (n.isEmpty()) return;
        e.saveCollection(e.currentCollectionPath());
        e.loadStarterCollection();
        const QString path = Paths::sceneCollections() + "/" + QString(n).replace(QRegularExpression("[^\\w -]"), "_") + ".json";
        e.saveCollection(path);
        e.loadCollection(path);
    });
    coll->addAction(tr("Open…"), this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Open scene collection"), Paths::sceneCollections(), "*.json");
        auto &e = Engine::instance();
        if (f.isEmpty()) return;
        e.saveCollection(e.currentCollectionPath());
        if (!e.loadCollection(f)) QMessageBox::warning(this, tr("Open"), tr("That file isn't a valid scene collection."));
    });
    coll->addAction(tr("Save"), this, [] { auto &e = Engine::instance(); e.saveCollection(e.currentCollectionPath()); });
    coll->addAction(tr("Export…"), this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Export scene collection"), "scenes.json", "*.json");
        if (!f.isEmpty() && !Engine::instance().saveCollection(f)) QMessageBox::warning(this, tr("Export"), tr("Could not write the file."));
    });
    coll->addAction(tr("Import…"), this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Import scene collection"), QString(), "*.json");
        if (f.isEmpty()) return;
        const QString dest = Paths::sceneCollections() + "/" + QFileInfo(f).fileName();
        QFile::remove(dest); QFile::copy(f, dest);
        auto &e = Engine::instance();
        e.saveCollection(e.currentCollectionPath());
        if (!e.loadCollection(dest)) QMessageBox::warning(this, tr("Import"), tr("That file isn't a valid scene collection."));
    });
    file->addSeparator();
    file->addAction(tr("Open recordings folder"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::instance().s("recording.path"))); });
    file->addAction(tr("Settings"), this, [this] { navigate(NavSettings); }, QKeySequence::Preferences);
    file->addSeparator();
    file->addAction(tr("Exit"), this, &QWidget::close, QKeySequence::Quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    QAction *u = m_preview->undoStack()->createUndoAction(this, tr("Undo"));
    u->setShortcut(QKeySequence::Undo);
    QAction *r = m_preview->undoStack()->createRedoAction(this, tr("Redo"));
    r->setShortcut(QKeySequence::Redo);
    edit->addAction(u); edit->addAction(r);
    edit->addSeparator();
    edit->addAction(tr("Edit transform…"), this, [this] { if (auto *i = m_preview->selectedItem()) (new TransformDialog(i, m_preview, this))->show(); }, QKeySequence(Qt::CTRL | Qt::Key_E));
    edit->addAction(tr("Fit selected to screen"), this, [this] {
        if (auto *i = m_preview->selectedItem()) { const auto b = ItemTransform::read(i); Engine::instance().fitItemToCanvas(i); m_preview->pushTransformUndo(i, b, tr("Fit to screen")); }
    }, QKeySequence(Qt::CTRL | Qt::Key_F));

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QMenu *panels = menuBar()->addMenu(tr("&Panels"));
    connect(panels, &QMenu::aboutToShow, this, [this, panels] { fillPanelsMenu(panels); });
    view->addSeparator();
    view->addAction(tr("Fullscreen program (projector)"), this, &MainWindow::openFullscreenProjector, QKeySequence(Qt::Key_F11));

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("Microphone test"), this, [this] { openMicTest(this); });
    tools->addAction(tr("Test streaming connections"), this, [this] { navigate(NavStream); });
    tools->addAction(tr("Run setup wizard"), this, &MainWindow::runFirstTimeWizard);
    tools->addAction(tr("Check for updates"), this, [] { Updater::instance().check(true); });
    tools->addAction(tr("Open logs folder"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::logs())); });
    tools->addAction(tr("Open current log"), this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Logger::currentLogFile())); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("User guide"), this, [] {
        const QString dir = QCoreApplication::applicationDirPath() + "/../../docs/";
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo::exists(dir + "USER-GUIDE.html") ? dir + "USER-GUIDE.html" : dir + "USER-GUIDE.md"));
    });
    help->addAction(tr("About HeartCast Studio"), this, [this] {
        QMessageBox::about(this, tr("About"), tr("<h2>%1 %2</h2><p>A cute, native live-streaming studio for Windows.</p>"
            "<p>Built with Qt 6 and libobs (OBS Studio's engine, GPL-2.0-or-later). This program is free software under the GNU GPL v2 or later.</p>"
            "<p>Logs: %3</p>").arg(BC_APP_NAME, BC_APP_VERSION, QDir::toNativeSeparators(Paths::logs())));
    });
    Q_UNUSED(e);
}

void MainWindow::buildStatusBar() {
    m_livePill = new QLabel; m_recPill = new QLabel;
    m_statDur = new QLabel; m_statKbps = new QLabel; m_statDrop = new QLabel; m_statFps = new QLabel; m_statCpu = new QLabel;
    m_statMsg = new QLabel; m_statMsg->setObjectName("Muted");
    statusBar()->addWidget(m_statMsg, 1);
    for (QLabel *l : {m_statDur, m_statKbps, m_statDrop, m_statFps, m_statCpu, m_recPill, m_livePill}) statusBar()->addPermanentWidget(l);
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
    for (const QString &n : Engine::instance().sceneOrder())
        hk.registerAction("scene." + n, tr("Switch to \"%1\"").arg(n), tr("Scenes"), [n] { Engine::instance().transitionTo(n); });
    hk.applyAll();
}

void MainWindow::navigate(int idx) {
    static const int pageFor[] = {0, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    if (idx < 0 || idx >= int(std::size(pageFor))) return;
    const int page = pageFor[idx];
    if (m_nav->currentRow() != idx) { QSignalBlocker b(m_nav); m_nav->setCurrentRow(idx); }
    // Docks belong to the Studio workspace: remember their arrangement when leaving.
    if (m_currentPage == 1 && page != 1) {
        m_studioDockState = saveState();
        for (QDockWidget *d : allDocks()) if (!d->isFloating()) d->hide();   // popped-out panels stay open
    } else if (page == 1 && m_currentPage != 1 && !m_studioDockState.isEmpty()) {
        restoreState(m_studioDockState);
    }
    m_currentPage = page;
    m_stack->setCurrentIndex(page);
    if (idx == NavScenes) { m_scenesDock->show(); m_scenesDock->raise(); }
    if (idx == NavSources) { m_sourcesDock->show(); m_sourcesDock->raise(); }
    if (idx == NavMixer) { m_mixerDock->show(); m_mixerDock->raise(); }
    if (page == 0) m_home->refresh();
    if (page == 11) m_analytics->refresh();
    if (page == 12) m_plugins->reload();
    if (page == 13) m_settings->load();
    if (page == 5) m_recordPage->reloadLibrary();
}

void MainWindow::refreshControls() {
    auto &e = Engine::instance();
    const bool live = e.isStreaming();
    m_liveBtn->setText(live ? tr("■ End Stream") : tr("✦ Go Live"));
    m_liveBtn->setProperty("live", live);
    m_liveBtn->style()->unpolish(m_liveBtn); m_liveBtn->style()->polish(m_liveBtn);
    m_recBtn->setText(e.isRecording() ? tr("■ Stop Recording") : tr("● Start Recording"));
    m_pauseBtn->setEnabled(e.isRecording());
    m_pauseBtn->setText(e.isRecordingPaused() ? tr("▶ Resume Recording") : tr("❚❚ Pause Recording"));
    m_replayBtn->setText(e.isReplayBufferActive() ? tr("Stop Replay Buffer") : tr("Start Replay Buffer"));
    m_vcamBtn->setText(e.isVirtualCamActive() ? tr("Stop Virtual Camera") : tr("Start Virtual Camera"));
    m_vcamBtn->setToolTip(e.virtualCamAvailable() ? QString() : tr("Virtual camera component not installed"));
    auto pill = [](QLabel *l, const QString &k, const QString &t) { l->setProperty("pill", k); l->setText(t); l->style()->unpolish(l); l->style()->polish(l); };
    pill(m_livePill, live ? "live" : "off", live ? tr("● LIVE") : tr("offline"));
    pill(m_recPill, e.isRecording() ? "rec" : "off", e.isRecordingPaused() ? tr("❚❚ paused") : e.isRecording() ? tr("● REC") : tr("not recording"));
    const bool busy = live || e.isRecording();
    m_header->setAnimating(!(busy && Settings::instance().b("general.reduceEffectsWhileLive")));
    if (m_tray) m_tray->setToolTip(live ? tr("HeartCast — LIVE") : QStringLiteral(BC_APP_NAME));
    m_streamPage->refreshStatus();
}

void MainWindow::tick() {
    auto &h = SessionHistory::instance();
    h.sample();
    auto &e = Engine::instance();
    const auto st = e.stats();
    double kb = 0; int dropped = 0, total = 0;
    for (const QString &id : e.activeDestinations()) { const auto s = e.destinationStatus(id); kb += s.kbps; dropped += s.droppedFrames; total += s.totalFrames; }
    m_statCpu->setText(tr("CPU %1%").arg(h.lastCpu, 0, 'f', 0) + (h.lastGpu >= 0 ? tr(" · GPU %1%").arg(h.lastGpu, 0, 'f', 0) : QString()) + tr(" · %1 MB").arg(h.lastRamMB));
    m_statFps->setText(QStringLiteral("%1 fps").arg(st.activeFps, 0, 'f', 1));
    m_statDrop->setText(tr("dropped %1").arg(dropped) + (total ? QStringLiteral(" (%1%)").arg(100.0 * dropped / total, 0, 'f', 1) : QString()));
    m_statKbps->setText(e.isStreaming() ? QStringLiteral("%1 kbps").arg(int(kb)) : QString());
    if (h.liveStartMs) { const qint64 s = (QDateTime::currentMSecsSinceEpoch() - h.liveStartMs) / 1000;
        m_statDur->setText(QStringLiteral("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg((s / 60) % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))); }
    else m_statDur->clear();
    m_stats->refresh();
    if (!m_transPanel->isAncestorOf(QApplication::focusWidget())) m_transPanel->sync();
    if (m_currentPage == 0) m_home->refresh();
    if (m_currentPage == 11) m_analytics->refresh();
    if (m_currentPage == 4) m_streamPage->refreshStatus();
}

void MainWindow::notify(const QString &text, bool error) {
    m_statMsg->setText((error ? "⚠ " : "♡ ") + text);
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
        QMessageBox::information(this, tr("Go live"), tr("Add a streaming destination first ♡"));
        navigate(NavStream);
        return;
    }
    m_liveBtn->setEnabled(false);
    m_liveBtn->setText(tr("Connecting…"));
    hub.goLive(m_streamPage->streamTitle(), [this](bool ok, const QString &msg) {
        m_liveBtn->setEnabled(true);
        refreshControls();
        if (!ok) QMessageBox::warning(this, tr("Couldn't go live"), msg.isEmpty() ? tr("No destination could start.") : msg);
        else if (!msg.isEmpty()) notify(tr("Live, with warnings: %1").arg(msg), true);
        else notify(tr("You're live! ✦ Have a great stream"), false);
    });
}

void MainWindow::endLive() {
    if (Settings::instance().b("general.confirmStopStream") &&
        QMessageBox::question(this, tr("End stream"), tr("End your stream on all platforms?")) != QMessageBox::Yes) return;
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
    if (!st.isEmpty()) { navigate(NavStudio); restoreState(st); }
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
    box.setWindowTitle(tr("Welcome back ♡"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("HeartCast didn't close normally last time. Your scenes were auto-saved and have been restored. "
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
        if (QMessageBox::question(this, tr("Quit"), tr("You're still live or recording. Stop everything and quit?")) != QMessageBox::Yes) { ev->ignore(); return; }
    }
    if (m_currentPage != 1) navigate(NavStudio);
    auto &s = Settings::instance();
    s.set("layout.main", QString::fromLatin1(saveState().toBase64()));
    s.set("layout.geometry", QString::fromLatin1(saveGeometry().toBase64()));
    s.save();
    if (m_tray) m_tray->hide();
    ev->accept();
}

} // namespace bc
